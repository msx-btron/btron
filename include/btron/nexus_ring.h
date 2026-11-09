/*
 * nexus_ring.h -- InterCore Nexus single-producer / single-consumer ring.
 *
 * One ring per producer, fixed power-of-two slot count, no heap, no lock,
 * no CAS (works on cores without ll/sc such as the R5900).  Slots are
 * write-once / read-once; ownership passes by index.  Non-Blocking Refusal
 * Law (PS2.txt 1.1): a full ring refuses and counts the refusal, it never
 * stalls the producer.
 *
 * Memory order: the producer publishes `head` after the slot is written, the
 * consumer publishes `tail` after the slot is read.  NEXUS_BARRIER() must be
 * a store/load ordering barrier on the target (MIPS `sync`, ARM `dmb ish`).
 */
#ifndef BTRON_NEXUS_RING_H
#define BTRON_NEXUS_RING_H

#include <stdint.h>

#ifndef NEXUS_BARRIER
# if defined(__mips__)
#  define NEXUS_BARRIER() __asm__ volatile("sync" ::: "memory")
# elif defined(__aarch64__)
#  define NEXUS_BARRIER() __asm__ volatile("dmb ish" ::: "memory")
# else
#  define NEXUS_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
# endif
#endif

/* Declare a ring type holding SLOTS entries of type T.  SLOTS must be 2^n. */
#define NEXUS_RING_DEFINE(NAME, T, SLOTS)                                      \
    typedef struct {                                                           \
        volatile uint32_t head;      /* producer-owned */                      \
        volatile uint32_t tail;      /* consumer-owned */                      \
        volatile uint32_t refused;   /* S_FULL count   */                      \
        T                 slot[SLOTS];                                         \
    } NAME;                                                                    \
    _Static_assert(((SLOTS) & ((SLOTS) - 1)) == 0, #NAME " slots not 2^n");    \
    /* 0 = posted, -1 = refused (counted) */                                   \
    static inline int NAME##_snd(NAME *r, const T *v) {                        \
        uint32_t h = r->head;                                                  \
        if ((uint32_t)(h - r->tail) >= (SLOTS)) { r->refused++; return -1; }   \
        r->slot[h & ((SLOTS) - 1)] = *v;                                       \
        NEXUS_BARRIER();                                                       \
        r->head = h + 1u;                                                      \
        return 0;                                                              \
    }                                                                          \
    /* 1 = received, 0 = R_EMPTY */                                            \
    static inline int NAME##_rcv(NAME *r, T *out) {                            \
        uint32_t t = r->tail;                                                  \
        if (t == r->head) return 0;                                            \
        NEXUS_BARRIER();                                                       \
        *out = r->slot[t & ((SLOTS) - 1)];                                     \
        NEXUS_BARRIER();                                                       \
        r->tail = t + 1u;                                                      \
        return 1;                                                              \
    }                                                                          \
    static inline uint32_t NAME##_len(const NAME *r) { return r->head - r->tail; }

#endif /* BTRON_NEXUS_RING_H */

