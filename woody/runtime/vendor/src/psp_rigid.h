/* Rigid mesh cache for PSP. Included after the shared colour/UV helpers.
 * CPU clipping remains the fallback for any node crossing a guard plane.
 * PSPGL copies and pins buffer data; CPU cache memory is never GE scratch. */
struct PspRigidVertex {
    float u, v;
    uint32_t color;
    float x, y, z;
};
struct PspRigidRun {
    uint32_t material, first, count;
};
struct PspRigidCache {
    struct PspRigidCache *next;
    unsigned node, count, nruns;
    GLuint vertices, indices;
    struct PspRigidVertex *data;
    uint32_t *point, *material, *poly_first;
    GLushort *visible;
    struct PspRigidRun *runs;
    float bounds[6], color_key[11], camera[3];
    int color_valid, camera_valid, x2;
    size_t bytes;
};
static size_t psp_rigid_bytes;
static unsigned psp_rigid_draws, psp_rigid_fallbacks;
#define PSP_RIGID_BUDGET ((getenv("WOODY_LOW_MEMORY") ? 1u : 6u) * 1024u * 1024u)
#ifndef PSP_RIGID_MIN_VERTICES
#define PSP_RIGID_MIN_VERTICES 96u
#endif
static void psp_rigid_destroy(struct PspRigidCache *c) {
    glDeleteBuffersARB(1, &c->vertices);
    glDeleteBuffersARB(1, &c->indices);
    free(c->data);
    free(c->point);
    free(c->material);
    free(c->poly_first);
    free(c->visible);
    free(c->runs);
    psp_rigid_bytes -= c->bytes;
    free(c);
}
static void psp_rigid_free(Renderer *r) {
    if (!r->ins)
        return;
    for (unsigned mi = 0; mi < r->ins->nmodels; mi++)
        for (unsigned ii = 0; ii < r->ins->models[mi].ninstances; ii++) {
            Instance *in = &r->ins->models[mi].instances[ii];
            struct PspRigidCache *c = in->psp_rigid_cache;
            while (c) {
                struct PspRigidCache *next = c->next;
                psp_rigid_destroy(c);
                c = next;
            }
            in->psp_rigid_cache = NULL;
        }
}
static struct PspRigidCache *psp_rigid_build(const Renderer *r, Instance *inst, unsigned ni) {
    Model *m = inst->model;
    InsNode *n = &m->nodes[ni];
    unsigned count = 0;
    for (unsigned k = 0; k < n->npolys; k++) {
        InsPoly *p = &n->polys[k];
        if (p->nverts < 3)
            continue;
        if (!(p->material & 0x8000)) {
            if (p->material >= r->tex->nmaterials)
                return NULL;
            unsigned group = r->tex->materials[p->material].group;
            if (group >= r->tex->ngroups || (r->tex->groups[group].flags & 2))
                return NULL;
        }
        if (p->nverts > 21847 || count > 65535 - (p->nverts - 2) * 3)
            return NULL;
        count += (p->nverts - 2) * 3;
    }
    if (!count || count < PSP_RIGID_MIN_VERTICES)
        return NULL;
    /* CPU data + GL vertex copy + CPU point/index maps + GL index copy. */
    size_t bytes = sizeof(struct PspRigidCache) + count * (2 * sizeof(struct PspRigidVertex) + 12) +
                   (n->npolys + 1) * sizeof(uint32_t) + n->npolys * sizeof(struct PspRigidRun);
    if (bytes > PSP_RIGID_BUDGET - psp_rigid_bytes)
        return NULL;
    struct PspRigidCache *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->bytes = bytes;
    psp_rigid_bytes += bytes;
    c->data = malloc(count * sizeof *c->data);
    c->point = malloc(count * sizeof *c->point);
    c->material = malloc(count * sizeof *c->material);
    c->visible = malloc(count * sizeof *c->visible);
    c->poly_first = malloc((n->npolys + 1) * sizeof *c->poly_first);
    c->runs = calloc(n->npolys, sizeof *c->runs);
    if (!c->data || !c->point || !c->material || !c->visible || !c->poly_first || !c->runs) {
        psp_rigid_destroy(c);
        return NULL;
    }
    c->node = ni;
    c->count = count;
    unsigned used = 0;
    for (unsigned k = 0; k < n->npolys; k++) {
        InsPoly *p = &n->polys[k];
        c->poly_first[k] = used;
        for (unsigned t = 2; t < p->nverts; t++) {
            unsigned corners[3] = {0, t - 1, t};
            const Material *mat = (p->material & 0x8000) ? NULL : &r->tex->materials[p->material];
            for (unsigned j = 0; j < 3; j++) {
                unsigned point = p->indices[corners[j]];
                Vec3 v = m->points[point].pos;
                v.x -= n->pivot.x;
                v.y -= n->pivot.y;
                v.z -= n->pivot.z;
                struct PspRigidVertex *dst = &c->data[used];
                dst->x = v.x;
                dst->y = v.y;
                dst->z = v.z;
                dst->u = dst->v = 0;
                if (mat)
                    material_uv(mat, v.x, v.y, v.z, &dst->u, &dst->v);
                c->point[used] = point;
                c->material[used] = p->material;
                float pos[3] = {v.x, v.y, v.z};
                for (unsigned q = 0; q < 3; q++) {
                    if (!used || pos[q] < c->bounds[q])
                        c->bounds[q] = pos[q];
                    if (!used || pos[q] > c->bounds[q + 3])
                        c->bounds[q + 3] = pos[q];
                }
                used++;
            }
        }
    }
    c->poly_first[n->npolys] = used;
    glGenBuffersARB(1, &c->vertices);
    glGenBuffersARB(1, &c->indices);
    c->next = inst->psp_rigid_cache;
    inst->psp_rigid_cache = c;
    return c;
}
static int psp_rigid_draw(const Renderer *r, Instance *inst, unsigned ni, int pass, unsigned frame,
                          int helper) {
    if (pass || frame || helper >= 0 || g_fading || g_fade_alpha != 1.0f || inst_fading(inst) ||
        inst->uv_mode || g_idyn_n || wenv("WOODY_NORIGIDCACHE"))
        return 0;
    struct PspRigidCache *c = inst->psp_rigid_cache;
    while (c && c->node != ni)
        c = c->next;
    if (!c && !(c = psp_rigid_build(r, inst, ni)))
        return 0;
    const Mat4 *world = &inst->node_world[ni];
    float bounds[6];
    for (unsigned i = 0; i < 8; i++) {
        Vec3 p = {c->bounds[(i & 1) ? 3 : 0], c->bounds[(i & 2) ? 4 : 1],
                  c->bounds[(i & 4) ? 5 : 2]};
        p = mat4_apply(world, p);
        float v[3] = {p.x, p.y, p.z};
        for (unsigned q = 0; q < 3; q++) {
            if (!i || v[q] < bounds[q])
                bounds[q] = v[q];
            if (!i || v[q] > bounds[q + 3])
                bounds[q + 3] = v[q];
        }
    }
    int box = woody_clip_box(bounds, psp_clip_planes);
    if (box < 0)
        return 1;
    if (box) {
        psp_rigid_fallbacks++;
        return 0;
    }
    /* A state change must not affect triangles already collected by the CPU. */
    bt_flush();
    float key[11] = {0};
    if (inst->plight)
        memcpy(key, inst->plight + ni * 6, 6 * sizeof(float));
    key[6] = inst->tint_scale;
    key[7] = (float)(inst->tint_red | (inst->tint_mode << 1));
    memcpy(key + 8, inst->tint_rgb, 3 * sizeof(float));
    /* show_light is stable during normal play; keep it part of validity. */
    key[6] += r->show_light ? 0 : 4096;
    if (!c->color_valid || memcmp(key, c->color_key, sizeof key)) {
        struct ModelVertexCache *cache = model_cache_begin(inst->model->npoints);
        const float base[3] = {1, 1, 1};
        c->x2 = 0;
        g_mat_blended = 0;
        for (unsigned i = 0; i < c->count; i++) {
            unsigned point = c->point[i];
            if (c->material[i] & 0x8000)
                continue;
            cached_lit_colour(cache, point, r, inst, world, (int)ni, &inst->model->points[point],
                              base, 1);
            if (g_bt.col[0] > 1 || g_bt.col[1] > 1 || g_bt.col[2] > 1)
                c->x2 = 1;
        }
        /* Cache allocation failure still has a correct, bounded fallback. */
        if (!cache)
            return 0;
        for (unsigned i = 0; i < c->count; i++) {
            int textured = !(c->material[i] & 0x8000);
            if (!textured) {
                float flat[3];
                argb1555_to_rgb(c->material[i], flat);
                cached_lit_colour(NULL, c->point[i], r, inst, world, (int)ni,
                                  &inst->model->points[c->point[i]], flat, 0);
            }
            const float *col = textured ? cache[c->point[i]].col : g_bt.col;
            uint32_t color = 0xff000000;
            for (unsigned q = 0; q < 3; q++) {
                float v = col[q] * ((textured && c->x2) ? 0.5f : 1.0f);
                if (v < 0)
                    v = 0;
                if (v > 1)
                    v = 1;
                color |= (uint32_t)(v * 255.0f) << (q * 8);
            }
            c->data[i].color = color;
        }
        glBindBufferARB(GL_ARRAY_BUFFER_ARB, c->vertices);
        glBufferDataARB(GL_ARRAY_BUFFER_ARB, c->count * sizeof *c->data, c->data,
                        GL_STREAM_DRAW_ARB);
        glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
        memcpy(c->color_key, key, sizeof key);
        c->color_valid = 1;
    }
    Vec3 camera = {0, 0, 0};
    int inverse = affine_inv_apply(world, g_cam_pos, &camera);
    float ck[3] = {camera.x, camera.y, camera.z};
    if (!inverse)
        ck[0] = ck[1] = ck[2] = 0;
    if (!c->camera_valid || memcmp(ck, c->camera, sizeof ck)) {
        InsNode *node = &inst->model->nodes[ni];
        unsigned used = 0;
        c->nruns = 0;
        for (unsigned k = 0; k < node->npolys; k++) {
            InsPoly *poly = &node->polys[k];
            if (poly->nverts < 3)
                continue;
            const float *plane = poly_plane(inst->model, node, poly);
            if (inverse && !(poly->flags & 2) &&
                plane[0] * camera.x + plane[1] * camera.y + plane[2] * camera.z + plane[3] <= 0)
                continue;
            struct PspRigidRun *run = c->nruns ? &c->runs[c->nruns - 1] : NULL;
            unsigned group =
                (poly->material & 0x8000) ? UINT32_MAX : r->tex->materials[poly->material].group;
            unsigned previous =
                run ? ((run->material & 0x8000) ? UINT32_MAX
                                                : r->tex->materials[run->material].group)
                    : 0;
            if (!run || previous != group) {
                run = &c->runs[c->nruns++];
                run->material = poly->material;
                run->first = used;
                run->count = 0;
            }
            for (unsigned i = c->poly_first[k]; i < c->poly_first[k + 1]; i++) {
                c->visible[used++] = (GLushort)i;
                run->count++;
            }
        }
        glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, c->indices);
        glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB, used * sizeof *c->visible, c->visible,
                        GL_STREAM_DRAW_ARB);
        glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
        memcpy(c->camera, ck, sizeof ck);
        c->camera_valid = 1;
    }
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glMultMatrixf(world->m);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, c->vertices);
    glTexCoordPointer(2, GL_FLOAT, 24, (const void *)0);
    glColorPointer(4, GL_UNSIGNED_BYTE, 24, (const void *)8);
    glVertexPointer(3, GL_FLOAT, 24, (const void *)12);
    glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, c->indices);
    for (unsigned k = 0; k < c->nruns; k++) {
        const Material *unused;
        set_material(r, c->runs[k].material, &unused, 0, inst);
        glTexEnvf(GL_TEXTURE_ENV, 0x8573, c->x2 ? 2.0f : 1.0f);
        glDrawElements(GL_TRIANGLES, c->runs[k].count, GL_UNSIGNED_SHORT,
                       (const void *)(size_t)(c->runs[k].first * sizeof(GLushort)));
        psp_rigid_draws++;
    }
    glTexEnvf(GL_TEXTURE_ENV, 0x8573, 1.0f);
    glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glPopMatrix();
    return 1;
}
