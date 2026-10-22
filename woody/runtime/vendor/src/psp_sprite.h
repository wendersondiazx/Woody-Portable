#ifndef WOODY_PSP_SPRITE_H
#define WOODY_PSP_SPRITE_H
#include "psp_clip.h"
#include <stdint.h>
typedef struct {
    float u, v;
    uint32_t color;
    float x, y, z;
} WoodySpriteVertex;
static inline unsigned woody_sprite_quad(const float pos[4][3], const float uv[4][2],
                                         const float color[4][4], const float planes[5][4],
                                         WoodySpriteVertex output[36]) {
    WoodyClipVertex vertices[4];
    unsigned masks[4];
    for (unsigned i = 0; i < 4; i++) {
        memcpy(vertices[i].a, pos[i], 3 * sizeof(float));
        memcpy(vertices[i].a + 3, uv[i], 2 * sizeof(float));
        for (unsigned q = 0; q < 4; q++) {
            float v = color[i][q];
            if (v < 0)
                v = 0;
            if (v > 1)
                v = 1;
            vertices[i].a[5 + q] = v;
        }
        masks[i] = woody_clip_mask(&vertices[i], planes);
    }
    static const unsigned corners[2][3] = {{0, 1, 2}, {0, 2, 3}};
    unsigned used = 0;
    for (unsigned t = 0; t < 2; t++) {
        WoodyClipVertex in[3], out[9];
        unsigned any = 0, all = 31;
        for (unsigned q = 0; q < 3; q++) {
            unsigned i = corners[t][q];
            in[q] = vertices[i];
            any |= masks[i];
            all &= masks[i];
        }
        if (all)
            continue;
        unsigned n = 3;
        if (any)
            n = woody_clip_triangle(in, planes, out);
        const WoodyClipVertex *poly = any ? out : in;
        for (unsigned i = 2; i < n; i++) {
            unsigned fan[3] = {0, i - 1, i};
            for (unsigned j = 0; j < 3; j++) {
                const float *v = poly[fan[j]].a;
                WoodySpriteVertex *dst = &output[used++];
                dst->x = v[0];
                dst->y = v[1];
                dst->z = v[2];
                dst->u = v[3];
                dst->v = v[4];
                dst->color = 0;
                for (unsigned q = 0; q < 4; q++) {
                    float c = v[5 + q];
                    if (c < 0)
                        c = 0;
                    if (c > 1)
                        c = 1;
                    dst->color |= (uint32_t)(c * 255.0f) << (8 * q);
                }
            }
        }
    }
    return used;
}
#endif
