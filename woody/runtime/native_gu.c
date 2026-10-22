/* Project-local fixed-function frontend. Only PSPSDK GU/kernel/display APIs.
 * It implements the subset used by Woody, not a general OpenGL driver. */
#ifndef WOODY_GL_IMPLEMENTATION
#define WOODY_GL_IMPLEMENTATION
#endif
#include "gl_compat.h"
#include <pspgu.h>
#include <pspge.h>
#include <pspkernel.h>
#include <pspdisplay.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    float u, v;
    unsigned color;
    float x, y, z;
} Vertex;
typedef struct {
    void *data;
    size_t size;
} Buffer;
typedef struct {
    void *data;
    int w, h, stride;
} Image;
typedef struct {
    Image image[8];
    unsigned *palette;
    int format, levels, maxlevel, min, mag, wrap, used, world, skip;
} Texture;
typedef struct {
    const void *ptr;
    unsigned buffer;
    int stride, size;
    GLenum type;
    int on;
} Array;
static unsigned list[65536] __attribute__((aligned(64)));
static unsigned char *arena;
static size_t arena_capacity;
static size_t arena_used;
static Buffer buffers[8192];
static Texture textures[4096];
static unsigned bound_array, bound_index, bound_texture;
static Array position, texcoord, colour;
static void *retired[16384];
static unsigned retired_count;
static size_t retired_bytes;
static GLenum error;
static unsigned current_colour = 0xffffffff;
static float current_u, current_v;
static Vertex immediate[8192];
static unsigned immediate_count;
static GLenum immediate_mode;
static int active, initialized, tex_dirty = 1, env_mode = GL_MODULATE;
static float matrices[3][32][16];
static unsigned stack[3], matrix_mode;
static unsigned char states[GU_MAX_STATUS];
static int polygon_enabled;
static float polygon_units;
static void *draw_buffer;
static int render_width = 480, render_height = 272, fragment2x, depth_write = 1;
static int viewport[4] = {0, 0, 480, 272}, scissor[4] = {0, 0, 480, 272};
static int resolved;

static void identity(float *m) {
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1;
}
static void submit_matrix(void) {
    sceGuSetMatrix(matrix_mode == 1   ? GU_PROJECTION
                   : matrix_mode == 2 ? GU_TEXTURE
                                      : GU_MODEL,
                   (const ScePspFMatrix4 *)matrices[matrix_mode][stack[matrix_mode]]);
}
static void reclaim(void) {
    for (unsigned i = 0; i < retired_count; i++)
        free(retired[i]);
    retired_count = 0;
    retired_bytes = 0;
    arena_used = 0;
}
static void sync_list(void) {
    if (!active)
        return;
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    active = 0;
    reclaim();
}
static void start_list(void) {
    if (!active) {
        sceGuStart(GU_DIRECT, list);
        active = 1;
    }
}
static void ensure(void) {
    start_list();
    if (sceGuCheckList() > 220000 || retired_bytes > 2 * 1024 * 1024) {
        sync_list();
        start_list();
    }
}
static void *temporary(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (n > arena_capacity) {
        error = GL_OUT_OF_MEMORY;
        return NULL;
    }
    if (arena_used + n > arena_capacity) {
        sync_list();
        start_list();
    }
    void *p = arena + arena_used;
    arena_used += n;
    return p;
}
static void retire(void *p, size_t n) {
    if (!p)
        return;
    if (!active) {
        free(p);
        return;
    }
    if (retired_count == 16384)
        sync_list();
    if (!active) {
        free(p);
        return;
    }
    retired[retired_count++] = p;
    retired_bytes += n;
}
int woody_gu_open(void) {
    arena_capacity = getenv("WOODY_LOW_MEMORY") ? 1024 * 1024 : 2 * 1024 * 1024;
    arena = memalign(64, arena_capacity);
    if (!arena)
        return -1;
    const char *width = getenv("WOODY_RENDER_WIDTH");
    int requested = width ? atoi(width) : 480;
    if (requested >= 240 && requested <= 480 && requested % 8 == 0) {
        render_width = requested;
        render_height = requested * 272 / 480;
    }
    sceGuInit();
    start_list();
    draw_buffer = (void *)0;
    sceGuDrawBuffer(GU_PSM_5650, draw_buffer, 512);
    sceGuDispBuffer(480, 272, (void *)(512 * 272 * 2), 512);
    sceGuDepthBuffer((void *)(512 * 272 * 4), 512);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuDepthRange(65535, 0);
    sceGuClearDepth(0);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
    states[GU_SCISSOR_TEST] = 1;
    sceGuEnable(GU_CLIP_PLANES);
    states[GU_CLIP_PLANES] = 1;
    sceGuFrontFace(GU_CCW);
    sceGuShadeModel(GU_SMOOTH);
    float unit[16];
    identity(unit);
    sceGuSetMatrix(GU_VIEW, (ScePspFMatrix4 *)unit);
    identity(matrices[0][0]);
    identity(matrices[1][0]);
    identity(matrices[2][0]);
    sceGuSetMatrix(GU_MODEL, (ScePspFMatrix4 *)unit);
    sceGuSetMatrix(GU_PROJECTION, (ScePspFMatrix4 *)unit);
    sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
    sceGuTexOffset(0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(GU_FALSE);
    sync_list();
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    initialized = 1;
    start_list();
    return 0;
}
static void upscale(void) {
    if (render_width == 480 || resolved)
        return;
    ensure();
    unsigned char *vram = sceGeEdramGetAddr();
    void *scratch = vram + 512 * 272 * 6;
    sceGuCopyImage(GU_PSM_5650, 0, 0, render_width, render_height, 512,
                   vram + (uintptr_t)draw_buffer, 0, 0, 512, scratch);
    sceGuTexSync();
    for (int i = 0; i < GU_MAX_STATUS; i++)
        sceGuDisable(i);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuScissor(0, 0, 480, 272);
    sceGuDepthMask(GU_TRUE);
    sceGuTexMode(GU_PSM_5650, 0, 0, 0);
    sceGuTexImage(0, 512, 512, 512, scratch);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1, 1);
    sceGuTexOffset(0, 0);
    struct ScreenVertex {
        unsigned short u, v;
        short x, y, z;
    };
    struct ScreenVertex *v = temporary(2 * sizeof *v);
    if (v) {
        v[0] = (struct ScreenVertex){0, 0, 0, 0, 0};
        v[1] = (struct ScreenVertex){render_width, render_height, 480, 272, 0};
        sceKernelDcacheWritebackRange(v, 2 * sizeof *v);
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL,
                       v);
    }
    for (int i = 0; i < GU_MAX_STATUS; i++)
        if (states[i])
            sceGuEnable(i);
        else
            sceGuDisable(i);
    if (fragment2x)
        sceGuEnable(GU_FRAGMENT_2X);
    sceGuDepthMask(!depth_write);
    sceGuTexFunc(env_mode == GL_REPLACE ? GU_TFX_REPLACE : GU_TFX_MODULATE, GU_TCC_RGBA);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    tex_dirty = 1;
    resolved = 1;
}
void woody_gu_swap(void) {
    upscale();
    sync_list();
    draw_buffer = sceGuSwapBuffers();
    start_list();
    resolved = 0;
    sceGuDrawBufferList(GU_PSM_5650, draw_buffer, 512);
}
void woody_gu_close(void) {
    sync_list();
    sceGuDisplay(GU_FALSE);
    sceGuTerm();
    initialized = 0;
    free(arena);
    arena = NULL;
}
void glFinish(void) {
    if (initialized) {
        sync_list();
        start_list();
    }
}
GLenum glGetError(void) {
    GLenum e = error;
    error = 0;
    return e;
}
const GLubyte *glGetString(GLenum key) {
    if (key == GL_EXTENSIONS)
        return (const GLubyte *)"GL_ARB_vertex_buffer_object GL_EXT_paletted_texture";
    if (key == GL_VERSION)
        return (const GLubyte *)"1.1 Woody native PSPSDK GU";
    return (const GLubyte *)"PSPSDK sceGu";
}
static unsigned byte(float x) {
    return x <= 0 ? 0 : x >= 1 ? 255 : (unsigned)(x * 255 + 0.5f);
}
void glColor4f(float r, float g, float b, float a) {
    current_colour = byte(r) | (byte(g) << 8) | (byte(b) << 16) | (byte(a) << 24);
    sceGuColor(current_colour);
}
void glColor3f(float r, float g, float b) {
    glColor4f(r, g, b, 1);
}
void glColor4fv(const float *p) {
    glColor4f(p[0], p[1], p[2], p[3]);
}
void glClearColor(float r, float g, float b, float a) {
    sceGuClearColor(byte(r) | (byte(g) << 8) | (byte(b) << 16) | (byte(a) << 24));
}
void glClear(GLbitfield mask) {
    ensure();
    sceGuClear((mask & GL_COLOR_BUFFER_BIT ? GU_COLOR_BUFFER_BIT : 0) |
               (mask & GL_DEPTH_BUFFER_BIT ? GU_DEPTH_BUFFER_BIT : 0));
}
void glClearStencil(int value) {
    (void)value;
}
void glDepthMask(GLboolean on) {
    depth_write = on;
    sceGuDepthMask(!on);
}
static int compare(GLenum f) {
    switch (f) {
    case GL_NEVER:
        return GU_NEVER;
    case GL_ALWAYS:
        return GU_ALWAYS;
    case GL_LESS:
        return GU_LESS;
    case GL_LEQUAL:
        return GU_LEQUAL;
    case GL_EQUAL:
        return GU_EQUAL;
    case GL_GREATER:
        return GU_GREATER;
    case GL_GEQUAL:
        return GU_GEQUAL;
    default:
        return GU_NOTEQUAL;
    }
}
void woody_gl_depth_func(GLenum f) {
    if (f == GL_LESS)
        f = GL_GREATER;
    else if (f == GL_LEQUAL)
        f = GL_GEQUAL;
    else if (f == GL_GREATER)
        f = GL_LESS;
    else if (f == GL_GEQUAL)
        f = GL_LEQUAL;
    sceGuDepthFunc(compare(f));
}
void glAlphaFunc(GLenum f, float ref) {
    sceGuAlphaFunc(compare(f), byte(ref), 255);
}
static int blend(GLenum f) {
    switch (f) {
    case GL_SRC_ALPHA:
        return GU_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA:
        return GU_ONE_MINUS_SRC_ALPHA;
    case GL_DST_COLOR:
    case GL_SRC_COLOR:
        return GU_OTHER_COLOR;
    default:
        return GU_FIX;
    }
}
void glBlendFunc(GLenum a, GLenum b) {
    sceGuBlendFunc(GU_ADD, blend(a), blend(b), a == GL_ONE ? 0xffffff : 0,
                   b == GL_ONE ? 0xffffff : 0);
}
static int capability(GLenum c) {
    switch (c) {
    case GL_DEPTH_TEST:
        return GU_DEPTH_TEST;
    case GL_ALPHA_TEST:
        return GU_ALPHA_TEST;
    case GL_BLEND:
        return GU_BLEND;
    case GL_CULL_FACE:
        return GU_CULL_FACE;
    case GL_TEXTURE_2D:
        return GU_TEXTURE_2D;
    case GL_SCISSOR_TEST:
        return GU_SCISSOR_TEST;
    default:
        return -1;
    }
}
void woody_gl_enable(GLenum c) {
    int s = capability(c);
    if (s >= 0) {
        if (!states[s])
            sceGuEnable(s);
        states[s] = 1;
    } else if (c == GL_POLYGON_OFFSET_FILL) {
        polygon_enabled = 1;
        sceGuDepthOffset((int)-polygon_units);
    }
}
void woody_gl_disable(GLenum c) {
    int s = capability(c);
    if (s >= 0) {
        if (states[s])
            sceGuDisable(s);
        states[s] = 0;
    } else if (c == GL_POLYGON_OFFSET_FILL) {
        polygon_enabled = 0;
        sceGuDepthOffset(0);
    }
}
GLboolean woody_gl_is_enabled(GLenum c) {
    int s = capability(c);
    return s >= 0 ? states[s] : c == GL_POLYGON_OFFSET_FILL ? polygon_enabled : 0;
}
void woody_gl_polygon_offset(float factor, float units) {
    (void)factor;
    polygon_units = units;
    if (polygon_enabled)
        sceGuDepthOffset((int)-units);
}
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    sceGuPixelMask((r ? 0 : 255) | (g ? 0 : 65280) | (b ? 0 : 16711680) | (a ? 0 : 0xff000000));
}
void glShadeModel(GLenum model) {
    sceGuShadeModel(model == GL_SMOOTH ? GU_SMOOTH : GU_FLAT);
}
void glPolygonMode(GLenum face, GLenum mode) {
    (void)face;
    (void)mode;
}
void glPixelStorei(GLenum key, GLint value) {
    (void)key;
    (void)value;
}
void glViewport(int x, int y, int w, int h) {
    viewport[0] = x;
    viewport[1] = y;
    viewport[2] = w;
    viewport[3] = h;
    x = x * render_width / 480;
    y = y * render_height / 272;
    w = w * render_width / 480;
    h = h * render_height / 272;
    sceGuOffset(2048 - w / 2 - x, 2048 - h / 2 - (render_height - y - h));
    sceGuViewport(2048, 2048, w, h);
}
void glScissor(int x, int y, int w, int h) {
    scissor[0] = x;
    scissor[1] = y;
    scissor[2] = w;
    scissor[3] = h;
    x = x * render_width / 480;
    y = y * render_height / 272;
    w = w * render_width / 480;
    h = h * render_height / 272;
    sceGuScissor(x, render_height - y - h, x + w, render_height - y);
}
void glGetFloatv(GLenum key, GLfloat *v) {
    (void)key;
    *v = 1;
}
void glMatrixMode(GLenum mode) {
    matrix_mode = mode == GL_PROJECTION ? 1 : mode == GL_TEXTURE ? 2 : 0;
}
void glLoadIdentity(void) {
    identity(matrices[matrix_mode][stack[matrix_mode]]);
    submit_matrix();
}
void glLoadMatrixf(const float *m) {
    memcpy(matrices[matrix_mode][stack[matrix_mode]], m, 64);
    submit_matrix();
}
void glMultMatrixf(const float *b) {
    float result[16], *a = matrices[matrix_mode][stack[matrix_mode]];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float v = 0;
            for (int k = 0; k < 4; k++)
                v += a[k * 4 + r] * b[c * 4 + k];
            result[c * 4 + r] = v;
        }
    glLoadMatrixf(result);
}
void glPushMatrix(void) {
    unsigned *s = &stack[matrix_mode];
    if (*s < 31) {
        memcpy(matrices[matrix_mode][*s + 1], matrices[matrix_mode][*s], 64);
        ++*s;
    }
}
void glPopMatrix(void) {
    if (stack[matrix_mode])
        --stack[matrix_mode];
    submit_matrix();
}
void glTranslatef(float x, float y, float z) {
    float m[16];
    identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
    glMultMatrixf(m);
}
void glScalef(float x, float y, float z) {
    float m[16];
    identity(m);
    m[0] = x;
    m[5] = y;
    m[10] = z;
    glMultMatrixf(m);
}
void glOrtho(double l, double r, double b, double t, double n, double f) {
    float m[16];
    identity(m);
    m[0] = 2 / (r - l);
    m[5] = 2 / (t - b);
    m[10] = -2 / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    glMultMatrixf(m);
}
void glGenBuffersARB(int n, unsigned *ids) {
    for (int k = 0; k < n; k++) {
        ids[k] = 0;
        for (unsigned i = 1; i < 8192; i++)
            if (!buffers[i].data) {
                buffers[i].data = memalign(16, 16);
                if (buffers[i].data)
                    ids[k] = i;
                break;
            }
        if (!ids[k])
            error = GL_OUT_OF_MEMORY;
    }
}
void glBindBufferARB(GLenum target, unsigned id) {
    if (id >= 8192) {
        error = GL_INVALID_VALUE;
        return;
    }
    if (target == GL_ARRAY_BUFFER_ARB)
        bound_array = id;
    else
        bound_index = id;
}
void glBufferDataARB(GLenum target, GLsizeiptrARB size, const void *data, GLenum usage) {
    (void)usage;
    unsigned id = target == GL_ARRAY_BUFFER_ARB ? bound_array : bound_index;
    if (!id || size < 0) {
        error = GL_INVALID_VALUE;
        return;
    }
    void *p = memalign(16, size ? size : 16);
    if (!p) {
        error = GL_OUT_OF_MEMORY;
        return;
    }
    if (data && size)
        memcpy(p, data, size);
    sceKernelDcacheWritebackRange(p, size);
    retire(buffers[id].data, buffers[id].size);
    buffers[id].data = p;
    buffers[id].size = size;
}
void glDeleteBuffersARB(int n, const unsigned *ids) {
    for (int k = 0; k < n; k++)
        if (ids[k] && ids[k] < 8192) {
            Buffer *b = &buffers[ids[k]];
            retire(b->data, b->size);
            memset(b, 0, sizeof *b);
        }
}
static Array *array(GLenum c) {
    return c == GL_VERTEX_ARRAY ? &position : c == GL_TEXTURE_COORD_ARRAY ? &texcoord : &colour;
}
void glEnableClientState(GLenum c) {
    array(c)->on = 1;
}
void glDisableClientState(GLenum c) {
    array(c)->on = 0;
}
static void pointer(Array *a, int size, GLenum type, int stride, const void *p) {
    a->size = size;
    a->type = type;
    a->stride = stride ? stride : size * (type == GL_FLOAT ? 4 : 1);
    a->ptr = p;
    a->buffer = bound_array;
}
void glVertexPointer(int n, GLenum t, int s, const void *p) {
    pointer(&position, n, t, s, p);
}
void glTexCoordPointer(int n, GLenum t, int s, const void *p) {
    pointer(&texcoord, n, t, s, p);
}
void glColorPointer(int n, GLenum t, int s, const void *p) {
    pointer(&colour, n, t, s, p);
}
static const void *address(Array *a, unsigned i) {
    return (const unsigned char *)(a->buffer ? buffers[a->buffer].data : NULL) + (uintptr_t)a->ptr +
           (size_t)i * a->stride;
}
static void texture_apply(void) {
    if (!tex_dirty || !bound_texture)
        return;
    Texture *t = &textures[bound_texture];
    if (!t->image[0].data)
        return;
    int last = t->levels < t->maxlevel ? t->levels : t->maxlevel;
    sceGuTexMode(t->format, last, 0, GU_FALSE);
    if (t->palette) {
        sceGuClutMode(GU_PSM_8888, 0, 255, 0);
        sceGuClutLoad(32, t->palette);
    }
    for (int i = 0; i <= last; i++) {
        Image *p = &t->image[i];
        sceGuTexImage(i, p->w, p->h, p->stride, p->data);
    }
    sceGuTexScale(1, 1);
    sceGuTexOffset(0, 0);
    int nearest = t->world && getenv("WOODY_FILTER") && atoi(getenv("WOODY_FILTER")) == 0;
    sceGuTexFilter(nearest ? GU_NEAREST_MIPMAP_NEAREST : t->min, nearest ? GU_NEAREST : t->mag);
    sceGuTexWrap(t->wrap, t->wrap);
    sceGuTexLevelMode(GU_TEXTURE_AUTO, 0);
    tex_dirty = 0;
}
static int primitive(GLenum p) {
    switch (p) {
    case GL_TRIANGLES:
        return GU_TRIANGLES;
    case GL_TRIANGLE_STRIP:
        return GU_TRIANGLE_STRIP;
    case GL_TRIANGLE_FAN:
        return GU_TRIANGLE_FAN;
    case GL_LINES:
        return GU_LINES;
    case GL_LINE_STRIP:
        return GU_LINE_STRIP;
    default:
        return GU_POINTS;
    }
}
static void draw(GLenum mode, int first, int count, GLenum itype, const void *indices) {
    if (count <= 0)
        return;
    ensure();
    texture_apply();
    int indexed = itype != 0;
    const unsigned char *ip =
        bound_index ? (const unsigned char *)buffers[bound_index].data + (uintptr_t)indices
                    : indices;
    /* The engine's persistent packed buffers already have exactly GE's layout. */
    if (mode != GL_QUADS && position.buffer && position.stride == 24 && position.size == 3 &&
        position.type == GL_FLOAT && (uintptr_t)position.ptr == 12 && texcoord.on &&
        texcoord.buffer == position.buffer && texcoord.stride == 24 &&
        (uintptr_t)texcoord.ptr == 0 && colour.on && colour.buffer == position.buffer &&
        colour.type == GL_UNSIGNED_BYTE && colour.stride == 24 && (uintptr_t)colour.ptr == 8 &&
        (!indexed || itype == GL_UNSIGNED_SHORT)) {
        sceGuDrawArray(primitive(mode),
                       GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D |
                           (indexed ? GU_INDEX_16BIT : 0),
                       count, indexed ? ip : NULL,
                       (const unsigned char *)buffers[position.buffer].data + (size_t)first * 24);
        return;
    }
    int output = mode == GL_QUADS ? (count / 4) * 6 : count;
    Vertex *v = temporary((size_t)output * sizeof *v);
    if (!v)
        return;
    for (int k = 0; k < output; k++) {
        static const int quad[6] = {0, 1, 2, 0, 2, 3};
        unsigned i = mode == GL_QUADS ? (unsigned)(k / 6 * 4 + quad[k % 6]) : (unsigned)k;
        i = indexed ? (itype == GL_UNSIGNED_SHORT ? ((const unsigned short *)ip)[i]
                                                  : ((const unsigned *)ip)[i])
                    : i + first;
        const float *p = address(&position, i);
        v[k].x = p[0];
        v[k].y = p[1];
        v[k].z = position.size > 2 ? p[2] : 0;
        v[k].u = v[k].v = 0;
        v[k].color = current_colour;
        if (texcoord.on) {
            const float *uv = address(&texcoord, i);
            v[k].u = uv[0];
            v[k].v = uv[1];
        }
        if (colour.on) {
            const void *c = address(&colour, i);
            if (colour.type == GL_UNSIGNED_BYTE)
                memcpy(&v[k].color, c, 4);
            else {
                const float *f = c;
                v[k].color = byte(f[0]) | (byte(f[1]) << 8) | (byte(f[2]) << 16) |
                             (byte(colour.size > 3 ? f[3] : 1) << 24);
            }
        }
    }
    sceKernelDcacheWritebackRange(v, output * sizeof *v);
    sceGuDrawArray(mode == GL_QUADS ? GU_TRIANGLES : primitive(mode),
                   GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, output,
                   NULL, v);
}
void glDrawArrays(GLenum mode, int first, int count) {
    draw(mode, first, count, 0, NULL);
}
void glDrawElements(GLenum mode, int count, GLenum type, const void *indices) {
    draw(mode, 0, count, type, indices);
}
void glBegin(GLenum mode) {
    immediate_mode = mode;
    immediate_count = 0;
}
void glTexCoord2f(float u, float v) {
    current_u = u;
    current_v = v;
}
void glTexCoord2fv(const float *v) {
    glTexCoord2f(v[0], v[1]);
}
void glVertex3f(float x, float y, float z) {
    if (immediate_count >= 8192) {
        error = GL_OUT_OF_MEMORY;
        return;
    }
    immediate[immediate_count++] = (Vertex){current_u, current_v, current_colour, x, y, z};
}
void glVertex2f(float x, float y) {
    glVertex3f(x, y, 0);
}
void glVertex3fv(const float *v) {
    glVertex3f(v[0], v[1], v[2]);
}
void glEnd(void) {
    Array p = position, t = texcoord, c = colour;
    unsigned old = bound_array;
    bound_array = 0;
    pointer(&position, 3, GL_FLOAT, 24, (char *)immediate + 12);
    position.on = 1;
    pointer(&texcoord, 2, GL_FLOAT, 24, immediate);
    texcoord.on = 1;
    pointer(&colour, 4, GL_UNSIGNED_BYTE, 24, (char *)immediate + 8);
    colour.on = 1;
    draw(immediate_mode, 0, immediate_count, 0, NULL);
    position = p;
    texcoord = t;
    colour = c;
    bound_array = old;
}
void glGenTextures(int n, unsigned *ids) {
    for (int k = 0; k < n; k++) {
        ids[k] = 0;
        for (unsigned i = 1; i < 4096; i++)
            if (!textures[i].used) {
                textures[i].used = 1;
                textures[i].maxlevel = 7;
                textures[i].min = GU_LINEAR_MIPMAP_NEAREST;
                textures[i].mag = GU_LINEAR;
                ids[k] = i;
                break;
            }
        if (!ids[k])
            error = GL_OUT_OF_MEMORY;
    }
}
void glBindTexture(GLenum target, unsigned id) {
    (void)target;
    if (id < 4096) {
        if (bound_texture != id)
            tex_dirty = 1;
        bound_texture = id;
    }
}
void glDeleteTextures(int n, const unsigned *ids) {
    for (int k = 0; k < n; k++)
        if (ids[k] && ids[k] < 4096) {
            Texture *t = &textures[ids[k]];
            for (int j = 0; j < 8; j++)
                retire(t->image[j].data, (size_t)t->image[j].stride * t->image[j].h *
                                             (t->format == GU_PSM_T8 ? 1 : 4));
            retire(t->palette, 1024);
            memset(t, 0, sizeof *t);
        }
    tex_dirty = 1;
}
void glColorTableEXT(GLenum target, GLenum internal, int width, GLenum format, GLenum type,
                     const void *data) {
    (void)target;
    (void)internal;
    (void)format;
    (void)type;
    Texture *t = &textures[bound_texture];
    unsigned *p = memalign(16, 1024);
    if (!p) {
        error = GL_OUT_OF_MEMORY;
        return;
    }
    memset(p, 0, 1024);
    memcpy(p, data, (width > 256 ? 256 : width) * 4);
    sceKernelDcacheWritebackRange(p, 1024);
    retire(t->palette, 1024);
    t->palette = p;
    tex_dirty = 1;
}
void glTexImage2D(GLenum target, int level, int internal, int w, int h, int border, GLenum format,
                  GLenum type, const void *data) {
    (void)target;
    (void)internal;
    (void)border;
    (void)type;
    if (level < 0 || level > 7 || w < 1 || h < 1 || w > 512 || h > 512) {
        error = GL_INVALID_VALUE;
        return;
    }
    Texture *t = &textures[bound_texture];
    if (level == 0 && t->world && w >= 128 && h >= 32 &&
        (getenv("WOODY_HALF_TEXTURES") || getenv("WOODY_LOW_MEMORY")))
        t->skip = 1;
    if (t->skip) {
        if (level == 0)
            return;
        --level;
    }
    int indexed = format == GL_COLOR_INDEX8_EXT;
    int bpp = indexed ? 1 : 4, stride = w < 16 / bpp ? 16 / bpp : w;
    unsigned char *p = memalign(16, (size_t)stride * h * bpp);
    if (!p) {
        error = GL_OUT_OF_MEMORY;
        return;
    }
    memset(p, 0, (size_t)stride * h * bpp);
    if (data)
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                unsigned char *d = p + ((size_t)y * stride + x) * bpp;
                const unsigned char *s =
                    (const unsigned char *)data + ((size_t)y * w + x) * (indexed            ? 1
                                                                         : format == GL_RGB ? 3
                                                                                            : 4);
                if (indexed)
                    *d = *s;
                else {
                    d[0] = s[format == GL_BGRA_EXT ? 2 : 0];
                    d[1] = s[1];
                    d[2] = s[format == GL_BGRA_EXT ? 0 : 2];
                    d[3] = format == GL_RGB ? 255 : s[3];
                }
            }
    sceKernelDcacheWritebackRange(p, (size_t)stride * h * bpp);
    Image *im = &t->image[level];
    retire(im->data, (size_t)im->stride * im->h * bpp);
    *im = (Image){p, w, h, stride};
    t->format = indexed ? GU_PSM_T8 : GU_PSM_8888;
    if (level > t->levels)
        t->levels = level;
    tex_dirty = 1;
}
void glTexSubImage2D(GLenum target, int level, int xoff, int yoff, int w, int h, GLenum format,
                     GLenum type, const void *data) {
    (void)target;
    (void)type;
    glFinish();
    Image *im = &textures[bound_texture].image[level];
    if (!im->data || xoff < 0 || yoff < 0 || xoff + w > im->w || yoff + h > im->h) {
        error = GL_INVALID_VALUE;
        return;
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned char *d = (unsigned char *)im->data + ((y + yoff) * im->stride + x + xoff) * 4;
            const unsigned char *s =
                (const unsigned char *)data + (y * w + x) * (format == GL_RGB ? 3 : 4);
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = format == GL_RGB ? 255 : s[3];
        }
    sceKernelDcacheWritebackRange(im->data, im->stride * im->h * 4);
    tex_dirty = 1;
}
static int filter(int value) {
    switch (value) {
    case GL_NEAREST:
        return GU_NEAREST;
    case GL_LINEAR:
        return GU_LINEAR;
    case GL_NEAREST_MIPMAP_NEAREST:
        return GU_NEAREST_MIPMAP_NEAREST;
    case GL_NEAREST_MIPMAP_LINEAR:
        return GU_NEAREST_MIPMAP_LINEAR;
    case GL_LINEAR_MIPMAP_LINEAR:
        return GU_LINEAR_MIPMAP_LINEAR;
    default:
        return GU_LINEAR_MIPMAP_NEAREST;
    }
}
void glTexParameteri(GLenum target, GLenum key, int value) {
    (void)target;
    Texture *t = &textures[bound_texture];
    if (key == GL_TEXTURE_MIN_FILTER)
        t->min = filter(value);
    else if (key == GL_TEXTURE_MAG_FILTER)
        t->mag = filter(value);
    else if (key == GL_TEXTURE_WRAP_S || key == GL_TEXTURE_WRAP_T)
        t->wrap = value == GL_REPEAT ? GU_REPEAT : GU_CLAMP;
    else if (key == GL_TEXTURE_MAX_LEVEL)
        t->maxlevel = value;
    tex_dirty = 1;
}
void glTexParameterf(GLenum target, GLenum key, float value) {
    glTexParameteri(target, key, (int)value);
}
void woody_gl_texture_max_level(unsigned level) {
    if (textures[bound_texture].skip && level)
        --level;
    textures[bound_texture].maxlevel = level > 7 ? 7 : level;
    tex_dirty = 1;
}
void woody_gl_world_texture(void) {
    textures[bound_texture].world = 1;
}
void woody_gl_texenvi(GLenum target, GLenum key, int value) {
    (void)target;
    if (key == GL_TEXTURE_ENV_MODE) {
        env_mode = value;
        sceGuTexFunc(value == GL_REPLACE ? GU_TFX_REPLACE : GU_TFX_MODULATE, GU_TCC_RGBA);
    }
}
void woody_gl_texenvf(GLenum target, GLenum key, float value) {
    (void)target;
    if (key == 0x8573) {
        fragment2x = value == 2;
        if (fragment2x)
            sceGuEnable(GU_FRAGMENT_2X);
        else
            sceGuDisable(GU_FRAGMENT_2X);
    }
}
void woody_gl_get_texenv(GLenum target, GLenum key, int *v) {
    (void)target;
    (void)key;
    *v = env_mode;
}
void glReadPixels(int x, int y, int w, int h, GLenum format, GLenum type, void *data) {
    upscale();
    glFinish();
    const unsigned short *fb =
        (const unsigned short *)((uintptr_t)sceGeEdramGetAddr() + (uintptr_t)draw_buffer);
    if (format != GL_RGB || type != GL_UNSIGNED_SHORT_5_6_5_REV || x < 0 || y < 0 || x + w > 480 ||
        y + h > 272) {
        error = GL_INVALID_VALUE;
        return;
    }
    /* GE transfer also forces framebuffer readback in PPSSPP. Scalar CPU
     * reads of VRAM can otherwise return its stale backing memory there. */
    unsigned short *copy = memalign(16, (size_t)w * h * 2);
    if (!copy) {
        error = GL_OUT_OF_MEMORY;
        return;
    }
    sceKernelDcacheWritebackInvalidateRange(copy, w * h * 2);
    sceGuCopyImage(GU_PSM_5650, x, 272 - y - h, w, h, 512, (void *)fb, 0, 0, w, copy);
    glFinish();
    unsigned short *d = data;
    for (int row = 0; row < h; row++)
        memcpy(d + row * w, copy + (h - 1 - row) * w, w * 2);
    free(copy);
    tex_dirty = 1;
}
