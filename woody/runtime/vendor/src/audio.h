/* audio.h - software mixer (waveOut, 44.1 kHz stereo) + .rck sound banks + streamed music
 * (Music.bf). Replaces the sound manager [0x4c2dd8] / Cryo Sound Library of the original
 * (docs/SOUND.md). */
#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>

#define AUDIO_BANKS 2 /* 0 = Common/<character>.rck, 1 = <LVL>.rck (docs/RCK.md) */

int audio_init(void); /* 0 = ok; on failure every call below is a no-op */
void audio_shutdown(void);

int audio_bank_load(
    int bank,
    const char *path); /* loads the type-0 items of an RKET bank; returns the sound count or -1 */
void audio_bank_free(int bank); /* stops the voices that use it */

/* ref = bank<<24 | type<<16 | index (RckGet encoding); owner = emitter key (instance pointer) or
 * NULL. vol 0..100; f > 0 = frequency factor, f <= 0 = wanted duration -f in seconds (0x46ac10 /
 * 0x46ac80); pos = pointer to the emitter position in world units, read live by the mixer, or NULL
 * for 2D; dmin in metres; maxdur = life of a loop in s (<= 0: forever). The fade-in set by
 * audio_next_fade_in applies to this voice and is cleared. Returns a voice handle (>0) or 0. */
int audio_play(uint32_t ref, const void *owner, int loop, float vol, float f, const float *pos,
               float dmin, float maxdur);
/* queue = 1: the queued Play variants (2D vt[0x2c]/[0x30], 3D argument queue = 1): the voice waits
 * silently behind the newest 2D voice (mgr+0x30) resp. behind the owner's newest 3D voice and its
 * queued successors, and starts when that one ends or stops */
int audio_play_q(uint32_t ref, const void *owner, int queue, int loop, float vol, float f,
                 const float *pos, float dmin, float maxdur);
void audio_next_fade_in(float t); /* message 1657 */
void audio_offline_advance(
    double dt); /* testing: WOODY_AUDIODUMP + WOODY_FIXDT mix per game frame, not in real time */
long long audio_dump_pos(void); /* frames written to WOODY_AUDIODUMP so far */
void audio_stop3d(uint32_t ref, const void *owner, float fade); /* key (owner, ref): message 1628 */
void audio_stop2d(uint32_t ref, float fade,
                  int mask); /* key ref; mask 1 = loops, 2 = one-shots: message 1652 */
void audio_stop_all(void);   /* every voice, not the streams */
void audio_set_volume(uint32_t ref, const void *owner, float vol);
void audio_listener(const float *pos, const float *right); /* per frame: the camera */
void audio_update(int (*active)(
    const void *owner));      /* per frame: 3D voices of owners that are not active fall silent */
void audio_pause(int paused); /* suspends the voices */
void audio_master(float sfx, float music); /* 0..1 */
void audio_reverse_stereo(
    int on); /* Woody.cfg +0x74 "Invert Left/Right" (0x46b7e0): 3D voices pan mirrored */
float audio_duration(uint32_t ref);
void audio_fx(int id, const void *owner,
              const float *pos); /* engine effect `id` of the SoundFx table; pos NULL = 2D */
void audio_fx_stop(int id, const void *owner, int is3d);

/* Streams from Music.bf: track numbers of table 0x4b73a0 (docs/SOUND.md 4.1). */
int audio_bf_open(const char *path);
void audio_music(int track); /* looping, replaces the current track at once */
void audio_music_stop(float fade);
void audio_music_pause(int paused, float fade);
int audio_music_track(void); /* -1 = none */
void audio_rtc(int track);   /* one-shot cinematic stream; -1 = stop */

/* The sound of an HNM film (hnm.c, logos_play in main_engine.c): a queue of interleaved s16 PCM,
 * mixed at full gain beside the rest (the original hands it to its own DirectSound buffer,
 * docs/HNM.md 3). push returns the frames taken (up to ~4 s queued). */
int audio_pcm_open(int rate, int channels);
int audio_pcm_push(const int16_t *pcm, int frames);
void audio_pcm_close(void);

#endif
