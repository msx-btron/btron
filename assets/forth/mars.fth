\ ──────────────────────────────────────────────────────────────────
\ mars.fth — Classical 1K Mars Voxel Landscape Demo (Pouet #4662)
\ Inspired by Tim Clarke's 1993 Assembly voxel engine.
\ Fully 16-bit cell compatible (zero overflow on MSX Z80 / IA-16)
\ with dual rendering to BTRON WND surface and MSX 40x24 console.
\ Interactive Flight: Left/Right (Turn), Up/Down (Speed),
\ Space (Ballistic Jump based on forward velocity), 'q'/ESC (Exit).
\ ──────────────────────────────────────────────────────────────────

40 CONSTANT SCREEN-W
24 CONSTANT SCREEN-H
8  CONSTANT HORIZON

VARIABLE WND
VARIABLE CAM-X
VARIABLE CAM-Y
VARIABLE CAM-Z
VARIABLE HEADING
VARIABLE SPEED
VARIABLE V-VEL
VARIABLE IS-JUMPING
VARIABLE RUNNING
VARIABLE FRAME-STEP
VARIABLE MAX-FRAMES
VARIABLE COL-W
VARIABLE ROW-H

VARIABLE DIST
VARIABLE MAX-Y
VARIABLE RAY-X
VARIABLE RAY-Y
VARIABLE TERRAIN-H
VARIABLE PROJ-Y
VARIABLE COL-X

8  COL-W !
10 ROW-H !
12 CAM-Z !
0  CAM-X !
0  CAM-Y !
0  HEADING !
2  SPEED !
0  V-VEL !
0  IS-JUMPING !
1  RUNNING !
0  FRAME-STEP !
4  MAX-FRAMES !

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

\ Update column and row size based on window dimensions
: UPDATE-MARS-DIMS
  WND @ WND-SIZE
  SWAP SCREEN-W / DUP 1 < IF DROP 1 THEN COL-W !
  SCREEN-H / DUP 1 < IF DROP 1 THEN ROW-H !
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
    CAM-Z @ TERRAIN-H @ - 10 * DIST @ / HORIZON + PROJ-Y !

    PROJ-Y @ 0 < IF 0 PROJ-Y ! THEN
    PROJ-Y @ SCREEN-H 1 - > IF SCREEN-H 1 - PROJ-Y ! THEN

    PROJ-Y @ MAX-Y @ < IF
      \ Draw terrain span in GUI window
      DIST @ TERRAIN-H @ MARS-COLOR
      COL-X @ COL-W @ *
      PROJ-Y @ ROW-H @ *
      COL-W @
      MAX-Y @ PROJ-Y @ - ROW-H @ *
      WND @ WND-RECT

      PROJ-Y @ MAX-Y !
    THEN

    1 DIST +!
  REPEAT

  \ Sky above highest ridge in GUI window
  MAX-Y @ 0 > IF
    MAX-Y @ MARS-SKY
    COL-X @ COL-W @ *
    0
    COL-W @
    MAX-Y @ ROW-H @ *
    WND @ WND-RECT
  THEN

  \ Store final horizon in buffer for terminal rendering
  MAX-Y @ COL-X @ SET-PROJ
;

\ Render one full frame (voxel columns + terminal frame)
: RENDER-MARS-FRAME
  UPDATE-MARS-DIMS

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

\ User Input & Flight Controls:
\ Left (28/a/A): Turn Left
\ Right (29/d/D): Turn Right
\ Up (30/w/W): Speed Up
\ Down (31/s/S): Speed Down
\ Space (32): Ballistic Jump proportional to forward velocity
\ 'q' / 'Q' / ESC (27): Quit
: PROCESS-INPUT
  KEY? IF
    KEY
    0 MAX-FRAMES ! \ User engaged controls -> switch to interactive flight

    \ Left Arrow or 'a' or 'A'
    DUP 28 = OVER 97 = OR OVER 65 = OR IF
      -1 HEADING +!
    THEN

    \ Right Arrow or 'd' or 'D'
    DUP 29 = OVER 100 = OR OVER 68 = OR IF
      1 HEADING +!
    THEN

    \ Up Arrow or 'w' or 'W'
    DUP 30 = OVER 119 = OR OVER 87 = OR IF
      SPEED @ 5 < IF 1 SPEED +! THEN
    THEN

    \ Down Arrow or 's' or 'S'
    DUP 31 = OVER 115 = OR OVER 83 = OR IF
      SPEED @ 0 > IF -1 SPEED +! THEN
    THEN

    \ Space: Ballistic Jump based on forward velocity
    DUP 32 = IF
      SPEED @ 2 / 3 + V-VEL !
      1 IS-JUMPING !
    THEN

    \ 'q' or 'Q' or ESC
    DUP 27 = OVER 113 = OR OVER 81 = OR IF
      0 RUNNING !
    THEN

    DROP
  THEN
;

\ Ballistic Flight Physics & Ground Collision
: UPDATE-PHYSICS
  \ 1. Horizontal forward navigation
  HEADING @ CAM-X +!
  SPEED @ CAM-Y +!

  \ 2. Vertical ballistic jump arc
  V-VEL @ CAM-Z +!
  -1 V-VEL +! \ Martian gravity pull

  \ 3. Terrain collision check (minimum clearance 4 units above canyon floor)
  CAM-X @ CAM-Y @ MARS-HEIGHT 4 +
  DUP CAM-Z @ > IF
    CAM-Z !
    0 V-VEL !
    0 IS-JUMPING !
  ELSE
    DROP
  THEN
;

\ Interactive Flight Loop
: FLY
  BEGIN
    RUNNING @
  WHILE
    PROCESS-INPUT
    UPDATE-PHYSICS
    RENDER-MARS-FRAME
    20 MS

    1 FRAME-STEP +!
    MAX-FRAMES @ 0 > IF
      FRAME-STEP @ MAX-FRAMES @ < 0= IF
        0 RUNNING !
      THEN
    THEN
  REPEAT
;

\ Main Mars Flight Demo Runner
: MARS
  CR
  320 240 WND-OPEN WND !
  WND @ ' RENDER-MARS-FRAME WND-ON-PAINT

  0 CAM-X !
  0 CAM-Y !
  12 CAM-Z !
  0 HEADING !
  2 SPEED !
  0 V-VEL !
  0 IS-JUMPING !
  1 RUNNING !
  0 FRAME-STEP !

  FLY
  CR
;

MARS
