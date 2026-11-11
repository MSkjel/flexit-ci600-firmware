// Text lookup in the panel's language. English is in lang.h, the others in lang_*.c.
#include "ui_int.h"

LANGUAGE(LANG_EN, STRINGS);
extern const lang_t LANG_NO;

static const struct {
  const char *name;  // in its own language
  const lang_t *texts;
  bool pct_space;  // a space before %, the Norwegian standard
} LANGS[] = {{"English", &LANG_EN, false}, {"Norsk", &LANG_NO, true}};
_Static_assert(LEN(LANGS) == N_LANGS, "N_LANGS (proto/plink_values.h) counts the languages");

const char *lang_name(int lang) { return LANGS[lang].name; }

const char *lang_text(int lang, str_t s) {
  const lang_t *l = LANGS[lang].texts;
  return l->at[s] || s == S_NONE ? l->block + l->at[s] : NULL;
}

const char *tr(str_t s) {
  const char *t = lang_text(M.language, s);
  return t ? t : LANG_EN.block + LANG_EN.at[s];  // English if missing, make check catches that
}

bool lang_pct_space(int lang) { return LANGS[lang].pct_space; }
