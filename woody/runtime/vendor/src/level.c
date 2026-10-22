/* level.c - loaders for .gel / .tex / .ins and the skeletal pose evaluation (0x43a3a0). */
#include "level.h"
#ifndef _WIN32
#include "plat.h" /* fopen: the data names ignoring case (plat_sdl.c) */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------- reader */
typedef struct {
    const uint8_t *d;
    size_t pos, size;
    int err;
} Rd;
static uint32_t ru32(Rd *r) {
    if (r->pos + 4 > r->size) {
        r->err = 1;
        r->pos = r->size;
        return 0;
    }
    uint32_t v;
    memcpy(&v, r->d + r->pos, 4);
    r->pos += 4;
    return v;
}
static int32_t ri32(Rd *r) {
    return (int32_t)ru32(r);
}
static float rf32(Rd *r) {
    uint32_t v = ru32(r);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static Vec3 rvec3(Rd *r) {
    Vec3 v;
    v.x = rf32(r);
    v.y = rf32(r);
    v.z = rf32(r);
    return v;
}
static uint32_t *ru32s(Rd *r, uint32_t n) {
    uint32_t *a = (uint32_t *)calloc(n ? n : 1, 4);
    for (uint32_t i = 0; i < n; i++)
        a[i] = ru32(r);
    return a;
}
static const uint8_t *rraw(Rd *r, size_t n) {
    if (r->pos + n > r->size) {
        r->err = 1;
        return NULL;
    }
    const uint8_t *p = r->d + r->pos;
    r->pos += n;
    return p;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz + 16);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *size = (size_t)sz;
    return buf;
}

/* ---------------------------------------------------------------- .tex */
int tex_load(TexFile *t, const char *path) {
    memset(t, 0, sizeof *t);
    t->data = read_file(path, &t->size);
    if (!t->data)
        return -1;
    Rd r = {t->data, 0, t->size, 0};
    t->ngroups = ru32(&r);
    ru32(&r); /* texture_count */
    if (t->ngroups > t->size / 36)
        r.err = 1; /* a damaged file: counts are checked against what the file can hold before
                      anything is allocated */
    t->groups = (TexGroup *)calloc(r.err ? 1 : t->ngroups ? t->ngroups : 1, sizeof(TexGroup));
    if (r.err)
        t->ngroups = 0;
    for (uint32_t g = 0; g < t->ngroups && !r.err; g++) {
        TexGroup *tg = &t->groups[g];
        tg->width = ru32(&r);
        tg->height = ru32(&r);
        tg->flags = ru32(&r);
        tg->scroll_u = rf32(&r);
        tg->scroll_v = rf32(&r);
        tg->anim_duration = rf32(&r);
        tg->frame_count = ru32(&r);
        ru32(&r);
        ru32(&r);
        size_t fb = (size_t)tg->width * tg->height;
        if (!fb || tg->width > 512 || tg->height > 512 || !tg->frame_count ||
            tg->frame_count > 32) {
            r.err = 1;
            tg->frame_count = 0;
            break;
        }
        tg->ps2_palettes = calloc(tg->frame_count, sizeof *tg->ps2_palettes);
        tg->ps2_frames = calloc(tg->frame_count, sizeof *tg->ps2_frames);
        unsigned w = tg->width, h = tg->height;
        fb = 0;
        do {
            fb += (size_t)w * h;
            tg->ps2_levels++;
            if (w <= 16 || h <= 16)
                break;
            w /= 2;
            h /= 2;
        } while (1);
        if (!tg->ps2_palettes || !tg->ps2_frames) {
            r.err = 1;
            break;
        }
        for (uint32_t f = 0; f < tg->frame_count && !r.err; f++) {
            /* Entry zero belongs to the 256-entry palette. Each PS2 payload
             * ends with "EKO\0", not a header before the palette. */
            tg->ps2_palettes[f] = rraw(&r, 1024);
            tg->ps2_frames[f] = rraw(&r, fb);
            if (ru32(&r) != 0x004f4b45)
                r.err = 1;
        }
    }
    t->nmaterials = r.err ? 0 : ru32(&r);
    if (t->nmaterials > (r.size - r.pos) / 52) {
        r.err = 1;
        t->nmaterials = 0;
    }
    t->materials = (Material *)calloc(t->nmaterials ? t->nmaterials : 1, sizeof(Material));
    for (uint32_t i = 0; i < t->nmaterials; i++) {
        t->materials[i].group = ru32(&r);
        for (int k = 0; k < 12; k++)
            t->materials[i].m[k] = rf32(&r);
    }
    if (r.err || r.pos != r.size) {
        fprintf(stderr, "%s: parse error (pos %zu of %zu)\n", path, r.pos, r.size);
        return -1;
    }
    return 0;
}
void tex_free(TexFile *t) {
    for (uint32_t g = 0; g < t->ngroups; g++) {
        free(t->groups[g].frames);
        free(t->groups[g].ps2_palettes);
        free(t->groups[g].ps2_frames);
    }
    free(t->groups);
    free(t->materials);
    free(t->data);
    memset(t, 0, sizeof *t);
}

/* ---------------------------------------------------------------- .gel */
/* Scratch of the kd queries below. It hangs off the level so the query functions can stay const:
 * the frame stamp is the port's equivalent of the dword at poly+4 that 0x407000 and 0x42ac10 use to
 * handle a polygon only once when several cells list it. */
struct GelQuery {
    uint32_t *stamp, gen;  /* per polygon: the generation that last collected it */
    uint32_t *out, n, cap; /* the polygons of the running query */
    uint32_t *all;
    int off; /* 0..npolys-1, built on demand for levels without a kd-tree */
    int32_t *stack;
    uint32_t scap;        /* node stack of the box query */
    struct SegNode *segs; /* node + clipped segment stack of the segment query */
};
/* The instance side of the collision queries (gel_col_* below, docs/FORMAT_TEX_COL_VIS_LIT.md 2.1).
 */
struct GelCol {
    int have; /* a .col file matching the cells was loaded */
    uint32_t *first,
        *refs; /* .col: the refs of cell c are refs[first[c] .. first[c+1]) (cell+0x40 / +0x44) */
    Instance **dyn;
    uint32_t ndyn, dyn_cap; /* the dynamic list 0x4c3bb4 / [0x4c4bec] */
    int32_t *visit;
    uint32_t nvisit, visit_cap; /* the visited cells 0x4c4be8 / [0x4c4be4] of the last walk */
    uint32_t *cstamp, cgen;     /* cell stamp +4 / [0x4c3bac] of the flood walks */
    uint32_t igen;              /* instance stamp [0x4c4c08] (Instance.col_stamp) */
    GelColRef *out;
    uint32_t nout, out_cap; /* the instances of the last gel_col_instances() */
};
struct SegNode {
    int32_t node;
    float a[3], b[3];
};

/* One cell record (0x407f33 / 0x4080e6): u32 npoly, u32 poly[npoly], f32 bbox[6], i32 link[6], u32
 * nnodes, node[nnodes]. The six neighbour links and their local subtrees are a shortcut from a cell
 * to the cell next to it; the queries below descend from the root instead, only the floor search
 * 0x40a0c0 follows the -y link. */
static int read_cells(Rd *r, GelCell *out, uint32_t n) {
    for (uint32_t i = 0; i < n && !r->err; i++) {
        GelCell *c = &out[i];
        c->npolys = ru32(r);
        c->polys = (const uint32_t *)rraw(r, 4 * (size_t)c->npolys);
        for (int k = 0; k < 6; k++)
            c->bbox[k] = rf32(r);
        for (int k = 0; k < 6; k++)
            c->link[k] = (int32_t)ru32(r);
        c->nnodes = ru32(r);
        c->nodes = (const uint8_t *)rraw(r, 16 * (size_t)c->nnodes);
        if (r->pos > r->size)
            r->err = 1;
    }
    return r->err;
}

static void gel_build_queries(GelFile *g) {
    g->q = (struct GelQuery *)calloc(1, sizeof *g->q);
    g->q->stamp = (uint32_t *)calloc(g->npolys ? g->npolys : 1, 4);
    g->q->off =
        wenv("WOODY_NOKD") !=
        NULL; /* fall back to scanning the whole level, to tell a tree bug from a collision bug */
    g->col = (struct GelCol *)calloc(1, sizeof *g->col);
    g->col->cstamp = (uint32_t *)calloc(g->ncells ? g->ncells : 1, 4);
    /* A kd leaf that lies above every sector root belongs to no sector, and a polygon can miss
     * every cell list. Such polygons have no place in the tree to be found from, so both the
     * renderer and the queries always take them along. In well built levels there are none. */
    uint8_t *seen = (uint8_t *)calloc(g->npolys ? g->npolys : 1, 1);
    for (uint32_t i = 0; i < g->ncells; i++)
        for (uint32_t k = 0; k < g->cells[i].npolys; k++) {
            uint32_t q = g->cells[i].polys[k];
            if (q < g->npolys)
                seen[q] |= 1;
        }
    for (uint32_t i = 0; i < g->nsectors; i++)
        for (uint32_t k = 0; k < g->sectors[i].npolys; k++) {
            uint32_t q = g->sectors[i].polys[k];
            if (q < g->npolys)
                seen[q] |= 2;
        }
    for (uint32_t i = 0; i < g->npolys; i++)
        if (seen[i] != 3)
            g->nloose++;
    if (g->nloose) {
        g->loose = (uint32_t *)malloc((size_t)g->nloose * 4);
        g->nloose = 0;
        for (uint32_t i = 0; i < g->npolys; i++)
            if (seen[i] != 3)
                g->loose[g->nloose++] = i;
    }
    free(seen);
}

int gel_load(GelFile *g, const char *path) {
    memset(g, 0, sizeof *g);
    g->data = read_file(path, &g->size);
    if (!g->data)
        return -1;
    Rd r = {g->data, 0, g->size, 0};
    g->npolys = ru32(&r);
    ru32(&r); /* total indices */
    g->polys = (GelPoly *)calloc(g->npolys, sizeof(GelPoly));
    for (uint32_t i = 0; i < g->npolys; i++) {
        GelPoly *p = &g->polys[i];
        p->nverts = ru32(&r);
        p->material = ru32(&r);
        for (int k = 0; k < 4; k++)
            p->plane[k] = rf32(&r);
        p->indices = ru32s(&r, p->nverts);
    }
    /* portals: only the group lists below address them */
    uint32_t nport = ru32(&r);
    ru32(&r);
    for (uint32_t i = 0; i < nport; i++) {
        uint32_t n = ru32(&r);
        r.pos += 16 + 4 * (size_t)n;
    }
    /* groups (zones): count x exclusive end index, then a portal list per group */
    g->ngroups = ru32(&r);
    g->groups = (GelGroup *)calloc(g->ngroups ? g->ngroups : 1, sizeof(GelGroup));
    for (uint32_t i = 0, prev = 0; i < g->ngroups; i++) {
        uint32_t end = ru32(&r);
        g->groups[i].first = prev;
        g->groups[i].end = end;
        prev = end;
    }
    for (uint32_t i = 0; i < g->ngroups; i++) {
        uint32_t k = ru32(&r);
        r.pos += 8 * (size_t)k;
    }
    g->nverts = ru32(&r);
    g->verts = (GelVert *)calloc(g->nverts, sizeof(GelVert));
    for (uint32_t i = 0; i < g->nverts; i++) {
        g->verts[i].x = rf32(&r);
        g->verts[i].y = rf32(&r);
        g->verts[i].z = rf32(&r);
        g->verts[i].colour = ru32(&r);
    }
    /* cells (0x407f33) and, with the same record, the sectors behind the kd-tree */
    g->ncells = ru32(&r);
    g->cells = (GelCell *)calloc(g->ncells ? g->ncells : 1, sizeof(GelCell));
    read_cells(&r, g->cells, g->ncells);
    g->nkd = ru32(&r);
    g->kd = (KdNode *)calloc(g->nkd ? g->nkd : 1, sizeof(KdNode));
    for (uint32_t i = 0; i < g->nkd && !r.err; i++) {
        int32_t t = (int32_t)ru32(&r);
        g->kd[i].axis = (int16_t)(t & 0xffff);
        g->kd[i].sector = t >> 16;
        g->kd[i].d = rf32(&r);
        g->kd[i].le = (int32_t)ru32(&r);
        g->kd[i].gt = (int32_t)ru32(&r);
    }
    g->nsectors = ru32(&r);
    g->sectors = (GelCell *)calloc(g->nsectors ? g->nsectors : 1, sizeof(GelCell));
    read_cells(&r, g->sectors, g->nsectors);
    if (r.err) {
        fprintf(stderr, "%s: parse error\n", path);
        return -1;
    }
    gel_build_queries(g);
    float *b = g->bbox;
    b[0] = b[2] = b[4] = 1e30f;
    b[1] = b[3] = b[5] = -1e30f;
    for (uint32_t i = 0; i < g->nverts; i++) {
        GelVert *v = &g->verts[i];
        if (v->x < b[0])
            b[0] = v->x;
        if (v->x > b[1])
            b[1] = v->x;
        if (v->y < b[2])
            b[2] = v->y;
        if (v->y > b[3])
            b[3] = v->y;
        if (v->z < b[4])
            b[4] = v->z;
        if (v->z > b[5])
            b[5] = v->z;
    }
    return 0;
}
void gel_free(GelFile *g) {
    for (uint32_t i = 0; i < g->npolys; i++)
        free(g->polys[i].indices);
    free(g->polys);
    free(g->verts);
    free(g->kd);
    free(g->groups);
    free(g->cells);
    free(g->sectors);
    free(g->loose);
    if (g->q) {
        free(g->q->stamp);
        free(g->q->out);
        free(g->q->all);
        free(g->q->stack);
        free(g->q->segs);
        free(g->q);
    }
    if (g->col) {
        free(g->col->first);
        free(g->col->refs);
        free(g->col->dyn);
        free(g->col->visit);
        free(g->col->cstamp);
        free(g->col->out);
        free(g->col);
    }
    free(g->data);
    memset(g, 0, sizeof *g);
}

/* 0x4081c0: which sector a point falls in - walk down from the root until a node carries a sector
 * index */
int32_t gel_sector(const GelFile *g, Vec3 p) {
    if (!g->nkd)
        return -1;
    const float v[3] = {p.x, p.y, p.z};
    int32_t i = 0;
    for (uint32_t guard = 0; guard < g->nkd; guard++) {
        const KdNode *n = &g->kd[i];
        if (n->sector != -1)
            return (uint32_t)n->sector < g->nsectors ? n->sector : -1;
        if (n->axis < 0 || n->axis > 2)
            return -1;
        int32_t c = v[n->axis] + n->d <= 0 ? n->le : n->gt;
        if (c < 0 || (uint32_t)c >= g->nkd)
            return -1; /* a leaf before a sector root: outside every sector */
        i = c;
    }
    return -1;
}

/* 0x408180: which kd leaf cell a point falls in - down to a leaf, whatever sectors are passed on
 * the way */
int32_t gel_cell(const GelFile *g, Vec3 p) {
    if (!g->ncells)
        return -1;
    if (!g->nkd)
        return 0;
    const float v[3] = {p.x, p.y, p.z};
    int32_t i = 0;
    for (uint32_t guard = 0; guard <= g->nkd; guard++) {
        const KdNode *n = &g->kd[i];
        if (n->axis < 0 || n->axis > 2)
            return -1;
        int32_t c = v[n->axis] + n->d <= 0 ? n->le : n->gt;
        if (c < 0) {
            uint32_t k = (uint32_t)~c;
            return k < g->ncells ? (int32_t)k : -1;
        }
        if ((uint32_t)c >= g->nkd)
            return -1;
        i = c;
    }
    return -1;
}

/* 0x40a0c0(p, -1): the floor polygon under a point. Start in the kd leaf of p (0x408180); in that
 * cell's polygon list take every polygon that faces up (ny >= 0, 0x40a141), has p in front of it
 * (n.p + D > 0, 0x40a16a), lies no lower than the cell's bottom (n.p + D <= ny * (p.y - cell.ymin)
 * + 0.001, 0x40a17b, [0x4a94c4] = 0.001) and holds p in its xz shadow (every edge prev -> cur:
 * (cur.x-p.x)(prev.z-p.z) - (prev.x-p.x)(cur.z-p.z) >= 0, 0x40a1b3..0x40a1f8). The loop does not
 * stop at a hit: the LAST hit of the list wins, not the highest. No hit: follow the cell's -y link
 * (+0x18): >= 0 a local subtree (0x40ab60, child indices relative to its root), INT_MIN = nothing
 * below (-1), else cell ~link. */
static int32_t cell_subtree(const GelCell *c, int32_t root, Vec3 p) {
    const float v[3] = {p.x, p.y, p.z};
    int32_t i = 0;
    for (uint32_t guard = 0; guard <= c->nnodes; guard++) {
        if (root < 0 || (uint32_t)(root + i) >= c->nnodes)
            return -1;
        const uint8_t *n = c->nodes + 16 * (size_t)(root + i);
        int32_t t, le, gt;
        float d;
        memcpy(&t, n, 4);
        memcpy(&d, n + 4, 4);
        memcpy(&le, n + 8, 4);
        memcpy(&gt, n + 12, 4);
        int axis = (int16_t)(t & 0xffff);
        float x = axis == 0   ? v[0] + d
                  : axis == 1 ? v[1] + d
                  : axis == 2 ? v[2] + d
                              : 0.0f; /* 0x40ab10: other axis = 0.0 */
        int32_t ch = x <= 0 ? le : gt;
        if (ch < 0)
            return ~ch;
        i = ch;
    }
    return -1;
}
int32_t gel_floor_poly(const GelFile *g, Vec3 p) {
    int32_t c = gel_cell(g, p);
    for (uint32_t guard = 0; c >= 0 && (uint32_t)c < g->ncells && guard <= g->ncells; guard++) {
        const GelCell *C = &g->cells[c];
        float dy = p.y - C->bbox[2];
        int32_t hit = -1;
        for (uint32_t k = 0; k < C->npolys; k++) {
            uint32_t i = C->polys[k];
            if (i >= g->npolys)
                continue;
            const GelPoly *P = &g->polys[i];
            const float *pl = P->plane;
            if (pl[1] < 0 || P->nverts < 1)
                continue;
            float d = pl[1] * p.y + pl[2] * p.z + pl[0] * p.x + pl[3];
            if (d <= 0 || pl[1] * dy + 0.001f < d)
                continue;
            const GelVert *a = &g->verts[P->indices[P->nverts - 1]];
            uint32_t e = 0;
            for (; e < P->nverts; e++) {
                const GelVert *b = &g->verts[P->indices[e]];
                if ((b->x - p.x) * (a->z - p.z) - (a->x - p.x) * (b->z - p.z) < 0)
                    break;
                a = b;
            }
            if (e == P->nverts)
                hit = (int32_t)i;
        }
        if (hit >= 0)
            return hit;
        int32_t l = C->link[2]; /* -y */
        if (l == INT32_MIN)
            return -1;
        c = l >= 0 ? cell_subtree(C, l, p) : ~l;
    }
    return -1;
}
int32_t gel_floor_group(const GelFile *g, Vec3 p) {
    int32_t f = gel_floor_poly(g, p);
    if (f < 0)
        return -1;
    for (uint32_t i = 0; i < g->ngroups; i++)
        if ((uint32_t)f < g->groups[i].end)
            return (int32_t)i; /* 0x40a26a: first group whose last polygon >= f */
    return -1;
}
const VisList *vis_entry(const VisFile *v, const GelFile *g, Vec3 p) {
    int32_t s = gel_sector(g, p); /* 0x40821a */
    if (!v || s < 0 || (uint32_t)s >= v->nsectors || !v->sectors[s].nlists)
        return NULL;
    int32_t grp = gel_floor_group(g, p); /* 0x408224 */
    const VisSector *S = &v->sectors[s];
    for (uint32_t e = 0; e < S->nlists; e++)
        if ((int32_t)v->pool[S->first + e].id == grp)
            return &v->pool[S->first + e]; /* 0x40823f */
    return &v->pool[S->first];             /* 0x408256: no entry with that id -> the first */
}
void gel_race_regions(const GelFile *g, const Vec3 *pts, uint32_t n, int32_t out[6]) {
    int k = 0;
    for (int i = 0; i < 6; i++)
        out[i] = -1; /* 0x455ee0 list of 5 = -1, +0xd4 = -1 terminator */
    for (uint32_t i = 0; i < n && i < 0x7fff; i++) { /* count read as a 16-bit word (0x455f2a) */
        int32_t grp = gel_floor_group(g, pts[i]);    /* 0x455f51 */
        if (grp == -1 || out[k] == grp)
            continue;
        if (out[k] != -1) {
            if (++k == 5)
                break;
        }
        out[k] = grp;
    }
}

/* ---- polygon queries over the kd-tree ---------------------------------------------------------
 * A cell lists every polygon that crosses it, so the polygons of the cells a query volume meets are
 * exactly the ones that can answer it. A polygon that reaches into several cells is collected once,
 * by its frame stamp. */
#define GEL_QEPS 0.5f /* a point exactly on a split plane must reach both sides */

static void q_reserve(struct GelQuery *q, uint32_t extra) {
    if (q->n + extra <= q->cap)
        return;
    q->cap = (q->n + extra) * 2 + 256;
    q->out = (uint32_t *)realloc(q->out, (size_t)q->cap * 4);
}
static void q_add(const GelFile *g, const uint32_t *polys, uint32_t n) {
    struct GelQuery *q = g->q;
    q_reserve(q, n);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = polys[k];
        if (i >= g->npolys || q->stamp[i] == q->gen)
            continue;
        q->stamp[i] = q->gen;
        q->out[q->n++] = i;
    }
}
static void q_begin(const GelFile *g) {
    struct GelQuery *q = g->q;
    q->n = 0;
    if (++q->gen == 0) {
        memset(q->stamp, 0, (size_t)g->npolys * 4);
        q->gen = 1;
    }
    if (g->nloose)
        q_add(g, g->loose, g->nloose);
}
static GelPolySet q_all(const GelFile *g) /* no tree: the whole level, as before */
{
    struct GelQuery *q = g->q;
    if (!q->all) {
        q->all = (uint32_t *)malloc((size_t)(g->npolys ? g->npolys : 1) * 4);
        for (uint32_t i = 0; i < g->npolys; i++)
            q->all[i] = i;
    }
    GelPolySet s = {g->npolys, q->all};
    return s;
}
static int32_t *q_stack(const GelFile *g) {
    struct GelQuery *q = g->q;
    if (q->scap < g->nkd + 4) {
        q->scap = g->nkd + 4;
        q->stack = (int32_t *)realloc(q->stack, (size_t)q->scap * sizeof(int32_t));
    }
    return q->stack;
}

GelPolySet gel_polys_in_box(const GelFile *g, const float box[6]) {
    if (!g->ncells || !g->q || g->q->off)
        return q_all(g);
    q_begin(g);
    if (!g->nkd)
        q_add(g, g->cells[0].polys, g->cells[0].npolys);
    else {
        int32_t *st = q_stack(g);
        uint32_t sp = 0;
        st[sp++] = 0;
        while (sp) {
            const KdNode *n = &g->kd[st[--sp]];
            if (n->axis < 0 || n->axis > 2)
                continue;
            float split = -n->d; /* the test is p[axis] + d <= 0 */
            for (int side = 0; side < 2; side++) {
                if (side == 0 ? box[n->axis * 2] > split + GEL_QEPS
                              : box[n->axis * 2 + 1] <= split - GEL_QEPS)
                    continue;
                int32_t c = side == 0 ? n->le : n->gt;
                if (c < 0) {
                    uint32_t k = (uint32_t)~c;
                    if (k < g->ncells)
                        q_add(g, g->cells[k].polys, g->cells[k].npolys);
                } else if ((uint32_t)c < g->nkd && sp < g->nkd + 4)
                    st[sp++] = c;
            }
        }
    }
    {
        GelPolySet s = {g->q->n, g->q->out};
        return s;
    }
}

GelPolySet gel_polys_on_seg(const GelFile *g, Vec3 a, Vec3 b) {
    if (!g->ncells || !g->q || g->q->off)
        return q_all(g);
    q_begin(g);
    if (!g->nkd) {
        q_add(g, g->cells[0].polys, g->cells[0].npolys);
        GelPolySet s = {g->q->n, g->q->out};
        return s;
    }
    struct GelQuery *q = g->q;
    if (!q->segs)
        q->segs = (struct SegNode *)malloc((size_t)(g->nkd + 4) * sizeof(struct SegNode));
    uint32_t sp = 0;
    {
        struct SegNode *e = &q->segs[sp++];
        e->node = 0;
        e->a[0] = a.x;
        e->a[1] = a.y;
        e->a[2] = a.z;
        e->b[0] = b.x;
        e->b[1] = b.y;
        e->b[2] = b.z;
    }
    while (sp) {
        struct SegNode e = q->segs[--sp];
        const KdNode *n = &g->kd[e.node];
        if (n->axis < 0 || n->axis > 2)
            continue;
        float split = -n->d, da = e.a[n->axis] - split, db = e.b[n->axis] - split;
        float lo = da < db ? da : db, hi = da < db ? db : da;
        int crosses = (da <= 0) != (db <= 0);
        float m[3];
        if (crosses) {
            float t = da / (da - db);
            for (int k = 0; k < 3; k++)
                m[k] = e.a[k] + (e.b[k] - e.a[k]) * t;
        }
        for (int side = 0; side < 2; side++) {
            if (side == 0 ? lo > GEL_QEPS : hi <= -GEL_QEPS)
                continue; /* nothing of the segment on this side */
            const float *sa = e.a, *sb = e.b;
            if (crosses) {
                if ((da <= 0) == (side == 0))
                    sb = m;
                else
                    sa = m;
            } /* hand each side its own half */
            int32_t c = side == 0 ? n->le : n->gt;
            if (c < 0) {
                uint32_t k = (uint32_t)~c;
                if (k < g->ncells)
                    q_add(g, g->cells[k].polys, g->cells[k].npolys);
            } else if ((uint32_t)c < g->nkd && sp < g->nkd + 4) {
                struct SegNode *o = &q->segs[sp++];
                o->node = c;
                memcpy(o->a, sa, 12);
                memcpy(o->b, sb, 12);
            }
        }
    }
    {
        GelPolySet s = {q->n, q->out};
        return s;
    }
}

/* 0x4081c0 widened to a box: every sector whose root node the box can reach. Each sector has
 * exactly one root node, so no sector comes out twice. */
uint32_t gel_sectors_in_box(const GelFile *g, const float box[6], int32_t *out, uint32_t max) {
    uint32_t n = 0;
    if (!g->nsectors || !g->nkd || !g->q)
        return 0;
    int32_t *st = q_stack(g);
    uint32_t sp = 0;
    st[sp++] = 0;
    while (sp) {
        const KdNode *nd = &g->kd[st[--sp]];
        if (nd->sector >= 0) {
            if ((uint32_t)nd->sector < g->nsectors && n < max)
                out[n++] = nd->sector;
            continue;
        }
        if (nd->axis < 0 || nd->axis > 2)
            continue;
        float split = -nd->d;
        for (int side = 0; side < 2; side++) {
            if (side == 0 ? box[nd->axis * 2] > split + GEL_QEPS
                          : box[nd->axis * 2 + 1] <= split - GEL_QEPS)
                continue;
            int32_t c = side == 0 ? nd->le : nd->gt;
            if (c >= 0 && (uint32_t)c < g->nkd && sp < g->nkd + 4)
                st[sp++] = c; /* a leaf before a sector root has no sector */
        }
    }
    return n;
}

/* ---------------------------------------------------------------- .col and the collision cell
 * walks */
/* .col loader 0x4271e0 (docs/FORMAT_TEX_COL_VIS_LIT.md 2): for every kd leaf cell in .gel order a
 * u32 count n (-> cell+0x40) and n refs (-> cell+0x44), each ref = phase mask << 16 | object index
 * (world+0x40 = the script slots). Nothing in the exe writes cell+0x40/+0x44 after the load: the
 * registration is static, the MASK says at which of 16 animation phases the instance reaches into
 * the cell (a platform on its path is listed along the path, one phase bit per stretch). */
int gel_col_load(GelFile *g, const char *path) {
    struct GelCol *C = g->col;
    if (!C)
        return -1;
    free(C->first);
    free(C->refs);
    C->first = C->refs = NULL;
    C->have = 0;
    size_t size;
    uint8_t *d = read_file(path, &size);
    if (!d)
        return -1;
    size_t nw = size / 4, o = 0;
    uint32_t cells = 0;
    const uint32_t *w = (const uint32_t *)d;
    while (o < nw) {
        uint32_t c;
        memcpy(&c, &w[o], 4);
        if (o + 1 + c > nw)
            break;
        o += 1 + c;
        cells++;
    }
    if (o != nw || cells != g->ncells) {
        fprintf(stderr, "%s: %u records for %u kd cells, not used\n", path, cells, g->ncells);
        free(d);
        return -1;
    }
    C->first = (uint32_t *)malloc((cells + 1) * sizeof *C->first);
    C->refs = (uint32_t *)malloc((nw - cells + 1) * sizeof *C->refs);
    uint32_t k = 0;
    o = 0;
    for (uint32_t c = 0; c < cells; c++) {
        uint32_t m;
        memcpy(&m, &w[o++], 4);
        C->first[c] = k;
        for (uint32_t j = 0; j < m; j++)
            memcpy(&C->refs[k++], &w[o++], 4);
    }
    C->first[cells] = k;
    C->have = 1;
    free(d);
    return 0;
}
const uint32_t *
gel_col_cell(const GelFile *g, uint32_t cell,
             uint32_t *n) /* the refs of one cell (0x42aa0b reads the camera's leaf) */
{
    const struct GelCol *C = g->col;
    *n = 0;
    if (!C || !C->have || cell >= g->ncells)
        return NULL;
    *n = C->first[cell + 1] - C->first[cell];
    return C->refs + C->first[cell];
}

/* +8 & 0x20 in the port's terms: the instances whose own code moves and re-cells them (0x4077f0),
 * so the clock never does - the Perso and the race rider (types 1-3, 18, 19 bound to the player),
 * the enemies and bosses (PostLoad 0x419e4d, types 4-16; Instance.cell_dy), the links of messages
 * 59/61/62 (Instance.cell_fixed, 0x40e749 / 0x40d55d), the rocket and the bomb cannon (1200 ->
 * 0x4529b5, types 20/21) and the bombs (0x44d250, type 40). */
int ins_flag20(const Instance *in) {
    if (in->cell_dy > 0 || in->cell_fixed)
        return 1;
    if (!in->scripted &&
        (in->type == 1 || in->type == 2 || in->type == 3 || in->type == 18 || in->type == 19))
        return 1;
    return in->type == 20 || in->type == 21 || in->type == 40 || (in->type >= 4 && in->type <= 16);
}
/* The dynamic list 0x4c3bb4, rebuilt right after the instance list (0x401c68..0x401cbc): every
 * instance of this frame's list world+0x64, in list order, with flag 0x20, object kind 1 and press
 * nodes (model S+0x58 != 0), at most 0x3ff. The queries test these after the cell lists, with the
 * id slot | 0xffff0000 (every phase). */
void gel_col_dynamic(const GelFile *g, Instance *const *list, uint32_t n) {
    struct GelCol *C = g->col;
    if (!C)
        return;
    C->ndyn = 0;
    for (uint32_t i = 0; i < n && C->ndyn < 0x3ff; i++) {
        Instance *in = list[i];
        uint32_t nc;
        if (!in || !ins_flag20(in))
            continue;
        ins_collision_nodes(in->model, &nc);
        if (!nc)
            continue;
        if (C->ndyn >= C->dyn_cap) {
            C->dyn_cap = C->dyn_cap ? C->dyn_cap * 2 : 64;
            C->dyn = (Instance **)realloc(C->dyn, C->dyn_cap * sizeof *C->dyn);
        }
        C->dyn[C->ndyn++] = in;
    }
    if (wenv("WOODY_CELLCOL")) { /* testing: the dynamic list whenever it changes */
        static uint32_t last_n = 0xffffffffu, last_sum;
        uint32_t sum = 0;
        for (uint32_t i = 0; i < C->ndyn; i++)
            sum = sum * 31 + C->dyn[i]->index;
        if (C->ndyn != last_n || sum != last_sum) {
            printf("CELLCOL dynamic list (%u):", C->ndyn);
            for (uint32_t i = 0; i < C->ndyn; i++)
                printf(" %u(type %d)", C->dyn[i]->index, C->dyn[i]->type);
            puts("");
            last_n = C->ndyn;
            last_sum = sum;
        }
    }
}
/* +0xd0, written by the clock 0x43eee0 after the phase (0x43f264..0x43f2a3): phase = position /
 * length of the current animation, min(phase, 1.0) * 15.0 + 0.5 truncated (0x499580) = the bit,
 * shifted up by 16. The tests AND it with the ref's high half (0x4324df and the same lines in
 * 0x432b11, 0x431e40, 0x4331c3, 0x434054). */
uint32_t ins_phase_mask(const Instance *in) {
    const Model *m = in->model;
    float L = 1.0f;
    if (in->anim >= 0 && (uint32_t)in->anim < m->nanims && m->anims[in->anim].duration_s > 0)
        L = m->anims[in->anim].duration_s;
    float ph = in->anim_time / L;
    if (!(ph >= 0))
        ph = 0;
    if (ph > 1.0f)
        ph = 1.0f;
    return (1u << (uint32_t)(ph * 15.0f + 0.5f)) << 16;
}

static void visit_push(struct GelCol *C, int32_t cell) {
    if (C->nvisit >= C->visit_cap) {
        C->visit_cap = C->visit_cap ? C->visit_cap * 2 : 64;
        C->visit = (int32_t *)realloc(C->visit, C->visit_cap * sizeof *C->visit);
    }
    C->visit[C->nvisit++] = cell;
}
static void cstamp_next(const GelFile *g, struct GelCol *C) {
    if (++C->cgen == 0) {
        memset(C->cstamp, 0, (size_t)(g->ncells ? g->ncells : 1) * 4);
        C->cgen = 1;
    }
}
/* the cell tests of the two floods. 0x40a7a0 (cylinder): out if c.y + up < ymin or c.y - down >
 * ymax; then region codes in xz (x > xmax 8, x < xmin 4, z > zmax 2, z < zmin 1, jump table
 * 0x40a900): inside -> in; one axis out -> in when c + r > min / c - r < max; a corner -> in when
 * the xz distance to the corner < r. 0x40a290 (sphere): the same in three axes (table 0x40a5a0 via
 * the byte map 0x40a610): squared distance to the box < r^2, a face region by c +- r. */
typedef struct {
    Vec3 c;
    float r, up, down;
    int sphere;
} FloodQ;
static int flood_test(const GelCell *K, const FloodQ *q) {
    const float *b = K->bbox;
    float d2 = 0;
    int out = 0;
    const float v[3] = {q->c.x, q->c.y, q->c.z};
    if (!q->sphere) {
        if (q->c.y + q->up < b[2] || q->c.y - q->down > b[3])
            return 0;
    }
    for (int a = 0; a < 3; a++) {
        if (a == 1 && !q->sphere)
            continue;
        float d = 0;
        if (v[a] > b[2 * a + 1])
            d = v[a] - b[2 * a + 1];
        else if (v[a] < b[2 * a])
            d = b[2 * a] - v[a];
        else
            continue;
        out++;
        d2 += d * d;
        if (out == 1 && !(d < q->r))
            return 0; /* a face region: c + r > min / c - r < max */
    }
    return out <= 1 ? 1 : d2 < q->r * q->r;
}
/* 0x40a930 / 0x40a640: a link of a cell - a local kd subtree (both children of every node, the <=
 * side first: recursion on node+8, then loop on node+0xc; indices relative to the subtree root) or
 * a leaf ~link (INT_MIN = nothing). A leaf is stamped BEFORE its test (0x40a99d), appended when it
 * passes, and then its six links are walked in the order -x +x -y +y -z +z. */
static void flood_link(const GelFile *g, struct GelCol *C, const FloodQ *q, const uint8_t *base,
                       uint32_t nbase, int32_t idx, int depth) {
    for (uint32_t guard = 0; idx >= 0 && guard <= nbase; guard++) {
        if (!base || (uint32_t)idx >= nbase)
            return;
        const uint8_t *n = base + 16 * (size_t)idx;
        int32_t le, gt;
        memcpy(&le, n + 8, 4);
        memcpy(&gt, n + 12, 4);
        flood_link(g, C, q, base, nbase, le, depth + 1);
        idx = gt;
    }
    if (idx >= 0 || idx == INT32_MIN || depth > 4096)
        return;
    uint32_t cell = (uint32_t)~idx;
    if (cell >= g->ncells || C->cstamp[cell] == C->cgen)
        return;
    C->cstamp[cell] = C->cgen;
    const GelCell *K = &g->cells[cell];
    if (!flood_test(K, q))
        return;
    visit_push(C, (int32_t)cell);
    for (int f = 0; f < 6; f++) {
        int32_t l = K->link[f];
        if (l < 0)
            flood_link(g, C, q, NULL, 0, l, depth + 1);
        else if ((uint32_t)l < K->nnodes)
            flood_link(g, C, q, K->nodes + 16 * (size_t)l, K->nnodes - (uint32_t)l, 0, depth + 1);
    }
}
static uint32_t flood(const GelFile *g, const FloodQ *q) /* 0x40aa30 / 0x40a700 */
{
    struct GelCol *C = g->col;
    if (!C)
        return 0;
    C->nvisit = 0;
    int32_t c0 = gel_cell(g, q->c);
    if (c0 < 0)
        return 0; /* the original indexes cells[-1] here */
    cstamp_next(g, C);
    C->cstamp[c0] = C->cgen;
    visit_push(C, c0); /* the start cell is not tested (0x40aa76) */
    const GelCell *K = &g->cells[c0];
    for (int f = 0; f < 6; f++) {
        int32_t l = K->link[f];
        if (l < 0)
            flood_link(g, C, q, NULL, 0, l, 1);
        else if ((uint32_t)l < K->nnodes)
            flood_link(g, C, q, K->nodes + 16 * (size_t)l, K->nnodes - (uint32_t)l, 0, 1);
    }
    return C->nvisit;
}
uint32_t gel_walk_cyl(const GelFile *g, Vec3 c, float r, float up, float down) {
    FloodQ q = {c, r, up, down, 0};
    return flood(g, &q);
}
uint32_t gel_walk_sphere(const GelFile *g, Vec3 c, float r) {
    FloodQ q = {c, r, r, r, 1};
    return flood(g, &q);
}
/* 0x498520: the floor search visits p's cell (0x408180) and, while a cell holds no floor, the cell
 * under it through the -y link (+0x18: a local subtree descended with p itself, 0x40ab60, or ~link;
 * INT_MIN = no cell below). A cell accepts a floor polygon only within its own height (maxd = p.y -
 * cell.ymin, 0x4985b9), so the walk ends in the cell that holds the floor point. found / floor_y =
 * the floor the world part found (the port finds it with its own polygon query). */
uint32_t gel_walk_down(const GelFile *g, Vec3 p, int found, float floor_y) {
    struct GelCol *C = g->col;
    if (!C)
        return 0;
    C->nvisit = 0;
    int32_t c = gel_cell(g, p);
    for (uint32_t guard = 0; c >= 0 && (uint32_t)c < g->ncells && guard <= g->ncells; guard++) {
        const GelCell *K = &g->cells[c];
        visit_push(C, c);
        if (found && floor_y + 0.01f >= K->bbox[2])
            break; /* 0x498629: dist <= n.y * (p.y - ymin) + 0.001 */
        int32_t l = K->link[2];
        if (l == INT32_MIN)
            break;
        c = l >= 0 ? cell_subtree(K, l, p) : ~l;
    }
    return C->nvisit;
}
/* 0x497fb0 (segment, stops in the cell where b lies) / 0x497b10 (endless, dir = b - a): from a's
 * cell (0x408180) to the exit face of each cell - the candidate faces are fixed in the first cell:
 * the ray goes that way (0x406ee0 < 0) and a lies inside that face (0x406e50 > 0); t_exit = the
 * smallest dist / -dir over them, the first one on a tie - through its link (a local subtree
 * descended at the exit point a + d t_exit, 0x40ab60, or ~link; INT_MIN = out of the world) into
 * the next cell. A cell's polygons count only up to its t_exit, so the walk ends in the cell that
 * holds the world hit (t_stop, a fraction of d; the port finds it with its own polygon query), or
 * for the segment in the cell with t_exit > 1. */
static float face_dir(Vec3 d, int f) {
    const float v[3] = {d.x, d.y, d.z};
    return (f & 1) ? -v[f >> 1] : v[f >> 1];
}
static float face_dist(const GelCell *K, Vec3 p, int f) {
    const float v[3] = {p.x, p.y, p.z};
    return (f & 1) ? K->bbox[f] - v[f >> 1] : v[f >> 1] - K->bbox[f];
}
uint32_t gel_walk_seg(const GelFile *g, Vec3 a, Vec3 b, float t_stop, int endless) {
    struct GelCol *C = g->col;
    if (!C)
        return 0;
    C->nvisit = 0;
    Vec3 d = {b.x - a.x, b.y - a.y, b.z - a.z};
    int32_t c = gel_cell(g, a);
    if (c < 0)
        return 0;
    int faces[6], nf = 0;
    for (int f = 0; f < 6; f++)
        if (face_dir(d, f) < 0 && face_dist(&g->cells[c], a, f) > 0)
            faces[nf++] = f; /* 0x4980a7..0x4980e9 */
    for (uint32_t guard = 0; guard <= g->ncells; guard++) {
        const GelCell *K = &g->cells[c];
        visit_push(C, c);
        int ef = -1;
        float te = 0;
        for (int i = 0; i < nf; i++) {
            float dist = face_dist(K, a, faces[i]);
            if (!(dist > 0))
                continue;
            float t = dist / -face_dir(d, faces[i]);
            if (ef < 0 || t < te) {
                ef = faces[i];
                te = t;
            }
        }
        if (ef < 0)
            break; /* no exit face: 0x498145 (raw 1) */
        if (t_stop <= te)
            break; /* the world hit lies in this cell */
        if (!endless && te > 1.0f)
            break; /* b lies in this cell (0x498136) */
        int32_t l = K->link[ef];
        if (l == INT32_MIN)
            break; /* out of the world (0x498417) */
        if (l >= 0) {
            Vec3 q = {a.x + d.x * te, a.y + d.y * te, a.z + d.z * te};
            c = cell_subtree(K, l, q);
        } else
            c = ~l;
        if (c < 0 || (uint32_t)c >= g->ncells)
            break;
    }
    return C->nvisit;
}
/* The instances of the visited cells, in the order the original tests them (0x407171..0x407301 for
 * 0x407000, the same loop in 0x4074ca, 0x498475, 0x497f01, 0x497a61): per visited cell its .col
 * refs in file order, then the dynamic list. A new stamp per query ([0x4c4c08]++); an instance is
 * skipped when it is out of the world (+0x1c == -1: hidden by message 6, or its cell point outside
 * every sector), already tested (+0x20 == stamp), non-collidable (+8 & 0x40), without press nodes
 * (S+0x58 == 0), or when its phase bit +0xd0 is not in the ref's mask; otherwise it is stamped
 * (0x432506) and tested. */
static int g_col_all; /* gel_col_force_all(): testing, the old every-instance selection */
void gel_col_force_all(int on) {
    g_col_all = on;
}
static void out_push(struct GelCol *C, Instance *in, uint32_t id) {
    if (C->nout >= C->out_cap) {
        C->out_cap = C->out_cap ? C->out_cap * 2 : 128;
        C->out = (GelColRef *)realloc(C->out, C->out_cap * sizeof *C->out);
    }
    C->out[C->nout].in = in;
    C->out[C->nout].id = id;
    C->nout++;
}
void (*gel_col_clock)(Instance *in);
static int col_candidate(const struct GelCol *C, Instance *in, uint32_t id) {
    uint32_t nc;
    if (!in || !in->visible || !in->node_world || (in->cell_ok && in->cell_sec < 0))
        return 0; /* +0x1c == -1 */
    if (in->col_stamp == C->igen || in->noncollide)
        return 0;
    ins_collision_nodes(in->model, &nc);
    if (!nc)
        return 0;
    if (gel_col_clock)
        gel_col_clock(in); /* 0x4324c9..0x4324dc: vt[2](1) unless clocked this frame; its re-cell
                              0x43f351 moves an unlisted instance to its animated root */
    return (ins_phase_mask(in) & id & 0xffff0000u) != 0;
}
uint32_t gel_col_instances(const GelFile *g, const InsFile *ins, const GelColRef **out) {
    struct GelCol *C = g->col;
    *out = NULL;
    if (!C || !ins)
        return 0;
    C->nout = 0;
    if (++C->igen == 0) { /* wrapped: clear every stamp */
        for (uint32_t mi = 0; mi < ins->nmodels; mi++)
            for (uint32_t k = 0; k < ins->models[mi].ninstances; k++)
                ins->models[mi].instances[k].col_stamp = 0;
        C->igen = 1;
    }
    static int all = -1;
    if (all < 0)
        all = wenv("WOODY_COLALL") != NULL; /* testing: the port's old selection (every instance) */
    if (!C->have || all ||
        g_col_all) { /* port fallback without a .col: every instance, model order */
        for (uint32_t mi = 0; mi < ins->nmodels; mi++)
            for (uint32_t k = 0; k < ins->models[mi].ninstances; k++) {
                Instance *in = &ins->models[mi].instances[k];
                if (col_candidate(C, in, 0xffff0000u)) {
                    in->col_stamp = C->igen;
                    out_push(C, in, 0xffff0000u | (in->index & 0xffff));
                }
            }
        *out = C->out;
        return C->nout;
    }
    for (uint32_t v = 0; v < C->nvisit; v++) {
        uint32_t cell = (uint32_t)C->visit[v];
        if (cell >= g->ncells)
            continue;
        for (uint32_t j = C->first[cell]; j < C->first[cell + 1]; j++) {
            uint32_t ref = C->refs[j], slot = ref & 0xffff;
            Instance *in = slot < ins->nslots ? ins->slots[slot] : NULL;
            if (!col_candidate(C, in, ref))
                continue;
            in->col_stamp = C->igen;
            out_push(C, in, ref);
        }
    }
    for (uint32_t i = 0; i < C->ndyn; i++) { /* 0x407267 / 0x4984de / 0x497f79 / 0x497ad9 */
        Instance *in = C->dyn[i];
        uint32_t id = 0xffff0000u | (in->index & 0xffff);
        if (!col_candidate(C, in, id))
            continue;
        in->col_stamp = C->igen;
        out_push(C, in, id);
    }
    *out = C->out;
    return C->nout;
}

/* ---------------------------------------------------------------- .vis */
/* Loader 0x408260: one record per sector, holding one or two lists of (sector, flag) pairs - the
 * sectors that can be seen from this one (docs/FORMAT_TEX_COL_VIS_LIT.md 3). */
int vis_load(VisFile *v, const char *path, uint32_t nsectors) {
    memset(v, 0, sizeof *v);
    if (!nsectors)
        return -1;
    v->data = read_file(path, &v->size);
    if (!v->data)
        return -1;
    Rd r = {v->data, 0, v->size, 0};
    v->nsectors = nsectors;
    v->sectors = (VisSector *)calloc(nsectors, sizeof(VisSector));
    uint32_t cap = nsectors * 2 + 8, n = 0;
    v->pool = (VisList *)calloc(cap, sizeof(VisList));
    for (uint32_t s = 0; s < nsectors && !r.err; s++) {
        uint32_t nlists = ru32(&r), total = ru32(&r), sum = 0;
        if (nlists > 1024) {
            r.err = 1;
            break;
        }
        if (n + nlists > cap) {
            cap = (n + nlists) * 2 + 8;
            v->pool = (VisList *)realloc(v->pool, (size_t)cap * sizeof(VisList));
        }
        v->sectors[s].nlists = nlists;
        v->sectors[s].first = n;
        for (uint32_t e = 0; e < nlists && !r.err; e++) {
            VisList *L = &v->pool[n++];
            L->id = ru32(&r);
            L->npairs = ru32(&r);
            if (8 * (size_t)L->npairs > r.size - r.pos) {
                r.err = 1;
                break;
            }
            L->pairs = (const uint32_t *)rraw(&r, 8 * (size_t)L->npairs);
            for (uint32_t k = 0; k < L->npairs; k++)
                if (L->pairs[2 * k] >= nsectors) {
                    r.err = 1;
                    break;
                }
            sum += L->npairs;
        }
        if (sum != total)
            r.err = 1;
    }
    if (r.err || r.pos != r.size) {
        fprintf(stderr, "%s: does not match the .gel (%u sectors), ignored\n", path, nsectors);
        vis_free(v);
        return -1;
    }
    /* How much this actually culls, and the one property a potentially visible set must have: a
     * sector sees itself. Nothing outside the loader reads these lists in the original
     * (docs/FORMAT_TEX_COL_VIS_LIT.md 3), so the reading of the pairs rests on the .gel loader;
     * print enough to see at a glance whether it holds up on a level. */
    {
        uint32_t mn = 0xffffffffu, mx = 0, self = 0;
        double sum = 0;
        for (uint32_t s = 0; s < nsectors; s++) {
            uint32_t n = 0, has = 0;
            for (uint32_t e = 0; e < v->sectors[s].nlists; e++) {
                const VisList *L = &v->pool[v->sectors[s].first + e];
                n += L->npairs;
                for (uint32_t k = 0; k < L->npairs; k++)
                    if (L->pairs[2 * k] == s)
                        has = 1;
            }
            sum += n;
            if (n < mn)
                mn = n;
            if (n > mx)
                mx = n;
            self += has;
        }
        printf(".vis: %u sectors see %u..%u others (avg %.0f), %u of %u list themselves\n",
               nsectors, mn, mx, sum / nsectors, self, nsectors);
    }
    return 0;
}
void vis_free(VisFile *v) {
    free(v->sectors);
    free(v->pool);
    free(v->data);
    memset(v, 0, sizeof *v);
}

/* ---------------------------------------------------------------- .ins */
static void read_trajectory(Rd *r, Trajectory *t) {
    memset(t, 0, sizeof *t);
    t->npoints = ru32(r);
    if (!t->npoints)
        return;
    t->points = (Vec3 *)calloc(t->npoints, sizeof(Vec3));
    for (uint32_t i = 0; i < t->npoints; i++) {
        ru32(r);
        t->points[i] = rvec3(r);
    }
    t->closed = ru32(r);
}

static TrackRef *read_refs(Rd *r, uint32_t n) {
    TrackRef *t = (TrackRef *)calloc(n ? n : 1, sizeof(TrackRef));
    for (uint32_t i = 0; i < n; i++) {
        t[i].off = ru32(r);
        t[i].cnt = ru32(r);
    }
    return t;
}

static int read_node(Rd *r, InsNode *n, uint32_t nanims) {
    memset(n, 0, sizeof *n);
    n->flags = ru32(r);
    n->kind = n->flags & 0xff;
    n->type_code = (n->flags >> 8) & 0xff;
    n->sub_index = n->flags >> 16;
    if (n->flags & 0x20)
        n->marker_value = rf32(r);
    else if (n->flags & 0x40) {
        n->light_intensity = rf32(r);
        n->light_colour = ru32(r);
        ru32(r);
    } else if (n->flags & 0x10) {
        n->helper_a = rf32(r);
        n->helper_b = rf32(r);
        n->helper_mode = ru32(r);
    } else
        n->npolys = ru32(r);
    n->npoints = ru32(r);
    n->pivot = rvec3(r);
    n->a = ru32(r);
    n->b = ru32(r);
    n->c = ru32(r);
    if (n->a + n->b + n->c) {
        size_t bytes = ((size_t)n->a + n->b + n->c / 4) * 4;
        n->pool = (uint8_t *)rraw(r, bytes);
        if (n->a)
            n->pos_refs = read_refs(r, nanims);
        if (n->b)
            n->rot_refs = read_refs(r, nanims);
        if (n->c)
            n->event_refs = read_refs(r, nanims);
    }
    n->first_child = ri32(r);
    n->next_sibling = ri32(r);
    n->parent = -1;
    if (n->first_child > 0)
        n->first_child -= 1; /* file: 1-based, -1 none */
    if (n->next_sibling > 0)
        n->next_sibling -= 1;
    return r->err;
}

static int read_model(Rd *r, Model *m) {
    memset(m, 0, sizeof *m);
    uint32_t B = ru32(r);
    m->nanims = ru32(r);
    ru32(r);
    ru32(r);
    m->nnodes = B ? B - 1 : 0;
    m->anims = (InsAnim *)calloc(m->nanims ? m->nanims : 1, sizeof(InsAnim));
    for (uint32_t i = 0; i < m->nanims; i++) {
        m->anims[i].nframes = ru32(r);
        m->anims[i].duration_4096 = ru32(r);
        m->anims[i].duration_s = m->anims[i].duration_4096 / 4096.0f;
    }
    m->first_top_node = ru32(r);
    m->bbox_node = ru32(r);
    uint32_t n;
    uint32_t *tmp;
    n = ru32(r);
    tmp = ru32s(r, n);
    free(tmp); /* marker nodes */
    n = ru32(r);
    tmp = ru32s(r, n);
    free(tmp); /* light nodes */
    m->nvolume_nodes = ru32(r);
    m->volume_nodes = ru32s(r, m->nvolume_nodes);
    uint32_t nhull = ru32(r);
    uint32_t *hull = ru32s(r, nhull);
    n = ru32(r);
    tmp = ru32s(r, n);
    free(tmp); /* helper nodes */
    uint32_t npress = ru32(r);
    m->ncollision_ids = ru32(r);
    uint32_t *press = ru32s(r, npress);
    m->nmesh_nodes = ru32(r);
    m->mesh_nodes = ru32s(r, m->nmesh_nodes);

    m->nodes = (InsNode *)calloc(m->nnodes ? m->nnodes : 1, sizeof(InsNode));
    for (uint32_t i = 0; i < m->nnodes; i++)
        if (read_node(r, &m->nodes[i], m->nanims))
            return -1;

    uint32_t nextra = nhull + ((nhull || m->nmesh_nodes || npress) ? 1 : 0);
    for (uint32_t i = 0; i < nextra; i++) {
        ru32(r);
        uint32_t cnt = ru32(r);
        r->pos += 4 * (size_t)cnt;
    }
    free(hull);
    free(press);

    m->npoints = ru32(r);
    m->points = (InsPoint *)calloc(m->npoints ? m->npoints : 1, sizeof(InsPoint));
    for (uint32_t i = 0; i < m->npoints; i++)
        m->points[i].pos = rvec3(r);
    for (uint32_t i = 0; i < m->npoints; i++)
        m->points[i].normal = rvec3(r);
    for (uint32_t i = 0; i < m->npoints; i++)
        m->points[i].colour = rvec3(r);

    ru32(r);
    ru32(r); /* poly / index totals */
    uint32_t base = 0;
    for (uint32_t j = 0; j < m->nnodes; j++) {
        InsNode *nd = &m->nodes[j];
        nd->point_base = base;
        if (!(nd->flags & 0x70)) {
            nd->polys = (InsPoly *)calloc(nd->npolys ? nd->npolys : 1, sizeof(InsPoly));
            for (uint32_t k = 0; k < nd->npolys; k++) {
                InsPoly *p = &nd->polys[k];
                p->material = ru32(r) & 0xffff;
                p->flags = ru32(r);
                p->nverts = ru32(r);
                p->indices = ru32s(r, p->nverts);
            }
        }
        for (uint32_t k = 0; k < nd->npoints && base + k < m->npoints;
             k++) { /* N+0x2c (0x427daf..0x427e3d): the bounding radius around the pivot */
            Vec3 q = m->points[base + k].pos;
            float dx = q.x - nd->pivot.x, dy = q.y - nd->pivot.y, dz = q.z - nd->pivot.z,
                  d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d > nd->radius)
                nd->radius = d;
        }
        base += nd->npoints;
        for (int32_t c = nd->first_child; c >= 0; c = m->nodes[c].next_sibling) {
            if ((uint32_t)c >= m->nnodes)
                return -1;
            m->nodes[c].parent = (int32_t)j;
        }
    }
    if (base != m->npoints) {
        fprintf(stderr, "point count mismatch\n");
        return -1;
    }

    m->ntris = ru32(r);
    m->tris = (InsTri *)calloc(m->ntris ? m->ntris : 1, sizeof(InsTri));
    for (uint32_t i = 0; i < m->ntris; i++) {
        InsTri *t = &m->tris[i];
        t->i2 = ru32(r);
        t->i1 = ru32(r);
        t->i0 = ru32(r);
        t->material = ru32(r) & 0xffff;
    }

    m->ninstances = ru32(r);
    m->instances = (Instance *)calloc(m->ninstances ? m->ninstances : 1, sizeof(Instance));
    uint32_t nids = m->nvolume_nodes + m->ncollision_ids;
    for (uint32_t i = 0; i < m->ninstances; i++) {
        Instance *in = &m->instances[i];
        in->model = m;
        in->unk0 = ru32(r);
        read_trajectory(r, &in->traj);
        in->position = rvec3(r);
        in->quat.x = rf32(r);
        in->quat.y = rf32(r);
        in->quat.z = rf32(r);
        in->quat.w = rf32(r);
        in->scale = rvec3(r);
        in->id = ru32(r);
        in->index = in->id & 0xffffff;
        in->nids = nids;
        in->ids = ru32s(r, nids);
        in->visible = 1;
        in->anim = 0;
        in->anim_speed = 1.0f;
        in->node_world = (Mat4 *)calloc(m->nnodes ? m->nnodes : 1, sizeof(Mat4));
        in->plight = (float *)calloc(m->nnodes ? m->nnodes : 1, 6 * sizeof(float));
        mat4_from_trs(&in->world, in->position, in->quat, in->scale);
    }
    return r->err;
}

int ins_load(InsFile *f, const char *path) {
    memset(f, 0, sizeof *f);
    f->data = read_file(path, &f->size);
    if (!f->data)
        return -1;
    Rd r = {f->data, 0, f->size, 0};
    f->nslots = ru32(&r);
    f->nmodels = ru32(&r);
    f->models = (Model *)calloc(f->nmodels ? f->nmodels : 1, sizeof(Model));
    for (uint32_t i = 0; i < f->nmodels; i++)
        if (read_model(&r, &f->models[i])) {
            fprintf(stderr, "%s: model %u parse error at %zu\n", path, i, r.pos);
            return -1;
        }
    f->ncameras = ru32(&r);
    f->cameras = (Camera *)calloc(f->ncameras ? f->ncameras : 1, sizeof(Camera));
    for (uint32_t i = 0; i < f->ncameras; i++) {
        Camera *c = &f->cameras[i];
        c->position = rvec3(&r);
        c->id = ru32(&r);
        c->index = c->id & 0xffffff;
        read_trajectory(&r, &c->traj);
    }
    ru32(&r);
    if (r.err || r.pos != r.size) {
        fprintf(stderr, "%s: parse error (pos %zu of %zu)\n", path, r.pos, r.size);
        return -1;
    }
    f->slots = (Instance **)calloc(f->nslots + 16, sizeof(Instance *));
    f->cam_slots = (Camera **)calloc(f->nslots + 16, sizeof(Camera *));
    for (uint32_t i = 0; i < f->nmodels; i++)
        for (uint32_t k = 0; k < f->models[i].ninstances; k++) {
            Instance *in = &f->models[i].instances[k];
            if (in->index < f->nslots + 16)
                f->slots[in->index] = in;
        }
    for (uint32_t i = 0; i < f->ncameras; i++)
        if (f->cameras[i].index < f->nslots + 16)
            f->cam_slots[f->cameras[i].index] = &f->cameras[i];
    return 0;
}

void lit_free(LitFile *l) {
    for (uint32_t i = 0; i < l->nlights; i++) {
        LitLight *L = &l->lights[i];
        for (uint32_t k = 0; k < L->nc && L->c; k++)
            free(L->c[k].indices);
        free(L->a);
        free(L->b);
        free(L->c);
        free(L->bsp);
        free(L->planes);
    }
    free(l->lights);
    free(l->extra);
    free(l->sectors);
    free(l->data);
    memset(l, 0, sizeof *l);
}

void ins_free(InsFile *f) {
    for (uint32_t i = 0; i < f->nmodels; i++) {
        Model *m = &f->models[i];
        for (uint32_t j = 0; j < m->nnodes; j++) {
            InsNode *n = &m->nodes[j];
            for (uint32_t k = 0; k < n->npolys && n->polys; k++)
                free(n->polys[k].indices);
            free(n->polys);
            free(n->pos_refs);
            free(n->rot_refs);
            free(n->event_refs);
        }
        for (uint32_t j = 0; j < m->ninstances; j++) {
            free(m->instances[j].ids);
            free(m->instances[j].traj.points);
            free(m->instances[j].node_world);
            free(m->instances[j].plight);
#ifdef WOODY_PSP
            free(m->instances[j].psp_light_target);
#endif
        }
        free(m->owner);
        free(m->helper);
        free(m->coll);
        free(m->nodes);
        free(m->anims);
        free(m->points);
        free(m->tris);
        free(m->instances);
        free(m->volume_nodes);
        free(m->mesh_nodes);
    }
    for (uint32_t i = 0; i < f->ncameras; i++)
        free(f->cameras[i].traj.points);
    free(f->models);
    free(f->cameras);
    free(f->slots);
    free(f->cam_slots);
    free(f->data);
    memset(f, 0, sizeof *f);
}

/* ---------------------------------------------------------------- math */
void mat4_identity(Mat4 *m) {
    memset(m->m, 0, sizeof m->m);
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}
void mat4_mul(Mat4 *out, const Mat4 *a, const Mat4 *b) {
    Mat4 r;
    /* Skeleton transforms are affine. Preserve the general 4x4 path for callers
     * with projection matrices, and preserve aliasing through the temporary. */
    if (a->m[3] == 0 && a->m[7] == 0 && a->m[11] == 0 && a->m[15] == 1 && b->m[3] == 0 &&
        b->m[7] == 0 && b->m[11] == 0 && b->m[15] == 1) {
        for (int c = 0; c < 4; c++) {
            for (int row = 0; row < 3; row++) {
                float v = a->m[row] * b->m[c * 4] + a->m[4 + row] * b->m[c * 4 + 1] +
                          a->m[8 + row] * b->m[c * 4 + 2];
                if (c == 3)
                    v += a->m[12 + row];
                r.m[c * 4 + row] = v;
            }
            r.m[c * 4 + 3] = c == 3 ? 1 : 0;
        }
        *out = r;
        return;
    }
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a->m[k * 4 + rr] * b->m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    *out = r;
}
void mat4_from_trs(Mat4 *m, Vec3 t, Quat q, Vec3 s) {
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n == 0)
        n = 1; /* instance quaternions have norm 2 */
    float x = q.x / n, y = q.y / n, z = q.z / n, w = q.w / n;
    float *o = m->m;
    o[0] = (1 - 2 * (y * y + z * z)) * s.x;
    o[1] = (2 * (x * y + z * w)) * s.x;
    o[2] = (2 * (x * z - y * w)) * s.x;
    o[3] = 0;
    o[4] = (2 * (x * y - z * w)) * s.y;
    o[5] = (1 - 2 * (x * x + z * z)) * s.y;
    o[6] = (2 * (y * z + x * w)) * s.y;
    o[7] = 0;
    o[8] = (2 * (x * z + y * w)) * s.z;
    o[9] = (2 * (y * z - x * w)) * s.z;
    o[10] = (1 - 2 * (x * x + y * y)) * s.z;
    o[11] = 0;
    o[12] = t.x;
    o[13] = t.y;
    o[14] = t.z;
    o[15] = 1;
}
Vec3 mat4_apply(const Mat4 *m, Vec3 v) {
    const float *o = m->m;
    Vec3 r;
    r.x = o[0] * v.x + o[4] * v.y + o[8] * v.z + o[12];
    r.y = o[1] * v.x + o[5] * v.y + o[9] * v.z + o[13];
    r.z = o[2] * v.x + o[6] * v.y + o[10] * v.z + o[14];
    return r;
}
void rgb565_to_rgba(const uint16_t *src, uint8_t *dst, uint32_t n, int colour_key) {
    for (uint32_t i = 0; i < n; i++) {
        uint16_t v = src[i];
        uint8_t r = (uint8_t)(((v >> 11) & 31) << 3), g = (uint8_t)(((v >> 5) & 63) << 2),
                b = (uint8_t)((v & 31) << 3);
        r |= r >> 5;
        g |= g >> 6;
        b |= b >> 5;
        int key = colour_key && (r & 0xF0) == 0xF0 && (b & 0xF0) == 0xF0 && (g & 0xF0) == 0;
        if (key)
            r = g = b = 0; /* 0x47fc1e: the magenta is thrown away, the texel becomes ARGB
                            * 0x00000000. Leaving the magenta in place gave every alpha edge a pink
                            * fringe once the filter mixed it with its opaque neighbours. */
        dst[4 * i] = r;
        dst[4 * i + 1] = g;
        dst[4 * i + 2] = b;
        dst[4 * i + 3] = key ? 0 : 255;
    }
}
void argb1555_to_rgb(uint32_t v, float rgb[3]) /* 0x43dc65..0x43dca8: c5 * 0.0313725 (0x4aa3e8 =
                                                  8/255), the low bits 0: 0xFFFF is 248/255 */
{
    rgb[0] = ((v >> 10) & 31) * 8 / 255.0f;
    rgb[1] = ((v >> 5) & 31) * 8 / 255.0f;
    rgb[2] = (v & 31) * 8 / 255.0f;
}

/* ---------------------------------------------------------------- .lit (loader 0x40ac30) */
int lit_load(LitFile *l, const char *path) {
    memset(l, 0, sizeof *l);
    l->data = read_file(path, &l->size);
    if (!l->data)
        return -1;
    Rd r = {l->data, 0, l->size, 0};
    if (ru32(&r) != 0x20010822) {
        free(l->data);
        memset(l, 0, sizeof *l);
        return -1;
    }
    l->nlights = ru32(&r);
    l->lights = (LitLight *)calloc(l->nlights ? l->nlights : 1, sizeof(LitLight));
    for (uint32_t i = 0; i < l->nlights && !r.err; i++) {
        LitLight *L = &l->lights[i];
        ru32(&r);
        ru32(&r);
        L->pos = rvec3(&r);
        for (int k = 0; k < 3; k++)
            L->colour[k] = rf32(&r);
        L->range = rf32(&r);
        L->na = ru32(&r);
        L->a = ru32s(&r, L->na);
        L->nb = ru32(&r);
        L->b = ru32s(&r, L->nb);
        L->nc = ru32(&r);
        ru32(&r);
        L->c = (LitPoly *)calloc(L->nc ? L->nc : 1, sizeof(LitPoly));
        for (uint32_t k = 0; k < L->nc && !r.err; k++) {
            LitPoly *p = &L->c[k];
            p->n = ru32(&r);
            for (int q = 0; q < 4; q++)
                p->plane[q] = rf32(&r);
            p->face = ru32(&r);
            if (p->n > 4096) {
                r.err = 1;
                p->n = 0;
                break;
            }
            p->indices = (int32_t *)ru32s(&r, p->n);
        }
        uint32_t nd = ru32(&r);
        r.pos += 16 * (size_t)nd;
        L->nbsp = ru32(&r);
        L->bsp = ru32s(&r, 3 * L->nbsp);
        L->nplanes = ru32(&r);
        L->planes = (float *)ru32s(&r, 4 * L->nplanes);
    }
    l->nextra = ru32(&r);
    l->extra = (Vec3 *)calloc(l->nextra ? l->nextra : 1, sizeof(Vec3));
    for (uint32_t i = 0; i < l->nextra && !r.err; i++) {
        l->extra[i] = rvec3(&r);
        ru32(&r);
    }
    if (r.err) {
        fprintf(stderr, "%s: truncated\n", path);
        l->nlights = 0;
        return -1;
    }
    /* trailer (0x43fd90): u32 dword count, then one {u32 n, u32 light[n]} per gel SECTOR ->
     * lightsys+0x10. It is what picks the light of an instance, so without it a character indoors
     * is lit by the sun outside. */
    if (r.pos + 4 <= r.size) {
        uint32_t total = ru32(&r);
        size_t end = r.pos + 4 * (size_t)total;
        if (end <= r.size) {
            uint32_t cap = total ? total : 1;
            l->sectors = (LitSector *)calloc(cap, sizeof(LitSector));
            while (r.pos + 4 <= end && l->nsectors < cap) {
                uint32_t n = ru32(&r);
                if (r.pos + 4 * (size_t)n > end)
                    break;
                l->sectors[l->nsectors].n = n;
                l->sectors[l->nsectors].idx = (const uint32_t *)(l->data + r.pos);
                r.pos += 4 * (size_t)n;
                l->nsectors++;
            }
        }
    }
    return 0;
}
/* 0x40b540: walk the light's shadow BSP down to a leaf. -1 = no leaf face, i.e. the point is in the
 * open part of the light's volume; otherwise the index of the face that may shadow it - the caller
 * decides on which side the point is. */
int32_t lit_bsp_face(const LitLight *l, const GelFile *g, Vec3 p) {
    if (!l->nbsp)
        return -1;
    uint32_t i = 0;
    for (int guard = 0; guard < 4096; guard++) {
        if (i >= l->nbsp)
            return -1;
        const uint32_t *nd = &l->bsp[3 * i];
        if (nd[0] >= l->nplanes)
            return -1;
        const float *pl = &l->planes[4 * nd[0]];
        uint32_t v = (pl[0] * p.x + pl[1] * p.y + pl[2] * p.z + pl[3]) > 0.0f ? nd[1] : nd[2];
        if ((v & 0xF) == 0) {
            i = v >> 4;
            continue;
        }
        if ((v & 0xF) != 1)
            return -1;
        uint32_t f = v >> 4;
        return f < g->npolys ? (int32_t)f : -1;
    }
    return -1;
}
int lit_point_lit(const LitLight *l, const GelFile *g, Vec3 p) {
    int32_t f = lit_bsp_face(l, g, p);
    if (f < 0)
        return 1; /* 0x43ba3d, 0x42f211: lit means no leaf face ... */
    const float *fp = g->polys[f].plane;
    return fp[0] * p.x + fp[1] * p.y + fp[2] * p.z + fp[3] >
           0.0f; /* ... or in front of the leaf face */
}

/* ---------------------------------------------------------------- pose evaluation (0x43a3a0) */
static int track_pos(const InsNode *n, int anim, float t, Vec3 *out) {
    if (!n->pos_refs || !n->pos_refs[anim].cnt)
        return 0;
    const float *f = (const float *)(n->pool + (size_t)n->pos_refs[anim].off * 4);
    uint32_t cnt = n->pos_refs[anim].cnt;
    if (t <= f[0] || cnt == 1) {
        out->x = f[1];
        out->y = f[2];
        out->z = f[3];
        return 1;
    }
    const float *last = f + 4 * (cnt - 1);
    if (t >= last[0]) {
        out->x = last[1];
        out->y = last[2];
        out->z = last[3];
        return 1;
    }
    uint32_t i = 1;
    while (i < cnt && f[4 * i] < t)
        i++;
    const float *a = f + 4 * (i - 1), *b = f + 4 * i;
    float u = (b[0] > a[0]) ? (t - a[0]) / (b[0] - a[0]) : 0;
    out->x = a[1] + (b[1] - a[1]) * u;
    out->y = a[2] + (b[2] - a[2]) * u;
    out->z = a[3] + (b[3] - a[3]) * u;
    return 1;
}
static int track_rot(const InsNode *n, int anim, float t, Quat *out) {
    if (!n->rot_refs || !n->rot_refs[anim].cnt || n->kind == 0x80)
        return 0;
    const float *f = (const float *)(n->pool + ((size_t)n->a + n->rot_refs[anim].off) * 4);
    uint32_t cnt = n->rot_refs[anim].cnt;
    const float *a, *b;
    float u;
    if (t <= f[0] || cnt == 1) {
        a = b = f;
        u = 0;
    } else if (t >= f[5 * (cnt - 1)]) {
        a = b = f + 5 * (cnt - 1);
        u = 0;
    } else {
        uint32_t i = 1;
        while (i < cnt && f[5 * i] < t)
            i++;
        a = f + 5 * (i - 1);
        b = f + 5 * i;
        u = (b[0] > a[0]) ? (t - a[0]) / (b[0] - a[0]) : 0;
    }
    /* 0x440a80: a true slerp, k0 = sin((1 - u)O) / sin O, k1 = sin(uO) / sin O with O = acos(a.b)
     * (0x49a7e0), plain lerp when a.b >= 0.9999 (0x4aa41c); no shortest-path sign flip, so a pair
     * with a.b < 0 turns the long way round. (The port used a shortest-path nlerp before: on the
     * W3C drop-away platforms, model 10 anim 4, one key pair turns 179 degrees, and the nlerp
     * turned it up to 8 degrees late, a third slower at the start, so Woody was carried off it too
     * slowly; docs/PERSO_MOVE.md 6.6) */
    float c = a[1] * b[1] + a[2] * b[2] + a[3] * b[3] + a[4] * b[4], k0 = 1.0f - u, k1 = u;
    if (c < 0.9999f) {
        float O = acosf(c < -1.0f ? -1.0f : c), s = sinf(O);
        if (s != 0) {
            k0 = sinf((1.0f - u) * O) / s;
            k1 = sinf(u * O) / s;
        } /* port guard: an exact sin O = 0 would divide by zero */
    }
    out->x = k0 * a[1] + k1 * b[1];
    out->y = k0 * a[2] + k1 * b[2];
    out->z = k0 * a[3] + k1 * b[3];
    out->w = k0 * a[4] + k1 * b[4];
    return 1;
}

static Vec3 track_pos_cut(const InsNode *n, int anim, float frame) /* 0x43a660 */
{
    Vec3 out = {0, 0, 0};
    const float *f = (const float *)(n->pool + (size_t)n->pos_refs[anim].off * 4);
    uint32_t cnt = n->pos_refs[anim].cnt, i = 0;
    while (i < cnt && f[4 * i] < frame)
        i++;
    if (i > 0 && i < cnt &&
        (int)(f[4 * i] - f[4 * (i - 1)]) ==
            1) { /* keys one frame apart and far apart: a camera cut, do not interpolate */
        Vec3 p0, p1;
        track_pos(n, anim, floorf(frame), &p0);
        track_pos(n, anim, floorf(frame) + 1.0f, &p1);
        float dx = p1.x - p0.x, dy = p1.y - p0.y, dz = p1.z - p0.z;
        if (dx * dx + dy * dy + dz * dz > 40000.0f)
            return p0;
    }
    track_pos(n, anim, frame, &out);
    return out;
}
int ins_camera_eval(const Instance *inst, int anim, float phase, Vec3 *eye, Vec3 *target) {
    const Model *m = inst->model;
    const InsNode *cn = NULL, *tn = NULL;
    if (anim < 0 || (uint32_t)anim >= m->nanims)
        return 0;
    for (uint32_t i = 0; i < m->nnodes; i++) {
        const InsNode *n = &m->nodes[i];
        if (n->parent >= 0)
            continue;
        if (n->flags == 0x80)
            cn = n;
        else if (n->flags == 0x180)
            tn = n;
    }
    if (!cn || !tn || !cn->pos_refs || !cn->pos_refs[anim].cnt || !tn->pos_refs ||
        !tn->pos_refs[anim].cnt)
        return 0;
    float frame = (float)m->anims[anim].nframes * phase;
    *eye = mat4_apply(&inst->world, track_pos_cut(cn, anim, frame));
    *target = mat4_apply(&inst->world, track_pos_cut(tn, anim, frame));
    return 1;
}
int ins_root_end(const Instance *inst, int anim, Vec3 *pos, Vec3 *forward) {
    return ins_root_at(inst, anim, 1.0f, 0, pos, forward);
}
int ins_root_at(const Instance *inst, int anim, float phase, int base, Vec3 *pos, Vec3 *forward) {
    const Model *m = inst->model;
    const InsNode *root = NULL;
    if (anim < 0 || (uint32_t)anim >= m->nanims || base < 0 || (uint32_t)base >= m->nanims)
        return 0;
    for (uint32_t i = 0; i < m->nnodes && !root; i++)
        if (m->nodes[i].parent < 0 && m->nodes[i].flags == 0)
            root = &m->nodes[i];
    if (!root)
        return 0;
    Mat4 A, B, WA;
    Vec3 p = {0, 0, 0}, one = {1, 1, 1};
    Quat q = {0, 0, 0, 1};
    float fr = (float)m->anims[anim].nframes * phase;
    track_pos(root, anim, fr, &p);
    if (track_rot(root, anim, fr, &q)) {
        q.x = -q.x;
        q.y = -q.y;
        q.z = -q.z;
    }
    mat4_from_trs(&A, p, q, one);
    p = (Vec3){0, 0, 0};
    q = (Quat){0, 0, 0, 1};
    track_pos(root, base, 0, &p);
    if (track_rot(root, base, 0, &q)) {
        q.x = -q.x;
        q.y = -q.y;
        q.z = -q.z;
    }
    mat4_from_trs(&B, p, q, one);
    mat4_mul(&WA, &inst->world, &A);
    /* B is rigid: B^-1 v = R^T (v - t) */
    Vec3 v[2] = {{0, 0, 0}, {0, -1, 0}}, w[2]; /* origin and the model's forward (-y) */
    for (int k = 0; k < 2; k++) {
        Vec3 d = {v[k].x - B.m[12], v[k].y - B.m[13], v[k].z - B.m[14]};
        Vec3 l = {B.m[0] * d.x + B.m[1] * d.y + B.m[2] * d.z,
                  B.m[4] * d.x + B.m[5] * d.y + B.m[6] * d.z,
                  B.m[8] * d.x + B.m[9] * d.y + B.m[10] * d.z};
        w[k] = mat4_apply(&WA, l);
    }
    *pos = w[0];
    *forward = (Vec3){w[1].x - w[0].x, 0, w[1].z - w[0].z};
    return 1;
}

static void pose_rec(Instance *inst, int32_t node, const Mat4 *parent, int anim, float tf) {
    Model *m = inst->model;
    for (int32_t i = node; i >= 0; i = m->nodes[i].next_sibling) {
        InsNode *n = &m->nodes[i];
        Mat4 local;
        Vec3 p = {0, 0, 0};
        Quat q = {0, 0, 0, 1};
        Vec3 one = {1, 1, 1};
        int hp = track_pos(n, anim, tf, &p), hr = track_rot(n, anim, tf, &q);
        if (hp && !hr) {
            inst->node_world[i] = *parent;
            Vec3 t = mat4_apply(parent, p);
            inst->node_world[i].m[12] = t.x;
            inst->node_world[i].m[13] = t.y;
            inst->node_world[i].m[14] = t.z;
        } else if (hp || hr) {
            q.x = -q.x;
            q.y = -q.y;
            q.z = -q.z; /* 0x440370 conjugates; the .ins loader pre-negates instance quaternions
                           (0x428758) but not track keys */
            mat4_from_trs(&local, p, q, one);
            mat4_mul(&inst->node_world[i], parent, &local);
        } else
            inst->node_world[i] = *parent; /* 0x43a50a: no tracks -> copy parent */
        if (n->kind != 0x80 && n->first_child >= 0)
            pose_rec(inst, n->first_child, &inst->node_world[i], anim, tf);
    }
}

void ins_pose(Instance *inst, int anim, float t) {
    Model *m = inst->model;
    if (!m->nnodes)
        return;
    float tf = 0;
    if (m->nanims && anim >= 0 && (uint32_t)anim < m->nanims) {
        InsAnim *a = &m->anims[anim];
        if (!a->pose_kind) {
            a->pose_kind = 1;
            for (uint32_t i = 0; i < m->nnodes; i++) {
                const InsNode *n = &m->nodes[i];
                if ((n->pos_refs && n->pos_refs[anim].cnt > 1) ||
                    (n->rot_refs && n->kind != 0x80 && n->rot_refs[anim].cnt > 1)) {
                    a->pose_kind = 2;
                    break;
                }
            }
        }
        if (a->pose_kind == 2) {
            float dur = a->duration_s > 0 ? a->duration_s : 1.0f;
            float phase = fmodf(t / dur, 1.0f);
            if (phase < 0)
                phase += 1.0f;
            tf = phase * (float)a->nframes;
        }
    } else
        anim = 0;
    /* A stationary crate does not need to rehearse its skeleton thirty times a second.
     * Moving platforms still invalidate this through their world matrix or animation frame. */
    if (inst->pose_valid && inst->pose_anim == anim && inst->pose_frame == tf &&
        !memcmp(&inst->pose_world, &inst->world, sizeof(Mat4)))
        return;
    int32_t top = m->first_top_node ? (int32_t)m->first_top_node - 1 : 0;
    if ((uint32_t)top >= m->nnodes)
        top = 0;
    /* nodes not reached from the top chain keep the instance matrix */
    for (uint32_t i = 0; i < m->nnodes; i++)
        inst->node_world[i] = inst->world;
    pose_rec(inst, top, &inst->world, anim, tf);
    inst->pose_world = inst->world;
    inst->pose_anim = anim;
    inst->pose_frame = tf;
    inst->pose_valid = 1;
    inst->pose_revision++;
}

/* Which node owns a point. Walking the node list per point (as this did) is O(nodes) on every
 * single vertex, and the collision code asks it for every vertex of every hull polygon it looks at;
 * the answer is fixed, so build it once per model. The lowest node index that claims a point wins,
 * exactly as the walk did. */
int ins_point_owner(const Model *m, uint32_t pi) {
    if (!m->owner) {
        Model *mm = (Model *)m;
        mm->owner = (int32_t *)malloc((size_t)(m->npoints ? m->npoints : 1) * 4);
        for (uint32_t i = 0; i < (m->npoints ? m->npoints : 1); i++)
            mm->owner[i] = -1;
        for (uint32_t n = 0; n < m->nnodes; n++)
            for (uint32_t k = 0; k < m->nodes[n].npoints; k++) {
                uint32_t q = m->nodes[n].point_base + k;
                if (q < m->npoints && mm->owner[q] < 0)
                    mm->owner[q] = (int32_t)n;
            }
    }
    return pi < m->npoints ? (int)m->owner[pi] : -1;
}
/* The nodes the collision code has to consider at all: the press nodes (flags 0x01, list
 * S+0x58/0x5c). Every instance collision test of the original walks that list and only that one -
 * floor vt[7] 0x432480, cylinder vt[8] 0x433140, sphere vt[9] 0x433ff0 and the ray 0x4359b0. The
 * hull nodes (flags 0x04, S+0x38) are the visible meshes of characters and props (Woody is 43 of
 * them and no press node) and never collide. Models without any press node are then dropped in one
 * test instead of walking their whole node list on every ground query. */
const uint32_t *ins_collision_nodes(const Model *m, uint32_t *count) {
    if (!m->coll_ok) {
        Model *mm = (Model *)m;
        for (uint32_t i = 0; i < m->nnodes; i++)
            if (m->nodes[i].kind == 1 && m->nodes[i].polys)
                mm->ncoll++;
        if (mm->ncoll) {
            mm->coll = (uint32_t *)malloc((size_t)mm->ncoll * 4);
            mm->ncoll = 0;
            for (uint32_t i = 0; i < m->nnodes; i++)
                if (m->nodes[i].kind == 1 && m->nodes[i].polys)
                    mm->coll[mm->ncoll++] = i;
        }
        mm->coll_ok = 1;
    }
    *count = m->ncoll;
    return m->coll;
}

/* World aabb of one node: its own points are fixed in the node's space, so the box is built once
 * and only has to be transformed by the node's current matrix (the absolute-value trick, three dot
 * products per axis). */
int ins_node_world_box(const Instance *inst, uint32_t ni, float out[6]) {
    const Model *m = inst->model;
    if (ni >= m->nnodes || !inst->node_world)
        return 0;
    InsNode *n = &((Model *)m)->nodes[ni];
    if (!n->box_state) {
        n->box_state = n->npoints ? 1 : -1;
        float *b = n->box;
        b[0] = b[2] = b[4] = 1e30f;
        b[1] = b[3] = b[5] = -1e30f;
        for (uint32_t k = 0; k < n->npoints; k++) {
            uint32_t q = n->point_base + k;
            /* ins_point_world() puts a point in the space of the node that owns it; if that is not
             * this node the box would be in the wrong space, so give up on it for this node. */
            if (q >= m->npoints || ins_point_owner(m, q) != (int)ni) {
                n->box_state = -1;
                break;
            }
            Vec3 p = m->points[q].pos;
            p.x -= n->pivot.x;
            p.y -= n->pivot.y;
            p.z -= n->pivot.z;
            const float v[3] = {p.x, p.y, p.z};
            for (int a = 0; a < 3; a++) {
                if (v[a] < b[a * 2])
                    b[a * 2] = v[a];
                if (v[a] > b[a * 2 + 1])
                    b[a * 2 + 1] = v[a];
            }
        }
    }
    if (n->box_state < 0)
        return 0;
    const float *M = inst->node_world[ni].m;
    float c[3], e[3];
    for (int a = 0; a < 3; a++) {
        c[a] = (n->box[a * 2] + n->box[a * 2 + 1]) * 0.5f;
        e[a] = (n->box[a * 2 + 1] - n->box[a * 2]) * 0.5f;
    }
    for (int a = 0; a < 3;
         a++) { /* row a of the matrix (column major: element (a, j) = M[j*4+a]) */
        float wc = M[a] * c[0] + M[4 + a] * c[1] + M[8 + a] * c[2] + M[12 + a];
        float we = fabsf(M[a]) * e[0] + fabsf(M[4 + a]) * e[1] + fabsf(M[8 + a]) * e[2];
        out[a * 2] = wc - we;
        out[a * 2 + 1] = wc + we;
    }
    return 1;
}

Vec3 ins_point_world(const Instance *inst, uint32_t pi) {
    const Model *m = inst->model;
    int o = ins_point_owner(m, pi);
    Vec3 p = m->points[pi].pos;
    if (o < 0)
        return mat4_apply(&inst->world, p);
    Vec3 piv = m->nodes[o].pivot;
    p.x -= piv.x;
    p.y -= piv.y;
    p.z -= piv.z;
    return mat4_apply(&inst->node_world[o], p);
}

/* inst+0x60, written by the clock 0x43f2f1 after every pose: start at the model's first top-level
 * node (S+0x6c, 1-based), step to the next sibling (N+0x80) as long as the node is a 0x80 dummy
 * (Woody's camera nodes), and take the translation of that node's world matrix. In practice this is
 * node 0, the skeleton root. The renderer measures the outline distance from it (0x43b447) and the
 * light choice reads it (0x42e3e4 via the cell 0x4077f0), so a cinematic that walks a model far
 * away from its .ins position is judged where it is, not where it was placed. */
Vec3 ins_anim_centre(const Instance *inst) {
    const Model *m = inst->model;
    Vec3 p = {inst->world.m[12], inst->world.m[13], inst->world.m[14]};
    if (!inst->node_world || !m->nnodes)
        return p;
    int32_t i = m->first_top_node ? (int32_t)m->first_top_node - 1 : 0;
    for (uint32_t hop = 0;
         i >= 0 && (uint32_t)i < m->nnodes && m->nodes[i].kind == 0x80 && hop < m->nnodes; hop++)
        i = m->nodes[i].next_sibling;
    if (i < 0 || (uint32_t)i >= m->nnodes)
        return p;
    p.x = inst->node_world[i].m[12];
    p.y = inst->node_world[i].m[13];
    p.z = inst->node_world[i].m[14];
    return p;
}

/* getenv with a cache. The UCRT getenv scans the whole environment, and the WOODY_* test switches
 * are read in the frame loop, per instance and per collision polygon - dozens to hundreds of scans
 * a frame. The environment does not change while the engine runs, so the first call per name reads
 * it and later calls return that. Keyed by the address of the name (every caller passes a literal);
 * main thread only. */
const char *wenv(const char *name) {
    static struct {
        const char *k, *v;
    } t[1024];
    static unsigned n;
    unsigned h = (unsigned)(((uintptr_t)name >> 2) * 2654435761u) & 1023;
    for (unsigned i = 0; i < 1024; i++, h = (h + 1) & 1023) {
        if (t[h].k == name)
            return t[h].v;
        if (!t[h].k) {
            if (n >= 768)
                break;
            t[h].k = name;
            t[h].v = getenv(name);
            n++;
            return t[h].v;
        }
    }
    return getenv(name);
}
