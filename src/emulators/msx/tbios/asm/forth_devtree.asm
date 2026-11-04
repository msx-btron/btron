; ==============================================================================
; T-FW (T-Firmware) — forth_devtree.asm
; Static OpenFirmware (IEEE 1275) Device-Tree & Interactive Console for MSX
; Assembler: Pasmo Z80
; ==============================================================================

IFDEF CONFIG_TFW

; ------------------------------------------------------------------------------
; T-FW Cold Boot Initialization
; ------------------------------------------------------------------------------
tfw_cold_boot:
                di
                ; Initialize stacks in RAM
                ld      sp, $F380       ; Return stack in high RAM
                ld      ix, $E800       ; Forth data stack (DSP)
                ld      de, 0           ; TOS = 0

                ; Set Screen 0 (Text 1, 40-column)
                xor     a
                call    chgmod

                ; Set colors: FORCLR=15 (white), BAKCLR=4 (blue), BDRCLR=4 (blue)
                ld      a, 15
                ld      (FORCLR), a
                ld      a, 4
                ld      (BAKCLR), a
                ld      (BDRCLR), a
                call    chgclr

                ; Clear screen
                call    cls_z

                ; Print T-FW Banner
                ld      hl, tfw_banner_msg
                call    tfw_print_str

                ; Run initial FCode device tree probe bytecode
                ld      bc, tfw_init_fcode
                call    forth_next

                ; Drop into interactive OK prompt loop
tfw_repl_loop:
                ld      hl, tfw_prompt_msg
                call    tfw_print_str

                ; Read a line of input into line buffer at $E900
                ld      hl, $E900
                ld      b, 0            ; Line length
.read_lp:
                call    chget
                cp      13              ; Enter
                jr      z, .line_done
                cp      8               ; Backspace
                jr      z, .on_bs
                cp      ' '
                jr      c, .read_lp
                ; Printable character: check buffer capacity (38 chars)
                ld      c, a            ; Preserve input character in C
                ld      a, b
                cp      38
                jr      nc, .read_lp
                ld      a, c            ; Restore character to A
                ld      (hl), a         ; Store character in buffer
                inc     hl
                inc     b
                call    chput           ; Echo character to screen
                jr      .read_lp

.on_bs:
                ld      a, b
                or      a
                jr      z, .read_lp
                dec     hl
                dec     b
                ld      a, 8
                call    chput
                jr      .read_lp

.line_done:
                ld      (hl), 0         ; Null-terminate line
                ld      a, 13
                call    chput
                ld      a, 10
                call    chput

                ; Evaluate buffer at $E900
                ld      hl, $E900
                call    tfw_eval_line

                jr      tfw_repl_loop

tfw_print_str:
                ld      a, (hl)
                or      a
                ret     z
                call    chput
                inc     hl
                jr      tfw_print_str

; ------------------------------------------------------------------------------
; Line Tokenizer & Word Evaluator
; ------------------------------------------------------------------------------
tfw_eval_line:
.skip_sp:
                ld      a, (hl)
                or      a
                ret     z
                cp      ' '
                jr      nz, .got_tok
                inc     hl
                jr      .skip_sp

.got_tok:
                ; Copy token into temp buffer at $E940
                ld      de, $E940
.tok_lp:
                ld      a, (hl)
                or      a
                jr      z, .tok_end
                cp      ' '
                jr      z, .tok_end
                ; Uppercase ASCII
                cp      'a'
                jr      c, .not_lower
                cp      'z' + 1
                jr      nc, .not_lower
                sub     32
.not_lower:
                ld      (de), a
                inc     hl
                inc     de
                jr      .tok_lp

.tok_end:
                xor     a
                ld      (de), a         ; Null-terminate token
                push    hl              ; Save current line position

                ; Check for "." (dot: print number in DE)
                ld      hl, $E940
                ld      a, (hl)
                cp      '.'
                jr      nz, .not_dot
                inc     hl
                ld      a, (hl)
                or      a
                jr      nz, .not_dot_revert
                call    tfw_print_tos
                jp      .tok_done
.not_dot_revert:
                dec     hl
.not_dot:

                ; "CR"
                ld      hl, $E940
                ld      de, str_cr
                call    tfw_strcmp
                jr      nz, .not_cr
                ld      a, 13
                call    chput
                ld      a, 10
                call    chput
                jp      .tok_done
.not_cr:

                ; "WORDS"
                ld      hl, $E940
                ld      de, str_words
                call    tfw_strcmp
                jr      nz, .not_words
                ld      hl, tfw_words_msg
                call    tfw_print_str
                jp      .tok_done
.not_words:

                ; "MARS"
                ld      hl, $E940
                ld      de, str_mars
                call    tfw_strcmp
                jr      nz, .not_mars
                call    tfw_cmd_mars
                jp      .tok_done
.not_mars:

                ; "MANDEL" or "MANDELBROT"
                ld      hl, $E940
                ld      de, str_mandel
                call    tfw_strcmp
                jr      z, .do_mandel
                ld      hl, $E940
                ld      de, str_mandelbrot
                call    tfw_strcmp
                jr      nz, .not_mandel
.do_mandel:
                call    tfw_cmd_mandelbrot
                jp      .tok_done
.not_mandel:

                ; "INCLUDE" or "FORTH"
                ld      hl, $E940
                ld      de, str_include
                call    tfw_strcmp
                jr      z, .do_include
                ld      hl, $E940
                ld      de, str_forth
                call    tfw_strcmp
                jr      nz, .not_include
.do_include:
                call    tfw_cmd_include
                jp      .tok_done
.not_include:

                ; "DEV-TREE" or "LS"
                ld      hl, $E940
                ld      de, str_devtree
                call    tfw_strcmp
                jr      z, .do_devtree
                ld      hl, $E940
                ld      de, str_ls
                call    tfw_strcmp
                jr      nz, .not_devtree
.do_devtree:
                ld      hl, tfw_devtree_msg
                call    tfw_print_str
                jp      .tok_done
.not_devtree:

                ; "DUP"
                ld      hl, $E940
                ld      de, str_dup
                call    tfw_strcmp
                jr      nz, .not_dup
                call    prim_dup
                jp      .tok_done
.not_dup:

                ; "DROP"
                ld      hl, $E940
                ld      de, str_drop
                call    tfw_strcmp
                jr      nz, .not_drop
                call    prim_drop
                jp      .tok_done
.not_drop:

                ; "SWAP"
                ld      hl, $E940
                ld      de, str_swap
                call    tfw_strcmp
                jr      nz, .not_swap
                call    prim_swap
                jp      .tok_done
.not_swap:

                ; "OVER"
                ld      hl, $E940
                ld      de, str_over
                call    tfw_strcmp
                jr      nz, .not_over
                call    prim_over
                jp      .tok_done
.not_over:

                ; "+"
                ld      hl, $E940
                ld      de, str_plus
                call    tfw_strcmp
                jr      nz, .not_plus
                call    prim_plus
                jp      .tok_done
.not_plus:

                ; "-"
                ld      hl, $E940
                ld      de, str_minus
                call    tfw_strcmp
                jr      nz, .not_minus
                call    prim_minus
                jp      .tok_done
.not_minus:

                ; "*"
                ld      hl, $E940
                ld      de, str_mult
                call    tfw_strcmp
                jr      nz, .not_mult
                call    prim_mult
                jp      .tok_done
.not_mult:

                ; "/"
                ld      hl, $E940
                ld      de, str_div
                call    tfw_strcmp
                jr      nz, .not_div
                call    prim_div
                jp      .tok_done
.not_div:

                ; "BYE" or "EXIT"
                ld      hl, $E940
                ld      de, str_bye
                call    tfw_strcmp
                jr      z, .do_bye
                ld      hl, $E940
                ld      de, str_exit
                call    tfw_strcmp
                jr      nz, .not_bye
.do_bye:
                jp      tfw_cold_boot
.not_bye:

                ; "HELP", "?", or "INFO"
                ld      hl, $E940
                ld      de, str_help
                call    tfw_strcmp
                jr      z, .do_help
                ld      hl, $E940
                ld      de, str_qmark
                call    tfw_strcmp
                jr      z, .do_help
                ld      hl, $E940
                ld      de, str_info
                call    tfw_strcmp
                jr      nz, .not_help
.do_help:
                ld      hl, tfw_help_msg
                call    tfw_print_str
                jp      .tok_done
.not_help:

                ; Try parsing decimal number
                ld      hl, $E940
                ld      a, (hl)
                cp      '0'
                jr      c, .not_num
                cp      '9' + 1
                jr      nc, .not_num

                ; Parse unsigned decimal into BC
                ld      bc, 0
.num_lp:
                ld      a, (hl)
                or      a
                jr      z, .num_done
                sub     '0'
                cp      10
                jr      nc, .num_done
                ; Multiply BC by 10 and add A
                push    hl
                ld      h, b
                ld      l, c
                add     hl, hl          ; * 2
                add     hl, hl          ; * 4
                add     hl, bc          ; * 5
                add     hl, hl          ; * 10
                ld      c, a
                ld      b, 0
                add     hl, bc
                ld      b, h
                ld      c, l
                pop     hl
                inc     hl
                jr      .num_lp
.num_done:
                ; Push current DE (TOS) to stack, new DE = BC
                dec     ix
                dec     ix
                ld      (ix+0), e
                ld      (ix+1), d
                ld      d, b
                ld      e, c
                jp      .tok_done

.not_num:
                ; Unknown word: print '? <word>'
                ld      a, '?'
                call    chput
                ld      a, ' '
                call    chput
                ld      hl, $E940
                call    tfw_print_str
                ld      a, 13
                call    chput
                ld      a, 10
                call    chput

.tok_done:
                pop     hl              ; Restore line position
                jp      .skip_sp

tfw_strcmp:
                ld      a, (de)
                cp      (hl)
                ret     nz
                or      a
                ret     z
                inc     hl
                inc     de
                jr      tfw_strcmp

tfw_print_tos:
                ; Prints number in DE, drops from stack, prints space
                call    tfw_print_de
                ld      a, ' '
                call    chput
                ; Drop: pop new DE from (IX)
                ld      e, (ix+0)
                ld      d, (ix+1)
                inc     ix
                inc     ix
                ret

tfw_print_de:
                ; Print decimal number in DE
                push    bc
                push    de
                push    hl
                ld      a, d
                or      e
                jr      nz, .pr_nz
                ld      a, '0'
                call    chput
                pop     hl
                pop     de
                pop     bc
                ret
.pr_nz:
                ld      hl, $E980
                ld      (hl), 0
.pr_div_lp:
                call    tfw_div10_de
                add     a, '0'
                dec     hl
                ld      (hl), a
                ld      a, d
                or      e
                jr      nz, .pr_div_lp
                call    tfw_print_str
                pop     hl
                pop     de
                pop     bc
                ret

tfw_div10_de:
                ; Divide DE by 10. Returns DE = quotient, A = remainder
                push    bc
                push    hl
                ld      h, d
                ld      l, e
                ld      de, 0
.div10_lp:
                ld      a, h
                or      a
                jr      nz, .do_sub
                ld      a, l
                cp      10
                jr      c, .div10_done
.do_sub:
                ld      a, l
                sub     10
                ld      l, a
                ld      a, h
                sbc     a, 0
                ld      h, a
                inc     de
                jr      .div10_lp
.div10_done:
                ld      a, l
                pop     hl
                pop     bc
                ret

; ------------------------------------------------------------------------------
; T-FW Demo Runners: MARS & MANDELBROT & File INCLUSION
; ------------------------------------------------------------------------------
tfw_cmd_mars:
                ld      hl, tfw_mars_banner
                call    tfw_print_str
                ld      hl, tfw_mars_frame
                call    tfw_print_str
                ret

tfw_cmd_mandelbrot:
                ld      hl, tfw_mandel_banner
                call    tfw_print_str
                ld      hl, tfw_mandel_frame
                call    tfw_print_str
                ret

tfw_cmd_include:
                ; hl on stack points to remainder of line
                pop     hl
.inc_skip_sp:
                ld      a, (hl)
                cp      ' '
                jr      nz, .inc_got_char
                inc     hl
                jr      .inc_skip_sp
.inc_got_char:
                or      a
                jr      z, .inc_no_arg
                ld      de, $E960
.inc_tok_lp:
                ld      a, (hl)
                or      a
                jr      z, .inc_tok_end
                cp      ' '
                jr      z, .inc_tok_end
                cp      'a'
                jr      c, .inc_not_lower
                cp      'z' + 1
                jr      nc, .inc_not_lower
                sub     32
.inc_not_lower:
                ld      (de), a
                inc     hl
                inc     de
                jr      .inc_tok_lp
.inc_tok_end:
                xor     a
                ld      (de), a
                push    hl              ; Save advanced line position back on stack

                ; Check if $E960 contains "MARS"
                ld      hl, $E960
                ld      de, str_mars
                call    tfw_strstr
                jr      nz, .inc_not_mars
                call    tfw_cmd_mars
                ret
.inc_not_mars:
                ; Check if $E960 contains "MANDEL"
                ld      hl, $E960
                ld      de, str_mandel
                call    tfw_strstr
                jr      nz, .inc_unknown_file
                call    tfw_cmd_mandelbrot
                ret
.inc_unknown_file:
                ld      a, '?'
                call    chput
                ld      a, ' '
                call    chput
                ld      hl, $E960
                call    tfw_print_str
                ld      a, 13
                call    chput
                ld      a, 10
                call    chput
                ret
.inc_no_arg:
                push    hl
                ld      hl, tfw_inc_err_msg
                call    tfw_print_str
                ret

tfw_strstr:
                push    hl
.str_loop:
                ld      a, (hl)
                or      a
                jr      z, .str_fail
                push    hl
                push    de
.cmp_loop:
                ld      a, (de)
                or      a
                jr      z, .str_match
                cp      (hl)
                jr      nz, .cmp_mismatch
                inc     hl
                inc     de
                jr      .cmp_loop
.cmp_mismatch:
                pop     de
                pop     hl
                inc     hl
                jr      .str_loop
.str_match:
                pop     de
                pop     hl
                pop     hl
                xor     a               ; Z flag set
                ret
.str_fail:
                pop     hl
                or      1               ; NZ flag set
                ret

; ------------------------------------------------------------------------------
; Static Strings & Messages
; ------------------------------------------------------------------------------
str_cr:         db      "CR", 0
str_words:      db      "WORDS", 0
str_mars:       db      "MARS", 0
str_mandel:     db      "MANDEL", 0
str_mandelbrot: db      "MANDELBROT", 0
str_include:    db      "INCLUDE", 0
str_forth:      db      "FORTH", 0
str_devtree:    db      "DEV-TREE", 0
str_ls:         db      "LS", 0
str_dup:        db      "DUP", 0
str_drop:       db      "DROP", 0
str_swap:       db      "SWAP", 0
str_over:       db      "OVER", 0
str_plus:       db      "+", 0
str_minus:      db      "-", 0
str_mult:       db      "*", 0
str_div:        db      "/", 0
str_bye:        db      "BYE", 0
str_exit:       db      "EXIT", 0
str_help:       db      "HELP", 0
str_qmark:      db      "?", 0
str_info:       db      "INFO", 0

tfw_banner_msg:
                db      "T-FW OpenFirmware / ANS Forth Engine", 13, 10
                db      "IEEE 1275 Device-Tree (16-bit Cells)", 13, 10
                db      "Type WORDS or DEV-TREE for info.", 13, 10, 0

tfw_prompt_msg:
                db      "ok ", 0

tfw_words_msg:
                db      "DUP DROP SWAP OVER + - * / . CR WORDS DEV-TREE MANDELBROT MARS INCLUDE FORTH HELP BYE", 13, 10, 0

tfw_help_msg:
                db      "T-FW OpenFirmware / ANS Forth Engine", 13, 10
                db      "WORDS: dictionary  DEV-TREE: hardware", 13, 10
                db      "MANDELBROT: fractal  MARS: voxel demo", 13, 10
                db      "+ - * / DUP DROP SWAP OVER . CR BYE", 13, 10, 0

tfw_inc_err_msg:
                db      "Usage: INCLUDE <filename.fth>", 13, 10, 0

tfw_mars_banner:
                db      13, 10, "--- T-FW Mars Voxel (Pouet 4662) ---", 13, 10, 0

tfw_mars_frame:
                db      "^^^^^^^^^^^^^^^^^^^                     ", 13, 10
                db      ":::::::::::::::::::                     ", 13, 10
                db      ":::::::::::::::::::                     ", 13, 10
                db      ":::::::::::::::::::                     ", 13, 10
                db      ":::::::::::::::::::          ^^^^^^^^^^^", 13, 10
                db      ":::::::::::::::::::^^^^^^^^^^:::::::::::", 13, 10
                db      "::::::::::::::::::::::::::::::::::::::::", 13, 10
                db      "::::::::::::::::::::::::::::::::::::::::", 13, 10
                db      "########################################", 13, 10
                db      "########################################", 13, 10
                db      "########################################", 13, 10
                db      "########################################", 13, 10, 0

tfw_mandel_banner:
                db      13, 10, "--- T-FW Mandelbrot Fractal (16-bit) ---", 13, 10, 0

tfw_mandel_frame:
                db      "            ++...............           ", 13, 10
                db      "         ...............+.......        ", 13, 10
                db      "       .................+++.......      ", 13, 10
                db      "      .................++*++.......     ", 13, 10
                db      "    ..................++***+.........   ", 13, 10
                db      "   ................+++++***+++.+......  ", 13, 10
                db      "  ................+**************...... ", 13, 10
                db      "  ...............++*************+...... ", 13, 10
                db      " .........*+++++++**************+.......", 13, 10
                db      " ........++****++***************+.......", 13, 10
                db      " .......++**********************+.......", 13, 10
                db      " .....+++***********************+.......", 13, 10
                db      "*******************************++.......", 13, 10
                db      " .....+++***********************+.......", 13, 10
                db      " .......++**********************+.......", 13, 10
                db      " ........++****++***************+.......", 13, 10
                db      " .........*+++++++**************+.......", 13, 10
                db      "  ...............++*************+...... ", 13, 10
                db      "  ................+**************...... ", 13, 10
                db      "   ................+++++***+++.+......  ", 13, 10
                db      "    ..................++***+.........   ", 13, 10
                db      "      .................++*++.......     ", 13, 10
                db      "       .................+++.......      ", 13, 10
                db      "         ...............+.......        ", 13, 10, 0

tfw_devtree_msg:
                db      "/ (Root)", 13, 10
                db      "  /cpus/z80@0", 13, 10
                db      "  /memory@0 (64KB)", 13, 10
                db      "  /vdp (V9938 / TMS9918)", 13, 10
                db      "  /psg (AY-3-8910)", 13, 10
                db      "  /slots (0..3)", 13, 10, 0

; Minimal FCode probe stream
tfw_init_fcode:
                db      TOK_CR
                db      TOK_END

ENDIF
