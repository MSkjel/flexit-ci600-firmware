// Bitmaps in 32-bit words: a bit for each of some numbered things
#pragma once
#include <stdbool.h>
#include <stdint.h>

static inline bool bit(const uint32_t *map, unsigned i) { return map[i / 32] >> (i % 32) & 1; }

static inline void bit_put(uint32_t *map, unsigned i, bool on) {
  if (on)
    map[i / 32] |= 1u << (i % 32);
  else
    map[i / 32] &= ~(1u << (i % 32));
}

// The lowest bit set in the map's words, -1 if none is
static inline int bit_first(const uint32_t *map, unsigned words) {
  for (unsigned w = 0; w < words; w++)
    if (map[w])
      return (int) (w * 32 + (unsigned) __builtin_ctz(map[w]));
  return -1;
}
