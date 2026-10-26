// Drawing and formatting helpers.
#include "ui.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Styles left at their defaults (a 0 position or radius, the default font) aren't set, each one takes LVGL heap
lv_obj_t *mk_group(lv_obj_t *p, int w, int h) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t *mk_box(lv_obj_t *p, int x, int y, int w, int h, lv_color_t bg, int radius) {
  lv_obj_t *o = mk_group(p, w, h);
  if (x || y)
    lv_obj_set_pos(o, x, y);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  if (radius)
    lv_obj_set_style_radius(o, radius, 0);
  return o;
}

// Text in flash is used without copying. LONG_DOT writes its dots into the text, so label_dots() copies it first.
lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *txt) {
  lv_obj_t *l = lv_label_create(p);
  if (f != LV_FONT_DEFAULT)
    lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  label_text(l, txt ? txt : "");
  return l;
}

lv_obj_t *mk_wrap(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *txt, int x, int y, int w) {
  lv_obj_t *l = mk_label(p, f, c, txt);
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(l, x, y);
  return l;
}

void label_dots(lv_obj_t *l) {
  if (((lv_label_t *) l)->static_txt)
    lv_label_set_text(l, lv_label_get_text(l));  // make a copy
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
}

lv_obj_t *mk_icon(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *ic) {
#ifdef CI600_SIM
  uint32_t i = 0, cp = _lv_txt_encoded_next(ic, &i);
  lv_font_glyph_dsc_t g;
  if (cp && !lv_font_get_glyph_dsc(f, &g, cp, 0)) {  // the panel would draw a box
    fprintf(stderr, "icon U+%X isn't in that icon font, add it to BIG_ICONS in tools/mkfonts.py\n", (unsigned) cp);
    abort();
  }
#endif
  return mk_label(p, f, c, ic);
}

lv_obj_t *mk_bar(lv_obj_t *p, int x, int y, int w, int h, int val, int max, lv_color_t c) {
  lv_obj_t *b = lv_bar_create(p);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, T.surface2, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(b, h / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, c, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(b, h / 2, LV_PART_INDICATOR);
  lv_bar_set_range(b, 0, max > 0 ? max : 1);  // LVGL divides by the range
  lv_bar_set_value(b, val, LV_ANIM_OFF);
  return b;
}

lv_obj_t *mk_arc(lv_obj_t *p, int size, int width, int val, int max, lv_color_t c) {
  lv_obj_t *a = lv_arc_create(p);
  lv_obj_remove_style_all(a);
  lv_obj_set_size(a, size, size);
  lv_arc_set_bg_angles(a, 135, 45);
  lv_arc_set_range(a, 0, max);
  lv_arc_set_value(a, val);
  lv_obj_set_style_arc_color(a, T.surface2, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  return a;
}

void set_text(lv_obj_t *l, const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  lv_vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  lv_label_set_text(l, buf);
}

int sprint(char *buf, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = lv_vsnprintf(buf, SIZE_MAX, fmt, ap);
  va_end(ap);
  return n;
}

const char *fmt_dec(char *buf, int32_t v, uint8_t dec) {
  static const uint32_t DIV[4] = {1, 10, 100, 1000};
  if (!dec) {
    sprint(buf, "%ld", (long) v);
    return buf;
  }
  uint32_t a = v < 0 ? 0u - (uint32_t) v : (uint32_t) v, div = DIV[dec > 3 ? 3 : dec];
  sprint(buf, "%s%lu.%0*lu", v < 0 ? "-" : "", (unsigned long) (a / div), dec, (unsigned long) (a % div));
  return buf;
}

const char *fmt_temp(char *buf, int16_t t, bool unit) {
  if (t == T_NONE)
    return strcpy(buf, NO_VALUE);
  if (M.temp_dec)
    fmt_dec(buf, t, 1);
  else
    sprint(buf, "%d", (t + (t < 0 ? -5 : 5)) / 10);  // rounded to whole degrees
  strcat(buf, unit ? "\xC2\xB0"
                     "C"
                   : "\xC2\xB0");
  return buf;
}

const char *fmt_target(char *buf, int16_t t) {
  fmt_dec(buf, t, 1);
  strcat(buf, "\xC2\xB0"
              "C");
  return buf;
}

const char *fmt_hm(char *buf, int minutes) {
  int h = minutes / 60, m = minutes % 60;  // an end at midnight is 24:00
  if (M.clock_24h)
    sprint(buf, "%02d:%02d", h, m);
  else
    sprint(buf, "%d:%02d %s", (h + 11) % 12 + 1, m, h % 24 < 12 ? _(S_AM) : _(S_PM));
  return buf;
}

lv_obj_t *mk_card(lv_obj_t *p, int x, int y, int w, int h, lv_color_t border, int border_w) {
  lv_obj_t *c = mk_box(p, x, y, w, h, T.surface, CARD_RADIUS);
  lv_obj_set_style_border_width(c, border_w, 0);
  lv_obj_set_style_border_color(c, border, 0);
  return c;
}

// Icon in a circle, drawn as the label's own background so it's one object
lv_obj_t *mk_disc(lv_obj_t *p, int x, int y, int size, lv_color_t bg, const lv_font_t *f, lv_color_t c,
                  const char *icon) {
  lv_obj_t *l = mk_icon(p, f, c, icon);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_size(l, size, size);
  lv_obj_set_style_bg_color(l, bg, 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(l, size / 2, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(l, (size - lv_font_get_line_height(f)) / 2, 0);
  return l;
}

lv_obj_t *mk_badge(lv_obj_t *p, int x, int y, int size, const lv_font_t *f, lv_color_t c, const char *icon) {
  return mk_disc(p, x, y, size, lv_color_mix(c, T.surface, 60), f, c, icon);
}

static void timer_gone(lv_event_t *e) { lv_timer_del(lv_event_get_user_data(e)); }

lv_timer_t *mk_timer(lv_obj_t *owner, lv_timer_cb_t cb, uint32_t ms) {
  lv_timer_t *t = lv_timer_create(cb, ms, NULL);
  lv_obj_add_event_cb(owner, timer_gone, LV_EVENT_DELETE, t);
  return t;
}

void label_text(lv_obj_t *l, const char *txt) {
  if (plat_const(txt))
    lv_label_set_text_static(l, txt);
  else
    lv_label_set_text(l, txt);
}

void mk_empty(lv_obj_t *p, lv_color_t c, const char *icon, const char *title, const char *sub) {
  lv_obj_align(mk_icon(p, &icons_48, c, icon), LV_ALIGN_CENTER, 0, -30);
  lv_obj_align(mk_label(p, &font_f16, T.text, title), LV_ALIGN_CENTER, 0, 14);
  if (sub)
    lv_obj_align(mk_label(p, &font_f12, T.dim, sub), LV_ALIGN_CENTER, 0, 36);
}

lv_obj_t *mk_chart(lv_obj_t *p, int w, int lines) {
  lv_obj_t *c = lv_chart_create(p);
  lv_obj_remove_style_all(c);
  lv_obj_set_pos(c, CHART_X, CHART_Y);
  lv_obj_set_size(c, w, CHART_H);
  lv_chart_set_type(c, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(c, lines, 5);
  lv_obj_set_style_line_color(c, T.line, LV_PART_MAIN);
  lv_obj_set_style_line_width(c, 1, LV_PART_MAIN);
  lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
  lv_obj_set_style_size(c, 0, LV_PART_INDICATOR);
  lv_chart_set_point_count(c, TREND_LEN);
  return c;
}

lv_color_t level_color(int mode) {
  return mode == MODE_MAX ? T.warn : mode == MODE_NORMAL ? T.accent : mode == MODE_MIN ? T.cold : T.faint;
}

uint32_t ui_hex(lv_color_t c) { return lv_color_to32(c) & 0xFFFFFF; }

lv_obj_t *mk_day_bar(lv_obj_t *p, int x, int y, int w, int h, int day) {
  lv_obj_t *track = mk_box(p, x, y, w, h, T.surface, 4);
  for (int i = 0; i < M.n_periods[day]; i++) {
    const period_t *s = &M.week[day][i];
    int a = s->start * w / 1440, e = s->end * w / 1440;
    mk_box(track, a, 0, e - a, h, level_color(s->level), 4);
  }
  return track;
}

void keep_in_view(int16_t *top, int row, int rows) {
  if (row < *top)
    *top = (int16_t) row;
  if (row >= *top + rows)
    *top = (int16_t) (row - rows + 1);
}

void clamp_sel(int n, int max_top) {  // the list may have got shorter since
  frame_t *f = ui_frame();
  if (f->sel >= n)
    f->sel = (int16_t) (n - 1);
  if (f->top > max_top)
    f->top = (int16_t) max_top;
}

int32_t step_value(int32_t v, int n, int step, int32_t min, int32_t max) {
  v += n * step;
  if (v <= min || v >= max)
    return v <= min ? min : max;
  int32_t off = (v % step + step) % step;  // snap back onto multiples of step, e.g. after an end that isn't one
  return v + (n < 0 && off ? step - off : -off);
}

int16_t step_sel(int sel, int key, int n) { return (int16_t) ((sel + (key == KEY_DOWN ? 1 : n - 1)) % n); }

bool sel_key(int key, int n) {
  if (key != KEY_UP && key != KEY_DOWN)
    return false;
  frame_t *f = ui_frame();
  f->sel = step_sel(f->sel, key, n);
  f->s->update();
  return true;
}

lv_color_t item_color(const item_t *it) {
  if (!(it->flags & ITEMF_LEVELS) || it->value == ITEM_UNKNOWN)
    return T.accent;
  int32_t v = it->value;
  return v < it->level[0] || v > it->level[3] ? T.err : v < it->level[1] || v > it->level[2] ? T.warn : T.ok;
}

int item_pct(const item_t *it) {
  int64_t span = (int64_t) it->max - it->min, v = (int64_t) it->value - it->min;
  return span <= 0 ? 0 : v <= 0 ? 0 : v >= span ? 100 : (int) (v * 100 / span);
}

bool item_on(const item_t *it) { return it->value != 0 && it->value != ITEM_UNKNOWN; }

char *item_text(char *buf, const item_t *it, bool unit) {
  if (it->type == ITEM_BUTTON)
    return strcpy(buf, _(S_PRESS));
  if (it->type == ITEM_TEXT)
    return strcpy(buf, it->text[0] ? it->text : NO_VALUE);
  if (it->value == ITEM_UNKNOWN)
    return strcpy(buf, NO_VALUE);  // like an unknown temperature
  if (it->type == ITEM_TOGGLE)
    return strcpy(buf, item_on(it) ? (it->on[0] ? it->on : _(S_ON)) : (it->off[0] ? it->off : _(S_OFF)));
  fmt_dec(buf, it->value, it->dec);
  return unit ? add_unit(buf, it->unit) : buf;
}

// txt in the value font if it fits in w, else the row font, padded down to sit on the same line. Texts from the
// ESP can be long. The font goes first, a label with dots cuts the text as it's set.
void label_fit(lv_obj_t *l, const char *txt, int w) {
  bool fits = lv_txt_get_width(txt, (uint32_t) strlen(txt), &font_f20, 0, LV_TEXT_FLAG_NONE) <= w;
  lv_obj_set_style_text_font(l, fits ? &font_f20 : &font_f14, 0);
  lv_obj_set_style_pad_top(l, fits ? 0 : lv_font_get_line_height(&font_f20) - lv_font_get_line_height(&font_f14), 0);
  lv_label_set_text(l, txt);
}

char *add_unit(char *buf, const char *unit) {
  if (unit && unit[0]) {
    strcat(buf, unit[0] == '%' || !strncmp(unit, "\xC2\xB0", 2) ? "" : " ");  // no space before % or a degree sign
    strcat(buf, unit);
  }
  return buf;
}
