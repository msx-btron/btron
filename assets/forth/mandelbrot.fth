\ ──────────────────────────────────────────────────────────────────
\ mandelbrot.fth — Mandelbrot Fractal Demo for B-System & MSX T-FW
\ Fully 16-bit signed cell compatible (zero overflow on Z80 / IA-16)
\ with dual rasterization to BTRON WND surface and MSX/gterm console.
\ ──────────────────────────────────────────────────────────────────

40 CONSTANT SCREEN-W
24 CONSTANT SCREEN-H
16 CONSTANT MAX-ITER
40 CONSTANT F-SCALE
160 CONSTANT ESCAPE-LIMIT

VARIABLE WND
VARIABLE C-REAL
VARIABLE C-IMAG
VARIABLE Z-REAL
VARIABLE Z-IMAG
VARIABLE Z-REAL2
VARIABLE Z-IMAG2
VARIABLE ITER-COUNT
VARIABLE CELL-X
VARIABLE CELL-Y
VARIABLE CELL-ITER

\ Palette calculation: map iteration count to 32-bit ARGB
: CALC-COLOR ( iter -- color )
  DUP MAX-ITER = IF
    DROP 0 0 0 RGB
  ELSE
    DUP 15 *
    OVER 10 *
    ROT 8 * 100 +
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

\ Draw pixel/cell on screen and terminal
: DRAW-CELL ( x y iter -- )
  CELL-ITER !
  CELL-Y !
  CELL-X !

  \ Draw rectangle on GUI window if present
  CELL-ITER @ CALC-COLOR
  CELL-X @ 8 *
  CELL-Y @ 10 *
  8 10
  WND @ WND-RECT

  \ Draw character on console terminal / MSX Screen 0
  CELL-ITER @ MAX-ITER = IF
    42 EMIT \ '*'
  ELSE
    CELL-ITER @ 6 > IF
      43 EMIT \ '+'
    ELSE
      CELL-ITER @ 2 > IF
        46 EMIT \ '.'
      ELSE
        32 EMIT \ ' '
      THEN
    THEN
  THEN
;

\ Main Render Word
: MANDELBROT
  CR
  320 240 WND-OPEN WND !

  \ Coordinate bounds (scaled by 40):
  \ Real: -80 to +40 (step 3 per column) -> -2.0 to +1.0
  \ Imag: -48 to +48 (step 4 per row)    -> -1.2 to +1.2
  SCREEN-H 0 DO
    I 4 * 48 - C-IMAG !

    SCREEN-W 0 DO
      I 3 * 80 - C-REAL !
      I J MANDEL-PIXEL DRAW-CELL
    LOOP

    CR
    WND @ WND-UPDATE
  LOOP

  WND @ WND-UPDATE
  CR
;

MANDELBROT
