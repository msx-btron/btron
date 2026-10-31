/*
 * MemoryArbiter.bsv — 4-Port Priority Crossbar Memory Arbiter.
 *
 * Arbitrates between CPU, VDP Display Scanout, VDP Blitter, and Geo3D Coprocessor.
 * Routes physical memory requests (RAM, VRAM, ROM, I/O) to the external memory
 * controller / Verilator C99 memory model with matching master tags.
 */

package MemoryArbiter;

import FIFOF::*;
import GetPut::*;
import Bus::*;

interface MemoryArbiter;
    // Client 0: CPU Core
    interface Put#(BusReq)  cpu_req;
    interface Get#(BusResp) cpu_resp;

    // Client 1: VDP Scanout (highest priority for pixel streaming)
    interface Put#(BusReq)  vdp_scan_req;
    interface Get#(BusResp) vdp_scan_resp;

    // Client 2: VDP Command Engine / Blitter
    interface Put#(BusReq)  vdp_cmd_req;
    interface Get#(BusResp) vdp_cmd_resp;

    // Client 3: Geo3D Coprocessor
    interface Put#(BusReq)  geo3d_req;
    interface Get#(BusResp) geo3d_resp;

    // Outbound Physical Bus (to SDRAM / BRAM / C99 Memory Model)
    method ActionValue#(BusReq) get_bus_req();
    method Action               put_bus_resp(BusResp resp);
endinterface: MemoryArbiter

(* synthesize *)
module mkMemoryArbiter(MemoryArbiter);
    FIFOF#(BusReq)  q_cpu_req      <- mkFIFOF;
    FIFOF#(BusResp) q_cpu_resp     <- mkFIFOF;

    FIFOF#(BusReq)  q_scan_req     <- mkFIFOF;
    FIFOF#(BusResp) q_scan_resp    <- mkFIFOF;

    FIFOF#(BusReq)  q_cmd_req      <- mkFIFOF;
    FIFOF#(BusResp) q_cmd_resp     <- mkFIFOF;

    FIFOF#(BusReq)  q_geo_req      <- mkFIFOF;
    FIFOF#(BusResp) q_geo_resp     <- mkFIFOF;

    FIFOF#(BusReq)  q_bus_out      <- mkFIFOF;

    // Round-robin counter for fair arbitration among non-scan clients
    Reg#(Bit#(2)) rr_grant <- mkReg(0);

    // Arbitration Rule: VDP Scanout has strict priority
    rule arbitrate_requests;
        if (q_scan_req.notEmpty) begin
            let req = q_scan_req.first;
            q_scan_req.deq;
            q_bus_out.enq(req);
        end
        else if (rr_grant == 2'd0 && q_cpu_req.notEmpty) begin
            let req = q_cpu_req.first;
            q_cpu_req.deq;
            q_bus_out.enq(req);
            rr_grant <= 2'd1;
        end
        else if (rr_grant == 2'd1 && q_cmd_req.notEmpty) begin
            let req = q_cmd_req.first;
            q_cmd_req.deq;
            q_bus_out.enq(req);
            rr_grant <= 2'd2;
        end
        else if (rr_grant == 2'd2 && q_geo_req.notEmpty) begin
            let req = q_geo_req.first;
            q_geo_req.deq;
            q_bus_out.enq(req);
            rr_grant <= 2'd0;
        end
        else begin
            // Advance round-robin if the granted client is idle
            if (q_cpu_req.notEmpty) begin
                let req = q_cpu_req.first;
                q_cpu_req.deq;
                q_bus_out.enq(req);
                rr_grant <= 2'd1;
            end
            else if (q_cmd_req.notEmpty) begin
                let req = q_cmd_req.first;
                q_cmd_req.deq;
                q_bus_out.enq(req);
                rr_grant <= 2'd2;
            end
            else if (q_geo_req.notEmpty) begin
                let req = q_geo_req.first;
                q_geo_req.deq;
                q_bus_out.enq(req);
                rr_grant <= 2'd0;
            end
        end
    endrule

    // Client Interfaces
    interface Put cpu_req       = toPut(q_cpu_req);
    interface Get cpu_resp      = toGet(q_cpu_resp);

    interface Put vdp_scan_req  = toPut(q_scan_req);
    interface Get vdp_scan_resp = toGet(q_scan_resp);

    interface Put vdp_cmd_req   = toPut(q_cmd_req);
    interface Get vdp_cmd_resp  = toGet(q_cmd_resp);

    interface Put geo3d_req     = toPut(q_geo_req);
    interface Get geo3d_resp    = toGet(q_geo_resp);

    // Outbound Bus Methods
    method ActionValue#(BusReq) get_bus_req();
        let r = q_bus_out.first;
        q_bus_out.deq;
        return r;
    endmethod

    method Action put_bus_resp(BusResp resp);
        if (resp.tag == tagCPU) begin
            q_cpu_resp.enq(resp);
        end
        else if (resp.tag == tagVDPScan) begin
            q_scan_resp.enq(resp);
        end
        else if (resp.tag == tagVDPCmd) begin
            q_cmd_resp.enq(resp);
        end
        else if (resp.tag == tagGeo3D) begin
            q_geo_resp.enq(resp);
        end
    endmethod
endmodule: mkMemoryArbiter

endpackage: MemoryArbiter
