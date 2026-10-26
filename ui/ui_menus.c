// The menus, their info row and status line texts, and menu_check() for make check.
#include "ui_menu.h"

// ---------------------------------------------------------------- info row texts
static void info_outdoor(char *b, size_t n) { fmt_temp(b, n, M.t_outdoor, true); }
static void info_supply(char *b, size_t n) { fmt_temp(b, n, M.t_supply, true); }
static void info_extract(char *b, size_t n) { fmt_temp(b, n, M.t_extract, true); }
static void info_water(char *b, size_t n) { fmt_temp(b, n, M.t_water, true); }
static void info_room(char *b, size_t n) { fmt_temp(b, n, M.t_room, true); }
static void info_filter_left(char *b, size_t n) {
  int days = filter_days_left();
  if (!days)
    copy_text(b, n, _(S_CHANGE_NOW));
  else
    sprint(b, n, _(S_DAYS_LEFT), days);
}
static void info_filter_hours(char *b, size_t n) { sprint(b, n, _(S_FILTER_HOURS), M.filter_hours); }
static void info_picture(char *b, size_t n) {
  if (pic_state() == PIC_RECEIVING)
    sprint(b, n, _(S_RECEIVING_PCT), pic_percent());
  else if (pic_state() == PIC_READY)
    sprint(b, n, _(S_PICTURE_SIZE), pic_width(), pic_height());
  else
    copy_text(b, n, _(S_NONE_YET));
}
static void hours(char *b, size_t n, uint32_t h) {
  if (h >= 1000)
    sprint(b, n, _(S_HOURS_THOUSANDS), (unsigned long) h / 1000, (unsigned long) h % 1000);
  else
    sprint(b, n, _(S_HOURS), (unsigned long) h);
}
static void info_h_total(char *b, size_t n) { hours(b, n, M.h_total); }
static void info_h_stop(char *b, size_t n) { hours(b, n, M.h_stop); }
static void info_h_min(char *b, size_t n) { hours(b, n, M.h_min); }
static void info_h_normal(char *b, size_t n) { hours(b, n, M.h_normal); }
static void info_h_max(char *b, size_t n) { hours(b, n, M.h_max); }
static void info_h_rotor(char *b, size_t n) { hours(b, n, M.h_rotor); }
static void info_h_heater(char *b, size_t n) { hours(b, n, M.h_heater); }
static void info_filter_resets(char *b, size_t n) { sprint(b, n, "%u", M.filter_resets); }
static void dip(char *b, size_t n, int ds) { copy_text(b, n, (M.dip >> ds) & 1 ? _(S_ON) : _(S_OFF)); }
static void info_ds2(char *b, size_t n) { dip(b, n, 1); }
static void info_ds3(char *b, size_t n) { dip(b, n, 2); }
static void panel_version(char *b, size_t n, int i) {
  uint16_t v = M.panel_version[i];
  if (v)
    sprint(b, n, "%d.%d", v >> 8, v & 0xFF);
  else
    copy_text(b, n, _(S_NO_PANEL));
}
static void info_panel1(char *b, size_t n) { panel_version(b, n, 0); }
static void info_panel2(char *b, size_t n) { panel_version(b, n, 1); }
static void info_panel3(char *b, size_t n) { panel_version(b, n, 2); }

// ---------------------------------------------------------------- option names
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
static const str_t O_FIRE[5] = {
    [1] = S_STOP_BOTH_FANS, [2] = S_BOTH_AT_MAX, [3] = S_EXTRACT_AT_MAX, [4] = S_SUPPLY_AT_MAX};
static const str_t O_ROTOR_ALARM[2] = {S_STOP_THE_UNIT, S_KEEP_RUNNING};
static const str_t O_TEST_SPEED[MODE_COUNT + 1] = {S_OFF, S_STOP, S_MIN, S_NORMAL, S_MAX};
// same order as CPU_MHZ
static const str_t O_CPU_SPEED[N_CPU_SPEEDS] = {S_N_72_MHZ_STOCK, S_N_80_MHZ, S_N_88_MHZ, S_N_96_MHZ, S_N_128_MHZ};

// The four dashboard tiles show one of the unit's values, or any ESP item except buttons
static const char *tile_opt(int o) {
  const char *name = tile_name(o);
  return name ? name : NO_VALUE;  // an item that's gone
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
_Static_assert(TILE_UNIT + N_ITEMS <= MAX_OPTS, "tile_options fits");

static const opt_fn_t TILE_OPTS = {.text = tile_opt, .used = tile_used, .list = tile_options};
static const opt_fn_t VIEW_OPTS = {.used = view_used};
static const opt_fn_t LANG_OPTS = {.text = lang_name};
static const opt_fn_t SAVER_OPTS = {.used = saver_used};

// ---------------------------------------------------------------- rows shown only with others set
// The text rows only matter when there's text on a picture
static bool text_on_picture(void) { return M.screensaver == SAVER_PIC_CLOCK || M.screensaver == SAVER_PIC_ALL; }
static bool recovery_on(void) { return M.adv.cool_recovery; }
static bool deice_on(void) { return M.adv.deice; }
static bool advanced_on(void) { return M.advanced; }

// ---------------------------------------------------------------- Comfort and Smart home, from the ESP's items
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
const menu_t m_comfort = {.title = S_COMFORT, .row = comfort_row, .n = N_ITEMS};
static const menu_t m_smart = {.title = S_SMART_HOME, .row = smart_row, .n = N_ITEMS};

// ---------------------------------------------------------------- main menu status lines
// Comfort and Smart home: the first two sensor values, else the item count
static void grid_page(char *b, size_t n, bool comfort) {
  int items = 0, shown = 0;
  b[0] = 0;
  for (int i = 0; i < N_ITEMS; i++) {
    const item_t *it = &M.items[i];
    mitem_t m;
    if (!item_row(i, &m, comfort))
      continue;  // only what the menu lists
    items++;
    if (it->type == ITEM_SENSOR && shown < 2) {
      char v[24];
      add_text(add_text(b, n, shown++ ? " " MIDDOT " " : ""), n, item_text(v, sizeof v, it, true));
    }
  }
  if (!M.esp_online)
    copy_text(b, n, _(S_ESP_OFFLINE));
  else if (!items)
    copy_text(b, n, _(S_NOTHING_YET));
  else if (!shown)
    sprint(b, n, items == 1 ? _(S_N_ITEM) : _(S_N_ITEMS), items);
}
static void grid_comfort(char *b, size_t n) { grid_page(b, n, true); }
static void grid_smart(char *b, size_t n) { grid_page(b, n, false); }

static void grid_vent(char *b, size_t n) {
  char p[16];
  if (M.boost_left)
    sprint(b, n, _(S_BOOST_MIN), M.boost_left);
  else
    sprint(b, n, "%s, %s", tr(MODE_NAMES[M.mode_now]), fmt_pct(p, sizeof p, M.fan_sup));
}
static void grid_temp(char *b, size_t n) {
  char a[16];
  sprint(b, n, _(S_TARGET), fmt_target(a, sizeof a, M.setpoint));
}
static void grid_sched(char *b, size_t n) { copy_text(b, n, M.week_program ? _(S_PROGRAM_ON) : _(S_OFF)); }
static void grid_alarms(char *b, size_t n) {
  if (M.n_alarms)
    sprint(b, n, _(S_N_ACTIVE), M.n_alarms);
  else
    copy_text(b, n, _(S_ALL_CLEAR));
}
static void grid_stats(char *b, size_t n) {
  char h[24];
  hours(h, sizeof h, M.h_total);
  sprint(b, n, _(S_HOURS_TOTAL), h);
}
static void grid_display(char *b, size_t n) {
  char p[16];
  sprint(b, n, "%s, %s", tr(THEME_NAMES[M.theme % N_THEMES]), fmt_pct(p, sizeof p, M.brightness));
}
static void grid_saver(char *b, size_t n) { copy_text(b, n, tr(O_SAVER[saver_used(V_SCREENSAVER, M.screensaver)])); }
static void grid_time(char *b, size_t n) {
  char t[16];
  sprint(b, n, "%02d.%02d.%04d %s", M.dd, M.mo, M.yy, fmt_hm(t, sizeof t, M.hh * 60 + M.mi));
}
static void grid_advanced(char *b, size_t n) { copy_text(b, n, _(S_INSTALLER_SETTINGS)); }
static void grid_system(char *b, size_t n) {
  if (M.bus_offline)
    copy_text(b, n, _(S_NO_CONTACT));
  else if (M.cx60_version)
    sprint(b, n, _(S_ADDRESS_CX60), M.bus_addr, M.cx60_version >> 8, M.cx60_version & 0xFF);
  else
    sprint(b, n, _(S_ADDRESS), M.bus_addr);
}

// ---------------------------------------------------------------- menus
// One row per line, clang-format would pack them together.
// clang-format off
MENU(m_vent, S_VENTILATION,
     ENUM(IC_FAN, S_FAN_SPEED, V_MODE, S_FAN_SPEED_HELP, OPTS(MODE_NAMES)),
     TOG(IC_TIMER, S_BOOST_NOW, V_BOOST, S_BOOST_NOW_HELP),
     NUM(IC_TIMER_SAND, S_BOOST_TIME, V_BOOST_MINUTES, S_BOOST_TIME_HELP, .step = 5, .unit = S_UNIT_MIN));

#define TEMP(ic, t, v, h, ...) NUM(ic, t, v, h, .step = 5, .dec = 1, .unit = S_UNIT_C, __VA_ARGS__)
#define TENTHS(ic, t, v, h, ...) NUM(ic, t, v, h, .dec = 1, .unit = S_UNIT_C, __VA_ARGS__)  // offsets, differences
MENU(m_temp, S_TEMPERATURE,
     TEMP(IC_THERMOSTAT, S_SETPOINT, V_SETPOINT, S_SETPOINT_HELP),
     TOG(IC_FIRE, S_HEATER, V_HEATER, S_HEATER_HELP, .without = FBIT(FEAT_WATER_HEATER)),  // the unit keeps it on
     TOG(IC_SNOW, S_COOLING, V_COOLING, S_COOLING_HELP, .need = FBIT(FEAT_COOLING)),
     HEAD(S_SENSORS),
     INFO(IC_THERMO, S_OUTDOOR, info_outdoor, S_OUTDOOR_HELP),
     INFO(IC_ARROW_R, S_SUPPLY, info_supply, S_SUPPLY_HELP),
     INFO(IC_HOME_THERMO, S_EXTRACT, info_extract, S_EXTRACT_HELP),
     INFO(IC_WATER, S_RETURN_WATER, info_water, S_RETURN_WATER_HELP, .need = FBIT(FEAT_WATER_HEATER)),
     INFO(IC_THERMO, S_PANEL_SENSOR, info_room, S_PANEL_SENSOR_HELP));

#define DAY(t, d, h) SCR(IC_CALENDAR, t, SCR_DAY, h, .arg = d)
MENU(m_sched, S_WEEK_PROGRAM,
     TOG(IC_CAL_CLOCK, S_WEEK_PROGRAM, V_WEEK_PROGRAM, S_WEEK_PROGRAM_HELP),
     SCR(IC_CALENDAR, S_WEEK_OVERVIEW, SCR_SCHEDULE, S_WEEK_OVERVIEW_HELP),
     HEAD(S_DAYS),
     DAY(S_MONDAY, 0, S_MONDAY_HELP),
     DAY(S_TUESDAY, 1, S_TUESDAY_HELP),
     DAY(S_WEDNESDAY, 2, S_WEDNESDAY_HELP),
     DAY(S_THURSDAY, 3, S_THURSDAY_HELP),
     DAY(S_FRIDAY, 4, S_FRIDAY_HELP),
     DAY(S_SATURDAY, 5, S_SATURDAY_HELP),
     DAY(S_SUNDAY, 6, S_SUNDAY_HELP));

MENU(m_filter, S_FILTER,
     INFO(IC_FILTER, S_STATUS, info_filter_left, S_STATUS_HELP),
     INFO(IC_COUNTER, S_RUNNING_SINCE_CHANGE, info_filter_hours, S_RUNNING_SINCE_CHANGE_HELP),
     INFO(IC_COUNTER, S_TIMES_CHANGED, info_filter_resets, S_TIMES_CHANGED_HELP),
     NUM(IC_CALENDAR, S_CHANGE_EVERY, V_FILTER_INTERVAL, S_CHANGE_EVERY_HELP, .step = 30, .unit = S_UNIT_DAYS),
     ACT(IC_RESTORE, S_FILTERS_CHANGED, A_RESET_FILTER, S_FILTERS_CHANGED_HELP));

MENU(m_alarms, S_ALARMS,
     SCR(IC_BELL_RING, S_ACTIVE_ALARMS, SCR_ALARMS, S_ACTIVE_ALARMS_HELP),
     ENUM(IC_BELL, S_POP_UPS, V_ALARM_POPUPS, S_POP_UPS_HELP, OPTS(O_POPUPS)),
     SCR(IC_HISTORY, S_HISTORY, SCR_ALARMS, S_HISTORY_HELP, .arg = ALARMS_HISTORY),
     ACT(IC_CHECK_C, S_RESET_ALARMS, A_RESET_ALARMS, S_RESET_ALARMS_HELP),
     ACT(IC_DELETE, S_CLEAR_HISTORY, A_CLEAR_HISTORY, S_CLEAR_HISTORY_HELP, .flags = MF_DANGER));

MENU(m_stats, S_STATISTICS,
     INFO(IC_COUNTER, S_TOTAL, info_h_total, S_TOTAL_HOURS_HELP),
     INFO(IC_FAN_OFF, S_STOPPED, info_h_stop, S_STOPPED_HOURS_HELP),
     INFO(IC_FAN, S_MIN, info_h_min, S_MIN_HOURS_HELP),
     INFO(IC_FAN, S_NORMAL, info_h_normal, S_NORMAL_HOURS_HELP),
     INFO(IC_FAN, S_MAX, info_h_max, S_MAX_HOURS_HELP),
     INFO(IC_ROTOR, S_ROTOR, info_h_rotor, S_ROTOR_HOURS_HELP),
     INFO(IC_FIRE, S_HEATER, info_h_heater, S_HEATER_HOURS_HELP));

#define LIGHT(ic, t, v, h, fl) NUM(ic, t, v, h, .step = 5, .unit = S_UNIT_PCT, .flags = MF_BACKLIGHT | (fl))
MENU(m_night, S_NIGHT_MODE,
     TOG(IC_NIGHT, S_NIGHT_MODE, V_NIGHT, S_NIGHT_MODE_HELP),
     NUM(IC_CLOCK, S_FROM, V_NIGHT_FROM, S_NIGHT_FROM_HELP, .flags = MF_HOUR),
     NUM(IC_CLOCK, S_TO, V_NIGHT_TO, S_NIGHT_TO_HELP, .flags = MF_HOUR),
     LIGHT(IC_BRIGHTNESS, S_BRIGHTNESS, V_NIGHT_BRIGHTNESS, S_NIGHT_BRIGHTNESS_HELP, 0),
     LIGHT(IC_BRIGHTNESS, S_IDLE_BRIGHTNESS, V_NIGHT_IDLE_BRIGHTNESS, S_NIGHT_IDLE_BRIGHTNESS_HELP, MF_ZERO_OFF),
     ENUM(IC_PALETTE, S_THEME_AT_NIGHT, V_NIGHT_THEME, S_THEME_AT_NIGHT_HELP, OPTS(O_NIGHT_THEME)));

#define TILE_ROW(t, v) ENUM(IC_MONITOR, t, v, S_TILE_HELP, .fn = &TILE_OPTS)
MENU(m_tiles, S_DASHBOARD_TILES,
     TILE_ROW(S_TILE_1, V_TILE1),
     TILE_ROW(S_TILE_2, V_TILE2),
     TILE_ROW(S_TILE_3, V_TILE3),
     TILE_ROW(S_TILE_4, V_TILE4));

MENU(m_views, S_VIEWS,
     TOG(IC_FAN, S_AIRFLOW, V_SHOW_FLOW, S_AIRFLOW_HELP),
     TOG(IC_CHART, S_TRENDS, V_SHOW_TREND, S_TRENDS_HELP),
     TOG(IC_CHART, S_ESP_SENSORS, V_SHOW_SENSORS, S_SENSORS_HELP, .need = FBIT(FEAT_ESP)),
     TOG(IC_LEAF, S_COMFORT, V_SHOW_COMFORT, S_COMFORT_HELP, .need = FBIT(FEAT_ESP)),
     TOG(IC_HASS, S_SMART_HOME, V_SHOW_SMART, S_SMART_HOME_HELP, .need = FBIT(FEAT_ESP)));

MENU(m_home, S_HOME_SCREEN,
     ENUM(IC_HOME, S_START_VIEW, V_HOME_VIEW, S_START_VIEW_HELP, OPTS(O_VIEW), .fn = &VIEW_OPTS),
     SUB(IC_EYE, S_VIEWS, m_views, S_VIEWS_HELP),
     ENUM(IC_THERMO, S_BIG_TEMPERATURE, V_DASH_TEMP, S_BIG_TEMPERATURE_HELP, OPTS(O_BIG)),
     SUB(IC_MONITOR, S_DASHBOARD_TILES, m_tiles, S_DASHBOARD_TILES_HELP));

MENU(m_lights, S_LIGHTS,
     ENUM(IC_CHECK_C, S_OK_LIGHT, V_LED_OK, S_OK_LIGHT_HELP, OPTS(O_LED_OK)),
     TOG(IC_ALERT, S_ALARM_LIGHT, V_LED_ALARM, S_ALARM_LIGHT_HELP),
     TOG(IC_FILTER, S_FILTER_LIGHT, V_LED_FILTER, S_FILTER_LIGHT_HELP));

MENU(m_display, S_DISPLAY,
     ENUM(IC_PALETTE, S_THEME, V_THEME, S_THEME_HELP, OPTS(THEME_NAMES)),
     LIGHT(IC_BRIGHTNESS, S_BRIGHTNESS, V_BRIGHTNESS, S_BRIGHTNESS_HELP, 0),
     SUB(IC_NIGHT, S_NIGHT_MODE, m_night, S_NIGHT_MODE_HELP),
     SUB(IC_HOME, S_HOME_SCREEN, m_home, S_HOME_SCREEN_HELP),
     SUB(IC_BULB, S_LIGHTS, m_lights, S_LIGHTS_HELP),
     ENUM(IC_LIST, S_LANGUAGE, V_LANGUAGE, S_LANGUAGE_HELP, .fn = &LANG_OPTS),
     TOG(IC_CLOCK, S_N_24_HOUR_CLOCK, V_CLOCK_24H, S_N_24_HOUR_CLOCK_HELP),
     ENUM(IC_THERMO, S_TEMPERATURES, V_TEMP_DEC, S_TEMPERATURES_HELP, OPTS(O_DECIMALS)),
     TOG(IC_HELP, S_KEY_HINTS, V_KEY_HINTS, S_KEY_HINTS_HELP),
     TOG(IC_MONITOR, S_FAST_DISPLAY, V_FAST_DISPLAY, S_FAST_DISPLAY_HELP, .done = S_ONLY_UP_TO_80_MHZ));

MENU(m_saver, S_SCREENSAVER,
     ENUM(IC_SLEEP, S_IDLE_AFTER, V_IDLE_AFTER, S_IDLE_AFTER_HELP, OPTS(O_IDLE_AFTER)),
     LIGHT(IC_NIGHT, S_IDLE_BRIGHTNESS, V_IDLE_BRIGHTNESS, S_IDLE_BRIGHTNESS_HELP, MF_ZERO_OFF),
     ENUM(IC_MONITOR, S_SHOW, V_SCREENSAVER, S_SCREENSAVER_HELP, OPTS(O_SAVER), .fn = &SAVER_OPTS),
     ENUM(IC_LIST, S_TEXT_ON_PICTURE, V_SAVER_TEXT, S_TEXT_ON_PICTURE_HELP, OPTS(O_SAVER_TEXT), .need = FBIT(FEAT_ESP),
          .shown = text_on_picture),
     ENUM(IC_UP, S_TEXT_POSITION, V_SAVER_TOP, S_TEXT_POSITION_HELP, OPTS(O_SAVER_POS), .need = FBIT(FEAT_ESP),
          .shown = text_on_picture),
     INFO(IC_PALETTE, S_PICTURE, info_picture, S_PICTURE_HELP, .need = FBIT(FEAT_ESP)),
     TOG(IC_HOME_CLOCK, S_BACK_TO_START_VIEW, V_GO_HOME, S_BACK_TO_START_VIEW_HELP));

MENU(m_time, S_DATE_AND_TIME,
     NUM(IC_CLOCK, S_HOUR, V_HOUR, S_HOUR_HELP),
     NUM(IC_CLOCK, S_MINUTE, V_MINUTE, S_MINUTE_HELP),
     NUM(IC_CALENDAR, S_DAY, V_DAY, S_DAY_HELP),
     NUM(IC_CALENDAR, S_MONTH, V_MONTH, S_MONTH_HELP),
     NUM(IC_CALENDAR, S_YEAR, V_YEAR, S_YEAR_HELP),
     TOG(IC_SUN, S_SUMMER_TIME, V_AUTO_DST, S_SUMMER_TIME_HELP, .without = FBIT(FEAT_ESP)));

// one toggle per FEATURES() entry
#define FEAT_ITEM(id, ic, t, h, ...) TOG(ic, S_##t, V_FEAT_FIRST + FEAT_##id, S_##h),
MENU(m_equipment, S_EQUIPMENT, FEATURES(FEAT_ITEM));
#undef FEAT_ITEM

MENU(m_system, S_SYSTEM,
     SCR(IC_INFO, S_ABOUT, SCR_INFO, S_ABOUT_HELP),
     SUB(IC_TOOLS, S_EQUIPMENT, m_equipment, S_EQUIPMENT_HELP),
     SCR(IC_LAN, S_BUS, SCR_INFO, S_BUS_HELP, .arg = INFO_BUS),
     SCR(IC_COUNTER, S_DIAGNOSTICS, SCR_INFO, S_DIAGNOSTICS_HELP, .arg = INFO_DIAG),
     NUM(IC_LAN, S_PANEL_ADDRESS, V_BUS_ADDR, S_PANEL_ADDRESS_HELP),
     TOG(IC_LOCK, S_KEY_LOCK, V_KEY_LOCK, S_KEY_LOCK_HELP),
     TOG(IC_LOCK, S_LOCK_WHEN_IDLE, V_AUTO_LOCK, S_LOCK_WHEN_IDLE_HELP),
     TOG(IC_COG, S_ADVANCED_MENU, V_ADVANCED, S_ADVANCED_MENU_HELP),
     ACT(IC_REFRESH, S_RESTART_PANEL, A_RESTART, S_RESTART_PANEL_HELP),
     ACT(IC_RESTORE, S_RESET_PANEL_SETTINGS, A_RESET_PANEL, S_RESET_PANEL_SETTINGS_HELP, .flags = MF_DANGER));

// ---------------------------------------------------------------- Advanced, the installer's settings
// Stock had these behind PINs, here they're behind System > Advanced menu.
#define FANS(t, v, h) NUM(IC_FAN, t, v, h, .unit = S_UNIT_PCT, .need = FBIT(FEAT_EC_FANS))
MENU(m_adv_fans, S_FANS,
     FANS(S_MIN_SUPPLY, V_FAN_MIN_SUP, S_MIN_SUPPLY_HELP),
     FANS(S_MIN_EXTRACT, V_FAN_MIN_EXT, S_MIN_EXTRACT_HELP),
     FANS(S_NORMAL_SUPPLY, V_FAN_NORM_SUP, S_NORMAL_SUPPLY_HELP),
     FANS(S_NORMAL_EXTRACT, V_FAN_NORM_EXT, S_NORMAL_EXTRACT_HELP),
     FANS(S_MAX_SUPPLY, V_FAN_MAX_SUP, S_MAX_SUPPLY_HELP),
     FANS(S_MAX_EXTRACT, V_FAN_MAX_EXT, S_MAX_EXTRACT_HELP),
     ENUM(IC_TIMER, S_BOOST_SPEED, V_BOOST_SPEED, S_BOOST_SPEED_HELP, OPTS(MODE_NAMES), .without = FBIT(FEAT_EC_FANS)));

#define PI(t, v, h, ...) NUM(IC_TUNE, t, v, h, __VA_ARGS__)
#define COOL .need = FBIT(FEAT_COOLING)
MENU(m_adv_pi, S_REGULATOR_TUNING,
     HEAD(S_HEATING),
     PI(S_SUPPLY_AIR_K, V_PI_HEAT_SUP_K, S_SUPPLY_K_HELP),
     PI(S_SUPPLY_AIR_I, V_PI_HEAT_SUP_I, S_SUPPLY_I_HELP),
     PI(S_EXTRACT_AIR_K, V_PI_HEAT_EXT_K, S_EXTRACT_K_HELP),
     PI(S_EXTRACT_AIR_I, V_PI_HEAT_EXT_I, S_EXTRACT_I_HELP),
     HEAD(S_ROTOR),
     PI(S_SUPPLY_AIR_K, V_PI_ROTOR_SUP_K, S_SUPPLY_K_HELP),
     PI(S_SUPPLY_AIR_I, V_PI_ROTOR_SUP_I, S_SUPPLY_I_HELP),
     PI(S_EXTRACT_AIR_K, V_PI_ROTOR_EXT_K, S_EXTRACT_K_HELP),
     PI(S_EXTRACT_AIR_I, V_PI_ROTOR_EXT_I, S_EXTRACT_I_HELP),
     HEAD(S_COOLING),
     PI(S_SUPPLY_AIR_K, V_PI_COOL_SUP_K, S_SUPPLY_K_HELP, COOL),
     PI(S_SUPPLY_AIR_I, V_PI_COOL_SUP_I, S_SUPPLY_I_HELP, COOL),
     PI(S_EXTRACT_AIR_K, V_PI_COOL_EXT_K, S_EXTRACT_K_HELP, COOL),
     PI(S_EXTRACT_AIR_I, V_PI_COOL_EXT_I, S_EXTRACT_I_HELP, COOL));

MENU(m_adv_reg, S_REGULATION,
     ENUM(IC_TUNE, S_REGULATE_ON, V_REGULATION, S_REGULATE_ON_HELP, OPTS(O_REG)),
     TEMP(IC_THERMO_LO, S_MIN_SUPPLY_AIR, V_SUPPLY_MIN, S_MIN_SUPPLY_AIR_HELP),
     TEMP(IC_THERMO_HI, S_MAX_SUPPLY_AIR, V_SUPPLY_MAX, S_MAX_SUPPLY_AIR_HELP),
     HEAD(S_NEUTRAL_ZONES),
     TENTHS(IC_SNOW, S_COOLING_TO_ROTOR, V_NEUTRAL_COOL, S_COOLING_TO_ROTOR_HELP, COOL),
     TENTHS(IC_ROTOR, S_ROTOR_TO_HEATING, V_NEUTRAL_HEAT, S_ROTOR_TO_HEATING_HELP),
     SUB(IC_TUNE, S_REGULATOR_TUNING, m_adv_pi, S_REGULATOR_TUNING_HELP));

MENU(m_adv_cooling, S_COOLING,
     TEMP(IC_THERMO_LO, S_MIN_OUTDOOR_TEMP, V_COOL_MIN_OUTDOOR, S_MIN_OUTDOOR_TEMP_HELP),
     ENUM(IC_FAN, S_MIN_FAN_SPEED, V_COOL_MIN_SPEED, S_MIN_FAN_SPEED_HELP, OPTS(MODE_NAMES)),
     NUM(IC_TIMER_SAND, S_RESTART_DELAY, V_COOL_DELAY, S_RESTART_DELAY_HELP, .step = 10, .unit = S_UNIT_S));

MENU(m_adv_recovery, S_HEAT_RECOVERY,
     TOG(IC_SNOW_THERMO, S_COOLING_RECOVERY, V_COOL_RECOVERY, S_COOLING_RECOVERY_HELP),
     TENTHS(IC_THERMO, S_DIFFERENCE, V_RECOVERY_DIFF, S_DIFFERENCE_HELP, .shown = recovery_on),
     ENUM(IC_ALERT, S_ROTOR_ALARM, V_ROTOR_ALARM, S_ROTOR_ALARM_HELP, OPTS(O_ROTOR_ALARM)),
     TOG(IC_SNOWY, S_DE_ICING, V_DEICE, S_DE_ICING_HELP),
     TEMP(IC_THERMO_LO, S_STARTS_BELOW, V_DEICE_TEMP, S_STARTS_BELOW_HELP, .shown = deice_on),
     NUM(IC_ROTOR, S_ROTOR_SPEED, V_DEICE_ROTOR, S_ROTOR_SPEED_HELP, .shown = deice_on),
     NUM(IC_TIMER_SAND, S_ROTOR_PERIOD, V_DEICE_ROTOR_PERIOD, S_ROTOR_PERIOD_HELP, .shown = deice_on),
     NUM(IC_REFRESH, S_ROTOR_FREQUENCY, V_DEICE_ROTOR_FREQ, S_ROTOR_FREQUENCY_HELP, .shown = deice_on),
     TEMP(IC_FAN, S_LESS_AIR_BELOW, V_LESS_AIR_TEMP, S_LESS_AIR_BELOW_HELP, .shown = deice_on),
     NUM(IC_FAN, S_REDUCED_FAN_SPEED, V_DEICE_FAN, S_REDUCED_FAN_SPEED_HELP, .step = 5, .unit = S_UNIT_PCT,
         .shown = deice_on),
     NUM(IC_TIMER_SAND, S_FAN_PERIOD, V_DEICE_FAN_PERIOD, S_FAN_PERIOD_HELP, .shown = deice_on),
     NUM(IC_REFRESH, S_FAN_FREQUENCY, V_DEICE_FAN_FREQ, S_FAN_FREQUENCY_HELP, .shown = deice_on),
     NUM(IC_SPEED, S_RAMP_TIME, V_DEICE_RAMP, S_RAMP_TIME_HELP, .shown = deice_on));

MENU(m_adv_sensors, S_SENSORS,
     TENTHS(IC_ARROW_R, S_SUPPLY_AIR_OFFSET, V_CAL_SUPPLY, S_SENSOR_OFFSET_HELP),
     TENTHS(IC_HOME_THERMO, S_EXTRACT_AIR_OFFSET, V_CAL_EXTRACT, S_SENSOR_OFFSET_HELP),
     TENTHS(IC_THERMO, S_OUTDOOR_OFFSET, V_CAL_OUTDOOR, S_SENSOR_OFFSET_HELP),
     TENTHS(IC_WATER, S_RETURN_WATER_OFFSET, V_CAL_WATER, S_SENSOR_OFFSET_HELP, .need = FBIT(FEAT_WATER_HEATER)),
     TOG(IC_HOME_THERMO, S_EXTRACT_AIR_SENSOR, V_EXTRACT_SENSOR, S_EXTRACT_AIR_SENSOR_HELP),
     TOG(IC_THERMO, S_OUTDOOR_SENSOR, V_OUTDOOR_SENSOR, S_OUTDOOR_SENSOR_HELP),
     HEAD(S_THIS_PANEL),
     TENTHS(IC_THERMO, S_PANEL_SENSOR_OFFSET, V_ROOM_OFFSET, S_PANEL_SENSOR_OFFSET_HELP),
     ENUM(IC_HOME_THERMO, S_INDOOR_TEMPERATURE, V_INDOOR_SOURCE, S_INDOOR_TEMPERATURE_HELP, OPTS(O_INDOOR)));

#define AC_FANS .without = FBIT(FEAT_EC_FANS)
MENU(m_adv_inputs, S_INPUTS,
     ENUM(IC_FIRE, S_FIRE_SMOKE, V_FIRE_MODE, S_FIRE_SMOKE_HELP, OPTS(O_FIRE)),
     TOG(IC_FILTER, S_FILTER_GUARD, V_FILTER_GUARD, S_FILTER_GUARD_HELP),
     TOG(IC_THERMOSTAT, S_EXTERNAL_SETPOINT, V_EXT_SETPOINT, S_EXTERNAL_SETPOINT_HELP),
     HEAD(S_HOME_AWAY),
     ENUM(IC_AWAY, S_FAN_SPEED, V_AWAY_SPEED, S_AWAY_SPEED_HELP, OPTS(MODE_NAMES)),
     TEMP(IC_AWAY, S_SETPOINT, V_AWAY_SETPOINT, S_AWAY_SETPOINT_HELP),
     NUM(IC_TIMER_SAND, S_HOLD_TIME, V_AWAY_HOLD, S_HOLD_TIME_HELP, .step = 5, .unit = S_UNIT_MIN),
     HEAD(S_FIREPLACE_INPUT),
     ENUM(IC_FIREPLACE, S_SUPPLY_SPEED, V_FIREPLACE_SUP, S_SUPPLY_SPEED_HELP, OPTS(MODE_NAMES), AC_FANS),
     ENUM(IC_FIREPLACE, S_EXTRACT_SPEED, V_FIREPLACE_EXT, S_EXTRACT_SPEED_HELP, OPTS(MODE_NAMES), AC_FANS));

#define DELAY(t, v) NUM(IC_TIMER_SAND, t, v, S_START_DELAY_HELP, .unit = S_UNIT_S)
MENU(m_adv_start, S_START_AND_STOP,
     DELAY(S_START_DELAY_1, V_START_DELAY1),
     DELAY(S_START_DELAY_2, V_START_DELAY2),
     DELAY(S_START_DELAY_3, V_START_DELAY3),
     DELAY(S_START_DELAY_4, V_START_DELAY4),
     NUM(IC_FAN_CLOCK, S_FANS_AFTER_HEATER, V_RUN_ON, S_FANS_AFTER_HEATER_HELP, .step = 10, .unit = S_UNIT_S,
         .without = FBIT(FEAT_WATER_HEATER)));

#define PCT_OR_OFF .step = 10, .unit = S_UNIT_PCT, .flags = MF_ZERO_OFF
MENU(m_adv_test, S_TEST,
     ENUM(IC_FAN, S_FANS, V_TEST_SPEED, S_TEST_FANS_HELP, OPTS(O_TEST_SPEED), AC_FANS),
     NUM(IC_FAN, S_FANS, V_TEST_FANS, S_TEST_FANS_HELP, PCT_OR_OFF, .need = FBIT(FEAT_EC_FANS)),
     TOG(IC_FIRE, S_HEATER, V_TEST_HEATER, S_TEST_HEATER_HELP),
     TOG(IC_ROTOR, S_ROTOR, V_TEST_ROTOR, S_TEST_ROTOR_HELP),
     NUM(IC_SNOW, S_COOLING, V_TEST_COOLING, S_TEST_COOLING_HELP, PCT_OR_OFF, COOL));

MENU(m_adv_unit, S_UNIT,
     INFO(IC_ROTOR, S_DS2_ROTOR_GUARD, info_ds2, S_DS2_HELP),
     INFO(IC_ROTOR, S_DS3_ROTOR_MOTOR, info_ds3, S_DS3_HELP),
     HEAD(S_PANELS),
     INFO(IC_MONITOR, S_PANEL_1, info_panel1, S_PANELS_HELP),
     INFO(IC_MONITOR, S_PANEL_2, info_panel2, S_PANELS_HELP),
     INFO(IC_MONITOR, S_PANEL_3, info_panel3, S_PANELS_HELP));

MENU(m_advanced, S_ADVANCED,
     SUB(IC_FAN, S_FANS, m_adv_fans, S_ADV_FANS_HELP),
     SUB(IC_TUNE, S_REGULATION, m_adv_reg, S_REGULATION_HELP),
     SUB(IC_SNOW, S_COOLING, m_adv_cooling, S_ADV_COOLING_HELP, COOL),
     SUB(IC_ROTOR, S_HEAT_RECOVERY, m_adv_recovery, S_HEAT_RECOVERY_HELP),
     SUB(IC_THERMO, S_SENSORS, m_adv_sensors, S_ADV_SENSORS_HELP),
     SUB(IC_PLUG, S_INPUTS, m_adv_inputs, S_INPUTS_HELP),
     SUB(IC_POWER, S_START_AND_STOP, m_adv_start, S_START_AND_STOP_HELP),
     SUB(IC_SERVICE, S_TEST, m_adv_test, S_TEST_HELP),
     SUB(IC_INFO, S_UNIT, m_adv_unit, S_UNIT_INFO_HELP),
     ACT(IC_RESTORE, S_FACTORY_RESET_UNIT, A_FACTORY_RESET, S_FACTORY_RESET_UNIT_HELP, .flags = MF_DANGER),
     HEAD(S_THIS_PANEL),
     ENUM(IC_CHIP, S_PROCESSOR_SPEED, V_CPU_SPEED, S_PROCESSOR_SPEED_HELP, OPTS(O_CPU_SPEED)));

// Main menu grid. Each entry opens a submenu, .fmt writes its status line.
static const mitem_t m_main_items[] = {
    SUB(IC_FAN, S_VENTILATION, m_vent, S_VENTILATION_HELP, .fmt = grid_vent),
    SUB(IC_THERMOSTAT, S_TEMPERATURE, m_temp, S_TEMPERATURE_HELP, .fmt = grid_temp),
    SUB(IC_LEAF, S_COMFORT, m_comfort, S_COMFORT_MENU_HELP, .fmt = grid_comfort, .need = FBIT(FEAT_ESP)),
    SUB(IC_HASS, S_SMART_HOME, m_smart, S_SMART_HOME_HELP, .fmt = grid_smart, .need = FBIT(FEAT_ESP)),
    SUB(IC_CAL_CLOCK, S_WEEK_PROGRAM, m_sched, S_SCHEDULE_HELP, .fmt = grid_sched),
    SUB(IC_FILTER, S_FILTER, m_filter, S_FILTER_HELP, .fmt = info_filter_left),
    SUB(IC_BELL, S_ALARMS, m_alarms, S_ALARMS_HELP, .fmt = grid_alarms, .flags = MF_ALARMS),
    SUB(IC_CHART, S_STATISTICS, m_stats, S_STATISTICS_HELP, .fmt = grid_stats),
    SUB(IC_PALETTE, S_DISPLAY, m_display, S_DISPLAY_HELP, .fmt = grid_display),
    SUB(IC_MONITOR, S_SCREENSAVER, m_saver, S_SCREENSAVER_MENU_HELP, .fmt = grid_saver),
    SUB(IC_CLOCK, S_DATE_AND_TIME, m_time, S_DATE_AND_TIME_HELP, .fmt = grid_time),
    SUB(IC_CHIP, S_SYSTEM, m_system, S_SYSTEM_HELP, .fmt = grid_system),
    SUB(IC_COG, S_ADVANCED, m_advanced, S_ADVANCED_HELP, .fmt = grid_advanced, .shown = advanced_on),
};
const menu_t m_main = {.title = S_MENU, .items = m_main_items, .n = LEN(m_main_items)};
_Static_assert(LEN(m_main_items) <= GRID_MAX, "the grid has room for the main menu");

// Settings for one week program period (M.period), see sched_edit()
static const mitem_t period_items[] = {
    NUM(IC_CLOCK, S_START, V_PERIOD_START, S_PERIOD_START_HELP, .step = 15, .flags = MF_TIME),
    NUM(IC_CLOCK, S_END, V_PERIOD_END, S_PERIOD_END_HELP, .step = 15, .flags = MF_TIME),
    ENUM(IC_FAN, S_FAN_SPEED, V_PERIOD_LEVEL, S_PERIOD_FAN_SPEED_HELP, OPTS(MODE_NAMES)),
    TEMP(IC_THERMOSTAT, S_SETPOINT, V_PERIOD_SETPOINT, S_PERIOD_SETPOINT_HELP),
    ACT(IC_DELETE, S_REMOVE_PERIOD, A_REMOVE_PERIOD, S_REMOVE_PERIOD_HELP, .flags = MF_POP_PARENT,
        .done = S_PERIOD_REMOVED),
};
// clang-format on

void open_filter_menu(void) { ui_push(&SCR_LIST, (intptr_t) &m_filter); }

static char period_title[24];

// Title like "Monday, period 2", numbered by where the period is in the day now
static void period_name(void) {
  int n = 0;
  for (int i = 0; i < M.n_periods[M.period.day]; i++) {
    n++;
    if (M.week[M.period.day][i].idx == M.period.p.idx)
      break;
  }
  lv_snprintf(period_title, sizeof period_title, _(S_DAY_PERIOD), tr(DAY_NAMES[M.period.day % 7]), n);
  ui_set_title(period_title);
}

static const menu_t m_period = {.items = period_items, .n = LEN(period_items), .prepare = period_name};

const menu_t *period_menu(void) { return &m_period; }

// make check: each enum row has one named option per value. bad is the count before m's.
static int check(const menu_t *m, char *why, size_t n, int bad) {
  for (int i = 0; m->items && i < m->n; i++) {
    const mitem_t *it = &m->items[i];
    if (it->type == MI_SUB && it->target != m)
      bad = check(it->target, why, n, bad);
    if (it->type != MI_ENUM || (it->fn && it->fn->text))
      continue;
    val_def_t d = val_def(it->val);
    int unnamed = (int) d.min;  // options below the minimum are never offered
    while (unnamed < it->n_opts && it->opts[unnamed] != S_NONE)
      unnamed++;
    bool miscounted = it->n_opts != d.max + 1, wrong = miscounted || unnamed < it->n_opts;
    if (wrong) {
      if (!bad && miscounted)
        lv_snprintf(why, n, "%s: %d options for %ld values", tr(it->title), it->n_opts, (long) d.max + 1);
      else if (!bad)
        lv_snprintf(why, n, "%s: option %d has no name", tr(it->title), unnamed);
      bad++;
    }
  }
  return bad;
}

int menu_check(char *why, size_t n) { return check(&m_period, why, n, check(&m_main, why, n, 0)); }
