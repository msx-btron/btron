/*
 * Kernel, device, and record-file name copies.
 * A name longer than the slot must come back terminated inside the slot.
 * Replacing the bounded copy with strcpy walks off the static buffer.
 */
#include <stdio.h>
#include <string.h>

#include <btron/device.h>
#include <btron/error.h>
#include <btron/file.h>
#include <btron/sys_mgmt.h>

static int g_fail;

static void check(const char *name, int ok)
{
    if (ok) {
        printf("  OK   %s\n", name);
        return;
    }
    printf("  FAIL %s\n", name);
    g_fail++;
}

static void test_sys_name(void)
{
    char longn[80];
    char got[96];
    char shortn[] = "hi";
    W sz;
    ER er;

    memset(longn, 'A', sizeof longn - 1);
    longn[sizeof longn - 1] = '\0';
    er = set_cnf(CNF_SYS_NAME, longn, (W)sizeof longn);
    check("set_cnf long name", er == E_OK);

    memset(got, 'Z', sizeof got);
    sz = 0;
    er = get_cnf(CNF_SYS_NAME, got, &sz);
    check("get_cnf long name", er == E_OK && sz == 64 && got[63] == '\0' &&
          strlen(got) == 63 && got[64] == 'Z');

    er = set_cnf(CNF_SYS_NAME, shortn, (W)sizeof shortn);
    check("set_cnf short name", er == E_OK);
    memset(got, 'Z', sizeof got);
    sz = 0;
    er = get_cnf(CNF_SYS_NAME, got, &sz);
    check("get_cnf short name drops the previous tail",
          er == E_OK && sz == 3 && strcmp(got, "hi") == 0 && got[3] == 'Z');
}

static void test_dev_name(void)
{
    char longn[40];
    ID id;

    memset(longn, 'D', sizeof longn - 1);
    longn[sizeof longn - 1] = '\0';
    id = opn_dev_mgr(longn, DEV_READ);
    check("opn_dev_mgr long name", id > 0);
    check("opn_dev_mgr NULL", opn_dev_mgr(NULL, DEV_READ) == ER_PAR);
}

static void test_paths(void)
{
    char big[80];
    char clipped[64];
    DIR_ENTRY entry;
    VOL_INFO info;
    ID fd;
    ID dir;
    ER er;

    memset(big, 'p', sizeof big - 1);
    big[0] = '/';
    big[sizeof big - 1] = '\0';
    memcpy(clipped, big, 63);
    clipped[63] = '\0';

    fd = cre_fil(big, F_READ | F_WRITE);
    check("cre_fil long path", fd > 0);
    check("long path is stored clipped", opn_fil(clipped, F_READ) > 0);
    check("long path is not stored whole", opn_fil(big, F_READ) == ER_NOEXS);

    fd = cre_fil("/test_kcopy.tad", F_READ | F_WRITE);
    check("cre_fil short path", fd > 0);
    er = mov_fil("/test_kcopy.tad", "/test_kcopy_moved.tad");
    check("mov_fil", er == E_OK);
    check("moved path opens", opn_fil("/test_kcopy_moved.tad", F_READ) > 0);
    check("old path is gone", opn_fil("/test_kcopy.tad", F_READ) == ER_NOEXS);

    dir = opn_dir("/sys");
    memset(&entry, 0xA5, sizeof entry);
    er = rd_dir(dir, &entry);
    check("rd_dir name", er == E_OK && strcmp(entry.name, "sample.tad") == 0);

    memset(&info, 0xA5, sizeof info);
    er = ref_vol(1, &info);
    check("ref_vol name", er == E_OK && strcmp(info.vol_name, "BTRON_SYSTEM") == 0 &&
          info.block_size == 4096);
}

int main(void)
{
    test_sys_name();
    test_dev_name();
    test_paths();
    printf("%s (%d failed)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
