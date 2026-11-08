/*
 * BTRON3 BCM2711 AArch64 Hardware HDMI Audio Driver Header
 * VideoCore VI HDMI MAI (Multi-channel Audio Interconnect) & ACR Engine
 * Conforming to B-Audio specification & NASA JPL Rules
 */

#ifndef _BCM2711_HDMI_AUDIO_H_
#define _BCM2711_HDMI_AUDIO_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* BCM2711 ARM Physical Base Addresses for HDMI0 */
#define BCM2711_HDMI0_HD_BASE          0xFEF20000UL /* HDMI Management & MAI FIFO */
#define BCM2711_HDMI0_CORE_BASE        0xFEF00000UL /* HDMI Core & Packet RAM */
#define BCM2711_HDMI1_HD_BASE          0xFEF30000UL
#define BCM2711_HDMI1_CORE_BASE        0xFEF10000UL

/* HDMI MAI Control Registers (relative to HDMI0_HD_BASE) */
#define HDMI_MAI_CTL                   0x0010
#define HDMI_MAI_THR                   0x0014
#define HDMI_MAI_FMT                   0x0018
#define HDMI_MAI_DATA                  0x001C
#define HDMI_MAI_SMP                   0x0020

/* HDMI_MAI_CTL Bitfields */
#define HDMI_MAI_CTL_RESET             (1u << 0)
#define HDMI_MAI_CTL_FLUSH             (1u << 1)
#define HDMI_MAI_CTL_ENABLE            (1u << 2)
#define HDMI_MAI_CTL_CH_NUM_2          (0u << 3)
#define HDMI_MAI_CTL_CH_NUM_8          (1u << 3)
#define HDMI_MAI_CTL_FORMAT_LPCM       (0u << 4)
#define HDMI_MAI_CTL_FORMAT_HBR        (1u << 4)
#define HDMI_MAI_CTL_ERROR             (1u << 8)

/* HDMI Core Registers (relative to HDMI0_CORE_BASE) */
#define HDMI_CORE_REV                  0x0000
#define HDMI_SW_RESET_CONTROL          0x0004
#define HDMI_HOTPLUG                   0x0008
#define HDMI_TX_PHY_ACR_0              0x0080
#define HDMI_TX_PHY_ACR_1              0x0084
#define HDMI_RAM_PACKET_START          0x0400
#define HDMI_RAM_PACKET_CONFIG         0x04A0

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

/* DMA Pacing Signal for BCM2711 DMA Engine */
#define BCM2711_DMA_DREQ_HDMI0_MAI     10u
#define BCM2711_HDMI_DMA_CHANNEL       4

/* CEA-861 Audio InfoFrame (14 bytes total wire packet: 3-byte header + 1-byte checksum + 10-byte payload) */
typedef struct __attribute__((packed)) {
    uint8_t type;            /* 0x84: Audio InfoFrame */
    uint8_t version;         /* 0x01 */
    uint8_t length;          /* 0x0A */
    uint8_t checksum;        /* 256 - (sum % 256) */
    uint8_t audio_coding;    /* Coding type & Channel count (CT[3:0], CC[2:0]) */
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

