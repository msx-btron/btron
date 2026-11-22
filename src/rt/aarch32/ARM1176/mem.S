/* ARMv6 ARM state. BCM2835: Pi 1, Pi Zero, Pi Zero W, CM1.
   No NEON. LDM and STM only on aligned addresses. A mismatched
   source and destination alignment is a byte copy, so the kernel
   is correct whether or not SCTLR.A traps an unaligned LDR.
   Little-endian.
   memcpy is Arm Optimized Routines v25.01 string/arm/memcpy.S
   built without __ARM_NEON__. memset, strcmp, and strlen from that
   tag are Thumb-2 and are not linked here.
   https://github.com/ARM-software/optimized-routines/blob/v25.01/string/arm/memcpy.S
   Index: third_party/aarch32/SOURCES.md */
.syntax unified
.arch armv6
.cpu arm1176jzf-s
.arm

.global rt_memcpy_arm1176
.type rt_memcpy_arm1176, %function
rt_memcpy_arm1176:
    cmp r2, #0
    bxeq lr
    push {r4-r10, lr}
    mov ip, r0
    eor r3, r0, r1
    tst r3, #3
    bne .Lmc_b
.Lmc_peel:
    cmp r2, #0
    beq .Lmc_out
    tst r0, #3
    beq .Lmc_32
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    subs r2, r2, #1
    b .Lmc_peel
.Lmc_32:
    cmp r2, #32
    blo .Lmc_16
    pld [r1, #64]
    ldm r1!, {r3-r10}
    stm r0!, {r3-r10}
    subs r2, r2, #32
    b .Lmc_32
.Lmc_16:
    cmp r2, #16
    blo .Lmc_w
    ldm r1!, {r3-r6}
    stm r0!, {r3-r6}
    subs r2, r2, #16
    b .Lmc_16
.Lmc_w:
    cmp r2, #4
    blo .Lmc_b
    ldr r3, [r1], #4
    str r3, [r0], #4
    subs r2, r2, #4
    b .Lmc_w
.Lmc_b:
    cmp r2, #0
    beq .Lmc_out
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    subs r2, r2, #1
    b .Lmc_b
.Lmc_out:
    mov r0, ip
    pop {r4-r10, pc}

.global rt_memset_arm1176
.type rt_memset_arm1176, %function
rt_memset_arm1176:
    cmp r2, #0
    bxeq lr
    push {r4-r10, lr}
    mov ip, r0
    uxtb r1, r1
    orr r1, r1, r1, lsl #8
    orr r1, r1, r1, lsl #16
.Lms_peel:
    cmp r2, #0
    beq .Lms_out
    tst r0, #3
    beq .Lms_32
    strb r1, [r0], #1
    subs r2, r2, #1
    b .Lms_peel
.Lms_32:
    cmp r2, #32
    blo .Lms_16
    mov r3, r1
    mov r4, r1
    mov r5, r1
    mov r6, r1
    mov r7, r1
    mov r8, r1
    mov r9, r1
    mov r10, r1
.Lms_32s:
    stm r0!, {r3-r10}
    subs r2, r2, #32
    cmp r2, #32
    bhs .Lms_32s
.Lms_16:
    cmp r2, #16
    blo .Lms_w
    mov r3, r1
    mov r4, r1
    mov r5, r1
    mov r6, r1
.Lms_16s:
    stm r0!, {r3-r6}
    subs r2, r2, #16
    cmp r2, #16
    bhs .Lms_16s
.Lms_w:
    cmp r2, #4
    blo .Lms_b
    str r1, [r0], #4
    subs r2, r2, #4
    b .Lms_w
.Lms_b:
    cmp r2, #0
    beq .Lms_out
    strb r1, [r0], #1
    subs r2, r2, #1
    b .Lms_b
.Lms_out:
    mov r0, ip
    pop {r4-r10, pc}

.global rt_memcmp_arm1176
.type rt_memcmp_arm1176, %function
rt_memcmp_arm1176:
    push {r4, lr}
    cmp r2, #0
    beq .Lmp_0
    eor r3, r0, r1
    tst r3, #3
    bne .Lmp_b
.Lmp_peel:
    tst r0, #3
    beq .Lmp_w
    cmp r2, #0
    beq .Lmp_0
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    subs r2, r2, #1
    cmp r3, r4
    beq .Lmp_peel
    sub r0, r3, r4
    pop {r4, pc}
.Lmp_w:
    cmp r2, #4
    blo .Lmp_b
    ldr r3, [r0], #4
    ldr r4, [r1], #4
    cmp r3, r4
    bne .Lmp_rw
    subs r2, r2, #4
    b .Lmp_w
.Lmp_rw:
    sub r0, r0, #4
    sub r1, r1, #4
.Lmp_b:
    cmp r2, #0
    beq .Lmp_0
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    subs r2, r2, #1
    cmp r3, r4
    beq .Lmp_b
    sub r0, r3, r4
    pop {r4, pc}
.Lmp_0:
    mov r0, #0
    pop {r4, pc}
