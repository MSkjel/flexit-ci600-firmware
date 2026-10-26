// hw/board.h for the host programs. Time only moves when the program moves it, and flash is a RAM array.
#include "board.h"
#include "layout.h"
#include "rs485.h"
#include <string.h>

uint32_t stub_ms, stub_rtc;
uint8_t stub_flash[FLASH_SETTINGS_PAGES * FLASH_PAGE];  // the settings pages
int stub_flash_writes_left = -1;                        // power fails after this many more writes, -1 = never
int stub_flash_erases;
uint32_t stub_flash_bad;  // a page that won't program, 0 = none
bool stub_bus_quiet = true;

uint32_t board_ms(void) { return stub_ms; }
uint32_t board_us(void) { return stub_ms * 1000u; }
void board_delay_ms(uint32_t ms) { stub_ms += ms; }
void board_kick(void) {}
uint32_t board_irq_save(void) { return 0; }
void board_irq_restore(uint32_t s) { (void) s; }
uint8_t board_keys(void) { return 0; }
void board_leds(bool alarm, bool filter, bool ok) {
  (void) alarm;
  (void) filter;
  (void) ok;
}
void board_backlight(int pct) { (void) pct; }
uint16_t board_room_adc(void) { return 1990; }
uint32_t board_rtc_get(void) { return stub_rtc; }
void board_rtc_set(uint32_t t) { stub_rtc = t; }
void board_rtc_poll(void) {}
int stub_resets;
void board_reset(void) { stub_resets++; }
void board_bootloader(void) {}
bool rs485_wait_quiet(uint32_t ms) {
  (void) ms;
  return stub_bus_quiet;
}

const void *board_flash(uint32_t addr) { return stub_flash + (addr - FLASH_SETTINGS); }

bool board_flash_erase(uint32_t addr) {
  if (addr < FLASH_SETTINGS || addr - FLASH_SETTINGS >= sizeof stub_flash)
    return false;
  if (stub_flash_writes_left == 0)
    return false;
  stub_flash_erases++;
  memset(stub_flash + (addr - FLASH_SETTINGS) / FLASH_PAGE * FLASH_PAGE, 0xFF, FLASH_PAGE);
  return true;
}

bool board_flash_write(uint32_t addr, const void *data, uint32_t n) {
  if (addr < FLASH_SETTINGS || addr - FLASH_SETTINGS + n > sizeof stub_flash || stub_flash_writes_left == 0)
    return false;
  if (stub_flash_bad && addr - stub_flash_bad < FLASH_PAGE)
    return false;
  if (stub_flash_writes_left > 0)
    stub_flash_writes_left--;
  memcpy(stub_flash + (addr - FLASH_SETTINGS), data, n);
  return true;
}
