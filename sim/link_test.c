// Both ends of the link against each other, without bus timing. Every message has to arrive once and in
// order, both ways.
//   make build/link_test && build/link_test
#include "pbulk.h"
#include "plink.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("%s:%d: %s\n", __FILE__, __LINE__, #c); \
      fails++; \
    } \
  } while (0)

static pbulk_esp_t E;
static pbulk_panel_t P;
static plink_t L;  // the panel's outgoing queue

// What each end got. Counted messages carry a sequence number, DATA one per 4 bytes.
static int got_esp, got_panel, wrong;
static uint32_t next_esp, next_panel;
static uint16_t last_key;  // the last uncounted message the panel got
static uint8_t last[256];
static int last_n, others;  // others = uncounted messages the panel got

static void at_esp(void *u, uint16_t k, const uint8_t *b, int n) {
  (void) u;
  if (k != K_ACTION || plink_number(b, n) != next_esp)
    wrong++;
  next_esp++;
  got_esp++;
}

static void at_panel(void *u, uint16_t k, const uint8_t *b, int n) {
  (void) u;
  if (k != K_ACTION && k != PBULK_DATA_KEY) {
    others++;
    last_key = k;
    memcpy(last, b, n);
    last_n = n;
    return;
  }
  for (int i = 0; i + 4 <= n; i += 4, next_panel++, got_panel++)
    if (plink_number(b + i, 4) != next_panel)
      wrong++;
}

static void reset(uint16_t esess, uint16_t psess) {
  pbulk_esp_init(&E, esess);
  pbulk_panel_init(&P, psess);
  plink_init(&L);
  got_esp = got_panel = wrong = others = 0;
  next_esp = next_panel = 0;
}

static void esp_send(uint16_t key, uint32_t v) {
  uint8_t b[4] = {(uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24)};
  CHECK(pbulk_esp_send(&E, key, b, 4, plink_kind(key) == PLINK_VALUE));
}

// The panel's main loop. Takes what came in and puts its own messages in the next answer.
static void panel_loop(void) {
  uint8_t p[PBULK_MAX];
  int n;
  while ((n = pbulk_panel_take(&P, p)) > 0)
    pbulk_parse(p, n, at_panel, NULL);
  if (!P.out_full && (n = plink_pack(&L, p, PBULK_ANSWER_MAX)) > 0)
    pbulk_panel_put(&P, p, n);
  P.waiting = (uint8_t) (plink_waiting(&L) > 255 ? 255 : plink_waiting(&L));
}

// One lossless transaction in the quiet after a status broadcast. Returns the panel's status, -1 if no answer.
static int transact(uint8_t say) {
  uint8_t f[PBULK_FRAME_MAX], a[PBULK_ANSWER_FRAME_MAX];
  int n = pbulk_esp_window(&E, 10000, f);  // 10 ms of the quiet left, as the ESP sees it
  int an = n ? pbulk_panel_frame(&P, f, n, 10000 - (uint32_t) (n + 1) * PBUS_BYTE_US, say, a) : 0;
  if (!an)
    return -1;
  pbulk_esp_answer(&E, a, an, at_esp, NULL);
  return a[PBULK_A_STATUS];
}

static void cycles(int n) {
  for (int i = 0; i < n; i++) {
    panel_loop();
    transact(PB_OK);
  }
  panel_loop();
}

// ---------------------------------------------------------------- lost frames and answers, a late ESP
static void test_random(void) {
  reset(0x2B, 0x51);
  int sent = 0, said = 0, lost = 0;
  unsigned seed = 1;
  for (int c = 0; c < 3000; c++) {
    while (sent < 4000 && pbulk_esp_room(&E) >= 8) {
      esp_send((sent / 50) % 2 ? PBULK_DATA_KEY : K_ACTION, (uint32_t) sent);
      sent++;
    }
    for (int i = 0; i < 5 && said < 600 && c % 4 == 0; i++)  // the panel sends a few messages now and then
      plink_send(&L, K_ACTION, (uint32_t) said++);
    panel_loop();
    seed = seed * 1103515245 + 12345;
    uint32_t since = 300 + (seed >> 16) % 3000;
    uint8_t f[PBULK_FRAME_MAX], a[PBULK_ANSWER_FRAME_MAX];
    int n = pbulk_esp_window(&E, PBUS_END_US - since, f);
    if (!n)
      continue;
    bool drop_frame = (seed >> 8) % 17 == 0, drop_answer = (seed >> 9) % 13 == 0;
    uint32_t late = (seed >> 10) % 23 == 0 ? 900 : 0;                // the ESP started later than it planned
    uint32_t at = since + late + (uint32_t) (n + 1) * PBUS_BYTE_US;  // when the panel's idle interrupt fires
    int an = drop_frame ? 0 : pbulk_panel_frame(&P, f, n, at < PBUS_END_US ? PBUS_END_US - at : 0, PB_OK, a);
    if (an) {
      CHECK(at + PBUS_PANEL_US + (uint32_t) an * PBUS_BYTE_US <= PBUS_END_US);  // off the bus in time
      if (!drop_answer)
        pbulk_esp_answer(&E, a, an, at_esp, NULL);
      else
        lost++;
    }
  }
  cycles(400);
  printf("random: ESP sent %d, panel got %d; panel sent %d, ESP got %d; %d answers lost, margin %u, %d wrong\n", sent,
         got_panel, said, got_esp, lost, E.margin_us, wrong);
  CHECK(!wrong && got_panel == sent && got_esp == said && !E.resyncs && !P.resyncs);
}

// ---------------------------------------------------------------- a newer value replaces one still waiting
static void test_merge(void) {
  reset(9, 7);
  uint8_t one[4] = {1}, two[4] = {2}, three[4] = {3};
  pbulk_esp_send(&E, K_THEME, one, 4, true);
  pbulk_esp_send(&E, K_THEME, two, 4, true);  // replaces it
  CHECK(E.qn == 7 && E.q[3] == 2);
  uint8_t f[PBULK_FRAME_MAX];
  pbulk_esp_window(&E, PBUS_END_US - 500, f);   // the merged value goes out
  pbulk_esp_send(&E, K_THEME, three, 4, true);  // already sent, so this one queues behind it
  CHECK(E.qn == 14 && E.q[3] == 2 && E.q[10] == 3);
  pbulk_esp_send(&E, K_ACTION, one, 4, false);  // events are never replaced
  pbulk_esp_send(&E, K_ACTION, one, 4, false);
  CHECK(E.qn == 28);
  uint8_t odd[3] = {0};
  CHECK(!pbulk_esp_send(&E, PBULK_DATA_KEY, odd, 3, false));  // DATA must be a multiple of 4 bytes
}

// ---------------------------------------------------------------- the ESP restarts after the panel's last
// messages went through, so nothing is waiting at the panel. What it sends next reaches the new ESP.
static void test_esp_restart(void) {
  reset(0x11, 0x77);
  for (int i = 0; i < 6; i++)
    plink_send(&L, K_ACTION, (uint32_t) i);
  cycles(10);
  CHECK(got_esp == 6 && !P.out_full);
  pbulk_esp_init(&E, 0x22);  // the ESP restarts
  transact(PB_OK);           // first it gets an empty answer
  got_esp = 0;
  for (int i = 0; i < 4; i++)
    plink_send(&L, K_ACTION, next_esp + (uint32_t) i);
  cycles(10);
  CHECK(got_esp == 4 && !wrong && !E.resyncs && !P.resyncs);  // and without needing a resync
}

// ---------------------------------------------------------------- S3: a restart that picks the session the
// other end knew. The ESP asks for a message the new panel never sent, so the panel picks a new session.
static void test_panel_same_session(void) {
  reset(0x11, 0x77);
  for (int i = 0; i < 20; i++) {
    plink_send(&L, K_ACTION, (uint32_t) i);
    esp_send(K_ACTION, (uint32_t) i);
  }
  cycles(20);
  CHECK(got_esp == 20 && got_panel == 20);
  pbulk_panel_init(&P, 0x77);  // the panel restarts with the same session
  plink_init(&L);
  for (int i = 20; i < 25; i++) {
    plink_send(&L, K_ACTION, (uint32_t) i);
    esp_send(K_ACTION, (uint32_t) i);
  }
  cycles(20);
  CHECK(got_esp == 25 && got_panel == 25 && !wrong && P.sessions == 1);
}

// The same for the ESP: the panel asks for messages the new ESP never sent.
static void test_esp_same_session(void) {
  reset(0x11, 0x77);
  for (int i = 0; i < 30; i++)
    esp_send(K_ACTION, (uint32_t) i);
  cycles(20);
  CHECK(got_panel == 30);
  pbulk_esp_init(&E, 0x11);  // the ESP restarts with the same session
  for (int i = 30; i < 35; i++)
    esp_send(K_ACTION, (uint32_t) i);
  cycles(20);
  CHECK(got_panel == 35 && !wrong && E.sessions == 1);
}

// ---------------------------------------------------------------- the panel's main loop is slow, its buffer fills
static void test_busy(void) {
  reset(0x31, 0x32);
  for (int i = 0; i < 300; i++)
    esp_send(PBULK_DATA_KEY, (uint32_t) i);
  int busy = 0;
  for (int i = 0; i < 20; i++)  // nothing drains the panel's buffer
    busy += transact(PB_OK) == PB_BUSY;
  CHECK(busy > 0 && P.busy > 0 && E.busy > 0);
  cycles(100);
  CHECK(got_panel == 300 && !wrong);
}

// ---------------------------------------------------------------- the panel refuses: nothing is taken and the
// ESP stops. With PB_NO_GAPS it only stops using the gaps.
static void test_refused(void) {
  reset(0x41, 0x42);
  esp_send(K_ACTION, 0);
  CHECK(transact(PB_REFUSED) == PB_REFUSED && E.refused && P.refused == 1);
  uint8_t f[PBULK_FRAME_MAX];
  CHECK(!pbulk_esp_window(&E, 10000, f));
  panel_loop();
  CHECK(!got_panel);
  reset(0x41, 0x42);
  esp_send(K_ACTION, 0);
  CHECK(transact(PB_NO_GAPS) == PB_NO_GAPS && E.no_gaps && !E.refused);
  pbus_gaps_t g = {.open = true, .req_us = 0, .next_us = 20000};
  CHECK(pbus_gaps_left(&g, 1000) && !pbulk_esp_gap_left(&E, &g, 1000));
  cycles(5);
  CHECK(got_panel == 1);
}

// ---------------------------------------------------------------- message numbers wrap around
static void test_wrap(void) {
  reset(0x51, 0x52);
  E.num = 0xFFF0;
  P.pnum = 0xFFF0;
  for (int i = 0; i < 40; i++)
    esp_send(i % 2 ? PBULK_DATA_KEY : K_ACTION, (uint32_t) i);
  for (int i = 0; i < 40; i++) {
    while (!plink_room(&L))
      cycles(1);
    plink_send(&L, K_ACTION, (uint32_t) i);
  }
  cycles(30);
  CHECK(got_panel == 40 && got_esp == 40 && !wrong);
}

// ---------------------------------------------------------------- a text arrives in one piece
static void test_text(void) {
  reset(0x61, 0x62);
  const char *t = "Stue \xC3\xB8st: 0123456789 0123456789 0123456789 0123456789 abcdefghi";  // 64 bytes
  CHECK(strlen(t) == PLINK_TEXT_MAX);
  CHECK(pbulk_esp_send(&E, K_ITEM_LABEL + 2, (const uint8_t *) t, PLINK_TEXT_MAX, false));
  cycles(3);
  CHECK(last_key == K_ITEM_LABEL + 2 && last_n == PLINK_TEXT_MAX && !memcmp(last, t, PLINK_TEXT_MAX));
}

// ---------------------------------------------------------------- the panel restarted: unsent messages are
// dropped, except values, which the ESP still has to send
static bool keep_values(uint16_t key) { return plink_kind(key) == PLINK_VALUE; }

static void test_drop_unsent(void) {
  reset(0x71, 0x72);
  for (int i = 0; i < 40; i++)
    esp_send(K_ACTION, (uint32_t) i);
  esp_send(K_THEME, 2);
  uint8_t f[PBULK_FRAME_MAX];
  pbulk_esp_window(&E, 4000, f);  // a short window, so only some go out
  int out = (uint16_t) (E.unsent - E.num);
  CHECK(out > 0 && out < 40);
  pbulk_esp_drop_unsent(&E, keep_values);
  CHECK(E.qn == out * 7 + 7);
  E.sent = false;  // that frame never arrived, so they're sent again
  cycles(10);
  CHECK(got_panel == out && !wrong && last_key == K_THEME && last[0] == 2);
}

// ---------------------------------------------------------------- an answer is lost and the resend has less room,
// so it splits the DATA in front of messages that went out. They still count as sent, and arrive once.
static void test_split_resend(void) {
  reset(0x81, 0x82);
  transact(PB_OK);  // first contact
  for (int i = 0; i < 16; i++)
    esp_send(PBULK_DATA_KEY, (uint32_t) i);  // one DATA message of 64 bytes
  esp_send(K_ACTION, 16);
  uint8_t none[1];
  CHECK(pbulk_esp_send(&E, K_ITEM_UNIT + 3, none, 0, false));  // an empty text last
  uint8_t f[PBULK_FRAME_MAX], a[PBULK_ANSWER_FRAME_MAX];
  int n = pbulk_esp_window(&E, 10000, f);
  CHECK(f[PBULK_N] == E.qn && pbulk_panel_frame(&P, f, n, 10000, PB_OK, a));  // all of it arrives, the answer not
  n = pbulk_esp_window(&E, 3800, f);
  CHECK(f[PBULK_N] < 64);
  uint16_t qn = E.qn;
  pbulk_esp_drop_unsent(&E, keep_values);  // all of it went out, so none is dropped
  CHECK(E.qn == qn);
  int an = pbulk_panel_frame(&P, f, n, 10000, PB_OK, a);
  pbulk_esp_answer(&E, a, an, at_esp, NULL);
  cycles(5);
  CHECK(got_panel == 17 && !wrong && others == 1 && !E.sessions && !E.qn);
}

// ---------------------------------------------------------------- the CU60 comes back too soon, then doesn't
static uint8_t STATUS[PBUS_STATUS_LEN], POLL[8], POLL1[8], REPLY[13];

static void frame(uint8_t *f, int n, const uint8_t *head, int k) {
  memcpy(f, head, k);
  uint16_t c = pbus_crc16(f, n - 2);
  f[n - 2] = c & 0xFF;
  f[n - 1] = c >> 8;
}

// One CU60 cycle from t_us as the panel sees it. Status broadcast, a poll of panel 3 quiet_us later, a reply
// with nothing flagged, then the CU60's next frame next_us after the poll.
static void bus_cycle(pbus_panel_t *w, uint32_t t_us, uint32_t quiet_us, uint32_t next_us) {
  uint32_t end = t_us + PBUS_STATUS_LEN * PBUS_BYTE_US;
  pbus_panel_burst(w, STATUS, PBUS_STATUS_LEN, t_us, end + PBUS_BYTE_US, (end + PBUS_BYTE_US) / 1000);
  uint32_t poll = end + quiet_us;
  pbus_panel_burst(w, POLL, 8, poll, poll + 9 * PBUS_BYTE_US, poll / 1000);
  pbus_panel_burst(w, REPLY, 13, poll + 1500, poll + 1500 + 14 * PBUS_BYTE_US, (poll + 1500) / 1000);
  pbus_panel_burst(w, POLL1, 8, poll + next_us, poll + next_us + 9 * PBUS_BYTE_US, (poll + next_us) / 1000);
}

// Cycles until the panel lets the link or the gaps back in, -1 if never.
static int cycles_to(pbus_panel_t *w, uint32_t *t, uint32_t quiet_us, uint32_t next_us, bool (*back)(pbus_panel_t *)) {
  for (int c = 0; c < 200000; c++, *t += 110000) {
    if (back(w))
      return c;
    bus_cycle(w, *t, quiet_us, next_us);
  }
  return -1;
}

static bool link_in(pbus_panel_t *w) { return !w->refused; }
static bool gaps_on(pbus_panel_t *w) { return !w->gaps.off; }

static void test_rearm(void) {
  frame(STATUS, PBUS_STATUS_LEN, (const uint8_t[]) {0x00, 0x10, 0x00, 0xBE, 0x00, 0x55, 0xAA}, 7);
  frame(POLL, 8, (const uint8_t[]) {3, 0x01, 0x00, 0x00, 0x00, 0x40}, 6);  // the settings poll of panel 3
  frame(POLL1, 8, (const uint8_t[]) {1, 0x01, 0x00, 0x00, 0x00, 0x40}, 6);
  frame(REPLY, 13, (const uint8_t[]) {3, 0x01, 8}, 3);
  pbus_panel_t w;
  pbus_panel_init(&w);
  uint32_t t = 0;
  uint8_t say;
  // the CU60 polls 14.4 ms after its status broadcast ends, 30 ms after it started, on its 10 ms grid
  for (int i = 0; i < 3; i++, t += 110000)
    bus_cycle(&w, t, 14400, 20000);
  CHECK(!w.refused && w.quiet_min_us == 14400 && w.gaps.gaps && !w.gaps.off);
  bus_cycle(&w, t, 12000, 20000);  // once at 12 ms
  t += 110000;
  pbus_panel_window(&w, t, t / 1000, &say);
  CHECK(w.refused && say == PBUS_REFUSED && w.quiet_min_us == 12000);
  CHECK(cycles_to(&w, &t, 14400, 20000, link_in) == PBUS_REARM);  // after PBUS_REARM clean cycles
  CHECK(w.quiet_min_us == 14400);                                 // reset when the link was let back in
  bus_cycle(&w, t, 12000, 20000);                                 // again, and now it takes twice as long
  t += 110000;
  CHECK(cycles_to(&w, &t, 14400, 20000, link_in) == 2 * PBUS_REARM);
  // Gaps after polls. The CU60 comes back 5 ms after a poll instead of 20, so the gaps go off. They come back
  // after a run of gaps the CU60 kept to, and the run is twice as long the next time.
  bus_cycle(&w, t, 14400, 5000);
  t += 110000;
  CHECK(w.gaps.off && !w.refused);
  CHECK(cycles_to(&w, &t, 14400, 20000, gaps_on) == PBUS_GAP_REARM);
  bus_cycle(&w, t, 14400, 5000);
  t += 110000;
  CHECK(cycles_to(&w, &t, 14400, 20000, gaps_on) == 2 * PBUS_GAP_REARM);
}

// ---------------------------------------------------------------- the panel's queue (plink)
static void test_plink(void) {
  plink_t l;
  plink_init(&l);
  plink_send(&l, K_THEME, 1);
  plink_send(&l, K_THEME, 2);  // a value, the newer one replaces it
  plink_send(&l, K_ACTION, 1);
  plink_send(&l, K_ACTION, 2);  // events, both kept
  CHECK(l.qn == 3 && l.q[0].value == 2);
  plink_drop_events(&l);
  CHECK(l.qn == 1 && l.q[0].key == K_THEME);
  uint8_t b[PBULK_ANSWER_MAX];
  int n = plink_pack(&l, b, sizeof b);
  CHECK(n == PLINK_NUMBER_BYTES && b[0] == (K_THEME & 0xFF) && b[2] == 4 && b[3] == 2 && !l.qn);
  for (int i = 0; i < PLINK_QUEUE; i++)
    plink_send(&l, K_ACTION, (uint32_t) i);
  CHECK(!plink_send(&l, K_ACTION, 99) && l.dropped == 1);
  CHECK(plink_pack(&l, b, 20) == 2 * PLINK_NUMBER_BYTES && l.qn == PLINK_QUEUE - 2 && l.q[0].value == 2);
  CHECK(plink_kind(K_ITEM_DEF + 3) == PLINK_BYTES && plink_kind(K_ITEM_LABEL + N_ITEMS - 1) == PLINK_BYTES &&
        plink_kind(K_ITEM_UNIT + 7) == PLINK_BYTES && plink_kind(K_ITEM_HELP) == PLINK_BYTES &&
        plink_kind(K_ITEM_ON) == PLINK_BYTES && plink_kind(K_ITEM_OFF + N_ITEMS - 1) == PLINK_BYTES &&
        plink_kind(K_PIC_DATA) == PLINK_BYTES && plink_kind(K_NOTIFY) == PLINK_BYTES);
  CHECK(plink_kind(K_HELLO) == PLINK_VALUE && plink_kind(K_ITEM_VALUE + N_ITEMS - 1) == PLINK_VALUE &&
        plink_kind(K_PIC_ACK) == PLINK_EVENT && plink_kind(K_ACTION) == PLINK_EVENT);
}

int main(void) {
  test_random();
  test_merge();
  test_esp_restart();
  test_panel_same_session();
  test_esp_same_session();
  test_busy();
  test_refused();
  test_wrap();
  test_text();
  test_drop_unsent();
  test_split_resend();
  test_rearm();
  test_plink();
  printf(fails ? "link_test: %d failed\n" : "link_test: all passed\n", fails);
  return fails != 0;
}
