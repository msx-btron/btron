/*
 * test_usb_audio.c — Unit Test Suite for USB Audio Class 1.0 (UAC1) on xHCI
 *
 * Verifies:
 * 1. UAC1 frame math and latency constraints:
 *    - 48 kHz stereo 16-bit = 192 bytes per 1 ms microframe.
 *    - 4 packets = 4.0 ms real-time latency target.
 *    - 16 packets = 16 ms total queue depth.
 * 2. Descriptor parser simulation:
 *    - Synthetic USB config descriptor containing AudioControl, AudioStreaming Alt 0/1,
 *      and Isochronous OUT endpoint (192 bytes MPS, interval 1).
 *    - Accurate detection of USB_CLASS_AUDIO and USB_SUBCLASS_AUDIOSTREAMING.
 * 3. B-Audio standard 5-call lifecycle:
 *    - init, attach, open, write, close, detach.
 *    - Sample packing and ring buffer wrap-around modulo 16.
 *    - Mono-to-stereo channel expansion.
 *    - Transfer completion callbacks and queued bytes accounting.
 * 4. NASA JPL Safety Rules:
 *    - Zero dynamic allocations, bounded loops, strict state invariants.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include <drivers/broadcom/usb_audio_uac1.h>
#include <drivers/broadcom/xhci.h>

/* Mock MMIO & serial output for host unit testing */
void uart_puts(const char *s) { (void)s; }
void uart_hex32(uint32_t val) { (void)val; }

/* Mock xHCI audio queueing for host unit test */
static uint32_t s_mock_queued_transfers = 0;
static uint32_t s_mock_last_length = 0;
static uintptr_t s_mock_last_phys = 0;

int xhci_queue_audio_isoch_transfer(uint32_t slot_id, uint8_t ep_addr, uintptr_t buf_phys, uint32_t length) {
    (void)slot_id;
    (void)ep_addr;
    s_mock_queued_transfers++;
    s_mock_last_phys = buf_phys;
    s_mock_last_length = length;
    return 0;
}

static int s_tests_run = 0;
static int s_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    s_tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        assert(cond); \
    } else { \
        s_tests_passed++; \
    } \
} while(0)

/* ── Test 1: UAC1 Frame Timing and Latency Math ───────────────────── */

static void test_uac1_timing_contracts(void) {
    /* 48 kHz stereo 16-bit: 48 frames per 1 ms frame */
    uint32_t samples_per_frame = UAC1_DEFAULT_RATE / 1000u;
    TEST_ASSERT(samples_per_frame == 48u, "48 kHz produces 48 samples per 1 ms");
    TEST_ASSERT(UAC1_FRAME_SAMPLES_1MS == 48u, "UAC1_FRAME_SAMPLES_1MS is 48");

    /* 48 samples * 2 channels * 2 bytes/sample = 192 bytes */
    uint32_t bytes_per_ms = samples_per_frame * UAC1_DEFAULT_CHANNELS * sizeof(int16_t);
    TEST_ASSERT(bytes_per_ms == 192u, "48 kHz 16-bit stereo frame is 192 bytes");
    TEST_ASSERT(UAC1_FRAME_BYTES_1MS == 192u, "UAC1_FRAME_BYTES_1MS matches 192 bytes");

    /* Latency target: 4 packets = 4.0 ms <= 4.0 ms */
    double latency_target_ms = (double)UAC1_TARGET_LATENCY_PACKETS * 1.0;
    TEST_ASSERT(latency_target_ms <= 4.0, "UAC1 target latency is <= 4.0 ms");

    /* Buffer capacity: 16 packets = 16.0 ms */
    double total_ring_ms = (double)UAC1_RING_PACKETS * 1.0;
    TEST_ASSERT(total_ring_ms == 16.0, "Total UAC1 queue depth is 16.0 ms");
}

/* ── Test 2: Synthetic UAC1 Descriptor Parsing ────────────────────── */

static void test_uac1_descriptor_parsing(void) {
    /* Build a synthetic USB configuration descriptor representing a standard UAC1 DAC */
    uint8_t cfg_buf[256];
    memset(cfg_buf, 0, sizeof(cfg_buf));

    uint32_t off = 0;

    /* 1. Configuration Descriptor (9 bytes) */
    cfg_buf[off + 0] = 9;                       /* bLength */
    cfg_buf[off + 1] = 0x02;                    /* USB_DT_CONFIGURATION */
    cfg_buf[off + 2] = 0;                       /* wTotalLength (set later) */
    cfg_buf[off + 3] = 0;
    cfg_buf[off + 4] = 2;                       /* bNumInterfaces (Control + Streaming) */
    cfg_buf[off + 5] = 1;                       /* bConfigurationValue */
    cfg_buf[off + 6] = 0;                       /* iConfiguration */
    cfg_buf[off + 7] = 0x80;                    /* bmAttributes (Bus Powered) */
    cfg_buf[off + 8] = 50;                      /* bMaxPower (100 mA) */
    off += 9;

    /* 2. Audio Control Interface Descriptor (Interface 0, Alt 0, 9 bytes) */
    cfg_buf[off + 0] = 9;
    cfg_buf[off + 1] = 0x04;                    /* USB_DT_INTERFACE */
    cfg_buf[off + 2] = 0;                       /* bInterfaceNumber = 0 */
    cfg_buf[off + 3] = 0;                       /* bAlternateSetting = 0 */
    cfg_buf[off + 4] = 0;                       /* bNumEndpoints = 0 */
    cfg_buf[off + 5] = USB_CLASS_AUDIO;         /* 0x01 */
    cfg_buf[off + 6] = USB_SUBCLASS_AUDIOCONTROL; /* 0x01 */
    cfg_buf[off + 7] = 0;                       /* bInterfaceProtocol */
    cfg_buf[off + 8] = 0;
    off += 9;

    /* 3. Audio Streaming Interface Descriptor (Interface 1, Alt 0, Zero Bandwidth, 9 bytes) */
    cfg_buf[off + 0] = 9;
    cfg_buf[off + 1] = 0x04;                    /* USB_DT_INTERFACE */
    cfg_buf[off + 2] = 1;                       /* bInterfaceNumber = 1 */
    cfg_buf[off + 3] = 0;                       /* bAlternateSetting = 0 */
    cfg_buf[off + 4] = 0;                       /* bNumEndpoints = 0 */
    cfg_buf[off + 5] = USB_CLASS_AUDIO;         /* 0x01 */
    cfg_buf[off + 6] = USB_SUBCLASS_AUDIOSTREAMING; /* 0x02 */
    cfg_buf[off + 7] = 0;
    cfg_buf[off + 8] = 0;
    off += 9;

    /* 4. Audio Streaming Interface Descriptor (Interface 1, Alt 1, Operational Streaming, 9 bytes) */
    cfg_buf[off + 0] = 9;
    cfg_buf[off + 1] = 0x04;                    /* USB_DT_INTERFACE */
    cfg_buf[off + 2] = 1;                       /* bInterfaceNumber = 1 */
    cfg_buf[off + 3] = 1;                       /* bAlternateSetting = 1 */
    cfg_buf[off + 4] = 1;                       /* bNumEndpoints = 1 */
    cfg_buf[off + 5] = USB_CLASS_AUDIO;         /* 0x01 */
    cfg_buf[off + 6] = USB_SUBCLASS_AUDIOSTREAMING; /* 0x02 */
    cfg_buf[off + 7] = 0;
    cfg_buf[off + 8] = 0;
    off += 9;

    /* 5. Audio Streaming Format Type I Descriptor (11 bytes) */
    cfg_buf[off + 0] = 11;
    cfg_buf[off + 1] = 0x24;                    /* CS_INTERFACE */
    cfg_buf[off + 2] = 0x02;                    /* FORMAT_TYPE */
    cfg_buf[off + 3] = 0x01;                    /* FORMAT_TYPE_I */
    cfg_buf[off + 4] = 2;                       /* bNrChannels = 2 */
    cfg_buf[off + 5] = 2;                       /* bSubFrameSize = 2 */
    cfg_buf[off + 6] = 16;                      /* bBitResolution = 16 */
    cfg_buf[off + 7] = 1;                       /* bSamFreqType = 1 (Discrete) */
    cfg_buf[off + 8] = 0x80;                    /* 48000 Hz = 0x00BB80 (little endian) */
    cfg_buf[off + 9] = 0xBB;
    cfg_buf[off + 10] = 0x00;
    off += 11;

    /* 6. Isochronous OUT Endpoint Descriptor (9 bytes) */
    cfg_buf[off + 0] = 9;
    cfg_buf[off + 1] = 0x05;                    /* USB_DT_ENDPOINT */
    cfg_buf[off + 2] = 0x01;                    /* bEndpointAddress: EP 1 OUT */
    cfg_buf[off + 3] = 0x01;                    /* bmAttributes: Isochronous */
    cfg_buf[off + 4] = 192;                     /* wMaxPacketSize: 192 bytes */
    cfg_buf[off + 5] = 0;
    cfg_buf[off + 6] = 1;                       /* bInterval = 1 ms */
    cfg_buf[off + 7] = 0;                       /* bRefresh */
    cfg_buf[off + 8] = 0;                       /* bSynchAddress */
    off += 9;

    uint32_t tot_len = off;
    cfg_buf[2] = (uint8_t)(tot_len & 0xFF);
    cfg_buf[3] = (uint8_t)((tot_len >> 8) & 0xFF);

    /* Run parser logic exactly as implemented in xhci.c */
    bool is_audio = false;
    uint8_t audio_if_num = 0;
    uint8_t audio_ep_addr = 0;
    uint32_t audio_ep_mps = 0;
    uint32_t audio_ep_interval = 1;
    uint8_t cur_if_class = 0;
    uint8_t cur_if_subclass = 0;

    for (uint32_t p = 0; p + 2 <= tot_len; ) {
        uint8_t len = cfg_buf[p];
        uint8_t type = cfg_buf[p + 1];
        if (len < 2 || p + len > tot_len) break;

        if (type == 0x04 && len >= 9) {
            uint8_t if_num = cfg_buf[p + 2];
            cur_if_class = cfg_buf[p + 5];
            cur_if_subclass = cfg_buf[p + 6];
            if (cur_if_class == USB_CLASS_AUDIO && cur_if_subclass == USB_SUBCLASS_AUDIOSTREAMING) {
                is_audio = true;
                audio_if_num = if_num;
            }
        } else if (type == 0x05 && len >= 7) {
            uint8_t ep_addr = cfg_buf[p + 2];
            uint8_t ep_attr = cfg_buf[p + 3];
            uint32_t mps = cfg_buf[p + 4] | ((uint32_t)cfg_buf[p + 5] << 8);
            uint8_t interval = cfg_buf[p + 6];

            if (is_audio && !(ep_addr & 0x80) && ((ep_attr & 0x03) == 0x01)) {
                audio_ep_addr = ep_addr;
                audio_ep_mps = mps;
                audio_ep_interval = interval;
            }
        }
        p += len;
    }

    TEST_ASSERT(is_audio == true, "Synthetic descriptor detected as Audio Class");
    TEST_ASSERT(audio_if_num == 1, "AudioStreaming interface identified as interface 1");
    TEST_ASSERT(audio_ep_addr == 0x01, "Audio Isochronous endpoint address is 0x01 OUT");
    TEST_ASSERT(audio_ep_mps == 192, "Audio endpoint MaxPacketSize is 192 bytes");
    TEST_ASSERT(audio_ep_interval == 1, "Audio endpoint interval is 1 ms");
}

/* ── Test 3: UAC1 B-Audio Lifecycle and 5-Call Interface ─────────── */

static void test_uac1_driver_lifecycle(void) {
    usb_audio_uac1_init();
    TEST_ASSERT(!usb_audio_uac1_is_present(), "Initially not present");
    TEST_ASSERT(!usb_audio_uac1_is_ready(), "Initially not ready");
    TEST_ASSERT(usb_audio_uac1_get_queued_bytes() == 0, "Initially 0 queued bytes");

    /* Negative test: write before attach/open */
    int16_t dummy[96] = {0};
    int ret = usb_audio_uac1_write(dummy, 48);
    TEST_ASSERT(ret == -1, "Write fails when unattached");

    /* Attach DAC */
    usb_audio_uac1_attach(2 /* slot 2 */, 0x01 /* EP 1 OUT */, 192 /* MPS */);
    TEST_ASSERT(usb_audio_uac1_is_present(), "Present after attach");
    TEST_ASSERT(!usb_audio_uac1_is_ready(), "Not ready before open");

    /* Open audio stream: 48 kHz stereo */
    ret = usb_audio_uac1_open(48000, 2);
    TEST_ASSERT(ret == 0, "Open succeeds");
    TEST_ASSERT(usb_audio_uac1_is_ready(), "Ready after open");
    TEST_ASSERT(usb_audio_uac1_get_queued_bytes() == 0, "0 queued bytes initially");

    /* Write exactly 1 ms of stereo audio (48 frames = 96 samples) */
    int16_t test_frame_1[96];
    for (int i = 0; i < 96; i++) {
        test_frame_1[i] = (int16_t)(1000 + i);
    }
    s_mock_queued_transfers = 0;

    ret = usb_audio_uac1_write(test_frame_1, 48);
    TEST_ASSERT(ret == 48, "Wrote 48 frames successfully");
    TEST_ASSERT(s_mock_queued_transfers == 1, "Queued 1 xHCI isochronous transfer");
    TEST_ASSERT(s_mock_last_length == 192, "Queued transfer length is exactly 192 bytes");
    TEST_ASSERT(usb_audio_uac1_get_queued_bytes() == 192, "Queued bytes is 192");

    /* Verify memory buffer integrity in ring packet 0 */
    const int16_t *buf0 = usb_audio_uac1_get_packet_buffer(0);
    TEST_ASSERT(buf0 != NULL, "Packet buffer 0 accessible");
    TEST_ASSERT(buf0[0] == 1000, "Buffer sample 0 matches input");
    TEST_ASSERT(buf0[95] == 1095, "Buffer sample 95 matches input");

    /* Transfer completion callback from xHCI event ring */
    usb_audio_uac1_on_transfer_complete();
    TEST_ASSERT(usb_audio_uac1_get_queued_bytes() == 0, "Queued bytes decremented to 0 after completion");

    /* Close and Detach */
    usb_audio_uac1_close();
    TEST_ASSERT(!usb_audio_uac1_is_ready(), "Not ready after close");

    usb_audio_uac1_detach(2);
    TEST_ASSERT(!usb_audio_uac1_is_present(), "Not present after detach");
}

/* ── Test 4: Ring Wrap-Around and Multi-Packet Streaming ──────────── */

static void test_uac1_ring_wraparound(void) {
    usb_audio_uac1_init();
    usb_audio_uac1_attach(1, 0x01, 192);
    usb_audio_uac1_open(48000, 2);

    /* Write 18 consecutive 1 ms frames (exceeding UAC1_RING_PACKETS = 16) */
    int16_t frame[96];
    for (int p = 0; p < 18; p++) {
        for (int i = 0; i < 96; i++) {
            frame[i] = (int16_t)(p * 100 + i);
        }
        int ret = usb_audio_uac1_write(frame, 48);
        TEST_ASSERT(ret == 48, "Frame write succeeds");
    }

    /* Packet 16 wraps to index 0, Packet 17 wraps to index 1 */
    const int16_t *buf0 = usb_audio_uac1_get_packet_buffer(0);
    const int16_t *buf1 = usb_audio_uac1_get_packet_buffer(1);
    TEST_ASSERT(buf0[0] == 1600, "Packet 16 wrapped cleanly into buffer slot 0");
    TEST_ASSERT(buf1[0] == 1700, "Packet 17 wrapped cleanly into buffer slot 1");

    usb_audio_uac1_close();
}

/* ── Test 5: Mono to Stereo Channel Expansion ─────────────────────── */

static void test_uac1_mono_expansion(void) {
    usb_audio_uac1_init();
    usb_audio_uac1_attach(1, 0x01, 192);
    usb_audio_uac1_open(48000, 1 /* Mono */);

    int16_t mono_samples[48];
    for (int i = 0; i < 48; i++) {
        mono_samples[i] = (int16_t)(500 + i);
    }

    int ret = usb_audio_uac1_write(mono_samples, 48);
    TEST_ASSERT(ret == 48, "Wrote 48 mono frames");

    /* Verify expansion in packet buffer: Left and Right should duplicate */
    const int16_t *buf = usb_audio_uac1_get_packet_buffer(0);
    TEST_ASSERT(buf[0] == 500 && buf[1] == 500, "Sample 0 replicated to L & R");
    TEST_ASSERT(buf[94] == 547 && buf[95] == 547, "Sample 47 replicated to L & R");

    usb_audio_uac1_close();
}

/* ── Test 6: Partial Frame Padding ────────────────────────────────── */

static void test_uac1_partial_padding(void) {
    usb_audio_uac1_init();
    usb_audio_uac1_attach(1, 0x01, 192);
    usb_audio_uac1_open(48000, 2);

    /* Write 20 frames (partial, less than 48 samples) */
    int16_t partial[40];
    for (int i = 0; i < 40; i++) partial[i] = 777;

    int ret = usb_audio_uac1_write(partial, 20);
    TEST_ASSERT(ret == 20, "Wrote 20 partial frames");

    const int16_t *buf = usb_audio_uac1_get_packet_buffer(0);
    TEST_ASSERT(buf[0] == 777, "First sample present");
    TEST_ASSERT(buf[39] == 777, "Last active sample present");
    TEST_ASSERT(buf[40] == 0, "Unfilled samples zero-padded");
    TEST_ASSERT(buf[95] == 0, "End of 1 ms frame zero-padded");

    usb_audio_uac1_close();
}

/* ── Main Runner ─────────────────────────────────────────────────── */

int main(void) {
    printf("==========================================================\n");
    printf(" B-Audio USB Audio Class 1.0 (UAC1) Unit Test Suite\n");
    printf("==========================================================\n");

    test_uac1_timing_contracts();
    test_uac1_descriptor_parsing();
    test_uac1_driver_lifecycle();
    test_uac1_ring_wraparound();
    test_uac1_mono_expansion();
    test_uac1_partial_padding();

    printf("\nTest Summary: %d / %d tests passed (100%% Success).\n", s_tests_passed, s_tests_run);
    return (s_tests_passed == s_tests_run) ? 0 : 1;
}

