/* ambient.h - instance class 90, the "environment instance" (vtable 0x4a90ac, think 0x472560),
 * docs/AMBIENT.md. Mode 0 = butterflies wandering in the instance's volume node (0x47e050 /
 * 0x47d440), mode 1 = a steady stream of glowing motes rising from the bottom of the volume
 * (0x47e160 / 0x47dca0), mode 2 = rain falling through it (0x47e230 / 0x47de10, ripple 0x47e310 /
 * 0x47df80). Every particle is a record of the ONE effect pool of the game
 * ([0x5e823c]+0xdb8, 2000 x 0x50 B, docs/PARTICLES.md 0), which main_engine.c owns (FxRec kind
 * FX_AMB). */
#ifndef WOODY_AMBIENT_H
#define WOODY_AMBIENT_H
#include <stdint.h>
#include "level.h"

/* the class-90 part of a pool record (the fields the creators 0x47e050 / 0x47e160 / 0x47e230 /
 * 0x47e310 write) */
typedef struct AmbFx {
    int kind, owner; /* which callback (butterfly / mote / drop / ripple), index of the owning
                        environment instance (+8) */
    float age, life; /* +0, +4 (a butterfly's +0 is its wing-flap phase, life 1e6) */
    Vec3 pos, dir;   /* +0x0c, +0x18 */
    float speed, fallen,
        len; /* mote +0x24 speed; drop +0x2c speed, +0x24 height fallen, +0x28 height of the box */
    float wander;   /* butterfly +0x24: time since the last change of course */
    int img, state; /* butterfly +0x28 (bank 0 image 53 + img), +0x2c (0 flying, 1 coming down, 2
                       landed) */
} AmbFx;

void ambient_reset(void); /* level load / free: every environment instance forgotten */
/* the subsystem handler 0x46cca0: 1501 mode (0x46cd07), 1502 colour/life/count (0x46cd2e), 1503
 * rain force (0x46cda0), 1504 count (0x46cdcc), 1511 off flag (0x46cf44); args[0] is the instance
 * ref */
void ambient_msg(Instance *in, int id, const uint32_t *args, int nargs);
void ambient_update(
    float dt); /* the think of every class-90 instance of this frame's list (creates records) */
int ambient_fx_run(AmbFx *f, float dt,
                   const float *eye); /* a record's callback (moves and draws it); 0 = it freed
                                         itself. Called by the pool driver in main_engine.c between
                                         hud_world_sprites_begin / end */
AmbFx *game_fx_amb_new(
    void); /* main_engine.c: a new record of the shared pool, NULL when the pool is full */

#endif
