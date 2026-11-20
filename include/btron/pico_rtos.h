/*
 * pico_rtos.h — Clean-room Cortex-M RTOS Core API (core_pico.c)
 *
 * Static, zero-heap, NASA JPL Rule 3 compliant preemptive kernel for
 * ClockworkPi PicoCalc (RP2350 Cortex-M33 / RP2040 Cortex-M0+) and QEMU.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_PICO_RTOS_H_
#define _BTRON_PICO_RTOS_H_

#include <stdint.h>
#include <stddef.h>

#define PICO_MAX_TASKS          8
#if defined(PICO_REAL_HW)
#define PICO_TASK_STACK_WORDS  1024u   /* 4 KiB per task (headroom for TinyGL 3D) */
#else
#define PICO_TASK_STACK_WORDS   512u   /* 2 KiB per task (8 * 2 KiB = 16 KiB) */
#endif

typedef enum {
    PICO_TSK_UNUSED  = 0,
    PICO_TSK_READY   = 1,
    PICO_TSK_RUNNING = 2,
    PICO_TSK_DELAYED = 3,
    PICO_TSK_WAITSEM = 4,
    PICO_TSK_WAITFLG = 5,
    PICO_TSK_DORMANT = 6
} PICO_TSK_STATE;

typedef struct {
    volatile int32_t count;
    int32_t          max_count;
} PICO_SEM;

typedef struct {
    volatile uint32_t bits;
} PICO_FLG;

typedef void (*pico_task_fn)(void *arg);
typedef void (*pico_out_fn)(const char *s, void *ctx);

/* Task & timing primitives */
int      pico_tsk_create(const char *name, uint8_t prio, pico_task_fn fn, void *arg);
void     pico_tsk_delay(uint32_t ms);
void     pico_tsk_yield(void);
uint32_t pico_ticks_ms(void);
uint32_t pico_idle_wfi_count(void);

/* IPC primitives */
void     pico_sem_init(PICO_SEM *sem, int32_t init_count, int32_t max_count);
int      pico_sem_wait(PICO_SEM *sem, uint32_t timeout_ms);
void     pico_sem_signal(PICO_SEM *sem);

void     pico_flg_init(PICO_FLG *flg, uint32_t init_bits);
void     pico_flg_set(PICO_FLG *flg, uint32_t pattern);
uint32_t pico_flg_wait(PICO_FLG *flg, uint32_t mask, int clear_on_exit, uint32_t timeout_ms);

/* Diagnostics & shell reuse (shared between UART0 and PicoCalc Terminal app) */
uint32_t pico_static_sram_used(void);
int      pico_active_task_count(void);
void     pico_shell_exec(const char *line, pico_out_fn out, void *ctx);

/* Minimal freestanding formatting helpers (no libc printf) */
void     pico_u32_to_dec(uint32_t v, char *buf, size_t cap);
void     pico_u32_to_hex(uint32_t v, char *buf, size_t cap);

#endif /* _BTRON_PICO_RTOS_H_ */
