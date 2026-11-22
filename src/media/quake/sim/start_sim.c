/*
 * start_sim.c — playable maps/start.bsp without progs.dat.
 *
 * Collision is the player clip hull. Doors, walls, and gates are the other
 * hulls. The picture is flat-shaded BSP faces plus a few cooked mdl frames,
 * drawn through PicoGL into a 128x96 target.
 */
#include "start_sim.h"
#include "start_blob.h"
#include "picogl.h"

#include <math.h>
#include <string.h>

#define VW 128
#define VH 96
#define CLEAR_RGB 0x0841u
#define EPS 0.03125f
#define MAX_SHOT 16
#define TRI_ACTOR 180
#define TRI_SUB 220
#define TRI_WORLD 480

enum {
    BIT_FWD = 1, BIT_BACK = 2, BIT_LEFT = 4, BIT_RIGHT = 8,
    BIT_JUMP = 16, BIT_FIRE = 32,
    BIT_LL = 64, BIT_LR = 128, BIT_LU = 256, BIT_LD = 512
};

static uint16_t s_rgb[VW * VH];
static int32_t  s_iz[VW * VH];
static int      s_bound;
static int      s_lit;
static int      s_ready;

static float    s_org[3];
static float    s_vel[3];
static float    s_yaw, s_pitch;
static int      s_ground;
static int      s_health, s_armor;
static int      s_shells, s_nails, s_rockets, s_cells;
static int      s_weapons;
static int      s_weapon;
static uint32_t s_quad_until;
static int      s_dead;
static uint32_t s_dead_until;
static int      s_bits;
static uint32_t s_hold[10];
static uint32_t s_now;
static uint32_t s_next_fire;
static int      s_slip;
static char     s_msg[64];
static uint32_t s_rng = 1u;
static int      s_tele_block;

static float    s_door_pos[START_NDOOR];
static int      s_door_state[START_NDOOR];
static float    s_door_wait[START_NDOOR];

static float    s_zorg[START_NZOMBIE][3];
static float    s_zyaw[START_NZOMBIE];
static int      s_zhp[START_NZOMBIE];
static int      s_zstate[START_NZOMBIE];
static uint32_t s_znext[START_NZOMBIE];

static uint8_t  s_taken[START_NPICK];

typedef struct { float o[3], v[3]; int kind, dmg, alive; } shot_t;
static shot_t   s_shot[MAX_SHOT];

static const int s_cool[8] = { 400, 500, 700, 100, 80, 600, 800, 100 };
static const char *const s_wname[8] = { "AX", "SG", "SS", "NG", "SN", "GL", "RL", "LG" };

static int rnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (int)((s_rng >> 16) & 255u);
}

static float clampf(float v, float a, float b)
{
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

static float qatan2(float y, float x)
{
    float ax, ay, a, s, r;
    ax = x < 0.0f ? -x : x;
    ay = y < 0.0f ? -y : y;
    if (ax < 1.0e-8f && ay < 1.0e-8f) return 0.0f;
    a = (ax < ay) ? ax / ay : ay / ax;
    s = a * a;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0.0f) r = 3.14159274f - r;
    if (y < 0.0f) r = -r;
    return r;
}

static float len3(const float *v)
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void norm3(float *v)
{
    float L = len3(v);
    if (L < 1.0e-4f) return;
    v[0] /= L; v[1] /= L; v[2] /= L;
}

static void angles(float fwd[3], float right[3], float up[3])
{
    float y = s_yaw * (3.14159265f / 180.0f);
    float p = s_pitch * (3.14159265f / 180.0f);
    float cy = cosf(y), sy = sinf(y), cp = cosf(p), sp = sinf(p);
    fwd[0] = cp * cy; fwd[1] = cp * sy; fwd[2] = -sp;
    if (right) { right[0] = sy; right[1] = -cy; right[2] = 0.0f; }
    if (up) { up[0] = sp * cy; up[1] = sp * sy; up[2] = cp; }
}

static float plane_of(int plane, const float *p)
{
    const float *pl;
    int typ;
    if (plane < 0 || plane >= START_NPLANE) return 0.0f;
    pl = start_plane[plane];
    typ = (int)pl[4];
    if (typ >= 0 && typ <= 2) return p[typ] - pl[3];
    return pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] - pl[3];
}

static int solid_leaf(int c)
{
    return c == -2 || c == -8;
}

static int contents_at(int head, const float *p)
{
    int num = head;
    int guard = 0;
    /* A leaf head is the contents. Returning empty here lets a trace
     * walk through a solid leaf and sink into the floor. */
    if (head < 0) return head;
    while (num >= 0) {
        int plane, c0, c1;
        float d;
        if (num >= START_NCLIP) return -1;
        plane = start_clip_plane[num];
        c0 = start_clip_child[num][0];
        c1 = start_clip_child[num][1];
        d = plane_of(plane, p);
        num = (d < 0.0f) ? c1 : c0;
        if (++guard > START_NCLIP) return -1;
    }
    return num;
}

typedef struct {
    float fraction;
    float end[3];
    float n[3];
    int startsolid;
} tr_t;

/* Explicit stack. The RP2040 MSP guard is 4 KB, and a recursive hull
 * walk of this map does not fit there. One trace at a time, core 0. */
typedef struct {
    int num;
    float p1f, p2f;
    float p1[3], p2[3];
    int stage, side, far;
    float midf, mid[3];
} hull_fr_t;

static hull_fr_t s_hull_fr[64];

static int rec_hull(int head, int num, float p1f, float p2f,
                    const float *p1, const float *p2, tr_t *tr)
{
    int sp = 0;
    s_hull_fr[0].num = num;
    s_hull_fr[0].p1f = p1f;
    s_hull_fr[0].p2f = p2f;
    s_hull_fr[0].p1[0] = p1[0]; s_hull_fr[0].p1[1] = p1[1]; s_hull_fr[0].p1[2] = p1[2];
    s_hull_fr[0].p2[0] = p2[0]; s_hull_fr[0].p2[1] = p2[1]; s_hull_fr[0].p2[2] = p2[2];
    s_hull_fr[0].stage = 0;
    while (sp >= 0) {
        hull_fr_t *f = &s_hull_fr[sp];
        int plane, c0, c1, side;
        float t1, t2, frac, midf, mid[3];
        const float *pl;
        if (f->stage == 0) {
            if (f->num < 0) {
                if (solid_leaf(f->num)) tr->startsolid = 1;
                sp--;
                continue;
            }
            if (sp > 48 || f->num >= START_NCLIP) { sp--; continue; }
            plane = start_clip_plane[f->num];
            c0 = start_clip_child[f->num][0];
            c1 = start_clip_child[f->num][1];
            if (plane < 0 || plane >= START_NPLANE) { sp--; continue; }
            t1 = plane_of(plane, f->p1);
            t2 = plane_of(plane, f->p2);
            if (t1 >= 0.0f && t2 >= 0.0f) { f->num = c0; continue; }
            if (t1 < 0.0f && t2 < 0.0f) { f->num = c1; continue; }
            if (t1 < 0.0f) frac = (t1 + EPS) / (t1 - t2);
            else frac = (t1 - EPS) / (t1 - t2);
            frac = clampf(frac, 0.0f, 1.0f);
            midf = f->p1f + (f->p2f - f->p1f) * frac;
            mid[0] = f->p1[0] + frac * (f->p2[0] - f->p1[0]);
            mid[1] = f->p1[1] + frac * (f->p2[1] - f->p1[1]);
            mid[2] = f->p1[2] + frac * (f->p2[2] - f->p1[2]);
            side = (t1 < 0.0f) ? 1 : 0;
            f->stage = 1;
            f->side = side;
            f->far = side ? c0 : c1;
            f->midf = midf;
            f->mid[0] = mid[0]; f->mid[1] = mid[1]; f->mid[2] = mid[2];
            if (sp + 1 >= 64) { sp--; continue; }
            sp++;
            s_hull_fr[sp].num = side ? c1 : c0;
            s_hull_fr[sp].p1f = f->p1f;
            s_hull_fr[sp].p2f = midf;
            s_hull_fr[sp].p1[0] = f->p1[0];
            s_hull_fr[sp].p1[1] = f->p1[1];
            s_hull_fr[sp].p1[2] = f->p1[2];
            s_hull_fr[sp].p2[0] = mid[0];
            s_hull_fr[sp].p2[1] = mid[1];
            s_hull_fr[sp].p2[2] = mid[2];
            s_hull_fr[sp].stage = 0;
            continue;
        }
        if (!solid_leaf(contents_at(f->far, f->mid))) {
            f->stage = 0;
            f->num = f->far;
            f->p1f = f->midf;
            f->p1[0] = f->mid[0];
            f->p1[1] = f->mid[1];
            f->p1[2] = f->mid[2];
            continue;
        }
        plane = start_clip_plane[f->num];
        pl = start_plane[plane];
        if (f->side) {
            tr->n[0] = -pl[0]; tr->n[1] = -pl[1]; tr->n[2] = -pl[2];
        } else {
            tr->n[0] = pl[0]; tr->n[1] = pl[1]; tr->n[2] = pl[2];
        }
        frac = 0.0f;
        midf = f->midf;
        mid[0] = f->mid[0]; mid[1] = f->mid[1]; mid[2] = f->mid[2];
        {
            float span = f->p2f - f->p1f;
            float along = (span > 1.0e-8f) ? (midf - f->p1f) / span : 0.0f;
            frac = along;
        }
        while (solid_leaf(contents_at(head, mid))) {
            frac -= 0.1f;
            if (frac < 0.0f) break;
            midf = f->p1f + (f->p2f - f->p1f) * frac;
            mid[0] = f->p1[0] + frac * (f->p2[0] - f->p1[0]);
            mid[1] = f->p1[1] + frac * (f->p2[1] - f->p1[1]);
            mid[2] = f->p1[2] + frac * (f->p2[2] - f->p1[2]);
        }
        tr->fraction = midf;
        tr->end[0] = mid[0]; tr->end[1] = mid[1]; tr->end[2] = mid[2];
        return 0;
    }
    return 1;
}

static void trace_one(tr_t *tr, int head, const float *a, const float *b)
{
    tr->fraction = 1.0f;
    tr->startsolid = 0;
    tr->end[0] = b[0]; tr->end[1] = b[1]; tr->end[2] = b[2];
    tr->n[0] = tr->n[1] = 0.0f; tr->n[2] = 1.0f;
    if (head < 0) return;
    if (solid_leaf(contents_at(head, a))) {
        tr->startsolid = 1;
        tr->fraction = 0.0f;
        tr->end[0] = a[0]; tr->end[1] = a[1]; tr->end[2] = a[2];
        return;
    }
    rec_hull(head, head, 0.0f, 1.0f, a, b, tr);
}

static void trace(tr_t *tr, const float *a, const float *b)
{
    int i;
    trace_one(tr, START_HULL1, a, b);
    for (i = 0; i < START_NMODEL; i++) {
        tr_t sub;
        float da[3], db[3], delta[3];
        int head, k, door;
        float rad;
        if (i == 0) continue;
        if (!start_model_draw[i] && start_model_kind[i] == 0) continue;
        if (start_model_kind[i] == 0 && !start_model_draw[i]) continue;
        /* Triggers are not solid. Kind 0 with draw 0 was cleared in the cook. */
        if (!start_model_draw[i]) continue;
        head = start_model_head1[i];
        if (head <= 0) continue;
        delta[0] = delta[1] = delta[2] = 0.0f;
        for (door = 0; door < START_NDOOR; door++) {
            if (start_door_model[door] != i) continue;
            delta[0] = start_door_dir[door][0] * s_door_pos[door];
            delta[1] = start_door_dir[door][1] * s_door_pos[door];
            delta[2] = start_door_dir[door][2] * s_door_pos[door];
            break;
        }
        rad = 96.0f;
        if (a[0] < start_model_mins[i][0] + delta[0] - rad &&
            b[0] < start_model_mins[i][0] + delta[0] - rad) continue;
        if (a[0] > start_model_maxs[i][0] + delta[0] + rad &&
            b[0] > start_model_maxs[i][0] + delta[0] + rad) continue;
        for (k = 0; k < 3; k++) {
            da[k] = a[k] - delta[k];
            db[k] = b[k] - delta[k];
        }
        trace_one(&sub, head, da, db);
        if (sub.fraction < tr->fraction) {
            tr->fraction = sub.fraction;
            tr->startsolid = sub.startsolid;
            tr->n[0] = sub.n[0]; tr->n[1] = sub.n[1]; tr->n[2] = sub.n[2];
            tr->end[0] = sub.end[0] + delta[0];
            tr->end[1] = sub.end[1] + delta[1];
            tr->end[2] = sub.end[2] + delta[2];
        }
    }
}

static void clip_vel(float *vel, const float *n)
{
    float d = vel[0] * n[0] + vel[1] * n[1] + vel[2] * n[2];
    if (d < 0.0f) {
        vel[0] -= d * n[0];
        vel[1] -= d * n[1];
        vel[2] -= d * n[2];
    }
}

static void slide(float *org, float *vel, float dt, int horizontal)
{
    int bump;
    float time = dt;
    for (bump = 0; bump < 3 && time > 1.0e-4f; bump++) {
        tr_t tr;
        float end[3];
        end[0] = org[0] + vel[0] * time;
        end[1] = org[1] + vel[1] * time;
        end[2] = org[2] + (horizontal ? 0.0f : vel[2] * time);
        if (horizontal) end[2] = org[2];
        trace(&tr, org, end);
        org[0] = tr.end[0]; org[1] = tr.end[1]; org[2] = tr.end[2];
        if (tr.startsolid) return;
        if (tr.fraction >= 1.0f) return;
        time *= (1.0f - tr.fraction);
        clip_vel(vel, tr.n);
        if (tr.n[2] > 0.7f) s_ground = 1;
    }
}

static void physics(float dt)
{
    float fwd[3], right[3], wish[3], end[3];
    float f, s, spd;
    tr_t tr;
    if (s_dead) return;
    if (s_bits & BIT_LL) s_yaw += 120.0f * dt;
    if (s_bits & BIT_LR) s_yaw -= 120.0f * dt;
    if (s_bits & BIT_LU) s_pitch -= 80.0f * dt;
    if (s_bits & BIT_LD) s_pitch += 80.0f * dt;
    if (s_yaw >= 360.0f) s_yaw -= 360.0f;
    if (s_yaw < 0.0f) s_yaw += 360.0f;
    s_pitch = clampf(s_pitch, -70.0f, 80.0f);
    angles(fwd, right, NULL);
    f = ((s_bits & BIT_FWD) ? 1.0f : 0.0f) - ((s_bits & BIT_BACK) ? 1.0f : 0.0f);
    s = ((s_bits & BIT_RIGHT) ? 1.0f : 0.0f) - ((s_bits & BIT_LEFT) ? 1.0f : 0.0f);
    wish[0] = fwd[0] * f + right[0] * s;
    wish[1] = fwd[1] * f + right[1] * s;
    wish[2] = 0.0f;
    spd = len3(wish);
    if (spd > 0.0f) { wish[0] /= spd; wish[1] /= spd; wish[2] = 0.0f; }
    if (s_ground) {
        float speed = sqrtf(s_vel[0] * s_vel[0] + s_vel[1] * s_vel[1]);
        float control = speed < 100.0f ? 100.0f : speed;
        float drop = control * 4.0f * dt;
        float ns = speed - drop;
        if (ns < 0.0f) ns = 0.0f;
        if (speed > 0.0f) {
            float sc = ns / speed;
            s_vel[0] *= sc; s_vel[1] *= sc;
        }
        {
            float cur = s_vel[0] * wish[0] + s_vel[1] * wish[1];
            float add = 320.0f - cur;
            float acc = 10.0f * dt * 320.0f;
            if (add > 0.0f && spd > 0.0f) {
                if (acc > add) acc = add;
                s_vel[0] += wish[0] * acc;
                s_vel[1] += wish[1] * acc;
            }
        }
        if ((s_bits & BIT_JUMP) && s_ground) {
            s_vel[2] = 270.0f;
            s_ground = 0;
        }
    }
    if (!s_ground) s_vel[2] -= 800.0f * dt;

    end[0] = s_org[0]; end[1] = s_org[1]; end[2] = s_org[2] + 18.0f;
    trace(&tr, s_org, end);
    s_org[0] = tr.end[0]; s_org[1] = tr.end[1]; s_org[2] = tr.end[2];
    slide(s_org, s_vel, dt, 1);
    end[0] = s_org[0]; end[1] = s_org[1]; end[2] = s_org[2] - 18.0f + s_vel[2] * dt;
    trace(&tr, s_org, end);
    s_org[0] = tr.end[0]; s_org[1] = tr.end[1]; s_org[2] = tr.end[2];
    if (tr.fraction < 1.0f && tr.n[2] > 0.7f) {
        s_ground = 1;
        if (s_vel[2] < 0.0f) s_vel[2] = 0.0f;
    } else if (tr.fraction >= 1.0f && s_vel[2] <= 0.0f) {
        /* The hull backoff stops short of the plane. A step of exactly 18
         * returns to that hover point and misses, so the next tick is
         * airborne and will not accelerate. Probe a few units further.
         * A rising jump stays airborne: the probe would catch the floor
         * just left behind. */
        tr_t probe;
        float below[3];
        below[0] = s_org[0];
        below[1] = s_org[1];
        below[2] = s_org[2] - 4.0f;
        trace(&probe, s_org, below);
        if (probe.fraction < 1.0f && probe.n[2] > 0.7f && !probe.startsolid) {
            s_org[0] = probe.end[0];
            s_org[1] = probe.end[1];
            s_org[2] = probe.end[2];
            s_ground = 1;
            if (s_vel[2] < 0.0f) s_vel[2] = 0.0f;
        } else {
            s_ground = 0;
        }
    } else if (tr.fraction >= 1.0f) {
        s_ground = 0;
    }
    if (s_org[2] < -900.0f) {
        s_org[0] = START_SPAWN_X; s_org[1] = START_SPAWN_Y; s_org[2] = START_SPAWN_Z + 24.0f;
        s_vel[0] = s_vel[1] = s_vel[2] = 0.0f;
    }
}

static int overlap_player(const float mins[3], const float maxs[3])
{
    float a0[3], a1[3];
    a0[0] = s_org[0] - 16.0f; a1[0] = s_org[0] + 16.0f;
    a0[1] = s_org[1] - 16.0f; a1[1] = s_org[1] + 16.0f;
    a0[2] = s_org[2] - 24.0f; a1[2] = s_org[2] + 32.0f;
    return a0[0] < maxs[0] && a1[0] > mins[0] &&
           a0[1] < maxs[1] && a1[1] > mins[1] &&
           a0[2] < maxs[2] && a1[2] > mins[2];
}

static void door_bounds(int i, float mins[3], float maxs[3])
{
    int k;
    for (k = 0; k < 3; k++) {
        float d = start_door_dir[i][k] * s_door_pos[i];
        mins[k] = (float)start_door_mins[i][k] + d;
        maxs[k] = (float)start_door_maxs[i][k] + d;
    }
}

static void open_door(int i)
{
    if (i < 0 || i >= START_NDOOR) return;
    if (s_door_state[i] == 0 || s_door_state[i] == 3) s_door_state[i] = 1;
}

static void doors(float dt)
{
    int i;
    for (i = 0; i < START_NDOOR; i++) {
        float mins[3], maxs[3];
        if (start_door_touch[i]) {
            door_bounds(i, mins, maxs);
            mins[0] -= 8.0f; mins[1] -= 8.0f; mins[2] -= 8.0f;
            maxs[0] += 8.0f; maxs[1] += 8.0f; maxs[2] += 8.0f;
            if (overlap_player(mins, maxs)) open_door(i);
        }
        if (s_door_state[i] == 1) {
            s_door_pos[i] += start_door_speed[i] * dt;
            if (s_door_pos[i] >= start_door_dist[i]) {
                s_door_pos[i] = start_door_dist[i];
                s_door_state[i] = 2;
                s_door_wait[i] = start_door_wait[i];
            }
        } else if (s_door_state[i] == 2) {
            if (start_door_wait[i] >= 0.0f) {
                s_door_wait[i] -= dt;
                if (s_door_wait[i] <= 0.0f) s_door_state[i] = 3;
            }
        } else if (s_door_state[i] == 3) {
            s_door_pos[i] -= start_door_speed[i] * dt;
            if (s_door_pos[i] <= 0.0f) {
                s_door_pos[i] = 0.0f;
                s_door_state[i] = 0;
            }
        }
    }
}

static void give_weapon(int w, int shells, int nails, int rockets, int cells)
{
    if (w < 0 || w > 7) return;
    s_weapons |= 1 << w;
    s_weapon = w;
    s_shells += shells;
    s_nails += nails;
    s_rockets += rockets;
    s_cells += cells;
}

static void take_pickup(int i)
{
    int k = start_pick_kind[i];
    s_taken[i] = 1;
    if (k == START_PICK_HEALTH) {
        if (s_health < 100) s_health += 25;
        if (s_health > 100) s_health = 100;
    } else if (k == START_PICK_SHELLS) s_shells += 20;
    else if (k == START_PICK_NAILS) s_nails += 25;
    else if (k == START_PICK_ROCKETS) s_rockets += 5;
    else if (k == START_PICK_CELLS) s_cells += 6;
    else if (k == START_PICK_ARMOR_G && s_armor < 100) s_armor = 100;
    else if (k == START_PICK_ARMOR_Y && s_armor < 150) s_armor = 150;
    else if (k == START_PICK_ARMOR_R && s_armor < 200) s_armor = 200;
    else if (k == START_PICK_QUAD) s_quad_until = s_now + 30000u;
    else if (k == START_PICK_SHOT) give_weapon(START_W_SHOT, 10, 0, 0, 0);
    else if (k == START_PICK_SSG) give_weapon(START_W_SSG, 10, 0, 0, 0);
    else if (k == START_PICK_NAIL) give_weapon(START_W_NAIL, 0, 20, 0, 0);
    else if (k == START_PICK_SNAIL) give_weapon(START_W_SNAIL, 0, 30, 0, 0);
    else if (k == START_PICK_GL) give_weapon(START_W_GL, 0, 0, 5, 0);
    else if (k == START_PICK_RL) give_weapon(START_W_RL, 0, 0, 5, 0);
    else if (k == START_PICK_LG) give_weapon(START_W_LG, 0, 0, 0, 15);
}

static void pickups(void)
{
    int i;
    if (s_dead) return;
    for (i = 0; i < START_NPICK; i++) {
        float dx, dy, dz;
        if (s_taken[i]) continue;
        dx = s_org[0] - start_pick_org[i][0];
        dy = s_org[1] - start_pick_org[i][1];
        dz = s_org[2] - start_pick_org[i][2];
        if (dx * dx + dy * dy + dz * dz < 56.0f * 56.0f) take_pickup(i);
    }
}

static void hurt_player(int dmg)
{
    int absorbed;
    if (s_dead || dmg <= 0) return;
    if (s_armor > 0) {
        absorbed = dmg / 2;
        if (absorbed > s_armor) absorbed = s_armor;
        s_armor -= absorbed;
        dmg -= absorbed;
    }
    s_health -= dmg;
    if (s_health <= 0) {
        s_health = 0;
        s_dead = 1;
        s_dead_until = s_now + 1000u;
        memcpy(s_msg, "Died", 5);
    }
}

static void hurt_zombie(int i, int dmg)
{
    if (i < 0 || i >= START_NZOMBIE || s_zstate[i] == 3) return;
    if (s_quad_until > s_now) dmg *= 2;
    s_zhp[i] -= dmg;
    s_zstate[i] = (s_zhp[i] <= 0) ? 3 : 1;
    if (s_zhp[i] < -20) s_zstate[i] = 3;
}

static int seg_hit_zombie(const float *a, const float *b, int *which, float *best)
{
    int i, hit = 0;
    for (i = 0; i < START_NZOMBIE; i++) {
        float ab[3], ap[3], c[3], t, L2, dx, dy, dz, dist;
        if (s_zstate[i] == 3) continue;
        ab[0] = b[0] - a[0]; ab[1] = b[1] - a[1]; ab[2] = b[2] - a[2];
        L2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
        if (L2 < 1.0f) continue;
        c[0] = s_zorg[i][0]; c[1] = s_zorg[i][1]; c[2] = s_zorg[i][2] + 16.0f;
        ap[0] = c[0] - a[0]; ap[1] = c[1] - a[1]; ap[2] = c[2] - a[2];
        t = (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / L2;
        if (t < 0.0f || t > 1.0f) continue;
        dx = a[0] + ab[0] * t - c[0];
        dy = a[1] + ab[1] * t - c[1];
        dz = a[2] + ab[2] * t - c[2];
        dist = dx * dx + dy * dy + dz * dz;
        if (dist < 24.0f * 24.0f && t < *best) {
            *best = t;
            *which = i;
            hit = 1;
        }
    }
    return hit;
}

static int spawn_shot(const float *o, const float *v, int kind, int dmg)
{
    int i;
    for (i = 0; i < MAX_SHOT; i++) {
        if (s_shot[i].alive) continue;
        s_shot[i].o[0] = o[0]; s_shot[i].o[1] = o[1]; s_shot[i].o[2] = o[2];
        s_shot[i].v[0] = v[0]; s_shot[i].v[1] = v[1]; s_shot[i].v[2] = v[2];
        s_shot[i].kind = kind;
        s_shot[i].dmg = dmg;
        s_shot[i].alive = 1;
        return 1;
    }
    return 0;
}

static void explode(const float *p, int dmg, float rad)
{
    int i;
    float r2 = rad * rad;
    for (i = 0; i < START_NZOMBIE; i++) {
        float dx = s_zorg[i][0] - p[0];
        float dy = s_zorg[i][1] - p[1];
        float dz = (s_zorg[i][2] + 16.0f) - p[2];
        if (dx * dx + dy * dy + dz * dz < r2) hurt_zombie(i, dmg);
    }
    {
        float dx = s_org[0] - p[0];
        float dy = s_org[1] - p[1];
        float dz = (s_org[2] + 16.0f) - p[2];
        if (dx * dx + dy * dy + dz * dz < r2) hurt_player(dmg / 2);
    }
}

static void projectiles(float dt)
{
    int i;
    for (i = 0; i < MAX_SHOT; i++) {
        tr_t tr;
        float end[3];
        int which = -1;
        float best = 2.0f;
        if (!s_shot[i].alive) continue;
        if (s_shot[i].kind == 3) s_shot[i].v[2] -= 400.0f * dt;
        end[0] = s_shot[i].o[0] + s_shot[i].v[0] * dt;
        end[1] = s_shot[i].o[1] + s_shot[i].v[1] * dt;
        end[2] = s_shot[i].o[2] + s_shot[i].v[2] * dt;
        if (seg_hit_zombie(s_shot[i].o, end, &which, &best)) {
            if (s_shot[i].kind == 1) hurt_zombie(which, s_shot[i].dmg);
            else explode(s_zorg[which], s_shot[i].dmg, s_shot[i].kind == 2 ? 120.0f : 80.0f);
            s_shot[i].alive = 0;
            continue;
        }
        trace(&tr, s_shot[i].o, end);
        if (tr.fraction < 1.0f || tr.startsolid) {
            if (s_shot[i].kind == 1) {
                /* Nail: stop on a wall. */
            } else {
                explode(tr.end, s_shot[i].dmg, s_shot[i].kind == 2 ? 120.0f : 80.0f);
            }
            s_shot[i].alive = 0;
            continue;
        }
        s_shot[i].o[0] = tr.end[0];
        s_shot[i].o[1] = tr.end[1];
        s_shot[i].o[2] = tr.end[2];
        if (s_shot[i].o[2] < -800.0f || s_shot[i].o[2] > 2000.0f) s_shot[i].alive = 0;
    }
}

static void hitscan(const float *eye, const float *dir, float range, int dmg)
{
    float end[3];
    tr_t tr;
    int which = -1;
    float best = 2.0f;
    /* The trace is the player hull, so a zombie against a wall sits a
     * few units past the expanded plane. Accept that slop. */
    end[0] = eye[0] + dir[0] * range;
    end[1] = eye[1] + dir[1] * range;
    end[2] = eye[2] + dir[2] * range;
    if (!seg_hit_zombie(eye, end, &which, &best)) return;
    trace(&tr, eye, end);
    if (best * range <= tr.fraction * range + 32.0f)
        hurt_zombie(which, dmg);
}

static void fire(void)
{
    float eye[3], fwd[3], right[3], up[3], dir[3];
    int w, i, dmg;
    if (s_dead) return;
    if (s_now < s_next_fire) return;
    w = s_weapon;
    if (w < 0 || w > 7) w = 0;
    if ((s_weapons & (1 << w)) == 0) return;
    eye[0] = s_org[0]; eye[1] = s_org[1]; eye[2] = s_org[2] + 22.0f;
    angles(fwd, right, up);
    if (w == START_W_AXE) {
        s_next_fire = s_now + (uint32_t)s_cool[w];
        hitscan(eye, fwd, 64.0f, 24);
        return;
    }
    if (w == START_W_SHOT || w == START_W_SSG) {
        int pellets = (w == START_W_SSG) ? 14 : 6;
        int cost = (w == START_W_SSG) ? 2 : 1;
        if (s_shells < cost) return;
        s_shells -= cost;
        s_next_fire = s_now + (uint32_t)s_cool[w];
        dmg = (w == START_W_SSG) ? 4 : 6;
        for (i = 0; i < pellets; i++) {
            dir[0] = fwd[0] + right[0] * ((rnd() - 128) / 1600.0f) + up[0] * ((rnd() - 128) / 1600.0f);
            dir[1] = fwd[1] + right[1] * ((rnd() - 128) / 1600.0f) + up[1] * ((rnd() - 128) / 1600.0f);
            dir[2] = fwd[2] + up[2] * ((rnd() - 128) / 1600.0f);
            norm3(dir);
            hitscan(eye, dir, 1024.0f, dmg);
        }
        return;
    }
    if (w == START_W_NAIL || w == START_W_SNAIL) {
        int cost = (w == START_W_SNAIL) ? 2 : 1;
        float speed = (w == START_W_SNAIL) ? 1000.0f : 900.0f;
        if (s_nails < cost) return;
        s_nails -= cost;
        s_next_fire = s_now + (uint32_t)s_cool[w];
        dir[0] = fwd[0] * speed; dir[1] = fwd[1] * speed; dir[2] = fwd[2] * speed;
        spawn_shot(eye, dir, 1, 9);
        return;
    }
    if (w == START_W_GL || w == START_W_RL) {
        float speed = (w == START_W_RL) ? 900.0f : 600.0f;
        if (s_rockets < 1) return;
        s_rockets -= 1;
        s_next_fire = s_now + (uint32_t)s_cool[w];
        eye[0] += fwd[0] * 16.0f; eye[1] += fwd[1] * 16.0f; eye[2] += fwd[2] * 16.0f;
        dir[0] = fwd[0] * speed; dir[1] = fwd[1] * speed; dir[2] = fwd[2] * speed;
        spawn_shot(eye, dir, w == START_W_RL ? 2 : 3, w == START_W_RL ? 110 : 80);
        return;
    }
    if (w == START_W_LG) {
        if (s_cells < 1) return;
        s_cells -= 1;
        s_next_fire = s_now + (uint32_t)s_cool[w];
        hitscan(eye, fwd, 600.0f, 30);
    }
}

static void zombies(float dt)
{
    int i;
    for (i = 0; i < START_NZOMBIE; i++) {
        float dx, dy, dz, dist, wish[3], end[3];
        tr_t tr;
        if (s_zstate[i] == 3) continue;
        dx = s_org[0] - s_zorg[i][0];
        dy = s_org[1] - s_zorg[i][1];
        dz = s_org[2] - s_zorg[i][2];
        dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (s_zstate[i] == 0 && dist < 700.0f) s_zstate[i] = 1;
        if (s_zstate[i] == 0) continue;
        if (dist > 1.0f) s_zyaw[i] = qatan2(dy, dx) * (180.0f / 3.14159265f);
        if (dist < 52.0f) {
            s_zstate[i] = 2;
            if (s_now >= s_znext[i]) {
                s_znext[i] = s_now + 900u;
                hurt_player(10);
            }
            continue;
        }
        s_zstate[i] = 1;
        wish[0] = dx / dist * 50.0f;
        wish[1] = dy / dist * 50.0f;
        wish[2] = 0.0f;
        end[0] = s_zorg[i][0] + wish[0] * dt;
        end[1] = s_zorg[i][1] + wish[1] * dt;
        end[2] = s_zorg[i][2];
        trace(&tr, s_zorg[i], end);
        s_zorg[i][0] = tr.end[0];
        s_zorg[i][1] = tr.end[1];
        end[2] = s_zorg[i][2] - 24.0f;
        trace(&tr, s_zorg[i], end);
        if (tr.fraction < 1.0f && tr.n[2] > 0.7f) s_zorg[i][2] = tr.end[2];
    }
}

static void triggers(void)
{
    int i;
    if (s_dead) return;
    for (i = 0; i < START_NTRIG; i++) {
        if (!overlap_player(start_trig_mins[i], start_trig_maxs[i])) continue;
        if (start_trig_kind[i] == START_TRIG_SLIP) {
            s_slip = 1;
            memcpy(s_msg, "Walk into the Slipgate to start playing Quake!", 46);
        } else if (start_trig_kind[i] == START_TRIG_TELE) {
            int d = start_trig_b[i];
            if (d >= 0 && d < START_NDEST && !s_tele_block) {
                s_org[0] = start_dest[d][0];
                s_org[1] = start_dest[d][1];
                s_org[2] = start_dest[d][2];
                s_yaw = start_dest[d][3];
                s_vel[0] = s_vel[1] = s_vel[2] = 0.0f;
                s_tele_block = 15;
            }
        } else if (start_trig_kind[i] == START_TRIG_DOOR) {
            open_door(start_trig_a[i]);
        }
    }
    if (s_tele_block > 0) s_tele_block--;
}

static void respawn(void)
{
    int i;
    s_org[0] = START_SPAWN_X;
    s_org[1] = START_SPAWN_Y;
    s_org[2] = START_SPAWN_Z;
    for (i = 0; i < 8; i++) {
        if (!solid_leaf(contents_at(START_HULL1, s_org))) break;
        s_org[2] += 8.0f;
    }
    s_vel[0] = s_vel[1] = s_vel[2] = 0.0f;
    s_yaw = START_SPAWN_YAW;
    s_pitch = 0.0f;
    s_health = 100;
    s_dead = 0;
    memcpy(s_msg, start_title, strlen(start_title) + 1);
}

static int bit_of(int key)
{
    if (key == 'w' || key == 'W') return BIT_FWD;
    if (key == 's' || key == 'S') return BIT_BACK;
    if (key == 'a' || key == 'A') return BIT_LEFT;
    if (key == 'd' || key == 'D') return BIT_RIGHT;
    if (key == ' ') return BIT_JUMP;
    if (key == START_KEY_FIRE || key == 'f' || key == 'F') return BIT_FIRE;
    /* PicoCalc arrows are the Quake cluster: up/down walk, left/right turn.
     * q and z look. WASD stays walk and strafe. */
    if (key == 0xB5) return BIT_FWD;
    if (key == 0xB6) return BIT_BACK;
    if (key == 0xB4) return BIT_LL;
    if (key == 0xB7) return BIT_LR;
    if (key == 'q' || key == 'Q') return BIT_LU;
    if (key == 'z' || key == 'Z') return BIT_LD;
    return 0;
}

static int bit_index(int bit)
{
    int i = 0;
    while (bit > 1) { bit >>= 1; i++; }
    return i;
}

void start_sim_key(int key, int down)
{
    int bit, idx, w;
    if (!s_ready) start_sim_init();
    if (down && key >= '1' && key <= '8') {
        w = key - '1';
        if (s_weapons & (1 << w)) s_weapon = w;
        return;
    }
    if (down && (key == 'e' || key == 'E')) {
        int n;
        for (n = 1; n <= 8; n++) {
            w = (s_weapon + n) & 7;
            if (s_weapons & (1 << w)) { s_weapon = w; break; }
        }
        return;
    }
    bit = bit_of(key);
    if (!bit) return;
    idx = bit_index(bit);
    if (down) {
        s_bits |= bit;
        /* Latch. The tick stamps the deadline from its own clock. A frame
         * longer than the old 500 ms used to clear the key before physics. */
        s_hold[idx] = 0xFFFFFFFFu;
    } else {
        s_bits &= ~bit;
        s_hold[idx] = 0;
    }
}

void start_sim_init(void)
{
    int i;
    memset(s_door_pos, 0, sizeof s_door_pos);
    memset(s_door_state, 0, sizeof s_door_state);
    memset(s_taken, 0, sizeof s_taken);
    memset(s_shot, 0, sizeof s_shot);
    memset(s_hold, 0, sizeof s_hold);
    s_bits = 0;
    s_ground = 0;
    s_armor = 0;
    s_shells = s_nails = s_rockets = s_cells = 0;
    s_weapons = 1 << START_W_AXE;
    s_weapon = START_W_AXE;
    s_quad_until = 0;
    s_slip = 0;
    s_dead = 0;
    s_next_fire = 0;
    s_tele_block = 0;
    s_now = 0;
    for (i = 0; i < START_NZOMBIE; i++) {
        s_zorg[i][0] = start_zombie[i][0];
        s_zorg[i][1] = start_zombie[i][1];
        s_zorg[i][2] = start_zombie[i][2];
        s_zyaw[i] = start_zombie[i][3];
        s_zhp[i] = 60;
        s_zstate[i] = 0;
        s_znext[i] = 0;
    }
    s_ready = 1;
    respawn();
    for (i = 0; i < 20; i++) physics(0.05f);
}

void start_sim_tick(uint32_t now_ms)
{
    float dt;
    int i;
    uint32_t prev;
    if (!s_ready) start_sim_init();
    prev = s_now;
    /* The first tick adopts the device clock. A latched key is stamped
     * after that, so a jump in now_ms cannot expire it in this same tick. */
    if (prev == 0) {
        s_now = now_ms ? now_ms : 1u;
        dt = 0.05f;
    } else {
        s_now = now_ms;
        if (now_ms < prev) dt = 0.05f;
        else dt = (float)(now_ms - prev) / 1000.0f;
        if (dt < 0.0f) dt = 0.05f;
        if (dt > 0.1f) dt = 0.1f;
    }
    for (i = 0; i < 10; i++) {
        if (s_hold[i] == 0xFFFFFFFFu)
            s_hold[i] = s_now + 2000u;
        else if (s_hold[i] && s_now > s_hold[i]) {
            s_bits &= ~(1 << i);
            s_hold[i] = 0;
        }
    }
    if (s_dead) {
        if (s_now >= s_dead_until) respawn();
        return;
    }
    physics(dt);
    doors(dt);
    pickups();
    triggers();
    if (s_bits & BIT_FIRE) fire();
    zombies(dt);
    projectiles(dt);
}

static int s_budget;

static void emit_face(int fi, float ox, float oy, float oz, const float *eye)
{
    int ntri, t, k, plane, side;
    float d, xyz[9];
    uint32_t rgba;
    if (fi < 0 || fi >= START_NFACE || s_budget <= 0) return;
    plane = start_face_plane[fi];
    d = plane_of(plane, eye);
    side = start_face_side[fi];
    if (side) d = -d;
    if (d < 1.0f) return;
    ntri = start_face_ntri[fi];
    rgba = start_face_rgba[fi];
    for (t = 0; t < ntri && s_budget > 0; t++) {
        for (k = 0; k < 3; k++) {
            int vi = start_idx[(start_face_tri0[fi] + t) * 3 + k];
            if (vi < 0 || vi >= START_NVERT) return;
            xyz[k * 3 + 0] = (float)start_vert[vi][0] + ox;
            xyz[k * 3 + 1] = (float)start_vert[vi][1] + oy;
            xyz[k * 3 + 2] = (float)start_vert[vi][2] + oz;
        }
        picogl_tris(xyz, NULL, 1, rgba, -1);
        s_budget--;
    }
}

static int ahead(const int16_t mins[3], const int16_t maxs[3], const float *eye, const float *fwd)
{
    float cx, cy, cz, dx, dy, dz, rad, dot;
    cx = 0.5f * ((float)mins[0] + (float)maxs[0]);
    cy = 0.5f * ((float)mins[1] + (float)maxs[1]);
    cz = 0.5f * ((float)mins[2] + (float)maxs[2]);
    dx = cx - eye[0]; dy = cy - eye[1]; dz = cz - eye[2];
    rad = 0.5f * ((float)(maxs[0] - mins[0]) + (float)(maxs[1] - mins[1]) + (float)(maxs[2] - mins[2]));
    dot = dx * fwd[0] + dy * fwd[1] + dz * fwd[2];
    return dot > -rad - 32.0f;
}

typedef struct { int node, stage, front; } node_fr_t;
static node_fr_t s_node_fr[128];

static void walk_node(int node, const float *eye, const float *fwd)
{
    int sp = 0;
    s_node_fr[0].node = node;
    s_node_fr[0].stage = 0;
    while (sp >= 0) {
        int n = s_node_fr[sp].node;
        int stage = s_node_fr[sp].stage;
        if (stage == 0) {
            int front;
            float d, p[3];
            if (n < 0 || s_budget <= 0 || n >= START_NNODE ||
                !ahead(start_node_mins[n], start_node_maxs[n], eye, fwd)) {
                sp--;
                continue;
            }
            p[0] = eye[0]; p[1] = eye[1]; p[2] = eye[2];
            d = plane_of(start_node_plane[n], p);
            front = (d >= 0.0f) ? 0 : 1;
            s_node_fr[sp].stage = 1;
            s_node_fr[sp].front = front;
            if (sp + 1 >= 128) { sp--; continue; }
            sp++;
            s_node_fr[sp].node = start_node_child[n][front];
            s_node_fr[sp].stage = 0;
        } else if (stage == 1) {
            unsigned f, cnt;
            int front = s_node_fr[sp].front;
            s_node_fr[sp].stage = 2;
            cnt = start_node_nface[n];
            for (f = 0; f < cnt && s_budget > 0; f++) {
                int fi = (int)start_node_face0[n] + (int)f;
                if (fi >= 0 && fi < START_NFACE && start_face_model[fi] == 0)
                    emit_face(fi, 0, 0, 0, eye);
            }
            if (sp + 1 >= 128) { sp--; continue; }
            sp++;
            s_node_fr[sp].node = start_node_child[n][front ^ 1];
            s_node_fr[sp].stage = 0;
        } else {
            sp--;
        }
    }
}

static void xform_vert(const int16_t *v, const float *org,
                      float cy, float sy, float cp, float sp, float *o)
{
    float x = (float)v[0], y = (float)v[1], z = (float)v[2];
    float fx = cp * cy, fy = cp * sy, fz = -sp;
    float rx = sy, ry = -cy;
    float ux = sp * cy, uy = sp * sy, uz = cp;
    o[0] = org[0] + x * fx - y * rx + z * ux;
    o[1] = org[1] + x * fy - y * ry + z * uy;
    o[2] = org[2] + x * fz + z * uz;
}

static void draw_mesh(int mesh, const float *org, float yaw, float pitch)
{
    int n, i, k;
    uint32_t rgba;
    float yr, pr, cy, sy, cp, sp;
    if (mesh < 0 || mesh >= START_NMESH || s_budget <= 0) return;
    n = start_mesh_n[mesh];
    rgba = start_mesh_rgba[mesh];
    if (n < 3) return;
    yr = yaw * (3.14159265f / 180.0f);
    pr = pitch * (3.14159265f / 180.0f);
    cy = cosf(yr);
    sy = sinf(yr);
    cp = cosf(pr);
    sp = sinf(pr);
    for (i = 0; i + 2 < n && s_budget > 0; i += 3) {
        float xyz[9];
        for (k = 0; k < 3; k++) {
            const int16_t *v = start_avert[start_mesh_v0[mesh] + i + k];
            xform_vert(v, org, cy, sy, cp, sp, xyz + k * 3);
        }
        picogl_tris(xyz, NULL, 1, rgba, -1);
        s_budget--;
    }
}

static void draw_box(const float *org, float s, uint32_t rgba)
{
    static const int8_t corner[8][3] = {
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
    };
    static const int8_t face[12][3] = {
        {0,1,2},{0,2,3},{4,6,5},{4,7,6},
        {0,4,5},{0,5,1},{1,5,6},{1,6,2},
        {2,6,7},{2,7,3},{3,7,4},{3,4,0}
    };
    int i;
    float p[8][3];
    for (i = 0; i < 8; i++) {
        p[i][0] = org[0] + corner[i][0] * s;
        p[i][1] = org[1] + corner[i][1] * s;
        p[i][2] = org[2] + corner[i][2] * s;
    }
    for (i = 0; i < 12 && s_budget > 0; i++) {
        float xyz[9];
        int k;
        for (k = 0; k < 3; k++) {
            xyz[k * 3] = p[face[i][k]][0];
            xyz[k * 3 + 1] = p[face[i][k]][1];
            xyz[k * 3 + 2] = p[face[i][k]][2];
        }
        picogl_tris(xyz, NULL, 1, rgba, -1);
        s_budget--;
    }
}

static void plot(int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= VW || y >= VH) return;
    s_rgb[y * VW + x] = c;
}

static void draw_text(int x, int y, const char *s, uint16_t c)
{
    int col = x;
    if (!s) return;
    while (*s) {
        unsigned ch = (unsigned char)*s++;
        int row, bit;
        if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
        if (* (s - 1) == '\n' || col > VW - 5) {
            col = x;
            y += 6;
            if (*(s - 1) == '\n') continue;
        }
        if (ch < 32 || ch > 90) { col += 5; continue; }
        for (row = 0; row < 5; row++) {
            uint8_t bits = start_font[ch - 32][row];
            for (bit = 0; bit < 4; bit++) {
                if (bits & (uint8_t)(8 >> bit)) plot(col + bit, y + row, c);
            }
        }
        col += 5;
    }
}

static const int s_viewmesh[8] = {
    START_MESH_VAXE, START_MESH_VSHOT, START_MESH_VSSG, START_MESH_VNAIL,
    START_MESH_VSNAIL, START_MESH_VGL, START_MESH_VRL, START_MESH_VLG
};

void start_sim_render(void)
{
    float eye[3], fwd[3], local[3];
    int i, x;
    char hud[40];
    if (!s_ready) start_sim_init();
    if (!s_bound) {
        picogl_bind_target(s_rgb, s_iz, VW, VH, VW);
        s_bound = 1;
#if defined(PICO_REAL_HW)
        pico_core1_launch();
#endif
    }
    picogl_begin_frame(VW, VH);
    picogl_set_cull(0);
    picogl_set_depth(1);
    picogl_clear_target(1, 1, CLEAR_RGB);
    eye[0] = s_org[0]; eye[1] = s_org[1]; eye[2] = s_org[2] + 22.0f;
    {
        float ang[3];
        ang[0] = s_pitch; ang[1] = s_yaw; ang[2] = 0.0f;
        picogl_set_camera(eye, ang, VW, VH);
    }
    angles(fwd, NULL, NULL);
    s_budget = TRI_ACTOR;
    if (s_weapon >= 0 && s_weapon < 8) {
        float vorg[3];
        /* v_*.mdl vertices start about 2 units in front of the eye. The
         * near plane is 8, so those vertices clip into a corner spike.
         * Park the model 16 units along the view. */
        vorg[0] = eye[0] + fwd[0] * 16.0f;
        vorg[1] = eye[1] + fwd[1] * 16.0f;
        vorg[2] = eye[2] + fwd[2] * 16.0f;
        draw_mesh(s_viewmesh[s_weapon], vorg, s_yaw, s_pitch);
    }
    for (i = 0; i < START_NZOMBIE; i++) {
        int mesh = START_MESH_ZSTAND;
        float dz;
        dz = s_zorg[i][0] - eye[0];
        if (dz * dz > 800.0f * 800.0f &&
            (s_zorg[i][1] - eye[1]) * (s_zorg[i][1] - eye[1]) > 800.0f * 800.0f)
            continue;
        if (s_zstate[i] == 1) mesh = START_MESH_ZWALK;
        else if (s_zstate[i] == 2) mesh = START_MESH_ZATK;
        else if (s_zstate[i] == 3) mesh = (s_zhp[i] < -20) ? START_MESH_ZGIB : START_MESH_ZDIE;
        draw_mesh(mesh, s_zorg[i], s_zyaw[i], 0.0f);
    }
    for (i = 0; i < START_NPICK; i++) {
        float dx, dy;
        if (s_taken[i]) continue;
        dx = start_pick_org[i][0] - eye[0];
        dy = start_pick_org[i][1] - eye[1];
        if (dx * dx + dy * dy > 700.0f * 700.0f) continue;
        if (start_pick_mesh[i] < START_NMESH)
            draw_mesh(start_pick_mesh[i], start_pick_org[i], 0.0f, 0.0f);
        else
            draw_box(start_pick_org[i], 8.0f, 0xFF40C040u);
    }
    for (i = 0; i < MAX_SHOT; i++) {
        int mesh = START_MESH_SPIKE;
        if (!s_shot[i].alive) continue;
        if (s_shot[i].kind == 2) mesh = START_MESH_MISSILE;
        else if (s_shot[i].kind == 3) mesh = START_MESH_GRENADE;
        draw_mesh(mesh, s_shot[i].o, s_yaw, 0.0f);
    }
    for (i = 0; i < START_NFLAME; i++) {
        float dx = start_flame[i][0] - eye[0];
        float dy = start_flame[i][1] - eye[1];
        int mesh = ((s_now / 200u) & 1u) ? START_MESH_FLAME2 : START_MESH_FLAME;
        if (dx * dx + dy * dy > 450.0f * 450.0f) continue;
        draw_mesh(mesh, start_flame[i], 0.0f, 0.0f);
    }
    s_budget = TRI_SUB;
    for (i = 1; i < START_NMODEL; i++) {
        unsigned f;
        float ox = 0, oy = 0, oz = 0;
        int door;
        if (!start_model_draw[i]) continue;
        for (door = 0; door < START_NDOOR; door++) {
            if (start_door_model[door] != i) continue;
            ox = start_door_dir[door][0] * s_door_pos[door];
            oy = start_door_dir[door][1] * s_door_pos[door];
            oz = start_door_dir[door][2] * s_door_pos[door];
            break;
        }
        local[0] = eye[0] - ox; local[1] = eye[1] - oy; local[2] = eye[2] - oz;
        for (f = 0; f < start_model_nface[i] && s_budget > 0; f++)
            emit_face((int)start_model_face0[i] + (int)f, ox, oy, oz, local);
    }
    s_budget = TRI_WORLD;
    walk_node(0, eye, fwd);
    picogl_flush_target();

    {
        int ammo = 0;
        if (s_weapon == START_W_SHOT || s_weapon == START_W_SSG) ammo = s_shells;
        else if (s_weapon == START_W_NAIL || s_weapon == START_W_SNAIL) ammo = s_nails;
        else if (s_weapon == START_W_GL || s_weapon == START_W_RL) ammo = s_rockets;
        else if (s_weapon == START_W_LG) ammo = s_cells;
        hud[0] = 'H';
        hud[1] = (char)('0' + (s_health / 100) % 10);
        hud[2] = (char)('0' + (s_health / 10) % 10);
        hud[3] = (char)('0' + s_health % 10);
        hud[4] = ' ';
        hud[5] = 'A';
        hud[6] = (char)('0' + (s_armor / 100) % 10);
        hud[7] = (char)('0' + (s_armor / 10) % 10);
        hud[8] = (char)('0' + s_armor % 10);
        hud[9] = ' ';
        hud[10] = s_wname[s_weapon][0];
        hud[11] = s_wname[s_weapon][1];
        hud[12] = ' ';
        hud[13] = (char)('0' + (ammo / 10) % 10);
        hud[14] = (char)('0' + ammo % 10);
        hud[15] = 0;
    }
    draw_text(2, 2, hud, 0xFFFFu);
    draw_text(2, VH - 12, s_msg, 0xFFE0u);
    s_lit = 0;
    for (x = 0; x < VW * VH; x++) if (s_rgb[x] != CLEAR_RGB) s_lit++;
}

const uint16_t *start_sim_pixels(void) { return s_rgb; }
int start_sim_view_w(void) { return VW; }
int start_sim_view_h(void) { return VH; }
int start_sim_lit(void) { return s_lit; }
const float *start_sim_origin(void) { return s_org; }
int start_sim_health(void) { return s_health; }
int start_sim_armor(void) { return s_armor; }
int start_sim_weapon(void) { return s_weapon; }
int start_sim_ammo(void)
{
    if (s_weapon == START_W_SHOT || s_weapon == START_W_SSG) return s_shells;
    if (s_weapon == START_W_NAIL || s_weapon == START_W_SNAIL) return s_nails;
    if (s_weapon == START_W_GL || s_weapon == START_W_RL) return s_rockets;
    if (s_weapon == START_W_LG) return s_cells;
    return 0;
}
int start_sim_onground(void) { return s_ground; }
int start_sim_has_weapon(int weapon)
{
    if (weapon < 0 || weapon > 7) return 0;
    return (s_weapons & (1 << weapon)) != 0;
}
int start_sim_solid(float x, float y, float z)
{
    float p[3];
    p[0] = x; p[1] = y; p[2] = z;
    return solid_leaf(contents_at(START_HULL1, p));
}
int start_sim_n_zombie(void) { return START_NZOMBIE; }
int start_sim_zombie_health(int i)
{
    if (i < 0 || i >= START_NZOMBIE) return 0;
    return s_zhp[i];
}
const float *start_sim_zombie_origin(int i)
{
    if (i < 0 || i >= START_NZOMBIE) return s_org;
    return s_zorg[i];
}
int start_sim_n_door(void) { return START_NDOOR; }
float start_sim_door_pos(int i)
{
    if (i < 0 || i >= START_NDOOR) return 0;
    return s_door_pos[i];
}
void start_sim_door_bounds(int i, float mins[3], float maxs[3])
{
    if (i < 0 || i >= START_NDOOR) return;
    door_bounds(i, mins, maxs);
}
int start_sim_n_pickup(void) { return START_NPICK; }
void start_sim_pickup(int i, float org[3], int *kind)
{
    if (i < 0 || i >= START_NPICK) return;
    org[0] = start_pick_org[i][0];
    org[1] = start_pick_org[i][1];
    org[2] = start_pick_org[i][2];
    if (kind) *kind = start_pick_kind[i];
}
int start_sim_n_trig(void) { return START_NTRIG; }
void start_sim_trig(int i, float mins[3], float maxs[3], int *kind)
{
    int k;
    if (i < 0 || i >= START_NTRIG) return;
    for (k = 0; k < 3; k++) {
        mins[k] = start_trig_mins[i][k];
        maxs[k] = start_trig_maxs[i][k];
    }
    if (kind) *kind = start_trig_kind[i];
}
int start_sim_projectiles(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_SHOT; i++) n += s_shot[i].alive;
    return n;
}
int start_sim_slipped(void) { return s_slip; }
const char *start_sim_message(void) { return s_msg; }
void start_sim_set_origin(float x, float y, float z)
{
    s_org[0] = x; s_org[1] = y; s_org[2] = z;
    s_vel[0] = s_vel[1] = s_vel[2] = 0.0f;
}
void start_sim_set_yaw(float yaw) { s_yaw = yaw; s_pitch = 0.0f; }
float start_sim_yaw(void) { return s_yaw; }
