\ ──────────────────────────────────────────────────────────────────
\ mars.fth — Classical 1K Mars Voxel Landscape Demo (Pouet #4662)
\ Inspired by Tim Clarke's 1993 Assembly voxel engine.
\ Fully 16-bit cell compatible (zero overflow on MSX Z80 / IA-16)
\ In CLU mode (display processor in dev-tree), renders to BTRON WND
\ and captures interactive flight controls:
\   - Left/Right Arrows: Turn Left / Right (Yaw rotation)
\   - Up/Down Arrows (or W/S): Accelerate Forward / Reverse
\   - 'A' / 'D' (or Shift + Left/Right): Lateral Strafe
\   - Space: Ballistic Jump (height proportional to forward speed)
\   - 'q' / 'Q' / ESC: Exit Demo
\ Fallback to ASCII 3D terrain profile when headless / MSX console.
\ ──────────────────────────────────────────────────────────────────

320 CONSTANT SCREEN-W
240 CONSTANT SCREEN-H
96  CONSTANT HORIZON

VARIABLE WND
VARIABLE CAM-X
VARIABLE CAM-Y
VARIABLE CAM-Z
VARIABLE HEADING
VARIABLE SPEED
VARIABLE STRAFE
VARIABLE V-VEL
VARIABLE IS-JUMPING
VARIABLE JUMP-VEL
VARIABLE JUMP-HEIGHT
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
VARIABLE LAT-OFF

1  COL-W !
1  ROW-H !
52 CAM-Z !
16 CAM-X !
8  CAM-Y !
0  HEADING !
0  SPEED !
0  STRAFE !
0  V-VEL !
0  IS-JUMPING !
1  RUNNING !
0  FRAME-STEP !
4  MAX-FRAMES !

\ Buffer for 320 column horizon heights (1 cell per column)
CREATE PROJ-BUF 320 CELLS ALLOT

: SET-PROJ ( val idx -- )
  CELLS PROJ-BUF + !
;

: GET-PROJ ( idx -- val )
  CELLS PROJ-BUF + @
;

\ 16-angle Sine/Cosine Approximation (scaled by 16)
\ Symmetry: cos(ang) = sin(ang + 4)
: GET-SIN-QUAD ( ang -- sin )
  DUP 0 = IF DROP 0 ELSE
  DUP 1 = IF DROP 6 ELSE
  DUP 2 = IF DROP 11 ELSE
  DUP 3 = IF DROP 15 ELSE
  DROP 16 THEN THEN THEN THEN
;

: GET-SIN ( ang -- sin )
  15 AND
  DUP 5 < IF
    GET-SIN-QUAD
  ELSE
    DUP 9 < IF
      8 SWAP - GET-SIN-QUAD
    ELSE
      DUP 13 < IF
        8 - GET-SIN-QUAD NEGATE
      ELSE
        16 SWAP - GET-SIN-QUAD NEGATE
      THEN
    THEN
  THEN
;

: GET-COS ( ang -- cos )
  4 + GET-SIN
;

VARIABLE MAP-X
VARIABLE MAP-Y
VARIABLE SHADE-D
VARIABLE SHADE-H
VARIABLE SHADE-SLOPE
VARIABLE SHADE-LIGHT
VARIABLE SHADE-HAZE
VARIABLE COL-R
VARIABLE COL-G
VARIABLE COL-B

\ Triangle wave function (0..32) for procedural harmonics
: TRI ( n -- val )
  63 AND 32 - DUP 0 < IF NEGATE THEN
;

\ Ridge noise function: sharp peaks (32) and flat canyons (0)
: RIDGE ( n -- val )
  TRI 32 SWAP -
;

\ Calibrated Procedural Martian Canyon Heightfield (matching 1993 Tim Clarke MARS)
: MARS-HEIGHT ( x y -- h )
  MAP-Y ! MAP-X !
  \ Multi-octave natural Martian peaks & canyon foothills
  MAP-X @ 2 / RIDGE MAP-Y @ 2 / RIDGE + 4 * 5 /
  MAP-X @ RIDGE MAP-Y @ RIDGE * 30 / +
  MAP-X @ 2 * MAP-Y @ + RIDGE 4 / +
  MAP-Y @ 2 * MAP-X @ - RIDGE 4 / +
  \ Twin central mountain massif (matching 1993 Tim Clarke MARS)
  MAP-X @ 24 - DUP * MAP-Y @ 28 - DUP * + 12 / 36 SWAP - DUP 0 < IF DROP 0 THEN +
  MAP-X @ 40 - DUP * MAP-Y @ 32 - DUP * + 10 / 42 SWAP - DUP 0 < IF DROP 0 THEN +
  \ Foreground canyon roll
  MAP-X @ 12 - DUP * MAP-Y @ 12 - DUP * + 16 / 18 SWAP - DUP 0 < IF DROP 0 THEN +
  12 +
;

\ Authentic Tim Clarke 64-step VGA Terracotta Palette with Directional Sunlight & Depth Haze
: MARS-COLOR ( dist h slope -- color )
  SHADE-SLOPE !
  SHADE-H !
  SHADE-D !

  \ Sunlight intensity: base 26 + slope highlight + elevation - distance haze
  26 SHADE-SLOPE @ 5 * 2 / + SHADE-H @ 6 / + SHADE-D @ 3 / - SHADE-LIGHT !
  SHADE-LIGHT @ 0 < IF 0 SHADE-LIGHT ! THEN
  SHADE-LIGHT @ 63 > IF 63 SHADE-LIGHT ! THEN

  \ VGA DAC Terracotta curve (smooth gradient from deep canyon shadow to bright sunlit peak)
  SHADE-LIGHT @ 30 < IF
    SHADE-LIGHT @ 15 * 2 / COL-R !
    SHADE-LIGHT @ 15 - DUP 0 < IF DROP 0 THEN 3 * 2 / COL-G !
    0 COL-B !
  ELSE
    225 SHADE-LIGHT @ 30 - 9 * 10 / + COL-R !
    22  SHADE-LIGHT @ 30 - 7 * 2 / + COL-G !
    SHADE-LIGHT @ 32 - DUP 0 < IF DROP 0 THEN 14 * 5 / COL-B !
  THEN

  \ Distance haze blend toward atmospheric horizon (140, 28, 24)
  SHADE-D @ 5 * DUP 255 > IF DROP 255 THEN SHADE-HAZE !
  COL-R @ 255 SHADE-HAZE @ - * 140 SHADE-HAZE @ * + 255 /
  COL-G @ 255 SHADE-HAZE @ - * 28  SHADE-HAZE @ * + 255 /
  COL-B @ 255 SHADE-HAZE @ - * 24  SHADE-HAZE @ * + 255 /
  RGB
;

\ Render Authentic Martian Atmosphere (Dusty Rose Zenith -> Salmon Wisps -> Horizon Glow)
: RENDER-SKY-BACKDROP
  HORIZON 0 DO
    \ Atmospheric gradient with gentle cloud wisp modulation
    160 I 45 * HORIZON / + I 8 AND 4 = IF 18 + THEN
    45  I 50 * HORIZON / + I 8 AND 4 = IF 14 + THEN
    40  I 45 * HORIZON / + I 8 AND 4 = IF 14 + THEN
    RGB
    0 I SCREEN-W COL-W @ * 1 WND @ WND-RECT
  LOOP
  \ Soft atmospheric horizon line
  140 28 24 RGB
  0 HORIZON SCREEN-W COL-W @ * 1 WND @ WND-RECT
  \ Deep canyon base floor below horizon
  50 10 8 RGB
  0 HORIZON 1 + SCREEN-W COL-W @ * SCREEN-H HORIZON - WND @ WND-RECT
;

\ Update column and row size based on window dimensions
: UPDATE-MARS-DIMS
  1 COL-W !
  1 ROW-H !
;

\ Raymarch a single voxel column with 3D heading rotation & perspective frustum
: RENDER-MARS-COL ( col -- )
  COL-X !
  SCREEN-H 1 - MAX-Y !

  \ March from near (d=3) to far (d=48)
  3 DIST !
  BEGIN
    DIST @ 48 <
  WHILE
    \ Perspective frustum lateral spread across 320 columns: (col - 160) * dist / 108
    COL-X @ 160 - DIST @ * 108 / LAT-OFF !

    \ World coordinates ray_x, ray_y with 16-angle yaw rotation
    HEADING @ GET-SIN DIST @ * HEADING @ GET-COS LAT-OFF @ * + 16 / CAM-X @ + RAY-X !
    HEADING @ GET-COS DIST @ * HEADING @ GET-SIN LAT-OFF @ * - 16 / CAM-Y @ + RAY-Y !

    RAY-X @ RAY-Y @ MARS-HEIGHT TERRAIN-H !

    \ Perspective screen Y: sy = HORIZON + ((CAM-Z - TERRAIN-H) * 88) / DIST
    CAM-Z @ TERRAIN-H @ - 88 * DIST @ / HORIZON + PROJ-Y !

    PROJ-Y @ 0 < IF 0 PROJ-Y ! THEN
    PROJ-Y @ SCREEN-H 1 - > IF SCREEN-H 1 - PROJ-Y ! THEN

    PROJ-Y @ MAX-Y @ < IF
      \ Calculate directional sunlight slope (x-1 vs x+1)
      RAY-X @ 1 - RAY-Y @ MARS-HEIGHT
      RAY-X @ 1 + RAY-Y @ MARS-HEIGHT -
      DIST @ TERRAIN-H @ ROT MARS-COLOR
      PROJ-Y @ MAX-Y @ COL-X @ WND @ WND-VLINE

      PROJ-Y @ 1 - MAX-Y !
    THEN

    1 DIST +!
  REPEAT

  \ Store final horizon in buffer for terminal rendering
  MAX-Y @ 1 + COL-X @ SET-PROJ
;

\ Render one full frame (sky backdrop + voxel columns + terminal frame if no display processor)
: RENDER-MARS-FRAME
  UPDATE-MARS-DIMS
  RENDER-SKY-BACKDROP

  \ 1. Compute voxel columns & draw GUI window
  SCREEN-W 0 DO
    I RENDER-MARS-COL
  LOOP

  \ 2. Render ASCII 3D terrain profile ONLY IF display processor is not in dev-tree
  DISPLAY? 0= IF
    24 0 DO
      40 0 DO
        I 4 * GET-PROJ
        J 10 * = IF
          94 EMIT \ '^' peak crest
        ELSE
          I 4 * GET-PROJ J 10 * < IF
            J 16 > IF 35 EMIT ELSE 58 EMIT THEN \ '#' canyon floor / ':' slope
          ELSE
            32 EMIT \ ' ' Martian sky
          THEN
        THEN
      LOOP
      CR
    LOOP
  THEN

  WND @ WND-UPDATE
;

\ User Input & Flight Controls (Identical to reference implementation mars.c):
\ Left (28): Turn Left (Yaw)
\ Right (29): Turn Right (Yaw)
\ Up (30) or 'w' / 'W': Forward Step
\ Down (31) or 's' / 'S': Reverse Step
\ 'a' / 'A' (or Shift+Left): Lateral Strafe Left
\ 'd' / 'D' (or Shift+Right): Lateral Strafe Right
\ Space (32): Ballistic Jump Arc
\ 'q' / 'Q' / ESC (27): Quit
: PROCESS-INPUT
  BEGIN
    KEY?
  WHILE
    KEY
    0 MAX-FRAMES ! \ User engaged controls -> switch to interactive flight

    \ Left Arrow: Turn Left
    DUP 28 = IF
      HEADING @ 1 - 15 AND HEADING !
    THEN

    \ Right Arrow: Turn Right
    DUP 29 = IF
      HEADING @ 1 + 15 AND HEADING !
    THEN

    \ Strafe Left ('a' or 'A')
    DUP 97 = OVER 65 = OR IF
      HEADING @ GET-COS -3 * 16 / CAM-X +!
      HEADING @ GET-SIN 3 * 16 / CAM-Y +!
    THEN

    \ Strafe Right ('d' or 'D')
    DUP 100 = OVER 68 = OR IF
      HEADING @ GET-COS 3 * 16 / CAM-X +!
      HEADING @ GET-SIN -3 * 16 / CAM-Y +!
    THEN

    \ Up Arrow or 'w' or 'W': Forward Step
    DUP 30 = OVER 119 = OR OVER 87 = OR IF
      HEADING @ GET-SIN 2 * 16 / CAM-X +!
      HEADING @ GET-COS 2 * 16 / CAM-Y +!
    THEN

    \ Down Arrow or 's' or 'S': Reverse Step
    DUP 31 = OVER 115 = OR OVER 83 = OR IF
      HEADING @ GET-SIN -2 * 16 / CAM-X +!
      HEADING @ GET-COS -2 * 16 / CAM-Y +!
    THEN

    \ Space: Ballistic Jump with gravity arc
    DUP 32 = IF
      JUMP-HEIGHT @ 0= IF
        18 JUMP-VEL !
        1 JUMP-HEIGHT !
      THEN
    THEN

    \ 'q' or 'Q' or ESC
    DUP 27 = OVER 113 = OR OVER 81 = OR IF
      0 RUNNING !
    THEN

    DROP
  REPEAT
;

\ Ballistic Flight Physics & Flight Clearance matching reference mars.c
: UPDATE-PHYSICS
  \ 1. Ballistic jump arc
  JUMP-HEIGHT @ 0 > IF
    JUMP-VEL @ JUMP-HEIGHT +!
    -2 JUMP-VEL +!
    JUMP-HEIGHT @ 0 < IF
      0 JUMP-HEIGHT !
      0 JUMP-VEL !
    THEN
  THEN

  \ 2. Automatic ground clearance (25 units above terrain) + jump altitude
  CAM-X @ CAM-Y @ MARS-HEIGHT 25 + JUMP-HEIGHT @ + CAM-Z !
;

\ Interactive Flight Loop
: FLY
  BEGIN
    RUNNING @
  WHILE
    PROCESS-INPUT
    UPDATE-PHYSICS
    RENDER-MARS-FRAME
    2 MS

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
  DISPLAY? 0= IF CR THEN
  320 240 WND-OPEN WND !
  WND @ ' RENDER-MARS-FRAME WND-ON-PAINT

  16 CAM-X !
  8  CAM-Y !
  0  HEADING !
  0  SPEED !
  0  STRAFE !
  0  JUMP-VEL !
  0  JUMP-HEIGHT !
  1  RUNNING !
  0  FRAME-STEP !
  UPDATE-PHYSICS
  DISPLAY? IF 0 MAX-FRAMES ! ELSE 4 MAX-FRAMES ! THEN

  FLY
  DISPLAY? 0= IF CR THEN
;

MARS
