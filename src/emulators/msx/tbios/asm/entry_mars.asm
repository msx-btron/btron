; ==============================================================================
; MARS 1993 Voxel Landscape — MSX Cartridge ROM (32 KiB)
; Target: MSX2 / MSX2+ / MSX turbo R Cartridge (Page 1: $4000..$BFFF)
; Assembler: Pasmo Z80
;
; Screen 8 (Graphic 7, 256x212, 8bpp GGGRRRBB)
; Displays calibrated Tim Clarke 1993 Martian landscape with real-time
; hardware vertical and horizontal scrolling via V9938 registers.
; ==============================================================================

                org     $4000

; ── MSX Cartridge Header ───────────────────────────────────────────────
                db      "AB"                    ; Cartridge ROM signature ($41, $42)
                dw      mars_start              ; INIT entry point ($4002)
                dw      mars_start              ; EXEC entry point ($4004)
                dw      0                       ; STATEMENT (none)
                dw      0                       ; DEVICE (none)
                dw      0                       ; TEXT (none)
                dw      0                       ; Reserved

; ── RAM Variables (MSX Page 3: $C000..$FFFF) ─────────────────────────
RAM_SCROLL_Y    equ     $C000
RAM_SCROLL_X    equ     $C001

; ── Cartridge Entry Point ──────────────────────────────────────────────
mars_start:
                di
                ld      sp, $EFFF               ; Stack below system variables

                ; Switch to Screen 8 (Graphic 7)
                call    init_screen8

                ; Decompress calibrated 256x200 Martian landscape into VRAM
                call    load_mars_frame

                ; Enable display
                ld      a, $60                  ; Display active, 16K VRAM
                ld      b, 1
                call    set_vdp_reg

                ; Initialize scroll offsets in RAM
                xor     a
                ld      (RAM_SCROLL_Y), a
                ld      (RAM_SCROLL_X), a

; ── Interactive Main Loop ──────────────────────────────────────────────
mars_loop:
                ; 1. Read keyboard row 8 (cursor keys: Space, Right, Down, Up, Left)
                ld      a, 8
                out     ($AA), a
                in      a, ($A9)
                ld      c, a                    ; Active low: bit 7=Right, 6=Down, 5=Up, 4=Left, 0=Space

                ; 2. Read Joystick Port 1 via PSG (active low: bit 0=Up, 1=Down, 2=Left, 3=Right)
                ld      a, 7
                out     ($A0), a
                in      a, ($A2)
                and     $BF                     ; Bit 6 = 0: Port A input mode
                out     ($A1), a

                ld      a, 15
                out     ($A0), a
                ld      a, $0F                  ; Select joypad 1 (bit 6 = 0)
                out     ($A1), a
                ld      a, 14
                out     ($A0), a
                in      a, ($A2)
                ld      d, a                    ; Joystick bits

                ; Check Up: Keyboard row 8 bit 5 OR Joypad bit 0
                bit     5, c
                jr      z, .move_up
                bit     0, d
                jr      nz, .check_down
.move_up:
                ld      a, (RAM_SCROLL_Y)
                cp      $06                     ; Max upward perspective shift (+6)
                jr      nc, .check_horizontal
                inc     a
                ld      (RAM_SCROLL_Y), a
                jr      .check_horizontal

.check_down:
                ; Check Down: Keyboard row 8 bit 6 OR Joypad bit 1
                bit     6, c
                jr      z, .move_down
                bit     1, d
                jr      nz, .check_horizontal
.move_down:
                ld      a, (RAM_SCROLL_Y)
                or      a                       ; Clamped to 0 (no wrapping or black gaps)
                jr      z, .check_horizontal
                dec     a
                ld      (RAM_SCROLL_Y), a

.check_horizontal:
                ; Check Left: Keyboard row 8 bit 4 OR Joypad bit 2
                bit     4, c
                jr      z, .move_left
                bit     2, d
                jr      nz, .check_right
.move_left:
                ld      a, (RAM_SCROLL_X)
                dec     a
                ld      (RAM_SCROLL_X), a
                jr      .apply_scroll

.check_right:
                ; Check Right: Keyboard row 8 bit 7 OR Joypad bit 3
                bit     7, c
                jr      z, .move_right
                bit     3, d
                jr      nz, .apply_scroll
.move_right:
                ld      a, (RAM_SCROLL_X)
                inc     a
                ld      (RAM_SCROLL_X), a

.apply_scroll:
                ; Update V9938 R#23 (Vertical scroll offset)
                ld      a, (RAM_SCROLL_Y)
                ld      b, 23
                call    set_vdp_reg

                ; Update V9958 R#26 (Horizontal scroll high: 8-pixel units)
                ld      a, (RAM_SCROLL_X)
                srl     a
                srl     a
                srl     a
                and     $3F
                ld      b, 26
                call    set_vdp_reg

                ; Update V9958 R#27 (Horizontal scroll low 3 bits: 0..7 px)
                ld      a, (RAM_SCROLL_X)
                and     $07
                ld      b, 27
                call    set_vdp_reg

                ; Frame pacing delay
                ld      b, 10
.delay:
                ld      d, 255
.dloop:         dec     d
                jr      nz, .dloop
                djnz    .delay

                jp      mars_loop

; ── V9938 Screen 8 Initialization ──────────────────────────────────────
init_screen8:
                ; R#0: $0E (Screen 8: M5=1, M4=1, M3=1)
                ld      a, $0E
                ld      b, 0
                call    set_vdp_reg

                ; R#1: $40 (Display off during load)
                ld      a, $40
                ld      b, 1
                call    set_vdp_reg

                ; R#2: $1F (Pattern layout table base)
                ld      a, $1F
                ld      b, 2
                call    set_vdp_reg

                ; R#8: $08 (Color bus enable)
                ld      a, $08
                ld      b, 8
                call    set_vdp_reg

                ; R#9: $80 (212 lines, non-interlace)
                ld      a, $80
                ld      b, 9
                call    set_vdp_reg

                ; R#23: $00 (Vertical scroll reset)
                xor     a
                ld      b, 23
                call    set_vdp_reg

                ; R#26: $00 (Horizontal scroll high reset)
                xor     a
                ld      b, 26
                call    set_vdp_reg

                ; R#27: $00 (Horizontal scroll low reset)
                xor     a
                ld      b, 27
                call    set_vdp_reg
                ret

; Set VDP Register: A = Value, B = Register number (0..63)
set_vdp_reg:
                out     ($99), a
                ld      a, b
                or      $80                     ; Bit 7 = Register write
                out     ($99), a
                ret

; ── Load Calibrated Martian Frame into VRAM ────────────────────────────
load_mars_frame:
                ; Set VRAM Base Address register R#14 to 0
                xor     a
                out     ($99), a
                ld      a, $8E                  ; R#14
                out     ($99), a

                ; Set VRAM write address 0x0000
                xor     a
                out     ($99), a
                ld      a, $40                  ; Write mode ($4000 mask)
                out     ($99), a

                ; Stream RLE data
                ld      hl, mars_rle_data
.unpack:
                ld      a, (hl)                 ; Run length
                or      a
                jr      z, .fill_rest           ; 0 = End of stream
                inc     hl
                ld      b, a
                ld      a, (hl)                 ; Pixel value
                inc     hl
.pixel_loop:
                out     ($98), a
                djnz    .pixel_loop
                jr      .unpack

.fill_rest:
                ; Clear bottom margins (lines 200..211) with canyon base floor
                ld      bc, 256 * 12
.fill_lp:
                ld      a, $10
                out     ($98), a
                dec     bc
                ld      a, b
                or      c
                jr      nz, .fill_lp
                ret

; ── Include Calibrated Landscape RLE Stream ────────────────────────────
                include "mars_rle.inc"

; Pad ROM to exactly 32 KiB ($8000 bytes from $4000 to $C000)
                ds      $C000 - $
