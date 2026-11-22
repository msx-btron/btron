/* ARMv6-M strings for Cortex-M0, M0+, M1, and M23. Thumb-1 only.
   A word load requires an aligned address that still has four bytes
   inside the current 4 KiB page.
   No post-index, so every step is a separate add.
   strlen: https://github.com/picolibc/picolibc/blob/1.8.10/newlib/libc/machine/arm/strlen-thumb1-Os.S
   strcmp: https://github.com/picolibc/picolibc/blob/1.8.10/newlib/libc/machine/arm/strcmp.S
   Index: third_party/aarch32/SOURCES.md */
.syntax unified
.cpu cortex-m0
.thumb

.global rt_strlen_m0
.type rt_strlen_m0, %function
.thumb_func
rt_strlen_m0:
    push {r4-r7, lr}
    mov r4, r0
    ldr r5, =0x01010101
    ldr r6, =0x80808080
.Lla:
    movs r3, #3
    ands r3, r0
    beq .Llw
    ldrb r3, [r0]
    cmp r3, #0
    beq .Lld
    adds r0, r0, #1
    b .Lla
.Llw:
    mov r3, r0
    lsrs r3, r3, #12
    lsls r3, r3, #12
    mov r2, r0
    subs r2, r2, r3
    adds r2, r2, #3
    lsrs r2, r2, #12
    bne .Llb
    ldr r3, [r0]
    mov r1, r3
    subs r1, r1, r5
    mvns r2, r3
    ands r1, r2
    ands r1, r6
    bne .Llb
    adds r0, r0, #4
    b .Llw
.Llb:
    ldrb r3, [r0]
    cmp r3, #0
    beq .Lld
    adds r0, r0, #1
    b .Llb
.Lld:
    subs r0, r0, r4
    pop {r4-r7, pc}
.ltorg

.global rt_strnlen_m0
.type rt_strnlen_m0, %function
.thumb_func
rt_strnlen_m0:
    push {r4, lr}
    movs r2, #0
.Lnl:
    cmp r2, r1
    beq .Lnd
    ldrb r3, [r0, r2]
    cmp r3, #0
    beq .Lnd
    adds r2, r2, #1
    b .Lnl
.Lnd:
    mov r0, r2
    pop {r4, pc}

.global rt_strcpy_m0
.type rt_strcpy_m0, %function
.thumb_func
rt_strcpy_m0:
    push {r4-r7, lr}
    mov r4, r0
    ldr r5, =0x01010101
    ldr r6, =0x80808080
.Lca:
    movs r2, #3
    mov r3, r0
    eors r3, r1
    ands r3, r2
    bne .Lcb
    mov r3, r0
    ands r3, r2
    beq .Lcw
    ldrb r3, [r1]
    strb r3, [r0]
    adds r0, r0, #1
    adds r1, r1, #1
    cmp r3, #0
    beq .Lcd
    b .Lca
.Lcw:
    mov r3, r1
    lsrs r3, r3, #12
    lsls r3, r3, #12
    mov r2, r1
    subs r2, r2, r3
    adds r2, r2, #3
    lsrs r2, r2, #12
    bne .Lcb
    ldr r3, [r1]
    mov r12, r3
    mov r2, r3
    subs r2, r2, r5
    mvns r3, r3
    ands r2, r3
    ands r2, r6
    bne .Lcb
    mov r3, r12
    str r3, [r0]
    adds r0, r0, #4
    adds r1, r1, #4
    b .Lcw
.Lcb:
    ldrb r3, [r1]
    strb r3, [r0]
    adds r0, r0, #1
    adds r1, r1, #1
    cmp r3, #0
    bne .Lcb
.Lcd:
    mov r0, r4
    pop {r4-r7, pc}
.ltorg

.global rt_strncpy_m0
.type rt_strncpy_m0, %function
.thumb_func
rt_strncpy_m0:
    push {r4, lr}
    mov r4, r0
.Lny:
    cmp r2, #0
    beq .Lnyd
    ldrb r3, [r1]
    cmp r3, #0
    beq .Lnyp
    strb r3, [r0]
    adds r0, r0, #1
    adds r1, r1, #1
    subs r2, r2, #1
    b .Lny
.Lnyp:
    movs r3, #0
    strb r3, [r0]
    adds r0, r0, #1
    subs r2, r2, #1
    b .Lny
.Lnyd:
    mov r0, r4
    pop {r4, pc}

.global rt_strcat_m0
.type rt_strcat_m0, %function
.thumb_func
rt_strcat_m0:
    push {r4, r5, lr}
    mov r4, r0
    mov r5, r1
    bl rt_strlen_m0
    adds r0, r4, r0
    mov r1, r5
    bl rt_strcpy_m0
    mov r0, r4
    pop {r4, r5, pc}

.global rt_strncat_m0
.type rt_strncat_m0, %function
.thumb_func
rt_strncat_m0:
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    bl rt_strlen_m0
    adds r7, r4, r0
    movs r3, #0
.Lnca:
    cmp r3, r6
    beq .Lncb
    ldrb r2, [r5, r3]
    cmp r2, #0
    beq .Lncb
    strb r2, [r7, r3]
    adds r3, r3, #1
    b .Lnca
.Lncb:
    movs r2, #0
    strb r2, [r7, r3]
    mov r0, r4
    pop {r4-r7, pc}

.global rt_strcmp_m0
.type rt_strcmp_m0, %function
.thumb_func
rt_strcmp_m0:
    push {r4-r7, lr}
    ldr r5, =0x01010101
    ldr r6, =0x80808080
.Lpa:
    movs r2, #3
    mov r3, r0
    eors r3, r1
    ands r3, r2
    bne .Lpb
    mov r3, r0
    ands r3, r2
    beq .Lpw
    ldrb r3, [r0]
    ldrb r4, [r1]
    adds r0, r0, #1
    adds r1, r1, #1
    cmp r3, r4
    bne .Lpd
    cmp r3, #0
    beq .Lp0
    b .Lpa
.Lpw:
    mov r3, r0
    lsrs r3, r3, #12
    lsls r3, r3, #12
    mov r2, r0
    subs r2, r2, r3
    adds r2, r2, #3
    lsrs r2, r2, #12
    bne .Lpb
    mov r3, r1
    lsrs r3, r3, #12
    lsls r3, r3, #12
    mov r2, r1
    subs r2, r2, r3
    adds r2, r2, #3
    lsrs r2, r2, #12
    bne .Lpb
    ldr r4, [r0]
    ldr r3, [r1]
    cmp r4, r3
    bne .Lpb
    mov r12, r4
    mov r2, r4
    subs r2, r2, r5
    mvns r4, r4
    ands r2, r4
    ands r2, r6
    bne .Lpb
    adds r0, r0, #4
    adds r1, r1, #4
    b .Lpa
.Lpb:
    ldrb r3, [r0]
    ldrb r4, [r1]
    adds r0, r0, #1
    adds r1, r1, #1
    cmp r3, r4
    bne .Lpd
    cmp r3, #0
    bne .Lpb
.Lp0:
    movs r0, #0
    pop {r4-r7, pc}
.Lpd:
    subs r0, r3, r4
    pop {r4-r7, pc}
.ltorg

.global rt_strncmp_m0
.type rt_strncmp_m0, %function
.thumb_func
rt_strncmp_m0:
    push {r4, lr}
    cmp r2, #0
    beq .Lnp0
.Lnpa:
    ldrb r3, [r0]
    ldrb r4, [r1]
    adds r0, r0, #1
    adds r1, r1, #1
    subs r2, r2, #1
    cmp r3, r4
    bne .Lnpd
    cmp r3, #0
    beq .Lnp0
    cmp r2, #0
    bne .Lnpa
.Lnp0:
    movs r0, #0
    pop {r4, pc}
.Lnpd:
    subs r0, r3, r4
    pop {r4, pc}

.global rt_strchr_m0
.type rt_strchr_m0, %function
.thumb_func
rt_strchr_m0:
    push {r4-r7, lr}
    uxtb r1, r1
    mov r4, r1
    lsls r3, r1, #8
    orrs r4, r3
    lsls r3, r4, #16
    orrs r4, r3
    ldr r5, =0x01010101
    ldr r6, =0x80808080
.Lha:
    movs r3, #3
    ands r3, r0
    bne .Lhb
    mov r3, r0
    lsrs r3, r3, #12
    lsls r3, r3, #12
    mov r2, r0
    subs r2, r2, r3
    adds r2, r2, #3
    lsrs r2, r2, #12
    bne .Lhb
    ldr r3, [r0]
    mov r12, r3
    mov r2, r3
    subs r2, r2, r5
    mvns r7, r3
    ands r2, r7
    ands r2, r6
    bne .Lhb
    mov r3, r12
    eors r3, r4
    mov r2, r3
    subs r2, r2, r5
    mvns r7, r3
    ands r2, r7
    ands r2, r6
    bne .Lhb
    adds r0, r0, #4
    b .Lha
.Lhb:
    ldrb r3, [r0]
    cmp r3, r1
    beq .Lhd
    cmp r3, #0
    beq .Lhn
    adds r0, r0, #1
    b .Lhb
.Lhd:
    pop {r4-r7, pc}
.Lhn:
    movs r0, #0
    pop {r4-r7, pc}
.ltorg

.global rt_strrchr_m0
.type rt_strrchr_m0, %function
.thumb_func
rt_strrchr_m0:
    push {r4, lr}
    uxtb r1, r1
    movs r2, #0
.Lrra:
    ldrb r3, [r0]
    cmp r3, r1
    bne .Lrrb
    mov r2, r0
.Lrrb:
    cmp r3, #0
    beq .Lrrd
    adds r0, r0, #1
    b .Lrra
.Lrrd:
    mov r0, r2
    pop {r4, pc}

.global rt_strstr_m0
.type rt_strstr_m0, %function
.thumb_func
rt_strstr_m0:
    ldrb r3, [r1]
    cmp r3, #0
    bne .Lsa
    bx lr
.Lsa:
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
.Lsb:
    ldrb r3, [r4]
    cmp r3, #0
    beq .Lsf
    ldrb r6, [r5]
    cmp r3, r6
    bne .Lse
    mov r0, r4
    mov r1, r5
.Lsc:
    ldrb r6, [r1]
    cmp r6, #0
    beq .Lsd
    ldrb r7, [r0]
    cmp r7, r6
    bne .Lse
    adds r0, r0, #1
    adds r1, r1, #1
    b .Lsc
.Lsd:
    mov r0, r4
    pop {r4-r7, pc}
.Lse:
    adds r4, r4, #1
    b .Lsb
.Lsf:
    movs r0, #0
    pop {r4-r7, pc}

.global rt_strpbrk_m0
.type rt_strpbrk_m0, %function
.thumb_func
rt_strpbrk_m0:
    ldrb r3, [r1]
    cmp r3, #0
    bne .Lba
    movs r0, #0
    bx lr
.Lba:
    push {r4-r6, lr}
    mov r4, r0
    mov r5, r1
.Lbb:
    ldrb r6, [r4]
    cmp r6, #0
    beq .Lbe
    mov r1, r5
.Lbc:
    ldrb r3, [r1]
    cmp r3, #0
    beq .Lbd
    cmp r6, r3
    beq .Lbf
    adds r1, r1, #1
    b .Lbc
.Lbd:
    adds r4, r4, #1
    b .Lbb
.Lbe:
    movs r0, #0
    pop {r4-r6, pc}
.Lbf:
    mov r0, r4
    pop {r4-r6, pc}

.global rt_strscpy_m0
.type rt_strscpy_m0, %function
.thumb_func
rt_strscpy_m0:
    cmp r2, #0
    bne .Lsy
    push {lr}
    mov r0, r1
    bl rt_strlen_m0
    pop {pc}
.Lsy:
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    mov r0, r1
    bl rt_strlen_m0
    mov r7, r0
    subs r6, r6, #1
    cmp r7, r6
    bhi .Lsyb
    mov r6, r7
.Lsyb:
    mov r0, r4
    mov r1, r5
    mov r2, r6
    bl rt_memcpy_m0
    movs r3, #0
    strb r3, [r4, r6]
    mov r0, r7
    pop {r4-r7, pc}

.global rt_strscat_m0
.type rt_strscat_m0, %function
.thumb_func
rt_strscat_m0:
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    mov r1, r2
    bl rt_strnlen_m0
    mov r7, r0
    cmp r7, r6
    blo .Lsta
    mov r0, r5
    bl rt_strlen_m0
    adds r0, r7, r0
    pop {r4-r7, pc}
.Lsta:
    adds r0, r4, r7
    mov r1, r5
    subs r2, r6, r7
    bl rt_strscpy_m0
    adds r0, r7, r0
    pop {r4-r7, pc}

.global rt_strpad_m0
.type rt_strpad_m0, %function
.thumb_func
rt_strpad_m0:
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    uxtb r7, r3
    mov r0, r5
    mov r1, r6
    bl rt_strnlen_m0
    mov r2, r0
    push {r2}
    mov r0, r4
    mov r1, r5
    bl rt_memcpy_m0
    pop {r2}
    cmp r2, r6
    bhs .Ldd
    subs r3, r6, r2
    adds r0, r4, r2
    mov r1, r7
    mov r2, r3
    bl rt_memset_m0
.Ldd:
    pop {r4-r7, pc}
