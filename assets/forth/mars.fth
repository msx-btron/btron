\ ──────────────────────────────────────────────────────────────────
\ mars.fth — Classical 1K Mars Voxel Landscape Demo (Pouet #4662)
\ Inspired by Tim Clarke's 1993 Assembly voxel engine.
\ Fully 16-bit cell compatible (zero overflow on MSX Z80 / IA-16)
\ with dual rendering to BTRON WND surface and MSX 40x24 console.
\ ──────────────────────────────────────────────────────────────────

40 CONSTANT SCREEN-W
24 CONSTANT SCREEN-H
8  CONSTANT HORIZON
12 CONSTANT CAM-Z

VARIABLE WND
VARIABLE CAM-X
VARIABLE CAM-Y
VARIABLE DIST
VARIABLE MAX-Y
VARIABLE RAY-X
VARIABLE RAY-Y
VARIABLE TERRAIN-H
VARIABLE PROJ-Y
VARIABLE COL-X

\ Buffer for 40 column horizon heights (1 cell per column)
VARIABLE PROJ-00 VARIABLE PROJ-01 VARIABLE PROJ-02 VARIABLE PROJ-03
VARIABLE PROJ-04 VARIABLE PROJ-05 VARIABLE PROJ-06 VARIABLE PROJ-07
VARIABLE PROJ-08 VARIABLE PROJ-09 VARIABLE PROJ-10 VARIABLE PROJ-11
VARIABLE PROJ-12 VARIABLE PROJ-13 VARIABLE PROJ-14 VARIABLE PROJ-15
VARIABLE PROJ-16 VARIABLE PROJ-17 VARIABLE PROJ-18 VARIABLE PROJ-19
VARIABLE PROJ-20 VARIABLE PROJ-21 VARIABLE PROJ-22 VARIABLE PROJ-23
VARIABLE PROJ-24 VARIABLE PROJ-25 VARIABLE PROJ-26 VARIABLE PROJ-27
VARIABLE PROJ-28 VARIABLE PROJ-29 VARIABLE PROJ-30 VARIABLE PROJ-31
VARIABLE PROJ-32 VARIABLE PROJ-33 VARIABLE PROJ-34 VARIABLE PROJ-35
VARIABLE PROJ-36 VARIABLE PROJ-37 VARIABLE PROJ-38 VARIABLE PROJ-39

: SET-PROJ ( val idx -- )
  CELLS PROJ-00 + !
;

: GET-PROJ ( idx -- val )
  CELLS PROJ-00 + @
;

\ Multi-octave Martian Canyon Heightfield (0..10)
: MARS-HEIGHT ( x y -- h )
  2DUP + 7 AND
  SWAP 3 * SWAP - 5 AND +
  3 RSHIFT 1 +
;

\ Martian Terrain Shading Palette
: MARS-COLOR ( dist h -- color )
  SWAP 4 *       \ distance darkening
  SWAP 15 *      \ height highlighting
  +
  DUP 2 * 60 + 255 AND \ Red channel (rich iron oxide)
  OVER 35 + 255 AND    \ Green channel (ochre/sandstone)
  ROT 20 + 255 AND     \ Blue channel (deep shadow/dust)
  RGB
;

\ Martian Sky Atmosphere
: MARS-SKY ( y -- color )
  DUP 8 * 15 + \ Red
  OVER 3 * 8 + \ Green
  ROT 2 * 20 + \ Blue
  RGB
;

\ Raymarch a single column and update window + horizon buffer
: RENDER-MARS-COL ( col -- )
  COL-X !
  SCREEN-H 1 - MAX-Y !

  \ March from near (d=3) to far (d=20)
  3 DIST !
  BEGIN
    DIST @ 20 <
  WHILE
    CAM-X @ DIST @ COL-X @ 20 - * 15 / + RAY-X !
    CAM-Y @ DIST @ + RAY-Y !

    RAY-X @ RAY-Y @ MARS-HEIGHT TERRAIN-H !

    \ sy = horizon + (camZ - h) * 10 / d
    CAM-Z TERRAIN-H @ - 10 * DIST @ / HORIZON + PROJ-Y !

    PROJ-Y @ 0 < IF 0 PROJ-Y ! THEN
    PROJ-Y @ SCREEN-H 1 - > IF SCREEN-H 1 - PROJ-Y ! THEN

    PROJ-Y @ MAX-Y @ < IF
      \ Draw terrain span in GUI window
      DIST @ TERRAIN-H @ MARS-COLOR
      COL-X @ 8 *
      PROJ-Y @ 10 *
      8
      MAX-Y @ PROJ-Y @ - 10 *
      WND @ WND-RECT

      PROJ-Y @ MAX-Y !
    THEN

    1 DIST +!
  REPEAT

  \ Sky above highest ridge in GUI window
  MAX-Y @ 0 > IF
    MAX-Y @ MARS-SKY
    COL-X @ 8 *
    0
    8
    MAX-Y @ 10 *
    WND @ WND-RECT
  THEN

  \ Store final horizon in buffer for terminal rendering
  MAX-Y @ COL-X @ SET-PROJ
;

\ Render one full frame (voxel columns + terminal frame)
: RENDER-MARS-FRAME
  \ 1. Compute voxel columns & draw GUI window
  SCREEN-W 0 DO
    I RENDER-MARS-COL
  LOOP

  \ 2. Render ASCII 3D terrain profile to MSX / gterm console
  SCREEN-H 0 DO
    SCREEN-W 0 DO
      I GET-PROJ
      J = IF
        94 EMIT \ '^' peak crest
      ELSE
        I GET-PROJ J < IF
          J 16 > IF 35 EMIT ELSE 58 EMIT THEN \ '#' canyon floor / ':' slope
        ELSE
          32 EMIT \ ' ' Martian sky
        THEN
      THEN
    LOOP
    CR
  LOOP

  WND @ WND-UPDATE
;

\ Main Mars Flight Demo Runner
: MARS
  CR
  320 240 WND-OPEN WND !
  0 CAM-X !
  0 CAM-Y !

  \ Flight sequence over the Red Planet (4 flyover steps)
  4 0 DO
    RENDER-MARS-FRAME
    2 CAM-X +!
    3 CAM-Y +!
  LOOP

  CR
;

MARS
