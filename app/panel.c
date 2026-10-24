// see panel.h
#include "panel.h"
#include "model.h"
#include "ui.h"
#include <string.h>

const uint16_t TILE_DEFAULTS[4] = {TILE_OUTDOOR, TILE_SUPPLY, TILE_ROTOR, TILE_FILTER};

void panel_defaults(void) {
  M.theme = M.night_theme = 0;
  M.brightness = 90;
  M.idle_brightness = 20;
  M.idle_after = 3;  // 2 min
  M.screensaver = SAVER_PIC_ALL;
  M.saver_text = SAVER_TEXT_PLAIN;
  M.saver_top = false;
  M.home_view = VIEW_DASH;
  M.go_home = true;
  for (int v = 0; v < VIEW_COUNT; v++)
    M.view_on[v] = true;
  M.dash_temp = DT_INDOOR;
  memcpy(M.tiles, TILE_DEFAULTS, sizeof TILE_DEFAULTS);
  M.clock_24h = M.key_hints = M.temp_dec = M.fast_display = true;
  M.language = 0;
  M.room_offset = -40;  // the same fixed 4.0 C the stock firmware uses
  M.indoor_source = INDOOR_EXTRACT;
  M.night = false;
  M.night_from = 22;
  M.night_to = 7;
  M.night_brightness = 50;
  M.night_idle_brightness = 0;
  M.led_ok = LED_OK_NOT_NIGHT;
  M.led_alarm = M.led_filter = true;
  M.key_lock = M.auto_lock = false;
  M.alarm_popups = POPUPS_ALL;
  M.auto_dst = true;
}

void model_defaults(void) {
  memset(&M, 0, sizeof M);
  M.bus_addr = 3;  // the ESP uses 1
  panel_defaults();
  M.features = FBIT(FEAT_ESP) | FBIT(FEAT_EC_FANS);
  M.mode = M.mode_now = MODE_NORMAL;
  M.filter_days = 180;
  M.boost_minutes = 30;
  M.t_supply = M.t_extract = M.t_outdoor = M.t_water = M.t_room = T_NONE;
}

bool night_active(void) {
  if (!M.night || M.night_from == M.night_to)
    return false;
  int h = M.hh;
  return M.night_from < M.night_to ? h >= M.night_from && h < M.night_to : h >= M.night_from || h < M.night_to;
}

uint8_t theme_now(void) { return night_active() && M.night_theme ? M.night_theme - 1 : M.theme; }

uint16_t idle_seconds(void) {
  static const uint16_t S[IDLE_STEPS] = {15, 30, 60, 120, 300, 600, 1800, 0};
  return S[M.idle_after];
}

int16_t indoor_temp(void) {
  if (M.indoor_source == INDOOR_PANEL && M.t_room != T_NONE)
    return M.t_room;
  return M.t_extract != T_NONE ? M.t_extract : M.t_room;
}

int heat_recovery(void) {
  if (M.t_extract == T_NONE || M.t_outdoor == T_NONE || M.t_supply == T_NONE || M.t_extract - M.t_outdoor <= 30 ||
      M.heat_pct)
    return -1;
  int eff = (M.t_supply - M.t_outdoor) * 100 / (M.t_extract - M.t_outdoor);
  return eff < 0 ? 0 : eff > 100 ? 100 : eff;
}

int filter_days_left(void) {
  int h = (int) FILTER_INTERVAL_H - (int) M.filter_hours;
  return h <= 0 ? 0 : (h + 23) / 24;
}

int filter_life(void) {
  if (!M.filter_days)
    return 0;  // the CU60 allows a 0 day interval
  int life = 100 - (int) ((uint32_t) M.filter_hours * 100 / FILTER_INTERVAL_H);
  return life < 0 ? 0 : life;
}

bool alarm_pops(int code) {
  return M.alarm_popups != POPUPS_OFF && (code != ALARM_FILTER || M.alarm_popups == POPUPS_ALL);
}

int alarm_new_pops(void) {
  static uint8_t before[LEN(M.alarms)], n_before;
  int pop = 0;
  for (int i = 0; i < M.n_alarms && !pop; i++) {  // the first is the most serious
    uint8_t c = M.alarms[i].code;
    if (!memchr(before, c, n_before) && alarm_pops(c))
      pop = c;
  }
  n_before = M.n_alarms;
  for (int i = 0; i < M.n_alarms; i++)
    before[i] = M.alarms[i].code;
  return pop;
}

void panel_leds(bool out[3], uint32_t ms) {
  bool filter = false, other = false;
  for (int i = 0; i < M.n_alarms; i++) {
    if (M.alarms[i].code == ALARM_FILTER || M.alarms[i].code == ALARM_FILTER_GUARD)
      filter = true;
    else
      other = true;
  }
  bool ok = !other && !filter && (M.led_ok == LED_OK_ALWAYS || (M.led_ok == LED_OK_NOT_NIGHT && !night_active()));
  bool want[3] = {other && M.led_alarm, filter && M.led_filter, ok};
  for (int i = 0; i < 3; i++) {
    switch ((M.leds >> (2 * i)) & 3) {  // ESP override, LED_*. 1 = off, 2 = on, 3 = blink
      case 1:
        want[i] = false;
        break;
      case 2:
        want[i] = true;
        break;
      case 3:
        want[i] = ms / 500 % 2;
        break;
    }
    out[i] = want[i];
  }
  int screen = ui_ok_led(ms);
  if (screen >= 0)
    out[2] = screen;
}
