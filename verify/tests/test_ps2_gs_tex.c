/*
 * verify/tests/test_ps2_gs_tex.c -- a GL texture plane's VRAM address, against words written by hand
 *
 * Row 5, step 5b of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  The discipline is
 * increments 2, 3 and 4's: `ps2_gs_tex.c`, the packer and the VRAM cursor are compiled into this
 * program, and every expectation was derived from the register field tables before the code ran.
 *
 * What is under test is the arithmetic between two sides that each hold half the truth: TinyGL owns
 * bytes in RDRAM (an upload extent inside a plane rounded to a power of two per axis) and the GS
 * owns an address in its own 4 MB.  A plane that is planned one 256-byte block off draws a
 * neighbouring texture; a `TBW` derived from the upload instead of the padded plane skews every row
 * of the picture by the difference; and a span rounded to the wrong unit puts two planes in one
 * swizzle block.  None of that is visible on a screen as a wrong number, only as a wrong icon, so
 * it is gated here as words.
 *
 * The two planes are the real ones: the XMB's icon atlas (a 384x384 upload in a 512x512 allocation)
 * and its font sheet (128x96 in 128x128), so the byte counts and the 132 pages left at the end are
 * the budget the plan of record quotes, not a toy's.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/media/xmb/xmb_atlas.h"
#include "../../src/media/xmb/xmb_ps2_atlas.h"
#include "../../src/drivers/playstation2/ps2_gs_pack.c"
#include "../../src/drivers/playstation2/ps2_gs_vram.c"
#include "../../src/drivers/playstation2/ps2_gs_tex.c"

static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_failures++;
        printf("  [FAIL] %s\n", what);
    }
}

static void check_word(const char *what, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got != want) {
        s_failures++;
        printf("  [FAIL] %s got=0x%016llx want=0x%016llx\n", what,
               (unsigned long long)got, (unsigned long long)want);
    }
}

static void check_num(const char *what, unsigned long got, unsigned long want)
{
    s_checks++;
    if (got != want) {
        s_failures++;
        printf("  [FAIL] %s got=%lu want=%lu\n", what, got, want);
    }
}

/* The registry never reads the plane's bytes -- it keys on the pointer and the upload path in
 * ps2_gs.c streams them -- so a gate needs distinct, stable addresses and nothing more. */
static char s_planes[GS_TEX_MAX + 4][64];

static const void *plane_of(int i)
{
    return &s_planes[i][0];
}

/* Reading a slot through `gs_tex_at(i)->field` means that a law which fails to create the slot
 * crashes the run instead of reporting it, and a crashed gate prints nothing at all -- control D
 * below (the registry renamed to the reused index rather than kept as a high-water mark) is exactly
 * that failure, and it segfaulted before its first FAIL row.  These answer 0 for a slot that is not
 * there, so every case below fails as a number on a line rather than as a truncated log. */
static const void *plane_at(uint32_t i)
{
    const gs_tex_t *e = gs_tex_at(i);
    return e != 0 ? e->plane : 0;
}

static uint32_t byte_at(uint32_t i)
{
    const gs_tex_t *e = gs_tex_at(i);
    return e != 0 ? e->byte : 0u;
}

static uint32_t tbw_at(uint32_t i)
{
    const gs_tex_t *e = gs_tex_at(i);
    return e != 0 ? e->tbw : 0u;
}

static uint32_t pxw_at(uint32_t i)
{
    const gs_tex_t *e = gs_tex_at(i);
    return e != 0 ? e->px_w : 0u;
}

static uint32_t qwc_at(uint32_t i)
{
    const gs_tex_t *e = gs_tex_at(i);
    return e != 0 ? e->qwc : 0u;
}

static uint32_t pending_at(uint32_t i)
{
    const gs_tex_t *e = gs_tex_at(i);
    return e != 0 ? e->pending : 1u;   /* absent: claim it is pending, so the count row refuses */
}

/* The numbers the two real sheets produce, derived by hand and named once so every case below
 * quotes the same plan.
 *
 * The cursor starts above the canvas: 13 units of 64 px x 600 rows x 4 B = 1,996,800, rounded up to
 * whole 8 KB pages is 244 pages = 1,998,848.  An icon atlas of 512x512 CT32 texels is 1,048,576
 * bytes -- already a whole number of the 16 KB plane units -- so the font sheet at 128x128 =
 * 65,536 follows it at 3,047,424, and 4,194,304 - 3,112,960 = 1,081,344 bytes are left: 132 whole
 * 8 KB pages, which is the budget row 5 of the plan says the GS backend has to live inside. */
static const uint32_t k_atlas_byte  = 1998848u;
static const uint32_t k_atlas_tba   = 7808u;      /* 1,998,848 / 256 */
static const uint32_t k_font_byte   = 3047424u;
static const uint32_t k_font_tba    = 11904u;     /* 3,047,424 / 256 */
static const uint32_t k_left_after  = 1081344u;

/* TEX0: TBA[13:0] TBW[19:14] PSM[25:20] TW[29:26] TH[33:30] TCC[34], the rest zero.  Atlas:
 * 7,808 + (8<<14) + (0<<20 for CT32) + (9<<26) + (9<<30) + (1<<34) = 0x664021E80.  Font:
 * 11,904 + (2<<14) + (7<<26) + (7<<30) + (1<<34) = 0x5DC00AE80. */
static const uint64_t k_tex0_atlas = 0x664021E80ULL;
static const uint64_t k_tex0_font  = 0x5DC00AE80ULL;

/* TEX1: LCM[0] MXL[4:2] MMAG[5] MMIN[8:6] MTBA[9] L[20:19] K[43:32].  Both sheets are drawn with a
 * bilinear filter on both axes, which is what the XMB asks for (GL_TEXTURE_MIN_FILTER and
 * GL_TEXTURE_MAG_FILTER both GL_LINEAR) and what the POSIX reference (the virgl span loop) honours --
 * 1:1 texturing for HW OpenGL.  MMAG=1 is 1<<5 and MMIN=1 (plain bilinear minification, NOT a mipmap
 * read: the GS only reaches for a chain at MMIN>=2, which would set MTBA and read L) is 1<<6, and the
 * two together are 0x20 | 0x40 = 0x60, with MXL, MTBA, L and K all zero.  This sampler was A/B'd
 * against the operator's icon black-rectangles and is NOT their author (the settled page reads correct
 * white-on-blue under either filter); it is pinned LINEAR because that is the faithful 1:1 port. */
static const uint64_t k_tex1       = 0x60ULL;

/* CLAMP: WMS[1:0] WMT[3:2] MINU[13:4] MAXU[23:14] MINV[33:24] MAXV[43:34].
 * Hardware 2 is REGION_CLAMP.  A 384-in-512 atlas REGION_CLAMPs to texel 383
 * (0xA | (383<<14) | (383<<34) = 0x5FC005FC00A) so LINEAR at u=1 does not
 * read the transparent pad.  Font 128x96 in 128x128: S is POT (mode 1, 127),
 * T is REGION_CLAMP to 95 (95<<34 = 0x17C00000000). */
static const uint64_t k_clamp_atlas = 0x5FC005FC00AULL;
static const uint64_t k_clamp_font  = 0x17C001FC009ULL;

/* What the registry says about a plane, all of it checked. */
static void plan_words(const char *tag, const gs_tex_t *t, uint32_t byte, uint32_t tba,
                       uint32_t tbw, uint32_t tw_exp, uint32_t th_exp, uint32_t qwc,
                       uint64_t tex0, uint64_t clamp)
{
    char label[96];

    sprintf(label, "%s: VRAM base offset", tag);
    check_num(label, t->byte, byte);
    sprintf(label, "%s: TEX0.TBA in 256-byte blocks", tag);
    check_num(label, t->tba, tba);
    sprintf(label, "%s: TEX0.TBW in units of 64 texels", tag);
    check_num(label, t->tbw, tbw);
    sprintf(label, "%s: TEX0.TW is the padded width's exponent", tag);
    check_num(label, t->tw_exp, tw_exp);
    sprintf(label, "%s: TEX0.TH is the padded height's exponent", tag);
    check_num(label, t->th_exp, th_exp);
    sprintf(label, "%s: the Host->Local stream's quadword count", tag);
    check_num(label, t->qwc, qwc);
    sprintf(label, "%s: TEX0", tag);
    check_word(label, t->tex0, tex0);
    sprintf(label, "%s: TEX1", tag);
    check_word(label, t->tex1, k_tex1);
    sprintf(label, "%s: CLAMP", tag);
    check_word(label, t->clamp, clamp);
}

/* The two sheets the XMB binds, planned and word-for-word. */
static void test_two_sheets(void)
{
    const gs_tex_t *atlas, *font;

    gs_tex_init();
    check_num("a fresh registry holds nothing", gs_tex_count(), 0u);
    check_num("and the cursor starts above the canvas", gs_tex_vram_left(),
              4194304u - 1998848u);

    atlas = gs_tex_register(plane_of(0), 384u, 384u, 512u, 512u);
    check("the icon atlas is planned", atlas != 0);
    if (atlas == 0) return;
    plan_words("atlas", atlas, k_atlas_byte, k_atlas_tba, 8u, 9u, 9u, 65536u,
               k_tex0_atlas, k_clamp_atlas);

    /* The law the whole file rests on: the GS addresses the plane, not the upload.  384 is what
     * the app wrote; 512 is what TEX0 names, and the difference is padding this plane owns. */
    check_num("the upload extent is recorded as what was uploaded", atlas->px_w, 384u);
    check_num("but TW names the 512-texel plane", 1u << atlas->tw_exp, 512u);
    check("and the stream carries the padded plane, padding included",
          atlas->pad_w == 512u && atlas->qwc == 512u * 512u / 4u);

    font = gs_tex_register(plane_of(1), 128u, 96u, 128u, 128u);
    check("the font sheet is planned", font != 0);
    if (font == 0) return;
    plan_words("font", font, k_font_byte, k_font_tba, 2u, 7u, 7u, 4096u,
               k_tex0_font, k_clamp_font);

    check("two planes, two slots, in registration order",
          gs_tex_count() == 2u && gs_tex_at(0u) == atlas && gs_tex_at(1u) == font);
    check("and a slot past the end answers nothing",
          gs_tex_at(2u) == 0 && gs_tex_at(0xFFFFFFFFu) == 0);

    /* No shared byte, in either unit the registers use: the spans are whole 16 KB planes. */
    check_num("the second plane starts after the first plane's whole span",
              font->byte, k_atlas_byte + 1048576u);
    check_num("which leaves the 132 pages the plan of record budgets",
              gs_tex_vram_left(), k_left_after);

    check("a draw call's question -- have these bytes an address? -- answers by pointer",
          gs_tex_find(plane_of(0)) == atlas && gs_tex_find(plane_of(1)) == font);
    check("and a texture that was never registered answers 0 rather than somebody else's page",
          gs_tex_find(plane_of(2)) == 0 && gs_tex_find(0) == 0);

    /* With no sink installed -- the host gate -- the bytes have not crossed, which is what a
     * present path logs. */
    check_num("both planes wait for their bytes", gs_tex_pending(), 2u);
    check("and an entry's own flag says so", atlas->pending == 1 && font->pending == 1);
}

/* The refusals, each of which has to leave the cursor and the registry exactly as they were: a
 * half-registered plane is a draw from an address nobody claimed. */
static void test_refusals(void)
{
    uint32_t left;

    gs_tex_init();
    left = gs_tex_vram_left();

    check("a null plane is refused", gs_tex_register(0, 64u, 64u, 64u, 64u) == 0);
    check("so is an empty extent",
          gs_tex_register(plane_of(0), 0u, 64u, 64u, 64u) == 0 &&
          gs_tex_register(plane_of(0), 64u, 0u, 64u, 64u) == 0);
    /* TW/TH are exponents, so a padded axis that is not a power of two has no name in the
     * register at all -- and gl_image_alloc() already rounded it, so this is a caller that read
     * the wrong field of its own image. */
    check("a padded width that is not a power of two has no TEX0",
          gs_tex_register(plane_of(0), 100u, 128u, 100u, 128u) == 0);
    check("nor a padded height",
          gs_tex_register(plane_of(0), 128u, 100u, 128u, 100u) == 0);
    /* The transfer's RRW/RRH would read past the source and the clamp would name texels the app
     * never wrote. */
    check("an upload larger than its plane is refused",
          gs_tex_register(plane_of(0), 128u, 64u, 64u, 64u) == 0 &&
          gs_tex_register(plane_of(0), 64u, 128u, 64u, 64u) == 0);

    check_num("and none of them claimed VRAM", gs_tex_vram_left(), left);
    check_num("none of them took a slot", gs_tex_count(), 0u);

    /* An upload exactly the plane's size is the ordinary 1:1 case and must not be refused by the
     * extent check. */
    check("an upload that exactly fills its plane is planned",
          gs_tex_register(plane_of(0), 64u, 64u, 64u, 64u) != 0);
}

/* The sink: the plan is complete when the bus owner is told, and the telling happens while the
 * application is still inside the upload -- not at frame end, which is the order that would put a
 * primitive sampling a plane before the transfer that fills it. */
static uint32_t s_sink_calls, s_sink_last, s_sink_seen_byte, s_sink_seen_qwc;
static int s_sink_seen_pending;

static void sink_take(uint32_t index)
{
    const gs_tex_t *t = gs_tex_at(index);

    s_sink_calls++;
    s_sink_last = index;
    /* Read what the entry says *during* the call: 0 here would be a plan handed over empty. */
    if (t != 0) {
        s_sink_seen_byte = t->byte;
        s_sink_seen_qwc = t->qwc;
        s_sink_seen_pending = t->pending;
    } else {
        s_sink_seen_byte = 0u;
        s_sink_seen_qwc = 0u;
        s_sink_seen_pending = -1;
    }
}

static void test_sink_at_registration(void)
{
    const gs_tex_t *atlas, *again;

    gs_tex_init();
    s_sink_calls = 0u;
    gs_tex_install_sink(sink_take);

    atlas = gs_tex_register(plane_of(0), 384u, 384u, 512u, 512u);
    check_num("the bus owner is told during the registration, once", s_sink_calls, 1u);
    check_num("with the slot it can read back", s_sink_last, 0u);
    check_num("and a plan already complete: the VRAM address", s_sink_seen_byte, k_atlas_byte);
    check_num("the stream length", s_sink_seen_qwc, 65536u);
    check("the bytes are pending right then, because the sink has not said otherwise",
          s_sink_seen_pending == 1);
    check_num("so the registry still counts it pending", gs_tex_pending(), 1u);

    /* The sink that says the bytes crossed is the only thing that clears the flag; the registry
     * cannot know about a DMA it does not own. */
    gs_tex_uploaded(0u);
    check_num("and once it has, nothing is pending", gs_tex_pending(), 0u);
    gs_tex_uploaded(9u);
    check("a slot that does not exist is not an error, just nothing", gs_tex_at(9u) == 0);

    /* Same pointer, same geometry: gl_image_alloc()'s "same plane, new contents".  The address and
     * the words stand and the bytes cross again -- one slot, no second span. */
    again = gs_tex_register(plane_of(0), 384u, 384u, 512u, 512u);
    check("the re-upload is the same entry", again == atlas);
    check_num("handed over again", s_sink_calls, 2u);
    check_num("with the same slot", s_sink_last, 0u);
    check("the same plan, so the same address and the same length",
          again->byte == k_atlas_byte && again->qwc == 65536u && again->tex0 == k_tex0_atlas);
    check("and a smaller upload extent into the same plane keeps the plane",
          gs_tex_register(plane_of(0), 256u, 256u, 512u, 512u) == atlas &&
          atlas->px_w == 256u && atlas->byte == k_atlas_byte);
    check_num("still one slot", gs_tex_count(), 1u);
    check_num("and still the VRAM the first registration took", gs_tex_vram_left(),
              4194304u - 1998848u - 1048576u);

    /* An installed sink of 0 means no bus: both entries are still planned, and pending counts
     * them -- the re-upload that no sink handed over this time included, because the flag is the
     * sink's to clear and nobody has. */
    gs_tex_install_sink(0);
    s_sink_calls = 0u;
    check("clearing the sink still plans",
          gs_tex_register(plane_of(1), 128u, 96u, 128u, 128u) != 0);
    check_num("and tells nobody", s_sink_calls, 0u);
    check_num("so both the new plane and the un-handed re-upload are pending",
              gs_tex_pending(), 2u);
}

/* A pointer the C heap handed to a different texture: the identity matches, the geometry does not,
 * so the entry is re-planned and takes a fresh span.  Reusing the old span is the bug this case
 * exists for -- the abandoned plane's bytes would still be addressed by the new plane's TEX0. */
static void test_recycled_pointer(void)
{
    const gs_tex_t *first, *again;
    uint32_t old_byte, left;
    uint64_t old_tex0, old_clamp;

    gs_tex_init();
    first = gs_tex_register(plane_of(0), 128u, 128u, 128u, 128u);
    check("a 128x128 sheet is planned at the cursor", first != 0 && first->byte == k_atlas_byte);
    if (first == 0) return;
    /* Taken before the re-plan, because the re-plan writes through the same entry: `again` and
     * `first` are one object, so "the old span is gone" can only be said against a copy. */
    old_byte  = first->byte;
    old_tex0  = first->tex0;
    old_clamp = first->clamp;
    left = gs_tex_vram_left();
    check_num("and it claimed one 64 KiB span", left, 4194304u - 1998848u - 65536u);

    again = gs_tex_register(plane_of(0), 64u, 64u, 64u, 64u);
    check("the same pointer with different geometry is the same slot", again == first);
    check_num("re-planned into a fresh span, not the old one", again->byte, 1998848u + 65536u);
    check_num("whose TBA is that offset in 256-byte blocks", again->tba, 8064u);
    check("and whose words all moved with it",
          again->tbw == 1u && again->tw_exp == 6u && again->th_exp == 6u);
    check_word("TEX0 for a 64x64 plane at 2,064,384", again->tex0, 0x598005F80ULL);
    check_word("CLAMP over 64 texels on both axes", again->clamp, 0xFC000FC005ULL);
    check_num("the abandoned 64 KiB is spent, not hidden", gs_tex_vram_left(),
              4194304u - 2064384u - 16384u);
    check("and no word the entry hands out now names the abandoned span",
          again->byte != old_byte && again->tex0 != old_tex0 && again->clamp != old_clamp);
    check_num("still one slot", gs_tex_count(), 1u);

    /* A *third* plane lands after the re-plan, which is the proof the cursor moved rather than the
     * slot being patched in place. */
    check("a new plane starts where the re-plan ended",
          gs_tex_register(plane_of(1), 64u, 64u, 64u, 64u) != 0 &&
          byte_at(1u) == 2064384u + 16384u);
}

/* The cap and the end of VRAM, both of which have to refuse rather than draw from an address
 * nobody owns. */
static void test_cap_and_exhaustion(void)
{
    const gs_tex_t *a, *b, *c;
    uint32_t i, left;

    gs_tex_init();
    for (i = 0u; i < GS_TEX_MAX; i++)
        if (gs_tex_register(plane_of((int)i), 64u, 64u, 64u, 64u) == 0) break;
    check("the registry fills to its cap without complaint", i == GS_TEX_MAX);
    check_num("every slot is taken", gs_tex_count(), GS_TEX_MAX);
    left = gs_tex_vram_left();
    check("a plane past the cap is refused, not silently dropped into a slot in use",
          gs_tex_register(plane_of((int)GS_TEX_MAX), 64u, 64u, 64u, 64u) == 0);
    check_num("the cap did not move", gs_tex_count(), GS_TEX_MAX);
    check_num("nor the cursor", gs_tex_vram_left(), left);

    /* Now the real limit: 4 MB, from a cursor already 1,998,848 in.  Two 512x512 planes fit and a
     * third does not. */
    gs_tex_init();
    a = gs_tex_register(plane_of(0), 512u, 512u, 512u, 512u);
    b = gs_tex_register(plane_of(1), 512u, 512u, 512u, 512u);
    check("two icon atlases fit", a != 0 && b != 0);
    left = gs_tex_vram_left();
    check_num("and the room they leave is 96 KiB", left, 4194304u - 4096000u);
    c = gs_tex_register(plane_of(2), 512u, 512u, 512u, 512u);
    check("a third does not: it is refused whole", c == 0);
    check_num("with the cursor untouched", gs_tex_vram_left(), left);
    check_num("and no slot half-written", gs_tex_count(), 2u);

    /* A plane that does fit still lands. */
    check("a sheet that fits the remainder is planned",
          gs_tex_register(plane_of(3), 128u, 128u, 128u, 128u) != 0);
    check_num("leaving 32 KiB", gs_tex_vram_left(), 32768u);
    check("and a 256x256 plane needs more than that",
          gs_tex_register(plane_of(4), 256u, 256u, 256u, 256u) == 0);

    /* Recycling a pointer whose re-plan does not fit kills the entry rather than leaving it keyed
     * on an address the old plane owned: the alternative is a draw from somebody else's page. */
    check("the recycled re-plan that does not fit is refused",
          gs_tex_register(plane_of(0), 256u, 256u, 256u, 256u) == 0);
    check("the entry is dead rather than stale",
          gs_tex_at(0u) != 0 && plane_at(0u) == 0 && pending_at(0u) == 0);
    check("and the dead key no longer answers a draw call", gs_tex_find(plane_of(0)) == 0);
    check("a new plane is still planned after it", gs_tex_register(plane_of(5), 64u, 64u,
                                                                   64u, 64u) != 0);
    /* The dead slot IS the next plane's home now, and the abandoned span is its address: a
     * registry that only ever appends turned the eighth plane in a session into a texture with
     * no VRAM page -- which draws as one flat rectangle however alive its GL side is.  See
     * test_release_and_reuse() for the whole release law. */
    check_num("which takes the dead slot rather than growing the registry", gs_tex_count(), 3u);
    check("so the reused slot answers, and at the span the dead entry abandoned",
          plane_at(0u) == plane_of(5) && byte_at(0u) == k_atlas_byte);
    check_num("and the killed entry is the only one that stops counting as pending",
              gs_tex_pending(), 3u);
}

/* The release path, which is the half the registry was missing: a plane's death hands its slot
 * AND its VRAM span back, and the next plane takes both.  This is the second of the two
 * independent routes to the operator's flat icon rectangles -- the first is the process-global GL
 * context, gated in verify/tests/test_gl_context_isolation.c on painted pixels.  A texture can
 * survive its own context and still be refused a page here. */
static void test_release_and_reuse(void)
{
    const gs_tex_t *a, *b, *c, *d;
    uint32_t left_after_three;

    gs_tex_init();
    a = gs_tex_register(plane_of(0), 512u, 512u, 512u, 512u);
    b = gs_tex_register(plane_of(1), 64u, 64u, 64u, 64u);
    c = gs_tex_register(plane_of(2), 64u, 64u, 64u, 64u);
    check("three planes are planned", a != 0 && b != 0 && c != 0);
    left_after_three = gs_tex_vram_left();
    check_num("atlas + two sheets, from the cursor", left_after_three,
              4194304u - 1998848u - 1048576u - 16384u - 16384u);

    check_num("freeing the middle plane's span answers", gs_tex_free(plane_of(0)), 0);
    check("and a key that is gone stops answering a draw call", gs_tex_find(plane_of(0)) == 0);
    check("the entry is cleared, not shifted",
          plane_at(0u) == 0 && plane_at(1u) == plane_of(1));
    check_num("with the registry no shorter -- a middle slot cannot trim the tail",
              gs_tex_count(), 3u);
    check_num("and a non-trailing span leaves the cursor where it was",
              gs_tex_vram_left(), left_after_three);

    /* The whole point: a plane of the same size takes the abandoned span and the freed slot, so
     * the count never climbs and the address is one somebody just gave back. */
    d = gs_tex_register(plane_of(3), 512u, 512u, 512u, 512u);
    check("a fourth plane of the abandoned size is planned", d != 0);
    check("in the slot the first one vacated", d == gs_tex_at(0u));
    check_num("at the address it vacated", d != 0 ? d->byte : 0u, k_atlas_byte);
    check_num("and the registry did not grow", gs_tex_count(), 3u);
    /* The sharpest edge of taking a middle slot: `s_n` is a high-water mark, and code that treated
     * the reused index as a new end would shorten the registry to the height of the hole -- the
     * two planes above it would stop answering gs_tex_find() while still holding a VRAM span.
     * This is the first version of this rung's source got wrong, so it is gated by name. */
    check("and the planes above the hole still answer a draw call",
          gs_tex_find(plane_of(1)) == b && gs_tex_find(plane_of(2)) == c);
    check_num("nor did the cursor move", gs_tex_vram_left(), left_after_three);

    /* A smaller plane fits the same span, and the remainder has to stay available -- otherwise
     * reuse is a renaming of the leak rather than a fix for it. */
    gs_tex_init();
    check("a 512x512 atlas claims the first megabyte",
          gs_tex_register(plane_of(0), 512u, 512u, 512u, 512u) != 0);
    check("a sheet behind it starts at the atlas's end",
          gs_tex_register(plane_of(1), 64u, 64u, 64u, 64u) != 0 &&
          byte_at(1u) == k_atlas_byte + 1048576u);
    check_num("the atlas dies", gs_tex_free(plane_of(0)), 0);
    check("a small plane lands in its span",
          gs_tex_register(plane_of(2), 64u, 64u, 64u, 64u) != 0 &&
          byte_at(0u) == k_atlas_byte);
    check("and the 1,032,192 bytes left of it are not stranded: the next small plane is handed "
          "the remainder",
          gs_tex_register(plane_of(3), 64u, 64u, 64u, 64u) != 0 &&
          byte_at(2u) == k_atlas_byte + 16384u);

    /* Freeing the LAST entry trims the registry and moves the cursor, which is the number the
     * [GSTS] row prints as `vram=`. */
    gs_tex_init();
    check("two sheets", gs_tex_register(plane_of(0), 64u, 64u, 64u, 64u) != 0 &&
                        gs_tex_register(plane_of(1), 64u, 64u, 64u, 64u) != 0);
    left_after_three = gs_tex_vram_left();
    check("the trailing one dies", gs_tex_free(plane_of(1)) == 0);
    check_num("the registry is one shorter", gs_tex_count(), 1u);
    check_num("and the cursor came back for its span", gs_tex_vram_left(),
              left_after_three + 16384u);
    check("freeing a key that was never registered is refused, not a no-op",
          gs_tex_free(plane_of(7)) == -1);

    /* And the lowest span of all.  `byte` is an address, never a flag: a first version of this
     * rung guarded gs_vram_free() on `byte != 0`, which is harmless on the console -- where
     * gs_tex_init() starts the cursor above the display pages -- and leaks the whole span of any
     * entry planned at the arena's base.  The host link in verify/tests/test_fb_order.c never
     * calls gs_tex_init(), so there the base IS zero, and that is the build that found it. */
    gs_tex_init();
    check("a plane at the arena's own base is planned",
          gs_tex_register(plane_of(0), 64u, 64u, 64u, 64u) != 0 &&
          byte_at(0u) == 1998848u);
    check_num("freeing it answers", gs_tex_free(plane_of(0)), 0);
    check_num("and the cursor comes home, so the arena is whole again", gs_tex_vram_left(),
              4194304u - 1998848u);
}

/* The unit question the whole registry rests on: a plane's span is the *padded* picture's, on the
 * 16 KB unit gs_vram_tex_bytes() documents. */
static void test_plane_span(void)
{
    /* 32x128 texels: the width field is a whole number of 64-texel units, so the plane the GS
     * addresses is 64 wide and the span is 64*128*4 = 32,768, not 32*128*4 = 16,384. */
    check_num("a narrow plane is padded to the width unit before it is rounded",
              gs_vram_tex_bytes(32u, 128u), 32768u);
    check_num("and a 64x64 plane is one 16 KiB unit", gs_vram_tex_bytes(64u, 64u), 16384u);
    check_num("a 512x512 atlas is 64 of them", gs_vram_tex_bytes(512u, 512u), 1048576u);
    check_num("which is also its quadword count x 16",
              gs_vram_tex_bytes(512u, 512u), 65536u * 16u);
    /* The case the 16 KB unit exists for: 64x32 CT32 is 8,192 bytes, which is a whole 8 KB page and
     * half a plane.  Rounded to the page a frame buffer uses, two such planes would share one 4 KB
     * swizzle block -- the unit the picture is built out of -- which is why the answer is 16,384. */
    check_num("a plane smaller than one 16 KiB unit still takes the whole unit",
              gs_vram_tex_bytes(64u, 32u), 16384u);
    check_num("and one 64x64 plane is the smallest legal span",
              gs_vram_tex_bytes(64u, 64u), 16384u);
    check_num("TBW names 64-texel units, rounded up", gs_vram_fbw(512u), 8u);
    /* The atlas's 384-wide upload would be 6 units, and 6 is the wrong answer: rows would be read
     * 128 texels apart from where gl_image_alloc() put them.  TBW comes from the plane, which is
     * what test_two_sheets() already gated at 8. */
    check_num("an upload's own width is not the field's value", gs_vram_fbw(384u), 6u);

    gs_tex_init();
    check("the registry plans a narrow plane at its padded width",
          gs_tex_register(plane_of(6), 32u, 128u, 64u, 128u) != 0);
    check("with TBW from the plane, not the upload",
          tbw_at(0u) == 1u && pxw_at(0u) == 32u);
    check_num("and a stream that carries the whole padded plane", qwc_at(0u),
              64u * 128u / 4u);
    check_num("which is the span the cursor moved by", gs_tex_vram_left(),
              4194304u - 1998848u - 32768u);
}

/* SPEC: the GS tex registry follows the EEGS layer holder.  Claiming
 * the layer evicts every live plane so the incoming window can
 * register; gs_tex_register still refuses a 9th while the cap is
 * full.  gs_tex_init is not this path (it would drop the DMA sink). */
static void test_evict_all(void)
{
    uint32_t i, left0, n;
    const gs_tex_t *atlas, *font;

    gs_tex_init();
    left0 = gs_tex_vram_left();
    for (i = 0u; i < GS_TEX_MAX; i++)
        if (gs_tex_register(plane_of((int)i), 64u, 64u, 64u, 64u) == 0) break;
    check("the registry fills to its cap without complaint", i == GS_TEX_MAX);
    check("a plane past the cap is still refused",
          gs_tex_register(plane_of((int)GS_TEX_MAX), 64u, 64u, 64u, 64u) == 0);

    n = gs_tex_evict_all();
    check_num("evict-all drops every live plane", n, GS_TEX_MAX);
    check_num("the registry is empty", gs_tex_count(), 0u);
    check_num("and the VRAM cursor is home", gs_tex_vram_left(), left0);
    check("old keys no longer answer a draw call",
          gs_tex_find(plane_of(0)) == 0 && gs_tex_find(plane_of(7)) == 0);
    check_num("an empty evict is zero", gs_tex_evict_all(), 0u);

    /* Incoming layer (XMB) lands both sheets after a full Quake fill. */
    atlas = gs_tex_register(plane_of(0), 384u, 384u, 512u, 512u);
    font  = gs_tex_register(plane_of(1), 128u, 96u, 128u, 128u);
    check("the incoming atlas and font both land", atlas != 0 && font != 0);
    check_num("two slots, not nine", gs_tex_count(), 2u);
    check("the old third Quake plane is gone", gs_tex_find(plane_of(2)) == 0);

    /* Cap-refuse law still holds after a new fill of 8. */
    gs_tex_init();
    for (i = 0u; i < GS_TEX_MAX; i++)
        (void)gs_tex_register(plane_of((int)i), 64u, 64u, 64u, 64u);
    check("a 9th is still refused after a later fill",
          gs_tex_register(plane_of((int)GS_TEX_MAX), 64u, 64u, 64u, 64u) == 0);
}

static uint32_t cell_texel(const uint32_t *pm, int cell, int cx, int cy)
{
    int ox = (cell % XB_ATLAS_GRID) * XB_ICON_CELL;
    int oy = (cell / XB_ATLAS_GRID) * XB_ICON_CELL;
    return pm[(oy + cy) * XB_ICON_PAD + (ox + cx)];
}

/* GS source-over: (Cs-Cd)*As>>7+Cd, As in 0..128. */
static unsigned src_over_ch(unsigned cs, unsigned cd, unsigned sa)
{
    unsigned as = (sa * 128u + 127u) / 255u;
    int d = (int)cs - (int)cd;
    return (unsigned)(d * (int)as / 128 + (int)cd);
}

static int write_ppm(const char *path, int w, int h, const unsigned char *rgb)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 1, (size_t)w * (size_t)h * 3u, f);
    fclose(f);
    return 1;
}

/* Scale one 64×64 cell 4× into RGB, and the same cell source-over a cyan ribbon. */
static void dump_cell_pair(const uint32_t *pm, int cell, const char *stem)
{
    const int s = 4, n = XB_ICON_CELL * s;
    unsigned char *rgb, *over;
    int x, y, i;
    char path[256];

    rgb = (unsigned char *)calloc((size_t)n * (size_t)n * 3u, 1);
    over = (unsigned char *)calloc((size_t)n * (size_t)n * 3u, 1);
    if (!rgb || !over) {
        free(rgb);
        free(over);
        return;
    }
    for (y = 0; y < n; y++) {
        for (x = 0; x < n; x++) {
            uint32_t t = cell_texel(pm, cell, x / s, y / s);
            unsigned r = t & 0xFFu, g = (t >> 8) & 0xFFu;
            unsigned b = (t >> 16) & 0xFFu, a = (t >> 24) & 0xFFu;
            i = (y * n + x) * 3;
            rgb[i] = (unsigned char)r;
            rgb[i + 1] = (unsigned char)g;
            rgb[i + 2] = (unsigned char)b;
            /* Brightened-ribbon dest: high G/B, low R — the cyan the
             * hardware showed through A=0 STROKE holes. */
            over[i]     = (unsigned char)src_over_ch(r, 48u, a);
            over[i + 1] = (unsigned char)src_over_ch(g, 176u, a);
            over[i + 2] = (unsigned char)src_over_ch(b, 255u, a);
        }
    }
    snprintf(path, sizeof path, "/tmp/xmb_ps2_check/%s.ppm", stem);
    (void)write_ppm(path, n, n, rgb);
    snprintf(path, sizeof path, "/tmp/xmb_ps2_check/%s_over_cyan.ppm", stem);
    (void)write_ppm(path, n, n, over);
    free(rgb);
    free(over);
}

/* Coverage-only (POSIX) over the same cyan dest: this is the hardware cyan. */
static void dump_coverage_over_cyan(int cell, const char *stem)
{
    const int s = 4, n = XB_ICON_CELL * s;
    unsigned char *over;
    int x, y, i, ox, oy;
    char path[256];

    over = (unsigned char *)calloc((size_t)n * (size_t)n * 3u, 1);
    if (!over) return;
    ox = (cell % XB_ATLAS_GRID) * XB_ICON_CELL;
    oy = (cell / XB_ATLAS_GRID) * XB_ICON_CELL;
    for (y = 0; y < n; y++) {
        for (x = 0; x < n; x++) {
            unsigned a = xb_icon_alpha[(oy + y / s) * XB_ICON_TEX + (ox + x / s)];
            i = (y * n + x) * 3;
            over[i]     = (unsigned char)src_over_ch(248u, 48u, a);
            over[i + 1] = (unsigned char)src_over_ch(250u, 176u, a);
            over[i + 2] = (unsigned char)src_over_ch(255u, 255u, a);
        }
    }
    snprintf(path, sizeof path, "/tmp/xmb_ps2_check/%s_coverage_over_cyan.ppm", stem);
    (void)write_ppm(path, n, n, over);
    free(over);
}

/* Occupied non-droplet cells: navy A=255 inside the glyph bbox (STROKE
 * interiors included), A=0 in the square-cell corners. */
static void test_ps2_icon_plane_opaque(void)
{
    uint32_t *pm;
    int cell, cx, cy, drop_a = 0, hole_bad = 0, corner_ink = 0;
    uint32_t t, apps_c, cmd_c, apps_0;
    unsigned over_r, over_g, cov_r;

    pm = (uint32_t *)calloc((size_t)XB_ICON_PAD * XB_ICON_PAD, sizeof(uint32_t));
    check("icon plane alloc", pm != 0);
    if (!pm) return;
    xb_ps2_fill_icon_plane(pm, XB_ICON_PAD);

    /* Applications 2x2 gap is the cell centre: coverage is 0, bake is navy. */
    apps_c = cell_texel(pm, IC_CAT_APPS, XB_ICON_CELL / 2, XB_ICON_CELL / 2);
    check_num("Applications centre alpha is 255", (apps_c >> 24) & 0xFFu, 255u);
    check_num("Applications centre R is navy 12", apps_c & 0xFFu, 12u);

    /* Commands is a STROKE window plus a prompt.  Cell centre can clip the
     * chevron AA; pick a zero-coverage texel inside the window. */
    cmd_c = 0u;
    {
        int ox = (IC_CAT_COMMANDS % XB_ATLAS_GRID) * XB_ICON_CELL;
        int oy = (IC_CAT_COMMANDS / XB_ATLAS_GRID) * XB_ICON_CELL;
        int found = 0;
        for (cy = 12; cy < XB_ICON_CELL - 12 && !found; cy++) {
            for (cx = 12; cx < XB_ICON_CELL - 12; cx++) {
                if (xb_icon_alpha[(oy + cy) * XB_ICON_TEX + (ox + cx)])
                    continue;
                cmd_c = cell_texel(pm, IC_CAT_COMMANDS, cx, cy);
                found = 1;
                break;
            }
        }
        check("Commands has a hollow interior texel", found);
    }
    check_num("Commands interior alpha is 255", (cmd_c >> 24) & 0xFFu, 255u);
    check_num("Commands interior R is navy 12", cmd_c & 0xFFu, 12u);

    apps_0 = cell_texel(pm, IC_CAT_APPS, 0, 0);
    check_num("Applications cell corner alpha is 0", (apps_0 >> 24) & 0xFFu, 0u);
    t = cell_texel(pm, IC_CAT_COMMANDS, 0, 0);
    check_num("Commands cell corner alpha is 0", (t >> 24) & 0xFFu, 0u);

    /* Source-over the bake onto a brightened-ribbon dest.  A=255 navy must
     * replace cyan; A=0 corner must keep cyan.  Coverage-only (POSIX) at
     * the same centre keeps cyan — that is the hardware artifact. */
    over_r = src_over_ch(apps_c & 0xFFu, 48u, (apps_c >> 24) & 0xFFu);
    over_g = src_over_ch((apps_c >> 8) & 0xFFu, 176u, (apps_c >> 24) & 0xFFu);
    check_num("Applications centre over cyan is navy R", over_r, 12u);
    check_num("Applications centre over cyan is navy G", over_g, 20u);
    over_r = src_over_ch(apps_0 & 0xFFu, 48u, (apps_0 >> 24) & 0xFFu);
    check_num("Applications corner over cyan keeps ribbon R", over_r, 48u);
    cov_r = src_over_ch(248u, 48u, 0u);
    check_num("coverage-only centre over cyan is the ribbon (the artifact)",
              cov_r, 48u);

    for (cell = 0; cell < XB_ICON_CELLS; cell++) {
        int has = 0, bx0, by0, bx1, by1;
        int ox, oy;
        if (cell == IC_DROPLET) continue;
        ox = (cell % XB_ATLAS_GRID) * XB_ICON_CELL;
        oy = (cell / XB_ATLAS_GRID) * XB_ICON_CELL;
        bx0 = XB_ICON_CELL; by0 = XB_ICON_CELL; bx1 = -1; by1 = -1;
        for (cy = 0; cy < XB_ICON_CELL; cy++)
            for (cx = 0; cx < XB_ICON_CELL; cx++)
                if (xb_icon_alpha[(oy + cy) * XB_ICON_TEX + (ox + cx)]) {
                    has = 1;
                    if (cx < bx0) bx0 = cx;
                    if (cy < by0) by0 = cy;
                    if (cx > bx1) bx1 = cx;
                    if (cy > by1) by1 = cy;
                }
        if (!has) continue;
        if (bx0 > 0) bx0--;
        if (by0 > 0) by0--;
        if (bx1 < XB_ICON_CELL - 1) bx1++;
        if (by1 < XB_ICON_CELL - 1) by1++;
        for (cy = 0; cy < XB_ICON_CELL; cy++) {
            for (cx = 0; cx < XB_ICON_CELL; cx++) {
                uint32_t p = cell_texel(pm, cell, cx, cy);
                unsigned a = (p >> 24) & 0xFFu;
                int in_badge = (cx >= bx0 && cx <= bx1 &&
                                cy >= by0 && cy <= by1);
                if (in_badge) {
                    if (a != 255u || (p & 0xFFu) < 12u)
                        hole_bad++;
                } else if (a != 0u) {
                    corner_ink++;
                }
            }
        }
    }
    check_num("badge texels are opaque navy-or-white", (uint32_t)hole_bad, 0u);
    check_num("cell corners outside the badge stay A=0", (uint32_t)corner_ink, 0u);

    for (cy = 0; cy < XB_ICON_CELL; cy++)
        for (cx = 0; cx < XB_ICON_CELL; cx++)
            if (((cell_texel(pm, IC_DROPLET, cx, cy) >> 24) & 0xFFu) < 255u)
                drop_a++;
    check("droplet keeps some transparent texels for particles", drop_a > 0);

    t = pm[(XB_ICON_PAD - 1) * XB_ICON_PAD + (XB_ICON_PAD - 1)];
    check_num("pad corner stays 0", t, 0u);

    (void)system("mkdir -p /tmp/xmb_ps2_check");
    dump_cell_pair(pm, IC_CAT_APPS, "apps");
    dump_cell_pair(pm, IC_CAT_SETTINGS, "settings");
    dump_cell_pair(pm, IC_CAT_COMMANDS, "commands");
    dump_coverage_over_cyan(IC_CAT_APPS, "apps");
    dump_coverage_over_cyan(IC_CAT_COMMANDS, "commands");

    free(pm);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 GS texture-plane registry vs hand-built TEX0/TEX1/CLAMP...\n");
    printf("==========================================================\n");

    test_ps2_icon_plane_opaque();
    test_two_sheets();
    test_refusals();
    test_sink_at_registration();
    test_recycled_pointer();
    test_cap_and_exhaustion();
    test_release_and_reuse();
    test_plane_span();
    test_evict_all();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - every plane's address and words are what the fields say\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - a texture plane is planned in a unit the registers do not use\n");
    return 1;
}
