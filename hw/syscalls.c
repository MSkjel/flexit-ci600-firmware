// No heap on purpose, LVGL has its own pool. A malloc() from libc gets NULL instead of growing into the stack.
// printf to a string never calls this.
#include <errno.h>

void *_sbrk(int incr) {
  (void) incr;
  errno = ENOMEM;
  return (void *) -1;
}
