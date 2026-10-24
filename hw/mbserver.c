// Modbus server, see mbserver.h
#include "mbserver.h"
#include "pbus.h"
#include "plink_values.h"
#include <string.h>

mb_t MB;

uint16_t mb_crc(const uint8_t *p, int n) {
  return pbus_crc16(p, n);  // the same CRC, table-driven
}

void mb_init(uint8_t addr, uint16_t version, uint8_t patch) {
  memset(&MB, 0, sizeof MB);
  MB.addr = addr;
  MB.input[0] = FW_TYPE;
  MB.input[2] = patch;
  MB.input[3] = version;
}

static bool bit(const uint32_t *map, uint16_t reg) { return map[reg / 32] >> (reg % 32) & 1; }

// Returns false if the coil stays set: the relay was of an older value of ours and the CX60 has to read the new one.
static bool set_pending(uint16_t reg, bool on) {
  if (on)
    MB.pending[reg / 32] |= 1u << (reg % 32);
  else if (bit(MB.again, reg)) {
    MB.again[reg / 32] &= ~(1u << (reg % 32));
    return false;
  } else
    MB.pending[reg / 32] &= ~(1u << (reg % 32));
  return true;
}

bool mb_written(uint16_t reg) { return reg < MB_REGS && bit(MB.written, reg); }
bool mb_relayed(uint16_t reg) { return reg < MB_REGS && bit(MB.relayed, reg); }

// Only flags the main loop if it's the first value or a different one.
static void stored(uint16_t reg, uint16_t value, bool relayed) {
  if (relayed)
    MB.relayed[reg / 32] |= 1u << (reg % 32);
  else
    MB.relayed[reg / 32] &= ~(1u << (reg % 32));
  if (bit(MB.written, reg) && MB.hold[reg] == (int16_t) value)
    return;
  MB.hold[reg] = (int16_t) value;
  MB.written[reg / 32] |= 1u << (reg % 32);
  MB.changed[reg / 32] |= 1u << (reg % 32);
  MB.any_changed = true;
}

void mb_load(uint16_t reg, int16_t value) {
  if (reg < MB_REGS)
    stored(reg, (uint16_t) value, false);
}

static int finish(uint8_t *r, int n) {
  uint16_t c = mb_crc(r, n);
  r[n] = c & 0xFF;
  r[n + 1] = c >> 8;
  return n + 2;
}

static int exception(const uint8_t *q, uint8_t *r, uint8_t code) {
  r[0] = q[0];
  r[1] = q[1] | 0x80;
  r[2] = code;
  return finish(r, 3);
}

int mb_frame(const uint8_t *q, int n, uint8_t *r) {
  if (n < 4 || n > MB_MAX_FRAME || mb_crc(q, n - 2) != (q[n - 2] | q[n - 1] << 8)) {
    MB.errors++;
    return 0;
  }
  MB.frames++;
  bool bcast = q[0] == 0;
  if (!bcast && q[0] != MB.addr)
    return 0;
  if (!bcast)
    MB.ours++;
  uint8_t fc = q[1];
  uint16_t a = n >= 6 ? (uint16_t) (q[2] << 8 | q[3]) : 0;
  uint16_t cnt = n >= 8 ? (uint16_t) (q[4] << 8 | q[5]) : 0;
  int len;

  switch (fc) {
    case 0x01:  // read coils, the pending flags. The CX60 reads past the end too, those are 0
    case 0x02:  // read discrete inputs, always 0
      if (bcast || n != 8 || !cnt || cnt > 2000)
        return bcast ? 0 : exception(q, r, 3);
      r[0] = q[0];
      r[1] = fc;
      r[2] = (uint8_t) ((cnt + 7) / 8);
      memset(r + 3, 0, r[2]);
      if (fc == 0x01)
        for (uint16_t i = 0; i < cnt; i++)
          if (a + i < MB_REGS && bit(MB.pending, a + i))
            r[3 + i / 8] |= 1u << (i % 8);
      return finish(r, 3 + r[2]);

    case 0x03:  // read holding registers
    case 0x04:  // read input registers (discovery)
      if (bcast || n != 8 || !cnt || cnt > 125)
        return bcast ? 0 : exception(q, r, 3);
      if (fc == 0x03 ? a + cnt > MB_REGS : a + cnt > 5)
        return exception(q, r, 2);
      r[0] = q[0];
      r[1] = fc;
      r[2] = (uint8_t) (cnt * 2);
      for (uint16_t i = 0; i < cnt; i++) {
        uint16_t v = fc == 0x03 ? (uint16_t) MB.hold[a + i] : MB.input[a + i];
        r[3 + 2 * i] = v >> 8;
        r[4 + 2 * i] = v & 0xFF;
      }
      return finish(r, 3 + r[2]);

    case 0x05:  // write single coil
      if (n != 8)
        return 0;
      if (a < MB_REGS)
        set_pending(a, cnt == 0xFF00);
      if (bcast)
        return 0;
      memcpy(r, q, 6);
      return finish(r, 6);

    case 0x06:  // write single register
    case 0x65:  // Flexit's relay of a panel's change, write the register and clear its coil
      if (n != 8)
        return 0;
      if (a < MB_REGS && (fc != 0x65 || set_pending(a, false)))
        stored(a, cnt, fc == 0x65);
      if (bcast)
        return 0;
      memcpy(r, q, 6);
      return finish(r, 6);

    case 0x0F:  // write multiple coils
      if (n < 9 || n != 9 + q[6] || !cnt || q[6] != (cnt + 7) / 8)
        return bcast ? 0 : exception(q, r, 3);
      for (uint16_t i = 0; i < cnt; i++)
        if (a + i < MB_REGS)
          set_pending(a + i, (q[7 + i / 8] >> (i % 8)) & 1);
      if (bcast)
        return 0;
      memcpy(r, q, 6);
      return finish(r, 6);

    case 0x10:  // write multiple registers
      if (n < 9 || n != 9 + q[6] || q[6] != cnt * 2)
        return bcast ? 0 : exception(q, r, 3);
      for (uint16_t i = 0; i < cnt; i++)
        if (a + i < MB_REGS)
          stored(a + i, (uint16_t) (q[7 + 2 * i] << 8 | q[8 + 2 * i]), false);
      if (bcast)
        return 0;
      memcpy(r, q, 6);
      return finish(r, 6);

    case 0x64:  // enter the bootloader for a firmware update. The updaters send [addr 0x64 2 0 crc].
      if (bcast || n != 6 || q[2] != 2 || q[3] != 0)
        return 0;
      MB.enter_bootloader = true;
      len = n - 2;
      memcpy(r, q, len);
      return finish(r, len);

    default:
      return bcast ? 0 : exception(q, r, 1);
  }
}

// The CX60 sends its relay right behind another server's reply with no idle gap, so one receive can hold
// several frames. We find each one by trying the lengths its function code allows and checking the CRC.
static int frame_len(const uint8_t *q, int n) {
  if (n < 4)
    return 0;
  uint8_t fc = q[1];
  int c[4], k = 0;
  if (fc & 0x80)
    c[k++] = 5;
  if (fc <= 0x06 || fc == 0x65)
    c[k++] = 8;
  if (fc >= 0x01 && fc <= 0x04)
    c[k++] = 5 + q[2];
  if ((fc == 0x0F || fc == 0x10) && n >= 7) {
    c[k++] = 9 + q[6];
    c[k++] = 8;
  }
  for (int i = 0; i < k; i++)
    if (c[i] <= n && mb_crc(q, c[i] - 2) == (q[c[i] - 2] | q[c[i] - 1] << 8))
      return c[i];
  return 0;
}

int mb_chunk(const uint8_t *q, int n, uint8_t *r) {
  if (n >= 4 && mb_crc(q, n - 2) == (q[n - 2] | q[n - 1] << 8))
    return mb_frame(q, n, r);  // a single frame
  int reply = 0, off = 0;
  bool junk = false;
  while (n - off >= 4) {
    int len = frame_len(q + off, n - off);
    if (!len) {
      off++;
      junk = true;
      continue;
    }
    int x = mb_frame(q + off, len, r);  // a request to us is always the last, since the CX60 then waits for us
    if (x)
      reply = x;
    off += len;
  }
  if (junk || off < n)
    MB.errors++;
  return reply;
}

void mb_write(uint16_t reg, int16_t value) {
  if (reg >= MB_REGS)
    return;
  // The CX60 may have read the old value already. Its relay of that would undo this one, so it's skipped.
  if (bit(MB.pending, reg))
    MB.again[reg / 32] |= 1u << (reg % 32);
  MB.hold[reg] = value;
  set_pending(reg, true);
}

bool mb_take_changed(uint16_t *reg) {
  if (!MB.any_changed)
    return false;
  for (unsigned w = 0; w < sizeof MB.changed / sizeof *MB.changed; w++) {
    if (!MB.changed[w])
      continue;
    unsigned b = (unsigned) __builtin_ctz(MB.changed[w]);
    MB.changed[w] &= ~(1u << b);
    *reg = (uint16_t) (w * 32 + b);
    return true;
  }
  MB.any_changed = false;
  return false;
}
