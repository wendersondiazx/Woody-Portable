/* instance.h - generic (base class) instance behaviour driven by the level script; see
 * docs/INSTANCE.md. */
#ifndef WOODY_INSTANCE_H
#define WOODY_INSTANCE_H
#include "level.h"

void inst_init(Instance *inst); /* ctor defaults: animation stopped on anim 0 */
int inst_msg(Instance *inst, uint32_t id, const uint32_t *args, uint32_t nargs,
             float now); /* 1 = retry next frame (12/13) */
void inst_tick(Instance *inst, float now, float dt);
void inst_play_once(Instance *inst, int anim, float speed, float now);
/* cinematics / scripted actions: slot0 = anim, clamps on the last frame */ /* fade, path
                                                                               follower,
                                                                               animation clock
                                                                               -> inst->anim /
                                                                               anim_time */

int inst_ray_press(
    const GelFile *g, const InsFile *ins, const Instance *skip, Vec3 a, Vec3 b, float *frac,
    Vec3 *n_out,
    const Instance **inst_out); /* ray 0x4359b0, instance part (hit kind 2): press-node polygons of
                                   the instances in the cells along a->b, fraction of a->b */
int inst_point_in_press(const GelFile *g, const InsFile *ins, const Instance *skip, Vec3 p,
                        const Instance **inst_out); /* ray hit kind 3: p inside a press node of an
                                                       instance of p's cell */

#endif
