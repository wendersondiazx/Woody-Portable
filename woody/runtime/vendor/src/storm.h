/* storm.h - instance class 80, the lightning rod, and the thunderstorm it shelters Woody from
 * (docs/STORM.md). The storm itself is a set of globals (0x5e59e4..0x5e59ec) that the script starts
 * with game message 1100 and stops with 1101; every interval a bolt strikes Woody (Kill 9) unless
 * he stands inside the sphere of a lightning rod. */
#ifndef WOODY_STORM_H
#define WOODY_STORM_H
#include "level.h"

struct Player;

void storm_reset(void); /* level load: 0x451b90 (storm off) + the class dtor (list emptied) */
void storm_add(Instance *inst); /* SetTypeInstance 80: ctor 0x451a90 + Init 0x451b10 */
void storm_zone_param(Instance *inst, int mode,
                      int v);     /* message 54 (handler 0x451b50): mode 1 = radius, 2 = height */
void storm_start(float interval); /* message 1100 [interval x100] -> 0x451ba0 */
void storm_stop(void);            /* message 1101 -> 0x451bd0, also Game states 0 and 3 (death) */
/* 0x451cc0 at the start of the Game tick 0x4459c0 (not while paused); `frozen` = the Perso does not
 * register as an actor this frame (cinematic, scripted action) */
void storm_update(float dt, struct Player *pl, int frozen);
void storm_fx_draw(
    const float *eye,
    float dt); /* the bolts and the rod arcs (particle callbacks 0x46d520 / 0x46da00) */
void storm_rod_drawn(Instance *inst); /* Renderer.on_drawn: vt[26] 0x452010, the rod's colour, only
                                         for a drawn rod */
void storm_overlay_draw(
    int paused, float dt); /* 0x46e0d0: the full-screen darkening and flashes, 2D, before the HUD */

#endif
