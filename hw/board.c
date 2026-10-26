// see board.h
#include "board.h"
#include "input.h"  // IN_*
#include "layout.h"
#include "model.h"  // RESET_*
#include "stm32f1.h"

static uint32_t hz = 8000000;
static volatile uint32_t ms;
static volatile uint8_t keys, key_down, key_up;  // debounced state, presses and releases not read yet
static uint8_t key_hist[IN_COUNT];
static volatile uint16_t bl_now, bl_want;  // backlight compare, now and where it's fading to

// ---------------------------------------------------------------- clocks and pins
static void clocks(void) {
  RCC->CR |= RCC_CR_HSION;  // run on HSI while the PLL is reconfigured
  while (!(RCC->CR & RCC_CR_HSIRDY)) {
  }
  RCC->CFGR &= ~RCC_CFGR_SW;
  while (RCC->CFGR & RCC_CFGR_SWS) {
  }
  RCC->CR &= ~RCC_CR_PLLON;
  while (RCC->CR & RCC_CR_PLLRDY) {
  }
  RCC->CR |= RCC_CR_HSEON;  // 8 MHz crystal
  for (uint32_t i = 0; !(RCC->CR & RCC_CR_HSERDY); i++)
    if (i > 2000000)
      return;  // no crystal, stay on HSI at 8 MHz
  FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
  RCC->CFGR = RCC_CFGR_PLLMUL9 | RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_ADCPRE_DIV6;  // 72 MHz
  RCC->CR |= RCC_CR_PLLON;
  while (!(RCC->CR & RCC_CR_PLLRDY)) {
  }
  RCC->CFGR |= RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
  }
  hz = 72000000;
}

uint32_t board_hz(void) { return hz; }

// Same as stock's gpio_init and gpio_init_early. Level goes in before mode so outputs don't glitch.
static const struct {
  gpio_t *port;
  uint8_t pin, mode, level;  // output level, or the pull for inputs, 1 = up
} PINS[] = {
    {PIN_LCD_RESET, PIN_OUT, 1},      // released
    {PIN_LCD_CS, PIN_OUT, 1},         // idle
    {GPIOA, 5, PIN_AF, 0},            // SPI1 SCK
    {GPIOA, 6, PIN_IN_PULL, 1},       // SPI1 MISO
    {GPIOA, 7, PIN_AF, 0},            // SPI1 MOSI
    {GPIOC, 4, PIN_OUT, 0},           // LCD lines the stock firmware holds low
    {GPIOC, 5, PIN_OUT, 0},           //
    {GPIOA, 8, PIN_AF, 0},            // TIM1 CH1, backlight
    {GPIOB, 0, PIN_AIN, 0},           // backlight LED current, not read
    {GPIOC, 0, PIN_AIN, 0},           // room thermistor
    {GPIOA, 9, PIN_AF, 0},            // USART1 TX
    {GPIOA, 10, PIN_IN, 0},           // USART1 RX
    {PIN_RS485_RX_OFF, PIN_OUT, 0},   //
    {PIN_RS485_TX_ON, PIN_OUT, 0},    //
    {PIN_KEY_DOWN, PIN_IN_PULL, 0},   // keys are high when pressed
    {PIN_KEY_OK, PIN_IN_PULL, 0},     //
    {PIN_KEY_UP, PIN_IN_PULL, 0},     //
    {PIN_KEY_HELP, PIN_IN_PULL, 0},   //
    {PIN_KEY_BACK, PIN_IN_PULL, 0},   //
    {PIN_LED_ALARM, PIN_OUT_OD, 1},   // LEDs are lit when low
    {PIN_LED_FILTER, PIN_OUT_OD, 1},  //
    {PIN_LED_OK, PIN_OUT_OD, 1},      //
    {GPIOA, 0, PIN_IN_PULL, 1},       // not used from here on
    {GPIOA, 3, PIN_IN, 0},            //
    {GPIOB, 10, PIN_OUT, 0},          //
    {GPIOB, 11, PIN_IN, 0},           //
    {GPIOB, 12, PIN_IN_PULL, 0},      //
    {GPIOB, 13, PIN_IN_PULL, 0},      //
    {GPIOB, 14, PIN_IN_PULL, 0},      //
    {GPIOB, 15, PIN_IN_PULL, 0},      //
    {GPIOC, 1, PIN_OUT, 0},           //
    {GPIOC, 2, PIN_IN, 0},            //
    {GPIOC, 3, PIN_OUT, 1},           //
    {GPIOC, 6, PIN_IN_PULL, 0},       //
    {GPIOC, 7, PIN_IN_PULL, 0},       //
    {GPIOC, 8, PIN_IN_PULL, 0},       //
    {GPIOC, 9, PIN_IN_PULL, 0},       //
    {GPIOC, 13, PIN_IN, 0},           //
};

static void pins(void) {
  RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN;
  for (unsigned i = 0; i < LEN(PINS); i++) {
    pin_set(PINS[i].port, PINS[i].pin, PINS[i].level);
    pin_mode(PINS[i].port, PINS[i].pin, PINS[i].mode);
  }
}

// ---------------------------------------------------------------- microseconds
// TIM2 at 1 MHz, the overflow interrupt makes it 32 bits. Not the cycle counter, it stops while the core sleeps
// unless DBG_SLEEP is set, and DBG_SLEEP keeping the bus clock on was most of what made the chip warm.
static volatile uint32_t us_hi;

void TIM2_IRQHandler(void) {
  TIM2->SR = 0;
  us_hi++;
}

uint32_t board_us(void) {
  uint32_t h, l, wrapped;
  do {
    h = us_hi;
    l = TIM2->CNT;
    wrapped = TIM2->SR & TIM_SR_UIF;
  } while (h != us_hi);
  if (wrapped && l < 0x8000)
    h++;  // just wrapped and the interrupt hasn't run yet
  return h << 16 | l;
}

static void us_init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
  TIM2->PSC = hz / 1000000 - 1;  // APB1 is /2 but its timers get x2 back, so this is the core clock
  TIM2->ARR = 0xFFFF;
  TIM2->EGR = TIM_EGR_UG;  // load the prescaler
  TIM2->SR = 0;
  TIM2->DIER = TIM_DIER_UIE;
  TIM2->CR1 = TIM_CR1_CEN;
  irq_enable(IRQ_TIM2);
}

// ---------------------------------------------------------------- sleeping
// Awake time is the main loop plus long interrupts. The bus adds its time with board_isr_time(), the 1 ms tick
// and the display DMA are too short to count.
static volatile uint32_t isr_us;
static uint32_t slept_us, busy_from_us;
static bool debugger;

void board_sleep(void) {
  // A debugger needs DBG_SLEEP to read memory while the core sleeps. Only set while one is attached, since
  // flash.py leaves the bit set and it survives a reset.
  bool dbg = DHCSR & DHCSR_C_DEBUGEN;
  if (dbg != debugger) {
    debugger = dbg;
    if (dbg)
      DBGMCU_CR |= DBGMCU_CR_SLEEP;
    else
      DBGMCU_CR &= ~DBGMCU_CR_SLEEP;
  }
  uint32_t t = board_us();
  __asm volatile("wfi");
  slept_us += board_us() - t;
}

void board_isr_time(uint32_t us) { isr_us += us; }

uint16_t board_busy_pm(void) {
  uint32_t s = irq_save();
  uint32_t now = board_us(), span = now - busy_from_us, isr = isr_us, slept = slept_us;
  busy_from_us = now;
  isr_us = slept_us = 0;
  irq_restore(s);
  uint32_t idle = slept > isr ? slept - isr : 0;
  if (!span || idle > span)
    return 0;
  return (uint16_t) (1000 - (uint64_t) idle * 1000 / span);
}

// ---------------------------------------------------------------- 1 ms tick
void SysTick_Handler(void) {
  ms++;
  static const struct {
    gpio_t *g;
    uint8_t pin;
  } K[IN_COUNT] = {
      [IN_UP] = {PIN_KEY_UP},     [IN_DOWN] = {PIN_KEY_DOWN}, [IN_OK] = {PIN_KEY_OK},
      [IN_BACK] = {PIN_KEY_BACK}, [IN_HELP] = {PIN_KEY_HELP},
  };
  for (int k = 0; k < IN_COUNT; k++) {  // debounce: 8 equal samples in a row
    uint8_t bit = (uint8_t) (1u << k);
    key_hist[k] = (uint8_t) (key_hist[k] << 1 | pin_get(K[k].g, K[k].pin));
    if (key_hist[k] == 0xFF && !(keys & bit)) {
      keys |= bit;
      key_down |= bit;
    } else if (!key_hist[k] && (keys & bit)) {
      keys &= (uint8_t) ~bit;
      key_up |= bit;
    }
  }
  if (ms % 8 == 0 && bl_now != bl_want) {  // fade, about 1/4 s from dark to full
    int step = (bl_want > bl_now ? bl_want - bl_now : bl_now - bl_want) / 8 + 1;
    bl_now = (uint16_t) (bl_want > bl_now ? bl_now + step : bl_now - step);
    TIM1->CCR1 = bl_now;
  }
}

uint32_t board_ms(void) { return ms; }

void board_delay_ms(uint32_t d) {
  uint32_t t = ms;
  while (ms - t < d)
    board_kick();
}

uint32_t board_irq_save(void) { return irq_save(); }
void board_irq_restore(uint32_t s) { irq_restore(s); }
uint8_t board_keys(void) { return keys; }

// The tick collects the edges, or a quick press while the UI is drawing would get lost.
uint8_t board_key_events(uint8_t *up) {
  uint32_t s = irq_save();
  uint8_t d = key_down;
  *up = key_up;
  key_down = key_up = 0;
  irq_restore(s);
  return d;
}

void board_leds(bool alarm, bool filter, bool ok) {
  pin_set(PIN_LED_ALARM, !alarm);
  pin_set(PIN_LED_FILTER, !filter);
  pin_set(PIN_LED_OK, !ok);
}

// ---------------------------------------------------------------- backlight
// TIM1 CH1 on PA8 at ~68 kHz switches the backlight's boost converter directly. 100 % duty shorts the coil
// across the supply and the chip browns out, so the duty is limited.
//
// Stock regulates the LED current on PB0, but it samples 7.5 ADC cycles at the start of a period and reads about
// 2 counts at any duty it uses, way below its targets of 240 full and 6 dimmed. So stock just sits at the limit,
// compare 210. PB0 is too noisy and drifts too much to regulate on, and it doesn't show where the light starts
// either, the average rises steadily from compare 140 up. We set the duty directly and never read PB0.
// Brightness follows the square of the %, which looks about even.
#define BL_TOP 1050
#define BL_DARK 15   // compare at 0 %, the panel we tried was still readable at 10
#define BL_FULL 225  // compare at 100 %, a bit brighter than stock's 210

static void backlight_init(void) {
  RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
  TIM1->PSC = 0;
  TIM1->ARR = BL_TOP;
  TIM1->CCMR1 = TIM_CCMR1_OC1M_PWM1 | TIM_CCMR1_OC1PE;
  TIM1->CCER = TIM_CCER_CC1E;
  TIM1->CCR1 = 0;
  TIM1->BDTR = TIM_BDTR_MOE;  // main output enable, needed on the advanced timer
  TIM1->EGR = TIM_EGR_UG;
  TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

void board_backlight(int pct) {
  if (pct > 100)
    pct = 100;
  // 0 % is the lowest dim level and still readable, below 0 is off
  bl_want = pct < 0 ? 0 : (uint16_t) (BL_DARK + (uint32_t) (pct * pct) * (BL_FULL - BL_DARK) / 10000);
}

// ---------------------------------------------------------------- room thermistor (PC0, ADC1 channel 10)
// The ADC is only powered while reading, once a second. Left on it draws a milliampere.
static void adc_init(void) {
  RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
  ADC1->CR2 = ADC_CR2_ADON;
  board_delay_ms(1);
  ADC1->CR2 |= ADC_CR2_RSTCAL;
  while (ADC1->CR2 & ADC_CR2_RSTCAL) {
  }
  ADC1->CR2 |= ADC_CR2_CAL;
  while (ADC1->CR2 & ADC_CR2_CAL) {
  }
  ADC1->SMPR1 = ADC_SMP_239_5 << 0;  // channel 10
  ADC1->SQR1 = 0;
  ADC1->SQR3 = 10;
  ADC1->CR2 |= ADC_CR2_EXTSEL_SWSTART | ADC_CR2_EXTTRIG;
  ADC1->CR2 &= ~ADC_CR2_ADON;
}

uint16_t board_room_adc(void) {  // 16 readings averaged
  ADC1->CR2 |= ADC_CR2_ADON;
  uint32_t t = board_us();
  while (board_us() - t < 2) {
  }  // the ADC needs 1 us after power-up
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) {
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC)) {
    }
    sum += ADC1->DR & 0xFFF;
  }
  ADC1->CR2 &= ~ADC_CR2_ADON;
  return (uint16_t) (sum / 16);
}

// ---------------------------------------------------------------- RTC
// The 32 kHz crystal can take seconds to start after a power cut, and the bus can't wait that long. We start it
// here and board_rtc_poll() sets up the RTC once it runs. A software clock keeps time until then, or for good
// if there's no crystal.
static bool rtc_ok;                  // the RTC is running on the crystal
static uint32_t soft_time, soft_ms;  // software clock, the time was soft_time at tick soft_ms

static bool rtc_wait(void) {  // wait for the last RTC write to finish
  for (uint32_t i = 0; i < 100000; i++)
    if (RTC->CRL & RTC_CRL_RTOFF)
      return true;
  return false;
}

static bool rtc_sync(void) {  // wait until the registers show the counter again after a reset
  RTC->CRL &= ~RTC_CRL_RSF;
  for (uint32_t i = 0; i < 1000000; i++)
    if (RTC->CRL & RTC_CRL_RSF)
      return true;
  return false;
}

// a two-word register pair like PRLH/PRLL, in configuration mode
static void rtc_write(volatile uint32_t *hi, uint32_t v) {
  RTC->CRL |= RTC_CRL_CNF;
  hi[0] = v >> 16;
  hi[1] = v & 0xFFFF;
  RTC->CRL &= ~RTC_CRL_CNF;
}

static bool rtc_on_crystal(void) {
  return (RCC->BDCR & (RCC_BDCR_RTCEN | RCC_BDCR_RTCSEL | RCC_BDCR_LSERDY)) ==
         (RCC_BDCR_RTCEN | RCC_BDCR_RTCSEL_LSE | RCC_BDCR_LSERDY);
}

static void rtc_init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
  PWR->CR |= PWR_CR_DBP;  // backup domain writable
  if (rtc_on_crystal())
    rtc_ok = rtc_sync();  // still running from before the reset
  else
    RCC->BDCR |= RCC_BDCR_LSEON;
}

void board_rtc_poll(void) {
  if (rtc_ok)
    return;
  uint32_t s = (ms - soft_ms) / 1000;  // move whole seconds to soft_time, ms - soft_ms wraps after 49.7 days
  soft_time += s;
  soft_ms += s * 1000;
  if (!(RCC->BDCR & RCC_BDCR_LSERDY))
    return;
  uint32_t now = board_rtc_get();  // carry the software clock's time over to the RTC
  RCC->BDCR |= RCC_BDCR_RTCSEL_LSE | RCC_BDCR_RTCEN;
  if (!rtc_wait())
    return;
  rtc_write(&RTC->PRLH, 0x7FFF);  // 32768 Hz -> seconds
  if (!rtc_wait() || !rtc_sync())
    return;
  rtc_ok = true;
  board_rtc_set(now);
}

uint32_t board_rtc_get(void) {
  if (!rtc_ok)
    return soft_time + (ms - soft_ms) / 1000;
  uint32_t h, l;
  do {
    h = RTC->CNTH;
    l = RTC->CNTL;
  } while (h != RTC->CNTH);
  return h << 16 | (l & 0xFFFF);
}

void board_rtc_set(uint32_t t) {
  soft_time = t;
  soft_ms = ms;
  if (!rtc_ok || !rtc_wait())
    return;
  rtc_write(&RTC->CNTH, t);
  rtc_wait();
}

// ---------------------------------------------------------------- reset cause
// Kept through a reset. Starts over at power on, and on the first start after flashing, when it holds whatever
// was in that RAM.
#define RESTARTS_MAGIC 0x52535443u
__attribute__((section(".noinit"))) static struct {
  uint32_t magic, n;
} restarts;

uint8_t board_reset_cause(uint32_t *fault_pc) {
  uint32_t csr = RCC->CSR;
  RCC->CSR |= RCC_CSR_RMVF;
  uint8_t cause = fault_record[0] == FAULT_MAGIC ? RESET_FAULT
                  : csr & RCC_CSR_IWDGRSTF       ? RESET_WATCHDOG
                  : csr & RCC_CSR_SFTRSTF        ? RESET_SOFTWARE
                  : csr & RCC_CSR_PORRSTF        ? RESET_POWER
                                                 : RESET_PIN;
  *fault_pc = cause == RESET_FAULT ? fault_record[1] : 0;
  fault_record[0] = 0;
  if (cause == RESET_POWER || restarts.magic != RESTARTS_MAGIC) {
    restarts.magic = RESTARTS_MAGIC;
    restarts.n = 0;
  } else {
    restarts.n++;
  }
  return cause;
}

uint32_t board_restarts(void) { return restarts.n; }

// ---------------------------------------------------------------- entropy
__attribute__((section(".noinit"))) static uint32_t starts;  // not zeroed, counts on from whatever power-up left

uint32_t board_entropy(void) {
  const uint32_t *uid = (const uint32_t *) 0x1FFFF7E8;  // the chip's unique ID
  uint32_t h = ++starts * 0x9E3779B9u;
  for (int i = 0; i < 3; i++)
    h = (h ^ uid[i]) * 0x85EBCA6Bu;
  h ^= SYSTICK->VAL ^ board_us();  // how long start-up took, to the cycle
  return h ^ h >> 15;
}

// ---------------------------------------------------------------- watchdog, reset, flash
static void watchdog_init(void) {
  IWDG->KR = IWDG_KEY_ACCESS;
  IWDG->PR = IWDG_PR_DIV64;  // 40 kHz / 64
  IWDG->RLR = 1250;          // 2 s
  IWDG->KR = IWDG_KEY_RELOAD;
  IWDG->KR = IWDG_KEY_START;
}

void board_kick(void) { IWDG->KR = IWDG_KEY_RELOAD; }

void board_reset(void) {
  SCB->AIRCR = SCB_AIRCR_SYSRESETREQ;
  for (;;) {
  }
}

void board_bootloader(void) {
  // tells the stock bootloader to stay in update mode. rtc_init() left the backup domain writable
  BKP_DR(1) = 0xDEAD;
  board_reset();
}

static void flash_unlock(void) {
  if (FLASH->CR & FLASH_CR_LOCK) {
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
  }
}

static bool flash_wait(void) {
  while (FLASH->SR & FLASH_SR_BSY) {
  }
  bool ok = !(FLASH->SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR));
  FLASH->SR = FLASH_SR_EOP | FLASH_SR_WRPRTERR | FLASH_SR_PGERR;
  return ok;
}

static bool settings_page(uint32_t addr, uint32_t n) {
  return addr >= FLASH_SETTINGS && addr + n <= FLASH_SETTINGS + FLASH_SETTINGS_PAGES * FLASH_PAGE;
}

const void *board_flash(uint32_t addr) { return (const void *) (uintptr_t) addr; }

bool board_flash_erase(uint32_t addr) {
  if (!settings_page(addr, FLASH_PAGE))
    return false;
  board_kick();
  flash_unlock();
  flash_wait();
  FLASH->CR |= FLASH_CR_PER;
  FLASH->AR = addr;
  FLASH->CR |= FLASH_CR_STRT;
  bool ok = flash_wait();
  FLASH->CR &= ~FLASH_CR_PER;
  FLASH->CR |= FLASH_CR_LOCK;
  return ok;
}

bool board_flash_write(uint32_t addr, const void *data, uint32_t n) {
  if (!settings_page(addr, n))
    return false;
  const uint8_t *p = data;
  bool ok = true;
  flash_unlock();
  FLASH->CR |= FLASH_CR_PG;
  for (uint32_t i = 0; i < n && ok; i += 2) {
    uint16_t v = (uint16_t) (p[i] | (i + 1 < n ? p[i + 1] : 0xFF) << 8);
    *(volatile uint16_t *) (uintptr_t) (addr + i) = v;
    ok = flash_wait();
  }
  FLASH->CR &= ~FLASH_CR_PG;
  FLASH->CR |= FLASH_CR_LOCK;
  return ok;
}

void board_init(void) {
  clocks();
  pins();
  DBGMCU_CR &= ~DBGMCU_CR_SLEEP_STOP_STANDBY;  // a debugger may have left them set
  // Interrupt priorities. The us clock first since the others read it, then the bus. The tick and the display
  // DMA are short and can wait.
  NVIC_IPR8(IRQ_TIM2) = 0x00;
  NVIC_IPR8(IRQ_USART1) = 0x10;
  SCB_SHPR3 = (SCB_SHPR3 & 0x00FFFFFF) | 0x20u << 24;
  NVIC_IPR8(IRQ_DMA1_CH3) = 0x20;
  SYSTICK->LOAD = hz / 1000 - 1;
  SYSTICK->VAL = 0;
  SYSTICK->CTRL = SYSTICK_CTRL_ON;
  us_init();
  rtc_init();
  backlight_init();
  adc_init();
  watchdog_init();
}
