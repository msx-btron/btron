/*
 * SlotManager.bsv — MSX Primary & Secondary Slot Translator.
 *
 * Implements:
 *   1. 4-Page primary slot selection via Port #A8 (PPI Port A).
 *   2. Secondary sub-slot registers at 0xFFFF per primary slot.
 *   3. MSX turbo R DRAM shadow mode (fast internal DRAM at 0x080000 for Slot 0-0).
 *   4. Translation of 16-bit CPU virtual address to 24-bit physical memory bus address:
 *        - Slot 0-0: Main BIOS ROM (0x000000..0x007FFF) or Shadow DRAM (0x080000..0x087FFF)
 *        - Slot 0-2: Sub-ROM (0x008000..0x00BFFF)
 *        - Slot 1: Cartridge Slot A (0x100000..0x1FFFFF)
 *        - Slot 2: Cartridge Slot B (0x200000..0x2FFFFF)
 *        - Slot 3: Main RAM / Memory Mapper (0x010000..0x07FFFF)
 *
 * References:
 *   src/emulators/msx/slots.c
 *   src/emulators/msx/ppi.c
 */

package SlotManager;

import Bus::*;

// ── Physical Base Addresses ─────────────────────────────────────────────────
BusAddr baseBIOS       = 24'h000000; // 32 KB Main BIOS
BusAddr baseSubROM     = 24'h008000; // 16 KB Sub-ROM / BASIC
BusAddr baseRAM        = 24'h010000; // Up to 512 KB Internal RAM
BusAddr baseShadowDRAM = 24'h080000; // Fast Shadow DRAM for Slot 0-0
BusAddr baseCartA      = 24'h100000; // Cartridge A
BusAddr baseCartB      = 24'h200000; // Cartridge B

interface SlotManager;
    // PPI Port #A8 access
    method Action  write_ppi_a(Bit#(8) val);
    method Bit#(8) read_ppi_a();

    // Mode configuration from T9769C
    method Action set_dram_mode(Bool is_dram);

    // Address translation: CPU virtual 16-bit -> Physical 24-bit
    method BusAddr translate(Bit#(16) cpu_addr);
    method Bool    is_slot_reg(Bit#(16) cpu_addr);
endinterface: SlotManager

(* synthesize *)
module mkSlotManager(SlotManager);
    Reg#(Bit#(8)) ppi_a     <- mkReg(8'h00);
    Reg#(Bool)    dram_mode <- mkReg(False);

    // Secondary subslot registers per primary slot (accessed at 0xFFFF)
    Reg#(Bit#(8)) sub_slot0 <- mkReg(8'h00);
    Reg#(Bit#(8)) sub_slot1 <- mkReg(8'h00);
    Reg#(Bit#(8)) sub_slot2 <- mkReg(8'h00);
    Reg#(Bit#(8)) sub_slot3 <- mkReg(8'h00);

    // Get primary slot (0..3) for a given 16KB page
    function Bit#(2) get_primary_slot(Bit#(16) addr);
        Bit#(2) page = addr[15:14];
        case (page)
            2'd0: return ppi_a[1:0];
            2'd1: return ppi_a[3:2];
            2'd2: return ppi_a[5:4];
            2'd3: return ppi_a[7:6];
        endcase
    endfunction

    // Get secondary subslot (0..3) for a given primary slot and page
    function Bit#(2) get_sub_slot(Bit#(2) prim, Bit#(16) addr);
        Bit#(2) page = addr[15:14];
        Bit#(8) sub_reg = case (prim)
            2'd0: sub_slot0;
            2'd1: sub_slot1;
            2'd2: sub_slot2;
            2'd3: sub_slot3;
        endcase;
        case (page)
            2'd0: return sub_reg[1:0];
            2'd1: return sub_reg[3:2];
            2'd2: return sub_reg[5:4];
            2'd3: return sub_reg[7:6];
        endcase
    endfunction

    // Port #A8 Access
    method Action write_ppi_a(Bit#(8) val);
        ppi_a <= val;
    endmethod

    method Bit#(8) read_ppi_a() = ppi_a;

    method Action set_dram_mode(Bool is_dram);
        dram_mode <= is_dram;
    endmethod

    // Address translation function
    method BusAddr translate(Bit#(16) cpu_addr);
        Bit#(2) prim = get_primary_slot(cpu_addr);
        Bit#(2) sub  = get_sub_slot(prim, cpu_addr);
        Bit#(14) offset = cpu_addr[13:0];

        case (prim)
            // ── Primary Slot 0: BIOS / Sub-ROM / Shadow DRAM ────────────────
            2'd0: begin
                if (sub == 2'd0) begin
                    // Slot 0-0: Main BIOS
                    if (dram_mode) begin
                        return baseShadowDRAM + zeroExtend(cpu_addr);
                    end else begin
                        return baseBIOS + zeroExtend(cpu_addr);
                    end
                end else if (sub == 2'd2) begin
                    // Slot 0-2: Sub-ROM
                    return baseSubROM + zeroExtend(offset);
                end else begin
                    return baseBIOS + zeroExtend(cpu_addr);
                end
            end

            // ── Primary Slot 1: Cartridge A ─────────────────────────────────
            2'd1: begin
                return baseCartA + zeroExtend(cpu_addr);
            end

            // ── Primary Slot 2: Cartridge B ─────────────────────────────────
            2'd2: begin
                return baseCartB + zeroExtend(cpu_addr);
            end

            // ── Primary Slot 3: Internal RAM (Slot 3-0) ─────────────────────
            2'd3: begin
                return baseRAM + zeroExtend(cpu_addr);
            end
        endcase
    endmethod

    method Bool is_slot_reg(Bit#(16) cpu_addr) = (cpu_addr == 16'hFFFF);
endmodule: mkSlotManager

endpackage: SlotManager

