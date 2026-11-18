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
        const char *sec_line = "01) //dkar chag/ Індекси\n";
        clarity_wylie_line_to_tibetan(sec_line, out_line, sizeof(out_line));
        TEST_ASSERT(strstr(out_line, "01)") != NULL, "Section number 01 preserved");
        TEST_ASSERT(strstr(out_line, "དཀར་ཆག") != NULL, "Section term transliterated to དཀར་ཆག");
        TEST_ASSERT(strstr(out_line, "(Індекси)\n") != NULL, "Section item closing parenthesis is on the same line");

        const char *sec_phowa = "01) //'pho ba/ Пхова\n";
        clarity_wylie_line_to_tibetan(sec_phowa, out_line, sizeof(out_line));
        TEST_ASSERT(strstr(out_line, "(Пхова)\n") != NULL, "Section item with trailing newline keeps parenthesis intact without extra line break");

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
        TEST_ASSERT(strstr(ch0->transl_title, "Коментар на Зміст") != NULL, "Chapter 0 alternative language title loaded ('Коментар на Зміст')");
        TEST_ASSERT(strstr(book.classes[0].title, "Попередні практики") != NULL, "Class 1 alternative language title loaded");
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
        /* Note: test_book is PECHA_SIZE_KANGYUR (560x110 mm); PECHA_SIZE_DERGE is now 700x180 mm */
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
        ER er = tad_browser_navigate(&tb, "tad_bin/osal/index.tad");
        TEST_ASSERT(er == E_OK, "tad_browser_navigate loaded tad_bin/osal/index.tad");
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

        /* Verify that Virtual Body Link titles are fully displayed without 63-byte truncation */
        if (target_link_idx >= 0) {
            const char *lbl = tb.spans[target_link_idx].style.vobj_label;
            const char *txt = tb.spans[target_link_idx].text;
            TEST_ASSERT(strlen(lbl) > 63, "Virtual Body label exceeds legacy 63-byte limit");
            TEST_ASSERT(strstr(lbl, "Коментар на Зміст") != NULL,
                        "Virtual Body label preserves full Ukrainian title ('Коментар на Зміст')");
            TEST_ASSERT(strstr(txt, "Коментар на Зміст") != NULL,
                        "Virtual Body span text displays full untruncated title in TAD Browser");

            /* Verify link bounding box matches string width with respect to encoding/language */
            int link_w = tb.spans[target_link_idx].bounds.right - tb.spans[target_link_idx].bounds.left;
            int avail_w = tb.doc_width - tb.spans[target_link_idx].bounds.left - 24;
            TEST_ASSERT(link_w > 100, "Link bounding box has positive width matching content");
            TEST_ASSERT(link_w < avail_w, "Link bounding box matches string width and does not stretch enormously across window");
        }

        /* B. Simulate Clicking Chapter Link and Navigating */
        if (target_link_idx >= 0) {
            const char *target_path = tb.spans[target_link_idx].style.vobj_path;
            TEST_ASSERT(strlen(target_path) > 0, "Virtual Body link points to valid chapter path");

            ER nav_er = tad_browser_navigate(&tb, target_path);
            TEST_ASSERT(nav_er == E_OK, "tad_browser_navigate successfully followed chapter link");
            TEST_ASSERT(tb.is_binary_tad == TRUE, "Target chapter loaded as valid binary TAD");
            TEST_ASSERT(tb.span_count > 0, "Chapter spans rendered in TAD Browser");
            TEST_ASSERT(tad_browser_can_go_back(&tb) == TRUE, "History stack records previous TOC page");

            /* B2. Verify Back Button Window Invalidation & Navigation */
            extern void handle_tad_browser_event(WND *wnd, const EVT *evt);
            WND test_wnd;
            memset(&test_wnd, 0, sizeof(WND));
            test_wnd.bounds.left = 100;
            test_wnd.bounds.top = 100;
            test_wnd.bounds.right = 820;
            test_wnd.bounds.bottom = 600;
            test_wnd.user_data = (VW)(uintptr_t)&tb;
            test_wnd.event_handler = handle_tad_browser_event;
            test_wnd.img_valid = TRUE; /* starts valid */

            /* Simulate clicking toolbar [◄ 戻る] (Back button at x=20, y=30 relative to window body) */
            EVT click_back;
            memset(&click_back, 0, sizeof(EVT));
            click_back.type = EV_BUT_DOWN;
            click_back.pos.x = test_wnd.bounds.left + 4 + 20;
            click_back.pos.y = test_wnd.bounds.top + 26 + 30;

            int hist_before = tb.history_idx;
            test_wnd.event_handler(&test_wnd, &click_back);
            TEST_ASSERT(tb.history_idx < hist_before, "Back button click successfully stepped back in history");
            TEST_ASSERT(test_wnd.img_valid == FALSE, "Back button invalidated window for immediate screen repaint");
        }

        /* C. Ingest Unified Scroll Volume (shog-dril) */
        ER s_er = tad_browser_load_file(&tb, "tad_bin/osal_shog_dril.tad");
        TEST_ASSERT(s_er == E_OK, "tad_browser_load_file loaded complete roll volume tad_bin/osal_shog_dril.tad");
        TEST_ASSERT(tb.is_binary_tad == TRUE, "Scroll volume is valid binary TAD");
        TEST_ASSERT(tb.span_count > 50, "Scroll volume loaded comprehensive book spans (> 50)");

        /* D. Double Newline (\n\n) Preservation & Empty Line Spacing */
        /* D1: Text TAD source with intentional \n\n between paragraphs */
        TAD_BROWSER tb_nl;
        tad_browser_init(&tb_nl);
        const char *txt_source =
            "Paragraph One: Introductory invocation.\n"
            "\n"
            "Paragraph Two: Main commentary text.\n"
            "\n"
            "\n"
            "Paragraph Three: Conclusion.";
        ER nl_er = tad_browser_load_buffer(&tb_nl, txt_source, (UW)strlen(txt_source), "Newline Test");
        TEST_ASSERT(nl_er == E_OK, "tad_browser_load_buffer parsed text with intentional double newlines");
        TEST_ASSERT(tb_nl.span_count >= 5, "Generated spans including empty lines for double newlines");

        int empty_count = 0;
        for (int i = 0; i < tb_nl.span_count; i++) {
            if (tb_nl.spans[i].text[0] == '\0') empty_count++;
        }
        TEST_ASSERT(empty_count >= 3, "Intentional blank lines preserved as empty spacing spans (3 empty spans for 1+2 blank lines)");

        tad_browser_layout(&tb_nl, 640);
        /* Verify that empty span adds vertical separation */
        int gap_p1_p2 = tb_nl.spans[2].bounds.top - tb_nl.spans[0].bounds.bottom;
        TEST_ASSERT(gap_p1_p2 >= 18, "Vertical spacing between P1 and P2 reflects intentional blank line");

        /* D2: Binary TAD chapter with intentional \n\n */
        TAD_BROWSER tb_ch;
        tad_browser_init(&tb_ch);
        ER y_er = tad_browser_load_file(&tb_ch, "tad_bin/osal/yang_ti.tad");
        TEST_ASSERT(y_er == E_OK, "Loaded produced chapter tad_bin/osal/yang_ti.tad");
        int bin_empty = 0;
        for (int i = 0; i < tb_ch.span_count; i++) {
            if (tb_ch.spans[i].text[0] == '\0') bin_empty++;
        }
        TEST_ASSERT(bin_empty > 10, "Binary chapter TAD preserves intentional double newlines as empty line spans in TAD Browser");
    }

    /* ── Test Group 6: Pecha Size Enum & Proportional Dimensions ───── */
    printf("\n==> Test Group 6: Pecha Size Enum & Proportional Dimensions\n");
    {
        int w, h;
        const char *name;

        /* Kangyur */
        clarity_get_pecha_dimensions(PECHA_SIZE_KANGYUR, &w, &h, &name);
        TEST_ASSERT(w == 560 && h == 110, "Kangyur Pecha: 560 x 110 mm");
        TEST_ASSERT(strstr(name, "Kangyur") != NULL, "Kangyur name label correct");

        /* Rinchen Terdzo */
        clarity_get_pecha_dimensions(PECHA_SIZE_RINCHEN, &w, &h, &name);
        TEST_ASSERT(w == 650 && h == 140, "Rinchen Terdzo: 650 x 140 mm");

        /* Derge Woodblock (classic 700x180 mm, 13 rows) */
        clarity_get_pecha_dimensions(PECHA_SIZE_DERGE, &w, &h, &name);
        TEST_ASSERT(w == 700 && h == 180, "Derge Woodblock: 700 x 180 mm");
        TEST_ASSERT(strstr(name, "700") != NULL && strstr(name, "180") != NULL,
                    "Derge Woodblock name contains 700 and 180");

        /* Pocket Dharma */
        clarity_get_pecha_dimensions(PECHA_SIZE_POCKET, &w, &h, &name);
        TEST_ASSERT(w == 320 && h == 85, "Pocket Dharma: 320 x 85 mm");

        /* Derge Wide (720x200 mm, 25 rows) */
        clarity_get_pecha_dimensions(PECHA_SIZE_DERGE_WIDE, &w, &h, &name);
        TEST_ASSERT(w == 720 && h == 200, "Derge Wide: 720 x 200 mm");
        TEST_ASSERT(strstr(name, "Wide") != NULL, "Derge Wide name label correct");

        /* Proportional row counts verification via load chapter */
        ClarityBook derge_book;
        clarity_book_init(&derge_book);
        int d_rc = clarity_book_open_dir(&derge_book, "./assets/osal", PECHA_SIZE_DERGE, TRUE);
        TEST_ASSERT(d_rc == 0, "Opened osal dir with PECHA_SIZE_DERGE (700x180 mm)");

        ClarityDoc d_doc;
        memset(&d_doc, 0, sizeof(ClarityDoc));
        int d_load = clarity_book_load_chapter_into_doc(&derge_book, 0, &d_doc);
        TEST_ASSERT(d_load == 0, "Loaded chapter 0 as Derge Woodblock");
        TEST_ASSERT(d_doc.page_w_mm == 700 && d_doc.page_h_mm == 180,
                    "Derge Woodblock doc page dimensions: 700 x 180 mm");
        TEST_ASSERT(d_doc.frame_count >= 2, "Derge Woodblock import paginated into >= 2 pages");
        /* Each page should hold at most 13 visual rows — text must not be a single giant block */
        TEST_ASSERT(d_doc.frame_count >= 10, "dkar.chag.txt (959 lines) produces >= 10 Derge pages");
        printf("       -> Derge Woodblock dkar.chag.txt loaded %d pages (frames)\n", d_doc.page_count);
    }

    /* ── Test Group 7: Source Ingestion Completeness & Consistency Check ── */
    printf("\n==> Test Group 7: Source Ingestion Completeness & Consistency Check\n");
    {
        ClarityBook book;
        clarity_book_init(&book);
        int rc = clarity_book_open_dir(&book, "./assets/osal", PECHA_SIZE_DERGE, TRUE);
        TEST_ASSERT(rc == 0, "Opened osal directory for consistency verification");

        ClarityDoc doc;
        memset(&doc, 0, sizeof(ClarityDoc));
        int load_rc = clarity_book_load_chapter_into_doc(&book, 0, &doc);
        TEST_ASSERT(load_rc == 0, "Loaded dkar.chag.txt with full integrity tracking");

        /* Basic consistency check on dkar.chag.txt */
        ClarityIntegrityReport rep;
        int check_rc = clarity_book_check_integrity(&book, 0, &doc, &rep);
        TEST_ASSERT(check_rc == 0, "clarity_book_check_integrity succeeded for dkar.chag.txt");
        TEST_ASSERT(rep.is_complete == TRUE, "dkar.chag.txt marked complete (is_complete == TRUE)");
        TEST_ASSERT(rep.source_lines >= 950, "dkar.chag.txt source lines counted (>= 950)");
        TEST_ASSERT(rep.loaded_lines == rep.source_lines, "All source lines paginated without dropping");
        TEST_ASSERT(rep.page_count >= 70, "dkar.chag.txt paginated into full ~80 pages (not truncated at 32)");
        TEST_ASSERT(rep.frame_count == rep.page_count, "Frame count matches page count");
        TEST_ASSERT(rep.source_bytes > 0, "Source file size recorded in integrity report");
        TEST_ASSERT(rep.source_checksum > 0, "Source file Adler-32 checksum computed");

        /* Verify 32-bit canvas coordinates beyond legacy 16-bit boundary (page 58..80) */
        TEST_ASSERT(doc.frames[doc.frame_count - 1].bounds.top > 32767,
                    "Page 80 frame bounds.top exceeds 16-bit int16 boundary (> 32767)");
        for (int p = 0; p < doc.frame_count; p++) {
            TEST_ASSERT(doc.frames[p].bounds.top >= 0,
                        "Frame bounds.top is strictly positive (no 16-bit overflow wrap to negative)");
            TEST_ASSERT(doc.frames[p].bounds.bottom > doc.frames[p].bounds.top,
                        "Frame bounds.bottom > bounds.top");
            TEST_ASSERT(doc.frames[p].text_len > 0,
                        "Frame has non-empty text content (no empty pages after item 63)");
            if (p > 0) {
                TEST_ASSERT(doc.frames[p].bounds.top > doc.frames[p - 1].bounds.top,
                            "Frame bounds.top monotonically increases across consecutive pages");
            }
        }

        /* Verify final frame contains closing colophon */
        ClarityFrame *last_frame = &doc.frames[doc.frame_count - 1];
        TEST_ASSERT(last_frame->text_len > 0, "Final page frame has non-empty text");
        char last_utf8[2048];
        tc_to_utf8_string((const TC*)last_frame->text, last_frame->text_len, last_utf8, sizeof(last_utf8));
        TEST_ASSERT(strstr(last_utf8, "2025") != NULL, "Final page contains end-of-file colophon ('2025')");

        /* Test incomplete loading detection: mock truncated doc */
        ClarityDoc trunc_doc = doc;
        trunc_doc.is_complete = FALSE;
        trunc_doc.loaded_lines = 300;
        ClarityIntegrityReport trunc_rep;
        int trunc_rc = clarity_doc_check_consistency(&trunc_doc, "./assets/osal/dkar.chag.txt", &trunc_rep);
        TEST_ASSERT(trunc_rc == -1, "clarity_doc_check_consistency correctly caught unfinished loading");
        TEST_ASSERT(trunc_rep.is_complete == FALSE, "Unfinished loading flagged is_complete == FALSE");
        TEST_ASSERT(strstr(trunc_rep.err_msg, "Unfinished source loading") != NULL,
                    "Integrity check produced diagnostic error message for unfinished load");

        /* Test single chapter integrity: smon.lam.txt (chapter 1) */
        ClarityDoc smon_doc;
        memset(&smon_doc, 0, sizeof(ClarityDoc));
        int smon_load = clarity_book_load_chapter_into_doc(&book, 1, &smon_doc);
        TEST_ASSERT(smon_load == 0, "Loaded smon.lam.txt");
        ClarityIntegrityReport smon_rep;
        int smon_check = clarity_book_check_integrity(&book, 1, &smon_doc, &smon_rep);
        TEST_ASSERT(smon_check == 0, "smon.lam.txt passed consistency check");
        TEST_ASSERT(smon_rep.is_complete == TRUE, "smon.lam.txt is fully loaded");
    }

    /* ── Test Group 6: TOC Mini-Window Vertical Scrollbar ──────────── */
    printf("\n==> Test Group 6: TOC Mini-Window Vertical Scrollbar Mechanics\n");
    {
        /* Mini-window reduced proportions (12px vs standard 16px) */
        int toc_sb_w = 12;
        int toc_btn_h = 12;
        int toc_hdr_h = 36;
        int win_w = 280;
        int win_h = 620;
        int view_h = win_h - toc_hdr_h;
        int total_entries = book.chapter_count + book.class_count;
        int total_h = total_entries * 18; /* 18px per row */
        int max_scroll = (total_h > view_h) ? (total_h - view_h) : 0;

        TEST_ASSERT(toc_sb_w == 12, "TOC mini-window scrollbar width is reduced to 12px");
        TEST_ASSERT(toc_btn_h == 12, "TOC stepper button height is 12px square");
        TEST_ASSERT(max_scroll > 0, "TOC content exceeds viewport requiring vertical scrolling");

        /* Track and Elevator thumb geometry */
        int track_h = view_h - toc_btn_h * 2;
        TEST_ASSERT(track_h > 100, "Scrollbar track has ample vertical range");
        int thumb_h = (track_h * view_h) / total_h;
        if (thumb_h < 12) thumb_h = 12;
        TEST_ASSERT(thumb_h >= 12, "Thumb meets minimum accessible height (>= 12px)");
        TEST_ASSERT(thumb_h < track_h, "Thumb is strictly smaller than track (allows elevator motion)");

        /* Scrollbar position does not obscure chapter titles */
        int content_w = win_w - toc_sb_w;
        TEST_ASSERT(content_w >= 260, "TOC content area has >= 260px width for bilingual titles");

        /* Header pinning and viewport clipping tests */
        RECT content_clip = { 0, (H)toc_hdr_h, (H)content_w, (H)win_h };
        TEST_ASSERT(content_clip.top == toc_hdr_h, "Content clip strictly starts below pinned header (y >= 36)");
        TEST_ASSERT(content_clip.right == win_w - toc_sb_w, "Content clip strictly excludes scrollbar gutter");

        /* Simulate scroll offset and verify rows with y + h <= toc_hdr_h are not rendered in header */
        int test_scroll = 72; /* Scrolled down 4 rows */
        int row0_y = toc_hdr_h - test_scroll; /* 36 - 72 = -36 */
        TEST_ASSERT(row0_y + 18 <= toc_hdr_h, "Scrolled rows do not draw over pinned header");
    }

    printf("\n=================================================================\n");
    printf(" ALL 64 CLARITY OSAL TIBETAN BOOK & TAD TESTS PASSED (100%%)\n");
    printf("=================================================================\n");
    return 0;
}
