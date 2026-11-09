/*
 * test_demo_paths.c — Demo_Play search list vs SYS.VOL and ISO names
 *
 * Hardware Nov 10 2026: Demo_Play could not load drone1m1 even though
 * E1M1 painted (PAK indexed) and drone1m1.dem is in SYS.VOL and in
 * QUAKE/ on the ISO.  The search list must name both.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../src/media/quake/include/cl_demo_paths.h"

static int s_checks, s_fail;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_fail++;
        printf("  [FAIL] %s\n", what);
    } else {
        printf("  [OK]   %s\n", what);
    }
}

static int list_has(char paths[][DEMO_PATH_LEN], int n, const char *want)
{
    int i;
    for (i = 0; i < n; i++)
        if (strcmp(paths[i], want) == 0) return 1;
    return 0;
}

int main(void)
{
    char paths[DEMO_PATH_MAX][DEMO_PATH_LEN];
    int n, i;

    printf("test_demo_paths: Demo_Play search list\n");

    n = Demo_PathCandidates("drone1m1", paths, DEMO_PATH_MAX);
    check("drone1m1 yields paths", n >= 5);
    for (i = 0; i < n; i++)
        printf("    %s\n", paths[i]);

    check("bare name", list_has(paths, n, "drone1m1"));
    check(".dem suffix", list_has(paths, n, "drone1m1.dem"));
    check("/SYS/drone1m1.dem (embedded volume)",
          list_has(paths, n, "/SYS/drone1m1.dem"));
    check("QUAKE/DRONE1M1.DEM (ISO FILEIO/CDVD)",
          list_has(paths, n, "QUAKE/DRONE1M1.DEM"));
    check("assets/quake/drone1m1.dem (host)",
          list_has(paths, n, "assets/quake/drone1m1.dem"));

    n = Demo_PathCandidates("drone1m1.dem", paths, DEMO_PATH_MAX);
    check(".dem input strips to the same /SYS path",
          list_has(paths, n, "/SYS/drone1m1.dem"));
    check(".dem input still has ISO 8.3",
          list_has(paths, n, "QUAKE/DRONE1M1.DEM"));

    n = Demo_PathCandidates("QUAKE/DRONE1M1.DEM", paths, DEMO_PATH_MAX);
    check("ISO spelling still names /SYS",
          list_has(paths, n, "/SYS/drone1m1.dem") ||
          list_has(paths, n, "/SYS/DRONE1M1.dem") ||
          list_has(paths, n, "/SYS/DRONE1M1.DEM"));

    /* SYS.VOL on the host, when present, carries the payload. */
    {
        FILE *f = fopen("btron_sys.vol", "rb");
        if (!f) {
            printf("  [SKIP] btron_sys.vol not in cwd\n");
        } else {
            long sz;
            char *buf;
            fseek(f, 0, SEEK_END);
            sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            buf = (char *)malloc((size_t)sz);
            if (!buf) {
                fclose(f);
                check("sys.vol malloc", 0);
            } else {
                int hit = 0;
                long k;
                if (fread(buf, 1, (size_t)sz, f) == (size_t)sz) {
                    for (k = 0; k + 12 <= sz; k++) {
                        if (memcmp(buf + k, "drone1m1.dem", 12) == 0) {
                            hit = 1;
                            break;
                        }
                    }
                }
                check("btron_sys.vol contains drone1m1.dem", hit);
                free(buf);
                fclose(f);
            }
        }
    }

    check("empty name is refused", Demo_PathCandidates("", paths, 8) == 0);
    check("NULL name is refused", Demo_PathCandidates(NULL, paths, 8) == 0);

    printf("%d checks, %d failed\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}
