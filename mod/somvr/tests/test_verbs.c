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

int main(void)
{
    somvr_vb_cfg_default(&C);
    test_grab_hold_release();
    test_wraith_open();
    test_fist_miss_far();
    test_cone();
    test_ask();
    test_kinds();
    printf("test_verbs: %d checks, %d failures\n", g_n, g_fail);
    return g_fail != 0;
}
