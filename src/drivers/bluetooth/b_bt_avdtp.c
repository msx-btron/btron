/*
 * B-BT: AVDTP v1.3 Layer Implementation (src/drivers/bluetooth/b_bt_avdtp.c)
 * Conforming to Bluetooth A2DP v1.3.2 Specification
 *
 * NASA JPL "Power of Ten" Software Safety Rules:
 *   - Deterministic finite state machine
 *   - Bounded packet parsing
 *   - Zero dynamic memory allocation
 *   - Functions <= 60 lines
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <drivers/bluetooth/b_bt_avdtp.h>
#include <drivers/bluetooth/b_bt_l2cap.h>
#include <drivers/bluetooth/b_bt_sbc.h>

static bbt_avdtp_state_t s_state = AVDTP_STATE_IDLE;
static uint8_t           s_trans_label = 0;
static uint8_t           s_active_seid = 1;
static uint16_t          s_sig_cid = 0;
static uint16_t          s_media_cid = 0x0042;
static uint16_t          s_seq_num = 0;
static uint32_t          s_timestamp = 0;

void bbt_avdtp_init(void) {
    s_state = AVDTP_STATE_IDLE;
    s_trans_label = 0;
    s_active_seid = 1;
    s_sig_cid = 0;
    s_media_cid = 0x0042;
    s_seq_num = 0;
    s_timestamp = 0;
}

bbt_avdtp_state_t bbt_avdtp_get_state(void) {
    return s_state;
}

int bbt_avdtp_send_discover(uint16_t handle, uint16_t sig_cid) {
    s_sig_cid = sig_cid;
    uint8_t pkt[2];
    pkt[0] = (uint8_t)((s_trans_label++ << 4) | (0x00 << 2) | AVDTP_MSG_COMMAND);
    pkt[1] = AVDTP_SIG_DISCOVER;

    s_state = AVDTP_STATE_DISCOVERING;
    return bbt_l2cap_send(handle, sig_cid, pkt, sizeof(pkt));
}

int bbt_avdtp_send_set_config(uint16_t handle, uint16_t sig_cid, uint8_t seid) {
    s_active_seid = seid;
    uint8_t pkt[12];
    pkt[0] = (uint8_t)((s_trans_label++ << 4) | (0x00 << 2) | AVDTP_MSG_COMMAND);
    pkt[1] = AVDTP_SIG_SET_CONFIGURATION;
    pkt[2] = (uint8_t)(seid << 2);     /* ACP SEID */
    pkt[3] = 0x01 << 2;                 /* INT SEID */
    pkt[4] = AVDTP_CAP_MEDIA_TRANSPORT;
    pkt[5] = 0;                         /* Length of Media Transport = 0 */
    pkt[6] = AVDTP_CAP_MEDIA_CODEC;
    pkt[7] = 4;                         /* Length of Codec Cap */
    pkt[8] = 0x00;                      /* Audio */
    pkt[9] = A2DP_CODEC_SBC;            /* SBC */
    pkt[10] = 0x11;                     /* 48 kHz (0x10), Joint Stereo (0x01) */
    pkt[11] = 0x15;                     /* 16 Blocks (0x10), 8 Subbands (0x04), Loudness (0x01) */

    s_state = AVDTP_STATE_CONFIGURING;
    return bbt_l2cap_send(handle, sig_cid, pkt, sizeof(pkt));
}

int bbt_avdtp_send_open(uint16_t handle, uint16_t sig_cid, uint8_t seid) {
    uint8_t pkt[3];
    pkt[0] = (uint8_t)((s_trans_label++ << 4) | (0x00 << 2) | AVDTP_MSG_COMMAND);
    pkt[1] = AVDTP_SIG_OPEN;
    pkt[2] = (uint8_t)(seid << 2);

    s_state = AVDTP_STATE_OPENING;
    return bbt_l2cap_send(handle, sig_cid, pkt, sizeof(pkt));
}

int bbt_avdtp_send_start(uint16_t handle, uint16_t sig_cid, uint8_t seid) {
    uint8_t pkt[3];
    pkt[0] = (uint8_t)((s_trans_label++ << 4) | (0x00 << 2) | AVDTP_MSG_COMMAND);
    pkt[1] = AVDTP_SIG_START;
    pkt[2] = (uint8_t)(seid << 2);

    return bbt_l2cap_send(handle, sig_cid, pkt, sizeof(pkt));
}

int bbt_avdtp_send_media_frame(uint16_t handle, uint16_t media_cid,
                               const uint8_t *sbc_data, uint16_t sbc_len,
                               uint32_t sample_count) {
    if (!sbc_data || sbc_len == 0 || s_state != AVDTP_STATE_STREAMING) return -1;
    if (sbc_len > BBT_ACL_MTU_MAX - sizeof(bbt_avdtp_media_hdr_t)) return -1;

    uint8_t pkt[sizeof(bbt_avdtp_media_hdr_t) + SBC_MAX_FRAME_LEN];
    bbt_avdtp_media_hdr_t *hdr = (bbt_avdtp_media_hdr_t *)pkt;

    hdr->v_p_x_cc = 0x80;   /* Version 2 */
    hdr->m_pt = 0x60;       /* Dynamic Audio PT 96 */
    hdr->seq_num = (uint16_t)((s_seq_num >> 8) | (s_seq_num << 8));
    hdr->timestamp = ((s_timestamp >> 24) & 0xFF) | (((s_timestamp >> 16) & 0xFF) << 8) |
                     (((s_timestamp >> 8) & 0xFF) << 16) | ((s_timestamp & 0xFF) << 24);
    hdr->ssrc = 0x01000000; /* SSRC 1 in network endian */
    hdr->sbc_frames = 1;

    memcpy(pkt + sizeof(bbt_avdtp_media_hdr_t), sbc_data, sbc_len);

    s_seq_num++;
    s_timestamp += sample_count;

    return bbt_l2cap_send(handle, media_cid, pkt, sizeof(bbt_avdtp_media_hdr_t) + sbc_len);
}

void bbt_avdtp_on_rx_signal(uint16_t handle, const uint8_t *sig_data, size_t len) {
    if (!sig_data || len < 2) return;
    uint8_t msg_type = sig_data[0] & 0x03u;
    uint8_t sig_id = sig_data[1] & 0x3Fu;

    if (msg_type == AVDTP_MSG_ACCEPT) {
        switch (sig_id) {
            case AVDTP_SIG_DISCOVER:
                if (len >= 4) {
                    uint8_t seid = (uint8_t)(sig_data[2] >> 2);
                    bbt_avdtp_send_set_config(handle, s_sig_cid, seid);
                }
                break;

            case AVDTP_SIG_SET_CONFIGURATION:
                s_state = AVDTP_STATE_CONFIGURED;
                bbt_avdtp_send_open(handle, s_sig_cid, s_active_seid);
                break;

            case AVDTP_SIG_OPEN:
                s_state = AVDTP_STATE_OPEN;
                bbt_l2cap_connect_req(handle, L2CAP_PSM_AVDTP, s_media_cid);
                bbt_avdtp_send_start(handle, s_sig_cid, s_active_seid);
                break;

            case AVDTP_SIG_START:
                s_state = AVDTP_STATE_STREAMING;
                break;

            default:
                break;
        }
    }
}

