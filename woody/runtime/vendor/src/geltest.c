/* geltest.c - self-contained test of the kd-tree queries in level.c (docs/FORMAT_GEL.md 5-7) and of
 * the .vis loader (docs/FORMAT_TEX_COL_VIS_LIT.md 3). It needs no game data: it builds a random kd
 * subdivision with random triangles, writes it out as a .gel, reads it back and checks the queries
 * against brute force.
 *
 *   python -m ziglang cc -std=c99 -O2 -o out/geltest.exe src/level.c src/geltest.c &&
 * ./out/geltest.exe [seed]
 *
 * What it proves: the leaf a point lands in and the sector over it (0x408180 / 0x4081c0) agree with
 * the cell boxes; a box query returns every polygon whose box meets it, without duplicates; a
 * segment query returns everything the cells along the segment list; a sector box query returns
 * every sector the box reaches. Those are exactly the guarantees the collision code and the
 * renderer rely on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "level.h"

#define WMIN -1000.0f
#define WMAX 1000.0f
#define NLEAF 200 /* upper bound on the leaves the random split may produce */
#define NPOLY 900
#define MAXNODE 4096
#define MAXSECT 64

static unsigned rs = 12345;
static unsigned rnd(void) {
    rs = rs * 1664525u + 1013904223u;
    return rs >> 8;
}
static float rndf(float a, float b) {
    return a + (b - a) * (float)(rnd() % 100000) / 100000.0f;
}

/* ---- the reference tree, kept next to the file so the queries can be checked against it ---- */
typedef struct {
    int axis;
    float split;
    int le, gt;
    float bbox[6];
    int leaf;
    int sector;
} Node;
static Node nd[MAXNODE];
static int nnode, ncell, nsect;
static int sector_of_leaf[MAXNODE];
static float pv[NPOLY][3][3], pbox[NPOLY][6];

static int build(float bbox[6], int depth, int *nextleaf) {
    int i = nnode++;
    memcpy(nd[i].bbox, bbox, 24);
    nd[i].sector = -1;
    if (depth >= 8 || *nextleaf >= NLEAF || (depth > 3 && rnd() % 4 == 0)) {
        nd[i].leaf = 1;
        nd[i].le = (*nextleaf)++;
        return i;
    }
    int ax = (int)(rnd() % 3);
    float lo = bbox[ax * 2], hi = bbox[ax * 2 + 1];
    nd[i].axis = ax;
    nd[i].split = rndf(lo + (hi - lo) * 0.25f, lo + (hi - lo) * 0.75f);
    float b2[6];
    memcpy(b2, bbox, 24);
    b2[ax * 2 + 1] = nd[i].split;
    int le = build(b2, depth + 1, nextleaf);
    memcpy(b2, bbox, 24);
    b2[ax * 2] = nd[i].split;
    int gt = build(b2, depth + 1, nextleaf);
    nd[i].le = le;
    nd[i].gt = gt;
    return i;
}
static void mark_sectors(int i, int depth) /* every node at depth 2 is the root of one sector */
{
    if (nd[i].leaf)
        return;
    if (depth == 2) {
        if (nsect < MAXSECT)
            nd[i].sector = nsect++;
        return;
    }
    mark_sectors(nd[i].le, depth + 1);
    mark_sectors(nd[i].gt, depth + 1);
}
static void leaf_sectors(int i, int sec) {
    if (nd[i].sector >= 0)
        sec = nd[i].sector;
    if (nd[i].leaf) {
        sector_of_leaf[nd[i].le] = sec;
        return;
    }
    leaf_sectors(nd[i].le, sec);
    leaf_sectors(nd[i].gt, sec);
}
static int poly_meets(int p, const float *bb) {
    return pbox[p][0] <= bb[1] && pbox[p][1] >= bb[0] && pbox[p][2] <= bb[3] &&
           pbox[p][3] >= bb[2] && pbox[p][4] <= bb[5] && pbox[p][5] >= bb[4];
}

/* ---- file writer ---- */
static unsigned char *buf;
static size_t bp;
static void w32(unsigned v) {
    memcpy(buf + bp, &v, 4);
    bp += 4;
}
static void wf(float v) {
    memcpy(buf + bp, &v, 4);
    bp += 4;
}
static void wcell(const float *bb, const unsigned char *mark) {
    int n = 0;
    for (int p = 0; p < NPOLY; p++)
        n += mark[p] != 0;
    w32((unsigned)n);
    for (int p = 0; p < NPOLY; p++)
        if (mark[p])
            w32((unsigned)p);
    for (int k = 0; k < 6; k++)
        wf(bb[k]);
    for (int k = 0; k < 6; k++)
        w32(0x80000000u); /* neighbour links: none, the port descends from the root */
    w32(0);               /* no local neighbour subtrees */
}
static void dump(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    if (fwrite(buf, 1, bp, f) != bp) {
        perror(path);
        fclose(f);
        exit(1);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc > 1)
        rs = (unsigned)atoi(argv[1]);
    buf = (unsigned char *)malloc(16 << 20);
    float world[6] = {WMIN, WMAX, WMIN, WMAX, WMIN, WMAX};
    int nextleaf = 0;
    build(world, 0, &nextleaf);
    ncell = nextleaf;
    mark_sectors(0, 0);
    leaf_sectors(0, -1);
    for (int p = 0; p < NPOLY; p++) {
        float cx = rndf(WMIN, WMAX), cy = rndf(WMIN, WMAX), cz = rndf(WMIN, WMAX), s = rndf(5, 120);
        for (int v = 0; v < 3; v++)
            for (int k = 0; k < 3; k++)
                pv[p][v][k] = (k == 0 ? cx : k == 1 ? cy : cz) + rndf(-s, s);
        for (int k = 0; k < 3; k++) {
            pbox[p][k * 2] = pbox[p][k * 2 + 1] = pv[p][0][k];
            for (int v = 1; v < 3; v++) {
                if (pv[p][v][k] < pbox[p][k * 2])
                    pbox[p][k * 2] = pv[p][v][k];
                if (pv[p][v][k] > pbox[p][k * 2 + 1])
                    pbox[p][k * 2 + 1] = pv[p][v][k];
            }
        }
    }
    /* [1] polygons, three vertices each (vertex 3i+k) */
    w32(NPOLY);
    w32(NPOLY * 3);
    for (int p = 0; p < NPOLY; p++) {
        w32(3);
        w32(0);
        float ux = pv[p][1][0] - pv[p][0][0], uy = pv[p][1][1] - pv[p][0][1],
              uz = pv[p][1][2] - pv[p][0][2];
        float vx = pv[p][2][0] - pv[p][0][0], vy = pv[p][2][1] - pv[p][0][1],
              vz = pv[p][2][2] - pv[p][0][2];
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx,
              l = sqrtf(nx * nx + ny * ny + nz * nz);
        if (l < 1e-6f)
            l = 1;
        nx /= l;
        ny /= l;
        nz /= l;
        wf(nx);
        wf(ny);
        wf(nz);
        wf(-(nx * pv[p][0][0] + ny * pv[p][0][1] + nz * pv[p][0][2]));
        for (int v = 0; v < 3; v++)
            w32((unsigned)(p * 3 + v));
    }
    w32(0);
    w32(0); /* [2] no portals */
    w32(1);
    w32(NPOLY);
    w32(0);         /* [3] one group over everything, empty portal list */
    w32(NPOLY * 3); /* [4] vertices */
    for (int p = 0; p < NPOLY; p++)
        for (int v = 0; v < 3; v++) {
            wf(pv[p][v][0]);
            wf(pv[p][v][1]);
            wf(pv[p][v][2]);
            w32(0x808080);
        }
    int *cellnode = (int *)calloc((size_t)ncell, sizeof(int));
    for (int i = 0; i < nnode; i++)
        if (nd[i].leaf)
            cellnode[nd[i].le] = i;
    unsigned char *mark = (unsigned char *)malloc(NPOLY);
    w32((unsigned)ncell); /* [5] leaf cells */
    for (int c = 0; c < ncell; c++) {
        const float *bb = nd[cellnode[c]].bbox;
        for (int p = 0; p < NPOLY; p++)
            mark[p] = (unsigned char)poly_meets(p, bb);
        wcell(bb, mark);
    }
    { /* [6] the tree itself, depth first, so the child indices are absolute in this array */
        int order[MAXNODE], at[MAXNODE], on = 0, stack[MAXNODE], sp = 0;
        stack[sp++] = 0;
        while (sp) {
            int i = stack[--sp];
            if (nd[i].leaf)
                continue;
            at[i] = on;
            order[on++] = i;
            stack[sp++] = nd[i].gt;
            stack[sp++] = nd[i].le;
        }
        w32((unsigned)on);
        for (int k = 0; k < on; k++) {
            int i = order[k], le = nd[i].le, gt = nd[i].gt;
            w32(((unsigned)nd[i].sector << 16) | (unsigned)(nd[i].axis & 0xffff));
            wf(-nd[i].split);
            w32(nd[le].leaf ? (unsigned)~nd[le].le : (unsigned)at[le]);
            w32(nd[gt].leaf ? (unsigned)~nd[gt].le : (unsigned)at[gt]);
        }
    }
    w32((unsigned)nsect); /* [7] sectors: the union of the leaves under each root */
    for (int s = 0; s < nsect; s++) {
        float bb[6] = {1e30f, -1e30f, 1e30f, -1e30f, 1e30f, -1e30f};
        memset(mark, 0, NPOLY);
        for (int c = 0; c < ncell; c++)
            if (sector_of_leaf[c] == s) {
                const float *cb = nd[cellnode[c]].bbox;
                for (int k = 0; k < 3; k++) {
                    if (cb[k * 2] < bb[k * 2])
                        bb[k * 2] = cb[k * 2];
                    if (cb[k * 2 + 1] > bb[k * 2 + 1])
                        bb[k * 2 + 1] = cb[k * 2 + 1];
                }
                for (int p = 0; p < NPOLY; p++)
                    if (poly_meets(p, cb))
                        mark[p] = 1;
            }
        wcell(bb, mark);
    }
    dump("out/synth.gel");
    printf("synthetic level: %d kd nodes, %d leaf cells, %d sectors, %zu bytes\n", nnode, ncell,
           nsect, bp);

    GelFile g;
    if (gel_load(&g, "out/synth.gel")) {
        printf("FAIL: gel_load\n");
        return 1;
    }
    if (g.npolys != NPOLY || (int)g.ncells != ncell || (int)g.nsectors != nsect) {
        printf("FAIL: counts after load\n");
        return 1;
    }
    printf("loaded: %u polys, %u cells, %u sectors, %u groups, %u loose faces\n", g.npolys,
           g.ncells, g.nsectors, g.ngroups, g.nloose);

    int bad = 0;
    unsigned char *in = (unsigned char *)calloc(NPOLY, 1);
    for (int t = 0; t < 20000 && !bad; t++) { /* 1. the leaf and the sector of a point */
        Vec3 p = {rndf(WMIN, WMAX), rndf(WMIN, WMAX), rndf(WMIN, WMAX)};
        int32_t c = gel_cell(&g, p);
        if (c < 0 || c >= ncell) {
            printf("FAIL: gel_cell -> %d\n", c);
            bad++;
            break;
        }
        const float *bb = g.cells[c].bbox;
        if (p.x < bb[0] - 1 || p.x > bb[1] + 1 || p.y < bb[2] - 1 || p.y > bb[3] + 1 ||
            p.z < bb[4] - 1 || p.z > bb[5] + 1) {
            printf("FAIL: gel_cell gave a cell the point is not in\n");
            bad++;
            break;
        }
        if (gel_sector(&g, p) != sector_of_leaf[c]) {
            printf("FAIL: gel_sector disagrees with the tree\n");
            bad++;
            break;
        }
    }
    for (int t = 0; t < 2000 && !bad;
         t++) { /* 2. box query: every polygon that meets the box, once */
        float c[3] = {rndf(WMIN, WMAX), rndf(WMIN, WMAX), rndf(WMIN, WMAX)}, e = rndf(1, 300);
        float box[6] = {c[0] - e, c[0] + e, c[1] - e, c[1] + e, c[2] - e, c[2] + e};
        GelPolySet ps = gel_polys_in_box(&g, box);
        memset(in, 0, NPOLY);
        for (uint32_t k = 0; k < ps.n && !bad; k++) {
            if (in[ps.polys[k]]) {
                printf("FAIL: box query returned a polygon twice\n");
                bad++;
            }
            in[ps.polys[k]] = 1;
        }
        for (int p = 0; p < NPOLY && !bad; p++)
            if (poly_meets(p, box) && !in[p]) {
                printf("FAIL: box query missed polygon %d\n", p);
                bad++;
            }
    }
    for (int t = 0; t < 2000 && !bad;
         t++) { /* 3. segment query: everything the cells along it list */
        Vec3 a = {rndf(WMIN, WMAX), rndf(WMIN, WMAX), rndf(WMIN, WMAX)},
             b = {rndf(WMIN, WMAX), rndf(WMIN, WMAX), rndf(WMIN, WMAX)};
        GelPolySet ps = gel_polys_on_seg(&g, a, b);
        memset(in, 0, NPOLY);
        for (uint32_t k = 0; k < ps.n; k++)
            in[ps.polys[k]] = 1;
        for (int q = 0; q <= 2000 && !bad; q++) {
            float u = q / 2000.0f;
            Vec3 m = {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u};
            int32_t c = gel_cell(&g, m);
            if (c < 0)
                continue;
            for (uint32_t k = 0; k < g.cells[c].npolys; k++)
                if (!in[g.cells[c].polys[k]]) {
                    printf("FAIL: segment query missed polygon %u of cell %d\n",
                           g.cells[c].polys[k], c);
                    bad++;
                    break;
                }
        }
    }
    for (int t = 0; t < 2000 && !bad; t++) { /* 4. sector box query: every sector the box reaches */
        float c[3] = {rndf(WMIN, WMAX), rndf(WMIN, WMAX), rndf(WMIN, WMAX)}, e = rndf(1, 400);
        float box[6] = {c[0] - e, c[0] + e, c[1] - e, c[1] + e, c[2] - e, c[2] + e};
        int32_t out[MAXSECT];
        uint32_t n = gel_sectors_in_box(&g, box, out, MAXSECT);
        unsigned char got[MAXSECT] = {0};
        for (uint32_t k = 0; k < n && !bad; k++) {
            if (got[out[k]]) {
                printf("FAIL: sector query returned a sector twice\n");
                bad++;
            }
            got[out[k]] = 1;
        }
        for (int q = 0; q < 400 && !bad; q++) {
            Vec3 m = {rndf(box[0], box[1]), rndf(box[2], box[3]), rndf(box[4], box[5])};
            int32_t s = gel_sector(&g, m);
            if (s >= 0 && !got[s]) {
                printf("FAIL: sector query missed sector %d\n", s);
                bad++;
            }
        }
    }
    { /* 5. a .vis over the same sectors, written and read back */
        unsigned want[MAXSECT][2 * MAXSECT];
        int wn[MAXSECT];
        bp = 0;
        for (int s = 0; s < nsect; s++) {
            unsigned lists[2][MAXSECT];
            int ln[2] = {0, 0}, nl = 1 + (s & 1), total = 0; /* the data has one or two lists */
            wn[s] = 0;
            for (int e = 0; e < nl; e++)
                for (int t = 0; t < nsect; t++)
                    if (rnd() % 3 || t == s) {
                        lists[e][ln[e]++] = (unsigned)t;
                        want[s][wn[s]++] = (unsigned)t;
                    }
            for (int e = 0; e < nl; e++)
                total += ln[e];
            w32((unsigned)nl);
            w32((unsigned)total);
            for (int e = 0; e < nl; e++) {
                w32((unsigned)e);
                w32((unsigned)ln[e]);
                for (int t = 0; t < ln[e]; t++) {
                    w32(lists[e][t]);
                    w32((unsigned)(t & 1));
                }
            }
        }
        dump("out/synth.vis");
        VisFile v;
        if (vis_load(&v, "out/synth.vis", (unsigned)nsect)) {
            printf("FAIL: vis_load rejected a well formed file\n");
            bad++;
        } else {
            for (int s = 0; s < nsect; s++) {
                int n = 0;
                for (uint32_t e = 0; e < v.sectors[s].nlists; e++) {
                    const VisList *L = &v.pool[v.sectors[s].first + e];
                    for (uint32_t k = 0; k < L->npairs; k++, n++)
                        if (n >= wn[s] || L->pairs[2 * k] != want[s][n]) {
                            printf("FAIL: .vis pair %d of sector %d\n", n, s);
                            bad++;
                            break;
                        }
                }
                if (n != wn[s]) {
                    printf("FAIL: sector %d has %d pairs, expected %d\n", s, n, wn[s]);
                    bad++;
                }
            }
            vis_free(&v);
        }
        {
            FILE *f = fopen("out/trunc.vis", "wb");
            fwrite(buf, 1, bp - 4, f);
            fclose(f);
        } /* a short file must be refused */
        {
            VisFile v2;
            if (vis_load(&v2, "out/trunc.vis", (unsigned)nsect) == 0) {
                printf("FAIL: truncated .vis accepted\n");
                bad++;
                vis_free(&v2);
            }
        }
    }
    gel_free(&g);
    free(in);
    free(mark);
    free(cellnode);
    free(buf);
    printf(bad ? "FAILED (%d)\n" : "all kd / .vis checks passed\n", bad);
    return bad != 0;
}
