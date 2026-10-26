// Week program screens: the week overview and one day's periods.
#include "ui_int.h"

// ---------------------------------------------------------------- the week
#define WEEK_X 46  // timelines: left edge, width, row height, top
#define WEEK_W 262
#define WEEK_ROW 22
#define WEEK_Y 22

typedef struct {
  lv_obj_t *now;
} week_t;
SCREEN_STATE(week_t, week);

static const str_t DAY_SHORT[7] = {S_MON, S_TUE, S_WED, S_THU, S_FRI, S_SAT, S_SUN};

static void week_build(lv_obj_t *b, intptr_t arg) {
  (void) arg;
  for (int h = 0; h <= 24; h += 6) {
    lv_obj_t *l = mk_label(b, &font_f12, T.faint, "");
    set_text(l, "%02d", h);
    lv_obj_set_pos(l, WEEK_X + WEEK_W * h / 24 - (h == 24 ? 14 : h ? 7 : 0), 2);
  }
  for (int d = 0; d < 7; d++) {
    int y = WEEK_Y + d * WEEK_ROW;
    lv_obj_set_pos(mk_label(b, &font_f14, d == M.wday ? T.accent : T.dim, tr(DAY_SHORT[d])), 10, y + 1);
    mk_day_bar(b, WEEK_X, y + 3, WEEK_W, WEEK_ROW - 8, d);
  }
  for (int h = 6; h < 24; h += 6) {  // 6 h grid lines across all days
    lv_obj_t *g = mk_box(b, WEEK_X + WEEK_W * h / 24, WEEK_Y, 1, 7 * WEEK_ROW - 4, T.bg, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_60, 0);
  }
  week->now = mk_box(b, 0, 0, 2, WEEK_ROW - 2, T.text, 1);
  for (int m = MODE_MIN; m <= MODE_MAX; m++) {  // colour legend
    int x = WEEK_X + (m - MODE_MIN) * 74;
    mk_box(b, x, 182, 10, 10, level_color(m), 3);
    lv_obj_set_pos(mk_label(b, &font_f12, T.dim, tr(MODE_NAMES[m])), x + 14, 179);
  }
  lv_obj_t *st =
      mk_label(b, &font_f12, M.week_program ? T.ok : T.faint, M.week_program ? _(S_PROGRAM_ON) : _(S_PROGRAM_OFF));
  lv_obj_align(st, LV_ALIGN_TOP_RIGHT, -12, 179);
  ui_hints(S_NONE, S_NONE, S_BACK, S_HELP);
  ui_help(S_WEEK_HELP);
}

static void week_update(void) {  // move the "now" marker
  lv_obj_set_pos(week->now, WEEK_X + (M.hh * 60 + M.mi) * WEEK_W / 1440 - 1, WEEK_Y + M.wday * WEEK_ROW);
}

const screen_t SCR_SCHEDULE = {.title = S_WEEK_PROGRAM, .build = week_build, .update = week_update};

// ---------------------------------------------------------------- one day (arg 0 = Monday)
// The day's periods in order, plus "Add period" while there's room. OK opens the period's settings.
#define DAY_ROW 38

typedef struct {
  lv_obj_t *row[PERIODS_PER_DAY], *text[PERIODS_PER_DAY];  // each row and its times or "Add period"
  int n;                                                   // rows: periods, plus "Add period" if there's room
} day_t;
SCREEN_STATE(day_t, day);

// Returns the label with the times
static lv_obj_t *period_row(lv_obj_t *row, const period_t *p) {
  char a[16], b[16];
  mk_box(row, 10, 12, 10, 10, level_color(p->level), 3);
  lv_obj_t *t = mk_label(row, &font_f16, T.text, "");
  set_text(t, "%s  " EN_DASH "  %s", fmt_hm(a, sizeof a, p->start), fmt_hm(b, sizeof b, p->end));
  lv_obj_set_pos(t, 30, 6);
  lv_obj_align(mk_label(row, &font_f14, level_color(p->level), tr(MODE_NAMES[p->level % MODE_COUNT])),
               LV_ALIGN_RIGHT_MID, -12, 0);
  return t;
}

static void day_build(lv_obj_t *b, intptr_t arg) {
  int d = (int) arg % 7;
  ui_set_title(tr(DAY_NAMES[d]));
  mk_day_bar(b, 12, 8, 296, 14, d);
  day->n = M.n_periods[d] + sched_can_add(d);
  clamp_sel(day->n, 0);
  for (int s = 0; s < day->n; s++) {
    lv_obj_t *row = day->row[s] = mk_box(b, 8, 32 + s * DAY_ROW, 304, DAY_ROW - 4, T.bg, 8);
    if (s < M.n_periods[d])
      day->text[s] = period_row(row, &M.week[d][s]);
    else
      lv_obj_set_pos(day->text[s] = mk_label(row, &font_f14, T.faint, _(S_ADD_PERIOD)), 30, 8);
  }
  ui_hints(S_MOVE, S_EDIT, S_BACK, S_HELP);
  ui_help(S_DAY_SCREEN_HELP);
}

static void day_update(void) {
  int sel = ui_frame()->sel;
  for (int s = 0; s < day->n; s++)
    lv_obj_set_style_bg_color(day->row[s], s == sel ? T.surface : T.bg, 0);
}

static bool day_key(int k) {
  if (k != KEY_OK)
    return sel_key(k, day->n);
  frame_t *f = ui_frame();
  int d = (int) f->arg % 7;
  bool existing = f->sel < M.n_periods[d];
  if (!sched_edit(d, existing ? f->sel : -1))
    return true;
  if (!existing)
    ui_toast(IC_CHECK, S_PERIOD_ADDED);
  ui_push(&SCR_LIST, (intptr_t) period_menu());
  return true;
}

// Selected row's text: the period's times, or "+  Add period"
static const char *day_selected(void) { return lv_label_get_text(day->text[ui_frame()->sel]); }

const screen_t SCR_DAY = {.build = day_build, .update = day_update, .key = day_key, .selected = day_selected};
