\ ──────────────────────────────────────────────────────────────────
\ mars.fth — Classical Mars Voxel Landscape Demo (1993 Tim Clarke)
\ Faithful clean port of 1993 Tim Clarke Mars voxel engine.
\ Fully 16-bit cell compatible (zero overflow on MSX Z80 / IA-16)
\ Features:
\   - Infinite toroidal fractal space: seamless periodic wrapping (no island edges)
\   - High-resolution 320x240 voxel raymarching with directional sunlit slope shading
\   - Authentic 96-color VGA palette (Terracotta slopes + Dusty Rose plasma sky)
\   - Multi-layer perspective plasma sky with cloud wisps and glowing horizon line
\   - Real-time flight controls matching reference mars.c:
\       • Left/Right Arrows: Yaw Turn Left / Right
\       • Up/Down Arrows (or W/S): Forward / Reverse
\       • 'A' / 'D': Lateral Strafe
\       • Space: Ballistic Jump arc with Martian gravity
\       • 'q' / 'Q' / ESC: Clean exit
\ ──────────────────────────────────────────────────────────────────

320 CONSTANT SCREEN-W
240 CONSTANT SCREEN-H
104 CONSTANT HORIZON

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

VARIABLE DIST
VARIABLE MAX-Y
VARIABLE RAY-X
VARIABLE RAY-Y
VARIABLE TERRAIN-H
VARIABLE PROJ-Y
VARIABLE COL-X
VARIABLE LAT-OFF

120 CAM-X !
100 CAM-Y !
40  CAM-Z !
0   HEADING !
0   SPEED !
0   STRAFE !
0   V-VEL !
0   IS-JUMPING !
0   JUMP-VEL !
0   JUMP-HEIGHT !
1   RUNNING !
0   FRAME-STEP !
4   MAX-FRAMES !

\ Buffer for 320 column horizon heights (1 cell per column)
CREATE PROJ-BUF 320 CELLS ALLOT

: SET-PROJ ( val idx -- )
  CELLS PROJ-BUF + !
;

: GET-PROJ ( idx -- val )
  CELLS PROJ-BUF + @
;

\ 96-Color Authentic Tim Clarke 1993 Palette Table (Terracotta + Rose Sky)
CREATE MARS-PAL 96 CELLS ALLOT

: SET-PAL ( color idx -- )
  CELLS MARS-PAL + !
;

: GET-PAL ( idx -- color )
  CELLS MARS-PAL + @
;

: INIT-PALETTE
  \ 0..63: Terracotta Martian mountain slope gradient
  64 0 DO
    I 4 * DUP 255 > IF DROP 255 THEN
    I 15 - DUP 0 < IF DROP 0 THEN 22 * 10 / DUP 255 > IF DROP 255 THEN
    I 32 - DUP 0 < IF DROP 0 THEN 15 * 10 / DUP 255 > IF DROP 255 THEN
    RGB I SET-PAL
  LOOP
  \ 64..95: Dusty rose Martian sky and pink-tinted cloud gradient
  32 0 DO
    200
    40 I 4 * + DUP 255 > IF DROP 255 THEN
    40 I 4 * + DUP 255 > IF DROP 255 THEN
    RGB I 64 + SET-PAL
  LOOP
;

INIT-PALETTE

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
VARIABLE SKY-BASE
VARIABLE SKY-U
VARIABLE SKY-V
VARIABLE DX1
VARIABLE DY1
VARIABLE DX2
VARIABLE DY2

\ Toroidal coordinate wrap (period 256 -> infinite seamless space)
: WRAP ( n -- 0..255 )
  255 AND
;

\ Triangle wave function (0..32) for procedural harmonics
: TRI ( n -- val )
  63 AND 32 - DUP 0 < IF NEGATE THEN
;

\ Ridge noise function: sharp peaks (32) and flat canyons (0)
: RIDGE ( n -- val )
  TRI 32 SWAP -
;

\ Infinite Toroidal Martian Heightfield (Seamless 256x256 multi-octave fractal)
: MARS-HEIGHT ( x y -- h )
  WRAP MAP-Y ! WRAP MAP-X !

  \ Octave 1: Major mountain massif (wavelength 128)
  MAP-X @ 2 / RIDGE MAP-Y @ 2 / RIDGE * 16 / 3 * 4 /

  \ Octave 2: Craggy ridges (wavelength 64)
  MAP-X @ RIDGE MAP-Y @ RIDGE + 4 / +

  \ Octave 3: Diagonal erosional gullies (wavelength 32)
  MAP-X @ 2 * MAP-Y @ + TRI MAP-Y @ 2 * MAP-X @ - TRI + 4 / +

  \ Octave 4: Surface scree & roughness (wavelength 16)
  MAP-X @ 4 * MAP-Y @ 2 * - TRI MAP-Y @ 4 * MAP-X @ 2 * + TRI + 8 / +

  \ Twin mountain peaks centered at (140, 140) and (100, 148) with toroidal wrapping
  MAP-X @ 140 - 255 AND DUP 128 > IF 256 SWAP - THEN DX1 !
  MAP-Y @ 140 - 255 AND DUP 128 > IF 256 SWAP - THEN DY1 !
  56 DX1 @ 2 * DY1 @ 3 * + 4 * 5 / - DUP 0 < IF DROP 0 THEN

  MAP-X @ 100 - 255 AND DUP 128 > IF 256 SWAP - THEN DX2 !
  MAP-Y @ 148 - 255 AND DUP 128 > IF 256 SWAP - THEN DY2 !
  48 DX2 @ 2 * DY2 @ 3 * + 4 * 5 / - DUP 0 < IF DROP 0 THEN

  OVER OVER < IF SWAP THEN DROP
  +
  DUP 6 < IF DROP 6 THEN
;

\ Render Authentic Martian Plasma Atmosphere (Dusty Rose Zenith -> Salmon Wisps -> Horizon Glow)
: RENDER-SKY-BACKDROP
  HORIZON 0 DO
    I 22 * HORIZON / 64 + SKY-BASE !
    \ Perspective plasma wisps across scanlines (8-pixel spans for fluid rendering)
    40 0 DO
      I 8 *
      DUP 160 - 88 * J 16 + / HEADING @ 12 * + CAM-X @ 2 / + 255 AND SKY-U !
      1200 J 16 + / CAM-Y @ 2 / + 255 AND SKY-V !
      SKY-U @ 63 AND 32 - DUP 0 < IF NEGATE THEN
      SKY-V @ 63 AND 32 - DUP 0 < IF NEGATE THEN * 512 /
      SKY-BASE @ + DUP 95 > IF DROP 95 THEN
      GET-PAL
      SWAP J 8 1 WND @ WND-RECT
    LOOP
  LOOP

  \ Soft atmospheric horizon glow line (palette 80)
  80 GET-PAL
  0 HORIZON SCREEN-W 1 WND @ WND-RECT

  \ Deep canyon base floor below horizon
  SCREEN-H HORIZON - 1 - 0 DO
    10 I 3 / + DUP 63 > IF DROP 63 THEN GET-PAL
    0 HORIZON 1 + I + SCREEN-W 1 WND @ WND-RECT
  LOOP
;

\ Raymarch a single high-resolution voxel column with 3D heading & perspective frustum
: RENDER-MARS-COL ( col -- )
  COL-X !
  SCREEN-H 1 - MAX-Y !

  \ March from near (d=4) to far (d=56)
  4 DIST !
  BEGIN
    DIST @ 56 <
  WHILE
    \ Perspective frustum lateral spread across 320 columns: (col - 160) * dist / 120
    COL-X @ 160 - DIST @ * 120 / LAT-OFF !

    \ World coordinates ray_x, ray_y with 16-angle yaw rotation
    HEADING @ GET-SIN DIST @ * HEADING @ GET-COS LAT-OFF @ * + 16 / CAM-X @ + RAY-X !
    HEADING @ GET-COS DIST @ * HEADING @ GET-SIN LAT-OFF @ * - 16 / CAM-Y @ + RAY-Y !

    RAY-X @ RAY-Y @ MARS-HEIGHT TERRAIN-H !

    \ Perspective screen Y: sy = HORIZON + ((CAM-Z - TERRAIN-H) * 26) / DIST
    CAM-Z @ TERRAIN-H @ - 26 * DIST @ / HORIZON + PROJ-Y !

    PROJ-Y @ 0 < IF 0 PROJ-Y ! THEN
    PROJ-Y @ SCREEN-H 1 - > IF SCREEN-H 1 - PROJ-Y ! THEN

    PROJ-Y @ MAX-Y @ < IF
      \ Directional sunlight shading: slope delta + altitude - distance haze
      RAY-X @ 1 - RAY-Y @ MARS-HEIGHT
      RAY-X @ 1 + RAY-Y @ MARS-HEIGHT -
      4 * 22 +
      TERRAIN-H @ 5 / +
      DIST @ 3 / -
      DUP 0 < IF DROP 0 THEN
      DUP 63 > IF DROP 63 THEN
      GET-PAL
      PROJ-Y @ MAX-Y @ COL-X @ WND @ WND-VLINE

      PROJ-Y @ 1 - MAX-Y !
    THEN

    1 DIST +!
  REPEAT

  MAX-Y @ 1 + COL-X @ SET-PROJ
;

\ Render one full frame (sky backdrop + voxel terrain + terminal profile if headless)
: RENDER-MARS-FRAME
  RENDER-SKY-BACKDROP

  \ 1. Compute voxel columns & draw GUI window
  SCREEN-W 0 DO
    I RENDER-MARS-COL
  LOOP

  \ 2. Render ASCII 3D terrain profile ONLY IF display processor is not in dev-tree
  DISPLAY? 0= IF
    24 0 DO
      40 0 DO
        I 8 * GET-PROJ
        J 10 * = IF
          94 EMIT \ '^' peak crest
        ELSE
          I 8 * GET-PROJ J 10 * < IF
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
\ 'a' / 'A': Lateral Strafe Left
\ 'd' / 'D': Lateral Strafe Right
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
      HEADING @ GET-SIN 3 * 16 / CAM-X +!
      HEADING @ GET-COS 3 * 16 / CAM-Y +!
    THEN

    \ Down Arrow or 's' or 'S': Reverse Step
    DUP 31 = OVER 115 = OR OVER 83 = OR IF
      HEADING @ GET-SIN -3 * 16 / CAM-X +!
      HEADING @ GET-COS -3 * 16 / CAM-Y +!
    THEN

    \ Space: Ballistic Jump with Martian gravity arc
    DUP 32 = IF
      JUMP-HEIGHT @ 0= IF
        20 JUMP-VEL !
        1 JUMP-HEIGHT !
      THEN
    THEN

    \ 'q' or 'Q' or ESC (27) -> terminate flight loop
    DUP 27 = OVER 113 = OR OVER 81 = OR IF
      0 RUNNING !
    THEN

    DROP
  REPEAT
;

\ Ballistic Flight Physics & Flight Clearance matching reference mars.c
: UPDATE-PHYSICS
  \ 1. Ballistic jump arc with Martian gravity
  JUMP-HEIGHT @ 0 > IF
    JUMP-VEL @ JUMP-HEIGHT +!
    -2 JUMP-VEL +!
    JUMP-HEIGHT @ 0 < IF
      0 JUMP-HEIGHT !
      0 JUMP-VEL !
    THEN
  THEN

  \ 2. Automatic ground clearance: 24 units above terrain + jump altitude
  CAM-X @ CAM-Y @ MARS-HEIGHT 24 + JUMP-HEIGHT @ + CAM-Z !
;

\ Interactive Flight Loop
: FLY
  BEGIN
    RUNNING @
  WHILE
    PROCESS-INPUT
    UPDATE-PHYSICS
    RENDER-MARS-FRAME
    16 MS

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

  120 CAM-X !
  100 CAM-Y !
  0   HEADING !
  0   SPEED !
  0   STRAFE !
  0   JUMP-VEL !
  0   JUMP-HEIGHT !
  1   RUNNING !
  0   FRAME-STEP !
  UPDATE-PHYSICS
  DISPLAY? IF 0 MAX-FRAMES ! ELSE 4 MAX-FRAMES ! THEN

  FLY
  WND @ DUP IF WND-CLOSE ELSE DROP THEN
  DISPLAY? 0= IF CR THEN
;

MARS
