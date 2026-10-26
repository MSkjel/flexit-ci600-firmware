// Damaged and random input for pinflate(), both ends of the link and the panel's picture and animation messages.
// No crash, no write past a buffer (SAN=1), output as promised. Each test starts from seed 1, FUZZ_SEED=n for another.
//   build/fuzz_test [-l | -v] [TEST...]
#include "anim.h"
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
  if (!p->out_full && !below(4)) {
    uint8_t m[PBULK_ANSWER_MAX] = {K_THEME & 0xFF, K_THEME >> 8};
    int len = below(PBULK_ANSWER_MAX - PBULK_MSG_HEAD + 1);
    m[2] = (uint8_t) len;
    noise(m + PBULK_MSG_HEAD, len);
    pbulk_panel_put(p, m, PBULK_MSG_HEAD + len);
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

// An animation of 4 random frames, its records damaged or not, into slot
static void send_animation(int slot) {
  int w = 2 * (1 + below(16)), h = 1 + below(16), bytes = w / 2 * h;
  static uint8_t frames[4][16 * 16], d[5 * (ANIM_RECORD_HEAD + PDEFLATE_BOUND(16 * 16)) + 16];
  for (int f = 0; f < 4; f++)
    for (int i = 0; i < bytes; i++)
      frames[f][i] = (uint8_t) (i % 5 < 2 ? 0xFF : rnd());
  unsigned n = 0;
  for (int f = 0; f <= 4; f++)
    n += plink_anim_record(f ? frames[f - 1] : NULL, frames[f % 4], (unsigned) w, (unsigned) h, (uint8_t) rnd(), d + n);
  if (below(2))
    n = (unsigned) damage(d, (int) n, (int) sizeof d);
  uint8_t b[ANIM_BEGIN_HEAD + PIC_PAL_LEN + 16];
  int bn = plink_anim_begin(b, ++id, (uint8_t) slot, (uint16_t) w, (uint16_t) h, (uint8_t) (1 + below(2)),
                            (uint8_t) below(ANIM_OPAQUE + 1), n, palette());
  send_head(K_ANIM_BEGIN, b, bn, (int) sizeof b);
  send_data(d, n, plink_crc32(0, d, n));
}

// The animations, sent whole or damaged, placed and moved by messages of every kind and drawn as they go
static void test_anims(void) {
  seed();
  static const struct {
    uint16_t key;
    int len;  // 0 for a GLIDE or DRIFT, which have a value for each channel
  } MSGS[] = {{K_ANIM_FRAMES, ANIM_FRAMES_LEN},
              {K_ANIM_PLACE, ANIM_PLACE_LEN},
              {K_ANIM_AREA, ANIM_AREA_LEN},
              {K_ANIM_GLIDE, 0},
              {K_ANIM_DRIFT, 0},
              {K_ANIM_WANDER, ANIM_WANDER_LEN},
              {K_ANIM_FACE, ANIM_FACE_LEN},
              {K_ANIM_FLIP, ANIM_FLIP_LEN},
              {K_ANIM_WAVE, ANIM_WAVE_LEN},
              {K_ANIM_MAP, ANIM_MAP_LEN},
              {K_ANIM_ROUNDS, ANIM_ROUNDS_LEN}};
  lv_obj_t *layer = anim_show(lv_scr_act());
  for (int round = 0; round < 3000; round++) {
    if (!below(8))
      send_animation(below(ANIM_SLOTS + 1));
    for (int k = below(6); k > 0; k--) {
      int i = below((int) LEN(MSGS));
      uint8_t m[32];
      noise(m, (int) sizeof m);
      m[ANIM_MSG_PLACE] = (uint8_t) below(ANIM_PLACES + 1);
      if (MSGS[i].key == K_ANIM_FRAMES)
        m[ANIM_FRAMES_SLOT] = (uint8_t) below(ANIM_SLOTS + 1);
      if (MSGS[i].key == K_ANIM_PLACE) {  // most on the screen
        m[ANIM_PLACE_SLOT] = (uint8_t) below(ANIM_SLOTS + 1);
        plink_put16(m + ANIM_PLACE_X, (uint16_t) (below(400) - 40));
        plink_put16(m + ANIM_PLACE_Y, (uint16_t) (below(300) - 30));
      }
      int n = MSGS[i].len;
      if (!n) {
        m[1] &= (1u << ANIM_CHANNELS) - 1;
        n = MSGS[i].key == K_ANIM_GLIDE ? ANIM_GLIDE_HEAD : ANIM_DRIFT_HEAD;
        n += 2 * __builtin_popcount(m[1]);
      }
      if (!below(8))
        n = below((int) sizeof m);
      uint8_t *b = heap(m, n);
      anim_msg(MSGS[i].key, b, n);
      free(b);
    }
    if (!below(10)) {  // a slot, all of them or a place, now or over a while
      uint8_t c[4];
      unsigned what = ANIM_ALL;
      if (below(3))
        what = (unsigned) below(ANIM_SLOTS);
      else if (below(2))
        what = ANIM_PLACE_BIT | (unsigned) below(ANIM_PLACES);
      plink_put32(c, what | (unsigned) below(3) * 200 << 8);
      pic_msg(K_ANIM_CLEAR, c, 4);
    }
    while (anim_gone() >= 0) {
    }
    CHECK(anim_used() <= ANIM_POOL);
    sim_run(5 * below(20));
    if (!below(50)) {  // the screensaver goes and comes back
      lv_obj_del(layer);
      layer = anim_show(lv_scr_act());
    }
  }
  lv_obj_clean(lv_scr_act());
  uint8_t all[4];
  plink_put32(all, ANIM_ALL);
  pic_msg(K_ANIM_CLEAR, all, 4);
}

static const check_test_t TESTS[] = {
    {"inflate", test_inflate},
    {"panel_frames", test_panel_frames},
    {"esp_answers", test_esp_answers},
    {"pictures", test_pictures},
    {"anims", test_anims},
    {0},
};

int main(int argc, char **argv) {
  sim_display_init();
  anim_init();
  return check_main("fuzz_test", (const check_test_t *const[]) {TESTS, NULL}, argc, argv);
}
