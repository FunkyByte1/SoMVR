/* test_front.c - front-first enemies (docs/re-xr-pipeline.md 42): somvr_front.c - the veto after the game's score (in
 * view, the game's own rejections, far orcs, no id), the warning at a pass's end (the best-scored vetoed orc, only when
 * nobody in view was accepted, max_warn), wait -> ready -> expiry -> quiet -> a new warning, pass boundaries (another
 * slot, a later tick), the records (full, forgotten), the clock's wrap, a fight at the AI's tick rate (no orc behind
 * the player is granted before its warning ran 1 s), the damage scale, and the AI's camera copy against the game's
 * own camera-term formula. */
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

int main(void) {
    test_basic();
    test_warning();
    test_passes();
    test_prio();
    test_records();
    test_fight();
    test_damage();
    test_camcopy();
    printf("test_front: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
