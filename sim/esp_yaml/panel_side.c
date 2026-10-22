// Panel side of the ESP package test. The real link, picture and value code, behind plain C calls for the
// C++ harness.
#include "ui.h"
#include "link.h"
#include "picture.h"
#include "plink.h"
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
  pic_msg(K_ANIM_CLEAR, NULL, 0);
  M.esp_online = false;
  val_forget_esp();
  link_init();
}

void pn_poll(void) { link_poll(); }
// A burst that isn't the link's, starting at start_us. Handed over the way the RX interrupt in hw/rs485.c does.
void pn_bus(const uint8_t *b, int n, uint32_t start_us) {
  uint32_t idle = start_us + (uint32_t) (n + 1) * PBUS_BYTE_US;
  link_bus(b, n, start_us, idle, idle / 1000);
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
int pn_known(uint16_t key) { return val_known(val_by_key(key)); }
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
  return item_text(b, &M.items[i], true);
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

// Line y of the animation's frame from its decoder, 3 bytes per pixel, RGB565 then alpha. Returns the width, or 0
// if there's no animation.
int pn_anim_line(int y, uint8_t *out) {
  lv_obj_t *o = anim_show(lv_scr_act());
  if (!o)
    return 0;
  lv_img_decoder_dsc_t dsc;
  int w = 0;
  if (lv_img_decoder_open(&dsc, lv_img_get_src(o), lv_color_black(), 0) == LV_RES_OK) {
    w = dsc.header.w;
    if (lv_img_decoder_read_line(&dsc, 0, (lv_coord_t) y, (lv_coord_t) w, out) != LV_RES_OK)
      w = 0;
    lv_img_decoder_close(&dsc);
  }
  lv_obj_del(o);
  return w;
}

// Let the animation play for ms.
void pn_anim_step(int ms) {
  lv_obj_t *o = anim_show(lv_scr_act());
  lv_tick_inc((uint32_t) ms);
  lv_timer_handler();
  if (o)
    lv_obj_del(o);
}

int pn_anim_ready(void) { return anim_ready(); }
void pn_anim_pos(int *x, int *y) {
  int16_t ax, ay;
  anim_pos(&ax, &ay);
  *x = ax;
  *y = ay;
}

// Screenshot of the picture screensaver as DIR/NAME.ppm.
void pn_shot(const char *dir, const char *name) {
  M.screensaver = 3;
  ui_value_changed(V_SCREENSAVER);
  ui_push(&SCR_SAVER, 0);
  sim_shot(dir, name);
  ui_pop();
}
