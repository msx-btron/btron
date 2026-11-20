/*
 * tad_parser.h — Shared Headless B-System TAD / Hypertext Stream Parser
 *
 * Extracted from tad_browser.c text/hypertext logic: zero WND, zero malloc,
 * zero GIF/PNG/zlib buffers. Word-wraps to any screen width (e.g. 36 cols on
 * PicoCalc 320x320) and extracts navigable `[TARGET]` hypertext links.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_TAD_PARSER_H_
#define _BTRON_TAD_PARSER_H_

#include <stdint.h>

#define TAD_MAX_LINKS    8
#define TAD_LINK_MAX    24

typedef struct {
    char  target[TAD_LINK_MAX];
    int   line_idx;
} TAD_LINK;

typedef struct {
    char     *lines;      /* flat [max_lines * stride] caller-owned buffer */
    int       stride;
    int       max_lines;
    int       n_lines;
    TAD_LINK  links[TAD_MAX_LINKS];
    int       n_links;
} TAD_VIEW;

void tad_parse_stream(TAD_VIEW *view, const uint8_t *src, uint32_t len, int max_cols);
void tad_parse_text(TAD_VIEW *view, const char *src, int max_cols);

#endif /* _BTRON_TAD_PARSER_H_ */
