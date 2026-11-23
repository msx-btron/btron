/*
 * tools/mkosalbook.c — Standalone Osal Book TAD builder
 *
 * Compiles ./assets/osal/ into ./tad_bin/osal/ (index.tad, chapter .tad files,
 * and unified scroll osal_shog_dril.tad) using Clarity's canonical Wylie-to-
 * Tibetan book compiler without SDL2 or desktop dependencies.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clarity_doc.h"
#include "clarity_book.h"

/* Stub: clarity_book_populate_doc() is unused by the CLI TAD exporter. */
ClarityFrame *clarity_doc_add_frame(ClarityDoc *doc, ClarityFrameType type,
                                    int x, int y, int w, int h)
{
    (void)doc; (void)type; (void)x; (void)y; (void)w; (void)h;
    return NULL;
}

int main(int argc, char **argv)
{
    const char *src_dir  = (argc > 1 && argv[1][0]) ? argv[1] : "assets/osal";
    const char *out_dir  = (argc > 2 && argv[2][0]) ? argv[2] : "tad_bin/osal";
    const char *shog_out = (argc > 3 && argv[3][0]) ? argv[3] : "tad_bin/osal/osal_shog_dril.tad";

    ClarityBook *book = (ClarityBook *)calloc(1, sizeof(ClarityBook));
    if (!book) {
        fprintf(stderr, "[OSAL] Out of memory\n");
        return 1;
    }

    int rc = clarity_book_open_dir(book, src_dir, PECHA_SIZE_KANGYUR, TRUE);
    if (rc != 0 || !book->is_loaded) {
        fprintf(stderr, "[OSAL] Failed to load Osal sources from %s\n", src_dir);
        free(book);
        return 1;
    }

    rc = clarity_book_export_tad(book, out_dir, shog_out);
    if (rc != 0) {
        fprintf(stderr, "[OSAL] Failed to export TAD book into %s\n", out_dir);
        free(book);
        return 1;
    }

    printf("[OSAL] Built %d classes, %d chapters -> %s/ (+ %s)\n",
           book->class_count, book->chapter_count, out_dir, shog_out);
    free(book);
    return 0;
}
