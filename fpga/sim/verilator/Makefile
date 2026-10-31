# fpga/sim/verilator/Makefile
# MSX turboR FPGA Co-Simulation Makefile (Phases 1, 2, 3).
#
# Targets:
#   make phase1       — build and run Phase 1 (Clocks & Memory Arbiter)
#   make phase2       — build and run Phase 2 (CPU Core, Multiplier, S1990)
#   make phase3       — build and run Phase 3 (Slot Manager & Memory Mapper)
#   make all          — run Phase 1, 2, and 3 suites
#   make clean        — remove generated artifacts

BSC        ?= bsc
VERILATOR  ?= verilator
CC         ?= clang
CXX        ?= clang++

# ── Directories ────────────────────────────────────────────────────────────
FPGA_ROOT  := $(realpath $(CURDIR)/../..)
BSV_ROOT   := $(FPGA_ROOT)/bsv
SIM_DIR    := $(CURDIR)
VL_OBJ     := $(SIM_DIR)/obj_dir
BSC_BDIR   := $(SIM_DIR)/bsc_bdir

# ── BSV Sources ────────────────────────────────────────────────────────────
BSV_P1_SRCS := $(BSV_ROOT)/Bus.bsv \
               $(BSV_ROOT)/MSXClocks.bsv \
               $(BSV_ROOT)/memory/MemoryArbiter.bsv \
               $(BSV_ROOT)/TopPhase1.bsv

BSV_P2_SRCS := $(BSV_ROOT)/Bus.bsv \
               $(BSV_ROOT)/MSXClocks.bsv \
               $(BSV_ROOT)/memory/MemoryArbiter.bsv \
               $(BSV_ROOT)/cpu/ALU.bsv \
               $(BSV_ROOT)/cpu/Decode.bsv \
               $(BSV_ROOT)/cpu/T9769C.bsv \
               $(BSV_ROOT)/cpu/CPUCore.bsv \
               $(BSV_ROOT)/TopPhase2.bsv

BSV_P3_SRCS := $(BSV_ROOT)/Bus.bsv \
               $(BSV_ROOT)/MSXClocks.bsv \
               $(BSV_ROOT)/memory/MemoryArbiter.bsv \
               $(BSV_ROOT)/memory/SlotManager.bsv \
               $(BSV_ROOT)/memory/Mapper.bsv \
               $(BSV_ROOT)/cpu/ALU.bsv \
               $(BSV_ROOT)/cpu/Decode.bsv \
               $(BSV_ROOT)/cpu/T9769C.bsv \
               $(BSV_ROOT)/cpu/CPUCore.bsv \
               $(BSV_ROOT)/TopPhase3.bsv

BSV_P4_SRCS := $(BSV_ROOT)/Bus.bsv \
               $(BSV_ROOT)/MSXClocks.bsv \
               $(BSV_ROOT)/memory/MemoryArbiter.bsv \
               $(BSV_ROOT)/memory/SlotManager.bsv \
               $(BSV_ROOT)/memory/Mapper.bsv \
               $(BSV_ROOT)/cpu/ALU.bsv \
               $(BSV_ROOT)/cpu/Decode.bsv \
               $(BSV_ROOT)/cpu/T9769C.bsv \
               $(BSV_ROOT)/cpu/CPUCore.bsv \
               $(BSV_ROOT)/vdp/VDPRegs.bsv \
               $(BSV_ROOT)/vdp/Raster.bsv \
               $(BSV_ROOT)/vdp/Blitter.bsv \
               $(BSV_ROOT)/vdp/YJK.bsv \
               $(BSV_ROOT)/vdp/V9958.bsv \
               $(BSV_ROOT)/TopPhase4.bsv

TOP_P1_V   := $(SIM_DIR)/mkTopPhase1.v
TOP_P2_V   := $(SIM_DIR)/mkTopPhase2.v
TOP_P3_V   := $(SIM_DIR)/mkTopPhase3.v
TOP_P4_V   := $(SIM_DIR)/mkTopPhase4.v

# ── Compiler include paths ─────────────────────────────────────────────────
VERILATOR_ROOT ?= $(shell verilator --getenv VERILATOR_ROOT 2>/dev/null || echo /opt/homebrew/share/verilator)
VL_INCLUDE      = $(VERILATOR_ROOT)/include

BSC_LIBDIR := /opt/homebrew/Cellar/bsc/2026.07.1_1/libexec/lib/Verilog
EXCLUDE_V  := InoutConnect.v ProbeHook.v ProbeCapture.v ProbeWire.v \
              ProbeMux.v ProbeTrigger.v ProbeValue.v main.v
BSC_LIB_V  := $(filter-out $(addprefix $(BSC_LIBDIR)/,$(EXCLUDE_V)), $(wildcard $(BSC_LIBDIR)/*.v))

COMMON_INC := -I$(SIM_DIR) -I$(VL_OBJ) -I$(VL_INCLUDE)
CFLAGS     := -std=c99  -O2 -Wall $(COMMON_INC) -DVERILATOR=1
CXXFLAGS   := -std=c++17 -O2 -Wall $(COMMON_INC) -DVERILATOR=1

VL_FLAGS   := -Wall -Wno-WIDTHTRUNC -Wno-PINCONNECTEMPTY -Wno-UNUSEDSIGNAL --error-limit 99 \
              +incdir+$(BSC_LIBDIR) $(foreach v,$(BSC_LIB_V),-v $(v))

# ── Binaries ───────────────────────────────────────────────────────────────
BIN_P1     := $(SIM_DIR)/fpga_sim_phase1
BIN_P2     := $(SIM_DIR)/fpga_sim_phase2
BIN_P3     := $(SIM_DIR)/fpga_sim_phase3
BIN_P4     := $(SIM_DIR)/fpga_sim_phase4

.PHONY: all phase1 phase2 phase3 phase4 run clean

all: phase1 phase2 phase3 phase4

# ── Phase 1 Build & Run ────────────────────────────────────────────────────
$(TOP_P1_V): $(BSV_P1_SRCS)
	@mkdir -p $(BSC_BDIR)
	$(BSC) -verilog -p +:$(BSV_ROOT):$(BSV_ROOT)/memory \
	       -vdir $(SIM_DIR) -bdir $(BSC_BDIR) -u $(BSV_ROOT)/TopPhase1.bsv

$(VL_OBJ)/VmkTopPhase1__ALL.o: $(TOP_P1_V)
	@mkdir -p $(VL_OBJ)
	$(VERILATOR) --cc --Mdir $(VL_OBJ) $(VL_FLAGS) $(TOP_P1_V) --top-module mkTopPhase1
	$(MAKE) -C $(VL_OBJ) -f VmkTopPhase1.mk
	$(CXX) $(CXXFLAGS) -c $(VL_INCLUDE)/verilated.cpp -o $(VL_OBJ)/verilated.o
	$(CXX) $(CXXFLAGS) -c $(VL_INCLUDE)/verilated_threads.cpp -o $(VL_OBJ)/verilated_threads.o

$(BIN_P1): $(VL_OBJ)/VmkTopPhase1__ALL.o $(SIM_DIR)/sim_main.c $(SIM_DIR)/mem_model.c $(SIM_DIR)/frame_dump.c $(SIM_DIR)/trace_compare.c $(SIM_DIR)/vl_bridge.cpp $(SIM_DIR)/sc_stub.cpp
	$(CC) $(CFLAGS) -c $(SIM_DIR)/sim_main.c -o $(VL_OBJ)/sim_main.o
	$(CC) $(CFLAGS) -c $(SIM_DIR)/mem_model.c -o $(VL_OBJ)/mem_model.o
	$(CC) $(CFLAGS) -c $(SIM_DIR)/frame_dump.c -o $(VL_OBJ)/frame_dump.o
	$(CC) $(CFLAGS) -c $(SIM_DIR)/trace_compare.c -o $(VL_OBJ)/trace_compare.o
	$(CXX) $(CXXFLAGS) -c $(SIM_DIR)/vl_bridge.cpp -o $(VL_OBJ)/vl_bridge.o
	$(CXX) $(CXXFLAGS) -c $(SIM_DIR)/sc_stub.cpp -o $(VL_OBJ)/sc_stub.o
	$(CXX) -std=c++17 -O2 \
	    $(VL_OBJ)/sim_main.o $(VL_OBJ)/mem_model.o $(VL_OBJ)/frame_dump.o $(VL_OBJ)/trace_compare.o \
	    $(VL_OBJ)/vl_bridge.o $(VL_OBJ)/sc_stub.o \
	    $(VL_OBJ)/VmkTopPhase1__ALL.o $(VL_OBJ)/verilated.o $(VL_OBJ)/verilated_threads.o \
	    -o $@

phase1: $(BIN_P1)
	@echo "=== Running Phase 1 (Clocks & Memory Arbiter) ==="
	$(BIN_P1) 1000000

# ── Phase 2 Build & Run ────────────────────────────────────────────────────
$(TOP_P2_V): $(BSV_P2_SRCS)
	@mkdir -p $(BSC_BDIR)
	$(BSC) -verilog -p +:$(BSV_ROOT):$(BSV_ROOT)/memory:$(BSV_ROOT)/cpu \
	       -vdir $(SIM_DIR) -bdir $(BSC_BDIR) -u $(BSV_ROOT)/TopPhase2.bsv

$(VL_OBJ)/VmkTopPhase2__ALL.o: $(TOP_P2_V)
	@mkdir -p $(VL_OBJ)
	$(VERILATOR) --cc --Mdir $(VL_OBJ) $(VL_FLAGS) \
	    -v $(SIM_DIR)/mkCPUCore.v \
	    -v $(SIM_DIR)/mkR800Multiplier.v \
	    -v $(SIM_DIR)/mkT9769C.v \
	    $(TOP_P2_V) --top-module mkTopPhase2
	$(MAKE) -C $(VL_OBJ) -f VmkTopPhase2.mk
	$(CXX) $(CXXFLAGS) -c $(VL_INCLUDE)/verilated.cpp -o $(VL_OBJ)/verilated.o
	$(CXX) $(CXXFLAGS) -c $(VL_INCLUDE)/verilated_threads.cpp -o $(VL_OBJ)/verilated_threads.o

$(BIN_P2): $(VL_OBJ)/VmkTopPhase2__ALL.o $(SIM_DIR)/sim_phase2.c $(SIM_DIR)/mem_model.c $(SIM_DIR)/trace_compare.c $(SIM_DIR)/vl_bridge_p2.cpp $(SIM_DIR)/sc_stub.cpp
	$(CC) $(CFLAGS) -c $(SIM_DIR)/sim_phase2.c -o $(VL_OBJ)/sim_phase2.o
	$(CC) $(CFLAGS) -c $(SIM_DIR)/mem_model.c -o $(VL_OBJ)/mem_model.o
	$(CC) $(CFLAGS) -c $(SIM_DIR)/trace_compare.c -o $(VL_OBJ)/trace_compare.o
	$(CXX) $(CXXFLAGS) -c $(SIM_DIR)/vl_bridge_p2.cpp -o $(VL_OBJ)/vl_bridge_p2.o
	$(CXX) $(CXXFLAGS) -c $(SIM_DIR)/sc_stub.cpp -o $(VL_OBJ)/sc_stub.o
	$(CXX) -std=c++17 -O2 \
	    $(VL_OBJ)/sim_phase2.o $(VL_OBJ)/mem_model.o $(VL_OBJ)/trace_compare.o \
	    $(VL_OBJ)/vl_bridge_p2.o $(VL_OBJ)/sc_stub.o \
	    $(VL_OBJ)/VmkTopPhase2__ALL.o $(VL_OBJ)/verilated.o $(VL_OBJ)/verilated_threads.o \
	    -o $@

phase2: $(BIN_P2)
	@echo "=== Running Phase 2 (CPU Core, Multiplier, S1990) ==="
	$(BIN_P2)

# ── Phase 3 Build & Run ────────────────────────────────────────────────────
$(TOP_P3_V): $(BSV_P3_SRCS)
	@mkdir -p $(BSC_BDIR)
	$(BSC) -verilog -p +:$(BSV_ROOT):$(BSV_ROOT)/memory:$(BSV_ROOT)/cpu \
	       -vdir $(SIM_DIR) -bdir $(BSC_BDIR) -u $(BSV_ROOT)/TopPhase3.bsv

$(VL_OBJ)/VmkTopPhase3__ALL.o: $(TOP_P3_V)
	@mkdir -p $(VL_OBJ)
	$(VERILATOR) --cc --Mdir $(VL_OBJ) $(VL_FLAGS) \
	    -v $(SIM_DIR)/mkCPUCore.v \
	    -v $(SIM_DIR)/mkR800Multiplier.v \
	    -v $(SIM_DIR)/mkT9769C.v \
	    -v $(SIM_DIR)/mkSlotManager.v \
	    -v $(SIM_DIR)/mkMapper.v \
	    $(TOP_P3_V) --top-module mkTopPhase3
	$(MAKE) -C $(VL_OBJ) -f VmkTopPhase3.mk
	$(CXX) $(CXXFLAGS) -c $(VL_INCLUDE)/verilated.cpp -o $(VL_OBJ)/verilated.o
	$(CXX) $(CXXFLAGS) -c $(VL_INCLUDE)/verilated_threads.cpp -o $(VL_OBJ)/verilated_threads.o

$(BIN_P3): $(VL_OBJ)/VmkTopPhase3__ALL.o $(SIM_DIR)/sim_phase3.c $(SIM_DIR)/mem_model.c $(SIM_DIR)/trace_compare.c $(SIM_DIR)/vl_bridge_p3.cpp $(SIM_DIR)/sc_stub.cpp
	$(CC) $(CFLAGS) -c $(SIM_DIR)/sim_phase3.c -o $(VL_OBJ)/sim_phase3.o
	$(CC) $(CFLAGS) -c $(SIM_DIR)/mem_model.c -o $(VL_OBJ)/mem_model.o
	$(CC) $(CFLAGS) -c $(SIM_DIR)/trace_compare.c -o $(VL_OBJ)/trace_compare.o
	$(CXX) $(CXXFLAGS) -c $(SIM_DIR)/vl_bridge_p3.cpp -o $(VL_OBJ)/vl_bridge_p3.o
	$(CXX) $(CXXFLAGS) -c $(SIM_DIR)/sc_stub.cpp -o $(VL_OBJ)/sc_stub.o
	$(CXX) -std=c++17 -O2 \
	    $(VL_OBJ)/sim_phase3.o $(VL_OBJ)/mem_model.o $(VL_OBJ)/trace_compare.o \
	    $(VL_OBJ)/vl_bridge_p3.o $(VL_OBJ)/sc_stub.o \
	    $(VL_OBJ)/VmkTopPhase3__ALL.o $(VL_OBJ)/verilated.o $(VL_OBJ)/verilated_threads.o \
	    -o $@

phase3: $(BIN_P3)
	@echo "=== Running Phase 3 (Slot Manager & Memory Mapper) ==="
	$(BIN_P3)

# ── Phase 4 Build & Run ────────────────────────────────────────────────────
$(TOP_P4_V): $(BSV_P4_SRCS)
	@mkdir -p $(BSC_BDIR)
	$(BSC) -verilog -no-warn-action-shadowing \
	       -p +:$(BSV_ROOT):$(BSV_ROOT)/memory:$(BSV_ROOT)/cpu:$(BSV_ROOT)/vdp \
	       -vdir $(SIM_DIR) -bdir $(BSC_BDIR) -u $(BSV_ROOT)/TopPhase4.bsv

$(VL_OBJ)/VmkTopPhase4__ALL.a: $(TOP_P4_V)
	@mkdir -p $(VL_OBJ)
	$(VERILATOR) --cc --Mdir $(VL_OBJ) $(VL_FLAGS) \
	    -v $(SIM_DIR)/mkCPUCore.v \
	    -v $(SIM_DIR)/mkR800Multiplier.v \
	    -v $(SIM_DIR)/mkT9769C.v \
	    -v $(SIM_DIR)/mkSlotManager.v \
	    -v $(SIM_DIR)/mkMapper.v \
	    -v $(SIM_DIR)/mkRaster.v \
	    -v $(SIM_DIR)/mkBlitter.v \
	    -v $(SIM_DIR)/mkV9958.v \
	    $(TOP_P4_V) --top-module mkTopPhase4
	$(MAKE) -C $(VL_OBJ) -f VmkTopPhase4.mk
	$(CXX) $(CXXFLAGS) -c $(VL_INCLUDE)/verilated.cpp -o $(VL_OBJ)/verilated.o
	$(CXX) $(CXXFLAGS) -c $(VL_INCLUDE)/verilated_threads.cpp -o $(VL_OBJ)/verilated_threads.o

$(BIN_P4): $(VL_OBJ)/VmkTopPhase4__ALL.a $(SIM_DIR)/sim_phase4.c $(SIM_DIR)/mem_model.c $(SIM_DIR)/trace_compare.c $(SIM_DIR)/vl_bridge_p4.cpp $(SIM_DIR)/sc_stub.cpp
	$(CC) $(CFLAGS) -c $(SIM_DIR)/sim_phase4.c -o $(VL_OBJ)/sim_phase4.o
	$(CC) $(CFLAGS) -c $(SIM_DIR)/mem_model.c -o $(VL_OBJ)/mem_model.o
	$(CC) $(CFLAGS) -c $(SIM_DIR)/trace_compare.c -o $(VL_OBJ)/trace_compare.o
	$(CXX) $(CXXFLAGS) -c $(SIM_DIR)/vl_bridge_p4.cpp -o $(VL_OBJ)/vl_bridge_p4.o
	$(CXX) $(CXXFLAGS) -c $(SIM_DIR)/sc_stub.cpp -o $(VL_OBJ)/sc_stub.o
	$(CXX) -std=c++17 -O2 \
	    $(VL_OBJ)/sim_phase4.o $(VL_OBJ)/mem_model.o $(VL_OBJ)/trace_compare.o \
	    $(VL_OBJ)/vl_bridge_p4.o $(VL_OBJ)/sc_stub.o \
	    $(VL_OBJ)/VmkTopPhase4__ALL.a $(VL_OBJ)/VmkTopPhase4.o $(VL_OBJ)/verilated.o $(VL_OBJ)/verilated_threads.o \
	    -o $@

phase4: $(BIN_P4)
	@echo "=== Running Phase 4 (Yamaha V9958 Video Processor) ==="
	$(BIN_P4)

clean:
	rm -rf $(VL_OBJ) $(BSC_BDIR) $(BIN_P1) $(BIN_P2) $(BIN_P3) $(BIN_P4)
	rm -f $(SIM_DIR)/mk*.v $(SIM_DIR)/*.bo frame_phase4.ppm
