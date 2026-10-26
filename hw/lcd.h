// The CI600's TFT, an ILI932x on SPI1, 320x240 landscape, RGB565.
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool lcd_init(void);  // false if the controller id is one the stock firmware doesn't know either
// Sends the area's pixels row by row over DMA and returns. done() is called from the interrupt when they're
// all out, don't touch px before that. Waits for the previous transfer first.
void lcd_draw(int x1, int y1, int x2, int y2, const uint16_t *px, void (*done)(void));
extern volatile uint32_t lcd_send_us;  // time spent sending pixels, us, for PERF
void lcd_fast(bool on);                // pixels at up to LCD_FAST_HZ instead of LCD_REG_HZ, from the next area on
void lcd_wait(void);                   // until the last area is out

// SPI speeds, the most each may run at, lcd_spi_div() picks the divisor. At 72 MHz that's stock's core / 16 and / 8,
// and / 4 for Fast display.
#define LCD_SPI_MAX_HZ 20000000u  // never faster, whatever the settings say
#define LCD_INIT_HZ 8000000u      // setting the display up: core / 16, up to 128 MHz
#define LCD_REG_HZ 16000000u      // registers and pixels: core / 8, up to 128 MHz
#define LCD_FAST_HZ 20000000u     // Fast display's pixels: core / 4 up to 80 MHz, then / 8

// The SPI's divisor at core clock hz for want: the smallest power of two from 2 to 256 that keeps it at or under want
// and LCD_SPI_MAX_HZ
static inline uint32_t lcd_spi_div(uint32_t hz, uint32_t want) {
  if (want > LCD_SPI_MAX_HZ)
    want = LCD_SPI_MAX_HZ;
  uint32_t div = 2;
  while (div < 256 && hz > (uint64_t) want * div)
    div *= 2;
  return div;
}
