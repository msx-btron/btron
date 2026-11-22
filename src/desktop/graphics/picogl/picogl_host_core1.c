/*
 * picogl_host_core1.c — host consumer for the span ring.
 *
 * The picocalc image links pico_core1.c instead. This pthread runs the
 * same drain: it paints from the ring slot, then publishes tail. Apple
 * ld64 does not turn a missing weak into NULL, so these are strong.
 * state 0 means the thread is not up and the span paints inline.
 */
#include "picogl.h"

#include <pthread.h>
#include <sched.h>

static pthread_t s_thr;
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_cv = PTHREAD_COND_INITIALIZER;
static int s_ready;
static volatile int s_stop;
static int s_started;

static void *worker(void *arg)
{
    (void)arg;
    __atomic_store_n(&s_ready, 1, __ATOMIC_RELEASE);
    for (;;) {
        picogl_span_drain();
        pthread_mutex_lock(&s_mu);
        if (s_stop) {
            pthread_mutex_unlock(&s_mu);
            break;
        }
        if (picogl_span_finished() == picogl_span_posted())
            pthread_cond_wait(&s_cv, &s_mu);
        pthread_mutex_unlock(&s_mu);
    }
    s_ready = 0;
    return 0;
}

void pico_core1_launch(void)
{
    if (s_ready || s_started) return;
    s_started = 1;
    if (pthread_create(&s_thr, 0, worker, 0) != 0) {
        s_started = 0;
        return;
    }
    while (!__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE)) sched_yield();
}

void pico_core1_sev(void)
{
    pthread_mutex_lock(&s_mu);
    pthread_cond_signal(&s_cv);
    pthread_mutex_unlock(&s_mu);
}

void pico_core1_reap(void)
{
    int spins = 0;
    if (!__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE)) return;
    while (picogl_span_finished() != picogl_span_posted()) {
        pico_core1_sev();
        if (++spins > 10000000) return;
        sched_yield();
    }
}

int pico_core1_run_flush(void)
{
    if (!__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE)) return 0;
    pico_core1_reap();
    return 1;
}

int pico_core1_state(void)
{
    if (!__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE)) return 0;
    return picogl_span_queued() > 0 ? 2 : 1;
}

unsigned pico_core1_queued(void)
{
    return __atomic_load_n(&s_ready, __ATOMIC_ACQUIRE)
        ? (unsigned)picogl_span_queued() : 0u;
}

unsigned pico_core1_refused(void)
{
    return __atomic_load_n(&s_ready, __ATOMIC_ACQUIRE) ? picogl_span_refused() : 0u;
}
