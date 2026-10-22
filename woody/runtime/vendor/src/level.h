/* level.h - in-memory representation and loaders for the level files of
 * Woody Woodpecker: Escape from Buzz Buzzard Park (PC, 2001).
 * Formats: docs/FORMAT_GEL.md, docs/FORMAT_INS.md, docs/FORMAT_TEX_COL_VIS_LIT.md.
 * Coordinates are kept exactly as in the files (D3D left-handed, y up). */
#ifndef WOODY_LEVEL_H
#define WOODY_LEVEL_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    float x, y, z;
} Vec3;
typedef struct {
    float x, y, z, w;
} Quat;
typedef struct {
    float m[16];
} Mat4; /* column-major, OpenGL style */

/* ---- .tex --------------------------------------------------------------- */
typedef struct {
    uint32_t width, height, flags; /* flags bit0 = magenta colour key */
    float scroll_u, scroll_v, anim_duration;
    uint32_t frame_count;
    uint16_t **frames; /* frame_count pointers into file data (RGB565, row-major) */
    const uint8_t **ps2_palettes, **ps2_frames; /* original indexed PS2 frames and their palettes */
    unsigned ps2_levels;
    uint32_t gl_tex;     /* renderer handle of the current frame */
    uint32_t *gl_frames; /* renderer handles per frame (frame_count) */
} TexGroup;

typedef struct {
    uint32_t group;
    float m[12]; /* u = m0 x + m3 y + m6 z + m9 ; v = m1 x + m4 y + m7 z + m10 */
} Material;

typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t ngroups;
    TexGroup *groups;
    uint32_t nmaterials;
    Material *materials;
} TexFile;

/* ---- .gel (only what the renderer needs: polygons + vertices) -------------- */
typedef struct {
    uint32_t nverts;
    uint32_t material; /* bit 15 = no material */
    float plane[4];
    uint32_t *indices;
} GelPoly;

typedef struct {
    float x, y, z;
    uint32_t colour;
} GelVert;

typedef struct {
    int32_t axis, sector;
    float d;
    int32_t le, gt;
} KdNode; /* 0x40ab10: p[axis] + d <= 0 -> le, otherwise gt; child < 0 = leaf ~child; sector >= 0
             marks a sector root */

/* Sections 5 and 7 share one record (0x4c bytes, vtable 0x4a94f4): the leaves of the kd-tree and,
 * one level up, the sectors. Both carry the polygons that cross them, which is how the original
 * answers every geometry question without looking at the whole level: the floor under a point
 * (0x40a0c0), the collision push-out (0x407000) and which part of the world to draw (0x42a980) all
 * start from the cell or sector the point falls in. */
typedef struct {
    uint32_t npolys;
    const uint32_t *polys;
    float bbox[6]; /* bbox order xmin xmax ymin ymax zmin zmax (0x406e50) */
    int32_t link[6];
    uint32_t nnodes;
    const uint8_t *nodes;
} GelCell; /* neighbour per face -x +x -y +y -z +z (+0x10..+0x24): INT_MIN none,
            * < 0 cell ~link, >= 0 root of a local subtree in nodes (16 B each, +0x48) */
typedef struct {
    uint32_t first, end;
} GelGroup; /* section 3: the polygons [first, end) of one zone */
typedef struct {
    uint32_t n;
    const uint32_t *polys;
} GelPolySet; /* result of a query below; valid until the next one */
struct GelQuery;

typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t npolys;
    GelPoly *polys;
    uint32_t nverts;
    GelVert *verts;
    uint32_t ncells, nsectors; /* kd leaf cells and sectors; the sector a point is in decides its
                                  lights (docs/LIGHTING.md 3) */
    uint32_t nkd;
    KdNode *kd; /* the main kd-tree (0x408180) */
    float bbox[6];
    uint32_t ngroups;
    GelGroup *groups; /* section 3, addressed by the .vis pairs */
    GelCell *cells;   /* ncells kd leaves; a .col record belongs to each of them */
    GelCell *sectors; /* nsectors sectors; .vis and the .lit trailer are indexed by these */
    uint32_t nloose;
    uint32_t *loose;    /* polygons that no cell or sector lists: never cull these away */
    struct GelQuery *q; /* scratch of the queries below (level.c) */
    struct GelCol *col; /* the .col registration of instances per cell and the collision cell walks
                           (gel_col_load) */
} GelFile;

/* ---- .vis: the potentially visible set of a sector (docs/FORMAT_TEX_COL_VIS_LIT.md 3)
 * ------------- */
typedef struct {
    uint32_t id, npairs;
    const uint32_t *pairs;
} VisList; /* npairs x (sector, flag); flag = the group of section 3 */
typedef struct {
    uint32_t nlists, first;
} VisSector; /* lists [first, first+nlists) of VisFile.pool */
typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t nsectors;
    VisSector *sectors;
    VisList *pool;
} VisFile;

/* ---- .lit: precomputed light visibility (docs/LIGHTING.md) ---------------------- */
typedef struct {
    uint32_t n, face;
    float plane[4];
    int32_t *indices;
} LitPoly; /* index < 0: extra vertex -i-1; face = P+0x08, the parent's MATERIAL word, not a face
              index (render_gl.c finds the parent B face) */
typedef struct {
    Vec3 pos;
    float colour[3];
    float range;     /* colour 0..255 */
    uint32_t na, *a; /* faces the light sees completely */
    uint32_t nb, *b; /* partially lit faces (parents of the polygons) */
    uint32_t nc;
    LitPoly *c; /* the lit fragments of the b faces */
    uint32_t nbsp;
    uint32_t *bsp; /* {plane, front, back} */
    uint32_t nplanes;
    float *planes; /* 4 floats each */
} LitLight;
typedef struct {
    uint32_t n;
    const uint32_t *idx;
} LitSector; /* the lights of one gel sector (trailer -> lightsys+0x10) */
typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t nlights;
    LitLight *lights;
    uint32_t nextra;
    Vec3 *extra;
    uint32_t nsectors;
    LitSector *sectors; /* trailer: one light list per gel sector; 0 when the .lit has no trailer */
} LitFile;

/* ---- .ins ------------------------------------------------------------------ */
typedef struct {
    uint32_t off, cnt;
} TrackRef;

typedef struct {
    uint32_t material; /* bit 15 set: ARGB1555 flat colour; else index in TexFile.materials */
    uint32_t flags,
        nverts;        /* flags bit 1 (0x2) = double sided, never back-face culled (0x43bf65) */
    uint32_t *indices; /* absolute indices into Model.points */
    float plane[4];
    int plane_ok; /* polygon plane in pivot-relative node space, built on first use (0x4280c2) */
} InsPoly;

typedef struct {
    uint32_t flags, kind, type_code, sub_index;
    uint32_t npolys, npoints, point_base;
    Vec3 pivot;
    uint32_t a, b, c;                           /* track pool sizes: dwords, dwords, bytes */
    uint8_t *pool;                              /* a+b+c/4 dwords */
    TrackRef *pos_refs, *rot_refs, *event_refs; /* per animation (or NULL) */
    int32_t first_child, next_sibling, parent;  /* 0-based node indices, -1 = none/root */
    InsPoly *polys;
    float box[6];
    int box_state; /* pivot-relative aabb of the node's own points, built on first use (0 = not yet,
                      -1 = the points are not all this node's) */
    float radius; /* N+0x2c: max |p - pivot| over the node's own points (loader 0x427daf..0x427e3d),
                     the node cull of the instance tests */
    /* light / helper / marker payload */
    float light_intensity;
    uint32_t light_colour;
    float helper_a, helper_b;
    uint32_t helper_mode;
    float marker_value;
} InsNode;

typedef struct {
    Vec3 pos, normal, colour;
} InsPoint;
typedef struct {
    uint32_t i0, i1, i2, material;
} InsTri;
typedef struct {
    uint32_t nframes, duration_4096;
    float duration_s;
    int pose_kind; /* 0 unknown, 1 fixed tracks, 2 animated tracks */
} InsAnim;
typedef struct {
    uint32_t npoints;
    Vec3 *points;
    uint32_t closed;
} Trajectory;

struct Model;
typedef struct Instance {
    struct Model *model;
    uint32_t unk0;
    Trajectory traj;
    Vec3 position;
    Quat quat;
    Vec3 scale;
    uint32_t id, index;
    uint32_t nids;
    uint32_t *ids; /* 0x03.. world_volume ids then 0x07.. collision ids */
    /* runtime */
    int visible;
    int anim;
    float anim_time;
    float anim_speed;
    int type; /* type from SetTypeInstance */
    int snd_anim;
    float snd_tf; /* sound events: animation and frame time at the previous tick */
    unsigned
        snd_frame; /* frame of that tick (+0xc4): events only fire when it was the previous frame */
    Mat4 world;    /* instance placement */
    /* base class state driven by the script (instance.c, docs/INSTANCE.md) */
    int scripted; /* 0 = animation owned by other code (the player) */
    int slot[4];
    float a_speed, a_base_speed, a_start, a_pos;
    int a_ended; /* +0xb0.., +0xa0, +0xa4, +0xa8, +0xac, +0x9c */
    float fade, fade_target, fade_rate;
    int noncollide;
    uint32_t setflags; /* +0x6c, +0xfc, +0x100, +8 & 0x40, +0xf0 */
    uint32_t traj_flags;
    float traj_start, traj_dur; /* path follower (+0x78) */
    Mat4 *node_world;           /* per node, updated by ins_pose() */
    Mat4 pose_world;
    float pose_frame;
    int pose_anim, pose_valid;
    uint32_t pose_revision;
#ifdef WOODY_PSP
    void *psp_rigid_cache;
    float *psp_light_target;
    uint32_t psp_light_revision;
    int psp_light_valid;
#endif
    int tint_red; /* render colour hook vtbl[26]: vertex colours times (1,0,0) (the rocket's warning
                     blink 0x4537d0) */
    float tint_scale; /* the same hook as a factor on the lit colour, 0 = off: 0.1 = a locked figure
                         of the world-select carousel (0x451a40) */
    int tint_mode;
    float tint_rgb[3]; /* the same hook in general: [0x5ac850] 1 = lit vertex colour times rgb, 2 =
                          plus rgb (1.0 = 255), 0 = off: the bomb, black with red flashes (0x44d9a0)
                        */
    int l_init, light,
        l_seen;    /* model lighting: initialised, chosen light, root seen by it (0x42e3e4) */
    float *plight; /* per node: light vector in the node's own space + light colour, 6 floats
                      (inst+0xf4, 0x18 B per mesh part; 0x43b912) */
    int tex_mode;
    float tex_t0, tex_fac; /* +0xd8 bits 0-2, +0xdc, +0xe0: texture frame override, messages 16 / 18
                              / 19 (docs/INSTANCE.md 2) */
    int uv_mode;
    float uv_t0, uv_fac, uv_t2; /* +0xd8 bits 3-5, +0xe4, +0xe8, +0xec: UV scroll override, messages
                                   15 / 17 / 19 (docs/INSTANCE.md 2) */
    int drawn; /* set by the renderer each frame: this instance survived the visibility pass */
    /* the per-frame instance list world+0x60/+0x64 (0x42a980 -> 0x42a840, rnd_instance_list,
     * docs/INSTANCE.md 4.1): listed = in this frame's list, so its Think vtbl[3] runs (0x42b400)
     * and its 3D sounds play (0x401ee7). cell_sec / cell_grp = +0x1c / +0x18, the sector and the
     * floor group of the cell point (0x407790), recomputed when cell_ref moves; cell_dy > 0: the
     * cell point is position + (0, cell_dy, 0) (an enemy's collision centre, 0x4077f0 in 0x41b2c0),
     * otherwise the animated root inst+0x60 (the clock 0x43f2f1) */
    int listed, in_zone;
    int32_t cell_sec, cell_grp;
    Vec3 cell_ref;
    int cell_ok;
    float cell_dy; /* in_zone: passed the sector / group / link part (the draw gate) */
    /* flag 0x20 of +8 without an own mover (messages 61 / 62 set it on the boss links, 0x40e749 /
     * 0x40d55d): the clock 0x43f2ed never re-cells it, so its cell point stays the .ins position
     * +0xc of the last explicit 0x407790 / 0x4077f0 */
    int cell_fixed;
    /* the sector chain sector+0x44 -> inst+0x24 (0x407790 pushes in front, 0x407850 unlinks) that
     * 0x42a840 walks, and the frame of the last clock run inst+0x58 (0x43eeee: once per frame);
     * render_gl.c keeps them (docs/INSTANCE.md 4.1). chain_sec1 = the sector whose chain holds it +
     * 1, 0 = in no chain */
    struct Instance *cell_next;
    int32_t chain_sec1;
    uint32_t clock_frame;
    /* the pose cache +0x7c (0x42f490, stored by the draw 0x42ecf8 while the clock speed is 0,
     * cleared by 0x42f483 when it is not): position +0xc, animation position +0xac and slot0 +0xb0
     * of the stored pose. A clock run that finds it valid (0x42f3d0, only when fade +0x6c < 0.01)
     * copies the cached matrices and returns before its re-cell (0x43f048..0x43f06e) */
    int pc_ok;
    Vec3 pc_pos;
    float pc_ac;
    int pc_slot;
    uint32_t link_seq; /* > 0: SetTypeInstance 1200 replaced the object and 0x403e7a linked the new
                          one in front (rnd_note_link) */
    /* +0x20 as the collision queries use it: the stamp [0x4c4c08] of the query that already tested
     * this instance (level.c gel_col_instances) */
    uint32_t col_stamp;
    int sh_slot;
    uint32_t
        sh_gen; /* render_gl.c: its cast-shadow cache (slot + 1) and the generation it belongs to */
} Instance;

typedef struct Model {
    uint32_t nnodes, nanims;
    InsAnim *anims;
    uint32_t first_top_node, bbox_node;
    InsNode *nodes;
    uint32_t npoints;
    InsPoint *points;
    uint32_t ntris;
    InsTri *tris;
    uint32_t ninstances;
    Instance *instances;
    uint32_t nvolume_nodes, *volume_nodes;
    uint32_t nmesh_nodes, *mesh_nodes;
    uint32_t ncollision_ids;
    int32_t *owner, *helper;
    float cull_r; /* per point: owning node (built lazily, ins_point_owner); per node: its helper
                     child (render_gl.c) */
    uint32_t ncoll, *coll;
    int coll_ok; /* the press nodes (kind 1), built lazily */
} Model;

typedef struct {
    Vec3 position;
    uint32_t id, index;
    Trajectory traj;
} Camera;

typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t nslots;
    uint32_t nmodels;
    Model *models;
    uint32_t ncameras;
    Camera *cameras;
    Instance **slots; /* nslots pointers (instance or NULL) */
    Camera **cam_slots;
} InsFile;

/* ---- API ----------------------------------------------------------------------- */
int tex_load(TexFile *t, const char *path); /* 0 on success */
int gel_load(GelFile *g, const char *path);
int ins_load(InsFile *f, const char *path);
int lit_load(LitFile *l, const char *path);
int vis_load(VisFile *v, const char *path,
             uint32_t nsectors); /* nsectors comes from the .gel; -1 when the file is missing or
                                    does not match */
int lit_point_lit(const LitLight *l, const GelFile *g,
                  Vec3 p); /* BSP point query 0x40b540 + leaf plane test */
int32_t lit_bsp_face(const LitLight *l, const GelFile *g,
                     Vec3 p);                 /* 0x40b540 itself: the leaf face, -1 = none */
int32_t gel_sector(const GelFile *g, Vec3 p); /* 0x4081c0: the kd sector a point is in, -1 = none */
int32_t gel_cell(const GelFile *g,
                 Vec3 p); /* 0x408180: the kd leaf cell a point is in, -1 = none */
/* 0x40a0c0(p, -1): the polygon of the floor under p (last match in the first cell down the -y links
 * that has one), -1 = none; gel_floor_group = 0x40a26a of that polygon: the section-3 group it lies
 * in, -1 = none (docs/RACE.md 2.1) */
int32_t gel_floor_poly(const GelFile *g, Vec3 p);
int32_t gel_floor_group(const GelFile *g, Vec3 p);
/* 0x408210: the .vis list of p - the entry of p's sector whose id is the floor group under p, else
 * the sector's first entry. NULL = none */
const VisList *vis_entry(const VisFile *v, const GelFile *g, Vec3 p);
/* SetRaceInfo 0x455dc0 tail: the distinct floor groups under the points of the race polyline, in
 * track order, at most 5, -1 terminated */
void gel_race_regions(const GelFile *g, const Vec3 *pts, uint32_t n, int32_t out[6]);
/* Every polygon that crosses a kd leaf meeting the box / the segment, each one once, plus the loose
 * polygons. The set lives in the level's own scratch buffer and is replaced by the next query on
 * the same level. */
GelPolySet gel_polys_in_box(const GelFile *g,
                            const float box[6]); /* box: xmin xmax ymin ymax zmin zmax */
GelPolySet gel_polys_on_seg(const GelFile *g, Vec3 a, Vec3 b);
/* Sectors whose kd subtree meets the box (0x4081c0 widened to a box). Returns how many were written
 * to out. */
uint32_t gel_sectors_in_box(const GelFile *g, const float box[6], int32_t *out, uint32_t max);

/* ---- which instances a collision query tests (docs/FORMAT_TEX_COL_VIS_LIT.md 2.1,
 * docs/PERSO_MOVE.md 6.7) ---------------- Every instance test of the original (floor vt[7]
 * 0x432480, ray vt[5] 0x432ab0, endless ray vt[6] 0x431de0, cylinder vt[8] 0x433140, sphere vt[9]
 * 0x433ff0) runs only for the instances REGISTERED in the kd leaf cells the query visited: the .col
 * lists cell+0x40/+0x44 (static, from the file; each ref = phase mask << 16 | slot), cell by cell
 * in visit order, each ref in file order, then the dynamic list 0x4c3bb4 (the listed flag-0x20
 * instances with press nodes, rebuilt every frame by 0x401c68). An instance is tested once per
 * query (stamp +0x20), only when it is in the world (+0x1c != -1), collidable
 * (+8 & 0x40 clear), has press nodes, and its current animation phase bit (+0xd0) is in the ref's
 * mask. The walks fill the visited cell list 0x4c4be8 / [0x4c4be4]; gel_col_instances() then yields
 * the instances in order. Without a .col file every visible collidable instance with press nodes
 * comes out, in model order (port fallback). */
int gel_col_load(GelFile *g, const char *path); /* 0x4271e0; 0 on success */
const uint32_t *gel_col_cell(const GelFile *g, uint32_t cell,
                             uint32_t *n); /* the refs of one cell, NULL / 0 without a .col */
void gel_col_dynamic(const GelFile *g, struct Instance *const *list,
                     uint32_t n);          /* 0x401c68..0x401cbc from this frame's instance list */
int ins_flag20(const struct Instance *in); /* +8 & 0x20: moved by its own code, re-celled by it */
uint32_t ins_phase_mask(
    const struct Instance *in); /* +0xd0 = (1 << round(min(phase, 1) * 15)) << 16 (0x43f285) */
uint32_t gel_walk_cyl(const GelFile *g, Vec3 c, float r, float up,
                      float down); /* 0x40aa30: flood over the cells the cylinder band touches */
uint32_t gel_walk_sphere(const GelFile *g, Vec3 c,
                         float r); /* 0x40a700: the same for a sphere (cell test 0x40a290) */
uint32_t
gel_walk_down(const GelFile *g, Vec3 p, int found,
              float floor_y); /* 0x498520: p's cell and down the -y links to the floor's cell */
uint32_t gel_walk_seg(const GelFile *g, Vec3 a, Vec3 b, float t_stop,
                      int endless); /* 0x497fb0 / 0x497b10: the cells along a->b up to t_stop */
typedef struct {
    struct Instance *in;
    uint32_t id;
} GelColRef; /* id = the .col ref (mask << 16 | slot) or slot | 0xffff0000 */
uint32_t gel_col_instances(const GelFile *g, const InsFile *ins, const GelColRef **out);
extern void (*gel_col_clock)(struct Instance *in); /* the clock vt[2](1) a query runs on an instance
                                                      it tests (0x4324d6), set by render_gl.c */
void gel_col_force_all(
    int on); /* testing (WOODY_CELLCHECK): every instance, as before the cell lists */
void tex_free(TexFile *t);
void gel_free(GelFile *g);
void ins_free(InsFile *f);
void lit_free(LitFile *l);
void vis_free(VisFile *v);

/* Animation: evaluate the node hierarchy of `inst` for animation `anim` at `t` seconds
 * (wraps around); writes inst->node_world[] (includes the instance placement). */
void ins_pose(Instance *inst, int anim, float t);
/* cinematics (docs/CINEMATIC.md): camera eye/target from the top-level nodes with flags 0x80 /
 * 0x180 of an animation (0x42fa80, with the cut detection of 0x43a660); 0 when the animation has no
 * camera track */
int ins_camera_eval(const Instance *inst, int anim, float phase, Vec3 *eye, Vec3 *target);
/* where the root motion of `anim` leaves the model (0x44edb0): C = W * root(anim, end) * root(anim
 * 0, start)^-1 */
int ins_root_end(const Instance *inst, int anim, Vec3 *pos, Vec3 *forward);
/* root motion 0x44e290: where the root of `anim` at `phase` 0..1 puts a model standing in `base`
 * anim frame 0 (E = B^-1 . A . W) */
int ins_root_at(const Instance *inst, int anim, float phase, int base, Vec3 *pos, Vec3 *forward);
/* Transform point i of the model (owner node applied, pivot subtracted) to world space. */
Vec3 ins_point_world(const Instance *inst, uint32_t point_index);
/* inst+0x60 (0x43f2f1): where the animation has put the model - the world position of its skeleton
 * root. */
Vec3 ins_anim_centre(const Instance *inst);
/* Which node owns point index i (0-based node index). */
int ins_point_owner(const Model *m, uint32_t point_index);
/* The nodes a collision query has to look at: the press nodes (kind 1). */
const uint32_t *ins_collision_nodes(const Model *m, uint32_t *count);
/* World aabb of one node's geometry under the instance's current pose, to reject it without
 * touching its polygons. 0 when the node has no usable box (no points of its own), and the caller
 * has to take it as it comes. */
int ins_node_world_box(const Instance *inst, uint32_t node, float out[6]);

/* math helpers */
void mat4_identity(Mat4 *m);
void mat4_mul(Mat4 *out, const Mat4 *a, const Mat4 *b); /* out = a * b (apply b first) */
void mat4_from_trs(Mat4 *m, Vec3 t, Quat q, Vec3 s);
Vec3 mat4_apply(const Mat4 *m, Vec3 v);
void rgb565_to_rgba(const uint16_t *src, uint8_t *dst, uint32_t n, int colour_key);
void argb1555_to_rgb(uint32_t v, float rgb[3]);

/* getenv with a cache, for the WOODY_* test switches read on hot paths (level.c). Callers pass
 * string literals. */
const char *wenv(const char *name);

#endif
