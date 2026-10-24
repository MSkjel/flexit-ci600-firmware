// Main menu tiles, the row lists and the value editor / action confirm box.
#include "ui.h"
#include <stdio.h>
#include <string.h>

// Menu row types
typedef enum {
  MI_SUB,     // opens a submenu
  MI_NUM,     // number, changed in the editor
  MI_ENUM,    // one of a list of options, picked in the editor
  MI_TOGGLE,  // on/off, OK flips it
  MI_ACTION,  // runs an action after a confirmation
  MI_INFO,    // read-only value
  MI_SCREEN,  // opens another screen
  MI_HEAD,    // section header, can't be selected
  MI_PRESS    // ESP button, OK presses it
} mi_t;

// mitem_t flags
enum {
  MF_TIME = 1,         // minutes since midnight, shown as a time
  MF_ZERO_OFF = 2,     // 0 is shown as Off
  MF_BACKLIGHT = 4,    // a brightness, shown on the backlight while it's being edited
  MF_DANGER = 8,       // can't be undone, so OK has to be held
  MF_POP_PARENT = 16,  // the action removes what the list is about, so close the list too
  MF_HOUR = 32         // an hour of the day, shown as a time
};

typedef struct menu menu_t;
// One menu row. Which fields are used depends on the type.
typedef struct {
  uint8_t type;             // MI_*
  uint8_t flags;            // MF_*
  uint8_t act;              // MI_ACTION: A_*
  uint8_t arg;              // MI_SCREEN: arg for the screen
  uint8_t need;             // FBIT(FEAT_x) mask, hidden unless all are installed
  uint8_t without;          // FBIT(FEAT_x) mask, hidden if any is installed
  uint8_t dec;              // MI_NUM: decimals
  uint8_t n_opts;           // MI_ENUM: length of opts
  uint16_t val;             // val_t it shows or changes, the range comes from values.h
  uint16_t step;            // MI_NUM: step size, 0 means 1
  str_t title, help, unit;  // ESP item rows take the title and unit from the item, the help from the ESP
  str_t done;               // MI_ACTION: toast shown after it, S_DONE if unset
  const char *icon;
  const str_t *opts;                // MI_ENUM: option names, see OPTS
  const char *(*opt_text)(int o);   // MI_ENUM: or a function giving each option's text
  const void *target;               // MI_SUB: menu_t, MI_SCREEN: screen_t
  void (*fmt)(char *buf);           // MI_INFO text, or a main menu tile's status line
  int (*used)(val_t val, int opt);  // MI_ENUM: option actually used for opt, a fallback when there's no ESP
  int (*options)(uint16_t *opt);    // MI_ENUM: options when they aren't 0..max, up to MAX_OPTS. Returns the count
  bool (*shown)(void);              // hidden while this is false, for rows that depend on another row
} mitem_t;
_Static_assert(FEAT_COUNT <= 8, "the features fit need and without");

// A menu has either fixed items or a row() function that makes them.
struct menu {
  str_t title;
  const char *title_text;            // overrides title, for titles built at run time
  const mitem_t *items;              // fixed rows, NULL when row() generates them
  bool (*row)(int i, mitem_t *out);  // writes row i (0..n-1) to out, false if it's hidden right now
  uint8_t n;                         // number of rows
  void (*prepare)(void);             // called each time the menu opens, e.g. to build title_text
};

// Row initialisers take icon, title, what the row shows or does, help, then any other fields by name.
// NUM, ENUM and TOG edit value v, ACT runs action a, SUB opens menu m, SCR opens screen s, INFO shows what f writes.
// OPTS(o) takes an ENUM's option names from the array o.
#define ROW(ty, ic, t, h) .type = ty, .icon = ic, .title = t, .help = h
#define NUM(ic, t, v, h, ...) {ROW(MI_NUM, ic, t, h), .val = v, __VA_ARGS__}
#define ENUM(ic, t, v, h, ...) {ROW(MI_ENUM, ic, t, h), .val = v, __VA_ARGS__}
#define OPTS(o) .opts = o, .n_opts = LEN(o)
#define TOG(ic, t, v, h, ...) {ROW(MI_TOGGLE, ic, t, h), .val = v, __VA_ARGS__}
#define ACT(ic, t, a, h, ...) {ROW(MI_ACTION, ic, t, h), .act = a, __VA_ARGS__}
#define SUB(ic, t, m, h, ...) {ROW(MI_SUB, ic, t, h), .target = &m, __VA_ARGS__}
#define SCR(ic, t, s, h, ...) {ROW(MI_SCREEN, ic, t, h), .target = &s, __VA_ARGS__}
#define INFO(ic, t, f, h, ...) {ROW(MI_INFO, ic, t, h), .fmt = f, __VA_ARGS__}
#define HEAD(t) {.type = MI_HEAD, .title = t}
// MENU(name, title, rows...) defines a static menu_t called name
#define MENU(name, t, ...) \
  static const mitem_t name##_items[] = {__VA_ARGS__}; \
  static const menu_t name = {.title = t, .items = name##_items, .n = LEN(name##_items)}

// ---------------------------------------------------------------- info row texts
static void f_temp(char *b, int16_t t) { fmt_temp(b, t, true); }
static void i_outdoor(char *b) { f_temp(b, M.t_outdoor); }
static void i_supply(char *b) { f_temp(b, M.t_supply); }
static void i_extract(char *b) { f_temp(b, M.t_extract); }
static void i_water(char *b) { f_temp(b, M.t_water); }
static void i_room(char *b) { f_temp(b, M.t_room); }
static void i_filter_left(char *b) {
  int days = filter_days_left();
  if (!days)
    strcpy(b, _(S_CHANGE_NOW));
  else
    sprintf(b, _(S_DAYS_LEFT), days);
}
static void i_filter_hours(char *b) { sprintf(b, _(S_FILTER_HOURS), M.filter_hours); }
static void i_picture(char *b) {
  if (pic_state() == PIC_RECEIVING)
    sprintf(b, _(S_RECEIVING_PCT), pic_percent());
  else if (pic_state() == PIC_READY)
    sprintf(b, _(S_PICTURE_SIZE), pic_width(), pic_height());
  else
    strcpy(b, _(S_NONE_YET));
}
static void hours(char *b, uint32_t h) {
  if (h >= 1000)
    sprintf(b, _(S_HOURS_THOUSANDS), (unsigned long) h / 1000, (unsigned long) h % 1000);
  else
    sprintf(b, _(S_HOURS), (unsigned long) h);
}
static void i_h_total(char *b) { hours(b, M.h_total); }
static void i_h_stop(char *b) { hours(b, M.h_stop); }
static void i_h_min(char *b) { hours(b, M.h_min); }
static void i_h_normal(char *b) { hours(b, M.h_normal); }
static void i_h_max(char *b) { hours(b, M.h_max); }
static void i_h_rotor(char *b) { hours(b, M.h_rotor); }
static void i_h_heater(char *b) { hours(b, M.h_heater); }

// ---------------------------------------------------------------- menus
static const str_t O_REG[REG_COUNT] = {[REG_EXTRACT] = S_EXTRACT_AIR, [REG_SUPPLY] = S_SUPPLY_AIR};
// same order as idle_seconds()
static const str_t O_IDLE_AFTER[IDLE_STEPS] = {S_IDLE_15S,  S_IDLE_30S,   S_IDLE_1MIN,  S_IDLE_2MIN,
                                               S_IDLE_5MIN, S_IDLE_10MIN, S_IDLE_30MIN, S_NEVER};
static const str_t O_NIGHT_THEME[N_THEMES + 1] = {S_SAME_AS_BY_DAY, S_MIDNIGHT, S_DAYLIGHT, S_GRAPHITE};
static const str_t O_BIG[DT_COUNT] = {
    [DT_INDOOR] = S_INDOOR, [DT_OUTDOOR] = S_OUTDOOR, [DT_SUPPLY] = S_SUPPLY_AIR, [DT_EXTRACT] = S_EXTRACT_AIR};
static const str_t O_SAVER_TEXT[SAVER_TEXT_COUNT] = {[SAVER_TEXT_BAND] = S_SHADED_BAND,
                                                     [SAVER_TEXT_SHADOW] = S_SHADOW,
                                                     [SAVER_TEXT_PLAIN] = S_JUST_TEXT,
                                                     [SAVER_TEXT_SMALL] = S_SMALL};
static const str_t O_SAVER_POS[2] = {[false] = S_BOTTOM, [true] = S_TOP};
static const str_t O_DECIMALS[2] = {[false] = S_WHOLE_DEGREES, [true] = S_ONE_DECIMAL};
static const str_t O_LED_OK[LED_OK_COUNT] = {
    [LED_OK_ALWAYS] = S_WHEN_ALL_IS_WELL, [LED_OK_NOT_NIGHT] = S_NOT_AT_NIGHT, [LED_OK_NEVER] = S_NEVER};
static const str_t O_POPUPS[POPUPS_COUNT] = {
    [POPUPS_ALL] = S_ALL_ALARMS, [POPUPS_NOT_FILTER] = S_NOT_THE_FILTER, [POPUPS_OFF] = S_OFF};
static const str_t O_SAVER[SAVER_COUNT] = {[SAVER_OFF] = S_OFF,
                                           [SAVER_CLOCK] = S_CLOCK,
                                           [SAVER_CLOCK_TEMPS] = S_CLOCK_TEMPS,
                                           [SAVER_PIC] = S_PICTURE,
                                           [SAVER_PIC_CLOCK] = S_PICTURE_CLOCK,
                                           [SAVER_PIC_ALL] = S_PICTURE_ALL};
static const str_t O_VIEW[VIEW_COUNT] = {[VIEW_DASH] = S_DASHBOARD,  [VIEW_FLOW] = S_AIRFLOW,
                                         [VIEW_TREND] = S_TRENDS,    [VIEW_SENSORS] = S_ESP_SENSORS,
                                         [VIEW_COMFORT] = S_COMFORT, [VIEW_SMART] = S_SMART_HOME};
static const str_t O_INDOOR[INDOOR_COUNT] = {[INDOOR_EXTRACT] = S_EXTRACT_AIR, [INDOOR_PANEL] = S_THIS_PANEL};

#define FANS(t, v, h) NUM(IC_FAN, t, v, h, .unit = S_UNIT_PCT, .need = FBIT(FEAT_EC_FANS))
MENU(m_vent, S_VENTILATION, ENUM(IC_FAN, S_FAN_SPEED, V_MODE, S_FAN_SPEED_HELP, OPTS(MODE_NAMES)),
     TOG(IC_TIMER, S_BOOST_NOW, V_BOOST, S_BOOST_NOW_HELP),
     NUM(IC_TIMER_SAND, S_BOOST_TIME, V_BOOST_MINUTES, S_BOOST_TIME_HELP, .step = 5, .unit = S_UNIT_MIN),
     HEAD(S_FAN_SPEEDS), FANS(S_MIN_SUPPLY, V_FAN_MIN_SUP, S_MIN_SUPPLY_HELP),
     FANS(S_MIN_EXTRACT, V_FAN_MIN_EXT, S_MIN_EXTRACT_HELP),
     FANS(S_NORMAL_SUPPLY, V_FAN_NORM_SUP, S_NORMAL_SUPPLY_HELP),
     FANS(S_NORMAL_EXTRACT, V_FAN_NORM_EXT, S_NORMAL_EXTRACT_HELP),
     FANS(S_MAX_SUPPLY, V_FAN_MAX_SUP, S_MAX_SUPPLY_HELP), FANS(S_MAX_EXTRACT, V_FAN_MAX_EXT, S_MAX_EXTRACT_HELP));

#define TEMP(ic, t, v, h, ...) NUM(ic, t, v, h, .step = 5, .dec = 1, .unit = S_UNIT_C, __VA_ARGS__)
MENU(m_temp, S_TEMPERATURE, TEMP(IC_THERMOSTAT, S_SETPOINT, V_SETPOINT, S_SETPOINT_HELP),
     ENUM(IC_TUNE, S_REGULATE_ON, V_REGULATION, S_REGULATE_ON_HELP, OPTS(O_REG)),
     TEMP(IC_THERMO_LO, S_MIN_SUPPLY_AIR, V_SUPPLY_MIN, S_MIN_SUPPLY_AIR_HELP),
     TEMP(IC_THERMO_HI, S_MAX_SUPPLY_AIR, V_SUPPLY_MAX, S_MAX_SUPPLY_AIR_HELP),
     TOG(IC_FIRE, S_HEATER, V_HEATER, S_HEATER_HELP, .without = FBIT(FEAT_WATER_HEATER)),  // the unit keeps it on
     TOG(IC_SNOW, S_COOLING, V_COOLING, S_COOLING_HELP, .need = FBIT(FEAT_COOLING)), HEAD(S_SENSORS),
     INFO(IC_THERMO, S_OUTDOOR, i_outdoor, S_OUTDOOR_HELP), INFO(IC_ARROW_R, S_SUPPLY, i_supply, S_SUPPLY_HELP),
     INFO(IC_HOME_THERMO, S_EXTRACT, i_extract, S_EXTRACT_HELP),
     INFO(IC_WATER, S_RETURN_WATER, i_water, S_RETURN_WATER_HELP, .need = FBIT(FEAT_WATER_HEATER)),
     INFO(IC_THERMO, S_PANEL_SENSOR, i_room, S_PANEL_SENSOR_HELP),
     ENUM(IC_HOME_THERMO, S_INDOOR_TEMPERATURE, V_INDOOR_SOURCE, S_INDOOR_TEMPERATURE_HELP, OPTS(O_INDOOR)),
     NUM(IC_THERMO, S_PANEL_SENSOR_OFFSET, V_ROOM_OFFSET, S_PANEL_SENSOR_OFFSET_HELP, .dec = 1, .unit = S_UNIT_C));

#define DAY(t, d, h) SCR(IC_CALENDAR, t, SCR_DAY, h, .arg = d)
MENU(m_sched, S_SCHEDULE, TOG(IC_CAL_CLOCK, S_WEEK_PROGRAM, V_WEEK_PROGRAM, S_WEEK_PROGRAM_HELP),
     SCR(IC_CALENDAR, S_WEEK_OVERVIEW, SCR_SCHEDULE, S_WEEK_OVERVIEW_HELP), HEAD(S_DAYS),
     DAY(S_MONDAY, 0, S_MONDAY_HELP), DAY(S_TUESDAY, 1, S_TUESDAY_HELP), DAY(S_WEDNESDAY, 2, S_WEDNESDAY_HELP),
     DAY(S_THURSDAY, 3, S_THURSDAY_HELP), DAY(S_FRIDAY, 4, S_FRIDAY_HELP), DAY(S_SATURDAY, 5, S_SATURDAY_HELP),
     DAY(S_SUNDAY, 6, S_SUNDAY_HELP));

MENU(m_filter, S_FILTER, INFO(IC_FILTER, S_STATUS, i_filter_left, S_STATUS_HELP),
     INFO(IC_COUNTER, S_RUNNING_SINCE_CHANGE, i_filter_hours, S_RUNNING_SINCE_CHANGE_HELP),
     NUM(IC_CALENDAR, S_CHANGE_EVERY, V_FILTER_INTERVAL, S_CHANGE_EVERY_HELP, .step = 30, .unit = S_UNIT_DAYS),
     ACT(IC_RESTORE, S_FILTERS_CHANGED, A_RESET_FILTER, S_FILTERS_CHANGED_HELP));

void open_filter_menu(void) { ui_push(&SCR_LIST, (intptr_t) &m_filter); }

MENU(m_alarms, S_ALARMS, SCR(IC_BELL_RING, S_ACTIVE_ALARMS, SCR_ALARMS, S_ACTIVE_ALARMS_HELP),
     ENUM(IC_BELL, S_POP_UPS, V_ALARM_POPUPS, S_POP_UPS_HELP, OPTS(O_POPUPS)),
     SCR(IC_HISTORY, S_HISTORY, SCR_ALARMS, S_HISTORY_HELP, .arg = ALARMS_HISTORY),
     ACT(IC_CHECK_C, S_RESET_ALARMS, A_RESET_ALARMS, S_RESET_ALARMS_HELP),
     ACT(IC_DELETE, S_CLEAR_HISTORY, A_CLEAR_HISTORY, S_CLEAR_HISTORY_HELP, .flags = MF_DANGER));

MENU(m_stats, S_STATISTICS, INFO(IC_COUNTER, S_TOTAL, i_h_total, S_TOTAL_HOURS_HELP),
     INFO(IC_FAN_OFF, S_STOPPED, i_h_stop, S_STOPPED_HOURS_HELP), INFO(IC_FAN, S_MIN, i_h_min, S_MIN_HOURS_HELP),
     INFO(IC_FAN, S_NORMAL, i_h_normal, S_NORMAL_HOURS_HELP), INFO(IC_FAN, S_MAX, i_h_max, S_MAX_HOURS_HELP),
     INFO(IC_ROTOR, S_ROTOR, i_h_rotor, S_ROTOR_HOURS_HELP), INFO(IC_FIRE, S_HEATER, i_h_heater, S_HEATER_HOURS_HELP));

#define LIGHT(ic, t, v, h, fl) NUM(ic, t, v, h, .step = 5, .unit = S_UNIT_PCT, .flags = MF_BACKLIGHT | (fl))
MENU(m_night, S_NIGHT_MODE, TOG(IC_NIGHT, S_NIGHT_MODE, V_NIGHT, S_NIGHT_MODE_HELP),
     NUM(IC_CLOCK, S_FROM, V_NIGHT_FROM, S_NIGHT_FROM_HELP, .flags = MF_HOUR),
     NUM(IC_CLOCK, S_TO, V_NIGHT_TO, S_NIGHT_TO_HELP, .flags = MF_HOUR),
     LIGHT(IC_BRIGHTNESS, S_BRIGHTNESS, V_NIGHT_BRIGHTNESS, S_NIGHT_BRIGHTNESS_HELP, 0),
     LIGHT(IC_BRIGHTNESS, S_IDLE_BRIGHTNESS, V_NIGHT_IDLE_BRIGHTNESS, S_NIGHT_IDLE_BRIGHTNESS_HELP, MF_ZERO_OFF),
     ENUM(IC_PALETTE, S_THEME_AT_NIGHT, V_NIGHT_THEME, S_THEME_AT_NIGHT_HELP, OPTS(O_NIGHT_THEME)));

// The four dashboard tiles show one of the unit's values, or any ESP item except buttons
static const char *tile_opt(int o) {
  const char *name = tile_name(o);
  return name ? name : "\xE2\x80\x93";  // en dash for an item that's gone
}

static int tile_options(uint16_t *opt) {
  int n = 0;
  for (int t = 0; t < TILE_UNIT; t++)
    opt[n++] = (uint16_t) t;
  for (int i = 0; i < N_ITEMS; i++)
    if (M.items[i].type && M.items[i].type != ITEM_BUTTON && HAS(FEAT_ESP))
      opt[n++] = (uint16_t) (TILE_ITEM + M.items[i].id);
  return n;
}
#define TILE_ROW(t, v) \
  ENUM(IC_MONITOR, t, v, S_TILE_HELP, .opt_text = tile_opt, .used = tile_used, .options = tile_options)
MENU(m_tiles, S_DASHBOARD_TILES, TILE_ROW(S_TILE_1, V_TILE1), TILE_ROW(S_TILE_2, V_TILE2), TILE_ROW(S_TILE_3, V_TILE3),
     TILE_ROW(S_TILE_4, V_TILE4));

MENU(m_views, S_VIEWS, TOG(IC_FAN, S_AIRFLOW, V_SHOW_FLOW, S_AIRFLOW_HELP),
     TOG(IC_CHART, S_TRENDS, V_SHOW_TREND, S_TRENDS_HELP),
     TOG(IC_CHART, S_ESP_SENSORS, V_SHOW_SENSORS, S_SENSORS_HELP, .need = FBIT(FEAT_ESP)),
     TOG(IC_LEAF, S_COMFORT, V_SHOW_COMFORT, S_COMFORT_HELP, .need = FBIT(FEAT_ESP)),
     TOG(IC_HASS, S_SMART_HOME, V_SHOW_SMART, S_SMART_HOME_HELP, .need = FBIT(FEAT_ESP)));

MENU(m_home, S_HOME_SCREEN,
     ENUM(IC_HOME, S_START_VIEW, V_HOME_VIEW, S_START_VIEW_HELP, OPTS(O_VIEW), .used = view_used),
     SUB(IC_EYE, S_VIEWS, m_views, S_VIEWS_HELP),
     ENUM(IC_THERMO, S_BIG_TEMPERATURE, V_DASH_TEMP, S_BIG_TEMPERATURE_HELP, OPTS(O_BIG)),
     SUB(IC_MONITOR, S_DASHBOARD_TILES, m_tiles, S_DASHBOARD_TILES_HELP));

MENU(m_lights, S_LIGHTS, ENUM(IC_CHECK_C, S_OK_LIGHT, V_LED_OK, S_OK_LIGHT_HELP, OPTS(O_LED_OK)),
     TOG(IC_ALERT, S_ALARM_LIGHT, V_LED_ALARM, S_ALARM_LIGHT_HELP),
     TOG(IC_FILTER, S_FILTER_LIGHT, V_LED_FILTER, S_FILTER_LIGHT_HELP));

MENU(m_display, S_DISPLAY, ENUM(IC_PALETTE, S_THEME, V_THEME, S_THEME_HELP, OPTS(THEME_NAMES)),
     LIGHT(IC_BRIGHTNESS, S_BRIGHTNESS, V_BRIGHTNESS, S_BRIGHTNESS_HELP, 0),
     SUB(IC_NIGHT, S_NIGHT_MODE, m_night, S_NIGHT_MODE_HELP), SUB(IC_HOME, S_HOME_SCREEN, m_home, S_HOME_SCREEN_HELP),
     SUB(IC_BULB, S_LIGHTS, m_lights, S_LIGHTS_HELP),
     ENUM(IC_LIST, S_LANGUAGE, V_LANGUAGE, S_LANGUAGE_HELP, .opt_text = lang_name),
     TOG(IC_CLOCK, S_N_24_HOUR_CLOCK, V_CLOCK_24H, S_N_24_HOUR_CLOCK_HELP),
     ENUM(IC_THERMO, S_TEMPERATURES, V_TEMP_DEC, S_TEMPERATURES_HELP, OPTS(O_DECIMALS)),
     TOG(IC_HELP, S_KEY_HINTS, V_KEY_HINTS, S_KEY_HINTS_HELP),
     TOG(IC_MONITOR, S_FAST_DISPLAY, V_FAST_DISPLAY, S_FAST_DISPLAY_HELP));

// The text rows only matter when there's text on a picture
static bool text_on_picture(void) { return M.screensaver == SAVER_PIC_CLOCK || M.screensaver == SAVER_PIC_ALL; }

MENU(m_saver, S_SCREENSAVER, ENUM(IC_SLEEP, S_IDLE_AFTER, V_IDLE_AFTER, S_IDLE_AFTER_HELP, OPTS(O_IDLE_AFTER)),
     LIGHT(IC_NIGHT, S_IDLE_BRIGHTNESS, V_IDLE_BRIGHTNESS, S_IDLE_BRIGHTNESS_HELP, MF_ZERO_OFF),
     ENUM(IC_MONITOR, S_SHOW, V_SCREENSAVER, S_SCREENSAVER_HELP, OPTS(O_SAVER), .used = saver_used),
     ENUM(IC_LIST, S_TEXT_ON_PICTURE, V_SAVER_TEXT, S_TEXT_ON_PICTURE_HELP, OPTS(O_SAVER_TEXT), .need = FBIT(FEAT_ESP),
          .shown = text_on_picture),
     ENUM(IC_UP, S_TEXT_POSITION, V_SAVER_TOP, S_TEXT_POSITION_HELP, OPTS(O_SAVER_POS), .need = FBIT(FEAT_ESP),
          .shown = text_on_picture),
     INFO(IC_PALETTE, S_PICTURE, i_picture, S_PICTURE_HELP, .need = FBIT(FEAT_ESP)),
     TOG(IC_HOME_CLOCK, S_BACK_TO_START_VIEW, V_GO_HOME, S_BACK_TO_START_VIEW_HELP));

MENU(m_time, S_DATE_AND_TIME, NUM(IC_CLOCK, S_HOUR, V_HOUR, S_HOUR_HELP),
     NUM(IC_CLOCK, S_MINUTE, V_MINUTE, S_MINUTE_HELP), NUM(IC_CALENDAR, S_DAY, V_DAY, S_DAY_HELP),
     NUM(IC_CALENDAR, S_MONTH, V_MONTH, S_MONTH_HELP), NUM(IC_CALENDAR, S_YEAR, V_YEAR, S_YEAR_HELP),
     TOG(IC_SUN, S_SUMMER_TIME, V_AUTO_DST, S_SUMMER_TIME_HELP, .without = FBIT(FEAT_ESP)));

// one toggle per FEATURES() entry
#define FEAT_ITEM(id, ic, t, h) TOG(ic, S_##t, V_FEAT_FIRST + FEAT_##id, S_##h),
MENU(m_equipment, S_EQUIPMENT, FEATURES(FEAT_ITEM));
#undef FEAT_ITEM

MENU(m_system, S_SYSTEM, SCR(IC_INFO, S_ABOUT, SCR_INFO, S_ABOUT_HELP),
     SUB(IC_TOOLS, S_EQUIPMENT, m_equipment, S_EQUIPMENT_HELP),
     SCR(IC_LAN, S_BUS, SCR_INFO, S_BUS_HELP, .arg = INFO_BUS),
     SCR(IC_COUNTER, S_DIAGNOSTICS, SCR_INFO, S_DIAGNOSTICS_HELP, .arg = INFO_DIAG),
     NUM(IC_LAN, S_PANEL_ADDRESS, V_BUS_ADDR, S_PANEL_ADDRESS_HELP),
     TOG(IC_LOCK, S_KEY_LOCK, V_KEY_LOCK, S_KEY_LOCK_HELP),
     TOG(IC_LOCK, S_LOCK_WHEN_IDLE, V_AUTO_LOCK, S_LOCK_WHEN_IDLE_HELP),
     ACT(IC_REFRESH, S_RESTART_PANEL, A_RESTART, S_RESTART_PANEL_HELP),
     ACT(IC_RESTORE, S_RESET_PANEL_SETTINGS, A_RESET_PANEL, S_RESET_PANEL_SETTINGS_HELP, .flags = MF_DANGER),
     ACT(IC_RESTORE, S_FACTORY_RESET_UNIT, A_FACTORY_RESET, S_FACTORY_RESET_UNIT_HELP, .flags = MF_DANGER));

// Rows from the ESP's items, for the Comfort or Smart home menu
static bool item_row(int i, mitem_t *out, bool comfort) {
  const item_t *s = &M.items[i];
  bool ro = s->flags & ITEMF_RO;  // a read-only toggle or number is shown like a sensor
  *out = (mitem_t) {.type = s->type == ITEM_TOGGLE && !ro   ? MI_TOGGLE
                            : s->type == ITEM_NUMBER && !ro ? MI_NUM
                            : s->type == ITEM_BUTTON        ? MI_PRESS
                                                            : MI_INFO,
                    .icon = icon_glyph(s->icon),
                    .help = S_ITEM_HELP,
                    .val = (uint16_t) (V_ITEM_FIRST + i),
                    .step = s->step,
                    .dec = s->dec};
  return s->type && (s->flags & ITEMF_MENU) && !(s->flags & ITEMF_COMFORT) == !comfort;
}
static bool comfort_row(int i, mitem_t *out) { return item_row(i, out, true); }
static bool smart_row(int i, mitem_t *out) { return item_row(i, out, false); }
static const menu_t m_comfort = {.title = S_COMFORT, .row = comfort_row, .n = N_ITEMS};
static const menu_t m_smart = {.title = S_SMART_HOME, .row = smart_row, .n = N_ITEMS};

// ---------------------------------------------------------------- main menu
// Comfort and Smart home tile status. The first two sensor values, else the item count.
static void s_page(char *b, bool comfort) {
  int n = 0, shown = 0;
  b[0] = 0;
  for (int i = 0; i < N_ITEMS; i++) {
    const item_t *it = &M.items[i];
    mitem_t m;
    if (!item_row(i, &m, comfort))
      continue;  // only what the menu lists
    n++;
    if (it->type == ITEM_SENSOR && shown < 2) {
      char v[24];
      strcat(strcat(b, shown++ ? " \xC2\xB7 " : ""), item_text(v, it, true));
    }
  }
  if (!M.esp_online)
    strcpy(b, _(S_ESP_OFFLINE));
  else if (!n)
    strcpy(b, _(S_NOTHING_YET));
  else if (!shown)
    sprintf(b, n == 1 ? _(S_N_ITEM) : _(S_N_ITEMS), n);
}
static void s_comfort(char *b) { s_page(b, true); }
static void s_smart(char *b) { s_page(b, false); }

static void s_vent(char *b) {
  if (M.boost_left)
    sprintf(b, _(S_BOOST_MIN), M.boost_left);
  else
    sprintf(b, "%s, %d%%", tr(MODE_NAMES[M.mode_now]), M.fan_sup);
}
static void s_temp(char *b) {
  char a[16];
  sprintf(b, _(S_TARGET), fmt_target(a, M.setpoint));
}
static void s_sched(char *b) { strcpy(b, M.week_program ? _(S_PROGRAM_ON) : _(S_OFF)); }
static void s_alarms(char *b) {
  if (M.n_alarms)
    sprintf(b, _(S_N_ACTIVE), M.n_alarms);
  else
    strcpy(b, _(S_ALL_CLEAR));
}
static void s_stats(char *b) {
  char h[16];
  hours(h, M.h_total);
  sprintf(b, _(S_HOURS_TOTAL), h);
}
static void s_display(char *b) { sprintf(b, "%s, %d%%", tr(THEME_NAMES[M.theme % N_THEMES]), M.brightness); }
static void s_saver(char *b) { strcpy(b, tr(O_SAVER[saver_used(V_SCREENSAVER, M.screensaver)])); }
static void s_time(char *b) {
  char t[12];
  sprintf(b, "%02d.%02d.%04d %s", M.dd, M.mo, M.yy, fmt_hm(t, M.hh * 60 + M.mi));
}
static void s_system(char *b) {
  if (M.bus_offline)
    strcpy(b, _(S_NO_CONTACT));
  else if (M.cx60_version)
    sprintf(b, _(S_ADDRESS_CX60), M.bus_addr, M.cx60_version >> 8, M.cx60_version & 0xFF);
  else
    sprintf(b, _(S_ADDRESS), M.bus_addr);
}

// Main menu tiles. Each opens a submenu, .fmt writes the status line under the title.
MENU(m_main, S_MENU, SUB(IC_FAN, S_VENTILATION, m_vent, S_VENTILATION_HELP, .fmt = s_vent),
     SUB(IC_THERMOSTAT, S_TEMPERATURE, m_temp, S_TEMPERATURE_HELP, .fmt = s_temp),
     SUB(IC_LEAF, S_COMFORT, m_comfort, S_COMFORT_MENU_HELP, .fmt = s_comfort, .need = FBIT(FEAT_ESP)),
     SUB(IC_HASS, S_SMART_HOME, m_smart, S_SMART_HOME_HELP, .fmt = s_smart, .need = FBIT(FEAT_ESP)),
     SUB(IC_CAL_CLOCK, S_SCHEDULE, m_sched, S_SCHEDULE_HELP, .fmt = s_sched),
     SUB(IC_FILTER, S_FILTER, m_filter, S_FILTER_HELP, .fmt = i_filter_left),
     SUB(IC_BELL, S_ALARMS, m_alarms, S_ALARMS_HELP, .fmt = s_alarms),
     SUB(IC_CHART, S_STATISTICS, m_stats, S_STATISTICS_HELP, .fmt = s_stats),
     SUB(IC_PALETTE, S_DISPLAY, m_display, S_DISPLAY_HELP, .fmt = s_display),
     SUB(IC_MONITOR, S_SCREENSAVER, m_saver, S_SCREENSAVER_MENU_HELP, .fmt = s_saver),
     SUB(IC_CLOCK, S_DATE_AND_TIME, m_time, S_DATE_AND_TIME_HELP, .fmt = s_time),
     SUB(IC_CHIP, S_SYSTEM, m_system, S_SYSTEM_HELP, .fmt = s_system));

#define TILE_W 149
#define TILE_H 44
#define TILE_GAP 6
#define TILE_ROWS 4  // rows of tiles on screen

#define N_TILES LEN(m_main_items)
#define TILE_SLOTS (TILE_ROWS * 2)  // tile widgets, reused for other tiles when scrolling

// Main menu state
typedef struct {
  lv_obj_t *box[TILE_SLOTS], *icon[TILE_SLOTS], *title[TILE_SLOTS], *status[TILE_SLOTS];
  int8_t shown[TILE_SLOTS];      // tile index shown in each slot, -1 = none
  const mitem_t *tile[N_TILES];  // tiles available with the installed features, in order
  int n;                         // number of tiles
} grid_t;
SCREEN_STATE(grid_t, grid);

static void menu_build(lv_obj_t *b, intptr_t arg) {
  (void) arg;
  memset(grid, 0, sizeof *grid);
  ui_hints(S_MOVE, S_OPEN, S_BACK, S_HELP);
  for (unsigned i = 0; i < m_main.n; i++)
    if (ui_has(m_main.items[i].need))
      grid->tile[grid->n++] = &m_main.items[i];
  frame_t *f = ui_frame();  // tiles may have gone since
  int rows = (grid->n + 1) / 2, last_top = rows > TILE_ROWS ? rows - TILE_ROWS : 0;
  if (f->sel >= grid->n)
    f->sel = grid->n - 1;
  if (f->top > last_top)
    f->top = (int16_t) last_top;
  for (int k = 0; k < TILE_SLOTS && k < grid->n; k++) {
    lv_obj_t *t =
        mk_box(b, 8 + (k % 2) * (TILE_W + TILE_GAP), 4 + (k / 2) * (TILE_H + TILE_GAP), TILE_W, TILE_H, T.surface, 10);
    lv_obj_set_style_border_width(t, 2, 0);  // always 2 so the content doesn't move when selected
    grid->box[k] = t;
    grid->icon[k] = mk_disc(t, 5, 5, 30, T.surface2, &icons_16, T.text, "");
    grid->title[k] = mk_label(t, &font_f14, T.text, "");
    lv_obj_set_pos(grid->title[k], 44, 2);
    grid->status[k] = mk_label(t, &font_f12, T.dim, "");
    lv_obj_set_size(grid->status[k], TILE_W - 50, 17);
    label_dots(grid->status[k]);
    lv_obj_set_pos(grid->status[k], 44, 21);
    grid->shown[k] = -1;
  }
}

static void menu_update(void) {
  frame_t *f = ui_frame();
  keep_in_view(&f->top, f->sel / 2, TILE_ROWS);
  char buf[48];
  for (int k = 0; k < TILE_SLOTS && k < grid->n; k++) {
    int i = f->top * 2 + k;
    if (i >= grid->n) {
      lv_obj_add_flag(grid->box[k], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(grid->box[k], LV_OBJ_FLAG_HIDDEN);
    const mitem_t *it = grid->tile[i];
    if (grid->shown[k] != i) {
      grid->shown[k] = (int8_t) i;
      label_text(grid->icon[k], it->icon);
      label_text(grid->title[k], tr(it->title));
    }
    bool sel = i == f->sel;
    lv_obj_set_style_bg_color(grid->box[k], sel ? T.accent_soft : T.surface, 0);
    lv_obj_set_style_border_color(grid->box[k], sel ? T.accent : T.surface, 0);
    lv_obj_set_style_bg_color(grid->icon[k], sel ? T.accent : T.surface2, 0);
    lv_obj_set_style_text_color(grid->icon[k], sel ? T.on_accent : T.text, 0);
    it->fmt(buf);
    lv_label_set_text(grid->status[k], buf);
    lv_obj_set_style_text_color(grid->status[k], it->fmt == s_alarms && M.n_alarms ? T.err : T.dim, 0);
  }
  ui_help(grid->tile[f->sel]->help);
}

static bool menu_key(int k) {
  frame_t *f = ui_frame();
  if (k == KEY_UP || k == KEY_DOWN) {
    f->sel = step_sel(f->sel, k, grid->n);
    menu_update();
    return true;
  }
  if (k == KEY_OK) {
    ui_push(&SCR_LIST, (intptr_t) grid->tile[f->sel]->target);
    return true;
  }
  return false;
}

static const char *menu_selected(void) { return tr(grid->tile[ui_frame()->sel]->title); }

const screen_t SCR_MENU = {
    .title = S_MENU, .build = menu_build, .update = menu_update, .key = menu_key, .selected = menu_selected};

// Settings for one week program period (M.period), see sched_edit()
static char period_title[24];

// Title like "Monday, period 2", numbered by where the period is in the day now
static void period_name(void) {
  int n = 0;
  for (int i = 0; i < M.n_periods[M.period.day]; i++) {
    n++;
    if (M.week[M.period.day][i].idx == M.period.idx)
      break;
  }
  snprintf(period_title, sizeof period_title, _(S_DAY_PERIOD), tr(DAY_NAMES[M.period.day % 7]), n);
}

static const mitem_t period_items[] = {
    NUM(IC_CLOCK, S_START, V_PERIOD_START, S_PERIOD_START_HELP, .step = 15, .flags = MF_TIME),
    NUM(IC_CLOCK, S_END, V_PERIOD_END, S_PERIOD_END_HELP, .step = 15, .flags = MF_TIME),
    ENUM(IC_FAN, S_FAN_SPEED, V_PERIOD_LEVEL, S_PERIOD_FAN_SPEED_HELP, OPTS(MODE_NAMES)),
    TEMP(IC_THERMOSTAT, S_SETPOINT, V_PERIOD_SETPOINT, S_PERIOD_SETPOINT_HELP),
    ACT(IC_DELETE, S_REMOVE_PERIOD, A_REMOVE_PERIOD, S_REMOVE_PERIOD_HELP, .flags = MF_POP_PARENT,
        .done = S_PERIOD_REMOVED),
};
static const menu_t m_period = {
    .title_text = period_title, .items = period_items, .n = LEN(period_items), .prepare = period_name};

const menu_t *period_menu(void) { return &m_period; }

// ---------------------------------------------------------------- row lists (SCR_LIST)
#define ROW_H 34
#define ROWS 5  // rows on screen, their widgets are reused as the list scrolls
#define MAX_ROWS 24

static const char *item_title(const mitem_t *it) {
  const item_t *s = item_of(it->val);
  return s ? s->label : tr(it->title);
}

static const char *item_unit(const mitem_t *it) {  // "" for none
  const item_t *s = item_of(it->val);
  return s ? s->unit : tr(it->unit);
}

static const char *opt_text(const mitem_t *it, int o) { return it->opt_text ? it->opt_text(o) : tr(it->opts[o]); }

// SCR_LIST state. Only ROWS row widgets exist, slot k shows list row shown[k].
typedef struct {
  const menu_t *m;
  lv_obj_t *sel_bg, *help, *scroll;  // selection highlight, help line, scroll bar thumb
  lv_obj_t *slot[ROWS], *val[ROWS];  // row containers, and each one's switch or value label, NULL if none
  int8_t shown[ROWS];                // list row in each slot, -1 = none
  uint8_t src[MAX_ROWS];             // menu row index of each list row, hidden rows left out
  int n;                             // number of list rows
} list_t;
SCREEN_STATE(list_t, list);

// Row r of the list. Generated rows are built in buf.
static const mitem_t *row(int r, mitem_t *buf) {
  if (!list->m->row)
    return &list->m->items[list->src[r]];
  list->m->row(list->src[r], buf);
  return buf;
}

static int row_type(int r) {
  mitem_t b;
  return row(r, &b)->type;
}

static bool shown_off(const mitem_t *it, int32_t v) { return (it->flags & MF_ZERO_OFF) && v == 0; }

// The number without its unit, formatted as shown, or Off
static const char *number_only(const mitem_t *it, int32_t v, char *buf) {
  if (shown_off(it, v))
    return strcpy(buf, _(S_OFF));
  if (it->flags & (MF_TIME | MF_HOUR))
    return fmt_hm(buf, it->flags & MF_HOUR ? v * 60 : v);
  return fmt_dec(buf, v, it->dec);
}

// The number with its unit, or Off
static void number_text(const mitem_t *it, int32_t v, char *buf) {
  if (v == ITEM_UNKNOWN && item_of(it->val)) {
    strcpy(buf, "\xE2\x80\x93");  // en dash, an item with no value
    return;
  }
  number_only(it, v, buf);
  if (!shown_off(it, v))
    add_unit(buf, item_unit(it));
}

// Option actually used for o. A fallback when o needs the ESP and there isn't one
static int opt_used(const mitem_t *it, int o) { return it->used ? it->used((val_t) it->val, o) : o; }

static void value_text(const mitem_t *it, char *buf) {
  int32_t v = val_get(it->val);
  switch (it->type) {
    case MI_NUM:
      number_text(it, v, buf);
      break;
    case MI_ENUM:
      v = opt_used(it, v);
      strcpy(buf, opt_text(it, v));
      break;
    case MI_INFO:
      if (it->fmt)
        it->fmt(buf);
      else if (item_of(it->val))
        item_text(buf, item_of(it->val), true);  // an ESP sensor, or a read-only toggle as On or Off
      else
        number_text(it, v, buf);
      break;
    default:
      buf[0] = 0;
  }
}

// An ESP toggle with no value yet is off
static bool is_on(const mitem_t *it) {
  const item_t *s = item_of(it->val);
  return s ? item_on(s) : val_get(it->val) != 0;
}

static void toggle_draw(lv_obj_t *sw, bool on) {
  lv_obj_set_style_bg_color(sw, on ? T.accent : T.surface2, 0);
  lv_obj_t *knob = lv_obj_get_child(sw, 0);
  lv_obj_set_x(knob, on ? 18 : 2);
  lv_obj_set_style_bg_color(knob, on ? T.on_accent : T.dim, 0);
}

static bool selectable(int r) { return row_type(r) != MI_HEAD; }

static bool read_only(const mitem_t *it) { return val_def((val_t) it->val).flags & VF_RO; }

// Picks the rows the installed equipment and the ESP support, and drops empty headers
static void list_rows(void) {
  const menu_t *m = list->m;
  for (int i = 0; i < m->n && list->n < MAX_ROWS; i++) {
    mitem_t b;
    const mitem_t *it = m->row ? (m->row(i, &b) ? &b : NULL) : &m->items[i];
    if (!it || !ui_has(it->need) || (M.features & it->without) || !val_known(it->val) || (it->shown && !it->shown()))
      continue;
    if (list->n && row_type(list->n - 1) == MI_HEAD && it->type == MI_HEAD)
      list->n--;
    list->src[list->n++] = (uint8_t) i;
  }
  if (list->n && row_type(list->n - 1) == MI_HEAD)
    list->n--;
}

static int slots(void) { return list->n < ROWS ? list->n : ROWS; }

// Section header, its name and a line to the right edge
static void head_row(lv_obj_t *s, const mitem_t *it) {
  const char *title = tr(it->title);
  lv_obj_set_pos(mk_label(s, &font_f12, T.accent, title), 16, 14);
  int x = 16 + lv_txt_get_width(title, strlen(title), &font_f12, 0, 0) + 8;
  mk_box(s, x, 22, SCR_W - 16 - x, 1, T.line, 0);
}

// Draws row r in slot k. The switch or value label goes in list->val[k] for list_update to fill in.
static void list_row(int k, int r) {
  lv_obj_t *s = list->slot[k];
  lv_obj_clean(s);
  list->val[k] = NULL;
  list->shown[k] = (int8_t) r;
  if (r >= list->n)
    return;
  mitem_t b;
  const mitem_t *it = row(r, &b);
  int x = 16;
  if (it->type == MI_HEAD) {
    head_row(s, it);
    return;
  }
  if (it->icon) {
    lv_obj_set_pos(mk_icon(s, &icons_16, T.dim, it->icon), 16, 8);
    x = 42;
  }
  lv_obj_set_pos(mk_label(s, &font_f16, T.text, item_title(it)), x, 6);
  if (it->type == MI_TOGGLE) {
    lv_obj_t *sw = list->val[k] = mk_box(s, SCR_W - 54, 8, 36, 18, T.surface2, 9);
    mk_box(sw, 2, 2, 14, 14, T.dim, 7);
  } else if (it->type == MI_SUB || it->type == MI_SCREEN || it->type == MI_ACTION || it->type == MI_PRESS) {
    const char *ic = it->type == MI_PRESS ? IC_PLAY : it->type == MI_ACTION ? IC_DOTS : IC_RIGHT;
    lv_obj_set_pos(mk_icon(s, &icons_16, T.faint, ic), SCR_W - 34, 8);
  } else {
    lv_obj_t *v = list->val[k] = mk_label(s, &font_f14, it->type == MI_INFO ? T.dim : T.accent, "");
    lv_obj_set_size(v, 140, 19);
    label_dots(v);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(v, SCR_W - 160, 8);
  }
}

// Puts row top + k in slot k. Rows already drawn just move, the rest get drawn.
static void place_rows(int top) {
  for (int k = 0; k < slots(); k++)
    for (int j = 0; j < slots(); j++)
      if (j != k && list->shown[j] == top + k) {
        lv_obj_t *s = list->slot[j], *v = list->val[j];
        int8_t r = list->shown[j];
        list->slot[j] = list->slot[k];
        list->val[j] = list->val[k];
        list->shown[j] = list->shown[k];
        list->slot[k] = s;
        list->val[k] = v;
        list->shown[k] = r;
      }
  for (int k = 0; k < slots(); k++) {
    lv_obj_set_y(list->slot[k], k * ROW_H);
    if (list->shown[k] != top + k)
      list_row(k, top + k);
  }
}

static void list_build(lv_obj_t *b, intptr_t arg) {
  memset(list, 0, sizeof *list);
  list->m = (const menu_t *) arg;
  if (list->m->prepare)
    list->m->prepare();
  ui_set_title(list->m->title_text ? list->m->title_text : tr(list->m->title));
  if (list->m->row && !M.esp_online) {  // an ESP menu, stale while the ESP is away
    mk_empty(b, T.faint, IC_WIFI_OFF, _(S_ESP_NOT_CONNECTED),
             list->m == &m_comfort ? _(S_COMFORT_NEEDS_ESP) : _(S_SMART_NEEDS_ESP));
    ui_hints(S_NONE, S_NONE, S_BACK, S_HELP);
    return;
  }
  list_rows();
  if (!list->n) {  // an ESP menu with no items yet
    mk_empty(b, T.faint, IC_HASS, _(S_NOTHING_HERE_YET), _(S_SMART_MENU_EMPTY));
    ui_hints(S_NONE, S_NONE, S_BACK, S_HELP);
    return;
  }
  frame_t *f = ui_frame();  // the list may have got shorter since
  int last_top = list->n > ROWS ? list->n - ROWS : 0;
  if (f->sel >= list->n)
    f->sel = (int16_t) (list->n - 1);
  if (f->top > last_top)
    f->top = (int16_t) last_top;
  while (f->sel < list->n - 1 && !selectable(f->sel))
    f->sel++;

  list->sel_bg = mk_box(b, 6, 0, SCR_W - 12, ROW_H - 2, T.surface, 8);  // created first so it's behind the rows
  mk_box(list->sel_bg, 0, 7, 3, ROW_H - 16, T.accent, 2);
  for (int k = 0; k < slots(); k++) {
    lv_obj_t *s = list->slot[k] = lv_obj_create(b);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, SCR_W, ROW_H);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    list->shown[k] = -1;
  }
  if (list->n > ROWS) {
    lv_obj_t *track = mk_box(b, SCR_W - 4, 4, 2, ROWS * ROW_H - 8, T.surface2, 1);
    list->scroll = mk_box(track, 0, 0, 2, (ROWS * ROW_H - 8) * ROWS / list->n, T.dim, 1);
  }
  mk_box(b, 0, ROWS * ROW_H, SCR_W, 1, T.line, 0);
  list->help = mk_label(b, &font_f12, T.dim, "");  // help for the selected row, truncated
  lv_obj_set_size(list->help, SCR_W - 24, 17);
  label_dots(list->help);
  lv_obj_set_pos(list->help, 12, ROWS * ROW_H + 6);
}

static void list_update(void) {
  frame_t *f = ui_frame();
  if (!list->n)
    return;
  keep_in_view(&f->top, f->sel, ROWS);
  if (f->top > 0 && f->sel - 1 < f->top && row_type(f->sel - 1) == MI_HEAD)
    f->top--;  // keep its header in view
  place_rows(f->top);
  lv_obj_set_y(list->sel_bg, (f->sel - f->top) * ROW_H + 1);
  if (list->scroll) {
    int track = ROWS * ROW_H - 8, len = lv_obj_get_height(list->scroll);
    lv_obj_set_y(list->scroll, (track - len) * f->top / (list->n - ROWS));
  }
  char buf[48];
  mitem_t b;
  for (int k = 0; k < slots(); k++) {
    if (!list->val[k])
      continue;
    const mitem_t *it = row(list->shown[k], &b);
    if (it->type == MI_TOGGLE)
      toggle_draw(list->val[k], is_on(it));
    else {
      value_text(it, buf);
      lv_label_set_text(list->val[k], buf);
    }
  }
  const mitem_t *it = row(f->sel, &b);
  if (it->help == S_ITEM_HELP) {  // ESP item help, requested when the row is selected
    const char *h = link_item_help(it->val - V_ITEM_FIRST);
    lv_label_set_text(list->help, h ? h : "");
    ui_help_item(it->val - V_ITEM_FIRST);
  } else {
    lv_label_set_text(list->help, tr(it->help));
    ui_help(it->help);
  }
  switch (it->type) {
    case MI_NUM:
    case MI_ENUM:
      ui_hints(S_MOVE, S_CHANGE, S_BACK, S_HELP);
      break;
    case MI_TOGGLE:
      ui_hints(S_MOVE, read_only(it) ? S_NONE : S_SWITCH, S_BACK, S_HELP);
      break;
    case MI_INFO:
      ui_hints(S_MOVE, S_NONE, S_BACK, S_HELP);
      break;
    case MI_ACTION:
      ui_hints(S_MOVE, S_RUN, S_BACK, S_HELP);
      break;
    case MI_PRESS:
      ui_hints(S_MOVE, read_only(it) ? S_NONE : S_PRESS, S_BACK, S_HELP);
      break;
    default:
      ui_hints(S_MOVE, S_OPEN, S_BACK, S_HELP);
  }
}

static mitem_t edit_row;  // copy of a generated row being edited

static bool list_key(int k) {
  frame_t *f = ui_frame();
  if (!list->n)
    return false;
  mitem_t b;
  const mitem_t *it = row(f->sel, &b);
  if (k == KEY_UP || k == KEY_DOWN) {
    do
      f->sel = step_sel(f->sel, k, list->n);
    while (!selectable(f->sel));
    list_update();
    return true;
  }
  if (k != KEY_OK)
    return false;
  switch (it->type) {
    case MI_SUB:
      ui_push(&SCR_LIST, (intptr_t) it->target);
      break;
    case MI_SCREEN:
      ui_push((const screen_t *) it->target, it->arg);
      break;
    case MI_TOGGLE:
      if (read_only(it))
        break;
      val_set(it->val, !is_on(it));
      ui_toast(val_get(it->val) ? IC_TOGGLE_ON : IC_TOGGLE_OFF, val_get(it->val) ? S_ON : S_OFF);
      break;
    case MI_NUM:
    case MI_ENUM:
    case MI_ACTION:
      if (it == &b) {  // SCR_EDIT keeps the pointer and may rebuild from it, so copy generated rows
        edit_row = b;
        it = &edit_row;
      }
      ui_push(&SCR_EDIT, (intptr_t) it);
      break;
    case MI_PRESS:
      if (read_only(it))
        break;
      val_set(it->val, 1);
      ui_toast(IC_CHECK, S_SENT);
      break;
    default:
      break;
  }
  return true;
}

static const char *list_selected(void) {
  mitem_t b;
  return list->n ? item_title(row(ui_frame()->sel, &b)) : NULL;
}

const screen_t SCR_LIST = {.build = list_build, .update = list_update, .key = list_key, .selected = list_selected};

// ---------------------------------------------------------------- value editor / confirmation
#define OPT_ROWS 5  // enum options shown at a time, longer lists scroll
#define MAX_OPTS 32
_Static_assert(TILE_UNIT + N_ITEMS <= MAX_OPTS, "tile_options fits");
// SCR_EDIT state. It's an overlay so it can't use ui_screen_mem.
static struct {
  const frame_t *f;  // the editor's frame
  const mitem_t *it;
  int32_t v, min, max;  // value being edited and its range
  lv_obj_t *val, *unit, *bar, *opts[OPT_ROWS], *check, *opt_bg;
  uint16_t offer[MAX_OPTS];  // MI_ENUM: the n usable options, ed.v is offer[pos]
  int n, pos;
  int16_t first;  // first option on screen
  bool armed;     // OK was pressed in this dialog, not just still held from the list
} ed;

#define EDIT_W 272  // width of the editor card, and of the text inside it
#define EDIT_TEXT_W 244

// Fills ed.offer with the usable options and finds ed.v among them. A new editor scrolls it to the middle.
static void offer_options(const mitem_t *it, bool rebuild) {
  int now = opt_used(it, ed.v);
  uint16_t opts[MAX_OPTS];
  int n = 0;
  if (it->options)
    n = it->options(opts);
  else
    for (int o = ed.min; o <= ed.max && n < MAX_OPTS; o++)
      opts[n++] = (uint16_t) o;
  ed.n = ed.pos = 0;
  for (int i = 0; i < n; i++)
    if (opt_used(it, opts[i]) == opts[i]) {
      if (opts[i] == now)
        ed.pos = ed.n;
      ed.offer[ed.n++] = opts[i];
    }
  ed.v = ed.offer[ed.pos];
  int rows = ed.n < OPT_ROWS ? ed.n : OPT_ROWS, first = rebuild ? ed.first : ed.pos - rows / 2;
  ed.first = (int16_t) (first < 0 ? 0 : first > ed.n - rows ? ed.n - rows : first);
}

static void edit_number(lv_obj_t *card, const mitem_t *it) {
  ed.val = mk_label(card, &font_n40, T.accent, "");
  ed.unit = mk_label(card, &font_f16, T.dim, "");
  ed.bar = mk_bar(card, 18, 104, EDIT_TEXT_W - 8, 6, 0, ed.max - ed.min, T.accent);
  char a[16];
  lv_obj_set_pos(mk_label(card, &font_f12, T.faint, number_only(it, ed.min, a)), 18, 116);
  lv_obj_align(mk_label(card, &font_f12, T.faint, number_only(it, ed.max, a)), LV_ALIGN_TOP_RIGHT, -18, 116);
  ui_hints(S_ADJUST, S_SAVE, S_CANCEL, S_HELP);
}

static void edit_options(lv_obj_t *card, int rows) {
  ed.opt_bg = mk_box(card, 8, 0, EDIT_W - 16, 22, T.accent_soft, 6);
  for (int i = 0; i < rows; i++) {
    ed.opts[i] = mk_label(card, &font_f14, T.text, "");
    lv_obj_set_pos(ed.opts[i], 38, 40 + i * 24);
  }
  ed.check = mk_icon(card, &icons_16, T.accent, IC_CHECK);  // marks the current setting
  ui_hints(S_CHOOSE, S_SAVE, S_CANCEL, S_HELP);
}

// Action confirm box. The help text says what it does, then how to confirm.
static void edit_confirm(lv_obj_t *card, const mitem_t *it, const char *warning, int warning_y) {
  lv_obj_t *q = mk_label(card, &font_f14, T.text, tr(it->help));
  lv_obj_set_width(q, EDIT_TEXT_W);
  lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(q, 14, 40);
  lv_obj_t *w = mk_label(card, &font_f12, T.warn, warning);
  lv_obj_set_width(w, EDIT_TEXT_W);
  lv_label_set_long_mode(w, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(w, 14, warning_y);
  ui_hints(S_NONE, it->flags & MF_DANGER ? S_HOLD_TO_CONFIRM : S_CONFIRM, S_CANCEL, S_NONE);
}

static void edit_build(lv_obj_t *b, intptr_t arg) {
  const mitem_t *it = (const mitem_t *) arg;
  const frame_t *f = ui_frame();
  bool rebuild = f->built && ed.f == f;  // keeps the value being edited
  if (!rebuild) {
    memset(&ed, 0, sizeof ed);
    ed.f = f;
    ed.v = it->type == MI_ACTION ? 0 : val_get(it->val);
  }
  ed.it = it;
  if (rebuild && (!val_known(it->val) || (item_of(it->val) && !M.esp_online)))
    ui_close_after(0);  // the ESP went away, or restarted and forgot the item being edited
  ed.min = val_def(it->val).min;
  ed.max = val_def(it->val).max;
  if (ed.v == ITEM_UNKNOWN && item_of(it->val))
    ed.v = ed.min;  // a number with no value yet starts from the bottom
  if (it->type == MI_ENUM)
    offer_options(it, rebuild);
  int rows = ed.n < OPT_ROWS ? ed.n : OPT_ROWS, h = it->type == MI_ENUM ? 46 + rows * 24 : 150, warning_y = 0;
  const char *warning = tr(it->flags & MF_DANGER ? S_HOLD_OK_WARNING : S_PRESS_OK);
  if (it->type == MI_ACTION) {  // fit the question with the warning below it
    lv_point_t qs, ws;
    lv_txt_get_size(&qs, tr(it->help), &font_f14, 0, 0, EDIT_TEXT_W, LV_TEXT_FLAG_NONE);
    lv_txt_get_size(&ws, warning, &font_f12, 0, 0, EDIT_TEXT_W, LV_TEXT_FLAG_NONE);
    warning_y = 40 + qs.y + 10;
    h = warning_y + ws.y + 14;
  }
  if (h > BODY_H - 8)
    h = BODY_H - 8;
  lv_obj_t *card = mk_card(b, (SCR_W - EDIT_W) / 2, (BODY_H - h) / 2, EDIT_W, h, T.line, 1);
  lv_obj_set_pos(mk_icon(card, &icons_16, T.accent, it->icon ? it->icon : IC_TUNE), 14, 12);
  lv_obj_set_pos(mk_label(card, &font_f16, T.text, item_title(it)), 38, 9);
  if (it->help == S_ITEM_HELP)
    ui_help_item(it->val - V_ITEM_FIRST);
  else
    ui_help(it->help);
  if (it->type == MI_NUM)
    edit_number(card, it);
  else if (it->type == MI_ENUM)
    edit_options(card, rows);
  else
    edit_confirm(card, it, warning, warning_y);
}

static void edit_update(void) {
  const mitem_t *it = ed.it;
  char a[16];
  if (it->type == MI_NUM) {
    bool off = shown_off(it, ed.v);  // Off in a text font, the number font has no letters
    const lv_font_t *f = off ? &font_f28 : &font_n40;
    const char *unit = off ? "" : item_unit(it);
    lv_obj_set_style_text_font(ed.val, f, 0);
    lv_label_set_text(ed.val, number_only(it, ed.v, a));
    label_text(ed.unit, unit);
    int y = 38 + (font_n40.line_height - font_n40.base_line) - (f->line_height - f->base_line);  // same baseline
    lv_obj_align(ed.val, LV_ALIGN_TOP_MID, -lv_txt_get_width(unit, strlen(unit), &font_f16, 0, 0) / 2 - 2, y);
    lv_obj_align_to(ed.unit, ed.val, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -3 - font_n40.base_line);  // near its baseline
    lv_bar_set_value(ed.bar, ed.v - ed.min, LV_ANIM_OFF);
  } else if (it->type == MI_ENUM) {
    int rows = ed.n < OPT_ROWS ? ed.n : OPT_ROWS, used = opt_used(it, val_get(it->val)), now = -1;
    for (int i = 0; i < ed.n; i++)
      if (ed.offer[i] == used)
        now = i;
    keep_in_view(&ed.first, ed.pos, rows);
    for (int i = 0; i < rows; i++) {
      int o = ed.offer[ed.first + i];
      lv_label_set_text(ed.opts[i], opt_text(it, o));
      lv_obj_set_style_text_color(ed.opts[i], o == ed.v ? T.accent : T.text, 0);
    }
    lv_obj_set_y(ed.opt_bg, 39 + (ed.pos - ed.first) * 24);
    if (now >= ed.first && now < ed.first + rows) {
      lv_obj_clear_flag(ed.check, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(ed.check, 14, 42 + (now - ed.first) * 24);
    } else {
      lv_obj_add_flag(ed.check, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static bool edit_key(int k) {
  const mitem_t *it = ed.it;
  if ((k == KEY_UP || k == KEY_DOWN) && it->type != MI_ACTION) {
    int d = k == KEY_UP ? 1 : -1;
    if (it->type == MI_ENUM) {
      ed.pos = step_sel(ed.pos, k, ed.n);  // UP moves up the list, unlike a number where it increases
      ed.v = ed.offer[ed.pos];
    } else {
      int step = it->step ? it->step : 1, mul = input_hold_ms > 4000 ? 10 : input_hold_ms > 1500 ? 5 : 1;
      int32_t base = item_of(it->val) ? ed.min : 0;  // an ESP number's steps count from its min
      ed.v = base + step_value(ed.v - base, d * mul, step, ed.min - base, ed.max - base);
    }
    edit_update();
    return true;
  }
  bool danger = it->flags & MF_DANGER;
  if (k == KEY_OK && danger) {
    ed.armed = true;
    ui_toast(IC_TIMER_SAND, S_KEEP_HOLDING_OK);
    return true;
  }
  if (k == KEY_OK_LONG && !(danger && ed.armed))
    return true;
  if (k == KEY_OK || k == KEY_OK_LONG) {
    if (it->type == MI_ACTION) {
      do_action(it->act);
      ui_pop();
      if (it->flags & MF_POP_PARENT)
        ui_pop();
      ui_toast(IC_CHECK, it->done ? it->done : S_DONE);
    } else {
      ui_pop();
      val_set(it->val, ed.v);
      ui_toast(IC_CHECK, S_SAVED);
    }
    return true;
  }
  if (k == KEY_BACK) {
    ui_pop();
    return true;
  }
  return false;
}

static const char *edit_selected(void) { return ed.it->type == MI_ENUM ? opt_text(ed.it, ed.v) : NULL; }

const screen_t SCR_EDIT = {
    .flags = SF_OVERLAY, .build = edit_build, .update = edit_update, .key = edit_key, .selected = edit_selected};

// The brightness being edited, so the backlight can show it
int ui_backlight_preview(void) {
  if (ui_top() != &SCR_EDIT || !ed.it || !(ed.it->flags & MF_BACKLIGHT))
    return -1;
  return (int) ed.v;  // at night 0 means off, but it previews as 0 %, not with the light off
}

// For make check: each enum row has one named option per value
static int check(const menu_t *m, char *why, size_t n) {
  int bad = 0;
  for (int i = 0; m->items && i < m->n; i++) {
    const mitem_t *it = &m->items[i];
    if (it->type == MI_SUB && it->target != m)
      bad += check(it->target, why, n);
    if (it->type != MI_ENUM || it->opt_text)
      continue;
    val_def_t d = val_def(it->val);
    int unnamed = 0;
    while (unnamed < it->n_opts && it->opts[unnamed] != S_NONE)
      unnamed++;
    if (it->n_opts != d.max + 1 && !bad++)
      snprintf(why, n, "%s: %d options for %ld values", tr(it->title), it->n_opts, (long) d.max + 1);
    else if (unnamed < it->n_opts && !bad++)
      snprintf(why, n, "%s: option %d has no name", tr(it->title), unnamed);
  }
  return bad;
}

int menu_check(char *why, size_t n) { return check(&m_main, why, n) + check(&m_period, why, n); }
