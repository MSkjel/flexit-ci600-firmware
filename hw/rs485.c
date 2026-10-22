// RS-485 on USART1. A frame ends when the line goes idle and we answer it right in the interrupt like stock,
// since the CU60 only waits 10 ms for a register read. The ESP's link frames get answered here too. Replies go out
// from the TXE interrupt so the core can sleep.
#include "rs485.h"
#include "board.h"
#include "link.h"
#include "mbserver.h"
#include "stm32f1.h"

#define RX_MAX 512  // room for a few frames back to back, see mb_chunk
static uint8_t rx[RX_MAX], tx[MB_MAX_FRAME];
static int rx_len;            // only used by the interrupt
static uint32_t burst_start;  // time of the burst's first byte
static int tx_len, tx_pos;    // the reply being sent, only used by the interrupt

void rs485_init(void) {
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  USART1->BRR = (board_hz + 57600) / 115200;
  USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_IDLEIE;
  irq_enable(IRQ_USART1);
}

// One of the two is set from the first byte to the last stop bit, and the interrupt swaps them in one write
bool rs485_busy(void) { return USART1->CR1 & (USART_CR1_TXEIE | USART_CR1_TCIE); }

// Just after its status broadcast, or when there's no CU60 at all.
bool rs485_quiet(void) {
  const pbus_panel_t *w = link_bus_state();
  uint32_t s = board_irq_save();
  bool after = w->after_status && board_us() - w->status_end_us < PBUS_END_US;
  bool cu60 = board_ms() - w->status_ms < 1000;
  board_irq_restore(s);
  return !rs485_busy() && (after || !cu60);
}

// Sends the first byte, the interrupt sends the rest.
static void send(int n) {
  pin_set(GPIOA, 11, 1);  // receiver off, so we don't hear ourselves
  pin_set(GPIOA, 12, 1);  // driver on
  tx_len = n;
  tx_pos = 1;
  USART1->DR = tx[0];  // SR was read just before, so this write also clears a stale TC
  USART1->CR1 |= USART_CR1_TXEIE;
}

void USART1_IRQHandler(void) {
  uint32_t entry = board_us();
  uint32_t sr = USART1->SR, cr1 = USART1->CR1;
  if ((cr1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {  // room for the next byte of the reply
    if (tx_pos < tx_len)
      USART1->DR = tx[tx_pos++];
    if (tx_pos >= tx_len)
      USART1->CR1 = (cr1 & ~USART_CR1_TXEIE) | USART_CR1_TCIE;
  }
  if ((cr1 & USART_CR1_TCIE) && (sr & USART_SR_TC)) {  // the last stop bit is out, release the bus
    USART1->CR1 &= ~USART_CR1_TCIE;
    USART1->SR = ~USART_SR_TC;
    pin_set(GPIOA, 12, 0);
    pin_set(GPIOA, 11, 0);
  }
  if (sr & USART_SR_RXNE) {
    uint8_t b = (uint8_t) USART1->DR;
    if (rx_len == 0)
      burst_start = board_us();
    if (rx_len < RX_MAX)
      rx[rx_len++] = b;
  } else if (sr & USART_SR_ERRORS) {
    (void) USART1->DR;  // reading DR clears an overrun, noise or framing error
  }
  if (sr & USART_SR_IDLE) {  // line idle, the frame is complete
    uint32_t idle = board_us();
    (void) USART1->DR;
    int n = rx_len;
    rx_len = 0;
    int r = 0, skip = 0;  // skip = bytes in front of the CU60's
    bool modbus = true;
    if (n > 0 && rx[0] == PBULK_ADDR) {  // the ESP link, or noise that looks like it
      int k = pbulk_frame_len(rx, n);
      if (k == n) {
        r = link_bulk_frame(rx, n, idle, board_ms(), tx);
        while (r > 0 && board_us() - idle < PBUS_PANEL_WAIT_US) {
        }  // give the ESP's driver time to let go of the bus
        modbus = false;
      } else if (k && k < n && pbus_crc_ok(rx, k)) {
        skip = k;  // the CU60 sent right behind a link frame. That goes unanswered, the ESP sends it again
      } else if (pbus_is_cu60(rx + 1, n - 1)) {
        skip = 1;  // a noise byte in front of a CU60 frame
      } else {
        modbus = false;  // a link frame cut short, or noise
      }
    }
    if (modbus) {
      link_bus(rx + skip, n - skip, burst_start + (uint32_t) skip * PBUS_BYTE_US, idle, board_ms());
      r = mb_chunk(rx + skip, n - skip, tx);
    }
    if (r > 0) {
      uint32_t start = board_us();
      send(r);
      if (modbus)  // link.c needs to see our own reply on the bus too
        link_bus(tx, r, start, start + (uint32_t) (r + 1) * PBUS_BYTE_US, board_ms());
    }
  }
  board_isr_us += board_us() - entry;
}
