/* touch.h - an on-screen pad for touch screens (PORT EXTRA, the SDL builds; on by default on
 * Android, elsewhere with WOODY_TOUCH=1). The left part of the screen is a floating stick, the
 * right part has the face buttons (A jump, B duck, X attack, Y special), LB (camera behind), RB
 * (look around) and Start (pause); they reach the game as one more pad (pad_sdl.c), so the menus
 * and the bindings of the Controls page work as with a real one. A real pad or a key hides the
 * controls until the screen is touched again. */
#ifndef WOODY_TOUCH_H
#define WOODY_TOUCH_H
#include "pad.h"

void touch_event(
    const void *sdl_event, int width,
    int height); /* an SDL_Event (finger events, keys); width / height = the drawable */
void touch_pad(
    PadState *st,
    int real_pad_used); /* merge into this frame's pads; real_pad_used hides the controls */
void touch_draw(int width, int height); /* over the finished frame, before the swap */
#endif
