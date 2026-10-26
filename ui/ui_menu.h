// Menu rows and menus. Tables in ui_menus.c, grid and row lists in ui_list.c, editor in ui_edit.c.
#pragma once
#include "ui_int.h"

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
  MF_HOUR = 32,        // an hour of the day, shown as a time
  MF_ALARMS = 64       // main menu entry, its status line in the alarm colour while there are alarms
};

typedef struct menu menu_t;

#define MAX_OPTS 32  // options an MI_ENUM row can offer

// MI_ENUM options that come from functions
typedef struct {
  const char *(*text)(int o);       // each option's text, instead of opts
  int (*used)(val_t val, int opt);  // option actually used for opt, a fallback when there's no ESP
  int (*list)(uint16_t *opt);       // options when they aren't 0..max, up to MAX_OPTS. Returns the count
} opt_fn_t;

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
  str_t done;               // MI_ACTION: toast shown after it, S_DONE if unset. MI_TOGGLE: toast when it can't go on
  const char *icon;
  union {
    const str_t *opts;   // MI_ENUM: option names, see OPTS
    const void *target;  // MI_SUB: menu_t, MI_SCREEN: screen_t
  };
  const opt_fn_t *fn;                // MI_ENUM: functions for the options, NULL for none
  void (*fmt)(char *buf, size_t n);  // MI_INFO text, or a main menu entry's status line, n bytes with the 0
  bool (*shown)(void);               // hidden while this is false, for rows that depend on another row
} mitem_t;
_Static_assert(FEAT_COUNT <= 8, "the features fit need and without");

// A menu has either fixed items or a row() function that makes them.
struct menu {
  str_t title;
  uint8_t n;                         // number of rows
  const mitem_t *items;              // fixed rows, NULL when row() generates them
  bool (*row)(int i, mitem_t *out);  // writes row i (0..n-1) to out, false if it's hidden right now
  void (*prepare)(void);             // called each time the menu opens, e.g. to set a title made at run time
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

// ui_menus.c
#define GRID_MAX 16             // main menu entries the grid has room for
extern const menu_t m_main;     // SCR_MENU's grid
extern const menu_t m_comfort;  // so SCR_LIST can tell Comfort from Smart home while the ESP is away

// ui_list.c, a row's texts
const char *item_title(const mitem_t *it);
const char *item_unit(const mitem_t *it);        // "" for none
const char *opt_text(const mitem_t *it, int o);  // an option's name, a dash for one that doesn't exist
int opt_used(const mitem_t *it, int o);          // option actually used for o, a fallback when it needs the ESP
bool shown_off(const mitem_t *it, int32_t v);    // v is shown as Off
const char *number_only(const mitem_t *it, int32_t v, char *buf, size_t n);  // the number without its unit, or Off
void row_help(const mitem_t *it);                                            // the help ? shows for the row
