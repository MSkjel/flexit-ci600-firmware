// Week program, each day's periods in M.week and the one being edited in M.period.
#pragma once
#include "model.h"

void sched_store(int day, const period_t *periods, int n);  // replaces a day's periods, max 4, sorted by start
bool sched_can_add(int day);                                // the day has room for another period
bool sched_edit(int day, int i);                        // edit period i, < 0 for a new one. false if there's no room
void sched_range(val_t v, int32_t *min, int32_t *max);  // V_PERIOD_START or _END's range, between the neighbours
void sched_period_changed(val_t v);                     // one of the V_PERIOD_* values changed
void sched_period_remove(void);                         // removes the period being edited
