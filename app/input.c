// see input.h
#include "input.h"
#include "model.h"
#include "panel.h"
#include "ui.h"

// Key timing, ms, see input.h
#define REPEAT_FIRST_MS 450  // UP/DOWN held this long start repeating
#define REPEAT_MS 120        // every this often
#define FAST_AFTER_MS 3000   // and once held this long
#define REPEAT_FAST_MS 60    // every this often
#define HOLD_MAX_MS 60000    // where input_held_ms() stops
#define OK_LONG_MS 2000      // OK held, KEY_OK_LONG
#define HOME_MS 800          // BACK held, KEY_HOME
#define LOCK_MS 3000         // HELP held, locks or unlocks the keys

static uint16_t hold_ms;    // how long the repeating key has been held
static uint32_t now, last;  // now, and the last key press or news
static bool idle;

void input_activity(void) {
  last = now;
  if (idle) {
    idle = false;
    ui_wake();
  }
}

bool input_idle(void) { return idle; }

uint32_t input_held_ms(void) { return hold_ms; }

void input_tick(uint32_t ms) {
  now = ms;
  uint16_t after = idle_seconds();
  if (idle || !after || ms - last < after * 1000u)
    return;
  idle = true;
  if (M.auto_lock && !M.key_lock)
    val_set(V_KEY_LOCK, 1);  // System > Lock when idle
  ui_idle();
}

void input_keys(uint8_t down, uint8_t up, uint8_t held, uint32_t ms) {
  static uint32_t down_at[IN_COUNT], next_rep[IN_COUNT];
  // ignore = keys that woke the panel or hit the lock, until released. long_done = long press already fired
  static uint8_t ignore, long_done;
  // the keys that act when pressed, BACK and HELP act on release
  static const uint8_t TO_UI[] = {[IN_UP] = KEY_UP, [IN_DOWN] = KEY_DOWN, [IN_OK] = KEY_OK};
  now = ms;
  if (down && idle) {  // the first press only wakes it up
    ignore |= down;
    input_activity();
  }
  for (int k = 0; k < IN_COUNT; k++) {
    uint8_t bit = (uint8_t) (1u << k);
    if (ignore & bit) {
      if (!(held & bit))
        ignore &= (uint8_t) ~bit;
      continue;
    }
    if (down & bit) {  // pressed, maybe released in the same call too
      down_at[k] = ms;
      next_rep[k] = ms + REPEAT_FIRST_MS;
      long_done &= (uint8_t) ~bit;
      last = ms;
      hold_ms = 0;
      if (M.key_lock && k != IN_HELP) {
        ui_key_lock_notice(false);
        ignore |= bit;
        continue;
      }
      if (k != IN_BACK && k != IN_HELP)
        ui_key(TO_UI[k]);
    } else if ((held & bit) && !(up & bit) && !(long_done & bit)) {  // held
      uint32_t t = ms - down_at[k];
      if ((k == IN_OK && t > OK_LONG_MS) || (k == IN_BACK && t > HOME_MS)) {
        long_done |= bit;
        ui_key(k == IN_OK ? KEY_OK_LONG : KEY_HOME);
      } else if (k == IN_HELP && t > LOCK_MS) {
        long_done |= bit;
        val_set(V_KEY_LOCK, !M.key_lock);
        ui_key_lock_notice(true);
      } else if ((k == IN_UP || k == IN_DOWN) && (int32_t) (ms - next_rep[k]) >= 0) {
        next_rep[k] = ms + (t > FAST_AFTER_MS ? REPEAT_FAST_MS : REPEAT_MS);
        hold_ms = (uint16_t) (t > HOLD_MAX_MS ? HOLD_MAX_MS : t);
        ui_key(TO_UI[k]);
        last = ms;
      }
    }
    if ((up & bit) && !(long_done & bit)) {  // BACK and HELP act on release, so a long press can do something else
      if (k == IN_BACK)
        ui_key(KEY_BACK);
      else if (k == IN_HELP)
        M.key_lock ? ui_key_lock_notice(false) : ui_key(KEY_HELP);
    }
  }
}

int input_backlight(void) {
  int preview = ui_backlight_preview();
  if (preview >= 0)
    return preview;
  if (!idle)
    return night_active() ? M.night_brightness : M.brightness;
  int pct = night_active() ? M.night_idle_brightness : M.idle_brightness;
  return pct ? pct : -1;  // 0 means off
}
