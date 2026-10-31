/*
 * mem_model.c — Pure C99 Physical Memory Model Implementation.
 */

#include "mem_model.h"
#include "vl_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mem_model_init(mem_model_t *m)
{
    if (!m) return;
    m->size = MEM_MODEL_SIZE;
    m->raw = (uint8_t *)malloc(m->size);
    if (m->raw) {
        memset(m->raw, 0xFF, m->size);
    }
    m->reads_serviced = 0;
    m->writes_serviced = 0;
}

void mem_model_destroy(mem_model_t *m)
{
    if (!m) return;
    if (m->raw) {
        free(m->raw);
        m->raw = NULL;
    }
}

void mem_model_reset(mem_model_t *m)
{
    if (!m || !m->raw) return;
    memset(m->raw, 0xFF, m->size);
    m->reads_serviced = 0;
    m->writes_serviced = 0;
}

bool mem_model_load_rom(mem_model_t *m, uint32_t base_addr, const char *path)
{
    FILE *f;
    size_t n;
    if (!m || !m->raw || !path || base_addr >= m->size) return false;
    f = fopen(path, "rb");
    if (!f) return false;
    n = fread(m->raw + base_addr, 1, m->size - base_addr, f);
    fclose(f);
    return (n > 0);
}

bool mem_model_load_buf(mem_model_t *m, uint32_t base_addr, const uint8_t *buf, size_t len)
{
    if (!m || !m->raw || !buf || base_addr + len > m->size) return false;
    memcpy(m->raw + base_addr, buf, len);
    return true;
}

uint8_t mem_model_read(mem_model_t *m, uint32_t addr)
{
    if (!m || !m->raw) return 0xFF;
    return m->raw[addr % m->size];
}

void mem_model_write(mem_model_t *m, uint32_t addr, uint8_t val)
{
    if (!m || !m->raw) return;
    m->raw[addr % m->size] = val;
}

bool mem_model_service(mem_model_t *m, void *vl_handle)
{
    vl_mem_req_t req;
    if (!m || !vl_handle) return false;
    if (!vl_has_mem_req(vl_handle)) return false;

    if (vl_get_mem_req(vl_handle, &req)) {
        if (req.write) {
            mem_model_write(m, req.addr, req.data);
            m->writes_serviced++;
            vl_put_mem_resp(vl_handle, req.tag, req.data, true);
        } else {
            uint8_t d = mem_model_read(m, req.addr);
            m->reads_serviced++;
            vl_put_mem_resp(vl_handle, req.tag, d, true);
        }
        return true;
    }
    return false;
}

