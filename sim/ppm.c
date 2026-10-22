// Writing PPM files and making directories. Doesn't need LVGL.
#include "sim.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

void sim_ppm(const char *path, int w, int h, const uint32_t *rgb) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    perror(path);
    exit(1);
  }
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i++) {
    uint8_t px[3] = {(uint8_t) (rgb[i] >> 16), (uint8_t) (rgb[i] >> 8), (uint8_t) rgb[i]};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}

void sim_mkdir(const char *dir) {
  char p[256];
  snprintf(p, sizeof p, "%s", dir);
  for (char *s = p + 1; *s; s++)
    if (*s == '/') {
      *s = 0;
      mkdir(p, 0755);
      *s = '/';
    }
  if (mkdir(p, 0755) && errno != EEXIST) {
    perror(dir);
    exit(1);
  }
}
