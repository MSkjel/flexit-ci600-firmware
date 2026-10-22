// Screensaver animation from the ESP (K_ANIM_BEGIN), played over the picture. RAM only. Each record is the
// deflated rectangle that changed since the last frame, so we only redraw that.
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

void anim_init(void);  // registers the LVGL decoder
// Transfer header from picture.c. -1 if we take it and *len = record bytes to come, else a PIC_* error.
int anim_open(const uint8_t *b, int n, uint32_t *len);
void anim_bytes(uint32_t at, const uint8_t *b, unsigned n);  // record bytes at offset at
bool anim_close(void);                                       // all bytes are in. false if the records are bad
void anim_clear(void);
bool anim_ready(void);
void anim_move(const uint8_t *b, int n);  // K_ANIM_MOVE
void anim_pos(int16_t *x, int16_t *y);    // where it is now
lv_obj_t *anim_show(lv_obj_t *parent);    // creates the image, which plays while it exists. NULL if no animation
