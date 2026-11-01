/*
 * Blitter.bsv — Yamaha V9938 / V9958 VDP Command Engine.
 *
 * Implements:
 *   - 11 VDP command engine commands:
 *       HMMV (0x0C), HMMM (0x0D), YMMM (0x0E), HMMC (0x0F)
 *       LMMV (0x08), LMMM (0x09), LMMC (0x0B)
 *       LINE (0x07), PSET (0x05), POINT (0x04), STOP (0x00)
 *   - Alex Wulms Bresenham line algorithm (major/minor axis, error accumulator)
 *   - Full 10-bit coordinate wrapping (SX, SY, DX, DY, NX, NY)
 *   - 16 logical operations (IMP, AND, OR, XOR, NOT, TIMP, TAND, TOR, TXOR, TNOT)
 *   - Non-blocking split-phase bus interface with tagVDPCmd
 *   - Command busy (ST_CE) and CPU transfer ready (ST_TR) tracking
 *
 * References:
 *   src/emulators/msx/v9938.c (lines 230-530)
 */

package Blitter;

import FIFOF::*;
import GetPut::*;
import Bus::*;

// ── Command Codes (high nibble of CMD) ──────────────────────────────────────
Bit#(4) cmdSTOP  = 4'h0;
Bit#(4) cmdPOINT = 4'h4;
Bit#(4) cmdPSET  = 4'h5;
Bit#(4) cmdSRCH  = 4'h6;
Bit#(4) cmdLINE  = 4'h7;
Bit#(4) cmdLMMV  = 4'h8;
Bit#(4) cmdLMMM  = 4'h9;
Bit#(4) cmdLMCM  = 4'hA;
Bit#(4) cmdLMMC  = 4'hB;
Bit#(4) cmdHMMV  = 4'hC;
Bit#(4) cmdHMMM  = 4'hD;
Bit#(4) cmdYMMM  = 4'hE;
Bit#(4) cmdHMMC  = 4'hF;

typedef struct {
    Bit#(10) sx;
    Bit#(10) sy;
    Bit#(10) dx;
    Bit#(10) dy;
    Bit#(10) nx;
    Bit#(10) ny;
    Bit#(8)  col;
    Bit#(8)  arg;
    Bit#(8)  cmd;
    Bit#(5)  mode;
} BlitCmdReq deriving (Bits, Eq);

// ── Blitter FSM States ──────────────────────────────────────────────────────
typedef enum {
    BlitIdle,
    BlitStart,
    // Byte commands (HMMV, HMMM, YMMM)
    BlitHMMV_Write,
    BlitHMMV_Wait,
    BlitHMMM_Read,
    BlitHMMM_WaitRead,
    BlitHMMM_Write,
    BlitHMMM_WaitWrite,
    // Pixel commands (PSET, POINT, LINE, LMMV, LMMM)
    BlitLMMM_ReadSrc,
    BlitLMMM_WaitSrc,
    BlitPixel_Read,
    BlitPixel_WaitRead,
    BlitPixel_Write,
    BlitPixel_WaitWrite,
    // CPU transfer commands (HMMC, LMMC)
    BlitTransfer_WaitCPU,
    BlitTransfer_Write,
    BlitTransfer_WaitWrite,
    BlitStep,
    BlitDone
} BlitState deriving (Bits, Eq, FShow);

interface Blitter;
    // Register Configuration
    method Action set_params(
        Bit#(10) sx, Bit#(10) sy,
        Bit#(10) dx, Bit#(10) dy,
        Bit#(10) nx, Bit#(10) ny,
        Bit#(8)  col, Bit#(8) arg, Bit#(8) cmd,
        Bit#(5)  mode
    );

    // CPU Transfer Data feed (for HMMC / LMMC)
    method Action cpu_transfer_write(Bit#(8) data);

    // Status Queries
    (* always_ready *) method Bool    is_busy();     // ST_CE
    (* always_ready *) method Bool    is_tr();       // ST_TR
    (* always_ready *) method Bit#(8) point_color(); // POINT readback color for S#7

    // VRAM Memory Bus Interface (to MemoryArbiter tagVDPCmd)
    interface Get#(BusReq)  vram_req;
    interface Put#(BusResp) vram_resp;
endinterface: Blitter

(* synthesize *)
module mkBlitter(Blitter);
    // Control Registers
    Reg#(Bit#(10)) reg_sx  <- mkReg(0);
    Reg#(Bit#(10)) reg_sy  <- mkReg(0);
    Reg#(Bit#(10)) reg_dx  <- mkReg(0);
    Reg#(Bit#(10)) reg_dy  <- mkReg(0);
    Reg#(Bit#(10)) reg_nx  <- mkReg(0);
    Reg#(Bit#(10)) reg_ny  <- mkReg(0);
    Reg#(Bit#(8))  reg_col <- mkReg(0);
    Reg#(Bit#(8))  reg_arg <- mkReg(0);
    Reg#(Bit#(8))  reg_cmd <- mkReg(0);
    Reg#(Bit#(5))  reg_mode<- mkReg(0);

    // State & Iteration Counters
    Reg#(BlitState) state    <- mkReg(BlitIdle);
    Reg#(Bit#(10))  cur_xi   <- mkReg(0);
    Reg#(Bit#(10))  cur_yi   <- mkReg(0);
    Reg#(Bit#(10))  count_x  <- mkReg(0);
    Reg#(Bit#(10))  count_y  <- mkReg(0);

    // Bresenham LINE State
    Reg#(Bit#(10))  line_step <- mkReg(0);
    Reg#(Int#(16))  line_d    <- mkReg(0);
    Reg#(Bit#(10))  line_x    <- mkReg(0);
    Reg#(Bit#(10))  line_y    <- mkReg(0);

    // Readback & Transfer Buffers
    Reg#(Bit#(8))   read_data   <- mkReg(0);
    Reg#(Bit#(8))   src_pix     <- mkReg(0);
    Reg#(Bit#(8))   pt_color    <- mkReg(0);

    // Command Dispatch & Transfer Queues
    FIFOF#(BlitCmdReq) q_cmd_start <- mkFIFOF1;
    FIFOF#(Bit#(8))    q_tr_in     <- mkFIFOF1;

    // Bus Interface FIFOs
    FIFOF#(BusReq)  q_req  <- mkFIFOF;
    FIFOF#(BusResp) q_resp <- mkFIFOF;

    // Helper: Address calculations
    // VRAM is mapped at physical base 0x300000
    Bit#(24) vramBase = 24'h300000;

    // Line pitch: Screen 7 (0x14) and Screen 8 (0x1C) = 256 bytes; Screen 5 (0x0C) and Screen 6 (0x10) = 128 bytes
    Bool pitch256 = (reg_mode == 5'h14 || reg_mode == 5'h1C);
    Bit#(10) linePitch = pitch256 ? 10'd256 : 10'd128;

    // DIX and DIY direction flags from ARG
    Bool dixNegative = (reg_arg[2] == 1'b1);
    Bool diyNegative = (reg_arg[3] == 1'b1);

    function Bit#(10) stepX(Bit#(10) base, Bit#(10) offset, Bool neg);
        return neg ? (base - offset) : (base + offset);
    endfunction

    function Bit#(10) stepY(Bit#(10) base, Bit#(10) offset, Bool neg);
        return neg ? (base - offset) : (base + offset);
    endfunction

    // Logical Operations function
    function Bit#(8) applyOp(Bit#(4) op, Bit#(8) oldC, Bit#(8) newC);
        case (op)
            4'h1: return oldC & newC;
            4'h2: return oldC | newC;
            4'h3: return oldC ^ newC;
            4'h4: return ~newC;
            4'h8: return (newC != 0) ? newC : oldC;
            4'h9: return (newC != 0) ? (oldC & newC) : oldC;
            4'hA: return (newC != 0) ? (oldC | newC) : oldC;
            4'hB: return (newC != 0) ? (oldC ^ newC) : oldC;
            4'hC: return (newC != 0) ? (~newC) : oldC;
            default: return newC; // 4'h0 IMP
        endcase
    endfunction

    // ── Blitter FSM Rules ───────────────────────────────────────────────────

    // Accept new command from queue when idle
    rule blit_accept (state == BlitIdle && q_cmd_start.notEmpty);
        let req = q_cmd_start.first;
        q_cmd_start.deq;

        reg_sx   <= req.sx;
        reg_sy   <= req.sy;
        reg_dx   <= req.dx;
        reg_dy   <= req.dy;
        reg_nx   <= req.nx;
        reg_ny   <= req.ny;
        reg_col  <= req.col;
        reg_arg  <= req.arg;
        reg_cmd  <= req.cmd;
        reg_mode <= req.mode;

        if (req.cmd[7:4] == cmdSTOP) begin
            state <= BlitIdle;
        end else begin
            state <= BlitStart;
        end
    endrule

    // Start Command Dispatch
    rule blit_start (state == BlitStart);
        Bit#(4) code = reg_cmd[7:4];
        cur_xi <= 0;
        cur_yi <= 0;

        // Determine X count
        Bit#(10) effNx = (reg_nx == 0) ? (pitch256 ? 10'd256 : 10'd128) : reg_nx;
        Bit#(10) effNy = (reg_ny == 0) ? 10'd1023 : reg_ny;

        count_y <= effNy;

        if (code == cmdHMMV) begin
            // HMMV: count_x is in bytes
            Bit#(10) bx = (reg_mode == 5'h1C) ? effNx :
                          (reg_mode == 5'h10) ? (effNx >> 2) : (effNx >> 1);
            count_x <= (bx == 0) ? linePitch : bx;
            state <= BlitHMMV_Write;
        end else if (code == cmdHMMM || code == cmdYMMM) begin
            Bit#(10) bx = (code == cmdYMMM) ? linePitch :
                          ((reg_mode == 5'h1C) ? effNx :
                           (reg_mode == 5'h10) ? (effNx >> 2) : (effNx >> 1));
            count_x <= (bx == 0) ? linePitch : bx;
            state <= BlitHMMM_Read;
        end else if (code == cmdPSET || code == cmdPOINT) begin
            state <= BlitPixel_Read;
        end else if (code == cmdLINE) begin
            line_step <= 0;
            line_x    <= reg_dx;
            line_y    <= reg_dy;
            // line_d = 2 * min_len - maj_len (NX is maj_len, NY is min_len)
            Int#(16) majLen = unpack({6'b0, reg_nx});
            Int#(16) minLen = unpack({6'b0, reg_ny});
            line_d    <= 2 * minLen - majLen;
            state     <= BlitPixel_Read;
        end else if (code == cmdLMMV) begin
            count_x <= effNx;
            state   <= BlitPixel_Read;
        end else if (code == cmdLMMM) begin
            // Source pixel first, then the existing dest read/modify/write.
            count_x <= effNx;
            state   <= BlitLMMM_ReadSrc;
        end else if (code == cmdHMMC || code == cmdLMMC) begin
            count_x <= (code == cmdHMMC) ? ((reg_mode == 5'h1C) ? effNx : (effNx >> 1)) : effNx;
            state   <= BlitTransfer_WaitCPU;
        end else begin
            state <= BlitDone;
        end
    endrule

    // HMMV: Write byte fill
    rule blit_hmmv_write (state == BlitHMMV_Write);
        Bit#(10) y = stepY(reg_dy, cur_yi, diyNegative);
        Bit#(10) startX = (reg_mode == 5'h1C) ? reg_dx :
                          (reg_mode == 5'h10) ? (reg_dx >> 2) : (reg_dx >> 1);
        Bit#(10) x = stepX(startX, cur_xi, dixNegative) & (linePitch - 1);
        Bit#(24) addr = vramBase + zeroExtend(y) * zeroExtend(linePitch) + zeroExtend(x);

        q_req.enq(BusReq {
            tag:   tagVDPCmd,
            addr:  addr,
            data:  reg_col,
            write: True,
            is_io: False
        });
        state <= BlitHMMV_Wait;
    endrule

    rule blit_hmmv_wait (state == BlitHMMV_Wait);
        let resp <- toGet(q_resp).get();
        // Advance iteration
        if (cur_xi + 1 >= count_x) begin
            cur_xi <= 0;
            if (cur_yi + 1 >= count_y) begin
                state <= BlitDone;
            end else begin
                cur_yi <= cur_yi + 1;
                state  <= BlitHMMV_Write;
            end
        end else begin
            cur_xi <= cur_xi + 1;
            state  <= BlitHMMV_Write;
        end
    endrule

    // HMMM: Read source byte
    rule blit_hmmm_read (state == BlitHMMM_Read);
        Bit#(10) sy = stepY(reg_sy, cur_yi, diyNegative);
        Bit#(10) startSx = (reg_cmd[7:4] == cmdYMMM) ? 0 :
                           ((reg_mode == 5'h1C) ? reg_sx :
                            (reg_mode == 5'h10) ? (reg_sx >> 2) : (reg_sx >> 1));
        Bit#(10) sx = stepX(startSx, cur_xi, dixNegative) & (linePitch - 1);
        Bit#(24) sAddr = vramBase + zeroExtend(sy) * zeroExtend(linePitch) + zeroExtend(sx);

        q_req.enq(BusReq {
            tag:   tagVDPCmd,
            addr:  sAddr,
            data:  0,
            write: False,
            is_io: False
        });
        state <= BlitHMMM_WaitRead;
    endrule

    rule blit_hmmm_wait_read (state == BlitHMMM_WaitRead);
        let resp <- toGet(q_resp).get();
        read_data <= resp.data;
        state     <= BlitHMMM_Write;
    endrule

    rule blit_hmmm_write (state == BlitHMMM_Write);
        Bit#(10) dy = stepY(reg_dy, cur_yi, diyNegative);
        Bit#(10) startDx = (reg_cmd[7:4] == cmdYMMM) ? 0 :
                           ((reg_mode == 5'h1C) ? reg_dx :
                            (reg_mode == 5'h10) ? (reg_dx >> 2) : (reg_dx >> 1));
        Bit#(10) dx = stepX(startDx, cur_xi, dixNegative) & (linePitch - 1);
        Bit#(24) dAddr = vramBase + zeroExtend(dy) * zeroExtend(linePitch) + zeroExtend(dx);

        q_req.enq(BusReq {
            tag:   tagVDPCmd,
            addr:  dAddr,
            data:  read_data,
            write: True,
            is_io: False
        });
        state <= BlitHMMM_WaitWrite;
    endrule

    rule blit_hmmm_wait_write (state == BlitHMMM_WaitWrite);
        let resp <- toGet(q_resp).get();
        if (cur_xi + 1 >= count_x) begin
            cur_xi <= 0;
            if (cur_yi + 1 >= count_y) begin
                state <= BlitDone;
            end else begin
                cur_yi <= cur_yi + 1;
                state  <= BlitHMMM_Read;
            end
        end else begin
            cur_xi <= cur_xi + 1;
            state  <= BlitHMMM_Read;
        end
    endrule

    // LMMM: read the source pixel, then fall into the dest read/modify/write.
    rule blit_lmmm_read_src (state == BlitLMMM_ReadSrc);
        Bit#(10) x = stepX(reg_sx, cur_xi, dixNegative);
        Bit#(10) y = stepY(reg_sy, cur_yi, diyNegative);
        Bit#(24) byteAddr = (reg_mode == 5'h1C) ?
            (vramBase + zeroExtend(y) * 256 + zeroExtend(x[7:0])) :
            (vramBase + zeroExtend(y) * 128 + zeroExtend(x[8:1]));

        q_req.enq(BusReq {
            tag:   tagVDPCmd,
            addr:  byteAddr,
            data:  0,
            write: False,
            is_io: False
        });
        state <= BlitLMMM_WaitSrc;
    endrule

    rule blit_lmmm_wait_src (state == BlitLMMM_WaitSrc);
        let resp <- toGet(q_resp).get();
        Bit#(10) x = stepX(reg_sx, cur_xi, dixNegative);
        src_pix <= (reg_mode == 5'h1C) ? resp.data :
                   (x[0] == 1'b1) ? (resp.data & 8'h0F) : ((resp.data >> 4) & 8'h0F);
        state <= BlitPixel_Read;
    endrule

    // Pixel operations (PSET, POINT, LINE, LMMV, and LMMM's destination)
    rule blit_pixel_read (state == BlitPixel_Read);
        Bit#(4) code = reg_cmd[7:4];
        Bit#(10) x = (code == cmdPOINT) ? reg_sx :
                     (code == cmdLINE)  ? line_x :
                     (code == cmdPSET)  ? reg_dx :
                     stepX(reg_dx, cur_xi, dixNegative);
        Bit#(10) y = (code == cmdPOINT) ? reg_sy :
                     (code == cmdLINE)  ? line_y :
                     (code == cmdPSET)  ? reg_dy :
                     stepY(reg_dy, cur_yi, diyNegative);

        Bit#(24) byteAddr = (reg_mode == 5'h1C) ?
            (vramBase + zeroExtend(y) * 256 + zeroExtend(x[7:0])) :
            (vramBase + zeroExtend(y) * 128 + zeroExtend(x[8:1]));

        q_req.enq(BusReq {
            tag:   tagVDPCmd,
            addr:  byteAddr,
            data:  0,
            write: False,
            is_io: False
        });
        state <= BlitPixel_WaitRead;
    endrule

    rule blit_pixel_wait_read (state == BlitPixel_WaitRead);
        let resp <- toGet(q_resp).get();
        Bit#(4) code = reg_cmd[7:4];
        Bit#(10) x = (code == cmdPOINT) ? reg_sx :
                     (code == cmdLINE)  ? line_x :
                     (code == cmdPSET)  ? reg_dx :
                     stepX(reg_dx, cur_xi, dixNegative);

        if (code == cmdPOINT) begin
            // Extract pixel
            Bit#(8) col = (reg_mode == 5'h1C) ? resp.data :
                          (x[0] == 1'b1) ? (resp.data & 8'h0F) : ((resp.data >> 4) & 8'h0F);
            pt_color <= col;
            state    <= BlitDone;
        end else begin
            // Apply logical operation
            Bit#(4) op = reg_cmd[3:0];
            Bit#(8) oldC = (reg_mode == 5'h1C) ? resp.data :
                           (x[0] == 1'b1) ? (resp.data & 8'h0F) : ((resp.data >> 4) & 8'h0F);
            // LMMM's ink is the source pixel. LMMV, PSET and LINE use R#44.
            Bit#(8) ink = (code == cmdLMMM) ? src_pix : reg_col;
            Bit#(8) newC = applyOp(op, oldC, ink);

            Bit#(8) outData = resp.data;
            if (reg_mode == 5'h1C) begin
                outData = newC;
            end else begin
                if (x[0] == 1'b1)
                    outData = (resp.data & 8'hF0) | (newC & 8'h0F);
                else
                    outData = (resp.data & 8'h0F) | ((newC & 8'h0F) << 4);
            end
            read_data <= outData;
            state     <= BlitPixel_Write;
        end
    endrule

    rule blit_pixel_write (state == BlitPixel_Write);
        Bit#(4) code = reg_cmd[7:4];
        Bit#(10) x = (code == cmdLINE) ? line_x :
                     (code == cmdPSET) ? reg_dx :
                     stepX(reg_dx, cur_xi, dixNegative);
        Bit#(10) y = (code == cmdLINE) ? line_y :
                     (code == cmdPSET) ? reg_dy :
                     stepY(reg_dy, cur_yi, diyNegative);

        Bit#(24) byteAddr = (reg_mode == 5'h1C) ?
            (vramBase + zeroExtend(y) * 256 + zeroExtend(x[7:0])) :
            (vramBase + zeroExtend(y) * 128 + zeroExtend(x[8:1]));

        q_req.enq(BusReq {
            tag:   tagVDPCmd,
            addr:  byteAddr,
            data:  read_data,
            write: True,
            is_io: False
        });
        state <= BlitPixel_WaitWrite;
    endrule

    rule blit_pixel_wait_write (state == BlitPixel_WaitWrite);
        let resp <- toGet(q_resp).get();
        Bit#(4) code = reg_cmd[7:4];

        if (code == cmdPSET) begin
            state <= BlitDone;
        end else if (code == cmdLINE) begin
            // Bresenham line step
            Int#(16) majLen = unpack({6'b0, reg_nx});
            Int#(16) minLen = unpack({6'b0, reg_ny});
            Bool majY = (reg_arg[0] == 1'b1);

            Bit#(10) nextX = line_x;
            Bit#(10) nextY = line_y;
            Int#(16) nextD = line_d;

            if (majY) begin
                nextY = diyNegative ? line_y - 1 : line_y + 1;
                if (line_d >= 0) begin
                    nextX = dixNegative ? line_x - 1 : line_x + 1;
                    nextD = line_d - 2 * majLen + 2 * minLen;
                end else begin
                    nextD = line_d + 2 * minLen;
                end
            end else begin
                nextX = dixNegative ? line_x - 1 : line_x + 1;
                if (line_d >= 0) begin
                    nextY = diyNegative ? line_y - 1 : line_y + 1;
                    nextD = line_d - 2 * majLen + 2 * minLen;
                end else begin
                    nextD = line_d + 2 * minLen;
                end
            end

            line_x <= nextX;
            line_y <= nextY;
            line_d <= nextD;

            if (line_step >= reg_nx) begin
                state <= BlitDone;
            end else begin
                line_step <= line_step + 1;
                state     <= BlitPixel_Read;
            end
        end else if (code == cmdLMMV || code == cmdLMMM) begin
            BlitState next = (code == cmdLMMM) ? BlitLMMM_ReadSrc : BlitPixel_Read;
            if (cur_xi + 1 >= count_x) begin
                cur_xi <= 0;
                if (cur_yi + 1 >= count_y) begin
                    state <= BlitDone;
                end else begin
                    cur_yi <= cur_yi + 1;
                    state  <= next;
                end
            end else begin
                cur_xi <= cur_xi + 1;
                state  <= next;
            end
        end else begin
            state <= BlitDone;
        end
    endrule

    // CPU Transfer (HMMC / LMMC)
    rule blit_transfer_start (state == BlitTransfer_WaitCPU && q_tr_in.notEmpty);
        let trVal = q_tr_in.first;
        q_tr_in.deq;

        Bit#(10) dy = stepY(reg_dy, cur_yi, diyNegative);
        Bit#(10) startDx = (reg_mode == 5'h1C) ? reg_dx :
                           (reg_mode == 5'h10) ? (reg_dx >> 2) : (reg_dx >> 1);
        Bit#(10) dx = stepX(startDx, cur_xi, dixNegative) & (linePitch - 1);
        Bit#(24) dAddr = vramBase + zeroExtend(dy) * zeroExtend(linePitch) + zeroExtend(dx);

        q_req.enq(BusReq {
            tag:   tagVDPCmd,
            addr:  dAddr,
            data:  trVal,
            write: True,
            is_io: False
        });
        state <= BlitTransfer_WaitWrite;
    endrule

    rule blit_transfer_wait (state == BlitTransfer_WaitWrite);
        let resp <- toGet(q_resp).get();
        if (cur_xi + 1 >= count_x) begin
            cur_xi <= 0;
            if (cur_yi + 1 >= count_y) begin
                state <= BlitDone;
            end else begin
                cur_yi <= cur_yi + 1;
                state  <= BlitTransfer_WaitCPU;
            end
        end else begin
            cur_xi <= cur_xi + 1;
            state  <= BlitTransfer_WaitCPU;
        end
    endrule

    rule blit_finish (state == BlitDone);
        state <= BlitIdle;
    endrule

    // Discard any transfer writes if not in transfer mode
    rule drop_unused_tr (state != BlitTransfer_WaitCPU && q_tr_in.notEmpty);
        q_tr_in.deq;
    endrule

    // ── Interface Methods ───────────────────────────────────────────────────

    method Action set_params(
        Bit#(10) sx, Bit#(10) sy,
        Bit#(10) dx, Bit#(10) dy,
        Bit#(10) nx, Bit#(10) ny,
        Bit#(8)  col, Bit#(8) arg, Bit#(8) cmd,
        Bit#(5)  mode
    );
        q_cmd_start.enq(BlitCmdReq {
            sx: sx, sy: sy,
            dx: dx, dy: dy,
            nx: nx, ny: ny,
            col: col, arg: arg, cmd: cmd,
            mode: mode
        });
    endmethod

    method Action cpu_transfer_write(Bit#(8) data);
        q_tr_in.enq(data);
    endmethod

    method Bool is_busy()     = (state != BlitIdle) || q_cmd_start.notEmpty;
    method Bool is_tr()       = (state == BlitTransfer_WaitCPU);
    method Bit#(8) point_color() = pt_color;

    interface Get vram_req  = toGet(q_req);
    interface Put vram_resp = toPut(q_resp);
endmodule: mkBlitter

endpackage: Blitter
