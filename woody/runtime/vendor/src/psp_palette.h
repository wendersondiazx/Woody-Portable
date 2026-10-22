#ifndef WOODY_PSP_PALETTE_H
#define WOODY_PSP_PALETTE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Exact indexing only: never quantize or discard alpha to fit a palette. */
static inline unsigned woody_psp_palette(const uint8_t *rgba, size_t pixels, uint8_t *indices,
                                         uint8_t palette[1024]) {
    int slots[512];
    uint32_t colors[256];
    unsigned used = 0;
    for (unsigned i = 0; i < 512; i++)
        slots[i] = -1;
    memset(palette, 0, 1024);
    for (size_t i = 0; i < pixels; i++) {
        uint32_t color;
        memcpy(&color, rgba + i * 4, 4);
        unsigned slot = (color * 2654435761u) >> 23;
        while (slots[slot] >= 0 && colors[slots[slot]] != color)
            slot = (slot + 1) & 511;
        if (slots[slot] < 0) {
            if (used == 256)
                return 0;
            colors[used] = color;
            slots[slot] = (int)used;
            memcpy(palette + used * 4, &color, 4);
            used++;
        }
        indices[i] = (uint8_t)slots[slot];
    }
    return used;
}
#endif
