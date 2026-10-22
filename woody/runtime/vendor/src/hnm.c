/* hnm.c - Cryo HNM6 films: container, "IX" frame decoder, CRYO_APC sound. See hnm.h and
 * docs/HNM.md.
 *
 * The frame decoder follows the hand-written x86 of CM6_640x16.dll (image base 0x10000000; the game
 * loads CM6_<width>x16.DLL, 0x497256): HNMPI_Init 0x10001000 -> tables 0x100060f4 / 0x100061cc
 * (RGB565) / 0x10001090, HNMPI_DecodeFrame 0x10001060 -> 0x10007730 -> quantisation 0x10006318,
 * nibble unpack 0x10006450, then the block tree of an inter frame 0x1000119c or a key frame
 * 0x100022ac. The DLL hard-codes the 640-pixel pitch (0x500 bytes); here the pitch is the frame
 * width. */
#include "hnm.h"
#ifndef _WIN32
#include "plat.h" /* fopen: the data names ignoring case (plat_sdl.c) */
#endif
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- tables of the DLL (.data
 * 0x1001a70c..0x1001acec) */
static const uint8_t k_zigzag[64] = {/* 0x1001a70c */
                                     0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18,
                                     11, 4,  5,  12, 19, 26, 33, 40, 48, 41, 34, 27, 20,
                                     13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43,
                                     36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45,
                                     38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
static const int32_t k_aan[64] = {/* 0x1001a90c: AAN IDCT scale factors, 2^14 = 1 */
                                  16384, 22725, 21407, 19266, 16384, 12873, 8867,  4520,
                                  22725, 31521, 29692, 26722, 22725, 17855, 12299, 6270,
                                  21407, 29692, 27969, 25172, 21407, 16819, 11585, 5906,
                                  19266, 26722, 25172, 22654, 19266, 15137, 10426, 5315,
                                  16384, 22725, 21407, 19266, 16384, 12873, 8867,  4520,
                                  12873, 17855, 16819, 15137, 12873, 10114, 6967,  3552,
                                  8867,  12299, 11585, 10426, 8867,  6967,  4799,  2446,
                                  4520,  6270,  5906,  5315,  4520,  3552,  2446,  1247};
static const uint8_t k_luma[64] = {/* 0x1001aa0c: the JPEG luminance table */
                                   16,  11,  10,  16,  24, 40, 51,  61,  12,  12,  14,  19,  26,
                                   58,  60,  55,  14,  13, 16, 24,  40,  57,  69,  56,  14,  17,
                                   22,  29,  51,  87,  80, 62, 18,  22,  37,  56,  68,  109, 103,
                                   77,  24,  35,  55,  64, 81, 104, 113, 92,  49,  64,  78,  87,
                                   103, 121, 120, 101, 72, 92, 95,  98,  112, 100, 103, 99};
static const uint8_t k_chroma[64] = {/* 0x1001ab0c: the JPEG chrominance table */
                                     17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99,
                                     99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66,
                                     99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                     99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                     99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
static const int8_t k_s2a[16] = {
    1,  1,  1,  1,  2,  2,  2,  2,
    -2, -2, -2, -2, -1, -1, -1, -1}; /* 0x1001ac6c: first of a pair (codes 8, 10) */
static const int8_t k_s2b[16] = {1, 2, -2, -1, 1, 2, -2, -1,
                                 1, 2, -2, -1, 1, 2, -2, -1}; /* 0x1001acac: second of a pair */
static const int8_t k_s4[16] = {
    1,  2,  3,  4,  5,  6,  7,  8,
    -8, -7, -6, -5, -4, -3, -2, -1}; /* 0x1001acec: single values (codes 9, 11..13) */

static int32_t g_spiral[4096]; /* 0x100130d0: short-motion offsets, dy * pitch + dx */
static int g_spiral_pitch;
static uint16_t
    g_mask[64]; /* 0x10019e0c: per zigzag position, (1 << row) << 8 | 1 << col of the IDCT input */
static uint32_t g_rgb[640]; /* 0x1001c52c..: 64 zero, R[128], 64 zero, G[128], 64 zero, B[128], 64
                               zero (.bss) */

static void tables_init(int pitch) /* HNMPI_Init 0x10001000 -> 0x10007700 (16 bpp) */
{
    if (g_spiral_pitch == pitch)
        return;
    for (int i = 0; i < 64; i++) {
        int n = k_zigzag[i];
        g_mask[i] = (uint16_t)((1 << (n >> 3)) << 8 | 1 << (n & 7));
    } /* 0x100060f4 */
    memset(g_rgb, 0, sizeof g_rgb); /* 0x100061cc (RGB565) */
    for (int i = 0; i < 128; i++) {
        g_rgb[64 + i] = (uint32_t)((i >> 1) < 0x1f ? i >> 1 : 0x1f)
                        << 11; /* R: index = 8-bit value / 4 */
        g_rgb[256 + i] = (uint32_t)(i < 0x3e ? i : 0x3e)
                         << 5; /* G: capped at 62, not 63 (the "251" of the 8-bit value) */
        g_rgb[448 + i] = (uint32_t)((i >> 1) < 0x1f ? i >> 1 : 0x1f); /* B */
    }
    /* 0x10001090: square rings around the block, from the outside (index 4095, offset (+32, +32))
     * inwards, sides 63, 61, .., 1: left, up, right, down, then one step up-left into the next
     * ring. Index 0 = (+1, 0), 1 = (0, 0), 2 = (0, +1), ... */
    int idx = 4095, dx = 32, dy = 32;
    for (int side = 63; side > 0; side -= 2) {
        for (int k = 0; k < side; k++) {
            g_spiral[idx--] = dy * pitch + dx;
            dx--;
        }
        for (int k = 0; k < side; k++) {
            g_spiral[idx--] = dy * pitch + dx;
            dy--;
        }
        for (int k = 0; k < side; k++) {
            g_spiral[idx--] = dy * pitch + dx;
            dx++;
        }
        for (int k = 0; k < side; k++) {
            g_spiral[idx--] = dy * pitch + dx;
            dy++;
        }
        dx--;
        dy--;
    }
    g_spiral_pitch = pitch;
}

/* ---------------------------------------------------------------- the four streams of a frame
 * (0x10007730) */
typedef struct {
    const uint8_t *bit, *bit_end;
    uint32_t reg;
    int nbits; /* header +4: block tree, LE dwords, MSB first ([0x10020e6c], ebp/ebx) */
    const uint8_t *mo, *mo_end; /* header +8: 16-bit motion words ([0x10020e70]) */
    const uint8_t *so, *so_end;
    int so_odd;
    int so_next; /* header +0xc: 12-bit short-motion words, two per 3 bytes ([0x10020e74],
                    0x10001232) */
    const uint8_t *jp, *jp_end;
    int jp_odd; /* header +0x10: 4-bit coefficient codes, low nibble first ([0x10020e78],
                   0x10006450) */
    int32_t qy[64], qc[64]; /* 0x1001980c luma / 0x1001990c chroma, per zigzag position */
    int32_t co[3][64];      /* 0x100191e4: Y, U, V after the IDCT */
    uint16_t *cur;
    const uint16_t *prev;
    int w, h;
} Dec;

static int bit(Dec *d) {
    if (!d->nbits) {
        d->reg = d->bit + 4 <= d->bit_end ? (uint32_t)d->bit[0] | d->bit[1] << 8 | d->bit[2] << 16 |
                                                (uint32_t)d->bit[3] << 24
                                          : 0;
        d->bit += 4;
        d->nbits = 32;
    }
    int b = (int)(d->reg >> 31);
    d->reg <<= 1;
    d->nbits--;
    return b;
}
static int bits3(Dec *d) {
    int a = bit(d);
    int b = bit(d);
    return a << 2 | b << 1 | bit(d);
}
static unsigned motion(Dec *d) {
    if (d->mo + 2 > d->mo_end)
        return 0;
    unsigned v = d->mo[0] | d->mo[1] << 8;
    d->mo += 2;
    return v;
}
static int shortmo(Dec *d) {
    if (d->so_odd) {
        d->so_odd = 0;
        return d->so_next;
    }
    uint32_t v = 0;
    for (int k = 0; k < 3; k++)
        v |= (uint32_t)(d->so + k < d->so_end ? d->so[k] : 0) << (8 * k);
    d->so += 3;
    d->so_odd = 1;
    d->so_next = (int)(v >> 12 & 0xfff);
    return (int)(v & 0xfff);
}
static int nib(Dec *d) {
    if (d->jp >= d->jp_end)
        return 0;
    int v = d->jp_odd ? *d->jp++ >> 4 : *d->jp & 15;
    d->jp_odd ^= 1;
    return v;
}

/* ---------------------------------------------------------------- key blocks: coefficients
 * 0x10006507, IDCT 0x1000705e, colour 0x10006d98 */
/* 0x10006507: the three planes Y, U, V one after the other in one array of 3 x 64 (0x100191e4,
 * cleared once per block). The position p runs over the whole array: a handler writes its values,
 * adds to p, and only then tests whether p reached a multiple of 64 (`test esi, 0xff`); the skips
 * (codes 1, 14) do not test at all, so a skip onto 64 is followed by more codes and a run past 64
 * writes on into the next plane's coefficients, with the quantisation of p and this plane's mask -
 * the next plane then starts at its own base anyway ([0x100191cc] + 0x100). The three logos never
 * do either (every plane ends with code 0 or a write onto 64), so this and the per-plane reading of
 * ScummVM's HNM6 decoder give the same pictures. */
static void planes(Dec *d, uint16_t mask[3]) {
    int32_t *co = d->co[0];
    memset(co, 0, 3 * 64 * sizeof *co);
    for (int k = 0; k < 3; k++) {
        int p = 64 * k;
        uint16_t m = 0;
#define PUT(v)                                                                                     \
    do {                                                                                           \
        if (p < 192) {                                                                             \
            co[(p & ~63) + k_zigzag[p & 63]] = (int32_t)(v) * (p < 64 ? d->qy : d->qc)[p & 63];    \
            m |= g_mask[p & 63];                                                                   \
        }                                                                                          \
        p++;                                                                                       \
    } while (0)
        {
            int hi = nib(d), lo = nib(d);
            PUT((int8_t)(hi << 4 | lo));
        } /* the DC: a signed byte, high nibble first */
        while (p < 192) { /* no test here: see the skips */
            int b = nib(d), z, t;
            switch (b) { /* jump table 0x1001ac0c */
            case 0:
                goto done; /* 0x10006d71: the rest is zero */
            case 1:
                p += 5;
                continue; /* 0x1000667f: skips, no end test */
            case 14:
                p += 1;
                continue; /* 0x10006672 */
            case 2:
                p += 1;
                PUT(1);
                break;
            case 3:
                p += 1;
                PUT(-1);
                break;
            case 4:
                p += 2;
                PUT(1);
                break;
            case 5:
                p += 2;
                PUT(-1);
                break;
            case 6:
                p += 3;
                PUT(1);
                break;
            case 7:
                p += 3;
                PUT(-1);
                break;
            case 8:
                b = nib(d);
                z = b >> 2 & 3;
                t = b & 3;
                p += z + 1; /* skip z + 1, then t + 2 values of +-1/+-2 from pairs (0x1001ac4c) */
                for (int n = t + 2; n > 0; n -= 2) {
                    b = nib(d);
                    PUT(k_s2a[b]);
                    if (n > 1)
                        PUT(k_s2b[b]);
                }
                break;
            case 9:
                b = nib(d);
                z = b >> 2 & 3;
                t = b & 3;
                p += z + 1; /* skip z + 1, then t + 1 values of +-1..8 (0x1001ac5c) */
                for (int n = t + 1; n > 0; n--)
                    PUT(k_s4[nib(d)]);
                break;
            case 10:
                b = nib(d);
                PUT(k_s2a[b]);
                PUT(k_s2b[b]);
                break;
            case 11:
            case 12:
            case 13:
                for (int n = b - 10; n > 0; n--)
                    PUT(k_s4[nib(d)]);
                break;
            case 15: {
                int hi = nib(d), lo = nib(d);
                PUT((int8_t)(hi << 4 | lo));
            } break;
            }
            if (!(p & 63))
                break; /* the end test of the writing handlers */
        }
    done:
#undef PUT
        mask[k] = m; /* 0x10006d77: [0x100194fc + k * 4] */
    }
}

static void aan(int32_t *x, int s) /* one 8-point pass (0x1000708d / 0x100071a9) */
{
    int32_t x2n6 = x[2 * s] - x[6 * s], x2p6 = x[2 * s] + x[6 * s], x0n4 = x[0] - x[4 * s],
            x0p4 = x[0] + x[4 * s];
    int32_t x5n3 = x[5 * s] - x[3 * s], x3p5 = x[3 * s] + x[5 * s], x1n7 = x[1 * s] - x[7 * s],
            x1p7 = x[1 * s] + x[7 * s];
    int32_t t0 = ((3 * x2n6) >> 1) - x2p6, t1 = (3 * (x1p7 - x3p5)) >> 1, t2 = 30 * (x5n3 + x1n7);
    int32_t t3 = (17 * x1n7 - t2) >> 4, t4 = (t2 - 40 * x5n3) >> 4, t5 = t4 - x3p5 - x1p7;
    int32_t e0 = x0p4 + x2p6, e1 = x0n4 + t0, e2 = x0n4 - t0, e3 = x0p4 - x2p6, s17 = x1p7 + x3p5,
            u = t1 - t5, v = t3 + u;
    x[0] = e0 + s17;
    x[7 * s] = e0 - s17;
    x[1 * s] = e1 + t5;
    x[6 * s] = e1 - t5;
    x[2 * s] = e2 + u;
    x[5 * s] = e2 - u;
    x[3 * s] = e3 - v;
    x[4 * s] = e3 + v;
}
static int top_bit(unsigned v) {
    int n = 0;
    while (v >>= 1)
        n++;
    return n;
}
static void idct(int32_t *x,
                 uint16_t mask) /* 0x1000705e: only the rows / columns that hold coefficients */
{
    if (mask == 0x0101) {
        for (int i = 1; i < 64; i++)
            x[i] = x[0];
        return;
    } /* 0x100076f1: DC only */
    int rows = mask >> 8, cols = mask & 0xff;
    if ((int8_t)rows <
        (int8_t)cols) { /* 0x10007079: a *signed* byte compare (row 7 = 0x80 counts as negative) */
        for (int r = 0, n = top_bit((unsigned)rows) + 1; r < n; r++)
            aan(x + 8 * r, 1); /* 0x100073b9: along the filled rows */
        if (rows == 1)
            for (int r = 1; r < 8; r++)
                memcpy(x + 8 * r, x, 8 * sizeof *x); /* 0x100075d1 */
        else
            for (int c = 0; c < 8; c++)
                aan(x + c, 8); /* 0x100074c9 */
    } else {
        for (int c = 0, n = top_bit((unsigned)cols) + 1; c < n; c++)
            aan(x + c, 8); /* 0x1000708d: along the filled columns */
        if (cols == 1)
            for (int r = 0; r < 8; r++)
                for (int c = 1; c < 8; c++)
                    x[8 * r + c] = x[8 * r]; /* 0x10007299 */
        else
            for (int r = 0; r < 8; r++)
                aan(x + 8 * r, 1); /* 0x100071a9 */
    }
}
static uint32_t rgb_at(int i) {
    return i >= 0 && i < 640 ? g_rgb[i] : 0;
}
static void keyblock(Dec *d, int x, int y) /* 0x10007018 */
{
    uint16_t m[3];
    planes(d, m);
    idct(d->co[0], m[0]);
    idct(d->co[1], m[1]);
    idct(d->co[2], m[2]);
    uint16_t *o = d->cur + y * d->w + x;
    for (int k = 0; k < 64; k++) { /* 0x10006d98: YUV -> RGB565 through the clamp tables */
        int32_t Y = d->co[0][k], U = d->co[1][k], V = d->co[2][k];
        int32_t cr = ((V >> 4) * 16 + 8) / 10; /* 0x1001ad2c: ((i - 256) * 16 + 8) / 10, idiv */
        int32_t cb = (U >> 4) / 3;             /* 0x1001b92c: (i - 256) / 3 */
        int32_t yy = (Y >> 4) + 0x80;
        int r = (yy + cr) >> 2, b = (yy + (U >> 3)) >> 2, g = (yy - (cr >> 1) - cb) >> 2;
        o[(k >> 3) * d->w + (k & 7)] =
            (uint16_t)(rgb_at(64 + r) | rgb_at(448 + b) | rgb_at(256 + g));
    }
}

/* ---------------------------------------------------------------- block copies (the transform
 * tables 0x100170d0..) */
/* dst(x, y) of an sx * sy block from the source region at `src`; xform 4..7 read an sy * sx region
 * (checked against 0x1000337b, 0x100034e2, 0x100039d7, 0x10003cce, 0x10003fc5, 0x100042bc for 8x8).
 */
static void blit(uint16_t *dst, const uint16_t *src, int pitch, int sx, int sy, int xf) {
    if (xf == 0) { /* straight copy: the DLL moves 2 dwords (4 pixels) at a time, row by row */
        int ch = sx < 4 ? sx : 4;
        uint16_t t[4];
        for (int y = 0; y < sy; y++)
            for (int x = 0; x < sx; x += ch) {
                memcpy(t, src + y * pitch + x, ch * sizeof *t);
                memcpy(dst + y * pitch + x, t, ch * sizeof *t);
            }
        return;
    }
    uint16_t t[64];
    for (int y = 0; y < sy; y++)
        for (int x = 0; x < sx; x++) {
            int X, Y;
            switch (xf) {
            case 1:
                X = sx - 1 - x;
                Y = y;
                break; /* mirror */
            case 2:
                X = x;
                Y = sy - 1 - y;
                break; /* flip */
            case 3:
                X = sx - 1 - x;
                Y = sy - 1 - y;
                break; /* half turn */
            case 4:
                X = sy - 1 - y;
                Y = sx - 1 - x;
                break; /* anti-transpose */
            case 5:
                X = y;
                Y = sx - 1 - x;
                break; /* quarter turn */
            case 6:
                X = sy - 1 - y;
                Y = x;
                break; /* quarter turn the other way */
            default:
                X = y;
                Y = x;
                break; /* transpose */
            }
            t[y * sx + x] = src[Y * pitch + X];
        }
    for (int y = 0; y < sy; y++)
        memcpy(dst + y * pitch, t + y * sx, sx * sizeof *t);
}

static int wrap_x(int x, int w) {
    return x < 0 ? x + w : x >= w ? x - w : x;
} /* table 0x100181b0 */

/* the 16-bit motion word of a block of 8x8, 8x4 or 4x8 or 4x4: x = 128 - bits 7..14 (wrapped on the
 * line), y = offy - bits 0..6, both from the *8x8-aligned* block corner */
static const uint16_t *big_src(Dec *d, const uint16_t *base, unsigned mo, int x, int y, int offy) {
    int bx = x & ~7, by = y & ~7;
    int sx = wrap_x(bx + 128 - (int)(mo >> 7 & 0xff), d->w), sy = by + offy - (int)(mo & 0x7f);
    return base + sy * d->w + sx;
}
/* key frame (0x100022ac): big motion inside the frame being built, the transform = 2 bits + bit 15
 * of the word */
static void kf_motion(Dec *d, int x, int y, int sx, int sy) {
    unsigned mo = motion(d);
    int a = bit(d);
    int b = bit(d);
    int xf = a << 2 | b << 1 | (int)(mo >> 15);
    blit(d->cur + y * d->w + x, big_src(d, d->cur, mo, x, y, sx == 8 && sy == 8 ? 0 : 4), d->w, sx,
         sy, xf);
}
/* the small motion of 2x2 / 2x4 / 4x2 blocks: transform = bits 12..14; from the frame being built x
 * 63 - bits 0..6, y 6 - bits 7..11 (no wrap: a line overflow lands on the neighbouring line, the
 * DLL's linear addressing); inter frames with bit 15 clear take the previous frame, x 31 - bits
 * 0..5, y 31 - bits 6..11 */
static void small_motion(Dec *d, int x, int y, int sx, int sy, int inter) {
    unsigned mo = motion(d);
    int xf = (int)(mo >> 12 & 7), bx = x & ~7, by = y & ~7;
    const uint16_t *src;
    if (!inter || (mo & 0x8000))
        src = d->cur + (by + 6 - (int)(mo >> 7 & 0x1f)) * d->w + bx + 63 - (int)(mo & 0x7f);
    else
        src = d->prev + (by + 31 - (int)(mo >> 6 & 0x3f)) * d->w + bx + 31 - (int)(mo & 0x3f);
    blit(d->cur + y * d->w + x, src, d->w, sx, sy, xf);
}

/* ---------------------------------------------------------------- key frame tree (0x10002320 and
 * callees) */
static void kf22(Dec *d, int x, int y) {
    small_motion(d, x, y, 2, 2, 0);
}
static void kf24(Dec *d, int x, int y) {
    if (bit(d)) {
        kf22(d, x, y);
        kf22(d, x, y + 2);
    } else
        small_motion(d, x, y, 2, 4, 0);
}
static void kf42(Dec *d, int x, int y) {
    if (bit(d)) {
        kf22(d, x, y);
        kf22(d, x + 2, y);
    } else
        small_motion(d, x, y, 4, 2, 0);
}
static void kf44(Dec *d, int x, int y) {
    if (bit(d)) {
        if (bit(d)) {
            kf22(d, x, y);
            kf22(d, x + 2, y);
            kf22(d, x, y + 2);
            kf22(d, x + 2, y + 2);
        } /* 11: four 2x2 */
        else {
            kf24(d, x, y);
            kf24(d, x + 2, y);
        } /* 10: two 2x4 */
    } else {
        if (bit(d)) {
            kf42(d, x, y);
            kf42(d, x, y + 2);
        } /* 01: two 4x2 */
        else
            kf_motion(d, x, y, 4, 4); /* 00 */
    }
}
static void kf48(Dec *d, int x, int y) {
    if (bit(d))
        kf_motion(d, x, y, 4, 8);
    else {
        kf44(d, x, y);
        kf44(d, x, y + 4);
    }
}
static void kf84(Dec *d, int x, int y) {
    if (bit(d))
        kf_motion(d, x, y, 8, 4);
    else {
        kf44(d, x, y);
        kf44(d, x + 4, y);
    }
}
static void kf88(Dec *d, int x, int y) {
    if (bit(d)) {
        if (bit(d)) {
            if (bit(d))
                kf_motion(d, x, y, 8, 8);
            else
                keyblock(d, x, y);
        } /* 111 motion, 110 key block */
        else {
            kf44(d, x, y);
            kf44(d, x + 4, y);
            kf44(d, x, y + 4);
            kf44(d, x + 4, y + 4);
        } /* 10: four 4x4 */
    } else {
        if (bit(d)) {
            kf48(d, x, y);
            kf48(d, x + 4, y);
        } /* 01: two 4x8 */
        else {
            kf84(d, x, y);
            kf84(d, x, y + 4);
        } /* 00: two 8x4 */
    }
}

/* ---------------------------------------------------------------- inter frame tree (0x10001214 and
 * callees) */
static void skip(Dec *d, int x, int y, int sx, int sy) {
    blit(d->cur + y * d->w + x, d->prev + y * d->w + x, d->w, sx, sy, 0);
} /* 0x1000150e */
static void if_motion(
    Dec *d, int x, int y, int sx,
    int sy) /* 0x1000140e: 3 transform bits; bit 15 = inside the frame being built (0x100023f9) */
{
    unsigned mo = motion(d);
    int xf = bits3(d);
    const uint16_t *src = (mo & 0x8000) ? big_src(d, d->cur, mo, x, y, sx == 8 && sy == 8 ? 0 : 4)
                                        : big_src(d, d->prev, mo, x, y, 64);
    blit(d->cur + y * d->w + x, src, d->w, sx, sy, xf);
}
static void if_short(Dec *d, int x, int y, int sx,
                     int sy) /* 0x10001232: a 12-bit index into the spiral, from the previous frame,
                                not block-aligned */
{
    int k = shortmo(d);
    blit(d->cur + y * d->w + x, d->prev + y * d->w + x + g_spiral[k], d->w, sx, sy, 0);
}
static void if22(Dec *d, int x, int y) {
    if (bit(d)) {
        if (bit(d))
            skip(d, x, y, 2, 2);
        else
            small_motion(d, x, y, 2, 2, 1);
    } else
        if_short(d, x, y, 2, 2);
}
static void if24(Dec *d, int x, int y) {
    if (bit(d)) {
        if (bit(d)) {
            if (bit(d)) {
                if22(d, x, y);
                if22(d, x, y + 2);
            } else
                skip(d, x, y, 2, 4);
        } else
            small_motion(d, x, y, 2, 4, 1);
    } else
        if_short(d, x, y, 2, 4);
}
static void if42(Dec *d, int x, int y) {
    if (bit(d)) {
        if (bit(d)) {
            if (bit(d)) {
                if22(d, x, y);
                if22(d, x + 2, y);
            } else
                skip(d, x, y, 4, 2);
        } else
            small_motion(d, x, y, 4, 2, 1);
    } else
        if_short(d, x, y, 4, 2);
}
static void if44(Dec *d, int x, int y) {
    if (bit(d)) {
        if (bit(d)) {
            if (bit(d)) {
                if22(d, x, y);
                if22(d, x + 2, y);
                if22(d, x, y + 2);
                if22(d, x + 2, y + 2);
            } /* 111 */
            else {
                if24(d, x, y);
                if24(d, x + 2, y);
            } /* 110 */
        } else {
            if (bit(d)) {
                if42(d, x, y);
                if42(d, x, y + 2);
            } /* 101 */
            else
                skip(d, x, y, 4, 4); /* 100 */
        }
    } else {
        if (bit(d))
            if_motion(d, x, y, 4, 4);
        else
            if_short(d, x, y, 4, 4);
    }
}
static void if48(Dec *d, int x, int y) {
    if (bit(d)) {
        if (bit(d))
            skip(d, x, y, 4, 8);
        else {
            if44(d, x, y);
            if44(d, x, y + 4);
        }
    } else {
        if (bit(d))
            if_motion(d, x, y, 4, 8);
        else
            if_short(d, x, y, 4, 8);
    }
}
static void if84(Dec *d, int x, int y) {
    if (bit(d)) {
        if (bit(d))
            skip(d, x, y, 8, 4);
        else {
            if44(d, x, y);
            if44(d, x + 4, y);
        }
    } else {
        if (bit(d))
            if_motion(d, x, y, 8, 4);
        else
            if_short(d, x, y, 8, 4);
    }
}
static void if88(Dec *d, int x, int y) {
    if (bit(d)) {
        if (bit(d)) {
            if (bit(d)) {
                if (!bit(d)) {
                    if44(d, x, y);
                    if44(d, x + 4, y);
                    if44(d, x, y + 4);
                    if44(d, x + 4, y + 4);
                }
            } /* 1110; 1111 = pushal/ret in the DLL (0x100016c3), never coded */
            else
                skip(d, x, y, 8, 8); /* 110 */
        } else {
            if (bit(d)) {
                if48(d, x, y);
                if48(d, x + 4, y);
            } /* 101 */
            else {
                if84(d, x, y);
                if84(d, x, y + 4);
            } /* 100 */
        }
    } else {
        if (bit(d)) {
            if (bit(d))
                keyblock(d, x, y);
            else
                if_motion(d, x, y, 8, 8);
        } /* 011 key block, 010 motion */
        else
            if_short(d, x, y, 8, 8); /* 00 */
    }
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

int hnm6_decode(const uint8_t *data, uint32_t size, uint16_t *cur, const uint16_t *prev, int width,
                int height) {
    if (size < 24 || (width & 7) || (height & 7))
        return -1;
    int32_t quality = (int32_t)rd32(data);
    uint32_t o[5];
    for (int k = 0; k < 5; k++) {
        o[k] = rd32(data + 4 + 4 * k);
        if (o[k] > size)
            return -1;
    }
    if (o[0] > o[1] || o[1] > o[2] || o[2] > o[3] || o[3] > o[4])
        return -1;
    tables_init(width);
    static Dec d;
    memset(&d, 0, sizeof d);
    d.bit = data + o[0];
    d.bit_end = data + o[1];
    d.mo = data + o[1];
    d.mo_end = data + o[2];
    d.so = data + o[2];
    d.so_end = data + o[3];
    d.jp = data + o[3];
    d.jp_end = data + o[4];
    d.cur = cur;
    d.prev = prev;
    d.w = width;
    d.h = height;
    int key = quality < 0, q = key ? -quality : quality; /* 0x100077bf: negative = key frame
                                                            (0x100022ac), else inter (0x1000119c) */
    if (q < 0)
        q = 0;
    if (q > 100)
        q = 100; /* 0x10006318 */
    int32_t qf = q < 50 ? (q ? 5000 / q : 5000) : 200 - 2 * q;
    for (int i = 0; i < 64;
         i++) { /* per natural position (0x1001a10c / 0x1001a40c), then into zigzag order */
        int n = k_zigzag[i];
        int32_t a = (k_luma[n] * qf + 50) / 100, b = (k_chroma[n] * qf + 50) / 100;
        a = a < 8 ? 8 : a > 255 ? 255 : a;
        b = b < 8 ? 8 : b > 255 ? 255 : b;
        d.qy[i] = (int32_t)(((int64_t)a * k_aan[n]) >> 13);
        d.qc[i] = (int32_t)(((int64_t)b * k_aan[n]) >> 13);
    }
    if (!key && !prev)
        return -1;
    for (int y = 0; y < height; y += 8)
        for (int x = 0; x < width; x += 8) {
            if (key)
                kf88(&d, x, y);
            else
                if88(&d, x, y);
        }
    return 0;
}

/* ---------------------------------------------------------------- CRYO_APC sound: IMA ADPCM,
 * Woody.exe 0x4a56d0 / 0x4a5870 */
static const int16_t k_ima_step[89] =
    {/* 0x4c2a40 */
     7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,    23,
     25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,   73,    80,
     88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,  253,   279,
     307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,  876,   963,
     1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749, 3024,  3327,
     3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487,
     12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t k_ima_adj[8] = {-1, -1, -1, -1, 2, 4, 6, 8}; /* 0x4c2bb0 */

static int16_t apc_nibble(HnmFile *h, int ch, int c) {
    int step = k_ima_step[h->index[ch]];
    int diff = step >> 3; /* 0x4a5940: shifts, not the (2c+1) * step / 8 of the IMA spec */
    if (c & 4)
        diff += step;
    if (c & 2)
        diff += step >> 1;
    if (c & 1)
        diff += step >> 2;
    h->pred[ch] += (c & 8) ? -diff : diff; /* 32-bit, neither clamped nor truncated */
    int idx = h->index[ch] + k_ima_adj[c & 7];
    h->index[ch] = idx < 0 ? 0 : idx > 88 ? 88 : idx;
    return (int16_t)h->pred[ch]; /* the store is a 16-bit word */
}
static void apc_decode(HnmFile *h, const uint8_t *p, uint32_t n) {
    uint32_t frames = h->channels == 2 ? n : n * 2;
    if (h->sound_total && h->sound_done + frames > h->sound_total)
        frames = h->sound_done < h->sound_total ? h->sound_total - h->sound_done : 0;
    if ((int)frames + h->npcm > h->pcm_cap) {
        int cap = (int)frames + h->npcm + 4096;
        int16_t *q = realloc(h->pcm, (size_t)cap * h->channels * sizeof *q);
        if (!q)
            return;
        h->pcm = q;
        h->pcm_cap = cap;
    }
    int16_t *o = h->pcm + h->npcm * h->channels;
    for (uint32_t k = 0; k < frames;
         k++) { /* high nibble first; stereo: high = left, low = right */
        if (h->channels == 2) {
            o[2 * k] = apc_nibble(h, 0, p[k] >> 4);
            o[2 * k + 1] = apc_nibble(h, 1, p[k] & 15);
        } else
            o[k] = apc_nibble(h, 0, (k & 1) ? p[k >> 1] & 15 : p[k >> 1] >> 4);
    }
    h->npcm += (int)frames;
    h->sound_done += frames;
}

/* ---------------------------------------------------------------- container */
#define MARGIN 144 /* rows above and below each frame: motion may point up to 127 rows up */

int hnm_open(HnmFile *h, const char *path) {
    memset(h, 0, sizeof *h);
    uint8_t hd[64];
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fread(hd, 1, 64, f) != 64 || memcmp(hd, "HNM6", 4)) {
        fclose(f);
        return -1;
    }
    h->f = f;
    h->flags = hd[6];
    h->width = hd[8] | hd[9] << 8;
    h->height = hd[10] | hd[11] << 8;
    h->frames = (int)rd32(hd + 16);
    h->frame_size = rd32(hd + 28);
    if (h->width <= 0 || h->height <= 0 || (h->width & 7) || (h->height & 7) || h->width > 2048 ||
        h->height > 2048) {
        hnm_close(h);
        return -1;
    }
    size_t n = (size_t)h->width * (h->height + 2 * MARGIN);
    for (int k = 0; k < 2; k++)
        if (!(h->buf[k] = calloc(n, 2))) {
            hnm_close(h);
            return -1;
        }
    h->cur = h->buf[0] + MARGIN * h->width;
    h->prev = h->buf[1] + MARGIN * h->width;
    h->frame_time = 1.0 / 15; /* 0x4935e3: 15.0 without sound */
    return 0;
}

int hnm_next(HnmFile *h) {
    uint8_t b4[4];
    h->npcm = 0;
    if (!h->f || fread(b4, 1, 4, h->f) != 4)
        return 0;
    uint32_t size = rd32(b4) & 0xffffff; /* the top byte is not part of the size */
    if (!size)
        return 0; /* the files end with a zero superchunk */
    if (size < 4)
        return -1;
    size -= 4;
    if (size + 16 > h->chunk_cap) {
        uint8_t *c = realloc(h->chunk, size + 16);
        if (!c)
            return -1;
        h->chunk = c;
        h->chunk_cap = size + 16;
    }
    if (fread(h->chunk, 1, size, h->f) != size)
        return -1;
    memset(h->chunk + size, 0, 16);
    int got = 0;
    for (uint32_t p = 0; p + 8 <= size;) {
        uint32_t csz = rd32(h->chunk + p);
        const uint8_t *c = h->chunk + p + 8;
        int pad = h->chunk[p + 7]; /* flags +6: high byte = padding bytes at the end */
        if (csz < 8 || csz > size - p)
            return -1; /* not p + csz > size: a size near 4G wrapped past it (endless loop / reads
                          far outside) */
        uint32_t n = csz - 8 > (uint32_t)pad ? csz - 8 - (uint32_t)pad : 0;
        if ((c[-4] == 'A' && c[-3] == 'A') || (c[-4] == 'B' && c[-3] == 'B')) { /* sound */
            if (n >= 32 && !memcmp(c, "CRYO_APC", 8)) { /* the first block: header, 32 frames of
                                                           pre-buffer (0x4941f0 / 0x4a55a0) */
                h->sound_total = rd32(c + 12);
                h->rate = (int)rd32(c + 16);
                h->pred[0] = (int32_t)rd32(c + 20);
                h->pred[1] = (int32_t)rd32(c + 24);
                int ch = (rd32(c + 28) & 1) ? 2 : 1;
                if (ch != h->channels)
                    h->pcm_cap = 0; /* pcm_cap counts frames of the old channel count */
                h->channels = ch;
                h->index[0] = h->index[1] = 0;
                h->has_sound = h->rate > 0;
                h->sound_done = 0;
                c += 32;
                n -= 32;
                if (h->has_sound)
                    h->frame_time = (double)(h->channels == 2 ? n : n * 2) / 32 / h->rate;
            }
            if (h->has_sound)
                apc_decode(h, c, n);
        } else if (c[-4] == 'I' &&
                   c[-3] == 'X') { /* video (the player also takes "IV", 0x492d29) */
            uint16_t *t = h->prev;
            h->prev = h->cur;
            h->cur = t;
            if (hnm6_decode(c, csz - 8, h->cur, h->prev, h->width, h->height))
                return -1;
            got = 1;
            h->frame++;
        }
        p += (csz + 3) & ~3u;
    }
    return got;
}

void hnm_close(HnmFile *h) {
    if (h->f)
        fclose(h->f);
    free(h->buf[0]);
    free(h->buf[1]);
    free(h->chunk);
    free(h->pcm);
    memset(h, 0, sizeof *h);
}
