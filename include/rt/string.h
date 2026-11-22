#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <string.h>
#else
#ifndef _GL_STRING_H_
#define _GL_STRING_H_

#include <rt/libstr.h>

#define memset  tkl_memset
#define memcpy  tkl_memcpy
#define memmove tkl_memmove
#define memcmp  tkl_memcmp
#define strlen  tkl_strlen
#define strnlen tkl_strnlen
#define strcpy  tkl_strcpy
#define strncpy tkl_strncpy
#define strscpy tkl_strscpy
#define strscat tkl_strscat
#define strpad  tkl_strpad
#define strcmp  tkl_strcmp
#define strncmp tkl_strncmp
#define strcat  tkl_strcat
#define strncat tkl_strncat
#define strstr  tkl_strstr
#define strchr  tkl_strchr
#define strrchr tkl_strrchr
#define strpbrk tkl_strpbrk

extern void *Imalloc(size_t size);

static inline char *strdup(const char *s)
{
    size_t n;
    char *p;

    if (!s) return 0;
    n = tkl_strlen(s);
    if (n >= 1024u * 1024u) return 0;
    n += 1u;
    p = (char *)Imalloc(n);
    if (p) tkl_memcpy(p, s, n);
    return p;
}

static inline int strcasecmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && *s2) {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++; s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static inline int strncasecmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2 || n == 0) return 0;
    while (n-- > 0 && *s1 && *s2) {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++; s2++;
    }
    return 0;
}


/* B-RT. One declaration set. ISO names above stay the public calls. */
/* Scalar C99 string plane (tkl_* on rt.btron.pro).
   Bounded where a length is given. No locale, no heap. */

size_t      rt_strlen(const char *s);
size_t      rt_strnlen(const char *s, size_t n);

char       *rt_strcpy(char *dst, const char *src);
char       *rt_strncpy(char *dst, const char *src, size_t n);
char       *rt_strcat(char *dst, const char *src);
char       *rt_strncat(char *dst, const char *src, size_t n);

int         rt_strcmp(const char *a, const char *b);
int         rt_strncmp(const char *a, const char *b, size_t n);

char       *rt_strchr(const char *s, int c);
char       *rt_strrchr(const char *s, int c);
char       *rt_strstr(const char *hay, const char *needle);
char       *rt_strpbrk(const char *s, const char *accept);

size_t      rt_strscpy(char *dst, const char *src, size_t n);
size_t      rt_strscat(char *dst, const char *src, size_t n);

void        rt_strpad(char *dst, const char *src, size_t width, int fill);

size_t      rt_strlen_neon(const char *s);
int         rt_strcmp_neon(const char *a, const char *b);
char       *rt_strchr_neon(const char *s, int c);

/* RVV, one object per XLEN. A NUL scan uses a fault-only-first load.
   Bounded calls stop at n. memmove is not in this set. */
size_t      rt_strlen_rvv(const char *s);
size_t      rt_strnlen_rvv(const char *s, size_t n);
char       *rt_strcpy_rvv(char *dst, const char *src);
char       *rt_strncpy_rvv(char *dst, const char *src, size_t n);
char       *rt_strcat_rvv(char *dst, const char *src);
char       *rt_strncat_rvv(char *dst, const char *src, size_t n);
int         rt_strcmp_rvv(const char *a, const char *b);
int         rt_strncmp_rvv(const char *a, const char *b, size_t n);
char       *rt_strchr_rvv(const char *s, int c);
char       *rt_strrchr_rvv(const char *s, int c);
char       *rt_strstr_rvv(const char *hay, const char *needle);
char       *rt_strpbrk_rvv(const char *s, const char *accept);
size_t      rt_strscpy_rvv(char *dst, const char *src, size_t n);
size_t      rt_strscat_rvv(char *dst, const char *src, size_t n);
void        rt_strpad_rvv(char *dst, const char *src, size_t width, int fill);

/* Integer RV32 and RV64, one object per XLEN. A word load is aligned,
   so it stays inside one page. A word store contains no NUL.
   Bounded calls stop at n. Do not link the two objects together. */
size_t      rt_strlen_rv64(const char *s);
size_t      rt_strnlen_rv64(const char *s, size_t n);
char       *rt_strcpy_rv64(char *dst, const char *src);
char       *rt_strncpy_rv64(char *dst, const char *src, size_t n);
char       *rt_strcat_rv64(char *dst, const char *src);
char       *rt_strncat_rv64(char *dst, const char *src, size_t n);
int         rt_strcmp_rv64(const char *a, const char *b);
int         rt_strncmp_rv64(const char *a, const char *b, size_t n);
char       *rt_strchr_rv64(const char *s, int c);
char       *rt_strrchr_rv64(const char *s, int c);
char       *rt_strstr_rv64(const char *hay, const char *needle);
char       *rt_strpbrk_rv64(const char *s, const char *accept);
size_t      rt_strscpy_rv64(char *dst, const char *src, size_t n);
size_t      rt_strscat_rv64(char *dst, const char *src, size_t n);
void        rt_strpad_rv64(char *dst, const char *src, size_t width, int fill);

size_t      rt_strlen_rv32(const char *s);
size_t      rt_strnlen_rv32(const char *s, size_t n);
char       *rt_strcpy_rv32(char *dst, const char *src);
char       *rt_strncpy_rv32(char *dst, const char *src, size_t n);
char       *rt_strcat_rv32(char *dst, const char *src);
char       *rt_strncat_rv32(char *dst, const char *src, size_t n);
int         rt_strcmp_rv32(const char *a, const char *b);
int         rt_strncmp_rv32(const char *a, const char *b, size_t n);
char       *rt_strchr_rv32(const char *s, int c);
char       *rt_strrchr_rv32(const char *s, int c);
char       *rt_strstr_rv32(const char *hay, const char *needle);
char       *rt_strpbrk_rv32(const char *s, const char *accept);
size_t      rt_strscpy_rv32(char *dst, const char *src, size_t n);
size_t      rt_strscat_rv32(char *dst, const char *src, size_t n);
void        rt_strpad_rv32(char *dst, const char *src, size_t width, int fill);

/* R5900 LQ/SQ. A quad is stored only when it contains no NUL. */
size_t      rt_strlen_ee(const char *s);
size_t      rt_strnlen_ee(const char *s, size_t n);
char       *rt_strcpy_ee(char *dst, const char *src);
char       *rt_strncpy_ee(char *dst, const char *src, size_t n);
char       *rt_strcat_ee(char *dst, const char *src);
char       *rt_strncat_ee(char *dst, const char *src, size_t n);
int         rt_strcmp_ee(const char *a, const char *b);
int         rt_strncmp_ee(const char *a, const char *b, size_t n);
char       *rt_strchr_ee(const char *s, int c);
char       *rt_strrchr_ee(const char *s, int c);
char       *rt_strstr_ee(const char *hay, const char *needle);
char       *rt_strpbrk_ee(const char *s, const char *accept);
size_t      rt_strscpy_ee(char *dst, const char *src, size_t n);
size_t      rt_strscat_ee(char *dst, const char *src, size_t n);
void        rt_strpad_ee(char *dst, const char *src, size_t width, int fill);

/* VR4300 ld/sd. Not RSP vector microcode. */
size_t      rt_strlen_rcp(const char *s);
size_t      rt_strnlen_rcp(const char *s, size_t n);
char       *rt_strcpy_rcp(char *dst, const char *src);
char       *rt_strncpy_rcp(char *dst, const char *src, size_t n);
char       *rt_strcat_rcp(char *dst, const char *src);
char       *rt_strncat_rcp(char *dst, const char *src, size_t n);
int         rt_strcmp_rcp(const char *a, const char *b);
int         rt_strncmp_rcp(const char *a, const char *b, size_t n);
char       *rt_strchr_rcp(const char *s, int c);
char       *rt_strrchr_rcp(const char *s, int c);
char       *rt_strstr_rcp(const char *hay, const char *needle);
char       *rt_strpbrk_rcp(const char *s, const char *accept);
size_t      rt_strscpy_rcp(char *dst, const char *src, size_t n);
size_t      rt_strscat_rcp(char *dst, const char *src, size_t n);
void        rt_strpad_rcp(char *dst, const char *src, size_t width, int fill);

/* AArch32. One suffix per instruction stream, not per board.
   arm1176 is ARMv6. a32 is AArch32 NEON. m0 is Thumb-1.
   m3, m4, and m33 share a Thumb-2 integer body. m7 adds PLD.
   m55 is Helium. A word store never contains a NUL. */
size_t rt_strlen_arm1176(const char *s);
size_t rt_strnlen_arm1176(const char *s, size_t n);
char *rt_strcpy_arm1176(char *dst, const char *src);
char *rt_strncpy_arm1176(char *dst, const char *src, size_t n);
char *rt_strcat_arm1176(char *dst, const char *src);
char *rt_strncat_arm1176(char *dst, const char *src, size_t n);
int rt_strcmp_arm1176(const char *a, const char *b);
int rt_strncmp_arm1176(const char *a, const char *b, size_t n);
char *rt_strchr_arm1176(const char *s, int c);
char *rt_strrchr_arm1176(const char *s, int c);
char *rt_strstr_arm1176(const char *hay, const char *needle);
char *rt_strpbrk_arm1176(const char *s, const char *accept);
size_t rt_strscpy_arm1176(char *dst, const char *src, size_t n);
size_t rt_strscat_arm1176(char *dst, const char *src, size_t n);
void rt_strpad_arm1176(char *dst, const char *src, size_t width, int fill);

size_t rt_strlen_a32(const char *s);
size_t rt_strnlen_a32(const char *s, size_t n);
char *rt_strcpy_a32(char *dst, const char *src);
char *rt_strncpy_a32(char *dst, const char *src, size_t n);
char *rt_strcat_a32(char *dst, const char *src);
char *rt_strncat_a32(char *dst, const char *src, size_t n);
int rt_strcmp_a32(const char *a, const char *b);
int rt_strncmp_a32(const char *a, const char *b, size_t n);
char *rt_strchr_a32(const char *s, int c);
char *rt_strrchr_a32(const char *s, int c);
char *rt_strstr_a32(const char *hay, const char *needle);
char *rt_strpbrk_a32(const char *s, const char *accept);
size_t rt_strscpy_a32(char *dst, const char *src, size_t n);
size_t rt_strscat_a32(char *dst, const char *src, size_t n);
void rt_strpad_a32(char *dst, const char *src, size_t width, int fill);

size_t rt_strlen_m0(const char *s);
size_t rt_strnlen_m0(const char *s, size_t n);
char *rt_strcpy_m0(char *dst, const char *src);
char *rt_strncpy_m0(char *dst, const char *src, size_t n);
char *rt_strcat_m0(char *dst, const char *src);
char *rt_strncat_m0(char *dst, const char *src, size_t n);
int rt_strcmp_m0(const char *a, const char *b);
int rt_strncmp_m0(const char *a, const char *b, size_t n);
char *rt_strchr_m0(const char *s, int c);
char *rt_strrchr_m0(const char *s, int c);
char *rt_strstr_m0(const char *hay, const char *needle);
char *rt_strpbrk_m0(const char *s, const char *accept);
size_t rt_strscpy_m0(char *dst, const char *src, size_t n);
size_t rt_strscat_m0(char *dst, const char *src, size_t n);
void rt_strpad_m0(char *dst, const char *src, size_t width, int fill);

size_t rt_strlen_m3(const char *s);
size_t rt_strnlen_m3(const char *s, size_t n);
char *rt_strcpy_m3(char *dst, const char *src);
char *rt_strncpy_m3(char *dst, const char *src, size_t n);
char *rt_strcat_m3(char *dst, const char *src);
char *rt_strncat_m3(char *dst, const char *src, size_t n);
int rt_strcmp_m3(const char *a, const char *b);
int rt_strncmp_m3(const char *a, const char *b, size_t n);
char *rt_strchr_m3(const char *s, int c);
char *rt_strrchr_m3(const char *s, int c);
char *rt_strstr_m3(const char *hay, const char *needle);
char *rt_strpbrk_m3(const char *s, const char *accept);
size_t rt_strscpy_m3(char *dst, const char *src, size_t n);
size_t rt_strscat_m3(char *dst, const char *src, size_t n);
void rt_strpad_m3(char *dst, const char *src, size_t width, int fill);

size_t rt_strlen_m4(const char *s);
size_t rt_strnlen_m4(const char *s, size_t n);
char *rt_strcpy_m4(char *dst, const char *src);
char *rt_strncpy_m4(char *dst, const char *src, size_t n);
char *rt_strcat_m4(char *dst, const char *src);
char *rt_strncat_m4(char *dst, const char *src, size_t n);
int rt_strcmp_m4(const char *a, const char *b);
int rt_strncmp_m4(const char *a, const char *b, size_t n);
char *rt_strchr_m4(const char *s, int c);
char *rt_strrchr_m4(const char *s, int c);
char *rt_strstr_m4(const char *hay, const char *needle);
char *rt_strpbrk_m4(const char *s, const char *accept);
size_t rt_strscpy_m4(char *dst, const char *src, size_t n);
size_t rt_strscat_m4(char *dst, const char *src, size_t n);
void rt_strpad_m4(char *dst, const char *src, size_t width, int fill);

size_t rt_strlen_m7(const char *s);
size_t rt_strnlen_m7(const char *s, size_t n);
char *rt_strcpy_m7(char *dst, const char *src);
char *rt_strncpy_m7(char *dst, const char *src, size_t n);
char *rt_strcat_m7(char *dst, const char *src);
char *rt_strncat_m7(char *dst, const char *src, size_t n);
int rt_strcmp_m7(const char *a, const char *b);
int rt_strncmp_m7(const char *a, const char *b, size_t n);
char *rt_strchr_m7(const char *s, int c);
char *rt_strrchr_m7(const char *s, int c);
char *rt_strstr_m7(const char *hay, const char *needle);
char *rt_strpbrk_m7(const char *s, const char *accept);
size_t rt_strscpy_m7(char *dst, const char *src, size_t n);
size_t rt_strscat_m7(char *dst, const char *src, size_t n);
void rt_strpad_m7(char *dst, const char *src, size_t width, int fill);

size_t rt_strlen_m33(const char *s);
size_t rt_strnlen_m33(const char *s, size_t n);
char *rt_strcpy_m33(char *dst, const char *src);
char *rt_strncpy_m33(char *dst, const char *src, size_t n);
char *rt_strcat_m33(char *dst, const char *src);
char *rt_strncat_m33(char *dst, const char *src, size_t n);
int rt_strcmp_m33(const char *a, const char *b);
int rt_strncmp_m33(const char *a, const char *b, size_t n);
char *rt_strchr_m33(const char *s, int c);
char *rt_strrchr_m33(const char *s, int c);
char *rt_strstr_m33(const char *hay, const char *needle);
char *rt_strpbrk_m33(const char *s, const char *accept);
size_t rt_strscpy_m33(char *dst, const char *src, size_t n);
size_t rt_strscat_m33(char *dst, const char *src, size_t n);
void rt_strpad_m33(char *dst, const char *src, size_t width, int fill);

size_t rt_strlen_m55(const char *s);
size_t rt_strnlen_m55(const char *s, size_t n);
char *rt_strcpy_m55(char *dst, const char *src);
char *rt_strncpy_m55(char *dst, const char *src, size_t n);
char *rt_strcat_m55(char *dst, const char *src);
char *rt_strncat_m55(char *dst, const char *src, size_t n);
int rt_strcmp_m55(const char *a, const char *b);
int rt_strncmp_m55(const char *a, const char *b, size_t n);
char *rt_strchr_m55(const char *s, int c);
char *rt_strrchr_m55(const char *s, int c);
char *rt_strstr_m55(const char *hay, const char *needle);
char *rt_strpbrk_m55(const char *s, const char *accept);
size_t rt_strscpy_m55(char *dst, const char *src, size_t n);
size_t rt_strscat_m55(char *dst, const char *src, size_t n);
void rt_strpad_m55(char *dst, const char *src, size_t width, int fill);
#endif /* _GL_STRING_H_ */
#endif /* HOSTED */

