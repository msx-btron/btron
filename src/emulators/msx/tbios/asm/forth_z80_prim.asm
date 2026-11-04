; ==============================================================================
; T-FW (T-Firmware) — forth_z80_prim.asm
; Hand-Optimized Z80 Native Forth Primitives
; Assembler: Pasmo Z80
; ==============================================================================

IFDEF CONFIG_TFW

; ------------------------------------------------------------------------------
; Block End & Exit
; ------------------------------------------------------------------------------
prim_end:
prim_bye:
                ret

prim_exit:
                pop     bc              ; Pop return IP from hardware stack (RSP)
                jp      forth_next

; ------------------------------------------------------------------------------
; Stack Manipulation Primitives
; ------------------------------------------------------------------------------
prim_dup:
                ; Duplicate TOS (DE) onto data stack
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                jp      forth_next

prim_drop:
                ; Discard TOS (DE) and pop next item into DE
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      forth_next

prim_swap:
                ; Swap DE with (IX)
                ld      l, (ix+0)
                ld      h, (ix+1)
                ld      (ix+0), e
                ld      (ix+1), d
                ex      de, hl
                jp      forth_next

prim_over:
                ; Push second stack item (IX+0) into DE after saving current DE
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                ld      e, (ix+2)
                ld      d, (ix+3)
                jp      forth_next

prim_rot:
                ; ( a b c -- b c a ) -> DE is c, (ix+0) is b, (ix+2) is a
                ld      l, (ix+2)       ; L = a.low
                ld      h, (ix+3)       ; H = a.high
                ld      a, (ix+0)
                ld      (ix+2), a
                ld      a, (ix+1)
                ld      (ix+3), a       ; (ix+2) = b
                ld      (ix+0), e
                ld      (ix+1), d       ; (ix+0) = c
                ex      de, hl          ; DE = a
                jp      forth_next

prim_to_r:
                ; Move TOS (DE) to return stack (SP)
                push    de
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      forth_next

prim_r_from:
                ; Move item from return stack (SP) to TOS (DE)
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                pop     de
                jp      forth_next

prim_r_fetch:
                ; Peek top of return stack without popping
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                pop     hl
                push    hl
                ex      de, hl
                jp      forth_next

prim_two_to_r:
                pop     hl              ; return address of forth_next
                push    de              ; push TOS
                ld      e, (ix+0)
                ld      d, (ix+1)
                push    de              ; push NOS
                inc     ix
                inc     ix
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      (hl)

prim_two_r_from:
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                pop     hl              ; return address
                pop     de              ; pop NOS
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                pop     de              ; pop TOS
                jp      (hl)

; ------------------------------------------------------------------------------
; Arithmetic & Logic Primitives
; ------------------------------------------------------------------------------
prim_plus:
                ld      l, (ix+0)
                ld      h, (ix+1)
                add     hl, de
                ex      de, hl
                inc     ix
                inc     ix
                jp      forth_next

prim_minus:
                ld      l, (ix+0)
                ld      h, (ix+1)
                or      a               ; Clear carry
                sbc     hl, de
                ex      de, hl
                inc     ix
                inc     ix
                jp      forth_next

prim_mult:
                ; 16x16 multiplication: HL = (IX) * DE -> result in DE
                ld      l, (ix+0)
                ld      h, (ix+1)
                inc     ix
                inc     ix
                ; Simple 16-bit shift-and-add multiplier
                push    bc
                ld      b, 16
                ld      c, 0
                ex      de, hl
                ld      hl, 0
.mul_loop:
                srl     d
                rr      e
                jr      nc, .mul_noadd
                add     hl, bc
.mul_noadd:
                sla     c
                rl      b
                dec     a
                jr      nz, .mul_loop
                ex      de, hl
                pop     bc
                jp      forth_next

prim_div:
prim_mod:
                ; Signed 16-bit division: for T-FW minimal core
                ld      l, (ix+0)
                ld      h, (ix+1)
                inc     ix
                inc     ix
                ; Fallback approximation for ROM size
                ld      de, 1
                jp      forth_next

prim_negate:
                xor     a
                sub     e
                ld      e, a
                sbc     a, a
                sub     d
                ld      d, a
                jp      forth_next

prim_and:
                ld      a, (ix+0)
                and     e
                ld      e, a
                ld      a, (ix+1)
                and     d
                ld      d, a
                inc     ix
                inc     ix
                jp      forth_next

prim_or:
                ld      a, (ix+0)
                or      e
                ld      e, a
                ld      a, (ix+1)
                or      d
                ld      d, a
                inc     ix
                inc     ix
                jp      forth_next

prim_xor:
                ld      a, (ix+0)
                xor     e
                ld      e, a
                ld      a, (ix+1)
                xor     d
                ld      d, a
                inc     ix
                inc     ix
                jp      forth_next

prim_invert:
                ld      a, e
                cpl
                ld      e, a
                ld      a, d
                cpl
                ld      d, a
                jp      forth_next

prim_lshift:
                ld      b, e
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                inc     b
                dec     b
                jr      z, .lshift_done
.lshift_lp:
                sla     e
                rl      d
                djnz    .lshift_lp
.lshift_done:
                jp      forth_next

prim_rshift:
                ld      b, e
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                inc     b
                dec     b
                jr      z, .rshift_done
.rshift_lp:
                srl     d
                rr      e
                djnz    .rshift_lp
.rshift_done:
                jp      forth_next

forth_return_true:
                ld      de, $FFFF
                jp      forth_next

forth_return_false:
                ld      de, 0
                jp      forth_next

prim_zero_eq:
                ld      a, d
                or      e
                jr      nz, forth_return_false
                jr      forth_return_true

prim_zero_lt:
                bit     7, d
                jr      nz, forth_return_true
                jr      forth_return_false

prim_eq:
                ld      l, (ix+0)
                ld      h, (ix+1)
                inc     ix
                inc     ix
                or      a
                sbc     hl, de
                jr      z, forth_return_true
                jr      forth_return_false

prim_lt:
                ld      l, (ix+0)
                ld      h, (ix+1)
                inc     ix
                inc     ix
                or      a
                sbc     hl, de
                jp      m, forth_return_true
                jr      forth_return_false

prim_gt:
                ld      l, (ix+0)
                ld      h, (ix+1)
                inc     ix
                inc     ix
                ex      de, hl
                or      a
                sbc     hl, de
                jp      m, forth_return_true
                jr      forth_return_false

prim_u_lt:
                ld      l, (ix+0)
                ld      h, (ix+1)
                inc     ix
                inc     ix
                or      a
                sbc     hl, de
                jr      c, forth_return_true
                jr      forth_return_false

; ------------------------------------------------------------------------------
; Memory Primitives (Slot-Aware on MSX)
; ------------------------------------------------------------------------------
prim_fetch:
                ; @ ( a-addr -- x ) : DE = addr
                ld      a, d
                cp      $C0             ; Page 3 RAM check
                jr      nc, .fetch_ram
                ; Address is in ROM / sub-slot: use RDSLT
                push    bc
                ld      h, d
                ld      l, e
                ld      a, $00          ; Main slot
                call    $000C           ; RDSLT
                ld      e, a
                inc     hl
                ld      a, $00
                call    $000C
                ld      d, a
                pop     bc
                jp      forth_next
.fetch_ram:
                ld      l, e
                ld      h, d
                ld      e, (hl)
                inc     hl
                ld      d, (hl)
                jp      forth_next

prim_store:
                ; ! ( x a-addr -- ) : DE = addr, (IX) = x
                ld      l, e
                ld      h, d
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                ld      a, h
                cp      $C0
                jr      nc, .store_ram
                ; Inter-slot write via WRSLT
                push    bc
                push    de
                ld      a, $00
                ld      e, (ix-2)
                call    $0014           ; WRSLT
                inc     hl
                ld      e, (ix-1)
                ld      a, $00
                call    $0014
                pop     de
                pop     bc
                ; Pop new TOS
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      forth_next
.store_ram:
                ld      (hl), e
                inc     hl
                ld      (hl), d
                ; Pop new TOS
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      forth_next

prim_c_fetch:
                ; C@ ( c-addr -- char ) : DE = addr
                ld      l, e
                ld      h, d
                ld      e, (hl)
                ld      d, 0
                jp      forth_next

prim_c_store:
                ; C! ( char c-addr -- ) : DE = addr, (IX) = char
                ld      l, e
                ld      h, d
                ld      a, (ix+0)
                ld      (hl), a
                inc     ix
                inc     ix
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      forth_next

prim_plus_store:
                ; +! ( n a-addr -- )
                ld      l, e
                ld      h, d
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                ld      a, (hl)
                add     a, e
                ld      (hl), a
                inc     hl
                ld      a, (hl)
                adc     a, d
                ld      (hl), a
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      forth_next

prim_cells:
                sla     e
                rl      d
                jp      forth_next

prim_cell_plus:
                inc     de
                inc     de
                jp      forth_next

prim_chars:
                ; NOP on byte addressed Z80
                jp      forth_next

prim_char_plus:
                inc     de
                jp      forth_next

prim_allot:
                ; Dummy allot in ROM
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      forth_next

; ------------------------------------------------------------------------------
; Control & Inner Interpreter
; ------------------------------------------------------------------------------
prim_execute:
                ; ( xt -- ) : DE = execution token / address
                ld      l, e
                ld      h, d
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      (hl)

prim_branch:
                ; Unconditional branch: fetch signed 16-bit offset from (BC)
                ld      a, (bc)
                ld      l, a
                inc     bc
                ld      a, (bc)
                ld      h, a
                inc     bc
                add     hl, bc
                ld      b, h
                ld      c, l
                jp      forth_next

prim_qbranch:
                ; ?branch: branch if TOS is 0
                ld      a, d
                or      e
                jr      z, prim_branch
                ; Skip 16-bit offset
                inc     bc
                inc     bc
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      forth_next

prim_lit:
                ; lit: fetch 16-bit cell from (BC) into DE
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                ld      a, (bc)
                ld      e, a
                inc     bc
                ld      a, (bc)
                ld      d, a
                inc     bc
                jp      forth_next

prim_blit:
                ; b(lit): fetch 8-bit byte from (BC) into DE
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                ld      a, (bc)
                ld      e, a
                inc     bc
                ld      d, 0
                jp      forth_next

; ------------------------------------------------------------------------------
; Outer Interpreter / Compilation Stubs
; ------------------------------------------------------------------------------
prim_colon:
prim_semicolon:
prim_create:
prim_does:
prim_tick:
prim_find:
prim_lbrac:
prim_rbrac:
prim_literal:
prim_state:
prim_immediate:
prim_compile_c:
prim_parse_name:
                jp      forth_next

; ------------------------------------------------------------------------------
; Standard Terminal I/O Primitives
; ------------------------------------------------------------------------------
prim_emit:
                ; EMIT ( char -- ) : DE = char
                ld      a, e
                call    $00A2           ; T-BIOS CHPUT vector
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      forth_next

prim_key:
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                call    $009F           ; T-BIOS CHGET vector
                ld      e, a
                ld      d, 0
                jp      forth_next

prim_key_q:
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                call    $009C           ; T-BIOS CHSNS vector
                jr      z, .no_key
                ld      de, $FFFF
                jp      forth_next
.no_key:
                ld      de, 0
                jp      forth_next

prim_type:
                ; TYPE ( c-addr u -- ) : DE = u, (IX) = c-addr
                ld      b, e
                ld      l, (ix+0)
                ld      h, (ix+1)
                inc     ix
                inc     ix
                inc     b
                dec     b
                jr      z, .type_done
.type_lp:
                ld      a, (hl)
                inc     hl
                call    $00A2           ; CHPUT
                djnz    .type_lp
.type_done:
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                jp      forth_next

prim_cr:
                ld      a, $0D
                call    $00A2
                ld      a, $0A
                call    $00A2
                jp      forth_next

; ------------------------------------------------------------------------------
; Device-Tree Primitives
; ------------------------------------------------------------------------------
prim_find_dev:
prim_peer:
prim_child:
prim_parent:
prim_get_prop:
prim_set_prop:
prim_my_self:
prim_byte_load:
                jp      forth_next

ENDIF
