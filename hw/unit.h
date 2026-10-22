// The CU60 side of the model. Registers from the bus go into the model, panel changes go back as registers.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FW_VERSION \
  (FW_MAJOR << 8 | FW_MINOR)  // input register 3 at discovery, stock 7.x is 0x07xx. FW_* from config.mk

void unit_init(void);
void unit_start(void);           // call after settings_load
void unit_poll(void);            // main loop, applies register changes to the model
void unit_second(void);          // 1 Hz, checks whether the CU60 is still there
bool unit_cached(uint16_t reg);  // reg is a CU60 setting worth keeping in flash
void unit_cache_load(uint16_t reg, int16_t value);
