// Unit tests. The firmware's hw/ code runs on board_stub.c.
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
#include "ui.h"
#include "unit.h"
#include <stdio.h>
#include <string.h>

model_t M;
extern uint32_t stub_ms, stub_rtc;
extern uint8_t stub_flash[];
extern int stub_flash_writes_left, stub_flash_erases;
extern bool stub_bus_quiet;
static int fails;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("%s:%d: %s\n", __FILE__, __LINE__, #c); \
      fails++; \
    } \
  } while (0)

// ---------------------------------------------------------------- picture
#define W 10  // pixels, so 5 bytes per row
#define H 6
static uint8_t px[H][W / 2];  // the expected picture, 2 pixels per byte

static uint32_t tint;  // XORed into every palette colour, see rgb()
static uint32_t rgb(int i) { return ((uint32_t) (i * 0x0F0F0F) ^ 0x204060) ^ tint; }

// PackBits for one row, a run where 2+ bytes repeat, otherwise single-byte literals
static unsigned pack_row(const uint8_t *row, uint8_t *out) {
  unsigned n = 0;
  for (unsigned i = 0; i < W / 2;) {
    unsigned k = 1;
    while (i + k < W / 2 && row[i + k] == row[i])
      k++;
    if (k >= 2) {
      out[n++] = (uint8_t) (k + 126);
      out[n++] = row[i];
      i += k;
    } else {
      out[n++] = 0;
      out[n++] = row[i++];
    }
  }
  return n;
}

static unsigned pack(int y, int count, uint8_t *out) {
  unsigned n = 0;
  for (int r = y; r < y + count; r++)
    n += pack_row(px[r], out + n);
  return n;
}

static uint8_t status(int32_t ack) { return ack < 0 ? 0xFF : (uint8_t) (ack >> 24); }

static uint8_t id;  // id of the last transfer

static void put16(uint8_t *b, unsigned v) {
  b[0] = (uint8_t) v;
  b[1] = (uint8_t) (v >> 8);
}

static void put32(uint8_t *b, uint32_t v) {
  put16(b, v & 0xFFFF);
  put16(b + 2, v >> 16);
}

static void palette(uint8_t *b) {
  for (int i = 0; i < 16; i++, b += 3) {
    b[0] = (uint8_t) (rgb(i) >> 16);
    b[1] = (uint8_t) (rgb(i) >> 8);
    b[2] = (uint8_t) rgb(i);
  }
}

// BEGIN: id, format, w, h, len, palette
static int32_t begin(int w, int h, uint32_t len) {
  uint8_t b[58] = {++id, PIC_PALETTE};
  put16(b + 2, (unsigned) w);
  put16(b + 4, (unsigned) h);
  put32(b + 6, len);
  palette(b + 10);
  return pic_msg(K_PIC_BEGIN, b, sizeof b);
}

// PATCH: id, y, count, len, then the palette if pal
static int32_t patch(int y, int count, uint32_t len, bool pal) {
  uint8_t b[57] = {++id};
  put16(b + 1, (unsigned) y);
  put16(b + 3, (unsigned) count);
  put32(b + 5, len);
  palette(b + 9);
  return pic_msg(K_PIC_PATCH, b, pal ? 57 : 9);
}

static int32_t end(uint32_t crc) {
  uint8_t b[4];
  put32(b, crc);
  return pic_msg(K_PIC_END, b, 4);
}

static int32_t data(const uint8_t *b, unsigned n) { return pic_msg(K_PIC_DATA, b, (int) n); }

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

static int32_t send_picture(unsigned piece) {
  uint8_t b[512];
  unsigned n = pack(0, H, b);
  return rest(begin(W, H, n), b, n, piece, plink_crc32(0, b, n));
}

static int32_t send_patch(int y, int count, bool pal) {
  uint8_t b[512];
  unsigned n = pack(y, count, b);
  return rest(patch(y, count, n, pal), b, n, 7, plink_crc32(0, b, n));
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
      px[r][i] = (uint8_t) (i < 2 ? seed + r : (seed + r) * 17 + i);  // a repeat, then literals
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
  uint8_t b[512];
  unsigned n = pack(0, 2, b);
  patch(0, 2, n, false);
  data(b, n / 2);
  fill(0, H, 40);
  CHECK(status(send_picture(64)) == PIC_DONE);
  CHECK(shows());

  // a bad patch with a run across a row end is refused and leaves the picture alone
  uint8_t bad[] = {126 + 7, 0x11, 0, 0x22, 0, 0x33, 0, 0x44};  // a 7-byte repeat in a 5-byte row
  CHECK(status(rest(patch(1, 1, sizeof bad, false), bad, sizeof bad, 8, plink_crc32(0, bad, sizeof bad))) == PIC_BAD);
  CHECK(shows());
  // and so are bytes after the last row
  n = pack(1, 1, b);
  b[n] = 0;
  b[n + 1] = 0x55;
  CHECK(status(rest(patch(1, 1, n + 2, false), b, n + 2, 8, plink_crc32(0, b, n + 2))) == PIC_BAD && shows());
  CHECK(status(rest(patch(1, 0, 2, false), b, 2, 8, plink_crc32(0, b, 2))) == PIC_BAD && shows());
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
  CHECK(status(rest(patch(1, 1, n, false), b, n, 4, 12345)) == PIC_BAD_CRC);
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
  pic_msg(K_PIC_CLEAR, NULL, 0);
  CHECK(!shows());
}

// ---------------------------------------------------------------- JPEG pictures
#define JW 48
#define JH 24

static uint32_t photo(void *ctx, int x, int y) {  // soft gradients with grain
  (void) ctx;
  return (uint32_t) (40 + x * 4 + (x * y) % 7) << 16 | (uint32_t) (60 + y * 7) << 8 | (uint32_t) (200 - x - y);
}

static int32_t jpeg_begin(int w, int h, uint32_t len) {  // BEGIN: id, format, w, h, len
  uint8_t b[10] = {++id, PIC_JPEG};
  put16(b + 2, (unsigned) w);
  put16(b + 4, (unsigned) h);
  put32(b + 6, len);
  return pic_msg(K_PIC_BEGIN, b, sizeof b);
}

static int32_t send_jpeg(const uint8_t *j, uint32_t n, int w, int h) {
  return rest(jpeg_begin(w, h, n), j, n, 61, plink_crc32(0, j, n));
}

static void test_jpeg(void) {
  static uint8_t j[8192];
  pjpeg_t e;
  pjpeg_begin(&e, JW, JH, 90, j, sizeof j);
  while (pjpeg_row(&e, photo, NULL)) {
  }
  uint32_t n = pjpeg_end(&e);
  CHECK(n > 600 && pjpeg_mse(&e) > 0 && pjpeg_mse(&e) < 20);
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
  lv_area_t outside = {0, 0, JW, 0};
  CHECK(!pic_read(&outside, part, JW));
  CHECK(status(patch(0, 1, 4, false)) == PIC_NO_PICTURE);  // only a 16-colour picture takes patches

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

// ---------------------------------------------------------------- the animation
#define AW 8
#define AH 4
static uint8_t an[4][AH][AW / 2];  // the frames, colour 15 is transparent

// ANIM_BEGIN: id, w, h, scale, x, y, transparent colour, len, palette. Then the data.
static int32_t anim_send(int w, int x, uint32_t len, const uint8_t *data, uint32_t crc) {
  uint8_t h[63] = {++id};
  put16(h + 1, (unsigned) w);
  put16(h + 3, AH);
  h[5] = 2;
  put16(h + 6, (unsigned) x);
  put16(h + 8, 10);
  h[10] = 15;
  put32(h + 11, len);
  palette(h + 15);
  return rest(pic_msg(K_ANIM_BEGIN, h, sizeof h), data, len, 5, crc);
}

// Whether the animation's decoder gives frame f, drawn at 2x.
static bool anim_shows(lv_obj_t *img, int f) {
  lv_img_decoder_dsc_t dsc;
  if (lv_img_decoder_open(&dsc, lv_img_get_src(img), lv_color_black(), 0) != LV_RES_OK)
    return false;
  bool same = dsc.header.w == AW * 2 && dsc.header.h == AH * 2 && dsc.header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA;
  for (int y = 0; y < AH * 2 && same; y++) {
    uint8_t line[AW * 2 * LV_IMG_PX_SIZE_ALPHA_BYTE];
    lv_img_decoder_read_line(&dsc, 0, (lv_coord_t) y, AW * 2, line);
    for (int x = 0; x < AW * 2; x++) {
      uint8_t b = an[f][y / 2][x / 4], c = x / 2 & 1 ? b & 15 : b >> 4, *p = &line[x * LV_IMG_PX_SIZE_ALPHA_BYTE];
      lv_color_t want = lv_color_hex(rgb(c));
      same &= c == 15 ? p[2] == LV_OPA_TRANSP : p[2] == LV_OPA_COVER && !memcmp(p, &want, sizeof want);
    }
  }
  lv_img_decoder_close(&dsc);
  return same;
}

static void test_anim(void) {
  memset(an, 0xFF, sizeof an);
  an[0][1][1] = 0x12;  // a dot that moves, then stays
  an[1][2][2] = 0x34;
  an[2][2][2] = 0x34;
  an[3][0][0] = 0x56;
  uint8_t d[512];
  uint32_t n = 0;
  for (int f = 0; f <= 4; f++)  // frame 0 whole, the last record back to frame 0
    n += plink_anim_record(f ? &an[f - 1][0][0] : NULL, &an[f % 4][0][0], AW, AH, (uint8_t) (5 + f), d + n);
  CHECK(status(anim_send(AW, 0, n, d, plink_crc32(0, d, n))) == PIC_DONE && anim_ready());
  lv_obj_t *img = anim_show(lv_scr_act());
  CHECK(img && anim_shows(img, 0));
  sim_run(50);  // the first frame for 50 ms, the next for 60, ...
  CHECK(anim_shows(img, 1));
  sim_run(60);
  CHECK(anim_shows(img, 2));  // same as frame 1, so a record with no rows
  sim_run(70);
  CHECK(anim_shows(img, 3));
  sim_run(80);
  CHECK(anim_shows(img, 0));  // and back to the start
  lv_obj_del(img);

  // bad sizes, then bad data
  CHECK(status(anim_send(AW - 1, 0, n, d, 0)) == PIC_BAD_SIZE && !anim_ready());
  CHECK(status(anim_send(AW, PIC_MAX_W - AW, n, d, 0)) == PIC_BAD_SIZE);
  CHECK(status(anim_send(AW, 0, ANIM_POOL, d, 0)) == PIC_TOO_BIG);
  CHECK(status(anim_send(AW, 0, UINT32_MAX, d, 0)) == PIC_TOO_BIG);  // doesn't wrap round to fit
  uint8_t big[63] = {++id};                                          // a frame bigger than the pool
  put16(big + 1, 160);
  put16(big + 3, 100);
  big[5] = 1;
  big[10] = ANIM_OPAQUE;
  put32(big + 11, 4);
  CHECK(status(pic_msg(K_ANIM_BEGIN, big, sizeof big)) == PIC_TOO_BIG);
  uint8_t bad[sizeof d];
  memcpy(bad, d, n);
  bad[4] = AH + 1;
  CHECK(status(anim_send(AW, 0, n, bad, plink_crc32(0, bad, n))) == PIC_BAD);
  CHECK(status(anim_send(AW, 0, n, d, 12345)) == PIC_BAD_CRC);
  uint8_t five[5] = {0x11, 0x11, 0x11, 0x11, 0x11}, over[5 + PDEFLATE_BOUND(5)] = {5, 0, 0, 4, 1};
  uint32_t on = 5 + pdeflate(five, 5, 5, 5, over + 5, PDEFLATE_BOUND(5));  // 5 bytes for a rectangle of 4
  CHECK(status(anim_send(AW, 0, on, over, plink_crc32(0, over, on))) == PIC_BAD);
  CHECK(status(anim_send(AW, 0, n - 1, d, plink_crc32(0, d, n - 1))) == PIC_BAD);  // the last record cut short
  // A picture coming in cancels an animation coming in. A finished animation stays until the ESP clears it.
  CHECK(status(anim_send(AW, 0, n, d, plink_crc32(0, d, n))) == PIC_DONE);
  uint8_t h[63] = {++id};
  put16(h + 1, AW);
  put16(h + 3, AH);
  h[5] = 1;
  h[10] = 15;
  put32(h + 11, n);
  CHECK(pic_msg(K_ANIM_BEGIN, h, sizeof h) == -1 && data(d, n / 2) == -1);
  fill(0, H, 5);
  CHECK(status(send_picture(16)) == PIC_DONE && shows() && !anim_ready());
  CHECK(data(d + n / 2, n - n / 2) == -1 && end(plink_crc32(0, d, n)) == -1);  // the rest is no longer expected
  CHECK(status(anim_send(AW, 0, n, d, plink_crc32(0, d, n))) == PIC_DONE && anim_ready());
  pic_msg(K_ANIM_CLEAR, NULL, 0);
  CHECK(!anim_ready() && shows());
}

// ---------------------------------------------------------------- deflate
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
  CHECK(pinflate(DYN, sizeof DYN, out, N, N, N) == sizeof DYN && !memcmp(out, TXT, N));
  CHECK(pinflate(FIX, sizeof FIX, out, N, N, N) == sizeof FIX && !memcmp(out, TXT, N));
  CHECK(pinflate(stored, sizeof stored, out, N, N, N) == sizeof stored && !memcmp(out, TXT, N));
  CHECK(pinflate(DYN, sizeof DYN, NULL, N, N, N) == sizeof DYN);  // only checking
  // wrong length, cut short and a damaged code are refused
  CHECK(!pinflate(DYN, sizeof DYN, out, N, N, N - 1) && !pinflate(DYN, sizeof DYN, out, N, N, N + 1));
  CHECK(!pinflate(DYN, sizeof DYN - 1, out, N, N, N) && !pinflate(stored, sizeof stored - 1, out, N, N, N));
  stored[3] ^= 1;
  CHECK(!pinflate(stored, sizeof stored, out, N, N, N));

  // our own round trip, also from and into a rectangle of a bigger buffer
  uint8_t big[40][24], back[40][24], c[PDEFLATE_BOUND(sizeof big)];
  for (int y = 0; y < 40; y++)
    for (int x = 0; x < 24; x++)
      big[y][x] = (uint8_t) ((x / 3 + y / 5) % 4 * 0x11);
  uint32_t k = pdeflate(&big[0][0], sizeof big, sizeof big, sizeof big, c, sizeof c);
  CHECK(k > 0 && k < sizeof big / 4 && pinflate(c, k, &back[0][0], sizeof back, sizeof back, sizeof back) == k);
  CHECK(!memcmp(big, back, sizeof big));
  memset(back, 0xAB, sizeof back);
  k = pdeflate(&big[3][5], 10, 24, 10 * 20, c, sizeof c);  // 10 x 20 from (5, 3)
  CHECK(k > 0 && pinflate(c, k, &back[7][2], 10, 24, 10 * 20) == k);
  bool same = true;
  for (int y = 0; y < 40; y++)
    for (int x = 0; x < 24; x++)
      same &= back[y][x] == (y >= 7 && y < 27 && x >= 2 && x < 12 ? big[y - 4][x + 3] : 0xAB);
  CHECK(same);
  CHECK(pdeflate(&big[0][0], sizeof big, sizeof big, sizeof big, c, 10) == 0);  // doesn't fit
}

// ---------------------------------------------------------------- number and time formatting
static void test_formats(void) {
  char b[24];
  CHECK(!strcmp(fmt_dec(b, 12345, 3), "12.345"));
  CHECK(!strcmp(fmt_dec(b, -12345, 2), "-123.45"));
  CHECK(!strcmp(fmt_dec(b, -5, 1), "-0.5"));
  CHECK(!strcmp(fmt_dec(b, 7, 0), "7"));
  CHECK(!strcmp(fmt_dec(b, INT32_MIN, 1), "-214748364.8"));
  M.clock_24h = false;
  CHECK(!strcmp(fmt_hm(b, 0), "12:00 am") && !strcmp(fmt_hm(b, 13 * 60 + 5), "1:05 pm"));
  CHECK(!strcmp(fmt_hm(b, 1440), "12:00 am"));
  M.clock_24h = true;
  CHECK(!strcmp(fmt_hm(b, 13 * 60 + 5), "13:05") && !strcmp(fmt_hm(b, 1440), "24:00"));
  // a period's start: 0..1435 in 15-minute steps
  CHECK(step_value(1425, 1, 15, 0, 1435) == 1435 && step_value(1435, -1, 15, 0, 1435) == 1425);
  CHECK(step_value(0, -1, 15, 0, 1435) == 0 && step_value(960, 1, 15, 5, 1440) == 975);
  CHECK(step_value(-10, 3, 5, -90, 90) == 5 && step_value(-88, -1, 5, -90, 90) == -90);
}

// ---------------------------------------------------------------- values
static void test_values(void) {
  model_defaults();
  for (int v = V_NONE + 1; v < V_COUNT; v++) {
    val_def_t d = val_def((val_t) v);
    CHECK(d.min <= d.max);
    if (d.key)
      CHECK((int) val_by_key(d.key) == v);  // keys must be unique, they identify values on the link and in flash
    if ((d.flags & (VF_CU60 | VF_CMD)) == VF_CU60)
      CHECK((int) val_by_cu60(val_cu60_reg((val_t) v)) == v);
  }
  val_set(V_BRIGHTNESS, 150);
  CHECK(M.brightness == 100);  // clamped
  val_set(V_ROOM_TEMP, 5);
  CHECK(M.t_room == T_NONE);  // read-only
  val_set_remote(V_BUS_ADDR, 2);
  CHECK(M.bus_addr == 3);  // the ESP may only read it
  val_load(V_THEME, 9);
  CHECK(M.theme == N_THEMES - 1);  // loaded from flash, also clamped
  val_set_remote(V_FAN_MIN_SUP, 55);
  CHECK(M.fan_pct[0][0] == 55);
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
  CHECK(!strcmp(item_text(b, button, true), _(S_PRESS)));

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
  CHECK(!strcmp(item_text(b, co2, true), "\xE2\x80\x93") && !item_pct(co2) && item_color(co2).full == T.accent.full);
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
  stub_ms += 6000;  // the flag is saved like a change on the keys, so a restart soon after knows
  settings_poll();
  CHECK(stub_flash_erases == erases + 1);
  M.dst_now = true;  // a restart before it was saved: 02:xx comes twice, so the old flag stands and nothing moves
  clock_follow(clock_to_unix());
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
  clock_follow(t);  // from the RTC at boot, or the CU60
  CHECK(M.dst_now && clock_to_unix() == t);
  M.dst_now = true;  // and the other way, a summer flag in winter
  clock_from_unix(1792328640u);
  clock_second();
  CHECK(M.dst_now && clock_to_unix() == 1792328640u);
  clock_follow(1792897200u);  // 03:00 on 25 October is standard time
  CHECK(!M.dst_now);
  clock_follow(1792893600u + 3600 + 1800);  // 03:30 that day: still is
  CHECK(!M.dst_now);
  clock_follow(1792895400u);  // 02:30 that day could be either, the flag stays
  CHECK(!M.dst_now);
  M.dst_now = true;
  clock_follow(1792895400u);
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
static int frame(uint8_t *f, int n) {  // appends the CRC
  uint16_t c = mb_crc(f, n);
  f[n] = c & 0xFF;
  f[n + 1] = c >> 8;
  return n + 2;
}

static void test_mbserver(void) {
  uint8_t q[300], r[MB_MAX_FRAME];
  mb_init(3, FW_VERSION, FW_PATCH);
  // a frame longer than any reply isn't answered, the reply would overflow
  memset(q, 0, sizeof q);
  q[0] = 3, q[1] = 0x64;
  CHECK(mb_frame(q, frame(q, 280), r) == 0);
  // the bootloader request is only accepted exactly as the updaters send it
  uint8_t enter[8] = {3, 0x64, 2, 0};
  CHECK(mb_frame(enter, frame(enter, 4), r) == 6 && MB.enter_bootloader);
  MB.enter_bootloader = false;
  uint8_t other[8] = {3, 0x64, 3, 0};
  CHECK(mb_frame(other, frame(other, 4), r) == 0 && !MB.enter_bootloader);
  // write coils with a byte count that doesn't match the coil count
  uint8_t coils[16] = {3, 0x0F, 0, 0, 0, 20, 1, 0xFF};
  CHECK(mb_frame(coils, frame(coils, 8), r) == 5 && r[1] == 0x8F);
  // only an actual change is reported to the main loop
  uint8_t w[8] = {0, 0x06, 0, 0x0C, 0, 200};
  uint16_t reg;
  mb_frame(w, frame(w, 6), r);
  CHECK(mb_take_changed(&reg) && reg == 0x0C && !mb_take_changed(&reg));
  mb_frame(w, frame(w, 6), r);
  CHECK(!mb_take_changed(&reg));  // same value again
  // discovery reads the input registers: our mark, the patch level and the version
  uint8_t disc[8] = {3, 0x04, 0, 0, 0, 5};
  CHECK(mb_frame(disc, frame(disc, 6), r) == 15 && (r[3] << 8 | r[4]) == FW_TYPE && (r[5] << 8 | r[6]) == 0);
  CHECK((r[7] << 8 | r[8]) == FW_PATCH && (r[9] << 8 | r[10]) == FW_VERSION);
  // Fan down twice, NORMAL -> MIN -> STOP. The CU60 has read MIN when STOP comes, and its relay of MIN mustn't
  // undo STOP.
  mb_write(0x000, 1);
  uint8_t rd[8] = {3, 0x03, 0, 0, 0, 1};
  CHECK(mb_frame(rd, frame(rd, 6), r) == 7 && r[4] == 1);
  mb_write(0x000, 0);
  uint8_t relay[8] = {0, 0x65, 0, 0, 0, 1};
  mb_frame(relay, frame(relay, 6), r);
  CHECK(MB.hold[0] == 0 && (MB.pending[0] & 1));  // the coil stays set for the CU60 to read STOP
  CHECK(mb_frame(rd, frame(rd, 6), r) == 7 && r[4] == 0);
  relay[5] = 0;
  mb_frame(relay, frame(relay, 6), r);
  CHECK(MB.hold[0] == 0 && !(MB.pending[0] & 1));
}

// ---------------------------------------------------------------- the CU60's clock, which the panels keep
static void relay(uint16_t reg, uint16_t v) {  // the CU60 passing a panel's change on to all panels
  uint8_t q[8] = {0, 0x65, (uint8_t) (reg >> 8), (uint8_t) reg, (uint8_t) (v >> 8), (uint8_t) v}, r[MB_MAX_FRAME];
  mb_frame(q, frame(q, 6), r);
}

static void block(uint16_t reg, const uint16_t *v, int n) {  // an FC16 broadcast, the CU60's own blocks
  uint8_t q[MB_MAX_FRAME] = {0, 0x10, (uint8_t) (reg >> 8), (uint8_t) reg, 0, (uint8_t) n, (uint8_t) (2 * n)};
  uint8_t r[MB_MAX_FRAME];
  for (int i = 0; i < n; i++) {
    q[7 + 2 * i] = (uint8_t) (v[i] >> 8);
    q[8 + 2 * i] = (uint8_t) v[i];
  }
  mb_frame(q, frame(q, 7 + 2 * n), r);
}

static void cu60(uint16_t reg, uint16_t v) { block(reg, &v, 1); }  // one register of a status block

static bool coil(uint16_t reg) { return MB.pending[reg / 32] >> (reg % 32) & 1; }

static bool clock_coils(void) { return coil(0x94) && coil(0x95); }

static void test_bus_clock(void) {
  model_defaults();
  unit_init();
  uint32_t t = 1792328640u;  // 2026-10-18 13:04:00
  // written at the start of each minute
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
  clock_from_unix(t + 59);
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
  // The settings block the CU60 sends when it starts has the clock it saved at its last power cut. Ours stays,
  // unless we have none.
  uint16_t old[2] = {(uint16_t) ((t2 - 7200) >> 16), (uint16_t) (t2 - 7200)};
  block(0x94, old, 2);
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK(stub_rtc == t2 && clock_to_unix() == t2);
  stub_rtc = 0;
  clock_from_unix(0);
  old[1]--;
  block(0x94, old, 2);
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK(stub_rtc == t2 - 7201);
}

// ---------------------------------------------------------------- settings in the RAM flash, with power cuts
static void test_settings(void) {
  model_defaults();
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  settings_load();
  M.brightness = 42;
  settings_touch(true);
  settings_flush();
  model_defaults();
  settings_load();
  CHECK(M.brightness == 42);
  // power fails while saving, the previous save is still there
  M.brightness = 55;
  settings_touch(true);
  stub_flash_writes_left = 3;
  settings_flush();
  stub_flash_writes_left = -1;
  model_defaults();
  settings_load();
  CHECK(M.brightness == 42);
  // the next save works, and a save with no changes erases nothing
  M.brightness = 55;
  settings_touch(true);
  settings_flush();
  int erases = stub_flash_erases;
  settings_touch(true);
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
    settings_touch(true);
    settings_flush();
  }
  for (int i = 0; i < FLASH_SETTINGS_PAGES; i++)
    CHECK(*(const uint32_t *) (stub_flash + i * FLASH_PAGE) == 0x32534943u);  // each holds a copy
  model_defaults();
  settings_load();
  CHECK(M.brightness == 10 + 2 * FLASH_SETTINGS_PAGES - 1);

  // a change on the keys is saved 5 s after the last one, others at most once an hour
  erases = stub_flash_erases;
  M.brightness = 70;
  settings_touch(false);
  stub_ms += 6000;
  settings_poll();
  CHECK(stub_flash_erases == erases);  // written less than an hour ago
  stub_ms += 3600 * 1000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 1);
  M.brightness = 71;
  settings_touch(false);
  stub_ms += 6000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 1);
  M.brightness = 72;
  settings_touch(true);  // on the keys, saves this one and the one waiting
  stub_ms += 1000;
  settings_poll();
  CHECK(stub_flash_erases == erases + 1);  // not before 5 s
  stub_ms += 5000;
  stub_bus_quiet = false;
  settings_poll();
  CHECK(stub_flash_erases == erases + 1);  // nor while the bus is busy, an erase stalls the bus interrupt
  stub_bus_quiet = true;
  settings_poll();
  CHECK(stub_flash_erases == erases + 2);
  model_defaults();
  settings_load();
  CHECK(M.brightness == 72);
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
  settings_touch(true);
  for (int s = 0; s < 7; s++) {
    rt[17]++;
    block(0x14C, rt, 20);
    unit_poll();
    stub_ms += 1000;
    settings_poll();
  }
  CHECK(stub_flash_erases == erases + 1);
  // after a restart the counters are back from flash, the CU60 only sends the high words when it starts
  model_defaults();
  unit_init();
  settings_load();
  unit_poll();
  CHECK(M.h_stop == 18 && M.h_total == 91 && M.brightness == 33);
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
static int layout_changes;  // see --wrap in the Makefile
void __wrap_ui_layout_changed(void) { layout_changes++; }

static period_t period(int start, int end, int idx) {
  return (period_t) {.start_h = (uint8_t) (start / 60),
                     .start_m = (uint8_t) (start % 60),
                     .end_h = (uint8_t) (end / 60),
                     .end_m = (uint8_t) (end % 60),
                     .level = MODE_NORMAL,
                     .idx = (uint8_t) idx,
                     .setpoint = 200};
}

static void test_schedule(void) {
  model_defaults();
  mb_init(3, FW_VERSION, FW_PATCH);
  // a new period is 08:00-16:00 if that's free, else the first free time
  CHECK(sched_edit(0, -1) && M.n_periods[0] == 1 && M.period.start == 8 * 60 && M.period.end == 16 * 60);
  const period_t day[3] = {period(390, 480, 0), period(480, 930, 1), period(930, 1350, 2)};
  sched_store(1, day, 3);
  CHECK(sched_edit(1, -1) && M.period.idx == 3 && M.period.start == 0 && M.period.end == 390);
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
  CHECK(sched_edit(3, -1) && M.period.start == 480 && M.period.end == 486);
  sched_range(V_PERIOD_START, &lo, &hi);
  CHECK(lo == 480 && hi == 481);
  // an edit stays between its neighbours, 08:00-15:30 between 06:30-08:00 and 15:30-22:30
  sched_store(1, day, 3);
  CHECK(sched_edit(1, 1));
  val_set(V_PERIOD_START, 7 * 60);
  val_set(V_PERIOD_END, 1440);
  CHECK(M.period.start == 480 && M.period.end == 930);
  val_set(V_PERIOD_START, 1000);  // pushes the end along, as far as the next one
  CHECK(M.period.start == 925 && M.period.end == 930);
  val_set(V_PERIOD_END, 0);
  CHECK(M.period.start == 480 && M.period.end == 485 && M.week[1][1].end_m == 5);
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
static int keys[64], n_keys, idles, wakes, toasts;
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
  CHECK(M.key_lock && n_keys == 0);
  toasts = 0;
  hold(IN_OK, 50, 0);
  CHECK(n_keys == 0 && toasts == 1);
  hold(IN_HELP, 3100, 0);
  CHECK(!M.key_lock);
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
  uint8_t f[PBULK_FRAME_MAX] = {PBULK_ADDR, PBULK_FC_SEND, (uint8_t) (3 + n)}, a[PBULK_ANSWER_FRAME_MAX];
  put16(f + PBULK_ESESS, 0xE5);
  put16(f + PBULK_NUM, esp_num++);
  put16(f + PBULK_PSESS, ack ? p->psess : 0);
  put16(f + PBULK_PNEXT, p->pnum);
  put16(f + PBULK_PAYLOAD, key);
  f[PBULK_PAYLOAD + 2] = (uint8_t) n;
  memcpy(f + PBULK_PAYLOAD + 3, b, (size_t) n);
  put16(f + PBULK_PAYLOAD + 3 + n, pbus_crc16(f, PBULK_PAYLOAD + 3 + n));
  link_bulk_frame(f, PBULK_FRAME(3 + n), 0, 0, a);
  link_poll();
}

static void number_from_esp(uint16_t key, uint32_t v, bool ack) {
  uint8_t b[4];
  put32(b, v);
  from_esp(key, b, 4, ack);
}

static bool queued(uint16_t key, uint32_t value) {
  const plink_t *l = link_queue();
  for (int i = 0; i < l->qn; i++)
    if (l->q[i].key == key && l->q[i].value == value)
      return true;
  return false;
}

// ---------------------------------------------------------------- the CU60's registers, in and out of the model
static void test_unit(void) {
  model_defaults();
  unit_init();
  toasts = 0;
  // The CU60 ORs the heater flags of the panels present, so off has to clear all three and on only sets ours.
  // What it made of them, 0x10E, is what's shown.
  cu60(0x10E, 1);
  unit_poll();
  CHECK(M.heater_enabled);
  val_set(V_HEATER, 0);
  CHECK(!MB.hold[0x13F] && !MB.hold[0x140] && !MB.hold[0x141] && coil(0x13F) && coil(0x140) && coil(0x141));
  for (uint16_t r = 0x13F; r <= 0x141; r++)
    relay(r, 0);
  cu60(0x10E, 0);
  unit_poll();
  CHECK(!M.heater_enabled);
  val_set(V_HEATER, 1);
  CHECK(M.heater_enabled && MB.hold[0x141] == 1 && coil(0x141) && !coil(0x13F) && !coil(0x140));
  relay(0x141, 1);
  for (int s = 0; s < 3; s++) {  // our flag is one vote, and the unit didn't take it: no heater, say
    cu60(0x0C3, (uint16_t) (200 + s));
    unit_poll();
    stub_ms += 1000;
    unit_second();
  }
  CHECK(!M.heater_enabled);
  cu60(0x10E, 1);
  unit_poll();
  CHECK(M.heater_enabled);

  // The mode set and the one running. A boost runs MAX while NORMAL is set, and Boost -> Max on the overlay
  // then writes MAX all the same.
  cu60(0x000, MODE_NORMAL);
  cu60(0x0BF, MODE_MAX);
  cu60(0x0C1, 600);
  unit_poll();
  CHECK(M.mode == MODE_NORMAL && M.mode_now == MODE_MAX && M.boost_on && M.boost_left == 10);
  val_set(V_BOOST, 0);
  val_set(V_MODE, MODE_MAX);
  CHECK(coil(0x13B) && MB.hold[0x000] == MODE_MAX && coil(0x000));
  relay(0x13B, 1);
  relay(0x000, MODE_MAX);
  cu60(0x0C1, 0);
  unit_poll();
  CHECK(!M.boost_on && !M.boost_left);

  // the setpoint in effect is 0xC2, 0xBE is the one set
  cu60(0x0BE, 210);
  cu60(0x0C2, 190);
  unit_poll();
  CHECK(M.setpoint_now == 190 && M.setpoint == 210);

  // extract regulation needs the extract sensor switched on, which the stock panel does along with it
  cu60(0x114, REG_SUPPLY);
  unit_poll();
  val_set(V_REGULATION, REG_EXTRACT);
  CHECK(MB.hold[0x114] == REG_EXTRACT && coil(0x114) && MB.hold[0x136] == 1 && coil(0x136));

  // The CU60's own block changes the week program setting. A stock panel on the bus writes 0x117 as its periods
  // start and end, and its relays don't.
  val_set(V_WEEK_PROGRAM, 1);
  relay(0x117, 1);
  cu60(0x117, 0);
  unit_poll();
  CHECK(!M.week_program);
  val_set(V_WEEK_PROGRAM, 1);
  relay(0x117, 1);
  relay(0x117, 0);
  unit_poll();
  CHECK(M.week_program);

  // what the CU60 holds is shown as it is, its limits are wider than the editors'
  cu60(0x091, 300);
  cu60(0x016, 0);
  unit_poll();
  CHECK(M.boost_minutes == 300 && M.filter_months == 0 && filter_life() == 0 && filter_days_left() == 0);

  // A boost the CU60 doesn't count down, nothing answers at address 1, is stopped after a while and the user told
  val_set(V_BOOST, 1);
  CHECK(coil(0x13A));
  for (int s = 0; s < 12; s++) {  // counting down
    cu60(0x0C1, (uint16_t) (1800 - s));
    unit_poll();
    unit_second();
  }
  CHECK(M.boost_on && !coil(0x13B) && !toasts);
  for (int s = 0; s < 10; s++) {  // stuck, 9 seconds without a change
    cu60(0x0C1, 1700);
    unit_poll();
    unit_second();
  }
  CHECK(!coil(0x13B) && !toasts);
  cu60(0x0C1, 1700);
  unit_poll();
  unit_second();
  CHECK(coil(0x13B) && toasts == 1);
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
}

int main(void) {
  sim_display_init();
  anim_init();
  test_picture();
  test_jpeg();
  test_deflate();
  test_anim();
  test_formats();
  test_values();
  test_items();
  test_clock();
  test_mbserver();
  test_bus_clock();
  test_settings();
  test_runtime();
  test_leds();
  test_schedule();
  test_input();
  test_unit();
  test_link();
  char why[96] = "";
  if (menu_check(why, sizeof why)) {
    printf("menus: %s\n", why);
    fails++;
  }
  printf("unit tests: %s\n", fails ? "FAILED" : "ok");
  return fails != 0;
}
