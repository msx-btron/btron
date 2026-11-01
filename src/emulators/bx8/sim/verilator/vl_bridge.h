/*
 * vl_bridge.h — Thin C-linkage Bridge to Verilated Hardware Core.
 *
 * Exposes a pure C99 interface for testbenches without any C++ types or headers.
 */

#ifndef _VL_BRIDGE_H_
#define _VL_BRIDGE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool tick_28m;
    bool tick_7m;
    bool tick_3m58;
    bool tick_e6;
    bool tick_pcm;
    bool tick_audio;
} vl_clocks_t;

typedef struct {
    uint32_t addr;
    uint8_t  data;
    bool     write;
    bool     is_io;
    uint8_t  tag;
} vl_mem_req_t;

void *vl_create(void);
void  vl_destroy(void *handle);
void  vl_reset(void *handle);
void  vl_step(void *handle);

// Clock tick query
void  vl_get_clocks(void *handle, vl_clocks_t *clks);

// Client bus requests into hardware arbiter
bool  vl_cpu_req(void *handle, uint32_t addr, uint8_t data, bool write, bool is_io);
bool  vl_vdp_scan_req(void *handle, uint32_t addr);
bool  vl_vdp_cmd_req(void *handle, uint32_t addr, uint8_t data, bool write);
bool  vl_geo3d_req(void *handle, uint32_t addr, uint8_t data, bool write);

// Outbound physical memory bus requests from arbiter
bool  vl_has_mem_req(void *handle);
bool  vl_get_mem_req(void *handle, vl_mem_req_t *req);

// Inbound physical memory bus response into arbiter
bool  vl_put_mem_resp(void *handle, uint8_t tag, uint8_t data, bool ready);

// CPU response from arbiter
bool  vl_has_cpu_resp(void *handle);
bool  vl_get_cpu_resp(void *handle, uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* _VL_BRIDGE_H_ */

