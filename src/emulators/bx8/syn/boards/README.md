# BX-8 boards

The wrappers instantiate `mkTopMSXTurboR` through `msx_soc_shell.v`. The shell answers memory with zeros. There is no SDRAM or DDR controller, no pin constraint file, and no bitstream.

A working set for one power-house machine is a 4 MB MegaROM (Neon), the 512 KB mapper, and the 128 KB VRAM, about 4.7 MB. Two cartridge images want more than 8 MB. Every board below has at least 32 MB of external RAM.

## EP4CE15F17C8 is not a target

Cyclone IV E EP4CE15 has 15,408 logic elements and 56 M9K blocks, 504 Kbit (63 KB). The F17 package is a 256-ball FBGA and bonds 165 user I/Os. C8 is the slow speed grade. 63 KB is smaller than the VRAM, so a MegaROM cannot live on the die. Cards exist that add SDRAM to this package. The fabric is still 15,408 logic elements, which is the original OneChipMSX class, not room for this VDP, two cartridge windows, the R800, and a memory controller.

## Replacement

`qmtech_ep4ce55/` — QMTECH Cyclone IV core board, EP4CE55F23C8N. 55,856 logic elements, 2,340 Kbit on chip, 324 user I/Os. The Land Boards RETRO-EP4CE15 note lists this card with 32 MB Winbond W9825C6KH-6 and a 50 MHz crystal. PLL ratio 50 MHz × 63 / 55 = 57.2727 MHz.

## Three cheap boards

`icesugar_pro/` — Muse Lab iCESugar-Pro. Lattice LFE5U-25F-6BG256C, 24K LUT, 32 MB IS42S16160B, 25 MHz. This is the Yosys target in `syn/yosys/ecp5_synth.ys`. Block RAM is 1,008 Kbit (126 KB), so VRAM stays in the SDRAM.

`qmtech_xc7a35t/` — QMTECH Artix-7 core board, XC7A35T-1CSG325C. 33,280 logic cells, 1,800 Kbit block RAM, 32 MB W9825G6KH-6, 50 MHz. MMCM ratio 50 × 63 / 5 / 11 = 57.2727 MHz.

`tang_mega_60k/` — Sipeed Tang Mega 60K SOM, GW5AT-LV60PG484A. 59,904 LUT4, 512 MB DDR3 on the SOM. This is the large-RAM board. Primer 20K was a different card, a GW2A-18, and that directory is gone. The Sipeed wiki does not name the oscillator, so the wrapper does not invent a PLL. `clk_soc` is the input clock until the schematic's frequency is written down.

## Credits

* Namdak Tonpa
