/* hnm.h - Cryo HNM6 films (the three logos Logo\*.hnm): container, HNM6 "IX" video (CM6_640x16.dll,
 * HNMPI_DecodeFrame 0x10001060) and the CRYO_APC 4-bit ADPCM sound (Woody.exe 0x4a5580..0x4a5a90).
 * docs/HNM.md. Plain C99, no platform code: the player loop lives in main_engine.c (logos_play). */
#ifndef WOODY_HNM_H
#define WOODY_HNM_H
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    int width, height, frames, frame; /* header +8/+0xa, +0x10; frame = frames decoded so far */
    uint32_t frame_size;              /* header +0x1c: width * height * 2 */
    uint8_t flags; /* header +6: 0x40 = sound, 0x80 = stereo (the files; see docs/HNM.md 2) */
    uint16_t *cur, *prev; /* RGB565, pitch = width; cur = the last decoded frame */
    uint16_t
        *buf[2]; /* the two frame buffers, each with a margin (motion vectors may point outside) */
    uint8_t *chunk;
    uint32_t chunk_cap; /* the current superchunk */
    /* sound (APC): the samples the last superchunk carried, interleaved when stereo */
    int has_sound, rate, channels;
    uint32_t sound_total,
        sound_done; /* APC header +0xc: samples per channel in the whole file; decoded so far */
    int32_t pred[2];
    int index[2]; /* ADPCM state per channel: the predictor stays 32 bit (0x4a5967) */
    int16_t *pcm;
    int npcm, pcm_cap; /* npcm = sample frames */
    double frame_time; /* seconds per frame: the sound of one superchunk, else 1/15 (0x4935e3) */
} HnmFile;

int hnm_open(HnmFile *h, const char *path); /* 0 = ok */
int hnm_next(HnmFile *h); /* next superchunk: 1 = h->cur holds a new frame (and h->pcm its sound), 0
                             = end, -1 = bad data */
void hnm_close(HnmFile *h);

/* The frame decoder alone (HNMPI_DecodeFrame(data, prev, cur) of the 640-wide DLL, generalised to
 * any width that is a multiple of 8). `data` = the IX chunk payload (24-byte header + streams),
 * must be readable 8 bytes past `size`. */
int hnm6_decode(const uint8_t *data, uint32_t size, uint16_t *cur, const uint16_t *prev, int width,
                int height);

#endif
