// Runs LVGL on the PC with the display in memory, saves screenshots and lays out the ESP's messages.
#include "picture.h"
#include "plat.h"
#include "plink.h"
#include "sim.h"
#include "ui.h"
#include "src/draw/sw/lv_draw_sw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240
#define HEAP_MARGIN 4096  // free LVGL heap every screen must leave

unsigned long sim_px;
static lv_color_t fb[W * H], draw_buf[W * 40];

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px) {
  if (ui_held()) {  // the LCD keeps what it has
    lv_disp_flush_ready(d);
    return;
  }
  int w = a->x2 - a->x1 + 1;
  sim_px += (unsigned long) w * (unsigned long) (a->y2 - a->y1 + 1);
  for (int y = a->y1; y <= a->y2; y++)
    memcpy(&fb[y * W + a->x1], px + (y - a->y1) * w, w * sizeof *px);
  lv_disp_flush_ready(d);
}

void sim_display_init(void) {
  lv_init();
  static lv_disp_draw_buf_t db;
  lv_disp_draw_buf_init(&db, draw_buf, NULL, W * 40);
  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.hor_res = W;
  drv.ver_res = H;
  drv.flush_cb = flush;
  drv.rounder_cb = pic_round;
  drv.draw_buf = &db;
  lv_disp_drv_register(&drv);
}

void sim_run(int ms) {
  for (int t = 0; t < ms; t += 5) {
    lv_tick_inc(5);
    lv_timer_handler();
  }
}

uint16_t sim_rgb565(uint32_t rgb) {
  return (uint16_t) ((rgb >> 8 & 0xF800) | (rgb >> 5 & 0x07E0) | (rgb >> 3 & 0x001F));
}

uint32_t sim_rgb888(uint16_t c) {
  uint32_t r = (c >> 11) << 3 | c >> 13, g = ((c >> 5) & 63) << 2 | ((c >> 9) & 3), b = (c & 31) << 3 | ((c >> 2) & 7);
  return r << 16 | g << 8 | b;
}

uint32_t sim_pixel(int x, int y) {
  lv_refr_now(NULL);
  return sim_rgb888(fb[y * W + x].full);
}

int sim_stale(void) {
  static lv_color_t was[W * H];
  unsigned long px = sim_px;
  lv_refr_now(NULL);
  memcpy(was, fb, sizeof fb);
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(NULL);
  sim_px = px;
  int n = 0;
  for (int i = 0; i < W * H; i++)
    n += was[i].full != fb[i].full;
  return n;
}

void sim_draw(lv_obj_t *o, const lv_area_t *a, lv_color_t bg, lv_color_t *buf) {
  lv_disp_t *d = lv_disp_get_default();
  lv_draw_sw_ctx_t ctx = *(lv_draw_sw_ctx_t *) d->driver->draw_ctx;
  for (uint32_t i = 0; i < lv_area_get_size(a); i++)
    buf[i] = bg;
  lv_area_t area = *a;
  ctx.base_draw.buf = buf;
  ctx.base_draw.buf_area = &area;
  ctx.base_draw.clip_area = &area;
  _lv_refr_set_disp_refreshing(d);
  lv_event_send(o, LV_EVENT_DRAW_MAIN, &ctx);
  _lv_refr_set_disp_refreshing(NULL);
}

bool sim_shot(const char *dir, const char *name) {
  sim_run(50);
  lv_refr_now(NULL);
  static uint32_t rgb[W * H];
  for (int i = 0; i < W * H; i++)
    rgb[i] = sim_rgb888(fb[i].full);
  char path[256];
  snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
  sim_ppm(path, W, H, rgb);
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  unsigned used = (unsigned) (mon.total_size - mon.free_size);
  bool ok = mon.total_size - used >= HEAP_MARGIN;
  printf("%-30s lvgl heap %5u of %u B%s\n", name, used, (unsigned) mon.total_size, ok ? "" : "  FAIL: too little left");
  return ok;
}

// ---------------------------------------------------------------- the ESP's messages
int sim_pic_begin(uint8_t *b, uint8_t id, int format, int w, int h, uint32_t len, const uint32_t *pal) {
  return plink_pic_begin(b, id, (uint8_t) format, (uint16_t) w, (uint16_t) h, len, pal);
}

int sim_pic_patch(uint8_t *b, uint8_t id, int y, int count, uint32_t len, const uint32_t *pal) {
  return plink_pic_patch(b, id, (uint16_t) y, (uint16_t) count, len, pal);
}

int sim_anim_begin(uint8_t *b, uint8_t id, int slot, int w, int h, int scale, int transp, uint32_t len,
                   const uint32_t *pal) {
  return plink_anim_begin(b, id, (uint8_t) slot, (uint16_t) w, (uint16_t) h, (uint8_t) scale, (uint8_t) transp, len,
                          pal);
}

int sim_anim_place(uint8_t *b, int place, int slot, int x, int y, int own, int start, int fade) {
  b[ANIM_MSG_PLACE] = (uint8_t) place;
  b[ANIM_PLACE_SLOT] = (uint8_t) slot;
  plink_put16(b + ANIM_PLACE_X, (uint16_t) x);
  plink_put16(b + ANIM_PLACE_Y, (uint16_t) y);
  b[ANIM_PLACE_OWN] = (uint8_t) own;
  b[ANIM_PLACE_START] = (uint8_t) start;
  plink_put16(b + ANIM_PLACE_FADE, (uint16_t) fade);
  return ANIM_PLACE_LEN;
}

// The channels, then from at a value for each of them, times k
static int values(uint8_t *b, int at, unsigned channels, const int *v, int k) {
  b[ANIM_GLIDE_CHANNELS] = (uint8_t) channels;  // DRIFT's too
  for (int c = 0; c < ANIM_CHANNELS; c++)
    if (channels >> c & 1) {
      plink_put16(b + at, (uint16_t) (*v++ * k));
      at += 2;
    }
  return at;
}

int sim_anim_glide(uint8_t *b, int place, unsigned channels, int ms, int ease, const int *to) {
  b[ANIM_MSG_PLACE] = (uint8_t) place;
  plink_put16(b + ANIM_GLIDE_MS, (uint16_t) ms);
  b[ANIM_GLIDE_EASE] = (uint8_t) ease;
  return values(b, ANIM_GLIDE_HEAD, channels, to, 1);
}

int sim_anim_drift(uint8_t *b, int place, unsigned channels, const int *v) {
  b[ANIM_MSG_PLACE] = (uint8_t) place;
  return values(b, ANIM_DRIFT_HEAD, channels, v, 10);  // in tenths
}

// True for string literals. They're in read-only data like on the panel, so writing to one crashes here too.
extern const char etext[], __data_start[];
bool plat_const(const void *p) { return (const char *) p >= etext && (const char *) p < __data_start; }

// Made up, so the screenshots don't change with every other change
void plat_stats(plat_stats_t *s) {
  *s = (plat_stats_t) {.uptime_s = 3 * 86400 + 4 * 3600 + 12 * 60 + 7,
                       .restarts = 1,
                       .full_us = 152300,
                       .full_send_us = 136900,
                       .stack_free = 5312,
                       .saves = 128,
                       .heap_size = 16384,
                       .heap_used = 9830,
                       .heap_peak = 14540};
}

// A different link session on every simulated start, but the same sequence on every run.
uint32_t plat_entropy(void) {
  static uint32_t starts;
  return ++starts * 0x9E3779B9u;
}
