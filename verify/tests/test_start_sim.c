/*
 * Host gate for the PicoCalc start-level sim. Walk, one door, a shotgun
 * hit, a rocket, the slipgate sentence, and a lit 128x96 frame.
 */
#include "start_sim.h"
#include "start_blob.h"
#include "picogl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        printf("FAIL %s\n", msg);
        g_fail++;
    } else {
        printf("ok   %s\n", msg);
    }
}

static void dump_org(const char *tag)
{
    const float *o = start_sim_origin();
    printf("  %s org %.1f %.1f %.1f ground %d solid %d hp %d\n",
           tag, o[0], o[1], o[2], start_sim_onground(),
           start_sim_solid(o[0], o[1], o[2]), start_sim_health());
}

static void ticks(uint32_t t0, int n, int step)
{
    int i;
    for (i = 0; i < n; i++) start_sim_tick(t0 + (uint32_t)(i * step));
}

int main(void)
{
    const float *o;
    float z0, y0;
    int i;

    start_sim_init();
    o = start_sim_origin();
    dump_org("spawn");
    expect(!start_sim_solid(o[0], o[1], o[2]), "spawn is not solid");
    expect(start_sim_health() == 100, "health starts at 100");
    z0 = o[2];
    y0 = o[1];
    ticks(50, 40, 50);
    o = start_sim_origin();
    dump_org("idle");
    expect(o[2] > z0 - 80.0f, "idle does not fall through the floor");
    z0 = o[2];

    start_sim_set_yaw(90.0f);
    for (i = 0; i < 40; i++) {
        start_sim_key('w', 1);
        start_sim_tick(3000u + (uint32_t)i * 50u);
    }
    o = start_sim_origin();
    dump_org("walk");
    printf("  moved y by %.1f\n", o[1] - y0);
    expect(o[1] > y0 + 20.0f, "yaw 90 walks toward +Y");
    expect(o[2] > z0 - 48.0f, "walk stays on the floor");
    y0 = o[1];
    for (i = 0; i < 40; i++) {
        start_sim_key('w', 1);
        start_sim_tick(5000u + (uint32_t)i * 50u);
    }
    o = start_sim_origin();
    dump_org("walk2");
    printf("  second leg y by %.1f\n", o[1] - y0);
    expect(o[1] > y0 + 20.0f, "walk keeps moving");

    {
        float mins[3], maxs[3];
        float pos0;
        start_sim_door_bounds(0, mins, maxs);
        printf("  door0 box %.0f %.0f %.0f .. %.0f %.0f %.0f touch\n",
               mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);
        start_sim_set_origin(0.5f * (mins[0] + maxs[0]),
                             0.5f * (mins[1] + maxs[1]),
                             0.5f * (mins[2] + maxs[2]));
        pos0 = start_sim_door_pos(0);
        ticks(6000, 8, 50);
        printf("  door0 pos %.1f -> %.1f\n", pos0, start_sim_door_pos(0));
        expect(start_sim_door_pos(0) > pos0 + 10.0f, "touch opens door 0");
    }

    start_sim_init();
    {
        int got = 0;
        for (i = 0; i < start_sim_n_pickup(); i++) {
            float p[3];
            int kind = 0;
            start_sim_pickup(i, p, &kind);
            if (kind != START_PICK_SSG) continue;
            start_sim_set_origin(p[0], p[1], p[2]);
            start_sim_tick(10000);
            got = start_sim_has_weapon(START_W_SSG);
            printf("  ssg at %.1f %.1f %.1f owned %d\n", p[0], p[1], p[2], got);
            break;
        }
        expect(got, "supershotgun pickup grants the weapon");
    }

    {
        /* Zombie 8 stands in an open spot. One tick with dt 0 fires before
         * gravity carries the player off the line. */
        const float *z = start_sim_zombie_origin(8);
        float ox = z[0];
        float oy = z[1] + 40.0f;
        float oz = z[2];
        int hp0 = start_sim_zombie_health(8);
        float yaw = (float)(atan2(z[1] - oy, z[0] - ox) * (180.0 / 3.14159265));
        start_sim_set_origin(ox, oy, oz);
        start_sim_set_yaw(yaw);
        start_sim_key('3', 1);
        start_sim_key('f', 1);
        start_sim_tick(10000);
        printf("  zombie 8 hp %d -> %d weapon %d ammo %d from %.1f %.1f %.1f now %.1f %.1f %.1f\n",
               hp0, start_sim_zombie_health(8), start_sim_weapon(), start_sim_ammo(),
               ox, oy, oz,
               start_sim_origin()[0], start_sim_origin()[1], start_sim_origin()[2]);
        expect(start_sim_zombie_health(8) < hp0, "supershotgun drops a zombie's health");
    }

    start_sim_init();
    {
        int nshot = 0;
        for (i = 0; i < start_sim_n_pickup(); i++) {
            float p[3];
            int kind = 0;
            start_sim_pickup(i, p, &kind);
            if (kind != START_PICK_RL) continue;
            start_sim_set_origin(p[0], p[1], p[2]);
            start_sim_tick(80000);
            start_sim_set_origin(START_SPAWN_X, START_SPAWN_Y, START_SPAWN_Z);
            start_sim_set_yaw(90.0f);
            start_sim_key('7', 1);
            start_sim_key('f', 1);
            start_sim_tick(80050);
            nshot = start_sim_projectiles();
            printf("  rocket weapon %d ammo-path projectiles %d org %.1f %.1f %.1f\n",
                   start_sim_weapon(), nshot,
                   start_sim_origin()[0], start_sim_origin()[1], start_sim_origin()[2]);
            break;
        }
        expect(nshot > 0, "rocket launcher fires a projectile");
    }

    start_sim_init();
    {
        int got = 0;
        for (i = 0; i < start_sim_n_trig(); i++) {
            float mins[3], maxs[3];
            int kind = 0;
            const float *before;
            start_sim_trig(i, mins, maxs, &kind);
            if (kind != START_TRIG_SLIP) continue;
            start_sim_set_origin(0.5f * (mins[0] + maxs[0]),
                                 0.5f * (mins[1] + maxs[1]),
                                 0.5f * (mins[2] + maxs[2]));
            before = start_sim_origin();
            printf("  slip at %.1f %.1f %.1f\n", before[0], before[1], before[2]);
            start_sim_tick(90000);
            printf("  slip msg [%s] flag %d\n", start_sim_message(), start_sim_slipped());
            got = start_sim_slipped() &&
                  strcmp(start_sim_message(),
                         "Walk into the Slipgate to start playing Quake!") == 0;
            break;
        }
        expect(got, "slipgate keeps the hall and shows its sentence");
    }

    start_sim_init();
    ticks(50, 10, 50);
    start_sim_render();
    printf("  lit %d / %d tris %d\n", start_sim_lit(), 128 * 96, picogl_tri_n());
    expect(start_sim_lit() > (128 * 96) / 12, "the hall lights more than the HUD");
    expect(picogl_tri_n() > 30, "the frame submits triangles");

    if (g_fail) {
        printf("%d failed\n", g_fail);
        return 1;
    }
    printf("start sim ok\n");
    return 0;
}
