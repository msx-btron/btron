/*
 * dp_accel.c -- pointer motion shared by every bare-metal port.
 *
 * A relative mouse report is a count, not a distance: what one count means on
 * screen depends on how the source produced it and on how wide the desktop is.
 * So this file owns only the arithmetic that is the same wherever it happens --
 * multiply, integrate, hand back whole pixels and remember the rest -- and each
 * port supplies its own multiplier and decides what a move is worth: the Pi 400
 * moves a hardware cursor plane for free, the PS2 has to repaint the desktop.
 *
 * The leftover is the whole reason this is shared rather than inlined at each
 * call.  Scaling by anything under 1.0 truncates a one-count report to zero, so
 * a pointer that cannot be moved gently is not a slow pointer, it is a broken
 * one; carrying the remainder in 1/256ths of a pixel is what lets a sub-1 scale
 * still answer a single count, eventually.
 *
 * Deliberately not here: the Haiku continuous curve, which is float arithmetic
 * and borrows an fsqrt instruction.  It stays in the port that offers it, and a
 * target with no floating point has to be given an integer curve of its own
 * rather than have this one weakened to suit both.
 */

#include <btron/dp_accel.h>
#include <btron/libc_shim.h>

/* Past this a report is not a hand moving a mouse; it is a burst, a stuck
 * device, or garbage off a bus, and none of them belong the pointer across the
 * desktop in one frame. */
#define DP_PTR_MAX_COUNTS 512
#define DP_PTR_MAX_PIXELS 512

int32_t dp_ptr_scale(int32_t raw, int32_t mult_fp, int32_t *carry)
{
    if (raw == 0) {
        return 0;
    }

    if (raw >  DP_PTR_MAX_COUNTS) raw =  DP_PTR_MAX_COUNTS;
    if (raw < -DP_PTR_MAX_COUNTS) raw = -DP_PTR_MAX_COUNTS;

    int32_t total = (carry ? *carry : 0) + raw * mult_fp;
    int32_t pixels = total / 256;

    if (carry) {
        int32_t rest = total % 256;
        if (rest >  255) rest =  255;
        if (rest < -255) rest = -255;
        *carry = rest;
    }

    if (pixels >  DP_PTR_MAX_PIXELS) pixels =  DP_PTR_MAX_PIXELS;
    if (pixels < -DP_PTR_MAX_PIXELS) pixels = -DP_PTR_MAX_PIXELS;
    return pixels;
}

int32_t dp_ptr_limit(int32_t want, int32_t max_step, int32_t *defer)
{
    int32_t v, out, debt, bound;

    if (max_step <= 0 || !defer) return want;

    v = want + *defer;
    if (v > max_step) out = max_step;
    else if (v < -max_step) out = -max_step;
    else out = v;

    debt = v - out;
    bound = max_step * DP_PTR_LIMIT_DEBT;
    if (debt >  bound) debt =  bound;
    if (debt < -bound) debt = -bound;
    *defer = debt;
    return out;
}

const int32_t btron_ptr_gain_fp[BTRON_PTR_GAIN_STEPS] = { 0, 64, 96, 128, 160, 192, 256 };

int btron_ptr_gain_index(int32_t fp)
{
    int i, best = 0;
    int32_t best_d = 0;

    if (fp <= 0) return 0;
    for (i = 1; i < BTRON_PTR_GAIN_STEPS; i++) {
        int32_t d = btron_ptr_gain_fp[i] - fp;
        if (d < 0) d = -d;
        if (i == 1 || d < best_d) { best_d = d; best = i; }
    }
    return best;
}

const char *btron_ptr_gain_label(int index)
{
    static char line[BTRON_PTR_GAIN_STEPS][32];
    static uint32_t built;
    int32_t fp;

    if (index < 0 || index >= BTRON_PTR_GAIN_STEPS) return "gain";
    if (!(built & (1u << index))) {
        fp = btron_ptr_gain_fp[index];
        if (index == 0)
            snprintf(line[index], sizeof(line[index]), "%s", "Auto (source gain)");
        else
            snprintf(line[index], sizeof(line[index]), "%d.%02d px per count",
                     (int)((fp * 100 + 128) / 256 / 100),
                     (int)((fp * 100 + 128) / 256 % 100));
        built |= 1u << index;
    }
    return line[index];
}

const uint32_t btron_ptr_delay_us[BTRON_PTR_DELAY_STEPS] = {
    0u, 250u, 500u, 1000u, 2000u, 4000u
};

int btron_ptr_delay_index(uint32_t delay_us)
{
    int i, best = 0;
    uint32_t best_d = 0u;

    if (delay_us == 0u) return 0;
    for (i = 1; i < BTRON_PTR_DELAY_STEPS; i++) {
        uint32_t stop = btron_ptr_delay_us[i];
        uint32_t d = (stop >= delay_us) ? (stop - delay_us) : (delay_us - stop);
        if (i == 1 || d < best_d) { best_d = d; best = i; }
    }
    return best;
}

const char *btron_ptr_delay_label(int index)
{
    static const char *const labels[BTRON_PTR_DELAY_STEPS] = {
        "Auto (source delay)",
        "0.25 ms (Compensated)",
        "0.50 ms (Compensated)",
        "1.00 ms (1 kHz)",
        "2.00 ms (500 Hz)",
        "4.00 ms (250 Hz)"
    };
    if (index < 0 || index >= BTRON_PTR_DELAY_STEPS) return "delay";
    return labels[index];
}
