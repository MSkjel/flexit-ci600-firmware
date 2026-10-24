// V_* ids for every value. The ones shared with the ESP come from proto/plink_values.h, which explains the
// columns. Panel-only ones are below, then the equipment bits and ESP items.
#pragma once

// clang-format off: a table, laid out by hand
#define PANEL_VALUES(X) \
    /* panel-only, not sent to the ESP. The key is only for flash */ \
    X(NIGHT_THEME,     0x060, M.night_theme,        0, N_THEMES, VF_SAVED | VF_LAYOUT, 0, 1) /* 0 = same as day */ \
    X(DASH_TEMP,       0x061, M.dash_temp,          0, DT_COUNT - 1, VF_SAVED | VF_LAYOUT, 0, 1) /* DT_* */ \
    X(SAVER_TEXT,      0x062, M.saver_text,         0, SAVER_TEXT_COUNT - 1, VF_SAVED | VF_LAYOUT, 0, 1) \
    X(SAVER_TOP,       0x063, M.saver_top,          0, 1,    VF_SAVED | VF_LAYOUT, 0, 1) \
    X(GO_HOME,         0x064, M.go_home,            0, 1,    VF_SAVED, 0, 1) \
    X(SHOW_FLOW,       0x065, M.view_on[VIEW_FLOW], 0, 1,    VF_SAVED | VF_LAYOUT, 0, 1) \
    X(SHOW_TREND,      0x066, M.view_on[VIEW_TREND], 0, 1,   VF_SAVED | VF_LAYOUT, 0, 1) \
    X(SHOW_SENSORS,    0x067, M.view_on[VIEW_SENSORS], 0, 1, VF_SAVED | VF_LAYOUT, 0, 1) \
    X(SHOW_COMFORT,    0x068, M.view_on[VIEW_COMFORT], 0, 1, VF_SAVED | VF_LAYOUT, 0, 1) \
    X(SHOW_SMART,      0x069, M.view_on[VIEW_SMART], 0, 1,   VF_SAVED | VF_LAYOUT, 0, 1) \
    X(TEMP_DEC,        0x06A, M.temp_dec,           0, 1,    VF_SAVED, 0, 1) \
    X(LED_OK,          0x06B, M.led_ok,             0, LED_OK_COUNT - 1, VF_SAVED, 0, 1) \
    X(LED_ALARM,       0x06C, M.led_alarm,          0, 1,    VF_SAVED, 0, 1) \
    X(LED_FILTER,      0x06D, M.led_filter,         0, 1,    VF_SAVED, 0, 1) \
    X(AUTO_LOCK,       0x06E, M.auto_lock,          0, 1,    VF_SAVED, 0, 1) \
    X(ALARM_POPUPS,    0x06F, M.alarm_popups,       0, POPUPS_COUNT - 1, VF_SAVED, 0, 1) \
    X(AUTO_DST,        0x070, M.auto_dst,           0, 1,    VF_SAVED, 0, 1) \
    X(DST_NOW,         0x071, M.dst_now,            0, 1,    VF_SAVED | VF_RO, 0, 1) /* kept across restarts */ \
    X(FAST_DISPLAY,    0x072, M.fast_display,       0, 1,    VF_SAVED, 0, 1) \
    /* clock, not on the link or in flash since the RTC keeps it. So no key */ \
    X(HOUR,            0,     M.hh,                 0, 23,   VF_CLOCK, 0, 1) \
    X(MINUTE,          0,     M.mi,                 0, 59,   VF_CLOCK, 0, 1) \
    X(DAY,             0,     M.dd,                 1, 31,   VF_CLOCK, 0, 1) \
    X(MONTH,           0,     M.mo,                 1, 12,   VF_CLOCK, 0, 1) \
    X(YEAR,            0,     M.yy,              2024, 2099, VF_CLOCK, 0, 1) \
    /* week program period being edited (M.period), written to the CU60 as a whole. Times in minutes after \
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
_Static_assert(V_FAN_MAX_EXT - V_FAN_MIN_SUP == 5, "fan percentages");
_Static_assert(V_PERIOD_SETPOINT - V_PERIOD_START == 3, "the period's values");

// One VALUES() row, see plink_values.h for the columns
typedef struct {
  int32_t min, max;
  uint16_t key, flags, cu60;
  uint8_t scale;
} val_def_t;

val_def_t val_def(val_t v);       // all zero for V_NONE
uint16_t val_cu60_reg(val_t v);   // CU60 register for v, offset by our bus address if VF_PER_PANEL
val_t val_by_key(uint16_t key);   // V_NONE if no value has that key
val_t val_by_cu60(uint16_t reg);  // value a CU60 register holds, not command registers. V_NONE if none
int32_t val_get(val_t v);
void val_set(val_t v, int32_t value);         // set by the user
void val_set_remote(val_t v, int32_t value);  // set by the ESP
void val_set_bus(val_t v, int32_t value);     // read from the CU60
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
