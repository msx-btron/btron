/*
 * B-System BTRON3 / T-FW — forth_def.h
 * ANS Forth-2012 Core Subset & OpenFirmware IEEE 1275 Shared Definitions
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#ifndef _BTRON_FORTH_DEF_H_
#define _BTRON_FORTH_DEF_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Multi-Tier Cell Width Abstraction ─────────────────────────── */
#if defined(FORTH_16BIT) || defined(TFW_Z80) || defined(__IA16__) || defined(__MSDOS__)
    typedef int16_t          forth_cell_t;
    typedef uint16_t         forth_ucell_t;
    #define FORTH_CELL_SIZE  2
    #define FORTH_CELL_SHIFT 1
    #define FORTH_STACK_MAX  128
    #define FORTH_RSTACK_MAX 64
    #define FORTH_ARENA_MAX  (16 * 1024)
    #define FORTH_TIB_MAX    256
    #define FORTH_DEV_MAX    64
#elif defined(FORTH_32BIT) || defined(__arm__) || defined(__mips__) || defined(__i386__)
    typedef int32_t          forth_cell_t;
    typedef uint32_t         forth_ucell_t;
    #define FORTH_CELL_SIZE  4
    #define FORTH_CELL_SHIFT 2
    #define FORTH_STACK_MAX  512
    #define FORTH_RSTACK_MAX 256
    #define FORTH_ARENA_MAX  (128 * 1024)
    #define FORTH_TIB_MAX    1024
    #define FORTH_DEV_MAX    128
#else
    /* Native 64-bit / default host architecture */
    typedef intptr_t         forth_cell_t;
    typedef uintptr_t        forth_ucell_t;
    #define FORTH_CELL_SIZE  ((int)sizeof(forth_cell_t))
    #define FORTH_CELL_SHIFT ((sizeof(forth_cell_t) == 8) ? 3 : 2)
    #define FORTH_STACK_MAX  512
    #define FORTH_RSTACK_MAX 256
    #define FORTH_ARENA_MAX  (256 * 1024)
    #define FORTH_TIB_MAX    1024
    #define FORTH_DEV_MAX    256
#endif

/* ── 64-Byte Cache Line Alignment ───────────────────────────────── */
#if defined(__GNUC__) || defined(__clang__)
    #define FORTH_ALIGN64    __attribute__((aligned(64)))
    #define FORTH_HOT        __attribute__((hot))
    #define FORTH_INLINE     inline __attribute__((always_inline))
#else
    #define FORTH_ALIGN64
    #define FORTH_HOT
    #define FORTH_INLINE     inline
#endif

/* ── Canonical Token Assignment (ANS Forth Core Subset) ─────────── */
enum ForthToken {
    /* 0x00: Block End */
    FORTH_TOK_END          = 0x00,

    /* 0x01 - 0x0C: Stack Manipulation */
    FORTH_TOK_DUP          = 0x01,
    FORTH_TOK_DROP         = 0x02,
    FORTH_TOK_SWAP         = 0x03,
    FORTH_TOK_OVER         = 0x04,
    FORTH_TOK_ROT          = 0x05,
    FORTH_TOK_TO_R         = 0x06,
    FORTH_TOK_R_FROM       = 0x07,
    FORTH_TOK_R_FETCH      = 0x08,
    FORTH_TOK_TWO_TO_R     = 0x09,
    FORTH_TOK_TWO_R_FROM   = 0x0A,
    FORTH_TOK_TWO_DUP      = 0x0B,
    FORTH_TOK_TWO_DROP     = 0x0C,

    /* 0x10 - 0x21: Arithmetic & Logic */
    FORTH_TOK_PLUS         = 0x10,
    FORTH_TOK_MINUS        = 0x11,
    FORTH_TOK_MULT         = 0x12,
    FORTH_TOK_DIV          = 0x13,
    FORTH_TOK_MOD          = 0x14,
    FORTH_TOK_NEGATE       = 0x15,
    FORTH_TOK_AND          = 0x16,
    FORTH_TOK_OR           = 0x17,
    FORTH_TOK_XOR          = 0x18,
    FORTH_TOK_INVERT       = 0x19,
    FORTH_TOK_LSHIFT       = 0x1A,
    FORTH_TOK_RSHIFT       = 0x1B,
    FORTH_TOK_ZERO_EQ      = 0x1C,
    FORTH_TOK_ZERO_LT      = 0x1D,
    FORTH_TOK_EQ           = 0x1E,
    FORTH_TOK_LT           = 0x1F,
    FORTH_TOK_GT           = 0x20,
    FORTH_TOK_U_LT         = 0x21,
    FORTH_TOK_NEQ          = 0x22,
    FORTH_TOK_ZERO_NEQ     = 0x23,

    /* 0x24 - 0x2D: Memory Access & Portability */
    FORTH_TOK_FETCH        = 0x24,
    FORTH_TOK_STORE        = 0x25,
    FORTH_TOK_C_FETCH      = 0x26,
    FORTH_TOK_C_STORE      = 0x27,
    FORTH_TOK_PLUS_STORE   = 0x28,
    FORTH_TOK_CELLS        = 0x29,
    FORTH_TOK_CELL_PLUS    = 0x2A,
    FORTH_TOK_CHARS        = 0x2B,
    FORTH_TOK_CHAR_PLUS    = 0x2C,
    FORTH_TOK_ALLOT        = 0x2D,
    FORTH_TOK_COMMA        = 0x2E,
    FORTH_TOK_C_COMMA      = 0x2F,

    /* 0x30 - 0x37: Control & Inner Interpreter */
    FORTH_TOK_EXECUTE      = 0x30,
    FORTH_TOK_EXIT         = 0x31,
    FORTH_TOK_BRANCH       = 0x32,
    FORTH_TOK_QBRANCH      = 0x33,
    FORTH_TOK_LIT          = 0x34,
    FORTH_TOK_BLIT         = 0x35,
    FORTH_TOK_LOOP         = 0x36,
    FORTH_TOK_I            = 0x37,

    /* 0x38 - 0x47: Compilation & Outer Interpreter */
    FORTH_TOK_COLON        = 0x38,
    FORTH_TOK_SEMICOLON    = 0x39,
    FORTH_TOK_CREATE       = 0x3A,
    FORTH_TOK_DOES         = 0x3B,
    FORTH_TOK_TICK         = 0x3C,
    FORTH_TOK_FIND         = 0x3D,
    FORTH_TOK_LBRAC        = 0x3E,
    FORTH_TOK_RBRAC        = 0x3F,
    FORTH_TOK_LITERAL      = 0x40,
    FORTH_TOK_STATE        = 0x41,
    FORTH_TOK_IMMEDIATE    = 0x42,
    FORTH_TOK_COMPILE_COMMA= 0x43,
    FORTH_TOK_PARSE_NAME   = 0x44,
    FORTH_TOK_J            = 0x45,
    FORTH_TOK_DOT          = 0x46,
    FORTH_TOK_DOT_S        = 0x47,

    /* 0x48 - 0x4E: I/O & System */
    FORTH_TOK_EMIT         = 0x48,
    FORTH_TOK_KEY          = 0x49,
    FORTH_TOK_KEY_Q        = 0x4A,
    FORTH_TOK_TYPE         = 0x4B,
    FORTH_TOK_CR           = 0x4C,
    FORTH_TOK_BYE          = 0x4D,
    FORTH_TOK_WORDS        = 0x4E,
    FORTH_TOK_HERE         = 0x4F,

    /* 0x60 - 0x69: OpenFirmware (IEEE 1275) Device-Tree Extensions */
    FORTH_TOK_FIND_DEV     = 0x60,
    FORTH_TOK_PEER         = 0x61,
    FORTH_TOK_CHILD        = 0x62,
    FORTH_TOK_PARENT       = 0x63,
    FORTH_TOK_GET_PROP     = 0x64,
    FORTH_TOK_SET_PROP     = 0x65,
    FORTH_TOK_MY_SELF      = 0x66,
    FORTH_TOK_BYTE_LOAD    = 0x67,
    FORTH_TOK_DEV_TREE     = 0x68,
    FORTH_TOK_HELP         = 0x69,
    FORTH_TOK_PWD          = 0x6A,

    /* 0x70 - 0x7B: B-System Window & Raster Graphics Extensions */
    FORTH_TOK_WND_OPEN     = 0x70,
    FORTH_TOK_WND_CLOSE    = 0x71,
    FORTH_TOK_WND_PIXEL    = 0x72,
    FORTH_TOK_WND_VLINE    = 0x73,
    FORTH_TOK_WND_RECT     = 0x74,
    FORTH_TOK_WND_CLEAR    = 0x75,
    FORTH_TOK_WND_UPDATE   = 0x76,
    FORTH_TOK_WND_SIZE     = 0x77,
    FORTH_TOK_RGB          = 0x78,
    FORTH_TOK_INCLUDE      = 0x79,
    FORTH_TOK_WND_ON_PAINT = 0x7A,
    FORTH_TOK_MS           = 0x7B,
    FORTH_TOK_DISPLAY_Q    = 0x7C,

    /* 0xFF: Token Stream Escape */
    FORTH_TOK_ESCAPE       = 0xFF,
    FORTH_TOKEN_COUNT      = 256
};

/* ── Dictionary Header Flags ────────────────────────────────────── */
#define FORTH_FLAG_IMMEDIATE 0x80
#define FORTH_FLAG_SMUDGE    0x40
#define FORTH_FLAG_FCODE     0x20
#define FORTH_LEN_MASK       0x1F

/* ── OpenFirmware Device-Tree Sizing ────────────────────────────── */
#define FCODE_MAX_PROPS      8
#define FCODE_PROP_KEY_LEN   16
#define FCODE_NODE_NAME_LEN  16

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FORTH_DEF_H_ */
