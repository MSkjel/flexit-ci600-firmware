// The encoders with their work blocks from malloc, for the tests. The ESP hands them blocks of its own.
#pragma once
#include "pdeflate.h"
#include "plink.h"
#include <stdlib.h>

// The blocks for up to len bytes of input. Any of them NULL if there wasn't the memory.
static inline pdeflate_work_t enc_work(uint32_t len) {
  return (pdeflate_work_t) {malloc(PDEFLATE_ENC), malloc(PDEFLATE_TOK(len)), malloc(PDEFLATE_REST(len))};
}
static inline void enc_free(pdeflate_work_t *wk) {
  free(wk->rest);
  free(wk->tok);
  free(wk->enc);
}

// pdeflate_in(), 0 too if the blocks couldn't be had
static inline uint32_t pdeflate(const uint8_t *in, unsigned width, unsigned stride, uint32_t len, uint8_t *out,
                                uint32_t cap) {
  if (len > 65535)
    return 0;
  pdeflate_work_t wk = enc_work(len);
  uint32_t n = wk.enc && wk.tok && wk.rest ? pdeflate_in(&wk, in, width, stride, len, out, cap) : 0;
  enc_free(&wk);
  return n;
}

// plink_overlay_record_in()
static inline unsigned plink_overlay_record(const uint8_t *prev, const uint8_t *cur, unsigned w, unsigned h,
                                            uint8_t delay, uint8_t *out) {
  pdeflate_work_t wk = enc_work((w + 1) / 2 * h);
  unsigned n = wk.enc && wk.tok && wk.rest ? plink_overlay_record_in(&wk, prev, cur, w, h, delay, out) : 0;
  enc_free(&wk);
  return n;
}
