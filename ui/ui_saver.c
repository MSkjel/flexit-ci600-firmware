// Screensaver. A clock on black or over the ESP's picture, with the ESP's animation on top.
#include "ui_int.h"

#define BAND_H 74              // height of the band behind the text over a picture
#define SHADOW 2               // text shadow offset, px
#define PICTURE_TEXT 0xD4DCDE  // date and temperatures over a picture, softer than the clock's white
#define BAND_LINES_W 150       // the ESP's lines over a picture, right of the clock and the date
#define SMALL_LINES_W 200      // and in a corner

typedef struct {
  lv_obj_t *clock, *date, *temps;
  lv_obj_t *lines[SAVER_LINES];                   // the ESP's, in place of the temps
  lv_obj_t *shade[3], *shade_lines[SAVER_LINES];  // shadows of them all, with SAVER_TEXT_SHADOW
  lv_obj_t *group;                                // the texts over a picture, which glide together
  bool over_picture;
  bool small;      // SAVER_TEXT_SMALL: time and temps in the corners, no date
  bool shows_pic;  // the picture is on it
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

bool saver_picture(void) { return ui_top() == &SCR_SAVER && saver->shows_pic; }

// The ESP's lines, a label each so one that's too long ends in dots. saver_update() gives them their fonts and places.
static void mk_lines(lv_obj_t *p, lv_obj_t *l[SAVER_LINES], lv_color_t c, int w, lv_text_align_t align) {
  for (int i = 0; i < SAVER_LINES; i++) {
    l[i] = mk_text(p, &font_f16, c, "", w, align);
    label_dots(l[i]);
    lv_obj_add_flag(l[i], LV_OBJ_FLAG_HIDDEN);
  }
}

// Clock, date and maybe temps or the ESP's lines on the band. Shadows are black, see-through and SHADOW px down and
// right.
static void band_texts(lv_obj_t *band, lv_obj_t *l[3], lv_obj_t *lines[SAVER_LINES], bool temps, bool shadow) {
  int d = shadow ? SHADOW : 0;
  lv_color_t c = shadow ? lv_color_black() : lv_color_hex(PICTURE_TEXT);
  l[0] = mk_label(band, &font_n40, shadow ? c : lv_color_white(), "");
  lv_obj_set_pos(l[0], 14 + d, 2 + d);
  l[1] = mk_label(band, &font_f14, c, "");
  lv_obj_set_pos(l[1], 16 + d, 50 + d);
  l[2] = NULL;
  if (temps) {
    l[2] = mk_text(band, &font_f16, c, "", BAND_LINES_W, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_recolor(l[2], true);
    lv_obj_set_pos(l[2], SCR_W - 14 - BAND_LINES_W + d, 14 + d);
    mk_lines(band, lines, c, BAND_LINES_W, LV_TEXT_ALIGN_RIGHT);
  }
  for (int i = 0; shadow && i < 3; i++)
    if (l[i])
      lv_obj_set_style_text_opa(l[i], LV_OPA_70, 0);
  for (int i = 0; shadow && temps && i < SAVER_LINES; i++)
    lv_obj_set_style_text_opa(lines[i], LV_OPA_70, 0);
}

// Text over the picture, top or bottom, on a shaded band, with shadows or as it is. Shadows go first so they end
// up behind.
static void over_picture(lv_obj_t *b, bool temps) {
  bool shadow = M.saver_text == SAVER_TEXT_SHADOW;
  lv_obj_t *band = mk_box(b, 0, M.saver_top ? 0 : SCR_H - BAND_H, SCR_W, BAND_H, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(band, M.saver_text == SAVER_TEXT_BAND ? LV_OPA_50 : LV_OPA_TRANSP, 0);
  saver->group = mk_group(band, SCR_W, BAND_H);
  if (shadow)
    band_texts(saver->group, saver->shade, saver->shade_lines, temps, true);
  lv_obj_t *l[3];
  band_texts(saver->group, l, saver->lines, temps, false);
  saver->clock = l[0];
  saver->date = l[1];
  saver->temps = l[2];
  saver->over_picture = true;
}

// The time in a corner, and the temperatures or the ESP's lines in the other
static void small_texts(lv_obj_t *b, bool temps) {
  saver->clock = mk_label(b, &font_f20, lv_color_white(), "");
  lv_obj_set_align(saver->clock, M.saver_top ? LV_ALIGN_TOP_LEFT : LV_ALIGN_BOTTOM_LEFT);
  if (temps) {
    saver->temps = mk_label(b, &font_f16, lv_color_white(), "");
    lv_obj_set_align(saver->temps, M.saver_top ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_BOTTOM_RIGHT);
    mk_lines(b, saver->lines, lv_color_white(), SMALL_LINES_W, LV_TEXT_ALIGN_RIGHT);
  }
  saver->over_picture = saver->small = true;
}

static void saver_build(lv_obj_t *b, intptr_t arg) {
  (void) arg;
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
  anim_show(b);  // over the picture, under the texts
  if (pic) {
    saver->shows_pic = true;
    if (mode != SAVER_PIC && M.saver_text == SAVER_TEXT_SMALL)
      small_texts(b, temps);
    else if (mode != SAVER_PIC)  // SAVER_PIC is the picture alone
      over_picture(b, temps);
    return;
  }
  saver->clock = mk_label(b, &font_n64, lv_color_mix(T.text, lv_color_black(), 200), "");
  lv_obj_set_align(saver->clock, LV_ALIGN_CENTER);
  saver->date = mk_label(b, &font_f16, T.dim, "");
  lv_obj_set_align(saver->date, LV_ALIGN_CENTER);
  if (temps) {
    saver->temps = mk_label(b, &font_f16, T.dim, "");
    lv_label_set_recolor(saver->temps, true);
    lv_obj_set_align(saver->temps, LV_ALIGN_CENTER);
    mk_lines(b, saver->lines, T.dim, SCR_W - 40, LV_TEXT_ALIGN_CENTER);
  }
}

// Indoor and outdoor temperatures with coloured numbers. Stacked over a picture, side by side on black.
static void temps_text(lv_obj_t *l, uint32_t in, uint32_t out) {
  char a[16], c[16];
  if (saver->over_picture)
    set_text(l, "%s  #%06x %s#\n%s  #%06x %s#", _(S_IN), (unsigned) in, fmt_temp(a, sizeof a, indoor_temp(), false),
             _(S_OUT), (unsigned) out, fmt_temp(c, sizeof c, M.t_outdoor, false));
  else
    set_text(l, "%s  #%06x %s#      %s  #%06x %s#", _(S_INDOOR), (unsigned) in,
             fmt_temp(a, sizeof a, indoor_temp(), false), _(S_OUTDOOR), (unsigned) out,
             fmt_temp(c, sizeof c, M.t_outdoor, false));
}

// -r..r and back, a step at a time
static int zigzag(uint32_t n, int r) {
  int p = (int) (n % (uint32_t) (4 * r));
  return p < 2 * r ? p - r : 3 * r - p;
}

// Where the texts have glided to, rx and ry at most. A pixel when the minute changes, y every third so they go all
// round. A day is a whole number of rounds, so midnight doesn't jump.
static void glide(int rx, int ry, int *x, int *y) {
  uint32_t n = (uint32_t) (M.hh * 60 + M.mi);
  *x = zigzag(n, rx);
  *y = zigzag(n / 3, ry);
}

static const lv_font_t *line_font(int i) {
  return M.saver_size[i] >= 28 ? &font_f28 : M.saver_size[i] >= 20 ? &font_f20 : &font_f16;
}

// How many of the ESP's lines show: up to the last with text that fits in room px, *h high. 0 while the ESP is away
// or has none, and the temps show.
static int lines_fitting(int room, int *h) {
  int n = 0, sum = 0;
  *h = 0;
  for (int i = 0; M.esp_online && i < SAVER_LINES; i++) {
    sum += lv_font_get_line_height(line_font(i));
    if (sum > room)
      break;
    if (M.saver_line[i][0]) {
      n = i + 1;
      *h = sum;
    }
  }
  return n;
}

static void show_if(lv_obj_t *o, bool on) {
  if (on && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN))
    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else if (!on && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN))
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// The first n of the ESP's lines from x, y down, the rest hidden
static void place_lines(lv_obj_t *l[SAVER_LINES], int n, int x, int y) {
  for (int i = 0; i < SAVER_LINES; i++) {
    show_if(l[i], i < n);
    if (i >= n)
      continue;
    const lv_font_t *f = line_font(i);
    if (lv_obj_get_style_text_font(l[i], LV_PART_MAIN) != f)
      lv_obj_set_style_text_font(l[i], f, 0);
    lv_obj_set_height(l[i], lv_font_get_line_height(f));
    lv_obj_set_pos(l[i], x, y);
    set_text(l[i], "%s", M.saver_line[i]);
    y += lv_font_get_line_height(f);
  }
}

static void small_update(void) {
  char a[16], c[16];
  int gx, gy, h, y = M.saver_top ? 4 : -4;
  glide(4, 2, &gx, &gy);
  lv_obj_set_pos(saver->clock, 10 + gx, y + gy);
  if (!saver->temps)
    return;
  int n = lines_fitting(SCR_H / 2, &h);
  show_if(saver->temps, !n);
  if (!n) {
    set_text(saver->temps, "%s %s  %s %s", _(S_IN), fmt_temp(a, sizeof a, indoor_temp(), false), _(S_OUT),
             fmt_temp(c, sizeof c, M.t_outdoor, false));
    lv_obj_set_pos(saver->temps, -10 + gx, y + gy);
  }
  place_lines(saver->lines, n, SCR_W - 10 - SMALL_LINES_W + gx, (M.saver_top ? 4 : SCR_H - 4 - h) + gy);
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
  int gx, gy, h, n;
  if (saver->over_picture) {
    glide(4, 2, &gx, &gy);
    lv_obj_set_pos(saver->group, gx, gy);
    if (saver->shade[0]) {  // copy the texts to their shadows
      set_text(saver->shade[0], "%s", lv_label_get_text(saver->clock));
      set_text(saver->shade[1], "%s", lv_label_get_text(saver->date));
    }
    if (!saver->temps)
      return;
    n = lines_fitting(BAND_H - 2, &h);
    int x = SCR_W - 14 - BAND_LINES_W, y = (BAND_H - h) / 2;
    show_if(saver->temps, !n);
    if (!n)
      temps_text(saver->temps, 0xFFFFFF, 0xFFFFFF);
    place_lines(saver->lines, n, x, y);
    if (saver->shade[2]) {
      show_if(saver->shade[2], !n);
      if (!n)
        temps_text(saver->shade[2], 0, 0);
      place_lines(saver->shade_lines, n, x + SHADOW, y + SHADOW);
    }
    return;
  }
  // On black the ESP's lines go under the date, and the clock and the date up by what they take more than the temps
  glide(8, 4, &gx, &gy);
  n = saver->temps ? lines_fitting(110, &h) : 0;
  int up = n ? (h - lv_font_get_line_height(&font_f16)) / 2 : 0;
  lv_obj_set_pos(saver->clock, gx, (saver->temps ? -34 : -20) - up + gy);
  lv_obj_set_pos(saver->date, gx, (saver->temps ? 18 : 32) - up + gy);
  if (!saver->temps)
    return;
  show_if(saver->temps, !n);
  if (!n) {
    temps_text(saver->temps, ui_hex(T.text), ui_hex(T.cold));
    lv_obj_set_pos(saver->temps, gx, 60 + gy);
  }
  place_lines(saver->lines, n, 20 + gx, SCR_H / 2 + 51 - up + gy);
}

const screen_t SCR_SAVER = {.flags = SF_NOBARS, .build = saver_build, .update = saver_update};
