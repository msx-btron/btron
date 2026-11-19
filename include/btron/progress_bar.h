/*
 * B-System (BTRON 3.20) Reusable Haiku-Style Progress Bar Control: progress_bar.h
 * Modeled on Haiku's BStatusBar adapted to BTRON 90s HMI / Meishi 3D bevels.
 *
 * Features:
 *  - Top row: left-aligned primary action label + right-aligned metric/percentage
 *  - 2px sunken 3D bevel track with glossy multi-band Haiku steel-blue fill
 *  - Indeterminate diagonal barber-pole stripe animation when maximum == 0
 *  - Bottom live file/path readout with automatic leading "..." truncation
 *  - NASA/JPL Rule 3: 100% static / zero heap allocation
 */

#ifndef _BTRON_PROGRESS_BAR_H_
#define _BTRON_PROGRESS_BAR_H_

#include <btron/types.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/libc_shim.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTRON_PB_COL_WELL          0xFFFBF9F5  /* Warm Meishi ivory track well */
#define BTRON_PB_COL_SHADOW        0xFF8A857A  /* Outer sunken bevel shadow */
#define BTRON_PB_COL_SHADOW_DARK   0xFF504C44  /* Inner sunken bevel dark */
#define BTRON_PB_COL_HIGHLIGHT     0xFFF4F2EC  /* Inner bottom-right light */
#define BTRON_PB_COL_TEXT_PRIMARY  0xFF141923  /* Deep charcoal label */
#define BTRON_PB_COL_TEXT_MUTED    0xFF5C6573  /* Slate gray live file path */

/* Haiku BStatusBar Glossy Steel-Blue Fill Palette */
#define BTRON_PB_COL_FILL_SPECULAR 0xFF6EAEE8  /* 1px top specular highlight */
#define BTRON_PB_COL_FILL_SHEEN    0xFF4A8CC7  /* Upper gloss band */
#define BTRON_PB_COL_FILL_BODY     0xFF2B608A  /* Primary BTRON/Haiku steel blue */
#define BTRON_PB_COL_FILL_DEPTH    0xFF1F4A6E  /* Lower depth band */
#define BTRON_PB_COL_FILL_EDGE     0xFF143552  /* Crisp leading-edge separator */

typedef struct {
    RECT        rect;               /* Full control bounds (top labels + 22px bar + detail) */
    uint32_t    current;            /* Current progress value (bytes or files) */
    uint32_t    maximum;            /* Maximum value (0 = indeterminate barber-pole mode) */
    uint32_t    anim_tick;          /* Animation tick for indeterminate stripes */
    char        label[64];          /* Top-left action text */
    char        trailing_text[48];  /* Top-right metric (e.g. "42% (84/200 files)") */
    char        detail_text[96];    /* Bottom live file path (e.g. "Copying: /SYS/Apps/Editor") */
    COLOR       fill_color;         /* Primary fill color (0 = default Haiku steel blue) */
} BTRON_PROGRESS_BAR;

static inline void btron_pb_safe_copy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static inline void btron_progress_bar_init(BTRON_PROGRESS_BAR *pb, const RECT *r, const char *label) {
    if (!pb) return;
    memset(pb, 0, sizeof(*pb));
    if (r) pb->rect = *r;
    if (label) btron_pb_safe_copy(pb->label, label, sizeof(pb->label));
    pb->fill_color = BTRON_PB_COL_FILL_BODY;
}

static inline void btron_progress_bar_update(BTRON_PROGRESS_BAR *pb,
                                             uint32_t current, uint32_t maximum,
                                             const char *trailing_text,
                                             const char *detail_text) {
    if (!pb) return;
    pb->current = current;
    pb->maximum = maximum;
    pb->anim_tick++;
    if (trailing_text) btron_pb_safe_copy(pb->trailing_text, trailing_text, sizeof(pb->trailing_text));
    if (detail_text)   btron_pb_safe_copy(pb->detail_text, detail_text, sizeof(pb->detail_text));
}

static inline void btron_progress_bar_paint(GDEV *dev, const BTRON_PROGRESS_BAR *pb, COLOR bg_col) {
    if (!dev || !pb) return;

    int left  = pb->rect.left;
    int top   = pb->rect.top;
    int right = pb->rect.right;
    int full_w = right - left;
    if (full_w < 40) return;

    /* 1. Top Label Row: Left action label + Right trailing metric */
    if (pb->label[0]) {
        drw_tc_string(dev, (H)left, (H)top, pb->label, BTRON_PB_COL_TEXT_PRIMARY, bg_col);
    }
    if (pb->trailing_text[0]) {
        int tw = (int)strlen(pb->trailing_text) * 8;
        int tx = right - tw;
        if (tx < left + 120) tx = left + 120;
        drw_tc_string(dev, (H)tx, (H)top, pb->trailing_text, BTRON_PB_COL_TEXT_MUTED, bg_col);
    }

    /* 2. Sunken 2px 3D Bevel Track (22px tall) */
    RECT tr = { (H)left, (H)(top + 18), (H)right, (H)(top + 40) };
    fill_rec(dev, &tr, BTRON_PB_COL_WELL);

    /* Outer 1px sunken bevel */
    set_col(dev, BTRON_PB_COL_SHADOW, bg_col);
    drw_lin(dev, tr.left, tr.top, tr.right - 1, tr.top);
    drw_lin(dev, tr.left, tr.top, tr.left, tr.bottom - 1);
    set_col(dev, COLOR_WHITE, bg_col);
    drw_lin(dev, tr.left, tr.bottom - 1, tr.right - 1, tr.bottom - 1);
    drw_lin(dev, tr.right - 1, tr.top, tr.right - 1, tr.bottom - 1);

    /* Inner 1px sunken bevel */
    set_col(dev, BTRON_PB_COL_SHADOW_DARK, bg_col);
    drw_lin(dev, (H)(tr.left + 1), (H)(tr.top + 1), (H)(tr.right - 2), (H)(tr.top + 1));
    drw_lin(dev, (H)(tr.left + 1), (H)(tr.top + 1), (H)(tr.left + 1), (H)(tr.bottom - 2));
    set_col(dev, BTRON_PB_COL_HIGHLIGHT, bg_col);
    drw_lin(dev, (H)(tr.left + 1), (H)(tr.bottom - 2), (H)(tr.right - 2), (H)(tr.bottom - 2));
    drw_lin(dev, (H)(tr.right - 2), (H)(tr.top + 1), (H)(tr.right - 2), (H)(tr.bottom - 2));

    /* 3. Progress Fill or Indeterminate Barber-Pole Stripes */
    int inner_l = tr.left + 2;
    int inner_t = tr.top + 2;
    int inner_r = tr.right - 2;
    int inner_b = tr.bottom - 2;
    int inner_w = inner_r - inner_l;
    int inner_h = inner_b - inner_t;

    if (inner_w > 0 && inner_h >= 6) {
        if (pb->maximum == 0) {
            /* Indeterminate mode: Haiku-style diagonal animated stripes */
            int phase = (int)((pb->anim_tick * 3U) % 16U);
            for (int y = 0; y < inner_h; y++) {
                COLOR c = (y < 2) ? BTRON_PB_COL_FILL_SPECULAR :
                          (y < 5) ? BTRON_PB_COL_FILL_SHEEN : BTRON_PB_COL_FILL_BODY;
                set_col(dev, c, BTRON_PB_COL_WELL);
                for (int x = 0; x < inner_w; x++) {
                    if (((x + y + phase) & 15) < 8) {
                        drw_lin(dev, (H)(inner_l + x), (H)(inner_t + y),
                                     (H)(inner_l + x), (H)(inner_t + y));
                    }
                }
            }
        } else {
            uint32_t cur = (pb->current > pb->maximum) ? pb->maximum : pb->current;
            int fill_w = (int)(((uint64_t)inner_w * (uint64_t)cur) / (uint64_t)pb->maximum);
            if (fill_w > 0) {
                int fx1 = inner_l;
                int fx2 = inner_l + fill_w;

                /* Glossy multi-band Haiku BStatusBar fill */
                RECT r_body = { (H)fx1, (H)inner_t, (H)fx2, (H)inner_b };
                fill_rec(dev, &r_body, pb->fill_color ? pb->fill_color : BTRON_PB_COL_FILL_BODY);

                /* 1px top specular highlight */
                set_col(dev, BTRON_PB_COL_FILL_SPECULAR, BTRON_PB_COL_WELL);
                drw_lin(dev, (H)fx1, (H)inner_t, (H)(fx2 - 1), (H)inner_t);

                /* 3px upper sheen band */
                RECT r_sheen = { (H)fx1, (H)(inner_t + 1), (H)fx2, (H)(inner_t + 4) };
                fill_rec(dev, &r_sheen, BTRON_PB_COL_FILL_SHEEN);

                /* 2px lower depth band */
                RECT r_depth = { (H)fx1, (H)(inner_b - 2), (H)fx2, (H)inner_b };
                fill_rec(dev, &r_depth, BTRON_PB_COL_FILL_DEPTH);

                /* Crisp leading-edge separator */
                if (fill_w < inner_w) {
                    set_col(dev, BTRON_PB_COL_FILL_EDGE, BTRON_PB_COL_WELL);
                    drw_lin(dev, (H)(fx2 - 1), (H)inner_t, (H)(fx2 - 1), (H)(inner_b - 1));
                }
            }
        }
    }
    set_col(dev, COLOR_BLACK, bg_col);

    /* 4. Bottom Live File Path Readout (with leading "..." truncation) */
    if (pb->detail_text[0]) {
        int max_chars = full_w / 8;
        if (max_chars < 8) max_chars = 8;
        if (max_chars > 95) max_chars = 95;

        size_t dlen = strlen(pb->detail_text);
        if ((int)dlen <= max_chars) {
            drw_tc_string(dev, (H)left, (H)(tr.bottom + 6), pb->detail_text,
                          BTRON_PB_COL_TEXT_MUTED, bg_col);
        } else {
            char trunc[96];
            trunc[0] = '.'; trunc[1] = '.'; trunc[2] = '.';
            int keep = max_chars - 3;
            const char *tail = pb->detail_text + (dlen - (size_t)keep);
            for (int k = 0; k < keep; k++) trunc[3 + k] = tail[k];
            trunc[3 + keep] = '\0';
            drw_tc_string(dev, (H)left, (H)(tr.bottom + 6), trunc,
                          BTRON_PB_COL_TEXT_MUTED, bg_col);
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_PROGRESS_BAR_H_ */
