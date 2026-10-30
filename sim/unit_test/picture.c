// app/picture.c: pictures of 16 colours and JPEGs, and the deflate they come in
#include "unit_test.h"

// ---------------------------------------------------------------- picture
static uint8_t px[H][W / 2];  // the expected picture, 2 pixels per byte

static uint32_t tint;  // XORed into every palette colour, see rgb()
uint32_t rgb(int i) { return ((uint32_t) (i * 0x0F0F0F) ^ 0x204060) ^ tint; }

unsigned pack(int y, int count, uint8_t *out) {
  unsigned n = 0;
  for (int r = y; r < y + count; r++)
    n += plink_pack_row(px[r], W / 2, out + n);
  return n;
}

uint8_t status(int32_t ack) { return ack < 0 ? 0xFF : (uint8_t) (ack >> 24); }

uint8_t id;
int32_t (*to_pic)(uint16_t key, const uint8_t *b, int n) = pic_msg;

const uint32_t *palette(void) {
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
int32_t patch(int y, int count, uint32_t len, bool pal) {
  uint8_t b[PIC_PATCH_HEAD + PIC_PAL_LEN];
  return to_pic(K_PIC_PATCH, b, sim_pic_patch(b, ++id, y, count, len, pal ? palette() : NULL));
}

int32_t end(uint32_t crc) {
  uint8_t b[4];
  plink_put32(b, crc);
  return to_pic(K_PIC_END, b, 4);
}

int32_t data(const uint8_t *b, unsigned n) { return to_pic(K_PIC_DATA, b, (int) n); }

// The rest of a transfer after the header got answer a. DATA in pieces, then END. Returns the last answer.
int32_t rest(int32_t a, const uint8_t *b, unsigned len, unsigned piece, uint32_t crc) {
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
int32_t rest_zip(int32_t a, const uint8_t *b, unsigned n, const uint8_t *hist, unsigned nh, unsigned piece) {
  uint8_t z[1024];
  return rest(a, z, zip(b, n, hist, nh, z), piece, plink_crc32(0, b, n));
}

int32_t send_picture(unsigned piece) {
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
bool shows(void) {
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

void fill(int y, int count, uint8_t seed) {
  for (int r = y; r < y + count; r++)
    for (int i = 0; i < W / 2; i++)
      px[r][i] = (uint8_t) (i < 3 ? seed + r : (seed + r) * 17 + i);  // a run, then literals
}

// No picture, and messages straight to picture.c
static void no_picture(void) {
  to_pic = pic_msg;
  tint = 0;
  pic_msg(K_PIC_CLEAR, NULL, 0);
}

static void test_picture(void) {
  no_picture();
  fill(0, H, 1);
  int32_t a = send_picture(3);
  CHECK_EQ(status(a), PIC_DONE);
  CHECK_EQ(a >> 16 & 0xFF, id);  // the answer says which transfer
  CHECK_EQ(pic_state(), PIC_READY);
  CHECK(shows());

  fill(2, 3, 9);  // rows 2-4 replaced
  CHECK_EQ(status(send_patch(2, 3, false)), PIC_DONE);
  CHECK(shows());

  // a patch cut off by a new picture. The new one goes to the start of the pool, not behind the old.
  uint8_t b[512], z[1024];
  unsigned n = pack(0, 2, b);
  patch(0, 2, n, false);
  data(z, zip(b, n, NULL, 0, z) / 2);
  fill(0, H, 40);
  CHECK_EQ(status(send_picture(64)), PIC_DONE);
  CHECK(shows());

  // a bad patch with a run across a row end is refused and leaves the picture alone
  uint8_t bad[] = {126 + 7, 0x11, 0, 0x22, 0, 0x33, 0, 0x44};  // a 7-byte repeat in a 5-byte row
  CHECK_EQ(status(rest_zip(patch(1, 1, sizeof bad, false), bad, sizeof bad, NULL, 0, 8)), PIC_BAD);
  CHECK(shows());
  // and so are bytes after the last row
  n = pack(1, 1, b);
  b[n] = 0;
  b[n + 1] = 0x55;
  CHECK_EQ(status(rest_zip(patch(1, 1, n + 2, false), b, n + 2, NULL, 0, 8)), PIC_BAD);
  CHECK(shows());
  CHECK_EQ(status(rest_zip(patch(1, 0, 2, false), b, 2, NULL, 0, 8)), PIC_BAD);
  CHECK(shows());
  // and the next patch is still spliced in at the right length
  fill(5, 1, 70);
  CHECK_EQ(status(send_patch(5, 1, true)), PIC_DONE);
  CHECK(shows());

  // a new palette alone, as a patch with no rows
  tint = 0x300000;
  CHECK_EQ(status(send_patch(0, 0, true)), PIC_DONE);
  CHECK(shows());
  tint = 0;
  CHECK_EQ(status(send_patch(H, 0, true)), PIC_DONE);
  CHECK(shows());

  // A wrong CRC changes nothing. A patch has to fit in the pool behind the picture, and its rows in the picture.
  n = pack(1, 1, b);
  CHECK_EQ(status(rest(patch(1, 1, n, false), z, zip(b, n, NULL, 0, z), 4, 12345)), PIC_BAD_CRC);
  CHECK(shows());
  CHECK_EQ(status(patch(1, 1, PIC_POOL, false)), PIC_NO_ROOM);
  CHECK_EQ(status(patch(1, 1, UINT32_MAX, false)), PIC_NO_ROOM);  // doesn't wrap round to fit
  CHECK_EQ(status(patch(4, 3, n, false)), PIC_BAD_SIZE);
  CHECK(shows());

  // The ESP restarting drops a patch in progress but keeps the picture. A picture in progress is dropped.
  patch(0, 1, n, false);
  pic_reset();
  CHECK_EQ(end(0), -1);
  CHECK(shows());
  begin(W, H, 100);
  CHECK_EQ(pic_state(), PIC_RECEIVING);
  pic_reset();
  CHECK_EQ(pic_state(), PIC_NONE);
  CHECK(!shows());
  CHECK_EQ(status(patch(0, 1, n, false)), PIC_NO_PICTURE);

  // DATA and END outside a transfer are ignored. A bad header is refused.
  CHECK_EQ(data(b, 4), -1);
  CHECK_EQ(end(0), -1);
  CHECK_EQ(status(pic_msg(K_PIC_BEGIN, b, 9)), PIC_BAD);
  CHECK_EQ(status(begin(PIC_MAX_W + 2, H, 4)), PIC_BAD_SIZE);
  CHECK_EQ(status(begin(W, H, PIC_POOL + 1)), PIC_TOO_BIG);

  // the whole picture again, in 1-byte pieces
  fill(0, H, 3);
  CHECK_EQ(status(send_picture(1)), PIC_DONE);
  CHECK(shows());

  // A patch deflated against the picture the panel has: rows copied from others come as matches into it, so it
  // takes fewer bytes than the rows themselves
  uint8_t old[512];
  unsigned no = pack(0, H, old);
  memcpy(px[2], px[5], sizeof px[2]);
  memcpy(px[3], px[0], sizeof px[3]);
  n = pack(2, 2, b);
  unsigned k = zip(b, n, old, no, z);
  CHECK(k < n);
  CHECK_EQ(status(rest(patch(2, 2, n, false), z, k, 5, plink_crc32(0, b, n))), PIC_DONE);
  CHECK(shows());
  // Pieces that are too long, make too much, or reach back past the pool are refused
  n = pack(0, H, b);
  k = zip(b, n, NULL, 0, z);
  z[0] = PZIP_MAX - PZIP_HEAD + 1;
  CHECK_EQ(status(rest(begin(W, H, n), z, k, 64, plink_crc32(0, b, n))), PIC_BAD);
  k = zip(b, n, NULL, 0, z);
  CHECK_EQ(status(rest(begin(W, H, n - 1), z, k, 64, plink_crc32(0, b, n))), PIC_BAD);
  k = zip(b, n, old, no, z);  // as if the panel had a picture before it, which a whole one can't reach
  CHECK_EQ(status(rest(begin(W, H, n), z, k, 64, plink_crc32(0, b, n))), PIC_BAD);
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
  no_picture();
  uint8_t b[1024];
  fill_big(0, BH, 1);
  unsigned n = pack_big(0, BH, b);
  CHECK_EQ(status(rest_zip(begin(BW, BH, n), b, n, NULL, 0, 50)), PIC_DONE);
  CHECK(big_shows());
  int patches[][2] = {{5, 13}, {8, 8}, {16, 1}, {0, 27}, {20, 7}, {3, 2}};  // y, count
  for (size_t i = 0; i < sizeof patches / sizeof patches[0]; i++) {
    int y = patches[i][0], count = patches[i][1];
    fill_big(y, count, (int) i + 2);
    n = pack_big(y, count, b);
    CHECK_EQ(status(rest_zip(patch(y, count, n, false), b, n, NULL, 0, 50)), PIC_DONE);
    CHECK(big_shows());
  }
  // A patch redraws only its rows, the picture object keeps showing
  lv_obj_t *o = pic_show(lv_scr_act());
  lv_refr_now(NULL);
  lv_disp_t *d = lv_disp_get_default();
  fill_big(9, 3, 9);
  n = pack_big(9, 3, b);
  CHECK_EQ(status(rest_zip(patch(9, 3, n, false), b, n, NULL, 0, 50)), PIC_DONE);
  CHECK(big_shows());
  lv_area_t *a = &d->inv_areas[0];  // LVGL adds 5 px all round
  CHECK_EQ(d->inv_p, 1);
  CHECK_EQ(a->y1, o->coords.y1 + 9 - 5);
  CHECK_EQ(a->y2, o->coords.y1 + 11 + 5);
  lv_refr_now(NULL);
  CHECK_EQ(status(rest_zip(patch(0, 0, 0, true), b, 0, NULL, 0, 50)), PIC_DONE);  // all its colours
  CHECK(big_shows());
  CHECK_EQ(d->inv_p, 1);
  CHECK_EQ(a->y1, o->coords.y1 - 5);
  CHECK_EQ(a->y2, o->coords.y2 + 5);
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
  no_picture();
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
  CHECK(n97 > n);
  CHECK_EQ(e.n, 2);
  CHECK_EQ(e.o[1].quality, 97);
  CHECK_EQ(e.o[1].len, n97);
  CHECK_EQ(status(send_jpeg(j, n, JW, JH)), PIC_DONE);
  CHECK_EQ(pic_state(), PIC_READY);
  CHECK_EQ(pic_format(), PIC_JPEG);
  CHECK_EQ(pic_width(), JW);
  CHECK_EQ(pic_height(), JH);

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
  CHECK_EQ(o->coords.y1 % 8, 0);
  CHECK(o->coords.y1 > 0);
  CHECK_EQ(o->coords.x1, (320 - JW) / 2);
  lv_area_t over = {(lv_coord_t) (o->coords.x1 + 3), (lv_coord_t) (o->coords.y1 + 3), (lv_coord_t) (o->coords.x1 + 9),
                    (lv_coord_t) (o->coords.y1 + 12)};
  lv_area_t beside = {0, (lv_coord_t) (o->coords.y1 + 3), 9, (lv_coord_t) (o->coords.y1 + 12)}, b0 = beside;
  lv_area_t probe = {0, 0, 0, 36}, low = {0, 0, 0, 4};
  pic_round(NULL, &over);
  pic_round(NULL, &beside);
  lv_disp_get_default()->rendering_in_progress = 1;  // the probes come while LVGL draws
  pic_round(NULL, &probe);
  pic_round(NULL, &low);
  lv_disp_get_default()->rendering_in_progress = 0;
  CHECK_EQ(over.y1, o->coords.y1);
  CHECK_EQ(over.y2, o->coords.y1 + 15);
  CHECK_EQ(over.x1, o->coords.x1 + 3);
  CHECK(!memcmp(&beside, &b0, sizeof b0));
  CHECK_EQ(probe.y1, 0);
  CHECK_EQ(probe.y2, 39);
  CHECK_EQ(low.y2, 4);
  lv_refr_now(NULL);
  _lv_inv_area(NULL, &(lv_area_t) {0, 236, 3, 239});  // a strip of a few lines beside it still gets drawn
  lv_refr_now(NULL);
  CHECK_EQ(lv_disp_get_default()->inv_p, 0);
  lv_obj_del(o);
  lv_area_t still = {1, 3, 2, 5};  // nothing to round to without it
  pic_round(NULL, &still);
  CHECK_EQ(still.y1, 3);
  CHECK_EQ(still.y2, 5);
  lv_area_t outside = {0, 0, JW, 0};
  CHECK(!pic_read(&outside, part, JW));
  CHECK_EQ(status(patch(0, 1, 4, false)), PIC_NO_PICTURE);  // only a 16-colour picture takes patches

  // LVGL's heap too full for the decoder: taken, drawn once there's room
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  void *hog = lv_mem_alloc(mon.free_biggest_size - 1024);
  CHECK(hog);
  CHECK_EQ(status(send_jpeg(j, n, JW, JH)), PIC_DONE);
  CHECK(!pic_read(&all, part, JW));
  lv_mem_free(hog);
  CHECK(pic_read(&all, part, JW) && !memcmp(part, whole, sizeof whole));

  // refused: a size off the MCU grid, a size that isn't the JPEG's, a cut-off stream, a missing restart marker
  CHECK_EQ(status(jpeg_begin(JW + 8, JH, n)), PIC_BAD_SIZE);
  CHECK_EQ(status(jpeg_begin(JW, JH + 4, n)), PIC_BAD_SIZE);
  CHECK_EQ(status(send_jpeg(j, n, JW, JH + 8)), PIC_BAD);
  CHECK_EQ(status(send_jpeg(j, n - 40, JW, JH)), PIC_BAD);
  for (uint32_t i = n - 2; i > 0; i--)
    if (j[i] == 0xFF && j[i + 1] == 0xD1) {
      j[i + 1] = 0xD2;  // restart markers out of order
      break;
    }
  CHECK_EQ(status(send_jpeg(j, n, JW, JH)), PIC_BAD);
  CHECK_EQ(pic_state(), PIC_NONE);
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
  CHECK_EQ(pinflate(DYN, sizeof DYN, out, N, N, N, 0), sizeof DYN);
  CHECK(!memcmp(out, TXT, N));
  CHECK_EQ(pinflate(FIX, sizeof FIX, out, N, N, N, 0), sizeof FIX);
  CHECK(!memcmp(out, TXT, N));
  CHECK_EQ(pinflate(stored, sizeof stored, out, N, N, N, 0), sizeof stored);
  CHECK(!memcmp(out, TXT, N));
  CHECK_EQ(pinflate(DYN, sizeof DYN, NULL, N, N, N, 0), sizeof DYN);  // only checking
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
  CHECK(k > 0);
  CHECK(k < sizeof big / 4);
  CHECK_EQ(pinflate(c, k, &back[0][0], sizeof back, sizeof back, sizeof back, 0), k);
  CHECK(!memcmp(big, back, sizeof big));
  memset(back, 0xAB, sizeof back);
  k = pdeflate(&big[3][5], 10, 24, 10 * 20, c, sizeof c);  // 10 x 20 from (5, 3)
  CHECK(k > 0);
  CHECK_EQ(pinflate(c, k, &back[7][2], 10, 24, 10 * 20, 0), k);
  bool same = true;
  for (int y = 0; y < 40; y++)
    for (int x = 0; x < 24; x++)
      same &= back[y][x] == (y >= 7 && y < 27 && x >= 2 && x < 12 ? big[y - 4][x + 3] : 0xAB);
  CHECK(same);
  CHECK_EQ(pdeflate(&big[0][0], sizeof big, sizeof big, sizeof big, c, 10), 0);  // doesn't fit

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
    CHECK(ok);
    CHECK_EQ(made, sizeof data);
    CHECK(!memcmp(outp + nh, data, sizeof data));
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
  CHECK_EQ(pzip(&none, z), 0);
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
  CHECK(ok);
  CHECK_EQ(made, 600);
  CHECK(!memcmp(run, got, 600));
  CHECK(zn < 40);
  free(run);
  free(got);
}

// The panel acks every KiB of a long one, counting up, then DONE
static void test_progress(void) {
  no_picture();
  static uint8_t j[8192];
  pjpeg_t e;
  pjpeg_begin(&e, 2 * JW, 2 * JH, 97, j, sizeof j);
  while (pjpeg_mcu(&e, photo, NULL)) {
  }
  uint32_t n = pjpeg_end(&e);
  CHECK(n > 2048 && n <= sizeof j);
  int32_t a = jpeg_begin(2 * JW, 2 * JH, n);
  CHECK(a < 0);
  int acks = 0;
  uint32_t got = 0;
  bool counting = true;
  for (uint32_t i = 0; i < n; i += 61) {
    a = data(j + i, n - i < 61 ? n - i : 61);
    if (a < 0)
      continue;
    counting &= status(a) == PIC_PROGRESS && (a & 0xFFFF) > got && (a & 0xFFFF) <= i + 61;
    got = a & 0xFFFF;
    acks++;
  }
  CHECK(counting);
  CHECK_EQ(acks, (int) (n / 1024));
  CHECK_EQ(status(end(plink_crc32(0, j, n))), PIC_DONE);
  CHECK_EQ(pic_state(), PIC_READY);
  CHECK_EQ(pic_width(), 2 * JW);
}

const check_test_t PICTURE_TESTS[] = {
    {"picture", test_picture}, {"picture_parts", test_picture_parts},
    {"jpeg", test_jpeg},       {"progress", test_progress},
    {"deflate", test_deflate}, {0},
};
