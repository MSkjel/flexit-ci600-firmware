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
int sim_stale(void);                               // redraw the whole screen and count the pixels that changed
void sim_draw(lv_obj_t *o, const lv_area_t *a, lv_color_t bg, lv_color_t *buf);  // what o draws over a, into buf
// 0xRRGGBB as the panel draws it, and back with the low bits filled from the high ones
uint16_t sim_rgb565(uint32_t rgb);
uint32_t sim_rgb888(uint16_t c);
// A JPEG of the screen decoded, and the mean error per colour channel against what the screen shows, of the tile
// that's furthest off. -1 if it isn't a whole 320x240 JPEG. Text at quality 80 is 12 off, a stale tile 30 or more.
double sim_jpeg_off(const uint8_t *jpeg, size_t n);
const uint32_t *sim_jpeg_rgb(void);  // the pixels it decoded, 0xRRGGBB
// The ESP's picture and animation messages as plink_keys.h lays them out, into b. Each returns its length. pal is
// 16 colours, NULL for none.
int sim_pic_begin(uint8_t *b, uint8_t id, int format, int w, int h, uint32_t len, const uint32_t *pal);
int sim_pic_patch(uint8_t *b, uint8_t id, int y, int count, uint32_t len, const uint32_t *pal);
int sim_anim_begin(uint8_t *b, uint8_t id, int slot, int w, int h, int scale, int transp, uint32_t len,
                   const uint32_t *pal);
int sim_anim_place(uint8_t *b, int place, int slot, int x, int y, int own, int start, int fade);
int sim_anim_glide(uint8_t *b, int place, unsigned channels, int ms, int ease, const int *to);  // a value a channel
int sim_anim_drift(uint8_t *b, int place, unsigned channels, const int *v);  // px or degrees a second

// ppm.c, doesn't need LVGL
void sim_ppm(const char *path, int w, int h, const uint32_t *rgb);  // pixels are 0xRRGGBB, exits on error
void sim_mkdir(const char *dir);                                    // like mkdir -p, exits on error

extern int sim_trial_answer;  // plat_trial_end()'s keep, -1 until the next one

// sim_model.c
struct item;
void sim_model_init(void);
extern bool sim_fresh;  // plat_fresh(), the panel started without settings
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
void sim_text(char *out, int size, const char *both);  // the part in the panel's language
void sim_item(int i, struct item *it);                 // SIM_ITEMS[i] as the panel stores it
const char *sim_item_label(const char *name);

// sim_esp.c, a fake ESP talking to the panel over a fake CX60 bus
void sim_esp_init(void);
void sim_esp_items(void);  // send all of SIM_ITEMS
void sim_esp_fill(void);   // send filler items up to N_ITEMS, on the smart home page
void sim_esp_notify(const char *text, int icon, int level);
void sim_esp_set(uint16_t key, uint32_t value);
void sim_esp_saver_line(int i, int size, const char *text);  // line i of the screensaver's, size in px
unsigned sim_esp_send_picture(void);
unsigned sim_esp_send_photo(int *quality);  // a photo-like picture, as a JPEG at the best quality that fits
unsigned sim_esp_send_moon(int *y, int *count);
void sim_esp_send_dawn(void);
unsigned sim_esp_send_animation(int x, int y);  // into slot 0, placed at x, y
void sim_esp_place_animation(int place, int slot, int x, int y, int own, int start);
void sim_esp_move_animation(uint16_t key, int a, int b, int ms);  // GLIDE or DRIFT of place 0: x, y or vx, vy
void sim_link_run(int cycles);                                    // run that many CX60 cycles
int sim_link_settle(void);  // run until both ends are idle, returns the cycles it took or -1
void sim_esp_report(const char *what);
void sim_esp_watch(uint16_t key);  // sim_esp_seen() tells if a message with it came from the panel since
bool sim_esp_seen(void);
void sim_esp_shot(int quality, bool all);  // a K_SHOT
// Tiles in the SHOT_DONE since, -1 for none yet. bytes of its PIC_DATA so far, bad heads or what isn't zeros after.
int sim_esp_shot_done(unsigned long *bytes, unsigned long *bad);
extern int sim_esp_late;  // answers that ended after the CX60's quiet time, a sim run fails on any
// The JPEG of the tiles so far, 0 if one is missing or it doesn't fit cap
unsigned sim_esp_shot_jpeg(uint8_t *out, unsigned cap, int quality);
extern unsigned long sim_cycles;

#ifdef __cplusplus
}
#endif
