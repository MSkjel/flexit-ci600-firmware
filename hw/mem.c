// memset, memcpy and memmove a word at a time, newlib-nano's go a byte at a time. The M3 reads words at any
// address, so only the destination gets aligned.
#include <stdint.h>
#include <string.h>

typedef uint32_t __attribute__((aligned(1), may_alias)) any32_t;  // a word at any address

void *memset(void *d, int c, size_t n) {
  uint8_t *p = d;
  if (n >= 8) {
    uint32_t w = (uint8_t) c * 0x01010101u;
    for (; (uintptr_t) p & 3; n--)
      *p++ = (uint8_t) c;
    for (; n >= 16; n -= 16, p += 16) {
      uint32_t *q = (uint32_t *) p;
      q[0] = q[1] = q[2] = q[3] = w;
    }
    for (; n >= 4; n -= 4, p += 4)
      *(uint32_t *) p = w;
  }
  while (n--)
    *p++ = (uint8_t) c;
  return d;
}

// Upwards, so also for overlaps with d below s
static void up(uint8_t *p, const uint8_t *s, size_t n) {
  if (n >= 8) {
    for (; (uintptr_t) p & 3; n--)
      *p++ = *s++;
    if ((uintptr_t) s & 3)
      for (; n >= 4; n -= 4, p += 4, s += 4)
        *(uint32_t *) p = *(const any32_t *) s;
    else
      for (; n >= 16; n -= 16, p += 16, s += 16) {  // ldm and stm
        const uint32_t *a = (const uint32_t *) s;
        uint32_t w0 = a[0], w1 = a[1], w2 = a[2], w3 = a[3], *b = (uint32_t *) p;
        b[0] = w0;
        b[1] = w1;
        b[2] = w2;
        b[3] = w3;
      }
    for (; n >= 4; n -= 4, p += 4, s += 4)
      *(uint32_t *) p = *(const any32_t *) s;
  }
  while (n--)
    *p++ = *s++;
}

void *memcpy(void *restrict d, const void *restrict s, size_t n) {
  up(d, s, n);
  return d;
}

void *memmove(void *d, const void *s, size_t n) {
  if ((uintptr_t) d - (uintptr_t) s >= n) {  // d below s or past its end
    up(d, s, n);
    return d;
  }
  uint8_t *p = (uint8_t *) d + n;
  const uint8_t *q = (const uint8_t *) s + n;
  if (n >= 8) {
    for (; (uintptr_t) p & 3; n--)
      *--p = *--q;
    for (; n >= 4; n -= 4) {
      p -= 4;
      q -= 4;
      *(uint32_t *) p = *(const any32_t *) q;
    }
  }
  while (n--)
    *--p = *--q;
  return d;
}
