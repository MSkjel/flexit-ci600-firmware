// Modbus server, see mbserver.h
#include "mbserver.h"
#include "pbus.h"
#include "plink_values.h"
#include <string.h>

mb_t MB;

// Function codes, Modbus's and then Flexit's own
enum {
  FC_READ_COILS = 0x01,
  FC_READ_DISCRETE = 0x02,
  FC_READ_HOLDING = 0x03,
  FC_READ_INPUT = 0x04,
  FC_WRITE_COIL = 0x05,
  FC_WRITE_REG = 0x06,
  FC_WRITE_COILS = 0x0F,
  FC_WRITE_REGS = 0x10,
  FC_BOOTLOADER = 0x64,  // enter the bootloader, from the updaters
  FC_RELAY = 0x65,       // the CX60's relay of a panel's change
};
// Exception codes
enum { EX_FUNCTION = 1, EX_ADDRESS = 2, EX_VALUE = 3 };

void mb_init(uint8_t addr, uint16_t version, uint8_t patch) {
  memset(&MB, 0, sizeof MB);
  MB.addr = addr;
  MB.input[0] = FW_TYPE;
  MB.input[2] = patch;
  MB.input[3] = version;
}

static bool bit(const uint32_t *map, uint16_t reg) { return map[reg / 32] >> (reg % 32) & 1; }

static void put_bit(uint32_t *map, uint16_t reg, bool on) {
  if (on)
    map[reg / 32] |= 1u << (reg % 32);
  else
    map[reg / 32] &= ~(1u << (reg % 32));
}

// Returns false if the coil stays set: the relay was of an older value of ours and the CX60 has to read the new one.
static bool set_pending(uint16_t reg, bool on) {
  if (!on && bit(MB.again, reg)) {
    put_bit(MB.again, reg, false);
    return false;
  }
  put_bit(MB.pending, reg, on);
  return true;
}

bool mb_written(uint16_t reg) { return reg < MB_REGS && bit(MB.written, reg); }
bool mb_relayed(uint16_t reg) { return reg < MB_REGS && bit(MB.relayed, reg); }
bool mb_pending(uint16_t reg) { return reg < MB_REGS && bit(MB.pending, reg); }
bool mb_bootloader_requested(void) { return MB.enter_bootloader; }

// Only flags the main loop if it's the first value or a different one.
static void stored(uint16_t reg, uint16_t value, bool relayed) {
  put_bit(MB.relayed, reg, relayed);
  if (bit(MB.written, reg) && MB.hold[reg] == (int16_t) value)
    return;
  MB.hold[reg] = (int16_t) value;
  put_bit(MB.written, reg, true);
  put_bit(MB.changed, reg, true);
  MB.any_changed = true;
}

void mb_load(uint16_t reg, int16_t value) {
  if (reg < MB_REGS)
    stored(reg, (uint16_t) value, false);
}

// An exception reply, none to a broadcast
static int exception(const uint8_t *q, uint8_t *r, uint8_t code) {
  if (!q[0])
    return 0;
  r[0] = q[0];
  r[1] = q[1] | 0x80;
  r[2] = code;
  return pbus_put_crc(r, 3);
}

int mb_frame(const uint8_t *q, int n, uint8_t *r) {
  if (n > MB_MAX_FRAME || !pbus_crc_ok(q, n)) {
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
  uint16_t a = n >= 6 ? (uint16_t) (q[2] << 8 | q[3]) : 0;     // the first register or coil
  uint16_t word = n >= 8 ? (uint16_t) (q[4] << 8 | q[5]) : 0;  // a single write's value
  uint16_t cnt = word;                                         // or how many the others are for

  switch (fc) {
    case FC_READ_COILS:     // the pending flags. The CX60 reads past the end too, those are 0
    case FC_READ_DISCRETE:  // always 0
      if (bcast || n != 8 || !cnt || cnt > 2000)
        return exception(q, r, EX_VALUE);
      r[0] = q[0];
      r[1] = fc;
      r[2] = (uint8_t) ((cnt + 7) / 8);
      memset(r + 3, 0, r[2]);
      if (fc == FC_READ_COILS)
        for (uint16_t i = 0; i < cnt; i++)
          if (a + i < MB_REGS && bit(MB.pending, a + i))
            r[3 + i / 8] |= 1u << (i % 8);
      return pbus_put_crc(r, 3 + r[2]);

    case FC_READ_HOLDING:
    case FC_READ_INPUT:  // discovery
      if (bcast || n != 8 || !cnt || cnt > 125)
        return exception(q, r, EX_VALUE);
      if (a + cnt > (fc == FC_READ_HOLDING ? MB_REGS : MB_INPUTS))
        return exception(q, r, EX_ADDRESS);
      r[0] = q[0];
      r[1] = fc;
      r[2] = (uint8_t) (cnt * 2);
      for (uint16_t i = 0; i < cnt; i++) {
        uint16_t v = fc == FC_READ_HOLDING ? (uint16_t) MB.hold[a + i] : MB.input[a + i];
        r[3 + 2 * i] = v >> 8;
        r[4 + 2 * i] = v & 0xFF;
      }
      return pbus_put_crc(r, 3 + r[2]);

    case FC_WRITE_COIL:
      if (n != 8)
        return 0;
      if (a < MB_REGS)
        set_pending(a, word == 0xFF00);
      break;

    case FC_WRITE_REG:
    case FC_RELAY:  // write the register and clear its coil
      if (n != 8)
        return 0;
      if (a < MB_REGS && (fc != FC_RELAY || set_pending(a, false)))
        stored(a, word, fc == FC_RELAY);
      break;

    case FC_WRITE_COILS:
      if (n < 9 || n != 9 + q[6] || !cnt || q[6] != (cnt + 7) / 8)
        return exception(q, r, EX_VALUE);
      for (uint16_t i = 0; i < cnt; i++)
        if (a + i < MB_REGS)
          set_pending(a + i, (q[7 + i / 8] >> (i % 8)) & 1);
      break;

    case FC_WRITE_REGS:
      if (n < 9 || n != 9 + q[6] || q[6] != cnt * 2)
        return exception(q, r, EX_VALUE);
      for (uint16_t i = 0; i < cnt; i++)
        if (a + i < MB_REGS)
          stored(a + i, (uint16_t) (q[7 + 2 * i] << 8 | q[8 + 2 * i]), false);
      break;

    case FC_BOOTLOADER:  // for a firmware update. The updaters send [addr 0x64 2 0 crc].
      if (bcast || n != 6 || q[2] != 2 || q[3] != 0)
        return 0;
      MB.enter_bootloader = true;
      memcpy(r, q, 4);
      return pbus_put_crc(r, 4);

    default:
      return exception(q, r, EX_FUNCTION);
  }
  if (bcast)  // the writes echo the request's first 6 bytes
    return 0;
  memcpy(r, q, 6);
  return pbus_put_crc(r, 6);
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
  if (fc <= FC_WRITE_REG || fc == FC_RELAY)
    c[k++] = 8;
  if (fc >= FC_READ_COILS && fc <= FC_READ_INPUT)
    c[k++] = 5 + q[2];
  if ((fc == FC_WRITE_COILS || fc == FC_WRITE_REGS) && n >= 7) {
    c[k++] = 9 + q[6];
    c[k++] = 8;
  }
  for (int i = 0; i < k; i++)
    if (c[i] <= n && pbus_crc_ok(q, c[i]))
      return c[i];
  return 0;
}

int mb_chunk(const uint8_t *q, int n, uint8_t *r) {
  if (pbus_crc_ok(q, n))
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
    put_bit(MB.again, reg, true);
  MB.hold[reg] = value;
  set_pending(reg, true);
}

bool mb_take_changed(uint16_t *reg) {
  if (!MB.any_changed)
    return false;
  for (unsigned w = 0; w < MB_WORDS; w++) {
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
