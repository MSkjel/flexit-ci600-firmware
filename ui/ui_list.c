// The main menu's grid (SCR_MENU) and the row lists (SCR_LIST).
#include "ui_menu.h"
#include <string.h>

// ---------------------------------------------------------------- main menu
#define GRID_W 149
#define GRID_H 44
#define GRID_GAP 6
#define GRID_ROWS 4                 // rows of entries on screen
#define GRID_SLOTS (GRID_ROWS * 2)  // entry widgets, reused as the grid scrolls

// Main menu state
typedef struct {
  lv_obj_t *box[GRID_SLOTS], *icon[GRID_SLOTS], *title[GRID_SLOTS], *status[GRID_SLOTS];
  int8_t shown[GRID_SLOTS];       // entry shown in each slot, -1 = none
  const mitem_t *item[GRID_MAX];  // entries available with the installed features, in order
  int n;                          // number of entries
} grid_t;
SCREEN_STATE(grid_t, grid);

static void menu_build(lv_obj_t *b, intptr_t arg) {
  (void) arg;
  ui_hints(S_MOVE, S_OPEN, S_BACK, S_HELP);
  for (unsigned i = 0; i < m_main.n; i++) {
    const mitem_t *it = &m_main.items[i];
    if (HAS_ALL(it->need) && (!it->shown || it->shown()))
      grid->item[grid->n++] = it;
  }
  int rows = (grid->n + 1) / 2;
  clamp_sel(grid->n, rows > GRID_ROWS ? rows - GRID_ROWS : 0);
  for (int k = 0; k < GRID_SLOTS && k < grid->n; k++) {
    lv_obj_t *t =
        mk_box(b, 8 + (k % 2) * (GRID_W + GRID_GAP), 4 + (k / 2) * (GRID_H + GRID_GAP), GRID_W, GRID_H, T.surface, 10);
    lv_obj_set_style_border_width(t, 2, 0);  // always 2 so the content doesn't move when selected
    grid->box[k] = t;
    grid->icon[k] = mk_disc(t, 5, 5, 30, T.surface2, &icons_16, T.text, "");
    grid->title[k] = mk_label(t, &font_f14, T.text, "");
    lv_obj_set_pos(grid->title[k], 44, 2);
    grid->status[k] = mk_label(t, &font_f12, T.dim, "");
    lv_obj_set_size(grid->status[k], GRID_W - 50, 17);
    label_dots(grid->status[k]);
    lv_obj_set_pos(grid->status[k], 44, 21);
    grid->shown[k] = -1;
  }
}

static void menu_update(void) {
  frame_t *f = ui_frame();
  keep_in_view(&f->top, f->sel / 2, GRID_ROWS);
  char buf[48];
  for (int k = 0; k < GRID_SLOTS && k < grid->n; k++) {
    int i = f->top * 2 + k;
    if (i >= grid->n) {
      lv_obj_add_flag(grid->box[k], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(grid->box[k], LV_OBJ_FLAG_HIDDEN);
    const mitem_t *it = grid->item[i];
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
    it->fmt(buf, sizeof buf);
    lv_label_set_text(grid->status[k], buf);
    lv_obj_set_style_text_color(grid->status[k], (it->flags & MF_ALARMS) && M.n_alarms ? T.err : T.dim, 0);
  }
  ui_help(grid->item[f->sel]->help);
}

static bool menu_key(int k) {
  if (k != KEY_OK)
    return sel_key(k, grid->n);
  ui_push(&SCR_LIST, (intptr_t) grid->item[ui_frame()->sel]->target);
  return true;
}

static const char *menu_selected(void) { return tr(grid->item[ui_frame()->sel]->title); }

const screen_t SCR_MENU = {
    .title = S_MENU, .build = menu_build, .update = menu_update, .key = menu_key, .selected = menu_selected};

// ---------------------------------------------------------------- a row's texts
const char *item_title(const mitem_t *it) {
  const item_t *s = item_of(it->val);
  return s ? s->label : tr(it->title);
}

const char *item_unit(const mitem_t *it) {
  const item_t *s = item_of(it->val);
  return s ? s->unit : tr(it->unit);
}

const char *opt_text(const mitem_t *it, int o) {
  if (it->fn && it->fn->text)
    return it->fn->text(o);
  return o >= 0 && o < it->n_opts ? tr(it->opts[o]) : NO_VALUE;  // a value from the bus can be out of range
}

bool shown_off(const mitem_t *it, int32_t v) { return (it->flags & MF_ZERO_OFF) && v == 0; }

const char *number_only(const mitem_t *it, int32_t v, char *buf, size_t n) {
  if (shown_off(it, v))
    return copy_text(buf, n, _(S_OFF));
  if (it->flags & (MF_TIME | MF_HOUR))
    return fmt_hm(buf, n, it->flags & MF_HOUR ? v * 60 : v);
  return fmt_dec(buf, n, v, it->dec);
}

// The number with its unit, or Off
static void number_text(const mitem_t *it, int32_t v, char *buf, size_t n) {
  if (v == ITEM_UNKNOWN && item_of(it->val)) {
    copy_text(buf, n, NO_VALUE);  // an item with no value
    return;
  }
  number_only(it, v, buf, n);
  if (!shown_off(it, v))
    add_unit(buf, n, item_unit(it));
}

int opt_used(const mitem_t *it, int o) { return it->fn && it->fn->used ? it->fn->used((val_t) it->val, o) : o; }

static void value_text(const mitem_t *it, char *buf, size_t n) {
  int32_t v = val_get(it->val);
  switch (it->type) {
    case MI_NUM:
      number_text(it, v, buf, n);
      break;
    case MI_ENUM:
      v = opt_used(it, v);
      copy_text(buf, n, opt_text(it, v));
      break;
    case MI_INFO:
      if (it->fmt)
        it->fmt(buf, n);
      else if (item_of(it->val))
        item_text(buf, n, item_of(it->val), true);  // an ESP sensor, or a read-only toggle as On or Off
      else
        number_text(it, v, buf, n);
      break;
    default:
      buf[0] = 0;
  }
}

// An ESP item's help is asked for when its row is selected
void row_help(const mitem_t *it) {
  if (it->help == S_ITEM_HELP)
    ui_help_item(it->val - V_ITEM_FIRST);
  else
    ui_help(it->help);
}

// ---------------------------------------------------------------- row lists (SCR_LIST)
#define ROW_H 34
#define ROWS 5  // rows on screen, their widgets are reused as the list scrolls
#define MAX_ROWS 24
#define ROW_END (SCR_W - 18)  // where a row's switch and arrow end
#define SWITCH_W 36           // toggle switch, its knob and the gap around the knob
#define KNOB 14
#define KNOB_GAP 2
#define SWITCH_H (KNOB + 2 * KNOB_GAP)
#define ARROW_X (ROW_END - 16)  // a 16 px icon
#define VALUE_W 140             // the value, right-aligned in its label
#define VALUE_H 19
#define VALUE_X (SCR_W - 20 - VALUE_W)

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

// An ESP toggle with no value yet is off
static bool is_on(const mitem_t *it) {
  const item_t *s = item_of(it->val);
  return s ? item_on(s) : val_get(it->val) != 0;
}

static void toggle_draw(lv_obj_t *sw, bool on) {
  lv_obj_set_style_bg_color(sw, on ? T.accent : T.surface2, 0);
  lv_obj_t *knob = lv_obj_get_child(sw, 0);
  lv_obj_set_x(knob, on ? SWITCH_W - KNOB - KNOB_GAP : KNOB_GAP);
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
    if (!it || !HAS_ALL(it->need) || HAS_ANY(it->without) || !val_known(it->val) || (it->shown && !it->shown()))
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
    lv_obj_t *sw = list->val[k] = mk_box(s, ROW_END - SWITCH_W, 8, SWITCH_W, SWITCH_H, T.surface2, SWITCH_H / 2);
    mk_box(sw, KNOB_GAP, KNOB_GAP, KNOB, KNOB, T.dim, KNOB / 2);
  } else if (it->type == MI_SUB || it->type == MI_SCREEN || it->type == MI_ACTION || it->type == MI_PRESS) {
    const char *ic = it->type == MI_PRESS ? IC_PLAY : it->type == MI_ACTION ? IC_DOTS : IC_RIGHT;
    lv_obj_set_pos(mk_icon(s, &icons_16, T.faint, ic), ARROW_X, 8);
  } else {
    lv_obj_t *v = list->val[k] =
        mk_text(s, &font_f14, it->type == MI_INFO ? T.dim : T.accent, "", VALUE_W, LV_TEXT_ALIGN_RIGHT);
    label_dots(v);
    lv_obj_set_height(v, VALUE_H);
    lv_obj_set_pos(v, VALUE_X, 8);
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
  list->m = (const menu_t *) arg;
  ui_set_title(tr(list->m->title));  // prepare() may set another
  if (list->m->prepare)
    list->m->prepare();
  if (list->m->row && !M.esp_online) {  // an ESP menu, stale while the ESP is away
    mk_empty_screen(b, T.faint, IC_WIFI_OFF, _(S_ESP_NOT_CONNECTED),
                    list->m == &m_comfort ? _(S_COMFORT_NEEDS_ESP) : _(S_SMART_NEEDS_ESP));
    return;
  }
  list_rows();
  if (!list->n) {  // an ESP menu with no items yet
    mk_empty_screen(b, T.faint, IC_HASS, _(S_NOTHING_HERE_YET), _(S_SMART_MENU_EMPTY));
    return;
  }
  clamp_sel(list->n, list->n > ROWS ? list->n - ROWS : 0);
  frame_t *f = ui_frame();
  while (f->sel < list->n - 1 && !selectable(f->sel))
    f->sel++;

  list->sel_bg = mk_box(b, 6, 0, SCR_W - 12, ROW_H - 2, T.surface, 8);  // created first so it's behind the rows
  mk_box(list->sel_bg, 0, 7, 3, ROW_H - 16, T.accent, 2);
  for (int k = 0; k < slots(); k++) {
    list->slot[k] = mk_group(b, SCR_W, ROW_H);
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

// Scrolls so the selected row and the header above it are in view, and moves the highlight and scroll bar
static void list_scroll(frame_t *f) {
  keep_in_view(&f->top, f->sel, ROWS);
  if (f->top > 0 && f->sel - 1 < f->top && row_type(f->sel - 1) == MI_HEAD)
    f->top--;  // keep its header in view
  place_rows(f->top);
  lv_obj_set_y(list->sel_bg, (f->sel - f->top) * ROW_H + 1);
  if (list->scroll) {
    int track = ROWS * ROW_H - 8, len = lv_obj_get_height(list->scroll);
    lv_obj_set_y(list->scroll, (track - len) * f->top / (list->n - ROWS));
  }
}

// The switches and values of the rows shown
static void list_values(void) {
  char buf[48];
  mitem_t b;
  for (int k = 0; k < slots(); k++) {
    if (!list->val[k])
      continue;
    const mitem_t *it = row(list->shown[k], &b);
    if (it->type == MI_TOGGLE)
      toggle_draw(list->val[k], is_on(it));
    else {
      value_text(it, buf, sizeof buf);
      lv_label_set_text(list->val[k], buf);
    }
  }
}

// What OK does on each kind of row
static const str_t OK_HINT[] = {[MI_SUB] = S_OPEN,   [MI_NUM] = S_CHANGE, [MI_ENUM] = S_CHANGE, [MI_TOGGLE] = S_SWITCH,
                                [MI_ACTION] = S_RUN, [MI_INFO] = S_NONE,  [MI_SCREEN] = S_OPEN, [MI_PRESS] = S_PRESS};

static void list_update(void) {
  frame_t *f = ui_frame();
  if (!list->n)
    return;
  list_scroll(f);
  list_values();
  mitem_t b;
  const mitem_t *it = row(f->sel, &b);
  const char *h = it->help == S_ITEM_HELP ? link_item_help(it->val - V_ITEM_FIRST) : tr(it->help);
  lv_label_set_text(list->help, h ? h : "");
  row_help(it);
  ui_hints(S_MOVE, read_only(it) ? S_NONE : OK_HINT[it->type], S_BACK, S_HELP);
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
    case MI_TOGGLE: {
      if (read_only(it))
        break;
      bool on = !is_on(it);
      val_set(it->val, on);
      if (on && !is_on(it) && it->done)
        ui_toast(IC_ALERT, it->done);  // refused, see val_def()
      else
        ui_toast(val_get(it->val) ? IC_TOGGLE_ON : IC_TOGGLE_OFF, val_get(it->val) ? S_ON : S_OFF);
      break;
    }
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
