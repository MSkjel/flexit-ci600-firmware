// Shared helpers for the simulator and the tests.
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// simlib.c
void sim_display_init(void);                       // lv_init() and a 320x240 display in memory
void sim_run(int ms);                              // advance LVGL's tick and run its timers, in 5 ms steps
extern unsigned long sim_px;                       // pixels drawn so far
bool sim_shot(const char *dir, const char *name);  // save the screen as DIR/NAME.ppm, false if LVGL's heap ran low
uint32_t sim_pixel(int x, int y);                  // on the screen as drawn now, 0xRRGGBB

// ppm.c, doesn't need LVGL
void sim_ppm(const char *path, int w, int h, const uint32_t *rgb);  // pixels are 0xRRGGBB, exits on error
void sim_mkdir(const char *dir);                                    // like mkdir -p, exits on error

// sim_model.c
struct item;
void sim_model_init(void);
// An item of the fake ESP. Texts are "English|Norsk". The first SIM_N_COMFORT are on the panel from the start.
typedef struct {
  const char *name;
  uint8_t type, flags, icon, dec;
  uint16_t step;
  int32_t value, min, max;
  const char *label, *unit, *help;
  bool levels;
  int16_t level[4];
  const char *text;  // an ITEM_TEXT's value
} sim_item_t;
extern const sim_item_t SIM_ITEMS[];
extern const int SIM_N_ITEMS, SIM_N_COMFORT;
int sim_item_id(const char *name);
void sim_text(char *out, int size, const char *both);  // the part in the panel's language
void sim_item(int i, struct item *it);                 // SIM_ITEMS[i] as the panel stores it
const char *sim_item_label(const char *name);

// sim_esp.c, a fake ESP talking to the panel over a fake CU60 bus
void sim_esp_init(void);
void sim_esp_items(void);  // send all of SIM_ITEMS
void sim_esp_fill(void);   // send filler items up to N_ITEMS, on the smart home page
void sim_esp_notify(const char *text, int icon, int level);
void sim_esp_set(uint16_t key, uint32_t value);
unsigned sim_esp_send_picture(void);
unsigned sim_esp_send_photo(int *quality);  // a photo-like picture, as a JPEG at the best quality that fits
unsigned sim_esp_send_moon(int *y, int *count);
void sim_esp_send_dawn(void);
unsigned sim_esp_send_animation(int x, int y);
void sim_esp_move_animation(int mode, int a, int b, int ms);  // K_ANIM_MOVE: a, b are x, y or vx, vy
int sim_link_settle(void);  // run until both ends are idle, returns the cycles it took or -1
void sim_esp_report(const char *what);
extern unsigned long sim_cycles;

#ifdef __cplusplus
}
#endif
