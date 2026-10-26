// The CX60 side of the model. Registers from the bus go into the model, panel changes go back as registers.
#pragma once
#include "model.h"
#include "version.h"  // FW_MAJOR, FW_MINOR and FW_PATCH, made by config.mk

#define FW_VERSION (FW_MAJOR << 8 | FW_MINOR)  // input register 3 at discovery, stock 7.x is 0x07xx

void unit_init(void);
void unit_start(void);             // call after settings_load
void unit_poll(void);              // main loop, applies register changes to the model
void unit_second(void);            // 1 Hz, checks whether the CX60 is still there
bool unit_cached(uint16_t reg);    // reg is a CX60 setting worth keeping in flash
int16_t unit_reg(uint16_t reg);    // our copy of a CX60 register
void unit_value_changed(val_t v);  // plat_value_changed()'s CX60 part, writes v if the CX60 keeps it
void unit_action(int a);           // plat_action()'s CX60 part
