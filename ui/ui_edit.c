// Value editor and action confirm box over a row list (SCR_EDIT), and the box for a setting on trial (SCR_TRIAL).
#include "ui_menu.h"
#include <string.h>

#define OPT_ROWS 5  // enum options shown at a time, longer lists scroll
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
#define EDIT_TEXT_W (EDIT_W - 2 * CARD_PAD)
#define EDIT_TOP 40           // where the card's content starts, under its title
#define OPT_H 24              // option row height
#define FASTER_HELD_MS 1500   // UP or DOWN held this long steps 5 at a time
#define FASTEST_HELD_MS 4000  // and this long, 10

static int opt_rows(void) { return ed.n < OPT_ROWS ? ed.n : OPT_ROWS; }  // options shown

// Fills ed.offer with the usable options and finds ed.v among them. A new editor scrolls it to the middle.
static void offer_options(const mitem_t *it, bool rebuild) {
  int now = opt_used(it, ed.v);
  uint16_t opts[MAX_OPTS];
  int n = 0;
  if (it->fn && it->fn->list)
    n = it->fn->list(opts);
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
  int rows = opt_rows(), first = rebuild ? ed.first : ed.pos - rows / 2;
  ed.first = (int16_t) (first < 0 ? 0 : first > ed.n - rows ? ed.n - rows : first);
}

static void edit_number(lv_obj_t *card, const mitem_t *it) {
  ed.val = mk_label(card, &font_n40, T.accent, "");
  ed.unit = mk_label(card, &font_f16, T.dim, "");
  ed.bar = mk_bar(card, 18, 104, EDIT_TEXT_W - 8, 6, 0, ed.max - ed.min, T.accent);
  char a[16];
  lv_obj_set_pos(mk_label(card, &font_f12, T.faint, number_only(it, ed.min, a, sizeof a)), 18, 116);
  lv_obj_align(mk_label(card, &font_f12, T.faint, number_only(it, ed.max, a, sizeof a)), LV_ALIGN_TOP_RIGHT, -18, 116);
  ui_hints(S_ADJUST, S_SAVE, S_CANCEL, S_HELP);
}

static void edit_options(lv_obj_t *card) {
  ed.opt_bg = mk_box(card, 8, 0, EDIT_W - 16, OPT_H - 2, T.accent_soft, 6);
  for (int i = 0; i < opt_rows(); i++) {
    ed.opts[i] = mk_label(card, &font_f14, T.text, "");
    lv_obj_set_pos(ed.opts[i], 38, EDIT_TOP + i * OPT_H);
  }
  ed.check = mk_icon(card, &icons_16, T.accent, IC_CHECK);  // marks the current setting
  ui_hints(S_CHOOSE, S_SAVE, S_CANCEL, S_HELP);
}

static const char *confirm_warning(const mitem_t *it) {
  return tr(it->flags & MF_DANGER ? S_HOLD_OK_WARNING : S_PRESS_OK);
}

// Height of a box with question q and warning w under it, at *warning_y
static int confirm_height(const char *q, const char *w, int *warning_y) {
  lv_point_t qs, ws;
  lv_txt_get_size(&qs, q, &font_f14, 0, 0, EDIT_TEXT_W, LV_TEXT_FLAG_NONE);
  lv_txt_get_size(&ws, w, &font_f12, 0, 0, EDIT_TEXT_W, LV_TEXT_FLAG_NONE);
  *warning_y = EDIT_TOP + qs.y + 10;
  return *warning_y + ws.y + 14;
}

// Returns the warning's label
static lv_obj_t *confirm_texts(lv_obj_t *card, const char *q, const char *w, int warning_y) {
  mk_wrap(card, &font_f14, T.text, q, CARD_PAD, EDIT_TOP, EDIT_TEXT_W);
  return mk_wrap(card, &font_f12, T.warn, w, CARD_PAD, warning_y, EDIT_TEXT_W);
}

// Action confirm box. The help text says what it does, then how to confirm.
static void edit_confirm(lv_obj_t *card, const mitem_t *it, int warning_y) {
  confirm_texts(card, tr(it->help), confirm_warning(it), warning_y);
  ui_hints(S_NONE, it->flags & MF_DANGER ? S_HOLD_TO_CONFIRM : S_CONFIRM, S_CANCEL, S_NONE);
}

// Centred, h high, with an icon and a title
static lv_obj_t *edit_card(lv_obj_t *b, int h, const char *icon, const char *title) {
  if (h > BODY_H - 8)
    h = BODY_H - 8;
  lv_obj_t *card = mk_card(b, (SCR_W - EDIT_W) / 2, (BODY_H - h) / 2, EDIT_W, h, T.line, 1);
  lv_obj_set_pos(mk_icon(card, &icons_16, T.accent, icon), CARD_PAD, 12);
  lv_obj_set_pos(mk_label(card, &font_f16, T.text, title), 38, 9);
  return card;
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
  int warning_y = 0, h = 150;
  if (it->type == MI_ENUM)
    h = EDIT_TOP + 6 + opt_rows() * OPT_H;
  else if (it->type == MI_ACTION)
    h = confirm_height(tr(it->help), confirm_warning(it), &warning_y);
  lv_obj_t *card = edit_card(b, h, it->icon ? it->icon : IC_TUNE, item_title(it));
  row_help(it);
  if (it->type == MI_NUM)
    edit_number(card, it);
  else if (it->type == MI_ENUM)
    edit_options(card);
  else
    edit_confirm(card, it, warning_y);
}

static void edit_update(void) {
  const mitem_t *it = ed.it;
  char a[16];
  if (it->type == MI_NUM) {
    bool off = shown_off(it, ed.v);  // Off in a text font, the number font has no letters
    const lv_font_t *f = off ? &font_f28 : &font_n40;
    const char *unit = off ? "" : item_unit(it);
    st_font(ed.val, f);
    lv_label_set_text(ed.val, number_only(it, ed.v, a, sizeof a));
    label_text(ed.unit, unit);
    int y = 38 + (font_n40.line_height - font_n40.base_line) - (f->line_height - f->base_line);  // same baseline
    lv_obj_align(ed.val, LV_ALIGN_TOP_MID, -lv_txt_get_width(unit, strlen(unit), &font_f16, 0, 0) / 2 - 2, y);
    lv_obj_align_to(ed.unit, ed.val, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -3 - font_n40.base_line);  // near its baseline
    lv_bar_set_value(ed.bar, ed.v - ed.min, LV_ANIM_OFF);
  } else if (it->type == MI_ENUM) {
    int rows = opt_rows(), used = opt_used(it, val_get(it->val)), now = -1;
    for (int i = 0; i < ed.n; i++)
      if (ed.offer[i] == used)
        now = i;
    keep_in_view(&ed.first, ed.pos, rows);
    for (int i = 0; i < rows; i++) {
      int o = ed.offer[ed.first + i];
      lv_label_set_text(ed.opts[i], opt_text(it, o));
      st_text(ed.opts[i], o == ed.v ? T.accent : T.text);
    }
    lv_obj_set_y(ed.opt_bg, EDIT_TOP - 1 + (ed.pos - ed.first) * OPT_H);
    if (now >= ed.first && now < ed.first + rows) {
      lv_obj_clear_flag(ed.check, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(ed.check, 14, EDIT_TOP + 2 + (now - ed.first) * OPT_H);
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
      uint32_t held = input_held_ms();
      int step = it->step ? it->step : 1, mul = held > FASTEST_HELD_MS ? 10 : held > FASTER_HELD_MS ? 5 : 1;
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

// ---------------------------------------------------------------- a setting on trial
// The countdown runs on under other screens, and after HOME closed the box.
#define TRIAL_MS 20000
static struct {
  val_t v;            // on trial, V_NONE if none
  uint32_t end;       // lv_tick_get() when it goes off
  lv_timer_t *timer;  // every second until then
  lv_obj_t *left;     // the time left
} trial;

static void trial_left(char *buf, size_t n) {
  int32_t ms = (int32_t) (trial.end - lv_tick_get());
  int s = ms > 0 ? (int) ((ms + 999) / 1000) : 0;
  if (trial.v == V_CPU_SPEED)
    sprint(buf, n, _(S_BACK_TO_72_IN), s);
  else
    sprint(buf, n, _(S_FAST_OFF_IN), s);
}

static void trial_answer(bool keep) {
  val_t v = trial.v;
  lv_timer_del(trial.timer);
  trial.v = V_NONE;
  ui_remove_overlay(&SCR_TRIAL);
  plat_trial_end(keep);
  if (!keep)
    val_set(v, 0);
  ui_toast(keep ? IC_CHECK : IC_TOGGLE_OFF, keep ? S_SAVED : S_OFF);
}

static void trial_tick(lv_timer_t *t) {
  (void) t;
  if ((int32_t) (lv_tick_get() - trial.end) >= 0)
    trial_answer(false);
  else
    ui_refresh();  // the countdown, on the second
}

void ui_trial(int v) {
  trial.v = (val_t) v;
  trial.end = lv_tick_get() + TRIAL_MS;
  trial.timer = lv_timer_create(trial_tick, 1000, NULL);
  input_activity();
  ui_push(&SCR_TRIAL, 0);
}

static void trial_build(lv_obj_t *b, intptr_t arg) {
  (void) arg;
  bool cpu = trial.v == V_CPU_SPEED;
  char left[48], title[32];
  trial_left(left, sizeof left);
  sprint(title, sizeof title, _(S_KEEP_MHZ), CPU_MHZ[M.cpu_speed]);  // the speed on trial, see hw/main.c
  int warning_y, h = confirm_height(_(S_KEEP_QUESTION), left, &warning_y);
  lv_obj_t *card = edit_card(b, h, cpu ? IC_CHIP : IC_MONITOR, cpu ? title : _(S_KEEP_FAST_DISPLAY));
  trial.left = confirm_texts(card, _(S_KEEP_QUESTION), left, warning_y);
  ui_hints(S_NONE, S_KEEP, S_CANCEL, S_HELP);
  ui_help(cpu ? S_PROCESSOR_SPEED_HELP : S_FAST_DISPLAY_HELP);
}

static void trial_update(void) {
  char left[48];
  trial_left(left, sizeof left);
  lv_label_set_text(trial.left, left);
}

static bool trial_key(int k) {
  if (k == KEY_OK || k == KEY_BACK)
    trial_answer(k == KEY_OK);
  return k != KEY_HELP;  // ? shows the help, the other keys do nothing
}

const screen_t SCR_TRIAL = {.flags = SF_OVERLAY, .build = trial_build, .update = trial_update, .key = trial_key};
