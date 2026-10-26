// Start-up and the main loop.
#include "board.h"
#include "clock.h"
#include "input.h"
#include "layout.h"
#include "lcd.h"
#include "lvgl.h"
#include "link.h"
#include "mbserver.h"
#include "panel.h"
#include "picture.h"
#include "plat.h"
#include "room.h"
#include "rs485.h"
#include "settings.h"
#include "trend.h"
#include "ui.h"
#include "unit.h"

model_t M;

// Two draw buffers, LVGL renders into one while DMA sends the other.
#define LV_LINES 8
_Static_assert(SCR_W *LV_LINES <= 0xFFFF, "a strip goes out in one DMA transfer");
static lv_color_t draw_buf[2][SCR_W * LV_LINES];
static lv_disp_drv_t drv;

// Drawing time and CPU load. tools/perf.py reads this over SWD and knows the layout.
volatile struct {
  uint32_t frames;                  // lv_timer_handler calls that drew something
  uint32_t last_us, max_us;         // the whole call, render + flush
  uint32_t last_flush_us, last_px;  // part of that spent sending to the display, and pixels sent
  uint32_t keys;                    // key presses
  uint32_t busy_pm;                 // awake time over the last second, per mille
  uint32_t stack_free;              // bytes of stack never used so far
} PERF;
volatile uint8_t dbg_key;  // write a KEY_x here over SWD to press that key
static uint32_t flush_px;

bool plat_const(const void *p) { return (uintptr_t) p >= FLASH_BASE_ADDR && (uintptr_t) p < FLASH_SETTINGS; }
uint32_t plat_entropy(void) { return board_entropy(); }

static void flushed(void) { lv_disp_flush_ready(&drv); }

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px) {
  if (ui_held()) {
    lv_disp_flush_ready(d);
    return;
  }
  lcd_draw(a->x1, a->y1, a->x2, a->y2, (const uint16_t *) px, flushed);
  flush_px += (uint32_t) (a->x2 - a->x1 + 1) * (uint32_t) (a->y2 - a->y1 + 1);
}

static uint32_t heap_peak;  // most of LVGL's memory seen in use, checked after each redraw

static uint32_t heap_used(void) {
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  return m.total_size - m.free_size;
}

static uint32_t full_us, full_send_us;  // the last big redraw scaled to the whole screen, for Diagnostics

static void draw(void) {
  flush_px = 0;
  lcd_send_us = 0;
  lcd_fast(M.fast_display);
  uint32_t t = board_us();
  lv_timer_handler();
  if (!flush_px)
    return;
  bool big = flush_px >= (uint32_t) SCR_W * SCR_H / 2;  // a new page, the status bar stays
  if (big)
    lcd_wait();  // so the last strip counts
  uint32_t us = board_us() - t;
  if (big) {
    full_us = (uint32_t) ((uint64_t) us * SCR_W * SCR_H / flush_px);
    full_send_us = (uint32_t) ((uint64_t) lcd_send_us * SCR_W * SCR_H / flush_px);
  }
  PERF.frames++;
  PERF.last_us = us;
  if (us > PERF.max_us)
    PERF.max_us = us;
  PERF.last_flush_us = lcd_send_us;  // DMA time, mostly overlapping LVGL drawing the next strip
  PERF.last_px = flush_px;
  uint32_t used = heap_used();
  if (used > heap_peak)
    heap_peak = used;
}

static void keys(uint32_t ms) {
  uint8_t up, down = board_key_events(&up);
  if (down)
    PERF.keys++;
  input_keys(down, up, board_keys(), ms);
  if (dbg_key) {
    input_activity();
    ui_key(dbg_key);
    dbg_key = 0;
  }
}

static uint32_t uptime_s;

void plat_stats(plat_stats_t *s) {
  uint32_t used = heap_used();
  *s = (plat_stats_t) {.uptime_s = uptime_s,
                       .restarts = board_restarts(),
                       .full_us = full_us,
                       .full_send_us = full_send_us,
                       .stack_free = PERF.stack_free,
                       .saves = settings_saves(),
                       .heap_size = LV_MEM_SIZE,
                       .heap_used = used,
                       .heap_peak = used > heap_peak ? used : heap_peak};
}

// Value changes and actions. The CX60 part of them is unit.c's, and saving is settings.c's
void plat_value_changed(val_t v) {
  if (v == V_ROOM_OFFSET)
    room_offset_changed();
  unit_value_changed(v);
}

void plat_action(int a) {
  switch (a) {
    case A_RESET_PANEL:
      panel_defaults();
      plat_save(true);
      // fall through
    case A_RESTART:
      settings_flush();
      board_reset();
      break;
    default:
      unit_action(a);
      break;
  }
}

// An updater asked for the bootloader. Its reply goes out first, then the settings are saved.
#define BOOTLOADER_REPLY_MS 20  // the 6 bytes take under 1 ms
static void bootloader(void) {
  for (uint32_t t = board_ms(); rs485_busy() && board_ms() - t < BOOTLOADER_REPLY_MS;) {
  }
  settings_flush();
  board_bootloader();
}

static void second(void) {
  uptime_s++;
  board_rtc_poll();
  clock_from_unix(board_rtc_get());
  clock_second();
  unit_second();
  room_second();
  trend_second();
  link_second();
  ui_second();
  PERF.busy_pm = board_busy_pm();
  PERF.stack_free = board_stack_free();
  M.cpu_busy = (uint8_t) ((PERF.busy_pm + 5) / 10);
}

int main(void) {
  board_init();
  model_defaults();
  M.reset_cause = board_reset_cause(&M.fault_pc);
  unit_init();
  settings_load();  // panel settings and the cached CX60 settings
  unit_start();
  rs485_init();  // answer the CX60 as early as possible, it only looks for panels once

  bool known = lcd_init();
  lv_init();
  static lv_disp_draw_buf_t db;
  lv_disp_draw_buf_init(&db, draw_buf[0], draw_buf[1], SCR_W * LV_LINES);
  lv_disp_drv_init(&drv);
  drv.hor_res = SCR_W;
  drv.ver_res = SCR_H;
  drv.flush_cb = flush;
  drv.rounder_cb = pic_round;
  drv.draw_buf = &db;
  lv_disp_drv_register(&drv);
  uint32_t now_s = board_rtc_get();
  clock_from_unix(now_s);
  if (now_s > PLINK_EPOCH)
    clock_follow(now_s, now_s);  // the saved dst_now may be stale, or the settings wiped
  ui_init();
  link_init();
  if (!known)
    ui_toast(IC_ALERT, S_UNKNOWN_DISPLAY_CONTROLLER);

  // Every 5 ms is plenty, the bus is answered in its interrupt. Sleep in between, awake the core runs flat out
  // at 72 MHz and the chip gets warm.
  uint32_t next_s = board_ms() + 1000, next = 0;
  for (;;) {
    board_kick();
    uint32_t t = board_ms();
    if ((int32_t) (t - next) >= 0) {
      next = t + 5;
      keys(t);
      unit_poll();
      if (mb_bootloader_requested())
        bootloader();
      link_poll();
      if ((int32_t) (t - next_s) >= 0) {
        next_s += 1000;
        second();
      }
      input_tick(t);
      draw();
      bool leds[3];
      panel_leds(leds, t);
      board_leds(leds[0], leds[1], leds[2]);
      board_backlight(input_backlight());
      settings_poll();
    }
    board_sleep();
  }
}
