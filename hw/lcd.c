// ILI932x TFT on SPI1. The reset and both init sequences are copied from stock's lcd_init and lcd_write_reg.
#include "lcd.h"
#include "board.h"
#include "prof.h"
#include "stm32f1.h"
#include <string.h>

// First byte of every SPI transfer, 0111 0 RS RW
#define START_INDEX 0x70
#define START_WRITE 0x72
#define START_READ 0x73

// Until the last byte is out
static void spi_drain(void) {
  while (!(SPI1->SR & SPI_SR_TXE)) {
  }
  while (SPI1->SR & SPI_SR_BSY) {
  }
}

// Frame size and speed: SPI_CR1_DFF for 16 bits, and SPI_CR1_BR from spi_speed()
static void spi_mode(uint32_t bits) {
  SPI1->CR1 &= ~SPI_CR1_SPE;
  SPI1->CR1 = (SPI1->CR1 & ~(SPI_CR1_DFF | SPI_CR1_BR)) | bits;
  SPI1->CR1 |= SPI_CR1_SPE;
}

// SPI_CR1_BR for at most want, at the clock now
static uint32_t spi_speed(uint32_t want) { return SPI_CR1_BR_DIV(lcd_spi_div(board_hz(), want)); }
static uint32_t reg_br, pixel_br;  // the registers' and the pixels'. Set by lcd_init(), which runs after the clock
static uint32_t pixel_ns;          // what a pixel takes
static uint16_t window[4];  // registers 0x50 to 0x53 as the controller has them: the first and last row, then column

// Deselecting waits until the last byte is out.
static void cs(int on) {
  if (!on)
    spi_drain();
  pin_set(PIN_LCD_CS, !on);
}

static inline void tx(uint8_t b) {
  while (!(SPI1->SR & SPI_SR_TXE)) {
  }
  SPI1->DR = b;
}

static uint8_t rx(void) {
  while (SPI1->SR & SPI_SR_BSY) {
  }
  (void) SPI1->DR;
  while (!(SPI1->SR & SPI_SR_TXE)) {
  }
  SPI1->DR = 0xFF;
  while (!(SPI1->SR & SPI_SR_RXNE)) {
  }
  return (uint8_t) SPI1->DR;
}

static void index_reg(uint8_t r) {
  cs(1);
  tx(START_INDEX);
  tx(0);
  tx(r);
  cs(0);
}

static void wreg(uint8_t r, uint16_t v) {
  index_reg(r);
  cs(1);
  tx(START_WRITE);
  tx(v >> 8);
  tx(v & 0xFF);
  cs(0);
}

static uint16_t read_id(void) {
  index_reg(0);
  cs(1);
  tx(START_READ);
  rx();  // dummy byte
  uint16_t hi = rx(), lo = rx();
  cs(0);
  return (uint16_t) (hi << 8 | lo);
}

// Init step, write v to register r or wait v ms if r is DELAY
typedef struct {
  uint8_t r;
  uint16_t v;
} rv_t;
#define DELAY 0xFF

// stock lcd_init, controller id 0x9220/0x9221
static const rv_t INIT_9220[] = {
    {0xE5, 0xC000}, {7, 0},        {0, 1},        {1, 0},       {2, 0x700},     {3, 0x5030},   {4, 0},
    {8, 0x202},     {9, 0},        {10, 0},       {0xC, 3},     {0xD, 0},       {0xF, 2},      {0x10, 0},
    {0x11, 7},      {0x12, 0},     {0x13, 0},     {DELAY, 200}, {0x10, 0x17B0}, {0x11, 0x147}, {DELAY, 50},
    {0x12, 0x13C},  {DELAY, 50},   {0x13, 0xE00}, {0x29, 9},    {DELAY, 50},    {0x20, 0},     {0x21, 0},
    {0x30, 0x207},  {0x31, 0x505}, {0x32, 0x102}, {0x35, 6},    {0x36, 0},      {0x37, 0x707}, {0x38, 0x506},
    {0x39, 0x407},  {0x3C, 0x106}, {0x3D, 0x601}, {0x50, 0},    {0x51, 0xEF},   {0x52, 0},     {0x53, 0x13F},
    {0x60, 0x2700}, {0x61, 1},     {0x6A, 0},     {0x80, 0},    {0x81, 0},      {0x82, 0},     {0x83, 0},
    {0x84, 0},      {0x85, 0},     {0x90, 0x10},  {0x92, 0},    {0x93, 3},      {0x95, 0x110}, {0x97, 0},
    {0x98, 0},      {7, 0x173},
};

// stock lcd_init, controller id 0x9224/0x9225
static const rv_t INIT_9224[] = {
    {0xE5, 0x78F0}, {1, 0},         {2, 0x700},    {3, 0x5010},    {4, 0},        {8, 0x202},    {9, 0},
    {10, 0},        {0xC, 3},       {0xD, 0},      {0xF, 2},       {0x10, 0},     {0x11, 7},     {0x12, 0},
    {0x13, 0},      {7, 1},         {DELAY, 200},  {0x10, 0x1690}, {0x11, 0x227}, {DELAY, 50},   {0x12, 0x9C},
    {DELAY, 50},    {0x13, 0x1800}, {0x2B, 0xD},   {0x29, 0x24},   {DELAY, 50},   {0x20, 0},     {0x21, 0},
    {0x30, 0x303},  {0x31, 0x303},  {0x32, 0x303}, {0x35, 0x300},  {0x36, 0},     {0x37, 0x303}, {0x38, 0x303},
    {0x39, 0x603},  {0x3C, 3},      {0x3D, 0},     {0x50, 0},      {0x51, 0xEF},  {0x52, 0},     {0x53, 0x13F},
    {0x60, 0xA700}, {0x61, 1},      {0x6A, 0},     {0x90, 0x10},   {0x92, 0x600}, {0x97, 0},     {7, 0x133},
};

static void run(const rv_t *s, int n) {
  for (int i = 0; i < n; i++) {
    if (s[i].r == DELAY)
      board_delay_ms(s[i].v);
    else
      wreg(s[i].r, s[i].v);
  }
}

bool lcd_init(void) {
  RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
  SPI1->CR1 = SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_MSTR | SPI_CR1_CPOL | SPI_CR1_CPHA;  // soft NSS, master, mode 3
  spi_mode(spi_speed(LCD_INIT_HZ));                                                    // slower during setup
  board_delay_ms(400);     // as stock, lets the display's supply settle after power-up
  pin_set(PIN_LCD_CS, 0);  // CS low during the reset, as stock
  pin_set(PIN_LCD_RESET, 0);
  board_delay_ms(1);
  pin_set(PIN_LCD_RESET, 1);
  board_delay_ms(120);
  bool known = true;
  switch (read_id() & 0xFEFE) {
    case 0x9224:
      run(INIT_9224, sizeof INIT_9224 / sizeof *INIT_9224);
      break;
    default:
      known = false;  // unknown id, try the first one
      // fall through
    case 0x9220:
      run(INIT_9220, sizeof INIT_9220 / sizeof *INIT_9220);
      break;
  }
  wreg(0x03, 0x5038);  // entry mode, BGR as stock but along a screen row first, see lcd_draw
  reg_br = spi_speed(LCD_REG_HZ);
  pixel_br = spi_speed(LCD_PIXEL_HZ);
  pixel_ns = 16000 * lcd_spi_div(board_hz(), LCD_PIXEL_HZ) / (board_hz() / 1000000);
  spi_mode(reg_br);
  RCC->AHBENR |= RCC_AHBENR_DMA1EN;
  irq_enable(IRQ_DMA1_CH3);
  memset(window, 0xFF, sizeof window);  // none it will be asked for, so the first area sets them
  return known;
}

// Screen (x, y) is GRAM (y, x). Register 0x20 = y, 0x21 = x, window 0x50-0x51 is y and 0x52-0x53 is x.
// Entry mode 0x5038 walks along a screen row and wraps to the next one inside the window, so a whole area goes
// out in one burst in LVGL's order. Stock set the cursor for every column.
volatile uint32_t lcd_send_us;
static volatile bool sending;
static void (*send_done)(void);
static uint32_t send_start;
static struct {
  const uint16_t *px;       // the next row
  uint16_t w, pitch, rows;  // rows still to go, w pixels each, pitch apart
} left;

// The next rows to the DMA: all that are left if they follow each other in memory, else one, and its interrupt sends
// the next. The display takes the pause between two as it comes.
static void send_rows(void) {
  unsigned rows = left.pitch == left.w ? left.rows : 1;
  dma_ch_t *ch = DMA1_CH(3);  // SPI1 TX
  ch->CCR = 0;
  ch->CPAR = (uint32_t) &SPI1->DR;
  ch->CMAR = (uint32_t) left.px;
  ch->CNDTR = left.w * rows;
  left.px += left.pitch * rows;
  left.rows = (uint16_t) (left.rows - rows);
  // last: the interrupt may come before the next line, and it's here again
  ch->CCR = DMA_CCR_MSIZE_16 | DMA_CCR_PSIZE_16 | DMA_CCR_MINC | DMA_CCR_DIR_TO_PERIPH | DMA_CCR_TCIE | DMA_CCR_EN;
}

void lcd_draw(int x1, int y1, int x2, int y2, const uint16_t *px, unsigned pitch, void (*done)(void)) {
  lcd_wait();
  const uint16_t want[4] = {(uint16_t) y1, (uint16_t) y2, (uint16_t) x1, (uint16_t) x2};
  for (int i = 0; i < 4; i++)  // areas after each other share their rows or their columns
    if (window[i] != want[i])
      wreg((uint8_t) (0x50 + i), window[i] = want[i]);
  wreg(0x20, (uint16_t) y1);
  wreg(0x21, (uint16_t) x1);
  index_reg(0x22);  // GRAM
  cs(1);
  tx(START_WRITE);
  spi_drain();
  spi_mode(SPI_CR1_DFF | pixel_br);  // 16-bit frames, one pixel each, high byte first
  sending = true;
  send_done = done;
  send_start = board_us();
  left.px = px;
  left.w = (uint16_t) (x2 - x1 + 1);
  left.pitch = (uint16_t) pitch;
  left.rows = (uint16_t) (y2 - y1 + 1);
  SPI1->CR2 |= SPI_CR2_TXDMAEN;
  send_rows();
}

void DMA1_Channel3_IRQHandler(void) {
  DMA1_IFCR = DMA_IFCR_CH3;
  if (left.rows) {
    send_rows();
    return;
  }
  DMA1_CH(3)->CCR = 0;
  spi_drain();  // the last pixel out of the shift register
  SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
  spi_mode(reg_br);
  cs(0);
  lcd_send_us += board_us() - send_start;
  sending = false;
  if (send_done)
    send_done();
}

uint32_t lcd_us(uint32_t px) { return px * pixel_ns / 1000; }

void lcd_wait(void) {
  bool was = prof_waits(true);  // the interrupt that ends a send comes here too, while main waits
  while (sending)
    board_sleep();  // woken by the DMA interrupt
  prof_waits(was);
}
