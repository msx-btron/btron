#ifndef ORCH_CHK_H
#define ORCH_CHK_H

/* JPL rule 5: side-effect-free check. Failure records the line and
 * returns through the caller. It does not abort, allocate, or print. */
#ifdef __clang_analyzer__
void orch_assert_fail(int line) __attribute__((analyzer_noreturn));
#else
void orch_assert_fail(int line);
#endif

static inline void orch_check(int cond, int line)
{
    if (cond == 0) {
        orch_assert_fail(line);
    }
}

#endif
