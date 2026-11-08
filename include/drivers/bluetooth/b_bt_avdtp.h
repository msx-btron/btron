/*
 * B-BT: AVDTP (Audio/Video Distribution Transport Protocol v1.3) Header
 * Conforming to Bluetooth A2DP v1.3.2 Specification
 * NASA JPL Safety Rules: Strictly typed state machines, bounded packet parsing
 */

#ifndef _BTRON_B_BT_AVDTP_H_
#define _BTRON_B_BT_AVDTP_H_

#include <drivers/bluetooth/b_bt.h>

/* AVDTP Message Types */
#define AVDTP_MSG_COMMAND               0x00u
#define AVDTP_MSG_ACCEPT                0x02u
#define AVDTP_MSG_REJECT                0x03u

/* AVDTP Signal Identifiers */
#define AVDTP_SIG_DISCOVER              0x01u
#define AVDTP_SIG_GET_CAPABILITIES      0x02u
#define AVDTP_SIG_SET_CONFIGURATION     0x03u
#define AVDTP_SIG_GET_CONFIGURATION     0x04u
#define AVDTP_SIG_RECONFIGURE           0x05u
#define AVDTP_SIG_OPEN                  0x06u
#define AVDTP_SIG_START                 0x07u
#define AVDTP_SIG_CLOSE                 0x08u
#define AVDTP_SIG_SUSPEND               0x09u
#define AVDTP_SIG_ABORT                 0x0Au

/* AVDTP Service Capabilities */
#define AVDTP_CAP_MEDIA_TRANSPORT       0x01u
#define AVDTP_CAP_MEDIA_CODEC           0x07u

/* Codec Types */
#define A2DP_CODEC_SBC                  0x00u

/* Stream States */
typedef enum {
    AVDTP_STATE_IDLE = 0,
    AVDTP_STATE_DISCOVERING,
    AVDTP_STATE_CONFIGURING,
    AVDTP_STATE_CONFIGURED,
    AVDTP_STATE_OPENING,
    AVDTP_STATE_OPEN,
    AVDTP_STATE_STREAMING,
    AVDTP_STATE_CLOSING
} bbt_avdtp_state_t;

/* RTP Media Header (12 bytes) + 1 byte SBC payload header */
typedef struct {
    uint8_t  v_p_x_cc;       /* 0x80: V=2, P=0, X=0, CC=0 */
    uint8_t  m_pt;           /* 0x60: M=0, PT=96 (Dynamic Audio) */
    uint16_t seq_num;        /* Sequence Number (Big Endian) */
    uint32_t timestamp;      /* Sample timestamp (Big Endian) */
    uint32_t ssrc;           /* Synchronization Source (0x00000001) */
    uint8_t  sbc_frames;     /* Number of SBC frames in this packet (1) */
} __attribute__((packed)) bbt_avdtp_media_hdr_t;

/* Public Functions */
void              bbt_avdtp_init(void);
bbt_avdtp_state_t bbt_avdtp_get_state(void);
int               bbt_avdtp_send_discover(uint16_t handle, uint16_t sig_cid);
int               bbt_avdtp_send_set_config(uint16_t handle, uint16_t sig_cid, uint8_t seid);
int               bbt_avdtp_send_open(uint16_t handle, uint16_t sig_cid, uint8_t seid);
int               bbt_avdtp_send_start(uint16_t handle, uint16_t sig_cid, uint8_t seid);
int               bbt_avdtp_send_media_frame(uint16_t handle, uint16_t media_cid,
                                             const uint8_t *sbc_data, uint16_t sbc_len,
                                             uint32_t sample_count);
void              bbt_avdtp_on_rx_signal(uint16_t handle, const uint8_t *sig_data, size_t len);

#endif /* _BTRON_B_BT_AVDTP_H_ */

