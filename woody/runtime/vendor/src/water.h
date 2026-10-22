/* water.h - instance class 60, the water volume (docs/WATER.md): a wobbling, see-through surface
 * over the top face of the instance's box, drifting wakes on it, and Kill(7) for the player who
 * gets into the box. */
#ifndef WOODY_WATER_H
#define WOODY_WATER_H
#include "level.h"

struct Player;

void water_reset(const TexFile *tex); /* level load: forget every volume; tex = the level's .tex */
void water_add(Instance *inst);       /* SetTypeInstance 60 (ctor at 0x403c79) */
/* message 1506 SetWaterVolumeParameter (0x46ce38 -> 0x474690): the four script arguments as they
 * come, x0.01 except the subdivision count. Builds the surface grid (0x473120). */
void water_param(Instance *inst, int32_t cell, int32_t subdiv, int32_t amp, int32_t alpha);
void water_update(float dt,
                  struct Player *pl); /* Update 0x4747f0 for every volume, and the wakes */
/* the surface (Draw 0x4738c0 with bit 4): texture list 8 = SRCALPHA / INVSRCALPHA, no z write,
 * MODULATE2X, drawn after the models. Called from the renderer; `eye` = camera position. */
void water_draw(const TexFile *tex, Vec3 eye);
void water_fx_draw(void); /* the wakes: flat additive sprites (0x473050) */

#endif
