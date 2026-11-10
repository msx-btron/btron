/*
 * test_libstr_bounds.c — tkl_strscpy / tkl_strscat / tkl_strpad
 *
 * Includes src/kernel/libstr.c so the hosted binary tests the image copy,
 * not glibc strncpy. The freestanding half of this file is compiled again
 * with -ffreestanding and records assert() failures.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#if defined(BTRON_ASSERT_RT_PROBE)

#include <assert.h>

int btron_assert_probe(void)
{
    int i;

    btron_assert_count = 0;
    btron_assert_line = 0;

    i = 0;
    assert(i++ == 0);
    if (i != 1 || btron_assert_count != 0 || btron_assert_line != 0)
        return 1;

    assert(1 == 0);
    if (btron_assert_count != 1 || btron_assert_line == 0)
        return 2;

    assert(1 == 0);
    if (btron_assert_count != 2 || btron_assert_line == 0)
        return 3;

    return 0;
}

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/kernel/libstr.c"

extern int btron_assert_probe(void);

static int s_checks, s_fail;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_fail++;
        printf("  [FAIL] %s\n", what);
    } else {
        printf("  [OK]   %s\n", what);
    }
}

static int is_fill(const unsigned char *p, size_t n, unsigned char c)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i] != c)
            return 0;
    return 1;
}

int main(void)
{
    struct {
        unsigned char pre;
        char buf[8];
        unsigned char post;
    } slot;
    char exact[4];
    char *bare;
    size_t r;
    int i;

    printf("==========================================================\n");
    printf(" tkl_strscpy / tkl_strscat / tkl_strpad\n");
    printf("==========================================================\n");

    memset(&slot, 0xA5, sizeof slot);
    r = tkl_strscpy(slot.buf, "", 8);
    check("empty source writes one NUL and leaves the tail",
          r == 0 && slot.buf[0] == '\0' && slot.pre == 0xA5 && slot.post == 0xA5
          && is_fill((unsigned char *)slot.buf + 1, 7, 0xA5));

    memset(&slot, 0xA5, sizeof slot);
    r = tkl_strscpy(slot.buf, "hi", 0);
    check("n == 0 writes nothing",
          r == (size_t)-1 && slot.pre == 0xA5 && slot.post == 0xA5
          && is_fill((unsigned char *)slot.buf, 8, 0xA5));

    memset(&slot, 0xA5, sizeof slot);
    r = tkl_strscpy(slot.buf, "hi", 1);
    check("n == 1 writes only a NUL and reports truncation",
          r == (size_t)-1 && slot.buf[0] == '\0'
          && is_fill((unsigned char *)slot.buf + 1, 7, 0xA5)
          && slot.post == 0xA5);

    memset(&slot, 0xA5, sizeof slot);
    r = tkl_strscpy(slot.buf, "hello", 8);
    check("short copy does not zero the tail",
          r == 5 && strcmp(slot.buf, "hello") == 0
          && slot.buf[6] == (char)0xA5 && slot.buf[7] == (char)0xA5
          && slot.pre == 0xA5 && slot.post == 0xA5);

    memset(exact, 0xA5, sizeof exact);
    r = tkl_strscpy(exact, "abc", 4);
    check("exact fit of 3 chars in 4 bytes",
          r == 3 && strcmp(exact, "abc") == 0);

    memset(&slot, 0xA5, sizeof slot);
    r = tkl_strscpy(slot.buf, "hello!", 4);
    check("one over truncates, terminates, and spares the next byte",
          r == (size_t)-1 && strcmp(slot.buf, "hel") == 0
          && slot.buf[4] == (char)0xA5 && slot.post == 0xA5 && slot.pre == 0xA5);

    memset(&slot, 0xA5, sizeof slot);
    r = tkl_strscpy(slot.buf, "ab\0cdef", 8);
    check("embedded NUL stops the copy",
          r == 2 && strcmp(slot.buf, "ab") == 0 && slot.buf[3] == (char)0xA5);

    memset(&slot, 0xA5, sizeof slot);
    r = tkl_strscpy(NULL, "hi", 8);
    check("NULL destination does not store", r == (size_t)-1);
    r = tkl_strscpy(slot.buf, NULL, 8);
    check("NULL source does not store",
          r == (size_t)-1 && is_fill((unsigned char *)slot.buf, 8, 0xA5));

    /* 16 non-zero bytes and nothing after them. ASan faults a 17th read. */
    bare = (char *)malloc(16);
    check("miptex-sized source allocated", bare != NULL);
    if (bare) {
        struct { char buf[16]; unsigned char post; } wide;
        memset(bare, 'Q', 16);
        memset(&wide, 0xA5, sizeof wide);
        r = tkl_strscpy(wide.buf, bare, 16);
        check("16 non-zero bytes: 15 copied, NUL at the end, no 17th read",
              r == (size_t)-1 && memcmp(wide.buf, "QQQQQQQQQQQQQQQ", 15) == 0
              && wide.buf[15] == '\0' && wide.post == 0xA5);
        memset(exact, 0xA5, sizeof exact);
        r = tkl_strscpy(exact, bare, 4);
        check("unterminated source of capacity 4",
              r == (size_t)-1 && memcmp(exact, "QQQ", 3) == 0 && exact[3] == '\0');
        free(bare);
    }

    memset(&slot, 0xA5, sizeof slot);
    memcpy(slot.buf, "ab", 3);
    r = tkl_strscat(slot.buf, "cd", 8);
    check("strscat appends inside capacity",
          r == 4 && strcmp(slot.buf, "abcd") == 0 && slot.buf[5] == (char)0xA5);

    memset(&slot, 0xA5, sizeof slot);
    memcpy(slot.buf, "ab", 3);
    r = tkl_strscat(slot.buf, "cdef", 4);
    check("strscat truncation keeps a NUL inside capacity",
          r == (size_t)-1 && strcmp(slot.buf, "abc") == 0 && slot.buf[4] == (char)0xA5);

    memset(&slot, 0xA5, sizeof slot);
    r = tkl_strscat(slot.buf, "z", 8);
    check("strscat refuses a destination with no NUL in n",
          r == (size_t)-1 && is_fill((unsigned char *)slot.buf, 8, 0xA5));

    memset(&slot, 0xA5, sizeof slot);
    tkl_strpad(slot.buf, "hi", 4);
    check("strpad zero-fills the field and stops at n",
          memcmp(slot.buf, "hi\0\0", 4) == 0 && slot.buf[4] == (char)0xA5
          && slot.post == 0xA5 && slot.pre == 0xA5);

    memset(&slot, 0xA5, sizeof slot);
    tkl_strpad(slot.buf, "abcdefghij", 4);
    check("strpad of an over-long source writes n bytes and no extra NUL",
          memcmp(slot.buf, "abcd", 4) == 0 && slot.buf[4] == (char)0xA5);

    memset(&slot, 0xA5, sizeof slot);
    tkl_strpad(slot.buf, NULL, 4);
    check("strpad of NULL source clears the field",
          is_fill((unsigned char *)slot.buf, 4, 0) && slot.buf[4] == (char)0xA5);

    /* Today's strncpy. A change here is a silent break of unmigrated callers. */
    memset(&slot, 0xA5, sizeof slot);
    tkl_strncpy(slot.buf, "hi", 4);
    check("strncpy of a short source zero-fills through n",
          memcmp(slot.buf, "hi\0\0", 4) == 0 && slot.buf[4] == (char)0xA5);

    memset(&slot, 0xA5, sizeof slot);
    tkl_strncpy(slot.buf, "abcdef", 4);
    check("strncpy of a long source plants no NUL",
          memcmp(slot.buf, "abcd", 4) == 0 && slot.buf[4] == (char)0xA5);

    bare = (char *)malloc(4);
    check("strncpy bound source allocated", bare != NULL);
    if (bare) {
        memset(bare, 'Z', 4);
        memset(exact, 0xA5, sizeof exact);
        tkl_strncpy(exact, bare, 4);
        check("strncpy reads n bytes of an unterminated source and no more",
              memcmp(exact, "ZZZZ", 4) == 0);
        free(bare);
    }

    i = btron_assert_probe();
    check("freestanding assert records the line and does not abort", i == 0);

    printf("----------------------------------------------------------\n");
    printf(" %d checks, %d failed\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}

#endif
