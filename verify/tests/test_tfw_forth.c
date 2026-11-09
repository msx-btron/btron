/*
 * B-System BTRON3 / T-FW — test_tfw_forth.c
 * Verification Test for T-FW (T-Firmware) MSX Z80 OpenFirmware / Forth ROM
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <btron/tronmsx.h>
#include "../../src/emulators/msx/msx_int.h"
#include "../../src/emulators/msx/msx_cfg.h"
#include "../../src/emulators/msx/msx_kbd.h"

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_total++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
        assert(cond); \
    } \
} while(0)

int main(void) {
    printf("==========================================================\n");
    printf(" Running T-FW (T-Firmware) Z80 OpenFirmware Verification...\n");
    printf("==========================================================\n");

    /* 0. Host letters S/R/D are matrix letters, not cursor keys (Forth shell). */
    {
        uint8_t r, c;
        TEST_ASSERT(msx_kbd_position('s', &r, &c) && (r != MSX_KEY_DOWN_R || c != MSX_KEY_DOWN_C),
                    "letter S is not cursor-down");
        TEST_ASSERT(msx_kbd_position('d', &r, &c) && (r != MSX_KEY_RIGHT_R || c != MSX_KEY_RIGHT_C),
                    "letter D is not cursor-right");
        TEST_ASSERT(msx_kbd_position('r', &r, &c) && r == 4u && c == 7u,
                    "letter R is MSX matrix row 4 col 7");
    }

    /* 1. Verify T-FW ROM files on disk */
    printf("[1] Verifying T-FW ROM Binary Size & Integrity...\n");
    FILE *f = fopen(".build/tbios/tfw_main_msx2.rom", "rb");
    TEST_ASSERT(f != NULL, "Opened .build/tbios/tfw_main_msx2.rom");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    TEST_ASSERT(sz == 32768, "T-FW MSX2 ROM is strictly 32768 bytes (32 KiB Main ROM budget)");

    /* 2. Load T-FW into MSX Emulator Core */
    printf("[2] Booting T-FW in B-MSX Z80 Hardware Emulator...\n");
    msx_set_bios_mode(BMSX_BIOS_TFW);
    strncpy(g_bmsx_cfg.main_rom, "tfw_main_msx2.rom", sizeof(g_bmsx_cfg.main_rom) - 1);
    int rc = msx_init(NULL);
    TEST_ASSERT(rc == MSX_OK, "msx_init succeeded with T-FW ROM");

    /* 3. Run initial boot frames */
    printf("[3] Stepping Z80 Execution into T-FW Forth Console...\n");
    for (int frame = 0; frame < 30; frame++) {
        msx_run_frame();
    }

    uint16_t pc, sp, bc, de, hl;
    uint8_t a;
    msx_cpu_words(&pc, &sp, &bc, &de, &hl, &a);

    /* Verify Z80 CPU is executing valid instructions within ROM/RAM */
    TEST_ASSERT(sp > 0xC000, "Z80 Stack Pointer is mapped into Page 3 RAM ($C000-$FFFF)");
    printf("    -> Z80 State: PC=%04X SP=%04X BC=%04X DE=%04X HL=%04X A=%02X\n",
           pc, sp, bc, de, hl, a);

    static uint16_t fb[256 * 212];
    msx_blit_rgb565(fb);
    int ink = 0;
    uint16_t bg = fb[0];
    for (int k = 0; k < 256 * 212; k++) {
        if (fb[k] != bg) ink++;
    }
    printf("    -> Rendered T-FW screen: bg=0x%04X, active character ink = %d pixels\n", bg, ink);
    TEST_ASSERT(ink > 20, "T-FW console banner & prompt successfully rendered to screen");

    uint32_t vram_sz = 0;
    uint8_t *vram = vdp_vram(&vram_sz);
    if (vram) {
        char linebuf[41];
        printf("    -> VRAM Screen 0 Name Table contents:\n");
        for (int r = 0; r < 5; r++) {
            memcpy(linebuf, vram + r * 40, 40);
            linebuf[40] = '\0';
            printf("       row %d: [%s]\n", r, linebuf);
        }
    }

    /* 4. Interactive Forth Command Verification */
    printf("[4] Testing Interactive Forth Evaluation: 'WORDS'...\n");
    /* Type 'W' (Row 5, col 4), 'O' (Row 4, col 4), 'R' (Row 4, col 7), 'D' (Row 3, col 1), 'S' (Row 5, col 0), CR (Row 7, col 7) */
    struct { uint8_t row; uint8_t col; } seq[] = {
        {5, 4}, /* W */
        {4, 4}, /* O */
        {4, 7}, /* R */
        {3, 1}, /* D */
        {5, 0}, /* S */
        {7, 7}  /* CR */
    };
    for (size_t i = 0; i < sizeof(seq)/sizeof(seq[0]); i++) {
        msx_kbd_down(seq[i].row, seq[i].col);
        for (int f = 0; f < 3; f++) msx_run_frame();
        msx_kbd_up(seq[i].row, seq[i].col);
        for (int f = 0; f < 3; f++) msx_run_frame();
    }

    int found_words_echo = 0;
    int found_words_output = 0;
    if (vram) {
        printf("    -> VRAM Screen 0 after typing WORDS:\n");
        char linebuf[41];
        for (int r = 0; r < 10; r++) {
            memcpy(linebuf, vram + r * 40, 40);
            linebuf[40] = '\0';
            printf("       row %d: [%s]\n", r, linebuf);
            if (strstr(linebuf, "WORDS")) found_words_echo = 1;
            if (strstr(linebuf, "DUP DROP SWAP")) found_words_output = 1;
        }
    }
    TEST_ASSERT(found_words_echo, "Forth input was echoed to screen (WORDS)");
    TEST_ASSERT(found_words_output, "Forth interpreter evaluated WORDS and listed dictionary");

    /* 4b. Interactive Forth Command Verification: 'DEV-TREE' */
    printf("[4b] Testing Interactive Forth Evaluation: 'DEV-TREE'...\n");
    /* Type 'D' (3,1), 'E' (3,2), 'V' (5,3), '-' (1,2), 'T' (5,1), 'R' (4,7), 'E' (3,2), 'E' (3,2), CR (7,7) */
    struct { uint8_t row; uint8_t col; } seq_dt[] = {
        {3, 1}, /* D */
        {3, 2}, /* E */
        {5, 3}, /* V */
        {1, 2}, /* - */
        {5, 1}, /* T */
        {4, 7}, /* R */
        {3, 2}, /* E */
        {3, 2}, /* E */
        {7, 7}  /* CR */
    };
    for (size_t i = 0; i < sizeof(seq_dt)/sizeof(seq_dt[0]); i++) {
        msx_kbd_down(seq_dt[i].row, seq_dt[i].col);
        for (int f = 0; f < 3; f++) msx_run_frame();
        msx_kbd_up(seq_dt[i].row, seq_dt[i].col);
        for (int f = 0; f < 3; f++) msx_run_frame();
    }

    int found_devtree_echo = 0;
    int found_devtree_output = 0;
    if (vram) {
        printf("    -> VRAM Screen 0 after typing DEV-TREE:\n");
        char linebuf[41];
        for (int r = 0; r < 20; r++) {
            memcpy(linebuf, vram + r * 40, 40);
            linebuf[40] = '\0';
            printf("       row %2d: [%s]\n", r, linebuf);
            if (strstr(linebuf, "DEV-TREE")) found_devtree_echo = 1;
            if (strstr(linebuf, "/cpus/z80")) found_devtree_output = 1;
        }
    }
    TEST_ASSERT(found_devtree_echo, "Forth input was echoed to screen (DEV-TREE)");
    TEST_ASSERT(found_devtree_output, "Forth interpreter evaluated DEV-TREE and listed devices");

    /* 4b2. Test Shift+[NUM] symbols input on MSX */
    printf("[4b2] Testing Shift+[NUM] symbols input on MSX...\n");
    /* Hold Shift (row 6, col 0) */
    msx_kbd_down(6, 0);
    for (int f = 0; f < 3; f++) msx_run_frame();
    /* Press '1' (row 0, col 1) -> yields '!' */
    msx_kbd_down(0, 1);
    for (int f = 0; f < 3; f++) msx_run_frame();
    msx_kbd_up(0, 1);
    for (int f = 0; f < 3; f++) msx_run_frame();
    /* Press '4' (row 0, col 4) -> yields '$' */
    msx_kbd_down(0, 4);
    for (int f = 0; f < 3; f++) msx_run_frame();
    msx_kbd_up(0, 4);
    for (int f = 0; f < 3; f++) msx_run_frame();
    /* Release Shift */
    msx_kbd_up(6, 0);
    for (int f = 0; f < 15; f++) msx_run_frame();

    int found_shifted_symbols = 0;
    if (vram) {
        printf("    -> VRAM Screen 0 after typing Shift+1, Shift+4:\n");
        char linebuf[41];
        for (int r = 0; r < 24; r++) {
            memcpy(linebuf, vram + r * 40, 40);
            linebuf[40] = '\0';
            printf("       row %2d: [%s]\n", r, linebuf);
            if (strstr(linebuf, "!$")) {
                found_shifted_symbols = 1;
            }
        }
    }
    TEST_ASSERT(found_shifted_symbols, "Shift+[NUM] produced shifted symbols (!$) on MSX");

    /* Clear the line with CR for subsequent tests */
    msx_kbd_down(7, 7);
    for (int f = 0; f < 3; f++) msx_run_frame();
    msx_kbd_up(7, 7);
    for (int f = 0; f < 10; f++) msx_run_frame();

    /* 4c. Interactive Forth Evaluation: 'MARS' and 'MANDELBROT' */
    printf("[4c] Testing Interactive Forth Evaluation: 'MARS'...\n");
    const char *mars_cmd = "MARS";
    for (const char *p = mars_cmd; *p; p++) {
        uint8_t r, c;
        if (msx_kbd_position(*p, &r, &c)) {
            msx_kbd_down(r, c);
            for (int f = 0; f < 3; f++) msx_run_frame();
            msx_kbd_up(r, c);
            for (int f = 0; f < 3; f++) msx_run_frame();
        }
    }
    /* Enter (7, 7) */
    msx_kbd_down(7, 7);
    for (int f = 0; f < 3; f++) msx_run_frame();
    msx_kbd_up(7, 7);
    for (int f = 0; f < 40; f++) msx_run_frame();

    int found_mars_output = 0;
    if (vram) {
        printf("    -> VRAM Screen 0 after typing MARS:\n");
        char linebuf[41];
        for (int r = 0; r < 24; r++) {
            memcpy(linebuf, vram + r * 40, 40);
            linebuf[40] = '\0';
            printf("       row %2d: [%s]\n", r, linebuf);
            if (strstr(linebuf, "Mars Voxel") || strstr(linebuf, "^^^^^") || strstr(linebuf, "#####")) {
                found_mars_output = 1;
            }
        }
    }
    TEST_ASSERT(found_mars_output, "Forth interpreter evaluated MARS and rendered voxel landscape on MSX");

    /* 4d. Interactive Forth Evaluation: 'MANDEL' */
    printf("[4d] Testing Interactive Forth Evaluation: 'MANDEL'...\n");
    for (int f = 0; f < 70; f++) msx_run_frame();

    const char *mandel_cmd = "MANDEL";
    for (const char *p = mandel_cmd; *p; p++) {
        uint8_t r, c;
        if (msx_kbd_position(*p, &r, &c)) {
            msx_kbd_down(r, c);
            for (int f = 0; f < 3; f++) msx_run_frame();
            msx_kbd_up(r, c);
            for (int f = 0; f < 3; f++) msx_run_frame();
        }
    }
    msx_kbd_down(7, 7);
    for (int f = 0; f < 3; f++) msx_run_frame();
    msx_kbd_up(7, 7);
    for (int f = 0; f < 40; f++) msx_run_frame();

    int found_mandel_output = 0;
    if (vram) {
        printf("    -> VRAM Screen 0 after typing MANDEL:\n");
        char linebuf[41];
        for (int r = 0; r < 24; r++) {
            memcpy(linebuf, vram + r * 40, 40);
            linebuf[40] = '\0';
            printf("       row %2d: [%s]\n", r, linebuf);
            if (strstr(linebuf, "Mandelbrot") || strstr(linebuf, "++...") || strstr(linebuf, "*****")) {
                found_mandel_output = 1;
            }
        }
    }
    TEST_ASSERT(found_mandel_output, "Forth interpreter evaluated MANDEL and rendered fractal on MSX");

    /* 5. Verify MSX2+ and turbo R ROM Targets */
    printf("[5] Verifying tfw_main_msx2+.rom & tfw_main_msxtr.rom Targets...\n");
    FILE *f2p = fopen(".build/tbios/tfw_main_msx2+.rom", "rb");
    TEST_ASSERT(f2p != NULL, "Opened .build/tbios/tfw_main_msx2+.rom");
    if (f2p) {
        fseek(f2p, 0, SEEK_END);
        TEST_ASSERT(ftell(f2p) == 32768, "T-FW MSX2+ ROM is 32 KiB");
        fclose(f2p);
    }

    FILE *ftr = fopen(".build/tbios/tfw_main_msxtr.rom", "rb");
    TEST_ASSERT(ftr != NULL, "Opened .build/tbios/tfw_main_msxtr.rom");
    if (ftr) {
        fseek(ftr, 0, SEEK_END);
        TEST_ASSERT(ftell(ftr) == 32768, "T-FW turbo R ROM is 32 KiB");
        fclose(ftr);
    }

    printf("==========================================================\n");
    printf(" T-FW Verification: %d/%d passed (100%% Success)\n", g_tests_passed, g_tests_total);
    printf("==========================================================\n");

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
