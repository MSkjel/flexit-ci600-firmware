// Vector table and reset handler. The stock bootloader sets the stack and jumps to FLASH_APP, where the stock
// app starts too.
#include "board.h"
#include "stm32f1.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _end, _estack;
int main(void);
extern void (*const vectors[])(void);

#define PAINT 0xC1600C16u  // unused stack fill, board_stack_free() looks for it

// FAULT_MAGIC and the faulting PC, for board_reset_cause() after the reset
__attribute__((section(".noinit"))) uint32_t fault_record[2];

void Reset_Handler(void) {
  SCB->VTOR = (uint32_t) vectors;
  uint32_t *s = &_sidata, *d = &_sdata;
  while (d < &_edata)
    *d++ = *s++;
  for (d = &_sbss; d < &_ebss;)
    *d++ = 0;
  uint32_t sp;
  __asm volatile("mov %0, sp" : "=r"(sp));
  for (d = &_end; d < (uint32_t *) sp - 16;)  // paint the free stack, stopping 64 bytes below sp
    *d++ = PAINT;
  main();
  for (;;) {
  }
}

uint32_t board_stack_free(void) {
  uint32_t *p = &_end;
  while (p < &_estack && *p == PAINT)
    p++;
  return (uint32_t) ((uint8_t *) p - (uint8_t *) &_end);
}

void fault(uint32_t *frame) {
  fault_record[0] = FAULT_MAGIC;
  fault_record[1] = frame[6];  // the stacked PC
  board_reset();
}

// Calls fault() with the exception frame from MSP or PSP, whichever was in use.
__attribute__((naked)) void HardFault_Handler(void) {
  __asm volatile("tst lr, #4\n"
                 "ite eq\n"
                 "mrseq r0, msp\n"
                 "mrsne r0, psp\n"
                 "b fault\n");
}

static void Default_Handler(void) {
  for (;;) {
  }  // the watchdog restarts us
}

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

// Cortex-M3 exceptions and the 60 STM32F103xE interrupts. 0x08002100 is only 256-byte aligned, so only vectors
// below offset 0x100 work.
_Static_assert(16 + IRQ_USART1 < 64 && 16 + IRQ_DMA1_CH3 < 64 && 16 + IRQ_TIM2 < 64, "vectors below 0x100");
#pragma GCC diagnostic ignored "-Woverride-init"  // the IRQ entries below override the Default_Handler range
__attribute__((section(".isr_vector"), used)) void (*const vectors[16 + 60])(void) = {
    (void (*)(void)) &_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    [4 ... 10] = Default_Handler,
    [11 ... 14] = Default_Handler,
    SysTick_Handler,
    [16 ... 16 + 59] = Default_Handler,
    [16 + IRQ_USART1] = USART1_IRQHandler,
    [16 + IRQ_DMA1_CH3] = DMA1_Channel3_IRQHandler,  // SPI1 TX DMA, pixels to the display
    [16 + IRQ_TIM2] = TIM2_IRQHandler,               // the microsecond clock, see board.c
};
