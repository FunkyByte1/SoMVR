/* somvr_surface.h - SOMVR_HANDS_SURFACE=on (build 13 candidate; the HL:A gap HG-6, docs/design-hla.md 2.1 a-c and 2.6;
 * docs/re-xr-pipeline.md 38): the hand you see, and the sword in it, stop at the world's surfaces instead of passing
 * through; a faint ghost of the real hand shows where the controller is once the two part; a tick in the hand at the
 * contact, a clank when the blade meets stone.
 * Pure: no globals, no game reads (somvr_surface_game.c casts the game's rays and feeds this). Tested by
 * tests/test_surface.c.
 *
 * Units: world cm (the game's axes), seconds. A surface is a plane: a point the game's ray hit and the hit's normal
 * (turned toward the ray's start: the player's side). Each frame the game side gives a hand its sample spheres where
 * the controller puts them (the palm and three finger tips of the hand as drawn) and the planes its rays met, the blade
 * (base, tip) when the sword is in that hand and the blade's planes. The step moves the samples together (the hand is
 * rigid) out of every plane by the least translation (rounds of the deepest), then the sword the same way by its tip
 * and base (at most blade_max_cm: deeper than that the blade goes in - the void - rather than the hand leaving the
 * controller that far), then the hand again. The shown hand = the real one + that offset. Never slower than the
 * controller: the offset only eases (spring_s) when it would jump back by more than jump_cm in one frame (a surface
 * lost, the hand freed); while the controller comes out of a surface the offset shrinks with it, 1:1.
 */
#ifndef SOMVR_SURFACE_H
#define SOMVR_SURFACE_H

#include "somvr_pose.h"

#define SOMVR_SF_NS 4   /* a hand's samples: the palm, the index tip, the middle tip, the thumb tip */
#define SOMVR_SF_NPL 6  /* planes a hand (or its blade) meets in a frame */

typedef struct { sv_vec3 p, n; } somvr_sf_plane; /* a point on the surface, its unit normal out (toward the player) */

typedef struct {
    float palm_r_cm, tip_r_cm; /* the samples' radii: the palm 3.5, the finger tips 1.0 */
    float blade_r_cm;          /* the blade's margin off a surface (1.0) */
    float blade_max_cm;        /* the sword pushes the hand back at most this far (25); deeper: the blade goes in */
    float ghost_cm;            /* the ghost shows from this separation (4) ... */
    float ghost_full_cm;       /* ... at full strength from this (12) */
    float ghost_a;             /* its opacity at full strength (0.35) */
    float ghost_in_s;          /* its fade in (0.10) and out (0.15) */
    float ghost_out_s;
    float spring_s;            /* the ease of a jump back (0.08) */
    float jump_cm;             /* an offset that shrinks by more than this in one frame eases (2) */
    float contact_cm;          /* a push over this is a contact (0.3) */
    float tick_cms;            /* the hand's speed into the surface for a full tick (150 cm/s) */
    float tick_gap_s;          /* no new tick within this of the last (0.15) */
    float clank_cms;           /* the tip's speed into the surface for a clank (60 cm/s; slower: a tick) */
    float clank_full_cms;      /* ... for a full clank (300) */
    float fx_cms;              /* ... for the sparks (150) */
    /* build 20 (SOMVR_HANDS_SURFACE_BLADE=pivot, docs/re-xr-pipeline.md 79): the drawn blade stays at a surface while the
     * hand goes on 1:1 - it turns about the grip point (somvr_sf_pivot) instead of pushing the hand back */
    int blade_pivot;           /* 1: pivot; 0 (default): build 13's push of the hand (up to blade_max_cm) */
    float pivot_max_rad;       /* the blade turns at most this far off the hand (85 deg; past it the blade goes in) */
} somvr_sf_cfg;
void somvr_sf_cfg_default(somvr_sf_cfg *c);
/* SOMVR_HANDS_SURFACE_GHOST=A (0..1, 0 = no ghost) and _BLADE_MAX_CM=N (0 = the blade never stops): 0 = not understood */
int somvr_sf_parse_ghost(const char *v, somvr_sf_cfg *c);
int somvr_sf_parse_blade_max(const char *v, somvr_sf_cfg *c);

/* one frame's input for a hand */
typedef struct {
    int free;                        /* 0: an exception holds this hand (a climb hold, the bow, the menu, a pause, ...):
                                        nothing clamps it, the offset eases back */
    sv_vec3 s[SOMVR_SF_NS];          /* the samples where the controller puts them */
    float r[SOMVR_SF_NS];            /* their radii (0: not used) */
    somvr_sf_plane pl[SOMVR_SF_NPL]; /* the hand's surfaces this frame */
    int npl;
    int blade;                       /* the sword is in this hand: base, tip (where the controller puts them) */
    sv_vec3 base, tip;
    somvr_sf_plane bpl[SOMVR_SF_NPL];
    int nbpl;
} somvr_sf_in;

enum {
    SOMVR_SF_EV_CONTACT = 1,     /* the hand met a surface (o->speed, o->tick) */
    SOMVR_SF_EV_RELEASE = 2,     /* ... and left it (o->held_s, o->depth_max) */
    SOMVR_SF_EV_BLADE = 4,       /* the blade met a surface (o->bspeed, o->clank / o->tick, o->fx) */
    SOMVR_SF_EV_BLADE_OFF = 8,   /* ... and left it */
    SOMVR_SF_EV_VOID = 16,       /* the blade went in deeper than blade_max_cm (from this frame on until it is out) */
    SOMVR_SF_EV_GHOST_ON = 32,   /* the ghost began to show */
    SOMVR_SF_EV_GHOST_OFF = 64,  /* ... and is gone */
    SOMVR_SF_EV_EASE = 128       /* a jump back eases (o->depth: the size of the jump) */
};

typedef struct {
    int contact, blade, vd;    /* the hand / the blade on a surface now; the blade in (void) */
    sv_vec3 off;               /* shown - real (world cm) */
    float ghost;               /* the ghost's strength 0..1 (x ghost_a = its opacity) */
    double t_contact, t_blade, t_tick, t_clank;
    float depth_max, bdepth_max;
    sv_vec3 last_s0, last_tip; /* the last frame's real palm and tip (the approach speeds) */
    int have_last, have_tip;
} somvr_sf_hand;
void somvr_sf_hand_reset(somvr_sf_hand *h);

typedef struct {
    unsigned ev;          /* SOMVR_SF_EV_* */
    sv_vec3 off;          /* the shown hand = the real one + off */
    float depth;          /* the hand's push this frame (cm); EV_EASE: the jump eased */
    float bdepth;         /* the sword's push this frame (cm; the needed one when it went in) */
    int plane, bplane;    /* the plane that pushed the deepest (-1 none): the game side names its surface */
    float speed, bspeed;  /* at a contact: the palm's / the tip's speed into the surface (cm/s) */
    float tick;           /* a tick's amplitude (0: none) */
    float clank;          /* a clank's strength 0..1 (0: none) */
    int fx;               /* the clank's sparks */
    float alpha;          /* the ghost's opacity (strength x ghost_a) */
    float held_s;         /* at a release: how long the contact lasted */
    float depth_max;      /* at a release: its deepest push */
    float bheld_s, bdepth_max; /* the same for the blade's release */
} somvr_sf_out;

/* the least translation that puts the spheres (s[i], r[i] > 0) out of every plane (rounds of the deepest, at most
 * 4 x npl); *depth = the largest single push, *which = its plane (-1: none). The spheres move together. */
sv_vec3 somvr_sf_push(const somvr_sf_plane *pl, int npl, const sv_vec3 *s, const float *r, int ns, float *depth, int *which);
/* a plane from a ray's hit: its point and normal (made unit and turned toward `from`; a bad normal: -dir); 0 = no plane */
int somvr_sf_plane_of(sv_vec3 from, sv_vec3 hit, sv_vec3 normal, somvr_sf_plane *out);
/* one frame of a hand: dt = since the last frame (0: the first) */
void somvr_sf_step(somvr_sf_hand *h, const somvr_sf_cfg *c, const somvr_sf_in *in, double t, float dt, somvr_sf_out *o);

/* build 20, SOMVR_HANDS_SURFACE_BLADE=pivot: the least turn of the blade about its grip point that keeps it out of the
 * plane - the blade from grip along unit dir, len cm, its tip at least margin cm off the plane - in the plane of dir and
 * the plane's normal (the tip rides along the surface); perp = a unit vector across the blade to turn toward when the
 * blade points straight into the plane. 1 = turned (*rot = the rotation vector: axis x angle, rad, the plane's frame;
 * *depth = how deep the tip would be, cm); 0 = the blade is out (nothing to do); 2 = it cannot: the grip point itself is
 * within margin of the plane, or the turn would pass max_rad (the blade goes in). */
int somvr_sf_pivot(const somvr_sf_plane *pl, sv_vec3 grip, sv_vec3 dir, float len, float margin, float max_rad, sv_vec3 perp,
                   sv_vec3 *rot, float *depth);
/* the pivoted blade's contact over frames: one clank (or a tick when slow) as it meets a surface, its release */
typedef struct {
    int on;                   /* the blade at a surface (turned) now */
    double t_on, t_clank, t_tick;
    sv_vec3 last_tip;         /* the hand's tip last frame (the approach speed) */
    int have_tip;
    float depth_max, peak_rad;
} somvr_sf_pv;
/* one frame: on = turned this frame (depth: the tip's depth before the turn, ang: the turn, rad), tip = the tip where the
 * hand puts it, n = the plane's normal. o's events as somvr_sf_step's blade ones: EV_BLADE (o->bspeed into the surface,
 * o->clank / o->fx, or o->tick), EV_BLADE_OFF (o->bheld_s, o->bdepth_max) */
void somvr_sf_pv_step(somvr_sf_pv *p, const somvr_sf_cfg *c, int on, float depth, float ang, sv_vec3 tip, sv_vec3 n,
                      double t, float dt, somvr_sf_out *o);

/* the surfaces that stop a hand (a record's name): not a character's (Flesh*, Armor*, and "" = no record: an orc's
 * capsule has none, boot 001811; orcs' bodies never clamp, so the blade's hits and the hands on orcs stay as they
 * were), nor sky, air, invisible ones, player blockers, liquids or puddles, foliage (the hand goes into a bush), cloth */
int somvr_sf_solid(const char *name);

#endif
