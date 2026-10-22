/* Smooth periodic sprite rotation without software double-precision trig.
 * 512 samples match the original engine's turn table; interpolation avoids
 * introducing visible angular steps when an effect supplies fractional turns. */
#ifndef WOODY_PSP_TRIG_H
#define WOODY_PSP_TRIG_H
#include <math.h>
static inline void woody_psp_sincos_turns(float turns, float *s, float *c) {
    static float table[513];
    static int ready;
    if (!ready) {
        for (unsigned i = 0; i < 512; i++)
            table[i] = sinf((float)i * (6.283185307179586f / 512.0f));
        table[0] = table[256] = table[512] = 0;
        table[128] = 1;
        table[384] = -1;
        ready = 1;
    }
    float phase = (turns - floorf(turns)) * 512.0f;
    unsigned i = (unsigned)phase;
    if (i >= 512)
        i = 0;
    float f = phase - floorf(phase);
    unsigned j = (i + 128) & 511;
    *s = table[i] + (table[i + 1] - table[i]) * f;
    *c = table[j] + (table[j + 1] - table[j]) * f;
}
#endif
