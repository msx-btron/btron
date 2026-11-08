/*
 * ps2_boot.c — PlayStation 2 Cleanroom Boot Telemetry & Diagnostics
 *
 * Implements hardware bring-up logging and ASYNC plane calibration reports.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_boot.h"
#include "ps2_console.h"
#include "ps2_irq.h"
#include "ps2_fp.h"
#include "ps2_usb.h"
#include <btron/async_rt.h>
#include "gles/gl_backend_choice.h"

extern uint32_t ps2_count_read(void);
extern void ps2_delay_cycles(uint32_t count);
extern uint32_t g_kbd_repeat_delay_us;
extern uint32_t g_kbd_repeat_interval_us;
extern int      g_kbd_repeat_enabled;

static uint32_t ps2_us_since(uint32_t start)
{
    return (uint32_t)(ps2_count_read() - start) / btron_ee_ticks_per_us();
}

static void ps2_boot_hold(uint32_t ms)
{
    const uint32_t start  = ps2_count_read();
    const uint32_t budget = ms * (1000u * btron_ee_ticks_per_us());
    while ((uint32_t)(ps2_count_read() - start) < budget) {
        ps2_delay_cycles(200u);
    }
}

void ps2_log_boot_head(uint32_t gs_init_us, uint32_t full_upload_us, uint32_t band_upload_us, int timebase_ok)
{
    ps2_kprintf("[LOG] build %s %s\n", __DATE__, __TIME__);
    ps2_kprintf("[BOOT] B-System / BTRON3 3.20  Emotion Engine R5900 (MIPS-III LE)  32 MB RDRAM  no MMU\n");
    ps2_kprintf("[GS] SetGsCrt omode=0x52 720p  DISPLAY 800x600 mag1x  CT32  VRAM page 0 pitch 832 px (1.90 MB)\n");
    ps2_kprintf("[GS] init %u us   full canvas upload 120000 QW %u us   one console row-band %u us\n",
                (unsigned int)gs_init_us, (unsigned int)full_upload_us, (unsigned int)band_upload_us);
    ps2_kprintf("[CPU] CP0 Count %s  (%u ticks/us, %s)\n",
                timebase_ok ? "running" : "STOPPED, counted delays",
                (unsigned int)btron_ee_ticks_per_us(),
                btron_ee_ticks_per_us_measured()
                    ? "measured against the input plane"
                    : "documented rate, plane gave no window");
    {
        uint32_t fcsr = 0u;
        const uint32_t st = btron_fp_probe(&fcsr);
        ps2_kprintf("[CPU] CP0.Status=0x%08x  CU3..CU0=%d%d%d%d  IE=%d  EXL=%d\n",
                    (unsigned int)st,
                    (int)((st >> 31) & 1u), (int)((st >> 30) & 1u),
                    (int)((st >> 29) & 1u), (int)((st >> 28) & 1u),
                    (int)(st & 1u), (int)((st >> 1) & 1u));
        ps2_kprintf("[FPU] single precision: %s  proof add(1.0f,2.0f)=0x%08x want 0x40400000  FCSR=0x%08x\n",
                    btron_fp_on ? "Emotion Engine COP1" : "bit engine, COP1 did not answer",
                    (unsigned int)btron_fp_seen, (unsigned int)fcsr);
        ps2_kprintf("[CFG] this image: PS2_FP=%s  PS2_GL=%s\n",
                    btron_fp_law(), gl_backend_name(gl_backend_default()));
    }
}

void ps2_log_boot_tail(uint32_t probe_us, int hid_devs)
{
    ps2_kprintf("[USB] probe pass took %u us\n", (unsigned int)probe_us);
    ps2_kprintf("[SIO] SIO0 console is write-only here: PCSX2 wires no host terminal to it\n");
    ps2_kprintf("[PAD] DualShock decoder ready, no SIF/IOP pad client yet  input: %d USB HID\n",
                hid_devs);
    ps2_kprintf("[PTR] 1:1 hardware pointer (no curve)\n");
    ps2_kprintf("[KBD] auto-repeat: enabled=%d  delay=%u us  interval=%u us\n",
                g_kbd_repeat_enabled ? 1 : 0,
                (unsigned int)g_kbd_repeat_delay_us,
                (unsigned int)g_kbd_repeat_interval_us);
}

void ps2_log_async_calibration(void)
{
    const int arm_rc = ps2_irq_input_arm();
    const uint32_t st0 = ps2_irq_status();
    const uint32_t win0 = ps2_count_read();
    const uint32_t tick0 = ps2_irq_ticks();
    const uint32_t fm0 = ps2_usb_frame_number();
    uint32_t window_us, got, owed, frames;
    uint32_t st1;

    ps2_boot_hold(50u);
    window_us = ps2_us_since(win0);
    got = ps2_irq_ticks() - tick0;
    owed = window_us / ps2_irq_input_period_us();
    st1 = ps2_irq_status();

    frames = ps2_usb_frame_number() - fm0;
    ps2_kprintf("[ASYNC] input plane Timer0 (INTC 9): period=%u us compare=%u ticks=%u of the %u this %u us window owes -> %s%s\n",
                (unsigned int)ps2_irq_input_period_us(),
                (unsigned int)ps2_irq_timer_compare(),
                (unsigned int)got, (unsigned int)owed,
                (unsigned int)window_us,
                arm_rc < 0 ? "the INTC registry REJECTED the handler: " : "",
                (arm_rc < 0 || got == 0u) ? "DEAD"
                                          : ((got + 1u >= owed) ? "LIVE" : "STALLED"));

    if (arm_rc >= 0 && got > 0u) {
        ps2_usb_input_plane(1);
        ps2_kprintf("[ASYNC] hid collection: the input plane reads the OHCI rings and a pass delivers them\n");
    }

    ps2_kprintf("[ASYNC] machine microsecond = %u Count ticks, measured over %u periods of the plane (%u ticks summed, mean %u) -- %s\n",
                (unsigned int)btron_ee_ticks_per_us(),
                (unsigned int)ps2_irq_calib_periods(),
                (unsigned int)ps2_irq_calib_gap_ticks(),
                (unsigned int)(ps2_irq_calib_periods()
                                   ? ps2_irq_calib_gap_ticks() / ps2_irq_calib_periods() : 0u),
                btron_ee_ticks_per_us_measured()
                    ? "measured, so every microsecond this image prints is real time"
                    : "FALLBACK to documented 147");

    ps2_kprintf("[ASYNC] gaps since arm: %u..%u ticks = %u..%u us, against %u us period\n",
                (unsigned int)ps2_irq_gap_min_ticks(),
                (unsigned int)ps2_irq_gap_max_ticks(),
                (unsigned int)(ps2_irq_gap_min_ticks() / btron_ee_ticks_per_us()),
                (unsigned int)(ps2_irq_gap_max_ticks() / btron_ee_ticks_per_us()),
                (unsigned int)ps2_irq_input_period_us());
    ps2_kprintf("[ASYNC] OHCI frame counter: %u frames of %u -- %s\n",
                (unsigned int)frames, (unsigned int)owed,
                frames == 0u ? "controller not counting"
                : (frames + frames / 2u < owed ? "CP0 running FAST"
                : (owed + owed / 2u < frames ? "CP0 running SLOW" : "agree (50 ms)")));
    ps2_kprintf("[ASYNC] CP0.Status 0x%08x -> 0x%08x: EIE(bit16)=%d -> %d, IEc(bit0)=%d -> %d\n",
                (unsigned int)st0, (unsigned int)st1,
                (int)((st0 >> 16) & 1u), (int)((st1 >> 16) & 1u),
                (int)(st0 & 1u), (int)(st1 & 1u));

    const uint32_t cnt0 = ps2_irq_timer_count();
    uint32_t cnt1;
    ps2_boot_hold(2u);
    cnt1 = ps2_irq_timer_count();
    ps2_kprintf("[ASYNC] timer0 MODE=0x%08x count %u -> %u, compare=%u, ticks=%u\n",
                (unsigned int)ps2_irq_timer_mode(),
                (unsigned int)cnt0, (unsigned int)cnt1,
                (unsigned int)ps2_irq_timer_compare(),
                (unsigned int)ps2_irq_ticks());

    {
        const uint32_t wcet_100 = ps2_irq_wcet_ticks() * 100u / btron_ee_ticks_per_us();
        ps2_kprintf("[ASYNC] isr wcet=%u ticks = %u.%02u us of %u us budget, overruns=%u\n",
                    (unsigned int)ps2_irq_wcet_ticks(),
                    (unsigned int)(wcet_100 / 100u), (unsigned int)(wcet_100 % 100u),
                    (unsigned int)ASYNC_ISR_BUDGET_US,
                    (unsigned int)ps2_irq_overruns());
    }
}

