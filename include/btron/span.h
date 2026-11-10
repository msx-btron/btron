/*
 * Half-open span [ofs, ofs+len) inside [0, size).
 * Negative ends are rejected. The sum is never formed in signed arithmetic,
 * so a hostile ofs near INT_MAX cannot wrap into looking small.
 */
#ifndef BTRON_SPAN_H
#define BTRON_SPAN_H

#include <stddef.h>

static inline int btron_span_inside(int ofs, int len, int size)
{
    size_t uo, ul, us;

    if (ofs < 0 || len < 0 || size < 0)
        return 0;
    uo = (size_t)ofs;
    ul = (size_t)len;
    us = (size_t)size;
    if (ul > us)
        return 0;
    if (uo > us - ul)
        return 0;
    return 1;
}

#endif /* BTRON_SPAN_H */
