// Damaged and random input for pinflate(), both ends of the link and the panel's picture and animation messages.
// No crash, no write past a buffer (SAN=1), output as promised. Each test starts from seed 1, FUZZ_SEED=n for another.
//   build/fuzz_test [-l | -v] [TEST...]
#include "overlay_int.h"
#include "model.h"
#include "pbulk.h"
#include "pdeflate.h"
#include "picture.h"
#include "pjpeg.h"
#include "plink.h"
#include "check.h"
#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rnd_state;
static uint32_t rnd(void) {
  rnd_state = rnd_state * 1664525u + 1013904223u;
  return rnd_state >> 8;
}
static int below(int n) { return n > 0 ? (int) (rnd() % (uint32_t) n) : 0; }

static void seed(void) {
  const char *s = getenv("FUZZ_SEED");
  rnd_state = s ? (uint32_t) strtoul(s, NULL, 0) : 1;
}

// b[n] damaged a few ways: bits flipped, bytes changed, cut short, longer with noise or a run copied from
// elsewhere in it. Returns the new length, at most cap.
static int damage(uint8_t *b, int n, int cap) {
  for (int k = 1 + below(3); k > 0; k--)
    switch (below(6)) {
      case 0:
      case 1:
        if (n)
          b[below(n)] ^= (uint8_t) (1u << below(8));
        break;
      case 2:
        if (n)
          b[below(n)] = (uint8_t) rnd();
        break;
      case 3:
        n = below(n + 1);
        break;
      case 4:
        for (int m = below(16); m > 0 && n < cap; m--)
          b[n++] = (uint8_t) rnd();
        break;
      default:
        if (n > 1) {
          int from = below(n), to = below(n), len = 1 + below(n - (from > to ? from : to));
          memmove(b + to, b + from, (size_t) len);
        }
    }
  return n;
}

// A copy of b[n] on the heap, exactly that size so the sanitizer sees a read past it
static uint8_t *heap(const uint8_t *b, int n) {
  uint8_t *h = malloc(n ? (size_t) n : 1);
  memcpy(h, b, (size_t) n);
  return h;
}

static void noise(uint8_t *b, int n) {
  for (int i = 0; i < n; i++)
    b[i] = (uint8_t) rnd();
}

// ---------------------------------------------------------------- pinflate()
#define RAW 3000
static uint8_t raw[RAW];  // what the streams make

// Streams to start from: pdeflate()'s of runs and rows, of a rectangle of them and of noise, and a pzip piece
// against a history. Each makes len bytes of raw, width of them every stride, after hist bytes of raw.
typedef struct {
  uint8_t b[PDEFLATE_BOUND(RAW)];
  uint32_t n, len, hist;
  unsigned width, stride;
} stream_t;
static stream_t streams[4];

static void make_streams(void) {
  for (int i = 0; i < RAW; i++)  // runs and rows, then noise
    raw[i] = (uint8_t) (i >= 2000 ? (int) rnd() : i % 300 < 120 ? (i / 40) * 17 : i / 3);
  stream_t *s = streams;
  *s = (stream_t) {.len = RAW, .width = RAW, .stride = RAW};
  s->n = pdeflate(raw, RAW, RAW, RAW, s->b, sizeof s->b);
  s++;
  *s = (stream_t) {.len = 24 * 40, .width = 24, .stride = 60};  // a rectangle of a bigger buffer
  s->n = pdeflate(raw, 24, 60, 24 * 40, s->b, sizeof s->b);
  s++;
  *s = (stream_t) {.len = 1000, .width = 1000, .stride = 1000};
  s->n = pdeflate(raw + 2000, 1000, 1000, 1000, s->b, sizeof s->b);  // the noise, stored
  s++;
  static uint16_t table[PZIP_TABLE];
  uint8_t piece[PZIP_MAX];
  pzip_t z = {.hist = raw, .nh = 1000, .data = raw + 1000, .n = 600, .table = table};
  unsigned m = pzip(&z, piece);  // the first piece, against the 1000 bytes before it
  *s = (stream_t) {.n = m - PZIP_HEAD, .len = piece[1] | piece[2] << 8, .hist = 1000};
  memcpy(s->b, piece + PZIP_HEAD, s->n);
  s->width = s->stride = s->len;
}

// Inflates in[n] as len bytes, width of them every stride, after the hist bytes of raw it may match. The output
// buffer is exactly that big, and pinflate() mustn't touch the gaps between rows or the history.
static uint32_t inflate_into(const uint8_t *in, uint32_t n, unsigned width, unsigned stride, uint32_t len,
                             uint32_t hist, uint8_t *got) {
  uint32_t rows = (len + width - 1) / width, size = hist + (rows - 1) * stride + len - (rows - 1) * width;
  uint8_t *out = malloc(size);
  for (uint32_t i = 0; i < size; i++)
    out[i] = (uint8_t) (i * 7 + 1);
  memcpy(out, raw, hist);
  uint32_t used = pinflate(in, n, out + hist, width, stride, len, hist);
  CHECK(used <= n);
  bool kept = !memcmp(out, raw, hist);
  for (uint32_t r = 0; r + 1 < rows; r++)
    for (uint32_t i = hist + r * stride + width; i < hist + (r + 1) * stride; i++)
      kept &= out[i] == (uint8_t) (i * 7 + 1);
  CHECK_MSG(kept, "pinflate() wrote outside its %u x %u rectangle", width, rows);
  if (got && used)
    for (uint32_t i = 0; i < len; i++)
      got[i] = out[hist + i / width * stride + i % width];
  free(out);
  return used;
}

static void test_inflate(void) {
  seed();
  make_streams();
  static uint8_t got[RAW];
  for (int i = 0; i < 4; i++) {  // undamaged, each inflates to what it was made from
    stream_t *s = &streams[i];
    uint8_t *in = heap(s->b, (int) s->n);
    CHECK_EQ(inflate_into(in, s->n, s->width, s->stride, s->len, s->hist, got), s->n);
    const uint8_t *want = i == 0 ? raw : i == 1 ? NULL : i == 2 ? raw + 2000 : raw + 1000;
    bool same = true;
    for (uint32_t k = 0; k < s->len; k++)
      same &= got[k] == (want ? want[k] : raw[k / 24 * 60 + k % 24]);
    CHECK_MSG(same, "stream %d inflates to something else", i);
    free(in);
  }
  for (int round = 0; round < 40000; round++) {
    stream_t *s = &streams[below(4)];
    uint8_t b[PDEFLATE_BOUND(RAW) + 16];
    int n = (int) s->n;
    memcpy(b, s->b, (size_t) n);
    if (below(8))
      n = damage(b, n, (int) sizeof b);
    else
      noise(b, n = below((int) sizeof b));
    uint8_t *in = heap(b, n);
    uint32_t len = below(4) ? s->len : 1 + (uint32_t) below(RAW);
    switch (below(3)) {
      case 0:
        CHECK(pinflate(in, (uint32_t) n, NULL, len, len, len, 0) <= (uint32_t) n);  // only checking
        break;
      case 1: {  // into a rectangle
        unsigned width = 1 + (unsigned) below(64), stride = width + (unsigned) below(9);
        inflate_into(in, (uint32_t) n, width, stride, len, 0, NULL);
        break;
      }
      default:  // after a history
        inflate_into(in, (uint32_t) n, len, len, len, s->hist ? s->hist : 1 + (uint32_t) below(1000), NULL);
    }
    free(in);
  }
}

// ---------------------------------------------------------------- the link's frames
static long bytes_seen;

// pbulk_parse()'s and pbulk_esp_answer()'s callback: reads every byte it's given, so the sanitizer sees one past
static void got_msg(void *u, uint16_t key, const uint8_t *b, int len) {
  (void) key;
  CHECK(len >= 0 && len <= *(int *) u);
  for (int i = 0; i < len; i++)
    bytes_seen += b[i];
}

static bool well_formed(const uint8_t *f, int n) { return pbulk_frame_len(f, n) == n && pbus_crc_ok(f, n); }

// A few messages for the ESP's queue: events, values, DATA and a text
static void esp_queue(pbulk_esp_t *e) {
  for (int k = below(4); k > 0; k--) {
    uint8_t v[PBULK_MSG_MAX];
    int n = below(3) ? 4 : below(PBULK_MSG_MAX + 1);
    noise(v, n);
    uint16_t key = below(3) ? PBULK_DATA_KEY : (uint16_t) below(0x400);
    pbulk_esp_send(e, key, v, key == PBULK_DATA_KEY ? n & ~3 : n, (unsigned) below(4));
  }
}

// What the panel's main loop does with what came in, and now and then a message of its own
static void panel_loop(pbulk_panel_t *p) {
  uint8_t out[PBULK_MAX];
  int n;
  while ((n = pbulk_panel_take(p, out)) > 0) {
    CHECK(n <= PBULK_MAX);
    pbulk_parse(out, n, got_msg, &n);
  }
  if (pbulk_panel_free(p) && !below(4)) {  // a message or two, DATA in 4s like a screenshot's
    uint8_t m[PBULK_ANSWER_MAX];
    int o = 0;
    for (int k = 1 + below(2); k > 0 && PBULK_ANSWER_MAX - o >= PBULK_MSG_HEAD; k--) {
      uint16_t key = below(2) ? PBULK_DATA_KEY : K_THEME;
      int len = below(PBULK_ANSWER_MAX - o - PBULK_MSG_HEAD + 1);
      if (key == PBULK_DATA_KEY)
        len &= ~3;
      plink_put16(m + o, key);
      m[o + 2] = (uint8_t) len;
      noise(m + o + PBULK_MSG_HEAD, len);
      o += PBULK_MSG_HEAD + len;
    }
    pbulk_panel_put(p, m, o);
  }
}

// The ESP's frames, damaged, to the panel: its answer is a whole frame or none
static void test_panel_frames(void) {
  seed();
  static pbulk_esp_t e;
  static pbulk_panel_t p;
  pbulk_esp_init(&e, 0x31, 3);
  pbulk_panel_init(&p, 0x32);
  for (int round = 0; round < 80000; round++) {
    pbulk_esp_retry(&e);  // after a refusal
    esp_queue(&e);
    uint8_t f[PBULK_FRAME_MAX + 16];
    int n = pbulk_esp_window(&e, 2000 + (uint32_t) below(20000), f);
    if (n && !CHECK(well_formed(f, n)))
      break;
    if (!n) {  // a frame of noise
      noise(f, n = below((int) sizeof f));
      if (n >= 2) {
        f[0] = PBULK_ADDR;
        f[1] = PBULK_FC_SEND;
      }
    } else if (below(2))
      n = damage(f, n, (int) sizeof f);
    if (n >= PBULK_FRAME(0) && below(4)) {  // most with their length and CRC right, so the panel looks inside
      f[PBULK_N] = (uint8_t) (n - PBULK_FRAME(0));
      pbus_put_crc(f, n - 2);
    }
    uint8_t *in = heap(f, n), *a = malloc(PBULK_ANSWER_FRAME_MAX);
    uint8_t say = below(8) ? PB_OK : (uint8_t) below(3);
    int an = pbulk_panel_frame(&p, 3, in, n, (uint32_t) below(20000), say, PBUS_END_US, a);
    CHECK(an >= 0 && an <= PBULK_ANSWER_FRAME_MAX);
    if (an > 0) {
      CHECK(well_formed(a, an));
      pbulk_esp_answer(&e, a, an, got_msg, &an);
    } else
      pbulk_esp_no_answer(&e);
    free(in);
    free(a);
    panel_loop(&p);
    if (!below(500))
      pbulk_panel_init(&p, (uint16_t) rnd());
  }
}

// The panel's answers, damaged, to the ESP: its queue and its next frame stay whole
static void test_esp_answers(void) {
  seed();
  static pbulk_esp_t e;
  static pbulk_panel_t p;
  pbulk_esp_init(&e, 0x41, 3);
  pbulk_panel_init(&p, 0x42);
  for (int round = 0; round < 80000; round++) {
    pbulk_esp_retry(&e);  // after a refusal
    esp_queue(&e);
    uint8_t f[PBULK_FRAME_MAX], a[PBULK_ANSWER_FRAME_MAX + 16];
    int n = pbulk_esp_window(&e, 2000 + (uint32_t) below(20000), f);
    if (n && !CHECK(well_formed(f, n)))
      break;
    int an = n ? pbulk_panel_frame(&p, 3, f, n, 20000, PB_OK, PBUS_END_US, a) : 0;
    if (!an) {  // an answer of noise
      noise(a, an = below((int) sizeof a));
      if (an >= 2) {
        a[0] = PBULK_ADDR;
        a[1] = PBULK_FC_ANSWER;
      }
    } else if (below(2))
      an = damage(a, an, (int) sizeof a);
    if (an >= PBULK_ANSWER(0) && below(4)) {  // most to this session with their length and CRC right
      a[PBULK_N] = (uint8_t) (an - PBULK_ANSWER(0));
      plink_put16(a + PBULK_A_ESESS, e.session);
      pbus_put_crc(a, an - 2);
    }
    if (an >= 3 && a[0] == PBULK_ADDR && a[1] == PBULK_FC_ANSWER) {  // an F7 67 frame, as panel_link.h passes on
      uint8_t *in = heap(a, an);
      pbulk_esp_answer(&e, in, an, got_msg, &an);
      free(in);
    }
    CHECK(e.qn <= PBULK_ESP_BUF);
    panel_loop(&p);
    if (!below(500))
      pbulk_esp_init(&e, (uint16_t) rnd(), 3);
  }
}

// ---------------------------------------------------------------- the panel's pictures and animations
static uint8_t id;
static uint8_t status(int32_t ack) { return (uint8_t) (ack >> 24); }

// An answer from picture.c, -1 for none
static void answer(int32_t a) { CHECK_MSG(a == -1 || (a >= 0 && status(a) <= PIC_BAD), "answer 0x%08X", (unsigned) a); }

static const uint32_t *palette(void) {
  static uint32_t pal[16];
  for (int i = 0; i < 16; i++)
    pal[i] = rnd() & 0xFFFFFF;
  return pal;
}

// data[n] as DATA in pieces of all sizes, then END with crc
static void send_data(const uint8_t *data, unsigned n, uint32_t crc) {
  for (unsigned i = 0; i < n;) {
    unsigned k = 1 + (unsigned) below(120);
    k = k < n - i ? k : n - i;
    uint8_t *b = heap(data + i, (int) k);
    answer(pic_msg(K_PIC_DATA, b, (int) k));
    free(b);
    i += k;
  }
  uint8_t c[4];
  plink_put32(c, crc);
  answer(pic_msg(K_PIC_END, c, below(8) ? 4 : below(5)));
}

// A header of n bytes as it is, or damaged
static void send_head(uint16_t key, uint8_t *h, int n, int cap) {
  if (!below(6))
    n = damage(h, n, cap);
  uint8_t *b = heap(h, n);
  answer(pic_msg(key, b, n));
  free(b);
}

// Rows of 16 colours packed, then in pzip pieces into z[cap]. Damaged before or after.
static unsigned rows_zipped(int w, int rows, uint8_t *z, int cap, uint32_t *crc) {
  static uint8_t packed[PIC_POOL];
  static uint16_t table[PZIP_TABLE];
  unsigned n = 0, bytes = (unsigned) (w + 1) / 2;
  for (int r = 0; r < rows && n + bytes * 2 + 2 < sizeof packed; r++) {
    uint8_t row[PIC_MAX_W / 2];
    for (unsigned i = 0; i < bytes; i++)
      row[i] = (uint8_t) (i % 9 < 4 ? (uint32_t) r * 17 : rnd());
    n += plink_pack_row(row, bytes, packed + n);
  }
  if (!below(4))
    n = (unsigned) damage(packed, (int) n, (int) sizeof packed);
  *crc = plink_crc32(0, packed, n);
  pzip_t zp = {.data = packed, .n = n, .table = table};
  unsigned k = 0, m;
  while ((m = pzip(&zp, z + k)))
    k += m;
  if (!below(4))
    k = (unsigned) damage(z, (int) k, cap);
  return k;
}

// Every pixel of the picture shown, and a part of it, into buffers just big enough
static void read_picture(void) {
  if (pic_state() != PIC_READY)
    return;
  int w = pic_width(), h = pic_height();
  CHECK(w > 0 && w <= PIC_MAX_W && h > 0 && h <= PIC_MAX_H);
  lv_color_t *all = malloc(sizeof *all * (size_t) (w * h));
  lv_area_t a = {0, 0, (lv_coord_t) (w - 1), (lv_coord_t) (h - 1)};
  pic_read(&a, all, w);
  free(all);
  a.x1 = (lv_coord_t) below(w);
  a.y1 = (lv_coord_t) below(h);
  a.x2 = (lv_coord_t) (a.x1 + below(w - a.x1));
  a.y2 = (lv_coord_t) (a.y1 + below(h - a.y1));
  lv_color_t *part = malloc(sizeof *part * (size_t) lv_area_get_size(&a));
  pic_read(&a, part, lv_area_get_width(&a));
  free(part);
}

static uint32_t photo(void *ctx, int x, int y) {
  (void) ctx;
  return (uint32_t) (40 + x * 4 + (x * y) % 7) << 16 | (uint32_t) (60 + y * 7) << 8 | (uint32_t) (200 - x - y);
}

// Pictures of 16 colours, patches of them and JPEGs, sent whole or damaged, then drawn if they're taken
static void test_pictures(void) {
  seed();
  static uint8_t jpeg[8192], z[PIC_POOL + 1024];
  pjpeg_t e;
  pjpeg_begin(&e, 48, 24, 90, jpeg, sizeof jpeg);
  while (pjpeg_mcu(&e, photo, NULL)) {
  }
  uint32_t jn = pjpeg_end(&e);
  if (!CHECK(jn <= sizeof jpeg))
    return;
  for (int round = 0; round < 8000; round++) {
    uint8_t h[PIC_BEGIN_HEAD + PIC_PAL_LEN + 16];
    uint32_t crc;
    switch (below(4)) {
      case 0: {
        int w = 1 + below(below(4) ? 64 : PIC_MAX_W), rows = 1 + below(below(4) ? 32 : PIC_MAX_H);
        unsigned n = rows_zipped(w, rows, z, (int) sizeof z, &crc);
        int hn = plink_pic_begin(h, ++id, PIC_PALETTE, (uint16_t) w, (uint16_t) rows, n, palette());
        send_head(K_PIC_BEGIN, h, hn, (int) sizeof h);
        send_data(z, n, crc);
        break;
      }
      case 1: {
        int y = below(40), rows = below(8), w = pic_state() == PIC_READY ? pic_width() : 1 + below(64);
        unsigned n = rows_zipped(w, rows, z, (int) sizeof z, &crc);
        int hn = plink_pic_patch(h, ++id, (uint16_t) y, (uint16_t) rows, n, below(2) ? palette() : NULL);
        send_head(K_PIC_PATCH, h, hn, (int) sizeof h);
        send_data(z, n, crc);
        break;
      }
      case 2: {
        static uint8_t j[sizeof jpeg + 16];
        memcpy(j, jpeg, jn);
        int n = below(4) ? damage(j, (int) jn, (int) sizeof j) : (int) jn;
        int hn = plink_pic_begin(h, ++id, PIC_JPEG, 48, 24, (uint32_t) n, NULL);
        send_head(K_PIC_BEGIN, h, hn, (int) sizeof h);
        send_data(j, (unsigned) n, plink_crc32(0, j, (unsigned) n));
        break;
      }
      default: {  // any picture message, of any length
        static const uint16_t KEYS[] = {K_PIC_BEGIN, K_PIC_PATCH, K_PIC_DATA, K_PIC_END, K_PIC_CLEAR};
        int n = below((int) sizeof h);
        noise(h, n);
        send_head(KEYS[below(5)], h, n, (int) sizeof h);
      }
    }
    read_picture();
  }
  pic_msg(K_PIC_CLEAR, NULL, 0);
}

// An overlay of 4 random frames, a still of the first or a field, its records damaged or not, into slot, its width
// odd or even. Its colours have any opacity, any of them glow or cycle.
static void send_overlay(int slot) {
  int kind = below(4), still = kind != OVERLAY_ANIMATED, w = 1 + below(still ? 128 : 32),
      h = 1 + below(still ? 48 : 16), bytes = (w + 1) / 2 * h;
  static uint8_t frames[4][64 * 48], d[5 * (OVERLAY_RECORD_HEAD + PDEFLATE_BOUND(64 * 48)) + 16];
  for (int f = 0; f < 4; f++)
    for (int i = 0; i < bytes; i++)
      frames[f][i] = (uint8_t) (i % 5 < 2 ? 0xFF : rnd());
  unsigned n = 0;
  if (kind == OVERLAY_STILL)
    n = plink_overlay_pieces(frames[0], (unsigned) w, (unsigned) h, (uint16_t) rnd(), (uint8_t) rnd(), d, sizeof d);
  else if (kind == OVERLAY_SHEET)  // its frames, now and then one made longer
    for (int f = 0; f < 4; f++) {
      memcpy(d + n, (uint8_t[]) {(uint8_t) rnd(), below(6) ? OVERLAY_RECORD_FRAME : OVERLAY_RECORD_MORE, 0, 0, 0},
             OVERLAY_RECORD_HEAD);
      n += OVERLAY_RECORD_HEAD;
      n += plink_overlay_pieces(frames[f], (unsigned) w, (unsigned) h, (uint16_t) rnd(), 0, d + n, sizeof d - n);
    }
  else if (kind == OVERLAY_FIELD) {  // one of them, mostly as it can be
    d[OVERLAY_FIELD_KIND] = (uint8_t) below(OVERLAY_FIELDS + (below(8) ? 0 : 2));
    d[OVERLAY_FIELD_A] = (uint8_t) (below(4) ? 1 + below(OVERLAY_NOISE_LAYERS) : (int) rnd());
    plink_put16(d + OVERLAY_FIELD_B, (uint16_t) (below(8) ? OVERLAY_NOISE_MIN + below(60) : (int) rnd()));
    n = OVERLAY_FIELD_LEN;
  } else
    for (int f = 0; f <= 4; f++)
      n += plink_overlay_record(f ? frames[f - 1] : NULL, frames[f % 4], (unsigned) w, (unsigned) h, (uint8_t) rnd(),
                                d + n);
  if (below(2))
    n = (unsigned) damage(d, (int) n, (int) sizeof d);
  uint32_t pal[16];
  for (int i = 0; i < 16; i++)
    pal[i] = palette()[i] | (below(2) ? 0xFF000000 : rnd() << 24);
  uint8_t b[OVERLAY_BEGIN_LEN + 16];
  int bn =
      plink_overlay_begin(b, ++id, (uint8_t) slot, (uint8_t) kind, (uint16_t) w, (uint16_t) h, (uint8_t) (1 + below(2)),
                          n, pal, (uint16_t) rnd(), rnd() & 0xFFFFFF, (uint16_t) (below(2) ? rnd() : 0));
  if (!below(16))
    b[OVERLAY_BEGIN_KIND] = (uint8_t) rnd();
  send_head(K_OVERLAY_BEGIN, b, bn, (int) sizeof b);
  send_data(d, n, plink_crc32(0, d, n));
}

// A number in a value: 1, 2 or 4 bytes, or a register
static int put_number(uint8_t *b) {
  int k = below(4);
  if (k == 3) {
    b[0] = (uint8_t) (OVERLAY_VREG + below(OVERLAY_REGS));
    return 1;
  }
  static const int32_t EDGE[] = {INT32_MIN, INT32_MIN + 1, INT32_MAX, -1, 0, 1 << 30, -(1 << 30)};
  int32_t v = below(4) ? below(600) - 100 : below(4) ? (int32_t) rnd() : EDGE[below(7)];  // the ends of a number too
  b[0] = k == 2 ? OVERLAY_V32 : k ? OVERLAY_V16 : OVERLAY_V8;
  int len = k == 2 ? 4 : k ? 2 : 1;
  for (int i = 0; i < len; i++)  // low byte first
    b[1 + i] = (uint8_t) ((uint32_t) v >> 8 * i);
  return 1 + len;
}

// A value in a script, 40 bytes at most: a number, or one worked out by OPs that mostly have what they take, now
// and then with one that isn't one or without its end
static int put_value(uint8_t *b) {
  if (below(3))
    return put_number(b);
  int n = 0, left = 0;
  b[n++] = OVERLAY_VEXPR;
  for (int ops = 1 + below(10); (ops > 0 || left != 1) && n < 28; ops--) {
    int op = OVERLAY_OP_END + 1 + below(OVERLAY_OPS - OVERLAY_OP_END - 1);
    if (!below(40))
      op = below(256);
    if (op >= OVERLAY_OPS || OP_TAKES[op] > left || (ops <= 0 && OP_TAKES[op] < 2 && left > 1) || !below(3)) {
      if (left < OVERLAY_EXPR_STACK - 1 || !below(8)) {
        n += put_number(b + n);
        left++;
      }
      if (op < OVERLAY_OPS && below(20))
        continue;
    }
    b[n++] = (uint8_t) op;
    left += op < OVERLAY_OPS ? 1 - OP_TAKES[op] : 0;
    if (op == OVERLAY_OP_GET || op == OVERLAY_OP_OWN)
      b[n++] = (uint8_t) below(OVERLAY_GETS + (below(20) ? 0 : 3));
    if (op == OVERLAY_OP_GET || op == OVERLAY_OP_DIST || op == OVERLAY_OP_ANGLE)
      b[n++] = (uint8_t) (below(4) ? below(OVERLAY_PLACES + (below(20) ? 0 : 2)) : OVERLAY_SELF);
    if (op == OVERLAY_OP_DIST || op == OVERLAY_OP_ANGLE)
      b[n++] = (uint8_t) (below(4) ? below(OVERLAY_PLACES) : OVERLAY_SELF);
    if (op == OVERLAY_OP_PICK || op == OVERLAY_OP_CURVE) {  // now and then none, or more than there's room for
      int k = below(8) ? 1 + below(4) : below(2) * 255;
      b[n++] = (uint8_t) k;
      noise(b + n, 2 * LV_MIN(k, 4));
      n += 2 * LV_MIN(k, 4);
    }
  }
  if (below(30))
    b[n++] = OVERLAY_OP_END;
  return n;
}

// What a bind binds, mostly something that can be
static uint8_t bind_what(void) {
  static const uint8_t OPS[] = {OVERLAY_BIND_SET, OVERLAY_BIND_ADD, OVERLAY_BIND_MUL, OVERLAY_BIND_FREE};
  int to = below(6)   ? below(OVERLAY_CHANNELS)
           : below(2) ? OVERLAY_BIND_FRAME + below(OVERLAY_BIND_TARGETS - OVERLAY_BIND_FRAME + 1)
                      : OVERLAY_BIND_REG + below(OVERLAY_REGS + 1);
  return (uint8_t) (to | (to < OVERLAY_CHANNELS || !below(8) ? OPS[below(4)]
                          : below(6)                         ? OVERLAY_BIND_SET
                                                             : OVERLAY_BIND_FREE));
}

// A GLIDE's or DRIFT's channels and values from at on, k of them, mostly ones it takes. Returns where they end.
static int put_channels(uint8_t *m, int at, int k, bool drift) {
  for (; k > 0; k--, at += OVERLAY_GLIDE_EACH) {
    int c = below(drift ? OVERLAY_SIZE : OVERLAY_CHANNELS);
    m[at] = (uint8_t) (below(30) ? c : (int) rnd());
    plink_put16(m + at + 1, (uint16_t) (below(3) ? below(scales(c) ? 300 : 400) - (scales(c) ? 0 : 60) : (int) rnd()));
  }
  return at;
}

// What a place takes from the ESP or a SEND but for its script, mostly as it should be: its key, into m. Returns its
// length.
static int put_msg(uint16_t *key, uint8_t *m) {
  static const uint16_t KEYS[] = {K_OVERLAY_AREA, K_OVERLAY_GLIDE, K_OVERLAY_DRIFT, K_OVERLAY_WANDER,
                                  K_OVERLAY_FACE, K_OVERLAY_VIEW,  K_OVERLAY_BIND,  K_OVERLAY_TINT};
  static const uint8_t LENS[] = {OVERLAY_AREA_LEN, 0, 0, OVERLAY_WANDER_LEN, OVERLAY_FACE_LEN, OVERLAY_VIEW_LEN, 0,
                                 OVERLAY_TINT_LEN};
  int i = below((int) LEN(KEYS)), n = LENS[i];
  *key = KEYS[i];
  noise(m, OVERLAY_SEND_MAX);
  if (*key == K_OVERLAY_GLIDE) {
    m[OVERLAY_GLIDE_EASE] = (uint8_t) below(below(20) ? 4 : 256);
    if (below(3))
      plink_put16(m + OVERLAY_GLIDE_MS, (uint16_t) (below(4) * 100));
    n = put_channels(m, OVERLAY_GLIDE_HEAD, 1 + below(3), false);
  } else if (*key == K_OVERLAY_DRIFT) {
    n = put_channels(m, OVERLAY_DRIFT_HEAD, 1 + below(3), true);
  } else if (*key == K_OVERLAY_BIND) {
    m[OVERLAY_BIND_WHAT] = bind_what();
    n = OVERLAY_BIND_VALUE +
        ((m[OVERLAY_BIND_WHAT] & OVERLAY_BIND_OP) == OVERLAY_BIND_FREE ? 0 : put_value(m + OVERLAY_BIND_VALUE));
  } else if (*key == K_OVERLAY_TINT) {
    m[OVERLAY_TINT_ON] = (uint8_t) below(3);
  } else if (*key == K_OVERLAY_AREA && below(8)) {
    plink_put16(m + OVERLAY_AREA_X0, (uint16_t) (below(200) - 20));
    plink_put16(m + OVERLAY_AREA_Y0, (uint16_t) (below(150) - 20));
    plink_put16(m + OVERLAY_AREA_X1, (uint16_t) (150 + below(200)));
    plink_put16(m + OVERLAY_AREA_Y1, (uint16_t) (100 + below(160)));
    m[OVERLAY_AREA_EDGES] = (uint8_t) below(3);
    m[OVERLAY_AREA_EDGES + 1] = (uint8_t) below(3);
  } else if (*key == K_OVERLAY_FACE && below(8)) {
    m[OVERLAY_FACE_FLAGS] = (uint8_t) below(4);
    plink_put16(m + OVERLAY_FACE_TILT, (uint16_t) below(181));
    plink_put16(m + OVERLAY_FACE_RATE, (uint16_t) below(400));
  }
  return below(12) ? n : below(OVERLAY_SEND_MAX + 1);
}

// A script of steps as the panel reads them, mostly good ones, to place p in the messages the ESP would send
static void send_script(int p) {
  uint8_t b[OVERLAY_SCRIPT_MAX + 136];
  uint16_t starts[128], jumps[128];  // where its steps start, and where its GOTOs and IFs say to go on
  int n = 0, steps = 0, nj = 0, more = below(4) ? 12 : 100;  // now and then a long one
  int regs = below(10) ? OVERLAY_OWN_REGS : below(OVERLAY_OWN_REGS + 2);
  while (n < OVERLAY_SCRIPT_MAX - 130 && steps < (int) LEN(starts) && below(more)) {
    starts[steps++] = (uint16_t) n;
    int op = below(OVERLAY_DOS);
    b[n++] = (uint8_t) op;
    if (op == OVERLAY_DO_SEND) {  // to a place, its own mostly
      uint16_t key;
      uint8_t m[OVERLAY_SEND_MAX + 48];
      int k = put_msg(&key, m);
      b[n++] = (uint8_t) (below(4) ? OVERLAY_SELF : below(OVERLAY_PLACES + 1));
      b[n++] = (uint8_t) (key - K_OVERLAY_FRAMES);
      b[n++] = (uint8_t) k;
      memcpy(b + n, m, (size_t) k);
      n += k;
      b[n++] = (uint8_t) below(2);
      if (b[n - 1]) {
        b[n++] = (uint8_t) below(k + 1);
        n += put_value(b + n);
      }
      continue;
    }
    if (op == OVERLAY_DO_GLIDE) {  // some channels, now and then none or too many, or no such one
      int k = below(20) ? 1 + below(3) : below(64);
      b[n++] = (uint8_t) (k | below(4) << 6);
      n += put_value(b + n);
      for (k = LV_MIN(k, 3); k > 0; k--) {
        b[n++] = (uint8_t) below(OVERLAY_CHANNELS + (below(20) ? 0 : 2));
        n += put_value(b + n);
      }
      continue;
    }
    for (const char *f = SCRIPT_FORMS[op]; *f; f++)
      if (*f == 'v')
        n += put_value(b + n);
      else if (*f == 'b') {
        b[n++] = bind_what();
        if ((b[n - 1] & OVERLAY_BIND_OP) != OVERLAY_BIND_FREE || !below(20))
          n += put_value(b + n);
      } else if (*f == 's') {  // runs of columns, now and then one too few or a bad one
        int k = 3 * below(4) + (below(10) ? 0 : 1);
        b[n++] = (uint8_t) k;
        for (int i = 0; i < k; i++) {
          int v = i % 3 ? below(80) - 10 : below(50) - (below(10) ? 0 : 5);
          plink_put16(b + n, (uint16_t) (i % 3 == 2 ? v + plink_i16(b + n - 2) : v));
          n += 2;
        }
      } else if (*f == 'f') {  // soft edges, mostly none or a few px, now and then more than a box
        for (int i = 0; i < 4; i++, n += 2)
          plink_put16(b + n, (uint16_t) (below(3) ? below(2) * below(30) : (int) (rnd() & 0x7FF)));
      } else if (*f == 'a') {  // filled in below
        jumps[nj++] = (uint16_t) n;
        n += 2;
      } else if (*f == 'z') {  // all of it, some, or more than there is
        int z = below(3) ? below(2) * below(40) : (int) (rnd() & 0xFFFF);
        b[n++] = (uint8_t) z;
        b[n++] = (uint8_t) (z >> 8);
      } else
        b[n++] = (uint8_t) (*f == 'r'   ? below(OVERLAY_REGS)
                            : *f == 'p' ? (below(4) ? below(OVERLAY_PLACES) : OVERLAY_SELF)
                            : *f == 'm' ? below(OVERLAY_CLIP_NEXT + 1)
                                        : below(256));
  }
  for (int i = 0; i < nj; i++) {  // a step, the end, or now and then nowhere
    int to = below(10) ? (below(4) ? starts[below(steps)] : n) : below(n + 1);
    b[jumps[i]] = (uint8_t) to;
    b[jumps[i] + 1] = (uint8_t) (to >> 8);
  }
  if (!below(10))
    noise(b + below(n + 1), below(4));
  for (int at = 0; at < n || !at;) {
    uint8_t m[PBULK_MSG_MAX] = {(uint8_t) p, (uint8_t) at,       (uint8_t) (at >> 8),
                                (uint8_t) n, (uint8_t) (n >> 8), (uint8_t) (below(40) ? regs : below(10))};
    int k = LV_MIN(n - at, (int) sizeof m - OVERLAY_SCRIPT_BYTES);
    memcpy(m + OVERLAY_SCRIPT_BYTES, b + at, (size_t) k);
    overlay_msg(K_OVERLAY_SCRIPT, m, OVERLAY_SCRIPT_BYTES + k);
    at += k;
    if (!k)
      break;
  }
}

// The overlays, sent whole or damaged, placed, moved and bound by messages of every kind and drawn as they go
static void test_overlays(void) {
  seed();
  lv_obj_t *layer = overlay_show(lv_scr_act());
  for (int round = 0; round < 3000; round++) {
    if (!below(8))
      send_overlay(below(OVERLAY_SLOTS + 1));
    for (int k = below(6); k > 0; k--) {
      uint8_t m[OVERLAY_SEND_MAX + 48];
      uint16_t key;
      int n;
      if (below(4)) {
        n = put_msg(&key, m);
      } else {  // a slot's rate, or a place, most on the screen
        noise(m, OVERLAY_PLACE_LEN);
        key = below(3) ? K_OVERLAY_PLACE : K_OVERLAY_FRAMES;
        n = key == K_OVERLAY_PLACE ? OVERLAY_PLACE_LEN : OVERLAY_FRAMES_LEN;
        m[OVERLAY_PLACE_SLOT] = (uint8_t) below(OVERLAY_SLOTS + 1);
        plink_put16(m + OVERLAY_PLACE_X, (uint16_t) (below(400) - 40));
        plink_put16(m + OVERLAY_PLACE_Y, (uint16_t) (below(300) - 30));
      }
      m[OVERLAY_MSG_PLACE] = (uint8_t) below((key == K_OVERLAY_FRAMES ? OVERLAY_SLOTS : OVERLAY_PLACES) + 1);
      uint8_t *b = heap(m, n);
      overlay_msg(key, b, n);
      free(b);
    }
    if (!below(10)) {  // a slot, all of them or a place, now or over a while
      uint8_t c[4];
      unsigned what = OVERLAY_ALL;
      if (below(3))
        what = (unsigned) below(OVERLAY_SLOTS);
      else if (below(2))
        what = OVERLAY_PLACE_BIT | (unsigned) below(OVERLAY_PLACES);
      plink_put32(c, what | (unsigned) below(3) * 200 << 8);
      pic_msg(K_OVERLAY_CLEAR, c, 4);
    }
    for (int k = below(3), tries = 0; k > 0 && tries < 20; tries++) {  // mostly to places that are there
      int p = below(OVERLAY_PLACES);
      overlay_look_t l;
      if (overlay_look(p, &l) || !below(10)) {
        send_script(p);
        k--;
      }
    }
    while (overlay_gone() >= 0 || overlay_mark() >= 0) {
    }
    CHECK(overlay_used() <= OVERLAY_POOL);
    sim_run(5 * below(20));
    if (!below(50)) {  // the screensaver goes and comes back
      lv_obj_del(layer);
      layer = overlay_show(lv_scr_act());
    }
  }
  lv_obj_clean(lv_scr_act());
  uint8_t all[4];
  plink_put32(all, OVERLAY_ALL);
  pic_msg(K_OVERLAY_CLEAR, all, 4);
  CHECK_EQ(overlay_used(), 0);
}

static const check_test_t TESTS[] = {
    {"inflate", test_inflate},   {"panel_frames", test_panel_frames}, {"esp_answers", test_esp_answers},
    {"pictures", test_pictures}, {"overlays", test_overlays},         {0},
};

int main(int argc, char **argv) {
  printf("fuzz_test from seed %s\n", getenv("FUZZ_SEED") ? getenv("FUZZ_SEED") : "1");
  sim_display_init();
  overlay_init();
  return check_main("fuzz_test", (const check_test_t *const[]) {TESTS, NULL}, argc, argv);
}
