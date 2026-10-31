/*
 * TopPhase1.bsv — Phase 1 Top-Level SoC: Clocks, Memory Arbiter & Crossbar.
 *
 * Exposes synchronous clock enables and arbitrated memory bus transactions
 * to the pure C99 Verilator testbench via direct FIFOF signals.
 */

package TopPhase1;

import MSXClocks::*;
import Bus::*;
import MemoryArbiter::*;
import GetPut::*;
import FIFOF::*;

interface TopPhase1;
    // Clock Ticks
    (* always_ready *) method Bool tick_28m();
    (* always_ready *) method Bool tick_7m();
    (* always_ready *) method Bool tick_3m58();
    (* always_ready *) method Bool tick_e6();
    (* always_ready *) method Bool tick_pcm();
    (* always_ready *) method Bool tick_audio();

    // CPU Client: enqueue a bus request
    method Action cpu_req(BusAddr addr, BusData data, Bool write, Bool is_io);

    // VDP Scan Client: enqueue a read request
    method Action vdp_scan_req(BusAddr addr);

    // VDP Cmd Client: enqueue a request
    method Action vdp_cmd_req(BusAddr addr, BusData data, Bool write);

    // Geo3D Client: enqueue a request
    method Action geo3d_req(BusAddr addr, BusData data, Bool write);

    // Outbound physical memory bus (to C99 memory model)
    (* always_ready *) method Bool      mem_req_valid();
    (* always_ready *) method BusAddr   mem_req_addr();
    (* always_ready *) method BusData   mem_req_data();
    (* always_ready *) method Bool      mem_req_write();
    (* always_ready *) method Bool      mem_req_is_io();
    (* always_ready *) method MasterTag mem_req_tag();
    method Action mem_req_deq();

    // Inbound memory response from C99 memory model
    method Action put_mem_resp(MasterTag tag, BusData data, Bool ready);

    // CPU response out
    (* always_ready *) method Bool    cpu_resp_valid();
    (* always_ready *) method BusData cpu_resp_data();
    method Action cpu_resp_deq();
endinterface: TopPhase1

(* synthesize *)
module mkTopPhase1(TopPhase1);
    MSXClocks     clocks  <- mkMSXClocks;
    MemoryArbiter arbiter <- mkMemoryArbiter;

    // Staging FIFO for the outbound bus requests (filled by arbiter)
    FIFOF#(BusReq)  q_out  <- mkFIFOF;
    // Staging FIFO for cpu responses
    FIFOF#(BusResp) q_cpu_resp <- mkFIFOF;

    // Forward arbiter's outbound request into our staging FIFO
    rule pump_arb_out;
        let req <- arbiter.get_bus_req();
        q_out.enq(req);
    endrule

    // Forward arbiter's CPU response into our staging FIFO
    rule pump_cpu_resp;
        let resp <- arbiter.cpu_resp.get();
        q_cpu_resp.enq(resp);
    endrule

    // Clock ticks
    method Bool tick_28m   = clocks.tick_28m;
    method Bool tick_7m    = clocks.tick_7m;
    method Bool tick_3m58  = clocks.tick_3m58;
    method Bool tick_e6    = clocks.tick_e6;
    method Bool tick_pcm   = clocks.tick_pcm;
    method Bool tick_audio = clocks.tick_audio;

    // Client Requests
    method Action cpu_req(BusAddr addr, BusData data, Bool write, Bool is_io);
        arbiter.cpu_req.put(BusReq { tag: tagCPU, addr: addr, data: data, write: write, is_io: is_io });
    endmethod

    method Action vdp_scan_req(BusAddr addr);
        arbiter.vdp_scan_req.put(BusReq { tag: tagVDPScan, addr: addr, data: 8'h00, write: False, is_io: False });
    endmethod

    method Action vdp_cmd_req(BusAddr addr, BusData data, Bool write);
        arbiter.vdp_cmd_req.put(BusReq { tag: tagVDPCmd, addr: addr, data: data, write: write, is_io: False });
    endmethod

    method Action geo3d_req(BusAddr addr, BusData data, Bool write);
        arbiter.geo3d_req.put(BusReq { tag: tagGeo3D, addr: addr, data: data, write: write, is_io: False });
    endmethod

    // Outbound Memory Request — directly from staging FIFO head
    method Bool mem_req_valid()  = q_out.notEmpty;
    method BusAddr   mem_req_addr()  = q_out.notEmpty ? q_out.first.addr  : 0;
    method BusData   mem_req_data()  = q_out.notEmpty ? q_out.first.data  : 0;
    method Bool      mem_req_write() = q_out.notEmpty ? q_out.first.write : False;
    method Bool      mem_req_is_io() = q_out.notEmpty ? q_out.first.is_io : False;
    method MasterTag mem_req_tag()   = q_out.notEmpty ? q_out.first.tag   : 0;
    method Action mem_req_deq() if (q_out.notEmpty);
        q_out.deq;
    endmethod

    // Inbound Memory Response
    method Action put_mem_resp(MasterTag tag, BusData data, Bool ready);
        arbiter.put_bus_resp(BusResp { tag: tag, data: data, ready: ready });
    endmethod

    // CPU Response
    method Bool cpu_resp_valid()  = q_cpu_resp.notEmpty;
    method BusData cpu_resp_data() = q_cpu_resp.notEmpty ? q_cpu_resp.first.data : 0;
    method Action cpu_resp_deq() if (q_cpu_resp.notEmpty);
        q_cpu_resp.deq;
    endmethod
endmodule: mkTopPhase1

endpackage: TopPhase1
