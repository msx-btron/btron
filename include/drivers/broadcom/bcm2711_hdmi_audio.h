/*
 * BTRON3 BCM2711 AArch64 Hardware HDMI Audio Driver Header
 * VideoCore VI HDMI MAI (Multi-channel Audio Interconnect) & ACR Engine
 * Conforming to B-Audio specification & NASA JPL Rules
 *
 * Register map matches the BCM2711 (VC5) blocks in Linux vc4_hdmi_regs.h.
 * HDMI0 and HDMI1 share one HD block; their MAI windows are 0x20 apart.
 */

#ifndef _BCM2711_HDMI_AUDIO_H_
#define _BCM2711_HDMI_AUDIO_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* BCM2711 ARM physical bases. The HD block is shared by both ports. */
#define BCM2711_HDMI0_CORE_BASE        0xFEF00700UL
#define BCM2711_HDMI0_RAM_BASE         0xFEF01B00UL
#define BCM2711_HDMI0_HD_BASE          0xFEF20000UL
#define BCM2711_HDMI1_CORE_BASE        0xFEF05700UL
#define BCM2711_HDMI1_RAM_BASE         0xFEF06B00UL
#define BCM2711_HDMI1_HD_BASE          0xFEF20000UL

/* HDMI0 MAI registers, relative to the shared HD block.
 * HDMI1 uses the same offsets plus HDMI1_MAI_SHIFT. */
#define HDMI_MAI_CTL                   0x0010
#define HDMI_MAI_THR                   0x0014
#define HDMI_MAI_FMT                   0x0018
#define HDMI_MAI_DATA                  0x001C
#define HDMI_MAI_SMP                   0x0020
#define HDMI1_MAI_SHIFT                0x0020

/* HDMI_MAI_CTL bitfields (VC4_HD_MAI_CTL_*). */
#define HDMI_MAI_CTL_RESET             (1u << 0)
#define HDMI_MAI_CTL_ERRORF            (1u << 1)
#define HDMI_MAI_CTL_ERRORE            (1u << 2)
#define HDMI_MAI_CTL_ENABLE            (1u << 3)
#define HDMI_MAI_CTL_CHNUM_SHIFT       4
#define HDMI_MAI_CTL_PAREN             (1u << 8)
#define HDMI_MAI_CTL_FLUSH             (1u << 9)
#define HDMI_MAI_CTL_WHOLSMP           (1u << 12)
#define HDMI_MAI_CTL_CHALIGN           (1u << 13)
#define HDMI_MAI_CTL_DLATE             (1u << 15)

/* HDMI core registers, relative to either HDMI core base. */
#define HDMI_MAI_CHANNEL_MAP           0x009C
#define HDMI_MAI_CONFIG_REG            0x00A0
#define HDMI_AUDIO_PACKET_CONFIG       0x00B8
#define HDMI_RAM_PACKET_CONFIG         0x00BC
#define HDMI_CRP_CFG                   0x00C8
#define HDMI_CTS_0                     0x00CC
#define HDMI_CTS_1                     0x00D0
#define HDMI_SCHEDULER_CONTROL         0x00E0
#define HDMI_HOTPLUG                   0x01A8

#define HDMI_HOTPLUG_CONNECTED         (1u << 0)
#define HDMI_SCHEDULER_MODE_HDMI       (1u << 0)
#define HDMI_SCHEDULER_HDMI_ACTIVE     (1u << 1)
#define HDMI_RAM_PACKET_ENABLE         (1u << 16)
#define HDMI_PACKET_STRIDE             0x24u
#define HDMI_AUDIO_PACKET_ID           4u

/* DVP parent that clocks MAI_SMP. Not a mailbox firmware-clock id. */
#define BCM2711_HSM_CLOCK_HZ           108000000u

/* Legacy-DMA bus address of MAI_DATA (0x7E window, not the ARM 0xFE alias). */
#define BCM2711_HDMI0_MAI_DATA_BUS     0x7EF2001Cu
#define BCM2711_HDMI1_MAI_DATA_BUS     0x7EF2003Cu

/* CEA-861 Audio InfoFrame Packet Constants */
#define CEA861_AUDIO_INFOFRAME_TYPE    0x84
#define CEA861_AUDIO_INFOFRAME_VERS    0x01
#define CEA861_AUDIO_INFOFRAME_LEN     0x0A

/* B-Audio Real-Time Constraints */
#define BCM2711_HDMI_AUDIO_RATE        48000u
#define BCM2711_HDMI_AUDIO_CHANNELS    2u
#define BCM2711_HDMI_PERIOD_FRAMES     128u
#define BCM2711_HDMI_PERIOD_BYTES      (BCM2711_HDMI_PERIOD_FRAMES * BCM2711_HDMI_AUDIO_CHANNELS * sizeof(int16_t))
#define BCM2711_HDMI_RING_PERIODS      2u
#define BCM2711_HDMI_RING_BYTES        (BCM2711_HDMI_PERIOD_BYTES * BCM2711_HDMI_RING_PERIODS)

/* DMA pacing. Channel 4, DREQ 10 = HDMI0 MAI, DREQ 17 = HDMI1 MAI. */
#define BCM2711_DMA_DREQ_HDMI0_MAI     10u
#define BCM2711_DMA_DREQ_HDMI1         17u
#define BCM2711_HDMI_DMA_CHANNEL       4

/* CEA-861 Audio InfoFrame (14 bytes: 3-byte header + checksum + 10-byte payload).
 * audio_coding low 3 bits are the channel-count code CC = channels - 1
 * (1 means stereo). A value of 2 advertises three channels and a Sony mutes. */
typedef struct __attribute__((packed)) {
    uint8_t type;            /* 0x84: Audio InfoFrame */
    uint8_t version;         /* 0x01 */
    uint8_t length;          /* 0x0A */
    uint8_t checksum;        /* 256 - (sum % 256) */
    uint8_t audio_coding;    /* CT in bits 7:4, CC = channels-1 in bits 2:0 */
    uint8_t format_info;     /* Sampling Frequency (SF[2:0]), Sample Size (SS[1:0]) */
    uint8_t format_ext;      /* Format / coding extension */
    uint8_t speaker_alloc;   /* Speaker allocation mapping (FL/FR = 0x00) */
    uint8_t downmix_info;    /* Downmix inhibit & level shift value */
    uint8_t reserved[5];     /* Reserved bytes */
} cea861_audio_infoframe_t;

/* ACR (Audio Clock Regeneration) parameter pair */
typedef struct {
    uint32_t n;
    uint32_t cts;
} hdmi_acr_t;

/* Public API Prototypes */
int      bcm2711_hdmi_audio_init(void);
int      bcm2711_hdmi_audio_open(uint32_t sample_rate, uint8_t channels);
int      bcm2711_hdmi_audio_write(const int16_t *samples, size_t frames);
void     bcm2711_hdmi_audio_close(void);
bool     bcm2711_hdmi_audio_is_ready(void);
uint32_t bcm2711_hdmi_audio_get_queued_bytes(void);
bool     bcm2711_hdmi_audio_is_sink_present(void);

/* Mathematical helper for ACR generation across arbitrary pixel clocks */
int      bcm2711_hdmi_calculate_acr(uint32_t sample_rate, uint32_t pixel_clock_hz, hdmi_acr_t *out_acr);

/* Helper to build compliant CEA-861 Audio InfoFrame */
int      bcm2711_hdmi_build_infoframe(cea861_audio_infoframe_t *out_frame, uint32_t sample_rate, uint8_t channels);

#endif /* _BCM2711_HDMI_AUDIO_H_ */
