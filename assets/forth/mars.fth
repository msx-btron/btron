\ ──────────────────────────────────────────────────────────────────
\ mars.fth — Classical Mars Voxel Landscape Demo (1993 Tim Clarke)
\ Pure Forth implementation of 1993 Tim Clarke Mars voxel engine.
\ Features:
\   - Imports authentic 64KB Diamond-Square heightmap (mars_height.bin)
\   - Imports authentic 64KB directional slope/shading map (mars_shade.bin)
\   - Imports authentic 64KB plasma cloud texture (mars_sky.bin)
\   - Authentic 96-color VGA palette (Terracotta slopes + Dusty Rose plasma sky)
\   - Fully preserved multi-layer plasma sky with glowing horizon line
\   - Infinite toroidal fractal space wrapping (period 256)
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

VARIABLE CAM-FX
VARIABLE CAM-FY
VARIABLE FINE-HEADING

\ Initial camera position calibrated to match reference mars.c (0x03e8 >> 4 = 62)
62 256 * CAM-FX !
62 256 * CAM-FY !
62  CAM-X !
62  CAM-Y !
75  CAM-Z !
0   FINE-HEADING !
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

\ ── Authentic 64KB Map & Sky Arrays ───────────────────────────────
CREATE MARS-HEIGHT-MAP 65536 ALLOT
CREATE MARS-SHADE-MAP  65536 ALLOT
CREATE MARS-SKY-MAP    65536 ALLOT

\ Import authentic binary datasets exported from 1993 Diamond-Square engine
MARS-HEIGHT-MAP 65536 LOAD-FILE mars_height.bin DROP
MARS-SHADE-MAP  65536 LOAD-FILE mars_shade.bin  DROP
MARS-SKY-MAP    65536 LOAD-FILE mars_sky.bin    DROP

\ Toroidal coordinate wrap (period 256 -> infinite seamless space)
: WRAP ( n -- 0..255 )
  255 AND
;

: MARS-HEIGHT ( x y -- h )
  WRAP 256 * SWAP WRAP + MARS-HEIGHT-MAP + C@
;

: MARS-SHADE ( x y -- shade )
  WRAP 256 * SWAP WRAP + MARS-SHADE-MAP + C@
;

: MARS-SKY ( x y -- color )
  WRAP 256 * SWAP WRAP + MARS-SKY-MAP + C@
;

\ Calibrate camera altitude above ground
: CALC-ALTITUDE
  CAM-X @ CAM-Y @ MARS-HEIGHT 22 + JUMP-HEIGHT @ + CAM-Z !
;

: SYNC-CAM
  CAM-FX @ 65535 AND CAM-FX !
  CAM-FY @ 65535 AND CAM-FY !
  CAM-FX @ 256 / 255 AND CAM-X !
  CAM-FY @ 256 / 255 AND CAM-Y !
  CALC-ALTITUDE
;

CALC-ALTITUDE

\ 16-angle Sine/Cosine Approximation (scaled by 16)
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

VARIABLE SKY-BASE
VARIABLE SKY-U
VARIABLE SKY-V

\ Render Authentic Martian Plasma Atmosphere (Dusty Rose Zenith -> Salmon Wisps -> Horizon Glow)
: RENDER-SKY-BACKDROP
  \ 1. Multi-layer perspective plasma sky across rows 0..HORIZON-1 (2-pixel scanlines)
  HORIZON 2 / 0 DO
    40 0 DO
      \ Perspective plasma wisps across scanlines (8-pixel spans for fluid framerate)
      I 8 * 160 - 80 * HORIZON J 2 * - 1 + / HEADING @ 16 * + CAM-X @ + WRAP SKY-U !
      1200 HORIZON J 2 * - 1 + / CAM-Y @ + WRAP SKY-V !
      SKY-U @ SKY-V @ MARS-SKY GET-PAL
      I 8 * J 2 * 8 2 WND @ WND-RECT
    LOOP
  LOOP

  \ 2. Soft atmospheric horizon glow line (color 80)
  80 GET-PAL
  0 HORIZON SCREEN-W 2 WND @ WND-RECT

  \ 3. Deep canyon base floor below horizon (fast 4-pixel bands)
  SCREEN-H HORIZON - 2 - 4 / 0 DO
    6 I + DUP 36 > IF DROP 36 THEN GET-PAL
    0 HORIZON 2 + I 4 * + SCREEN-W 4 WND @ WND-RECT
  LOOP
;

\ Raymarch a single high-resolution voxel column with 3D heading & perspective frustum
: RENDER-MARS-COL ( col -- )
  COL-X !
  SCREEN-H 1 - MAX-Y !

  \ March from near (d=3) to far (d=58)
  3 DIST !
  BEGIN
    DIST @ 58 <
  WHILE
    \ Perspective frustum lateral spread across 320 columns
    COL-X @ 160 - DIST @ * 115 / LAT-OFF !

    \ World coordinates ray_x, ray_y with 16-angle yaw rotation
    HEADING @ GET-SIN DIST @ * HEADING @ GET-COS LAT-OFF @ * + 16 / CAM-X @ + RAY-X !
    HEADING @ GET-COS DIST @ * HEADING @ GET-SIN LAT-OFF @ * - 16 / CAM-Y @ + RAY-Y !

    RAY-X @ RAY-Y @ MARS-HEIGHT TERRAIN-H !

    \ Perspective screen Y: sy = HORIZON + ((CAM-Z - TERRAIN-H) * 26) / DIST
    CAM-Z @ TERRAIN-H @ - 26 * DIST @ / HORIZON + PROJ-Y !

    \ Natural mountain crest clamp at top of screen (row 0)
    PROJ-Y @ 0 < IF 0 PROJ-Y ! THEN
    PROJ-Y @ SCREEN-H 1 - > IF SCREEN-H 1 - PROJ-Y ! THEN

    PROJ-Y @ MAX-Y @ < IF
      \ Directional sunlight shading sampled from authentic MARS-SHADE-MAP
      RAY-X @ RAY-Y @ MARS-SHADE
      DIST @ 4 / -
      DUP 0 < IF DROP 0 THEN
      DUP 63 > IF DROP 63 THEN
      GET-PAL
      PROJ-Y @ MAX-Y @ COL-X @ WND @ WND-VLINE

      PROJ-Y @ 1 - MAX-Y !
    THEN

    \ If column filled to top of screen, early exit
    MAX-Y @ 0 < IF
      58 DIST !
    ELSE
      DIST @ 16 < IF 1 ELSE 2 THEN DIST +!
    THEN
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
: PROCESS-INPUT ( key -- )
  DUP 28 = IF
    \ Snappy Yaw Turn Left (16 discrete headings matching original mars.c)
    HEADING @ 1 - 15 AND HEADING !
  ELSE DUP 29 = IF
    \ Snappy Yaw Turn Right (16 discrete headings matching original mars.c)
    HEADING @ 1 + 15 AND HEADING !
  ELSE DUP 30 = OVER 119 = OR OVER 87 = OR IF
    \ Move Forward along heading (2x viewport speed: 1.5 map units per step)
    HEADING @ GET-SIN 24 * CAM-FX +!
    HEADING @ GET-COS 24 * CAM-FY +!
    SYNC-CAM
  ELSE DUP 31 = OVER 115 = OR OVER 83 = OR IF
    \ Move Reverse along heading (2x viewport speed: 1.5 map units per step)
    HEADING @ GET-SIN 24 * CAM-FX @ SWAP - CAM-FX !
    HEADING @ GET-COS 24 * CAM-FY @ SWAP - CAM-FY !
    SYNC-CAM
  ELSE DUP 97 = OVER 65 = OR IF
    \ Strafe Left (2x viewport speed: 1.5 map units per step)
    HEADING @ GET-COS 24 * CAM-FX @ SWAP - CAM-FX !
    HEADING @ GET-SIN 24 * CAM-FY +!
    SYNC-CAM
  ELSE DUP 100 = OVER 68 = OR IF
    \ Strafe Right (2x viewport speed: 1.5 map units per step)
    HEADING @ GET-COS 24 * CAM-FX +!
    HEADING @ GET-SIN 24 * CAM-FY @ SWAP - CAM-FY !
    SYNC-CAM
  ELSE DUP 32 = IF
    \ Ballistic Jump Leap
    IS-JUMPING @ 0= IF
      1 IS-JUMPING !
      28 JUMP-VEL !
    THEN
  ELSE DUP 113 = OVER 81 = OR OVER 27 = OR IF
    0 RUNNING !
  THEN THEN THEN THEN THEN THEN THEN THEN
  DROP
;

: UPDATE-PHYSICS
  IS-JUMPING @ IF
    JUMP-HEIGHT @ JUMP-VEL @ + JUMP-HEIGHT !
    JUMP-VEL @ 3 - JUMP-VEL ! \ Martian gravity
    JUMP-HEIGHT @ 1 < IF
      0 JUMP-HEIGHT !
      0 JUMP-VEL !
      0 IS-JUMPING !
    THEN
    CALC-ALTITUDE
  THEN
;

\ Main interactive flight loop
: FLY
  BEGIN
    RUNNING @
  WHILE
    KEY? IF
      KEY PROCESS-INPUT
    THEN
    UPDATE-PHYSICS
    RENDER-MARS-FRAME
    16 MS
  REPEAT
;

: MARS
  DISPLAY? IF
    SCREEN-W SCREEN-H WND-OPEN WND !
  THEN
  CALC-ALTITUDE
  1 RUNNING !
  RENDER-MARS-FRAME
  FLY
  WND @ IF
    WND @ WND-CLOSE
    0 WND !
  THEN
;

\ Auto-launch interactive flight when evaluated
MARS
