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

                ; Initialize scroll offsets
                xor     a
                ld      (scroll_y), a
                ld      (scroll_x), a

; ── Interactive Main Loop ──────────────────────────────────────────────
mars_loop:
                ; Read keyboard row 8 (cursor keys: Space, Right, Down, Up, Left)
                ld      a, 8
                out     ($AA), a
                in      a, ($A9)
                ld      c, a                    ; Bit 7=Down, 6=Up, 5=Right, 4=Left, 0=Space

                ; Check Up key (bit 5 / bit 6)
                bit     5, c
                jr      nz, .check_down
                ld      a, (scroll_y)
                inc     a
                ld      (scroll_y), a
                jr      .apply_scroll

.check_down:
                bit     6, c
                jr      nz, .check_left
                ld      a, (scroll_y)
                dec     a
                ld      (scroll_y), a
                jr      .apply_scroll

.check_left:
                bit     4, c
                jr      nz, .check_right
                ld      a, (scroll_x)
                dec     a
                ld      (scroll_x), a
                jr      .apply_scroll

.check_right:
                bit     7, c
                jr      nz, .apply_scroll
                ld      a, (scroll_x)
                inc     a
                ld      (scroll_x), a

.apply_scroll:
                ; Update V9938 R#23 (Vertical scroll offset)
                ld      a, (scroll_y)
                ld      b, 23
                call    set_vdp_reg

                ; Update V9938 R#27 (Horizontal scroll low 3 bits)
                ld      a, (scroll_x)
                and     $07
                ld      b, 27
                call    set_vdp_reg

                ; Small frame delay
                ld      b, 20
.delay:
                ld      d, 255
.dloop:         dec     d
                jr      nz, .dloop
                djnz    .delay

                jr      mars_loop

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
                ret     z                       ; 0 = End of stream
                inc     hl
                ld      b, a
                ld      a, (hl)                 ; Pixel value
                inc     hl
.pixel_loop:
                out     ($98), a
                djnz    .pixel_loop
                jr      .unpack

; ── Variables ──────────────────────────────────────────────────────────
scroll_y:       db      0
scroll_x:       db      0

; ── Include Calibrated Landscape RLE Stream ────────────────────────────
                include "mars_rle.inc"

; Pad ROM to exactly 32 KiB ($8000 bytes from $4000 to $C000)
                ds      $C000 - $
