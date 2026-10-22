/* CPU clipping before the GE's finite screen-coordinate window. Attributes
 * are world XYZ, UV and RGBA; intersections must interpolate all of them. */
#ifndef WOODY_PSP_CLIP_H
#define WOODY_PSP_CLIP_H
#include <string.h>
typedef struct {
    float a[9];
} WoodyClipVertex;
static inline float woody_clip_distance(const WoodyClipVertex *v, const float p[4]) {
    return v->a[0] * p[0] + v->a[1] * p[1] + v->a[2] * p[2] + p[3];
}
static inline unsigned woody_clip_mask_subset(const WoodyClipVertex *v, const float planes[5][4],
                                              unsigned active) {
    unsigned mask = 0;
    for (unsigned i = 0; i < 5; i++)
        if ((active & (1u << i)) && woody_clip_distance(v, planes[i]) < 0)
            mask |= 1u << i;
    return mask;
}
static inline unsigned woody_clip_mask(const WoodyClipVertex *v, const float planes[5][4]) {
    return woody_clip_mask_subset(v, planes, 31);
}
/* -1: wholly outside, 0: wholly inside, otherwise intersecting plane bits. */
static inline int woody_clip_box(const float bounds[6], const float planes[5][4]) {
    unsigned mask = 0;
    for (unsigned p = 0; p < 5; p++) {
        float lo = planes[p][3], hi = lo;
        for (unsigned c = 0; c < 3; c++) {
            float n = planes[p][c];
            lo += n * bounds[c + (n < 0 ? 3 : 0)];
            hi += n * bounds[c + (n < 0 ? 0 : 3)];
        }
        if (hi < 0)
            return -1;
        if (lo < 0)
            mask |= 1u << p;
    }
    return (int)mask;
}
static inline unsigned woody_clip_triangle(const WoodyClipVertex in[3], const float planes[5][4],
                                           WoodyClipVertex out[9]) {
    WoodyClipVertex work[2][9];
    memcpy(work[0], in, 3 * sizeof *in);
    unsigned n = 3, side = 0;
    for (unsigned p = 0; p < 5 && n; p++) {
        WoodyClipVertex *src = work[side], *dst = work[side ^ 1];
        unsigned count = 0;
        for (unsigned i = 0; i < n; i++) {
            const WoodyClipVertex *a = &src[(i + n - 1) % n], *b = &src[i];
            float da = woody_clip_distance(a, planes[p]), db = woody_clip_distance(b, planes[p]);
            if ((da < 0) != (db < 0)) {
                float t = da / (da - db);
                for (unsigned c = 0; c < 9; c++)
                    dst[count].a[c] = a->a[c] + t * (b->a[c] - a->a[c]);
                count++;
            }
            if (db >= 0)
                dst[count++] = *b;
        }
        n = count;
        side ^= 1;
    }
    memcpy(out, work[side], n * sizeof *out);
    return n;
}
#endif
