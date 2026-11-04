\ ──────────────────────────────────────────────────────────────────
\ mandelbrot.fth — Mandelbrot Fractal Demo for B-System ANS Forth
\ Computes fixed-point complex Mandelbrot set and renders to
\ native BTRON window raster surface and terminal.
\ ──────────────────────────────────────────────────────────────────

320 CONSTANT SCREEN-W
240 CONSTANT SCREEN-H
32  CONSTANT MAX-ITER
1000 CONSTANT F-SCALE
4000 CONSTANT ESCAPE-LIMIT

VARIABLE WND
VARIABLE C-REAL
VARIABLE C-IMAG
VARIABLE Z-REAL
VARIABLE Z-IMAG
VARIABLE Z-REAL2
VARIABLE Z-IMAG2
VARIABLE ITER-COUNT
VARIABLE PIX-COLOR

\ Palette calculation: map iteration count to 32-bit ARGB
: CALC-COLOR ( iter -- color )
  DUP MAX-ITER = IF
    DROP 0 0 0 RGB
  ELSE
    DUP 8 * 255 AND
    OVER 14 * 255 AND
    ROT 22 * 60 + 255 AND
    RGB
  THEN
;

\ Compute iteration count for current C-REAL and C-IMAG
: MANDEL-PIXEL ( -- iter )
  0 Z-REAL !
  0 Z-IMAG !
  0 ITER-COUNT !
  BEGIN
    Z-REAL @ DUP * F-SCALE / Z-REAL2 !
    Z-IMAG @ DUP * F-SCALE / Z-IMAG2 !
    Z-REAL2 @ Z-IMAG2 @ + ESCAPE-LIMIT >
    ITER-COUNT @ MAX-ITER = OR IF
      ITER-COUNT @
      1
    ELSE
      \ Z-IMAG = 2 * Z-REAL * Z-IMAG / F-SCALE + C-IMAG
      Z-REAL @ Z-IMAG @ * 2 * F-SCALE / C-IMAG @ + Z-IMAG !
      \ Z-REAL = Z-REAL2 - Z-IMAG2 + C-REAL
      Z-REAL2 @ Z-IMAG2 @ - C-REAL @ + Z-REAL !
      1 ITER-COUNT +!
      0
    THEN
  UNTIL
;

\ Main Render Word
: MANDELBROT
  CR
  SCREEN-W SCREEN-H WND-OPEN WND !
  WND @ 0= IF
    \ Fallback if GUI window not supported in headless mode
  THEN

  \ Coordinate bounds:
  \ Real: -2.1 to +0.9 (span 3.0, center -0.6)
  \ Imag: -1.2 to +1.2 (span 2.4, center 0.0)
  SCREEN-H 0 DO
    \ C-IMAG = -1200 + (I * 2400) / SCREEN-H
    I 2400 * SCREEN-H / 1200 - C-IMAG !

    SCREEN-W 0 DO
      \ C-REAL = -2100 + (I * 3000) / SCREEN-W
      I 3000 * SCREEN-W / 2100 - C-REAL !

      MANDEL-PIXEL
      DUP CALC-COLOR
      I J WND @ WND-PIXEL

      \ ASCII preview on terminal every 8th pixel
      I 8 MOD 0= IF
        MAX-ITER = IF
          42 EMIT  \ '*'
        ELSE
          32 EMIT  \ ' '
        THEN
      ELSE
        DROP
      THEN
    LOOP

    \ Refresh terminal line and update window every 16 rows
    CR
    I 15 AND 0= IF
      WND @ WND-UPDATE
    THEN
  LOOP

  WND @ WND-UPDATE
  CR
;

MANDELBROT
