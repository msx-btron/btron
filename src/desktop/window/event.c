/*
 * B-System (BTRON 3.20) System Event Queue: event.c
 * Pure Specification-based implementation of Sakamura BTRON / BTRON3 Architecture.
 */

#include <btron/event.h>
#include <btron/dp.h>
#include <btron/libc_shim.h>
#include <btron/nexus_ring.h>
#if BTRON_HOSTED
#include <SDL.h>
#endif

NEXUS_RING_DEFINE(evt_ring_t, EVT, 256)
NEXUS_RING_DEFINE(evt_edge_t, EVT, 64)

static evt_ring_t s_evt;
static evt_edge_t s_edge;
static EVT        s_pending_move;
static int        s_have_move;

static int evt_is_edge(UW type)
{
    return type == EV_KEY_DOWN || type == EV_KEY_UP ||
           type == EV_BUT_DOWN || type == EV_BUT_UP;
}

static int evt_queued(void)
{
    return (int)evt_ring_t_len(&s_evt) + (int)evt_edge_t_len(&s_edge) + s_have_move;
}

static int evt_take(EVT *p_evt)
{
    if (evt_ring_t_rcv(&s_evt, p_evt)) return 1;
    if (evt_edge_t_rcv(&s_edge, p_evt)) return 1;
    if (s_have_move) {
        *p_evt = s_pending_move;
        s_have_move = 0;
        return 1;
    }
    return 0;
}

static void evt_flush_move(void)
{
    if (!s_have_move) return;
    if (evt_ring_t_snd(&s_evt, &s_pending_move) == 0) s_have_move = 0;
}

ER init_evt_sys(void) {
    s_evt.head = s_evt.tail = s_evt.refused = 0;
    s_edge.head = s_edge.tail = s_edge.refused = 0;
    s_have_move = 0;
    return E_OK;
}

#if BTRON_HOSTED && !defined(_WIN32)
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static void poll_tty_stdin(void) {
    if (!isatty(STDIN_FILENO)) return;

    static BOOL flags_set = FALSE;
    if (!flags_set) {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        flags_set = TRUE;
    }

    char buf[64];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
        for (ssize_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)buf[i];
            EVT ev;
            memset(&ev, 0, sizeof(EVT));
            ev.type = EV_KEY_DOWN;

            if (c == '\r' || c == '\n') {
                ev.key = SDLK_RETURN;
                ev.data = 0;
            } else if (c == 127 || c == '\b') {
                ev.key = SDLK_BACKSPACE;
                ev.data = 0;
            } else if (c == 3) { /* Ctrl+C */
                ev.key = SDLK_c;
                ev.data = (VW)KMOD_CTRL;
            } else if (c == 7) { /* Ctrl+G (^G) - QEMU style mouse release/grab */
                ev.key = SDLK_g;
                ev.data = (VW)KMOD_CTRL;
            } else if (c == 12) { /* Ctrl+L */
                ev.key = SDLK_l;
                ev.data = (VW)KMOD_CTRL;
            } else if (c == 21) { /* Ctrl+U */
                ev.key = SDLK_u;
                ev.data = (VW)KMOD_CTRL;
            } else if (c == 27) { /* Escape sequence handling */
                if (i + 2 < n && buf[i+1] == '[') {
                    if (buf[i+2] == 'A') ev.key = SDLK_UP;
                    else if (buf[i+2] == 'B') ev.key = SDLK_DOWN;
                    else if (buf[i+2] == 'C') ev.key = SDLK_RIGHT;
                    else if (buf[i+2] == 'D') ev.key = SDLK_LEFT;
                    i += 2;
                } else {
                    ev.key = SDLK_ESCAPE;
                }
                ev.data = 0;
            } else if (c >= 32 && c <= 126) {
                ev.key = c;
                ev.data = (VW)1; /* 1 = Text Input Character */
            } else {
                continue;
            }

            printf("[TRACE-TTY] Read stdin byte=%d ('%c') -> Enqueued KEY_DOWN\n",
                   c, (c >= 32 && c <= 126) ? (char)c : '?');
            fflush(stdout);
            snd_evt(&ev);
        }
    }
}
#endif

ER snd_evt(const EVT *p_evt) {
    if (!p_evt) return E_PAR;

    /* MOVE coalesces in a producer-side slot: latest position wins, the ring
     * never holds a run of motion, and a published slot is never mutated. */
    if (p_evt->type == EV_MOUSE_MOVE) {
        s_pending_move = *p_evt;
        s_have_move = 1;
        return E_OK;
    }

    evt_flush_move();

    if (evt_ring_t_snd(&s_evt, p_evt) == 0) {
        if (p_evt->type == EV_KEY_DOWN || p_evt->type == EV_BUT_DOWN) {
#if BTRON_HOSTED
            printf("[TRACE-EVT] Enqueued type=%d key=%u ('%c') data=%ld (q_count=%d)\n",
                   p_evt->type, (unsigned)p_evt->key,
                   (p_evt->key >= 32 && p_evt->key <= 126) ? (char)p_evt->key : '?',
                   (long)(uintptr_t)p_evt->data, evt_queued());
            fflush(stdout);
#endif
        }
        return E_OK;
    }

    /* Ring full: never-drop KEY/BUT wait on the edge ring; MOVE already pending. */
    if (evt_is_edge(p_evt->type)) {
        return evt_edge_t_snd(&s_edge, p_evt) == 0 ? E_OK : E_BUSY;
    }
    return E_BUSY;
}

ER get_evt(EVT *p_evt, W timeout_ms) {
    if (!p_evt) return E_PAR;
    (void)timeout_ms;

#if BTRON_HOSTED && !defined(_WIN32)
    /* Poll TTY stdin for terminal input */
    poll_tty_stdin();
#endif

    /* Drain the Nexus queue first */
    if (evt_take(p_evt)) return E_OK;

#if BTRON_HOSTED
    /* Poll SDL events */
    SDL_Event sdlev;
    /* Wheel momentum accumulator.  A host trackpad/swipe delivers many
     * sub-line float deltas per gesture (and sign-reversed rubber-band
     * events when it settles at a scroll boundary).  Emitting on the raw
     * float sign turned every one of those into a PAGE step, which read as
     * scroll acceleration plus a bounce-back-down at the top edge.  Sum the
     * travel and emit one notch per whole line instead; a direction reversal
     * drops the leftover momentum so the settling tail can never fire a step. */
    static float s_wheel_accum = 0.0f;
    static int   s_wheel_sign  = 0;
    while (SDL_PollEvent(&sdlev)) {
        EVT ev;
        memset(&ev, 0, sizeof(EVT));

        switch (sdlev.type) {
            case SDL_QUIT:
                ev.type = EV_WND_CLOSE;
                snd_evt(&ev);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (!sdl_is_mouse_grabbed()) {
                    /* Clicking inside window re-grabs mouse pointer (QEMU style) */
                    sdl_set_mouse_grab(TRUE);
                }
                printf("[TRACE-SDL] MOUSEBUTTONDOWN pos=(%d,%d) btn=%d\n",
                       sdlev.button.x, sdlev.button.y, sdlev.button.button);
                fflush(stdout);
                ev.type = EV_BUT_DOWN;
                ev.pos.x = sdlev.button.x;
                ev.pos.y = sdlev.button.y;
                ev.button = sdlev.button.button;
                ev.data = (VW)(uintptr_t)sdlev.button.timestamp;
                if (ev.data == 0) ev.data = (VW)(uintptr_t)SDL_GetTicks();
                snd_evt(&ev);
                break;
            case SDL_MOUSEBUTTONUP:
                ev.type = EV_BUT_UP;
                ev.pos.x = sdlev.button.x;
                ev.pos.y = sdlev.button.y;
                ev.button = sdlev.button.button;
                ev.data = (VW)(uintptr_t)sdlev.button.timestamp;
                if (ev.data == 0) ev.data = (VW)(uintptr_t)SDL_GetTicks();
                snd_evt(&ev);
                break;
            case SDL_MOUSEMOTION:
                ev.type = EV_MOUSE_MOVE;
                ev.pos.x = sdlev.motion.x;
                ev.pos.y = sdlev.motion.y;
                snd_evt(&ev);
                break;
            case SDL_TEXTINPUT:
                /* Text input is handled directly via SDL_KEYDOWN to avoid duplicate keystroke events */
                break;
            case SDL_MOUSEWHEEL: {
                float dy = sdlev.wheel.y;
                if (sdlev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
                int dir = (dy > 0.0f) ? 1 : ((dy < 0.0f) ? -1 : 0);
                if (dir == 0) break;            /* null / settled event: ignore */
                if (dir != s_wheel_sign) {       /* reversal: drop trailing momentum */
                    s_wheel_accum = 0.0f;
                    s_wheel_sign = dir;
                }
                s_wheel_accum += dy;
                int notches = (int)s_wheel_accum;
                if (notches != 0) {
                    s_wheel_accum -= (float)notches;
                    ev.type = EV_KEY_DOWN;
                    ev.key = (notches > 0) ? BTRON_KEY_PAGE_UP : BTRON_KEY_PAGE_DOWN;
                    int n = (notches > 0) ? notches : -notches;
                    for (int i = 0; i < n; i++) snd_evt(&ev);
                }
                break;
            }
            case SDL_KEYDOWN:
                /* Check for ^G (Ctrl+G) or Ctrl+Alt+G release shortcut (QEMU style) */
                if ((sdlev.key.keysym.sym == SDLK_g && (sdlev.key.keysym.mod & KMOD_CTRL)) ||
                    ((sdlev.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) == (KMOD_CTRL | KMOD_ALT) && sdlev.key.keysym.sym == SDLK_g)) {
                    sdl_toggle_mouse_grab();
                    break;
                }
                ev.type = EV_KEY_DOWN;
                ev.key = sdlev.key.keysym.sym;
                ev.data = (VW)(uintptr_t)sdlev.key.keysym.mod;
                snd_evt(&ev);
                break;
            case SDL_KEYUP:
                ev.type = EV_KEY_UP;
                ev.key = sdlev.key.keysym.sym;
                ev.data = (VW)(uintptr_t)sdlev.key.keysym.mod;
                snd_evt(&ev);
                break;
            default:
                break;
        }

        /* If an event was enqueued, return the head immediately */
        if (evt_take(p_evt)) return E_OK;
    }
#endif

    p_evt->type = EV_NONE;
    return E_TMOUT;
}

ER clr_evt(UW mask) {
    EVT ev;
    if (mask == 0) return E_OK;
    while (evt_take(&ev)) { }
    return E_OK;
}

ER pke_evt(EVT *p_evt, UW mask) {
    if (!p_evt) return E_PAR;
    if (!evt_queued()) return E_NOEXS;
    if (evt_ring_t_len(&s_evt) != 0u) {
        *p_evt = s_evt.slot[s_evt.tail & 255u];
        if ((mask == 0) || (mask & EV_MASK(p_evt->type))) return E_OK;
    }
    if (evt_edge_t_len(&s_edge) != 0u) {
        *p_evt = s_edge.slot[s_edge.tail & 63u];
        if ((mask == 0) || (mask & EV_MASK(p_evt->type))) return E_OK;
    }
    if (s_have_move && ((mask == 0) || (mask & EV_MASK(EV_MOUSE_MOVE)))) {
        *p_evt = s_pending_move;
        return E_OK;
    }
    return E_NOEXS;
}

ER def_evt(W wndid, UW mask) {
    (void)wndid;
    (void)mask;
    return E_OK;
}
