/*
 * B-BT: HCI (Host Controller Interface) Implementation (src/drivers/bluetooth/b_bt_hci.c)
 * Conforming to Bluetooth Core Spec v5.0, Volume 4, Part A (H4 Transport)
 *
 * NASA JPL "Power of Ten" Software Safety Rules:
 *   - Zero heap allocations post-boot
 *   - Bounded packet parsing
 *   - Short functions (<= 60 lines)
 *   - 100% checked return values
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <drivers/bluetooth/b_bt_hci.h>
#include <drivers/bluetooth/b_bt_l2cap.h>
#include <drivers/bluetooth/b_bt.h>

extern bbt_tx_fn_t g_bbt_tx_fn;

/* Active Connection State */
typedef struct {
    bool     connected;
    uint16_t handle;
} bbt_hci_conn_t;

static bbt_hci_conn_t s_hci_conn = {0};

/* ── Send HCI Command (H4 Packet Type 0x01) ───────────────────────── */

int bbt_hci_send_cmd(uint16_t opcode, const void *param, uint8_t param_len) {
    if (!g_bbt_tx_fn) return -1;
    if (param_len > 255 - sizeof(bbt_hci_cmd_hdr_t)) return -1;

    uint8_t pkt[260];
    bbt_hci_cmd_hdr_t *hdr = (bbt_hci_cmd_hdr_t *)pkt;
    hdr->h4_type = HCI_H4_CMD;
    hdr->opcode = opcode;
    hdr->param_len = param_len;

    if (param && param_len > 0) {
        memcpy(pkt + sizeof(bbt_hci_cmd_hdr_t), param, param_len);
    }

    size_t total_len = sizeof(bbt_hci_cmd_hdr_t) + param_len;
    return g_bbt_tx_fn(pkt, total_len);
}

/* ── Send HCI ACL Data (H4 Packet Type 0x02) ──────────────────────── */

int bbt_hci_send_acl(uint16_t handle, uint8_t pb_flag, const void *payload, uint16_t len) {
    if (!g_bbt_tx_fn || !payload || len == 0) return -1;
    if (len > BBT_ACL_MTU_MAX) return -1;

    uint8_t pkt[sizeof(bbt_hci_acl_hdr_t) + BBT_ACL_MTU_MAX];
    bbt_hci_acl_hdr_t *hdr = (bbt_hci_acl_hdr_t *)pkt;
    hdr->h4_type = HCI_H4_ACL;
    hdr->handle_flags = (handle & 0x0FFFu) | ((uint16_t)(pb_flag & 0x03u) << 12);
    hdr->data_len = len;

    memcpy(pkt + sizeof(bbt_hci_acl_hdr_t), payload, len);
    size_t total_len = sizeof(bbt_hci_acl_hdr_t) + len;
    return g_bbt_tx_fn(pkt, total_len);
}

/* ── Receive HCI Event Handler ────────────────────────────────────── */

void bbt_hci_on_rx_event(const uint8_t *evt_data, size_t len) {
    if (!evt_data || len < sizeof(bbt_hci_evt_hdr_t)) return;

    const bbt_hci_evt_hdr_t *hdr = (const bbt_hci_evt_hdr_t *)evt_data;
    const uint8_t *params = evt_data + sizeof(bbt_hci_evt_hdr_t);
    size_t p_len = len - sizeof(bbt_hci_evt_hdr_t);
    if (p_len < hdr->param_len) return;

    switch (hdr->event_code) {
        case HCI_EV_CMD_COMPLETE:
            /* Command Complete: param 0 is Num_HCI_Cmd_Packets, 1..2 is Opcode, 3 is Status */
            break;

        case HCI_EV_CONN_COMPLETE:
            if (p_len >= 11 && params[0] == 0 /* Success */) {
                s_hci_conn.connected = true;
                s_hci_conn.handle = (uint16_t)params[1] | ((uint16_t)params[2] << 8);
                /* Initiate L2CAP connection for AVDTP Signaling (PSM 0x0019) */
                bbt_l2cap_connect_req(s_hci_conn.handle, L2CAP_PSM_AVDTP, 0x0041);
            }
            break;

        case HCI_EV_DISCONN_COMPLETE:
            s_hci_conn.connected = false;
            s_hci_conn.handle = 0;
            break;

        default:
            break;
    }
}

/* ── Receive HCI ACL Handler ──────────────────────────────────────── */

void bbt_hci_on_rx_acl(const uint8_t *acl_data, size_t len) {
    if (!acl_data || len < sizeof(bbt_hci_acl_hdr_t)) return;

    const bbt_hci_acl_hdr_t *hdr = (const bbt_hci_acl_hdr_t *)acl_data;
    uint16_t handle = hdr->handle_flags & 0x0FFFu;
    const uint8_t *l2cap_frame = acl_data + sizeof(bbt_hci_acl_hdr_t);
    size_t frame_len = len - sizeof(bbt_hci_acl_hdr_t);

    if (frame_len >= hdr->data_len) {
        bbt_l2cap_on_rx_frame(handle, l2cap_frame, hdr->data_len);
    }
}

