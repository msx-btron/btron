/* verify/tests/test_nexus_ring.c -- SPSC Nexus ring gate: refusal law, order, wrap, no loss. */
#include <stdio.h>
#include <stdint.h>
#include "../../include/btron/nexus_ring.h"

typedef struct { uint32_t type, v; } ev_t;
NEXUS_RING_DEFINE(ev_ring_t, ev_t, 8)

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    static ev_ring_t r;
    ev_t e, o;

    CHECK(ev_ring_t_rcv(&r, &o) == 0);                 /* R_EMPTY */
    for (uint32_t i = 0; i < 8; i++) { e.type = 1; e.v = i; CHECK(ev_ring_t_snd(&r, &e) == 0); }
    e.v = 99;
    CHECK(ev_ring_t_snd(&r, &e) == -1);                /* S_FULL, never stalls */
    CHECK(r.refused == 1);
    for (uint32_t i = 0; i < 8; i++) { CHECK(ev_ring_t_rcv(&r, &o) == 1); CHECK(o.v == i); }
    CHECK(ev_ring_t_rcv(&r, &o) == 0);

    /* wrap across 32-bit index overflow */
    r.head = r.tail = 0xFFFFFFFCu;
    for (uint32_t i = 0; i < 100; i++) {
        e.v = i; CHECK(ev_ring_t_snd(&r, &e) == 0);
        CHECK(ev_ring_t_rcv(&r, &o) == 1); CHECK(o.v == i);
    }
    CHECK(ev_ring_t_len(&r) == 0);

    printf(fails ? "nexus_ring: %d FAILED\n" : "nexus_ring: all passed\n", fails);
    return fails != 0;
}

