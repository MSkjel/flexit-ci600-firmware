// The ESP link on a simulated bus, with the CU60's schedule as its firmware runs it. Fails if a link frame
// ever overlaps the CU60, the CU60 gets more than 256 bytes at once, or a picture arrives damaged.
//   build/bulk_test [seconds] [cu60 control_tick ms] [seed] [ESP RX FIFO threshold] [panel 2]
#include "pbulk.h"
#include "plink.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t ns_t;  // simulated time in ns
#define US 1000LL
#define MS 1000000LL
#define BYTE 86806LL  // 10 bits at 115200
#define STEP 1000LL   // simulation step, 1 us

// Timing as flexit_modbus_server.cpp assumes it. Bytes become readable at the RX FIFO threshold or 2 bytes
// after the line goes quiet, plus ESP_ISR_US of interrupt latency.
static int esp_threshold = 1;
#define ESP_ISR_US 150
#define PANEL_STATUS_ISR_US \
  250  // us hw/rs485.c's interrupt spends on a status frame, measured 0.22 ms. Only the first byte that
       // comes in meanwhile survives

static ns_t now;
static uint32_t rnd_state = 1;
static uint32_t rnd(void) {
  rnd_state = rnd_state * 1664525u + 1013904223u;
  return rnd_state >> 8;
}
static ns_t between(ns_t a, ns_t b) { return a + (ns_t) (rnd() % (uint32_t) (b - a + 1)); }

// ---------------------------------------------------------------- the bus
enum { CU60, PANEL, ESP, PANEL2, NDEV };
static const char *NAME[NDEV] = {"CU60", "panel", "ESP", "panel 2"};

typedef struct {  // what a device is sending, bulk = a link frame
  uint8_t b[300];
  int n, i;
  bool on, bulk;
  ns_t next, busy_until;  // when the next byte is done, when the driver lets go after the DE tail
} tx_t;
static tx_t TX[NDEV];

typedef struct {  // what a device is receiving
  uint8_t b[600];
  int n;
  int src;  // who sent it, -1 = more than one
  ns_t first, last;
} burst_t;

static struct {
  long frames, bulk, collisions, cu60_hit, answers_hit;
  bool status_quiet;  // nothing but the link since the status broadcast ended
} BUS;

static void rx_byte(int dev, uint8_t c, int src);

static void start_tx(int dev, const uint8_t *b, int n, bool bulk) {
  for (int d = 0; d < NDEV; d++)
    if (d != dev && (TX[d].on || now < TX[d].busy_until)) {
      BUS.collisions++;
      if (!bulk && !TX[d].bulk)
        continue;
      if (dev != CU60 && d != CU60) {  // the panel answering into the ESP's driver, which loses the answer
        BUS.answers_hit++;
        continue;
      }
      BUS.cu60_hit++;
      printf("  COLLISION at %.3f ms: %s (%s) starts while %s (%s) is on the bus\n", now / 1e6, NAME[dev],
             bulk ? "link" : "modbus", NAME[d], TX[d].bulk ? "link" : "modbus");
    }
  if (!bulk)
    BUS.status_quiet = false;  // the quiet after the status frame is over
  tx_t *t = &TX[dev];
  memcpy(t->b, b, n);
  t->n = n;
  t->i = 0;
  t->on = true;
  t->bulk = bulk;
  t->next = now + BYTE;
  BUS.frames++;
  if (bulk)
    BUS.bulk++;
}

static ns_t de_tail(int dev) {  // how long the driver stays on after the last stop bit
  if (dev != ESP)
    return 2 * US;
  return rnd() % 100 ? between(10 * US, 100 * US) : between(100 * US, 1000 * US);
}

static void step_tx(void) {
  for (int d = 0; d < NDEV; d++) {
    tx_t *t = &TX[d];
    if (!t->on || now < t->next)
      continue;
    for (int r = 0; r < NDEV; r++)
      if (r != d)
        rx_byte(r, t->b[t->i], d);
    if (++t->i == t->n) {
      t->on = false;
      t->busy_until = now + de_tail(d);
    } else {
      t->next += BYTE;
    }
  }
}

static uint16_t crc(const uint8_t *p, int n) { return pbus_crc16(p, n); }
static int fin(uint8_t *f, int n) {
  uint16_t c = crc(f, n);
  f[n] = c & 0xFF;
  f[n + 1] = c >> 8;
  return n + 2;
}

// ---------------------------------------------------------------- the CU60, modelled on its firmware
// a frame the main loop queued, J_STATUS = the status broadcast
enum { J_SEND, J_STATUS };
typedef struct {
  int kind;
  uint8_t f[16];
  int n;
} job_t;

static struct {
  // swtimers counted down by SysTick. *_c = ms left, 0 = stopped. *_x = expired.
  // io = 10 ms bus step, mb = TMR_MODBUS one-shot, ctl = control_tick every second, ht = every 100 ms
  int io_c, io_x, mb_c, mb_x, ctl_c, ctl_x, ht_c, ht_x;
  ns_t next_tick;
  int state, poll_state;
  bool bc_busy;
  ns_t busy_until;  // main loop busy in control_tick and such until then
  ns_t ctl_ns;      // how long control_tick takes
  job_t jobs[64];
  int nj, ij;
  bool waiting;  // a job went out, wait for rx_done or TMR_MODBUS
  bool rx_en, rx_done;
  int rx_len, rx_src, want_src, want_len;
  uint8_t rx_buf[300];
  bool reply_ok;
  ns_t last_fc6;
  long status_n, overflows, longest;
} C;

static int PRESENT[4] = {0, 1, 0, 1};                   // by address. ESP at 1, this panel at 3, panel 2 if asked for
static const int DEV_OF[4] = {-1, ESP, PANEL2, PANEL};  // address to device
static uint8_t STATUS[PBUS_STATUS_LEN];
static ns_t status_end_true;  // when the last status broadcast ended on the wire

static void cu_tx(const uint8_t *f, int n) {
  start_tx(CU60, f, n, false);
  C.rx_en = false;
  C.rx_done = false;
}

// a Modbus request, addr, fc, two big-endian 16-bit fields and the CRC
static int mk(uint8_t *f, uint8_t addr, uint8_t fc, uint16_t a, uint16_t b) {
  f[0] = addr;
  f[1] = fc;
  f[2] = a >> 8;
  f[3] = a & 0xFF;
  f[4] = b >> 8;
  f[5] = b & 0xFF;
  return fin(f, 6);
}

static void tick(void) {  // SysTick
  int *c[4] = {&C.io_c, &C.mb_c, &C.ctl_c, &C.ht_c}, *x[4] = {&C.io_x, &C.mb_x, &C.ctl_x, &C.ht_x};
  static const int reload[4] = {10, 0, 1000, 100};
  for (int i = 0; i < 4; i++)
    if (*c[i] && !--*c[i]) {
      *x[i] = 1;
      *c[i] = reload[i];
    }
}

static bool expired(int *x) {
  if (*x) {
    *x = 0;
    return true;
  }
  return false;
}

static void push(int kind, const uint8_t *f, int n) {
  job_t *j = &C.jobs[C.nj++];
  j->kind = kind;
  if (n)
    memcpy(j->f, f, n);
  j->n = n;
}

static void run_job(void) {
  job_t *j = &C.jobs[C.ij++];
  if (j->kind == J_STATUS) {
    cu_tx(STATUS, sizeof STATUS);
    C.bc_busy = true;
    C.mb_c = 20;
    C.mb_x = 0;
    C.status_n++;
    return;
  }
  cu_tx(j->f, j->n);
  C.mb_c = 10;
  C.mb_x = 0;
  C.want_src = j->f[0] ? DEV_OF[j->f[0]] : -2;
  C.want_len = 7;
  C.waiting = true;
}

// Coils flagged in a poll's reply, which the CU60 reads right away. All of them after the settings poll, the
// first 20 after the counters poll, like mb_poll_panel_settings and mb_poll_panel_counters.
static int flagged(bool settings, int *first) {
  int bits = settings ? 0x14C : 20, n = 0;
  *first = -1;
  for (int i = 0; i < bits && 3 + i / 8 < C.rx_len - 2; i++)
    if (C.rx_buf[3 + i / 8] >> (i % 8) & 1) {
      if (*first < 0)
        *first = i;
      n++;
    }
  return n;
}

static void poll(int p, bool settings) {
  if (!PRESENT[p]) {
    C.state = C.state % 6 + 1;
    if (C.state == 7)
      C.state = 0;
    return;
  }
  if (C.poll_state == 0) {
    uint8_t f[8];
    mk(f, (uint8_t) p, 1, settings ? 0 : 0x14C, settings ? 0x14C : 0x168);
    cu_tx(f, 8);
    C.want_src = DEV_OF[p];
    C.want_len = settings ? 47 : 50;
    C.poll_state = 1;
    C.mb_c = 100;
    C.mb_x = 0;
    return;
  }
  if (!expired(&C.mb_x)) {
    if (!C.rx_done)
      return;
    C.rx_done = false;
    int first, n = C.reply_ok ? flagged(settings, &first) : 0;
    for (int i = 0; i < n; i++) {  // which coils doesn't matter here
      uint8_t f[8];
      uint16_t reg = (uint16_t) ((settings ? 0 : 0x14C) + first + i);
      mk(f, (uint8_t) p, 3, reg, 1);
      push(J_SEND, f, 8);
      mk(f, 0, 0x65, reg, (uint16_t) rnd());
      push(J_SEND, f, 8);
    }
  }
  C.poll_state = 0;
  C.state = C.state == 6 ? 0 : C.state + 1;
}

static void cu60_step(void) {  // one TMR_IO step of main_loop's state machine
  switch (C.state) {
    case 0:
      if (!C.bc_busy) {
        // every second, an FC6 broadcast per runtime counter that went up, each followed by a 10 ms wait
        if (now - C.last_fc6 >= 1000 * MS) {
          C.last_fc6 = now;
          for (int i = 0; i < 4; i++) {
            uint8_t f[8];
            mk(f, 0, 6, (uint16_t) (0x14D + 2 * i), (uint16_t) rnd());
            push(J_SEND, f, 8);
          }
        }
        push(J_STATUS, NULL, 0);
        return;
      }
      if (expired(&C.mb_x)) {
        C.bc_busy = false;
        C.state = 1;
      }
      return;
    case 1:
      poll(3, true);
      return;
    case 2:
      poll(3, false);
      return;
    case 3:
      poll(2, true);
      return;
    case 4:
      poll(2, false);
      return;
    case 5:
      poll(1, true);
      return;
    case 6:
      poll(1, false);
      return;
  }
}

static void cu60(void) {
  if (now >= C.next_tick) {
    C.next_tick += MS;
    tick();
  }
  if (now < C.busy_until || TX[CU60].on)
    return;
  if (C.waiting) {  // the synchronous waits in the coil loops
    if (!C.rx_done && !expired(&C.mb_x))
      return;
    C.waiting = false;
    C.rx_done = false;
  }
  if (C.ij < C.nj) {
    run_job();
    return;
  }
  C.ij = C.nj = 0;
  if (expired(&C.ht_x)) {
    C.busy_until = now + 20 * US;
    return;
  }
  if (expired(&C.ctl_x)) {
    C.busy_until = now + C.ctl_ns;
    return;
  }
  if (expired(&C.io_x))
    cu60_step();
}

// A panel's or the ESP's reply to an FC1 poll or FC3 read from the CU60, with `flags` coils flagged.
static int reply_to(const uint8_t *q, uint8_t *f, int flags) {
  f[0] = q[0];
  if (q[1] != 1) {
    f[1] = 3;
    f[2] = 2;
    f[3] = 0;
    f[4] = 1;
    return fin(f, 5);
  }
  bool settings = !(q[2] << 8 | q[3]);
  int bytes = settings ? 42 : 45;
  f[1] = 1;
  f[2] = (uint8_t) bytes;
  memset(f + 3, 0, bytes);
  for (int i = 0; i < flags; i++) {
    int c = settings ? 0xA1 + i : i;
    f[3 + c / 8] |= (uint8_t) (1u << (c % 8));
  }
  return fin(f, 3 + bytes);
}

// How many coils a device flags in its reply to q. Settings now and then when something changed, counters
// rarely when one was reset.
static int flags_for(const uint8_t *q, int settings_one_in) {
  if (q[1] != 1)
    return 0;
  if (!(q[2] << 8 | q[3]))
    return rnd() % (uint32_t) settings_one_in == 0 ? 3 : 0;
  return rnd() % 500 == 0 ? 1 : 0;
}

// ---------------------------------------------------------------- panel 2 (if there is one): answers polls
static burst_t P2;
static uint8_t p2_answer[64];
static int p2_answer_n;
static ns_t p2_answer_at;

static void panel2(void) {
  if (P2.n && now >= P2.last + BYTE) {
    if (P2.n == 8 && P2.b[0] == 2 && crc(P2.b, 6) == (P2.b[6] | P2.b[7] << 8)) {
      p2_answer_n = reply_to(P2.b, p2_answer, flags_for(P2.b, 50));
      p2_answer_at = now + between(200 * US, 1500 * US);
    }
    P2.n = 0;
  }
  if (p2_answer_n && now >= p2_answer_at && !TX[PANEL2].on) {
    start_tx(PANEL2, p2_answer, p2_answer_n, false);
    p2_answer_n = 0;
  }
}

// ---------------------------------------------------------------- the panel (address 3)
static pbulk_panel_t PRX;
static pbus_panel_t PBUS;  // the bus as its RX interrupt sees it
static burst_t PB;
static struct {  // the panel's UART holds one byte while hw/rs485.c's interrupt is busy with a status frame
  ns_t busy_until;
  bool held;
  long overruns;
} PW;
static ns_t panel_answer_at, panel_drain_at;
static uint8_t panel_answer[300];
static int panel_answer_n;
static int panel_coils;
static uint32_t panel_said, esp_heard, esp_heard_wrong;  // numbered messages from the panel to the ESP
static long panel_gap_answers;

// Like hw/rs485.c. A link frame gets an answer if pbus says there's time. Anything else goes to pbus, and gets
// a reply if it's for us.
static void panel_burst(burst_t *b) {
  ns_t idle = b->last + BYTE;  // the idle interrupt
  uint32_t idle_us = (uint32_t) (idle / US), ms = (uint32_t) (idle / MS);
  if (b->n > 0 && b->b[0] == PBULK_ADDR) {
    if (pbulk_frame_len(b->b, b->n) != b->n)
      return;
    bool gap = !(PBUS.after_status && idle_us - PBUS.status_end_us < PBUS_END_US);
    uint8_t say;
    uint32_t left = pbus_panel_window(&PBUS, idle_us, ms, &say);
    int n = pbulk_panel_frame(&PRX, 3, b->b, b->n, left, say, panel_answer);
    if (n) {  // wait for the ESP's driver to let go, like hw/rs485.c
      panel_answer_n = n;
      panel_answer_at = idle + between(PBUS_PANEL_WAIT_US * US, PBUS_PANEL_US * US);
      if (gap)
        panel_gap_answers++;
    }
    return;
  }
  pbus_panel_burst(&PBUS, b->b, b->n, (uint32_t) (b->first / US), idle_us, ms);
  if (PBUS.after_status) {
    PW.busy_until = idle + PANEL_STATUS_ISR_US * US;
    PW.held = false;
  }
  if (b->n == 8 && b->b[0] == 3 && crc(b->b, 6) == (b->b[6] | b->b[7] << 8)) {  // a poll or a read for us
    int flags = 0;
    if (b->b[1] == 1 && !(b->b[2] << 8 | b->b[3])) {
      flags = panel_coils;
      panel_coils = 0;
    } else
      flags = flags_for(b->b, 1 << 30);
    panel_answer_n = reply_to(b->b, panel_answer, flags);
    panel_answer_at = idle + 20 * US;
  }
}

// ---------------------------------------------------------------- pictures, one after the other
#define PIC_LEN 8400  // a full screen, 16 colours, compressed
// byte i of picture n
static uint8_t pic_byte(uint32_t n, uint32_t i) { return (uint8_t) ((i * 2654435761u + n * 40503u) >> 13); }
static uint32_t pic_no, got_no;
static int pic_pos = -1, got_n;  // pic_pos -1 = BEGIN not sent yet
static bool got_ok;
static long pics_ok, pics_bad;
static ns_t first_pic_at, last_pic_at;
static long helps_sent, helps_got;   // help texts, see feed_help
static ns_t help_queued, help_wait;  // when the last one was queued, the longest one waited to arrive

static void panel_msg(void *u, uint16_t key, const uint8_t *b, int len) {
  (void) u;
  if (key == K_PIC_BEGIN) {
    got_no = plink_number(b + 6, 4);
    got_n = 0;
    got_ok = true;
  } else if (key == PBULK_DATA_KEY) {
    for (int i = 0; i < len && got_n < PIC_LEN; i++, got_n++)
      if (b[i] != pic_byte(got_no, (uint32_t) got_n))
        got_ok = false;
  } else if (key == K_ITEM_HELP) {
    helps_got += len == PBULK_MSG_MAX;
    if (now - help_queued > help_wait)
      help_wait = now - help_queued;
  } else if (key == K_PIC_END) {
    if (got_ok && got_n == PIC_LEN)
      pics_ok++;
    else
      pics_bad++;
    if (!first_pic_at)
      first_pic_at = now;
    last_pic_at = now;
  }
}

static void panel(void) {
  if (PB.n && now >= PB.last + BYTE) {
    panel_burst(&PB);
    PB.n = 0;
  }
  if (panel_answer_n && now >= panel_answer_at && !TX[PANEL].on) {
    start_tx(PANEL, panel_answer, panel_answer_n, panel_answer[0] == PBULK_ADDR);
    if (panel_answer[0] != PBULK_ADDR)  // pbus sees our own replies too, like in hw/rs485.c
      pbus_panel_burst(&PBUS, panel_answer, panel_answer_n, (uint32_t) (now / US), (uint32_t) (now / US),
                       (uint32_t) (now / MS));
    panel_answer_n = 0;
  }
  if (now >= panel_drain_at) {  // the main loop, which LVGL can keep busy for a while
    uint8_t p[PBULK_MAX];
    int n;
    while ((n = pbulk_panel_take(&PRX, p)) > 0)
      pbulk_parse(p, n, panel_msg, NULL);
    if (!PRX.out_full && rnd() % 1000 == 0) {  // now and then the panel has messages for the ESP
      uint8_t m[PBULK_ANSWER_MAX];
      int k = 0, count = 1 + rnd() % 9;
      for (int i = 0; i < count; i++, panel_said++) {
        m[k++] = K_THEME & 0xFF;
        m[k++] = K_THEME >> 8;
        m[k++] = 4;
        m[k++] = (uint8_t) panel_said;
        m[k++] = (uint8_t) (panel_said >> 8);
        m[k++] = 0;
        m[k++] = 0;
      }
      pbulk_panel_put(&PRX, m, k);
    }
    panel_drain_at = now + (rnd() % 10 ? between(1 * MS, 8 * MS) : between(20 * MS, 80 * MS));
  }
}

// ---------------------------------------------------------------- the ESP (address 1)
// Its UART driver moves bytes from the FIFO to a ring at the RX FIFO threshold, or after 2 idle bytes with
// ESPHome's rx_timeout 2. The loop splits the ring into frames by length and CRC. Here we cut frames where
// the bus went quiet, which comes to the same.
static pbulk_esp_t ETX;
#define NB 4096
static struct {
  int fifo;                 // bytes in the hardware FIFO
  ns_t fifo_last, move_at;  // last byte into the FIFO, when the driver's interrupt runs
  int ring;                 // bytes in the driver's ring, not read yet
  uint8_t frame[NB][200];   // each burst as it came, by burst number
  int len[NB], have[NB];    // its length so far, how much of it was read
  bool done[NB];            // already done, it was a whole frame before the line went quiet
  long next, first;         // the burst coming in, the oldest not read in full
  ns_t last;                // last byte on the bus, for the burst boundary
  bool open;
  ns_t loop_at, prev_start;
  int prev_k;  // how much of a status frame the previous loop had read
  bool status_new, status_last;
  int reply;                     // a request to us needs a reply
  bool replied, replied_before;  // this loop answered the CU60, the loop before did
  long windows, sent, gap_frames;
  ns_t tx_at;
  uint8_t txbuf[300];
  int txn;
  bool tx_bulk;
  ns_t tx_started, seen_at;  // when its frame started, when it last read or sent a frame
  bool own;                  // its own reply is on the bus, to pass on once it's out
} E;
static pbus_gaps_t EG;  // the gaps, as the ESP sees them

static void esp_rx(uint8_t c) {
  int id = (int) (E.next % NB);
  if (!E.open) {
    E.open = true;
    E.len[id] = 0;
    E.have[id] = 0;
    E.done[id] = false;
  }
  if (E.len[id] < 200)
    E.frame[id][E.len[id]] = c;
  E.len[id]++;
  E.last = now;
  E.fifo++;
  E.fifo_last = now;
}

static void esp_uart(void) {
  if (E.open && now >= E.last + BYTE) {
    E.open = false;
    E.next++;
  }  // the bus went quiet, next burst
  if (!E.fifo || (E.fifo < esp_threshold && now < E.fifo_last + 2 * BYTE))
    return;
  if (!E.move_at)
    E.move_at = now + between(5 * US, 60 * US);  // the driver's interrupt latency
  if (now < E.move_at)
    return;
  E.move_at = 0;
  E.ring += E.fifo;
  E.fifo = 0;
}

static void feed_picture(void) {  // queue pictures for the link, as packages/panel.yaml does
  while (pbulk_esp_room(&ETX) >= 70) {
    bool ok;
    uint8_t v[58] = {0};
    if (pic_pos == -1) {
      memcpy(v + 6, &pic_no, 4);  // in this test the header carries the picture number
      ok = pbulk_esp_send(&ETX, K_PIC_BEGIN, v, sizeof v, false);
    } else if (pic_pos >= PIC_LEN) {
      ok = pbulk_esp_send(&ETX, K_PIC_END, v, 4, false);
    } else {
      for (int b = 0; b < 4 && pic_pos + b < PIC_LEN; b++)
        v[b] = pic_byte(pic_no, (uint32_t) (pic_pos + b));
      ok = pbulk_esp_send(&ETX, PBULK_DATA_KEY, v, 4, false);
    }
    if (!ok)
      break;
    if (pic_pos >= PIC_LEN) {
      pic_no++;
      pic_pos = -1;
    } else
      pic_pos = pic_pos < 0 ? 0 : pic_pos + 4;
  }
}

// A help text every few seconds, as long as a message that isn't DATA can be. It can't be split, so it has to fit
// a window on its own, or it and everything behind it would wait forever. It goes behind the picture data queued
#define HELP_WAIT_MAX (20 * 1000 * MS)
static void feed_help(void) {
  if (helps_sent > helps_got || now < help_queued + 3000 * MS)
    return;
  uint8_t h[PBULK_MSG_MAX];
  memset(h, 'h', sizeof h);
  if (pbulk_esp_send(&ETX, K_ITEM_HELP, h, PBULK_MSG_MAX, false)) {
    helps_sent++;
    help_queued = now;
  }
}

static void esp_got(void *u, uint16_t key, const uint8_t *b, int len) {
  (void) u;
  uint32_t v = len >= 2 ? (uint32_t) (b[0] | b[1] << 8) : 0;
  if (key != K_THEME || v != (esp_heard & 0xFFFF))
    esp_heard_wrong++;
  esp_heard++;
}

static uint8_t esp_req[8];  // the CU60's request to the ESP, to answer

// The component's frame callback, frames in bus order. We only know roughly when a frame ended, after the
// previous loop read the UART and before now.
static void esp_frame(const uint8_t *f, int n) {
  // like frame_age_us(), the UART hands bytes over at its threshold or 2 bytes after the line goes quiet
  uint32_t age = (uint32_t) ((now - E.prev_start) / US) + ESP_ISR_US +
                 (uint32_t) (esp_threshold - 1 > 2 ? esp_threshold - 1 : 2) * PBUS_BYTE_US;
  pbus_gaps_frame(&EG, f, n, (uint32_t) (now / US) - age - (uint32_t) n * 87,
                  (uint32_t) (now / US) - (uint32_t) n * 86);
  E.seen_at = now;
  if (pbus_is_status(f, n)) {
    E.status_new = E.status_last = true;
    return;
  }
  E.status_last = false;
  if (n >= 3 && f[0] == PBULK_ADDR && f[1] == PBULK_FC_ANSWER) {
    pbulk_esp_answer(&ETX, f, n, esp_got, NULL);
    return;
  }
  if (n == 8 && f[0] == 1 && (f[1] == 1 || f[1] == 3)) {
    memcpy(esp_req, f, 8);
    E.reply = 1;
  }
}

static ns_t esp_loop_len(void) {  // one ESPHome loop over all components
  uint32_t r = rnd() % 1000;
  if (r < 850)
    return between(50 * US, 400 * US);
  if (r < 980)
    return between(400 * US, 4 * MS);
  return between(4 * MS, 30 * MS);
}

static ns_t esp_start(void) {  // from deciding to send to the first bit on the wire, for DE, yield and the driver
  return rnd() % 100 ? between(30 * US, 300 * US) : between(300 * US, 1500 * US);
}

static void esp(void) {
  esp_uart();
  if (E.own && !TX[ESP].on) {  // its reply is out, and flush() passes it to the frame callback
    pbus_gaps_frame(&EG, TX[ESP].b, TX[ESP].n, (uint32_t) (E.tx_started / US), (uint32_t) (E.tx_started / US));
    E.seen_at = now;
    E.own = false;
  }
  if (E.txn) {  // sendResponse() blocks until it's out
    if (now >= E.tx_at && !TX[ESP].on) {
      start_tx(ESP, E.txbuf, E.txn, E.tx_bulk);
      E.own = !E.tx_bulk;
      E.tx_started = now;
      E.txn = 0;
    }
    return;
  }
  if (TX[ESP].on || now < TX[ESP].busy_until || now < E.loop_at)
    return;
  ns_t start = now;  // mb_.update() reads what the driver has
  // the component's splitter, a frame as soon as all of it is in by length and CRC
  while (E.first < E.next || (E.first == E.next && E.ring > 0)) {
    int id = (int) (E.first % NB);
    int take = E.len[id] - E.have[id];
    if (take > E.ring)
      take = E.ring;
    E.have[id] += take;
    E.ring -= take;
    if (E.done[id]) {
      if (E.first == E.next)
        break;
      E.first++;
      continue;
    }
    int n = E.len[id];
    bool whole = E.have[id] == n &&
                 (E.first < E.next ||
                  (n >= 4 && n <= 200 && crc(E.frame[id], n - 2) == (E.frame[id][n - 2] | E.frame[id][n - 1] << 8)));
    if (!whole) {
      E.status_last = false;
      break;
    }  // another frame has started, so the status frame isn't the last
    esp_frame(E.frame[id], n > 200 ? 200 : n);
    if (E.first == E.next) {
      E.done[id] = true;
      break;
    }
    E.first++;
  }
  int id = (int) (E.first % NB), part = E.first <= E.next && !E.done[id] ? E.have[id] : 0;  // the partial frame, if any
  int k = pbus_status_part(E.frame[id], part > 200 ? 200 : part);
  if (E.reply) {  // the Modbus library answers the CU60 first
    int n = reply_to(esp_req, E.txbuf, flags_for(esp_req, 50));
    E.txn = n;
    E.tx_bulk = false;
    E.tx_at = now + esp_start();
    E.reply = 0;
    E.replied = true;
  } else if (E.status_new && E.status_last && !E.replied_before) {  // the status window, in the component's loop()
    E.windows++;
    // panel_link.h's window(), run right after the end of the status frame was read. One frame every cycle,
    // with messages or as a poll.
    ns_t wait = pbus_wait_us(0) * US;
    uint32_t age =
        pbus_since_status((uint32_t) ((now + wait - E.prev_start) / US), E.prev_k, esp_threshold, ESP_ISR_US);
    int n = pbulk_esp_window(&ETX, pbus_status_left(age), E.txbuf);
    if (n) {
      E.txn = n;
      E.tx_bulk = true;
      E.tx_at = now + wait + esp_start();
      E.sent++;
    }
  } else if (pbulk_esp_gap_left(&ETX, &EG, (uint32_t) (now / US))) {  // a gap after a poll, panel_link.h's gap()
    ns_t wait = pbus_wait_us((uint32_t) ((now - E.seen_at) / US)) * US;
    uint32_t left = pbulk_esp_gap_left(&ETX, &EG, (uint32_t) ((now + wait) / US));
    int n = left ? pbulk_esp_window(&ETX, left, E.txbuf) : 0;
    if (n) {
      E.txn = n;
      E.tx_bulk = true;
      E.tx_at = now + wait + esp_start();
      E.gap_frames++;
    }
  }
  E.status_new = false;
  E.replied_before = E.replied;  // its reply may have gone out after the status frame
  E.replied = false;
  E.prev_start = start;
  E.prev_k = k;
  feed_help();
  feed_picture();
  E.loop_at = now + esp_loop_len();
}

static void rx_byte(int dev, uint8_t c, int src) {
  if (dev == ESP) {
    esp_rx(c);
    return;
  }
  if (dev == PANEL) {
    if (TX[PANEL].on)
      return;                   // its receiver is off while it sends
    if (now < PW.busy_until) {  // its UART holds one byte until the interrupt is free
      if (PW.held) {
        PW.overruns++;
        return;
      }
      PW.held = true;
    }
    if (!PB.n) {
      PB.first = now;
      PB.src = src;
    } else if (PB.src != src)
      PB.src = -1;
    if (PB.n < 600)
      PB.b[PB.n++] = c;
    PB.last = now;
    return;
  }
  if (dev == PANEL2) {
    if (TX[PANEL2].on)
      return;
    if (!P2.n)
      P2.src = src;
    if (P2.n < 600)
      P2.b[P2.n++] = c;
    P2.last = now;
    return;
  }
  if (!C.rx_en)
    return;  // the CU60 listens from the end of its frame until the line goes idle
  if (!C.rx_len)
    C.rx_src = src;
  else if (C.rx_src != src)
    C.rx_src = -1;
  if (C.rx_len < 300)
    C.rx_buf[C.rx_len] = c;
  C.rx_len++;
  if (C.rx_len > C.longest)
    C.longest = C.rx_len;
  if (C.rx_len > 256)
    C.overflows++;
}

static ns_t cu_rx_last;
static int cu_rx_seen;

static void cu60_rx(void) {
  static bool was_on;
  if (was_on && !TX[CU60].on) {  // its frame is out, start listening
    C.rx_en = true;
    C.rx_len = cu_rx_seen = 0;
    if (TX[CU60].n == PBUS_STATUS_LEN) {
      status_end_true = now;
      BUS.status_quiet = true;
    }
  }
  was_on = TX[CU60].on;
  if (!C.rx_en)
    return;
  if (C.rx_len != cu_rx_seen) {
    cu_rx_seen = C.rx_len;
    cu_rx_last = now;
  }
  if (C.rx_len && now >= cu_rx_last + BYTE) {  // line idle, rx_done and stop listening
    C.rx_done = true;
    C.rx_en = false;
    C.reply_ok = C.rx_src == C.want_src && C.rx_len == C.want_len;
  }
}

// ----------------------------------------------------------------
int main(int argc, char **argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 120;
  double ctl_ms = argc > 2 ? atof(argv[2]) : 1;
  rnd_state = argc > 3 ? (uint32_t) atoi(argv[3]) : 1;
  esp_threshold = argc > 4 ? atoi(argv[4]) : 1;
  PRESENT[2] = argc > 5 ? atoi(argv[5]) : 0;
  STATUS[0] = 0;
  STATUS[1] = 0x10;
  STATUS[2] = 0;
  STATUS[3] = 0xBE;
  STATUS[4] = 0;
  STATUS[5] = 0x55;
  STATUS[6] = 0xAA;
  for (int i = 7; i < PBUS_STATUS_LEN - 2; i++)
    STATUS[i] = (uint8_t) i;
  fin(STATUS, PBUS_STATUS_LEN - 2);

  C.io_c = 10;
  C.ctl_c = 1000;
  C.ht_c = 100;
  C.next_tick = between(0, MS);
  C.ctl_ns = (ns_t) (ctl_ms * MS);
  pbulk_esp_init(&ETX, 0x5A, 3);
  uint8_t too_long[PBULK_MSG_MAX + 1] = {0};
  bool refused_long = !pbulk_esp_send(&ETX, K_ITEM_HELP, too_long, sizeof too_long, false);
  pbus_panel_init(&PBUS);
  panel_drain_at = 5 * MS;

  ns_t end = (ns_t) (seconds * 1e9);
  ns_t worst_end = 0;  // the latest a link frame ended after a status frame
  for (now = 0; now < end; now += STEP) {
    step_tx();
    cu60_rx();
    cu60();
    panel();
    esp();
    if (PRESENT[2])
      panel2();
    for (int d = ESP; d >= PANEL; d--)
      if (BUS.status_quiet && TX[d].bulk && (TX[d].on || now < TX[d].busy_until) && now - status_end_true > worst_end)
        worst_end = now - status_end_true;
    if (rnd() % 3000000 == 0)
      panel_coils = 3;
  }

  printf("%.0f s simulated, CU60 control_tick %.1f ms, ESP RX threshold %d: %ld frames on the bus, %ld the link's\n",
         seconds, ctl_ms, esp_threshold, BUS.frames, BUS.bulk);
  printf("  CU60: %ld status broadcasts, longest burst it was handed %ld bytes, %ld overflows\n", C.status_n, C.longest,
         C.overflows);
  printf("  CU60 quiet after a status frame, shortest the panel saw: %.1f ms\n", PBUS.quiet_min_us / 1000.0);
  printf("  ESP: %ld windows, %u frames with messages, %u polls, %u answered, %u missed, %u too short%s\n", E.windows,
         ETX.frames, ETX.polls, ETX.answered, ETX.missed, ETX.short_windows, ETX.refused ? ", refused" : "");
  printf(
      "  panel: %u frames, %u polls, %u taken, %u busy, %u late, %u refused; it said %u, the ESP heard %u (%u wrong)\n",
      PRX.frames, PRX.polls, PRX.taken, PRX.busy, PRX.late, PRX.refused, panel_said, esp_heard, esp_heard_wrong);
  printf("  gaps after polls: the ESP saw %u (the panel %u) and sent %ld frames in them, the panel answered %ld%s%s\n",
         EG.gaps, PBUS.gaps.gaps, E.gap_frames, panel_gap_answers, EG.off || PBUS.gaps.off ? ", turned off" : "",
         ETX.no_gaps ? ", the panel said no" : "");
  printf("  the link off the bus by %.2f ms after the status frame (the promise: %.1f); %ld bytes the panel lost\n"
         "  while busy with a status frame\n",
         worst_end / 1e6, PBUS_END_US / 1000.0, PW.overruns);
  printf("  pictures of %d bytes: %ld intact, %ld not; the first after %.1f s, then one every %.1f s\n", PIC_LEN,
         pics_ok, pics_bad, first_pic_at / 1e9, pics_ok > 1 ? (last_pic_at - first_pic_at) / 1e9 / (pics_ok - 1) : 0);
  if (helps_sent > helps_got && now - help_queued > help_wait)
    help_wait = now - help_queued;  // still waiting
  printf("  help texts of %d bytes: %ld queued, %ld arrived, the longest wait %.1f s; one byte longer: %s\n",
         PBULK_MSG_MAX, helps_sent, helps_got, help_wait / 1e9, refused_long ? "refused" : "ACCEPTED");
  printf("  collisions: %ld of the link with the CU60, %ld answers lost to the ESP's driver staying on,\n"
         "              %ld without the link (the ESP answering the CU60 after it gave up)\n",
         BUS.cu60_hit, BUS.answers_hit, BUS.collisions - BUS.cu60_hit - BUS.answers_hit);
  // The gaps go off when the CU60 is more than PBUS_GAP_EARLY_US early, which happens when its main loop is
  // busy that long at a step. So they only have to stay on with control_tick <= 2.5 ms.
  bool ok = !BUS.cu60_hit && !C.overflows && !PW.overruns && (ctl_ms > 2.5 || (!EG.off && !PBUS.gaps.off)) &&
            !pics_bad && !esp_heard_wrong && panel_said - esp_heard < 20 && (ETX.refused || pics_ok > 0) &&
            refused_long && (ETX.refused || help_wait < HELP_WAIT_MAX);
  printf("%s\n", ok ? "OK" : "FAILED");
  return ok ? 0 : 1;
}
