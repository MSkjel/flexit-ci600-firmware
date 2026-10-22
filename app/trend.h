// Last 24 h for the trend views, a point every 15 min. Temperatures take a byte per point, outdoor in 0.5 C steps
// and the rest 0.2 C from -5 to 45.8 C. Up to N_TRENDS ESP sensors with ITEMF_TREND are kept as is, by item id.
#pragma once
#include <stdint.h>
#include "model.h"

void trend_second(void);                    // 1 Hz, adds a point every 15 minutes
uint8_t trend_code(int series, int value);  // temperature to its byte, T_NONE -> 0
int trend_value(int series, uint8_t code);  // byte back to 0.1 C, 0 -> TREND_NONE
void trend_keep(int id);                    // start a trend for item id, if there's a free slot
const item_trend_t *trend_of(int id);       // NULL if there's none
