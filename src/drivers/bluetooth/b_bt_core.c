/*
 * B-BT: Bluetooth Core Subsystem Coordinator (src/drivers/bluetooth/b_bt_core.c)
 * Conforming to Bluetooth Core Spec v5.0
 * NASA JPL "Power of Ten" Software Safety Rules:
 *   - Zero dynamic heap allocation
 *   - Single static coordinator state
 *   - Functions <= 60 lines
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <drivers/bluetooth/b_bt.h>
#include <drivers/bluetooth/b_bt_hci.h>
#include <drivers/bluetooth/b_bt_l2cap.h>
#include <drivers/bluetooth/b_bt_avdtp.h>
#include <drivers/bluetooth/b_bt_sbc.h>

bbt_tx_fn_t g_bbt_tx_fn = NULL;
static bbt_state_t s_bbt_state = BBT_STATE_DOWN;

void bbt_init(bbt_tx_fn_t tx_fn) {
    g_bbt_tx_fn = tx_fn;
    s_bbt_state = BBT_STATE_INITIALIZING;

    bbt_l2cap_init();
    bbt_avdtp_init();
    bbt_sbc_init();

    /* Send standard HCI Reset */
    bbt_hci_send_cmd(HCI_OP_RESET, NULL, 0);
    s_bbt_state = BBT_STATE_READY;
}

bbt_state_t bbt_get_state(void) {
    if (bbt_avdtp_get_state() == AVDTP_STATE_STREAMING) {
        return BBT_STATE_STREAMING;
    }
    return s_bbt_state;
}

bool bbt_is_ready(void) {
    return (s_bbt_state >= BBT_STATE_READY);
}

bool bbt_is_streaming(void) {
    return (bbt_avdtp_get_state() == AVDTP_STATE_STREAMING);
}

void bbt_rx_packet(const uint8_t *packet, size_t len) {
    if (!packet || len < 2) return;
    uint8_t h4 = packet[0];

    if (h4 == HCI_H4_EVT) {
        bbt_hci_on_rx_event(packet, len);
    } else if (h4 == HCI_H4_ACL) {
        bbt_hci_on_rx_acl(packet, len);
    }
}

