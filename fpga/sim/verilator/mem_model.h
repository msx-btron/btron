/*
 * mem_model.h — Pure C99 Physical Memory Model & ROM Loader.
 *
 * Implements a flat 16MB physical address space (0x000000 - 0xFFFFFF) and
 * services Verilated memory bus requests with zero-latency response.
 */

#ifndef _MEM_MODEL_H_
#define _MEM_MODEL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MEM_MODEL_SIZE (16u * 1024u * 1024u)

typedef struct {
    uint8_t *raw;
    size_t   size;
    uint32_t reads_serviced;
    uint32_t writes_serviced;
} mem_model_t;

void     mem_model_init(mem_model_t *m);
void     mem_model_destroy(mem_model_t *m);
void     mem_model_reset(mem_model_t *m);

bool     mem_model_load_rom(mem_model_t *m, uint32_t base_addr, const char *path);
bool     mem_model_load_buf(mem_model_t *m, uint32_t base_addr, const uint8_t *buf, size_t len);

uint8_t  mem_model_read(mem_model_t *m, uint32_t addr);
void     mem_model_write(mem_model_t *m, uint32_t addr, uint8_t val);

// Step service: checks for outbound memory request from hardware and injects response
bool     mem_model_service(mem_model_t *m, void *vl_handle);

#endif /* _MEM_MODEL_H_ */

