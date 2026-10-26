// The tests' checks and their runner. A failed check prints its line and test, CHECK_EQ and CHECK_STR both values.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *name;
  void (*fn)(void);
} check_test_t;  // a table of them ends with {0}

extern const char *check_test;        // the test running, NULL outside one
extern int check_count, check_fails;  // the checks made and failed so far

// Each counts a check and returns whether it passed
bool check_ok(void);
bool check_failed(const char *file, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
bool check_eq(long long a, long long b, const char *ea, const char *eb, const char *file, int line);
bool check_str(const char *a, const char *b, const char *ea, const char *eb, const char *file, int line);

// Runs the tables' tests, the list ends with NULL, and prints one line for them all. An argument picks the test it
// names, or else those with it in their name. -l lists them. Returns the exit status.
int check_main(const char *what, const check_test_t *const *tables, int argc, char **argv);

#define CHECK(c) ((c) ? check_ok() : check_failed(__FILE__, __LINE__, "%s", #c))
// printf() arguments, only evaluated when it fails
#define CHECK_MSG(c, ...) ((c) ? check_ok() : check_failed(__FILE__, __LINE__, __VA_ARGS__))
// Integers up to 32 bits, as long long: a pointer doesn't compile, a float would be cut
#define CHECK_EQ(a, b) check_eq(a, b, #a, #b, __FILE__, __LINE__)
#define CHECK_STR(a, b) check_str(a, b, #a, #b, __FILE__, __LINE__)  // either may be NULL

#ifdef __cplusplus
}
#endif
