// Keys and idle. UP/DOWN repeat and speed up, BACK held goes home, OK held 2 s is OK_LONG for things that can't
// be undone, HELP held 3 s locks or unlocks the keys.
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { IN_UP, IN_DOWN, IN_OK, IN_BACK, IN_HELP, IN_COUNT };  // key bits, 1 << IN_x

// down/up = key bits pressed/released since the last call, held = held now
void input_keys(uint8_t down, uint8_t up, uint8_t held, uint32_t ms);
void input_tick(uint32_t ms);  // every pass of the main loop, checks for idle
void input_activity(void);     // something new to look at, like an alarm. Wakes up
bool input_idle(void);
int input_backlight(void);      // %, -1 = off
extern uint16_t input_hold_ms;  // how long the repeating key has been held
