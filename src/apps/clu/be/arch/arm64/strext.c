#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Append the expansion of `src` to the heap string *dst, growing it.
 *
 * Both vsnprintf calls below take a format supplied by the caller, so
 * flawfinder's (format) rule fires on them by construction: this is the
 * variadic forwarder every arm64 decoder formats through, and there is no
 * literal to substitute.  The bounds are real and measured -- see the
 * capacity note on the second call.  Recorded in doc/txt/SQCSA.txt slice 8. */
static int _concat_internal(char **dst, const char *src, va_list args){
    if(!src || !dst)
        return 0;

    size_t dstlen = 0;
    int need, w;
    char *dst1, *tight;

    if(*dst)
        dstlen = strlen(*dst);

    /* Back up args before it gets used. Client calls va_end
     * on the parameter themselves when calling vconcat.
     */
    va_list args1;
    va_copy(args1, args);

    /* A negative return is an encoding error, not a length: adding it to a
     * size_t wrapped to a huge allocation request. */
    need = vsnprintf(NULL, 0, src, args); /* flawfinder: ignore */
    if(need < 0){
        va_end(args1);
        return 0;
    }

    /* need is the measured expansion, so it already covers the format's own
     * length; the old srclen term only over-allocated. */
    size_t total = dstlen + (size_t)need + 1;

    dst1 = malloc(total);
    if(!dst1){
        va_end(args1);
        return 0;
    }

    if(dstlen)
        memcpy(dst1, *dst, dstlen);
    dst1[dstlen] = '\0';
    free(*dst);
    *dst = NULL;

    /* The write starts at dstlen, so the room left is total - dstlen.
     * Passing `total` here let the expansion run dstlen bytes past the
     * allocation -- a real heap overflow on every append to a non-empty
     * string. */
    w = vsnprintf(dst1 + dstlen, total - dstlen, src, args1); /* flawfinder: ignore */

    va_end(args1);

    if(w < 0){
        free(dst1);
        return 0;
    }

    tight = realloc(dst1, strlen(dst1) + 1);
    *dst = tight ? tight : dst1;

    return w;
}

int concat(char **dst, const char *src, ...){
    va_list args;
    va_start(args, src);

    int w = _concat_internal(dst, src, args);

    va_end(args);

    return w;
}

int vconcat(char **dst, const char *src, va_list args){
    return _concat_internal(dst, src, args);
}