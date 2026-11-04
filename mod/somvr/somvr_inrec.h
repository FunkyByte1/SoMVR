/* somvr_inrec.h - SOMVR_INPUT_REC's recording of a session's input and its replays (docs/re-xr-pipeline.md 160).
 * Pure: no globals, no game calls, no OpenXR include (somvr_inrec.c; the game side somvr_inrec_game.c; tested by
 * tests/test_inrec.c; read by tests/replay_input.c and scripts/inrec.py).
 *
 * The file (<SOMVR_LOG>.inrec beside somvr.log; a bug mark's marks/<id>/input.inrec is the same format) is a 64-byte
 * header and then records, each `uint16 type, uint16 size` (size = the whole record's bytes, a multiple of 8) and its
 * payload; a reader skips a type it does not know by its size, so a newer writer's records never break an older reader.
 * Little-endian, the structs as laid out here (asserted below). Times: `t` = the game clock of somvr.log's `t=`
 * (cb_now, seconds), `xr_time` = the OpenXR XrTime (ns). Poses in the XR space the eyes and the grips are located in
 * (OpenXR: metres, right-handed, -Z forward); the rig's values in the game's convention (somvr_eyes.h).
 *
 *   FRAME (every stereo frame, H4 after the hands are located): the views' XrTime, both eye poses, the rig (P0, yaw0,
 *          the base and its yaw psi, the player size), both controllers (physical: 0 left, 1 right) as the logic got
 *          them - the grip as located (and with SOMVR_HANDS_TEST, as the test words put it), its velocities and flags,
 *          the layer's snapshot of the aim pose, the trigger, the squeeze (a test squeeze over it), the stick, the
 *          buttons, their press counters and the action/flag bytes
 *   STATE (4 Hz): Talion (root, heading, health, flags: crouched, weapons out, climb holds, the grab, the practice
 *          area), the nearest SOMVR_IR_ORCS orcs (where, heading, distance, health, dead / in a paired move / ours)
 *   BLADE (each combat frame with a blade, or its absence: the swing tracker's reset): the hits' blade as combat fed
 *          its swing tracker (sc_speed_push) - for the offline force tiers
 *   HIT   (each landed hit, and each contact's tier as combat decides it): the tier, where along the blade, the
 *          edge's angle, the blow's energy, the speed
 *   MARK  (each bug mark): its number, time and folder name
 *   CFG   (once at the start): a config blob (id 1 = combat's sc_cfg, its size checked by the reader)
 *   NOTE  (text): the recorder's start, a replay's start and stop, the trigger words that change a test
 *   REPLAY (a replayed frame, SOMVR_INPUT_REPLAY): the source XrTime it shows and the remap's turn - for the compare
 */
#ifndef SOMVR_INREC_H
#define SOMVR_INREC_H

#include <stddef.h>
#include <stdint.h>

#include "somvr_hands.h"

#define SOMVR_IR_MAGIC "SOMVRIN1"
#define SOMVR_IR_VERSION 1u
#define SOMVR_IR_ORCS 6

enum { SOMVR_IR_FRAME = 1, SOMVR_IR_STATE = 2, SOMVR_IR_BLADE = 3, SOMVR_IR_HIT = 4, SOMVR_IR_MARK = 5,
       SOMVR_IR_CFG = 6, SOMVR_IR_NOTE = 7, SOMVR_IR_REPLAY = 8, SOMVR_IR_NTYPE = 9 };
enum { SOMVR_IRF_LEFTY = 1u,       /* frame flags: SOMVR_HANDED=left (the roles are swapped after the record) */
       SOMVR_IRF_FRESH = 2u,       /* the views were located this frame (else reused / extrapolated) */
       SOMVR_IRF_SNAP = 4u,        /* the snapshot was read (the controller block is the layer's) */
       SOMVR_IRF_REPLAYED = 8u,    /* SOMVR_INPUT_REPLAY gave this frame's head, grips and controls */
       SOMVR_IRF_TESTHANDS = 16u,  /* a SOMVR_HANDS_TEST pose or squeeze was in this frame */
       SOMVR_IRF_STEREO = 32u };   /* the frame was a complete stereo frame */
enum { SOMVR_IRS_CROUCH = 1u, SOMVR_IRS_SWORD = 2u, SOMVR_IRS_BOW = 4u, SOMVR_IRS_DAGGER = 8u, SOMVR_IRS_CLIMB_L = 16u,
       SOMVR_IRS_CLIMB_R = 32u, SOMVR_IRS_GRAB = 64u, SOMVR_IRS_FINISH = 128u, SOMVR_IRS_PRACTICE = 256u,
       SOMVR_IRS_PAUSED = 512u, SOMVR_IRS_VRMENU = 1024u, SOMVR_IRS_HEALTH = 2048u /* health known */,
       SOMVR_IRS_BODY = 4096u /* Talion's object read */ };
enum { SOMVR_IRO_DEAD = 1u, SOMVR_IRO_SYNC = 2u, SOMVR_IRO_PRACTICE = 4u, SOMVR_IRO_HEALTH = 8u };
enum { SOMVR_IRB_VALID = 1u, SOMVR_IRB_PUSHED = 2u };
enum { SOMVR_IRC_SCCFG = 1u };

typedef struct {
    char magic[8];       /*  0: "SOMVRIN1" */
    uint32_t version;    /*  8: SOMVR_IR_VERSION */
    uint32_t hdr_size;   /* 12: 64 */
    double t0;           /* 16: the game clock at the start */
    int64_t unix_t0;     /* 24: time(NULL) at the start */
    int32_t pid;         /* 32 */
    uint32_t flags;      /* 36: bit 0 = a mark's segment (marks/<id>/input.inrec), bit 1 = a cut (scripts/inrec.py) */
    uint32_t frame_size; /* 40: sizeof(somvr_ir_frame) of the writer (a reader checks it) */
    uint32_t pad;        /* 44 */
    char build[16];      /* 48: __DATE__ of the writer (logs only) */
} somvr_ir_hdr;

typedef struct {
    uint16_t type, size;
} somvr_ir_rh;

typedef struct {         /* one controller, physical (0 left, 1 right) */
    float p[3], q[4];    /*   0: the grip as the logic got it (XR space, m, x y z w) */
    float lin[3], ang[3];/*  28: its velocities (m/s, rad/s, XR space) */
    float aim_p[3], aim_q[4]; /* 52: the layer's snapshot's aim pose (at snap_time) */
    float trigger, squeeze, stick[2]; /* 80 */
    uint32_t buttons;    /*  96: SOMVR_BTN_* */
    uint8_t valid, tracked, vel_valid, src; /* 100: somvr_hand's */
    uint8_t grip_flags, aim_flags, vel_flags, active; /* 104: somvr_hand_in's */
    uint8_t presses[8];  /* 108: SOMVR_NBTN rising-edge counters */
} somvr_ir_hand;         /* 116 */

typedef struct {
    somvr_ir_rh h;       /*   0 */
    uint32_t frame;      /*   4: the renderer's frame id */
    double t;            /*   8: the game clock */
    int64_t xr_time;     /*  16: the views' XrTime (the grips' too) */
    int64_t snap_time;   /*  24: the snapshot's XrTime (0 = none) */
    float eye_p[2][3];   /*  32: the eye poses (XR space) */
    float eye_q[2][4];   /*  56 */
    float rig_p0[3];     /*  88: the rig (game convention, cm / rad): P0 */
    float rig_yaw0;      /* 100 */
    float base[3];       /* 104: the frame's base B (cm, the world) */
    float base_yaw;      /* 116: its heading psi */
    float scale;         /* 120: the player size (1 = 1:1) */
    uint32_t flags;      /* 124: SOMVR_IRF_* */
    uint16_t profile[2]; /* 128 */
    uint32_t in_flags;   /* 132: the snapshot's SOMVR_INS_* */
    somvr_ir_hand hand[2]; /* 136 */
} somvr_ir_frame;        /* 368 */

typedef struct {
    float pos[3], yaw, dist, health; /* cm (the world), rad, cm from Talion's root, the game's health (SOMVR_IRO_HEALTH) */
    uint32_t id;         /* the orc's HOBJECT, its low 32 bits (the same orc across samples) */
    uint32_t flags;      /* SOMVR_IRO_* */
} somvr_ir_orc;          /* 32 */

typedef struct {
    somvr_ir_rh h;       /*   0 */
    uint32_t n_orcs;     /*   4 */
    double t;            /*   8 */
    int64_t xr_time;     /*  16: the last frame's */
    float root[3], yaw;  /*  24: Talion's root (cm, the world), heading (rad) */
    float health, health_max; /* 40: the health now and the most seen */
    uint32_t flags;      /*  48: SOMVR_IRS_* */
    uint32_t ctx;        /*  52: the controls' context (SC_CTX_*) */
    somvr_ir_orc orc[SOMVR_IR_ORCS]; /* 56, nearest first */
} somvr_ir_state;        /* 248 */

typedef struct {
    somvr_ir_rh h;       /*  0 */
    uint32_t frame;      /*  4 */
    double t;            /*  8: the blade's time (somvr_blade.t) */
    float base[3], tip[3], vel_tip[3], vel_base[3], flat_n[3]; /* 16: somvr_blade's (cm, cm/s, the world) */
    uint32_t flags;      /* 76: SOMVR_IRB_*: 0 = no blade (the tracker reset) */
} somvr_ir_blade;        /* 80 */

typedef struct {
    somvr_ir_rh h;       /*  0 */
    uint32_t orc;        /*  4: the orc's id (somvr_ir_orc.id) */
    double t;            /*  8 */
    int32_t tier;        /* 16: SC_TIER_* dealt */
    float along, edge_deg, blow_j, contact_mps, k; /* 20: where on the blade, its edge, the energy (J; NaN = none), the
                                                    * contact point's speed (m/s), the damage factor */
    uint32_t frame;      /* 40 */
    uint32_t kind;       /* 44: SOMVR_IRH_DEALT (combat's own hit, its final tier) or _CONTACT (the tier as decided at a
                          * contact, before the stealth kill, the verbs, the hold's kill or the execution may take it) */
} somvr_ir_hit;          /* 48 */
enum { SOMVR_IRH_DEALT = 0u, SOMVR_IRH_CONTACT = 1u };

typedef struct {
    somvr_ir_rh h;       /*  0 */
    uint32_t n;          /*  4: the mark's number */
    double t;            /*  8 */
    char name[32];       /* 16: its folder's name */
} somvr_ir_mark;         /* 48 */

typedef struct {
    somvr_ir_rh h;       /* 0 */
    uint32_t id;         /* 4: SOMVR_IRC_* */
    uint32_t len;        /* 8: the blob's bytes (after this header) */
    uint32_t pad;        /* 12 */
} somvr_ir_cfg;          /* 16 + len, padded to 8 */

typedef struct {
    somvr_ir_rh h;       /* 0 */
    uint32_t pad;        /* 4 */
    double t;            /* 8 */
} somvr_ir_note;         /* 16 + the text (NUL-terminated), padded to 8 */

typedef struct {
    somvr_ir_rh h;       /*  0 */
    uint32_t frame;      /*  4 */
    int64_t src_time;    /*  8: the source's XrTime this frame showed */
    int64_t src_t0;      /* 16: the source's first replayed XrTime */
    float rot;           /* 24: the remap's turn about +Y (game convention, rad) */
    float kappa;         /* 28: its part latched at the start (psi_src - psi_live) */
    float p0_src[3];     /* 32: the remap's two origins (game convention, cm) */
    float p0_live[3];    /* 44 */
    uint32_t flags;      /* 56: bit 0 = interpolated between two source frames, bit 1 = past the source's end (held) */
    uint32_t pad;        /* 60 */
} somvr_ir_replay;       /* 64 */

_Static_assert(sizeof(somvr_ir_hdr) == 64, "inrec header");
_Static_assert(sizeof(somvr_ir_hand) == 116 && offsetof(somvr_ir_hand, buttons) == 96 &&
                   offsetof(somvr_ir_hand, presses) == 108, "inrec hand");
_Static_assert(sizeof(somvr_ir_frame) == 368 && offsetof(somvr_ir_frame, hand) == 136 &&
                   offsetof(somvr_ir_frame, rig_p0) == 88 && offsetof(somvr_ir_frame, flags) == 124, "inrec frame");
_Static_assert(sizeof(somvr_ir_state) == 248 && offsetof(somvr_ir_state, orc) == 56, "inrec state");
_Static_assert(sizeof(somvr_ir_blade) == 80 && sizeof(somvr_ir_hit) == 48 && sizeof(somvr_ir_mark) == 48 &&
                   sizeof(somvr_ir_cfg) == 16 && sizeof(somvr_ir_note) == 16 && sizeof(somvr_ir_replay) == 64,
               "inrec records");

/* ---- writing ------------------------------------------------------------------------------------------------------ */

void somvr_ir_hdr_init(somvr_ir_hdr *h, double t0, int64_t unix_t0, int32_t pid, uint32_t flags);
/* one controller's record from the grip as located (may be NULL or invalid) and the snapshot's hand (may be NULL);
 * squeeze >= 0 replaces the snapshot's (a test squeeze) */
void somvr_ir_hand_put(somvr_ir_hand *o, const somvr_hand *h, const somvr_hand_in *in, float squeeze);
/* a NOTE record of text into buf (cap bytes): its size (0 = does not fit) */
size_t somvr_ir_note_make(void *buf, size_t cap, double t, const char *text);
/* a CFG record of len bytes into buf: its size (0 = does not fit) */
size_t somvr_ir_cfg_make(void *buf, size_t cap, uint32_t id, const void *blob, uint32_t len);

/* ---- reading ------------------------------------------------------------------------------------------------------ */

/* 1 = a header of this format (magic, version 1.., hdr_size 64.., frame_size = ours) */
int somvr_ir_hdr_ok(const somvr_ir_hdr *h);
/* the next record at off in buf[0..len): its header, 1 = whole and sane (size >= 4, a multiple of 8, inside) */
int somvr_ir_next(const void *buf, size_t len, size_t off, somvr_ir_rh *rh);
/* the controller's record back as a located grip (time = the XrTime it is for) and a snapshot hand (either NULL) */
void somvr_ir_hand_get(const somvr_ir_hand *o, int64_t time, somvr_hand *h, somvr_hand_in *in);
/* the index of the last frame with xr_time <= t in f[0..n) (sorted by xr_time); -1 = before the first */
int somvr_ir_find(const somvr_ir_frame *f, int n, int64_t t);
/* the frame at XrTime t: between f[i] and f[i+1] (u = where), positions and analog values linear, orientations
 * nlerp'd on the short arc, the discrete values (buttons, flags, presses, the rig) of the earlier one; before the
 * first / after the last = that frame held. *flags_out: bit 0 interpolated, bit 1 past the end (may be NULL) */
void somvr_ir_sample(const somvr_ir_frame *f, int n, int64_t t, somvr_ir_frame *out, uint32_t *flags_out);

/* ---- the replay's remap (re-xr-pipeline.md 160.3) -----------------------------------------------------------------
 * A recorded XR pose is moved into the live XR space so that it keeps its place relative to the rig that mapped it:
 * game convention g' = P0_live + R(rot) (g - P0_src), q' = Y(rot) (x) q, with rot = (yaw0_live - yaw0_src) + kappa and
 * kappa = psi_src - psi_live latched at the replay's start. Then the live rig maps g' to the world exactly as the
 * recording's mapped g, at the live base: the head and the hands stand to Talion (and, while psi changes alike in
 * both, to the world's directions) as the player's did. In XR space this is a turn by -rot about +Y. */
typedef struct {
    float rot, kappa;
    float p0_src[3], p0_live[3]; /* game convention, cm */
} somvr_ir_map;
void somvr_ir_map_make(somvr_ir_map *m, const float p0_src[3], float yaw0_src, const float p0_live[3], float yaw0_live,
                       float kappa);
void somvr_ir_map_pose(const somvr_ir_map *m, const float p[3], const float q[4], float op[3], float oq[4]);
void somvr_ir_map_vec(const somvr_ir_map *m, const float v[3], float o[3]); /* a velocity (linear or angular) */
/* the whole frame's poses (the eyes, both grips and aims, their velocities) through m, in place */
void somvr_ir_map_frame(const somvr_ir_map *m, somvr_ir_frame *f);
/* a press counter replayed onto the live one: live_base + (now - start), mod 256 */
static inline uint8_t somvr_ir_press(uint8_t live_base, uint8_t start, uint8_t now) {
    return (uint8_t)(live_base + (uint8_t)(now - start));
}

#endif
