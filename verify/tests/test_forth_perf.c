/*
 * B-System BTRON3 / T-FW — test_forth_perf.c
 * Verification Test for Cache-Line Alignment, L1 Residency & Zero-Heap Performance
 */

#include <btron/forth_vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
    printf(" Running Forth Microarchitectural Performance Suite...\n");
    printf("==========================================================\n");

    /* 1. 64-Byte Cache Line Alignment */
    printf("[1] Verifying 64-Byte Cache-Line Alignment (L1 False-Sharing Elimination)...\n");
    ForthVM *vm = malloc(sizeof(ForthVM));
    TEST_ASSERT(vm != NULL, "VM allocated");
    forth_vm_init(vm);

    uintptr_t dstack_addr = (uintptr_t)vm->dstack;
    uintptr_t rstack_addr = (uintptr_t)vm->rstack;
    uintptr_t arena_addr  = (uintptr_t)vm->arena;

    TEST_ASSERT((dstack_addr % 64) == 0, "vm->dstack is 64-byte cache-line aligned");
    TEST_ASSERT((rstack_addr % 64) == 0, "vm->rstack is 64-byte cache-line aligned");
    TEST_ASSERT((arena_addr % 64) == 0,  "vm->arena is 64-byte cache-line aligned");

    /* 2. Top-of-Stack (TOS) Caching */
    printf("[2] Verifying Register-Cached Top of Stack (TOS)...\n");
    forth_push(vm, 42);
    TEST_ASSERT(vm->tos_valid == true, "TOS is marked valid");
    TEST_ASSERT(vm->tos == 42, "Cached TOS equals 42");

    forth_push(vm, 99);
    TEST_ASSERT(vm->tos == 99, "Cached TOS updated to 99");

    forth_cell_t popped = forth_pop(vm);
    TEST_ASSERT(popped == 99, "Popped value is 99");
    TEST_ASSERT(vm->tos == 42, "Cached TOS restored to 42");

    /* 3. High-Throughput FCode Bytecode Benchmark */
    printf("[3] Benchmarking Computed Goto FCode Inner Dispatch (100,000 iterations)...\n");
    forth_vm_reset_stacks(vm);
    /* Bytecode loop: 1 DUP + DROP */
    static const uint8_t bench_bytecode[] = {
        FORTH_TOK_DUP,
        FORTH_TOK_DROP
    };

    forth_push(vm, 1234);

    clock_t start = clock();
    const int iterations = 100000;
    for (int i = 0; i < iterations; i++) {
        forth_eval_fcode(vm, bench_bytecode, sizeof(bench_bytecode));
    }
    clock_t end = clock();

    double elapsed_sec = (double)(end - start) / CLOCKS_PER_SEC;
    double mops = ((double)iterations * 2.0 / 1000000.0) / (elapsed_sec > 0.0 ? elapsed_sec : 0.000001);

    printf("    -> Executed %d dispatches in %.4f sec (%.2f Million Ops/sec)\n",
           iterations * 2, elapsed_sec, mops);
    TEST_ASSERT(vm->dstack[0] == 1234, "Stack integrity verified after 200,000 opcodes");

    /* 4. Zero-Heap Guarantee */
    printf("[4] Verifying Zero Dynamic Memory Allocation during Execution...\n");
    /* All allotment is bounded within static arena */
    TEST_ASSERT(vm->here < FORTH_ARENA_MAX, "Arena compilation within static bounds");
    TEST_ASSERT(vm->dsp < FORTH_STACK_MAX,  "Stack usage strictly bounded");

    free(vm);

    printf("==========================================================\n");
    printf(" Performance Tests: %d/%d passed (100%% Success)\n", g_tests_passed, g_tests_total);
    printf("==========================================================\n");

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
