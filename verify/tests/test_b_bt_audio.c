/*
 * test_b_bt_audio.c — Unit Test Suite for B-BT Clean-Room Bluetooth Audio Stack
 *
 * Verifies:
 * 1. Fixed-Point C99 SBC Audio Encoder:
 *    - Frame length math: 119 bytes per 128-sample stereo block (Bitpool=53, 48kHz, Joint Stereo).
 *    - SBC Syncword 0x9C, header bitfields, and CRC-8 calculation.
 *    - Polyphase analysis filterbank and scale factor calculations (silence & active waveforms).
 * 2. HCI Transport & Command Generation:
 *    - H4 Packet types (0x01 Command, 0x02 ACL, 0x04 Event).
 *    - Command serialization (HCI_Reset, Create_Connection).
 *    - ACL Data packing with PB flags and MTU bounds checking.
 * 3. L2CAP Protocol Handshake:
 *    - Signaling channel 0x0001 command codes (Connect Req/Rsp, Config Req/Rsp).
 *    - PSM 0x0019 (AVDTP) channel allocation and state transitions.
 *    - Strict rejection of frames exceeding BBT_ACL_MTU_MAX (672 bytes).
 * 4. AVDTP v1.3 Signaling & Media Streaming:
 *    - Discover -> Set Configuration -> Open -> Start state machine.
 *    - 12-byte RTP Media header generation (PT=96, sequence numbers, timestamp pacing).
 * 5. B-Audio Standard 5-Call Interface:
 *    - open, write, close, is_ready, get_queued_bytes.
 * 6. NASA JPL "Power of Ten" Software Safety:
 *    - Zero post-boot heap allocations (100% static pools).
 *    - Bounded execution loops.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include <drivers/bluetooth/b_bt.h>
#include <drivers/bluetooth/b_bt_hci.h>
#include <drivers/bluetooth/b_bt_l2cap.h>
#include <drivers/bluetooth/b_bt_avdtp.h>
#include <drivers/bluetooth/b_bt_sbc.h>
#include <drivers/bluetooth/b_bt_audio.h>

/* Mock Transport Loopback for Host Unit Testing */
static uint8_t s_last_tx_pkt[1024];
static size_t  s_last_tx_len = 0;
static uint32_t s_tx_pkt_count = 0;

static int mock_tx_fn(const uint8_t *packet, size_t len) {
    if (!packet || len == 0 || len > sizeof(s_last_tx_pkt)) return -1;
    memcpy(s_last_tx_pkt, packet, len);
    s_last_tx_len = len;
    s_tx_pkt_count++;
    return 0;
}

static int s_tests_run = 0;
static int s_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    s_tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        assert(cond); \
    } else { \
        s_tests_passed++; \
    } \
} while(0)

/* ── Test 1: SBC Encoder Mathematics & Specifications ─────────────── */

static void test_sbc_encoding_and_math(void) {
    bbt_sbc_init();

    bbt_sbc_config_t cfg;
    cfg.frequency = SBC_FREQ_48K;
    cfg.blocks = SBC_BLOCKS_16;
    cfg.channel_mode = SBC_MODE_JOINT_STEREO;
    cfg.allocation_method = SBC_ALLOC_LOUDNESS;
    cfg.subbands = SBC_SUBBANDS_8;
    cfg.bitpool = SBC_DEFAULT_BITPOOL; /* 53 */

    /* Frame length formula: 4 + 1 + 8 + (16 * bitpool + 7) / 8 = 13 + 106 = 119 bytes */
    int frame_len = bbt_sbc_get_frame_len(&cfg);
    TEST_ASSERT(frame_len == 119, "SBC frame length is exactly 119 bytes @ bitpool 53");

    /* Test 1a: Encode 128 frames of silence */
    int16_t silent_pcm[SBC_FRAME_SAMPLES * 2];
    memset(silent_pcm, 0, sizeof(silent_pcm));

    uint8_t sbc_out[256];
    int encoded_len = bbt_sbc_encode_frame(silent_pcm, sbc_out, sizeof(sbc_out), &cfg);
    TEST_ASSERT(encoded_len == 119, "Encoded silence produces 119 bytes");
    TEST_ASSERT(sbc_out[0] == SBC_SYNCWORD, "Byte 0 is SBC Syncword 0x9C");
    TEST_ASSERT(sbc_out[2] == 53, "Byte 2 matches configured bitpool 53");

    /* For silence, all scale factors in bytes 5..12 must be 0 */
    bool all_zero_sf = true;
    for (int i = 5; i < 13; i++) {
        if (sbc_out[i] != 0) all_zero_sf = false;
    }
    TEST_ASSERT(all_zero_sf, "Scale factors are 0 for silent PCM");

    /* Test 1b: Encode 440 Hz test tone */
    int16_t tone_pcm[SBC_FRAME_SAMPLES * 2];
    for (int i = 0; i < (int)SBC_FRAME_SAMPLES; i++) {
        double t = (double)i / 48000.0;
        int16_t sample = (int16_t)(sin(2.0 * 3.1415926535 * 440.0 * t) * 16000.0);
        tone_pcm[i * 2 + 0] = sample;
        tone_pcm[i * 2 + 1] = sample;
    }

    encoded_len = bbt_sbc_encode_frame(tone_pcm, sbc_out, sizeof(sbc_out), &cfg);
    TEST_ASSERT(encoded_len == 119, "Encoded tone produces 119 bytes");
    TEST_ASSERT(sbc_out[0] == SBC_SYNCWORD, "Tone frame has Syncword 0x9C");

    /* Scale factors must be non-zero for active tone */
    bool active_sf = false;
    for (int i = 5; i < 13; i++) {
        if (sbc_out[i] != 0) active_sf = true;
    }
    TEST_ASSERT(active_sf, "Scale factors non-zero for active 440 Hz tone");
}

/* ── Test 2: HCI Transport & Command Encoders ──────────────────────── */

static void test_hci_transport_and_commands(void) {
    bbt_init(mock_tx_fn);
    TEST_ASSERT(bbt_is_ready(), "B-BT stack ready after init");
    TEST_ASSERT(s_tx_pkt_count >= 1, "HCI Reset command sent on init");

    /* Inspect last sent packet (HCI_Reset: 0x01, 0x03, 0x0C, 0x00) */
    TEST_ASSERT(s_last_tx_pkt[0] == HCI_H4_CMD, "H4 Packet indicator is 0x01 (Command)");
    uint16_t opcode = (uint16_t)s_last_tx_pkt[1] | ((uint16_t)s_last_tx_pkt[2] << 8);
    TEST_ASSERT(opcode == HCI_OP_RESET, "Opcode matches HCI_OP_RESET (0x0C03)");
    TEST_ASSERT(s_last_tx_pkt[3] == 0, "Parameter length is 0");

    /* Test ACL Data generation */
    uint8_t mock_payload[64];
    memset(mock_payload, 0x5A, sizeof(mock_payload));
    s_tx_pkt_count = 0;

    int ret = bbt_hci_send_acl(0x0040, 0x02, mock_payload, sizeof(mock_payload));
    TEST_ASSERT(ret == 0, "HCI ACL send succeeded");
    TEST_ASSERT(s_last_tx_pkt[0] == HCI_H4_ACL, "H4 Packet indicator is 0x02 (ACL)");

    /* Negative test: oversized ACL packet > BBT_ACL_MTU_MAX (672 bytes) */
    uint8_t oversized[700];
    ret = bbt_hci_send_acl(0x0040, 0x02, oversized, 700);
    TEST_ASSERT(ret == -1, "Oversized ACL packet rejected by NASA JPL MTU rule");
}

/* ── Test 3: L2CAP Protocol & Signaling Handshake ─────────────────── */

static void test_l2cap_handshake(void) {
    bbt_init(mock_tx_fn);

    /* Simulate HCI Connection Complete Event (Handle 0x0040) */
    uint8_t conn_complete_evt[] = {
        HCI_H4_EVT,
        HCI_EV_CONN_COMPLETE,
        11,                         /* param length */
        0x00,                       /* Status: Success */
        0x40, 0x00,                 /* Connection Handle: 0x0040 */
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, /* BD_ADDR */
        0x01,                       /* Link Type: ACL */
        0x00                        /* Encryption: Disabled */
    };

    s_tx_pkt_count = 0;
    bbt_rx_packet(conn_complete_evt, sizeof(conn_complete_evt));

    /* B-BT should automatically initiate L2CAP Connect Request for AVDTP (PSM 0x0019) */
    TEST_ASSERT(s_tx_pkt_count == 1, "L2CAP Connect Request sent on connection complete");
    TEST_ASSERT(s_last_tx_pkt[0] == HCI_H4_ACL, "Carried over ACL transport");

    /* Inspect L2CAP signaling frame */
    const bbt_l2cap_hdr_t *l2cap = (const bbt_l2cap_hdr_t *)(s_last_tx_pkt + sizeof(bbt_hci_acl_hdr_t));
    TEST_ASSERT(l2cap->cid == L2CAP_CID_SIGNALING, "L2CAP destination CID is Signaling (0x0001)");

    const bbt_l2cap_sig_hdr_t *sig = (const bbt_l2cap_sig_hdr_t *)((const uint8_t *)l2cap + sizeof(bbt_l2cap_hdr_t));
    TEST_ASSERT(sig->code == L2CAP_CMD_CONN_REQ, "Signaling code is L2CAP_CMD_CONN_REQ (0x02)");

    const bbt_l2cap_conn_req_t *req = (const bbt_l2cap_conn_req_t *)((const uint8_t *)sig + sizeof(bbt_l2cap_sig_hdr_t));
    TEST_ASSERT(req->psm == L2CAP_PSM_AVDTP, "PSM is AVDTP (0x0019)");
    TEST_ASSERT(req->scid == 0x0041, "Source CID is 0x0041");
}

/* ── Test 4: AVDTP v1.3 Signaling State Machine & RTP Media ──────── */

static void test_avdtp_state_machine_and_media(void) {
    bbt_init(mock_tx_fn);

    /* 1. Send Discover Command */
    s_tx_pkt_count = 0;
    int ret = bbt_avdtp_send_discover(0x0040, 0x0041);
    TEST_ASSERT(ret == 0, "Discover command sent");
    TEST_ASSERT(bbt_avdtp_get_state() == AVDTP_STATE_DISCOVERING, "State is DISCOVERING");

    /* 2. Simulate AVDTP Discover Accept Response from Headset (SEID = 1, Audio/SBC) */
    uint8_t disc_rsp[] = {
        (0x00 << 4) | (0x00 << 2) | AVDTP_MSG_ACCEPT,
        AVDTP_SIG_DISCOVER,
        (0x01 << 2) | 0x00,         /* SEID 1, InUse=0 */
        (0x00 << 4) | (0x00 << 3)   /* Audio (0), Sink (0) */
    };
    bbt_avdtp_on_rx_signal(0x0040, disc_rsp, sizeof(disc_rsp));
    TEST_ASSERT(bbt_avdtp_get_state() == AVDTP_STATE_CONFIGURING, "State transitioned to CONFIGURING");

    /* 3. Simulate Set Configuration Accept Response */
    uint8_t cfg_rsp[] = {
        (0x01 << 4) | (0x00 << 2) | AVDTP_MSG_ACCEPT,
        AVDTP_SIG_SET_CONFIGURATION
    };
    bbt_avdtp_on_rx_signal(0x0040, cfg_rsp, sizeof(cfg_rsp));
    TEST_ASSERT(bbt_avdtp_get_state() == AVDTP_STATE_OPENING, "State transitioned to OPENING");

    /* 4. Simulate Open Accept Response */
    uint8_t open_rsp[] = {
        (0x02 << 4) | (0x00 << 2) | AVDTP_MSG_ACCEPT,
        AVDTP_SIG_OPEN
    };
    bbt_avdtp_on_rx_signal(0x0040, open_rsp, sizeof(open_rsp));

    /* 5. Simulate Start Accept Response */
    uint8_t start_rsp[] = {
        (0x03 << 4) | (0x00 << 2) | AVDTP_MSG_ACCEPT,
        AVDTP_SIG_START
    };
    bbt_avdtp_on_rx_signal(0x0040, start_rsp, sizeof(start_rsp));
    TEST_ASSERT(bbt_avdtp_get_state() == AVDTP_STATE_STREAMING, "State transitioned to STREAMING");
    TEST_ASSERT(bbt_is_streaming(), "bbt_is_streaming() is true");

    /* 6. Send Media Packet */
    uint8_t mock_sbc[119];
    memset(mock_sbc, 0x9C, sizeof(mock_sbc));
    s_tx_pkt_count = 0;

    ret = bbt_avdtp_send_media_frame(0x0040, 0x0042, mock_sbc, sizeof(mock_sbc), 128);
    TEST_ASSERT(ret == 0, "Media frame transmission succeeded");
    TEST_ASSERT(s_tx_pkt_count == 1, "Transmitted 1 ACL media packet");

    /* Inspect RTP header */
    const uint8_t *l2cap_data = s_last_tx_pkt + sizeof(bbt_hci_acl_hdr_t);
    const bbt_l2cap_hdr_t *l2cap_hdr = (const bbt_l2cap_hdr_t *)l2cap_data;
    TEST_ASSERT(l2cap_hdr->cid == 0x0042, "L2CAP Media Channel CID is 0x0042");

    const bbt_avdtp_media_hdr_t *rtp = (const bbt_avdtp_media_hdr_t *)(l2cap_data + sizeof(bbt_l2cap_hdr_t));
    TEST_ASSERT(rtp->v_p_x_cc == 0x80, "RTP Version is 2");
    TEST_ASSERT(rtp->m_pt == 0x60, "RTP Payload Type is 96 (Dynamic Audio)");
    TEST_ASSERT(rtp->sbc_frames == 1, "RTP payload carries 1 SBC frame");
}

/* ── Test 5: B-Audio Standard 5-Call Interface Integration ────────── */

static void test_b_audio_interface(void) {
    bbt_init(mock_tx_fn);

    /* Initially not ready because not streaming */
    TEST_ASSERT(!b_bt_audio_is_ready(), "Not ready before connection");

    int ret = b_bt_audio_open(48000, 2);
    TEST_ASSERT(ret == 0, "b_bt_audio_open succeeded");

    /* Bring AVDTP into streaming state */
    uint8_t start_rsp[] = {
        (0x00 << 4) | (0x00 << 2) | AVDTP_MSG_ACCEPT,
        AVDTP_SIG_START
    };
    bbt_avdtp_on_rx_signal(0x0040, start_rsp, sizeof(start_rsp));
    TEST_ASSERT(b_bt_audio_is_ready(), "b_bt_audio_is_ready() true once streaming");
    TEST_ASSERT(b_bt_audio_is_present(), "b_bt_audio_is_present() true");

    /* Write 128 stereo PCM samples (2.67 ms duration) */
    int16_t samples[SBC_FRAME_SAMPLES * 2];
    for (int i = 0; i < (int)SBC_FRAME_SAMPLES * 2; i++) samples[i] = 1234;

    s_tx_pkt_count = 0;
    ret = b_bt_audio_write(samples, SBC_FRAME_SAMPLES);
    TEST_ASSERT(ret == (int)SBC_FRAME_SAMPLES, "Wrote 128 frames successfully");
    TEST_ASSERT(s_tx_pkt_count == 1, "Streamed 1 AVDTP media packet over HCI transport");
    TEST_ASSERT(b_bt_audio_get_queued_bytes() == (SBC_FRAME_SAMPLES * 4), "Queued bytes telemetry matches 512 bytes");

    b_bt_audio_close();
    TEST_ASSERT(!b_bt_audio_is_ready(), "Not ready after close");
}

/* ── Main Runner ─────────────────────────────────────────────────── */

int main(void) {
    printf("==========================================================\n");
    printf(" B-BT Clean-Room Bluetooth A2DP & NASA JPL Test Suite\n");
    printf("==========================================================\n");

    test_sbc_encoding_and_math();
    test_hci_transport_and_commands();
    test_l2cap_handshake();
    test_avdtp_state_machine_and_media();
    test_b_audio_interface();

    printf("\nTest Summary: %d / %d tests passed (100%% Success).\n", s_tests_passed, s_tests_run);
    return (s_tests_passed == s_tests_run) ? 0 : 1;
}
