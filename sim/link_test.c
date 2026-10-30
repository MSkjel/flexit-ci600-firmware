// Both ends of the link against each other, without bus timing. Every message has to arrive once and in
// order, both ways.
//   make build/link_test && build/link_test [TEST...]
#include "pbulk.h"
#include "plink.h"
#include "check.h"
#include "cx60.h"
#include <stdio.h>
#include <string.h>

static pbulk_esp_t E;
static pbulk_panel_t P;
static plink_t L;  // the panel's outgoing queue

// What each end got. Counted messages carry a sequence number, DATA one per 4 bytes.
static int got_esp, got_panel, wrong;
static uint32_t next_esp, next_panel;
static uint32_t data_goal, data_put, data_got;  // the panel's DATA, numbers of 4 bytes like the ESP's
static uint16_t last_key;                       // the last uncounted message the panel got
static uint8_t last[256];
static int last_n, others;  // others = uncounted messages the panel got

static void at_esp(void *u, uint16_t k, const uint8_t *b, int n) {
  (void) u;
  if (k == PBULK_DATA_KEY) {
    for (int i = 0; i + 4 <= n; i += 4, data_got++)
      if (plink_number(b + i, 4) != data_got)
        wrong++;
    return;
  }
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
  pbulk_esp_init(&E, esess, 3);
  pbulk_panel_init(&P, psess);
  plink_init(&L);
  got_esp = got_panel = wrong = others = 0;
  next_esp = next_panel = 0;
  data_goal = data_put = data_got = 0;
}

static void esp_send(uint16_t key, uint32_t v) {
  uint8_t b[4];
  plink_put32(b, v);
  CHECK(pbulk_esp_send(&E, key, b, 4, plink_bulk_flags(key)));
}

// The panel's main loop. Takes what came in and puts its own messages in the next answers, DATA in what's left.
static void panel_loop(void) {
  uint8_t p[PBULK_PAYLOAD_MAX];
  int n;
  while ((n = pbulk_panel_take(&P, p)) > 0)
    pbulk_parse(p, n, at_panel, NULL);
  while (pbulk_panel_free(&P)) {
    n = plink_pack(&L, p, PBULK_ANSWER_MAX);
    int k = (int) (data_goal - data_put) * 4, room = (PBULK_ANSWER_MAX - n - PBULK_MSG_HEAD) & ~3;
    if (k > room)
      k = room;
    if (k > 0) {
      plink_put16(p + n, PBULK_DATA_KEY);
      p[n + 2] = (uint8_t) k;
      for (int i = 0; i < k; i += 4)
        plink_put32(p + n + PBULK_MSG_HEAD + i, data_put++);
      n += PBULK_MSG_HEAD + k;
    }
    if (!n)
      break;
    pbulk_panel_put(&P, p, n);
  }
  unsigned w = plink_waiting(&L) + (data_goal - data_put) * 4;
  P.waiting = (uint8_t) (w > 255 ? 255 : w);
}

// One lossless transaction in the quiet after a status broadcast. Returns the panel's status, -1 if no answer.
static int transact(uint8_t say) {
  uint8_t f[PBULK_FRAME_MAX], a[PBULK_ANSWER_FRAME_MAX];
  int n = pbulk_esp_window(&E, 10000, f);  // 10 ms of the quiet left, as the ESP sees it
  int an = n ? pbulk_panel_frame(&P, 3, f, n, 10000 - (uint32_t) (n + 1) * PBUS_BYTE_US, say, PBUS_END_US, a) : 0;
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
  data_goal = 3000;
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
    int an =
        drop_frame ? 0 : pbulk_panel_frame(&P, 3, f, n, at < PBUS_END_US ? PBUS_END_US - at : 0, PB_OK, PBUS_END_US, a);
    if (an) {
      CHECK(at + PBUS_PANEL_US + (uint32_t) an * PBUS_BYTE_US <= PBUS_END_US);  // off the bus in time
      if (!drop_answer)
        pbulk_esp_answer(&E, a, an, at_esp, NULL);
      else
        lost++;
    }
  }
  cycles(400);
  printf("random: ESP sent %d, panel got %d; panel sent %d and %u of DATA, ESP got %d and %u; %d answers lost, margin "
         "%u, %d wrong\n",
         sent, got_panel, said, data_goal, got_esp, data_got, lost, E.margin_us, wrong);
  CHECK(!wrong);
  CHECK_EQ(got_panel, sent);
  CHECK_EQ(got_esp, said);
  CHECK_EQ(data_got, data_goal);
  CHECK(!E.resyncs);
  CHECK(!P.resyncs);
}

// ---------------------------------------------------------------- a newer value replaces one still waiting
static void test_merge(void) {
  reset(9, 7);
  uint8_t one[4] = {1}, two[4] = {2}, three[4] = {3};
  pbulk_esp_send(&E, K_THEME, one, 4, true);
  pbulk_esp_send(&E, K_THEME, two, 4, true);  // replaces it
  CHECK_EQ(E.qn, 7);
  CHECK_EQ(E.q[3], 2);
  uint8_t f[PBULK_FRAME_MAX];
  pbulk_esp_window(&E, PBUS_END_US - 500, f);   // the merged value goes out
  pbulk_esp_send(&E, K_THEME, three, 4, true);  // already sent, so this one queues behind it
  CHECK_EQ(E.qn, 14);
  CHECK_EQ(E.q[3], 2);
  CHECK_EQ(E.q[10], 3);
  pbulk_esp_send(&E, K_ACTION, one, 4, false);  // events are never replaced
  pbulk_esp_send(&E, K_ACTION, one, 4, false);
  CHECK_EQ(E.qn, 28);
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
  CHECK_EQ(got_esp, 6);
  CHECK(!P.out_full && !P.next_full);
  pbulk_esp_init(&E, 0x22, 3);  // the ESP restarts
  transact(PB_OK);              // first it gets an empty answer
  got_esp = 0;
  for (int i = 0; i < 4; i++)
    plink_send(&L, K_ACTION, next_esp + (uint32_t) i);
  cycles(10);
  CHECK_EQ(got_esp, 4);
  CHECK(!wrong);
  CHECK(!E.resyncs);
  CHECK(!P.resyncs);  // and without needing a resync
}

// ---------------------------------------------------------------- a restart that picks the session the other
// end knew. The ESP asks for a message the new panel never sent, so the panel picks a new session.
static void test_panel_same_session(void) {
  reset(0x11, 0x77);
  for (int i = 0; i < 20; i++) {
    plink_send(&L, K_ACTION, (uint32_t) i);
    esp_send(K_ACTION, (uint32_t) i);
  }
  cycles(20);
  CHECK_EQ(got_esp, 20);
  CHECK_EQ(got_panel, 20);
  pbulk_panel_init(&P, 0x77);  // the panel restarts with the same session
  plink_init(&L);
  for (int i = 20; i < 25; i++) {
    plink_send(&L, K_ACTION, (uint32_t) i);
    esp_send(K_ACTION, (uint32_t) i);
  }
  cycles(20);
  CHECK_EQ(got_esp, 25);
  CHECK_EQ(got_panel, 25);
  CHECK(!wrong);
  CHECK(P.psess != 0x77);  // a new session
}

// The same for the ESP: the panel asks for messages the new ESP never sent.
static void test_esp_same_session(void) {
  reset(0x11, 0x77);
  for (int i = 0; i < 30; i++)
    esp_send(K_ACTION, (uint32_t) i);
  cycles(20);
  CHECK_EQ(got_panel, 30);
  pbulk_esp_init(&E, 0x11, 3);  // the ESP restarts with the same session
  for (int i = 30; i < 35; i++)
    esp_send(K_ACTION, (uint32_t) i);
  cycles(20);
  CHECK_EQ(got_panel, 35);
  CHECK(!wrong);
  CHECK_EQ(E.sessions, 1);
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
  CHECK_EQ(got_panel, 300);
  CHECK(!wrong);
}

// ---------------------------------------------------------------- with plenty to send, each answer carries some.
// The one that says the ESP has all of out carries next, which the main loop put meanwhile.
static void test_panel_rate(void) {
  reset(0x51, 0x52);
  uint32_t seq = 0;
  int carried = 0;
  for (int c = 0; c < 40; c++) {
    while (plink_room(&L))
      plink_send(&L, K_ACTION, seq++);
    panel_loop();
    int had = got_esp;
    transact(PB_OK);
    carried += got_esp > had;
  }
  CHECK_EQ(carried, 40);
  CHECK(!wrong);
}

// ---------------------------------------------------------------- the panel refuses: nothing is taken and the
// ESP stops. With PB_NO_GAPS it only stops using the gaps.
static void test_refused(void) {
  reset(0x41, 0x42);
  esp_send(K_ACTION, 0);
  CHECK_EQ(transact(PB_REFUSED), PB_REFUSED);
  CHECK(E.refused);
  CHECK_EQ(P.refused, 1);
  uint8_t f[PBULK_FRAME_MAX];
  CHECK(!pbulk_esp_window(&E, 10000, f));
  panel_loop();
  CHECK(!got_panel);
  reset(0x41, 0x42);
  esp_send(K_ACTION, 0);
  CHECK_EQ(transact(PB_NO_GAPS), PB_NO_GAPS);
  CHECK(E.no_gaps);
  CHECK(!E.refused);
  pbus_gaps_t g = {.open = true, .req_us = 0, .next_us = 20000};
  CHECK(pbus_gaps_left(&g, 1000) && !pbulk_esp_gap_left(&E, &g, 1000));
  cycles(5);
  CHECK_EQ(got_panel, 1);
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
  CHECK_EQ(got_panel, 40);
  CHECK_EQ(got_esp, 40);
  CHECK(!wrong);
}

// ---------------------------------------------------------------- a text arrives in one piece
static void test_text(void) {
  reset(0x61, 0x62);
  const char *t = "Stue \xC3\xB8st: 0123456789 0123456789 0123456789 0123456789 abcdefghi";  // 64 bytes
  CHECK_EQ(strlen(t), PLINK_TEXT_MAX);
  CHECK(pbulk_esp_send(&E, K_ITEM_LABEL + 2, (const uint8_t *) t, PLINK_TEXT_MAX, false));
  cycles(3);
  CHECK_EQ(last_key, K_ITEM_LABEL + 2);
  CHECK_EQ(last_n, PLINK_TEXT_MAX);
  CHECK(!memcmp(last, t, PLINK_TEXT_MAX));
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
  CHECK_EQ(E.qn, out * 7 + 7);
  E.sent = false;  // that frame never arrived, so they're sent again
  cycles(10);
  CHECK_EQ(got_panel, out);
  CHECK(!wrong);
  CHECK_EQ(last_key, K_THEME);
  CHECK_EQ(last[0], 2);
}

// ---------------------------------------------------------------- a HELLO jumps the unsent messages
static void test_hello_first(void) {
  reset(0x91, 0x92);
  esp_send(K_ACTION, 0);
  uint8_t f[PBULK_FRAME_MAX];
  pbulk_esp_window(&E, 4000, f);  // a short window, only that one goes out
  esp_send(K_ACTION, 1);
  esp_send(PBULK_DATA_KEY, 2);
  esp_send(K_HELLO, 1);
  esp_send(K_HELLO, 2);
  esp_send(PBULK_DATA_KEY, 3);  // appended to the DATA at the end
  CHECK_EQ(E.qn, 4 * 7 + 4);
  CHECK_EQ(plink_get16(E.q + 7), K_HELLO);
  CHECK_EQ(E.q[10], 2);
  CHECK(pbulk_esp_queued(&E, K_HELLO));
  E.sent = false;  // that frame never arrived
  cycles(5);
  CHECK_EQ(got_panel, 4);
  CHECK(!wrong);
  CHECK_EQ(others, 1);
  CHECK_EQ(last_key, K_HELLO);
  CHECK_EQ(last[0], 2);
  CHECK(!pbulk_esp_queued(&E, K_HELLO) && !pbulk_esp_queued(&E, K_ACTION));
}

// ---------------------------------------------------------------- two panels, only the linked one answers
static void test_other_panel(void) {
  reset(0xA1, 0xA2);
  esp_send(K_ACTION, 0);
  uint8_t f[PBULK_FRAME_MAX], a[PBULK_ANSWER_FRAME_MAX], a2[PBULK_ANSWER_FRAME_MAX];
  int n = pbulk_esp_window(&E, 10000, f);
  pbulk_panel_t other;
  pbulk_panel_init(&other, 0x55);
  CHECK(!pbulk_panel_frame(&other, 2, f, n, 10000, PB_OK, PBUS_END_US, a2) && !other.frames && !other.bad);
  int an = pbulk_panel_frame(&P, 3, f, n, 10000, PB_OK, PBUS_END_US, a);
  CHECK(an);
  CHECK_EQ(P.frames, 1);
  CHECK(!P.bad);
  CHECK(!pbulk_panel_frame(&other, 2, a, an, 10000, PB_OK, PBUS_END_US, a2) && !other.frames && !other.bad);
  pbulk_esp_answer(&E, a, an, at_esp, NULL);
  cycles(3);
  CHECK_EQ(got_panel, 1);
  CHECK(!wrong);
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
  CHECK_EQ(f[PBULK_N], E.qn);
  CHECK(pbulk_panel_frame(&P, 3, f, n, 10000, PB_OK, PBUS_END_US, a));  // all of it arrives, not the answer
  n = pbulk_esp_window(&E, 3800, f);
  CHECK(f[PBULK_N] < 64);
  uint16_t qn = E.qn;
  pbulk_esp_drop_unsent(&E, keep_values);  // all of it went out, so none is dropped
  CHECK_EQ(E.qn, qn);
  int an = pbulk_panel_frame(&P, 3, f, n, 10000, PB_OK, PBUS_END_US, a);
  pbulk_esp_answer(&E, a, an, at_esp, NULL);
  cycles(5);
  CHECK_EQ(got_panel, 17);
  CHECK(!wrong);
  CHECK_EQ(others, 1);
  CHECK(!E.sessions);
  CHECK(!E.qn);
}

// ---------------------------------------------------------------- the CX60 comes back too soon, then doesn't
static uint8_t STATUS[PBUS_STATUS_LEN], POLL[8], POLL1[8], REPLY[13];

// One CX60 cycle from t_us as the panel sees it. Status broadcast, a poll of panel 3 quiet_us later, a reply
// with nothing flagged, then the CX60's next frame next_us after the poll.
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
  for (int c = 0; c < 200000; c++, *t += PBUS_CYCLE_US) {
    if (back(w))
      return c;
    bus_cycle(w, *t, quiet_us, next_us);
  }
  return -1;
}

static bool link_in(pbus_panel_t *w) { return !w->refused; }
static bool gaps_on(pbus_panel_t *w) { return !w->gaps.off; }

static void cx60_frames(void) {
  cx60_status(STATUS);
  cx60_poll(POLL, 3);
  cx60_poll(POLL1, 1);
  cx60_reply(REPLY);
}

static void test_rearm(void) {
  cx60_frames();
  pbus_panel_t w;
  pbus_panel_init(&w);
  uint32_t t = 0;
  uint8_t say;
  // the CX60 polls 14.4 ms after its status broadcast ends, 30 ms after it started, on its 10 ms grid
  for (int i = 0; i < 3; i++, t += PBUS_CYCLE_US)
    bus_cycle(&w, t, 14400, 20000);
  CHECK(!w.refused);
  CHECK_EQ(w.quiet_min_us, 14400);
  CHECK(w.gaps.gaps);
  CHECK(!w.gaps.off);
  bus_cycle(&w, t, 12000, 20000);  // once at 12 ms
  t += PBUS_CYCLE_US;
  pbus_panel_window(&w, t, t / 1000, &say);
  CHECK(w.refused);
  CHECK_EQ(say, PBUS_REFUSED);
  CHECK_EQ(w.quiet_min_us, 12000);
  CHECK_EQ(cycles_to(&w, &t, 14400, 20000, link_in), PBUS_REARM);  // after PBUS_REARM clean cycles
  CHECK_EQ(w.quiet_min_us, 14400);                                 // reset when the link was let back in
  bus_cycle(&w, t, 12000, 20000);                                  // again, and now it takes twice as long
  t += PBUS_CYCLE_US;
  CHECK_EQ(cycles_to(&w, &t, 14400, 20000, link_in), 2 * PBUS_REARM);
  // Gaps after polls. The CX60 comes back 5 ms after a poll instead of 20, so the gaps go off. They come back
  // after a run of gaps the CX60 kept to, and the run is twice as long the next time.
  bus_cycle(&w, t, 14400, 5000);
  t += PBUS_CYCLE_US;
  CHECK(w.gaps.off && !w.refused);
  CHECK_EQ(cycles_to(&w, &t, 14400, 20000, gaps_on), PBUS_GAP_REARM);
  bus_cycle(&w, t, 14400, 5000);
  t += PBUS_CYCLE_US;
  CHECK_EQ(cycles_to(&w, &t, 14400, 20000, gaps_on), 2 * PBUS_GAP_REARM);
}

// The deadline after a status broadcast follows the shortest quiet time measured. The ESP learns it from the
// answers and drops back after a frame that got none.
static void test_status_end(void) {
  cx60_frames();
  pbus_panel_t w;
  pbus_panel_init(&w);
  uint32_t t = 0;
  for (int i = 0; i < PBUS_END_SURE - 1; i++, t += PBUS_CYCLE_US)
    bus_cycle(&w, t, 14400, 20000);
  CHECK_EQ(w.end_us, PBUS_END_US);  // not measured long enough
  bus_cycle(&w, t, 14400, 20000);
  t += PBUS_CYCLE_US;
  CHECK_EQ(w.end_us, PBUS_END_MAX_US);
  bus_cycle(&w, t, 13650, 20000);  // once a bit sooner
  t += PBUS_CYCLE_US;
  CHECK_EQ(w.end_us, 12600);
  bus_cycle(&w, t, 12500, 20000);  // too soon
  t += PBUS_CYCLE_US;
  CHECK(w.refused);
  CHECK_EQ(w.end_us, PBUS_END_US);
  CHECK(cycles_to(&w, &t, 14400, 20000, link_in) > 0);
  CHECK_EQ(w.end_us, PBUS_END_US);  // measured again from scratch
  for (int i = 0; i < PBUS_END_SURE; i++, t += PBUS_CYCLE_US)
    bus_cycle(&w, t, 14400, 20000);
  CHECK_EQ(w.end_us, PBUS_END_MAX_US);

  pbulk_esp_t e;
  pbulk_panel_t p;
  pbulk_esp_init(&e, 0x31, 3);
  pbulk_panel_init(&p, 0x32);
  uint8_t f[PBULK_FRAME_MAX], a[PBULK_ANSWER_FRAME_MAX];
  int n = pbulk_esp_window(&e, 10000, f);
  int an = pbulk_panel_frame(&p, 3, f, n, 10000, PB_OK, 12600, a);
  pbulk_esp_answer(&e, a, an, at_esp, NULL);
  CHECK_EQ(e.end_us, 12600);
  CHECK_EQ(a[PBULK_A_STATUS] & 3, PB_OK);
  CHECK(!e.refused);
  CHECK_EQ(pbus_status_left(1000, e.end_us), 12600 - PBUS_ESP_START_US - 1000);
  pbulk_esp_window(&e, 10000, f);  // this one gets no answer
  pbulk_esp_no_answer(&e);
  CHECK_EQ(e.end_us, PBUS_END_US);
  CHECK_EQ(e.missed, 1);
  CHECK(!e.sent);
}

// A frame read late started no sooner than the one before it ended
static void test_bus_order(void) {
  cx60_frames();
  pbus_gaps_t g;
  memset(&g, 0, sizeof g);
  pbus_gaps_frame(&g, STATUS, PBUS_STATUS_LEN, 1000, 1000);
  uint32_t end = 1000 + PBUS_STATUS_LEN * PBUS_BYTE_NS / 1000;
  pbus_gaps_frame(&g, POLL, 8, 0, end + 15000);  // could have started any time since 0
  CHECK_EQ(g.step_lo_us, end);
  CHECK_EQ(g.end_lo_us, end + 8 * PBUS_BYTE_NS / 1000);
}

// ---------------------------------------------------------------- the panel's queue (plink)
static void test_plink(void) {
  plink_t l;
  plink_init(&l);
  plink_send(&l, K_THEME, 1);
  plink_send(&l, K_THEME, 2);  // a value, the newer one replaces it
  plink_send(&l, K_ACTION, 1);
  plink_send(&l, K_ACTION, 2);  // events, both kept
  plink_send(&l, K_ITEM_HELP, 3);
  plink_send(&l, K_ITEM_HELP, 4);  // only the latest help request counts
  plink_send(&l, K_ITEM_VALUE + 2, 5);
  CHECK_EQ(l.qn, 5);
  CHECK_EQ(l.value[0], 2);
  CHECK_EQ(l.value[3], 4);
  plink_forget(&l);
  CHECK_EQ(l.qn, 1);
  CHECK_EQ(l.key[0], K_THEME);
  uint8_t b[PBULK_ANSWER_MAX];
  int n = plink_pack(&l, b, sizeof b);
  CHECK_EQ(n, 4);
  CHECK_EQ(b[0], K_THEME & 0xFF);
  CHECK_EQ(b[1], K_THEME >> 8);
  CHECK_EQ(b[2], 1);
  CHECK_EQ(b[3], 2);
  CHECK(!l.qn);
  // numbers go without their high zero bytes, 0 as none at all, a negative one whole
  const uint32_t nums[] = {0, 1, 0xFF, 0x100, 0x123456, 0x80000000u, (uint32_t) -40};
  const int lens[] = {0, 1, 1, 2, 3, 4, 4};
  for (int i = 0; i < 7; i++) {
    uint8_t v[4];
    int len = plink_put_number(v, nums[i]);
    CHECK_EQ(len, lens[i]);
    CHECK_EQ(plink_number(v, len), nums[i]);
  }
  for (int i = 0; i < PLINK_QUEUE - 1; i++)
    plink_send(&l, K_ACTION, (uint32_t) i);
  CHECK(!plink_room(&l));
  CHECK(!plink_send(&l, K_ACTION, 99));
  CHECK_EQ(l.qn, PLINK_QUEUE - 1);
  // a HELLO always fits, goes first and replaces a waiting one
  CHECK(plink_send(&l, K_HELLO, 7));
  CHECK_EQ(l.qn, PLINK_QUEUE);
  CHECK_EQ(l.key[0], K_HELLO);
  CHECK(!l.value[1]);
  CHECK(plink_send(&l, K_HELLO, 8));
  CHECK_EQ(l.qn, PLINK_QUEUE);
  CHECK_EQ(l.value[0], 8);
  CHECK(!l.value[1]);
  CHECK_EQ(plink_waiting(&l), 4 + 3 + 4 * 30);  // HELLO 8, actions 0 and 1..30
  CHECK_EQ(plink_pack(&l, b, 20), 4 + 3 + 3 * 4);
  CHECK_EQ(l.qn, PLINK_QUEUE - 5);
  CHECK_EQ(l.value[0], 4);
  CHECK_EQ(plink_kind(K_ITEM_DEF + 3), PLINK_BYTES);
  CHECK_EQ(plink_kind(K_ITEM_LABEL + N_ITEMS - 1), PLINK_BYTES);
  CHECK_EQ(plink_kind(K_ITEM_UNIT + 7), PLINK_BYTES);
  CHECK_EQ(plink_kind(K_ITEM_HELP), PLINK_BYTES);
  CHECK_EQ(plink_kind(K_ITEM_ON), PLINK_BYTES);
  CHECK_EQ(plink_kind(K_ITEM_OFF + N_ITEMS - 1), PLINK_BYTES);
  CHECK_EQ(plink_kind(K_PIC_DATA), PLINK_BYTES);
  CHECK_EQ(plink_kind(K_NOTIFY), PLINK_BYTES);
  CHECK_EQ(plink_kind(K_HELLO), PLINK_VALUE);
  CHECK_EQ(plink_kind(K_ITEM_VALUE + N_ITEMS - 1), PLINK_VALUE);
  CHECK_EQ(plink_kind(K_PIC_ACK), PLINK_EVENT);
  CHECK_EQ(plink_kind(K_ACTION), PLINK_EVENT);
}

static const check_test_t TESTS[] = {
    {"random", test_random},
    {"merge", test_merge},
    {"esp_restart", test_esp_restart},
    {"panel_same_session", test_panel_same_session},
    {"esp_same_session", test_esp_same_session},
    {"busy", test_busy},
    {"panel_rate", test_panel_rate},
    {"refused", test_refused},
    {"wrap", test_wrap},
    {"text", test_text},
    {"drop_unsent", test_drop_unsent},
    {"split_resend", test_split_resend},
    {"hello_first", test_hello_first},
    {"other_panel", test_other_panel},
    {"rearm", test_rearm},
    {"status_end", test_status_end},
    {"bus_order", test_bus_order},
    {"plink", test_plink},
    {0},
};

int main(int argc, char **argv) {
  return check_main("link_test", (const check_test_t *const[]) {TESTS, NULL}, argc, argv);
}
