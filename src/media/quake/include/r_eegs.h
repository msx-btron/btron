/*
 * r_eegs.h — Quake draw path for the PS2 GS (EEGS), with no TinyGL immediate mode.
 *
 * World, HUD and alias geometry are transformed on VU0 and submitted as
 * eegs_triangle() GIF.  TinyGL is used only at texture upload (glTexImage2D
 * registers a GS plane).  TinyGL backends keep the existing glBegin path.
 */
#ifndef QUAKE_R_EEGS_H
#define QUAKE_R_EEGS_H

#include "quakedef.h"

int  R_EEGS_Live(void);
void R_EEGS_BeginFrame(int width, int height);
void R_EEGS_SetCamera(const float org[3], const float ang[3], int width, int height);
void R_EEGS_Poly(const float *xyz, const float *st, int nverts,
                 uint32_t rgba, int gl_tex_idx);
void R_EEGS_Fill(int x, int y, int w, int h, uint32_t color);
void R_EEGS_String(int x, int y, const char *str, const uint8_t font[96][8]);
void R_EEGS_Tris(const float *xyz, const float *st, int ntris,
                 uint32_t rgba, int gl_tex_idx);

#endif /* QUAKE_R_EEGS_H */
