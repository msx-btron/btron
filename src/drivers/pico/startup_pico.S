/*
 * startup_pico.S — Clean-room Cortex-M33 (RP2350) & Cortex-M0+ (RP2040) Startup
 *
 * Implements:
 *   - Vector table (.vectors)
 *   - Reset_Handler: cpsid i, Core 1 WFE park (real HW), SCB->VTOR setup,
 *     FPU enable (Cortex-M33), .data copy, .bss zero, jump to pico_kernel_entry
 *   - PendSV_Handler: PSP preemptive context switch (ARMv8-M & ARMv6-M)
 *   - SysTick_Handler -> pico_systick_isr
 *   - HardFault_Handler -> captures MSP/PSP exception frame -> pico_hardfault_c
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

    .syntax unified
    .thumb

/* ── Vector Table ──────────────────────────────────────────────────────── */
    .section .vectors, "a", %progbits
    .align 8
    .global __vectors_start__
__vectors_start__:
    .word __stack_top__          /*  0: Initial MSP                        */
    .word Reset_Handler          /*  1: Reset                              */
    .word NMI_Handler            /*  2: NMI                                */
    .word HardFault_Handler      /*  3: HardFault                          */
    .word Default_Handler        /*  4: MemManage (M33) / Reserved (M0+)   */
    .word Default_Handler        /*  5: BusFault  (M33) / Reserved (M0+)   */
    .word Default_Handler        /*  6: UsageFault(M33) / Reserved (M0+)   */
    .word Default_Handler        /*  7: SecureFault (M33) / Reserved       */
    .word 0                      /*  8: Reserved                           */
    .word 0                      /*  9: Reserved                           */
    .word 0                      /* 10: Reserved                           */
    .word SVC_Handler            /* 11: SVCall                             */
    .word Default_Handler        /* 12: DebugMonitor                       */
    .word 0                      /* 13: Reserved                           */
    .word PendSV_Handler         /* 14: PendSV (RTOS context switch)       */
    .word SysTick_Handler        /* 15: SysTick (1 kHz RTOS tick)          */
    /* External IRQs 0..31 */
    .rept 32
    .word Default_Handler
    .endr

/* ── Reset Handler ─────────────────────────────────────────────────────── */
    .section .text.Reset_Handler, "ax", %progbits
    .global Reset_Handler
    .type Reset_Handler, %function
    .thumb_func
Reset_Handler:
    cpsid i                      /* Disable IRQs (safe SD-boot handoff)    */

    /* Disable SysTick left running by PicoCalc Bootloader v1.0 */
    ldr r0, =0xE000E010
    movs r1, #0
    str r1, [r0]

    /* Disable & clear ALL pending NVIC external IRQs from bootloader! */
    ldr r0, =0xE000E180          /* NVIC->ICER[0] */
    movs r1, #0
    mvns r1, r1                  /* 0xFFFFFFFF */
    str r1, [r0]
    ldr r0, =0xE000E280          /* NVIC->ICPR[0] */
    str r1, [r0]
#if defined(__ARM_ARCH_8M_MAIN__)
    ldr r0, =0xE000E184          /* NVIC->ICER[1] (RP2350 IRQs 32..51) */
    str r1, [r0]
    ldr r0, =0xE000E284          /* NVIC->ICPR[1] */
    str r1, [r0]
#endif

    /* Clear any pending SysTick / PendSV exceptions in SCB->ICSR */
    ldr r0, =0xE000ED04
    ldr r1, =0x0A000000          /* PENDSTCLR (bit 25) | PENDSVCLR (bit 27) */
    str r1, [r0]

    /* Ensure Privileged Thread Mode using MSP (CONTROL = 0) */
    movs r0, #0
    msr control, r0
    isb

#if defined(PICO_REAL_HW)
    /* Park Core 1 in low-power WFE loop (SIO_CPUID @ 0xD0000000) */
    ldr r0, =0xD0000000
    ldr r0, [r0]
    cmp r0, #0
    beq 1f
.core1_park:
    wfe
    b .core1_park
1:
#endif

    /* Set MSP explicitly (required when launched from pico_multi_booter) */
    ldr r0, =__stack_top__
    msr msp, r0

    /* Point SCB->VTOR (0xE000ED08) to our vector table */
    ldr r0, =0xE000ED08
    ldr r1, =__vectors_start__
    str r1, [r0]
    dsb
    isb

#if defined(__ARM_ARCH_8M_MAIN__)
    /* Enable FPv5-SP FPU (CP10/CP11 full access in SCB->CPACR @ 0xE000ED88) */
    ldr r0, =0xE000ED88
    ldr r1, [r0]
    orr r1, r1, #(0xF << 20)
    str r1, [r0]
    dsb
    isb
#endif

    /* Copy .data from Flash (__data_load__) to SRAM (__data_start__..__data_end__) */
    ldr r0, =__data_start__
    ldr r1, =__data_end__
    ldr r2, =__data_load__
.copy_data:
    cmp r0, r1
    bhs .copy_data_done
    ldr r3, [r2]
    str r3, [r0]
    adds r0, r0, #4
    adds r2, r2, #4
    b .copy_data
.copy_data_done:

    /* Zero .bss (__bss_start__..__bss_end__) */
    ldr r0, =__bss_start__
    ldr r1, =__bss_end__
    movs r2, #0
.zero_bss:
    cmp r0, r1
    bhs .zero_bss_done
    str r2, [r0]
    adds r0, r0, #4
    b .zero_bss
.zero_bss_done:

    /* Enter C kernel (never returns) */
    bl pico_kernel_entry
.halt_loop:
    wfi
    b .halt_loop
    .size Reset_Handler, . - Reset_Handler

/* ── PendSV Preemptive Context Switcher (PSP) ──────────────────────────── */
/*
 * On entry, hardware stacked {r0-r3, r12, lr, pc, xPSR} onto the active PSP
 * (or MSP on very first launch when PSP == 0).
 * We push {r4-r11} onto PSP (if PSP != 0), pass cur_psp in r0 to
 * pico_sched_switch(cur_psp), receive next_psp in r0, pop {r4-r11},
 * set PSP = next_psp, and return to Thread mode using PSP (EXC_RETURN=0xFFFFFFFD).
 */
    .section .text.PendSV_Handler, "ax", %progbits
    .global PendSV_Handler
    .type PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    cpsid i
    mrs r0, psp
    cmp r0, #0
    beq .pendsv_no_save

#if defined(__ARM_ARCH_8M_MAIN__)
    stmdb r0!, {r4-r11}
#else
    /* ARMv6-M (Cortex-M0+): save r4-r7 and r8-r11 using Thumb-1 */
    subs r0, r0, #32
    stmia r0!, {r4-r7}
    mov r4, r8
    mov r5, r9
    mov r6, r10
    mov r7, r11
    stmia r0!, {r4-r7}
    subs r0, r0, #32
#endif

.pendsv_no_save:
    /* Call C scheduler: r0 = pico_sched_switch(cur_psp) */
    push {lr}
    bl pico_sched_switch
    pop {r3}

#if defined(__ARM_ARCH_8M_MAIN__)
    ldmia r0!, {r4-r11}
#else
    /* ARMv6-M: restore r8-r11 then r4-r7 */
    adds r0, r0, #16
    ldmia r0!, {r4-r7}
    mov r8, r4
    mov r9, r5
    mov r10, r6
    mov r11, r7
    subs r0, r0, #32
    ldmia r0!, {r4-r7}
    adds r0, r0, #16
#endif

    msr psp, r0
    dsb
    isb
    cpsie i
    /* Return to Thread mode using PSP */
    ldr r0, =0xFFFFFFFD
    bx r0
    .size PendSV_Handler, . - PendSV_Handler

/* ── SysTick Handler ───────────────────────────────────────────────────── */
    .section .text.SysTick_Handler, "ax", %progbits
    .global SysTick_Handler
    .type SysTick_Handler, %function
    .thumb_func
SysTick_Handler:
    push {lr}
    bl pico_systick_isr
    pop {pc}
    .size SysTick_Handler, . - SysTick_Handler

/* ── HardFault Handler (Diagnostics) ───────────────────────────────────── */
    .section .text.HardFault_Handler, "ax", %progbits
    .global HardFault_Handler
    .type HardFault_Handler, %function
    .thumb_func
HardFault_Handler:
    movs r0, #4
    mov r1, lr
    tst r0, r1
    beq .hf_msp
    mrs r0, psp
    b .hf_call
.hf_msp:
    mrs r0, msp
.hf_call:
    bl pico_hardfault_c
.hf_spin:
    wfi
    b .hf_spin
    .size HardFault_Handler, . - HardFault_Handler

/* ── Weak Default Handlers ─────────────────────────────────────────────── */
    .section .text.Default_Handler, "ax", %progbits
    .weak NMI_Handler
    .weak SVC_Handler
    .global Default_Handler
    .type Default_Handler, %function
    .thumb_func
NMI_Handler:
SVC_Handler:
Default_Handler:
    wfi
    b Default_Handler
    .size Default_Handler, . - Default_Handler
