// Shared styles, see ui_int.h. A style of its own costs an object 24 bytes or more of LVGL's heap, a shared one an
// 8-byte slot in its style list. So there's one style for each theme colour as a text colour and as a background,
// one for each font, and one for each value many objects have. The theme's change goes into the colour styles and
// every object follows, see st_theme(). A colour that isn't the theme's, like black over a picture, and a value
// only an object or two has, are set on the object as before.
#include "ui_int.h"

static lv_style_t text_st[N_COLOURS], bg_st[N_COLOURS];
static const lv_font_t *const FONTS[] = {
    &font_f12, &font_f14, &font_f16, &font_f20, &font_f28, &font_n40,
    &font_n64, &icons_16, &icons_22, &icons_32, &icons_48,
};
static lv_style_t font_st[LEN(FONTS)];
static struct {
  uint8_t r;
  lv_style_t st;
} radius_st[] = {{.r = CARD_RADIUS}, {.r = 10}, {.r = 4}, {.r = 3}};  // the radii many boxes have
lv_style_t ST_OPAQUE, ST_CENTER, ST_RIGHT, ST_BORDER1, ST_BORDER2, ST_PAD_RIGHT8, ST_PAD_HOR4, ST_ARC_ROUND,
    ST_TEXT_OPA70;
_Static_assert(sizeof(theme_t) == N_COLOURS * sizeof(lv_color_t), "the theme's colours as an array");
_Static_assert(CARD_RADIUS == PILL_RADIUS, "one radius style for both");

static int colour_index(lv_color_t c) {  // -1 if it isn't a theme colour
  const lv_color_t *t = (const lv_color_t *) &T;
  for (int i = 0; i < N_COLOURS; i++)
    if (t[i].full == c.full)
      return i;
  return -1;
}

// Puts want in place of the style o has from the same family for selector sel, if it has one. prop is what changes
static bool swap(lv_obj_t *o, const lv_style_t *family, int n, lv_style_t *want, lv_style_selector_t sel,
                 lv_style_prop_t prop) {
  for (unsigned i = 0; i < o->style_cnt; i++) {
    _lv_obj_style_t *s = &o->styles[i];
    if (s->is_local || s->is_trans || s->selector != sel)
      continue;
    if ((uintptr_t) s->style - (uintptr_t) family >= (uintptr_t) n * sizeof *family)
      continue;
    if (s->style != want) {
      s->style = want;
      lv_obj_refresh_style(o, sel, prop);
    }
    return true;
  }
  return false;
}

static void give(lv_obj_t *o, lv_style_t *family, int n, int k, lv_style_selector_t sel, lv_style_prop_t prop) {
  lv_obj_remove_local_style_prop(o, prop, sel);  // a colour of its own from before would win over the shared one
  if (!swap(o, family, n, &family[k], sel, prop))
    lv_obj_add_style(o, &family[k], sel);
}

void st_text(lv_obj_t *o, lv_color_t c) {
  int k = colour_index(c);
  if (k < 0)
    lv_obj_set_style_text_color(o, c, 0);
  else
    give(o, text_st, N_COLOURS, k, 0, LV_STYLE_TEXT_COLOR);
}

void st_bg(lv_obj_t *o, lv_color_t c, lv_style_selector_t sel) {
  int k = colour_index(c);
  if (k < 0)
    lv_obj_set_style_bg_color(o, c, sel);
  else
    give(o, bg_st, N_COLOURS, k, sel, LV_STYLE_BG_COLOR);
}

void st_font(lv_obj_t *o, const lv_font_t *f) {
  for (int k = 0; k < (int) LEN(FONTS); k++)
    if (FONTS[k] == f) {
      give(o, font_st, LEN(FONTS), k, 0, LV_STYLE_TEXT_FONT);
      return;
    }
  lv_obj_set_style_text_font(o, f, 0);
}

void st_radius(lv_obj_t *o, int r, lv_style_selector_t sel) {
  for (unsigned k = 0; k < LEN(radius_st); k++)
    if (radius_st[k].r == r) {
      lv_obj_add_style(o, &radius_st[k].st, sel);
      return;
    }
  lv_obj_set_style_radius(o, (lv_coord_t) r, sel);
}

void st_align(lv_obj_t *o, lv_text_align_t align) {
  if (align != LV_TEXT_ALIGN_LEFT)
    lv_obj_add_style(o, align == LV_TEXT_ALIGN_CENTER ? &ST_CENTER : &ST_RIGHT, 0);
}

void st_border(lv_obj_t *o, int width) {
  if (width == 1 || width == 2)
    lv_obj_add_style(o, width == 1 ? &ST_BORDER1 : &ST_BORDER2, 0);
  else
    lv_obj_set_style_border_width(o, (lv_coord_t) width, 0);
}

void st_theme(void) {
  const lv_color_t *t = (const lv_color_t *) &T;
  for (int i = 0; i < N_COLOURS; i++) {
    lv_style_set_text_color(&text_st[i], t[i]);
    lv_style_set_bg_color(&bg_st[i], t[i]);
  }
  lv_obj_report_style_change(NULL);
}

void st_init(void) {
  for (int i = 0; i < N_COLOURS; i++) {
    lv_style_init(&text_st[i]);
    lv_style_init(&bg_st[i]);
  }
  for (unsigned k = 0; k < LEN(FONTS); k++) {
    lv_style_init(&font_st[k]);
    lv_style_set_text_font(&font_st[k], FONTS[k]);
  }
  for (unsigned k = 0; k < LEN(radius_st); k++) {
    lv_style_init(&radius_st[k].st);
    lv_style_set_radius(&radius_st[k].st, radius_st[k].r);
  }
  lv_style_init(&ST_OPAQUE);
  lv_style_init(&ST_CENTER);
  lv_style_init(&ST_RIGHT);
  lv_style_init(&ST_BORDER1);
  lv_style_init(&ST_BORDER2);
  lv_style_init(&ST_PAD_HOR4);
  lv_style_init(&ST_PAD_RIGHT8);
  lv_style_init(&ST_ARC_ROUND);
  lv_style_init(&ST_TEXT_OPA70);
  lv_style_set_bg_opa(&ST_OPAQUE, LV_OPA_COVER);
  lv_style_set_text_align(&ST_CENTER, LV_TEXT_ALIGN_CENTER);
  lv_style_set_text_align(&ST_RIGHT, LV_TEXT_ALIGN_RIGHT);
  lv_style_set_border_width(&ST_BORDER1, 1);
  lv_style_set_border_width(&ST_BORDER2, 2);
  lv_style_set_pad_hor(&ST_PAD_HOR4, 4);
  lv_style_set_pad_right(&ST_PAD_RIGHT8, 8);
  lv_style_set_arc_rounded(&ST_ARC_ROUND, true);
  lv_style_set_text_opa(&ST_TEXT_OPA70, LV_OPA_70);
}
