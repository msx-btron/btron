#include "rt/stdio.h"
#include "rt/string.h"

#include <stdint.h>

/* Reverse-digit emitter (sn_put_rev on the site). */
static int put_rev(char *buf, size_t cap, size_t *pos, unsigned long long v, int base, int upper)
{
    char tmp[32];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0)
        tmp[n++] = '0';
    while (v) {
        tmp[n++] = digits[v % (unsigned)base];
        v /= (unsigned)base;
    }
    while (n--) {
        if (*pos + 1 < cap)
            buf[*pos] = tmp[n];
        (*pos)++;
    }
    return 0;
}

static void put_ch(char *buf, size_t cap, size_t *pos, char c)
{
    if (*pos + 1 < cap)
        buf[*pos] = c;
    (*pos)++;
}

static void put_str(char *buf, size_t cap, size_t *pos, const char *s, size_t max)
{
    size_t i = 0;
    if (!s) s = "(null)";
    while (i < max && s[i]) {
        put_ch(buf, cap, pos, s[i]);
        i++;
    }
}

static void pad(char *buf, size_t cap, size_t *pos, int n, char c)
{
    while (n-- > 0)
        put_ch(buf, cap, pos, c);
}

int rt_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    size_t pos = 0;
    const char *p;

    /* A null format is an empty format. p must see that replacement. */
    if (!fmt)
        fmt = "";
    p = fmt;

    while (*p) {
        if (*p != '%') {
            put_ch(buf, n, &pos, *p++);
            continue;
        }
        p++;
        /* flags */
        int left = 0, sign = 0, space = 0, alt = 0, zero = 0;
        for (;;) {
            if (*p == '-') { left = 1; p++; }
            else if (*p == '+') { sign = 1; p++; }
            else if (*p == ' ') { space = 1; p++; }
            else if (*p == '#') { alt = 1; p++; }
            else if (*p == '0') { zero = 1; p++; }
            else break;
        }
        /* width */
        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            if (width < 0) { left = 1; width = -width; }
            p++;
        } else {
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
        }
        /* precision */
        int prec = -1;
        if (*p == '.') {
            p++;
            prec = 0;
            if (*p == '*') {
                prec = va_arg(ap, int);
                if (prec < 0) prec = -1;
                p++;
            } else {
                while (*p >= '0' && *p <= '9') {
                    prec = prec * 10 + (*p - '0');
                    p++;
                }
            }
        }
        /* length */
        int len = 0; /* 0 def, 1 h, 2 hh, 3 l, 4 ll, 5 z */
        if (*p == 'h') {
            p++;
            if (*p == 'h') { len = 2; p++; } else len = 1;
        } else if (*p == 'l') {
            p++;
            if (*p == 'l') { len = 4; p++; } else len = 3;
        } else if (*p == 'z' || *p == 't') {
            len = 5;
            p++;
        }

        char conv = *p ? *p++ : '\0';
        char numbuf[72];
        size_t npos = 0;
        int negative = 0;
        char prefix[4];
        int plen = 0;

        switch (conv) {
        case '%':
            put_ch(buf, n, &pos, '%');
            break;
        case 'c': {
            char ch = (char)va_arg(ap, int);
            if (!left && width > 1) pad(buf, n, &pos, width - 1, ' ');
            put_ch(buf, n, &pos, ch);
            if (left && width > 1) pad(buf, n, &pos, width - 1, ' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            size_t sl;
            if (!s) s = "(null)";
            sl = 0;
            while (s[sl] && (prec < 0 || (int)sl < prec)) sl++;
            if (!left && width > (int)sl) pad(buf, n, &pos, width - (int)sl, ' ');
            put_str(buf, n, &pos, s, sl);
            if (left && width > (int)sl) pad(buf, n, &pos, width - (int)sl, ' ');
            break;
        }
        case 'd':
        case 'i': {
            long long v;
            if (len == 4) v = va_arg(ap, long long);
            else if (len == 3) v = va_arg(ap, long);
            else if (len == 5) v = (long long)va_arg(ap, size_t);
            else {
                int x = va_arg(ap, int);
                if (len == 1) x = (short)x;
                if (len == 2) x = (signed char)x;
                v = x;
            }
            if (v < 0) { negative = 1; v = -v; }
            put_rev(numbuf, sizeof numbuf, &npos, (unsigned long long)v, 10, 0);
            goto emit_num;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            unsigned long long v;
            int base = conv == 'o' ? 8 : (conv == 'u' ? 10 : 16);
            int upper = conv == 'X';
            if (len == 4) v = va_arg(ap, unsigned long long);
            else if (len == 3) v = va_arg(ap, unsigned long);
            else if (len == 5) v = va_arg(ap, size_t);
            else {
                unsigned x = va_arg(ap, unsigned);
                if (len == 1) x = (unsigned short)x;
                if (len == 2) x = (unsigned char)x;
                v = x;
            }
            if (alt && v != 0) {
                if (base == 8) prefix[plen++] = '0';
                if (base == 16) {
                    prefix[plen++] = '0';
                    prefix[plen++] = upper ? 'X' : 'x';
                }
            }
            put_rev(numbuf, sizeof numbuf, &npos, v, base, upper);
            goto emit_num;
        }
        case 'p': {
            void *ptr = va_arg(ap, void *);
            uintptr_t v = (uintptr_t)ptr;
            prefix[plen++] = '0';
            prefix[plen++] = 'x';
            put_rev(numbuf, sizeof numbuf, &npos, (unsigned long long)v, 16, 0);
            goto emit_num;
        }
        default:
            put_ch(buf, n, &pos, '%');
            if (conv) put_ch(buf, n, &pos, conv);
            break;
        }
        continue;

    emit_num: {
            int digits = (int)npos;
            int precpad = 0;
            int total;
            char signch = 0;
            if (prec == 0 && digits == 1 && numbuf[0] == '0' && npos == 1) {
                npos = 0;
                digits = 0;
            }
            if (prec > digits) precpad = prec - digits;
            if (negative) signch = '-';
            else if (sign) signch = '+';
            else if (space) signch = ' ';
            total = digits + precpad + plen + (signch ? 1 : 0);
            if (!left && !zero && width > total)
                pad(buf, n, &pos, width - total, ' ');
            if (signch) put_ch(buf, n, &pos, signch);
            {
                int i;
                for (i = 0; i < plen; i++)
                    put_ch(buf, n, &pos, prefix[i]);
            }
            if (!left && zero && width > total)
                pad(buf, n, &pos, width - total, '0');
            pad(buf, n, &pos, precpad, '0');
            {
                size_t i;
                for (i = 0; i < npos; i++)
                    put_ch(buf, n, &pos, numbuf[i]);
            }
            if (left && width > total)
                pad(buf, n, &pos, width - total, ' ');
        }
    }

    if (n) {
        size_t term = pos < n ? pos : n - 1;
        buf[term] = '\0';
    }
    return (int)pos;
}

int rt_snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = rt_vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

/* Binary32 decimal — simple, deterministic, not fully IEEE-faithful for edge cases. */
int rt_fmt_f32_into(char *buf, size_t n, float x, int prec)
{
    union { float f; uint32_t u; } bits;
    int exp2, sign;
    uint32_t mant;
    int i, len;
    char tmp[64];
    unsigned long long ip, frac_scale, fp;
    double absx; /* only for a short decimal path; still bounded */

    if (prec < 0) prec = 6;
    if (prec > 9) prec = 9;
    bits.f = x;
    sign = (bits.u >> 31) & 1;
    exp2 = (int)((bits.u >> 23) & 0xFF);
    mant = bits.u & 0x7FFFFFu;

    if (exp2 == 0xFF) {
        const char *s = mant ? "nan" : (sign ? "-inf" : "inf");
        return rt_snprintf(buf, n, "%s", s);
    }
    if (exp2 == 0 && mant == 0)
        return rt_snprintf(buf, n, sign ? "-0.%0*d" : "0.%0*d", prec, 0);

    absx = x < 0 ? -x : x;
    ip = (unsigned long long)absx;
    {
        double fpart = absx - (double)ip;
        frac_scale = 1;
        for (i = 0; i < prec; i++) frac_scale *= 10ULL;
        fp = (unsigned long long)(fpart * (double)frac_scale + 0.5);
        if (fp >= frac_scale) {
            ip++;
            fp = 0;
        }
    }
    len = 0;
    if (sign) tmp[len++] = '-';
    {
        char rev[24];
        int rn = 0;
        unsigned long long v = ip;
        if (v == 0) rev[rn++] = '0';
        while (v) {
            rev[rn++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (rn--)
            tmp[len++] = rev[rn];
    }
    if (prec > 0) {
        tmp[len++] = '.';
        {
            char fdig[12];
            int k;
            for (k = prec - 1; k >= 0; k--) {
                fdig[k] = (char)('0' + (fp % 10));
                fp /= 10;
            }
            for (k = 0; k < prec; k++)
                tmp[len++] = fdig[k];
        }
    }
    tmp[len] = '\0';
    return rt_snprintf(buf, n, "%s", tmp);
}

int rt_fmt_f32(char *buf, size_t n, float x)
{
    return rt_fmt_f32_into(buf, n, x, 6);
}
