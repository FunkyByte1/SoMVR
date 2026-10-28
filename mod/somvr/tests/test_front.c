/* test_front.c - front-first enemies (docs/re-xr-pipeline.md 42): somvr_front.c - the veto after the game's score (in
 * view, the game's own rejections, far orcs, no id), the warning at a pass's end (the best-scored vetoed orc, only when
 * nobody in view was accepted, max_warn), wait -> ready -> expiry -> quiet -> a new warning, pass boundaries (another
 * slot, a later tick), the records (full, forgotten), the clock's wrap, a fight at the AI's tick rate (no orc behind
 * the player is granted before its warning ran 1 s), the damage scale, and the AI's camera copy against the game's
 * own camera-term formula. Build 19: the crowd's pace (one wind-up at Talion at a time, impacts gap_s apart, the
 * warnings held while a turn holds, one grant a pass) and a crowd of four at the AI's tick rate. */
#include "../somvr_front.h"

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

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-3)
#define SLOT_A ((uintptr_t)0x1000)
#define SLOT_B ((uintptr_t)0x2000)
#define ORC1 0x111ull
#define ORC2 0x222ull
#define ORC3 0x333ull

/* one candidate: returns the verdict, *out the score after the judge */
static int judge(somvr_front *f, const somvr_front_cfg *c, uintptr_t slot, uint32_t t, uint64_t id, float off,
                 float score, float *out, uint64_t *started) {
    float s = score;
    int v = somvr_front_judge(f, c, slot, t, id, off, 300.f, 0, &s, started);
    if (out) *out = s;
    return v;
}

static void test_basic(void) {
    somvr_front_cfg c;
    somvr_front_cfg_default(&c);
    CHECK(c.view_deg == 110.f && c.warn_s == 1.f && c.ready_s == 2.f && c.quiet_s == 2.f && c.far_cm == 0.f &&
              c.veto_score == -50.f && c.max_warn == 1,
          "defaults");
    somvr_front f;
    somvr_front_init(&f);
    float s;
    CHECK(judge(&f, &c, SLOT_A, 100, ORC1, 20.f, 1.5f, &s, 0) == SOMVR_FR_IN_VIEW && s == 1.5f, "in view: unchanged");
    CHECK(judge(&f, &c, SLOT_A, 100, ORC1, 55.f, 1.5f, &s, 0) == SOMVR_FR_IN_VIEW, "the cone's edge (55 = 110 / 2)");
    CHECK(judge(&f, &c, SLOT_A, 100, ORC2, NAN, 1.5f, &s, 0) == SOMVR_FR_IN_VIEW, "unknown direction: in view");
    CHECK(judge(&f, &c, SLOT_A, 100, ORC3, 170.f, -4.f, &s, 0) == SOMVR_FR_GAME && s == -4.f,
          "the game's rejection (too far: -4) stays");
    CHECK(judge(&f, &c, SLOT_A, 100, ORC3, 170.f, NAN, &s, 0) == SOMVR_FR_GAME, "a NaN score: the game's");
    CHECK(!somvr_front_find(&f, ORC1) && !somvr_front_find(&f, ORC3), "no records for in-view or rejected orcs");
    CHECK(judge(&f, &c, SLOT_A, 100, ORC3, 56.f, 2.f, &s, 0) == SOMVR_FR_VETO && s == -50.f, "out of view: vetoed");
    CHECK(somvr_front_find(&f, ORC3) && somvr_front_find(&f, ORC3)->want, "the vetoed orc wants a warning");
    CHECK(judge(&f, &c, SLOT_A, 100, 0, 170.f, 2.f, &s, 0) == SOMVR_FR_FULL && s == 2.f, "no id: unchanged");
    CHECK(f.passes == 1 && f.judged == 7 && f.in_view == 3 && f.game_rejects == 2 && f.vetoes == 1 && f.full == 1,
          "counters (passes %llu judged %llu)", (unsigned long long)f.passes, (unsigned long long)f.judged);
    CHECK(somvr_front_end_pass(&f, &c, 100) == 0, "an orc in view was accepted in the pass: no warning");
    CHECK(somvr_front_warnings(&f, &c, 100) == 0 && !somvr_front_find(&f, ORC3)->want, "none; the wants cleared");
    c.far_cm = 2000.f;
    s = 2.f;
    CHECK(somvr_front_judge(&f, &c, SLOT_A, 200, ORC2, 170.f, 2500.f, 0, &s, 0) == SOMVR_FR_FAR && s == 2.f,
          "far_cm: an orc 25 m behind is not vetoed");
    CHECK(strcmp(somvr_front_verdict_name(SOMVR_FR_WAIT), "wait") == 0 && strcmp(somvr_front_verdict_name(99), "?") == 0,
          "names");
}

static void test_warning(void) {
    somvr_front_cfg c;
    somvr_front_cfg_default(&c);
    somvr_front f;
    somvr_front_init(&f);
    float s;
    uint64_t st = 7;
    /* tick 1000: two orcs behind, nobody in view */
    CHECK(judge(&f, &c, SLOT_A, 1000, ORC1, 150.f, 1.2f, &s, &st) == SOMVR_FR_VETO && st == 0, "orc 1 vetoed");
    CHECK(judge(&f, &c, SLOT_A, 1000, ORC2, 120.f, 1.8f, &s, &st) == SOMVR_FR_VETO, "orc 2 vetoed");
    /* tick 1033: the next pass ends the first: the warning for the best-scored (orc 2) */
    CHECK(judge(&f, &c, SLOT_A, 1033, ORC1, 150.f, 1.2f, &s, &st) == SOMVR_FR_VETO && st == ORC2,
          "the next tick ends the pass: orc 2 (1.8 > 1.2) warned (%#llx)", (unsigned long long)st);
    CHECK(judge(&f, &c, SLOT_A, 1033, ORC2, 120.f, 1.8f, &s, &st) == SOMVR_FR_WAIT && s == -50.f && st == 0,
          "orc 2 waits");
    CHECK(somvr_front_warnings(&f, &c, 1033) == 1 && fabsf(somvr_front_warn_level(&f, &c, ORC2, 1533) - 0.5f) < 1e-3f &&
              somvr_front_warn_level(&f, &c, ORC1, 1533) == -1.f,
          "one running; its level half way at 0.5 s");
    CHECK(somvr_front_end_pass(&f, &c, 1066) == 0, "max_warn 1: orc 1 gets none while orc 2's runs");
    /* 0.999 s after the warning's start: still waiting; 1.0 s: ready */
    CHECK(judge(&f, &c, SLOT_A, 1033 + 999, ORC2, 120.f, 1.8f, &s, 0) == SOMVR_FR_WAIT, "0.999 s: wait");
    CHECK(judge(&f, &c, SLOT_A, 1033 + 1000, ORC2, 120.f, 1.8f, &s, 0) == SOMVR_FR_READY && s == 1.8f,
          "1.0 s: ready, the game's score back");
    CHECK(somvr_front_warn_level(&f, &c, ORC2, 2033) == 1.f, "the level full");
    CHECK(judge(&f, &c, SLOT_A, 1033 + 3000, ORC2, 120.f, 1.8f, &s, 0) == SOMVR_FR_READY, "ready to 3.0 s");
    {
        somvr_front g = f; /* a copy: granted at 2.5 s, the warning is used up */
        somvr_front_granted(&g, ORC2);
        CHECK(g.used == 1 && !somvr_front_find(&g, ORC2)->warning, "granted: the warning used");
        CHECK(judge(&g, &c, SLOT_A, 4000, ORC2, 120.f, 1.8f, &s, 0) == SOMVR_FR_VETO && somvr_front_find(&g, ORC2)->want,
              "its next turn from behind: vetoed, wanting a new warning (no quiet time)");
        somvr_front_granted(&g, ORC3);
        CHECK(g.used == 1, "an unknown orc: nothing");
    }
    /* never granted: the warning runs out, the orc goes quiet for 2 s */
    CHECK(judge(&f, &c, SLOT_A, 1033 + 3001, ORC2, 120.f, 1.8f, &s, 0) == SOMVR_FR_VETO &&
              !somvr_front_find(&f, ORC2)->want && somvr_front_find(&f, ORC2)->quiet,
          "3.001 s: expired, vetoed and quiet");
    CHECK(judge(&f, &c, SLOT_A, 1033 + 3001, ORC1, 150.f, 1.2f, &s, 0) == SOMVR_FR_VETO &&
              somvr_front_find(&f, ORC1)->want,
          "orc 1 still behind, wanting");
    CHECK(f.expired == 1, "counted");
    uint64_t w = somvr_front_end_pass(&f, &c, 4100);
    CHECK(w == ORC1, "orc 1's turn now (%#llx)", (unsigned long long)w);
    CHECK(judge(&f, &c, SLOT_A, 4200, ORC1, 150.f, 1.2f, &s, 0) == SOMVR_FR_WAIT, "orc 1 waits");
    /* orc 1 walks into view: in view wins at once, its warning kept */
    CHECK(judge(&f, &c, SLOT_A, 4300, ORC1, 30.f, 1.2f, &s, 0) == SOMVR_FR_IN_VIEW && s == 1.2f, "in view: allowed");
    CHECK(somvr_front_find(&f, ORC1)->warning, "its warning kept");
    /* orc 2 is quiet until 4034 + 2000: no warning before, one after (orc 1's expired at 4100 + 3000 = 7100) */
    CHECK(somvr_front_end_pass(&f, &c, 4400) == 0, "orc 1 was in view in that pass: no warning");
    CHECK(judge(&f, &c, SLOT_A, 7200, ORC2, 120.f, 1.8f, &s, 0) == SOMVR_FR_VETO && somvr_front_find(&f, ORC2)->want,
          "7.2 s: orc 2 not quiet any more");
    CHECK(somvr_front_end_pass(&f, &c, 7200) == ORC2, "warned again");
}

static void test_passes(void) {
    somvr_front_cfg c;
    somvr_front_cfg_default(&c);
    somvr_front f;
    somvr_front_init(&f);
    float s;
    uint64_t st;
    CHECK(judge(&f, &c, SLOT_A, 500, ORC1, 150.f, 1.f, &s, &st) == SOMVR_FR_VETO, "slot A: orc 1 behind");
    CHECK(judge(&f, &c, SLOT_B, 500, ORC2, 10.f, 1.f, &s, &st) == SOMVR_FR_IN_VIEW && st == ORC1,
          "slot B, the same tick: slot A's pass ended, orc 1 warned (its own pass had nobody in view)");
    CHECK(f.passes == 2, "two passes");
    CHECK(somvr_front_end_pass(&f, &c, 500) == 0 && !f.open, "slot B's pass: orc 2 in view, nothing; closed");
    CHECK(somvr_front_end_pass(&f, &c, 600) == 0, "no open pass: only the expiry runs");
    /* max_warn 2: two at once */
    c.max_warn = 2;
    somvr_front_init(&f);
    judge(&f, &c, SLOT_A, 0, ORC1, 150.f, 1.f, &s, 0);
    judge(&f, &c, SLOT_A, 0, ORC2, 150.f, 2.f, &s, 0);
    CHECK(somvr_front_end_pass(&f, &c, 30) == ORC2, "the best first");
    judge(&f, &c, SLOT_A, 60, ORC1, 150.f, 1.f, &s, 0);
    CHECK(somvr_front_end_pass(&f, &c, 60) == ORC1 && somvr_front_warnings(&f, &c, 60) == 2, "max_warn 2: the second");
}

/* The game sorts a prio candidate (entry+0x23a, its timer run out) first whatever its score: vetoed, it would end every
 * pass before anyone is granted. It is warned at once: an orc in view, max_warn and its quiet time do not hold it. */
static void test_prio(void) {
    somvr_front_cfg c;
    somvr_front_cfg_default(&c);
    somvr_front f;
    somvr_front_init(&f);
    float s;
    uint64_t st;
    CHECK(judge(&f, &c, SLOT_A, 0, ORC1, 20.f, 2.f, &s, 0) == SOMVR_FR_IN_VIEW, "orc 1 in front");
    CHECK(judge(&f, &c, SLOT_A, 0, ORC3, 150.f, 1.f, &s, 0) == SOMVR_FR_VETO, "orc 3 behind (not prio)");
    s = 1.f;
    CHECK(somvr_front_judge(&f, &c, SLOT_A, 0, ORC2, 150.f, 300.f, 1, &s, &st) == SOMVR_FR_VETO && s == -50.f,
          "orc 2 behind, prio: vetoed");
    CHECK(somvr_front_end_pass(&f, &c, 0) == ORC2 && f.prio_warnings == 1, "warned although orc 1 is in view");
    /* a second prio orc while orc 2's warning runs: max_warn 1 does not hold it */
    judge(&f, &c, SLOT_A, 33, ORC1, 20.f, 2.f, &s, 0);
    s = 1.f;
    somvr_front_judge(&f, &c, SLOT_A, 33, ORC3, 150.f, 300.f, 1, &s, 0);
    CHECK(somvr_front_end_pass(&f, &c, 33) == ORC3 && somvr_front_warnings(&f, &c, 33) == 2, "max_warn 1: still warned");
    s = 1.f;
    CHECK(somvr_front_judge(&f, &c, SLOT_A, 1000, ORC2, 150.f, 300.f, 1, &s, 0) == SOMVR_FR_READY && s == 1.f,
          "1 s later: ready (the slot waited 1 s)");
    /* quiet does not hold a prio orc */
    somvr_front_init(&f);
    judge(&f, &c, SLOT_A, 0, ORC2, 150.f, 1.f, &s, 0);
    somvr_front_end_pass(&f, &c, 0);
    judge(&f, &c, SLOT_A, 3100, ORC2, 150.f, 1.f, &s, 0); /* expired unused: quiet */
    CHECK(somvr_front_find(&f, ORC2)->quiet, "quiet");
    s = 1.f;
    somvr_front_judge(&f, &c, SLOT_A, 3200, ORC2, 150.f, 300.f, 1, &s, &st);
    CHECK(somvr_front_end_pass(&f, &c, 3200) == ORC2 && !somvr_front_find(&f, ORC2)->quiet, "prio: warned while quiet");
}

static void test_records(void) {
    somvr_front_cfg c;
    somvr_front_cfg_default(&c);
    somvr_front f;
    somvr_front_init(&f);
    float s;
    for (int i = 0; i < SOMVR_FRONT_ORCS; i++)
        CHECK(judge(&f, &c, SLOT_A, 0, 0x1000 + i, 150.f, 1.f, &s, 0) == SOMVR_FR_VETO, "orc %d vetoed", i);
    CHECK(judge(&f, &c, SLOT_A, 0, 0x9999, 150.f, 1.f, &s, 0) == SOMVR_FR_FULL && s == 1.f, "the 17th: full, unchanged");
    /* 10 s later the old ones are forgotten (not seen, no warning running) */
    somvr_front_end_pass(&f, &c, 0); /* warns one (0x1000..: the first of equal scores) */
    CHECK(judge(&f, &c, SLOT_A, 20000, 0x9999, 150.f, 1.f, &s, 0) == SOMVR_FR_VETO, "20 s later: a record for it");
    somvr_front_end_pass(&f, &c, 20000);
    int n = 0;
    for (int i = 0; i < SOMVR_FRONT_ORCS; i++) n += f.o[i].id != 0;
    CHECK(n == 1, "the forgotten freed at the pass end (%d left)", n);
    /* the clock's wrap */
    somvr_front_init(&f);
    uint32_t t0 = 0xffffff00u;
    judge(&f, &c, SLOT_A, t0, ORC1, 150.f, 1.f, &s, 0);
    CHECK(somvr_front_end_pass(&f, &c, t0) == ORC1, "warned just before the wrap");
    CHECK(judge(&f, &c, SLOT_A, t0 + 999, ORC1, 150.f, 1.f, &s, 0) == SOMVR_FR_WAIT, "wait across the wrap");
    CHECK(judge(&f, &c, SLOT_A, t0 + 1000, ORC1, 150.f, 1.f, &s, 0) == SOMVR_FR_READY, "ready across the wrap (%u)",
          t0 + 1000);
}

/* A fight at the AI's tick (30 Hz): orc 1 in front until 2 s, then it dies; orcs 2 and 3 behind. The grant picks the
 * best positive score (a stand-in for FUN_028d7b50's sort and FUN_028cd4b0); a granted orc attacks for 0.6 s, then
 * the slot re-arms. Nobody behind is granted while orc 1 is accepted in view, and no grant behind comes before 1 s of
 * its warning. */
static void test_fight(void) {
    somvr_front_cfg c;
    somvr_front_cfg_default(&c);
    somvr_front f;
    somvr_front_init(&f);
    uint64_t ids[3] = {ORC1, ORC2, ORC3};
    float off[3] = {20.f, 160.f, 100.f}, base[3] = {1.0f, 1.4f, 1.1f};
    uint64_t granted = 0;
    uint32_t busy_until = 0, warned_at[3] = {0, 0, 0};
    int grants[3] = {0, 0, 0}, early = 0, behind_while_front = 0;
    for (uint32_t t = 0; t <= 12000; t += 33) {
        uint64_t st = 0;
        if (t < busy_until) { /* the slot is taken: no pass (the game skips scoring while a grant runs) */
            st = somvr_front_end_pass(&f, &c, t);
        } else {
            granted = 0;
            float best = 0.f;
            int bi = -1;
            for (int i = 0; i < 3; i++) {
                if (i == 0 && t >= 2000) continue; /* orc 1 is dead */
                float s = base[i];
                uint64_t s1 = 0;
                somvr_front_judge(&f, &c, SLOT_A, t, ids[i], off[i], 400.f, 0, &s, &s1);
                if (s1) st = s1;
                if (s > 0.f && s > best) best = s, bi = i;
            }
            if (bi >= 0) {
                granted = ids[bi];
                grants[bi]++;
                busy_until = t + 600;
                if (bi > 0 && t < 2000) behind_while_front++;
                if (bi > 0 && (!warned_at[bi] || t - warned_at[bi] < 1000)) early++;
                if (bi > 0) warned_at[bi] = 0; /* used: the next grant behind needs a new warning */
                somvr_front_granted(&f, granted);
            }
        }
        for (int i = 0; i < 3; i++)
            if (st == ids[i]) warned_at[i] = t;
    }
    (void)granted;
    CHECK(grants[0] > 0, "orc 1 (in front) attacked (%d)", grants[0]);
    CHECK(behind_while_front == 0, "nobody behind was granted while orc 1 stood in front");
    CHECK(grants[1] > 0 && grants[2] > 0, "after orc 1's death both behind got turns (%d, %d)", grants[1], grants[2]);
    CHECK(early == 0, "no grant behind came before 1 s of its warning");
    CHECK(f.warnings >= 2 && f.used == f.warnings - (uint64_t)somvr_front_warnings(&f, &c, 12000) - f.expired,
          "warnings started %llu, used %llu, expired %llu", (unsigned long long)f.warnings, (unsigned long long)f.used,
          (unsigned long long)f.expired);
}

static void test_damage(void) {
    CHECK(somvr_front_damage(40.f, 0.5f, 0.f) == 20.f, "x0.5");
    CHECK(somvr_front_damage(40.f, 1.f, 0.f) == 40.f, "x1");
    CHECK(somvr_front_damage(-10.f, 0.5f, 0.f) == -10.f && somvr_front_damage(0.f, 0.5f, 0.f) == 0.f, "heals, 0 stay");
    CHECK(somvr_front_damage(5000.f, 0.5f, 1000.f) == 5000.f && somvr_front_damage(999.f, 0.5f, 1000.f) == 499.5f,
          "keep_above: a scripted kill stays");
    CHECK(somvr_front_damage(40.f, NAN, 0.f) == 40.f && somvr_front_damage(40.f, 1.5f, 0.f) == 40.f &&
              somvr_front_damage(40.f, -1.f, 0.f) == 40.f,
          "a bad scale = 1");
}

static void test_camcopy(void) {
    somvr_front_camcopy cc;
    float head[3] = {100.f, 170.f, -50.f};
    float id[4] = {0.f, 0.f, 0.f, 2.f}; /* not normalised */
    somvr_front_camcopy_fill(&cc, head, id, 110.f, 90.f);
    CHECK(cc.quat[3] == 1.f && cc.quat2[3] == 1.f && cc.pos2[0] == 100.f && cc.pos[2] == -50.f, "normalised, copied");
    CHECK(fabsf(cc.fov[0] - 1.919862f) < 1e-5f && fabsf(cc.fov[1] - 1.570796f) < 1e-5f, "FOVs in radians");
    float ahead[3] = {100.f, 170.f, 400.f}, behind[3] = {100.f, 170.f, -900.f}, right[3] = {600.f, 170.f, -50.f};
    CHECK(fabsf(somvr_front_cam_dot(&cc, ahead) - 1.f) < 1e-5f && fabsf(somvr_front_cam_dot(&cc, behind) + 1.f) < 1e-5f &&
              fabsf(somvr_front_cam_dot(&cc, right)) < 1e-5f,
          "identity: +z ahead, -z behind, +x to the side");
    CHECK(somvr_front_cam_dot(&cc, head) == 0.f, "at the position: 0 (the game's guard)");
    float yaw90[4] = {0.f, sinf((float)M_PI / 4), 0.f, cosf((float)M_PI / 4)}; /* +90 deg about +y */
    somvr_front_camcopy_fill(&cc, head, yaw90, 110.f, 90.f);
    CHECK(fabsf(somvr_front_cam_dot(&cc, right) - 1.f) < 1e-5f, "turned 90 deg: +x (right) is ahead");
    float zero[4] = {0, 0, 0, 0}, nanq[4] = {NAN, 0, 0, 1};
    somvr_front_camcopy_fill(&cc, head, zero, 5.f, 400.f);
    CHECK(cc.quat[3] == 1.f && fabsf(cc.fov[0] - 0.349066f) < 1e-5f && fabsf(cc.fov[1] - 2.967060f) < 1e-5f,
          "a zero quaternion = identity; FOVs clamped 20..170");
    somvr_front_camcopy_fill(&cc, head, nanq, NAN, 90.f);
    CHECK(cc.quat[3] == 1.f && fabsf(cc.fov[0] - 1.919862f) < 1e-5f, "a NaN quaternion = identity; a NaN FOV = 110");
}


/* ---- build 19: the crowd's pace ---- */
static void test_pace(void) {
    somvr_pace_cfg c;
    somvr_pace_cfg_default(&c);
    CHECK(NEAR(c.gap_s, 0.7f) && NEAR(c.lead_s, 0.85f) && NEAR(c.start_s, 2.5f) && NEAR(c.swing_s, 0.8f),
          "pace defaults %.2f %.2f %.2f %.2f", c.gap_s, c.lead_s, c.start_s, c.swing_s);
    somvr_pace p;
    somvr_pace_init(&p);
    double ft = 0;
    CHECK(somvr_pace_may(&p, &c, ORC1, 10.0, &ft) && ft == 10.0, "nothing held: may (%.2f)", ft);
    /* a grant holds the turn for everyone else (its own orc keeps its score: a veto must not take back its grant) */
    CHECK(somvr_pace_grant(&p, &c, ORC1, 10.0) == 0, "the first grant: no overlap");
    CHECK(!somvr_pace_may(&p, &c, ORC2, 10.1, &ft) && somvr_pace_may(&p, &c, ORC1, 10.1, NULL) && ft >= 10.9 - 1e-9,
          "a pending grant holds for the others, not its own orc (free from %.2f)", ft);
    /* its blow starts 0.5 s later, the impact 0.8 s after: held until 11.3 (gap 0.7 < lead 0.85: nothing after it) */
    CHECK(somvr_pace_start(&p, &c, ORC1, 10.5, 0.8f, 0) == SOMVR_PACE_GRANTED, "the start matches the grant");
    CHECK(!somvr_pace_may(&p, &c, ORC2, 11.0, &ft) && NEAR(ft, 11.3), "held until its impact (%.3f)", ft);
    somvr_pace_turn out[4];
    double gaps[4];
    CHECK(somvr_pace_update(&p, &c, 11.29, out, gaps, 4) == 0, "not landed before the impact");
    CHECK(somvr_pace_update(&p, &c, 11.31, out, gaps, 4) == 1 && out[0].id == ORC1 && gaps[0] == -1 &&
              NEAR(p.last_impact_t, 11.3) && NEAR(p.lead_seen, 1.3f),
          "landed at 11.3 (the first: gap %.2f, lead seen %.2f)", gaps[0], p.lead_seen);
    CHECK(somvr_pace_may(&p, &c, ORC2, 11.31, NULL) && somvr_pace_may(&p, &c, ORC1, 11.31, NULL),
          "free at the impact: the next one may wind up as this one recovers");
    /* the next: granted at the impact, starting 0.05 s later: its impact 0.85 s after the last one's */
    somvr_pace_grant(&p, &c, ORC2, 11.31);
    somvr_pace_start(&p, &c, ORC2, 11.36, 0.8f, 0);
    CHECK(somvr_pace_update(&p, &c, 12.2, out, gaps, 4) == 1 && NEAR(gaps[0], 0.86) && p.short_gaps == 0 &&
              NEAR(p.lead_seen, 0.85f),
          "the second 0.86 s after the first (lead seen %.2f)", p.lead_seen);
    /* a gap longer than the lead: the release lags the impact by gap - lead */
    somvr_pace_cfg c2 = c;
    c2.gap_s = 1.2f;
    CHECK(!somvr_pace_may(&p, &c2, ORC3, 12.16 + 0.30, &ft) && NEAR(ft, 12.16 + 0.35) &&
              somvr_pace_may(&p, &c2, ORC3, 12.16 + 0.36, NULL),
          "gap 1.2 s: free 0.35 s after the impact (%.3f)", ft);
    /* a faster attack seen (grant -> impact 0.55 s): the lead learnt, the release later by 0.30 s */
    somvr_pace_grant(&p, &c, ORC3, 13.0);
    somvr_pace_start(&p, &c, ORC3, 13.05, 0.5f, 0);
    somvr_pace_update(&p, &c, 13.6, out, gaps, 4);
    CHECK(NEAR(p.lead_seen, 0.55f) && NEAR(somvr_pace_lead(&p, &c), 0.55f) && !somvr_pace_may(&p, &c, ORC1, 13.6, &ft) &&
              NEAR(ft, 13.55 + 0.15),
          "a 0.55 s lead learnt: free 0.15 s after the impact (%.3f)", ft);
    /* a keyless move of a granted orc (a step, not a blow): counted, its turn holds on for the blow */
    somvr_pace_grant(&p, &c, ORC1, 14.0);
    CHECK(somvr_pace_start(&p, &c, ORC1, 14.2, -1.f, 1) == (SOMVR_PACE_MOVE | SOMVR_PACE_GRANTED) && p.moves == 1 &&
              !somvr_pace_may(&p, &c, ORC2, 14.2, NULL),
          "a keyless move: the grant's turn holds");
    CHECK(somvr_pace_start(&p, &c, ORC1, 14.3, 0.8f, 0) == SOMVR_PACE_GRANTED &&
              somvr_pace_update(&p, &c, 15.11, 0, 0, 0) == 1 && !somvr_pace_may(&p, &c, ORC2, 15.2, NULL) &&
              somvr_pace_may(&p, &c, ORC2, 15.26, NULL),
          "... its blow after the move lands and frees it (with the learnt lead: 0.15 s after)");
    CHECK(somvr_pace_start(&p, &c, ORC2, 15.3, -1.f, 1) == SOMVR_PACE_MOVE && somvr_pace_may(&p, &c, ORC2, 15.3, NULL),
          "a keyless move of an orc with no grant: nothing held");
    CHECK(somvr_pace_start(&p, &c, ORC3, 15.3, -1.f, 0) == SOMVR_PACE_MOVE &&
              somvr_pace_start(&p, &c, ORC3, 15.3, 0.f, 0) == SOMVR_PACE_MOVE && somvr_pace_may(&p, &c, ORC2, 15.3, NULL) &&
              p.unannounced == 0,
          "no impact key (a body-collision move) or one at 0 s: not a blow, nothing held");
    /* a grant that never starts a blow: let go after start_s */
    somvr_pace_grant(&p, &c, ORC2, 16.0);
    CHECK(somvr_pace_update(&p, &c, 18.4, out, gaps, 4) == 0 && !somvr_pace_may(&p, &c, ORC3, 18.4, NULL),
          "held 2.4 s after a grant with no start");
    somvr_pace_update(&p, &c, 18.6, out, gaps, 4);
    CHECK(p.let_go == 1 && somvr_pace_may(&p, &c, ORC3, 18.6, NULL), "let go after 2.5 s");
    /* an attack with no grant (unannounced) holds too; another orc starting while it winds up: an overlap */
    CHECK(somvr_pace_start(&p, &c, ORC3, 19.0, 0.8f, 0) == SOMVR_PACE_UNANNOUNCED && !somvr_pace_may(&p, &c, ORC1, 19.4, NULL),
          "an unannounced start holds");
    CHECK(somvr_pace_start(&p, &c, ORC1, 19.4, 0.8f, 0) == (SOMVR_PACE_UNANNOUNCED | SOMVR_PACE_OVERLAP) && p.overlaps == 1,
          "a second wind-up while one runs: an overlap");
    CHECK(somvr_pace_grant(&p, &c, ORC2, 19.5) == 1 && p.overlaps == 2, "a grant while a turn holds: an overlap");
    /* the first ends early (our hit stopped it 0.5 s before its impact): cancelled; the second met the blade 0.1 s
     * before its impact: landed then */
    double g = 0;
    CHECK(somvr_pace_end(&p, &c, ORC3, 19.3, 0.3f, &g) == 0 && p.cancelled == 1 && g == -1, "cancelled early");
    uint64_t imp = p.impacts;
    CHECK(somvr_pace_end(&p, &c, ORC1, 20.1, 0.3f, &g) == 1 && p.impacts == imp + 1 && NEAR(p.last_impact_t, 20.1) &&
              NEAR(g, 5.0),
          "met at the blade 0.1 s before the impact: landed (gap %.2f)", g);
    CHECK(somvr_pace_end(&p, &c, ORC1, 20.2, 0.3f, &g) == -1, "its track's end after: nothing");
    /* the gaps: min, max, under the gap */
    CHECK(p.gaps == 4 && NEAR(p.min_gap, 0.86) && NEAR(p.max_gap, 5.0) && p.short_gaps == 0,
          "gaps %llu min %.2f max %.2f short %llu", (unsigned long long)p.gaps, p.min_gap, p.max_gap,
          (unsigned long long)p.short_gaps);
    /* one grant a pass: prio first, then the score, a tie keeps the first */
    CHECK(somvr_pace_pass_wins(1, 0.5f, 0, 20.f) && !somvr_pace_pass_wins(0, 20.f, 1, 0.5f) &&
              somvr_pace_pass_wins(0, 2.f, 0, 1.f) && !somvr_pace_pass_wins(0, 1.f, 0, 1.f) &&
              !somvr_pace_pass_wins(1, 1.f, 1, 2.f),
          "one grant a pass: the pick");
    /* the turn table full: counted, nothing breaks */
    somvr_pace_init(&p);
    for (int i = 0; i < SOMVR_PACE_TURNS + 2; i++) somvr_pace_grant(&p, &c, 0x1000u + (uint64_t)i, 1.0);
    CHECK(p.full == 2 && !somvr_pace_may(&p, &c, ORC1, 1.5, NULL), "full: %llu", (unsigned long long)p.full);
}

/* FRONT's warnings held while another orc's turn holds (hold_warn): no new warning, counted; released: it starts */
static void test_pace_hold_warn(void) {
    somvr_front_cfg c;
    somvr_front_cfg_default(&c);
    somvr_front f;
    somvr_front_init(&f);
    float s = 3.f;
    uint64_t st = 0;
    f.hold_warn = 1;
    somvr_front_judge(&f, &c, SLOT_A, 1000, ORC2, 150.f, 300.f, 0, &s, &st);
    CHECK(s == c.veto_score && somvr_front_end_pass(&f, &c, 1000) == 0 && f.held == 1 && f.warnings == 0,
          "held: vetoed, no warning (held %llu)", (unsigned long long)f.held);
    s = 3.f;
    somvr_front_judge(&f, &c, SLOT_A, 1050, ORC2, 150.f, 300.f, 1, &s, &st);
    CHECK(somvr_front_end_pass(&f, &c, 1050) == 0 && f.held == 2, "held: a prio orc waits too");
    f.hold_warn = 0;
    s = 3.f;
    somvr_front_judge(&f, &c, SLOT_A, 1100, ORC2, 150.f, 300.f, 0, &s, &st);
    CHECK(somvr_front_end_pass(&f, &c, 1100) == ORC2 && f.warnings == 1, "released: its warning starts");
    CHECK(strcmp(somvr_front_verdict_name(SOMVR_FR_PACE), "pace") == 0 &&
              strcmp(somvr_front_verdict_name(SOMVR_FR_PASS), "pass") == 0,
          "the pace's verdict names");
}

/* a crowd of four at the AI's tick rate (20 a second): the game scores every free orc each pass and would grant up to
 * two a pass (a worst case); grant -> start 0.05..0.95 s, start -> impact 0.8 s (the berserker's fast blow 0.5 s);
 * the pace: one wind-up at a time, every gap >= 0.7 s once the fast lead is learnt (at most one short gap before),
 * every orc gets turns, no lull over 3 s */
static void test_pace_crowd(void) {
    somvr_pace_cfg c;
    somvr_pace_cfg_default(&c);
    somvr_pace p;
    somvr_pace_init(&p);
    const uint64_t ids[4] = {ORC1, ORC2, ORC3, 0x444ull};
    const float imp[4] = {0.8f, 0.8f, 0.8f, 0.5f}; /* the fourth: a berserker's quick blow */
    double grant_t[4] = {0}, start_t[4] = {0}, land_t[4] = {0}, recover_t[4] = {0};
    int turns[4] = {0}, winding_max = 0, overlaps = 0, k = 0;
    double last = -1, min_gap = 99, max_gap = 0;
    int short_gaps = 0;
    for (int ms = 0; ms <= 60000; ms += 50) {
        double t = ms * 0.001;
        somvr_pace_turn out[4];
        double gaps[4];
        int n = somvr_pace_update(&p, &c, t, out, gaps, 4);
        for (int j = 0; j < n; j++) {
            if (last >= 0) {
                double g = out[j].impact_t - last;
                min_gap = g < min_gap ? g : min_gap;
                max_gap = g > max_gap ? g : max_gap;
                short_gaps += g < 0.7 - 1e-6;
            }
            last = out[j].impact_t;
        }
        int winding = 0;
        for (int i = 0; i < 4; i++) { /* the orcs' own clocks: start after the grant, land, recover 1 s */
            if (grant_t[i] > 0 && !(start_t[i] > 0)) {
                double d = 0.05 + 0.9 * (double)((k * 7 + i * 3) % 10) / 9.0;
                if (t >= grant_t[i] + d) {
                    start_t[i] = t, k++;
                    overlaps += (somvr_pace_start(&p, &c, ids[i], t, imp[i], 0) & SOMVR_PACE_OVERLAP) != 0;
                }
            }
            if (start_t[i] > 0 && t >= start_t[i] + imp[i] - 1e-9 && !(land_t[i] > 0)) land_t[i] = t, recover_t[i] = t + 1.0;
            if (land_t[i] > 0 && t >= recover_t[i]) grant_t[i] = start_t[i] = land_t[i] = 0;
            winding += start_t[i] > 0 && !(land_t[i] > 0);
        }
        winding_max = winding > winding_max ? winding : winding_max;
        /* one pass: every free orc scored; our pace and one-a-pass rule; the game grants up to two accepted */
        int kept = -1, accepted = 0;
        float kept_score = 0;
        for (int i = 0; i < 4; i++) {
            if (grant_t[i] > 0) continue;
            float sc = 10.f + (float)((ms / 50 + i * 5) % 7); /* the game's scores move */
            if (!somvr_pace_may(&p, &c, ids[i], t, NULL)) continue;
            if (kept < 0 || somvr_pace_pass_wins(0, sc, 0, kept_score)) kept = i, kept_score = sc;
            accepted++;
        }
        (void)accepted;
        if (kept >= 0) { /* only the kept one has a score >= 0: the game's sort grants it alone */
            overlaps += somvr_pace_grant(&p, &c, ids[kept], t);
            grant_t[kept] = t, turns[kept]++;
        }
    }
    CHECK(winding_max == 1 && overlaps == 0, "one wind-up at a time (max %d, overlaps %d)", winding_max, overlaps);
    CHECK(short_gaps <= 1 && p.short_gaps == (uint64_t)short_gaps, "gaps under 0.7 s: %d (the first fast blow, before "
          "its lead was learnt)", short_gaps);
    CHECK(max_gap <= 3.0, "no lull: the longest gap %.2f s (shortest %.2f)", max_gap, min_gap);
    CHECK(turns[0] > 3 && turns[1] > 3 && turns[2] > 3 && turns[3] > 3, "every orc got turns (%d %d %d %d)", turns[0],
          turns[1], turns[2], turns[3]);
    CHECK(p.impacts > 40 && p.overlaps == 0 && p.unannounced == 0, "impacts %llu in 60 s", (unsigned long long)p.impacts);
    /* the same crowd without the pace: blows overlap (the rule's reason) */
    double gt[4] = {0}, stt[4] = {0}, lt[4] = {0};
    int wmax = 0;
    for (int ms = 0; ms <= 20000; ms += 50) {
        double t = ms * 0.001;
        int w = 0;
        for (int i = 0; i < 4; i++) {
            if (!(gt[i] > 0)) gt[i] = t; /* the game grants every free orc */
            if (!(stt[i] > 0) && t >= gt[i] + 0.05 + 0.1 * i) stt[i] = t;
            if (stt[i] > 0 && !(lt[i] > 0) && t >= stt[i] + imp[i]) lt[i] = t;
            if (lt[i] > 0 && t >= lt[i] + 1.0) gt[i] = stt[i] = lt[i] = 0;
            w += stt[i] > 0 && !(lt[i] > 0);
        }
        wmax = w > wmax ? w : wmax;
    }
    CHECK(wmax >= 3, "without the pace %d orcs wind up at once", wmax);
}

/* build 19, the answer hold (re-xr-pipeline.md 70.7): a berserker whose blow hit Talion answers a cut of his while he
 * recoils (its headbutt / bash, no grant): its turn stays its own after the impact - verdict_s for DEFENCE's verdict,
 * then answer_s after a hit (or until released: Talion's recoil over) - so the answer starts in its own turn */
static void test_pace_answer(void) {
    somvr_pace_cfg c;
    somvr_pace_cfg_default(&c);
    CHECK(c.answer_s == 0.f && NEAR(c.verdict_s, 0.6f), "answer defaults: off, verdict %.2f", c.verdict_s);
    c.verdict_s = 0.25f; /* the arithmetic below */
    somvr_pace p;
    somvr_pace_init(&p);
    somvr_pace_turn out[4];
    double gaps[4], ft = 0, since = 0;
    /* off: no mark, the turn ends at the impact as before */
    somvr_pace_grant(&p, &c, ORC1, 10.0);
    CHECK(somvr_pace_may_answer(&p, &c, ORC1) == 0, "off: no answer mark");
    somvr_pace_start(&p, &c, ORC1, 10.05, 0.8f, 0);
    CHECK(somvr_pace_update(&p, &c, 10.9, out, gaps, 4) == 1 && somvr_pace_may(&p, &c, ORC2, 10.9, NULL) &&
              !somvr_pace_holding(&p, ORC1, NULL),
          "off: free at the impact");
    c.answer_s = 3.0f;
    somvr_pace_init(&p);
    /* the berserker's grant and blow; it may answer */
    somvr_pace_grant(&p, &c, ORC1, 10.0);
    CHECK(somvr_pace_may_answer(&p, &c, ORC1) == 1 && somvr_pace_may_answer(&p, &c, ORC2) == 0, "marked (only a live turn)");
    somvr_pace_start(&p, &c, ORC1, 10.05, 0.8f, 0);
    CHECK(somvr_pace_update(&p, &c, 10.9, out, gaps, 4) == 1 && out[0].id == ORC1 && p.impacts == 1,
          "its blow landed at 10.85 (counted)");
    CHECK(somvr_pace_holding(&p, ORC1, &since) == 1 && NEAR(since, 10.85) && !somvr_pace_may(&p, &c, ORC2, 10.9, &ft) &&
              NEAR(ft, 11.1) && somvr_pace_may(&p, &c, ORC1, 10.9, NULL) && p.holds_wait == 1,
          "the turn waits verdict_s for the verdict (free from %.2f; its own orc may be granted)", ft);
    /* DEFENCE: the blow hit Talion: held on for answer_s */
    CHECK(somvr_pace_verdict(&p, &c, ORC1, 10.9, 1) == 1 && somvr_pace_holding(&p, ORC1, NULL) == 2 && p.holds == 1 &&
              !somvr_pace_may(&p, &c, ORC2, 12.0, &ft) && NEAR(ft, 13.9),
          "a hit: held to %.2f", ft);
    CHECK(somvr_pace_update(&p, &c, 12.0, out, gaps, 4) == 0 && somvr_pace_holding(&p, ORC1, NULL) == 2,
          "the hold is no landing and does not run out early");
    /* our cut at 11.4, its answer (the headbutt) at 11.5: in its own turn (no overlap, not unannounced) */
    int fl = somvr_pace_start(&p, &c, ORC1, 11.5, 0.8f, 0);
    CHECK(fl == SOMVR_PACE_ANSWER && p.answers == 1 && p.unannounced == 0 && p.overlaps == 0 &&
              !somvr_pace_holding(&p, ORC1, NULL) && !somvr_pace_may(&p, &c, ORC2, 12.0, &ft) && NEAR(ft, 12.3),
          "its answer: flags %d, held to its impact %.2f", fl, ft);
    /* the answer lands at 12.3: a hold again (it may answer again), its verdict no hit (dodged): free */
    CHECK(somvr_pace_update(&p, &c, 12.31, out, gaps, 4) == 1 && NEAR(gaps[0], 1.45) && somvr_pace_holding(&p, ORC1, NULL) == 1,
          "the answer landed (gap %.2f), waiting for its verdict", gaps[0]);
    CHECK(somvr_pace_verdict(&p, &c, ORC1, 12.32, 0) == 0 && p.no_hit == 1 && somvr_pace_may(&p, &c, ORC2, 12.32, NULL) &&
              somvr_pace_verdict(&p, &c, ORC1, 12.33, 1) == -1,
          "no hit: free at once (a late verdict finds nothing)");
    /* the verdict before the landing (the same frame): the landing holds at once */
    somvr_pace_grant(&p, &c, ORC1, 13.0);
    somvr_pace_may_answer(&p, &c, ORC1);
    somvr_pace_start(&p, &c, ORC1, 13.1, 0.7f, 0);
    CHECK(somvr_pace_verdict(&p, &c, ORC1, 13.8, 1) == 1 && somvr_pace_holding(&p, ORC1, NULL) == 0,
          "a verdict on a started blow is kept for its landing");
    somvr_pace_update(&p, &c, 13.81, out, gaps, 4);
    CHECK(somvr_pace_holding(&p, ORC1, &since) == 2 && NEAR(since, 13.8) && p.holds == 2 && !somvr_pace_may(&p, &c, ORC2, 16.7, &ft) &&
              NEAR(ft, 16.8),
          "held from its landing to %.2f", ft);
    /* released: Talion's recoil over */
    CHECK(somvr_pace_release(&p, ORC1) == 1 && p.released == 1 && somvr_pace_may(&p, &c, ORC2, 15.0, NULL) &&
              somvr_pace_release(&p, ORC1) == 0,
          "released: free");
    /* a hold runs out (no answer, no release) */
    somvr_pace_grant(&p, &c, ORC1, 20.0);
    somvr_pace_may_answer(&p, &c, ORC1);
    somvr_pace_start(&p, &c, ORC1, 20.1, 0.8f, 0);
    somvr_pace_update(&p, &c, 20.95, out, gaps, 4);
    somvr_pace_verdict(&p, &c, ORC1, 20.95, 1);
    CHECK(somvr_pace_update(&p, &c, 23.9, out, gaps, 4) == 0 && somvr_pace_holding(&p, ORC1, NULL) == 2 &&
              somvr_pace_update(&p, &c, 23.96, out, gaps, 4) == 0 && !somvr_pace_holding(&p, ORC1, NULL) && p.expired == 1 &&
              somvr_pace_may(&p, &c, ORC2, 23.96, NULL),
          "the hold ran out at %.2f (expired %llu)", 20.9 + 3.0, (unsigned long long)p.expired);
    /* no verdict at all: the wait runs out after verdict_s */
    somvr_pace_grant(&p, &c, ORC1, 30.0);
    somvr_pace_may_answer(&p, &c, ORC1);
    somvr_pace_start(&p, &c, ORC1, 30.1, 0.8f, 0);
    somvr_pace_update(&p, &c, 30.91, out, gaps, 4);
    CHECK(somvr_pace_holding(&p, ORC1, NULL) == 1 && somvr_pace_update(&p, &c, 31.16, out, gaps, 4) == 0 &&
              !somvr_pace_holding(&p, ORC1, NULL) && p.expired == 2,
          "no verdict: free after %.2f s", c.verdict_s);
    /* a grant of the holding orc itself replaces its hold (its next blow is a turn like any) */
    somvr_pace_grant(&p, &c, ORC1, 40.0);
    somvr_pace_may_answer(&p, &c, ORC1);
    somvr_pace_start(&p, &c, ORC1, 40.1, 0.8f, 0);
    somvr_pace_update(&p, &c, 40.95, out, gaps, 4);
    somvr_pace_verdict(&p, &c, ORC1, 40.95, 1);
    CHECK(somvr_pace_grant(&p, &c, ORC1, 41.5) == 0 && !somvr_pace_holding(&p, ORC1, NULL) &&
              somvr_pace_start(&p, &c, ORC1, 41.6, 0.8f, 0) == SOMVR_PACE_GRANTED,
          "granted again in its hold: a granted blow");
    /* its blow cut short by our cut (somvr_pace_end: met, or cancelled): its answer hold too */
    somvr_pace_update(&p, &c, 42.5, out, gaps, 4);
    somvr_pace_init(&p);
    somvr_pace_grant(&p, &c, ORC1, 50.0);
    somvr_pace_may_answer(&p, &c, ORC1);
    somvr_pace_start(&p, &c, ORC1, 50.1, 0.7f, 0);
    double g = 0;
    CHECK(somvr_pace_end(&p, &c, ORC1, 50.4, 0.3f, &g) == 0 && p.cancelled == 1 && somvr_pace_holding(&p, ORC1, NULL) == 1 &&
              !somvr_pace_may(&p, &c, ORC2, 50.5, NULL) && somvr_pace_verdict(&p, &c, ORC1, 50.45, 1) == 1 &&
              somvr_pace_start(&p, &c, ORC1, 50.5, 0.27f, 0) == SOMVR_PACE_ANSWER,
          "cut short: held, promoted, its bash in its own turn");
    CHECK(somvr_pace_end(&p, &c, ORC1, 50.75, 0.3f, &g) == 1 && somvr_pace_holding(&p, ORC1, NULL) == 1,
          "its bash met at its impact: held for its verdict");
    somvr_pace_init(&p);
    /* an unmarked orc (an infantry) is not held */
    somvr_pace_update(&p, &c, 42.5, out, gaps, 4);
    somvr_pace_grant(&p, &c, ORC2, 43.0);
    somvr_pace_start(&p, &c, ORC2, 43.1, 0.8f, 0);
    CHECK(somvr_pace_update(&p, &c, 43.95, out, gaps, 4) == 1 && !somvr_pace_holding(&p, ORC2, NULL) &&
              somvr_pace_verdict(&p, &c, ORC2, 43.95, 1) == -1 && somvr_pace_may(&p, &c, ORC3, 43.95, NULL),
          "an unmarked orc: free at its impact");
}

int main(void) {
    test_basic();
    test_warning();
    test_passes();
    test_prio();
    test_records();
    test_fight();
    test_damage();
    test_camcopy();
    test_pace();
    test_pace_hold_warn();
    test_pace_crowd();
    test_pace_answer();
    printf("test_front: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
