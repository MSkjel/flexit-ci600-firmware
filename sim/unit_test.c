// Unit tests. The firmware's hw/ code runs on board_stub.c.
#include "board.h"
#include "bus.h"
#include "clock.h"
#include "layout.h"
#include "link.h"
#include "mbserver.h"
#include "pdeflate.h"
#include "pjpeg.h"
#include "plink.h"
#include "settings.h"
#include "trend.h"
#include "sim.h"
#include "ui_int.h"
#include "unit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

model_t M;
extern uint32_t stub_ms, stub_rtc;
extern uint8_t stub_flash[];
extern int stub_flash_writes_left, stub_flash_erases, stub_resets;
extern uint32_t stub_flash_bad;
extern bool stub_bus_quiet;
static int fails;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("%s:%d: %s\n", __FILE__, __LINE__, #c); \
      fails++; \
    } \
  } while (0)

// main.c's, which has the firmware's main()
void plat_value_changed(val_t v) { unit_value_changed(v); }
void plat_action(int a) {
  if (a == A_RESTART)
    board_reset();
  else
    unit_action(a);
}

// ---------------------------------------------------------------- picture
#define W 10  // pixels, so 5 bytes per row
#define H 6
static uint8_t px[H][W / 2];  // the expected picture, 2 pixels per byte

static uint32_t tint;  // XORed into every palette colour, see rgb()
static uint32_t rgb(int i) { return ((uint32_t) (i * 0x0F0F0F) ^ 0x204060) ^ tint; }

static unsigned pack(int y, int count, uint8_t *out) {
  unsigned n = 0;
  for (int r = y; r < y + count; r++)
    n += plink_pack_row(px[r], W / 2, out + n);
  return n;
}

static uint8_t status(int32_t ack) { return ack < 0 ? 0xFF : (uint8_t) (ack >> 24); }

static uint8_t id;                                                          // id of the last transfer
static int32_t (*to_pic)(uint16_t key, const uint8_t *b, int n) = pic_msg;  // or through the link, see test_link

static const uint32_t *palette(void) {  // rgb()'s 16 as they are now
  static uint32_t pal[16];
  for (int i = 0; i < 16; i++)
    pal[i] = rgb(i);
  return pal;
}

static int32_t begin(int w, int h, uint32_t len) {
  uint8_t b[PIC_BEGIN_HEAD + PIC_PAL_LEN];
  return to_pic(K_PIC_BEGIN, b, sim_pic_begin(b, ++id, PIC_PALETTE, w, h, len, palette()));
}

// PATCH, with the palette if pal
static int32_t patch(int y, int count, uint32_t len, bool pal) {
  uint8_t b[PIC_PATCH_HEAD + PIC_PAL_LEN];
  return to_pic(K_PIC_PATCH, b, sim_pic_patch(b, ++id, y, count, len, pal ? palette() : NULL));
}

static int32_t end(uint32_t crc) {
  uint8_t b[4];
  plink_put32(b, crc);
  return to_pic(K_PIC_END, b, 4);
}

static int32_t data(const uint8_t *b, unsigned n) { return to_pic(K_PIC_DATA, b, (int) n); }

// The rest of a transfer after the header got answer a. DATA in pieces, then END. Returns the last answer.
static int32_t rest(int32_t a, const uint8_t *b, unsigned len, unsigned piece, uint32_t crc) {
  if (a >= 0)
    return a;  // refused at once
  for (unsigned i = 0; i < len; i += piece) {
    a = data(b + i, len - i < piece ? len - i : piece);
    if (a >= 0 && status(a) != PIC_PROGRESS)
      return a;  // refused partway
  }
  return end(crc);
}

// b[n] as the ESP sends it, pzip pieces against the hist bytes the panel has before where b goes
static unsigned zip(const uint8_t *b, unsigned n, const uint8_t *hist, unsigned nh, uint8_t *out) {
  static uint16_t table[PZIP_TABLE];
  pzip_t z = {.hist = hist, .nh = nh, .data = b, .n = n, .table = table};
  unsigned k = 0, m;
  while ((m = pzip(&z, out + k)))
    k += m;
  return k;
}

// A patch or picture header got answer a, then b[n] goes as pieces against hist
static int32_t rest_zip(int32_t a, const uint8_t *b, unsigned n, const uint8_t *hist, unsigned nh, unsigned piece) {
  uint8_t z[1024];
  return rest(a, z, zip(b, n, hist, nh, z), piece, plink_crc32(0, b, n));
}

static int32_t send_picture(unsigned piece) {
  uint8_t b[512];
  unsigned n = pack(0, H, b);
  return rest_zip(begin(W, H, n), b, n, NULL, 0, piece);
}

static int32_t send_patch(int y, int count, bool pal) {
  uint8_t b[512];
  unsigned n = pack(y, count, b);
  return rest_zip(patch(y, count, n, pal), b, n, NULL, 0, 7);
}

// Whether the panel draws px
static bool shows(void) {
  lv_color_t all[W * H];
  lv_area_t a = {0, 0, W - 1, H - 1};
  if (!pic_read(&a, all, W))
    return false;
  bool same = true;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint8_t b = px[y][x / 2];
      same &= all[y * W + x].full == lv_color_hex(rgb(x & 1 ? b & 15 : b >> 4)).full;
    }
  return same;
}

static void fill(int y, int count, uint8_t seed) {
  for (int r = y; r < y + count; r++)
    for (int i = 0; i < W / 2; i++)
      px[r][i] = (uint8_t) (i < 3 ? seed + r : (seed + r) * 17 + i);  // a run, then literals
}

static void test_picture(void) {
  fill(0, H, 1);
  int32_t a = send_picture(3);
  CHECK(status(a) == PIC_DONE && (a >> 16 & 0xFF) == id);  // the answer says which transfer
  CHECK(pic_state() == PIC_READY && shows());

  fill(2, 3, 9);  // rows 2-4 replaced
  CHECK(status(send_patch(2, 3, false)) == PIC_DONE);
  CHECK(shows());

  // a patch cut off by a new picture. The new one goes to the start of the pool, not behind the old.
  uint8_t b[512], z[1024];
  unsigned n = pack(0, 2, b);
  patch(0, 2, n, false);
  data(z, zip(b, n, NULL, 0, z) / 2);
  fill(0, H, 40);
  CHECK(status(send_picture(64)) == PIC_DONE);
  CHECK(shows());

  // a bad patch with a run across a row end is refused and leaves the picture alone
  uint8_t bad[] = {126 + 7, 0x11, 0, 0x22, 0, 0x33, 0, 0x44};  // a 7-byte repeat in a 5-byte row
  CHECK(status(rest_zip(patch(1, 1, sizeof bad, false), bad, sizeof bad, NULL, 0, 8)) == PIC_BAD);
  CHECK(shows());
  // and so are bytes after the last row
  n = pack(1, 1, b);
  b[n] = 0;
  b[n + 1] = 0x55;
  CHECK(status(rest_zip(patch(1, 1, n + 2, false), b, n + 2, NULL, 0, 8)) == PIC_BAD && shows());
  CHECK(status(rest_zip(patch(1, 0, 2, false), b, 2, NULL, 0, 8)) == PIC_BAD && shows());
  // and the next patch is still spliced in at the right length
  fill(5, 1, 70);
  CHECK(status(send_patch(5, 1, true)) == PIC_DONE);
  CHECK(shows());

  // a new palette alone, as a patch with no rows
  tint = 0x300000;
  CHECK(status(send_patch(0, 0, true)) == PIC_DONE);
  CHECK(shows());
  tint = 0;
  CHECK(status(send_patch(H, 0, true)) == PIC_DONE && shows());

  // A wrong CRC changes nothing. A patch has to fit in the pool behind the picture, and its rows in the picture.
  n = pack(1, 1, b);
  CHECK(status(rest(patch(1, 1, n, false), z, zip(b, n, NULL, 0, z), 4, 12345)) == PIC_BAD_CRC);
  CHECK(shows());
  CHECK(status(patch(1, 1, PIC_POOL, false)) == PIC_NO_ROOM);
  CHECK(status(patch(1, 1, UINT32_MAX, false)) == PIC_NO_ROOM);  // doesn't wrap round to fit
  CHECK(status(patch(4, 3, n, false)) == PIC_BAD_SIZE);
  CHECK(shows());

  // The ESP restarting drops a patch in progress but keeps the picture. A picture in progress is dropped.
  patch(0, 1, n, false);
  pic_reset();
  CHECK(end(0) == -1 && shows());
  begin(W, H, 100);
  CHECK(pic_state() == PIC_RECEIVING);
  pic_reset();
  CHECK(pic_state() == PIC_NONE && !shows());
  CHECK(status(patch(0, 1, n, false)) == PIC_NO_PICTURE);

  // DATA and END outside a transfer are ignored. A bad header is refused.
  CHECK(data(b, 4) == -1 && end(0) == -1);
  CHECK(status(pic_msg(K_PIC_BEGIN, b, 9)) == PIC_BAD);
  CHECK(status(begin(PIC_MAX_W + 2, H, 4)) == PIC_BAD_SIZE);
  CHECK(status(begin(W, H, PIC_POOL + 1)) == PIC_TOO_BIG);

  // a big one, with progress every KiB
  fill(0, H, 3);
  CHECK(status(send_picture(1)) == PIC_DONE && shows());

  // A patch deflated against the picture the panel has: rows copied from others come as matches into it, so it
  // takes fewer bytes than the rows themselves
  uint8_t old[512];
  unsigned no = pack(0, H, old);
  memcpy(px[2], px[5], sizeof px[2]);
  memcpy(px[3], px[0], sizeof px[3]);
  n = pack(2, 2, b);
  unsigned k = zip(b, n, old, no, z);
  CHECK(k < n && status(rest(patch(2, 2, n, false), z, k, 5, plink_crc32(0, b, n))) == PIC_DONE && shows());
  // Pieces that are too long, make too much, or reach back past the pool are refused
  n = pack(0, H, b);
  k = zip(b, n, NULL, 0, z);
  z[0] = PZIP_MAX - PZIP_HEAD + 1;
  CHECK(status(rest(begin(W, H, n), z, k, 64, plink_crc32(0, b, n))) == PIC_BAD);
  k = zip(b, n, NULL, 0, z);
  CHECK(status(rest(begin(W, H, n - 1), z, k, 64, plink_crc32(0, b, n))) == PIC_BAD);
  k = zip(b, n, old, no, z);  // as if the panel had a picture before it, which a whole one can't reach
  CHECK(status(rest(begin(W, H, n), z, k, 64, plink_crc32(0, b, n))) == PIC_BAD);
  pic_msg(K_PIC_CLEAR, NULL, 0);
  CHECK(!shows());
}

// A picture taller than the row checkpoints, every 8th row, with an odd width: any part drawn is the same as that
// part of the whole, also after patches across the checkpoints
#define BW 37
#define BH 27
static uint8_t big[BH][(BW + 1) / 2];

static void fill_big(int y, int count, int seed) {
  for (int r = y; r < y + count; r++)
    for (int i = 0; i < (BW + 1) / 2; i++)
      big[r][i] = (uint8_t) ((i + r) % 7 < 3 ? seed * 16 + r : (seed + r) * 17 + i * 5);  // runs and literals
}

static unsigned pack_big(int y, int count, uint8_t *out) {
  unsigned n = 0;
  for (int r = y; r < y + count; r++)
    n += plink_pack_row(big[r], (BW + 1) / 2, out + n);
  return n;
}

static bool big_part(int x1, int y1, int x2, int y2) {
  static lv_color_t got[BH][BW];
  memset(got, 0, sizeof got);
  lv_area_t a = {(lv_coord_t) x1, (lv_coord_t) y1, (lv_coord_t) x2, (lv_coord_t) y2};
  if (!pic_read(&a, &got[y1][x1], BW))
    return false;
  bool same = true;
  for (int y = 0; y < BH; y++)
    for (int x = 0; x < BW; x++) {
      uint8_t b = big[y][x / 2];
      bool in = x >= x1 && x <= x2 && y >= y1 && y <= y2;
      same &= got[y][x].full == (in ? lv_color_hex(rgb(x & 1 ? b & 15 : b >> 4)).full : 0);  // nothing outside
    }
  return same;
}

static bool big_shows(void) {
  static const int PARTS[][4] = {{0, 0, BW - 1, BH - 1}, {0, 0, 0, 0},   {BW - 1, BH - 1, BW - 1, BH - 1},
                                 {3, 5, 20, 13},         {4, 8, 4, 16},  {1, 7, 34, 9},
                                 {10, 15, 36, 26},       {9, 17, 11, 23}};
  bool ok = true;
  for (size_t i = 0; i < sizeof PARTS / sizeof PARTS[0]; i++)
    ok &= big_part(PARTS[i][0], PARTS[i][1], PARTS[i][2], PARTS[i][3]);
  return ok;
}

static void test_picture_parts(void) {
  uint8_t b[1024];
  fill_big(0, BH, 1);
  unsigned n = pack_big(0, BH, b);
  CHECK(status(rest_zip(begin(BW, BH, n), b, n, NULL, 0, 50)) == PIC_DONE && big_shows());
  int patches[][2] = {{5, 13}, {8, 8}, {16, 1}, {0, 27}, {20, 7}, {3, 2}};  // y, count
  for (size_t i = 0; i < sizeof patches / sizeof patches[0]; i++) {
    int y = patches[i][0], count = patches[i][1];
    fill_big(y, count, (int) i + 2);
    n = pack_big(y, count, b);
    CHECK(status(rest_zip(patch(y, count, n, false), b, n, NULL, 0, 50)) == PIC_DONE && big_shows());
  }
  // A patch redraws only its rows, the picture object keeps showing
  lv_obj_t *o = pic_show(lv_scr_act());
  lv_refr_now(NULL);
  lv_disp_t *d = lv_disp_get_default();
  fill_big(9, 3, 9);
  n = pack_big(9, 3, b);
  CHECK(status(rest_zip(patch(9, 3, n, false), b, n, NULL, 0, 50)) == PIC_DONE && big_shows());
  lv_area_t *a = &d->inv_areas[0];  // LVGL adds 5 px all round
  CHECK(d->inv_p == 1 && a->y1 == o->coords.y1 + 9 - 5 && a->y2 == o->coords.y1 + 11 + 5);
  lv_refr_now(NULL);
  CHECK(status(rest_zip(patch(0, 0, 0, true), b, 0, NULL, 0, 50)) == PIC_DONE && big_shows());  // all its colours
  CHECK(d->inv_p == 1 && a->y1 == o->coords.y1 - 5 && a->y2 == o->coords.y2 + 5);
  lv_obj_del(o);
  pic_msg(K_PIC_CLEAR, NULL, 0);
}

// ---------------------------------------------------------------- JPEG pictures
#define JW 48
#define JH 24

static uint32_t photo(void *ctx, int x, int y) {  // soft gradients with grain
  (void) ctx;
  return (uint32_t) (40 + x * 4 + (x * y) % 7) << 16 | (uint32_t) (60 + y * 7) << 8 | (uint32_t) (200 - x - y);
}

static int32_t jpeg_begin(int w, int h, uint32_t len) {
  uint8_t b[PIC_BEGIN_HEAD];
  return pic_msg(K_PIC_BEGIN, b, sim_pic_begin(b, ++id, PIC_JPEG, w, h, len, NULL));
}

static int32_t send_jpeg(const uint8_t *j, uint32_t n, int w, int h) {
  return rest(jpeg_begin(w, h, n), j, n, 61, plink_crc32(0, j, n));
}

static void test_jpeg(void) {
  static uint8_t j[8192];
  pjpeg_t e;
  pjpeg_begin(&e, JW, JH, 97, j, 100);  // doesn't fit, but its size is counted to the end
  while (pjpeg_mcu(&e, photo, NULL)) {
  }
  uint32_t n97 = pjpeg_end(&e);
  pjpeg_begin(&e, JW, JH, 90, j, sizeof j);
  pjpeg_also(&e, 97);  // and counted along
  while (pjpeg_mcu(&e, photo, NULL)) {
  }
  uint32_t n = pjpeg_end(&e);
  CHECK(n > 600 && n <= sizeof j && pjpeg_mse(&e) > 0 && pjpeg_mse(&e) < 20);
  CHECK(n97 > n && e.n == 2 && e.o[1].quality == 97 && e.o[1].len == n97);
  CHECK(status(send_jpeg(j, n, JW, JH)) == PIC_DONE);
  CHECK(pic_state() == PIC_READY && pic_format() == PIC_JPEG && pic_width() == JW && pic_height() == JH);

  // drawn whole, close to the photo
  static lv_color_t whole[JW * JH], part[JW * JH];
  lv_area_t all = {0, 0, JW - 1, JH - 1};
  CHECK(pic_read(&all, whole, JW));
  long err = 0;
  for (int y = 0; y < JH; y++)
    for (int x = 0; x < JW; x++) {
      lv_color_t want = lv_color_hex(photo(NULL, x, y));
      int dr = want.ch.red - whole[y * JW + x].ch.red, dg = want.ch.green - whole[y * JW + x].ch.green,
          db = want.ch.blue - whole[y * JW + x].ch.blue;
      err += dr * dr + dg * dg + db * db;
    }
  CHECK(err < JW * JH * 3);  // under about one step per channel at 5-6 bits
  // drawn in strips off its 8-line rows and over part of the width, same pixels
  memset(part, 0, sizeof part);
  for (int y = 0; y < JH; y += 5) {
    lv_area_t s = {3, (lv_coord_t) y, JW - 5, (lv_coord_t) LV_MIN(y + 4, JH - 1)};
    CHECK(pic_read(&s, part + y * JW + 3, JW));
  }
  bool same = true;
  for (int y = 0; y < JH; y++)
    for (int x = 3; x <= JW - 5; x++)
      same &= part[y * JW + x].full == whole[y * JW + x].full;
  CHECK(same);
  // Only the MCUs a part meets are decoded, the ones before it read past and the ones after left. Same pixels, and
  // nothing outside it.
  static const lv_area_t PARTS[] = {{0, 0, 5, 7}, {17, 3, 30, 20}, {40, 9, 47, 23}, {16, 8, 31, 15}, {31, 0, 32, 23}};
  for (size_t i = 0; i < sizeof PARTS / sizeof PARTS[0]; i++) {
    const lv_area_t *s = &PARTS[i];
    memset(part, 0, sizeof part);
    same = pic_read(s, part + s->y1 * JW + s->x1, JW);
    for (int y = 0; y < JH; y++)
      for (int x = 0; x < JW; x++)
        same &= part[y * JW + x].full ==
                (x >= s->x1 && x <= s->x2 && y >= s->y1 && y <= s->y2 ? whole[y * JW + x].full : 0);
    CHECK(same);
  }
  // Shown with its top on a multiple of 8 lines. LVGL's areas over it are rounded out to its MCU rows, the rest
  // stay as they are, and so do strips when LVGL asks how high they may be.
  lv_obj_t *o = pic_show(lv_scr_act());
  lv_obj_update_layout(o);
  CHECK(o->coords.y1 % 8 == 0 && o->coords.y1 > 0 && o->coords.x1 == (320 - JW) / 2);
  lv_area_t over = {(lv_coord_t) (o->coords.x1 + 3), (lv_coord_t) (o->coords.y1 + 3), (lv_coord_t) (o->coords.x1 + 9),
                    (lv_coord_t) (o->coords.y1 + 12)};
  lv_area_t beside = {0, (lv_coord_t) (o->coords.y1 + 3), 9, (lv_coord_t) (o->coords.y1 + 12)}, b0 = beside;
  lv_area_t probe = {0, 0, 0, 36}, low = {0, 0, 0, 4};
  pic_round(NULL, &over);
  pic_round(NULL, &beside);
  pic_round(NULL, &probe);
  pic_round(NULL, &low);
  CHECK(over.y1 == o->coords.y1 && over.y2 == o->coords.y1 + 15 && over.x1 == o->coords.x1 + 3);
  CHECK(!memcmp(&beside, &b0, sizeof b0) && probe.y1 == 0 && probe.y2 == 39 && low.y2 == 4);
  lv_refr_now(NULL);
  _lv_inv_area(NULL, &(lv_area_t) {0, 236, 3, 239});  // a strip of a few lines beside it still gets drawn
  lv_refr_now(NULL);
  CHECK(lv_disp_get_default()->inv_p == 0);
  lv_obj_del(o);
  lv_area_t still = {1, 3, 2, 5};  // nothing to round to without it
  pic_round(NULL, &still);
  CHECK(still.y1 == 3 && still.y2 == 5);
  lv_area_t outside = {0, 0, JW, 0};
  CHECK(!pic_read(&outside, part, JW));
  CHECK(status(patch(0, 1, 4, false)) == PIC_NO_PICTURE);  // only a 16-colour picture takes patches

  // LVGL's heap too full for the decoder: taken, drawn once there's room
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  void *hog = lv_mem_alloc(mon.free_biggest_size - 1024);
  CHECK(hog && status(send_jpeg(j, n, JW, JH)) == PIC_DONE && !pic_read(&all, part, JW));
  lv_mem_free(hog);
  CHECK(pic_read(&all, part, JW) && !memcmp(part, whole, sizeof whole));

  // refused: a size off the MCU grid, a size that isn't the JPEG's, a cut-off stream, a missing restart marker
  CHECK(status(jpeg_begin(JW + 8, JH, n)) == PIC_BAD_SIZE);
  CHECK(status(jpeg_begin(JW, JH + 4, n)) == PIC_BAD_SIZE);
  CHECK(status(send_jpeg(j, n, JW, JH + 8)) == PIC_BAD);
  CHECK(status(send_jpeg(j, n - 40, JW, JH)) == PIC_BAD);
  for (uint32_t i = n - 2; i > 0; i--)
    if (j[i] == 0xFF && j[i + 1] == 0xD1) {
      j[i + 1] = 0xD2;  // restart markers out of order
      break;
    }
  CHECK(status(send_jpeg(j, n, JW, JH)) == PIC_BAD);
  CHECK(pic_state() == PIC_NONE);
}

// ---------------------------------------------------------------- the animations
#define AW 8
#define AH 4
static uint8_t an[4][AH][AW / 2];  // the frames, colour 15 is transparent

// ANIM_BEGIN at scale 2, colour 15 transparent, then the data
static int32_t anim_send(int slot, int w, uint32_t len, const uint8_t *data, uint32_t crc) {
  uint8_t h[ANIM_BEGIN_HEAD + PIC_PAL_LEN];
  int n = sim_anim_begin(h, ++id, slot, w, AH, 2, 15, len, palette());
  return rest(pic_msg(K_ANIM_BEGIN, h, n), data, len, 5, crc);
}

static void place_in(int p, int slot, int x, int y, int own, int start, int fade) {
  uint8_t m[10];
  anim_msg(K_ANIM_PLACE, m, sim_anim_place(m, p, slot, x, y, own, start, fade));
}

static void place(int p, int slot, int x, int y, int own, int start) { place_in(p, slot, x, y, own, start, 0); }

static void anim_clear_msg(unsigned v) {
  uint8_t m[4];
  plink_put32(m, v);
  pic_msg(K_ANIM_CLEAR, m, 4);
}

static lv_obj_t *layer;  // anim_show()'s, at the top left of the screen

static lv_obj_t *show_anims(void) { return layer = anim_show(lv_scr_act()); }

static bool anim_pos(int k, int16_t *x, int16_t *y) {
  anim_look_t l;
  if (!anim_look(k, &l))
    return false;
  *x = l.x;
  *y = l.y;
  return true;
}

static bool anim_angle(int k, int *deg, int *flip) {
  anim_look_t l;
  if (!anim_look(k, &l))
    return false;
  *deg = l.deg;
  *flip = l.flip;
  return true;
}

static bool placed(void) {
  anim_look_t l;
  for (int k = 0; k < ANIM_PLACES; k++)
    if (anim_look(k, &l))
      return true;
  return false;
}

#define BEHIND 0xFF00FF  // what the animations are drawn over, in no test palette

// Place k's box as the layer draws it: each pixel's palette index, -1 where it's see-through, -2 for something
// between. False if it isn't drawn.
static bool drawn(int k, anim_look_t *l, int *px) {
  static lv_color_t buf[PIC_MAX_W * PIC_MAX_H];
  if (!layer || !lv_obj_is_valid(layer) || !anim_look(k, l) || !(l->tiles & 1))
    return false;
  sim_draw(layer, &l->box, lv_color_hex(BEHIND), buf);
  for (uint32_t i = 0; i < lv_area_get_size(&l->box); i++) {
    px[i] = buf[i].full == lv_color_hex(BEHIND).full ? -1 : -2;
    for (int c = 0; c < 16; c++)
      if (buf[i].full == lv_color_hex(rgb(c)).full)
        px[i] = c;
  }
  return true;
}

// Whether place k shows frame f of the AW x AH frames, drawn at 2x
static bool anim_shows(int k, int f) {
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
static uint32_t anim_records(uint8_t *d, int delay) {
  uint32_t n = 0;
  for (int f = 0; f <= 4; f++)
    n += plink_anim_record(f ? &an[f - 1][0][0] : NULL, &an[f % 4][0][0], AW, AH, (uint8_t) (delay + f), d + n);
  return n;
}

static void test_anim(void) {
  memset(an, 0xFF, sizeof an);
  an[0][1][1] = 0x12;  // a dot that moves, then stays
  an[1][2][2] = 0x34;
  an[2][2][2] = 0x34;
  an[3][0][0] = 0x56;
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE && !placed());  // not placed yet
  CHECK(anim_used() == AW / 2 * AH + n && show_anims() && !anim_shows(0, 0));
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
  lv_obj_del(layer);

  // bad sizes, then bad data
  CHECK(status(anim_send(0, AW - 1, n, d, 0)) == PIC_BAD_SIZE && !placed());  // the slot's gone with it
  CHECK(status(anim_send(0, PIC_MAX_W, n, d, 0)) == PIC_BAD_SIZE);
  CHECK(status(anim_send(0, AW, ANIM_POOL, d, 0)) == PIC_TOO_BIG);
  CHECK(status(anim_send(0, AW, UINT32_MAX, d, 0)) == PIC_TOO_BIG);  // doesn't wrap round to fit
  uint8_t big[ANIM_BEGIN_HEAD + PIC_PAL_LEN];                        // a frame bigger than the pool
  int bn = sim_anim_begin(big, ++id, 1, 160, 100, 1, ANIM_OPAQUE, 4, palette());
  CHECK(status(pic_msg(K_ANIM_BEGIN, big, bn)) == PIC_TOO_BIG);
  big[1] = ANIM_SLOTS;  // no such slot
  CHECK(status(pic_msg(K_ANIM_BEGIN, big, bn)) == PIC_BAD);
  uint8_t bad[sizeof d];
  memcpy(bad, d, n);
  bad[4] = AH + 1;
  CHECK(status(anim_send(0, AW, n, bad, plink_crc32(0, bad, n))) == PIC_BAD);
  CHECK(status(anim_send(0, AW, n, d, 12345)) == PIC_BAD_CRC);
  uint8_t five[5] = {0x11, 0x11, 0x11, 0x11, 0x11}, over[5 + PDEFLATE_BOUND(5)] = {5, 0, 0, 4, 1};
  uint32_t on = 5 + pdeflate(five, 5, 5, 5, over + 5, PDEFLATE_BOUND(5));  // 5 bytes for a rectangle of 4
  CHECK(status(anim_send(0, AW, on, over, plink_crc32(0, over, on))) == PIC_BAD);
  CHECK(status(anim_send(0, AW, n - 1, d, plink_crc32(0, d, n - 1))) == PIC_BAD);  // the last record cut short
  CHECK(anim_used() == 0);
  // A picture coming in cancels an animation coming in. A finished animation stays until the ESP clears it.
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  uint8_t h[ANIM_BEGIN_HEAD + PIC_PAL_LEN];
  int hn = sim_anim_begin(h, ++id, 1, AW, AH, 1, 15, n, palette());
  CHECK(pic_msg(K_ANIM_BEGIN, h, hn) == -1 && data(d, n / 2) == -1);
  fill(0, H, 5);
  CHECK(status(send_picture(16)) == PIC_DONE && shows() && placed() && anim_used() == AW / 2 * AH + n);
  CHECK(data(d + n / 2, n - n / 2) == -1 && end(plink_crc32(0, d, n)) == -1);  // the rest is no longer expected
  CHECK(status(anim_send(1, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE && placed());
  anim_clear_msg(ANIM_ALL);
  CHECK(!placed() && shows() && anim_used() == 0);
  // Clearing another slot or a place leaves it coming in, clearing its own slot drops the rest of it
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  h[0] = ++id;
  CHECK(pic_msg(K_ANIM_BEGIN, h, hn) == -1 && data(d, n / 2) == -1);
  anim_clear_msg(ANIM_PLACE_BIT | 0);
  anim_clear_msg(0 | 300 << 8);
  CHECK(data(d + n / 2, n - n / 2) == -1 && status(end(plink_crc32(0, d, n))) == PIC_DONE);
  CHECK(anim_used() == AW / 2 * AH + n);
  h[0] = ++id;
  CHECK(pic_msg(K_ANIM_BEGIN, h, hn) == -1 && data(d, n / 2) == -1);
  anim_clear_msg(1 | 300 << 8);
  CHECK(data(d + n / 2, n - n / 2) == -1 && end(plink_crc32(0, d, n)) == -1 && anim_used() == 0);
}

// Two slots and places in step and out of step, sharing the pool
static void test_anim_slots(void) {
  uint8_t d[512], d2[ANIM_POOL];
  uint32_t n = anim_records(d, 5);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  uint32_t n2 = anim_records(d2, 10);  // the same frames in slot 1, every one twice as long
  CHECK(status(anim_send(1, AW, n2, d2, plink_crc32(0, d2, n2))) == PIC_DONE);
  unsigned frame = AW / 2 * AH, both = 2 * frame + n + n2;
  CHECK(anim_used() == both);
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 100, 0, 0, 0);    // in step with place 0
  place(2, 0, 200, 0, 1, 254);  // its own frame, 254 frames ahead: 2 of the 4
  place(3, 1, 0, 100, 0, 0);    // slot 1
  place(4, 1, 100, 100, 1, 0);  // its own frame, in step as it happens
  CHECK(anim_used() == both + 2 * frame);
  place(5, 2, 0, 0, 0, 0);                // no animation there
  place(6, 0, -10, PIC_MAX_H - 1, 0, 0);  // partly off the screen, where something can come in from
  place(ANIM_PLACES, 0, 0, 0, 0, 0);      // no such place
  int16_t x, y;
  CHECK(anim_pos(2, &x, &y) && x == 200 && y == 0 && !anim_pos(5, &x, &y));
  CHECK(anim_pos(6, &x, &y) && x == -10 && y == PIC_MAX_H - 1);
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
  CHECK(anim_used() == both + frame && anim_pos(2, &x, &y) && x == 50);
  anim_clear_msg(ANIM_PLACE_BIT | 4);
  CHECK(anim_used() == both && !anim_pos(4, &x, &y));
  // the pool is packed: clearing slot 0 moves slot 1 down, and it plays on from where it was
  anim_clear_msg(0);
  CHECK(anim_used() == frame + n2 && !anim_pos(0, &x, &y) && !anim_pos(1, &x, &y) && anim_pos(3, &x, &y));
  CHECK(anim_shows(3, 1));
  sim_run(60);  // 210
  CHECK(anim_shows(3, 2));
  // one byte too many for what's left
  CHECK(status(anim_send(0, AW, ANIM_POOL - anim_used() - frame + 1, d, 0)) == PIC_TOO_BIG &&
        anim_used() == frame + n2);
  anim_clear_msg(ANIM_ALL);
  CHECK(anim_used() == 0);
  // slot 1 padded with empty records until only one frame of its own fits behind slot 0 and it
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  uint32_t m = (ANIM_POOL - anim_used() - 2 * frame - n2) / 5;
  for (uint32_t i = 0; i < m; i++)
    memcpy(d2 + n2 + 5 * i, (uint8_t[]) {10, 0, 0, 0, 0}, 5);
  uint32_t n3 = n2 + 5 * m;
  CHECK(status(anim_send(1, AW, n3, d2, plink_crc32(0, d2, n3))) == PIC_DONE);
  CHECK(ANIM_POOL - anim_used() >= frame && ANIM_POOL - anim_used() < 2 * frame);
  place(0, 0, 0, 0, 1, 0);
  place(1, 0, 20, 0, 1, 0);  // no room for its frame
  place(2, 1, 40, 0, 0, 0);  // no frame needed
  CHECK(anim_pos(0, &x, &y) && !anim_pos(1, &x, &y) && anim_pos(2, &x, &y));
  anim_clear_msg(ANIM_PLACE_BIT | 0);
  place(1, 0, 20, 0, 1, 0);
  CHECK(!anim_pos(0, &x, &y) && anim_pos(1, &x, &y));
  anim_clear_msg(ANIM_ALL);
  CHECK(anim_used() == 0 && !placed());
  lv_obj_clean(lv_scr_act());
}

// Whether place 0's box has its top left at x, y
static bool box_at(int x, int y) {
  anim_look_t l;
  return anim_look(0, &l) && l.box.x1 == x && l.box.y1 == y;
}

static bool anim_at(int x, int y) {
  int16_t ax, ay;
  return anim_pos(0, &ax, &ay) && ax == x && ay == y && box_at(x, y);
}

// whether place 0 is drawn again a screen to the left, up, and both, for the part over the right or bottom edge
static bool ghosts_shown(bool right, bool below) {
  anim_look_t l;
  return anim_look(0, &l) && l.tiles == (1 | right << 1 | below << 2 | (right && below) << 3);
}

// GLIDE of place p's channel c to v over ms, eased
static void glide_msg(int p, int c, int v, int ms, int ease) {
  uint8_t m[7];
  anim_msg(K_ANIM_GLIDE, m, sim_anim_glide(m, p, 1u << c, ms, ease, &v));
}

static void drift_ch(int p, int c, int v) {
  uint8_t m[4];
  anim_msg(K_ANIM_DRIFT, m, sim_anim_drift(m, p, 1u << c, &v));
}

// place 0 to x, y over ms, or drifting vx, vy px/s, each one message
static void move_msg(int x, int y, int ms) {
  uint8_t m[9];
  int v[2] = {x, y};
  anim_msg(K_ANIM_GLIDE, m, sim_anim_glide(m, 0, 1u << ANIM_X | 1u << ANIM_Y, ms, 0, v));
}

static void drift_msg(int vx, int vy) {
  uint8_t m[6];
  int v[2] = {vx, vy};
  anim_msg(K_ANIM_DRIFT, m, sim_anim_drift(m, 0, 1u << ANIM_X | 1u << ANIM_Y, v));
}

// GLIDE and DRIFT of x and y: glides land on the target, drifts wrap round the screen, bad messages are ignored
static void test_anim_move(void) {
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  move_msg(100, 50, 0);  // no animation yet
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 10, 10, 0, 0);
  show_anims();
  CHECK(anim_at(10, 10) && ghosts_shown(false, false));  // 16x8 where it was placed
  uint8_t glide[9] = {0, 1u << ANIM_X, 0, 0, 0, 100}, drift[4] = {0, 1u << ANIM_X, 0xD8, 0xFF};
  anim_msg(K_ANIM_GLIDE, glide, 6);  // the wrong length
  anim_msg(K_ANIM_GLIDE, glide, 9);  // one channel, two values
  anim_msg(K_ANIM_DRIFT, drift, 5);
  anim_msg(K_ANIM_GLIDE, NULL, 0);
  glide[1] = 0;  // no channel
  anim_msg(K_ANIM_GLIDE, glide, 5);
  glide[1] = 1u << ANIM_CHANNELS;  // no such channel
  anim_msg(K_ANIM_GLIDE, glide, 7);
  glide[1] = 1u << ANIM_X | 1u << ANIM_SIZE;
  plink_put16(glide + 7, 5);  // x is fine, size 5 isn't, so neither goes
  anim_msg(K_ANIM_GLIDE, glide, sizeof glide);
  glide[1] = 1u << ANIM_X;
  glide[4] = ANIM_EASE_IN << 2;  // no such ease
  anim_msg(K_ANIM_GLIDE, glide, 7);
  drift[1] = 1u << ANIM_SIZE;  // only x, y and turn drift
  anim_msg(K_ANIM_DRIFT, drift, sizeof drift);
  glide[0] = 1;  // no such place
  glide[4] = 0;
  anim_msg(K_ANIM_GLIDE, glide, 7);
  sim_run(200);
  CHECK(anim_at(10, 10));
  move_msg(100, 50, 0);  // ms = 0 is a jump
  CHECK(anim_at(100, 50));
  move_msg(310, 50, 0);  // partly off the right edge: that's where it is, no wrapping until it drifts
  CHECK(anim_at(310, 50) && ghosts_shown(false, false));
  drift_msg(-40, 0);  // 2 px a step
  sim_run(50);
  CHECK(anim_at(308, 50) && ghosts_shown(true, false));
  move_msg(-20, 60, 1000);  // a glide can end partly off the screen
  sim_run(500);
  int16_t x, y;
  anim_pos(0, &x, &y);
  CHECK(x == 144 && y == 55);  // halfway from 308, 50
  sim_run(500);
  CHECK(anim_at(-20, 60));  // landed exactly
  sim_run(500);
  CHECK(anim_at(-20, 60));  // and stays
  drift_msg(-40, 0);        // a drift puts it on the screen again, from the right
  sim_run(50);
  CHECK(anim_at(298, 60) && ghosts_shown(false, false));
  sim_run(50 * 149);
  CHECK(anim_at(0, 60));  // 2 px a step
  sim_run(50);
  CHECK(anim_at(318, 60) && ghosts_shown(true, false));  // over the right edge, the ghost draws the rest
  sim_run(50 * 159);
  CHECK(anim_at(0, 60) && ghosts_shown(false, false));
  drift_msg(0, 100);  // 5 px a step down
  sim_run(50 * 35);
  CHECK(anim_at(0, 235) && ghosts_shown(false, true));
  sim_run(50);
  CHECK(anim_at(0, 0) && ghosts_shown(false, false));
  sim_run(50);
  CHECK(anim_at(0, 5));
  drift_msg(-1, 0);  // the sub-pixel part adds up
  sim_run(50);
  CHECK(anim_at(319, 5) && ghosts_shown(true, false));
  sim_run(50 * 19);
  CHECK(anim_at(319, 5));
  sim_run(50);
  CHECK(anim_at(318, 5));
  drift_msg(0, 0);
  sim_run(1000);
  CHECK(anim_at(318, 5));
  // LVGL too busy for two steps: the next one makes up for them
  move_msg(100, 100, 0);
  drift_msg(40, 0);
  sim_run(50);
  lv_tick_inc(150);
  lv_timer_handler();
  CHECK(anim_at(108, 100));
  drift_msg(0, 0);
  // the axes go their own ways: x drifts while y glides
  move_msg(100, 100, 0);
  drift_ch(0, ANIM_X, 40);
  glide_msg(0, ANIM_Y, 120, 500, 0);
  sim_run(500);
  CHECK(anim_at(120, 120));
  sim_run(500);
  CHECK(anim_at(140, 120));
  drift_msg(0, 0);
  move_msg(318, 5, 0);
  drift_msg(-1, 0);
  drift_msg(0, 0);  // on the screen's torus again, still
  // the animation outlives its image, and the motion with it
  lv_obj_del(layer);
  drift_msg(-1, 0);
  sim_run(2000);  // nothing moves without an image
  show_anims();
  CHECK(anim_at(318, 5) && ghosts_shown(true, false));
  sim_run(1000);
  CHECK(anim_at(317, 5));
  lv_obj_del(layer);
  // placing it again stops it and puts it there
  place(0, 0, 20, 10, 0, 0);
  show_anims();
  sim_run(1000);
  CHECK(anim_at(20, 10));
  drift_msg(-1, 0);
  anim_clear_msg(0);  // clearing the slot takes the place with it
  CHECK(!anim_pos(0, &x, &y));
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 20, 10, 0, 0);
  show_anims();
  sim_run(2000);
  CHECK(anim_at(20, 10));
  glide_msg(0, ANIM_X, -32768, 0, 0);  // a glide across the whole int16 range
  glide_msg(0, ANIM_X, 32767, 65535, 0);
  sim_run(50000);
  int16_t gx, gy;
  anim_pos(0, &gx, &gy);
  CHECK(gx == 17232 && gy == 10);
  sim_run(15550);  // landed
  anim_pos(0, &gx, &gy);
  CHECK(gx == 32767 && gy == 10);
  // a message every 30 ms doesn't hold it up, it drifts on at 2 px a step
  move_msg(100, 100, 0);
  drift_msg(40, 0);
  for (int i = 0; i < 33; i++) {
    glide_msg(0, ANIM_OPACITY, 100, 0, 0);
    sim_run(30);
  }
  CHECK(anim_at(138, 100));
  lv_obj_del(layer);
  anim_clear_msg(ANIM_ALL);
}

// Glides ease in, out or both: from 0 to 200 over a second, where it is at 250 and 500 ms
static void test_anim_ease(void) {
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  show_anims();
  static const int16_t WANT[4][2] = {{50, 100}, {12, 50}, {87, 150}, {31, 100}};  // none, in, out, both
  for (int e = 0; e < 4; e++) {
    glide_msg(0, ANIM_X, 0, 0, 0);
    glide_msg(0, ANIM_X, 200, 1000, e);
    int16_t x[2], y;
    sim_run(250);
    anim_pos(0, &x[0], &y);
    sim_run(250);
    anim_pos(0, &x[1], &y);
    CHECK(x[0] == WANT[e][0] && x[1] == WANT[e][1]);
    sim_run(500);
    CHECK(anim_pos(0, &x[0], &y) && x[0] == 200);
  }
  lv_obj_del(layer);
  anim_clear_msg(ANIM_ALL);
}

// The frame's pixel at ix, iy of the 16 x 8 it's drawn at, scale 2, as drawn() gives it
static int frame_px(int ix, int iy) {
  uint8_t b = an[0][iy / 2][ix / 4];
  int k = ix / 2 & 1 ? b & 15 : b >> 4;
  return k == 15 ? -1 : k;
}

// Place k's pixel at bx, by of its box as drawn() gives it, -3 if it isn't drawn
static int drawn_px(int k, int bx, int by) {
  static int px[PIC_MAX_W * PIC_MAX_H];
  anim_look_t l;
  return drawn(k, &l, px) ? px[by * lv_area_get_width(&l.box) + bx] : -3;
}

// Whether place 0 is drawn w x h, every pixel the frame's at where(bx, by)
static bool drawn_as(int w, int h, void (*where)(int bx, int by, int *ix, int *iy)) {
  static int px[PIC_MAX_W * PIC_MAX_H];
  anim_look_t l;
  if (!drawn(0, &l, px) || lv_area_get_width(&l.box) != w || lv_area_get_height(&l.box) != h)
    return false;
  for (int by = 0; by < h; by++)
    for (int bx = 0; bx < w; bx++) {
      int ix, iy;
      where(bx, by, &ix, &iy);
      if (px[by * w + bx] != frame_px(ix, iy))
        return false;
    }
  return true;
}

static void at_same(int bx, int by, int *ix, int *iy) {
  *ix = bx;
  *iy = by;
}
static void at_90(int bx, int by, int *ix, int *iy) {  // turned a quarter clockwise: the right side faces down
  *ix = by;
  *iy = 7 - bx;
}
static void at_180(int bx, int by, int *ix, int *iy) {
  *ix = 15 - bx;
  *iy = 7 - by;
}
static void at_flip_x(int bx, int by, int *ix, int *iy) {
  *ix = 15 - bx;
  *iy = by;
}
static void at_flip_x_90(int bx, int by, int *ix, int *iy) {  // mirrored, then turned
  *ix = 15 - by;
  *iy = 7 - bx;
}

// The same frame each time, nothing in it symmetric
static void odd_frames(void) {
  memset(an, 0xFF, sizeof an);
  for (int f = 0; f < 4; f++) {
    an[f][0][0] = 0x12;
    an[f][1][1] = 0x3F;
    an[f][3][3] = 0x45;
  }
}

static void flip_msg(int p, int flip) { anim_msg(K_ANIM_FLIP, (uint8_t[]) {(uint8_t) p, (uint8_t) flip}, 2); }

// Turns and FLIP: quarter turns and flips draw exactly, turns go the short way, spins add up, bad messages are
// ignored
static void test_anim_turn(void) {
  odd_frames();
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 10, 10, 0, 0);
  show_anims();
  int a, flip;
  glide_msg(0, ANIM_TURN, 90, 0, 0);  // at once
  CHECK(anim_angle(0, &a, &flip) && a == 90 && !flip && drawn_as(8, 16, at_90));
  CHECK(box_at(14, 6));  // centred where the 16 x 8 frame was
  glide_msg(0, ANIM_TURN, 180, 0, 0);
  CHECK(drawn_as(16, 8, at_180) && box_at(10, 10));
  flip_msg(0, ANIM_FLIP_X);
  glide_msg(0, ANIM_TURN, 0, 0, 0);
  CHECK(anim_angle(0, &a, &flip) && a == 0 && flip == ANIM_FLIP_X && drawn_as(16, 8, at_flip_x));
  glide_msg(0, ANIM_TURN, 90, 0, 0);
  CHECK(drawn_as(8, 16, at_flip_x_90));
  // a turn over time goes the short way round: 90 to 350 is 100 back, not 260 on
  flip_msg(0, 0);
  glide_msg(0, ANIM_TURN, 350, 1000, 0);
  sim_run(500);
  CHECK(anim_angle(0, &a, &flip) && a >= 38 && a <= 42);
  sim_run(600);
  CHECK(anim_angle(0, &a, &flip) && a == 350);
  // a spin adds up past 360, and a stop keeps the angle
  drift_ch(0, ANIM_TURN, 90);
  sim_run(1000);
  CHECK(anim_angle(0, &a, &flip) && a >= 75 && a <= 80);
  drift_ch(0, ANIM_TURN, 0);
  sim_run(1000);
  int b;
  CHECK(anim_angle(0, &b, &flip) && b == a);
  // a bad flip, the wrong length and no such place change nothing
  flip_msg(0, 4);
  flip_msg(5, ANIM_FLIP_Y);
  anim_msg(K_ANIM_FLIP, (uint8_t[]) {0, ANIM_FLIP_Y, 0}, 3);
  anim_msg(K_ANIM_FLIP, NULL, 0);
  CHECK(anim_angle(0, &b, &flip) && b == a && !flip);
  // placed again, it starts unturned
  lv_obj_del(layer);
  place(0, 0, 10, 10, 0, 0);
  show_anims();
  CHECK(anim_angle(0, &a, &flip) && !a && !flip && drawn_as(16, 8, at_same));
  lv_obj_del(layer);
  anim_clear_msg(ANIM_ALL);
}

static void at_200(int bx, int by, int *ix, int *iy) {
  *ix = bx / 2;
  *iy = by / 2;
}
static void at_50(int bx, int by, int *ix, int *iy) {
  *ix = 2 * bx + 1;
  *iy = 2 * by + 1;
}
static void at_90_200(int bx, int by, int *ix, int *iy) {
  *ix = by / 2;
  *iy = 7 - bx / 2;
}

static int look_size(int place) {
  anim_look_t l;
  return anim_look(place, &l) ? l.size : -1;
}
static int look_opa(int place) {
  anim_look_t l;
  return anim_look(place, &l) ? l.opa : -1;
}

// Size and opacity: sizes draw exactly round the middle, sizes and fades get there over time, a place can fade in
// and a clear can fade it out first
static void test_anim_look(void) {
  odd_frames();
  uint8_t d[512], d2[ANIM_POOL];
  uint32_t n = anim_records(d, 5);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 40, 40, 0, 0);
  show_anims();
  glide_msg(0, ANIM_SIZE, 200, 0, 0);
  CHECK(look_size(0) == 2000 && drawn_as(32, 16, at_200) && box_at(32, 36));
  glide_msg(0, ANIM_SIZE, 50, 0, 0);
  CHECK(drawn_as(8, 4, at_50) && box_at(44, 42));
  glide_msg(0, ANIM_TURN, 90, 0, 0);  // a quarter turn at twice the size
  glide_msg(0, ANIM_SIZE, 200, 0, 0);
  CHECK(drawn_as(16, 32, at_90_200) && box_at(40, 28));
  glide_msg(0, ANIM_TURN, 0, 0, 0);
  glide_msg(0, ANIM_SIZE, 100, 1000, 0);  // back over a second, then drawn straight again
  sim_run(500);
  CHECK(look_size(0) == 1500);
  sim_run(600);
  CHECK(look_size(0) == 1000 && drawn_as(16, 8, at_same) && box_at(40, 40));
  glide_msg(0, ANIM_SIZE, 9, 0, 0);  // out of range
  glide_msg(0, ANIM_SIZE, 401, 0, 0);
  glide_msg(0, ANIM_OPACITY, 101, 0, 0);
  glide_msg(0, ANIM_SPEED, 1001, 0, 0);
  CHECK(look_size(0) == 1000 && look_opa(0) == 1000);
  // to see-through over a second, then opaque at once
  glide_msg(0, ANIM_OPACITY, 0, 1000, 0);
  sim_run(500);
  CHECK(look_opa(0) == 500 && drawn_px(0, 0, 0) == -2);  // half the frame's, half what's behind
  sim_run(600);
  int16_t x, y;
  CHECK(look_opa(0) == 0 && anim_pos(0, &x, &y));
  glide_msg(0, ANIM_OPACITY, 100, 0, 0);
  CHECK(look_opa(0) == 1000 && drawn_px(0, 0, 0) == 1);
  // placed fading in over 400 ms
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_PLACE_BIT | 0);
  place_in(1, 0, 100, 40, 0, 0, 400);
  show_anims();
  CHECK(look_opa(1) == 0);
  sim_run(200);
  CHECK(look_opa(1) == 500);
  sim_run(250);
  CHECK(look_opa(1) == 1000);
  // cleared over 500 ms: it stays while it fades, then goes, with nothing to tell the ESP
  anim_clear_msg(ANIM_PLACE_BIT | 1 | 500 << 8);
  sim_run(250);
  CHECK(anim_pos(1, &x, &y) && look_opa(1) == 500);
  sim_run(300);
  CHECK(!anim_pos(1, &x, &y) && anim_gone() == -1);
  // a slot cleared over time goes with its last place, and gives back its room then
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 40, 0, 1, 0);
  show_anims();
  unsigned used = anim_used();
  anim_clear_msg(0 | 300 << 8);
  CHECK(anim_used() == used && anim_pos(0, &x, &y) && anim_pos(1, &x, &y));
  place(2, 0, 80, 0, 0, 0);  // no new places for it
  CHECK(!anim_pos(2, &x, &y));
  sim_run(400);
  CHECK(anim_used() == 0 && !anim_pos(0, &x, &y) && !anim_pos(1, &x, &y));
  // also when its last places go at once or onto another slot
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  CHECK(status(anim_send(1, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 40, 0, 0, 0);
  anim_clear_msg(0 | 300 << 8);
  anim_clear_msg(ANIM_PLACE_BIT | 0);
  place(1, 1, 40, 0, 0, 0);
  CHECK(anim_used() == AW / 2 * AH + n && anim_pos(1, &x, &y));
  anim_clear_msg(ANIM_ALL);
  // but a new animation that needs the room takes it at once
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  lv_obj_clean(lv_scr_act());
  place(0, 0, 0, 0, 0, 0);
  show_anims();
  anim_clear_msg(0 | 5000 << 8);
  unsigned frame = AW / 2 * AH;
  uint32_t n2 = anim_records(d2, 5), m = (ANIM_POOL - frame - n2) / 5;
  for (uint32_t i = 0; i < m; i++)
    memcpy(d2 + n2 + 5 * i, (uint8_t[]) {10, 0, 0, 0, 0}, 5);
  n2 += 5 * m;
  CHECK(status(anim_send(1, AW, n2, d2, plink_crc32(0, d2, n2))) == PIC_DONE && !anim_pos(0, &x, &y) &&
        anim_used() == frame + n2);
  // not shown, it goes at once
  lv_obj_clean(lv_scr_act());
  place(2, 1, 0, 0, 0, 0);
  anim_clear_msg(ANIM_PLACE_BIT | 2 | 500 << 8);
  CHECK(!anim_pos(2, &x, &y));
  anim_clear_msg(ANIM_ALL);
  CHECK(anim_used() == 0);
}

static void frames_msg(int slot, int rate) {
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
  memset(an, 0xFF, sizeof an);
  an[0][1][1] = 0x12;
  an[1][2][2] = 0x34;
  an[2][2][1] = 0x34;
  an[3][0][0] = 0x56;
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);  // 50, 60, 70, 80 ms, and 90 for the first frame after that
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  while (anim_gone() >= 0)
    ;
  place(2, 0, 0, 10, 1, 0);
  show_anims();
  rounds_msg(2, 1, 200, 7);
  sim_run(170);
  CHECK(anim_shows(2, 2) && look_opa(2) == 1000);
  sim_run(20);
  CHECK(anim_shows(2, 3));  // the end of its round
  sim_run(100);
  CHECK(look_opa(2) > 0 && look_opa(2) < 1000 && anim_gone() == -1);
  sim_run(200);
  int16_t x, y;
  CHECK(!anim_pos(2, &x, &y) && anim_gone() == (2 | 7 << 8) && anim_gone() == -1);
  // in step with the slot, the round it's in counts
  lv_obj_clean(lv_scr_act());
  place(0, 0, 0, 40, 0, 0);
  show_anims();
  rounds_msg(0, 2, 0, 9);
  sim_run(250);
  CHECK(anim_pos(0, &x, &y) && anim_gone() == -1);
  sim_run(450);
  CHECK(!anim_pos(0, &x, &y) && anim_gone() == (0 | 9 << 8));
  // placed again before the ESP heard, there's nothing to tell
  place(3, 0, 0, 60, 1, 0);
  show_anims();
  rounds_msg(3, 1, 0, 1);
  sim_run(400);
  CHECK(!anim_pos(3, &x, &y));
  place(3, 0, 0, 60, 1, 0);
  CHECK(anim_gone() == -1);
  // 0 plays on
  lv_obj_clean(lv_scr_act());
  show_anims();
  rounds_msg(3, 1, 0, 2);
  rounds_msg(3, 0, 0, 2);
  anim_msg(K_ANIM_ROUNDS, (uint8_t[]) {3, 1, 0, 0, 0}, 5);  // the wrong length
  sim_run(1000);
  CHECK(anim_pos(3, &x, &y) && anim_gone() == -1);
  // a single frame shows for its frame time each round
  uint8_t one[64];
  uint32_t n1 = plink_anim_record(NULL, &an[0][0][0], AW, AH, 10, one);
  CHECK(status(anim_send(1, AW, n1, one, plink_crc32(0, one, n1))) == PIC_DONE);
  lv_obj_clean(lv_scr_act());
  place(4, 1, 100, 100, 0, 0);
  show_anims();
  rounds_msg(4, 3, 0, 2);
  sim_run(250);
  CHECK(anim_pos(4, &x, &y));
  sim_run(150);
  CHECK(!anim_pos(4, &x, &y) && anim_gone() == (4 | 2 << 8));
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
  CHECK(!anim_pos(4, &x, &y) && anim_gone() == (4 | 3 << 8));
  // cleared while it fades out after its rounds, alone or with its slot: it fades on as it was and still says so
  for (int slot = 0; slot < 2; slot++) {
    place(2, 0, 0, 10, 1, 0);
    rounds_msg(2, 1, 400, 5);
    sim_run(200);
    anim_clear_msg(slot ? 0 | 100 << 8 : ANIM_PLACE_BIT | 2 | 100 << 8);
    sim_run(200);
    CHECK(anim_pos(2, &x, &y) && anim_gone() == -1);
    sim_run(300);
    CHECK(!anim_pos(2, &x, &y) && anim_gone() == (2 | 5 << 8));
  }
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
}

static void area_msg(int x0, int y0, int x1, int y1, int ex, int ey) {
  uint8_t m[11] = {0};
  plink_put16(m + 1, x0);
  plink_put16(m + 3, y0);
  plink_put16(m + 5, x1);
  plink_put16(m + 7, y1);
  m[9] = (uint8_t) ex;
  m[10] = (uint8_t) ey;
  anim_msg(K_ANIM_AREA, m, sizeof m);
}

static void wander_msg(int s0, int s1, int heading, int spread, int l0, int l1, int r0, int r1) {
  uint8_t m[17] = {0};
  int v[8] = {s0 * 10, s1 * 10, heading, spread, l0, l1, r0, r1};  // speeds in tenths
  for (int i = 0; i < 8; i++)
    plink_put16(m + 1 + 2 * i, v[i]);
  anim_msg(K_ANIM_WANDER, m, sizeof m);
}

static void face_msg(int flags, int facing, int tilt, int rate, int len) {
  uint8_t m[8] = {0, (uint8_t) flags};
  plink_put16(m + 2, facing);
  plink_put16(m + 4, tilt);
  plink_put16(m + 6, rate);
  anim_msg(K_ANIM_FACE, m, len);
}

// How far a run of steps went, whether it kept to x0..x1, y0..y1 with its 16 x 8, and how often it stood still
typedef struct {
  int lo_x, hi_x, lo_y, hi_y, rests;
  bool inside;
} trail_t;

static trail_t trail(int steps, int x0, int y0, int x1, int y1) {
  trail_t t = {999, -999, 999, -999, 0, true};
  int16_t x, y, px = -1, py = -1;
  int still = 0;
  for (int i = 0; i < steps; i++) {
    sim_run(50);
    anim_pos(0, &x, &y);
    t.inside &= x >= x0 && x + 16 <= x1 && y >= y0 && y + 8 <= y1;
    t.lo_x = LV_MIN(t.lo_x, x);
    t.hi_x = LV_MAX(t.hi_x, x);
    t.lo_y = LV_MIN(t.lo_y, y);
    t.hi_y = LV_MAX(t.hi_y, y);
    still = x == px && y == py ? still + 1 : 0;
    t.rests += still == 3;
    px = x;
    py = y;
  }
  return t;
}

// K_ANIM_AREA, ANIM_WANDER and ANIM_FACE: a drift bounces inside its area or crosses out and back in, a wander keeps
// to its area, rests, and turns back off the edges, and a place can face where it goes
static void test_anim_paths(void) {
  odd_frames();
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 100, 100, 0, 0);
  show_anims();
  area_msg(50, 80, 200, 140, ANIM_EDGE_BOUNCE, ANIM_EDGE_BOUNCE);
  drift_msg(100, 37);
  trail_t t = trail(400, 50, 80, 200, 140);
  CHECK(t.inside && t.lo_x == 50 && t.hi_x == 184 && t.lo_y == 80 && t.hi_y == 132);
  // out across the right edge and in at the left, and back
  area_msg(50, 80, 200, 140, ANIM_EDGE_CROSS, ANIM_EDGE_BOUNCE);
  move_msg(180, 100, 0);
  drift_msg(100, 0);
  int xs[5], want[5] = {185, 190, 195, 34, 39};
  int16_t x, y;
  for (int i = 0; i < 5; i++) {
    sim_run(50);
    anim_pos(0, &x, &y);
    xs[i] = x;
  }
  CHECK(!memcmp(xs, want, sizeof xs));
  drift_msg(-100, 0);
  sim_run(50);
  CHECK(anim_pos(0, &x, &y) && x == 200);
  sim_run(50);
  CHECK(anim_pos(0, &x, &y) && x == 195);
  area_msg(50, 80, 70, 140, ANIM_EDGE_CROSS, ANIM_EDGE_BOUNCE);
  drift_msg(3000, 0);  // across the area many times a step, and still about it
  int lo = 9999, hi = -9999;
  for (int i = 0; i < 40; i++) {
    sim_run(50);
    anim_pos(0, &x, &y);
    lo = LV_MIN(lo, x);
    hi = LV_MAX(hi, x);
  }
  CHECK(lo >= 50 - 16 && hi < 70 && hi > lo);
  // a wander about its area, resting now and then
  area_msg(60, 40, 260, 200, ANIM_EDGE_BOUNCE, ANIM_EDGE_BOUNCE);
  wander_msg(20, 60, 0, 180, 200, 800, 0, 300);
  t = trail(1200, 60, 40, 260, 200);
  CHECK(t.inside && t.hi_x - t.lo_x > 100 && t.hi_y - t.lo_y > 60 && t.rests > 0);
  // heading right with little spread, it turns back off the right edge and goes all the way left
  wander_msg(100, 100, 0, 10, 300, 300, 0, 0);
  t = trail(200, 60, 40, 260, 200);
  CHECK(t.inside && t.lo_x == 60 && t.hi_x == 244 && !t.rests);
  // a bad area or wander changes nothing
  area_msg(60, 40, 60, 200, ANIM_EDGE_BOUNCE, ANIM_EDGE_BOUNCE);
  area_msg(60, 40, 260, 200, ANIM_EDGE_BOUNCE + 1, ANIM_EDGE_BOUNCE);
  anim_msg(K_ANIM_AREA, (uint8_t[]) {0, 0, 0, 0, 0, 1, 0, 1, 0, 0}, 10);
  t = trail(200, 60, 40, 260, 200);
  CHECK(t.inside);

  // frames that face right, mirrored to face left, leaning up to 30 degrees, upright when it stops
  area_msg(0, 0, PIC_MAX_W, PIC_MAX_H, ANIM_EDGE_BOUNCE, ANIM_EDGE_BOUNCE);
  move_msg(150, 100, 0);
  face_msg(ANIM_FACE_MIRROR | ANIM_FACE_LEVEL, 0, 30, 0, 8);
  int a, flip;
  drift_msg(-50, 0);
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip) && a == 0 && flip == ANIM_FLIP_X);
  drift_msg(-50, 50);  // down to the left
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip) && a == 330 && flip == ANIM_FLIP_X);
  drift_msg(50, 50);  // down to the right: unmirrored, the same lean the other way
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip) && a == 30 && !flip);
  drift_msg(0, 0);
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip) && a == 0 && !flip);
  // 90 degrees a second
  face_msg(ANIM_FACE_MIRROR, 0, 30, 90, 8);
  drift_msg(50, -50);
  sim_run(100);
  CHECK(anim_angle(0, &a, &flip) && a == 351);
  sim_run(500);
  CHECK(anim_angle(0, &a, &flip) && a == 330);
  // frames that face up turn all the way round toward where it goes
  face_msg(0, 270, 180, 0, 8);
  drift_msg(50, 0);
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip) && a == 90 && !flip);
  face_msg(0, 270, 181, 0, 8);
  face_msg(ANIM_FACE_LEVEL << 1, 270, 0, 0, 8);
  face_msg(0, 270, 0, -1, 8);
  face_msg(0, 270, 0, 0, 7);
  drift_msg(0, 50);
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip) && a == 180);
  glide_msg(0, ANIM_TURN, 45, 0, 0);  // a turn takes over
  sim_run(200);
  CHECK(anim_angle(0, &a, &flip) && a == 45);
  lv_obj_del(layer);
  anim_clear_msg(ANIM_ALL);
}

static void wave_msg(int p, int c, int lo, int hi, int rise, int fall, int hold_hi, int hold_lo, int jitter,
                     int flags) {
  uint8_t m[16] = {(uint8_t) p, (uint8_t) c};
  int v[6] = {lo, hi, rise, fall, hold_hi, hold_lo};
  for (int i = 0; i < 6; i++)
    plink_put16(m + 2 + 2 * i, v[i]);
  m[14] = (uint8_t) jitter;
  m[15] = (uint8_t) flags;
  anim_msg(K_ANIM_WAVE, m, sizeof m);
}

// WAVE: a size breathes there and back and settles, a blink holds at each end, a wave on x eases at its slow end,
// random legs stay between low and high, a wave scales a glide and a fade out, and there are ANIM_WAVES at most,
// counting the stopped ones only until they're needed
static void test_anim_wave(void) {
  odd_frames();
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 100, 100, 0, 0);
  show_anims();
  wave_msg(0, ANIM_SIZE, 100, 120, 1000, 1000, 0, 0, 0, 0);
  static const int BREATH[] = {1100, 1200, 1100, 1000, 1100};
  bool ok = true;
  for (int i = 0; i < 5; i++) {
    sim_run(500);
    ok &= look_size(0) == BREATH[i];
  }
  CHECK(ok);
  wave_msg(0, ANIM_SIZE, 100, 100, 500, 500, 0, 0, 0, 0);  // back down over the fall time
  sim_run(250);
  CHECK(look_size(0) == 1050);
  sim_run(350);
  CHECK(look_size(0) == 1000);
  // on for 500 ms and off for 300, at once
  wave_msg(0, ANIM_OPACITY, 0, 100, 0, 0, 500, 300, 0, 0);
  sim_run(450);
  CHECK(look_opa(0) == 1000);
  sim_run(100);
  CHECK(look_opa(0) == 0);
  sim_run(200);
  CHECK(look_opa(0) == 0);
  sim_run(100);
  CHECK(look_opa(0) == 1000);
  wave_msg(0, ANIM_OPACITY, 100, 100, 0, 0, 0, 0, 0, 0);
  // 100 px right and back, slow at the low end: it eases in going out and eases out coming back
  wave_msg(0, ANIM_X, 0, 100, 1000, 1000, 0, 0, 0, ANIM_WAVE_SLOW_LOW);
  int16_t x, y;
  sim_run(500);
  CHECK(anim_pos(0, &x, &y) && x == 125 && y == 100);
  sim_run(500);
  CHECK(anim_pos(0, &x, &y) && x == 200);
  sim_run(500);
  CHECK(anim_pos(0, &x, &y) && x == 125);
  sim_run(500);
  CHECK(anim_pos(0, &x, &y) && x == 100);
  wave_msg(0, ANIM_X, 0, 0, 0, 0, 0, 0, 0, 0);
  // random legs with times that vary
  wave_msg(0, ANIM_SIZE, 50, 150, 200, 200, 0, 0, 50, ANIM_WAVE_RANDOM);
  int lo = 9999, hi = 0;
  for (int i = 0; i < 200; i++) {
    sim_run(50);
    lo = LV_MIN(lo, look_size(0));
    hi = LV_MAX(hi, look_size(0));
  }
  CHECK(lo >= 500 && hi <= 1500 && hi - lo > 500);
  // it stays where low = high says, and scales what a glide sets
  wave_msg(0, ANIM_SIZE, 120, 120, 0, 0, 0, 0, 0, 0);
  glide_msg(0, ANIM_SIZE, 200, 0, 0);
  sim_run(1000);
  CHECK(look_size(0) == 2400);
  wave_msg(0, ANIM_SIZE, 100, 100, 0, 0, 0, 0, 0, 0);
  glide_msg(0, ANIM_SIZE, 100, 0, 0);
  sim_run(100);
  CHECK(look_size(0) == 1000);
  // bad ones change nothing
  wave_msg(0, ANIM_SIZE, 120, 110, 0, 0, 0, 0, 0, 0);
  wave_msg(0, ANIM_SIZE, 120, 120, 0, 0, 0, 0, 101, 0);
  wave_msg(0, ANIM_SIZE, 120, 120, 0, 0, 0, 0, 0, ANIM_WAVE_RANDOM << 1);
  wave_msg(0, ANIM_CHANNELS, 120, 120, 0, 0, 0, 0, 0, 0);
  anim_msg(K_ANIM_WAVE, (uint8_t[16]) {0, ANIM_SIZE, 120, 0, 120}, 15);
  sim_run(100);
  CHECK(look_size(0) == 1000);
  // ANIM_WAVES at most, one more waits for one to go
  lv_obj_clean(lv_scr_act());
  place(1, 0, 10, 10, 0, 0);
  place(2, 0, 10, 50, 0, 0);
  show_anims();
  for (int i = 0; i < ANIM_WAVES; i++) {
    int c = i % ANIM_CHANNELS, v = c >= ANIM_SIZE ? 90 : 1;
    wave_msg(i / ANIM_CHANNELS, c, v, v, 0, 0, 0, 0, 0, 0);
  }
  wave_msg(2, ANIM_SIZE, 150, 150, 0, 0, 0, 0, 0, 0);
  sim_run(100);
  CHECK(look_size(0) == 900 && look_size(2) == 1000);
  wave_msg(0, ANIM_SIZE, 100, 100, 0, 0, 0, 0, 0, 0);
  sim_run(100);
  wave_msg(2, ANIM_SIZE, 150, 150, 0, 0, 0, 0, 0, 0);
  sim_run(100);
  CHECK(look_size(0) == 1000 && look_size(2) == 1500);
  // one stopped but still on its way back makes room at once for a new one, not for a stop of none
  wave_msg(0, ANIM_OPACITY, 100, 100, 1000, 1000, 0, 0, 0, 0);
  wave_msg(2, ANIM_TURN, 0, 0, 0, 0, 0, 0, 0, 0);
  sim_run(100);
  CHECK(look_opa(0) > 900 && look_opa(0) < 1000);
  wave_msg(2, ANIM_OPACITY, 50, 50, 0, 0, 0, 0, 0, 0);
  sim_run(100);
  CHECK(look_opa(0) == 1000 && look_opa(2) == 500);
  // a place that goes takes its waves with it, and one leaving fades out flicker and all
  anim_clear_msg(ANIM_PLACE_BIT | 1);
  wave_msg(2, ANIM_OPACITY, 50, 100, 100, 100, 0, 0, 0, ANIM_WAVE_RANDOM);
  anim_clear_msg(ANIM_PLACE_BIT | 2 | 500 << 8);
  sim_run(300);
  CHECK(anim_pos(2, &x, &y) && look_opa(2) >= 200 && look_opa(2) <= 400);
  sim_run(300);
  CHECK(!anim_pos(2, &x, &y));
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
}

static void map_msg(int p, int c, int left, int right, int top, int bottom) {
  uint8_t m[10] = {(uint8_t) p, (uint8_t) c};
  int v[4] = {left, right, top, bottom};
  for (int i = 0; i < 4; i++)
    plink_put16(m + 2 + 2 * i, v[i]);
  anim_msg(K_ANIM_MAP, m, sizeof m);
}

// MAP: size, opacity, turn and speed follow where its middle is across its area, and the places bigger from their
// map are drawn in front
static void test_anim_map(void) {
  odd_frames();
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  show_anims();
  area_msg(0, 0, 320, 200, ANIM_EDGE_BOUNCE, ANIM_EDGE_BOUNCE);
  map_msg(0, ANIM_SIZE, 100, 100, 50, 100);  // smaller further up
  move_msg(152, 96, 0);                      // the 16 x 8 frame's middle halfway down
  CHECK(look_size(0) == 750);
  move_msg(152, -4, 0);  // at the top
  CHECK(look_size(0) == 500);
  move_msg(152, 296, 0);  // below the area it's the bottom's
  CHECK(look_size(0) == 1000);
  map_msg(0, ANIM_SIZE, 50, 100, 50, 100);  // left to right too, the two multiply
  move_msg(-8, -4, 0);
  CHECK(look_size(0) == 250);
  move_msg(152, 96, 0);
  CHECK(look_size(0) == 562);
  glide_msg(0, ANIM_SIZE, 200, 0, 0);  // and what a glide sets
  CHECK(look_size(0) == 1124);
  glide_msg(0, ANIM_SIZE, 100, 0, 0);
  map_msg(0, ANIM_SIZE, 100, 100, 100, 100);
  map_msg(0, ANIM_OPACITY, 100, 100, 20, 100);  // fainter further up
  CHECK(look_size(0) == 1000 && look_opa(0) == 600);
  map_msg(0, ANIM_OPACITY, 100, 100, 100, 100);
  int a, flip;
  map_msg(0, ANIM_TURN, 0, 360, 0, 0);  // rolls as it goes, half way round in the middle
  CHECK(anim_angle(0, &a, &flip) && a == 180);
  map_msg(0, ANIM_TURN, 0, 0, 0, 0);
  map_msg(0, ANIM_X, 50, 50, 50, 50);  // not x or y
  map_msg(0, ANIM_SPEED, 50, 50, 100, 100);
  drift_msg(40, 0);
  sim_run(500);
  int16_t x, y;
  CHECK(anim_pos(0, &x, &y) && x == 162 && y == 96);  // half of 2 px a step
  drift_msg(0, 0);
  // the nearer place is in front, and they swap as they go
  lv_obj_clean(lv_scr_act());
  place(0, 0, 100, 150, 0, 0);
  place(1, 0, 200, 20, 0, 0);
  map_msg(0, ANIM_SIZE, 100, 100, 50, 100);
  map_msg(1, ANIM_SIZE, 100, 100, 50, 100);
  show_anims();
  anim_look_t l[2];
  CHECK(anim_look(0, &l[0]) && anim_look(1, &l[1]) && l[0].depth > l[1].depth);
  move_msg(100, 0, 0);
  CHECK(anim_look(0, &l[0]) && anim_look(1, &l[1]) && l[0].depth < l[1].depth);
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
}

// FRAMES: a slot's frames go at its rate, the one it's on too, 0 holds it
static void test_anim_frames(void) {
  memset(an, 0xFF, sizeof an);
  an[0][1][1] = 0x12;
  an[1][2][2] = 0x34;
  an[2][2][1] = 0x34;
  an[3][0][0] = 0x56;
  uint8_t d[512];
  uint32_t n = anim_records(d, 10);  // 100, 110, 120, 130 ms
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
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

// px a loop of the 4 test frames redraws, a frame at a time from paused
static unsigned long loop_px(void) {
  unsigned long px = sim_px;
  for (int i = 0; i < 4; i++) {
    frames_msg(0, 100);  // on at once
    sim_run(20);
    frames_msg(0, 0);
    sim_run(30);
  }
  return sim_px - px;
}

// A frame redraws the rectangle it changed, flipped or sized too, and a size that leaves the box as it is redraws
// nothing. Every place is drawn by one object, without LVGL's heap.
static void test_anim_redraw(void) {
  memset(an, 0xFF, sizeof an);
  an[0][1][1] = 0x12;
  an[1][2][2] = 0x34;
  an[2][2][1] = 0x34;
  an[3][0][0] = 0x56;
  uint8_t d[512];
  uint32_t n = anim_records(d, 10);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  for (int k = 0; k < ANIM_PLACES; k++)
    place(k, 0, 10 + 30 * k, 100, 0, 0);
  lv_mem_monitor_t m0, m1;
  lv_mem_monitor(&m0);
  show_anims();
  lv_mem_monitor(&m1);
  CHECK(m0.free_size - m1.free_size < 200);
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 100, 100, 0, 0);
  show_anims();
  frames_msg(0, 0);
  sim_run(100);
  unsigned long plain = loop_px();
  CHECK(plain == 8 * 4 + 8 * 2 + 8 * 6 + 8 * 4);  // the rectangles the records changed, at 2x
  flip_msg(0, ANIM_FLIP_X | ANIM_FLIP_Y);
  sim_run(100);
  CHECK(loop_px() == plain && !sim_stale());
  glide_msg(0, ANIM_SIZE, 200, 0, 0);
  sim_run(100);
  CHECK(loop_px() == 18 * 10 + 18 * 6 + 17 * 13 + 17 * 9 && !sim_stale());  // twice that, and half a pixel round
  glide_msg(0, ANIM_TURN, 30, 0, 0);
  sim_run(100);
  loop_px();
  CHECK(!sim_stale());
  glide_msg(0, ANIM_TURN, 0, 0, 0);
  glide_msg(0, ANIM_SIZE, 100, 0, 0);
  sim_run(100);
  unsigned long px = sim_px;
  wave_msg(0, ANIM_SIZE, 97, 100, 1000, 1000, 0, 0, 0, 0);  // 16 x 8 all the way
  sim_run(3000);
  CHECK(sim_px == px && look_size(0) < 1000);
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
}

// ---------------------------------------------------------------- deflate
static int32_t like_test(void *ctx, uint32_t i) {  // where data byte i came from
  (void) ctx;
  return (int32_t) (1000 + i);
}

static void test_deflate(void) {
  // zlib's output inflates, a dynamic block, a fixed one and a stored one
  static const char TXT[] = "Clear sky over the hills, a red cabin and a flag. Clear sky over the hills, a red cabin "
                            "and a flag. Clear sky over the hills, a red cabin and a flag. ";
  static const uint8_t DYN[] = {
      0x05, 0xc1, 0x81, 0x09, 0x80, 0x20, 0x10, 0x86, 0xd1, 0x55, 0xbe, 0x01, 0xa2, 0x25, 0x9a, 0xe4, 0x4f,
      0x2f, 0x15, 0x0f, 0x85, 0x33, 0x02, 0xb7, 0xef, 0xbd, 0xcb, 0x4d, 0xc1, 0xea, 0x9b, 0xf9, 0x59, 0xf0,
      0x56, 0xa3, 0x36, 0xf7, 0x75, 0x20, 0xc2, 0x32, 0x49, 0x77, 0x1b, 0x68, 0x64, 0xc4, 0xe3, 0x2a, 0x27,
      0x97, 0x9b, 0x82, 0xd5, 0x37, 0xf3, 0xb3, 0xe0, 0xad, 0x46, 0x6d, 0xee, 0xeb, 0x40, 0x84, 0x65, 0x92,
      0xee, 0x36, 0xd0, 0xc8, 0x88, 0xc7, 0x55, 0x4e, 0x2e, 0x37, 0x05, 0xab, 0x6f, 0xe6, 0x67, 0xc1, 0x5b,
      0x8d, 0xda, 0xdc, 0xd7, 0x81, 0x08, 0xcb, 0x24, 0xdd, 0x6d, 0xa0, 0x91, 0x11, 0x8f, 0xab, 0x9c, 0xfc};
  static const uint8_t FIX[] = {0x73, 0xce, 0x49, 0x4d, 0x2c, 0x52, 0x28, 0xce, 0xae, 0x54, 0xc8, 0x2f, 0x4b, 0x2d,
                                0x52, 0x28, 0xc9, 0x48, 0x55, 0xc8, 0xc8, 0xcc, 0xc9, 0x29, 0xd6, 0x51, 0x48, 0x54,
                                0x28, 0x4a, 0x4d, 0x51, 0x48, 0x4e, 0x4c, 0xca, 0xcc, 0x53, 0x48, 0xcc, 0x4b, 0x01,
                                0xf2, 0xd3, 0x72, 0x12, 0xd3, 0xf5, 0x14, 0x9c, 0xe9, 0xa0, 0x03, 0x00};
  enum { N = sizeof TXT - 1 };
  uint8_t out[N], stored[5 + N] = {1, N, 0, (uint8_t) ~N, 0xFF};
  memcpy(stored + 5, TXT, N);
  CHECK(pinflate(DYN, sizeof DYN, out, N, N, N, 0) == sizeof DYN && !memcmp(out, TXT, N));
  CHECK(pinflate(FIX, sizeof FIX, out, N, N, N, 0) == sizeof FIX && !memcmp(out, TXT, N));
  CHECK(pinflate(stored, sizeof stored, out, N, N, N, 0) == sizeof stored && !memcmp(out, TXT, N));
  CHECK(pinflate(DYN, sizeof DYN, NULL, N, N, N, 0) == sizeof DYN);  // only checking
  // wrong length, cut short and a damaged code are refused
  CHECK(!pinflate(DYN, sizeof DYN, out, N, N, N - 1, 0) && !pinflate(DYN, sizeof DYN, out, N, N, N + 1, 0));
  CHECK(!pinflate(DYN, sizeof DYN - 1, out, N, N, N, 0) && !pinflate(stored, sizeof stored - 1, out, N, N, N, 0));
  stored[3] ^= 1;
  CHECK(!pinflate(stored, sizeof stored, out, N, N, N, 0));

  // our own round trip, also from and into a rectangle of a bigger buffer
  uint8_t big[40][24], back[40][24], c[PDEFLATE_BOUND(sizeof big)];
  for (int y = 0; y < 40; y++)
    for (int x = 0; x < 24; x++)
      big[y][x] = (uint8_t) ((x / 3 + y / 5) % 4 * 0x11);
  uint32_t k = pdeflate(&big[0][0], sizeof big, sizeof big, sizeof big, c, sizeof c);
  CHECK(k > 0 && k < sizeof big / 4 && pinflate(c, k, &back[0][0], sizeof back, sizeof back, sizeof back, 0) == k);
  CHECK(!memcmp(big, back, sizeof big));
  memset(back, 0xAB, sizeof back);
  k = pdeflate(&big[3][5], 10, 24, 10 * 20, c, sizeof c);  // 10 x 20 from (5, 3)
  CHECK(k > 0 && pinflate(c, k, &back[7][2], 10, 24, 10 * 20, 0) == k);
  bool same = true;
  for (int y = 0; y < 40; y++)
    for (int x = 0; x < 24; x++)
      same &= back[y][x] == (y >= 7 && y < 27 && x >= 2 && x < 12 ? big[y - 4][x + 3] : 0xAB);
  CHECK(same);
  CHECK(pdeflate(&big[0][0], sizeof big, sizeof big, sizeof big, c, 10) == 0);  // doesn't fit

  // pzip: pieces that inflate one by one into place, matching what's before them and the near bytes too
  static uint8_t pool[6000], data[3000], z[4000];
  static uint16_t table[PZIP_TABLE];
  uint32_t seed = 7;
  for (unsigned i = 0; i < sizeof pool; i++) {
    seed = seed * 1103515245u + 12345u;
    pool[i] = (uint8_t) (seed >> 16);
  }
  for (unsigned i = 0; i < sizeof data; i++)  // runs from the history, its own repeats, and noise
    data[i] = i % 100 < 40 ? pool[1000 + i] : i % 100 < 70 ? (uint8_t) (i / 7) : (uint8_t) (i * 131);
  for (int round = 0; round < 4; round++) {  // no history, history, like() too, like() without a table
    uint32_t nh = round == 0 ? 0 : 4000;
    pzip_t zp = {.hist = pool, .nh = nh, .data = data, .n = sizeof data, .table = round < 3 ? table : NULL};
    if (round >= 2)
      zp.like = like_test;
    static uint8_t outp[6000 + 3000];
    memcpy(outp, pool, nh);
    unsigned zn = 0, made = 0, m;
    bool ok = true;
    while ((m = pzip(&zp, z + zn))) {
      unsigned clen = z[zn], olen = z[zn + 1] | z[zn + 2] << 8;
      ok &= m <= PZIP_MAX && clen == m - PZIP_HEAD &&
            pinflate(z + zn + PZIP_HEAD, clen, outp + nh + made, olen, olen, olen, nh + made) == clen;
      made += olen;
      zn += m;
    }
    CHECK(ok && made == sizeof data && !memcmp(outp + nh, data, sizeof data));
    CHECK(round == 2 ? zn < sizeof data * 2 / 3 : zn < sizeof data);  // like() finds the history far back
  }
  // noise goes stored, a little over its size, and nothing at all is no piece
  for (unsigned i = 0; i < sizeof data; i++) {
    seed = seed * 1103515245u + 12345u;
    data[i] = (uint8_t) (seed >> 16);
  }
  pzip_t zr = {.data = data, .n = sizeof data, .table = table};
  unsigned zn = 0, m;
  while ((m = pzip(&zr, z + zn)))
    zn += m;
  CHECK(zn <= sizeof data + sizeof data / (PZIP_MAX - PZIP_HEAD - 5) * (PZIP_HEAD + 5) + PZIP_HEAD + 5);
  pzip_t none = {.data = data, .n = 0, .table = table};
  CHECK(pzip(&none, z) == 0);
  // a run to the very end, like() too: nothing read past it
  uint8_t *run = malloc(600), *got = malloc(600);
  memset(run, 0x5A, 600);
  pzip_t zt = {.hist = pool, .nh = 4000, .data = run, .n = 600, .table = table, .like = like_test};
  zn = 0;
  unsigned made = 0;
  bool ok = true;
  while ((m = pzip(&zt, z + zn))) {
    unsigned olen = z[zn + 1] | z[zn + 2] << 8;
    ok &= pinflate(z + zn + PZIP_HEAD, z[zn], got + made, olen, olen, olen, made) == z[zn];
    made += olen;
    zn += m;
  }
  CHECK(ok && made == 600 && !memcmp(run, got, 600) && zn < 40);
  free(run);
  free(got);
}

// ---------------------------------------------------------------- number and time formatting
// The letters' opacity table only has room for 4 bpp, see lv_draw_sw_letter.c in lib/lvgl-ci600.patch
static void test_fonts(void) {
  const lv_font_t *fonts[] = {&font_f12, &font_f14, &font_f16, &font_f20, &font_f28, &font_n40,
                              &font_n64, &icons_16, &icons_22, &icons_32, &icons_48};
  for (size_t i = 0; i < LEN(fonts); i++)
    CHECK(((const lv_font_fmt_txt_dsc_t *) fonts[i]->dsc)->bpp <= 4);
}

static void test_formats(void) {
  char b[24];
  CHECK(!strcmp(fmt_dec(b, sizeof b, 12345, 3), "12.345"));
  CHECK(!strcmp(fmt_dec(b, sizeof b, -12345, 2), "-123.45"));
  CHECK(!strcmp(fmt_dec(b, sizeof b, -5, 1), "-0.5"));
  CHECK(!strcmp(fmt_dec(b, sizeof b, 7, 0), "7"));
  CHECK(!strcmp(fmt_dec(b, sizeof b, INT32_MIN, 1), "-214748364.8"));
  M.clock_24h = false;
  CHECK(!strcmp(fmt_hm(b, sizeof b, 0), "12:00 am") && !strcmp(fmt_hm(b, sizeof b, 13 * 60 + 5), "1:05 pm"));
  CHECK(!strcmp(fmt_hm(b, sizeof b, 1440), "12:00 am"));
  M.clock_24h = true;
  CHECK(!strcmp(fmt_hm(b, sizeof b, 13 * 60 + 5), "13:05") && !strcmp(fmt_hm(b, sizeof b, 1440), "24:00"));
  // a period's start: 0..1435 in 15-minute steps
  CHECK(step_value(1425, 1, 15, 0, 1435) == 1435 && step_value(1435, -1, 15, 0, 1435) == 1425);
  CHECK(step_value(0, -1, 15, 0, 1435) == 0 && step_value(960, 1, 15, 5, 1440) == 975);
  CHECK(step_value(-10, 3, 5, -90, 90) == 5 && step_value(-88, -1, 5, -90, 90) == -90);
  // no space before % or °, but µg/m³ starts with the same byte as °
  CHECK(!strcmp(add_unit(strcpy(b, "5"), sizeof b, "%"), "5%") &&
        !strcmp(add_unit(strcpy(b, "21"), sizeof b, "\xC2\xB0"), "21\xC2\xB0"));
  CHECK(!strcmp(add_unit(strcpy(b, "12"), sizeof b, "\xC2\xB5g/m\xC2\xB3"), "12 \xC2\xB5g/m\xC2\xB3"));
  CHECK(!strcmp(fmt_pct(b, sizeof b, 50), "50%"));
  M.language = 1;  // Norwegian puts a space before %
  CHECK(!strcmp(fmt_pct(b, sizeof b, 50), "50 %") && !strcmp(add_unit(strcpy(b, "5"), sizeof b, "%"), "5 %"));
  M.language = 0;
  // texts are cut to fit their buffer
  CHECK(!strcmp(fmt_dec(b, 5, 123456, 0), "1234") && !strcmp(add_text(strcpy(b, "ab"), 4, "cd"), "abc"));
}

// ---------------------------------------------------------------- values
static void test_values(void) {
  model_defaults();
  for (int v = V_NONE + 1; v < V_COUNT; v++) {
    val_def_t d = val_def((val_t) v);
    CHECK(d.min <= d.max);
    if (d.key)
      CHECK((int) val_by_key(d.key) == v);  // keys must be unique, they identify values on the link and in flash
    if ((d.flags & (VF_CX60 | VF_BY_HAND)) == VF_CX60)
      CHECK((int) val_by_cx60(val_cx60_reg((val_t) v)) == v);
  }
  val_set(V_BRIGHTNESS, 150);
  CHECK(M.brightness == 100);  // clamped
  val_set(V_ROOM_TEMP, 5);
  CHECK(M.t_room == T_NONE);  // read-only
  val_set_remote(V_BUS_ADDR, 2);
  CHECK(M.bus_addr == 3);  // the ESP may only read it
  val_load(V_THEME, 9);
  CHECK(M.theme == N_THEMES - 1);  // loaded from flash, also clamped
}

// ---------------------------------------------------------------- the ESP's items
static void trend_hours(int h) {
  for (int i = 0; i < h * 3600; i++)
    trend_second();
}

static void test_items(void) {
  model_defaults();
  val_forget_esp();
  memset(M.item_trends, 0, sizeof M.item_trends);
  M.esp_online = true;
  item_t *co2 = &M.items[2], *target = &M.items[3], *button = &M.items[4];
  *co2 = (item_t) {.type = ITEM_SENSOR,
                   .flags = ITEMF_VIEW | ITEMF_TREND | ITEMF_LEVELS,
                   .id = 77,
                   .max = 2000,
                   .level = {0, 0, 800, 1200}};
  *target = (item_t) {.type = ITEM_NUMBER, .flags = ITEMF_MENU, .id = 78, .step = 50, .min = 400, .max = 1500};
  *button = (item_t) {.type = ITEM_BUTTON, .flags = ITEMF_VIEW, .id = 79, .max = 1};
  val_t v = V_ITEM_FIRST + 2;
  CHECK(!val_known(V_ITEM_FIRST) && val_known(v) && item_of(v) == co2 && item_by_id(77) == co2 && !item_by_id(5));
  val_set_remote(v, 2500);  // from the ESP, taken as it is
  CHECK(co2->value == 2500 && item_pct(co2) == 100);
  val_set(v, 400);  // a sensor can't be set on the panel
  CHECK(co2->value == 2500);
  val_set(v + 1, 2000);
  CHECK(target->value == 1500);  // a number the user sets is kept in its range
  char b[24];
  CHECK(!strcmp(item_text(b, sizeof b, button, true), _(S_PRESS)));
  item_t *washer = &M.items[6];
  *washer = (item_t) {.type = ITEM_TEXT, .flags = ITEMF_VIEW, .id = 80, .value = ITEM_UNKNOWN, .text = "Washing"};
  CHECK(!strcmp(item_text(b, sizeof b, washer, true), "Washing") && (val_def(v + 4).flags & VF_RO));
  washer->text[0] = 0;
  CHECK(!strcmp(item_text(b, sizeof b, washer, true), "\xE2\x80\x93"));

  trend_keep(77);
  for (int id = 100; id < 100 + N_TRENDS; id++)
    trend_keep(id);
  CHECK(trend_of(77) && trend_of(100) && !trend_of(100 + N_TRENDS - 1) && !trend_of(0));  // room for N_TRENDS
  M.items[5] = (item_t) {.type = ITEM_SENSOR, .flags = ITEMF_TREND, .id = 200, .value = 7};
  trend_hours(1);
  const item_trend_t *t = trend_of(77);
  CHECK(t && t->pt[TREND_LEN - 1] == 2500 && t->pt[TREND_LEN - 5] == TREND_NONE);
  CHECK(!trend_of(100));  // no item has that id, so it's freed at its first point
  CHECK(trend_of(200) && trend_of(200)->pt[TREND_LEN - 1] == 7);  // and an item that wants a trend gets the slot
  // The sensor becomes unavailable: a dash, not coloured by its levels, and no trend points
  T.accent = lv_color_hex(0x0000FF);
  T.err = lv_color_hex(0xFF0000);
  val_set_remote(v, ITEM_UNKNOWN);
  CHECK(!strcmp(item_text(b, sizeof b, co2, true), "\xE2\x80\x93") && !item_pct(co2) &&
        item_color(co2).full == T.accent.full);
  for (int i = 0; i < 900; i++)
    trend_second();
  CHECK(t->pt[TREND_LEN - 1] == TREND_NONE && t->pt[TREND_LEN - 2] == 2500);
  val_forget_esp();
  trend_hours(23);
  CHECK(trend_of(77) && trend_of(200));  // ESP gone for less than a day, kept
  trend_hours(1);
  CHECK(!trend_of(77) && !trend_of(200));
}

// ---------------------------------------------------------------- the clock and summer time
static void test_clock(void) {
  model_defaults();
  uint32_t t = 1792328640u;  // 2026-10-18 13:04
  clock_from_unix(t);
  CHECK(M.yy == 2026 && M.mo == 10 && M.dd == 18 && M.hh == 13 && M.mi == 4 && clock_to_unix() == t);
  // summer time 2026 is 29 March 02:00 to 25 October 03:00 summer time, which is 02:00 standard
  CHECK(!clock_summer(1774749600u - 1) && clock_summer(1774749600u));  // 2026-03-29 02:00
  CHECK(clock_summer(1792893600u - 1) && !clock_summer(1792893600u));  // 2026-10-25 02:00 standard
  // without an ESP the panel changes to and from summer time itself
  M.features = 0;
  M.auto_dst = true;
  M.dst_now = true;
  clock_from_unix(1792893600u + 3600 - 1);  // 02:59:59 summer time
  stub_rtc = clock_to_unix();
  clock_from_unix(stub_rtc + 1);
  int erases = stub_flash_erases;
  clock_second();
  CHECK(!M.dst_now && M.hh == 2 && M.mi == 0);  // 03:00 summer is 02:00 standard
  CHECK(stub_flash_erases == erases + 1);       // saved right away
  M.dst_now = true;                             // restart with the old flag: 02:xx comes twice, it stands
  clock_follow(clock_to_unix(), clock_to_unix());
  clock_second();
  CHECK(M.dst_now && M.hh == 2 && M.mi == 0);
  M.dst_now = false;
  clock_from_unix(1774749600u - 1);  // 01:59:59 standard time on 29 March
  clock_from_unix(clock_to_unix() + 1);
  clock_second();
  CHECK(M.dst_now && M.hh == 3 && M.mi == 0 && stub_rtc == 1774749600u + 3600);
  // a stale flag never moves the clock outside the hour of a switch: the settings wiped in summer
  M.dst_now = false;
  t = 1784124000u;  // 2026-07-15 14:00
  clock_from_unix(t);
  clock_second();
  CHECK(!M.dst_now && clock_to_unix() == t);
  clock_follow(t, t);  // from the RTC at boot, or the CX60
  CHECK(M.dst_now && clock_to_unix() == t);
  M.dst_now = true;  // and the other way, a summer flag in winter
  clock_from_unix(1792328640u);
  clock_second();
  CHECK(M.dst_now && clock_to_unix() == 1792328640u);
  clock_follow(1792897200u, 1792897200u);  // 03:00 on 25 October is standard time
  CHECK(!M.dst_now);
  clock_follow(1792893600u + 3600 + 1800, 1792893600u);  // 03:30 that day: still is
  CHECK(!M.dst_now);
  clock_follow(1792895400u, 1792895400u);  // 02:30 that day could be either, the flag stays
  CHECK(!M.dst_now);
  M.dst_now = true;
  clock_follow(1792895400u, 1792895400u);
  CHECK(M.dst_now);
  clock_follow(1792895400u, 1792895400u + 3600);  // unless the clock just moved back an hour
  CHECK(!M.dst_now);
  clock_follow(1774751400u, 1774751400u);  // 02:30 on 29 March doesn't exist, the flag stays either way
  CHECK(!M.dst_now);
  M.dst_now = true;
  clock_follow(1774751400u, 1774751400u);
  CHECK(M.dst_now);
  // the day stays in its month, so setting the day before the month doesn't roll the date over
  M.yy = 2026, M.mo = 2, M.dd = 15;
  val_set(V_DAY, 30);
  CHECK(M.yy == 2026 && M.mo == 2 && M.dd == 28);
  M.mo = 3, M.dd = 31;
  val_set(V_MONTH, 4);
  CHECK(M.mo == 4 && M.dd == 30);
  M.yy = 2028, M.mo = 2, M.dd = 29;
  val_set(V_YEAR, 2027);
  CHECK(M.yy == 2027 && M.mo == 2 && M.dd == 28);
  M.features = FBIT(FEAT_ESP) | FBIT(FEAT_EC_FANS);
}

// ---------------------------------------------------------------- the Modbus server

static void test_mbserver(void) {
  uint8_t q[300], r[MB_MAX_FRAME];
  mb_init(3, FW_VERSION, FW_PATCH);
  // a frame longer than any reply isn't answered, the reply would overflow
  memset(q, 0, sizeof q);
  q[0] = 3, q[1] = 0x64;
  CHECK(mb_frame(q, pbus_put_crc(q, 280), r) == 0);
  // the bootloader request is only accepted exactly as the updaters send it
  uint8_t enter[8] = {3, 0x64, 2, 0};
  CHECK(mb_frame(enter, pbus_put_crc(enter, 4), r) == 6 && MB.enter_bootloader);
  MB.enter_bootloader = false;
  uint8_t other[8] = {3, 0x64, 3, 0};
  CHECK(mb_frame(other, pbus_put_crc(other, 4), r) == 0 && !MB.enter_bootloader);
  // write coils with a byte count that doesn't match the coil count
  uint8_t coils[16] = {3, 0x0F, 0, 0, 0, 20, 1, 0xFF};
  CHECK(mb_frame(coils, pbus_put_crc(coils, 8), r) == 5 && r[1] == 0x8F);
  // only an actual change is reported to the main loop
  uint8_t w[8] = {0, 0x06, 0, 0x0C, 0, 200};
  uint16_t reg;
  mb_frame(w, pbus_put_crc(w, 6), r);
  CHECK(mb_take_changed(&reg) && reg == 0x0C && !mb_take_changed(&reg));
  mb_frame(w, pbus_put_crc(w, 6), r);
  CHECK(!mb_take_changed(&reg));  // same value again
  // discovery reads the input registers: our mark, the patch level and the version
  uint8_t disc[8] = {3, 0x04, 0, 0, 0, 5};
  CHECK(mb_frame(disc, pbus_put_crc(disc, 6), r) == 15 && (r[3] << 8 | r[4]) == FW_TYPE && (r[5] << 8 | r[6]) == 0);
  CHECK((r[7] << 8 | r[8]) == FW_PATCH && (r[9] << 8 | r[10]) == FW_VERSION);
  // Fan down twice, NORMAL -> MIN -> STOP. The CX60 has read MIN when STOP comes, and its relay of MIN mustn't
  // undo STOP.
  mb_write(0x000, 1);
  uint8_t rd[8] = {3, 0x03, 0, 0, 0, 1};
  CHECK(mb_frame(rd, pbus_put_crc(rd, 6), r) == 7 && r[4] == 1);
  mb_write(0x000, 0);
  uint8_t relay[8] = {0, 0x65, 0, 0, 0, 1};
  mb_frame(relay, pbus_put_crc(relay, 6), r);
  CHECK(MB.hold[0] == 0 && (MB.pending[0] & 1));  // the coil stays set for the CX60 to read STOP
  CHECK(mb_frame(rd, pbus_put_crc(rd, 6), r) == 7 && r[4] == 0);
  relay[5] = 0;
  mb_frame(relay, pbus_put_crc(relay, 6), r);
  CHECK(MB.hold[0] == 0 && !(MB.pending[0] & 1));
}

// What the RX interrupt does with a burst. Here a link frame gets no answer, the link hasn't started.
static void test_bus_burst(void) {
  model_defaults();
  unit_init();
  uint8_t b[64], r[MB_MAX_FRAME];
  uint8_t link[PBULK_FRAME(0)] = {PBULK_ADDR, PBULK_FC_SEND, 0, 3};
  int nl = pbus_put_crc(link, PBULK_PAYLOAD);
  uint8_t rd[8] = {3, 0x03, 0, 0x0C, 0, 1}, wr[8] = {0, 0x06, 0, 0x0C, 0, 200};
  pbus_put_crc(rd, 6);
  pbus_put_crc(wr, 6);
  bool modbus = true;
  CHECK(bus_burst(link, nl, 0, 2000, 2, r, &modbus) == 0 && !modbus);
  // the CX60 right behind a link frame: the link frame goes unanswered, the CX60's is handled
  memcpy(b, link, (size_t) nl);
  memcpy(b + nl, wr, 8);
  uint16_t reg;
  CHECK(bus_burst(b, nl + 8, 0, 2000, 2, r, &modbus) == 0 && modbus);
  CHECK(MB.hold[0x0C] == 200 && mb_take_changed(&reg) && reg == 0x0C);
  memcpy(b + nl, rd, 8);
  CHECK(bus_burst(b, nl + 8, 0, 2000, 2, r, &modbus) == 7 && modbus && r[0] == 3 && r[4] == 200);
  // a noise byte that looks like a link frame's start, in front of the CX60's
  b[0] = PBULK_ADDR;
  memcpy(b + 1, rd, 8);
  CHECK(bus_burst(b, 9, 0, 2000, 2, r, &modbus) == 7 && modbus && r[0] == 3 && r[4] == 200);
  // a link frame cut short is dropped
  modbus = true;
  CHECK(bus_burst(link, nl - 1, 0, 2000, 2, r, &modbus) == 0 && !modbus);
}

// ---------------------------------------------------------------- the CX60's clock, which the panels keep
static void relay(uint16_t reg, uint16_t v) {  // the CX60 passing a panel's change on to all panels
  uint8_t q[8] = {0, 0x65, (uint8_t) (reg >> 8), (uint8_t) reg, (uint8_t) (v >> 8), (uint8_t) v}, r[MB_MAX_FRAME];
  mb_frame(q, pbus_put_crc(q, 6), r);
}

static void block(uint16_t reg, const uint16_t *v, int n) {  // an FC16 broadcast, the CX60's own blocks
  uint8_t q[MB_MAX_FRAME] = {0, 0x10, (uint8_t) (reg >> 8), (uint8_t) reg, 0, (uint8_t) n, (uint8_t) (2 * n)};
  uint8_t r[MB_MAX_FRAME];
  for (int i = 0; i < n; i++) {
    q[7 + 2 * i] = (uint8_t) (v[i] >> 8);
    q[8 + 2 * i] = (uint8_t) v[i];
  }
  mb_frame(q, pbus_put_crc(q, 7 + 2 * n), r);
}

static void cx60(uint16_t reg, uint16_t v) { block(reg, &v, 1); }  // one register of a status block

static bool clock_coils(void) { return mb_pending(0x94) && mb_pending(0x95); }

static void test_bus_clock(void) {
  model_defaults();
  unit_init();
  uint32_t t = 1792328641u;  // 2026-10-18 13:04:01
  // written at :01 of each minute
  stub_rtc = t;
  clock_from_unix(t);
  unit_second();
  CHECK(clock_coils() && (uint16_t) MB.hold[0x94] == t >> 16 && (uint16_t) MB.hold[0x95] == (t & 0xFFFF));
  relay(0x94, (uint16_t) (t >> 16));
  relay(0x95, (uint16_t) t);
  CHECK(!clock_coils());
  // our own write coming back leaves the clock alone
  stub_rtc = t + 1;
  clock_from_unix(stub_rtc);
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK(stub_rtc == t + 1);
  clock_from_unix(t + 59);  // 13:05:00
  unit_second();
  CHECK(!clock_coils());
  clock_from_unix(t + 60);
  unit_second();
  CHECK(clock_coils() && (uint16_t) MB.hold[0x95] == ((t + 60) & 0xFFFF));
  relay(0x94, (uint16_t) (t >> 16));
  relay(0x95, (uint16_t) (t + 60));
  // Another panel sets the clock 18 hours on, which changes both words. The main loop runs between the two
  // relays, and the clock is only read once both are in.
  stub_rtc = t + 60;
  uint32_t t2 = (t | 0xFFFF) + 1 + 100;
  relay(0x94, (uint16_t) (t2 >> 16));
  for (int i = 0; i < 2; i++, stub_ms += 5)
    unit_poll();
  relay(0x95, (uint16_t) t2);
  for (int i = 0; i < 2; i++, stub_ms += 5)
    unit_poll();
  CHECK(stub_rtc == t + 60);
  stub_ms += 600;
  unit_poll();
  CHECK(stub_rtc == t2 && clock_to_unix() == t2);
  // one only a second off isn't followed, the clocks would fall behind
  relay(0x95, (uint16_t) (t2 - 1));
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK(stub_rtc == t2);
  // The settings block the CX60 sends when it starts has the clock it saved at its last power cut. Ours stays
  // and goes out right away, unless we have none.
  uint16_t old[2] = {(uint16_t) ((t2 - 7200) >> 16), (uint16_t) (t2 - 7200)};
  block(0x94, old, 2);
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK(stub_rtc == t2 && clock_to_unix() == t2);
  unit_second();
  CHECK(clock_coils() && (uint16_t) MB.hold[0x94] == t2 >> 16 && (uint16_t) MB.hold[0x95] == (t2 & 0xFFFF));
  stub_rtc = 0;
  clock_from_unix(0);
  old[1]--;
  block(0x94, old, 2);
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK(stub_rtc == t2 - 7201);

  // another panel switches back first, at our 02:59:59, and ours isn't made again an hour later
  M.features = 0;
  M.auto_dst = true;
  M.dst_now = true;
  uint32_t autumn = 1792893600u;  // 2026-10-25 02:00 standard
  stub_rtc = autumn + 3599;
  clock_from_unix(stub_rtc);
  relay(0x94, (uint16_t) (autumn >> 16));
  relay(0x95, (uint16_t) autumn);
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK(!M.dst_now && stub_rtc == autumn);
  clock_from_unix(autumn + 3600);
  clock_second();
  CHECK(!M.dst_now && M.hh == 3 && M.mi == 0);
  // a stock panel's 02:00 after our spring switch is followed without switching again
  uint32_t spring = 1774749600u;  // 2026-03-29 02:00
  M.dst_now = false;
  stub_rtc = spring;
  clock_from_unix(spring);
  clock_second();
  CHECK(M.dst_now && stub_rtc == spring + 3600 && clock_coils());
  relay(0x94, (uint16_t) ((spring + 1) >> 16));
  relay(0x95, (uint16_t) (spring + 1));
  unit_poll();
  stub_ms += 600;
  unit_poll();
  clock_second();
  CHECK(M.dst_now && stub_rtc == spring + 1 && M.hh == 2);
}

// ---------------------------------------------------------------- settings in the RAM flash, with power cuts
static void test_settings(void) {
  model_defaults();
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  settings_load();
  M.brightness = 42;
  plat_save(true);
  settings_flush();
  model_defaults();
  settings_load();
  CHECK(M.brightness == 42);
  // power fails while saving, the previous save is still there
  M.brightness = 55;
  plat_save(true);
  stub_flash_writes_left = 3;
  settings_flush();
  stub_flash_writes_left = -1;
  model_defaults();
  settings_load();
  CHECK(M.brightness == 42);
  // the next save works, and a save with no changes erases nothing
  M.brightness = 55;
  plat_save(true);
  settings_flush();
  int erases = stub_flash_erases;
  plat_save(true);
  settings_flush();
  CHECK(stub_flash_erases == erases);
  model_defaults();
  settings_load();
  CHECK(M.brightness == 55);
  // A copy that fails its check is skipped. Here the newest, the second save on the second page, is damaged
  // so the one before it loads.
  stub_flash[FLASH_PAGE + 20] ^= 1;
  model_defaults();
  settings_load();
  CHECK(M.brightness == 42);

  // saves take turns on all the pages, and the newest is loaded wherever it is
  for (int i = 0; i < 2 * FLASH_SETTINGS_PAGES; i++) {
    M.brightness = 10 + i;
    plat_save(true);
    settings_flush();
  }
  uint32_t magic = *(const uint32_t *) stub_flash;
  for (int i = 0; i < FLASH_SETTINGS_PAGES; i++)  // each holds a copy
    CHECK(magic != 0xFFFFFFFFu && *(const uint32_t *) (stub_flash + i * FLASH_PAGE) == magic);
  model_defaults();
  settings_load();
  CHECK(M.brightness == 10 + 2 * FLASH_SETTINGS_PAGES - 1);

  // a change on the keys is saved 5 s after the last one, others at most once an hour
  erases = stub_flash_erases;
  M.brightness = 70;
  plat_save(false);
  stub_ms += 6000;
  settings_poll();
  CHECK(stub_flash_erases == erases);  // written less than an hour ago
  stub_ms += 3600 * 1000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 1);
  M.brightness = 71;
  plat_save(false);
  stub_ms += 6000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 1);
  M.brightness = 72;
  plat_save(true);  // on the keys, saves this one and the one waiting
  stub_ms += 1000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 1);  // not before 5 s
  stub_ms += 5000;
  stub_bus_quiet = false;
  settings_poll();
  CHECK(stub_flash_erases == erases + 1);  // nor while the bus is busy, an erase stalls the bus interrupt
  stub_bus_quiet = true;
  stub_ms += 6000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 2);
  model_defaults();
  settings_load();
  CHECK(M.brightness == 72);

  // a page that fails a save is skipped next time
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  settings_load();
  stub_flash_bad = FLASH_SETTINGS;
  M.brightness = 61;
  plat_save(true);
  settings_flush();
  settings_flush();
  stub_flash_bad = 0;
  model_defaults();
  settings_load();
  CHECK(M.brightness == 61);
}

// ---------------------------------------------------------------- the runtime counters, kept in flash
static void test_runtime(void) {
  model_defaults();
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  unit_init();
  settings_load();
  uint16_t rt[20] = {0};
  rt[0] = 1;   // stop: 65536 s, 18 h
  rt[16] = 5;  // total: 91 h
  block(0x14C, rt, 20);
  unit_poll();
  CHECK(M.h_stop == 18 && M.h_total == 91);
  // the low words come every second, and mustn't hold up a save made on the keys
  int erases = stub_flash_erases;
  M.brightness = 33;
  plat_save(true);
  for (int s = 0; s < 7; s++) {
    rt[17]++;
    block(0x14C, rt, 20);
    unit_poll();
    stub_ms += 1000;
    settings_poll();
  }
  CHECK(stub_flash_erases == erases + 1);
  // after a restart the counters are back from flash, the CX60 only sends the high words when it starts
  model_defaults();
  unit_init();
  settings_load();
  unit_poll();
  CHECK(M.h_stop == 18 && M.h_total == 91 && M.brightness == 33);
  // the total's high word changed while we were off
  cx60(0x15D, 9);
  cx60(0x0CB, 109);
  unit_poll();
  CHECK(M.h_total == 109);
  // saved soon: a high word and a relayed week program change, not a relayed fan change
  erases = stub_flash_erases;
  cx60(0x15C, 6);
  unit_poll();
  stub_ms += 6000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 1);
  relay(0x118, 1);
  unit_poll();
  stub_ms += 6000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 2);
  relay(0x002, 50);
  unit_poll();
  stub_ms += 6000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 2);
  M.features &= ~FBIT(FEAT_ESP);  // no ESP, so the relay is another panel's
  relay(0x002, 55);
  unit_poll();
  stub_ms += 6000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 3);
}

// The de-icing settings sit among the counters, 0x158..0x15B, but they're saved like the other settings
static void test_deice_saved(void) {
  model_defaults();
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  unit_init();
  settings_load();
  plat_save(true);
  settings_flush();  // so the hourly save isn't due
  // with an ESP a relayed change waits for the hourly save, the ESP sends it again when we restart
  int erases = stub_flash_erases;
  relay(0x158, 1);
  unit_poll();
  stub_ms += 6000;
  settings_poll();
  CHECK(stub_flash_erases == erases);
  // without one it's another panel's, which nobody sends again
  M.features &= ~FBIT(FEAT_ESP);
  const int16_t v[4] = {0, -200, -150, 60};
  for (int i = 0; i < 4; i++) {
    relay((uint16_t) (0x158 + i), (uint16_t) v[i]);
    unit_poll();
    stub_ms += 6000;
    settings_poll();
    CHECK(stub_flash_erases == erases + 1 + i);
  }
  // and they're back after a restart
  model_defaults();
  unit_init();
  settings_load();
  unit_poll();
  CHECK(!M.adv.deice && M.adv.deice_temp == -200 && M.adv.less_air_temp == -150 && M.adv.deice_rotor == 60);
}

// ---------------------------------------------------------------- the LEDs
static void test_leds(void) {
  model_defaults();
  bool l[3];
  M.led_ok = LED_OK_ALWAYS;
  panel_leds(l, 0);
  CHECK(!l[0] && !l[1] && l[2]);
  M.alarms[0] = (alarm_t) {.code = ALARM_FILTER, .active = true};
  M.n_alarms = 1;
  panel_leds(l, 0);
  CHECK(!l[0] && l[1] && !l[2]);
  M.alarms[0].code = ALARM_FILTER_GUARD;  // on the filter light too, as stock has it
  panel_leds(l, 0);
  CHECK(!l[0] && l[1] && !l[2]);
  M.alarms[1] = (alarm_t) {.code = 9, .active = true};
  M.n_alarms = 2;
  panel_leds(l, 0);
  CHECK(l[0] && l[1] && !l[2]);
  M.n_alarms = 0;
  M.led_ok = LED_OK_NOT_NIGHT;
  M.night = true;
  M.night_from = 22;
  M.night_to = 7;
  M.hh = 23;
  panel_leds(l, 0);
  CHECK(!l[2]);
  // a blink set by the ESP actually blinks
  M.leds = 3 << 4;  // the OK LED
  panel_leds(l, 100);
  bool a = l[2];
  panel_leds(l, 600);
  CHECK(a != l[2]);
  M.leds = 0;
  M.night = false;
}

// ---------------------------------------------------------------- the week program
static int layout_changes, saver_builds;  // see --wrap in the Makefile
void __wrap_ui_layout_changed(void) { layout_changes++; }
void __real_ui_value_changed(val_t v);
void __wrap_ui_value_changed(val_t v) {
  saver_builds += v == V_SCREENSAVER;
  __real_ui_value_changed(v);
}

static period_t period(int start, int end, int idx) {
  return (period_t) {(uint16_t) start, (uint16_t) end, MODE_NORMAL, (uint8_t) idx, 200};
}

static void test_schedule(void) {
  model_defaults();
  mb_init(3, FW_VERSION, FW_PATCH);
  // a new period is 08:00-16:00 if that's free, else the first free time
  CHECK(sched_edit(0, -1) && M.n_periods[0] == 1 && M.period.p.start == 8 * 60 && M.period.p.end == 16 * 60);
  const period_t day[3] = {period(390, 480, 0), period(480, 930, 1), period(930, 1350, 2)};
  sched_store(1, day, 3);
  CHECK(sched_edit(1, -1) && M.period.p.idx == 3 && M.period.p.start == 0 && M.period.p.end == 390);
  CHECK(!sched_can_add(1));
  const period_t full[3] = {period(0, 480, 0), period(480, 960, 1), period(960, 1440, 2)};
  sched_store(2, full, 3);
  CHECK(!sched_can_add(2) && !sched_edit(2, -1) && M.n_periods[2] == 3);
  // a gap no longer than the shortest period takes no new period, its start and end couldn't be edited
  const period_t gap[2] = {period(0, 480, 0), period(485, 1440, 1)};
  sched_store(3, gap, 2);
  CHECK(!sched_can_add(3));
  const period_t gap6[2] = {period(0, 480, 0), period(486, 1440, 1)};
  sched_store(3, gap6, 2);
  int32_t lo, hi;
  CHECK(sched_edit(3, -1) && M.period.p.start == 480 && M.period.p.end == 486);
  sched_range(V_PERIOD_START, &lo, &hi);
  CHECK(lo == 480 && hi == 481);
  // an edit stays between its neighbours, 08:00-15:30 between 06:30-08:00 and 15:30-22:30
  sched_store(1, day, 3);
  CHECK(sched_edit(1, 1));
  val_set(V_PERIOD_START, 7 * 60);
  val_set(V_PERIOD_END, 1440);
  CHECK(M.period.p.start == 480 && M.period.p.end == 930);
  val_set(V_PERIOD_START, 1000);  // pushes the end along, as far as the next one
  CHECK(M.period.p.start == 925 && M.period.p.end == 930);
  val_set(V_PERIOD_END, 0);
  CHECK(M.period.p.start == 480 && M.period.p.end == 485 && M.week[1][1].end == 485);
  // the screens are rebuilt when a day changes, also from the bus, and not when it doesn't
  layout_changes = 0;
  sched_store(1, M.week[1], M.n_periods[1]);
  CHECK(layout_changes == 0);
  for (int i = 0; i < 2; i++) {
    mb_load((uint16_t) (0x01C + i * 4), (int16_t) (360 + i * 120));  // Monday, start and end
    mb_load((uint16_t) (0x01D + i * 4), (int16_t) (420 + i * 120));
    mb_load((uint16_t) (0x118 + i), 1);
  }
  unit_poll();
  CHECK(M.n_periods[0] == 2 && layout_changes > 0);
  layout_changes = 0;
  mb_load(0x119, 0);  // another panel removed the second
  unit_poll();
  CHECK(M.n_periods[0] == 1 && layout_changes == 1);
}

// ---------------------------------------------------------------- keys and idle (input.c): what reaches the UI
// input.c's calls into the UI land here, see --wrap in the Makefile
static int keys[64], n_keys, idles, wakes, toasts, lock_notices[2];  // lock_notices[toggled]
void __wrap_ui_key(int k) {
  if (n_keys < (int) LEN(keys))
    keys[n_keys++] = k;
}
void __wrap_ui_idle(void) { idles++; }
void __wrap_ui_wake(void) { wakes++; }
void __wrap_ui_toast(const char *icon, str_t msg) {
  (void) icon;
  (void) msg;
  toasts++;
}
void __wrap_ui_key_lock_notice(bool toggled) { lock_notices[toggled]++; }

static uint32_t now_ms = 1000;

// Hold key k for ms with the keys in also held the whole time. Calls input.c every 5 ms like the main loop.
static void hold(int k, uint32_t ms, uint8_t also) {
  uint8_t bit = (uint8_t) (1u << k);
  input_keys(bit, 0, bit | also, now_ms);
  for (uint32_t t = 0; t < ms; t += 5) {
    now_ms += 5;
    input_keys(0, 0, bit | also, now_ms);
    input_tick(now_ms);
  }
  input_keys(0, bit, also, now_ms);
}

static void test_input(void) {
  model_defaults();
  M.idle_after = 0;  // 15 s
  input_tick(now_ms);
  hold(IN_OK, 50, 0);
  CHECK(n_keys == 1 && keys[0] == KEY_OK);
  // idle at 15 s exactly
  now_ms += 14000;
  input_tick(now_ms);
  CHECK(!input_idle());
  now_ms += 1000 - 50;
  input_tick(now_ms);
  CHECK(input_idle() && idles == 1);
  // the first press only wakes it
  n_keys = 0;
  hold(IN_DOWN, 50, 0);
  CHECK(!input_idle() && wakes == 1 && n_keys == 0);
  // a long OK fires once, and tapping another key meanwhile doesn't fire it again
  hold(IN_OK, 2500, 0);
  CHECK(n_keys == 2 && keys[0] == KEY_OK && keys[1] == KEY_OK_LONG);
  n_keys = 0;
  uint8_t ok = 1u << IN_OK;
  input_keys(ok, 0, ok, now_ms);
  for (int i = 0; i < 500; i++) {
    now_ms += 5;
    input_keys(0, 0, ok, now_ms);
  }
  hold(IN_DOWN, 20, ok);
  for (int i = 0; i < 100; i++) {
    now_ms += 5;
    input_keys(0, 0, ok, now_ms);
  }
  input_keys(0, ok, 0, now_ms);
  CHECK(n_keys == 3 && keys[1] == KEY_OK_LONG && keys[2] == KEY_DOWN);
  // BACK tapped is BACK, held is HOME with no BACK after it
  n_keys = 0;
  hold(IN_BACK, 100, 0);
  hold(IN_BACK, 1000, 0);
  CHECK(n_keys == 2 && keys[0] == KEY_BACK && keys[1] == KEY_HOME);
  // holding HELP for 3 s locks the keys, then a key press is refused with a toast
  n_keys = 0;
  hold(IN_HELP, 3100, 0);
  CHECK(M.key_lock && n_keys == 0 && lock_notices[true] == 1);
  hold(IN_OK, 50, 0);
  CHECK(n_keys == 0 && lock_notices[false] == 1);
  hold(IN_HELP, 3100, 0);
  CHECK(!M.key_lock && lock_notices[true] == 2 && lock_notices[false] == 1);
  // UP held repeats at 450 ms, then every 120 ms
  n_keys = 0;
  hold(IN_UP, 1000, 0);
  CHECK(n_keys == 6);
}

// ---------------------------------------------------------------- link.c: what the ESP's messages do
static int views, shown_view;
void __wrap_ui_show_view(int v) {
  views++;
  shown_view = v;
}

static uint16_t esp_num;  // number of the ESP's next message

// A message from the ESP in a frame of its own, the way the RX interrupt hands it over, then the main loop. With
// ack the ESP has all of the panel's messages, else it hasn't heard its answers.
static void from_esp(uint16_t key, const uint8_t *b, int n, bool ack) {
  const pbulk_panel_t *p = link_bulk_state();
  uint8_t f[PBULK_FRAME_MAX] = {PBULK_ADDR, PBULK_FC_SEND, (uint8_t) (3 + n), M.bus_addr}, a[PBULK_ANSWER_FRAME_MAX];
  plink_put16(f + PBULK_ESESS, 0xE5);
  plink_put16(f + PBULK_NUM, esp_num++);
  plink_put16(f + PBULK_PSESS, ack ? p->psess : 0);
  plink_put16(f + PBULK_PNEXT, p->pnum);
  plink_put16(f + PBULK_PAYLOAD, key);
  f[PBULK_PAYLOAD + 2] = (uint8_t) n;
  memcpy(f + PBULK_PAYLOAD + 3, b, (size_t) n);
  pbus_put_crc(f, PBULK_PAYLOAD + 3 + n);
  link_bulk_frame(f, PBULK_FRAME(3 + n), 0, 0, a);
  link_poll();
}

static void number_from_esp(uint16_t key, uint32_t v, bool ack) {
  uint8_t b[4];
  plink_put32(b, v);
  from_esp(key, b, 4, ack);
}

static int32_t via_link(uint16_t key, const uint8_t *b, int n) {
  uint8_t m[256] = {0};  // DATA in multiples of 4 bytes, the ESP pads its last. Each 4 count as a message.
  memcpy(m, b, (size_t) n);
  from_esp(key, m, key == K_PIC_DATA ? (n + 3) & ~3 : n, false);
  esp_num = (uint16_t) (esp_num + (key == K_PIC_DATA ? (n + 3) / 4 - 1 : 0));
  return -1;
}

static void test_link_picture(void) {
  to_pic = via_link;
  fill(0, H, 21);
  saver_builds = 0;
  send_picture(64);
  CHECK(pic_state() == PIC_READY && shows() && saver_builds > 0);
  saver_builds = 0;
  fill(1, 2, 22);
  uint8_t b[512];
  unsigned n = pack(1, 2, b);
  rest_zip(patch(1, 2, n, false), b, n, NULL, 0, 64);
  CHECK(shows() && saver_builds == 0);
  to_pic = pic_msg;
  pic_msg(K_PIC_CLEAR, NULL, 0);
}

// Our K_SETTINGS as link_poll() queues them, taken out so the next ones fit. Returns how many.
static int settings_out(uint32_t *out, int max) {
  plink_t *l = (plink_t *) link_queue();
  int k = 0;
  for (int i = 0; i < 20; i++) {
    for (int j = 0; j < l->qn && k < max; j++)
      if (l->key[j] == K_SETTINGS)
        out[k++] = l->value[j];
    l->qn = 0;
    link_poll();
  }
  return k;
}

static bool queued(uint16_t key, uint32_t value) {
  const plink_t *l = link_queue();
  for (int i = 0; i < l->qn; i++)
    if (l->key[i] == key && l->value[i] == value)
      return true;
  return false;
}

// ---------------------------------------------------------------- the installer's settings and the manual tests
static void test_advanced(void) {
  model_defaults();
  unit_init();
  // They come and go by register like the other settings
  cx60(0x159, (uint16_t) -150);
  unit_poll();
  CHECK(M.adv.deice_temp == -150);
  val_set(V_DEICE_TEMP, -200);
  CHECK(MB.hold[0x159] == -200 && mb_pending(0x159));
  // DIP switches, the panels the unit found and the filter changes
  cx60(0x10F, 1);
  cx60(0x112, 1);
  cx60(0x09B, 0x0703);
  cx60(0x0CC, 4);
  unit_poll();
  CHECK(M.dip == 0x9 && M.panel_version[1] == 0x0703 && !M.panel_version[0] && M.filter_resets == 4);
  cx60(0x10F, 0);
  unit_poll();
  CHECK(M.dip == 0x8);
  // From the ESP after we started, like the CX60 sent it. A change of ours the CX60 hasn't read yet stays.
  plat_cx60_reg(0x0B5, 75);
  unit_poll();
  CHECK(M.adv.pi[0] == 75);
  val_set(V_PI_HEAT_SUP_I, 500);
  plat_cx60_reg(0x0B6, 900);
  unit_poll();
  CHECK(M.adv.pi[1] == 500 && MB.hold[0x0B6] == 500);
  // A test writes its value, then its enable, and is over after a minute like in the CX60
  val_set(V_TEST_SPEED, MODE_MAX + 1);
  CHECK(MB.hold[0x0A8] == MODE_MAX && MB.hold[0x143] == 1 && mb_pending(0x0A8) && mb_pending(0x143));
  val_set(V_TEST_HEATER, 1);
  CHECK(MB.hold[0x145] == 1 && MB.hold[0x146] == 1);
  for (int s = 0; s < 59; s++)
    unit_second();
  CHECK(M.test_speed == MODE_MAX + 1 && M.test_heater);
  unit_second();
  CHECK(!M.test_speed && !M.test_heater);
  // Off ends it right away
  val_set(V_TEST_ROTOR, 1);
  CHECK(MB.hold[0x148] == 1);
  val_set(V_TEST_ROTOR, 0);
  CHECK(!MB.hold[0x148]);
}

// ---------------------------------------------------------------- the CX60's registers, in and out of the model
static void test_unit(void) {
  model_defaults();
  unit_init();
  toasts = 0;
  // The CX60 ORs the heater flags of the panels present, so off has to clear all three and on only sets ours.
  // What it made of them, 0x10E, is what's shown.
  cx60(0x10E, 1);
  unit_poll();
  CHECK(M.heater_enabled);
  val_set(V_HEATER, 0);
  CHECK(!MB.hold[0x13F] && !MB.hold[0x140] && !MB.hold[0x141] && mb_pending(0x13F) && mb_pending(0x140) &&
        mb_pending(0x141));
  for (uint16_t r = 0x13F; r <= 0x141; r++)
    relay(r, 0);
  cx60(0x10E, 0);
  unit_poll();
  CHECK(!M.heater_enabled);
  val_set(V_HEATER, 1);
  CHECK(M.heater_enabled && MB.hold[0x141] == 1 && mb_pending(0x141) && !mb_pending(0x13F) && !mb_pending(0x140));
  relay(0x141, 1);
  for (int s = 0; s < 3; s++) {  // our flag is one vote, and the unit didn't take it: no heater, say
    cx60(0x0C3, (uint16_t) (200 + s));
    unit_poll();
    stub_ms += 1000;
    unit_second();
  }
  CHECK(!M.heater_enabled && toasts == 1);
  toasts = 0;
  cx60(0x10E, 1);
  unit_poll();
  CHECK(M.heater_enabled);

  // The mode set and the one running. A boost runs MAX while NORMAL is set, and Boost -> Max on the overlay
  // then writes MAX all the same.
  cx60(0x000, MODE_NORMAL);
  cx60(0x0BF, MODE_MAX);
  cx60(0x0C1, 600);
  unit_poll();
  CHECK(M.mode == MODE_NORMAL && M.mode_now == MODE_MAX && M.boost_on && M.boost_left == 10);
  val_set(V_BOOST, 0);
  val_set(V_MODE, MODE_MAX);
  CHECK(mb_pending(0x13B) && MB.hold[0x000] == MODE_MAX && mb_pending(0x000));
  relay(0x13B, 1);
  relay(0x000, MODE_MAX);
  cx60(0x0C1, 0);
  unit_poll();
  CHECK(!M.boost_on && !M.boost_left);

  // the setpoint in effect is 0xC2, 0xBE is the one set
  cx60(0x0BE, 210);
  cx60(0x0C2, 190);
  unit_poll();
  CHECK(M.setpoint_now == 190 && M.setpoint == 210);

  // extract regulation needs the extract sensor switched on, which the stock panel does along with it
  cx60(0x114, REG_SUPPLY);
  unit_poll();
  val_set(V_REGULATION, REG_EXTRACT);
  CHECK(MB.hold[0x114] == REG_EXTRACT && mb_pending(0x114) && MB.hold[0x136] == 1 && mb_pending(0x136));
  // supply air too cold, 0x0C2 is min + 6
  cx60(0x00D, 160);
  cx60(0x0C2, 166);
  unit_poll();
  CHECK(M.supply_min == 160 && M.setpoint_now == 190);

  cx60(0x114, 7);  // anything but 0 is supply air to the CX60
  unit_poll();
  CHECK(M.regulation == REG_SUPPLY);

  // not the master, the ESP writes the setpoint, or we do while it's offline
  cx60(0x00F, 1);
  unit_poll();
  M.esp_online = true;
  val_set(V_SETPOINT, 215);
  CHECK(!mb_pending(0x00C));
  M.esp_online = false;
  val_set(V_SETPOINT, 220);
  CHECK(mb_pending(0x00C) && MB.hold[0x00C] == 220);
  relay(0x00C, 210);
  unit_poll();
  CHECK(M.setpoint == 210);

  // EC fans come from the DIP switches, not the menu
  cx60(0x112, 0);
  unit_poll();
  val_set(V_FEAT_FIRST + FEAT_EC_FANS, 0);
  CHECK(HAS(FEAT_EC_FANS));

  // the CX60's block and another panel like us change the week program, a stock panel's relays don't
  val_set(V_WEEK_PROGRAM, 1);
  relay(0x117, 1);
  cx60(0x117, 0);
  unit_poll();
  CHECK(!M.week_program);
  val_set(V_WEEK_PROGRAM, 1);
  relay(0x117, 1);
  relay(0x117, 0);
  unit_poll();
  CHECK(!M.week_program);
  cx60(0x099, 0x0712);  // stock 7.18 at address 1
  val_set(V_WEEK_PROGRAM, 1);
  relay(0x117, 1);
  relay(0x117, 0);
  unit_poll();
  CHECK(M.week_program);

  // what the CX60 holds is shown as it is, its limits are wider than the editors'
  cx60(0x091, 300);
  cx60(0x016, 0);
  unit_poll();
  CHECK(M.boost_minutes == 300 && M.filter_days == 0 && filter_life() == 0 && filter_days_left() == 0);
  // a filter interval that isn't whole months is due when the CX60 says
  cx60(0x016, 100);
  cx60(0x0C0, 90 * 24);
  unit_poll();
  CHECK(M.filter_days == 100 && filter_days_left() == 10);

  // A boost the CX60 doesn't count down, nothing answers at address 1, is stopped after a while and the user told
  val_set(V_BOOST, 1);
  CHECK(mb_pending(0x13A));
  for (int s = 0; s < 12; s++) {  // counting down
    cx60(0x0C1, (uint16_t) (1800 - s));
    unit_poll();
    unit_second();
  }
  CHECK(M.boost_on && !mb_pending(0x13B) && !toasts);
  for (int s = 0; s < 10; s++) {  // stuck, 9 seconds without a change
    cx60(0x0C1, 1700);
    unit_poll();
    unit_second();
  }
  CHECK(!mb_pending(0x13B) && !toasts);
  cx60(0x0C1, 1700);
  unit_poll();
  unit_second();
  CHECK(mb_pending(0x13B) && toasts == 1);
  cx60(0x0C1, 0);
  unit_poll();

  // a stock panel sets back a time set on the keys: one toast, a later change is just followed
  toasts = 0;
  uint32_t t = 1792328641u;  // 2026-10-18 13:04:01
  stub_rtc = t;
  clock_from_unix(t);
  val_set(V_HOUR, 15);
  CHECK(clock_coils() && stub_rtc == t + 7200);
  relay(0x94, (uint16_t) ((t + 1) >> 16));
  relay(0x95, (uint16_t) (t + 1));
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK(stub_rtc == t + 1 && toasts == 1);
  for (int s = 0; s < 5; s++)
    unit_second();
  relay(0x95, (uint16_t) (t + 600));
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK(stub_rtc == t + 600 && toasts == 1);
}

static void test_link(void) {
  model_defaults();
  M.features |= FBIT(FEAT_ESP);
  M.idle_after = 0;  // 15 s
  link_init();
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16, false);
  CHECK(link_compatible());
  // only views that exist, and one wakes the panel
  number_from_esp(K_SHOW_VIEW, VIEW_COUNT, false);
  number_from_esp(K_SHOW_VIEW, UINT32_MAX, false);
  CHECK(!views);
  now_ms += 20000;
  input_tick(now_ms);
  CHECK(input_idle());
  wakes = 0;
  number_from_esp(K_SHOW_VIEW, VIEW_FLOW, false);
  CHECK(views == 1 && shown_view == VIEW_FLOW && !input_idle() && wakes == 1);
  // Before the ESP flashes us it asks for our settings: the format, each saved value, then how many
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  M.theme = 2;
  plat_save_now();
  int saved_n = 0, theme_at = -1;
  for (val_t s = val_next(V_NONE, VF_SAVED, 0); s != V_COUNT; s = val_next(s, VF_SAVED, 0))
    saved_n++;
  number_from_esp(K_SETTINGS, 0, false);
  uint32_t snap[256];
  int k = settings_out(snap, 256);
  for (int i = 0; i < k; i++)
    if (snap[i] == ((uint32_t) K_THEME << 16 | 2))
      theme_at = i;
  CHECK(k == saved_n + 2 && snap[0] == SETTINGS_FORMAT && snap[k - 1] == ((uint32_t) SETTINGS_END << 16 | saved_n));
  CHECK(theme_at > 0);
  // Not taken by a panel with settings of its own
  int resets = stub_resets;
  M.theme = 0;
  for (int i = 0; i < k; i++)
    number_from_esp(K_SETTINGS, snap[i], false);
  CHECK(M.theme == 0 && stub_resets == resets);
  // After the flash it starts without any and says so, and takes them back and restarts with them saved
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  settings_load();
  link_init();
  CHECK(queued(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK | HELLO_FRESH));
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16, false);
  for (int i = 0; i < k; i++)
    number_from_esp(K_SETTINGS, snap[i], false);
  CHECK(M.theme == 2 && stub_resets == resets + 1);
  M.theme = 0;
  settings_load();
  CHECK(M.theme == 2 && !plat_fresh());  // saved before the restart
  // none from another format, nor with one missing
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  settings_load();
  M.theme = 0;
  snap[0]++;
  for (int i = 0; i < k; i++)
    number_from_esp(K_SETTINGS, snap[i], false);
  snap[0]--;
  for (int i = 0; i < k; i++)
    if (i != theme_at)
      number_from_esp(K_SETTINGS, snap[i], false);
  CHECK(M.theme == 0 && stub_resets == resets + 1);
  plat_save_now();
  settings_load();  // with settings again, for what follows
  // a toggle's words and a text share their room: each only takes its own, and a new type starts empty
  uint8_t def[ITEM_DEF_LEN] = {ITEM_TOGGLE};
  item_t *it = &M.items[1];
  from_esp(K_ITEM_DEF + 1, def, sizeof def, false);
  from_esp(K_ITEM_ON + 1, (const uint8_t *) "Open", 4, false);
  from_esp(K_ITEM_TEXT + 1, (const uint8_t *) "Washing", 7, false);
  CHECK(!strcmp(it->on, "Open") && !it->off[0]);
  def[0] = ITEM_TEXT;
  from_esp(K_ITEM_DEF + 1, def, sizeof def, false);
  CHECK(!it->text[0]);
  from_esp(K_ITEM_ON + 1, (const uint8_t *) "Open", 4, false);
  from_esp(K_ITEM_TEXT + 1, (const uint8_t *) "Washing", 7, false);
  CHECK(!strcmp(it->text, "Washing"));
  from_esp(K_ITEM_DEF + 1, def, sizeof def, false);  // the same type again keeps it
  CHECK(!strcmp(it->text, "Washing"));
  // the setpoint goes to the ESP, but not while it's offline
  M.esp_online = false;
  val_set(V_SETPOINT, 230);
  CHECK(!queued(K_SETPOINT, 230));
  M.esp_online = true;
  val_set(V_SETPOINT, 235);
  CHECK(queued(K_SETPOINT, 235));
  // A picture's last ack waits while the queue is full, the ESP hasn't heard the panel's answers
  while (plink_room(link_queue()))
    link_action(A_NONE);
  uint8_t bad = 9;
  from_esp(K_PIC_BEGIN, &bad, 1, false);
  uint32_t ack = (uint32_t) PIC_BAD << 24 | 9u << 16;
  CHECK(!queued(K_PIC_ACK, ack));
  from_esp(0, &bad, 0, true);  // now it has, there's room
  link_poll();
  CHECK(queued(K_PIC_ACK, ack));

  // Item help: not asked while the ESP is away, only the latest waits, asked again if it didn't fit
  link_init();
  M.esp_version = PLINK_VERSION;
  M.esp_online = false;
  CHECK(!link_item_help(1) && !queued(K_ITEM_HELP, 1));
  M.esp_online = true;
  link_item_help(1);
  link_item_help(2);
  CHECK(queued(K_ITEM_HELP, 2) && !queued(K_ITEM_HELP, 1));
  link_init();
  M.esp_version = PLINK_VERSION;
  while (plink_room(link_queue()))
    link_action(A_NONE);
  link_item_help(3);
  CHECK(!queued(K_ITEM_HELP, 3));
  link_poll();  // some go out
  link_item_help(3);
  CHECK(queued(K_ITEM_HELP, 3));

  // ESP restart: our HELLO goes ahead of the waiting values, even with a full queue
  link_init();
  M.esp_version = PLINK_VERSION;
  link_poll();                   // our own HELLO is in the answer
  const uint16_t spare = 0x0D0;  // keys no message has, so they're values
  for (uint16_t k = spare; plink_room(link_queue()); k++)
    link_send(k, 1);
  uint8_t d[512];  // and its animations are gone with it, it sends them again
  uint32_t n = anim_records(d, 5);
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK, false);
  CHECK(link_queue()->key[0] == K_HELLO && link_queue()->value[0] == (uint32_t) PLINK_VERSION << 16);
  CHECK(queued(spare, 1) && link_queue()->qn == PLINK_QUEUE && !placed() && !anim_used());
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);  // also after the answer to ours
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16, false);
  CHECK(!anim_used());
  // An animation shows and goes on its own, the screen isn't built again for it
  saver_builds = 0;
  CHECK(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  uint8_t at[10] = {0};
  from_esp(K_ANIM_PLACE, at, sizeof at, false);
  number_from_esp(K_ANIM_CLEAR, ANIM_PLACE_BIT | 0, false);
  number_from_esp(K_ANIM_CLEAR, ANIM_ALL, false);
  CHECK(!saver_builds && !anim_used());
  // A CX60 value in the ignored answer goes again unless a newer one waits. Item values don't.
  link_init();
  M.esp_version = PLINK_VERSION;
  link_send(K_SETPOINT, 215);
  link_send(K_FAN_NORM_SUP, 50);
  link_send(K_ITEM_VALUE + 1, 7);
  link_poll();  // they're in the answer
  link_send(K_FAN_NORM_SUP, 60);
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK, false);
  CHECK(link_queue()->key[0] == K_HELLO && queued(K_SETPOINT, 215) && queued(K_FAN_NORM_SUP, 60));
  CHECK(!queued(K_FAN_NORM_SUP, 50) && !queued(K_ITEM_VALUE + 1, 7));

  // ESP switched off: offline at once, and until it's heard again
  M.esp_online = true;
  M.features &= ~FBIT(FEAT_ESP);
  link_second();
  CHECK(!M.esp_online);
  M.features |= FBIT(FEAT_ESP);
  link_second();
  CHECK(!M.esp_online);
  from_esp(0, &bad, 0, false);
  link_second();
  CHECK(M.esp_online);
}

int main(void) {
  sim_display_init();
  anim_init();
  test_picture();
  test_picture_parts();
  test_jpeg();
  test_deflate();
  test_anim();
  test_anim_slots();
  test_anim_move();
  test_anim_ease();
  test_anim_turn();
  test_anim_look();
  test_anim_rounds();
  test_anim_paths();
  test_anim_wave();
  test_anim_map();
  test_anim_frames();
  test_anim_redraw();
  test_fonts();
  test_formats();
  test_values();
  test_items();
  test_clock();
  test_mbserver();
  test_bus_burst();
  test_bus_clock();
  test_settings();
  test_runtime();
  test_deice_saved();
  test_leds();
  test_schedule();
  test_input();
  test_unit();
  test_advanced();
  test_link();
  test_link_picture();
  char why[96] = "";
  if (menu_check(why, sizeof why)) {
    printf("menus: %s\n", why);
    fails++;
  }
  printf("unit tests: %s\n", fails ? "FAILED" : "ok");
  return fails != 0;
}
