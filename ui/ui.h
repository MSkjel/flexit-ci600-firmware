// CI600 replacement UI (LVGL 8.3, 320x240, five keys)
#pragma once
#include "lvgl.h"
#include "model.h"
#include "icons.h"
#include "plink_icons.h"
#include "input.h"
#include "link.h"
#include "panel.h"
#include "picture.h"
#include "anim.h"
#include "plat.h"
#include "schedule.h"
#include "lang.h"
#include "trend.h"

#define SCR_W 320
#define SCR_H 240
#define BAR_H 24   // status bar
#define HINT_H 20  // key hint bar
#define BODY_Y BAR_H
#define BODY_H (SCR_H - BAR_H - HINT_H)
#define HELP_W 260    // help text width, make check verifies every *_HELP text fits
#define HELP_LINES 6  // max lines of help text

// Key codes. HOME is BACK held, OK_LONG is OK held for 2 s.
enum { KEY_UP = 1, KEY_DOWN, KEY_OK, KEY_BACK, KEY_HELP, KEY_HOME, KEY_OK_LONG };

// Called by the platform
void ui_init(void);
void ui_key(int key);
void ui_idle(void);     // no keys for a while, back to the start view and the screensaver if they're on
void ui_wake(void);     // leaving idle, closes the screensaver
void ui_refresh(void);  // model changed
void ui_second(void);   // 1 Hz: night theme, new alarms and messages, redraw

// ---------------------------------------------------------------- UI internals
// Theme colours. The hex values for each theme are in ui_core.c.
// clang-format off
#define THEME_COLOURS(X) \
  X(bg) X(surface) X(surface2) X(line) X(text) X(dim) X(faint) X(accent) X(accent_soft) X(on_accent) X(warm) \
  X(cold) X(ok) X(warn) X(err)
// clang-format on
#define X(name) lv_color_t name;
typedef struct {
  THEME_COLOURS(X)
} theme_t;
#undef X
extern theme_t T;
extern const str_t THEME_NAMES[N_THEMES], MODE_NAMES[MODE_COUNT], DAY_NAMES[7];  // Monday first

// screen_t flags
enum {
  SF_OVERLAY = 1,  // drawn over the screen below, which stays built
  SF_NOBARS = 2    // full screen, no status or hint bar
};

// A kind of screen. Each open one is a frame_t on the screen stack.
typedef struct screen {
  str_t title;  // status bar title, S_NONE for none
  uint8_t flags;
  void (*build)(lv_obj_t *body, intptr_t arg);  // creates the widgets, arg comes from ui_push()
  void (*update)(void);                         // refreshes the values, on model changes and every second
  bool (*key)(int key);                         // true if it handled the key
  const char *(*selected)(void);                // text of the selected row, for the simulator's checks
} screen_t;

// One entry on the screen stack
typedef struct {
  const screen_t *s;
  intptr_t arg;
  int16_t sel, top;      // selected row and scroll position, kept while another screen is on top
  const char *title;     // set by ui_set_title(), NULL to use the screen's
  str_t hints[4], help;  // key hints (UP/DOWN, OK, BACK, ?) and the text ? shows
  lv_obj_t *root;        // overlay container, SF_OVERLAY only
  uint32_t close_at;     // lv_tick_get() time to close at, 0 = never
  bool built;            // built before, so building it again is a rebuild
} frame_t;

void ui_push(const screen_t *s, intptr_t arg);
void ui_pop(void);
const screen_t *ui_top(void);  // top screen, NULL before ui_init()
void ui_replace(const screen_t *s, intptr_t arg);
void ui_go_home(void);
frame_t *ui_frame(void);                                    // frame being built or updated, else the top one
void ui_rebuild(void);                                      // rebuild everything, e.g. after a theme change
void ui_set_title(const char *t);                           // keeps the pointer, so t must stay valid
void ui_help(str_t text);                                   // text the ? key shows for this screen
void ui_help_item(int item);                                // same, with an ESP item's help
void ui_hints(str_t ud, str_t ok, str_t back, str_t help);  // key hint labels, S_NONE hides one
void ui_toast(const char *icon, str_t msg);
const char *ui_selected(void);       // selected row on the top screen for the simulator's checks, NULL if none
const char *ui_title(void);          // title shown, for the simulator's checks
void ui_close_after(uint32_t ms);    // close the current screen after ms, calling again restarts the timer
void ui_view_dots(int n, int cur);   // page dots in the status bar for the home views
void ui_show_view(int view);         // back to the home screen on this view
const char *icon_glyph(uint8_t id);  // glyph for an ESP icon id, ICN_* in proto/plink_icons.h

// drawing helpers (ui_widgets.c)
#define CARD_RADIUS 14
#define CHART_X 40  // trend chart position and height
#define CHART_Y 26
#define CHART_H 140
lv_obj_t *mk_box(lv_obj_t *p, int x, int y, int w, int h, lv_color_t bg, int radius);
lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *txt);
lv_obj_t *mk_icon(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *ic);
void label_dots(lv_obj_t *l);                   // truncate with "...", copying static text first
void label_text(lv_obj_t *l, const char *txt);  // doesn't copy text in flash, so not for label_dots() labels
lv_obj_t *mk_bar(lv_obj_t *p, int x, int y, int w, int h, int val, int max, lv_color_t c);
lv_obj_t *mk_arc(lv_obj_t *p, int size, int width, int val, int max, lv_color_t c);
lv_obj_t *mk_card(lv_obj_t *p, int x, int y, int w, int h, lv_color_t border, int border_w);
lv_obj_t *mk_disc(lv_obj_t *p, int x, int y, int size, lv_color_t bg, const lv_font_t *f, lv_color_t c,
                  const char *icon);  // icon in a filled circle
lv_obj_t *mk_badge(lv_obj_t *p, int x, int y, int size, lv_color_t c, const lv_font_t *f, const char *icon);
void mk_empty(lv_obj_t *p, lv_color_t c, const char *icon, const char *title, const char *sub);  // empty state
lv_obj_t *mk_chart(lv_obj_t *p, int w, int lines);                    // 24 h chart with this many grid lines
lv_obj_t *day_bar(lv_obj_t *p, int x, int y, int w, int h, int day);  // a day's periods as blocks on a track
lv_color_t level_color(int mode);                                     // colour of a fan speed (MODE_*)
lv_color_t item_color(const item_t *it);                  // ok, warn or err by the item's levels, else accent
int item_pct(const item_t *it);                           // value as 0..100 between min and max, 0 if unknown
bool item_on(const item_t *it);                           // a toggle that's on, not off or unknown
char *item_text(char *buf, const item_t *it, bool unit);  // current value as text: the number, a dash, on/off or Press
void keep_in_view(int16_t *top, int row, int rows);       // scroll *top so row is among the rows shown
int16_t step_sel(int sel, int key, int n);                // move the selection for UP or DOWN, wrapping around
int32_t step_value(int32_t v, int n, int step, int32_t min, int32_t max);  // n steps, onto multiples of step, clamped
void set_text(lv_obj_t *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
const char *fmt_temp(char *buf, int16_t t, bool unit);
const char *fmt_target(char *buf, int16_t t);  // setpoint, always with one decimal whatever Temperatures says
const char *fmt_dec(char *buf, int32_t v, uint8_t dec);
const char *fmt_hm(char *buf, int minutes);   // minutes since midnight up to 24:00, 12 or 24 h as the clock is set
char *add_unit(char *buf, const char *unit);  // no space before %, : or °
int ui_backlight_preview(void);               // brightness an open editor is previewing, -1 if none

// Only one full screen is built at a time, so they share one state buffer. Overlays keep their own statics.
// SCREEN_STATE(type, name) points name into the buffer and checks that type fits.
#define UI_SCREEN_MEM 256
extern uint8_t ui_screen_mem[UI_SCREEN_MEM];
#define SCREEN_STATE(type, name) \
  static type *const name = (type *) (void *) ui_screen_mem; \
  _Static_assert(sizeof(type) <= UI_SCREEN_MEM, #type " fits in ui_screen_mem")

bool ui_has(uint32_t need);  // true if all features in the mask are installed

const char *alarm_text(uint8_t code);  // name of a CU60 alarm code

// screens
enum { ALARMS_NOW, ALARMS_HISTORY };       // arg for SCR_ALARMS
enum { INFO_ABOUT, INFO_BUS, INFO_DIAG };  // arg for SCR_INFO
extern const screen_t SCR_HOME, SCR_MENU, SCR_LIST, SCR_EDIT, SCR_ALARMS, SCR_ALARM_POPUP, SCR_SCHEDULE, SCR_INFO,
    SCR_HELP, SCR_SAVER, SCR_FAN_HUD, SCR_DAY, SCR_NOTE;
// A home screen view. BACK cycles through them in VIEW_* order.
typedef struct {
  void (*build)(lv_obj_t *body);
  void (*update)(void);
  str_t help;
  uint32_t need;            // required features, FBIT(FEAT_x) mask
  bool (*avail)(void);      // NULL = always shown, false hides the view when there's nothing to show
  bool (*next_page)(void);  // optional, BACK shows the next page first. Last page returns false and resets to page 1
} view_t;
extern const view_t DASH_VIEW, FLOW_VIEW, TREND_VIEW, SENSORS_VIEW, COMFORT_VIEW, SMART_VIEW;
struct menu;
const struct menu *period_menu(void);  // settings of the period in M.period, for SCR_LIST
void open_filter_menu(void);           // opens Filter, from the filter alarm pop-up
int menu_check(char *why, size_t n);   // make check: how many menu option problems, the first one goes in why
const char *tile_name(int src);        // name of a dashboard tile source (TILE_*), NULL if its ESP item is missing
// Option actually used for a setting. Falls back when it needs the ESP and there isn't one
int tile_used(val_t tile, int src);  // a dashboard tile (V_TILE1..4)
int view_used(val_t v, int view);    // the start view
int saver_used(val_t v, int mode);   // the screensaver
