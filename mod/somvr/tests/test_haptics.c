/* test_haptics.c - build 34: the one haptics language (somvr_haptics.h, docs/re-xr-pipeline.md 153). The table (every
 * word in its class's band, names unique, patterns distinct), the rules (nothing but an alert reaches the weakest
 * impact, at any gain and any player strength; a kill the most an impact gives; the weapon's words reach the other hand
 * only with the sword in both, the body's always), the player's strength (HAPTICS 0..150 %: x s up to 100 %, order-kept
 * above - build 33 clipped every impact from 0.67 up to the same 1.0 at 150 %), the expansion into the mixer's pulses
 * (the herb's three bites apart - build 33's call ran them into one buzz; the clank's ring; the hurt in both hands) and
 * the mixer (somvr_ctl.c) at the player's strength. */
#include "../somvr_ctl.h"
#include "../somvr_haptics.h"

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

static int is_impact(int c) { return c == SOMVR_HXC_THUMP || c == SOMVR_HXC_KILL; }
/* a word's strength over its length (amplitude x s) on its own hand: its pulses and its tail (a linear ramp: half) */
static float energy(int w, float k)
{
    somvr_hx_shot s[SOMVR_HX_SHOTS];
    int n = somvr_hx_shots(w, k, 1, SOMVR_HAND_R, 0, s);
    float e = 0;
    for (int i = 0; i < n; i++)
        if (s[i].hand == SOMVR_HAND_R) e += s[i].amp * s[i].dur * (s[i].ramp ? 0.5f : 1.0f);
    return e;
}

static void test_table(void)
{
    int bad = 0;
    for (int w = 0; w < SOMVR_HX_N; w++) {
        const somvr_hx_word *d = somvr_hx_word_of(w);
        const somvr_hx_band *b = somvr_hx_band_of(d->cls);
        int cont = w == SOMVR_HX_GAME; /* the game's rumble: the mixer's continuous level, no pulse */
        int ok = d->name && d->name[0] && d->cls < SOMVR_HXC_N && d->lo <= d->hi && d->lo >= b->lo - 1e-6f &&
                 d->hi <= b->hi + 1e-6f && d->n >= b->n_lo && d->n <= b->n_hi && d->n <= 3 &&
                 (cont ? d->dur == 0 && d->cls == SOMVR_HXC_RUMBLE
                       : d->dur >= b->dur_lo - 1e-6f && d->dur <= b->dur_hi + 1e-6f) &&
                 d->gap >= 0 && (d->n == 1 || d->gap > 0) && d->decay > 0 && d->decay <= 1 && d->tail >= 0 &&
                 d->tail <= b->tail_max + 1e-6f && (d->tail > 0) == (d->tail_s > 0) &&
                 somvr_hx_total(w) <= b->total_max + 1e-6f && d->two >= 0 && d->two <= 1 && d->both >= 0 &&
                 d->both <= 1 && !(d->two > 0 && d->both > 0) && !(d->oth_tail && !(d->two > 0 || d->both > 0)) &&
                 (d->src == SOMVR_HAP_UI || d->src == SOMVR_HAP_COMBAT || (cont && d->src == SOMVR_HAP_RUMBLE));
        if (!ok) {
            bad++;
            printf("  word %s (%d): out of its class %s's band\n", d->name ? d->name : "?", w, b->name);
        }
        for (int v = 0; v < w; v++) {
            const somvr_hx_word *e = somvr_hx_word_of(v);
            if (!strcmp(d->name, e->name)) bad++, printf("  words %d and %d share the name %s\n", v, w, d->name);
            /* a distinct pattern: two words alike in class, pulses, levels, length, gap, tail and hands mean one */
            if (d->cls == e->cls && d->n == e->n && d->lo == e->lo && d->hi == e->hi && d->dur == e->dur &&
                d->gap == e->gap && d->tail == e->tail && d->tail_s == e->tail_s && d->both == e->both)
                bad++, printf("  words %s and %s are the same pulse\n", e->name, d->name);
        }
    }
    CHECK(bad == 0, "the table: %d problems", bad);
    CHECK(somvr_hx_word_of(-1) == somvr_hx_word_of(SOMVR_HX_TICK) && somvr_hx_word_of(SOMVR_HX_N) ==
              somvr_hx_word_of(SOMVR_HX_TICK) && !strcmp(somvr_hx_name(SOMVR_HX_N), "?") &&
              !strcmp(somvr_hx_name(SOMVR_HX_HIT), "hit") && !strcmp(somvr_hx_band_of(SOMVR_HXC_ALERT)->name, "alert"),
          "out of range: the tick's row, the name ?");
    /* the classes' bands are ordered: cue words under the impacts, the kill at the top */
    float cue_top = 0, imp_floor = 1;
    for (int c = 0; c < SOMVR_HXC_N; c++)
        if (c != SOMVR_HXC_ALERT) {
            const somvr_hx_band *b = somvr_hx_band_of(c);
            if (is_impact(c)) imp_floor = fminf(imp_floor, b->lo);
            else cue_top = fmaxf(cue_top, b->hi);
        }
    CHECK(cue_top < imp_floor && NEAR(imp_floor, 0.55, 1e-6) && NEAR(cue_top, 0.5, 1e-6),
          "the bands: every cue class under the impacts (%g < %g)", cue_top, imp_floor);
    printf("table: %d words in %d classes, each in its band, names unique, no two the same pulse\n", SOMVR_HX_N,
           SOMVR_HXC_N);
}

/* nothing but an alert as strong as the weakest impact - at any k, any gain, either hand, any player strength */
static void test_rules(void)
{
    float imp_lo = 1, loud = 0, kill_e = 1e9f, thump_e = 0;
    int loud_w = -1, thump_w = -1;
    for (int w = 0; w < SOMVR_HX_N; w++) {
        const somvr_hx_word *d = somvr_hx_word_of(w);
        if (is_impact(d->cls)) {
            imp_lo = fminf(imp_lo, somvr_hx_level(w, 0, 1));
            if (d->cls == SOMVR_HXC_KILL) kill_e = fminf(kill_e, energy(w, 0));
            else if (energy(w, 1) > thump_e) thump_e = energy(w, 1), thump_w = w;
            continue;
        }
        if (d->cls == SOMVR_HXC_ALERT) continue;
        for (int g = 0; g < 4; g++) { /* gains 1, 2, 10, 1000: a feature's switch never lifts a word out of its class */
            float gain = g == 0 ? 1 : g == 1 ? 2 : g == 2 ? 10 : 1000;
            somvr_hx_shot s[SOMVR_HX_SHOTS];
            int n = somvr_hx_shots(w, 1, gain, SOMVR_HAND_R, 1, s);
            for (int i = 0; i < n; i++)
                if (s[i].amp > loud) loud = s[i].amp, loud_w = w;
        }
    }
    CHECK(loud < imp_lo && loud_w >= 0, "no cue reaches an impact: the loudest %s %g, the weakest impact %g",
          somvr_hx_name(loud_w), loud, imp_lo);
    for (int si = 1; si <= 15; si++) { /* the player's strength 10 .. 150 %: the order stays */
        float s = 0.1f * (float)si;
        CHECK(somvr_hx_scale(loud, s) < somvr_hx_scale(imp_lo, s), "at %.0f %%: a cue %g reaches an impact %g", s * 100,
              somvr_hx_scale(loud, s), somvr_hx_scale(imp_lo, s));
    }
    CHECK(kill_e >= thump_e && thump_w >= 0, "a kill (%g) at least the strongest thump (%s %g)", kill_e,
          somvr_hx_name(thump_w), thump_e);
    CHECK(somvr_hx_level(SOMVR_HX_KILL, 0, 1) >= 0.9f, "a kill's own end still a kill: %g", somvr_hx_level(SOMVR_HX_KILL, 0, 1));
    /* the alerts: their own patterns - none is a cue's or an impact's, and the threat's warnings stand out */
    int alert_bad = 0;
    for (int w = 0; w < SOMVR_HX_N; w++) {
        const somvr_hx_word *a = somvr_hx_word_of(w);
        if (a->cls != SOMVR_HXC_ALERT) continue;
        for (int v = 0; v < SOMVR_HX_N; v++) {
            const somvr_hx_word *e = somvr_hx_word_of(v);
            if (e->cls == SOMVR_HXC_ALERT) continue;
            if (a->n == e->n && fabsf(a->dur - e->dur) < 0.015f && fabsf(a->hi - e->hi) < 0.1f) /* felt alike */
                alert_bad++, printf("  alert %s feels like %s\n", a->name, e->name);
        }
    }
    CHECK(alert_bad == 0, "the alerts' patterns are their own (%d alike)", alert_bad);
    CHECK(somvr_hx_level(SOMVR_HX_RED_LAST, 1, 1) > 2 * somvr_hx_level(SOMVR_HX_NO, 1, 1) &&
              somvr_hx_word_of(SOMVR_HX_RED_LAST)->n == somvr_hx_word_of(SOMVR_HX_NO)->n,
          "the unblockable's double: the no's shape at more than twice its strength");
    /* the weapon's words reach the other hand with the sword in both; the body's always; the cues never */
    CHECK(somvr_hx_word_of(SOMVR_HX_HIT)->two > 0 && somvr_hx_word_of(SOMVR_HX_FLESH)->two > 0 &&
              somvr_hx_word_of(SOMVR_HX_CLANK)->two > 0 && somvr_hx_word_of(SOMVR_HX_KILL)->two > 0 &&
              somvr_hx_word_of(SOMVR_HX_HURT)->both == 1 && somvr_hx_word_of(SOMVR_HX_MARK)->both == 1 &&
              somvr_hx_word_of(SOMVR_HX_TICK)->two == 0 && somvr_hx_word_of(SOMVR_HX_NO)->both == 0,
          "the shares: weapon, body, cue");
    float share = -1;
    int same = 1;
    for (int w = 0; w < SOMVR_HX_N; w++)
        if (somvr_hx_word_of(w)->two > 0) {
            if (share < 0) share = somvr_hx_word_of(w)->two;
            same &= somvr_hx_word_of(w)->two == share;
        }
    CHECK(same && NEAR(share, 0.6, 1e-6), "one two-hand share for every weapon word (%g)", share);
    printf("rules: the loudest cue (%s %.2f) under the weakest impact (%.2f) at 10..150 %% and any gain; a kill the "
           "most an impact gives; the alerts their own; one two-hand share %.1f\n",
           somvr_hx_name(loud_w), loud, imp_lo, share);
}

static void test_scale(void)
{
    CHECK(somvr_hx_scale(0.4f, 1) == 0.4f && somvr_hx_scale(0.4f, 0.5f) == 0.2f && somvr_hx_scale(0, 1.5f) == 0 &&
              somvr_hx_scale(0.5f, 0) == 0 && somvr_hx_scale(NAN, 1) == 0 && somvr_hx_scale(0.5f, NAN) == 0 &&
              somvr_hx_scale(-1, 1) == 0 && somvr_hx_scale(1, 1.5f) == 1 && somvr_hx_scale(1, 1) == 1 &&
              somvr_hx_scale(2, 1) == 1 && somvr_hx_scale(1, 9) == 1,
          "the strength: x s up to 100 %%, 0 / NaN / negative 0, 1 stays 1, clamps");
    int mono = 1, keep = 1;
    for (int si = 1; si <= 15; si++) {
        float s = 0.1f * (float)si, prev = 0;
        for (int ai = 1; ai <= 100; ai++) {
            float a = 0.01f * (float)ai, r = somvr_hx_scale(a, s);
            keep &= r > prev, mono &= r <= 1 && (si == 1 || r >= somvr_hx_scale(a, s - 0.1f));
            prev = r;
        }
    }
    CHECK(mono && keep, "monotone in a and in s, every two levels kept apart (mono %d, order %d)", mono, keep);
    /* build 33: 0.8 and 1.0 both 1.0 at 150 % (a x s clipped) - the hit's tiers gone; now apart */
    float h8 = somvr_hx_scale(0.8f, 1.5f), h10 = somvr_hx_scale(1.0f, 1.5f), t = somvr_hx_scale(0.2f, 1.5f);
    CHECK(h10 - h8 > 0.1f && h8 > 0.8f && t > 0.25f && t < 0.3f, "150 %%: 0.8 -> %g, 1.0 -> %g, a tick 0.2 -> %g", h8, h10, t);
    printf("scale: x s up to 100 %%, a s / (1 + (s - 1) a) above; at 150 %% 0.8 -> %.3f, 1.0 -> 1, 0.2 -> %.3f\n", h8, t);
}

static void test_shots(void)
{
    somvr_hx_shot s[SOMVR_HX_SHOTS];
    const somvr_hx_word *no = somvr_hx_word_of(SOMVR_HX_NO);
    int n = somvr_hx_shots(SOMVR_HX_NO, 1, 1, SOMVR_HAND_L, 1, s);
    CHECK(n == 2 && s[0].hand == SOMVR_HAND_L && s[1].hand == SOMVR_HAND_L && s[0].t == 0 &&
              NEAR(s[1].t, no->dur + no->gap, 1e-6) && s[0].amp == s[1].amp && !s[0].ramp && s[0].src == SOMVR_HAP_UI,
          "no: two alike on the left, %g s apart (n %d)", (double)(no->dur + no->gap), n);
    n = somvr_hx_shots(SOMVR_HX_EMPTY, 1, 1, SOMVR_HAND_R, 0, s);
    CHECK(n == 3 && s[2].t > s[1].t && s[1].t > s[0].t, "empty: three (%d)", n);
    /* the herb's bites: three, each ended before the next (build 33's call: one 0.29 s buzz), softer each */
    n = somvr_hx_shots(SOMVR_HX_EAT, 1, 1, SOMVR_HAND_R, 0, s);
    int apart = n == 3;
    for (int i = 1; i < n; i++) apart &= s[i].t >= s[i - 1].t + s[i - 1].dur + 0.05f && s[i].amp < s[i - 1].amp;
    CHECK(apart, "eat: three bites apart, each softer (%d)", n);
    sc_hap m;
    sc_hap_reset(&m);
    double t0 = 5;
    for (int i = 0; i < n; i++) sc_hap_add_at(&m, t0, t0 + s[i].t, s[i].amp, s[i].dur, s[i].ramp, s[i].src);
    int gaps = 0, on = 0, was = 1;
    for (int i = 0; i < 400; i++) {
        float l = sc_hap_level(&m, t0 + i * 0.001, 1, NULL, NULL, NULL);
        on = l > 0;
        gaps += was && !on, was = on;
    }
    CHECK(gaps == 3, "eat in the mixer: three bites with silence between (%d gaps)", gaps);
    sc_hap_reset(&m); /* build 33's three calls (in_haptic_after's amp, dur, delay order mixed up): never silent */
    sc_hap_add(&m, t0, 0.6f, 0.05f, 0, SOMVR_HAP_COMBAT);
    sc_hap_add_at(&m, t0, t0 + 0.05, 0.5f, 0.12f, 0, SOMVR_HAP_COMBAT);
    sc_hap_add_at(&m, t0, t0 + 0.05, 0.4f, 0.24f, 0, SOMVR_HAP_COMBAT);
    int old_gaps = 0;
    was = 1;
    for (int i = 0; i < 400; i++) {
        on = sc_hap_level(&m, t0 + i * 0.001, 1, NULL, NULL, NULL) > 0;
        old_gaps += was && !on, was = on;
    }
    CHECK(old_gaps == 1, "build 33's herb call: one buzz (%d)", old_gaps);
    /* the weapon's share: the hit on the right, the left only with the sword in both */
    n = somvr_hx_shots(SOMVR_HX_HIT, 1, 1, SOMVR_HAND_R, 0, s);
    CHECK(n == 1 && s[0].hand == SOMVR_HAND_R && s[0].amp == 1.0f && NEAR(s[0].dur, 0.09, 1e-6), "hit, one hand (%d)", n);
    n = somvr_hx_shots(SOMVR_HX_HIT, 0, 1, SOMVR_HAND_R, 1, s);
    CHECK(n == 2 && s[1].hand == SOMVR_HAND_L && NEAR(s[1].amp, 0.6 * 0.8, 1e-6) && NEAR(s[0].amp, 0.8, 1e-6),
          "hit (light) with two hands: right 0.8, left 0.48 (%d: %g %g)", n, n > 1 ? s[0].amp : 0, n > 1 ? s[1].amp : 0);
    n = somvr_hx_shots(SOMVR_HX_TICK, 1, 1, SOMVR_HAND_R, 1, s);
    CHECK(n == 1, "a cue in one hand, two hands or not (%d)", n);
    /* metal: the crack and its ring (a ramp from half down to 0 right after), the other hand only with two */
    n = somvr_hx_shots(SOMVR_HX_CLANK, 1, 1, SOMVR_HAND_R, 0, s);
    CHECK(n == 2 && !s[0].ramp && s[1].ramp && NEAR(s[1].t, s[0].dur, 1e-6) && NEAR(s[1].amp, 0.5 * s[0].amp, 1e-6) &&
              NEAR(s[0].amp, 0.9, 1e-6),
          "clank: the crack 0.9, its ring from 0.45 (%d)", n);
    n = somvr_hx_shots(SOMVR_HX_CLANK, 1, 1, SOMVR_HAND_R, 1, s);
    CHECK(n == 3 && s[2].hand == SOMVR_HAND_L && !s[2].ramp, "clank with two: the left's knock, no ring (%d)", n);
    /* the block: the free hand braces, one-handed too */
    n = somvr_hx_shots(SOMVR_HX_BLOCK, 0, 1, SOMVR_HAND_R, 0, s);
    CHECK(n == 3 && s[2].hand == SOMVR_HAND_L && NEAR(s[2].amp, 0.45 * 0.75, 1e-6) && NEAR(s[0].amp, 0.75, 1e-6),
          "block: the crack 0.75, the ring, the free hand's brace 0.34 (%d)", n);
    /* the body's: the hurt in both hands alike, tails too */
    n = somvr_hx_shots(SOMVR_HX_HURT, 1, 1, SOMVR_HAND_R, 0, s);
    int l = 0, r = 0;
    for (int i = 0; i < n; i++) l += s[i].hand == SOMVR_HAND_L, r += s[i].hand == SOMVR_HAND_R;
    CHECK(n == 4 && l == 2 && r == 2 && s[0].amp == s[2].amp, "hurt: both hands, each a thump and a tail (%d: L %d R %d)", n,
          l, r);
    /* k, gain: graded within the word, NaN the low end, a gain capped at the class's top, 0 nothing */
    CHECK(NEAR(somvr_hx_level(SOMVR_HX_THUMP, 0.5f, 1), 0.775, 1e-6) && somvr_hx_level(SOMVR_HX_THUMP, NAN, 1) == 0.55f &&
              somvr_hx_level(SOMVR_HX_THUMP, 7, 1) == 1.0f && somvr_hx_level(SOMVR_HX_THUMP, -3, 1) == 0.55f &&
              somvr_hx_level(SOMVR_HX_TICK, 1, 10) == somvr_hx_band_of(SOMVR_HXC_TICK)->hi &&
              somvr_hx_level(SOMVR_HX_TICK, 1, 0) == 0 && somvr_hx_level(SOMVR_HX_TICK, 1, NAN) == 0 &&
              NEAR(somvr_hx_level(SOMVR_HX_TICK, 1, 0.5f), 0.1, 1e-6),
          "k and the gain");
    CHECK(somvr_hx_shots(SOMVR_HX_TICK, 1, 0, SOMVR_HAND_R, 0, s) == 0 && somvr_hx_shots(SOMVR_HX_GAME, 1, 1, 0, 0, s) == 0 &&
              somvr_hx_shots(-1, 1, 1, 0, 0, s) == 0 && somvr_hx_shots(SOMVR_HX_N, 1, 1, 0, 0, s) == 0,
          "nothing: a gain of 0, the game's rumble, out of range");
    CHECK(NEAR(somvr_hx_level(SOMVR_HX_GAME, 1, 0.8f), 0.4, 1e-6) && somvr_hx_level(SOMVR_HX_GAME, 1, 3) == 0.5f,
          "the game's rumble: half its level, at most the rumble's top");
    int most = 0;
    for (int w = 0; w < SOMVR_HX_N; w++) {
        int k = somvr_hx_shots(w, 1, 1, SOMVR_HAND_R, 1, s);
        most = k > most ? k : most;
    }
    CHECK(most <= SOMVR_HX_SHOTS && most <= 2 * 4, "the most pulses of a word %d (the mixer: 4 a hand)", most);
    printf("shots: no 2, empty 3, eat 3 apart (build 33: one buzz), the hit's left share with two, the clank's ring, the "
           "block's brace, the hurt in both, k and the gain\n");
}

/* the mixer at the player's strength: 150 % keeps a light hit under a heavy one (build 33: both 1.0) */
static void test_mixer(void)
{
    sc_hap m;
    somvr_haptic_req r;
    sc_hap_reset(&m);
    sc_hap_add(&m, 10, somvr_hx_level(SOMVR_HX_HIT, 0, 1), 0.09f, 0, SOMVR_HAP_COMBAT);
    CHECK(sc_hap_tick(&m, 10, 1.5f, 1.0f / 90, &r) && NEAR(r.amplitude, 1.2 / 1.4, 1e-5), "a light hit at 150 %%: %g",
          r.amplitude);
    sc_hap_reset(&m);
    sc_hap_add(&m, 10, somvr_hx_level(SOMVR_HX_HIT, 1, 1), 0.09f, 0, SOMVR_HAP_COMBAT);
    CHECK(sc_hap_tick(&m, 10, 1.5f, 1.0f / 90, &r) && r.amplitude == 1.0f, "a heavy one: %g", r.amplitude);
    sc_hap_reset(&m);
    sc_hap_add(&m, 10, somvr_hx_level(SOMVR_HX_TICK, 1, 1), 0.014f, 0, SOMVR_HAP_UI);
    CHECK(sc_hap_tick(&m, 10, 0.5f, 1.0f / 90, &r) && NEAR(r.amplitude, 0.1, 1e-6), "a tick at 50 %%: %g", r.amplitude);
    printf("mixer: the player's strength through somvr_hx_scale (150 %%: light hit 0.857, heavy 1.0; 50 %%: a tick 0.10)\n");
}

int main(void)
{
    test_table();
    test_rules();
    test_scale();
    test_shots();
    test_mixer();
    printf("test_haptics: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
