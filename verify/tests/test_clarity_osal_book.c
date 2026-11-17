/*
 * B-System (BTRON 3.20) Verification Suite: Tibetan Osal Book & TAD Generation
 * verify/tests/test_clarity_osal_book.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <btron/types.h>
#include <btron/error.h>
#include <btron/wylie.h>
#include <btron/troncode.h>
#include <btron/tad.h>
#include "clarity_doc.h"
#include "clarity_book.h"
#include <btron/tad_browser.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
        return 1; \
    } else { \
        printf("  [PASS] %s\n", msg); \
    } \
} while(0)

int main(void)
{
    printf("=================================================================\n");
    printf(" BTRON 3.20 Clarity Tibetan Osal Book & Legal TAD Verification   \n");
    printf("=================================================================\n");

    /* ── Test Group 1: EWTS Wylie Transliteration Parser ─────────────── */
    printf("\n==> Test Group 1: EWTS Wylie to Tibetan Transliteration\n");
    {
        char out_line[1024];

        /* A. Master work header */
        const char *hdr = "@#/_/klong chen 'od gsal snying thig/ dkar chag/ Лонгчен Осал: Коментар на Зміст";
        int len = clarity_wylie_line_to_tibetan(hdr, out_line, sizeof(out_line));
        TEST_ASSERT(len > 0, "Header parsed and transliterated");
        TEST_ASSERT(strstr(out_line, "༄༅། །") != NULL, "Tibetan head mark ༄༅། ། inserted");
        TEST_ASSERT(strstr(out_line, "ཀློང་ཆེན་འོད་གསལ་སྙིང་ཐིག") != NULL, "Tradition transliterated to ཀློང་ཆེན་འོད་གསལ་སྙིང་ཐིག");
        TEST_ASSERT(strstr(out_line, "དཀར་ཆག") != NULL, "Section title transliterated to དཀར་ཆག");
        TEST_ASSERT(strstr(out_line, "Лонгчен Осал: Коментар на Зміст") != NULL, "Translation title preserved");

        /* B. Class enumeration */
        const char *cls_line = "I) /sngon 'gro dang gzhi'i chos/ Клас 1: Попередні практики і фундаментальна Дхарма";
        clarity_wylie_line_to_tibetan(cls_line, out_line, sizeof(out_line));
        TEST_ASSERT(strstr(out_line, "I)") != NULL, "Roman numeral I preserved");
        TEST_ASSERT(strstr(out_line, "སྔོན་འགྲོ་དང་གཞིའི་ཆོས") != NULL, "Class Wylie transliterated to སྔོན་འགྲོ་དང་གཞིའི་ཆོས");

        /* C. Section item */
        const char *sec_line = "01) //dkar chag/ Індекси";
        clarity_wylie_line_to_tibetan(sec_line, out_line, sizeof(out_line));
        TEST_ASSERT(strstr(out_line, "01)") != NULL, "Section number 01 preserved");
        TEST_ASSERT(strstr(out_line, "དཀར་ཆག") != NULL, "Section term transliterated to དཀར་ཆག");

        /* D. Glossary entry */
        const char *term_line = "/khro bo ka li zhi ba/ Кродха Калі. Чорна мирна";
        clarity_wylie_line_to_tibetan(term_line, out_line, sizeof(out_line));
        TEST_ASSERT(strstr(out_line, "ཁྲོ་བོ་ཀ་ལི་ཞི་བ") != NULL, "Glossary term transliterated to ཁྲོ་བོ་ཀ་ལི་ཞི་བ");

        /* E. Embedded parenthetical Wylie */
        const char *prose_line = "Суть перебуває як світло (bum nang gi mar me lta bu'o).";
        clarity_wylie_line_to_tibetan(prose_line, out_line, sizeof(out_line));
        TEST_ASSERT(strstr(out_line, "བུམ་ནང་གི་མར་མེ་ལྟ་བུའོ") != NULL, "Parenthesized Wylie transliterated inside prose");

        /* F. Term definition with slash without leading slash */
        const char *term_def = "rtsa ba/ root";
        clarity_wylie_line_to_tibetan(term_def, out_line, sizeof(out_line));
        TEST_ASSERT(strstr(out_line, "རྩ་བ") != NULL, "Term 'rtsa ba' transliterated to རྩ་བ");
        TEST_ASSERT(strstr(out_line, "root") != NULL, "English 'root' preserved");

        /* G. Numbered catalog line with optional category */
        const char *cat_line = "001 rang grol skor gsum la (spyi don)/ Start of Rang Grol Korsum";
        clarity_wylie_line_to_tibetan(cat_line, out_line, sizeof(out_line));
        TEST_ASSERT(strstr(out_line, "001") != NULL, "Number 001 preserved");
        TEST_ASSERT(strstr(out_line, "རང་གྲོལ་སྐོར་གསུམ་ལ") != NULL, "Catalog title transliterated to རང་གྲོལ་སྐོར་གསུམ་ལ");
        TEST_ASSERT(strstr(out_line, "སྤྱི་དོན") != NULL, "Category 'spyi don' transliterated to སྤྱི་དོན");
        TEST_ASSERT(strstr(out_line, "Start of Rang Grol Korsum") != NULL, "Translation preserved");
    }

    /* ── Test Group 2: Osal Source Directory Ingestion & Classifier ─── */
    printf("\n==> Test Group 2: Osal Source Directory Ingestion & Classifier\n");
    ClarityBook book;
    {
        clarity_book_init(&book);
        int rc = clarity_book_open_dir(&book, "./assets/osal", PECHA_SIZE_KANGYUR, TRUE);
        TEST_ASSERT(rc == 0, "clarity_book_open_dir scanned ./assets/osal successfully");
        TEST_ASSERT(book.chapter_count >= 30, "Loaded all canonical chapters (>= 30 files)");
        TEST_ASSERT(book.class_count == 5, "Reconstructed exact 5 doctrinal classes (I to V)");

        /* Verify Class I */
        TEST_ASSERT(strcmp(book.classes[0].roman, "I") == 0, "Class 1 Roman numeral is I");
        TEST_ASSERT(strstr(book.classes[0].tibetan, "སྔོན་འགྲོ") != NULL, "Class 1 Tibetan title is སྔོན་འགྲོ་དང་གཞིའི་ཆོས");
        TEST_ASSERT(book.classes[0].chapter_count > 0, "Class 1 has chapters assigned");

        /* Verify Chapter 0 is dkar.chag */
        const ClarityBookChapter *ch0 = &book.chapters[0];
        TEST_ASSERT(strcmp(ch0->filename, "dkar.chag.txt") == 0, "Chapter 0 is dkar.chag.txt");
        TEST_ASSERT(strstr(ch0->tibetan_title, "དཀར་ཆག") != NULL, "Chapter 0 Tibetan title is དཀར་ཆག");
        TEST_ASSERT(ch0->class_id == 1, "dkar.chag assigned to Class 1");

        /* Verify Dzogchen Class V chapters */
        const ClarityBookClass *cls5 = &book.classes[4];
        TEST_ASSERT(strcmp(cls5->roman, "V") == 0, "Class 5 Roman numeral is V");
        TEST_ASSERT(cls5->chapter_count >= 4, "Class 5 (Dzogchen) contains chapters (sems.sde, khregs.chod, thod.rgal, 'od.gsal, bar.do, yang.ti)");
    }

    /* ── Test Group 3: Pecha Document Formatting & Navigation ──────── */
    printf("\n==> Test Group 3: Pecha Loose-Leaf Formatting & Navigation\n");
    ClarityDoc doc;
    {
        memset(&doc, 0, sizeof(ClarityDoc));
        int l_rc = clarity_book_load_chapter_into_doc(&book, 0, &doc);
        TEST_ASSERT(l_rc == 0, "Loaded Chapter 0 (dkar.chag) into ClarityDoc");
        TEST_ASSERT(doc.fmt == FMT_PECHA, "Document format switched to FMT_PECHA");
        TEST_ASSERT(doc.page_w_mm == 560 && doc.page_h_mm == 110, "Page dimensions set to Kangyur Pecha (560x110 mm)");
        TEST_ASSERT(doc.frame_count >= 1, "Clarity Text Frame created for Pecha loose-leaf");

        ClarityFrame *f0 = &doc.frames[0];
        TEST_ASSERT(f0->type == FRAME_TEXT, "Primary frame is FRAME_TEXT");
        TEST_ASSERT(f0->text_len > 0, "Text buffer populated with content");

        /* Verify presence of Tibetan TRON code codepoints (Plane 10: 0x9F00..0x9FFF) */
        BOOL has_tibetan = FALSE;
        for (UW i = 0; i < f0->text_len; i++) {
            if (f0->text[i] >= 0x9F00 && f0->text[i] <= 0x9FFF) {
                has_tibetan = TRUE;
                break;
            }
        }
        TEST_ASSERT(has_tibetan, "Frame text contains Tibetan TRON Code units (Plane 10)");

        /* Test navigation: Next chapter */
        clarity_book_next_chapter(&book, &doc);
        TEST_ASSERT(book.active_chapter_idx == 1, "Navigated to Chapter 1");
        TEST_ASSERT(strcmp(book.chapters[1].filename, "smon.lam.txt") == 0, "Chapter 1 is smon.lam.txt");

        /* Test navigation: Prev chapter */
        clarity_book_prev_chapter(&book, &doc);
        TEST_ASSERT(book.active_chapter_idx == 0, "Navigated back to Chapter 0");
    }

    /* ── Test Group 3b: test.txt Format Calibration & QuarkXPress Rollout */
    printf("\n==> Test Group 3b: test.txt Format Calibration & Multi-Page Rollout\n");
    {
        ClarityBook test_book;
        clarity_book_init(&test_book);
        int t_rc = clarity_book_open_dir(&test_book, "./assets/osal/test.txt", PECHA_SIZE_KANGYUR, TRUE);
        TEST_ASSERT(t_rc == 0, "Directly opened ./assets/osal/test.txt as single chapter");
        TEST_ASSERT(test_book.chapter_count == 1, "Single chapter initialized for test.txt");

        ClarityDoc t_doc;
        memset(&t_doc, 0, sizeof(ClarityDoc));
        int load_rc = clarity_book_load_chapter_into_doc(&test_book, 0, &t_doc);
        TEST_ASSERT(load_rc == 0, "Loaded test.txt into ClarityDoc");
        TEST_ASSERT(t_doc.page_count >= 2, "test.txt paginated into multiple pages (>= 2)");
        TEST_ASSERT(t_doc.frame_count == t_doc.page_count, "Every page has an assigned text frame placeholder");

        /* Verify each page has a correctly positioned frame rectangle */
        int px_w = (t_doc.page_w_mm * CLARITY_DPI * 10) / 254;
        int px_h = (t_doc.page_h_mm * CLARITY_DPI * 10) / 254;
        int p_gap = CLARITY_PAGE_GAP_PX;
        int margin_x = 40, margin_y = 20;

        for (int p = 0; p < t_doc.page_count; p++) {
            ClarityFrame *f = &t_doc.frames[p];
            TEST_ASSERT(f->type == FRAME_TEXT, "Frame is FRAME_TEXT");
            int expected_y = margin_y + p * (px_h + p_gap);
            TEST_ASSERT(f->bounds.top == expected_y, "Frame Y offset matches page sheet top");
            TEST_ASSERT(f->bounds.left == margin_x, "Frame left margin matches page");
            TEST_ASSERT(f->bounds.right - f->bounds.left == px_w - margin_x * 2, "Frame width fills printable area");
        }

        /* Check that first frame has calibration lines */
        ClarityFrame *f0 = &t_doc.frames[0];
        TEST_ASSERT(f0->text_len > 0, "Page 1 frame populated with initial lines");

        /* Check that Page 2 frame also has text rolled out */
        ClarityFrame *f1 = &t_doc.frames[1];
        TEST_ASSERT(f1->text_len > 0, "Page 2 frame populated with rolled-out continuation lines");
    }

    /* ── Test Group 4: Legal Binary TAD Book Export ─────────────────── */
    printf("\n==> Test Group 4: Legal Binary TAD Book Export\n");
    {
        int exp_rc = clarity_book_export_tad(&book, "tad_bin/osal", "tad_bin/osal_shog_dril.tad");
        TEST_ASSERT(exp_rc == 0, "clarity_book_export_tad completed successfully");

        FILE *f_toc = fopen("tad_bin/osal/index.tad", "rb");
        TEST_ASSERT(f_toc != NULL, "Master TOC file tad_bin/osal/index.tad created");
        if (f_toc) fclose(f_toc);

        FILE *f_ch = fopen("tad_bin/osal/dkar_chag.tad", "rb");
        TEST_ASSERT(f_ch != NULL, "Chapter file tad_bin/osal/dkar_chag.tad created");
        if (f_ch) fclose(f_ch);

        FILE *f_shog = fopen("tad_bin/osal_shog_dril.tad", "rb");
        TEST_ASSERT(f_shog != NULL, "Complete roll volume tad_bin/osal_shog_dril.tad created");
        if (f_shog) fclose(f_shog);
    }

    /* ── Test Group 5: Ingestion & Verification in TAD Browser ──────── */
    printf("\n==> Test Group 5: Ingestion & Verification in TAD Browser\n");
    {
        TAD_BROWSER tb;
        tad_browser_init(&tb);

        /* A. Load Master TOC in TAD Browser */
        ER er = tad_browser_load_file(&tb, "tad_bin/osal/index.tad");
        TEST_ASSERT(er == E_OK, "tad_browser_load_file loaded tad_bin/osal/index.tad");
        TEST_ASSERT(tb.is_binary_tad == TRUE, "TAD Browser identified valid BTRON3 Record Type 1 binary TAD");
        TEST_ASSERT(tb.span_count > 10, "Parsed formatted spans in TAD Browser (> 10 spans)");

        /* Check presence of Virtual Body links in TOC */
        int vobj_links = 0;
        int target_link_idx = -1;
        for (int i = 0; i < tb.span_count; i++) {
            if (tb.spans[i].style.is_vobj && tb.spans[i].is_link) {
                vobj_links++;
                if (target_link_idx < 0) target_link_idx = i;
            }
        }
        TEST_ASSERT(vobj_links >= 5, "Table of Contents contains multiple Virtual Body chapter links");

        /* B. Simulate Clicking Chapter Link and Navigating */
        if (target_link_idx >= 0) {
            const char *target_path = tb.spans[target_link_idx].style.vobj_path;
            TEST_ASSERT(strlen(target_path) > 0, "Virtual Body link points to valid chapter path");

            ER nav_er = tad_browser_navigate(&tb, target_path);
            TEST_ASSERT(nav_er == E_OK, "tad_browser_navigate successfully followed chapter link");
            TEST_ASSERT(tb.is_binary_tad == TRUE, "Target chapter loaded as valid binary TAD");
            TEST_ASSERT(tb.span_count > 0, "Chapter spans rendered in TAD Browser");
        }

        /* C. Ingest Unified Scroll Volume (shog-dril) */
        ER s_er = tad_browser_load_file(&tb, "tad_bin/osal_shog_dril.tad");
        TEST_ASSERT(s_er == E_OK, "tad_browser_load_file loaded complete roll volume tad_bin/osal_shog_dril.tad");
        TEST_ASSERT(tb.is_binary_tad == TRUE, "Scroll volume is valid binary TAD");
        TEST_ASSERT(tb.span_count > 50, "Scroll volume loaded comprehensive book spans (> 50)");
    }

    printf("\n=================================================================\n");
    printf(" ALL 28 CLARITY OSAL TIBETAN BOOK & TAD TESTS PASSED (100%%)\n");
    printf("=================================================================\n");
    return 0;
}
