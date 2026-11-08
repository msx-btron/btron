/*
 * B-BT: L2CAP Layer Implementation (src/drivers/bluetooth/b_bt_l2cap.c)
 * Conforming to Bluetooth Core Spec v5.0, Volume 3, Part A
 *
 * NASA JPL "Power of Ten" Software Safety Rules:
 *   - Static channel table (BBT_MAX_L2CAP_CHANS = 4)
 *   - Zero dynamic memory allocation
 *   - Bounded parsing
 *   - Modular functions <= 60 lines
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <drivers/bluetooth/b_bt_l2cap.h>
#include <drivers/bluetooth/b_bt_hci.h>
#include <drivers/bluetooth/b_bt_avdtp.h>

static bbt_l2cap_chan_t s_channels[BBT_MAX_L2CAP_CHANS];
static uint8_t s_sig_id = 1;

void bbt_l2cap_init(void) {
    for (uint32_t i = 0; i < BBT_MAX_L2CAP_CHANS; i++) {
        s_channels[i].active = false;
        s_channels[i].handle = 0;
        s_channels[i].scid = 0;
        s_channels[i].dcid = 0;
        s_channels[i].psm = 0;
        s_channels[i].configured = false;
    }
    s_sig_id = 1;
}

int bbt_l2cap_send(uint16_t handle, uint16_t dcid, const void *data, uint16_t len) {
    if (!data || len == 0 || len > BBT_ACL_MTU_MAX - sizeof(bbt_l2cap_hdr_t)) {
        return -1;
    }

    uint8_t frame[BBT_ACL_MTU_MAX];
    bbt_l2cap_hdr_t *hdr = (bbt_l2cap_hdr_t *)frame;
    hdr->length = len;
    hdr->cid = dcid;

    memcpy(frame + sizeof(bbt_l2cap_hdr_t), data, len);
    return bbt_hci_send_acl(handle, 0x02 /* First PB */, frame, sizeof(bbt_l2cap_hdr_t) + len);
}

int bbt_l2cap_connect_req(uint16_t handle, uint16_t psm, uint16_t scid) {
    int free_idx = -1;
    for (uint32_t i = 0; i < BBT_MAX_L2CAP_CHANS; i++) {
        if (!s_channels[i].active) {
            free_idx = (int)i;
            break;
        }
    }
    if (free_idx < 0) return -1;

    s_channels[free_idx].active = true;
    s_channels[free_idx].handle = handle;
    s_channels[free_idx].scid = scid;
    s_channels[free_idx].dcid = 0;
    s_channels[free_idx].psm = psm;
    s_channels[free_idx].configured = false;

    uint8_t sig_buf[sizeof(bbt_l2cap_sig_hdr_t) + sizeof(bbt_l2cap_conn_req_t)];
    bbt_l2cap_sig_hdr_t *sig = (bbt_l2cap_sig_hdr_t *)sig_buf;
    sig->code = L2CAP_CMD_CONN_REQ;
    sig->identifier = s_sig_id++;
    sig->length = sizeof(bbt_l2cap_conn_req_t);

    bbt_l2cap_conn_req_t *req = (bbt_l2cap_conn_req_t *)(sig_buf + sizeof(bbt_l2cap_sig_hdr_t));
    req->psm = psm;
    req->scid = scid;

    return bbt_l2cap_send(handle, L2CAP_CID_SIGNALING, sig_buf, sizeof(sig_buf));
}

static void handle_signaling_command(uint16_t handle, const uint8_t *sig_data, size_t len) {
    if (len < sizeof(bbt_l2cap_sig_hdr_t)) return;
    const bbt_l2cap_sig_hdr_t *sig = (const bbt_l2cap_sig_hdr_t *)sig_data;
    const uint8_t *payload = sig_data + sizeof(bbt_l2cap_sig_hdr_t);

    switch (sig->code) {
        case L2CAP_CMD_CONN_RSP: {
            if (sig->length < sizeof(bbt_l2cap_conn_rsp_t)) return;
            const bbt_l2cap_conn_rsp_t *rsp = (const bbt_l2cap_conn_rsp_t *)payload;
            if (rsp->result == 0 /* Success */) {
                for (uint32_t i = 0; i < BBT_MAX_L2CAP_CHANS; i++) {
                    if (s_channels[i].active && s_channels[i].scid == rsp->scid) {
                        s_channels[i].dcid = rsp->dcid;

                        /* Send L2CAP Configure Request */
                        uint8_t cfg_buf[sizeof(bbt_l2cap_sig_hdr_t) + sizeof(bbt_l2cap_cfg_req_t)];
                        bbt_l2cap_sig_hdr_t *csig = (bbt_l2cap_sig_hdr_t *)cfg_buf;
                        csig->code = L2CAP_CMD_CFG_REQ;
                        csig->identifier = s_sig_id++;
                        csig->length = sizeof(bbt_l2cap_cfg_req_t);

                        bbt_l2cap_cfg_req_t *creq = (bbt_l2cap_cfg_req_t *)(cfg_buf + sizeof(bbt_l2cap_sig_hdr_t));
                        creq->dcid = rsp->dcid;
                        creq->flags = 0;

                        bbt_l2cap_send(handle, L2CAP_CID_SIGNALING, cfg_buf, sizeof(cfg_buf));
                        break;
                    }
                }
            }
            break;
        }

        case L2CAP_CMD_CFG_REQ: {
            if (sig->length < sizeof(bbt_l2cap_cfg_req_t)) return;
            const bbt_l2cap_cfg_req_t *creq = (const bbt_l2cap_cfg_req_t *)payload;

            uint8_t crsp_buf[sizeof(bbt_l2cap_sig_hdr_t) + sizeof(bbt_l2cap_cfg_rsp_t)];
            bbt_l2cap_sig_hdr_t *csig = (bbt_l2cap_sig_hdr_t *)crsp_buf;
            csig->code = L2CAP_CMD_CFG_RSP;
            csig->identifier = sig->identifier;
            csig->length = sizeof(bbt_l2cap_cfg_rsp_t);

            bbt_l2cap_cfg_rsp_t *crsp = (bbt_l2cap_cfg_rsp_t *)(crsp_buf + sizeof(bbt_l2cap_sig_hdr_t));
            crsp->scid = creq->dcid;
            crsp->flags = 0;
            crsp->result = 0; /* Success */

            bbt_l2cap_send(handle, L2CAP_CID_SIGNALING, crsp_buf, sizeof(crsp_buf));
            break;
        }

        case L2CAP_CMD_CFG_RSP: {
            if (sig->length < sizeof(bbt_l2cap_cfg_rsp_t)) return;
            const bbt_l2cap_cfg_rsp_t *crsp = (const bbt_l2cap_cfg_rsp_t *)payload;
            if (crsp->result == 0) {
                for (uint32_t i = 0; i < BBT_MAX_L2CAP_CHANS; i++) {
                    if (s_channels[i].active && s_channels[i].scid == crsp->scid) {
                        s_channels[i].configured = true;
                        if (s_channels[i].psm == L2CAP_PSM_AVDTP) {
                            /* Signaling channel ready: start AVDTP Discover */
                            bbt_avdtp_send_discover(handle, s_channels[i].dcid);
                        }
                        break;
                    }
                }
            }
            break;
        }

        default:
            break;
    }
}

void bbt_l2cap_on_rx_frame(uint16_t handle, const uint8_t *frame, size_t len) {
    if (!frame || len < sizeof(bbt_l2cap_hdr_t)) return;
    const bbt_l2cap_hdr_t *hdr = (const bbt_l2cap_hdr_t *)frame;
    const uint8_t *payload = frame + sizeof(bbt_l2cap_hdr_t);

    if (hdr->cid == L2CAP_CID_SIGNALING) {
        handle_signaling_command(handle, payload, hdr->length);
    } else {
        /* Route dynamic channels to upper layer (AVDTP) */
        bbt_avdtp_on_rx_signal(handle, payload, hdr->length);
    }
}

