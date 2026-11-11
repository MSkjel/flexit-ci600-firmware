// The overlays' insides, shared by overlay.c, overlay_motion.c, overlay_script.c and overlay_draw.c. See overlay.h.
#pragma once
#include "overlay.h"
#include "plink_keys.h"

#define MOVE_MS 50     // a motion step, 20 a second
#define MOVE_MAX 20    // steps the motion catches up at most, after a stall
#define NO_OFF 0xFFFF  // no offset in the pool
#define FULL 1000      // the shares are in 1/1000
#define MILLI 1000     // x and y in 1/1000 px, turns in 1/1000 degree, phase in 1/1000 colour
#define TURN (360 * MILLI)
#define FADE OVERLAY_CHANNELS  // a channel of our own: its fade in when placed and out when it leaves, a share
#define MOVES 32               // glides and drifts at a time, of all places together
#define PHASES 720720          // colours a drifting phase goes round, any number of cycling ones a whole many times
_Static_assert(OVERLAY_POOL < NO_OFF, "16-bit offsets and lengths in the pool, and NO_OFF");

typedef struct {
  bool ready, leaving;     // leaving: goes with its last place
  bool still;              // no frame: OVERLAY_STILL's or _SHEET's pieces unpacked as they're drawn, or a field
  bool sheet;              // OVERLAY_SHEET: a frame of them after each record that starts one
  bool field;              // OVERLAY_FIELD: its data is what it is, see field_at()
  uint8_t scale;           // 1 or 2
  uint16_t w, h;           // size in frame pixels, rows of row_bytes()
  uint16_t off;            // its frame in pool, an animated one's, the records right after. NO_OFF = empty slot
  uint16_t rate;           // frame rate in percent, 0 = paused
  uint16_t len;            // record bytes
  uint16_t loop_at, next;  // loop_at = the second record, where we loop back to. next = record to show next
  uint16_t last;           // the record that takes the last frame back to the first, NO_OFF for a still picture
  uint16_t at;             // a sheet's: the record that starts the frame it shows
  uint16_t frames;         // in the loop, not counting the records that only make one longer
  uint32_t due;            // tick for the next record
  lv_color_t pal[16];
  uint8_t opa[8];    // the colours' opacities, 0 clear to 15, two a byte, the first's in the high nibble
  uint16_t glows;    // a bit for each colour that light and tint leave as it is
  uint16_t cycles;   // and for each that a place's phase takes round
  lv_color_t shade;  // what light takes its colours toward below FULL
} slot_t;

// A channel on its way. The place has where a glide ends already, so what's left of d is added to that.
typedef struct {
  int32_t d;            // how far a glide had to go when it started, or a drift's speed a second
  uint16_t ms;          // a glide's, 0 for a drift
  uint16_t steps : 14;  // MOVE_MS steps of it gone
  uint16_t ease : 2;    // OVERLAY_EASE_*
  uint8_t place, ch;    // place OVERLAY_PLACES = free
} move_t;

enum { STAY, LEAVE, LEAVE_GONE };  // LEAVE_GONE answers OVERLAY_GONE once it's gone

// What a place shows, all its channels together
typedef struct {
  int16_t deg, size, size_xy[2], speed, opa, light, level;  // the shares in 1/1000
  int16_t phase;                                            // 1/256 colour, 0 to 16 colours
  int16_t pan[2], box[2];                                   // px at full size
  uint16_t pivot[2];                                        // 1/1000 of its box
  uint16_t sub[2];                                          // what x and y are past the whole px, in 1/1000
  uint8_t view;                                             // OVERLAY_VIEW_*
  uint8_t depth;
} look_t;

typedef struct {
  uint8_t slot;  // OVERLAY_SLOTS = empty place
  uint8_t view;  // OVERLAY_VIEW_*
  // bit per axis, a drift put it there. On OVERLAY_EDGE_SCREEN what's over an edge shows at the other
  uint8_t drifting;
  uint8_t tiles;    // bit k: tile k of drawn is on the layer, see tile(). 0 = not drawn
  uint8_t leaving;  // STAY, or fading out to go
  uint8_t bound;    // its binds, see overlay_script.c
  bool own;         // plays frames of its own
  bool jumped;      // a wrap or a crossing put it somewhere else this step, which isn't the way it went
  uint16_t rows;    // what's bound to its rows makes of them, see script_rows(). 0 = nothing is
  uint16_t frame;   // an animated slot's frame of its own in pool, NO_OFF for none
  uint16_t at;      // or a sheet's that it shows, the record it starts at
  uint16_t next;    // the next record of its own frame
  uint16_t script;  // its script in pool, NO_OFF = none, see overlay_script.c
  uint16_t shape;   // its script's SHAPE runs, from the start of its bytes. NO_OFF = none, all of its box
  uint16_t soft;    // its script's SOFT edges, from the start of its bytes. NO_OFF = none
  uint16_t roam;    // its roam_t in pool, NO_OFF = it neither wanders nor faces where it goes
  int32_t tint;     // K_OVERLAY_TINT's 0xRRGGBB, NO_TINT for none
  uint32_t due;     // tick for that record. A still picture's ms left of its round instead
  uint32_t born;    // tick it was placed at
  // Where glides and drifts have its channels, see ch_at(): those before size in 1/1000, then the rest and its fade
  int32_t big[OVERLAY_SIZE];
  int16_t small[FADE + 1 - OVERLAY_SIZE];
  look_t now;       // what it shows
  int16_t x, y;     // where it is, can be partly off the screen
  lv_area_t drawn;  // its box on the layer, as drawn
  struct {
    int16_t x0, y0, x1, y1;
    uint8_t edge[2];  // OVERLAY_EDGE_* for x and y
  } area;
  uint32_t rng;
} place_t;
#define NO_TINT (-1)

// How a place gets about on its own, in the pool for those that do
typedef struct {
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
} roam_t;
_Static_assert(sizeof(roam_t) == OVERLAY_ROAM, "what the ESP counts a wander or a face as");

extern slot_t slots[OVERLAY_SLOTS];
extern place_t places[OVERLAY_PLACES];
extern move_t moves[MOVES];
extern lv_obj_t *layer;  // draws every place on the screensaver, while it's shown
// what the channels from size on can be
extern const int16_t CH_LO[FADE + 1 - OVERLAY_SIZE], CH_HI[FADE + 1 - OVERLAY_SIZE];

static inline bool looping(const slot_t *s) { return s->loop_at < s->len; }
static inline unsigned row_bytes(const slot_t *s) { return (s->w + 1u) / 2; }
// Its box at full size, px across or down
static inline int shown(const place_t *p, int axis) { return p->now.box[axis]; }
// Its pivot on the screen, across or down: where it is
static inline int pivot_at(const place_t *p, int axis) {
  return (axis ? p->y : p->x) + shown(p, axis) * p->now.pivot[axis] / OVERLAY_PIVOT_MAX;
}
static inline lv_opa_t colour_opa(const slot_t *s, int i) {
  return (lv_opa_t) ((s->opa[i / 2] >> (i & 1 ? 0 : 4) & 15) * 17);
}
static inline int32_t wrap(int32_t v, int32_t m) { return (v % m + m) % m; }  // to 0..m
// a / b down to a whole number, b > 0
static inline int32_t floor_div(int32_t a, int32_t b) { return a >= 0 ? a / b : -((b - 1 - a) / b); }
static inline int64_t floor_div64(int64_t a, int64_t b) { return a >= 0 ? a / b : -((b - 1 - a) / b); }
static inline uint32_t isqrt(uint32_t v) {  // the root, down to a whole number
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
// A NOISE is the same again this many of its first layer's bumps on, so a pan that drifts goes round with it
static inline int noise_cells(unsigned grain) { return LV_MIN(INT16_MAX / (int) LV_MAX(grain, 1u), 64); }
_Static_assert(INT16_MAX / OVERLAY_NOISE_MAX > 0, "a round of them is a pan a glide can set");
static inline uint32_t hash32(uint32_t x) {  // all of its bits from any of x's
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  return x ^ x >> 16;
}
// 1/1000 degree to -180..180
static inline int32_t half(int32_t a) {
  a = wrap(a, TURN);
  return a > TURN / 2 ? a - TURN : a;
}
static inline bool scales(int c) { return c >= OVERLAY_SIZE && c < OVERLAY_PIVOT_X; }  // a share
// Ours from the messages' units: 1/1000 px, degree and colour, 1/1000 of full for percent, the rest as they are
static inline int32_t unit(int c) { return c < OVERLAY_SIZE ? MILLI : scales(c) ? FULL / 100 : 1; }
static inline int32_t limited(int c, int32_t v) {  // as channel c can be, see CH_LO
  return c < OVERLAY_SIZE ? v : LV_CLAMP(CH_LO[c - OVERLAY_SIZE], v, CH_HI[c - OVERLAY_SIZE]);
}
static inline bool wraps(const place_t *p, int axis) {
  return (p->drifting >> axis & 1) && p->area.edge[axis] == OVERLAY_EDGE_SCREEN;
}

// A place's own number, another each time it's placed
static inline uint32_t seed_of(const place_t *p) { return hash32(p->born ^ (uint32_t) (p - places + 1) * 0x9E3779B9u); }

// overlay.c
extern uint8_t pool[OVERLAY_POOL];
uint16_t take(uint32_t n);  // n bytes of pool, NO_OFF if they don't fit
// n bytes for p to have. NO_OFF if they don't fit, or making room took p away.
uint16_t take_for(const place_t *p, uint32_t n);
void give(uint16_t off, uint32_t n);           // back, moving what's behind them down
const uint8_t *place_frame(const place_t *p);  // the frame it shows, an animated slot's
uint16_t place_at(const place_t *p);           // a sheet's frame it shows, the record it starts at
uint8_t *records(const slot_t *s);
// The record at offset at: an animated slot's into frame where it goes, a still's piece unpacked into frame at w / 2
// bytes a row. Only checked if frame is NULL. Returns its length, 0 if it's bad. area gets its rectangle, in frame
// pixels.
uint32_t record(const slot_t *s, uint32_t at, uint8_t *frame, lv_area_t *area);
// The delay a round of a slot without frames goes by: its first record's, a field's the longest
static inline uint8_t round_delay(const slot_t *s) {
  return s->field ? OVERLAY_DELAY_MAX : records(s)[OVERLAY_RECORD_DELAY];
}
static inline bool more(const slot_t *s, uint32_t at) {  // the record at at only makes the frame before it longer
  return records(s)[at + OVERLAY_RECORD_X] == OVERLAY_RECORD_MORE;
}
static inline bool heads(const slot_t *s, uint32_t at) {  // a sheet's record at at starts a frame or makes it longer
  return records(s)[at + OVERLAY_RECORD_X] >= OVERLAY_RECORD_FRAME;
}
void gone(place_t *p);  // it faded out, and goes
void leave(place_t *p, uint16_t ms, uint8_t how);
void wake(void);                                  // something new to play
uint32_t period(const slot_t *s, uint8_t delay);  // ms a record's delay is at the slot's rate
// The next record of a frame applied, an animated slot's to frame, a sheet's to *at, *next moved on, area what it
// changed if it isn't NULL. Returns its delay.
uint8_t advance(const slot_t *s, uint8_t *frame, uint16_t *at, uint16_t *next, lv_area_t *area);
void first_frame(const slot_t *s, uint8_t *frame, uint16_t *at, uint16_t *next);  // and *next the record after it
int msg_len(uint16_t key, int n);  // what a message of n bytes to overlay_msg() takes, -1 if that's not n

// overlay_script.c
extern const char *const SCRIPT_FORMS[OVERLAY_DOS];    // what each step takes, see there
extern const uint8_t OP_TAKES[OVERLAY_OPS];            // and each OP, see there
void script_msg(place_t *p, const uint8_t *b, int n);  // K_OVERLAY_SCRIPT
void bind_msg(place_t *p, const uint8_t *b, int n);    // K_OVERLAY_BIND
void script_drop(place_t *p);                          // its script goes, and what it bound
void binds_drop(place_t *p);                           // and all its binds
void binds_moved(uint16_t off, uint32_t n);            // give()'s, for where the binds are
bool script_moves(const place_t *p);                   // it waits on the motion steps
void script_steps(place_t *p, uint32_t ms);            // ms more of them
void script_round(place_t *p);                         // a round of its clip, or of its slot's frames, ended
bool script_plays(const place_t *p);                   // it has a script that plays its own frame, all of it in
// Its own frame on if a record is due, for one that script_plays(). Returns how long until the next one.
uint32_t script_play(place_t *p, uint32_t now);
uint8_t script_tag(const place_t *p);  // its END's, for OVERLAY_GONE
// Its shape's runs and how many, see OVERLAY_DO_SHAPE: width, top and bottom, 2 bytes each. NULL without one.
const uint8_t *script_shape(const place_t *p, int *runs);
const uint8_t *script_soft(const place_t *p);  // its soft edges, see OVERLAY_DO_SOFT, 2 bytes each. NULL for none.
void script_clear(void);                       // all of them go: the shared registers back to 0, no marks to send
void script_init(void);
// Its channels from to last in v, in our units, as its binds make them. Those from the first after its registers.
void script_bound(place_t *p, int32_t *v, int from, int last);
// What its OVERLAY_BIND_ROW_*s make of a row of its box
typedef struct {
  int32_t shift;             // 1/1000 px, to the pixel or a quarter of one
  int32_t size, opa, light;  // 1/1000 of the place's
  int32_t phase;             // 1/1000 colour more
} row_t;
#define ROW_AS_IS ((row_t) {0, FULL, FULL, FULL, 0})
void script_row(const place_t *p, int row, row_t *out);
uint16_t script_rows(const place_t *p);  // a sum of some of them that's another when they change, 0 if none are bound

// overlay_motion.c
int32_t eased(unsigned e, int32_t f);       // f and the result in 1/65536 of the way
int32_t ch_at(const place_t *p, int c);     // channel c as its glide or drift has it now, in our units
void ch_set(place_t *p, int c, int32_t v);  // there at once, its glide or drift over
void ch_glide(place_t *p, int c, int32_t to, uint16_t ms, unsigned ease);  // from where it is
void ch_drift(place_t *p, int c, int32_t rate);                            // a second, 0 stops it there
bool ch_moves(const place_t *p, int c);                                    // it glides or drifts
void moves_drop(const place_t *p);                                         // all of its end where they are
void update(place_t *p);                                                   // what it shows, redrawn if that changed
bool roam(const place_t *p, roam_t *r);      // its wander and face, both off and false if it has neither
bool roam_set(place_t *p, const roam_t *r);  // from now on. False if there's no room for them.
void new_leg(place_t *p);
int32_t angle(int32_t dx, int32_t dy);                // degrees to dx right and dy down, 0 for nowhere
int32_t pick(uint32_t *rng, int32_t lo, int32_t hi);  // lo..hi at random, both in, the next from rng
bool moving(const place_t *p);
void wake_mover(void);
uint32_t since_step(void);  // ms since the last motion step, 0 if it's stopped
// The tick the motion has got to. What's worked out between two steps is all at the first one's time.
uint32_t step_tick(void);
void motion_init(void);

// overlay_draw.c
lv_area_t box(const place_t *p);
void refresh(place_t *p, const look_t *was, const lv_area_t *b);
void hide(place_t *p);
void order(void);
void redraw_place(const place_t *p);  // all of it, as something it shows changed
void changed(const place_t *p, const lv_area_t *a);
void draw_init(void);
