// The fake CX60's frames, for the tests that put one on the bus. After pbus.h, which esp_test has from the ESP repo.
#pragma once
#include <string.h>

// f[n]: head[k], then the CRC
static inline void cx60_frame(uint8_t *f, int n, const uint8_t *head, int k) {
  memcpy(f, head, (size_t) k);
  pbus_put_crc(f, n - 2);
}

// The status broadcast, PBUS_STATUS_LEN bytes, made up after its head
static inline void cx60_status(uint8_t *f) {
  for (int i = 0; i < PBUS_STATUS_LEN; i++)
    f[i] = (uint8_t) i;
  cx60_frame(f, PBUS_STATUS_LEN, PBUS_STATUS_HEAD, sizeof PBUS_STATUS_HEAD);
}

// The settings poll of a panel, 8 bytes
static inline void cx60_poll(uint8_t *f, uint8_t panel) {
  const uint8_t head[] = {panel, 0x01, 0x00, 0x00, 0x00, 0x40};
  cx60_frame(f, 8, head, sizeof head);
}

// Panel 3's reply to it with nothing flagged, 13 bytes
static inline void cx60_reply(uint8_t *f) {
  static const uint8_t head[] = {3, 0x01, 8};
  memset(f, 0, 13);
  cx60_frame(f, 13, head, sizeof head);
}
