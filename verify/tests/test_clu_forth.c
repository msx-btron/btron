/*
 * B-System BTRON3 / T-FW — test_clu_forth.c
 * Automated Unit & Conformance Test Suite for ANS Forth-2012 Core Subset
 */

#include <btron/forth_vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int g_tests_passed = 0;
static int g_tests_total = 0;

static char s_test_output[1024];
static size_t s_test_idx = 0;

static void test_emit(char c) {
    if (s_test_idx < sizeof(s_test_output) - 1) {
        s_test_output[s_test_idx++] = c;
        s_test_output[s_test_idx] = '\0';
    }
}

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
    printf(" Running ANS Forth-2012 Core Subset Test Suite...\n");
    printf("==========================================================\n");

    ForthVM vm;
    forth_vm_init(&vm);

    /* 1. Stack Manipulation */
    printf("[1] Verifying Stack Primitives (DUP, DROP, SWAP, OVER, ROT)...\n");
    forth_eval(&vm, "10 DUP");
    TEST_ASSERT(vm.dsp == 1, "DUP increases stack depth");
    TEST_ASSERT(vm.dstack[0] == 10 && vm.dstack[1] == 10, "DUP duplicates TOS");

    forth_eval(&vm, "DROP");
    TEST_ASSERT(vm.dsp == 0 && vm.dstack[0] == 10, "DROP discards TOS");

    forth_eval(&vm, "20 SWAP");
    TEST_ASSERT(vm.dstack[0] == 20 && vm.dstack[1] == 10, "SWAP exchanges top two items");

    forth_eval(&vm, "OVER");
    TEST_ASSERT(vm.dsp == 2 && vm.dstack[2] == 20, "OVER copies second item");

    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "1 2 3 ROT");
    TEST_ASSERT(vm.dstack[0] == 2 && vm.dstack[1] == 3 && vm.dstack[2] == 1, "ROT rotates top three items");

    /* 2. Arithmetic & Logic */
    printf("[2] Verifying Arithmetic & Logic (+, -, *, /, MOD, NEGATE, Bitwise)...\n");
    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "15 25 +");
    TEST_ASSERT(vm.dstack[0] == 40, "15 + 25 = 40");

    forth_eval(&vm, "10 -");
    TEST_ASSERT(vm.dstack[0] == 30, "40 - 10 = 30");

    forth_eval(&vm, "3 *");
    TEST_ASSERT(vm.dstack[0] == 90, "30 * 3 = 90");

    forth_eval(&vm, "7 /");
    TEST_ASSERT(vm.dstack[0] == 12, "90 / 7 = 12");

    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "29 5 MOD");
    TEST_ASSERT(vm.dstack[0] == 4, "29 MOD 5 = 4");

    forth_eval(&vm, "NEGATE");
    TEST_ASSERT(vm.dstack[0] == -4, "NEGATE 4 = -4");

    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "12 10 AND");
    TEST_ASSERT(vm.dstack[0] == 8, "12 & 10 = 8");

    forth_eval(&vm, "3 OR");
    TEST_ASSERT(vm.dstack[0] == 11, "8 | 3 = 11");

    forth_eval(&vm, "6 XOR");
    TEST_ASSERT(vm.dstack[0] == 13, "11 ^ 6 = 13");

    /* 3. Comparisons */
    printf("[3] Verifying Relational Operators (0=, =, <, >)...\n");
    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "0 0=");
    TEST_ASSERT(vm.dstack[0] == -1, "0 0= is TRUE (-1)");

    forth_eval(&vm, "5 0=");
    TEST_ASSERT(vm.dstack[1] == 0, "5 0= is FALSE (0)");

    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "42 42 =");
    TEST_ASSERT(vm.dstack[0] == -1, "42 42 = is TRUE");

    forth_eval(&vm, "10 20 <");
    TEST_ASSERT(vm.dstack[1] == -1, "10 < 20 is TRUE");

    forth_eval(&vm, "30 20 <");
    TEST_ASSERT(vm.dstack[2] == 0, "30 < 20 is FALSE");

    /* 4. Memory Access & Cells */
    printf("[4] Verifying Memory Primitives (@, !, C@, C!, +!, CELLS, CELL+)...\n");
    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "100 0 !");
    forth_eval(&vm, "0 @");
    TEST_ASSERT(vm.dstack[0] == 100, "Memory store and fetch cell");

    forth_eval(&vm, "25 0 +!");
    forth_eval(&vm, "0 @");
    TEST_ASSERT(vm.dstack[1] == 125, "+! adds n to cell");

    forth_eval(&vm, "42 16 C!");
    forth_eval(&vm, "16 C@");
    TEST_ASSERT(vm.dstack[2] == 42, "C! and C@ byte access");

    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "3 CELLS");
    TEST_ASSERT(vm.dstack[0] == 3 * FORTH_CELL_SIZE, "CELLS calculates byte size");

    /* 5. Colon Definition Compilation */
    printf("[5] Verifying Colon Definitions (: SQUARE DUP * ;)...\n");
    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, ": SQUARE DUP * ;");
    forth_eval(&vm, "7 SQUARE");
    TEST_ASSERT(vm.dstack[0] == 49, "7 SQUARE = 49");

    forth_eval(&vm, ": DOUBLE DUP + ;");
    forth_eval(&vm, "21 DOUBLE");
    TEST_ASSERT(vm.dstack[1] == 42, "21 DOUBLE = 42");

    /* 6. OpenFirmware Device Tree */
    printf("[6] Verifying OpenFirmware Device-Tree API...\n");
    FCodeNode *root = forth_dev_find(&vm, "/");
    TEST_ASSERT(root != NULL, "Root node '/' found");

    FCodeNode *test_dev = forth_dev_create(&vm, "vdp@98", root);
    TEST_ASSERT(test_dev != NULL, "Device node 'vdp@98' created");

    static const uint16_t vram_size = 128;
    int err = forth_dev_set_prop(test_dev, "vram", &vram_size, sizeof(vram_size));
    TEST_ASSERT(err == 0, "Property 'vram' set successfully");

    uint16_t out_len = 0;
    const void *val = forth_dev_get_prop(test_dev, "vram", &out_len);
    TEST_ASSERT(val != NULL && out_len == sizeof(uint16_t), "Property 'vram' retrieved");
    TEST_ASSERT(*(const uint16_t *)val == 128, "Property 'vram' value is 128");

    FCodeNode *found = forth_dev_find(&vm, "/vdp@98");
    TEST_ASSERT(found == test_dev, "Path resolution for '/vdp@98' matches created node");

    /* 7. Words, Device-Tree & Interactive Recovery (CLU Parity with MSX) */
    printf("[7] Verifying CLU Words, Dev-Tree, Help & Non-Fatal Abort Recovery...\n");
    vm.emit_fn = test_emit;

    /* Case-insensitive words */
    s_test_idx = 0;
    forth_eval(&vm, "words");
    TEST_ASSERT(strstr(s_test_output, "DUP") != NULL && strstr(s_test_output, "DEV-TREE") != NULL,
                "Case-insensitive 'words' outputs dictionary words");

    /* Case-insensitive dev-tree */
    s_test_idx = 0;
    forth_eval(&vm, "dev-tree");
    TEST_ASSERT(strstr(s_test_output, "/ (Root)") != NULL && strstr(s_test_output, "/vdp@98") != NULL,
                "Case-insensitive 'dev-tree' dumps device hierarchy");

    /* Case-insensitive help & ? & info */
    s_test_idx = 0;
    forth_eval(&vm, "help");
    TEST_ASSERT(strstr(s_test_output, "OpenFirmware") != NULL && strstr(s_test_output, "WORDS") != NULL,
                "Case-insensitive 'help' prints commands summary");

    s_test_idx = 0;
    forth_eval(&vm, "?");
    TEST_ASSERT(strstr(s_test_output, "OpenFirmware") != NULL, "'?' aliases to help");

    s_test_idx = 0;
    forth_eval(&vm, "info");
    TEST_ASSERT(strstr(s_test_output, "OpenFirmware") != NULL, "'info' aliases to help");

    /* Non-fatal abort recovery */
    s_test_idx = 0;
    int err_code = forth_eval(&vm, "some_unknown_command");
    TEST_ASSERT(err_code != 0, "Unknown word returns non-zero error code");
    TEST_ASSERT(strstr(s_test_output, "? some_unknown_command") != NULL, "Unknown word prints diagnostic '? <word>'");
    TEST_ASSERT(vm.halted == false, "VM is not permanently halted after abort");

    /* Subsequent command executes cleanly */
    forth_vm_reset_stacks(&vm);
    forth_eval(&vm, "40 2 +");
    TEST_ASSERT(vm.dsp == 0 && vm.dstack[0] == 42, "Subsequent command executes cleanly after error");

    printf("==========================================================\n");
    printf(" ANS Forth Core Tests: %d/%d passed (100%% Success)\n", g_tests_passed, g_tests_total);
    printf("==========================================================\n");

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}

