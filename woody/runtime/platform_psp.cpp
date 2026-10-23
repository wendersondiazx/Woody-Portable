// PSP platform backend for the GPL-3.0 WoodyRE engine.
#include "plat.h"
#undef fopen
#include "render_gl.h"
#include "pad.h"
#include <GL/gl.h>

#include <pspctrl.h>
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
#include "presentation_psp.h"
extern volatile int woody_quit;
int woody_gu_open(void);
void woody_gu_swap(void);
void woody_gu_close(void);
int plat_exists(const char *path) {
    struct stat s;
    return !stat(path, &s);
}
FILE *plat_fopen(const char *path, const char *mode) {
    char fixed[1024], resolved[1024] = "";
    snprintf(fixed, sizeof fixed, "%s", path);
    for (char *p = fixed; *p; p++)
        if (*p == '\\')
            *p = '/';
    FILE *f = fopen(fixed, mode);
    if (f || mode[0] != 'r')
        return f;
    char *save = nullptr;
    if (fixed[0] == '/')
        strcpy(resolved, "/");
    for (char *part = strtok_r(fixed, "/", &save); part; part = strtok_r(nullptr, "/", &save)) {
        DIR *d = opendir(resolved[0] ? resolved : ".");
        if (!d)
            return nullptr;
        struct dirent *entry;
        char name[256] = "";
        while ((entry = readdir(d))) {
            size_t n = strlen(part), m = strlen(entry->d_name);
            if (!strcasecmp(part, entry->d_name) ||
                (m == n + 1 && entry->d_name[n] == '.' && !strncasecmp(part, entry->d_name, n))) {
                snprintf(name, sizeof name, "%s", entry->d_name);
                break;
            }
        }
        closedir(d);
        if (!name[0] || strlen(resolved) + strlen(name) + 2 >= sizeof resolved)
            return nullptr;
        if (resolved[0] && resolved[strlen(resolved) - 1] != '/')
            strcat(resolved, "/");
        strcat(resolved, name);
    }
    return fopen(resolved, mode);
}
int plat_vsc_to_vk(int sc) {
    (void)sc;
    return 0;
}
void (*plat_gl_proc(const char *name))(void) {
    (void)name;
    return nullptr;
}
void plat_message(const char *text, int warn) {
    fprintf(stderr, "%s: %s\n", warn ? "warning" : "info", text);
}
int win_open(Window *w, const char *title, int width, int height) {
    (void)title;
    (void)width;
    (void)height;
    memset(w, 0, sizeof *w);
    if (woody_gu_open())
        return -1;
    glShadeModel(GL_SMOOTH);
    w->width = 480;
    w->height = 272;
    w->focused = 1;
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    printf("PSPSDK GU: %s\n", glGetString(GL_VERSION));
    printf("PSP display: requested RGB565 Z16 target=480x272\n");
    return 0;
}
void win_mode(Window *w, int width, int height, int full) {
    (void)width;
    (void)height;
    (void)full;
    w->width = 480;
    w->height = 272;
}
int win_vsync(int interval) {
    /* The game clock supplies the selected cap; no second vblank wait here. */
    (void)interval;
    return 0;
}
void win_poll(Window *w) {
    scePowerTick(PSP_POWER_TICK_ALL);
    static double deadline;
    static int initialized;
    if (!initialized) {
        const char *seconds = getenv("WOODY_TEST_SECONDS");
        if (seconds && atof(seconds) > 0)
            deadline = win_time() + atof(seconds);
        initialized = 1;
    }
    if (deadline > 0 && win_time() >= deadline)
        woody_quit = 1;
    w->quit = woody_quit;
    w->focused = 1;
}
GLenum woody_read_pixels_rgba(unsigned char *pixels) {
    /* The SDK GE transfer reads native RGB565; expand for the capture writer. */
    unsigned short *native = static_cast<short unsigned int *>(malloc(480 * 272 * sizeof *native));
    if (!native)
        return GL_OUT_OF_MEMORY;
    while (glGetError() != GL_NO_ERROR) {
    }
    glReadPixels(0, 0, 480, 272, GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, native);
    GLenum error = glGetError();
    if (!error)
        for (unsigned i = 0; i < 480 * 272; i++) {
            unsigned p = native[i], r = p & 31, g = (p >> 5) & 63, b = p >> 11;
            pixels[4 * i] = (r << 3) | (r >> 2);
            pixels[4 * i + 1] = (g << 2) | (g >> 4);
            pixels[4 * i + 2] = (b << 3) | (b >> 2);
            pixels[4 * i + 3] = 255;
        }
    free(native);
    return error;
}
void win_swap(Window *w) {
    (void)w;
    static unsigned frame;
    ++frame;
    if ((frame == 120 || frame == 600) && getenv("WOODY_CAPTURE")) {
        unsigned char *pixels = static_cast<unsigned char *>(malloc(480 * 272 * 4));
        if (pixels) {
            while (glGetError() != GL_NO_ERROR) {
            }
            GLenum err = woody_read_pixels_rgba(pixels);
            if (!err) {
                FILE *f = fopen(frame == 120 ? "capture.ppm" : "capture-600.ppm", "wb");
                if (f) {
                    fprintf(f, "P6\n480 272\n255\n");
                    unsigned char row[480 * 3];
                    for (int y = 271; y >= 0; y--) {
                        for (int x = 0; x < 480; x++)
                            memcpy(row + x * 3, pixels + 4 * (y * 480 + x), 3);
                        fwrite(row, 1, sizeof row, f);
                    }
                    fclose(f);
                }
            }
            printf("PSP capture GL error %x\n", (unsigned)err);
            free(pixels);
        }
    }
    woody_gu_swap();
}
void win_close(Window *w) {
    (void)w;
    printf("PSP final GL error %x\n", (unsigned)glGetError());
    glFinish();
    woody_presentation_free();
    woody_gu_close();
}
void woody_presentation_present(void) {
    woody_gu_swap();
    sceDisplayWaitVblankStart();
}
void win_title(Window *w, const char *title) {
    (void)w;
    (void)title;
}
double win_time(void) {
    return sceKernelGetSystemTimeWide() / 1000000.0;
}
void pad_poll(PadState *st, int focused, unsigned changes) {
    (void)changes;
    memset(st, 0, sizeof *st);
    if (!focused)
        return;
    SceCtrlData p;
    sceCtrlPeekBufferPositive(&p, 1);
    st->kind = PADK_DS4;
    const unsigned masks[] = {PSP_CTRL_CROSS,
                              PSP_CTRL_CIRCLE,
                              PSP_CTRL_SQUARE,
                              PSP_CTRL_TRIANGLE,
                              PSP_CTRL_LTRIGGER,
                              PSP_CTRL_RTRIGGER,
                              0,
                              0,
                              PSP_CTRL_SELECT,
                              PSP_CTRL_START,
                              0,
                              0,
                              PSP_CTRL_UP,
                              PSP_CTRL_DOWN,
                              PSP_CTRL_LEFT,
                              PSP_CTRL_RIGHT};
    for (unsigned i = 0; i < sizeof masks / sizeof masks[0]; i++)
        if (p.Buttons & masks[i])
            st->buttons |= 1u << i;
    st->lx = (p.Lx - 128) / 127.0f;
    st->ly = (p.Ly - 128) / 127.0f;
}
void pad_rumble(float strength, float seconds) {
    (void)strength;
    (void)seconds;
}
void pad_set_strength(float strength) {
    (void)strength;
}
void pad_close(void) {}
const char *pad_kind_name(int kind) {
    (void)kind;
    return "PSP";
}
const char *data_find(void) {
    return "data/Data";
}
int data_verify(const char *dir) {
    (void)dir;
    fprintf(stderr, "Use the PS2 asset manifest to verify this dataset.\n");
    return -1;
}
