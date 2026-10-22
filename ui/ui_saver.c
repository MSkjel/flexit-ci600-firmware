// Screensaver. A clock on black or over the ESP's picture, with the ESP's animation on top.
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define BAND_H 74  // height of the band behind the text over a picture
#define SHADOW 2   // text shadow offset, px

typedef struct {
  lv_obj_t *clock, *date, *temps;
  lv_obj_t *shade[3];  // shadows of the three, with SAVER_TEXT_SHADOW
  bool over_picture;
  bool small;  // SAVER_TEXT_SMALL: time and temps in the corners, no date
} saver_t;
SCREEN_STATE(saver_t, saver);

static const str_t MONTHS[12] = {S_JANUARY, S_FEBRUARY, S_MARCH,     S_APRIL,   S_MAY,      S_JUNE,
                                 S_JULY,    S_AUGUST,   S_SEPTEMBER, S_OCTOBER, S_NOVEMBER, S_DECEMBER};

// A picture mode without a picture falls back to the clock, with temps if the mode had them
static int saver_plain(int mode) {
  return mode < SAVER_PIC ? mode : mode == SAVER_PIC_ALL ? SAVER_CLOCK_TEMPS : SAVER_CLOCK;
}

// The pictures come from the ESP
int saver_used(val_t v, int mode) {
  (void) v;
  return HAS(FEAT_ESP) ? mode : saver_plain(mode);
}

// Clock, date and maybe temps on the band. Shadows are black, see-through and SHADOW px down and right.
static void band_texts(lv_obj_t *band, lv_obj_t *l[3], bool temps, bool shadow) {
  int d = shadow ? SHADOW : 0;
  lv_color_t c = shadow ? lv_color_black() : lv_color_hex(0xD4DCDE);
  l[0] = mk_label(band, &font_n40, shadow ? c : lv_color_white(), "");
  lv_obj_set_pos(l[0], 14 + d, 2 + d);
  l[1] = mk_label(band, &font_f14, c, "");
  lv_obj_set_pos(l[1], 16 + d, 50 + d);
  l[2] = NULL;
  if (temps) {
    l[2] = mk_label(band, &font_f16, c, "");
    lv_label_set_recolor(l[2], true);
    lv_obj_set_width(l[2], 150);
    lv_obj_set_style_text_align(l[2], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(l[2], SCR_W - 164 + d, 14 + d);
  }
  for (int i = 0; shadow && i < 3; i++)
    if (l[i])
      lv_obj_set_style_text_opa(l[i], LV_OPA_70, 0);
}

// Text over the picture, top or bottom, on a shaded band, with shadows or as it is. Shadows go first so they end
// up behind.
static void over_picture(lv_obj_t *b, bool temps) {
  bool shadow = M.saver_text == SAVER_TEXT_SHADOW;
  lv_obj_t *band = mk_box(b, 0, M.saver_top ? 0 : SCR_H - BAND_H, SCR_W, BAND_H, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(band, M.saver_text == SAVER_TEXT_BAND ? LV_OPA_50 : LV_OPA_TRANSP, 0);
  if (shadow)
    band_texts(band, saver->shade, temps, true);
  lv_obj_t *l[3];
  band_texts(band, l, temps, false);
  saver->clock = l[0];
  saver->date = l[1];
  saver->temps = l[2];
  saver->over_picture = true;
}

// The time in a corner, and the temperatures in the other
static void small_texts(lv_obj_t *b, bool temps) {
  saver->clock = mk_label(b, &font_f20, lv_color_white(), "");
  if (temps)
    saver->temps = mk_label(b, &font_f16, lv_color_white(), "");
  saver->over_picture = saver->small = true;
}

static void saver_build(lv_obj_t *b, intptr_t arg) {
  (void) arg;
  memset(saver, 0, sizeof *saver);
  if (M.screensaver == SAVER_OFF) {  // switched off from the ESP while it shows
    ui_close_after(0);
    return;
  }
  int mode = saver_used(V_SCREENSAVER, M.screensaver);
  lv_obj_t *pic = mode >= SAVER_PIC ? pic_show(b) : NULL;
  if (!pic)
    mode = saver_plain(mode);  // no picture yet
  bool temps = mode == SAVER_CLOCK_TEMPS || mode == SAVER_PIC_ALL;
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
  if (pic) {
    anim_show(b);
    if (mode != SAVER_PIC && M.saver_text == SAVER_TEXT_SMALL)
      small_texts(b, temps);
    else if (mode != SAVER_PIC)  // SAVER_PIC is the picture alone
      over_picture(b, temps);
    return;
  }
  anim_show(b);
  saver->clock = mk_label(b, &font_n64, lv_color_mix(T.text, lv_color_black(), 200), "");
  saver->date = mk_label(b, &font_f16, T.dim, "");
  if (temps) {
    saver->temps = mk_label(b, &font_f16, T.dim, "");
    lv_label_set_recolor(saver->temps, true);
  }
}

// Indoor and outdoor temperatures with coloured numbers. Stacked over a picture, side by side on black.
static void temps_text(lv_obj_t *l, uint32_t in, uint32_t out) {
  char a[16], c[16];
  if (saver->over_picture)
    set_text(l, "%s  #%06x %s#\n%s  #%06x %s#", _(S_IN), (unsigned) in, fmt_temp(a, indoor_temp(), false), _(S_OUT),
             (unsigned) out, fmt_temp(c, M.t_outdoor, false));
  else
    set_text(l, "%s  #%06x %s#      %s  #%06x %s#", _(S_INDOOR), (unsigned) in, fmt_temp(a, indoor_temp(), false),
             _(S_OUTDOOR), (unsigned) out, fmt_temp(c, M.t_outdoor, false));
}

static uint32_t hex(lv_color_t c) { return lv_color_to32(c) & 0xFFFFFF; }

// Moves a few pixels every minute, like the big clock, so nothing stays in one place for hours
static void small_update(void) {
  char a[16], c[16];
  int dx = (M.mi % 5 - 2) * 2, y = M.saver_top ? 4 : -4;
  lv_align_t left = M.saver_top ? LV_ALIGN_TOP_LEFT : LV_ALIGN_BOTTOM_LEFT;
  lv_align_t right = M.saver_top ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_BOTTOM_RIGHT;
  lv_obj_align(saver->clock, left, 10 + dx, y);
  if (!saver->temps)
    return;
  set_text(saver->temps, "%s %s  %s %s", _(S_IN), fmt_temp(a, indoor_temp(), false), _(S_OUT),
           fmt_temp(c, M.t_outdoor, false));
  lv_obj_align(saver->temps, right, -10 + dx, y);
}

static void saver_update(void) {
  if (!saver->clock)
    return;
  if (M.clock_24h)
    set_text(saver->clock, "%02d:%02d", M.hh, M.mi);
  else
    set_text(saver->clock, "%d:%02d", (M.hh + 11) % 12 + 1, M.mi);
  if (saver->date)
    set_text(saver->date, _(S_DATE), tr(DAY_NAMES[M.wday % 7]), M.dd, tr(MONTHS[(M.mo + 11) % 12]));
  if (saver->small) {
    small_update();
    return;
  }
  if (saver->temps)
    temps_text(saver->temps, saver->over_picture ? 0xFFFFFF : hex(T.text),
               saver->over_picture ? 0xFFFFFF : hex(T.cold));
  // shift a few pixels every minute so nothing stays in one place for hours
  int dx = (M.mi % 5 - 2) * 4, dy = (M.mi / 5 % 3 - 1) * 4;
  if (saver->over_picture) {
    if (saver->shade[0]) {  // copy the texts to their shadows
      lv_label_set_text(saver->shade[0], lv_label_get_text(saver->clock));
      lv_label_set_text(saver->shade[1], lv_label_get_text(saver->date));
      if (saver->shade[2])
        temps_text(saver->shade[2], 0, 0);
      lv_obj_set_x(saver->shade[0], 14 + SHADOW + dx / 2);
    }
    lv_obj_set_x(saver->clock, 14 + dx / 2);
    return;
  }
  lv_obj_align(saver->clock, LV_ALIGN_CENTER, dx, (saver->temps ? -34 : -20) + dy);
  lv_obj_align(saver->date, LV_ALIGN_CENTER, dx, (saver->temps ? 18 : 32) + dy);
  if (saver->temps)
    lv_obj_align(saver->temps, LV_ALIGN_CENTER, dx, 60 + dy);
}

const screen_t SCR_SAVER = {.flags = SF_NOBARS, .build = saver_build, .update = saver_update};
