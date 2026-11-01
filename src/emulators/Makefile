# src/emulators — `make` builds the bx8 Verilator suite, then publishes plates.
#
# The suite makefile writes a PPM and calls publish_plates.py before it
# returns, so the fpgaMSX page can show the new frame on reload. `make publish`
# skips the suite and only copies plates that are already on disk.

.PHONY: all publish

all:
	$(MAKE) -C bx8/sim/verilator all

publish:
	$(MAKE) -C bx8/sim/verilator publish_plates
