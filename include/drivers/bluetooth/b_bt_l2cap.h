/*
 * B-BT: L2CAP (Logical Link Control and Adaptation Protocol) Header
 * Conforming to Bluetooth Core Spec v5.0, Volume 3, Part A
 * NASA JPL Safety Rules: Static channels, bounded frame sizes
 */

#ifndef _BTRON_B_BT_L2CAP_H_
#define _BTRON_B_BT_L2CAP_H_

#include <drivers/bluetooth/b_bt.h>

#define L2CAP_CID_SIGNALING          0x0001u
#define L2CAP_PSM_AVDTP              0x0019u

/* L2CAP Signaling Command Codes */
#define L2CAP_CMD_CONN_REQ           0x02u
#define L2CAP_CMD_CONN_RSP           0x03u
#define L2CAP_CMD_CFG_REQ            0x04u
#define L2CAP_CMD_CFG_RSP            0x05u
#define L2CAP_CMD_DISC_REQ           0x06u
#define L2CAP_CMD_DISC_RSP           0x07u

/* Standard Header Structures */
typedef struct {
    uint16_t length;
    uint16_t cid;
} __attribute__((packed)) bbt_l2cap_hdr_t;

typedef struct {
    uint8_t  code;
    uint8_t  identifier;
    uint16_t length;
} __attribute__((packed)) bbt_l2cap_sig_hdr_t;

typedef struct {
    uint16_t psm;
    uint16_t scid;
} __attribute__((packed)) bbt_l2cap_conn_req_t;

typedef struct {
    uint16_t dcid;
    uint16_t scid;
    uint16_t result;
    uint16_t status;
} __attribute__((packed)) bbt_l2cap_conn_rsp_t;

typedef struct {
    uint16_t dcid;
    uint16_t flags;
} __attribute__((packed)) bbt_l2cap_cfg_req_t;

typedef struct {
    uint16_t scid;
    uint16_t flags;
    uint16_t result;
} __attribute__((packed)) bbt_l2cap_cfg_rsp_t;

/* Channel Structure (Static Pool) */
typedef struct {
    bool     active;
    uint16_t handle;
    uint16_t scid;
    uint16_t dcid;
    uint16_t psm;
    bool     configured;
} bbt_l2cap_chan_t;

/* Public Functions */
void bbt_l2cap_init(void);
int  bbt_l2cap_connect_req(uint16_t handle, uint16_t psm, uint16_t scid);
int  bbt_l2cap_send(uint16_t handle, uint16_t dcid, const void *data, uint16_t len);
void bbt_l2cap_on_rx_frame(uint16_t handle, const uint8_t *frame, size_t len);

#endif /* _BTRON_B_BT_L2CAP_H_ */

