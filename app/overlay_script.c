// Places' scripts, see K_OVERLAY_SCRIPT: steps the panel runs itself, so a place can play parts of its frames, wait,
// move and look where the others are without the ESP. A script is in the pool behind what's there, its run_t and
// then its bytes.
#include "overlay_int.h"
#include "plink.h"
#include <stddef.h>
#include <string.h>

enum {
  W_LOADING,  // its bytes are still coming
  W_RUN,      // on with the step at pc
  W_YIELD,    // took its OVERLAY_SCRIPT_STEPS, on at the next motion step
  W_MS,       // a WAIT's left ms, pc after it
  W_UNTIL,    // an UNTIL didn't hold, on at pc at the next motion step
  W_ROUNDS,   // till rounds is want, pc after the ROUNDS
  W_CLIP,     // pc at a CLIP for the end of this round
  W_DONE,     // past its last step, or ENDed
};

typedef struct {
  uint16_t len;       // its bytes
  uint16_t pc;        // the step it's at, or while it's W_LOADING the bytes in so far
  uint8_t wait;       // W_*
  uint8_t tag;        // its END's
  bool turned;        // a step showed another frame at the end of a round
  uint16_t frame;     // the frame its own frame shows
  uint16_t from, to;  // its clip, frames from to to, both in
  uint16_t rate;      // percent its frames go at, 0 holds them
  uint32_t rounds;    // of its clip, so far
  uint32_t want;      // rounds a ROUNDS waits for
  int32_t left;       // ms a WAIT has left
  uint32_t start;     // lv_tick_get() when it started
  int32_t r[OVERLAY_OWN_REGS];
} run_t;
_Static_assert(sizeof(run_t) <= OVERLAY_SCRIPT_POOL, "a script's run_t in what the ESP counts for it");
_Static_assert(OVERLAY_SCRIPT_MAX <= UINT16_MAX, "2 bytes for where a step starts");

static int32_t shared[OVERLAY_REGS - OVERLAY_OWN_REGS];
static uint8_t mark_bits, mark_tag[OVERLAY_PLACES];  // marks for the ESP
static bool ending;                                  // a round just ended, so a CLIP for the end of one goes at once

static void load(const place_t *p, run_t *r) { memcpy(r, pool + p->script, sizeof *r); }
static void save(const place_t *p, const run_t *r) { memcpy(pool + p->script, r, sizeof *r); }
static const uint8_t *bytes(const place_t *p) { return pool + p->script + OVERLAY_SCRIPT_POOL; }
static uint8_t head(const place_t *p, size_t at) { return p->script == NO_OFF ? 0 : pool[p->script + at]; }

static int32_t *reg(run_t *r, unsigned k) { return k < OVERLAY_OWN_REGS ? &r->r[k] : &shared[k - OVERLAY_OWN_REGS]; }

// ---------------------------------------------------------------- reading a step

// What each step takes, see OVERLAY_DO_*: r a register, v a value, p a place, c an OVERLAY_IF_*, g an OVERLAY_GET_*,
// a where to go on, m an OVERLAY_CLIP_*, e an OVERLAY_EASE_*, t a tag, l a list, read as its number the value before
// picks, z a size. SEND's message is read on its own.
static const char *const FORMS[OVERLAY_DOS] = {
    [OVERLAY_DO_SET] = "rv",    [OVERLAY_DO_ADD] = "rv",     [OVERLAY_DO_SUB] = "rv",  [OVERLAY_DO_MUL] = "rv",
    [OVERLAY_DO_DIV] = "rv",    [OVERLAY_DO_MOD] = "rv",     [OVERLAY_DO_MIN] = "rv",  [OVERLAY_DO_MAX] = "rv",
    [OVERLAY_DO_ABS] = "rv",    [OVERLAY_DO_RAND] = "rvv",   [OVERLAY_DO_GET] = "rgp", [OVERLAY_DO_DIST] = "rpp",
    [OVERLAY_DO_ANGLE] = "rpp", [OVERLAY_DO_SIN] = "rvv",    [OVERLAY_DO_COS] = "rvv", [OVERLAY_DO_PICK] = "rvl",
    [OVERLAY_DO_GOTO] = "a",    [OVERLAY_DO_IF] = "vcva",    [OVERLAY_DO_WAIT] = "v",  [OVERLAY_DO_UNTIL] = "vcva",
    [OVERLAY_DO_ROUNDS] = "v",  [OVERLAY_DO_CLIP] = "vvmzz", [OVERLAY_DO_FRAME] = "v", [OVERLAY_DO_RATE] = "v",
    [OVERLAY_DO_MOVE] = "vvve", [OVERLAY_DO_SEND] = "",      [OVERLAY_DO_MARK] = "t",  [OVERLAY_DO_END] = "vt",
};

// A step being read: its bytes, and what it says
typedef struct {
  const uint8_t *s;
  unsigned at, len;
  bool bad;
  uint8_t op;
  int32_t a[5];                   // in order, a value as it is now, five for the longest form, CLIP
  uint16_t key;                   // SEND's
  uint8_t n;                      // its message's length
  uint8_t msg[OVERLAY_SEND_MAX];  // with its values in
} step_t;

static uint8_t byte(step_t *t) {
  if (t->at >= t->len) {
    t->bad = true;
    return 0;
  }
  return t->s[t->at++];
}
static uint16_t word(step_t *t) {  // low byte first
  uint8_t lo = byte(t);
  return (uint16_t) (lo | byte(t) << 8);
}

// A value as it is now in r. Without r it's only read.
static int32_t value(step_t *t, run_t *r) {
  uint8_t k = byte(t);
  if (k == OVERLAY_V16 || k == OVERLAY_V32) {  // low byte first
    uint32_t v = 0;
    int n = k == OVERLAY_V16 ? 2 : 4;
    for (int i = 0; i < n; i++)
      v |= (uint32_t) byte(t) << 8 * i;
    return n == 2 ? (int16_t) v : (int32_t) v;
  }
  if (k < OVERLAY_VREG || k >= OVERLAY_VREG + OVERLAY_REGS) {
    t->bad = true;
    return 0;
  }
  return r ? *reg(r, k - OVERLAY_VREG) : 0;
}

// The keys a SEND can have: what a place takes but SCRIPT
static bool sendable(uint16_t key) {
  return key >= K_OVERLAY_AREA && key <= K_OVERLAY_TINT && key != K_OVERLAY_SCRIPT && key != K_OVERLAY_GONE &&
         key != K_OVERLAY_MARK;
}

// SEND: key, n, the message and k values for it, each at its byte
// What a message takes of a value: its low 16 bits, past those it's at the end
static uint16_t low16(int32_t v) { return (uint16_t) LV_CLAMP(-32768, v, 65535); }

static void read_send(step_t *t, run_t *r) {
  t->key = (uint16_t) (K_OVERLAY_FRAMES + byte(t));
  t->n = byte(t);
  if (!sendable(t->key) || t->n < 2 || t->n > OVERLAY_SEND_MAX) {
    t->bad = true;
    return;
  }
  for (unsigned i = 0; i < t->n; i++)
    t->msg[i] = byte(t);
  if (msg_len(t->key, t->msg) != (int) t->n)
    t->bad = true;
  for (unsigned k = byte(t); k && !t->bad; k--) {
    unsigned at = byte(t);
    uint16_t v = low16(value(t, r));
    bool keeps = t->key == K_OVERLAY_GLIDE || t->key == K_OVERLAY_DRIFT;  // their channels give the length
    if (at < 1u + keeps || at + 2 > t->n)
      t->bad = true;
    else
      plink_put16(t->msg + at, v);
  }
}

// Number k of a list, its first below that and its last past it
static int32_t from_list(step_t *t, int32_t k) {
  unsigned n = byte(t);
  int32_t v = 0;
  t->bad |= !n;
  for (unsigned i = 0; i < n && !t->bad; i++) {
    int16_t x = (int16_t) word(t);
    if ((int32_t) i == LV_CLAMP(0, k, (int32_t) n - 1))
      v = x;
  }
  return v;
}

// The step at at into t, its values from r, or only read without r. False if it's bad.
static bool read_step(step_t *t, const uint8_t *s, unsigned len, unsigned at, run_t *r) {
  t->s = s;
  t->len = len;
  t->at = at;
  t->bad = false;
  t->op = byte(t);
  if (t->op >= OVERLAY_DOS)
    return false;
  if (t->op == OVERLAY_DO_SEND)
    read_send(t, r);
  int i = 0;
  for (const char *f = FORMS[t->op]; *f && !t->bad; f++, i++) {
    int32_t v = *f == 'v'                ? value(t, r)
                : *f == 'l'              ? from_list(t, t->a[i - 1])
                : *f == 'a' || *f == 'z' ? word(t)
                                         : byte(t);
    bool ok = *f == 'r'   ? v < OVERLAY_REGS
              : *f == 'p' ? v < OVERLAY_PLACES || v == OVERLAY_SELF
              : *f == 'c' ? v <= OVERLAY_IF_GT
              : *f == 'g' ? v < OVERLAY_GETS
              : *f == 'm' ? v <= OVERLAY_CLIP_NEXT
              : *f == 'e' ? v <= (OVERLAY_EASE_IN | OVERLAY_EASE_OUT)
                          : true;
    t->bad |= !ok;
    t->a[i] = v;
  }
  return !t->bad;
}

// Whether a script for p is whole: each step can be read, each goes on at the start of a step or the end, and only
// a place with a frame of its own plays frames
static bool check(const place_t *p, const uint8_t *s, unsigned len) {
  uint8_t starts[(OVERLAY_SCRIPT_MAX + 8) / 8] = {0}, to[sizeof starts] = {0};
  bool own = p->frame != NO_OFF;
  step_t t;
  for (unsigned at = 0; at < len; at = t.at) {
    if (!read_step(&t, s, len, at, NULL))
      return false;
    starts[at / 8] |= (uint8_t) (1u << at % 8);
    if (t.op == OVERLAY_DO_GOTO || t.op == OVERLAY_DO_IF || t.op == OVERLAY_DO_UNTIL) {
      unsigned go = (unsigned) t.a[t.op == OVERLAY_DO_GOTO ? 0 : 3];
      if (go > len)
        return false;
      to[go / 8] |= (uint8_t) (1u << go % 8);
    }
    if ((t.op == OVERLAY_DO_CLIP || t.op == OVERLAY_DO_FRAME || t.op == OVERLAY_DO_RATE) && !own)
      return false;
  }
  starts[len / 8] |= (uint8_t) (1u << len % 8);
  for (unsigned i = 0; i < sizeof starts; i++)
    if (to[i] & ~starts[i])
      return false;
  return true;
}

// ---------------------------------------------------------------- frames of its own

// ms a record's delay is at the slot's rate and the script's, OVERLAY_FRAME_MIN_MS at least
static uint32_t frame_ms(const slot_t *s, const run_t *r, uint8_t delay) {
  uint32_t ms = delay * (OVERLAY_TICK_MS * 100u * 100u) / (LV_MAX(s->rate, 1) * LV_MAX(r->rate, 1));
  return ms < OVERLAY_FRAME_MIN_MS ? OVERLAY_FRAME_MIN_MS : ms;
}

// Frame f, 0..frames - 1: on from the one it shows if that's next, else from the first
static void to_frame(place_t *p, run_t *r, unsigned f) {
  const slot_t *s = &slots[p->slot];
  uint8_t *frame = pool + p->frame, delay;
  lv_area_t a = {0, 0, (lv_coord_t) (s->w - 1), (lv_coord_t) (s->h - 1)};
  if (f == (r->frame + 1u) % s->frames && !more(s, p->next)) {
    a = (lv_area_t) {0, 0, -1, -1};
    delay = advance(s, frame, &p->next, &a);
  } else {
    first_frame(s, frame);
    p->next = s->loop_at;
    delay = records(s)[OVERLAY_RECORD_DELAY];
    for (unsigned k = 0; k < f;) {
      bool starts = !more(s, p->next);
      uint8_t d = advance(s, frame, &p->next, NULL);
      if (starts) {
        k++;
        delay = d;
      }
    }
  }
  r->frame = (uint16_t) f;
  r->turned = true;
  p->due = lv_tick_get() + frame_ms(s, r, delay);
  if (a.x2 >= a.x1)
    changed(p, &a);
  wake();
}

// v as one of its frames, round them
static unsigned frame_of(const place_t *p, int32_t v) {
  int32_t n = slots[p->slot].frames;
  return n ? (unsigned) wrap(v, n) : 0;
}

static bool plays(const place_t *p) { return looping(&slots[p->slot]); }

// It shows w x h of its frame from the top left, all of it across or down for 0
static void to_size(place_t *p, int32_t w, int32_t h) {
  const slot_t *s = &slots[p->slot];
  uint16_t was[2] = {p->shows[0], p->shows[1]};
  p->shows[0] = (uint16_t) (w ? LV_MIN(w, s->w) : s->w);
  p->shows[1] = (uint16_t) (h ? LV_MIN(h, s->h) : s->h);
  if (memcmp(was, p->shows, sizeof was))
    update(p);  // its box
}

// ---------------------------------------------------------------- running it

static const place_t *other(const place_t *p, int32_t k) {
  const place_t *q = k == OVERLAY_SELF ? p : &places[k];
  return q->slot == OVERLAY_SLOTS ? NULL : q;
}

static int32_t get(const place_t *p, const run_t *r, int32_t what, int32_t k) {
  const place_t *q = other(p, k);
  if (!q)
    return 0;
  run_t o;
  const run_t *qr = q == p ? r : NULL;
  if (q != p && q->script != NO_OFF) {
    load(q, &o);
    qr = &o;
  }
  switch (what) {
    case OVERLAY_GET_X:
      return q->x;
    case OVERLAY_GET_Y:
      return q->y;
    case OVERLAY_GET_W:
      return shown(q, 0);
    case OVERLAY_GET_H:
      return shown(q, 1);
    case OVERLAY_GET_TURN:
      return q->now.deg;
    case OVERLAY_GET_SIZE:
      return q->now.size / (FULL / 100);
    case OVERLAY_GET_OPACITY:
      return q->now.opa / (FULL / 100);
    case OVERLAY_GET_LIGHT:
      return q->now.light / (FULL / 100);
    case OVERLAY_GET_FRAME:
      return qr ? qr->frame : 0;
    case OVERLAY_GET_ROUNDS:
      return qr ? (int32_t) qr->rounds : 0;
    case OVERLAY_GET_MS:
      return qr ? (int32_t) lv_tick_elaps(qr->start) : 0;
  }
  return 1;  // here
}

// Its middle, false if it isn't there
static bool middle(const place_t *p, int32_t k, int32_t *x, int32_t *y) {
  const place_t *q = other(p, k);
  if (!q)
    return false;
  *x = q->x + shown(q, 0) / 2;
  *y = q->y + shown(q, 1) / 2;
  return true;
}

static uint32_t isqrt(uint32_t v) {
  uint32_t r = 0, b = 1u << 30;
  while (b > v)
    b >>= 2;
  for (; b; b >>= 2)
    if (v >= r + b) {
      v -= r + b;
      r = (r >> 1) + b;
    } else {
      r >>= 1;
    }
  return r;
}

// The way and the distance from place j's middle to k's, false if one isn't there
static bool between(const place_t *p, int32_t j, int32_t k, int32_t *dx, int32_t *dy) {
  int32_t x0, y0, x1, y1;
  if (!middle(p, j, &x0, &y0) || !middle(p, k, &x1, &y1))
    return false;
  *dx = LV_CLAMP(-32767, x1 - x0, 32767);
  *dy = LV_CLAMP(-32767, y1 - y0, 32767);
  return true;
}

static int32_t angle(int32_t dx, int32_t dy) {
  while (LV_ABS(dx) > 1456 || LV_ABS(dy) > 1456) {  // what lv_atan2 takes
    dx /= 2;
    dy /= 2;
  }
  return dx || dy ? lv_atan2(dy, dx) : 0;
}

static int32_t sine(int32_t deg, int32_t n, bool cosine) {
  int16_t d = (int16_t) wrap(wrap(deg, 360) + (cosine ? 90 : 0), 360);
  int64_t v = (int64_t) n * lv_trigo_sin(d);
  return (int32_t) ((v + (v < 0 ? -LV_TRIGO_SIN_MAX : LV_TRIGO_SIN_MAX) / 2) / LV_TRIGO_SIN_MAX);  // rounded
}

static bool holds(int32_t a, int32_t c, int32_t b) {
  switch (c) {
    case OVERLAY_IF_LT:
      return a < b;
    case OVERLAY_IF_LE:
      return a <= b;
    case OVERLAY_IF_EQ:
      return a == b;
    case OVERLAY_IF_NE:
      return a != b;
    case OVERLAY_IF_GE:
      return a >= b;
  }
  return a > b;
}

static int32_t maths(uint8_t op, int32_t a, int32_t b) {
  uint32_t ua = (uint32_t) a, ub = (uint32_t) b;  // wraps round, as signed maths mustn't
  switch (op) {
    case OVERLAY_DO_SET:
      return b;
    case OVERLAY_DO_ADD:
      return (int32_t) (ua + ub);
    case OVERLAY_DO_SUB:
      return (int32_t) (ua - ub);
    case OVERLAY_DO_MUL:
      return (int32_t) (ua * ub);
    case OVERLAY_DO_DIV:
      return b && !(a == INT32_MIN && b == -1) ? a / b : 0;
    case OVERLAY_DO_MOD:
      return b && !(a == INT32_MIN && b == -1) ? a % b : 0;
    case OVERLAY_DO_MIN:
      return LV_MIN(a, b);
    case OVERLAY_DO_MAX:
      return LV_MAX(a, b);
  }
  return b == INT32_MIN ? INT32_MAX : LV_ABS(b);  // ABS
}

static void set_rate(place_t *p, run_t *r, int32_t v) {
  uint16_t was = r->rate, rate = (uint16_t) LV_CLAMP(0, v, OVERLAY_RATE_MAX);
  uint32_t now = lv_tick_get();
  if (!was)
    p->due = now;
  else if (rate && (int32_t) (p->due - now) > 0)
    p->due = now + (p->due - now) * was / rate;
  r->rate = rate;
  wake();
}

// The step at r->pc, which can be the one it waits at. False when it's over: the script ended, or waits.
static bool do_step(place_t *p, run_t *r) {
  step_t t;
  if (!read_step(&t, bytes(p), r->len, r->pc, r)) {  // check() saw it whole, so it can't be
    r->pc = r->len;
    return true;
  }
  int32_t *a = t.a;
  uint16_t next = (uint16_t) t.at;
  switch (t.op) {
    case OVERLAY_DO_SET:
    case OVERLAY_DO_ADD:
    case OVERLAY_DO_SUB:
    case OVERLAY_DO_MUL:
    case OVERLAY_DO_DIV:
    case OVERLAY_DO_MOD:
    case OVERLAY_DO_MIN:
    case OVERLAY_DO_MAX:
    case OVERLAY_DO_ABS:
      *reg(r, (unsigned) a[0]) = maths(t.op, *reg(r, (unsigned) a[0]), a[1]);
      break;
    case OVERLAY_DO_RAND:
      *reg(r, (unsigned) a[0]) = pick(p, a[1], a[2]);
      break;
    case OVERLAY_DO_GET:
      *reg(r, (unsigned) a[0]) = get(p, r, a[1], a[2]);
      break;
    case OVERLAY_DO_PICK:
      *reg(r, (unsigned) a[0]) = a[2];
      break;
    case OVERLAY_DO_DIST:
    case OVERLAY_DO_ANGLE: {
      int32_t dx = 0, dy = 0;
      bool both = between(p, a[1], a[2], &dx, &dy);
      *reg(r, (unsigned) a[0]) = !both                      ? 0
                                 : t.op == OVERLAY_DO_ANGLE ? angle(dx, dy)
                                                            : (int32_t) isqrt((uint32_t) (dx * dx + dy * dy));
      break;
    }
    case OVERLAY_DO_SIN:
    case OVERLAY_DO_COS:
      *reg(r, (unsigned) a[0]) = sine(a[1], a[2], t.op == OVERLAY_DO_COS);
      break;
    case OVERLAY_DO_GOTO:
      next = (uint16_t) a[0];
      break;
    case OVERLAY_DO_IF:
      if (holds(a[0], a[1], a[2]))
        next = (uint16_t) a[3];
      break;
    case OVERLAY_DO_WAIT:
      if (a[0] > 0) {
        r->left = (int32_t) LV_MIN((uint32_t) a[0] + since_step(), INT32_MAX);  // the next step takes those too
        r->wait = W_MS;
      }
      break;
    case OVERLAY_DO_UNTIL:
      if (!holds(a[0], a[1], a[2])) {
        r->wait = W_UNTIL;
        r->pc = (uint16_t) a[3];
        return false;
      }
      break;
    case OVERLAY_DO_ROUNDS:
      if (a[0] > 0) {
        r->want = r->rounds + (uint32_t) a[0];
        r->wait = W_ROUNDS;
      }
      break;
    case OVERLAY_DO_CLIP:
      if (!plays(p))
        break;
      if (a[2] == OVERLAY_CLIP_NEXT && r->wait != W_CLIP && !ending) {
        r->wait = W_CLIP;
        return false;  // done from here at the end of the round
      }
      r->from = (uint16_t) frame_of(p, a[0]);
      r->to = (uint16_t) LV_MAX(r->from, frame_of(p, a[1]));
      r->rounds = 0;
      to_size(p, a[3], a[4]);
      to_frame(p, r, r->from);
      break;
    case OVERLAY_DO_FRAME:
      if (plays(p))
        to_frame(p, r, frame_of(p, a[0]));
      break;
    case OVERLAY_DO_RATE:
      set_rate(p, r, a[0]);
      break;
    case OVERLAY_DO_MOVE: {
      uint8_t m[OVERLAY_GLIDE_HEAD + 4] = {(uint8_t) (p - places), 1u << OVERLAY_X | 1u << OVERLAY_Y};
      plink_put16(m + OVERLAY_GLIDE_MS, low16(a[2]));
      m[OVERLAY_GLIDE_EASE] = (uint8_t) a[3];
      plink_put16(m + OVERLAY_GLIDE_HEAD, low16(a[0]));
      plink_put16(m + OVERLAY_GLIDE_HEAD + 2, low16(a[1]));
      overlay_msg(K_OVERLAY_GLIDE, m, sizeof m);
      break;
    }
    case OVERLAY_DO_SEND:
      t.msg[OVERLAY_MSG_PLACE] = (uint8_t) (p - places);
      overlay_msg(t.key, t.msg, t.n);
      break;
    case OVERLAY_DO_MARK:
      mark_bits |= (uint8_t) (1u << (p - places));
      mark_tag[p - places] = (uint8_t) a[0];
      break;
    case OVERLAY_DO_END:
      r->tag = (uint8_t) a[1];
      r->rate = 0;
      r->wait = W_DONE;
      save(p, r);
      if (!p->leaving)
        leave(p, (uint16_t) LV_CLAMP(0, a[0], 65535), LEAVE_GONE);
      return false;
  }
  r->pc = next;
  if (r->wait == W_CLIP)
    r->wait = W_RUN;
  return r->wait == W_RUN;
}

// On from r->pc until it waits or ends, OVERLAY_SCRIPT_STEPS steps at most
static void run(place_t *p) {
  run_t r;
  load(p, &r);
  for (int n = 0;; n++) {
    if (r.pc >= r.len) {
      r.wait = W_DONE;
      break;
    }
    if (n == OVERLAY_SCRIPT_STEPS) {
      r.wait = W_YIELD;
      break;
    }
    if (!do_step(p, &r)) {
      if (r.wait == W_DONE)
        return;  // ENDed, saved
      break;
    }
  }
  save(p, &r);
  if (r.wait == W_MS || r.wait == W_UNTIL || r.wait == W_YIELD)
    wake_mover();
}

// A round of its clip ended: a ROUNDS or CLIP that waited for it goes on
static void round_over(place_t *p) {
  run_t r;
  load(p, &r);
  r.rounds++;
  bool on = (r.wait == W_ROUNDS && r.rounds == r.want) || r.wait == W_CLIP;
  if (r.wait == W_ROUNDS && on)
    r.wait = W_RUN;
  save(p, &r);
  if (on) {
    ending = true;
    run(p);
    ending = false;
  }
}

// ---------------------------------------------------------------- from overlay.c and overlay_motion.c

void script_drop(place_t *p) {
  if (p->script == NO_OFF)
    return;
  run_t r;
  load(p, &r);
  give(p->script, OVERLAY_SCRIPT_POOL + r.len);
  p->script = NO_OFF;
  mark_bits &= (uint8_t) ~(1u << (p - places));
}

void script_msg(place_t *p, const uint8_t *b, int n) {
  if (n < OVERLAY_SCRIPT_BYTES) {
    script_drop(p);
    return;
  }
  unsigned at = plink_get16(b + OVERLAY_SCRIPT_AT), len = plink_get16(b + OVERLAY_SCRIPT_LEN),
           k = (unsigned) n - OVERLAY_SCRIPT_BYTES;
  run_t r;
  if (len > OVERLAY_SCRIPT_MAX || at + k > len) {
    script_drop(p);
    return;
  }
  if (!at) {
    script_drop(p);
    if (!len || (p->script = take(OVERLAY_SCRIPT_POOL + len)) == NO_OFF)
      return;
    memset(&r, 0, sizeof r);
    r.len = (uint16_t) len;
  } else {
    if (p->script == NO_OFF)
      return;
    load(p, &r);
    if (r.wait != W_LOADING || at != r.pc || len != r.len) {  // not the one coming in
      script_drop(p);
      return;
    }
  }
  memcpy(pool + p->script + OVERLAY_SCRIPT_POOL + at, b + OVERLAY_SCRIPT_BYTES, k);
  r.pc = (uint16_t) (at + k);
  save(p, &r);
  if (r.pc < r.len)
    return;
  if (!check(p, bytes(p), len)) {
    script_drop(p);
    return;
  }
  // From its first step and the first frame, all of them round and round
  const slot_t *s = &slots[p->slot];
  r.pc = 0;
  r.wait = W_RUN;
  r.start = lv_tick_get();
  r.rate = 100;
  r.to = (uint16_t) LV_MAX(s->frames, 1) - 1;
  if (p->frame != NO_OFF && plays(p)) {
    first_frame(s, pool + p->frame);
    p->next = s->loop_at;
    p->due = r.start + frame_ms(s, &r, records(s)[OVERLAY_RECORD_DELAY]);
    lv_area_t a = {0, 0, (lv_coord_t) (s->w - 1), (lv_coord_t) (s->h - 1)};
    changed(p, &a);
    wake();
  } else if (!plays(p)) {
    p->due = period(s, records(s)[OVERLAY_RECORD_DELAY]);  // its rounds from now
  }
  save(p, &r);
  run(p);
}

bool script_plays(const place_t *p) { return p->script != NO_OFF && head(p, offsetof(run_t, wait)) != W_LOADING; }

bool script_moves(const place_t *p) {
  uint8_t w = head(p, offsetof(run_t, wait));
  return p->script != NO_OFF && (w == W_MS || w == W_UNTIL || w == W_YIELD || (!plays(p) && w != W_DONE));
}

void script_steps(place_t *p, uint32_t ms) {
  if (p->script == NO_OFF || head(p, offsetof(run_t, wait)) == W_LOADING)
    return;
  const slot_t *s = &slots[p->slot];
  if (!plays(p) && s->rate) {  // a round each time its first record's delay goes by
    uint32_t left = ms;
    while (p->script != NO_OFF && left >= p->due) {
      left -= p->due;
      p->due = period(s, records(s)[OVERLAY_RECORD_DELAY]);
      round_over(p);
    }
    p->due -= left;
  }
  if (p->script == NO_OFF)
    return;
  run_t r;
  load(p, &r);
  if (r.wait == W_MS) {
    r.left -= (int32_t) LV_MIN(ms, INT32_MAX);
    if (r.left > 0) {
      save(p, &r);
      return;
    }
  } else if (r.wait != W_YIELD && r.wait != W_UNTIL) {
    return;
  }
  r.wait = W_RUN;
  save(p, &r);
  run(p);
}

void script_round(place_t *p) {
  if (p->script != NO_OFF && head(p, offsetof(run_t, wait)) != W_LOADING)
    round_over(p);
}

uint32_t script_play(place_t *p, uint32_t now) {
  run_t r;
  load(p, &r);
  if (r.wait == W_LOADING || !r.rate)
    return 1000;
  if ((int32_t) (p->due - now) > 0)
    return p->due - now;
  const slot_t *s = &slots[p->slot];
  if (!more(s, p->next) && r.frame == r.to) {  // its clip's last frame is over
    r.turned = false;
    save(p, &r);
    round_over(p);
    load(p, &r);
    if (!r.turned && r.rate)
      to_frame(p, &r, r.from);
  } else {
    lv_area_t a = {0, 0, -1, -1};
    bool starts = !more(s, p->next);
    p->due += frame_ms(s, &r, advance(s, pool + p->frame, &p->next, &a));
    if (starts)
      r.frame = (uint16_t) ((r.frame + 1u) % s->frames);
    if (a.x2 >= a.x1)
      changed(p, &a);
  }
  save(p, &r);
  if (!r.rate)
    return 1000;
  if ((int32_t) (p->due - now) <= 0)
    p->due = now + OVERLAY_FRAME_MIN_MS;  // fell behind, like after the backlight was off
  return p->due - now;
}

uint8_t script_tag(const place_t *p) { return head(p, offsetof(run_t, tag)); }

void script_clear(void) {
  memset(shared, 0, sizeof shared);
  mark_bits = 0;
}

int32_t overlay_mark(void) {
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (mark_bits >> k & 1) {
      mark_bits &= (uint8_t) ~(1u << k);
      return k | mark_tag[k] << 8;
    }
  return -1;
}
