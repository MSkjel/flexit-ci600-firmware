// Values. Changes from the user, the ESP, the bus and flash go through apply(). It clamps to the range and the VF_*
// flags decide if it's saved, written to the bus or sent to the ESP. The clock, ROOM_TEMP, SETPOINT, FW_VERSION and
// PERIOD_* are written directly.
#include "model.h"
#include "clock.h"
#include "link.h"
#include "panel.h"
#include "plat.h"
#include "schedule.h"
#include <string.h>

// Where each PLINK_VALUES value lives. proto/ leaves that out, the ESP has the table too. PANEL_VALUES rows have it.
#define PLINK_FIELDS(F) \
  F(THEME, M.theme) \
  F(BRIGHTNESS, M.brightness) \
  F(IDLE_BRIGHTNESS, M.idle_brightness) \
  F(IDLE_AFTER, M.idle_after) \
  F(SCREENSAVER, M.screensaver) \
  F(HOME_VIEW, M.home_view) \
  F(LANGUAGE, M.language) \
  F(CLOCK_24H, M.clock_24h) \
  F(KEY_HINTS, M.key_hints) \
  F(NIGHT, M.night) \
  F(NIGHT_FROM, M.night_from) \
  F(NIGHT_TO, M.night_to) \
  F(NIGHT_BRIGHTNESS, M.night_brightness) \
  F(NIGHT_IDLE_BRIGHTNESS, M.night_idle_brightness) \
  F(TILE1, M.tiles[0]) \
  F(TILE2, M.tiles[1]) \
  F(TILE3, M.tiles[2]) \
  F(TILE4, M.tiles[3]) \
  F(BUS_ADDR, M.bus_addr) \
  F(KEY_LOCK, M.key_lock) \
  F(LEDS, M.leds) \
  F(ROOM_OFFSET, M.room_offset) \
  F(INDOOR_SOURCE, M.indoor_source) \
  F(ROOM_TEMP, M.t_room) \
  F(FW_VERSION, M.fw_version) \
  F(SETPOINT, M.setpoint) \
  F(FAN_NORM_SUP, M.fan_pct[1][0]) \
  F(FAN_NORM_EXT, M.fan_pct[1][1])
#define ONE(...) +1
_Static_assert(0 PLINK_FIELDS(ONE) == 0 PLINK_VALUES(ONE), "a field for each PLINK_VALUES row");
#undef ONE

// The ESP writes the CX60's values on the bus itself, so it can't set one here too. apply() relies on that.
#define LINK_W_CX60(id, k, lo, hi, fl, reg) || ((fl) & (VF_CX60 | VF_LINK_W)) == (VF_CX60 | VF_LINK_W)
#define LINK_W_CX60_FIELD(id, k, field, ...) LINK_W_CX60(id, k, __VA_ARGS__)
_Static_assert(!(0 PLINK_VALUES(LINK_W_CX60) PANEL_VALUES(LINK_W_CX60_FIELD)), "no value both VF_CX60 and VF_LINK_W");
#undef LINK_W_CX60_FIELD
#undef LINK_W_CX60

// the VALUES() table, by value id
static const val_def_t DEFS[V_FEAT_FIRST] = {
#define X(id, k, lo, hi, fl, reg) [V_##id] = {.min = lo, .max = hi, .key = k, .flags = fl, .cx60 = reg},
#define X_FIELD(id, k, field, ...) X(id, k, __VA_ARGS__)
    PLINK_VALUES(X) PANEL_VALUES(X_FIELD)
#undef X_FIELD
#undef X
};

// FBIT()s of the equipment the unit's DIP switches decide, see equipment.h
#define DIP_BIT(id, icon, title, help, dip) | ((dip) ? FBIT(FEAT_##id) : 0u)
static const uint32_t FROM_DIP = 0u FEATURES(DIP_BIT);
#undef DIP_BIT

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
  if (v == V_FAST_DISPLAY) {  // off over 80 MHz, so apply() refuses it from anywhere
    val_def_t d = DEFS[v];
    d.max = fast_display_ok();
    return d;
  }
  if (v > V_NONE && v < V_FEAT_FIRST)
    return DEFS[v];
  if (v >= V_FEAT_FIRST && v <= V_FEAT_LAST) {
    bool dip = FROM_DIP >> (v - V_FEAT_FIRST) & 1;
    uint16_t fl = VF_PANEL | VF_LAYOUT | (dip ? VF_RO : 0);
    return (val_def_t) {.max = 1, .key = K_FEATURE + (v - V_FEAT_FIRST), .flags = fl};
  }
  if (v >= V_ITEM_FIRST && v <= V_ITEM_LAST) {
    const item_t *it = &M.items[v - V_ITEM_FIRST];
    bool ro = it->type == ITEM_SENSOR || it->type == ITEM_TEXT || (it->flags & ITEMF_RO);
    return (val_def_t) {.min = it->min,
                        .max = it->max,
                        .key = K_ITEM_VALUE + (v - V_ITEM_FIRST),
                        .flags = VF_ESP | VF_LINK_RW | (ro ? VF_RO : 0)};
  }
  return (val_def_t) {0};
}

uint16_t val_cx60_reg(val_t v) {
  val_def_t d = val_def(v);
  return (uint16_t) (d.cx60 + ((d.flags & VF_PER_PANEL) ? M.bus_addr - 1u : 0u));
}

// Linear search, a few microseconds per register the CX60 changed
val_t val_by_cx60(uint16_t reg) {
  for (int v = V_NONE + 1; v < V_FEAT_FIRST; v++)
    if ((DEFS[v].flags & (VF_CX60 | VF_BY_HAND)) == VF_CX60 && val_cx60_reg((val_t) v) == reg)
      return (val_t) v;
  return V_NONE;
}

val_t val_next(val_t v, uint16_t want, uint16_t skip) {
  while (++v < V_ITEM_FIRST) {
    uint16_t f = val_def(v).flags;
    if ((f & want) && !(f & skip))
      return v;
  }
  return V_COUNT;
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
#define F(id, field) \
  case V_##id: \
    return (int32_t) (field);
#define X(id, key, field, ...) F(id, field)
    PLINK_FIELDS(F)
    PANEL_VALUES(X)
#undef X
#undef F
    default:
      return 0;
  }
}

static val_t on_trial = V_NONE;

void val_trial(val_t v) { on_trial = v; }

int32_t val_saved(val_t v) { return v == on_trial ? 0 : val_get(v); }  // what ui_trial() goes back to

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
#define F(id, field) \
  case V_##id: \
    field = x; \
    break;
#define X(id, key, field, ...) F(id, field)
    PLINK_FIELDS(F)
    PANEL_VALUES(X)
#undef X
#undef F
    default:
      break;
  }
}

static int32_t clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }

// Each value's field type, for fit(). A field of another type doesn't compile.
enum { FIELD_I32, FIELD_BOOL, FIELD_U8, FIELD_I16, FIELD_U16, FIELD_U32, FIELD_TYPES };
#define FIELD_TYPE(field) \
  _Generic((field), bool: FIELD_BOOL, uint8_t: FIELD_U8, int16_t: FIELD_I16, uint16_t: FIELD_U16, uint32_t: FIELD_U32)
static const uint8_t TYPES[V_FEAT_FIRST] = {
#define F(id, field) [V_##id] = FIELD_TYPE(field),
#define X(id, key, field, ...) F(id, field)
    PLINK_FIELDS(F) PANEL_VALUES(X)
#undef X
#undef F
};

// x from the CX60 as v's field holds it, so storing it doesn't wrap. Anything but 0 is on for a bool, as the CX60
// takes it. The rest clamp to the type's range.
static int32_t fit(val_t v, int32_t x) {
  static const int32_t LO[FIELD_TYPES] = {[FIELD_I32] = INT32_MIN, [FIELD_I16] = INT16_MIN};
  static const int32_t HI[FIELD_TYPES] = {[FIELD_I32] = INT32_MAX,
                                          [FIELD_U8] = UINT8_MAX,
                                          [FIELD_I16] = INT16_MAX,
                                          [FIELD_U16] = UINT16_MAX,
                                          [FIELD_U32] = INT32_MAX};
  int t = v < V_FEAT_FIRST ? TYPES[v] : FIELD_I32;  // an equipment bit is 0 or 1 already
  return t == FIELD_BOOL ? x != 0 : clamp(x, LO[t], HI[t]);
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
  // No clamp for the ESP's items, or the CX60's values past fit(): its limits are wider than the editors'. Lists
  // (VF_ENUM) clamp, there's nothing to show past their options.
  if (from == FROM_BUS)
    x = fit(v, x);
  if (!(item && from == FROM_ESP) && (from != FROM_BUS || (d.flags & VF_ENUM)))
    x = clamp(x, d.min, d.max);
  // a button press counts even when the value is the same
  bool press = item && M.items[v - V_ITEM_FIRST].type == ITEM_BUTTON;
  if (x == val_get(v) && !press)
    return;
  store(v, x);
  if ((v == V_MONTH || v == V_YEAR) && M.dd > val_def(V_DAY).max)
    M.dd = (uint8_t) val_def(V_DAY).max;  // 31.03 to April is 30.04, not 01.05
  if (from == FROM_FLASH)
    return;  // loading at startup, nothing else runs yet. hw/main.c turns Fast display off if the clock needs it
  if (v == V_CPU_SPEED && M.fast_display && !fast_display_ok())
    apply(V_FAST_DISPLAY, 0, from);  // before plat_value_changed() restarts at the new speed, so it's saved too
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
  } else if (from == FROM_ESP) {
    plat_value_changed(v);
    if (x != want)
      link_value_changed(v);  // clamped, so tell the ESP what we actually stored
  }
  ui_value_changed(v);
}

void val_set(val_t v, int32_t x) { apply(v, x, FROM_USER); }
void val_set_remote(val_t v, int32_t x) { apply(v, x, FROM_ESP); }
void val_update(val_t v, int32_t x) { apply(v, x, FROM_BUS); }
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
