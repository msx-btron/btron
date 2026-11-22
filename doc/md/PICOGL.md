# PicoGL

Architecture and implementation plan for the PicoCalc 3D pipeline. Phases 1 and 5 meet their Done-when lines. Phases 2, 3, and 4 are in the tree. Their UART frame-rate lines are not measured. A phase is finished only when its "Done when" line is true.

PicoGL is the 3D pipeline for the Clockwork PicoCalc. Two RP2040 cores, no framebuffer, the ST7365P that `make picocalc` already drives. Core 1 rasters. Core 0 runs SegUI, the submit, and the SPI DMA.

## Phases and deliverables

Five phases, in order. A phase is done only when its deliverable exists and its "Done when" line is true. The SRAM assert (`__sram_used__` ≤ 210 KB) is a gate on every phase that links a PicoCalc image. SegUI sources are not removed.

| Phase | Deliverable | Done when |
|---|---|---|
| 1. Scene submit | The seven `R_EEGS_*` calls bin triangles on Pico, and a host test checks one fan | The host test passes. The PS2 body is unchanged. SRAM layout for depth and buckets is written down |
| 2. Raster | Core 1 draws bands. GLGears on the existing center tile runs on PicoGL | UART `picogl gears` is 20 fps or better on mask `0x0007FFFC` |
| 3. `make picocalc` | That image launches GLGears and XMB. TinyGL is gone | Page 1 is GLGears. Page 2 is XMB at 12 fps or better. Terminal and Cabinet still open |
| 4. `make picocalc-quake` | That image launches a playable `start` level in the center tile | Walk, open a door, kill a zombie, fire a picked-up weapon. The slipgate stays in the hall. 12 fps or better while standing still |
| 5. Gates | `make test-picocalc` builds both images and rejects the things that must not ship | The script fails the link if it contains `ztriangle`, `e1m1`, or `progs.dat` |

### Phase 1 — Scene submit

**Deliverable.** `GL_BACKEND_PICOGL = 5`. On `PICO_REAL_HW` the seven calls in `r_eegs.h` transform and bin. The raster is empty. A host test feeds one fan and checks the bins. The PS2 implementation stays behind `BTRON_PS2_TARGET`.

**Also in this phase.** Measured `.build/picocalc-rp2040/btron-picocalc-rp2040.elf` on 2026-11-22: data 3,268 + bss 198,924 = 202,192. The assert is 215,040, so 12,848 bytes were free beside SegUI. The 10 KB depth picture and a second bucket set do not fit. Neither does a 48-wide bin (23,040). The choice recorded in `picogl.h` is one bucket set, depth stored on the triangle (`iz`), and 16 triangles a band (16 × 20 × 24 = 7,680).

**Done when.** The host fan matches the expected bins and `drop` is 0. Desktop, virgl, V3D, and TinyGL still take the `glBegin` path.

### Phase 2 — Raster

**Deliverable.** `pico_core1.c` wakes core 1. The span kernel runs there, in the 4 KB scratch bank. Core 0 does BitBlt, the HUD, and the SPI DMA. `st7365p_flush_band_dma` fills one strip while the other is on the wire. GLGears stays the center tile and is the fixture for this phase. It uses the GLES door, not TinyGL.

**Done when.** On hardware, UART prints `picogl gears` at 20 fps or better for the 17 bands in mask `0x0007FFFC`. The SRAM assert holds. A host test still rasters one band on one core, so the reference does not need the FIFO, and the depth winner matches a software band.

**Status, 2026-11-22.** The host test passes. `make PICO_BOARD=rp2040 picocalc` links both the UF2 elf and the SD-boot elf. `__sram_used__` is 207,364 (data 2,804 + bss 204,560), so 7,676 bytes are free under 215,040. `.scratch_x` is `paint_row`, 452 bytes, at `0x20040000`. Core 1's stack top is `0x20041400`. The 20 fps line is not measured. There is no board in this run.

GLGears on that image calls the GLES door by name (`picogl_gl_*`). The image does not go through `g_gl`. At the end of phase 2, TinyGL was still linked and the 112×96 color and iz came out of the 96 KB arena. Phase 3 moves those buffers to static BSS and drops the arena. A bound target queues 16 triangles and flushes them, so a gear strip does not drop. With no target, a full band still counts as a drop.

Core 0 transforms, then waits while core 1 runs the integer span. That span does not divide, so it does not take the hardware divider. A cold boot leaves core 1 in the bootrom. `pico_core1_launch` sends `0, 0, 1, VTOR, SP, entry`, and the bootrom echoes each word. If core 1 has already entered `Reset_Handler`, the park there accepts one thumb pointer in flash or SRAM and ignores the bootrom words. If the wake fails, core 0 paints the same span.

`dp_pico_present` paints a strip, then calls `st7365p_flush_band_dma`. The wait at the start of that call is the previous band. The new transfer is not waited out. SPI is one bus, so the window is programmed only while the bus is idle. The `ms` in the UART line is the whole animated present.

### Phase 3 — `make picocalc`

**Deliverable.** The existing `make picocalc` image. Page 1 of the launcher is unchanged and GLGears is still the center. Page 2, reached with Left/Right, is XMB (`xmb_pico.c`, 32×32 P8 cells). `PICOCALC_TGL_C` is not in the link.

**Done when.** GLGears and XMB both launch. UART `picogl xmb` is 12 fps or better. Terminal and Cabinet still open. `ztriangle` is absent from the elf.

**Status, 2026-11-22.** `make PICO_BOARD=rp2040 picocalc` links. `PICOCALC_TGL_C` is not on that link. `llvm-nm` has no `ztriangle`, `glInit`, or `ZB_` symbol, and the elf has no `ztriangle` string. `build_terminal`, `build_cabinet`, `build_glgears`, and `build_xmb` are in the elf. Page 1 is still the nine apps with GLGears in the center. Left/Right on the launcher switches pages. Page 2 is one XMB tile. Keys 1–9 still open Terminal through Prefs. The XMB app paints a gradient, a `DST_COLOR_ONE` ribbon, one category row, one icon row of 32×32 P8 cells, and A1 labels. Enter on Terminal, Editor, Browser, Cabinet, GLGears, Chat, or Prefs opens that app.

`__sram_used__` on the RP2040 elf is 173,588 (data 2,804 + bss 170,784), so 41,452 bytes are free under 215,040. The RP2350 elf is 173,580. The 96 KB arena is gone. Gears color is `glgears_segui_render.s_rgb` (21,504) and iz is `s_iz` (43,008), both static BSS. `.scratch_x` is still `paint_row`, 452 bytes, at `0x20040000`. On RP2350, `.scratch_x` is at `0x20080000` and core 1's stack top is `0x20081400`. The host bin and raster tests pass. UART `picogl xmb <ms> tri= span= drop= bands=` is compiled. The 12 fps line is not measured. There is no board in this run. This link has no `gl_dispatch`, so the GLES door stays the `picogl_gl_*` names. `pico_tinygl_rt.c` stays for `sinf` and `sqrtf`.

### Phase 4 — `make picocalc-quake`

**Deliverable.** A sibling target, elf `btron-picocalc-quake-$(PICO_BOARD)`, flag `-DPICOCALC_APP_QUAKE`. Same SegUI shell. The center tile is the `start` level instead of GLGears. The image contains the cooked `.rodata` blob, `start_sim.c`, and alias frames submitted with `R_EEGS_Tris`. It does not contain Gears, XMB, `progs.dat`, or any map but `start`.

**Done when.** From the center tile you can walk the hall, open a door, kill a zombie, and fire a weapon you picked up. A changelevel shows the slipgate line and does not load another map. UART `picogl quake` is 12 fps or better while standing still, and `drop` is printed if a band overflows. `e1m1` and `progs.dat` are absent.

**Status, 2026-11-22.** `make PICO_BOARD=rp2040 picocalc-quake` links `.build/picocalc-quake-rp2040/btron-picocalc-quake-rp2040.elf`. text 792,000, data 2,692, bss 182,524. `__sram_used__` is 185,216 (`0x2d380`), so 29,824 bytes are free under 215,040. `__bss_end__` is `0x2002d384`, which is under `__stack_top__` (`0x20040000`) minus the 4 KB guard. `.scratch_x` is still `paint_row`, 452 bytes, at `0x20040000`. The view is 128×96 and flat shaded: `s_rgb` is 24,576 bytes, `s_iz` is 49,152, the hull walk `s_hull_fr` is 4,096, and the node walk `s_node_fr` is 1,536. The cook stores each face's palette-average color. The sim submits that color with `tex = -1` through `picogl_tris`. The cooked blob is `.rodata`. UF2 `btron-picocalc-quake-rp2040.uf2` is 1,589,760 bytes. The payload bin is 794,692 bytes at `0x10000000`, under the 2 MB flash. The SD-boot bin is 794,436 bytes at `0x10032000`, under the 1,892,352-byte window. `PICO_BOARD` defaults to `rp2040`. `PICO_BOARD=rp2350` also links: text 792,924, data 2,692, bss 182,516, `__sram_used__` 185,208. Its UF2 is 1,591,296 bytes and its SD-boot bin is 795,596 bytes.

`glgears_segui.c` and `xmb_pico.c` are out of `PICOCALC_COMMON_C`. `llvm-nm` has `build_quake`, `start_sim_init`, `start_sim_tick`, `start_sim_render`, and the eight shell builders. It has no `build_glgears`, `build_xmb`, `xmb_pico`, `build_quake_preview`, or `ztriangle`. The elf has no `e1m1`, `progs.dat`, or `player.mdl` string. `r_brush.c` and `r_alias.c` are not on this link. A `GLGears` string remains from `doc/txt/PICO.txt` in the cabinet image. That is help text, not the renderer. The center slot keeps the `PALM_APP_GLGEARS` index. Inside the app, keys `1`–`8` select a carried weapon and `e` cycles. Ctrl (`0x7E`) fires. Launcher keys `1`–`9` still open apps only while the launcher is up. F1 stays Home.

`make test-start-sim` passed on the host. The spawn is not solid. A yaw-90 walk moves. Door 0 opens from 0 to 54. The supershotgun drops zombie 8 from 60 to 4. The rocket launcher leaves one projectile in flight. The slipgate keeps the hall and shows `Walk into the Slipgate to start playing Quake!`. One 128×96 frame lit 4,547 of 12,288 pixels and submitted 104 triangles. The hall has no shotgun pickup. The picked-up weapons in that test are the supershotgun and the rocket launcher. The 12 fps line is not measured. There is no board in this run. Nothing was flashed. `make flash-picocalc` still builds the gears/XMB UF2. The eight shell apps stayed. `make test-picocalc` relinked that gears image in the same run.

### Phase 5 — Gates

**Deliverable.** `scripts/test_picocalc_build.sh` builds `make picocalc` and `make picocalc-quake` for the boards it already covers, and checks the SRAM assert, the UF2, and boot2.

**Done when.** The script fails if either elf contains `ztriangle`, the string `e1m1`, or `progs.dat`. The existing picocalc checks still pass.

**Status, 2026-11-22.** `make test-picocalc` runs `scripts/test_picocalc_build.sh` and passed. It builds both images for RP2040 and RP2350. UF2 family IDs match (`0xE48BFF56` / `0xE48BFF59`), the RP2040 boot2 CRC is `0xC0278CA8`, and each SD-boot image has the right MSP and a Thumb reset handler. `__sram_used__` is 173,588 and 173,580 on the gears elfs, and 185,216 and 185,208 on the quake elfs, all under 215,040. The RP2040 payloads fit in 2 MiB and in the SD-boot window. A file that contains `ztriangle`, `e1m1`, or `progs.dat` is rejected, and none of the four elfs contain those strings or a `ztriangle` symbol. The gears elfs have `build_glgears`, `build_xmb`, `build_terminal`, and `build_cabinet`. The quake elfs have `build_quake` and `start_sim_render`, and they do not have `build_glgears` or `build_xmb`. `PICO_BOARD` defaults to `rp2040`, so unqualified `make picocalc` and `make picocalc-quake` are the RP2040 images.

## 1. What ships

Both images keep the SegUI shell: Terminal, Editor, Browser, Cabinet, Contacts, Phone, Chat, Prefs, and the center tile. `PICOCALC_COMMON_C` stays. TinyGL (`PICOCALC_TGL_C`, including `ztriangle.c`) leaves both links. PicoGL is the only rasterizer.

The home grid is a fixed 3×3. The fifth button is the center:

```
Terminal   Editor    Browser
Cabinet    CENTER    Contacts
Phone      Chat      Prefs
```

| Image | Center tile | Also on the image |
|---|---|---|
| `make picocalc` | GLGears, the GLES door | XMB, on a second home page. Left/Right on the launcher switches pages. Page 2 is one tile, XMB. The nine apps on page 1 stay |
| `make picocalc-quake` | Quake | The same nine-app shell. The center slot is the start level, and it is playable. Gears and XMB are not on this image |

`PALM_APP_GLGEARS` stays the slot index so page 1 does not renumber. The Quake image changes that slot's build function and menu. Page 2 exists only on `make picocalc`.

The 2026-11-22 quake link is that sim: a flat-shaded hall, host-tested, not flashed. 12 fps is not measured. XMB is off that elf because page 2 belongs to `make picocalc`. GLGears is off because its 64 KB of color and iz is the SRAM the level needs.

## 2. The start level is playable

`maps/start.bsp` is the entrance. pak0 has no string "Welcome to Quake". Worldspawn says `Introduction`. The spawn is `544 288 32`, angle 90. A trigger in the hall says `Walk into the Slipgate to start playing Quake!`. The map is a hall, not a backdrop: 5 doors, 9 zombies, 9 func_walls, pickups, and 5 changelevels.

You can walk it.

| You can | You cannot |
|---|---|
| Look, walk, strafe, jump. Gravity and step-up. Collision with the world hull and with doors | Load `e1m1` or any other map. A changelevel or episode gate shows the slipgate line and leaves you in the hall. That is the end of this image |
| Open the five `func_door`s | Save, load, deathmatch, coop, the retail console, the retail menu |
| Fight the nine zombies. They stand, chase, melee, and die | Meet any other monster. None are on this map |
| Pick up the health, shells, nails, cells, rockets, armor, quad, and the weapons lying in the hall: shotgun, nailgun, super nailgun, rocket launcher, grenade launcher, lightning | Carry those items into another level |
| Fire those weapons. Nails, rockets, and grenades exist, with a cap of 16 in flight | Hear the retail sound set in v1. The image is silent. Sound is a later flash budget |
| Read the HUD: health, armor, ammo, and `Introduction` | |

### Why this is not `progs.dat`

id's `progs.dat` is 413,116 bytes and defines 195 fields per edict, 780 bytes, plus the engine header. The map has 385 edicts. A 448-slot table is about 350 KB before globals (46 KB) and before SegUI. RP2040 SRAM is 264 KB with a 210 KB link assert. RP2350 is 520 KB and the same assert. The desktop VM does not fit beside the shell on either board.

The start level runs as a C simulation over the entity lump. The lump is 27,652 bytes and it stays in the blob whole. Each live thing is a small struct, about 64 bytes:

| Slots | Count | Cap |
|---|---|---|
| Player | 1 | 1 |
| Zombies | 9 | 9 |
| Doors | 5 | 5 |
| Pickups | 25 | 32 |
| Projectiles | nails, rockets, grenades | 16 in flight |

Under 4 KB of game state. `func_wall` and the world hull stay in the BSP. Triggers are records in the entity lump, not slots.

Movement uses the clipnodes (42,608 bytes, kept). The walk is the Quake ground move: wishdir from the keys, accelerate, friction, gravity, a point trace, a step-up of 18 units, then slide. The i2c keyboard already in the shell supplies the keys: arrows look, WASD move, Space jumps, Ctrl fires, number keys or the existing weapon cycle picks the gun you are holding. F1 is still Home and is not stolen.

Doors read `angle`, `lip`, `wait`, and `speed` from the entity. Touch starts the move. The door is a bmodel hull swept along that axis.

A zombie has four frames groups: stand, walk, attack, die. It wakes when the player is in view of its leaf, walks the hull toward the player, melees in range, and dies at zero health. Shotgun is a trace. Nails and rockets are entities that move and damage what they hit.

`trigger_changelevel`, `trigger_teleport` to another map, and `func_episodegate` do not call `World_LoadMap`. They set the HUD line to the slipgate sentence. Teleports whose target is inside `start.bsp` still move the player.

### What the blob contains

A host tool reads `assets/quake/pak/pak0.pak` and writes one `.rodata` image.

| Kept | Size |
|---|---|
| Planes, vertices, nodes, texinfo, faces, leaves, marksurfaces, edges, surfedges, models | 547,568 |
| Clipnodes | 42,608 |
| Entity lump, whole | 27,652 |
| 81 textures, smallest mip, P8, plus the 768-byte palette | 6,984 + 768 |
| Models listed below, cooked | budget 256 KB, the tool refuses to exceed it |

Dropped: vis, lighting, full mips, `progs.dat`, every map except `start.bsp`, every monster model except the zombie, the retail sound set.

Cooked models, few frames, one skin, submitted as `R_EEGS_Tris`:

| Model | Frames |
|---|---|
| `progs/zombie.mdl`, `h_zombie.mdl`, `zom_gib.mdl` | stand, walk, attack, die |
| `v_axe`, `v_shot`, `v_shot2`, `v_nail`, `v_nail2`, `v_rock`, `v_rock2`, `v_light` | idle and fire |
| `g_shot`, `g_nail`, `g_nail2`, `g_rock`, `g_rock2`, `g_light`, `armor`, `quaddama` | one frame, the pickup |
| `spike`, `s_spike`, `missile`, `grenade`, `lavaball` | one frame |
| `flame`, `flame2` | two frames, the torches |

`player.mdl` (191,852 bytes) is not shipped. The view is first person. A death is the gib models and a respawn at `info_player_start`.

Alias draws on Pico from the cooked frames in the blob. `start_sim.c` submits them with `picogl_tris`, which is what `R_EEGS_Tris` forwards to on Pico. `r_alias.c` and `r_brush.c` are not on this link. The PS2 EEGS path still returns immediately from alias and particles, because that is the 30 fps slice on the GS. Particles that are not a projectile are omitted.

Flash, RP2040, 2 MB: shell about 495 KB minus TinyGL, BSP about 625 KB, models at the 256 KB cap, PicoGL and the sim well under 150 KB of code. The image stays under 2 MB with margin for the +200 KB SD-boot origin. The blob is not copied into SRAM.

## 3. Pipeline

The seven steps in this section are the draw pipeline. They are not the delivery phases.

The bar is [Pico3D](https://github.com/bernhardstrobl/Pico3D): core 1 rasters, Q10, affine textures, the inner loop in scratch SRAM. Pico3D draws into a full 240×240 framebuffer (graphics total about 218 KB) and its perspective path divides per pixel. PicoGL keeps the core split and drops the framebuffer and the per-pixel divide.

| | Pico3D | PicoGL |
|---|---|---|
| Panel | PicoSystem 240×240 | ST7365P 320×320, SPI 31.25 MHz |
| Raster core | Core 1 | Core 1 |
| Color memory | Full RGB565 frame plus Z | Two 320×16 strips, 20 KB, already in `dp_pico.c`. Z is one band |
| Inner loop | Barycentric divides | Edge walk. UV on the SIO interpolator |
| Submit | `render_triangle()` | The seven scene calls |

### Constraints

| Item | Value |
|---|---|
| Cores | Two Cortex-M0+ at 125 MHz, no FPU. Core 1 rasters. Core 0 does the rest |
| SRAM | 264 KB on RP2040. `__sram_used__` ≤ 210 KB, 4 KB stack guard (`link_picocalc.ld`) |
| Flash | 2 MB on the RP2040 image |
| SPI | 31.25 MHz, 8-bit DMA channel 0. One band is 320×16×2 = 10,240 bytes = **2.62 ms**. Twenty dirty bands = **52.4 ms** = **19.1 fps** |
| Shell | SegUI stays. A 320×320 RGB565 frame is 204,800 bytes and is not allocated |

The 19 fps ceiling is the wire. Missing the 2.62 ms window on core 1 drops the frame rate into the teens. Thirty fps needs a smaller window or a faster SPI clock, which is a driver change.

`dp_pico_present` paints the idle strip, then starts `st7365p_flush_band_dma`. The wait at the start of that call is the previous band. Core 0 is the only core that programs SPI and DMA. Phase 2 does not hide the span behind that DMA: core 0 waits while core 1 paints the gears target, then core 0 blits and kicks the wire.

### Stages

```
core 0, once per frame
1 Submit      scene call or glEnd
2 Transform   one Q10 MVP, one light
3 Clip        six planes, then perspective divide
4 Bin         triangle → the bands it covers
core 1, each dirty band, inside 2.62 ms
5 Raster      edge walk, interpolator UV, 1/z, write the strip
core 0
6 Cover       BitBlt and HUD over the strip
7 Scan        DMA channel 0
```

**Stage 1, two doors, one triangle.**

The scene door is the core. These are the calls in `src/media/quake/include/r_eegs.h`. PS2 implements them with VU0 and the GS. Pico implements them with stages 2–7.

```c
int  R_EEGS_Live(void);
void R_EEGS_BeginFrame(int width, int height);
void R_EEGS_SetCamera(const float org[3], const float ang[3],
                      int width, int height);
void R_EEGS_Poly(const float *xyz, const float *st, int nverts,
                 uint32_t rgba, int gl_tex_idx);
void R_EEGS_Tris(const float *xyz, const float *st, int ntris,
                 uint32_t rgba, int gl_tex_idx);
void R_EEGS_Fill(int x, int y, int w, int h, uint32_t color);
void R_EEGS_String(int x, int y, const char *str,
                   const uint8_t font[96][8]);
```

| Call | Contract |
|---|---|
| `Live` | True for `GL_BACKEND_EEGS` and `GL_BACKEND_PICOGL` |
| `BeginFrame` | Viewport. Drops the bins. Clears color. No full-frame Z |
| `SetCamera` | One camera. Quake origin and angles. One view-projection. No matrix stack |
| `Poly` | One convex face, ≤ 32 verts. World xyz. Texel UVs. One RGBA. One texture index |
| `Tris` | A triangle list, same shade and texture. World faces, cooked alias frames, projectiles |
| `Fill`, `String` | HUD. Screen pixels. No depth |

`GL_BACKEND_PICOGL = 5`. `g_picogl_ops` is the GLES door: `glBegin` records ≤ 32 verts, `glEnd` emits the same triangles as `R_EEGS_Tris`. GLGears is this door. The PicoCalc image calls the door by name. That link has no `gl_dispatch` and no TinyGL, so there is no TinyGL default left in the elf. Host builds still default to TinyGL. The subset is ES 1.1 common-lite. Outside it, `GL_INVALID_ENUM` and a no-op.

| In | Out |
|---|---|
| Triangles, fans, strips, quads, quad strips | Polygons, lines, points |
| Vertex, texcoord, color, normal | Vertex arrays |
| Rotate, translate, scale, push/pop, identity, frustum, ortho, viewport | A second matrix stack |
| Depth, cull, blend, texture 2D, one light | Fog, stencil, scissor, lights 1–7 |
| Flat and smooth. Nearest and linear. Repeat on power-of-two | Mipmaps, cube maps |
| Blend src-alpha, one/one, dst-color/one | The rest |
| Clear color and depth | Display lists |

`glGetString(GL_RENDERER)` is `"PicoGL ES 1.1"`. `glGetString(GL_VERSION)` is `"OpenGL ES-CL 1.1"`.

**Stage 2.** One MVP in Q10 (1024 = 1.0). Float exists only while that matrix is built. A 64-vertex cache covers one fan. `R_EEGS_Poly`'s single RGBA, and `GL_FLAT`, store one color on the triangle.

**Stage 3.** Sutherland–Hodgman on the six planes, the same rule as `eegs_geom.c`: a vertex with w near 0 never reaches the raster. Near 8, far 1024, in Q10. The EEGS 0.1 / 1000 frustum does not fit a 16-bit band Z.

**Stage 4.** A triangle is 24 bytes: y0, y1, edge steps, 1/z, RGB565 or UV, texture id, flags (`FLAT`, `TEX`, `ADD`, `ALPHA`). Twenty bands, 16 triangles each. Overflow increments `drop` and is a failed frame. 16 × 20 × 24 = 7,680 bytes. That is the size that fits in the 12,848 bytes Phase 1 measured free.

**Stage 5, core 1.** Opaque spans keep the nearer 1/z. ADD and ALPHA blend after that. U and V step on the per-core interpolator. Edges longer than 16 pixels are split at submit, so the span stays affine and does not divide per pixel. The span kernel sits in the 4 KB scratch bank. The hardware divider is used only from core 1, and only after core 0 has finished clipping. Phase 2's span is the integer edge test in `paint_row` (452 bytes at `0x20040000`). It does not sample a texture and it does not divide. Core 0 waits for it.

**Stage 6, core 0.** HUD and XMB over the finished band. Rectangles do not become triangles.

**Stage 7, core 0.** DMA. Core 1 never touches GPIO, SPI, or DMA registers.

### Cores

| | Core 0 | Core 1 |
|---|---|---|
| Work | SegUI, keys, the sim, stages 1–4, stage 6, DMA kick | Stage 5 |
| Handshake | Pushes a frame token on the SIO FIFO when the bins are sealed. Starts DMA when core 1 pushes a band index | Rasters into the strip that is not on the wire. Pushes frame-done at the end |

Launch is bare-metal in `pico_core1.c`. The bootrom parks core 1. Core 0 sends `0, 0, 1, VTOR, SP, entry` through the FIFO, and the bootrom echoes each word. Core 1's stack is 1 KB at `0x20041400`, in scratch bank Y. There is no Pico SDK here. The per-frame job is a flag in SRAM, not another FIFO word. Core 0 waits until core 1 sets it done.

One bucket set is what fits. Core 0 runs SegUI during stage 5 and transforms the next view after frame-done. A second set would be another 7,680 bytes and the measured image does not have them.

XMB is mostly stage 6, on core 0. Core 1 still clears the bands so the shell and the DMA wait are not the same core.

### Band framebuffer and BitBlt

The framebuffer is the strip being filled. `picogl_fb_bind_band` names it.

```c
typedef struct {
    const void     *pixels;   /* flash or SRAM */
    int             w, h, stride;
    uint8_t         fmt;      /* RGB565, P8, A1 */
    const uint16_t *pal;
} picogl_surf_t;

void picogl_fill(int x, int y, int w, int h, uint16_t rgb565_be);
void picogl_blit(const picogl_surf_t *src,
                 int sx, int sy, int sw, int sh,
                 int dx, int dy, int dw, int dh, int op);
void picogl_mark_bands(int y, int h);
```

| Op | Who | Loop |
|---|---|---|
| `COPY` | HUD bars, the Quake 2× store | Word stores. An aligned RGB565 row may use DMA channel 1 |
| `P8` | XMB cells, world mips | Palette expand. Wrap is a mask |
| `A1` | 8×8 HUD glyphs, XMB labels | Pen color |
| `SRC_OVER` | Icons | A = 0 keeps the destination |
| `DST_COLOR_ONE` | The ribbon | A run of destination-brighten. This is the ribbon, not a 32×32 mesh |

Textures are power-of-two, edge ≤ 128, in flash or in an 8 KB arena. The 384² atlas is not a PicoGL texture. SegUI's own painter stays on `dp_pico_put_pixel` in v1.

Quake's world is drawn at internal 160×160 and stored 2× into the 320 strip. Shade and texture work drop by four. The wire cost stays 320 wide.

### Counters and targets

```
picogl <client> <ms> tri=<n> span=<n> drop=<n> bands=<n>
```

`ms` is the whole animated present: the span, the blits, and the DMA. SPI stays 31.25 MHz. Phase 2 prints this from `core_segui.c` with `TIMER0_TIMERAWL` while GLGears is moving.

| Client | Target |
|---|---|
| GLGears, mask `0x0007FFFC` (17 bands) | 20–22 fps. 17 × 2.62 ms is about 45 ms |
| XMB | 12–19 fps. 19 when the ribbon fits in the band DMA |
| Quake, standing still | 12–19 fps. Bins are reused until the player or a zombie moves |
| Quake, in a fight | 12 fps or better, and `drop` is printed if a band overflows |

### Memory added on top of the shell

| Object | Bytes |
|---|---|
| Strips | 20,480, already in `dp_pico` |
| Bins | 7,680. One set, 16 triangles a band |
| Span pool | 4,096, phase 2, and only if the assert still holds |
| Depth | On the triangle (`iz`). No 10 KB depth picture |
| Texture arena | 8,192 |
| Core 1 stack | 1,024 |
| Context | ~2 KB |
| Start-level slots | < 4 KB |
| Total beside the shell, phase 1 bins | 7,680. Later rows have to share the 12,848 bytes that were free. `__sram_used__` stays ≤ 215,040 |

Phase 3 dropped the 96 KB arena with TinyGL. Gears color and iz are static BSS: 112×96 RGB565 (21,504) plus int32 iz (43,008). `pico_tinygl_rt.c` stays for `sinf` and `sqrtf`. There is no 112×96 RGBA buffer. On the 2026-11-22 rp2040 elf, `__sram_used__` is 173,580, so 41,460 bytes are free under 215,040.

## 4. Files

Desktop, virgl, EEGS, and V3D do not compile the PicoGL files.

| File | Role |
|---|---|
| `src/desktop/graphics/picogl/picogl.h` | Caps, Q10, band 16 |
| `picogl_ctx.c` | GLES door, stages 1–2 |
| `picogl_xform.c` | Stages 2–3 |
| `picogl_span.c` | Stage 4 on core 0, stage 5 on core 1, the seven `R_EEGS_*` on `PICO_REAL_HW` |
| `picogl_tex.c` | P8, RGB565 |
| `picogl_blit.c` | Stage 6 |
| `src/drivers/pico/picogl_present.c` | Stage 7 |
| `src/drivers/pico/pico_core1.c` | Wake core 1, FIFO, stack |
| `src/media/xmb/xmb_pico.c` | Page 2. Does not link `xmb.c`, `xmb_wave.c`, or the atlas |
| `src/media/quake/sim/start_sim.c` | The C simulation in §2. Linked only into `make picocalc-quake` |

`r_eegs.c` keeps the PS2 body behind `BTRON_PS2_TARGET`.

XMB paint, inside the band: gradient fill, ribbon as `DST_COLOR_ONE`, 32² P8 cells as `SRC_OVER`, A1 labels. One category row and one icon row. Cells come from the names in `xmb_icons.h`, regenerated at 32², and only the cells this page draws are in the header.

`make picocalc-quake` is a sibling target. Same linker script, elf `btron-picocalc-quake-$(PICO_BOARD)`, flag `-DPICOCALC_APP_QUAKE`. It adds the blob and `start_sim.c`. The sim calls `picogl_tris` directly. It does not add `r_brush.c`, `r_alias.c`, the progs VM, the server, or the Quake menu.

## 5. Decisions

| Decision | Reason |
|---|---|
| The seven `R_EEGS_*` calls are the scene API | Quake faces and cooked models already want this shape. A second GLES world path would rebuild it |
| GLGears stays the center of `make picocalc`, XMB is page 2 | Both have to be on that image, and the 3×3 is full. Page 1 does not lose an app |
| The Quake image keeps the shell and replaces only the center tile | Same rule as the gears slot. The shell is not a strip-down |
| The start level is a C sim, not `progs.dat` | 385 edicts at 780 bytes do not fit beside SegUI on RP2040 or RP2350 |
| Changelevel stays in the hall | The image ships one map. The slipgate line is the ending |
| Alias is on for Pico and off for PS2 EEGS | The hall is not playable without the zombie and the gun. The GS 30 fps slice still skips alias |
| Core 1 rasters, core 0 submits and scans | Pico3D's split, on a band. The pixel loop is alone in scratch and owns the divider |
| The framebuffer is the 16-line strip | A full frame does not fit |
| Rectangles go through BitBlt | The ribbon, the icons, and the HUD are not meshes |
| Internal 160, stored 2× | Quarter the shade work. The scan stays 320 |
| v1 is silent | The 256 KB model cap and the 2 MB flash come first |

## 6. Open questions

1. Settled in Phase 4. Inside the Quake app, keys `1`–`8` select a carried weapon and `e` cycles. Ctrl (`0x7E`) or `f` fires. Launcher keys `1`–`9` still open apps only while the launcher is up. F1 stays Home.
2. Depth and buckets are settled by the Phase 1 measurement, not left open. `.build/picocalc-rp2040/btron-picocalc-rp2040.elf` was data 3,268 + bss 198,924 = 202,192, against an assert of 215,040. Free: 12,848. Depth stays on the triangle. One bucket set, 16 triangles a band, 7,680 bytes. A per-pixel depth picture and a second bucket set do not fit beside SegUI. Core 0 refills the bins after core 1 finishes the frame.
