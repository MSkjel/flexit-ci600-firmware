// see bus.h
#include "bus.h"
#include "link.h"
#include "mbserver.h"

int bus_burst(const uint8_t *rx, int n, uint32_t first_us, uint32_t idle_us, uint32_t ms, uint8_t *tx, bool *modbus) {
  int skip = 0;  // bytes in front of the CX60's
  *modbus = false;
  if (n > 0 && rx[0] == PBULK_ADDR) {  // the ESP link, or noise that looks like it
    int k = pbulk_frame_len(rx, n);
    if (k == n)
      return link_bulk_frame(rx, n, idle_us, ms, tx);
    if (k && k < n && pbus_crc_ok(rx, k))
      skip = k;  // the CX60 sent right behind a link frame. That goes unanswered, the ESP sends it again
    else if (pbus_is_cx60(rx + 1, n - 1))
      skip = 1;  // a noise byte in front of a CX60 frame
    else
      return 0;  // a link frame cut short, or noise
  }
  *modbus = true;
  link_bus(rx + skip, n - skip, first_us + (uint32_t) skip * PBUS_BYTE_US, idle_us, ms);
  return mb_chunk(rx + skip, n - skip, tx);
}
