#ifndef START_SIM_H
#define START_SIM_H

#include <stdint.h>

#define START_KEY_FIRE 0x7E
#define START_W_AXE  0
#define START_W_SHOT 1
#define START_W_SSG  2
#define START_W_NAIL 3
#define START_W_SNAIL 4
#define START_W_GL   5
#define START_W_RL   6
#define START_W_LG   7

void start_sim_init(void);
void start_sim_key(int key, int down);
void start_sim_tick(uint32_t now_ms);
void start_sim_render(void);

const uint16_t *start_sim_pixels(void);
int  start_sim_view_w(void);
int  start_sim_view_h(void);

/* Device picture scales to the destination. 320 renders at the window,
 * or at the panel when fullscreen. 160 and 80 render that square and
 * scale up to the same rectangle. The menu is destination pixels.
 * Host builds keep the 128x96 target. The SegUI capture renders 320. */
#if defined(PICO_REAL_HW)
#include <btron/dp.h>
void start_sim_set_dest(int dw, int dh);
/* Swap in the list core 1 finished, then kick the next build. The
 * picture is one pose behind after the first frame. */
void start_sim_acquire(void);
/* Block until that build finishes, so the next tick can write the pose.
 * Call this inside the present, after the last band. */
void start_sim_join_build(void);
void start_sim_paint_band(uint16_t *be, int stride, int band_y, int band_h,
                          int ox, int oy, GDEV *dev);
int  start_sim_fullscreen(void);
int  start_sim_console_open(void);
/* Play and the drone repaint. The menu stays on the last frame. */
int  start_sim_moving(void);
#endif
#if !defined(PICO_REAL_HW)
#include <btron/dp.h>
/* SegUI capture. Boot menu and status bar in the system font. */
void start_sim_paint_menu(GDEV *dev, int ox, int oy, int dw, int dh);
#endif
int  start_sim_lit(void);

const float *start_sim_origin(void);
int  start_sim_health(void);
int  start_sim_armor(void);
int  start_sim_weapon(void);
int  start_sim_ammo(void);
int  start_sim_onground(void);
int  start_sim_has_weapon(int weapon);
int  start_sim_solid(float x, float y, float z);

int   start_sim_n_zombie(void);
int   start_sim_zombie_health(int i);
const float *start_sim_zombie_origin(int i);

int   start_sim_n_door(void);
float start_sim_door_pos(int i);
void  start_sim_door_bounds(int i, float mins[3], float maxs[3]);

int  start_sim_n_pickup(void);
void start_sim_pickup(int i, float org[3], int *kind);

int  start_sim_n_trig(void);
void start_sim_trig(int i, float mins[3], float maxs[3], int *kind);

int  start_sim_projectiles(void);
int  start_sim_slipped(void);
const char *start_sim_message(void);

void start_sim_set_origin(float x, float y, float z);
void start_sim_set_yaw(float yaw);
float start_sim_yaw(void);

#endif
