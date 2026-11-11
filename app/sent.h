// What the LCD was last sent, as a checksum for each tile of it, so the tiles LVGL drew the same again don't have
// to go to it. Sending a strip takes about as long as drawing it, and a weather animation redraws most of the
// screen each step to change a few pixels of it.
#pragma once
#include "lvgl.h"
#include "ui.h"

#define SENT_TILE_W 16
#define SENT_TILE_H 8                     // the lines LVGL's strips end on, see refr_area() in lib/lvgl-ci600.patch
#define SENT_STRIP (SCR_W * SENT_TILE_H)  // LVGL's draw buffer in pixels, a row of tiles across the screen

// The display's flush has strip a's pixels px, row by row. A bit for each tile it's on that the LCD doesn't show
// like that yet, from the top left one along their rows: those that changed, and those it only has a part of. They
// count as sent from here on, and screenshots hear of them.
uint32_t sent_changed(const lv_area_t *a, const lv_color_t *px);
// The same without comparing, for when that takes longer than sending: all of them
uint32_t sent_all(const lv_area_t *a);
// The next of those tiles to send, taken out of changed: tiles side by side, with the same ones of the rows below
// where no more than they changed, as much of them as a has. Returns run's first pixel in px, where its rows are a's
// width apart, NULL when none are left.
const lv_color_t *sent_run(const lv_area_t *a, const lv_color_t *px, uint32_t *changed, lv_area_t *run);
