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
// The panel's lights, in the order M.leds has them
enum { LIGHT_ALARM, LIGHT_FILTER, LIGHT_OK, N_LIGHTS };
void panel_leds(bool out[N_LIGHTS], uint32_t ms);  // the lights as they are now

// The clock mark a run leaves for the next start, see board_set_clock_mark(). Two CPU_MHZ[] indexes: the speed it
// runs at, and the one the next start tries, not saved yet. 0 for no trial.
#define CLOCK_MARK(ran, trial) ((uint8_t) ((ran) | (trial) << 4))
#define CLOCK_RAN(mark) ((mark) & 15)
#define CLOCK_TRIAL(mark) ((mark) >> 4)
// MHz to start at, from Processor speed (a CPU_MHZ[] index), the last run's mark and its reset cause (RESET_*). A
// crash or the watchdog over 72 sets *revert, to set it back to 72.
int cpu_clock_choice(uint8_t speed, uint8_t last, uint8_t reset_cause, bool *revert);
int cpu_speed_of(int mhz);   // the CPU_MHZ[] index of mhz, 0 if there's none
bool fast_display_ok(void);  // Fast display may be on at this Processor speed, up to 80 MHz
