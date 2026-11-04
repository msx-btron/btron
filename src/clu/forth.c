/*
 * B-System BTRON3 / T-FW — forth.c
 * Native B-System Command Line Forth & OpenFirmware Interpreter (clu forth)
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#include <btron/forth_vm.h>
#include <btron/core.h>
#include <btron/libc_shim.h>

static ForthVM s_forth_vm;
static int s_vm_initialized = 0;
static ShellOutputFn s_clu_out = NULL;
static void *s_clu_ud = NULL;
static char s_emit_buf[256];
static size_t s_emit_idx = 0;

static void clu_forth_flush(void) {
    if (s_clu_out && s_emit_idx > 0) {
        s_emit_buf[s_emit_idx] = '\0';
        s_clu_out(s_emit_buf, COLOR_LTGRAY, s_clu_ud);
        s_emit_idx = 0;
    }
}

static void clu_forth_emit(char c) {
    if (s_clu_out) {
        if (c == '\n' || s_emit_idx >= sizeof(s_emit_buf) - 2) {
            s_emit_buf[s_emit_idx] = '\0';
            s_clu_out(s_emit_buf, COLOR_LTGRAY, s_clu_ud);
            s_emit_idx = 0;
        } else {
            s_emit_buf[s_emit_idx++] = c;
        }
    } else {
#if BTRON_HOSTED
        putchar(c);
        fflush(stdout);
#else
        (void)c;
#endif
    }
}

static void init_system_devtree(ForthVM *vm) {
    FCodeNode *root = forth_dev_find(vm, "/");
    if (!root) root = forth_dev_create(vm, "/", NULL);
    if (!root) return;
    vm->active_node = root;

    static const char sys_type[] = "boot-firmware";
    static const char sys_model[] = "B-System / Sakamura T-Kernel 2.0 Engine";
    forth_dev_set_prop(root, "device_type", sys_type, (uint16_t)strlen(sys_type));
    forth_dev_set_prop(root, "model", sys_model, (uint16_t)strlen(sys_model));

    FCodeNode *cpus = forth_dev_create(vm, "cpus", root);
    if (cpus) {
#if defined(__aarch64__)
        static const char cpu_model[] = "AArch64 Core (ARMv8-A)";
#elif defined(__x86_64__)
        static const char cpu_model[] = "x86_64 SMP Core (Kota Uchida Engine)";
#elif defined(__arm__)
        static const char cpu_model[] = "ARM32 Core (ARM1136 / Cortex-A7)";
#elif defined(__mips__)
        static const char cpu_model[] = "MIPS R5900 (Emotion Engine)";
#else
        static const char cpu_model[] = "Generic Microprocessor";
#endif
        static const char cpus_type[] = "cpus";
        forth_dev_set_prop(cpus, "device_type", cpus_type, (uint16_t)strlen(cpus_type));
        FCodeNode *cpu0 = forth_dev_create(vm, "cpu@0", cpus);
        if (cpu0) {
            forth_dev_set_prop(cpu0, "model", cpu_model, (uint16_t)strlen(cpu_model));
            static const char cpu_status[] = "running";
            forth_dev_set_prop(cpu0, "status", cpu_status, (uint16_t)strlen(cpu_status));
        }
    }

    FCodeNode *mem = forth_dev_create(vm, "memory@0", root);
    if (mem) {
        static const uint32_t mem_size = (uint32_t)FORTH_ARENA_MAX;
        forth_dev_set_prop(mem, "reg", &mem_size, sizeof(mem_size));
        static const char mem_model[] = "Zero-heap Arena / BTRON Linear Space";
        forth_dev_set_prop(mem, "model", mem_model, (uint16_t)strlen(mem_model));
    }

    FCodeNode *kernel = forth_dev_create(vm, "kernel", root);
    if (kernel) {
        static const char kern_type[] = "microkernel";
        static const char kern_model[] = "Sakamura T-Kernel 2.0 RTOS";
        forth_dev_set_prop(kernel, "device_type", kern_type, (uint16_t)strlen(kern_type));
        forth_dev_set_prop(kernel, "model", kern_model, (uint16_t)strlen(kern_model));

        FCodeNode *tasks = forth_dev_create(vm, "tasks", kernel);
        if (tasks) {
            static const char task_model[] = "ITRON / T-Kernel Task Subsystem";
            forth_dev_set_prop(tasks, "model", task_model, (uint16_t)strlen(task_model));
        }
        FCodeNode *sem = forth_dev_create(vm, "semaphores", kernel);
        if (sem) {
            static const char sem_model[] = "Sync Semaphores & Mutexes";
            forth_dev_set_prop(sem, "model", sem_model, (uint16_t)strlen(sem_model));
        }
        FCodeNode *timer = forth_dev_create(vm, "timer", kernel);
        if (timer) {
            static const char timer_model[] = "System RTC (1000 Hz Tick)";
            forth_dev_set_prop(timer, "model", timer_model, (uint16_t)strlen(timer_model));
        }
    }

    FCodeNode *devices = forth_dev_create(vm, "devices", root);
    if (devices) {
        static const char dev_type[] = "bus";
        static const char dev_model[] = "B-System I/O Bus & Peripherals";
        forth_dev_set_prop(devices, "device_type", dev_type, (uint16_t)strlen(dev_type));
        forth_dev_set_prop(devices, "model", dev_model, (uint16_t)strlen(dev_model));

        FCodeNode *disp = forth_dev_create(vm, "display", devices);
        if (disp) {
            static const char disp_model[] = "VGA 32-bpp Linear Framebuffer";
            forth_dev_set_prop(disp, "model", disp_model, (uint16_t)strlen(disp_model));
        }
        FCodeNode *kbd = forth_dev_create(vm, "keyboard", devices);
        if (kbd) {
            static const char kbd_model[] = "TRON / JIS Keyboard Subsystem";
            forth_dev_set_prop(kbd, "model", kbd_model, (uint16_t)strlen(kbd_model));
        }
        FCodeNode *mouse = forth_dev_create(vm, "mouse", devices);
        if (mouse) {
            static const char mouse_model[] = "2-Button / 3-Button Pointer Device";
            forth_dev_set_prop(mouse, "model", mouse_model, (uint16_t)strlen(mouse_model));
        }
        FCodeNode *storage = forth_dev_create(vm, "storage", devices);
        if (storage) {
            static const char storage_model[] = "HFDS Real Body Storage";
            forth_dev_set_prop(storage, "model", storage_model, (uint16_t)strlen(storage_model));

            FCodeNode *vsys = forth_dev_create(vm, "vol@sys", storage);
            if (vsys) {
                static const char vsys_model[] = "Mounted /SYS (B-FS Volume 1)";
                forth_dev_set_prop(vsys, "model", vsys_model, (uint16_t)strlen(vsys_model));
            }
            FCodeNode *vanders = forth_dev_create(vm, "vol@anders", storage);
            if (vanders) {
                static const char vanders_model[] = "Mounted /ANDERS (B-FS Volume 2)";
                forth_dev_set_prop(vanders, "model", vanders_model, (uint16_t)strlen(vanders_model));
            }
        }
    }
}

/* ── CLU Shell Command Entry Point ──────────────────────────────── */
void clu_cmd_forth(const char *args, ShellOutputFn out, void *ud) {
    s_clu_out = out;
    s_clu_ud = ud;

    if (!s_vm_initialized) {
        forth_vm_init(&s_forth_vm);
        s_forth_vm.emit_fn = clu_forth_emit;
        init_system_devtree(&s_forth_vm);
        s_vm_initialized = 1;
    }

    if (args && *args) {
        /* Skip leading spaces */
        while (*args == ' ' || *args == '\t') args++;
        if (*args) {
            forth_eval(&s_forth_vm, args);
            clu_forth_flush();
            return;
        }
    }

    out("T-FW OpenFirmware / ANS Forth Engine (B-System CLU)", COLOR_CYAN, ud);
    char status[80];
    snprintf(status, sizeof(status), "IEEE 1275 Device-Tree (%d-bit Cells, %d KB Arena)",
             (int)(FORTH_CELL_SIZE * 8), (int)(FORTH_ARENA_MAX / 1024));
    out(status, COLOR_LTGRAY, ud);
    out("Type WORDS or DEV-TREE for info.", COLOR_YELLOW, ud);
    out("Type 'bye' or 'exit' to return to shell.", COLOR_LTGRAY, ud);
}

int clu_forth_is_halted(void) {
    return s_forth_vm.halted;
}

void clu_forth_reset_halt(void) {
    s_forth_vm.halted = false;
}

/* ── Interactive CLI REPL ───────────────────────────────────────── */
int forth_repl(ForthVM *vm) {
#if BTRON_HOSTED
    char line[FORTH_TIB_MAX];
    printf("T-FW OpenFirmware / ANS Forth Engine (B-System CLU)\n");
    printf("IEEE 1275 Device-Tree (%d-bit Cells, %d KB Arena)\n",
           (int)(FORTH_CELL_SIZE * 8), (int)(FORTH_ARENA_MAX / 1024));
    printf("Type WORDS or DEV-TREE for info.\n");
    printf("Type 'BYE' or 'exit' to return to shell.\n\n");

    while (!vm->halted) {
        printf(vm->state ? "] " : "ok ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }

        forth_eval(vm, line);
    }
    return 0;
#else
    (void)vm;
    return 0;
#endif
}

#if defined(FORTH_STANDALONE)
int main(int argc, char **argv) {
    forth_vm_init(&s_forth_vm);
    init_system_devtree(&s_forth_vm);

    if (argc > 1) {
        /* Run script or single expression */
        if (strcmp(argv[1], "-e") == 0 && argc > 2) {
            return forth_eval(&s_forth_vm, argv[2]);
        }
        FILE *f = fopen(argv[1], "r");
        if (!f) {
            fprintf(stderr, "forth: cannot open '%s'\n", argv[1]);
            return 1;
        }
        char line[FORTH_TIB_MAX];
        while (fgets(line, sizeof(line), f) && !s_forth_vm.halted) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
                line[--len] = '\0';
            }
            forth_eval(&s_forth_vm, line);
        }
        fclose(f);
        return s_forth_vm.last_error;
    }

    return forth_repl(&s_forth_vm);
}
#endif
