#include <pspkernel.h>
#include <psppower.h>
#include <pspdebug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "audio.h"
#include "engine.hpp"
#include <malloc.h>
PSP_MODULE_INFO("WoodyPSP", 0, 0, 8);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(-2048);
volatile int woody_quit;
static volatile int callback_status;
static int woody_exit_callback(int a, int b, void *p) {
    (void)a;
    (void)b;
    (void)p;
    woody_quit = 1;
    return 0;
}
static int callbacks(SceSize s, void *p) {
    (void)s;
    (void)p;
    int cb = sceKernelCreateCallback("exit", woody_exit_callback, nullptr);
    int result = cb < 0 ? cb : sceKernelRegisterExitCallback(cb);
    callback_status = result < 0 ? result : 1;
    if (result < 0) {
        if (cb >= 0)
            sceKernelDeleteCallback(cb);
        return result;
    }
    while (!woody_quit)
        sceKernelSleepThreadCB();
    return 0;
}
int main(void) {
    /* Partition and libc heap are different budgets; startup may already have
     * reserved the heap before main. Log both instead of treating low partition
     * headroom as proof that the firmware did not grant extended memory. */
    unsigned boot_free = sceKernelTotalFreeMemSize();
    unsigned boot_largest = sceKernelMaxFreeMemSize();
    struct mallinfo heap = mallinfo();
    int t = sceKernelCreateThread("callbacks", callbacks, 0x11, 0x1000, 0, nullptr);
    if (t >= 0) {
        int started = sceKernelStartThread(t, 0, nullptr);
        if (started < 0) {
            sceKernelDeleteThread(t);
            callback_status = started;
        } else {
            while (!callback_status)
                sceKernelDelayThread(1000);
        }
    } else {
        callback_status = t;
    }
    scePowerSetClockFrequency(333, 333, 166);
    setvbuf(stdout, nullptr, _IONBF, 0);
    freopen("woodypsp-errors.log", "w", stderr);
    setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "WoodyPSP r8: built %s %s; C++17; native PSPSDK GU; Eko stereo music\n",
            __DATE__, __TIME__);
    fprintf(stderr, "WoodyPSP boot: partition free=%u largest=%u CPU=%d bus=%d callbacks=%08x\n",
            boot_free, boot_largest, scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency(),
            (unsigned)callback_status);
    fprintf(stderr, "WoodyPSP heap: arena=%u used=%u free=%u; shadows=off target=60\n",
            (unsigned)heap.arena, (unsigned)heap.uordblks, (unsigned)heap.fordblks);
    if (callback_status < 0) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("Exit callback initialization failed (%08x).\n",
                             (unsigned)callback_status);
        sceKernelDelayThread(3000000);
        sceKernelExitGame();
        return 1;
    }
    setenv("WOODY_FPSCAP", "60", 1);
    setenv("WOODY_NO_MUSIC", "1", 1);
    void *probe = malloc(24u * 1024u * 1024u);
    int expanded = probe != nullptr;
    if (probe) {
        *(volatile unsigned char *)probe = 0;
        free(probe);
    }
    if (!expanded)
        setenv("WOODY_LOW_MEMORY", "1", 1);
    printf("PSP memory profile: %s\n", expanded ? "64 MB" : "32 MB compatible");
    FILE *performance = fopen("woody-performance.cfg", "rb");
    if (performance) {
        char line[96], value[16];
        int setting;
        while (fgets(line, sizeof line, performance)) {
            if (sscanf(line, "music=%d", &setting) == 1 && (setting == 0 || setting == 1)) {
                if (setting == 0)
                    setenv("WOODY_NO_MUSIC", "1", 1);
                else
                    unsetenv("WOODY_NO_MUSIC");
            } else if (sscanf(line, "memory=%d", &setting) == 1 && setting == 32) {
                setenv("WOODY_LOW_MEMORY", "1", 1);
            } else if (sscanf(line, "particles=%d", &setting) == 1 && setting >= 0 &&
                       setting <= 512) {
                snprintf(value, sizeof value, "%d", setting);
                setenv("WOODY_PARTICLES", value, 1);
            } else if (sscanf(line, "filter=%d", &setting) == 1 && setting >= 0 && setting <= 1) {
                snprintf(value, sizeof value, "%d", setting);
                setenv("WOODY_FILTER", value, 1);
            } else if (sscanf(line, "textures=%d", &setting) == 1 && setting == 1) {
                setenv("WOODY_HALF_TEXTURES", "1", 1);
            } else if (sscanf(line, "width=%d", &setting) == 1 && setting >= 240 &&
                       setting <= 480 && setting % 8 == 0) {
                snprintf(value, sizeof value, "%d", setting);
                setenv("WOODY_RENDER_WIDTH", value, 1);
            } else if (sscanf(line, "detail=%d", &setting) == 1 && setting >= 0 && setting <= 2) {
                snprintf(value, sizeof value, "%d", setting);
                setenv("WOODY_DETAIL", value, 1);
            } else if (sscanf(line, "distance=%d", &setting) == 1 && setting >= 40 &&
                       setting <= 150) {
                snprintf(value, sizeof value, "%d", setting);
                setenv("WOODY_DISTANCE", value, 1);
            }
        }
        fclose(performance);
    }
    setenv("WOODY_ASPECT", "wide", 1);
    FILE *quality = fopen("woody-outline.txt", "rb");
    if (quality) {
        if (fgetc(quality) == '1')
            setenv("WOODY_OUTLINE", "1", 1);
        fclose(quality);
    }
    FILE *fps_file = fopen("woody-fps.txt", "rb");
    if (fps_file) {
        int cap;
        if (fscanf(fps_file, "%d", &cap) == 1 && (cap == 30 || cap == 40 || cap == 60)) {
            char value[8];
            snprintf(value, sizeof value, "%d", cap);
            setenv("WOODY_FPSCAP", value, 1);
        }
        fclose(fps_file);
    }
    /* Profiling belongs to a test run. The Memory Stick has enough chores already. */
    FILE *reference = fopen("woody-render-reference.flag", "rb");
    if (reference) {
        fclose(reference);
        remove("woody-render-reference.flag");
        setenv("WOODY_NORIGIDCACHE", "1", 1);
    }
    FILE *models = fopen("woody-profile-models.flag", "rb");
    if (models) {
        fclose(models);
        remove("woody-profile-models.flag");
        setenv("WOODY_PROF2", "1", 1);
    }
    FILE *diagnostics = fopen("woody-test-seconds.txt", "rb");
    if (diagnostics) {
        fclose(diagnostics);
        setenv("WOODY_FPSLOG", "1", 1);
        setenv("WOODY_PROF", "1", 1);
    }
    FILE *capture = fopen("woody-capture.flag", "rb");
    if (capture) {
        fclose(capture);
        remove("woody-capture.flag");
        setenv("WOODY_CAPTURE", "1", 1);
    }
    char level[32] = "House";
    FILE *export_loading = fopen("woody-export-loading.flag", "rb");
    if (export_loading) {
        fclose(export_loading);
        remove("woody-export-loading.flag");
        setenv("WOODY_EXPORT_LOADING", "1", 1);
    }
    FILE *level_file = fopen("woody-test-level.txt", "rb");
    if (level_file) {
        if (fgets(level, sizeof level, level_file)) {
            level[strcspn(level, "\r\n")] = 0;
            for (unsigned i = 0; level[i]; i++)
                if (!isalnum((unsigned char)level[i])) {
                    strcpy(level, "House");
                    break;
                }
            if (!level[0])
                strcpy(level, "House");
        }
        fclose(level_file);
        remove("woody-test-level.txt");
    }
    FILE *trace_input = fopen("woody-trace-input.flag", "rb");
    if (trace_input) {
        fclose(trace_input);
        remove("woody-trace-input.flag");
        setenv("WOODY_POSLOG", "1", 1);
    }
    FILE *model_view = fopen("woody-model-view.flag", "rb");
    if (model_view) {
        fclose(model_view);
        remove("woody-model-view.flag");
        setenv("WOODY_MODEL_VIEW", "1", 1);
    }
    FILE *test_duration = fopen("woody-test-seconds.txt", "rb");
    if (test_duration) {
        char seconds[32];
        if (fgets(seconds, sizeof seconds, test_duration))
            setenv("WOODY_TEST_SECONDS", seconds, 1);
        fclose(test_duration);
        remove("woody-test-seconds.txt");
    }
    char camera_values[5][32];
    float camera[5];
    const char *args[32] = {"WoodyPSP", "data/Data", level,  "--res",
                            "480x272",  "--aspect",  "wide", nullptr};
    int argc = 7;
    FILE *camera_file = fopen("woody-test-camera.txt", "rb");
    if (camera_file) {
        if (fscanf(camera_file, "%f %f %f %f %f", &camera[0], &camera[1], &camera[2], &camera[3],
                   &camera[4]) == 5) {
            args[argc++] = "--cam";
            for (int i = 0; i < 5; i++) {
                snprintf(camera_values[i], sizeof camera_values[i], "%f", (double)camera[i]);
                args[argc++] = camera_values[i];
            }
            args[argc] = nullptr;
        }
        fclose(camera_file);
        remove("woody-test-camera.txt");
    }
    char route_values[6][32];
    FILE *route = fopen("woody-test-route.txt", "rb");
    if (route) {
        float x, y, z, peck, jump;
        if (fscanf(route, "%f %f %f %f %f", &x, &y, &z, &peck, &jump) == 5 && x > -1000000 &&
            x < 1000000 && y > -1000000 && y < 1000000 && z > -1000000 && z < 1000000) {
            float values[5] = {x, y, z, peck, jump};
            for (int i = 0; i < 5; i++)
                snprintf(route_values[i], sizeof route_values[i], "%f", (double)values[i]);
            args[argc++] = "--pos";
            for (int i = 0; i < 3; i++)
                args[argc++] = route_values[i];
            if (peck >= 0 && peck < 3600) {
                args[argc++] = "--peck";
                args[argc++] = route_values[3];
                args[argc++] = "0.1";
            }
            if (jump >= 0 && jump < 3600) {
                args[argc++] = "--jump";
                args[argc++] = route_values[4];
            }
            setenv("WOODY_POSLOG", "1", 1);
            setenv("WOODY_ROCKETLOG", "1", 1);
        }
        fclose(route);
        remove("woody-test-route.txt");
    }
    FILE *side = fopen("woody-test-side.txt", "rb");
    if (side) {
        unsigned marker;
        if (fscanf(side, "%u", &marker) == 1 && marker < 1000000) {
            char value[32];
            snprintf(value, sizeof value, "%u", marker);
            setenv("WOODY_SIDE", value, 1);
            setenv("WOODY_POSLOG", "1", 1);
        }
        fclose(side);
        remove("woody-test-side.txt");
    }
    char next_level[32], next_time[32];
    FILE *next = fopen("woody-test-next.txt", "rb");
    if (next) {
        float when;
        if (fscanf(next, "%31s %f", next_level, &when) == 2 && when >= 0.5f && when <= 300) {
            int valid = 1;
            for (unsigned i = 0; next_level[i]; i++)
                if (!isalnum((unsigned char)next_level[i]))
                    valid = 0;
            if (valid) {
                snprintf(next_time, sizeof next_time, "%f", (double)when);
                args[argc++] = "--next";
                args[argc++] = next_level;
                args[argc++] = next_time;
            }
        }
        fclose(next);
        remove("woody-test-next.txt");
    }
    args[argc] = nullptr;
    int result = woody::run(argc, args);
    /* Engine load errors can return before its normal shutdown path. */
    audio_shutdown();
    fflush(nullptr); /* sceKernelExitGame bypasses libc's normal exit flushing. */
    if (result) {
        pspDebugScreenInit();
        pspDebugScreenPrintf(
            "WoodyPSP startup failed (%d).\nSee woodyre.log and woodypsp-errors.log.\n", result);
        while (!woody_quit)
            sceKernelDelayThread(20000);
    }
    sceKernelExitGame();
    return result;
}
