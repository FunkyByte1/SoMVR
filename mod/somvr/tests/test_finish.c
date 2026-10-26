/* test_finish.c - SOMVR_FINISH's pure part (somvr_finish.c): the classes of the game's attacks by name (the defaults
 * on the doc's real names and on guessed ones, case, NULL, the node fallback, a replaced list, "-", the limits), the
 * pick, the state kinds; the zones (estimated, from the Head node, a downed orc), "from behind" at every side and a
 * turned orc, the heading of a quaternion against sv_heading; the execution judge on a scripted swing through the neck
 * (OK at the contact frame and only there; not ready, began before ready vs within the lead, too slow, too short, a
 * poke, a chest-high swing, no swing, no blade, 200 random swings against the rules), the ground judge (a downward stab
 * into the torso; tilted, slow, the base first, not down), the stealth judge (a stab into the upper back, a slash across
 * the neck, from the side, slow, the base, a miss, the behind angle swept); the glow (on, the linger, a flicker, forced
 * off, counts); the latch (a new squeeze at the neck, held from the start, far, no orc, the trigger and the button holds
 * once per latch, both, release, lost by distance and by the orc); the moment's cooldown and claim. */
#include "../somvr_finish.h"
#include "../somvr_pose.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
#define DEG 0.01745329252f

static float frand(float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; }

/* ---- names ------------------------------------------------------------------------------------------------------- */
static void test_names(void) {
    sf_names n;
    sf_names_default(&n);
    struct { const char *node, *atk; int cls; } cases[] = {
        { NULL, "PC_Bip_SneakInstaKill_B", SF_CLS_STEALTH },     /* re-combat-items.md 4 */
        { NULL, "PC_Bip_Sneak_InstantKill_Loud_B", SF_CLS_STEALTH },
        { NULL, "PC_Bip_StealthTorment_F", SF_CLS_BRUTAL },      /* not stealth: brutalize is tested first */
        { NULL, "PC_Bip_Wall_Torment_Hang", SF_CLS_BRUTAL },
        { NULL, "DLC2_PC_StealthBrutal_Resist_B", SF_CLS_BRUTAL },
        { NULL, "PC_Bip_StealthBrand_B", SF_CLS_BRAND },
        { NULL, "pc_bip_execution_f_1", SF_CLS_EXEC },           /* case-insensitive */
        { NULL, "PC_Bip_GroundExecution_1", SF_CLS_GROUND },     /* not exec: ground is tested first */
        { NULL, "PC_Bip_Drain_Stunned_F", SF_CLS_DRAIN },
        { NULL, "PC_Bip_Interrogate_Loop", SF_CLS_INTERROGATE },
        { NULL, "PC_HC_High_L1_D0_F_RR_LL_08", SF_CLS_NONE },     /* a stock strike */
        { NULL, "PC_Bip_Sync_Cntr_F", SF_CLS_NONE },              /* a counter */
        { "PC_Node_Execute", "PC_HC_High_L1_D0_F_1", SF_CLS_EXEC }, /* the node's name when the record says nothing */
        { "PC_Node_Execute", "PC_Bip_Drain_F", SF_CLS_DRAIN },      /* the record's name first */
        { NULL, NULL, SF_CLS_NONE },
        { "", "", SF_CLS_NONE },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int k = sf_class_of(&n, cases[i].node, cases[i].atk);
        CHECK(k == cases[i].cls, "class of %s/%s = %s, want %s", cases[i].node ? cases[i].node : "-",
              cases[i].atk ? cases[i].atk : "-", sf_class_name(k), sf_class_name(cases[i].cls));
    }
    CHECK(sf_contains("ABCdef", "cDe") && !sf_contains("abc", "") && !sf_contains(NULL, "a") && !sf_contains("a", NULL) &&
              !sf_contains("ab", "abc") && sf_contains("aab", "ab"), "contains");
    /* a replaced list */
    CHECK(sf_names_set(&n, SF_CLS_EXEC, " PC_Bip_Fin_ , ,Kill_Exec ") == 2 && !strcmp(n.pat[SF_CLS_EXEC][0], "PC_Bip_Fin_") &&
              !strcmp(n.pat[SF_CLS_EXEC][1], "Kill_Exec"), "set: trimmed, empty skipped");
    CHECK(sf_class_of(&n, NULL, "PC_Bip_Execution_F") == SF_CLS_NONE && sf_class_of(&n, NULL, "pc_bip_fin_3") == SF_CLS_EXEC,
          "the replaced list is the only one");
    CHECK(sf_names_set(&n, SF_CLS_STEALTH, "-") == 0 && sf_class_of(&n, NULL, "PC_Bip_SneakInstaKill_B") == SF_CLS_NONE,
          "'-' = never");
    CHECK(sf_names_set(&n, SF_CLS_DRAIN, "a,b,c,d,e,f,g,h,i,j") == SF_PAT_MAX, "at most SF_PAT_MAX");
    char longp[100];
    memset(longp, 'x', sizeof longp - 1), longp[sizeof longp - 1] = 0;
    CHECK(sf_names_set(&n, SF_CLS_BRAND, longp) == 1 && strlen(n.pat[SF_CLS_BRAND][0]) == SF_PAT_LEN - 1, "cut");
    CHECK(sf_names_set(&n, SF_CLS_NONE, "x") == 0 && sf_names_set(&n, SF_CLS_N, "x") == 0 && sf_names_set(&n, SF_CLS_EXEC, NULL) == 0,
          "bad class, NULL list");
    for (int k = 1; k < SF_CLS_N; k++)
        CHECK(sf_class_find(sf_class_name(k)) == k, "find %s", sf_class_name(k));
    CHECK(sf_class_find("nope") == -1 && sf_class_find(NULL) == -1 && !strcmp(sf_class_name(99), "?") &&
              !strcmp(sf_class_name(0), "none"), "names");
    int cl[] = { SF_CLS_NONE, SF_CLS_EXEC, SF_CLS_GROUND, SF_CLS_EXEC };
    CHECK(sf_pick(cl, 4, SF_CLS_GROUND, SF_CLS_EXEC) == 2 && sf_pick(cl, 4, SF_CLS_EXEC, SF_CLS_NONE) == 1 &&
              sf_pick(cl, 4, SF_CLS_DRAIN, SF_CLS_EXEC) == 1 && sf_pick(cl, 4, SF_CLS_DRAIN, SF_CLS_BRAND) == -1 &&
              sf_pick(cl, 4, SF_CLS_NONE, SF_CLS_NONE) == -1 && sf_pick(cl, 0, SF_CLS_EXEC, SF_CLS_NONE) == -1,
          "pick");
    /* the states */
    CHECK(sf_state_kind("Combat_State_Stunned") == SF_ST_STUN && sf_state_kind("WraithStun") == SF_ST_STUN &&
              sf_state_kind("KnockDown_Front") == SF_ST_DOWN && sf_state_kind("State_KnockedDown") == SF_ST_DOWN &&
              sf_state_kind("KD_Back") == SF_ST_DOWN && sf_state_kind("Struggle_Pinned") == SF_ST_OTHER &&
              sf_state_kind("") == SF_ST_NONE && sf_state_kind(NULL) == SF_ST_NONE,
          "state kinds");
    CHECK(!strcmp(sf_state_name(SF_ST_DOWN), "down") && !strcmp(sf_state_name(9), "?"), "state names");
}

/* ---- geometry ---------------------------------------------------------------------------------------------------- */
static void test_geometry(const sf_cfg *c) {
    float root[3] = { 100, 50, -20 };
    sf_zones z;
    sf_zones_of(c, root, 0, NULL, 0, &z);
    CHECK(NEAR(z.neck[0], 100, 1e-4) && NEAR(z.neck[1], 112, 1e-4) && NEAR(z.neck[2], -20, 1e-4), "neck");
    CHECK(NEAR(z.head[1], 136, 1e-4) && NEAR(z.head[0], 100, 1e-4), "head");
    CHECK(NEAR(z.back[0], 100, 1e-4) && NEAR(z.back[1], 96, 1e-4) && NEAR(z.back[2], -34, 1e-4), "back behind (+Z facing)");
    CHECK(NEAR(z.torso[1], 68, 1e-4), "torso");
    sf_zones_of(c, root, 90 * DEG, NULL, 0, &z);
    CHECK(NEAR(z.back[0], 86, 1e-3) && NEAR(z.back[2], -20, 1e-3), "back behind (+X facing)");
    float hn[3] = { 103, 120, -18 };
    sf_zones_of(c, root, 0, hn, 0, &z);
    CHECK(NEAR(z.neck[0], 103, 1e-4) && NEAR(z.neck[1], 114, 1e-4) && NEAR(z.head[1], 138, 1e-4), "from the Head node");
    sf_zones_of(c, root, 0, hn, 1, &z);
    CHECK(!memcmp(z.neck, z.torso, sizeof z.neck) && !memcmp(z.head, z.torso, sizeof z.head) && NEAR(z.torso[1], 68, 1e-4),
          "down: everything at the torso");
    float o[3] = { 0, 0, 0 };
    float p_front[3] = { 0, 30, 100 }, p_back[3] = { 0, 0, -100 }, p_side[3] = { 100, 0, 0 }, p_on[3] = { 0.5f, 90, 0 };
    CHECK(NEAR(sf_behind_deg(o, 0, p_front), 0, 1e-3) && NEAR(sf_behind_deg(o, 0, p_back), 180, 1e-3) &&
              NEAR(sf_behind_deg(o, 0, p_side), 90, 1e-3) && sf_behind_deg(o, 0, p_on) == -1,
          "behind: front %.1f back %.1f side %.1f", sf_behind_deg(o, 0, p_front), sf_behind_deg(o, 0, p_back),
          sf_behind_deg(o, 0, p_side));
    float p_w[3] = { -100, 0, 0 };
    CHECK(NEAR(sf_behind_deg(o, 90 * DEG, p_w), 180, 1e-3) && NEAR(sf_behind_deg(o, -90 * DEG, p_w), 0, 1e-3), "turned orc");
    for (int i = 0; i < 300; i++) {
        sv_quat q = sv_qnormalize((sv_quat){ frand(-1, 1), frand(-1, 1), frand(-1, 1), frand(-1, 1) });
        float qa[4];
        sv_quat_to(q, qa);
        float a = sf_yaw_of_quat(qa), b = sv_heading(q), d = fabsf(a - b);
        if (d > 3.14159f) d = 6.28319f - d;
        CHECK(d < 1e-3f, "yaw %f vs sv_heading %f", a, b);
    }
    float qy[4] = { 0, sinf(0.6f), 0, cosf(0.6f) };
    CHECK(NEAR(sf_yaw_of_quat(qy), 1.2f, 1e-5), "yaw of a pure Y turn");
}

/* ---- blades ------------------------------------------------------------------------------------------------------ */
/* a level blade turning about pivot (the shoulder) at heading th: base 20 cm, tip 100 cm out; w rad/s */
static void level_blade(const float pivot[3], float th, float w, double t, somvr_blade *b) {
    float d[3] = { sinf(th), 0, cosf(th) }, tv[3] = { cosf(th), 0, -sinf(th) };
    memset(b, 0, sizeof *b);
    b->valid = 1, b->t = t;
    for (int k = 0; k < 3; k++) {
        b->base[k] = pivot[k] + 20 * d[k];
        b->tip[k] = pivot[k] + 100 * d[k];
        b->vel_tip[k] = w * 100 * tv[k];
    }
}
static void line_blade(const float base[3], const float dir[3], float len, const float vel[3], double t, somvr_blade *b) {
    memset(b, 0, sizeof *b);
    b->valid = 1, b->t = t;
    for (int k = 0; k < 3; k++) {
        b->base[k] = base[k];
        b->tip[k] = base[k] + len * dir[k];
        b->vel_tip[k] = vel[k];
    }
}

/* the combat module's orc capsule about a root (re-xr-pipeline.md 6.3: 35 cm, 90 cm down and up; the blade's 3 cm) */
static int body_contact(const somvr_blade *prev, const somvr_blade *cur, const float root[3], sc_contact *ct) {
    float c0[3] = { root[0], root[1] - 90, root[2] }, c1[3] = { root[0], root[1] + 90, root[2] };
    return sc_sweep_capsule(prev, cur, 3, c0, c1, 35, 4, ct);
}

/* A swing from th0 to th1 at w rad/s about a pivot `drop` cm below the neck height and 80 cm in front of the neck (so
 * the blade's 80 cm point crosses the orc's axis at th = 0), judged as the game side does: at the first frame the
 * blade meets the body capsule. Returns that verdict (MISS: no contact); *at = its frame, *oks = OK verdicts over all
 * the contact frames (the game side judges the first only). */
static int run_swing(const sf_cfg *c, float th0, float th1, float w, float drop, int ready, double ready_since,
                     int *at, int *oks) {
    sc_cfg sc;
    sc_cfg_default(&sc);
    sc_speed sp;
    sc_speed_reset(&sp);
    float root[3] = { 0, 0, 0 };
    sf_zones z;
    sf_zones_of(c, root, 180 * DEG, NULL, 0, &z); /* the orc faces the player at -Z */
    float pivot[3] = { z.neck[0], z.neck[1] - drop, z.neck[2] - 80 };
    somvr_blade prev, cur;
    memset(&prev, 0, sizeof prev);
    double t = 10.0, swing_t0 = -1;
    int first = SF_WHY_MISS;
    *at = -1, *oks = 0;
    for (int k = 0; k < 200; k++, t += 1.0 / 90) {
        float th = th0 + (float)(w * (t - 10.0)) * (th1 > th0 ? 1.0f : -1.0f);
        if ((th1 > th0 && th > th1) || (th1 < th0 && th < th1)) break;
        level_blade(pivot, th, th1 > th0 ? w : -w, t, &cur);
        int e = sc_speed_push(&sp, &sc, &cur);
        if (e > 0) swing_t0 = t;
        if (e < 0) swing_t0 = -1;
        sc_contact ct;
        if (body_contact(k ? &prev : NULL, &cur, root, &ct)) {
            int why = sf_judge_exec_at(c, &sp, swing_t0, ct.point, &z, ready, ready_since);
            if (*at < 0) *at = k, first = why;
            *oks += why == SF_WHY_OK;
        }
        prev = cur;
    }
    return first;
}

static void test_exec(const sf_cfg *c) {
    int at, oks;
    /* a committed cut right to left at neck height at 7 m/s from 70 deg out */
    int why = run_swing(c, -70 * DEG, 70 * DEG, 7, 0, 1, 0, &at, &oks);
    CHECK(why == SF_WHY_OK && at > 0, "the cut: %s at %d", sf_why_name(why), at);
    /* the blade's 80 cm point reaches the 38 cm capsule about 28 deg before the axis: the judged frame */
    float th_at = -70 * DEG + 7.0f * (float)at / 90;
    CHECK(th_at < -20 * DEG && th_at > -35 * DEG, "judged where the blade meets the body: %.1f deg", th_at / DEG);
    why = run_swing(c, 70 * DEG, -70 * DEG, 7, 0, 1, 0, &at, &oks);
    CHECK(why == SF_WHY_OK, "the backhand: %s", sf_why_name(why));
    CHECK(run_swing(c, -70 * DEG, 70 * DEG, 7, 0, 0, 0, &at, &oks) == SF_WHY_NOTREADY, "not ready");
    /* ready since 0.2 s into the swing (began 10.0): too early; 0.1 s: within the lead */
    CHECK(run_swing(c, -70 * DEG, 70 * DEG, 7, 0, 1, 10.2, &at, &oks) == SF_WHY_EARLY, "began well before ready");
    CHECK(run_swing(c, -70 * DEG, 70 * DEG, 7, 0, 1, 10.1, &at, &oks) == SF_WHY_OK, "began within the lead");
    why = run_swing(c, -70 * DEG, 70 * DEG, 3.8f, 0, 1, 0, &at, &oks);
    CHECK(why == SF_WHY_SLOW, "3.8 m/s: %s", sf_why_name(why));
    why = run_swing(c, -12 * DEG, 40 * DEG, 7, 0, 1, 0, &at, &oks);
    CHECK(why == SF_WHY_SHORT, "a flick from inside the body: %s", sf_why_name(why));
    why = run_swing(c, -70 * DEG, 70 * DEG, 7, 45, 1, 0, &at, &oks);
    CHECK(why == SF_WHY_LOW, "chest high: %s", sf_why_name(why));
    why = run_swing(c, -70 * DEG, 70 * DEG, 7, -45, 1, 0, &at, &oks);
    CHECK(why == SF_WHY_HIGH, "over the head: %s", sf_why_name(why));
    why = run_swing(c, -70 * DEG, 70 * DEG, 7, -90, 1, 0, &at, &oks);
    CHECK(why == SF_WHY_MISS, "far over the head: %s", sf_why_name(why));
    why = run_swing(c, -70 * DEG, 70 * DEG, 7, 12, 1, 0, &at, &oks);
    CHECK(why == SF_WHY_OK, "the throat, 12 cm below the neck: %s", sf_why_name(why));
    why = run_swing(c, -70 * DEG, 70 * DEG, 7, -30, 1, 0, &at, &oks);
    CHECK(why == SF_WHY_OK, "the head, 30 cm above the neck: %s", sf_why_name(why));
    /* a thrust at the neck along the blade at 6 m/s */
    {
        sc_cfg sc;
        sc_cfg_default(&sc);
        sc_speed sp;
        sc_speed_reset(&sp);
        float root[3] = { 0, 0, 0 }, dir[3] = { 0, 0, 1 }, vel[3] = { 0, 0, 600 };
        sf_zones z;
        sf_zones_of(c, root, 180 * DEG, NULL, 0, &z);
        somvr_blade prev, cur;
        double t = 5, t0 = -1;
        int got = -1;
        for (int k = 0; k < 40 && got < 0; k++, t += 1.0 / 90) {
            float base[3] = { z.neck[0], z.neck[1] + 5, z.neck[2] - 200 + 600 * (float)(t - 5) };
            line_blade(base, dir, 90, vel, t, &cur);
            int e = sc_speed_push(&sp, &sc, &cur);
            if (e > 0) t0 = t;
            sc_contact ct;
            if (body_contact(k ? &prev : NULL, &cur, root, &ct)) got = sf_judge_exec_at(c, &sp, t0, ct.point, &z, 1, 0);
            prev = cur;
        }
        CHECK(got == SF_WHY_POKE, "a thrust: %s", sf_why_name(got));
    }
    {
        sc_speed sp;
        sc_speed_reset(&sp);
        float root[3] = { 0, 0, 0 }, pt[3] = { 0, 62, -38 };
        sf_zones z;
        sf_zones_of(c, root, 180 * DEG, NULL, 0, &z);
        CHECK(sf_judge_exec_at(c, &sp, -1, pt, &z, 1, 0) == SF_WHY_NOSWING, "a still blade at the neck");
    }
    /* random swings: the verdict follows the rules */
    srand(7);
    int okn = 0;
    for (int i = 0; i < 200; i++) {
        float w = frand(2, 10), drop = frand(-60, 50), a0 = frand(-90, -5) * DEG;
        int r = run_swing(c, a0, 80 * DEG, w, drop, 1, 0, &at, &oks);
        int expect_ok = w >= c->exec_mps + 0.3f && a0 <= -62 * DEG && drop <= c->exec_below_cm - 3 &&
                        drop >= -(c->head_up_cm + c->exec_above_cm - 3) && drop > -40;
        if (r == SF_WHY_OK) okn++;
        if (expect_ok) CHECK(r == SF_WHY_OK, "random swing %d: w %.1f drop %.1f from %.0f deg: %s", i, w, drop, a0 / DEG,
                             sf_why_name(r));
        if (w < c->exec_mps - 0.3f) CHECK(r != SF_WHY_OK, "random slow swing %d (%.1f m/s) passed", i, w);
        if (drop > c->exec_below_cm + 1 || drop < -(c->head_up_cm + c->exec_above_cm + 1))
            CHECK(r != SF_WHY_OK, "random swing %d %.0f cm off passed", i, drop);
    }
    CHECK(okn > 20 && okn < 200, "random: %d OK", okn);
    /* the sword's stab from behind, at the contact */
    {
        sc_speed sp;
        sc_speed_reset(&sp);
        float root[3] = { 0, 0, 0 }, up[3] = { 0, 50, -36 }, low[3] = { 0, 2, -36 };
        sf_zones z;
        sf_zones_of(c, root, 0, NULL, 0, &z); /* faces +Z: its back at -Z */
        sp.axis_mps = 2.0f, sp.cos_axis = 0.95f, sp.mean_mps = 2.1f;
        CHECK(sf_judge_back_at(c, &sp, up, 0.9f, &z, 175) == SF_WHY_OK, "a stab into the back");
        CHECK(sf_judge_back_at(c, &sp, up, 0.9f, &z, 100) == SF_WHY_FRONT, "from the side");
        CHECK(sf_judge_back_at(c, &sp, up, 0.2f, &z, 175) == SF_WHY_TIP, "the hilt first");
        CHECK(sf_judge_back_at(c, &sp, low, 0.9f, &z, 175) == SF_WHY_LOW, "into the legs");
        sp.axis_mps = 0.5f, sp.mean_mps = 0.6f;
        CHECK(sf_judge_back_at(c, &sp, up, 0.9f, &z, 175) == SF_WHY_NOTSTAB, "a push");
        sp.mean_mps = 3.0f, sp.cos_axis = 0.1f;
        CHECK(sf_judge_back_at(c, &sp, up, 0.9f, &z, 175) == SF_WHY_OK, "a slash across the back");
    }
}

static void test_ground(const sf_cfg *c) {
    float root[3] = { 0, 0, 0 }, down[3] = { 0, -1, 0 };
    sf_zones z;
    sf_zones_of(c, root, 0, NULL, 1, &z);
    struct { float tilt_deg, speed; int flip, is_down; int want; } cases[] = {
        { 0, 250, 0, 1, SF_WHY_OK }, { 30, 250, 0, 1, SF_WHY_OK }, { 50, 250, 0, 1, SF_WHY_UP },
        { 0, 120, 0, 1, SF_WHY_NOTSTAB }, { 0, 250, 1, 1, SF_WHY_TIP }, { 0, 250, 0, 0, SF_WHY_NOTREADY },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        sc_cfg sc;
        sc_cfg_default(&sc);
        sc_speed sp;
        sc_speed_reset(&sp);
        float tl = cases[i].tilt_deg * DEG;
        float dir[3] = { sinf(tl), -cosf(tl), 0 };
        if (cases[i].flip) dir[0] = -dir[0], dir[1] = -dir[1];
        float v[3] = { down[0] * cases[i].speed, down[1] * cases[i].speed, 0 };
        if (!cases[i].flip) /* the stab moves along the blade */
            v[0] = dir[0] * cases[i].speed, v[1] = dir[1] * cases[i].speed;
        somvr_blade prev, cur;
        double t = 3;
        int got = SF_WHY_MISS;
        for (int k = 0; k < 90 && got == SF_WHY_MISS; k++, t += 1.0 / 90) {
            float travel = cases[i].speed * (float)(t - 3);
            float base[3];
            if (!cases[i].flip) /* from 120 cm up, the tip 90 cm ahead along dir */
                base[0] = z.torso[0] - 120 * dir[0] + travel * dir[0], base[1] = z.torso[1] - 120 * dir[1] + travel * dir[1];
            else /* the blade upside down above the torso, moving down: the base reaches it first */
                base[0] = z.torso[0], base[1] = z.torso[1] + 60 - travel;
            base[2] = z.torso[2];
            line_blade(base, dir, 90, v, t, &cur);
            sc_speed_push(&sp, &sc, &cur);
            got = sf_judge_ground(c, &sp, k ? &prev : NULL, &cur, &z, cases[i].is_down, NULL);
            if (got == SF_WHY_NOTREADY) break;
            prev = cur;
        }
        CHECK(got == cases[i].want, "ground case %zu (tilt %.0f, %.0f cm/s%s%s): %s, want %s", i, cases[i].tilt_deg,
              cases[i].speed, cases[i].flip ? ", upside down" : "", cases[i].is_down ? "" : ", standing",
              sf_why_name(got), sf_why_name(cases[i].want));
    }
    /* the hold: a downward blade moving along itself */
    sc_speed sp;
    sc_speed_reset(&sp);
    somvr_blade b;
    float base[3] = { 0, 120, 0 }, dn[3] = { 0, -1, 0 }, side[3] = { 1, 0, 0 }, v0[3] = { 0, 0, 0 };
    line_blade(base, dn, 90, v0, 1, &b);
    sp.axis_mps = 1.2f;
    CHECK(sf_ground_hold(c, &sp, &b), "down and moving: held");
    sp.axis_mps = 0.5f;
    CHECK(!sf_ground_hold(c, &sp, &b), "down and slow: not held");
    sp.axis_mps = 1.2f;
    line_blade(base, side, 90, v0, 1, &b);
    CHECK(!sf_ground_hold(c, &sp, &b), "level: not held");
    b.valid = 0;
    CHECK(!sf_ground_hold(c, &sp, &b) && !sf_ground_hold(c, &sp, NULL), "no blade: not held");
}

/* the dagger moving from `from` along dir at `speed` (cm/s; the velocity along the blade when stab, else across) */
static int run_dagger(const sf_cfg *c, const sf_zones *z, const float aim[3], const float dir[3], float speed, int stab,
                      float behind, int base_first) {
    sc_cfg sc;
    sc_cfg_default(&sc);
    sc_speed sp;
    sc_speed_reset(&sp);
    somvr_blade prev, cur;
    double t = 1;
    int got = SF_WHY_MISS;
    float mv[3];
    if (stab) memcpy(mv, dir, sizeof mv);
    else mv[0] = 1, mv[1] = 0, mv[2] = 0; /* a slash: the blade along dir, moving along +X */
    for (int k = 0; k < 90 && got == SF_WHY_MISS; k++, t += 1.0 / 90) {
        float s = speed * (float)(t - 1), base[3], v[3], bdir[3];
        for (int j = 0; j < 3; j++) {
            v[j] = mv[j] * speed;
            bdir[j] = base_first ? -dir[j] : dir[j];
            base[j] = aim[j] - mv[j] * 40 + mv[j] * s - (stab ? 30 * dir[j] : 15 * dir[j]);
        }
        if (base_first) /* the blade reversed: its base leads */
            for (int j = 0; j < 3; j++) base[j] += 30 * dir[j];
        line_blade(base, bdir, 30, v, t, &cur);
        sc_speed_push(&sp, &sc, &cur);
        got = sf_judge_stab(c, &sp, k ? &prev : NULL, &cur, z, behind, NULL);
        if (got == SF_WHY_FRONT) break;
        prev = cur;
    }
    return got;
}

static void test_stab(const sf_cfg *c) {
    float root[3] = { 0, 0, 0 };
    sf_zones z;
    sf_zones_of(c, root, 0, NULL, 0, &z); /* faces +Z: the player stands at -Z */
    float fwd[3] = { 0, 0, 1 }, low[3] = { z.back[0], z.back[1] - 45, z.back[2] };
    CHECK(run_dagger(c, &z, z.back, fwd, 300, 1, 170, 0) == SF_WHY_OK, "a stab into the upper back");
    CHECK(run_dagger(c, &z, z.neck, fwd, 300, 0, 170, 0) == SF_WHY_OK, "a slash across the neck");
    CHECK(run_dagger(c, &z, z.back, fwd, 300, 1, 90, 0) == SF_WHY_FRONT, "from the side");
    CHECK(run_dagger(c, &z, z.back, fwd, 100, 1, 170, 0) == SF_WHY_NOTSTAB, "a slow push");
    CHECK(run_dagger(c, &z, z.neck, fwd, 150, 0, 170, 0) == SF_WHY_NOTSTAB, "a slow slash");
    CHECK(run_dagger(c, &z, z.back, fwd, 300, 1, 170, 1) == SF_WHY_TIP, "the pommel first");
    CHECK(run_dagger(c, &z, low, fwd, 300, 1, 170, 0) == SF_WHY_MISS, "into the hip");
    somvr_blade b;
    memset(&b, 0, sizeof b);
    sc_speed sp;
    sc_speed_reset(&sp);
    CHECK(sf_judge_stab(c, &sp, NULL, &b, &z, 180, NULL) == SF_WHY_NOBLADE, "no dagger");
    for (float a = 0; a <= 180; a += 5) {
        int w = run_dagger(c, &z, z.back, fwd, 300, 1, a, 0);
        CHECK((a >= c->behind_deg) == (w == SF_WHY_OK), "behind %.0f deg: %s", a, sf_why_name(w));
    }
}

static void test_glow(const sf_cfg *c) {
    sf_glow g;
    sf_glow_reset(&g);
    CHECK(sf_glow_step(&g, c, 0, 0, 1.0) == 0 && !g.on, "off");
    CHECK(sf_glow_step(&g, c, 1, 0, 1.1) == 1 && g.on && g.since == 1.1 && g.ons == 1, "on");
    CHECK(sf_glow_step(&g, c, 1, 0, 1.2) == 0, "held");
    CHECK(sf_glow_step(&g, c, 0, 0, 1.3) == 0 && sf_glow_step(&g, c, 0, 0, 1.44) == 0 && g.on, "lingers");
    CHECK(sf_glow_step(&g, c, 1, 0, 1.45) == 0 && g.on, "a flicker keeps it on");
    CHECK(sf_glow_step(&g, c, 0, 0, 1.6) == 0 && sf_glow_step(&g, c, 0, 0, 1.71) == -1 && !g.on, "off after 0.25 s");
    CHECK(sf_glow_step(&g, c, 1, 0, 2.0) == 1 && sf_glow_step(&g, c, 1, 1, 2.01) == -1 && !g.on, "forced off at once");
    CHECK(sf_glow_step(&g, c, 1, 1, 2.02) == 0 && !g.on && g.ons == 2, "forced off stays off");
}

static void test_latch(const sf_cfg *c) {
    sf_latch l;
    sf_latch_reset(&l);
    sf_latch_in in;
    memset(&in, 0, sizeof in);
    in.valid = 1, in.key = 0x1000, in.held_key_alive = 0x1000;
    float neck[3] = { 0, 160, 0 };
    memcpy(in.neck, neck, sizeof neck), memcpy(in.held_neck, neck, sizeof neck);
    in.hand[0] = 10, in.hand[1] = 160, in.hand[2] = 0;
    in.t = 1, in.squeeze = 1;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_NONE && !l.held, "a grip held since before: no latch");
    in.t = 1.1, in.squeeze = 0;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_NONE && l.armed, "opened: armed");
    in.hand[0] = 30; /* 30 cm: too far */
    in.t = 1.2, in.squeeze = 0.9f;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_NONE && !l.held, "far: no latch");
    in.hand[0] = 10;
    in.t = 1.3;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_NONE && !l.held, "squeezed far away, moved on: holds nothing");
    in.t = 1.4, in.squeeze = 0.2f;
    sf_latch_step(&l, c, &in);
    in.t = 1.5, in.squeeze = 0.8f, in.key = 0;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_NONE && !l.held && !l.armed, "no orc offered: no latch, the squeeze spent");
    in.key = 0x1000;
    in.t = 1.55;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_NONE && !l.held, "offered now, the same squeeze: nothing");
    in.t = 1.58, in.squeeze = 0.1f;
    sf_latch_step(&l, c, &in);
    in.t = 1.6, in.squeeze = 0.8f;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_LATCH && l.held && l.key == 0x1000 && l.latches == 1, "latch");
    in.t = 1.7, in.squeeze = 0.5f; /* between off and on: still held */
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_NONE && l.held, "hysteresis");
    in.trigger = 0.9f, in.t = 1.8;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_NONE, "trigger down");
    in.t = 2.1;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_NONE, "0.3 s");
    in.t = 2.21;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_DRAIN && l.drains == 1, "drain after 0.4 s");
    in.t = 3, in.upper = 1;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_NONE && sf_latch_step(&l, c, &(sf_latch_in){ 0 }) == SF_LEV_RELEASE,
          "one action per latch, then the hand lost = released");
    /* the interrogation; both held: the trigger first */
    sf_latch_reset(&l);
    in.trigger = 0, in.upper = 0, in.squeeze = 0, in.t = 4;
    sf_latch_step(&l, c, &in);
    in.squeeze = 1, in.t = 4.1;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_LATCH, "latch 2");
    in.upper = 1, in.t = 4.2;
    sf_latch_step(&l, c, &in);
    in.t = 4.65;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_INTERROGATE && l.interrogations == 1, "interrogate");
    sf_latch_reset(&l);
    in.upper = 0, in.squeeze = 0, in.t = 5;
    sf_latch_step(&l, c, &in);
    in.squeeze = 1, in.t = 5.1;
    sf_latch_step(&l, c, &in);
    in.upper = 1, in.trigger = 1, in.t = 5.2;
    sf_latch_step(&l, c, &in);
    in.t = 5.65;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_DRAIN, "both: the drain");
    /* release and loss */
    in.squeeze = 0.3f, in.t = 6;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_RELEASE && !l.held && l.releases == 1, "released");
    in.squeeze = 0, in.upper = 0, in.trigger = 0, in.t = 6.1;
    sf_latch_step(&l, c, &in);
    in.squeeze = 1, in.t = 6.2;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_LATCH, "latch 3");
    in.held_neck[0] = 60, in.t = 6.3; /* the orc moved: its neck 50 cm from the hand */
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_LOST && l.losses == 1, "lost: too far");
    in.held_neck[0] = 0, in.squeeze = 0, in.t = 6.4;
    sf_latch_step(&l, c, &in);
    in.squeeze = 1, in.t = 6.5;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_LATCH, "latch 4");
    in.held_key_alive = 0, in.t = 6.6;
    CHECK(sf_latch_step(&l, c, &in) == SF_LEV_LOST && l.losses == 2, "lost: the orc gone");
    CHECK(!strcmp(sf_lev_name(SF_LEV_DRAIN), "drain") && !strcmp(sf_lev_name(42), "?"), "event names");
}

static void test_moment(const sf_cfg *c) {
    sf_moment m;
    sf_moment_reset(&m);
    CHECK(sf_moment_cool(&m, c, 0) && sf_moment_claim(&m, 0, 0) == 0, "idle");
    sf_moment_start(&m, c, 0xabc, SF_CLS_EXEC, 10);
    CHECK(!sf_moment_cool(&m, c, 10.5) && sf_moment_cool(&m, c, 11.0), "cooldown 1 s");
    CHECK(sf_moment_claim(&m, 12, 0) == 0xabc && m.cls == SF_CLS_EXEC, "claimed");
    CHECK(sf_moment_claim(&m, 14.0, 0) == 0, "the claim ends after 4 s");
    sf_moment_start(&m, c, 0xdef, SF_CLS_STEALTH, 20);
    CHECK(sf_moment_claim(&m, 20.1, 1) == 0 && sf_moment_claim(&m, 20.2, 0) == 0, "ended by the game");
    for (int w = 0; w < SF_WHY_N; w++)
        CHECK(sf_why_name(w)[0] && strcmp(sf_why_name(w), "?"), "why %d", w);
    CHECK(!strcmp(sf_why_name(-1), "?"), "why outside");
}

int main(void) {
    sf_cfg c;
    sf_cfg_default(&c);
    CHECK(c.exec_streak == 8 && NEAR(c.behind_deg, 110, 0) && NEAR(c.latch_cm, 25, 0) && c.steps == 4, "defaults");
    test_names();
    test_geometry(&c);
    test_exec(&c);
    test_ground(&c);
    test_stab(&c);
    test_glow(&c);
    test_latch(&c);
    test_moment(&c);
    printf("test_finish: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
