/*
 * shell_core.h — Shared Headless B-System Shell Dispatcher
 *
 * Provides real B-System filesystem & system commands (`ls`, `cat`, `df`,
 * `echo`, `help`, `ver`, `mem`, `ps`, `reboot`, `bootsel`) over cabinet_core
 * + platform hooks, with zero WND and zero heap dependencies.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_SHELL_CORE_H_
#define _BTRON_SHELL_CORE_H_

#include <stdint.h>

typedef void (*BtronShellOutFn)(const char *text, void *ctx);

typedef struct {
    const char *board_name;
    const char *cpu_name;
    uint32_t  (*get_sram_used)(void);
    uint32_t  (*get_sram_total)(void);
    void      (*fallback_exec)(const char *cmd, BtronShellOutFn out, void *ctx);
} BtronShellHooks;

/* Returns 1 if handled by shell_core, 0 if unknown/delegated */
int btron_shell_core_exec(const char *line, BtronShellOutFn out, void *ctx,
                          const BtronShellHooks *hooks);

#endif /* _BTRON_SHELL_CORE_H_ */
