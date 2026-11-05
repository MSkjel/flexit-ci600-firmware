// app/overlay.c: the overlays' pool, its slots and places, and their frames
#include "unit_test.h"

// ---------------------------------------------------------------- the overlays
uint8_t an[4][AH][AW / 2];

// A dot that moves, then stays put for a frame. Or moves on each one.
void dot_frames(bool moves) {
  memset(an, 0xFF, sizeof an);
  an[0][1][1] = 0x12;
  an[1][2][2] = 0x34;
  an[2][2][moves ? 1 : 2] = 0x34;
  an[3][0][0] = 0x56;
}

// rgb()'s colours as an overlay has them, 15 clear
static const uint32_t *clear15(void) {
  static uint32_t pal[16];
  for (int i = 0; i < 15; i++)
    pal[i] = 0xFF000000 | rgb(i);
  return pal;
}

// OVERLAY_BEGIN of kind, w x h at scale, then the data in pieces of piece bytes
static int32_t send_kind(int kind, int slot, int w, int h, int scale, const uint32_t *pal, uint32_t len,
                         const uint8_t *data, unsigned piece) {
  uint8_t b[OVERLAY_BEGIN_HEAD + OVERLAY_PAL_LEN];
  int n = sim_overlay_begin(b, ++id, slot, kind, w, h, scale, len, pal);
  return rest(pic_msg(K_OVERLAY_BEGIN, b, n), data, len, piece, plink_crc32(0, data, len));
}

// An animated OVERLAY_BEGIN at scale 2, colour 15 clear, then the data
int32_t overlay_send(int slot, int w, uint32_t len, const uint8_t *data, uint32_t crc) {
  uint8_t h[OVERLAY_BEGIN_HEAD + OVERLAY_PAL_LEN];
  int n = sim_overlay_begin(h, ++id, slot, OVERLAY_ANIMATED, w, AH, 2, len, clear15());
  return rest(pic_msg(K_OVERLAY_BEGIN, h, n), data, len, 5, crc);
}

void place_in(int p, int slot, int x, int y, int own, int start, int fade) {
  uint8_t m[10];
  overlay_msg(K_OVERLAY_PLACE, m, sim_overlay_place(m, p, slot, x, y, own, start, fade));
}

void place(int p, int slot, int x, int y, int own, int start) { place_in(p, slot, x, y, own, start, 0); }

void overlay_clear_msg(unsigned v) {
  uint8_t m[4];
  plink_put32(m, v);
  pic_msg(K_OVERLAY_CLEAR, m, 4);
}

lv_obj_t *overlays;

lv_obj_t *show_overlays(void) { return overlays = overlay_show(lv_scr_act()); }

void no_overlays(void) {
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

bool overlay_pos(int k, int16_t *x, int16_t *y) {
  overlay_look_t l;
  if (!overlay_look(k, &l))
    return false;
  *x = l.x;
  *y = l.y;
  return true;
}

int look_opa(int place) {
  overlay_look_t l;
  return overlay_look(place, &l) ? l.opa : -1;
}

bool placed(void) {
  overlay_look_t l;
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (overlay_look(k, &l))
      return true;
  return false;
}

#define BEHIND 0xFF00FF  // what the overlays are drawn over, in no test palette

// Place k's box as the layer draws it over BEHIND into buf, in strips of lines. False if it isn't drawn.
static bool draw_box(int k, overlay_look_t *l, lv_color_t *buf, int lines) {
  if (!overlays || !lv_obj_is_valid(overlays) || !overlay_look(k, l) || !(l->tiles & 1))
    return false;
  int w = lv_area_get_width(&l->box);
  for (int y = l->box.y1; y <= l->box.y2; y += lines) {
    lv_area_t a = {l->box.x1, y, l->box.x2, LV_MIN(y + lines - 1, l->box.y2)};
    sim_draw(overlays, &a, lv_color_hex(BEHIND), buf + (y - l->box.y1) * w);
  }
  return true;
}

// Place k's box as the layer draws it: each pixel's palette index, -1 where it's see-through, -2 for something
// between. False if it isn't drawn.
bool drawn(int k, overlay_look_t *l, int *px) {
  static lv_color_t buf[PIC_MAX_W * PIC_MAX_H];
  if (!draw_box(k, l, buf, PIC_MAX_H))
    return false;
  for (uint32_t i = 0; i < lv_area_get_size(&l->box); i++) {
    px[i] = buf[i].full == lv_color_hex(BEHIND).full ? -1 : -2;
    for (int c = 0; c < 16; c++)
      if (buf[i].full == lv_color_hex(rgb(c)).full)
        px[i] = c;
  }
  return true;
}

// Whether place k shows frame f of the AW x AH frames, drawn at 2x
bool overlay_shows(int k, int f) {
  static int px[PIC_MAX_W * PIC_MAX_H];
  overlay_look_t l;
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
uint32_t overlay_records(uint8_t *d, int delay) {
  uint32_t n = 0;
  for (int f = 0; f <= 4; f++)
    n += plink_overlay_record(f ? &an[f - 1][0][0] : NULL, &an[f % 4][0][0], AW, AH, (uint8_t) (delay + f), d + n);
  return n;
}

static void test_anim(void) {
  no_overlays();
  dot_frames(false);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  CHECK(!placed());  // not placed yet
  CHECK_EQ(overlay_used(), AW / 2 * AH + n);
  CHECK(show_overlays());
  CHECK(!overlay_shows(0, 0));
  place(0, 0, 0, 10, 0, 0);
  CHECK(placed() && overlay_shows(0, 0));  // shown as it's placed
  sim_run(50);                             // the first frame for 50 ms, the next for 60, ...
  CHECK(overlay_shows(0, 1));
  sim_run(60);
  CHECK(overlay_shows(0, 2));  // same as frame 1, so a record with no rows
  sim_run(70);
  CHECK(overlay_shows(0, 3));
  sim_run(80);
  CHECK(overlay_shows(0, 0));  // and back to the start
  lv_obj_del(overlays);

  // bad sizes, then bad data
  CHECK_EQ(status(overlay_send(0, AW - 1, n, d, 0)), PIC_BAD_SIZE);
  CHECK(!placed());  // the slot's gone with it
  CHECK_EQ(status(overlay_send(0, PIC_MAX_W, n, d, 0)), PIC_BAD_SIZE);
  CHECK_EQ(status(overlay_send(0, AW, OVERLAY_POOL, d, 0)), PIC_TOO_BIG);
  CHECK_EQ(status(overlay_send(0, AW, UINT32_MAX, d, 0)), PIC_TOO_BIG);  // doesn't wrap round to fit
  uint8_t big[OVERLAY_BEGIN_HEAD + OVERLAY_PAL_LEN];                     // a frame bigger than the pool, 15000 bytes
  int bn = sim_overlay_begin(big, ++id, 1, OVERLAY_ANIMATED, 300, 100, 1, 4, clear15());
  CHECK_EQ(status(pic_msg(K_OVERLAY_BEGIN, big, bn)), PIC_TOO_BIG);
  big[OVERLAY_BEGIN_KIND] = OVERLAY_STILL + 1;  // no such kind
  CHECK_EQ(status(pic_msg(K_OVERLAY_BEGIN, big, bn)), PIC_BAD);
  big[OVERLAY_BEGIN_KIND] = OVERLAY_ANIMATED;
  big[OVERLAY_BEGIN_SLOT] = OVERLAY_SLOTS;  // no such slot
  CHECK_EQ(status(pic_msg(K_OVERLAY_BEGIN, big, bn)), PIC_BAD);
  CHECK_EQ(status(pic_msg(K_OVERLAY_BEGIN, big, bn - 1)), PIC_BAD);  // the opacities cut short
  uint8_t bad[sizeof d];
  memcpy(bad, d, n);
  bad[4] = AH + 1;
  CHECK_EQ(status(overlay_send(0, AW, n, bad, plink_crc32(0, bad, n))), PIC_BAD);
  CHECK_EQ(status(overlay_send(0, AW, n, d, 12345)), PIC_BAD_CRC);
  uint8_t five[5] = {0x11, 0x11, 0x11, 0x11, 0x11}, over[sizeof d + 5 + PDEFLATE_BOUND(5)];
  uint32_t on = plink_overlay_record(NULL, &an[0][0][0], AW, AH, 5, over);
  memcpy(over + on, (uint8_t[]) {5, 0, 0, 4, 1}, 5);
  on += 5 + pdeflate(five, 5, 5, 5, over + on + 5, PDEFLATE_BOUND(5));  // then 5 bytes for a rectangle of 4
  CHECK_EQ(status(overlay_send(0, AW, on, over, plink_crc32(0, over, on))), PIC_BAD);
  uint32_t part = plink_overlay_record(NULL, &an[0][0][0], AW - 2, AH, 5, over);  // the first not the whole frame
  CHECK_EQ(status(overlay_send(0, AW, part, over, plink_crc32(0, over, part))), PIC_BAD);
  CHECK_EQ(status(overlay_send(0, AW, n - 1, d, plink_crc32(0, d, n - 1))), PIC_BAD);  // the last record cut short
  CHECK_EQ(overlay_used(), 0);
  // A picture coming in cancels an animation coming in. A finished animation stays until the ESP clears it.
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  uint8_t h[OVERLAY_BEGIN_HEAD + OVERLAY_PAL_LEN];
  int hn = sim_overlay_begin(h, ++id, 1, OVERLAY_ANIMATED, AW, AH, 1, n, clear15());
  CHECK_EQ(pic_msg(K_OVERLAY_BEGIN, h, hn), -1);
  CHECK_EQ(data(d, n / 2), -1);
  fill(0, H, 5);
  CHECK_EQ(status(send_picture(16)), PIC_DONE);
  CHECK(shows());
  CHECK(placed());
  CHECK_EQ(overlay_used(), AW / 2 * AH + n);
  CHECK_EQ(data(d + n / 2, n - n / 2), -1);
  CHECK_EQ(end(plink_crc32(0, d, n)), -1);  // the rest is no longer expected
  CHECK_EQ(status(overlay_send(1, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  CHECK(placed());
  overlay_clear_msg(OVERLAY_ALL);
  CHECK(!placed());
  CHECK(shows());
  CHECK_EQ(overlay_used(), 0);
  // Clearing another slot or a place leaves it coming in, clearing its own slot drops the rest of it
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  h[0] = ++id;
  CHECK_EQ(pic_msg(K_OVERLAY_BEGIN, h, hn), -1);
  CHECK_EQ(data(d, n / 2), -1);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0);
  overlay_clear_msg(0 | 300 << 8);
  CHECK_EQ(data(d + n / 2, n - n / 2), -1);
  CHECK_EQ(status(end(plink_crc32(0, d, n))), PIC_DONE);
  CHECK_EQ(overlay_used(), AW / 2 * AH + n);
  h[0] = ++id;
  CHECK_EQ(pic_msg(K_OVERLAY_BEGIN, h, hn), -1);
  CHECK_EQ(data(d, n / 2), -1);
  overlay_clear_msg(1 | 300 << 8);
  CHECK_EQ(data(d + n / 2, n - n / 2), -1);
  CHECK_EQ(end(plink_crc32(0, d, n)), -1);
  CHECK_EQ(overlay_used(), 0);
}

// Two slots and places in step and out of step, sharing the pool
static void test_anim_slots(void) {
  no_overlays();
  dot_frames(false);
  uint8_t d[512], d2[OVERLAY_POOL];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  uint32_t n2 = overlay_records(d2, 10);  // the same frames in slot 1, every one twice as long
  CHECK_EQ(status(overlay_send(1, AW, n2, d2, plink_crc32(0, d2, n2))), PIC_DONE);
  unsigned frame = AW / 2 * AH, both = 2 * frame + n + n2;
  CHECK_EQ(overlay_used(), both);
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 100, 0, 0, 0);    // in step with place 0
  place(2, 0, 200, 0, 1, 254);  // its own frame, 254 frames ahead: 2 of the 4
  place(3, 1, 0, 100, 0, 0);    // slot 1
  place(4, 1, 100, 100, 1, 0);  // its own frame, in step as it happens
  CHECK_EQ(overlay_used(), both + 2 * frame);
  place(5, 2, 0, 0, 0, 0);                // no animation there
  place(6, 0, -10, PIC_MAX_H - 1, 0, 0);  // partly off the screen, where something can come in from
  place(OVERLAY_PLACES, 0, 0, 0, 0, 0);   // no such place
  int16_t x, y;
  CHECK(overlay_pos(2, &x, &y));
  CHECK_EQ(x, 200);
  CHECK_EQ(y, 0);
  CHECK(!overlay_pos(5, &x, &y));
  CHECK(overlay_pos(6, &x, &y));
  CHECK_EQ(x, -10);
  CHECK_EQ(y, PIC_MAX_H - 1);
  show_overlays();
  // slot 0 shows a frame for 50, 60, 70, 80 ms, slot 1 for 100, 110, ...; place 2 starts on frame 2
  CHECK(overlay_shows(0, 0) && overlay_shows(1, 0) && overlay_shows(2, 2) && overlay_shows(3, 0) &&
        overlay_shows(4, 0));
  sim_run(50);
  CHECK(overlay_shows(0, 1) && overlay_shows(1, 1) && overlay_shows(2, 2) && overlay_shows(3, 0) &&
        overlay_shows(4, 0));
  sim_run(50);  // 100: place 2 on to frame 3 at 70, slot 1 to frame 1
  CHECK(overlay_shows(0, 1) && overlay_shows(2, 3) && overlay_shows(3, 1) && overlay_shows(4, 1));
  sim_run(10);  // 110: slot 0 to frame 2
  CHECK(overlay_shows(0, 2) && overlay_shows(1, 2) && overlay_shows(2, 3));
  sim_run(40);  // 150: place 2 back to frame 0
  CHECK(overlay_shows(2, 0) && overlay_shows(0, 2));
  // a new place takes the old one's spot, a place with a frame of its own gives it back when cleared
  place(2, 1, 50, 50, 0, 0);
  CHECK_EQ(overlay_used(), both + frame);
  CHECK(overlay_pos(2, &x, &y));
  CHECK_EQ(x, 50);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 4);
  CHECK_EQ(overlay_used(), both);
  CHECK(!overlay_pos(4, &x, &y));
  // the pool is packed: clearing slot 0 moves slot 1 down, and it plays on from where it was
  overlay_clear_msg(0);
  CHECK_EQ(overlay_used(), frame + n2);
  CHECK(!overlay_pos(0, &x, &y));
  CHECK(!overlay_pos(1, &x, &y));
  CHECK(overlay_pos(3, &x, &y));
  CHECK(overlay_shows(3, 1));
  sim_run(60);  // 210
  CHECK(overlay_shows(3, 2));
  // one byte too many for what's left
  CHECK_EQ(status(overlay_send(0, AW, OVERLAY_POOL - overlay_used() - frame + 1, d, 0)), PIC_TOO_BIG);
  CHECK_EQ(overlay_used(), frame + n2);
  overlay_clear_msg(OVERLAY_ALL);
  CHECK_EQ(overlay_used(), 0);
  // slot 1 padded with empty records until only one frame of its own fits behind slot 0 and it
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  uint32_t m = (OVERLAY_POOL - overlay_used() - 2 * frame - n2) / 5;
  for (uint32_t i = 0; i < m; i++)
    memcpy(d2 + n2 + 5 * i, (uint8_t[]) {10, 0, 0, 0, 0}, 5);
  uint32_t n3 = n2 + 5 * m;
  CHECK_EQ(status(overlay_send(1, AW, n3, d2, plink_crc32(0, d2, n3))), PIC_DONE);
  CHECK(OVERLAY_POOL - overlay_used() >= frame && OVERLAY_POOL - overlay_used() < 2 * frame);
  place(0, 0, 0, 0, 1, 0);
  place(1, 0, 20, 0, 1, 0);  // no room for its frame
  place(2, 1, 40, 0, 0, 0);  // no frame needed
  CHECK(overlay_pos(0, &x, &y) && !overlay_pos(1, &x, &y) && overlay_pos(2, &x, &y));
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0);
  place(1, 0, 20, 0, 1, 0);
  CHECK(!overlay_pos(0, &x, &y) && overlay_pos(1, &x, &y));
  overlay_clear_msg(OVERLAY_ALL);
  CHECK_EQ(overlay_used(), 0);
  CHECK(!placed());
  lv_obj_clean(lv_scr_act());
}

void frames_msg(int slot, int rate) {
  uint8_t m[3] = {(uint8_t) slot};
  plink_put16(m + 1, rate);
  overlay_msg(K_OVERLAY_FRAMES, m, sizeof m);
}

static void rounds_msg(int p, int rounds, int fade, int tag) {
  uint8_t m[6] = {(uint8_t) p};
  plink_put16(m + 1, rounds);
  plink_put16(m + 3, fade);
  m[5] = (uint8_t) tag;
  overlay_msg(K_OVERLAY_ROUNDS, m, sizeof m);
}

// K_OVERLAY_ROUNDS: plays to the end of its rounds, a frame of its own holds the last one while it fades, then it goes
// and the panel says so
static void test_anim_rounds(void) {
  no_overlays();
  dot_frames(true);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);  // 50, 60, 70, 80 ms, and 90 for the first frame after that
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  while (overlay_gone() >= 0)
    ;
  place(2, 0, 0, 10, 1, 0);
  show_overlays();
  rounds_msg(2, 1, 200, 7);
  sim_run(170);
  CHECK(overlay_shows(2, 2));
  CHECK_EQ(look_opa(2), 1000);
  sim_run(20);
  CHECK(overlay_shows(2, 3));  // the end of its round
  sim_run(100);
  CHECK(look_opa(2) > 0);
  CHECK(look_opa(2) < 1000);
  CHECK_EQ(overlay_gone(), -1);
  sim_run(200);
  int16_t x, y;
  CHECK(!overlay_pos(2, &x, &y));
  CHECK_EQ(overlay_gone(), 2 | 7 << 8);
  CHECK_EQ(overlay_gone(), -1);
  // in step with the slot, the round it's in counts
  lv_obj_clean(lv_scr_act());
  place(0, 0, 0, 40, 0, 0);
  show_overlays();
  rounds_msg(0, 2, 0, 9);
  sim_run(250);
  CHECK(overlay_pos(0, &x, &y));
  CHECK_EQ(overlay_gone(), -1);
  sim_run(450);
  CHECK(!overlay_pos(0, &x, &y));
  CHECK_EQ(overlay_gone(), 0 | 9 << 8);
  // placed again before the ESP heard, there's nothing to tell
  place(3, 0, 0, 60, 1, 0);
  show_overlays();
  rounds_msg(3, 1, 0, 1);
  sim_run(400);
  CHECK(!overlay_pos(3, &x, &y));
  place(3, 0, 0, 60, 1, 0);
  CHECK_EQ(overlay_gone(), -1);
  // 0 plays on
  lv_obj_clean(lv_scr_act());
  show_overlays();
  rounds_msg(3, 1, 0, 2);
  rounds_msg(3, 0, 0, 2);
  overlay_msg(K_OVERLAY_ROUNDS, (uint8_t[]) {3, 1, 0, 0, 0}, 5);  // the wrong length
  sim_run(1000);
  CHECK(overlay_pos(3, &x, &y));
  CHECK_EQ(overlay_gone(), -1);
  // a single frame shows for its frame time each round
  uint8_t one[64];
  uint32_t n1 = plink_overlay_record(NULL, &an[0][0][0], AW, AH, 10, one);
  CHECK_EQ(status(overlay_send(1, AW, n1, one, plink_crc32(0, one, n1))), PIC_DONE);
  lv_obj_clean(lv_scr_act());
  place(4, 1, 100, 100, 0, 0);
  show_overlays();
  rounds_msg(4, 3, 0, 2);
  sim_run(250);
  CHECK(overlay_pos(4, &x, &y));
  sim_run(150);
  CHECK(!overlay_pos(4, &x, &y));
  CHECK_EQ(overlay_gone(), 4 | 2 << 8);
  // and only while it's shown, at its frame rate
  lv_obj_clean(lv_scr_act());
  place(4, 1, 100, 100, 0, 0);
  rounds_msg(4, 2, 0, 3);
  frames_msg(1, 50);  // 200 ms a round
  sim_run(3000);
  show_overlays();
  sim_run(350);
  CHECK(overlay_pos(4, &x, &y));
  sim_run(100);
  CHECK(!overlay_pos(4, &x, &y));
  CHECK_EQ(overlay_gone(), 4 | 3 << 8);
  // cleared while it fades out after its rounds, alone or with its slot: it fades on as it was and still says so
  for (int slot = 0; slot < 2; slot++) {
    place(2, 0, 0, 10, 1, 0);
    rounds_msg(2, 1, 400, 5);
    sim_run(200);
    overlay_clear_msg(slot ? 0 | 100 << 8 : OVERLAY_PLACE_BIT | 2 | 100 << 8);
    sim_run(200);
    CHECK(overlay_pos(2, &x, &y));
    CHECK_EQ(overlay_gone(), -1);
    sim_run(300);
    CHECK(!overlay_pos(2, &x, &y));
    CHECK_EQ(overlay_gone(), 2 | 5 << 8);
  }
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// FRAMES: a slot's frames go at its rate, the one it's on too, 0 holds it
static void test_anim_frames(void) {
  no_overlays();
  dot_frames(true);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 10);  // 100, 110, 120, 130 ms
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  show_overlays();
  frames_msg(0, 200);  // twice as fast, 50 ms left of the first frame
  sim_run(80);
  CHECK(overlay_shows(0, 1));
  sim_run(50);  // 130, the next one 55 ms after 50
  CHECK(overlay_shows(0, 2));
  sim_run(60);
  CHECK(overlay_shows(0, 3));
  frames_msg(0, 0);
  sim_run(1000);
  CHECK(overlay_shows(0, 3));
  frames_msg(0, 100);
  frames_msg(OVERLAY_SLOTS, 0);  // no such slot, too fast, the wrong length
  frames_msg(0, 1001);
  overlay_msg(K_OVERLAY_FRAMES, (uint8_t[]) {0, 0}, 2);
  sim_run(100);
  CHECK(overlay_shows(0, 0));  // on at once, for 140 ms
  sim_run(100);
  CHECK(overlay_shows(0, 1));
  // to 1 % and back after a moment: what's left of the frame is back to its time at 100 %, not 11 s
  frames_msg(0, 1);
  sim_run(50);
  frames_msg(0, 100);
  sim_run(100);
  CHECK(overlay_shows(0, 2));
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// ---------------------------------------------------------------- the picture's parts in front of places

static void part_msg(int k, int x0, int y0, int x1, int y1, unsigned colours, int depth) {
  uint8_t m[PIC_PART_LEN] = {(uint8_t) k};
  plink_put16(m + PIC_PART_X0, (uint16_t) x0);
  plink_put16(m + PIC_PART_Y0, (uint16_t) y0);
  plink_put16(m + PIC_PART_X1, (uint16_t) x1);
  plink_put16(m + PIC_PART_Y1, (uint16_t) y1);
  plink_put16(m + PIC_PART_COLOURS, (uint16_t) colours);
  m[PIC_PART_DEPTH] = (uint8_t) depth;
  overlay_part(m, PIC_PART_LEN);
}

static void depth_msg(int p, int depth) {
  uint8_t m[OVERLAY_DEPTH_LEN] = {(uint8_t) p, (uint8_t) depth};
  overlay_msg(K_OVERLAY_DEPTH, m, OVERLAY_DEPTH_LEN);
}

static int look_depth(int p) {
  overlay_look_t l;
  return overlay_look(p, &l) ? l.depth : -1;
}

// The picture's palette index at x, y on the screen, -1 off it
static int pic_index(const lv_obj_t *pic, int x, int y) {
  lv_area_t a = {x - pic->coords.x1, y - pic->coords.y1, x - pic->coords.x1, y - pic->coords.y1};
  lv_color_t c;
  if (!pic_read(&a, &c, 1))
    return -1;
  for (int i = 0; i < 16; i++)
    if (c.full == lv_color_hex(rgb(i)).full)
      return i;
  return -2;
}

static int was[PIC_MAX_W * PIC_MAX_H];

// Place k is drawn as in was, but for what's in x0..x1 - 1, y0..y1 - 1 of colours, which shows what's behind it
static bool behind(int k, const lv_obj_t *pic, int x0, int y0, int x1, int y1, unsigned colours) {
  static int px[PIC_MAX_W * PIC_MAX_H];
  overlay_look_t l;
  if (!drawn(k, &l, px))
    return false;
  int w = lv_area_get_width(&l.box), left = 0;
  for (int i = 0; i < (int) lv_area_get_size(&l.box); i++) {
    int x = l.box.x1 + i % w, y = l.box.y1 + i / w, c = pic_index(pic, x, y);
    bool in = x >= x0 && x < x1 && y >= y0 && y < y1 && (colours == PIC_ALL_COLOURS || (c >= 0 && colours >> c & 1));
    if (px[i] != (in ? -1 : was[i]))
      return false;
    left += in && was[i] != -1;
  }
  return colours == 0 || left > 0;  // and something was left that would have been drawn
}

static bool draw_was(int k) {
  overlay_look_t l;
  return drawn(k, &l, was);
}

static void test_anim_parts(void) {
  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
  fill(0, H, 3);
  CHECK_EQ(status(send_picture(16)), PIC_DONE);
  lv_obj_t *pic = pic_show(lv_scr_act());
  lv_obj_update_layout(pic);
  int px0 = pic->coords.x1, py0 = pic->coords.y1;

  // pic_in() against the picture's own pixels, along every row and in every colour
  for (int y = py0 - 1; y <= py0 + H; y++)
    for (int x1 = px0 - 2; x1 < px0 + W + 2; x1 += 3)
      for (int x2 = x1; x2 < px0 + W + 2; x2 += 2)
        for (unsigned colours = 1; colours < 0x10000; colours = colours * 3 + 1) {
          uint32_t bits[2] = {0, 0}, want[2] = {0, 0};
          pic_in(y, x1, x2, (uint16_t) colours, bits, x1);
          for (int x = x1; x <= x2; x++) {
            int c = pic_index(pic, x, y);
            if (c >= 0 && colours >> c & 1)
              want[(x - x1) / 32] |= 1u << (x - x1) % 32;
          }
          CHECK(bits[0] == want[0] && bits[1] == want[1]);
        }

  // A place over the whole picture, every pixel drawn
  memset(an, 0x12, sizeof an);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  show_overlays();
  place(0, 0, px0 - 3, py0 - 1, 0, 0);
  CHECK_EQ(look_depth(0), OVERLAY_NEAREST);
  CHECK(draw_was(0));

  // Nearer parts cover it once it's farther, and only there
  unsigned colours = 1u << 3 | 1u << 7 | 1u << 9;
  part_msg(0, px0, py0, px0 + W, py0 + H, colours, 10);
  CHECK(behind(0, pic, 0, 0, 0, 0, 0));  // at OVERLAY_NEAREST nothing's in front
  depth_msg(0, 10);
  CHECK(behind(0, pic, 0, 0, 0, 0, 0));  // nor at the part's own depth
  depth_msg(0, 9);
  CHECK_EQ(look_depth(0), 9);
  CHECK(behind(0, pic, px0, py0, px0 + W, py0 + H, colours));
  part_msg(1, px0 + 2, py0 + 1, px0 + 5, py0 + 4, PIC_ALL_COLOURS, 200);
  CHECK(!behind(0, pic, px0, py0, px0 + W, py0 + H, colours));
  part_msg(0, 0, 0, 0, 0, 0, 0);  // empty, gone
  CHECK(behind(0, pic, px0 + 2, py0 + 1, px0 + 5, py0 + 4, PIC_ALL_COLOURS));
  part_msg(PIC_PARTS, 0, 0, PIC_MAX_W, PIC_MAX_H, PIC_ALL_COLOURS, 255);  // no such part
  uint8_t bad[OVERLAY_DEPTH_LEN + 1] = {0, 0, 0};
  overlay_msg(K_OVERLAY_DEPTH, bad, sizeof bad);  // the wrong length
  CHECK_EQ(look_depth(0), 9);
  CHECK(behind(0, pic, px0 + 2, py0 + 1, px0 + 5, py0 + 4, PIC_ALL_COLOURS));

  // Turned, the same
  int deg = 30;
  uint8_t m[16];
  overlay_msg(K_OVERLAY_GLIDE, m, sim_overlay_glide(m, 0, 1u << OVERLAY_TURN, 0, 0, &deg));
  part_msg(0, px0, py0, px0 + W, py0 + H, colours, 10);
  part_msg(1, 0, 0, 0, 0, 0, 0);
  depth_msg(0, OVERLAY_NEAREST);
  CHECK(draw_was(0));
  depth_msg(0, 0);
  CHECK(behind(0, pic, px0, py0, px0 + W, py0 + H, colours));
  overlay_parts_clear();
  CHECK(behind(0, pic, 0, 0, 0, 0, 0));

  // Depth before the size map and the number, and PLACE starts it nearest
  place(1, 0, 40, 40, 0, 0);
  depth_msg(0, 100);
  depth_msg(1, 50);
  overlay_look_t l[2];
  CHECK(overlay_look(0, &l[0]) && overlay_look(1, &l[1]) && l[0].order > l[1].order);
  place(0, 0, 40, 40, 0, 0);
  CHECK_EQ(look_depth(0), OVERLAY_NEAREST);
  CHECK(overlay_look(0, &l[0]) && overlay_look(1, &l[1]) && l[0].order > l[1].order);
  depth_msg(0, 10);
  CHECK(overlay_look(0, &l[0]) && overlay_look(1, &l[1]) && l[0].order < l[1].order);

  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
}

// ---------------------------------------------------------------- stills

#define SW 40  // a still's frame, in pieces of 8 rows with a clear band and clear edges
#define SH 30
static uint8_t sf[SH][SW / 2];

static void still_frame(void) {
  for (int y = 0; y < SH; y++)
    for (int x = 0; x < SW; x++) {
      int c = (x * 3 + y * 5) % 17;
      if (c > 15 || y < 3 || (y >= 16 && y < 24) || (y >= 8 && y < 16 && (x < 5 || x >= 36)))
        c = 15;
      uint8_t *b = &sf[y][x / 2];
      *b = x & 1 ? (uint8_t) ((*b & 0xF0) | c) : (uint8_t) ((*b & 0x0F) | c << 4);
    }
}

static void glide_now(int p, unsigned channels, const int *to) {
  uint8_t m[32];
  overlay_msg(K_OVERLAY_GLIDE, m, sim_overlay_glide(m, p, channels, 0, 0, to));
}

static void flip_now(int p, int flip) { overlay_msg(K_OVERLAY_FLIP, (uint8_t[]) {(uint8_t) p, (uint8_t) flip}, 2); }

typedef struct {
  int x, y, flip;
  int to[3];  // turn, size and opacity
} still_look_t;

// What place 0 shows of slot as k has it, drawn whole into buf. False if it isn't drawn, or drawn in strips of 8 and 3
// lines it isn't the same.
static bool draw_as(int slot, const still_look_t *k, overlay_look_t *l, lv_color_t *buf) {
  static lv_color_t strips[PIC_MAX_W * PIC_MAX_H];
  place(0, slot, k->x, k->y, 0, 0);
  glide_now(0, 1u << OVERLAY_TURN | 1u << OVERLAY_SIZE | 1u << OVERLAY_OPACITY, k->to);
  flip_now(0, k->flip);
  if (!draw_box(0, l, buf, PIC_MAX_H))
    return false;
  size_t n = lv_area_get_size(&l->box) * sizeof *buf;
  return draw_box(0, l, strips, 8) && !memcmp(strips, buf, n) && draw_box(0, l, strips, 3) && !memcmp(strips, buf, n);
}

// A still draws what an animated overlay of the same frame draws, however it's placed and turned, in pieces
static void test_still(void) {
  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
  still_frame();
  static uint8_t a[OVERLAY_RECORD_HEAD + PDEFLATE_BOUND(SW / 2 * SH)], st[2048];
  uint32_t an_n = plink_overlay_record(NULL, &sf[0][0], SW, SH, 10, a);
  uint32_t n = plink_overlay_pieces(&sf[0][0], SW, SH, 1u << 15, 10, st, sizeof st);
  CHECK(n && n < sizeof st);
  CHECK(!plink_overlay_pieces(&sf[0][0], SW, SH, 1u << 15, 10, st, n - 1));  // doesn't fit
  static const still_look_t LOOKS[] = {
      {60, 50, 0, {0, 100, 100}},
      {60, 50, 0, {0, 50, 100}},
      {60, 50, 0, {0, 173, 100}},
      {60, 50, 0, {30, 100, 100}},
      {60, 50, 0, {90, 100, 100}},
      {60, 50, 0, {180, 100, 100}},
      {60, 50, 0, {211, 81, 100}},
      {60, 50, OVERLAY_FLIP_X, {0, 100, 100}},
      {60, 50, OVERLAY_FLIP_Y, {0, 100, 100}},
      {60, 50, OVERLAY_FLIP_X | OVERLAY_FLIP_Y, {45, 120, 100}},
      {60, 50, 0, {0, 100, 50}},
      {60, 50, 0, {30, 100, 50}},
      {-15, PIC_MAX_H - 12, 0, {0, 100, 100}},  // partly off the screen
      {PIC_MAX_W - 20, -7, 0, {60, 100, 100}},
  };
  show_overlays();
  static lv_color_t want[PIC_MAX_W * PIC_MAX_H], got[PIC_MAX_W * PIC_MAX_H];
  for (int scale = 1; scale <= 2; scale++) {
    CHECK_EQ(status(send_kind(OVERLAY_ANIMATED, 0, SW, SH, scale, clear15(), an_n, a, 200)), PIC_DONE);
    CHECK_EQ(status(send_kind(OVERLAY_STILL, 1, SW, SH, scale, clear15(), n, st, 200)), PIC_DONE);
    CHECK_EQ(overlay_used(), SW / 2 * SH + an_n + n);  // a still has no frame
    for (int i = 0; i < (int) LEN(LOOKS); i++) {
      overlay_look_t la, ls;
      bool ok = draw_as(0, &LOOKS[i], &la, want) && draw_as(1, &LOOKS[i], &ls, got) && ls.still && !la.still &&
                !memcmp(&la.box, &ls.box, sizeof la.box) && !memcmp(want, got, lv_area_get_size(&la.box) * sizeof *got);
      uint32_t ink = 0;
      for (uint32_t k = 0; k < lv_area_get_size(&la.box); k++)
        ink += want[k].full != lv_color_hex(BEHIND).full;
      if (!CHECK(ok && ink > 100))
        printf("    look %d at scale %d\n", i, scale);
    }
  }

  // behind a part of the picture, the same
  part_msg(0, 70, 55, 90, 62, PIC_ALL_COLOURS, 10);
  overlay_look_t la, ls;
  CHECK(draw_as(0, &LOOKS[3], &la, want));
  depth_msg(0, 9);
  CHECK(draw_box(0, &la, want, PIC_MAX_H));
  CHECK(draw_as(1, &LOOKS[3], &ls, got));
  depth_msg(0, 9);
  CHECK(draw_box(0, &ls, got, PIC_MAX_H) && !memcmp(want, got, lv_area_get_size(&la.box) * sizeof *got));
  CHECK(got[(58 - ls.box.y1) * lv_area_get_width(&ls.box) + 80 - ls.box.x1].full == lv_color_hex(BEHIND).full);
  overlay_parts_clear();

  // its pixels: what's in its pieces, 16 where there are none
  static uint8_t px[SW * SH];
  place(0, 1, 60, 50, 0, 0);
  CHECK(overlay_pixels(0, px));
  int none = 0, wrong = 0;
  for (int y = 0; y < SH; y++)
    for (int x = 0; x < SW; x++) {
      int c = x & 1 ? sf[y][x / 2] & 15 : sf[y][x / 2] >> 4, p = px[y * SW + x];
      none += p == 16;
      wrong += p != c && !(p == 16 && c == 15);
    }
  CHECK_EQ(wrong, 0);
  CHECK(none >= SW * 11);  // the clear rows at the top and in the middle at least
  place(0, 0, 60, 50, 0, 0);
  CHECK(overlay_pixels(0, px) && px[0] == 15);

  // no room on LVGL's heap to unpack a piece: nothing drawn, and drawn once there is
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  void *hog = lv_mem_alloc(mon.free_biggest_size - OVERLAY_PIECE_MAX / 2);
  CHECK(hog);
  static int drawn_px[PIC_MAX_W * PIC_MAX_H];
  place(0, 1, 60, 50, 0, 0);
  CHECK(drawn(0, &ls, drawn_px));
  int shown = 0;
  for (uint32_t i = 0; i < lv_area_get_size(&ls.box); i++)
    shown += drawn_px[i] != -1;
  CHECK_EQ(shown, 0);
  CHECK(!overlay_pixels(0, px));
  lv_mem_free(hog);
  CHECK(drawn(0, &ls, drawn_px));
  for (uint32_t i = 0; i < lv_area_get_size(&ls.box); i++)
    shown += drawn_px[i] != -1;
  CHECK(shown > 0);
  lv_obj_clean(lv_scr_act());

  // a frame of its own takes no room, its rounds go by at its delay
  overlay_clear_msg(0);
  CHECK_EQ(overlay_used(), n);
  place(2, 1, 0, 0, 1, 7);
  CHECK_EQ(overlay_used(), n);
  show_overlays();
  rounds_msg(2, 3, 0, 4);
  int16_t x, y;
  sim_run(250);
  CHECK(overlay_pos(2, &x, &y));
  sim_run(100);
  CHECK(!overlay_pos(2, &x, &y));
  CHECK_EQ(overlay_gone(), 2 | 4 << 8);
  lv_obj_clean(lv_scr_act());

  // a still bigger than the pool, its pieces small
  static uint8_t wide[PIC_MAX_H][PIC_MAX_W / 2];
  memset(wide, 0xFF, sizeof wide);
  wide[100][80] = 0x12;
  uint32_t wn = plink_overlay_pieces(&wide[0][0], PIC_MAX_W, PIC_MAX_H, 1u << 15, 10, st, sizeof st);
  CHECK(wn && wn < 64);
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 2, PIC_MAX_W, PIC_MAX_H, 1, clear15(), wn, st, 200)), PIC_DONE);
  CHECK_EQ(overlay_used(), n + wn);
  show_overlays();
  place(3, 2, 0, 0, 0, 0);
  CHECK(drawn(3, &ls, drawn_px));
  CHECK(drawn_px[100 * PIC_MAX_W + 160] == 1 && drawn_px[100 * PIC_MAX_W + 161] == 2);
  CHECK(drawn_px[100 * PIC_MAX_W + 162] == -1 && drawn_px[99 * PIC_MAX_W + 160] == -1);
  // and all clear, one empty piece
  memset(wide, 0xFF, sizeof wide);
  wn = plink_overlay_pieces(&wide[0][0], PIC_MAX_W, PIC_MAX_H, 1u << 15, 10, st, sizeof st);
  CHECK_EQ(wn, OVERLAY_RECORD_HEAD);
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 2, PIC_MAX_W, PIC_MAX_H, 1, clear15(), wn, st, 200)), PIC_DONE);
  place(3, 2, 0, 0, 0, 0);
  CHECK(!drawn(3, &ls, drawn_px) || drawn_px[100 * PIC_MAX_W + 160] == -1);
  no_overlays();
}

// Pieces that don't fit their still, or run past their rows or the end
static void test_still_bad(void) {
  no_overlays();
  uint8_t d[64];
  // 9 rows of 320 pixels, each in two runs, more than OVERLAY_PIECE_MAX. 8 fit.
  for (int rows = 9; rows >= 8; rows--) {
    memcpy(d, (uint8_t[]) {10, 0, 0, PIC_MAX_W / 2, (uint8_t) rows}, OVERLAY_RECORD_HEAD);
    for (int r = 0; r < rows; r++)
      memcpy(d + OVERLAY_RECORD_HEAD + 4 * r, (uint8_t[]) {255, 0x11, 157, 0x11}, 4);
    uint32_t n = OVERLAY_RECORD_HEAD + 4u * (unsigned) rows;
    CHECK_EQ(status(send_kind(OVERLAY_STILL, 0, PIC_MAX_W, 16, 1, clear15(), n, d, 20)),
             rows == 8 ? PIC_DONE : PIC_BAD);
    if (rows == 8)  // and cut short
      CHECK_EQ(status(send_kind(OVERLAY_STILL, 0, PIC_MAX_W, 16, 1, clear15(), n - 1, d, 20)), PIC_BAD);
  }
  CHECK_EQ(overlay_used(), 0);
  static const struct {
    uint8_t d[9];
    uint32_t n;
  } BAD[] = {
      {{10, 0, 0, 1, 1, 0x81, 0x11}, 7},   // a run of 3 in a row of 1
      {{10, 0, 0, 1, 1, 0x01, 0x11}, 7},   // 2 literal bytes in a row of 1
      {{10, 0, 0, 2, 1, 0x01, 0x11}, 7},   // and past the end
      {{10, 0, 3, 1, 2, 0, 1, 0, 2}, 9},   // rows past the frame's
      {{10, 4, 0, 1, 1, 0, 1}, 7},         // past its right edge
      {{10, 0, 0, 1, 0}, 5},               // no rows but a width
      {{10, 0, 0, 0, 0, 10, 0, 0, 0}, 9},  // and a piece cut short after an empty one
  };
  for (int i = 0; i < (int) LEN(BAD); i++)
    if (!CHECK_EQ(status(send_kind(OVERLAY_STILL, 0, 8, 4, 1, clear15(), BAD[i].n, BAD[i].d, 3)), PIC_BAD))
      printf("    piece %d\n", i);
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 0, 8, 4, 1, clear15(), 0, d, 3)), PIC_BAD);  // nothing at all
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 0, 8, 4, 1, clear15(), 7, (uint8_t[]) {10, 0, 0, 1, 1, 0, 0x12}, 3)),
           PIC_DONE);
  CHECK_EQ(overlay_used(), 7);
  no_overlays();
}

// Each colour at its own opacity, and the place's on top
static void test_colour_opacity(void) {
  no_overlays();
  for (int y = 0; y < AH; y++)
    for (int x = 0; x < AW / 2; x++)
      an[0][y][x] = (uint8_t) ((x * 2 % 4) << 4 | (x * 2 + 1) % 4);  // colours 0, 1, 2, 3 across
  uint32_t pal[16] = {0x80000000 | rgb(0), 0xFF000000 | rgb(1), rgb(2), 0x11000000 | rgb(3)};
  uint8_t d[64];
  uint32_t n = plink_overlay_record(NULL, &an[0][0][0], AW, AH, 10, d), sn;
  static uint8_t st[64];
  sn = plink_overlay_pieces(&an[0][0][0], AW, AH, 1u << 2, 10, st, sizeof st);
  CHECK_EQ(status(send_kind(OVERLAY_ANIMATED, 0, AW, AH, 1, pal, n, d, 20)), PIC_DONE);
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 1, AW, AH, 1, pal, sn, st, 20)), PIC_DONE);
  show_overlays();
  lv_color_t behind = lv_color_hex(BEHIND), buf[AW * AH];
  for (int slot = 0; slot < 2; slot++)
    for (int half = 0; half < 2; half++) {
      place(0, slot, 10, 10, 0, 0);
      if (half)
        glide_now(0, 1u << OVERLAY_OPACITY, (int[]) {50});
      overlay_look_t l;
      CHECK(draw_box(0, &l, buf, PIC_MAX_H));
      CHECK(l.colour_opa[0] == 136 && l.colour_opa[1] == 255 && l.colour_opa[2] == 0 && l.colour_opa[3] == 17 &&
            l.colour_opa[15] == 0);
      static const lv_opa_t OPA[2][4] = {{136, 255, 0, 17}, {68, 128, 0, 9}};
      int off = 0;
      for (int i = 0; i < AW * AH; i++) {
        int c = i % 4;
        lv_color_t want = lv_color_mix(lv_color_hex(rgb(c)), behind, OPA[half][c]);
        if (OPA[half][c] == 255)
          want = lv_color_hex(rgb(c));
        off += buf[i].full != (OPA[half][c] ? want : behind).full;
      }
      if (!CHECK_EQ(off, 0))
        printf("    slot %d at %s\n", slot, half ? "50 %" : "100 %");
    }
  no_overlays();
}

const check_test_t OVERLAY_TESTS[] = {
    {"overlay", test_anim},
    {"overlay_slots", test_anim_slots},
    {"overlay_rounds", test_anim_rounds},
    {"overlay_frames", test_anim_frames},
    {"overlay_parts", test_anim_parts},
    {"overlay_still", test_still},
    {"overlay_still_bad", test_still_bad},
    {"overlay_colour_opacity", test_colour_opacity},
    {0},
};
