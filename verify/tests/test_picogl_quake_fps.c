/*
 * Host timing of one start-map frame. The number is this machine, not
 * the RP2040. The sim posts through the span ring and joins once.
 */
#include <stdio.h>
#include <time.h>

#include "start_sim.h"
#include "picogl.h"

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

int main(void)
{
    uint64_t t0, t1;
    int lit, tris, spans;
    start_sim_init();
    start_sim_render();
    picogl_reset_stats();
    t0 = now_us();
    start_sim_render();
    t1 = now_us();
    lit = start_sim_lit();
    tris = picogl_tri_n();
    spans = picogl_span_n();
    printf("quake host us=%llu tri=%d span=%d lit=%d core=%d refused=%u\n",
           (unsigned long long)(t1 - t0), tris, spans, lit,
           pico_core1_state(), pico_core1_refused());
    printf("quake host is not an RP2040 frame time\n");
    if (tris < 40 || lit < 1000 || pico_core1_state() == 0) return 1;
    return 0;
}
