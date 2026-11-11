// Screensaver overlays from the ESP (K_OVERLAY_BEGIN), drawn over the picture. RAM only. A slot holds an overlay, a
// place shows one somewhere on the screen. An animated one's records are the deflated rectangles that changed since
// the last frame, so we only redraw those, and the places of a slot show the same frame unless one has a frame of its
// own. A still one is kept packed in pieces, unpacked as they're drawn. A field has neither, what it shows is worked
// out as it's drawn.
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

void overlay_init(void);
// Transfer header from picture.c. -1 if we take it and *len = record bytes to come, else a PIC_* error.
int overlay_open(const uint8_t *b, int n, uint32_t *len);
void overlay_bytes(uint32_t at, const uint8_t *b, unsigned n);  // record bytes at offset at
bool overlay_close(void);                                       // all bytes are in. false if the records are bad
void overlay_abort(void);                                       // the transfer failed, drop the slot
bool overlay_clear(const uint8_t *b, int n);                    // K_OVERLAY_CLEAR. true if it dropped the one coming in
void overlay_clear_all(void);
void overlay_msg(uint16_t key, const uint8_t *b, int n);  // K_OVERLAY_FRAMES to K_OVERLAY_SCRIPT
void overlay_part(const uint8_t *b, int n);               // K_PIC_PART
void overlay_parts_clear(void);                           // the picture went, and its parts with it
int32_t overlay_gone(void);                               // the next K_OVERLAY_GONE to send, -1 for none
int32_t overlay_mark(void);                               // the next K_OVERLAY_MARK, -1 for none
unsigned overlay_used(void);                              // bytes of OVERLAY_POOL in use
// The object that draws the places on parent, at its top left. They play while it exists.
lv_obj_t *overlay_show(lv_obj_t *parent);

// A place as it is now, for the tests
typedef struct {
  int16_t x, y;             // where it is
  int16_t pan[2], w, h;     // where its frame is in its box, and the box, px at full size
  int16_t deg, size, opa;   // its turn, and size and opacity in 1/1000
  int16_t size_xy[2];       // size_x and size_y, in 1/1000
  int16_t light;            // in 1/1000
  uint16_t pivot[2];        // in 1/1000 of its box
  uint8_t view;             // OVERLAY_VIEW_*
  uint8_t tiles;            // bit 0 drawn, then over the right edge, the bottom one, and both
  uint8_t order;            // drawn after order others
  uint8_t depth;            // its OVERLAY_DEPTH
  lv_area_t box;            // what it's drawn in, from the top left of overlay_show()'s object
  bool still;               // OVERLAY_STILL
  lv_color_t pal[16];       // its colours as it shows them
  lv_opa_t colour_opa[16];  // and their opacities
} overlay_look_t;
bool overlay_look(int place, overlay_look_t *l);  // false if there's no such place
bool overlay_covered(int place, int x, int y);    // a part of the picture is in front of place at x, y, as box is
// The frame place shows, w x h at a byte a pixel into px: its colour, 16 where a still has none. False if there's no
// such place, or no memory to unpack a still.
bool overlay_pixels(int place, uint8_t *px);
