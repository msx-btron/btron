/*
 * ps2_sys_vol.s -- the /SYS volume, carried inside the image's .data
 *
 * The B-System filesystem layer needs a block device before anything in it can be opened.
 * /SYS is the instant RAM volume (fonts, C-BIOS, drone1m1.dem): mkbtronfs writes
 * btron_sys.vol from tools/manifest.txt, the same image the hosted targets mount.
 * Large payloads (QUAKE/PAK0.PAK, maps) stay on the ISO and stream through CDVD
 * (doc/txt/PS2.txt §20.2).  This file names one path and describes no record.
 *
 * Two details are load-bearing and neither is a style choice.
 *
 *   `.balign 1024` -- BTRON_BLOCK_SIZE is 1024, so an aligned base makes every offset
 *   blk_mem_create's callbacks reach (lba * block_size) aligned too, and every memcpy in the
 *   volume layer a whole number of aligned words.  Unaligned lw/sw on a MIPS is an address error,
 *   and an image whose volume happens to start on an odd paragraph would find it in the first
 *   `dir`, not in the build.
 *
 *   `.section .data.ps2_sys_vol, "aw"` -- "w" because the filesystem writes.  .data is RDRAM once
 *   the loader has done its work, so `mkf` and `vol_sync` operate on the copy in RAM: the volume is
 *   writable, and it is writable from no address, no copy and no second buffer.  "a" because it has
 *   to survive the link, and the `.data.*` spelling is what ps2.ld's `*(.data.*)` collects.  What
 *   does not survive is a reboot: the next boot re-extracts the image the disc carried.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

	.section .data.ps2_sys_vol, "aw", @progbits
	.balign 1024

	.globl	ps2_sys_vol_image
ps2_sys_vol_image:
	.incbin	"btron_sys.vol"

	.globl	ps2_sys_vol_image_end
ps2_sys_vol_image_end:
