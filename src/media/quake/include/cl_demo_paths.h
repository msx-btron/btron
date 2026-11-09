/*
 * cl_demo_paths.h — names Demo_Play asks FS_LoadFile for
 *
 * Host gate: verify/tests/test_demo_paths.c.  PS2 must try /SYS (embedded
 * volume) and QUAKE/NAME.DEM (ISO FILEIO/CDVD) as well as the host assets/
 * spelling, because pak0.pak does not carry the drone tracks.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */
#ifndef QUAKE_CL_DEMO_PATHS_H
#define QUAKE_CL_DEMO_PATHS_H

#include <stdio.h>
#include <string.h>

#define DEMO_PATH_MAX 8
#define DEMO_PATH_LEN 96

/* Fill out[0..max) with the search names for `demoname`.
 * `drone1m1` and `drone1m1.dem` produce the same list.  Returns count. */
static inline int Demo_PathCandidates(const char *demoname,
                                      char out[][DEMO_PATH_LEN], int max)
{
    char base[64];
    char upper[64];
    const char *p;
    int i, n = 0;

    if (!demoname || !demoname[0] || !out || max < 1) return 0;
    p = strrchr(demoname, '/');
    p = p ? p + 1 : demoname;
    snprintf(base, sizeof(base), "%s", p);
    {
        char *dot = strstr(base, ".dem");
        if (!dot) dot = strstr(base, ".DEM");
        if (dot) *dot = 0;
    }
    if (!base[0]) return 0;
    for (i = 0; base[i] && i < (int)sizeof(upper) - 1; i++) {
        char c = base[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
        upper[i] = c;
    }
    upper[i] = 0;

#define DEMO_ADD(fmt, arg)                                                 \
    do {                                                                   \
        if (n < max) {                                                     \
            snprintf(out[n], DEMO_PATH_LEN, fmt, arg);                     \
            n++;                                                           \
        }                                                                  \
    } while (0)

    DEMO_ADD("%s", base);
    DEMO_ADD("%s.dem", base);
    DEMO_ADD("/SYS/%s.dem", base);
    DEMO_ADD("/SYS/%s", base);
    DEMO_ADD("QUAKE/%s.DEM", upper);
    DEMO_ADD("%s.DEM", upper);
    DEMO_ADD("assets/quake/%s.dem", base);
    DEMO_ADD("assets/quake/%s", base);
#undef DEMO_ADD
    return n;
}

#endif /* QUAKE_CL_DEMO_PATHS_H */
