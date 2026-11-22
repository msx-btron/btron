/*
 * desktop_res.h — Requested BTRON desktop resolutions.
 *
 * The names and pixel sizes begin from the list in doc/txt/PICO.txt.
 * Six FOMA panels that were missing from that list are included.
 * The tier is the publishing rule, not the banner in that file:
 *   DESKTOP    800x600 and larger. Desk, kiosk, vehicle cluster.
 *   HANDHELD   640x480, plus the FOMA panels that were not already listed.
 *              A communicator held in two hands. 240x320 is 320x240.
 *   POCKET     under a 320x320 square, and larger than BAND.
 *   BAND       fits in 128x160, and larger than CALC. Sub-pico.
 *   CALC       both sides under 100. Sub-pico.
 *
 * Selecting a BTRON_RES picks the tier. The framebuffer size does not.
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_DESKTOP_RES_H_
#define _BTRON_DESKTOP_RES_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTRON_DESK_DESKTOP  = 0, /* Window compositor. 800x600 and larger. */
    BTRON_DESK_HANDHELD = 1, /* Two-hand communicator. 640x480 and FOMA panels. */
    BTRON_DESK_POCKET   = 2, /* One-hand panel. Under 320x320. */
    BTRON_DESK_BAND     = 3, /* Strip. Fits in 128x160. Sub-pico. */
    BTRON_DESK_CALC     = 4  /* Key panel. Both sides under 100. Sub-pico. */
} BTRON_DESK;

typedef enum {
    /* DESKTOP — 800x600 and larger */
    BTRON_RES_VESA_1280X1024 = 0,
    BTRON_RES_WORK_1024X768,
    BTRON_RES_WS_800X600,
    /* HANDHELD — 640x480 and the FOMA panels missing from PICO.txt */
    BTRON_RES_SVGA_640X480,
    BTRON_RES_FWVGA_480X864,
    BTRON_RES_FWVGA_480X854,
    BTRON_RES_VGAPLUS_480X690,
    BTRON_RES_WQVGA_240X432,
    BTRON_RES_WQVGA_240X400,
    BTRON_RES_QVGAPLUS_240X345,
    /* POCKET — under 320x320 */
    BTRON_RES_VGA_320X240,
    BTRON_RES_HQVGA_240X160,
    BTRON_RES_S40_208X208,
    BTRON_RES_S60_176X208,
    BTRON_RES_QCIF_176X144,
    BTRON_RES_GB_160X144,
    /* BAND — fits in 128x160. Sub-pico. */
    BTRON_RES_S30_128X160,
    BTRON_RES_QQVGA_160X120,
    BTRON_RES_SQVGA_128X128,
    BTRON_RES_SQCIF_128X96,
    BTRON_RES_ARDUBOY_128X64,
    /* CALC — both sides under 100. Sub-pico. */
    BTRON_RES_N7110_96X60,
    BTRON_RES_N1100_96X64,
    BTRON_RES_N1280_96X68,
    BTRON_RES_QQCIF_88X72,
    BTRON_RES_N5110I_84X84,
    BTRON_RES_N3310_84X48,
    BTRON_RES_COUNT
} BTRON_RES;

typedef struct {
    int w;
    int h;
    BTRON_DESK desk;
    const char *name;
} BTRON_RES_SPEC;

static inline BTRON_RES_SPEC btron_res_spec(BTRON_RES id)
{
    static const BTRON_RES_SPEC table[BTRON_RES_COUNT] = {
        { 1280, 1024, BTRON_DESK_DESKTOP,  "1280 x 1024 VESA Desktop" },
        { 1024,  768, BTRON_DESK_DESKTOP,  "1024 x 768 Workspace Desktop" },
        {  800,  600, BTRON_DESK_DESKTOP,  "800 x 600 Workstation Desktop" },
        {  640,  480, BTRON_DESK_HANDHELD, "640 x 480 SVGA Desktop" },
        {  480,  864, BTRON_DESK_HANDHELD, "480 x 864 Full Wide VGA" },
        {  480,  854, BTRON_DESK_HANDHELD, "480 x 854 Full Wide VGA" },
        {  480,  690, BTRON_DESK_HANDHELD, "480 x 690 VGA+" },
        {  240,  432, BTRON_DESK_HANDHELD, "240 x 432 Wide QVGA" },
        {  240,  400, BTRON_DESK_HANDHELD, "240 x 400 Wide QVGA" },
        {  240,  345, BTRON_DESK_HANDHELD, "240 x 345 QVGA+" },
        {  320,  240, BTRON_DESK_POCKET,   "320 x 240 VGA Desktop" },
        {  240,  160, BTRON_DESK_POCKET,   "240 x 160 HQVGA" },
        {  208,  208, BTRON_DESK_POCKET,   "208 x 208 Series 40" },
        {  176,  208, BTRON_DESK_POCKET,   "176 x 208 Series 60" },
        {  176,  144, BTRON_DESK_POCKET,   "176 x 144 QCIF" },
        {  160,  144, BTRON_DESK_POCKET,   "160 x 144 Game Boy" },
        {  128,  160, BTRON_DESK_BAND,     "128 x 160 Series 30, Nokia 2630, 3500, 6650" },
        {  160,  120, BTRON_DESK_BAND,     "160 x 120 QQVGA" },
        {  128,  128, BTRON_DESK_BAND,     "128 x 128 SQVGA" },
        {  128,   96, BTRON_DESK_BAND,     "128 x 96 SQCIF" },
        {  128,   64, BTRON_DESK_BAND,     "128 x 64 Arduboy" },
        {   96,   60, BTRON_DESK_CALC,     "96 x 60 Nokia 7110" },
        {   96,   64, BTRON_DESK_CALC,     "96 x 64 TI-81, Nokia 1100, 3410, 3510i, 6310i" },
        {   96,   68, BTRON_DESK_CALC,     "96 x 68 Nokia 1280, 1600, 2600" },
        {   88,   72, BTRON_DESK_CALC,     "88 x 72 QQCIF" },
        {   84,   84, BTRON_DESK_CALC,     "84 x 84 Nokia 5110i" },
        {   84,   48, BTRON_DESK_CALC,     "84 x 48 Nokia 3310, 3330, 5110, 8210" }
    };
    BTRON_RES_SPEC none;
    none.w = 0;
    none.h = 0;
    none.desk = BTRON_DESK_DESKTOP;
    none.name = "";
    if ((unsigned)id >= (unsigned)BTRON_RES_COUNT) return none;
    return table[id];
}

/* The requested resolution selects the engine. */
static inline BTRON_DESK btron_desk_for_res(BTRON_RES id)
{
    return btron_res_spec(id).desk;
}

static inline const char *btron_desk_name(BTRON_DESK desk)
{
    if (desk == BTRON_DESK_DESKTOP) return "DESKTOP";
    if (desk == BTRON_DESK_HANDHELD) return "HANDHELD";
    if (desk == BTRON_DESK_POCKET) return "POCKET";
    if (desk == BTRON_DESK_BAND) return "BAND";
    if (desk == BTRON_DESK_CALC) return "CALC";
    return "";
}

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_DESKTOP_RES_H_ */
