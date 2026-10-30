/* somvr_above.h - build 25 (som-b25-above; docs/re-xr-pipeline.md 102, handoff/HANDOFF-stealth.md "build 25"): the game's
 * kills and brands from above by the VR player's own hands - the pure parts (no game memory): the sword hand's plunge,
 * the open left hand aimed down at an orc, and the blink that carries the view over the game's drop (the descent's
 * pattern: the view held from the press, out as the game's kill starts (a refused press: no black at all), black while
 * the game drops Talion onto the orc, the view seated on the ground behind him,
 * back in). The game side: somvr_above_game.c (SOMVR_ABOVE). Units: cm, cm/s, seconds; y up (game space). */
#ifndef SOMVR_ABOVE_H
#define SOMVR_ABOVE_H

typedef struct {
    float plunge_cms;   /* the plunge: the sword hand at least this fast (200 cm/s: a stab, 2-4 m/s; a sword lowered at 1
                         * m/s peaks at 1.6 - boot 20261029-220827 took that for a plunge at the stealth thrust's 1.5) */
    float down_cms;     /* ... and at least this fast downward (140 cm/s) */
    float point_deg;    /* the point leads: the hand's way within this of the blade (40 deg; the thrust's 30 + room for an
                         * overhead plunge's arc) */
    float aim_deg;      /* aimed at him: the blade within this of the line from its point to his chest (40 deg) */
    float brand_deg;    /* the brand: the open left hand on the line from the eyes to his head within this (20 deg) */
    float brand_out_cm; /* ... the palm at least this far out from the eyes (an arm reached out, not a hand at the face) */
    float out_s, in_s;  /* the blink's fade out at the press and back in on the ground (0.12 / 0.20 s) */
    float start_s;      /* the game's own kill must start within this of the press, else back in where he stands (0.6 s) */
    float black_max_s;  /* black at most this long (no landing seen): the view is seated and fades in (2.5 s) */
} somvr_fa_cfg;
void somvr_fa_cfg_default(somvr_fa_cfg *c);

/* the plunge's measures (for the lines) */
typedef struct {
    float speed, down;      /* cm/s */
    float point_deg;        /* the hand's way against the blade */
    float aim_deg;          /* the blade against the line to him */
    const char *why;        /* "" = a plunge */
} somvr_fa_plunge;
/* v: the sword hand's velocity (cm/s), axis: the blade from its base to its point (any length), to: from the point to his
 * chest. 1 = a plunge at him: fast, down, point first, aimed */
int somvr_fa_plunge_judge(const somvr_fa_cfg *c, const float v[3], const float axis[3], const float to[3], somvr_fa_plunge *o);

/* the sword hand's mean velocity (cm/s) over the newest span of min_s..max_s of n samples (times t, positions p, oldest
 * first): the newest sample against the oldest one within the span. 1 = found. Why a span: headless at 20 Hz a single
 * frame's velocity of a sword lowered at 1 m/s spiked to 2.08 (boot 20261029-223941); a stab holds its speed */
int somvr_fa_mean_vel(const double *t, const float (*p)[3], int n, double min_s, double max_s, float v[3]);

/* the brand's aim: 1 = the palm is out from the eyes (brand_out_cm) on the line to his head (brand_deg); *deg its angle */
int somvr_fa_aimed(const somvr_fa_cfg *c, const float eye[3], const float palm[3], const float head[3], float *deg);

/* the blink (one per press) */
enum { SOMVR_FAB_IDLE = 0, SOMVR_FAB_WAIT, SOMVR_FAB_OUT, SOMVR_FAB_BLACK, SOMVR_FAB_IN, SOMVR_FAB_HELD, SOMVR_FAB_N };
extern const char *const somvr_fa_blink_names[SOMVR_FAB_N];
typedef struct {
    int phase;
    int started;     /* the game's kill started (its attack on Talion) */
    int over;        /* ... and ended */
    int seated;      /* the view was put on the ground (once) */
    int refused;     /* the game started nothing within start_s: back in, nothing moved */
    int black;       /* this press fades (the drop); 0 = the view simply held (the ledge kill: Talion stays on the wall) */
    double t_press, t0;
    float alpha;
} somvr_fa_blink;
typedef struct {
    float fade;      /* 0..1 the black's level this frame */
    int hold;        /* the view held (the body moments' hold) */
    int seat;        /* 1 this frame: put the view on the ground now (under the full black) */
    int changed;     /* the phase changed */
    int done;        /* back to idle this frame */
} somvr_fa_blink_out;
void somvr_fa_blink_press(somvr_fa_blink *b, double t, int black);
/* every frame: started = the game's kill is on Talion (or was), landed = Talion is on the ground under it, over = it ended */
void somvr_fa_blink_step(somvr_fa_blink *b, const somvr_fa_cfg *c, double t, int started, int landed, int over,
                         somvr_fa_blink_out *o);

#endif
