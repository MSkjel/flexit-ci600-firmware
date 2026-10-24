// The STM32F103 registers we use, from RM0008. No vendor library.
#pragma once
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *) (a))

typedef struct {
  volatile uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR, AHBENR, APB2ENR, APB1ENR, BDCR, CSR;
} rcc_t;
typedef struct {
  volatile uint32_t ACR, KEYR, OPTKEYR, SR, CR, AR, RES, OBR, WRPR;
} flash_t;
typedef struct {
  volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} gpio_t;
typedef struct {
  volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
} usart_t;
typedef struct {
  volatile uint32_t CR1, CR2, SR, DR, CRCPR, RXCRCR, TXCRCR, I2SCFGR, I2SPR;
} spi_t;
typedef struct {
  volatile uint32_t CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2, CCER, CNT, PSC, ARR, RCR, CCR1, CCR2, CCR3, CCR4, BDTR,
      DCR, DMAR;
} tim_t;
typedef struct {
  volatile uint32_t SR, CR1, CR2, SMPR1, SMPR2, JOFR1, JOFR2, JOFR3, JOFR4, HTR, LTR, SQR1, SQR2, SQR3, JSQR, JDR1,
      JDR2, JDR3, JDR4, DR;
} adc_t;
typedef struct {
  volatile uint32_t CRH, CRL, PRLH, PRLL, DIVH, DIVL, CNTH, CNTL, ALRH, ALRL;
} rtc_t;
typedef struct {
  volatile uint32_t KR, PR, RLR, SR;
} iwdg_t;
typedef struct {
  volatile uint32_t CR, CSR;
} pwr_t;
typedef struct {
  volatile uint32_t CTRL, LOAD, VAL, CALIB;
} systick_t;
typedef struct {
  volatile uint32_t CPUID, ICSR, VTOR, AIRCR, SCR, CCR;
} scb_t;

#define RCC ((rcc_t *) 0x40021000)
#define FLASH ((flash_t *) 0x40022000)
#define GPIOA ((gpio_t *) 0x40010800)
#define GPIOB ((gpio_t *) 0x40010C00)
#define GPIOC ((gpio_t *) 0x40011000)
#define USART1 ((usart_t *) 0x40013800)
#define SPI1 ((spi_t *) 0x40013000)
#define TIM1 ((tim_t *) 0x40012C00)
#define TIM2 ((tim_t *) 0x40000000)
#define ADC1 ((adc_t *) 0x40012400)
#define RTC ((rtc_t *) 0x40002800)
#define IWDG ((iwdg_t *) 0x40003000)
#define PWR ((pwr_t *) 0x40007000)
#define BKP_DR(n) REG(0x40006C00 + 4 * (n))
#define SYSTICK ((systick_t *) 0xE000E010)
#define SCB ((scb_t *) 0xE000ED00)
#define NVIC_ISER(n) REG(0xE000E100 + 4 * (n))
#define NVIC_IPR8(n) (*(volatile uint8_t *) (0xE000E400 + (n)))

#define IRQ_USART1 37
#define IRQ_DMA1_CH3 13
#define IRQ_TIM2 28
#define SCB_SHPR3 REG(0xE000ED20)  // SysTick priority in bits 31:24
#define DHCSR REG(0xE000EDF0)      // bit 0 is set while a debugger is attached
#define DBGMCU_CR REG(0xE0042004)  // bit 0 DBG_SLEEP keeps the clocks on while the core sleeps

// ---------------------------------------------------------------- bits
// RCC
#define RCC_CR_HSION (1u << 0)
#define RCC_CR_HSIRDY (1u << 1)
#define RCC_CR_HSEON (1u << 16)
#define RCC_CR_HSERDY (1u << 17)
#define RCC_CR_PLLON (1u << 24)
#define RCC_CR_PLLRDY (1u << 25)
#define RCC_CFGR_SW (3u << 0)
#define RCC_CFGR_SW_PLL (2u << 0)
#define RCC_CFGR_SWS (3u << 2)
#define RCC_CFGR_SWS_PLL (2u << 2)
#define RCC_CFGR_PPRE1_DIV2 (4u << 8)
#define RCC_CFGR_ADCPRE_DIV6 (2u << 14)
#define RCC_CFGR_PLLSRC_HSE (1u << 16)
#define RCC_CFGR_PLLMUL9 (7u << 18)
#define RCC_AHBENR_DMA1EN (1u << 0)
#define RCC_APB2ENR_AFIOEN (1u << 0)
#define RCC_APB2ENR_IOPAEN (1u << 2)
#define RCC_APB2ENR_IOPBEN (1u << 3)
#define RCC_APB2ENR_IOPCEN (1u << 4)
#define RCC_APB2ENR_ADC1EN (1u << 9)
#define RCC_APB2ENR_TIM1EN (1u << 11)
#define RCC_APB2ENR_SPI1EN (1u << 12)
#define RCC_APB2ENR_USART1EN (1u << 14)
#define RCC_APB1ENR_TIM2EN (1u << 0)
#define RCC_APB1ENR_BKPEN (1u << 27)
#define RCC_APB1ENR_PWREN (1u << 28)
#define RCC_BDCR_LSEON (1u << 0)
#define RCC_BDCR_LSERDY (1u << 1)
#define RCC_BDCR_RTCSEL (3u << 8)
#define RCC_BDCR_RTCSEL_LSE (1u << 8)
#define RCC_BDCR_RTCEN (1u << 15)
#define RCC_CSR_RMVF (1u << 24)
#define RCC_CSR_PINRSTF (1u << 26)
#define RCC_CSR_PORRSTF (1u << 27)
#define RCC_CSR_SFTRSTF (1u << 28)
#define RCC_CSR_IWDGRSTF (1u << 29)

// FLASH
#define FLASH_ACR_LATENCY_2 (2u << 0)
#define FLASH_ACR_PRFTBE (1u << 4)
#define FLASH_SR_BSY (1u << 0)
#define FLASH_SR_PGERR (1u << 2)
#define FLASH_SR_WRPRTERR (1u << 4)
#define FLASH_SR_EOP (1u << 5)
#define FLASH_CR_PG (1u << 0)
#define FLASH_CR_PER (1u << 1)
#define FLASH_CR_STRT (1u << 6)
#define FLASH_CR_LOCK (1u << 7)
#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu

// PWR, RTC
#define PWR_CR_DBP (1u << 8)
#define RTC_CRL_RSF (1u << 3)
#define RTC_CRL_CNF (1u << 4)
#define RTC_CRL_RTOFF (1u << 5)

// ADC
#define ADC_SR_EOC (1u << 1)
#define ADC_CR2_ADON (1u << 0)
#define ADC_CR2_CAL (1u << 2)
#define ADC_CR2_RSTCAL (1u << 3)
#define ADC_CR2_EXTSEL_SWSTART (7u << 17)
#define ADC_CR2_EXTTRIG (1u << 20)
#define ADC_CR2_SWSTART (1u << 22)
#define ADC_CR2_TSVREFE (1u << 23)
#define ADC_SMP_239_5 7u  // 239.5-cycle sample time

// SPI
#define SPI_CR1_CPHA (1u << 0)
#define SPI_CR1_CPOL (1u << 1)
#define SPI_CR1_MSTR (1u << 2)
#define SPI_CR1_BR (7u << 3)
#define SPI_CR1_BR_DIV4 (1u << 3)
#define SPI_CR1_BR_DIV8 (2u << 3)
#define SPI_CR1_BR_DIV16 (3u << 3)
#define SPI_CR1_SPE (1u << 6)
#define SPI_CR1_SSI (1u << 8)
#define SPI_CR1_SSM (1u << 9)
#define SPI_CR1_DFF (1u << 11)
#define SPI_CR2_TXDMAEN (1u << 1)
#define SPI_SR_RXNE (1u << 0)
#define SPI_SR_TXE (1u << 1)
#define SPI_SR_BSY (1u << 7)

// USART
#define USART_SR_ERRORS 0x0Fu  // parity, framing, noise, overrun
#define USART_SR_IDLE (1u << 4)
#define USART_SR_RXNE (1u << 5)
#define USART_SR_TC (1u << 6)
#define USART_SR_TXE (1u << 7)
#define USART_CR1_RE (1u << 2)
#define USART_CR1_TE (1u << 3)
#define USART_CR1_IDLEIE (1u << 4)
#define USART_CR1_RXNEIE (1u << 5)
#define USART_CR1_TCIE (1u << 6)
#define USART_CR1_TXEIE (1u << 7)
#define USART_CR1_UE (1u << 13)

// DMA
#define DMA_CCR_EN (1u << 0)
#define DMA_CCR_TCIE (1u << 1)
#define DMA_CCR_DIR_TO_PERIPH (1u << 4)
#define DMA_CCR_MINC (1u << 7)
#define DMA_CCR_PSIZE_16 (1u << 8)
#define DMA_CCR_MSIZE_16 (1u << 10)
#define DMA_IFCR_CH3 (0xFu << 8)

// TIM
#define TIM_CR1_CEN (1u << 0)
#define TIM_CR1_ARPE (1u << 7)
#define TIM_EGR_UG (1u << 0)
#define TIM_DIER_UIE (1u << 0)
#define TIM_SR_UIF (1u << 0)
#define TIM_CCMR1_OC1PE (1u << 3)
#define TIM_CCMR1_OC1M_PWM1 (6u << 4)
#define TIM_CCER_CC1E (1u << 0)
#define TIM_BDTR_MOE (1u << 15)

// IWDG, SCB, SysTick, debug
#define IWDG_KEY_ACCESS 0x5555u
#define IWDG_KEY_RELOAD 0xAAAAu
#define IWDG_KEY_START 0xCCCCu
#define IWDG_PR_DIV64 4u
#define SCB_AIRCR_SYSRESETREQ (0x05FAu << 16 | 1u << 2)
#define SYSTICK_CTRL_ON 7u  // enabled, interrupt, core clock
#define DHCSR_C_DEBUGEN (1u << 0)
#define DBGMCU_CR_SLEEP (1u << 0)
#define DBGMCU_CR_SLEEP_STOP_STANDBY 7u

typedef struct {
  volatile uint32_t CCR, CNDTR, CPAR, CMAR, RESERVED;
} dma_ch_t;
#define DMA1_ISR REG(0x40020000)
#define DMA1_IFCR REG(0x40020004)
#define DMA1_CH(n) ((dma_ch_t *) (0x40020008 + 20 * ((n) - 1)))

static inline uint32_t irq_save(void) {
  uint32_t p;
  __asm volatile("mrs %0, primask\n cpsid i" : "=r"(p)::"memory");
  return p;
}
static inline void irq_restore(uint32_t p) { __asm volatile("msr primask, %0" ::"r"(p) : "memory"); }
static inline void irq_enable(int n) { NVIC_ISER(n / 32) = 1u << (n % 32); }

// GPIO pin modes (CNF << 2 | MODE)
enum {
  PIN_AIN = 0x0,
  PIN_IN = 0x4,
  PIN_IN_PULL = 0x8,
  PIN_OUT = 0x2,
  PIN_OUT_OD = 0x6,
  PIN_AF = 0xA,  // alternate function push-pull, 2 MHz like the stock firmware
};

static inline void pin_mode(gpio_t *g, int pin, int mode) {
  volatile uint32_t *r = pin < 8 ? &g->CRL : &g->CRH;
  int s = (pin & 7) * 4;
  *r = (*r & ~(0xFu << s)) | (uint32_t) mode << s;
}
static inline void pin_set(gpio_t *g, int pin, int on) { g->BSRR = on ? 1u << pin : 1u << (pin + 16); }
static inline int pin_get(gpio_t *g, int pin) { return (g->IDR >> pin) & 1; }
