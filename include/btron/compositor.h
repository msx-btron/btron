/*
 * compositor.h -- the one compositor policy shared by every core.
 *
 * Copies drift (ASYNC.txt: determinism is period + budget, so the policy must
 * be one thing).  This header is that one thing.  A core supplies only a
 * compositor_ops_t -- the parts that are genuinely machine-specific -- and
 * calls comp_pass() from its loop.
 *
 * Header-only and free of GDEV/GL types on purpose: the policy is host-testable
 * (verify/tests/test_compositor.c) and links into every target with no Makefile
 * source-list change.  All times are uint32 microseconds; every comparison is
 * a wrapping difference, so the ~71 min rollover is harmless.
 *
 * Ownership rule (GL vs. double buffer): render() draws into the BACK target
 * only and present() is the single place that exposes it.  comp_pass() never
 * calls render() re-entrantly: a nested pass returns COMP_BUSY and is counted.
 */
#ifndef BTRON_COMPOSITOR_H
#define BTRON_COMPOSITOR_H

#include <stdint.h>
#include <stddef.h>
#include <btron/async_rt.h>

/* comp_pass() results */
enum { COMP_IDLE = 0, COMP_PRESENTED = 1, COMP_STOP = 2, COMP_BUSY = 3 };

typedef struct {
    uint32_t runs;       /* presented frames                                    */
    uint32_t last_us;    /* last frame cost, present wait excluded              */
    uint32_t max_us;     /* worst frame cost since init                         */
    uint32_t over;       /* frames that exceeded the UI budget                  */
    uint32_t late;       /* frames started later than the active UI interval    */
    uint32_t throttled;  /* frames paced at the slow interval                   */
    uint32_t nested;     /* comp_pass() entered while a pass was running        */
} comp_stat_t;

typedef struct {
    void     *ctx;
    uint32_t (*now_us)(void *ctx);                /* required                              */
    void     (*poll_input)(void *ctx);            /* input plane; bounded; may be NULL     */
    int      (*drain_events)(void *ctx);          /* >0 repaint, 0 none, <0 stop; or NULL  */
    int      (*detect_damage)(void *ctx);         /* !=0 repaint (resize, invalid, pointer)*/
    void     (*render)(void *ctx, int full);      /* draw into BACK target; required       */
    uint32_t (*present)(void *ctx);               /* expose it; returns us to NOT charge   */
    void     (*idle)(void *ctx);                  /* nothing due: wait-for-interrupt etc.  */
} compositor_ops_t;

typedef struct {
    const compositor_ops_t *ops;
    const btron_plane_cfg_t *cfg;
    uint32_t budget_us;        /* UI budget the charge is compared with        */
    uint32_t last_present;     /* start of the previous presented frame         */
    uint32_t last_panel;       /* panel (clock) refresh epoch                   */
    uint32_t last_full;        /* whole-canvas convergence epoch                */
    uint32_t last_paint_us;    /* drives the slow-paint throttle                */
    int      pending;          /* damage owed                                   */
    int      full_pending;     /* damage owed is the whole canvas               */
    int      running;          /* re-entry latch                                */
    int      started;
    comp_stat_t ui;
} compositor_t;

static inline void comp_init(compositor_t *c, const compositor_ops_t *ops,
                             const btron_plane_cfg_t *cfg, uint32_t ui_budget_us)
{
    uint32_t now = ops->now_us(ops->ctx);
    *c = (compositor_t){ 0 };
    c->ops = ops;
    c->cfg = cfg;
    c->budget_us = ui_budget_us;
    c->last_present = c->last_panel = c->last_full = now;
    c->pending = c->full_pending = 1;     /* the first frame is always owed in full */
}

static inline void comp_damage(compositor_t *c)      { c->pending = 1; }
static inline void comp_damage_full(compositor_t *c) { c->pending = 1; c->full_pending = 1; }

/* Shared drain policy: a MOVE is not a repaint unless a button is held, a
 * window is being dragged/resized, or a menu is open.  Typed without EVT so
 * the host compositor test can call it. */
static inline int comp_evt_redraw(int is_move, unsigned button, int interacting, int menu)
{
    if (!is_move) return 1;
    return button != 0u || interacting || menu;
}

/* Active UI interval: the table's period, or its slow period after a heavy paint. */
static inline uint32_t comp_ui_interval(const compositor_t *c)
{
    const btron_plane_cfg_t *p = c->cfg;
    if (p->ui_slow_paint_us && p->ui_period_slow_us && c->last_paint_us > p->ui_slow_paint_us)
        return p->ui_period_slow_us;
    return p->ui_period_us;
}

/* One shared "has this period elapsed" test; re-arms *last when it has. */
static inline int comp_period_due(uint32_t now, uint32_t *last, uint32_t period_us)
{
    if (!period_us) return 0;                      /* 0 = disabled */
    if ((uint32_t)(now - *last) < period_us) return 0;
    *last = now;
    return 1;
}

/* Panel (clock rows go stale) and full-convergence safety net.  Returns bit0 panel, bit1 full.
 * Exposed separately because a core with band damage (PS2) applies them only while the pointer
 * is still; comp_pass() applies them unconditionally as plain damage. */
static inline unsigned comp_timers(compositor_t *c, uint32_t now)
{
    unsigned r = 0;
    if (comp_period_due(now, &c->last_panel, c->cfg->panel_refresh_us))      r |= 1u;
    if (comp_period_due(now, &c->last_full,  c->cfg->full_convergence_us))   r |= 2u;
    return r;
}

/* The loop body.  Order is the ASYNC.txt data path: input plane, events, damage, UI plane. */
static inline int comp_pass(compositor_t *c)
{
    const compositor_ops_t *o = c->ops;
    uint32_t now, interval;

    if (c->running) { c->ui.nested++; return COMP_BUSY; }
    c->running = 1;

    if (o->poll_input) o->poll_input(o->ctx);

    if (o->drain_events) {
        int r = o->drain_events(o->ctx);
        if (r < 0) { c->running = 0; return COMP_STOP; }
        if (r > 0) comp_damage(c);
    }
    if (o->detect_damage && o->detect_damage(o->ctx)) comp_damage(c);

    now = o->now_us(o->ctx);
    {
        unsigned t = comp_timers(c, now);
        if (t & 1u) comp_damage(c);
        if (t & 2u) comp_damage_full(c);
    }

    interval = comp_ui_interval(c);
    if (c->pending && (uint32_t)(now - c->last_present) >= interval) {
        const int full = c->full_pending;
        uint32_t wait = 0u, t1, cost;

        if (c->started && (uint32_t)(now - c->last_present) > interval + interval / 2u)
            c->ui.late++;
        if (interval == c->cfg->ui_period_slow_us && interval != c->cfg->ui_period_us)
            c->ui.throttled++;

        c->last_present = now;
        c->pending = c->full_pending = 0;       /* cleared first: render may re-damage */
        o->render(o->ctx, full);
        if (o->present) wait = o->present(o->ctx);
        t1 = o->now_us(o->ctx);

        cost = (uint32_t)(t1 - now);
        cost = cost > wait ? cost - wait : 0u;
        c->last_paint_us = cost;
        c->ui.last_us = cost;
        if (cost > c->ui.max_us) c->ui.max_us = cost;
        if (c->budget_us && cost > c->budget_us) c->ui.over++;
        c->ui.runs++;
        c->started = 1;
        c->running = 0;
        return COMP_PRESENTED;
    }

    c->running = 0;
    if (o->idle) o->idle(o->ctx);
    return COMP_IDLE;
}

#endif /* BTRON_COMPOSITOR_H */

