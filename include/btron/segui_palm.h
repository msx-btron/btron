/*
 * segui_palm.h — Palm OS-Like Application Runtime for PicoCalc SegUI (320x320)
 *
 * Implements the architecture specified in doc/txt/PICO.txt:
 *   - Single foreground Form + 4x2 Home Launcher (zero overlapping windows)
 *   - 8 frozen apps: Terminal, Editor, Browser, Cabinet, GLGears,
 *     Contacts, Phone, Preferences
 *   - Zero pixel coordinates in any app (100% SegUI autolayout)
 *   - Zero post-boot heap allocation (NASA JPL Rule 3)
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_SEGUI_PALM_H_
#define _BTRON_SEGUI_PALM_H_

#include <stdint.h>
#include <btron/segui.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PALM_APP_LAUNCHER = 0,
    PALM_APP_TERMINAL,
    PALM_APP_EDITOR,
    PALM_APP_BROWSER,
    PALM_APP_CABINET,
    PALM_APP_GLGEARS,
    PALM_APP_CONTACTS,
    PALM_APP_PHONE,
    PALM_APP_CHAT,
    PALM_APP_PREFS,
    PALM_APP_COUNT
} SEGUI_PALM_APP_ID;

/* Special key constants (matching STM32 i2ckbd + host test harness) */
#define PALM_KEY_UP         0xB5
#define PALM_KEY_DOWN       0xB6
#define PALM_KEY_LEFT       0xB4
#define PALM_KEY_RIGHT      0xB7
#define PALM_KEY_ENTER      0x0A
#define PALM_KEY_BS         0x08
#define PALM_KEY_TAB        0x09
#define PALM_KEY_ESC        0x1B
#define PALM_KEY_HOME       0x81   /* F1: Palm Home Launcher                */
#define PALM_KEY_MENU       0x82   /* F2: Palm Pull-Down Menu               */

typedef void (*SEGUI_PALM_OUT_FN)(const char *text, void *ctx);

/* Platform hardware bridge (mocked on host capture, wired in core_segui.c) */
typedef struct SEGUI_PALM_HW_OPS {
    const char *board_name;
    const char *cpu_name;
    uint32_t  (*get_uptime_ms)(void);
    uint32_t  (*get_sram_used)(void);
    uint32_t  (*get_sram_total)(void);
    int       (*get_task_count)(void);
    int       (*get_battery_pct)(void);
    void      (*set_lcd_backlight)(uint8_t level);
    void      (*set_kbd_backlight)(uint8_t level);
    void      (*set_uart_mirror)(int enabled);
    void      (*power_off)(void);
    void      (*reboot_bootsel)(void);
    void      (*shell_exec)(const char *cmd, SEGUI_PALM_OUT_FN out, void *ctx);
} SEGUI_PALM_HW_OPS;

typedef struct SEGUI_PALM_MENU_ITEM {
    const char *label;
    void (*action)(void);
} SEGUI_PALM_MENU_ITEM;

/* Lifecycle & rendering API */
void             segui_palm_init(const SEGUI_PROFILE *prof, const SEGUI_PALM_HW_OPS *hw);
void             segui_palm_switch(SEGUI_PALM_APP_ID id);
SEGUI_PALM_APP_ID segui_palm_active_id(void);
const char      *segui_palm_app_name(SEGUI_PALM_APP_ID id);
SEGUI_APP       *segui_palm_app(void);

/* Event & tick dispatch (returns TRUE when display needs repaint) */
BOOL             segui_palm_handle_key(int key, GDEV *dev);
BOOL             segui_palm_tick(uint32_t now_ms);
BOOL             segui_palm_is_animated(void);

/* Layout & paint (paints base SegUI + Palm menu/alert overlays) */
void             segui_palm_layout(GDEV *dev);
void             segui_palm_paint(GDEV *dev);

/* Modal alert helper */
void             segui_palm_alert(const char *title, const char *msg,
                                  void (*on_confirm)(void));

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_SEGUI_PALM_H_ */
