// These desktop rendering enhancements are absent from the original PS2 game.
#include "gtao.h"
#include "postfx.h"
#include "texpack.h"
void gtao_enable(int on) {
    (void)on;
}
int gtao_supported(void) {
    return 0;
}
void gtao_frame(float zn, float zf, float sx, float sy) {
    (void)zn;
    (void)zf;
    (void)sx;
    (void)sy;
}
void postfx_set(int a, int b, int c) {
    (void)a;
    (void)b;
    (void)c;
}
int postfx_msaa_max(void) {
    return 0;
}
int postfx_smaa_supported(void) {
    return 0;
}
void postfx_begin(int w, int h) {
    (void)w;
    (void)h;
}
void postfx_end(void) {}
int postfx_samples(void) {
    return 0;
}
int postfx_size(int *w, int *h) {
    (void)w;
    (void)h;
    return 0;
}
uint64_t tp_hash(char kind, const void *data, uint32_t bytes, int w, int h) {
    (void)kind;
    (void)data;
    (void)bytes;
    (void)w;
    (void)h;
    return 0;
}
void tp_scope(const char *name) {
    (void)name;
}
const char *tp_scope_get(void) {
    return "PS2";
}
void tp_set_dump(int on) {
    (void)on;
}
int tp_replace(uint64_t hash, int mode) {
    (void)hash;
    (void)mode;
    return 0;
}
void tp_dump(uint64_t hash, const uint8_t *rgba, int w, int h) {
    (void)hash;
    (void)rgba;
    (void)w;
    (void)h;
}
