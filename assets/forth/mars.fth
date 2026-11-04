\ ──────────────────────────────────────────────────────────────────
\ mars.fth — Classical 1K Mars Voxel Landscape Demo (Pouet #4662)
\ Inspired by Tim Clarke's 1993 Assembly voxel engine.
\ Renders authentic raymarched Martian landscape with canyons,
\ cratered ridges, atmospheric red haze, and dusky sky
\ directly to B-System native WND surface and terminal.
\ ──────────────────────────────────────────────────────────────────

320 CONSTANT SCREEN-W
240 CONSTANT SCREEN-H
75  CONSTANT HORIZON
85  CONSTANT CAM-Z

VARIABLE WND
VARIABLE CAM-X
VARIABLE CAM-Y
VARIABLE DIST
VARIABLE MAX-Y
VARIABLE RAY-X
VARIABLE RAY-Y
VARIABLE TERRAIN-H
VARIABLE PROJ-Y

\ Multi-octave Martian Heightfield Generator
: MARS-HEIGHT ( x y -- h )
  \ Layer 1: rolling canyon dunes and ridges
  2DUP 7 * SWAP 11 * + 255 AND
  \ Layer 2: craggy basalt fractures
  ROT 19 * ROT 13 * - 127 AND +
  \ Layer 3: high-frequency impact crater noise
  2DUP XOR 63 AND +
  4 RSHIFT 10 +
;

\ Martian Terrain Shading Palette
: MARS-COLOR ( dist h -- color )
  \ Base terrain color based on height: rust red to golden ochre
  SWAP 2 RSHIFT \ distance haze factor
  SWAP 4 *       \ height brightness
  +
  DUP 2 * 60 + 255 AND \ Red channel (rich iron oxide)
  OVER 35 + 255 AND    \ Green channel (ochre/sandstone)
  ROT 20 + 255 AND     \ Blue channel (deep shadow/dust)
  RGB
;

\ Martian Sky Atmosphere
: MARS-SKY ( y -- color )
  \ Gradient from deep cosmic purple (0,0,30) to dusty red horizon (90,30,25)
  HORIZON MIN
  DUP 80 * HORIZON / 15 + \ Red
  OVER 30 * HORIZON / 8 + \ Green
  ROT 25 * HORIZON / 20 + \ Blue
  RGB
;

\ Render one full frame of Mars voxel landscape
: RENDER-MARS-FRAME
  SCREEN-W 0 DO
    SCREEN-H 1 - MAX-Y !

    \ Raymarch column I from near (d=14) to far (d=160)
    14 DIST !
    BEGIN
      DIST @ 160 <
    WHILE
      \ Compute world coordinates for column ray
      CAM-X @ DIST @ I 160 - * 110 / + RAY-X !
      CAM-Y @ DIST @ + RAY-Y !

      RAY-X @ RAY-Y @ MARS-HEIGHT TERRAIN-H !

      \ Screen projection: sy = horizon + (camZ - h) * 110 / d
      CAM-Z TERRAIN-H @ - 110 * DIST @ / HORIZON + PROJ-Y !

      PROJ-Y @ 0 < IF 0 PROJ-Y ! THEN
      PROJ-Y @ SCREEN-H 1 - > IF SCREEN-H 1 - PROJ-Y ! THEN

      PROJ-Y @ MAX-Y @ < IF
        \ Render vertical terrain span
        DIST @ TERRAIN-H @ MARS-COLOR
        PROJ-Y @ MAX-Y @ I WND @ WND-VLINE
        PROJ-Y @ MAX-Y !
      THEN

      3 DIST +! \ Ray step size
    REPEAT

    \ Fill remaining column above highest ridge with Martian sky
    MAX-Y @ 0 > IF
      MAX-Y @ MARS-SKY
      0 MAX-Y @ I WND @ WND-VLINE
    THEN
  LOOP

  WND @ WND-UPDATE
;

\ Main Mars Demo Runner
: MARS
  CR
  SCREEN-W SCREEN-H WND-OPEN WND !
  0 CAM-X !
  0 CAM-Y !

  \ Flight sequence over the Red Planet (8 flyover steps)
  8 0 DO
    RENDER-MARS-FRAME
    3 CAM-X +!
    6 CAM-Y +!
    46 EMIT \ Print '.' in console for progress telemetry
  LOOP

  CR
;

MARS
