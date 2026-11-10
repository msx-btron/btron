#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <assert.h>
#else
#ifndef _GL_ASSERT_H_
#define _GL_ASSERT_H_

/* Freestanding (bare-metal / UEFI) builds have no libc abort. A failed check
 * records a sticky count and the source line, the way orch_check does, and
 * returns. It does not print, allocate, or stop the caller. The counters live
 * in src/kernel/libstr.c, which every freestanding image links. */
extern int btron_assert_count;
extern int btron_assert_line;

#define assert(expr)                                                          \
    do {                                                                      \
        if (!(expr)) {                                                        \
            btron_assert_count++;                                             \
            btron_assert_line = __LINE__;                                     \
        }                                                                     \
    } while (0)

#endif /* _GL_ASSERT_H_ */
#endif /* HOSTED */
