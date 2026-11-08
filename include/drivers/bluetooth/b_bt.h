/*
 * B-BT: BTRON Clean-Room Bluetooth Stack
 * Master definitions & NASA JPL "Power of Ten" safety contracts
 * Conforming to Bluetooth Core v5.0 / A2DP v1.3.2 Specification
 */

#ifndef _BTRON_B_BT_H_
#define _BTRON_B_BT_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BBT_ASSERT(cond) do { if (!(cond)) return -1; } while (0)
#define BBT_ASSERT_VOID(cond) do { if (!(cond)) return; } while (0)

/* NASA JPL Rule 3: Static, strictly bounded capacity limits */
#define BBT_MAX_ACL_CONNS    2u   /* 1 Audio Sink + 1 Diagnostic Channel */
#define BBT_MAX_L2CAP_CHANS  4u   /* Signaling, AVDTP-Sig, AVDTP-Media, Control */
#define BBT_MAX_SEPS         4u   /* Discovered Stream Endpoints */
#define BBT_TX_RING_FRAMES   4u   /* 4-frame SPSC transmit ring (deterministic latency) */
#define BBT_ACL_MTU_MAX      672u /* Deterministic maximum transmission unit */

/* 48-bit Bluetooth Device Address */
typedef struct {
    uint8_t bytes[6];
} __attribute__((packed)) bbt_bd_addr_t;

/* Connection States */
typedef enum {
    BBT_STATE_DOWN = 0,
    BBT_STATE_INITIALIZING,
    BBT_STATE_READY,
    BBT_STATE_CONNECTING_ACL,
    BBT_STATE_ACL_CONNECTED,
    BBT_STATE_AVDTP_CONFIGURED,
    BBT_STATE_STREAMING,
    BBT_STATE_DISCONNECTING
} bbt_state_t;

/* Transport transmit hook */
typedef int (*bbt_tx_fn_t)(const uint8_t *packet, size_t len);

/* B-BT Subsystem Core Functions */
void        bbt_init(bbt_tx_fn_t tx_fn);
bbt_state_t bbt_get_state(void);
bool        bbt_is_ready(void);
bool        bbt_is_streaming(void);
void        bbt_rx_packet(const uint8_t *packet, size_t len);

#endif /* _BTRON_B_BT_H_ */

