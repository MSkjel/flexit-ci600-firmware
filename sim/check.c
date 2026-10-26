// The tests' checks and their runner, see check.h
#include "check.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *check_test;
int check_count, check_fails;

bool check_ok(void) {
  check_count++;
  return true;
}

bool check_failed(const char *file, int line, const char *fmt, ...) {
  check_count++;
  check_fails++;
  printf("  FAIL %s:%d: ", file, line);
  if (check_test)
    printf("%s: ", check_test);
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
  fflush(stdout);  // before a crash further on
  return false;
}

// v, and in hex too when it's big enough to be bits
static const char *num(char *b, size_t n, long long v) {
  snprintf(b, n, v > 0xFFFF ? "%lld (0x%llx)" : "%lld", v, (unsigned long long) v);
  return b;
}

bool check_eq(long long a, long long b, const char *ea, const char *eb, const char *file, int line) {
  char x[48], y[48];
  return a == b ? check_ok()
                : check_failed(file, line, "%s == %s: %s != %s", ea, eb, num(x, sizeof x, a), num(y, sizeof y, b));
}

bool check_str(const char *a, const char *b, const char *ea, const char *eb, const char *file, int line) {
  if (a && b ? !strcmp(a, b) : a == b)
    return check_ok();
  return check_failed(file, line, "%s == %s: \"%s\" != \"%s\"", ea, eb, a ? a : "(null)", b ? b : "(null)");
}

static bool named(const check_test_t *const *tables, const char *name) {
  for (; *tables; tables++)
    for (const check_test_t *t = *tables; t->name; t++)
      if (!strcmp(t->name, name))
        return true;
  return false;
}

// Whether the arguments pick test name: by its whole name, or by part of it if no test has that name. used[i] is
// set when argv[i] picks it.
static bool picked(const check_test_t *const *tables, const char *name, int argc, char **argv, bool *used) {
  bool any = argc < 2;
  for (int i = 1; i < argc; i++)
    if (named(tables, argv[i]) ? !strcmp(name, argv[i]) : strstr(name, argv[i]) != NULL)
      any = used[i] = true;
  return any;
}

int check_main(const char *what, const check_test_t *const *tables, int argc, char **argv) {
  bool list = argc > 1 && !strcmp(argv[1], "-l"), verbose = argc > 1 && !strcmp(argv[1], "-v");
  if (list || verbose) {
    argv++;
    argc--;
  }
  bool used[argc];
  memset(used, 0, sizeof used);
  int all = 0, run = 0;
  for (const check_test_t *const *g = tables; *g; g++)
    for (const check_test_t *t = *g; t->name; t++, all++)
      if (picked(tables, t->name, argc, argv, used) && list)
        printf("%s\n", t->name);
  for (int i = 1; i < argc; i++)
    if (!used[i]) {
      printf("%s: no test called %s, -l lists them\n", what, argv[i]);
      return 2;
    }
  if (list)
    return 0;
  char failed[160] = "";
  for (const check_test_t *const *g = tables; *g; g++)
    for (const check_test_t *t = *g; t->name; t++) {
      if (!picked(tables, t->name, argc, argv, used))
        continue;
      int count = check_count, fails = check_fails;
      check_test = t->name;
      t->fn();
      check_test = NULL;
      run++;
      if (verbose)
        printf("%s: %d checks\n", t->name, check_count - count);
      if (check_fails > fails && strlen(failed) + strlen(t->name) + 4 < sizeof failed)
        strcat(strcat(failed, *failed ? ", " : " in "), t->name);
    }
  printf("%s: ", what);
  if (run < all)
    printf("%d of ", run);
  printf("%d tests, %d checks: ", all, check_count);
  if (check_fails)
    printf("%d FAILED%s\n", check_fails, failed);
  else
    printf("ok\n");
  return check_fails != 0;
}
