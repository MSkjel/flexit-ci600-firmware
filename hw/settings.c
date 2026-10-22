// see settings.h
#include "settings.h"
#include "board.h"
#include "layout.h"
#include "mbserver.h"
#include "model.h"
#include "plink.h"  // plink_crc32
#include "rs485.h"
#include "unit.h"
#include <stddef.h>

#define MAGIC 0x32534943u   // "CIS2"
#define REG_KEY 0x8000u     // the key of a cached CU60 register is REG_KEY | register
#define QUIET_MS 5000u      // a save waits until nothing has changed for this long
#define LATER_MS 3600000u   // other changes are written at most this often
#define QUIET_WAIT_MS 250u  // for a quiet moment on the bus, the CU60's status broadcast comes every 110 ms
_Static_assert(FLASH_SETTINGS + FLASH_SETTINGS_PAGES * FLASH_PAGE == 0x08080000, "the pages end with the flash");

// Page header, then count records. seq goes up with every save, the highest is current.
typedef struct {
  uint32_t magic, seq;
  uint16_t count, pad;
  uint32_t crc;  // of the records
} head_t;

typedef struct {
  uint16_t key;
  int16_t value;
} record_t;

#define MAX_RECORDS ((FLASH_PAGE - sizeof(head_t)) / sizeof(record_t))
_Static_assert(V_ITEM_FIRST + MB_REGS <= MAX_RECORDS, "every saved value and register fits in a page");

static bool dirty, soon;              // unsaved changes, some of them to save soon
static uint32_t touched, written_ms;  // when the last change came, when flash was last written
static uint32_t seq, cur;             // the newest copy's seq and page address, cur 0 if there's none

// Page for the next save. Saves go round all the pages, and one that fails is skipped next time.
static uint32_t next = FLASH_SETTINGS;
static uint32_t after(uint32_t page) {
  return FLASH_SETTINGS + (page - FLASH_SETTINGS + FLASH_PAGE) % (FLASH_SETTINGS_PAGES * FLASH_PAGE);
}

static bool saved(val_t v) { return val_def(v).flags & VF_SAVED; }

// Next record to save, start with *i = 0. Values go before the cached registers because the bus address among
// them decides which per-panel registers are ours.
static bool next_record(int *i, record_t *out) {
  for (; *i < V_ITEM_FIRST; (*i)++)
    if (*i > V_NONE && saved((val_t) *i)) {
      *out = (record_t) {val_def((val_t) *i).key, (int16_t) val_get((val_t) *i)};
      (*i)++;
      return true;
    }
  for (int r; (r = *i - V_ITEM_FIRST) < MB_REGS; (*i)++)
    if (unit_cached((uint16_t) r)) {
      *out = (record_t) {(uint16_t) (REG_KEY | r), MB.hold[r]};
      (*i)++;
      return true;
    }
  return false;
}

static const head_t *valid(uint32_t page) {
  const head_t *h = (const head_t *) board_flash(page);
  if (h->magic != MAGIC || h->count > MAX_RECORDS)
    return NULL;
  return h->crc == plink_crc32(0, h + 1, h->count * sizeof(record_t)) ? h : NULL;
}

void settings_load(void) {
  cur = seq = 0;
  for (uint32_t i = 0; i < FLASH_SETTINGS_PAGES; i++) {  // the newest valid copy
    uint32_t page = FLASH_SETTINGS + i * FLASH_PAGE;
    const head_t *h = valid(page);
    if (h && (!cur || h->seq > seq)) {
      cur = page;
      seq = h->seq;
    }
  }
  next = cur ? after(cur) : FLASH_SETTINGS;
  if (!cur)
    return;
  const head_t *h = board_flash(cur);
  const record_t *it = (const record_t *) (h + 1);
  for (unsigned i = 0; i < h->count; i++) {
    if (it[i].key & REG_KEY)
      unit_cache_load(it[i].key & ~REG_KEY, it[i].value);
    else
      val_load(val_by_key(it[i].key), it[i].value);
  }
}

// The newest copy already has exactly these records.
static bool unchanged(void) {
  const head_t *h = cur ? valid(cur) : NULL;
  if (!h)
    return false;
  const record_t *it = (const record_t *) (h + 1);
  record_t now;
  unsigned n = 0;
  for (int i = 0; next_record(&i, &now); n++)
    if (n >= h->count || it[n].key != now.key || it[n].value != now.value)
      return false;
  return n == h->count;
}

// force saves even if the bus never gets quiet
static void save(bool force) {
  touched = board_ms();  // if the save fails, try again later rather than right away
  if (unchanged()) {
    dirty = soon = false;
    return;
  }
  if (!rs485_wait_quiet(QUIET_WAIT_MS) && !force)
    return;
  written_ms = board_ms();
  head_t h = {.magic = MAGIC, .seq = seq + 1, .pad = 0xFFFF, .crc = 0};
  uint32_t page = next, at = page + sizeof h;
  next = after(page);
  if (!board_flash_erase(page))
    return;
  record_t it;
  for (int i = 0; next_record(&i, &it); h.count++, at += sizeof it) {
    h.crc = plink_crc32(h.crc, &it, sizeof it);
    if (!board_flash_write(at, &it, sizeof it))
      return;
  }
  if (!board_flash_write(page, &h, sizeof h))
    return;
  seq = h.seq;
  cur = page;
  dirty = soon = false;
}

uint32_t settings_saves(void) { return seq; }

void settings_touch(bool quickly) {
  dirty = true;
  soon |= quickly;
  if (quickly)
    touched = board_ms();  // only quick ones restart the wait, the cached runtime counters change every second
}

// A page erase stalls the chip for 20-40 ms, the bus interrupt included, see rs485_wait_quiet().
void settings_poll(void) {
  uint32_t now = board_ms();
  if (dirty && now - touched > QUIET_MS && (soon || now - written_ms >= LATER_MS))
    save(false);
}

void settings_flush(void) {
  if (dirty)
    save(true);
}
