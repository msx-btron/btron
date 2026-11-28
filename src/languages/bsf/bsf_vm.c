/*
 * B-System (BTRON 3.20) — src/languages/bsf/bsf_vm.c
 * B-Sound Forth (BSF) Compiler & Frozen-Token Real-Time Evaluator
 * Pure C99, zero heap allocation, adheres to NASA/JPL Power of Ten rules.
 */

#include <btron/bsf.h>
#include <btron/libc_shim.h>
#include <string.h>
#include <stdio.h>

/* Weak VFS hook so standalone tests link cleanly without full VFS */
__attribute__((weak)) int vfs_read_file(const char *path, char *buf, size_t max_bytes, size_t *out_bytes) {
    (void)path; (void)buf; (void)max_bytes; (void)out_bytes;
    return -1;
}

/* Internal DSP hooks implemented in bsf_dsp.c */
extern void bsf_dsp_voice_set(int idx, fix16_t y, int mode);
extern void bsf_dsp_voice_freq(int idx, fix16_t y);
extern void bsf_dsp_voice_mode(int idx, int mode);
extern void bsf_dsp_voice_amp(int idx, fix16_t amp);
extern void bsf_dsp_voice_pan(int idx, fix16_t pan);
extern void bsf_dsp_voice_on(int idx);
extern void bsf_dsp_voice_off(int idx);
extern void bsf_dsp_voice_filter(int idx, int type, fix16_t cutoff_hz, fix16_t q);
extern void bsf_dsp_voice_fm(int idx, fix16_t ratio, fix16_t index);
extern void bsf_dsp_voice_sat(int idx, fix16_t k);
extern void bsf_dsp_voice_lfo(int idx, fix16_t rate_hz, fix16_t depth_oct);
extern void bsf_dsp_voice_am(int idx, fix16_t rate_hz, fix16_t depth);
extern void bsf_dsp_set_reverb(fix16_t mix);
extern void bsf_dsp_set_echo(fix16_t delay_ms, fix16_t feedback, fix16_t mix);
extern void bsf_dsp_voice_echo(int idx, fix16_t delay_ms, fix16_t feedback, fix16_t mix);
extern void bsf_dsp_set_voice_count(int n);
extern void bsf_dsp_y_out(fix16_t y);
extern fix16_t bsf_dsp_get_x(void);
extern int32_t bsf_dsp_get_part(void);
extern int32_t bsf_dsp_get_samples(void);
extern void bsf_dsp_set_error(const char *msg);

/* ── Internal Opcodes ────────────────────────────────────────────── */
enum BsfOp {
    OP_NOP = 0,
    OP_EXIT,
    OP_LIT,
    OP_CALL,
    OP_BRANCH,
    OP_QBRANCH,
    OP_DO,
    OP_LOOP,
    OP_PLOOP,
    OP_I,
    OP_J,
    OP_VAR_ADDR,

    /* Stack */
    OP_DUP, OP_DROP, OP_SWAP, OP_OVER, OP_ROT, OP_NIP, OP_TUCK,
    OP_2DUP, OP_2DROP, OP_PICK, OP_TO_R, OP_R_FROM, OP_R_FETCH,

    /* Memory */
    OP_FETCH, OP_STORE, OP_PLUS_STORE,

    /* Arithmetic & Logic */
    OP_ADD, OP_SUB, OP_FMUL, OP_FDIV, OP_IMUL, OP_IDIV, OP_STAR_SLASH,
    OP_FMOD, OP_IMOD, OP_NEGATE, OP_ABS, OP_MIN, OP_MAX, OP_CLAMP, OP_CLAMP01,
    OP_SMOOTHSTEP, OP_2MUL, OP_2DIV, OP_LSHIFT, OP_RSHIFT,
    OP_AND, OP_OR, OP_XOR, OP_INVERT,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE, OP_ZEQ, OP_ZLT, OP_ZGT,
    OP_F16, OP_TO_INT, OP_QUIET, OP_PI, OP_2PI,

    /* Transcendentals & Analytical */
    OP_SIN, OP_COS, OP_EXP, OP_LOG2, OP_POW2, OP_TANH,
    OP_FRAC, OP_FLOOR, OP_BIT, OP_DIGIT, OP_DIAT,
    OP_HZ_TO_Y, OP_Y_TO_HZ, OP_NOTE_TO_Y, OP_SAT, OP_EV_SEG,
    OP_EV_RESET, OP_EV, OP_EV_SLIDE, OP_EV_MIDI, OP_EV_FETCH,

    /* DSP & Voice Control */
    OP_VOICE_SET, OP_VOICE_FREQ, OP_VOICE_MODE, OP_VOICE_AMP, OP_VOICE_PAN,
    OP_VOICE_ON, OP_VOICE_OFF, OP_VOICE_FILTER, OP_VOICE_FM, OP_VOICE_SAT,
    OP_VOICE_LFO, OP_VOICE_AM, OP_REVERB, OP_ECHO, OP_VECHO, OP_VOICES_N, OP_Y_OUT,
    OP_X_FETCH, OP_PART_FETCH, OP_SAMPLES_FETCH
};

typedef struct {
    char     name[32];
    uint8_t  kind;     /* 0=prim, 1=colon, 2=const, 3=var */
    int32_t  val;      /* opcode, code offset, const value, or var index */
} BsfWord;

typedef struct {
    int32_t  dstack[BSF_DSTACK_MAX];
    int32_t  rstack[BSF_RSTACK_MAX];
    int32_t  dsp;
    int32_t  rsp;

    BsfWord  dict[BSF_DICT_MAX];
    int32_t  dict_count;
    int32_t  code[BSF_CODE_MAX];
    int32_t  code_len;
    int32_t  vars[BSF_VARS_MAX];
    int32_t  var_count;

    /* Base watermark saved after primitives + shims are loaded */
    int32_t  base_dict_count;
    int32_t  base_code_len;
    int32_t  base_var_count;

    /* Compiler state */
    bool     compiling;
    int32_t  cstack[BSF_CSTACK_MAX];
    int32_t  csp;
    int32_t  case_end_starts[16];
    int32_t  case_depth;
    int32_t  case_end_patches[64];
    int32_t  case_end_count;

    /* Resolved track entry point */
    int32_t  track_eval_word;
    fix16_t  ev_acc;
    bool     halted;
} BsfVm;

static BsfVm s_vm;

static int bsf_strcasecmp_local(const char *a, const char *b) {
    for (int i = 0; i < 64; i++) {
        char c1 = a[i], c2 = b[i];
        if (c1 >= 'a' && c1 <= 'z') c1 = (char)(c1 - 32);
        if (c2 >= 'a' && c2 <= 'z') c2 = (char)(c2 - 32);
        if (c1 != c2 || c1 == '\0') return (int)(unsigned char)c1 - (int)(unsigned char)c2;
    }
    return 0;
}

static void vm_add_prim(const char *name, int32_t op) {
    if (s_vm.dict_count >= BSF_DICT_MAX) return;
    BsfWord *w = &s_vm.dict[s_vm.dict_count++];
    strncpy(w->name, name, sizeof(w->name) - 1);
    w->name[sizeof(w->name) - 1] = '\0';
    w->kind = 0;
    w->val = op;
}

static int vm_find_word(const char *name) {
    for (int i = s_vm.dict_count - 1; i >= 0; i--) {
        if (bsf_strcasecmp_local(s_vm.dict[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool vm_emit(int32_t cell) {
    if (s_vm.code_len >= BSF_CODE_MAX) {
        bsf_dsp_set_error("BSF code arena full");
        return false;
    }
    s_vm.code[s_vm.code_len++] = cell;
    return true;
}

static inline void vm_push(int32_t v) {
    if (s_vm.dsp + 1 >= BSF_DSTACK_MAX) {
        s_vm.halted = true;
        bsf_dsp_set_error("BSF data stack overflow");
        return;
    }
    s_vm.dstack[++s_vm.dsp] = v;
}

static inline int32_t vm_pop(void) {
    if (s_vm.dsp < 0) {
        s_vm.halted = true;
        bsf_dsp_set_error("BSF data stack underflow");
        return 0;
    }
    return s_vm.dstack[s_vm.dsp--];
}

static inline void vm_rpush(int32_t v) {
    if (s_vm.rsp + 1 >= BSF_RSTACK_MAX) {
        s_vm.halted = true;
        bsf_dsp_set_error("BSF return stack overflow");
        return;
    }
    s_vm.rstack[++s_vm.rsp] = v;
}

static inline int32_t vm_rpop(void) {
    if (s_vm.rsp < 0) {
        s_vm.halted = true;
        bsf_dsp_set_error("BSF return stack underflow");
        return 0;
    }
    return s_vm.rstack[s_vm.rsp--];
}

/* Parse integer or decimal fixed-point literal (e.g. "42", "-3", "0.16", "-2.5") */
static bool vm_parse_number(const char *tok, int32_t *out_val) {
    if (!tok || !tok[0]) return false;
    int i = 0;
    bool neg = false;
    if (tok[i] == '-') { neg = true; i++; }
    else if (tok[i] == '+') { i++; }
    if (tok[i] == '\0') return false;

    /* Hex literal 0x... */
    if (tok[i] == '0' && (tok[i + 1] == 'x' || tok[i + 1] == 'X')) {
        i += 2;
        if (tok[i] == '\0') return false;
        uint32_t uv = 0;
        for (; tok[i] != '\0'; i++) {
            char c = tok[i];
            uint32_t d;
            if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
            else return false;
            uv = (uv << 4) | d;
        }
        *out_val = neg ? -(int32_t)uv : (int32_t)uv;
        return true;
    }

    int64_t int_part = 0;
    bool has_digit = false;
    while (tok[i] >= '0' && tok[i] <= '9') {
        has_digit = true;
        int_part = int_part * 10 + (tok[i] - '0');
        i++;
    }

    if (tok[i] == '\0') {
        if (!has_digit) return false;
        *out_val = neg ? (int32_t)(-int_part) : (int32_t)int_part;
        return true;
    }

    if (tok[i] == '.') {
        i++;
        int64_t frac_num = 0;
        int64_t frac_den = 1;
        while (tok[i] >= '0' && tok[i] <= '9') {
            has_digit = true;
            if (frac_den < 1000000LL) {
                frac_num = frac_num * 10 + (tok[i] - '0');
                frac_den *= 10;
            }
            i++;
        }
        if (tok[i] != '\0' || !has_digit) return false;
        int64_t q = (int_part << 16) + ((frac_num << 16) + (frac_den / 2)) / frac_den;
        if (neg) q = -q;
        *out_val = (int32_t)q;
        return true;
    }

    return false;
}

/* ── Frozen-Token Inner Interpreter (Real-Time Audio Plane) ─────── */
static int vm_exec_from(int32_t start_pc) {
    int32_t pc = start_pc;
    s_vm.halted = false;
    int32_t base_rsp = s_vm.rsp;

    for (int32_t step = 0; step < BSF_MAX_STEPS && !s_vm.halted; step++) {
        if (pc < 0 || pc >= s_vm.code_len) {
            bsf_dsp_set_error("BSF PC out of bounds");
            s_vm.halted = true;
            return -1;
        }
        int32_t op = s_vm.code[pc++];
        switch (op) {
            case OP_NOP: break;
            case OP_EXIT:
                if (s_vm.rsp <= base_rsp) return 0;
                pc = vm_rpop();
                break;
            case OP_LIT:
                vm_push(s_vm.code[pc++]);
                break;
            case OP_CALL: {
                int32_t target = s_vm.code[pc++];
                vm_rpush(pc);
                pc = target;
                break;
            }
            case OP_BRANCH:
                pc = s_vm.code[pc];
                break;
            case OP_QBRANCH: {
                int32_t target = s_vm.code[pc++];
                int32_t flag = vm_pop();
                if (flag == 0) pc = target;
                break;
            }
            case OP_DO: {
                int32_t idx = vm_pop();
                int32_t lim = vm_pop();
                vm_rpush(lim);
                vm_rpush(idx);
                break;
            }
            case OP_LOOP: {
                int32_t target = s_vm.code[pc++];
                int32_t idx = vm_rpop() + 1;
                int32_t lim = vm_rpop();
                if (idx < lim) {
                    vm_rpush(lim);
                    vm_rpush(idx);
                    pc = target;
                }
                break;
            }
            case OP_PLOOP: {
                int32_t target = s_vm.code[pc++];
                int32_t inc = vm_pop();
                int32_t idx = vm_rpop() + inc;
                int32_t lim = vm_rpop();
                if ((inc >= 0 && idx < lim) || (inc < 0 && idx > lim)) {
                    vm_rpush(lim);
                    vm_rpush(idx);
                    pc = target;
                }
                break;
            }
            case OP_I:
                if (s_vm.rsp >= 0) vm_push(s_vm.rstack[s_vm.rsp]);
                else vm_push(0);
                break;
            case OP_J:
                if (s_vm.rsp >= 2) vm_push(s_vm.rstack[s_vm.rsp - 2]);
                else vm_push(0);
                break;
            case OP_VAR_ADDR:
                vm_push(s_vm.code[pc++]);
                break;

            /* Stack */
            case OP_DUP: { int32_t a = vm_pop(); vm_push(a); vm_push(a); break; }
            case OP_DROP: (void)vm_pop(); break;
            case OP_SWAP: { int32_t b = vm_pop(), a = vm_pop(); vm_push(b); vm_push(a); break; }
            case OP_OVER: {
                if (s_vm.dsp < 1) { s_vm.halted = true; break; }
                vm_push(s_vm.dstack[s_vm.dsp - 1]);
                break;
            }
            case OP_ROT: {
                int32_t c = vm_pop(), b = vm_pop(), a = vm_pop();
                vm_push(b); vm_push(c); vm_push(a);
                break;
            }
            case OP_NIP: { int32_t b = vm_pop(); (void)vm_pop(); vm_push(b); break; }
            case OP_TUCK: { int32_t b = vm_pop(), a = vm_pop(); vm_push(b); vm_push(a); vm_push(b); break; }
            case OP_2DUP: {
                if (s_vm.dsp < 1) { s_vm.halted = true; break; }
                int32_t a = s_vm.dstack[s_vm.dsp - 1], b = s_vm.dstack[s_vm.dsp];
                vm_push(a); vm_push(b);
                break;
            }
            case OP_2DROP: (void)vm_pop(); (void)vm_pop(); break;
            case OP_PICK: {
                int32_t n = vm_pop();
                if (n < 0 || n > s_vm.dsp) { vm_push(0); }
                else { vm_push(s_vm.dstack[s_vm.dsp - n]); }
                break;
            }
            case OP_TO_R: vm_rpush(vm_pop()); break;
            case OP_R_FROM: vm_push(vm_rpop()); break;
            case OP_R_FETCH:
                vm_push((s_vm.rsp >= 0) ? s_vm.rstack[s_vm.rsp] : 0);
                break;

            /* Memory */
            case OP_FETCH: {
                int32_t a = vm_pop();
                vm_push((a >= 0 && a < BSF_VARS_MAX) ? s_vm.vars[a] : 0);
                break;
            }
            case OP_STORE: {
                int32_t a = vm_pop(), v = vm_pop();
                if (a >= 0 && a < BSF_VARS_MAX) s_vm.vars[a] = v;
                break;
            }
            case OP_PLUS_STORE: {
                int32_t a = vm_pop(), v = vm_pop();
                if (a >= 0 && a < BSF_VARS_MAX) s_vm.vars[a] += v;
                break;
            }

            /* Arithmetic & Logic */
            case OP_ADD: { int32_t b = vm_pop(), a = vm_pop(); vm_push(a + b); break; }
            case OP_SUB: { int32_t b = vm_pop(), a = vm_pop(); vm_push(a - b); break; }
            case OP_FMUL: { int32_t b = vm_pop(), a = vm_pop(); vm_push(fix16_mul(a, b)); break; }
            case OP_FDIV: { int32_t b = vm_pop(), a = vm_pop(); vm_push(fix16_div(a, b)); break; }
            case OP_IMUL: { int32_t b = vm_pop(), a = vm_pop(); vm_push(a * b); break; }
            case OP_IDIV: { int32_t b = vm_pop(), a = vm_pop(); vm_push(b ? (a / b) : 0); break; }
            case OP_STAR_SLASH: {
                int32_t c = vm_pop(), b = vm_pop(), a = vm_pop();
                vm_push(c ? (int32_t)(((int64_t)a * (int64_t)b) / (int64_t)c) : 0);
                break;
            }
            case OP_FMOD: { int32_t b = vm_pop(), a = vm_pop(); vm_push(fix16_mod(a, b)); break; }
            case OP_IMOD: {
                int32_t b = vm_pop(), a = vm_pop();
                if (b <= 0) { vm_push(0); break; }
                int32_t r = a % b;
                if (r < 0) r += b;
                vm_push(r);
                break;
            }
            case OP_NEGATE: vm_push(-vm_pop()); break;
            case OP_ABS: vm_push(fix16_abs(vm_pop())); break;
            case OP_MIN: { int32_t b = vm_pop(), a = vm_pop(); vm_push(fix16_min(a, b)); break; }
            case OP_MAX: { int32_t b = vm_pop(), a = vm_pop(); vm_push(fix16_max(a, b)); break; }
            case OP_CLAMP: {
                int32_t hi = vm_pop(), lo = vm_pop(), x = vm_pop();
                vm_push(fix16_clamp(x, lo, hi));
                break;
            }
            case OP_CLAMP01: vm_push(fix16_clamp(vm_pop(), 0, FIX16_ONE)); break;
            case OP_SMOOTHSTEP: vm_push(fix16_smoothstep(vm_pop())); break;
            case OP_2MUL: vm_push(vm_pop() << 1); break;
            case OP_2DIV: vm_push(vm_pop() >> 1); break;
            case OP_LSHIFT: { int32_t s = vm_pop(), a = vm_pop(); vm_push((int32_t)((uint32_t)a << (s & 31))); break; }
            case OP_RSHIFT: { int32_t s = vm_pop(), a = vm_pop(); vm_push((int32_t)((uint32_t)a >> (s & 31))); break; }
            case OP_AND: { int32_t b = vm_pop(), a = vm_pop(); vm_push(a & b); break; }
            case OP_OR:  { int32_t b = vm_pop(), a = vm_pop(); vm_push(a | b); break; }
            case OP_XOR: { int32_t b = vm_pop(), a = vm_pop(); vm_push(a ^ b); break; }
            case OP_INVERT: vm_push(~vm_pop()); break;
            case OP_EQ:  { int32_t b = vm_pop(), a = vm_pop(); vm_push(a == b ? -1 : 0); break; }
            case OP_NEQ: { int32_t b = vm_pop(), a = vm_pop(); vm_push(a != b ? -1 : 0); break; }
            case OP_LT:  { int32_t b = vm_pop(), a = vm_pop(); vm_push(a < b ? -1 : 0); break; }
            case OP_GT:  { int32_t b = vm_pop(), a = vm_pop(); vm_push(a > b ? -1 : 0); break; }
            case OP_LE:  { int32_t b = vm_pop(), a = vm_pop(); vm_push(a <= b ? -1 : 0); break; }
            case OP_GE:  { int32_t b = vm_pop(), a = vm_pop(); vm_push(a >= b ? -1 : 0); break; }
            case OP_ZEQ: vm_push(vm_pop() == 0 ? -1 : 0); break;
            case OP_ZLT: vm_push(vm_pop() < 0 ? -1 : 0); break;
            case OP_ZGT: vm_push(vm_pop() > 0 ? -1 : 0); break;
            case OP_F16: vm_push(fix16_from_int(vm_pop())); break;
            case OP_TO_INT: vm_push(fix16_to_int(vm_pop())); break;
            case OP_QUIET: vm_push(FIX16_QUIET); break;
            case OP_PI: vm_push(FIX16_PI); break;
            case OP_2PI: vm_push(FIX16_TWO_PI); break;

            /* Transcendentals & Analytical */
            case OP_SIN: vm_push(fix16_sin(vm_pop())); break;
            case OP_COS: vm_push(fix16_cos(vm_pop())); break;
            case OP_EXP: vm_push(fix16_exp(vm_pop())); break;
            case OP_LOG2: vm_push(fix16_log2(vm_pop())); break;
            case OP_POW2: vm_push(fix16_pow2(vm_pop())); break;
            case OP_TANH: vm_push(fix16_tanh(vm_pop())); break;
            case OP_FRAC: vm_push(fix16_frac(vm_pop())); break;
            case OP_FLOOR: vm_push(fix16_floor(vm_pop())); break;
            case OP_BIT: {
                int32_t k = vm_pop(), m = vm_pop();
                vm_push(fix16_bit((uint32_t)m, k));
                break;
            }
            case OP_DIGIT: {
                int32_t idx = vm_pop(), num = vm_pop();
                vm_push(fix16_digit((uint32_t)num, idx));
                break;
            }
            case OP_DIAT: vm_push(fix16_diat(vm_pop())); break;
            case OP_HZ_TO_Y: vm_push(fix16_hz_to_y(vm_pop())); break;
            case OP_Y_TO_HZ: vm_push(fix16_y_to_hz(vm_pop())); break;
            case OP_NOTE_TO_Y: {
                int32_t midi = vm_pop();
                /* (midi - 60) / 12 in Q16.16 */
                vm_push(fix16_div(fix16_from_int(midi - 60), fix16_from_int(12)));
                break;
            }
            case OP_SAT: {
                int32_t k = vm_pop(), x = vm_pop();
                vm_push(fix16_waveshaper(x, k));
                break;
            }
            case OP_EV_SEG: {
                /* ( t t0 f0 dur f1 -- y ) */
                fix16_t f1  = vm_pop();
                fix16_t dur = vm_pop();
                fix16_t f0  = vm_pop();
                fix16_t t0  = vm_pop();
                fix16_t t   = vm_pop();
                if (t < t0 || t >= t0 + dur || dur <= 0 || f0 <= 0) {
                    vm_push(FIX16_QUIET);
                } else if (f1 <= 0 || f1 == f0) {
                    vm_push(fix16_clamp(fix16_hz_to_y(f0), fix16_from_int(-3), fix16_from_int(3)));
                } else {
                    fix16_t p = fix16_clamp(fix16_div(t - t0, fix16_mul(dur, 49152 /* 0.75 */)), 0, FIX16_ONE);
                    fix16_t y0 = fix16_hz_to_y(f0);
                    fix16_t y1 = fix16_hz_to_y(f1);
                    fix16_t y = y0 + fix16_mul(y1 - y0, p);
                    vm_push(fix16_clamp(y, fix16_from_int(-3), fix16_from_int(3)));
                }
                break;
            }

            case OP_EV_RESET:
                s_vm.ev_acc = FIX16_QUIET;
                break;
            case OP_EV: {
                /* ( t t0 f0 dur -- ) */
                fix16_t dur = vm_pop();
                fix16_t f0  = vm_pop();
                fix16_t t0  = vm_pop();
                fix16_t t   = vm_pop();
                if (s_vm.ev_acc == FIX16_QUIET && t >= t0 && t < t0 + dur && f0 > 0) {
                    s_vm.ev_acc = fix16_clamp(fix16_hz_to_y(f0), fix16_from_int(-3), fix16_from_int(3));
                }
                break;
            }
            case OP_EV_SLIDE: {
                /* ( t t0 f0 dur f1 -- ) */
                fix16_t f1  = vm_pop();
                fix16_t dur = vm_pop();
                fix16_t f0  = vm_pop();
                fix16_t t0  = vm_pop();
                fix16_t t   = vm_pop();
                if (s_vm.ev_acc == FIX16_QUIET && t >= t0 && t < t0 + dur && dur > 0 && f0 > 0) {
                    if (f1 <= 0 || f1 == f0) {
                        s_vm.ev_acc = fix16_clamp(fix16_hz_to_y(f0), fix16_from_int(-3), fix16_from_int(3));
                    } else {
                        fix16_t p = fix16_clamp(fix16_div(t - t0, fix16_mul(dur, 49152)), 0, FIX16_ONE);
                        fix16_t y0 = fix16_hz_to_y(f0);
                        fix16_t y1 = fix16_hz_to_y(f1);
                        s_vm.ev_acc = fix16_clamp(y0 + fix16_mul(y1 - y0, p), fix16_from_int(-3), fix16_from_int(3));
                    }
                }
                break;
            }
            case OP_EV_MIDI: {
                /* ( t t0 midi_int dur -- ) */
                fix16_t dur  = vm_pop();
                int32_t midi = vm_pop();
                fix16_t t0   = vm_pop();
                fix16_t t    = vm_pop();
                if (s_vm.ev_acc == FIX16_QUIET && t >= t0 && t < t0 + dur) {
                    fix16_t y = fix16_div(fix16_from_int(midi - 60), fix16_from_int(12));
                    s_vm.ev_acc = fix16_clamp(y, fix16_from_int(-3), fix16_from_int(3));
                }
                break;
            }
            case OP_EV_FETCH:
                vm_push(s_vm.ev_acc);
                break;

            /* DSP & Voice Control */
            case OP_VOICE_SET: {
                int32_t idx = vm_pop(), mode = vm_pop(), y = vm_pop();
                bsf_dsp_voice_set(idx, y, mode);
                break;
            }
            case OP_VOICE_FREQ: { int32_t idx = vm_pop(), y = vm_pop(); bsf_dsp_voice_freq(idx, y); break; }
            case OP_VOICE_MODE: { int32_t idx = vm_pop(), m = vm_pop(); bsf_dsp_voice_mode(idx, m); break; }
            case OP_VOICE_AMP:  { int32_t idx = vm_pop(), a = vm_pop(); bsf_dsp_voice_amp(idx, a); break; }
            case OP_VOICE_PAN:  { int32_t idx = vm_pop(), p = vm_pop(); bsf_dsp_voice_pan(idx, p); break; }
            case OP_VOICE_ON:   bsf_dsp_voice_on(vm_pop()); break;
            case OP_VOICE_OFF:  bsf_dsp_voice_off(vm_pop()); break;
            case OP_VOICE_FILTER: {
                int32_t idx = vm_pop(), q = vm_pop(), cut = vm_pop(), tp = vm_pop();
                bsf_dsp_voice_filter(idx, tp, cut, q);
                break;
            }
            case OP_VOICE_FM: {
                int32_t idx = vm_pop(), ind = vm_pop(), rat = vm_pop();
                bsf_dsp_voice_fm(idx, rat, ind);
                break;
            }
            case OP_VOICE_SAT: { int32_t idx = vm_pop(), k = vm_pop(); bsf_dsp_voice_sat(idx, k); break; }
            case OP_VOICE_LFO: {
                int32_t idx = vm_pop(), dep = vm_pop(), rat = vm_pop();
                bsf_dsp_voice_lfo(idx, rat, dep);
                break;
            }
            case OP_VOICE_AM: {
                int32_t idx = vm_pop(), dep = vm_pop(), rat = vm_pop();
                bsf_dsp_voice_am(idx, rat, dep);
                break;
            }
            case OP_REVERB:   bsf_dsp_set_reverb(vm_pop()); break;
            case OP_ECHO: {
                int32_t mix = vm_pop(), fb = vm_pop(), dms = vm_pop();
                bsf_dsp_set_echo(dms, fb, mix);
                break;
            }
            case OP_VECHO: {
                int32_t idx = vm_pop(), mix = vm_pop(), fb = vm_pop(), dms = vm_pop();
                bsf_dsp_voice_echo(idx, dms, fb, mix);
                break;
            }
            case OP_VOICES_N: bsf_dsp_set_voice_count(vm_pop()); break;
            case OP_Y_OUT:    bsf_dsp_y_out(vm_pop()); break;
            case OP_X_FETCH:  vm_push(bsf_dsp_get_x()); break;
            case OP_PART_FETCH: vm_push(bsf_dsp_get_part()); break;
            case OP_SAMPLES_FETCH: vm_push(bsf_dsp_get_samples()); break;

            default:
                bsf_dsp_set_error("BSF unknown opcode");
                s_vm.halted = true;
                return -1;
        }
    }
    return s_vm.halted ? -1 : 0;
}

/* ── VM Initialization & Dictionary Watermark ────────────────────── */
void bsf_vm_reset_all(void) {
    memset(&s_vm, 0, sizeof(s_vm));
    s_vm.dsp = -1;
    s_vm.rsp = -1;
    s_vm.csp = -1;
    s_vm.track_eval_word = -1;

    /* Stack */
    vm_add_prim("DUP", OP_DUP);         vm_add_prim("DROP", OP_DROP);
    vm_add_prim("SWAP", OP_SWAP);       vm_add_prim("OVER", OP_OVER);
    vm_add_prim("ROT", OP_ROT);         vm_add_prim("NIP", OP_NIP);
    vm_add_prim("TUCK", OP_TUCK);       vm_add_prim("2DUP", OP_2DUP);
    vm_add_prim("2DROP", OP_2DROP);     vm_add_prim("PICK", OP_PICK);
    vm_add_prim(">R", OP_TO_R);         vm_add_prim("R>", OP_R_FROM);
    vm_add_prim("R@", OP_R_FETCH);      vm_add_prim("I", OP_I);
    vm_add_prim("J", OP_J);             vm_add_prim("EXIT", OP_EXIT);

    /* Memory */
    vm_add_prim("@", OP_FETCH);         vm_add_prim("!", OP_STORE);
    vm_add_prim("+!", OP_PLUS_STORE);

    /* Arithmetic & Logic */
    vm_add_prim("+", OP_ADD);           vm_add_prim("-", OP_SUB);
    vm_add_prim("*", OP_FMUL);          vm_add_prim("/", OP_FDIV);
    vm_add_prim("F*", OP_FMUL);         vm_add_prim("F/", OP_FDIV);
    vm_add_prim("I*", OP_IMUL);         vm_add_prim("I/", OP_IDIV);
    vm_add_prim("*/", OP_STAR_SLASH);   vm_add_prim("MOD", OP_FMOD);
    vm_add_prim("IMOD", OP_IMOD);       vm_add_prim("NEGATE", OP_NEGATE);
    vm_add_prim("ABS", OP_ABS);         vm_add_prim("MIN", OP_MIN);
    vm_add_prim("MAX", OP_MAX);         vm_add_prim("CLAMP", OP_CLAMP);
    vm_add_prim("CLAMP01", OP_CLAMP01); vm_add_prim("SMOOTHSTEP", OP_SMOOTHSTEP);
    vm_add_prim("2*", OP_2MUL);         vm_add_prim("2/", OP_2DIV);
    vm_add_prim("LSHIFT", OP_LSHIFT);   vm_add_prim("RSHIFT", OP_RSHIFT);
    vm_add_prim("AND", OP_AND);         vm_add_prim("OR", OP_OR);
    vm_add_prim("XOR", OP_XOR);         vm_add_prim("INVERT", OP_INVERT);
    vm_add_prim("=", OP_EQ);            vm_add_prim("<>", OP_NEQ);
    vm_add_prim("<", OP_LT);            vm_add_prim(">", OP_GT);
    vm_add_prim("<=", OP_LE);           vm_add_prim(">=", OP_GE);
    vm_add_prim("0=", OP_ZEQ);          vm_add_prim("0<", OP_ZLT);
    vm_add_prim("0>", OP_ZGT);          vm_add_prim("F16", OP_F16);
    vm_add_prim(">INT", OP_TO_INT);     vm_add_prim("QUIET", OP_QUIET);
    vm_add_prim("PI", OP_PI);           vm_add_prim("2PI", OP_2PI);

    /* Transcendentals & Analytical */
    vm_add_prim("SIN", OP_SIN);         vm_add_prim("COS", OP_COS);
    vm_add_prim("EXP", OP_EXP);         vm_add_prim("LOG2", OP_LOG2);
    vm_add_prim("POW2", OP_POW2);       vm_add_prim("TANH", OP_TANH);
    vm_add_prim("FRAC", OP_FRAC);       vm_add_prim("FLOOR", OP_FLOOR);
    vm_add_prim("BIT", OP_BIT);         vm_add_prim("DIGIT", OP_DIGIT);
    vm_add_prim("DIAT", OP_DIAT);       vm_add_prim("HZ>Y", OP_HZ_TO_Y);
    vm_add_prim("Y>HZ", OP_Y_TO_HZ);    vm_add_prim("NOTE>Y", OP_NOTE_TO_Y);
    vm_add_prim("SAT", OP_SAT);         vm_add_prim("EV-SEG", OP_EV_SEG);
    vm_add_prim("EV-RESET", OP_EV_RESET);
    vm_add_prim("EV", OP_EV);
    vm_add_prim("EV-SLIDE", OP_EV_SLIDE);
    vm_add_prim("EV-MIDI", OP_EV_MIDI);
    vm_add_prim("EV@", OP_EV_FETCH);

    /* DSP & Voices */
    vm_add_prim("VOICE-SET", OP_VOICE_SET);
    vm_add_prim("VOICE-FREQ!", OP_VOICE_FREQ);
    vm_add_prim("VOICE-MODE!", OP_VOICE_MODE);
    vm_add_prim("VOICE-AMP!", OP_VOICE_AMP);
    vm_add_prim("VOICE-PAN!", OP_VOICE_PAN);
    vm_add_prim("VOICE-ON", OP_VOICE_ON);
    vm_add_prim("VOICE-OFF", OP_VOICE_OFF);
    vm_add_prim("VOICE-FILTER!", OP_VOICE_FILTER);
    vm_add_prim("VOICE-FM!", OP_VOICE_FM);
    vm_add_prim("VOICE-SAT!", OP_VOICE_SAT);
    vm_add_prim("VOICE-LFO!", OP_VOICE_LFO);
    vm_add_prim("VOICE-AM!", OP_VOICE_AM);
    vm_add_prim("REVERB!", OP_REVERB);
    vm_add_prim("ECHO!", OP_ECHO);
    vm_add_prim("VECHO!", OP_VECHO);
    vm_add_prim("VOICES-N!", OP_VOICES_N);
    vm_add_prim("Y-OUT", OP_Y_OUT);
    vm_add_prim("X@", OP_X_FETCH);
    vm_add_prim("PART@", OP_PART_FETCH);
    vm_add_prim("SAMPLES@", OP_SAMPLES_FETCH);
}

void bsf_vm_save_watermark(void) {
    s_vm.base_dict_count = s_vm.dict_count;
    s_vm.base_code_len   = s_vm.code_len;
    s_vm.base_var_count  = s_vm.var_count;
}

void bsf_vm_restore_watermark(void) {
    s_vm.dict_count = s_vm.base_dict_count;
    s_vm.code_len   = s_vm.base_code_len;
    s_vm.var_count  = s_vm.base_var_count;
    s_vm.dsp = -1;
    s_vm.rsp = -1;
    s_vm.csp = -1;
    s_vm.compiling = false;
    s_vm.track_eval_word = -1;
}

int bsf_vm_resolve_track_entry(void) {
    s_vm.track_eval_word = vm_find_word("TRACK-EVAL");
    return (s_vm.track_eval_word >= 0) ? 0 : -1;
}

int bsf_vm_run_track_eval(fix16_t x_pos, int32_t part_idx) {
    if (s_vm.track_eval_word < 0) return -1;
    const BsfWord *w = &s_vm.dict[s_vm.track_eval_word];
    if (w->kind != 1) return -1;
    s_vm.dsp = -1;
    s_vm.rsp = -1;
    vm_push(x_pos);
    vm_push(part_idx);
    return vm_exec_from(w->val);
}

/* ── Compile-Time Tokenizer & Compiler (Load/Reload Only) ────────── */
static bool next_token(const char **p, char *out, size_t cap) {
    const char *s = *p;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s == '\0') { *p = s; return false; }
    size_t n = 0;
    while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') {
        if (n + 1 < cap) out[n++] = *s;
        s++;
    }
    out[n] = '\0';
    *p = s;
    return true;
}

int bsf_compile_source(const char *src, const char *origin) {
    if (!src) return -1;
    const char *p = src;
    char tok[64];
    int line_no = 1;

    while (*p) {
        /* Track newlines while skipping whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (*p == '\n') line_no++;
            p++;
        }
        if (!*p) break;
        if (!next_token(&p, tok, sizeof(tok))) break;

        /* Line comment */
        if (strcmp(tok, "\\") == 0) {
            while (*p && *p != '\n') p++;
            continue;
        }
        /* Paren comment */
        if (strcmp(tok, "(") == 0) {
            while (*p && *p != ')') {
                if (*p == '\n') line_no++;
                p++;
            }
            if (*p == ')') p++;
            continue;
        }

        /* INCLUDE <path> */
        if (bsf_strcasecmp_local(tok, "INCLUDE") == 0) {
            char inc_path[128];
            if (!next_token(&p, inc_path, sizeof(inc_path))) {
                bsf_dsp_set_error("INCLUDE missing filename");
                return -1;
            }
            static char s_inc_buf[16384];
            size_t len = 0;
            if (bsf_read_text_file(inc_path, s_inc_buf, sizeof(s_inc_buf) - 1, &len) != 0) {
                char err[128];
                snprintf(err, sizeof(err), "INCLUDE failed: %s", inc_path);
                bsf_dsp_set_error(err);
                return -1;
            }
            s_inc_buf[len] = '\0';
            if (bsf_compile_source(s_inc_buf, inc_path) != 0) return -1;
            continue;
        }

        /* Colon definition */
        if (strcmp(tok, ":") == 0) {
            char wname[32];
            if (!next_token(&p, wname, sizeof(wname))) {
                bsf_dsp_set_error("Expected word name after :");
                return -1;
            }
            if (s_vm.dict_count >= BSF_DICT_MAX) {
                bsf_dsp_set_error("BSF dictionary full");
                return -1;
            }
            BsfWord *w = &s_vm.dict[s_vm.dict_count++];
            strncpy(w->name, wname, sizeof(w->name) - 1);
            w->name[sizeof(w->name) - 1] = '\0';
            w->kind = 1;
            w->val  = s_vm.code_len;
            s_vm.compiling = true;
            continue;
        }

        /* Semicolon */
        if (strcmp(tok, ";") == 0) {
            if (!s_vm.compiling) {
                bsf_dsp_set_error("Unexpected ; outside definition");
                return -1;
            }
            if (!vm_emit(OP_EXIT)) return -1;
            s_vm.compiling = false;
            continue;
        }

        /* CONSTANT */
        if (bsf_strcasecmp_local(tok, "CONSTANT") == 0) {
            char cname[32];
            if (!next_token(&p, cname, sizeof(cname))) return -1;
            if (s_vm.dict_count >= BSF_DICT_MAX) return -1;
            int32_t v = vm_pop();
            BsfWord *w = &s_vm.dict[s_vm.dict_count++];
            strncpy(w->name, cname, sizeof(w->name) - 1);
            w->name[sizeof(w->name) - 1] = '\0';
            w->kind = 2;
            w->val  = v;
            continue;
        }

        /* VARIABLE */
        if (bsf_strcasecmp_local(tok, "VARIABLE") == 0) {
            char vname[32];
            if (!next_token(&p, vname, sizeof(vname))) return -1;
            if (s_vm.dict_count >= BSF_DICT_MAX || s_vm.var_count >= BSF_VARS_MAX) return -1;
            BsfWord *w = &s_vm.dict[s_vm.dict_count++];
            strncpy(w->name, vname, sizeof(w->name) - 1);
            w->name[sizeof(w->name) - 1] = '\0';
            w->kind = 3;
            w->val  = s_vm.var_count++;
            s_vm.vars[w->val] = 0;
            continue;
        }

        /* Control flow words (compile-only) */
        if (s_vm.compiling) {
            if (bsf_strcasecmp_local(tok, "IF") == 0) {
                vm_emit(OP_QBRANCH);
                if (s_vm.csp + 1 >= BSF_CSTACK_MAX) return -1;
                s_vm.cstack[++s_vm.csp] = s_vm.code_len;
                vm_emit(0);
                continue;
            }
            if (bsf_strcasecmp_local(tok, "ELSE") == 0) {
                if (s_vm.csp < 0) return -1;
                int32_t if_patch = s_vm.cstack[s_vm.csp--];
                vm_emit(OP_BRANCH);
                s_vm.cstack[++s_vm.csp] = s_vm.code_len;
                vm_emit(0);
                s_vm.code[if_patch] = s_vm.code_len;
                continue;
            }
            if (bsf_strcasecmp_local(tok, "THEN") == 0) {
                if (s_vm.csp < 0) return -1;
                int32_t patch = s_vm.cstack[s_vm.csp--];
                s_vm.code[patch] = s_vm.code_len;
                continue;
            }
            if (bsf_strcasecmp_local(tok, "DO") == 0) {
                vm_emit(OP_DO);
                if (s_vm.csp + 1 >= BSF_CSTACK_MAX) return -1;
                s_vm.cstack[++s_vm.csp] = s_vm.code_len;
                continue;
            }
            if (bsf_strcasecmp_local(tok, "LOOP") == 0) {
                if (s_vm.csp < 0) return -1;
                int32_t dest = s_vm.cstack[s_vm.csp--];
                vm_emit(OP_LOOP);
                vm_emit(dest);
                continue;
            }
            if (bsf_strcasecmp_local(tok, "+LOOP") == 0) {
                if (s_vm.csp < 0) return -1;
                int32_t dest = s_vm.cstack[s_vm.csp--];
                vm_emit(OP_PLOOP);
                vm_emit(dest);
                continue;
            }
            if (bsf_strcasecmp_local(tok, "BEGIN") == 0) {
                if (s_vm.csp + 1 >= BSF_CSTACK_MAX) return -1;
                s_vm.cstack[++s_vm.csp] = s_vm.code_len;
                continue;
            }
            if (bsf_strcasecmp_local(tok, "UNTIL") == 0) {
                if (s_vm.csp < 0) return -1;
                int32_t dest = s_vm.cstack[s_vm.csp--];
                vm_emit(OP_QBRANCH);
                vm_emit(dest);
                continue;
            }
            if (bsf_strcasecmp_local(tok, "WHILE") == 0) {
                vm_emit(OP_QBRANCH);
                if (s_vm.csp + 1 >= BSF_CSTACK_MAX) return -1;
                s_vm.cstack[++s_vm.csp] = s_vm.code_len;
                vm_emit(0);
                continue;
            }
            if (bsf_strcasecmp_local(tok, "REPEAT") == 0) {
                if (s_vm.csp < 1) return -1;
                int32_t wh_patch = s_vm.cstack[s_vm.csp--];
                int32_t beg_dest = s_vm.cstack[s_vm.csp--];
                vm_emit(OP_BRANCH);
                vm_emit(beg_dest);
                s_vm.code[wh_patch] = s_vm.code_len;
                continue;
            }
            if (bsf_strcasecmp_local(tok, "CASE") == 0) {
                if (s_vm.case_depth < 16) {
                    s_vm.case_end_starts[s_vm.case_depth++] = s_vm.case_end_count;
                }
                continue;
            }
            if (bsf_strcasecmp_local(tok, "OF") == 0) {
                /* OVER = IF DROP */
                vm_emit(OP_OVER);
                vm_emit(OP_EQ);
                vm_emit(OP_QBRANCH);
                if (s_vm.csp + 1 >= BSF_CSTACK_MAX) return -1;
                s_vm.cstack[++s_vm.csp] = s_vm.code_len;
                vm_emit(0);
                vm_emit(OP_DROP);
                continue;
            }
            if (bsf_strcasecmp_local(tok, "ENDOF") == 0) {
                if (s_vm.csp < 0) return -1;
                int32_t of_patch = s_vm.cstack[s_vm.csp--];
                vm_emit(OP_BRANCH);
                if (s_vm.case_end_count < 64) {
                    s_vm.case_end_patches[s_vm.case_end_count++] = s_vm.code_len;
                }
                vm_emit(0);
                s_vm.code[of_patch] = s_vm.code_len;
                continue;
            }
            if (bsf_strcasecmp_local(tok, "ENDCASE") == 0) {
                vm_emit(OP_DROP);
                if (s_vm.case_depth > 0) {
                    int32_t start_idx = s_vm.case_end_starts[--s_vm.case_depth];
                    for (int32_t k = start_idx; k < s_vm.case_end_count; k++) {
                        s_vm.code[s_vm.case_end_patches[k]] = s_vm.code_len;
                    }
                    s_vm.case_end_count = start_idx;
                }
                continue;
            }
        }

        /* Dictionary lookup */
        int wi = vm_find_word(tok);
        if (wi >= 0) {
            const BsfWord *w = &s_vm.dict[wi];
            if (s_vm.compiling) {
                if (w->kind == 0) {
                    vm_emit(w->val);
                } else if (w->kind == 1) {
                    vm_emit(OP_CALL);
                    vm_emit(w->val);
                } else if (w->kind == 2) {
                    vm_emit(OP_LIT);
                    vm_emit(w->val);
                } else if (w->kind == 3) {
                    vm_emit(OP_VAR_ADDR);
                    vm_emit(w->val);
                }
            } else {
                if (w->kind == 0) {
                    int32_t tmp_pc = s_vm.code_len;
                    if (tmp_pc + 2 >= BSF_CODE_MAX) return -1;
                    s_vm.code[tmp_pc] = w->val;
                    s_vm.code[tmp_pc + 1] = OP_EXIT;
                    int32_t saved_len = s_vm.code_len;
                    s_vm.code_len = tmp_pc + 2;
                    int rc = vm_exec_from(tmp_pc);
                    s_vm.code_len = saved_len;
                    if (rc != 0) return -1;
                } else if (w->kind == 1) {
                    if (vm_exec_from(w->val) != 0) return -1;
                } else if (w->kind == 2 || w->kind == 3) {
                    vm_push(w->val);
                }
            }
            continue;
        }

        /* Numeric literal */
        int32_t num = 0;
        if (vm_parse_number(tok, &num)) {
            if (s_vm.compiling) {
                vm_emit(OP_LIT);
                vm_emit(num);
            } else {
                vm_push(num);
            }
            continue;
        }

        char err[128];
        snprintf(err, sizeof(err), "%s:%d unknown word '%s'",
                 origin ? origin : "bsf", line_no, tok);
        bsf_dsp_set_error(err);
        return -1;
    }

    if (s_vm.compiling) {
        bsf_dsp_set_error("Unterminated colon definition (missing ;)");
        s_vm.compiling = false;
        return -1;
    }
    return 0;
}

/* ── File I/O Helper (VFS /SYS/... with host assets/... fallback) ── */
int bsf_read_text_file(const char *path, char *buf, size_t cap, size_t *out_len) {
    if (!path || !buf || cap < 2) return -1;
    size_t n = 0;

    /* 1. Try BTRON VFS first (exact path) */
    if (vfs_read_file(path, buf, cap - 1, &n) == 0 && n > 0) {
        buf[n] = '\0';
        if (out_len) *out_len = n;
        return 0;
    }

#if BTRON_HOSTED || defined(__STDC_HOSTED__)
    /* 2. Host filesystem fallback: strip leading /SYS/ -> assets/ */
    const char *rel = path;
    if (strncmp(path, "/SYS/", 5) == 0) rel = path + 5;
    else if (path[0] == '/') rel = path + 1;

    char candidates[4][256];
    snprintf(candidates[0], sizeof(candidates[0]), "%s", path);
    snprintf(candidates[1], sizeof(candidates[1]), "assets/%s", rel);
    snprintf(candidates[2], sizeof(candidates[2]), "btron/assets/%s", rel);
    snprintf(candidates[3], sizeof(candidates[3]), "../btron/assets/%s", rel);

    for (int i = 0; i < 4; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) {
            size_t rd = fread(buf, 1, cap - 1, f);
            fclose(f);
            buf[rd] = '\0';
            if (out_len) *out_len = rd;
            return 0;
        }
    }
#endif
    return -1;
}
