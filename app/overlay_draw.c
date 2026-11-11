// Draws the places, see overlay_int.h: their boxes, what to redraw, and the pixels through their views, turn, size,
// flip, tint and light, a frame's or a field's.
#include "overlay_int.h"
#include "picture.h"
#include "plink.h"
#include <string.h>

lv_obj_t *layer;
static uint8_t z[OVERLAY_PLACES];  // the places in the order they're drawn, the nearest last

// The picture's parts in front of places, see K_PIC_PART
typedef struct {
  int16_t x0, y0, x1, y1;  // on the layer, x1 and y1 just past it. Empty for none
  uint16_t colours;
  uint8_t depth;
} part_t;
static part_t parts[PIC_PARTS];

static int to255(int v) { return (v * 255 + FULL / 2) / FULL; }  // from 1/1000
static lv_opa_t opa8(int opa) { return (lv_opa_t) to255(opa); }
static int zoom(int size, int n) { return size == FULL ? n : LV_MAX(1, (n * size + FULL / 2) / FULL); }
// Its box px across or down at its sizes
static int zoomed(const look_t *l, int axis) { return zoom(l->size * LV_ABS(l->size_xy[axis]) / FULL, l->box[axis]); }
// Its box mirrored across or down: flipped, or by a size below 0, not both
static bool flipped(const look_t *l, int axis) {
  return !(l->view & (axis ? OVERLAY_VIEW_FLIP_Y : OVERLAY_VIEW_FLIP_X)) == (l->size_xy[axis] < 0);
}
// Its pivot across or down, in half px of its box at full size
static int pivot2(const place_t *p, int axis) {
  return (2 * shown(p, axis) * p->now.pivot[axis] + OVERLAY_PIVOT_MAX / 2) / OVERLAY_PIVOT_MAX;
}
static int64_t ceil_div(int64_t a, int64_t b) { return a >= 0 ? (a + b - 1) / b : -(-a / b); }  // b > 0
static int32_t fixed(int px) { return px * 65536; }                                             // to 1/65536 px

// The box it's drawn in, from x, y: the px whose middles are in its box at its sizes, flipped and turned
// round its pivot, and with OVERLAY_VIEW_SMOOTH the ones next to them, which get some of it
lv_area_t box(const place_t *p) {
  int64_t lo[2], hi[2], c[2];  // its edges from the pivot, and the pivot, in 1/65536 px
  for (int a = 0; a < 2; a++) {
    int f = shown(p, a), z = zoomed(&p->now, a), p2 = pivot2(p, a);
    int before = flipped(&p->now, a) ? 2 * f - p2 : p2;  // half px of it before the pivot
    lo[a] = f ? -(int64_t) before * 32768 * z / f : 0;
    hi[a] = f ? (int64_t) (2 * f - before) * 32768 * z / f : 0;
    c[a] = (int64_t) p2 * 32768;
  }
  if (p->now.deg) {  // the box round its turned corners
    int64_t cs = lv_trigo_cos(p->now.deg), sn = lv_trigo_sin(p->now.deg), x[2] = {lo[0], hi[0]}, y[2] = {lo[1], hi[1]};
    lo[0] = lo[1] = INT64_MAX;
    hi[0] = hi[1] = INT64_MIN;
    for (int k = 0; k < 4; k++) {
      int64_t u = x[k & 1], v = y[k >> 1], tx = (u * cs - v * sn) / LV_TRIGO_SIN_MAX,
              ty = (u * sn + v * cs) / LV_TRIGO_SIN_MAX;
      lo[0] = LV_MIN(lo[0], tx);
      hi[0] = LV_MAX(hi[0], tx);
      lo[1] = LV_MIN(lo[1], ty);
      hi[1] = LV_MAX(hi[1], ty);
    }
  }
  lv_coord_t e[4];
  int more = p->now.view & OVERLAY_VIEW_SMOOTH ? 1 : 0;
  for (int a = 0; a < 2; a++) {
    e[a] = (lv_coord_t) (ceil_div(c[a] + lo[a] - 32768, 65536) - more);
    e[a + 2] = (lv_coord_t) (ceil_div(c[a] + hi[a] - 32768, 65536) - 1 + more);
  }
  return (lv_area_t) {e[0], e[1], e[2], e[3]};
}

// Tile k of a box on the layer, on the screen: 0 where it is, then a screen to the left, up, and both. Those show
// the part over the right or bottom edge.
static lv_area_t tile(const lv_area_t *a, int k) {
  lv_area_t c = *a;
  lv_area_move(&c, layer->coords.x1 - (k & 1 ? PIC_MAX_W : 0), layer->coords.y1 - (k & 2 ? PIC_MAX_H : 0));
  return c;
}

// Redraws a at each tile with its bit set in tiles. Not with lv_obj_invalidate_area(), that adds 5 px all round
// for a transform the layer doesn't have.
static void redraw(const lv_area_t *a, unsigned tiles) {
  for (int k = 0; k < 4; k++) {
    if (!(tiles >> k & 1))
      continue;
    lv_area_t c = tile(a, k);
    if (_lv_area_intersect(&c, &c, &layer->coords))
      _lv_inv_area(lv_obj_get_disp(layer), &c);
  }
}

// What it's past the whole px across or down as it's drawn, in quarters, so one that creeps isn't drawn again each
// step
static int sub(const look_t *l, int axis) { return l->view & OVERLAY_VIEW_SMOOTH ? l->sub[axis] * 4 / MILLI : 0; }

// Whether l is drawn as was is: its speed isn't drawn, nor what's less than a pixel or a shade
static bool drawn_as(const look_t *l, const look_t *was) {
  return l->deg == was->deg && l->view == was->view && opa8(l->opa) == opa8(was->opa) &&
         zoomed(l, 0) == zoomed(was, 0) && zoomed(l, 1) == zoomed(was, 1) && flipped(l, 0) == flipped(was, 0) &&
         flipped(l, 1) == flipped(was, 1) && to255(l->light) == to255(was->light) &&
         to255(l->level) == to255(was->level) && l->phase == was->phase && l->depth == was->depth &&
         !memcmp(l->pan, was->pan, sizeof was->pan) && !memcmp(l->box, was->box, sizeof was->box) &&
         !memcmp(l->pivot, was->pivot, sizeof was->pivot) && sub(l, 0) == sub(was, 0) && sub(l, 1) == sub(was, 1);
}

// Redraws it as it is now in box b, see box(), from how it looked with was: where it was and where it is now are
// redrawn if they differ
void refresh(place_t *p, const look_t *was, const lv_area_t *b) {
  if (!layer)
    return;
  lv_area_t a = {0, 0, -1, -1};
  unsigned tiles = 0;
  if (opa8(p->now.opa) > LV_OPA_MIN && shown(p, 0) && shown(p, 1)) {
    a = (lv_area_t) {p->x + b->x1, p->y + b->y1, p->x + b->x2, p->y + b->y2};
    bool right = wraps(p, 0) && a.x2 >= PIC_MAX_W, below = wraps(p, 1) && a.y2 >= PIC_MAX_H;
    tiles = 1 | right << 1 | below << 2 | (right && below) << 3;
  }
  if (tiles == p->tiles && (!tiles || (!memcmp(&a, &p->drawn, sizeof a) && drawn_as(&p->now, was))))
    return;
  for (int k = 0; k < 4; k++) {
    unsigned o = p->tiles >> k & 1, n = tiles >> k & 1;
    if (o && n && _lv_area_is_on(&p->drawn, &a)) {  // a step on, both at once
      lv_area_t u;
      _lv_area_join(&u, &p->drawn, &a);
      redraw(&u, 1u << k);
    } else {
      redraw(&p->drawn, o << k);
      redraw(&a, n << k);
    }
  }
  p->drawn = a;
  p->tiles = (uint8_t) tiles;
}

void hide(place_t *p) {
  redraw(&p->drawn, p->tiles);
  p->tiles = 0;
}

// The places at more depth, then the bigger ones, are nearer and drawn in front. The others go by number.
void order(void) {
  uint8_t o[OVERLAY_PLACES];
  int64_t near[OVERLAY_PLACES];
  for (int k = 0; k < OVERLAY_PLACES; k++) {
    const place_t *p = &places[k];
    int64_t d = p->slot == OVERLAY_SLOTS ? INT64_MIN : ((int64_t) p->now.depth << 32) + p->now.size;
    int i = k;
    for (; i && near[i - 1] > d; i--) {
      o[i] = o[i - 1];
      near[i] = near[i - 1];
    }
    o[i] = (uint8_t) k;
    near[i] = d;
  }
  for (int i = 0; i < OVERLAY_PLACES; i++)
    if (o[i] != z[i]) {
      z[i] = o[i];
      redraw(&places[o[i]].drawn, places[o[i]].tiles);
    }
}

void redraw_place(const place_t *p) { redraw(&p->drawn, p->tiles); }

// Redraws what part q covers
static void redraw_part(const part_t *q) {
  if (layer)
    redraw(&(lv_area_t) {q->x0, q->y0, q->x1 - 1, q->y1 - 1}, 1);
}

void overlay_part(const uint8_t *b, int n) {
  if (n != PIC_PART_LEN || b[0] >= PIC_PARTS)
    return;
  part_t *q = &parts[b[0]];
  redraw_part(q);
  *q = (part_t) {plink_i16(b + PIC_PART_X0), plink_i16(b + PIC_PART_Y0),        plink_i16(b + PIC_PART_X1),
                 plink_i16(b + PIC_PART_Y1), plink_get16(b + PIC_PART_COLOURS), b[PIC_PART_DEPTH]};
  redraw_part(q);
}

void overlay_parts_clear(void) {
  for (int k = 0; k < PIC_PARTS; k++)
    redraw_part(&parts[k]);
  memset(parts, 0, sizeof parts);
}

// The box's px *lo..*hi, from *lo, that show frame px a..b of n when the box is m across
static void cover(int a, int b, int n, int m, bool flip, lv_coord_t *lo, lv_coord_t *hi) {
  if (m != n) {  // and half a pixel more either side for the rounding
    a = a * m / n - 1;
    b = (b + 1) * m / n;
  }
  a = LV_MAX(a, 0);
  b = LV_MIN(b, m - 1);
  int at = *lo;
  *lo = (lv_coord_t) (at + (flip ? m - 1 - b : a));
  *hi = (lv_coord_t) (at + (flip ? m - 1 - a : b));
}

// Redraws what a record changed of the frame p shows, a in frame pixels. All of it when it's turned, smooth or its
// rows are bound, or its frame isn't once at the top left of its box.
void changed(const place_t *p, const lv_area_t *a) {
  lv_area_t r = p->drawn;
  int k = slots[p->slot].scale, w = shown(p, 0), h = shown(p, 1);
  if (!p->now.deg && !p->now.pan[0] && !p->now.pan[1] && !p->rows &&
      !(p->now.view & (OVERLAY_VIEW_REPEAT_X | OVERLAY_VIEW_REPEAT_Y | OVERLAY_VIEW_SMOOTH)) && p->shape == NO_OFF) {
    if (a->x1 * k >= w || a->y1 * k >= h)
      return;  // past its box
    cover(a->x1 * k, LV_MIN((a->x2 + 1) * k, w) - 1, w, lv_area_get_width(&p->drawn), flipped(&p->now, 0), &r.x1,
          &r.x2);
    cover(a->y1 * k, LV_MIN((a->y2 + 1) * k, h) - 1, h, lv_area_get_height(&p->drawn), flipped(&p->now, 1), &r.y1,
          &r.y2);
  }
  redraw(&r, p->tiles);
}

// One of its colours tinted: shades of tint from black through it to white, as c is dark or light
static lv_color_t tinted(lv_color_t c, lv_color_t tint) {
  int k = lv_color_brightness(c);
  return k < 128 ? lv_color_mix(tint, lv_color_black(), (uint8_t) (k * 2))
                 : lv_color_mix(lv_color_white(), tint, (uint8_t) ((k - 128) * 2));
}

// The k-th of bits that's set, from 0
static int nth(unsigned bits, int k) {
  for (; k > 0; k--)
    bits &= bits - 1;
  return __builtin_ctz(bits);
}

// The colours p shows into pal, and into opa how opaque each is of 255 at opacity: its slot's with its cycling ones
// phase on, in 1/256 of a colour, each between the two it's then, in its tint and at light, 255 as they are, but for
// those that glow. A field's are the 16 it goes along, its phase where on them. Returns a bit for each that's clear.
static uint16_t palette(const place_t *p, int light, int phase, int opacity, lv_color_t *pal, lv_opa_t *opa) {
  const slot_t *s = &slots[p->slot];
  lv_color_t tint = lv_color_hex((uint32_t) p->tint);
  int n = s->field ? 0 : __builtin_popcount(s->cycles);
  uint16_t clear = 0;
  phase = n > 1 ? wrap(phase, n * 256) : 0;
  for (int i = 0; i < 16; i++) {
    int a = i, b = i, f = 0;
    if (phase && s->cycles >> i & 1) {
      int at = __builtin_popcount(s->cycles & ((1u << i) - 1)) + (phase >> 8);
      a = nth(s->cycles, at % n);
      b = nth(s->cycles, (at + 1) % n);
      f = phase & 255;
    }
    lv_color_t c = f ? lv_color_mix(s->pal[b], s->pal[a], (uint8_t) f) : s->pal[a];
    int k = f ? (colour_opa(s, a) * (256 - f) + colour_opa(s, b) * f) >> 8 : colour_opa(s, a);
    if (!(s->glows >> a & 1)) {
      if (p->tint != NO_TINT)
        c = tinted(c, tint);
      if (light != 255)
        c = light < 255 ? lv_color_mix(c, s->shade, (uint8_t) light)
                        : lv_color_mix(lv_color_white(), c, (uint8_t) LV_MIN(light - 255, 255));
    }
    pal[i] = c;
    opa[i] = (lv_opa_t) ((opacity * k + 127) / 255);
    clear |= (uint16_t) (!opa[i] << i);
  }
  return clear;
}

bool overlay_look(int place, overlay_look_t *l) {
  if (place < 0 || place >= OVERLAY_PLACES || places[place].slot == OVERLAY_SLOTS)
    return false;
  const place_t *p = &places[place];
  const slot_t *s = &slots[p->slot];
  *l = (overlay_look_t) {.x = p->x,
                         .y = p->y,
                         .pan = {p->now.pan[0], p->now.pan[1]},
                         .w = p->now.box[0],
                         .h = p->now.box[1],
                         .deg = p->now.deg,
                         .size = p->now.size,
                         .size_xy = {p->now.size_xy[0], p->now.size_xy[1]},
                         .pivot = {p->now.pivot[0], p->now.pivot[1]},
                         .opa = p->now.opa,
                         .light = p->now.light,
                         .view = p->view,
                         .tiles = p->tiles,
                         .box = p->drawn,
                         .still = s->still};
  palette(p, to255(p->now.light), p->now.phase, LV_OPA_COVER, l->pal, l->colour_opa);
  l->depth = p->now.depth;
  for (int i = 0; i < OVERLAY_PLACES; i++)
    if (z[i] == place)
      l->order = (uint8_t) i;
  return true;
}

// ---------------------------------------------------------------- the pixels

// a / b for a >= 0 and b > 0, a shift when b is a px in 1/65536, as it is for a place at its own size
static int32_t over(int32_t a, int32_t b) { return b == 65536 ? a >> 16 : a / b; }

// Narrows i0..i1 to the i where 0 <= u + i * du < n
static void span(int32_t u, int32_t du, int32_t n, int *i0, int *i1) {
  int lo = 0, hi = -1;
  if (du > 0) {
    lo = u < 0 ? (int) over(du - 1 - u, du) : 0;
    hi = u < n ? (int) over(n - 1 - u, du) : -1;
  } else if (du < 0) {
    lo = u >= n ? (int) over(u - n - du, -du) : 0;
    hi = u >= 0 ? (int) over(u, -du) : -1;
  } else if (u >= 0 && u < n) {
    hi = *i1;
  }
  *i0 = LV_MAX(*i0, lo);
  *i1 = LV_MIN(*i1, hi);
}

// Most of a drawn frame is clear. How many of the n pixels of a frame's row from px x on, each the next one (step 1)
// or the one before (-1), go as whole bytes that are clear, two pixels each: both in clear, and four bytes at a time
// where they're the frame's own clear colour, 15. None unless x is the first of its byte that way.
__attribute__((always_inline)) static inline int clear_bytes(const uint8_t *line, int x, int step, int n,
                                                             uint16_t clear) {
  if ((x & 1) != (step < 0))
    return 0;
  const uint8_t *at = line + (x >> 1);
  int k = 0;
  if (clear >> 15)
    for (uint32_t four; n - k >= 8; k += 8, at += 4 * step) {
      memcpy(&four, step > 0 ? at : at - 3, sizeof four);
      if (four != UINT32_MAX)
        break;
    }
  for (; n - k >= 2 && (clear >> (*at >> 4) & clear >> (*at & 15) & 1); k += 2, at += step) {
  }
  return k;
}

// The step clear_bytes() takes along a frame's row when pixels du, dv apart are a frame px apart along one, else 0
static int along(int32_t du, int32_t dv, int sh) { return dv ? 0 : du == 1 << sh ? 1 : du == -(1 << sh) ? -1 : 0; }

// n pixels of frame f into d, pixel i from u + i * du, v + i * dv in 1/65536 px of the frame drawn at scale. sh
// takes that to frame pixels, rb is f's bytes per row. The colours in clear aren't drawn, the others at opa, or as
// they are if opa is NULL. fragment() calls it both ways, so the plain one is its own compile, without the mix.
__attribute__((always_inline)) static inline void pixels(lv_color_t *d, int n, const uint8_t *f, int rb, int32_t u,
                                                         int32_t v, int32_t du, int32_t dv, int sh,
                                                         const lv_color_t *pal, const lv_opa_t *opa, uint16_t clear) {
  int step = along(du, dv, sh);
  for (; n > 0; n--, d++, u += du, v += dv) {
    int x = u >> sh;
    const uint8_t *line = f + (v >> sh) * rb;
    uint8_t q = line[x >> 1];
    q = x & 1 ? q & 15 : q >> 4;
    if (!(clear >> q & 1)) {
      *d = !opa || opa[q] > LV_OPA_MAX ? pal[q] : lv_color_mix(pal[q], *d, opa[q]);
    } else if (step) {  // and the clear bytes after it
      int k = clear_bytes(line, x + step, step, n - 1, clear);
      n -= k;
      d += k;
      u += k * du;
    }
  }
}

// The picture's parts nearer than p that reach into c on the screen, a bit each
static unsigned nearer(const place_t *p, const lv_area_t *c) {
  int lx = layer->coords.x1, ly = layer->coords.y1;
  unsigned in = 0;
  for (int k = 0; k < PIC_PARTS; k++) {
    const part_t *q = &parts[k];
    in |= (unsigned) (q->depth > p->now.depth && q->x0 + lx <= c->x2 && q->x1 + lx > c->x1 && q->y0 + ly <= c->y2 &&
                      q->y1 + ly > c->y1)
          << k;
  }
  return in;
}

// Which of the n pixels from x on screen row y one of the picture's parts in `in`, a bit each, covers: bit i of cover
// for x + i. NULL if none do.
static const uint32_t *covered(unsigned in, int y, int x, int n, uint32_t *cover) {
  bool any = false;
  for (int k = 0; in >> k; k++) {
    const part_t *q = &parts[k];
    if (!(in >> k & 1))
      continue;
    int lx = layer->coords.x1, ly = layer->coords.y1;
    int a = LV_MAX(q->x0 + lx, x), b = LV_MIN(q->x1 + lx, x + n);  // b just past it
    if (y < q->y0 + ly || y >= q->y1 + ly || a >= b)
      continue;
    if (!any)
      memset(cover, 0, (size_t) (n + 31) / 32 * sizeof *cover);
    any = true;
    if (q->colours == PIC_ALL_COLOURS)
      for (int i = a - x; i < b - x; i++)
        cover[i / 32] |= 1u << i % 32;
    else
      pic_in(y, a, b - 1, q->colours, cover, x);
  }
  return any ? cover : NULL;
}

static bool cover_at(const uint32_t *cover, int i) { return cover[i / 32] >> i % 32 & 1; }

bool overlay_covered(int place, int x, int y) {
  uint32_t cover[1];
  if (!layer || place < 0 || place >= OVERLAY_PLACES || places[place].slot == OVERLAY_SLOTS)
    return false;
  x += layer->coords.x1;
  y += layer->coords.y1;
  lv_area_t at = {(lv_coord_t) x, (lv_coord_t) y, (lv_coord_t) x, (lv_coord_t) y};
  const uint32_t *c = covered(nearer(&places[place], &at), y, x, 1, cover);
  return c && cover_at(c, 0);
}

// x / m for a channel's most m, by multiplying, as the processor's divide takes up to 12 cycles: exact up to m * m,
// all channel() gives it. LV_UDIV255() is exact up to 63 * 255.
_Static_assert(LV_COLOR_DEPTH == 16, "a channel's most is 31 or 63");
static unsigned div_most(unsigned x, unsigned m) { return m == 63 ? x * 2081u >> 17 : x * 2115u >> 16; }

// A channel of s over one of d, c and e, at opacity a, m the channel's most
static inline unsigned channel(unsigned mode, unsigned c, unsigned e, unsigned a, unsigned m) {
  unsigned t = LV_UDIV255(c * a);  // what it adds or screens
  return mode == OVERLAY_BLEND_ADD      ? LV_MIN(m, e + t)
         : mode == OVERLAY_BLEND_SCREEN ? e + t - div_most(e * t, m)
                                        : div_most(e * (m - LV_UDIV255((m - c) * a)), m);
}

// s over d at opacity a, as OVERLAY_BLEND_* mode says
static lv_color_t blend(unsigned mode, lv_color_t s, lv_color_t d, lv_opa_t a) {
  if (mode == OVERLAY_BLEND_MIX)
    return a > LV_OPA_MAX ? s : lv_color_mix(s, d, a);
  unsigned r = channel(mode, LV_COLOR_GET_R(s), LV_COLOR_GET_R(d), a, 31),
           g = channel(mode, LV_COLOR_GET_G(s), LV_COLOR_GET_G(d), a, 63),
           b = channel(mode, LV_COLOR_GET_B(s), LV_COLOR_GET_B(d), a, 31);
  LV_COLOR_SET_R(d, r);
  LV_COLOR_SET_G(d, g);
  LV_COLOR_SET_B(d, b);
  return d;
}

// A field as a place shows it, see OVERLAY_FIELD_*
typedef struct {
  uint8_t kind, a;
  uint16_t b;
  int32_t w, h;    // its box, px
  int32_t pan[2];  // where a NOISE starts, 1/256 px
  int level;       // 1/1000
  int phase;       // 1/256 colour of the 16
  uint32_t seed;   // a NOISE's, the place's own
  int cells;       // and how many of its first layer's bumps on it's the same again
  struct {         // and for each of its layers the bumps at the corners of the square it was in last
    int32_t x, y;
    uint8_t at[4];
  } in[OVERLAY_NOISE_LAYERS];
} field_t;

static int slow(int f) { return f * f * (3 * 256 - 2 * f) >> 16; }  // 0..256 of the way, slow at both ends

// How much of a field there is at u, v of its box, in 1/65536 px, 0..255
static int field_at(field_t *f, int32_t u, int32_t v) {
  int32_t s = u / f->w, t = v / f->h;  // 0..65535 across and down
  switch (f->kind) {
    case OVERLAY_FIELD_LINEAR: {
      int k = (f->a & OVERLAY_LINEAR_DOWN ? t : s) >> 8;
      return f->a & OVERLAY_LINEAR_MIRROR ? 255 - LV_ABS(2 * k - 255) : k;
    }
    case OVERLAY_FIELD_RADIAL: {
      int32_t x = s - 32768, y = t - 32768;
      uint32_t out = (uint32_t) (x * x) + (uint32_t) (y * y);  // 1 << 30 at the ellipse
      return out >= 1u << 30 ? 0 : 255 - (int) isqrt(out >> 14);
    }
    case OVERLAY_FIELD_CONE: {
      int32_t off = LV_ABS(2 * t - 65536), wide = f->a * 256 + ((65536 - f->a * 256) * (s >> 4) >> 12);
      return off >= wide ? 0 : 255 - off * 255 / wide;
    }
  }
  int sum = 0;  // NOISE: each layer between the bumps of its squares, half as much of each as of the one before
  int32_t x = (u >> 8) - f->pan[0], y = (v >> 8) - f->pan[1];
  for (int k = 0; k < f->a; k++) {
    int32_t side = LV_MAX((f->b << 8) >> k, 256), xi = floor_div(x, side), yi = floor_div(y, side);
    if (xi != f->in[k].x || yi != f->in[k].y) {
      f->in[k].x = xi;
      f->in[k].y = yi;
      for (int c = 0, round = f->cells << k; c < 4; c++)
        f->in[k].at[c] =
            (uint8_t) (hash32((f->seed + (uint32_t) k) ^ ((uint32_t) wrap(xi + (c & 1), round) * 0x9E3779B1u) ^
                              ((uint32_t) wrap(yi + (c >> 1), round) * 0x85EBCA77u)) >>
                       24);
    }
    const uint8_t *at = f->in[k].at;
    int fx = slow((x - xi * side) * 256 / side), fy = slow((y - yi * side) * 256 / side);
    int top = at[0] + ((at[1] - at[0]) * fx >> 8), bottom = at[2] + ((at[3] - at[2]) * fx >> 8);
    sum += (top + ((bottom - top) * fy >> 8)) >> (k + 1);
  }
  return sum * 256 / (256 - (256 >> f->a));
}

// The colour k of 255 along a field's 16 is, at its level and with its phase, and in *a how opaque
static lv_color_t ramp(const field_t *f, int k, const lv_color_t *pal, const lv_opa_t *opa, int *a) {
  int at = (((LV_MIN(k * f->level / FULL, 255) * 3855) >> 8) + f->phase) & 4095;  // in 1/256 colour, 16 round
  int i = at >> 8, j = (i + 1) & 15, part = at & 255;
  *a = part ? (opa[i] * (256 - part) + opa[j] * part) >> 8 : opa[i];
  return part ? lv_color_mix(pal[j], pal[i], (uint8_t) part) : pal[i];
}

// How pixels() draws a place that isn't just its frame's pixels as they are: how it blends, its soft edges and box
// width, the top and bottom of the run being drawn, in px of its box, where the first pixel is in its box and the
// step to the next, in 1/65536 px. A field's, or for OVERLAY_VIEW_SMOOTH the frame rectangle's size and which of its
// sides, a SIDE_* each, have more of the frame past them.
typedef struct {
  unsigned mode;
  const uint8_t *soft;
  int w, top, bottom;
  int32_t bu, bv, du, dv;
  field_t *field;
  bool smooth;
  uint8_t more;
  int16_t tw, th;
} shade_t;
enum { SIDE_X = 1, SIDE_TOP = 2, SIDE_BOTTOM = 4 };

// Of *a, 0 to 256, what shows px from an edge, in 1/65536 px inside it
static void edge(int *a, int32_t in, int px) {
  if (px && in < fixed(px))
    *a = LV_MIN(*a, in <= 0 ? 0 : (int) (in / px >> 8));
}

// How much shows of the pixel at bu, bv of its box, 0 to 256, by its soft edges
static int soft_at(const shade_t *s, int32_t bu, int32_t bv) {
  int a = 256;
  edge(&a, bu, plink_get16(s->soft));
  edge(&a, fixed(s->w) - bu, plink_get16(s->soft + 2));
  edge(&a, bv - fixed(s->top), plink_get16(s->soft + 4));
  edge(&a, fixed(s->bottom) - bv, plink_get16(s->soft + 6));
  return a;
}

// soft, or NULL when none of a row is by an edge: it's level on the screen at bv down its box, in the run from top
// to bottom, and only those two edges are soft
static const uint8_t *soft_by(const uint8_t *soft, int32_t bv, int top, int bottom) {
  if (!soft || plink_get16(soft) || plink_get16(soft + 2))
    return soft;
  int32_t under = fixed(plink_get16(soft + 4)), over = fixed(plink_get16(soft + 6));
  return bv - fixed(top) < under || fixed(bottom) - bv < over ? soft : NULL;
}

// The frame rectangle at f where u, v falls between four of its pixels, in 1/2^sh of one: their colours by how near
// each is and how opaque, and in *a how opaque that is. Past the rectangle it's clear, but where there's more of the
// frame it's as the pixel at that side. Where the four are one colour, as most of a drawn frame is, it's that one.
static lv_color_t smooth_at(const shade_t *s, const uint8_t *f, int rb, int32_t u, int32_t v, int sh,
                            const lv_color_t *pal, const lv_opa_t *opa, int *a) {
  u -= 1 << (sh - 1);
  v -= 1 << (sh - 1);
  int x = u >> sh, y = v >> sh, fx = u >> (sh - 8) & 255, fy = v >> (sh - 8) & 255, n = 0;
  uint8_t q[4];
  uint16_t near[4];
  bool one = true;
  for (int k = 0; k < 4; k++) {
    int xx = x + (k & 1), yy = y + (k >> 1), part = (k & 1 ? fx : 256 - fx) * (k >> 1 ? fy : 256 - fy) >> 8;
    bool past = (xx < 0 || xx >= s->tw ? !(s->more & SIDE_X) : false) || (yy < 0 && !(s->more & SIDE_TOP)) ||
                (yy >= s->th && !(s->more & SIDE_BOTTOM));
    if (!part)
      continue;
    xx = LV_CLAMP(0, xx, s->tw - 1);
    yy = LV_CLAMP(0, yy, s->th - 1);
    uint8_t c = f[yy * rb + (xx >> 1)];
    c = xx & 1 ? c & 15 : c >> 4;
    near[n] = (uint16_t) (past ? 0 : part);
    q[n] = c;
    one &= !past && c == q[0];
    n++;
  }
  if (one && n) {
    *a = opa[q[0]];
    return pal[q[0]];
  }
  uint32_t all = 0, c[3] = {0};
  for (int k = 0; k < n; k++) {
    uint32_t part = (uint32_t) near[k] * opa[q[k]];
    all += part;
    c[0] += part * LV_COLOR_GET_R(pal[q[k]]);
    c[1] += part * LV_COLOR_GET_G(pal[q[k]]);
    c[2] += part * LV_COLOR_GET_B(pal[q[k]]);
  }
  lv_color_t out = lv_color_black();
  *a = (int) LV_MIN(all >> 8, 255);
  if (all) {
    LV_COLOR_SET_R(out, c[0] / all);
    LV_COLOR_SET_G(out, c[1] / all);
    LV_COLOR_SET_B(out, c[2] / all);
  }
  return out;
}

// pixels() as s says, the first of them pixel i of those s has
static void shaded(lv_color_t *d, int n, const uint8_t *f, int rb, int32_t u, int32_t v, int32_t du, int32_t dv, int sh,
                   const lv_color_t *pal, const lv_opa_t *opa, uint16_t clear, const shade_t *s, int i) {
  int32_t bu = s->bu + i * s->du, bv = s->bv + i * s->dv;
  int step = s->field || s->smooth ? 0 : along(du, dv, sh);
  for (; n > 0; n--, d++, u += du, v += dv, bu += s->du, bv += s->dv) {
    lv_color_t c;
    int a;
    if (s->field) {
      c = ramp(s->field, field_at(s->field, u, v), pal, opa, &a);
    } else if (s->smooth) {
      c = smooth_at(s, f, rb, u, v, sh, pal, opa, &a);
    } else {
      int x = u >> sh;
      const uint8_t *line = f + (v >> sh) * rb;
      uint8_t q = line[x >> 1];
      q = x & 1 ? q & 15 : q >> 4;
      c = pal[q];
      a = clear >> q & 1 ? 0 : opa[q];
      if (!a && step) {  // and the clear bytes after it
        int k = clear_bytes(line, x + step, step, n - 1, clear);
        n -= k;
        d += k;
        u += k * du;
        bu += k * s->du;
        bv += k * s->dv;
      }
    }
    if (a && s->soft)
      a = a * soft_at(s, bu, bv) >> 8;
    if (a)
      *d = blend(s->mode, c, *d, (lv_opa_t) a);
  }
}

// pixels() for i0..i1 of row, pixel i from u + i * du, v + i * dv, or shaded() as s says. Those cover has, bit i - i0,
// are left as they are.
__attribute__((always_inline)) static inline void runs(lv_color_t *row, int i0, int i1, const uint32_t *cover,
                                                       const uint8_t *f, int rb, int32_t u, int32_t v, int32_t du,
                                                       int32_t dv, int sh, const lv_color_t *pal, const lv_opa_t *opa,
                                                       uint16_t clear, const shade_t *s) {
  for (int i = i0; i <= i1;) {
    int e = i1;
    if (cover) {
      for (; i <= i1 && cover_at(cover, i - i0); i++) {
      }
      for (e = i; e < i1 && !cover_at(cover, e + 1 - i0); e++) {
      }
    }
    if (i <= i1 && s)
      shaded(row + i, e - i + 1, f, rb, u + i * du, v + i * dv, du, dv, sh, pal, opa, clear, s, i - i0);
    else if (i <= i1)
      pixels(row + i, e - i + 1, f, rb, u + i * du, v + i * dv, du, dv, sh, pal, opa, clear);
    i = e + 1;
  }
}

// How a place's box on the screen goes back into the box at full size, in the clip c: pixel i of row y, from c's
// left, is at u + i * du, v + i * dv in 1/65536 of a px of the box, u and v from back(). sh takes those to frame
// pixels. Each pixel's middle goes back through the turn, the sizes and the flip, round the pivot. Upright
// that's the same u for each row, and the step from one pixel to the next is the box's, so a size that doesn't change
// the box looks the same.
typedef struct {
  lv_area_t c;
  int sh, oy, zh;  // the screen row of the box's top at full size, and its height at its size
  int pv[2];       // its pivot, in half px of the box at full size
  int32_t fw, fh;  // the box at full size
  int32_t sub[2];  // what it's past the whole px with OVERLAY_VIEW_SMOOTH, 1/65536 px
  bool fx, fy, turned;
  int32_t u, du, dv, qy, ry;  // upright u, and v from qy and ry: fh * 32768 = qy * zh + ry
  int32_t sv;                 // and what sub[1] takes of every row's v then
  int32_t cs, sn;             // the turn's, 1/32767
  int64_t dx, kx, ky;         // dx: the clip's first pixel from the pivot, in 1/65536 px
} brush_t;

// On its own: what it works them out with is off the stack again before the pixels
__attribute__((noinline)) static void brush(brush_t *b, const place_t *p, const lv_area_t *a, const lv_area_t *c) {
  const slot_t *s = &slots[p->slot];
  lv_area_t at = box(p);  // a from the box's top left at full size
  *b = (brush_t) {.c = *c,
                  .sh = s->field ? 16 : 15 + s->scale,
                  .oy = a->y1 - at.y1,
                  .zh = zoomed(&p->now, 1),
                  .pv = {pivot2(p, 0), pivot2(p, 1)},
                  .fw = shown(p, 0),
                  .fh = shown(p, 1),
                  .sub = {sub(&p->now, 0) << 14, sub(&p->now, 1) << 14},
                  .fx = flipped(&p->now, 0),
                  .fy = flipped(&p->now, 1),
                  .turned = p->now.deg != 0};
  b->dx = (int64_t) (2 * (c->x1 - a->x1 + at.x1) + 1 - b->pv[0]) * 32768 - b->sub[0];
  int zw = zoomed(&p->now, 0);
  if (!b->turned) {
    b->qy = (b->fh << 15) / b->zh;
    b->ry = (b->fh << 15) % b->zh;
    b->sv = (int32_t) ((int64_t) b->sub[1] * b->fh / b->zh);
    b->du = (65536 * b->fw) / zw;
    b->u = (int32_t) (b->dx * b->fw / zw);
    if (b->fx) {
      b->u = -b->u;
      b->du = -b->du;
    }
    b->u += b->pv[0] << 15;
    return;
  }
  b->cs = lv_trigo_cos(p->now.deg);
  b->sn = lv_trigo_sin(p->now.deg);
  b->kx = (int64_t) LV_TRIGO_SIN_MAX * zw;
  b->ky = (int64_t) LV_TRIGO_SIN_MAX * b->zh;
  b->du = (int32_t) ((int64_t) b->cs * 65536 * b->fw / b->kx);
  b->dv = (int32_t) (-(int64_t) b->sn * 65536 * b->fh / b->ky);
  if (b->fx)
    b->du = -b->du;
  if (b->fy)
    b->dv = -b->dv;
}

// Where the first pixel of screen row y goes back to
static void back(const brush_t *b, int y, int32_t *u, int32_t *v) {
  int k = 2 * (y - b->oy) + 1 - b->pv[1];  // its middle from the pivot, in half px
  if (b->turned) {
    int64_t dy = (int64_t) k * 32768 - b->sub[1];
    *u = (int32_t) ((b->dx * b->cs + dy * b->sn) * b->fw / b->kx);
    *v = (int32_t) ((dy * b->cs - b->dx * b->sn) * b->fh / b->ky);
    *u = (b->fx ? -*u : *u) + (b->pv[0] << 15);
  } else {
    *u = b->u;
    *v = k * b->qy + k * b->ry / b->zh - b->sv;
  }
  *v = (b->fy ? -*v : *v) + (b->pv[1] << 15);
}

// The box's px the clip goes back to, *u0..*u1 across and *v0..*v1 down, and a px more each side for the rounding
static void extent(const brush_t *b, int *u0, int *u1, int *v0, int *v1) {
  int32_t ua, va, ub, vb;
  back(b, b->c.y1, &ua, &va);
  back(b, b->c.y2, &ub, &vb);
  int64_t n = lv_area_get_width(&b->c) - 1, eu = n * b->du, ev = n * b->dv;
  int64_t u = LV_MIN(ua, ub), v = LV_MIN(va, vb), uu = LV_MAX(ua, ub), vv = LV_MAX(va, vb);
  *u0 = (int) (LV_MIN(u, u + eu) >> 16) - 1;
  *u1 = (int) (LV_MAX(uu, uu + eu) >> 16) + 1;
  *v0 = (int) (LV_MIN(v, v + ev) >> 16) - 1;
  *v1 = (int) (LV_MAX(vv, vv + ev) >> 16) + 1;
}

// The frames n px long from pan on that px lo..hi of its box show, *t0..*t1: round and round with repeat, else the
// one at pan. None if *t0 > *t1.
static void repeats(int pan, int n, int lo, int hi, bool repeat, int *t0, int *t1) {
  *t0 = repeat ? floor_div(lo - pan, n) : pan > hi || pan + n <= lo;
  *t1 = repeat ? floor_div(hi - pan, n) : 0;
}

// What paint() draws a place with
typedef struct {
  const place_t *p;
  const brush_t *b;
  lv_draw_ctx_t *ctx;
  const lv_color_t *pal;
  const lv_opa_t *opa;  // NULL when every colour is drawn as it is, or not at all
  uint16_t clear;
  uint16_t over;         // the picture's parts that may be over it where it's drawn now, see nearer()
  int phase;             // where a field is on its colours, 1/256 of one
  int x0, x1;            // its frame's repeats across
  unsigned mirror;       // the ways every other one is mirrored: 1 across, 2 down
  unsigned blend;        // OVERLAY_BLEND_*
  const uint8_t *soft;   // its soft edges, NULL for none
  field_t *field;        // what it shows if that's a field
  bool shades;           // by shaded(): it blends, has soft edges, is smooth or a field
  const row_t *row;      // what's bound to its rows makes of the one being drawn, NULL with none bound
  uint32_t in;           // the piece of a still that's unpacked, where its record is
  const uint8_t *shape;  // what its box shows: its script's shape, see script_shape(), NULL for all of it as one run
  int runs;
} paint_t;

// How opaque p is of 255, with a row's share of it
static int opacity(const place_t *p, int32_t share) {
  return LV_MIN(to255((int) ((int64_t) p->now.opa * share / FULL)), 255);
}

// Rectangle r of the frame at f, rb bytes a row: what the box shows of it where d's brush puts it. The frame's top
// left is at ou, ov in the box in 1/65536 px for d's first repeat across, and `apart` further on for each one after
// it, every other one mirrored across if d says so, and all of them down with `down`. A field's box is its
// rectangle. A row that's bound has its frame shifted and sized across round the pivot.
__attribute__((optimize("O3"))) static void fragment(const paint_t *d, const uint8_t *f, int rb, const lv_area_t *r,
                                                     int32_t ou, int32_t ov, bool down, int32_t apart) {
  const brush_t *b = d->b;
  const place_t *p = d->p;
  const slot_t *s = &slots[p->slot];
  lv_draw_ctx_t *ctx = d->ctx;
  const row_t *at = d->row;
  bool smooth = !d->field && p->now.view & OVERLAY_VIEW_SMOOTH;
  int sh = b->sh, stride = lv_area_get_width(ctx->buf_area), n = lv_area_get_width(&b->c);
  int32_t w = lv_area_get_width(r) << sh, h = lv_area_get_height(r) << sh, half = smooth ? 1 << (sh - 1) : 0;
  int32_t dv = down ? -b->dv : b->dv, pivot = b->pv[0] << 15;
  lv_color_t *row = (lv_color_t *) ctx->buf + (b->c.y1 - ctx->buf_area->y1) * stride + (b->c.x1 - ctx->buf_area->x1);
  uint32_t cover[(PIC_MAX_W + 31) / 32];
  // more of the frame above and below it: its repeats, and a still's pieces in the bands next to its own
  bool tiled = p->now.view & OVERLAY_VIEW_REPEAT_Y, above = r->y1 && r->y1 % OVERLAY_PIECE_ROWS == 0,
       below = r->y2 < s->h - 1 && (r->y2 + 1) % OVERLAY_PIECE_ROWS == 0;
  shade_t sd = {.mode = d->blend,
                .w = shown(p, 0),
                .du = b->du,
                .dv = b->dv,
                .field = d->field,
                .smooth = smooth,
                .more = (uint8_t) ((p->now.view & OVERLAY_VIEW_REPEAT_X ? SIDE_X : 0) |
                                   (tiled || above ? SIDE_TOP : 0) | (tiled || below ? SIDE_BOTTOM : 0)),
                .tw = (int16_t) lv_area_get_width(r),
                .th = (int16_t) lv_area_get_height(r)};
  for (int y = b->c.y1; y <= b->c.y2; y++, row += stride) {
    int32_t bu, bv, du = b->du;
    back(b, y, &bu, &bv);
    int32_t along_it = bu, v = bv - ov;  // along_it: where the row's first pixel is along its frames
    if (down)
      v = ((int32_t) s->h << sh) - 1 - v;
    v -= r->y1 << sh;
    if (!b->turned && (v + half < 0 || v + half >= h + 2 * half))
      continue;  // upright the whole row is past it
    if (at) {
      along_it = pivot + (int32_t) (((int64_t) bu - pivot - (int64_t) at->shift * 65536 / MILLI) * FULL / at->size);
      du = (int32_t) ((int64_t) du * FULL / at->size);
    }
    int i0 = 0, i1 = n - 1;
    span(v + half, dv, h + 2 * half, &i0, &i1);
    // where in the box its first and last pixels are across, the lesser first
    int32_t ua = bu + i0 * b->du, ub = bu + i1 * b->du, lo = LV_MIN(ua, ub), hi = LV_MAX(ua, ub);
    for (int j = 0, x = 0; j < d->runs && i0 <= i1; j++) {
      // the run's columns from x on, its top and the row past its bottom, in px of the box and no further
      const uint8_t *run = d->shape ? d->shape + 6 * j : NULL;
      int left = x;
      x = run ? LV_CLAMP(x, x + plink_i16(run), OVERLAY_BOX_MAX) : shown(p, 0);
      if (hi < fixed(left) || lo >= fixed(x))
        continue;  // none of them in its columns
      int top = run ? LV_CLAMP(0, plink_i16(run + 2), OVERLAY_BOX_MAX) : 0;
      int bottom = run ? LV_CLAMP(top, plink_i16(run + 4), OVERLAY_BOX_MAX) : shown(p, 1);
      int e0 = i0, e1 = i1;
      span(bu - fixed(left), b->du, fixed(x - left), &e0, &e1);
      span(bv - fixed(top), b->dv, fixed(bottom - top), &e0, &e1);
      if (e0 > e1)
        continue;
      sd.soft = b->dv ? d->soft : soft_by(d->soft, bv, top, bottom);
      sd.top = top;
      sd.bottom = bottom;
      // the repeats those pixels are in, and with smooth the ones next to them too, which they get some of
      int32_t fa = along_it + e0 * du - ou, fb = along_it + e1 * du - ou;
      int t0 = d->x0, t1 = d->x1;
      if (apart) {
        t0 = LV_MAX(t0, d->x0 + floor_div(LV_MIN(fa, fb), apart) - smooth);
        t1 = LV_MIN(t1, d->x0 + floor_div(LV_MAX(fa, fb), apart) + smooth);
      }
      for (int t = t0; t <= t1; t++) {
        int32_t u = along_it - ou - (t - d->x0) * apart, dt = du;
        if (d->mirror & 1 && t & 1) {
          u = ((int32_t) s->w << sh) - 1 - u;
          dt = -dt;
        }
        u -= r->x1 << sh;
        int s0 = e0, s1 = e1;
        span(u + half, dt, w + 2 * half, &s0, &s1);
        if (s0 > s1)
          continue;
        const uint32_t *cv = d->over ? covered(d->over, y, b->c.x1 + s0, s1 - s0 + 1, cover) : NULL;
        const lv_color_t *pal = d->pal;
        const lv_opa_t *opa = d->opa;
        uint16_t clear = d->clear;
        if (d->shades) {
          sd.bu = bu + s0 * b->du;
          sd.bv = bv + s0 * b->dv;
          runs(row, s0, s1, cv, f, rb, u, v, dt, dv, sh, pal, opa, clear, &sd);
        } else if (dv && opa) {
          runs(row, s0, s1, cv, f, rb, u, v, dt, dv, sh, pal, opa, clear, NULL);
        } else if (dv) {
          runs(row, s0, s1, cv, f, rb, u, v, dt, dv, sh, pal, NULL, clear, NULL);
        } else if (opa) {  // one row of f, the one v is in
          runs(row, s0, s1, cv, f + (v >> sh) * rb, 0, u, 0, dt, 0, sh, pal, opa, clear, NULL);
        } else {
          runs(row, s0, s1, cv, f + (v >> sh) * rb, 0, u, 0, dt, 0, sh, pal, NULL, clear, NULL);
        }
      }
    }
  }
}

// Rectangle r of the frame at f in all its repeats across, the one down at oy in the box
static void paint_row(const paint_t *d, const uint8_t *f, int rb, const lv_area_t *r, int oy, bool down) {
  const place_t *p = d->p;
  int fw = slots[p->slot].w * slots[p->slot].scale;
  fragment(d, f, rb, r, fixed(p->now.pan[0] + d->x0 * fw), fixed(oy), down, fixed(fw));
}

// All of a field's box as d paints it. On its own, so a frame has no room for it on the stack.
__attribute__((noinline)) static void paint_field(paint_t *d) {
  const place_t *p = d->p;
  const uint8_t *at = records(&slots[p->slot]);
  field_t field = {.kind = at[OVERLAY_FIELD_KIND],
                   .a = at[OVERLAY_FIELD_A],
                   .b = plink_get16(at + OVERLAY_FIELD_B),
                   .w = LV_MAX(shown(p, 0), 1),
                   .h = LV_MAX(shown(p, 1), 1),
                   .pan = {p->now.pan[0] * 256, p->now.pan[1] * 256},
                   .level = p->now.level,
                   .phase = d->phase,
                   .seed = seed_of(p),
                   .cells = noise_cells(plink_get16(at + OVERLAY_FIELD_B))};
  for (int k = 0; k < OVERLAY_NOISE_LAYERS; k++)
    field.in[k].x = INT32_MIN;
  d->field = &field;
  d->x0 = d->x1 = 0;
  fragment(d, NULL, 0, &(lv_area_t) {0, 0, (lv_coord_t) (field.w - 1), (lv_coord_t) (field.h - 1)}, 0, 0, false, 0);
  d->field = NULL;
}

// What d's place shows in the rows its brush has: its field, or its frame where pan puts it, once or round and
// round. A still's pieces are unpacked into *piece, from LVGL's heap the first time.
static void tiles(paint_t *d, uint8_t **piece) {
  const place_t *p = d->p;
  const slot_t *s = &slots[p->slot];
  uint8_t v = p->now.view;
  if (s->field) {
    paint_field(d);
    return;
  }
  int k = s->scale, fh = s->h * k, u0, u1, v0, v1, y0, y1;
  extent(d->b, &u0, &u1, &v0, &v1);
  v0 = LV_MAX(v0, 0);
  v1 = LV_MIN(v1, shown(p, 1) - 1);
  u0 = LV_MAX(u0, 0);
  u1 = LV_MIN(u1, shown(p, 0) - 1);
  if (d->row) {  // all the row shows of it, sized and shifted round the pivot
    int pv = d->b->pv[0] / 2, at = d->row->shift / MILLI;
    u0 = pv + (-pv - at - 1) * FULL / d->row->size - 1;
    u1 = pv + (shown(p, 0) - pv - at + 1) * FULL / d->row->size + 1;
  }
  repeats(p->now.pan[0], s->w * k, u0, u1, v & OVERLAY_VIEW_REPEAT_X, &d->x0, &d->x1);
  repeats(p->now.pan[1], fh, v0, v1, v & OVERLAY_VIEW_REPEAT_Y, &y0, &y1);
  lv_area_t whole = {0, 0, s->w - 1, s->h - 1};
  for (int t = y0; t <= y1 && d->x0 <= d->x1; t++) {
    int oy = p->now.pan[1] + t * fh;
    bool down = d->mirror & 2 && t & 1;
    if (!s->still) {
      paint_row(d, place_frame(p), (int) row_bytes(s), &whole, oy, down);
      continue;
    }
    // the frame's rows this one shows of v0..v1
    int lo = LV_MAX(v0, oy) - oy, hi = LV_MIN(v1, oy + fh - 1) - oy;
    if (down) {
      int l = fh - 1 - hi;
      hi = fh - 1 - lo;
      lo = l;
    }
    uint32_t from = s->sheet ? place_at(p) : 0;  // a sheet's frame, the pieces after where it starts
    from += s->sheet ? record_len(s, from) : 0;
    for (uint32_t at = from; at < s->len && !(s->sheet && heads(s, at)); at += record_len(s, at)) {
      lv_area_t r = record_area(s, at);
      if (r.y2 < lo / k || r.y1 > hi / k || lv_area_get_width(&r) <= 0)
        continue;
      if (!*piece && !(*piece = lv_mem_alloc(OVERLAY_PIECE_MAX)))
        return;
      if (d->in != at)
        piece_rows(s, at, *piece);
      d->in = at;
      paint_row(d, *piece, lv_area_get_width(&r) / 2, &r, oy, down);
    }
  }
}

// tiles() a row of b's at a time, each as what's bound to p's rows makes it: its frame where that puts it, in the
// row's light, colours and opacity. Worked out here, before the pixels, so the two aren't on the stack together.
__attribute__((noinline)) static void paint_rows(paint_t *d, brush_t *b, uint8_t **piece) {
  const place_t *p = d->p;
  const lv_color_t *pal = d->pal;
  const lv_opa_t *opa = d->opa;
  uint16_t clear = d->clear, row_clear = 0;
  lv_color_t row_pal[16];
  lv_opa_t row_opa[16];
  row_t row, made = ROW_AS_IS;  // made: the one row_pal is for
  d->row = &row;
  for (int y = b->c.y1, last = b->c.y2; y <= last; y++) {
    int32_t bu, bv;
    b->c.y1 = b->c.y2 = (lv_coord_t) y;
    back(b, y, &bu, &bv);
    script_row(p, bv >> 16, &row);
    int phase = p->now.phase + (int) ((int64_t) row.phase * 256 / MILLI);
    bool own = row.opa == FULL && row.light == FULL && !row.phase;  // in the place's own
    if (!own && (row.opa != made.opa || row.light != made.light || row.phase != made.phase)) {
      row_clear = palette(p, to255((int) ((int64_t) p->now.light * row.light / FULL)), phase, opacity(p, row.opa),
                          row_pal, row_opa);
      made = row;
    }
    d->pal = own ? pal : row_pal;
    d->opa = own ? opa : row_opa;
    d->clear = own ? clear : row_clear;
    d->phase = phase & 4095;
    tiles(d, piece);
  }
  d->row = NULL;
}

// p's box at a on the screen, the part in the area being drawn
static void paint(const place_t *p, lv_draw_ctx_t *ctx, const lv_area_t *a, uint8_t **piece) {
  lv_area_t c;
  if (!_lv_area_intersect(&c, a, ctx->clip_area))
    return;
  const slot_t *s = &slots[p->slot];
  lv_color_t pal[16];
  lv_opa_t opa[16];
  uint8_t v = p->now.view;
  brush_t b;
  brush(&b, p, a, &c);
  paint_t d = {.p = p,
               .b = &b,
               .ctx = ctx,
               .pal = pal,
               .opa = opa,
               .clear = palette(p, to255(p->now.light), p->now.phase, opacity(p, FULL), pal, opa),
               .over = (uint16_t) nearer(p, &c),
               .phase = p->now.phase,
               .blend = v & OVERLAY_VIEW_BLEND,
               .soft = script_soft(p),
               .in = UINT32_MAX,
               .runs = 1};
  d.shape = script_shape(p, &d.runs);
  bool rows = p->rows && !b.turned;  // upright only
  d.shades = d.blend != OVERLAY_BLEND_MIX || d.soft || s->field || v & OVERLAY_VIEW_SMOOTH;
  bool plain = !d.shades && !rows;  // every colour drawn as it is, or not at all
  for (int i = 0; i < 16; i++)
    plain &= opa[i] == LV_OPA_TRANSP || opa[i] > LV_OPA_MAX;
  if (plain)
    d.opa = NULL;
  d.mirror = v & OVERLAY_VIEW_MIRROR ? (v & OVERLAY_VIEW_REPEAT_X ? 1 : 0) | (v & OVERLAY_VIEW_REPEAT_Y ? 2 : 0) : 0;
  if (rows)
    paint_rows(&d, &b, piece);
  else
    tiles(&d, piece);
}

// Draws every place where it is, the nearest last
static void layer_event(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_DELETE) {
    layer = NULL;
    for (int k = 0; k < OVERLAY_PLACES; k++)
      places[k].tiles = 0;
  } else if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN) {
    uint8_t *piece = NULL;
    for (int i = 0; i < OVERLAY_PLACES; i++) {
      const place_t *p = &places[z[i]];
      for (int k = 0; k < 4; k++)
        if (p->tiles >> k & 1) {
          lv_area_t a = tile(&p->drawn, k);
          paint(p, lv_event_get_draw_ctx(e), &a, &piece);
        }
    }
    lv_mem_free(piece);
  }
}

lv_obj_t *overlay_show(lv_obj_t *parent) {
  if (layer)
    lv_obj_del(layer);
  layer = lv_obj_create(parent);
  lv_obj_remove_style_all(layer);
  lv_obj_set_size(layer, PIC_MAX_W, PIC_MAX_H);
  lv_obj_clear_flag(layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(layer, layer_event, LV_EVENT_ALL, NULL);
  lv_obj_update_layout(layer);
  bool moves = false;
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (places[k].slot != OVERLAY_SLOTS) {
      update(&places[k]);
      moves |= moving(&places[k]);
    }
  order();
  if (moves)
    wake_mover();
  wake();
  return layer;
}

void draw_init(void) {
  for (int k = 0; k < OVERLAY_PLACES; k++)
    z[k] = (uint8_t) k;
}
