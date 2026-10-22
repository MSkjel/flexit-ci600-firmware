// Checks the translations for missing texts, printf conversions that differ from the English, glyphs the
// fonts don't have and help texts too long for the help card.
#include "ui.h"
#include <stdio.h>
#include <string.h>

static const char *const NAMES[S_COUNT] = {
#define X(id, en) [S_##id] = "S_" #id,
    STRINGS(X)
#undef X
};
static const lv_font_t *const FONTS[] = {&font_f12, &font_f14, &font_f16, &font_f20, &font_f28};

// The printf conversions in s, in order. A translation that asks for other arguments would crash.
static void conversions(const char *s, char *out, size_t n) {
  size_t k = 0;
  for (; *s; s++) {
    if (*s != '%')
      continue;
    const char *c = s++;
    if (*s == '%')
      continue;
    s += strspn(s, "-+ #0123456789.*hlz");
    if (!*s)
      break;
    size_t len = (size_t) (s - c + 1);
    if (k + len < n) {
      memcpy(out + k, c, len);
      k += len;
    }
  }
  out[k] = 0;
}

static int glyphs(const char *lang, str_t s, const char *t) {
  for (uint32_t i = 0, c; (c = _lv_txt_encoded_next(t, &i)) != 0;) {
    if (c == '\n')
      continue;
    for (unsigned f = 0; f < sizeof FONTS / sizeof *FONTS; f++) {
      lv_font_glyph_dsc_t g;
      if (!lv_font_get_glyph_dsc(FONTS[f], &g, c, 0)) {
        printf("%s %s: U+%04X isn't in the fonts (tools/mkfonts.py)\n", lang, NAMES[s], (unsigned) c);
        return 1;
      }
    }
  }
  return 0;
}

int main(void) {
  int bad = 0;
  for (int lang = 0; lang < N_LANGS; lang++)
    for (int s = S_NONE + 1; s < S_COUNT; s++) {
      const char *t = lang_text(lang, s), *name = lang_name(lang);
      if (!t) {
        printf("%s %s: missing\n", name, NAMES[s]);
        bad++;
        continue;
      }
      char want[64], got[64];
      conversions(lang_text(0, s), want, sizeof want);
      conversions(t, got, sizeof got);
      if (strcmp(want, got)) {
        printf("%s %s: \"%s\" where the English has \"%s\"\n", name, NAMES[s], got, want);
        bad++;
      }
      bad += glyphs(name, s, t);
      lv_point_t size;
      lv_txt_get_size(&size, t, &font_f14, 0, 0, HELP_W, LV_TEXT_FLAG_NONE);
      if (strstr(NAMES[s], "_HELP") && size.y > HELP_LINES * font_f14.line_height) {
        printf("%s %s: %d lines on the help card, %d fit\n", name, NAMES[s], size.y / font_f14.line_height, HELP_LINES);
        bad++;
      }
    }
  printf("%d languages, %d texts each: %s\n", N_LANGS, S_COUNT - 1, bad ? "FAILED" : "ok");
  return bad != 0;
}
