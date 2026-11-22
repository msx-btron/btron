/* ISO and tkl_* names for the Cortex-M0 B-RT plane.
   Each name is an alias of one implementation. There is no second copy.
   memmove is the scalar routine. Memory and string calls are the M0 assembly.
   snprintf and the integer converters are the scalar C routines.
   malloc stays out of this list: the Pico shell returns NULL from malloc,
   and T-Kernel Imalloc calls rt_malloc directly. */
.syntax unified
.cpu cortex-m0plus
.thumb

.macro rt_alias name, target
.section .text.\name, "ax", %progbits
.global \name
.type \name, %function
.thumb_func
\name:
    push {r4, lr}
    ldr r4, =\target
    blx r4
    pop {r4, pc}
    .ltorg
.endm

rt_alias memcpy, rt_memcpy_m0
rt_alias tkl_memcpy, rt_memcpy_m0
rt_alias memset, rt_memset_m0
rt_alias tkl_memset, rt_memset_m0
rt_alias memcmp, rt_memcmp_m0
rt_alias tkl_memcmp, rt_memcmp_m0
rt_alias memmove, rt_memmove_scalar
rt_alias tkl_memmove, rt_memmove_scalar

rt_alias strlen, rt_strlen_m0
rt_alias tkl_strlen, rt_strlen_m0
rt_alias strnlen, rt_strnlen_m0
rt_alias tkl_strnlen, rt_strnlen_m0
rt_alias strcpy, rt_strcpy_m0
rt_alias tkl_strcpy, rt_strcpy_m0
rt_alias strncpy, rt_strncpy_m0
rt_alias tkl_strncpy, rt_strncpy_m0
rt_alias strcat, rt_strcat_m0
rt_alias tkl_strcat, rt_strcat_m0
rt_alias strncat, rt_strncat_m0
rt_alias tkl_strncat, rt_strncat_m0
rt_alias strcmp, rt_strcmp_m0
rt_alias tkl_strcmp, rt_strcmp_m0
rt_alias strncmp, rt_strncmp_m0
rt_alias tkl_strncmp, rt_strncmp_m0
rt_alias strchr, rt_strchr_m0
rt_alias tkl_strchr, rt_strchr_m0
rt_alias strrchr, rt_strrchr_m0
rt_alias tkl_strrchr, rt_strrchr_m0
rt_alias strstr, rt_strstr_m0
rt_alias tkl_strstr, rt_strstr_m0
rt_alias strpbrk, rt_strpbrk_m0
rt_alias tkl_strpbrk, rt_strpbrk_m0
rt_alias strscpy, rt_strscpy_m0
rt_alias tkl_strscpy, rt_strscpy_m0
rt_alias strscat, rt_strscat_m0
rt_alias tkl_strscat, rt_strscat_m0
rt_alias strpad, rt_strpad_m0
rt_alias tkl_strpad, rt_strpad_m0

rt_alias snprintf, rt_snprintf
rt_alias tkl_snprintf, rt_snprintf
rt_alias vsnprintf, rt_vsnprintf
rt_alias tkl_vsnprintf, rt_vsnprintf

rt_alias atoi, rt_atoi
rt_alias tkl_atoi, rt_atoi
rt_alias strtoul, rt_strtoul
rt_alias tkl_strtoul, rt_strtoul
rt_alias strtoull, rt_strtoull
rt_alias tkl_strtoull, rt_strtoull
rt_alias qsort, rt_qsort
rt_alias abs, rt_abs
