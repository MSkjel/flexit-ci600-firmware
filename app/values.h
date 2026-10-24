// V_* ids for every value. The ones shared with the ESP come from proto/plink_values.h, which explains the
// columns. Panel-only ones are below, then the equipment bits and ESP items.
#pragma once

// clang-format off: a table, laid out by hand
#define PANEL_VALUES(X) \
    /* panel-only, not sent to the ESP. The key is only for flash */ \
    /* display */ \
    X(NIGHT_THEME,     0x0A0, M.night_theme,        0, N_THEMES, VF_SAVED | VF_LAYOUT, 0, 1) /* 0 = same as day */ \
    X(DASH_TEMP,       0x0A1, M.dash_temp,          0, DT_COUNT - 1, VF_SAVED | VF_LAYOUT, 0, 1) /* DT_* */ \
    X(SAVER_TEXT,      0x0A2, M.saver_text,         0, SAVER_TEXT_COUNT - 1, VF_SAVED | VF_LAYOUT, 0, 1) \
    X(SAVER_TOP,       0x0A3, M.saver_top,          0, 1,    VF_SAVED | VF_LAYOUT, 0, 1) \
    X(GO_HOME,         0x0A4, M.go_home,            0, 1,    VF_SAVED, 0, 1) \
    X(TEMP_DEC,        0x0A5, M.temp_dec,           0, 1,    VF_SAVED, 0, 1) \
    X(FAST_DISPLAY,    0x0A6, M.fast_display,       0, 1,    VF_SAVED, 0, 1) \
    /* home views */ \
    X(SHOW_FLOW,       0x0B0, M.view_on[VIEW_FLOW], 0, 1,    VF_SAVED | VF_LAYOUT, 0, 1) \
    X(SHOW_TREND,      0x0B1, M.view_on[VIEW_TREND], 0, 1,   VF_SAVED | VF_LAYOUT, 0, 1) \
    X(SHOW_SENSORS,    0x0B2, M.view_on[VIEW_SENSORS], 0, 1, VF_SAVED | VF_LAYOUT, 0, 1) \
    X(SHOW_COMFORT,    0x0B3, M.view_on[VIEW_COMFORT], 0, 1, VF_SAVED | VF_LAYOUT, 0, 1) \
    X(SHOW_SMART,      0x0B4, M.view_on[VIEW_SMART], 0, 1,   VF_SAVED | VF_LAYOUT, 0, 1) \
    /* lights */ \
    X(LED_OK,          0x0B8, M.led_ok,             0, LED_OK_COUNT - 1, VF_SAVED, 0, 1) \
    X(LED_ALARM,       0x0B9, M.led_alarm,          0, 1,    VF_SAVED, 0, 1) \
    X(LED_FILTER,      0x0BA, M.led_filter,         0, 1,    VF_SAVED, 0, 1) \
    /* system */ \
    X(AUTO_LOCK,       0x0C0, M.auto_lock,          0, 1,    VF_SAVED, 0, 1) \
    X(ALARM_POPUPS,    0x0C1, M.alarm_popups,       0, POPUPS_COUNT - 1, VF_SAVED, 0, 1) \
    X(AUTO_DST,        0x0C2, M.auto_dst,           0, 1,    VF_SAVED, 0, 1) \
    X(DST_NOW,         0x0C3, M.dst_now,            0, 1,    VF_SAVED | VF_RO, 0, 1) /* kept across restarts */ \
    X(ADVANCED,        0x0C4, M.advanced,           0, 1,    VF_SAVED | VF_LAYOUT, 0, 1) \
    /* the unit's values. The CX60 relays every change to all panels and the ESP, so no key */ \
    X(MODE,            0,     M.mode,               0, MODE_MAX, VF_CX60 | VF_CMD, 0x000, 1) \
    X(BOOST,           0,     M.boost_on,           0, 1,    VF_CX60 | VF_CMD, 0, 1) /* commands 0x13A/0x13B */ \
    X(BOOST_MINUTES,   0,     M.boost_minutes,      0, 360,  VF_CX60, 0x091, 1) \
    X(SUPPLY_MIN,      0,     M.supply_min,        50, 250,  VF_CX60, 0x00D, 1) \
    X(SUPPLY_MAX,      0,     M.supply_max,       150, 450,  VF_CX60, 0x00E, 1) \
    X(REGULATION,      0,     M.regulation,         0, REG_COUNT - 1, VF_CX60, 0x114, 1) \
    X(HEATER,          0,     M.heater_enabled,     0, 1,    VF_CX60 | VF_PER_PANEL, 0x13F, 1) \
    X(COOLING,         0,     M.cooling_enabled,    0, 1,    VF_CX60, 0x115, 1) \
    X(FAN_MIN_SUP,     0,     M.fan_pct[0][0],     20, 100,  VF_CX60, 0x002, 1) \
    X(FAN_MAX_SUP,     0,     M.fan_pct[2][0],     20, 100,  VF_CX60, 0x004, 1) \
    X(FAN_MIN_EXT,     0,     M.fan_pct[0][1],     20, 100,  VF_CX60, 0x007, 1) \
    X(FAN_MAX_EXT,     0,     M.fan_pct[2][1],     20, 100,  VF_CX60, 0x009, 1) \
    X(FILTER_INTERVAL, 0,     M.filter_days,       30, 360,  VF_CX60, 0x016, 1) /* days */ \
    X(WEEK_PROGRAM,    0,     M.week_program,       0, 1,    VF_CX60, 0x117, 1) \
    /* the installer's settings, kept in the CX60. The ranges are stock's */ \
    X(BOOST_SPEED,     0,     M.adv.boost_speed,    1, 3,    VF_CX60, 0x090, 1) /* AC fans */ \
    X(NEUTRAL_COOL,    0,     M.adv.neutral_cool,   0, 50,   VF_CX60, 0x011, 1) \
    X(NEUTRAL_HEAT,    0,     M.adv.neutral_heat,   0, 50,   VF_CX60, 0x012, 1) \
    X(PI_HEAT_SUP_K,   0,     M.adv.pi[0],          0, 32767, VF_CX60, 0x0B5, 1) \
    X(PI_HEAT_SUP_I,   0,     M.adv.pi[1],          0, 32767, VF_CX60, 0x0B6, 1) \
    X(PI_HEAT_EXT_K,   0,     M.adv.pi[2],          0, 32767, VF_CX60, 0x0AD, 1) \
    X(PI_HEAT_EXT_I,   0,     M.adv.pi[3],          0, 32767, VF_CX60, 0x0AE, 1) \
    X(PI_ROTOR_SUP_K,  0,     M.adv.pi[4],          0, 32767, VF_CX60, 0x0B7, 1) \
    X(PI_ROTOR_SUP_I,  0,     M.adv.pi[5],          0, 32767, VF_CX60, 0x0B8, 1) \
    X(PI_ROTOR_EXT_K,  0,     M.adv.pi[6],          0, 32767, VF_CX60, 0x0AF, 1) \
    X(PI_ROTOR_EXT_I,  0,     M.adv.pi[7],          0, 32767, VF_CX60, 0x0B0, 1) \
    X(PI_COOL_SUP_K,   0,     M.adv.pi[8],          0, 32767, VF_CX60, 0x0BB, 1) \
    X(PI_COOL_SUP_I,   0,     M.adv.pi[9],          0, 32767, VF_CX60, 0x0BC, 1) \
    X(PI_COOL_EXT_K,   0,     M.adv.pi[10],         0, 32767, VF_CX60, 0x0B3, 1) \
    X(PI_COOL_EXT_I,   0,     M.adv.pi[11],         0, 32767, VF_CX60, 0x0B4, 1) \
    X(COOL_MIN_OUTDOOR, 0,    M.adv.cool_min_outdoor, 50, 250, VF_CX60, 0x013, 1) \
    X(COOL_MIN_SPEED,  0,     M.adv.cool_min_speed, 2, 3,    VF_CX60, 0x015, 1) \
    X(COOL_DELAY,      0,     M.adv.cool_delay,     0, 300,  VF_CX60, 0x014, 1) \
    X(COOL_RECOVERY,   0,     M.adv.cool_recovery,  0, 1,    VF_CX60 | VF_LAYOUT, 0x139, 1) \
    X(RECOVERY_DIFF,   0,     M.adv.recovery_diff,  0, 50,   VF_CX60, 0x092, 1) \
    X(ROTOR_ALARM,     0,     M.adv.rotor_alarm,    0, 1,    VF_CX60, 0x14B, 1) \
    X(DEICE,           0,     M.adv.deice,          0, 1,    VF_CX60 | VF_LAYOUT, 0x158, 1) \
    X(DEICE_TEMP,      0,     M.adv.deice_temp,  -450, -50,  VF_CX60, 0x159, 1) \
    X(DEICE_ROTOR,     0,     M.adv.deice_rotor,   15, 180,  VF_CX60, 0x15B, 1) \
    X(LESS_AIR_TEMP,   0,     M.adv.less_air_temp, -450, -50, VF_CX60, 0x15A, 1) \
    X(DEICE_ROTOR_PERIOD, 0,  M.adv.deice_period[0], 1, 60,  VF_CX60, 0x162, 1) \
    X(DEICE_ROTOR_FREQ, 0,    M.adv.deice_freq[0],  1, 10,   VF_CX60, 0x163, 1) \
    X(DEICE_FAN_PERIOD, 0,    M.adv.deice_period[1], 1, 60,  VF_CX60, 0x164, 1) \
    X(DEICE_FAN_FREQ,  0,     M.adv.deice_freq[1],  1, 10,   VF_CX60, 0x165, 1) \
    X(DEICE_FAN,       0,     M.adv.deice_fan,     20, 100,  VF_CX60, 0x166, 1) \
    X(DEICE_RAMP,      0,     M.adv.deice_ramp,     1, 5,    VF_CX60, 0x167, 1) \
    X(CAL_SUPPLY,      0,     M.adv.cal[0],       -50, 50,   VF_CX60, 0x08C, 1) \
    X(CAL_EXTRACT,     0,     M.adv.cal[1],       -50, 50,   VF_CX60, 0x08D, 1) \
    X(CAL_OUTDOOR,     0,     M.adv.cal[2],       -50, 50,   VF_CX60, 0x08E, 1) \
    X(CAL_WATER,       0,     M.adv.cal[3],       -50, 50,   VF_CX60, 0x08F, 1) \
    X(EXTRACT_SENSOR,  0,     M.adv.extract_sensor, 0, 1,    VF_CX60, 0x136, 1) \
    X(OUTDOOR_SENSOR,  0,     M.adv.outdoor_sensor, 0, 1,    VF_CX60, 0x137, 1) \
    X(FIRE_MODE,       0,     M.adv.fire_mode,      1, 4,    VF_CX60, 0x00B, 1) \
    X(FILTER_GUARD,    0,     M.adv.filter_guard,   0, 1,    VF_CX60, 0x113, 1) \
    X(EXT_SETPOINT,    0,     M.adv.ext_setpoint,   0, 1,    VF_CX60, 0x134, 1) \
    X(AWAY_SPEED,      0,     M.adv.away_speed,     1, 3,    VF_CX60, 0x0A6, 1) \
    X(AWAY_SETPOINT,   0,     M.adv.away_setpoint, 100, 400, VF_CX60, 0x0A7, 1) \
    X(AWAY_HOLD,       0,     M.adv.away_hold,      0, 120,  VF_CX60, 0x0A5, 1) \
    X(FIREPLACE_SUP,   0,     M.adv.fireplace_sup,  1, 3,    VF_CX60, 0x005, 1) /* AC fans */ \
    X(FIREPLACE_EXT,   0,     M.adv.fireplace_ext,  1, 3,    VF_CX60, 0x00A, 1) \
    X(START_DELAY1,    0,     M.adv.start_delay[0], 0, 60,   VF_CX60, 0x017, 1) \
    X(START_DELAY2,    0,     M.adv.start_delay[1], 0, 60,   VF_CX60, 0x018, 1) \
    X(START_DELAY3,    0,     M.adv.start_delay[2], 0, 60,   VF_CX60, 0x019, 1) \
    X(START_DELAY4,    0,     M.adv.start_delay[3], 0, 60,   VF_CX60, 0x01A, 1) \
    X(RUN_ON,          0,     M.adv.run_on,         0, 300,  VF_CX60, 0x01B, 1) /* electric heater */ \
    /* manual tests, written with their enable by unit.c. The CX60 ends them after a minute */ \
    X(TEST_SPEED,      0,     M.test_speed,         0, MODE_COUNT, 0, 0x0A8, 1) /* AC fans, MODE_* + 1 */ \
    X(TEST_FANS,       0,     M.test_fans,          0, 100,  0, 0x0A9, 1) /* EC fans */ \
    X(TEST_HEATER,     0,     M.test_heater,        0, 1,    0, 0x145, 1) \
    X(TEST_ROTOR,      0,     M.test_rotor,         0, 1,    0, 0x147, 1) \
    X(TEST_COOLING,    0,     M.test_cooling,       0, 100,  0, 0x0AA, 1) \
    /* clock, not on the link or in flash since the RTC keeps it. So no key */ \
    X(HOUR,            0,     M.hh,                 0, 23,   VF_CLOCK, 0, 1) \
    X(MINUTE,          0,     M.mi,                 0, 59,   VF_CLOCK, 0, 1) \
    X(DAY,             0,     M.dd,                 1, 31,   VF_CLOCK, 0, 1) \
    X(MONTH,           0,     M.mo,                 1, 12,   VF_CLOCK, 0, 1) \
    X(YEAR,            0,     M.yy,              2024, 2099, VF_CLOCK, 0, 1) \
    /* week program period being edited (M.period), written to the CX60 as a whole. Times in minutes after \
       midnight. VF_LAYOUT since moving a period can reorder the day */ \
    X(PERIOD_START,    0,     M.period.start,       0, 1435, VF_LAYOUT, 0, 1) \
    X(PERIOD_END,      0,     M.period.end,         5, 1440, VF_LAYOUT, 0, 1) \
    X(PERIOD_LEVEL,    0,     M.period.level,       0, MODE_MAX, 0, 0, 1) \
    X(PERIOD_SETPOINT, 0,     M.period.setpoint,  100, 300,  0, 0, 1)
#define VALUES(X) PLINK_VALUES(X) PANEL_VALUES(X)
// clang-format on

// V_NONE, one id per VALUES() row, one per equipment feature, then one per ESP item slot
#define V_ENUM(id, ...) V_##id,
typedef enum {
  V_NONE,
  VALUES(V_ENUM) V_FEAT_FIRST,
  V_FEAT_LAST = V_FEAT_FIRST + FEAT_COUNT - 1,  // equipment on/off, in FEAT_* order
  V_ITEM_FIRST,
  V_ITEM_LAST = V_ITEM_FIRST + N_ITEMS - 1,  // ESP item values, in M.items[] order
  V_COUNT
} val_t;
#undef V_ENUM

// Code relies on these ids being consecutive
_Static_assert(V_TILE4 == V_TILE1 + 3, "tiles");
_Static_assert(V_PERIOD_SETPOINT - V_PERIOD_START == 3, "the period's values");

// One VALUES() row, see plink_values.h for the columns
typedef struct {
  int32_t min, max;
  uint16_t key, flags, cx60;
  uint8_t scale;
} val_def_t;

val_def_t val_def(val_t v);       // all zero for V_NONE
uint16_t val_cx60_reg(val_t v);   // CX60 register for v, offset by our bus address if VF_PER_PANEL
val_t val_by_key(uint16_t key);   // V_NONE if no value has that key
val_t val_by_cx60(uint16_t reg);  // value a CX60 register holds, not command registers. V_NONE if none
int32_t val_get(val_t v);
void val_set(val_t v, int32_t value);         // set by the user
void val_set_remote(val_t v, int32_t value);  // set by the ESP
void val_set_bus(val_t v, int32_t value);     // read from the CX60
void val_load(val_t v, int32_t value);        // from flash at startup, only stores it
void do_action(int a);                        // the user ran action a (action_t)
void do_action_remote(int a);                 // the ESP asks to run action a

// ESP items exist once the ESP defines them. Cleared on its HELLO, then it sends them all again.
bool val_known(val_t v);  // false only for item slots the ESP hasn't defined
void val_forget_esp(void);
const item_t *item_of(val_t v);    // NULL if v isn't an item the ESP has defined
const item_t *item_by_id(int id);  // NULL if no item has that id

// Change notifications besides plat.h's
void ui_value_changed(val_t v);
void ui_layout_changed(void);      // rows or tiles came or went, rebuild the screen
void ui_alarms_changed(void);      // the alarms or their history
void link_value_changed(val_t v);  // tells the ESP
