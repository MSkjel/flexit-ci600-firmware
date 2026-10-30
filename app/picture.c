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
  // where every 8th row starts in pool, the rest are found from there. For a JPEG it's each MCU row of 8 lines,
  // just after its restart marker.
  uint16_t off[PIC_MAX_H / 8];
  lv_color_t pal[16];
  uint8_t gen;  // counts pictures, so an object shows only the one it was made for
} pic;
_Static_assert(PIC_POOL <= UINT16_MAX && PIC_MAX_H % 8 == 0, "off[]: 16 bits into pool, every 8th row");
static lv_obj_t *shown;  // the object showing it, NULL for none

// TJpgDec state while drawing part of a JPEG. About 3 KB, taken from LVGL's heap
typedef struct {
  JDEC jd;
  uint32_t pos;     // next byte of pool for TJpgDec
  lv_area_t area;   // being drawn, in the picture's coordinates
  lv_color_t *buf;  // where area's top left pixel goes
  int stride;
  uint8_t work[3072];  // TJpgDec's tables and buffers, it needs 2968 bytes for pjpeg.h's JPEGs
} jpeg_t;

// Transfer in progress. A whole picture into pic, a patch after it in pool, or an animation for anim.c
static struct {
  enum { XF_NONE, XF_PICTURE, XF_PATCH, XF_ANIM } kind;
  uint8_t id;                     // the transfer's id, echoed in our acks
  uint16_t y, count;              // a patch's first row and number of rows
  uint32_t len, got, crc, acked;  // total bytes, bytes received, CRC so far, bytes received at the last ack
  uint16_t rows;                  // complete rows so far
  uint8_t left, skip;             // PackBits: bytes the row still makes, data bytes before the next run
  bool new_pal;                   // patch comes with a new palette in pal
  lv_color_t pal[16];
  uint8_t piece[PZIP_MAX];  // a 16-colour picture's pzip piece as it comes
  uint8_t pn;
} xfer;
_Static_assert((PIC_MAX_W + 1) / 2 <= UINT8_MAX, "a row's bytes in left");

static uint32_t row_bytes(void) { return (pic.w + 1u) / 2u; }

// Bytes of row a PackBits code makes: the c + 1 after it below 128, else the one after it c - 126 times
static unsigned run_len(uint8_t c) { return c < 128 ? c + 1u : c - 126u; }

static void clear(void) { pic.state = PIC_NONE; }

void pic_reset(void) {
  if (xfer.kind == XF_PICTURE)
    clear();
  if (xfer.kind == XF_ANIM)
    anim_abort();
  xfer.kind = XF_NONE;
}

static int32_t ack(uint8_t status) {
  xfer.acked = xfer.got;
  return (int32_t) ((uint32_t) status << 24 | (uint32_t) xfer.id << 16 | (xfer.got & 0xFFFF));
}

static int32_t fail(uint8_t status) {
  pic_reset();
  return ack(status);
}

// A transfer's first message, the id at PIC_MSG_ID. Drops the one in progress.
static void start(const uint8_t *b, int n) {
  pic_reset();
  memset(&xfer, 0, sizeof xfer);
  xfer.id = n > PIC_MSG_ID ? b[PIC_MSG_ID] : 0;
}

static uint16_t get16be(const uint8_t *b) { return (uint16_t) (b[0] << 8 | b[1]); }  // JPEG's byte order

// One copy for anim.c and us: inlined, -O3 unrolls each into 440 bytes
__attribute__((noinline)) void pic_palette(lv_color_t *pal, const uint8_t *rgb) {
  for (int i = 0; i < 16; i++, rgb += 3)
    pal[i] = lv_color_make(rgb[0], rgb[1], rgb[2]);
}

// BEGIN, laid out in plink_keys.h: the head, then for PIC_PALETTE the palette
static int32_t begin(const uint8_t *b, int n) {
  start(b, n);
  clear();
  if (n <= PIC_BEGIN_FORMAT)
    return ack(PIC_BAD);
  uint8_t format = b[PIC_BEGIN_FORMAT];
  if (format == PIC_PALETTE ? n != PIC_BEGIN_HEAD + PIC_PAL_LEN : format != PIC_JPEG || n != PIC_BEGIN_HEAD)
    return ack(PIC_BAD);
  pic.format = format;
  pic.w = plink_get16(b + PIC_BEGIN_W);
  pic.h = plink_get16(b + PIC_BEGIN_H);
  uint32_t len = plink_number(b + PIC_BEGIN_SIZE, 4);
  if (!pic.w || !pic.h || pic.w > PIC_MAX_W || pic.h > PIC_MAX_H ||
      (pic.format == PIC_JPEG && (pic.w % 16 || pic.h % 8)))
    return ack(PIC_BAD_SIZE);
  if (len > PIC_POOL)
    return ack(PIC_TOO_BIG);
  if (pic.format == PIC_PALETTE)
    pic_palette(pic.pal, b + PIC_BEGIN_HEAD);
  pic.len = len;
  pic.off[0] = 0;
  pic.state = PIC_RECEIVING;
  xfer.kind = XF_PICTURE;
  xfer.len = len;
  xfer.left = (uint8_t) row_bytes();
  return -1;
}

// PATCH, laid out in plink_keys.h: the head, then a new palette or none. Stored after the picture, it must fit there.
static int32_t patch(const uint8_t *b, int n) {
  start(b, n);
  if (n != PIC_PATCH_HEAD && n != PIC_PATCH_HEAD + PIC_PAL_LEN)
    return ack(PIC_BAD);
  xfer.y = plink_get16(b + PIC_PATCH_Y);
  xfer.count = plink_get16(b + PIC_PATCH_COUNT);
  uint32_t len = plink_number(b + PIC_PATCH_SIZE, 4);
  if (pic.state != PIC_READY || pic.format != PIC_PALETTE)
    return ack(PIC_NO_PICTURE);
  if (xfer.y + xfer.count > pic.h)
    return ack(PIC_BAD_SIZE);
  if (len > PIC_POOL - pic.len)
    return ack(PIC_NO_ROOM);
  xfer.new_pal = n == PIC_PATCH_HEAD + PIC_PAL_LEN;
  if (xfer.new_pal)
    pic_palette(xfer.pal, b + PIC_PATCH_HEAD);
  xfer.kind = XF_PATCH;
  xfer.len = len;
  xfer.left = (uint8_t) row_bytes();
  return -1;
}

// Track n PackBits bytes that came in. Rows are packed on their own, so a run can't cross a row end.
static bool parse(const uint8_t *b, uint32_t n) {
  uint16_t rows = xfer.kind == XF_PICTURE ? pic.h : xfer.count;
  unsigned left = xfer.left, skip = xfer.skip;  // in registers for the loop, a failed transfer drops them anyway
  for (uint32_t i = 0; i < n;) {
    if (skip) {
      uint32_t k = skip < n - i ? skip : n - i;
      skip -= k;
      i += k;
      if (!skip && !left) {  // the row is done
        xfer.rows++;
        left = row_bytes();
        if (xfer.kind == XF_PICTURE && xfer.rows % 8 == 0 && xfer.rows < pic.h)
          pic.off[xfer.rows / 8] = (uint16_t) (xfer.got + i);
      }
      continue;
    }
    if (xfer.rows >= rows)
      return false;  // past the last row
    uint8_t c = b[i++];
    unsigned m = run_len(c);
    if (m > left)
      return false;  // a run across the end of a row
    left -= m;
    skip = c < 128 ? m : 1;
  }
  xfer.left = (uint8_t) left;
  xfer.skip = (uint8_t) skip;
  xfer.got += n;
  return true;
}

// ANIM_BEGIN: anim.c checks the header and keeps the records
static int32_t anim_begin(const uint8_t *b, int n) {
  start(b, n);
  int st = anim_open(b, n, &xfer.len);
  if (st >= 0)
    return ack((uint8_t) st);
  xfer.kind = XF_ANIM;
  return -1;
}

// pinflate into len contiguous bytes at out. Matches may reach hist bytes before it.
static uint32_t pinflate_flat(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t len, uint32_t hist) {
  return pinflate(in, n, out, len, len, len, hist);
}

// A 16-colour picture's or patch's pzip pieces, each inflated into place. False if one is bad.
static bool pieces(const uint8_t *b, int n) {
  uint8_t *dst = xfer.kind == XF_PATCH ? pool + pic.len : pool;
  for (int i = 0; i < n && xfer.got < xfer.len;) {
    int k = LV_MIN((xfer.pn ? PZIP_HEAD + xfer.piece[0] : 1) - xfer.pn, n - i);  // its length byte first
    memcpy(xfer.piece + xfer.pn, b + i, (size_t) k);
    xfer.pn = (uint8_t) (xfer.pn + k);
    i += k;
    if (xfer.piece[0] > PZIP_MAX - PZIP_HEAD)
      return false;
    if (xfer.pn < PZIP_HEAD + xfer.piece[0])
      continue;
    xfer.pn = 0;
    uint32_t made = plink_get16(xfer.piece + 1);
    if (!made || made > xfer.len - xfer.got ||
        !pinflate_flat(xfer.piece + PZIP_HEAD, xfer.piece[0], dst + xfer.got, made, (uint32_t) (dst - pool) + xfer.got))
      return false;
    xfer.crc = plink_crc32(xfer.crc, dst + xfer.got, made);
    if (!parse(dst + xfer.got, made))
      return false;
  }
  return true;
}

static int32_t data(const uint8_t *b, int n) {
  if (xfer.kind == XF_NONE)
    return -1;  // the rest of a failed transfer, the ESP already has our answer
  if (xfer.kind == XF_ANIM || pic.format == PIC_JPEG) {
    uint32_t k = LV_MIN(xfer.len - xfer.got, (uint32_t) n);
    if (xfer.kind == XF_ANIM)
      anim_bytes(xfer.got, b, k);
    else
      memcpy(pool + xfer.got, b, k);  // a JPEG, checked as a whole at the end
    xfer.crc = plink_crc32(xfer.crc, b, k);
    xfer.got += k;
  } else if (!pieces(b, n))
    return fail(PIC_BAD);
  return xfer.got - xfer.acked >= 1024 ? ack(PIC_PROGRESS) : -1;
}

// The packed row after the one at p. Its runs end at the row's end, parse() checked that.
static const uint8_t *next_row(const uint8_t *p) {
  for (uint32_t out = 0, rb = row_bytes(); out < rb; p += *p < 128 ? *p + 2u : 2u)
    out += run_len(*p);
  return p;
}

// Where packed row y starts, y == pic.h for the end
static const uint8_t *row_at(unsigned y) {
  if (y >= pic.h)
    return pool + pic.len;
  const uint8_t *p = pool + pic.off[y / 8];
  for (y %= 8; y; y--)
    p = next_row(p);
  return p;
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
  uint32_t plen = xfer.len, start = (uint32_t) (row_at(xfer.y) - pool),
           end = (uint32_t) (row_at(xfer.y + xfer.count) - pool), rest = pic.len - end;
  memmove(pool + start, pool + end, rest + plen);
  reverse(pool + start, pool + start + rest);
  reverse(pool + start + rest, pool + start + rest + plen);
  reverse(pool + start, pool + start + rest + plen);
  int32_t delta = (int32_t) plen - (int32_t) (end - start);
  const uint8_t *p = pool + start;  // the new rows' checkpoints, the later ones move by delta
  for (unsigned r = xfer.y; r < xfer.y + xfer.count; r++, p = next_row(p))
    if (r % 8 == 0)
      pic.off[r / 8] = (uint16_t) (p - pool);
  for (unsigned k = (xfer.y + xfer.count + 7u) / 8; k < (pic.h + 7u) / 8; k++)
    pic.off[k] = (uint16_t) (pic.off[k] + delta);
  pic.len = (uint32_t) ((int32_t) pic.len + delta);
  if (xfer.new_pal)
    memcpy(pic.pal, xfer.pal, sizeof pic.pal);
}

// Checks for a whole JPEG as pjpeg.h makes them. SOF0 at the picture's size, Y 2x1, Cb and Cr 1x1, a restart
// interval of one MCU row and the restart markers in order. Fills in where each MCU row starts.
static bool jpeg_rows(void) {
  const uint8_t *p = pool + 2, *end = pool + pic.len;
  bool sof = false, dri = false;
  if (pic.len < 4 || pool[0] != 0xFF || pool[1] != 0xD8 || pool[pic.len - 2] != 0xFF || pool[pic.len - 1] != 0xD9)
    return false;  // SOI ... EOI, a JPEG cut short has no EOI
  while (p + 4 <= end && p[0] == 0xFF) {
    unsigned len = get16be(p + 2);
    const uint8_t *s = p + 4;
    if (len < 2 || p + 2 + len > end)
      return false;
    if (p[1] == 0xC0)
      sof = len == 17 && s[0] == 8 && get16be(s + 1) == pic.h && get16be(s + 3) == pic.w && s[5] == 3 && s[7] == 0x21 &&
            s[10] == 0x11 && s[13] == 0x11;
    else if (p[1] == 0xDD)
      dri = len == 4 && get16be(s) == pic.w / 16;
    else if (p[1] == 0xDA) {  // the scan, MCU rows with RST0..RST7 in turn between them
      if (!sof || !dri)
        return false;
      unsigned rows = pic.h / 8u, k = 0;
      pic.off[k++] = (uint16_t) (p + 2 + len - pool);
      for (const uint8_t *q = p + 2 + len; q + 1 < end; q++)
        if (q[0] == 0xFF && q[1] >= 0xD0 && q[1] <= 0xD7) {
          if (k >= rows || q[1] != 0xD0 + (k - 1) % 8)
            return false;
          pic.off[k++] = (uint16_t) (q + 2 - pool);
        }
      return k == rows;
    }
    p += 2 + len;
  }
  return false;
}

static size_t jpeg_in(JDEC *jd, uint8_t *b, size_t n) {
  jpeg_t *j = jd->device;
  if (n > pic.len - j->pos)
    n = pic.len - j->pos;
  if (b)
    memcpy(b, pool + j->pos, n);
  j->pos += n;
  return n;
}

// TJpgDec output, one MCU. Copy the part inside the area we're drawing
__attribute__((optimize("O3"))) static int jpeg_out(JDEC *jd, void *bitmap, JRECT *r) {
  jpeg_t *j = jd->device;
  int w = r->right - r->left + 1;
  int x1 = LV_MAX(r->left, j->area.x1), x2 = LV_MIN(r->right, j->area.x2);
  int y1 = LV_MAX(r->top, j->area.y1), y2 = LV_MIN(r->bottom, j->area.y2);
  const uint16_t *s = (const uint16_t *) bitmap + (y1 - r->top) * w + (x1 - r->left);
  lv_color_t *d = j->buf + (y1 - j->area.y1) * j->stride + (x1 - j->area.x1);
  for (int y = y1; y <= y2; y++, s += w, d += j->stride)
    memcpy(d, s, (size_t) (x2 - x1 + 1) * sizeof *d);
  return 1;
}

static bool jpeg_prepare(jpeg_t *j) {
  j->pos = 0;
  return jd_prepare(&j->jd, jpeg_in, j->work, sizeof j->work, j) == JDR_OK && j->jd.msx == 2 && j->jd.msy == 1 &&
         j->jd.width == pic.w && j->jd.height == pic.h;
}

// whether TJpgDec takes the JPEG's tables, assumed yes when LVGL's heap is too full to check
static bool jpeg_check(void) {
  jpeg_t *j = lv_mem_alloc(sizeof *j);
  bool ok = !j || jpeg_prepare(j);
  lv_mem_free(j);
  return ok;
}

// A whole picture or patch is in: into place. False if the JPEG is no good, fail() drops it then.
static bool picture_done(bool jpeg) {
  if (jpeg && !jpeg_rows())
    return false;
  if (xfer.kind == XF_PATCH)
    splice();
  else
    pic.state = PIC_READY;
  if (jpeg && !jpeg_check())
    return false;
  if (xfer.kind == XF_PICTURE)
    pic.gen++;
  else if (shown && (xfer.count || xfer.new_pal)) {  // the same size and kind, only the rows that changed are drawn
    lv_area_t r = shown->coords;
    if (!xfer.new_pal) {
      r.y1 = (lv_coord_t) (r.y1 + xfer.y);
      r.y2 = (lv_coord_t) (r.y1 + xfer.count - 1);
    }
    lv_obj_invalidate_area(shown, &r);
  }
  return true;
}

// END, with the CRC-32 of the len bytes
static int32_t finish(uint32_t crc) {
  bool anim = xfer.kind == XF_ANIM, jpeg = xfer.kind == XF_PICTURE && pic.format == PIC_JPEG;
  uint16_t rows = xfer.kind == XF_PICTURE ? pic.h : xfer.count;
  if (xfer.got != xfer.len || (!anim && !jpeg && xfer.rows != rows))
    return fail(PIC_BAD);
  if (crc != xfer.crc)
    return fail(PIC_BAD_CRC);
  if (!(anim ? anim_close() : picture_done(jpeg)))
    return fail(PIC_BAD);
  int32_t a = ack(PIC_DONE);
  xfer.kind = XF_NONE;
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
      return xfer.kind == XF_NONE ? -1 : finish(plink_number(b, n));
    case K_ANIM_BEGIN:
      return anim_begin(b, n);
    case K_ANIM_CLEAR:
      if (anim_clear(b, n))  // it took the one coming in with it
        pic_reset();
      return -1;
    case K_PIC_CLEAR:
      pic_reset();
      clear();
      return -1;
    default:
      return -1;
  }
}

pic_state_t pic_state(void) { return pic.state; }
uint8_t pic_format(void) { return pic.format; }
uint8_t pic_percent(void) { return xfer.len ? (uint8_t) (xfer.got * 100 / xfer.len) : 0; }
uint16_t pic_width(void) { return pic.w; }
uint16_t pic_height(void) { return pic.h; }

// ---------------------------------------------------------------- drawing
// Pixels x1..x2 of the packed row at p into d, straight from its runs. Returns the next row.
__attribute__((optimize("O3"))) static const uint8_t *draw_row(const uint8_t *p, int x1, int x2, lv_color_t *d) {
  for (int x = 0, end = (int) row_bytes() * 2; x < end;) {  // x: the run's first pixel
    uint8_t c = *p++;
    int n = (int) run_len(c), a = LV_MAX(x, x1), b = LV_MIN(x + 2 * n - 1, x2);
    const uint8_t *s = c < 128 ? p + (a - x) / 2 : p;
    p += c < 128 ? n : 1;
    x += 2 * n;
    if (a > b)
      continue;
    if (a & 1) {
      *d++ = pic.pal[*s & 15];
      s += c < 128;
      a++;
    }
    if (c >= 128) {  // a repeat, the same two pixels over and over
      uint32_t two = pic.pal[*s >> 4].full | (uint32_t) pic.pal[*s & 15].full << 16;
      for (; a < b; a += 2, d += 2)
        memcpy(d, &two, 4);
    } else
      for (; a < b; a += 2, d += 2, s++) {  // a byte at a time, two pixels
        uint32_t two = pic.pal[*s >> 4].full | (uint32_t) pic.pal[*s & 15].full << 16;
        memcpy(d, &two, 4);
      }
    if (a == b)
      *d++ = pic.pal[*s >> 4];
  }
  return p;
}

__attribute__((optimize("O3"))) bool pic_read(const lv_area_t *a, lv_color_t *buf, int stride) {
  if (pic.state != PIC_READY || a->x1 < 0 || a->y1 < 0 || a->x2 >= pic.w || a->y2 >= pic.h || a->x1 > a->x2 ||
      a->y1 > a->y2)
    return false;
  if (pic.format == PIC_PALETTE) {
    const uint8_t *p = row_at((unsigned) a->y1);
    for (int y = a->y1; y <= a->y2; y++, buf += stride)
      p = draw_row(p, a->x1, a->x2, buf);
    return true;
  }
  jpeg_t *j = lv_mem_alloc(sizeof *j);  // the MCU rows a covers, each from the restart marker before it
  bool ok = j && jpeg_prepare(j);
  if (ok) {
    j->area = *a;
    j->buf = buf;
    j->stride = stride;
  }
  for (unsigned r = (unsigned) a->y1 / 8; ok && r <= (unsigned) a->y2 / 8; r++) {
    j->pos = pic.off[r];
    ok = jd_decomp_row(&j->jd, jpeg_out, r, (unsigned) a->x1, (unsigned) a->x2) == JDR_OK;
  }
  lv_mem_free(j);
  return ok;
}

void pic_round(lv_disp_drv_t *d, lv_area_t *a) {
  (void) d;
  // LVGL asking how high a strip may be, only while it draws. Below 8 lines it would find none and loop forever.
  bool probe = lv_disp_get_default()->rendering_in_progress;
  if (!shown || pic.format != PIC_JPEG || (probe ? a->y2 < 7 : !_lv_area_is_on(a, &shown->coords)))
    return;
  int top = shown->coords.y1;
  a->y1 = (lv_coord_t) (a->y1 - ((a->y1 - top) & 7));
  a->y2 = (lv_coord_t) (a->y2 + (7 - ((a->y2 - top) & 7)));
}

// The picture object draws straight into LVGL's draw buffer, only the part being drawn now
static void pic_event(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target(e);
  bool ours = pic.state == PIC_READY && (uintptr_t) lv_event_get_user_data(e) == pic.gen;
  if (lv_event_get_code(e) == LV_EVENT_DELETE && o == shown)
    shown = NULL;
  else if (lv_event_get_code(e) == LV_EVENT_COVER_CHECK) {
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
  if (pic.state != PIC_READY)
    return NULL;
  lv_obj_t *o = shown = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, pic.w, pic.h);
  if (pic.format == PIC_JPEG)  // its top on a multiple of 8 lines, so LVGL's strips start on its MCU rows
    lv_obj_align(o, LV_ALIGN_TOP_MID, 0, (PIC_MAX_H - pic.h) / 2 & ~7);
  else
    lv_obj_center(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(o, pic_event, LV_EVENT_ALL, (void *) (uintptr_t) pic.gen);
  return o;
}
