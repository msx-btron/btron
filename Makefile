# B-System Multi-Target Makefile
# Cleanroom / Sakamura T-Kernel 2.0 / POSIX / QEMU / BCM283x Bare-Metal / UEFI / PC-98
#
# Targets & Kernels:
#   posix         B-System/BTRON3 3.20 (posix-hosted) Hiroaki Takada — Cleanroom TRON Kernel [Target 0]
#   qemu          B-System/BTRON3 3.20 (arm-qemu-virtio) Hiroshi Tokita — Cleanroom TRON Kernel [Target 1]
#   sakamura      B-System/BTRON3 3.20 (sakamura-tkernel-virtio) Ken Sakamura — T-Kernel 2.0 [Target 3]
#   arm-elf       B-System/BTRON3 3.20 (armv7-bcm2836) Takahiro Yokobayashi — T-Kernel 2.0 [Target 2]
#   uefi          B-System/BTRON3 3.20 (x86_64-uefi-smp) Kota Uchida — T-Kernel 2.0 [Target 4]
#   pc98          B-System/BTRON3 3.20 (i386-pc98) Awe Morris — T-Kernel 2.0 [Target 5]
#   arm64-elf     B-System/BTRON3 3.20 (aarch64-bcm2711) Takanori Yokoyama — T-Kernel 2.0 [Target 6]
#   m68k          B-System/BTRON3 3.20 (m68k-q800) Motorola 68040 — Cleanroom TRON Kernel [Target 7]
#   ps2           B-System/BTRON3 3.20 (ps2-ee) Sony PlayStation 2 — Cleanroom TRON Kernel [Target 8]
#   mips          B-System/BTRON3 3.20 (mips-malta) Bare-Metal MIPS — Cleanroom TRON Kernel [Target 9]
#   foma          B-System/BTRON3 3.20 (foma) Cleanroom BTRON UI for FOMA devices [Target 10]
#
# Run Commands:
#   run-posix     Boot POSIX Microkernel Desktop (btron-posix)
#   run-qemu      Boot QEMU VirtIO Desktop (btron-qemu.elf)
#   run-kernel    Boot Pi 2B ELF in QEMU (btron-arm-baremetal.elf, raspi2b with display)
#   run-yoko      Boot Pi 3B AArch64 ELF in QEMU (btron-aarch64-baremetal.elf, raspi3b)
#   run-yoko4     Boot Pi 4B AArch64 ELF in QEMU (btron-aarch64-baremetal.elf, raspi4b)
#   run-sakamura  Boot Sakamura T-Kernel 2.0 Desktop (btron-sakamura.elf, display, kbd, mouse)
#   run-foma      Boot µBTRON-FOMA Mobile Workbench (btron-foma.elf, 480x640 portrait, AArch32 profile)
#   run-uefi      Boot x86_64 UEFI SMP in QEMU (btron-uchida.elf, aliases: run-eufi, run-uefu)
#   run-pc98      Boot NEC PC-9801/PC-9821 VM in QEMU (btron-morris.elf)
#   run-m68k      Boot Motorola 68040 Macintosh Quadra 800 in QEMU (btron-m68k.elf)
#   run-ps2       Boot Sony PlayStation 2 from btron-ps2.iso (ELF=1 for host ELF, empty tray)
#   run-mips      Boot Bare-Metal MIPS in QEMU Malta / Magnum (btron-mips.elf)
#   test-kernel   Test Pi 2B ELF in QEMU (raspi2b, serial-only, headless)
#   debug-gdb     QEMU + GDB stub on Pi 2B

COMMON_INCLUDES = -Iinclude -Iinclude/rt -Iinclude/graphics -Iinclude/drivers -Iinclude/drivers/broadcom -Isrc/kernel -Isrc/cores \
                  -Isrc/desktop -Isrc/desktop/graphics -Isrc/desktop/graphics/gles \
                  -Isrc/desktop/graphics/virgl -Isrc/desktop/graphics/v3d \
                  -Isrc/desktop/graphics/eegs -Isrc/desktop/graphics/tinygl \
                  -Isrc/desktop/window -Isrc/desktop/font -Isrc/desktop/tip \
                  -Isrc/desktop/tip/japanese -Isrc/desktop/tip/tibetan \
                  -Isrc/desktop/settings -Isrc/desktop/industrial \
                  -Isrc/apps -Isrc/apps/chat -Isrc/apps/clarity -Isrc/apps/browser \
                  -Isrc/apps/review -Isrc/apps/editor -Isrc/apps/workbench \
                  -Isrc/apps/volumes -Isrc/apps/clock -Isrc/apps/preferences \
                  -Isrc/apps/terminal -Isrc/apps/clu -Isrc/apps/chokanji -Isrc/apps/cabinet \
                  -Isrc/languages -Isrc/languages/forth -Isrc/languages/microscript \
                  -Isrc/media -Isrc/media/cassette -Isrc/media/paint -Isrc/media/orchestra \
                  -Isrc/media/xmb -Isrc/media/sdk -Isrc/media/quake/include \
                  -Isrc/drivers/broadcom -Isrc/drivers/playstation2

CC ?= gcc
CXX ?= c++
CFLAGS ?= -O2 -Wall -Wextra -std=c99 $(COMMON_INCLUDES)

# Feature-test macros for hosted POSIX targets.
# _DARWIN_C_SOURCE is a superset of POSIX.1-2008 and exposes BSD
# extensions (SIGWINCH, etc.) that strict _POSIX_C_SOURCE=200809L hides on macOS.
# On BSDs we expose __BSD_VISIBLE, _XOPEN_SOURCE, and _NETBSD_SOURCE.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S), Darwin)
    CFLAGS += -D_DARWIN_C_SOURCE
else ifneq ($(findstring BSD,$(UNAME_S)),)
    CFLAGS += -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D__BSD_VISIBLE=1 -D_NETBSD_SOURCE
else
    CFLAGS += -D_POSIX_C_SOURCE=200809L
endif

.PHONY: all posix qemu kernel tkernel sakamura foma uefi pc98 arm-elf arm64-elf m68k ps2 mips \
        check-structure test-ps2-softfloat test-arm-aeabi test-ps2-gs-pack test-ps2-gs-env test-ps2-gs-rec test-ps2-vu1-geom test-ps2-vu0-kernel test-gscube-nexus test-ps2-gs-poster test-ps2-gs-read test-ps2-gs-tex test-ps2-pointer test-ps2-fused-row test-ps2-gif-ring test-ps2-paintw test-eegs-raster test-eegs-geom test-eegs-present test-gl-math test-gl-backend-choice test-ps2-fb-order test-grid-map xmb-atlas test-xmb-atlas test-ps2-opengl-routing test-ps2-iso9660 test-demo-paths test-printf-float test-ps2-cdvd-loop \
        test-xmb-render test-xmb-render-tgl test-gl-context test-async-input \
        html2tad book2tad tad_bin test test-kernel test-yoko test-yoko4 test-m68k test-mips test-ps2 test-foma test-foma-ui foma-screens \
        segui-screens tbios-tools tbios-roms test-tbios \
        test-mozc test-editor test-hmi test-tad test-chat test-wylie verify test-fs test-sps test-chokanji test-quake test-replay \
        test-hull test-view test-secret test-render test-drone test-drone-all test-zexall msx-trace \
        test-mars-capture \
        run-posix run-qemu run-kernel run-yoko run-yoko4 run-sakamura run-foma run-uefi run-eufi run-uefu run-pc98 run-m68k run-ps2 run-mips debug-virtio debug-gdb clean \
        ps2-cfg ps2-dials \
        pi400 pi400-payload flash-pi400 fetch-pi400-fw \
        sec-libstr sec-be sec-clu sec-kernel sec-fs sec-drivers security security-report

QEMU_ARM     ?= qemu-system-arm
QEMU_AARCH64 ?= qemu-system-aarch64
QEMU_X86_64  ?= qemu-system-x86_64
QEMU_M68K    ?= qemu-system-m68k
QEMU_MIPS    ?= qemu-system-mipsel
QEMU_MIPS64  ?= qemu-system-mips64el
PCSX2_BIN    ?= /Applications/PCSX2.app/Contents/MacOS/PCSX2
# PCSX2 keeps every setting it has in one data directory, and -datapath names the
# parent of it: the emulator appends its own program name, so passing pcsx2/ puts
# the configuration at pcsx2/PCSX2/inis/PCSX2.ini.  That file is tracked here, so
# a run gets these settings rather than whatever the user-level config has drifted
# to.  BIOS images are the one thing kept outside the repo, symlinked file by file.
PCSX2_DATADIR ?= $(CURDIR)/pcsx2
PCSX2_DATA    := $(PCSX2_DATADIR)/PCSX2
PCSX2_BIOS    ?= $(HOME)/Library/Application Support/PCSX2/bios
M68K_CC      ?= m68k-elf-gcc

LLVM_CLANG := $(shell for p in /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang /usr/lib/llvm-*/bin/clang clang; do if command -v "$$p" >/dev/null 2>&1; then echo "$$p"; break; fi; done)
LLD_BIN    := $(shell for p in /opt/homebrew/bin/ld.lld /usr/local/bin/ld.lld /usr/bin/ld.lld ld.lld /opt/homebrew/opt/llvm/bin/ld.lld /usr/lib/llvm-*/bin/ld.lld; do if command -v "$$p" >/dev/null 2>&1; then echo "$$p"; break; fi; done)

ARM_LLD_FLAG := $(if $(LLD_BIN),-fuse-ld=$(LLD_BIN),-fuse-ld=lld)
# ARM32: Cortex-A7 for Pi 2B (BCM2836)
ARM32_CC ?= $(LLVM_CLANG) --target=arm-none-eabi -mcpu=cortex-a7 -marm $(ARM_LLD_FLAG) -ffreestanding -nostdlib
# AArch64: Cortex-A72 for Pi 4B (BCM2711) — kept for Pi4-only development
ARM64_CC ?= $(LLVM_CLANG) --target=aarch64-none-elf -mcpu=cortex-a72 $(ARM_LLD_FLAG) -ffreestanding -nostdlib
# IA-32 / X86 Freestanding: UEFI / PC-98
ifeq ($(shell uname -s), Darwin)
    X86_CC ?= $(LLVM_CLANG) --target=i686-none-elf -ffreestanding -nostdlib
    X86_LD ?= $(LLD_BIN) -m elf_i386
else
    X86_CC ?= $(if $(shell command -v i686-elf-gcc 2>/dev/null),i686-elf-gcc -ffreestanding -nostdlib,$(if $(shell command -v clang 2>/dev/null),clang --target=i686-none-elf -ffreestanding -nostdlib,$(CC) -m32 -ffreestanding -nostdlib))
    X86_LD ?= $(if $(shell command -v i686-elf-ld 2>/dev/null),i686-elf-ld,$(if $(LLD_BIN),$(LLD_BIN) -m elf_i386,ld -m elf_i386))
endif

# MIPS / PS2 Freestanding (Target 8: PS2 EE, Target 9: Malta / Magnum)
MIPS_CC     ?= $(LLVM_CLANG) --target=mipsel-unknown-elf -march=mips32r2 -mabi=32 -ffreestanding -nostdlib
# -march=mips2, not mips3: one O32 calling convention across the whole image, and
# still a subset of the R5900's MIPS III.  The ABI note under PS2_OBJS is the law.
# -msoft-float, likewise for the whole image and likewise an ABI question: the
# R5900 FPU is single-precision, so a hardware .d instruction is not merely slow, it
# is illegal and does nothing at all.  Under this flag a floating value travels in
# integer registers, so mixing it with a build that uses the FPU would corrupt every
# float argument that crosses the boundary -- and src/drivers/playstation2/ps2_builtins.c
# supplies the runtime the link then needs.
# Three objects take the FPU switched back on (see PS2_FP near %.pbench.o), chosen for the
# same reason: no float ever crosses into or out of them.  ps2_builtins.c *is* the
# soft-float runtime, so a bit pattern in an integer register is its argument by
# definition; vertex_stage.c is the geometry kernel, reached only by pointer, and the one
# conversion it needs is spelled through that runtime's own calling convention;
# xmb_wave.c is the launcher's waving surface, which takes the effect clock as IEEE-754 bits
# in an integer and hands the sheet out through memory, and whose two operations this CPU has
# no instruction for go back to the soft unit the same way -- xb_sinf_far_u32()/
# xb_cosf_far_u32(), bits in and bits out.  They take the flag because the EE's
# single-precision unit is what those files are for: 460,861 float operations in an XMB
# frame, at 28 cycles a libcall, was the price of applying the .d law to code that has never
# used a double.  Nothing else may take it: an object whose entry points take a float would
# move the ABI boundary rather than stay inside it, and gate row 57 is the check.  The law
# *inside* such a unit is a second thing again, and it is not about instructions -- see the
# register-allocation paragraph by PS2_WAVE_OBJ below.
PS2_CC      ?= $(LLVM_CLANG) --target=mipsel-unknown-elf -march=mips2 -mabi=32 -msoft-float -ffreestanding -nostdlib
MIPS_LD ?= $(if $(shell command -v mipsel-linux-gnu-ld 2>/dev/null),mipsel-linux-gnu-ld,$(LLD_BIN) -EL)

# BCM283x bare-metal flags (TYPE_RPI=2 → BCM2836, Pi 2B, Cortex-A7)
BCM_INC      = $(COMMON_INCLUDES)
ARM_CFLAGS   = -O2 -Wall -Wextra -std=c99 -mno-unaligned-access \
               -D_RPI_BCM283x_ -DTYPE_RPI=2 -DBTRON_TARGET=2 -mfpu=vfpv4 -mfloat-abi=softfp \
               $(BCM_INC)
ARM64_CFLAGS = -O2 -Wall -Wextra -std=c99 -mstrict-align \
               -D_RPI_BCM283x_ -DTYPE_RPI=3 -DBTRON_TARGET=6 \
               -DBTRON_GL_BACKEND_TINYGL \
               -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-unused-parameter \
               $(BCM_INC)

# Host OS / SDL2 detection
UNAME_S := $(shell uname -s)
EXEEXT :=
ifneq (,$(findstring MINGW,$(UNAME_S)))
    EXEEXT := .exe
endif

ifeq ($(UNAME_S), Darwin)
    SDL_CFLAGS   := $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/local/include/SDL2")
    SDL_LIBS     := $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2") \
                    -lz -lpthread -framework ApplicationServices -framework Cocoa
    QEMU_DISPLAY        := -display cocoa,show-cursor=on,zoom-to-fit=on
    # Match M68K display flags with show-cursor=on and zoom-to-fit=on
    KERNEL_DISPLAY      := $(QEMU_DISPLAY)
else ifneq (,$(filter $(UNAME_S),Linux FreeBSD NetBSD OpenBSD DragonFly))
    # Linux + the three BSDs (and DragonFly) - portable POSIX path, no Windows flags
    SDL_CFLAGS   := $(shell sdl2-config --cflags 2>/dev/null || \
                         pkg-config --cflags sdl2 2>/dev/null || \
                         echo "-I/usr/local/include/SDL2 -I/usr/include/SDL2")
    SDL_LIBS     := $(shell sdl2-config --libs 2>/dev/null || \
                         pkg-config --libs sdl2 2>/dev/null || \
                         echo "-lSDL2") \
                    -lz -lm -lpthread
    QEMU_DISPLAY        := -display default,show-cursor=on
    KERNEL_DISPLAY      := -display default
else ifneq (,$(findstring MINGW,$(UNAME_S)))
    SDL_CFLAGS   := $(shell sdl2-config --cflags 2>/dev/null || echo "-IC:/msys64/mingw64/include/SDL2 -Dmain=SDL_main")
    SDL_LIBS     := $(shell sdl2-config --libs 2>/dev/null || echo "-LC:/msys64/mingw64/lib -lmingw32 -mwindows -lSDL2main -lSDL2") -lz -lgdi32 -lwinpthread
    QEMU_DISPLAY        := -display default,show-cursor=on
    KERNEL_DISPLAY      := -display default
else
    # Generic Unix fallback - no -lgdi32
    SDL_CFLAGS   := $(shell sdl2-config --cflags 2>/dev/null || \
                         pkg-config --cflags sdl2 2>/dev/null || \
                         echo "-I/usr/local/include/SDL2 -I/usr/include/SDL2")
    SDL_LIBS     := $(shell sdl2-config --libs 2>/dev/null || \
                         pkg-config --libs sdl2 2>/dev/null || \
                         echo "-lSDL2") -lz -lm -lpthread
    QEMU_DISPLAY        := -display default,show-cursor=on
    KERNEL_DISPLAY      := -display default
endif

# ── Text Input Primitives (TIP) / Mozc IME sources ────────────────
IME_SRCS    = src/desktop/tip/japanese/mozc_kkc.c       \
              src/desktop/tip/tip_ife.c        \
              src/desktop/tip/tip_task.c       \
              src/desktop/tip/tip_vobj.c       \
              src/desktop/tip/tibetan/wylie.c          \
              src/desktop/tip/tibetan/tibetan_dict.c

# ── tronMSX core: the machine src/emulators/msx/msx_app.c drives ───────────────────
MSX_SRCS = src/emulators/msx/z80.c        \
           src/emulators/msx/r800.c       \
           src/emulators/msx/hd64180.c    \
           src/emulators/msx/v9938.c      \
           src/emulators/msx/s1985.c      \
           src/emulators/msx/t9769b.c     \
           src/emulators/msx/t9769c.c     \
           src/emulators/msx/slots.c      \
           src/emulators/msx/dsk.c        \
           src/emulators/msx/bios.c       \
           src/emulators/msx/vdp.c        \
           src/emulators/msx/psg.c        \
           src/emulators/msx/audio.c      \
           src/emulators/msx/scc.c        \
           src/emulators/msx/opll.c       \
           src/emulators/msx/pcm.c        \
           src/emulators/msx/y8950.c      \
           src/emulators/msx/ppi.c        \
           src/emulators/msx/kbd.c        \
           src/emulators/msx/geo3d.c      \
           src/emulators/msx/v9968.c      \
           src/emulators/msx/midi.c       \
           src/emulators/msx/msx_cfg.c    \
           src/emulators/msx/tronmsx.c

# ── CLU Applications (POSIX commands) ─────────────────────────────
CLU_APPS_SRCS = src/apps/clu/fs.c \
                src/apps/clu/ls.c \
                src/apps/clu/cp.c \
                src/apps/clu/rm.c \
                src/apps/clu/mv.c \
                src/apps/clu/ln.c \
                src/apps/clu/df.c \
                src/apps/clu/st.c \
                src/apps/clu/tp.c \
                src/apps/clu/ps_vm.c \
                src/apps/clu/sps.c \
                src/languages/forth/forth_core.c \
                src/languages/forth/forth_devtree.c \
                src/languages/forth/forth.c

CLU_APPS_HOST_OBJS = $(CLU_APPS_SRCS:.c=.host.o)
CLU_APPS_POSIX_OBJS = $(CLU_APPS_SRCS:.c=.posix.o)

# ── Common SDL2-hosted app sources ────────────────────────────────
BE_DASM_SRCS = src/apps/clu/be/native_dasm.c \
               src/apps/clu/be/arch/x86/insnsd.c src/apps/clu/be/arch/x86/insnsa.c \
               src/apps/clu/be/arch/x86/insnsb.c src/apps/clu/be/arch/x86/insnsn.c \
               src/apps/clu/be/arch/x86/disasm.c src/apps/clu/be/arch/x86/regdis.c \
               src/apps/clu/be/arch/x86/regs.c src/apps/clu/be/arch/x86/regflags.c \
               src/apps/clu/be/arch/x86/regvals.c src/apps/clu/be/arch/x86/iflag.c \
               src/apps/clu/be/arch/x86/sync.c src/apps/clu/be/arch/x86/disp8.c \
               src/apps/clu/be/arch/x86/nctype.c src/apps/clu/be/arch/x86/readnum.c \
               src/apps/clu/be/arch/x86/common.c src/apps/clu/be/arch/x86/alloc.c \
               src/apps/clu/be/arch/x86/string.c src/apps/clu/be/arch/x86/warnings.c \
               src/apps/clu/be/arch/x86/error.c src/apps/clu/be/arch/x86/ver.c \
               src/apps/clu/be/arch/arm64/BranchExcSys.c \
               src/apps/clu/be/arch/arm64/DataProcessingImmediate.c \
               src/apps/clu/be/arch/arm64/LoadsAndStores.c src/apps/clu/be/arch/arm64/bits.c \
               src/apps/clu/be/arch/arm64/strext.c \
               src/apps/clu/be/arch/arm64/DataProcessingFloatingPoint.c \
               src/apps/clu/be/arch/arm64/DataProcessingRegister.c \
               src/apps/clu/be/arch/arm64/armadillo.c src/apps/clu/be/arch/arm64/instruction.c \
               src/apps/clu/be/arch/arm64/utils.c src/apps/clu/be/arch/arm32/armv7.c \
               src/apps/clu/be/arch/riscv/riscv-disas.c src/apps/clu/be/arch/ppc/ppc_disasm.c \
               src/apps/clu/be/arch/m68k/dis68k.c src/apps/clu/be/arch/sh4/sh4.c \
               src/apps/clu/be/arch/mips/mips-rsp.c \
               src/apps/clu/be/arch/pdp11/pdp11.c

COMMON_SRCS = src/desktop/graphics/dp_core.c   \
              src/desktop/graphics/image_decode.c \
              src/desktop/graphics/icons_bundle.c \
              src/desktop/window/dnd.c         \
              src/desktop/graphics/dp_sdl.c    \
              src/desktop/font/troncode.c      \
              src/desktop/font/jis_fonts.c     \
              src/desktop/font/tibetan_fonts.c \
              src/desktop/font/font_mgr.c      \
              src/desktop/font/schrift.c       \
              src/desktop/window/wnd.c         \
              src/desktop/window/app_menu.c    \
              src/desktop/window/event.c       \
              src/apps/chokanji/pmc.c       \
              src/apps/chokanji/cab.c       \
              src/languages/microscript/microscript.c \
              src/apps/chokanji/clock.c     \
              src/apps/chokanji/kconv.c     \
              src/apps/chokanji/xfconv.c    \
              src/apps/chokanji/bpk_core.c  \
              src/apps/chokanji/unpack.c    \
              src/apps/chokanji/launchers.c \
              src/apps/cabinet/vobj.c       \
              src/apps/workbench/desktop.c    \
              src/apps/workbench/workbench.c  \
              src/apps/workbench/tracker.c    \
              src/apps/workbench/about.c      \
              src/apps/workbench/global_menu.c \
              src/apps/workbench/main.c       \
              src/apps/cabinet/vobj_manager.c  \
              src/apps/browser/tad_browser.c   \
              src/media/paint/paint.c         \
              src/apps/terminal/gterm.c         \
              src/apps/editor/t_editor.c      \
              src/apps/clarity/clarity.c        \
              src/apps/clarity/clarity_layout.c \
              src/apps/clarity/clarity_render.c \
              src/apps/clarity/clarity_export.c \
              src/apps/browser/pdf_reader.c     \
              src/apps/review/review.c         \
              src/media/cassette/audio_player.c  \
              src/apps/volumes/b_drivesetup.c  \
              src/media/orchestra/orchestra.c     \
              src/media/orchestra/orchestra_stage.c \
              src/media/orchestra/orchestra_arrange.c \
              src/media/orchestra/orchestra_piano.c \
              src/media/orchestra/orchestra_score.c \
              src/media/orchestra/orchestra_mix.c \
              src/media/orchestra/orchestra_insert.c \
              src/media/orchestra/orchestra_about.c \
              src/media/orchestra/orchestra_session.c \
              src/media/orchestra/orchestra_smf.c \
              src/media/sdk/gst_rt.c       \
              src/media/orchestra/lukhash_score.c \
              src/apps/chat/chat.c          \
              src/apps/chat/chat_xml.c      \
              src/emulators/msx/msx_app.c       \
              $(MSX_SRCS)              \
              src/desktop/settings/language.c  \
              src/desktop/settings/control_panel.c \
              src/desktop/settings/appearance.c \
              src/desktop/settings/desktop.c \
              src/desktop/settings/display.c \
              src/desktop/settings/input.c \
              src/desktop/settings/sound.c \
              src/desktop/settings/network.c \
              src/desktop/settings/media.c \
              src/desktop/settings/security.c \
              src/desktop/settings/system.c    \
              src/desktop/settings/terminal.c  \
              src/desktop/industrial/hmi/hmi_core.c       \
              src/desktop/industrial/hmi/hmi_switch.c     \
              src/desktop/industrial/hmi/hmi_selector.c   \
              src/desktop/industrial/hmi/hmi_volume.c     \
              src/desktop/industrial/hmi/hmi_meter.c      \
              src/desktop/industrial/hmi/hmi_controller.c \
              src/desktop/industrial/hmi/hmi_panel.c      \
              $(IME_SRCS)              \
              src/fs/blk_mem.c         \
              src/fs/blk_file.c        \
              src/fs/blk_qcow2.c       \
              src/fs/blk_part.c        \
              src/fs/vol.c             \
              src/fs/file.c            \
              src/apps/clu/lang.c           \
              src/apps/clu/term.c           \
              src/apps/clu/tty.c            \
              src/apps/clu/vfs.c            \
              src/apps/clu/sc/files.c       \
              src/apps/clu/sc/input.c       \
              src/apps/clu/sc/menus.c       \
              src/apps/clu/sc/sc.c          \
              src/apps/clu/tv/tv.c          \
              src/apps/clu/be/native.c      \
              $(BE_DASM_SRCS)          \
              $(CLU_APPS_SRCS)         \
              src/apps/clu/clu.c

QUAKE_SRCS = \
    src/media/quake/core/mathlib.c \
    src/media/quake/core/mem.c \
    src/media/quake/core/cvar.c \
    src/media/quake/core/cmd.c \
    src/media/quake/core/wad.c \
    src/media/quake/core/world.c \
    src/media/quake/core/pr_exec.c \
    src/media/quake/core/sv_phys.c \
    src/media/quake/core/sv_main.c \
    src/media/quake/sys/sys_btron.c \
    src/media/quake/sys/in_btron.c \
    src/media/quake/sys/fs_btron.c \
    src/media/quake/render/r_btron_gl.c \
    src/media/quake/render/r_eegs.c \
    src/media/quake/render/r_brush.c \
    src/media/quake/render/r_light.c \
    src/media/quake/render/r_surf.c \
    src/media/quake/render/r_alias.c \
    src/media/quake/render/r_part.c \
    src/media/quake/render/texture.c \
    src/media/quake/core/cl_demo.c \
    src/media/quake/app/quake_ui.c \
    src/media/quake/app/quake_app.c

DEMO_SRCS = \
    src/media/sdk/lilcu64_wa.c \
    src/media/sdk/lilcu64_score.c \
    src/media/sdk/lilcu64_synth.c \
    src/media/sdk/lilcu64_insert.c \
    src/media/sdk/lilcu64_demo.c \
    src/media/sdk/mars.c \
    src/drivers/playstation2/ps2_vu0_kernel.c

# Targets that already pull DEMO_SRCS must not list this again.
LILCU64_ENGINE_SRCS = \
    src/media/sdk/lilcu64_wa.c \
    src/media/sdk/lilcu64_score.c \
    src/media/sdk/lilcu64_synth.c \
    src/media/sdk/lilcu64_insert.c

VIRGL_SRCS = \
    src/desktop/graphics/gles/gl_dispatch.c \
    src/desktop/graphics/gles/gl_backend_choice.c \
    src/desktop/graphics/gles/egl_surface.c \
    src/desktop/graphics/virgl/backend_virgl.c \
    src/media/sdk/glgears.c \
    src/media/xmb/xmb.c \
    src/media/xmb/xmb_wave.c \
    $(QUAKE_SRCS) \
    $(DEMO_SRCS)

# ── POSIX build (Target 0) ────────────────────────────────────────
POSIX_STARTUP = src/cores/core_posix.c
POSIX_SRCS    = $(POSIX_STARTUP)        \
                src/drivers/virtio/virtio.c \
                src/cores/core_init.c  \
                src/kernel/libstr.c    \
                $(VIRGL_SRCS)          \
                $(COMMON_SRCS)

# ── QEMU VirtIO build (Target 1) ─────────────────────────────────
QEMU_STARTUP = src/cores/core_virtio.c
QEMU_SRCS    = $(QEMU_STARTUP)          \
                src/drivers/virtio/virtio.c \
                src/cores/core_init.c   \
                src/kernel/libstr.c     \
                $(VIRGL_SRCS)           \
                $(COMMON_SRCS)

TINYGL_SRCS = \
    src/desktop/graphics/tinygl/api.c \
    src/desktop/graphics/tinygl/arrays.c \
    src/desktop/graphics/tinygl/clear.c \
    src/desktop/graphics/tinygl/clip.c \
    src/desktop/graphics/tinygl/get.c \
    src/desktop/graphics/tinygl/image_util.c \
    src/desktop/graphics/tinygl/init.c \
    src/desktop/graphics/tinygl/light.c \
    src/desktop/graphics/tinygl/list.c \
    src/desktop/graphics/tinygl/matrix.c \
    src/desktop/graphics/tinygl/memory.c \
    src/desktop/graphics/tinygl/misc.c \
    src/desktop/graphics/tinygl/msghandling.c \
    src/desktop/graphics/tinygl/select.c \
    src/desktop/graphics/tinygl/specbuf.c \
    src/desktop/graphics/tinygl/texture.c \
    src/desktop/graphics/tinygl/vertex.c \
    src/desktop/graphics/tinygl/vertex_stage.c \
    src/desktop/graphics/tinygl/zbuffer.c \
    src/desktop/graphics/tinygl/zline.c \
    src/desktop/graphics/tinygl/zmath.c \
    src/desktop/graphics/tinygl/ztriangle.c \
    src/desktop/graphics/tinygl/accum.c \
    src/desktop/graphics/tinygl/zpostprocess.c \
    src/desktop/graphics/tinygl/zraster.c \
    src/desktop/graphics/tinygl/ztext.c

GL_SRCS = \
    src/desktop/graphics/gles/gl_dispatch.c \
    src/desktop/graphics/gles/gl_backend_choice.c \
    src/desktop/graphics/gles/egl_surface.c \
    src/desktop/graphics/gles/backend_tinygl.c \
    src/desktop/graphics/virgl/backend_virgl.c \
    src/media/sdk/glgears.c \
    src/media/xmb/xmb.c \
    src/media/xmb/xmb_wave.c \
    $(QUAKE_SRCS) \
    $(DEMO_SRCS)

# ── X86_64 / EMT64 UEFI build (Target 4) ────────────────────────
UEFI_STARTUP = src/cores/core_boot.c src/cores/core_smp.c
UEFI_SRCS    = $(UEFI_STARTUP)          \
               src/cores/core_init.c   \
               src/kernel/libstr.c      \
               src/kernel/libint64.c    \
               src/drivers/vesa/vesa.c  \
               src/drivers/uefi/ps2_mouse.c \
               src/drivers/virtio/virtio_gpu.c \
               $(TINYGL_SRCS)           \
               $(GL_SRCS)               \
               $(COMMON_NO_SDL_SRCS)

# ── NEC PC-98 build (Target 5) ──────────────────────────────────
PC98_STARTUP = src/cores/core_pc98.c src/drivers/pc98/boot_pc98.c
PC98_SRCS    = $(PC98_STARTUP)          \
               src/cores/core_boot.c   \
               src/cores/core_init.c   \
               src/kernel/libstr.c      \
               src/kernel/libint64.c    \
               src/drivers/vesa/vesa.c  \
               src/drivers/pc98/pc98_mouse.c \
               src/drivers/pc98/pc98_kbd.c \
               src/drivers/uefi/ps2_mouse.c \
               $(COMMON_NO_SDL_SRCS) \
               $(LILCU64_ENGINE_SRCS)

# ── BCM283x (Pi 2B) bare-metal arch sources ───────────────────────
ARCH_BCM_SRCS = src/drivers/broadcom/cache.c      \
                src/drivers/broadcom/chkplv.c     \
                src/drivers/broadcom/cntwus.c     \
                src/drivers/broadcom/cpu_calls.c  \
                src/drivers/broadcom/cpu_init.c   \
                src/drivers/broadcom/devinit.c    \
                src/drivers/broadcom/power.c      \
                src/drivers/broadcom/tkdev_init.c \
                src/drivers/broadcom/dwc2.c \
                src/drivers/broadcom/usb_audio_uac1.c

ARCH_BCM64_SRCS = $(ARCH_BCM_SRCS) \
                  src/drivers/broadcom/pcie_bcm2711.c \
                  src/drivers/broadcom/xhci.c \
                  src/drivers/broadcom/bcm2711_dma.c \
                  src/drivers/broadcom/bcm2711_hdmi_audio.c \
                  src/drivers/broadcom/v3d_init.c \
                  src/drivers/broadcom/v3d_cl.c \
                  src/drivers/broadcom/v3d_qpu.c

# BTRON Clean-Room Bluetooth Audio Stack (A2DP v1.3.2) - Opt-Out (Disabled by default)
BTRON_ENABLE_BLUETOOTH ?= 0

ifeq ($(BTRON_ENABLE_BLUETOOTH),1)
    COMMON_CFLAGS   += -DBTRON_HAVE_BLUETOOTH=1
    ARCH_BCM64_SRCS += src/drivers/bluetooth/b_bt_core.c \
                      src/drivers/bluetooth/b_bt_hci.c  \
                      src/drivers/bluetooth/b_bt_l2cap.c \
                      src/drivers/bluetooth/b_bt_avdtp.c \
                      src/drivers/bluetooth/b_bt_sbc.c   \
                      src/drivers/bluetooth/b_bt_audio.c
endif

TKERNEL_SAKAMURA_SRCS = \
    src/kernel/task.c         \
    src/kernel/task_manage.c  \
    src/kernel/task_sync.c    \
    src/kernel/semaphore.c    \
    src/kernel/eventflag.c    \
    src/kernel/mailbox.c      \
    src/kernel/messagebuf.c   \
    src/kernel/rendezvous.c   \
    src/kernel/mutex.c        \
    src/kernel/mempool.c      \
    src/kernel/mempfix.c      \
    src/kernel/subsystem.c    \
    src/kernel/time_calls.c   \
    src/kernel/timer.c        \
    src/kernel/klock.c        \
    src/kernel/wait.c         \
    src/kernel/objname.c      \
    src/kernel/misc_calls.c   \
    src/kernel/version.c      \
    src/kernel/libstr.c

TKERNEL_SRCS = src/cores/core_tkernel.c \
               src/drivers/virtio/virtio.c \
               src/cores/core_init.c     \
               $(TKERNEL_SAKAMURA_SRCS)   \
               $(VIRGL_SRCS)              \
               $(COMMON_SRCS)

# ── µBTRON-FOMA Mobile Target (Target 10) ────────────────────────
FOMA_STARTUP = src/cores/core_foma.c
FOMA_SRCS = $(FOMA_STARTUP)             \
            src/drivers/virtio/virtio.c \
            src/cores/core_init.c       \
            $(TKERNEL_SAKAMURA_SRCS)     \
            src/desktop/graphics/dp_core.c      \
            src/desktop/graphics/icons_bundle.c \
            src/desktop/graphics/dp_sdl.c       \
            src/desktop/font/troncode.c         \
            src/desktop/font/jis_fonts.c        \
            src/desktop/font/tibetan_fonts.c    \
            src/desktop/window/wnd.c            \
            src/desktop/window/app_menu.c       \
            src/desktop/window/event.c          \
            src/apps/chokanji/pmc.c          \
            $(IME_SRCS)                 \
            src/apps/terminal/gterm.c            \
            src/apps/editor/t_editor.c         \
            src/desktop/settings/terminal.c     \
            src/apps/workbench/desktop_mobile.c \
            src/apps/workbench/workbench_mobile.c \
            src/apps/workbench/main_mobile.c   \
            src/fs/blk_mem.c            \
            src/fs/blk_file.c           \
            src/fs/vol.c                \
            src/fs/file.c               \
            $(CLU_APPS_SRCS)            \
            src/apps/clu/clu.c

# Bare-metal: SDL-free subset only
COMMON_NO_SDL_SRCS = \
    src/desktop/graphics/dp_core.c \
    src/desktop/graphics/dp_accel.c \
    src/desktop/graphics/icons_bundle.c \
    src/desktop/font/troncode.c    \
    src/desktop/font/jis_fonts.c   \
    src/desktop/font/tibetan_fonts.c \
    src/desktop/window/wnd.c       \
    src/desktop/window/app_menu.c  \
    src/desktop/window/event.c     \
    src/desktop/window/dnd.c       \
    src/apps/chokanji/pmc.c     \
    src/apps/cabinet/vobj.c     \
    src/apps/workbench/desktop.c  \
    src/apps/workbench/workbench.c \
    src/apps/workbench/tracker.c  \
    src/apps/workbench/about.c    \
    src/apps/workbench/global_menu.c \
    src/apps/cabinet/vobj_manager.c \
    src/apps/browser/tad_browser.c \
    src/media/paint/paint.c \
    src/apps/terminal/gterm.c       \
    src/apps/editor/t_editor.c    \
    src/media/cassette/audio_player.c \
    src/media/orchestra/orchestra.c   \
    src/media/orchestra/orchestra_stage.c \
    src/media/orchestra/orchestra_arrange.c \
    src/media/orchestra/orchestra_piano.c \
    src/media/orchestra/orchestra_score.c \
    src/media/orchestra/orchestra_mix.c \
    src/media/orchestra/orchestra_insert.c \
    src/media/orchestra/orchestra_about.c \
    src/media/orchestra/orchestra_session.c \
    src/media/orchestra/orchestra_smf.c \
    src/media/sdk/gst_rt.c     \
    src/media/orchestra/lukhash_score.c \
    src/apps/chat/chat.c        \
    src/apps/chat/chat_xml.c    \
    src/desktop/settings/control_panel.c \
    src/desktop/settings/language.c \
    src/desktop/settings/appearance.c \
    src/desktop/settings/desktop.c \
    src/desktop/settings/display.c \
    src/desktop/settings/input.c \
    src/desktop/settings/sound.c \
    src/desktop/settings/network.c \
    src/desktop/settings/media.c \
    src/desktop/settings/security.c \
    src/desktop/settings/system.c  \
    src/desktop/settings/terminal.c \
    $(IME_SRCS)            \
    src/fs/blk_mem.c       \
    src/fs/vol.c           \
    src/fs/file.c          \
    src/fs/fs_stdio.c      \
    src/apps/clu/vfs.c          \
    $(CLU_APPS_SRCS)       \
    src/apps/clu/clu.c

BAREMETAL_STARTUP  = src/drivers/broadcom/startup_arm.c
BAREMETAL_LD       = src/drivers/broadcom/link.ld
# src/kernel/libaeabi.c is named here and not folded into COMMON_NO_SDL_SRCS: it
# answers the conversion libcalls only a 32-bit register file asks for, so the
# AArch64 lane -- which converts a 64-bit integer in one instruction -- must not
# link it, and the house rule is to name a runtime only in the lanes measured
# against it.  `make test-arm-aeabi` grades it against the host FPU.
# Full B-System applications suite for Pi 400 & raspi4b bare-metal (Cortex-A72 / AArch64)
ARM64_APPS_SRCS = \
    src/apps/chokanji/cab.c         \
    src/languages/microscript/microscript.c \
    src/apps/chokanji/clock.c       \
    src/apps/chokanji/kconv.c       \
    src/apps/chokanji/xfconv.c      \
    src/apps/chokanji/bpk_core.c    \
    src/apps/chokanji/unpack.c      \
    src/apps/chokanji/launchers.c   \
    src/apps/clarity/clarity.c         \
    src/apps/clarity/clarity_layout.c  \
    src/apps/clarity/clarity_render.c  \
    src/apps/clarity/clarity_export.c  \
    src/apps/browser/pdf_reader.c      \
    src/apps/review/review.c          \
    src/apps/volumes/b_drivesetup.c    \
    src/fs/blk_file.c          \
    src/fs/blk_qcow2.c         \
    src/fs/blk_part.c          \
    src/desktop/graphics/image_decode.c \
    src/media/sdk/mars.c       \
    src/desktop/industrial/hmi/hmi_core.c         \
    src/desktop/industrial/hmi/hmi_switch.c       \
    src/desktop/industrial/hmi/hmi_selector.c     \
    src/desktop/industrial/hmi/hmi_volume.c       \
    src/desktop/industrial/hmi/hmi_meter.c        \
    src/desktop/industrial/hmi/hmi_controller.c   \
    src/desktop/industrial/hmi/hmi_panel.c        \
    src/emulators/msx/msx_app.c \
    $(MSX_SRCS)                \
    src/apps/clu/lang.c             \
    src/apps/clu/term.c             \
    src/apps/clu/tty.c              \
    src/apps/clu/sc/files.c         \
    src/apps/clu/sc/input.c         \
    src/apps/clu/sc/menus.c         \
    src/apps/clu/sc/sc.c            \
    src/apps/clu/tv/tv.c            \
    src/apps/clu/be/native.c        \
    src/drivers/playstation2/ps2_vu0_kernel.c \
    $(TINYGL_SRCS)             \
    src/desktop/graphics/gles/gl_dispatch.c       \
    src/desktop/graphics/gles/gl_backend_choice.c \
    src/desktop/graphics/gles/egl_surface.c       \
    src/desktop/graphics/gles/backend_tinygl.c    \
    src/desktop/graphics/v3d/backend_v3d.c       \
    src/desktop/graphics/virgl/backend_virgl.c     \
    src/media/sdk/glgears.c         \
    src/media/xmb/xmb.c             \
    src/media/xmb/xmb_wave.c        \
    src/media/sdk/lilcu64_demo.c \
    $(QUAKE_SRCS)

ARM32_BAREMETAL_SRCS = src/cores/core_init.c src/cores/core_yoko.c $(TKERNEL_SAKAMURA_SRCS) $(ARCH_BCM_SRCS) $(BAREMETAL_STARTUP) $(COMMON_NO_SDL_SRCS) $(LILCU64_ENGINE_SRCS) src/kernel/libaeabi.c
ARM64_BAREMETAL_SRCS = src/cores/core_init.c src/cores/core_arm64.c $(TKERNEL_SAKAMURA_SRCS) $(ARCH_BCM64_SRCS) $(BAREMETAL_STARTUP) $(COMMON_NO_SDL_SRCS) $(LILCU64_ENGINE_SRCS) $(ARM64_APPS_SRCS)

# ── Object lists ─────────────────────────────────────────────────
POSIX_OBJS   = $(POSIX_SRCS:.c=.posix.o)
QEMU_OBJS    = $(QEMU_SRCS:.c=.qemu.o)
TKERNEL_OBJS = $(TKERNEL_SRCS:.c=.tkernel.o)
ARM32_OBJS   = $(ARM32_BAREMETAL_SRCS:.c=.arm32.o)
ARM64_OBJS   = $(ARM64_BAREMETAL_SRCS:.c=.arm64.o)
SAKAMURA_OBJS  = $(TKERNEL_SRCS:.c=.sakamura.o)
FOMA_OBJS      = $(FOMA_SRCS:.c=.foma.o)
UEFI_OBJS      = $(UEFI_SRCS:.c=.uefi.o)
PC98_OBJS      = $(PC98_SRCS:.c=.pc98.o)

# Object files are host-toolchain specific. In particular, a QEMU object
# produced by MinGW is a COFF file and cannot be linked by a Linux compiler.
# Keep a host/compiler stamp as a prerequisite so moving one checkout between
# hosts triggers a one-time rebuild without penalizing normal incremental
# builds.
POSIX_BUILD_TAG   := $(shell uname -s 2>/dev/null || echo unknown)-$(shell $(CC) -dumpmachine 2>/dev/null || echo unknown)
POSIX_BUILD_STAMP := .build/posix-$(POSIX_BUILD_TAG).stamp
$(POSIX_OBJS): $(POSIX_BUILD_STAMP) Makefile

$(POSIX_BUILD_STAMP):
	@mkdir -p $(dir $@)
	@touch $@

QEMU_BUILD_TAG   := $(shell uname -s 2>/dev/null || echo unknown)-$(shell $(CC) -dumpmachine 2>/dev/null || echo unknown)
QEMU_BUILD_STAMP := .build/qemu-$(QEMU_BUILD_TAG).stamp
$(QEMU_OBJS): $(QEMU_BUILD_STAMP) Makefile

$(QEMU_BUILD_STAMP):
	@mkdir -p $(dir $@)
	@touch $@

# ── Output names ──────────────────────────────────────────────────
POSIX_TARGET   = btron-posix$(EXEEXT)
QEMU_TARGET    = btron-qemu.elf$(EXEEXT)
TKERNEL_TARGET = ./.build/btron-tkernel.elf
SAKAMURA_TARGET = ./.build/btron-sakamura.elf
FOMA_TARGET     = ./.build/btron-foma.elf
TEST_FOMA_BIN   = ./.build/test_foma_ui
UEFI_TARGET     = ./.build/btron-uchida.elf
PC98_TARGET     = ./.build/btron-morris.elf
ARM32_TARGET   = btron-arm-baremetal.elf
ARM64_TARGET   = btron-aarch64-baremetal.elf
DEFAULT_TARGET = ./.build/btron

TKERNEL_INC = -D_RPI_BCM283x_ -DTYPE_RPI=2 \
              -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
              -Iinclude -Isrc/drivers/broadcom -Isrc/kernel -Isrc/cores

all: posix qemu kernel sakamura foma uefi pc98

# ── Tree structure invariants (see scripts/test_structure.sh) ──────────────
check-structure:
	@sh scripts/test_structure.sh

# Emit one source path per line for a *_SRCS variable, for the check above.
list-%:
	@for f in $($*); do echo $$f; done

# ═══════════════════════════════════════════════════════════════════
# POSIX Desktop
# ═══════════════════════════════════════════════════════════════════
posix: $(POSIX_TARGET) btron_sys.vol btron_anders.vol
	@ln -sf $(POSIX_TARGET) $(DEFAULT_TARGET)
	@echo "=========================================================="
	@echo " B-System POSIX Kernel & Desktop successfully built!"
	@echo " Startup File: $(POSIX_STARTUP)"
	@echo " Run './btron' or 'make run-posix' to start."
	@echo "=========================================================="
	@if [ -n "$(PYTHON)" ] || command -v elixir >/dev/null 2>&1; then $(MAKE) tad_bin; \
	  else echo "Note: tad_bin skipped (no python3 or elixir found) - desktop still runs."; fi

%.posix.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=0 -MMD -MP -c $< -o $@

$(POSIX_TARGET): $(POSIX_OBJS)
	$(CC) $(POSIX_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

run-posix: $(POSIX_TARGET) btron_sys.vol btron_anders.vol
	./$(POSIX_TARGET)

run-sakamura: $(SAKAMURA_TARGET) btron_sys.vol btron_anders.vol
	./$(SAKAMURA_TARGET)

# ══════════════════════════════════════════════════════════════════════
# FS Library (host/POSIX build) — blk_mem, blk_file, vol, file, clu
# ══════════════════════════════════════════════════════════════════════
FS_SRCS  = src/fs/blk_mem.c src/fs/blk_file.c src/fs/blk_qcow2.c src/fs/blk_part.c src/fs/vol.c src/fs/file.c
FS_OBJS  = $(FS_SRCS:.c=.host.o) src/kernel/libstr.host.o

# Host tools need tkl_strscpy / tkl_strpad. The full libstr heap is the image
# allocator; this object only has to export the string functions.
src/kernel/libstr.host.o: src/kernel/libstr.c include/libstr.h
	$(CC) $(CFLAGS) -DBTRON_TARGET=0 -DBTRON_LIBSTR_NO_HEAP -MMD -MP -c $< -o $@

src/kernel/libstr.test.o: src/kernel/libstr.c include/libstr.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_LIBSTR_NO_HEAP -MMD -MP -c $< -o $@

src/kernel/libstr.tgl.o: src/kernel/libstr.c include/libstr.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_LIBSTR_NO_HEAP -DBTRON_GL_BACKEND_TINYGL \
	      -DXMB_OUT_DIR='"./.build/tgl/"' -MMD -MP -c $< -o $@

# -MMD is not optional here.  Host objects are linked straight from .o files, so
# without a header dependency an edit to e.g. src/apps/clu/vfs.h or
# src/apps/clu/sc/sokhatsky.h rebuilds only the .c files a target happens to name, and
# the link mixes objects that disagree on struct layout -- sc then reads a File's
# is_dir from where size used to be and draws an empty pane that looks like an
# empty directory.
%.host.o: %.c
	$(CC) $(CFLAGS) -DBTRON_TARGET=0 -MMD -MP -c $< -o $@

src/apps/clu/clu.host.o: src/apps/clu/clu.c
	$(CC) $(CFLAGS) -DBTRON_TARGET=0 -MMD -MP -c $< -o $@

# Header dependencies recorded by the -MMD flags above: editing src/apps/clu/vfs.h or
# src/apps/clu/sc/sokhatsky.h must rebuild every object that included it, or a link
# mixes objects that disagree on struct layout.
-include $(shell find src verify -name '*.host.d' -o -name '*.posix.d' -o -name '*.test.d' -o -name '*.tgl.d' 2>/dev/null)

# ── mkbtronfs — host image builder ────────────────────────────────────
mkbtronfs: tools/mkbtronfs.c $(FS_OBJS)
	$(CC) $(CFLAGS) -Isrc $^ -o mkbtronfs
	@echo "[FS] mkbtronfs built."

# Host album encoder. Not linked into btron. Writes SYS/*.orch and SYS/*.mid.
album_encode: tools/album_encode.c src/media/sdk/lilcu64_score.c \
		src/media/sdk/lilcu64_wa.c src/media/sdk/lilcu64_insert.c \
		src/media/orchestra/orchestra_session.c src/media/orchestra/orchestra_smf.c \
		src/media/sdk/gst_rt.c src/media/orchestra/lukhash_score.c
	$(CC) $(CFLAGS) -Werror -Isrc/media/sdk $^ -lm -o album_encode
	@echo "[ALBUM] album_encode built."

# The manifest's payloads are prerequisites and not merely its input: with only the manifest and
# the tool listed, make rebuilds btron_sys.vol when either changes and *never* when a document the
# volume copies changes, so the image an ELF carries is the volume some other target happened to
# build last -- the stale-artifact disease this Makefile already cures twice for PS2_GL and PS2_FP,
# arrived at from the other side, where the content moved and the flags did not.
#
# mkbtronfs names those prerequisites itself, through --deps, in the same idiom the compiler's
# -MMD lines above use.  It used to be an awk pass over the manifest here, which was a second
# grammar for one file and could not read the first one: it took the last field of a line as the
# payload (so every entry that names a parent drawer, like a cartridge in /SYS/msx, dropped out)
# and it stopped at the first space (so `Dynamite Go! Go! - Demo (2021) (Rutubo Gameworks).ROM` was never a file make
# knew about).  One reader of a manifest is the tool that builds from it.
btron_sys.vol: mkbtronfs tools/manifest.txt
	./mkbtronfs tools/manifest.txt -o btron_sys.vol --deps btron_sys.vol.d
	@echo "[FS] btron_sys.vol written."
	@xxd btron_sys.vol | head -2

btron_anders.vol: mkbtronfs tools/manifest_anders.txt
	./mkbtronfs tools/manifest_anders.txt -o btron_anders.vol -l ANDERS --deps btron_anders.vol.d
	@echo "[FS] btron_anders.vol written."

btron_msx.vol: mkbtronfs tools/manifest_msx.txt
	./mkbtronfs tools/manifest_msx.txt -o btron_msx.vol -l MSX -b 32768 --deps btron_msx.vol.d
	@echo "[FS] btron_msx.vol written."

# Written by the recipe above, so the first build after a clean has no payload dependencies and
# every one after it does -- exactly the trade `-include *.d` already makes for C headers.
-include btron_sys.vol.d
-include btron_anders.vol.d
-include btron_msx.vol.d


# ── FS unit tests ──────────────────────────────────────────────────────
TEST_FS_BIN = ./.build/test_fs
$(TEST_FS_BIN): verify/tests/test_fs.c $(FS_OBJS) src/apps/clu/clu.host.o $(CLU_APPS_HOST_OBJS)
	$(CC) $(CFLAGS) -Isrc $^ -o $@

test-fs: $(TEST_FS_BIN) btron_sys.vol
	./$(TEST_FS_BIN)
	@echo "[FS] All FS tests passed."

# ── SPS (Secure PostScript & Forth VM) unit tests ──────────────────────
TEST_SPS_BIN = ./.build/test_sps
$(TEST_SPS_BIN): verify/tests/test_sps.c src/apps/clu/ps_vm.posix.o src/apps/clu/sps.posix.o src/desktop/font/schrift.posix.o src/apps/browser/pdf_reader.posix.o src/apps/review/review.posix.o src/desktop/graphics/dp_core.posix.o
	@mkdir -p .build
	$(CC) $(CFLAGS) $^ -o $@ -lm

test-sps: $(TEST_SPS_BIN)
	./$(TEST_SPS_BIN)

# ── ANS Forth-2012 Core Subset, FCode & 16-Bit Profile Targets ────────
FORTH_BIN = ./.build/forth
$(FORTH_BIN): src/languages/forth/forth.c src/languages/forth/forth_core.c src/languages/forth/forth_devtree.c
	@mkdir -p ./.build
	$(CC) $(CFLAGS) -DFORTH_STANDALONE=1 $^ -o $@

forth: $(FORTH_BIN)

FORTH16_BIN = ./.build/btron-forth16
$(FORTH16_BIN): src/languages/forth/forth16.c src/languages/forth/forth_core.c src/languages/forth/forth_devtree.c
	@mkdir -p ./.build
	$(CC) $(CFLAGS) -DFORTH_16BIT=1 $^ -o $@

forth-16: $(FORTH16_BIN)

TEST_CLU_FORTH_BIN = ./.build/test_clu_forth
$(TEST_CLU_FORTH_BIN): verify/tests/test_clu_forth.c src/languages/forth/forth_core.c src/languages/forth/forth_devtree.c
	@mkdir -p ./.build
	$(CC) $(CFLAGS) $^ -o $@

test-clu-forth: $(TEST_CLU_FORTH_BIN)
	./$(TEST_CLU_FORTH_BIN)

TEST_FORTH16_BIN = ./.build/test_forth16
$(TEST_FORTH16_BIN): verify/tests/test_forth16.c src/languages/forth/forth_core.c src/languages/forth/forth_devtree.c
	@mkdir -p ./.build
	$(CC) $(CFLAGS) -DFORTH_16BIT=1 $^ -o $@

test-forth16: $(TEST_FORTH16_BIN)
	./$(TEST_FORTH16_BIN)

TEST_FORTH_PERF_BIN = ./.build/test_forth_perf
$(TEST_FORTH_PERF_BIN): verify/tests/test_forth_perf.c src/languages/forth/forth_core.c src/languages/forth/forth_devtree.c
	@mkdir -p ./.build
	$(CC) $(CFLAGS) $^ -o $@

test-forth-perf: $(TEST_FORTH_PERF_BIN)
	./$(TEST_FORTH_PERF_BIN)

TEST_TFW_FORTH_SRCS = verify/tests/test_tfw_forth.c $(MSX_SRCS)
TEST_TFW_FORTH_OBJS = $(TEST_TFW_FORTH_SRCS:.c=.test.o)
TEST_TFW_FORTH_BIN  = ./.build/test_tfw_forth

$(TEST_TFW_FORTH_BIN): $(TEST_TFW_FORTH_OBJS) $(TFW_ROMS)
	@mkdir -p ./.build
	$(CC) $(TEST_TFW_FORTH_OBJS) -o $@ $(LDFLAGS)

test-tfw-forth: $(TEST_TFW_FORTH_BIN)
	./$(TEST_TFW_FORTH_BIN)

TEST_MARS_CAPTURE_BIN = ./.build/test_mars_capture
$(TEST_MARS_CAPTURE_BIN): verify/tests/test_mars_capture.c src/languages/forth/forth_core.c src/languages/forth/forth_devtree.c
	@mkdir -p ./.build
	$(CC) $(CFLAGS) $^ -o $@ -lz

test-mars-capture: $(TEST_MARS_CAPTURE_BIN)
	./$(TEST_MARS_CAPTURE_BIN)

test-forth: test-clu-forth test-forth16 test-forth-perf test-tfw-forth test-mars-capture

# ── Cho-Kanji (B-right/V 4.02) QCOW2 tests ───────────────────────────
# The read-write tests create, append to and delete records, so they must never
# open the golden disk. Each run clones a fresh scratch image and the suite points
# every test at it through BTRON_CHOKANJI_IMAGE. The clone is made in the recipe,
# not as a file target, because a file target keeps an old scratch disk once it
# exists -- and a disk a previous run already wrote to is exactly what hides a
# read/write regression.
CHOKANJI_GOLDEN   ?= $(if $(wildcard hda.golden),hda.golden,hda.qcow2)
CHOKANJI_RW_IMAGE  = ./.build/hda.rw.qcow2

TEST_CHOKANJI_BIN = ./.build/test_chokanji
$(TEST_CHOKANJI_BIN): verify/tests/test_chokanji.c $(FS_OBJS) src/apps/clu/clu.host.o $(CLU_APPS_HOST_OBJS)
	$(CC) $(CFLAGS) -Isrc $^ -o $@

test-chokanji: $(TEST_CHOKANJI_BIN)
	@mkdir -p $(dir $(CHOKANJI_RW_IMAGE))
	@rm -f $(CHOKANJI_RW_IMAGE)
	@cp -c $(CHOKANJI_GOLDEN) $(CHOKANJI_RW_IMAGE) 2>/dev/null || cp $(CHOKANJI_GOLDEN) $(CHOKANJI_RW_IMAGE)
	@echo "[CHOKANJI] scratch image $(CHOKANJI_RW_IMAGE) cloned from $(CHOKANJI_GOLDEN)"
	BTRON_CHOKANJI_IMAGE=$(CHOKANJI_RW_IMAGE) ./$(TEST_CHOKANJI_BIN)
	@echo "[CHOKANJI] All Cho-Kanji tests passed."

# ── Cho-Kanji Tier 1 & PMC NASA-Standard Apps verification ────────────
TEST_CHOKANJI_APPS_BIN = ./.build/test_chokanji_apps
$(TEST_CHOKANJI_APPS_BIN): verify/tests/test_chokanji_apps.c src/apps/chokanji/pmc.posix.o src/apps/chokanji/cab.posix.o src/apps/chokanji/clock.posix.o src/languages/microscript/microscript.posix.o src/apps/chokanji/kconv.posix.o src/apps/chokanji/xfconv.posix.o src/apps/chokanji/bpk_core.posix.o src/apps/chokanji/unpack.posix.o src/apps/browser/pdf_reader.posix.o src/apps/chokanji/cab.posix.o src/languages/microscript/microscript.posix.o src/apps/chokanji/clock.posix.o src/apps/chokanji/kconv.posix.o src/apps/chokanji/xfconv.posix.o src/apps/chokanji/bpk_core.posix.o src/apps/chokanji/unpack.posix.o src/apps/chokanji/bpk_core.posix.o src/apps/chokanji/unpack.posix.o src/desktop/window/wnd.posix.o src/desktop/graphics/dp_core.posix.o src/desktop/graphics/dp_sdl.posix.o src/desktop/font/troncode.posix.o src/desktop/font/jis_fonts.posix.o src/desktop/font/tibetan_fonts.posix.o src/desktop/font/font_mgr.posix.o src/desktop/window/event.posix.o src/desktop/window/app_menu.posix.o src/apps/cabinet/vobj.posix.o src/fs/vol.posix.o src/fs/file.posix.o src/fs/blk_mem.posix.o src/fs/blk_file.posix.o src/fs/blk_qcow2.posix.o src/fs/blk_part.posix.o
	$(CC) $(CFLAGS) -Isrc -Iinclude $^ -o $@ $(LDFLAGS) $(SDL_LIBS)

test-chokanji-apps: $(TEST_CHOKANJI_APPS_BIN)
	./$(TEST_CHOKANJI_APPS_BIN)
	@echo "[CHOKANJI APPS] All Tier 1 applications & PMC tests passed."

# ── CLU / Termios / TV / SC ───────────────────────────────────────────
CLU_CORE_SRCS = src/apps/clu/lang.c src/apps/clu/term.c src/apps/clu/tty.c src/apps/clu/vfs.c
CLU_CORE_OBJS = $(CLU_CORE_SRCS:.c=.host.o)

SC_SRCS = src/apps/clu/sc/files.c src/apps/clu/sc/input.c src/apps/clu/sc/menus.c
SC_OBJS = $(SC_SRCS:.c=.host.o)

# sc.c is a library everywhere; only the standalone bin/sc binary links its main().
src/apps/clu/sc/sc.standalone.o: src/apps/clu/sc/sc.c
	$(CC) $(CFLAGS) -DBTRON_TARGET=0 -DSC_STANDALONE -MMD -MP -c $< -o $@

src/apps/clu/tv/tv.host.o: src/apps/clu/tv/tv.c
	$(CC) $(CFLAGS) -DBTRON_TARGET=0 -MMD -MP -c $< -o $@

bin/tv: src/apps/clu/tv/tv.c $(CLU_CORE_OBJS) $(FS_OBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -DTV_STANDALONE -Isrc -Iinclude $^ -o $@

bin/sc: $(SC_OBJS) src/apps/clu/sc/sc.standalone.o src/apps/clu/tv/tv.host.o $(CLU_CORE_OBJS) $(CLU_APPS_HOST_OBJS) src/apps/clu/clu.host.o $(FS_OBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -Isrc -Iinclude $^ -o $@

TEST_TERMIOS_BIN = ./.build/test_termios
$(TEST_TERMIOS_BIN): verify/tests/test_termios_clu.c $(CLU_CORE_OBJS) src/apps/clu/sc/files.host.o src/apps/clu/sc/input.host.o src/apps/clu/sc/menus.host.o src/apps/clu/sc/sc.host.o src/apps/clu/tv/tv.host.o $(CLU_APPS_HOST_OBJS) src/apps/clu/clu.host.o $(FS_OBJS)
	@mkdir -p .build
	$(CC) $(CFLAGS) -Isrc -Iinclude $^ -o $@


test-termios: $(TEST_TERMIOS_BIN) btron_sys.vol btron_anders.vol
	./$(TEST_TERMIOS_BIN)
	@echo "[CLU] All termios, TV, and SC tests passed."



# ── Clarity DTP Frame & Control tests ────────────────────────────────
TEST_CLARITY_BIN = ./.build/test_clarity_frames
$(TEST_CLARITY_BIN): verify/tests/test_clarity_frames.c src/desktop/window/pmc.posix.o src/media/orchestra/lukhash_score.posix.o src/apps/review/review.posix.o src/apps/chokanji/launchers.posix.o src/apps/chokanji/pmc.posix.o src/apps/chokanji/cab.posix.o src/apps/chokanji/clock.posix.o src/languages/microscript/microscript.posix.o src/apps/chokanji/kconv.posix.o src/apps/chokanji/xfconv.posix.o src/apps/chokanji/bpk_core.posix.o src/apps/chokanji/unpack.posix.o src/apps/browser/pdf_reader.posix.o src/desktop/graphics/image_decode.posix.o src/desktop/window/dnd.posix.o src/apps/clarity/clarity.posix.o src/apps/clarity/clarity_layout.posix.o src/apps/clarity/clarity_render.posix.o src/apps/clarity/clarity_export.posix.o src/desktop/window/wnd.posix.o src/desktop/window/app_menu.posix.o src/desktop/window/event.posix.o src/desktop/graphics/dp_core.posix.o src/desktop/graphics/icons_bundle.posix.o src/desktop/font/troncode.posix.o src/desktop/font/jis_fonts.posix.o src/desktop/font/tibetan_fonts.posix.o src/desktop/font/font_mgr.posix.o src/cores/core_posix.posix.o src/cores/core_init.posix.o src/drivers/virtio/virtio.posix.o src/apps/cabinet/vobj.posix.o src/apps/workbench/desktop.posix.o src/apps/workbench/workbench.posix.o src/apps/workbench/tracker.posix.o src/apps/workbench/about.posix.o src/apps/workbench/global_menu.posix.o src/apps/cabinet/vobj_manager.posix.o src/apps/browser/tad_browser.posix.o src/apps/terminal/gterm.posix.o src/apps/editor/t_editor.posix.o src/media/cassette/audio_player.posix.o src/apps/volumes/b_drivesetup.posix.o src/media/orchestra/orchestra.posix.o src/media/orchestra/orchestra_stage.posix.o src/media/orchestra/orchestra_arrange.posix.o src/media/orchestra/orchestra_piano.posix.o src/media/orchestra/orchestra_score.posix.o src/media/orchestra/orchestra_mix.posix.o src/media/orchestra/orchestra_insert.posix.o src/media/orchestra/orchestra_about.posix.o src/media/orchestra/orchestra_session.posix.o src/media/orchestra/orchestra_smf.posix.o src/media/sdk/gst_rt.posix.o src/media/sdk/lilcu64_wa.posix.o src/media/sdk/lilcu64_score.posix.o src/media/sdk/lilcu64_synth.posix.o src/media/sdk/lilcu64_insert.posix.o src/apps/chat/chat.posix.o src/apps/chat/chat_xml.posix.o src/desktop/settings/language.posix.o src/desktop/settings/control_panel.posix.o src/desktop/settings/appearance.posix.o src/desktop/settings/desktop.posix.o src/desktop/settings/display.posix.o src/desktop/settings/input.posix.o src/desktop/settings/sound.posix.o src/desktop/settings/network.posix.o src/desktop/settings/media.posix.o src/desktop/settings/security.posix.o src/desktop/settings/system.posix.o src/desktop/settings/terminal.posix.o src/desktop/industrial/hmi/hmi_core.posix.o src/desktop/industrial/hmi/hmi_switch.posix.o src/desktop/industrial/hmi/hmi_selector.posix.o src/desktop/industrial/hmi/hmi_volume.posix.o src/desktop/industrial/hmi/hmi_meter.posix.o src/desktop/industrial/hmi/hmi_controller.posix.o src/desktop/industrial/hmi/hmi_panel.posix.o src/desktop/tip/japanese/mozc_kkc.posix.o src/desktop/tip/tip_ife.posix.o src/desktop/tip/tip_task.posix.o src/desktop/tip/tip_vobj.posix.o src/desktop/tip/tibetan/wylie.posix.o src/desktop/tip/tibetan/tibetan_dict.posix.o src/fs/blk_mem.posix.o src/fs/blk_file.posix.o src/fs/blk_qcow2.posix.o src/fs/blk_part.posix.o src/fs/vol.posix.o src/fs/file.posix.o $(CLU_APPS_POSIX_OBJS) src/apps/clu/clu.posix.o
	$(CC) $(CFLAGS) -Isrc $^ -o $@ $(LDFLAGS) $(SDL_LIBS)

test-clarity: $(TEST_CLARITY_BIN)
	./$(TEST_CLARITY_BIN)
	@echo "[CLARITY] All Clarity tests passed."

# ── Clarity Hypermedia & DND tests ──────────────────────────────────
TEST_CLARITY_HYPERMEDIA_BIN = ./.build/test_clarity_hypermedia
$(TEST_CLARITY_HYPERMEDIA_BIN): verify/tests/test_clarity_hypermedia.c src/desktop/window/pmc.posix.o src/media/orchestra/lukhash_score.posix.o src/apps/review/review.posix.o src/apps/chokanji/launchers.posix.o src/apps/chokanji/pmc.posix.o src/apps/chokanji/cab.posix.o src/apps/chokanji/clock.posix.o src/languages/microscript/microscript.posix.o src/apps/chokanji/kconv.posix.o src/apps/chokanji/xfconv.posix.o src/apps/chokanji/bpk_core.posix.o src/apps/chokanji/unpack.posix.o src/apps/browser/pdf_reader.posix.o src/desktop/graphics/image_decode.posix.o src/desktop/window/dnd.posix.o src/apps/clarity/clarity.posix.o src/apps/clarity/clarity_layout.posix.o src/apps/clarity/clarity_render.posix.o src/apps/clarity/clarity_export.posix.o src/desktop/window/wnd.posix.o src/desktop/window/app_menu.posix.o src/desktop/window/event.posix.o src/desktop/graphics/dp_core.posix.o src/desktop/graphics/icons_bundle.posix.o src/desktop/font/troncode.posix.o src/desktop/font/jis_fonts.posix.o src/desktop/font/tibetan_fonts.posix.o src/desktop/font/font_mgr.posix.o src/cores/core_posix.posix.o src/cores/core_init.posix.o src/drivers/virtio/virtio.posix.o src/apps/cabinet/vobj.posix.o src/apps/workbench/desktop.posix.o src/apps/workbench/workbench.posix.o src/apps/workbench/tracker.posix.o src/apps/workbench/about.posix.o src/apps/workbench/global_menu.posix.o src/apps/cabinet/vobj_manager.posix.o src/apps/browser/tad_browser.posix.o src/apps/terminal/gterm.posix.o src/apps/editor/t_editor.posix.o src/media/cassette/audio_player.posix.o src/apps/volumes/b_drivesetup.posix.o src/media/orchestra/orchestra.posix.o src/media/orchestra/orchestra_stage.posix.o src/media/orchestra/orchestra_arrange.posix.o src/media/orchestra/orchestra_piano.posix.o src/media/orchestra/orchestra_score.posix.o src/media/orchestra/orchestra_mix.posix.o src/media/orchestra/orchestra_insert.posix.o src/media/orchestra/orchestra_about.posix.o src/media/orchestra/orchestra_session.posix.o src/media/orchestra/orchestra_smf.posix.o src/media/sdk/gst_rt.posix.o src/media/sdk/lilcu64_wa.posix.o src/media/sdk/lilcu64_score.posix.o src/media/sdk/lilcu64_synth.posix.o src/media/sdk/lilcu64_insert.posix.o src/apps/chat/chat.posix.o src/apps/chat/chat_xml.posix.o src/desktop/settings/language.posix.o src/desktop/settings/control_panel.posix.o src/desktop/settings/appearance.posix.o src/desktop/settings/desktop.posix.o src/desktop/settings/display.posix.o src/desktop/settings/input.posix.o src/desktop/settings/sound.posix.o src/desktop/settings/network.posix.o src/desktop/settings/media.posix.o src/desktop/settings/security.posix.o src/desktop/settings/system.posix.o src/desktop/settings/terminal.posix.o src/desktop/industrial/hmi/hmi_core.posix.o src/desktop/industrial/hmi/hmi_switch.posix.o src/desktop/industrial/hmi/hmi_selector.posix.o src/desktop/industrial/hmi/hmi_volume.posix.o src/desktop/industrial/hmi/hmi_meter.posix.o src/desktop/industrial/hmi/hmi_controller.posix.o src/desktop/industrial/hmi/hmi_panel.posix.o src/desktop/tip/japanese/mozc_kkc.posix.o src/desktop/tip/tip_ife.posix.o src/desktop/tip/tip_task.posix.o src/desktop/tip/tip_vobj.posix.o src/desktop/tip/tibetan/wylie.posix.o src/desktop/tip/tibetan/tibetan_dict.posix.o src/fs/blk_mem.posix.o src/fs/blk_file.posix.o src/fs/blk_qcow2.posix.o src/fs/blk_part.posix.o src/fs/vol.posix.o src/fs/file.posix.o $(CLU_APPS_POSIX_OBJS) src/apps/clu/clu.posix.o
	$(CC) $(CFLAGS) -Iinclude -Isrc $^ -o $@ $(LDFLAGS) $(SDL_LIBS)

test-clarity-hypermedia: $(TEST_CLARITY_HYPERMEDIA_BIN)
	./$(TEST_CLARITY_HYPERMEDIA_BIN)
	@echo "[HYPERMEDIA] All Clarity Hypermedia tests passed."


# ═══════════════════════════════════════════════════════════════════
# QEMU VirtIO Desktop
# ═══════════════════════════════════════════════════════════════════
qemu: tad_bin $(QEMU_TARGET)
	@echo "=========================================================="
	@echo " B-System QEMU VirtIO Desktop built!"
	@echo " Run 'make run-qemu' to launch."
	@echo "=========================================================="

%.qemu.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=1 -DBTRON_QEMU_TARGET -c $< -o $@

$(QEMU_TARGET): $(QEMU_OBJS)
	$(CC) $(QEMU_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

run-qemu: $(QEMU_TARGET)
	@./$(QEMU_TARGET)

test-qemu: $(QEMU_TARGET)
	@./$(QEMU_TARGET)

# ═══════════════════════════════════════════════════════════════════
# T-Kernel SDL2 host build (development / debug on host)
# ═══════════════════════════════════════════════════════════════════
kernel: tad_bin $(TKERNEL_TARGET)
	@echo "=========================================================="
	@echo " Sakamura T-Kernel 2.0 Engine built: $(TKERNEL_TARGET)"
	@echo "=========================================================="

src/cores/%.tkernel.o: src/cores/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=2 -c $< -o $@

src/kernel/%.tkernel.o: src/kernel/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=2 -c $< -o $@

src/drivers/broadcom/%.tkernel.o: src/drivers/broadcom/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=2 -c $< -o $@

%.tkernel.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=2 -c $< -o $@

$(TKERNEL_TARGET): $(TKERNEL_OBJS)
	$(CC) $(TKERNEL_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

# ===================================================================
sakamura: tad_bin $(SAKAMURA_TARGET)
	@echo "=========================================================="
	@echo " Sakamura T-Kernel 2.0 Engine (UART/VirtIO Mode) built: $(SAKAMURA_TARGET)"
	@echo "=========================================================="

src/cores/%.sakamura.o: src/cores/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=3 -c $< -o $@

src/kernel/%.sakamura.o: src/kernel/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=3 -c $< -o $@

src/drivers/broadcom/%.sakamura.o: src/drivers/broadcom/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=3 -c $< -o $@

%.sakamura.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=3 -c $< -o $@

$(SAKAMURA_TARGET): $(SAKAMURA_OBJS)
	$(CC) $(SAKAMURA_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

# ═══════════════════════════════════════════════════════════════════
# µBTRON-FOMA Mobile Engine (Target 10: AArch32 UMTS Mobile Profile)
# ═══════════════════════════════════════════════════════════════════
foma: tad_bin $(FOMA_TARGET)
	@echo "=========================================================="
	@echo " µBTRON-FOMA Mobile Engine built: $(FOMA_TARGET)"
	@echo " Handset Profile: TI OMAP2430 / ARM1136 AArch32 UMTS"
	@echo " Display Viewport: 480x640 VGA Portrait (Vertical Screen)"
	@echo " Run 'make run-foma' to launch."
	@echo "=========================================================="

src/cores/%.foma.o: src/cores/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=10 -DBTRON_FOMA_TARGET -c $< -o $@

src/kernel/%.foma.o: src/kernel/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=10 -DBTRON_FOMA_TARGET -c $< -o $@

src/drivers/broadcom/%.foma.o: src/drivers/broadcom/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=10 -DBTRON_FOMA_TARGET -c $< -o $@

%.foma.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=10 -DBTRON_FOMA_TARGET -c $< -o $@

$(FOMA_TARGET): $(FOMA_OBJS)
	$(CC) $(FOMA_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

run-foma: $(FOMA_TARGET)
	@echo "=========================================================="
	@echo " Launching µBTRON-FOMA Mobile Workbench"
	@echo " Architecture : AArch32 TI OMAP2430 / ARM1136 Profile"
	@echo " Kernel       : Sakamura T-Kernel 2.0 Engine"
	@echo " Runner       : VirtIO MMIO Block & Framebuffer Runner"
	@echo " Viewport     : 480x640 VGA Portrait Screen"
	@echo " Controls     : 5-way D-pad (Arrows/Enter), Softkeys (F1/F2/F3),"
	@echo "                Numeric (1-9), Back (Esc/Bksp), Menu (M)"
	@echo "=========================================================="
	./$(FOMA_TARGET)

test-foma: $(FOMA_TARGET) $(TEST_FOMA_BIN)
	@echo "=========================================================="
	@echo " Testing µBTRON-FOMA Target Build & Symbols"
	@echo "=========================================================="
	@file $(FOMA_TARGET)
	@./$(TEST_FOMA_BIN)

# ── X86_64 / EMT64 UEFI SMP QEMU Kernel (Honoring Kota Uchida) ───
UEFI_LD     = src/drivers/uefi/uefi_qemu.ld
UEFI_CFLAGS = -O2 -Wall -Wextra -std=c99 -mno-sse -mno-mmx -mno-sse2 \
              -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-constant-conversion \
              -DBTRON_TARGET=4 -DBTRON_UEFI_TARGET -DBTRON_SMP -DBTRON_GL_BACKEND_TINYGL \
              $(COMMON_INCLUDES)

%.uefi.o: %.c
	$(X86_CC) $(UEFI_CFLAGS) -c $< -o $@

uefi: tad_bin $(UEFI_TARGET)
	@ln -sf $(UEFI_TARGET) btron-uefi.elf
	@echo "=========================================================="
	@echo " B-System X86_64 / EMT64 UEFI SMP Kernel built: $(UEFI_TARGET)"
	@echo " In honor of Kota Uchida (内田 公太) — Japanese UEFI OS pioneer"
	@echo " Run 'make run-uefi' or 'make run-eufi' to launch on QEMU."
	@echo "=========================================================="

$(UEFI_TARGET): $(UEFI_OBJS) $(UEFI_LD)
	@echo "=========================================================="
	@echo " Building B-System X86_64 / EMT64 UEFI SMP Kernel: $@"
	@echo "=========================================================="
	$(X86_LD) -T $(UEFI_LD) $(UEFI_OBJS) -o $@
	@echo "[UEFI-ELF] Built: $@"
	@file $@

run-uefi: $(UEFI_TARGET)
	@echo "=========================================================="
	@echo " Launching B-System X86_64 / EMT64 UEFI SMP on QEMU"
	@echo " Honoring : Kota Uchida (内田 公太) — MikanOS Pioneer"
	@echo " Machine  : q35  |  CPU: qemu64 (SMP 4 Cores)  |  RAM: 1G"
	@echo " Firmware : ACPI 6.5 MADT + LAPIC SMP Bring-up (core_smp.c)"
	@echo " Graphics : VirtIO-GPU 2D Display & VESA VBE 1024x768 32-bpp"
	@echo " Desktop  : desktop.c · wnd.c · gterm.c · Mozc IME"
	@echo "=========================================================="
	$(QEMU_X86_64) -M q35,accel=tcg -cpu qemu64 -smp cores=4,threads=1,sockets=1 -m 1G \
	    $(QEMU_DISPLAY) \
	    -device virtio-vga \
	    -kernel $(UEFI_TARGET) -serial stdio

run-eufi: run-uefi

test-uefi: $(UEFI_TARGET)
	@echo "=========================================================="
	@echo " Testing B-System x86_64 UEFI SMP Kernel on QEMU q35 (Headless CI)"
	@echo " Machine : q35  |  CPU: qemu64 (SMP 4)  |  RAM: 1G"
	@echo " Graphics: VirtIO-GPU 2D  |  Mode: headless, serial validation"
	@echo "=========================================================="
	@bash scripts/test_uefi.sh
# ═══════════════════════════════════════════════════════════════════
# NEC PC-98 Kernel Desktop (Honoring Awe Morris — zedBSD Pioneer)
# ═══════════════════════════════════════════════════════════════════
PC98_CFLAGS = -O2 -Wall -Wextra -std=c99 -mno-sse -mno-mmx -mno-sse2 -DBTRON_TARGET=5 -DBTRON_PC98_TARGET $(COMMON_INCLUDES)

%.pc98.o: %.c
	$(X86_CC) $(PC98_CFLAGS) -c $< -o $@

pc98: tad_bin $(PC98_TARGET)
	@ln -sf $(PC98_TARGET) btron-pc98.elf
	@echo "=========================================================="
	@echo " B-System NEC PC-98 Kernel built: $(PC98_TARGET)"
	@echo " In honor of Awe Morris — NEC PC-98 & zedBSD pioneer"
	@echo " Run 'make run-pc98' to launch."
	@echo "=========================================================="

$(PC98_TARGET): $(PC98_OBJS) $(UEFI_LD)
	@echo "=========================================================="
	@echo " Building B-System NEC PC-98 Kernel: $@"
	@echo "=========================================================="
	$(X86_LD) -T $(UEFI_LD) $(PC98_OBJS) -o $@
	@echo "[PC98-ELF] Built: $@"
	@file $@

run-pc98: $(PC98_TARGET)
	@echo "=========================================================="
	@echo " Launching B-System NEC PC-9801 / PC-9821 on QEMU"
	@echo " Honoring : Awe Morris (zedBSD & NEC PC-98 Architecture)"
	@echo " Machine  : NEC PC-9821 VM  |  CPU: 486/Pentium (PC-98 Planar VRAM)"
	@echo " Firmware : Ski Bootloader -> BTRON3 PC-98 Console & Desktop"
	@echo "=========================================================="
	@if command -v qemu-system-pc98 >/dev/null 2>&1; then \
	    qemu-system-pc98 -M pc98 -m 64M -kernel $(PC98_TARGET) -serial stdio; \
	elif [ -f tools/np2kai_bin ]; then \
	    ./tools/np2kai_bin; \
	else \
	    $(QEMU_X86_64) -M q35,accel=tcg -cpu qemu64 -m 1G $(QEMU_DISPLAY) -vga std -kernel $(PC98_TARGET) -serial stdio; \
	fi

test-pc98: $(PC98_TARGET)
	@echo "=========================================================="
	@echo " Testing B-System NEC PC-98 Kernel on QEMU (Headless CI)"
	@echo " Honoring : Awe Morris — zedBSD & NEC PC-98 Pioneer"
	@echo " Machine  : q35 (PC-98 compat)  |  Mode: headless, serial validation"
	@echo "=========================================================="
	@bash scripts/test_pc98.sh

# ═══════════════════════════════════════════════════════════════════
# Motorola 68040 Macintosh Quadra 800 Kernel (q800)
# ═══════════════════════════════════════════════════════════════════
M68K_TARGET     = ./.build/btron-m68k.elf
M68K_LD_SCRIPT  = src/drivers/m68k/m68k_q800.ld
M68K_CFLAGS     = -O2 -Wall -Wextra -std=c99 -mcpu=68040 -ffreestanding -nostdlib -DBTRON_TARGET=7 -DBTRON_M68K_TARGET $(COMMON_INCLUDES)
M68K_STARTUP    = src/cores/core_m68k.c
M68K_SRCS       = $(M68K_STARTUP)          \
                  src/cores/core_init.c   \
                  src/kernel/libstr.c      \
                  $(COMMON_NO_SDL_SRCS)    \
                  $(LILCU64_ENGINE_SRCS)
M68K_OBJS       = src/drivers/m68k/boot_m68k.m68k.o $(M68K_SRCS:.c=.m68k.o)

%.m68k.o: %.s
	$(M68K_CC) -mcpu=68040 -c $< -o $@

%.m68k.o: %.c
	$(M68K_CC) $(M68K_CFLAGS) -c $< -o $@

m68k: $(M68K_TARGET)

$(M68K_TARGET): $(M68K_OBJS) $(M68K_LD_SCRIPT)
	@echo "=========================================================="
	@echo " Building B-System M68K Quadra 800 Kernel: $@"
	@echo "=========================================================="
	$(M68K_CC) -mcpu=68040 -nostdlib -T $(M68K_LD_SCRIPT) $(M68K_OBJS) -o $@
	@echo "[M68K-ELF] Built: $@"
	@file $@

run-m68k: $(M68K_TARGET)
	@echo "=========================================================="
	@echo " Launching B-System M68K Macintosh Quadra 800 on QEMU"
	@echo " Machine  : Apple Macintosh Quadra 800 (-M q800)"
	@echo " CPU      : Motorola 68040 @ 33 MHz (MMU / FPU Active)"
	@echo " RAM      : 128 MB (32-Bit Linear Address Space)"
	@echo " Display  : NuBus Slot 9 DAFB Framebuffer 800x600 @ 8-bpp"
	@echo " Input    : MOS 6522 VIA1 / VIA2 System Controllers & ADB"
	@echo " Serial   : Zilog Z8530 ESCC Dual UART (Port A Active)"
	@echo " Storage  : NCR 53C96 ESP SCSI Host Adapter"
	@echo "=========================================================="
	$(QEMU_M68K) -M q800 -cpu m68040 -m 128M \
	    $(QEMU_DISPLAY) \
	    -kernel $(M68K_TARGET) -serial stdio

test-m68k: $(M68K_TARGET)
	@echo "=========================================================="
	@echo " Testing B-System M68K Kernel on QEMU Quadra 800 (Headless CI)"
	@echo " Honoring : Fumihiko Itagaki — uITRON 3.0 Pioneer"
	@echo " Machine  : q800  |  CPU: m68040  |  RAM: 128M"
	@echo " Mode     : headless, serial validation"
	@echo "=========================================================="
	@bash scripts/test_m68k.sh

# ═══════════════════════════════════════════════════════════════════
# Sony PlayStation 2 Emotion Engine Kernel (ps2 / PCSX2) [Target 8]
# ═══════════════════════════════════════════════════════════════════
AUTO_GUI       ?= 1
# HIDTRACE=1: Trace per-keypress and USB control-transfers on console (0: disabled)
HIDTRACE       ?= 0
# PS2_GL: Graphics rasterization backend (eegs: GS hardware, tinygl/virgl: software)
PS2_GL          ?= eegs
ifeq (,$(filter $(PS2_GL),tinygl virgl eegs))
$(error PS2_GL=$(PS2_GL) is not one of tinygl, virgl, eegs)
endif
PS2_TARGET     = btron-ps2.elf
PS2_ISO        = btron-ps2.iso
PS2_LD_SCRIPT  = src/drivers/playstation2/ps2.ld
PS2_CFLAGS     = -O2 -Wall -Wextra -std=c99 -ffreestanding -nostdlib \
                 -DBTRON_TARGET=8 -DBTRON_PS2_TARGET -DBTRON_AUTO_GUI=$(AUTO_GUI) \
                 -DBTRON_HID_TRACE=$(HIDTRACE) -DBTRON_GL_BACKEND_TINYGL \
                 $(COMMON_INCLUDES)
ifeq ($(PS2_GL),virgl)
PS2_CFLAGS    += -DBTRON_GL_PREFER_VIRGL
endif
ifeq ($(PS2_GL),eegs)
PS2_CFLAGS    += -DBTRON_GL_PREFER_EEGS
endif
# PS2_FP_COUNT=1: Instrument soft-float libcalls with per-stage call counters
PS2_FP_COUNT    ?= 0
PS2_CFLAGS    += -DBTRON_PS2_FP_COUNT=$(PS2_FP_COUNT)
# PS2_IDENT=1: GIF stream per-group hash invariance telemetry ([GSTID])
PS2_IDENT       ?= 0
PS2_CFLAGS    += -DBTRON_PS2_GS_IDENT=$(PS2_IDENT)
# PS2_PW=1: Per-app paint worker scheduler and rate-limiter (0: synchronous)
PS2_PW          ?= 0
ifeq (,$(filter $(PS2_PW),0 1))
$(error PS2_PW=$(PS2_PW) is not 0 or 1)
endif
PS2_CFLAGS    += -DBTRON_PS2_PAINT_WORKER=$(PS2_PW)
PS2_PW_SRCS     = $(if $(filter 1,$(PS2_PW)),src/drivers/playstation2/ps2_paint_worker.c,)
PS2_STARTUP    = src/cores/core_ps2.c
# Termios-hosted console applications (gterm, SC commander, TV vision)
CLU_TERMIO_SRCS = src/apps/clu/lang.c           \
                  src/apps/clu/term.c           \
                  src/apps/clu/tty.c            \
                  src/apps/clu/sc/files.c       \
                  src/apps/clu/sc/input.c       \
                  src/apps/clu/sc/menus.c       \
                  src/apps/clu/sc/sc.c          \
                  src/apps/clu/tv/tv.c          \
                  src/apps/clu/be/native.c
CLU_TERMIO_SRCS += $(BE_DASM_SRCS)
# PS2 Graphics Synthesizer and 3D application runtime sources
PS2_GL_SRCS    = src/desktop/graphics/gles/gl_dispatch.c     \
                 src/desktop/graphics/gles/gl_backend_choice.c \
                 src/desktop/graphics/gles/egl_surface.c     \
                 src/desktop/graphics/gles/backend_tinygl.c  \
                 src/desktop/graphics/virgl/backend_virgl.c   \
                 src/desktop/graphics/eegs/backend_eegs.c    \
                 src/desktop/graphics/eegs/eegs_geom.c       \
                 src/desktop/graphics/eegs/eegs_present.c    \
                 src/desktop/graphics/eegs/gscube_nexus.c    \
                 src/drivers/playstation2/ps2_vu1_geom.c \
                 src/media/xmb/xmb.c           \
                 src/media/xmb/xmb_wave.c      \
                 src/media/sdk/glgears.c       \
                 $(DEMO_SRCS)             \
                 $(PS2_QUAKE_SRCS)
# PS2_MSX=1: Link B-MSX (7.5 MB heap).  Default on so gterm `msx` is a real
# window, not the weak stub.  PS2_MSX=0 parks the core and restores 11 MB heap.
PS2_MSX         ?= 1
PS2_MSX_SRCS    = $(if $(filter 1,$(PS2_MSX)),$(MSX_SRCS) src/emulators/msx/msx_app.c,)
# PS2_QUAKE=1: Link Quake Light Sport (256 KiB hunk; one BSP at a time, PAK on media).
PS2_QUAKE      ?= 1
PS2_QUAKE_SRCS = $(if $(filter 1,$(PS2_QUAKE)),$(QUAKE_SRCS),)
PS2_QUAKE_STAMP = .build/ps2-quake.stamp
PS2_QUAKE_PREV := $(shell cat $(PS2_QUAKE_STAMP) 2>/dev/null)
ifneq ($(PS2_QUAKE_PREV),$(PS2_QUAKE))
$(shell mkdir -p ./.build && echo '$(PS2_QUAKE)' >$(PS2_QUAKE_STAMP) && \
        rm -f $(QUAKE_SRCS:.c=.ps2.o) $(QUAKE_SRCS:.c=.pbench.o) \
              src/drivers/playstation2/ps2_heap.ps2.o src/drivers/playstation2/ps2_heap.pbench.o)
$(info [PS2] PS2_QUAKE is now $(PS2_QUAKE) (was $(if $(PS2_QUAKE_PREV),$(PS2_QUAKE_PREV),unset)) -- the Quake units will rebuild)
endif
ifeq ($(PS2_QUAKE),1)
PS2_CFLAGS    += -DBTRON_PS2_QUAKE=1
endif
PS2_SRCS       = $(PS2_STARTUP)           \
                 src/cores/core_init.c    \
                 src/drivers/playstation2/ps2_heap.c \
                 src/drivers/playstation2/ps2_console.c \
                 src/drivers/playstation2/ps2_kbd.c \
                 src/drivers/playstation2/ps2_boot.c \
                 src/drivers/playstation2/ps2_gs.c \
                 src/drivers/playstation2/ps2_gs_present.c \
                 src/drivers/playstation2/ps2_gif_ring.c \
                 src/drivers/playstation2/ps2_gs_pack.c \
                 src/drivers/playstation2/ps2_gs_vram.c \
                 src/drivers/playstation2/ps2_gs_env.c \
                 src/drivers/playstation2/ps2_gs_poster.c \
                 src/drivers/playstation2/ps2_gs_rec.c \
                 src/drivers/playstation2/ps2_gs_tex.c \
                 src/drivers/playstation2/ps2_gs_read.c \
                 src/drivers/playstation2/ps2_builtins.c \
                 src/drivers/playstation2/ps2_sio.c \
                 src/drivers/playstation2/ps2_pad.c \
                 src/drivers/playstation2/ps2_iopram.c \
                 src/drivers/playstation2/ps2_irq.c \
                 src/drivers/playstation2/ps2_usb.c \
                 src/drivers/playstation2/ps2_cdvd.c \
                 src/drivers/playstation2/ps2_iso9660.c \
                 src/drivers/playstation2/ps2_sif.c \
                 src/drivers/playstation2/ps2_fileio.c \
                 $(CLU_TERMIO_SRCS)       \
                 $(PS2_MSX_SRCS)          \
                 $(PS2_PW_SRCS)           \
                 $(TINYGL_SRCS)           \
                 $(PS2_GL_SRCS)           \
                 src/kernel/libstr.c      \
                 src/kernel/libint64.c    \
                 $(COMMON_NO_SDL_SRCS)    \
                 src/apps/workbench/ps2_tile.c
PS2_OBJS       = src/drivers/playstation2/boot_ps2.ps2.o         \
                 src/drivers/playstation2/ps2_gs_reg.ps2.o      \
                 $(PS2_SYS_VOL_OBJ)                    \
                 $(PS2_SRCS:.c=.ps2.o)

# Built-in /SYS filesystem volume embedded in image data; verify manifest integrity
PS2_SYS_VOL_OBJ     = src/drivers/playstation2/ps2_sys_vol.ps2.o
PS2_SYS_VOL_SOURCE  = src/drivers/playstation2/ps2_sys_vol.s
$(PS2_SYS_VOL_OBJ): $(PS2_SYS_VOL_SOURCE) btron_sys.vol
	@./mkbtronfs tools/manifest.txt -o .build/ps2-vol-fresh.vol >/dev/null 2>&1; \
	 if ! cmp -s .build/ps2-vol-fresh.vol btron_sys.vol; then \
	   echo "[PS2] btron_sys.vol is not the manifest ($$(cmp -l .build/ps2-vol-fresh.vol btron_sys.vol | wc -l | tr -d ' ') bytes): a host FS suite wrote into it, so the image relinks from a fresh build"; \
	   cp .build/ps2-vol-fresh.vol btron_sys.vol; \
	 fi; \
	 rm -f .build/ps2-vol-fresh.vol
	$(PS2_CC) -I. -c $< -o $@

# MIPS O32 calling convention rules: mips2 ISA ensures consistent varargs and 32-bit GPR ABI
%.ps2.o: %.s
	$(PS2_CC) -c $< -o $@

%.ps2.o: %.c
	$(PS2_CC) $(PS2_CFLAGS) -MMD -MP -c $< -o $@

# Disable LLVM shrink-wrap pass on gst_rt.c to work around compiler crash
src/media/sdk/gst_rt.ps2.o: PS2_CFLAGS += -mllvm -enable-shrink-wrap=false

# PS2_GL stamp: rebuild backend selection units on rasterizer change
PS2_GL_STAMP  = .build/ps2-gl.stamp
PS2_GL_PREV  := $(shell cat $(PS2_GL_STAMP) 2>/dev/null)
ifneq ($(PS2_GL_PREV),$(PS2_GL))
$(shell mkdir -p ./.build && echo '$(PS2_GL)' >$(PS2_GL_STAMP) && \
        rm -f src/desktop/graphics/gles/gl_backend_choice.ps2.o src/desktop/graphics/gles/gl_dispatch.ps2.o src/desktop/graphics/gles/egl_surface.ps2.o)
$(info [PS2] PS2_GL is now $(PS2_GL) (was $(if $(PS2_GL_PREV),$(PS2_GL_PREV),unset)) -- the objects that read it will rebuild)
endif

# PS2_MSX stamp: rebuild core_ps2 and ps2_heap on MSX configuration change
PS2_MSX_STAMP = .build/ps2-msx.stamp
PS2_MSX_PREV := $(shell cat $(PS2_MSX_STAMP) 2>/dev/null)
ifneq ($(PS2_MSX_PREV),$(PS2_MSX))
$(shell mkdir -p ./.build && echo '$(PS2_MSX)' >$(PS2_MSX_STAMP) && \
        rm -f src/cores/core_ps2.ps2.o src/cores/core_ps2.pbench.o \
              src/drivers/playstation2/ps2_heap.ps2.o src/drivers/playstation2/ps2_heap.pbench.o)
$(info [PS2] PS2_MSX is now $(PS2_MSX) (was $(if $(PS2_MSX_PREV),$(PS2_MSX_PREV),unset)) -- the units that read it will rebuild)
endif
ifeq ($(PS2_MSX),1)
PS2_CFLAGS    += -DBTRON_PS2_MSX=1
endif

# PS2_PW stamp: rebuild paint worker and core_ps2 on policy change
PS2_PW_STAMP = .build/ps2-pw.stamp
PS2_PW_PREV := $(shell cat $(PS2_PW_STAMP) 2>/dev/null)
ifneq ($(PS2_PW_PREV),$(PS2_PW))
$(shell mkdir -p ./.build && echo '$(PS2_PW)' >$(PS2_PW_STAMP) && \
        rm -f src/cores/core_ps2.ps2.o src/cores/core_ps2.pbench.o \
              src/drivers/playstation2/ps2_paint_worker.ps2.o)
$(info [PS2] PS2_PW is now $(PS2_PW) (was $(if $(PS2_PW_PREV),$(PS2_PW_PREV),unset)) -- the paint worker's units will rebuild)
endif

# Shrink libstr fallback arena to 64 bytes; PS2 uses native s_ps2_heap allocator
src/kernel/libstr.ps2.o: PS2_CFLAGS += -DBTRON_LIBSTR_NO_HEAP=1

# PS2 headless measurement build (core_ps2.pbench.o)
PS2_BENCH_OBJ  = src/cores/core_ps2.pbench.o
PS2_BENCH_OBJS = $(PS2_BENCH_OBJ) $(filter-out src/cores/core_ps2.ps2.o,$(PS2_OBJS))
PS2_BENCH_TARGET = btron-ps2-bench.elf
# BENCH_APP: Enable XMB app lifecycle benchmarking (0: desktop idle)
BENCH_APP      ?= 0
# BENCH_APP_FRAMES: Frame budget before Escape injection (default: 60)
BENCH_APP_FRAMES ?= 60
# BENCH_LAUNCH_FRAMES: Frames for launcher-by-key cycle (default: 3)
BENCH_LAUNCH_FRAMES ?= 3
# BENCH_GS: GS hardware census (1: stage 1 readback, 2: live desktop, 6: PSMCT16 plate, 7: rotation)
BENCH_GS       ?= 0
# BENCH_TEX: Texture plane before/after title-tick differential census
# BENCH_ORDER: Execution ordering (1: lilcu demo before XMB, 0: XMB first)
BENCH_TEX      ?= 0
BENCH_ORDER    ?= 0
$(PS2_BENCH_OBJ): PS2_CFLAGS += -DBTRON_PS2_BENCH=1 -DBTRON_PS2_PROBE=1 -DBTRON_PS2_BENCH_APP=$(BENCH_APP) \
                                -DBTRON_PS2_BENCH_APP_FRAMES=$(BENCH_APP_FRAMES)u \
                                -DBTRON_PS2_BENCH_LAUNCH_FRAMES=$(BENCH_LAUNCH_FRAMES)u \
                                -DBTRON_PS2_BENCH_TEX=$(BENCH_TEX) \
                                -DBTRON_PS2_BENCH_ORDER=$(BENCH_ORDER)u \
                                -DBTRON_PS2_BENCH_GS=$(BENCH_GS)

PS2_BGS_STAMP = .build/ps2-bgs.stamp
# The frame budget is IN the key: it is a -D of this same object, and a loop that re-runs with
# BENCH_APP_FRAMES=12 after a run at 60 would otherwise silently reuse the 60-frame image.
PS2_BGS_KEY := $(BENCH_APP):$(BENCH_GS):$(BENCH_LAUNCH_FRAMES):$(BENCH_APP_FRAMES):$(BENCH_TEX):$(BENCH_ORDER)
PS2_BGS_PREV := $(shell cat $(PS2_BGS_STAMP) 2>/dev/null)
ifneq ($(PS2_BGS_PREV),$(PS2_BGS_KEY))
$(shell mkdir -p ./.build && echo '$(PS2_BGS_KEY)' >$(PS2_BGS_STAMP) && rm -f $(PS2_BENCH_OBJ))
endif

%.pbench.o: %.c
	$(PS2_CC) $(PS2_CFLAGS) -MMD -MP -c $< -o $@

# PS2_FP: Floating-point mode (hw: COP1 hardware with CSR cost penalty; soft: software libcalls)
PS2_FP ?= hw
PS2_FP_OBJ     = src/drivers/playstation2/ps2_builtins.ps2.o
PS2_KERNEL_OBJ = src/desktop/graphics/tinygl/vertex_stage.ps2.o
PS2_WAVE_OBJ   = src/media/xmb/xmb_wave.ps2.o
PS2_FP_OBJS    = $(PS2_FP_OBJ) $(PS2_KERNEL_OBJ) $(PS2_WAVE_OBJ)
PS2_FP_CSR_COST = -mllvm -regalloc-csr-first-time-cost=1000000000
# PS2_FP stamp: rebuild FPU-using units on mode change
PS2_FP_STAMP = .build/ps2-fp.stamp
PS2_FP_PREV := $(shell cat $(PS2_FP_STAMP) 2>/dev/null)
ifneq ($(PS2_FP_PREV),$(PS2_FP))
$(shell mkdir -p ./.build && echo '$(PS2_FP)' >$(PS2_FP_STAMP) && rm -f $(PS2_FP_OBJS))
$(info [PS2] PS2_FP is now $(PS2_FP) (was $(if $(PS2_FP_PREV),$(PS2_FP_PREV),unset)) -- the units that read it will rebuild)
endif
ifeq ($(PS2_FP),hw)
$(PS2_FP_OBJS): PS2_CC := $(subst -msoft-float,-mhard-float,$(PS2_CC)) $(PS2_FP_CSR_COST)
$(PS2_FP_OBJS): PS2_CFLAGS += -DBTRON_PS2_FP_HW
endif

# PS2_FP_COUNT stamp: rebuild profiling units on counter state change
PS2_FPC_OBJS = src/drivers/playstation2/ps2_builtins.ps2.o src/media/xmb/xmb.ps2.o \
               src/cores/core_ps2.ps2.o src/cores/core_ps2.pbench.o
PS2_FPC_STAMP = .build/ps2-fpc.stamp
PS2_FPC_PREV := $(shell cat $(PS2_FPC_STAMP) 2>/dev/null)
ifneq ($(PS2_FPC_PREV),$(PS2_FP_COUNT))
$(shell mkdir -p ./.build && echo '$(PS2_FP_COUNT)' >$(PS2_FPC_STAMP) && rm -f $(PS2_FPC_OBJS))
$(info [PS2] PS2_FP_COUNT is now $(PS2_FP_COUNT) (was $(if $(PS2_FPC_PREV),$(PS2_FPC_PREV),unset)) -- the units that read it will rebuild)
endif

# PS2_IDENT stamp: rebuild invariance tracking units on state change
PS2_IDENT_OBJS = src/desktop/graphics/eegs/eegs_present.ps2.o src/media/xmb/xmb.ps2.o \
                 src/cores/core_ps2.ps2.o src/cores/core_ps2.pbench.o
PS2_IDENT_STAMP = .build/ps2-ident.stamp
PS2_IDENT_PREV := $(shell cat $(PS2_IDENT_STAMP) 2>/dev/null)
ifneq ($(PS2_IDENT_PREV),$(PS2_IDENT))
$(shell mkdir -p ./.build && echo '$(PS2_IDENT)' >$(PS2_IDENT_STAMP) && rm -f $(PS2_IDENT_OBJS))
$(info [PS2] PS2_IDENT is now $(PS2_IDENT) (was $(if $(PS2_IDENT_PREV),$(PS2_IDENT_PREV),unset)) -- the units that read it will rebuild)
endif

ps2-bench: $(PS2_BENCH_TARGET)

# Benchmark image links with 64 KiB stack reserve for extra .rodata headroom
$(PS2_BENCH_TARGET): $(PS2_BENCH_OBJS) $(PS2_LD_SCRIPT)
	$(MIPS_LD) -T $(PS2_LD_SCRIPT) --defsym=__ps2_stack_reserve=0x10000 $(PS2_BENCH_OBJS) -o $@
	@echo "[PS2-BENCH] Built: $@"
	@file $@

# Auto-generated header dependencies for PS2 objects
-include $(PS2_OBJS:.ps2.o=.ps2.d)

ps2: $(PS2_TARGET) $(PS2_ISO)

$(PS2_TARGET): $(PS2_OBJS) $(PS2_LD_SCRIPT)
	@echo "=========================================================="
	@echo " Building B-System PS2 Kernel: $@"
	@echo "=========================================================="
	$(MIPS_LD) -T $(PS2_LD_SCRIPT) $(PS2_OBJS) -o $@
	@echo "[PS2-ELF] Built: $@"
	@file $@

# Package bootable ISO (SYSTEM.CNF, BTRON.ELF, SYS.VOL, ANDERS.VOL, QUAKE assets).
# Retail PS2 model: the ELF is the RAM working set; PAK/ROMs live on the
# disc (CD ~700 MiB, DVD ~4 GiB).  Do not incbin pak0 into the ELF.
$(PS2_ISO): $(PS2_TARGET) mkbtronfs tools/manifest.txt tools/manifest_anders.txt assets/quake/pak/pak0.pak $(wildcard assets/quake/drone1m*.dem)
	@echo "=========================================================="
	@echo " Packaging Bootable PS2 Disc ISO: $@"
	@echo "=========================================================="
	@rm -rf build/ps2_iso
	@mkdir -p build/ps2_iso/QUAKE
	@printf "BOOT2 = cdrom0:\\\\BTRON.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n" > build/ps2_iso/SYSTEM.CNF
	@cp $(PS2_TARGET) build/ps2_iso/BTRON.ELF
	@./mkbtronfs tools/manifest.txt        -o build/ps2_iso/SYS.VOL
	@./mkbtronfs tools/manifest_anders.txt -o build/ps2_iso/ANDERS.VOL -l ANDERS
	@cp assets/quake/pak/pak0.pak build/ps2_iso/PAK0.PAK
	@cp assets/quake/pak/pak0.pak build/ps2_iso/QUAKE/PAK0.PAK
	@for f in assets/quake/drone1m*.dem; do n=$$(basename "$$f" | tr '[:lower:]' '[:upper:]'); \
	    cp "$$f" "build/ps2_iso/QUAKE/$$n"; done
	@if command -v hdiutil >/dev/null 2>&1; then \
	    rm -f $@; \
	    hdiutil makehybrid -iso -joliet -default-volume-name "BTRON3_PS2" -o $@ build/ps2_iso >/dev/null; \
	elif command -v genisoimage >/dev/null 2>&1; then \
	    genisoimage -o $@ -V "BTRON3_PS2" build/ps2_iso >/dev/null; \
	elif command -v mkisofs >/dev/null 2>&1; then \
	    mkisofs -o $@ -V "BTRON3_PS2" build/ps2_iso >/dev/null; \
	elif command -v xorriso >/dev/null 2>&1; then \
	    xorriso -as mkisofs -o $@ -V "BTRON3_PS2" build/ps2_iso >/dev/null 2>&1; \
	fi
	@python3 tools/ps2_iso_semicolons.py $@
	@rm -rf build/ps2_iso
	@echo "[PS2-ISO] Packaged: $@"
	@echo "[PS2-ISO] ELF frozen: $$(shasum -a 1 $(PS2_TARGET) 2>/dev/null | cut -d' ' -f1) (link line untouched by this rule)"
	@file $@

run-ps: run-ps2

# Prepare emulator run environment and record build configuration dials
ps2-dials:
	@echo "PS2_GL=$(PS2_GL)"
	@echo "PS2_MSX=$(PS2_MSX)"
	@echo "PS2_FP=$(PS2_FP)"
	@echo "PS2_FP_COUNT=$(PS2_FP_COUNT)"
	@echo "BENCH_APP=$(BENCH_APP)"
	@echo "BENCH_APP_FRAMES=$(BENCH_APP_FRAMES)"
	@echo "BENCH_GS=$(BENCH_GS)"
	@echo "AUTO_GUI=$(AUTO_GUI)"
	@echo "ISO=$(ISO)"
	@echo "PS2_TARGET=$(PS2_TARGET)"
	@echo "PS2_ISO=$(PS2_ISO)"

ps2-cfg:
	@if [ ! -f "$(PCSX2_DATA)/inis/PCSX2.ini" ]; then \
	    echo "[ERROR] $(PCSX2_DATA)/inis/PCSX2.ini is missing"; exit 1; fi
	@if [ ! -d "$(PCSX2_BIOS)" ]; then \
	    echo "[ERROR] No PS2 BIOS at $(PCSX2_BIOS) -- pass PCSX2_BIOS=<dir>"; exit 1; fi
	@for d in bios logs memcards sstates snaps cache; do mkdir -p "$(PCSX2_DATA)/$$d"; done
	@for f in "$(PCSX2_BIOS)"/*.bin; do n=$$(basename "$$f"); \
	    if [ ! -e "$(PCSX2_DATA)/bios/$$n" ] && [ ! -L "$(PCSX2_DATA)/bios/$$n" ]; then \
	        ln -s "$$f" "$(PCSX2_DATA)/bios/$$n"; fi; done

# Default boots the ISO so CDVD/FILEIO see PAK0.PAK.  ELF=1 is host: ELF
# with an empty tray (Quake stays on the slipgate stub).
run-ps2: $(PS2_TARGET) $(if $(filter 1,$(ELF)),,$(PS2_ISO)) ps2-cfg
	@echo "=========================================================="
	@echo " Launching B-System PS2 on PCSX2 Emulator"
	@echo " Machine  : Sony PlayStation 2 (Emotion Engine R5900)"
	@echo " CPU      : 128-bit SIMD MIPS Core @ 294.912 MHz"
	@echo " RAM      : 32 MB RDRAM | VRAM: 4 MB GS eDRAM"
	@echo " Display  : Graphics Synthesizer 800x600 @ 32-bpp RGBA (VESA)"
	@echo " GUI Mode : $(if $(filter 1,$(AUTO_GUI)),Automatic Direct GUI,Two-Stage Console -> startx)"
	@if [ "$(ELF)" = "1" ]; then \
	    echo " Target   : Direct ELF: $(PS2_TARGET)  (empty tray; Quake PAK misses)"; \
	else \
	    echo " Target   : Disc ISO: $(PS2_ISO)"; \
	fi
	@echo " Emulator : $(PCSX2_BIN)"
	@echo " Config   : $(PCSX2_DATA)/inis/PCSX2.ini"
	@echo "=========================================================="
	@if [ "$(ELF)" = "1" ]; then boot="$(CURDIR)/$(PS2_TARGET)"; \
	else boot="$(CURDIR)/$(PS2_ISO)"; fi; \
	if [ -x "$(PCSX2_BIN)" ]; then \
	    "$(PCSX2_BIN)" -datapath "$(PCSX2_DATADIR)" -fastboot "$$boot"; \
	elif [ -d "/Applications/PCSX2.app" ]; then \
	    open -a /Applications/PCSX2.app --args -datapath "$(PCSX2_DATADIR)" -fastboot "$$boot"; \
	else \
	    echo "[ERROR] PCSX2 not found at $(PCSX2_BIN)"; \
	    exit 1; \
	fi

# Large-argument float trig folding verification probe
PS2_FARFOLD_OBJ = ./.build/ps2_farfold_probe.o

$(PS2_FARFOLD_OBJ): verify/tests/ps2_farfold_probe.c include/rt/math.h
	@mkdir -p ./.build
	$(PS2_CC) $(PS2_CFLAGS) -c $< -o $@

test-ps2: $(PS2_TARGET) $(PS2_ISO) $(PS2_FARFOLD_OBJ)
	@PS2_GL=$(PS2_GL) PS2_FP=$(PS2_FP) PS2_FP_COUNT=$(PS2_FP_COUNT) PS2_MSX=$(PS2_MSX) ./scripts/test_ps2.sh

# Verify PS2 soft-float runtime against host hardware FPU
TEST_PSFLOAT_SRCS = verify/tests/test_ps2_softfloat.c
TEST_PSFLOAT_OBJS = $(TEST_PSFLOAT_SRCS:.c=.test.o)
TEST_PSFLOAT_BIN  = ./.build/test_ps2_softfloat

test-ps2-softfloat: $(TEST_PSFLOAT_BIN)
	@echo "=========================================================="
	@echo " Running PS2 soft-float runtime vs the host FPU..."
	@echo "=========================================================="
	@./$(TEST_PSFLOAT_BIN)

$(TEST_PSFLOAT_BIN): $(TEST_PSFLOAT_OBJS) src/drivers/playstation2/ps2_builtins.c
	@mkdir -p ./.build
	$(CC) $(TEST_PSFLOAT_OBJS) -o $@ $(LDFLAGS) -lm

# The ARM32 spelling of the same instrument.  VFPv4 converts a *32-bit* core
# register to or from a floating one and nothing wider, and the Pi 2B image links
# -nostdlib, so the DAW's 64-bit sample/nanosecond arithmetic reaches the runtime
# only through names clang invents -- __aeabi_l2d, __aeabi_ul2d, __aeabi_l2f,
# __aeabi_ul2f, __aeabi_d2lz, __aeabi_f2ulz.  `make arm-elf` proves they exist; only
# this proves they are right, because a conversion that is 1 ulp off shows up as an
# audio glitch on a monitor and as nothing on a host.
TEST_ARMAEABI_SRCS = verify/tests/test_arm_aeabi.c
TEST_ARMAEABI_OBJS = $(TEST_ARMAEABI_SRCS:.c=.test.o)
TEST_ARMAEABI_BIN  = ./.build/test_arm_aeabi

test-arm-aeabi: $(TEST_ARMAEABI_BIN)
	@echo "=========================================================="
	@echo " Running ARM EABI conversion runtime vs the host FPU..."
	@echo "=========================================================="
	@./$(TEST_ARMAEABI_BIN)

$(TEST_ARMAEABI_BIN): $(TEST_ARMAEABI_OBJS) src/kernel/libaeabi.c
	@mkdir -p ./.build
	$(CC) $(TEST_ARMAEABI_OBJS) -o $@ $(LDFLAGS) -lm

TEST_HDMI_AUDIO_SRCS = verify/tests/test_hdmi_audio.c src/drivers/broadcom/bcm2711_hdmi_audio.c src/drivers/broadcom/usb_audio_uac1.c
TEST_HDMI_AUDIO_OBJS = $(TEST_HDMI_AUDIO_SRCS:.c=.test.o)
TEST_HDMI_AUDIO_BIN  = ./.build/test_hdmi_audio

test-hdmi-audio: $(TEST_HDMI_AUDIO_BIN)
	@echo "=========================================================="
	@echo " Running BCM2711 HDMI Audio ACR, InfoFrame & Latency Tests..."
	@echo "=========================================================="
	@./$(TEST_HDMI_AUDIO_BIN)

$(TEST_HDMI_AUDIO_BIN): $(TEST_HDMI_AUDIO_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_HDMI_AUDIO_OBJS) -o $@ $(LDFLAGS) -lm

TEST_USB_AUDIO_SRCS = verify/tests/test_usb_audio.c src/drivers/broadcom/usb_audio_uac1.c
TEST_USB_AUDIO_OBJS = $(TEST_USB_AUDIO_SRCS:.c=.test.o)
TEST_USB_AUDIO_BIN  = ./.build/test_usb_audio

test-usb-audio: $(TEST_USB_AUDIO_BIN)
	@echo "=========================================================="
	@echo " Running USB Audio Class 1.0 (UAC1) xHCI Tests..."
	@echo "=========================================================="
	@./$(TEST_USB_AUDIO_BIN)

$(TEST_USB_AUDIO_BIN): $(TEST_USB_AUDIO_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_USB_AUDIO_OBJS) -o $@ $(LDFLAGS) -lm

TEST_BT_AUDIO_SRCS = verify/tests/test_b_bt_audio.c \
                     src/drivers/bluetooth/b_bt_core.c \
                     src/drivers/bluetooth/b_bt_hci.c \
                     src/drivers/bluetooth/b_bt_l2cap.c \
                     src/drivers/bluetooth/b_bt_avdtp.c \
                     src/drivers/bluetooth/b_bt_sbc.c \
                     src/drivers/bluetooth/b_bt_audio.c
TEST_BT_AUDIO_OBJS = $(TEST_BT_AUDIO_SRCS:.c=.test.o)
TEST_BT_AUDIO_BIN  = ./.build/test_b_bt_audio

test-bt-audio: $(TEST_BT_AUDIO_BIN)
	@echo "=========================================================="
	@echo " Running B-BT Bluetooth A2DP & NASA JPL Audio Tests..."
	@echo "=========================================================="
	@./$(TEST_BT_AUDIO_BIN)

$(TEST_BT_AUDIO_BIN): $(TEST_BT_AUDIO_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_BT_AUDIO_OBJS) -o $@ $(LDFLAGS) -lm

# Increment 2: GS packet builder encoding verification vs register layout literals.
TEST_GSPACK_SRCS = verify/tests/test_ps2_gs_pack.c
TEST_GSPACK_OBJS = $(TEST_GSPACK_SRCS:.c=.test.o)
TEST_GSPACK_BIN  = ./.build/test_ps2_gs_pack

test-ps2-gs-pack: $(TEST_GSPACK_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS packet builder vs hand-computed layouts..."
	@echo "=========================================================="
	@./$(TEST_GSPACK_BIN)

$(TEST_GSPACK_BIN): $(TEST_GSPACK_OBJS) src/drivers/playstation2/ps2_gs_pack.c src/drivers/playstation2/ps2_gs_pack.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSPACK_OBJS) -o $@ $(LDFLAGS)

# ISO9660 finder: synthetic PVD plus btron-ps2.iso when present.
TEST_ISO9660_SRCS = verify/tests/test_ps2_iso9660.c
TEST_ISO9660_OBJS = $(TEST_ISO9660_SRCS:.c=.test.o)
TEST_ISO9660_BIN  = ./.build/test_ps2_iso9660

test-ps2-iso9660: $(TEST_ISO9660_BIN)
	@echo "=========================================================="
	@echo " Running PS2 ISO9660 finder vs synthetic PVD and disc ISO..."
	@echo "=========================================================="
	@./$(TEST_ISO9660_BIN)

TEST_DEMOPATH_SRCS = verify/tests/test_demo_paths.c
TEST_DEMOPATH_OBJS = $(TEST_DEMOPATH_SRCS:.c=.test.o)
TEST_DEMOPATH_BIN  = ./.build/test_demo_paths

test-demo-paths: $(TEST_DEMOPATH_BIN) btron_sys.vol
	@echo "=========================================================="
	@echo " Running Demo_Play path list vs SYS.VOL..."
	@echo "=========================================================="
	@./$(TEST_DEMOPATH_BIN)

$(TEST_DEMOPATH_BIN): $(TEST_DEMOPATH_OBJS) src/media/quake/include/cl_demo_paths.h
	@mkdir -p ./.build
	$(CC) $(TEST_DEMOPATH_OBJS) -o $@ $(LDFLAGS)

TEST_PRINTFF_SRCS = verify/tests/test_printf_float.c
TEST_PRINTFF_OBJS = $(TEST_PRINTFF_SRCS:.c=.test.o)
TEST_PRINTFF_BIN  = ./.build/test_printf_float

test-printf-float: $(TEST_PRINTFF_BIN)
	@echo "=========================================================="
	@echo " Running tkl_vsnprintf binary32 / C99 (NULL, 0)..."
	@echo "=========================================================="
	@./$(TEST_PRINTFF_BIN)

$(TEST_PRINTFF_BIN): $(TEST_PRINTFF_OBJS) src/kernel/libstr.c include/libstr.h
	@mkdir -p ./.build
	$(CC) $(TEST_PRINTFF_OBJS) -o $@ $(LDFLAGS)

# PAK and BSP spans. The hostile pair is INT_MAX-8 + 64, which wraps in signed int.
TEST_FSSPAN_SRC = verify/tests/test_fs_span.c
TEST_FSSPAN_BIN = ./.build/test_fs_span

test-fs-span: $(TEST_FSSPAN_BIN)
	@echo "=========================================================="
	@echo " Running btron_span_inside against a wrapping PAK/BSP length..."
	@echo "=========================================================="
	@./$(TEST_FSSPAN_BIN)

$(TEST_FSSPAN_BIN): $(TEST_FSSPAN_SRC) include/btron/span.h
	@mkdir -p ./.build
	$(CC) -std=c99 -Wall -Wextra -Wformat=2 -O1 -g -fsanitize=undefined \
		-fno-omit-frame-pointer -Iinclude $(TEST_FSSPAN_SRC) -o $@

# Kernel and record-file copies. A 79-byte name must come back as 63 bytes
# plus a NUL, and the caller's next byte must stay put. An unbounded copy
# walks off g_sys_name[64].
TEST_KCOPY_SRC = verify/tests/test_kcopy.c
TEST_KCOPY_BIN = ./.build/test_kcopy
TEST_KCOPY_OBJS = src/kernel/sys_mgmt.c src/kernel/dev_mgr.c src/kernel/fs_record.c src/kernel/libstr.c

test-kcopy: $(TEST_KCOPY_BIN)
	@echo "=========================================================="
	@echo " Running kernel and record-file name copies..."
	@echo "=========================================================="
	@./$(TEST_KCOPY_BIN)

$(TEST_KCOPY_BIN): $(TEST_KCOPY_SRC) $(TEST_KCOPY_OBJS) include/libstr.h
	@mkdir -p ./.build
	$(CC) -std=c99 -Wall -Wextra -Wformat=2 -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer -DBTRON_LIBSTR_NO_HEAP $(CFLAGS) \
		$(TEST_KCOPY_SRC) $(TEST_KCOPY_OBJS) -o $@ -lpthread

# Sized copy, freestanding assert record, and the local static-analysis gate.
# clang --analyze is the scanner OpenBSD drives with scan-build / CodeChecker.
# flawfinder (OpenBSD port devel/flawfinder) and cppcheck are optional: a missing
# one prints its brew line and does not fail the sanitized contract test.
TEST_LIBSTR_SRC = verify/tests/test_libstr_bounds.c
TEST_LIBSTR_BIN = ./.build/test_libstr_bounds
TEST_LIBSTR_ASAN = ./.build/test_libstr_bounds_asan
TEST_LIBSTR_PROBE = ./.build/test_assert_rt.o
TEST_LIBSTR_INCLUDES = -Iinclude -Iinclude/rt -Isrc/kernel

test-libstr-bounds: $(TEST_LIBSTR_BIN)
	@echo "=========================================================="
	@echo " Running tkl_strscpy / strscat / strpad and assert record..."
	@echo "=========================================================="
	@./$(TEST_LIBSTR_BIN)

$(TEST_LIBSTR_PROBE): $(TEST_LIBSTR_SRC) include/rt/assert.h
	@mkdir -p ./.build
	$(CC) -std=c99 -Wall -Wextra -ffreestanding -DBTRON_ASSERT_RT_PROBE \
		$(TEST_LIBSTR_INCLUDES) -c $(TEST_LIBSTR_SRC) -o $@

$(TEST_LIBSTR_BIN): $(TEST_LIBSTR_SRC) $(TEST_LIBSTR_PROBE) src/kernel/libstr.c include/libstr.h
	@mkdir -p ./.build
	$(CC) -std=c99 -Wall -Wextra -Wformat=2 -O2 $(TEST_LIBSTR_INCLUDES) \
		$(TEST_LIBSTR_SRC) $(TEST_LIBSTR_PROBE) -o $@

$(TEST_LIBSTR_ASAN): $(TEST_LIBSTR_SRC) $(TEST_LIBSTR_PROBE) src/kernel/libstr.c include/libstr.h
	@mkdir -p ./.build
	$(CC) -std=c99 -Wall -Wextra -Wformat=2 -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer $(TEST_LIBSTR_INCLUDES) \
		$(TEST_LIBSTR_SRC) $(TEST_LIBSTR_PROBE) -o $@

sec-libstr: $(TEST_LIBSTR_ASAN)
	@echo "=========================================================="
	@echo " sec-libstr: ASan/UBSan contract, then clang --analyze"
	@echo "=========================================================="
	@./$(TEST_LIBSTR_ASAN)
	$(CC) --analyze -std=c99 -Wall -Wextra -Wformat=2 $(TEST_LIBSTR_INCLUDES) \
		-Xanalyzer -analyzer-output=text src/kernel/libstr.c
	@if command -v flawfinder >/dev/null 2>&1; then \
		flawfinder --minlevel=4 --quiet src/kernel/libstr.c; \
	else \
		echo "flawfinder not on PATH (OpenBSD port devel/flawfinder): brew install flawfinder"; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck -j4 --std=c99 --enable=warning,portability --error-exitcode=1 --quiet \
			$(TEST_LIBSTR_INCLUDES) src/kernel/libstr.c; \
	else \
		echo "cppcheck not on PATH: brew install cppcheck"; \
	fi

# ── BE Binary Editor & Disassembler Security Analysis ────────────────────────
BE_SEC_INCLUDES = -Iinclude -Iinclude/rt -Isrc/apps/clu -Isrc/apps/clu/be

BE_CORE_SRCS = \
	src/apps/clu/be/be.c \
	src/apps/clu/be/editor.c \
	src/apps/clu/be/native.c \
	src/apps/clu/be/native_dasm.c \
	src/apps/clu/be/dasm/dasm.c \
	src/apps/clu/be/hex/hex.c \
	src/apps/clu/be/term/buffer.c \
	src/apps/clu/be/term/terminal.c

sec-be:
	@echo "=========================================================="
	@echo " sec-be: clang --analyze on BE core + all disassemblers"
	@echo "=========================================================="
	$(CC) --analyze -std=c99 -Wall -Wextra $(BE_SEC_INCLUDES) \
		-Xanalyzer -analyzer-output=text $(BE_CORE_SRCS)
	@echo "  (arch/ disassemblers: clang --analyze deferred to slice 8)"
	@if command -v flawfinder >/dev/null 2>&1; then \
		flawfinder --minlevel=4 --quiet src/apps/clu/be/; \
	else \
		echo "flawfinder not on PATH: brew install flawfinder"; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck -j4 --std=c99 --enable=warning,portability --quiet \
			$(BE_SEC_INCLUDES) src/apps/clu/be/; \
	else \
		echo "cppcheck not on PATH: brew install cppcheck"; \
	fi

# ── CLU Commands Security Analysis ───────────────────────────────────────────
CLU_SEC_INCLUDES = -Iinclude -Iinclude/rt -Isrc/apps/clu

CLU_TOP_SRCS = \
	src/apps/clu/clu.c \
	src/apps/clu/cp.c \
	src/apps/clu/df.c \
	src/apps/clu/fs.c \
	src/apps/clu/lang.c \
	src/apps/clu/ln.c \
	src/apps/clu/ls.c \
	src/apps/clu/mv.c \
	src/apps/clu/ps_vm.c \
	src/apps/clu/rm.c \
	src/apps/clu/sps.c \
	src/apps/clu/st.c \
	src/apps/clu/term.c \
	src/apps/clu/tp.c \
	src/apps/clu/tty.c \
	src/apps/clu/vfs.c \
	src/apps/clu/sh/sh.c \
	src/apps/clu/sc/sc.c \
	src/apps/clu/sc/input.c \
	src/apps/clu/sc/menus.c \
	src/apps/clu/sc/files.c \
	src/apps/clu/tv/tv.c

sec-clu:
	@echo "=========================================================="
	@echo " sec-clu: clang --analyze + flawfinder + cppcheck on CLU"
	@echo "=========================================================="
	$(CC) --analyze -std=c99 -Wall -Wextra $(CLU_SEC_INCLUDES) \
		-Xanalyzer -analyzer-output=text $(CLU_TOP_SRCS)
	@if command -v flawfinder >/dev/null 2>&1; then \
		flawfinder --minlevel=4 --quiet \
			src/apps/clu/sh/ src/apps/clu/sc/ src/apps/clu/tv/ \
			$(CLU_TOP_SRCS); \
	else \
		echo "flawfinder not on PATH: brew install flawfinder"; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck -j4 --std=c99 --enable=warning,portability --quiet \
			$(CLU_SEC_INCLUDES) \
			src/apps/clu/sh/ src/apps/clu/sc/ src/apps/clu/tv/ \
			$(CLU_TOP_SRCS); \
	else \
		echo "cppcheck not on PATH: brew install cppcheck"; \
	fi

# ── Kernel Security Analysis ──────────────────────────────────────────────────
KERNEL_SEC_INCLUDES = -Iinclude -Iinclude/rt -Iinclude/drivers/broadcom

sec-kernel:
	@echo "=========================================================="
	@echo " sec-kernel: clang --analyze + flawfinder + cppcheck"
	@echo "=========================================================="
	@find src/kernel -maxdepth 1 -name '*.c' | sort | \
		xargs $(CC) --analyze -std=c99 -Wall -Wextra $(KERNEL_SEC_INCLUDES) \
		-Xanalyzer -analyzer-output=text
	@if command -v flawfinder >/dev/null 2>&1; then \
		flawfinder --minlevel=4 --quiet src/kernel/; \
	else \
		echo "flawfinder not on PATH: brew install flawfinder"; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck -j4 --std=c99 --enable=warning,portability --quiet \
			$(KERNEL_SEC_INCLUDES) src/kernel/; \
	else \
		echo "cppcheck not on PATH: brew install cppcheck"; \
	fi

# ── Filesystem Security Analysis ──────────────────────────────────────────────
FS_SEC_INCLUDES = -Iinclude -Iinclude/rt -Iinclude/drivers/broadcom

sec-fs:
	@echo "=========================================================="
	@echo " sec-fs: clang --analyze + flawfinder + cppcheck on src/fs"
	@echo "=========================================================="
	@find src/fs -maxdepth 1 -name '*.c' | sort | \
		xargs $(CC) --analyze -std=c99 -Wall -Wextra $(FS_SEC_INCLUDES) \
		-Xanalyzer -analyzer-output=text
	@if command -v flawfinder >/dev/null 2>&1; then \
		flawfinder --minlevel=4 --quiet src/fs/; \
	else \
		echo "flawfinder not on PATH: brew install flawfinder"; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck -j4 --std=c99 --enable=warning,portability --quiet \
			$(FS_SEC_INCLUDES) src/fs/; \
	else \
		echo "cppcheck not on PATH: brew install cppcheck"; \
	fi

# ── Drivers Security Analysis ─────────────────────────────────────────────────
DRIVERS_SEC_INCLUDES = -Iinclude -Iinclude/rt -Iinclude/drivers -Iinclude/drivers/broadcom

sec-drivers:
	@echo "=========================================================="
	@echo " sec-drivers: flawfinder + cppcheck on src/drivers"
	@echo "=========================================================="
	@if command -v flawfinder >/dev/null 2>&1; then \
		flawfinder --minlevel=4 --quiet src/drivers/; \
	else \
		echo "flawfinder not on PATH: brew install flawfinder"; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck -j4 --std=c99 --enable=warning,portability --quiet \
			$(DRIVERS_SEC_INCLUDES) src/drivers/; \
	else \
		echo "cppcheck not on PATH: brew install cppcheck"; \
	fi

# ── Aggregated Security Gate ──────────────────────────────────────────────────
security-report:
	@python3 scripts/security_report.py

security: sec-libstr sec-be sec-clu sec-kernel sec-fs sec-drivers
	@echo "=========================================================="
	@echo " ALL SECURITY SCANS COMPLETE"
	@echo "=========================================================="
	@python3 scripts/security_report.py

test-ps2-cdvd-loop:
	@echo "=========================================================="
	@echo " Running PS2 Quake CDVD UART gate loop..."
	@echo "=========================================================="
	@python3 tools/ps2_quake_cdvd_loop.py --self-test

$(TEST_ISO9660_BIN): $(TEST_ISO9660_OBJS) src/drivers/playstation2/ps2_iso9660.c src/drivers/playstation2/ps2_iso9660.h
	@mkdir -p ./.build
	$(CC) $(TEST_ISO9660_OBJS) -o $@ $(LDFLAGS)

# Increment 3: GS drawing environment registers and VRAM page base arithmetic.
TEST_GSENV_SRCS = verify/tests/test_ps2_gs_env.c
TEST_GSENV_OBJS = $(TEST_GSENV_SRCS:.c=.test.o)
TEST_GSENV_BIN  = ./.build/test_ps2_gs_env

test-ps2-gs-env: $(TEST_GSENV_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS draw environment and VRAM budget..."
	@echo "=========================================================="
	@./$(TEST_GSENV_BIN)

$(TEST_GSENV_BIN): $(TEST_GSENV_OBJS) src/drivers/playstation2/ps2_gs_env.c src/drivers/playstation2/ps2_gs_vram.c
	@mkdir -p ./.build
	$(CC) $(TEST_GSENV_OBJS) -o $@ $(LDFLAGS)

# Increment 4: GS vertex recorder packet formatting and NLOOP count verification.
TEST_GSREC_SRCS = verify/tests/test_ps2_gs_rec.c
TEST_GSREC_OBJS = $(TEST_GSREC_SRCS:.c=.test.o)
TEST_GSREC_BIN  = ./.build/test_ps2_gs_rec

test-ps2-gs-rec: $(TEST_GSREC_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS vertex recorder vs hand-built GIF groups..."
	@echo "=========================================================="
	@./$(TEST_GSREC_BIN)

$(TEST_GSREC_BIN): $(TEST_GSREC_OBJS) src/drivers/playstation2/ps2_gs_rec.c src/drivers/playstation2/ps2_gs_pack.c src/drivers/playstation2/ps2_gs_rec.h src/drivers/playstation2/ps2_gs_pack.h verify/tests/gs_group_view.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSREC_OBJS) -o $@ $(LDFLAGS)

TEST_VU1GEOM_SRCS = verify/tests/test_ps2_vu1_geom.c
TEST_VU1GEOM_OBJS = $(TEST_VU1GEOM_SRCS:.c=.test.o)
TEST_VU1GEOM_BIN  = ./.build/test_ps2_vu1_geom

test-ps2-vu1-geom: $(TEST_VU1GEOM_BIN)
	@echo "=========================================================="
	@echo " Running PS2 VU1 geometry pipeline unit tests..."
	@echo "=========================================================="
	@./$(TEST_VU1GEOM_BIN)

$(TEST_VU1GEOM_BIN): $(TEST_VU1GEOM_OBJS) src/drivers/playstation2/ps2_vu1_geom.c src/drivers/playstation2/ps2_vu1_geom.h
	@mkdir -p ./.build
	$(CC) $(TEST_VU1GEOM_OBJS) -o $@ $(LDFLAGS)

TEST_VU0KERNEL_SRCS = verify/tests/test_ps2_vu0_kernel.c
TEST_VU0KERNEL_OBJS = $(TEST_VU0KERNEL_SRCS:.c=.test.o)
TEST_VU0KERNEL_BIN  = ./.build/test_ps2_vu0_kernel

test-ps2-vu0-kernel: $(TEST_VU0KERNEL_BIN)
	@echo "=========================================================="
	@echo " Running PS2 VU0 procedural geometry kernel unit tests..."
	@echo "=========================================================="
	@./$(TEST_VU0KERNEL_BIN)

$(TEST_VU0KERNEL_BIN): $(TEST_VU0KERNEL_OBJS) src/drivers/playstation2/ps2_vu0_kernel.c src/drivers/playstation2/ps2_vu0_kernel.h
	@mkdir -p ./.build
	$(CC) $(TEST_VU0KERNEL_OBJS) -o $@ $(LDFLAGS)

TEST_GSCUBE_SRCS = verify/tests/test_gscube_nexus.c
TEST_GSCUBE_OBJS = $(TEST_GSCUBE_SRCS:.c=.test.o)
TEST_GSCUBE_BIN  = ./.build/test_gscube_nexus

test-gscube-nexus: $(TEST_GSCUBE_BIN)
	@echo "=========================================================="
	@echo " Running GScube cluster scaling engine unit tests..."
	@echo "=========================================================="
	@./$(TEST_GSCUBE_BIN)

$(TEST_GSCUBE_BIN): $(TEST_GSCUBE_OBJS) src/desktop/graphics/eegs/gscube_nexus.c src/desktop/graphics/eegs/gscube_nexus.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSCUBE_OBJS) -o $@ $(LDFLAGS)

TEST_PS2ROUTING_BIN = ./.build/test_ps2_opengl_routing

test-ps2-opengl-routing: $(TEST_PS2ROUTING_BIN)
	@echo "=========================================================="
	@echo " Running PS2 clean-room OpenGL routing & gterm test..."
	@echo "=========================================================="
	@./$(TEST_PS2ROUTING_BIN)

$(TEST_PS2ROUTING_BIN): verify/tests/test_ps2_opengl_routing.c $(FS_OBJS) src/apps/clu/clu.host.o $(CLU_APPS_HOST_OBJS) \
                        src/desktop/graphics/eegs/backend_eegs.c src/drivers/playstation2/ps2_gs_rec.c src/drivers/playstation2/ps2_gs_tex.c \
                        src/drivers/playstation2/ps2_vu1_geom.c src/drivers/playstation2/ps2_vu0_kernel.c \
                        src/desktop/graphics/eegs/gscube_nexus.c src/drivers/playstation2/ps2_gs_env.c src/drivers/playstation2/ps2_gs_vram.c \
                        src/drivers/playstation2/ps2_gif_ring.c
	@mkdir -p ./.build
	$(CC) $(CFLAGS) -Isrc verify/tests/test_ps2_opengl_routing.c $(FS_OBJS) src/apps/clu/clu.host.o $(CLU_APPS_HOST_OBJS) src/desktop/graphics/eegs/backend_eegs.c src/drivers/playstation2/ps2_gs_rec.c src/drivers/playstation2/ps2_gs_tex.c -o $@ $(LDFLAGS) -lm


# LANE 9b: flat-color SPRITE batch packet generation and NLOOP overflow verification.
TEST_GSPOSTER_SRCS = verify/tests/test_ps2_gs_poster.c
TEST_GSPOSTER_OBJS = $(TEST_GSPOSTER_SRCS:.c=.test.o)
TEST_GSPOSTER_BIN  = ./.build/test_ps2_gs_poster

test-ps2-gs-poster: $(TEST_GSPOSTER_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS poster batch vs hand-built SPRITE groups..."
	@echo "=========================================================="
	@./$(TEST_GSPOSTER_BIN)

$(TEST_GSPOSTER_BIN): $(TEST_GSPOSTER_OBJS) src/drivers/playstation2/ps2_gs_poster.c \
                      src/drivers/playstation2/ps2_gs_poster.h \
                      src/drivers/playstation2/ps2_gs_pack.c src/drivers/playstation2/ps2_gs_pack.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSPOSTER_OBJS) -o $@ $(LDFLAGS)

# Increment 5b: GS texture-plane registry, VRAM layout, and TEX0 register encoding.
TEST_GSTEX_SRCS = verify/tests/test_ps2_gs_tex.c
TEST_GSTEX_OBJS = $(TEST_GSTEX_SRCS:.c=.test.o)
TEST_GSTEX_BIN  = ./.build/test_ps2_gs_tex

test-ps2-gs-tex: $(TEST_GSTEX_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS texture-plane registry vs hand-built TEX0..."
	@echo "=========================================================="
	@./$(TEST_GSTEX_BIN)

$(TEST_GSTEX_BIN): $(TEST_GSTEX_OBJS) src/drivers/playstation2/ps2_gs_tex.c \
                   src/drivers/playstation2/ps2_gs_pack.c src/drivers/playstation2/ps2_gs_vram.c \
                   src/drivers/playstation2/ps2_gs_tex.h src/drivers/playstation2/ps2_gs_pack.h \
                   src/drivers/playstation2/ps2_gs_vram.h \
                   src/media/xmb/xmb_ps2_atlas.h src/media/xmb/xmb_atlas.h src/media/xmb/xmb_icons.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSTEX_OBJS) -o $@ $(LDFLAGS)

# LANE 19 rung 2: Local->Host VRAM read request packet formatting and alignment verification.
TEST_GSREAD_SRCS = verify/tests/test_ps2_gs_read.c
TEST_GSREAD_OBJS = $(TEST_GSREAD_SRCS:.c=.test.o)
TEST_GSREAD_BIN  = ./.build/test_ps2_gs_read

test-ps2-gs-read: $(TEST_GSREAD_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS Local->Host request vs hand-computed packet words..."
	@echo "=========================================================="
	@./$(TEST_GSREAD_BIN)

$(TEST_GSREAD_BIN): $(TEST_GSREAD_OBJS) src/drivers/playstation2/ps2_gs_read.c \
                    src/drivers/playstation2/ps2_gs_pack.c src/drivers/playstation2/ps2_gs_vram.c \
                    src/drivers/playstation2/ps2_gs_read.h src/drivers/playstation2/ps2_gs_pack.h \
                    src/drivers/playstation2/ps2_gs_vram.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSREAD_OBJS) -o $@ $(LDFLAGS)

# Pointer dynamics: dual-speed stroke velocity segmentation.
TEST_PTRSTROKE_SRCS = verify/tests/test_ps2_ptr_stroke.c
TEST_PTRSTROKE_OBJS = $(TEST_PTRSTROKE_SRCS:.c=.test.o)
TEST_PTRSTROKE_BIN  = ./.build/test_ps2_ptr_stroke

# Pointer dynamics: differential pointer acceleration table check (dp_accel.c).
TEST_PTRACCUM_OBJ = verify/tests/test_ps2_ptr_accum.accum.o
TEST_PTRACCUM_BIN = ./.build/test_ps2_ptr_accum

test-ps2-pointer: $(TEST_PTRSTROKE_BIN) $(TEST_PTRACCUM_BIN)
	@echo "=========================================================="
	@echo " Running PS2 pointer stroke segmentation and the two-speed verdict..."
	@echo "=========================================================="
	@./$(TEST_PTRSTROKE_BIN)
	@echo ""
	@echo "=========================================================="
	@echo " Running PS2 pointer accumulation differential (dp_accel.c)..."
	@echo "=========================================================="
	@./$(TEST_PTRACCUM_BIN)

$(TEST_PTRSTROKE_BIN): $(TEST_PTRSTROKE_OBJS) src/cores/ps2_ptr_stroke.h
	@mkdir -p ./.build
	$(CC) $(TEST_PTRSTROKE_OBJS) -o $@ $(LDFLAGS)

$(TEST_PTRACCUM_OBJ): verify/tests/test_ps2_ptr_accum.c src/desktop/graphics/dp_accel.c include/btron/dp_accel.h
	$(CC) $(CFLAGS) -DBTRON_PS2_TARGET -MMD -MP -c $< -o $@

$(TEST_PTRACCUM_BIN): $(TEST_PTRACCUM_OBJ)
	$(CC) $(TEST_PTRACCUM_OBJ) -o $@ $(LDFLAGS)

# LANE 9c: composite row rule differential check (host dial vs PS2 dial).
TEST_FUSEDROW_SRCS = verify/tests/test_ps2_fused_row.c
TEST_FUSEDROW_OBJS = $(TEST_FUSEDROW_SRCS:.c=.test.o)
TEST_FUSEDROW_BIN  = ./.build/test_ps2_fused_row
TEST_FUSEDROW_DIAL_OBJ = verify/tests/test_ps2_fused_row.ps2dial.o
TEST_FUSEDROW_DIAL_BIN = ./.build/test_ps2_fused_row_ps2dial

test-ps2-fused-row: $(TEST_FUSEDROW_BIN) $(TEST_FUSEDROW_DIAL_BIN)
	@echo "=========================================================="
	@echo " Running the composite row rule -- host dial, then the PS2 dial..."
	@echo "=========================================================="
	@./$(TEST_FUSEDROW_BIN)
	@./$(TEST_FUSEDROW_DIAL_BIN)

$(TEST_FUSEDROW_BIN): $(TEST_FUSEDROW_OBJS) include/btron/fused_row.h include/btron/fast_blit.h
	@mkdir -p ./.build
	$(CC) $(TEST_FUSEDROW_OBJS) -o $@ $(LDFLAGS)

# PS2 target macro build of the fused row test.
$(TEST_FUSEDROW_DIAL_OBJ): verify/tests/test_ps2_fused_row.c include/btron/fused_row.h
	$(CC) $(CFLAGS) -DBTRON_PS2_TARGET -MMD -MP -c $< -o $@

$(TEST_FUSEDROW_DIAL_BIN): $(TEST_FUSEDROW_DIAL_OBJ) include/btron/fused_row.h
	@mkdir -p ./.build
	$(CC) $(TEST_FUSEDROW_DIAL_OBJ) -o $@ $(LDFLAGS)

# Increment 5a: EE/GS raster stage primitive dispatch and vertex color channel ordering.
TEST_EEGSR_SRCS = verify/tests/test_eegs_raster.c
TEST_EEGSR_OBJS = $(TEST_EEGSR_SRCS:.c=.test.o)
TEST_EEGSR_BIN  = ./.build/test_eegs_raster

test-eegs-raster: $(TEST_EEGSR_BIN)
	@echo "=========================================================="
	@echo " Running PS2 EE/GS raster stage vs hand-built GIF groups..."
	@echo "=========================================================="
	@./$(TEST_EEGSR_BIN)

$(TEST_EEGSR_BIN): $(TEST_EEGSR_OBJS) src/desktop/graphics/eegs/backend_eegs.c src/desktop/graphics/eegs/backend_eegs.h \
                   src/drivers/playstation2/ps2_gs_rec.c src/drivers/playstation2/ps2_gs_pack.c \
                   src/drivers/playstation2/ps2_gs_rec.h src/drivers/playstation2/ps2_gs_pack.h \
                   verify/tests/gs_group_view.h
	@mkdir -p ./.build
	$(CC) $(TEST_EEGSR_OBJS) -o $@ $(LDFLAGS)

TEST_EEGSG_SRCS = verify/tests/test_eegs_geom.c src/desktop/graphics/eegs/eegs_geom.c \
                  src/desktop/graphics/eegs/backend_eegs.c \
                  src/drivers/playstation2/ps2_gs_rec.c src/drivers/playstation2/ps2_gs_pack.c
TEST_EEGSG_OBJS = $(TEST_EEGSG_SRCS:.c=.test.o)
TEST_EEGSG_BIN  = ./.build/test_eegs_geom

test-eegs-geom: $(TEST_EEGSG_BIN)
	@echo "=========================================================="
	@echo " Running EEGS shared geom (clip, viewport, 2D submit)..."
	@echo "=========================================================="
	@./$(TEST_EEGSG_BIN)

$(TEST_EEGSG_BIN): $(TEST_EEGSG_OBJS) src/desktop/graphics/eegs/eegs_geom.h src/desktop/graphics/eegs/backend_eegs.h
	@mkdir -p ./.build
	$(CC) $(TEST_EEGSG_OBJS) -o $@ $(LDFLAGS) -lm

# LANE 12f gate (a): batch grid map projection bit-identical check vs TinyGL vertex walk.
TEST_GRIDMAP_SRCS = verify/tests/test_grid_map.c src/desktop/graphics/gles/gl_dispatch.c src/desktop/graphics/gles/backend_tinygl.c \
                    src/desktop/graphics/virgl/backend_virgl.c $(TINYGL_SRCS) src/media/xmb/xmb_wave.c
TEST_GRIDMAP_OBJS = $(TEST_GRIDMAP_SRCS:.c=.gm.o)
TEST_GRIDMAP_BIN  = ./.build/test_grid_map

%.gm.o: %.c
	$(CC) $(CFLAGS) -ffp-contract=off -DBTRON_GL_BACKEND_TINYGL -MMD -MP -c $< -o $@

test-grid-map: $(TEST_GRIDMAP_BIN)
	@echo "=========================================================="
	@echo " Running LANE 12f gate (a): the batch's map vs the vertex walk itself..."
	@echo "=========================================================="
	@./$(TEST_GRIDMAP_BIN)

$(TEST_GRIDMAP_BIN): $(TEST_GRIDMAP_OBJS) src/desktop/graphics/gles/grid_map.h src/desktop/graphics/tinygl/vertex_stage.c
	@mkdir -p ./.build
	$(CC) $(TEST_GRIDMAP_OBJS) -o $@ $(LDFLAGS) -lm

# Increment 5c: EE/GS presentation ordering, upload vs stream staging, and capacity.
TEST_EEGSP_SRCS = verify/tests/test_eegs_present.c
TEST_EEGSP_OBJS = $(TEST_EEGSP_SRCS:.c=.test.o)
TEST_EEGSP_BIN  = ./.build/test_eegs_present

test-eegs-present: $(TEST_EEGSP_BIN)
	@echo "=========================================================="
	@echo " Running PS2 EE/GS present: order, staging, capacity..."
	@echo "=========================================================="
	@./$(TEST_EEGSP_BIN)

$(TEST_EEGSP_BIN): $(TEST_EEGSP_OBJS) src/desktop/graphics/eegs/eegs_present.c src/desktop/graphics/eegs/eegs_present.h \
                   src/desktop/graphics/eegs/backend_eegs.c src/desktop/graphics/eegs/backend_eegs.h \
                   src/drivers/playstation2/ps2_gs_rec.c src/drivers/playstation2/ps2_gs_pack.c \
                   src/drivers/playstation2/ps2_gs_env.c src/drivers/playstation2/ps2_gs_vram.c \
                   src/drivers/playstation2/ps2_gif_ring.c src/drivers/playstation2/ps2_gif_ring.h \
                   src/drivers/playstation2/ps2_gs.h verify/tests/gs_group_view.h
	@mkdir -p ./.build
	$(CC) $(TEST_EEGSP_OBJS) -o $@ $(LDFLAGS)

# LANE 10: hardware GIF DMA writer ring non-blocking queue capacity and completion tracking.
TEST_GIFRING_SRCS = verify/tests/test_ps2_gif_ring.c
TEST_GIFRING_OBJS = $(TEST_GIFRING_SRCS:.c=.test.o)
TEST_GIFRING_BIN  = ./.build/test_ps2_gif_ring

test-ps2-gif-ring: $(TEST_GIFRING_BIN)
	@echo "=========================================================="
	@echo " Running PS2 LANE 10: the GIF writer ring..."
	@echo "=========================================================="
	@./$(TEST_GIFRING_BIN)

$(TEST_GIFRING_BIN): $(TEST_GIFRING_OBJS) src/drivers/playstation2/ps2_gif_ring.c \
                     src/drivers/playstation2/ps2_gif_ring.h
	@mkdir -p ./.build
	$(CC) $(TEST_GIFRING_OBJS) -o $@ $(LDFLAGS)

# Task #165: per-app paint budget allocation and window deduplication policy.
TEST_PAINTW_SRCS = verify/tests/test_ps2_paint_worker.c
TEST_PAINTW_OBJS = $(TEST_PAINTW_SRCS:.c=.test.o)
TEST_PAINTW_BIN  = ./.build/test_ps2_paint_worker

test-ps2-paintw: $(TEST_PAINTW_BIN)
	@echo "=========================================================="
	@echo " Running PS2 #165: the per-app paint budget and dedup..."
	@echo "=========================================================="
	@./$(TEST_PAINTW_BIN)

$(TEST_PAINTW_BIN): $(TEST_PAINTW_OBJS) src/drivers/playstation2/ps2_paint_worker.c \
                    src/drivers/playstation2/ps2_paint_worker.h
	@mkdir -p ./.build
	$(CC) $(TEST_PAINTW_OBJS) -o $@ $(LDFLAGS)

# GL backend selection dial verification (Makefile PS2_GL= dial vs preprocessor rule).
TEST_GLBLANE_SRCS = verify/tests/test_gl_backend_choice.c
TEST_GLBLANE_BIN  = ./.build/test_gl_backend_choice
GLBLANE_CFLAGS    = $(CFLAGS) -DBTRON_GL_BACKEND_TINYGL

# Measuring TU compiled with standard flags.
%.blc.o: %.c
	@mkdir -p ./.build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

test-gl-backend-choice: $(TEST_GLBLANE_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GL backend selection (Makefile PS2_GL= dial vs rule)..."
	@echo "=========================================================="
	@./$(TEST_GLBLANE_BIN)

verify/tests/gl_backend_choice_plain_port.blc.o: verify/tests/gl_backend_choice_plain_port.c src/desktop/graphics/gles/gl_backend_choice.c src/desktop/graphics/gles/gl_backend_choice.h
	@mkdir -p ./.build
	$(CC) $(GLBLANE_CFLAGS) -MMD -MP -c $< -o $@

verify/tests/gl_backend_choice_virgl_port.blc.o: verify/tests/gl_backend_choice_virgl_port.c src/desktop/graphics/gles/gl_backend_choice.c src/desktop/graphics/gles/gl_backend_choice.h
	@mkdir -p ./.build
	$(CC) $(GLBLANE_CFLAGS) -DBTRON_GL_PREFER_VIRGL -MMD -MP -c $< -o $@

verify/tests/gl_backend_choice_eegs_port.blc.o: verify/tests/gl_backend_choice_eegs_port.c src/desktop/graphics/gles/gl_backend_choice.c src/desktop/graphics/gles/gl_backend_choice.h
	@mkdir -p ./.build
	$(CC) $(GLBLANE_CFLAGS) -DBTRON_GL_PREFER_EEGS -MMD -MP -c $< -o $@

# Plain port TU without BTRON_GL_BACKEND_TINYGL.
verify/tests/gl_backend_choice_nostgl_port.blc.o: verify/tests/gl_backend_choice_nostgl_port.c src/desktop/graphics/gles/gl_backend_choice.c src/desktop/graphics/gles/gl_backend_choice.h
	@mkdir -p ./.build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST_GLBLANE_BIN): $(TEST_GLBLANE_SRCS:.c=.blc.o) \
                     verify/tests/gl_backend_choice_plain_port.blc.o \
                     verify/tests/gl_backend_choice_virgl_port.blc.o \
                     verify/tests/gl_backend_choice_eegs_port.blc.o \
                     verify/tests/gl_backend_choice_nostgl_port.blc.o
	@mkdir -p ./.build
	$(CC) $^ -o $@ $(LDFLAGS)

# Freestanding single-precision math series verification against host libm.
TEST_GLMATH_BIN = ./.build/test_gl_math_float

# Port TU forcing freestanding series evaluation with disabled builtins.
GLMATH_PORTFLAGS = -O1 -std=c11 -Wall -Wextra -ffreestanding \
                   -U__x86_64__ -U__i386__ -Iinclude -Iinclude/rt -Iinclude/graphics \
                   -fno-builtin-sqrt -fno-builtin-sqrtf -fno-builtin-sin -fno-builtin-sinf \
                   -fno-builtin-cos -fno-builtin-cosf -fno-builtin-fabs -fno-builtin-fabsf \
                   -fno-builtin-floor -fno-builtin-floorf -fno-builtin-exp -fno-builtin-expf \
                   -fno-builtin-pow -fno-builtin-powf -fno-builtin-atan -fno-builtin-atanf \
                   -fno-builtin-atan2 -fno-builtin-atan2f -fno-builtin-fmod -fno-builtin-fmodf

verify/tests/gl_math_float_port.test.o: verify/tests/gl_math_float_port.c include/rt/math.h
	$(CC) $(GLMATH_PORTFLAGS) -MMD -MP -c $< -o $@

test-gl-math: $(TEST_GLMATH_BIN)
	@echo "=========================================================="
	@echo " Running gl/math.h float-native series vs the host libm..."
	@echo "=========================================================="
	@./$(TEST_GLMATH_BIN)

$(TEST_GLMATH_BIN): verify/tests/test_gl_math_float.test.o verify/tests/gl_math_float_port.test.o
	@mkdir -p ./.build
	$(CC) $^ -o $@ $(LDFLAGS) -lm


# ═══════════════════════════════════════════════════════════════════
# PS2 canvas byte order -- one scene, both PIXEL domains, twice built
# ═══════════════════════════════════════════════════════════════════
# PS2 canvas byte order verification: COLOR-order build vs canvas-order build.
TEST_FBORDER_SRCS = verify/tests/test_fb_order.c src/desktop/graphics/dp_core.c \
                    src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c \
                    src/desktop/graphics/gles/gl_dispatch.c src/desktop/graphics/virgl/backend_virgl.c \
                    src/desktop/graphics/gles/gl_backend_choice.c \
                    src/desktop/graphics/eegs/backend_eegs.c \
                    src/drivers/playstation2/ps2_gs_rec.c src/drivers/playstation2/ps2_gs_pack.c \
                    src/drivers/playstation2/ps2_gs_tex.c src/drivers/playstation2/ps2_gs_vram.c \
                    $(TINYGL_SRCS)
TEST_FBORDER_COBJS = $(TEST_FBORDER_SRCS:.c=.fbo.o)
TEST_FBORDER_POBJS = $(TEST_FBORDER_SRCS:.c=.fbp.o)
TEST_FBORDER_CBIN  = ./.build/test_fb_order
TEST_FBORDER_PBIN  = ./.build/test_fb_order_canvas

%.fbo.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Target build with BTRON_PS2_TARGET for canvas byte order comparison.
%.fbp.o: %.c
	$(CC) $(CFLAGS) -DBTRON_PS2_TARGET -MMD -MP -c $< -o $@

test-ps2-fb-order: $(TEST_FBORDER_CBIN) $(TEST_FBORDER_PBIN)
	@echo "=========================================================="
	@echo " PS2 canvas byte order: COLOR-order build vs canvas-order build..."
	@echo "=========================================================="
	@mkdir -p ./.build
	@./$(TEST_FBORDER_CBIN) ./.build/fb_order_color.u32
	@./$(TEST_FBORDER_PBIN)  ./.build/fb_order_canvas.u32
	@if cmp -s ./.build/fb_order_color.u32 ./.build/fb_order_canvas.u32; then \
		echo "[FBORDER] PASS: both domains normalise to the same 31,488 words"; \
	else \
		echo "[FBORDER] FAIL: a word of the canvas-order build is not BTRON_PIXEL_FB()"; \
		echo "[FBORDER]        of the same word in the COLOR-order build.  The byte offset"; \
		echo "[FBORDER]        (1-based, from cmp -l below) / 4 gives the word index:"; \
		echo "[FBORDER]        0..19199 the 160x120 display-primitive canvas, 19200..25343"; \
		echo "[FBORDER]        the clear-only GL canvas, 25344..31487 the GL frame."; \
		cmp -l ./.build/fb_order_color.u32 ./.build/fb_order_canvas.u32 | head -24; \
		exit 1; \
	fi

$(TEST_FBORDER_CBIN): $(TEST_FBORDER_COBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_FBORDER_COBJS) -o $@ $(LDFLAGS) -lm

$(TEST_FBORDER_PBIN): $(TEST_FBORDER_POBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_FBORDER_POBJS) -o $@ $(LDFLAGS) -lm

-include $(TEST_FBORDER_COBJS:.o=.d) $(TEST_FBORDER_POBJS:.o=.d)


# ═══════════════════════════════════════════════════════════════════
# Bare-Metal MIPS Malta / Magnum Kernel (mips / QEMU) [Target 9]
# ═══════════════════════════════════════════════════════════════════
MIPS_TARGET     = ./.build/btron-mips.elf
MIPS_LD_SCRIPT  = src/drivers/mips/mips_qemu.ld
MIPS_CFLAGS     = -O2 -Wall -Wextra -std=c99 -ffreestanding -nostdlib \
                  -DBTRON_TARGET=9 -DBTRON_MIPS_TARGET \
                  -Isrc/drivers/mips $(COMMON_INCLUDES)
MIPS_STARTUP    = src/cores/core_mips.c
MIPS_SRCS       = $(MIPS_STARTUP)             \
                  src/drivers/mips/mips_uart.c \
                  src/kernel/libstr.c
MIPS_OBJS       = src/drivers/mips/boot_mips.mips.o $(MIPS_SRCS:.c=.mips.o)

%.mips.o: %.s
	$(MIPS_CC) -c $< -o $@

%.mips.o: %.c
	$(MIPS_CC) $(MIPS_CFLAGS) -c $< -o $@

mips: $(MIPS_TARGET)

$(MIPS_TARGET): $(MIPS_OBJS) $(MIPS_LD_SCRIPT)
	@echo "=========================================================="
	@echo " Building B-System MIPS Kernel: $@"
	@echo "=========================================================="
	$(MIPS_LD) -T $(MIPS_LD_SCRIPT) $(MIPS_OBJS) -o $@
	@ln -sf $@ btron-mips.elf
	@echo "[MIPS-ELF] Built: $@"
	@file $@

run-mips: $(MIPS_TARGET)
	@echo "=========================================================="
	@echo " Launching B-System MIPS Kernel on QEMU"
	@echo " Machine  : MIPS Malta Core LV (-M malta)"
	@echo " CPU      : MIPS 24Kf / 5KEc (Little-Endian)"
	@echo " RAM      : 256 MB (KSEG0 Mapped)"
	@echo " Console  : 16550 UART COM1 @ 0x180003F8 (stdio)"
	@echo "=========================================================="
	@if [ "$(MAGNUM)" = "1" ] && [ -f ntprom.raw ]; then \
	    echo "[QEMU] Booting MIPS Magnum (-M magnum) with ntprom.raw BIOS..."; \
	    $(QEMU_MIPS64) -M magnum -bios ./ntprom.raw -m 64M; \
	elif command -v $(QEMU_MIPS) >/dev/null 2>&1; then \
	    $(QEMU_MIPS) -M malta -cpu 24Kf -m 256M -kernel $(MIPS_TARGET) -nographic -monitor none; \
	elif command -v $(QEMU_MIPS64) >/dev/null 2>&1; then \
	    $(QEMU_MIPS64) -M malta -cpu 5KEc -m 256M -kernel $(MIPS_TARGET) -nographic -monitor none; \
	else \
	    echo "[ERROR] Neither $(QEMU_MIPS) nor $(QEMU_MIPS64) found"; \
	    exit 1; \
	fi

test-mips: $(MIPS_TARGET)
	@echo "=========================================================="
	@echo " Testing B-System MIPS Kernel on QEMU Malta (Headless CI)"
	@echo "=========================================================="
	@bash scripts/test_mips.sh

# ═══════════════════════════════════════════════════════════════════
# Bare-Metal ARM32 ELF — BCM283x Pi 2B (Cortex-A7 / ARMv7 / BCM2836)
# ═══════════════════════════════════════════════════════════════════
arm-elf: tad_bin $(ARM32_TARGET)

%.arm32.o: %.c
	$(ARM32_CC) $(ARM_CFLAGS) -c $< -o $@

$(ARM32_TARGET): $(ARM32_OBJS) $(BAREMETAL_LD)
	@echo "=========================================================="
	@echo " Building ARM32 ELF — BCM283x Pi 2B (Cortex-A7, ARMv7)"
	@echo " Startup: $(BAREMETAL_STARTUP)"
	@echo "=========================================================="
	$(ARM32_CC) $(ARM_CFLAGS) -Wl,-T,$(BAREMETAL_LD) $(ARM32_OBJS) -o $@
	@echo "[ARM-ELF] Built: $@"
	@file $@

# ═══════════════════════════════════════════════════════════════════
# Bare-Metal AArch64 ELF — Pi 4B (Cortex-A72 / BCM2711)
# ═══════════════════════════════════════════════════════════════════
arm64-elf: tad_bin $(ARM64_TARGET)

%.arm64.o: %.c
	$(ARM64_CC) $(ARM64_CFLAGS) -c $< -o $@

$(ARM64_TARGET): $(ARM64_OBJS) $(BAREMETAL_LD)
	@echo "=========================================================="
	@echo " Building AArch64 ELF — Pi 4B (Cortex-A72, BCM2711)"
	@echo " Startup: $(BAREMETAL_STARTUP)"
	@echo "=========================================================="
	$(ARM64_CC) $(ARM64_CFLAGS) -Wl,-T,$(BAREMETAL_LD) $(ARM64_OBJS) -o $@
	@echo "[ARM64-ELF] Built: $@"
	@file $@

# ═══════════════════════════════════════════════════════════════════
# Raspberry Pi 400 — One-Shot SD Card Image & Flash
# Target 6 (aarch64-bcm2711) Takanori Yokoyama — T-Kernel 2.0
#
# Usage:
#   make pi400              — build btron-pi400.img (64 MiB FAT32)
#   make flash-pi400        — auto-detect SD card & flash with dd
#   make flash-pi400 PI400_SDCARD=/dev/diskN  — explicit device
#   make fetch-pi400-fw     — download start4.elf + fixup4.dat
# ═══════════════════════════════════════════════════════════════════
PI400_IMG      = btron-pi400.img
PI400_IMG_SIZE = 192
PI400_FW_DIR   = third_party/pi400
PI400_CFG_DIR  = src/drivers/broadcom/pi400
PI400_PAYLOAD  = .build/pi400/btron_payload.bin
PI400_KERNEL8  = .build/pi400/kernel8.img

LLVM_OBJCOPY  ?= $(shell for p in \
    /opt/homebrew/opt/llvm/bin/llvm-objcopy \
    /usr/local/opt/llvm/bin/llvm-objcopy \
    llvm-objcopy; do \
    if command -v "$$p" >/dev/null 2>&1; then echo "$$p"; break; fi; done)

# Auto-detect the SD card that has a FAT32 volume named PiBoot (or BTRON3PI4).
# diskutil list -plist + grep is portable on any macOS version.
# Produces e.g. /dev/disk16  (parent disk, not the partition slice).
PI400_SDCARD ?= $(shell diskutil list | \
    awk '/PiBoot|BTRON3PI4/{print $$NF}' | \
    sed 's/s[0-9]*$$//' | head -1 | \
    sed 's|^|/dev/|')

# Resolve firmware paths — handle both upper and lower case (RISC OS copies uppercase)
PI400_START4  = $(firstword $(wildcard $(PI400_FW_DIR)/start4.elf $(PI400_FW_DIR)/START4.ELF))
PI400_FIXUP4  = $(firstword $(wildcard $(PI400_FW_DIR)/fixup4.dat $(PI400_FW_DIR)/fixup4.dat))

pi400: $(PI400_IMG)

# EL3 armstub: runs at Secure EL3 before the kernel to set SCR_EL3 (no IRQ/FIQ
# trap) and GICD_IGROUPR (Group 1) — both impossible from Non-secure EL1.
PI400_ARMSTUB_BIN = .build/pi400/armstub8.bin

$(PI400_ARMSTUB_BIN): src/drivers/broadcom/armstub8.S
	@mkdir -p .build/pi400
	$(ARM64_CC) -c $< -o .build/pi400/armstub8.o
	$(ARM64_CC) -Wl,-Ttext=0x0 -Wl,--image-base=0x0 -Wl,--build-id=none .build/pi400/armstub8.o -o .build/pi400/armstub8.elf
	$(LLVM_OBJCOPY) -O binary .build/pi400/armstub8.elf $@
	@SZ=$$(wc -c < $@ | tr -d ' '); echo "[PI400] armstub8.bin: $$SZ bytes (EL3, linked @0x0)"

# initramfs blob at 0x03000000.  A payload that stops after pak0 (~34 MiB) leaves
# /MSX unmapped at RAM 0x05400000 and yoko4 reports "flash btron_payload.bin".
$(PI400_PAYLOAD): btron_sys.vol btron_msx.vol scripts/tool_mk_pi400_payload.py
	@mkdir -p .build/pi400
	python3 scripts/tool_mk_pi400_payload.py $@

$(PI400_KERNEL8): $(ARM64_TARGET)
	@mkdir -p .build/pi400
	@if [ -z "$(LLVM_OBJCOPY)" ]; then \
	    echo "[ERROR] llvm-objcopy not found. Install: brew install llvm"; exit 1; fi
	$(LLVM_OBJCOPY) -O binary $(ARM64_TARGET) $@
	@SZ=$$(wc -c < $@ | tr -d ' '); \
	 echo "[PI400] kernel8.img: $$SZ bytes ($$(( $$SZ / 1024 )) KiB)"

pi400-payload: $(PI400_PAYLOAD) $(PI400_KERNEL8)

$(PI400_IMG): arm64-elf $(PI400_ARMSTUB_BIN) $(PI400_PAYLOAD) $(PI400_KERNEL8) btron_sys.vol btron_msx.vol
	@echo "=========================================================="
	@echo " BTRON Pi 400 — SD Card Image Builder"
	@echo " ELF     : $(ARM64_TARGET)"
	@echo " Image   : $(PI400_IMG)  ($(PI400_IMG_SIZE) MiB FAT32)"
	@echo "=========================================================="
	# ── 1. Verify llvm-objcopy available ──────────────────────────
	@if [ -z "$(LLVM_OBJCOPY)" ]; then \
	    echo "[ERROR] llvm-objcopy not found. Install: brew install llvm"; exit 1; fi
	# ── 2. Verify firmware blobs (auto-copy from /Volumes/PiBoot) ─
	@if [ -z "$(PI400_START4)" ] || [ -z "$(PI400_FIXUP4)" ]; then \
	    SRC=$$(ls /Volumes/PiBoot/START4.ELF /Volumes/PiBoot/start4.elf 2>/dev/null | head -1); \
	    if [ -n "$$SRC" ]; then \
	        echo "[PI400] Auto-copying firmware from /Volumes/PiBoot..."; \
	        mkdir -p $(PI400_FW_DIR); \
	        cp /Volumes/PiBoot/START4.ELF  $(PI400_FW_DIR)/START4.ELF  2>/dev/null || \
	        cp /Volumes/PiBoot/start4.elf  $(PI400_FW_DIR)/start4.elf; \
	        cp /Volumes/PiBoot/fixup4.dat  $(PI400_FW_DIR)/fixup4.dat; \
	    else \
	        echo "[ERROR] GPU firmware not found in $(PI400_FW_DIR)/ or /Volumes/PiBoot/"; \
	        echo "  Run: make fetch-pi400-fw   OR  copy start4.elf + fixup4.dat manually."; \
	        exit 1; \
	    fi; \
	fi
	# ── 3. kernel8.img + payload come from $(PI400_KERNEL8) / $(PI400_PAYLOAD)
	# ── 4. Create FAT32 disk image via hdiutil (macOS native) ─────
	@rm -f $(PI400_IMG) .build/pi400/pi400_work.dmg
	hdiutil create -size $(PI400_IMG_SIZE)m -fs MS-DOS -volname BTRON3PI4 \
	    -layout NONE -type UDIF .build/pi400/pi400_work >/dev/null 2>&1 || \
	hdiutil create -size $(PI400_IMG_SIZE)m -fs MS-DOS -volname BTRON3PI4 \
	    .build/pi400/pi400_work >/dev/null
	@echo "[PI400] Created $(PI400_IMG_SIZE) MiB FAT32 image."
	# ── 5. Mount, copy files, and detach ──────────────────────────
	@MOUNT_PT=$$(mktemp -d /tmp/btron_pi400_XXXXXX); \
	DEV=$$(hdiutil attach .build/pi400/pi400_work.dmg \
	        -mountpoint $$MOUNT_PT -nobrowse 2>/dev/null | \
	        grep '/dev/disk' | awk '{print $$1}' | head -1); \
	echo "[PI400] Attached $$DEV → $$MOUNT_PT"; \
	cp $(PI400_START4)                  $$MOUNT_PT/start4.elf; \
	cp $(PI400_FW_DIR)/fixup4.dat       $$MOUNT_PT/fixup4.dat; \
	cp $(PI400_FW_DIR)/bcm2711-rpi-400.dtb $$MOUNT_PT/bcm2711-rpi-400.dtb 2>/dev/null || true; \
	cp $(PI400_FW_DIR)/bcm2711-rpi-4-b.dtb   $$MOUNT_PT/bcm2711-rpi-4-b.dtb 2>/dev/null || true; \
	cp $(PI400_CFG_DIR)/config.txt      $$MOUNT_PT/config.txt; \
	cp $(PI400_CFG_DIR)/cmdline.txt     $$MOUNT_PT/cmdline.txt; \
	cp $(PI400_KERNEL8)                 $$MOUNT_PT/kernel8.img; \
	cp .build/pi400/armstub8.bin        $$MOUNT_PT/armstub8.bin; \
	cp btron_sys.vol                    $$MOUNT_PT/btron_sys.vol; \
	if [ -f btron_anders.vol ]; then cp btron_anders.vol $$MOUNT_PT/btron_anders.vol; fi; \
	cp btron_msx.vol                    $$MOUNT_PT/btron_msx.vol; \
	mkdir -p $$MOUNT_PT/msx; \
	cp -f assets/msx/*.rom assets/msx/*.ROM assets/msx/*.dsk assets/msx/*.DSK $$MOUNT_PT/msx/ 2>/dev/null || true; \
	cp $(PI400_PAYLOAD)                 $$MOUNT_PT/btron_payload.bin; \
	if [ -f assets/quake/pak/pak0.pak ]; then cp assets/quake/pak/pak0.pak $$MOUNT_PT/pak0.pak; fi; \
	echo "[PI400] SD image contents:"; \
	ls -lh $$MOUNT_PT/; \
	sync; \
	hdiutil detach $$DEV -force -quiet; \
	rmdir $$MOUNT_PT 2>/dev/null || true
	# ── 6. Convert UDIF .dmg → flat raw .img (dd-flashable) ───────
	@sleep 1
	hdiutil convert .build/pi400/pi400_work.dmg \
	    -format UDTO -o .build/pi400/pi400_raw >/dev/null 2>&1 || \
	hdiutil convert .build/pi400/pi400_work.dmg \
	    -format UDTO -o .build/pi400/pi400_raw
	@mv .build/pi400/pi400_raw.cdr $(PI400_IMG)
	@rm -f .build/pi400/pi400_work.dmg
	@echo "=========================================================="
	@echo " BTRON Pi 400 image ready: $(PI400_IMG)"
	@echo " Flash  : make flash-pi400"
	@echo " Device : $(PI400_SDCARD)  (auto-detected via 'PiBoot' label)"
	@echo "=========================================================="

fetch-pi400-fw:
	@echo "[PI400] Fetching GPU firmware from raspberrypi/firmware (master)..."
	@mkdir -p $(PI400_FW_DIR)
	curl -L --progress-bar \
	    -o $(PI400_FW_DIR)/start4.elf \
	    https://github.com/raspberrypi/firmware/raw/master/boot/start4.elf
	curl -L --progress-bar \
	    -o $(PI400_FW_DIR)/fixup4.dat \
	    https://github.com/raspberrypi/firmware/raw/master/boot/fixup4.dat
	@echo "[PI400] Firmware saved to $(PI400_FW_DIR)/"
	@ls -lh $(PI400_FW_DIR)/start4.elf $(PI400_FW_DIR)/fixup4.dat

flash-pi400: $(PI400_IMG)
	@echo "=========================================================="
	@echo " BTRON Pi 400 — Flash SD Card"
	@echo " Image  : $(PI400_IMG)"
	@echo " Device : $(PI400_SDCARD)"
	@echo "=========================================================="
	@if [ -z "$(PI400_SDCARD)" ]; then \
	    echo "[ERROR] Could not auto-detect SD card."; \
	    echo "  Plug in the card and run:"; \
	    echo "    make flash-pi400 PI400_SDCARD=/dev/diskN"; \
	    echo "  (find N with: diskutil list | grep -E 'FAT|PiBoot')"; \
	    exit 1; \
	fi
	@if [ ! -b "$(PI400_SDCARD)" ] && [ ! -c "$(PI400_SDCARD)" ]; then \
	    echo "[ERROR] Device $(PI400_SDCARD) does not exist."; \
	    exit 1; \
	fi
	@echo "[PI400] Unmounting all partitions on $(PI400_SDCARD)..."
	@diskutil unmountDisk $(PI400_SDCARD) || true
	@echo "[PI400] Writing image (sudo dd — enter macOS password if prompted):"
	sudo dd if=$(PI400_IMG) of=$(PI400_SDCARD) bs=4m conv=sync status=progress
	@echo "[PI400] Sync & eject..."
	@sync
	@diskutil eject $(PI400_SDCARD) || true
	@echo "=========================================================="
	@echo " SD card flashed! Safely remove, insert into Pi 400 & power on."
	@echo "  Serial debug (GPIO 14/15, 115200 baud) — optional USB-UART adapter."
	@echo "=========================================================="

# ═══════════════════════════════════════════════════════════════════
# Runs B-TRON on Raspberry Pi 2B (BCM2836, Cortex-A7, ARMv7 32-bit).
# Supports both qemu-system-arm and qemu-system-aarch64.
# ═══════════════════════════════════════════════════════════════════
run-kernel: $(ARM32_TARGET)
	@echo "=========================================================="
	@echo " Launching T-Kernel on QEMU Raspberry Pi 2B (BCM2836)"
	@echo " Machine : raspi2b  |  CPU: Cortex-A7  |  RAM: 1G"
	@echo " ELF     : $(ARM32_TARGET)"
	@echo " Devices : USB Keyboard, USB Mouse & VideoCore GPU Display"
	@echo "=========================================================="
	@echo " INPUT CAPTURE:"
	@echo "   Click inside the QEMU window to grab keyboard & mouse."
	@echo "   Press Ctrl+Alt+G (macOS: Ctrl+Option+G) to release grab."
	@echo "   Serial/UART input also works in THIS terminal window."
	@echo "=========================================================="
	@if command -v $(QEMU_AARCH64) >/dev/null 2>&1; then \
	    $(QEMU_AARCH64) -M raspi2b -m 1G $(KERNEL_DISPLAY) \
	        -device usb-kbd -device usb-mouse \
	        -kernel $(ARM32_TARGET) -serial stdio; \
	elif command -v $(QEMU_ARM) >/dev/null 2>&1; then \
	    $(QEMU_ARM) -M raspi2b -m 1G $(KERNEL_DISPLAY) \
	        -device usb-kbd -device usb-mouse \
	        -kernel $(ARM32_TARGET) -serial stdio; \
	else \
	    echo "[ERROR] QEMU not found — install qemu-system-aarch64 or qemu-system-arm"; \
	    exit 1; \
	fi

run-yoko: $(ARM64_TARGET)
	@echo "=========================================================="
	@echo " Launching T-Kernel AArch64 on QEMU Raspberry Pi 3B (BCM2837)"
	@echo " Honoring : Takanori Yokoyama (横山 孝徳) — T-Kernel Pioneer"
	@echo " Machine  : raspi3b  |  CPU: Cortex-A53 / AArch64  |  RAM: 1G"
	@echo " ELF      : $(ARM64_TARGET)"
	@echo " Devices  : USB Keyboard, USB Mouse & VideoCore GPU Display"
	@echo "=========================================================="
	@echo " INPUT CAPTURE:"
	@echo "   Click inside the QEMU window to grab keyboard & mouse."
	@echo "   Press Ctrl+Alt+G (macOS: Ctrl+Option+G) to release grab."
	@echo "   Serial/UART input also works in THIS terminal window."
	@echo "=========================================================="
	@if command -v $(QEMU_AARCH64) >/dev/null 2>&1; then \
	    $(QEMU_AARCH64) -M raspi3b -m 1G $(KERNEL_DISPLAY) \
	        -device usb-kbd -device usb-mouse \
	        -kernel $(ARM64_TARGET) -serial stdio; \
	else \
	    echo "[ERROR] $(QEMU_AARCH64) not found — install qemu-system-aarch64"; \
	    exit 1; \
	fi

run-yoko4: $(ARM64_TARGET) btron_sys.vol btron_anders.vol btron_msx.vol
	@echo "=========================================================="
	@echo " Launching T-Kernel AArch64 on QEMU Raspberry Pi 4B (BCM2711)"
	@echo " Honoring : Takanori Yokoyama (横山 孝徳) — T-Kernel Pioneer"
	@echo " Machine  : raspi4b  |  CPU: Cortex-A72 / AArch64  |  RAM: 2G"
	@echo " ELF      : $(ARM64_TARGET)"
	@echo " Volumes  : btron_sys.vol (@0x03000000), btron_anders.vol (@0x03200000), btron_msx.vol (@0x05400000)"
	@echo " Devices  : USB Keyboard, USB Mouse, USB DAC & VideoCore GPU Display"
	@echo "=========================================================="
	@echo " INPUT CAPTURE:"
	@echo "   Click inside the QEMU window to grab keyboard & mouse."
	@echo "   Press Ctrl+Alt+G (macOS: Ctrl+Option+G) to release grab."
	@echo "   Serial/UART input also works in THIS terminal window."
	@echo "=========================================================="
	@if command -v $(QEMU_AARCH64) >/dev/null 2>&1; then \
	    $(QEMU_AARCH64) -M raspi4b -m 2G $(KERNEL_DISPLAY) \
	        -device loader,file=btron_sys.vol,addr=0x03000000 \
	        -device loader,file=btron_anders.vol,addr=0x03200000 \
	        -device loader,file=btron_msx.vol,addr=0x05400000 \
	        $$(test -f assets/quake/pak/pak0.pak && echo "-device loader,file=assets/quake/pak/pak0.pak,addr=0x04000000") \
	        -device usb-kbd -device usb-mouse \
	        $$(if [ "$$(uname -s)" = Darwin ]; then echo "-audiodev coreaudio,id=snd0,out.frequency=48000,out.channels=2,out.format=s16 -device usb-audio,audiodev=snd0,buffer=12288"; fi) \
	        -kernel $(ARM64_TARGET) -serial stdio; \
	else \
	    echo "[ERROR] $(QEMU_AARCH64) not found — install qemu-system-aarch64"; \
	    exit 1; \
	fi

test-kernel: $(ARM32_TARGET)
	@echo "=========================================================="
	@echo " Testing T-Kernel on QEMU Raspberry Pi 2B (BCM2836)"
	@echo " Machine : raspi2b  |  CPU: Cortex-A7  |  RAM: 1G"
	@echo " Mode    : automated CI test (headless, serial validation)"
	@echo "=========================================================="
	@bash scripts/test_tkernel.sh

test-yoko: $(ARM64_TARGET)
	@echo "=========================================================="
	@echo " Testing T-Kernel on QEMU Raspberry Pi 3B (BCM2837)"
	@echo " Machine : raspi3b  |  CPU: Cortex-A53 / AArch64  |  RAM: 1G"
	@echo " Mode    : automated CI test (headless, serial validation)"
	@echo "=========================================================="
	@bash scripts/test_arm64.sh

test-yoko4: $(ARM64_TARGET)
	@echo "=========================================================="
	@echo " Testing T-Kernel on QEMU Raspberry Pi 4B (BCM2711)"
	@echo " Machine : raspi4b  |  CPU: Cortex-A72 / AArch64  |  RAM: 2G"
	@echo " Mode    : automated CI test (headless, serial validation)"
	@echo "=========================================================="
	@bash scripts/test_arm64_rpi4.sh

# ═══════════════════════════════════════════════════════════════════
# Debug
# ═══════════════════════════════════════════════════════════════════
debug-virtio: $(ARM64_TARGET)
	$(QEMU_AARCH64) -M raspi4b -m 2G -trace "virtio_*" \
	    -kernel $(ARM64_TARGET) -serial stdio

debug-gdb: $(ARM32_TARGET)
	@if command -v $(QEMU_ARM) >/dev/null 2>&1; then \
	    $(QEMU_ARM) -M raspi2b -m 1G -s -S -kernel $(ARM32_TARGET) -serial stdio; \
	elif command -v $(QEMU_AARCH64) >/dev/null 2>&1; then \
	    $(QEMU_AARCH64) -M raspi2b -m 1G -s -S -kernel $(ARM32_TARGET) -serial stdio; \
	fi

# ═══════════════════════════════════════════════════════════════════
# Mozc Kana-Kanji Conversion & TIP Unit Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_MOZC_SRCS = verify/tests/test_mozc.c src/desktop/tip/japanese/mozc_kkc.c src/desktop/tip/tip_ife.c src/desktop/tip/tibetan/wylie.c src/desktop/tip/tibetan/tibetan_dict.c \
                 src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c src/desktop/tip/tip_vobj.c src/desktop/window/wnd.c \
                 src/desktop/graphics/dp_core.c src/apps/chokanji/pmc.c
TEST_MOZC_OBJS = $(TEST_MOZC_SRCS:.c=.test.o)
TEST_MOZC_BIN  = ./.build/test_mozc

%.test.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -MMD -MP -c $< -o $@

test-mozc: $(TEST_MOZC_BIN)
	@echo "=========================================================="
	@echo " Running Mozc Kana-Kanji Conversion & TIP Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_MOZC_BIN)

$(TEST_MOZC_BIN): $(TEST_MOZC_OBJS)
	$(CC) $(TEST_MOZC_OBJS) -o $@ $(LDFLAGS)

# ═══════════════════════════════════════════════════════════════════
# Editor UI & Internal Functions Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_EDITOR_SRCS = verify/tests/test_editor_ui.c src/apps/editor/t_editor.c src/desktop/window/app_menu.c src/desktop/tip/japanese/mozc_kkc.c src/desktop/tip/tip_ife.c src/desktop/tip/tibetan/wylie.c src/desktop/tip/tibetan/tibetan_dict.c \
                   src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c src/desktop/tip/tip_vobj.c src/desktop/window/wnd.c \
                   src/desktop/graphics/dp_core.c src/apps/chokanji/pmc.c src/fs/blk_mem.c src/fs/blk_file.c src/fs/vol.c src/fs/file.c src/kernel/libstr.c
TEST_EDITOR_OBJS = $(TEST_EDITOR_SRCS:.c=.test.o)
TEST_EDITOR_BIN  = ./.build/test_editor

test-editor: $(TEST_EDITOR_BIN)
	@echo "=========================================================="
	@echo " Running B-System Editor UI & Internal Functions Tests..."
	@echo "=========================================================="
	@./$(TEST_EDITOR_BIN)

$(TEST_EDITOR_BIN): $(TEST_EDITOR_OBJS)
	$(CC) $(TEST_EDITOR_OBJS) -o $@ $(LDFLAGS)

# ═══════════════════════════════════════════════════════════════════
# TRON HMI Standard Library Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_HMI_SRCS = verify/tests/test_hmi.c src/desktop/industrial/hmi/hmi_core.c src/desktop/industrial/hmi/hmi_switch.c \
                src/desktop/industrial/hmi/hmi_selector.c src/desktop/industrial/hmi/hmi_volume.c src/desktop/industrial/hmi/hmi_meter.c \
                src/desktop/industrial/hmi/hmi_controller.c src/desktop/industrial/hmi/hmi_panel.c src/desktop/graphics/dp_core.c \
                src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c src/desktop/window/wnd.c src/apps/chokanji/pmc.c
TEST_HMI_OBJS = $(TEST_HMI_SRCS:.c=.test.o)
TEST_HMI_BIN  = ./.build/test_hmi

test-hmi: $(TEST_HMI_BIN)
	@echo "=========================================================="
	@echo " Running TRON HMI Standard Library Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_HMI_BIN)

$(TEST_HMI_BIN): $(TEST_HMI_OBJS)
	$(CC) $(TEST_HMI_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# TAD Unified Packing Pipeline — Python primary, Elixir fallback
# ═══════════════════════════════════════════════════════════════════
PYTHON ?= $(shell for py in python3 python3.14 python3.13 python3.12 python3.11 python3.10; do if command -v $$py >/dev/null 2>&1; then echo "$$py"; break; fi; done)

TAD_HTML2TAD := $(shell \
  if [ -n "$(PYTHON)" ]; then echo "$(PYTHON) scripts/tool_html2tad.py"; \
  elif command -v elixir >/dev/null 2>&1; then echo "elixir scripts/html2tad.exs"; fi)
TAD_BOOK2TAD := $(shell \
  if [ -n "$(PYTHON)" ]; then echo "$(PYTHON) scripts/tool_book2tad.py"; \
  elif command -v elixir >/dev/null 2>&1; then echo "elixir scripts/book2tad.exs"; fi)

tad_bin:
	@if [ -z "$(TAD_HTML2TAD)" ]; then \
	  echo "Note: neither python3 nor elixir found - tad_bin skipped (desktop still runs)."; \
	else $(TAD_HTML2TAD); $(TAD_BOOK2TAD); fi

html2tad:
	@$(TAD_HTML2TAD) --test
	@$(TAD_BOOK2TAD) --test

book2tad:
	@$(TAD_BOOK2TAD)

# ═══════════════════════════════════════════════════════════════════
# Native TAD Document Browser & Cabinet Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_TAD_SRCS = verify/tests/test_tad_browser.c src/apps/browser/tad_browser.c src/apps/cabinet/vobj_manager.c src/desktop/window/dnd.c src/desktop/window/app_menu.c \
                src/desktop/settings/appearance.c src/desktop/graphics/icons_bundle.c \
                src/desktop/tip/japanese/mozc_kkc.c src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c src/desktop/tip/tip_vobj.c \
                src/desktop/window/wnd.c src/desktop/graphics/dp_core.c src/apps/chokanji/pmc.c
TEST_TAD_OBJS = $(TEST_TAD_SRCS:.c=.test.o)
TEST_TAD_BIN  = ./.build/test_tad_browser

test-tad: $(TEST_TAD_BIN) tad_bin
	@echo "=========================================================="
	@echo " Running B-System Native TAD Browser & Cabinet Tests..."
	@echo "=========================================================="
	@./$(TEST_TAD_BIN)

$(TEST_TAD_BIN): $(TEST_TAD_OBJS)
	$(CC) $(TEST_TAD_OBJS) -o $@ $(LDFLAGS) -lz

# ═══════════════════════════════════════════════════════════════════
# BeOS Chat & TRON IPC Pub/Sub Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_CHAT_SRCS = verify/tests/test_chat.c src/apps/chat/chat.c src/apps/chat/chat_xml.c \
                 src/desktop/tip/japanese/mozc_kkc.c src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c \
                 src/desktop/window/wnd.c src/desktop/graphics/dp_core.c src/apps/chokanji/pmc.c
TEST_CHAT_OBJS = $(TEST_CHAT_SRCS:.c=.test.o)
TEST_CHAT_BIN  = ./.build/test_chat

test-chat: $(TEST_CHAT_BIN)
	@echo "=========================================================="
	@echo " Running B-System BeOS Chat (Blabber) & TRON IPC Tests..."
	@echo "=========================================================="
	@./$(TEST_CHAT_BIN)

$(TEST_CHAT_BIN): $(TEST_CHAT_OBJS)
	$(CC) $(TEST_CHAT_OBJS) -o $@ $(LDFLAGS) -lz

# ═══════════════════════════════════════════════════════════════════
# BTRON Deskbar Tracker & Task Manager Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_TRACKER_SRCS = verify/tests/test_tracker.c src/apps/workbench/tracker.c src/apps/workbench/desktop.c src/desktop/settings/appearance.c \
                    src/apps/cabinet/vobj.c src/apps/workbench/about.c src/desktop/window/wnd.c src/apps/chokanji/pmc.c \
                    src/desktop/window/app_menu.c \
                    src/desktop/graphics/dp_core.c src/desktop/graphics/icons_bundle.c src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c
TEST_TRACKER_OBJS = $(TEST_TRACKER_SRCS:.c=.test.o)
TEST_TRACKER_BIN  = ./.build/test_tracker

test-tracker: $(TEST_TRACKER_BIN)
	@echo "=========================================================="
	@echo " Running BTRON Deskbar Tracker Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_TRACKER_BIN)

$(TEST_TRACKER_BIN): $(TEST_TRACKER_OBJS)
	$(CC) $(TEST_TRACKER_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# Desktop Banded-Present Losslessness (the PS2 present's contract, headless)
# ═══════════════════════════════════════════════════════════════════
TEST_CLIP_SRCS = verify/tests/test_desktop_clip.c verify/tests/test_desktop_clip_count_port.c \
                 src/apps/workbench/tracker.c src/apps/workbench/desktop.c src/desktop/settings/appearance.c \
                 src/apps/cabinet/vobj.c src/apps/workbench/about.c src/desktop/window/wnd.c src/apps/chokanji/pmc.c \
                 src/desktop/window/app_menu.c src/apps/workbench/global_menu.c src/apps/workbench/workbench.c \
                 src/desktop/graphics/dp_core.c src/desktop/graphics/icons_bundle.c src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c
TEST_CLIP_OBJS = $(TEST_CLIP_SRCS:.c=.clip.o)
TEST_CLIP_BIN  = ./.build/test_desktop_clip
# LANE 12i rung (ii): banded presentation losslessness with counted tracker menu render.
verify/tests/test_desktop_clip.clip.o:             CFLAGS += -Dtracker_render_menu=counted_tracker_render_menu
src/apps/workbench/global_menu.clip.o:             CFLAGS += -Dtracker_render_menu=counted_tracker_render_menu
src/apps/workbench/workbench.clip.o:               CFLAGS += -Dtracker_render_menu=counted_tracker_render_menu
verify/tests/test_desktop_clip_count_port.clip.o:  CFLAGS += -Utracker_render_menu

%.clip.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -MMD -MP -c $< -o $@

test-deskclip: $(TEST_CLIP_BIN)
	@echo "=========================================================="
	@echo " Running Desktop Banded-Present Losslessness Tests..."
	@echo "=========================================================="
	@./$(TEST_CLIP_BIN)

$(TEST_CLIP_BIN): $(TEST_CLIP_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_CLIP_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# ASYNC Real-Time Input & Cursor Budget Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_ASYNC_INPUT_BIN = ./.build/test_async_input_budget
TEST_ASYNC_INPUT_SRCS = verify/tests/test_async_input_budget.c

$(TEST_ASYNC_INPUT_BIN): $(TEST_ASYNC_INPUT_SRCS)
	@mkdir -p ./.build
	$(CC) $(CFLAGS) $(TEST_ASYNC_INPUT_SRCS) -o $@ $(LDFLAGS) -lm

test-async-input: $(TEST_ASYNC_INPUT_BIN)
	@echo "=========================================================="
	@echo " Running ASYNC Real-Time Input & Cursor Budget Tests..."
	@echo "=========================================================="
	@./$(TEST_ASYNC_INPUT_BIN)

verify:
	@$(MAKE) -C verify run
# ═══════════════════════════════════════════════════════════════════
# Unified Test Suite Runner
# ═══════════════════════════════════════════════════════════════════

TEST_SETTINGS_BIN = ./.build/test_settings
TEST_SETTINGS_SRCS = verify/tests/test_language_settings.c \
                     src/desktop/settings/language.c \
                     src/desktop/settings/control_panel.c \
                     src/desktop/settings/appearance.c \
                     src/desktop/settings/desktop.c \
                     src/desktop/settings/display.c \
                     src/desktop/settings/input.c \
                     src/desktop/settings/sound.c \
                     src/desktop/settings/network.c \
                     src/desktop/settings/media.c \
                     src/desktop/settings/security.c \
                     src/desktop/settings/system.c \
                     src/desktop/settings/terminal.c \
                     src/desktop/tip/tip_ife.c \
                     src/desktop/tip/japanese/mozc_kkc.c \
                     src/desktop/tip/tibetan/wylie.c \
                     src/desktop/tip/tibetan/tibetan_dict.c \
                     src/desktop/font/troncode.c \
                     src/desktop/font/jis_fonts.c \
                     src/desktop/font/tibetan_fonts.c \
                     src/desktop/window/wnd.c \
                     src/apps/chokanji/pmc.c \
                     src/desktop/window/app_menu.c \
                     src/desktop/graphics/dp_core.c \
                     src/desktop/graphics/icons_bundle.c \
                     src/desktop/tip/tip_vobj.c

TEST_SETTINGS_OBJS = $(TEST_SETTINGS_SRCS:.c=.test.o)

$(TEST_SETTINGS_BIN): $(TEST_SETTINGS_OBJS)
	$(CC) $(TEST_SETTINGS_OBJS) -o $@ $(LDFLAGS)

test-settings: $(TEST_SETTINGS_BIN)
	@echo "=========================================================="
	@echo " Running Settings Cabinet: Language & IME Settings Tests..."
	@echo "=========================================================="
	./$(TEST_SETTINGS_BIN)

# ═══════════════════════════════════════════════════════════════════
# BTRON Global System Menu (Chokanji & Haiku) Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_GMENU_SRCS = verify/tests/test_global_menu.c src/apps/workbench/global_menu.c src/apps/workbench/tracker.c \
                  src/desktop/window/app_menu.c src/desktop/graphics/icons_bundle.c \
                  src/apps/workbench/about.c src/desktop/window/wnd.c src/apps/chokanji/pmc.c src/desktop/graphics/dp_core.c src/desktop/font/troncode.c \
                  src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c src/desktop/tip/tip_ife.c src/desktop/tip/japanese/mozc_kkc.c \
                  src/desktop/tip/tibetan/wylie.c src/desktop/tip/tibetan/tibetan_dict.c src/desktop/tip/tip_vobj.c
TEST_GMENU_OBJS = $(TEST_GMENU_SRCS:.c=.test.o)
TEST_GMENU_BIN  = ./.build/test_global_menu

test-global-menu: $(TEST_GMENU_BIN)
	@echo "=========================================================="
	@echo " Running BTRON Global System Menu & Japanese Deskbar Tests..."
	@echo "=========================================================="
	@./$(TEST_GMENU_BIN)

$(TEST_GMENU_BIN): $(TEST_GMENU_OBJS)
	$(CC) $(TEST_GMENU_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# XMB (XrossMediaBar) OpenGL Render Verification — pixels, not promises
# ═══════════════════════════════════════════════════════════════════
TEST_XMB_SRCS = verify/tests/test_xmb_render.c src/media/xmb/xmb.c src/media/xmb/xmb_wave.c \
                src/desktop/graphics/gles/gl_dispatch.c src/desktop/graphics/gles/egl_surface.c src/desktop/graphics/virgl/backend_virgl.c \
                src/desktop/graphics/gles/gl_backend_choice.c \
                src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c \
                src/apps/clu/vfs.c src/fs/blk_mem.c src/fs/blk_file.c \
                src/fs/vol.c src/fs/file.c src/kernel/libstr.c
TEST_XMB_OBJS = $(TEST_XMB_SRCS:.c=.test.o)
TEST_XMB_BIN  = ./.build/test_xmb_render

test-xmb-render: $(TEST_XMB_BIN)
	@echo "=========================================================="
	@echo " Running XMB OpenGL Render Verification..."
	@echo "=========================================================="
	@./$(TEST_XMB_BIN)

$(TEST_XMB_BIN): $(TEST_XMB_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_XMB_OBJS) -o $@ $(LDFLAGS) -lm

# XMB OpenGL render verification on PS2 TinyGL backend.
TEST_XMB_TGL_SRCS = verify/tests/test_xmb_render.c src/media/xmb/xmb.c src/media/xmb/xmb_wave.c \
                    src/desktop/graphics/gles/gl_dispatch.c src/desktop/graphics/gles/egl_surface.c \
                    src/desktop/graphics/gles/gl_backend_choice.c \
                    src/desktop/graphics/gles/backend_tinygl.c $(TINYGL_SRCS) src/desktop/graphics/virgl/backend_virgl.c \
                    src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c \
                    src/apps/clu/vfs.c src/fs/blk_mem.c src/fs/blk_file.c \
                    src/fs/vol.c src/fs/file.c src/kernel/libstr.c
TEST_XMB_TGL_OBJS = $(TEST_XMB_TGL_SRCS:.c=.tgl.o)
TEST_XMB_TGL_BIN  = ./.build/test_xmb_render_tgl

%.tgl.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_GL_BACKEND_TINYGL \
	      -DXMB_OUT_DIR='"./.build/tgl/"' -MMD -MP -c $< -o $@
test-xmb-render-tgl: $(TEST_XMB_TGL_BIN)
	@echo "=========================================================="
	@echo " Running XMB Render Verification on the PS2's TinyGL backend..."
	@echo "=========================================================="
	@mkdir -p ./.build/tgl
	@./$(TEST_XMB_TGL_BIN)

$(TEST_XMB_TGL_BIN): $(TEST_XMB_TGL_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_XMB_TGL_OBJS) -o $@ $(LDFLAGS) -lm

# LANE 24: TinyGL context isolation across multiple window surfaces.
TEST_GLCI_SRCS = verify/tests/test_gl_context_isolation.c \
                 src/desktop/graphics/gles/gl_dispatch.c src/desktop/graphics/gles/backend_tinygl.c src/desktop/graphics/virgl/backend_virgl.c \
                 $(TINYGL_SRCS)
TEST_GLCI_OBJS = $(TEST_GLCI_SRCS:.c=.ci.o)
TEST_GLCI_BIN  = ./.build/test_gl_context_isolation

%.ci.o: %.c
	$(CC) $(CFLAGS) -DBTRON_GL_BACKEND_TINYGL -MMD -MP -c $< -o $@

test-gl-context: $(TEST_GLCI_BIN)
	@echo "=========================================================="
	@echo " Running LANE 24 gate: one context, N window surfaces..."
	@echo "=========================================================="
	@./$(TEST_GLCI_BIN)

$(TEST_GLCI_BIN): $(TEST_GLCI_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_GLCI_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# XMB icon atlas: geometry on the host, sheet in the image
# ═══════════════════════════════════════════════════════════════════
# XMB icon atlas: geometry generation and static header validation.
GEN_XMB_ATLAS_OBJS = verify/tests/gen_xmb_atlas.test.o
GEN_XMB_ATLAS_BIN  = ./.build/gen_xmb_atlas

xmb-atlas: $(GEN_XMB_ATLAS_BIN)
	@$(GEN_XMB_ATLAS_BIN) > src/media/xmb/xmb_atlas.h.tmp || { rm -f src/media/xmb/xmb_atlas.h.tmp; exit 1; }
	@mv src/media/xmb/xmb_atlas.h.tmp src/media/xmb/xmb_atlas.h
	@echo "wrote src/media/xmb/xmb_atlas.h ($$(wc -c < src/media/xmb/xmb_atlas.h) bytes)"

test-xmb-atlas: $(GEN_XMB_ATLAS_BIN)
	@mkdir -p ./.build
	@$(GEN_XMB_ATLAS_BIN) > ./.build/xmb_atlas.h.new || { rm -f ./.build/xmb_atlas.h.new; exit 1; }
	@if cmp -s ./.build/xmb_atlas.h.new src/media/xmb/xmb_atlas.h; then \
	    echo "[PASS] src/media/xmb/xmb_atlas.h is still the sheet this geometry prints"; \
	 else \
	    echo "[FAIL] src/media/xmb/xmb_atlas.h has drifted from verify/tests/gen_xmb_atlas.c -- run make xmb-atlas"; \
	    rm -f ./.build/xmb_atlas.h.new; exit 1; fi
	@rm -f ./.build/xmb_atlas.h.new

# ═══════════════════════════════════════════════════════════════════
# Common Application Menu Subsystem Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_APP_MENU_SRCS = verify/tests/test_app_menu.c src/desktop/window/app_menu.c src/desktop/window/wnd.c src/apps/chokanji/pmc.c \
                     src/desktop/graphics/dp_core.c src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c
TEST_APP_MENU_OBJS = $(TEST_APP_MENU_SRCS:.c=.test.o)
TEST_APP_MENU_BIN  = ./.build/test_app_menu

test-app-menu: $(TEST_APP_MENU_BIN)
	@echo "=========================================================="
	@echo " Running Common Application Menu Subsystem Tests..."
	@echo "=========================================================="
	@./$(TEST_APP_MENU_BIN)

$(TEST_APP_MENU_BIN): $(TEST_APP_MENU_OBJS)
	$(CC) $(TEST_APP_MENU_OBJS) -o $@ $(LDFLAGS)

# ═══════════════════════════════════════════════════════════════════
# Review PDF & Marginalia Application Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_REVIEW_SRCS = verify/tests/test_review_ui.c \
                   src/apps/review/review.c \
                   src/apps/browser/pdf_reader.c \
                   src/desktop/font/schrift.c \
                   src/desktop/window/wnd.c \
                   src/desktop/window/app_menu.c \
                   src/desktop/window/event.c \
                   src/desktop/graphics/dp_core.c \
                   src/desktop/font/troncode.c \
                   src/desktop/font/jis_fonts.c \
                   src/desktop/font/tibetan_fonts.c \
                   src/desktop/font/font_mgr.c \
                   src/apps/chokanji/pmc.c
TEST_REVIEW_OBJS = $(TEST_REVIEW_SRCS:.c=.test.o)
TEST_REVIEW_BIN  = ./.build/test_review

test-review: $(TEST_REVIEW_BIN)
	@echo "=========================================================="
	@echo " Running Review PDF & Marginalia Application Tests..."
	@echo "=========================================================="
	@./$(TEST_REVIEW_BIN)

$(TEST_REVIEW_BIN): $(TEST_REVIEW_OBJS)
	$(CC) $(TEST_REVIEW_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS) -lm

# ═══════════════════════════════════════════════════════════════════
# Mouse Drivers Test Suite (UEFI PS/2 & PC-98 Bus Mouse)
# ═══════════════════════════════════════════════════════════════════
TEST_MOUSE_SRCS = verify/tests/test_mouse_drivers.c \
                  src/drivers/uefi/ps2_mouse.c \
                  src/drivers/pc98/pc98_mouse.c \
                  src/drivers/pc98/pc98_kbd.c
TEST_MOUSE_OBJS = $(TEST_MOUSE_SRCS:.c=.test.o)
TEST_MOUSE_BIN  = ./.build/test_mouse

test-mouse: $(TEST_MOUSE_BIN)
	@echo "=========================================================="
	@echo " Running Mouse Drivers Unit Tests (UEFI PS/2 & PC-98)..."
	@echo "=========================================================="
	@./$(TEST_MOUSE_BIN)

$(TEST_MOUSE_BIN): $(TEST_MOUSE_OBJS)
	$(CC) $(TEST_MOUSE_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# B-System DriveSetup Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_DRIVESETUP_SRCS = verify/tests/test_b_drivesetup.c \
                       src/desktop/window/app_menu.c \
                       src/apps/volumes/b_drivesetup.c
TEST_DRIVESETUP_OBJS = $(TEST_DRIVESETUP_SRCS:.c=.test.o)
TEST_DRIVESETUP_BIN  = ./.build/test_b_drivesetup

test-drivesetup: $(TEST_DRIVESETUP_BIN)
	@echo "=========================================================="
	@echo " Running B-System Minimal DriveSetup (b_drivesetup) Tests..."
	@echo "=========================================================="
	@./$(TEST_DRIVESETUP_BIN)

$(TEST_DRIVESETUP_BIN): $(TEST_DRIVESETUP_OBJS) $(FS_OBJS) src/apps/clu/clu.host.o $(CLU_APPS_HOST_OBJS)
	$(CC) $(TEST_DRIVESETUP_OBJS) $(FS_OBJS) src/apps/clu/clu.host.o $(CLU_APPS_HOST_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# HD64180 — Hitachi HD64180 CPU exerciser (Hitachi HC-90/95)
# ═══════════════════════════════════════════════════════════════════
TEST_HD64180_SRCS = verify/tests/test_hd64180.c \
                    src/emulators/msx/hd64180.c \
                    src/emulators/msx/z80.c
TEST_HD64180_OBJS = $(TEST_HD64180_SRCS:.c=.test.o)
TEST_HD64180_BIN  = ./.build/test_hd64180

test-hd64180: $(TEST_HD64180_BIN)
	@echo "=========================================================="
	@echo " Running HD64180 CPU Verification..."
	@echo "=========================================================="
	@./$(TEST_HD64180_BIN)

$(TEST_HD64180_BIN): $(TEST_HD64180_OBJS)
	$(CC) $(TEST_HD64180_OBJS) -o $@ $(LDFLAGS)

# ═══════════════════════════════════════════════════════════════════
# ZEXALL — Z80 instruction exerciser against the vendored MSX core
# ═══════════════════════════════════════════════════════════════════
TEST_ZEXALL_SRCS = src/emulators/msx/zexall.c \
                   src/emulators/msx/z80.c
TEST_ZEXALL_OBJS = $(TEST_ZEXALL_SRCS:.c=.test.o)
TEST_ZEXALL_BIN  = ./.build/test_zexall

test-zexall: $(TEST_ZEXALL_BIN)
	@echo "=========================================================="
	@echo " Running ZEXALL against src/emulators/msx/z80.c..."
	@echo "=========================================================="
	@./$(TEST_ZEXALL_BIN)

$(TEST_ZEXALL_BIN): $(TEST_ZEXALL_OBJS)
	$(CC) $(TEST_ZEXALL_OBJS) -o $@ $(LDFLAGS)

# ═══════════════════════════════════════════════════════════════════
# tronMSX boot tracer — headless ROM frames and P/VDP/PSG traffic
# Dev tool: msx-trace ROM=assets/msx/xxx.rom FRAMES=1300
# ═══════════════════════════════════════════════════════════════════
TEST_MSX_TRACE_SRCS = verify/tests/msx_trace.c $(MSX_SRCS)
TEST_MSX_TRACE_OBJS = $(TEST_MSX_TRACE_SRCS:.c=.test.o)
TEST_MSX_TRACE_BIN  = ./.build/msx_trace

msx-trace: $(TEST_MSX_TRACE_BIN)
	@echo "=========================================================="
	@echo " tronMSX boot trace: $(or $(ROM),assets/msx/c0man.rom)"
	@echo "=========================================================="
	@./$(TEST_MSX_TRACE_BIN) "$(or $(ROM),assets/msx/c0man.rom)" $(or $(FRAMES),300)

$(TEST_MSX_TRACE_BIN): $(TEST_MSX_TRACE_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_MSX_TRACE_OBJS) -o $@ $(LDFLAGS)

TEST_MSX_BOOT_SRCS  = verify/tests/test_msx_boot.c $(MSX_SRCS)
TEST_MSX_BOOT_OBJS  = $(TEST_MSX_BOOT_SRCS:.c=.test.o)
TEST_MSX_BOOT_BIN   = ./.build/test_msx_boot

msx-boot: $(TEST_MSX_BOOT_BIN)
	@./$(TEST_MSX_BOOT_BIN) "$(or $(ROM),assets/msx/c0man.rom)" $(or $(FRAMES),300) $(or $(DUMP),/tmp/msx_boot) "$(or $(MACH),)"

TEST_MSX_DSK_SRCS  = verify/tests/test_msx_dsk.c $(MSX_SRCS)
TEST_MSX_DSK_OBJS  = $(TEST_MSX_DSK_SRCS:.c=.test.o)
TEST_MSX_DSK_BIN   = ./.build/test_msx_dsk

msx-dsk: $(TEST_MSX_DSK_BIN)
	@./$(TEST_MSX_DSK_BIN)

$(TEST_MSX_DSK_BIN): $(TEST_MSX_DSK_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_MSX_DSK_OBJS) -o $@ $(LDFLAGS)

$(TEST_MSX_BOOT_BIN): $(TEST_MSX_BOOT_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_MSX_BOOT_OBJS) -o $@ $(LDFLAGS)

TEST_MSX_SURVEY_SRCS = verify/tests/test_msx_rom_survey.c $(MSX_SRCS)
TEST_MSX_SURVEY_OBJS = $(TEST_MSX_SURVEY_SRCS:.c=.test.o)
TEST_MSX_SURVEY_BIN  = ./.build/test_msx_rom_survey

msx-survey: $(TEST_MSX_SURVEY_BIN)
	@./$(TEST_MSX_SURVEY_BIN) $(or $(FRAMES),180)

$(TEST_MSX_SURVEY_BIN): $(TEST_MSX_SURVEY_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_MSX_SURVEY_OBJS) -o $@ $(LDFLAGS)

TEST_MSX_AUDIO_SRCS = verify/tests/test_msx_audio.c $(MSX_SRCS)
TEST_MSX_AUDIO_OBJS = $(TEST_MSX_AUDIO_SRCS:.c=.test.o)
TEST_MSX_AUDIO_BIN  = ./.build/test_msx_audio

msx-audio: $(TEST_MSX_AUDIO_BIN)
	@./$(TEST_MSX_AUDIO_BIN)

$(TEST_MSX_AUDIO_BIN): $(TEST_MSX_AUDIO_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_MSX_AUDIO_OBJS) -o $@ $(LDFLAGS)

TEST_AUREL_VAULT_SRCS = verify/tests/test_aurel_vault.c $(MSX_SRCS)
TEST_AUREL_VAULT_OBJS = $(TEST_AUREL_VAULT_SRCS:.c=.test.o)
TEST_AUREL_VAULT_BIN  = ./.build/test_aurel_vault

test-aurel-vault: $(TEST_AUREL_VAULT_BIN)
	@./$(TEST_AUREL_VAULT_BIN)

$(TEST_AUREL_VAULT_BIN): $(TEST_AUREL_VAULT_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_AUREL_VAULT_OBJS) -o $@ $(LDFLAGS)

TEST_BMSX_CFG_SRCS = verify/tests/test_bmsx_cfg.c $(MSX_SRCS)
TEST_BMSX_CFG_OBJS = $(TEST_BMSX_CFG_SRCS:.c=.test.o)
TEST_BMSX_CFG_BIN  = ./.build/test_bmsx_cfg

test-bmsx-cfg: $(TEST_BMSX_CFG_BIN) $(TBIOS_ROMS)
	@./$(TEST_BMSX_CFG_BIN)

$(TEST_BMSX_CFG_BIN): $(TEST_BMSX_CFG_OBJS) $(TBIOS_ROMS)
	@mkdir -p ./.build
	$(CC) $(TEST_BMSX_CFG_OBJS) -o $@ $(LDFLAGS)

# ═══════════════════════════════════════════════════════════════════
# T-BIOS & T-DOS Clean-Room Toolchain, ROMs, and Differential Tests
# ═══════════════════════════════════════════════════════════════════
PASMO_BIN    = ./.build/pasmo
PASMO_SRCS   = third_party/pasmo/pasmo/asm.cxx \
               third_party/pasmo/pasmo/cpc.cxx \
               third_party/pasmo/pasmo/pasmo.cxx \
               third_party/pasmo/pasmo/tap.cxx \
               third_party/pasmo/pasmo/asmerror.cxx \
               third_party/pasmo/pasmo/macro.cxx \
               third_party/pasmo/pasmo/pasmotypes.cxx \
               third_party/pasmo/pasmo/token.cxx \
               third_party/pasmo/pasmo/asmfile.cxx \
               third_party/pasmo/pasmo/nullstream.cxx \
               third_party/pasmo/pasmo/spectrum.cxx \
               third_party/pasmo/pasmo/tzx.cxx \
               third_party/pasmo/pasmo/memmap.cxx

$(PASMO_BIN): $(PASMO_SRCS)
	@mkdir -p ./.build
	$(CXX) -O2 -std=c++14 -DNDEBUG $(PASMO_SRCS) -o $@

IHX2ROM_BIN  = ./.build/ihx2rom
IHX2ROM_SRCS = src/emulators/msx/tbios/sdcc/ihx2rom.c

$(IHX2ROM_BIN): $(IHX2ROM_SRCS)
	@mkdir -p ./.build
	$(CC) -O2 $(IHX2ROM_SRCS) -o $@

tbios-tools: $(PASMO_BIN) $(IHX2ROM_BIN)

TBIOS_ROMS = ./.build/tbios/tbios_main_msx2.rom \
             ./.build/tbios/tbios_main_msx2+.rom \
             ./.build/tbios/tbios_main_msxtr.rom \
             ./.build/tbios/tbios_sub.rom \
             ./.build/tbios/tbios_logo.rom \
             ./.build/tbios/tbios_music.rom \
             ./.build/tbios/tdos_disk.rom \
             assets/msx/MARS.ROM

./.build/tbios/tbios_main_msx2.rom: src/emulators/msx/tbios/asm/entry_main.asm $(PASMO_BIN)
	@mkdir -p ./.build/tbios
	@$(PASMO_BIN) -I src/emulators/msx/tbios/inc -I src/emulators/msx/tbios/asm -E MSX_VER=1 src/emulators/msx/tbios/asm/entry_main.asm $@

./.build/tbios/tbios_main_msx2+.rom: src/emulators/msx/tbios/asm/entry_main.asm $(PASMO_BIN)
	@mkdir -p ./.build/tbios
	@$(PASMO_BIN) -I src/emulators/msx/tbios/inc -I src/emulators/msx/tbios/asm -E MSX_VER=2 src/emulators/msx/tbios/asm/entry_main.asm $@

./.build/tbios/tbios_main_msxtr.rom: src/emulators/msx/tbios/asm/entry_main.asm $(PASMO_BIN)
	@mkdir -p ./.build/tbios
	@$(PASMO_BIN) -I src/emulators/msx/tbios/inc -I src/emulators/msx/tbios/asm -E MSX_VER=3 src/emulators/msx/tbios/asm/entry_main.asm $@

./.build/tbios/tbios_sub.rom: src/emulators/msx/tbios/asm/entry_sub.asm $(PASMO_BIN)
	@mkdir -p ./.build/tbios
	@$(PASMO_BIN) -I src/emulators/msx/tbios/inc src/emulators/msx/tbios/asm/entry_sub.asm $@

./.build/tbios/tbios_logo.rom: src/emulators/msx/tbios/asm/entry_logo.asm $(PASMO_BIN)
	@mkdir -p ./.build/tbios
	@$(PASMO_BIN) -I src/emulators/msx/tbios/inc src/emulators/msx/tbios/asm/entry_logo.asm $@

./.build/tbios/tbios_music.rom: src/emulators/msx/tbios/asm/entry_music.asm $(PASMO_BIN)
	@mkdir -p ./.build/tbios
	@$(PASMO_BIN) -I src/emulators/msx/tbios/inc src/emulators/msx/tbios/asm/entry_music.asm $@

./.build/tbios/tdos_disk.rom: src/emulators/msx/tbios/asm/entry_disk.asm $(PASMO_BIN)
	@mkdir -p ./.build/tbios
	@$(PASMO_BIN) -I src/emulators/msx/tbios/inc src/emulators/msx/tbios/asm/entry_disk.asm $@

assets/msx/MARS.ROM: src/emulators/msx/tbios/asm/entry_mars.asm src/emulators/msx/tbios/asm/mars_rle.inc $(PASMO_BIN)
	@mkdir -p assets/msx
	@$(PASMO_BIN) -I src/emulators/msx/tbios/asm $< $@

tbios-roms: $(TBIOS_ROMS)

TFW_ASM_DEPS = $(wildcard src/emulators/msx/tbios/asm/*.asm) $(wildcard src/emulators/msx/tbios/inc/*.inc)

TFW_ROMS = ./.build/tbios/tfw_main_msx2.rom \
           ./.build/tbios/tfw_main_msx2+.rom \
           ./.build/tbios/tfw_main_msxtr.rom

./.build/tbios/tfw_main_msx2.rom: $(TFW_ASM_DEPS) $(PASMO_BIN)
	@mkdir -p ./.build/tbios
	@$(PASMO_BIN) -I src/emulators/msx/tbios/inc -I src/emulators/msx/tbios/asm -E MSX_VER=1 -E CONFIG_TFW=1 src/emulators/msx/tbios/asm/entry_main.asm $@
	@cp $@ assets/msx/

./.build/tbios/tfw_main_msx2+.rom: $(TFW_ASM_DEPS) $(PASMO_BIN)
	@mkdir -p ./.build/tbios
	@$(PASMO_BIN) -I src/emulators/msx/tbios/inc -I src/emulators/msx/tbios/asm -E MSX_VER=2 -E CONFIG_TFW=1 src/emulators/msx/tbios/asm/entry_main.asm $@
	@cp $@ assets/msx/

./.build/tbios/tfw_main_msxtr.rom: $(TFW_ASM_DEPS) $(PASMO_BIN)
	@mkdir -p ./.build/tbios
	@$(PASMO_BIN) -I src/emulators/msx/tbios/inc -I src/emulators/msx/tbios/asm -E MSX_VER=3 -E CONFIG_TFW=1 src/emulators/msx/tbios/asm/entry_main.asm $@
	@cp $@ assets/msx/

tfw-roms: $(TFW_ROMS)
	@cp $(TFW_ROMS) assets/msx/

TEST_TBIOS_DIFF_SRCS = verify/tests/test_tbios_diff.c $(MSX_SRCS)
TEST_TBIOS_DIFF_OBJS = $(TEST_TBIOS_DIFF_SRCS:.c=.test.o)
TEST_TBIOS_DIFF_BIN  = ./.build/test_tbios_diff

$(TEST_TBIOS_DIFF_BIN): $(TEST_TBIOS_DIFF_OBJS) $(TBIOS_ROMS)
	@mkdir -p ./.build
	$(CC) $(TEST_TBIOS_DIFF_OBJS) -o $@ $(LDFLAGS)

TEST_TBIOS_CORE_SRCS = verify/tests/test_tbios_core.c $(MSX_SRCS)
TEST_TBIOS_CORE_OBJS = $(TEST_TBIOS_CORE_SRCS:.c=.test.o)
TEST_TBIOS_CORE_BIN  = ./.build/test_tbios_core

$(TEST_TBIOS_CORE_BIN): $(TEST_TBIOS_CORE_OBJS) $(TBIOS_ROMS)
	@mkdir -p ./.build
	$(CC) $(TEST_TBIOS_CORE_OBJS) -o $@ $(LDFLAGS)

TEST_TBIOS_VIDEO_SRCS = verify/tests/test_tbios_video.c $(MSX_SRCS)
TEST_TBIOS_VIDEO_OBJS = $(TEST_TBIOS_VIDEO_SRCS:.c=.test.o)
TEST_TBIOS_VIDEO_BIN  = ./.build/test_tbios_video

$(TEST_TBIOS_VIDEO_BIN): $(TEST_TBIOS_VIDEO_OBJS) $(TBIOS_ROMS)
	@mkdir -p ./.build
	$(CC) $(TEST_TBIOS_VIDEO_OBJS) -o $@ $(LDFLAGS)

TEST_TBIOS_AUDIO_SRCS = verify/tests/test_tbios_audio.c $(MSX_SRCS)
TEST_TBIOS_AUDIO_OBJS = $(TEST_TBIOS_AUDIO_SRCS:.c=.test.o)
TEST_TBIOS_AUDIO_BIN  = ./.build/test_tbios_audio

$(TEST_TBIOS_AUDIO_BIN): $(TEST_TBIOS_AUDIO_OBJS) $(TBIOS_ROMS)
	@mkdir -p ./.build
	$(CC) $(TEST_TBIOS_AUDIO_OBJS) -o $@ $(LDFLAGS)

TEST_TBIOS_CPU_SRCS = verify/tests/test_tbios_cpu.c $(MSX_SRCS)
TEST_TBIOS_CPU_OBJS = $(TEST_TBIOS_CPU_SRCS:.c=.test.o)
TEST_TBIOS_CPU_BIN  = ./.build/test_tbios_cpu

$(TEST_TBIOS_CPU_BIN): $(TEST_TBIOS_CPU_OBJS) $(TBIOS_ROMS)
	@mkdir -p ./.build
	$(CC) $(TEST_TBIOS_CPU_OBJS) -o $@ $(LDFLAGS)

TEST_TBIOS_MEGAROM_SRCS = verify/tests/test_tbios_megarom.c $(MSX_SRCS)
TEST_TBIOS_MEGAROM_OBJS = $(TEST_TBIOS_MEGAROM_SRCS:.c=.test.o)
TEST_TBIOS_MEGAROM_BIN  = ./.build/test_tbios_megarom

$(TEST_TBIOS_MEGAROM_BIN): $(TEST_TBIOS_MEGAROM_OBJS) $(TBIOS_ROMS)
	@mkdir -p ./.build
	$(CC) $(TEST_TBIOS_MEGAROM_OBJS) -o $@ $(LDFLAGS)

TEST_TBIOS_DISK_SRCS = verify/tests/test_tbios_disk.c $(MSX_SRCS)
TEST_TBIOS_DISK_OBJS = $(TEST_TBIOS_DISK_SRCS:.c=.test.o)
TEST_TBIOS_DISK_BIN  = ./.build/test_tbios_disk

$(TEST_TBIOS_DISK_BIN): $(TEST_TBIOS_DISK_OBJS) $(TBIOS_ROMS)
	@mkdir -p ./.build
	$(CC) $(TEST_TBIOS_DISK_OBJS) -o $@ $(LDFLAGS)

test-tbios: $(TEST_TBIOS_DIFF_BIN) $(TEST_TBIOS_CORE_BIN) $(TEST_TBIOS_VIDEO_BIN) $(TEST_TBIOS_AUDIO_BIN) $(TEST_TBIOS_CPU_BIN) $(TEST_TBIOS_MEGAROM_BIN) $(TEST_TBIOS_DISK_BIN) $(TEST_BMSX_CFG_BIN) $(TBIOS_ROMS)
	@./$(TEST_TBIOS_DIFF_BIN)
	@./$(TEST_TBIOS_CORE_BIN)
	@./$(TEST_TBIOS_VIDEO_BIN)
	@./$(TEST_TBIOS_AUDIO_BIN)
	@./$(TEST_TBIOS_CPU_BIN)
	@./$(TEST_TBIOS_MEGAROM_BIN)
	@./$(TEST_TBIOS_DISK_BIN)
	@./$(TEST_BMSX_CFG_BIN)

test: test-tad test-editor test-chat test-mozc test-wylie test-hmi test-ski test-tracker test-deskclip test-settings test-global-menu test-app-menu test-review test-drivesetup test-fs test-hd64180 test-aurel-vault test-bmsx-cfg test-tbios test-quake test-ps2-softfloat test-arm-aeabi test-hdmi-audio test-usb-audio test-bt-audio test-ps2-gs-pack test-ps2-gs-env test-ps2-gs-rec test-ps2-vu1-geom test-ps2-vu0-kernel test-gscube-nexus test-ps2-gs-poster test-ps2-gs-read test-ps2-gs-tex test-ps2-pointer test-ps2-fused-row test-ps2-gif-ring test-ps2-paintw test-eegs-raster test-eegs-geom test-eegs-present test-gl-math test-gl-backend-choice test-ps2-fb-order test-grid-map test-xmb-atlas test-ps2-opengl-routing test-gl-context test-nexus-ring test-compositor test-ps2-iso9660 test-demo-paths test-printf-float test-ps2-cdvd-loop
	@echo "=========================================================="
	@echo " ALL B-SYSTEM TEST SUITES PASSED (100% SUCCESS)!"
	@echo "=========================================================="

# ═══════════════════════════════════════════════════════════════════
# Clean
# ═══════════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════════
# Ski Bootloader & Multi-Arch Boot Driver Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_SKI_SRCS = verify/tests/test_ski.c src/cores/core_smp.c \
                src/drivers/pc98/boot_pc98.c src/drivers/broadcom/boot_arm_stub.c
TEST_SKI_OBJS = $(TEST_SKI_SRCS:.c=.test.o)
TEST_SKI_BIN  = ./.build/test_ski

test-ski: $(TEST_SKI_BIN)
	@echo "=========================================================="
	@echo " Running Ski Bootloader (Bootman) Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_SKI_BIN)

$(TEST_SKI_BIN): $(TEST_SKI_OBJS)
	$(CC) $(TEST_SKI_OBJS) -o $@ $(LDFLAGS) -lm


# ═══════════════════════════════════════════════════════════════════
# Kernel TIP Extended Wylie (EWTS) Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_WYLIE_SRCS = verify/tests/test_wylie.c src/desktop/tip/tibetan/wylie.c
TEST_WYLIE_OBJS = $(TEST_WYLIE_SRCS:.c=.test.o)
TEST_WYLIE_BIN  = ./.build/test_wylie

test-wylie: $(TEST_WYLIE_BIN)
	@echo "=========================================================="
	@echo " Running Kernel TIP Extended Wylie (EWTS) Tests..."
	@echo "=========================================================="
	@./$(TEST_WYLIE_BIN)

$(TEST_WYLIE_BIN): $(TEST_WYLIE_OBJS)
	$(CC) $(TEST_WYLIE_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# Quake Verification & Glitches Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_QUAKEC_SRCS = verify/tests/test_quakec_interpreter.c src/media/quake/core/pr_exec.c \
                   src/media/quake/sys/fs_btron.c src/media/quake/core/cmd.c src/media/quake/core/cvar.c \
                   src/media/quake/core/mem.c src/media/quake/core/mathlib.c \
                   src/media/quake/core/sv_phys.c src/media/quake/core/world.c
TEST_QUAKEC_OBJS = $(TEST_QUAKEC_SRCS:.c=.test.o)
TEST_QUAKEC_BIN  = ./.build/test_quakec

TEST_QUAKE_ENT_SRCS = verify/tests/test_quake_entities_glitches.c src/media/quake/core/pr_exec.c \
                      src/media/quake/sys/fs_btron.c src/media/quake/core/cmd.c src/media/quake/core/cvar.c \
                      src/media/quake/core/mem.c src/media/quake/core/mathlib.c \
                      src/media/quake/core/sv_phys.c src/media/quake/core/world.c
TEST_QUAKE_ENT_OBJS = $(TEST_QUAKE_ENT_SRCS:.c=.test.o)
TEST_QUAKE_ENT_BIN  = ./.build/test_quake_entities

TEST_QUAKE_CTRL_SRCS = verify/tests/test_quake_controls.c src/media/quake/sys/in_btron.c \
                       src/media/quake/core/sv_phys.c src/media/quake/core/sv_main.c src/media/quake/core/cmd.c \
                       src/media/quake/core/cvar.c src/media/quake/core/mathlib.c src/media/quake/core/mem.c \
                       src/media/quake/sys/fs_btron.c src/media/quake/core/world.c
TEST_QUAKE_CTRL_OBJS = $(TEST_QUAKE_CTRL_SRCS:.c=.test.o)
TEST_QUAKE_CTRL_BIN  = ./.build/test_quake_controls

$(TEST_QUAKEC_BIN): $(TEST_QUAKEC_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKEC_OBJS) -o $@ $(LDFLAGS) -lm

$(TEST_QUAKE_ENT_BIN): $(TEST_QUAKE_ENT_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_ENT_OBJS) -o $@ $(LDFLAGS) -lm

$(TEST_QUAKE_CTRL_BIN): $(TEST_QUAKE_CTRL_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_CTRL_OBJS) -o $@ $(LDFLAGS) -lm

TEST_QUAKE_REPLAY_SRCS = verify/tests/test_quake_replay.c src/media/quake/core/cl_demo.c \
                         src/media/quake/core/cmd.c src/media/quake/core/cvar.c src/media/quake/core/mem.c \
                         src/media/quake/core/mathlib.c src/media/quake/core/world.c src/media/quake/core/pr_exec.c \
                         src/media/quake/core/sv_phys.c src/media/quake/core/sv_main.c \
                         src/media/quake/sys/sys_btron.c src/media/quake/sys/fs_btron.c \
                         src/media/quake/app/quake_ui.c
TEST_QUAKE_REPLAY_OBJS = $(TEST_QUAKE_REPLAY_SRCS:.c=.test.o)
TEST_QUAKE_REPLAY_BIN  = ./.build/test_quake_replay

$(TEST_QUAKE_REPLAY_BIN): $(TEST_QUAKE_REPLAY_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_REPLAY_OBJS) -o $@ $(LDFLAGS) -lm

test-replay: $(TEST_QUAKE_REPLAY_BIN)
	@echo "=========================================================="
	@echo " Running Quake Demo Replay & Level Select Test Suite..."
	@echo "=========================================================="
	@./$(TEST_QUAKE_REPLAY_BIN)

TEST_QUAKE_HULL_SRCS = verify/tests/test_quake_hull.c src/media/quake/core/cl_demo.c \
                       src/media/quake/core/cmd.c src/media/quake/core/cvar.c src/media/quake/core/mem.c \
                       src/media/quake/core/mathlib.c src/media/quake/core/world.c src/media/quake/core/pr_exec.c \
                       src/media/quake/core/sv_phys.c src/media/quake/core/sv_main.c \
                       src/media/quake/sys/sys_btron.c src/media/quake/sys/fs_btron.c \
                       src/media/quake/app/quake_ui.c
TEST_QUAKE_HULL_OBJS = $(TEST_QUAKE_HULL_SRCS:.c=.test.o)
TEST_QUAKE_HULL_BIN  = ./.build/test_quake_hull

$(TEST_QUAKE_HULL_BIN): $(TEST_QUAKE_HULL_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_HULL_OBJS) -o $@ $(LDFLAGS) -lm

test-hull: $(TEST_QUAKE_HULL_BIN)
	@./$(TEST_QUAKE_HULL_BIN)

TEST_QUAKE_VIEW_SRCS = verify/tests/test_quake_visibility.c \
                       src/media/quake/core/cl_demo.c \
                       src/media/quake/core/cmd.c src/media/quake/core/cvar.c src/media/quake/core/mem.c \
                       src/media/quake/core/mathlib.c src/media/quake/core/world.c src/media/quake/core/pr_exec.c \
                       src/media/quake/core/sv_phys.c src/media/quake/core/sv_main.c \
                       src/media/quake/sys/sys_btron.c src/media/quake/sys/fs_btron.c \
                       src/media/quake/app/quake_ui.c
TEST_QUAKE_VIEW_OBJS = $(TEST_QUAKE_VIEW_SRCS:.c=.test.o)
TEST_QUAKE_VIEW_BIN  = ./.build/test_quake_visibility

$(TEST_QUAKE_VIEW_BIN): $(TEST_QUAKE_VIEW_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_VIEW_OBJS) -o $@ $(LDFLAGS) -lm

test-view: $(TEST_QUAKE_VIEW_BIN)
	@./$(TEST_QUAKE_VIEW_BIN)

TEST_QUAKE_SECRET_SRCS = verify/tests/test_quake_secret.c \
                         src/media/quake/core/cl_demo.c \
                         src/media/quake/core/cmd.c src/media/quake/core/cvar.c src/media/quake/core/mem.c \
                         src/media/quake/core/mathlib.c src/media/quake/core/world.c src/media/quake/core/pr_exec.c \
                         src/media/quake/core/sv_phys.c src/media/quake/core/sv_main.c \
                         src/media/quake/sys/sys_btron.c src/media/quake/sys/fs_btron.c \
                         src/media/quake/app/quake_ui.c
TEST_QUAKE_SECRET_OBJS = $(TEST_QUAKE_SECRET_SRCS:.c=.test.o)
TEST_QUAKE_SECRET_BIN  = ./.build/test_quake_secret

$(TEST_QUAKE_SECRET_BIN): $(TEST_QUAKE_SECRET_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_SECRET_OBJS) -o $@ $(LDFLAGS) -lm

test-secret: $(TEST_QUAKE_SECRET_BIN)
	@./$(TEST_QUAKE_SECRET_BIN)

TEST_QUAKE_RENDER_SRCS = verify/tests/test_quake_render.c \
                         src/media/quake/core/cl_demo.c \
                         src/media/quake/core/cmd.c src/media/quake/core/cvar.c src/media/quake/core/mem.c \
                         src/media/quake/core/mathlib.c src/media/quake/core/world.c src/media/quake/core/pr_exec.c \
                         src/media/quake/core/sv_phys.c src/media/quake/core/sv_main.c \
                         src/media/quake/render/r_brush.c src/media/quake/render/r_eegs.c src/media/quake/render/r_light.c \
                         src/media/quake/render/r_surf.c src/media/quake/render/r_alias.c \
                         src/media/quake/render/texture.c \
                         src/media/quake/sys/sys_btron.c src/media/quake/sys/fs_btron.c \
                         src/media/quake/app/quake_ui.c \
                         src/desktop/graphics/gles/gl_dispatch.c src/desktop/graphics/gles/gl_backend_choice.c src/desktop/graphics/virgl/backend_virgl.c
TEST_QUAKE_RENDER_OBJS = $(TEST_QUAKE_RENDER_SRCS:.c=.test.o)
TEST_QUAKE_RENDER_BIN  = ./.build/test_quake_render

$(TEST_QUAKE_RENDER_BIN): $(TEST_QUAKE_RENDER_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_RENDER_OBJS) -o $@ $(LDFLAGS) -lm

test-render: $(TEST_QUAKE_RENDER_BIN)
	@./$(TEST_QUAKE_RENDER_BIN)

TEST_QUAKE_DRONE_SRCS = verify/tests/test_quake_drone.c \
                        src/media/quake/core/cl_demo.c \
                        src/media/quake/core/cmd.c src/media/quake/core/cvar.c src/media/quake/core/mem.c \
                        src/media/quake/core/mathlib.c src/media/quake/core/world.c src/media/quake/core/pr_exec.c \
                        src/media/quake/core/sv_phys.c src/media/quake/core/sv_main.c \
                        src/media/quake/sys/sys_btron.c src/media/quake/sys/fs_btron.c \
                        src/media/quake/app/quake_ui.c
TEST_QUAKE_DRONE_OBJS = $(TEST_QUAKE_DRONE_SRCS:.c=.test.o)
TEST_QUAKE_DRONE_BIN  = ./.build/test_quake_drone

$(TEST_QUAKE_DRONE_BIN): $(TEST_QUAKE_DRONE_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_DRONE_OBJS) -o $@ $(LDFLAGS) -lm

# Leaf-graph route planner for a drone run.  DRONE_MAP=maps/e1m2.bsp picks the level.
test-drone: $(TEST_QUAKE_DRONE_BIN)
	@DRONE_MAP=$${DRONE_MAP:-maps/e1m1.bsp} ./$(TEST_QUAKE_DRONE_BIN)

# "Footage for each level": record and re-read a track for every base episode map.
# Each one writes assets/quake/drone<map>.dem and verifies it through the
# player's own packet framing; a failure stops the run with that map's log.
# The pak0.pak in the tree is the shareware one, so episodes 2-4 report SKIP and
# start being recorded as soon as a registered pak is dropped in.
test-drone-all: $(TEST_QUAKE_DRONE_BIN)
	@for m in e1m1 e1m2 e1m3 e1m4 e1m5 e1m6 e1m7 e1m8 \
	           e2m1 e2m2 e2m3 e2m4 e2m5 e2m6 e2m7 e2m8 \
	           e3m1 e3m2 e3m3 e3m4 e3m5 e3m6 e3m7 \
	           e4m1 e4m2 e4m3 e4m4 e4m5 e4m6 ; do \
	  log=.build/drone_$$m.log ; \
	  DRONE_MAP=maps/$$m.bsp ./$(TEST_QUAKE_DRONE_BIN) > $$log 2>&1 ; rc=$$? ; \
	  if [ $$rc -eq 77 ] ; then \
	    printf '%-6s SKIP — %s is not in this pak0.pak\n' $$m $$m; continue ; \
	  fi ; \
	  if [ $$rc -ne 0 ] ; then \
	    printf '%-6s FAILED — see %s\n' $$m $$log ; cat $$log ; exit 1 ; \
	  fi ; \
	  printf '%-6s %s\n' $$m "$$(grep 'assets/quake' $$log | head -1 | sed 's/^ *//')" ; \
	done

test-quake: $(TEST_QUAKEC_BIN) $(TEST_QUAKE_ENT_BIN) $(TEST_QUAKE_CTRL_BIN) $(TEST_QUAKE_REPLAY_BIN)
	@echo "=========================================================="
	@echo " Running Quake Verification, Glitches & Replay Suite..."
	@echo "=========================================================="
	@./$(TEST_QUAKEC_BIN)
	@./$(TEST_QUAKE_ENT_BIN)
	@./$(TEST_QUAKE_CTRL_BIN)
	@./$(TEST_QUAKE_REPLAY_BIN)

clean:
	@$(MAKE) -C verify clean >/dev/null 2>&1 || true
	rm -f *.toc
	rm -f *.aux
	rm -f *.log
	rm -f *.out
	rm -rf tad_bin
	rm -f $(POSIX_TARGET) $(QEMU_TARGET) $(TKERNEL_TARGET) $(SAKAMURA_TARGET) \
	      $(ARM32_TARGET) $(ARM64_TARGET) $(DEFAULT_TARGET) $(UEFI_TARGET) $(PC98_TARGET) \
	      $(M68K_TARGET) $(PS2_TARGET) $(PS2_ISO) $(MIPS_TARGET) $(TEST_MOZC_BIN) \
	      $(TEST_EDITOR_BIN) $(TEST_HMI_BIN) $(TEST_TAD_BIN) $(TEST_CHAT_BIN) \
	      $(TEST_SKI_BIN) $(TEST_GMENU_BIN) $(TEST_DRIVESETUP_BIN) \
	      $(TEST_QUAKEC_BIN) $(TEST_QUAKE_ENT_BIN) $(TEST_QUAKE_CTRL_BIN)
	find src verify -type f \( -name "*.o" \) -delete 2>/dev/null || true
	rm -f ./verify/models/bfs_allocator_model
	rm -f ./verify/models/bfs_btree_model
	rm -f ./verify/models/bfs_model


# ===================================================================
# Automated Headless Window Screenshot Capture Pipeline
# ===================================================================
CAPTURE_SCREENS_BIN = ./.build/capture_screens
CAPTURE_SCREENS_SRCS = tools/capture_screens.c \
                       src/emulators/msx/msx_app.c \
                       $(MSX_SRCS) \
                       src/desktop/window/dnd.c \
                       src/desktop/graphics/image_decode.c \
                       src/apps/chokanji/pmc.c \
                       src/apps/workbench/desktop.c \
                       src/desktop/settings/language.c \
                       src/desktop/settings/control_panel.c \
                       src/desktop/settings/appearance.c \
                       src/desktop/settings/desktop.c \
                       src/desktop/settings/display.c \
                       src/desktop/settings/input.c \
                       src/desktop/settings/sound.c \
                       src/desktop/settings/network.c \
                       src/desktop/settings/media.c \
                       src/desktop/settings/security.c \
                       src/desktop/settings/system.c \
                       src/desktop/settings/terminal.c \
                       src/apps/workbench/global_menu.c \
                       src/apps/workbench/about.c \
                       src/apps/workbench/tracker.c \
                       src/apps/cabinet/vobj_manager.c \
                       src/apps/editor/t_editor.c \
                       src/apps/browser/tad_browser.c \
                       src/media/paint/paint.c \
                       src/apps/terminal/gterm.c \
                       src/media/cassette/audio_player.c \
                       src/media/orchestra/orchestra.c \
                       src/media/orchestra/orchestra_stage.c \
                       src/media/orchestra/orchestra_arrange.c \
                       src/media/orchestra/orchestra_piano.c \
                       src/media/orchestra/orchestra_score.c \
                       src/media/orchestra/orchestra_mix.c \
                       src/media/orchestra/orchestra_insert.c \
                       src/media/orchestra/orchestra_about.c \
                       src/media/orchestra/orchestra_session.c \
                       src/media/orchestra/orchestra_smf.c \
                       src/media/sdk/gst_rt.c \
                       src/media/orchestra/lukhash_score.c \
                       src/apps/chat/chat.c \
                       src/apps/chat/chat_xml.c \
                       src/desktop/tip/tip_ife.c \
                       src/desktop/tip/japanese/mozc_kkc.c \
                       src/desktop/tip/tibetan/wylie.c \
                       src/desktop/tip/tibetan/tibetan_dict.c \
                       src/desktop/font/troncode.c \
                       src/desktop/font/jis_fonts.c \
                       src/desktop/font/tibetan_fonts.c \
                       src/desktop/window/wnd.c \
                       src/desktop/window/app_menu.c \
                       src/desktop/graphics/dp_core.c \
                       src/desktop/graphics/icons_bundle.c \
                       src/fs/blk_mem.c \
                       src/fs/blk_file.c \
                       src/fs/blk_qcow2.c \
                       src/fs/blk_part.c \
                       src/fs/vol.c \
                       src/fs/file.c \
                       src/kernel/libstr.c \
                       src/apps/clu/lang.c \
                       src/apps/clu/term.c \
                       src/apps/clu/tty.c \
                       src/apps/clu/vfs.c \
                       src/apps/clu/sc/files.c \
                       src/apps/clu/sc/input.c \
                       src/apps/clu/sc/menus.c \
                       src/apps/clu/sc/sc.c \
                       src/apps/clu/tv/tv.c \
                       src/apps/clu/be/native.c \
                       $(BE_DASM_SRCS) \
                       $(CLU_APPS_SRCS) \
                       src/apps/clu/clu.c \
                       src/apps/volumes/b_drivesetup.c \
                       src/apps/clarity/clarity.c \
                       src/apps/clarity/clarity_layout.c \
                       src/apps/clarity/clarity_render.c \
                       src/apps/clarity/clarity_export.c \
                       src/apps/browser/pdf_reader.c \
                       src/apps/review/review.c \
                       src/desktop/font/schrift.c \
                       src/desktop/font/font_mgr.c \
                       src/desktop/graphics/gles/gl_dispatch.c \
                       src/desktop/graphics/gles/gl_backend_choice.c \
                       src/desktop/graphics/gles/egl_surface.c \
                       src/desktop/graphics/virgl/backend_virgl.c \
                       src/media/xmb/xmb.c \
                       src/media/xmb/xmb_wave.c \
                       src/media/sdk/lilcu64_wa.c \
                       src/media/sdk/lilcu64_score.c \
                       src/media/sdk/lilcu64_synth.c \
                       src/media/sdk/lilcu64_insert.c \
                       src/media/sdk/lilcu64_demo.c \
                       src/media/sdk/mars.c \
                       src/drivers/playstation2/ps2_vu0_kernel.c

CAPTURE_SCREENS_OBJS = $(CAPTURE_SCREENS_SRCS:.c=.test.o)

$(CAPTURE_SCREENS_BIN): $(CAPTURE_SCREENS_OBJS)
	$(CC) $(CAPTURE_SCREENS_OBJS) -o $@ $(LDFLAGS) -lm -lz

# ═══════════════════════════════════════════════════════════════════
# µBTRON-FOMA Mobile UI Toolkit Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_FOMA_SRCS = verify/tests/test_foma_ui.c src/apps/workbench/desktop_mobile.c \
                 src/desktop/graphics/dp_core.c src/desktop/font/troncode.c src/desktop/font/jis_fonts.c src/desktop/font/tibetan_fonts.c
TEST_FOMA_OBJS = $(TEST_FOMA_SRCS:.c=.test.o)
TEST_FOMA_BIN  = ./.build/test_foma_ui

test-foma-ui: $(TEST_FOMA_BIN)
	@echo "=========================================================="
	@echo " Running µBTRON-FOMA Mobile UI Toolkit Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_FOMA_BIN)

$(TEST_FOMA_BIN): $(TEST_FOMA_OBJS)
	$(CC) $(TEST_FOMA_OBJS) -o $@ $(LDFLAGS) -lm

screenshots: $(CAPTURE_SCREENS_BIN)
	@echo "=========================================================="
	@echo " Generating Isolated Window Screenshots from C99 Source..."
	@echo "=========================================================="
	@./$(CAPTURE_SCREENS_BIN)
	@python3 scripts/screen_raw_to_png.py
	@python3 scripts/screen_populate_doc.py

sreenshots: screenshots

# ═══════════════════════════════════════════════════════════════════
# µBTRON-FOMA Automated Screen Capture & Documentation
# ═══════════════════════════════════════════════════════════════════
CAPTURE_FOMA_BIN  = ./.build/capture_foma
CAPTURE_FOMA_SRCS = tools/capture_foma.c \
                    src/apps/workbench/desktop_mobile.c \
                    src/apps/workbench/workbench_mobile.c \
                    src/desktop/window/wnd.c \
                    src/apps/chokanji/pmc.c \
                    src/desktop/window/app_menu.c \
                    src/apps/terminal/gterm.c \
                    src/apps/editor/t_editor.c \
                    src/desktop/settings/terminal.c \
                    src/desktop/tip/tip_ife.c \
                    src/desktop/tip/japanese/mozc_kkc.c \
                    src/desktop/tip/tibetan/wylie.c \
                    src/desktop/tip/tibetan/tibetan_dict.c \
                    src/desktop/graphics/dp_core.c \
                    src/desktop/graphics/icons_bundle.c \
                    src/desktop/font/troncode.c \
                    src/desktop/font/jis_fonts.c \
                    src/desktop/font/tibetan_fonts.c \
                    src/fs/blk_mem.c \
                    src/fs/blk_file.c \
                    src/fs/vol.c \
                    src/fs/file.c \
                    src/kernel/libstr.c \
                    $(CLU_APPS_SRCS) \
                    src/apps/clu/clu.c

CAPTURE_FOMA_OBJS = $(CAPTURE_FOMA_SRCS:.c=.test.o)

$(CAPTURE_FOMA_BIN): $(CAPTURE_FOMA_OBJS)
	$(CC) $(CAPTURE_FOMA_OBJS) -o $@ $(LDFLAGS) -lm

foma-screens: $(CAPTURE_FOMA_BIN)
	@echo "=========================================================="
	@echo " Generating Isolated µBTRON-FOMA Mobile Screenshots..."
	@echo "=========================================================="
	@./$(CAPTURE_FOMA_BIN)
	@python3 scripts/screen_update_foma.py

# ═══════════════════════════════════════════════════════════════════
# SegUI (Segmentation UI) Automated Screen Capture - 480x272 panel
# ═══════════════════════════════════════════════════════════════════
CAPTURE_SEGUI_BIN  = ./.build/capture_segui
CAPTURE_SEGUI_SRCS = tools/capture_segui.c \
                     src/desktop/industrial/segui/segui.c \
                     src/desktop/industrial/segui/segui_demo.c \
                     src/desktop/industrial/segui/segui_phone.c \
                     src/desktop/graphics/dp_core.c \
                     src/desktop/font/troncode.c \
                     src/desktop/font/jis_fonts.c \
                     src/desktop/font/tibetan_fonts.c

CAPTURE_SEGUI_OBJS = $(CAPTURE_SEGUI_SRCS:.c=.test.o)

$(CAPTURE_SEGUI_BIN): $(CAPTURE_SEGUI_OBJS)
	$(CC) $(CAPTURE_SEGUI_OBJS) -o $@ $(LDFLAGS) -lm

segui-screens: $(CAPTURE_SEGUI_BIN)
	@echo "=========================================================="
	@echo " Generating SegUI 480x272 Screen Set..."
	@echo "=========================================================="
	@./$(CAPTURE_SEGUI_BIN)
	@python3 scripts/screen_update_segui.py

# ===================================================================
# Quake Automated Screenshot Pipeline — 21:9 (672×288) ARGB frames
# ===================================================================
CAPTURE_QUAKE_BIN  = ./.build/capture_quake
CAPTURE_QUAKE_SRCS = verify/tests/capture_quake.c \
                     src/media/quake/core/mathlib.c \
                     src/media/quake/core/mem.c \
                     src/media/quake/core/cvar.c \
                     src/media/quake/core/cmd.c \
                     src/media/quake/core/wad.c \
                     src/media/quake/core/world.c \
                     src/media/quake/core/pr_exec.c \
                     src/media/quake/core/sv_phys.c \
                     src/media/quake/core/sv_main.c \
                     src/media/quake/sys/sys_btron.c \
                     src/media/quake/sys/in_btron.c \
                     src/media/quake/sys/fs_btron.c \
                     src/media/quake/render/r_btron_gl.c \
                     src/media/quake/render/r_eegs.c \
                     src/media/quake/render/r_brush.c \
                     src/media/quake/render/r_light.c \
                     src/media/quake/render/r_surf.c \
                     src/media/quake/render/r_alias.c \
                     src/media/quake/render/r_part.c \
                     src/media/quake/render/texture.c \
                     src/media/quake/core/cl_demo.c \
                     src/media/quake/app/quake_ui.c \
                     src/desktop/graphics/gles/gl_dispatch.c \
                     src/desktop/graphics/gles/gl_backend_choice.c \
                     src/desktop/graphics/gles/egl_surface.c \
                     src/desktop/graphics/virgl/backend_virgl.c

CAPTURE_QUAKE_OBJS = $(CAPTURE_QUAKE_SRCS:.c=.test.o)

$(CAPTURE_QUAKE_BIN): $(CAPTURE_QUAKE_OBJS)
	@mkdir -p ./.build
	$(CC) $(CAPTURE_QUAKE_OBJS) -o $@ $(LDFLAGS) -lm -lz

quake-screens: $(CAPTURE_QUAKE_BIN)
	@echo "=========================================================="
	@echo " Generating Quake 21:9 (672x288) Screenshot Set..."
	@echo "=========================================================="
	@mkdir -p /tmp/btron_raw_screens
	@./$(CAPTURE_QUAKE_BIN)
	@python3 scripts/screen_raw_to_png.py
	@python3 scripts/screen_update_quake.py

# Nexus SPSC ring gate (host)
test-nexus-ring:
	@mkdir -p ./.build
	$(CC) -std=gnu11 -Wall -Wextra -O2 verify/tests/test_nexus_ring.c -o ./.build/test_nexus_ring
	@./.build/test_nexus_ring
.PHONY: test-nexus-ring

# Shared compositor policy gate (host)
test-compositor:
	@mkdir -p ./.build
	$(CC) -std=gnu11 -Wall -Wextra -O2 -Iinclude verify/tests/test_compositor.c -o ./.build/test_compositor
	@./.build/test_compositor
.PHONY: test-compositor
