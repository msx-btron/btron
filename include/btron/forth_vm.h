/*
 * B-System BTRON3 / T-FW — forth_vm.h
 * Forth Virtual Machine & OpenFirmware Device-Tree API
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#ifndef _BTRON_FORTH_VM_H_
#define _BTRON_FORTH_VM_H_

#include "forth_def.h"
#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Device-Tree Types ──────────────────────────────────────────── */
typedef struct {
    char           key[FCODE_PROP_KEY_LEN];
    const uint8_t *val;
    uint16_t       len;
} FCodeProp;

typedef struct FCodeNode {
    char              name[FCODE_NODE_NAME_LEN];
    struct FCodeNode *parent;
    struct FCodeNode *peer;
    struct FCodeNode *child;
    uint8_t           num_props;
    FCodeProp         props[FCODE_MAX_PROPS];
    const uint8_t    *methods_fcode;
    uint16_t          methods_len;
} FCodeNode;

/* ── Forth Virtual Machine Context ──────────────────────────────── */
typedef struct ForthVM ForthVM;

typedef void (*ForthPrimFn)(ForthVM *vm);

struct ForthVM {
    /* Cache-line aligned stacks to eliminate false sharing and split loads */
    FORTH_ALIGN64 forth_cell_t dstack[FORTH_STACK_MAX];
    FORTH_ALIGN64 forth_cell_t rstack[FORTH_RSTACK_MAX];
    FORTH_ALIGN64 uint8_t      arena[FORTH_ARENA_MAX]; /* Dictionary & compiled bytecode */
    FORTH_ALIGN64 uint8_t      ram[FORTH_ARENA_MAX];   /* User addressable memory */

    /* Stack pointers and cached TOS */
    forth_cell_t   dsp;              /* Data stack index (-1 = empty) */
    forth_cell_t   rsp;              /* Return stack index (-1 = empty) */
    forth_cell_t   tos;              /* Top of stack register-cached */
    bool           tos_valid;

    /* Execution state */
    const uint8_t *ip;               /* Instruction pointer */
    const uint8_t *ip_limit;         /* Bounded execution limit */
    bool           halted;
    bool           is_executing;     /* Re-entrancy guard */
    bool           state;            /* 0 = interpret, 1 = compile */
    forth_cell_t   latest;           /* Offset to latest dictionary header */
    forth_cell_t   here;             /* Compilation pointer in arena */

    /* Terminal input buffer */
    char           tib[FORTH_TIB_MAX];
    uint16_t       tib_len;
    uint16_t       tib_in;

    /* Compiler control stack (for IF/ELSE/THEN/DO/LOOP/BEGIN/WHILE/REPEAT) */
    forth_cell_t   cstack[FORTH_STACK_MAX];
    int8_t         csp;

    /* Device Tree */
    FCodeNode      dev_nodes[FORTH_DEV_MAX];
    uint16_t       num_dev_nodes;
    FCodeNode     *active_node;

    /* I/O Callback Hooks */
    void         (*emit_fn)(char c);
    int          (*key_fn)(void);
    bool         (*key_q_fn)(void);
    int          (*include_fn)(ForthVM *vm, const char *path);

    /* Window & Graphics Callback Hooks */
    forth_cell_t (*wnd_open_fn)(ForthVM *vm, forth_cell_t w, forth_cell_t h);
    void         (*wnd_close_fn)(ForthVM *vm, forth_cell_t wnd);
    void         (*wnd_pixel_fn)(ForthVM *vm, forth_cell_t wnd, forth_cell_t x, forth_cell_t y, forth_cell_t col);
    void         (*wnd_vline_fn)(ForthVM *vm, forth_cell_t wnd, forth_cell_t x, forth_cell_t y1, forth_cell_t y2, forth_cell_t col);
    void         (*wnd_rect_fn)(ForthVM *vm, forth_cell_t wnd, forth_cell_t x, forth_cell_t y, forth_cell_t w, forth_cell_t h, forth_cell_t col);
    void         (*wnd_clear_fn)(ForthVM *vm, forth_cell_t wnd, forth_cell_t col);
    void         (*wnd_update_fn)(ForthVM *vm, forth_cell_t wnd);
    void         (*wnd_size_fn)(ForthVM *vm, forth_cell_t wnd, forth_cell_t *out_w, forth_cell_t *out_h);
    void         (*wnd_on_paint_fn)(ForthVM *vm, forth_cell_t wnd, forth_cell_t xt);
    void         (*ms_fn)(ForthVM *vm, forth_cell_t ms);
    forth_cell_t (*file_read_fn)(ForthVM *vm, forth_cell_t dst_addr, forth_cell_t max_bytes, const char *path);

    /* Error and status */
    int            last_error;
};

/* ── Engine API ─────────────────────────────────────────────────── */
void        forth_vm_init(ForthVM *vm);
void        forth_vm_reset_stacks(ForthVM *vm);
int         forth_eval(ForthVM *vm, const char *line);
int         forth_eval_fcode(ForthVM *vm, const uint8_t *stream, size_t len);
void        forth_abort(ForthVM *vm, const char *msg);
void        forth_words(ForthVM *vm);
void        forth_help(ForthVM *vm);
void        forth_dev_dump(ForthVM *vm);

/* Stack primitives */
void        forth_push(ForthVM *vm, forth_cell_t val);
forth_cell_t forth_pop(ForthVM *vm);

/* Device Tree API */
FCodeNode*  forth_dev_create(ForthVM *vm, const char *name, FCodeNode *parent);
FCodeNode*  forth_dev_find(ForthVM *vm, const char *path);
int         forth_dev_set_prop(FCodeNode *node, const char *key, const void *val, uint16_t len);
const void* forth_dev_get_prop(FCodeNode *node, const char *key, uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FORTH_VM_H_ */
