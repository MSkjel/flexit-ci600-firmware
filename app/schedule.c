// see schedule.h
#include "schedule.h"
#include "plat.h"
#include <stdlib.h>
#include <string.h>

#define MIN_LEN 5  // shortest period, minutes

static int start_of(const period_t *p) { return p->start_h * 60 + p->start_m; }
static int end_of(const period_t *p) { return p->end_h * 60 + p->end_m; }

static int by_start(const void *a, const void *b) { return start_of(a) - start_of(b); }

void sched_store(int day, const period_t *periods, int n) {
  if (n > 4)
    n = 4;
  period_t s[4];
  memcpy(s, periods, n * sizeof *s);
  qsort(s, n, sizeof *s, by_start);
  if (n == M.n_periods[day] && !memcmp(s, M.week[day], n * sizeof *s))
    return;
  memcpy(M.week[day], s, n * sizeof *s);
  M.n_periods[day] = (uint8_t) n;
  ui_layout_changed();  // the day and week screens show them
}

// the day's periods without the one being edited
static int others(period_t *out) {
  const period_edit_t *p = &M.period;
  int n = 0;
  for (int i = 0; i < M.n_periods[p->day]; i++)
    if (M.week[p->day][i].idx != p->idx)
      out[n++] = M.week[p->day][i];
  return n;
}

// Overlapping periods run in the CX60's slot order, not as the day screen shows them. So a period stays between
// its neighbours: from where the ones that start before it end to where the next one starts.
void sched_range(val_t v, int32_t *min, int32_t *max) {
  const period_edit_t *p = &M.period;
  period_t s[4];
  int n = others(s), lo = 0, hi = 1440;
  for (int i = 0; i < n; i++) {
    int a = start_of(&s[i]), e = end_of(&s[i]);
    if (a <= p->start && e > lo)
      lo = e;
    else if (a > p->start && a < hi)
      hi = a;
  }
  if (hi - lo < MIN_LEN) {  // squeezed between overlapping ones from elsewhere, leave it free
    lo = 0;
    hi = 1440;
  }
  *min = v == V_PERIOD_END ? lo + MIN_LEN : lo;
  *max = v == V_PERIOD_END ? hi : hi - MIN_LEN;
}

// Put M.period into the day's list, replacing the one in the same CX60 slot, and write it to the unit
void sched_period_changed(val_t v) {
  period_edit_t *p = &M.period;
  if (v == V_PERIOD_START && p->end < p->start + MIN_LEN)
    p->end = (uint16_t) (p->start + MIN_LEN);
  if (v == V_PERIOD_END && p->start > p->end - MIN_LEN)
    p->start = (uint16_t) (p->end - MIN_LEN);
  period_t s[4];
  int n = others(s);
  if (n < 4)
    s[n++] = (period_t) {.start_h = (uint8_t) (p->start / 60),
                         .start_m = (uint8_t) (p->start % 60),
                         .end_h = (uint8_t) (p->end / 60),
                         .end_m = (uint8_t) (p->end % 60),
                         .level = p->level,
                         .idx = p->idx,
                         .setpoint = p->setpoint};
  sched_store(p->day, s, n);
  plat_period_set();
  ui_value_changed(v);
}

// Where a new period goes: 08:00-16:00 if that's free, else the first free time. false if there's none
static bool free_time(int day, uint16_t *start, uint16_t *end) {
  const period_t *w = M.week[day];
  int n = M.n_periods[day], at = 0;  // where the periods so far end
  if (n >= 4)
    return false;
  bool clash = false;
  for (int i = 0; i < n; i++)
    clash |= start_of(&w[i]) < 16 * 60 && end_of(&w[i]) > 8 * 60;
  if (!clash) {
    *start = 8 * 60;
    *end = 16 * 60;
    return true;
  }
  for (int i = 0; i <= n; i++) {
    int next = i < n ? start_of(&w[i]) : 1440;
    if (next - at > MIN_LEN) {  // more than the shortest period, so its start and end can be edited
      *start = (uint16_t) at;
      *end = (uint16_t) next;
      return true;
    }
    if (i < n && end_of(&w[i]) > at)
      at = end_of(&w[i]);
  }
  return false;
}

bool sched_can_add(int day) {
  uint16_t a, e;
  return free_time(day, &a, &e);
}

bool sched_edit(int day, int i) {
  period_edit_t *p = &M.period;
  if (i >= 0 && i < M.n_periods[day]) {
    const period_t *s = &M.week[day][i];
    *p = (period_edit_t) {.day = (uint8_t) day,
                          .idx = s->idx,
                          .start = (uint16_t) start_of(s),
                          .end = (uint16_t) end_of(s),
                          .level = s->level,
                          .setpoint = s->setpoint};
    return true;
  }
  uint16_t start, end;
  if (!free_time(day, &start, &end))
    return false;
  uint8_t used = 0;  // new period takes the day's first free CX60 slot
  for (int k = 0; k < M.n_periods[day]; k++)
    used |= (uint8_t) (1u << M.week[day][k].idx);
  int idx = 0;
  while (idx < 3 && (used & (1u << idx)))
    idx++;
  *p = (period_edit_t) {.day = (uint8_t) day,
                        .idx = (uint8_t) idx,
                        .start = start,
                        .end = end,
                        .level = MODE_NORMAL,
                        .setpoint = M.setpoint ? M.setpoint : 200};
  sched_period_changed(V_NONE);
  return true;
}

void sched_period_remove(void) {
  period_t s[4];
  int n = others(s);
  sched_store(M.period.day, s, n);
}
