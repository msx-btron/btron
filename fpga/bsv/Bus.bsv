/*
 * Bus.bsv — MSX turboR Physical Address, Data, and Crossbar Bus Definitions.
 *
 * Defines the 24-bit physical address space (0x000000 - 0x3FFFFF) and split-phase
 * Client/Server transaction types for the unified memory and I/O crossbar.
 */

package Bus;

// 24-bit physical address covering up to 4MB RAM, 4MB ROM, 128KB VRAM
typedef Bit#(24) BusAddr;
typedef Bit#(8)  BusData;
typedef Bit#(4)  MasterTag;

// Master Client Identifiers
MasterTag tagCPU     = 4'd0;  // Z80 / R800 CPU Core
MasterTag tagVDPScan = 4'd1;  // V9958 Display Rasterizer Scanout
MasterTag tagVDPCmd  = 4'd2;  // V9958 Command Engine (Blitter)
MasterTag tagGeo3D   = 4'd3;  // Geo3D 3D Geometry Coprocessor
MasterTag tagAudio   = 4'd4;  // PCM / Audio DMA buffer

typedef struct {
    MasterTag tag;
    BusAddr   addr;
    BusData   data;
    Bool      write;
    Bool      is_io;
} BusReq deriving (Bits, Eq, FShow);

typedef struct {
    MasterTag tag;
    BusData   data;
    Bool      ready;
} BusResp deriving (Bits, Eq, FShow);

endpackage: Bus
