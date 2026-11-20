/*
 * teditor_core.h — Shared Headless B-System T-Editor Document Engine
 *
 * Extracted from t_editor.c buffer/editing semantics: zero WND, zero malloc,
 * caller-owned static line storage (compatible with SEGUI_TEXT_DOC).
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_TEDITOR_CORE_H_
#define _BTRON_TEDITOR_CORE_H_

#include <stdint.h>
#include <stddef.h>

typedef struct {
    char  *lines;       /* flat [max_lines * stride] caller-owned buffer */
    int    stride;      /* bytes per line incl. NUL                      */
    int    max_lines;
    int    n_lines;
    int    cursor_line;
    int    cursor_col;
    int    top_line;
    int    dirty;
} TEditorCore;

void     ted_core_init(TEditorCore *ed, char *buf, int stride, int max_lines);
void     ted_core_load(TEditorCore *ed, const char *text);
uint32_t ted_core_save(const TEditorCore *ed, char *out, uint32_t cap);
void     ted_core_clear(TEditorCore *ed);
int      ted_core_insert_char(TEditorCore *ed, char ch);
int      ted_core_backspace(TEditorCore *ed);
int      ted_core_newline(TEditorCore *ed);
int      ted_core_move(TEditorCore *ed, int dline, int dcol, int vis_lines);
uint32_t ted_core_word_count(const TEditorCore *ed);

#endif /* _BTRON_TEDITOR_CORE_H_ */
