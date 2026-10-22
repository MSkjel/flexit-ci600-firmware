// Optional equipment. Feature bits, value ids and the System > Equipment menu come from this list. Anything in
// the UI with .need = FBIT(FEAT_x) is hidden without it.
//
// X(id, icon, title, help), title and help are string names from ui/lang.h
#pragma once

#define FEATURES(X) \
  X(ESP, IC_WIFI, EQUIP_ESP, EQUIP_ESP_HELP) \
  X(EC_FANS, IC_FAN, EQUIP_EC_FANS, EQUIP_EC_FANS_HELP) \
  X(WATER_HEATER, IC_WATER, EQUIP_WATER_HEATER, EQUIP_WATER_HEATER_HELP) \
  X(COOLING, IC_SNOW, COOLING, EQUIP_COOLING_HELP)

#define FEAT_ENUM(id, ...) FEAT_##id,
enum { FEATURES(FEAT_ENUM) FEAT_COUNT };
#undef FEAT_ENUM

#define FBIT(f) (1u << (f))
#define HAS(f) ((M.features >> (f)) & 1u)
