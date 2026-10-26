// RS-485 on USART1. A frame ends when the line goes idle and we answer it right in the interrupt like stock,
// since the CX60 only waits 10 ms for a register read. The ESP's link frames get answered here too. Replies go out
// from the TXE interrupt so the core can sleep.
#include "rs485.h"
#include "board.h"
#include "bus.h"
#include "link.h"
#include "mbserver.h"
#include "stm32f1.h"

#define RX_MAX 512  // room for a few frames back to back, see mb_chunk
static uint8_t rx[RX_MAX], tx[MB_MAX_FRAME];
static int rx_len;            // the burst so far, only the interrupt writes it
static uint32_t burst_start;  // time of the burst's first byte
static int tx_len, tx_pos;    // the reply being sent, only used by the interrupt

void rs485_init(void) {
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  USART1->BRR = (board_hz() + 57600) / 115200;
  USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_IDLEIE;
  irq_enable(IRQ_USART1);
}

// One of the two is set from the first byte to the last stop bit, and the interrupt swaps them in one write
bool rs485_busy(void) { return USART1->CR1 & (USART_CR1_TXEIE | USART_CR1_TCIE); }

// Early in the CX60's status broadcast, or with no CX60 at all. The rest of it and the gap after take 27 ms or
// more, and we miss nothing there: the status comes again next cycle and the ESP sends its frame again.
#define STATUS_EARLY 24  // bytes, 2 ms
bool rs485_wait_quiet(uint32_t ms) {
  const pbus_panel_t *w = link_bus_state();
  int head = (int) sizeof PBUS_STATUS_HEAD;
  for (uint32_t t = board_ms();;) {
    uint32_t s = board_irq_save();
    bool status = rx_len >= head && rx_len <= STATUS_EARLY && pbus_status_part(rx, head) == head;
    bool cx60 = board_ms() - w->status_ms < 1000;
    board_irq_restore(s);
    if (!rs485_busy() && (status || !cx60))
      return true;
    if (board_ms() - t >= ms)
      return false;
  }
}

// Sends the first byte, the interrupt sends the rest.
static void send(int n) {
  pin_set(PIN_RS485_RX_OFF, 1);  // so we don't hear ourselves
  pin_set(PIN_RS485_TX_ON, 1);
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
    pin_set(PIN_RS485_TX_ON, 0);
    pin_set(PIN_RS485_RX_OFF, 0);
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
  if (sr & USART_SR_IDLE) {  // line idle, the burst is complete
    uint32_t idle = board_us();
    // An SR read and then a DR read clear IDLE. Once DR was read above, another read could lose a new byte.
    if (!(sr & (USART_SR_RXNE | USART_SR_ERRORS)))
      (void) USART1->DR;
    int n = rx_len;
    rx_len = 0;
    bool modbus;
    int r = bus_burst(rx, n, burst_start, idle, board_ms(), tx, &modbus);
    if (r > 0) {
      while (!modbus && board_us() - idle < PBUS_PANEL_WAIT_US) {
      }  // give the ESP's driver time to let go of the bus
      uint32_t start = board_us();
      send(r);
      if (modbus)  // link.c needs to see our own reply on the bus too
        link_bus(tx, r, start, start + (uint32_t) (r + 1) * PBUS_BYTE_US, board_ms());
    }
  }
  board_isr_time(board_us() - entry);
}
