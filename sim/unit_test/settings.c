// hw/settings.c: the settings and the runtime counters in flash
#include "unit_test.h"

// ---------------------------------------------------------------- settings in the RAM flash, with power cuts
static void test_settings(void) {
  model_defaults();
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  settings_load();
  M.brightness = 42;
  plat_save(true);
  settings_flush();
  model_defaults();
  settings_load();
  CHECK_EQ(M.brightness, 42);
  // power fails while saving, the previous save is still there
  M.brightness = 55;
  plat_save(true);
  stub_flash_writes_left = 3;
  settings_flush();
  stub_flash_writes_left = -1;
  model_defaults();
  settings_load();
  CHECK_EQ(M.brightness, 42);
  // the next save works, and a save with no changes erases nothing
  M.brightness = 55;
  plat_save(true);
  settings_flush();
  int erases = stub_flash_erases;
  plat_save(true);
  settings_flush();
  CHECK_EQ(stub_flash_erases, erases);
  model_defaults();
  settings_load();
  CHECK_EQ(M.brightness, 55);
  // A copy that fails its check is skipped. Here the newest, the second save on the second page, is damaged
  // so the one before it loads.
  stub_flash[FLASH_PAGE + 20] ^= 1;
  model_defaults();
  settings_load();
  CHECK_EQ(M.brightness, 42);

  // saves take turns on all the pages, and the newest is loaded wherever it is
  for (int i = 0; i < 2 * FLASH_SETTINGS_PAGES; i++) {
    M.brightness = 10 + i;
    plat_save(true);
    settings_flush();
  }
  uint32_t magic = *(const uint32_t *) stub_flash;
  for (int i = 0; i < FLASH_SETTINGS_PAGES; i++) {  // each holds a copy
    CHECK(magic != 0xFFFFFFFFu);
    CHECK_EQ(*(const uint32_t *) (stub_flash + i * FLASH_PAGE), magic);
  }
  model_defaults();
  settings_load();
  CHECK_EQ(M.brightness, 10 + 2 * FLASH_SETTINGS_PAGES - 1);

  // a change on the keys is saved 5 s after the last one, others at most once an hour
  erases = stub_flash_erases;
  M.brightness = 70;
  plat_save(false);
  stub_ms += 6000;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases);  // written less than an hour ago
  stub_ms += 3600 * 1000;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases + 1);
  M.brightness = 71;
  plat_save(false);
  stub_ms += 6000;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases + 1);
  M.brightness = 72;
  plat_save(true);  // on the keys, saves this one and the one waiting
  stub_ms += 1000;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases + 1);  // not before 5 s
  stub_ms += 5000;
  stub_bus_quiet = false;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases + 1);  // nor while the bus is busy, an erase stalls the bus interrupt
  stub_bus_quiet = true;
  stub_ms += 6000;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases + 2);
  model_defaults();
  settings_load();
  CHECK_EQ(M.brightness, 72);

  // a page that fails a save is skipped next time
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  settings_load();
  stub_flash_bad = FLASH_SETTINGS;
  M.brightness = 61;
  plat_save(true);
  settings_flush();
  settings_flush();
  stub_flash_bad = 0;
  model_defaults();
  settings_load();
  CHECK_EQ(M.brightness, 61);
}

// ---------------------------------------------------------------- the runtime counters, kept in flash
static void test_runtime(void) {
  model_defaults();
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  unit_init();
  settings_load();
  uint16_t rt[20] = {0};
  rt[0] = 1;   // stop: 65536 s, 18 h
  rt[16] = 5;  // total: 91 h
  block(0x14C, rt, 20);
  unit_poll();
  CHECK_EQ(M.h_stop, 18);
  CHECK_EQ(M.h_total, 91);
  // the low words come every second, and mustn't hold up a save made on the keys
  int erases = stub_flash_erases;
  M.brightness = 33;
  plat_save(true);
  for (int s = 0; s < 7; s++) {
    rt[17]++;
    block(0x14C, rt, 20);
    unit_poll();
    stub_ms += 1000;
    settings_poll();
  }
  CHECK_EQ(stub_flash_erases, erases + 1);
  // after a restart the counters are back from flash, the CX60 only sends the high words when it starts
  model_defaults();
  unit_init();
  settings_load();
  unit_poll();
  CHECK_EQ(M.h_stop, 18);
  CHECK_EQ(M.h_total, 91);
  CHECK_EQ(M.brightness, 33);
  // the total's high word changed while we were off
  cx60(0x15D, 9);
  cx60(0x0CB, 109);
  unit_poll();
  CHECK_EQ(M.h_total, 109);
  // saved soon: a high word and a relayed week program change, not a relayed fan change
  erases = stub_flash_erases;
  cx60(0x15C, 6);
  unit_poll();
  stub_ms += 6000;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases + 1);
  relay(0x118, 1);
  unit_poll();
  stub_ms += 6000;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases + 2);
  relay(0x002, 50);
  unit_poll();
  stub_ms += 6000;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases + 2);
  M.features &= ~FBIT(FEAT_ESP);  // no ESP, so the relay is another panel's
  relay(0x002, 55);
  unit_poll();
  stub_ms += 6000;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases + 3);
}

// The de-icing settings sit among the counters, 0x158..0x15B, but they're saved like the other settings
static void test_deice_saved(void) {
  model_defaults();
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  unit_init();
  settings_load();
  plat_save(true);
  settings_flush();  // so the hourly save isn't due
  // with an ESP a relayed change waits for the hourly save, the ESP sends it again when we restart
  int erases = stub_flash_erases;
  relay(0x158, 1);
  unit_poll();
  stub_ms += 6000;
  settings_poll();
  CHECK_EQ(stub_flash_erases, erases);
  // without one it's another panel's, which nobody sends again
  M.features &= ~FBIT(FEAT_ESP);
  const int16_t v[4] = {0, -200, -150, 60};
  for (int i = 0; i < 4; i++) {
    relay((uint16_t) (0x158 + i), (uint16_t) v[i]);
    unit_poll();
    stub_ms += 6000;
    settings_poll();
    CHECK_EQ(stub_flash_erases, erases + 1 + i);
  }
  // and they're back after a restart
  model_defaults();
  unit_init();
  settings_load();
  unit_poll();
  CHECK(!M.adv.deice);
  CHECK_EQ(M.adv.deice_temp, -200);
  CHECK_EQ(M.adv.less_air_temp, -150);
  CHECK_EQ(M.adv.deice_rotor, 60);
}

const check_test_t SETTINGS_TESTS[] = {
    {"settings", test_settings},
    {"runtime", test_runtime},
    {"deice_saved", test_deice_saved},
    {0},
};
