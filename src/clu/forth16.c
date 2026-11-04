/*
 * B-System BTRON3 / T-FW — forth16.c
 * Native 16-Bit ANS Forth-2012 Engine & Verification Runner
 * Forces FORTH_16BIT=1 for bit-exact parity with 16-bit cell hardware (MSX Z80 / PC-98 IA-16).
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#ifndef FORTH_16BIT
#define FORTH_16BIT 1
#endif

#include <btron/forth_vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ForthVM s_forth16_vm;

static void init_16bit_devtree(ForthVM *vm) {
    FCodeNode *root = forth_dev_create(vm, "/", NULL);
    if (!root) return;

    static const char sys_desc[] = "16-Bit T-FW Engine Profile";
    forth_dev_set_prop(root, "device_type", sys_desc, (uint16_t)strlen(sys_desc));

    FCodeNode *cpus = forth_dev_create(vm, "cpus", root);
    if (cpus) {
        FCodeNode *cpu0 = forth_dev_create(vm, "cpu@0", cpus);
        if (cpu0) {
            static const char cpu_name[] = "Z80/IA-16 Compatible Core";
            forth_dev_set_prop(cpu0, "model", cpu_name, (uint16_t)strlen(cpu_name));
        }
    }

    FCodeNode *mem = forth_dev_create(vm, "memory@0", root);
    if (mem) {
        static const uint16_t mem_size = (uint16_t)FORTH_ARENA_MAX;
        forth_dev_set_prop(mem, "reg", &mem_size, sizeof(mem_size));
    }
}

int main(int argc, char **argv) {
    forth_vm_init(&s_forth16_vm);
    init_16bit_devtree(&s_forth16_vm);

    /* Assert that cell width is strictly 16-bit (2 bytes) */
    if (sizeof(forth_cell_t) != 2 || FORTH_CELL_SIZE != 2) {
        fprintf(stderr, "forth16: FATAL: forth_cell_t is %zu bytes, expected 2 bytes!\n",
                sizeof(forth_cell_t));
        return 1;
    }

    if (argc > 1) {
        if (strcmp(argv[1], "-e") == 0 && argc > 2) {
            return forth_eval(&s_forth16_vm, argv[2]);
        }
        FILE *f = fopen(argv[1], "r");
        if (!f) {
            fprintf(stderr, "forth16: cannot open '%s'\n", argv[1]);
            return 1;
        }
        char line[FORTH_TIB_MAX];
        while (fgets(line, sizeof(line), f) && !s_forth16_vm.halted) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
                line[--len] = '\0';
            }
            forth_eval(&s_forth16_vm, line);
        }
        fclose(f);
        return s_forth16_vm.last_error;
    }

    printf("ANS Forth-2012 Core Subset (16-Bit Cell Native Profile)\n");
    printf("Cell: %d-bit (Range: %d .. %d), Arena: %d KB\n",
           (int)(FORTH_CELL_SIZE * 8), -32768, 32767, (int)(FORTH_ARENA_MAX / 1024));
    printf("Bit-exact parity profile for T-FW MSX & PC-98.\n\n");

    char line[FORTH_TIB_MAX];
    while (!s_forth16_vm.halted) {
        printf(s_forth16_vm.state ? "] " : "ok ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }

        forth_eval(&s_forth16_vm, line);
    }
    return 0;
}

