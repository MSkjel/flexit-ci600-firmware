// Draws the places, see overlay_int.h: their boxes, what to redraw, and the pixels through turn, size and flip.
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

static lv_opa_t opa8(int opa) { return (lv_opa_t) ((opa * 255 + FULL / 2) / FULL); }
static int zoom(int size, int n) { return size == FULL ? n : LV_MAX(1, (n * size + FULL / 2) / FULL); }

// The box it's drawn in, from x, y: the frame at its size, or the box round that turned, centred on the frame
lv_area_t box(const place_t *p) {
  const slot_t *s = &slots[p->slot];
  int fw = s->w * s->scale, fh = s->h * s->scale, d = p->now.deg;
  int zw = zoom(p->now.size, fw), zh = zoom(p->now.size, fh), w = zw, h = zh;
  if (d) {
    int32_t c = LV_ABS(lv_trigo_cos((int16_t) d)), sn = LV_ABS(lv_trigo_sin((int16_t) d));  // 1/32767
    w = (int) ((zw * c + zh * sn + LV_TRIGO_SIN_MAX - 1) / LV_TRIGO_SIN_MAX);
    h = (int) ((zw * sn + zh * c + LV_TRIGO_SIN_MAX - 1) / LV_TRIGO_SIN_MAX);
  }
  int x = (fw - w) / 2, y = (fh - h) / 2;
  return (lv_area_t) {x, y, x + w - 1, y + h - 1};
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

// Redraws it as it is now in box b, see box(), from how it looked with was: where it was and where it is now are
// redrawn if they differ
void refresh(place_t *p, const look_t *was, const lv_area_t *b) {
  if (!layer)
    return;
  lv_area_t a = {0, 0, -1, -1};
  unsigned tiles = 0;
  if (opa8(p->now.opa) > LV_OPA_MIN) {
    a = (lv_area_t) {p->x + b->x1, p->y + b->y1, p->x + b->x2, p->y + b->y2};
    bool right = wraps(p, 0) && a.x2 >= PIC_MAX_W, below = wraps(p, 1) && a.y2 >= PIC_MAX_H;
    tiles = 1 | right << 1 | below << 2 | (right && below) << 3;
  }
  const slot_t *s = &slots[p->slot];
  int fw = s->w * s->scale, fh = s->h * s->scale;
  if (tiles == p->tiles &&
      (!tiles || (!memcmp(&a, &p->drawn, sizeof a) && p->now.deg == was->deg && p->now.flip == was->flip &&
                  opa8(p->now.opa) == opa8(was->opa) && zoom(p->now.size, fw) == zoom(was->size, fw) &&
                  zoom(p->now.size, fh) == zoom(was->size, fh))))
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

// The places at more depth, then those bigger from their size map, are nearer and drawn in front. The others go by
// number.
void order(void) {
  uint8_t o[OVERLAY_PLACES];
  int64_t near[OVERLAY_PLACES];
  for (int k = 0; k < OVERLAY_PLACES; k++) {
    const place_t *p = &places[k];
    int64_t d = p->slot == OVERLAY_SLOTS ? INT64_MIN : ((int64_t) p->depth << 32) + mapped(p, OVERLAY_SIZE);
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

void set_depth(place_t *p, uint8_t depth) {
  if (depth != p->depth) {
    p->depth = depth;
    redraw(&p->drawn, p->tiles);  // other parts may be in front of it
  }
}

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

// Redraws what a record changed of the frame p shows, a in frame pixels. All of it when it's turned.
void changed(const place_t *p, const lv_area_t *a) {
  lv_area_t r = p->drawn;
  if (!p->now.deg) {
    const slot_t *s = &slots[p->slot];
    int k = s->scale;
    cover(a->x1 * k, (a->x2 + 1) * k - 1, s->w * k, lv_area_get_width(&p->drawn), p->now.flip & OVERLAY_FLIP_X, &r.x1,
          &r.x2);
    cover(a->y1 * k, (a->y2 + 1) * k - 1, s->h * k, lv_area_get_height(&p->drawn), p->now.flip & OVERLAY_FLIP_Y, &r.y1,
          &r.y2);
  }
  redraw(&r, p->tiles);
}

bool overlay_look(int place, overlay_look_t *l) {
  if (place < 0 || place >= OVERLAY_PLACES || places[place].slot == OVERLAY_SLOTS)
    return false;
  const place_t *p = &places[place];
  *l = (overlay_look_t) {.x = p->x,
                         .y = p->y,
                         .deg = p->now.deg,
                         .size = p->now.size,
                         .opa = p->now.opa,
                         .flip = p->flip,
                         .tiles = p->tiles,
                         .box = p->drawn,
                         .frame = place_frame(p),
                         .pal = slots[p->slot].pal};
  l->depth = p->depth;
  for (int i = 0; i < OVERLAY_PLACES; i++)
    if (z[i] == place)
      l->order = (uint8_t) i;
  return true;
}

// ---------------------------------------------------------------- the pixels

// Narrows i0..i1 to the i where 0 <= u + i * du < n
static void span(int32_t u, int32_t du, int32_t n, int *i0, int *i1) {
  int lo = 0, hi = -1;
  if (du > 0) {
    lo = u < 0 ? (int) ((du - 1 - u) / du) : 0;
    hi = u < n ? (int) ((n - 1 - u) / du) : -1;
  } else if (du < 0) {
    lo = u >= n ? (int) ((u - n - du) / -du) : 0;
    hi = u >= 0 ? (int) (u / -du) : -1;
  } else if (u >= 0 && u < n) {
    hi = *i1;
  }
  *i0 = LV_MAX(*i0, lo);
  *i1 = LV_MIN(*i1, hi);
}

// n pixels of frame f into d, pixel i from u + i * du, v + i * dv in 1/65536 px of the frame drawn at scale. sh
// takes that to frame pixels, rb is f's bytes per row. paint() calls it twice so the opaque one is its own
// compile, without the mix.
__attribute__((always_inline)) static inline void pixels(lv_color_t *d, int n, const uint8_t *f, int rb, int32_t u,
                                                         int32_t v, int32_t du, int32_t dv, int sh,
                                                         const lv_color_t *pal, uint8_t transp, lv_opa_t opa) {
  for (; n > 0; n--, d++, u += du, v += dv) {
    int x = u >> sh;
    uint8_t q = f[(v >> sh) * rb + (x >> 1)];
    q = x & 1 ? q & 15 : q >> 4;
    if (q != transp)
      *d = opa > LV_OPA_MAX ? pal[q] : lv_color_mix(pal[q], *d, opa);
  }
}

// Which of the n pixels from x on screen row y a part of the picture nearer than p covers: bit i of cover for x + i.
// NULL if none do.
static const uint32_t *covered(const place_t *p, int y, int x, int n, uint32_t *cover) {
  bool any = false;
  for (int k = 0; k < PIC_PARTS; k++) {
    const part_t *q = &parts[k];
    if (q->depth <= p->depth)
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
  const uint32_t *c = covered(&places[place], y + layer->coords.y1, x + layer->coords.x1, 1, cover);
  return c && cover_at(c, 0);
}

// pixels() for i0..i1 of row, pixel i from u + i * du, v + i * dv. Those cover has, bit i - i0, are left as they are.
__attribute__((always_inline)) static inline void runs(lv_color_t *row, int i0, int i1, const uint32_t *cover,
                                                       const uint8_t *f, int rb, int32_t u, int32_t v, int32_t du,
                                                       int32_t dv, int sh, const lv_color_t *pal, uint8_t transp,
                                                       lv_opa_t opa) {
  for (int i = i0; i <= i1;) {
    int e = i1;
    if (cover) {
      for (; i <= i1 && cover_at(cover, i - i0); i++) {
      }
      for (e = i; e < i1 && !cover_at(cover, e + 1 - i0); e++) {
      }
    }
    if (i <= i1)
      pixels(row + i, e - i + 1, f, rb, u + i * du, v + i * dv, du, dv, sh, pal, transp, opa);
    i = e + 1;
  }
}

// p's box at a on the screen, the part in the area being drawn. Each pixel's middle goes back through the turn, the
// size and the flip into the frame. Unturned that's the same for each row, and the step from one pixel to the next
// is the box's, so a size that doesn't change the box looks the same.
__attribute__((optimize("O3"))) static void paint(const place_t *p, lv_draw_ctx_t *ctx, const lv_area_t *a) {
  lv_area_t c;
  if (!_lv_area_intersect(&c, a, ctx->clip_area))
    return;
  const slot_t *s = &slots[p->slot];
  const uint8_t *f = place_frame(p);
  int rb = s->w / 2, sh = 15 + s->scale, fw = s->w * s->scale, fh = s->h * s->scale;
  int zw = zoom(p->now.size, fw), zh = zoom(p->now.size, fh), w = lv_area_get_width(a), h = lv_area_get_height(a);
  int stride = lv_area_get_width(ctx->buf_area), n = lv_area_get_width(&c), x0 = c.x1 - a->x1;
  lv_color_t *row = (lv_color_t *) ctx->buf + (c.y1 - ctx->buf_area->y1) * stride + (c.x1 - ctx->buf_area->x1);
  lv_opa_t opa = opa8(p->now.opa);
  bool fx = p->now.flip & OVERLAY_FLIP_X, fy = p->now.flip & OVERLAY_FLIP_Y;
  uint32_t cover[(PIC_MAX_W + 31) / 32];
  if (!p->now.deg) {
    int32_t qy = (fh << 15) / zh, ry = (fh << 15) % zh, du = (65536 * fw) / zw;  // fh * 32768 = qy * zh + ry
    int32_t u = (int32_t) ((int64_t) (2 * x0 + 1 - zw) * 32768 * fw / zw);
    if (fx) {
      u = -u;
      du = -du;
    }
    u += fw << 15;
    int i0 = 0, i1 = n - 1;
    span(u, du, fw << 16, &i0, &i1);
    for (int y = c.y1; y <= c.y2 && i0 <= i1; y++, row += stride) {
      int k = 2 * (y - a->y1) + 1 - zh;
      int32_t v = k * qy + k * ry / zh;
      v = (fy ? -v : v) + (fh << 15);
      if (v < 0 || v >= fh << 16)
        continue;
      const uint8_t *fr = f + (v >> sh) * rb;
      const uint32_t *cv = covered(p, y, c.x1 + i0, i1 - i0 + 1, cover);
      if (opa > LV_OPA_MAX)
        runs(row, i0, i1, cv, fr, 0, u, 0, du, 0, sh, s->pal, s->transp, LV_OPA_COVER);
      else
        runs(row, i0, i1, cv, fr, 0, u, 0, du, 0, sh, s->pal, s->transp, opa);
    }
    return;
  }
  int32_t cs = lv_trigo_cos(p->now.deg), sn = lv_trigo_sin(p->now.deg);  // 1/32767
  int64_t kx = (int64_t) LV_TRIGO_SIN_MAX * zw, ky = (int64_t) LV_TRIGO_SIN_MAX * zh,
          dx = (int64_t) (2 * x0 + 1 - w) * 32768;
  int32_t du = (int32_t) ((int64_t) cs * 65536 * fw / kx), dv = (int32_t) (-(int64_t) sn * 65536 * fh / ky);
  if (fx)
    du = -du;
  if (fy)
    dv = -dv;
  for (int y = c.y1; y <= c.y2; y++, row += stride) {
    int64_t dy = (int64_t) (2 * (y - a->y1) + 1 - h) * 32768;
    int32_t u = (int32_t) ((dx * cs + dy * sn) * fw / kx), v = (int32_t) ((dy * cs - dx * sn) * fh / ky);
    u = (fx ? -u : u) + (fw << 15);
    v = (fy ? -v : v) + (fh << 15);
    int i0 = 0, i1 = n - 1;
    span(u, du, fw << 16, &i0, &i1);
    span(v, dv, fh << 16, &i0, &i1);
    if (i0 > i1)
      continue;
    const uint32_t *cv = covered(p, y, c.x1 + i0, i1 - i0 + 1, cover);
    if (opa > LV_OPA_MAX)
      runs(row, i0, i1, cv, f, rb, u, v, du, dv, sh, s->pal, s->transp, LV_OPA_COVER);
    else
      runs(row, i0, i1, cv, f, rb, u, v, du, dv, sh, s->pal, s->transp, opa);
  }
}

// Draws every place where it is, the nearest last
static void layer_event(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_DELETE) {
    layer = NULL;
    for (int k = 0; k < OVERLAY_PLACES; k++)
      places[k].tiles = 0;
  } else if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN) {
    for (int i = 0; i < OVERLAY_PLACES; i++) {
      const place_t *p = &places[z[i]];
      for (int k = 0; k < 4; k++)
        if (p->tiles >> k & 1) {
          lv_area_t a = tile(&p->drawn, k);
          paint(p, lv_event_get_draw_ctx(e), &a);
        }
    }
  }
}

lv_obj_t *overlay_show(lv_obj_t *parent) {
  if (layer)
    lv_obj_del(layer);
  layer = lv_obj_create(parent);
  lv_obj_remove_style_all(layer);
  lv_obj_set_size(layer, PIC_MAX_W, PIC_MAX_H);
  lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
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
