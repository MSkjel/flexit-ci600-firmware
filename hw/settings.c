// see settings.h
#include "settings.h"
#include "board.h"
#include "layout.h"
#include "mbserver.h"
#include "model.h"
#include "plat.h"
#include "plink.h"  // plink_crc32
#include "rs485.h"
#include "unit.h"
#include <stddef.h>

#define MAGIC (0x00534943u | ('0' + SETTINGS_FORMAT) << 24)  // "CIS3" for format 3
#define REG_KEY 0x8000u                                      // the key of a cached CX60 register is REG_KEY | register
#define QUIET_MS 5000u                                       // a save waits until nothing has changed for this long
#define LATER_MS 3600000u                                    // other changes are written at most this often
#define QUIET_WAIT_MS 250u  // for a quiet moment on the bus, the CX60's status broadcast comes every 110 ms
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
static uint32_t seq, cur_page;        // the newest copy's seq and page address, cur_page 0 if there's none
static bool fresh;                    // there was none when we started

// Page for the next save. Saves go round all the pages, and one that fails is skipped next time.
static uint32_t next_page = FLASH_SETTINGS;
static uint32_t page_after(uint32_t page) {
  return FLASH_SETTINGS + (page - FLASH_SETTINGS + FLASH_PAGE) % (FLASH_SETTINGS_PAGES * FLASH_PAGE);
}

// Calls f with each record to save until it returns false, then returns false. Values go before the cached
// registers, the bus address among them decides which per-panel registers are ours.
static bool for_each_record(bool (*f)(void *u, record_t it), void *u) {
  for (val_t v = val_next(V_NONE, VF_SAVED, 0); v != V_COUNT; v = val_next(v, VF_SAVED, 0))
    if (!f(u, (record_t) {val_def(v).key, (int16_t) val_get(v)}))
      return false;
  for (uint16_t r = 0; r < MB_REGS; r++)
    if (unit_cached(r) && !f(u, (record_t) {(uint16_t) (REG_KEY | r), unit_reg(r)}))
      return false;
  return true;
}

static const head_t *valid_head(uint32_t page) {
  const head_t *h = (const head_t *) board_flash(page);
  if (h->magic != MAGIC || h->count > MAX_RECORDS)
    return NULL;
  return h->crc == plink_crc32(0, h + 1, h->count * sizeof(record_t)) ? h : NULL;
}

void settings_load(void) {
  cur_page = seq = 0;
  for (uint32_t i = 0; i < FLASH_SETTINGS_PAGES; i++) {  // the newest valid copy
    uint32_t page = FLASH_SETTINGS + i * FLASH_PAGE;
    const head_t *h = valid_head(page);
    if (h && (!cur_page || h->seq > seq)) {
      cur_page = page;
      seq = h->seq;
    }
  }
  next_page = cur_page ? page_after(cur_page) : FLASH_SETTINGS;
  fresh = !cur_page;
  if (!cur_page)
    return;
  const head_t *h = board_flash(cur_page);
  const record_t *it = (const record_t *) (h + 1);
  for (unsigned i = 0; i < h->count; i++) {
    if (it[i].key & REG_KEY)
      mb_load(it[i].key & ~REG_KEY, it[i].value);
    else
      val_load(val_by_key(it[i].key), it[i].value);
  }
}

typedef struct {
  const head_t *h;
  unsigned n;  // records that matched so far
} compare_t;

static bool same(void *u, record_t now) {
  compare_t *c = u;
  const record_t *it = (const record_t *) (c->h + 1);
  if (c->n >= c->h->count || it[c->n].key != now.key || it[c->n].value != now.value)
    return false;
  c->n++;
  return true;
}

// The newest copy already has exactly these records.
static bool unchanged(void) {
  const head_t *h = cur_page ? valid_head(cur_page) : NULL;
  compare_t c = {h, 0};
  return h && for_each_record(same, &c) && c.n == h->count;
}

typedef struct {
  head_t h;
  uint32_t at;  // where the next record goes
} writer_t;

static bool put(void *u, record_t it) {
  writer_t *w = u;
  w->h.crc = plink_crc32(w->h.crc, &it, sizeof it);
  if (!board_flash_write(w->at, &it, sizeof it))
    return false;
  w->h.count++;
  w->at += sizeof it;
  return true;
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
  uint32_t page = next_page;
  writer_t w = {.h = {.magic = MAGIC, .seq = seq + 1, .pad = 0xFFFF, .crc = 0}, .at = page + sizeof(head_t)};
  next_page = page_after(page);
  if (!board_flash_erase(page) || !for_each_record(put, &w) || !board_flash_write(page, &w.h, sizeof w.h))
    return;
  seq = w.h.seq;
  cur_page = page;
  dirty = soon = false;
}

uint32_t settings_saves(void) { return seq; }

void plat_save(bool by_user) {
  dirty = true;
  soon |= by_user;
  if (by_user)
    touched = board_ms();  // only these restart the wait, the cached runtime counters change every second
}

void plat_save_now(void) {
  plat_save(true);
  settings_flush();
}

bool plat_fresh(void) { return fresh; }

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
