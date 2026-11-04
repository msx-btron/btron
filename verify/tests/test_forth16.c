/*
 * B-System BTRON3 / T-FW — test_forth16.c
 * Automated Verification Test for 16-Bit Cell Native Profile
 */

#ifndef FORTH_16BIT
#define FORTH_16BIT 1
#endif

#include <btron/forth_vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_total++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
        assert(cond); \
    } \
} while(0)

int main(void) {
    printf("==========================================================\n");
    printf(" Running 16-Bit ANS Forth Native Profile Test Suite...\n");
    printf("==========================================================\n");

    /* 1. Cell Width and Type Guarantees */
    printf("[1] Verifying 16-bit Cell Size & Limits...\n");
    TEST_ASSERT(sizeof(forth_cell_t) == 2, "forth_cell_t is strictly 2 bytes");
    TEST_ASSERT(sizeof(forth_ucell_t) == 2, "forth_ucell_t is strictly 2 bytes");
    TEST_ASSERT(FORTH_CELL_SIZE == 2, "FORTH_CELL_SIZE macro equals 2");

    ForthVM vm;
    forth_vm_init(&vm);

    /* 2. 16-bit Integer Arithmetic & Wrap-around */
    printf("[2] Verifying 16-bit Signed & Two's Complement Rollover...\n");
    forth_eval(&vm, "32767 1 +");
    TEST_ASSERT(vm.dstack[0] == -32768, "32767 + 1 overflows to -32768 in 16-bit two's complement");

    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "-32768 1 -");
    TEST_ASSERT(vm.dstack[0] == 32767, "-32768 - 1 underflows to 32767 in 16-bit two's complement");

    /* 3. 16-bit Unsigned Comparisons */
    printf("[3] Verifying 16-bit Unsigned vs Signed Comparisons...\n");
    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "-1 0 <");
    TEST_ASSERT(vm.dstack[0] == -1, "-1 < 0 is TRUE (-1) in signed 16-bit");

    forth_eval(&vm, "-1 0 U<");
    TEST_ASSERT(vm.dstack[1] == 0, "-1 (0xFFFF) U< 0 is FALSE (0) in unsigned 16-bit");

    forth_eval(&vm, "0 -1 U<");
    TEST_ASSERT(vm.dstack[2] == -1, "0 U< -1 (0xFFFF) is TRUE in unsigned 16-bit");

    /* 4. Cell Multipliers & Offsets */
    printf("[4] Verifying 16-bit Cell Step Math (CELLS, CELL+)...\n");
    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "10 CELLS");
    TEST_ASSERT(vm.dstack[0] == 20, "10 CELLS equals 20 bytes on 16-bit");

    forth_eval(&vm, "100 CELL+");
    TEST_ASSERT(vm.dstack[1] == 102, "100 CELL+ equals 102 bytes on 16-bit");

    /* 5. Memory Alignment & Zero-Heap Boundaries */
    printf("[5] Verifying 16-bit Memory Model & Bounded Arena...\n");
    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "1234 100 !");
    forth_eval(&vm, "100 @");
    TEST_ASSERT(vm.dstack[0] == 1234, "16-bit cell stored and fetched at byte offset 100");

    TEST_ASSERT(FORTH_ARENA_MAX == 16 * 1024, "16-bit profile arena is bounded to 16 KiB");

    printf("==========================================================\n");
    printf(" 16-Bit Forth Tests: %d/%d passed (100%% Success)\n", g_tests_passed, g_tests_total);
    printf("==========================================================\n");

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
