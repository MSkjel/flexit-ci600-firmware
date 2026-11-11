// Screenshots for the ESP, see K_SHOT in plink_keys.h. A tile at a time is drawn again and caught on its way to the
// LCD, so the screen doesn't change and nothing is kept but the tile being sent. Only the tiles that changed on the
// LCD since they were last sent go, unless the ESP asks for all.
#pragma once
#include "lvgl.h"
#include <stdint.h>

void shot_ask(uint32_t v);        // K_SHOT
void shot_reset(void);            // the ESP restarted, it asks again
void shot_changed(int x, int y);  // the LCD's tile at x, y got other pixels, see sent.h
// The display's flush, px area a's pixels row by row: the tile we wait for is encoded from it if it's all there.
// Not while the LCD is held, it would be what isn't shown.
void shot_flushed(const lv_area_t *a, const lv_color_t *px);
void shot_poll(void);  // before LVGL draws: the next tile to draw again
// The next of the tiles' PIC_DATA and SHOT_DONE into out, at most room bytes, to go after the other messages. Returns
// its length.
int shot_take(uint8_t *out, int room);
unsigned shot_waiting(void);  // bytes to come, so the ESP leaves room for them
