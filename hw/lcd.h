// The CI600's TFT, an ILI932x on SPI1, 320x240 landscape, RGB565.
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool lcd_init(void);  // false if the controller id is one the stock firmware doesn't know either
// Sends an area's pixels over DMA and returns. Its rows are pitch pixels apart at px. done() is called from the
// interrupt when they're all out, don't touch px before that. It may send the next area. Waits for the last one first.
void lcd_draw(int x1, int y1, int x2, int y2, const uint16_t *px, unsigned pitch, void (*done)(void));
extern volatile uint32_t lcd_send_us;  // time spent sending pixels, us, for PERF
void lcd_wait(void);                   // until the last area is out, and what its done() sent
uint32_t lcd_us(uint32_t px);          // how long that many pixels take to send

// SPI speeds, the most each may run at, lcd_spi_div() picks the divisor. At 72 MHz that's stock's core / 16 and / 8,
// and / 4 for the pixels, twice stock's.
#define LCD_INIT_HZ 8000000u    // setting the display up: core / 16, up to 128 MHz
#define LCD_REG_HZ 16000000u    // registers: core / 8, up to 128 MHz
#define LCD_PIXEL_HZ 20000000u  // pixels: core / 4 up to 80 MHz, then / 8. The controller is rated for 10, 36 garbles

// The SPI's divisor at core clock hz for want: the smallest power of two from 2 to 256 that keeps it at or under want
static inline uint32_t lcd_spi_div(uint32_t hz, uint32_t want) {
  uint32_t div = 2;
  while (div < 256 && hz > (uint64_t) want * div)
    div *= 2;
  return div;
}
