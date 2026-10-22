// System > About, Bus and Diagnostics pages.
#include "plat.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define INFO_ROWS 8  // all that fit above the key hints
#define INFO_ROW 23

// One row, label and value text
typedef struct {
  str_t k;
  char v[48];
} kv_t;

// KB with one decimal
static const char *kb(char *buf, uint32_t bytes) { return fmt_dec(buf, (int32_t) (bytes * 10 / 1024), 1); }

// Fills r with the rows for page INFO_*, returns how many
static int info_rows(int page, kv_t *r) {
  int n = 0;
// adds a row unless the page is full
#define ROW(key, fmt, ...) \
  do { \
    if (n == INFO_ROWS) \
      break; \
    r[n].k = key; \
    snprintf(r[n].v, sizeof r[n].v, fmt, ##__VA_ARGS__); \
    n++; \
  } while (0)
  if (page == INFO_ABOUT) {
    ROW(S_PANEL_FIRMWARE, "%u.%u.%u", (unsigned) (M.fw_version >> 16), (unsigned) (M.fw_version >> 8 & 0xFF),
        (unsigned) (M.fw_version & 0xFF));
    if (M.cu60_version)
      ROW(S_CU60_SOFTWARE, "%d.%02d", M.cu60_version >> 8, M.cu60_version & 0xFF);
    else
      ROW(S_CU60_SOFTWARE, "%s", _(S_NOT_SEEN_YET));
    ROW(S_PANEL_ADDRESS, "%d", M.bus_addr);
    if (HAS(FEAT_ESP))
      ROW(S_ESP, _(S_ESP_ADDRESS), M.esp_online ? _(S_ONLINE) : _(S_OFFLINE));
    ROW(S_FANS, "%s", HAS(FEAT_EC_FANS) ? _(S_EC_VARIABLE) : _(S_AC_STEPPED));
    ROW(S_HEATER, "%s", HAS(FEAT_WATER_HEATER) ? _(S_WATER_COIL) : _(S_ELECTRIC));
    ROW(S_COOLING, "%s", HAS(FEAT_COOLING) ? _(S_INSTALLED) : _(S_NOT_INSTALLED));
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
    ROW(S_SLOWEST_REDRAW, "%lu ms", (unsigned long) ((s.draw_max_us + 500) / 1000));
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
} info_t;
SCREEN_STATE(info_t, info);

static void info_build(lv_obj_t *b, intptr_t arg) {
  memset(info, 0, sizeof *info);
  ui_set_title(arg == INFO_BUS ? _(S_BUS) : arg == INFO_DIAG ? _(S_DIAGNOSTICS) : _(S_ABOUT));
  kv_t r[INFO_ROWS];
  info->page = (int) arg;
  info->n = info_rows(info->page, r);
  for (int i = 0; i < info->n; i++) {
    int y = 6 + i * INFO_ROW;
    if (i % 2 == 0)
      mk_box(b, 8, y - 2, 304, INFO_ROW, T.surface, 6);
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

const screen_t SCR_INFO = {.title = S_ABOUT, .build = info_build, .update = info_update};
