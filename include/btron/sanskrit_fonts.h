#ifndef _BTRON_SANSKRIT_FONTS_H_
#define _BTRON_SANSKRIT_FONTS_H_

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sanskrit / Devanagari 16x16 Bitmap Font Table (Unicode U+0900..U+097F)
 * Extracted from GNU Unifont for BTRON / Clarity 16x16 raster resolution.
 */
const UB* get_sanskrit_glyph_bitmap(UW cp);
const UB* get_sanskrit_glyph_bitmap_ex(UW cp, H *w, H *h);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_SANSKRIT_FONTS_H_ */
