// The overlays' insides, shared by overlay.c, overlay_motion.c and overlay_draw.c. See overlay.h.
#pragma once
#include "overlay.h"
#include "plink_keys.h"

#define MOVE_MS 50     // a motion step, 20 a second
#define MOVE_MAX 20    // steps the motion catches up at most, after a stall
#define NO_OFF 0xFFFF  // no offset in the pool
#define FULL 1000      // size, opacity and speed are in 1/1000
#define MILLI 1000     // x and y in 1/1000 px, turns in 1/1000 degree
#define TURN (360 * MILLI)
_Static_assert(OVERLAY_POOL < NO_OFF, "16-bit offsets and lengths in the pool, and NO_OFF");

typedef struct {
  bool ready, leaving;     // leaving: goes with its last place
  bool still;              // OVERLAY_STILL: no frame, its pieces are unpacked as they're drawn
  uint8_t scale;           // 1 or 2
  uint16_t w, h;           // size in frame pixels
  uint16_t off;            // its frame in pool, an animated one's, the records right after. NO_OFF = empty slot
  uint16_t rate;           // frame rate in percent, 0 = paused
  uint16_t len;            // record bytes
  uint16_t loop_at, next;  // loop_at = the second record, where we loop back to. next = record to show next
  uint16_t last;           // the record that takes the last frame back to the first, NO_OFF for a still picture
  uint16_t frames;         // records in the loop
  uint32_t due;            // tick for the next record
  lv_color_t pal[16];
  uint8_t opa[8];  // the colours' opacities, 0 clear to 15, two a byte, the first's in the high nibble
} slot_t;

// A value on its way from one to another
typedef struct {
  int32_t from, to;
  uint16_t ms;          // over ms, 0 = still at to
  uint16_t steps : 14;  // MOVE_MS steps of it gone
  uint16_t ease : 2;    // OVERLAY_EASE_*
} ramp_t;

// A place's channel going between lo and hi on its own, see K_OVERLAY_WAVE
typedef struct {
  uint8_t place, ch;  // place OVERLAY_PLACES = free
  uint8_t jitter, flags;
  bool up;                  // the last leg went up
  int16_t lo, hi;           // in the channel's units
  uint16_t ms[2], hold[2];  // the whole way up and down, the holds at hi and lo
  uint16_t left;            // ms left of the hold
  ramp_t r;                 // what it adds or its share now, see unit()
} wave_t;

enum { STAY, LEAVE, LEAVE_GONE };  // LEAVE_GONE answers OVERLAY_GONE once it's gone

typedef struct {
  int16_t deg, size, speed, opa;  // size, speed and opacity in 1/1000
  uint8_t flip;
} look_t;

typedef struct {
  uint8_t slot;  // OVERLAY_SLOTS = empty place
  uint8_t flip;  // OVERLAY_FLIP_X, OVERLAY_FLIP_Y
  uint8_t
      drifting;     // bit per axis, a drift put it there. On OVERLAY_EDGE_SCREEN what's over an edge shows at the other
  uint8_t tiles;    // bit k: tile k of drawn is on the layer, see tile(). 0 = not drawn
  uint8_t leaving;  // STAY, or fading out to go
  bool held;        // its own frame stays at the last one
  uint8_t tag;      // OVERLAY_ROUNDS's, for OVERLAY_GONE
  uint8_t depth;    // K_OVERLAY_DEPTH's
  uint16_t frame;   // a frame of its own in pool, NO_OFF = the slot's
  uint16_t next;    // the next record of its own frame
  uint32_t due;     // tick for that record. A still picture's ms left of its round instead
  uint16_t rounds, rounds_fade;                     // rounds to the end of, 0 = on and on
  ramp_t ch[OVERLAY_CHANNELS];                      // what glides and drifts set, see unit()
  int32_t rate[OVERLAY_TURN + 1];                   // the drifts of x, y and turn, per second
  int16_t map[OVERLAY_CHANNELS - OVERLAY_TURN][4];  // turn, size, opacity, speed at the area's left, right, top, bottom
  ramp_t fade;                                      // 1/1000, fades it in when placed and out when it leaves
  look_t now;                                       // what it shows, all its channels together
  int16_t x, y;                                     // where it is, can be partly off the screen
  lv_area_t drawn;                                  // its box on the layer, as drawn
  struct {
    int16_t x0, y0, x1, y1;
    uint8_t edge[2];  // OVERLAY_EDGE_* for x and y
  } area;
  struct {
    bool on, resting;
    uint16_t speed[2], leg[2], rest[2];  // min, max
    int16_t heading, spread;
    uint16_t left;  // ms of this leg or rest
  } wander;
  struct {
    bool on;
    uint8_t flags;  // OVERLAY_FACE_*
    int16_t facing, tilt, rate;
  } face;
  uint32_t rng;
} place_t;

extern slot_t slots[OVERLAY_SLOTS];
extern place_t places[OVERLAY_PLACES];
extern wave_t waves[OVERLAY_WAVES];
extern lv_obj_t *layer;  // draws every place on the screensaver, while it's shown
// what size, opacity and speed can be
extern const int32_t SHARE_LO[OVERLAY_CHANNELS], SHARE_HI[OVERLAY_CHANNELS];

static inline bool looping(const slot_t *s) { return s->loop_at < s->len; }
static inline lv_opa_t colour_opa(const slot_t *s, int i) {
  return (lv_opa_t) ((s->opa[i / 2] >> (i & 1 ? 0 : 4) & 15) * 17);
}
static inline int32_t wrap(int32_t v, int32_t m) { return (v % m + m) % m; }  // to 0..m
// 1/1000 degree to -180..180
static inline int32_t half(int32_t a) {
  a = wrap(a, TURN);
  return a > TURN / 2 ? a - TURN : a;
}
static inline bool scales(int c) { return c >= OVERLAY_SIZE; }
// Ours from the messages' units: 1/1000 px and degree, and 1/1000 of full for percent
static inline int32_t unit(int c) { return scales(c) ? FULL / 100 : MILLI; }
static inline int32_t no_change(int c) { return scales(c) ? FULL : 0; }
static inline bool wraps(const place_t *p, int axis) {
  return (p->drifting >> axis & 1) && p->area.edge[axis] == OVERLAY_EDGE_SCREEN;
}

// overlay.c
const uint8_t *place_frame(const place_t *p);  // the frame it shows, an animated slot's
uint8_t *records(const slot_t *s);
// The record at offset at: an animated slot's into frame where it goes, a still's piece unpacked into frame at w / 2
// bytes a row. Only checked if frame is NULL. Returns its length, 0 if it's bad. area gets its rectangle, in frame
// pixels.
uint32_t record(const slot_t *s, uint32_t at, uint8_t *frame, lv_area_t *area);
void gone(place_t *p);                       // it faded out, and goes
void still_rounds(place_t *p, uint32_t ms);  // ms more of a still picture's rounds
void wake(void);                             // something new to play

// overlay_motion.c
int32_t ramp_at(const ramp_t *r);
void ramp_to(ramp_t *r, int32_t to, uint16_t ms, unsigned ease);
void ramp_set(ramp_t *r, int32_t v);
wave_t *wave_of(const place_t *p, int c);  // NULL if channel c doesn't wave
bool stopping(const wave_t *w);
void wave_settle(wave_t *w);
int32_t mapped(const place_t *p, int c);
void update(place_t *p);  // what it shows, redrawn if that changed
void new_leg(place_t *p);
bool moving(const place_t *p);
void wake_mover(void);
void motion_init(void);

// overlay_draw.c
lv_area_t box(const place_t *p);
void refresh(place_t *p, const look_t *was, const lv_area_t *b);
void hide(place_t *p);
void order(void);
void set_depth(place_t *p, uint8_t depth);
void changed(const place_t *p, const lv_area_t *a);
void draw_init(void);
