/*
 * BTRON3 BCM2711 Hardware HDMI Audio Driver (src/drivers/broadcom/bcm2711_hdmi_audio.c)
 * VideoCore VI HDMI0 MAI (Multi-channel Audio Interconnect) & ACR Engine
 *
 * Conforming to:
 *   - B-Audio Specification (b-media/audio.html)
 *   - NASA JPL "Power of Ten" Software Safety Rules
 *   - BCM2711 VideoCore VI Cadence HDMI IP Architecture
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <drivers/broadcom/bcm2711_hdmi_audio.h>
#include <drivers/broadcom/bcm2711_dma.h>
#include <drivers/virtio/virtio.h>
#include <btron/arm64_mem.h>

#define BT_ASSERT(cond) do { if (!(cond)) return -1; } while (0)

extern uintptr_t g_mmio_base;
extern void uart_puts(const char *s);
extern void uart_hex32(uint32_t val);
extern int  bcm2711_dma_start_cyclic(int channel, uintptr_t cb_phys);
extern void bcm2711_dma_stop(int channel);
extern uint32_t bcm2711_dma_get_cb(int channel);

/* MMIO register access helpers (NASA JPL Rule 4: small inline helpers) */
static inline uint32_t hdmi_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

static inline void hdmi_write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
    __asm__ volatile("dsb sy" : : : "memory");
}

/* ── Static NASA JPL Rule 3 Coherent Memory Buffers ───────────────── */

/* 2x 128-frame ping-pong buffers: 512 bytes each, 1024 bytes total */
static int16_t s_hdmi_pcm_ring[BCM2711_HDMI_RING_PERIODS][BCM2711_HDMI_PERIOD_FRAMES * BCM2711_HDMI_AUDIO_CHANNELS]
    __attribute__((aligned(64)));

/* DMA Control Blocks for continuous cyclic transfers */
static bcm2711_dma_cb_t s_hdmi_dma_cbs[BCM2711_HDMI_RING_PERIODS]
    __attribute__((aligned(32)));

/* Driver state */
typedef struct {
    bool     initialized;
    bool     opened;
    uint32_t sample_rate;
    uint8_t  channels;
    uint32_t write_idx;       /* Current buffer period being filled (0 or 1) */
    uint32_t in_flight_bytes; /* Approximate depth for telemetry */
} hdmi_audio_state_t;

static hdmi_audio_state_t s_hdmi_state = {0};

/* ── ACR & CEA-861 Calculations (Mathematical Helpers) ────────────── */

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

    /* Zero frame (NASA JPL bounded loop) */
    uint8_t *ptr = (uint8_t *)out_frame;
    for (int i = 0; i < (int)sizeof(*out_frame); i++) {
        ptr[i] = 0;
    }

    out_frame->type = CEA861_AUDIO_INFOFRAME_TYPE;
    out_frame->version = CEA861_AUDIO_INFOFRAME_VERS;
    out_frame->length = CEA861_AUDIO_INFOFRAME_LEN;

    /* PB1: Audio Coding (LPCM=0x01) | Channel count */
    out_frame->audio_coding = (uint8_t)((0x01u << 4) | (channels & 0x07u));

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

/* ── Hardware Probe & Register Initialization ─────────────────────── */

bool bcm2711_hdmi_audio_is_sink_present(void) {
    if (g_mmio_base != 0xFE000000UL) return false;
    /* Probe HDMI0 Hotplug register */
    uint32_t hpd = hdmi_read32(BCM2711_HDMI0_CORE_BASE + HDMI_HOTPLUG);
    return (hpd & 1u) != 0;
}

static void hdmi_mai_configure_registers(void) {
    uintptr_t mai_base = BCM2711_HDMI0_HD_BASE;

    /* 1. Reset MAI block */
    hdmi_write32(mai_base + HDMI_MAI_CTL, HDMI_MAI_CTL_RESET);
    for (volatile int i = 0; i < 50; i++) __asm__ volatile("nop");

    /* 2. Flush FIFO */
    hdmi_write32(mai_base + HDMI_MAI_CTL, HDMI_MAI_CTL_FLUSH);

    /* 3. Configure FIFO threshold watermark for DMA DREQ (16 samples) */
    hdmi_write32(mai_base + HDMI_MAI_THR, 0x10u);

    /* 4. Format: 16-bit signed stereo */
    hdmi_write32(mai_base + HDMI_MAI_FMT, 0x0000u);

    /* 5. Enable MAI */
    hdmi_write32(mai_base + HDMI_MAI_CTL, HDMI_MAI_CTL_ENABLE | HDMI_MAI_CTL_FORMAT_LPCM);
}

static void hdmi_packet_configure(uint32_t sample_rate) {
    uintptr_t core_base = BCM2711_HDMI0_CORE_BASE;

    /* 1. Setup ACR for 1024x768@60Hz (65 MHz pixel clock) */
    hdmi_acr_t acr;
    if (bcm2711_hdmi_calculate_acr(sample_rate, 65000000u, &acr) == 0) {
        hdmi_write32(core_base + HDMI_TX_PHY_ACR_0, acr.n);
        hdmi_write32(core_base + HDMI_TX_PHY_ACR_1, acr.cts);
    }

    /* 2. Build and write CEA-861 Audio InfoFrame into packet RAM */
    cea861_audio_infoframe_t frame;
    if (bcm2711_hdmi_build_infoframe(&frame, sample_rate, 2) == 0) {
        const uint32_t *words = (const uint32_t *)&frame;
        /* Copy 14 bytes (4 32-bit words) to packet RAM */
        for (int w = 0; w < 4; w++) {
            hdmi_write32(core_base + HDMI_RAM_PACKET_START + (w * 4), words[w]);
        }
        /* Enable packet transmission scheduler */
        hdmi_write32(core_base + HDMI_RAM_PACKET_CONFIG, 0x01u);
    }
}

static void hdmi_dma_setup_ring(void) {
    uintptr_t mai_data_phys = BCM2711_HDMI0_HD_BASE + HDMI_MAI_DATA;

    for (int i = 0; i < (int)BCM2711_HDMI_RING_PERIODS; i++) {
        int next_i = (i + 1) % BCM2711_HDMI_RING_PERIODS;

        s_hdmi_dma_cbs[i].ti = BCM_DMA_TI_DEST_DREQ |
                               BCM_DMA_TI_PERMAP(BCM2711_DMA_DREQ_HDMI0_MAI) |
                               BCM_DMA_TI_SRC_INC |
                               BCM_DMA_TI_WAIT_RESP |
                               BCM_DMA_TI_INTEN;
        s_hdmi_dma_cbs[i].source_ad = (uint32_t)(uintptr_t)&s_hdmi_pcm_ring[i][0];
        s_hdmi_dma_cbs[i].dest_ad = (uint32_t)mai_data_phys;
        s_hdmi_dma_cbs[i].txfr_len = BCM2711_HDMI_PERIOD_BYTES;
        s_hdmi_dma_cbs[i].stride = 0;
        s_hdmi_dma_cbs[i].nextconbk = (uint32_t)(uintptr_t)&s_hdmi_dma_cbs[next_i];
        s_hdmi_dma_cbs[i].reserved[0] = 0;
        s_hdmi_dma_cbs[i].reserved[1] = 0;
    }
}

/* ── Public B-Audio 5-Call Interface ──────────────────────────────── */

int bcm2711_hdmi_audio_init(void) {
    if (s_hdmi_state.initialized) return 0;

    /* Initialize memory buffers to silence */
    for (int p = 0; p < (int)BCM2711_HDMI_RING_PERIODS; p++) {
        for (int f = 0; f < (int)(BCM2711_HDMI_PERIOD_FRAMES * BCM2711_HDMI_AUDIO_CHANNELS); f++) {
            s_hdmi_pcm_ring[p][f] = 0;
        }
    }

    hdmi_dma_setup_ring();
    s_hdmi_state.initialized = true;
    uart_puts("[HDMI-AUDIO] BCM2711 MAI & ACR subsystem initialized.\n");
    return 0;
}

int bcm2711_hdmi_audio_open(uint32_t sample_rate, uint8_t channels) {
    BT_ASSERT(sample_rate > 0);
    BT_ASSERT(channels >= 1 && channels <= 2);

    if (s_hdmi_state.opened) {
        bcm2711_hdmi_audio_close();
    }

    if (!s_hdmi_state.initialized) {
        bcm2711_hdmi_audio_init();
    }

    s_hdmi_state.sample_rate = sample_rate ? sample_rate : BCM2711_HDMI_AUDIO_RATE;
    s_hdmi_state.channels = channels;
    s_hdmi_state.write_idx = 0;
    s_hdmi_state.in_flight_bytes = 0;

    /* If on real hardware, configure HDMI registers and start DMA */
    if (g_mmio_base == 0xFE000000UL) {
        hdmi_mai_configure_registers();
        hdmi_packet_configure(s_hdmi_state.sample_rate);
        bcm2711_dma_start_cyclic(BCM2711_HDMI_DMA_CHANNEL, (uintptr_t)&s_hdmi_dma_cbs[0]);
    }

    s_hdmi_state.opened = true;
    return 0;
}

int bcm2711_hdmi_audio_write(const int16_t *samples, size_t frames) {
    BT_ASSERT(samples != NULL);
    BT_ASSERT(frames > 0);

    if (!s_hdmi_state.opened) return -1;

    size_t written = 0;
    while (written < frames) {
        size_t chunk = frames - written;
        if (chunk > BCM2711_HDMI_PERIOD_FRAMES) {
            chunk = BCM2711_HDMI_PERIOD_FRAMES;
        }

        uint32_t p = s_hdmi_state.write_idx;
        int16_t *dst = &s_hdmi_pcm_ring[p][0];

        if (s_hdmi_state.channels == 2) {
            for (size_t i = 0; i < chunk * 2; i++) {
                dst[i] = samples[(written * 2) + i];
            }
        } else {
            /* Mono to stereo expansion */
            for (size_t i = 0; i < chunk; i++) {
                dst[i * 2 + 0] = samples[written + i];
                dst[i * 2 + 1] = samples[written + i];
            }
        }

        s_hdmi_state.write_idx = (s_hdmi_state.write_idx + 1) % BCM2711_HDMI_RING_PERIODS;
        s_hdmi_state.in_flight_bytes = (uint32_t)(chunk * s_hdmi_state.channels * sizeof(int16_t));
        written += chunk;
    }

    return (int)frames;
}

void bcm2711_hdmi_audio_close(void) {
    if (!s_hdmi_state.opened) return;

    if (g_mmio_base == 0xFE000000UL) {
        bcm2711_dma_stop(BCM2711_HDMI_DMA_CHANNEL);
        hdmi_write32(BCM2711_HDMI0_HD_BASE + HDMI_MAI_CTL, HDMI_MAI_CTL_RESET);
    }

    s_hdmi_state.opened = false;
    s_hdmi_state.in_flight_bytes = 0;
}

bool bcm2711_hdmi_audio_is_ready(void) {
    return s_hdmi_state.opened;
}

uint32_t bcm2711_hdmi_audio_get_queued_bytes(void) {
    if (!s_hdmi_state.opened) return 0;
    return s_hdmi_state.in_flight_bytes;
}

/* ── Canonical B-Audio virtio_sound_* Strong Implementations for AArch64 ── */
#if defined(__aarch64__) && !defined(BTRON_HOSTED)
int virtio_sound_open(uint32_t sample_rate, uint8_t channels) {
    return bcm2711_hdmi_audio_open(sample_rate, channels);
}

int virtio_sound_write(const int16_t *samples, size_t frames) {
    return bcm2711_hdmi_audio_write(samples, frames);
}

void virtio_sound_close(void) {
    bcm2711_hdmi_audio_close();
}

bool virtio_sound_is_ready(void) {
    return bcm2711_hdmi_audio_is_ready();
}

uint32_t virtio_sound_get_queued_bytes(void) {
    return bcm2711_hdmi_audio_get_queued_bytes();
}
#endif
