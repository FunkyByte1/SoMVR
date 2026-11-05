/* somvr_live.h - the live loop's in-headset part (SOMVR_LIVE=on, docs/re-xr-pipeline.md 162): the coordinator's
 * short answers and the patches waiting for the player, shown as a card on the off hand's palm. Pure C11 (no OpenXR,
 * Vulkan or files): the card's text and picture, the calm gate, the live folder's small text formats. The layer
 * (vklayer/somvr_live_layer.c) reads the folder in its own thread, draws with these, places the card at the VR menu's
 * palm panel; the game side (somvr_live_game.c) applies a patch the player asked for. Tested by tests/test_live.c.
 *
 * The live folder (SOMVR_LIVE_DIR = logs/live/<session>, made by scripts/live_session.sh):
 *   say/<n>.txt        a message (scripts/live_say.sh): its text, at most SOMVR_LV_TEXT_MAX bytes, UTF-8 (shown as
 *                      ASCII: quotes and dashes folded, other characters '?'); the layer writes say/<n>.shown (the
 *                      card first on the palm) and say/<n>.read (tapped away), each with the time
 *   patches/<id>/patch.txt   KEY=VALUE lines (scripts/live_patch.sh stage): id, kind=live|restart, what, try, why
 *   patches/ready      the id of the newest staged patch
 *   patches/request.txt  the layer, at the VR menu's Apply patch / Undo patch: "apply ID SEQ" / "undo ID SEQ"
 *   patches/state.txt  the game side, after each request: KEY=VALUE - seq (the request it answered), current (the patch
 *                      live now, 0 = the session's own module), last=applied|undone|refused|stub|restart|bad, id, msg
 */
#ifndef SOMVR_LIVE_H
#define SOMVR_LIVE_H
#include <stddef.h>
#include <stdint.h>

#include "somvr_font.h"
#include "somvr_shared.h" /* somvr_posef */

#define SOMVR_LV_TEX_W 512
#define SOMVR_LV_CARD_H 320   /* the card's picture: rows 0 .. 319 */
#define SOMVR_LV_DOT_Y 320    /* the dot's: a 32 x 32 disc at (0, 320) */
#define SOMVR_LV_DOT_PX 32
#define SOMVR_LV_TEX_H 352
#define SOMVR_LV_COLS 29      /* characters a line (16 px each from x 20, as the Test page) */
#define SOMVR_LV_LINES 6      /* text lines under the title (the hint line under them) */
#define SOMVR_LV_TEXT_MAX 600
#define SOMVR_LV_SPEECH_MAX 720 /* b37: the spoken line (the title, the whole text, 0-terminated) */
#define SOMVR_LV_CARD_W_M 0.24f /* the card on the palm: 24 x 15 cm (21 texels a cm: capitals ~1 cm) */
#define SOMVR_LV_CARD_H_M 0.15f
#define SOMVR_LV_DOT_M 0.018f /* the dot: 1.8 cm */

enum { SOMVR_LVK_SAY = 1, SOMVR_LVK_PATCH = 2, SOMVR_LVK_RESULT = 3, SOMVR_LVK_WARN = 4 };
typedef struct {
    int kind;                                   /* SOMVR_LVK_*: the frame's colour */
    char title[SOMVR_LV_COLS + 1];
    char line[SOMVR_LV_LINES][SOMVR_LV_COLS + 1];
    int n;                                      /* lines used */
    int more;                                   /* the text did not fit: the last line ends in "..." */
    char hint[SOMVR_LV_COLS + 1];               /* the dim last line ("Tap to close") */
    char speech[SOMVR_LV_SPEECH_MAX];           /* b37: the spoken answer's line (somvr_lv_speech: the title unless a
                                                 * message's, then the whole text, not the wrapped lines) */
} somvr_lv_card;

/* text (UTF-8, 0-terminated) -> at most max_lines lines of at most cols characters, wrapped by words (a word longer
 * than a line is cut), newlines kept as breaks, ASCII only (folded); returns the lines used; *more = 1 when it did not
 * fit (the last line then ends in "...") */
int somvr_lv_wrap(const char *text, char out[][SOMVR_LV_COLS + 1], int max_lines, int cols, int *more);
/* one card: the title (folded, cut to a line), the text wrapped under it, the hint; b37: its speech (somvr_lv_speech) */
void somvr_lv_card_make(somvr_lv_card *c, int kind, const char *title, const char *text, const char *hint);
/* b37 (re-xr-pipeline.md 162.8): the spoken answer's line - "TITLE. TEXT" (a message's title "Message N" left out: its
 * text alone), the text whole (never the card's cut), each line break a sentence's end (". " unless it ends in . ! ?
 * or :), tabs and breaks as spaces, ASCII control bytes dropped, at most cap - 1 bytes (cut at a space); the length */
size_t somvr_lv_speech(int kind, const char *title, const char *text, char *out, size_t cap);
/* the texture (SOMVR_LV_TEX_W x SOMVR_LV_TEX_H RGBA8, premultiplied, the sRGB bytes; bgra: B and R swapped): the card
 * (a dark rounded panel, a frame in its kind's colour, the title, the lines, the hint) and the dot below it */
void somvr_lv_draw(const somvr_font *f, uint8_t *px, int bgra, const somvr_lv_card *c);

/* ---- b37 live3 (re-xr-pipeline.md 162.10): the card by itself, the dot on the hand ------------------------------- */
#define SOMVR_LV_SHOW_S 8.0     /* a card out at its calm moment stays this long (and while its line is spoken) */
#define SOMVR_LV_SHOW_MAX_S 30.0 /* ... at most */
#define SOMVR_LV_GAP_S 1.0      /* between one card's close and the next one out (a page after a page) */
/* where a card goes out by itself, world-locked (placed once, then still): beside the off hand (hand: 0 left, 1 right)
 * on its inner side (18 cm toward the body's middle, 10 cm up), 0.45-0.65 m from the head on the level, at most 45 deg
 * from the gaze's heading (on the hand's side), 12-35 cm below the eyes, facing the eyes. 0 = no pose (bad input) */
int somvr_lv_card_place(const somvr_posef *head, const somvr_posef *grip, int hand, somvr_posef *out);
/* the waiting dot on the BACK of hand's hand (4 cm out of the back of the hand from the grip, 1 cm toward the knuckles),
 * facing out of the back; 1 = the back faces the eyes (shown), 0 = it does not (hidden: never through the hand) */
int somvr_lv_dot_place(const somvr_posef *grip, int hand, const somvr_posef *head, somvr_posef *out);

/* ---- the calm gate: a card shows only after calm_s without a busy bit (somvr_vrmenu_shared.block's: the bow, a
 * climb hold, a swing, an attack incoming, build 35's SOMVR_VMG_FIGHT - an attack at Talion lately) ------------- */
typedef struct {
    double calm_s;      /* 3 s */
    uint32_t mask;      /* the bits that count (SOMVR_VMG_* without the card's own) */
} somvr_lv_ccfg;
void somvr_lv_ccfg_default(somvr_lv_ccfg *c);
typedef struct {
    int init;
    double busy_t;      /* the last frame with a busy bit (the first frame's time at the start) */
    uint32_t why;       /* the bits of the last busy frame */
    int calm;
    uint64_t n_calm, n_busy; /* changes to calm, to busy */
} somvr_lv_calm;
/* one frame: 1 = calm now */
int somvr_lv_calm_step(somvr_lv_calm *s, const somvr_lv_ccfg *c, double t, uint32_t block);

/* ---- the folder's formats ------------------------------------------------------------------------------------- */
typedef struct {
    int id;
    int live;                       /* kind=live (else restart: shown, never applied) */
    char what[160], try_[160], why[160];
} somvr_lv_patch;
/* patch.txt -> p; 1 = it has an id and a kind */
int somvr_lv_patch_parse(const char *txt, size_t n, somvr_lv_patch *p);
typedef struct {
    uint32_t seq;                   /* the request answered */
    int current;                    /* the patch live now (0 = the session's own module) */
    char last[16];                  /* applied, undone, refused, stub, restart, bad; b40 live4: start (the launcher put it
                                     * in before the game started: live_session.sh preapply) */
    int id;                         /* the request's patch */
    char msg[160];
} somvr_lv_state;
/* state.txt -> s; 1 = it has a seq */
int somvr_lv_state_parse(const char *txt, size_t n, somvr_lv_state *s);
/* "apply ID SEQ" / "undo ID SEQ" -> 1 = parsed (*op 1 apply, 2 undo) */
int somvr_lv_request_parse(const char *txt, size_t n, int *op, int *id, uint32_t *seq);
/* a KEY=VALUE file's value of key (0-terminated into out); 1 = found */
int somvr_lv_kv(const char *txt, size_t n, const char *key, char *out, size_t cap);
/* the state's result as a card (the title and the text): 1 = something to show */
int somvr_lv_result_card(const somvr_lv_state *s, const somvr_lv_patch *p, somvr_lv_card *c);
#endif
