// Replays a capture from the ESP's TCP bridge through hw/mbserver.c and hw/unit.c, compares our replies with
// the ESP's and takes screenshots with the data we got.
//   ./replay CAPTURE.txt OUTDIR [glued]      lines: "R|T seconds hexbytes"
#include "board.h"
#include "clock.h"
#include "mbserver.h"
#include "room.h"
#include "settings.h"
#include "sim.h"
#include "ui.h"
#include "unit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

model_t M;
extern uint32_t stub_ms, stub_rtc;  // board_stub.c

// hw/settings.c isn't linked in
void settings_load(void) {}
void settings_touch(bool by_user) { (void) by_user; }
void settings_poll(void) {}
void settings_flush(void) {}

static int unhex(const char *h, uint8_t *b) {
  int n = 0;
  while (h[0] && h[1] && h[0] != '\n') {
    unsigned v;
    sscanf(h, "%2x", &v);
    b[n++] = (uint8_t) v;
    h += 2;
  }
  return n;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: replay CAPTURE.txt OUTDIR [glued]\n");
    return 2;
  }
  const char *outdir = argv[2];
  sim_mkdir(outdir);
  // with glued, everything between two ESP replies goes to mb_chunk() at once, like the idle interrupt gets it
  bool glued = argc > 3 && !strcmp(argv[3], "glued");
  static uint8_t chunk[8192];
  int chunk_n = 0;
  memset(&M, 0, sizeof M);
  panel_defaults();
  M.bus_addr = 1;  // the ESP's address, so our replies can be compared with its
  M.features = FBIT(FEAT_ESP) | FBIT(FEAT_EC_FANS);
  M.filter_days = 180;
  unit_init();
  sim_display_init();
  ui_init();

  FILE *f = fopen(argv[1], "r");
  if (!f) {
    perror(argv[1]);
    return 1;
  }
  char line[1200], dir;
  double t;
  static char hex[1100];
  uint8_t q[MB_MAX_FRAME], ours[MB_MAX_FRAME], theirs[MB_MAX_FRAME];
  int ours_n = 0, same = 0, differ = 0, missing = 0, extra = 0;
  unsigned long frames = 0;
  while (fgets(line, sizeof line, f)) {
    if (sscanf(line, "%c %lf %1099s", &dir, &t, hex) != 3)
      continue;
    stub_ms = (uint32_t) (t * 1000);
    if (dir == 'R') {
      if (ours_n) {
        extra++;
        ours_n = 0;
      }  // we replied where the ESP didn't
      int n = unhex(hex, q);
      frames++;
      if (glued) {
        if (chunk_n + n <= (int) sizeof chunk) {
          memcpy(chunk + chunk_n, q, n);
          chunk_n += n;
        }
        continue;
      }
      ours_n = mb_frame(q, n, ours);
    } else {
      if (glued) {
        ours_n = mb_chunk(chunk, chunk_n, ours);
        chunk_n = 0;
      }
      int n = unhex(hex, theirs);
      if (!ours_n)
        missing++;
      else if (ours_n == n && !memcmp(ours, theirs, n))
        same++;
      else {
        differ++;
        if (differ <= 3) {
          printf("  differ: ours ");
          for (int i = 0; i < ours_n; i++)
            printf("%02x", ours[i]);
          printf("\n          ESP  %s\n", hex);
        }
      }
      ours_n = 0;
    }
    unit_poll();
  }
  fclose(f);
  printf("%lu frames, %lu with a good CRC; replies: %d same as the ESP, %d different, %d it sent and we didn't, %d we "
         "sent and it didn't\n",
         frames, (unsigned long) MB.frames, same, differ, missing, extra);

  stub_rtc = 1792419840;  // the capture has no time in it, so use 2026-10-19 14:24
  clock_from_unix(stub_rtc);
  unit_second();
  room_second();
  char a[16], b[16], c[16], d[16];
  printf("model: outdoor %s, supply %s, extract %s, water %s\n", fmt_temp(a, M.t_outdoor, true),
         fmt_temp(b, M.t_supply, true), fmt_temp(c, M.t_extract, true), fmt_temp(d, M.t_water, true));
  printf("       mode %d, supply fan %d%%, rotor %d%%, heating %d%%, cooling %d%%, boost %d min\n", M.mode_now,
         M.fan_sup, M.rotor_pct, M.heat_pct, M.cool_pct, M.boost_left);
  printf("       filter %u h, total %lu h, CU60 %d.%02d, alarms %d, log %d, EC fans %d, water heater %d\n",
         M.filter_hours, (unsigned long) M.h_total, M.cu60_version >> 8, M.cu60_version & 0xFF, M.n_alarms, M.n_history,
         (int) HAS(FEAT_EC_FANS), (int) HAS(FEAT_WATER_HEATER));
  printf("       runtime stop %lu h, min %lu, normal %lu, max %lu, rotor %lu, heater %lu\n", (unsigned long) M.h_stop,
         (unsigned long) M.h_min, (unsigned long) M.h_normal, (unsigned long) M.h_max, (unsigned long) M.h_rotor,
         (unsigned long) M.h_heater);
  for (int i = 0; i < M.n_alarms; i++)
    printf("       alarm %d (%s) since %02d.%02d %02d:%02d\n", M.alarms[i].code, alarm_text(M.alarms[i].code),
           M.alarms[i].dd, M.alarms[i].mo, M.alarms[i].hh, M.alarms[i].mi);
  for (int i = 0; i < M.n_history; i++)
    printf("       log   %d (%s) %02d.%02d %02d:%02d%s\n", M.history[i].code, alarm_text(M.history[i].code),
           M.history[i].dd, M.history[i].mo, M.history[i].hh, M.history[i].mi,
           M.history[i].active ? " still active" : "");

  ui_refresh();
  sim_shot(outdir, "real_dashboard");
  ui_replace(&SCR_HOME, VIEW_FLOW);
  sim_shot(outdir, "real_airflow");
  ui_push(&SCR_ALARMS, ALARMS_HISTORY);
  sim_shot(outdir, "real_alarm_log");
  return differ || missing || extra || !same;
}
