// Runs LVGL on the PC with the display in memory, and saves screenshots.
#include "plat.h"
#include "sim.h"
#include "ui.h"
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
  drv.draw_buf = &db;
  lv_disp_drv_register(&drv);
}

void sim_run(int ms) {
  for (int t = 0; t < ms; t += 5) {
    lv_tick_inc(5);
    lv_timer_handler();
  }
}

// RGB565 to RGB888, low bits filled from the high ones
static uint32_t rgb888(lv_color_t px) {
  uint16_t c = px.full;
  uint32_t r = (c >> 11) << 3 | c >> 13, g = ((c >> 5) & 63) << 2 | ((c >> 9) & 3), b = (c & 31) << 3 | ((c >> 2) & 7);
  return r << 16 | g << 8 | b;
}

uint32_t sim_pixel(int x, int y) {
  lv_refr_now(NULL);
  return rgb888(fb[y * W + x]);
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

bool sim_shot(const char *dir, const char *name) {
  sim_run(50);
  lv_refr_now(NULL);
  static uint32_t rgb[W * H];
  for (int i = 0; i < W * H; i++)
    rgb[i] = rgb888(fb[i]);
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
