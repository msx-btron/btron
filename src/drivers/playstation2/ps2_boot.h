/*
 * ps2_boot.h — PlayStation 2 Cleanroom Boot Telemetry & Diagnostics
 *
 * Implements hardware bring-up logging and ASYNC plane calibration reports.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_BOOT_H
#define PS2_BOOT_H

#include <stdint.h>
#include <stddef.h>

void ps2_log_boot_head(uint32_t gs_init_us, uint32_t full_upload_us, uint32_t band_upload_us, int timebase_ok);
void ps2_log_boot_tail(uint32_t probe_us, int hid_devs);
void ps2_log_async_calibration(void);

#endif /* PS2_BOOT_H */

