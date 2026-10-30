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
#include "plat.h"
#include "room.h"
#include "rs485.h"
#include "settings.h"
#include "shot.h"
#include "trend.h"
#include "ui.h"
#include "unit.h"

model_t M;

// Two draw buffers, LVGL renders into one while DMA sends the other.
#define LV_LINES 8
_Static_assert(SCR_W *LV_LINES <= 0xFFFF, "a strip goes out in one DMA transfer");
_Static_assert(LV_LINES >= 8 && LV_LINES % 8 == 0, "pic_round and shot_round want whole 8-line strips");
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
  shot_flushed(a, px);  // while the DMA sends it
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
                       .cpu_mhz = board_hz() / 1000000,
                       .restarts = board_restarts(),
                       .full_us = full_us,
                       .full_send_us = full_send_us,
                       .stack_free = PERF.stack_free,
                       .saves = settings_saves(),
                       .heap_size = LV_MEM_SIZE,
                       .heap_used = used,
                       .heap_peak = used > heap_peak ? used : heap_peak};
}

// Processor speed. The clock only changes at a start. A speed over 72 restarts into a trial at it, saved as 72 until
// the trial is answered, so no answer or a power cut is back at 72. A crash or the watchdog over 72 sets it back to
// 72, see cpu_clock_choice(). Fast display at 80 gets a trial too, 20 MHz may be too fast for the display. Over 80
// it's off, see fast_display_ok(). Saves during a trial keep the value from before it, see val_trial().
static bool on_trial;  // waiting for ui_trial()'s answer
static int running;    // the CPU_MHZ[] index the clock runs at, 0 without the crystal

// After settings_load(). *crashed is the MHz the last run crashed at if that was over 72, the speed is back at 72
// then, else 0. True if a setting changed that's saved right away.
static bool clock_start(int *crashed) {
  uint8_t last = M.reset_cause == RESET_POWER ? 0 : board_clock_mark();  // RAM can outlast a short cut
  bool revert;
  int mhz = cpu_clock_choice(M.cpu_speed, last, M.reset_cause, &revert), s = cpu_speed_of(mhz);
  bool trial = s && CLOCK_TRIAL(last) == s;
  board_set_clock_mark(0);
  if (s) {
    board_set_clock_mark(CLOCK_MARK(s, trial ? s : 0));  // first, so a hang while switching counts too
    if (board_set_clock((uint32_t) mhz)) {
      running = s;
      M.cpu_speed = (uint8_t) s;
      on_trial = trial;
      if (trial)
        val_trial(V_CPU_SPEED);  // saved as 72 until it's kept, a restart carries on with the trial
    } else {
      board_set_clock_mark(0);  // no crystal
    }
  }
  *crashed = revert ? CPU_MHZ[CLOCK_RAN(last)] : 0;
  if (revert)
    M.cpu_speed = 0;
  bool fast_off = M.fast_display && !fast_display_ok();  // from flash, or restored by the ESP
  if (fast_off)
    M.fast_display = false;
  return revert || fast_off;
}

// Processor speed changed, which takes a restart
static void clock_changed(void) {
  if (M.cpu_speed == running)
    return;  // no crystal
  board_set_clock_mark(CLOCK_MARK(running, M.cpu_speed));
  M.cpu_speed = 0;
  settings_flush();
  board_reset();
}

void plat_trial_end(bool keep) {
  on_trial = false;
  val_trial(V_NONE);
  board_set_clock_mark(CLOCK_MARK(running, 0));
  if (keep)
    plat_save(true);  // the setting that was on trial
}

// Value changes and actions. The CX60 part of them is unit.c's, and saving is settings.c's
void plat_value_changed(val_t v) {
  if (v == V_ROOM_OFFSET)
    room_offset_changed();
  if (v == V_CPU_SPEED)
    clock_changed();
  if (v == V_FAST_DISPLAY && M.fast_display && running && !on_trial) {
    on_trial = true;
    val_trial(V_FAST_DISPLAY);
    ui_trial(V_FAST_DISPLAY);
  }
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
  int crashed;
  bool changed = clock_start(&crashed);
  unit_start();
  rs485_init();  // answer the CX60 as early as possible, it only looks for panels once
  if (changed)
    plat_save_now();

  bool known = lcd_init();
  lv_init();
  static lv_disp_draw_buf_t db;
  lv_disp_draw_buf_init(&db, draw_buf[0], draw_buf[1], SCR_W * LV_LINES);
  lv_disp_drv_init(&drv);
  drv.hor_res = SCR_W;
  drv.ver_res = SCR_H;
  drv.flush_cb = flush;
  drv.rounder_cb = shot_round;
  drv.draw_buf = &db;
  lv_disp_drv_register(&drv);
  uint32_t now_s = board_rtc_get();
  clock_from_unix(now_s);
  if (now_s > PLINK_EPOCH)
    clock_follow(now_s, now_s);  // the saved dst_now may be stale, or the settings wiped
  ui_init();
  link_init();
  if (crashed) {
    char t[48];
    lv_snprintf(t, sizeof t, _(S_CRASHED_AT_MHZ), crashed);
    ui_toast_text(IC_ALERT, t);
  }
  if (!known)
    ui_toast(IC_ALERT, S_UNKNOWN_DISPLAY_CONTROLLER);
  if (on_trial)
    ui_trial(V_CPU_SPEED);

  // Every 5 ms is plenty, the bus is answered in its interrupt. Sleep in between, awake the core runs flat out
  // at 72 MHz or more and the chip gets warm.
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
      shot_poll();
      draw();
      bool leds[N_LIGHTS];
      panel_leds(leds, t);
      board_leds(leds[LIGHT_ALARM], leds[LIGHT_FILTER], leds[LIGHT_OK]);
      board_backlight(input_backlight());
      settings_poll();
    }
    board_sleep();
  }
}
