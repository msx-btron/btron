/*
 * BTRON3 USB Audio Class 1.0 (UAC1) Driver (src/drivers/broadcom/usb_audio_uac1.c)
 * Isochronous PCM streaming over VIA VL805 xHCI on Raspberry Pi 400 & QEMU
 *
 * Conforming to:
 *   - USB Device Class Definition for Audio Devices v1.0
 *   - B-Audio Specification (b-media/audio.html)
 *   - NASA JPL "Power of Ten" Software Safety Rules
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <drivers/broadcom/usb_audio_uac1.h>
#include <drivers/broadcom/xhci.h>
#include <btron/arm64_mem.h>

#define BT_ASSERT(cond) do { if (!(cond)) return -1; } while (0)

extern void uart_puts(const char *s);
extern void uart_hex32(uint32_t val);

/* ── NASA JPL Rule 3: Static Coherent Memory Pools ────────────────── */

#if defined(__aarch64__) && !defined(__APPLE__) && !defined(BTRON_HOSTED)
#define UAC1_DMA_BUF __attribute__((aligned(64), section(".btron.nocache")))
#else
#define UAC1_DMA_BUF __attribute__((aligned(64)))
#endif

/* 16x 192-byte isochronous packet buffers (16 ms total queue depth) */
static int16_t s_uac1_pcm_ring[UAC1_RING_PACKETS][UAC1_FRAME_SAMPLES_1MS * UAC1_DEFAULT_CHANNELS]
    UAC1_DMA_BUF;

typedef struct {
    bool              attached;
    bool              opened;
    uint32_t          slot_id;
    uint8_t           ep_addr;
    uint32_t          ep_mps;
    uint32_t          sample_rate;
    uint8_t           channels;
    uint32_t          write_packet_idx;
    volatile uint32_t queued_packets;
} uac1_driver_state_t;

static uac1_driver_state_t s_uac1 = {0};

/* ── Device Lifecycle ─────────────────────────────────────────────── */

void usb_audio_uac1_init(void) {
    s_uac1.attached = false;
    s_uac1.opened = false;
    s_uac1.slot_id = 0;
    s_uac1.ep_addr = 0;
    s_uac1.ep_mps = 0;
    s_uac1.sample_rate = UAC1_DEFAULT_RATE;
    s_uac1.channels = UAC1_DEFAULT_CHANNELS;
    s_uac1.write_packet_idx = 0;
    s_uac1.queued_packets = 0;

    for (uint32_t p = 0; p < UAC1_RING_PACKETS; p++) {
        for (uint32_t s = 0; s < UAC1_FRAME_SAMPLES_1MS * UAC1_DEFAULT_CHANNELS; s++) {
            s_uac1_pcm_ring[p][s] = 0;
        }
    }
}

void usb_audio_uac1_attach(uint32_t slot_id, uint8_t ep_addr, uint32_t max_packet_size) {
    if (slot_id == 0 || slot_id > XHCI_MAX_SLOTS) return;

    s_uac1.slot_id = slot_id;
    s_uac1.ep_addr = ep_addr;
    s_uac1.ep_mps = (max_packet_size > 0) ? max_packet_size : UAC1_FRAME_BYTES_1MS;
    s_uac1.attached = true;
    s_uac1.write_packet_idx = 0;
    s_uac1.queued_packets = 0;

    uart_puts("[UAC1] USB Audio device attached: slot=");
    uart_hex32(slot_id);
    uart_puts(" EP=0x");
    uart_hex32(ep_addr);
    uart_puts(" MPS=");
    uart_hex32(s_uac1.ep_mps);
    uart_puts("\n");
}

void usb_audio_uac1_detach(uint32_t slot_id) {
    if (s_uac1.slot_id == slot_id) {
        usb_audio_uac1_close();
        s_uac1.attached = false;
        s_uac1.slot_id = 0;
        s_uac1.ep_addr = 0;
        uart_puts("[UAC1] USB Audio device detached.\n");
    }
}

bool usb_audio_uac1_is_present(void) {
    return s_uac1.attached;
}

/* Callback from xHCI transfer event handler when an isochronous TRB completes */
void usb_audio_uac1_on_transfer_complete(void) {
    if (s_uac1.queued_packets > 0) {
        s_uac1.queued_packets--;
    }
}

/* ── Public B-Audio 5-Call Interface ──────────────────────────────── */

int usb_audio_uac1_open(uint32_t sample_rate, uint8_t channels) {
    if (!s_uac1.attached) return -1;
    BT_ASSERT(channels >= 1 && channels <= 2);

    if (s_uac1.opened) {
        usb_audio_uac1_close();
    }

    s_uac1.sample_rate = sample_rate ? sample_rate : UAC1_DEFAULT_RATE;
    s_uac1.channels = channels;
    s_uac1.write_packet_idx = 0;
    s_uac1.queued_packets = 0;
    s_uac1.opened = true;

    uart_puts("[UAC1] Opened stream: 48,000 Hz / 16-bit Stereo PCM (Isochronous 1 ms)\n");
    return 0;
}

int usb_audio_uac1_write(const int16_t *samples, size_t frames) {
    BT_ASSERT(samples != NULL);
    BT_ASSERT(frames > 0);

    if (!s_uac1.opened || !s_uac1.attached) return -1;

    size_t written_frames = 0;
    while (written_frames < frames) {
        size_t chunk_frames = frames - written_frames;
        if (chunk_frames > UAC1_FRAME_SAMPLES_1MS) {
            chunk_frames = UAC1_FRAME_SAMPLES_1MS;
        }

        uint32_t p = s_uac1.write_packet_idx;
        int16_t *dst = &s_uac1_pcm_ring[p][0];

        if (s_uac1.channels == 2) {
            for (size_t i = 0; i < chunk_frames * 2; i++) {
                dst[i] = samples[(written_frames * 2) + i];
            }
            /* Pad remaining samples in the 1 ms frame if partial */
            for (size_t i = chunk_frames * 2; i < UAC1_FRAME_SAMPLES_1MS * 2; i++) {
                dst[i] = 0;
            }
        } else {
            /* Mono to stereo expansion */
            for (size_t i = 0; i < chunk_frames; i++) {
                dst[i * 2 + 0] = samples[written_frames + i];
                dst[i * 2 + 1] = samples[written_frames + i];
            }
            for (size_t i = chunk_frames; i < UAC1_FRAME_SAMPLES_1MS; i++) {
                dst[i * 2 + 0] = 0;
                dst[i * 2 + 1] = 0;
            }
        }

        uint32_t transfer_bytes = UAC1_FRAME_BYTES_1MS;
        int ret = xhci_queue_audio_isoch_transfer(s_uac1.slot_id, s_uac1.ep_addr,
                                                  (uintptr_t)dst, transfer_bytes);
        if (ret == 0) {
            s_uac1.queued_packets++;
        }

        s_uac1.write_packet_idx = (s_uac1.write_packet_idx + 1) % UAC1_RING_PACKETS;
        written_frames += chunk_frames;
    }

    return (int)frames;
}

void usb_audio_uac1_close(void) {
    s_uac1.opened = false;
    s_uac1.queued_packets = 0;
    s_uac1.write_packet_idx = 0;
}

bool usb_audio_uac1_is_ready(void) {
    return s_uac1.opened && s_uac1.attached;
}

uint32_t usb_audio_uac1_get_queued_bytes(void) {
    if (!s_uac1.opened) return 0;
    return s_uac1.queued_packets * UAC1_FRAME_BYTES_1MS;
}

const int16_t *usb_audio_uac1_get_packet_buffer(uint32_t packet_idx) {
    if (packet_idx >= UAC1_RING_PACKETS) return NULL;
    return &s_uac1_pcm_ring[packet_idx][0];
}
