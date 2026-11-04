// A debug build's profiler, make -C hw PROF=1. TIM3 interrupts about 1000 times a second, over everything else, and
// counts the function it stopped, from the table tools/mkprof.py puts just after the image. Every PROF_S seconds the
// busiest go to the ESP's log, then it counts again.
#include "prof.h"
#include "board.h"
#include "lvgl.h"
#include "pbulk.h"
#include "plink.h"
#include "stm32f1.h"

#define PROF_US 991             // between samples, prime so they don't keep step with the 1 ms tick or the 5 ms loop
#define PROF_S 10               // seconds each report counts
#define PROF_TOP 16             // functions counted
#define PROF_MIN_PM 5           // the least share a report lists, per mille
#define PROF_MAGIC 0x464F5250u  // "PROF", the table's first word

extern const uint32_t _sidata[], _sdata[], _edata[];

// The table: PROF_MAGIC, n, where each of the n functions starts going up, its name's offset (2 bytes each), the names
static const uint32_t *at;
static const uint16_t *name_at;
static const char *names;
static uint32_t fns, asleep_fn;  // functions in the table, board_sleep()'s

// The busiest functions since the last report, by Space-Saving: one that isn't in yet takes the place of the one with
// the fewest samples, and its count on top. So each count is at most the fewest high.
typedef struct {
  uint16_t fn, n;
} count_t;
static count_t top[PROF_TOP];
static uint32_t used, samples, asleep;
static volatile bool sending;  // the report goes out, nothing counts meanwhile
static uint32_t seconds, line;

// The function pc is in, the last to start at or before it
static uint32_t function(uint32_t pc) {
  uint32_t lo = 0, hi = fns;
  while (hi - lo > 1) {
    uint32_t m = (lo + hi) / 2;
    if (at[m] <= pc)
      lo = m;
    else
      hi = m;
  }
  return lo;
}

void prof_sample(const uint32_t *frame) {
  TIM3->SR = 0;
  if (sending)
    return;
  uint32_t fn = function(frame[6]);  // the stacked PC
  samples++;
  if (fn == asleep_fn) {
    asleep++;
    return;
  }
  uint32_t least = 0;
  for (uint32_t i = 0; i < used; i++) {
    if (top[i].fn == fn) {
      top[i].n++;
      return;
    }
    if (top[i].n < top[least].n)
      least = i;
  }
  if (used < PROF_TOP)
    top[least = used++].n = 0;
  top[least].fn = (uint16_t) fn;
  top[least].n++;
}

// Calls prof_sample() with the exception frame from MSP or PSP, like HardFault_Handler
__attribute__((naked)) void TIM3_IRQHandler(void) {
  __asm volatile("tst lr, #4\n"
                 "ite eq\n"
                 "mrseq r0, msp\n"
                 "mrsne r0, psp\n"
                 "b prof_sample\n");
}

void prof_init(void) {
  const uint32_t *t = _sidata + (_edata - _sdata);  // just after the image
  if (t[0] != PROF_MAGIC)
    return;  // flashed without the table
  fns = t[1];
  at = t + 2;
  name_at = (const uint16_t *) (at + fns);
  names = (const char *) (name_at + fns);
  asleep_fn = function((uint32_t) (uintptr_t) board_sleep);
  RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
  TIM3->PSC = TIM2->PSC;  // 1 MHz like the us clock
  TIM3->ARR = PROF_US - 1;
  TIM3->EGR = TIM_EGR_UG;
  TIM3->SR = 0;
  TIM3->DIER = TIM_DIER_UIE;
  NVIC_IPR8(IRQ_TIM3) = 0x00;  // over the bus too, so its time is its own
  irq_enable(IRQ_TIM3);
  TIM3->CR1 = TIM_CR1_CEN;
}

void prof_second(void) {
  if (!fns || sending || ++seconds < PROF_S)
    return;
  seconds = line = 0;
  sending = true;
  __asm volatile("" ::: "memory");  // the counts are ours until it's false again
  // Busiest first
  for (uint32_t i = 1; i < used; i++)
    for (uint32_t k = i; k > 0 && top[k].n > top[k - 1].n; k--) {
      count_t c = top[k];
      top[k] = top[k - 1];
      top[k - 1] = c;
    }
}

static unsigned pm(uint32_t n) { return samples ? (unsigned) ((n * 1000 + samples / 2) / samples) : 0; }

int prof_log(uint8_t *out, int room) {
  if (!sending)
    return 0;
  char *t = (char *) out + PBULK_MSG_HEAD;
  int max = room - PBULK_MSG_HEAD < 255 ? room - PBULK_MSG_HEAD : 255, n;
  if (max <= 0)
    return 0;
  if (!line) {
    char high[32] = "";  // a count that took another's place has the fewest's count in it
    unsigned e = pm(top[PROF_TOP - 1].n);
    if (used == PROF_TOP)
      lv_snprintf(high, sizeof high, ", each at most %u.%u %% high", e / 10, e % 10);
    unsigned ds = (unsigned) (samples * PROF_US / 100000);
    n = lv_snprintf(t, (size_t) max, "prof %u.%u s at %u MHz: %u.%u %% asleep, the busiest%s:", ds / 10, ds % 10,
                    (unsigned) (board_hz() / 1000000), pm(asleep) / 10, pm(asleep) % 10, high);
  } else if (line <= used && pm(top[line - 1].n) >= PROF_MIN_PM) {
    unsigned p = pm(top[line - 1].n);
    n = lv_snprintf(t, (size_t) max, "prof %3u.%u %% %s", p / 10, p % 10, names + name_at[top[line - 1].fn]);
  } else {
    used = samples = asleep = 0;
    __asm volatile("" ::: "memory");
    sending = false;
    return 0;
  }
  if (n >= max)
    return 0;  // in an answer with more room
  line++;
  plink_put16(out, K_LOG);
  out[2] = (uint8_t) n;
  return PBULK_MSG_HEAD + n;
}
