/*
 * B-BT: Fixed-Point C99 SBC (Subband Codec) Audio Encoder Header
 * Conforming to Bluetooth A2DP v1.3.2 Specification (Appendix B)
 * NASA JPL "Power of Ten" Software Safety Rules:
 *   - 100% integer arithmetic (Q15 fixed-point)
 *   - Zero floating-point registers
 *   - Zero dynamic heap allocation
 *   - Bounded execution time (WCET <= 120 us on Cortex-A72)
 */

#ifndef _BTRON_B_BT_SBC_H_
#define _BTRON_B_BT_SBC_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SBC_SYNCWORD               0x9Cu

/* Sampling Frequencies */
#define SBC_FREQ_16K               0x00u
#define SBC_FREQ_32K               0x01u
#define SBC_FREQ_44K1              0x02u
#define SBC_FREQ_48K               0x03u

/* Block Modes */
#define SBC_BLOCKS_4               0x00u
#define SBC_BLOCKS_8               0x01u
#define SBC_BLOCKS_12              0x02u
#define SBC_BLOCKS_16              0x03u

/* Channel Modes */
#define SBC_MODE_MONO              0x00u
#define SBC_MODE_DUAL_CHANNEL      0x01u
#define SBC_MODE_STEREO            0x02u
#define SBC_MODE_JOINT_STEREO      0x03u

/* Allocation Methods */
#define SBC_ALLOC_LOUDNESS         0x00u
#define SBC_ALLOC_SNR              0x01u

/* Subbands */
#define SBC_SUBBANDS_4             0x00u
#define SBC_SUBBANDS_8             0x01u

/* Recommended High-Quality Bitpool for 48 kHz Joint Stereo */
#define SBC_DEFAULT_BITPOOL        53u
#define SBC_FRAME_SAMPLES          128u /* 16 blocks * 8 subbands = 128 stereo frames */
#define SBC_MAX_FRAME_LEN          256u

typedef struct {
    uint8_t frequency;
    uint8_t blocks;
    uint8_t channel_mode;
    uint8_t allocation_method;
    uint8_t subbands;
    uint8_t bitpool;
} bbt_sbc_config_t;

/* Public API Functions */
void bbt_sbc_init(void);
int  bbt_sbc_get_frame_len(const bbt_sbc_config_t *cfg);
int  bbt_sbc_encode_frame(const int16_t *pcm_stereo_samples,
                          uint8_t *out_sbc_buf,
                          size_t out_max_len,
                          const bbt_sbc_config_t *cfg);

#endif /* _BTRON_B_BT_SBC_H_ */

