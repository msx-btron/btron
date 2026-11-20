/*
 * test_segui_cores.c — Unit & Integration Gate for Shared Real B-System Engines
 *
 * Verifies cabinet_core, teditor_core, tad_parser, chat_core, and shell_core
 * end-to-end with zero heap allocation and zero WND dependencies.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <btron/ui_mode.h>
#include "../../src/apps/cabinet/cabinet_core.h"
#include "../../src/apps/editor/teditor_core.h"
#include "../../src/apps/browser/tad_parser.h"
#include "../../src/apps/chat/chat_core.h"
#include "../../src/apps/terminal/shell_core.h"

BTRON_UI_MODE g_btron_ui_mode = BTRON_UI_SEGUI;

static char s_shell_out[1024];
static void cap_out(const char *s, void *ctx)
{
    (void)ctx;
    strncat(s_shell_out, s, sizeof(s_shell_out) - strlen(s_shell_out) - 1);
}

int main(void)
{
    /* 1. UI mode contract */
    assert(btron_ui_is_segui() == 1);

    /* 2. Cabinet real /SYS BROM volume (zero mocks) */
    cabinet_core_init();
    assert(cabinet_core_count() >= 5);
    assert(cabinet_core_find("NORTIA.TAD") >= 0);
    assert(cabinet_core_find("OSAL_BOOK.TAD") >= 0);
    assert(cabinet_core_find("HEART_SUTRA.TAD") >= 0);
    assert(cabinet_core_find("PICO.TXT") >= 0);
    assert(cabinet_core_find("BOOT.LOG") >= 0);
    assert(cabinet_core_write("OSAL_BOOK.TAD", "x", 1) < 0); /* RO Flash protected */

    /* 2b. BOOT.LOG telemetry append + read */
    cabinet_bootlog_append("[BOOT] test-segui-cores OK\n");
    uint32_t bl_sz = 0;
    const char *bl = cabinet_core_read(cabinet_core_find("BOOT.LOG"), &bl_sz);
    assert(bl && bl_sz > 10 && strstr(bl, "[BOOT] test-segui-cores OK") != NULL);

    /* 3. TEditorCore load -> edit -> save -> Cabinet roundtrip */
    static char ed_buf[32 * 40];
    static char save_buf[1024];
    TEditorCore ed;
    ted_core_init(&ed, ed_buf, 40, 32);
    ted_core_load(&ed, "Hello");
    assert(ted_core_insert_char(&ed, '!'));
    assert(ted_core_newline(&ed));
    assert(ted_core_insert_char(&ed, 'B'));
    assert(ted_core_word_count(&ed) == 2);
    uint32_t sz = ted_core_save(&ed, save_buf, sizeof(save_buf));
    assert(strcmp(save_buf, "Hello!\nB") == 0);
    assert(cabinet_core_write("NORTIA.TAD", save_buf, sz) == 0);

    /* 4. ShellCore `ls`, `cat NORTIA.TAD`, `diag` over live Cabinet */
    BtronShellHooks hooks = { "RP2350", "Cortex-M33", NULL, NULL, NULL };
    s_shell_out[0] = '\0';
    assert(btron_shell_core_exec("cat NORTIA.TAD", cap_out, NULL, &hooks) == 1);
    assert(strstr(s_shell_out, "Hello!") != NULL);

    s_shell_out[0] = '\0';
    assert(btron_shell_core_exec("ls", cap_out, NULL, &hooks) == 1);
    assert(strstr(s_shell_out, "OSAL_BOOK.TAD") != NULL);

    s_shell_out[0] = '\0';
    assert(btron_shell_core_exec("diag", cap_out, NULL, &hooks) == 1);
    assert(strstr(s_shell_out, "[BOOT] test-segui-cores OK") != NULL);

    /* 5. TAD Parser binary TAD decoding (OSAL_BOOK.TAD) + TS_VOBJ link extraction */
    static char tad_buf[40 * 108];
    TAD_VIEW tv;
    tv.lines = tad_buf;
    tv.stride = 108;
    tv.max_lines = 40;
    uint32_t osal_sz = 0;
    const char *osal = cabinet_core_read(cabinet_core_find("OSAL_BOOK.TAD"), &osal_sz);
    assert(osal && osal_sz > 1000);
    tad_parse_stream(&tv, (const uint8_t *)osal, osal_sz, 36);
    assert(tv.n_lines > 10);
    assert(tv.n_links >= 1);
    assert(strcmp(tv.links[0].target, "dkar_chag.tad") == 0);

    /* 6. ChatCore Contacts + Phone + Chat state */
    chat_core_init();
    assert(chat_core_contact_count() >= 5);
    chat_core_select_peer("Ada Lovelace", "LoRa 04");
    assert(strcmp(chat_core_peer_name(), "Ada Lovelace") == 0);
    assert(strcmp(chat_core_dial_str(), "LoRa 04") == 0);
    int n0 = 0, n1 = 0;
    chat_core_msgs(&n0);
    chat_core_send_msg("Ping from PicoCalc");
    SEGUI_MSG *msgs = chat_core_msgs(&n1);
    assert(n1 == n0 + 1);
    assert(strcmp(msgs[n1 - 1].text, "Ping from PicoCalc") == 0);

    printf("[PASS] test-segui-cores: all 5 shared B-System engines verified!\n");
    return 0;
}
