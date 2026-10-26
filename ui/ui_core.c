// Screen stack, status bar, key hints and themes.
#include "ui_int.h"
#include <string.h>

theme_t T;
_Alignas(8) uint8_t ui_screen_mem[UI_SCREEN_MEM];
const str_t THEME_NAMES[N_THEMES] = {S_MIDNIGHT, S_DAYLIGHT, S_GRAPHITE};
const str_t MODE_NAMES[MODE_COUNT] = {
    [MODE_STOP] = S_STOP, [MODE_MIN] = S_MIN, [MODE_NORMAL] = S_NORMAL, [MODE_MAX] = S_MAX};
const str_t DAY_NAMES[7] = {S_MONDAY, S_TUESDAY, S_WEDNESDAY, S_THURSDAY, S_FRIDAY, S_SATURDAY, S_SUNDAY};

#define X(name) uint32_t name;
typedef struct {
  THEME_COLOURS(X)
} theme_hex_t;
#undef X
static const theme_hex_t THEMES[N_THEMES] = {
    {.bg = 0x0D1317,
     .surface = 0x162026,
     .surface2 = 0x1F2C34,
     .line = 0x2A3A44,
     .text = 0xE9F1F2,
     .dim = 0x8EA2AA,
     .faint = 0x5A6C74,
     .accent = 0x2EC4B6,
     .accent_soft = 0x123B3A,
     .on_accent = 0x04201E,
     .warm = 0xFF9F43,
     .cold = 0x5DADE2,
     .ok = 0x56C271,
     .warn = 0xF5B841,
     .err = 0xEF5B5B},  // Midnight
    {.bg = 0xECF1F3,
     .surface = 0xFFFFFF,
     .surface2 = 0xE1E8EB,
     .line = 0xCFD9DD,
     .text = 0x13202A,
     .dim = 0x56656D,
     .faint = 0x8E9CA3,
     .accent = 0x0E9384,
     .accent_soft = 0xD3EEEB,
     .on_accent = 0xFFFFFF,
     .warm = 0xE07B24,
     .cold = 0x2F80C0,
     .ok = 0x2E9E5B,
     .warn = 0xC98A10,
     .err = 0xD64545},  // Daylight
    {.bg = 0x000000,
     .surface = 0x131313,
     .surface2 = 0x242424,
     .line = 0x333333,
     .text = 0xFFFFFF,
     .dim = 0xB0B0B0,
     .faint = 0x747474,
     .accent = 0xFFC233,
     .accent_soft = 0x3A2E0E,
     .on_accent = 0x1A1200,
     .warm = 0xFF7A1A,
     .cold = 0x59C3FF,
     .ok = 0x4CD964,
     .warn = 0xFFC233,
     .err = 0xFF453A},  // Graphite
};

static uint8_t shown_theme;  // theme in use, to notice a day/night switch

static void apply_theme(void) {
  shown_theme = theme_now();
  const theme_hex_t *h = &THEMES[shown_theme];
#define X(name) T.name = lv_color_hex(h->name);
  THEME_COLOURS(X)
#undef X
}

// ---------------------------------------------------------------- stack
#define STACK_MAX 10
#define TOAST_MS 1800
static frame_t stack[STACK_MAX];
static int depth;
static int building = -1;  // frame being built or updated, -1 if none
static lv_obj_t *bar, *body, *hintbar, *clock_lbl, *title_lbl, *status_lbl, *dots;
static bool ready;            // ui_init has run
static bool rebuild_due;      // the layout changed, rebuild_check() rebuilds before the next draw
static bool refresh_due;      // a value changed, rebuild_check() refreshes before the next draw
static str_t shown_hints[4];  // hints currently drawn
static lv_obj_t *toast;       // shown over the title for TOAST_MS, NULL if none
static uint32_t toast_end;    // lv_tick_get() time the toast goes
static void draw_hints(const str_t *h);

frame_t *ui_frame(void) { return &stack[building >= 0 ? building : depth - 1]; }

const screen_t *ui_top(void) { return depth ? stack[depth - 1].s : NULL; }

static int base_of(int i) {  // nearest non-overlay frame at or below i
  while (i > 0 && (stack[i].s->flags & SF_OVERLAY))
    i--;
  return i;
}

static void apply_bars(void) {
  const char *title = NULL;
  for (int i = depth - 1; i >= 0 && !title; i--)
    title = stack[i].title ? stack[i].title : stack[i].s->title ? tr(stack[i].s->title) : NULL;
  bool nobars = stack[base_of(depth - 1)].s->flags & SF_NOBARS;
  if (nobars) {
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hintbar, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(title_lbl, title ? title : "");
  if (title)
    lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
  static const str_t NONE[4];
  const str_t *h = NONE;  // from the topmost frame that has any
  for (int i = depth - 1; i >= 0 && h == NONE; i--)
    if (memcmp(stack[i].hints, NONE, sizeof NONE))
      h = stack[i].hints;
  if (memcmp(h, shown_hints, sizeof shown_hints))
    draw_hints(h);
  if (M.key_hints && h != NONE)
    lv_obj_clear_flag(hintbar, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(hintbar, LV_OBJ_FLAG_HIDDEN);
}

static void build(int i) {
  frame_t *f = &stack[i];
  f->title = NULL;
  memset(f->hints, 0, sizeof f->hints);
  f->help = S_NONE;
  building = i;
  if (f->s->flags & SF_OVERLAY) {
    f->root = mk_box(lv_layer_top(), 0, BODY_Y, SCR_W, BODY_H, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(f->root, LV_OPA_70, 0);
    f->s->build(f->root, f->arg);
  } else {
    f->root = NULL;
    lv_obj_clean(body);
    memset(ui_screen_mem, 0, sizeof ui_screen_mem);
    bool full = f->s->flags & SF_NOBARS;
    lv_obj_set_pos(body, 0, full ? 0 : BODY_Y);
    lv_obj_set_size(body, SCR_W, full ? SCR_H : BODY_H);
    lv_obj_set_style_bg_color(lv_scr_act(), T.bg, 0);
    lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
    f->s->build(body, f->arg);
  }
  f->built = true;
  building = -1;
}

static void drop_root(frame_t *f) {  // deletes an overlay's widgets
  if (f->root) {
    lv_obj_del(f->root);
    f->root = NULL;
  }
}

static void drop_overlays(void) {
  for (int i = 0; i < depth; i++)
    drop_root(&stack[i]);
}

// After the stack changed. ui_refresh() would put up the bars too, but putting them up before it fills in the
// values leaves LVGL's heap in fewer pieces.
static void stack_changed(void) {
  apply_bars();
  ui_refresh();
}

// Rebuilds the top full screen and the overlays above it
static void rebuild_visible(void) {
  drop_overlays();
  for (int i = base_of(depth - 1); i < depth; i++)
    build(i);
  stack_changed();
}

void ui_push(const screen_t *s, intptr_t arg) {
  if (depth == STACK_MAX)
    return;
  stack[depth++] = (frame_t) {.s = s, .arg = arg};
  if (s->flags & SF_OVERLAY) {
    build(depth - 1);
    stack_changed();
  } else {
    rebuild_visible();
  }
}

void ui_pop(void) {
  if (depth <= 1)
    return;
  frame_t *f = &stack[--depth];
  if (f->root) {
    drop_root(f);
    stack_changed();
  } else {
    rebuild_visible();
  }
}

void ui_replace(const screen_t *s, intptr_t arg) {
  drop_root(&stack[depth - 1]);
  stack[depth - 1] = (frame_t) {.s = s, .arg = arg};
  rebuild_visible();
}

static frame_t *frame_of(const screen_t *s) {  // NULL if s isn't open
  for (int i = 0; i < depth; i++)
    if (stack[i].s == s)
      return &stack[i];
  return NULL;
}

// Takes overlay s and any help over it off the stack wherever they are, the rest stay built
void ui_remove_overlay(const screen_t *s) {
  int n = 0;
  bool gone = false;
  for (int i = 0; i < depth; i++) {
    gone = stack[i].s == s || (gone && stack[i].s == &SCR_HELP);
    if (!gone)
      stack[n++] = stack[i];
    else
      drop_root(&stack[i]);
  }
  if (n == depth)
    return;
  depth = n;
  stack_changed();
}

void ui_go_home(void) {
  drop_overlays();
  depth = 1;
  rebuild_visible();
}

void ui_rebuild(void) {
  rebuild_due = false;
  apply_theme();
  lv_obj_set_style_bg_color(lv_scr_act(), T.bg, 0);
  lv_obj_set_style_bg_color(bar, T.bg, 0);
  lv_obj_set_style_text_color(clock_lbl, T.text, 0);
  lv_obj_set_style_text_color(title_lbl, T.dim, 0);
  lv_obj_set_style_bg_color(hintbar, T.bg, 0);
  lv_obj_set_style_border_color(hintbar, T.line, 0);
  memset(shown_hints, 0xFF, sizeof shown_hints);  // force a redraw
  rebuild_visible();
}

void ui_set_title(const char *t) {
  ui_frame()->title = t;
  if (building < 0)
    apply_bars();
}

void ui_help(str_t text) { ui_frame()->help = text; }

const char *ui_selected(void) { return ui_top() && ui_top()->selected ? ui_top()->selected() : NULL; }

const char *ui_title(void) { return lv_label_get_text(title_lbl); }

void ui_close_after(uint32_t ms) { ui_frame()->close_at = (lv_tick_get() + ms) | 1; }

// Closes the top screen once its ui_close_after() time is up, and the toast once its time is up
static void close_check(lv_timer_t *t) {
  (void) t;
  frame_t *f = &stack[depth - 1];
  if (f->close_at && (int32_t) (lv_tick_get() - f->close_at) >= 0)
    ui_pop();
  if (toast && (int32_t) (lv_tick_get() - toast_end) >= 0) {
    lv_obj_del(toast);
    toast = NULL;
  }
}

// While a whole new picture comes in under the screensaver the LCD keeps the old one. Nothing goes to it until the
// transfer is over, another screen is up or HOLD_MS has passed.
#define HOLD_MS 15000
static uint32_t held_since;  // lv_tick_get() | 1, 0 = not held

void ui_picture_coming(void) {
  if (saver_picture())
    held_since = lv_tick_get() | 1;
}

bool ui_held(void) { return held_since != 0; }

static void hold_check(void) {
  if (held_since && (ui_top() != &SCR_SAVER || pic_state() != PIC_RECEIVING || lv_tick_get() - held_since >= HOLD_MS)) {
    held_since = 0;
    lv_obj_invalidate(lv_scr_act());  // what was drawn meanwhile never got there
  }
}

// The ESP sends all its items at once after a HELLO. Rebuilding for each one tripped the watchdog, so we
// rebuild at most once per loop.
static void rebuild_check(lv_timer_t *t) {
  (void) t;
  if (rebuild_due)
    ui_rebuild();
  else if (refresh_due)
    ui_refresh();
  hold_check();
}

// ---------------------------------------------------------------- bars
void ui_hints(str_t ud, str_t ok, str_t back, str_t help) {
  str_t *h = ui_frame()->hints;
  h[0] = ud;
  h[1] = ok;
  h[2] = back;
  h[3] = help;
  if (building < 0)
    apply_bars();
}

// Each hint is the key's arrows or its name on a small plate, then what it does
static void draw_hints(const str_t *h) {
  static const char *const ARROWS[4] = {IC_UP IC_DOWN, NULL, IC_LEFT, NULL}, *const CAPS[4] = {NULL, "OK", NULL, "?"};
  memcpy(shown_hints, h, sizeof shown_hints);
  lv_obj_clean(hintbar);
  for (int k = 0; k < 4; k++) {
    if (!h[k])
      continue;
    if (ARROWS[k]) {
      mk_icon(hintbar, &icons_16, T.text, ARROWS[k]);
    } else {
      lv_obj_t *cap = mk_label(hintbar, &font_f12, T.bg, CAPS[k]);
      lv_obj_set_style_bg_color(cap, T.dim, 0);
      lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(cap, 3, 0);
      lv_obj_set_style_pad_hor(cap, 4, 0);
    }
    lv_obj_t *t = mk_label(hintbar, &font_f12, T.dim, tr(h[k]));
    lv_obj_set_style_pad_right(t, 8, 0);
  }
}

void ui_view_dots(int n, int cur) {
  lv_obj_clean(dots);
  for (int i = 0; i < n; i++)
    mk_box(dots, 0, 0, i == cur ? 14 : 6, 6, i == cur ? T.accent : T.surface2, 3);
  if (n > 1)
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(title_lbl, "");
}

// Appends a coloured icon to the status line if it fits, returns the new length
static size_t add_status(char *buf, size_t size, size_t n, lv_color_t c, const char *ic) {
  int w = lv_snprintf(buf + n, size - n, "#%06x %s#", (unsigned) ui_hex(c), ic);
  if (w < 0 || (size_t) w >= size - n) {
    buf[n] = 0;
    return n;
  }
  return n + (size_t) w;
}

static void update_status(void) {
  char buf[128];
  lv_label_set_text(clock_lbl, fmt_hm(buf, sizeof buf, M.hh * 60 + M.mi));

  // status icons. The label is right-aligned so the row grows to the left.
  size_t n = 0;
  buf[0] = 0;
#define ADD(col, ic) n = add_status(buf, sizeof buf, n, col, ic)
  if (M.bus_offline)
    ADD(T.err, IC_LAN);
  if (M.key_lock)
    ADD(T.dim, IC_LOCK);
  if (M.boost_left)
    ADD(T.warn, IC_TIMER);
  if (M.week_program)
    ADD(T.dim, IC_CAL_CLOCK);
  if (M.filter_hours >= FILTER_INTERVAL_H)
    ADD(T.warn, IC_FILTER);
  if (M.n_alarms)
    ADD(T.err, IC_BELL_RING);
  if (HAS(FEAT_ESP))
    ADD(M.esp_online ? T.dim : T.err, M.esp_online ? IC_WIFI : IC_WIFI_OFF);
#undef ADD
  lv_label_set_text(status_lbl, buf);
}

// One at a time, a new one replaces the one shown, so fast key presses can't use up the heap
void ui_toast_text(const char *icon, const char *text) {
  if (toast)
    lv_obj_del(toast);
  lv_obj_t *t = toast = mk_pill(lv_layer_top(), LV_SIZE_CONTENT, T.text, 12, 5, 6);
  if (icon)
    mk_icon(t, &icons_16, T.bg, icon);
  mk_label(t, &font_f14, T.bg, text);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);  // covers the title briefly rather than the content
  toast_end = lv_tick_get() + TOAST_MS;
}

void ui_toast(const char *icon, str_t msg) { ui_toast_text(icon, tr(msg)); }

void ui_key_lock_notice(bool toggled) {
  if (toggled)
    ui_toast(M.key_lock ? IC_LOCK : IC_UNLOCK, M.key_lock ? S_KEYS_LOCKED : S_KEYS_UNLOCKED);
  else
    ui_toast(IC_LOCK, S_KEYS_LOCKED_HOW);
}

// ---------------------------------------------------------------- input and ticks
void ui_wake(void) {
  if (stack[depth - 1].s == &SCR_SAVER)
    ui_pop();
}

void ui_idle(void) {
  bool unread = false;  // an open message, alarm pop-up or trial, going home would drop it
  for (int i = 0; i < depth; i++)
    unread |= stack[i].s == &SCR_NOTE || stack[i].s == &SCR_ALARM_POPUP || stack[i].s == &SCR_TRIAL;
  if (M.go_home && !unread)  // Display > Home screen > Back to start view
    ui_show_view(M.home_view);
  if (M.screensaver)
    ui_push(&SCR_SAVER, 0);
}

void ui_key(int key) {
  frame_t *top = &stack[depth - 1];
  if (key == KEY_HOME) {
    ui_go_home();
    return;
  }
  if (top->s->key && top->s->key(key))
    return;
  if (key == KEY_BACK && depth > 1)
    ui_pop();
  else if (key == KEY_HELP && top->s != &SCR_HELP)
    ui_push(&SCR_HELP, top->help);
}

int ui_ok_led(uint32_t ms) {
  const screen_t *s = ui_top();
  return s && s->ok_led ? s->ok_led(ms) : -1;
}

void ui_refresh(void) {
  if (!ready || rebuild_due)
    return;  // the rebuild refreshes
  refresh_due = false;
  update_status();
  for (int i = base_of(depth - 1); i < depth; i++)
    if (stack[i].s->update) {
      int b = building;
      building = i;
      stack[i].s->update();
      building = b;
    }
  if (building < 0)
    apply_bars();
}

void ui_second(void) {
  if (theme_now() != shown_theme)
    ui_rebuild();  // night mode started or ended and has its own theme
  int code = alarm_new_pops();
  if (code) {
    frame_t *f = frame_of(&SCR_ALARM_POPUP);
    const alarm_t *shown = f ? alarm_find(f->arg) : NULL;
    if (shown && shown < alarm_find(code))
      code = shown->code;                 // the one shown is more serious and stays
    ui_remove_overlay(&SCR_ALARM_POPUP);  // one pop-up, on top, even if it was under the screensaver
    input_activity();
    ui_push(&SCR_ALARM_POPUP, code);
  }
  if (M.note_new) {
    M.note_new = false;
    ui_remove_overlay(&SCR_NOTE);  // the new message replaces the one shown
    input_activity();
    ui_push(&SCR_NOTE, 0);
  }
  ui_refresh();
}

// VF_LAYOUT values rebuild the screen, the rest just refresh it
void ui_value_changed(val_t v) {
  if (!ready)
    return;  // the model loads saved settings and cached bus data before the UI exists
  if (val_def(v).flags & VF_LAYOUT)
    rebuild_due = true;
  else
    refresh_due = true;
}

void ui_layout_changed(void) {
  if (ready)
    rebuild_due = true;
}

// Rebuilds the alarm list and pop-up in rebuild_check() if they're open, also under a help or a message.
// A pop-up whose alarm is gone switches to the next alarm that pops up, or closes
void ui_alarms_changed(void) {
  frame_t *f = frame_of(&SCR_ALARM_POPUP);
  if (f && !alarm_find(f->arg)) {
    f->arg = 0;
    for (int i = 0; i < M.n_alarms && !f->arg; i++)
      if (alarm_pops(M.alarms[i].code))
        f->arg = M.alarms[i].code;
    if (!f->arg)
      ui_remove_overlay(&SCR_ALARM_POPUP);
  }
  for (int i = 0; i < depth; i++)
    if (stack[i].s == &SCR_ALARMS || stack[i].s == &SCR_ALARM_POPUP)
      rebuild_due = true;
}

void ui_show_view(int view) {
  stack[0] = (frame_t) {.s = &SCR_HOME, .arg = view};
  ui_go_home();
}

// IC_* glyph for each ESP link icon id
static const char *const ICON_GLYPHS[] = {
#define X(n) IC_##n,
    PLINK_ICONS(X)
#undef X
};

const char *icon_glyph(uint8_t id) { return id < LEN(ICON_GLYPHS) ? ICON_GLYPHS[id] : IC_CIRCLE; }

void ui_init(void) {
  anim_init();
  apply_theme();
  lv_obj_t *scr = lv_scr_act();
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, T.bg, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  body = mk_group(scr, SCR_W, BODY_H);
  bar = mk_box(scr, 0, 0, SCR_W, BAR_H, T.bg, 0);
  clock_lbl = mk_label(bar, &font_f16, T.text, "");
  lv_obj_align(clock_lbl, LV_ALIGN_LEFT_MID, 10, 0);
  title_lbl = mk_label(bar, &font_f14, T.dim, "");
  lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, 0);
  status_lbl = mk_icon(bar, &icons_16, T.dim, "");
  lv_label_set_recolor(status_lbl, true);
  lv_obj_set_style_text_letter_space(status_lbl, 6, 0);
  lv_obj_align(status_lbl, LV_ALIGN_RIGHT_MID, -6, 0);
  dots = mk_group(bar, LV_SIZE_CONTENT, 8);
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dots, 4, 0);
  lv_obj_align(dots, LV_ALIGN_CENTER, 0, 0);

  hintbar = mk_box(scr, 0, SCR_H - HINT_H, SCR_W, HINT_H, T.bg, 0);
  lv_obj_set_style_border_side(hintbar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(hintbar, 1, 0);
  lv_obj_set_style_border_color(hintbar, T.line, 0);
  lv_obj_set_style_pad_left(hintbar, 8, 0);
  lv_obj_set_style_pad_column(hintbar, 4, 0);
  lv_obj_set_flex_flow(hintbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hintbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_timer_create(close_check, 100, NULL);
  lv_timer_create(rebuild_check, 0, NULL);
  ready = true;
  ui_show_view(M.home_view);
}
