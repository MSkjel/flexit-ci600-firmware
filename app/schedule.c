// see schedule.h
#include "schedule.h"
#include "plat.h"
#include <stdlib.h>
#include <string.h>

#define MIN_LEN 5  // shortest period, minutes

static int by_start(const void *a, const void *b) {
  return ((const period_t *) a)->start - ((const period_t *) b)->start;
}

void sched_store(int day, const period_t *periods, int n) {
  if (n > PERIODS_PER_DAY)
    n = PERIODS_PER_DAY;
  period_t s[PERIODS_PER_DAY];
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
    if (M.week[p->day][i].idx != p->p.idx)
      out[n++] = M.week[p->day][i];
  return n;
}

// Overlapping periods run in the CX60's slot order, not as the day screen shows them. So a period stays between
// its neighbours: from where the ones that start before it end to where the next one starts.
void sched_range(val_t v, int32_t *min, int32_t *max) {
  const period_t *p = &M.period.p;
  period_t s[PERIODS_PER_DAY];
  int n = others(s), lo = 0, hi = 1440;
  for (int i = 0; i < n; i++) {
    if (s[i].start <= p->start && s[i].end > lo)
      lo = s[i].end;
    else if (s[i].start > p->start && s[i].start < hi)
      hi = s[i].start;
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
  period_t *p = &M.period.p;
  if (v == V_PERIOD_START && p->end < p->start + MIN_LEN)
    p->end = (uint16_t) (p->start + MIN_LEN);
  if (v == V_PERIOD_END && p->start > p->end - MIN_LEN)
    p->start = (uint16_t) (p->end - MIN_LEN);
  period_t s[PERIODS_PER_DAY];
  int n = others(s);
  if (n < PERIODS_PER_DAY)
    s[n++] = *p;
  sched_store(M.period.day, s, n);
  plat_period_set();
  ui_value_changed(v);
}

// Where a new period goes: 08:00-16:00 if that's free, else the first free time. false if there's none
static bool free_time(int day, uint16_t *start, uint16_t *end) {
  const period_t *w = M.week[day];
  int n = M.n_periods[day], at = 0;  // where the periods so far end
  if (n >= PERIODS_PER_DAY)
    return false;
  bool clash = false;
  for (int i = 0; i < n; i++)
    clash |= w[i].start < 16 * 60 && w[i].end > 8 * 60;
  if (!clash) {
    *start = 8 * 60;
    *end = 16 * 60;
    return true;
  }
  for (int i = 0; i <= n; i++) {
    int next = i < n ? w[i].start : 1440;
    if (next - at > MIN_LEN) {  // more than the shortest period, so its start and end can be edited
      *start = (uint16_t) at;
      *end = (uint16_t) next;
      return true;
    }
    if (i < n && w[i].end > at)
      at = w[i].end;
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
    *p = (period_edit_t) {.day = (uint8_t) day, .p = M.week[day][i]};
    return true;
  }
  uint16_t start, end;
  if (!free_time(day, &start, &end))
    return false;
  uint8_t used = 0;  // new period takes the day's first free CX60 slot
  for (int k = 0; k < M.n_periods[day]; k++)
    used |= (uint8_t) (1u << M.week[day][k].idx);
  int idx = 0;
  while (idx < PERIODS_PER_DAY - 1 && (used & (1u << idx)))
    idx++;
  *p = (period_edit_t) {.day = (uint8_t) day,
                        .p = {.start = start,
                              .end = end,
                              .level = MODE_NORMAL,
                              .idx = (uint8_t) idx,
                              .setpoint = M.setpoint ? M.setpoint : 200}};
  sched_period_changed(V_NONE);
  return true;
}

void sched_period_remove(void) {
  period_t s[PERIODS_PER_DAY];
  int n = others(s);
  sched_store(M.period.day, s, n);
}
