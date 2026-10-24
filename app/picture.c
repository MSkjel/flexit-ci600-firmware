// Screensaver picture, see picture.h
#include "picture.h"
#include "anim.h"
#include "pdeflate.h"
#include "plink.h"
#include "tjpgd/tjpgd.h"
#include <string.h>

_Static_assert(LV_COLOR_DEPTH == 16 && !LV_COLOR_16_SWAP, "TJpgDec's RGB565 is LVGL's colour as it is");

static uint8_t pool[PIC_POOL];  // the compressed picture, then an incoming patch right after it

static struct {  // the picture
  pic_state_t state;
  uint8_t format;  // PIC_PALETTE or PIC_JPEG
  uint16_t w, h;
  uint32_t len;  // compressed bytes in pool
  // where each row starts in pool, plus the end of the last. For a JPEG it's MCU rows of 8 lines, each just after
  // its restart marker
  uint16_t row_off[PIC_MAX_H + 1];
  lv_color_t pal[16];
  uint8_t line[PIC_MAX_W / 2];  // last unpacked row, 16 colours
  int16_t line_y;               // which row line holds, -1 = none
  uint8_t gen;                  // counts pictures, so an object shows only the one it was made for
} P;

// TJpgDec state while drawing part of a JPEG. About 3 KB, taken from LVGL's heap
typedef struct {
  JDEC jd;
  uint32_t pos;     // next byte of pool for TJpgDec
  lv_area_t area;   // being drawn, in the picture's coordinates
  lv_color_t *buf;  // where area's top left pixel goes
  int stride;
  uint8_t work[3072];  // TJpgDec's tables and buffers, it needs 2968 bytes for pjpeg.h's JPEGs
} jpeg_t;

// Transfer in progress. A whole picture into P, a patch after it in pool, or an animation for anim.c
static struct {
  enum { IN_NONE, IN_PICTURE, IN_PATCH, IN_ANIM } kind;
  uint8_t id;                     // the transfer's id, echoed in our acks
  uint16_t y, count;              // a patch's first row and number of rows
  uint32_t len, got, crc, acked;  // total bytes, bytes received, CRC so far, bytes received at the last ack
  uint32_t out;                   // decompressed bytes so far
  uint16_t rows;                  // complete rows so far
  uint8_t lit, rep_next;          // PackBits literal bytes left, next byte is a repeat
  uint16_t rep;                   // length of that repeat
  bool new_pal;                   // patch comes with a new palette in pal
  lv_color_t pal[16];
  uint8_t piece[PZIP_MAX];  // a 16-colour picture's pzip piece as it comes
  uint8_t pn;
} IN;

static uint32_t row_bytes(void) { return (P.w + 1u) / 2u; }

static void clear(void) { P.state = PIC_NONE; }

void pic_reset(void) {
  if (IN.kind == IN_PICTURE)
    clear();
  if (IN.kind == IN_ANIM)
    anim_abort();
  IN.kind = IN_NONE;
}

static int32_t ack(uint8_t status) {
  IN.acked = IN.got;
  return (int32_t) ((uint32_t) status << 24 | (uint32_t) IN.id << 16 | (IN.got & 0xFFFF));
}

static int32_t fail(uint8_t status) {
  pic_reset();
  return ack(status);
}

static uint16_t get16(const uint8_t *b) { return (uint16_t) (b[0] | b[1] << 8); }
static uint16_t get16be(const uint8_t *b) { return (uint16_t) (b[0] << 8 | b[1]); }  // JPEG's byte order

static void palette(lv_color_t *pal, const uint8_t *rgb) {
  for (int i = 0; i < 16; i++, rgb += 3)
    pal[i] = lv_color_make(rgb[0], rgb[1], rgb[2]);
}

// BEGIN: id, format, w (2), h (2), len (4), then for PIC_PALETTE the palette (16 x RGB)
static int32_t begin(const uint8_t *b, int n) {
  pic_reset();
  clear();
  memset(&IN, 0, sizeof IN);
  IN.id = n ? b[0] : 0;
  if (n < 2 || (b[1] == PIC_PALETTE ? n != 58 : b[1] != PIC_JPEG || n != 10))
    return ack(PIC_BAD);
  P.format = b[1];
  P.w = get16(b + 2);
  P.h = get16(b + 4);
  uint32_t len = plink_number(b + 6, 4);
  if (!P.w || !P.h || P.w > PIC_MAX_W || P.h > PIC_MAX_H || (P.format == PIC_JPEG && (P.w % 16 || P.h % 8)))
    return ack(PIC_BAD_SIZE);
  if (len > PIC_POOL)
    return ack(PIC_TOO_BIG);
  if (P.format == PIC_PALETTE)
    palette(P.pal, b + 10);
  P.len = len;
  P.row_off[0] = 0;
  P.state = PIC_RECEIVING;
  IN.kind = IN_PICTURE;
  IN.len = len;
  return -1;
}

// PATCH: id, y (2), count (2), len (4), then a new palette or none. Stored after the picture, so it must fit there.
static int32_t patch(const uint8_t *b, int n) {
  pic_reset();
  memset(&IN, 0, sizeof IN);
  IN.id = n ? b[0] : 0;
  if (n != 9 && n != 57)
    return ack(PIC_BAD);
  IN.y = get16(b + 1);
  IN.count = get16(b + 3);
  uint32_t len = plink_number(b + 5, 4);
  if (P.state != PIC_READY || P.format != PIC_PALETTE)
    return ack(PIC_NO_PICTURE);
  if (IN.y + IN.count > P.h)
    return ack(PIC_BAD_SIZE);
  if (len > PIC_POOL - P.len)
    return ack(PIC_NO_ROOM);
  IN.new_pal = n == 57;
  if (IN.new_pal)
    palette(IN.pal, b + 9);
  IN.kind = IN_PATCH;
  IN.len = len;
  return -1;
}

// Track one compressed byte. Rows are packed on their own, so a run can't cross a row end.
static bool parse(uint8_t c) {
  uint16_t rows = IN.kind == IN_PICTURE ? P.h : IN.count;
  if (IN.rows >= rows)
    return false;  // past the last row
  if (IN.lit) {
    IN.lit--;
    IN.out++;
  } else if (IN.rep_next) {
    IN.out += IN.rep;
    IN.rep_next = 0;
  } else if (c < 128) {
    IN.lit = c + 1;
  } else {
    IN.rep = c - 126;
    IN.rep_next = 1;
  }
  IN.got++;
  if (IN.lit || IN.rep_next)
    return true;
  while (IN.rows < rows && IN.out >= (IN.rows + 1u) * row_bytes()) {
    if (IN.out > (IN.rows + 1u) * row_bytes())
      return false;  // a run crossed the end of a row
    if (IN.kind == IN_PICTURE)
      P.row_off[IN.rows + 1] = (uint16_t) IN.got;
    IN.rows++;
  }
  return true;
}

// ANIM_BEGIN: anim.c checks the header and keeps the records
static int32_t anim_begin(const uint8_t *b, int n) {
  pic_reset();
  memset(&IN, 0, sizeof IN);
  IN.id = n ? b[0] : 0;
  int st = anim_open(b, n, &IN.len);
  if (st >= 0)
    return ack((uint8_t) st);
  IN.kind = IN_ANIM;
  return -1;
}

static int32_t data(const uint8_t *b, int n) {
  if (IN.kind == IN_NONE)
    return -1;  // the rest of a failed transfer, the ESP already has our answer
  if (IN.kind == IN_ANIM) {
    unsigned k = IN.len - IN.got < (unsigned) n ? IN.len - IN.got : (unsigned) n;
    anim_bytes(IN.got, b, k);
    IN.crc = plink_crc32(IN.crc, b, k);
    IN.got += k;
    return IN.got - IN.acked >= 1024 ? ack(PIC_PROGRESS) : -1;
  }
  uint8_t *dst = IN.kind == IN_PATCH ? pool + P.len : pool;
  if (P.format == PIC_JPEG) {  // checked as a whole at the end
    uint32_t k = IN.len - IN.got < (uint32_t) n ? IN.len - IN.got : (uint32_t) n;
    memcpy(dst + IN.got, b, k);
    IN.crc = plink_crc32(IN.crc, b, k);
    IN.got += k;
  }
  for (int i = 0; P.format == PIC_PALETTE && i < n && IN.got < IN.len; i++) {  // each piece inflated into place
    IN.piece[IN.pn++] = b[i];
    if (IN.piece[0] > PZIP_MAX - PZIP_HEAD)
      return fail(PIC_BAD);
    if (IN.pn < PZIP_HEAD + IN.piece[0])
      continue;
    IN.pn = 0;
    uint32_t made = get16(IN.piece + 1);
    if (!made || made > IN.len - IN.got ||
        !pinflate(IN.piece + PZIP_HEAD, IN.piece[0], dst + IN.got, made, made, made, (uint32_t) (dst - pool) + IN.got))
      return fail(PIC_BAD);
    IN.crc = plink_crc32(IN.crc, dst + IN.got, made);
    while (made--)
      if (!parse(dst[IN.got]))
        return fail(PIC_BAD);
  }
  return IN.got - IN.acked >= 1024 ? ack(PIC_PROGRESS) : -1;
}

// Bytes in the compressed row at p. A patch's runs end at row ends, parse() checked that.
static uint32_t row_len(const uint8_t *p) {
  uint32_t out = 0, n = 0;
  while (out < row_bytes()) {
    uint8_t c = p[n];
    out += c < 128 ? c + 1u : c - 126u;
    n += c < 128 ? c + 2u : 2u;
  }
  return n;
}

static void reverse(uint8_t *a, uint8_t *b) {  // [a, b)
  for (; a < b; a++) {
    uint8_t t = *a;
    *a = *--b;
    *b = t;
  }
}

// Replace rows y..y+count-1 with the patch stored after the picture.
// [0, start) + old rows + [end, len) + patch -> [0, start) + patch + [end, len)
// Drop the old rows, then rotate the patch in front of the rest with three reversals. No second buffer needed.
static void splice(void) {
  uint32_t plen = IN.len, start = P.row_off[IN.y], end = P.row_off[IN.y + IN.count], rest = P.len - end;
  memmove(pool + start, pool + end, rest + plen);
  reverse(pool + start, pool + start + rest);
  reverse(pool + start + rest, pool + start + rest + plen);
  reverse(pool + start, pool + start + rest + plen);
  int32_t delta = (int32_t) plen - (int32_t) (end - start);
  for (unsigned r = IN.y + IN.count; r <= P.h; r++)
    P.row_off[r] = (uint16_t) (P.row_off[r] + delta);
  for (unsigned r = IN.y + 1u; r < IN.y + IN.count; r++)
    P.row_off[r] = (uint16_t) (P.row_off[r - 1] + row_len(pool + P.row_off[r - 1]));
  P.len = (uint32_t) ((int32_t) P.len + delta);
  if (IN.new_pal)
    memcpy(P.pal, IN.pal, sizeof P.pal);
}

// Checks for a whole JPEG as pjpeg.h makes them. SOF0 at the picture's size, Y 2x1, Cb and Cr 1x1, a restart
// interval of one MCU row and the restart markers in order. Fills in where each MCU row starts.
static bool jpeg_rows(void) {
  const uint8_t *p = pool + 2, *end = pool + P.len;
  bool sof = false, dri = false;
  if (P.len < 4 || pool[0] != 0xFF || pool[1] != 0xD8 || pool[P.len - 2] != 0xFF || pool[P.len - 1] != 0xD9)
    return false;  // SOI ... EOI, a JPEG cut short has no EOI
  while (p + 4 <= end && p[0] == 0xFF) {
    unsigned len = get16be(p + 2);
    const uint8_t *s = p + 4;
    if (len < 2 || p + 2 + len > end)
      return false;
    if (p[1] == 0xC0)
      sof = len == 17 && s[0] == 8 && get16be(s + 1) == P.h && get16be(s + 3) == P.w && s[5] == 3 && s[7] == 0x21 &&
            s[10] == 0x11 && s[13] == 0x11;
    else if (p[1] == 0xDD)
      dri = len == 4 && get16be(s) == P.w / 16;
    else if (p[1] == 0xDA) {  // the scan, MCU rows with RST0..RST7 in turn between them
      if (!sof || !dri)
        return false;
      unsigned rows = P.h / 8u, k = 0;
      P.row_off[k++] = (uint16_t) (p + 2 + len - pool);
      for (const uint8_t *q = p + 2 + len; q + 1 < end; q++)
        if (q[0] == 0xFF && q[1] >= 0xD0 && q[1] <= 0xD7) {
          if (k >= rows || q[1] != 0xD0 + (k - 1) % 8)
            return false;
          P.row_off[k++] = (uint16_t) (q + 2 - pool);
        }
      return k == rows;
    }
    p += 2 + len;
  }
  return false;
}

static size_t jpeg_in(JDEC *jd, uint8_t *b, size_t n) {
  jpeg_t *j = jd->device;
  if (n > P.len - j->pos)
    n = P.len - j->pos;
  if (b)
    memcpy(b, pool + j->pos, n);
  j->pos += n;
  return n;
}

// TJpgDec output, one MCU. Copy the part inside the area we're drawing
static int jpeg_out(JDEC *jd, void *bitmap, JRECT *r) {
  jpeg_t *j = jd->device;
  const uint16_t *s = bitmap;
  int w = r->right - r->left + 1;
  int x1 = LV_MAX(r->left, j->area.x1), x2 = LV_MIN(r->right, j->area.x2);
  int y1 = LV_MAX(r->top, j->area.y1), y2 = LV_MIN(r->bottom, j->area.y2);
  for (int y = y1; y <= y2; y++)
    for (int x = x1; x <= x2; x++)
      j->buf[(y - j->area.y1) * j->stride + (x - j->area.x1)].full = s[(y - r->top) * w + (x - r->left)];
  return 1;
}

static bool jpeg_prepare(jpeg_t *j) {
  j->pos = 0;
  return jd_prepare(&j->jd, jpeg_in, j->work, sizeof j->work, j) == JDR_OK && j->jd.msx == 2 && j->jd.msy == 1 &&
         j->jd.width == P.w && j->jd.height == P.h;
}

// whether TJpgDec takes the JPEG's tables, assumed yes when LVGL's heap is too full to check
static bool jpeg_check(void) {
  jpeg_t *j = lv_mem_alloc(sizeof *j);
  bool ok = !j || jpeg_prepare(j);
  lv_mem_free(j);
  return ok;
}

// END, with the CRC-32 of the len bytes
static int32_t finish(uint32_t crc) {
  if (IN.kind == IN_ANIM) {
    if (IN.got != IN.len)
      return fail(PIC_BAD);
    if (crc != IN.crc)
      return fail(PIC_BAD_CRC);
    if (!anim_close())
      return fail(PIC_BAD);
    int32_t a = ack(PIC_DONE);
    IN.kind = IN_NONE;
    return a;
  }
  bool jpeg = IN.kind == IN_PICTURE && P.format == PIC_JPEG;
  uint16_t rows = IN.kind == IN_PICTURE ? P.h : IN.count;
  if (IN.got != IN.len || (!jpeg && IN.rows != rows))
    return fail(PIC_BAD);
  if (crc != IN.crc)
    return fail(PIC_BAD_CRC);
  if (jpeg && !jpeg_rows())
    return fail(PIC_BAD);
  if (IN.kind == IN_PATCH)
    splice();
  else
    P.state = PIC_READY;
  if (jpeg && !jpeg_check()) {
    clear();
    return fail(PIC_BAD);
  }
  P.line_y = -1;
  P.gen++;
  int32_t a = ack(PIC_DONE);
  IN.kind = IN_NONE;
  return a;
}

int32_t pic_msg(uint16_t key, const uint8_t *b, int n) {
  switch (key) {
    case K_PIC_BEGIN:
      return begin(b, n);
    case K_PIC_PATCH:
      return patch(b, n);
    case K_PIC_DATA:
      return data(b, n);
    case K_PIC_END:
      return IN.kind == IN_NONE ? -1 : finish(plink_number(b, n));
    case K_ANIM_BEGIN:
      return anim_begin(b, n);
    case K_ANIM_CLEAR:
      if (IN.kind == IN_ANIM)
        pic_reset();
      anim_clear(b, n);
      return -1;
    case K_ANIM_PLACE:
      anim_place(b, n);
      return -1;
    case K_ANIM_MOVE:
      anim_move(b, n);
      return -1;
    case K_ANIM_TURN:
      anim_turn(b, n);
      return -1;
    case K_ANIM_AREA:
      anim_area(b, n);
      return -1;
    case K_ANIM_SIZE:
      anim_size(b, n);
      return -1;
    case K_ANIM_FADE:
      anim_fade(b, n);
      return -1;
    case K_ANIM_ROUNDS:
      anim_rounds(b, n);
      return -1;
    case K_PIC_CLEAR:
      pic_reset();
      clear();
      return -1;
    default:
      return -1;
  }
}

pic_state_t pic_state(void) { return P.state; }
uint8_t pic_format(void) { return P.format; }
uint8_t pic_percent(void) { return IN.len ? (uint8_t) (IN.got * 100 / IN.len) : 0; }
uint16_t pic_width(void) { return P.w; }
uint16_t pic_height(void) { return P.h; }

// ---------------------------------------------------------------- drawing
// row y of a 16-colour picture into P.line
static void unpack_row(int y) {
  const uint8_t *p = pool + P.row_off[y], *end = pool + P.row_off[y + 1];
  uint32_t o = 0, rb = row_bytes();
  while (o < rb && p < end) {
    uint8_t c = *p++;
    unsigned n = c < 128 ? c + 1u : c - 126u;
    if (o + n > rb)
      n = rb - o;
    if (c < 128) {
      memcpy(&P.line[o], p, n);
      p += c + 1u;
    } else
      memset(&P.line[o], *p++, n);
    o += n;
  }
}

bool pic_read(const lv_area_t *a, lv_color_t *buf, int stride) {
  if (P.state != PIC_READY || a->x1 < 0 || a->y1 < 0 || a->x2 >= P.w || a->y2 >= P.h || a->x1 > a->x2 || a->y1 > a->y2)
    return false;
  if (P.format == PIC_PALETTE) {
    for (int y = a->y1; y <= a->y2; y++, buf += stride) {
      if (y != P.line_y) {
        unpack_row(y);
        P.line_y = (int16_t) y;
      }
      const uint8_t *src = &P.line[a->x1 >> 1];
      lv_color_t *d = buf;
      int x = a->x1;
      if (x & 1) {
        *d++ = P.pal[*src++ & 15];
        x++;
      }
      for (; x < a->x2; x += 2, d += 2) {  // a byte at a time, two pixels
        uint8_t b = *src++;
        d[0] = P.pal[b >> 4];
        d[1] = P.pal[b & 15];
      }
      if (x == a->x2)
        *d = P.pal[*src >> 4];
    }
    return true;
  }
  jpeg_t *j = lv_mem_alloc(sizeof *j);  // the MCU rows a covers, each from the restart marker before it
  bool ok = j && jpeg_prepare(j);
  if (ok) {
    j->area = *a;
    j->buf = buf;
    j->stride = stride;
    unsigned r0 = (unsigned) a->y1 / 8, r1 = (unsigned) a->y2 / 8;
    j->pos = P.row_off[r0];
    ok = jd_decomp_rows(&j->jd, jpeg_out, r0, r1 - r0 + 1) == JDR_OK;
  }
  lv_mem_free(j);
  return ok;
}

// The picture object draws straight into LVGL's draw buffer, only the part being drawn now
static void pic_event(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target(e);
  bool ours = P.state == PIC_READY && (uintptr_t) lv_event_get_user_data(e) == P.gen;
  if (lv_event_get_code(e) == LV_EVENT_COVER_CHECK) {
    if (ours && _lv_area_is_in(lv_event_get_cover_area(e), &o->coords, 0))
      lv_event_set_cover_res(e, LV_COVER_RES_COVER);
  } else if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN && ours) {
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t c;
    if (!_lv_area_intersect(&c, &o->coords, ctx->clip_area))
      return;
    int stride = lv_area_get_width(ctx->buf_area);
    lv_color_t *buf = (lv_color_t *) ctx->buf + (c.y1 - ctx->buf_area->y1) * stride + (c.x1 - ctx->buf_area->x1);
    lv_area_t a = {c.x1 - o->coords.x1, c.y1 - o->coords.y1, c.x2 - o->coords.x1, c.y2 - o->coords.y1};
    if (!pic_read(&a, buf, stride))  // decoder out of memory or tables unusable, black like the screen
      for (int y = c.y1; y <= c.y2; y++, buf += stride)
        memset(buf, 0, lv_area_get_width(&c) * sizeof *buf);
  }
}

lv_obj_t *pic_show(lv_obj_t *parent) {
  if (P.state != PIC_READY)
    return NULL;
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, P.w, P.h);
  lv_obj_center(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(o, pic_event, LV_EVENT_ALL, (void *) (uintptr_t) P.gen);
  return o;
}
