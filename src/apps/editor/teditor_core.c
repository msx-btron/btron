/*
 * teditor_core.c — Shared Headless B-System T-Editor Document Engine
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "teditor_core.h"

static int t_strlen(const char *s)
{
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void t_strlcpy(char *dst, int cap, const char *src)
{
    int i = 0;
    if (!dst || cap <= 0) return;
    if (src) {
        for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static char *line_ptr(const TEditorCore *ed, int idx)
{
    return ed->lines + (size_t)idx * (size_t)ed->stride;
}

void ted_core_init(TEditorCore *ed, char *buf, int stride, int max_lines)
{
    if (!ed || !buf || stride <= 1 || max_lines <= 0) return;
    ed->lines       = buf;
    ed->stride      = stride;
    ed->max_lines   = max_lines;
    ed->n_lines     = 1;
    ed->cursor_line = 0;
    ed->cursor_col  = 0;
    ed->top_line    = 0;
    ed->dirty       = 0;
    buf[0]          = '\0';
}

void ted_core_clear(TEditorCore *ed)
{
    if (!ed || !ed->lines) return;
    ed->n_lines     = 1;
    ed->cursor_line = 0;
    ed->cursor_col  = 0;
    ed->top_line    = 0;
    ed->dirty       = 1;
    ed->lines[0]    = '\0';
}

void ted_core_load(TEditorCore *ed, const char *text)
{
    int li = 0, ci = 0, cols = 0;
    const int max_cols = 32; /* 32 half-width / 16 CJK cols fit cleanly in 320px SegUI */
    if (!ed || !ed->lines) return;
    ed->n_lines     = 1;
    ed->cursor_line = 0;
    ed->cursor_col  = 0;
    ed->top_line    = 0;
    ed->dirty       = 0;
    line_ptr(ed, 0)[0] = '\0';
    if (!text) return;

    while (*text && li < ed->max_lines) {
        unsigned char ch = (unsigned char)*text;
        int clen = 1, cw = 1;
        if ((ch & 0xE0u) == 0xC0u) { clen = 2; cw = 1; }
        else if ((ch & 0xF0u) == 0xE0u) {
            clen = 3;
            /* Tibetan (0xE0 0xBC..0xBF) is narrow (~1 col); CJK is full-width (2 cols) */
            cw = (ch == 0xE0u && ((unsigned char)text[1] >= 0xBCu && (unsigned char)text[1] <= 0xBFu)) ? 1 : 2;
        } else if ((ch & 0xF8u) == 0xF0u) { clen = 4; cw = 2; }

        if (ch == '\r') { text++; continue; }
        if (ch == '\n' || ci + clen >= ed->stride || (cols > 0 && cols + cw > max_cols)) {
            line_ptr(ed, li)[ci] = '\0';
            if (li + 1 >= ed->max_lines) break;
            li++;
            ci = 0;
            cols = 0;
            line_ptr(ed, li)[0] = '\0';
            if (ch == '\n') { text++; continue; }
        }
        for (int k = 0; k < clen && *text && ci < ed->stride - 1; k++) {
            line_ptr(ed, li)[ci++] = *text++;
        }
        cols += cw;
        line_ptr(ed, li)[ci] = '\0';
    }
    ed->n_lines     = li + 1;
    ed->cursor_line = li;
    ed->cursor_col  = ci;
}

uint32_t ted_core_save(const TEditorCore *ed, char *out, uint32_t cap)
{
    uint32_t pos = 0;
    if (!ed || !ed->lines || !out || cap == 0) return 0;
    for (int i = 0; i < ed->n_lines && pos + 1 < cap; i++) {
        const char *ln = line_ptr(ed, i);
        while (*ln && pos + 1 < cap) out[pos++] = *ln++;
        if (i + 1 < ed->n_lines && pos + 1 < cap) out[pos++] = '\n';
    }
    out[pos] = '\0';
    return pos;
}

int ted_core_insert_char(TEditorCore *ed, char ch)
{
    char *ln;
    int len, i;
    if (!ed || !ed->lines || ch < 0x20 || ch >= 0x7F) return 0;
    ln  = line_ptr(ed, ed->cursor_line);
    len = t_strlen(ln);
    if (len + 1 >= ed->stride) return 0;
    for (i = len; i >= ed->cursor_col; i--) ln[i + 1] = ln[i];
    ln[ed->cursor_col++] = ch;
    ed->dirty = 1;
    return 1;
}

int ted_core_backspace(TEditorCore *ed)
{
    char *ln;
    int len, i;
    if (!ed || !ed->lines) return 0;
    ln  = line_ptr(ed, ed->cursor_line);
    len = t_strlen(ln);
    if (ed->cursor_col > 0) {
        for (i = ed->cursor_col - 1; i < len; i++) ln[i] = ln[i + 1];
        ed->cursor_col--;
        ed->dirty = 1;
        return 1;
    }
    /* Join with previous line if at col 0 */
    if (ed->cursor_line > 0) {
        char *prev = line_ptr(ed, ed->cursor_line - 1);
        int plen = t_strlen(prev);
        int copy = len;
        if (plen + copy >= ed->stride) copy = ed->stride - 1 - plen;
        for (i = 0; i < copy; i++) prev[plen + i] = ln[i];
        prev[plen + copy] = '\0';
        for (i = ed->cursor_line; i < ed->n_lines - 1; i++) {
            t_strlcpy(line_ptr(ed, i), ed->stride, line_ptr(ed, i + 1));
        }
        ed->n_lines--;
        ed->cursor_line--;
        ed->cursor_col = plen;
        if (ed->cursor_line < ed->top_line) ed->top_line = ed->cursor_line;
        ed->dirty = 1;
        return 1;
    }
    return 0;
}

int ted_core_newline(TEditorCore *ed)
{
    char *ln;
    int i;
    if (!ed || !ed->lines || ed->n_lines >= ed->max_lines) return 0;
    for (i = ed->n_lines; i > ed->cursor_line + 1; i--) {
        t_strlcpy(line_ptr(ed, i), ed->stride, line_ptr(ed, i - 1));
    }
    ln = line_ptr(ed, ed->cursor_line);
    /* Split tail of current line onto the new line */
    t_strlcpy(line_ptr(ed, ed->cursor_line + 1), ed->stride, ln + ed->cursor_col);
    ln[ed->cursor_col] = '\0';
    ed->n_lines++;
    ed->cursor_line++;
    ed->cursor_col = 0;
    ed->dirty = 1;
    return 1;
}

int ted_core_move(TEditorCore *ed, int dline, int dcol, int vis_lines)
{
    int nl, nc, len;
    if (!ed || !ed->lines) return 0;
    nl = ed->cursor_line + dline;
    if (nl < 0) nl = 0;
    if (nl >= ed->n_lines) nl = ed->n_lines - 1;
    len = t_strlen(line_ptr(ed, nl));
    nc = ed->cursor_col + dcol;
    if (nc < 0) nc = 0;
    if (nc > len) nc = len;
    if (nl == ed->cursor_line && nc == ed->cursor_col) return 0;
    ed->cursor_line = nl;
    ed->cursor_col  = nc;
    if (vis_lines > 0) {
        if (ed->cursor_line < ed->top_line) ed->top_line = ed->cursor_line;
        if (ed->cursor_line >= ed->top_line + vis_lines)
            ed->top_line = ed->cursor_line - vis_lines + 1;
    }
    return 1;
}

uint32_t ted_core_word_count(const TEditorCore *ed)
{
    uint32_t words = 0;
    if (!ed || !ed->lines) return 0;
    for (int i = 0; i < ed->n_lines; i++) {
        const char *p = line_ptr(ed, i);
        int in_w = 0;
        while (*p) {
            int sp = (*p == ' ' || *p == '\t');
            if (!sp && !in_w) { words++; in_w = 1; }
            else if (sp) { in_w = 0; }
            p++;
        }
    }
    return words;
}
