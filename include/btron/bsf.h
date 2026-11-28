/*
 * B-System (BTRON 3.20) — include/btron/bsf.h
 * B-Sound Forth (BSF) Real-Time Firmware VM, 16-Voice DSP & Catalogue API
 * Pure C99, zero heap allocation on audio plane, NASA/JPL Power of Ten.
 */

#ifndef _BTRON_BSF_H_
#define _BTRON_BSF_H_

#include <rt/fix16.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSF_SAMPLE_RATE      44100u
#define BSF_QUANTUM          32u      /* 32 samples @ 44.1 kHz ~= 725 us (666 us WCET budget) */
#define BSF_MAX_VOICES       12
#define BSF_MAX_TRACKS       32       /* 26 built-in (20 samples + 6 compositions) + custom */
#define BSF_MAX_PARTS        20
#define BSF_MAX_SHIMS        16
#define BSF_MAX_PRIMS        15
#define BSF_DSTACK_MAX       256
#define BSF_RSTACK_MAX       128
#define BSF_CSTACK_MAX       64
#define BSF_DICT_MAX         256
#define BSF_CODE_MAX         32768    /* 32K 32-bit cells = 128 KiB static bytecode arena */
#define BSF_VARS_MAX         512
#define BSF_MAX_STEPS        32768    /* Hard JPL P10 execution bound per quantum */

/* 15 Pro DSP Primitive Modes (1:1 with Music.html legend & ch03-waveforms-dsp) */
typedef enum {
    BSF_MODE_SINE = 0,
    BSF_MODE_TRI  = 1,
    BSF_MODE_SAW  = 2,
    BSF_MODE_PLS  = 3,
    BSF_MODE_FM   = 4,
    BSF_MODE_AM   = 5,
    BSF_MODE_NSE  = 6,
    BSF_MODE_SAT  = 7,
    BSF_MODE_LPF  = 8,
    BSF_MODE_HPF  = 9,
    BSF_MODE_BPF  = 10,
    BSF_MODE_LFO  = 11,
    BSF_MODE_PNO  = 12,
    BSF_MODE_RVB  = 13,
    BSF_MODE_ECHO = 14,
    BSF_MODE_COUNT = 15
} BsfDspMode;

/* Track folder categories */
typedef enum {
    BSF_CAT_MATH_SYNTH  = 0,  /* /SYS/synth/samples/ 01..16 */
    BSF_CAT_BSOUND_DSP  = 1,  /* /SYS/synth/samples/ 17..20 */
    BSF_CAT_COMPOSITION = 2   /* /SYS/synth/compositions/ 21..26 */
} BsfCategory;

/* Static voice state (allocated in fixed pool, zero heap) */
typedef struct {
    bool     active;
    bool     gate;
    uint8_t  mode;            /* BsfDspMode */
    uint8_t  filt_type;       /* 0=none, 1=LPF, 2=HPF, 3=BPF */
    fix16_t  y_pitch;         /* Octave pitch relative to C4 (FIX16_QUIET = silent) */
    fix16_t  freq_hz;         /* Current frequency in Hz (Q16.16) */
    uint32_t phase;           /* Q32 oscillator phase */
    uint32_t fm_phase;        /* Q32 FM modulator phase */
    uint32_t lfo_phase;       /* Q32 LFO phase */
    uint32_t am_phase;        /* Q32 AM phase */
    float    env;             /* Smoothed anti-click amplitude envelope [0..1] */
    float    amp;             /* Target gain [0..1] */
    float    pan;             /* Stereo pan [-1..+1] */
    float    fm_ratio;        /* 2-Op FM modulator ratio (default 2.76) */
    float    fm_index;        /* 2-Op FM modulation index (default 1.8) */
    float    sat_k;           /* WaveShaper k (default 25.0) */
    float    lfo_rate;        /* Vibrato LFO rate Hz (default 5.8) */
    float    lfo_depth;       /* Vibrato depth in octaves (default 0.022) */
    float    am_rate;         /* AM rasp rate Hz (default 32.26) */
    float    am_depth;        /* AM depth [0..1] */
    float    filt_cutoff;     /* TPT SVF cutoff Hz */
    float    filt_q;          /* TPT SVF quality factor Q */
    float    svf_k;           /* Precomputed 1/Q damping */
    float    svf_a1;          /* Precomputed TPT SVF a1 = 1 / (1 + g*(g + k)) */
    float    svf_a2;          /* Precomputed TPT SVF a2 = g * a1 */
    float    svf_a3;          /* Precomputed TPT SVF a3 = g * a2 */
    float    lp_state;        /* 1-pole LP state (NSE low-band) */
    float    hp_state;        /* 1-pole HP state (NSE high-band) */
    float    bp_s1;           /* TPT SVF integrator 1 (ic1eq) */
    float    bp_s2;           /* TPT SVF integrator 2 (ic2eq) */
    float    pno_age;         /* Piano pluck decay age in seconds */
    float    pno_br;          /* Recursive felt-hammer brightness e^(-8.5*s) */
    float    pno_dc;          /* Recursive string body decay e^(-2.5*s) */
    fix16_t  prev_y;          /* Previous pitch for retrigger detection */
} BsfVoice;

/* Real-time telemetry snapshot read by Synth Visualizer & Cassette VU meters */
typedef struct {
    fix16_t  x_pos;                       /* Current cursor x in [-8, +8] */
    int      track_idx;                   /* Active catalogue index */
    int      part_idx;                    /* Active part index (0..part_count-1) */
    int      part_count;
    int      voice_count;
    fix16_t  voice_y[BSF_MAX_VOICES];     /* Current pitch per voice (FIX16_QUIET if silent) */
    uint8_t  voice_mode[BSF_MAX_VOICES];  /* Active BsfDspMode per voice */
    bool     voice_active[BSF_MAX_VOICES];
    bool     voice_echo_active[BSF_MAX_VOICES]; /* True when per-voice echo tail is audibly sounding */
    float    peak_l;                      /* [0..1] */
    float    peak_r;                      /* [0..1] */
    float    rms_l;                       /* [0..1] */
    float    rms_r;                       /* [0..1] */
    bool     limiter_hit;
    float    balance;                     /* [-1..+1] */
    uint32_t wcet_us;                     /* Measured microseconds for last quantum */
    uint64_t sample_counter;
    bool     playing;
    bool     muted;
    bool     auto_advance;
    char     last_error[128];
} BsfTelemetry;

/* Track / Sample / Composition metadata parsed from .bsf header comments */
typedef struct {
    int         id;                       /* 1..26 */
    BsfCategory category;
    char        title[64];
    char        subtitle[64];
    char        filepath[128];            /* e.g. /SYS/synth/samples/01_harmonic_fan.bsf */
    char        formula[256];
    int         part_count;
    char        part_names[BSF_MAX_PARTS][48];
    fix16_t     speed;                    /* x units per second */
    float       reverb_mix;               /* 0.0 .. 0.6 */
    float       gain;                     /* Normalized loudness trim (default calibrated per track) */
} BsfTrackInfo;

/* Base library file descriptor (Primitives & Shims) */
typedef struct {
    int  id;
    char name[40];
    char word[32];
    char marker[16];
    char filepath[128];
} BsfLibItem;

/* ── Public Lifecycle, Catalogue & Playback API ──────────────────── */

int  bsf_init(uint32_t sample_rate);
void bsf_close(void);

/* Load base libraries (14 primitives + 16 shims) and scan 26 tracks */
int  bsf_load_base_libraries(void);
int  bsf_scan_catalogue(void);

/* Track, part, transport, and hot-reload control */
int  bsf_select_track(int track_idx, int part_idx);
int  bsf_set_part(int part_idx);
int  bsf_reload(void);
void bsf_play(void);
void bsf_pause(void);
void bsf_stop(void);
void bsf_toggle_play(void);
void bsf_toggle_mute(void);
void bsf_toggle_auto_advance(void);
void bsf_set_x(fix16_t x_pos);
bool bsf_is_playing(void);

/* Audio rendering & VirtIO/POSIX pump */
void bsf_render_frames(int16_t *out_pcm, size_t frames);
int  bsf_audio_pump(void);

/* Pure preview evaluation for Visualizer curve plotting (thread/state-safe) */
int  bsf_eval_preview(fix16_t x_pos, int part_idx,
                      fix16_t *out_y, uint8_t *out_modes, int max_voices);
void bsf_get_preview_echo(int voice, float *out_sec, float *out_fb, float *out_wet);

/* Telemetry & catalogue accessors */
const BsfTelemetry *bsf_get_telemetry(void);
int                 bsf_get_track_count(void);
const BsfTrackInfo *bsf_get_track_info(int idx);
int                 bsf_get_prim_count(void);
const BsfLibItem   *bsf_get_prim_info(int idx);
int                 bsf_get_shim_count(void);
const BsfLibItem   *bsf_get_shim_info(int idx);
const char         *bsf_mode_name(uint8_t mode);

/* File I/O helper (reads VFS /SYS/... with host assets/... fallback) */
int  bsf_read_text_file(const char *path, char *buf, size_t cap, size_t *out_len);

/* Compile arbitrary BSF source string (used by loader & unit tests) */
int  bsf_compile_source(const char *src, const char *origin);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_BSF_H_ */
