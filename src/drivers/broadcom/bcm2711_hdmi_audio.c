/*
 * BTRON3 BCM2711 Hardware HDMI Audio Driver (src/drivers/broadcom/bcm2711_hdmi_audio.c)
 * VideoCore VI HDMI MAI (Multi-channel Audio Interconnect) & ACR Engine
 *
 * Apps open sound through virtio_sound_* below. That one sequence tries an
 * external USB DAC first, then Bluetooth when it is built, then HDMI.
 * Lil Cu, the orchestra, and tronMSX already call it; they do not program
 * HDMI themselves.
 *
 * The firmware owns HDMI video. This driver never resets the HDMI core and
 * never rewrites the timings. It only arms the MAI, the audio clock, and the
 * audio infoframe on the port whose cable is actually in.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <drivers/broadcom/bcm2711_hdmi_audio.h>
#include <drivers/broadcom/bcm2711_dma.h>
#include <drivers/broadcom/usb_audio_uac1.h>
#if defined(BTRON_HAVE_BLUETOOTH)
#include <drivers/bluetooth/b_bt_audio.h>
#endif
#include <drivers/virtio/virtio.h>
#include <btron/arm64_mem.h>

#define BT_ASSERT(cond) do { if (!(cond)) return -1; } while (0)

extern uintptr_t g_mmio_base;
extern void uart_puts(const char *s);
extern void uart_hex32(uint32_t val);

__attribute__((weak)) uint32_t bcm283x_get_board_revision(void) {
    return 0;
}

/* Strong definition lives in startup_arm.c once the framebuffer is up.
 * The host test does not link that file, so the weak symbol stays NULL. */
__attribute__((weak)) uint32_t *g_pi_fb_ptr;

#if defined(__aarch64__) && !defined(__APPLE__) && !defined(BTRON_HOSTED)
#define HDMI_DMA_MEM __attribute__((section(".btron.nocache"), aligned(64)))
#else
#define HDMI_DMA_MEM __attribute__((aligned(64)))
#endif

/* One desktop pump is ~16 ms. A 128-frame period is ~2.9 ms at 44.1 kHz,
 * so the DMA ring has to cover a whole frame or the idle period is replayed.
 * 8 periods is ~23 ms. The public 2-period constant stays the S16 contract. */
#define HDMI_IEC_PERIODS 8u
#define HDMI_IEC_FRAMES  BCM2711_HDMI_PERIOD_FRAMES
#define HDMI_IEC_WORDS   (HDMI_IEC_FRAMES * 2u)
#define HDMI_IEC_BYTES   (HDMI_IEC_WORDS * 4u)

/* Software S16 queue. 48 periods hold Lil Cu's 100 ms / 17640-byte target. */
#define HDMI_FIFO_FRAMES (48u * HDMI_IEC_FRAMES)

#define HDMI_MBOX_GET_CLOCK  0x00030002u
#define HDMI_MBOX_GET_GPIO   0x00030041u
#define HDMI_MBOX_CLOCK_PIXEL 9u
#define HDMI_GPIO_HPD0       128u
#define HDMI_GPIO_HPD1       129u
#define HDMI_PIXEL_FALLBACK  65000000u

/* ── MMIO ─────────────────────────────────────────────────────────── */

static inline uint32_t hdmi_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

static inline void hdmi_write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
    __asm__ volatile("dsb sy" ::: "memory");
}

static void hdmi_dsb(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint32_t cpu_to_bus(const void *p) {
    uintptr_t a = (uintptr_t)p;
    return (uint32_t)((a & ~0xC0000000UL) | 0xC0000000UL);
}

/* ── Coherent buffers ─────────────────────────────────────────────── */

/* IEC-60958 subframes, one 128-frame stereo period per slot. */
static uint32_t s_iec[HDMI_IEC_PERIODS][HDMI_IEC_WORDS] HDMI_DMA_MEM;

static bcm2711_dma_cb_t s_cb[HDMI_IEC_PERIODS] HDMI_DMA_MEM;
static uint32_t s_cb_bus[HDMI_IEC_PERIODS];

/* Private property-channel buffer. Do not reuse BTRON_NOCACHE_MBOX_ARM:
 * V3D owns that one and may be mid-call. */
static uint32_t s_mbox[16] HDMI_DMA_MEM;

/* CPU-only S16 queue. Not a DMA source. */
static int16_t s_fifo[HDMI_FIFO_FRAMES * 2u];
static uint32_t s_fifo_w;
static uint32_t s_fifo_r;

/* s_held: period already holds samples DMA has not finished.
 * s_fill_at: next period to try. s_dma_on: cyclic DMA and MAI are running. */
static uint8_t s_held[HDMI_IEC_PERIODS];
static int s_fill_at;
static int s_dma_on;
static int s_dma_fail_logged;
static int s_miss_logged;

typedef struct {
    bool     initialized;
    bool     opened;
    bool     hw;             /* DMA is running on a real port */
    int      port;           /* 0 or 1 when hw is set, else -1 */
    int      last_dma;       /* last CONBLK period, -1 until the first read */
    uint32_t sample_rate;
    uint8_t  channels;
    uint32_t iec_frame;      /* channel-status frame, 192-frame cycle */
} hdmi_audio_state_t;

static hdmi_audio_state_t s_hdmi_state;

typedef struct {
    uintptr_t core;
    uintptr_t ram;
    uint32_t  mai_off;
    uint32_t  data_bus;
    uint32_t  dreq;
} hdmi_port_t;

static const hdmi_port_t s_ports[2] = {
    { BCM2711_HDMI0_CORE_BASE, BCM2711_HDMI0_RAM_BASE, HDMI_MAI_CTL,
      BCM2711_HDMI0_MAI_DATA_BUS, BCM2711_DMA_DREQ_HDMI0_MAI },
    { BCM2711_HDMI1_CORE_BASE, BCM2711_HDMI1_RAM_BASE, HDMI_MAI_CTL + HDMI1_MAI_SHIFT,
      BCM2711_HDMI1_MAI_DATA_BUS, BCM2711_DMA_DREQ_HDMI1 },
};

/* ── ACR & CEA-861 ────────────────────────────────────────────────── */

int bcm2711_hdmi_calculate_acr(uint32_t sample_rate, uint32_t pixel_clock_hz, hdmi_acr_t *out_acr) {
    BT_ASSERT(sample_rate > 0);
    BT_ASSERT(pixel_clock_hz > 0);
    BT_ASSERT(out_acr != NULL);

    if (sample_rate == 48000u) {
        out_acr->n = 6144u;
        /* CTS = (pixel_clock_hz * 6144) / (128 * 48000) = pixel_clock_hz / 1000 */
        out_acr->cts = pixel_clock_hz / 1000u;
    } else if (sample_rate == 44100u) {
        out_acr->n = 6272u;
        out_acr->cts = (uint32_t)(((uint64_t)pixel_clock_hz * 6272ULL) / 5644800ULL);
    } else {
        out_acr->n = 6144u;
        out_acr->cts = pixel_clock_hz / 1000u;
    }
    return 0;
}

int bcm2711_hdmi_build_infoframe(cea861_audio_infoframe_t *out_frame, uint32_t sample_rate, uint8_t channels) {
    BT_ASSERT(out_frame != NULL);
    BT_ASSERT(channels >= 1 && channels <= 2);

    uint8_t *ptr = (uint8_t *)out_frame;
    for (int i = 0; i < (int)sizeof(*out_frame); i++) {
        ptr[i] = 0;
    }

    out_frame->type = CEA861_AUDIO_INFOFRAME_TYPE;
    out_frame->version = CEA861_AUDIO_INFOFRAME_VERS;
    out_frame->length = CEA861_AUDIO_INFOFRAME_LEN;

    /* PB1: coding type LPCM (1) in bits 7:4, CC = channels - 1 in bits 2:0.
     * Stereo is CC 1, so the byte is 0x11. CC 2 means three channels. */
    uint8_t cc = (uint8_t)((channels - 1u) & 0x07u);
    out_frame->audio_coding = (uint8_t)((0x01u << 4) | cc);

    /* PB2: Sampling Frequency & Sample Size (16-bit = 0x01) */
    uint8_t sf = (sample_rate == 44100u) ? 0x02u : 0x03u;
    out_frame->format_info = (uint8_t)((sf << 2) | 0x01u);

    out_frame->speaker_alloc = 0x00u; /* Front Left / Front Right */

    /* Checksum: 256 - (sum of all 14 bytes except checksum byte itself) */
    uint32_t sum = out_frame->type + out_frame->version + out_frame->length +
                   out_frame->audio_coding + out_frame->format_info +
                   out_frame->format_ext + out_frame->speaker_alloc +
                   out_frame->downmix_info;
    out_frame->checksum = (uint8_t)((256u - (sum & 0xFFu)) & 0xFFu);

    return 0;
}

/* ── Mailbox (pixel clock, cable detect) ──────────────────────────── */

static int hdmi_mbox8(uint32_t tag, uint32_t arg, uint32_t *out_word) {
    volatile uint32_t *buf;
    uintptr_t mbox_base;
    volatile uint32_t *status_reg;
    volatile uint32_t *write_reg;
    volatile uint32_t *read_reg;
    uint32_t mbox_addr;
    int to;

    if (g_mmio_base != 0xFE000000UL) return -1;

    buf = (volatile uint32_t *)s_mbox;
    buf[0] = 8u * 4u;
    buf[1] = 0;
    buf[2] = tag;
    buf[3] = 8;
    buf[4] = 0;
    buf[5] = arg;
    buf[6] = 0;
    buf[7] = 0;

    mbox_base = g_mmio_base + 0x0000B880UL;
    status_reg = (volatile uint32_t *)(mbox_base + 0x18u);
    write_reg  = (volatile uint32_t *)(mbox_base + 0x20u);
    read_reg   = (volatile uint32_t *)(mbox_base + 0x00u);
    mbox_addr = (uint32_t)(uintptr_t)s_mbox;
    hdmi_dsb();

    to = 50000;
    while ((*status_reg & 0x80000000u) && --to > 0) {
        __asm__ volatile("nop");
    }
    if (to <= 0) return -1;

    *write_reg = (mbox_addr & 0xFFFFFFF0u) | 8u;

    to = 50000;
    while (--to > 0) {
        int inner = 50000;
        while ((*status_reg & 0x40000000u) && --inner > 0) {
            __asm__ volatile("nop");
        }
        if (inner <= 0) return -1;
        if ((*read_reg & 0xFu) == 8u) break;
    }
    hdmi_dsb();
    if (buf[1] != 0x80000000u) return -1;
    if (out_word) *out_word = buf[6];
    return 0;
}

static uint32_t hdmi_pixel_clock_hz(void) {
    uint32_t hz = 0;
    if (hdmi_mbox8(HDMI_MBOX_GET_CLOCK, HDMI_MBOX_CLOCK_PIXEL, &hz) != 0 ||
        hz < 20000000u || hz > 400000000u) {
        hz = HDMI_PIXEL_FALLBACK;
    }
    uart_puts("[HDMI-AUDIO] pixel ");
    uart_hex32(hz);
    uart_puts("\n");
    return hz;
}

/* 1 = high, 0 = low, -1 = mailbox gave no answer. */
static int hdmi_gpio_level(uint32_t pin) {
    uint32_t state = 0;
    if (hdmi_mbox8(HDMI_MBOX_GET_GPIO, pin, &state) != 0) return -1;
    return state ? 1 : 0;
}

/* ── Sink / port ──────────────────────────────────────────────────── */

static int hdmi_is_qemu(void) {
    uint32_t board_rev = bcm283x_get_board_revision();
    if (board_rev == 0) return 0;
    if ((board_rev & 0x00F00000u) == 0x00B00000u) return 1;
    if (board_rev == 0x00B03115u || board_rev == 0x00B03111u) return 1;
    return 0;
}

/* VID_CTL lives in the shared HD block. HDMI0 is +0x44, HDMI1 is +0x48.
 * Bit 31 is the scanout enable the firmware sets on the live picture. */
#define HDMI0_VID_CTL_OFF  0x0044u
#define HDMI1_VID_CTL_OFF  0x0048u
#define HDMI_VID_CTL_ENABLE (1u << 31)

/* DVP audio gate. A set bit stops the 108 MHz parent (clk gate
 * set-to-disable). HDMI0 is bit 3, HDMI1 is bit 4. SW_INIT at +0x04 is
 * the HDMI reset; this driver never writes it. */
#define BCM2711_DVP_BASE          0xFEF00000UL
#define DVP_HT_RPI_MISC_CONFIG    0x08u

static int hdmi_port_scanning(int port) {
    uint32_t off = (port == 0) ? HDMI0_VID_CTL_OFF : HDMI1_VID_CTL_OFF;
    uint32_t vid = hdmi_read32(BCM2711_HDMI0_HD_BASE + off);
    return (vid & HDMI_VID_CTL_ENABLE) ? 1 : 0;
}

/* Open the 108 MHz parent into the MAI sampler. A closed gate leaves the
 * picture up and the audio tract stopped. */
static void hdmi_audio_clock_on(int port) {
    uintptr_t reg = BCM2711_DVP_BASE + DVP_HT_RPI_MISC_CONFIG;
    uint32_t bit = 1u << (3u + (uint32_t)port);
    uint32_t v = hdmi_read32(reg);
    if (v & bit) hdmi_write32(reg, v & ~bit);
    uart_puts("[HDMI-AUDIO] clk ");
    uart_hex32(hdmi_read32(reg));
    uart_puts("\n");
}

/* Follow the port the firmware is scanning. HDMI1 only when that jack is
 * the one with the cable. Otherwise HDMI0, including the case where neither
 * HPD bit reads back (firmware has the picture up and the hotplug register
 * did not). */
static int hdmi_select_port(void) {
    int v0 = hdmi_port_scanning(0);
    int v1 = hdmi_port_scanning(1);
    int g0;
    int g1;
    uint32_t h0;
    uint32_t h1;

    uart_puts("[HDMI-AUDIO] vid ");
    uart_hex32((uint32_t)v0 | ((uint32_t)v1 << 1));
    uart_puts("\n");
    if (v0 && !v1) return 0;
    if (v1 && !v0) return 1;

    g0 = hdmi_gpio_level(HDMI_GPIO_HPD0);
    g1 = hdmi_gpio_level(HDMI_GPIO_HPD1);
    if (g1 == 1 && g0 != 1) return 1;
    if (g0 == 1) return 0;

    h0 = hdmi_read32(BCM2711_HDMI0_CORE_BASE + HDMI_HOTPLUG) & HDMI_HOTPLUG_CONNECTED;
    h1 = hdmi_read32(BCM2711_HDMI1_CORE_BASE + HDMI_HOTPLUG) & HDMI_HOTPLUG_CONNECTED;
    uart_puts("[HDMI-AUDIO] HPD ");
    uart_hex32(h0 | (h1 << 1));
    uart_puts("\n");
    if (h1 && !h0) return 1;
    return 0;
}

bool bcm2711_hdmi_audio_is_sink_present(void) {
    int g0;
    int g1;
    uint32_t h0;
    uint32_t h1;

    if (g_mmio_base != 0xFE000000UL) return false;
    if (hdmi_is_qemu()) return false;

    g0 = hdmi_gpio_level(HDMI_GPIO_HPD0);
    g1 = hdmi_gpio_level(HDMI_GPIO_HPD1);
    if (g0 == 1 || g1 == 1) return true;

    h0 = hdmi_read32(BCM2711_HDMI0_CORE_BASE + HDMI_HOTPLUG) & HDMI_HOTPLUG_CONNECTED;
    h1 = hdmi_read32(BCM2711_HDMI1_CORE_BASE + HDMI_HOTPLUG) & HDMI_HOTPLUG_CONNECTED;
    if (h0 || h1) return true;

    /* Pi 400 has no analogue jack. A live framebuffer means the picture
     * is already on HDMI, even when the hotplug bit reads as zero. */
    if (g_pi_fb_ptr != NULL) return true;
    return false;
}

/* ── IEC 60958 subframe ───────────────────────────────────────────── */

static int iec_cs_bit(uint32_t frame, uint32_t rate) {
    uint8_t b3 = 0x00u;
    uint8_t byte;
    uint32_t bi = frame & 191u;
    uint32_t which = bi >> 3;

    if (rate == 48000u) b3 = 0x02u;
    else if (rate == 32000u) b3 = 0x03u;

    if (which == 0) byte = 0x04u;       /* consumer, linear PCM, no copyright */
    else if (which == 3) byte = b3;     /* sampling frequency */
    else if (which == 4) byte = 0x03u;  /* 24-bit slot, 16-bit samples */
    else byte = 0;
    return (int)((byte >> (bi & 7u)) & 1u);
}

/* 16-bit sample, left-aligned in the 24-bit audio field (bits 27:4).
 * Hardware overwrites the preamble. Even parity of bits 4..30 lands in bit 31. */
static uint32_t iec_subframe(int16_t sample, uint32_t frame, uint32_t rate) {
    uint32_t word = ((uint32_t)(uint16_t)sample) << 12;
    uint32_t p;
    if (iec_cs_bit(frame, rate)) word |= (1u << 30);
    p = word & 0x7FFFFFF0u;
    p ^= p >> 16;
    p ^= p >> 8;
    p ^= p >> 4;
    p ^= p >> 2;
    p ^= p >> 1;
    if (p & 1u) word |= (1u << 31);
    return word;
}

/* ── S16 FIFO ─────────────────────────────────────────────────────── */

static uint32_t fifo_fill(void) {
    return s_fifo_w - s_fifo_r;
}

static void fifo_reset(void) {
    uint32_t i;
    s_fifo_w = 0;
    s_fifo_r = 0;
    s_fill_at = 0;
    s_dma_on = 0;
    s_dma_fail_logged = 0;
    s_miss_logged = 0;
    for (i = 0; i < HDMI_IEC_PERIODS; i++) s_held[i] = 0;
}

static int fifo_push(const int16_t *samples, size_t frames) {
    size_t n = 0;
    int stereo = (s_hdmi_state.channels == 2);
    while (n < frames && fifo_fill() < HDMI_FIFO_FRAMES) {
        uint32_t slot = s_fifo_w % HDMI_FIFO_FRAMES;
        if (stereo) {
            s_fifo[slot * 2u]     = samples[n * 2u];
            s_fifo[slot * 2u + 1] = samples[n * 2u + 1];
        } else {
            s_fifo[slot * 2u]     = samples[n];
            s_fifo[slot * 2u + 1] = samples[n];
        }
        s_fifo_w++;
        n++;
    }
    return (int)n;
}

static void fifo_pop(int16_t *l, int16_t *r) {
    uint32_t slot;
    if (fifo_fill() == 0) {
        *l = 0;
        *r = 0;
        return;
    }
    slot = s_fifo_r % HDMI_FIFO_FRAMES;
    *l = s_fifo[slot * 2u];
    *r = s_fifo[slot * 2u + 1];
    s_fifo_r++;
}

/* ── DMA period service ───────────────────────────────────────────── */

static void hdmi_fill_period(int period) {
    volatile uint32_t *dst = s_iec[period];
    uint32_t rate = s_hdmi_state.sample_rate;
    uint32_t f;
    for (f = 0; f < HDMI_IEC_FRAMES; f++) {
        int16_t l;
        int16_t r;
        fifo_pop(&l, &r);
        dst[f * 2u]     = iec_subframe(l, s_hdmi_state.iec_frame, rate);
        dst[f * 2u + 1] = iec_subframe(r, s_hdmi_state.iec_frame, rate);
        s_hdmi_state.iec_frame++;
    }
    hdmi_dsb();
}

static int hdmi_live_period(void) {
    uint32_t cur = bcm2711_dma_get_cb(BCM2711_HDMI_DMA_CHANNEL);
    uint32_t i;
    for (i = 0; i < HDMI_IEC_PERIODS; i++) {
        if (cur == s_cb_bus[i]) return (int)i;
    }
    return -1;
}

/* Fill every period DMA is not reading and that does not already hold
 * unconsumed audio. A period is committed only when a full 128 frames are
 * waiting, so a 32-frame Orchestra tick is not padded with silence. */
static void hdmi_prime_idle(int live) {
    int n;
    for (n = 0; n < (int)HDMI_IEC_PERIODS; n++) {
        int p = (s_fill_at + n) % (int)HDMI_IEC_PERIODS;
        if (p == live || s_held[p]) continue;
        if (fifo_fill() < HDMI_IEC_FRAMES) return;
        hdmi_fill_period(p);
        s_held[p] = 1;
        s_fill_at = (p + 1) % (int)HDMI_IEC_PERIODS;
    }
}

/* DMA finished `last_dma` and has moved on to `live`. Rewrite each finished
 * period now. A short FIFO is an underrun: pad with silence instead of
 * replaying the previous phrase. Never write `live`. */
static void hdmi_release_finished(int live) {
    int guard = 0;
    if (s_hdmi_state.last_dma < 0) {
        s_hdmi_state.last_dma = live;
        return;
    }
    while (s_hdmi_state.last_dma != live && guard < (int)HDMI_IEC_PERIODS - 1) {
        int p = s_hdmi_state.last_dma;
        s_hdmi_state.last_dma = (s_hdmi_state.last_dma + 1) % (int)HDMI_IEC_PERIODS;
        guard++;
        s_held[p] = 0;
        hdmi_fill_period(p);
        s_held[p] = 1;
        s_fill_at = (p + 1) % (int)HDMI_IEC_PERIODS;
    }
}

static void hdmi_mai_enable(const hdmi_port_t *port) {
    uintptr_t mai = BCM2711_HDMI0_HD_BASE + port->mai_off;
    uint32_t en = ((uint32_t)s_hdmi_state.channels << HDMI_MAI_CTL_CHNUM_SHIFT) |
                  HDMI_MAI_CTL_WHOLSMP | HDMI_MAI_CTL_CHALIGN | HDMI_MAI_CTL_ENABLE;
    hdmi_write32(mai, en);
    /* ERRORF and ERRORE are write-1-to-clear. The FIFO can latch them
     * while it is still empty. Clear them with ENABLE held on. */
    hdmi_write32(mai, en | HDMI_MAI_CTL_ERRORE | HDMI_MAI_CTL_ERRORF);
}

/* Copy queued PCM into the IEC ring. DMA and MAI stay off until period 0
 * holds a full period, so the first words on the tract are the app's samples
 * rather than the silence cleared at open. */
static void hdmi_service(void) {
    int live;

    if (!s_hdmi_state.hw) return;

    if (!s_dma_on) {
        if (!s_held[0]) s_fill_at = 0;
        hdmi_prime_idle(-1);
        if (!s_held[0]) return;
        hdmi_dsb();
        if (bcm2711_dma_start_cyclic(BCM2711_HDMI_DMA_CHANNEL, s_cb_bus[0]) != 0) {
            if (!s_dma_fail_logged) {
                uart_puts("[HDMI-AUDIO] DMA start failed\n");
                s_dma_fail_logged = 1;
            }
            return;
        }
        hdmi_mai_enable(&s_ports[s_hdmi_state.port]);
        s_dma_on = 1;
        s_hdmi_state.last_dma = 0;
        {
            uint32_t n = 0;
            uint32_t i;
            for (i = 0; i < HDMI_IEC_PERIODS; i++) n += s_held[i];
            uart_puts("[HDMI-AUDIO] IEC ");
            uart_hex32(n);
            uart_puts("\n");
        }
        return;
    }

    live = hdmi_live_period();
    if (live < 0) {
        if (!s_miss_logged) {
            uart_puts("[HDMI-AUDIO] DMA CB ");
            uart_hex32(bcm2711_dma_get_cb(BCM2711_HDMI_DMA_CHANNEL));
            uart_puts("\n");
            s_miss_logged = 1;
        }
        /* Engine was started on period 0. Keep that one, fill the rest. */
        hdmi_prime_idle(0);
        return;
    }
    hdmi_release_finished(live);
    hdmi_prime_idle(live);
}

/* ── Register program (one port, no core reset) ───────────────────── */

static void mai_sample_ratio(uint32_t hsm, uint32_t fs, uint32_t *n_out, uint32_t *m_out) {
    uint32_t best_n = 1;
    uint32_t best_m = 1;
    uint64_t best_err = ~0ULL;
    uint32_t m;
    if (fs == 0) {
        *n_out = 1;
        *m_out = 1;
        return;
    }
    for (m = 1; m <= 256u; m++) {
        uint64_t num = (uint64_t)hsm * (uint64_t)m;
        uint64_t n64 = (num + (uint64_t)(fs / 2u)) / (uint64_t)fs;
        uint64_t prod;
        uint64_t err;
        if (n64 == 0 || n64 > 0xFFFFFFu) continue;
        prod = n64 * (uint64_t)fs;
        err = (prod > num) ? (prod - num) : (num - prod);
        if (err < best_err) {
            best_err = err;
            best_n = (uint32_t)n64;
            best_m = m;
            if (err == 0) break;
        }
    }
    *n_out = best_n;
    *m_out = best_m;
}

static uint32_t mai_rate_code(uint32_t rate) {
    if (rate == 44100u) return 8u;
    if (rate == 48000u) return 9u;
    if (rate == 32000u) return 7u;
    return 0u;
}

static void hdmi_write_infoframe(const hdmi_port_t *port, const cea861_audio_infoframe_t *frame) {
    uint8_t raw[32];
    uint32_t i;
    uintptr_t slot = port->ram + (HDMI_AUDIO_PACKET_ID * HDMI_PACKET_STRIDE);
    uintptr_t walk;
    const uint8_t *src = (const uint8_t *)frame;
    uint32_t cfg;

    for (i = 0; i < 32u; i++) raw[i] = 0;
    for (i = 0; i < sizeof(*frame) && i < 32u; i++) raw[i] = src[i];
    /* Drop only the audio slot. The firmware AVI packet stays enabled.
     * Writing the slot while it is still on the wire fails the checksum. */
    cfg = hdmi_read32(port->core + HDMI_RAM_PACKET_CONFIG);
    cfg &= ~(1u << HDMI_AUDIO_PACKET_ID);
    hdmi_write32(port->core + HDMI_RAM_PACKET_CONFIG, cfg);
    {
        uintptr_t clo = g_mmio_base + 0x3004u;
        uint32_t t0 = hdmi_read32(clo);
        uint32_t spins;
        /* CLO is the 1 MHz timer. The spin cap covers a timer that reads as zero. */
        for (spins = 0; spins < 200000u; spins++) {
            uint32_t st = hdmi_read32(port->core + HDMI_RAM_PACKET_STATUS);
            if ((st & (1u << HDMI_AUDIO_PACKET_ID)) == 0) break;
            if ((hdmi_read32(clo) - t0) > 1000u) break; /* 1 ms */
        }
    }

    for (i = 0; i < HDMI_PACKET_STRIDE; i += 4u) hdmi_write32(slot + i, 0);

    walk = slot;
    for (i = 0; i < 14u; i += 7u) {
        uint32_t w0 = (uint32_t)raw[i] |
                      ((uint32_t)raw[i + 1] << 8) |
                      ((uint32_t)raw[i + 2] << 16);
        uint32_t w1 = (uint32_t)raw[i + 3] |
                      ((uint32_t)raw[i + 4] << 8) |
                      ((uint32_t)raw[i + 5] << 16) |
                      ((uint32_t)raw[i + 6] << 24);
        hdmi_write32(walk, w0);
        walk += 4u;
        hdmi_write32(walk, w1);
        walk += 4u;
    }

    /* Keep the firmware AVI enable. OR the packet-RAM enable and audio slot. */
    cfg = hdmi_read32(port->core + HDMI_RAM_PACKET_CONFIG);
    cfg |= HDMI_RAM_PACKET_ENABLE | (1u << HDMI_AUDIO_PACKET_ID);
    hdmi_write32(port->core + HDMI_RAM_PACKET_CONFIG, cfg);
}

static void hdmi_force_mode_hdmi(const hdmi_port_t *port) {
    uint32_t sch = hdmi_read32(port->core + HDMI_SCHEDULER_CONTROL);
    uintptr_t clo = g_mmio_base + 0x3004u;
    uint32_t t0;
    if ((sch & HDMI_SCHEDULER_MODE_HDMI) == 0) {
        hdmi_write32(port->core + HDMI_SCHEDULER_CONTROL, sch | HDMI_SCHEDULER_MODE_HDMI);
    }
    t0 = hdmi_read32(clo);
    /* CLO is the 1 MHz system timer. The spin cap covers a timer that reads as zero. */
    for (uint32_t spins = 0; spins < 2000000u; spins++) {
        sch = hdmi_read32(port->core + HDMI_SCHEDULER_CONTROL);
        if (sch & HDMI_SCHEDULER_HDMI_ACTIVE) return;
        if ((hdmi_read32(clo) - t0) > 20000u) break; /* 20 ms, one frame */
    }
    uart_puts("[HDMI-AUDIO] HDMI_ACTIVE timeout\n");
}

static void hdmi_mai_stop(const hdmi_port_t *port) {
    uintptr_t mai = BCM2711_HDMI0_HD_BASE + port->mai_off;
    hdmi_write32(mai, HDMI_MAI_CTL_DLATE | HDMI_MAI_CTL_ERRORE | HDMI_MAI_CTL_ERRORF);
    hdmi_write32(mai, HDMI_MAI_CTL_RESET);
    hdmi_write32(mai, HDMI_MAI_CTL_ERRORF);
    hdmi_write32(mai, HDMI_MAI_CTL_FLUSH);
}

static void hdmi_program_port(const hdmi_port_t *port, uint32_t rate, uint8_t channels) {
    uintptr_t mai = BCM2711_HDMI0_HD_BASE + port->mai_off;
    uint32_t n = 1;
    uint32_t m = 1;
    uint32_t mask = (channels >= 2) ? 0x3u : 0x1u;
    uint32_t map = 0;
    uint32_t ch;
    hdmi_acr_t acr;
    uint32_t pixel;
    cea861_audio_infoframe_t frame;

    hdmi_audio_clock_on((port == &s_ports[1]) ? 1 : 0);

    hdmi_write32(mai, HDMI_MAI_CTL_RESET | HDMI_MAI_CTL_FLUSH |
                      HDMI_MAI_CTL_DLATE | HDMI_MAI_CTL_ERRORE | HDMI_MAI_CTL_ERRORF);

    mai_sample_ratio(BCM2711_HSM_CLOCK_HZ, rate, &n, &m);
    hdmi_write32(mai + (HDMI_MAI_SMP - HDMI_MAI_CTL), (n << 8) | (m - 1u));

    /* ENABLE comes after DMA is posting words. An enabled empty FIFO
     * latches ERRORE and the DREQ line stays quiet. */
    hdmi_write32(mai, ((uint32_t)channels << HDMI_MAI_CTL_CHNUM_SHIFT) |
                      HDMI_MAI_CTL_WHOLSMP | HDMI_MAI_CTL_CHALIGN);

    hdmi_write32(mai + (HDMI_MAI_FMT - HDMI_MAI_CTL),
                 (2u << 16) | (mai_rate_code(rate) << 8));
    hdmi_write32(mai + (HDMI_MAI_THR - HDMI_MAI_CTL), 0x10101C1Cu);

    hdmi_write32(port->core + HDMI_MAI_CONFIG_REG,
                 (1u << 27) | (1u << 26) | mask);
    for (ch = 0; ch < 8u; ch++) {
        if (mask & (1u << ch)) map |= ch << (4u * ch);
    }
    hdmi_write32(port->core + HDMI_MAI_CHANNEL_MAP, map);

    /* B-frame id 0x8 matches the ALSA IEC packer. Stereo leaves layout clear. */
    hdmi_write32(port->core + HDMI_AUDIO_PACKET_CONFIG,
                 (1u << 29) | (1u << 24) | (0x8u << 10) | mask);

    pixel = hdmi_pixel_clock_hz();
    if (bcm2711_hdmi_calculate_acr(rate, pixel, &acr) == 0) {
        hdmi_write32(port->core + HDMI_CRP_CFG, (1u << 24) | (acr.n & 0xFFFFFu));
        hdmi_write32(port->core + HDMI_CTS_0, acr.cts);
        hdmi_write32(port->core + HDMI_CTS_1, acr.cts);
    }

    hdmi_force_mode_hdmi(port);
    if (bcm2711_hdmi_build_infoframe(&frame, rate, channels) == 0) {
        hdmi_write_infoframe(port, &frame);
    }
}

static void hdmi_build_ring(const hdmi_port_t *port) {
    uint32_t i;
    /* NO_WIDE_BURSTS: a 128-bit beat into MAI_DATA is dropped and the
     * tract stays silent. One 32-bit word per beat matches the FIFO. */
    uint32_t ti = BCM_DMA_TI_NO_WIDE_BURSTS |
                  BCM_DMA_TI_DEST_DREQ |
                  BCM_DMA_TI_PERMAP(port->dreq) |
                  BCM_DMA_TI_SRC_INC |
                  BCM_DMA_TI_WAIT_RESP |
                  BCM_DMA_TI_INTEN;
    for (i = 0; i < HDMI_IEC_PERIODS; i++) {
        s_cb_bus[i] = cpu_to_bus(&s_cb[i]);
        s_cb[i].ti = ti;
        s_cb[i].source_ad = cpu_to_bus(&s_iec[i][0]);
        s_cb[i].dest_ad = port->data_bus;
        s_cb[i].txfr_len = HDMI_IEC_BYTES;
        s_cb[i].stride = 0;
        s_cb[i].nextconbk = 0; /* filled once every bus address is known */
        s_cb[i].reserved[0] = 0;
        s_cb[i].reserved[1] = 0;
    }
    for (i = 0; i < HDMI_IEC_PERIODS; i++) {
        uint32_t next = (i + 1u) % HDMI_IEC_PERIODS;
        s_cb[i].nextconbk = s_cb_bus[next];
    }
    hdmi_dsb();
}

static void hdmi_clear_iec(void) {
    uint32_t p;
    uint32_t i;
    for (p = 0; p < HDMI_IEC_PERIODS; p++) {
        for (i = 0; i < HDMI_IEC_WORDS; i++) s_iec[p][i] = 0;
    }
    hdmi_dsb();
}

/* ── Public B-Audio 5-Call Interface ──────────────────────────────── */

int bcm2711_hdmi_audio_init(void) {
    if (s_hdmi_state.initialized) return 0;
    hdmi_clear_iec();
    fifo_reset();
    s_hdmi_state.port = -1;
    s_hdmi_state.last_dma = -1;
    s_hdmi_state.initialized = true;
    uart_puts("[HDMI-AUDIO] BCM2711 MAI & ACR subsystem initialized.\n");
    return 0;
}

int bcm2711_hdmi_audio_open(uint32_t sample_rate, uint8_t channels) {
    int port;
    BT_ASSERT(sample_rate > 0);
    BT_ASSERT(channels >= 1 && channels <= 2);

    /* Host unit test sets g_mmio_base to 0. Do not touch a register. */
    if (g_mmio_base == 0xFE000000UL) {
        if (!bcm2711_hdmi_audio_is_sink_present()) return -1;
    }

    if (s_hdmi_state.opened) bcm2711_hdmi_audio_close();
    if (!s_hdmi_state.initialized) bcm2711_hdmi_audio_init();

    s_hdmi_state.sample_rate = sample_rate ? sample_rate : BCM2711_HDMI_AUDIO_RATE;
    s_hdmi_state.channels = channels;
    s_hdmi_state.iec_frame = 0;
    s_hdmi_state.last_dma = -1;
    s_hdmi_state.hw = false;
    s_hdmi_state.port = -1;
    fifo_reset();

    if (g_mmio_base == 0xFE000000UL) {
        port = hdmi_select_port();
        hdmi_clear_iec();
        hdmi_program_port(&s_ports[port], s_hdmi_state.sample_rate, channels);
        hdmi_build_ring(&s_ports[port]);
        /* DMA and MAI ENABLE wait for the first full IEC period. Starting
         * now would clock the cleared ring (silence) into the TV. */
        s_hdmi_state.port = port;
        s_hdmi_state.hw = true;
        uart_puts("[HDMI-AUDIO] port ");
        uart_hex32((uint32_t)port);
        uart_puts("\n");
    }

    s_hdmi_state.opened = true;
    return 0;
}

int bcm2711_hdmi_audio_write(const int16_t *samples, size_t frames) {
    int n;
    BT_ASSERT(samples != NULL);
    BT_ASSERT(frames > 0);
    if (!s_hdmi_state.opened) return -1;
    n = fifo_push(samples, frames);
    if (s_hdmi_state.hw) hdmi_service();
    return n;
}

void bcm2711_hdmi_audio_close(void) {
    if (!s_hdmi_state.opened && !s_hdmi_state.hw) return;
    if (s_hdmi_state.hw && s_hdmi_state.port >= 0 && s_hdmi_state.port < 2) {
        if (s_dma_on) {
            bcm2711_dma_stop(BCM2711_HDMI_DMA_CHANNEL);
            hdmi_mai_stop(&s_ports[s_hdmi_state.port]);
        }
    }
    s_hdmi_state.hw = false;
    s_hdmi_state.opened = false;
    s_hdmi_state.port = -1;
    s_hdmi_state.last_dma = -1;
    fifo_reset();
}

bool bcm2711_hdmi_audio_is_ready(void) {
    return s_hdmi_state.opened;
}

uint32_t bcm2711_hdmi_audio_get_queued_bytes(void) {
    if (!s_hdmi_state.opened) return 0;
    if (s_hdmi_state.hw) hdmi_service();
    return fifo_fill() * (uint32_t)s_hdmi_state.channels * 2u;
}

/* ── Canonical B-Audio virtio_sound_* sequence for AArch64 ───────── */
#if defined(__aarch64__) && !defined(BTRON_HOSTED)

enum {
    B_AUDIO_BACKEND_NONE = 0,
    B_AUDIO_BACKEND_USB,
    B_AUDIO_BACKEND_HDMI,
    B_AUDIO_BACKEND_BT
};
static int s_active_backend = B_AUDIO_BACKEND_NONE;

int virtio_sound_open(uint32_t sample_rate, uint8_t channels) {
    /* External DAC / headset before the TV. */
    if (usb_audio_uac1_is_present()) {
        if (usb_audio_uac1_open(sample_rate, channels) == 0) {
            s_active_backend = B_AUDIO_BACKEND_USB;
            uart_puts("[AUDIO] external USB\n");
            return 0;
        }
    }

#if defined(BTRON_HAVE_BLUETOOTH)
    if (b_bt_audio_is_present()) {
        if (b_bt_audio_open(sample_rate, channels) == 0) {
            s_active_backend = B_AUDIO_BACKEND_BT;
            uart_puts("[AUDIO] external BT\n");
            return 0;
        }
    }
#endif

    if (bcm2711_hdmi_audio_is_sink_present()) {
        if (bcm2711_hdmi_audio_open(sample_rate, channels) == 0) {
            s_active_backend = B_AUDIO_BACKEND_HDMI;
            uart_puts("[AUDIO] HDMI ");
            uart_hex32((uint32_t)s_hdmi_state.port);
            uart_puts("\n");
            return 0;
        }
    }

    s_active_backend = B_AUDIO_BACKEND_NONE;
    return -1;
}

int virtio_sound_write(const int16_t *samples, size_t frames) {
    if (s_active_backend == B_AUDIO_BACKEND_USB) {
        return usb_audio_uac1_write(samples, frames);
    } else if (s_active_backend == B_AUDIO_BACKEND_HDMI) {
        return bcm2711_hdmi_audio_write(samples, frames);
#if defined(BTRON_HAVE_BLUETOOTH)
    } else if (s_active_backend == B_AUDIO_BACKEND_BT) {
        return b_bt_audio_write(samples, frames);
#endif
    }
    return -1;
}

void virtio_sound_close(void) {
    if (s_active_backend == B_AUDIO_BACKEND_USB) {
        usb_audio_uac1_close();
    } else if (s_active_backend == B_AUDIO_BACKEND_HDMI) {
        bcm2711_hdmi_audio_close();
#if defined(BTRON_HAVE_BLUETOOTH)
    } else if (s_active_backend == B_AUDIO_BACKEND_BT) {
        b_bt_audio_close();
#endif
    }
    s_active_backend = B_AUDIO_BACKEND_NONE;
}

bool virtio_sound_is_ready(void) {
    if (s_active_backend == B_AUDIO_BACKEND_USB) {
        return usb_audio_uac1_is_ready();
    } else if (s_active_backend == B_AUDIO_BACKEND_HDMI) {
        return bcm2711_hdmi_audio_is_ready();
#if defined(BTRON_HAVE_BLUETOOTH)
    } else if (s_active_backend == B_AUDIO_BACKEND_BT) {
        return b_bt_audio_is_ready();
#endif
    }
    return false;
}

uint32_t virtio_sound_get_queued_bytes(void) {
    if (s_active_backend == B_AUDIO_BACKEND_USB) {
        return usb_audio_uac1_get_queued_bytes();
    } else if (s_active_backend == B_AUDIO_BACKEND_HDMI) {
        return bcm2711_hdmi_audio_get_queued_bytes();
#if defined(BTRON_HAVE_BLUETOOTH)
    } else if (s_active_backend == B_AUDIO_BACKEND_BT) {
        return b_bt_audio_get_queued_bytes();
#endif
    }
    return 0;
}
#endif
