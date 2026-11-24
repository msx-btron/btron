/*
 * B-System / BTRON3 Native TrueType Rasterizer (Clean-Room libschrift Port)
 *
 * Clean-room B-RT / NASA JPL Power-of-10 / MISRA C:2012 implementation of the
 * libschrift API with a Q16.16 fixed-point non-FPU rasterizer core.
 *
 * Credits & License:
 *   API design and algorithmic lineage from libschrift
 *   © 2019-2022 Thomas Oltmann and contributors (ISC License).
 *   B-System B-RT / NASA JPL MISRA C clean-room rewrite © 2026 Synrc Research Center.
 */

#ifndef BTRON_SCHRIFT_H
#define BTRON_SCHRIFT_H 1

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SFT_DOWNWARD_Y          0x01u

/* Static NASA JPL Rule 2/3 upper bounds (caller-owned scratch, zero heap) */
#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
#ifndef SFT_MAX_POINTS
#define SFT_MAX_POINTS          128u
#endif
#ifndef SFT_MAX_LINES
#define SFT_MAX_LINES           256u
#endif
#ifndef SFT_MAX_CONTOURS
#define SFT_MAX_CONTOURS        16u
#endif
#ifndef SFT_MAX_RASTER_CELLS
#define SFT_MAX_RASTER_CELLS    144u    /* up to 12x12 px on MCU (~1.1 KiB) */
#endif
#else
#ifndef SFT_MAX_POINTS
#define SFT_MAX_POINTS          1024u
#endif
#ifndef SFT_MAX_LINES
#define SFT_MAX_LINES           2048u
#endif
#ifndef SFT_MAX_CONTOURS
#define SFT_MAX_CONTOURS        128u
#endif
#ifndef SFT_MAX_RASTER_CELLS
#define SFT_MAX_RASTER_CELLS    16384u  /* up to 128x128 px on desktop */
#endif
#endif

typedef struct SFT          SFT;
typedef struct SFT_Font     SFT_Font;
typedef uint_least32_t      SFT_UChar;
typedef uint_fast32_t       SFT_Glyph;
typedef struct SFT_LMetrics SFT_LMetrics;
typedef struct SFT_GMetrics SFT_GMetrics;
typedef struct SFT_Kerning  SFT_Kerning;
typedef struct SFT_Image    SFT_Image;
typedef struct SFT_GMetricsQ16 SFT_GMetricsQ16;
typedef struct SFT_Scratch  SFT_Scratch;

struct SFT_Font
{
    const uint8_t *memory;
    uint32_t       size;
    uint16_t       unitsPerEm;
    int16_t        locaFormat;
    uint16_t       numLongHmtx;
    uint16_t       numGlyphs;
    uint32_t       cmapOff;
    uint16_t       cmapFormat;
    uint32_t       locaOff;
    uint32_t       glyfOff;
    uint32_t       hmtxOff;
    uint32_t       hheaOff;
    uint32_t       kernOff;
    uint8_t        inUse;
};

struct SFT
{
    SFT_Font *font;
    double    xScale;
    double    yScale;
    double    xOffset;
    double    yOffset;
    int       flags;
};

struct SFT_LMetrics
{
    double ascender;
    double descender;
    double lineGap;
};

struct SFT_GMetrics
{
    double advanceWidth;
    double leftSideBearing;
    int    yOffset;
    int    minWidth;
    int    minHeight;
};

struct SFT_GMetricsQ16
{
    int32_t advanceWidthQ16;
    int32_t leftSideBearingQ16;
    int32_t yOffset;
    int32_t minWidth;
    int32_t minHeight;
};

struct SFT_Kerning
{
    double xShift;
    double yShift;
};

struct SFT_Image
{
    void *pixels;
    int   width;
    int   height;
};

typedef struct SFT_PtQ8
{
    int32_t x;
    int32_t y;
} SFT_PtQ8;

typedef struct SFT_LineIdx
{
    uint16_t beg;
    uint16_t end;
} SFT_LineIdx;

typedef struct SFT_CellQ8
{
    int32_t cover;
    int32_t area;
} SFT_CellQ8;

/* Caller-owned static workspace: zero malloc/calloc/free in sft_render_q16() */
struct SFT_Scratch
{
    SFT_PtQ8    points[SFT_MAX_POINTS];
    SFT_LineIdx lines[SFT_MAX_LINES];
    uint16_t    contourEnds[SFT_MAX_CONTOURS];
    uint8_t     pointFlags[SFT_MAX_POINTS];
    SFT_CellQ8  cells[SFT_MAX_RASTER_CELLS];
    uint16_t    numPoints;
    uint16_t    numLines;
};

const char *sft_version(void);

/* Zero-heap B-System native API (non-FPU Q16.16 optimal) */
int sft_init_mem(SFT_Font *outFont, const void *mem, size_t size);
int sft_lookup_font(const SFT_Font *font, SFT_UChar codepoint, SFT_Glyph *glyph);
int sft_gmetrics_q16(const SFT_Font *font, SFT_Glyph glyph,
                     int32_t xScaleQ16, int32_t yScaleQ16, uint32_t flags,
                     SFT_GMetricsQ16 *metrics);
int sft_render_q16(const SFT_Font *font, SFT_Glyph glyph,
                   int32_t xScaleQ16, int32_t yScaleQ16,
                   int32_t xOffsetQ16, int32_t yOffsetQ16, uint32_t flags,
                   SFT_Image image, SFT_Scratch *scratch);

/* Standard libschrift-compatible API (backed by static font pool, zero heap) */
SFT_Font *sft_loadmem (const void *mem, size_t size);
SFT_Font *sft_loadfile(const char *filename);
void      sft_freefont(SFT_Font *font);

int sft_lmetrics(const SFT *sft, SFT_LMetrics *metrics);
int sft_lookup  (const SFT *sft, SFT_UChar codepoint, SFT_Glyph *glyph);
int sft_gmetrics(const SFT *sft, SFT_Glyph glyph, SFT_GMetrics *metrics);
int sft_kerning (const SFT *sft, SFT_Glyph leftGlyph, SFT_Glyph rightGlyph,
                 SFT_Kerning *kerning);
int sft_render  (const SFT *sft, SFT_Glyph glyph, SFT_Image image);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_SCHRIFT_H */
