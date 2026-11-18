/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Tibetan Book Architecture (clarity_book.h)
 * Data model, classifier reconstruction, Wylie->Tibetan conversion,
 * and canonical Pecha book generation.
 */

#ifndef _CLARITY_BOOK_H_
#define _CLARITY_BOOK_H_

#include <btron/types.h>
#include "clarity_doc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical Tibetan Pecha Sizes */
typedef enum {
    PECHA_SIZE_KANGYUR     = 0,  /* Kangyur / Tengyur:    560 × 110 mm (8 rows)  */
    PECHA_SIZE_RINCHEN     = 1,  /* Rinchen Terdzö:      650 × 140 mm (10 rows) */
    PECHA_SIZE_DERGE       = 2,  /* Derge Woodblock:     700 × 180 mm (13 rows) */
    PECHA_SIZE_POCKET      = 3,  /* Pocket Dharma:       320 × 85 mm  (6 rows)  */
    PECHA_SIZE_DERGE_WIDE  = 4   /* Derge Wide Woodblock: 720 × 200 mm (25 rows) */
} ClarityPechaSize;

typedef struct {
    size_t file_size;
    UW     checksum;             /* Adler-32 checksum */
    BOOL   is_valid;
    char   err_msg[64];
} ClarityFileIntegrity;

#define CLARITY_MAX_CHAPTERS 64
#define CLARITY_MAX_CLASSES   8

typedef struct {
    int  id;                 /* Class 1..5 */
    char roman[16];          /* "I", "II", "III", "IV", "V" */
    char wylie[128];         /* "sngon 'gro dang gzhi'i chos" */
    char tibetan[256];       /* "སྔོན་འགྲོ་དང་གཞིའི་ཆོས" */
    char title[256];         /* Ukrainian / Translation descriptive name */
    int  chapter_indices[CLARITY_MAX_CHAPTERS];
    int  chapter_count;
} ClarityBookClass;

typedef struct {
    int    id;
    int    class_id;         /* 1..5 */
    char   filename[64];     /* e.g. "dkar.chag.txt" */
    char   wylie_title[128];
    char   tibetan_title[256];
    char   transl_title[256];
    char   file_path[256];
    size_t file_size;
    int    line_count;
    char   text_preview[512];
} ClarityBookChapter;

typedef struct {
    char               title[128];         /* "Лонгчен Осал" */
    char               subtitle[128];      /* "klong chen 'od gsal snying thig" */
    char               tibetan_title[256]; /* "ཀློང་ཆེན་འོད་གསལ་སྙིང་ཐིག" */
    char               base_dir[256];      /* e.g. "./assets/osal" */
    ClarityPechaSize   pecha_size;
    BOOL               auto_tibetan;

    ClarityBookClass   classes[CLARITY_MAX_CLASSES];
    int                class_count;

    ClarityBookChapter chapters[CLARITY_MAX_CHAPTERS];
    int                chapter_count;

    int                active_chapter_idx;
    BOOL               is_loaded;
} ClarityBook;

/* Pecha geometric lookup */
void clarity_get_pecha_dimensions(ClarityPechaSize size, int *w_mm, int *h_mm, const char **name_out);

/* Wylie transliteration helpers */
int clarity_wylie_line_to_tibetan(const char *in_line, char *out_line, size_t max_out);
int clarity_wylie_to_tibetan_text(const char *in_text, char *out_text, size_t max_out);

/* Book management */
void clarity_book_init(ClarityBook *book);
int  clarity_book_open_dir(ClarityBook *book, const char *dir_path, ClarityPechaSize pecha_size, BOOL auto_tibetan);
int  clarity_book_load_chapter_into_doc(ClarityBook *book, int chapter_idx, ClarityDoc *doc);
int  clarity_book_next_chapter(ClarityBook *book, ClarityDoc *doc);
int  clarity_book_prev_chapter(ClarityBook *book, ClarityDoc *doc);
ClarityBook* clarity_get_book(void);

/* TAD Book Export */
int  clarity_book_export_tad(const ClarityBook *book, const char *out_dir, const char *single_tad_path);

/* Robust file integrity verification */
int  clarity_check_file_integrity(const char *path, ClarityFileIntegrity *out_info);

#ifdef __cplusplus
}
#endif

#endif /* _CLARITY_BOOK_H_ */
