// The CI600 board, STM32F103 at 72 MHz, or up to 128 with Processor speed.
//
//   LCD    SPI1, PA5 SCK, PA6 MISO, PA7 MOSI, PA4 CS, PA1 reset, PC4/PC5 held low. ILI932x, 320x240
//   keys   PB7 up, PB5 down, PB6 OK, PB9 back, PB8 help, high = pressed
//   LEDs   PC10 alarm, PC11 filter, PC12 OK, low = lit
//   light  backlight PWM on TIM1 CH1, PA8. PB0 on ADC 8 senses the LED current, too noisy to use
//   temp   room thermistor on PC0, ADC 10
//   RS-485 USART1 PA9/PA10, PA12 driver enable, PA11 receiver disable
//   clock  RTC on the 32.768 kHz LSE crystal, local time in Unix-style seconds like stock
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FAULT_MAGIC 0xFA017EC0u   // marks a valid fault record
extern uint32_t fault_record[2];  // FAULT_MAGIC and the faulting PC from startup.c, kept through the reset

// Pins the code drives or reads, as port, pin for stm32f1.h's pin_*()
#define PIN_LCD_RESET GPIOA, 1
#define PIN_LCD_CS GPIOA, 4
#define PIN_KEY_UP GPIOB, 7
#define PIN_KEY_DOWN GPIOB, 5
#define PIN_KEY_OK GPIOB, 6
#define PIN_KEY_BACK GPIOB, 9
#define PIN_KEY_HELP GPIOB, 8
#define PIN_LED_ALARM GPIOC, 10
#define PIN_LED_FILTER GPIOC, 11
#define PIN_LED_OK GPIOC, 12
#define PIN_RS485_RX_OFF GPIOA, 11  // receiver disable
#define PIN_RS485_TX_ON GPIOA, 12   // driver enable

void board_init(void);               // at 72 MHz, or 8 without the crystal
uint32_t board_hz(void);             // core clock
bool board_set_clock(uint32_t mhz);  // from 72 to one of CPU_MHZ. False without the crystal. Before the bus and display
uint8_t board_clock_mark(void);      // what the last run set below, 0 after a power cut. Read it before setting it
void board_set_clock_mark(uint8_t m);  // kept through a reset, for the next start
uint32_t board_ms(void);
uint32_t board_us(void);  // free-running, wraps every 71 minutes, keeps counting during sleep
void board_delay_ms(uint32_t ms);
void board_kick(void);             // feed the watchdog
void board_sleep(void);            // until the next interrupt, the 1 ms tick at the latest
uint16_t board_busy_pm(void);      // time awake since the last call, per mille
void board_isr_time(uint32_t us);  // a long interrupt's run time, so it isn't counted as sleep
uint32_t board_irq_save(void);
void board_irq_restore(uint32_t s);
uint8_t board_keys(void);               // debounced, 1 << IN_x
uint8_t board_key_events(uint8_t *up);  // keys pressed since the last call, *up = keys released
void board_leds(bool alarm, bool filter, bool ok);
void board_backlight(int pct);  // target brightness in %, fades to it. Negative = off
uint16_t board_room_adc(void);  // averaged 12-bit reading of the thermistor
uint32_t board_rtc_get(void);   // local time, Unix-style seconds
void board_rtc_set(uint32_t t);
void board_rtc_poll(void);                      // 1 Hz, sets up the RTC once its crystal is running
uint8_t board_reset_cause(uint32_t *fault_pc);  // RESET_*, call once at start-up
uint32_t board_restarts(void);                  // since the power came on
uint32_t board_entropy(void);                   // differs on every start
void board_reset(void);
void board_bootloader(void);             // restart into the stock bootloader for a firmware update
const void *board_flash(uint32_t addr);  // pointer for reading flash at addr
bool board_flash_erase(uint32_t addr);   // erase and write only work on the settings pages
bool board_flash_write(uint32_t addr, const void *data, uint32_t n);
uint32_t board_stack_free(void);  // bytes of stack never touched so far
