/*
 * B-BT: HCI (Host Controller Interface) Header
 * Conforming to Bluetooth Core Spec v5.0, Volume 2 & 4 (H4 Transport)
 * NASA JPL Safety Rules: Strictly typed packed structures, checked return values
 */

#ifndef _BTRON_B_BT_HCI_H_
#define _BTRON_B_BT_HCI_H_

#include <drivers/bluetooth/b_bt.h>

/* H4 Packet Indicators */
#define HCI_H4_CMD        0x01u
#define HCI_H4_ACL        0x02u
#define HCI_H4_SCO        0x03u
#define HCI_H4_EVT        0x04u

/* OGF (Opcode Group Field) */
#define HCI_OGF_LINK_CTL  0x01u
#define HCI_OGF_HOST_CTL  0x03u
#define HCI_OGF_INFO_PAR  0x04u
#define HCI_OGF_VENDOR    0x3Fu

/* HCI Command Opcodes: (OGF << 10) | OCF */
#define HCI_OP_RESET                    ((HCI_OGF_HOST_CTL << 10) | 0x0003u)
#define HCI_OP_SET_EVENT_MASK           ((HCI_OGF_HOST_CTL << 10) | 0x0001u)
#define HCI_OP_READ_BD_ADDR             ((HCI_OGF_INFO_PAR << 10) | 0x0001u)
#define HCI_OP_WRITE_SCAN_ENABLE        ((HCI_OGF_HOST_CTL << 10) | 0x001Au)
#define HCI_OP_CREATE_CONNECTION        ((HCI_OGF_LINK_CTL << 10) | 0x0005u)
#define HCI_OP_DISCONNECT               ((HCI_OGF_LINK_CTL << 10) | 0x0006u)

/* HCI Event Codes */
#define HCI_EV_CONN_COMPLETE            0x03u
#define HCI_EV_DISCONN_COMPLETE         0x05u
#define HCI_EV_CMD_COMPLETE             0x0Eu
#define HCI_EV_CMD_STATUS               0x0Fu
#define HCI_EV_NUM_COMPLETED_PACKETS    0x13u

/* Packet Structures */
typedef struct {
    uint8_t  h4_type;       /* 0x01 */
    uint16_t opcode;        /* OGF (6 bits) + OCF (10 bits) */
    uint8_t  param_len;
} __attribute__((packed)) bbt_hci_cmd_hdr_t;

typedef struct {
    uint8_t  h4_type;       /* 0x04 */
    uint8_t  event_code;
    uint8_t  param_len;
} __attribute__((packed)) bbt_hci_evt_hdr_t;

typedef struct {
    uint8_t  h4_type;       /* 0x02 */
    uint16_t handle_flags;  /* Handle (12 bits) | PB (2 bits) | BC (2 bits) */
    uint16_t data_len;
} __attribute__((packed)) bbt_hci_acl_hdr_t;

/* Public Functions */
int  bbt_hci_send_cmd(uint16_t opcode, const void *param, uint8_t param_len);
int  bbt_hci_send_acl(uint16_t handle, uint8_t pb_flag, const void *payload, uint16_t len);
void bbt_hci_on_rx_event(const uint8_t *evt_data, size_t len);
void bbt_hci_on_rx_acl(const uint8_t *acl_data, size_t len);

#endif /* _BTRON_B_BT_HCI_H_ */

