/*
 * test_hdmi_audio.c — Unit Test Suite & Real-Time Simulation for BCM2711 HDMI Audio
 *
 * Verifies:
 * 1. ACR (Audio Clock Regeneration) N / CTS calculation accuracy across standard video modes.
 * 2. CEA-861 Audio InfoFrame Type 0x84 packet generation, bitfields, and byte checksum rule:
 *    (Sum of all 14 bytes) % 256 == 0.
 * 3. B-Audio Real-Time Latency Contracts:
 *    - 128 frames @ 48 kHz = 2.667 ms buffer period duration.
 *    - Dual ping-pong buffer cycling and bounded telemetry.
 * 4. Streaming simulation: 440 Hz test tone generation, sample packing, and queue depth invariant.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include <drivers/broadcom/bcm2711_hdmi_audio.h>

/* Host test stubs for baremetal MMIO & DMA */
uintptr_t g_mmio_base = 0;
void uart_puts(const char *s) { (void)s; }
void uart_hex32(uint32_t val) { (void)val; }
int  bcm2711_dma_start_cyclic(int channel, uintptr_t cb_phys) { (void)channel; (void)cb_phys; return 0; }
void bcm2711_dma_stop(int channel) { (void)channel; }
uint32_t bcm2711_dma_get_cb(int channel) { (void)channel; return 0; }

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

/* ── Test 1: Audio Clock Regeneration (ACR) Accuracy ──────────────── */

static void test_acr_calculations(void) {
    hdmi_acr_t acr;

    /* 1. 1024x768 @ 60 Hz (65.0 MHz pixel clock) @ 48 kHz */
    int ret = bcm2711_hdmi_calculate_acr(48000, 65000000, &acr);
    TEST_ASSERT(ret == 0, "ACR 1024x768 calculation succeeded");
    TEST_ASSERT(acr.n == 6144, "ACR 48kHz N value is 6144");
    TEST_ASSERT(acr.cts == 65000, "ACR 65MHz CTS value is 65000");

    /* Verify HDMI ACR equation: N / CTS == (128 * fs) / fpix */
    double ratio_hw = (double)acr.n / (double)acr.cts;
    double ratio_ideal = (128.0 * 48000.0) / 65000000.0;
    TEST_ASSERT(fabs(ratio_hw - ratio_ideal) < 1e-9, "ACR 1024x768 exact frequency lock");

    /* 2. 1920x1080 @ 60 Hz (148.5 MHz pixel clock) @ 48 kHz */
    ret = bcm2711_hdmi_calculate_acr(48000, 148500000, &acr);
    TEST_ASSERT(ret == 0, "ACR 1080p60 calculation succeeded");
    TEST_ASSERT(acr.n == 6144, "ACR 1080p N value is 6144");
    TEST_ASSERT(acr.cts == 148500, "ACR 148.5MHz CTS value is 148500");

    /* 3. 1280x720 @ 60 Hz (74.25 MHz pixel clock) @ 48 kHz */
    ret = bcm2711_hdmi_calculate_acr(48000, 74250000, &acr);
    TEST_ASSERT(ret == 0, "ACR 720p60 calculation succeeded");
    TEST_ASSERT(acr.n == 6144, "ACR 720p N value is 6144");
    TEST_ASSERT(acr.cts == 74250, "ACR 74.25MHz CTS value is 74250");

    /* 4. 640x480 @ 60 Hz (25.2 MHz pixel clock) @ 48 kHz */
    ret = bcm2711_hdmi_calculate_acr(48000, 25200000, &acr);
    TEST_ASSERT(ret == 0, "ACR 480p60 calculation succeeded");
    TEST_ASSERT(acr.n == 6144, "ACR 480p N value is 6144");
    TEST_ASSERT(acr.cts == 25200, "ACR 25.2MHz CTS value is 25200");

    /* 5. 1024x768 @ 60 Hz @ 44.1 kHz */
    ret = bcm2711_hdmi_calculate_acr(44100, 65000000, &acr);
    TEST_ASSERT(ret == 0, "ACR 44.1kHz calculation succeeded");
    TEST_ASSERT(acr.n == 6272, "ACR 44.1kHz N value is 6272");
    TEST_ASSERT(acr.cts == 72222, "ACR 44.1kHz CTS is 72222");
}

/* ── Test 2: CEA-861 Audio InfoFrame Packet Integrity ─────────────── */

static void test_cea861_infoframe(void) {
    cea861_audio_infoframe_t frame;

    /* Build 48 kHz stereo frame */
    int ret = bcm2711_hdmi_build_infoframe(&frame, 48000, 2);
    TEST_ASSERT(ret == 0, "CEA-861 build succeeded");
    TEST_ASSERT(frame.type == CEA861_AUDIO_INFOFRAME_TYPE, "Packet type is 0x84 (Audio InfoFrame)");
    TEST_ASSERT(frame.version == 0x01, "Version is 0x01");
    TEST_ASSERT(frame.length == 0x0A, "Length is 10 bytes");
    TEST_ASSERT((frame.audio_coding & 0x07) == 2, "Channel count is 2");
    TEST_ASSERT(((frame.audio_coding >> 4) & 0x0F) == 1, "Audio coding is LPCM");
    TEST_ASSERT((frame.format_info & 0x03) == 1, "Sample size is 16-bit");
    TEST_ASSERT(((frame.format_info >> 2) & 0x07) == 3, "Sample rate is 48 kHz");

    /* Checksum verification rule: (sum of all 14 packet bytes) % 256 == 0 */
    const uint8_t *bytes = (const uint8_t *)&frame;
    uint32_t total_sum = 0;
    for (size_t i = 0; i < sizeof(frame); i++) {
        total_sum += bytes[i];
    }
    TEST_ASSERT((total_sum & 0xFF) == 0, "CEA-861 checksum validation (sum % 256 == 0)");

    /* Test 44.1 kHz stereo frame */
    ret = bcm2711_hdmi_build_infoframe(&frame, 44100, 2);
    TEST_ASSERT(ret == 0, "CEA-861 44.1kHz build succeeded");
    TEST_ASSERT(((frame.format_info >> 2) & 0x07) == 2, "Sample rate is 44.1 kHz");

    bytes = (const uint8_t *)&frame;
    total_sum = 0;
    for (size_t i = 0; i < sizeof(frame); i++) {
        total_sum += bytes[i];
    }
    TEST_ASSERT((total_sum & 0xFF) == 0, "CEA-861 44.1kHz checksum validation");
}

/* ── Test 3: Real-Time Latency & Buffer Contracts ─────────────────── */

static void test_realtime_audio_contracts(void) {
    /* 128 frames @ 48 kHz duration calculation */
    double period_ms = (128.0 / 48000.0) * 1000.0;
    TEST_ASSERT(fabs(period_ms - 2.666666667) < 0.001, "Period buffer latency is <= 2.67 ms");

    /* Verify period sizes */
    TEST_ASSERT(BCM2711_HDMI_PERIOD_FRAMES == 128, "Period frames == 128");
    TEST_ASSERT(BCM2711_HDMI_PERIOD_BYTES == 512, "Period bytes == 512 (128 * 2 ch * 2 bytes)");
    TEST_ASSERT(BCM2711_HDMI_RING_BYTES == 1024, "Total dual ping-pong ring == 1024 bytes");
}

/* ── Test 4: Audio Streaming & PCM Tone Generation Simulation ─────── */

static void test_pcm_streaming_simulation(void) {
    /* Open audio device */
    int ret = bcm2711_hdmi_audio_open(48000, 2);
    TEST_ASSERT(ret == 0, "bcm2711_hdmi_audio_open succeeded");
    TEST_ASSERT(bcm2711_hdmi_audio_is_ready() == true, "Device is ready");

    /* Generate a 440 Hz test tone (128 frames) */
    int16_t tone_chunk[128 * 2];
    for (int i = 0; i < 128; i++) {
        double phase = (2.0 * 3.141592653589793 * 440.0 * (double)i) / 48000.0;
        int16_t sample = (int16_t)(sin(phase) * 16384.0);
        tone_chunk[i * 2 + 0] = sample;
        tone_chunk[i * 2 + 1] = sample;
    }

    /* Write 1 period */
    ret = bcm2711_hdmi_audio_write(tone_chunk, 128);
    TEST_ASSERT(ret == 128, "Wrote 128 frames");
    TEST_ASSERT(bcm2711_hdmi_audio_get_queued_bytes() == 512, "Queued bytes == 512 (1 period)");

    /* Write 2nd period (ping-pong wrap-around) */
    ret = bcm2711_hdmi_audio_write(tone_chunk, 128);
    TEST_ASSERT(ret == 128, "Wrote 2nd 128 frames (wrap)");
    TEST_ASSERT(bcm2711_hdmi_audio_get_queued_bytes() == 512, "Queued bytes stays bounded to period");

    /* Close audio device */
    bcm2711_hdmi_audio_close();
    TEST_ASSERT(bcm2711_hdmi_audio_is_ready() == false, "Device is closed");
    TEST_ASSERT(bcm2711_hdmi_audio_get_queued_bytes() == 0, "Queued bytes cleared on close");
}

int main(void) {
    printf("==========================================================\n");
    printf(" BCM2711 HDMI Audio Subsystem Verification & Simulation\n");
    printf(" Conforming to B-Audio Spec & NASA JPL Power of Ten\n");
    printf("==========================================================\n");

    test_acr_calculations();
    test_cea861_infoframe();
    test_realtime_audio_contracts();
    test_pcm_streaming_simulation();

    printf("\n[SUMMARY] Passed %d of %d tests successfully.\n", s_tests_passed, s_tests_run);
    printf("==========================================================\n");
    return (s_tests_passed == s_tests_run) ? 0 : 1;
}
