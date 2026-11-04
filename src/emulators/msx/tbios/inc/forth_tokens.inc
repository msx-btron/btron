; ==============================================================================
; T-FW (T-Firmware) — forth_tokens.inc
; Canonical Token Definitions & Dictionary Flags for ANS Forth / OpenFirmware
; Assembler: Pasmo Z80
; ==============================================================================

; 0x00: Block End
TOK_END:            equ     $00

; 0x01 - 0x0A: Stack Manipulation
TOK_DUP:            equ     $01
TOK_DROP:           equ     $02
TOK_SWAP:           equ     $03
TOK_OVER:           equ     $04
TOK_ROT:            equ     $05
TOK_TO_R:           equ     $06
TOK_R_FROM:         equ     $07
TOK_R_FETCH:        equ     $08
TOK_TWO_TO_R:       equ     $09
TOK_TWO_R_FROM:     equ     $0A

; 0x10 - 0x21: Arithmetic & Logic
TOK_PLUS:           equ     $10
TOK_MINUS:          equ     $11
TOK_MULT:           equ     $12
TOK_DIV:            equ     $13
TOK_MOD:            equ     $14
TOK_NEGATE:         equ     $15
TOK_AND:            equ     $16
TOK_OR:             equ     $17
TOK_XOR:            equ     $18
TOK_INVERT:         equ     $19
TOK_LSHIFT:         equ     $1A
TOK_RSHIFT:         equ     $1B
TOK_ZERO_EQ:        equ     $1C
TOK_ZERO_LT:        equ     $1D
TOK_EQ:             equ     $1E
TOK_LT:             equ     $1F
TOK_GT:             equ     $20
TOK_U_LT:           equ     $21

; 0x24 - 0x2D: Memory Access & Portability
TOK_FETCH:          equ     $24
TOK_STORE:          equ     $25
TOK_C_FETCH:        equ     $26
TOK_C_STORE:        equ     $27
TOK_PLUS_STORE:     equ     $28
TOK_CELLS:          equ     $29
TOK_CELL_PLUS:      equ     $2A
TOK_CHARS:          equ     $2B
TOK_CHAR_PLUS:      equ     $2C
TOK_ALLOT:          equ     $2D

; 0x30 - 0x35: Control & Inner Interpreter
TOK_EXECUTE:        equ     $30
TOK_EXIT:           equ     $31
TOK_BRANCH:         equ     $32
TOK_QBRANCH:        equ     $33
TOK_LIT:            equ     $34
TOK_BLIT:           equ     $35

; 0x38 - 0x44: Compilation & Outer Interpreter
TOK_COLON:          equ     $38
TOK_SEMICOLON:      equ     $39
TOK_CREATE:         equ     $3A
TOK_DOES:           equ     $3B
TOK_TICK:           equ     $3C
TOK_FIND:           equ     $3D
TOK_LBRAC:          equ     $3E
TOK_RBRAC:          equ     $3F
TOK_LITERAL:        equ     $40
TOK_STATE:          equ     $41
TOK_IMMEDIATE:      equ     $42
TOK_COMPILE_COMMA:  equ     $43
TOK_PARSE_NAME:     equ     $44

; 0x48 - 0x4E: I/O & System
TOK_EMIT:           equ     $48
TOK_KEY:            equ     $49
TOK_KEY_Q:          equ     $4A
TOK_TYPE:           equ     $4B
TOK_CR:             equ     $4C
TOK_BYE:            equ     $4D
TOK_WORDS:          equ     $4E

; 0x60 - 0x69: OpenFirmware (IEEE 1275) Device-Tree Extensions
TOK_FIND_DEV:       equ     $60
TOK_PEER:           equ     $61
TOK_CHILD:          equ     $62
TOK_PARENT:         equ     $63
TOK_GET_PROP:       equ     $64
TOK_SET_PROP:       equ     $65
TOK_MY_SELF:        equ     $66
TOK_BYTE_LOAD:      equ     $67
TOK_DEV_TREE:       equ     $68
TOK_HELP:           equ     $69

; 0xFF: Token Stream Escape
TOK_ESCAPE:         equ     $FF

; Dictionary Header Flags
FLAG_IMMEDIATE:     equ     $80
FLAG_SMUDGE:        equ     $40
FLAG_FCODE:         equ     $20
LEN_MASK:           equ     $1F

