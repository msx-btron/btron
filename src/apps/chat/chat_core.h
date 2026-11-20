/*
 * chat_core.h — Shared Headless B-System Contacts / Phone / Chat Data Store
 *
 * Zero-WND, zero-calloc state shared by Contacts, Phone (Dialer/Recents),
 * and Chat (LoRa Mesh Thread/Circles).
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_CHAT_CORE_H_
#define _BTRON_CHAT_CORE_H_

#include <btron/segui.h>

#define CHAT_CORE_MAX_CONTACTS  10
#define CHAT_CORE_MAX_RECENTS    8

typedef struct {
    char name[SEGUI_LABEL_MAX];
    char addr[SEGUI_LABEL_MAX];
    int  favourite;
} CHAT_CONTACT;

void               chat_core_init(void);
int                chat_core_contact_count(void);
const CHAT_CONTACT *chat_core_contact(int idx);
void               chat_core_select_peer(const char *name, const char *addr);
const char        *chat_core_peer_name(void);
const char        *chat_core_peer_addr(void);
void               chat_core_set_dial(const char *num);
const char        *chat_core_dial_str(void);
void               chat_core_log_call(const char *name, const char *when);
SEGUI_MSG         *chat_core_msgs(int *out_count);
void               chat_core_send_msg(const char *text);

#endif /* _BTRON_CHAT_CORE_H_ */
