/*
 * B-System POSIX termios Shim: include/termios.h
 *
 * On hosted POSIX/macOS targets this forwards to the system termios.h,
 * ensuring POSIX.1-2008 symbols are visible even under -std=c99.
 * On bare-metal targets (no host termios) it falls through to the
 * standalone B-System implementation in include/rt/termios.h.
 */

#ifndef __BTRON_TERMIOS_SHIM_H__
#define __BTRON_TERMIOS_SHIM_H__

/* Promote to POSIX.1-2008 before any system header sees the feature macros */
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) && !defined(_GNU_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#if defined(__has_include_next) && __has_include_next(<termios.h>)
/* Host system has a real termios — use it directly */
#  include_next <termios.h>
#else
/* Bare-metal / freestanding: use our own POSIX-compatible implementation */
#  include <rt/termios.h>
#endif

#endif /* __BTRON_TERMIOS_SHIM_H__ */
