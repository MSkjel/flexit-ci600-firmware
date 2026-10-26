// see trend.h
#include "trend.h"
#include "model.h"
#include <string.h>

// Byte encoding per series in 0.1 C. Code 1 = lo, each code above it adds step
static const struct {
  int16_t lo;
  uint8_t step;
} STEP[TR_COUNT] = {[TR_OUTDOOR] = {-635, 5}, [TR_SUPPLY] = {-50, 2}, [TR_EXTRACT] = {-50, 2}, [TR_ROOM] = {-50, 2}};

uint8_t trend_code(int s, int v) {
  if (v == T_NONE)
    return 0;
  int c = (v - STEP[s].lo + STEP[s].step / 2) / STEP[s].step;
  return (uint8_t) ((c < 0 ? 0 : c > 254 ? 254 : c) + 1);
}

int trend_value(int s, uint8_t c) { return c ? STEP[s].lo + (c - 1) * STEP[s].step : TREND_NONE; }

const item_trend_t *trend_of(int id) {
  for (int i = 0; id && i < N_TRENDS; i++)
    if (M.item_trends[i].id == id)
      return &M.item_trends[i];
  return NULL;
}

void trend_keep(int id) {
  if (!id || trend_of(id))
    return;
  for (int i = 0; i < N_TRENDS; i++)
    if (!M.item_trends[i].id) {
      M.item_trends[i].id = (uint16_t) id;
      for (int k = 0; k < TREND_LEN; k++)
        M.item_trends[i].pt[k] = TREND_NONE;
      return;
    }
}

// An item's value now, TREND_NONE if it isn't there. Clamped so it can't be T_NONE or TREND_NONE.
static int16_t item_point(int id) {
  const item_t *it = item_by_id(id);
  if (!it || !M.esp_online || !(it->flags & ITEMF_TREND) || it->value == ITEM_UNKNOWN)
    return TREND_NONE;
  return (int16_t) (it->value < -32767 ? -32767 : it->value > 32766 ? 32766 : it->value);
}

void trend_second(void) {
  static uint16_t s;
  if (++s < 900)
    return;
  s = 0;
  const int16_t now[TR_COUNT] = {M.t_outdoor, M.t_supply, M.t_extract, M.t_room};
  for (int i = 0; i < TR_COUNT; i++) {
    memmove(&M.trend[i][0], &M.trend[i][1], TREND_LEN - 1);
    M.trend[i][TREND_LEN - 1] = trend_code(i, now[i]);
  }
  for (int i = 0; i < N_TRENDS; i++) {
    item_trend_t *t = &M.item_trends[i];
    if (!t->id)
      continue;
    memmove(&t->pt[0], &t->pt[1], (TREND_LEN - 1) * sizeof t->pt[0]);
    t->pt[TREND_LEN - 1] = item_point(t->id);
    bool any = false;
    for (int k = 0; k < TREND_LEN && !any; k++)
      any = t->pt[k] != TREND_NONE;
    if (!any)
      t->id = 0;  // its item has been gone a day, free the slot
  }
  // items that didn't get a slot yet may get one that was freed
  for (int i = 0; i < N_ITEMS; i++)
    if (M.items[i].flags & ITEMF_TREND)
      trend_keep(M.items[i].id);
}
