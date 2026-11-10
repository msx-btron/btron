#
# BTRON 3.20 Conformance Verifier Makefile
#

CC ?= gcc
SDL_CFLAGS ?= $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/include/SDL2")
SDL_LIBS ?= $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2")
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -Isuite -I../include -I../include/rt -I../include/graphics -I../include/drivers -I../src/kernel -I../src/desktop -I../src/desktop/graphics -I../src/desktop/window -I../src/desktop/font -I../src/apps/cabinet -I../src/desktop/industrial -I../src/apps/chokanji -D_GNU_SOURCE $(SDL_CFLAGS)
# Required libs live in VERIFY_LIBS (:=), NOT in the overridable LDFLAGS: an
# inherited environment LDFLAGS (e.g. Homebrew's keg-only curl hint) otherwise
# replaces the `?=` default and silently drops -lSDL2, breaking the link.
LDFLAGS ?=
VERIFY_LIBS := -lpthread -lm $(SDL_LIBS)

VERIFY_SRCS = suite/btron_verify.c \
              suite/results.c \
              suite/v_types.c \
              suite/v_errors.c \
              suite/v_proc.c \
              suite/v_itron.c \
              suite/v_memory.c \
              suite/v_clk.c \
              suite/v_device.c \
              suite/v_fs.c \
              suite/v_sys.c \
              suite/v_message.c \
              suite/v_vobj.c \
              suite/v_dp.c \
              suite/v_wnd.c \
              suite/v_hmi.c \
              suite/v_font_mgr.c \
              suite/v_tcpip.c \
              suite/v_omgr.c \
              ../src/cores/core_posix.c \
              ../src/kernel/ipc_msg.c \
              ../src/kernel/proc_mgr.c \
              ../src/kernel/mem_mgr.c \
              ../src/kernel/clk_mgr.c \
              ../src/kernel/dev_mgr.c \
              ../src/kernel/sys_mgmt.c \
              ../src/kernel/tcpip.c \
              ../src/desktop/window/event.c \
              ../src/desktop/graphics/dp_core.c \
              ../src/desktop/window/wnd.c \
              ../src/apps/chokanji/pmc.c \
              ../src/apps/cabinet/vobj.c \
              ../src/apps/cabinet/omgr.c \
              ../src/desktop/industrial/hmi/hmi_core.c \
              ../src/desktop/industrial/hmi/hmi_switch.c \
              ../src/desktop/industrial/hmi/hmi_selector.c \
              ../src/desktop/industrial/hmi/hmi_volume.c \
              ../src/desktop/industrial/hmi/hmi_meter.c \
              ../src/desktop/industrial/hmi/hmi_controller.c \
              ../src/desktop/industrial/hmi/hmi_panel.c \
              ../src/desktop/font/troncode.c \
              ../src/desktop/font/jis_fonts.c \
              ../src/desktop/font/tibetan_fonts.c \
              ../src/desktop/font/font_mgr.c \
              ../src/fs/blk_mem.c \
              ../src/fs/blk_file.c \
              ../src/fs/blk_qcow2.c \
              ../src/fs/blk_part.c \
              ../src/fs/vol.c \
              ../src/kernel/fs_record.c \
              ../src/kernel/libstr.c

VERIFY_OBJS = $(VERIFY_SRCS:.c=.o)
VERIFY_BIN = btron_verify

.PHONY: all clean run

all: $(VERIFY_BIN)

$(VERIFY_BIN): $(VERIFY_OBJS)
	$(CC) $(VERIFY_OBJS) -o $@ $(LDFLAGS) $(VERIFY_LIBS)

../src/kernel/libstr.o: ../src/kernel/libstr.c
	$(CC) $(CFLAGS) -DBTRON_LIBSTR_NO_HEAP -c $< -o $@

%.o: %.c suite/btron_verify.h ../include/btron/*.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(VERIFY_BIN)
	@./$(VERIFY_BIN) || true

clean:
	rm -f $(VERIFY_BIN) $(VERIFY_OBJS)
	rm -f *.o suite/*.o ../src/desktop/font/*.o ../src/kernel/*.o ../src/apps/cabinet/*.o ../src/desktop/window/*.o ../src/desktop/graphics/*.o ../src/desktop/industrial/hmi/*.o ../src/fs/*.o ../src/cores/*.o
