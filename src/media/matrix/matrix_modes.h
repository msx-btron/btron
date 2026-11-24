/*
 * matrix_modes.h — Rezmason/matrix 6-Mode Specification Tables & Glyph Sets
 *
 * Parameters ported directly from https://github.com/Rezmason/matrix/blob/master/js/config.js
 * by Jeremy Sachs (MIT License).
 */

#ifndef BTRON_MATRIX_MODES_H
#define BTRON_MATRIX_MODES_H

#include "matrix_internal.h"

/* 57 authentic glyphs from assets/ttf/Matrix-Code.ttf (excluding U+FFFD) */
static const uint32_t g_matrix_code_cps[57] = {
    0x0022u, 0x002au, 0x002bu, 0x0030u, 0x0031u, 0x0032u, 0x0033u, 0x0034u,
    0x0035u, 0x0037u, 0x0038u, 0x0039u, 0x003au, 0x003cu, 0x003eu, 0x007au,
    0x007cu, 0x00a6u, 0x00a9u, 0x254cu, 0x25aau, 0x30a2u, 0x30a6u, 0x30a8u,
    0x30aau, 0x30abu, 0x30adu, 0x30b1u, 0x30b3u, 0x30b5u, 0x30b7u, 0x30b9u,
    0x30bbu, 0x30bdu, 0x30bfu, 0x30c4u, 0x30c6u, 0x30cau, 0x30cbu, 0x30ccu,
    0x30cdu, 0x30cfu, 0x30d2u, 0x30dbu, 0x30deu, 0x30dfu, 0x30e0u, 0x30e1u,
    0x30e2u, 0x30e4u, 0x30e8u, 0x30e9u, 0x30eau, 0x30efu, 0x30fcu, 0xa78au,
    0xe937u
};

/* 138 authentic glyphs from assets/ttf/Matrix-Resurrected.ttf (U+E900..U+E989) */
static const uint32_t g_matrix_res_cps[138] = {
    0xe900u, 0xe901u, 0xe902u, 0xe903u, 0xe904u, 0xe905u, 0xe906u, 0xe907u,
    0xe908u, 0xe909u, 0xe90au, 0xe90bu, 0xe90cu, 0xe90du, 0xe90eu, 0xe90fu,
    0xe910u, 0xe911u, 0xe912u, 0xe913u, 0xe914u, 0xe915u, 0xe916u, 0xe917u,
    0xe918u, 0xe919u, 0xe91au, 0xe91bu, 0xe91cu, 0xe91du, 0xe91eu, 0xe91fu,
    0xe920u, 0xe921u, 0xe922u, 0xe923u, 0xe924u, 0xe925u, 0xe926u, 0xe927u,
    0xe928u, 0xe929u, 0xe92au, 0xe92bu, 0xe92cu, 0xe92du, 0xe92eu, 0xe92fu,
    0xe930u, 0xe931u, 0xe932u, 0xe933u, 0xe934u, 0xe935u, 0xe936u, 0xe937u,
    0xe938u, 0xe939u, 0xe93au, 0xe93bu, 0xe93cu, 0xe93du, 0xe93eu, 0xe93fu,
    0xe940u, 0xe941u, 0xe942u, 0xe943u, 0xe944u, 0xe945u, 0xe946u, 0xe947u,
    0xe948u, 0xe949u, 0xe94au, 0xe94bu, 0xe94cu, 0xe94du, 0xe94eu, 0xe94fu,
    0xe950u, 0xe951u, 0xe952u, 0xe953u, 0xe954u, 0xe955u, 0xe956u, 0xe957u,
    0xe958u, 0xe959u, 0xe95au, 0xe95bu, 0xe95cu, 0xe95du, 0xe95eu, 0xe95fu,
    0xe960u, 0xe961u, 0xe962u, 0xe963u, 0xe964u, 0xe965u, 0xe966u, 0xe967u,
    0xe968u, 0xe969u, 0xe96au, 0xe96bu, 0xe96cu, 0xe96du, 0xe96eu, 0xe96fu,
    0xe970u, 0xe971u, 0xe972u, 0xe973u, 0xe974u, 0xe975u, 0xe976u, 0xe977u,
    0xe978u, 0xe979u, 0xe97au, 0xe97bu, 0xe97cu, 0xe97du, 0xe97eu, 0xe97fu,
    0xe980u, 0xe981u, 0xe982u, 0xe983u, 0xe984u, 0xe985u, 0xe986u, 0xe987u,
    0xe988u, 0xe989u
};

/* 6-stripe Pride flag colors (Gilbert Baker / standard 6-color Pride flag) */
static const MatrixRGB g_pride_stripes[6] = {
    { 0xE4u, 0x03u, 0x03u },  /* Red    */
    { 0xFFu, 0x8Cu, 0x00u },  /* Orange */
    { 0xFFu, 0xEDu, 0x00u },  /* Yellow */
    { 0x00u, 0x80u, 0x26u },  /* Green  */
    { 0x24u, 0x40u, 0x8Eu },  /* Blue   */
    { 0x73u, 0x29u, 0x82u }   /* Violet */
};

static const MatrixModeSpec g_matrix_mode_specs[MATRIX_MODE_COUNT] = {
    /* 0: MATRIX_MODE_TRINITY — volumetric 3-plane silvery-emerald + warm gold glints */
    {
        .id             = "trinity",
        .label          = "Trinity",
        .font_id        = MATRIX_FONT_RESURRECTED,
        .num_cols_ref   = 60u,
        .flags          = MAT_FLAG_VOLUMETRIC | MAT_FLAG_GLINT | MAT_FLAG_BLOOM,
        .base_bright    = 0u,
        .fall_speed_q8  = 2400u,
        .cycle_speed_q8 = 48u,
        .decay_rate_q8  = 220u,
        .drop_len_min   = 7u,
        .drop_len_max   = 18u,
        .pal_bg         = { 0x00u, 0x00u, 0x00u },
        .pal_dim        = { 0x0Au, 0x36u, 0x28u },
        .pal_mid        = { 0x28u, 0xA8u, 0x7Cu },
        .pal_bright     = { 0x80u, 0xD8u, 0xB8u },
        .cursor_rgb     = { 0xB8u, 0xFFu, 0xCCu },
        .glint_rgb      = { 0xFFu, 0xCAu, 0x33u }
    },
    /* 1: MATRIX_MODE_CLASSIC — 1999/2003 opening titles emerald green */
    {
        .id             = "classic",
        .label          = "Classic",
        .font_id        = MATRIX_FONT_CODE,
        .num_cols_ref   = 64u,
        .flags          = MAT_FLAG_BLOOM,
        .base_bright    = 0u,
        .fall_speed_q8  = 2800u,
        .cycle_speed_q8 = 96u,
        .decay_rate_q8  = 190u,
        .drop_len_min   = 10u,
        .drop_len_max   = 26u,
        .pal_bg         = { 0x00u, 0x00u, 0x00u },
        .pal_dim        = { 0x04u, 0x42u, 0x0Cu },
        .pal_mid        = { 0x18u, 0xD8u, 0x30u },
        .pal_bright     = { 0x70u, 0xFFu, 0x80u },
        .cursor_rgb     = { 0xDAu, 0xFFu, 0xB8u },
        .glint_rgb      = { 0xFFu, 0xFFu, 0xFFu }
    },
    /* 2: MATRIX_MODE_RESURRECTIONS — 2021 135-glyph vibrant cyan-emerald */
    {
        .id             = "resurrections",
        .label          = "Resurrections",
        .font_id        = MATRIX_FONT_RESURRECTED,
        .num_cols_ref   = 64u,
        .flags          = MAT_FLAG_GLINT | MAT_FLAG_BLOOM,
        .base_bright    = 0u,
        .fall_speed_q8  = 2900u,
        .cycle_speed_q8 = 112u,
        .decay_rate_q8  = 200u,
        .drop_len_min   = 10u,
        .drop_len_max   = 24u,
        .pal_bg         = { 0x00u, 0x00u, 0x00u },
        .pal_dim        = { 0x00u, 0x4Cu, 0x38u },
        .pal_mid        = { 0x0Cu, 0xE8u, 0xA8u },
        .pal_bright     = { 0x80u, 0xFFu, 0xDCu },
        .cursor_rgb     = { 0xCCu, 0xFFu, 0xE6u },
        .glint_rgb      = { 0xFFu, 0xFFu, 0xFFu }
    },
    /* 3: MATRIX_MODE_OPERATOR — Nebuchadnezzar dense CRT + Chebyshev box ripple */
    {
        .id             = "operator",
        .label          = "Operator",
        .font_id        = MATRIX_FONT_CODE,
        .num_cols_ref   = 80u,
        .flags          = MAT_FLAG_BOX_RIPPLE | MAT_FLAG_BLOOM,
        .base_bright    = 36u,
        .fall_speed_q8  = 4200u,
        .cycle_speed_q8 = 44u,
        .decay_rate_q8  = 130u,
        .drop_len_min   = 14u,
        .drop_len_max   = 32u,
        .pal_bg         = { 0x00u, 0x00u, 0x00u },
        .pal_dim        = { 0x0Cu, 0x58u, 0x30u },
        .pal_mid        = { 0x1Au, 0xD8u, 0x80u },
        .pal_bright     = { 0x6Cu, 0xFFu, 0xB8u },
        .cursor_rgb     = { 0xA8u, 0xFFu, 0xDCu },
        .glint_rgb      = { 0xFFu, 0xFFu, 0xFFu }
    },
    /* 4: MATRIX_MODE_MEGACITY — Revolutions large bold glyphs, monumental pace */
    {
        .id             = "megacity",
        .label          = "Megacity",
        .font_id        = MATRIX_FONT_CODE,
        .num_cols_ref   = 40u,
        .flags          = MAT_FLAG_BLOOM,
        .base_bright    = 0u,
        .fall_speed_q8  = 1600u,
        .cycle_speed_q8 = 56u,
        .decay_rate_q8  = 135u,
        .drop_len_min   = 8u,
        .drop_len_max   = 20u,
        .pal_bg         = { 0x00u, 0x00u, 0x00u },
        .pal_dim        = { 0x08u, 0x48u, 0x12u },
        .pal_mid        = { 0x24u, 0xE0u, 0x38u },
        .pal_bright     = { 0x90u, 0xFFu, 0x7Cu },
        .cursor_rgb     = { 0xE8u, 0xFFu, 0xC4u },
        .glint_rgb      = { 0xFFu, 0xFFu, 0xD8u }
    },
    /* 5: MATRIX_MODE_PRIDE — 6-stripe Pride flag colors */
    {
        .id             = "pride",
        .label          = "Pride",
        .font_id        = MATRIX_FONT_CODE,
        .num_cols_ref   = 64u,
        .flags          = MAT_FLAG_PRIDE | MAT_FLAG_BLOOM,
        .base_bright    = 0u,
        .fall_speed_q8  = 2800u,
        .cycle_speed_q8 = 96u,
        .decay_rate_q8  = 185u,
        .drop_len_min   = 10u,
        .drop_len_max   = 26u,
        .pal_bg         = { 0x00u, 0x00u, 0x00u },
        .pal_dim        = { 0x30u, 0x30u, 0x30u },
        .pal_mid        = { 0xD0u, 0xD0u, 0xD0u },
        .pal_bright     = { 0xFFu, 0xFFu, 0xFFu },
        .cursor_rgb     = { 0xFFu, 0xFFu, 0xFFu },
        .glint_rgb      = { 0xFFu, 0xFFu, 0xFFu }
    }
};

#endif /* BTRON_MATRIX_MODES_H */
