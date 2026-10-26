// Optional equipment. Feature bits, value ids and the System > Equipment menu come from this list. Anything in
// the UI with .need = FBIT(FEAT_x) is hidden without it.
//
// X(id, icon, title, help, dip). title and help are string ids from ui/lang.h. dip 1: the unit's DIP switches say
// if it's there (hw/unit.c), read-only on the panel.
#pragma once

#define FEATURES(X) \
  X(ESP, IC_WIFI, EQUIP_ESP, EQUIP_ESP_HELP, 0) \
  X(EC_FANS, IC_FAN, EQUIP_EC_FANS, EQUIP_EC_FANS_HELP, 1) \
  X(WATER_HEATER, IC_WATER, EQUIP_WATER_HEATER, EQUIP_WATER_HEATER_HELP, 1) \
  X(COOLING, IC_SNOW, COOLING, EQUIP_COOLING_HELP, 0)

#define FEAT_ENUM(id, ...) FEAT_##id,
enum { FEATURES(FEAT_ENUM) FEAT_COUNT };
#undef FEAT_ENUM

#define FBIT(f) (1u << (f))
#define HAS(f) ((M.features >> (f)) & 1u)
#define HAS_ALL(mask) ((M.features & (mask)) == (mask))  // every feature in an FBIT() mask
#define HAS_ANY(mask) ((M.features & (mask)) != 0)       // any of them
