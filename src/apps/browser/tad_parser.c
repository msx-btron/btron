/*
 * tad_parser.c — Shared Headless B-System TAD / Hypertext Stream Parser
 *
 * Supports BOTH:
 *   1. Real compiled BTRON3 Binary TAD streams (0x00 0x01 magic + 0xFFA0..0xFFB0
 *      segments including TS_VOBJ 0xA8 links and TS_FPRIM 0xB0 rules/figures)
 *   2. Plain-text / Markdown-style TAD documents with [LINK] targets
 *
 * UTF-8 safe wrapping (never splits a multi-byte sequence across lines),
 * zero heap, zero libc, NASA JPL Rule 3 compliant.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "tad_parser.h"
#include <stddef.h>

static int utf8_char_len(uint8_t b)
{
    if ((b & 0x80u) == 0x00u) return 1;
    if ((b & 0xE0u) == 0xC0u) return 2;
    if ((b & 0xF0u) == 0xE0u) return 3;
    if ((b & 0xF8u) == 0xF0u) return 4;
    return 1;
}

static int utf8_col_width(const uint8_t *p, int clen)
{
    if (clen == 1) return 1;
    if (clen == 2) return 1; /* Cyrillic / Latin-Ext fit in ~1 narrow cell */
    /* 3-byte UTF-8: Tibetan (0xE0 0xBC..0xBF) is ~1-2 cols; CJK is 2 cols */
    if (clen == 3 && p[0] == 0xE0u && (p[1] >= 0xBCu && p[1] <= 0xBFu)) return 1;
    return 2;
}

static void emit_line(TAD_VIEW *v, const char *s, int len)
{
    char *dst;
    int i;
    if (!v || !v->lines || v->n_lines >= v->max_lines) return;
    if (len >= v->stride) {
        /* Back up to a valid UTF-8 boundary */
        len = v->stride - 1;
        while (len > 0 && (((uint8_t)s[len]) & 0xC0u) == 0x80u) len--;
    }
    dst = v->lines + (size_t)v->n_lines * (size_t)v->stride;
    for (i = 0; i < len; i++) dst[i] = s[i];
    dst[len] = '\0';
    v->n_lines++;
}

static void record_link_target(TAD_VIEW *v, const char *target, int tlen, int line_idx)
{
    int j;
    TAD_LINK *lk;
    if (!v || !target || tlen <= 0 || v->n_links >= TAD_MAX_LINKS) return;
    lk = &v->links[v->n_links++];
    lk->line_idx = line_idx;
    for (j = 0; j < tlen && j < TAD_LINK_MAX - 1; j++) {
        lk->target[j] = target[j];
    }
    lk->target[j] = '\0';
}

static void record_bracket_links(TAD_VIEW *v, const char *line, int len, int line_idx)
{
    int i = 0;
    while (i < len && v->n_links < TAD_MAX_LINKS) {
        if (line[i] == '[') {
            int start = ++i;
            while (i < len && line[i] != ']') i++;
            if (i < len && i > start) {
                record_link_target(v, line + start, i - start, line_idx);
            }
        }
        i++;
    }
}

static void emit_wrapped_utf8(TAD_VIEW *view, const char *ls, int llen, int max_cols, int scan_brackets)
{
    while (llen > 0 && view->n_lines < view->max_lines) {
        int pos = 0, cols = 0, last_space = -1;
        int max_bytes = view->stride - 1;
        while (pos < llen) {
            int clen = utf8_char_len((uint8_t)ls[pos]);
            int cw;
            if (pos + clen > llen) clen = llen - pos;
            cw = utf8_col_width((const uint8_t *)(ls + pos), clen);
            if ((cols + cw > max_cols || pos + clen > max_bytes) && pos > 0) break;
            if (clen == 1 && ls[pos] == ' ') last_space = pos;
            pos  += clen;
            cols += cw;
        }
        if (pos < llen && last_space > 4) {
            pos = last_space;
        }
        if (pos <= 0) break;
        if (scan_brackets) {
            record_bracket_links(view, ls, pos, view->n_lines);
        }
        emit_line(view, ls, pos);
        ls   += pos;
        llen -= pos;
        while (llen > 0 && *ls == ' ') { ls++; llen--; }
    }
}

static void emit_rule(TAD_VIEW *view, int max_cols)
{
    char tmp[64];
    int k, n = max_cols;
    if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    if (n > view->stride - 1) n = view->stride - 1;
    for (k = 0; k < n; k++) tmp[k] = '-';
    emit_line(view, tmp, n);
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static uint16_t rd_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void parse_binary_tad(TAD_VIEW *view, const uint8_t *p, uint32_t len, int max_cols)
{
    uint32_t off = 6; /* skip 0x00 0x01 version + 4-byte total length */
    while (off < len && view->n_lines < view->max_lines) {
        if (p[off] == 0xFFu) {
            uint8_t  tag;
            uint32_t seg_len;
            if (off + 6u > len) break;
            tag     = p[off + 1u];
            seg_len = rd_be32(p + off + 2u);
            off    += 6u;
            if (off + seg_len > len) break;

            if (tag == 0xA8u && seg_len >= 6u) {
                /* TS_VOBJ: Virtual Object hyperlink */
                uint32_t d = (seg_len >= 7u && p[off] == 0x00u) ? (off + 1u) : off;
                uint16_t lbl_len = rd_be16(p + d + 4u);
                uint32_t next_off = d + 6u;
                if (lbl_len == 0u && d + 5u < off + seg_len && p[d + 5u] > 0u) {
                    lbl_len = p[d + 5u];
                }
                if (lbl_len > 0u && next_off + lbl_len <= off + seg_len) {
                    char vbuf[160];
                    int vlen = 0, i;
                    const char *prefix = "> ";
                    for (i = 0; prefix[i] && vlen < (int)sizeof(vbuf) - 1; i++) {
                        vbuf[vlen++] = prefix[i];
                    }
                    for (i = 0; i < (int)lbl_len && vlen < (int)sizeof(vbuf) - 1; i++) {
                        char ch = (char)p[next_off + (uint32_t)i];
                        if (ch == '\r' || ch == '\n') ch = ' ';
                        vbuf[vlen++] = ch;
                    }
                    vbuf[vlen] = '\0';

                    /* Optional target path following label */
                    uint32_t poff = next_off + lbl_len;
                    if (poff + 2u <= off + seg_len) {
                        uint16_t plen = rd_be16(p + poff);
                        if (plen > 0u && poff + 2u + plen <= off + seg_len) {
                            record_link_target(view, (const char *)(p + poff + 2u),
                                               (int)plen, view->n_lines);
                        }
                    }
                    emit_wrapped_utf8(view, vbuf, vlen, max_cols, 0);
                }
            } else if (tag == 0xB0u && seg_len >= 1u) {
                /* TS_FPRIM: SubID 1 = horizontal rule, SubID 10 = figure */
                if (p[off] == 10u) {
                    emit_line(view, "[Figure]", 8);
                } else {
                    emit_rule(view, max_cols);
                }
            }
            off += seg_len;
        } else {
            /* Raw UTF-8 text run until next 0xFF segment */
            uint32_t run_end = off;
            while (run_end < len && p[run_end] != 0xFFu) run_end++;

            while (off < run_end && view->n_lines < view->max_lines) {
                uint32_t ls = off;
                int llen = 0;
                while (off < run_end && p[off] != '\n') {
                    if (p[off] != '\r') llen++;
                    off++;
                }
                if (off < run_end && p[off] == '\n') off++;

                if (llen == 0) {
                    emit_line(view, "", 0);
                } else {
                    emit_wrapped_utf8(view, (const char *)(p + ls), llen, max_cols, 0);
                }
            }
        }
    }
}

void tad_parse_stream(TAD_VIEW *view, const uint8_t *src, uint32_t len, int max_cols)
{
    uint32_t off = 0;
    if (!view || !view->lines || view->stride <= 1 || view->max_lines <= 0) return;
    view->n_lines = 0;
    view->n_links = 0;
    if (!src || len == 0u) return;
    if (max_cols <= 0 || max_cols >= view->stride) max_cols = view->stride - 1;
    if (max_cols > 63) max_cols = 63;

    /* Auto-detect compiled BTRON3 binary TAD format */
    if (len >= 6u && src[0] == 0x00u && src[1] == 0x01u) {
        parse_binary_tad(view, src, len, max_cols);
        return;
    }

    /* Plain-text / Markdown-style TAD document */
    while (off < len && src[off] != '\0' && view->n_lines < view->max_lines) {
        const char *ls = (const char *)(src + off);
        int llen = 0;
        while (off < len && src[off] != '\0' && src[off] != '\n') {
            if (src[off] != '\r') llen++;
            off++;
        }
        if (off < len && src[off] == '\n') off++;

        /* Strip leading '#' / '##' TAD/Markdown heading markers */
        if (llen > 1 && ls[0] == '#') {
            while (llen > 0 && (*ls == '#' || *ls == ' ')) { ls++; llen--; }
        }
        /* Horizontal rule '---' -> full-width rule */
        if (llen >= 3 && ls[0] == '-' && ls[1] == '-' && ls[2] == '-') {
            emit_rule(view, max_cols);
            continue;
        }
        if (llen == 0) {
            emit_line(view, "", 0);
            continue;
        }
        emit_wrapped_utf8(view, ls, llen, max_cols, 1);
    }
}

void tad_parse_text(TAD_VIEW *view, const char *src, int max_cols)
{
    uint32_t len = 0;
    if (src) {
        while (src[len]) len++;
    }
    tad_parse_stream(view, (const uint8_t *)src, len, max_cols);
}
