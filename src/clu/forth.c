/*
 * B-System BTRON3 / T-FW — forth.c
 * Native B-System Command Line Forth & OpenFirmware Interpreter (clu forth)
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#include <btron/forth_vm.h>
#include <btron/core.h>
#include <btron/wnd.h>
#include <btron/event.h>
#include <btron/libc_shim.h>
#include <time.h>
#if BTRON_HOSTED || defined(FORTH_STANDALONE)
#include <poll.h>
#endif
#include "vfs.h"

__attribute__((weak)) WND* opn_wnd(const char *title, H x, H y, H w, H h, UW attr) {
    (void)title; (void)x; (void)y; (void)w; (void)h; (void)attr;
    return NULL;
}
__attribute__((weak)) ER cls_wnd(WND *wnd) {
    (void)wnd;
    return E_OK;
}
__attribute__((weak)) ER inval_wnd(WND *wnd) {
    (void)wnd;
    return E_OK;
}
__attribute__((weak)) int vfs_read_file(const char *path, char *buf, size_t max_bytes, size_t *out_bytes) {
    (void)path; (void)buf; (void)max_bytes; (void)out_bytes;
    return -1;
}

static ForthVM s_forth_vm;
static int s_vm_initialized = 0;
static ShellOutputFn s_clu_out = NULL;
static void *s_clu_ud = NULL;
static char s_emit_buf[256];
static size_t s_emit_idx = 0;
static void clu_forth_flush(void);

/* Keyboard Event Ring Buffer for Interactive Forth */
static int s_forth_key_buf[64];
static int s_forth_key_head = 0;
static int s_forth_key_tail = 0;

static void forth_enqueue_key(int key) {
    int next = (s_forth_key_head + 1) % 64;
    if (next != s_forth_key_tail) {
        s_forth_key_buf[s_forth_key_head] = key;
        s_forth_key_head = next;
    }
}

static bool clu_forth_key_q(void) {
    if (s_forth_key_head != s_forth_key_tail) {
        return true;
    }
#if BTRON_HOSTED || defined(FORTH_STANDALONE)
    struct pollfd pfd = { .fd = 0, .events = POLLIN, .revents = 0 };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        return true;
    }
#endif
    return false;
}

static int clu_forth_key(void) {
    if (s_forth_key_head != s_forth_key_tail) {
        int k = s_forth_key_buf[s_forth_key_tail];
        s_forth_key_tail = (s_forth_key_tail + 1) % 64;
        return k;
    }
#if BTRON_HOSTED || defined(FORTH_STANDALONE)
    int c = getchar();
    if (c == 27) {
        struct pollfd pfd = { .fd = 0, .events = POLLIN, .revents = 0 };
        if (poll(&pfd, 1, 10) > 0 && (pfd.revents & POLLIN)) {
            int c2 = getchar();
            if (c2 == '[' && poll(&pfd, 1, 10) > 0 && (pfd.revents & POLLIN)) {
                int c3 = getchar();
                if (c3 == 'A') return 30; /* Up */
                if (c3 == 'B') return 31; /* Down */
                if (c3 == 'C') return 29; /* Right */
                if (c3 == 'D') return 28; /* Left */
            }
        }
    }
    return c;
#else
    return 0;
#endif
}

static void forth_wnd_event_handler(WND *wnd, const EVT *evt) {
    (void)wnd;
    if (!evt) return;
    if (evt->type == EV_KEY_DOWN) {
        UW k = evt->key;
        if (k == BTRON_KEY_LEFT) {
            forth_enqueue_key(28);
        } else if (k == BTRON_KEY_RIGHT) {
            forth_enqueue_key(29);
        } else if (k == BTRON_KEY_UP) {
            forth_enqueue_key(30);
        } else if (k == BTRON_KEY_DOWN) {
            forth_enqueue_key(31);
        } else if (k == BTRON_KEY_SPACE || k == ' ') {
            forth_enqueue_key(32);
        } else if (k < 128) {
            forth_enqueue_key((int)k);
        }
    }
}

static void forth_wnd_paint(WND *wnd, GDEV *dev) {
    (void)dev;
    if (wnd && wnd->user_data) {
        forth_cell_t xt = (forth_cell_t)wnd->user_data;
        if (xt > 0 && xt < FORTH_ARENA_MAX) {
            forth_push(&s_forth_vm, xt);
            uint8_t exec_tok = FORTH_TOK_EXECUTE;
            forth_eval_fcode(&s_forth_vm, &exec_tok, 1);
        }
    }
}

static void clu_forth_wnd_on_paint(ForthVM *vm, forth_cell_t wnd_val, forth_cell_t xt) {
    (void)vm;
    WND *wnd = (WND *)(uintptr_t)wnd_val;
    if (wnd) {
        wnd->user_data = (VW)xt;
        wnd->paint = forth_wnd_paint;
    }
}

static void clu_forth_ms(ForthVM *vm, forth_cell_t ms) {
    (void)vm;
    if (ms > 0) {
#if BTRON_HOSTED
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
#endif
    }
}

static forth_cell_t clu_forth_wnd_open(ForthVM *vm, forth_cell_t w, forth_cell_t h) {
    (void)vm;
    if (w <= 0 || h <= 0) return 0;
    if (w > 1920) w = 1920;
    if (h > 1080) h = 1080;
    WND *wnd = opn_wnd("B-System Forth Surface", 80, 50, (H)w, (H)h,
                       WND_ATTR_TITLE | WND_ATTR_BORDER | WND_ATTR_CLOSE | WND_ATTR_RESIZE);
    if (!wnd) return 0;
    wnd->paint = forth_wnd_paint;
    wnd->event_handler = forth_wnd_event_handler;
    wnd->img_valid = TRUE;
    if (wnd->dev && wnd->dev->pixels) {
        memset(wnd->dev->pixels, 0, (size_t)wnd->dev->width * (size_t)wnd->dev->height * sizeof(COLOR));
    }
    inval_wnd(wnd);
    return (forth_cell_t)(uintptr_t)wnd;
}

static void clu_forth_wnd_close(ForthVM *vm, forth_cell_t wnd_val) {
    (void)vm;
    WND *wnd = (WND *)(uintptr_t)wnd_val;
    if (wnd) cls_wnd(wnd);
}

static void clu_forth_wnd_pixel(ForthVM *vm, forth_cell_t wnd_val, forth_cell_t x, forth_cell_t y, forth_cell_t col) {
    (void)vm;
    WND *wnd = (WND *)(uintptr_t)wnd_val;
    if (wnd && wnd->dev && wnd->dev->pixels) {
        if (x >= 0 && x < wnd->dev->width && y >= 0 && y < wnd->dev->height) {
            wnd->dev->pixels[y * wnd->dev->width + x] = (COLOR)col;
        }
    }
}

static void clu_forth_wnd_vline(ForthVM *vm, forth_cell_t wnd_val, forth_cell_t x, forth_cell_t y1, forth_cell_t y2, forth_cell_t col) {
    (void)vm;
    WND *wnd = (WND *)(uintptr_t)wnd_val;
    if (wnd && wnd->dev && wnd->dev->pixels) {
        if (x < 0 || x >= wnd->dev->width) return;
        if (y1 > y2) { forth_cell_t tmp = y1; y1 = y2; y2 = tmp; }
        if (y1 < 0) y1 = 0;
        if (y2 >= wnd->dev->height) y2 = wnd->dev->height - 1;
        if (y1 > y2) return;
        COLOR *px = &wnd->dev->pixels[y1 * wnd->dev->width + x];
        H stride = wnd->dev->width;
        for (forth_cell_t y = y1; y <= y2; y++) {
            *px = (COLOR)col;
            px += stride;
        }
    }
}

static void clu_forth_wnd_rect(ForthVM *vm, forth_cell_t wnd_val, forth_cell_t x, forth_cell_t y, forth_cell_t w, forth_cell_t h, forth_cell_t col) {
    (void)vm;
    WND *wnd = (WND *)(uintptr_t)wnd_val;
    if (wnd && wnd->dev && wnd->dev->pixels && w > 0 && h > 0) {
        forth_cell_t x2 = x + w - 1;
        forth_cell_t y2 = y + h - 1;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x2 >= wnd->dev->width) x2 = wnd->dev->width - 1;
        if (y2 >= wnd->dev->height) y2 = wnd->dev->height - 1;
        for (forth_cell_t cy = y; cy <= y2; cy++) {
            COLOR *row = &wnd->dev->pixels[cy * wnd->dev->width + x];
            for (forth_cell_t cx = x; cx <= x2; cx++) {
                *row++ = (COLOR)col;
            }
        }
    }
}

static void clu_forth_wnd_clear(ForthVM *vm, forth_cell_t wnd_val, forth_cell_t col) {
    (void)vm;
    WND *wnd = (WND *)(uintptr_t)wnd_val;
    if (wnd && wnd->dev && wnd->dev->pixels) {
        size_t total = (size_t)wnd->dev->width * (size_t)wnd->dev->height;
        COLOR c = (COLOR)col;
        for (size_t i = 0; i < total; i++) {
            wnd->dev->pixels[i] = c;
        }
    }
}

static void clu_forth_wnd_update(ForthVM *vm, forth_cell_t wnd_val) {
    (void)vm;
    WND *wnd = (WND *)(uintptr_t)wnd_val;
    if (wnd) {
        wnd->img_valid = TRUE;
        inval_wnd(wnd);
    }
}

static void clu_forth_wnd_size(ForthVM *vm, forth_cell_t wnd_val, forth_cell_t *out_w, forth_cell_t *out_h) {
    (void)vm;
    WND *wnd = (WND *)(uintptr_t)wnd_val;
    if (wnd && wnd->dev) {
        if (out_w) *out_w = (forth_cell_t)wnd->dev->width;
        if (out_h) *out_h = (forth_cell_t)wnd->dev->height;
    } else {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
    }
}

static int clu_forth_include_file(ForthVM *vm, const char *path) {
    if (!vm || !path || !*path) return -1;

    static char s_inc_buf[65536];
    size_t got_bytes = 0;
    int read_ok = 0;

    /* 1. Try VFS directly or with /SYS/ prefix */
    if (vfs_read_file(path, s_inc_buf, sizeof(s_inc_buf) - 1, &got_bytes) == 0 && got_bytes > 0) {
        read_ok = 1;
    } else {
        char sys_path[128];
        snprintf(sys_path, sizeof(sys_path), "/SYS/%s", path);
        if (vfs_read_file(sys_path, s_inc_buf, sizeof(s_inc_buf) - 1, &got_bytes) == 0 && got_bytes > 0) {
            read_ok = 1;
        }
    }

    /* 2. Try host fopen if VFS was not available or did not find file */
    if (!read_ok) {
#if BTRON_HOSTED
        FILE *f = fopen(path, "rb");
        if (!f) {
            char asset_path[128];
            snprintf(asset_path, sizeof(asset_path), "assets/forth/%s", path);
            f = fopen(asset_path, "rb");
        }
        if (f) {
            got_bytes = fread(s_inc_buf, 1, sizeof(s_inc_buf) - 1, f);
            fclose(f);
            if (got_bytes > 0) read_ok = 1;
        }
#endif
    }

    if (!read_ok) {
        char errmsg[96];
        snprintf(errmsg, sizeof(errmsg), "forth: cannot open '%s'", path);
        forth_abort(vm, errmsg);
        return -1;
    }

    s_inc_buf[got_bytes] = '\0';

    /* Evaluate line by line */
    char *cur = s_inc_buf;
    while (*cur && !vm->halted) {
        char *line_start = cur;
        while (*cur && *cur != '\r' && *cur != '\n') cur++;
        if (*cur == '\r' || *cur == '\n') {
            *cur++ = '\0';
            if (*cur == '\n') cur++;
        }
        if (*line_start) {
            forth_eval(vm, line_start);
        }
    }
    clu_forth_flush();
    return vm->last_error;
}

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

static void clu_forth_init_vm(void) {
    if (!s_vm_initialized) {
        forth_vm_init(&s_forth_vm);
        s_forth_vm.emit_fn = clu_forth_emit;
        s_forth_vm.include_fn = clu_forth_include_file;
        s_forth_vm.wnd_open_fn = clu_forth_wnd_open;
        s_forth_vm.wnd_close_fn = clu_forth_wnd_close;
        s_forth_vm.wnd_pixel_fn = clu_forth_wnd_pixel;
        s_forth_vm.wnd_vline_fn = clu_forth_wnd_vline;
        s_forth_vm.wnd_rect_fn = clu_forth_wnd_rect;
        s_forth_vm.wnd_clear_fn = clu_forth_wnd_clear;
        s_forth_vm.wnd_update_fn = clu_forth_wnd_update;
        s_forth_vm.wnd_size_fn = clu_forth_wnd_size;
        s_forth_vm.wnd_on_paint_fn = clu_forth_wnd_on_paint;
        s_forth_vm.ms_fn = clu_forth_ms;
        s_forth_vm.key_q_fn = clu_forth_key_q;
        s_forth_vm.key_fn = clu_forth_key;
        init_system_devtree(&s_forth_vm);
        s_vm_initialized = 1;
    }
}

/* ── CLU Shell Command Entry Point ──────────────────────────────── */
void clu_cmd_forth(const char *args, ShellOutputFn out, void *ud) {
    s_clu_out = out;
    s_clu_ud = ud;

    clu_forth_init_vm();

    if (args && *args) {
        /* Skip leading spaces */
        while (*args == ' ' || *args == '\t') args++;
        if (*args) {
            size_t alen = strlen(args);
            if ((alen > 4 && strcmp(args + alen - 4, ".fth") == 0) ||
                (alen > 6 && strcmp(args + alen - 6, ".fcode") == 0)) {
                clu_forth_include_file(&s_forth_vm, args);
            } else {
                forth_eval(&s_forth_vm, args);
            }
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
    clu_forth_init_vm();

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
