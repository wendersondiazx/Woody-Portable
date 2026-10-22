#include "presentation_psp.h"
#include "vendor/src/plat.h"
#include "vendor/src/render_gl.h"
#include <GL/gl.h>
#include <pspctrl.h>
#include <pspkernel.h>
#include <psppower.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

extern volatile int woody_quit;
static GLuint cover;
static int attempted;
static void capture(const char *path) {
    unsigned char *pixels = malloc(480 * 272 * 4);
    if (!pixels)
        return;
    if (woody_read_pixels_rgba(pixels)) {
        free(pixels);
        return;
    }
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n480 272\n255\n");
        unsigned char row[480 * 3];
        for (int y = 271; y >= 0; y--) {
            for (int x = 0; x < 480; x++)
                memcpy(row + x * 3, pixels + (y * 480 + x) * 4, 3);
            fwrite(row, 1, sizeof row, f);
        }
        fclose(f);
    }
    free(pixels);
}
/* Five-bit rows, A-Z followed by 0-9. A tiny font works before game banks load. */
static const unsigned char glyphs[36][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
    {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {31, 4, 4, 4, 4, 4, 31},
    {7, 2, 2, 2, 18, 18, 12},     {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},     {14, 17, 19, 21, 25, 17, 14},
    {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},   {14, 16, 16, 30, 17, 17, 14},
    {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}};
static void quad(float x, float y, float w, float h) {
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
}
static void text(float x, float y, float scale, const char *s) {
    /* PSPGL treats a run of quads as a strip. Separate glyph pixels must be
     * explicit triangles, otherwise unrelated squares connect into streaks. */
    glBegin(GL_TRIANGLES);
    for (; *s; s++, x += scale * 6) {
        int c = toupper((unsigned char)*s), index = c >= 'A' && c <= 'Z'   ? c - 'A'
                                                    : c >= '0' && c <= '9' ? 26 + c - '0'
                                                                           : -1;
        if (index < 0)
            continue;
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 5; k++)
                if (glyphs[index][r] & (16 >> k)) {
                    float px = x + k * scale, py = y + r * scale;
                    glVertex2f(px, py);
                    glVertex2f(px + scale, py);
                    glVertex2f(px + scale, py + scale);
                    glVertex2f(px, py);
                    glVertex2f(px + scale, py + scale);
                    glVertex2f(px, py + scale);
                }
    }
    glEnd();
}
static void load_cover(void) {
    if (attempted)
        return;
    attempted = 1;
    FILE *f = fopen("data/presentation/cover.rgba", "rb");
    if (!f) {
        fprintf(stderr, "presentation: cover missing\n");
        return;
    }
    unsigned char head[12];
    unsigned char *pixels = NULL;
    if (fread(head, 1, sizeof head, f) != sizeof head || memcmp(head, "WPIC", 4) || head[4] != 0 ||
        head[5] != 1 || head[6] != 0 || head[7] != 0 || head[8] != 0 || head[9] != 1 ||
        head[10] != 0 || head[11] != 0)
        goto done;
    pixels = malloc(256 * 256 * 4);
    if (!pixels || fread(pixels, 1, 256 * 256 * 4, f) != 256 * 256 * 4)
        goto done;
    glGenTextures(1, &cover);
    glBindTexture(GL_TEXTURE_2D, cover);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
done:
    free(pixels);
    fclose(f);
}
static void draw(const char *level, const char *stage, float progress, float alpha, float slide,
                 int boot) {
    load_cover();
    glViewport(0, 0, 480, 272);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glTexEnvf(GL_TEXTURE_ENV, 0x8573, 1.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 480, 272, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glClearColor(0.025f, 0.06f, 0.12f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(0.05f, 0.13f, 0.23f, 1);
    glBegin(GL_QUADS);
    quad(266, 0, 214, 272);
    glEnd();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (cover) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, cover);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(1, 1, 1, alpha);
        float x = 8 - slide, y = 12, size = 248;
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);
        glVertex2f(x, y);
        glTexCoord2f(1, 0);
        glVertex2f(x + size, y);
        glTexCoord2f(1, 1);
        glVertex2f(x + size, y + size);
        glTexCoord2f(0, 1);
        glVertex2f(x, y + size);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
    glColor4f(0.5f, 0.82f, 1, alpha);
    text(282, boot ? 82 : 55, 1, boot ? "PORT POWERED BY" : "LOADING");
    glColor4f(1, 0.81f, 0.2f, alpha);
    text(282, boot ? 111 : 84, 2, boot ? "wendersondiazx" : level);
    glColor4f(1, 1, 1, alpha);
    text(282, boot ? 144 : 118, 1, boot ? "WOODY PSP R7" : stage);
    glColor4f(0.12f, 0.24f, 0.35f, 1);
    glBegin(GL_QUADS);
    quad(282, 180, 176, 3);
    glEnd();
    float width = 176 * fminf(1, fmaxf(0, progress));
    glColor4f(0.12f, 0.7f, 1, alpha);
    glBegin(GL_QUADS);
    quad(282, 180, width, 3);
    glEnd();
    glColor4f(0.5f, 0.62f, 0.72f, alpha);
    text(282, 224, 1, "ALL PSP MODELS");
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glColor4f(1, 1, 1, 1);
    static int captured_boot, captured_loading;
    if (getenv("WOODY_CAPTURE")) {
        if (boot && progress >= 0.45f && !captured_boot) {
            capture("capture-opening.ppm");
            captured_boot = 1;
        } else if (!boot && progress >= 0.55f && !captured_loading) {
            capture("capture-loading.ppm");
            captured_loading = 1;
        }
    }
    woody_presentation_present();
}
void woody_presentation_boot(void) {
    double start = win_time();
    while (!woody_quit) {
        double t = win_time() - start;
        if (t >= 2.2)
            break;
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        if (t > 0.3 && (pad.Buttons & (PSP_CTRL_START | PSP_CTRL_CROSS)))
            break;
        float fade = fminf(1, (float)t / 0.45f) * fminf(1, (2.2f - (float)t) / 0.3f);
        draw("", "", (float)t / 2.2f, fade, 18 * (1 - fade), 1);
        scePowerTick(PSP_POWER_TICK_ALL);
    }
}
void woody_presentation_loading(const char *level, const char *stage, float progress) {
    fflush(stdout); /* Loading milestones only; no card flush during play. */
    draw(level, stage, progress, 1, 0, 0);
}
void woody_presentation_free(void) {
    if (cover)
        glDeleteTextures(1, &cover);
    cover = 0;
    attempted = 0;
}
