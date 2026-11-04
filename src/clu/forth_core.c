/*
 * B-System BTRON3 / T-FW — forth_core.c
 * High-Performance ANS Forth-2012 Core Subset & FCode Engine
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#include <btron/forth_vm.h>
#include <btron/libc_shim.h>

/* ── Default I/O Callbacks ──────────────────────────────────────── */
static void default_emit(char c) {
#if BTRON_HOSTED
    putchar(c);
    fflush(stdout);
#else
    (void)c;
#endif
}

static int default_key(void) {
#if BTRON_HOSTED
    return getchar();
#else
    return -1;
#endif
}

static bool default_key_q(void) {
    return true;
}

static int forth_strncasecmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c1 = s1[i];
        char c2 = s2[i];
        if (c1 >= 'a' && c1 <= 'z') c1 = (char)(c1 - 32);
        if (c2 >= 'a' && c2 <= 'z') c2 = (char)(c2 - 32);
        if (c1 != c2 || c1 == '\0') {
            return (int)((unsigned char)c1 - (unsigned char)c2);
        }
    }
    return 0;
}

static bool is_valid_dev_node(const ForthVM *vm, const FCodeNode *n) {
    if (!vm || !n || vm->num_dev_nodes == 0) return false;
    if (n >= &vm->dev_nodes[0] && n < &vm->dev_nodes[vm->num_dev_nodes]) {
        return true;
    }
    return false;
}



/* ── Stack Access Helpers with TOS Caching ──────────────────────── */
void forth_push(ForthVM *vm, forth_cell_t val) {
    if (vm->dsp + 1 >= FORTH_STACK_MAX) {
        forth_abort(vm, "Data stack overflow");
        return;
    }
    vm->dstack[++vm->dsp] = val;
    vm->tos = val;
    vm->tos_valid = true;
}

forth_cell_t forth_pop(ForthVM *vm) {
    if (vm->dsp < 0) {
        forth_abort(vm, "Data stack underflow");
        return 0;
    }
    forth_cell_t val = vm->dstack[vm->dsp--];
    if (vm->dsp >= 0) {
        vm->tos = vm->dstack[vm->dsp];
        vm->tos_valid = true;
    } else {
        vm->tos_valid = false;
    }
    return val;
}

static void rpush(ForthVM *vm, forth_cell_t val) {
    if (vm->rsp + 1 >= FORTH_RSTACK_MAX) {
        forth_abort(vm, "Return stack overflow");
        return;
    }
    vm->rstack[++vm->rsp] = val;
}

static forth_cell_t rpop(ForthVM *vm) {
    if (vm->rsp < 0) {
        forth_abort(vm, "Return stack underflow");
        return 0;
    }
    return vm->rstack[vm->rsp--];
}

void forth_vm_reset_stacks(ForthVM *vm) {
    vm->dsp = -1;
    vm->rsp = -1;
    vm->tos_valid = false;
    vm->halted = false;
    vm->last_error = 0;
}

void forth_abort(ForthVM *vm, const char *msg) {
    vm->dsp = -1;
    vm->rsp = -1;
    vm->tos_valid = false;
    vm->state = false;
    vm->last_error = -1;
    if (msg && vm->emit_fn) {
        for (const char *p = msg; *p; p++) vm->emit_fn(*p);
        vm->emit_fn('\n');
    }
}

void forth_words(ForthVM *vm) {
    if (!vm) return;
    forth_cell_t cur = vm->latest;
    int col = 0;
    while (cur != 0 && cur < FORTH_ARENA_MAX) {
        forth_cell_t link = *(forth_cell_t *)&vm->arena[cur];
        forth_cell_t p = cur + (forth_cell_t)sizeof(forth_cell_t);
        uint8_t flen = vm->arena[p++];
        uint8_t wlen = flen & FORTH_LEN_MASK;
        if (!(flen & FORTH_FLAG_SMUDGE) && wlen > 0) {
            if (col + wlen + 1 > 72) {
                if (vm->emit_fn) vm->emit_fn('\n');
                col = 0;
            }
            for (uint8_t i = 0; i < wlen; i++) {
                if (vm->emit_fn) vm->emit_fn((char)vm->arena[p + i]);
            }
            if (vm->emit_fn) vm->emit_fn(' ');
            col += wlen + 1;
        }
        cur = link;
    }
    if (vm->emit_fn) vm->emit_fn('\n');
}

void forth_help(ForthVM *vm) {
    if (!vm || !vm->emit_fn) return;
    static const char help_msg[] =
        "T-FW OpenFirmware / ANS Forth Engine\n"
        "Commands:\n"
        "  WORDS        List all dictionary words\n"
        "  DEV-TREE     Display IEEE 1275 hardware device tree\n"
        "  .S           Display data stack contents\n"
        "  .            Pop and display top of stack\n"
        "  BYE / EXIT   Return to shell\n"
        "Stack: DUP DROP SWAP OVER ROT >R R> R@\n"
        "Math:  + - * / MOD NEGATE AND OR XOR INVERT\n";
    for (const char *p = help_msg; *p; p++) {
        vm->emit_fn(*p);
    }
}

/* ── Dictionary Header Representation in Arena ───────────────────── */
/*
 * Layout in vm->arena:
 * [link: forth_cell_t]
 * [flags_len: uint8_t]
 * [name: length bytes]
 * [alignment padding to FORTH_CELL_SIZE]
 * [cfa: uint8_t token or forth_cell_t]
 * [pfa: parameters / bytecode tokens]
 */

static forth_cell_t align_cell(forth_cell_t offset) {
    forth_cell_t mask = (forth_cell_t)(FORTH_CELL_SIZE - 1);
    return (offset + mask) & ~mask;
}

static void dict_create_word(ForthVM *vm, const char *name, uint8_t token, uint8_t flags) {
    uint8_t len = (uint8_t)strlen(name);
    if (len > 31) len = 31;

    forth_cell_t here = align_cell(vm->here);
    if (here + sizeof(forth_cell_t) + 1 + len + FORTH_CELL_SIZE + 4 >= FORTH_ARENA_MAX) {
        forth_abort(vm, "Dictionary arena full");
        return;
    }

    /* Store link to previous */
    *(forth_cell_t *)&vm->arena[here] = vm->latest;
    vm->latest = here;
    here += (forth_cell_t)sizeof(forth_cell_t);

    /* Store flags & length */
    vm->arena[here++] = (flags & ~FORTH_LEN_MASK) | (len & FORTH_LEN_MASK);

    /* Store name */
    memcpy(&vm->arena[here], name, len);
    here += len;

    /* Align for CFA */
    here = align_cell(here);
    vm->arena[here++] = token;
    vm->here = here;
}

static forth_cell_t dict_find(ForthVM *vm, const char *name, uint8_t len, uint8_t *out_flags, uint8_t *out_token) {
    forth_cell_t cur = vm->latest;
    while (cur != 0 && cur < FORTH_ARENA_MAX) {
        forth_cell_t link = *(forth_cell_t *)&vm->arena[cur];
        forth_cell_t p = cur + (forth_cell_t)sizeof(forth_cell_t);
        uint8_t flen = vm->arena[p++];
        uint8_t wlen = flen & FORTH_LEN_MASK;

        if (wlen == len && forth_strncasecmp((const char *)&vm->arena[p], name, len) == 0) {
            if (out_flags) *out_flags = flen;
            p += wlen;
            p = align_cell(p);
            if (out_token) *out_token = vm->arena[p];
            return p;
        }
        cur = link;
    }
    return 0;
}

/* ── Number Parser ──────────────────────────────────────────────── */
static bool parse_number(const char *str, forth_cell_t *out_val) {
    if (!str || !*str) return false;
    const char *p = str;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') { p++; }
    if (!*p) return false;

    forth_cell_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    if (*p != '\0') return false;
    *out_val = v * sign;
    return true;
}

/* ── High-Performance FCode / Token Bytecode Evaluator ──────────── */
/* Uses computed goto when supported by compiler to eliminate central BTB stalls */
int forth_eval_fcode(ForthVM *vm, const uint8_t *stream, size_t len) {
    if (!vm || !stream || len == 0) return 0;
    vm->ip = stream;
    vm->ip_limit = stream + len;
    vm->halted = false;

#if defined(__GNUC__) || defined(__clang__)
    /* Computed goto jump table */
    static const void *const dispatch_table[256] = {
        [FORTH_TOK_END]        = &&lbl_end,
        [FORTH_TOK_DUP]        = &&lbl_dup,
        [FORTH_TOK_DROP]       = &&lbl_drop,
        [FORTH_TOK_SWAP]       = &&lbl_swap,
        [FORTH_TOK_OVER]       = &&lbl_over,
        [FORTH_TOK_ROT]        = &&lbl_rot,
        [FORTH_TOK_TO_R]       = &&lbl_to_r,
        [FORTH_TOK_R_FROM]     = &&lbl_r_from,
        [FORTH_TOK_R_FETCH]    = &&lbl_r_fetch,
        [FORTH_TOK_TWO_TO_R]   = &&lbl_two_to_r,
        [FORTH_TOK_TWO_R_FROM] = &&lbl_two_r_from,
        [FORTH_TOK_PLUS]       = &&lbl_plus,
        [FORTH_TOK_MINUS]      = &&lbl_minus,
        [FORTH_TOK_MULT]       = &&lbl_mult,
        [FORTH_TOK_DIV]        = &&lbl_div,
        [FORTH_TOK_MOD]        = &&lbl_mod,
        [FORTH_TOK_NEGATE]     = &&lbl_negate,
        [FORTH_TOK_AND]        = &&lbl_and,
        [FORTH_TOK_OR]         = &&lbl_or,
        [FORTH_TOK_XOR]        = &&lbl_xor,
        [FORTH_TOK_INVERT]     = &&lbl_invert,
        [FORTH_TOK_LSHIFT]     = &&lbl_lshift,
        [FORTH_TOK_RSHIFT]     = &&lbl_rshift,
        [FORTH_TOK_ZERO_EQ]    = &&lbl_zero_eq,
        [FORTH_TOK_ZERO_LT]    = &&lbl_zero_lt,
        [FORTH_TOK_EQ]         = &&lbl_eq,
        [FORTH_TOK_LT]         = &&lbl_lt,
        [FORTH_TOK_GT]         = &&lbl_gt,
        [FORTH_TOK_U_LT]       = &&lbl_u_lt,
        [FORTH_TOK_FETCH]      = &&lbl_fetch,
        [FORTH_TOK_STORE]      = &&lbl_store,
        [FORTH_TOK_C_FETCH]    = &&lbl_c_fetch,
        [FORTH_TOK_C_STORE]    = &&lbl_c_store,
        [FORTH_TOK_PLUS_STORE] = &&lbl_plus_store,
        [FORTH_TOK_CELLS]      = &&lbl_cells,
        [FORTH_TOK_CELL_PLUS]  = &&lbl_cell_plus,
        [FORTH_TOK_CHARS]      = &&lbl_chars,
        [FORTH_TOK_CHAR_PLUS]  = &&lbl_char_plus,
        [FORTH_TOK_ALLOT]      = &&lbl_allot,
        [FORTH_TOK_EXECUTE]    = &&lbl_execute,
        [FORTH_TOK_EXIT]       = &&lbl_exit,
        [FORTH_TOK_BRANCH]     = &&lbl_branch,
        [FORTH_TOK_QBRANCH]    = &&lbl_qbranch,
        [FORTH_TOK_LIT]        = &&lbl_lit,
        [FORTH_TOK_BLIT]       = &&lbl_blit,
        [FORTH_TOK_EMIT]       = &&lbl_emit,
        [FORTH_TOK_KEY]        = &&lbl_key,
        [FORTH_TOK_KEY_Q]      = &&lbl_key_q,
        [FORTH_TOK_TYPE]       = &&lbl_type,
        [FORTH_TOK_CR]         = &&lbl_cr,
        [FORTH_TOK_BYE]        = &&lbl_bye,
        [FORTH_TOK_WORDS]      = &&lbl_words,
        [FORTH_TOK_FIND_DEV]   = &&lbl_find_dev,
        [FORTH_TOK_PEER]       = &&lbl_peer,
        [FORTH_TOK_CHILD]      = &&lbl_child,
        [FORTH_TOK_PARENT]     = &&lbl_parent,
        [FORTH_TOK_GET_PROP]   = &&lbl_get_prop,
        [FORTH_TOK_SET_PROP]   = &&lbl_set_prop,
        [FORTH_TOK_MY_SELF]    = &&lbl_my_self,
        [FORTH_TOK_BYTE_LOAD]  = &&lbl_byte_load,
        [FORTH_TOK_DEV_TREE]   = &&lbl_dev_tree,
        [FORTH_TOK_HELP]       = &&lbl_help,
        [FORTH_TOK_PWD]        = &&lbl_pwd,
    };

    #define DISPATCH() do { \
        if (vm->ip >= vm->ip_limit || vm->halted) goto lbl_done; \
        uint8_t tok = *vm->ip++; \
        const void *target = dispatch_table[tok]; \
        if (target) goto *target; \
        else goto lbl_unknown; \
    } while(0)

    DISPATCH();

lbl_dup: {
    forth_push(vm, vm->tos);
    DISPATCH();
}
lbl_drop: {
    forth_pop(vm);
    DISPATCH();
}
lbl_swap: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, b);
    forth_push(vm, a);
    DISPATCH();
}
lbl_over: {
    if (vm->dsp >= 1) {
        forth_push(vm, vm->dstack[vm->dsp - 1]);
    } else {
        forth_abort(vm, "Stack underflow in OVER");
    }
    DISPATCH();
}
lbl_rot: {
    forth_cell_t c = forth_pop(vm);
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, b);
    forth_push(vm, c);
    forth_push(vm, a);
    DISPATCH();
}
lbl_to_r: {
    rpush(vm, forth_pop(vm));
    DISPATCH();
}
lbl_r_from: {
    forth_push(vm, rpop(vm));
    DISPATCH();
}
lbl_r_fetch: {
    if (vm->rsp >= 0) forth_push(vm, vm->rstack[vm->rsp]);
    else forth_abort(vm, "Return stack underflow in R@");
    DISPATCH();
}
lbl_two_to_r: {
    forth_cell_t x2 = forth_pop(vm);
    forth_cell_t x1 = forth_pop(vm);
    rpush(vm, x1);
    rpush(vm, x2);
    DISPATCH();
}
lbl_two_r_from: {
    forth_cell_t x2 = rpop(vm);
    forth_cell_t x1 = rpop(vm);
    forth_push(vm, x1);
    forth_push(vm, x2);
    DISPATCH();
}
lbl_plus: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, a + b);
    DISPATCH();
}
lbl_minus: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, a - b);
    DISPATCH();
}
lbl_mult: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, a * b);
    DISPATCH();
}
lbl_div: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    if (b != 0) forth_push(vm, a / b);
    else forth_abort(vm, "Division by zero");
    DISPATCH();
}
lbl_mod: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    if (b != 0) forth_push(vm, a % b);
    else forth_abort(vm, "Division by zero in MOD");
    DISPATCH();
}
lbl_negate: {
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, -a);
    DISPATCH();
}
lbl_and: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, a & b);
    DISPATCH();
}
lbl_or: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, a | b);
    DISPATCH();
}
lbl_xor: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, a ^ b);
    DISPATCH();
}
lbl_invert: {
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, ~a);
    DISPATCH();
}
lbl_lshift: {
    forth_cell_t u = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, a << u);
    DISPATCH();
}
lbl_rshift: {
    forth_cell_t u = forth_pop(vm);
    forth_ucell_t a = (forth_ucell_t)forth_pop(vm);
    forth_push(vm, (forth_cell_t)(a >> u));
    DISPATCH();
}
lbl_zero_eq: {
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, (a == 0) ? -1 : 0);
    DISPATCH();
}
lbl_zero_lt: {
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, (a < 0) ? -1 : 0);
    DISPATCH();
}
lbl_eq: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, (a == b) ? -1 : 0);
    DISPATCH();
}
lbl_lt: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, (a < b) ? -1 : 0);
    DISPATCH();
}
lbl_gt: {
    forth_cell_t b = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, (a > b) ? -1 : 0);
    DISPATCH();
}
lbl_u_lt: {
    forth_ucell_t b = (forth_ucell_t)forth_pop(vm);
    forth_ucell_t a = (forth_ucell_t)forth_pop(vm);
    forth_push(vm, (a < b) ? -1 : 0);
    DISPATCH();
}
lbl_fetch: {
    forth_cell_t addr = forth_pop(vm);
    if (addr >= 0 && addr + (forth_cell_t)sizeof(forth_cell_t) <= FORTH_ARENA_MAX) {
        forth_push(vm, *(forth_cell_t *)&vm->ram[addr]);
    } else {
        forth_abort(vm, "Memory access violation in @");
    }
    DISPATCH();
}
lbl_store: {
    forth_cell_t addr = forth_pop(vm);
    forth_cell_t val  = forth_pop(vm);
    if (addr >= 0 && addr + (forth_cell_t)sizeof(forth_cell_t) <= FORTH_ARENA_MAX) {
        *(forth_cell_t *)&vm->ram[addr] = val;
    } else {
        forth_abort(vm, "Memory access violation in !");
    }
    DISPATCH();
}
lbl_c_fetch: {
    forth_cell_t addr = forth_pop(vm);
    if (addr >= 0 && addr < FORTH_ARENA_MAX) {
        forth_push(vm, (forth_cell_t)vm->ram[addr]);
    } else {
        forth_abort(vm, "Memory access violation in C@");
    }
    DISPATCH();
}
lbl_c_store: {
    forth_cell_t addr = forth_pop(vm);
    forth_cell_t val  = forth_pop(vm);
    if (addr >= 0 && addr < FORTH_ARENA_MAX) {
        vm->ram[addr] = (uint8_t)val;
    } else {
        forth_abort(vm, "Memory access violation in C!");
    }
    DISPATCH();
}
lbl_plus_store: {
    forth_cell_t addr = forth_pop(vm);
    forth_cell_t n    = forth_pop(vm);
    if (addr >= 0 && addr + (forth_cell_t)sizeof(forth_cell_t) <= FORTH_ARENA_MAX) {
        *(forth_cell_t *)&vm->ram[addr] += n;
    } else {
        forth_abort(vm, "Memory access violation in +!");
    }
    DISPATCH();
}
lbl_cells: {
    forth_cell_t n = forth_pop(vm);
    forth_push(vm, n * FORTH_CELL_SIZE);
    DISPATCH();
}
lbl_cell_plus: {
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, a + FORTH_CELL_SIZE);
    DISPATCH();
}
lbl_chars: {
    /* NOP on byte-addressable architecture */
    DISPATCH();
}
lbl_char_plus: {
    forth_cell_t a = forth_pop(vm);
    forth_push(vm, a + 1);
    DISPATCH();
}
lbl_allot: {
    forth_cell_t n = forth_pop(vm);
    if (vm->here + n <= FORTH_ARENA_MAX) {
        vm->here += n;
    } else {
        forth_abort(vm, "Arena allotment overflow");
    }
    DISPATCH();
}
lbl_execute: {
    forth_cell_t xt = forth_pop(vm);
    if (xt >= 0 && xt < FORTH_ARENA_MAX) {
        uint8_t tok = vm->arena[xt];
        if (tok == FORTH_TOK_ESCAPE) {
            forth_cell_t ret_ip = (forth_cell_t)(vm->ip - vm->arena);
            rpush(vm, ret_ip);
            vm->ip = &vm->arena[xt + 1];
        } else if (dispatch_table[tok] != NULL) {
            goto *dispatch_table[tok];
        } else {
            forth_cell_t ret_ip = (forth_cell_t)(vm->ip - vm->arena);
            rpush(vm, ret_ip);
            vm->ip = &vm->arena[xt];
        }
    }
    DISPATCH();
}
lbl_exit: {
    if (vm->rsp < 0) {
        goto lbl_done;
    }
    forth_cell_t ret_ip = rpop(vm);
    if (ret_ip >= 0 && ret_ip < FORTH_ARENA_MAX) {
        vm->ip = &vm->arena[ret_ip];
        DISPATCH();
    } else {
        goto lbl_done;
    }
}
lbl_branch: {
    int16_t off = (int16_t)(vm->ip[0] | (vm->ip[1] << 8));
    vm->ip += 2 + off;
    DISPATCH();
}
lbl_qbranch: {
    forth_cell_t flag = forth_pop(vm);
    int16_t off = (int16_t)(vm->ip[0] | (vm->ip[1] << 8));
    if (flag == 0) {
        vm->ip += 2 + off;
    } else {
        vm->ip += 2;
    }
    DISPATCH();
}
lbl_lit: {
    forth_cell_t val = 0;
    memcpy(&val, vm->ip, sizeof(forth_cell_t));
    vm->ip += sizeof(forth_cell_t);
    forth_push(vm, val);
    DISPATCH();
}
lbl_blit: {
    uint8_t byte_val = *vm->ip++;
    forth_push(vm, (forth_cell_t)byte_val);
    DISPATCH();
}
lbl_emit: {
    forth_cell_t c = forth_pop(vm);
    if (vm->emit_fn) vm->emit_fn((char)c);
    DISPATCH();
}
lbl_key: {
    int c = vm->key_fn ? vm->key_fn() : 0;
    forth_push(vm, (forth_cell_t)c);
    DISPATCH();
}
lbl_key_q: {
    bool q = vm->key_q_fn ? vm->key_q_fn() : true;
    forth_push(vm, q ? -1 : 0);
    DISPATCH();
}
lbl_type: {
    forth_cell_t u = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    if (vm->emit_fn && a >= 0 && a + u <= FORTH_ARENA_MAX) {
        for (forth_cell_t i = 0; i < u; i++) {
            vm->emit_fn((char)vm->arena[a + i]);
        }
    }
    DISPATCH();
}
lbl_cr: {
    if (vm->emit_fn) vm->emit_fn('\n');
    DISPATCH();
}
lbl_bye: {
    vm->halted = true;
    goto lbl_done;
}
lbl_find_dev: {
    forth_cell_t u = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    char path[32];
    if (u >= 0 && (size_t)u < sizeof(path) && a >= 0 && a + u <= FORTH_ARENA_MAX) {
        memcpy(path, &vm->arena[a], (size_t)u);
        path[u] = '\0';
        FCodeNode *node = forth_dev_find(vm, path);
        forth_push(vm, (forth_cell_t)(uintptr_t)node);
    } else {
        forth_push(vm, 0);
    }
    DISPATCH();
}
lbl_peer: {
    if (vm->dsp >= 0 && is_valid_dev_node(vm, (FCodeNode *)(uintptr_t)vm->tos)) {
        FCodeNode *n = (FCodeNode *)(uintptr_t)forth_pop(vm);
        forth_push(vm, (forth_cell_t)(uintptr_t)(n->peer));
    } else {
        forth_push(vm, (forth_cell_t)0);
    }
    DISPATCH();
}
lbl_child: {
    if (vm->dsp >= 0 && is_valid_dev_node(vm, (FCodeNode *)(uintptr_t)vm->tos)) {
        FCodeNode *n = (FCodeNode *)(uintptr_t)forth_pop(vm);
        forth_push(vm, (forth_cell_t)(uintptr_t)(n->child));
    } else {
        forth_push(vm, (forth_cell_t)0);
    }
    DISPATCH();
}
lbl_parent: {
    if (vm->dsp >= 0 && is_valid_dev_node(vm, (FCodeNode *)(uintptr_t)vm->tos)) {
        FCodeNode *n = (FCodeNode *)(uintptr_t)forth_pop(vm);
        forth_push(vm, (forth_cell_t)(uintptr_t)(n->parent));
    } else {
        if (vm->active_node && vm->active_node->parent) {
            vm->active_node = vm->active_node->parent;
        }
        if (vm->emit_fn) {
            const char *name = (vm->active_node && vm->active_node->name[0]) ? vm->active_node->name : "/";
            if (strcmp(name, "/") != 0) vm->emit_fn('/');
            for (const char *c = name; *c; c++) vm->emit_fn(*c);
            vm->emit_fn('\n');
        }
    }
    DISPATCH();
}
lbl_get_prop: {
    FCodeNode *n = (FCodeNode *)(uintptr_t)forth_pop(vm);
    forth_cell_t plen = forth_pop(vm);
    forth_cell_t padr = forth_pop(vm);
    char key[16];
    if (n && plen >= 0 && (size_t)plen < sizeof(key) && padr >= 0 && padr + plen <= FORTH_ARENA_MAX) {
        memcpy(key, &vm->arena[padr], (size_t)plen);
        key[plen] = '\0';
        uint16_t vlen = 0;
        const void *val = forth_dev_get_prop(n, key, &vlen);
        if (val) {
            forth_cell_t voffset = (forth_cell_t)((const uint8_t *)val - vm->arena);
            forth_push(vm, voffset);
            forth_push(vm, (forth_cell_t)vlen);
            forth_push(vm, 0); /* Success */
        } else {
            forth_push(vm, -1);
        }
    } else {
        forth_push(vm, -1);
    }
    DISPATCH();
}
lbl_set_prop: {
    FCodeNode *n = (FCodeNode *)(uintptr_t)forth_pop(vm);
    forth_cell_t plen = forth_pop(vm);
    forth_cell_t padr = forth_pop(vm);
    forth_cell_t vlen = forth_pop(vm);
    forth_cell_t vadr = forth_pop(vm);
    char key[16];
    if (n && plen >= 0 && (size_t)plen < sizeof(key) && padr >= 0 && padr + plen <= FORTH_ARENA_MAX &&
        vadr >= 0 && vadr + vlen <= FORTH_ARENA_MAX) {
        memcpy(key, &vm->arena[padr], (size_t)plen);
        key[plen] = '\0';
        int err = forth_dev_set_prop(n, key, &vm->arena[vadr], (uint16_t)vlen);
        forth_push(vm, err);
    } else {
        forth_push(vm, -1);
    }
    DISPATCH();
}
lbl_my_self: {
    forth_push(vm, (forth_cell_t)(uintptr_t)vm->active_node);
    DISPATCH();
}
lbl_byte_load: {
    forth_cell_t u = forth_pop(vm);
    forth_cell_t a = forth_pop(vm);
    if (a >= 0 && a + u <= FORTH_ARENA_MAX) {
        forth_eval_fcode(vm, &vm->arena[a], (size_t)u);
    }
    DISPATCH();
}
lbl_words: {
    forth_words(vm);
    DISPATCH();
}
lbl_dev_tree: {
    forth_dev_dump(vm);
    DISPATCH();
}
lbl_help: {
    forth_help(vm);
    DISPATCH();
}
lbl_pwd: {
    if (vm->emit_fn) {
        const char *name = (vm->active_node && vm->active_node->name[0]) ? vm->active_node->name : "/";
        if (strcmp(name, "/") != 0) vm->emit_fn('/');
        for (const char *c = name; *c; c++) vm->emit_fn(*c);
        vm->emit_fn('\n');
    }
    DISPATCH();
}
lbl_unknown: {
    forth_abort(vm, "Undefined FCode bytecode token");
    goto lbl_done;
}
lbl_end:
lbl_done:
    return vm->last_error;

#else
    /* Portable Switch-based fallback */
    while (vm->ip < vm->ip_limit && !vm->halted) {
        uint8_t tok = *vm->ip++;
        switch (tok) {
            case FORTH_TOK_END:        return 0;
            case FORTH_TOK_DUP:        forth_push(vm, vm->tos); break;
            case FORTH_TOK_DROP:       forth_pop(vm); break;
            case FORTH_TOK_SWAP: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, b); forth_push(vm, a);
                break;
            }
            case FORTH_TOK_OVER:
                if (vm->dsp >= 1) forth_push(vm, vm->dstack[vm->dsp - 1]);
                else forth_abort(vm, "Stack underflow in OVER");
                break;
            case FORTH_TOK_ROT: {
                forth_cell_t c = forth_pop(vm);
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, b); forth_push(vm, c); forth_push(vm, a);
                break;
            }
            case FORTH_TOK_TO_R:       rpush(vm, forth_pop(vm)); break;
            case FORTH_TOK_R_FROM:     forth_push(vm, rpop(vm)); break;
            case FORTH_TOK_R_FETCH:
                if (vm->rsp >= 0) forth_push(vm, vm->rstack[vm->rsp]);
                break;
            case FORTH_TOK_PLUS: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, a + b);
                break;
            }
            case FORTH_TOK_MINUS: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, a - b);
                break;
            }
            case FORTH_TOK_MULT: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, a * b);
                break;
            }
            case FORTH_TOK_DIV: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                if (b != 0) forth_push(vm, a / b);
                else forth_abort(vm, "Div zero");
                break;
            }
            case FORTH_TOK_AND: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, a & b);
                break;
            }
            case FORTH_TOK_OR: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, a | b);
                break;
            }
            case FORTH_TOK_XOR: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, a ^ b);
                break;
            }
            case FORTH_TOK_INVERT:
                forth_push(vm, ~forth_pop(vm));
                break;
            case FORTH_TOK_ZERO_EQ:
                forth_push(vm, (forth_pop(vm) == 0) ? -1 : 0);
                break;
            case FORTH_TOK_EQ: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, (a == b) ? -1 : 0);
                break;
            }
            case FORTH_TOK_LT: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, (a < b) ? -1 : 0);
                break;
            }
            case FORTH_TOK_GT: {
                forth_cell_t b = forth_pop(vm);
                forth_cell_t a = forth_pop(vm);
                forth_push(vm, (a > b) ? -1 : 0);
                break;
            }
            case FORTH_TOK_FETCH: {
                forth_cell_t addr = forth_pop(vm);
                if (addr >= 0 && addr + (forth_cell_t)sizeof(forth_cell_t) <= FORTH_ARENA_MAX)
                    forth_push(vm, *(forth_cell_t *)&vm->arena[addr]);
                break;
            }
            case FORTH_TOK_STORE: {
                forth_cell_t addr = forth_pop(vm);
                forth_cell_t val  = forth_pop(vm);
                if (addr >= 0 && addr + (forth_cell_t)sizeof(forth_cell_t) <= FORTH_ARENA_MAX)
                    *(forth_cell_t *)&vm->arena[addr] = val;
                break;
            }
            case FORTH_TOK_C_FETCH: {
                forth_cell_t addr = forth_pop(vm);
                if (addr >= 0 && addr < FORTH_ARENA_MAX)
                    forth_push(vm, (forth_cell_t)vm->arena[addr]);
                break;
            }
            case FORTH_TOK_C_STORE: {
                forth_cell_t addr = forth_pop(vm);
                forth_cell_t val  = forth_pop(vm);
                if (addr >= 0 && addr < FORTH_ARENA_MAX)
                    vm->arena[addr] = (uint8_t)val;
                break;
            }
            case FORTH_TOK_EMIT:
                if (vm->emit_fn) vm->emit_fn((char)forth_pop(vm));
                break;
            case FORTH_TOK_CR:
                if (vm->emit_fn) vm->emit_fn('\n');
                break;
            case FORTH_TOK_BYE:
                vm->halted = true;
                return 0;
            case FORTH_TOK_WORDS:
                forth_words(vm);
                break;
            case FORTH_TOK_DEV_TREE:
                forth_dev_dump(vm);
                break;
            case FORTH_TOK_HELP:
                forth_help(vm);
                break;
            case FORTH_TOK_PWD:
                if (vm->emit_fn) {
                    const char *name = (vm->active_node && vm->active_node->name[0]) ? vm->active_node->name : "/";
                    if (strcmp(name, "/") != 0) vm->emit_fn('/');
                    for (const char *c = name; *c; c++) vm->emit_fn(*c);
                    vm->emit_fn('\n');
                }
                break;
            case FORTH_TOK_PARENT:
                if (vm->dsp >= 0 && is_valid_dev_node(vm, (FCodeNode *)(uintptr_t)vm->tos)) {
                    FCodeNode *n = (FCodeNode *)(uintptr_t)forth_pop(vm);
                    forth_push(vm, (forth_cell_t)(uintptr_t)(n->parent));
                } else {
                    if (vm->active_node && vm->active_node->parent) {
                        vm->active_node = vm->active_node->parent;
                    }
                    if (vm->emit_fn) {
                        const char *name = (vm->active_node && vm->active_node->name[0]) ? vm->active_node->name : "/";
                        if (strcmp(name, "/") != 0) vm->emit_fn('/');
                        for (const char *c = name; *c; c++) vm->emit_fn(*c);
                        vm->emit_fn('\n');
                    }
                }
                break;
            case FORTH_TOK_PEER: {
                if (vm->dsp >= 0 && is_valid_dev_node(vm, (FCodeNode *)(uintptr_t)vm->tos)) {
                    FCodeNode *n = (FCodeNode *)(uintptr_t)forth_pop(vm);
                    forth_push(vm, (forth_cell_t)(uintptr_t)(n->peer));
                } else {
                    forth_push(vm, (forth_cell_t)0);
                }
                break;
            }
            case FORTH_TOK_CHILD: {
                if (vm->dsp >= 0 && is_valid_dev_node(vm, (FCodeNode *)(uintptr_t)vm->tos)) {
                    FCodeNode *n = (FCodeNode *)(uintptr_t)forth_pop(vm);
                    forth_push(vm, (forth_cell_t)(uintptr_t)(n->child));
                } else {
                    forth_push(vm, (forth_cell_t)0);
                }
                break;
            }
            case FORTH_TOK_MY_SELF:
                forth_push(vm, (forth_cell_t)(uintptr_t)vm->active_node);
                break;
            default:
                forth_abort(vm, "Unknown token in switch");
                return -1;
        }
    }
    return vm->last_error;
#endif
}

/* ── Outer Interpreter & Evaluator ──────────────────────────────── */
int forth_eval(ForthVM *vm, const char *line) {
    if (!vm || !line) return 0;
    vm->halted = false;
    const char *p = line;

    while (*p && !vm->halted) {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;

        /* Extract token */
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        size_t tok_len = p - start;
        if (tok_len == 0) break;

        char tok[32];
        if (tok_len >= sizeof(tok)) tok_len = sizeof(tok) - 1;
        memcpy(tok, start, tok_len);
        tok[tok_len] = '\0';

        /* Colon definition */
        if (strcmp(tok, ":") == 0) {
            /* Read next token as word name */
            while (*p == ' ' || *p == '\t') p++;
            const char *wstart = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
            size_t wlen = p - wstart;
            if (wlen > 0) {
                char wname[32];
                if (wlen >= sizeof(wname)) wlen = sizeof(wname) - 1;
                memcpy(wname, wstart, wlen);
                wname[wlen] = '\0';
                dict_create_word(vm, wname, FORTH_TOK_ESCAPE, FORTH_FLAG_SMUDGE);
                vm->state = true;
            }
            continue;
        }

        if (strcmp(tok, ";") == 0) {
            /* Append EXIT and finish compilation */
            if (vm->here < FORTH_ARENA_MAX) {
                vm->arena[vm->here++] = FORTH_TOK_EXIT;
            }
            /* Un-smudge latest word */
            if (vm->latest > 0) {
                vm->arena[vm->latest + sizeof(forth_cell_t)] &= ~FORTH_FLAG_SMUDGE;
            }
            vm->state = false;
            continue;
        }

        /* Interactive OpenFirmware device-tree navigation: 'dev <path>' or 'cd <path>' */
        if (!vm->state && ((tok_len == 3 && forth_strncasecmp(tok, "dev", 3) == 0) ||
                           (tok_len == 2 && forth_strncasecmp(tok, "cd", 2) == 0))) {
            while (*p == ' ' || *p == '\t') p++;
            const char *pstart = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
            size_t plen = p - pstart;
            if (plen > 0) {
                char path[64];
                if (plen >= sizeof(path)) plen = sizeof(path) - 1;
                memcpy(path, pstart, plen);
                path[plen] = '\0';
                if (strcmp(path, "..") == 0) {
                    if (vm->active_node && vm->active_node->parent) {
                        vm->active_node = vm->active_node->parent;
                    }
                } else {
                    FCodeNode *node = forth_dev_find(vm, path);
                    if (node) {
                        vm->active_node = node;
                    } else {
                        char errmsg[96];
                        snprintf(errmsg, sizeof(errmsg), "? device '%s' not found", path);
                        forth_abort(vm, errmsg);
                        return -1;
                    }
                }
            }
            continue;
        }

        /* Check dictionary */
        uint8_t flags = 0;
        uint8_t token = 0;
        forth_cell_t cfa = dict_find(vm, tok, (uint8_t)tok_len, &flags, &token);

        if (cfa != 0) {
            bool is_imm = (flags & FORTH_FLAG_IMMEDIATE) != 0;
            if (vm->state && !is_imm) {
                /* Compile mode */
                if (token == FORTH_TOK_ESCAPE) {
                    forth_cell_t pfa = cfa + 1;
                    if (vm->here + 1 + sizeof(forth_cell_t) + 1 < FORTH_ARENA_MAX) {
                        vm->arena[vm->here++] = FORTH_TOK_LIT;
                        memcpy(&vm->arena[vm->here], &pfa, sizeof(forth_cell_t));
                        vm->here += sizeof(forth_cell_t);
                        vm->arena[vm->here++] = FORTH_TOK_EXECUTE;
                    }
                } else {
                    if (vm->here < FORTH_ARENA_MAX) {
                        vm->arena[vm->here++] = token;
                    } else {
                        forth_abort(vm, "Arena overflow compiling token");
                    }
                }
            } else {
                /* Execute mode */
                if (token == FORTH_TOK_ESCAPE) {
                    forth_cell_t pfa = cfa + 1;
                    forth_eval_fcode(vm, &vm->arena[pfa], (size_t)(vm->here - pfa));
                } else {
                    uint8_t single_tok = token;
                    forth_eval_fcode(vm, &single_tok, 1);
                }
            }
            continue;
        }

        /* Check for literal number */
        forth_cell_t num = 0;
        if (parse_number(tok, &num)) {
            if (vm->state) {
                /* Compile literal */
                if (vm->here + 1 + sizeof(forth_cell_t) < FORTH_ARENA_MAX) {
                    vm->arena[vm->here++] = FORTH_TOK_LIT;
                    memcpy(&vm->arena[vm->here], &num, sizeof(forth_cell_t));
                    vm->here += sizeof(forth_cell_t);
                }
            } else {
                forth_push(vm, num);
            }
            continue;
        }

        /* Dot operator for diagnostic print */
        if (strcmp(tok, ".") == 0) {
            forth_cell_t val = forth_pop(vm);
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld ", (long)val);
            for (char *c = buf; *c; c++) if (vm->emit_fn) vm->emit_fn(*c);
            continue;
        }

        /* Stack print diagnostic (.s) */
        if (strcmp(tok, ".s") == 0 || strcmp(tok, ".S") == 0) {
            if (vm->emit_fn) {
                vm->emit_fn('<');
                char numbuf[16];
                snprintf(numbuf, sizeof(numbuf), "%d> ", (int)(vm->dsp + 1));
                for (char *c = numbuf; *c; c++) vm->emit_fn(*c);
                for (forth_cell_t i = 0; i <= vm->dsp; i++) {
                    snprintf(numbuf, sizeof(numbuf), "%ld ", (long)vm->dstack[i]);
                    for (char *c = numbuf; *c; c++) vm->emit_fn(*c);
                }
                vm->emit_fn('\n');
            }
            continue;
        }

        /* Unknown word */
        char errmsg[64];
        snprintf(errmsg, sizeof(errmsg), "? %s", tok);
        forth_abort(vm, errmsg);
        return -1;
    }

    return vm->last_error;
}

/* ── VM Initialization ──────────────────────────────────────────── */
void forth_vm_init(ForthVM *vm) {
    if (!vm) return;
    memset(vm, 0, sizeof(*vm));

    vm->dsp = -1;
    vm->rsp = -1;
    vm->tos_valid = false;
    vm->halted = false;
    vm->state = false;
    vm->latest = 0;
    vm->here = (forth_cell_t)sizeof(forth_cell_t);

    vm->emit_fn = default_emit;
    vm->key_fn = default_key;
    vm->key_q_fn = default_key_q;

    /* Populate primitive dictionary */
    dict_create_word(vm, "DUP",        FORTH_TOK_DUP,        0);
    dict_create_word(vm, "DROP",       FORTH_TOK_DROP,       0);
    dict_create_word(vm, "SWAP",       FORTH_TOK_SWAP,       0);
    dict_create_word(vm, "OVER",       FORTH_TOK_OVER,       0);
    dict_create_word(vm, "ROT",        FORTH_TOK_ROT,        0);
    dict_create_word(vm, ">R",         FORTH_TOK_TO_R,       0);
    dict_create_word(vm, "R>",         FORTH_TOK_R_FROM,     0);
    dict_create_word(vm, "R@",         FORTH_TOK_R_FETCH,    0);
    dict_create_word(vm, "2>R",        FORTH_TOK_TWO_TO_R,   0);
    dict_create_word(vm, "2R>",        FORTH_TOK_TWO_R_FROM, 0);

    dict_create_word(vm, "+",          FORTH_TOK_PLUS,       0);
    dict_create_word(vm, "-",          FORTH_TOK_MINUS,      0);
    dict_create_word(vm, "*",          FORTH_TOK_MULT,       0);
    dict_create_word(vm, "/",          FORTH_TOK_DIV,        0);
    dict_create_word(vm, "MOD",        FORTH_TOK_MOD,        0);
    dict_create_word(vm, "NEGATE",     FORTH_TOK_NEGATE,     0);
    dict_create_word(vm, "AND",        FORTH_TOK_AND,        0);
    dict_create_word(vm, "OR",         FORTH_TOK_OR,         0);
    dict_create_word(vm, "XOR",        FORTH_TOK_XOR,        0);
    dict_create_word(vm, "INVERT",     FORTH_TOK_INVERT,     0);
    dict_create_word(vm, "LSHIFT",     FORTH_TOK_LSHIFT,     0);
    dict_create_word(vm, "RSHIFT",     FORTH_TOK_RSHIFT,     0);
    dict_create_word(vm, "0=",         FORTH_TOK_ZERO_EQ,    0);
    dict_create_word(vm, "0<",         FORTH_TOK_ZERO_LT,    0);
    dict_create_word(vm, "=",          FORTH_TOK_EQ,         0);
    dict_create_word(vm, "<",          FORTH_TOK_LT,         0);
    dict_create_word(vm, ">",          FORTH_TOK_GT,         0);
    dict_create_word(vm, "U<",         FORTH_TOK_U_LT,       0);

    dict_create_word(vm, "@",          FORTH_TOK_FETCH,      0);
    dict_create_word(vm, "!",          FORTH_TOK_STORE,      0);
    dict_create_word(vm, "C@",         FORTH_TOK_C_FETCH,    0);
    dict_create_word(vm, "C!",         FORTH_TOK_C_STORE,    0);
    dict_create_word(vm, "+!",         FORTH_TOK_PLUS_STORE, 0);
    dict_create_word(vm, "CELLS",      FORTH_TOK_CELLS,      0);
    dict_create_word(vm, "CELL+",      FORTH_TOK_CELL_PLUS,  0);
    dict_create_word(vm, "CHARS",      FORTH_TOK_CHARS,      0);
    dict_create_word(vm, "CHAR+",      FORTH_TOK_CHAR_PLUS,  0);
    dict_create_word(vm, "ALLOT",      FORTH_TOK_ALLOT,      0);

    dict_create_word(vm, "EXECUTE",    FORTH_TOK_EXECUTE,    0);
    dict_create_word(vm, "EXIT",       FORTH_TOK_EXIT,       0);
    dict_create_word(vm, "EMIT",       FORTH_TOK_EMIT,       0);
    dict_create_word(vm, "KEY",        FORTH_TOK_KEY,        0);
    dict_create_word(vm, "KEY?",       FORTH_TOK_KEY_Q,      0);
    dict_create_word(vm, "TYPE",       FORTH_TOK_TYPE,       0);
    dict_create_word(vm, "CR",         FORTH_TOK_CR,         0);
    dict_create_word(vm, "BYE",        FORTH_TOK_BYE,        0);
    dict_create_word(vm, "WORDS",      FORTH_TOK_WORDS,      0);
    dict_create_word(vm, "DEV-TREE",   FORTH_TOK_DEV_TREE,   0);
    dict_create_word(vm, "LS",         FORTH_TOK_DEV_TREE,   0);
    dict_create_word(vm, "PWD",        FORTH_TOK_PWD,        0);
    dict_create_word(vm, "HELP",       FORTH_TOK_HELP,       0);
    dict_create_word(vm, "?",          FORTH_TOK_HELP,       0);
    dict_create_word(vm, "INFO",       FORTH_TOK_HELP,       0);

    dict_create_word(vm, "find-device", FORTH_TOK_FIND_DEV,   0);
    dict_create_word(vm, "peer",        FORTH_TOK_PEER,       0);
    dict_create_word(vm, "child",       FORTH_TOK_CHILD,      0);
    dict_create_word(vm, "parent",      FORTH_TOK_PARENT,     0);
    dict_create_word(vm, "get-property",FORTH_TOK_GET_PROP,   0);
    dict_create_word(vm, "set-property",FORTH_TOK_SET_PROP,   0);
    dict_create_word(vm, "my-self",     FORTH_TOK_MY_SELF,    0);
    dict_create_word(vm, "byte-load",   FORTH_TOK_BYTE_LOAD,  0);

    /* Create OpenFirmware root device node */
    FCodeNode *root = forth_dev_create(vm, "/", NULL);
    vm->active_node = root;
}
