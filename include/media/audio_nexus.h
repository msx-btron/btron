/*
 * B-System (BTRON 3.20) — include/media/audio_nexus.h
 * InterCore Nexus SPSC Lock-Free Audio Tract (POSIX · ARM64 · RP2040/RP2350 Pico)
 *
 * Implements the Zero-Switch Actor Topology (KERNEL-NEXUS.txt / nexus_ring.h):
 *   1. AudioNexusCmdRing  (UI/App Core -> Audio Actor, 32 slots, non-blocking snd)
 *   2. AudioNexusPcmRing  (Audio Actor -> Hardware Sink/DMA, 8 x 256 frames)
 *   3. AudioNexusTelem    (Audio Actor -> UI/Visualizer, wait-free seqlock)
 *
 * Pure C99, zero heap allocation, zero mutexes, NASA/JPL Power of Ten.
 */

#ifndef _BTRON_MEDIA_AUDIO_NEXUS_H_
#define _BTRON_MEDIA_AUDIO_NEXUS_H_

#include <btron/nexus_ring.h>
#include <rt/fix16.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_NEXUS_CHUNK_FRAMES   256u   /* 5.8 ms @ 44.1 kHz (8 x 32-sample BSF quanta) */
#define AUDIO_NEXUS_PCM_SLOTS      8u     /* 2^3 slots = 46.4 ms ring capacity */
#define AUDIO_NEXUS_CMD_SLOTS      32u    /* 2^5 command slots */
#define AUDIO_NEXUS_TARGET_FRAMES  768u   /* 3 chunks = 17.4 ms low-latency watermark */
#define AUDIO_NEXUS_TASK_PERIOD_MS 4u     /* 4 ms async TASK_AUDIO wake cadence */

typedef enum {
    AUDIO_NEXUS_ENGINE_NONE    = 0,
    AUDIO_NEXUS_ENGINE_BSF     = 1,
    AUDIO_NEXUS_ENGINE_LILCU64 = 2,
    AUDIO_NEXUS_ENGINE_ORCH    = 3
} AudioNexusEngine;

typedef enum {
    AUDIO_NEXUS_CMD_NOP = 0,
    AUDIO_NEXUS_CMD_BSF_PLAY,
    AUDIO_NEXUS_CMD_BSF_PAUSE,
    AUDIO_NEXUS_CMD_BSF_STOP,
    AUDIO_NEXUS_CMD_BSF_MUTE,
    AUDIO_NEXUS_CMD_BSF_TRACK,   /* arg0=track_idx, arg1=part_idx, arg2=auto_play */
    AUDIO_NEXUS_CMD_BSF_PART,    /* arg0=part_idx */
    AUDIO_NEXUS_CMD_BSF_SCRUB,   /* arg0=fix16_t x_pos */
    AUDIO_NEXUS_CMD_BSF_RELOAD,
    AUDIO_NEXUS_CMD_LC_PLAY,     /* arg0=track_num (1..4) */
    AUDIO_NEXUS_CMD_LC_STOP,
    AUDIO_NEXUS_CMD_LC_MUTE,
    AUDIO_NEXUS_CMD_ORCH_PLAY,
    AUDIO_NEXUS_CMD_ORCH_STOP,
    AUDIO_NEXUS_CMD_FLUSH
} AudioNexusCmdType;

typedef struct {
    uint16_t type;
    uint16_t flags;
    int32_t  arg0;
    int32_t  arg1;
    int32_t  arg2;
} AudioNexusCmd;

typedef struct {
    uint16_t frames;
    uint16_t engine;
    int16_t  pcm[AUDIO_NEXUS_CHUNK_FRAMES * 2u];
} AudioNexusPcmChunk;

NEXUS_RING_DEFINE(AudioNexusCmdRing, AudioNexusCmd, AUDIO_NEXUS_CMD_SLOTS)
NEXUS_RING_DEFINE(AudioNexusPcmRing, AudioNexusPcmChunk, AUDIO_NEXUS_PCM_SLOTS)

/* Wait-free seqlock telemetry snapshot published by Audio Actor */
typedef struct {
    uint8_t  active_engine;      /* AudioNexusEngine */
    uint8_t  playing;
    uint8_t  muted;
    uint8_t  limiter_hit;
    int32_t  track_idx;
    int32_t  part_idx;
    fix16_t  bsf_x_heard;        /* Presentation-compensated BSF cursor */
    float    lc_time_heard;      /* Presentation-compensated LilCu64 time (sec) */
    uint32_t orch_tick_heard;    /* Presentation-compensated Orchestra/SMF tick */
    uint32_t orch_loop_tick;     /* Total Orchestra/SMF loop length in ticks */
    float    peak_l;
    float    peak_r;
    float    rms_l;
    float    rms_r;
    uint32_t wcet_us;
    uint32_t ring_queued_frames;
    uint32_t cmd_refused;
    uint32_t pcm_refused;
    uint64_t sample_counter;
} AudioNexusTelem;

/* Lifecycle & async task management */
int  audio_nexus_init(uint32_t sample_rate);
int  audio_nexus_start_task(void);
void audio_nexus_ensure_started(void);
void audio_nexus_stop_task(void);
bool audio_nexus_is_async(void);

/* Exclusive engine handoff: stops all engines other than `keep` and flushes HW queue */
void audio_nexus_stop_all_except(AudioNexusEngine keep);

/* Non-blocking UI -> Audio Actor command post (0 = ok, -1 = refused/full) */
int  audio_nexus_post_cmd(const AudioNexusCmd *cmd);

/* Bounded single step for TASK_AUDIO, ARM64 Core 2/3, or Pico Core 1 */
int  audio_nexus_core_step(void);

/* Enable/disable external DMA pull mode (true on Pico PIO I2S/PWM DMA) */
void audio_nexus_set_pull_sink(bool enabled);

/* Consumer API for hardware DMA ISR / pull callback (returns frames copied) */
size_t audio_nexus_pull_pcm(int16_t *out_pcm, size_t max_frames);

/* Wait-free bounded telemetry snapshot read (returns true if valid) */
bool audio_nexus_read_telem(AudioNexusTelem *out);

/* Lightweight atomic guard protecting shared BSF VM between preview & async DSP */
void audio_nexus_vm_lock(void);
void audio_nexus_vm_unlock(void);

/* Pico Core 1 direct poll hook (called from pico_core1_body sev/wfe loop) */
void audio_nexus_pico_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_MEDIA_AUDIO_NEXUS_H_ */
