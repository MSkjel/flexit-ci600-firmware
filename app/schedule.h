// Week program. The unit has each day's periods, see plat_week(), and M.period is the one being edited.
#pragma once
#include "model.h"

int sched_day(int day, period_t out[PERIODS_PER_DAY]);  // the day's periods by start, how many
bool sched_can_add(int day);                            // the day has room for another period
bool sched_edit(int day, int i);                        // edit period i, < 0 for a new one. false if there's no room
void sched_range(val_t v, int32_t *min, int32_t *max);  // V_PERIOD_START or _END's range, between the neighbours
void sched_period_changed(val_t v);                     // one of the V_PERIOD_* values changed
