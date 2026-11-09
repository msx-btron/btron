/*
 * B-System (BTRON 3.20) Retro OS Environment Main Launcher & Event Loop: main.c
 * Pure Specification-based implementation of Sakamura BTRON / BTRON3 Architecture.
 */

#include <btron/btron.h>
#include <btron/tip.h>
#include <btron/apps.h>
#include <btron/tracker.h>
#include <btron/global_menu.h>
#include <btron/desktop.h>
#include <btron/about.h>
#include <btron/dnd.h>
#include <SDL.h>
#include <time.h>
#include <btron/compositor.h>
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations for BTRON Accessories */
extern WND* open_vobj_manager_window(void);
extern WND* open_clarity_window(void);
extern WND* open_t_editor_window(void);
extern WND* open_gterm_window(void);
extern WND* open_audio_player_window(void);
extern WND* launch_beos_chat(void);
extern WND* open_glgears_window(void);
__attribute__((weak)) extern WND* open_lilcu64_demo_window(void);
extern void orchestra_ui_tick(void);
extern void cassette_service(void);

extern BOOL init_sdl_backend(H width, H height, const char *title);
extern void flush_gdev_to_sdl(GDEV *dev);
extern void shutdown_sdl_backend(void);
extern void raise_sdl_window(void);

extern void btron_kernel_init(int target_mode);

#if !defined(_WIN32)
#include <termios.h>
#include <unistd.h>

static struct termios g_orig_termios;
static BOOL g_termios_saved = FALSE;

static void restore_terminal_tty(void) {
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
    }
}

static void set_terminal_raw_tty(void) {
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &g_orig_termios) == 0) {
            g_termios_saved = TRUE;
            atexit(restore_terminal_tty);

            struct termios raw = g_orig_termios;
            raw.c_lflag &= ~(ECHO | ICANON | ISIG);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        }
    }
}
#endif /* !_WIN32 */

static BOOL s_desktop_running = TRUE;
static GDEV *s_desktop_screen_dev = NULL;

static BOOL s_dragging = FALSE;
static WND *s_drag_wnd = NULL;
static H s_drag_off_x = 0, s_drag_off_y = 0;

static BOOL s_sliding_tab = FALSE;
static WND *s_slide_wnd = NULL;
static H s_slide_start_x = 0, s_slide_orig_off = 0;

static BOOL s_resizing = FALSE;
static WND *s_resize_wnd = NULL;
static H s_resize_orig_w = 0, s_resize_orig_h = 0;
static H s_resize_start_x = 0, s_resize_start_y = 0;

/* Shared compositor policy (include/btron/compositor.h): the same pass ARM64 and PS2 run. */
static uint32_t posix_comp_now(void *x) { (void)x; return btron_render_perf_us(); }
static void     posix_comp_render(void *x, int full);

static int posix_comp_drain(void *x) {
    (void)x;
    EVT ev;
    /* Poll and process all pending system events */
    while (get_evt(&ev, 0) == E_OK) {
        if (ev.type == EV_WND_CLOSE) {
            s_desktop_running = FALSE;
            return -1;
        } else if (ev.type == EV_BUT_DOWN) {
            raise_sdl_window();
            /* 0. Check Global System Menu (Deskbar & Chokanji Top Menus) */
            if (global_menu_handle_mouse_down(ev.pos.x, ev.pos.y)) {
                /* Consumed by Global System Menu Bar */
            } else {
                if (global_menu_is_open()) {
                    global_menu_close();
                }
                WND *clicked = find_wnd_at(ev.pos.x, ev.pos.y);
                if (clicked) {
                    if (get_top_wnd() != clicked) {
                        tip_cancel(); /* Reset pending IME composition on focus switch */
                        top_wnd(clicked);
                    }

                    /* 1. BTRON3 Bottom-Right Corner Resize Grip Check (16x16 corner) */
                    if ((clicked->attr & WND_ATTR_RESIZE) &&
                        ev.pos.x >= clicked->bounds.right - 16 && ev.pos.x <= clicked->bounds.right &&
                        ev.pos.y >= clicked->bounds.bottom - 16 && ev.pos.y <= clicked->bounds.bottom) {
                        s_resizing = TRUE;
                        s_resize_wnd = clicked;
                        s_resize_orig_w = clicked->bounds.right - clicked->bounds.left;
                        s_resize_orig_h = clicked->bounds.bottom - clicked->bounds.top;
                        s_resize_start_x = ev.pos.x;
                        s_resize_start_y = ev.pos.y;
                    }
                    /* 2. Titlebar & Compact Sliding Tab Area Check */
                    else if (clicked->attr & WND_ATTR_TITLE) {
                        RECT tab_r;
                        wget_tab_rect(clicked, &tab_r);
                        if (ev.pos.y >= clicked->bounds.top && ev.pos.y < tab_r.bottom) {
                            /* Close button check */
                            if (whit_test_close_btn(clicked, ev.pos.x, ev.pos.y)) {
                                cls_wnd(clicked);
                                tip_cancel();
                            }
                            /* Compact Tab Check */
                            else if (whit_test_tab(clicked, ev.pos.x, ev.pos.y)) {
                                SDL_Keymod mod = SDL_GetModState();
                                BOOL is_slide_gesture = (mod & KMOD_SHIFT) || (ev.button == 3) ||
                                                        (ev.pos.x >= tab_r.left && ev.pos.x < tab_r.left + 12);

                                if (is_slide_gesture && (clicked->attr & WND_ATTR_SLIDING_TAB)) {
                                    s_sliding_tab = TRUE;
                                    s_slide_wnd = clicked;
                                    s_slide_start_x = ev.pos.x;
                                    s_slide_orig_off = clicked->tab_offset_x;
                                } else {
                                    s_dragging = TRUE;
                                    s_drag_wnd = clicked;
                                    s_drag_off_x = ev.pos.x - clicked->bounds.left;
                                    s_drag_off_y = ev.pos.y - clicked->bounds.top;
                                }
                            } else {
                                /* Clicked on window top rail outside tab -> drag whole window */
                                s_dragging = TRUE;
                                s_drag_wnd = clicked;
                                s_drag_off_x = ev.pos.x - clicked->bounds.left;
                                s_drag_off_y = ev.pos.y - clicked->bounds.top;
                            }
                        } else {
                            /* Dispatch mouse click exclusively to clicked window client area */
                            if (clicked->event_handler) {
                                clicked->event_handler(clicked, &ev);
                            }
                        }
                    } else {
                        /* Dispatch mouse click exclusively to clicked window client area */
                        if (clicked->event_handler) {
                            clicked->event_handler(clicked, &ev);
                        }
                    }
                } else if (desktop_handle_click(ev.pos.x, ev.pos.y)) {
                    /* Handled by desktop icon dispatcher */
                }
            } /* end else of tracker_handle_mouse_down */
        } else if (ev.type == EV_BUT_UP) {
            s_dragging = FALSE;
            s_drag_wnd = NULL;
            s_sliding_tab = FALSE;
            s_slide_wnd = NULL;
            s_resizing = FALSE;
            s_resize_wnd = NULL;
            if (btron_dnd_is_active()) {
                WND *target = find_wnd_at(ev.pos.x, ev.pos.y);
                if (target && target->event_handler) {
                    target->event_handler(target, &ev);
                }
                btron_dnd_end();
            } else {
                WND *top = get_top_wnd();
                if (top && top->focused && top->event_handler) {
                    top->event_handler(top, &ev);
                }
            }
        } else if (ev.type == EV_MOUSE_MOVE) {
            global_menu_handle_mouse_move(ev.pos.x, ev.pos.y);
            tracker_handle_mouse_move(ev.pos.x, ev.pos.y);
            if (btron_dnd_is_active()) {
                btron_dnd_update(ev.pos.x, ev.pos.y);
                WND *target = find_wnd_at(ev.pos.x, ev.pos.y);
                if (target && target->event_handler) {
                    target->event_handler(target, &ev);
                }
            } else if (s_sliding_tab && s_slide_wnd) {
                H new_off = s_slide_orig_off + (ev.pos.x - s_slide_start_x);
                wset_tab_offset(s_slide_wnd, new_off);
            } else if (s_resizing && s_resize_wnd) {
                H new_w = s_resize_orig_w + (ev.pos.x - s_resize_start_x);
                H new_h = s_resize_orig_h + (ev.pos.y - s_resize_start_y);
                rsz_wnd(s_resize_wnd, new_w, new_h);
            } else if (s_dragging && s_drag_wnd) {
                mov_wnd(s_drag_wnd, ev.pos.x - s_drag_off_x, ev.pos.y - s_drag_off_y);
            } else {
                WND *top = get_top_wnd();
                if (top && top->focused && top->event_handler) {
                    top->event_handler(top, &ev);
                }
            }
        } else if (ev.type == EV_KEY_DOWN || ev.type == EV_KEY_UP) {
            if (ev.type == EV_KEY_DOWN && global_menu_handle_key(ev.key, ev.data)) {
                /* Handled by Global System Menu */
            } else if (ev.type == EV_KEY_DOWN && tracker_handle_key(ev.key)) {
                /* Handled by Tracker Start menu navigation */
            } else {
                /* Exclusively route keystrokes to the top focused active window */
                WND *top = get_top_wnd();
                if (top && top->focused && top->event_handler) {
                    top->event_handler(top, &ev);
                }
            }
        }
    }

    return 1;   /* hosted desktop repaints at the plane period: animations and blink are damage */
}

static int  posix_comp_detect(void *x) { (void)x; return 1; }
static void posix_comp_idle(void *x) {
    struct timespec ts = { 0, 1000000L };   /* nothing due: sleep 1 ms, the INPUT period */
    (void)x;
    nanosleep(&ts, NULL);
}

static void posix_comp_render(void *x, int full) {
    (void)x; (void)full;
    orchestra_ui_tick();
    cassette_service();

    if (about_is_animating()) {
        WND *aw = about_get_wnd();
        if (aw) inval_wnd(aw);
    }

    /* Continuous cursor blinking for the active focused terminal / window */
    WND *top_focused = get_top_wnd();
    if (top_focused && top_focused->focused) {
        inval_wnd(top_focused);
    }

    /* Render full BTRON desktop composition */
    render_desktop_background(s_desktop_screen_dev);
    redraw_all_windows();

    /* Floating candidate window is rendered exclusively for the active window */
    WND *top = get_top_wnd();
    if (top && top->focused && (tip_get_state() == TIP_STATE_CONVERTING || tip_get_state() == TIP_STATE_CANDIDATE_SELECT)) {
        tip_render_candidate_window(s_desktop_screen_dev, tip_get_caret_x(), tip_get_caret_y());
    }

    render_system_panel(s_desktop_screen_dev);

    /* Render Global Menu dropdown overlay when open */
    if (global_menu_is_open()) {
        global_menu_render_overlay(s_desktop_screen_dev);
    }

    /* Render Direct-Manipulation Drag-and-Drop Ghost Badge */
    if (btron_dnd_is_active()) {
        btron_dnd_render_ghost(s_desktop_screen_dev);
    }

    /* Flush composite buffer to SDL window */
    flush_gdev_to_sdl(s_desktop_screen_dev);
}

static const btron_plane_cfg_t s_posix_planes = {
    .input_period_us = ASYNC_INPUT_PERIOD_US, .ui_period_us = 16667u,
    .panel_refresh_us = 1000000u, .full_convergence_us = 0u, .input_slices_max = 16u,
    .ui_period_slow_us = 33333u, .ui_slow_paint_us = 30000u
};
static const compositor_ops_t s_posix_comp_ops = {
    .now_us = posix_comp_now, .drain_events = posix_comp_drain, .detect_damage = posix_comp_detect,
    .render = posix_comp_render, .present = NULL, .idle = posix_comp_idle
};
static compositor_t s_posix_comp;
static int s_posix_comp_ready;

void btron_desktop_step(void) {
    if (!s_desktop_screen_dev) {
        s_desktop_screen_dev = wnd_mgr_get_screen();
        if (!s_desktop_screen_dev) return;
    }
    if (!s_posix_comp_ready) {
        btron_planes_install(&s_posix_planes);
        comp_init(&s_posix_comp, &s_posix_comp_ops, btron_planes(), ASYNC_UI_BUDGET_US);
        s_posix_comp_ready = 1;
    }
    (void)comp_pass(&s_posix_comp);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

#if !defined(_WIN32)
    set_terminal_raw_tty();
#endif

    H screen_w = 1280;
    H screen_h = 800;

#ifndef BTRON_TARGET
#define BTRON_TARGET 0
#endif
    btron_kernel_init(BTRON_TARGET);

    if (!init_sdl_backend(screen_w, screen_h, "B-System Retro OS Desktop Environment (SDL2)")) {
        fprintf(stderr, "[B-System] Failed to initialize SDL2 backend.\n");
        return 1;
    }

    if (init_desktop(screen_w, screen_h) != E_OK) {
        fprintf(stderr, "[B-System] Desktop initialization failed.\n");
        shutdown_sdl_backend();
        return 1;
    }

    tip_init();
    init_evt_sys();
    btron_dnd_init();

    printf("[B-System] Launching Sakamura B-System Desktop & Accessories...\n");

    s_desktop_screen_dev = opn_dev(screen_w, screen_h);
    init_wnd_mgr(s_desktop_screen_dev);
    tracker_init();
    global_menu_init();

    /* Initial desktop state: accessories are launched on-demand via Deskbar or Workbench icons */

    while (s_desktop_running) {
        btron_desktop_step();
    }

    printf("[B-System] Shutting down B-System Retro OS Environment.\n");
    cls_dev(s_desktop_screen_dev);
    shutdown_sdl_backend();
    return 0;
}
