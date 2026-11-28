/*
 * B-System (BTRON 3.20) — src/media/synth/audio_nexus.c
 * InterCore Nexus SPSC Lock-Free Audio Tract Implementation
 *
 * Decouples DSP synthesis (BSF & LilCu64) from UI/compositor threads via
 * lock-free SPSC Nexus rings (include/btron/nexus_ring.h):
 *   - POSIX:   Dedicated async TASK_AUDIO (cre_tsk -> real pthread, 4 ms period)
 *   - ARM64:   TASK_AUDIO or secondary core (Core 2/3, dmb ish barrier)
 *   - Pico:    Core 1 sev/wfe loop (audio_nexus_pico_poll -> PIO/PWM DMA pull)
 *
 * Pure C99, zero heap allocation, NASA/JPL Power of Ten.
 */

#include <media/audio_nexus.h>
#include <media/orch_rt.h>
#include <btron/bsf.h>
#include <btron/itron.h>
#include "bsound_synth.h"
#include <string.h>

/* Weak Orchestra/SMF & clock hooks so standalone tests link without orch_rt.c */
__attribute__((weak)) OrchView *gst_view(void) { return NULL; }
__attribute__((weak)) void orch_rt_render_frames(int16_t *out_pcm, size_t frames) {
    if (out_pcm && frames) memset(out_pcm, 0, frames * 2u * sizeof(int16_t));
}
__attribute__((weak)) int  gst_rt_set_state(int target) { (void)target; return 0; }
__attribute__((weak)) uint32_t gst_tick_of_sample(uint64_t npos, uint32_t fs, uint32_t bpm, uint32_t den) {
    (void)npos; (void)fs; (void)bpm; (void)den; return 0u;
}
__attribute__((weak)) uint32_t btron_render_perf_us(void) { return 0u; }

/* Weak VirtIO / hardware audio driver hooks */
__attribute__((weak)) int      virtio_sound_open(uint32_t sample_rate, uint8_t channels) {
    (void)sample_rate; (void)channels; return -1;
}
__attribute__((weak)) int      virtio_sound_write(const int16_t *samples, size_t frames) {
    (void)samples; (void)frames; return -1;
}
__attribute__((weak)) bool     virtio_sound_is_ready(void) { return false; }
__attribute__((weak)) uint32_t virtio_sound_get_queued_bytes(void) { return 0; }
__attribute__((weak)) void     virtio_sound_flush(void) {}

/* Weak µITRON task hooks so standalone host tests link without kernel */
__attribute__((weak)) ID   cre_tsk(const T_CTSK *pk_ctsk) { (void)pk_ctsk; return -1; }
__attribute__((weak)) ER   sta_tsk(ID tskid, VW exinf)    { (void)tskid; (void)exinf; return -1; }
__attribute__((weak)) void dly_tsk(W dlytim)              { (void)dlytim; }

/* ── Static Nexus Rings & Wait-Free Telemetry ─────────────────────── */
static AudioNexusCmdRing s_cmd_ring;
static AudioNexusPcmRing s_pcm_ring;

static volatile uint32_t s_telem_seq = 0;
static AudioNexusTelem   s_telem_buf;

static uint32_t          s_sample_rate = BSF_SAMPLE_RATE;
static uint32_t          s_hw_rate     = 0u;
static volatile bool     s_inited      = false;
static volatile bool     s_running     = false;
static volatile bool     s_pull_sink   = false;
static ID                s_audio_tskid = 0;

/* Atomic spin-guard for shared BSF VM (held < 8 us during preview/render) */
static volatile int      s_vm_guard = 0;

void audio_nexus_vm_lock(void) {
    if (!s_running) return;
    for (uint32_t spin = 0; spin < 200000u; spin++) {
        if (!__atomic_test_and_set(&s_vm_guard, __ATOMIC_ACQUIRE)) {
            return;
        }
#if defined(__aarch64__) || defined(__arm__)
        __asm__ volatile("yield" ::: "memory");
#endif
    }
}

void audio_nexus_vm_unlock(void) {
    if (!s_running) return;
    __atomic_clear(&s_vm_guard, __ATOMIC_RELEASE);
}

int audio_nexus_init(uint32_t sample_rate) {
    if (s_inited) return 0;
    s_sample_rate = sample_rate ? sample_rate : BSF_SAMPLE_RATE;
    memset(&s_cmd_ring, 0, sizeof(s_cmd_ring));
    memset(&s_pcm_ring, 0, sizeof(s_pcm_ring));
    memset(&s_telem_buf, 0, sizeof(s_telem_buf));
    s_telem_seq = 0;
    s_inited = true;
    return 0;
}

bool audio_nexus_is_async(void) {
    return s_running;
}

void audio_nexus_set_pull_sink(bool enabled) {
    s_pull_sink = enabled;
}

int audio_nexus_post_cmd(const AudioNexusCmd *cmd) {
    if (!cmd) return -1;
    if (!s_inited) (void)audio_nexus_init(BSF_SAMPLE_RATE);
    return AudioNexusCmdRing_snd(&s_cmd_ring, cmd);
}

void audio_nexus_stop_all_except(AudioNexusEngine keep) {
    if (keep != AUDIO_NEXUS_ENGINE_BSF && bsf_is_playing()) {
        bsf_pause();
    }
    if (keep != AUDIO_NEXUS_ENGINE_LILCU64 && lilcu64_synth_get_active_track() > 0) {
        lilcu64_synth_stop_track();
    }
    if (keep != AUDIO_NEXUS_ENGINE_ORCH) {
        OrchView *ov = gst_view();
        if (ov && ov->state == ORCH_GST_PLAYING) {
            (void)gst_rt_set_state(ORCH_GST_PAUSED);
        }
    }
    {
        AudioNexusPcmChunk drop;
        for (int i = 0; i < (int)AUDIO_NEXUS_PCM_SLOTS; i++) {
            if (!AudioNexusPcmRing_rcv(&s_pcm_ring, &drop)) break;
        }
    }
    virtio_sound_flush();
}

void audio_nexus_ensure_started(void) {
    if (!s_inited) (void)audio_nexus_init(BSF_SAMPLE_RATE);
    if (!s_running) (void)audio_nexus_start_task();
}

static void nexus_exec_cmd(const AudioNexusCmd *c) {
    switch (c->type) {
        case AUDIO_NEXUS_CMD_BSF_PLAY:
            audio_nexus_stop_all_except(AUDIO_NEXUS_ENGINE_BSF);
            bsf_play();
            break;
        case AUDIO_NEXUS_CMD_BSF_PAUSE:
            bsf_pause();
            break;
        case AUDIO_NEXUS_CMD_BSF_STOP:
            bsf_stop();
            break;
        case AUDIO_NEXUS_CMD_BSF_MUTE:
            bsf_toggle_mute();
            break;
        case AUDIO_NEXUS_CMD_BSF_TRACK:
            audio_nexus_stop_all_except(AUDIO_NEXUS_ENGINE_BSF);
            if (bsf_select_track(c->arg0, c->arg1) == 0 && c->arg2) {
                bsf_play();
            }
            break;
        case AUDIO_NEXUS_CMD_BSF_PART:
            (void)bsf_set_part(c->arg0);
            break;
        case AUDIO_NEXUS_CMD_BSF_SCRUB:
            bsf_set_x((fix16_t)c->arg0);
            break;
        case AUDIO_NEXUS_CMD_BSF_RELOAD:
            (void)bsf_reload();
            break;
        case AUDIO_NEXUS_CMD_LC_PLAY:
            audio_nexus_stop_all_except(AUDIO_NEXUS_ENGINE_LILCU64);
            lilcu64_synth_play_track(c->arg0);
            break;
        case AUDIO_NEXUS_CMD_LC_STOP:
            lilcu64_synth_stop_track();
            break;
        case AUDIO_NEXUS_CMD_LC_MUTE:
            lilcu64_synth_toggle_mute();
            break;
        case AUDIO_NEXUS_CMD_ORCH_PLAY:
            audio_nexus_stop_all_except(AUDIO_NEXUS_ENGINE_ORCH);
            (void)gst_rt_set_state(ORCH_GST_PLAYING);
            break;
        case AUDIO_NEXUS_CMD_ORCH_STOP: {
            OrchView *ov = gst_view();
            if (ov && ov->state == ORCH_GST_PLAYING) {
                (void)gst_rt_set_state(ORCH_GST_PAUSED);
            }
            virtio_sound_flush();
            break;
        }
        case AUDIO_NEXUS_CMD_FLUSH:
            virtio_sound_flush();
            break;
        default:
            break;
    }
}

static void nexus_publish_telem(uint8_t engine, uint32_t queued_frames) {
    AudioNexusTelem snap;
    memset(&snap, 0, sizeof(snap));
    snap.active_engine = engine;
    snap.ring_queued_frames = queued_frames;
    snap.cmd_refused = s_cmd_ring.refused;
    snap.pcm_refused = s_pcm_ring.refused;

    if (engine == AUDIO_NEXUS_ENGINE_BSF) {
        const BsfTelemetry *bt = bsf_get_telemetry();
        if (bt) {
            snap.playing        = bt->playing ? 1u : 0u;
            snap.muted          = bt->muted ? 1u : 0u;
            snap.limiter_hit    = bt->limiter_hit ? 1u : 0u;
            snap.track_idx      = bt->track_idx;
            snap.part_idx       = bt->part_idx;
            snap.peak_l         = bt->peak_l;
            snap.peak_r         = bt->peak_r;
            snap.rms_l          = bt->rms_l;
            snap.rms_r          = bt->rms_r;
            snap.wcet_us        = bt->wcet_us;
            snap.sample_counter = bt->sample_counter;
            /* Presentation-compensated BSF x cursor */
            const BsfTrackInfo *tr = bsf_get_track_info(bt->track_idx);
            fix16_t spd = tr ? tr->speed : fix16_from_int(2);
            float q_sec = (s_sample_rate > 0u) ? ((float)queued_frames / (float)s_sample_rate) : 0.0f;
            fix16_t dx_q = fix16_mul(spd, fix16_from_float(q_sec));
            fix16_t xh = bt->x_pos - dx_q;
            if (xh < fix16_from_int(-8)) xh = fix16_from_int(-8);
            snap.bsf_x_heard = xh;
        }
    } else if (engine == AUDIO_NEXUS_ENGINE_LILCU64) {
        int tr = lilcu64_synth_get_active_track();
        float pk_l = 0.0f, pk_r = 0.0f, rms_l = 0.0f, rms_r = 0.0f;
        bool lim = false;
        lilcu64_synth_get_meters(&pk_l, &pk_r, &rms_l, &rms_r, &lim);
        snap.playing       = (tr > 0) ? 1u : 0u;
        snap.muted         = lilcu64_synth_is_muted() ? 1u : 0u;
        snap.limiter_hit   = lim ? 1u : 0u;
        snap.track_idx     = tr;
        snap.lc_time_heard = lilcu64_synth_get_track_time();
        snap.peak_l        = pk_l;
        snap.peak_r        = pk_r;
        snap.rms_l         = rms_l;
        snap.rms_r         = rms_r;
    } else if (engine == AUDIO_NEXUS_ENGINE_ORCH) {
        OrchView *ov = gst_view();
        if (ov) {
            uint64_t heard_pos = (ov->npos > (uint64_t)queued_frames)
                                 ? (ov->npos - (uint64_t)queued_frames) : 0ull;
            snap.playing         = (ov->state == ORCH_GST_PLAYING) ? 1u : 0u;
            snap.limiter_hit     = (ov->master_lim != 0u) ? 1u : 0u;
            snap.track_idx       = (int32_t)ov->track_count;
            snap.orch_tick_heard = gst_tick_of_sample(heard_pos, ov->sample_rate, ov->tempo, ov->den);
            snap.orch_loop_tick  = ov->loop_tick;
            snap.peak_l          = (float)ov->meter_l * 0.01f;
            snap.peak_r          = (float)ov->meter_r * 0.01f;
            snap.rms_l           = snap.peak_l * 0.707f;
            snap.rms_r           = snap.peak_r * 0.707f;
            snap.wcet_us         = ov->wcet_us;
            snap.sample_counter  = ov->npos;
        }
    }

    /* Seqlock publish (odd -> write -> barrier -> even) */
    uint32_t seq = s_telem_seq;
    s_telem_seq = seq + 1u;
    NEXUS_BARRIER();
    s_telem_buf = snap;
    NEXUS_BARRIER();
    s_telem_seq = seq + 2u;
}

int audio_nexus_core_step(void) {
    if (!s_inited) (void)audio_nexus_init(BSF_SAMPLE_RATE);

    /* 1. Drain up to 8 pending UI commands (bounded JPL P10 loop) */
    AudioNexusCmd cmd;
    for (int i = 0; i < 8; i++) {
        if (!AudioNexusCmdRing_rcv(&s_cmd_ring, &cmd)) break;
        nexus_exec_cmd(&cmd);
    }

    /* 2. Determine active synthesis engine (BSF, LilCu64, or Orchestra/SMF) */
    uint8_t engine = AUDIO_NEXUS_ENGINE_NONE;
    OrchView *ov = gst_view();
    if (bsf_is_playing()) {
        engine = AUDIO_NEXUS_ENGINE_BSF;
    } else if (lilcu64_synth_get_active_track() > 0) {
        engine = AUDIO_NEXUS_ENGINE_LILCU64;
    } else if (ov && ov->state == ORCH_GST_PLAYING) {
        engine = AUDIO_NEXUS_ENGINE_ORCH;
    }

    uint32_t hw_frames = s_pull_sink ? 0u : (virtio_sound_get_queued_bytes() / 4u);
    uint32_t ring_frames = AudioNexusPcmRing_len(&s_pcm_ring) * AUDIO_NEXUS_CHUNK_FRAMES;
    uint32_t total_queued = hw_frames + ring_frames;

    /* 3. Produce up to 2 chunks (512 frames = 11.6 ms) if below 17.4 ms watermark */
    int produced = 0;
    if (engine != AUDIO_NEXUS_ENGINE_NONE) {
        for (int iter = 0; iter < 2 && total_queued < AUDIO_NEXUS_TARGET_FRAMES; iter++) {
            if (AudioNexusPcmRing_len(&s_pcm_ring) >= AUDIO_NEXUS_PCM_SLOTS) break;
            AudioNexusPcmChunk chunk;
            chunk.frames = AUDIO_NEXUS_CHUNK_FRAMES;
            chunk.engine = engine;
            if (engine == AUDIO_NEXUS_ENGINE_BSF) {
                bsf_render_frames(chunk.pcm, AUDIO_NEXUS_CHUNK_FRAMES);
            } else if (engine == AUDIO_NEXUS_ENGINE_LILCU64) {
                lilcu64_synth_render_frames(chunk.pcm, AUDIO_NEXUS_CHUNK_FRAMES);
            } else {
                orch_rt_render_frames(chunk.pcm, AUDIO_NEXUS_CHUNK_FRAMES);
            }
            if (AudioNexusPcmRing_snd(&s_pcm_ring, &chunk) != 0) break;
            total_queued += AUDIO_NEXUS_CHUNK_FRAMES;
            produced += (int)AUDIO_NEXUS_CHUNK_FRAMES;
        }
    }

    /* 4. Push-sink drain (POSIX SDL2 / ARM64 HDMI & USB UAC1) */
    if (!s_pull_sink && AudioNexusPcmRing_len(&s_pcm_ring) > 0u) {
        uint32_t want_rate = (engine == AUDIO_NEXUS_ENGINE_ORCH && ov && ov->sample_rate)
                             ? ov->sample_rate : s_sample_rate;
        if (!virtio_sound_is_ready() || (s_hw_rate != 0u && s_hw_rate != want_rate)) {
            if (virtio_sound_open(want_rate, 2) == 0) {
                s_hw_rate = want_rate;
            }
        } else if (s_hw_rate == 0u) {
            s_hw_rate = want_rate;
        }
        if (virtio_sound_is_ready()) {
            AudioNexusPcmChunk out;
            for (int iter = 0; iter < 4; iter++) {
                if (!AudioNexusPcmRing_rcv(&s_pcm_ring, &out)) break;
                (void)virtio_sound_write(out.pcm, out.frames);
            }
        }
    }

    /* 5. Publish wait-free telemetry snapshot */
    hw_frames = s_pull_sink ? 0u : (virtio_sound_get_queued_bytes() / 4u);
    ring_frames = AudioNexusPcmRing_len(&s_pcm_ring) * AUDIO_NEXUS_CHUNK_FRAMES;
    nexus_publish_telem(engine, hw_frames + ring_frames);
    return produced;
}

size_t audio_nexus_pull_pcm(int16_t *out_pcm, size_t max_frames) {
    if (!out_pcm || max_frames < AUDIO_NEXUS_CHUNK_FRAMES) return 0;
    AudioNexusPcmChunk chunk;
    if (!AudioNexusPcmRing_rcv(&s_pcm_ring, &chunk)) return 0;
    memcpy(out_pcm, chunk.pcm, (size_t)chunk.frames * 2u * sizeof(int16_t));
    return chunk.frames;
}

bool audio_nexus_read_telem(AudioNexusTelem *out) {
    if (!out || !s_inited) return false;
    for (int attempt = 0; attempt < 4; attempt++) {
        uint32_t s1 = s_telem_seq;
        if (s1 & 1u) continue;
        NEXUS_BARRIER();
        AudioNexusTelem tmp = s_telem_buf;
        NEXUS_BARRIER();
        uint32_t s2 = s_telem_seq;
        if (s1 == s2) {
            *out = tmp;
            return true;
        }
    }
    return false;
}

/* ── Dedicated Async TASK_AUDIO (POSIX pthread / µITRON / ARM64) ──── */
static void audio_nexus_task_fn(VW exinf) {
    (void)exinf;
    while (s_running) {
        (void)audio_nexus_core_step();
        dly_tsk((W)AUDIO_NEXUS_TASK_PERIOD_MS);
    }
    s_audio_tskid = 0;
}

int audio_nexus_start_task(void) {
    if (!s_inited) (void)audio_nexus_init(BSF_SAMPLE_RATE);
    if (s_running) return 0;

    T_CTSK ctsk;
    memset(&ctsk, 0, sizeof(ctsk));
    ctsk.tskatr  = TA_HLNG;
    ctsk.task    = audio_nexus_task_fn;
    ctsk.itskpri = 5;       /* Equal priority to UI/Input per KERNEL-NEXUS.txt §3.1 */
    ctsk.stksz   = 32768;
    ctsk.name    = "audio_nexus";

    s_running = true;
    ID tid = cre_tsk(&ctsk);
    if (tid <= 0) {
        s_running = false;
        return -1;
    }
    s_audio_tskid = tid;
    (void)sta_tsk(tid, 0);
    return 0;
}

void audio_nexus_stop_task(void) {
    s_running = false;
}

/* Pico Core 1 direct poll hook (non-blocking, called inside pico_core1_body) */
void audio_nexus_pico_poll(void) {
    if (!s_inited) return;
    (void)audio_nexus_core_step();
}
