/* test_verbs.c - SOMVR_VERBS's pure part (somvr_verbs.h): the left hand's grab on an orc's head, the record kinds */
#include "../somvr_verbs.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail, g_n;
#define CHECK(c, ...)                                                                                                      \
    do {                                                                                                                   \
        g_n++;                                                                                                             \
        if (!(c)) {                                                                                                        \
            g_fail++;                                                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                    \
            printf(__VA_ARGS__);                                                                                           \
            printf("\n");                                                                                                  \
        }                                                                                                                  \
    } while (0)

static somvr_vb_cfg C;

/* one orc's head at (0, 180, 100); the eyes at (0, 175, 40) (60 cm off); the palm at (x, y, z) */
static void in_at(somvr_vb_hand_in *in, double t, float sq, float tr, float x, float y, float z)
{
    memset(in, 0, sizeof *in);
    in->t = t, in->live = 1, in->squeeze = sq, in->trigger = tr, in->have_palm = 1;
    in->palm[0] = x, in->palm[1] = y, in->palm[2] = z;
    in->have_eye = 1, in->eye[0] = 0, in->eye[1] = 175, in->eye[2] = 40;
    in->norc = 1;
    in->head[0][0] = 0, in->head[0][1] = 180, in->head[0][2] = 100;
}
static void step(somvr_vb_hand *s, somvr_vb_hand_in *in, somvr_vb_hand_out *o) { somvr_vb_hand_step(s, &C, in, o); }

static void test_grab_hold_release(void)
{
    somvr_vb_hand s;
    somvr_vb_hand_in in;
    somvr_vb_hand_out o;
    somvr_vb_hand_reset(&s);
    in_at(&in, 1.0, 0, 0, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(!o.grab && !o.b && o.event == SOMVR_VBE_NONE && s.phase == SOMVR_VBH_IDLE && !somvr_vb_trigger_taken(&s),
          "open hand at the head: nothing");
    in_at(&in, 1.05, 0.8f, 0, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(o.grab && !o.b && o.event == SOMVR_VBE_GRAB && o.orc == 0 && o.dist_cm < 10 && !o.by_cone &&
              s.phase == SOMVR_VBH_GRAB && somvr_vb_trigger_taken(&s),
          "a new squeeze on the head: the grab (%d %d %.1f)", o.grab, o.event, o.dist_cm);
    for (int k = 1; k <= 10; k++) { /* the orc moves with the game's grab: the hand may leave its head */
        in_at(&in, 1.05 + 0.05 * k, 0.5f, 0, 30, 140, 90);
        step(&s, &in, &o);
        CHECK(o.grab && !o.b && o.event == SOMVR_VBE_NONE, "held at %d (the grip 0.5 > off)", k);
    }
    /* the wraith hand while holding: the trigger = B, the game's end lets it go, a new press again */
    in_at(&in, 1.7, 0.9f, 0.9f, 30, 140, 90);
    step(&s, &in, &o);
    CHECK(o.grab && o.b && o.event == SOMVR_VBE_WRAITH, "the trigger while holding: B (%d %d)", o.b, o.event);
    in_at(&in, 2.0, 0.9f, 0.5f, 30, 140, 90);
    step(&s, &in, &o);
    CHECK(o.grab && o.b, "the trigger at 0.5 (> off): B held");
    in_at(&in, 2.1, 0.9f, 0.9f, 30, 140, 90);
    in.game_done = 1;
    step(&s, &in, &o);
    CHECK(o.grab && !o.b && o.event == SOMVR_VBE_DONE && fabsf(o.held_s - 0.4f) < 1e-3f, "the game's end: B let go (%.2f)",
          o.held_s);
    in_at(&in, 2.2, 0.9f, 0.9f, 30, 140, 90);
    step(&s, &in, &o);
    CHECK(o.grab && !o.b, "still held after the end: no B");
    in_at(&in, 2.3, 0.9f, 0.1f, 30, 140, 90);
    step(&s, &in, &o);
    in_at(&in, 2.4, 0.9f, 0.9f, 30, 140, 90);
    step(&s, &in, &o);
    CHECK(o.grab && o.b && o.event == SOMVR_VBE_WRAITH, "a new press: B again");
    in_at(&in, 2.5, 0.2f, 0.9f, 30, 140, 90);
    step(&s, &in, &o);
    CHECK(!o.grab && !o.b && o.event == SOMVR_VBE_RELEASE && fabsf(o.held_s - 1.45f) < 1e-3f && s.phase == SOMVR_VBH_IDLE &&
              !somvr_vb_trigger_taken(&s),
          "the grip opened: released after %.2f s, B too", o.held_s);
    in_at(&in, 2.6, 0.9f, 0, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(!o.grab && o.event == SOMVR_VBE_NONE, "within the cooldown: no new grab");
    in_at(&in, 2.7, 0.0f, 0, 5, 175, 95);
    step(&s, &in, &o);
    in_at(&in, 3.0, 0.9f, 0.9f, 5, 175, 95); /* the grip and the trigger at once */
    step(&s, &in, &o);
    CHECK(o.grab && !o.b && o.event == SOMVR_VBE_GRAB, "grip and trigger at once: the grab, no B");
    in_at(&in, 3.1, 0.9f, 0.9f, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(o.grab && !o.b, "... the trigger counts from its next press");
    in_at(&in, 3.2, 0.9f, 0.0f, 5, 175, 95);
    step(&s, &in, &o);
    in_at(&in, 3.3, 0.9f, 0.9f, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(o.grab && o.b, "... and then B");
    in_at(&in, 3.3 + C.hold_max_s + 0.01, 0.9f, 0.9f, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(o.grab && !o.b && o.event == SOMVR_VBE_DONE, "hold_max: B let go");
    in_at(&in, 10.0, 0.9f, 0.0f, 5, 175, 95);
    in.live = 0;
    step(&s, &in, &o);
    CHECK(!o.grab && !o.b && o.event == SOMVR_VBE_LOST && s.phase == SOMVR_VBH_IDLE, "not live while held: lost");
}

static void test_wraith_open(void)
{
    somvr_vb_hand s;
    somvr_vb_hand_in in;
    somvr_vb_hand_out o;
    somvr_vb_hand_reset(&s);
    in_at(&in, 1.0, 0, 0.9f, 5, 175, 95); /* the open hand on the head, the trigger: B (the stun's tap or the drain) */
    step(&s, &in, &o);
    CHECK(!o.grab && o.b && o.event == SOMVR_VBE_WRAITH && s.phase == SOMVR_VBH_WRAITH && somvr_vb_trigger_taken(&s),
          "the open hand's trigger on a head: B (%d %d)", o.b, o.event);
    in_at(&in, 1.1, 0, 0.9f, 60, 120, 30); /* moved away: B stays (the game's hold has the orc) */
    step(&s, &in, &o);
    CHECK(o.b, "moved away: B held");
    in_at(&in, 1.15, 0, 0.1f, 60, 120, 30);
    step(&s, &in, &o);
    CHECK(!o.b && o.event == SOMVR_VBE_WRAITH_END && fabsf(o.held_s - 0.15f) < 1e-3f && s.phase == SOMVR_VBH_IDLE,
          "let go after %.2f s (a tap: the game's stun)", o.held_s);
    in_at(&in, 2.0, 0, 0.9f, 80, 100, 0); /* the trigger far from every head: the bow's */
    step(&s, &in, &o);
    CHECK(!o.b && o.event == SOMVR_VBE_NONE && !somvr_vb_trigger_taken(&s), "the trigger elsewhere: nothing (the bow's)");
    in_at(&in, 2.1, 0, 0.0f, 80, 100, 0);
    step(&s, &in, &o);
    in_at(&in, 2.2, 0, 0.9f, 80, 100, 0);
    step(&s, &in, &o);
    for (int k = 1; k <= 5; k++) { /* a held trigger moved onto the head holds nothing */
        in_at(&in, 2.2 + 0.05 * k, 0, 0.9f, 80 - 15 * k, 100 + 15 * k, 19 * k);
        step(&s, &in, &o);
        CHECK(!o.b, "a held trigger moved onto the head: nothing (%d)", k);
    }
    in_at(&in, 3.0, 0, 0.9f, 5, 175, 95);
    in.game_done = 1;
    in_at(&in, 3.0, 0, 0.0f, 5, 175, 95);
    step(&s, &in, &o);
    in_at(&in, 3.5, 0, 0.9f, 5, 175, 95);
    step(&s, &in, &o);
    in_at(&in, 4.0, 0, 0.9f, 5, 175, 95);
    in.game_done = 1;
    step(&s, &in, &o);
    CHECK(!o.b && o.event == SOMVR_VBE_DONE && s.phase == SOMVR_VBH_WRAITH, "the game's end: B let go, still the wraith's");
    in_at(&in, 4.2, 0, 0.0f, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(o.event == SOMVR_VBE_WRAITH_END && s.phase == SOMVR_VBH_IDLE, "the trigger let go: idle");
}

static void test_fist_miss_far(void)
{
    somvr_vb_hand s;
    somvr_vb_hand_in in;
    somvr_vb_hand_out o;
    somvr_vb_hand_reset(&s);
    in_at(&in, 1.0, 0.9f, 0, 80, 100, 0); /* closed far away */
    step(&s, &in, &o);
    CHECK(!o.grab && o.event == SOMVR_VBE_NONE && o.edge, "a squeeze far away: nothing (%d)", o.event);
    for (int k = 1; k <= 10; k++) { /* the fist moved onto the head */
        float f = k / 10.0f;
        in_at(&in, 1.0 + 0.05 * k, 0.9f, 0, 80 * (1 - f) + 5 * f, 100 * (1 - f) + 175 * f, 95 * f);
        step(&s, &in, &o);
        CHECK(!o.grab && o.event == SOMVR_VBE_NONE, "a fist moved onto the head holds nothing (%d)", k);
    }
    in_at(&in, 2.0, 0.0f, 0, 5, 175, 95);
    step(&s, &in, &o);
    in_at(&in, 2.1, 0.9f, 0, 40, 175, 125); /* 50 cm from the head, out of the cone (70 deg off): a near miss */
    step(&s, &in, &o);
    CHECK(!o.grab && o.event == SOMVR_VBE_MISS && o.dist_cm > 40 && o.dist_cm < 80, "a near miss (%.1f)", o.dist_cm);
    in_at(&in, 2.2, 0.0f, 0, 0, 0, 0);
    step(&s, &in, &o);
    in_at(&in, 2.3, 0.9f, 0, 0, 60, 0);
    step(&s, &in, &o);
    CHECK(!o.grab && o.event == SOMVR_VBE_NONE, "far: no miss either");
    somvr_vb_hand_reset(&s);
    in_at(&in, 3.0, 0.6f, 0, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(!o.grab, "0.6 is not closed");
    in_at(&in, 3.05, 0.7f, 0, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(o.grab && o.event == SOMVR_VBE_GRAB, "0.7 closes");
    in_at(&in, 3.1, NAN, NAN, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(!o.grab && o.event == SOMVR_VBE_RELEASE, "NaN = open");
    somvr_vb_hand_reset(&s);
    in_at(&in, 4.0, 0.9f, 0, 5, 175, 95);
    in.have_palm = 0;
    step(&s, &in, &o);
    CHECK(!o.grab, "no palm: nothing");
    somvr_vb_hand_reset(&s);
    in_at(&in, 4.0, 0.9f, 0, 5, 175, 95);
    in.live = 0;
    step(&s, &in, &o);
    CHECK(!o.grab, "not live: nothing");
    somvr_vb_hand_reset(&s);
    in_at(&in, 4.0, 0.9f, 0, 5, 175, 95);
    in.norc = 0;
    step(&s, &in, &o);
    CHECK(!o.grab && o.event == SOMVR_VBE_NONE, "no orc: nothing");
    /* two orcs: the nearer head; a NaN head skipped */
    somvr_vb_hand_reset(&s);
    in_at(&in, 20.0, 0.9f, 0, 5, 175, 95);
    in.norc = 2;
    in.head[1][0] = 10, in.head[1][1] = 172, in.head[1][2] = 92;
    step(&s, &in, &o);
    CHECK(o.event == SOMVR_VBE_GRAB && o.orc == 1, "the nearer head: %d", o.orc);
    in.head[1][0] = NAN;
    somvr_vb_hand_reset(&s);
    step(&s, &in, &o);
    CHECK(o.event == SOMVR_VBE_GRAB && o.orc == 0, "a NaN head is skipped: %d", o.orc);
}

/* the arm reached out at an orc's head ~1 m off (melee): the eyes at (0, 170, 0), the head at (0, 180, 110) */
static void test_cone(void)
{
    somvr_vb_hand s;
    somvr_vb_hand_in in;
    somvr_vb_hand_out o;
#define EYES0 in.eye[0] = 0, in.eye[1] = 170, in.eye[2] = 0
    somvr_vb_hand_reset(&s);
    in_at(&in, 1.0, 0.9f, 0, 3, 172, 60); /* 50 cm out, on the line: 50 cm from the head (out of reach) */
    in.head[0][2] = 110, EYES0;
    step(&s, &in, &o);
    CHECK(o.event == SOMVR_VBE_GRAB && o.by_cone && o.cone_deg >= 0 && o.cone_deg < 5 && o.dist_cm > 40,
          "reached out at the head: a grab by the cone (%d %.1f deg %.1f cm)", o.event, o.cone_deg, o.dist_cm);
    somvr_vb_hand_reset(&s);
    in_at(&in, 1.0, 0.9f, 0, 30, 172, 50); /* out 58 cm, 31 deg off */
    in.head[0][2] = 110, EYES0;
    step(&s, &in, &o);
    CHECK(o.event != SOMVR_VBE_GRAB && !o.grab, "31 deg off: no grab (%d)", o.event);
    somvr_vb_hand_reset(&s);
    in_at(&in, 1.0, 0.9f, 0, 2, 171, 25); /* only 25 cm out: no cone */
    in.head[0][2] = 110, EYES0;
    step(&s, &in, &o);
    CHECK(o.event != SOMVR_VBE_GRAB, "the hand not out: no cone (%d)", o.event);
    somvr_vb_hand_reset(&s);
    in_at(&in, 1.0, 0.9f, 0, 3, 172, 60);
    in.head[0][2] = 230, EYES0; /* 230 cm off: too far for the cone */
    step(&s, &in, &o);
    CHECK(o.event != SOMVR_VBE_GRAB, "too far: no cone (%d)", o.event);
    somvr_vb_hand_reset(&s);
    in_at(&in, 1.0, 0.9f, 0, 40, 172, 40);
    in.norc = 2, EYES0;
    in.head[0][0] = 0, in.head[0][1] = 180, in.head[0][2] = 110; /* 45 deg off the reach */
    in.head[1][0] = 45, in.head[1][1] = 175, in.head[1][2] = 50; /* 11 cm from the palm */
    step(&s, &in, &o);
    CHECK(o.event == SOMVR_VBE_GRAB && o.orc == 1 && !o.by_cone, "on a head first (%d %d)", o.orc, o.by_cone);
    somvr_vb_hand_reset(&s); /* the open hand's trigger reached out at a head: the wraith hand */
    in_at(&in, 1.0, 0, 0.9f, 3, 172, 60);
    in.head[0][2] = 110, EYES0;
    step(&s, &in, &o);
    CHECK(o.event == SOMVR_VBE_WRAITH && o.b && o.by_cone, "the trigger reached out at a head: B (%d)", o.event);
#undef EYES0
}

/* the interrogation: the grabbed orc pulled to the face (the eyes at 0,175,40) */
static void test_ask(void)
{
    somvr_vb_hand s;
    somvr_vb_hand_in in;
    somvr_vb_hand_out o;
    somvr_vb_hand_reset(&s);
    in_at(&in, 1.0, 0.9f, 0, 5, 175, 95); /* grabbed 55 cm from the eyes */
    step(&s, &in, &o);
    CHECK(o.event == SOMVR_VBE_GRAB, "the grab");
    in_at(&in, 1.1, 0.9f, 0, 3, 172, 70); /* 30 cm: near */
    step(&s, &in, &o);
    CHECK(!o.ask && o.grab, "near: not yet");
    in_at(&in, 1.3, 0.9f, 0, 3, 172, 68);
    step(&s, &in, &o);
    CHECK(!o.ask, "0.2 s: not yet");
    in_at(&in, 1.36, 0.9f, 0, 3, 172, 68);
    step(&s, &in, &o);
    CHECK(o.ask && o.grab && o.event == SOMVR_VBE_ASK, "0.26 s near the face: A (%d)", o.event);
    in_at(&in, 1.5, 0.9f, 0, 5, 175, 95);
    step(&s, &in, &o);
    in_at(&in, 1.6, 0.9f, 0, 3, 172, 68);
    step(&s, &in, &o);
    in_at(&in, 2.0, 0.9f, 0, 3, 172, 68);
    step(&s, &in, &o);
    CHECK(!o.ask, "once per grab");
    somvr_vb_hand_reset(&s); /* grabbed right at the face (an orc very close): no ask until the hand has been away */
    in_at(&in, 5.0, 0.9f, 0, 5, 176, 62);
    in.head[0][2] = 65;
    step(&s, &in, &o);
    CHECK(o.event == SOMVR_VBE_GRAB, "a grab at the face");
    in_at(&in, 5.5, 0.9f, 0, 5, 176, 62);
    in.head[0][2] = 65;
    step(&s, &in, &o);
    CHECK(!o.ask, "never away: no ask");
}

static void test_kinds(void)
{
    static const struct { const char *n; int k; } t[] = {
        { "PC_HoG_Standing_Start_F", SOMVR_VBK_HOG },
        { "PC_HoG_Standing_Hold_Normal", SOMVR_VBK_HOG },
        { "PC_HoG_Standing_Hold_Normal_LowHealth", SOMVR_VBK_HOG },
        { "PC_HoG_Standing_NoTrgt_Start_F", SOMVR_VBK_HOG },
        { "PC_HoG_Standing_Complete", SOMVR_VBK_HOG_END },
        { "PC_HoG_Standing_Out_Normal", SOMVR_VBK_HOG_END },
        { "PC_HoG_Standing_Out", SOMVR_VBK_HOG_END },
        { "PL_St_L1_D1_F_LL_LL_St_1_HC_Lowest", SOMVR_VBK_STUN },
        { "PC_Grab_Into_F", SOMVR_VBK_GRAB },
        { "PC_Grab_Attempt_Held", SOMVR_VBK_GRAB },
        { "PC_Interrogation_Into", SOMVR_VBK_ASK },
        { "PC_Bip_Sync_Intrgtn_Into_NoBrand", SOMVR_VBK_ASK },
        { "PC_Grab_HOG_Into", SOMVR_VBK_HOG },
        { "PC_Grab_HOG_Into_Kill", SOMVR_VBK_HOG_END },
        { "PC_Grab_HOG_Out", SOMVR_VBK_HOG_END },
        { "PC_GrabFlurry_1", SOMVR_VBK_GRAB },
        { "PC_Bip_SneakInstaKill_Silent_B", SOMVR_VBK_STEALTH },
        { "PC_Bip_StealthTorment_B", SOMVR_VBK_STEALTH },
        { "PC_Bip_StealthBrand_F", SOMVR_VBK_STEALTH },
        { "PC_Recoil_Light_R2L_F", SOMVR_VBK_NONE },
        { "PC_CounterAttempt_Fail", SOMVR_VBK_NONE },
        { "", SOMVR_VBK_NONE },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        CHECK(somvr_vb_attack_kind(t[i].n) == t[i].k, "'%s' -> %d (want %d)", t[i].n, somvr_vb_attack_kind(t[i].n), t[i].k);
    CHECK(somvr_vb_attack_kind(NULL) == SOMVR_VBK_NONE, "NULL");
    CHECK(!strcmp(somvr_vb_event_name(SOMVR_VBE_GRAB), "grab") && !strcmp(somvr_vb_event_name(99), "?") &&
              !strcmp(somvr_vb_kind_name(SOMVR_VBK_STUN), "the stun"),
          "names");
}

/* build 19b: a stealth target's back (SOMVR_STEALTH_KILL_GRAB_CM). The stand-off's geometry (run 20261028-015924:
 * Talion's root at 0, the orc's root - its pelvis - 118 cm ahead, its head node ~80 cm over it and ~27 cm further on:
 * the palm 105 cm from it while 54 cm from the orc's axis); the eyes ducked (26 cm over the root) or the stick's crouch
 * (77), leaned in 30 cm (the head tether's start is 50) or not; the left hand where a real arm puts it (at most ~70 cm
 * from the left shoulder: 18 cm left of and 22 cm under the eyes) */
static void back_at(somvr_vb_hand_in *in, float eye_up, float lean, float x, float y, float z, float body_cm)
{
    memset(in, 0, sizeof *in);
    in->t = 1.0, in->live = 1, in->squeeze = 0.9f, in->have_palm = 1;
    in->palm[0] = x, in->palm[1] = y, in->palm[2] = z;
    in->have_eye = 1, in->eye[0] = 0, in->eye[1] = eye_up, in->eye[2] = lean;
    in->norc = 1;
    in->head[0][0] = 0, in->head[0][1] = 80, in->head[0][2] = 145;
    in->body[0][0] = 0, in->body[0][1] = 0, in->body[0][2] = 118;
    in->body_cm[0] = body_cm;
}
static float arm_cm(const somvr_vb_hand_in *in)
{
    float dx = in->palm[0] - (in->eye[0] - 18), dy = in->palm[1] - (in->eye[1] - 22), dz = in->palm[2] - in->eye[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
static void test_back(void)
{
    somvr_vb_hand s;
    somvr_vb_hand_in in;
    somvr_vb_hand_out o;
    static const struct { const char *what; float eye_up, lean, x, y, z; int old_takes; } P[] = {
        /* ducked, leaned 30: the palm on its lower back (the back's skin ~96 cm ahead) */
        { "ducked, leaned: the hand on its lower back", 26, 30, -10, 20, 92, 0 },
        /* the stick's crouch (the eyes standing), leaned 30: on its mid back */
        { "standing eyes, leaned: the hand on its mid back", 77, 30, -10, 40, 94, 0 },
        /* ducked, not leaned: the arm out at its mid back (short of it) */
        { "ducked: the arm out at its back, not touching", 26, 0, -14, 22, 62, 0 },
        /* ducked, leaned: the arm up at its shoulder blade (short of it: the head's cone takes it already) */
        { "ducked, leaned: the arm up at its shoulder blade", 26, 30, -12, 45, 85, 1 },
    };
    for (unsigned k = 0; k < sizeof P / sizeof P[0]; k++) {
        back_at(&in, P[k].eye_up, P[k].lean, P[k].x, P[k].y, P[k].z, 0);
        CHECK(arm_cm(&in) <= 72, "%s: a real arm reaches it (%.0f cm from the shoulder)", P[k].what, arm_cm(&in));
        somvr_vb_hand_reset(&s);
        step(&s, &in, &o);
        CHECK((o.event == SOMVR_VBE_GRAB) == P[k].old_takes && !o.by_body && o.back_cm < 0,
              "%s: the head only (build 19) %s (%d, %.1f cm from the head, %.1f deg)", P[k].what,
              P[k].old_takes ? "takes it" : "takes nothing", o.event, o.dist_cm, o.cone_deg);
        back_at(&in, P[k].eye_up, P[k].lean, P[k].x, P[k].y, P[k].z, 45);
        somvr_vb_hand_reset(&s);
        step(&s, &in, &o);
        CHECK(o.event == SOMVR_VBE_GRAB && o.grab && o.orc == 0 && o.back_cm >= 0,
              "%s: with its back offered: taken (%d, by the back %d, %.1f cm / %.1f deg off it)", P[k].what, o.event,
              o.by_body, o.back_cm, o.back_deg);
    }
    /* the hand at the hip (the dagger's slot) or held low by the side: never its back */
    back_at(&in, 26, 0, -25, -30, 10, 45);
    somvr_vb_hand_reset(&s);
    step(&s, &in, &o);
    CHECK(o.event != SOMVR_VBE_GRAB, "the hand at the hip: nothing (%d, %.1f cm, %.1f deg)", o.event, o.back_cm, o.back_deg);
    back_at(&in, 26, 0, -45, 0, 30, 45);
    somvr_vb_hand_reset(&s);
    step(&s, &in, &o);
    CHECK(o.event != SOMVR_VBE_GRAB, "the hand out to the side: nothing (%d, %.1f deg)", o.event, o.back_deg);
    /* on its head: still the head's grab (the nearer score) */
    back_at(&in, 26, 30, 0, 72, 140, 45);
    somvr_vb_hand_reset(&s);
    step(&s, &in, &o);
    CHECK(o.event == SOMVR_VBE_GRAB && !o.by_body && !o.by_cone, "on its head: the head's grab (%d %d)", o.by_body, o.by_cone);
    /* not a stealth target (body_cm 0): the back takes nothing, the line's numbers unset */
    back_at(&in, 26, 30, -10, 20, 92, 0);
    in.body_cm[0] = -1;
    somvr_vb_hand_reset(&s);
    step(&s, &in, &o);
    CHECK(o.event != SOMVR_VBE_GRAB && o.back_cm < 0, "not a target: nothing (%d)", o.event);
    /* the trigger with the open hand on its back: the wraith hand (the game's B behind it) */
    back_at(&in, 26, 30, -10, 20, 92, 45);
    in.squeeze = 0, in.trigger = 0.9f;
    somvr_vb_hand_reset(&s);
    step(&s, &in, &o);
    CHECK(o.event == SOMVR_VBE_WRAITH && o.by_body, "the open hand's trigger on its back: B (%d %d)", o.event, o.by_body);
}

/* build 21 (SOMVR_VERBS_CUE): the open hand's reach by the squeeze's rule, and the cue's on / tick / grace / fade */
static void test_cue(void)
{
    somvr_vb_hand s;
    somvr_vb_hand_in in;
    somvr_vb_hand_out o;
    int bb = -1;
    somvr_vb_hand_reset(&s);
    in_at(&in, 1.0, 0, 0, 5, 175, 95); /* the open hand at the head (5 cm) */
    step(&s, &in, &o);
    CHECK(somvr_vb_reach(&C, &s, &in, -1, 8, 5, &bb) == 0 && bb == 0, "the open hand on the head: in reach");
    in_at(&in, 1.0, 0, 0, 0, 120, 60); /* low by the hip, not reached out at it (the eyes 56 cm, 26 deg off) */
    step(&s, &in, &o);
    CHECK(somvr_vb_reach(&C, &s, &in, -1, 8, 5, &bb) == -1, "the hand by the hip: out of reach");
    /* the border: 50 cm from the head (reach 45), the cone too far off: out, but kept by an orc already shown */
    in_at(&in, 1.0, 0, 0, 0, 130, 100);
    step(&s, &in, &o);
    CHECK(somvr_vb_reach(&C, &s, &in, -1, 8, 5, &bb) == -1 && somvr_vb_reach(&C, &s, &in, 0, 8, 5, &bb) == 0,
          "50 cm: out of reach, kept by the shown orc (+8 cm)");
    in_at(&in, 1.0, 0, 0, 0, 120, 100);
    step(&s, &in, &o);
    CHECK(somvr_vb_reach(&C, &s, &in, 0, 8, 5, &bb) == -1, "60 cm: gone even for the shown orc");
    /* a fist at the head, a hand that holds, a hand not live: none */
    somvr_vb_hand_reset(&s);
    in_at(&in, 2.0, 0.9f, 0, 60, 175, 95);
    step(&s, &in, &o);
    in_at(&in, 2.05, 0.9f, 0, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(s.closed && s.phase == SOMVR_VBH_IDLE && somvr_vb_reach(&C, &s, &in, -1, 8, 5, &bb) == -1,
          "a fist moved onto the head: no cue (it holds nothing)");
    somvr_vb_hand_reset(&s);
    in_at(&in, 3.0, 0.9f, 0, 5, 175, 95);
    step(&s, &in, &o);
    CHECK(s.phase == SOMVR_VBH_GRAB && somvr_vb_reach(&C, &s, &in, -1, 8, 5, &bb) == -1, "holding: no cue");
    somvr_vb_hand_reset(&s);
    in_at(&in, 4.0, 0, 0, 5, 175, 95);
    in.live = 0;
    step(&s, &in, &o);
    CHECK(somvr_vb_reach(&C, &s, &in, -1, 8, 5, &bb) == -1, "not live (the bow, a wall, a menu): no cue");
    /* a stealth target's back: in reach by its back */
    back_at(&in, 26, 30, -10, 20, 92, 45);
    in.squeeze = 0;
    somvr_vb_hand_reset(&s);
    step(&s, &in, &o);
    CHECK(somvr_vb_reach(&C, &s, &in, -1, 8, 5, &bb) == 0 && bb == 1, "a stealth target's back: in reach by its back");

    somvr_vb_cue_cfg cc;
    somvr_vb_cue_cfg_default(&cc);
    somvr_vb_cue q;
    somvr_vb_cue_out co;
    somvr_vb_cue_reset(&q);
    somvr_vb_cue_step(&q, &cc, 10.0, 0, &co);
    CHECK(!co.on && !co.tick && co.id == 0 && !co.changed, "nothing in reach: no cue");
    somvr_vb_cue_step(&q, &cc, 10.011, 0x111, &co);
    CHECK(co.on && co.tick && co.id == 0x111 && co.changed && co.level < 0.2f, "in reach, the game's yes: on, one tick");
    int ticks = 0;
    for (int k = 1; k <= 20; k++) {
        somvr_vb_cue_step(&q, &cc, 10.011 + 0.011 * k, 0x111, &co);
        ticks += co.tick;
    }
    CHECK(co.on && ticks == 0 && co.level == 1.0f, "held: no more ticks, the glow full (%d %.2f)", ticks, co.level);
    somvr_vb_cue_step(&q, &cc, 10.3, 0, &co);
    CHECK(co.on && !co.changed, "one frame without the yes: kept (the grace)");
    somvr_vb_cue_step(&q, &cc, 10.4, 0, &co);
    CHECK(!co.on && co.changed && co.id == 0x111 && co.level < 1.0f && co.level > 0, "gone: off, fading on its orc");
    somvr_vb_cue_step(&q, &cc, 10.6, 0x111, &co);
    CHECK(co.on && !co.tick, "back within the re-arm (0.5 s): on, no second tick");
    somvr_vb_cue_step(&q, &cc, 10.7, 0, &co);
    somvr_vb_cue_step(&q, &cc, 10.9, 0, &co);
    somvr_vb_cue_step(&q, &cc, 11.0, 0, &co);
    CHECK(!co.on && co.id == 0 && co.level == 0, "off and faded: no orc");
    somvr_vb_cue_step(&q, &cc, 11.6, 0x111, &co);
    CHECK(co.on && co.tick, "back after the re-arm: a tick again");
    somvr_vb_cue_step(&q, &cc, 11.611, 0x222, &co);
    CHECK(co.on && co.tick && co.id == 0x222 && co.changed && co.level < 0.2f, "another orc: its own tick, its glow anew");
    CHECK(q.ticks == 3 && q.ons == 4, "ticks %llu ons %llu", (unsigned long long)q.ticks, (unsigned long long)q.ons);
}

int main(void)
{
    somvr_vb_cfg_default(&C);
    test_grab_hold_release();
    test_wraith_open();
    test_fist_miss_far();
    test_cone();
    test_ask();
    test_kinds();
    test_back();
    test_cue();
    printf("test_verbs: %d checks, %d failures\n", g_n, g_fail);
    return g_fail != 0;
}
