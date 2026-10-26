// Fake data for the simulator, and a stand-in for what the CX60 does when a value changes.
#include "ui.h"
#include "plink.h"
#include "sim.h"
#include <stdlib.h>
#include <string.h>

model_t M;

// The fake ESP's items, comfort page first, then the weather and the smart home page.
// name, type, flags, icon, decimals, step, value, min, max, label, unit, help, levels
// C() = on the comfort page, L() = four levels, NOL = no levels
#define C(f) (ITEMF_COMFORT | (f))
#define L(a, b, c, d) .levels = 1, .level = {a, b, c, d}
#define NOL .levels = 0, .level = {0}
#define DEG \
  "\xC2\xB0" \
  "C"
const sim_item_t SIM_ITEMS[] = {
    {"co2", ITEM_SENSOR, C(ITEMF_VIEW | ITEMF_MENU | ITEMF_TREND), ICN_CO2, 0, 1, 640, 0, 2000,
     "CO\xE2\x82\x82|CO\xE2\x82\x82", "ppm",
     "CO\xE2\x82\x82 level in the house.|CO\xE2\x82\x82-niv\xC3\xA5"
     "et i huset.",
     L(0, 0, 800, 1200)},
    {"humidity", ITEM_SENSOR, C(ITEMF_VIEW | ITEMF_MENU | ITEMF_TREND), ICN_HUMIDITY, 0, 1, 42, 0, 100,
     "Humidity|Fuktighet", "%", "Relative humidity in the house.|Relativ fuktighet i huset.", L(20, 30, 60, 70)},
    {"fireplace", ITEM_TOGGLE, C(ITEMF_VIEW | ITEMF_MENU), ICN_FIREPLACE, 0, 1, 0, 0, 1, "Fireplace|Peis", "",
     "Blows in more air than it takes out for a while, so the fireplace draws well.|"
     "Bl\xC3\xA5ser inn mer luft enn den tar ut en stund, s\xC3\xA5 peisen trekker godt.",
     NOL},
    {"fireplace_minutes", ITEM_NUMBER, C(ITEMF_MENU), ICN_TIMER_SAND, 0, 1, 15, 1, 240, "Fireplace time|Peistid", "min",
     "How long fireplace mode lasts.|Hvor lenge peismodus varer.", NOL},
    {"humidity_boost", ITEM_TOGGLE, C(ITEMF_VIEW | ITEMF_MENU), ICN_SHOWER, 0, 1, 0, 0, 1,
     "Humidity boost|Fuktforsering", "", "", NOL},
    {"humidity_trigger", ITEM_NUMBER, C(ITEMF_MENU), ICN_HUMIDITY, 1, 5, 80, 20, 300, "Humidity trigger|Fuktterskel",
     "%", "", NOL},
    {"co2_control", ITEM_TOGGLE, C(ITEMF_VIEW | ITEMF_MENU), ICN_CO2, 0, 1, 1, 0, 1,
     "CO\xE2\x82\x82 control|CO\xE2\x82\x82-styring", "", "", NOL},
    {"co2_target", ITEM_NUMBER, C(ITEMF_MENU), ICN_CO2, 0, 50, 900, 400, 1500,
     "CO\xE2\x82\x82 target|CO\xE2\x82\x82-m\xC3\xA5l", "ppm", "", NOL},
    {"summer_cooling", ITEM_TOGGLE, C(ITEMF_VIEW | ITEMF_MENU), ICN_SUN_THERMO, 0, 1, 0, 0, 1,
     "Summer cooling|Sommerkj\xC3\xB8ling", "", "", NOL},
    {"weather", ITEM_SENSOR, 0, ICN_SUN, 1, 1, 42, -500, 500, "Fair|Lettskyet", DEG, "", NOL},
    // smart home page, sent by sim_esp_items()
    {"living_room", ITEM_SENSOR, ITEMF_VIEW | ITEMF_MENU, ICN_HOME_THERMO, 1, 1, 216, -500, 1000, "Living room|Stue",
     DEG, "", NOL},
    {"power", ITEM_SENSOR, ITEMF_VIEW | ITEMF_MENU, ICN_BOLT, 0, 1, 1240, 0, 20000, "Power use|Str\xC3\xB8mbruk", "W",
     "", NOL},
    {"price", ITEM_SENSOR, ITEMF_VIEW | ITEMF_MENU, ICN_CASH, 2, 1, 142, 0, 10000, "Electricity|Str\xC3\xB8mpris",
     "kr/kWh", "", NOL},
    {"hallway_light", ITEM_TOGGLE, ITEMF_VIEW | ITEMF_MENU, ICN_BULB, 0, 1, 1, 0, 1, "Hallway light|Ganglys", "", "",
     NOL},
    {"washer", ITEM_TEXT, ITEMF_VIEW | ITEMF_MENU, ICN_WASHER, 0, 1, 0, 0, 1, "Washer|Vaskemaskin", "", "", NOL,
     .text = "Washing|Vasker"},
    {"bedroom", ITEM_NUMBER, ITEMF_VIEW | ITEMF_MENU, ICN_THERMOSTAT, 1, 5, 195, 150, 250, "Bedroom|Soverom", DEG, "",
     NOL},
    {"vacuum", ITEM_BUTTON, ITEMF_VIEW | ITEMF_MENU, ICN_VACUUM, 0, 1, 0, 0, 1, "Robot vacuum|Robotst\xC3\xB8vsuger",
     "", "", NOL},
    {"tomorrow", ITEM_SENSOR, ITEMF_MENU, ICN_SNOWY, 1, 1, -40, -500, 500, "Tomorrow|I morgen", DEG, "", NOL},
};
const int SIM_N_ITEMS = LEN(SIM_ITEMS), SIM_N_COMFORT = 10;

// The part of "English|Norsk" in the panel's language, or all of it if there's no '|'.
void sim_text(char *out, int size, const char *both) {
  const char *bar = strchr(both, '|'), *from = M.language && bar ? bar + 1 : both;
  int n = (int) (M.language || !bar ? strlen(from) : (size_t) (bar - both));
  n = n < size - 1 ? n : size - 1;
  memcpy(out, from, n);
  out[n] = 0;
}

const char *sim_item_label(const char *name) {
  static char b[ITEM_LABEL_LEN];
  for (int i = 0; i < SIM_N_ITEMS; i++)
    if (!strcmp(SIM_ITEMS[i].name, name))
      sim_text(b, sizeof b, SIM_ITEMS[i].label);
  return b;
}

void sim_item(int i, item_t *it) {
  const sim_item_t *d = &SIM_ITEMS[i];
  *it = (item_t) {.type = d->type,
                  .flags = (uint8_t) (d->flags | (d->levels ? ITEMF_LEVELS : 0)),
                  .icon = d->icon,
                  .dec = d->dec,
                  .step = d->step,
                  .id = plink_item_id(d->name),
                  .value = d->value,
                  .min = d->min,
                  .max = d->max};
  memcpy(it->level, d->level, sizeof it->level);
  sim_text(it->label, sizeof it->label, d->label);
  sim_text(it->unit, sizeof it->unit, d->unit);
  if (d->text)
    sim_text(it->text, sizeof it->text, d->text);
}

void sim_model_init(void) {
  model_defaults();
  M.t_outdoor = -32;
  M.t_supply = 198;
  M.t_extract = 221;
  M.t_water = T_NONE;
  M.t_room = 214;
  M.setpoint = 200;
  M.setpoint_now = 200;
  M.supply_min = 150;
  M.supply_max = 350;
  M.regulation = 0;
  M.mode = M.mode_now = MODE_NORMAL;
  M.boost_minutes = 30;
  M.fan_sup = 75;
  M.fan_ext = 75;
  const uint8_t fp[3][2] = {{50, 50}, {75, 75}, {100, 100}};
  memcpy(M.fan_pct, fp, sizeof fp);
  M.rotor_pct = 82;
  M.heat_pct = 12;
  M.heater_enabled = true;
  M.filter_hours = 2900;
  M.filter_days = 180;
  M.filter_resets = 7;
  // the installer's settings as they come from the factory
  M.adv.boost_speed = M.adv.fireplace_sup = MODE_MAX;
  M.adv.cool_min_speed = MODE_NORMAL;
  M.adv.away_speed = M.adv.fireplace_ext = MODE_MIN;
  M.adv.fire_mode = M.adv.rotor_alarm = 1;
  M.adv.extract_sensor = M.adv.outdoor_sensor = true;
  M.adv.neutral_cool = M.adv.neutral_heat = M.adv.recovery_diff = 10;
  M.adv.cool_min_outdoor = 170;
  M.adv.deice_temp = -150;
  M.adv.less_air_temp = -250;
  M.adv.deice_rotor = 25;
  M.adv.deice_period[0] = M.adv.deice_period[1] = 10;
  M.adv.deice_freq[0] = M.adv.deice_freq[1] = 2;
  M.adv.deice_fan = 50;
  M.adv.deice_ramp = 1;
  M.adv.away_setpoint = 180;
  M.adv.away_hold = 60;
  M.adv.cool_delay = M.adv.run_on = 180;
  M.adv.start_delay[1] = M.adv.start_delay[2] = M.adv.start_delay[3] = 20;
  static const uint16_t pi[12] = {60, 900, 60, 900, 0, 90, 0, 90, 0, 90, 0, 90};
  memcpy(M.adv.pi, pi, sizeof pi);
  M.panel_version[2] = 0x0001;  // this one
  M.dip = 0x1;                  // DS1 electric heater, EC fans
  M.week_program = true;
  static const period_t wk[] = {{6 * 60 + 30, 8 * 60, MODE_NORMAL, 0, 210},
                                {8 * 60, 15 * 60 + 30, MODE_MIN, 1, 190},
                                {15 * 60 + 30, 22 * 60 + 30, MODE_NORMAL, 2, 210}};
  static const period_t we[] = {{8 * 60, 23 * 60, MODE_NORMAL, 0, 210}, {17 * 60, 19 * 60, MODE_MAX, 1, 210}};
  for (int d = 0; d < 7; d++) {
    if (d < 5) {
      memcpy(M.week[d], wk, sizeof wk);
      M.n_periods[d] = 3;
    } else {
      memcpy(M.week[d], we, sizeof we);
      M.n_periods[d] = 2;
    }
  }
  M.week[4][2].end = 23 * 60 + 30;
  M.cpu_busy = 6;
  if (getenv("SIM_LANG"))
    M.language = (uint8_t) atoi(getenv("SIM_LANG"));
  M.esp_online = true;
  for (int i = 0; i < SIM_N_COMFORT; i++)  // as if the ESP already sent its comfort page and the weather
    sim_item(i, &M.items[i]);
  M.item_trends[0].id = M.items[0].id;  // 24 h trends for CO2 and humidity
  M.item_trends[1].id = M.items[1].id;
  // The last 24 hours up to 13:00, a daily sine plus some wobble. Integer math so it's the same everywhere.
  for (int i = 0; i < TREND_LEN; i++) {
    int q = (i + 52) % TREND_LEN;  // quarter hours after midnight
    int day = lv_trigo_sin((int16_t) ((q * 15 / 4 + 225) % 360)), wobble = lv_trigo_sin((int16_t) (i * 40 % 360));
    M.trend[TR_OUTDOOR][i] = trend_code(TR_OUTDOOR, -45 + 25 * day / LV_TRIGO_SIN_MAX + 4 * wobble / LV_TRIGO_SIN_MAX);
    M.trend[TR_SUPPLY][i] = trend_code(TR_SUPPLY, 192 + 6 * day / LV_TRIGO_SIN_MAX + (i % 11 == 0 ? 4 : 0));
    M.trend[TR_EXTRACT][i] = trend_code(TR_EXTRACT, 218 + 5 * day / LV_TRIGO_SIN_MAX);
    M.trend[TR_ROOM][i] = trend_code(TR_ROOM, 212 + 3 * day / LV_TRIGO_SIN_MAX);
    int t = i - 44;  // quarter hours since today's midnight, negative for yesterday
    int home = (t > 28 && t < 36) || t > 64 ? 10 : t < 28 ? 6 : 2;  // occupancy in tenths, 10 = everyone home
    M.item_trends[0].pt[i] =
        (int16_t) (480 + 60 * home + 40 * lv_trigo_sin((int16_t) (i * 52 % 360)) / LV_TRIGO_SIN_MAX);
    M.item_trends[1].pt[i] = (int16_t) (38 + 8 * home / 10 + (i == 70 ? 25 : i == 71 ? 15 : 0));  // a shower at 6:30
  }
  M.cx60_version = 0x0207;
  M.fw_version = 0x000100;  // 0.1.0
  M.bus_addr = 3;
  M.bus_ok = 184233;
  M.bus_err = 41;
  M.h_total = 41872;
  M.h_stop = 312;
  M.h_min = 9876;
  M.h_normal = 29011;
  M.h_max = 2673;
  M.h_rotor = 38110;
  M.h_heater = 4120;
  M.history[0] = (alarm_t) {10, 2, 9, 7, 12, false};
  M.history[1] = (alarm_t) {6, 14, 1, 3, 40, false};
  M.history[2] = (alarm_t) {9, 28, 11, 19, 5, false};
  M.n_history = 3;
  M.hh = 13;
  M.mi = 4;
  M.dd = 18;
  M.mo = 10;
  M.yy = 2026;
  M.wday = 5;
  M.brightness = 80;
  M.idle_brightness = 10;
  M.screensaver = 2;
  M.features = FBIT(FEAT_ESP) | FBIT(FEAT_EC_FANS);
}

// Roughly what the CX60 would do. On the panel, hw/unit.c puts the change on the bus instead.
void plat_value_changed(val_t v) {
  switch (v) {
    case V_MODE:
      M.mode_now = M.mode;
      M.fan_sup = M.mode ? M.fan_pct[M.mode - 1][0] : 0;
      M.fan_ext = M.mode ? M.fan_pct[M.mode - 1][1] : 0;
      break;
    case V_BOOST:
      M.boost_left = M.boost_on ? M.boost_minutes : 0;
      if (M.boost_on) {
        M.fan_sup = M.fan_pct[2][0];
        M.fan_ext = M.fan_pct[2][1];
      } else
        plat_value_changed(V_MODE);
      break;
    default:
      break;
  }
}

void plat_time_set(void) {}
void plat_cx60_reg(uint16_t reg, int16_t value) {  // hw/unit.c takes it like the CX60 sent it
  val_t v = val_by_cx60(reg);
  if (v != V_NONE)
    val_update(v, value);
}
void plat_save(bool by_user) { (void) by_user; }
void plat_save_now(void) {}
bool sim_fresh;
bool plat_fresh(void) { return sim_fresh; }

void plat_period_set(void) {}  // schedule.c keeps M.week itself

void plat_action(int a) {
  if (a == A_RESET_FILTER)
    M.filter_hours = 0;
  if (a == A_RESET_ALARMS)
    M.n_alarms = 0;
  if (a == A_CLEAR_HISTORY)
    M.n_history = 0;
  if (a == A_RESET_ALARMS || a == A_CLEAR_HISTORY)
    ui_alarms_changed();  // as read_alarms() and read_log() do on the unit
  if (a == A_RESET_PANEL)
    panel_defaults();  // the panel restarts after this
}
