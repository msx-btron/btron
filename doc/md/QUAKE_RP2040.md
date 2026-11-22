# Quake on RP2040

Stabilisation plan after the first device flash. The hall paints. The player still cannot play it and then leave it. This document is the work that makes the `start` level a background task on the quake image, and makes `make picocalc` a single home screen whose center is XMB.

PicoGL itself stays the pipeline in [PICOGL.md](PICOGL.md). Frame rates of 12 fps and 20 fps stay unmeasured. Nothing in this plan is a licence to claim them.

## Landed on the RP2040 images, 2026-11-22

Phase A is in the source and in both RP2040 links. Phases B, C, and D are not. Nothing was flashed from this pass. The title counter is a readout, not a measured frame rate.

- `make picocalc` is one 3×3. The center tile is XMB. Left and Right do not change pages. Key `5` opens `PALM_APP_XMB`. Keys `1`–`4` and `6`–`9` open Terminal through Prefs by index (`PALM_APP_TERMINAL + (key - '1')`). They do not skip a slot. `PALM_APP_GLGEARS` is not renumbered. GLGears opens from the XMB Applications row. Games / Quake on this XMB stays `XMB_GO_NONE`.
- The quake image has no XMB. Its center is still Quake, and key `5` stays that app.
- XMB paint is a gradient plus an integer floor. The selected category stays at the body center. Items are a vertical column under that center. The sliding square is gone. `s_glow` only changes floor-line brightness. `make test-xmb-pico` printed `xmb pico ok`.
- `ps` is 40 columns. The headers are `ID PRI STATE SWITCH C NAME` and `CORE STATE QUEUED REF NAME`. QEMU's core-1 row is `   1 OFF`. `make test-pico` passed with those markers. There is no `kbd` row and no `quake` row.
- Typematic is inside `i2ckbd_poll_event`, not a task. One edge, then delay, then repeat. Defaults are 400 ms and 80 ms. Prefs rows Key Delay and Key Repeat cycle `{250, 400, 600, 1000}` and `{40, 80, 120, 200}` and call `i2ckbd_set_repeat` from `segui_palm_init` on hardware. A release is not a second shell character. The 1.5 ms I2C wait is still a busy wait on `segui_task`.
- `dp_pico_present` bumps `dp_pico_present_gen` once per present. Quake renders only when that generation changes. Later bands blit. Gears skips a redraw when the generation and the angle match. `picogl_gl_end` no longer joins core 1. The flush before the blit is the join. The DMA wait stays. The 60 ms poll gate is gone. The 2000 ms key latch and the physics constants are unchanged.
- Animated PicoGL apps rewrite the status pill as `<caption>  nnn FPS` after 250 ms or 30 frames, then dirty bands 0 and 1 on the next present. The UART line is unchanged.

| Image | `__sram_used__` | Free under 215040 | UF2 |
|---|---|---|---|
| `btron-picocalc-rp2040` | 174248 (`0x2a8a8`) | 40792 | 906240 |
| `btron-picocalc-quake-rp2040` | 185880 (`0x2d618`) | 29160 | 1594880 |

Gears `__bss_end__` is `0x2002a8a8` (text 449944, data 2984, bss 171264, payload bin 452928, SD-boot 452672). Quake `__bss_end__` is `0x2002d618` (text 794320, data 2872, bss 183008, payload bin 797192, SD-boot 796936). Both sit under the 4 KB stack guard. RP2350 was not relinked. `make flash-picocalc` still writes only `btron-picocalc-rp2040.uf2`. Quake movement is only in `btron-picocalc-quake-rp2040.uf2`.

## What the flash showed

The device showed the introduction hall, the HUD `H100 A000 AX 00`, and the softkeys. That is `start_sim_render` completing one frame. Movement and the shell did not change, for three concrete reasons.

The sim, the raster submit, the reap, the blit, and the panel DMA all run inside `segui_task` in `src/cores/core_segui.c`. `start_sim_render` calls `picogl_flush_target()` before the HUD, which waits until core 1 has finished the triangles. `dp_pico_present` then waits for each band's DMA. Core 0 is inside that one task for the whole frame. The Nexus ring overlaps transform and span only during the submit loop. The user-visible frame is still one blocking pass.

The keyboard read is on that same task. `i2c1_read_u16` still busy-waits 1.5 ms (`pico_delay_us(1500)`) on every poll. The 60 ms animation gate is gone, and the present no longer re-renders the hall once per band, so a held arrow can refresh the 2000 ms latch. Repeat is typematic inside the poll. It is not its own task. `ps` still cannot be opened from the SegUI terminal while this loop owns the screen, because F1 and Esc call `segui_palm_switch`, and `build_quake` calls `start_sim_init()` on every entry. Leaving the hall destroys the player. Phases B and C are the fix for that, and they are not in the image.

`make flash-picocalc` writes the gears/XMB UF2. The quake control changes live only in `btron-picocalc-quake-rp2040.uf2`. A gears flash cannot show them.

## Decisions

1. **One home screen on `make picocalc`.** The 3×3 grid stays. The center tile is XMB. Left/Right no longer changes pages. GLGears stays a Palm app and is opened from the XMB Applications row. `PALM_APP_GLGEARS` is not renumbered. Key `5` is the only special launcher key: it opens XMB. Keys `1`–`4` and `6`–`9` stay `PALM_APP_TERMINAL + (key - '1')`, so they are Terminal, Editor, Browser, Cabinet, Contacts, Phone, Chat, and Prefs. They do not skip an index.

2. **The quake image keeps Quake in the center.** That elf has no XMB and no GLGears. SRAM still has to hold the 128×96 view.

3. **Keyboard is its own core-0 task.** It is the only caller of `i2ckbd_poll_event`. The 1.5 ms southbridge wait becomes `pico_tsk_delay`, so the CPU returns to the scheduler. Events travel on one Nexus SPSC ring. A full ring refuses and counts the refusal. The producer does not spin inside `snd`.

4. **One consumer owns the key ring.** The owner is `segui` or `quake`. The handoff reaps the ring, then flips the owner. Two tasks never call `rcv` on the same ring.

5. **Quake is a core-0 task that can wait.** Entering the center tile resumes it. F1 and Esc clear the foreground flag and return to the launcher without `start_sim_init()`. The task blocks on that flag. Position, yaw, weapons, and doors stay. The terminal can then run `ps`.

6. **Core 1 is still not a second scheduler.** `ps` keeps the task table for core 0, then the `CORE` table. The core-1 row is `picogl` with state OFF, RDY, RUN, or ERR, plus queued and refused. While Quake is suspended and the ring is empty, that row is RDY if the wake succeeded. It is OFF only when core 1 was never launched.

7. **SegUI owns the panel.** `dp_pico_present` stays in `segui_task`. The quake task writes `s_rgb` only while it is foreground. A suspended quake task does not tick and does not post triangles.

8. **XMB geometry has one origin.** The selected category is centered. Its items are a vertical column under that center. The pad, the icon, and the label use that same x and the selected index. Left and Right change category. Up and Down change item.

9. **Stacks stay inside the eight slots already reserved.** `PICO_MAX_TASKS` is 8 and each slot already has a 4 KB stack on hardware (`s_stacks` is static). Steady tasks become idle, console, segui, kbd, and, on the quake image, quake. The self-test's `t_prod` and `t_cons` still fit. Do not raise the task count or the stack words.

10. **No second bucket set, no cooked-blob rewrite, no `progs.dat`.** Flat shade, the 24-byte triangle, cull, and the strict depth test stay. The span ring stays 32 slots.

## Why the shell felt stuck

```
segui_task
  i2ckbd_poll_event          busy 1.5 ms, at most once per frame
  start_sim_tick             physics on this task
  start_sim_render
    picogl snd ...           core 1 paints during this loop
    picogl_flush_target      reap joins core 1
    HUD into s_rgb
  dp_pico_blit_rgb565
  dp_pico_present            DMA wait per band
  pico_tsk_delay(1)
```

`console` can run on a yield, which is why UART can still answer. The SegUI terminal is the same `segui_task` that is inside the frame, so it cannot. `build_quake` resets the sim on the way back in, so a trip to the terminal would also throw away the hall.

## Target shape

```
kbd (core 0)  --snd-->  key ring  --rcv-->  owner (segui or quake)
segui (core 0)          presents the panel, paints the terminal, runs ps
quake (core 0)          tick + submit while foreground, then wait on a flag
picogl (core 1)         drain the span ring, wfe when idle
```

`ps` that shipped, and that `make test-pico` checks, is the 40-column printer. QEMU has no core 1 raster thread, so the second row is OFF:

```
ID PRI STATE SWITCH C NAME
 0 255 RDY      171 0 idle
 1  20 RUN       87 0 console
CORE STATE QUEUED REF NAME
   0 RUN        0   0 rtos
   1 OFF        0   0 picogl
```

`scripts/test_pico.sh` keeps `ID PRI STATE SWITCH C NAME`, `CORE STATE QUEUED REF NAME`, and `   1 OFF`.

After phases B and C, the same columns should also show `segui`, `kbd`, and `quake`. `quake` is FLG or DLY while it waits. `kbd` is DLY between polls. A launched core 1 with an empty ring is RDY. That sample is the target. It is not what the current elf prints. QEMU still has no southbridge and no quake task.

## XMB

The first device paint was a horizontal category row, a horizontal item row, and a square that slid with `s_phase`. The pad and the label did not share an x. Up and Down changed category.

What is linked now:

- The selected category is locked at the body center. Neighbours sit left and right and shrink with distance. There is no page scroll and no sliding square.
- The floor is integer horizon lines and rays. `s_glow` only changes their brightness.
- Items of that category form a column on the category center. The pad is that cell. The label is centered on the same x.
- Left / Right: category, and the item returns to 0.
- Up / Down: item.
- Enter launches the focused item. Games / Quake on this elf stays `XMB_GO_NONE`. Applications / GLGears still switches to `PALM_APP_GLGEARS`.
- `s_launch_page` is gone. The home is one screen.

A host check locks the geometry: after `xmb_pico_move(+1, 0)` the reported category anchor x is unchanged (it is the center), and the selected item index is 0. After `xmb_pico_move(0, +1)` the item index advances and the item anchor x equals the category anchor x. The label string is the selected item's label.

## Phases

Each phase has one deliverable and a done-when. The SRAM assert (`__sram_used__` ≤ 215040, `__bss_end__` at least 4 KB under `__stack_top__`) is a gate on every phase that links an image. SegUI's eight shell apps stay on both images. Do not flash unless asked. Do not commit unless asked.

### Phase A — Home is one screen, XMB is centered

**Deliverable.** `make picocalc` opens a 3×3 launcher. The center control is XMB. The XMB painter and the keys share the anchors above.

**Done when.** A host test passes the anchor checks. Key `5` from the launcher switches to `PALM_APP_XMB`. Key `1` still opens Terminal. GLGears opens from the Applications row. The quake image still builds with Quake in the center and does not link `xmb_pico.c`. `__sram_used__` on the RP2040 gears elf stays under 215040.

**Files.** `segui_palm.c`, `segui_palm.h`, `xmb_pico.c`, `xmb_pico.h`, `verify/tests/test_xmb_pico.c`, `doc/md/PICOGL.md`.

**Status.** Landed on the RP2040 gears elf. `make test-xmb-pico` passed. The quake elf still has no `xmb_pico`. `__sram_used__` is 174,248.

### Phase B — `kbd` task

**Deliverable.** `pico_ui_start` creates `kbd` beside `segui`. The task posts `{key, down}` into a Nexus ring of 16 slots (16 × 4 bytes plus the cursors). `segui_task` no longer calls `i2ckbd_poll_event` or `i2ckbd_poll_key`. The I2C helper waits with `pico_tsk_delay(2)` instead of `pico_delay_us(1500)`.

**Done when.** A host test of the ring posts a press and a release in order, and a full ring returns refusal without spinning. `make test-pico` still passes, because QEMU does not create `kbd`. On the picocalc link, `llvm-nm` shows `kbd` as a task name string and does not show `i2ckbd_poll_event` referenced from `core_segui.c`. Both RP2040 images still link under the SRAM assert.

**Files.** `i2ckbd_stm32.c`, `core_segui.c`, `core_pico.c` only if the weak `pico_ui_start` must create nothing new, a new `pico_kbd.c` or the ring beside the existing driver, `include/btron/nexus_ring.h` used as-is.

**Law.** The producer calls `snd`. On refusal it increments the ring's own counter and drops that event. It does not reap inside `snd`. The consumer reaps by reading. Timeout and retry belong to the owner, at its next wake.

**Status.** Not started. Repeat and delay are in the poll, and Prefs can change them. That is not this task.

### Phase C — `quake` task, suspend, `ps`

**Deliverable.** On `PICOCALC_APP_QUAKE`, `pico_ui_start` creates `quake`. The task blocks until the foreground flag is set. While the flag is set it reads the key ring (it is the owner), calls `start_sim_key`, `start_sim_tick`, and `start_sim_render`, then delays one tick. `segui_task` blits `start_sim_pixels()` and presents. F1 and Esc clear the flag, reap the span ring, and switch to the launcher. The next entry sets the flag and does not call `start_sim_init()` if the task has already started once. A new game is an explicit menu action.

**Done when.** A host test calls the suspend and resume hooks around `start_sim_key` / `start_sim_tick` and the origin is the same after resume. `make test-start-sim` still passes (walk, door, supershotgun, rocket, slipgate sentence, lit frame, axe top rows, long Up, Left, jump). The quake RP2040 elf links. `__sram_used__` stays under 215040. `ps` source still prints both headers. QEMU's core-1 row is still OFF.

**Files.** `core_segui.c`, `segui_palm.c` (`build_quake` must not reset a live sim), `start_sim.c` (init split from first boot), `pico_core1.c` (reap on suspend so the row can return to RDY).

**`ps` rows while the terminal is up and Quake has been opened once.**

| Name | Core | State |
|---|---|---|
| idle, console, segui, kbd | 0 | as the scheduler has them |
| quake | 0 | FLG while waiting for the foreground flag |
| picogl | 1 | RDY, queued 0, once the wake has succeeded |

Opening Quake again sets the flag. The task leaves FLG. Core 1 moves to RUN only while `picogl_span_queued() > 0`.

**Status.** Not started. The hall still lives inside `segui_task`, and leaving it still calls `start_sim_init()`.

### Phase D — The next tick overlaps the panel

**Deliverable.** After a foreground frame is in `s_rgb` and has been blitted into the strip, `segui_task` kicks DMA and returns to the scheduler before the next band wait. The quake task may run `start_sim_tick` and start the next submit during that DMA. `kbd` runs on its own delay, including during DMA. The reap still happens before the blit reads `s_rgb`. The DMA wait stays. XMB rectangles stay on core 0.

**Done when.** The UART line is still `picogl <client> <ms> tri= span= drop= bands=`. A host test still reads pixels only after `picogl_flush_target()`. No fps number is written into the docs. The long-Up host check still moves.

This phase is the one that can change how fast the hall feels. It does not, by itself, make the keys work. Phases B and C do that.

**Status.** Not started. The present-generation guard stopped the hall from rendering about 17 times per visible frame. It does not overlap the next tick with DMA.

### Phase E — Gates

**Deliverable.** The same commands as today, from `/Users/tonpa/depot/bitedits/btron`:

- `make test-start-sim`
- `make test-picogl-bin`
- `make test-picogl-raster`
- `make test-pico`
- `make picocalc`
- `make picocalc-quake`

**Done when.** Each of those exits 0. RP2040 `__sram_used__` is recorded in this file from `llvm-nm` of `__sram_used__`. RP2350 is relinked only if a phase touches a file both boards compile. The quake elf has `build_quake`, `start_sim_render`, and the `quake` task symbol, and it has no `build_xmb`. The gears elf has `build_xmb` and the center launch going to `PALM_APP_XMB`. Neither elf gains `ztriangle`, `e1m1`, or `progs.dat`.

## What stays

- Flat faces, `tex = -1`, one uv, 24-byte `picogl_tri_t`.
- Cull default off. Depth test strict `>`. Screen-Y-down area test unchanged.
- `|ndc| > 1.25` drops the triangle. Viewmodels draw at eye + forward×16.
- Arrow map: `0xB5` forward, `0xB6` back, `0xB4` turn left, `0xB7` turn right. `q` / `z` look. Weapon keys `1`–`8` and `e` stay. Ctrl `0x7E` fires.
- Hold latch `0xFFFFFFFF`, stamped to `s_now + 2000` inside the tick. The 4-unit floor probe runs only when vertical speed is not upward.
- Hitscan slop of 32. `contents_at` returns the leaf id.
- One 16-triangle bucket set. Span ring of 32. `paint_row` stays in `.scratch_x`.
- `make flash-picocalc` still flashes the gears UF2. The quake UF2 is copied by hand until a flash target is asked for.
- Cooked blob stays generated under `.build/` and is not committed.

## SRAM budget

The landed table at the top of this file is the current RP2040 link. Phases B and C have not added a key ring or a task.

The task stacks are already in that number (`s_stacks[8][1024]` on hardware). A 16-slot key ring is a few hundred bytes. The plan does not add a framebuffer, a second view, or a second bucket set. If a phase would push `__sram_used__` over 215040, that phase stops and drops the new object. It does not drop a SegUI app to pay for it.

## Out of scope

- Measuring or advertising 12 fps or 20 fps.
- Flashing, or adding `flash-picocalc-quake`, unless asked.
- A QuakeC VM or `progs.dat`.
- Putting the sim on the gears image. Games / Quake on that XMB does not launch.
- A scheduler on core 1.
- Recooking `pak0.pak`.
- MSX, Bad Apple, and the cabinet text.

## Done when, for the whole plan

On the quake UF2, the user walks the hall with the arrows, opens the door, and fires a picked-up weapon. F1 returns to the launcher without resetting that position. The terminal accepts `ps` and shows `kbd`, `quake`, and `segui` on core 0, and `picogl` on core 1. Entering Quake again continues the same position.

On the gears UF2, the home screen is one 3×3 page, the center tile opens XMB, the highlight sits on the same cell as its label, and GLGears opens from the Applications row.
