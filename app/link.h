// Our end of the ESP link, see proto/plink.h, pbulk.h and pbus.h. Also watches the bus for when the link may use it.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pbulk.h"
#include "plink.h"

void link_init(void);
void link_poll(void);    // call often
void link_second(void);  // 1 Hz
void link_send(uint16_t key, uint32_t value);
void link_action(int action);          // the user ran an action
bool link_compatible(void);            // the ESP's HELLO had our link version
const char *link_item_help(int item);  // item help from the ESP. Asks for it, NULL until it arrives

// From the RX interrupt, a bus burst that isn't a link frame. Our own replies too. now_ms is the 1 ms tick.
void link_bus(const uint8_t *b, int n, uint32_t start_us, uint32_t idle_us, uint32_t now_ms);
// From the RX interrupt, a frame to PBULK_ADDR. idle_us = line went idle after it. Returns answer length, 0 = none
int link_bulk_frame(const uint8_t *f, int n, uint32_t idle_us, uint32_t now_ms, uint8_t *answer);

// for the Bus page
const pbulk_panel_t *link_bulk_state(void);
const pbus_panel_t *link_bus_state(void);
const plink_t *link_queue(void);
