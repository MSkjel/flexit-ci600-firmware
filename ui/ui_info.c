// System > About, Bus and Diagnostics pages, and the notices.
#include "plat.h"
#include "ui.h"
#include "authors.h"  // AUTHORS(X), from git by config.mk

#define INFO_ROWS 8  // all that fit above the key hints
#define INFO_ROW 23
#define REPO "github.com/MSkjel/flexit-ci600-firmware"

// One row, label and value text
typedef struct {
  str_t k;
  char v[48];
} kv_t;

// KB with one decimal
static const char *kb(char *buf, uint32_t bytes) { return fmt_dec(buf, (int32_t) (bytes * 10 / 1024), 1); }

static const char *fw_text(char buf[12]) {
  lv_snprintf(buf, 12, "%u.%u.%u", (unsigned) (M.fw_version >> 16 & 0xFF), (unsigned) (M.fw_version >> 8 & 0xFF),
              (unsigned) (M.fw_version & 0xFF));
  return buf;
}

// Fills r with the rows for page INFO_*, returns how many
static int info_rows(int page, kv_t *r) {
  int n = 0;
// adds a row unless the page is full
#define ROW(key, fmt, ...) \
  do { \
    if (n == INFO_ROWS) \
      break; \
    r[n].k = key; \
    lv_snprintf(r[n].v, sizeof r[n].v, fmt, ##__VA_ARGS__); \
    n++; \
  } while (0)
  if (page == INFO_ABOUT) {
    char fw[12];
    ROW(S_PANEL_FIRMWARE, "%s", fw_text(fw));
    if (M.cx60_version)
      ROW(S_CX60_SOFTWARE, "%d.%02d", M.cx60_version >> 8, M.cx60_version & 0xFF);
    else
      ROW(S_CX60_SOFTWARE, "%s", _(S_NOT_SEEN_YET));
    ROW(S_PANEL_ADDRESS, "%d", M.bus_addr);
    if (HAS(FEAT_ESP))
      ROW(S_ESP, _(S_ESP_ADDRESS), M.esp_online ? _(S_ONLINE) : _(S_OFFLINE));
    ROW(S_FANS, "%s", HAS(FEAT_EC_FANS) ? _(S_EC_VARIABLE) : _(S_AC_STEPPED));
    ROW(S_HEATER, "%s", HAS(FEAT_WATER_HEATER) ? _(S_WATER_COIL) : _(S_ELECTRIC));
    ROW(S_COOLING, "%s", HAS(FEAT_COOLING) ? _(S_INSTALLED) : _(S_NOT_INSTALLED));
    ROW(S_NONE, "%s", REPO);
  } else if (page == INFO_DIAG) {
    static const str_t STARTS[] = {[RESET_POWER] = S_START_POWER,
                                   [RESET_PIN] = S_START_PIN,
                                   [RESET_WATCHDOG] = S_START_WATCHDOG,
                                   [RESET_SOFTWARE] = S_START_SOFTWARE};
    plat_stats_t s;
    plat_stats(&s);
    uint32_t up = s.uptime_s;
    if (up >= 86400)
      ROW(S_UPTIME, _(S_UPTIME_DAYS), (unsigned long) (up / 86400), (unsigned) (up / 3600 % 24),
          (unsigned) (up / 60 % 60));
    else
      ROW(S_UPTIME, "%02u:%02u:%02u", (unsigned) (up / 3600), (unsigned) (up / 60 % 60), (unsigned) (up % 60));
    if (M.reset_cause == RESET_FAULT)
      ROW(S_LAST_START, _(S_START_CRASH), (unsigned long) M.fault_pc);
    else
      ROW(S_LAST_START, "%s", tr(STARTS[M.reset_cause]));
    ROW(S_RESTARTS, "%lu", (unsigned long) s.restarts);
    ROW(S_PROCESSOR, _(S_BUSY_PCT), M.cpu_busy);
    char used[12], peak[12];
    ROW(S_MEMORY, _(S_MEMORY_USE), kb(used, s.heap_used), (unsigned) (s.heap_size / 1024), kb(peak, s.heap_peak));
    ROW(S_FREE_STACK, "%s KB", kb(used, s.stack_free));
    ROW(S_FULL_REDRAW, _(S_REDRAW_SENDING), (unsigned long) ((s.full_us + 500) / 1000),
        (unsigned long) ((s.full_send_us + 500) / 1000));
    ROW(S_SETTINGS_SAVES, "%lu", (unsigned long) s.saves);
  } else {
    ROW(S_UNIT, "%s", M.bus_offline ? _(S_NO_CONTACT_LOWER) : _(S_TALKING));
    ROW(S_POLLS_TO_THIS_PANEL, "%lu", (unsigned long) M.bus_ours);
    ROW(S_FRAMES_OK, "%lu", (unsigned long) M.bus_ok);
    uint64_t frames = (uint64_t) M.bus_ok + M.bus_err;
    unsigned long bp = frames ? (unsigned long) (M.bus_err * 10000ull / frames) : 0;  // 0.01 % steps
    ROW(S_ERRORS, "%lu (%lu.%02lu %%)", (unsigned long) M.bus_err, bp / 100, bp % 100);
    if (HAS(FEAT_ESP)) {
      const pbulk_panel_t *b = link_bulk_state();
      const pbus_panel_t *w = link_bus_state();
      if (M.esp_version && !link_compatible())
        ROW(S_ESP_LINK, _(S_LINK_OTHER_VERSION), PLINK_VERSION, M.esp_version);
      else
        ROW(S_ESP_LINK, _(S_LINK_VERSION), PLINK_VERSION,
            w->refused     ? _(S_REFUSED)
            : M.esp_online ? _(S_CONNECTED)
                           : _(S_WAITING));
      ROW(S_LINK_FRAMES, "%lu", (unsigned long) b->frames);
      ROW(S_LATE_BUSY_LOST, "%lu, %lu, %lu", (unsigned long) b->late, (unsigned long) b->busy,
          (unsigned long) b->resyncs);
      // shortest pause after the unit's status broadcast, in 0.1 ms
      char ms[12];
      fmt_dec(ms, (int32_t) (w->quiet_min_us / 100), 1);
      if (w->quiet_min_us == UINT32_MAX)
        ROW(S_UNIT_PAUSE, "%s", _(S_NOT_SEEN_YET));
      else if (w->refused)
        ROW(S_UNIT_PAUSE, _(S_PAUSE_TOO_SHORT), ms);
      else if (w->gaps.off)
        ROW(S_UNIT_PAUSE, _(S_PAUSE_NO_GAPS), ms);
      else
        ROW(S_UNIT_PAUSE, _(S_PAUSE_MS), ms);
    }
  }
#undef ROW
  return n;
}

typedef struct {
  lv_obj_t *v[INFO_ROWS];
  int page, n;
  uint8_t keys;  // how far into KEYS
  uint32_t key_at;
} info_t;
SCREEN_STATE(info_t, info);

static void info_build(lv_obj_t *b, intptr_t arg) {
  ui_set_title(arg == INFO_BUS ? _(S_BUS) : arg == INFO_DIAG ? _(S_DIAGNOSTICS) : _(S_ABOUT));
  kv_t r[INFO_ROWS];
  info->page = (int) arg;
  info->n = info_rows(info->page, r);
  for (int i = 0; i < info->n; i++) {
    int y = 6 + i * INFO_ROW;
    if (i % 2 == 0)
      mk_box(b, 8, y - 2, 304, INFO_ROW, T.surface, 6);
    if (r[i].k == S_NONE) {  // a value alone, centred
      info->v[i] = mk_label(b, &font_f14, T.dim, r[i].v);
      lv_obj_align(info->v[i], LV_ALIGN_TOP_MID, 0, y + 1);
      continue;
    }
    lv_obj_set_pos(mk_label(b, &font_f14, T.dim, tr(r[i].k)), 18, y + 1);
    info->v[i] = mk_label(b, &font_f14, T.text, r[i].v);
    lv_obj_align(info->v[i], LV_ALIGN_TOP_RIGHT, -18, y + 1);
  }
  ui_hints(S_NONE, S_NONE, S_BACK, S_HELP);
  ui_help(arg == INFO_BUS ? S_BUS_PAGE_HELP : arg == INFO_DIAG ? S_DIAG_PAGE_HELP : S_ABOUT_PAGE_HELP);
}

// Values only, the rows themselves only change on a rebuild
static void info_update(void) {
  kv_t r[INFO_ROWS];
  if (info_rows(info->page, r) != info->n)
    return;
  for (int i = 0; i < info->n; i++)
    lv_label_set_text(info->v[i], r[i].v);
}

// KEYS on About open the notices. A wrong key or a 3 s pause starts over.
static bool info_key(int k) {
  static const uint8_t KEYS[] = {KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN, KEY_UP, KEY_DOWN, KEY_UP, KEY_DOWN, KEY_OK};
  if (info->page != INFO_ABOUT || (k != KEY_UP && k != KEY_DOWN && k != KEY_OK))
    return false;
  uint32_t now = lv_tick_get();
  if (now - info->key_at > 3000)
    info->keys = 0;
  info->key_at = now;
  info->keys = k == KEYS[info->keys] ? info->keys + 1 : k == KEYS[0];
  if (info->keys == LEN(KEYS)) {
    info->keys = 0;
    ui_push(&SCR_NOTICES, 0);
  }
  return true;
}

const screen_t SCR_INFO = {.build = info_build, .update = info_update, .key = info_key};

// ---------------------------------------------------------------- notices
#define NOTICE_MS 50           // per px up
#define NOTICE_LABELS 16       // enough for a screen of the shortest lines
#define NOTICE_W (SCR_W - 24)  // wider lines wrap

enum { N_TITLE, N_VERSION, N_GAP, N_HEAD, N_NAME, N_TEXT, N_MORE, N_LINK, N_LAST };

typedef struct {
  uint8_t kind;   // N_*
  str_t s;        // the text
  const char *t;  // or this, not translated
} notice_t;

// Top to bottom, then again
#define AUTHOR(name) {N_NAME, S_NONE, name}
static const notice_t NOTICES[] = {
    {N_TITLE, S_NONE, "CI600"},
    {N_VERSION, S_NONE, NULL},
    {N_GAP, S_NONE, NULL},
    {N_HEAD, S_NOTICE_REVERSED, NULL},
    {N_NAME, S_NONE, "MSkjel"},
#if AUTHORS_N
    {N_HEAD, S_NOTICE_WRITTEN, NULL},
    AUTHORS(AUTHOR),
#endif
    {N_HEAD, S_NOTICE_TOOLS, NULL},
    {N_NAME, S_NONE, "Ghidra"},
    {N_HEAD, S_NOTICE_BUILT_ON, NULL},
    {N_NAME, S_NONE, "LVGL"},
    {N_NAME, S_NOTICE_JPEG, NULL},
    {N_TEXT, S_NOTICE_FONT, NULL},
    {N_MORE, S_NOTICE_ICONS, NULL},
    {N_TEXT, S_NOTICE_ESPHOME, NULL},
    {N_TEXT, S_NOTICE_FLEXIT, NULL},
    {N_TEXT, S_NOTICE_HARMED, NULL},
    {N_MORE, S_NOTICE_CX60, NULL},
    {N_TEXT, S_NOTICE_SINCE, NULL},
    {N_LINK, S_NONE, REPO},
    {N_LAST, S_NOTICE_OK, NULL},
};
#undef AUTHOR

typedef struct {
  lv_obj_t *l[NOTICE_LABELS];
  int16_t y[NOTICE_LABELS], h[NOTICE_LABELS];  // h 0 = free
  int16_t next_y;                              // where the space above the next line starts
  uint8_t next;                                // NOTICES[next] comes next
  bool led_started;
  uint32_t led_from;  // ms
} notices_t;
SCREEN_STATE(notices_t, notices);

// Font, colour and px of space above, for each N_*
static const struct {
  const lv_font_t *font;
  const lv_color_t *c;
  uint8_t above;
} STYLES[] = {
    [N_TITLE] = {&font_f28, &T.accent, 0}, [N_VERSION] = {&font_f14, &T.dim, 2}, [N_GAP] = {NULL, NULL, 20},
    [N_HEAD] = {&font_f14, &T.dim, 22},    [N_NAME] = {&font_f16, &T.text, 2},   [N_TEXT] = {&font_f14, &T.text, 22},
    [N_MORE] = {&font_f14, &T.text, 2},    [N_LINK] = {&font_f14, &T.dim, 22},   [N_LAST] = {&font_f16, &T.accent, 40},
};

// Places lines on free labels until the screen is full below
static void notices_fill(lv_obj_t *b) {
  for (;;) {
    const notice_t *n = &NOTICES[notices->next];
    if (notices->next_y + STYLES[n->kind].above >= SCR_H)
      return;
    int i = 0;
    while (n->kind != N_GAP && i < NOTICE_LABELS && notices->h[i])
      i++;
    if (i == NOTICE_LABELS)
      return;
    notices->next_y += STYLES[n->kind].above;
    if (n->kind != N_GAP) {
      const lv_font_t *f = STYLES[n->kind].font;
      lv_obj_t *l = notices->l[i];
      if (!l) {
        l = notices->l[i] = lv_label_create(b);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      }
      lv_obj_set_style_text_font(l, f, 0);
      lv_obj_set_style_text_color(l, *STYLES[n->kind].c, 0);
      char fw[12];
      if (n->kind == N_VERSION)
        set_text(l, _(S_NOTICE_FIRMWARE), fw_text(fw));
      else
        label_text(l, n->t ? n->t : tr(n->s));
      lv_point_t sz;
      lv_txt_get_size(&sz, lv_label_get_text(l), f, 0, 0, NOTICE_W, LV_TEXT_FLAG_NONE);
      lv_obj_set_size(l, sz.x, sz.y);
      lv_obj_set_pos(l, (SCR_W - sz.x) / 2, notices->next_y);
      notices->y[i] = notices->next_y;
      notices->h[i] = (int16_t) sz.y;
      notices->next_y += sz.y;
    }
    if (++notices->next == LEN(NOTICES)) {
      notices->next = 0;
      notices->next_y += SCR_H / 2;
    }
  }
}

// One px up. Each line moves by itself, so only the lines get redrawn and not the space between them.
static void notices_step(lv_timer_t *t) {
  (void) t;
  if (ui_top() != &SCR_NOTICES)
    return;  // under a pop-up
  for (int i = 0; i < NOTICE_LABELS; i++) {
    if (!notices->h[i])
      continue;
    lv_obj_set_y(notices->l[i], --notices->y[i]);
    if (notices->y[i] + notices->h[i] <= 0)
      notices->h[i] = 0;
  }
  notices->next_y--;
  notices_fill(lv_obj_get_parent(notices->l[0]));
}

#define DOT_MS 120
#define MORSE_PAUSE 14  // dots dark after the gap behind the last character, 2 s in all

// Walks v in Morse to dot u, *on if the light is on there. Returns the length in dots.
static uint32_t morse(const char *v, uint32_t u, bool *on) {
  static const char *const DIGITS[10] = {"-----", ".----", "..---", "...--", "....-",
                                         ".....", "-....", "--...", "---..", "----."};
  uint32_t t = 0;
  *on = false;
  for (; *v; v++, t += 2)  // 3 between characters, one of them already after the last element
    for (const char *m = *v == '.' ? ".-.-.-" : DIGITS[*v - '0']; *m; m++) {
      uint32_t n = *m == '-' ? 3 : 1;
      *on |= u >= t && u < t + n;
      t += n + 1;
    }
  return t;
}

// The version in Morse, over and over from the first call
static bool notices_led(uint32_t ms) {
  if (!notices->led_started) {
    notices->led_started = true;
    notices->led_from = ms;
  }
  char fw[12];
  bool on;
  uint32_t len = morse(fw_text(fw), UINT32_MAX, &on) + MORSE_PAUSE;
  morse(fw, (ms - notices->led_from) / DOT_MS % len, &on);
  return on;
}

static void notices_build(lv_obj_t *b, intptr_t arg) {
  (void) arg;
  notices->next_y = 70;  // the title starts a bit down rather than below the screen
  notices_fill(b);
  mk_timer(notices->l[0], notices_step, NOTICE_MS);
}

static bool notices_key(int k) {
  if (k == KEY_OK)
    ui_replace(&SCR_KEYTEST, 0);
  return k != KEY_BACK;
}

const screen_t SCR_NOTICES = {.flags = SF_NOBARS, .build = notices_build, .key = notices_key, .ok_led = notices_led};
