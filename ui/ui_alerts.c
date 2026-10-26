// Alarm pop-up and lists, ESP messages and the help overlay.
#include "ui.h"

// Names by CX60 alarm code, from status registers 0xCD..0xEA
static const str_t ALARM_TXT[] = {
    S_ALARM,
    S_OVERHEAT_THERMOSTAT,
    S_FIRE_OR_SMOKE,
    S_FILTER_GUARD,
    S_SUPPLY_SENSOR_FAULT,
    S_EXTRACT_SENSOR_FAULT,
    S_OUTDOOR_SENSOR_FAULT,
    S_RETURN_WATER_SENSOR_FAULT,
    S_WATER_COIL_FROST,
    S_ROTOR_STOPPED,
    S_FILTER_CHANGE_DUE,
};
static const str_t ALARM_ADVICE[LEN(ALARM_TXT)] = {
    S_NONE,
    S_ADVICE_OVERHEAT,
    S_ADVICE_FIRE,
    S_ADVICE_FILTER_GUARD,
    S_ADVICE_SUPPLY_SENSOR,
    S_ADVICE_EXTRACT_SENSOR,
    S_ADVICE_OUTDOOR_SENSOR,
    S_ADVICE_RETURN_WATER_SENSOR,
    S_ADVICE_WATER_FROST,
    S_ADVICE_ROTOR,
    S_ADVICE_FILTER_DUE,
};
_Static_assert(LEN(ALARM_TXT) == ALARM_FILTER + 1, "an alarm text for each code");

static int alarm_code(uint8_t code) { return code < LEN(ALARM_TXT) ? code : 0; }

const char *alarm_text(uint8_t code) { return tr(ALARM_TXT[alarm_code(code)]); }

const alarm_t *alarm_find(intptr_t code) {
  for (int i = 0; i < M.n_alarms; i++)
    if (M.alarms[i].code == code)
      return &M.alarms[i];
  return NULL;
}

static lv_color_t alarm_color(uint8_t code) { return code == ALARM_FILTER ? T.warn : T.err; }

// Alert card with border and badge in colour c, and the kind of message next to the badge
static lv_obj_t *alert_card(lv_obj_t *b, int x, int y, int w, int h, lv_color_t c, int badge, const lv_font_t *f,
                            const char *icon, const char *kind) {
  lv_obj_t *card = mk_card(b, x, y, w, h, c, 2);
  mk_badge(card, 14, 14, badge, f, c, icon);
  lv_obj_set_pos(mk_label(card, &font_f12, c, kind), 14 + badge + 14, 14);
  return card;
}

// ---------------------------------------------------------------- alarm pop-up (arg = alarm code)
static void popup_build(lv_obj_t *b, intptr_t arg) {
  const alarm_t *a = alarm_find(arg);
  bool filter = a->code == ALARM_FILTER;
  lv_color_t c = alarm_color(a->code);
  lv_obj_t *card = alert_card(b, 12, 10, 296, 176, c, 52, &icons_32, filter ? IC_FILTER : IC_ALERT,
                              filter ? _(S_SERVICE_CAPS) : _(S_ALARM_CAPS));
  mk_wrap(card, &font_f20, T.text, alarm_text(a->code), 80, 31, 200);
  lv_obj_t *w = mk_label(card, &font_f12, T.dim, "");
  char hm[12];
  set_text(w, _(S_ALARM_SINCE), a->dd, a->mo, fmt_hm(hm, a->hh * 60 + a->mi));
  lv_obj_set_pos(w, 80, 60);
  mk_wrap(card, &font_f14, T.text, tr(ALARM_ADVICE[alarm_code(a->code)]), 14, 84, 268);
  if (M.n_alarms > 1) {
    lv_obj_t *more = mk_label(card, &font_f12, c, "");
    set_text(more, _(S_N_MORE), M.n_alarms - 1);
    lv_obj_align(more, LV_ALIGN_TOP_RIGHT, -12, 16);
  }
  ui_hints(S_NONE, filter ? S_FILTER_MENU : S_ALARM_LIST, filter ? S_LATER : S_CLOSE, S_HELP);
  ui_help(S_ALARM_POPUP_HELP);
}

static bool popup_key(int k) {
  if (k != KEY_OK)
    return false;
  bool filter = ui_frame()->arg == ALARM_FILTER;
  ui_pop();
  if (filter)
    open_filter_menu();
  else
    ui_push(&SCR_ALARMS, ALARMS_NOW);
  return true;
}

const screen_t SCR_ALARM_POPUP = {.flags = SF_OVERLAY, .build = popup_build, .key = popup_key};

// ---------------------------------------------------------------- active alarms, or the history (ALARMS_HISTORY)
#define ALARM_ROW 40
#define ALARM_ROWS (BODY_H / ALARM_ROW)  // rows on screen

typedef struct {
  lv_obj_t *list, *sel;
  int n;
} alarms_t;
SCREEN_STATE(alarms_t, alarms);

static void alarm_row(int i, const alarm_t *a, bool hist) {
  int y = 4 + i * ALARM_ROW;
  mk_box(alarms->list, 16, y + 13, 10, 10, !hist || a->active ? alarm_color(a->code) : T.faint, 5);
  lv_obj_set_pos(mk_label(alarms->list, &font_f16, T.text, alarm_text(a->code)), 36, y + 1);
  lv_obj_t *when = mk_label(alarms->list, &font_f12, T.dim, "");
  char hm[12];
  set_text(when, _(S_ALARM_WHEN), hist ? (a->active ? _(S_ACTIVE_SINCE) : _(S_LOGGED)) : _(S_SINCE), a->dd, a->mo,
           fmt_hm(hm, a->hh * 60 + a->mi));
  lv_obj_set_pos(when, 36, y + 20);
  lv_obj_t *code = mk_label(alarms->list, &font_f12, T.faint, "");
  set_text(code, "#%d", a->code);
  lv_obj_align(code, LV_ALIGN_TOP_RIGHT, -18, y + 12);
}

static void alarms_build(lv_obj_t *b, intptr_t arg) {
  bool hist = arg == ALARMS_HISTORY;
  ui_set_title(hist ? _(S_ALARM_HISTORY) : _(S_ACTIVE_ALARMS));
  ui_help(hist ? S_ALARM_HISTORY_HELP : S_ALARM_LIST_HELP);
  const alarm_t *src = hist ? M.history : M.alarms;
  alarms->n = hist ? M.n_history : M.n_alarms;
  if (!alarms->n) {
    mk_empty(b, T.ok, IC_CHECK_C, hist ? _(S_NO_ALARMS_LOGGED) : _(S_NO_ACTIVE_ALARMS), NULL);
    ui_hints(S_NONE, S_NONE, S_BACK, S_HELP);
    return;
  }
  clamp_sel(alarms->n, alarms->n > ALARM_ROWS ? alarms->n - ALARM_ROWS : 0);
  alarms->list = mk_group(mk_group(b, SCR_W, BODY_H), SCR_W, alarms->n * ALARM_ROW + 8);  // clipped by the outer one
  alarms->sel = mk_box(alarms->list, 6, 0, SCR_W - 12, ALARM_ROW - 4, T.surface, 8);
  for (int i = 0; i < alarms->n; i++)
    alarm_row(i, &src[i], hist);
  ui_hints(S_MOVE, S_NONE, S_BACK, S_HELP);
}

static void alarms_update(void) {
  if (!alarms->list)
    return;
  frame_t *f = ui_frame();
  keep_in_view(&f->top, f->sel, ALARM_ROWS);
  lv_obj_set_y(alarms->list, -f->top * ALARM_ROW);
  lv_obj_set_y(alarms->sel, 2 + f->sel * ALARM_ROW);
}

static bool alarms_key(int k) { return alarms->list && sel_key(k, alarms->n); }

const screen_t SCR_ALARMS = {.build = alarms_build, .update = alarms_update, .key = alarms_key};

// ---------------------------------------------------------------- message from the ESP
static void note_build(lv_obj_t *b, intptr_t arg) {
  (void) arg;
  bool alarm = M.note_level == NOTE_ALARM, warning = M.note_level == NOTE_WARNING;
  lv_color_t c = alarm ? T.err : warning ? T.warn : T.accent;
  str_t kind = alarm ? S_ALARM_CAPS : warning ? S_WARNING_CAPS : S_MESSAGE_CAPS;
  lv_obj_t *card = alert_card(b, 16, 22, 288, 152, c, 44, &icons_22, icon_glyph(M.note_icon), tr(kind));
  lv_obj_t *w = mk_label(card, &font_f12, T.dim, "");
  char hm[12];
  set_text(w, _(S_NOTE_FROM_HA), fmt_hm(hm, M.hh * 60 + M.mi));
  lv_obj_set_pos(w, 72, 30);
  mk_wrap(card, &font_f16, T.text, M.note, 14, 70, 258);
  ui_hints(S_NONE, S_CLOSE, S_CLOSE, S_NONE);
  ui_help(S_NOTE_HELP);
}

static bool note_key(int k) {
  if (k != KEY_OK)
    return false;
  ui_pop();
  return true;
}

const screen_t SCR_NOTE = {.flags = SF_OVERLAY, .build = note_build, .key = note_key};

// ---------------------------------------------------------------- help: the screen's text (arg) and the keys
static int help_item;  // item whose ESP help is shown when the text is S_ITEM_HELP

void ui_help_item(int item) {
  help_item = item;
  ui_help(S_ITEM_HELP);
}

// Shows an item's help once it arrives from the ESP. Each help on the stack has its own text label.
static void help_update(void) {
  frame_t *f = ui_frame();
  if (f->arg != S_ITEM_HELP)
    return;
  const char *h = link_item_help(help_item);
  lv_obj_t *text = lv_obj_get_child(lv_obj_get_child(f->root, 0), 0);  // the card's first child
  lv_label_set_text(text, !h ? _(S_ITEM_HELP) : h[0] ? h : _(S_NO_HELP));
}

static void help_build(lv_obj_t *b, intptr_t arg) {
  str_t text = (str_t) arg;
  lv_obj_t *card = mk_card(b, 16, 4, 288, BODY_H - 8, T.line, 1);
  // the card's first child, for help_update(). Up to HELP_LINES lines
  mk_wrap(card, &font_f14, T.text, tr(text ? text : S_NO_HELP), 14, 38, HELP_W);
  help_update();
  lv_obj_set_pos(mk_icon(card, &icons_22, T.accent, IC_HELP), 14, 9);
  lv_obj_set_pos(mk_label(card, &font_f16, T.text, _(S_HELP)), 44, 10);
  lv_obj_align(mk_wrap(card, &font_f12, T.dim, _(S_HELP_FOOTER), 0, 0, HELP_W), LV_ALIGN_BOTTOM_LEFT, 14, -10);
  ui_hints(S_NONE, S_CLOSE, S_CLOSE, S_NONE);
}

static bool help_key(int k) {
  if (k == KEY_OK || k == KEY_BACK || k == KEY_HELP)
    ui_pop();
  return true;  // swallow the other keys
}

const screen_t SCR_HELP = {.flags = SF_OVERLAY, .build = help_build, .update = help_update, .key = help_key};
