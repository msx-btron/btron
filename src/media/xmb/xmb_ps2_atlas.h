/*
 * xmb_ps2_atlas.h -- PS2 in-place icon-plane bake.
 *
 * TinyGL pads 384→512.  Occupied cells except IC_DROPLET paint a navy
 * badge in the glyph bounding box (STROKE interiors and 2×2 gaps included)
 * at A=255, with the white glyph lerped on top.  Cell corners outside that
 * box stay A=0 so source-over keeps the ribbon instead of stamping a 64×64
 * navy square.  Droplet keeps coverage alpha for additive particles.
 * Host-gateable: the same function is the test_ps2_gs_tex fill check.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */
#ifndef XMB_PS2_ATLAS_H
#define XMB_PS2_ATLAS_H

#include <stdint.h>
#include "xmb_icons.h"
/* Caller has already included xmb_atlas.h (xb_icon_alpha). The generated
 * sheet has no include guard, so this file must not pull it in again. */

#define XB_ICON_PAD 512

static inline uint32_t xb_ps2_pack_rgba(unsigned r, unsigned g, unsigned b,
                                        unsigned a)
{
    /* Canvas-order PIXEL: A top, R in the low byte, B at 16. */
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
           ((uint32_t)g << 8) | (uint32_t)r;
}

static inline void xb_ps2_fill_icon_plane(uint32_t *pm, int pad_w)
{
    int x, y, cell;

    for (y = 0; y < XB_ICON_PAD; y++)
        for (x = 0; x < XB_ICON_PAD; x++)
            pm[y * pad_w + x] = 0u;

    for (cell = 0; cell < XB_ICON_CELLS; cell++) {
        int ox = (cell % XB_ATLAS_GRID) * XB_ICON_CELL;
        int oy = (cell / XB_ATLAS_GRID) * XB_ICON_CELL;
        int cx, cy, has = 0;
        int bx0, by0, bx1, by1;

        for (cy = 0; cy < XB_ICON_CELL && !has; cy++)
            for (cx = 0; cx < XB_ICON_CELL; cx++)
                if (xb_icon_alpha[(oy + cy) * XB_ICON_TEX + (ox + cx)])
                    has = 1;
        if (!has)
            continue;

        /* Particles sample IC_DROPLET with SRC_ALPHA, GL_ONE.  An opaque
         * badge would add a navy square on every sparkle. */
        if (cell == IC_DROPLET) {
            for (cy = 0; cy < XB_ICON_CELL; cy++) {
                for (cx = 0; cx < XB_ICON_CELL; cx++) {
                    unsigned char a = xb_icon_alpha[(oy + cy) * XB_ICON_TEX
                                                    + (ox + cx)];
                    if (a)
                        pm[(oy + cy) * pad_w + (ox + cx)] =
                            xb_ps2_pack_rgba(248u, 250u, 255u, (unsigned)a);
                }
            }
            continue;
        }

        /* Tight box around coverage, plus one texel for LINEAR.  The
         * Applications 2×2 gap and a Commands window interior sit inside
         * this box (coverage is 0 there) and must be navy, not A=0. */
        bx0 = XB_ICON_CELL;
        by0 = XB_ICON_CELL;
        bx1 = -1;
        by1 = -1;
        for (cy = 0; cy < XB_ICON_CELL; cy++) {
            for (cx = 0; cx < XB_ICON_CELL; cx++) {
                if (!xb_icon_alpha[(oy + cy) * XB_ICON_TEX + (ox + cx)])
                    continue;
                if (cx < bx0) bx0 = cx;
                if (cy < by0) by0 = cy;
                if (cx > bx1) bx1 = cx;
                if (cy > by1) by1 = cy;
            }
        }
        if (bx0 > 0) bx0--;
        if (by0 > 0) by0--;
        if (bx1 < XB_ICON_CELL - 1) bx1++;
        if (by1 < XB_ICON_CELL - 1) by1++;

        for (cy = 0; cy < XB_ICON_CELL; cy++) {
            for (cx = 0; cx < XB_ICON_CELL; cx++) {
                unsigned char a = xb_icon_alpha[(oy + cy) * XB_ICON_TEX
                                                + (ox + cx)];
                uint32_t *dst = &pm[(oy + cy) * pad_w + (ox + cx)];
                unsigned ia = (unsigned)a;
                unsigned r8, g8, b8;
                int in_badge = (cx >= bx0 && cx <= bx1 &&
                                cy >= by0 && cy <= by1);

                if (!in_badge)
                    continue;
                r8 = (12u * (255u - ia) + 248u * ia) / 255u;
                g8 = (20u * (255u - ia) + 250u * ia) / 255u;
                b8 = (40u * (255u - ia) + 255u * ia) / 255u;
                *dst = xb_ps2_pack_rgba(r8, g8, b8, 255u);
            }
        }
    }
}

#endif /* XMB_PS2_ATLAS_H */
