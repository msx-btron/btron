/*
 * start_sim.c — playable cooked BSP without progs.dat.
 *
 * The host gate cooks maps/start.bsp. The picocalc quake image cooks
 * maps/e1m1.bsp and a drone camera track, and opens on a menu.
 * Collision is the player clip hull. Doors, walls, and gates are the other
 * hulls. The picture is flat-shaded BSP faces plus a few cooked mdl frames.
 * The host gate draws them into a 128x96 target. The SegUI capture uses
 * 320x320. The device records triangles at the selected render size and
 * scales that picture to the window, or to the 320x320 panel when
 * fullscreen. Sizes are 320x320, 160x320, 320x160, 160x160, 80x80,
 * 128x96, and 128x128. The menu and the status bar are the system font
 * in destination pixels, so r_mode does not scale them.
 */
#include "start_sim.h"
#include "start_blob.h"
#include "picogl.h"

#include <math.h>
#include <string.h>
#if defined(PICO_REAL_HW) || defined(SEGUI_HOST_QUAKE)
#include <btron/troncode.h>
#endif

#if defined(SEGUI_HOST_QUAKE)
#define VW 320
#define VH 320
#else
#define VW 128
#define VH 96
#endif
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

#if !defined(PICO_REAL_HW)
static uint16_t s_rgb[VW * VH];
static int32_t  s_iz[VW * VH];
static int      s_bound;
#endif
static int      s_lit;
static int      s_ready;

#if defined(PICO_REAL_HW) || defined(SEGUI_HOST_QUAKE)
/* Render size, then nearest scale onto the window or the panel.
 * Index 0 is a real 320x320. Direct paint happens only when that size
 * equals the destination. The boot picture is index 3, 160x160.
 * Menu rows are play, drone, then these seven sizes. */
#define RES_N      7
static const int s_res_w[RES_N] = { 320, 160, 320, 160, 80, 128, 128 };
static const int s_res_h[RES_N] = { 320, 320, 160, 160, 80,  96, 128 };
#define RES_FULL   0
#define RES_160    3
#define MENU_N     (2 + RES_N)
#endif

#if defined(PICO_REAL_HW)
/* The quake image boots into this menu. Fire runs the highlighted row.
 * M leaves play or the drone and comes back here. The drone camera is the
 * cooked track: the eye is the recorded origin, and the player hull does
 * not walk, so triggers along the route do not fire.
 * Rows 2..8 pick a render size and stay on the menu. Every size is scaled
 * to the window, or to the whole panel while fullscreen. */
#define MODE_MENU  0
#define MODE_PLAY  1
#define MODE_DRONE 2
#define CAP_TRI    896
#define CON_LOG    8
#define CON_COLS   32
static int      s_mode;
static int      s_menu_row = 1;
static int      s_cam_eye;
static uint32_t s_drone_t0;
static int      s_drone_i;
static int      s_drone_door_i;
static uint8_t  s_drone_hold[START_NDOOR];
static int      s_res;
static int      s_full;
static int      s_vw = 160;
static int      s_vh = 160;
static int      s_dw = 160;
static int      s_dh = 160;
static picogl_tri_t s_cap_tri[CAP_TRI];
static int      s_cap_n;
static uint16_t s_ord[CAP_TRI];
static uint16_t s_ord_tmp[CAP_TRI];
static int32_t  s_band_iz[320 * 16];
/* One band of a picture that is not already the destination. 320x16
 * RGB565. A matching size paints the strip and skips this. */
static uint16_t s_scale_rgb[320 * 16];
static int      s_ox, s_oy;
static GDEV    *s_dev;
static int      s_con;
static char     s_con_line[CON_COLS];
static int      s_con_len;
static char     s_con_log[CON_LOG][CON_COLS];
static int      s_con_n;

static void set_view_size(int dw, int dh)
{
    if (dw < 16) dw = 16;
    if (dh < 16) dh = 16;
    if (dw > 320) dw = 320;
    if (dh > 320) dh = 320;
    s_dw = dw;
    s_dh = dh;
    if (s_res < 0 || s_res >= RES_N) s_res = RES_160;
    s_vw = s_res_w[s_res];
    s_vh = s_res_h[s_res];
}
#endif

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
static int      s_god;
static int      s_noclip;
static int      s_fly;
static int      s_notarget;
static int      s_pause;
static uint32_t s_dead_until;
static int      s_bits;
static uint32_t s_hold[10];
static uint32_t s_now;
static uint32_t s_next_fire;
static int      s_slip;
static char     s_msg[64];
#if defined(PICO_REAL_HW)
static int put_dec(char *b, int n, int cap, int v)
{
    char tmp[4];
    int t = 0;
    if (v < 0) v = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0 && t < 4) {
        tmp[t++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (t > 0 && n < cap - 1) b[n++] = tmp[--t];
    if (n < cap) b[n] = 0;
    return n;
}

static void res_msg(void)
{
    int n = 0;
    int i = s_res;
    if (i < 0 || i >= RES_N) i = RES_160;
    n = put_dec(s_msg, n, (int)sizeof s_msg, s_res_w[i]);
    if (n < (int)sizeof s_msg - 1) s_msg[n++] = 'x';
    put_dec(s_msg, n, (int)sizeof s_msg, s_res_h[i]);
}
#endif
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
    if (s_noclip) {
        float step = 320.0f * dt;
        s_org[0] += wish[0] * step;
        s_org[1] += wish[1] * step;
        if (s_bits & BIT_JUMP) s_org[2] += step;
        s_vel[0] = s_vel[1] = s_vel[2] = 0.0f;
        s_ground = 0;
        return;
    }
    /* Quake fly keeps the hull and drops gravity. Jump climbs. */
    if (s_fly) {
        s_vel[0] = wish[0] * 320.0f;
        s_vel[1] = wish[1] * 320.0f;
        s_vel[2] = (s_bits & BIT_JUMP) ? 200.0f : 0.0f;
        s_ground = 0;
        slide(s_org, s_vel, dt, 0);
        s_vel[0] = s_vel[1] = s_vel[2] = 0.0f;
        return;
    }
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
    if (s_god) return;
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
    if (s_notarget) return;
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

#if defined(PICO_REAL_HW)
static void drone_reset_doors(void)
{
    memset(s_door_pos, 0, sizeof s_door_pos);
    memset(s_door_state, 0, sizeof s_door_state);
    memset(s_door_wait, 0, sizeof s_door_wait);
    memset(s_drone_hold, 0, sizeof s_drone_hold);
    s_drone_i = 0;
    s_drone_door_i = 0;
}

/* t is milliseconds from the start of this loop of the track. */
static void drone_pose(uint32_t t)
{
    int i, n, a, b, d;
    float u, x, y, z, pitch, yaw;
    n = START_NDRONE;
    if (n < 1) return;
    i = s_drone_i;
    if (i < 0 || i >= n) i = 0;
    while (i + 1 < n && start_drone_ms[i + 1] <= t) i++;
    while (i > 0 && start_drone_ms[i] > t) i--;
    s_drone_i = i;
    a = i;
    b = (i + 1 < n) ? i + 1 : i;
    u = 0.0f;
    if (b != a && start_drone_ms[b] > start_drone_ms[a]) {
        u = (float)(t - start_drone_ms[a]) /
            (float)(start_drone_ms[b] - start_drone_ms[a]);
    }
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    x = (float)start_drone_pos[a][0] +
        u * ((float)start_drone_pos[b][0] - (float)start_drone_pos[a][0]);
    y = (float)start_drone_pos[a][1] +
        u * ((float)start_drone_pos[b][1] - (float)start_drone_pos[a][1]);
    z = (float)start_drone_pos[a][2] +
        u * ((float)start_drone_pos[b][2] - (float)start_drone_pos[a][2]);
    pitch = (float)start_drone_ang[a][0] +
            u * ((float)start_drone_ang[b][0] - (float)start_drone_ang[a][0]);
    yaw = (float)start_drone_ang[a][1] +
          u * ((float)start_drone_ang[b][1] - (float)start_drone_ang[a][1]);
    s_org[0] = x;
    s_org[1] = y;
    s_org[2] = z;
    s_pitch = pitch;
    s_yaw = yaw;
    s_vel[0] = s_vel[1] = s_vel[2] = 0.0f;
    while (s_drone_door_i < START_NDRONE_DOOR &&
           start_drone_door_ms[s_drone_door_i] <= t) {
        d = start_drone_door[s_drone_door_i];
        if (d >= 0 && d < START_NDOOR) {
            open_door(d);
            s_drone_hold[d] = 1;
        }
        s_drone_door_i++;
    }
}

static void begin_play(void)
{
    int i;
    s_mode = MODE_PLAY;
    s_cam_eye = 0;
    s_bits = 0;
    memset(s_hold, 0, sizeof s_hold);
    respawn();
    for (i = 0; i < 20; i++) physics(0.05f);
}

static void begin_drone(void)
{
    s_mode = MODE_DRONE;
    s_cam_eye = 1;
    s_bits = 0;
    memset(s_hold, 0, sizeof s_hold);
    drone_reset_doors();
    s_drone_t0 = 0;
    drone_pose(0);
    memcpy(s_msg, "drone e1m1", 11);
}

static void enter_menu(void)
{
    s_mode = MODE_MENU;
    s_cam_eye = 0;
    s_bits = 0;
    memset(s_hold, 0, sizeof s_hold);
    respawn();
}

static int con_lower(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static const char *con_skip(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void con_trim(char *s)
{
    int i = 0, j = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    while (s[i]) s[j++] = s[i++];
    while (j > 0 && (s[j - 1] == ' ' || s[j - 1] == '\t' || s[j - 1] == ';'))
        j--;
    s[j] = 0;
}

static void con_say(const char *s)
{
    int i, n = 0;
    if (s_con_n >= CON_LOG) {
        for (i = 1; i < CON_LOG; i++)
            memcpy(s_con_log[i - 1], s_con_log[i], CON_COLS);
        i = CON_LOG - 1;
    } else {
        i = s_con_n++;
    }
    if (!s) s = "";
    while (s[n] && n < CON_COLS - 1) {
        s_con_log[i][n] = s[n];
        n++;
    }
    s_con_log[i][n] = 0;
}

static int con_is(const char *s, const char *w)
{
    while (*w) {
        if (con_lower((unsigned char)*s) != *w) return 0;
        s++;
        w++;
    }
    return *s == 0;
}

/* Word match. The character after the word is the end, space, '(', or '='. */
static int con_word(const char *s, const char *w, const char **rest)
{
    while (*w) {
        if (con_lower((unsigned char)*s) != *w) return 0;
        s++;
        w++;
    }
    if (*s && *s != ' ' && *s != '\t' && *s != '(' && *s != '=') return 0;
    if (rest) *rest = s;
    return 1;
}

static void app_ch(char *d, int *n, char c)
{
    if (*n < CON_COLS - 1) d[(*n)++] = c;
    d[*n] = 0;
}

static void app_s(char *d, int *n, const char *s)
{
    if (!s) return;
    while (*s) app_ch(d, n, *s++);
}

static void app_i(char *d, int *n, int v)
{
    char tmp[8];
    int i = 0;
    unsigned u;
    if (v < 0) {
        app_ch(d, n, '-');
        v = -v;
    }
    u = (unsigned)v;
    if (u == 0u) tmp[i++] = '0';
    while (u && i < 7) {
        tmp[i++] = (char)('0' + (u % 10u));
        u /= 10u;
    }
    while (i > 0) app_ch(d, n, tmp[--i]);
}

static int con_num(const char **ps, int *out)
{
    const char *s = *ps;
    int sign = 1, v = 0, any = 0;
    while (*s == ' ' || *s == '\t' || *s == '\'' || *s == '"' ||
           *s == ',' || *s == '(' || *s == '=')
        s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        if (v < 1000000) v = v * 10 + (*s - '0');
        any = 1;
        s++;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') s++;
    }
    if (!any) return 0;
    while (*s == '\'' || *s == '"' || *s == ')' || *s == ',') s++;
    *out = v * sign;
    *ps = s;
    return 1;
}

static void con_help(void)
{
    /* id's console names. The log is eight lines, so this page fits. */
    con_say("god noclip fly notarget");
    con_say("give all | h s n r c N");
    con_say("give 2-8  impulse 1-8");
    con_say("impulse 9 = give all");
    con_say("kill status pause map");
    con_say("changelevel restart quit");
    con_say("r_mode W H");
    con_say("fullscreen");
}

static void con_status(void)
{
    char b[CON_COLS];
    int n = 0;
    b[0] = 0;
    app_s(b, &n, "h");
    app_i(b, &n, s_health);
    app_s(b, &n, " a");
    app_i(b, &n, s_armor);
    app_s(b, &n, s_god ? " god" : "");
    app_s(b, &n, s_noclip ? " clip" : "");
    app_s(b, &n, s_fly ? " fly" : "");
    app_s(b, &n, s_notarget ? " nt" : "");
    con_say(b);
    n = 0;
    b[0] = 0;
    app_s(b, &n, "sh");
    app_i(b, &n, s_shells);
    app_s(b, &n, " na");
    app_i(b, &n, s_nails);
    app_s(b, &n, " ro");
    app_i(b, &n, s_rockets);
    app_s(b, &n, " ce");
    app_i(b, &n, s_cells);
    con_say(b);
    n = 0;
    b[0] = 0;
    app_s(b, &n, "w ");
    if (s_weapon >= 0 && s_weapon < 8) app_s(b, &n, s_wname[s_weapon]);
    con_say(b);
}

static void con_toggle_god(void)
{
    s_god = s_god ? 0 : 1;
    con_say(s_god ? "god on" : "god off");
}

static void con_toggle_noclip(void)
{
    s_noclip = s_noclip ? 0 : 1;
    con_say(s_noclip ? "noclip on" : "noclip off");
}

static void con_set_health(int v);
static int con_ammo(int v);

static void con_give_all(void)
{
    s_weapons = 0xFF;
    s_shells = s_nails = s_rockets = s_cells = 200;
    con_say("give all");
}

/* id give: 2 is the shotgun, 8 is the thunderbolt. 1 is the axe.
 * h s n r c a take a count. Words are the same letters. No float parse. */
static void con_give(const char *s)
{
    const char *rest;
    int v, key;
    s = con_skip(s);
    if (con_word(s, "all", 0)) { con_give_all(); return; }
    if (s[0] >= '1' && s[0] <= '8' && (s[1] == 0 || s[1] == ' ')) {
        int w = s[0] - '1';
        s_weapons |= 1 << w;
        s_weapon = w;
        con_say("give");
        return;
    }
    key = 0;
    rest = s;
    if (con_word(s, "health", &rest)) key = 'h';
    else if (con_word(s, "shells", &rest)) key = 's';
    else if (con_word(s, "nails", &rest)) key = 'n';
    else if (con_word(s, "rockets", &rest)) key = 'r';
    else if (con_word(s, "cells", &rest)) key = 'c';
    else if (con_word(s, "armor", &rest) || con_word(s, "armour", &rest)) key = 'a';
    else if (s[0] && (s[1] == 0 || s[1] == ' ' || s[1] == '\t')) {
        key = con_lower((unsigned char)s[0]);
        rest = s + 1;
    }
    if (key != 'h' && key != 's' && key != 'n' && key != 'r' &&
        key != 'c' && key != 'a') {
        con_say("give all | h s n r c");
        return;
    }
    if (!con_num(&rest, &v)) {
        con_say("give h s n r c <n>");
        return;
    }
    if (key == 'h') con_set_health(v);
    else if (key == 's') s_shells = con_ammo(v);
    else if (key == 'n') s_nails = con_ammo(v);
    else if (key == 'r') s_rockets = con_ammo(v);
    else if (key == 'c') s_cells = con_ammo(v);
    else {
        if (v < 0) v = 0;
        if (v > 999) v = 999;
        s_armor = v;
    }
    con_say("give");
}

static void con_impulse(const char *s)
{
    int v;
    if (!con_num(&s, &v)) { con_say("impulse 1-9"); return; }
    if (v == 9) { con_give_all(); return; }
    if (v >= 1 && v <= 8) {
        s_weapons |= 1 << (v - 1);
        s_weapon = v - 1;
        con_say("impulse");
        return;
    }
    con_say("impulse 1-9");
}

static void con_set_health(int v)
{
    if (v < 0) v = 0;
    if (v > 999) v = 999;
    s_health = v;
    if (v <= 0) {
        s_dead = 1;
        s_dead_until = s_now + 1000u;
    } else {
        s_dead = 0;
    }
}

static int con_ammo(int v)
{
    if (v < 0) return 0;
    if (v > 999) return 999;
    return v;
}

static const char *con_strip_self(const char *s)
{
    s = con_skip(s);
    if (s[0] && s[1] && s[2] && s[3] && s[4] == '.' &&
        con_lower((unsigned char)s[0]) == 's' &&
        con_lower((unsigned char)s[1]) == 'e' &&
        con_lower((unsigned char)s[2]) == 'l' &&
        con_lower((unsigned char)s[3]) == 'f')
        return s + 5;
    return s;
}

static void con_field(const char *s)
{
    char name[16];
    const char *eq;
    int i, v, x, y, z;
    s = con_strip_self(s);
    eq = s;
    while (*eq && *eq != '=' && *eq != ' ' && *eq != '\t') eq++;
    i = 0;
    while (s < eq && i < 15) name[i++] = (char)con_lower((unsigned char)*s++);
    name[i] = 0;
    if (con_is(name, "origin")) {
        s = eq;
        if (!con_num(&s, &x) || !con_num(&s, &y) || !con_num(&s, &z)) {
            con_say("origin x y z");
            return;
        }
        s_org[0] = (float)x;
        s_org[1] = (float)y;
        s_org[2] = (float)z;
        s_vel[0] = s_vel[1] = s_vel[2] = 0.0f;
        con_say("origin");
        return;
    }
    if (!con_num(&eq, &v)) {
        con_say("need N");
        return;
    }
    if (con_is(name, "health")) con_set_health(v);
    else if (con_is(name, "armor")) {
        if (v < 0) v = 0;
        if (v > 999) v = 999;
        s_armor = v;
    } else if (con_is(name, "shells")) s_shells = con_ammo(v);
    else if (con_is(name, "nails")) s_nails = con_ammo(v);
    else if (con_is(name, "rockets")) s_rockets = con_ammo(v);
    else if (con_is(name, "cells")) s_cells = con_ammo(v);
    else if (con_is(name, "weapon")) {
        if (v < 0 || v > 7) { con_say("0..7"); return; }
        s_weapons |= 1 << v;
        s_weapon = v;
    } else {
        con_say("?");
        return;
    }
    con_say("ok");
}

static void con_bprint(const char *s)
{
    char buf[CON_COLS];
    int n = 0;
    while (*s && *s != '"') s++;
    if (*s == '"') s++;
    while (*s && *s != '"' && n < CON_COLS - 1) buf[n++] = *s++;
    buf[n] = 0;
    con_say(buf);
}

static void toggle_full(void)
{
    s_full = s_full ? 0 : 1;
    if (s_full) memcpy(s_msg, "full", 5);
    else memcpy(s_msg, "window", 7);
}

static void con_exec(char *line)
{
    const char *s, *rest;
    int mode, v;
    con_trim(line);
    s = line;
    if (!*s) return;
    if (con_word(s, "help", 0)) { con_help(); return; }
    if (con_word(s, "status", 0)) { con_status(); return; }
    if (con_word(s, "god", 0)) { con_toggle_god(); return; }
    if (con_word(s, "noclip", 0)) { con_toggle_noclip(); return; }
    if (con_word(s, "fly", 0)) {
        s_fly = s_fly ? 0 : 1;
        con_say(s_fly ? "fly on" : "fly off");
        return;
    }
    if (con_word(s, "notarget", 0)) {
        s_notarget = s_notarget ? 0 : 1;
        con_say(s_notarget ? "notarget on" : "notarget off");
        return;
    }
    if (con_word(s, "give", &rest)) { con_give(con_skip(rest)); return; }
    if (con_word(s, "impulse", &rest)) { con_impulse(con_skip(rest)); return; }
    if (con_word(s, "kill", 0)) { con_set_health(0); con_say("kill"); return; }
    if (con_word(s, "pause", 0)) {
        s_pause = s_pause ? 0 : 1;
        con_say(s_pause ? "pause on" : "pause off");
        return;
    }
    if (con_word(s, "restart", 0)) { respawn(); con_say("restart"); return; }
    if (con_word(s, "map", &rest) || con_word(s, "changelevel", &rest)) {
        rest = con_skip(rest);
        if (*rest && !con_word(rest, "e1m1", 0)) {
            con_say("only e1m1");
            return;
        }
        if (s_mode != MODE_PLAY) begin_play();
        else respawn();
        con_say("e1m1");
        return;
    }
    if (con_word(s, "clear", 0)) { s_con_n = 0; return; }
    if (con_word(s, "quit", 0)) {
        s_con = 0;
        enter_menu();
        return;
    }
    if (con_word(s, "r_mode", &rest)) {
        int h, i, found;
        if (!con_num(&rest, &mode)) {
            con_say("r_mode W H");
            return;
        }
        if (!con_num(&rest, &h)) {
            if (mode == 320) h = 320;
            else if (mode == 160) h = 160;
            else if (mode == 80) h = 80;
            else {
                con_say("r_mode W H");
                return;
            }
        }
        found = -1;
        for (i = 0; i < RES_N; i++) {
            if (s_res_w[i] == mode && s_res_h[i] == h) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            con_say("r_mode W H");
            return;
        }
        s_res = found;
        set_view_size(s_dw, s_dh);
        res_msg();
        con_say(s_msg);
        return;
    }
    if (con_word(s, "fullscreen", &rest)) {
        if (con_num(&rest, &v)) s_full = v ? 1 : 0;
        else toggle_full();
        if (s_full) memcpy(s_msg, "full", 5);
        else memcpy(s_msg, "window", 7);
        con_say(s_full ? "full" : "window");
        return;
    }
    if (con_word(s, "echo", &rest)) { con_say(con_skip(rest)); return; }
    if (con_word(s, "bprint", &rest)) { con_bprint(rest); return; }
    if (con_word(s, "self", 0) ||
        (s[0] && s[1] && s[2] && s[3] && s[4] == '.')) {
        con_field(s);
        return;
    }
    if (con_word(s, "shells", 0) || con_word(s, "nails", 0) ||
        con_word(s, "rockets", 0) || con_word(s, "cells", 0) ||
        con_word(s, "health", 0) || con_word(s, "armor", 0) ||
        con_word(s, "weapon", 0) || con_word(s, "origin", 0)) {
        con_field(s);
        return;
    }
    con_say("?");
}

static void con_feed(int key)
{
    if (key == 0x08 || key == 0x7F) {
        if (s_con_len > 0) s_con_line[--s_con_len] = 0;
        return;
    }
    if (key == 0x0A || key == 0x0D) {
        con_exec(s_con_line);
        s_con_len = 0;
        s_con_line[0] = 0;
        return;
    }
    if (key == 0x1B) {
        s_con = 0;
        s_con_len = 0;
        s_con_line[0] = 0;
        return;
    }
    if (key >= 32 && key < 127 && s_con_len < CON_COLS - 1) {
        s_con_line[s_con_len++] = (char)key;
        s_con_line[s_con_len] = 0;
    }
}
#endif

static int bit_of(int key)
{
    if (key == 'w' || key == 'W') return BIT_FWD;
    if (key == 's' || key == 'S') return BIT_BACK;
    if (key == 'a' || key == 'A') return BIT_LEFT;
    if (key == 'd' || key == 'D') return BIT_RIGHT;
    if (key == ' ') return BIT_JUMP;
#if defined(PICO_REAL_HW)
    /* f toggles fullscreen on the device. Fire stays on space, enter, and
     * the PicoCalc fire key. The host gate still treats f as fire. */
    if (key == START_KEY_FIRE) return BIT_FIRE;
#else
    if (key == START_KEY_FIRE || key == 'f' || key == 'F') return BIT_FIRE;
#endif
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
#if defined(PICO_REAL_HW)
    if (down && key == 0x09) {
        s_con = s_con ? 0 : 1;
        s_con_len = 0;
        s_con_line[0] = 0;
        s_bits = 0;
        memset(s_hold, 0, sizeof s_hold);
        return;
    }
    if (s_con) {
        if (down) con_feed(key);
        return;
    }
    if (down && (key == 'f' || key == 'F')) {
        toggle_full();
        return;
    }
    if (s_mode == MODE_MENU) {
        if (!down) return;
        if (key == 0xB5 || key == 'w' || key == 'W') {
            s_menu_row = (s_menu_row + MENU_N - 1) % MENU_N;
            return;
        }
        if (key == 0xB6 || key == 's' || key == 'S') {
            s_menu_row = (s_menu_row + 1) % MENU_N;
            return;
        }
        if (key == 0x0A || key == ' ' || key == START_KEY_FIRE) {
            if (s_menu_row == 1) begin_drone();
            else if (s_menu_row >= 2 && s_menu_row < MENU_N) {
                s_res = s_menu_row - 2;
                set_view_size(s_dw, s_dh);
                res_msg();
            } else begin_play();
        }
        return;
    }
    if (down && (key == 'm' || key == 'M' || key == 0x82)) {
        enter_menu();
        return;
    }
    if (s_mode == MODE_DRONE) return;
#endif
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
    s_god = 0;
    s_noclip = 0;
    s_fly = 0;
    s_notarget = 0;
    s_pause = 0;
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
#if defined(PICO_REAL_HW)
    s_mode = MODE_MENU;
    s_cam_eye = 0;
    s_menu_row = 1;
    s_res = RES_160; /* 160x160 scaled up. The light boot picture. */
    s_full = 0;
    s_con = 0;
    s_con_len = 0;
    s_con_line[0] = 0;
    s_con_n = 0;
    s_vw = 160;
    s_vh = 160;
    s_dw = 160;
    s_dh = 160;
    s_cap_n = 0;
    s_dev = 0;
#endif
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
#if defined(PICO_REAL_HW)
    if (s_mode == MODE_MENU) return;
    if (s_pause) return;
    if (s_mode == MODE_DRONE) {
        uint32_t t, dur;
        int di;
        if (START_NDRONE > 0) {
            if (s_drone_t0 == 0 || s_now < s_drone_t0)
                s_drone_t0 = s_now ? s_now : 1u;
            t = s_now - s_drone_t0;
            dur = start_drone_ms[START_NDRONE - 1];
            if (t > dur) {
                s_drone_t0 = s_now ? s_now : 1u;
                drone_reset_doors();
                t = 0;
            }
            drone_pose(t);
        }
        doors(dt);
        for (di = 0; di < START_NDOOR; di++) {
            if (s_drone_hold[di] && s_door_state[di] == 2)
                s_door_wait[di] = 999.0f;
        }
        return;
    }
#endif
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

#if defined(PICO_REAL_HW)
/* BSP planes, once. Axial tests stay exact. General planes are Q10. */
static int s_rex, s_rey, s_rez;
static int s_rfx, s_rfy, s_rfz;
static int s_pl_ready;
static int8_t s_pt[START_NPLANE];
static int16_t s_pd[START_NPLANE];
static int16_t s_pn[START_NPLANE][3];

static void plane_cache(void)
{
    int i;
    if (s_pl_ready) return;
    for (i = 0; i < START_NPLANE; i++) {
        s_pt[i] = (int8_t)start_plane[i][4];
        s_pd[i] = (int16_t)start_plane[i][3];
        s_pn[i][0] = (int16_t)(start_plane[i][0] * 1024.0f);
        s_pn[i][1] = (int16_t)(start_plane[i][1] * 1024.0f);
        s_pn[i][2] = (int16_t)(start_plane[i][2] * 1024.0f);
    }
    s_pl_ready = 1;
}

static int plane_i(int plane, int x, int y, int z)
{
    int typ, d;
    if (plane < 0 || plane >= START_NPLANE) return 0;
    typ = s_pt[plane];
    d = s_pd[plane];
    if (typ == 0) return x - d;
    if (typ == 1) return y - d;
    if (typ == 2) return z - d;
    return (int)((((int32_t)s_pn[plane][0] * x
                 + (int32_t)s_pn[plane][1] * y
                 + (int32_t)s_pn[plane][2] * z) >> 10) - d);
}

static int ahead_i(const int16_t mins[3], const int16_t maxs[3])
{
    int cx = ((int)mins[0] + (int)maxs[0]) >> 1;
    int cy = ((int)mins[1] + (int)maxs[1]) >> 1;
    int cz = ((int)mins[2] + (int)maxs[2]) >> 1;
    int dx = cx - s_rex, dy = cy - s_rey, dz = cz - s_rez;
    int rad = ((int)(maxs[0] - mins[0]) + (int)(maxs[1] - mins[1])
               + (int)(maxs[2] - mins[2])) >> 1;
    int64_t dot = (int64_t)dx * s_rfx + (int64_t)dy * s_rfy + (int64_t)dz * s_rfz;
    return dot > -(((int64_t)rad + 32) << 14);
}

static const int16_t s_sin90[91] = {
    0,286,572,857,1143,1428,1713,1997,2280,2563,2845,3126,3406,3686,3964,
    4240,4516,4790,5063,5334,5604,5872,6138,6402,6664,6924,7182,7438,7692,
    7943,8192,8438,8682,8923,9162,9397,9630,9860,10087,10311,10531,10749,
    10963,11174,11381,11585,11786,11982,12176,12365,12551,12733,12911,13085,
    13255,13421,13583,13741,13894,14044,14189,14330,14466,14598,14726,14849,
    14968,15082,15191,15296,15396,15491,15582,15668,15749,15826,15897,15964,
    16026,16083,16135,16182,16225,16262,16294,16322,16344,16362,16374,16382,
    16384
};

static int isin_deg(int deg)
{
    int a = deg % 360;
    if (a < 0) a += 360;
    if (a <= 90) return s_sin90[a];
    if (a <= 180) return s_sin90[180 - a];
    if (a <= 270) return -s_sin90[a - 180];
    return -s_sin90[360 - a];
}

static int q14mul(int a, int b)
{
    return (int)(((int64_t)a * b) >> 14);
}

static void mesh_vert(const int16_t *v, int ox, int oy, int oz,
                      int cy, int sy, int cp, int sp, int *o)
{
    int x = v[0], y = v[1], z = v[2];
    int fx = q14mul(cp, cy), fy = q14mul(cp, sy), fz = -sp;
    int rx = sy, ry = -cy;
    int ux = q14mul(sp, cy), uy = q14mul(sp, sy), uz = cp;
    o[0] = ox + q14mul(x, fx) - q14mul(y, rx) + q14mul(z, ux);
    o[1] = oy + q14mul(x, fy) - q14mul(y, ry) + q14mul(z, uy);
    o[2] = oz + q14mul(x, fz) + q14mul(z, uz);
}
#endif

static void emit_face(int fi, float ox, float oy, float oz, const float *eye)
{
    int ntri, t, k, plane, side;
    uint32_t rgba;
    if (fi < 0 || fi >= START_NFACE || s_budget <= 0) return;
    plane = start_face_plane[fi];
    side = start_face_side[fi];
#if defined(PICO_REAL_HW)
    {
        int d = plane_i(plane, s_rex, s_rey, s_rez);
        int oxi = (int)ox, oyi = (int)oy, ozi = (int)oz;
        (void)eye;
        if (side) d = -d;
        if (d < 1) return;
        ntri = start_face_ntri[fi];
        rgba = start_face_rgba[fi];
        for (t = 0; t < ntri && s_budget > 0; t++) {
            int p[3][3];
            for (k = 0; k < 3; k++) {
                int vi = start_idx[(start_face_tri0[fi] + t) * 3 + k];
                if (vi < 0 || vi >= START_NVERT) return;
                p[k][0] = (int)start_vert[vi][0] + oxi;
                p[k][1] = (int)start_vert[vi][1] + oyi;
                p[k][2] = (int)start_vert[vi][2] + ozi;
            }
            picogl_world_tri(p[0][0], p[0][1], p[0][2],
                             p[1][0], p[1][1], p[1][2],
                             p[2][0], p[2][1], p[2][2], rgba);
            s_budget--;
        }
    }
#else
    {
    float d, xyz[9];
    d = plane_of(plane, eye);
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
#endif
}

#if !defined(PICO_REAL_HW)
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
#endif

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
#if defined(PICO_REAL_HW)
            if (n < 0 || s_budget <= 0 || n >= START_NNODE ||
                !ahead_i(start_node_mins[n], start_node_maxs[n])) {
                sp--;
                continue;
            }
            front = plane_i(start_node_plane[n], s_rex, s_rey, s_rez) >= 0 ? 0 : 1;
            (void)eye;
            (void)fwd;
#else
            float d, p[3];
            if (n < 0 || s_budget <= 0 || n >= START_NNODE ||
                !ahead(start_node_mins[n], start_node_maxs[n], eye, fwd)) {
                sp--;
                continue;
            }
            p[0] = eye[0]; p[1] = eye[1]; p[2] = eye[2];
            d = plane_of(start_node_plane[n], p);
            front = (d >= 0.0f) ? 0 : 1;
#endif
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

#if !defined(PICO_REAL_HW)
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
#endif

static void draw_mesh(int mesh, const float *org, float yaw, float pitch)
{
    int n, i, k;
    uint32_t rgba;
    if (mesh < 0 || mesh >= START_NMESH || s_budget <= 0) return;
    n = start_mesh_n[mesh];
    rgba = start_mesh_rgba[mesh];
    if (n < 3) return;
#if defined(PICO_REAL_HW)
    {
        int cy = isin_deg((int)yaw + 90);
        int sy = isin_deg((int)yaw);
        int cp = isin_deg((int)pitch + 90);
        int sp = isin_deg((int)pitch);
        int ox = (int)org[0], oy = (int)org[1], oz = (int)org[2];
        for (i = 0; i + 2 < n && s_budget > 0; i += 3) {
            int p[3][3];
            for (k = 0; k < 3; k++) {
                const int16_t *v = start_avert[start_mesh_v0[mesh] + i + k];
                mesh_vert(v, ox, oy, oz, cy, sy, cp, sp, p[k]);
            }
            picogl_world_tri(p[0][0], p[0][1], p[0][2],
                             p[1][0], p[1][1], p[1][2],
                             p[2][0], p[2][1], p[2][2], rgba);
            s_budget--;
        }
    }
#else
    {
    float yr, pr, cy, sy, cp, sp;
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
#endif
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
#if defined(PICO_REAL_HW)
    {
        int p[8][3];
        int ox = (int)org[0], oy = (int)org[1], oz = (int)org[2];
        int ss = (int)s;
        if (ss < 1) ss = 1;
        for (i = 0; i < 8; i++) {
            p[i][0] = ox + corner[i][0] * ss;
            p[i][1] = oy + corner[i][1] * ss;
            p[i][2] = oz + corner[i][2] * ss;
        }
        for (i = 0; i < 12 && s_budget > 0; i++) {
            picogl_world_tri(p[face[i][0]][0], p[face[i][0]][1], p[face[i][0]][2],
                             p[face[i][1]][0], p[face[i][1]][1], p[face[i][1]][2],
                             p[face[i][2]][0], p[face[i][2]][1], p[face[i][2]][2],
                             rgba);
            s_budget--;
        }
    }
#else
    {
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
#endif
}

static int view_w(void)
{
#if defined(PICO_REAL_HW)
    return s_vw;
#else
    return VW;
#endif
}

static int view_h(void)
{
#if defined(PICO_REAL_HW)
    return s_vh;
#else
    return VH;
#endif
}

#if !defined(PICO_REAL_HW)
static void plot(int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= view_w() || y >= view_h()) return;
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
        if (* (s - 1) == '\n' || col > view_w() - 5) {
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
#endif

static const int s_viewmesh[8] = {
    START_MESH_VAXE, START_MESH_VSHOT, START_MESH_VSSG, START_MESH_VNAIL,
    START_MESH_VSNAIL, START_MESH_VGL, START_MESH_VRL, START_MESH_VLG
};

#if defined(PICO_REAL_HW)
/* Nearer iz first. Equal iz keeps the earlier capture so a shared edge
 * pixel does not swap owners between frames. */
static void permute_tris(uint16_t *p, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        picogl_tri_t hold;
        int j, steps;
        uint16_t src;
        if (p[i] & 0x8000u) continue;
        src = (uint16_t)(p[i] & 0x7fffu);
        if (src == (uint16_t)i) {
            p[i] |= 0x8000u;
            continue;
        }
        hold = s_cap_tri[i];
        j = i;
        steps = 0;
        while (steps++ < n) {
            uint16_t from = (uint16_t)(p[j] & 0x7fffu);
            p[j] |= 0x8000u;
            if (from == (uint16_t)i) {
                s_cap_tri[j] = hold;
                break;
            }
            if (from >= (uint16_t)n) break;
            s_cap_tri[j] = s_cap_tri[from];
            j = (int)from;
        }
    }
}

static void sort_near(void)
{
    uint16_t *a = s_ord;
    uint16_t *b = s_ord_tmp;
    int n = s_cap_n;
    int i, w;
    if (n < 2 || n > CAP_TRI) {
        picogl_set_cover(0);
        return;
    }
    for (i = 0; i < n; i++) a[i] = (uint16_t)i;
    for (w = 1; w < n; w *= 2) {
        int lo;
        for (lo = 0; lo < n; lo += w * 2) {
            int mid = lo + w;
            int hi = mid + w;
            int i0, i1, o;
            if (mid > n) mid = n;
            if (hi > n) hi = n;
            i0 = lo;
            i1 = mid;
            o = lo;
            while (i0 < mid && i1 < hi) {
                int32_t z0 = s_cap_tri[a[i0]].iz;
                int32_t z1 = s_cap_tri[a[i1]].iz;
                if (z0 >= z1) b[o++] = a[i0++];
                else b[o++] = a[i1++];
            }
            while (i0 < mid) b[o++] = a[i0++];
            while (i1 < hi) b[o++] = a[i1++];
        }
        {
            uint16_t *t = a;
            a = b;
            b = t;
        }
    }
    permute_tris(a, n);
    picogl_set_cover(1);
}
#endif

void start_sim_render(void)
{
    float eye[3], fwd[3], local[3];
    int i;
    if (!s_ready) start_sim_init();
#if defined(PICO_REAL_HW)
    /* Record the frame. Each band paints the same list into the strip.
     * Core 1 stays awake so ps shows it ready. Quake spans are not posted:
     * the strip is also SegUI's, and a core-1 paint would race that fill. */
    picogl_capture(s_cap_tri, CAP_TRI);
    pico_core1_launch();
#else
    if (!s_bound) {
        picogl_bind_target(s_rgb, s_iz, VW, VH, VW);
        s_bound = 1;
    }
#endif
    picogl_begin_frame(view_w(), view_h());
    picogl_set_cull(0);
    picogl_set_depth(1);
#if !defined(PICO_REAL_HW)
    picogl_clear_target(1, 1, CLEAR_RGB);
#endif
    eye[0] = s_org[0];
    eye[1] = s_org[1];
#if defined(PICO_REAL_HW)
    /* Drone samples are the eye. Play keeps the standing view height. */
    eye[2] = s_cam_eye ? s_org[2] : s_org[2] + 22.0f;
#else
    eye[2] = s_org[2] + 22.0f;
#endif
    {
        float ang[3];
        ang[0] = s_pitch; ang[1] = s_yaw; ang[2] = 0.0f;
        picogl_set_camera(eye, ang, view_w(), view_h());
    }
    angles(fwd, NULL, NULL);
#if defined(PICO_REAL_HW)
    plane_cache();
    s_rex = (int)eye[0];
    s_rey = (int)eye[1];
    s_rez = (int)eye[2];
    s_rfx = (int)(fwd[0] * 16384.0f);
    s_rfy = (int)(fwd[1] * 16384.0f);
    s_rfz = (int)(fwd[2] * 16384.0f);
#endif
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

#if defined(PICO_REAL_HW)
    s_cap_n = picogl_captured();
    picogl_capture(0, 0);
    sort_near();
    s_lit = 0;
#else
    /* The host gate counts lit pixels. The device scan walked every
     * pixel after the picture was already on the panel. HUD glyphs are
     * in that count. The SegUI capture leaves them out: its canvas
     * paints the same lines with the system font after the blit.
     * test-start-sim does not define SEGUI_HOST_QUAKE. */
    {
        int x;
#if !defined(SEGUI_HOST_QUAKE)
        char hud[40];
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
        draw_text(2, 2, hud, 0xFFFFu);
        draw_text(2, VH - 12, s_msg, 0xFFE0u);
#endif
        s_lit = 0;
        for (x = 0; x < VW * VH; x++) if (s_rgb[x] != CLEAR_RGB) s_lit++;
    }
#endif
}

#if defined(PICO_REAL_HW) || defined(SEGUI_HOST_QUAKE)
/* Menu and status bar in destination pixels. r_mode does not scale them. */
#define CHROME_BG   0xFF0A0A0Eu
#define CHROME_HI   0xFF3A2814u
#define CHROME_RULE 0xFF8C2020u
#define CHROME_GOLD 0xFFD49A24u
#define CHROME_BAR  0xFF14110Eu

static int chrome_len(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void chrome_fill(GDEV *dev, int x, int y, int w, int h, COLOR c)
{
    RECT r;
    if (!dev || w <= 0 || h <= 0) return;
    r.left = (H)x;
    r.top = (H)y;
    r.right = (H)(x + w);
    r.bottom = (H)(y + h);
    fill_rec(dev, &r, c);
}

static void chrome_text(GDEV *dev, int x, int y, const char *s, COLOR c)
{
    if (!dev || !s || !s[0]) return;
    drw_tc_string(dev, (H)x, (H)y, s, c, 0);
}

static const char *chrome_item(int wide, int res, int i)
{
    static char b[20];
    int n, p, vals[2];
    if (i == 0) return wide ? "1. PLAY E1M1" : "1 PLAY";
    if (i == 1) return wide ? "2. DRONE" : "2 DRONE";
    if (i < 2 || i >= MENU_N) return "";
    n = 0;
    b[n++] = (char)('1' + i);
    if (wide) b[n++] = '.';
    b[n++] = ' ';
    vals[0] = s_res_w[i - 2];
    vals[1] = s_res_h[i - 2];
    for (p = 0; p < 2; p++) {
        char tmp[4];
        int t = 0;
        int v = vals[p];
        if (p && n < (int)sizeof b - 1) b[n++] = 'x';
        if (v <= 0) tmp[t++] = '0';
        while (v > 0 && t < 4) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (t > 0 && n < (int)sizeof b - 1) b[n++] = tmp[--t];
    }
    if (res == i - 2 && n < (int)sizeof b - 1) b[n++] = '*';
    b[n] = 0;
    return b;
}

static void paint_chrome(GDEV *dev, int ox, int oy, int dw, int dh,
                         int menu_on, int row, int res, int full, int con_on,
                         const char *msg)
{
    char hud[20];
    char sel[28];
    int health, armor, ammo, weapon, n, wide, i;
    int bar_top, area, card_w, card_h, card_x, card_y, y, hint;
    const char *wn = "  ";
    const char *foot;
    const char *hint_s;
    if (!dev || dw < 16 || dh < 16) return;
    health = s_health;
    armor = s_armor;
    weapon = s_weapon;
    ammo = 0;
    if (health < 0) health = 0;
    if (health > 999) health = 999;
    if (armor < 0) armor = 0;
    if (armor > 999) armor = 999;
    if (weapon == START_W_SHOT || weapon == START_W_SSG) ammo = s_shells;
    else if (weapon == START_W_NAIL || weapon == START_W_SNAIL) ammo = s_nails;
    else if (weapon == START_W_GL || weapon == START_W_RL) ammo = s_rockets;
    else if (weapon == START_W_LG) ammo = s_cells;
    if (ammo < 0) ammo = 0;
    if (ammo > 99) ammo = 99;
    if (weapon >= 0 && weapon < 8) wn = s_wname[weapon];
    hud[0] = 'H';
    hud[1] = (char)('0' + (health / 100) % 10);
    hud[2] = (char)('0' + (health / 10) % 10);
    hud[3] = (char)('0' + health % 10);
    hud[4] = ' ';
    hud[5] = 'A';
    hud[6] = (char)('0' + (armor / 100) % 10);
    hud[7] = (char)('0' + (armor / 10) % 10);
    hud[8] = (char)('0' + armor % 10);
    hud[9] = ' ';
    hud[10] = wn[0];
    hud[11] = wn[1];
    hud[12] = ' ';
    hud[13] = (char)('0' + (ammo / 10) % 10);
    hud[14] = (char)('0' + ammo % 10);
    hud[15] = 0;
    wide = dw >= 240;
    bar_top = dh - 17;
    if (bar_top < 0) bar_top = 0;
    if (!con_on && dh >= 17) {
        int tx;
        chrome_fill(dev, ox, oy + bar_top, dw, 1, CHROME_GOLD);
        chrome_fill(dev, ox, oy + bar_top + 1, dw, 16, CHROME_BAR);
        n = chrome_len(hud);
        tx = ox + (dw - n * 8) / 2;
        if (tx < ox + 2) tx = ox + 2;
        chrome_text(dev, tx, oy + bar_top + 1, hud, COLOR_WHITE);
    }
    if (con_on || !menu_on) {
        if (!con_on && msg && msg[0] && bar_top >= 16)
            chrome_text(dev, ox + 4, oy + bar_top - 16, msg, COLOR_YELLOW);
        return;
    }
    hint = dh >= 220;
    card_h = 4 + 16 + 2 + 2 + 4 + MENU_N * 16 + 4 + 16 + 4;
    if (hint) card_h += 16;
    area = bar_top > 2 ? bar_top - 2 : 0;
    if (card_h > area) {
        hint = 0;
        card_h = 4 + 16 + 2 + 2 + 4 + MENU_N * 16 + 4;
    }
    if (card_h > area) return;
    card_w = wide ? 208 : dw - 8;
    if (card_w > dw - 4) card_w = dw - 4;
    if (card_w < 64) card_w = dw;
    card_x = ox + (dw - card_w) / 2;
    card_y = oy + (area - card_h) / 2;
    if (card_y < oy) card_y = oy;
    chrome_fill(dev, card_x, card_y, card_w, card_h, CHROME_BG);
    chrome_fill(dev, card_x, card_y, card_w, 1, CHROME_GOLD);
    y = card_y + 4;
    foot = wide ? "Q U A K E" : "QUAKE";
    chrome_text(dev, card_x + (card_w - chrome_len(foot) * 8) / 2, y,
                foot, CHROME_GOLD);
    y += 16 + 2;
    n = card_w - 28;
    if (n < 40) n = card_w - 8;
    chrome_fill(dev, card_x + (card_w - n) / 2, y, n, 2, CHROME_RULE);
    y += 2 + 4;
    if (row < 0) row = 0;
    if (row >= MENU_N) row = MENU_N - 1;
    for (i = 0; i < MENU_N; i++) {
        const char *lab = chrome_item(wide, res, i);
        int hy = y + i * 16;
        if (i == row) {
            int k = 0;
            chrome_fill(dev, card_x + 4, hy - 1, card_w - 8, 16, CHROME_HI);
            sel[k++] = '>';
            sel[k++] = ' ';
            while (lab[k - 2] && k < (int)sizeof(sel) - 1) {
                sel[k] = lab[k - 2];
                k++;
            }
            sel[k] = 0;
            chrome_text(dev, card_x + 8, hy, sel, COLOR_YELLOW);
        } else {
            chrome_text(dev, card_x + 16, hy, lab, COLOR_WHITE);
        }
    }
    y += MENU_N * 16 + 4;
    foot = wide ? "UP/DN SELECT   FIRE" : "W/S FIRE";
    chrome_text(dev, card_x + (card_w - chrome_len(foot) * 8) / 2, y,
                foot, COLOR_LTGRAY);
    if (!hint) return;
    y += 16;
    if (wide)
        hint_s = full ? "TAB CONSOLE   F WINDOW" : "TAB CONSOLE   F FULL";
    else
        hint_s = full ? "TAB  F WINDOW" : "TAB  F FULL";
    chrome_text(dev, card_x + (card_w - chrome_len(hint_s) * 8) / 2, y,
                hint_s, COLOR_LTGRAY);
}

#if defined(SEGUI_HOST_QUAKE)
void start_sim_paint_menu(GDEV *dev, int ox, int oy, int dw, int dh)
{
    /* The capture is the 320 picture, so that row carries the star. */
    paint_chrome(dev, ox, oy, dw, dh, 1, 1, RES_FULL, 0, 0, 0);
}
#endif
#endif

#if defined(PICO_REAL_HW)
/* 8x16 system font in destination pixels, not in the render target. */
#define SYS_LH 16
static void draw_sys(int x, int y, const char *s, COLOR c)
{
    if (!s_dev || !s) return;
    if (y >= s_dh || y + SYS_LH <= 0) return;
    drw_tc_string(s_dev, (H)(s_ox + x), (H)(s_oy + y), s, c, 0);
}

static void draw_overlay(void)
{
    char line[CON_COLS + 2];
    int i, y, n;
    paint_chrome(s_dev, s_ox, s_oy, s_dw, s_dh,
                 s_mode == MODE_MENU, s_menu_row, s_res, s_full, s_con, s_msg);
    if (!s_con) return;
    y = s_dh - SYS_LH - CON_LOG * SYS_LH;
    if (y < 0) y = 0;
    n = s_con_n;
    if (n > CON_LOG) n = CON_LOG;
    for (i = 0; i < n; i++) {
        draw_sys(2, y, s_con_log[i], COLOR_WHITE);
        y += SYS_LH;
    }
    line[0] = '>';
    for (i = 0; i < s_con_len && i < CON_COLS - 1; i++)
        line[i + 1] = s_con_line[i];
    line[i + 1] = 0;
    draw_sys(2, s_dh - SYS_LH, line, COLOR_YELLOW);
}

void start_sim_set_dest(int dw, int dh)
{
    set_view_size(dw, dh);
}

/* Source row for destination row dy (0..dh-1). One divide per row. */
static int src_of(int dy, int rh, int dh)
{
    uint32_t v;
    if (dh <= 1 || dy <= 0) return 0;
    if (dy >= dh) dy = dh - 1;
    v = ((uint32_t)dy * (uint32_t)rh) / (uint32_t)dh;
    if (v >= (uint32_t)rh) v = (uint32_t)rh - 1u;
    return (int)v;
}

static void raster_rows(int sy0, int ns, int rw, int rh)
{
    if (ns < 1 || rw < 1 || rw > 320 || rh < 1) return;
    if (ns > 16) ns = 16;
    if (sy0 < 0) sy0 = 0;
    if (sy0 >= rh) return;
    if (sy0 + ns > rh) ns = rh - sy0;
    picogl_paint_band(s_cap_tri, s_cap_n, s_scale_rgb, rw,
                      sy0, ns, 0, 0, rw, rh,
                      s_band_iz, (uint16_t)CLEAR_RGB);
}

/* Exact 2x or 4x. Each source pixel is copied k times, no divider. */
static void scale_int(uint16_t *be, int stride, int band_y,
                      int ox, int oy, int dw, int k, int rw,
                      int y0, int y1, int sy0)
{
    int y;
    for (y = y0; y < y1; y++) {
        int sy = (y - oy) / k - sy0;
        const uint16_t *src;
        uint16_t *dst;
        int x, n;
        if (sy < 0) sy = 0;
        if (sy > 15) sy = 15;
        src = s_scale_rgb + sy * rw;
        dst = be + (y - band_y) * stride + ox;
        n = dw / k;
        if (k == 2) {
            for (x = 0; x < n; x++) {
                uint16_t p = src[x];
                dst[0] = p;
                dst[1] = p;
                dst += 2;
            }
        } else {
            for (x = 0; x < n; x++) {
                uint16_t p = src[x];
                dst[0] = p;
                dst[1] = p;
                dst[2] = p;
                dst[3] = p;
                dst += 4;
            }
        }
    }
}

static void scale_span(uint16_t *be, int stride, int band_y,
                       int ox, int oy, int dw, int dh, int rw, int rh,
                       int y0, int y1, int sy0)
{
    uint32_t step_x = ((uint32_t)rw << 16) / (uint32_t)dw;
    int y;
    for (y = y0; y < y1; y++) {
        int sy = src_of(y - oy, rh, dh) - sy0;
        const uint16_t *src;
        uint16_t *dst;
        uint32_t sx = 0;
        int x;
        if (sy < 0) sy = 0;
        if (sy > 15) sy = 15;
        src = s_scale_rgb + sy * rw;
        dst = be + (y - band_y) * stride + ox;
        for (x = 0; x < dw; x++) {
            int sxi = (int)(sx >> 16);
            if (sxi >= rw) sxi = rw - 1;
            if (sxi < 0) sxi = 0;
            dst[x] = src[sxi];
            sx += step_x;
        }
    }
}

static void paint_scaled(uint16_t *be, int stride, int band_y, int band_h,
                         int ox, int oy, int dw, int dh, int rw, int rh)
{
    int y0, y1, k, y;
    if (ox < 0) {
        dw += ox;
        ox = 0;
    }
    if (ox >= stride || dw <= 0 || dh <= 0) return;
    if (ox + dw > stride) dw = stride - ox;
    y0 = band_y > oy ? band_y : oy;
    y1 = band_y + band_h;
    if (y1 > oy + dh) y1 = oy + dh;
    if (y0 >= y1) return;
    k = 0;
    if (dw == rw * 2 && dh == rh * 2) k = 2;
    else if (dw == rw * 4 && dh == rh * 4) k = 4;
    y = y0;
    while (y < y1) {
        int yb = y;
        int s0, s1, ns;
        if (k)
            s0 = (y - oy) / k;
        else
            s0 = src_of(y - oy, rh, dh);
        while (yb + 1 < y1) {
            int s = k ? ((yb + 1 - oy) / k) : src_of(yb + 1 - oy, rh, dh);
            if (s < s0 || s >= s0 + 16) break;
            yb++;
        }
        s1 = k ? ((yb - oy) / k) : src_of(yb - oy, rh, dh);
        if (s1 < s0) s1 = s0;
        ns = s1 - s0 + 1;
        raster_rows(s0, ns, rw, rh);
        if (k)
            scale_int(be, stride, band_y, ox, oy, dw, k, rw, y, yb + 1, s0);
        else
            scale_span(be, stride, band_y, ox, oy, dw, dh, rw, rh,
                       y, yb + 1, s0);
        y = yb + 1;
    }
}

void start_sim_paint_band(uint16_t *be, int stride, int band_y, int band_h,
                          int ox, int oy, GDEV *dev)
{
    if (!be || stride <= 0 || stride > 320 || band_h <= 0 || band_h > 16)
        return;
    if (s_vw == s_dw && s_vh == s_dh) {
        /* The render size is the destination. One write per pixel. */
        picogl_paint_band(s_cap_tri, s_cap_n, be, stride, band_y, band_h,
                          ox, oy, s_vw, s_vh, s_band_iz, (uint16_t)CLEAR_RGB);
    } else if (s_vw > 0 && s_vw <= 320 && s_vh > 0) {
        paint_scaled(be, stride, band_y, band_h, ox, oy,
                     s_dw, s_dh, s_vw, s_vh);
    }
    s_ox = ox;
    s_oy = oy;
    s_dev = dev;
    draw_overlay();
    s_dev = 0;
}

int start_sim_fullscreen(void) { return s_full; }
int start_sim_console_open(void) { return s_con; }
int start_sim_moving(void)
{
    return s_mode == MODE_PLAY || s_mode == MODE_DRONE;
}
#endif

const uint16_t *start_sim_pixels(void)
{
#if defined(PICO_REAL_HW)
    return 0;
#else
    return s_rgb;
#endif
}
int start_sim_view_w(void) { return view_w(); }
int start_sim_view_h(void) { return view_h(); }
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
