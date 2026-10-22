// The panel's settings and a copy of the CU60's, kept in flash as (key, value) records.
//
// The CU60 only sends its settings when it starts, so we need the copy if the panel restarts on its own. Each
// save goes to the next page and the header is written last, so a power cut mid-save leaves the previous copy.
//
// Pages are rated for 10,000 erases. Changes on the panel's keys are saved 5 s after the last one, and so are
// the few bus changes nobody sends again, see save_soon() in unit.c. Other changes from the ESP or the bus, like
// the ESP's CO2 control moving the fans or a Home Assistant automation, at most once an hour. Losing those
// costs little since the CU60 sends its settings again when it starts, and the ESP when we do. That's under 1
// erase a page a day, about 36 years at the rated minimum.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void settings_load(void);           // call before the UI starts
void settings_touch(bool quickly);  // something changed, quickly = save 5 s after the last such change
void settings_poll(void);           // main loop, saves when it's time
void settings_flush(void);          // save now if anything is unsaved, before a restart
uint32_t settings_saves(void);      // since the flash was erased
