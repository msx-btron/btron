/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Tibetan Book Architecture (clarity_book.c)
 * Implementation of Osal format parser, classifier reconstruction,
 * EWTS Wylie -> Tibetan script conversion, Pecha formatting,
 * and BTRON3 legal binary TAD book generation.
 */

#include "clarity_book.h"
#include <btron/wylie.h>
#include <btron/troncode.h>
#include <btron/tad.h>
#include <btron/libc_shim.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if BTRON_HOSTED
#include <sys/stat.h>
#include <dirent.h>
#endif

static ClarityBook s_active_book;

/* Canonical reading order matching shog.dril.sh */
static const char *s_canonical_chapters[] = {
    "dkar.chag.txt",
    "smon.lam.txt",
    "sngon.'gro.txt",
    "khrus.txt",
    "dge.bsngo.txt",
    "rnam.thar.txt",
    "lo.rgyud.txt",
    "dbu.ma.txt",
    "rnal.'byor.spyod.pa.txt",
    "rgyud.txt",
    "dbang.txt",
    "mchod.pa.txt",
    "sngags.txt",
    "dkyil.'khor.txt",
    "tshogs.txt",
    "chod.txt",
    "sgrub.thabs.txt",
    "lha.srung.txt",
    "lha.txt",
    "bka'.brgyad.txt",
    "zhi.khro.txt",
    "tshe.sgrub.txt",
    "bla.ma.rnal.'byor.txt",
    "gtum.mo.txt",
    "rtsa.rlung.thig.txt",
    "nang.gsang.rnal.'byor.txt",
    "sems.sde.txt",
    "khregs.chod.txt",
    "thod.rgal.txt",
    "'od.gsal.txt",
    "bar.do.txt",
    "yang.ti.txt",
    /* Auxiliary texts */
    "dgon.mdzod.txt",
    "lam.rim.txt",
    "mkha'.'gro.txt",
    "skong.bshags.txt",
    "yon.tan.mdzod.txt",
    "zhi.gnas.txt",
    "test.txt",
    NULL
};

/* ── Pecha Paper Dimensions ─────────────────────────────────────── */

void clarity_get_pecha_dimensions(ClarityPechaSize size, int *w_mm, int *h_mm, const char **name_out)
{
    int w = 560, h = 110;
    const char *name = "Kangyur Pecha (560x110 mm)";

    switch (size) {
        case PECHA_SIZE_KANGYUR:
            w = 560; h = 110;
            name = "Kangyur Pecha (560x110 mm)";
            break;
        case PECHA_SIZE_RINCHEN:
            w = 650; h = 140;
            name = "Rinchen Terdzo (650x140 mm)";
            break;
        case PECHA_SIZE_DERGE:
            w = 700; h = 180;
            name = "Derge Woodblock (700x180 mm)";
            break;
        case PECHA_SIZE_POCKET:
            w = 320; h = 85;
            name = "Pocket Dharma (320x85 mm)";
            break;
        default:
            break;
    }
    if (w_mm) *w_mm = w;
    if (h_mm) *h_mm = h;
    if (name_out) *name_out = name;
}

/* ── Wylie Transliteration Helpers ───────────────────────────────── */

static BOOL is_wylie_str(const char *s, int len)
{
    if (!s || len <= 0) return FALSE;
    int letter_count = 0;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (isalpha((unsigned char)c)) letter_count++;
        else if (c != ' ' && c != '\'' && c != '+' && c != '.' &&
                 c != '_' && c != '-' && c != '~' && c != ':') {
            return FALSE;
        }
    }
    return (letter_count > 0);
}

/* Converts a single line of Osal text, transliterating Wylie parts to Tibetan */
int clarity_wylie_line_to_tibetan(const char *in_line, char *out_line, size_t max_out)
{
    if (!in_line || !out_line || max_out == 0) return 0;
    out_line[0] = '\0';

    const char *p = in_line;
    while (*p == ' ' || *p == '\t') p++;

    /* 1. Master text / work header starting with @#/_/ or @#/ */
    if (strncmp(p, "@#/_/", 5) == 0 || strncmp(p, "@#/", 3) == 0) {
        const char *content = (p[3] == '_') ? p + 5 : p + 3;
        char part1[256] = "", part2[256] = "", part3[512] = "";

        const char *s1 = strchr(content, '/');
        if (s1) {
            int len1 = (int)(s1 - content);
            if (len1 > 255) len1 = 255;
            strncpy(part1, content, len1);
            part1[len1] = '\0';

            const char *content2 = s1 + 1;
            while (*content2 == ' ') content2++;
            const char *s2 = strchr(content2, '/');
            if (s2) {
                int len2 = (int)(s2 - content2);
                if (len2 > 255) len2 = 255;
                strncpy(part2, content2, len2);
                part2[len2] = '\0';

                const char *content3 = s2 + 1;
                while (*content3 == ' ') content3++;
                strncpy(part3, content3, sizeof(part3) - 1);
            } else {
                strncpy(part2, content2, sizeof(part2) - 1);
            }
        } else {
            strncpy(part1, content, sizeof(part1) - 1);
        }

        /* Clean trailing newlines/spaces */
        for (char *c = part1 + strlen(part1) - 1; c >= part1 && (*c == ' ' || *c == '\n' || *c == '\r'); c--) *c = '\0';
        for (char *c = part2 + strlen(part2) - 1; c >= part2 && (*c == ' ' || *c == '\n' || *c == '\r'); c--) *c = '\0';
        for (char *c = part3 + strlen(part3) - 1; c >= part3 && (*c == ' ' || *c == '\n' || *c == '\r'); c--) *c = '\0';

        char tb1[512] = "", tb2[512] = "";
        if (part1[0]) wylie_to_tibetan(part1, tb1, sizeof(tb1));
        if (part2[0]) wylie_to_tibetan(part2, tb2, sizeof(tb2));

        if (tb1[0] && tb2[0] && part3[0]) {
            snprintf(out_line, max_out, "༄༅། །%s །%s །%s\n", tb1, tb2, part3);
        } else if (tb1[0] && part3[0]) {
            snprintf(out_line, max_out, "༄༅། །%s །%s\n", tb1, part3);
        } else if (tb1[0] && tb2[0]) {
            snprintf(out_line, max_out, "༄༅། །%s །%s\n", tb1, tb2);
        } else if (tb1[0]) {
            snprintf(out_line, max_out, "༄༅། །%s\n", tb1);
        } else {
            snprintf(out_line, max_out, "%s\n", in_line);
        }
        return (int)strlen(out_line);
    }

    /* 2. Top-level Class Roman header: e.g. "I) /sngon 'gro.../ Class 1..." */
    if ((p[0] == 'I' || p[0] == 'V' || p[0] == 'X') && (p[1] == ')' || p[2] == ')')) {
        const char *sl1 = strchr(p, '/');
        if (sl1) {
            const char *sl2 = strchr(sl1 + 1, '/');
            if (sl2) {
                char roman[16] = "";
                int r_len = (int)(sl1 - p);
                if (r_len > 15) r_len = 15;
                strncpy(roman, p, r_len);
                roman[r_len] = '\0';
                for (char *c = roman + strlen(roman) - 1; c >= roman && (*c == ' ' || *c == ')'); c--) *c = '\0';

                char wylie_cls[256] = "";
                int w_len = (int)(sl2 - sl1 - 1);
                if (w_len > 255) w_len = 255;
                strncpy(wylie_cls, sl1 + 1, w_len);
                wylie_cls[w_len] = '\0';

                char tb_cls[512] = "";
                wylie_to_tibetan(wylie_cls, tb_cls, sizeof(tb_cls));

                const char *rest = sl2 + 1;
                while (*rest == ' ') rest++;
                snprintf(out_line, max_out, "%s) ༄༅། %s — %s\n", roman, tb_cls, rest);
                return (int)strlen(out_line);
            }
        }
    }

    /* 3. Section enumerations: e.g. "01) //dkar chag/ Title" or "01) /dkar chag/ Title" */
    if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) && p[2] == ')') {
        const char *sl1 = strchr(p, '/');
        if (sl1) {
            while (*sl1 == '/') sl1++;
            const char *sl2 = strchr(sl1, '/');
            if (sl2) {
                char num_buf[16] = "";
                strncpy(num_buf, p, 3);
                num_buf[3] = '\0';

                char wylie_sec[256] = "";
                int w_len = (int)(sl2 - sl1);
                if (w_len > 255) w_len = 255;
                strncpy(wylie_sec, sl1, w_len);
                wylie_sec[w_len] = '\0';

                char tb_sec[512] = "";
                wylie_to_tibetan(wylie_sec, tb_sec, sizeof(tb_sec));

                const char *rest = sl2 + 1;
                while (*rest == ' ') rest++;
                snprintf(out_line, max_out, "%s %s (%s)\n", num_buf, tb_sec, rest);
                return (int)strlen(out_line);
            }
        }
    }

    /* 3b. Numbered entries: "N) wylie_text: Translation"
     *     The wylie portion is everything between ") " and ":", if it looks like Wylie.
     *     e.g. "1) rdo rje gsung: Ваджрні склади"
     *          "4) badz+ra gu ru bla ma'i rnal 'byor: гуру-йога Падмасамбхави"
     */
    if (isdigit((unsigned char)p[0])) {
        const char *paren = strchr(p, ')');
        if (paren && paren[1] == ' ') {
            /* Number prefix e.g. "1) " or "12) " */
            char num_part[8] = "";
            int np_len = (int)(paren - p + 2); /* "N) " */
            if (np_len > 7) np_len = 7;
            strncpy(num_part, p, np_len);
            num_part[np_len] = '\0';

            const char *wstart = paren + 2; /* text after "N) " */
            const char *colon  = strchr(wstart, ':');
            if (colon) {
                int w_len = (int)(colon - wstart);
                /* Only convert if chunk looks like Tibetan Wylie */
                if (w_len > 0 && w_len < 200) {
                    char wylie_part[200];
                    strncpy(wylie_part, wstart, w_len);
                    wylie_part[w_len] = '\0';
                    /* Strip trailing spaces */
                    for (char *c = wylie_part + w_len - 1; c >= wylie_part && *c == ' '; c--) *c = '\0';
                    if (is_wylie_str(wylie_part, (int)strlen(wylie_part))) {
                        char tb_part[512] = "";
                        wylie_to_tibetan(wylie_part, tb_part, sizeof(tb_part));
                        const char *def = colon + 1;
                        while (*def == ' ') def++;
                        snprintf(out_line, max_out, "%s%s: %s", num_part, tb_part, def);
                        /* Ensure newline */
                        size_t ol = strlen(out_line);
                        if (ol > 0 && out_line[ol-1] != '\n' && ol + 1 < max_out) {
                            out_line[ol] = '\n'; out_line[ol+1] = '\0';
                        }
                        return (int)strlen(out_line);
                    }
                }
            }
        }
    }

    /* 4. Definition entries starting with /wylie/ Definition */
    if (p[0] == '/') {
        const char *sl2 = strchr(p + 1, '/');
        if (sl2 && (sl2 - p) < 128) {
            char wylie_term[128] = "";
            int w_len = (int)(sl2 - p - 1);
            strncpy(wylie_term, p + 1, w_len);
            wylie_term[w_len] = '\0';

            if (is_wylie_str(wylie_term, w_len)) {
                char tb_term[256] = "";
                wylie_to_tibetan(wylie_term, tb_term, sizeof(tb_term));
                const char *def = sl2 + 1;
                while (*def == ' ') def++;
                snprintf(out_line, max_out, "[ %s། ] %s\n", tb_term, def);
                return (int)strlen(out_line);
            }
        }
    }

    /* 4b. Catalog entries starting with digits: e.g. "001 wylie (cat)/ transl" or "4. wylie/ transl" */
    if (isdigit((unsigned char)p[0])) {
        const char *sl = strchr(p, '/');
        if (sl && sl > p + 1) {
            const char *num_end = p;
            while (isdigit((unsigned char)*num_end)) num_end++;
            if (*num_end == '.' || *num_end == ' ') {
                if (*num_end == '.') num_end++;
                while (*num_end == ' ') num_end++;

                int n_len = (int)(num_end - p);
                char num_buf[32] = "";
                if (n_len > 31) n_len = 31;
                strncpy(num_buf, p, n_len);
                num_buf[n_len] = '\0';

                int w_len = (int)(sl - num_end);
                if (w_len > 0 && w_len < 512) {
                    char w_buf[512];
                    strncpy(w_buf, num_end, w_len);
                    w_buf[w_len] = '\0';
                    for (char *c = w_buf + strlen(w_buf) - 1; c >= w_buf && *c == ' '; c--) *c = '\0';

                    char cat_tb[256] = "";
                    char *lp = strchr(w_buf, '(');
                    char *rp = lp ? strchr(lp, ')') : NULL;
                    if (lp && rp && rp > lp) {
                        int c_len = (int)(rp - lp - 1);
                        if (c_len > 0 && c_len < 127) {
                            char cat_w[128];
                            strncpy(cat_w, lp + 1, c_len);
                            cat_w[c_len] = '\0';
                            wylie_to_tibetan(cat_w, cat_tb, sizeof(cat_tb));
                        }
                        *lp = '\0';
                        for (char *c = w_buf + strlen(w_buf) - 1; c >= w_buf && *c == ' '; c--) *c = '\0';
                    }

                    if (is_wylie_str(w_buf, (int)strlen(w_buf))) {
                        char tb_text[512] = "";
                        wylie_to_tibetan(w_buf, tb_text, sizeof(tb_text));
                        const char *transl = sl + 1;
                        while (*transl == ' ') transl++;
                        if (cat_tb[0]) {
                            snprintf(out_line, max_out, "%s%s (%s) — %s", num_buf, tb_text, cat_tb, transl);
                        } else {
                            snprintf(out_line, max_out, "%s%s — %s", num_buf, tb_text, transl);
                        }
                        size_t ol = strlen(out_line);
                        if (ol > 0 && out_line[ol - 1] != '\n' && ol + 1 < max_out) {
                            out_line[ol] = '\n'; out_line[ol + 1] = '\0';
                        }
                        return (int)strlen(out_line);
                    }
                }
            }
        }
    }

    /* 4c. Term definitions with slash: "wylie / translation" e.g. "rtsa ba/ root", "thig le/ Тіглє" */
    {
        const char *sl = strchr(p, '/');
        if (sl && sl != p && *(sl - 1) != '@' && *(sl + 1) != '/') {
            int w_len = (int)(sl - p);
            if (w_len > 0 && w_len < 256) {
                char w_buf[256];
                strncpy(w_buf, p, w_len);
                w_buf[w_len] = '\0';
                for (char *c = w_buf + strlen(w_buf) - 1; c >= w_buf && *c == ' '; c--) *c = '\0';
                if (is_wylie_str(w_buf, (int)strlen(w_buf))) {
                    char tb_term[512] = "";
                    wylie_to_tibetan(w_buf, tb_term, sizeof(tb_term));
                    const char *def = sl + 1;
                    while (*def == ' ') def++;
                    snprintf(out_line, max_out, "[ %s། ] %s", tb_term, def);
                    size_t ol = strlen(out_line);
                    if (ol > 0 && out_line[ol - 1] != '\n' && ol + 1 < max_out) {
                        out_line[ol] = '\n'; out_line[ol + 1] = '\0';
                    }
                    return (int)strlen(out_line);
                }
            }
        }
    }

    /* 4d. Lines with // ... /: e.g. "1) //Самозвільнене мислення སེམས་ཉིད་རང་གྲོལ། sems nyid rang grol/ ..." */
    if (strstr(p, "//")) {
        const char *dbs = strstr(p, "//");
        const char *sl = strchr(dbs + 2, '/');
        if (sl) {
            const char *mid = dbs + 2;
            int mid_len = (int)(sl - mid);
            char mid_buf[512];
            if (mid_len > 0 && mid_len < 511) {
                strncpy(mid_buf, mid, mid_len);
                mid_buf[mid_len] = '\0';
                char *tail = mid_buf + mid_len - 1;
                while (tail >= mid_buf && *tail == ' ') *tail-- = '\0';
                char *wstart = tail;
                while (wstart >= mid_buf && (isalpha((unsigned char)*wstart) || *wstart == ' ' || *wstart == '\'')) {
                    wstart--;
                }
                wstart++;
                while (*wstart == ' ') wstart++;
                if (wstart <= tail && is_wylie_str(wstart, (int)strlen(wstart)) && strlen(wstart) >= 3) {
                    char tb_w[256] = "";
                    wylie_to_tibetan(wstart, tb_w, sizeof(tb_w));
                    *wstart = '\0';
                    const char *def = sl + 1;
                    while (*def == ' ') def++;
                    int pre_len = (int)(dbs - p);
                    char pre_buf[64] = "";
                    if (pre_len > 0 && pre_len < 63) {
                        strncpy(pre_buf, p, pre_len);
                        pre_buf[pre_len] = '\0';
                    }
                    snprintf(out_line, max_out, "%s%s %s — %s", pre_buf, mid_buf, tb_w, def);
                    size_t ol = strlen(out_line);
                    if (ol > 0 && out_line[ol - 1] != '\n' && ol + 1 < max_out) {
                        out_line[ol] = '\n'; out_line[ol + 1] = '\0';
                    }
                    return (int)strlen(out_line);
                }
            }
        }
    }

    /* 5. Prose lines with embedded parenthesized Wylie (e.g. "(thugs rje zer du gnas pa ni...)") */
    size_t in_len = strlen(in_line);
    size_t o = 0;
    for (size_t i = 0; i < in_len && o + 8 < max_out; ) {
        if (in_line[i] == '(') {
            const char *c_close = strchr(&in_line[i + 1], ')');
            if (c_close && (c_close - &in_line[i + 1]) < 128) {
                int p_len = (int)(c_close - &in_line[i + 1]);
                char p_buf[128];
                strncpy(p_buf, &in_line[i + 1], p_len);
                p_buf[p_len] = '\0';

                /* Only convert if it looks like Tibetan Wylie (letters, spaces, dots, tsheg, apostrophes) */
                if (is_wylie_str(p_buf, p_len) && !isdigit((unsigned char)p_buf[0])) {
                    char tb_sub[256] = "";
                    wylie_to_tibetan(p_buf, tb_sub, sizeof(tb_sub));
                    o += snprintf(out_line + o, max_out - o, "(%s)", tb_sub);
                    i += (p_len + 2);
                    continue;
                }
            }
        }
        out_line[o++] = in_line[i++];
    }
    out_line[o] = '\0';
    return (int)o;
}

int clarity_wylie_to_tibetan_text(const char *in_text, char *out_text, size_t max_out)
{
    if (!in_text || !out_text || max_out == 0) return 0;
    out_text[0] = '\0';

    size_t o = 0;
    const char *p = in_text;
    while (*p && o + 256 < max_out) {
        const char *next_nl = strchr(p, '\n');
        char line[1024];
        int l_len = next_nl ? (int)(next_nl - p) : (int)strlen(p);
        if (l_len > 1023) l_len = 1023;
        strncpy(line, p, l_len);
        line[l_len] = '\0';

        char conv_line[1536];
        clarity_wylie_line_to_tibetan(line, conv_line, sizeof(conv_line));

        size_t c_len = strlen(conv_line);
        if (o + c_len < max_out - 2) {
            memcpy(out_text + o, conv_line, c_len);
            o += c_len;
            if (conv_line[c_len - 1] != '\n') {
                out_text[o++] = '\n';
            }
        }
        if (!next_nl) break;
        p = next_nl + 1;
    }
    out_text[o] = '\0';
    return (int)o;
}

/* ── Book Lifecycle & Directory Parser ───────────────────────────── */

void clarity_book_init(ClarityBook *book)
{
    if (!book) return;
    memset(book, 0, sizeof(ClarityBook));
    book->pecha_size = PECHA_SIZE_KANGYUR;
    book->auto_tibetan = TRUE;
    book->active_chapter_idx = 0;
    book->is_loaded = FALSE;
    strncpy(book->title, "Лонгчен Осал", sizeof(book->title) - 1);
    strncpy(book->subtitle, "klong chen 'od gsal snying thig", sizeof(book->subtitle) - 1);
    strncpy(book->tibetan_title, "ཀློང་ཆེན་འོད་གསལ་སྙིང་ཐིག", sizeof(book->tibetan_title) - 1);
}

static void init_default_classes(ClarityBook *book)
{
    book->class_count = 5;

    /* Class I */
    book->classes[0].id = 1;
    strcpy(book->classes[0].roman, "I");
    strcpy(book->classes[0].wylie, "sngon 'gro dang gzhi'i chos");
    strcpy(book->classes[0].tibetan, "སྔོན་འགྲོ་དང་གཞིའི་ཆོས");
    strcpy(book->classes[0].title, "Попередні практики і фундаментальна Дхарма");

    /* Class II */
    book->classes[1].id = 2;
    strcpy(book->classes[1].roman, "II");
    strcpy(book->classes[1].wylie, "gzhung dang lta ba'i chos");
    strcpy(book->classes[1].tibetan, "གཞུང་དང་ལྟ་བའི་ཆོས");
    strcpy(book->classes[1].title, "Тексти і філософська Дхарма");

    /* Class III */
    book->classes[2].id = 3;
    strcpy(book->classes[2].roman, "III");
    strcpy(book->classes[2].wylie, "bskyed rim mchod pa'i cho ga");
    strcpy(book->classes[2].tibetan, "བསྐྱེད་རིམ་མཆོད་པའི་ཆོ་ག");
    strcpy(book->classes[2].title, "Ритуали і церемонії стадії Зародження");

    /* Class IV */
    book->classes[3].id = 4;
    strcpy(book->classes[3].roman, "IV");
    strcpy(book->classes[3].wylie, "rdzogs rim");
    strcpy(book->classes[3].tibetan, "རྫོགས་རིམ");
    strcpy(book->classes[3].title, "Практики стадії Завершення");

    /* Class V */
    book->classes[4].id = 5;
    strcpy(book->classes[4].roman, "V");
    strcpy(book->classes[4].wylie, "rdzogs chen gyi nyams len");
    strcpy(book->classes[4].tibetan, "རྫོགས་ཆེན་གྱི་ཉམས་ལེན");
    strcpy(book->classes[4].title, "Практики остаточного довершення (Дзогчен)");
}

static int map_file_to_class(const char *filename)
{
    if (strstr(filename, "dkar.chag") || strstr(filename, "smon.lam") ||
        strstr(filename, "sngon.'gro") || strstr(filename, "khrus") ||
        strstr(filename, "dge.bsngo")  || strstr(filename, "rnam.thar") ||
        strstr(filename, "lo.rgyud")   || strstr(filename, "zhi.gnas")) {
        return 1;
    }
    if (strstr(filename, "dbu.ma") || strstr(filename, "rnal.'byor.spyod.pa") ||
        strstr(filename, "rgyud")  || strstr(filename, "yon.tan.mdzod")) {
        return 2;
    }
    if (strstr(filename, "dbang") || strstr(filename, "mchod.pa") ||
        strstr(filename, "sngags") || strstr(filename, "dkyil.'khor") ||
        strstr(filename, "tshogs") || strstr(filename, "chod") ||
        strstr(filename, "sgrub.thabs") || strstr(filename, "lha.srung") ||
        strstr(filename, "lha.txt") || strstr(filename, "bka'.brgyad") ||
        strstr(filename, "zhi.khro") || strstr(filename, "tshe.sgrub") ||
        strstr(filename, "bla.ma.rnal.'byor") || strstr(filename, "skong.bshags") ||
        strstr(filename, "mkha'.'gro")) {
        return 3;
    }
    if (strstr(filename, "gtum.mo") || strstr(filename, "rtsa.rlung.thig") ||
        strstr(filename, "nang.gsang.rnal.'byor")) {
        return 4;
    }
    if (strstr(filename, "sems.sde") || strstr(filename, "khregs.chod") ||
        strstr(filename, "thod.rgal") || strstr(filename, "'od.gsal") ||
        strstr(filename, "bar.do")    || strstr(filename, "yang.ti")) {
        return 5;
    }
    return 1;
}

int clarity_book_open_dir(ClarityBook *book, const char *dir_path, ClarityPechaSize pecha_size, BOOL auto_tibetan)
{
    if (!book || !dir_path) return -1;
    clarity_book_init(book);
    book->pecha_size = pecha_size;
    book->auto_tibetan = auto_tibetan;
    strncpy(book->base_dir, dir_path, sizeof(book->base_dir) - 1);

    init_default_classes(book);

#if BTRON_HOSTED
    /* Support opening a single .txt file directly as Chapter 0 */
    struct stat st_single;
    if (stat(dir_path, &st_single) == 0 && !S_ISDIR(st_single.st_mode)) {
        ClarityBookChapter *ch = &book->chapters[book->chapter_count];
        ch->id = 1;
        ch->class_id = map_file_to_class(dir_path);
        const char *slash = strrchr(dir_path, '/');
        const char *fname = slash ? slash + 1 : dir_path;
        strncpy(ch->filename, fname, sizeof(ch->filename) - 1);
        strncpy(ch->file_path, dir_path, sizeof(ch->file_path) - 1);
        ch->file_size = (size_t)st_single.st_size;

        char w_name[128] = "";
        strncpy(w_name, fname, sizeof(w_name) - 1);
        char *dot = strstr(w_name, ".txt");
        if (dot) *dot = '\0';
        for (char *c = w_name; *c; c++) if (*c == '.') *c = ' ';
        strncpy(ch->wylie_title, w_name, sizeof(ch->wylie_title) - 1);
        wylie_to_tibetan(w_name, ch->tibetan_title, sizeof(ch->tibetan_title));

        FILE *fp = fopen(dir_path, "r");
        if (fp) {
            char line_buf[512];
            int l_cnt = 0;
            while (fgets(line_buf, sizeof(line_buf), fp)) {
                l_cnt++;
                if (l_cnt == 1 && line_buf[0]) {
                    const char *s = strrchr(line_buf, '/');
                    if (s && s[1]) {
                        const char *tr = s + 1;
                        while (*tr == ' ') tr++;
                        strncpy(ch->transl_title, tr, sizeof(ch->transl_title) - 1);
                        char *nl = strchr(ch->transl_title, '\n');
                        if (nl) *nl = '\0';
                    }
                }
            }
            ch->line_count = l_cnt;
            fclose(fp);
        }
        if (!ch->transl_title[0]) {
            strncpy(ch->transl_title, ch->wylie_title, sizeof(ch->transl_title) - 1);
        }
        int cls_idx = ch->class_id - 1;
        if (cls_idx >= 0 && cls_idx < book->class_count) {
            book->classes[cls_idx].chapter_indices[book->classes[cls_idx].chapter_count++] = 0;
        }
        book->chapter_count = 1;
        book->is_loaded = TRUE;
        s_active_book = *book;
        return 0;
    }
#endif

    /* Enumerate canonical chapters in order */
    for (int i = 0; s_canonical_chapters[i] != NULL && book->chapter_count < CLARITY_MAX_CHAPTERS; i++) {
        const char *cname = s_canonical_chapters[i];
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, cname);

#if BTRON_HOSTED
        struct stat st;
        if (stat(fullpath, &st) != 0) {
            /* Try relative to cwd or assets/osal */
            snprintf(fullpath, sizeof(fullpath), "./%s/%s", dir_path, cname);
            if (stat(fullpath, &st) != 0) continue;
        }

        ClarityBookChapter *ch = &book->chapters[book->chapter_count];
        ch->id = book->chapter_count + 1;
        ch->class_id = map_file_to_class(cname);
        strncpy(ch->filename, cname, sizeof(ch->filename) - 1);
        strncpy(ch->file_path, fullpath, sizeof(ch->file_path) - 1);
        ch->file_size = (size_t)st.st_size;

        /* Derive Wylie title from filename (strip .txt) */
        char w_name[128] = "";
        strncpy(w_name, cname, sizeof(w_name) - 1);
        char *dot = strstr(w_name, ".txt");
        if (dot) *dot = '\0';
        for (char *c = w_name; *c; c++) if (*c == '.') *c = ' ';
        strncpy(ch->wylie_title, w_name, sizeof(ch->wylie_title) - 1);

        /* Transliterate title to Tibetan */
        wylie_to_tibetan(w_name, ch->tibetan_title, sizeof(ch->tibetan_title));

        /* Read preview from file */
        FILE *fp = fopen(fullpath, "r");
        if (fp) {
            char line_buf[512];
            int l_cnt = 0;
            while (fgets(line_buf, sizeof(line_buf), fp)) {
                l_cnt++;
                if (l_cnt == 1 && line_buf[0]) {
                    /* If line is @#/_/ header, extract translation */
                    const char *s = strrchr(line_buf, '/');
                    if (s && s[1]) {
                        const char *tr = s + 1;
                        while (*tr == ' ') tr++;
                        strncpy(ch->transl_title, tr, sizeof(ch->transl_title) - 1);
                        char *nl = strchr(ch->transl_title, '\n');
                        if (nl) *nl = '\0';
                    }
                }
                if (l_cnt <= 4 && strlen(ch->text_preview) + strlen(line_buf) < sizeof(ch->text_preview) - 1) {
                    strcat(ch->text_preview, line_buf);
                }
            }
            ch->line_count = l_cnt;
            fclose(fp);
        }
        if (!ch->transl_title[0]) {
            strncpy(ch->transl_title, ch->wylie_title, sizeof(ch->transl_title) - 1);
        }

        /* Register in class */
        int cls_idx = ch->class_id - 1;
        if (cls_idx >= 0 && cls_idx < book->class_count) {
            ClarityBookClass *c = &book->classes[cls_idx];
            if (c->chapter_count < CLARITY_MAX_CHAPTERS) {
                c->chapter_indices[c->chapter_count++] = book->chapter_count;
            }
        }

        book->chapter_count++;
#endif
    }

    if (book->chapter_count > 0) {
        book->is_loaded = TRUE;
        s_active_book = *book;
        return 0;
    }
    return -1;
}

ClarityBook* clarity_get_book(void)
{
    return &s_active_book;
}

int clarity_book_load_chapter_into_doc(ClarityBook *book, int chapter_idx, ClarityDoc *doc)
{
    if (!book || !doc || chapter_idx < 0 || chapter_idx >= book->chapter_count) return -1;
    const ClarityBookChapter *ch = &book->chapters[chapter_idx];

    /* Set up Pecha document layout */
    doc->fmt = FMT_PECHA;
    clarity_get_pecha_dimensions(book->pecha_size, &doc->page_w_mm, &doc->page_h_mm, NULL);
    doc->page_count  = 0;
    doc->active_page = 0;
    doc->frame_count = 0;
    doc->selected_frame = -1;

#if BTRON_HOSTED
    FILE *fp = fopen(ch->file_path, "r");
    if (!fp) return -1;

    /* Pecha page pixel dimensions (at unzoomed 72dpi) */
    int px_w = (doc->page_w_mm * CLARITY_DPI * 10) / 254;
    int px_h = (doc->page_h_mm * CLARITY_DPI * 10) / 254;
    int margin_x = 40, margin_y = 20;
    int inner_h  = px_h - margin_y * 2;
    int glyph_lh = 19;  /* GLYPH_H(16) + 3 line spacing */

    /* How many lines of body text fit on one Pecha folio */
    int lines_per_page = (inner_h > glyph_lh) ? (inner_h / glyph_lh) : 8;
    if (lines_per_page < 4) lines_per_page = 4;

    /* Accumulate all file lines into heap buffer */
    char  *raw_lines[4096];
    int    raw_count = 0;
    char  line_buf[1024];

    /* Pecha chapter header occupies first few lines */
    {
        char head[640];
        snprintf(head, sizeof(head),
                 "[PECHA %s]\n༄༅། །%s །%s\n────────────────────────────────────────\n",
                 ch->filename, ch->tibetan_title, ch->transl_title);
        raw_lines[raw_count] = (char*)malloc(strlen(head) + 1);
        if (raw_lines[raw_count]) { strcpy(raw_lines[raw_count], head); raw_count++; }
    }

    while (fgets(line_buf, sizeof(line_buf), fp) && raw_count < 4094) {
        char conv[2048];
        if (book->auto_tibetan) {
            clarity_wylie_line_to_tibetan(line_buf, conv, sizeof(conv));
        } else {
            strncpy(conv, line_buf, sizeof(conv) - 1);
            conv[sizeof(conv) - 1] = '\0';
        }
        raw_lines[raw_count] = (char*)malloc(strlen(conv) + 1);
        if (raw_lines[raw_count]) { strcpy(raw_lines[raw_count], conv); raw_count++; }
    }
    fclose(fp);

    /* Page gap in unzoomed pixels (matches clarity_draw_page p_gap at 100%) */
    int p_gap = CLARITY_PAGE_GAP_PX;
    if (p_gap < 24) p_gap = 24;

    /* Distribute lines into pages of CLARITY_MAX_FRAMES max */
    int page_idx = 0;
    int line_cursor = 0;
    int max_pages = CLARITY_MAX_FRAMES;
    if (max_pages > 32) max_pages = 32;

    while (line_cursor < raw_count && page_idx < max_pages) {
        /* Accumulate one page worth of lines */
        char page_buf[16384];
        page_buf[0] = '\0';
        int lines_on_page = 0;

        while (line_cursor < raw_count && lines_on_page < lines_per_page) {
            const char *ln = raw_lines[line_cursor];
            /* Count newlines in ln to avoid overflowing visual lines */
            int newlines_here = 0;
            for (const char *p = ln; *p; p++) {
                if (*p == '\n') newlines_here++;
            }
            if (newlines_here == 0) newlines_here = 1;
            if (strlen(page_buf) + strlen(ln) < sizeof(page_buf) - 2) {
                strcat(page_buf, ln);
                lines_on_page += newlines_here;
            } else {
                break; /* page_buf full — flush */
            }
            line_cursor++;
        }

        /* Frame Y-origin for this page (each page stacked below previous) */
        int frame_y = margin_y + page_idx * (px_h + p_gap);

        /* Create one FRAME_TEXT per page, positioned on its own page sheet */
        ClarityFrame *f = clarity_doc_add_frame(doc, FRAME_TEXT,
                                                margin_x, frame_y,
                                                px_w - margin_x * 2,
                                                px_h - margin_y * 2);
        if (f) {
            f->flow = FLOW_H_LTR;
            int tc_len = utf8_to_tc_string(page_buf, (TC*)f->text, CLARITY_TEXT_BUF - 1);
            f->text_len = (tc_len > 0) ? (UW)tc_len : 0;
            f->text[f->text_len] = 0;
            f->cursor_pos = 0;
            f->scroll_y   = 0;
            strncpy(f->text_path, ch->file_path, sizeof(f->text_path) - 1);

            /* Mark first frame with chapter moniker */
            if (page_idx == 0) {
                f->vobj_count = 1;
                f->vobjs[0].text_offset  = 0;
                f->vobjs[0].target_robj  = 1000 + ch->id;
                f->vobjs[0].type         = VOBJ_TYPE_TEXT;
                strncpy(f->vobjs[0].label, ch->tibetan_title, sizeof(f->vobjs[0].label) - 1);
                strncpy(f->vobjs[0].path,  ch->file_path,     sizeof(f->vobjs[0].path)  - 1);
            }
        }
        page_idx++;
    }

    /* Free heap lines */
    for (int i = 0; i < raw_count; i++) {
        free(raw_lines[i]);
    }

    int total_pages = (page_idx > 0) ? page_idx : 1;
    if (total_pages < 2) total_pages = 2; /* At least a 2-page Pecha spread */
    if (total_pages > max_pages) total_pages = max_pages;

    /* Fill all remaining pages in doc with rectangular text frame placeholders */
    while (page_idx < total_pages) {
        int frame_y = margin_y + page_idx * (px_h + p_gap);
        ClarityFrame *f = clarity_doc_add_frame(doc, FRAME_TEXT,
                                                margin_x, frame_y,
                                                px_w - margin_x * 2,
                                                px_h - margin_y * 2);
        if (f) {
            f->flow = FLOW_H_LTR;
            f->text_len = 0;
            f->text[0] = 0;
            f->cursor_pos = 0;
            f->scroll_y   = 0;
            strncpy(f->text_path, ch->file_path, sizeof(f->text_path) - 1);
        }
        page_idx++;
    }

    doc->page_count     = total_pages;
    doc->selected_frame = 0;
    doc->dirty          = FALSE;
    book->active_chapter_idx = chapter_idx;
    s_active_book = *book;
    return 0;
#else
    return -1;
#endif
}

int clarity_book_next_chapter(ClarityBook *book, ClarityDoc *doc)
{
    if (!book || !book->is_loaded || book->chapter_count == 0) return -1;
    int next_idx = (book->active_chapter_idx + 1) % book->chapter_count;
    return clarity_book_load_chapter_into_doc(book, next_idx, doc);
}

int clarity_book_prev_chapter(ClarityBook *book, ClarityDoc *doc)
{
    if (!book || !book->is_loaded || book->chapter_count == 0) return -1;
    int prev_idx = (book->active_chapter_idx - 1 + book->chapter_count) % book->chapter_count;
    return clarity_book_load_chapter_into_doc(book, prev_idx, doc);
}

/* ── BTRON3 Legal Binary TAD Serializer ───────────────────────────── */

static inline void tad_write_u16(UB *p, UH val) {
    p[0] = (UB)((val >> 8) & 0xFF);
    p[1] = (UB)(val & 0xFF);
}

static inline void tad_write_u32(UB *p, UW val) {
    p[0] = (UB)((val >> 24) & 0xFF);
    p[1] = (UB)((val >> 16) & 0xFF);
    p[2] = (UB)((val >> 8) & 0xFF);
    p[3] = (UB)(val & 0xFF);
}

static UW append_segment(UB *buf, UW pos, UH tag, const UB *payload, UW plen) {
    buf[pos++] = (UB)((tag >> 8) & 0xFF);
    buf[pos++] = (UB)(tag & 0xFF);
    tad_write_u32(&buf[pos], plen);
    pos += 4;
    if (plen > 0 && payload) {
        memcpy(&buf[pos], payload, plen);
        pos += plen;
    }
    return pos;
}

static UW append_page_seg(UB *buf, UW pos, int w_px, int h_px, int margin) {
    UB p[14];
    p[0] = 0; p[1] = 0;
    tad_write_u16(&p[2], (UH)h_px);
    tad_write_u16(&p[4], (UH)w_px);
    tad_write_u16(&p[6], (UH)margin);
    tad_write_u16(&p[8], (UH)margin);
    tad_write_u16(&p[10], (UH)margin);
    tad_write_u16(&p[12], (UH)margin);
    return append_segment(buf, pos, 0xFFA0, p, 14);
}

static UW append_font_seg(UB *buf, UW pos, UH font_id, UB plane) {
    UB p[4];
    p[0] = 0;
    tad_write_u16(&p[1], font_id);
    p[3] = plane;
    return append_segment(buf, pos, 0xFFA2, p, 4);
}

static UW append_char_seg(UB *buf, UW pos, UH size_pt, UH weight, UW color_rgb) {
    UB p[9];
    p[0] = 0;
    tad_write_u16(&p[1], size_pt);
    tad_write_u16(&p[3], weight);
    tad_write_u32(&p[5], color_rgb);
    return append_segment(buf, pos, 0xFFA3, p, 9);
}

static UW append_ruler_seg(UB *buf, UW pos, UH pitch, UH indent) {
    UB p[5];
    p[0] = 0;
    tad_write_u16(&p[1], pitch);
    tad_write_u16(&p[3], indent);
    return append_segment(buf, pos, 0xFFA1, p, 5);
}

static UW append_vobj_seg(UB *buf, UW pos, UW target_id, const char *label, const char *path) {
    UH lb_len = (UH)strlen(label);
    UH pb_len = (UH)strlen(path);
    UW plen = 1 + 4 + 2 + lb_len + 2 + pb_len;
    UB *p = (UB*)malloc(plen);
    if (!p) return pos;

    UW off = 0;
    p[off++] = 0;
    tad_write_u32(&p[off], target_id); off += 4;
    tad_write_u16(&p[off], lb_len);   off += 2;
    memcpy(&p[off], label, lb_len);   off += lb_len;
    tad_write_u16(&p[off], pb_len);   off += 2;
    memcpy(&p[off], path, pb_len);    off += pb_len;

    pos = append_segment(buf, pos, 0xFFA8, p, plen);
    free(p);
    return pos;
}

static UW append_hr_seg(UB *buf, UW pos, int width) {
    UB p[16];
    p[0] = 1; /* SubID 1: Vector Line */
    p[1] = 0;
    tad_write_u32(&p[2], 0x888888);
    tad_write_u16(&p[6], 0);
    tad_write_u16(&p[8], 0);
    tad_write_u16(&p[10], (UH)width);
    tad_write_u16(&p[12], 0);
    return append_segment(buf, pos, 0xFFB0, p, 14);
}

static UW append_text(UB *buf, UW pos, const char *utf8_text) {
    if (!utf8_text) return pos;
    size_t len = strlen(utf8_text);
    memcpy(&buf[pos], utf8_text, len);
    pos += (UW)len;
    if (len == 0 || utf8_text[len - 1] != '\n') {
        buf[pos++] = '\n';
    }
    return pos;
}

int clarity_book_export_tad(const ClarityBook *book, const char *out_dir, const char *single_tad_path)
{
    if (!book || !book->is_loaded) return -1;

#if BTRON_HOSTED
    const char *target_dir = (out_dir && out_dir[0]) ? out_dir : "tad_bin/osal";
    mkdir("tad_bin", 0755);
    mkdir(target_dir, 0755);

    int pecha_w_mm = 560, pecha_h_mm = 110;
    clarity_get_pecha_dimensions(book->pecha_size, &pecha_w_mm, &pecha_h_mm, NULL);
    int px_w = (pecha_w_mm * CLARITY_DPI * 10) / 254;
    int px_h = (pecha_h_mm * CLARITY_DPI * 10) / 254;

    /* ── A. Generate Master TOC Index (index.tad) ─────────────────── */
    char toc_path[512];
    snprintf(toc_path, sizeof(toc_path), "%s/index.tad", target_dir);

    UW buf_cap = 4 * 1024 * 1024;  /* 4 MB — large enough for 1000-line Tibetan chapters */
    UB *buf = (UB*)malloc(buf_cap);
    if (!buf) return -1;

    UW pos = 6; /* Leave 6 bytes for Record Type 1 header */

    /* Initialization segments */
    pos = append_page_seg(buf, pos, px_w, px_h, 30);
    pos = append_font_seg(buf, pos, 0, 1);
    pos = append_char_seg(buf, pos, 18, 700, 0x002266);
    pos = append_ruler_seg(buf, pos, 28, 0);

    /* Title & Banner */
    pos = append_text(buf, pos, "༄༅། །ཀློང་ཆེན་འོད་གསལ་སྙིང་ཐིག དཀར་ཆག་བཞུགས་སོ། །");
    pos = append_hr_seg(buf, pos, px_w - 60);

    pos = append_char_seg(buf, pos, 12, 400, 0x333333);
    pos = append_ruler_seg(buf, pos, 20, 0);
    pos = append_text(buf, pos, "Лонгчен Осал: Канонічний каталог тибетських термінів та творів (5 Класів)");
    pos = append_text(buf, pos, "");

    /* Table of contents classes and chapters */
    for (int c = 0; c < book->class_count; c++) {
        const ClarityBookClass *cls = &book->classes[c];

        pos = append_char_seg(buf, pos, 14, 700, 0x882200);
        pos = append_ruler_seg(buf, pos, 24, 0);

        char cls_hdr[512];
        snprintf(cls_hdr, sizeof(cls_hdr), "▶ Клас %s: ༄༅། %s — %s", cls->roman, cls->tibetan, cls->title);
        pos = append_text(buf, pos, cls_hdr);
        pos = append_hr_seg(buf, pos, px_w - 80);

        pos = append_char_seg(buf, pos, 11, 400, 0x000000);
        pos = append_ruler_seg(buf, pos, 18, 15);

        for (int ci = 0; ci < cls->chapter_count; ci++) {
            int ch_idx = cls->chapter_indices[ci];
            if (ch_idx >= 0 && ch_idx < book->chapter_count) {
                const ClarityBookChapter *ch = &book->chapters[ch_idx];

                char ch_tad_name[64];
                snprintf(ch_tad_name, sizeof(ch_tad_name), "%s.tad", ch->wylie_title);
                for (char *p = ch_tad_name; *p; p++) if (*p == ' ') *p = '_';

                char vobj_lbl[256];
                snprintf(vobj_lbl, sizeof(vobj_lbl), "%02d. ༄༅། %s — %s", ci + 1, ch->tibetan_title, ch->transl_title);

                pos = append_vobj_seg(buf, pos, 2000 + ch->id, vobj_lbl, ch_tad_name);
            }
        }
        pos = append_text(buf, pos, "");
    }

    /* Wrap in Record Type 1 */
    UW payload_len = pos - 6;
    buf[0] = 0x00; buf[1] = 0x01;
    tad_write_u32(&buf[2], payload_len);

    FILE *f_toc = fopen(toc_path, "wb");
    if (f_toc) {
        fwrite(buf, 1, pos, f_toc);
        fclose(f_toc);
    }

    /* ── B. Generate Individual Chapter TAD Files ─────────────────── */
    for (int i = 0; i < book->chapter_count; i++) {
        const ClarityBookChapter *ch = &book->chapters[i];

        char ch_tad_name[64];
        snprintf(ch_tad_name, sizeof(ch_tad_name), "%s.tad", ch->wylie_title);
        for (char *p = ch_tad_name; *p; p++) if (*p == ' ') *p = '_';

        char ch_path[512];
        snprintf(ch_path, sizeof(ch_path), "%s/%s", target_dir, ch_tad_name);

        pos = 6;
        pos = append_page_seg(buf, pos, px_w, px_h, 30);
        pos = append_font_seg(buf, pos, 0, 1);
        pos = append_char_seg(buf, pos, 16, 700, 0x003366);
        pos = append_ruler_seg(buf, pos, 26, 0);

        /* Chapter Title */
        char ch_title_str[512];
        snprintf(ch_title_str, sizeof(ch_title_str), "༄༅། །%s །%s", ch->tibetan_title, ch->transl_title);
        pos = append_text(buf, pos, ch_title_str);
        pos = append_hr_seg(buf, pos, px_w - 60);

        /* Back link to TOC */
        pos = append_vobj_seg(buf, pos, 1000, "⌂ 目録 (TOC / དཀར་ཆག)", "index.tad");
        pos = append_text(buf, pos, "");

        /* Body text */
        pos = append_char_seg(buf, pos, 11, 400, 0x000000);
        pos = append_ruler_seg(buf, pos, 18, 0);

        FILE *f_src = fopen(ch->file_path, "r");
        if (f_src) {
            char l_buf[4096];
            while (fgets(l_buf, sizeof(l_buf), f_src) && pos + 8192 < buf_cap) {
                char conv_line[8192];
                if (book->auto_tibetan) {
                    clarity_wylie_line_to_tibetan(l_buf, conv_line, sizeof(conv_line));
                } else {
                    strncpy(conv_line, l_buf, sizeof(conv_line) - 1);
                    conv_line[sizeof(conv_line) - 1] = '\0';
                }
                pos = append_text(buf, pos, conv_line);
            }
            fclose(f_src);
        }

        /* Footer link */
        pos = append_text(buf, pos, "");
        pos = append_hr_seg(buf, pos, px_w - 60);
        pos = append_vobj_seg(buf, pos, 1000, "⌂ 目録に戻る (Return to TOC)", "index.tad");

        payload_len = pos - 6;
        buf[0] = 0x00; buf[1] = 0x01;
        tad_write_u32(&buf[2], payload_len);

        FILE *f_ch = fopen(ch_path, "wb");
        if (f_ch) {
            fwrite(buf, 1, pos, f_ch);
            fclose(f_ch);
        }
    }

    /* ── C. Generate Unified Master Scroll Volume (shog.dril.tad) ─── */
    const char *unified_path = (single_tad_path && single_tad_path[0]) ?
                                single_tad_path : "tad_bin/osal_shog_dril.tad";

    pos = 6;
    pos = append_page_seg(buf, pos, px_w, px_h, 30);
    pos = append_font_seg(buf, pos, 0, 1);
    pos = append_char_seg(buf, pos, 20, 700, 0x002255);
    pos = append_ruler_seg(buf, pos, 30, 0);

    /* Frontispiece */
    pos = append_text(buf, pos, "༄༅། །ཀློང་ཆེན་འོད་གསལ་སྙིང་ཐིག ཤོག་དྲིལ་བཞུགས་སོ། །");
    pos = append_hr_seg(buf, pos, px_w - 60);

    pos = append_char_seg(buf, pos, 12, 400, 0x444444);
    pos = append_ruler_seg(buf, pos, 20, 0);
    pos = append_text(buf, pos, "Лонгчен Осал — Повний сувій (Shog-dril Roll Volume)");
    pos = append_text(buf, pos, "");

    for (int i = 0; i < book->chapter_count && pos + 8192 < buf_cap; i++) {
        const ClarityBookChapter *ch = &book->chapters[i];

        pos = append_char_seg(buf, pos, 14, 700, 0x882200);
        pos = append_ruler_seg(buf, pos, 24, 0);

        char ch_sec_hdr[512];
        snprintf(ch_sec_hdr, sizeof(ch_sec_hdr), "■ ལེའུ %02d. ༄༅། %s — %s", i + 1, ch->tibetan_title, ch->transl_title);
        pos = append_text(buf, pos, ch_sec_hdr);
        pos = append_hr_seg(buf, pos, px_w - 60);

        pos = append_char_seg(buf, pos, 11, 400, 0x000000);
        pos = append_ruler_seg(buf, pos, 18, 0);

        FILE *f_src = fopen(ch->file_path, "r");
        if (f_src) {
            char l_buf[4096];
            while (fgets(l_buf, sizeof(l_buf), f_src) && pos + 8192 < buf_cap) {
                char conv_line[8192];
                if (book->auto_tibetan) {
                    clarity_wylie_line_to_tibetan(l_buf, conv_line, sizeof(conv_line));
                } else {
                    strncpy(conv_line, l_buf, sizeof(conv_line) - 1);
                    conv_line[sizeof(conv_line) - 1] = '\0';
                }
                pos = append_text(buf, pos, conv_line);
            }
            fclose(f_src);
        }
        pos = append_text(buf, pos, "");
    }

    payload_len = pos - 6;
    buf[0] = 0x00; buf[1] = 0x01;
    tad_write_u32(&buf[2], payload_len);

    FILE *f_shog = fopen(unified_path, "wb");
    if (f_shog) {
        fwrite(buf, 1, pos, f_shog);
        fclose(f_shog);
    }

    free(buf);
    return 0;
#else
    return -1;
#endif
}
