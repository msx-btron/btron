# Desktop resolutions and common controls

The enum is `include/btron/desktop_res.h`. The page is `mobile.html`. The hero is the five appliance tiers. The resolution table, the control matrix, and the tier bands sit under that hero.

## Engine rule

`btron_desk_for_res()` reads a `BTRON_RES`. It does not measure the framebuffer.

| Requested mode | Tier | What paints |
| --- | --- | --- |
| 1280×1024, 1024×768, 800×600 | DESKTOP | Window compositor. Desk, kiosk, and vehicle cluster. |
| 640×480, 480×864, 480×854, 480×690, 240×432, 240×400, 240×345 | HANDHELD | Communicator held in two hands. Published frames are 480×640 portrait. The six smaller and taller sizes are the FOMA panels that were missing from the list. 240×320 stays the portrait of 320×240. |
| 320×240, 240×160, 208×208, 176×208, 176×144, 160×144 | POCKET | One-hand panel, under 320×320. |
| 128×160, 160×120, 128×128, 128×96, 128×64 | BAND | Fits in 128×160. Sub-pico. Same SegUI painter as POCKET. |
| 96×60, 96×64, 96×68, 88×72, 84×84, 84×48 | CALC | Both sides under 100. Sub-pico. Same SegUI painter as POCKET. |

`doc/txt/PICO.txt` still prints 800×600 under its old banner. The enum follows the table above. `BTRON_UI_SEGUI` keeps its numeric value. POCKET, BAND, and CALC are three published tiers on that one painter. PicoCalc at 320×320 is the pocket reference panel and is not in the list.

## Settings combobox

Display (`src/desktop/settings/display.c`) still offers three radios: 1024×768, 800×600, and 1920×1080, plus the OpenGL radios. 1920×1080 is not in `PICO.txt`. Leave those rows in place.

Add one combobox whose items are `btron_res_spec(i).name` for every `BTRON_RES`. Group the list by `btron_desk_for_res()` under DESKTOP, HANDHELD, POCKET, BAND, and CALC. Apply stores the `BTRON_RES`. It does not retarget the running image: on the MCU, `BTRON_UI_MODE` is a compile-time constant. PicoCalc Preferences stay a SegUI list until a row-sized combobox exists. Do not add the Haiku dropdown to the RP2040 image.

The combobox is the Installer control, lifted out of `src/apps/volumes/b_installer.c` (`draw_haiku_combobox`, the 24 px arrow tab, the open and closed well, the dropdown hit test). Installer calls the shared control. Do not draw a second copy.

## Control library

There is no single control library. Do not delete a drawer to make one. A later `include/btron/ctrl.h` is a desktop face that calls the drawers that already exist.

| Control | Keep | Desktop face |
| --- | --- | --- |
| Checkbox, radio, button | `include/btron/settings_dlg.h` | call `settings_draw_ui_*` |
| Progress bar | `include/btron/progress_bar.h` | call `btron_progress_bar_paint` |
| Scrollbar, switch, folder tab | `include/btron/pmc.h` | call `pmc_draw_*` |
| Label | `drw_tc_string` | one wrapper |
| Textbox | none yet | new single-line field. `FOMA_MODAL_INPUT` stays the handset field |
| Textarea | `gterm`, `t_editor`, `SEGUI_W_TEXT` | do not fold those documents into the widget |
| Group panel | `SETTINGS_SECTION` | frame with the same bevel the settings dialog already draws |
| Combobox | Installer, private | move into the desktop face |

HMI (`include/btron/hmi.h`, `src/desktop/industrial/hmi/`) stays the instrument panel: push, toggle, triad, up/down, radio, rotary, slider, dial, bar meter, LED, digital display, universal pad. It is linked from `COMMON_SRCS` and `ARM64_APPS_SRCS`. It is not in the handset image, the workstation image, or the PicoCalc link. SegUI stays the sub-pico face for POCKET, BAND, and CALC. Handset items, soft keys, and modals stay the HANDHELD face.

The matrix on `mobile.html` is two row groups. The first is the nine common controls. The second is the twelve HMI types. Columns are DESKTOP, HANDHELD, and POCKET. BAND and CALC share the POCKET painter, so they are not extra columns. A Yes cell names the link that actually contains the control.

## Not in this step

No settings applet edit. No PicoCalc relink. No rename of `BTRON_UI_SEGUI`. No new screenshot set for DESKTOP. `make segui-screens` rewrites the sub-pico gallery from `scripts/screen_update_segui.py` and keeps the POCKET · BAND · CALC heading. The HANDHELD gallery marker comment stays `<!-- µBTRON-FOMA Mobile UI Showcase -->`.
