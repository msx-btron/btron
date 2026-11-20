/*
 * ui_mode.h — Dual UI Capability Contract for B-System (Desktop WND vs SegUI)
 *
 * Allows shared B-System domain engines (*_core.c) and apps to adapt cleanly
 * to limited-resolution SegUI targets (ClockworkPi PicoCalc 320x320) without
 * breaking any existing desktop, Pi 400, PS2, UEFI, PC-98, or FOMA build.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_UI_MODE_H_
#define _BTRON_UI_MODE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTRON_UI_DESKTOP = 0,   /* Full WND compositor (default on all desktop targets) */
    BTRON_UI_SEGUI   = 1    /* Single-form coordinate-free SegUI (PicoCalc 320x320) */
} BTRON_UI_MODE;

#if defined(BTRON_SEGUI_COMPACT)
/* Compile-time constant on MCU targets so -Os --gc-sections strips desktop paths */
static inline BTRON_UI_MODE btron_ui_mode(void) { return BTRON_UI_SEGUI; }
static inline int btron_ui_is_segui(void)       { return 1; }
#else
extern BTRON_UI_MODE g_btron_ui_mode;
static inline BTRON_UI_MODE btron_ui_mode(void) { return g_btron_ui_mode; }
static inline int btron_ui_is_segui(void)       { return g_btron_ui_mode == BTRON_UI_SEGUI; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_UI_MODE_H_ */
