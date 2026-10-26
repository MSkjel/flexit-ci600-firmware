// app/anim.c: the animations' pool, its slots and places, and their frames
#include "unit_test.h"

// ---------------------------------------------------------------- the animations
uint8_t an[4][AH][AW / 2];

// A dot that moves, then stays put for a frame. Or moves on each one.
void dot_frames(bool moves) {
  memset(an, 0xFF, sizeof an);
  an[0][1][1] = 0x12;
  an[1][2][2] = 0x34;
  an[2][2][moves ? 1 : 2] = 0x34;
  an[3][0][0] = 0x56;
}

// ANIM_BEGIN at scale 2, colour 15 transparent, then the data
int32_t anim_send(int slot, int w, uint32_t len, const uint8_t *data, uint32_t crc) {
  uint8_t h[ANIM_BEGIN_HEAD + PIC_PAL_LEN];
  int n = sim_anim_begin(h, ++id, slot, w, AH, 2, 15, len, palette());
  return rest(pic_msg(K_ANIM_BEGIN, h, n), data, len, 5, crc);
}

void place_in(int p, int slot, int x, int y, int own, int start, int fade) {
  uint8_t m[10];
  anim_msg(K_ANIM_PLACE, m, sim_anim_place(m, p, slot, x, y, own, start, fade));
}

void place(int p, int slot, int x, int y, int own, int start) { place_in(p, slot, x, y, own, start, 0); }

void anim_clear_msg(unsigned v) {
  uint8_t m[4];
  plink_put32(m, v);
  pic_msg(K_ANIM_CLEAR, m, 4);
}

lv_obj_t *anims;

lv_obj_t *show_anims(void) { return anims = anim_show(lv_scr_act()); }

void no_anims(void) {
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
}

bool anim_pos(int k, int16_t *x, int16_t *y) {
  anim_look_t l;
  if (!anim_look(k, &l))
    return false;
  *x = l.x;
  *y = l.y;
  return true;
}

int look_opa(int place) {
  anim_look_t l;
  return anim_look(place, &l) ? l.opa : -1;
}

bool placed(void) {
  anim_look_t l;
  for (int k = 0; k < ANIM_PLACES; k++)
    if (anim_look(k, &l))
      return true;
  return false;
}

#define BEHIND 0xFF00FF  // what the animations are drawn over, in no test palette

// Place k's box as the layer draws it: each pixel's palette index, -1 where it's see-through, -2 for something
// between. False if it isn't drawn.
bool drawn(int k, anim_look_t *l, int *px) {
  static lv_color_t buf[PIC_MAX_W * PIC_MAX_H];
  if (!anims || !lv_obj_is_valid(anims) || !anim_look(k, l) || !(l->tiles & 1))
    return false;
  sim_draw(anims, &l->box, lv_color_hex(BEHIND), buf);
  for (uint32_t i = 0; i < lv_area_get_size(&l->box); i++) {
    px[i] = buf[i].full == lv_color_hex(BEHIND).full ? -1 : -2;
    for (int c = 0; c < 16; c++)
      if (buf[i].full == lv_color_hex(rgb(c)).full)
        px[i] = c;
  }
  return true;
}

// Whether place k shows frame f of the AW x AH frames, drawn at 2x
bool anim_shows(int k, int f) {
  static int px[PIC_MAX_W * PIC_MAX_H];
  anim_look_t l;
  if (!drawn(k, &l, px) || lv_area_get_width(&l.box) != AW * 2 || lv_area_get_height(&l.box) != AH * 2)
    return false;
  for (int y = 0; y < AH * 2; y++)
    for (int x = 0; x < AW * 2; x++) {
      uint8_t b = an[f][y / 2][x / 4], c = x / 2 & 1 ? b & 15 : b >> 4;
      if (px[y * AW * 2 + x] != (c == 15 ? -1 : c))
        return false;
    }
  return true;
}

// the records of the 4 test frames: frame 0 whole, the last record back to frame 0. Frame f shows for delay + f
uint32_t anim_records(uint8_t *d, int delay) {
  uint32_t n = 0;
  for (int f = 0; f <= 4; f++)
    n += plink_anim_record(f ? &an[f - 1][0][0] : NULL, &an[f % 4][0][0], AW, AH, (uint8_t) (delay + f), d + n);
  return n;
}

static void test_anim(void) {
  no_anims();
  dot_frames(false);
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  CHECK(!placed());  // not placed yet
  CHECK_EQ(anim_used(), AW / 2 * AH + n);
  CHECK(show_anims());
  CHECK(!anim_shows(0, 0));
  place(0, 0, 0, 10, 0, 0);
  CHECK(placed() && anim_shows(0, 0));  // shown as it's placed
  sim_run(50);                          // the first frame for 50 ms, the next for 60, ...
  CHECK(anim_shows(0, 1));
  sim_run(60);
  CHECK(anim_shows(0, 2));  // same as frame 1, so a record with no rows
  sim_run(70);
  CHECK(anim_shows(0, 3));
  sim_run(80);
  CHECK(anim_shows(0, 0));  // and back to the start
  lv_obj_del(anims);

  // bad sizes, then bad data
  CHECK_EQ(status(anim_send(0, AW - 1, n, d, 0)), PIC_BAD_SIZE);
  CHECK(!placed());  // the slot's gone with it
  CHECK_EQ(status(anim_send(0, PIC_MAX_W, n, d, 0)), PIC_BAD_SIZE);
  CHECK_EQ(status(anim_send(0, AW, ANIM_POOL, d, 0)), PIC_TOO_BIG);
  CHECK_EQ(status(anim_send(0, AW, UINT32_MAX, d, 0)), PIC_TOO_BIG);  // doesn't wrap round to fit
  uint8_t big[ANIM_BEGIN_HEAD + PIC_PAL_LEN];                         // a frame bigger than the pool
  int bn = sim_anim_begin(big, ++id, 1, 160, 100, 1, ANIM_OPAQUE, 4, palette());
  CHECK_EQ(status(pic_msg(K_ANIM_BEGIN, big, bn)), PIC_TOO_BIG);
  big[1] = ANIM_SLOTS;  // no such slot
  CHECK_EQ(status(pic_msg(K_ANIM_BEGIN, big, bn)), PIC_BAD);
  uint8_t bad[sizeof d];
  memcpy(bad, d, n);
  bad[4] = AH + 1;
  CHECK_EQ(status(anim_send(0, AW, n, bad, plink_crc32(0, bad, n))), PIC_BAD);
  CHECK_EQ(status(anim_send(0, AW, n, d, 12345)), PIC_BAD_CRC);
  uint8_t five[5] = {0x11, 0x11, 0x11, 0x11, 0x11}, over[5 + PDEFLATE_BOUND(5)] = {5, 0, 0, 4, 1};
  uint32_t on = 5 + pdeflate(five, 5, 5, 5, over + 5, PDEFLATE_BOUND(5));  // 5 bytes for a rectangle of 4
  CHECK_EQ(status(anim_send(0, AW, on, over, plink_crc32(0, over, on))), PIC_BAD);
  CHECK_EQ(status(anim_send(0, AW, n - 1, d, plink_crc32(0, d, n - 1))), PIC_BAD);  // the last record cut short
  CHECK_EQ(anim_used(), 0);
  // A picture coming in cancels an animation coming in. A finished animation stays until the ESP clears it.
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  uint8_t h[ANIM_BEGIN_HEAD + PIC_PAL_LEN];
  int hn = sim_anim_begin(h, ++id, 1, AW, AH, 1, 15, n, palette());
  CHECK_EQ(pic_msg(K_ANIM_BEGIN, h, hn), -1);
  CHECK_EQ(data(d, n / 2), -1);
  fill(0, H, 5);
  CHECK_EQ(status(send_picture(16)), PIC_DONE);
  CHECK(shows());
  CHECK(placed());
  CHECK_EQ(anim_used(), AW / 2 * AH + n);
  CHECK_EQ(data(d + n / 2, n - n / 2), -1);
  CHECK_EQ(end(plink_crc32(0, d, n)), -1);  // the rest is no longer expected
  CHECK_EQ(status(anim_send(1, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  CHECK(placed());
  anim_clear_msg(ANIM_ALL);
  CHECK(!placed());
  CHECK(shows());
  CHECK_EQ(anim_used(), 0);
  // Clearing another slot or a place leaves it coming in, clearing its own slot drops the rest of it
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  h[0] = ++id;
  CHECK_EQ(pic_msg(K_ANIM_BEGIN, h, hn), -1);
  CHECK_EQ(data(d, n / 2), -1);
  anim_clear_msg(ANIM_PLACE_BIT | 0);
  anim_clear_msg(0 | 300 << 8);
  CHECK_EQ(data(d + n / 2, n - n / 2), -1);
  CHECK_EQ(status(end(plink_crc32(0, d, n))), PIC_DONE);
  CHECK_EQ(anim_used(), AW / 2 * AH + n);
  h[0] = ++id;
  CHECK_EQ(pic_msg(K_ANIM_BEGIN, h, hn), -1);
  CHECK_EQ(data(d, n / 2), -1);
  anim_clear_msg(1 | 300 << 8);
  CHECK_EQ(data(d + n / 2, n - n / 2), -1);
  CHECK_EQ(end(plink_crc32(0, d, n)), -1);
  CHECK_EQ(anim_used(), 0);
}

// Two slots and places in step and out of step, sharing the pool
static void test_anim_slots(void) {
  no_anims();
  dot_frames(false);
  uint8_t d[512], d2[ANIM_POOL];
  uint32_t n = anim_records(d, 5);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  uint32_t n2 = anim_records(d2, 10);  // the same frames in slot 1, every one twice as long
  CHECK_EQ(status(anim_send(1, AW, n2, d2, plink_crc32(0, d2, n2))), PIC_DONE);
  unsigned frame = AW / 2 * AH, both = 2 * frame + n + n2;
  CHECK_EQ(anim_used(), both);
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 100, 0, 0, 0);    // in step with place 0
  place(2, 0, 200, 0, 1, 254);  // its own frame, 254 frames ahead: 2 of the 4
  place(3, 1, 0, 100, 0, 0);    // slot 1
  place(4, 1, 100, 100, 1, 0);  // its own frame, in step as it happens
  CHECK_EQ(anim_used(), both + 2 * frame);
  place(5, 2, 0, 0, 0, 0);                // no animation there
  place(6, 0, -10, PIC_MAX_H - 1, 0, 0);  // partly off the screen, where something can come in from
  place(ANIM_PLACES, 0, 0, 0, 0, 0);      // no such place
  int16_t x, y;
  CHECK(anim_pos(2, &x, &y));
  CHECK_EQ(x, 200);
  CHECK_EQ(y, 0);
  CHECK(!anim_pos(5, &x, &y));
  CHECK(anim_pos(6, &x, &y));
  CHECK_EQ(x, -10);
  CHECK_EQ(y, PIC_MAX_H - 1);
  show_anims();
  // slot 0 shows a frame for 50, 60, 70, 80 ms, slot 1 for 100, 110, ...; place 2 starts on frame 2
  CHECK(anim_shows(0, 0) && anim_shows(1, 0) && anim_shows(2, 2) && anim_shows(3, 0) && anim_shows(4, 0));
  sim_run(50);
  CHECK(anim_shows(0, 1) && anim_shows(1, 1) && anim_shows(2, 2) && anim_shows(3, 0) && anim_shows(4, 0));
  sim_run(50);  // 100: place 2 on to frame 3 at 70, slot 1 to frame 1
  CHECK(anim_shows(0, 1) && anim_shows(2, 3) && anim_shows(3, 1) && anim_shows(4, 1));
  sim_run(10);  // 110: slot 0 to frame 2
  CHECK(anim_shows(0, 2) && anim_shows(1, 2) && anim_shows(2, 3));
  sim_run(40);  // 150: place 2 back to frame 0
  CHECK(anim_shows(2, 0) && anim_shows(0, 2));
  // a new place takes the old one's spot, a place with a frame of its own gives it back when cleared
  place(2, 1, 50, 50, 0, 0);
  CHECK_EQ(anim_used(), both + frame);
  CHECK(anim_pos(2, &x, &y));
  CHECK_EQ(x, 50);
  anim_clear_msg(ANIM_PLACE_BIT | 4);
  CHECK_EQ(anim_used(), both);
  CHECK(!anim_pos(4, &x, &y));
  // the pool is packed: clearing slot 0 moves slot 1 down, and it plays on from where it was
  anim_clear_msg(0);
  CHECK_EQ(anim_used(), frame + n2);
  CHECK(!anim_pos(0, &x, &y));
  CHECK(!anim_pos(1, &x, &y));
  CHECK(anim_pos(3, &x, &y));
  CHECK(anim_shows(3, 1));
  sim_run(60);  // 210
  CHECK(anim_shows(3, 2));
  // one byte too many for what's left
  CHECK_EQ(status(anim_send(0, AW, ANIM_POOL - anim_used() - frame + 1, d, 0)), PIC_TOO_BIG);
  CHECK_EQ(anim_used(), frame + n2);
  anim_clear_msg(ANIM_ALL);
  CHECK_EQ(anim_used(), 0);
  // slot 1 padded with empty records until only one frame of its own fits behind slot 0 and it
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  uint32_t m = (ANIM_POOL - anim_used() - 2 * frame - n2) / 5;
  for (uint32_t i = 0; i < m; i++)
    memcpy(d2 + n2 + 5 * i, (uint8_t[]) {10, 0, 0, 0, 0}, 5);
  uint32_t n3 = n2 + 5 * m;
  CHECK_EQ(status(anim_send(1, AW, n3, d2, plink_crc32(0, d2, n3))), PIC_DONE);
  CHECK(ANIM_POOL - anim_used() >= frame && ANIM_POOL - anim_used() < 2 * frame);
  place(0, 0, 0, 0, 1, 0);
  place(1, 0, 20, 0, 1, 0);  // no room for its frame
  place(2, 1, 40, 0, 0, 0);  // no frame needed
  CHECK(anim_pos(0, &x, &y) && !anim_pos(1, &x, &y) && anim_pos(2, &x, &y));
  anim_clear_msg(ANIM_PLACE_BIT | 0);
  place(1, 0, 20, 0, 1, 0);
  CHECK(!anim_pos(0, &x, &y) && anim_pos(1, &x, &y));
  anim_clear_msg(ANIM_ALL);
  CHECK_EQ(anim_used(), 0);
  CHECK(!placed());
  lv_obj_clean(lv_scr_act());
}

void frames_msg(int slot, int rate) {
  uint8_t m[3] = {(uint8_t) slot};
  plink_put16(m + 1, rate);
  anim_msg(K_ANIM_FRAMES, m, sizeof m);
}

static void rounds_msg(int p, int rounds, int fade, int tag) {
  uint8_t m[6] = {(uint8_t) p};
  plink_put16(m + 1, rounds);
  plink_put16(m + 3, fade);
  m[5] = (uint8_t) tag;
  anim_msg(K_ANIM_ROUNDS, m, sizeof m);
}

// K_ANIM_ROUNDS: plays to the end of its rounds, a frame of its own holds the last one while it fades, then it goes
// and the panel says so
static void test_anim_rounds(void) {
  no_anims();
  dot_frames(true);
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);  // 50, 60, 70, 80 ms, and 90 for the first frame after that
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  while (anim_gone() >= 0)
    ;
  place(2, 0, 0, 10, 1, 0);
  show_anims();
  rounds_msg(2, 1, 200, 7);
  sim_run(170);
  CHECK(anim_shows(2, 2));
  CHECK_EQ(look_opa(2), 1000);
  sim_run(20);
  CHECK(anim_shows(2, 3));  // the end of its round
  sim_run(100);
  CHECK(look_opa(2) > 0);
  CHECK(look_opa(2) < 1000);
  CHECK_EQ(anim_gone(), -1);
  sim_run(200);
  int16_t x, y;
  CHECK(!anim_pos(2, &x, &y));
  CHECK_EQ(anim_gone(), 2 | 7 << 8);
  CHECK_EQ(anim_gone(), -1);
  // in step with the slot, the round it's in counts
  lv_obj_clean(lv_scr_act());
  place(0, 0, 0, 40, 0, 0);
  show_anims();
  rounds_msg(0, 2, 0, 9);
  sim_run(250);
  CHECK(anim_pos(0, &x, &y));
  CHECK_EQ(anim_gone(), -1);
  sim_run(450);
  CHECK(!anim_pos(0, &x, &y));
  CHECK_EQ(anim_gone(), 0 | 9 << 8);
  // placed again before the ESP heard, there's nothing to tell
  place(3, 0, 0, 60, 1, 0);
  show_anims();
  rounds_msg(3, 1, 0, 1);
  sim_run(400);
  CHECK(!anim_pos(3, &x, &y));
  place(3, 0, 0, 60, 1, 0);
  CHECK_EQ(anim_gone(), -1);
  // 0 plays on
  lv_obj_clean(lv_scr_act());
  show_anims();
  rounds_msg(3, 1, 0, 2);
  rounds_msg(3, 0, 0, 2);
  anim_msg(K_ANIM_ROUNDS, (uint8_t[]) {3, 1, 0, 0, 0}, 5);  // the wrong length
  sim_run(1000);
  CHECK(anim_pos(3, &x, &y));
  CHECK_EQ(anim_gone(), -1);
  // a single frame shows for its frame time each round
  uint8_t one[64];
  uint32_t n1 = plink_anim_record(NULL, &an[0][0][0], AW, AH, 10, one);
  CHECK_EQ(status(anim_send(1, AW, n1, one, plink_crc32(0, one, n1))), PIC_DONE);
  lv_obj_clean(lv_scr_act());
  place(4, 1, 100, 100, 0, 0);
  show_anims();
  rounds_msg(4, 3, 0, 2);
  sim_run(250);
  CHECK(anim_pos(4, &x, &y));
  sim_run(150);
  CHECK(!anim_pos(4, &x, &y));
  CHECK_EQ(anim_gone(), 4 | 2 << 8);
  // and only while it's shown, at its frame rate
  lv_obj_clean(lv_scr_act());
  place(4, 1, 100, 100, 0, 0);
  rounds_msg(4, 2, 0, 3);
  frames_msg(1, 50);  // 200 ms a round
  sim_run(3000);
  show_anims();
  sim_run(350);
  CHECK(anim_pos(4, &x, &y));
  sim_run(100);
  CHECK(!anim_pos(4, &x, &y));
  CHECK_EQ(anim_gone(), 4 | 3 << 8);
  // cleared while it fades out after its rounds, alone or with its slot: it fades on as it was and still says so
  for (int slot = 0; slot < 2; slot++) {
    place(2, 0, 0, 10, 1, 0);
    rounds_msg(2, 1, 400, 5);
    sim_run(200);
    anim_clear_msg(slot ? 0 | 100 << 8 : ANIM_PLACE_BIT | 2 | 100 << 8);
    sim_run(200);
    CHECK(anim_pos(2, &x, &y));
    CHECK_EQ(anim_gone(), -1);
    sim_run(300);
    CHECK(!anim_pos(2, &x, &y));
    CHECK_EQ(anim_gone(), 2 | 5 << 8);
  }
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
}

// FRAMES: a slot's frames go at its rate, the one it's on too, 0 holds it
static void test_anim_frames(void) {
  no_anims();
  dot_frames(true);
  uint8_t d[512];
  uint32_t n = anim_records(d, 10);  // 100, 110, 120, 130 ms
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  show_anims();
  frames_msg(0, 200);  // twice as fast, 50 ms left of the first frame
  sim_run(80);
  CHECK(anim_shows(0, 1));
  sim_run(50);  // 130, the next one 55 ms after 50
  CHECK(anim_shows(0, 2));
  sim_run(60);
  CHECK(anim_shows(0, 3));
  frames_msg(0, 0);
  sim_run(1000);
  CHECK(anim_shows(0, 3));
  frames_msg(0, 100);
  frames_msg(ANIM_SLOTS, 0);  // no such slot, too fast, the wrong length
  frames_msg(0, 1001);
  anim_msg(K_ANIM_FRAMES, (uint8_t[]) {0, 0}, 2);
  sim_run(100);
  CHECK(anim_shows(0, 0));  // on at once, for 140 ms
  sim_run(100);
  CHECK(anim_shows(0, 1));
  // to 1 % and back after a moment: what's left of the frame is back to its time at 100 %, not 11 s
  frames_msg(0, 1);
  sim_run(50);
  frames_msg(0, 100);
  sim_run(100);
  CHECK(anim_shows(0, 2));
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
}

const check_test_t ANIM_TESTS[] = {
    {"anim", test_anim},
    {"anim_slots", test_anim_slots},
    {"anim_rounds", test_anim_rounds},
    {"anim_frames", test_anim_frames},
    {0},
};
