// see input.h
#include "input.h"
#include "panel.h"
#include "ui.h"

uint16_t input_hold_ms;
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
  static const int TO_UI[IN_COUNT] = {
      [IN_UP] = KEY_UP, [IN_DOWN] = KEY_DOWN, [IN_OK] = KEY_OK, [IN_BACK] = KEY_BACK, [IN_HELP] = KEY_HELP};
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
      next_rep[k] = ms + 450;
      long_done &= (uint8_t) ~bit;
      last = ms;
      input_hold_ms = 0;
      if (M.key_lock && k != IN_HELP) {
        ui_toast(IC_LOCK, S_KEYS_LOCKED_HOW);
        ignore |= bit;
        continue;
      }
      if (k != IN_BACK && k != IN_HELP)
        ui_key(TO_UI[k]);
    } else if ((held & bit) && !(up & bit) && !(long_done & bit)) {  // held
      uint32_t t = ms - down_at[k];
      if ((k == IN_OK && t > 2000) || (k == IN_BACK && t > 800)) {
        long_done |= bit;
        ui_key(k == IN_OK ? KEY_OK_LONG : KEY_HOME);
      } else if (k == IN_HELP && t > 3000) {
        long_done |= bit;
        val_set(V_KEY_LOCK, !M.key_lock);
        ui_toast(M.key_lock ? IC_LOCK : IC_UNLOCK, M.key_lock ? S_KEYS_LOCKED : S_KEYS_UNLOCKED);
      } else if ((k == IN_UP || k == IN_DOWN) && (int32_t) (ms - next_rep[k]) >= 0) {
        next_rep[k] = ms + (t > 3000 ? 60 : 120);
        input_hold_ms = (uint16_t) (t > 60000 ? 60000 : t);
        ui_key(TO_UI[k]);
        last = ms;
      }
    }
    if ((up & bit) && !(long_done & bit)) {  // BACK and HELP act on release, so a long press can do something else
      if (k == IN_BACK)
        ui_key(KEY_BACK);
      else if (k == IN_HELP)
        M.key_lock ? ui_toast(IC_LOCK, S_KEYS_LOCKED_HOW) : ui_key(KEY_HELP);
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
