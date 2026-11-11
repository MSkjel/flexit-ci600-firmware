// Screensaver overlays, see overlay.h. Motion is in overlay_motion.c, drawing in overlay_draw.c.
#include "overlay_int.h"
#include "input.h"
#include "pdeflate.h"
#include "picture.h"
#include "plink.h"
#include <string.h>

// Every slot's frame (w * h / 2 bytes, an animated one's) and then its records, the frames places of their own, their
// scripts and what's bound to them, packed from the start. Freeing one moves what's behind it down.
uint8_t pool[OVERLAY_POOL];
static unsigned used;

slot_t slots[OVERLAY_SLOTS];
place_t places[OVERLAY_PLACES];
static uint8_t in = OVERLAY_SLOTS;  // the slot a transfer is coming in for
static lv_timer_t *timer;
static uint16_t gone_bits;  // places gone after their scripts' END, for OVERLAY_GONE
static uint8_t gone_tag[OVERLAY_PLACES];
_Static_assert(OVERLAY_PLACES <= 16, "a bit per place in gone_bits");
_Static_assert(OVERLAY_PAL_LEN - PIC_PAL_LEN == sizeof slots[0].opa, "the opacities as they come");

static uint32_t frame_bytes(const slot_t *s) { return s->still ? 0 : row_bytes(s) * s->h; }
uint8_t *records(const slot_t *s) { return pool + s->off + frame_bytes(s); }
const uint8_t *place_frame(const place_t *p) { return pool + (p->frame == NO_OFF ? slots[p->slot].off : p->frame); }
uint16_t place_at(const place_t *p) { return p->own ? p->at : slots[p->slot].at; }

// ---------------------------------------------------------------- the pool

// Takes n bytes, ending fade-outs early to make room. NO_OFF if they still don't fit.
uint16_t take(uint32_t n) {
  for (int k = 0; k < OVERLAY_PLACES && n > OVERLAY_POOL - used; k++)
    if (places[k].slot != OVERLAY_SLOTS && places[k].leaving)
      gone(&places[k]);
  if (n > OVERLAY_POOL - used)
    return NO_OFF;
  used += n;
  return (uint16_t) (used - n);
}

uint16_t take_for(const place_t *p, uint32_t n) {
  uint16_t off = take(n);
  if (off == NO_OFF || p->slot != OVERLAY_SLOTS)
    return off;
  give(off, n);
  return NO_OFF;
}

static void moved_down(uint16_t *at, uint16_t off, uint32_t n) {
  if (*at != NO_OFF && *at > off)
    *at -= (uint16_t) n;
}

// Frees the n bytes at off and moves everything behind down
void give(uint16_t off, uint32_t n) {
  memmove(pool + off, pool + off + n, used - off - n);
  used -= n;
  for (int i = 0; i < OVERLAY_SLOTS; i++)
    moved_down(&slots[i].off, off, n);
  for (int i = 0; i < OVERLAY_PLACES; i++) {
    moved_down(&places[i].frame, off, n);
    moved_down(&places[i].script, off, n);
    moved_down(&places[i].roam, off, n);
  }
  binds_moved(off, n);
}

// ---------------------------------------------------------------- places

// How a new place starts: still, upright, full size, opaque, in its own colours, nearest, on the whole screen, its
// frame once in its box
static void reset(place_t *p) {
  moves_drop(p);
  for (int c = 0; c <= FADE; c++)
    ch_set(p, c, scales(c) || c == FADE ? FULL : 0);
  ch_set(p, OVERLAY_PIVOT_X, OVERLAY_PIVOT_MAX / 2);
  ch_set(p, OVERLAY_PIVOT_Y, OVERLAY_PIVOT_MAX / 2);
  ch_set(p, OVERLAY_DEPTH, OVERLAY_NEAREST);
  p->now = (look_t) {.size = FULL,
                     .size_xy = {FULL, FULL},
                     .speed = FULL,
                     .opa = FULL,
                     .light = FULL,
                     .level = FULL,
                     .pivot = {OVERLAY_PIVOT_MAX / 2, OVERLAY_PIVOT_MAX / 2},
                     .depth = OVERLAY_NEAREST};
  p->view = 0;
  p->rows = 0;
  p->shape = p->soft = NO_OFF;
  p->own = false;
  p->tint = NO_TINT;
  p->drifting = 0;
  p->area.x0 = p->area.y0 = 0;
  p->area.x1 = PIC_MAX_W;
  p->area.y1 = PIC_MAX_H;
  p->area.edge[0] = p->area.edge[1] = OVERLAY_EDGE_SCREEN;
  p->leaving = STAY;
}

static void drop_slot(int i);

// Whether slot i has places, or with shown, places that show its frame
static bool has_places(int i, bool shown) {
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (places[k].slot == i && (!shown || !places[k].own))
      return true;
  return false;
}

// Takes p away, and its slot with it if that's leaving and this was its last place
static void drop_place(place_t *p) {
  int i = p->slot;
  if (i == OVERLAY_SLOTS)
    return;
  hide(p);
  script_drop(p);
  binds_drop(p);
  roam_set(p, &(roam_t) {0});
  if (p->frame != NO_OFF)
    give(p->frame, frame_bytes(&slots[i]));
  p->slot = OVERLAY_SLOTS;
  p->frame = NO_OFF;
  reset(p);
  if (slots[i].leaving && !has_places(i, false))
    drop_slot(i);
}

static void drop_slot(int i) {
  slot_t *s = &slots[i];
  if (s->off == NO_OFF)
    return;
  s->leaving = false;  // not again from drop_place()
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (places[k].slot == i)
      drop_place(&places[k]);
  give(s->off, frame_bytes(s) + s->len);
  s->off = NO_OFF;
  s->ready = false;
  if (i == in)
    in = OVERLAY_SLOTS;
}

// It has faded out and goes, telling the ESP if its script ended it
void gone(place_t *p) {
  int k = (int) (p - places);
  if (p->leaving == LEAVE_GONE) {
    gone_bits |= (uint16_t) (1u << k);
    gone_tag[k] = script_tag(p);
  }
  drop_place(p);
}

// Fades out over ms and goes, at the next step at the soonest: not while frames step. One that isn't shown goes
// at once.
void leave(place_t *p, uint16_t ms, uint8_t how) {
  p->leaving = how;
  if (!layer) {
    gone(p);
    return;
  }
  ch_glide(p, FADE, 0, ms ? ms : 1, 0);
  wake_mover();
}

// Its places fade out over ms, and it goes with the last. One already leaving goes on as it was, with its OVERLAY_GONE.
static void clear_slot(int i, uint16_t ms) {
  if (!ms || !has_places(i, false)) {
    drop_slot(i);
    return;
  }
  slots[i].leaving = true;
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (places[k].slot == i && !places[k].leaving)
      leave(&places[k], ms, LEAVE);
}

void overlay_clear_all(void) {
  for (int i = 0; i < OVERLAY_SLOTS; i++)
    drop_slot(i);
  gone_bits = 0;
  script_clear();
}

bool overlay_clear(const uint8_t *b, int n) {
  uint32_t v = plink_number(b, n), what = v & 0xFF;
  uint16_t ms = (uint16_t) (v >> 8);
  uint8_t was = in;
  if (what == OVERLAY_ALL) {
    for (int i = 0; i < OVERLAY_SLOTS; i++)
      clear_slot(i, ms);
    script_clear();
  } else if (what & OVERLAY_PLACE_BIT) {
    unsigned k = what & ~OVERLAY_PLACE_BIT;
    if (k >= OVERLAY_PLACES || places[k].slot == OVERLAY_SLOTS)
      return false;
    if (!ms)
      drop_place(&places[k]);
    else if (!places[k].leaving)  // as in clear_slot()
      leave(&places[k], ms, LEAVE);
  } else if (what < OVERLAY_SLOTS) {
    clear_slot((int) what, ms);
  }
  return in != was;
}

unsigned overlay_used(void) { return used; }

// The nibbles of rectangle a at f, rb bytes a row from its top left, into px a byte each, stride bytes a row: those
// of the frame, w of them
static void spread(uint8_t *px, int w, const uint8_t *f, int rb, const lv_area_t *a) {
  for (int y = a->y1; y <= a->y2; y++, f += rb)
    for (int x = a->x1; x <= a->x2 && x < w; x++) {
      uint8_t b = f[(x - a->x1) / 2];
      px[y * w + x] = x & 1 ? b & 15 : b >> 4;
    }
}

bool overlay_pixels(int place, uint8_t *px) {
  if (place < 0 || place >= OVERLAY_PLACES || places[place].slot == OVERLAY_SLOTS)
    return false;
  const place_t *p = &places[place];
  const slot_t *s = &slots[p->slot];
  if (s->field)
    return false;
  if (!s->still) {
    spread(px, s->w, place_frame(&places[place]), (int) row_bytes(s), &(lv_area_t) {0, 0, s->w - 1, s->h - 1});
    return true;
  }
  uint8_t *piece = lv_mem_alloc(OVERLAY_PIECE_MAX);
  if (!piece)
    return false;
  memset(px, 16, (size_t) s->w * s->h);
  uint32_t at = s->sheet ? place_at(p) : 0, k = s->sheet ? record(s, at, NULL, NULL) : 0;
  for (at += k; at < s->len && !(s->sheet && heads(s, at)); at += k) {
    lv_area_t a;
    k = record(s, at, piece, &a);
    spread(px, s->w, piece, lv_area_get_width(&a) / 2, &a);
  }
  lv_mem_free(piece);
  return true;
}

int32_t overlay_gone(void) {
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (gone_bits >> k & 1) {
      gone_bits &= (uint16_t) ~(1u << k);
      return k | gone_tag[k] << 8;
    }
  return -1;
}

// ---------------------------------------------------------------- a transfer

void overlay_abort(void) {
  if (in < OVERLAY_SLOTS)
    drop_slot(in);
}

int overlay_open(const uint8_t *b, int n, uint32_t *len) {
  if (n != OVERLAY_BEGIN_LEN)
    return PIC_BAD;
  uint8_t i = b[OVERLAY_BEGIN_SLOT], kind = b[OVERLAY_BEGIN_KIND];
  if (i >= OVERLAY_SLOTS || kind > OVERLAY_FIELD)
    return PIC_BAD;
  slot_t *s = &slots[i];
  drop_slot(i);
  s->w = plink_get16(b + OVERLAY_BEGIN_W);
  s->h = plink_get16(b + OVERLAY_BEGIN_H);
  s->scale = b[OVERLAY_BEGIN_SCALE];
  s->still = kind != OVERLAY_ANIMATED;
  s->sheet = kind == OVERLAY_SHEET;
  s->field = kind == OVERLAY_FIELD;
  s->rate = 100;
  *len = plink_number(b + OVERLAY_BEGIN_SIZE, 4);
  pic_palette(s->pal, b + OVERLAY_BEGIN_HEAD);
  memcpy(s->opa, b + OVERLAY_BEGIN_HEAD + PIC_PAL_LEN, sizeof s->opa);
  s->glows = plink_get16(b + OVERLAY_BEGIN_GLOWS);
  s->shade = lv_color_make(b[OVERLAY_BEGIN_SHADE], b[OVERLAY_BEGIN_SHADE + 1], b[OVERLAY_BEGIN_SHADE + 2]);
  s->cycles = plink_get16(b + OVERLAY_BEGIN_CYCLES);
  if (!s->w || !s->h || s->h > 255 || (s->scale != 1 && s->scale != 2) || s->w * s->scale > PIC_MAX_W ||
      s->h * s->scale > PIC_MAX_H)
    return PIC_BAD_SIZE;
  if (*len > OVERLAY_POOL || (s->off = take(frame_bytes(s) + *len)) == NO_OFF)
    return PIC_TOO_BIG;
  s->len = (uint16_t) *len;
  in = i;
  return -1;
}

void overlay_bytes(uint32_t at, const uint8_t *b, unsigned n) {
  if (in < OVERLAY_SLOTS)
    memcpy(records(&slots[in]) + at, b, n);
}

// h rows of w bytes at p, packed as plink_pack_row packs them, into out, or only checked if out is NULL. Returns the
// bytes they take, 0 if they run past end or a run past the end of a row.
static uint32_t unpack(const uint8_t *p, const uint8_t *end, unsigned w, unsigned h, uint8_t *out) {
  const uint8_t *start = p;
  for (unsigned y = 0; y < h; y++)
    for (unsigned left = w; left;) {
      if (p == end)
        return 0;
      uint8_t c = *p++;
      unsigned n = c < 128 ? c + 1u : c - 126u, k = c < 128 ? n : 1;  // the bytes it makes, and those after it
      if (n > left || (unsigned) (end - p) < k)
        return 0;
      if (out) {
        if (c < 128)
          memcpy(out, p, n);
        else
          memset(out, *p, n);
        out += n;
      }
      p += k;
      left -= n;
    }
  return (uint32_t) (p - start);
}

// A record is OVERLAY_RECORD_*: delay, x / 2, y, w / 2, h, then the rectangle, deflated or a still's packed rows
uint32_t record(const slot_t *s, uint32_t at, uint8_t *frame, lv_area_t *area) {
  const uint8_t *r = records(s) + at, *end = records(s) + s->len, *d = r + OVERLAY_RECORD_HEAD;
  unsigned rb = row_bytes(s);
  if (end - r < OVERLAY_RECORD_HEAD)
    return 0;
  unsigned x = r[OVERLAY_RECORD_X], y = r[OVERLAY_RECORD_Y], w = r[OVERLAY_RECORD_W], h = r[OVERLAY_RECORD_H];
  if (!w && !h &&
      ((x == OVERLAY_RECORD_MORE && at && (!s->still || s->sheet)) || (x == OVERLAY_RECORD_FRAME && s->sheet)))
    x = y = 0;  // nothing changes, or a sheet's frame starts
  if (x + w > rb || y + h > s->h || !w != !h || (s->still && w * h > OVERLAY_PIECE_MAX))
    return 0;
  uint32_t n = 0;
  if (w) {
    n = s->still ? unpack(d, end, w, h, frame)
                 : pinflate(d, (uint32_t) (end - d), frame ? frame + y * rb + x : NULL, w, rb, w * h, 0);
    if (!n)
      return 0;
  }
  if (area)
    *area = (lv_area_t) {x * 2, y, (x + w) * 2 - 1, y + h - 1};
  return OVERLAY_RECORD_HEAD + n;
}

// delay is in OVERLAY_TICK_MS at 100 %, scaled by the slot's rate
uint32_t period(const slot_t *s, uint8_t delay) {
  uint32_t ms = delay * (OVERLAY_TICK_MS * 100u) / LV_MAX(s->rate, 1);
  return ms < OVERLAY_FRAME_MIN_MS ? OVERLAY_FRAME_MIN_MS : ms;
}

// Something new to play: the timer looks soon and then waits for the next frame
void wake(void) {
  lv_timer_set_period(timer, 10);
  lv_timer_reset(timer);
}

// Where the record after a sheet's frame from at is, the one that starts the next or makes it longer, loop_at past the
// last
static uint16_t past(const slot_t *s, uint32_t at) {
  for (at += record(s, at, NULL, NULL); at < s->len && !heads(s, at);)
    at += record(s, at, NULL, NULL);
  return (uint16_t) (at < s->len ? at : s->loop_at);
}

// An animated slot's record at *next applied to frame, a sheet's frame from there at *at unless it makes the one
// before longer, and *next moved on, back to loop_at after the last. area gets what it changed. Returns its delay.
uint8_t advance(const slot_t *s, uint8_t *frame, uint16_t *at, uint16_t *next, lv_area_t *area) {
  uint8_t delay = records(s)[*next + OVERLAY_RECORD_DELAY];
  if (s->sheet) {
    if (!more(s, *next) && area)
      *area = (lv_area_t) {0, 0, (lv_coord_t) (s->w - 1), (lv_coord_t) (s->h - 1)};
    *at = more(s, *next) ? *at : *next;
    *next = past(s, *next);
    return delay;
  }
  *next = (uint16_t) (*next + record(s, *next, frame, area));
  if (*next >= s->len)
    *next = s->loop_at;
  return delay;
}

// The first frame: an animated slot's into frame, the whole of it in the first record, a sheet's at *at
void first_frame(const slot_t *s, uint8_t *frame, uint16_t *at, uint16_t *next) {
  *at = 0;
  if (s->sheet) {
    *next = past(s, 0);
    return;
  }
  record(s, 0, frame, NULL);
  *next = s->loop_at;
}

// Whether the n bytes at d say what a field is, see OVERLAY_FIELD_*
static bool is_field(const uint8_t *d, unsigned n) {
  if (n != OVERLAY_FIELD_LEN)
    return false;
  unsigned a = d[OVERLAY_FIELD_A], b = plink_get16(d + OVERLAY_FIELD_B);
  switch (d[OVERLAY_FIELD_KIND]) {
    case OVERLAY_FIELD_LINEAR:
      return a <= (OVERLAY_LINEAR_MIRROR | OVERLAY_LINEAR_DOWN);
    case OVERLAY_FIELD_RADIAL:
    case OVERLAY_FIELD_CONE:
      return true;
    case OVERLAY_FIELD_NOISE:
      return a >= 1 && a <= OVERLAY_NOISE_LAYERS && b >= OVERLAY_NOISE_MIN && b <= OVERLAY_NOISE_MAX;
  }
  return false;
}

bool overlay_close(void) {
  if (in >= OVERLAY_SLOTS)
    return false;
  int i = in;
  slot_t *s = &slots[i];
  in = OVERLAY_SLOTS;
  s->last = NO_OFF;
  s->frames = 0;
  s->loop_at = s->len;  // a still's pieces don't loop
  bool ok = s->field ? is_field(records(s), s->len) : s->len;
  for (uint32_t at = 0, k; ok && !s->field && at < s->len; at += k) {
    lv_area_t a;
    if (!(k = record(s, at, NULL, &a))) {
      ok = false;
    } else if (s->sheet) {  // frames from the first record on
      ok = at || heads(s, 0);
      s->frames += heads(s, at) && !more(s, at);
    } else if (s->still) {
      continue;
    } else if (at) {
      s->last = (uint16_t) at;
      s->frames += !more(s, at);
    } else {
      s->loop_at = (uint16_t) k;
      ok = lv_area_get_width(&a) == 2 * (int) row_bytes(s) && lv_area_get_height(&a) == s->h;
    }
  }
  if (s->sheet) {  // round and round from the first, a round over when it's back there
    s->loop_at = s->frames > 1 ? 0 : s->len;
    s->last = 0;
  }
  if (!ok || (!s->sheet && s->last != NO_OFF && more(s, s->last))) {
    drop_slot(i);
    return false;
  }
  if (!s->still || s->sheet) {
    first_frame(s, s->still ? NULL : pool + s->off, &s->at, &s->next);
    s->due = lv_tick_get() + period(s, records(s)[OVERLAY_RECORD_DELAY]);
  }
  s->ready = true;
  wake();
  return true;
}

// ---------------------------------------------------------------- playing

// The next record of slot i's frames, or of place own's own, and what it changed redrawn on every place that shows
// them. Back to the first frame, a round of them ended for their scripts.
static void step(int i, place_t *own, uint32_t *due) {
  slot_t *s = &slots[i];
  uint16_t *next = own ? &own->next : &s->next;
  lv_area_t a = {0, 0, -1, -1};
  bool round = *next == s->last;
  *due +=
      period(s, advance(s, s->still ? NULL : pool + (own ? own->frame : s->off), own ? &own->at : &s->at, next, &a));
  for (int k = 0; k < OVERLAY_PLACES; k++) {
    place_t *p = &places[k];
    if (p->slot != i || (own ? p != own : p->own))
      continue;
    if (a.x2 >= a.x1)
      changed(p, &a);
    if (round)
      script_round(p);
  }
}

// The next record of slot i's frames, or place own's, once it's due. Returns how long until the one after.
static uint32_t play(int i, place_t *own, uint32_t *due, uint32_t now) {
  if ((int32_t) (*due - now) <= 0)
    step(i, own, due);
  if ((int32_t) (*due - now) <= 0)
    *due = now + OVERLAY_FRAME_MIN_MS;  // fell behind, like after the backlight was off
  return *due - now;
}

// Every frame that's due goes on, then the timer waits for the next one. Only what's on the screen plays, a paused
// slot holds its frame.
static void tick(lv_timer_t *t) {
  if (input_backlight() < 0)
    return;
  uint32_t now = lv_tick_get(), wait = 1000, next;  // next on its own: LV_MIN would play each one twice
  for (int i = 0; i < OVERLAY_SLOTS && layer; i++) {
    slot_t *s = &slots[i];
    if (!s->ready || !looping(s) || !s->rate || !has_places(i, true))
      continue;
    next = play(i, NULL, &s->due, now);
    wait = LV_MIN(wait, next);
  }
  for (int k = 0; k < OVERLAY_PLACES && layer; k++) {
    place_t *p = &places[k];
    if (p->slot == OVERLAY_SLOTS || !p->own || !looping(&slots[p->slot]) || !slots[p->slot].rate)
      continue;
    next = script_plays(p) ? script_play(p, now) : play(p->slot, p, &p->due, now);
    wait = LV_MIN(wait, next);
  }
  lv_timer_set_period(t, wait < 10 ? 10 : wait);
}

// ---------------------------------------------------------------- the messages

static void place_msg(const uint8_t *b) {
  uint8_t k = b[OVERLAY_MSG_PLACE], slot = b[OVERLAY_PLACE_SLOT];
  if (k >= OVERLAY_PLACES || slot >= OVERLAY_SLOTS || !slots[slot].ready || slots[slot].leaving)
    return;
  place_t *p = &places[k];
  const slot_t *s = &slots[slot];
  int16_t x = plink_i16(b + OVERLAY_PLACE_X),
          y = plink_i16(b + OVERLAY_PLACE_Y);  // can be partly or all off the screen
  uint16_t fade = plink_get16(b + OVERLAY_PLACE_FADE);
  drop_place(p);
  gone_bits &= (uint16_t) ~(1u << k);
  uint8_t delay = round_delay(s);  // of the first frame
  if (b[OVERLAY_PLACE_OWN] && (!s->still || s->sheet)) {
    if (!s->still && (p->frame = take(frame_bytes(s))) == NO_OFF)
      return;
    p->own = true;
    first_frame(s, s->still ? NULL : pool + p->frame, &p->at, &p->next);
  }
  p->due = looping(s) ? lv_tick_get() + period(s, delay) : period(s, delay);  // else ms left of its round
  p->slot = slot;
  p->born = lv_tick_get();
  p->rng = (p->born + 1) * 2654435761u ^ (uint32_t) (k + 1) * 0x9E3779B9u;
  if (!p->rng)
    p->rng = 1;
  ch_set(p, OVERLAY_W, s->w * s->scale * MILLI);
  ch_set(p, OVERLAY_H, s->h * s->scale * MILLI);
  ch_set(p, OVERLAY_X, (int32_t) x * MILLI);
  ch_set(p, OVERLAY_Y, (int32_t) y * MILLI);
  if (fade) {
    ch_set(p, FADE, 0);
    ch_glide(p, FADE, FULL, fade, 0);
    wake_mover();
  }
  update(p);
  order();
  wake();
}

// A glide or drift of channel c stops what else moved it
static void take_over(place_t *p, int c) {
  roam_t r;
  ch_set(p, c, ch_at(p, c));
  if (!roam(p, &r))
    return;
  if (c <= OVERLAY_Y && r.wander.on) {
    r.wander.on = false;
    ch_drift(p, OVERLAY_X, 0);
    ch_drift(p, OVERLAY_Y, 0);
  }
  if (c == OVERLAY_TURN)
    r.face.on = false;
  roam_set(p, &r);
}

// Whether what a GLIDE or DRIFT of n bytes has from at on, a channel and its value each, is what we take
static bool takes(const uint8_t *b, int n, int at, bool drift) {
  for (; at < n; at += OVERLAY_GLIDE_EACH) {
    int c = b[at];
    int32_t v = plink_i16(b + at + 1);
    if (c >= (drift ? OVERLAY_SIZE : OVERLAY_CHANNELS))
      return false;
    bool box = c == OVERLAY_W || c == OVERLAY_H;
    if (!drift && (limited(c, v * unit(c)) != v * unit(c) || (box && (v < 0 || v > OVERLAY_BOX_MAX))))
      return false;
  }
  return true;
}

static void glide_msg(place_t *p, const uint8_t *b, int n) {
  if (b[OVERLAY_GLIDE_EASE] > (OVERLAY_EASE_IN | OVERLAY_EASE_OUT) || !takes(b, n, OVERLAY_GLIDE_HEAD, false))
    return;
  for (int at = OVERLAY_GLIDE_HEAD; at < n; at += OVERLAY_GLIDE_EACH) {
    int c = b[at];
    int32_t to = plink_i16(b + at + 1) * unit(c);
    take_over(p, c);
    if (c <= OVERLAY_Y)
      p->drifting &= (uint8_t) ~(1u << c);  // a glide can end partly off the screen, and that's all
    if (c == OVERLAY_TURN) {
      int32_t now = wrap(ch_at(p, c), TURN);
      ch_set(p, c, now);
      to = now + half(to - now);  // the short way round
    }
    ch_glide(p, c, to, plink_get16(b + OVERLAY_GLIDE_MS), b[OVERLAY_GLIDE_EASE]);
  }
}

static void drift_msg(place_t *p, const uint8_t *b, int n) {
  if (!takes(b, n, OVERLAY_DRIFT_HEAD, true))
    return;
  for (int at = OVERLAY_DRIFT_HEAD; at < n; at += OVERLAY_GLIDE_EACH) {
    int c = b[at];
    int32_t rate =
        plink_i16(b + at + 1) * (c == OVERLAY_TURN ? MILLI / 10 : MILLI / 100);  // from tenths, or hundredths
    take_over(p, c);
    ch_drift(p, c, rate);
    if (c <= OVERLAY_Y && rate)
      p->drifting |= (uint8_t) (1u << c);
  }
}

static void wander_msg(place_t *p, const uint8_t *b) {
  roam_t r;
  roam(p, &r);
  for (int i = 0; i < 2; i++) {
    r.wander.speed[i] = LV_MIN(plink_get16(b + OVERLAY_WANDER_SPEED + 2 * i), 10000);
    r.wander.leg[i] = plink_get16(b + OVERLAY_WANDER_LEG + 2 * i);
    r.wander.rest[i] = plink_get16(b + OVERLAY_WANDER_REST + 2 * i);
  }
  r.wander.heading = (int16_t) wrap(plink_i16(b + OVERLAY_WANDER_HEADING), 360);
  r.wander.spread = (int16_t) LV_CLAMP(0, plink_i16(b + OVERLAY_WANDER_SPREAD), 180);
  r.wander.on = true;
  if (!roam_set(p, &r))
    return;
  p->drifting = 3;
  new_leg(p);  // its drifts take over x and y
}

static void face_msg(place_t *p, const uint8_t *b) {
  int16_t tilt = plink_i16(b + OVERLAY_FACE_TILT), rate = plink_i16(b + OVERLAY_FACE_RATE);
  roam_t r;
  if (b[OVERLAY_FACE_FLAGS] > (OVERLAY_FACE_MIRROR | OVERLAY_FACE_LEVEL) || tilt < 0 || tilt > 180 || rate < 0)
    return;
  roam(p, &r);
  r.face.on = true;
  r.face.flags = b[OVERLAY_FACE_FLAGS];
  r.face.facing = plink_i16(b + OVERLAY_FACE_FACING);
  r.face.tilt = tilt;
  r.face.rate = rate;
  if (roam_set(p, &r))
    ch_set(p, OVERLAY_TURN, ch_at(p, OVERLAY_TURN));  // what turned it stops
}

static void area_msg(place_t *p, const uint8_t *b) {
  int16_t x0 = plink_i16(b + OVERLAY_AREA_X0), y0 = plink_i16(b + OVERLAY_AREA_Y0), x1 = plink_i16(b + OVERLAY_AREA_X1),
          y1 = plink_i16(b + OVERLAY_AREA_Y1);
  const uint8_t *edge = b + OVERLAY_AREA_EDGES;  // x, y
  if (x1 <= x0 || y1 <= y0 || edge[0] > OVERLAY_EDGE_BOUNCE || edge[1] > OVERLAY_EDGE_BOUNCE)
    return;
  p->area.x0 = x0;
  p->area.y0 = y0;
  p->area.x1 = x1;
  p->area.y1 = y1;
  p->area.edge[0] = edge[0];
  p->area.edge[1] = edge[1];
}

static void tint_msg(place_t *p, const uint8_t *b) {
  const uint8_t *c = b + OVERLAY_TINT_RGB;
  int32_t tint = b[OVERLAY_TINT_ON] ? (int32_t) c[0] << 16 | c[1] << 8 | c[2] : NO_TINT;
  if (b[OVERLAY_TINT_ON] > 1 || tint == p->tint)
    return;
  p->tint = tint;
  redraw_place(p);
}

static void view_msg(place_t *p, const uint8_t *b) {
  uint8_t mask = b[OVERLAY_VIEW_MASK];
  p->view = (uint8_t) ((p->view & ~mask) | (b[OVERLAY_VIEW_FLAGS] & mask));
}

// The ms left of a frame at rate was, at rate
static uint32_t rescaled(uint32_t left, uint16_t was, uint16_t rate) { return was && rate ? left * was / rate : left; }

// A frame due at tick *due at rate was, at rate. From paused it goes on at once.
static void retime(uint32_t *due, uint16_t was, uint16_t rate, uint32_t now) {
  if (!was)
    *due = now;
  else if ((int32_t) (*due - now) > 0)
    *due = now + rescaled(*due - now, was, rate);
}

static void frames_msg(const uint8_t *b) {
  uint8_t i = b[OVERLAY_FRAMES_SLOT];
  uint16_t rate = plink_get16(b + OVERLAY_FRAMES_RATE);
  if (i >= OVERLAY_SLOTS || rate > OVERLAY_RATE_MAX)
    return;
  slot_t *s = &slots[i];
  uint32_t now = lv_tick_get();
  retime(&s->due, s->rate, rate, now);
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (places[k].slot == i && !looping(s))
      places[k].due = rescaled(places[k].due, s->rate, rate);  // ms left of the round
    else if (places[k].slot == i)
      retime(&places[k].due, s->rate, rate, now);
  s->rate = rate;
  wake();
}

int msg_len(uint16_t key, int n) {
  // FRAMES to TINT, 0 for those as long as what they have
  static const uint8_t LEN[] = {OVERLAY_FRAMES_LEN, OVERLAY_PLACE_LEN, OVERLAY_AREA_LEN, 0, 0,
                                OVERLAY_WANDER_LEN, OVERLAY_FACE_LEN,  OVERLAY_VIEW_LEN, 0, OVERLAY_TINT_LEN};
  _Static_assert(sizeof LEN == K_OVERLAY_TINT - K_OVERLAY_FRAMES + 1, "a length for each");
  if (key < K_OVERLAY_FRAMES || key > K_OVERLAY_TINT)
    return -1;
  if (key == K_OVERLAY_BIND)
    return n >= OVERLAY_BIND_VALUE ? n : -1;
  if (key == K_OVERLAY_GLIDE || key == K_OVERLAY_DRIFT) {  // a channel and its value each, one at least
    int head = key == K_OVERLAY_GLIDE ? OVERLAY_GLIDE_HEAD : OVERLAY_DRIFT_HEAD;
    return n > head && (n - head) % OVERLAY_GLIDE_EACH == 0 ? n : -1;
  }
  return LEN[key - K_OVERLAY_FRAMES];
}

void overlay_msg(uint16_t key, const uint8_t *b, int n) {
  if (n < 2 || (key != K_OVERLAY_SCRIPT && n != msg_len(key, n)))
    return;
  if (key == K_OVERLAY_FRAMES) {
    frames_msg(b);
    return;
  }
  if (key == K_OVERLAY_PLACE) {
    place_msg(b);
    return;
  }
  uint8_t k = b[OVERLAY_MSG_PLACE];
  place_t *p = k < OVERLAY_PLACES && places[k].slot != OVERLAY_SLOTS ? &places[k] : NULL;
  if (!p)
    return;
  switch (key) {
    case K_OVERLAY_AREA:
      area_msg(p, b);
      break;
    case K_OVERLAY_GLIDE:
      glide_msg(p, b, n);
      break;
    case K_OVERLAY_DRIFT:
      drift_msg(p, b, n);
      break;
    case K_OVERLAY_WANDER:
      wander_msg(p, b);
      break;
    case K_OVERLAY_FACE:
      face_msg(p, b);
      break;
    case K_OVERLAY_VIEW:
      view_msg(p, b);
      break;
    case K_OVERLAY_BIND:
      bind_msg(p, b, n);
      break;
    case K_OVERLAY_TINT:
      tint_msg(p, b);
      break;
    case K_OVERLAY_SCRIPT:
      script_msg(p, b, n);
      break;
  }
  if (p->slot == OVERLAY_SLOTS)
    return;  // its script ended it, and it wasn't shown
  update(p);
  order();
  if (moving(p))
    wake_mover();
}

void overlay_init(void) {
  motion_init();
  script_init();
  for (int i = 0; i < OVERLAY_SLOTS; i++)
    slots[i].off = NO_OFF;
  for (int k = 0; k < OVERLAY_PLACES; k++) {
    places[k].slot = OVERLAY_SLOTS;
    places[k].frame = places[k].script = places[k].roam = NO_OFF;
    places[k].bound = 0;
    reset(&places[k]);
  }
  draw_init();
  if (!timer)
    timer = lv_timer_create(tick, 100, NULL);
}
