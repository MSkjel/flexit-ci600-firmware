// The CI600's TFT, an ILI932x on SPI1, 320x240 landscape, RGB565.
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool lcd_init(void);  // false if the controller id is one the stock firmware doesn't know either
// Sends the area's pixels row by row over DMA and returns. done() is called from the interrupt when they're
// all out, don't touch px before that. Waits for the previous transfer first.
void lcd_draw(int x1, int y1, int x2, int y2, const uint16_t *px, void (*done)(void));
extern volatile uint32_t lcd_send_us;  // time spent sending pixels, us, for PERF
void lcd_fast(bool on);                // pixels at 18 MHz instead of 9, from the next area on
void lcd_wait(void);                   // until the last area is out
