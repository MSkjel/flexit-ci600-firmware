// Panel side of the ESP package test. The real link, picture and value code, behind plain C calls for the
// C++ harness.
#include "ui_int.h"
#include "link.h"
#include "panel_side.h"
#include "picture.h"
#include "plink.h"
#include "shot.h"
#include "sim.h"
#include <stdio.h>
#include <string.h>

void pn_init(void) {
  sim_model_init();
  val_forget_esp();
  M.esp_online = false;
  sim_display_init();
  ui_init();
  link_init();
}

void pn_restart(void) {  // a restart loses the link state and the picture, which only lives in RAM
  pic_msg(K_PIC_CLEAR, NULL, 0);
  anim_clear_all();
  M.esp_online = false;
  val_forget_esp();
  link_init();
}

void pn_reboot(int wiped) {  // a restart, with wiped the flash erased its settings
  if (wiped)
    panel_defaults();
  sim_fresh = wiped;
  pn_restart();
}

void pn_poll(void) { link_poll(); }
// A burst that isn't the link's, starting at start_us. Handed over the way the RX interrupt in hw/rs485.c does.
void pn_bus(const uint8_t *b, int n, uint32_t start_us) {
  uint32_t idle = start_us + (uint32_t) (n + 1) * PBUS_BYTE_US;
  link_bus(b, n, pbus_crc_ok(b, n), start_us, idle, idle / 1000);
}
// A link frame, the line going idle after it at idle_us. Returns the answer's length.
int pn_bulk(const uint8_t *f, int n, uint32_t idle_us, uint8_t *answer) {
  return link_bulk_frame(f, n, idle_us, idle_us / 1000, answer);
}
void pn_second(void) {
  link_second();
  lv_tick_inc(1000);
  lv_timer_handler();
}
int pn_online(void) { return M.esp_online; }
int pn_get(uint16_t key) { return (int) val_get(val_by_key(key)); }
int pn_reg(uint16_t reg) { return (int) val_get(val_by_cx60(reg)); }  // the panel's value for a CX60 register
void pn_set_reg(uint16_t reg, int v) { val_set(val_by_cx60(reg), v); }
void pn_set(uint16_t key, int v) { val_set(val_by_key(key), v); }
void pn_action(int a) { do_action((action_t) a); }
int pn_idle(void) { return !link_queue()->qn && !link_bulk_state()->out_full; }  // nothing waiting for the ESP
const char *pn_note(int *icon, int *level, int *fresh) {
  *icon = M.note_icon;
  *level = M.note_level;
  *fresh = M.note_new;
  M.note_new = false;
  return M.note;
}
const char *pn_saver_line(int i, int *size) {
  *size = M.saver_size[i];
  return M.saver_line[i];
}
void pn_clock(int *y, int *mo, int *d, int *h, int *mi) {
  *y = M.yy;
  *mo = M.mo;
  *d = M.dd;
  *h = M.hh;
  *mi = M.mi;
}
// Item i as the panel has it. Returns its type, 0 if the slot is empty.
int pn_item(int i, int *flags, int *icon, int *dec, int *step, int *id, int *min, int *max, int *value,
            const char **label, const char **unit) {
  const item_t *it = &M.items[i];
  *flags = it->flags;
  *icon = it->icon;
  *dec = it->dec;
  *step = it->step;
  *id = it->id;
  *min = it->min;
  *max = it->max;
  *value = it->value;
  *label = it->label;
  *unit = it->unit;
  return it->type;
}
int pn_item_level(int i, int k) { return M.items[i].level[k]; }  // item i's level k as the panel has it
const char *pn_item_text(int i) {                                // item i's value as the panel shows it
  static char b[24];
  return item_text(b, sizeof b, &M.items[i], true);
}
const char *pn_item_help(int i) { return link_item_help(i); }
// Line y of the picture as the panel draws it, in RGB565
int pn_pic_line(int y, uint16_t *out) {
  lv_area_t a = {0, (lv_coord_t) y, (lv_coord_t) (pic_width() - 1), (lv_coord_t) y};
  return pic_read(&a, (lv_color_t *) out, pic_width());
}

int pn_pic(int *w, int *h) {
  *w = pic_width();
  *h = pic_height();
  return pic_state();
}
int pn_pic_format(void) { return pic_format(); }

// Line y of the frame place p shows, w x h animation pixels drawn at scale, 3 bytes per pixel: RGB565, then 0 where
// it's colour transp. 0 if there's no such place.
int pn_anim_line(int p, int y, int w, int scale, int transp, uint8_t *out) {
  anim_look_t l;
  if (!anim_look(p, &l))
    return 0;
  for (int x = 0; x < w * scale; x++) {
    int sx = x / scale, c = l.frame[y / scale * (w / 2) + sx / 2];
    c = sx & 1 ? c & 15 : c >> 4;
    memcpy(out + 3 * x, &l.pal[c], 2);
    out[3 * x + 2] = c == transp ? LV_OPA_TRANSP : LV_OPA_COVER;
  }
  return w * scale;
}

// Let the animations play for ms.
void pn_anim_step(int ms) {
  lv_obj_t *o = anim_show(lv_scr_act());
  lv_tick_inc((uint32_t) ms);
  lv_timer_handler();
  lv_obj_del(o);
}

// 0 if there's no such place
int pn_anim(int place, pn_look_t *out) {
  anim_look_t l;
  if (!anim_look(place, &l))
    return 0;
  *out = (pn_look_t) {l.x, l.y, l.deg, l.flip, l.size, l.opa, l.depth};
  return 1;
}
int pn_anim_covered(int place, int x, int y) { return anim_covered(place, x, y); }
unsigned pn_anim_used(void) { return anim_used(); }

// The picture screensaver up, so its animations play, or down again. The picture alone unless pn_saver_texts()
// said otherwise.
static int saver_mode = SAVER_PIC;
void pn_saver(int on) {
  if (on) {
    M.screensaver = (uint8_t) saver_mode;
    ui_value_changed(V_SCREENSAVER);
    ui_push(&SCR_SAVER, 0);
  } else {
    ui_pop();
  }
}
void pn_saver_texts(int mode, int top) {
  saver_mode = mode;
  M.saver_top = top;
}

// The visible labels with text under o, x1 y1 x2 y2 each, tight to the text when it's right-aligned in a wider label
static int labels(lv_obj_t *o, int *out, int n, int max) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) || n >= max)
    return n;
  if (lv_obj_check_type(o, &lv_label_class)) {
    const char *t = lv_label_get_text(o);
    if (!t || !*t)
      return n;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    lv_point_t size;
    lv_txt_get_size(&size, t, lv_obj_get_style_text_font(o, 0), 0, 0, LV_COORD_MAX,
                    lv_label_get_recolor(o) ? LV_TEXT_FLAG_RECOLOR : LV_TEXT_FLAG_NONE);
    if (lv_obj_get_style_text_align(o, 0) == LV_TEXT_ALIGN_RIGHT)
      a.x1 = (lv_coord_t) (a.x2 - size.x);
    out[n * 4] = a.x1;
    out[n * 4 + 1] = a.y1;
    out[n * 4 + 2] = a.x2;
    out[n * 4 + 3] = a.y2;
    return n + 1;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
    n = labels(lv_obj_get_child(o, (int32_t) i), out, n, max);
  return n;
}
int pn_texts(int *out, int max) {
  if (ui_top() != &SCR_SAVER)
    return 0;
  lv_obj_update_layout(lv_scr_act());
  return labels(lv_scr_act(), out, 0, max);
}

// Screenshot of the picture screensaver as DIR/NAME.ppm
void pn_draw(int ms) {
  shot_poll();
  lv_tick_inc((uint32_t) ms);
  lv_timer_handler();
}

void pn_shot(const char *dir, const char *name) {
  pn_saver(1);
  sim_shot(dir, name);
  pn_saver(0);
}
