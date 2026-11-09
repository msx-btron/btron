/* verify/tests/test_compositor.c -- the shared compositor policy, on the host, deterministic clock. */
#include <stdio.h>
#include <stdint.h>
#include <btron/compositor.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

typedef struct { uint32_t now, cost, wait; int renders, fulls, presents, stop_after, events, idles, nested_try; compositor_t *c; } M;

static uint32_t m_now(void *x) { return ((M *)x)->now; }
static void m_render(void *x, int full) { M *m = x; m->renders++; m->fulls += full; m->now += m->cost;
                                          if (m->nested_try) { CHECK(comp_pass(m->c) == COMP_BUSY); } }
static uint32_t m_present(void *x) { M *m = x; m->presents++; return m->wait; }
static int m_drain(void *x) { M *m = x; if (m->stop_after && m->renders >= m->stop_after) return -1;
                              return m->events ? (m->events--, 1) : 0; }
static void m_idle(void *x) { ((M *)x)->idles++; }

static const btron_plane_cfg_t cfg = {
    .input_period_us = 1000, .ui_period_us = 8000, .panel_refresh_us = 100000,
    .full_convergence_us = 500000, .input_slices_max = 4,
    .ui_period_slow_us = 33000, .ui_slow_paint_us = 15000 };

int main(void)
{
    M m = { 0 };
    compositor_ops_t ops = { .ctx = &m, .now_us = m_now, .drain_events = m_drain,
                             .render = m_render, .present = m_present, .idle = m_idle };
    compositor_t c;
    m.c = &c;
    comp_init(&c, &ops, &cfg, 4000);

    CHECK(comp_evt_redraw(0, 0, 0, 0) == 1);
    CHECK(comp_evt_redraw(1, 0, 0, 0) == 0);
    CHECK(comp_evt_redraw(1, 1, 0, 0) == 1);
    CHECK(comp_evt_redraw(1, 0, 1, 0) == 1);
    CHECK(comp_evt_redraw(1, 0, 0, 1) == 1);

    /* first frame is owed in full, and respects the interval from init */
    m.now = 0; CHECK(comp_pass(&c) == COMP_IDLE);
    m.now = 8000; CHECK(comp_pass(&c) == COMP_PRESENTED); CHECK(m.fulls == 1);

    /* no damage => idle, never paints */
    m.now += 20000; CHECK(comp_pass(&c) == COMP_IDLE); CHECK(m.renders == 1);

    /* event damage paints, but not before the interval */
    m.events = 1; m.now += 100; CHECK(comp_pass(&c) == COMP_PRESENTED);
    m.events = 1; m.now += 100; CHECK(comp_pass(&c) == COMP_IDLE);       /* too soon, damage kept */
    m.now += 8000;               CHECK(comp_pass(&c) == COMP_PRESENTED);  /* owed damage lands     */
    CHECK(m.fulls == 1);                                                   /* partial, not full     */

    /* heavy paint engages the slow interval, present wait is not charged */
    m.cost = 20000; m.wait = 4000; m.events = 1; m.now += 8000;
    CHECK(comp_pass(&c) == COMP_PRESENTED);
    CHECK(c.ui.last_us == 16000u);
    CHECK(c.ui.over >= 1);
    m.cost = 100; m.wait = 0; m.events = 1; m.now += 9000;
    CHECK(comp_pass(&c) == COMP_IDLE);                  /* 9 ms < 33 ms slow interval */
    m.now += 25000; CHECK(comp_pass(&c) == COMP_PRESENTED);
    CHECK(c.ui.throttled >= 1);

    /* panel timer damages on its own; convergence asks for the whole canvas */
    m.now += 100000; { int f = m.fulls; CHECK(comp_pass(&c) == COMP_PRESENTED); (void)f; }
    m.now += 600000; { int f = m.fulls; CHECK(comp_pass(&c) == COMP_PRESENTED); CHECK(m.fulls == f + 1); }

    /* re-entry from inside render is refused and counted, never nested */
    m.nested_try = 1; m.events = 1; m.now += 40000;
    CHECK(comp_pass(&c) == COMP_PRESENTED); CHECK(c.ui.nested == 1); m.nested_try = 0;

    /* drain may stop the loop without painting */
    m.stop_after = m.renders; m.now += 40000; { int r = m.renders; CHECK(comp_pass(&c) == COMP_STOP); CHECK(m.renders == r); }

    /* 32-bit clock rollover */
    m.stop_after = 0; m.now = 0xFFFFFF00u; c.last_present = 0xFFFFC000u; c.last_panel = c.last_full = m.now;
    m.events = 1; CHECK(comp_pass(&c) == COMP_PRESENTED);

    printf(fails ? "compositor: %d FAILED\n" : "compositor: all passed\n", fails);
    return fails != 0;
}
