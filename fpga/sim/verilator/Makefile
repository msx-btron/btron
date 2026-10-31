# fpga/sim/verilator/Makefile
# Phase 1: Clocks + Memory Arbiter co-simulation against C99 mem_model.
#
# Build is two-step to handle mixed C99/C++ sources correctly:
#   Step 1: verilator --cc        → generates C++ model in obj_dir/
#   Step 2: compile each source, then link manually
#
# Targets:
#   make          — build ./fpga_sim_phase1
#   make run      — run 1,000,000 master-clock cycles and verify
#   make verilog  — only regenerate Verilog from BSV
#   make clean    — remove generated files

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

# ── BSV source packages (dependency order) ─────────────────────────────────
BSV_SRCS   := $(BSV_ROOT)/Bus.bsv \
              $(BSV_ROOT)/MSXClocks.bsv \
              $(BSV_ROOT)/memory/MemoryArbiter.bsv \
              $(BSV_ROOT)/TopPhase1.bsv

TOP_MODULE := mkTopPhase1
TOP_V      := $(SIM_DIR)/$(TOP_MODULE).v

# ── Compiler include paths ─────────────────────────────────────────────────
VERILATOR_ROOT  ?= $(shell verilator --getenv VERILATOR_ROOT 2>/dev/null || echo /opt/homebrew/share/verilator)
VL_INCLUDE       = $(VERILATOR_ROOT)/include

# BSC support library — exclude Verilog files with unsupported constructs
BSC_LIBDIR  := /opt/homebrew/Cellar/bsc/2026.07.1_1/libexec/lib/Verilog
EXCLUDE_V   := InoutConnect.v ProbeHook.v ProbeCapture.v ProbeWire.v \
               ProbeMux.v ProbeTrigger.v ProbeValue.v main.v
BSC_LIB_V   := $(filter-out $(addprefix $(BSC_LIBDIR)/,$(EXCLUDE_V)), $(wildcard $(BSC_LIBDIR)/*.v))

# ── C99 testbench sources ──────────────────────────────────────────────────
C_SRCS      := $(SIM_DIR)/sim_main.c \
               $(SIM_DIR)/mem_model.c \
               $(SIM_DIR)/frame_dump.c \
               $(SIM_DIR)/trace_compare.c

# C++ bridge files (two: vl_bridge + sc_stub)
CXX_SRCS    := $(SIM_DIR)/vl_bridge.cpp \
               $(SIM_DIR)/sc_stub.cpp

# ── Compiler flags ─────────────────────────────────────────────────────────
COMMON_INC  := -I$(SIM_DIR) -I$(VL_OBJ) -I$(VL_INCLUDE)
CFLAGS      := -std=c99  -O2 -Wall $(COMMON_INC) -DVERILATOR=1
CXXFLAGS    := -std=c++17 -O2 -Wall $(COMMON_INC) -DVERILATOR=1

# Object files derived from sources
C_OBJS   := $(patsubst $(SIM_DIR)/%.c,   $(VL_OBJ)/%.o, $(C_SRCS))
CXX_OBJS := $(patsubst $(SIM_DIR)/%.cpp, $(VL_OBJ)/%.o, $(CXX_SRCS))
VL_OBJS  := $(VL_OBJ)/VmkTopPhase1__ALL.o \
             $(VL_OBJ)/verilated.o \
             $(VL_OBJ)/verilated_threads.o

OUTPUT   := $(SIM_DIR)/fpga_sim_phase1

.PHONY: all run clean verilog verilate

all: $(OUTPUT)

# ── Step 1: Compile BSV → Verilog ─────────────────────────────────────────
verilog: $(TOP_V)

$(TOP_V): $(BSV_SRCS)
	@mkdir -p $(BSC_BDIR)
	$(BSC) -verilog \
	       -p +:$(BSV_ROOT):$(BSV_ROOT)/memory \
	       -vdir $(SIM_DIR) \
	       -bdir $(BSC_BDIR) \
	       -u $(BSV_ROOT)/TopPhase1.bsv
	@echo "Verilog generated: $(TOP_V)"

# ── Step 2: Verilate (generate C++ model only, no build) ──────────────────
$(VL_OBJ)/VmkTopPhase1__ALL.o: $(TOP_V)
	@mkdir -p $(VL_OBJ)
	$(VERILATOR) --cc \
	    --Mdir $(VL_OBJ) \
	    -Wall \
	    -Wno-WIDTHTRUNC \
	    -Wno-PINCONNECTEMPTY \
	    -Wno-UNUSEDSIGNAL \
	    --error-limit 99 \
	    +incdir+$(BSC_LIBDIR) \
	    $(foreach v,$(BSC_LIB_V),-v $(v)) \
	    $(TOP_V) \
	    --top-module $(TOP_MODULE)
	$(MAKE) -C $(VL_OBJ) -f VmkTopPhase1.mk
	# Compile Verilator runtime support
	$(CXX) $(CXXFLAGS) -c $(VL_INCLUDE)/verilated.cpp        -o $(VL_OBJ)/verilated.o
	$(CXX) $(CXXFLAGS) -c $(VL_INCLUDE)/verilated_threads.cpp -o $(VL_OBJ)/verilated_threads.o

# ── Step 3: Compile C99 testbench sources ─────────────────────────────────
$(VL_OBJ)/%.o: $(SIM_DIR)/%.c | $(VL_OBJ)/VmkTopPhase1__ALL.o
	$(CC) $(CFLAGS) -c $< -o $@

# ── Step 4: Compile C++ bridge sources ────────────────────────────────────
$(VL_OBJ)/%.o: $(SIM_DIR)/%.cpp | $(VL_OBJ)/VmkTopPhase1__ALL.o
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ── Step 5: Link everything ────────────────────────────────────────────────
$(OUTPUT): $(VL_OBJS) $(C_OBJS) $(CXX_OBJS)
	$(CXX) -std=c++17 -O2 \
	    $(C_OBJS) $(CXX_OBJS) $(VL_OBJS) \
	    -o $@
	@echo "Built: $@"

run: $(OUTPUT)
	@echo "--- Running Phase 1 co-simulation (1,000,000 master cycles) ---"
	$(OUTPUT) 1000000

clean:
	rm -rf $(VL_OBJ) $(BSC_BDIR) $(OUTPUT)
	rm -f $(SIM_DIR)/mk*.v $(SIM_DIR)/*.bo
