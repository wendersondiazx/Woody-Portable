/* hud.h - the 2D layer: bitmap font, strings, text box (message 1080) and the in-game HUD
 * (docs/HUD_TEXT.md). */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>

typedef struct {
    int face; /* 0 Woody, 1 Knothead, 2 Splinter (hud+8) */
    int race; /* hud+4: race levels have no health row */
    int lives, bonus, got, total, show_total, unique, charges;
    int dollar; /* message 1172 this frame (app+0x70 -> 0x4484a0): the $ counter takes the place of
                   the bonus counter */
    float health, power; /* power = charge time * 2/3, 1 = full */
} HudState;

int hud_load(const char *common_rck,
             const char *level_rck); /* images 61..64 + strings of bank 0, font of the level bank;
                                        needs a GL context */
void hud_free(void);
void hud_begin(int win_w, int win_h); /* 640x480 virtual, origin top left; after the 3D frame */
/* port extras (docs/DISPLAY.md 3/4): the same on the box vx, vy, vw, vh of the window (GL origin
 * bottom left); a box wider than 4:3 shows more virtual x left and right of 0..640 (the layout
 * stays 4:3, centred). hud_bars blacks out the rest of the window. hud_port_str: a string ref for
 * port-only ASCII text (menu items, choices). */
void hud_begin_view(int vx, int vy, int vw, int vh);
void hud_bars(int win_w, int win_h, int vx, int vy, int vw, int vh);
uint32_t hud_port_str(const char *ascii);
uint32_t hud_port_str_tmp(int k, const char *ascii);
const char *hud_tr(const char *en); /* port-only text in the loaded CD's language (UTF-8), or en */
void hud_end(void);
void hud_draw(const HudState *s, float dt);
/* 0x448450: hud+0 = 0 game, 1 the pause pages 0x18 / 0x19 (the extended HUD: $ and charge counters
 * slide in), 2 hidden (every other menu page); call it every frame before hud_draw. A race level
 * (race = hud+4) changes the state without the slide animations. */
void hud_state(int state, int race);
void hud_boss_bar(
    int cur, int max,
    float t); /* 0x47b0b0: the boss health row, t = seconds since 0x4484d0 switched it on */
void hud_text_open(int halign, int valign, const uint32_t *ids,
                   int n);                /* message 1080; ids = string refs, 0x20001 = spacer */
void hud_text_draw(int closed, float dt); /* closed = the script variable went non-zero */
void hud_text_reset(void);
/* the HUD animator (hud+0x30, docs/HUD_TEXT.md 4.6): the icon that flies in from the pickup and the
 * 25 W's that are paid out as a heart or an extra life. hud_draw ticks them; the reward is detected
 * from the counters, like the original's setters do (0x448380). kind 1..5 = pickup types 30, 36,
 * 35, 34, 37 (0x448510); screen = the pickup projected into 640x480, NULL when it is off screen;
 * face = the portrait index for kind 1. */
void hud_anim_reset(void);
void hud_anim_pickup(int kind, const float *screen, int face);
/* menu pages of the common page class (docs/TITLE.md 5, MENU_NEWGAME.md 2): an item is {Common
 * string, flags, value}; flags 1 selectable, 2 header (never selected), 4 right, 8 left, 0x80
 * centred on x 160, 0x20 size x 0.8, 0x10 slider ("name value%"), 0x100 choice (port extra: "name
 * string", value = a string ref). The items run from y = yfrac * 480, one cell (62 * S / 40) apart,
 * size 30 shrinking until the widest name fits; the selected one blinks away at 2 Hz. `ready` = the
 * page's input delay is over. */
typedef struct {
    uint32_t id, flags;
    int value;
} MenuItem;
void hud_menu_items(const MenuItem *it, int n, float yfrac, int sel, int ready);
void hud_menu_tick(float dt); /* the blink phase, once per frame */
void hud_menu_blink(float t); /* set it: 0 after up/down, 0.25 after a slider step */
void hud_menu_page(const uint32_t *ids, int n, float yfrac, int sel,
                   float dt); /* tick + selectable items */
void hud_logo(int grow,
              float dt); /* House.rck image 1: fades in on pages 0/1 (grow), out elsewhere */
void hud_logo_off(void);
void hud_title_reset(void);
void hud_iris(float v);       /* black ring, hole radius v * 475 (0 = black, 0.85 = open) */
void hud_rect(uint32_t argb); /* flat colour over the virtual screen */
float hud_pen_text(float x, float y, float size, uint32_t col, uint32_t ref,
                   int num); /* BlackBox: Common string ref (0: the number num) at the pen, returns
                                the new pen x */
void hud_credits_enter(
    void); /* menu page 0x20 entered (0x45bd60): picture 0, the roll from the bottom */
void hud_gameover(
    void); /* menu page 0x1d (0x45bbd0): black panel, Common 56 "GAME OVER" in the centre */
void hud_credits(int prev_level, float dt); /* the credits page (0x45bd90): black panel, picture set
                                               of app+0x6c, THE END, the roll */
/* the save-slot list of pages 2 (load) and 5 (save), docs/MENU_LOAD.md 2.2: sel 1..4, pct 0 = free
 * slot, open bit 0 Knothead / bit 1 Splinter unlocked, slide = the panels' offset while they move
 * in or out, cross = draw the red cross over free slots (page 2), title = Common string (25 / 24)
 */
typedef struct {
    int sel, pct[4], open[4], cross;
    float slide;
    uint32_t title;
} HudSlots;
void hud_slot_list(const HudSlots *s, float dt);
/* the 2D layer of the world-select carousel, page 3 (docs/MENU_LOAD.md 4.6, vt[17] 0x45e8a0): name
 * on top, the stats panel left and "Game cleared" / "Location" right of the selected figure (only
 * unlocked and not BlackBox: `stats`), the PLAY / SEE HIGH SCORES list (`list`: unlocked, the page
 * neither opening nor closing), the two yellow arrows and "Total Score :". off = the text slide (<=
 * 0), arrow_s = the arrows' slide, arrow_l / arrow_r their grey (128 = 1). */
typedef struct {
    uint32_t name; /* Common string 30..34 */
    int stats, face, lives, unique, charges;
    float health;
    int pct;
    uint32_t world, part; /* location strings of the first unfinished level, 0 = none */
    int list, nitems, list_sel;
    const MenuItem *items;
    float yfrac;
    float off, arrow_s, arrow_l, arrow_r;
    int total;
} HudCarousel;
void hud_carousel(const HudCarousel *c);
/* the results screen after a level, menu page 0x1e (docs/RESULTS.md): level = the index of the
 * level just left (app+0x6c), st = {total enemies, total W, enemies beaten, W found} (app+0x74..),
 * time in s, best = the saved best score from BEFORE this run, cats = complete categories = new
 * unique items (the $ line). */
typedef struct {
    int level, race;
    int st[4];
    float time;
    int best, cats;
} HudResults;
void hud_results_enter(void); /* 0x4544b0: page entered (state 0), nothing drawn yet */
void hud_results_show(void);  /* 0x454560: iris closes, texts slide in, the lines count (state 1) */
void hud_results_hide(void);  /* 0x454580: everything gone at once, iris opens (state 2/3) */
int hud_results_confirm(
    void); /* 0x4545a0: 1 = all counted (OK); while counting it skips to the end */
int hud_results_draw(const HudResults *r,
                     float dt); /* 1 while a number is counting (tick loop SoundFx 0x3d) */
/* menu page 4, the high scores of one character (docs/MENU_LOAD.md 4.8, class 0x45bfb0, draw
 * 0x45bff0): one row per finished level in play order, up to the first unfinished one. slide = the
 * texts' offset (600 -> 0 while opening, 0 -> 600 while closing), grow = the backdrop's scale (0 ->
 * 1, 1 -> 0). */
typedef struct {
    uint32_t world, part;
    int best, race;
    float time;
    int en_got, en_tot, w_got, w_tot;
} HudScoreRow;
typedef struct {
    int face;
    float slide, grow;
    int nrows;
    HudScoreRow row[9];
} HudScores;
void hud_scores(const HudScores *s);
int hud_sky_images(uint32_t out[5]); /* level bank images in cube order 3,0,1,2,4 when the bank has
                                        >= 5 images, else 0 */
/* pickups are sprites, not meshes (docs/BONUS.md 3.1, 0x479530): n = 0 life, 1 charge, 2 W, 3 $, 4
 * flag. Call between the 3D frame and hud_begin. */
void hud_world_sprites_begin(const float *right, const float *up);
void hud_world_sprite(int n, const float *pos, float size);
void hud_world_wing(int n, const float *c, const float *u, const float *v,
                    float half); /* bank 0 images 53..56: one wing of a butterfly */
void hud_world_bubble(int image, const float *pos, float size,
                      int mirror); /* bank 0 images 44..52: the speech bubble 0x478980, alpha
                                      blended, mirrored in u */
void hud_world_sprites_end(void);
/* The world sprites are not drawn when called: each hud_world_* call records one quad (one batch of
 * 0x481560 on list +0x1c8 alpha blended or +0x1cc additive), and the renderer draws them inside the
 * fade buckets of 0x428d00 (rnd_sorted, docs/ MODEL_RENDER.md 10). hud_world_sprites_begin empties
 * the record; hud_world_sprites_late marks where the instance Updates (0x42b400: the bonus halos)
 * end and the model draw 0x42b380 would come, which matters for the creation order of bucket 0. */
void hud_world_sprites_late(void);
int hud_wq_count(void);
void hud_wq_quad(int i, float v[4][3], int *blended,
                 int *early); /* corners (k0 first, as submitted) and kind of record i */
void hud_wq_draw(int i); /* draw record i: needs depth test on, depth writes off, blending on */
void hud_wq_done(void);  /* texture env / blend func / colour back to the defaults */
/* additive effect sprite (bank 0 image 0, 4 or 6), rotated by `turns` around the view axis; colour
 * = rgb * alpha */
void hud_world_fx(int image, const float *pos, float size, float turns, const float *rgb,
                  float alpha);
/* the same sprite, but lying in the plane with normal `n` instead of facing the camera (sprite flag
 * bit 0 off, 0x4717d7): what the nine quads of an explosion flash are made of
 * (docs/PROJECTILES.md 5.3) */
void hud_world_fx_plane(int image, const float *pos, const float *n, float size, const float *rgb,
                        float alpha);
/* the sprite primitive 0x470f10 with the original's own flags (docs/PARTICLES.md 1): 1 camera
 * facing, else a plane (0x20: spanned by basis R = [0..2] and F = [3..5]; neither: normal =
 * basis[0..2]); 2 own colour/alpha, else 0.5 grey; 4 rotation `rot` in 1/512 turn; 8 alpha blended
 * at texture x 2c, else additive texture x c x a; 0x40 UV set `mirror` (0x470d80: 1 = v flipped, 2
 * = u flipped, 3 both). `size` is the half diagonal. Between hud_world_sprites_begin/end. */
void hud_world_spr(int image, const float *pos, float size, int rot, const float *rgb, float alpha,
                   int flags, const float *basis, int mirror);
void hud_world_spr_mode(int mode, int image, const float *pos, float size, int rot,
                        const float *rgb, float alpha, int flags, const float *basis,
                        int mirror); /* sprite mode 0x12 (square) or 0x1a (1:2 upright) */
/* additive ribbon segment with a colour per end (bank 0 image 0) */
void hud_world_ribbon(const float *a, const float *b, const float *eye, float hw,
                      const float *rgb_a, const float *rgb_b);
/* additive camera-facing line quad (line primitive 0x471a10, bank 0 image 1): half width hw,
 * colour*alpha at both ends */
void hud_world_beam(const float *a, const float *b, const float *eye, float hw, const float *rgb,
                    float alpha_a, float alpha_b);
/* the same line quad without a texture (0x471a10 without flag 0x200): the lightning arc along a
 * laser */
void hud_world_line(const float *a, const float *b, const float *eye, float hw, const float *rgb,
                    float alpha_a, float alpha_b);
/* the same additive line with a bank 0 image along it, u from a (0) to b (1): the drops of the
 * water splash (image 57, docs/SPLASH.md 4) */
void hud_world_streak(int image, const float *a, const float *b, const float *eye, float hw,
                      const float *rgb, float alpha_a, float alpha_b);
/* the same after setting the line's uv set to 0x470d80 mode `flip` (0 plain, 1 v mirrored, 2 u
 * mirrored, 3 both, 4/6 turned): the storm's lightning bolt, image 30 (docs/STORM.md 5). The set
 * persists: every later line (hud_world_streak/_beam) keeps it, as in the original */
/* S+0x208 of the shared sprite object: the position of the last world sprite handed to 0x470f10
 * (drawn or not) */
void hud_last_sprite_pos(float out[3]);
void hud_world_streak_flip(int image, const float *a, const float *b, const float *eye, float hw,
                           const float *rgb, float alpha_a, float alpha_b, int flip);
/* an additive textured quad in world space, colour per vertex (already times alpha): the fire ring
 * of the special attack (docs/PERSO_SPECIAL.md 3.3) */
void hud_world_quad(int image, const float v[4][3], const float uv[4][2], const float rgb[4][3]);
/* additive ring of half width hw around c, lying in the plane with normal n: the landing marker
 * under Woody */

#endif
