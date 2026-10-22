// Panel defaults, night mode and the other small things that follow from the panel settings.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void model_defaults(void);    // everything as on a new panel
void panel_defaults(void);    // factory panel settings, not the bus address or equipment
bool night_active(void);      // night mode is on and it's night now
uint8_t theme_now(void);      // the theme, or the night theme
uint16_t idle_seconds(void);  // the Idle after setting, 0 = never
int16_t indoor_temp(void);    // extract air since that's the whole home, or this panel's sensor if picked
int heat_recovery(void);      // % = (supply - outdoor) / (extract - outdoor) with the heater off, -1 if unknown
int filter_days_left(void);   // until the filters are due, 0 when they are
int filter_life(void);        // % of the filter interval left
bool alarm_pops(int code);    // alarms with this code pop up
int alarm_new_pops(void);     // a new popup alarm's code since the last call, else 0. It wakes the screen
void panel_leds(bool out[3], uint32_t ms);  // alarm, filter and OK lights now
