; ==============================================================================
; T-FW (T-Firmware) — forth_z80_next.asm
; Fast Z80 Inner Interpreter & Token Dispatcher
; Assembler: Pasmo Z80
; ==============================================================================

IFDEF CONFIG_TFW

                include "forth_tokens.inc"

; ------------------------------------------------------------------------------
; Inner Interpreter Dispatcher (NEXT)
; ------------------------------------------------------------------------------
forth_next:
                ld      a, (bc)         ; Fetch next token byte
                inc     bc              ; Advance IP
                add     a, a            ; Token * 2 (table offset)
                ld      l, a
                ld      h, 0
                ld      de, token_dispatch_table
                add     hl, de
                ld      e, (hl)
                inc     hl
                ld      d, (hl)
                ex      de, hl
                jp      (hl)            ; Jump directly to primitive

; ------------------------------------------------------------------------------
; Token Dispatch Table (256 Words = 512 Bytes)
; ------------------------------------------------------------------------------
token_dispatch_table:
                dw      prim_end        ; 0x00: Block End
                dw      prim_dup        ; 0x01: DUP
                dw      prim_drop       ; 0x02: DROP
                dw      prim_swap       ; 0x03: SWAP
                dw      prim_over       ; 0x04: OVER
                dw      prim_rot        ; 0x05: ROT
                dw      prim_to_r       ; 0x06: >R
                dw      prim_r_from     ; 0x07: R>
                dw      prim_r_fetch    ; 0x08: R@
                dw      prim_two_to_r   ; 0x09: 2>R
                dw      prim_two_r_from ; 0x0A: 2R>
                defs    ($10 - $0B) * 2, 0

                dw      prim_plus       ; 0x10: +
                dw      prim_minus      ; 0x11: -
                dw      prim_mult       ; 0x12: *
                dw      prim_div        ; 0x13: /
                dw      prim_mod        ; 0x14: MOD
                dw      prim_negate     ; 0x15: NEGATE
                dw      prim_and        ; 0x16: AND
                dw      prim_or         ; 0x17: OR
                dw      prim_xor        ; 0x18: XOR
                dw      prim_invert     ; 0x19: INVERT
                dw      prim_lshift     ; 0x1A: LSHIFT
                dw      prim_rshift     ; 0x1B: RSHIFT
                dw      prim_zero_eq    ; 0x1C: 0=
                dw      prim_zero_lt    ; 0x1D: 0<
                dw      prim_eq         ; 0x1E: =
                dw      prim_lt         ; 0x1F: <
                dw      prim_gt         ; 0x20: >
                dw      prim_u_lt       ; 0x21: U<
                defs    ($24 - $22) * 2, 0

                dw      prim_fetch      ; 0x24: @
                dw      prim_store      ; 0x25: !
                dw      prim_c_fetch    ; 0x26: C@
                dw      prim_c_store    ; 0x27: C!
                dw      prim_plus_store ; 0x28: +!
                dw      prim_cells      ; 0x29: CELLS
                dw      prim_cell_plus  ; 0x2A: CELL+
                dw      prim_chars      ; 0x2B: CHARS
                dw      prim_char_plus  ; 0x2C: CHAR+
                dw      prim_allot      ; 0x2D: ALLOT
                defs    ($30 - $2E) * 2, 0

                dw      prim_execute    ; 0x30: EXECUTE
                dw      prim_exit       ; 0x31: EXIT
                dw      prim_branch     ; 0x32: branch
                dw      prim_qbranch    ; 0x33: ?branch
                dw      prim_lit        ; 0x34: lit
                dw      prim_blit       ; 0x35: b(lit)
                defs    ($38 - $36) * 2, 0

                dw      prim_colon      ; 0x38: :
                dw      prim_semicolon  ; 0x39: ;
                dw      prim_create     ; 0x3A: CREATE
                dw      prim_does       ; 0x3B: DOES>
                dw      prim_tick       ; 0x3C: '
                dw      prim_find       ; 0x3D: FIND
                dw      prim_lbrac      ; 0x3E: [
                dw      prim_rbrac      ; 0x3F: ]
                dw      prim_literal    ; 0x40: LITERAL
                dw      prim_state      ; 0x41: STATE
                dw      prim_immediate  ; 0x42: IMMEDIATE
                dw      prim_compile_c  ; 0x43: COMPILE,
                dw      prim_parse_name ; 0x44: PARSE-NAME
                defs    ($48 - $45) * 2, 0

                dw      prim_emit       ; 0x48: EMIT
                dw      prim_key        ; 0x49: KEY
                dw      prim_key_q      ; 0x4A: KEY?
                dw      prim_type       ; 0x4B: TYPE
                dw      prim_cr         ; 0x4C: CR
                dw      prim_bye        ; 0x4D: BYE
                defs    ($60 - $4E) * 2, 0

                dw      prim_find_dev   ; 0x60: find-device
                dw      prim_peer       ; 0x61: peer
                dw      prim_child      ; 0x62: child
                dw      prim_parent     ; 0x63: parent
                dw      prim_get_prop   ; 0x64: get-property
                dw      prim_set_prop   ; 0x65: set-property
                dw      prim_my_self    ; 0x66: my-self
                dw      prim_byte_load  ; 0x67: byte-load
                defs    (256 - $68) * 2, 0

ENDIF
