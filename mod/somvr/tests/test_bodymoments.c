/* test_bodymoments.c - unit tests of somvr_bodymoments.c (SOMVR_BODYMOMENTS=1, first person through the body moments):
 * the glob, the name table (the default against the names P7, P8 and P9 logged: deaths, recoils, counters), the
 * policy (defaults, parse, priorities), the conditions (death by name or dead flag, knockdown, hold/window names, a sync
 * that stays live, our climb exempt), the step (a hold in the first frame, chained syncs, the release by an ease or a
 * blink with the cut on a black frame, a hold again mid-blink, the knockdown dim and the death grey-out, the window's
 * debounce and minimum, pause, gaps), the base (frozen exactly, the ease velocity-continuous without overshoot, the
 * cut, the yaw kept the player's in the three modes, NaN) and a 60 s sequence at 90 Hz in which the view never moves
 * with the body outside the cuts and the eases. */
#include "../somvr_bodymoments.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(c)) {                                                                                                    \
            fails++;                                                                                                   \
            printf("FAIL line %d: ", __LINE__);                                                                        \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))
#define CB(c) SOMVR_BMC_BIT(c)
#define FB(c) SOMVR_FB_BIT(SOMVR_FB_##c)
#define DT (1.0 / 90)

static float d3(const float *a, const float *b) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void test_glob(void) {
    CHECK(somvr_bm_glob("PreDeath", "PreDeath") && somvr_bm_glob("predeath", "PREDEATH"), "exact, case");
    CHECK(!somvr_bm_glob("PreDeath", "PreDeath_Kneeling") && somvr_bm_glob("PreDeath*", "PreDeath_Kneeling") &&
              somvr_bm_glob("PreDeath*", "PreDeath"), "prefix");
    CHECK(somvr_bm_glob("*Loop", "PC_DeathLoop") && !somvr_bm_glob("*Loop", "PC_DeathLoop2"), "suffix");
    CHECK(somvr_bm_glob("PC_*Death*", "PC_RecoilAny_PreDeath") && somvr_bm_glob("PC_*Death*", "PC_DeathLoop") &&
              !somvr_bm_glob("PC_*Death*", "AI_DeathLoop"), "middle");
    CHECK(somvr_bm_glob("a*b*c", "axxbyyc") && somvr_bm_glob("a*b*c", "abbbc") && !somvr_bm_glob("a*b*c", "axxbyy") &&
              somvr_bm_glob("*a*a*", "banana") && !somvr_bm_glob("*x*", "banana"), "backtracking");
    CHECK(somvr_bm_glob("PC_?ecoil*", "PC_Recoil_Light") && !somvr_bm_glob("PC_?ecoil*", "PC_ecoil"), "?");
    CHECK(somvr_bm_glob("*", "") && somvr_bm_glob("*", "x") && somvr_bm_glob("", "") && !somvr_bm_glob("", "x") &&
              !somvr_bm_glob("x", ""), "empty");
}

static void test_names(void) {
    somvr_bm_names t;
    CHECK(somvr_bm_names_parse(NULL, &t) == 0 && t.n == 10, "default: %d", t.n);
    uint32_t A = SOMVR_BMS_ATK, S = SOMVR_BMS_STATE, T = SOMVR_BMS_TAG, Y = SOMVR_BMS_SYNC;
    /* P8's death */
    CHECK(somvr_bm_names_class(&t, A, "PC_RecoilAny_PreDeath") == SOMVR_BMN_DEATH &&
              somvr_bm_names_class(&t, A, "PC_PreDeathLoop") == SOMVR_BMN_DEATH &&
              somvr_bm_names_class(&t, A, "PC_DeathLoop") == SOMVR_BMN_DEATH, "P8's death records");
    /* P7's recoils, P9's counters (they knock an orc down: not Talion's knockdown), P7/P8's attacks */
    static const char *const calm[] = { "PC_Recoil_Light_R2L_F", "PC_Recoil_Light_R2L_B", "PC_Recoil_Medium_D2U_B",
                                        "PC_Recoil_Light_ST_F", "PC_Recoil_HitByRanged", "PC_Bip_Sync_Cntr_Knockdown_B3",
                                        "PC_Bip_Sync_Cntr_Knockback_B", "PC_Bip_Sync_Cntr_Knockback_B2",
                                        "PC_Bip_Sync_Cntr_Stun_F2", "PC_CounterAttempt_Fail", "PC_Deflected_LL",
                                        "PC_HC_High_L1_D0_F_LR_RR_UD_2", "PC_Bip_SneakInstaKill_Loud_B",
                                        "PC_Bip_StealthTorment_B", "PC_StealthBrand_B", "PC_Stealth", "" };
    int any = 0;
    for (size_t i = 0; i < sizeof calm / sizeof calm[0]; i++) any |= (int)somvr_bm_names_class(&t, A, calm[i]);
    CHECK(any == 0, "a calm attack name matched (%#x)", (unsigned)any);
    /* P7/P8's calm tags */
    static const char *const tags[] = { "Gnd", "Idl", "Allow_Showdown", "InCombat", "CombatCam", "Tight", "CloseToWalls",
                                        "Stl", "Teleport", "NamedTarget", "FinalBlow", "OneOnOne" };
    any = 0;
    for (size_t i = 0; i < sizeof tags / sizeof tags[0]; i++) any |= (int)somvr_bm_names_class(&t, T, tags[i]);
    CHECK(any == 0, "a calm tag matched (%#x)", (unsigned)any);
    /* knockdowns as they may be named (RT) */
    CHECK(somvr_bm_names_class(&t, A, "PC_Recoil_Heavy_Knockdown_B") == SOMVR_BMN_KNOCKDOWN &&
              somvr_bm_names_class(&t, A, "PC_Recoil_KnockDown_F") == SOMVR_BMN_KNOCKDOWN &&
              somvr_bm_names_class(&t, A, "PC_Recoil_KnockedDown_Loop") == SOMVR_BMN_KNOCKDOWN &&
              somvr_bm_names_class(&t, A, "PC_Recoil_GetUp_Back") == SOMVR_BMN_KNOCKDOWN &&
              somvr_bm_names_class(&t, A, "PC_GetUp_Front") == SOMVR_BMN_KNOCKDOWN, "knockdown recoils");
    CHECK(somvr_bm_names_class(&t, S, "Knockdown_Long") == SOMVR_BMN_KNOCKDOWN &&
              somvr_bm_names_class(&t, S, "State_KnockedDown") == SOMVR_BMN_KNOCKDOWN &&
              somvr_bm_names_class(&t, T, "Knockdown") == SOMVR_BMN_KNOCKDOWN &&
              somvr_bm_names_class(&t, S, "Stunned") == 0, "knockdown state and tag");
    /* the sources keep apart: a state pattern never classes an attack, the attack patterns never a tag */
    CHECK(somvr_bm_names_class(&t, A, "Knockdown_Long") == 0 && somvr_bm_names_class(&t, T, "PC_DeathLoop") == 0 &&
              somvr_bm_names_class(&t, Y, "PC_DeathLoop") == 0, "sources");
    CHECK(somvr_bm_names_class(&t, A, NULL) == 0 && somvr_bm_names_class(&t, A, "") == 0, "no name");
    /* our own lists */
    CHECK(somvr_bm_names_parse("hold=sync:PC_Bip_Drain*,first=sync:PC_Bip_Sync_Cntr*,window=Mount*", &t) == 0 && t.n == 3,
          "custom: %d", t.n);
    CHECK(somvr_bm_names_class(&t, Y, "PC_Bip_Drain_F") == SOMVR_BMN_HOLD &&
              somvr_bm_names_class(&t, Y, "PC_Bip_Sync_Cntr_F") == SOMVR_BMN_FIRST &&
              somvr_bm_names_class(&t, A, "PC_Bip_Sync_Cntr_F") == 0 &&
              somvr_bm_names_class(&t, T, "Mount") == SOMVR_BMN_WINDOW && somvr_bm_names_class(&t, A, "MountBeast") ==
              SOMVR_BMN_WINDOW, "custom classes and the any source");
    CHECK(somvr_bm_names_parse("+death=tag:Dead*", &t) == 0 && t.n == 11 &&
              somvr_bm_names_class(&t, T, "Dead_Loop") == SOMVR_BMN_DEATH &&
              somvr_bm_names_class(&t, A, "PC_DeathLoop") == SOMVR_BMN_DEATH, "+ adds to the defaults");
    CHECK(somvr_bm_names_parse("none", &t) == 0 && t.n == 0, "none");
    CHECK(somvr_bm_names_parse("default", &t) == 0 && t.n == 10, "default by name");
    CHECK(somvr_bm_names_parse("bogus=atk:X,death=nowhere:Y,death=atk:,death,hold=atk:OK", &t) == -1 && t.n == 1 &&
              t.cls[0] == SOMVR_BMN_HOLD && t.src[0] == SOMVR_BMS_ATK, "bad items: %d", t.n);
    char lng[200];
    memset(lng, 'a', sizeof lng - 1), lng[sizeof lng - 1] = 0;
    memcpy(lng, "hold=", 5);
    CHECK(somvr_bm_names_parse(lng, &t) == -1 && t.n == 0, "too long");
    char many[2048] = "";
    for (int i = 0; i < 30; i++) strcat(many, "hold=atk:X,");
    CHECK(somvr_bm_names_parse(many, &t) == -1 && t.n == SOMVR_BM_MAXNAMES, "too many: %d", t.n);
    /* two classes for one name */
    CHECK(somvr_bm_names_parse("hold=atk:PC_X*,death=any:*X_D*", &t) == 0 &&
              somvr_bm_names_class(&t, A, "PC_X_Dying") == (SOMVR_BMN_HOLD | SOMVR_BMN_DEATH), "two classes");
}

static void test_policy(void) {
    somvr_bm_policy p;
    somvr_bm_policy_default(&p);
    static const int want[SOMVR_BMC_N] = { SOMVR_BMA_WINDOW, SOMVR_BMA_FIRST, SOMVR_BMA_WINDOW, SOMVR_BMA_HOLD,
                                           SOMVR_BMA_HOLD,   SOMVR_BMA_WINDOW, SOMVR_BMA_WINDOW, SOMVR_BMA_FIRST,
                                           SOMVR_BMA_GREY,   SOMVR_BMA_WINDOW, SOMVR_BMA_WINDOW, SOMVR_BMA_FIRST,
                                           SOMVR_BMA_WINDOW, SOMVR_BMA_DIM,    SOMVR_BMA_HOLD,   SOMVR_BMA_WINDOW };
    int ok = 1;
    for (int k = 0; k < SOMVR_BMC_N; k++) ok &= p.act[k] == want[k];
    CHECK(ok, "the default policy");
    CHECK(!strcmp(somvr_bm_cond_names[SOMVR_FB_TAG], "tag") && !strcmp(somvr_bm_cond_names[SOMVR_BMC_KNOCKDOWN], "knockdown") &&
              !strcmp(somvr_bm_cond_names[SOMVR_BMC_WINDOW], "window") && !strcmp(somvr_bm_act_names[SOMVR_BMA_GREY], "grey"),
          "names");
    CHECK(somvr_bm_policy_parse("mount=first, scripted=window,knockdown=hold", &p) == 0 &&
              p.act[SOMVR_FB_MOUNT] == SOMVR_BMA_FIRST && p.act[SOMVR_FB_SCRIPTED] == SOMVR_BMA_WINDOW &&
              p.act[SOMVR_BMC_KNOCKDOWN] == SOMVR_BMA_HOLD && p.act[SOMVR_FB_SYNC] == SOMVR_BMA_HOLD, "parse");
    CHECK(somvr_bm_policy_parse("mount=fly,nothing=first,=hold,sync,death=dim", &p) == -1 &&
              p.act[SOMVR_FB_MOUNT] == SOMVR_BMA_WINDOW && p.act[SOMVR_FB_DEATH] == SOMVR_BMA_DIM, "bad items kept out");
    CHECK(somvr_bm_policy_parse("default", &p) == 0 && p.act[SOMVR_FB_DEATH] == SOMVR_BMA_GREY, "default");
    somvr_bm_policy_default(&p);
    int hc, w, wc;
    CHECK(somvr_bm_actions(&p, 0, &hc, &w, &wc) == SOMVR_BMA_FIRST && hc == -1 && !w && wc == -1, "nothing");
    CHECK(somvr_bm_actions(&p, FB(SYNC), &hc, &w, &wc) == SOMVR_BMA_HOLD && hc == SOMVR_FB_SYNC && !w, "sync");
    CHECK(somvr_bm_actions(&p, FB(SYNC) | CB(SOMVR_BMC_KNOCKDOWN), &hc, &w, &wc) == SOMVR_BMA_DIM &&
              hc == SOMVR_BMC_KNOCKDOWN, "knockdown over sync");
    CHECK(somvr_bm_actions(&p, FB(DEATH) | CB(SOMVR_BMC_KNOCKDOWN) | FB(STRUGGLE), &hc, &w, &wc) == SOMVR_BMA_GREY &&
              hc == SOMVR_FB_DEATH, "death over all");
    CHECK(somvr_bm_actions(&p, FB(SYNC) | FB(CINE) | FB(PRES), &hc, &w, &wc) == SOMVR_BMA_HOLD && w && wc == SOMVR_FB_CINE,
          "the window beside a hold");
    CHECK(somvr_bm_actions(&p, FB(TAG) | FB(CLIMB) | FB(SCRIPTED), &hc, &w, &wc) == SOMVR_BMA_FIRST && !w,
          "the kill cam, the traversal and a scripted camera stay first person");
    CHECK(somvr_bm_actions(&p, FB(FORCED), NULL, &w, NULL) == SOMVR_BMA_FIRST && w, "forced = the window");
}

static void test_conds(void) {
    somvr_bm_in in;
    memset(&in, 0, sizeof in);
    CHECK(somvr_bm_conds(&in) == 0, "nothing");
    in.fb = FB(CINE) | FB(SYNC) | FB(FORCED);
    CHECK(somvr_bm_conds(&in) == in.fb, "the fallback's pass");
    in.fb = 0xffffffffu;
    CHECK(somvr_bm_conds(&in) == ((1u << SOMVR_FB_NCOND) - 1u), "only the fallback's 13 from it");
    in.fb = 0, in.names = SOMVR_BMN_DEATH;
    CHECK(somvr_bm_conds(&in) == FB(DEATH), "death by name");
    in.names = 0, in.dead = 1;
    CHECK(somvr_bm_conds(&in) == FB(DEATH), "death by the dead flag");
    in.dead = 0, in.names = SOMVR_BMN_KNOCKDOWN | SOMVR_BMN_HOLD | SOMVR_BMN_WINDOW;
    CHECK(somvr_bm_conds(&in) == (CB(SOMVR_BMC_KNOCKDOWN) | CB(SOMVR_BMC_HOLD) | CB(SOMVR_BMC_WINDOW)), "name classes");
    in.fb = FB(SYNC) | FB(STRUGGLE), in.names = SOMVR_BMN_FIRST;
    CHECK(somvr_bm_conds(&in) == FB(STRUGGLE), "a sync record of the first class stays live");
    in.fb = FB(CLIMB) | FB(ATTACH) | FB(MOUNT), in.names = 0, in.our_climb = 1;
    CHECK(somvr_bm_conds(&in) == FB(MOUNT), "our climb: climb and attach exempt");
    in.our_climb = 0;
    CHECK(somvr_bm_conds(&in) == (FB(CLIMB) | FB(ATTACH) | FB(MOUNT)), "without our climb");
    in.fb = FB(SYNC) | FB(ATTACH) | FB(MOUNT) | FB(PRES), in.our_grab = 1; /* SOMVR_VERBS' grab: a hold, not the window */
    CHECK(somvr_bm_conds(&in) == (FB(SYNC) | CB(SOMVR_BMC_HOLD)), "our grab: mount, attach, pres exempt, a hold");
    in.fb = FB(CINE) | FB(MOUNT);
    CHECK(somvr_bm_conds(&in) == (FB(CINE) | CB(SOMVR_BMC_HOLD)), "our grab: a cinematic stays the window's");
    in.fb = 0;
    CHECK(somvr_bm_conds(&in) == CB(SOMVR_BMC_HOLD), "our grab alone: a hold");
    in.our_grab = 0;
}

/* a test rig: the step and the base at 90 Hz with a live base that the test moves */
typedef struct {
    somvr_bm s;
    somvr_bm_cfg c;
    somvr_bm_out o;
    double t;
    float live[3], pos[3], yaw;
    float live_yaw;
    int32_t snaps;
    int cut, yaw_mode, stereo;
} rig_t;
static void rig_init(rig_t *r) {
    memset(r, 0, sizeof *r);
    somvr_bm_init(&r->s);
    somvr_bm_cfg_default(&r->c);
    r->t = 100;
    r->live[0] = 100, r->live[1] = 180, r->live[2] = -50;
    r->stereo = 1;
}
/* one frame: act/win from a hold action and a window wish */
static void rig_frame(rig_t *r, int act, int win, int paused) {
    r->t += DT;
    somvr_bm_step(&r->s, &r->c, act, act == SOMVR_BMA_FIRST ? -1 : SOMVR_FB_SYNC, win, win ? SOMVR_FB_CINE : -1, paused,
                  r->t, &r->o);
    if (r->stereo && !r->o.window)
        somvr_bm_base(&r->s, &r->c, r->live, r->live_yaw, r->snaps, 0.5235988f, r->yaw_mode, r->t, r->pos, &r->yaw,
                      &r->cut);
}

static void test_hold_ease(void) {
    rig_t r;
    rig_init(&r);
    for (int i = 0; i < 10; i++) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(!r.o.frozen && r.o.fade == 0 && d3(r.pos, r.live) < 1e-4f && !r.cut, "calm: the live base");
    /* walking at 3 m/s, then a sync: the view stops in the very frame */
    for (int i = 0; i < 9; i++) r.live[0] += 300 * DT, rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    float before[3];
    memcpy(before, r.pos, sizeof before);
    r.live[0] += 300 * DT;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(r.o.frozen && r.o.act == SOMVR_BMA_HOLD && r.o.act_changed && d3(r.pos, before) < 1e-4f && r.o.fade == 0 &&
              !r.o.body_anim, "the first sync frame holds the last view (moved %g)", d3(r.pos, before));
    /* the body lunges 15 cm and turns: the view stays */
    int still = 1;
    for (int i = 0; i < 40; i++) {
        r.live[2] += 15.0f / 40;
        rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
        still &= d3(r.pos, before) < 1e-4f && r.o.frozen;
    }
    CHECK(still, "held while the sync runs");
    /* a 0.1 s gap between two syncs: still one hold */
    for (int i = 0; i < 9; i++) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0), still &= r.o.frozen;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(still && r.s.holds == 1, "chained syncs: one hold (%llu)", (unsigned long long)r.s.holds);
    /* the end: 0.25 s later the release, an ease (the live base 15.3 cm away) */
    int n = 0, rel = 0;
    float F[3];
    memcpy(F, r.pos, sizeof F);
    while (r.o.frozen && n < 100) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0), n++, rel |= r.o.release;
    CHECK(n >= 22 && n <= 24 && rel == 1 && r.s.eases == 1 && r.s.blinks == 0, "released after %d frames by %d", n, rel);
    /* the ease: from the held point, velocity continuous (0), monotonic, no overshoot, settled within 0.5 s */
    float prev = d3(F, r.live), prev_pos[3], vmax = 0, step0 = 0;
    memcpy(prev_pos, r.pos, sizeof prev_pos);
    CHECK(d3(r.pos, F) < 0.3f, "the ease's first frame %g cm from the held point", d3(r.pos, F));
    int mono = 1, k;
    for (k = 0; k < 60 && r.s.ease; k++) {
        rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
        float d = d3(r.pos, r.live), v = d3(r.pos, prev_pos) / (float)DT;
        if (k == 0) step0 = d3(r.pos, prev_pos);
        mono &= d <= prev + 1e-4f && r.pos[2] <= r.live[2] + 1e-3f;
        vmax = fmaxf(vmax, v);
        prev = d, memcpy(prev_pos, r.pos, sizeof prev_pos);
    }
    CHECK(mono && k < 55 && !r.s.ease && d3(r.pos, r.live) < 1e-4f, "the ease: monotonic %d, settled after %d frames", mono,
          k);
    CHECK(step0 < 1.0f && vmax < 150, "the ease starts slow (%g cm) and peaks at %g cm/s for 15 cm", step0, vmax);
    CHECK(!r.cut && r.s.cuts == 0, "no cut");
}

static void test_first_frame(void) {
    /* a hold already in the very first frame (a restart mid-sync): held at the first base, not at an unset point */
    rig_t r;
    rig_init(&r);
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(r.o.frozen && d3(r.pos, r.live) < 1e-4f && !r.cut && r.s.cuts == 0, "held at the first base (%g %g %g)", r.pos[0],
          r.pos[1], r.pos[2]);
    float F[3];
    memcpy(F, r.pos, sizeof F);
    r.live[0] += 50;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(d3(r.pos, F) < 1e-4f, "and stays there");
    /* the first frames flat (the window), then a hold: held at the first stereo base */
    rig_init(&r);
    r.stereo = 0;
    for (int i = 0; i < 5; i++) rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    r.stereo = 1;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(r.o.frozen && d3(r.pos, r.live) < 1e-4f, "held at the first stereo base");
}

static void test_blink(void) {
    rig_t r;
    rig_init(&r);
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    float F[3];
    memcpy(F, r.pos, sizeof F);
    /* a counter carries Talion 1.2 m */
    for (int i = 0; i < 60; i++) r.live[0] += 120.0f / 60, rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    int rel = 0, n = 0, cut_at = -1, black = 0, last_black = -1;
    float lv[40];
    int frozen[40];
    for (int i = 0; i < 40; i++) {
        rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
        rel |= r.o.release;
        lv[i] = r.o.fade, frozen[i] = d3(r.pos, F) < 1e-4f;
        if (r.cut && cut_at < 0) cut_at = i, n++;
        if (r.o.fade >= 1) black++, last_black = i;
    }
    int start = -1;
    for (int i = 0; i < 40 && start < 0; i++)
        if (lv[i] > 0) start = i;
    CHECK(rel == 2 && r.s.blinks == 1 && r.s.eases == 0 && n == 1, "a blink (release %d, %llu blinks)", rel,
          (unsigned long long)r.s.blinks);
    int rising = 1, held = 1;
    for (int i = start; i >= 0 && i < cut_at; i++) rising &= i == start || lv[i] >= lv[i - 1], held &= frozen[i];
    CHECK(start >= 21 && cut_at > start && cut_at - start <= 6 && lv[cut_at] >= 1 && rising && held,
          "the fade from frame %d, the cut on the first black frame %d (%g), held until then", start, cut_at,
          cut_at >= 0 ? lv[cut_at] : 0);
    CHECK(black >= 3 && last_black >= cut_at + 2 && lv[39] == 0 && r.s.blink == SOMVR_BMB_IDLE && d3(r.pos, r.live) < 1e-4f,
          "black %d frames until %d, then clear on the live base", black, last_black);
    CHECK(NEAR(r.s.last_seat_cm, 120, 0.5), "the release measured %g cm", r.s.last_seat_cm);
    /* held again while fading out: back in from that level, never cut */
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    memcpy(F, r.pos, sizeof F);
    r.live[0] += 100;
    for (int i = 0; i < 30; i++) rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    for (int i = 0; i < 60 && !(r.s.blink == SOMVR_BMB_OUT && r.o.fade > 0); i++) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(r.s.blink == SOMVR_BMB_OUT && r.o.fade > 0 && r.o.fade < 1, "fading out (%d %g)", r.s.blink, r.o.fade);
    float lvl = r.o.fade;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(r.s.blink == SOMVR_BMB_IN && NEAR(r.o.fade, lvl, 0.25) && r.o.frozen && d3(r.pos, F) < 1e-4f && !r.cut,
          "held again: back in from %g (%g), still held", lvl, r.o.fade);
    for (int i = 0; i < 20; i++) rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(r.o.fade == 0 && r.o.frozen && d3(r.pos, F) < 1e-4f && r.s.cuts == 1, "clear and held");
    /* seat_cm 0: every release a blink, even in place */
    rig_init(&r);
    r.c.seat_cm = 0;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    for (int i = 0; i < 40; i++) rig_frame(&r, i < 10 ? SOMVR_BMA_HOLD : SOMVR_BMA_FIRST, 0, 0);
    CHECK(r.s.blinks == 1 && r.s.eases == 0 && NEAR(r.s.last_seat_cm, 0, 1e-4), "seat_cm 0: a blink, even in place");
    /* no fade times: a plain cut */
    rig_init(&r);
    r.c.blink_out_s = r.c.blink_in_s = r.c.blink_black_s = 0;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    r.live[0] += 200;
    int cuts = 0;
    for (int i = 0; i < 40; i++) rig_frame(&r, i < 10 ? SOMVR_BMA_HOLD : SOMVR_BMA_FIRST, 0, 0), cuts += r.cut;
    CHECK(cuts == 1 && d3(r.pos, r.live) < 1e-4f && r.o.fade == 0, "no blink times: one cut");
}

static void test_dim_grey(void) {
    rig_t r;
    rig_init(&r);
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    /* a knockdown: frozen, dimmed to 0.5 within 0.15 s, the arms to the animation */
    float F[3];
    memcpy(F, r.pos, sizeof F);
    int n = 0;
    while (r.o.fade < 0.5f - 1e-4f && n < 60) r.live[1] -= 1, rig_frame(&r, SOMVR_BMA_DIM, 0, 0), n++;
    CHECK(n >= 13 && n <= 15 && r.o.frozen && r.o.body_anim && d3(r.pos, F) < 1e-4f && r.s.dims == 1,
          "the knockdown's dim at 0.5 after %d frames", n);
    CHECK(r.o.red, "the knockdown's dim is red");
    for (int i = 0; i < 90; i++) r.live[1] -= 50.0f / 90, rig_frame(&r, SOMVR_BMA_DIM, 0, 0);
    CHECK(NEAR(r.o.fade, 0.5, 1e-4) && r.o.frozen && d3(r.pos, F) < 1e-4f, "held dim (%g)", r.o.fade);
    /* a hit while down (a sync): still dim */
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(r.o.act == SOMVR_BMA_DIM && NEAR(r.o.fade, 0.5, 1e-4), "a lower action keeps the dim during the debounce");
    /* up: the dim ramps away, the release (the root 60 cm lower: a blink) */
    n = 0;
    while (r.o.act != SOMVR_BMA_FIRST && n < 60) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0), n++;
    CHECK(n >= 21 && n <= 24 && !r.o.body_anim, "up after %d frames", n);
    for (int i = 0; i < 40; i++) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(r.o.fade == 0 && !r.o.frozen && r.s.blinks == 1 && d3(r.pos, r.live) < 1e-4f, "clear, re-seated");
    /* the death: the 1 s grey-out to 0.8 */
    n = 0;
    while (r.o.fade < 0.8f - 1e-4f && n < 200) rig_frame(&r, SOMVR_BMA_GREY, 0, 0), n++;
    CHECK(n >= 88 && n <= 92 && r.o.act == SOMVR_BMA_GREY && r.o.body_anim && r.s.greys == 1, "grey 0.8 after %d frames", n);
    CHECK(!r.o.red, "the death's grey-out is not red");
    /* the dim's colour: red while it decays, black once the blink is darker; SOMVR_BM_RED=0 black */
    rig_init(&r);
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    for (int i = 0; i < 30; i++) rig_frame(&r, SOMVR_BMA_DIM, 0, 0);
    r.live[0] += 100;
    int red_decay = 1, black_blink = 1, seen_blink = 0;
    for (int i = 0; i < 60; i++) {
        rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
        if (r.s.blink_alpha > r.s.level) black_blink &= !r.o.red, seen_blink = 1;
        else if (r.s.level > 0 && r.s.act == SOMVR_BMA_FIRST) red_decay &= r.o.red;
    }
    CHECK(red_decay && black_blink && seen_blink && !r.o.red && r.o.fade == 0, "the dim's colour while it ends (%d %d %d)",
          red_decay, black_blink, seen_blink);
    rig_init(&r);
    r.c.red = 0;
    for (int i = 0; i < 30; i++) rig_frame(&r, SOMVR_BMA_DIM, 0, 0);
    CHECK(!r.o.red && NEAR(r.o.fade, 0.5, 1e-4), "red off: a black dim");
    /* knockdown -> death: dim to grey, not down first */
    rig_init(&r);
    for (int i = 0; i < 30; i++) rig_frame(&r, SOMVR_BMA_DIM, 0, 0);
    float a = r.o.fade;
    rig_frame(&r, SOMVR_BMA_GREY, 0, 0);
    CHECK(r.o.act == SOMVR_BMA_GREY && r.o.fade >= a, "dim -> grey rises (%g -> %g)", a, r.o.fade);
}

static void test_window(void) {
    rig_t r;
    rig_init(&r);
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    /* a 50 ms flicker: nothing */
    for (int i = 0; i < 4; i++) rig_frame(&r, SOMVR_BMA_FIRST, 1, 0);
    for (int i = 0; i < 4; i++) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(!r.o.window && r.s.windows == 0, "a flicker");
    /* a cinematic: the window after 0.1 s */
    int n = 0;
    while (!r.o.window && n < 60) rig_frame(&r, SOMVR_BMA_FIRST, 1, 0), n++;
    CHECK(n == 10 && r.o.win_changed && r.o.win_cond == SOMVR_FB_CINE && r.o.fade == 0, "the window after %d frames", n);
    /* gone after 0.2 s: the window stays at least 1 s */
    for (int i = 0; i < 18; i++) rig_frame(&r, SOMVR_BMA_FIRST, 1, 0);
    n = 0;
    while (r.o.window && n < 200) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0), n++;
    CHECK(n >= 71 && n <= 73, "out after %d frames (the 1 s minimum: 90 after the entry)", n);
    /* the body moved while flat: the first stereo frame is the live base by a cut (the layer fades the world in) */
    CHECK(r.cut && d3(r.pos, r.live) < 1e-4f, "a cut back");
    r.live[0] += 500;
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(!r.cut && d3(r.pos, r.live) < 1e-4f, "then live");
    /* a hold that ends inside the window: no blink, a cut afterwards */
    rig_init(&r);
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    for (int i = 0; i < 20; i++) rig_frame(&r, SOMVR_BMA_HOLD, 1, 0);
    CHECK(r.o.window && r.o.frozen, "the window over a hold");
    r.live[0] += 300;
    int rel = 0;
    for (int i = 0; i < 60; i++) rig_frame(&r, SOMVR_BMA_FIRST, 1, 0), rel |= r.o.release;
    CHECK(rel == 3 && !r.o.frozen && r.s.blinks == 0, "released inside the window by a cut (%d)", rel);
    for (int i = 0; i < 60; i++) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(!r.o.window && d3(r.pos, r.live) < 1e-4f && r.s.cuts >= 1, "live after the window");
    /* a hold still on when the window ends: held again from the live base (re-latched behind the layer's fade) */
    rig_init(&r);
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    for (int i = 0; i < 100; i++) rig_frame(&r, SOMVR_BMA_HOLD, 1, 0);
    r.live[2] += 250;
    for (int i = 0; i < 60; i++) r.stereo = 0, rig_frame(&r, SOMVR_BMA_HOLD, 0, 0); /* flat until it ends */
    r.stereo = 1;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    float F[3];
    memcpy(F, r.pos, sizeof F);
    r.live[2] += 30;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(!r.o.window && r.o.frozen && d3(F, (float[]){ 100, 180, 200 }) < 1e-3f && d3(r.pos, F) < 1e-4f,
          "held from the live base after the window (%g %g %g)", F[0], F[1], F[2]);
}

static void test_pause_gap(void) {
    rig_t r;
    rig_init(&r);
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    /* paused: nothing changes */
    for (int i = 0; i < 30; i++) rig_frame(&r, SOMVR_BMA_HOLD, 1, 1);
    CHECK(!r.o.frozen && !r.o.window && r.s.holds == 0, "paused: no hold, no window");
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(r.o.frozen, "unpaused: the hold");
    for (int i = 0; i < 60; i++) rig_frame(&r, SOMVR_BMA_FIRST, 0, 1);
    CHECK(r.o.frozen, "paused: the hold stays");
    /* a blink in flight finishes while paused */
    r.live[0] += 200;
    for (int i = 0; i < 30; i++) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(r.s.blink != SOMVR_BMB_IDLE || r.s.blinks == 1, "the blink started");
    for (int i = 0; i < 30; i++) rig_frame(&r, SOMVR_BMA_FIRST, 0, 1);
    CHECK(r.s.blink == SOMVR_BMB_IDLE && r.o.fade == 0 && d3(r.pos, r.live) < 1e-4f, "the blink finished while paused");
    /* a gap (no H4 for 2 s): a hold that ended meanwhile goes by a cut */
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    r.t += 2.0;
    r.live[0] += 400;
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(r.o.release == 3 && !r.o.frozen && r.cut && d3(r.pos, r.live) < 1e-4f && r.o.fade == 0, "a gap: a cut (%d)",
          r.o.release);
    /* a gap inside a hold keeps it */
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    float F[3];
    memcpy(F, r.pos, sizeof F);
    r.t += 2.0, r.live[0] += 100;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(r.o.frozen && d3(r.pos, F) < 1e-4f, "a gap inside a hold");
    /* the level jumps across a gap */
    rig_init(&r);
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    r.t += 3;
    rig_frame(&r, SOMVR_BMA_GREY, 0, 0);
    CHECK(NEAR(r.o.fade, 0.8, 1e-4), "the grey level after a gap at once (%g)", r.o.fade);
    /* NaN time and a NaN live base */
    rig_init(&r);
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    somvr_bm_step(&r.s, &r.c, SOMVR_BMA_HOLD, SOMVR_FB_SYNC, 0, -1, 0, NAN, &r.o);
    CHECK(r.o.frozen && isfinite(r.o.fade), "NaN time");
    float bad[3] = { NAN, 0, 0 }, p[3], y;
    int cut;
    somvr_bm_base(&r.s, &r.c, bad, NAN, 0, 0.5f, SOMVR_BMY_PLAYER, r.t + DT, p, &y, &cut);
    CHECK(isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) && isfinite(y), "NaN live: %g %g %g %g", p[0], p[1], p[2], y);
    /* bad action values */
    somvr_bm_step(&r.s, &r.c, 99, 0, 0, -1, 0, r.t + 2 * DT, &r.o);
    CHECK(r.o.act >= SOMVR_BMA_FIRST && r.o.act <= SOMVR_BMA_GREY, "a bad action clamped");
}

static void test_yaw(void) {
    rig_t r;
    rig_init(&r);
    const float snap = 0.5235988f;
    r.live_yaw = 1.0f;
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(NEAR(r.yaw, 1.0, 1e-5), "player: starts at the rig's yaw (%g)", r.yaw);
    /* the game camera turns 40 deg (a combat camera): the view does not */
    for (int i = 0; i < 40; i++) r.live_yaw += 0.01745f, rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(NEAR(r.yaw, 1.0, 1e-4), "player: the camera's turn absorbed (%g)", r.yaw);
    /* the player's snap: +30 deg, exactly */
    r.snaps++, r.live_yaw += snap;
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(NEAR(r.yaw, 1.0 + snap, 1e-4), "player: the snap passes (%g)", r.yaw);
    /* across +-pi */
    r.live_yaw = 3.1f;
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    r.live_yaw = -3.1f;
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(NEAR(r.yaw, 1.0 + snap, 1e-4), "player: a wrap is no turn (%g)", r.yaw);
    somvr_bm_recenter(&r.s);
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(NEAR(r.yaw, -3.1, 1e-4), "player: a recentre faces the rig's yaw again (%g)", r.yaw);
    r.live_yaw = -3.0f;
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(NEAR(r.yaw, -3.1, 1e-4), "player: absorbing again after the recentre (%g)", r.yaw);
    /* moments: the camera passes outside, is absorbed during a hold, the offset kept after */
    rig_init(&r);
    r.yaw_mode = SOMVR_BMY_MOMENTS;
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    r.live_yaw = 0.3f;
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(NEAR(r.yaw, 0.3, 1e-5), "moments: outside, the camera turns the view");
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    for (int i = 0; i < 20; i++) r.live_yaw += 0.05f, rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(NEAR(r.yaw, 0.3, 1e-4), "moments: the sync camera's 57 deg absorbed (%g)", r.yaw);
    for (int i = 0; i < 60; i++) rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(NEAR(r.yaw, 0.3, 1e-4), "moments: no jump at the release (%g)", r.yaw);
    r.live_yaw += 0.2f;
    rig_frame(&r, SOMVR_BMA_FIRST, 0, 0);
    CHECK(NEAR(r.yaw, 0.5, 1e-4), "moments: the camera again afterwards, the offset kept (%g)", r.yaw);
    /* camera: untouched */
    rig_init(&r);
    r.yaw_mode = SOMVR_BMY_CAMERA;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    r.live_yaw = 2.0f;
    rig_frame(&r, SOMVR_BMA_HOLD, 0, 0);
    CHECK(r.yaw == 2.0f && !strcmp(somvr_bm_yaw_names[SOMVR_BMY_CAMERA], "camera"), "camera: the rig's yaw");
}

/* 60 s at 90 Hz: walking, a counter (1.2 m), a struggle in place, a knockdown sliding 1.5 m, a kill with its slow-mo
 * camera (first person: the yaw kept), the death, the Nemesis screen in the window, the respawn far away. The view
 * never moves with the body in a hold, follows it 1:1 otherwise, and jumps only at cuts (always black or after the
 * window). */
static void test_sequence(void) {
    rig_t r;
    rig_init(&r);
    somvr_bm_policy p;
    somvr_bm_policy_default(&p);
    struct { double t0, t1; uint32_t conds; float vx, vz; } ev[] = {
        { 0, 5, 0, 300, 0 },                                     /* walking */
        { 5, 5.8, FB(SYNC), 150, 0 },                            /* a counter carries him 1.2 m */
        { 5.8, 8, 0, 0, 0 },
        { 8, 10, FB(STRUGGLE), 0, 4 },                           /* a struggle, 8 cm */
        { 10, 10.5, 0, 0, 0 },
        { 10.5, 14, FB(TAG), 200, 0 },                           /* fighting on, a kill cam (FinalBlow) */
        { 14, 16, CB(SOMVR_BMC_KNOCKDOWN), -75, 0 },             /* knocked down, sliding 1.5 m */
        { 16, 20, 0, 0, 0 },
        { 20, 24, FB(DEATH), 0, 0 },                             /* the death */
        { 24, 34, FB(DEATH) | FB(PRES), 0, 0 },                  /* the Nemesis screen */
        { 34, 40, 0, 0, 0 },                                     /* the respawn (a teleport at 34) */
        { 40, 60, 0, 250, 0 },
    };
    double t0 = r.t;
    int dragged = 0, held_frames = 0, cut_unhidden = 0, cuts = 0, live_off = 0, win_frames = 0, frames = 0;
    float prev_pos[3], prev_live[3];
    memcpy(prev_pos, r.pos, sizeof prev_pos);
    int have_prev = 0, teleported = 0, yaw_moved = 0, was_flat = 0;
    float yaw0 = 0;
    while (r.t - t0 < 60) {
        double tt = r.t - t0;
        uint32_t conds = 0;
        float vx = 0, vz = 0;
        for (size_t i = 0; i < sizeof ev / sizeof ev[0]; i++)
            if (tt >= ev[i].t0 && tt < ev[i].t1) conds = ev[i].conds, vx = ev[i].vx, vz = ev[i].vz;
        if (tt >= 34 && !teleported) teleported = 1, r.live[0] += 5000, r.live[2] -= 3000;
        if (conds & FB(TAG)) r.live_yaw += 0.02f; /* the kill camera swings the game camera */
        memcpy(prev_live, r.live, sizeof prev_live);
        r.live[0] += vx * (float)DT, r.live[2] += vz * (float)DT;
        int hc, w, wc;
        int act = somvr_bm_actions(&p, conds, &hc, &w, &wc);
        r.t += DT;
        somvr_bm_step(&r.s, &r.c, act, hc, w, wc, 0, r.t, &r.o);
        frames++;
        if (r.o.window) {
            win_frames++, was_flat = 1;
            continue;
        }
        float yaw;
        somvr_bm_base(&r.s, &r.c, r.live, r.live_yaw, 0, 0.5236f, SOMVR_BMY_PLAYER, r.t, r.pos, &yaw, &r.cut);
        if (!have_prev) yaw0 = yaw;
        yaw_moved |= !NEAR(yaw, yaw0, 1e-4);
        if (have_prev) {
            float moved = d3(r.pos, prev_pos), body = d3(r.live, prev_live);
            if (r.cut) {
                cuts++;
                cut_unhidden += !(r.o.fade >= 1) && !was_flat; /* after the window: behind the layer's fade */
            } else if (r.o.frozen) {
                held_frames++;
                dragged += moved > 1e-4f;
            } else if (!r.s.ease) {
                live_off += d3(r.pos, r.live) > 1e-3f;
                dragged += moved > body + 1e-3f;
            } else {
                dragged += moved > 3.0f; /* an ease frame: at most 3 cm (270 cm/s) */
            }
        }
        memcpy(prev_pos, r.pos, sizeof prev_pos), have_prev = 1, was_flat = 0;
    }
    CHECK(frames >= 5395 && frames <= 5402, "frames %d", frames);
    CHECK(dragged == 0, "the view moved with the body in %d frames", dragged);
    CHECK(held_frames > 700 && live_off == 0, "held %d frames, off the live base in %d free frames", held_frames, live_off);
    CHECK(r.s.holds == 4 && r.s.blinks == 2 && r.s.eases == 1 && r.s.windows == 1 && r.s.dims == 1 && r.s.greys == 1,
          "holds %llu blinks %llu eases %llu windows %llu dims %llu greys %llu", (unsigned long long)r.s.holds,
          (unsigned long long)r.s.blinks, (unsigned long long)r.s.eases, (unsigned long long)r.s.windows,
          (unsigned long long)r.s.dims, (unsigned long long)r.s.greys);
    CHECK(cuts == 3 && cut_unhidden == 0, "%d cuts, %d of them not black or after the window", cuts, cut_unhidden);
    CHECK(win_frames > 800 && !yaw_moved, "the window %d frames; the kill camera never turned the view", win_frames);
    CHECK(d3(r.pos, r.live) < 1e-3f && r.o.fade == 0 && !r.o.frozen, "the end: live, clear");
    printf("sequence (60 s at 90 Hz): walking, a 1.2 m counter (blink), an 8 cm struggle (ease), a kill cam, a 1.5 m "
           "knockdown (dim, blink), death (grey), the Nemesis window, the respawn (cut): %d frames held, %d cuts, %d "
           "flat\n", held_frames, cuts, win_frames);
}

/* build 20: the grab's hold lets go once the player's own locomotion owns Talion (the user's build 19b session: the
 * interrogation's end left our_grab set and the view on the held point while the stick walked Talion away) */
static void test_grab_free(void) {
    somvr_bm_free f;
    somvr_bm_free_init(&f);
    const double dt = 1.0 / 90;
    double t = 0;
    int suppressed = 0, start_frees = 0;
    /* a grab: the player's locomotion owns him 0.46 s (the grab's attempt), then the game's grab and interrogation 15 s */
    for (int i = 0; i < 90 * 16; i++, t += dt)
        start_frees += somvr_bm_free_step(&f, 1, t < 0.46, t, 1.0f);
    CHECK(start_frees == 0 && !f.latched, "the grab's own start (0.46 s ours) and the game's hold: never freed (%d)",
          start_frees);
    /* the game's kill is over, our_grab stuck: the stick walks him */
    double t_free = -1;
    for (int i = 0; i < 90 * 30; i++, t += dt) {
        int s = somvr_bm_free_step(&f, 1, 1, t, 1.0f);
        suppressed += s;
        if (s && t_free < 0) t_free = t;
    }
    CHECK(t_free > 0 && fabs(t_free - (16.0 + 1.0)) < 2 * dt && suppressed > 90 * 28, "freed %.3f s after the game let "
          "go (want 1.0), suppressed %d frames", t_free - 16.0, suppressed);
    /* the conditions with it: no hold left */
    somvr_bm_in in;
    memset(&in, 0, sizeof in);
    in.our_grab = !somvr_bm_free_step(&f, 1, 0, t, 1.0f); /* latched: even a game tick does not bring it back */
    CHECK(!(somvr_bm_conds(&in) & SOMVR_BMC_BIT(SOMVR_BMC_HOLD)), "latched: our_grab no hold");
    /* the grab ends: re-armed; a new grab under the game's hold holds */
    CHECK(somvr_bm_free_step(&f, 0, 1, t, 1.0f) == 0 && !f.latched && f.frees == 1, "re-armed after the grab");
    in.our_grab = !somvr_bm_free_step(&f, 1, 0, t + dt, 1.0f);
    CHECK((somvr_bm_conds(&in) & SOMVR_BMC_BIT(SOMVR_BMC_HOLD)) != 0, "the next grab holds again");
    /* 0 = never */
    somvr_bm_free g;
    somvr_bm_free_init(&g);
    int any = 0;
    for (int i = 0; i < 900; i++)
        any += somvr_bm_free_step(&g, 1, 1, i * dt, 0.0f);
    CHECK(any == 0, "SOMVR_BM_GRAB_FREE_S=0: never");
    printf("grab free: the grab's start and the game's 15 s hold kept, freed %.2f s after the player's locomotion took "
           "Talion back\n", t_free - 16.0);
}

int main(void) {
    test_glob();
    test_names();
    test_policy();
    test_conds();
    test_hold_ease();
    test_first_frame();
    test_blink();
    test_dim_grey();
    test_window();
    test_pause_gap();
    test_yaw();
    test_sequence();
    test_grab_free();
    printf("test_bodymoments: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
