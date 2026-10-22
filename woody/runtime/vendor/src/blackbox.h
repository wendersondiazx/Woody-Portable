/* blackbox.h - the BlackBox mini game (App state 3, level 25 \Data\BlackBox): the object 0x484420
 * of app+0xe4 and its frame 0x4846d0, a 2D platformer drawn over the frozen 3D level
 * (docs/BLACKBOX.md). */
#ifndef BLACKBOX_H
#define BLACKBOX_H

/* 0x484420: images 0..65 of the level bank (<data>/BlackBox/Blackbox.rck) and \Game\mask.bin
 * (<data>/../Game/mask.bin). Needs the GL context. 0 = ok. */
int bb_init(const char *data_dir);
void bb_free(void);
int bb_active(void);
/* 0x4846d0, once per frame inside the 2D layer (640x480 virtual, after hud_begin_view): logic and
 * drawing in one pass as in the original. menu = App state 0 (a pause page is open: dt 0,
 * everything frozen but drawn); held[k] = action k held (0x467400), k = 0 left, 1 right, 2 up, 3
 * down, 4 jump, 5 duck, 6 attack. Returns 1 when the third level is over (0x401d31: credits). */
int bb_frame(int menu, float dt, const int held[7]);

#endif
