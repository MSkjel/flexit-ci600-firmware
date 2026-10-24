// Panel state, everything the UI shows and edits. Filled from the CU60, the ESP and our own hardware, or with
// made-up data in the simulator.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "equipment.h"
#include "plink_values.h"  // values shared with the ESP, and what their numbers mean

#define LEN(a) (sizeof(a) / sizeof((a)[0]))
#define T_NONE INT16_MIN      // no sensor / unknown
#define TREND_NONE INT16_MAX  // no sample yet (LVGL's LV_CHART_POINT_NONE)
#define TREND_LEN 96          // 24 h at 15 min
#define N_TRENDS 4            // ESP sensors the panel can keep a 24 h trend of
#define NOTE_LEN (PLINK_TEXT_MAX + 1)

enum { TR_OUTDOOR, TR_SUPPLY, TR_EXTRACT, TR_ROOM, TR_N };        // temperature trend series, M.trend[]
enum { DT_INDOOR, DT_OUTDOOR, DT_SUPPLY, DT_EXTRACT, DT_COUNT };  // which temperature the dashboard shows big
// how the clock and temperatures are drawn over a picture
enum { SAVER_TEXT_BAND, SAVER_TEXT_SHADOW, SAVER_TEXT_PLAIN, SAVER_TEXT_SMALL, SAVER_TEXT_COUNT };
enum { POPUPS_ALL, POPUPS_NOT_FILTER, POPUPS_OFF, POPUPS_COUNT };              // which new alarms pop up
enum { LED_OK_ALWAYS, LED_OK_NOT_NIGHT, LED_OK_NEVER, LED_OK_COUNT };          // when the OK light is on
enum { RESET_POWER, RESET_PIN, RESET_WATCHDOG, RESET_SOFTWARE, RESET_FAULT };  // why the panel last started
#define ALARM_FILTER 10                  // CU60 filter reminder, sorted after all other alarms
#define ALARM_FILTER_GUARD 3             // the filter guard input, on the filter light like the reminder
extern const uint16_t TILE_DEFAULTS[4];  // factory dashboard tiles

typedef struct {
  uint8_t code;            // CU60 alarm code
  uint8_t dd, mo, hh, mi;  // when it started
  bool active;             // for history entries, still active
} alarm_t;

// One period of a day in the week program
typedef struct {
  uint8_t start_h, start_m, end_h, end_m, level;  // level = MODE_*
  uint8_t idx;                                    // which of the day's four CU60 slots it uses
  int16_t setpoint;                               // 0.1 C
} period_t;

// The period open in Schedule > day > period, edited through V_PERIOD_*. See sched_edit()
typedef struct {
  uint8_t day, idx;     // day 0 = Monday, idx as in period_t
  uint16_t start, end;  // minutes after midnight
  uint8_t level;
  int16_t setpoint;
} period_edit_t;

// Item defined by the ESP, K_ITEM_* in proto/plink_keys.h. type 0 = unused slot
typedef struct item {
  uint8_t type, flags, icon, dec;  // ITEM_*, ITEMF_*, ICN_*, decimals
  uint16_t step, id;               // id = hash of the item's name, tiles and trends refer to it by that
  int32_t value, min, max;
  int16_t level[4];  // ITEMF_LEVELS: bad below [0], good from [1] to [2], bad above [3], fair in between
  char label[ITEM_LABEL], unit[ITEM_UNIT];
  char on[ITEM_STATE], off[ITEM_STATE];  // a toggle's words for on and off, "" for On and Off
  char text[ITEM_TEXT_LEN];              // an ITEM_TEXT's value, "" for none
} item_t;

// 24 h of one ITEMF_TREND sensor. Kept by item id so it carries on when the ESP renumbers its items.
typedef struct {
  uint16_t id;            // 0 = free slot
  int16_t pt[TREND_LEN];  // oldest first, TREND_NONE where there was no value
} item_trend_t;

typedef struct {
  // temperatures, 0.1 C
  int16_t t_outdoor, t_supply, t_extract, t_water, t_room;
  int16_t setpoint, supply_min, supply_max;
  int16_t setpoint_now;  // the one in effect now, from the week program or external control. 0 = unknown
  uint8_t regulation;    // REG_*

  // ventilation
  uint8_t mode;      // MODE_*, the one set. A boost or the week program may run another
  uint8_t mode_now;  // the one running
  bool boost_on;
  uint16_t boost_left;       // MAX timer minutes left, 0 = off
  uint16_t boost_minutes;    // MAX timer length
  uint8_t fan_sup, fan_ext;  // current output %
  uint8_t fan_pct[3][2];     // [MIN/NORMAL/MAX][supply/extract] %

  // heat recovery, heating, cooling
  uint8_t rotor_pct, heat_pct, cool_pct;
  bool heater_enabled, cooling_enabled;  // heater: as the unit has it, the panels' flags ORed

  // filter
  uint16_t filter_hours;  // since the last change
  uint16_t filter_days;   // change interval

  // alarms
  alarm_t alarms[8];  // active alarms, the filter reminder last
  uint8_t n_alarms;
  alarm_t history[5];  // the CU60's alarm log
  uint8_t n_history;

  // schedule
  bool week_program;
  period_t week[7][4];  // each day's periods, sorted by start time
  uint8_t n_periods[7];
  period_edit_t period;

  // ESP, bus address 1
  bool esp_online;
  uint16_t esp_version;  // link version from its HELLO, 0 = none yet
  item_t items[N_ITEMS];
  item_trend_t item_trends[N_TRENDS];
  char note[NOTE_LEN];            // notification text from the ESP
  uint8_t note_icon, note_level;  // ICN_*, NOTE_*
  bool note_new;

  uint8_t trend[TR_N][TREND_LEN];  // last 24 h of the temperatures, oldest first, encoded by trend_code()

  // system
  uint16_t cu60_version;
  uint32_t fw_version;  // maj<<16 | min<<8 | patch
  uint8_t bus_addr;
  uint32_t bus_ok, bus_err, bus_ours;  // frames received, bad frames, polls to this panel
  bool bus_offline;                    // no frames from the CU60 for a while, so the values shown are stale
  uint32_t h_total, h_stop, h_min, h_normal, h_max, h_rotor, h_heater;  // run hours
  uint8_t cpu_busy;                                                     // % of the last second the CPU was awake
  uint8_t reset_cause;                                                  // RESET_*
  uint32_t fault_pc;                                                    // PC of the fault, for RESET_FAULT

  // clock
  uint8_t hh, mi, ss, dd, mo, wday;  // wday 0 = Monday
  uint16_t yy;

  // panel settings, saved on the panel
  uint8_t theme, brightness, idle_brightness, idle_after, home_view;  // idle_after is a step, see idle_seconds()
  uint16_t tiles[4];    // dashboard tiles, left to right: TILE_*, or TILE_ITEM + an item's id
  uint8_t language;     // 0 English, 1 Norwegian
  uint8_t screensaver;  // SAVER_*
  bool clock_24h, key_hints;
  int16_t room_offset;            // panel sensor correction, 0.1 C. The panel warms itself up
  bool night, key_lock;           // night mode on, keys locked
  uint8_t night_from, night_to;   // night mode hours, may wrap past midnight
  uint8_t night_idle_brightness;  // idle backlight at night, %, 0 = off
  uint8_t night_brightness;       // backlight in use at night, %
  uint8_t indoor_source;          // INDOOR_*
  uint8_t night_theme;            // 0 = same as the day, else theme + 1
  uint8_t dash_temp;              // DT_*
  uint8_t saver_text;             // SAVER_TEXT_*
  bool saver_top;                 // screensaver clock at the top of the picture
  bool go_home;                   // back to the home view when idle
  bool view_on[VIEW_COUNT];       // views BACK cycles through, dashboard always
  bool temp_dec;                  // temperatures with one decimal
  bool fast_display;              // pixels to the screen at 18 MHz, over the controller's rated 10
  uint8_t led_ok;                 // LED_OK_*
  bool led_alarm, led_filter;     // use the alarm and filter lights
  bool auto_lock;                 // lock the keys when idle
  uint8_t alarm_popups;           // POPUPS_*
  bool auto_dst, dst_now;         // switch summer time ourselves if no ESP, and whether we're on it now
  uint16_t leds;                  // light overrides from the ESP, 2 bits per light: LED_*
  uint32_t features;              // FBIT(FEAT_x) for each installed option, see equipment.h
} model_t;

extern model_t M;

#define FILTER_INTERVAL_H ((uint32_t) M.filter_days * 24u)

#include "values.h"
