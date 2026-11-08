/*
 * B-BT: Standard B-Audio Driver Implementation for Bluetooth A2DP (src/drivers/bluetooth/b_bt_audio.c)
 * Conforming to B-Audio specification & NASA JPL Rules
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <drivers/bluetooth/b_bt_audio.h>
#include <drivers/bluetooth/b_bt_sbc.h>
#include <drivers/bluetooth/b_bt_avdtp.h>
#include <drivers/bluetooth/b_bt.h>

typedef struct {
    bool              opened;
    uint32_t          sample_rate;
    uint8_t           channels;
    uint32_t          in_flight_bytes;
    bbt_sbc_config_t  sbc_cfg;
} bbt_audio_state_t;

static bbt_audio_state_t s_bt_audio = {0};

/* Coherent static buffer for 128 stereo frames */
static int16_t s_pcm_chunk[SBC_FRAME_SAMPLES * 2] __attribute__((aligned(16)));
static uint8_t s_sbc_frame[SBC_MAX_FRAME_LEN] __attribute__((aligned(16)));

bool b_bt_audio_is_present(void) {
    return bbt_is_streaming();
}

int b_bt_audio_open(uint32_t sample_rate, uint8_t channels) {
    BBT_ASSERT(channels >= 1 && channels <= 2);

    s_bt_audio.sample_rate = sample_rate ? sample_rate : 48000u;
    s_bt_audio.channels = channels;
    s_bt_audio.in_flight_bytes = 0;

    s_bt_audio.sbc_cfg.frequency = SBC_FREQ_48K;
    s_bt_audio.sbc_cfg.blocks = SBC_BLOCKS_16;
    s_bt_audio.sbc_cfg.channel_mode = (channels == 2) ? SBC_MODE_JOINT_STEREO : SBC_MODE_MONO;
    s_bt_audio.sbc_cfg.allocation_method = SBC_ALLOC_LOUDNESS;
    s_bt_audio.sbc_cfg.subbands = SBC_SUBBANDS_8;
    s_bt_audio.sbc_cfg.bitpool = SBC_DEFAULT_BITPOOL;

    s_bt_audio.opened = true;
    return 0;
}

int b_bt_audio_write(const int16_t *samples, size_t frames) {
    BBT_ASSERT(samples != NULL);
    BBT_ASSERT(frames > 0);
    if (!s_bt_audio.opened) return -1;

    size_t written = 0;
    while (written < frames) {
        size_t chunk = frames - written;
        if (chunk > SBC_FRAME_SAMPLES) {
            chunk = SBC_FRAME_SAMPLES;
        }

        /* Pack chunk into s_pcm_chunk with mono expansion & zero padding if needed */
        if (s_bt_audio.channels == 2) {
            for (size_t i = 0; i < chunk * 2; i++) {
                s_pcm_chunk[i] = samples[(written * 2) + i];
            }
            for (size_t i = chunk * 2; i < SBC_FRAME_SAMPLES * 2; i++) {
                s_pcm_chunk[i] = 0;
            }
        } else {
            for (size_t i = 0; i < chunk; i++) {
                s_pcm_chunk[i * 2 + 0] = samples[written + i];
                s_pcm_chunk[i * 2 + 1] = samples[written + i];
            }
            for (size_t i = chunk; i < SBC_FRAME_SAMPLES; i++) {
                s_pcm_chunk[i * 2 + 0] = 0;
                s_pcm_chunk[i * 2 + 1] = 0;
            }
        }

        /* Encode 128 stereo PCM samples to SBC frame */
        int sbc_len = bbt_sbc_encode_frame(s_pcm_chunk, s_sbc_frame, sizeof(s_sbc_frame), &s_bt_audio.sbc_cfg);
        if (sbc_len > 0) {
            /* Transmit over AVDTP Media Channel (Handle 0x0040, Media CID 0x0042) */
            bbt_avdtp_send_media_frame(0x0040, 0x0042, s_sbc_frame, (uint16_t)sbc_len, SBC_FRAME_SAMPLES);
            s_bt_audio.in_flight_bytes = (uint32_t)(chunk * s_bt_audio.channels * sizeof(int16_t));
        }

        written += chunk;
    }

    return (int)frames;
}

void b_bt_audio_close(void) {
    s_bt_audio.opened = false;
    s_bt_audio.in_flight_bytes = 0;
}

bool b_bt_audio_is_ready(void) {
    return s_bt_audio.opened && bbt_is_streaming();
}

uint32_t b_bt_audio_get_queued_bytes(void) {
    if (!s_bt_audio.opened) return 0;
    return s_bt_audio.in_flight_bytes;
}

