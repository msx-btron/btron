/*
 * chat_core.c — Shared Headless B-System Contacts / Phone / Chat Data Store
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "chat_core.h"

static void ch_strlcpy(char *dst, int cap, const char *src)
{
    int i = 0;
    if (!dst || cap <= 0) return;
    if (src) {
        for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static const CHAT_CONTACT s_contacts[] = {
    { "Ken Sakamura",      "090-2841-0320", 1 },
    { "Ada Lovelace",      "LoRa 04",       1 },
    { "Grace Hopper",      "LoRa 11",       1 },
    { "Claude Shannon",    "LoRa 27",       0 },
    { "Margaret Hamilton", "LoRa 31",       0 },
    { "Ken Thompson",      "LoRa 08",       0 },
    { "Barbara Liskov",    "LoRa 19",       0 }
};

static char s_peer_name[SEGUI_LABEL_MAX] = "Ken Sakamura";
static char s_peer_addr[SEGUI_LABEL_MAX] = "090-2841-0320";
static char s_dial_num[SEGUI_LABEL_MAX]  = "090-2841-0320";

static SEGUI_MSG s_msgs[SEGUI_MSG_MAX] = {
    { SEGUI_MSG_NOTE, "10:24  LoRa CH 23 / SF9" },
    { SEGUI_MSG_THEM, "Did the PicoCalc beacon land?" },
    { SEGUI_MSG_ME,   "3 packets, all ACKed." },
    { SEGUI_MSG_THEM, "RSSI is -84 this far out." },
    { SEGUI_MSG_ME,   "Running B-System on RP2350." },
    { SEGUI_MSG_THEM, "320x320 Palm SegUI looks sharp!" },
    { SEGUI_MSG_NOTE, "Read 10:29" }
};
static int s_n_msg  = 7;
static int s_inited = 0;

void chat_core_init(void)
{
    if (s_inited) return;
    s_inited = 1;
}

int chat_core_contact_count(void)
{
    return (int)(sizeof(s_contacts) / sizeof(s_contacts[0]));
}

const CHAT_CONTACT *chat_core_contact(int idx)
{
    if (idx < 0 || idx >= chat_core_contact_count()) return NULL;
    return &s_contacts[idx];
}

void chat_core_select_peer(const char *name, const char *addr)
{
    if (name && name[0]) ch_strlcpy(s_peer_name, SEGUI_LABEL_MAX, name);
    if (addr && addr[0]) {
        ch_strlcpy(s_peer_addr, SEGUI_LABEL_MAX, addr);
        ch_strlcpy(s_dial_num,  SEGUI_LABEL_MAX, addr);
    }
}

const char *chat_core_peer_name(void) { return s_peer_name; }
const char *chat_core_peer_addr(void) { return s_peer_addr; }

void chat_core_set_dial(const char *num)
{
    ch_strlcpy(s_dial_num, SEGUI_LABEL_MAX, num ? num : "");
}

const char *chat_core_dial_str(void) { return s_dial_num; }

void chat_core_log_call(const char *name, const char *when)
{
    (void)name; (void)when;
}

SEGUI_MSG *chat_core_msgs(int *out_count)
{
    if (out_count) *out_count = s_n_msg;
    return s_msgs;
}

void chat_core_send_msg(const char *text)
{
    if (!text || !text[0]) return;
    if (s_n_msg >= SEGUI_MSG_MAX) {
        for (int i = 0; i < SEGUI_MSG_MAX - 1; i++) s_msgs[i] = s_msgs[i + 1];
        s_n_msg = SEGUI_MSG_MAX - 1;
    }
    s_msgs[s_n_msg].who = SEGUI_MSG_ME;
    ch_strlcpy(s_msgs[s_n_msg].text, SEGUI_TEXT_MAX, text);
    s_n_msg++;
}
