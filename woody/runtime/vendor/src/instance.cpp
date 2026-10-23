/* instance.c - generic instance behaviour of the base class (docs/INSTANCE.md): animation clock
 * 0x43eee0, PlayAnim messages 1..5/12/13, path follower 42/43/44/46, transparency fade 56/57,
 * show/hide 6. texture frame override 16/18/19, UV scroll override 15/17, the instance half of the
 * ray 0x4359b0 (inst_ray_press). Not ported: SetFlags 45 (stored only). */
#include <math.h>
#include "instance.h"

#define T_ACTIVE 0x20000u
#define T_REVERSE 0x40000u
#define T_LOOP 0x80000u
#define T_PINGPONG 0x100000u
#define T_ORIENT 0x200000u     /* message 46 a == 1 (0x4381e0) */
#define T_ORIENT_REV 0x400000u /* message 46 b == 1 */

static float anim_L(const Instance *I, int a) {
    const Model *m = I->model;
    return (a >= 0 && (uint32_t)a < m->nanims && m->anims[a].duration_s > 0)
               ? m->anims[a].duration_s
               : 1.0f;
}
static void set_speed(Instance *I, float v) {
    I->a_speed = I->a_base_speed = v;
} /* 0x42e290 */

void inst_init(Instance *I) /* ctor 0x42e250 / 0x42e210 / 0x44e7c0 */
{
    I->slot[0] = 0;
    I->slot[1] = I->slot[2] = I->slot[3] = -1;
    I->a_speed = I->a_base_speed = 0;
    I->a_start = 0;
    I->a_pos = 0;
    I->a_ended = 0;
    I->fade = 0;
    I->fade_target = 0;
    I->fade_rate = 100.0f;
    I->noncollide = 0;
    I->setflags = 0;
    I->traj_flags = 0;
    I->traj_start = 0;
    I->traj_dur = 1.0f;
    I->scripted = 1;
    I->tex_mode = 0;
    I->tex_t0 = 0;
    I->tex_fac = 1.0f;
    I->uv_mode = 0;
    I->uv_t0 = 0;
    I->uv_fac = 1.0f;
    I->uv_t2 = 0;
}

/* returns 1 when the message must be offered again next frame (12/13 waiting for the running
 * animation to end) */
int inst_msg(Instance *I, uint32_t id, const uint32_t *arg, uint32_t nargs, float now) {
    int a1 = nargs > 1 ? (int)arg[1] : 0, a2 = nargs > 2 ? (int)arg[2] : 0,
        a3 = nargs > 3 ? (int)arg[3] : 0, a4 = nargs > 4 ? (int)arg[4] : 0;
    const Model *m = I->model;
    switch (id) {
    case 1: /* still frame at position t (scale 0.001 of the PREVIOUS slot0 length) */
        if ((uint32_t)a1 >= m->nanims)
            return 0;
        set_speed(I, 0);
        I->a_pos = anim_L(I, I->slot[0]) * a2 * 0.001f;
        I->slot[0] = a1;
        I->slot[1] = I->slot[2] = I->slot[3] = -1;
        I->a_ended = 0;
        return 0;
    case 2: /* one shot with start offset: anim, flag, dur, off */
        if ((uint32_t)a1 >= m->nanims || a3 <= 0)
            return 0;
        I->slot[0] = a1;
        set_speed(I, anim_L(I, a1) / (a3 * 0.01f));
        I->a_start = now - a4 * 0.001f * a3 * 0.01f;
        if (a2 == 0)
            set_speed(I, -I->a_speed);
        I->slot[1] = I->slot[2] = I->slot[3] = -1;
        I->a_ended = 0;
        return 0;
    case 12:
        if (!I->a_ended && I->a_speed != 0)
            return 1; /* fallthrough */
    case 3: {         /* one shot "door": anim, flag, dur */
        if ((uint32_t)a1 >= m->nanims)
            return 0;
        if (a3 <= 10) {
            set_speed(I, 0);
            I->a_pos = a2 ? anim_L(I, I->slot[0]) : 0;
            return 0;
        } /* jump to the end / start */
        float L = anim_L(I, a1), v = L / (a3 * 0.01f);
        if (I->slot[0] == a1 &&
            I->a_speed != 0) { /* already running: reverse from the current pose */
            if (I->a_speed > 0) {
                if (a2 != 0)
                    return 0;
                float p = (now - I->a_start) / (L / I->a_speed);
                p = 1.0f - (p - floorf(p));
                I->a_start = now - (L / v) * p;
                set_speed(I, -v);
            } else {
                if (a2 != 1)
                    return 0;
                float p = (now - I->a_start) / -(L / I->a_speed);
                p = p - floorf(p);
                set_speed(I, v);
                I->a_start = now - (L / v) * (1.0f - p);
            }
            return 0;
        }
        I->slot[0] = a1;
        set_speed(I, a2 == 0 ? -v : v);
        I->a_start = now;
        I->slot[1] = I->slot[2] = I->slot[3] = -1;
        I->a_ended = 0;
        return 0;
    }
    case 13:
        if (!I->a_ended && I->a_speed != 0)
            return 1; /* fallthrough */
    case 4:           /* loop: anim, flag, dur */
        if ((uint32_t)a1 >= m->nanims || a3 <= 0)
            return 0;
        I->slot[0] = I->slot[1] = I->slot[2] = I->slot[3] = a1;
        set_speed(I, anim_L(I, a1) / (a3 * 0.01f));
        if (a2 == 0)
            set_speed(I, -I->a_speed);
        I->a_start = now;
        I->a_ended = 0;
        return 0;
    case 5:
        set_speed(I, 0);
        return 0; /* freeze on the current position */
    case 6:
        I->visible = a1 != 0;
        return 0; /* 0x407850 / 0x407790: hidden = not drawn, no collision, no volumes */
    case 42:
    case 43: /* path follower: a (1 = forward), f = time in 1/100 s, c = ping-pong */
        if (I->traj.npoints < 2 || a2 <= 0)
            return 0;
        I->traj_flags =
            T_ACTIVE | (a1 != 1 ? T_REVERSE : 0) |
            (id == 43 ? T_LOOP | (a3 == 1 ? T_PINGPONG : 0) : (I->traj_flags & T_PINGPONG)) |
            (I->traj_flags & (T_ORIENT | T_ORIENT_REV)); /* 0x437d10 / 0x437d50 keep bits 21-22 */
        I->traj_dur = a2 * 0.01f;
        I->traj_start = now;
        I->position = I->traj.points[a1 == 1 ? 0 : I->traj.npoints - 1];
        I->cell_ok = 0;
        return 0; /* 0x42dd5d / 0x42dd8b: 0x4077f0(0) re-cells it at that point (renderer
                   * chains_sync); otherwise it kept its old cell, e.g. the far path end of the S1R
                   * launchers' 42 [0, 1], and was never listed */
    case 44:
        I->traj_flags &= ~T_ACTIVE;
        return 0;
    case 46:
        if (I->traj.npoints)
            I->traj_flags = (I->traj_flags & ~(T_ORIENT | T_ORIENT_REV)) |
                            (a1 == 1 ? T_ORIENT : 0) | (a2 == 1 ? T_ORIENT_REV : 0);
        return 0; /* 0x42dc7d -> 0x4381e0 (S1R / S3R launchers) */
    case 16:
    case 18: /* texture frame override B (docs/INSTANCE.md 2): 16 = one shot, 18 = loop; a2 = 1
              * forward, 0 backward, 2 there and back; a3 x 0.01 = factor on the texture duration,
              * a1 (0xffff) is stored but never read. 0x42da32 / 0x42db0f: any other a2 leaves the
              * mode bits alone, the clock and factor are still set */
        if (a2 >= 0 && a2 <= 2)
            I->tex_mode = (id == 16 ? 1 : 4) + (a2 == 1 ? 0 : a2 == 0 ? 1 : 2);
        I->tex_t0 = now;
        I->tex_fac = a3 * 0.01f;
        return 0;
    case 15:
    case 17: /* UV scroll override A (0x42d9c3 / 0x42daae, docs/INSTANCE.md 2): 15 = scroll for a4 x
              * 0.01 s and stop there, 17 = endless; a2 = 1 forward, 0 backward, anything else
              * leaves the mode bits alone; a3 x 0.01 = factor on the texture's scroll speed, a1
              * (byte +0xda) is stored but never read. No level script sends either message. */
        if (a2 == 1)
            I->uv_mode = id == 15 ? 1 : 4;
        else if (a2 == 0)
            I->uv_mode = id == 15 ? 2 : 5;
        I->uv_t0 = now;
        I->uv_fac = a3 * 0.01f;
        if (id == 15)
            I->uv_t2 = a4 * 0.01f;
        return 0;
    case 19:
        I->tex_mode = 0;
        I->uv_mode = 0;
        return 0; /* 0x42db86: +0xd8 &= 0xc0, both overrides off */
    case 45:
        I->setflags |= (uint32_t)a1 & 0x23;
        return 0;
    case 56: /* transparency: direct on the base class, a target on classes behind 0x44e8f0 */
        if (I->type == 0 || I->type == 41 || I->type == 90)
            I->fade = I->fade_target = a1 * 0.01f;
        else
            I->fade_target = a1 * 0.01f;
        return 0;
    case 57:
        I->fade_rate = a1 * 0.01f;
        return 0;
    default:
        return 0;
    }
}

static void traj_update(Instance *I, float now) /* 0x437da0 */
{
    if (!(I->traj_flags & T_ACTIVE))
        return;
    const Trajectory *T = &I->traj;
    uint32_t n = T->npoints;
    float t = now - I->traj_start, frac, ip = 0;
    if (!(I->traj_flags & T_LOOP) && t > I->traj_dur) {
        frac = 1.0f;
        I->traj_flags &= ~T_ACTIVE;
    } else
        frac = modff(t / I->traj_dur, &ip);
    int fwd = !(I->traj_flags & T_REVERSE);
    if ((I->traj_flags & T_PINGPONG) && ((int)ip & 1))
        fwd = !fwd;
    float total = 0;
    for (uint32_t i = 0; i + 1 < n; i++) {
        Vec3 a = T->points[i], b = T->points[i + 1];
        total += sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) +
                       (b.z - a.z) * (b.z - a.z));
    }
    float d = frac * total, acc = 0, rem = d;
    int i0 = -1, i1 = -1;
    for (uint32_t s = 0; s + 1 < n; s++) {
        uint32_t i = fwd ? s : n - 1 - s, j = fwd ? i + 1 : i - 1;
        Vec3 a = T->points[i], b = T->points[j];
        float len = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) +
                          (b.z - a.z) * (b.z - a.z));
        acc += len;
        if (d <= acc) {
            float k = len > 1e-6f ? rem / len : 0;
            I->position = Vec3{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k};
            i0 = (int)i;
            i1 = (int)j;
            break;
        }
        rem -= len;
    }
    if ((I->traj_flags & T_ORIENT) && i0 >= 0) { /* 0x437eee..0x4381bf: rows (F x up, F, up), F =
                                                    the horizontal P[i0] - P[i1] (against the */
        Vec3 A = T->points[i0],
             B = T->points[i1]; /* travel direction), or P[i1] - P[i0] with bit 22; the model's
                                   local y lies along F, local z up */
        float fx = (I->traj_flags & T_ORIENT_REV) ? B.x - A.x : A.x - B.x,
              fz = (I->traj_flags & T_ORIENT_REV) ? B.z - A.z : A.z - B.z,
              l = sqrtf(fx * fx + fz * fz);
        if (l > 0) { /* [0x4a9004]: a zero-length step leaves F unnormalised (0) = a degenerate
                        matrix; the port keeps the old one */
            fx /= l;
            fz /= l;
            float m00 = -fz, m10 = 0, m20 = fx, m01 = fx, m11 = 0, m21 = fz, m02 = 0, m12 = 1,
                  m22 = 0, t = m00 + m11 + m22;
            Quat q; /* columns = R, F, U */
            if (t > 0) {
                float r = sqrtf(t + 1) * 2;
                q = Quat{(m21 - m12) / r, (m02 - m20) / r, (m10 - m01) / r, r * 0.25f};
            } else if (m00 > m11 && m00 > m22) {
                float r = sqrtf(1 + m00 - m11 - m22) * 2;
                q = Quat{r * 0.25f, (m01 + m10) / r, (m02 + m20) / r, (m21 - m12) / r};
            } else if (m11 > m22) {
                float r = sqrtf(1 + m11 - m00 - m22) * 2;
                q = Quat{(m01 + m10) / r, r * 0.25f, (m12 + m21) / r, (m02 - m20) / r};
            } else {
                float r = sqrtf(1 + m22 - m00 - m11) * 2;
                q = Quat{(m02 + m20) / r, (m12 + m21) / r, r * 0.25f, (m10 - m01) / r};
            }
            I->quat = q;
        }
    }
    mat4_from_trs(&I->world, I->position, I->quat, I->scale);
}

void inst_play_once(Instance *I, int anim, float speed, float now) {
    I->slot[0] = anim;
    I->slot[1] = I->slot[2] = I->slot[3] = -1;
    set_speed(I, speed);
    I->a_start = now;
    I->a_pos = 0;
    I->a_ended = 0;
}

void inst_tick(Instance *I, float now, float dt) {
    if (!I->scripted)
        return;
    /* fade 0x44e810 */
    float old = I->fade;
    if (I->fade < I->fade_target) {
        I->fade += dt * I->fade_rate;
        if (I->fade > I->fade_target)
            I->fade = I->fade_target;
    } else if (I->fade > I->fade_target) {
        I->fade -= dt * I->fade_rate;
        if (I->fade < I->fade_target)
            I->fade = I->fade_target;
    }
    if (I->fade > 0.9f && old <= 0.9f)
        I->noncollide = 1;
    if (I->fade < 0.9f && old >= 0.9f)
        I->noncollide = 0;
    if (!I->visible)
        return; /* hidden: no clock (time still runs, it is absolute) */
    traj_update(I, now);
    /* animation clock 0x43eee0 */
    float phase, L = anim_L(I, I->slot[0]);
    if (I->a_speed != 0) {
        I->a_ended = 0;
        if (I->a_speed > 0)
            phase = (now - I->a_start) / (L / I->a_speed);
        else
            phase = 1.0f - (now - I->a_start) / -(L / I->a_speed);
        if (!(phase >= 0 && phase < 1)) {
            I->a_ended = 1;
            if (I->slot[1] != -1) { /* next slot: seamless, time exact */
                float ratio = L / anim_L(I, I->slot[1]), nwrap = 0;
                if (phase < 0) {
                    nwrap = -floorf(phase);
                    phase += nwrap;
                }
                if (phase >= 1) {
                    nwrap = floorf(phase);
                    phase -= nwrap;
                }
                phase *= ratio;
                if (phase >= 1)
                    phase = 0.9999f;
                if (nwrap != 0)
                    I->a_start += nwrap * (L / fabsf(I->a_speed));
                I->a_speed = I->a_base_speed;
                I->slot[0] = I->slot[1];
                I->slot[1] = I->slot[2];
                I->slot[2] = I->slot[3];
                L = anim_L(I, I->slot[0]);
            } else if (I->a_speed < 0) {
                if (phase < 0) {
                    set_speed(I, 0);
                    phase = 0;
                }
            } else if (phase > 1 || phase == 1) {
                set_speed(I, 0);
                phase = 1;
            }
        }
        I->a_pos = L * phase;
    } else
        phase = I->a_pos / L;
    if (phase > 0.9999f)
        phase = 0.9999f;
    if (phase < 0)
        phase = 0; /* ins_pose() wraps at 1 */
    I->anim = I->slot[0];
    I->anim_time = phase * L;
}

/* ---- the instance half of the ray 0x4359b0 (and of the endless ray 0x435810): 0x497ed0 / 0x497a30
 * answering 4 is hit kind 2, node [0x4c4be0] -> [0x53a58c], instance [0x4c4c0c]+0x40 -> [0x53a560].
 * Like every instance test of the original it walks the PRESS nodes (kind 1) of the instances in
 * the cells the ray visits plus the dynamic list 0x4c3bb4 (docs/EVENTS.md 3.1, BOMB.md 5.2); a
 * hidden instance (message 6: no cell) and a non-collidable one (+8 & 0x40, the fade) take no part.
 * The actors do not take part either: the Perso and enemy models have no press node. The instances
 * are the ones the original tests - those registered (.col) in the cells along a->b, then the
 * dynamic list (level.c gel_col_instances) - but the test stays the port's: one-sided like the
 * original (front side of the loader's plane, see below), the nearest polygon (the original's vt[5]
 * 0x432ab0 lets the last polygon tested win, player.c ray_instances; the lasers, shots and bombs of
 * main_engine.c take the nearest, as the endless test vt[6] 0x431de0 does). Returns 1 on a hit with
 * the fraction of a->b, the normal turned towards a and the instance. */
static Vec3 v3sub(Vec3 a, Vec3 b) {
    Vec3 r = {a.x - b.x, a.y - b.y, a.z - b.z};
    return r;
}
static float v3dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static Vec3 v3cross(Vec3 a, Vec3 b) {
    Vec3 r = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return r;
}
int inst_ray_press(const GelFile *g, const InsFile *ins, const Instance *skip, Vec3 a, Vec3 b,
                   float *frac, Vec3 *n_out, const Instance **inst_out) {
    float best = 2.0f;
    int hit = 0;
    Vec3 v[16];
    float sb[6] = {fminf(a.x, b.x) - 1, fmaxf(a.x, b.x) + 1, fminf(a.y, b.y) - 1,
                   fmaxf(a.y, b.y) + 1, fminf(a.z, b.z) - 1, fmaxf(a.z, b.z) + 1};
    gel_walk_seg(g, a, b, 2.0f,
                 0); /* the cells along a->b (0x497fb0's walk; the lasers end b at their wall) */
    const GelColRef *cr;
    uint32_t ncr = gel_col_instances(g, ins, &cr);
    for (uint32_t q = 0; q < ncr; q++) {
        const Model *m = cr[q].in->model;
        uint32_t ncn;
        const uint32_t *cn = ins_collision_nodes(m, &ncn);
        {
            const Instance *in = cr[q].in;
            if (in == skip)
                continue;
            for (uint32_t ci = 0; ci < ncn; ci++) {
                uint32_t ni = cn[ci];
                const InsNode *nd = &m->nodes[ni];
                if (nd->kind != 1 || !nd->polys)
                    continue;
                float nb[6];
                if (ins_node_world_box(in, ni, nb) &&
                    (nb[0] > sb[1] || nb[1] < sb[0] || nb[2] > sb[3] || nb[3] < sb[2] ||
                     nb[4] > sb[5] || nb[5] < sb[4]))
                    continue;
                for (uint32_t pi = 0; pi < nd->npolys; pi++) {
                    const InsPoly *pl = &nd->polys[pi];
                    if (pl->nverts < 3 || pl->nverts > 16)
                        continue;
                    for (uint32_t c = 0; c < pl->nverts; c++)
                        v[c] = ins_point_world(in, pl->indices[c]);
                    Vec3 nrm = v3cross(v3sub(v[1], v[0]), v3sub(v[2], v[0]));
                    float l = sqrtf(v3dot(nrm, nrm));
                    if (l < 1e-6f)
                        continue;
                    nrm.x /= l;
                    nrm.y /= l;
                    nrm.z /= l;
                    /* one-sided like 0x431de0 / 0x432ab0: the loader's plane (R-Q) x (R-P) is -nrm
                     * here, and a polygon counts only when the start lies on that front side
                     * (0x432169) and the ray crosses it inwards - a ray that starts inside a press
                     * node leaves it without a hit (the W3B laser 737's beams start inside its own
                     * housing) */
                    float da = v3dot(v3sub(a, v[0]), nrm), db = v3dot(v3sub(b, v[0]), nrm);
                    if (da > 0 || !(db > 0))
                        continue;
                    float t = da / (da - db);
                    if (t >= best)
                        continue;
                    Vec3 q = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
                    int sign = 0,
                        in_poly = 1; /* inside: every edge turns the same way round the normal */
                    for (uint32_t c = 0; c < pl->nverts && in_poly; c++) {
                        float s = v3dot(
                            v3cross(v3sub(v[(c + 1) % pl->nverts], v[c]), v3sub(q, v[c])), nrm);
                        int sg = s > 1e-3f ? 1 : (s < -1e-3f ? -1 : 0);
                        if (sg) {
                            if (!sign)
                                sign = sg;
                            else if (sg != sign)
                                in_poly = 0;
                        }
                    }
                    if (!in_poly)
                        continue;
                    nrm.x = -nrm.x;
                    nrm.y = -nrm.y;
                    nrm.z = -nrm.z; /* towards a */
                    best = t;
                    hit = 1;
                    if (n_out)
                        *n_out = nrm;
                    if (inst_out)
                        *inst_out = in;
                }
            }
        }
    }
    *frac = best;
    return hit;
}

/* the hit kind 3 of the ray 0x4359b0 (0x4330c0 in the instance test 0x432ab0): the START of the
 * segment lies inside a press node -- behind every one of its planes -- and then the answer is that
 * instance at t = 0, whatever else the ray meets. The port takes the side of each polygon's plane
 * that the node's own centre lies on as "behind", so the winding does not matter, and wants the
 * point at least 1 unit deep (the original: strictly behind every plane). */
int inst_point_in_press(const GelFile *g, const InsFile *ins, const Instance *skip, Vec3 p,
                        const Instance **inst_out) {
    Vec3 v[16];
    gel_walk_seg(g, p, p, 2.0f, 0); /* p's cell: where the ray's walk starts */
    const GelColRef *cr;
    uint32_t ncr = gel_col_instances(g, ins, &cr);
    for (uint32_t q = 0; q < ncr; q++) {
        const Model *m = cr[q].in->model;
        uint32_t ncn;
        const uint32_t *cn = ins_collision_nodes(m, &ncn);
        {
            const Instance *in = cr[q].in;
            if (in == skip)
                continue;
            for (uint32_t ci = 0; ci < ncn; ci++) {
                uint32_t ni = cn[ci];
                const InsNode *nd = &m->nodes[ni];
                if (nd->kind != 1 || !nd->polys || nd->npolys < 4)
                    continue;
                float nb[6];
                if (ins_node_world_box(in, ni, nb) && (p.x < nb[0] || p.x > nb[1] || p.y < nb[2] ||
                                                       p.y > nb[3] || p.z < nb[4] || p.z > nb[5]))
                    continue;
                Vec3 c = {0, 0, 0};
                int nc = 0;
                for (uint32_t pi = 0; pi < nd->npolys; pi++)
                    for (uint32_t q = 0; q < nd->polys[pi].nverts; q++) {
                        Vec3 w = ins_point_world(in, nd->polys[pi].indices[q]);
                        c.x += w.x;
                        c.y += w.y;
                        c.z += w.z;
                        nc++;
                    }
                if (!nc)
                    continue;
                c.x /= nc;
                c.y /= nc;
                c.z /= nc;
                int inside = 1;
                for (uint32_t pi = 0; pi < nd->npolys && inside; pi++) {
                    const InsPoly *pl = &nd->polys[pi];
                    if (pl->nverts < 3 || pl->nverts > 16)
                        continue;
                    for (uint32_t q = 0; q < 3; q++)
                        v[q] = ins_point_world(in, pl->indices[q]);
                    Vec3 nrm = v3cross(v3sub(v[1], v[0]), v3sub(v[2], v[0]));
                    float nl = sqrtf(v3dot(nrm, nrm));
                    if (nl < 1e-6f)
                        continue;
                    float dp = v3dot(v3sub(p, v[0]), nrm) / nl,
                          dc = v3dot(v3sub(c, v[0]), nrm) / nl;
                    if (dc < 0) {
                        dp = -dp;
                        dc = -dc;
                    }
                    if (dp < 1.0f)
                        inside = 0; /* port tolerance: at least 1 unit inside, so a bomb lying on a
                                       press node is not "in" it */
                }
                if (inside) {
                    if (inst_out)
                        *inst_out = in;
                    return 1;
                }
            }
        }
    }
    return 0;
}
