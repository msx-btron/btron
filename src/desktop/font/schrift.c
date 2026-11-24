/*
 * B-System / BTRON3 Native TrueType Rasterizer (Clean-Room libschrift Port)
 *
 * Clean-room B-RT / NASA JPL Power-of-10 / MISRA C:2012 implementation:
 *   - Zero POSIX / Win32 dependencies (freestanding & hosted B-RT compatible)
 *   - Zero dynamic heap allocation (NASA JPL Rule 3)
 *   - Zero recursion (NASA JPL Rule 1); explicit bounded stacks & loops (Rule 2)
 *   - Q16.16 / Q8 fixed-point outline, Bezier, and analytic coverage core
 *     (zero soft-float instructions on non-FPU ARMv6-M / RP2040 / m68k)
 *
 * Credits & License:
 *   API & algorithmic lineage from libschrift
 *   © 2019-2022 Thomas Oltmann and contributors (ISC License).
 *   B-System B-RT / NASA JPL MISRA C rewrite © 2026 Synrc Research Center.
 */

#include <btron/schrift.h>
#include <string.h>

#define SCHRIFT_VERSION          "0.10.2-btron"

#define SFT_MAGIC_TTF            0x00010000u
#define SFT_MAGIC_TRUE           0x74727565u

#define SFT_MAX_TABLES           64u
#define SFT_MAX_CMAP_SUBTABLES   32u
#define SFT_MAX_CMAP_GROUPS      4096u
#define SFT_MAX_COMPOUND_ITEMS   16u
#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
#define SFT_MAX_BEZIER_STACK     16u
#define SFT_MAX_BEZIER_DEPTH     4u
#else
#define SFT_MAX_BEZIER_STACK     32u
#define SFT_MAX_BEZIER_DEPTH     5u
#endif
#define SFT_STATIC_FONT_SLOTS    8u

#define SFT_PT_ON_CURVE          0x01u
#define SFT_PT_X_SHORT           0x02u
#define SFT_PT_Y_SHORT           0x04u
#define SFT_PT_REPEAT            0x08u
#define SFT_PT_X_SAME_OR_POS     0x10u
#define SFT_PT_Y_SAME_OR_POS     0x20u

#define SFT_COMP_ARG_16BIT       0x0001u
#define SFT_COMP_ARGS_XY         0x0002u
#define SFT_COMP_SCALE           0x0008u
#define SFT_COMP_MORE            0x0020u
#define SFT_COMP_XY_SCALE        0x0040u
#define SFT_COMP_2X2             0x0080u

#define SFT_SUB_SHIFT            8
#define SFT_SUB_ONE              256
#define SFT_SUB_MASK             255
#define SFT_FLAT_TOL_Q8          32   /* 0.125 px in Q8 */

typedef struct SFT_XformQ16
{
    int32_t m00;
    int32_t m01;
    int32_t m10;
    int32_t m11;
    int32_t tx; /* Q8 pixel offset */
    int32_t ty; /* Q8 pixel offset */
} SFT_XformQ16;

typedef struct SFT_BezierItem
{
    SFT_PtQ8 p0;
    SFT_PtQ8 p1;
    SFT_PtQ8 p2;
    uint8_t  depth;
} SFT_BezierItem;

static SFT_Font s_font_pool[SFT_STATIC_FONT_SLOTS];

#if !defined(PICO_REAL_HW) && !defined(MATRIX_PICO_PROFILE)
static SFT_Scratch s_compat_scratch;
#endif

/* ── Safe big-endian readers (bounds checked, overflow safe) ────────────── */

static int sft_safe_range(uint32_t total, uint32_t off, uint32_t len)
{
    if (off > total) {
        return 0;
    }
    if (len > (total - off)) {
        return 0;
    }
    return 1;
}

static uint16_t sft_u16(const uint8_t *m, uint32_t off)
{
    return (uint16_t)(((uint16_t)m[off] << 8) | (uint16_t)m[off + 1u]);
}

static int16_t sft_i16(const uint8_t *m, uint32_t off)
{
    return (int16_t)sft_u16(m, off);
}

static uint32_t sft_u32(const uint8_t *m, uint32_t off)
{
    return ((uint32_t)m[off] << 24)
         | ((uint32_t)m[off + 1u] << 16)
         | ((uint32_t)m[off + 2u] << 8)
         |  (uint32_t)m[off + 3u];
}

static int32_t sft_abs32(int32_t v)
{
    return (v < 0) ? -v : v;
}

static int32_t sft_floor_q8(int32_t v)
{
    return (v >= 0) ? (v >> SFT_SUB_SHIFT) : -(((-v) + SFT_SUB_MASK) >> SFT_SUB_SHIFT);
}

static int32_t sft_ceil_q8(int32_t v)
{
    return (v >= 0) ? ((v + SFT_SUB_MASK) >> SFT_SUB_SHIFT) : -((-v) >> SFT_SUB_SHIFT);
}

/* ── SFNT table lookup ──────────────────────────────────────────────────── */

static int sft_find_table(const uint8_t *mem, uint32_t size,
                          const char tag[4], uint32_t *outOff, uint32_t *outLen)
{
    uint16_t numTables;
    uint16_t i;

    if (!sft_safe_range(size, 0u, 12u)) {
        return -1;
    }
    numTables = sft_u16(mem, 4u);
    if (numTables > SFT_MAX_TABLES) {
        numTables = SFT_MAX_TABLES;
    }
    for (i = 0u; i < numTables; i++) {
        uint32_t rec = 12u + ((uint32_t)i * 16u);
        if (!sft_safe_range(size, rec, 16u)) {
            return -1;
        }
        if ((mem[rec]      == (uint8_t)tag[0]) &&
            (mem[rec + 1u] == (uint8_t)tag[1]) &&
            (mem[rec + 2u] == (uint8_t)tag[2]) &&
            (mem[rec + 3u] == (uint8_t)tag[3])) {
            uint32_t toff = sft_u32(mem, rec + 8u);
            uint32_t tlen = sft_u32(mem, rec + 12u);
            if (!sft_safe_range(size, toff, tlen)) {
                return -1;
            }
            *outOff = toff;
            if (outLen != NULL) {
                *outLen = tlen;
            }
            return 0;
        }
    }
    return -1;
}

static int sft_select_cmap(SFT_Font *font, uint32_t cmapBase)
{
    uint16_t nsub;
    uint16_t i;
    uint32_t bestOff = 0u;
    uint16_t bestFmt = 0u;
    uint8_t  bestPrio = 0u;

    if (!sft_safe_range(font->size, cmapBase, 4u)) {
        return -1;
    }
    nsub = sft_u16(font->memory, cmapBase + 2u);
    if (nsub > SFT_MAX_CMAP_SUBTABLES) {
        nsub = SFT_MAX_CMAP_SUBTABLES;
    }
    for (i = 0u; i < nsub; i++) {
        uint32_t rec = cmapBase + 4u + ((uint32_t)i * 8u);
        uint16_t pid;
        uint16_t eid;
        uint32_t subOff;
        uint16_t fmt;
        uint8_t  prio = 0u;

        if (!sft_safe_range(font->size, rec, 8u)) {
            break;
        }
        pid = sft_u16(font->memory, rec);
        eid = sft_u16(font->memory, rec + 2u);
        subOff = cmapBase + sft_u32(font->memory, rec + 4u);
        if (!sft_safe_range(font->size, subOff, 2u)) {
            continue;
        }
        fmt = sft_u16(font->memory, subOff);
        if ((pid == 3u && eid == 10u) || (pid == 0u && eid == 4u)) {
            if (fmt == 12u) { prio = 4u; }
        } else if ((pid == 3u && eid == 1u) || (pid == 0u && eid == 3u)) {
            if (fmt == 4u) { prio = 3u; }
        } else if (pid == 0u) {
            if (fmt == 4u) { prio = 2u; }
            else if (fmt == 12u) { prio = 2u; }
        } else if (pid == 3u && eid == 0u && fmt == 4u) {
            prio = 1u;
        }
        if (prio > bestPrio) {
            bestPrio = prio;
            bestOff = subOff;
            bestFmt = fmt;
        }
    }
    if (bestPrio == 0u) {
        return -1;
    }
    font->cmapOff = bestOff;
    font->cmapFormat = bestFmt;
    return 0;
}

/* ── Font initialization ────────────────────────────────────────────────── */

const char *sft_version(void)
{
    return SCHRIFT_VERSION;
}

int sft_init_mem(SFT_Font *outFont, const void *mem, size_t size)
{
    const uint8_t *bytes;
    uint32_t magic;
    uint32_t headOff = 0u;
    uint32_t maxpOff = 0u;
    uint32_t cmapBase = 0u;
    uint32_t sz32;

    if ((outFont == NULL) || (mem == NULL) || (size < 12u) || (size > 0x7FFFFFFFu)) {
        return -1;
    }
    memset(outFont, 0, sizeof(*outFont));
    bytes = (const uint8_t *)mem;
    sz32 = (uint32_t)size;

    magic = sft_u32(bytes, 0u);
    if ((magic != SFT_MAGIC_TTF) && (magic != SFT_MAGIC_TRUE)) {
        return -1;
    }

    outFont->memory = bytes;
    outFont->size = sz32;

    if (sft_find_table(bytes, sz32, "head", &headOff, NULL) < 0) { return -1; }
    if (sft_find_table(bytes, sz32, "maxp", &maxpOff, NULL) < 0) { return -1; }
    if (sft_find_table(bytes, sz32, "hhea", &outFont->hheaOff, NULL) < 0) { return -1; }
    if (sft_find_table(bytes, sz32, "hmtx", &outFont->hmtxOff, NULL) < 0) { return -1; }
    if (sft_find_table(bytes, sz32, "loca", &outFont->locaOff, NULL) < 0) { return -1; }
    if (sft_find_table(bytes, sz32, "glyf", &outFont->glyfOff, NULL) < 0) { return -1; }
    if (sft_find_table(bytes, sz32, "cmap", &cmapBase, NULL) < 0) { return -1; }
    (void)sft_find_table(bytes, sz32, "kern", &outFont->kernOff, NULL);

    if (!sft_safe_range(sz32, headOff, 54u) ||
        !sft_safe_range(sz32, maxpOff, 6u)  ||
        !sft_safe_range(sz32, outFont->hheaOff, 36u)) {
        return -1;
    }

    outFont->unitsPerEm  = sft_u16(bytes, headOff + 18u);
    outFont->locaFormat  = sft_i16(bytes, headOff + 50u);
    outFont->numGlyphs   = sft_u16(bytes, maxpOff + 4u);
    outFont->numLongHmtx = sft_u16(bytes, outFont->hheaOff + 34u);

    if ((outFont->unitsPerEm == 0u) ||
        ((outFont->locaFormat != 0) && (outFont->locaFormat != 1)) ||
        (outFont->numLongHmtx == 0u)) {
        return -1;
    }
    if (sft_select_cmap(outFont, cmapBase) < 0) {
        return -1;
    }
    outFont->inUse = 1u;
    return 0;
}

SFT_Font *sft_loadmem(const void *mem, size_t size)
{
    uint32_t i;
    for (i = 0u; i < SFT_STATIC_FONT_SLOTS; i++) {
        if (s_font_pool[i].inUse == 0u) {
            if (sft_init_mem(&s_font_pool[i], mem, size) == 0) {
                return &s_font_pool[i];
            }
            return NULL;
        }
    }
    return NULL;
}

SFT_Font *sft_loadfile(const char *filename)
{
    /* Freestanding B-RT builds use sft_init_mem / sft_loadmem directly. */
    (void)filename;
    return NULL;
}

void sft_freefont(SFT_Font *font)
{
    if (font != NULL) {
        font->inUse = 0u;
        font->memory = NULL;
        font->size = 0u;
    }
}

/* ── Codepoint lookup (cmap Format 4 & Format 12) ───────────────────────── */

static int sft_cmap_fmt4(const SFT_Font *font, uint32_t cp, SFT_Glyph *outGlyph)
{
    uint32_t base = font->cmapOff;
    uint16_t segX2;
    uint16_t segCount;
    uint32_t endOff;
    uint32_t startOff;
    uint32_t deltaOff;
    uint32_t rangeOff;
    uint16_t lo;
    uint16_t hi;
    uint16_t step;

    if (cp > 0xFFFFu) {
        *outGlyph = 0u;
        return 0;
    }
    if (!sft_safe_range(font->size, base, 14u)) {
        return -1;
    }
    segX2 = sft_u16(font->memory, base + 6u);
    segCount = (uint16_t)(segX2 >> 1);
    if ((segCount == 0u) || ((segX2 & 1u) != 0u)) {
        return -1;
    }
    endOff   = base + 14u;
    startOff = endOff + segX2 + 2u;
    deltaOff = startOff + segX2;
    rangeOff = deltaOff + segX2;
    if (!sft_safe_range(font->size, rangeOff, segX2)) {
        return -1;
    }

    lo = 0u;
    hi = (uint16_t)(segCount - 1u);
    for (step = 0u; step < 16u; step++) {
        uint16_t mid = (uint16_t)((lo + hi) >> 1);
        uint16_t endCode = sft_u16(font->memory, endOff + ((uint32_t)mid * 2u));
        if ((uint16_t)cp <= endCode) {
            if (mid == lo) { break; }
            hi = mid;
        } else {
            if (lo == hi) { break; }
            lo = (uint16_t)(mid + 1u);
        }
    }
    {
        uint32_t idx2 = (uint32_t)hi * 2u;
        uint16_t endCode   = sft_u16(font->memory, endOff + idx2);
        uint16_t startCode = sft_u16(font->memory, startOff + idx2);
        if ((uint16_t)cp < startCode || (uint16_t)cp > endCode) {
            *outGlyph = 0u;
            return 0;
        }
        {
            int16_t  idDelta = sft_i16(font->memory, deltaOff + idx2);
            uint16_t idRange = sft_u16(font->memory, rangeOff + idx2);
            if (idRange == 0u) {
                *outGlyph = (SFT_Glyph)((uint16_t)((int32_t)cp + (int32_t)idDelta));
                return 0;
            }
            {
                uint32_t gOff = (rangeOff + idx2) + (uint32_t)idRange
                              + ((cp - (uint32_t)startCode) * 2u);
                uint16_t gid;
                if (!sft_safe_range(font->size, gOff, 2u)) {
                    return -1;
                }
                gid = sft_u16(font->memory, gOff);
                if (gid != 0u) {
                    gid = (uint16_t)((int32_t)gid + (int32_t)idDelta);
                }
                *outGlyph = (SFT_Glyph)gid;
                return 0;
            }
        }
    }
}

static int sft_cmap_fmt12(const SFT_Font *font, uint32_t cp, SFT_Glyph *outGlyph)
{
    uint32_t base = font->cmapOff;
    uint32_t nGroups;
    uint32_t lo;
    uint32_t hi;
    uint32_t step;

    if (!sft_safe_range(font->size, base, 16u)) {
        return -1;
    }
    nGroups = sft_u32(font->memory, base + 12u);
    if (nGroups == 0u) {
        *outGlyph = 0u;
        return 0;
    }
    if (nGroups > SFT_MAX_CMAP_GROUPS) {
        nGroups = SFT_MAX_CMAP_GROUPS;
    }
    if (!sft_safe_range(font->size, base + 16u, nGroups * 12u)) {
        return -1;
    }
    lo = 0u;
    hi = nGroups - 1u;
    for (step = 0u; step < 16u && lo <= hi; step++) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        uint32_t rec = base + 16u + (mid * 12u);
        uint32_t sCode = sft_u32(font->memory, rec);
        uint32_t eCode = sft_u32(font->memory, rec + 4u);
        if (cp < sCode) {
            if (mid == 0u) { break; }
            hi = mid - 1u;
        } else if (cp > eCode) {
            lo = mid + 1u;
        } else {
            uint32_t startGid = sft_u32(font->memory, rec + 8u);
            *outGlyph = (SFT_Glyph)(startGid + (cp - sCode));
            return 0;
        }
    }
    *outGlyph = 0u;
    return 0;
}

int sft_lookup_font(const SFT_Font *font, SFT_UChar codepoint, SFT_Glyph *glyph)
{
    if ((font == NULL) || (font->memory == NULL) || (glyph == NULL)) {
        return -1;
    }
    if (font->cmapFormat == 4u) {
        return sft_cmap_fmt4(font, (uint32_t)codepoint, glyph);
    }
    if (font->cmapFormat == 12u) {
        return sft_cmap_fmt12(font, (uint32_t)codepoint, glyph);
    }
    return -1;
}

int sft_lookup(const SFT *sft, SFT_UChar codepoint, SFT_Glyph *glyph)
{
    if (sft == NULL) {
        return -1;
    }
    return sft_lookup_font(sft->font, codepoint, glyph);
}

/* ── Glyph outline offset & metrics (Q16.16 core) ───────────────────────── */

static int sft_glyph_outline_range(const SFT_Font *font, SFT_Glyph glyph,
                                   uint32_t *outOff, uint32_t *outLen)
{
    uint32_t g = (uint32_t)glyph;
    uint32_t off0;
    uint32_t off1;

    if (g >= (uint32_t)font->numGlyphs) {
        return -1;
    }
    if (font->locaFormat == 0) {
        uint32_t p = font->locaOff + (g * 2u);
        if (!sft_safe_range(font->size, p, 4u)) { return -1; }
        off0 = (uint32_t)sft_u16(font->memory, p) * 2u;
        off1 = (uint32_t)sft_u16(font->memory, p + 2u) * 2u;
    } else {
        uint32_t p = font->locaOff + (g * 4u);
        if (!sft_safe_range(font->size, p, 8u)) { return -1; }
        off0 = sft_u32(font->memory, p);
        off1 = sft_u32(font->memory, p + 4u);
    }
    if (off1 < off0) {
        return -1;
    }
    *outLen = off1 - off0;
    if (*outLen == 0u) {
        *outOff = 0u;
        return 0;
    }
    *outOff = font->glyfOff + off0;
    if (!sft_safe_range(font->size, *outOff, *outLen)) {
        return -1;
    }
    return 0;
}

static int sft_hor_metrics(const SFT_Font *font, SFT_Glyph glyph,
                           uint16_t *outAdv, int16_t *outLsb)
{
    uint32_t g = (uint32_t)glyph;
    uint32_t nLong = (uint32_t)font->numLongHmtx;

    if (g < nLong) {
        uint32_t p = font->hmtxOff + (g * 4u);
        if (!sft_safe_range(font->size, p, 4u)) { return -1; }
        *outAdv = sft_u16(font->memory, p);
        *outLsb = sft_i16(font->memory, p + 2u);
    } else {
        uint32_t lastAdv = font->hmtxOff + ((nLong - 1u) * 4u);
        uint32_t lsbOff  = font->hmtxOff + (nLong * 4u) + ((g - nLong) * 2u);
        if (!sft_safe_range(font->size, lastAdv, 2u) ||
            !sft_safe_range(font->size, lsbOff, 2u)) {
            return -1;
        }
        *outAdv = sft_u16(font->memory, lastAdv);
        *outLsb = sft_i16(font->memory, lsbOff);
    }
    return 0;
}

static int32_t sft_scale_fu_to_q8(int32_t fu, int32_t scaleQ16, uint16_t upem)
{
    int64_t num = (int64_t)fu * (int64_t)scaleQ16;
    int64_t den = (int64_t)upem << 8; /* Q16 -> Q8 */
    return (int32_t)(num / den);
}

int sft_gmetrics_q16(const SFT_Font *font, SFT_Glyph glyph,
                     int32_t xScaleQ16, int32_t yScaleQ16, uint32_t flags,
                     SFT_GMetricsQ16 *metrics)
{
    uint16_t adv = 0u;
    int16_t  lsb = 0;
    uint32_t gOff = 0u;
    uint32_t gLen = 0u;

    if ((font == NULL) || (font->memory == NULL) || (metrics == NULL)) {
        return -1;
    }
    memset(metrics, 0, sizeof(*metrics));
    if (sft_hor_metrics(font, glyph, &adv, &lsb) < 0) {
        return -1;
    }
    metrics->advanceWidthQ16    = (int32_t)(((int64_t)adv * (int64_t)xScaleQ16) / (int64_t)font->unitsPerEm);
    metrics->leftSideBearingQ16 = (int32_t)(((int64_t)lsb * (int64_t)xScaleQ16) / (int64_t)font->unitsPerEm);

    if (sft_glyph_outline_range(font, glyph, &gOff, &gLen) < 0) {
        return -1;
    }
    if (gLen < 10u) {
        return 0; /* empty glyph (e.g. space) */
    }
    {
        int16_t xMin = sft_i16(font->memory, gOff + 2u);
        int16_t yMin = sft_i16(font->memory, gOff + 4u);
        int16_t xMax = sft_i16(font->memory, gOff + 6u);
        int16_t yMax = sft_i16(font->memory, gOff + 8u);
        int32_t bx0  = sft_floor_q8(sft_scale_fu_to_q8(xMin, xScaleQ16, font->unitsPerEm));
        int32_t bx1  = sft_ceil_q8 (sft_scale_fu_to_q8(xMax, xScaleQ16, font->unitsPerEm));
        int32_t by0;
        int32_t by1;

        if ((flags & SFT_DOWNWARD_Y) != 0u) {
            by0 = sft_floor_q8(sft_scale_fu_to_q8(-yMax, yScaleQ16, font->unitsPerEm));
            by1 = sft_ceil_q8 (sft_scale_fu_to_q8(-yMin, yScaleQ16, font->unitsPerEm));
        } else {
            by0 = sft_floor_q8(sft_scale_fu_to_q8(yMin, yScaleQ16, font->unitsPerEm));
            by1 = sft_ceil_q8 (sft_scale_fu_to_q8(yMax, yScaleQ16, font->unitsPerEm));
        }
        metrics->yOffset   = by0;
        metrics->minWidth  = (bx1 > bx0) ? (bx1 - bx0 + 1) : 0;
        metrics->minHeight = (by1 > by0) ? (by1 - by0 + 1) : 0;
    }
    return 0;
}

/* ── Outline decoding & iterative Bezier tessellation (Q8) ──────────────── */

static int sft_add_point(SFT_Scratch *sc, SFT_PtQ8 p, uint16_t *outIdx)
{
    if (sc->numPoints >= SFT_MAX_POINTS) {
        return -1;
    }
    *outIdx = sc->numPoints;
    sc->points[sc->numPoints] = p;
    sc->numPoints++;
    return 0;
}

static int sft_add_line(SFT_Scratch *sc, uint16_t beg, uint16_t end)
{
    if (sc->numLines >= SFT_MAX_LINES) {
        return -1;
    }
    sc->lines[sc->numLines].beg = beg;
    sc->lines[sc->numLines].end = end;
    sc->numLines++;
    return 0;
}

static int sft_tessellate_quad(SFT_Scratch *sc, SFT_PtQ8 p0, SFT_PtQ8 p1, SFT_PtQ8 p2)
{
    SFT_BezierItem stack[SFT_MAX_BEZIER_STACK];
    uint32_t sp = 0u;
    uint32_t iter;

    stack[sp].p0 = p0;
    stack[sp].p1 = p1;
    stack[sp].p2 = p2;
    stack[sp].depth = 0u;
    sp++;

    for (iter = 0u; iter < 128u && sp > 0u; iter++) {
        SFT_BezierItem cur;
        int32_t dx;
        int32_t dy;

        sp--;
        cur = stack[sp];
        dx = sft_abs32(cur.p0.x + cur.p2.x - (2 * cur.p1.x));
        dy = sft_abs32(cur.p0.y + cur.p2.y - (2 * cur.p1.y));

        if ((dx <= SFT_FLAT_TOL_Q8 && dy <= SFT_FLAT_TOL_Q8) ||
            (cur.depth >= SFT_MAX_BEZIER_DEPTH) ||
            ((sp + 2u) > SFT_MAX_BEZIER_STACK)) {
            uint16_t i0 = 0u;
            uint16_t i1 = 0u;
            if (sft_add_point(sc, cur.p0, &i0) < 0) { return -1; }
            if (sft_add_point(sc, cur.p2, &i1) < 0) { return -1; }
            if (sft_add_line(sc, i0, i1) < 0)       { return -1; }
        } else {
            SFT_PtQ8 a1;
            SFT_PtQ8 b1;
            SFT_PtQ8 mid;
            a1.x  = (cur.p0.x + cur.p1.x) >> 1;
            a1.y  = (cur.p0.y + cur.p1.y) >> 1;
            b1.x  = (cur.p1.x + cur.p2.x) >> 1;
            b1.y  = (cur.p1.y + cur.p2.y) >> 1;
            mid.x = (a1.x + b1.x) >> 1;
            mid.y = (a1.y + b1.y) >> 1;

            stack[sp].p0 = mid;
            stack[sp].p1 = b1;
            stack[sp].p2 = cur.p2;
            stack[sp].depth = (uint8_t)(cur.depth + 1u);
            sp++;

            stack[sp].p0 = cur.p0;
            stack[sp].p1 = a1;
            stack[sp].p2 = mid;
            stack[sp].depth = (uint8_t)(cur.depth + 1u);
            sp++;
        }
    }
    return 0;
}

static SFT_PtQ8 sft_xform_point(int32_t fx, int32_t fy, const SFT_XformQ16 *xf)
{
    SFT_PtQ8 p;
    int64_t xq24 = ((int64_t)fx * (int64_t)xf->m00) + ((int64_t)fy * (int64_t)xf->m01);
    int64_t yq24 = ((int64_t)fx * (int64_t)xf->m10) + ((int64_t)fy * (int64_t)xf->m11);
    p.x = (int32_t)(xq24 >> 16) + xf->tx;
    p.y = (int32_t)(yq24 >> 16) + xf->ty;
    return p;
}

static int sft_decode_simple_glyph(const SFT_Font *font, uint32_t gOff, uint32_t gLen,
                                   uint16_t numContours, const SFT_XformQ16 *xf,
                                   SFT_Scratch *sc)
{
    uint32_t pos = gOff + 10u;
    uint32_t endBound = gOff + gLen;
    uint16_t c;
    uint16_t numPts;
    uint16_t instrLen;
    uint16_t p;
    uint16_t basePt = sc->numPoints;
    int32_t  accX = 0;
    int32_t  accY = 0;

    if ((numContours == 0u) || (numContours > SFT_MAX_CONTOURS)) {
        return (numContours == 0u) ? 0 : -1;
    }
    if (!sft_safe_range(endBound, pos, ((uint32_t)numContours * 2u) + 2u)) {
        return -1;
    }
    for (c = 0u; c < numContours; c++) {
        sc->contourEnds[c] = sft_u16(font->memory, pos);
        pos += 2u;
    }
    numPts = (uint16_t)(sc->contourEnds[numContours - 1u] + 1u);
    if ((numPts == 0u) || (((uint32_t)basePt + (uint32_t)numPts) > SFT_MAX_POINTS)) {
        return -1;
    }

    instrLen = sft_u16(font->memory, pos);
    pos += 2u;
    if (!sft_safe_range(endBound, pos, (uint32_t)instrLen)) {
        return -1;
    }
    pos += (uint32_t)instrLen;

    /* Read flags (with RLE repeat) */
    p = 0u;
    while (p < numPts) {
        uint8_t fl;
        if (pos >= endBound) { return -1; }
        fl = font->memory[pos++];
        sc->pointFlags[p++] = fl;
        if ((fl & SFT_PT_REPEAT) != 0u) {
            uint8_t rep;
            uint8_t r;
            if (pos >= endBound) { return -1; }
            rep = font->memory[pos++];
            for (r = 0u; r < rep && p < numPts; r++) {
                sc->pointFlags[p++] = fl;
            }
        }
    }

    /* Read X deltas */
    for (p = 0u; p < numPts; p++) {
        uint8_t fl = sc->pointFlags[p];
        if ((fl & SFT_PT_X_SHORT) != 0u) {
            int32_t dx;
            if (pos >= endBound) { return -1; }
            dx = (int32_t)font->memory[pos++];
            accX += ((fl & SFT_PT_X_SAME_OR_POS) != 0u) ? dx : -dx;
        } else if ((fl & SFT_PT_X_SAME_OR_POS) == 0u) {
            if (!sft_safe_range(endBound, pos, 2u)) { return -1; }
            accX += (int32_t)sft_i16(font->memory, pos);
            pos += 2u;
        }
        sc->points[basePt + p].x = accX;
    }

    /* Read Y deltas & transform to Q8 pixel space */
    for (p = 0u; p < numPts; p++) {
        uint8_t fl = sc->pointFlags[p];
        if ((fl & SFT_PT_Y_SHORT) != 0u) {
            int32_t dy;
            if (pos >= endBound) { return -1; }
            dy = (int32_t)font->memory[pos++];
            accY += ((fl & SFT_PT_Y_SAME_OR_POS) != 0u) ? dy : -dy;
        } else if ((fl & SFT_PT_Y_SAME_OR_POS) == 0u) {
            if (!sft_safe_range(endBound, pos, 2u)) { return -1; }
            accY += (int32_t)sft_i16(font->memory, pos);
            pos += 2u;
        }
        sc->points[basePt + p] = sft_xform_point(sc->points[basePt + p].x, accY, xf);
    }
    sc->numPoints = (uint16_t)(basePt + numPts);

    /* Emit contour lines & tessellated quadratic curves */
    {
        uint16_t cStart = 0u;
        for (c = 0u; c < numContours; c++) {
            uint16_t cEnd = sc->contourEnds[c];
            uint16_t cnt;
            uint16_t i;
            SFT_PtQ8 curOn;
            SFT_PtQ8 ctrl;
            uint8_t  hasCtrl = 0u;
            uint16_t firstIdx = 0u;

            if ((cEnd < cStart) || (cEnd >= numPts)) { return -1; }
            cnt = (uint16_t)(cEnd - cStart + 1u);

            if ((sc->pointFlags[cStart] & SFT_PT_ON_CURVE) != 0u) {
                curOn = sc->points[basePt + cStart];
                firstIdx = (uint16_t)(basePt + cStart);
            } else if ((sc->pointFlags[cEnd] & SFT_PT_ON_CURVE) != 0u) {
                curOn = sc->points[basePt + cEnd];
                firstIdx = (uint16_t)(basePt + cEnd);
            } else {
                SFT_PtQ8 pa = sc->points[basePt + cStart];
                SFT_PtQ8 pb = sc->points[basePt + cEnd];
                curOn.x = (pa.x + pb.x) >> 1;
                curOn.y = (pa.y + pb.y) >> 1;
                if (sft_add_point(sc, curOn, &firstIdx) < 0) { return -1; }
            }
            ctrl = curOn;

            for (i = 0u; i <= cnt; i++) {
                uint16_t local = (uint16_t)(cStart + ((i == cnt) ? 0u : i));
                SFT_PtQ8 pt = sc->points[basePt + local];
                uint8_t on = (uint8_t)(sc->pointFlags[local] & SFT_PT_ON_CURVE);

                if ((i == 0u) && ((sc->pointFlags[cStart] & SFT_PT_ON_CURVE) != 0u)) {
                    continue;
                }
                if ((i == cnt) && ((sc->pointFlags[cStart] & SFT_PT_ON_CURVE) == 0u)) {
                    if (hasCtrl != 0u) {
                        if (sft_tessellate_quad(sc, curOn, ctrl, sc->points[firstIdx]) < 0) { return -1; }
                    } else {
                        uint16_t i0 = 0u;
                        if (sft_add_point(sc, curOn, &i0) < 0) { return -1; }
                        if (sft_add_line(sc, i0, firstIdx) < 0) { return -1; }
                    }
                    break;
                }

                if (on != 0u) {
                    if (hasCtrl != 0u) {
                        if (sft_tessellate_quad(sc, curOn, ctrl, pt) < 0) { return -1; }
                        hasCtrl = 0u;
                    } else {
                        uint16_t i0 = 0u;
                        uint16_t i1 = 0u;
                        if (sft_add_point(sc, curOn, &i0) < 0) { return -1; }
                        if (sft_add_point(sc, pt, &i1) < 0)    { return -1; }
                        if (sft_add_line(sc, i0, i1) < 0)      { return -1; }
                    }
                    curOn = pt;
                } else {
                    if (hasCtrl != 0u) {
                        SFT_PtQ8 mid;
                        mid.x = (ctrl.x + pt.x) >> 1;
                        mid.y = (ctrl.y + pt.y) >> 1;
                        if (sft_tessellate_quad(sc, curOn, ctrl, mid) < 0) { return -1; }
                        curOn = mid;
                    }
                    ctrl = pt;
                    hasCtrl = 1u;
                }
            }
            cStart = (uint16_t)(cEnd + 1u);
        }
    }
    return 0;
}

static int sft_decode_glyph(const SFT_Font *font, SFT_Glyph glyph,
                            const SFT_XformQ16 *rootXf, SFT_Scratch *sc)
{
    uint32_t gOff = 0u;
    uint32_t gLen = 0u;
    int16_t  numContours;

    if (sft_glyph_outline_range(font, glyph, &gOff, &gLen) < 0) {
        return -1;
    }
    if (gLen < 10u) {
        return 0;
    }
    numContours = sft_i16(font->memory, gOff);
    if (numContours >= 0) {
        return sft_decode_simple_glyph(font, gOff, gLen, (uint16_t)numContours, rootXf, sc);
    }

    /* Iterative compound glyph decode (NASA JPL Rule 1: no recursion) */
    {
        uint32_t pos = gOff + 10u;
        uint32_t endBound = gOff + gLen;
        uint32_t item;

        for (item = 0u; item < SFT_MAX_COMPOUND_ITEMS; item++) {
            uint16_t flags;
            uint16_t subGlyph;
            int32_t  arg1 = 0;
            int32_t  arg2 = 0;
            int32_t  a = 16384; /* F2Dot14 1.0 */
            int32_t  b = 0;
            int32_t  c = 0;
            int32_t  d = 16384;
            SFT_XformQ16 subXf;
            uint32_t subOff = 0u;
            uint32_t subLen = 0u;

            if (!sft_safe_range(endBound, pos, 4u)) { return -1; }
            flags    = sft_u16(font->memory, pos);
            subGlyph = sft_u16(font->memory, pos + 2u);
            pos += 4u;

            if ((flags & SFT_COMP_ARG_16BIT) != 0u) {
                if (!sft_safe_range(endBound, pos, 4u)) { return -1; }
                arg1 = (int32_t)sft_i16(font->memory, pos);
                arg2 = (int32_t)sft_i16(font->memory, pos + 2u);
                pos += 4u;
            } else {
                if (!sft_safe_range(endBound, pos, 2u)) { return -1; }
                arg1 = (int32_t)(int8_t)font->memory[pos];
                arg2 = (int32_t)(int8_t)font->memory[pos + 1u];
                pos += 2u;
            }
            if ((flags & SFT_COMP_SCALE) != 0u) {
                if (!sft_safe_range(endBound, pos, 2u)) { return -1; }
                a = (int32_t)sft_i16(font->memory, pos);
                d = a;
                pos += 2u;
            } else if ((flags & SFT_COMP_XY_SCALE) != 0u) {
                if (!sft_safe_range(endBound, pos, 4u)) { return -1; }
                a = (int32_t)sft_i16(font->memory, pos);
                d = (int32_t)sft_i16(font->memory, pos + 2u);
                pos += 4u;
            } else if ((flags & SFT_COMP_2X2) != 0u) {
                if (!sft_safe_range(endBound, pos, 8u)) { return -1; }
                a = (int32_t)sft_i16(font->memory, pos);
                b = (int32_t)sft_i16(font->memory, pos + 2u);
                c = (int32_t)sft_i16(font->memory, pos + 4u);
                d = (int32_t)sft_i16(font->memory, pos + 6u);
                pos += 8u;
            }

            subXf.m00 = (int32_t)((((int64_t)rootXf->m00 * a) + ((int64_t)rootXf->m01 * b)) >> 14);
            subXf.m01 = (int32_t)((((int64_t)rootXf->m00 * c) + ((int64_t)rootXf->m01 * d)) >> 14);
            subXf.m10 = (int32_t)((((int64_t)rootXf->m10 * a) + ((int64_t)rootXf->m11 * b)) >> 14);
            subXf.m11 = (int32_t)((((int64_t)rootXf->m10 * c) + ((int64_t)rootXf->m11 * d)) >> 14);
            subXf.tx  = rootXf->tx;
            subXf.ty  = rootXf->ty;
            if ((flags & SFT_COMP_ARGS_XY) != 0u) {
                SFT_PtQ8 delta = sft_xform_point(arg1, arg2, rootXf);
                subXf.tx = delta.x;
                subXf.ty = delta.y;
            }

            if (sft_glyph_outline_range(font, (SFT_Glyph)subGlyph, &subOff, &subLen) == 0 && subLen >= 10u) {
                int16_t subC = sft_i16(font->memory, subOff);
                if (subC >= 0) {
                    if (sft_decode_simple_glyph(font, subOff, subLen, (uint16_t)subC, &subXf, sc) < 0) {
                        return -1;
                    }
                }
            }
            if ((flags & SFT_COMP_MORE) == 0u) {
                break;
            }
        }
    }
    return 0;
}

/* ── Exact analytic Q8 scanline coverage rasterizer (zero float) ────────── */

static void sft_accum_segment_row(SFT_CellQ8 *cells, int width, int height,
                                  int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t iy = sft_floor_q8((y0 < y1) ? y0 : y1);
    int32_t dy = y1 - y0;
    int32_t ix0;
    int32_t ix1;

    if ((dy == 0) || (iy < 0) || (iy >= height)) {
        return;
    }
    ix0 = sft_floor_q8(x0);
    ix1 = sft_floor_q8(x1);

    if (ix0 == ix1) {
        int32_t cx = (ix0 < 0) ? 0 : ix0;
        if (cx < width) {
            int32_t xm = ((x0 + x1) >> 1) - (cx << SFT_SUB_SHIFT);
            if (xm < 0) { xm = 0; }
            if (xm > SFT_SUB_ONE) { xm = SFT_SUB_ONE; }
            cells[(iy * width) + cx].cover += dy;
            cells[(iy * width) + cx].area  += dy * xm;
        }
        return;
    }

    /* Segment crosses multiple X pixel columns within one Y scanline */
    {
        int32_t step = (x1 > x0) ? 1 : -1;
        int32_t curX = x0;
        int32_t curY = y0;
        int32_t totalDx = x1 - x0;
        int32_t col = ix0;
        int32_t guard;

        for (guard = 0; guard < 256; guard++) {
            int32_t nextX;
            int32_t nextY;
            int32_t segDy;
            int32_t cx;

            if (col == ix1) {
                nextX = x1;
                nextY = y1;
            } else {
                nextX = (step > 0) ? ((col + 1) << SFT_SUB_SHIFT) : (col << SFT_SUB_SHIFT);
                nextY = y0 + (int32_t)(((int64_t)(nextX - x0) * (int64_t)dy) / (int64_t)totalDx);
            }
            segDy = nextY - curY;
            cx = (col < 0) ? 0 : col;
            if ((segDy != 0) && (cx < width)) {
                int32_t xm = ((curX + nextX) >> 1) - (cx << SFT_SUB_SHIFT);
                if (xm < 0) { xm = 0; }
                if (xm > SFT_SUB_ONE) { xm = SFT_SUB_ONE; }
                cells[(iy * width) + cx].cover += segDy;
                cells[(iy * width) + cx].area  += segDy * xm;
            }
            if (col == ix1) {
                break;
            }
            curX = nextX;
            curY = nextY;
            col += step;
        }
    }
}

static void sft_draw_line_q8(SFT_CellQ8 *cells, int width, int height,
                             SFT_PtQ8 p0, SFT_PtQ8 p1)
{
    int32_t dy = p1.y - p0.y;
    int32_t dx = p1.x - p0.x;
    int32_t iy0;
    int32_t iy1;
    int32_t step;
    int32_t curX = p0.x;
    int32_t curY = p0.y;
    int32_t row;
    int32_t guard;

    if (dy == 0) {
        return;
    }
    iy0 = sft_floor_q8(p0.y);
    iy1 = sft_floor_q8(p1.y);
    if (dy < 0 && ((p0.y & SFT_SUB_MASK) == 0)) { iy0--; }
    if (dy > 0 && ((p1.y & SFT_SUB_MASK) == 0)) { iy1--; }

    if (iy0 == iy1) {
        sft_accum_segment_row(cells, width, height, p0.x, p0.y, p1.x, p1.y);
        return;
    }

    step = (dy > 0) ? 1 : -1;
    row = iy0;
    for (guard = 0; guard < 256; guard++) {
        int32_t nextY;
        int32_t nextX;

        if (row == iy1) {
            nextY = p1.y;
            nextX = p1.x;
        } else {
            nextY = (step > 0) ? ((row + 1) << SFT_SUB_SHIFT) : (row << SFT_SUB_SHIFT);
            nextX = p0.x + (int32_t)(((int64_t)(nextY - p0.y) * (int64_t)dx) / (int64_t)dy);
        }
        sft_accum_segment_row(cells, width, height, curX, curY, nextX, nextY);
        if (row == iy1) {
            break;
        }
        curX = nextX;
        curY = nextY;
        row += step;
    }
}

static void sft_post_process_q8(const SFT_CellQ8 *cells, uint8_t *dst,
                                int width, int height)
{
    int y;
    for (y = 0; y < height; y++) {
        int32_t accum = 0;
        const SFT_CellQ8 *rowCells = &cells[y * width];
        uint8_t *rowOut = &dst[y * width];
        int x;
        for (x = 0; x < width; x++) {
            int32_t cov;
            accum += rowCells[x].cover;
            cov = ((accum << SFT_SUB_SHIFT) - rowCells[x].area) >> SFT_SUB_SHIFT;
            if (cov < 0)   { cov = -cov; }
            if (cov > 255) { cov = 255; }
            rowOut[x] = (uint8_t)cov;
        }
    }
}

int sft_render_q16(const SFT_Font *font, SFT_Glyph glyph,
                   int32_t xScaleQ16, int32_t yScaleQ16,
                   int32_t xOffsetQ16, int32_t yOffsetQ16, uint32_t flags,
                   SFT_Image image, SFT_Scratch *scratch)
{
    uint32_t gOff = 0u;
    uint32_t gLen = 0u;
    uint32_t cellCount;
    SFT_XformQ16 xf;
    int16_t xMin;
    int16_t yMin;
    int16_t yMax;
    int32_t bx0;
    int32_t by0;
    uint16_t i;

    if ((font == NULL) || (font->memory == NULL) || (scratch == NULL) ||
        (image.pixels == NULL) || (image.width <= 0) || (image.height <= 0)) {
        return -1;
    }
    cellCount = (uint32_t)image.width * (uint32_t)image.height;
    if (cellCount > SFT_MAX_RASTER_CELLS) {
        return -1;
    }
    memset(image.pixels, 0, (size_t)cellCount);

    if (sft_glyph_outline_range(font, glyph, &gOff, &gLen) < 0) {
        return -1;
    }
    if (gLen < 10u) {
        return 0; /* empty glyph */
    }

    xMin = sft_i16(font->memory, gOff + 2u);
    yMin = sft_i16(font->memory, gOff + 4u);
    yMax = sft_i16(font->memory, gOff + 8u);

    bx0 = sft_floor_q8(sft_scale_fu_to_q8(xMin, xScaleQ16, font->unitsPerEm));
    if ((flags & SFT_DOWNWARD_Y) != 0u) {
        by0 = sft_floor_q8(sft_scale_fu_to_q8(-yMax, yScaleQ16, font->unitsPerEm));
    } else {
        by0 = sft_floor_q8(sft_scale_fu_to_q8(yMin, yScaleQ16, font->unitsPerEm));
    }

    xf.m00 = (int32_t)(((int64_t)xScaleQ16 << 8) / (int64_t)font->unitsPerEm);
    xf.m01 = 0;
    xf.m10 = 0;
    xf.m11 = (int32_t)(((int64_t)yScaleQ16 << 8) / (int64_t)font->unitsPerEm);
    if ((flags & SFT_DOWNWARD_Y) != 0u) {
        xf.m11 = -xf.m11;
    }
    xf.tx = (xOffsetQ16 >> 8) - (bx0 << SFT_SUB_SHIFT);
    xf.ty = (yOffsetQ16 >> 8) - (by0 << SFT_SUB_SHIFT);

    scratch->numPoints = 0u;
    scratch->numLines  = 0u;
    if (sft_decode_glyph(font, glyph, &xf, scratch) < 0) {
        return -1;
    }

    memset(scratch->cells, 0, (size_t)cellCount * sizeof(SFT_CellQ8));
    for (i = 0u; i < scratch->numLines; i++) {
        uint16_t b = scratch->lines[i].beg;
        uint16_t e = scratch->lines[i].end;
        if ((b < scratch->numPoints) && (e < scratch->numPoints)) {
            sft_draw_line_q8(scratch->cells, image.width, image.height,
                             scratch->points[b], scratch->points[e]);
        }
    }
    sft_post_process_q8(scratch->cells, (uint8_t *)image.pixels, image.width, image.height);
    return 0;
}

/* ── Standard libschrift-compatible double wrappers ─────────────────────── */

int sft_lmetrics(const SFT *sft, SFT_LMetrics *metrics)
{
    double factor;
    if ((sft == NULL) || (sft->font == NULL) || (sft->font->memory == NULL) || (metrics == NULL)) {
        return -1;
    }
    memset(metrics, 0, sizeof(*metrics));
    factor = sft->yScale / (double)sft->font->unitsPerEm;
    metrics->ascender  = (double)sft_i16(sft->font->memory, sft->font->hheaOff + 4u) * factor;
    metrics->descender = (double)sft_i16(sft->font->memory, sft->font->hheaOff + 6u) * factor;
    metrics->lineGap   = (double)sft_i16(sft->font->memory, sft->font->hheaOff + 8u) * factor;
    return 0;
}

int sft_gmetrics(const SFT *sft, SFT_Glyph glyph, SFT_GMetrics *metrics)
{
    SFT_GMetricsQ16 mq;
    if ((sft == NULL) || (metrics == NULL)) {
        return -1;
    }
    if (sft_gmetrics_q16(sft->font, glyph,
                         (int32_t)(sft->xScale * 65536.0),
                         (int32_t)(sft->yScale * 65536.0),
                         (uint32_t)sft->flags, &mq) < 0) {
        return -1;
    }
    metrics->advanceWidth    = (double)mq.advanceWidthQ16 / 65536.0;
    metrics->leftSideBearing = (double)mq.leftSideBearingQ16 / 65536.0;
    metrics->yOffset         = (int)mq.yOffset;
    metrics->minWidth        = (int)mq.minWidth;
    metrics->minHeight       = (int)mq.minHeight;
    return 0;
}

int sft_kerning(const SFT *sft, SFT_Glyph leftGlyph, SFT_Glyph rightGlyph,
                SFT_Kerning *kerning)
{
    const SFT_Font *font;
    uint32_t base;
    uint16_t nTables;
    uint16_t nPairs;
    uint32_t key;
    uint32_t lo;
    uint32_t hi;
    uint32_t step;

    if ((sft == NULL) || (sft->font == NULL) || (kerning == NULL)) {
        return -1;
    }
    memset(kerning, 0, sizeof(*kerning));
    font = sft->font;
    if (font->kernOff == 0u) {
        return 0;
    }
    base = font->kernOff;
    if (!sft_safe_range(font->size, base, 4u)) {
        return 0;
    }
    nTables = sft_u16(font->memory, base + 2u);
    if ((nTables == 0u) || !sft_safe_range(font->size, base + 4u, 14u)) {
        return 0;
    }
    nPairs = sft_u16(font->memory, base + 10u);
    if ((nPairs == 0u) || !sft_safe_range(font->size, base + 18u, (uint32_t)nPairs * 6u)) {
        return 0;
    }
    key = (((uint32_t)leftGlyph & 0xFFFFu) << 16) | ((uint32_t)rightGlyph & 0xFFFFu);
    lo = 0u;
    hi = (uint32_t)nPairs - 1u;
    for (step = 0u; step < 16u && lo <= hi; step++) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        uint32_t rec = base + 18u + (mid * 6u);
        uint32_t cur = sft_u32(font->memory, rec);
        if (key < cur) {
            if (mid == 0u) { break; }
            hi = mid - 1u;
        } else if (key > cur) {
            lo = mid + 1u;
        } else {
            int16_t val = sft_i16(font->memory, rec + 4u);
            kerning->xShift = ((double)val * sft->xScale) / (double)font->unitsPerEm;
            return 0;
        }
    }
    return 0;
}

int sft_render(const SFT *sft, SFT_Glyph glyph, SFT_Image image)
{
#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
    (void)sft; (void)glyph; (void)image;
    return -1; /* MCU targets use sft_render_q16() with caller-owned scratch */
#else
    if (sft == NULL) {
        return -1;
    }
    return sft_render_q16(sft->font, glyph,
                          (int32_t)(sft->xScale * 65536.0),
                          (int32_t)(sft->yScale * 65536.0),
                          (int32_t)(sft->xOffset * 65536.0),
                          (int32_t)(sft->yOffset * 65536.0),
                          (uint32_t)sft->flags,
                          image, &s_compat_scratch);
#endif
}
