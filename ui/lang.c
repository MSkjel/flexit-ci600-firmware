// Text lookup in the panel's language. English is in lang.h, the others in lang_*.c.
#include "ui_int.h"

static const char *const EN[S_COUNT] = {[S_NONE] = "",
#define X(id, en) [S_##id] = en,
                                        STRINGS(X)
#undef X
};
extern const char *const LANG_NO[S_COUNT];

static const struct {
  const char *name;  // in its own language
  const char *const *text;
  bool pct_space;  // a space before %, the Norwegian standard
} LANGS[] = {{"English", EN, false}, {"Norsk", LANG_NO, true}};
_Static_assert(LEN(LANGS) == N_LANGS, "N_LANGS (proto/plink_values.h) counts the languages");

const char *tr(str_t s) {
  const char *t = LANGS[M.language].text[s];
  return t ? t : EN[s];  // English if missing, make check catches that
}

const char *lang_name(int lang) { return LANGS[lang].name; }

const char *lang_text(int lang, str_t s) { return LANGS[lang].text[s]; }

bool lang_pct_space(int lang) { return LANGS[lang].pct_space; }
