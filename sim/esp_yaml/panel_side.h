// The panel's side of the ESP package test, see panel_side.c
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pn_init(void);
void pn_restart(void);
void pn_reboot(int wiped);
void pn_poll(void);
void pn_second(void);
int pn_online(void);
int pn_get(uint16_t key);
int pn_reg(uint16_t reg);
void pn_set_reg(uint16_t reg, int v);
void pn_set(uint16_t key, int v);
void pn_action(int a);
int pn_idle(void);
const char *pn_note(int *icon, int *level, int *fresh);
void pn_clock(int *y, int *mo, int *d, int *h, int *mi);
int pn_item(int i, int *flags, int *icon, int *dec, int *step, int *id, int *min, int *max, int *value,
            const char **label, const char **unit);
int pn_item_level(int i, int k);
const char *pn_item_text(int i);
const char *pn_item_help(int i);
int pn_pic(int *w, int *h);
int pn_pic_format(void);
int pn_pic_line(int y, uint16_t *out);
// A place of the animations as the panel has it, see anim_look_t
typedef struct {
  int x, y, deg, flip, size, opa;
} pn_look_t;
int pn_anim(int place, pn_look_t *l);
int pn_anim_line(int place, int y, int w, int scale, int transp, uint8_t *out);
void pn_anim_step(int ms);
unsigned pn_anim_used(void);
void pn_bus(const uint8_t *b, int n, uint32_t start_us);
int pn_bulk(const uint8_t *f, int n, uint32_t idle_us, uint8_t *answer);
void pn_saver(int on);
void pn_shot(const char *dir, const char *name);
void pn_draw(int ms);  // ms on, the panel draws as its main loop does

#ifdef __cplusplus
}
#endif
