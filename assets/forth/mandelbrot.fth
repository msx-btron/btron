\ ──────────────────────────────────────────────────────────────────
\ mandelbrot.fth — Classical Mandelbrot Fractal Explorer
\ Fully 16-bit cell compatible (zero overflow on MSX Z80 / IA-16).
\ In CLU mode (display processor in dev-tree), renders to BTRON WND
\ and captures interactive navigation (Left/Right/Up/Down/WASD pan,
\ Space reset, 'q'/ESC quit). Fallback to ASCII when headless.
\ ──────────────────────────────────────────────────────────────────

40 CONSTANT SCREEN-W
24 CONSTANT SCREEN-H
15 CONSTANT MAX-ITER
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
VARIABLE CELL-W
VARIABLE CELL-H
VARIABLE PAN-X
VARIABLE PAN-Y
VARIABLE RUNNING

8  CELL-W !
10 CELL-H !
0  PAN-X !
0  PAN-Y !
1  RUNNING !

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

\ Mandelbrot pixel iterator for point (cr, ci)
: MANDEL-PIXEL ( cr ci -- iter )
  0 Z-REAL !
  0 Z-IMAG !
  0 ITER-COUNT !

  BEGIN
    \ zr2 = (zr * zr) / 40
    Z-REAL @ DUP * F-SCALE / Z-REAL2 !
    \ zi2 = (zi * zi) / 40
    Z-IMAG @ DUP * F-SCALE / Z-IMAG2 !

    \ check escape: zr2 + zi2 > 160 (i.e. |z|^2 > 4.0)
    Z-REAL2 @ Z-IMAG2 @ + ESCAPE-LIMIT > IF
      ITER-COUNT @
      1
    ELSE
      1 ITER-COUNT +!
      ITER-COUNT @ MAX-ITER = IF
        MAX-ITER
        1
      ELSE
        \ zi_next = (2 * zr * zi) / 40 + ci
        Z-REAL @ Z-IMAG @ * 20 / C-IMAG @ +
        \ zr_next = zr2 - zi2 + cr
        Z-REAL2 @ Z-IMAG2 @ - C-REAL @ +
        Z-REAL !
        Z-IMAG !
        0
      THEN
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
  CELL-X @ CELL-W @ *
  CELL-Y @ CELL-H @ *
  CELL-W @ CELL-H @
  WND @ WND-RECT

  \ Draw character on console terminal ONLY IF display processor is not in dev-tree
  DISPLAY? 0= IF
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
  THEN
;

\ Update cell dimensions based on current window size
: UPDATE-DIMS
  WND @ WND-SIZE
  SWAP SCREEN-W / DUP 1 < IF DROP 1 THEN CELL-W !
  SCREEN-H / DUP 1 < IF DROP 1 THEN CELL-H !
;

\ Window repaint callback
: MANDELBROT-PAINT
  UPDATE-DIMS
  SCREEN-H 0 DO
    I 4 * 48 - PAN-Y @ + C-IMAG !
    SCREEN-W 0 DO
      I 3 * 80 - PAN-X @ + C-REAL !
      I J MANDEL-PIXEL DRAW-CELL
    LOOP
    DISPLAY? 0= IF CR THEN
  LOOP
  WND @ WND-UPDATE
;

\ Process interactive input:
\ Left (28/a/A): Pan Left
\ Right (29/d/D): Pan Right
\ Up (30/w/W): Pan Up
\ Down (31/s/S): Pan Down
\ Space (32): Reset View
\ 'q' / 'Q' / ESC (27): Quit
: PROCESS-MANDEL-INPUT
  KEY? IF
    KEY

    \ Left Arrow or 'a' or 'A'
    DUP 28 = OVER 97 = OR OVER 65 = OR IF
      -4 PAN-X +!
      MANDELBROT-PAINT
    THEN

    \ Right Arrow or 'd' or 'D'
    DUP 29 = OVER 100 = OR OVER 68 = OR IF
      4 PAN-X +!
      MANDELBROT-PAINT
    THEN

    \ Up Arrow or 'w' or 'W'
    DUP 30 = OVER 119 = OR OVER 87 = OR IF
      -4 PAN-Y +!
      MANDELBROT-PAINT
    THEN

    \ Down Arrow or 's' or 'S'
    DUP 31 = OVER 115 = OR OVER 83 = OR IF
      4 PAN-Y +!
      MANDELBROT-PAINT
    THEN

    \ Space: Reset to Center
    DUP 32 = IF
      0 PAN-X !
      0 PAN-Y !
      MANDELBROT-PAINT
    THEN

    \ 'q' or 'Q' or ESC
    DUP 27 = OVER 113 = OR OVER 81 = OR IF
      0 RUNNING !
    THEN

    DROP
  THEN
;

VARIABLE LAST-W
VARIABLE LAST-H

: CHECK-RESIZE
  WND @ WND-SIZE
  DUP LAST-H @ <> SWAP LAST-W @ <> OR IF
    WND @ WND-SIZE LAST-H ! LAST-W !
    MANDELBROT-PAINT
  THEN
;

\ Interactive Navigation Loop
: MANDEL-INTERACTIVE
  1 RUNNING !
  WND @ WND-SIZE LAST-H ! LAST-W !
  BEGIN
    RUNNING @
  WHILE
    CHECK-RESIZE
    PROCESS-MANDEL-INPUT
    20 MS
  REPEAT
;

\ Main Render Word
: MANDELBROT
  DISPLAY? 0= IF CR THEN
  320 240 WND-OPEN WND !
  WND @ ' MANDELBROT-PAINT WND-ON-PAINT
  0 PAN-X !
  0 PAN-Y !

  MANDELBROT-PAINT

  DISPLAY? IF
    MANDEL-INTERACTIVE
  THEN
  DISPLAY? 0= IF CR THEN
;

MANDELBROT
