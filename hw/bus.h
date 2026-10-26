// Routes a burst from the RS-485 bus: ESP frames to link.c, the rest to the Modbus server and to link.c, which times
// the bus. No hardware access, so the host programs run it too.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// rx[n] came in from first_us, the line went idle at idle_us, ms is the 1 ms tick. Returns the reply's length in tx,
// 0 = none. *modbus: a Modbus reply, link_bus() must see it too. Otherwise a link answer, which waits for the ESP's
// driver to let go of the bus.
int bus_burst(const uint8_t *rx, int n, uint32_t first_us, uint32_t idle_us, uint32_t ms, uint8_t *tx, bool *modbus);
