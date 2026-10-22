// Values. Every change goes through apply(), wherever it comes from. It clamps to the range and the VF_* flags
// decide if it's saved, written to the bus or sent to the ESP.
#include "model.h"
#include "clock.h"
#include "link.h"
#include "plat.h"
#include "schedule.h"
#include <string.h>

// the VALUES() table, by value id
static const val_def_t DEFS[V_FEAT_FIRST] = {
#define X(id, k, field, lo, hi, fl, reg, sc) \
  [V_##id] = {.min = lo, .max = hi, .key = k, .flags = fl, .cu60 = reg, .scale = sc},
    VALUES(X)
#undef X
};

val_def_t val_def(val_t v) {
  if (v == V_DAY) {  // the days in the month, so setting the day doesn't roll into the next month
    val_def_t d = DEFS[v];
    d.max = clock_month_days(M.yy, M.mo);
    return d;
  }
  if (v == V_PERIOD_START || v == V_PERIOD_END) {
    val_def_t d = DEFS[v];
    sched_range(v, &d.min, &d.max);
    return d;
  }
  if (v > V_NONE && v < V_FEAT_FIRST)
    return DEFS[v];
  if (v >= V_FEAT_FIRST && v <= V_FEAT_LAST)
    return (val_def_t) {.max = 1, .key = K_FEATURE + (v - V_FEAT_FIRST), .flags = VF_PANEL | VF_LAYOUT, .scale = 1};
  if (v >= V_ITEM_FIRST && v <= V_ITEM_LAST) {
    const item_t *it = &M.items[v - V_ITEM_FIRST];
    bool ro = it->type == ITEM_SENSOR || it->type == ITEM_TEXT || (it->flags & ITEMF_RO);
    return (val_def_t) {.min = it->min,
                        .max = it->max,
                        .key = K_ITEM_VALUE + (v - V_ITEM_FIRST),
                        .flags = VF_ESP | VF_LINK_RW | (ro ? VF_RO : 0),
                        .scale = 1};
  }
  return (val_def_t) {0};
}

uint16_t val_cu60_reg(val_t v) {
  val_def_t d = val_def(v);
  return (uint16_t) (d.cu60 + ((d.flags & VF_PER_PANEL) ? M.bus_addr - 1u : 0u));
}

// Linear search, a few microseconds per register the CU60 changed
val_t val_by_cu60(uint16_t reg) {
  for (int v = V_NONE + 1; v < V_FEAT_FIRST; v++)
    if ((DEFS[v].flags & (VF_CU60 | VF_CMD)) == VF_CU60 && val_cu60_reg((val_t) v) == reg)
      return (val_t) v;
  return V_NONE;
}

val_t val_by_key(uint16_t key) {
  if (!key)
    return V_NONE;  // key 0 is for values that aren't on the link
  for (int v = V_NONE + 1; v < V_FEAT_FIRST; v++)
    if (DEFS[v].key == key)
      return (val_t) v;
  if (key >= K_FEATURE && key < K_FEATURE + FEAT_COUNT)
    return (val_t) (V_FEAT_FIRST + key - K_FEATURE);
  if (key >= K_ITEM_VALUE && key < K_ITEM_VALUE + N_ITEMS)
    return (val_t) (V_ITEM_FIRST + key - K_ITEM_VALUE);
  return V_NONE;
}

const item_t *item_of(val_t v) {
  return v >= V_ITEM_FIRST && v <= V_ITEM_LAST && M.items[v - V_ITEM_FIRST].type ? &M.items[v - V_ITEM_FIRST] : NULL;
}

const item_t *item_by_id(int id) {
  for (int i = 0; id && i < N_ITEMS; i++)
    if (M.items[i].type && M.items[i].id == id)
      return &M.items[i];
  return NULL;
}

bool val_known(val_t v) { return v < V_ITEM_FIRST || v > V_ITEM_LAST || item_of(v); }

void val_forget_esp(void) {
  memset(M.items, 0, sizeof M.items);
  for (int i = 0; i < N_ITEMS; i++)
    M.items[i].value = ITEM_UNKNOWN;  // until the ESP sends one
}

int32_t val_get(val_t v) {
  if (v >= V_FEAT_FIRST && v <= V_FEAT_LAST)
    return HAS(v - V_FEAT_FIRST);
  if (v >= V_ITEM_FIRST && v <= V_ITEM_LAST)
    return M.items[v - V_ITEM_FIRST].value;
  switch (v) {
#define X(id, key, field, ...) \
  case V_##id: \
    return (int32_t) (field);
    VALUES(X)
#undef X
    default:
      return 0;
  }
}

static void store(val_t v, int32_t x) {
  if (v >= V_FEAT_FIRST && v <= V_FEAT_LAST) {
    uint32_t bit = FBIT(v - V_FEAT_FIRST);
    M.features = x ? M.features | bit : M.features & ~bit;
    return;
  }
  if (v >= V_ITEM_FIRST && v <= V_ITEM_LAST) {
    M.items[v - V_ITEM_FIRST].value = x;
    return;
  }
  switch (v) {
#define X(id, key, field, ...) \
  case V_##id: \
    field = x; \
    break;
    VALUES(X)
#undef X
    default:
      break;
  }
}

typedef enum { FROM_USER, FROM_ESP, FROM_BUS, FROM_FLASH } source_t;

static void apply(val_t v, int32_t x, source_t from) {
  if (v <= V_NONE || v >= V_COUNT)
    return;
  val_def_t d = val_def(v);
  if ((from == FROM_USER && (d.flags & VF_RO)) || (from == FROM_ESP && !(d.flags & VF_LINK_W)))
    return;
  bool item = v >= V_ITEM_FIRST && v <= V_ITEM_LAST;
  int32_t want = x;
  // The ESP's items are taken as they are, and so is what the CU60 holds: its limits are wider than the editors'
  if (!(item && from == FROM_ESP) && from != FROM_BUS)
    x = x < d.min ? d.min : x > d.max ? d.max : x;
  // a button press counts even when the value is the same
  bool press = item && M.items[v - V_ITEM_FIRST].type == ITEM_BUTTON;
  if (x == val_get(v) && !press)
    return;
  store(v, x);
  if ((v == V_MONTH || v == V_YEAR) && M.dd > val_def(V_DAY).max)
    M.dd = (uint8_t) val_def(V_DAY).max;  // 31.03 to April is 30.04, not 01.05
  if (from == FROM_FLASH)
    return;  // loading at startup, nothing else runs yet
  // With auto lock the keys relock on idle anyway, so don't wear the flash on every lock and unlock
  if ((d.flags & VF_SAVED) && !(v == V_KEY_LOCK && M.auto_lock))
    plat_save(from == FROM_USER);
  if (v >= V_PERIOD_START && v <= V_PERIOD_SETPOINT) {
    sched_period_changed(v);
    return;
  }
  if (from == FROM_USER) {
    plat_value_changed(v);
    link_value_changed(v);
  } else if (from == FROM_ESP && !(d.flags & VF_CU60)) {
    plat_value_changed(v);  // not for CU60 values, the ESP has already written those to the CU60
    if (x != want)
      link_value_changed(v);  // clamped, so tell the ESP what we actually stored
  }
  ui_value_changed(v);
}

void val_set(val_t v, int32_t x) { apply(v, x, FROM_USER); }
void val_set_remote(val_t v, int32_t x) { apply(v, x, FROM_ESP); }
void val_set_bus(val_t v, int32_t x) { apply(v, x, FROM_BUS); }
void val_load(val_t v, int32_t x) { apply(v, x, FROM_FLASH); }

static void act(int a, source_t source) {
  if (a == A_REMOVE_PERIOD)
    sched_period_remove();
  plat_action(a);
  // Not for a removed period, the ESP sees the registers change. Nor a restart or panel reset, it gets our HELLO
  // when we're back.
  if (source == FROM_USER && a != A_REMOVE_PERIOD && a != A_RESET_PANEL && a != A_RESTART)
    link_action(a);
  ui_value_changed(V_NONE);
}

void do_action(int a) { act(a, FROM_USER); }

void do_action_remote(int a) {
  if (a > A_NONE && a <= A_REMOTE_LAST)
    act(a, FROM_ESP);
}
