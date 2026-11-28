/*
 * B-System (BTRON 3.20) — src/languages/bsf/bsf_dsp.c
 * B-Sound Forth (BSF) 16-Voice Real-Time DSP Engine, Schroeder Reverb,
 * 4-Folder Catalogue Scanner, & VirtIO/POSIX Audio Pump
 * Pure C99, zero heap allocation on audio plane, NASA/JPL Power of Ten.
 */

#include <btron/bsf.h>
#include <btron/libc_shim.h>
#include "../../media/sdk/lilcu64_synth.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#if BTRON_HOSTED || defined(__STDC_HOSTED__)
#include <time.h>
#endif

/* Weak VirtIO audio driver hooks (same pattern as lilcu64_synth.c) */
__attribute__((weak)) int  virtio_sound_open(uint32_t sample_rate, uint8_t channels) {
    (void)sample_rate; (void)channels; return -1;
}
__attribute__((weak)) int  virtio_sound_write(const int16_t *samples, size_t frames) {
    (void)samples; (void)frames; return -1;
}
__attribute__((weak)) void     virtio_sound_close(void) {}
__attribute__((weak)) bool     virtio_sound_is_ready(void) { return false; }
__attribute__((weak)) uint32_t virtio_sound_get_queued_bytes(void) { return 0; }
__attribute__((weak)) void     virtio_sound_flush(void) {}

/* Internal VM bridge (bsf_vm.c) */
extern void bsf_vm_reset_all(void);
extern void bsf_vm_save_watermark(void);
extern void bsf_vm_restore_watermark(void);
extern int  bsf_vm_resolve_track_entry(void);
extern int  bsf_vm_run_track_eval(fix16_t x_pos, int32_t part_idx);

/* ── Static Engine State ─────────────────────────────────────────── */
static uint32_t     s_rate = BSF_SAMPLE_RATE;
static BsfVoice     s_voices[BSF_MAX_VOICES];
static BsfTelemetry s_telem;
static bool         s_initialized = false;
static bool         s_base_loaded = false;
static bool         s_virtio_open = false;
static bool         s_preview_mode = false;
static int          s_y_out_cursor = 0;

/* Preview scratch buffer */
static fix16_t      s_prev_y[BSF_MAX_VOICES];
static uint8_t      s_prev_modes[BSF_MAX_VOICES];
static float        s_prev_echo_sec[BSF_MAX_VOICES];
static float        s_prev_echo_fb[BSF_MAX_VOICES];
static float        s_prev_echo_wet[BSF_MAX_VOICES];
static int          s_prev_count = 0;

/* Deterministic xorshift32 noise RNG */
static uint32_t     s_rng = 0x9E3779B9u;

/* 4-Comb Schroeder Hall Reverb (Orchestra Eq. 15, g = 0.78) */
#define BSF_COMB_MAX 2048
static const uint32_t k_comb_del[4] = { 1116, 1356, 1617, 1907 };
static float          s_comb_buf[4][BSF_COMB_MAX];
static uint32_t       s_comb_idx[4];
static float          s_comb_lp[4];
static float          s_reverb_mix = 0.0f;

/* Per-voice stereo / ping-pong echo primitive (4 independent damped delay buses, up to 800 ms @ 44.1 kHz) */
#define BSF_ECHO_VOICES 4
#define BSF_ECHO_MAX    36864
static float          s_vecho_l[BSF_ECHO_VOICES][BSF_ECHO_MAX];
static float          s_vecho_r[BSF_ECHO_VOICES][BSF_ECHO_MAX];
static uint32_t       s_vecho_idx[BSF_ECHO_VOICES];
static uint32_t       s_vecho_del_l[BSF_ECHO_VOICES];
static uint32_t       s_vecho_del_r[BSF_ECHO_VOICES];
static float          s_vecho_lp_l[BSF_ECHO_VOICES];
static float          s_vecho_lp_r[BSF_ECHO_VOICES];
static float          s_vecho_damp[BSF_ECHO_VOICES];
static bool           s_vecho_pp[BSF_ECHO_VOICES];
static float          s_vecho_fb[BSF_ECHO_VOICES];
static float          s_vecho_wet[BSF_ECHO_VOICES];
static float          s_vecho_sec[BSF_ECHO_VOICES];

/*
 * Gated Slow-RMS Broadcast Auto-Leveler (ch03-waveforms-dsp.tex, Def. AGC):
 *   R_q      = sqrt( (1 / 2Q) * sum(x_L^2 + x_R^2) )
 *   R_hat    = R_hat + alpha * (R_q - R_hat)        if R_q > GATE_RMS (frozen on rests)
 *   g*       = clamp(TARGET_RMS / max(R_hat, FLOOR), GAIN_MIN, GAIN_MAX)
 *   g_agc    = g_agc + alpha_gain * (g* - g_agc)
 *   y_{L,R}  = 0.42 * tanh(1.15 * g_agc * g_track * x_{L,R})
 * Time constants: alpha = 1 - exp(-dt_q / tau), dt_q = Q / f_s.
 */
#define BSF_AGC_TARGET_RMS  0.26f   /* Pre-limiter RMS target (-11.7 dBFS -> ~-19 dBFS post-tanh) */
#define BSF_AGC_GATE_RMS    0.025f  /* Silence gate threshold (-32 dBFS): freezes gain on rests */
#define BSF_AGC_RMS_FLOOR   0.05f   /* Denominator safety floor */
#define BSF_AGC_GAIN_MIN    0.52f   /* -5.7 dB maximum attenuation */
#define BSF_AGC_GAIN_MAX    1.95f   /* +5.8 dB maximum makeup boost */
#define BSF_AGC_TAU_ATK_S   0.040f  /* 40 ms RMS attack time constant */
#define BSF_AGC_TAU_REL_S   0.240f  /* 240 ms RMS release time constant */
#define BSF_AGC_TAU_GAIN_S  0.050f  /* 50 ms zipper-free gain slew time constant */

static float          s_warm_lp_l = 0.0f;
static float          s_warm_lp_r = 0.0f;
static float          s_active_smooth = 2.0f;
static float          s_agc_rms = BSF_AGC_TARGET_RMS;
static float          s_agc_gain = 1.0f;
static float          s_pno_br_mul = 0.9998073f; /* exp(-8.5 / 44100) */
static float          s_pno_dc_mul = 0.9999433f; /* exp(-2.5 / 44100) */

/* Precompute Cytomic / Zavalishin TPT 2-pole SVF coefficients (unconditionally BIBO stable) */
static void voice_update_svf(BsfVoice *v) {
    float fc = v->filt_cutoff;
    float max_fc = 0.45f * (float)s_rate;
    if (fc < 20.0f) fc = 20.0f;
    if (fc > max_fc) fc = max_fc;
    float q = v->filt_q;
    if (q < 0.5f) q = 0.5f;
    if (q > 12.0f) q = 12.0f;
    float g = tanf(3.14159265f * fc / (float)s_rate);
    float k = 1.0f / q;
    float a1 = 1.0f / (1.0f + g * (g + k));
    v->svf_k  = k;
    v->svf_a1 = a1;
    v->svf_a2 = g * a1;
    v->svf_a3 = g * v->svf_a2;
}

/* Low-latency pump buffer (256 frames = 5.8 ms @ 44.1 kHz) */
#define BSF_PUMP_FRAMES        256u
#define BSF_TARGET_QUEUE_BYTES 4096u  /* ~23.2 ms low-latency B-Audio watermark */
static int16_t        s_pump_pcm[BSF_PUMP_FRAMES * 2];
static char           s_file_buf[24576];

/* ── Canonical Base Libraries (14 Primitives + 16 Shims) ─────────── */
static const BsfLibItem k_prims[BSF_MAX_PRIMS] = {
    {  1, "Sine Oscillator",     "PRIM-SINE", "SINE", "/SYS/synth/primitives/01_sine.bsf" },
    {  2, "Triangle Oscillator", "PRIM-TRI",  "TRI",  "/SYS/synth/primitives/02_tri.bsf"  },
    {  3, "Band-Limited Saw",    "PRIM-SAW",  "SAW",  "/SYS/synth/primitives/03_saw.bsf"  },
    {  4, "Pulse / Square",      "PRIM-PLS",  "PLS",  "/SYS/synth/primitives/04_pls.bsf"  },
    {  5, "2-Op FM Synth",       "PRIM-FM",   "FM",   "/SYS/synth/primitives/05_fm.bsf"   },
    {  6, "Amplitude Mod",       "PRIM-AM",   "AM",   "/SYS/synth/primitives/06_am.bsf"   },
    {  7, "Stochastic Noise",    "PRIM-NSE",  "NSE",  "/SYS/synth/primitives/07_nse.bsf"  },
    {  8, "WaveShaper Sat",      "PRIM-SAT",  "SAT",  "/SYS/synth/primitives/08_sat.bsf"  },
    {  9, "Resonant Low-Pass",   "PRIM-LPF",  "LPF",  "/SYS/synth/primitives/09_lpf.bsf"  },
    { 10, "High-Pass Edge",      "PRIM-HPF",  "HPF",  "/SYS/synth/primitives/10_hpf.bsf"  },
    { 11, "Formant Band-Pass",   "PRIM-BPF",  "BPF",  "/SYS/synth/primitives/11_bpf.bsf"  },
    { 12, "Pitch / Ribbon LFO",  "PRIM-LFO",  "LFO",  "/SYS/synth/primitives/12_lfo.bsf"  },
    { 13, "Acoustic Piano",      "PRIM-PNO",  "PNO",  "/SYS/synth/primitives/13_pno.bsf"  },
    { 14, "Schroeder Reverb",    "PRIM-RVB",  "RVB",  "/SYS/synth/primitives/14_rvb.bsf"  },
    { 15, "Stereo Ping-Pong Echo","PRIM-ECHO","ECH",  "/SYS/synth/primitives/15_echo.bsf" }
};

static const BsfLibItem k_shims[BSF_MAX_SHIMS] = {
    {  1, "Pulse Squash",     "SHIM-PULSE-SQUASH", "SINE", "/SYS/synth/shims/01_pulse_squash.bsf" },
    {  2, "Frog Croak",       "SHIM-FROG-CROAK",   "AM",   "/SYS/synth/shims/02_frog_croak.bsf"   },
    {  3, "Pulse Bell",       "SHIM-PULSE-BELL",   "FM",   "/SYS/synth/shims/03_pulse_bell.bsf"   },
    {  4, "Waddle Bass",      "SHIM-WADDLE",       "LPF",  "/SYS/synth/shims/04_waddle.bsf"       },
    {  5, "Float Chord",      "SHIM-FLOAT",        "SINE", "/SYS/synth/shims/05_float.bsf"        },
    {  6, "Dance Cascade",    "SHIM-DANCE",        "SINE", "/SYS/synth/shims/06_dance.bsf"        },
    {  7, "Crystalline Ding", "SHIM-DING",         "SINE", "/SYS/synth/shims/07_ding.bsf"         },
    {  8, "Retro Kick",       "SHIM-KICK",         "SINE", "/SYS/synth/shims/08_kick.bsf"         },
    {  9, "Dual Snare",       "SHIM-SNARE",        "NSE",  "/SYS/synth/shims/09_snare.bsf"        },
    { 10, "Metallic Hi-Hat",  "SHIM-HIHAT",        "HPF",  "/SYS/synth/shims/10_hihat.bsf"        },
    { 11, "Jazz Guitar",      "SHIM-GUITAR",       "TRI",  "/SYS/synth/shims/11_guitar.bsf"       },
    { 12, "Grand Piano",      "SHIM-GRAND-PIANO",  "PNO",  "/SYS/synth/shims/12_grand_piano.bsf"  },
    { 13, "Pedal Sub-Bass",   "SHIM-PEDAL-BASS",   "TRI",  "/SYS/synth/shims/13_pedal_bass.bsf"   },
    { 14, "Industrial Kick",  "SHIM-IND-KICK",     "SAT",  "/SYS/synth/shims/14_ind_kick.bsf"     },
    { 15, "Industrial Bass",  "SHIM-IND-BASS",     "SAT",  "/SYS/synth/shims/15_ind_bass.bsf"     },
    { 16, "Cyber Lead",       "SHIM-CYBER-LEAD",   "SAT",  "/SYS/synth/shims/16_cyber_lead.bsf"   }
};

/* Default catalogue paths (20 samples + 6 compositions = 26 entries) */
static const char *k_default_tracks[26] = {
    "/SYS/synth/samples/01_harmonic_fan.bsf",
    "/SYS/synth/samples/02_burgundy_groove.bsf",
    "/SYS/synth/samples/03_motor_chord.bsf",
    "/SYS/synth/samples/04_sub_swell.bsf",
    "/SYS/synth/samples/05_lime_breakbeat.bsf",
    "/SYS/synth/samples/06_cobalt_euclid.bsf",
    "/SYS/synth/samples/07_crimson_acid.bsf",
    "/SYS/synth/samples/08_mint_swing.bsf",
    "/SYS/synth/samples/09_emerald_acid.bsf",
    "/SYS/synth/samples/10_violet_drift.bsf",
    "/SYS/synth/samples/11_bronze_pulse.bsf",
    "/SYS/synth/samples/12_obsidian_canon.bsf",
    "/SYS/synth/samples/13_sage_funk.bsf",
    "/SYS/synth/samples/14_sapphire_arp.bsf",
    "/SYS/synth/samples/15_neon_pluck.bsf",
    "/SYS/synth/samples/16_analytical_etudes.bsf",
    "/SYS/synth/samples/17_bsound_kinematic.bsf",
    "/SYS/synth/samples/18_bsound_percussion.bsf",
    "/SYS/synth/samples/19_bsound_orchestra.bsf",
    "/SYS/synth/samples/20_bsound_industrial.bsf",
    "/SYS/synth/compositions/21_carol_of_bells.bsf",
    "/SYS/synth/compositions/22_green_caterpillar.bsf",
    "/SYS/synth/compositions/23_house_of_rising_sun.bsf",
    "/SYS/synth/compositions/24_tron_init.bsf",
    "/SYS/synth/compositions/25_electro_glide.bsf",
    "/SYS/synth/compositions/26_carla_bley_440.bsf"
};

static BsfTrackInfo s_tracks[BSF_MAX_TRACKS];
static int          s_track_count = 0;

static const char *k_mode_names[BSF_MODE_COUNT] = {
    "SINE", "TRI", "SAW", "PLS", "FM", "AM", "NSE",
    "SAT",  "LPF", "HPF", "BPF", "LFO", "PNO", "RVB", "ECH"
};

const char *bsf_mode_name(uint8_t mode) {
    if (mode >= BSF_MODE_COUNT) return "SINE";
    return k_mode_names[mode];
}

/* ── VM-to-DSP Hook Implementations ──────────────────────────────── */
void bsf_dsp_set_error(const char *msg) {
    if (!msg) return;
    strncpy(s_telem.last_error, msg, sizeof(s_telem.last_error) - 1);
    s_telem.last_error[sizeof(s_telem.last_error) - 1] = '\0';
}

fix16_t bsf_dsp_get_x(void)       { return s_telem.x_pos; }
int32_t bsf_dsp_get_part(void)    { return s_telem.part_idx; }
int32_t bsf_dsp_get_samples(void) { return (int32_t)(s_telem.sample_counter & 0x7FFFFFFFu); }

void bsf_dsp_set_reverb(fix16_t mix) {
    if (s_preview_mode) return;
    float m = fix16_to_float(mix);
    if (m < 0.0f) m = 0.0f;
    if (m > 0.65f) m = 0.65f;
    s_reverb_mix = m;
}

void bsf_dsp_voice_echo(int idx, fix16_t delay_ms, fix16_t feedback, fix16_t mix) {
    if (idx < 0 || idx >= BSF_MAX_VOICES) return;
    float dms = fix16_to_float(delay_ms);
    if (dms < 20.0f) dms = 20.0f;
    if (dms > 800.0f) dms = 800.0f;
    float fb = fix16_to_float(feedback);
    if (fb < 0.0f) fb = 0.0f;
    if (fb > 0.78f) fb = 0.78f;
    float m = fix16_to_float(mix);
    if (m < 0.0f) m = 0.0f;
    if (m > 0.65f) m = 0.65f;

    if (s_preview_mode) {
        s_prev_echo_sec[idx] = dms * 0.001f;
        s_prev_echo_fb[idx]  = fb;
        s_prev_echo_wet[idx] = m;
        return;
    }
    if (idx >= BSF_ECHO_VOICES) return;
    uint32_t dl = (uint32_t)(dms * 0.001f * (float)s_rate);
    if (dl >= BSF_ECHO_MAX) dl = BSF_ECHO_MAX - 1u;
    /* Exact Haas stereo spread & tape damping matching synth-engine.js Track 5 */
    float spread_s = (dms < 300.0f) ? 0.014f : ((dms < 550.0f) ? 0.022f : 0.016f);
    uint32_t spread = (uint32_t)(spread_s * (float)s_rate);
    uint32_t dr = (dl + spread < BSF_ECHO_MAX) ? (dl + spread) : (BSF_ECHO_MAX - 1u);
    if (dr < 64u) dr = 64u;
    s_vecho_del_l[idx] = dl;
    s_vecho_del_r[idx] = dr;
    s_vecho_damp[idx]  = (dms >= 550.0f) ? 0.40f : 0.28f;
    s_vecho_pp[idx]    = (dms < 300.0f); /* 239ms ping-pong; 478/666ms parallel stereo */
    s_vecho_fb[idx]    = fb;
    s_vecho_wet[idx]   = m;
    s_vecho_sec[idx]   = dms * 0.001f;
}

void bsf_dsp_set_echo(fix16_t delay_ms, fix16_t feedback, fix16_t mix) {
    bsf_dsp_voice_echo(0, delay_ms, feedback, mix);
}

void bsf_dsp_set_voice_count(int n) {
    if (n < 0) n = 0;
    if (n > BSF_MAX_VOICES) n = BSF_MAX_VOICES;
    if (s_preview_mode) {
        s_prev_count = n;
    } else {
        s_telem.voice_count = n;
        for (int i = n; i < BSF_MAX_VOICES; i++) {
            s_voices[i].gate = false;
            s_voices[i].y_pitch = FIX16_QUIET;
            s_telem.voice_y[i] = FIX16_QUIET;
            s_telem.voice_active[i] = false;
        }
    }
}

void bsf_dsp_voice_set(int idx, fix16_t y, int mode) {
    if (idx < 0 || idx >= BSF_MAX_VOICES) return;
    uint8_t m = (mode >= 0 && mode < BSF_MODE_COUNT) ? (uint8_t)mode : BSF_MODE_SINE;
    if (s_preview_mode) {
        s_prev_y[idx] = y;
        s_prev_modes[idx] = m;
        if (idx + 1 > s_prev_count) s_prev_count = idx + 1;
        return;
    }
    BsfVoice *v = &s_voices[idx];
    if (v->mode != m && m != BSF_MODE_LPF && m != BSF_MODE_HPF && m != BSF_MODE_BPF) {
        v->filt_type = 0; /* Clear stale filter insert when switching to a non-filter primitive */
    }
    v->mode = m;
    v->y_pitch = y;
    s_telem.voice_y[idx] = y;
    s_telem.voice_mode[idx] = m;
    if (y == FIX16_QUIET) {
        v->gate = false;
        s_telem.voice_active[idx] = false;
    } else {
        if (!v->gate || fix16_abs(y - v->prev_y) > 4096) {
            v->pno_age = 0.0f;
            v->pno_br  = 1.0f;
            v->pno_dc  = 1.0f;
        }
        v->prev_y = y;
        v->freq_hz = fix16_y_to_hz(y);
        v->active = true;
        v->gate = true;
        s_telem.voice_active[idx] = true;
    }
    if (idx + 1 > s_telem.voice_count) s_telem.voice_count = idx + 1;
}

void bsf_dsp_voice_freq(int idx, fix16_t y) {
    if (idx < 0 || idx >= BSF_MAX_VOICES) return;
    int m = s_preview_mode ? s_prev_modes[idx] : s_voices[idx].mode;
    bsf_dsp_voice_set(idx, y, m);
}

void bsf_dsp_voice_mode(int idx, int mode) {
    if (idx < 0 || idx >= BSF_MAX_VOICES) return;
    uint8_t m = (mode >= 0 && mode < BSF_MODE_COUNT) ? (uint8_t)mode : BSF_MODE_SINE;
    if (s_preview_mode) {
        s_prev_modes[idx] = m;
    } else {
        s_voices[idx].mode = m;
        s_telem.voice_mode[idx] = m;
    }
}

void bsf_dsp_voice_amp(int idx, fix16_t amp) {
    if (s_preview_mode || idx < 0 || idx >= BSF_MAX_VOICES) return;
    float a = fix16_to_float(amp);
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    s_voices[idx].amp = a;
}

void bsf_dsp_voice_pan(int idx, fix16_t pan) {
    if (s_preview_mode || idx < 0 || idx >= BSF_MAX_VOICES) return;
    float p = fix16_to_float(pan);
    if (p < -1.0f) p = -1.0f;
    if (p > 1.0f) p = 1.0f;
    s_voices[idx].pan = p;
}

void bsf_dsp_voice_on(int idx) {
    if (s_preview_mode || idx < 0 || idx >= BSF_MAX_VOICES) return;
    s_voices[idx].active = true;
    s_voices[idx].gate = true;
    s_telem.voice_active[idx] = true;
}

void bsf_dsp_voice_off(int idx) {
    if (s_preview_mode || idx < 0 || idx >= BSF_MAX_VOICES) return;
    s_voices[idx].gate = false;
    s_voices[idx].y_pitch = FIX16_QUIET;
    s_telem.voice_y[idx] = FIX16_QUIET;
    s_telem.voice_active[idx] = false;
}

void bsf_dsp_voice_filter(int idx, int type, fix16_t cutoff_hz, fix16_t q) {
    if (s_preview_mode || idx < 0 || idx >= BSF_MAX_VOICES) return;
    BsfVoice *v = &s_voices[idx];
    float fc = fix16_to_float(cutoff_hz);
    float fq = fix16_to_float(q);
    uint8_t ft = (type >= 0 && type <= 3) ? (uint8_t)type : 0u;
    bool changed = (v->filt_type != ft)
                || (fabsf(v->filt_cutoff - fc) > 0.5f)
                || (fabsf(v->filt_q - fq) > 0.01f);
    v->filt_type = ft;
    v->filt_cutoff = fc;
    v->filt_q = fq;
    if (changed) voice_update_svf(v);
}

void bsf_dsp_voice_fm(int idx, fix16_t ratio, fix16_t index) {
    if (s_preview_mode || idx < 0 || idx >= BSF_MAX_VOICES) return;
    s_voices[idx].fm_ratio = fix16_to_float(ratio);
    s_voices[idx].fm_index = fix16_to_float(index);
}

void bsf_dsp_voice_sat(int idx, fix16_t k) {
    if (s_preview_mode || idx < 0 || idx >= BSF_MAX_VOICES) return;
    s_voices[idx].sat_k = fix16_to_float(k);
}

void bsf_dsp_voice_lfo(int idx, fix16_t rate_hz, fix16_t depth_oct) {
    if (s_preview_mode || idx < 0 || idx >= BSF_MAX_VOICES) return;
    s_voices[idx].lfo_rate = fix16_to_float(rate_hz);
    s_voices[idx].lfo_depth = fix16_to_float(depth_oct);
}

void bsf_dsp_voice_am(int idx, fix16_t rate_hz, fix16_t depth) {
    if (s_preview_mode || idx < 0 || idx >= BSF_MAX_VOICES) return;
    s_voices[idx].am_rate = fix16_to_float(rate_hz);
    s_voices[idx].am_depth = fix16_to_float(depth);
}

void bsf_dsp_y_out(fix16_t y) {
    int idx = s_y_out_cursor++;
    if (idx < BSF_MAX_VOICES) {
        int m = s_preview_mode ? s_prev_modes[idx] : s_voices[idx].mode;
        bsf_dsp_voice_set(idx, y, m);
    }
}

/* ── Voice Reset & Default Initialization ────────────────────────── */
static void reset_voices(void) {
    memset(s_voices, 0, sizeof(s_voices));
    for (int i = 0; i < BSF_MAX_VOICES; i++) {
        s_voices[i].y_pitch = FIX16_QUIET;
        s_voices[i].prev_y = FIX16_QUIET;
        s_voices[i].amp = 0.65f;
        s_voices[i].pan = (i % 2 == 0) ? -0.18f : 0.18f;
        s_voices[i].fm_ratio = 2.76f;
        s_voices[i].fm_index = 1.8f;
        s_voices[i].sat_k = 25.0f;
        s_voices[i].lfo_rate = 5.8f;
        s_voices[i].lfo_depth = 0.020f;
        s_voices[i].am_rate = 32.26f;
        s_voices[i].am_depth = 0.45f;
        s_voices[i].filt_cutoff = 1200.0f;
        s_voices[i].filt_q = 2.0f;
        s_voices[i].pno_br = 1.0f;
        s_voices[i].pno_dc = 1.0f;
        voice_update_svf(&s_voices[i]);
        s_telem.voice_y[i] = FIX16_QUIET;
        s_telem.voice_mode[i] = BSF_MODE_SINE;
        s_telem.voice_active[i] = false;
        s_telem.voice_echo_active[i] = false;
    }
    memset(s_comb_buf, 0, sizeof(s_comb_buf));
    memset(s_comb_idx, 0, sizeof(s_comb_idx));
    memset(s_comb_lp, 0, sizeof(s_comb_lp));
    memset(s_vecho_l, 0, sizeof(s_vecho_l));
    memset(s_vecho_r, 0, sizeof(s_vecho_r));
    memset(s_vecho_idx, 0, sizeof(s_vecho_idx));
    memset(s_vecho_lp_l, 0, sizeof(s_vecho_lp_l));
    memset(s_vecho_lp_r, 0, sizeof(s_vecho_lp_r));
    memset(s_vecho_wet, 0, sizeof(s_vecho_wet));
    memset(s_vecho_sec, 0, sizeof(s_vecho_sec));
    memset(s_vecho_fb, 0, sizeof(s_vecho_fb));
    memset(s_vecho_pp, 0, sizeof(s_vecho_pp));
    for (int e = 0; e < BSF_ECHO_VOICES; e++) {
        s_vecho_del_l[e] = 17640u;
        s_vecho_del_r[e] = 18346u;
        s_vecho_damp[e]  = 0.36f;
    }
    s_warm_lp_l = 0.0f;
    s_warm_lp_r = 0.0f;
    s_active_smooth = 2.0f;
    s_agc_rms = BSF_AGC_TARGET_RMS;
    s_agc_gain = 1.0f;
}

/* ── Header Metadata Parser for .bsf Tracks ──────────────────────── */
static void parse_track_metadata(const char *src, const char *path, BsfTrackInfo *info, int fallback_id) {
    char path_copy[128];
    strncpy(path_copy, path ? path : "", sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    memset(info, 0, sizeof(*info));
    info->id = fallback_id;
    info->category = (fallback_id <= 16) ? BSF_CAT_MATH_SYNTH
                   : (fallback_id <= 20) ? BSF_CAT_BSOUND_DSP
                                         : BSF_CAT_COMPOSITION;
    snprintf(info->title, sizeof(info->title), "TRACK %02d", fallback_id);
    strncpy(info->filepath, path_copy, sizeof(info->filepath) - 1);
    info->part_count = 1;
    info->speed = fix16_from_float(2.0f);
    info->reverb_mix = 0.0f;
    info->gain = 1.0f;

    const char *p = src;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len > 3 && p[0] == '\\' && p[1] == ' ' && p[2] == '@') {
            char line[300];
            size_t body_len = len - 3;
            size_t clen = (body_len < sizeof(line) - 1) ? body_len : (sizeof(line) - 1);
            memcpy(line, p + 3, clen);
            line[clen] = '\0';
            while (clen > 0 && (line[clen - 1] == '\r' || line[clen - 1] == '\n' || line[clen - 1] == ' ')) {
                line[--clen] = '\0';
            }
            if (strncmp(line, "ID ", 3) == 0) {
                info->id = (int)strtol(line + 3, NULL, 10);
            } else if (strncmp(line, "TITLE ", 6) == 0) {
                strncpy(info->title, line + 6, sizeof(info->title) - 1);
            } else if (strncmp(line, "SUBTITLE ", 9) == 0) {
                strncpy(info->subtitle, line + 9, sizeof(info->subtitle) - 1);
            } else if (strncmp(line, "PARTS ", 6) == 0) {
                int pc = (int)strtol(line + 6, NULL, 10);
                if (pc >= 1 && pc <= BSF_MAX_PARTS) info->part_count = pc;
            } else if (strncmp(line, "PART ", 5) == 0) {
                /* Format: PART <1-based-idx> <name> */
                char *endp = NULL;
                int pi = (int)strtol(line + 5, &endp, 10) - 1;
                if (pi >= 0 && pi < BSF_MAX_PARTS && endp) {
                    while (*endp == ' ') endp++;
                    strncpy(info->part_names[pi], endp, sizeof(info->part_names[pi]) - 1);
                }
            } else if (strncmp(line, "SPEED ", 6) == 0) {
                float sp = (float)atof(line + 6);
                if (sp > 0.1f && sp < 32.0f) info->speed = fix16_from_float(sp);
            } else if (strncmp(line, "REVERB ", 7) == 0) {
                float rv = (float)atof(line + 7);
                if (rv >= 0.0f && rv <= 0.65f) info->reverb_mix = rv;
            } else if (strncmp(line, "GAIN ", 5) == 0) {
                float gn = (float)atof(line + 5);
                if (gn >= 0.1f && gn <= 4.0f) info->gain = gn;
            } else if (strncmp(line, "FORMULA ", 8) == 0) {
                strncpy(info->formula, line + 8, sizeof(info->formula) - 1);
            } else if (strncmp(line, "CATEGORY ", 9) == 0) {
                if (strstr(line + 9, "COMP")) info->category = BSF_CAT_COMPOSITION;
                else if (strstr(line + 9, "DSP") || strstr(line + 9, "SHIM")) info->category = BSF_CAT_BSOUND_DSP;
                else info->category = BSF_CAT_MATH_SYNTH;
            }
        }
        if (!eol) break;
        p = eol + 1;
    }

    for (int i = 0; i < info->part_count; i++) {
        if (info->part_names[i][0] == '\0') {
            snprintf(info->part_names[i], sizeof(info->part_names[i]), "Part %d/%d", i + 1, info->part_count);
        }
    }
}

/* ── Base Library Loading & Catalogue Scanning ───────────────────── */
int bsf_load_base_libraries(void) {
    bsf_vm_reset_all();
    s_telem.last_error[0] = '\0';

    /* 1. Compile all 14 Base Primitives (/SYS/synth/primitives/) */
    for (int i = 0; i < BSF_MAX_PRIMS; i++) {
        size_t len = 0;
        if (bsf_read_text_file(k_prims[i].filepath, s_file_buf, sizeof(s_file_buf), &len) != 0) {
            char err[128];
            snprintf(err, sizeof(err), "Missing primitive: %s", k_prims[i].filepath);
            bsf_dsp_set_error(err);
            return -1;
        }
        if (bsf_compile_source(s_file_buf, k_prims[i].word) != 0) {
            return -1;
        }
    }

    /* 2. Compile all 16 Canonical Shims (/SYS/synth/shims/) */
    for (int i = 0; i < BSF_MAX_SHIMS; i++) {
        size_t len = 0;
        if (bsf_read_text_file(k_shims[i].filepath, s_file_buf, sizeof(s_file_buf), &len) != 0) {
            char err[128];
            snprintf(err, sizeof(err), "Missing shim: %s", k_shims[i].filepath);
            bsf_dsp_set_error(err);
            return -1;
        }
        if (bsf_compile_source(s_file_buf, k_shims[i].word) != 0) {
            return -1;
        }
    }

    bsf_vm_save_watermark();
    s_base_loaded = true;
    return 0;
}

int bsf_scan_catalogue(void) {
    s_track_count = 0;
    for (int i = 0; i < 26 && s_track_count < BSF_MAX_TRACKS; i++) {
        size_t len = 0;
        if (bsf_read_text_file(k_default_tracks[i], s_file_buf, sizeof(s_file_buf), &len) == 0) {
            parse_track_metadata(s_file_buf, k_default_tracks[i], &s_tracks[s_track_count], i + 1);
            s_track_count++;
        }
    }
    return s_track_count;
}

int bsf_init(uint32_t sample_rate) {
    if (s_initialized) return 0;
    if (sample_rate == 0) sample_rate = BSF_SAMPLE_RATE;
    s_rate = sample_rate;
    s_pno_br_mul = expf(-8.5f / (float)s_rate);
    s_pno_dc_mul = expf(-2.5f / (float)s_rate);
    memset(&s_telem, 0, sizeof(s_telem));
    s_telem.x_pos = fix16_from_int(-8);
    s_telem.auto_advance = true;
    reset_voices();

    if (!s_base_loaded) {
        (void)bsf_load_base_libraries();
    }
    if (s_track_count == 0) {
        (void)bsf_scan_catalogue();
    }
    if (s_track_count > 0 && s_telem.track_idx >= 0) {
        (void)bsf_select_track(0, 0);
    }
    s_initialized = true;
    return 0;
}

void bsf_close(void) {
    s_telem.playing = false;
    /* Flush queued PCM without closing the shared system B-Audio device */
    virtio_sound_flush();
    s_virtio_open = false;
    reset_voices();
}

int bsf_select_track(int track_idx, int part_idx) {
    if (track_idx < 0 || track_idx >= s_track_count) return -1;
    if (!s_base_loaded && bsf_load_base_libraries() != 0) return -1;

    const BsfTrackInfo *tr = &s_tracks[track_idx];
    if (part_idx < 0) part_idx = 0;
    if (part_idx >= tr->part_count) part_idx = tr->part_count - 1;

    size_t len = 0;
    if (bsf_read_text_file(tr->filepath, s_file_buf, sizeof(s_file_buf), &len) != 0) {
        bsf_dsp_set_error("Cannot open BSF track file");
        return -1;
    }

    /* Refresh metadata in case the file was edited in t_editor */
    parse_track_metadata(s_file_buf, tr->filepath, &s_tracks[track_idx], tr->id);

    bsf_vm_restore_watermark();
    s_telem.last_error[0] = '\0';
    if (bsf_compile_source(s_file_buf, tr->title) != 0) {
        return -1;
    }
    if (bsf_vm_resolve_track_entry() != 0) {
        bsf_dsp_set_error("BSF track missing TRACK-EVAL word");
        return -1;
    }

    reset_voices();
    virtio_sound_flush();
    s_reverb_mix = s_tracks[track_idx].reverb_mix;
    s_telem.track_idx = track_idx;
    s_telem.part_idx = part_idx;
    s_telem.part_count = s_tracks[track_idx].part_count;
    s_telem.x_pos = fix16_from_int(-8);
    s_telem.voice_count = 0;
    return 0;
}

int bsf_set_part(int part_idx) {
    if (s_telem.track_idx < 0 || s_telem.track_idx >= s_track_count) return -1;
    const BsfTrackInfo *tr = &s_tracks[s_telem.track_idx];
    if (part_idx < 0) part_idx = 0;
    if (part_idx >= tr->part_count) part_idx = tr->part_count - 1;
    s_telem.part_idx = part_idx;
    s_telem.x_pos = fix16_from_int(-8);
    virtio_sound_flush();
    return 0;
}

int bsf_reload(void) {
    bool was_playing = s_telem.playing;
    int tr = s_telem.track_idx;
    int pt = s_telem.part_idx;
    s_telem.playing = false;
    virtio_sound_flush();

    if (bsf_load_base_libraries() != 0) return -1;
    if (tr < 0 || tr >= s_track_count) tr = 0;
    if (bsf_select_track(tr, pt) != 0) return -1;
    if (was_playing) bsf_play();
    return 0;
}

void bsf_play(void) {
    if (!s_initialized) bsf_init(BSF_SAMPLE_RATE);
    if (!virtio_sound_is_ready()) {
        if (virtio_sound_open(s_rate, 2) == 0) {
            s_virtio_open = true;
        }
    } else {
        s_virtio_open = true;
    }
    s_telem.playing = true;
}

void bsf_pause(void) { s_telem.playing = false; virtio_sound_flush(); }
void bsf_stop(void)  { s_telem.playing = false; s_telem.x_pos = fix16_from_int(-8); virtio_sound_flush(); reset_voices(); }
void bsf_toggle_play(void) { if (s_telem.playing) bsf_pause(); else bsf_play(); }
void bsf_toggle_mute(void) { s_telem.muted = !s_telem.muted; if (s_telem.muted) virtio_sound_flush(); }
void bsf_toggle_auto_advance(void) { s_telem.auto_advance = !s_telem.auto_advance; }
bool bsf_is_playing(void) { return s_telem.playing; }

void bsf_set_x(fix16_t x_pos) {
    s_telem.x_pos = fix16_clamp(x_pos, fix16_from_int(-8), fix16_from_int(8));
    virtio_sound_flush();
}

/* ── Pure Preview Evaluation (Visualizer Curve Plotting) ─────────── */
int bsf_eval_preview(fix16_t x_pos, int part_idx,
                     fix16_t *out_y, uint8_t *out_modes, int max_voices) {
    if (!out_y || max_voices <= 0) return 0;
    int lim = (max_voices < BSF_MAX_VOICES) ? max_voices : BSF_MAX_VOICES;
    for (int i = 0; i < lim; i++) {
        s_prev_y[i] = FIX16_QUIET;
        s_prev_modes[i] = BSF_MODE_SINE;
        s_prev_echo_sec[i] = 0.0f;
        s_prev_echo_fb[i] = 0.0f;
        s_prev_echo_wet[i] = 0.0f;
    }
    s_prev_count = 0;
    s_y_out_cursor = 0;
    s_preview_mode = true;
    (void)bsf_vm_run_track_eval(x_pos, part_idx);
    s_preview_mode = false;

    int cnt = (s_prev_count < lim) ? s_prev_count : lim;
    for (int i = 0; i < cnt; i++) {
        out_y[i] = s_prev_y[i];
        if (out_modes) out_modes[i] = s_prev_modes[i];
    }
    return cnt;
}

void bsf_get_preview_echo(int voice, float *out_sec, float *out_fb, float *out_wet) {
    if (voice < 0 || voice >= BSF_MAX_VOICES) {
        if (out_sec) *out_sec = 0.0f;
        if (out_fb)  *out_fb  = 0.0f;
        if (out_wet) *out_wet = 0.0f;
        return;
    }
    if (out_sec) *out_sec = s_prev_echo_sec[voice];
    if (out_fb)  *out_fb  = s_prev_echo_fb[voice];
    if (out_wet) *out_wet = s_prev_echo_wet[voice];
}

/* ── Oscillator & Filter Synthesis (Per-Sample, Zero Heap) ───────── */
static inline float fast_noise(void) {
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return ((float)(int32_t)x) * (1.0f / 2147483648.0f);
}

static inline float fast_sin_u32(uint32_t ph) {
    fix16_t s = fix16_sin_norm((fix16_t)(ph >> 16));
    return fix16_to_float(s);
}

static inline float fast_tri(float ph_f) {
    /* Exact sin-phase (2/pi)*arcsin(sin(2*pi*ph_f)) for phase-coherent mixing */
    if (ph_f < 0.25f) return 4.0f * ph_f;
    if (ph_f < 0.75f) return 2.0f - 4.0f * ph_f;
    return 4.0f * ph_f - 4.0f;
}

static inline float fast_saw4(uint32_t ph) {
    /* 4-harmonic band-limited sawtooth (ch03-waveforms-dsp Def. 3.2) */
    return 0.62f * (fast_sin_u32(ph)
                    - 0.50f * fast_sin_u32(ph * 2u)
                    + 0.333333f * fast_sin_u32(ph * 3u)
                    - 0.25f * fast_sin_u32(ph * 4u));
}

static inline float svf_process(BsfVoice *v, float x, uint8_t type) {
    /* Cytomic / Zavalishin TPT 2-pole resonant SVF (zero transcendentals, zero div) */
    float v3 = x - v->bp_s2;
    float v1 = v->svf_a1 * v->bp_s1 + v->svf_a2 * v3;
    float v2 = v->bp_s2 + v->svf_a2 * v->bp_s1 + v->svf_a3 * v3;
    v->bp_s1 = 2.0f * v1 - v->bp_s1;
    v->bp_s2 = 2.0f * v2 - v->bp_s2;
    if (type == 1u) return v2;                               /* Resonant LP */
    if (type == 2u) return x - v->svf_k * v1 - v2;           /* Resonant HP */
    return v->svf_k * v1;                                    /* Unity-peak BP */
}

static inline float synth_voice_sample(BsfVoice *v, float inv_sr) {
    float f = fix16_to_float(v->freq_hz);
    if (f < 20.0f) f = 20.0f;
    if (f > 14000.0f) f = 14000.0f;

    /* LFO vibrato modulation (ch03-waveforms-dsp Eq. 17) */
    if (v->mode == BSF_MODE_LFO) {
        v->lfo_phase += (uint32_t)(v->lfo_rate * inv_sr * 4294967296.0f);
        float lfo = fast_sin_u32(v->lfo_phase);
        f *= (1.0f + lfo * v->lfo_depth * 0.693147f);
    }

    uint32_t inc = (uint32_t)(f * inv_sr * 4294967296.0f);
    v->phase += inc;
    float ph_f = (float)(v->phase >> 16) * (1.0f / 65536.0f); /* [0..1) */
    float s1 = fast_sin_u32(v->phase);
    float s = 0.0f;

    switch (v->mode) {
        case BSF_MODE_SINE: {
            s = (f < 95.0f) ? (0.90f * s1 + 0.10f * fast_sin_u32(v->phase << 1)) : s1;
            break;
        }
        case BSF_MODE_TRI: {
            s = fast_tri(ph_f);
            break;
        }
        case BSF_MODE_SAW:
        case BSF_MODE_LFO: {
            s = fast_saw4(v->phase);
            break;
        }
        case BSF_MODE_PLS: {
            s = 0.78f * fix16_to_float(fix16_tanh(fix16_from_float(3.4f * s1)));
            break;
        }
        case BSF_MODE_FM: {
            /* 2-Op FM synthesis (ch03-waveforms-dsp Eq. 12) */
            v->fm_phase += (uint32_t)(f * v->fm_ratio * inv_sr * 4294967296.0f);
            float mod = v->fm_index * fast_sin_u32(v->fm_phase);
            uint32_t ph_m = v->phase + (uint32_t)(int32_t)(mod * 683565276.0f);
            s = fast_sin_u32(ph_m);
            break;
        }
        case BSF_MODE_AM: {
            /* 4-harmonic band-limited saw with AM/tremolo (ch03-waveforms-dsp Eq. 19) */
            v->am_phase += (uint32_t)(v->am_rate * inv_sr * 4294967296.0f);
            float d = v->am_depth;
            if (d < 0.0f) d = 0.0f;
            if (d > 1.0f) d = 1.0f;
            float env_am = (1.0f - d) + d * fast_sin_u32(v->am_phase);
            s = fast_saw4(v->phase) * env_am;
            break;
        }
        case BSF_MODE_NSE: {
            /* Pitch-adaptive deterministic xorshift32 noise (ch03-waveforms-dsp Def. 3.5) */
            float n = fast_noise();
            if (f > 900.0f) {
                /* High percussion (hats/claps): shaped HP brush + metallic FM ping */
                v->hp_state += 0.45f * (n - v->hp_state);
                v->fm_phase += (uint32_t)(f * 2.76f * inv_sr * 4294967296.0f);
                float hp = n - v->hp_state;
                s = hp * 0.45f + s1 * fast_sin_u32(v->fm_phase) * 0.18f;
            } else {
                /* Low/mid percussion (snares/toms): LP body noise + fundamental */
                v->lp_state += 0.12f * (n - v->lp_state);
                s = v->lp_state * 0.72f + s1 * 0.28f;
            }
            break;
        }
        case BSF_MODE_SAT: {
            /* B-Sound WaveShaper C_k(x) normalized by C_k(1) (ch03-waveforms-dsp Eq. 14) */
            float raw;
            if (f < 100.0f) {
                raw = s1 * 1.45f;
            } else {
                float sq = fix16_to_float(fix16_tanh(fix16_from_float(2.4f * s1)));
                raw = (fast_saw4(v->phase) * 0.65f + sq * 0.45f) * 1.35f;
            }
            float k = v->sat_k;
            if (k < 0.0f) k = 0.0f;
            if (k > 100.0f) k = 100.0f;
            s = (raw * (3.14159265f + k)) / (3.14159265f + k * fabsf(raw));
            break;
        }
        case BSF_MODE_LPF: {
            /* Resonant 2-pole TPT SVF Low-Pass honoring filt_cutoff & filt_q */
            float saw3 = 0.65f * (s1 - 0.50f * fast_sin_u32(v->phase * 2u)
                                  + 0.333333f * fast_sin_u32(v->phase * 3u));
            float raw = saw3 + 0.35f * fast_tri(ph_f);
            s = svf_process(v, raw, 1u) * 1.30f;
            break;
        }
        case BSF_MODE_HPF: {
            /* Resonant 2-pole TPT SVF High-Pass honoring filt_cutoff & filt_q */
            float raw = 0.65f * fast_saw4(v->phase) + 0.35f * fast_noise();
            s = svf_process(v, raw, 2u) * 1.25f;
            break;
        }
        case BSF_MODE_BPF: {
            /* Resonant 2-pole TPT SVF Band-Pass honoring filt_cutoff & filt_q */
            float raw = 0.65f * fast_tri(ph_f) + 0.30f * fast_noise();
            s = svf_process(v, raw, 3u) * 1.35f;
            break;
        }
        case BSF_MODE_PNO:
        case BSF_MODE_ECHO: {
            /* Acoustic Piano-Drop Primitive (ch03-waveforms-dsp Def. 3.3, zero expf per sample) */
            float br = v->pno_br;
            float dc = v->pno_dc;
            v->pno_age += inv_sr;
            v->pno_br *= s_pno_br_mul;
            v->pno_dc *= s_pno_dc_mul;
            float body = 0.16f + 0.84f * dc;
            float hi_roll = (f > 480.0f) ? 0.55f : 1.0f;
            float a2 = 0.20f + 0.18f * br;
            float a3 = 0.06f + 0.12f * br;
            float a4 = 0.05f * br;
            s = 1.15f * body * (0.78f * s1
                                + hi_roll * (a2 * fast_sin_u32(v->phase * 2u)
                                             + a3 * fast_sin_u32(v->phase * 3u)
                                             + a4 * fast_sin_u32(v->phase * 4u)));
            break;
        }
        case BSF_MODE_RVB: {
            /* Hall reverb shimmer carrier (soft sine + airy 2.76f overtone) */
            v->fm_phase += (uint32_t)(f * 2.76f * inv_sr * 4294967296.0f);
            s = 0.72f * s1 + 0.14f * fast_sin_u32(v->fm_phase);
            break;
        }
        default:
            s = s1;
            break;
    }

    /* Optional post-oscillator TPT SVF insert when VOICE-FILTER! is chained onto a non-filter mode */
    if (v->filt_type != 0u
        && v->mode != BSF_MODE_LPF
        && v->mode != BSF_MODE_HPF
        && v->mode != BSF_MODE_BPF) {
        s = svf_process(v, s, v->filt_type);
    }

    /* Smooth anti-click envelope */
    float target = v->gate ? v->amp : 0.0f;
    float coeff = v->gate ? 0.012f : 0.004f;
    v->env += coeff * (target - v->env);
    if (!v->gate && v->env < 0.0004f) {
        v->env = 0.0f;
        v->active = false;
    }
    return s * v->env;
}

static uint64_t bsf_now_us(void) {
#if BTRON_HOSTED || defined(__STDC_HOSTED__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000L);
#else
    return 0;
#endif
}

/* ── Main Real-Time Block Renderer (32-Sample Quanta) ────────────── */
void bsf_render_frames(int16_t *out_pcm, size_t frames) {
    if (!out_pcm || frames == 0) return;
    if (!s_telem.playing || s_telem.muted) {
        memset(out_pcm, 0, frames * 2 * sizeof(int16_t));
        s_telem.peak_l *= 0.85f;
        s_telem.peak_r *= 0.85f;
        s_telem.rms_l  *= 0.85f;
        s_telem.rms_r  *= 0.85f;
        s_telem.limiter_hit = false;
        memset(s_telem.voice_echo_active, 0, sizeof(s_telem.voice_echo_active));
        return;
    }

    float inv_sr = 1.0f / (float)s_rate;
    fix16_t speed = (s_telem.track_idx >= 0 && s_telem.track_idx < s_track_count)
                    ? s_tracks[s_telem.track_idx].speed : fix16_from_int(2);
    /* dx per 32-sample quantum */
    fix16_t dx_q = fix16_mul(speed, fix16_from_float((float)BSF_QUANTUM * inv_sr));

    float pk_l = 0.0f, pk_r = 0.0f;
    float sq_l = 0.0f, sq_r = 0.0f;
    float vecho_pk[BSF_ECHO_VOICES] = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool lim_hit = false;

    size_t done = 0;
    while (done < frames) {
        size_t q = frames - done;
        if (q > BSF_QUANTUM) q = BSF_QUANTUM;

        uint64_t t0 = bsf_now_us();

        /* 1. Evaluate BSF track word at current (x, part) */
        s_y_out_cursor = 0;
        s_preview_mode = false;
        (void)bsf_vm_run_track_eval(s_telem.x_pos, s_telem.part_idx);

        /* Advance x cursor */
        s_telem.x_pos += dx_q;
        if (s_telem.x_pos >= fix16_from_int(8)) {
            s_telem.x_pos = fix16_from_int(-8);
            if (s_telem.auto_advance && s_telem.part_count > 1) {
                s_telem.part_idx = (s_telem.part_idx + 1) % s_telem.part_count;
            }
        }

        /* 2. Render q stereo samples (equal-RMS voicing + smoothed active-voice norm + gated slow-RMS leveler) */
        int cur_active = 0;
        for (int v = 0; v < BSF_MAX_VOICES; v++) {
            if (s_voices[v].gate || s_voices[v].env > 0.02f) cur_active++;
        }
        if (cur_active < 1) cur_active = 1;
        s_active_smooth += 0.08f * ((float)cur_active - s_active_smooth);
        float voice_norm = 1.0f / sqrtf(s_active_smooth > 1.0f ? s_active_smooth : 1.0f);
        float tr_gain = (s_telem.track_idx >= 0 && s_telem.track_idx < s_track_count)
                        ? s_tracks[s_telem.track_idx].gain : 1.0f;
        const float lp_alpha = 0.58f; /* ~7.2 kHz analog warmth LP @ 44.1 kHz */
        float q_pre_sq = 0.0f;

        for (size_t i = 0; i < q; i++) {
            float dry_l = 0.0f, dry_r = 0.0f;
            float echo_sum_l = 0.0f, echo_sum_r = 0.0f;
            for (int v = 0; v < BSF_MAX_VOICES; v++) {
                float vl = 0.0f, vr = 0.0f;
                if (s_voices[v].active || s_voices[v].gate) {
                    float s = synth_voice_sample(&s_voices[v], inv_sr) * voice_norm;
                    float pl = 0.5f * (1.0f - s_voices[v].pan);
                    float pr = 0.5f * (1.0f + s_voices[v].pan);
                    vl = s * pl;
                    vr = s * pr;
                    dry_l += vl;
                    dry_r += vr;
                }
                /* Per-voice tape-damped stereo / ping-pong echo (1:1 with synth-engine.js L2882-2903) */
                if (v < BSF_ECHO_VOICES && s_vecho_wet[v] > 0.005f) {
                    uint32_t wi = s_vecho_idx[v];
                    uint32_t rl_idx = (wi + BSF_ECHO_MAX - s_vecho_del_l[v]) % BSF_ECHO_MAX;
                    uint32_t rr_idx = (wi + BSF_ECHO_MAX - s_vecho_del_r[v]) % BSF_ECHO_MAX;
                    float rd_l = s_vecho_l[v][rl_idx];
                    float rd_r = s_vecho_r[v][rr_idx];
                    float dmp = s_vecho_damp[v];
                    s_vecho_lp_l[v] += (rd_l - s_vecho_lp_l[v]) * dmp;
                    s_vecho_lp_r[v] += (rd_r - s_vecho_lp_r[v]) * dmp;
                    if (s_vecho_pp[v]) {
                        s_vecho_l[v][wi] = vl + s_vecho_lp_r[v] * s_vecho_fb[v];
                        s_vecho_r[v][wi] = vr + s_vecho_lp_l[v] * s_vecho_fb[v];
                    } else {
                        s_vecho_l[v][wi] = vl + s_vecho_lp_l[v] * s_vecho_fb[v];
                        s_vecho_r[v][wi] = vr + s_vecho_lp_r[v] * s_vecho_fb[v];
                    }
                    s_vecho_idx[v] = (wi + 1u) % BSF_ECHO_MAX;
                    float el_out = s_vecho_lp_l[v] * s_vecho_wet[v];
                    float er_out = s_vecho_lp_r[v] * s_vecho_wet[v];
                    echo_sum_l += el_out;
                    echo_sum_r += er_out;
                    float ep = fabsf(el_out) + fabsf(er_out);
                    if (ep > vecho_pk[v]) vecho_pk[v] = ep;
                }
            }

            /* 1-pole analog warmth filter on dry + echo bus */
            float bus_l = dry_l + echo_sum_l;
            float bus_r = dry_r + echo_sum_r;
            s_warm_lp_l += (bus_l - s_warm_lp_l) * lp_alpha;
            s_warm_lp_r += (bus_r - s_warm_lp_r) * lp_alpha;
            float mix_l = s_warm_lp_l;
            float mix_r = s_warm_lp_r;

            /* 4-Comb Schroeder Hall Reverb fed by dry + echo (g = 0.78, damp = 0.28, synth-engine.js L2926-2941) */
            if (s_reverb_mix > 0.005f) {
                float mono = 0.5f * (mix_l + mix_r);
                float rl = 0.0f, rr = 0.0f;
                for (int c = 0; c < 4; c++) {
                    uint32_t ci = s_comb_idx[c];
                    float del = s_comb_buf[c][ci];
                    s_comb_lp[c] += (del - s_comb_lp[c]) * 0.28f;
                    s_comb_buf[c][ci] = mono + 0.78f * s_comb_lp[c];
                    s_comb_idx[c] = (ci + 1u) % k_comb_del[c];
                    if (c & 1) rr += del; else rl += del;
                }
                mix_l += rl * 0.5f * s_reverb_mix;
                mix_r += rr * 0.5f * s_reverb_mix;
            }

            q_pre_sq += 0.5f * (mix_l * mix_l + mix_r * mix_r);

            /* Master auto-leveled gain + optional @GAIN trim + soft-limiter bus (0.42 ceiling) */
            float pre_l = mix_l * 1.15f * s_agc_gain * tr_gain;
            float pre_r = mix_r * 1.15f * s_agc_gain * tr_gain;
            if (fabsf(pre_l) > 0.85f || fabsf(pre_r) > 0.85f) lim_hit = true;
            float out_l = tanhf(pre_l) * 0.42f;
            float out_r = tanhf(pre_r) * 0.42f;

            float al = fabsf(out_l), ar = fabsf(out_r);
            if (al > pk_l) pk_l = al;
            if (ar > pk_r) pk_r = ar;
            sq_l += out_l * out_l;
            sq_r += out_r * out_r;

            size_t idx = (done + i) * 2;
            out_pcm[idx]     = (int16_t)(out_l * 30000.0f);
            out_pcm[idx + 1] = (int16_t)(out_r * 30000.0f);
        }

        /* Gated slow-RMS broadcast auto-leveler (exact exponential time constants) */
        {
            float q_rms = sqrtf(q_pre_sq / (float)q);
            if (q_rms > BSF_AGC_GATE_RMS) {
                float dt_q = (float)q * inv_sr;
                float tau = (q_rms > s_agc_rms) ? BSF_AGC_TAU_ATK_S : BSF_AGC_TAU_REL_S;
                float a_rms = 1.0f - expf(-dt_q / tau);
                s_agc_rms += a_rms * (q_rms - s_agc_rms);

                float denom = (s_agc_rms > BSF_AGC_RMS_FLOOR) ? s_agc_rms : BSF_AGC_RMS_FLOOR;
                float desired = BSF_AGC_TARGET_RMS / denom;
                if (desired < BSF_AGC_GAIN_MIN) desired = BSF_AGC_GAIN_MIN;
                if (desired > BSF_AGC_GAIN_MAX) desired = BSF_AGC_GAIN_MAX;

                float a_gain = 1.0f - expf(-dt_q / BSF_AGC_TAU_GAIN_S);
                s_agc_gain += a_gain * (desired - s_agc_gain);
            }
        }

        uint64_t dt = bsf_now_us() - t0;
        if (dt < 50000u) s_telem.wcet_us = (uint32_t)dt;
        s_telem.sample_counter += q;
        done += q;
    }

    float inv_f = 1.0f / (float)frames;
    s_telem.peak_l = pk_l;
    s_telem.peak_r = pk_r;
    s_telem.rms_l  = sqrtf(sq_l * inv_f);
    s_telem.rms_r  = sqrtf(sq_r * inv_f);
    s_telem.limiter_hit = lim_hit;
    for (int e = 0; e < BSF_ECHO_VOICES; e++) {
        s_telem.voice_echo_active[e] = (s_vecho_wet[e] > 0.005f && vecho_pk[e] > 0.003f);
    }
    s_telem.balance = (pk_l + pk_r > 0.001f) ? ((pk_r - pk_l) / (pk_l + pk_r)) : 0.0f;
}

int bsf_audio_pump(void) {
    if (!s_telem.playing) return 0;
    if (!virtio_sound_is_ready()) {
        if (virtio_sound_open(s_rate, 2) == 0) s_virtio_open = true;
        else return 0;
    }
    /* Tight low-latency B-Audio watermark: ~23.2 ms (4096 bytes) in 5.8 ms (256-frame) chunks */
    uint32_t queued = virtio_sound_get_queued_bytes();
    int pumped = 0;
    for (int iter = 0; iter < 4 && queued < BSF_TARGET_QUEUE_BYTES; iter++) {
        bsf_render_frames(s_pump_pcm, BSF_PUMP_FRAMES);
        (void)virtio_sound_write(s_pump_pcm, BSF_PUMP_FRAMES);
        queued += BSF_PUMP_FRAMES * 4u;
        pumped += (int)BSF_PUMP_FRAMES;
    }
    return pumped;
}

/* ── Accessors ───────────────────────────────────────────────────── */
const BsfTelemetry *bsf_get_telemetry(void)  { return &s_telem; }
int                 bsf_get_track_count(void) { return s_track_count; }
const BsfTrackInfo *bsf_get_track_info(int idx) {
    return (idx >= 0 && idx < s_track_count) ? &s_tracks[idx] : NULL;
}
int                 bsf_get_prim_count(void) { return BSF_MAX_PRIMS; }
const BsfLibItem   *bsf_get_prim_info(int idx) {
    return (idx >= 0 && idx < BSF_MAX_PRIMS) ? &k_prims[idx] : NULL;
}
int                 bsf_get_shim_count(void) { return BSF_MAX_SHIMS; }
const BsfLibItem   *bsf_get_shim_info(int idx) {
    return (idx >= 0 && idx < BSF_MAX_SHIMS) ? &k_shims[idx] : NULL;
}
