/* test_fallback.c - unit tests of somvr_fallback.c (SOMVR_FALLBACK=auto, the automatic third-person fallback): the
 * detector (every condition from its read, a read without its `have` bit ignored, the FSM/life/photo values that mean
 * something and the ones that do not, the tag classes), the tag list (classes, prefixes, bad items, none) and condition
 * names, the switch (enter and exit debounce, a flicker shorter than the debounce, the minimum hold and the one-toggle-
 * per-second floor, pause holds, disabled conditions, the paired action that counts only once long or moving, forced),
 * the transition (fade out, the swap on the first black frame, at least two black frames, fade in, the level per
 * frame, turn-arounds mid-fade from the current level, a swap again while black, a plain cut, out 0 = a cut to black,
 * the record's fade level) and a whole sequence through all three stages at 90 Hz. */
#include "../somvr_fallback.h"

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
#define B(c) SOMVR_FB_BIT(SOMVR_FB_##c)

/* a frame of the world with nothing special: alive, FSM supported, photo mode off, every read made */
static somvr_fb_in calm(void) {
    somvr_fb_in in;
    memset(&in, 0, sizeof in);
    in.have = 0x1fff;
    in.life = 2, in.fsm = 6, in.photo = SOMVR_FB_PHOTO_OFF;
    return in;
}

static void test_detect(void) {
    somvr_fb_in in = calm();
    CHECK(somvr_fb_detect(&in) == 0, "a calm frame: %#x", somvr_fb_detect(&in));
    struct { const char *what; uint32_t want; void (*set)(somvr_fb_in *); } cases[] = {
        { "cine", B(CINE), NULL }, { "scripted", B(SCRIPTED), NULL }, { "pres", B(PRES), NULL },
        { "sync", B(SYNC), NULL }, { "struggle", B(STRUGGLE), NULL }, { "mount", B(MOUNT), NULL },
        { "attach", B(ATTACH), NULL }, { "climb", B(CLIMB), NULL }, { "death", B(DEATH), NULL },
        { "photo", B(PHOTO), NULL }, { "spect", B(SPECT), NULL },
    };
    for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        somvr_fb_in a = calm(), b;
        uint32_t need = 0;
        switch (k) {
        case 0: a.cine = 0x1234, need = SOMVR_FBR_CINE; break;
        case 1: a.scripted = 1, need = SOMVR_FBR_CAM; break;
        case 2: a.pres = 0x55, need = SOMVR_FBR_PRES; break;
        case 3: a.sync = 0x77, need = SOMVR_FBR_SYNC; break;
        case 4: a.struggle[1] = 0x99, need = SOMVR_FBR_STRUGGLE; break;
        case 5: a.mount = 0x1d8, need = SOMVR_FBR_MOUNT; break;
        case 6: a.attach = 1, need = SOMVR_FBR_ATTACH; break;
        case 7: a.fsm = SOMVR_FB_FSM_TRANSITION, need = SOMVR_FBR_FSM; break;
        case 8: a.life = SOMVR_FB_LIFE_DYING, need = SOMVR_FBR_LIFE; break;
        case 9: a.photo = 0, need = SOMVR_FBR_PHOTO; break;
        case 10: a.spect = 4, need = SOMVR_FBR_SPECT; break;
        }
        CHECK(somvr_fb_detect(&a) == cases[k].want, "%s: %#x, want %#x", cases[k].what, somvr_fb_detect(&a), cases[k].want);
        b = a, b.have &= ~need;
        CHECK(somvr_fb_detect(&b) == 0, "%s without its read: %#x", cases[k].what, somvr_fb_detect(&b));
    }
    in = calm(), in.struggle[0] = 1;
    CHECK(somvr_fb_detect(&in) == B(STRUGGLE), "the state struggle");
    /* values that mean nothing for the fallback */
    int fsm_calm[] = { 1, 3, 6, 7, 8, 10, 11 };
    for (unsigned i = 0; i < sizeof fsm_calm / sizeof fsm_calm[0]; i++) {
        in = calm(), in.fsm = fsm_calm[i];
        CHECK(somvr_fb_detect(&in) == 0, "fsm %d (falling, landing, combat stay first person): %#x", fsm_calm[i],
              somvr_fb_detect(&in));
    }
    in = calm(), in.life = 1;
    CHECK(somvr_fb_detect(&in) == 0, "life 1 (not in the world) is not death");
    /* tags */
    in = calm(), in.tag_classes = SOMVR_FB_TAGC_DEATH;
    CHECK(somvr_fb_detect(&in) == B(DEATH), "a death tag with life 2 (P8): %#x", somvr_fb_detect(&in));
    in.tag_classes = SOMVR_FB_TAGC_CLIMB;
    CHECK(somvr_fb_detect(&in) == B(CLIMB), "a climb tag");
    in.tag_classes = SOMVR_FB_TAGC_OTHER;
    CHECK(somvr_fb_detect(&in) == B(TAG), "an other tag");
    in.tag_classes = 7, in.have &= ~SOMVR_FBR_TAGS;
    CHECK(somvr_fb_detect(&in) == 0, "tags without their read");
    in = calm();
    in.cine = 1, in.sync = 2, in.mount = 3, in.life = 3;
    CHECK(somvr_fb_detect(&in) == (B(CINE) | B(SYNC) | B(MOUNT) | B(DEATH)), "several at once: %#x", somvr_fb_detect(&in));
    in = calm(), in.have = 0, in.photo = 0, in.cine = 1;
    CHECK(somvr_fb_detect(&in) == 0, "nothing read: nothing detected");
    printf("detect: 11 conditions from their reads, none without them, calm FSM states, life 1, the tag classes\n");
}

static void test_tags(void) {
    somvr_fb_tags t;
    CHECK(somvr_fb_tags_parse(SOMVR_FB_TAGS_DEFAULT, &t) == 0 && t.n == 2, "the default: n %d", t.n);
    CHECK(somvr_fb_tag_class(&t, "PreDeath") == SOMVR_FB_TAGC_DEATH && somvr_fb_tag_class(&t, "PreDeath_Kneeling") ==
          SOMVR_FB_TAGC_DEATH, "PreDeath* matches P8's two death tags");
    CHECK(somvr_fb_tag_class(&t, "FinalBlow") == SOMVR_FB_TAGC_OTHER && somvr_fb_tag_class(&t, "FinalBlowX") == 0,
          "FinalBlow exact");
    const char *calm_tags[] = { "Gnd", "Idl", "Allow_Showdown", "InCombat", "CombatCam", "Tight", "CloseToWalls", "Stl",
                                "Teleport", "NamedTarget", "Pre", "" };
    for (unsigned i = 0; i < sizeof calm_tags / sizeof calm_tags[0]; i++)
        CHECK(somvr_fb_tag_class(&t, calm_tags[i]) == 0, "P7/P8's calm tag '%s' is in a class", calm_tags[i]);
    CHECK(somvr_fb_tag_class(&t, NULL) == 0, "NULL name");
    CHECK(somvr_fb_tags_parse(" climb=Climb* , climb=Ledge_Hang,tag=Mount , other=X,death=Dead ", &t) == 0 && t.n == 5,
          "spaces and classes: n %d", t.n);
    CHECK(somvr_fb_tag_class(&t, "ClimbUp") == SOMVR_FB_TAGC_CLIMB && somvr_fb_tag_class(&t, "Ledge_Hang") ==
          SOMVR_FB_TAGC_CLIMB && somvr_fb_tag_class(&t, "Ledge_Hang2") == 0 && somvr_fb_tag_class(&t, "Mount") ==
          SOMVR_FB_TAGC_OTHER && somvr_fb_tag_class(&t, "X") == SOMVR_FB_TAGC_OTHER &&
          somvr_fb_tag_class(&t, "Dead") == SOMVR_FB_TAGC_DEATH, "classes");
    CHECK(somvr_fb_tags_parse("death=A,both=B,=C,*,climb=,D", &t) == -1 && t.n == 2 && !strcmp(t.name[0], "A") &&
          !strcmp(t.name[1], "D"), "bad items skipped, the good ones kept: n %d", t.n);
    CHECK(somvr_fb_tags_parse("none", &t) == 0 && t.n == 0 && somvr_fb_tags_parse("", &t) == 0 && t.n == 0 &&
          somvr_fb_tags_parse(NULL, &t) == 0 && t.n == 0, "none / empty");
    char lng[200];
    memset(lng, 'a', sizeof lng - 1), lng[sizeof lng - 1] = 0;
    CHECK(somvr_fb_tags_parse(lng, &t) == -1 && t.n == 0, "a name too long");
    char many[400] = "";
    for (int i = 0; i < 20; i++) snprintf(many + strlen(many), sizeof many - strlen(many), "T%d,", i);
    CHECK(somvr_fb_tags_parse(many, &t) == -1 && t.n == SOMVR_FB_MAXTAGS, "more than %d: %d kept", SOMVR_FB_MAXTAGS, t.n);
    /* both classes on one name */
    CHECK(somvr_fb_tags_parse("death=Mount,tag=Mount", &t) == 0 &&
          somvr_fb_tag_class(&t, "Mount") == (SOMVR_FB_TAGC_DEATH | SOMVR_FB_TAGC_OTHER), "two classes");
    uint32_t b;
    CHECK(somvr_fb_cond_parse("all", &b) == 0 && b == SOMVR_FB_ALL && somvr_fb_cond_parse(NULL, &b) == 0 &&
          b == SOMVR_FB_ALL, "all");
    CHECK(somvr_fb_cond_parse("none", &b) == 0 && b == 0, "none");
    CHECK(somvr_fb_cond_parse("cine, sync,death", &b) == 0 && b == (B(CINE) | B(SYNC) | B(DEATH)), "a list: %#x", b);
    CHECK(somvr_fb_cond_parse("cine,bogus,mount", &b) == -1 && b == (B(CINE) | B(MOUNT)), "an unknown name: %#x", b);
    for (int i = 0; i < SOMVR_FB_NCOND; i++)
        CHECK(somvr_fb_cond_parse(somvr_fb_cond_names[i], &b) == 0 && b == SOMVR_FB_BIT(i), "name %s", somvr_fb_cond_names[i]);
    printf("tags: the default (PreDeath*, FinalBlow), P7/P8's calm tags in no class, classes, prefixes, bad items, limits; "
           "condition names\n");
}

/* run the switch from t0 to t1 at 90 Hz with raw (root fixed or moving); returns the time of the first change (-1) */
static double run_sw(somvr_fb_sw *s, const somvr_fb_sw_cfg *c, uint32_t raw, double t0, double t1, const float *root,
                     float root_v, int hold, int *changes) {
    double first = -1;
    float r[3] = { 0, 0, 0 };
    for (int i = 0;; i++) {
        double t = t0 + i / 90.0;
        if (t > t1 + 1e-9) break;
        if (root) r[0] = root[0] + root_v * (float)(t - t0), r[1] = root[1], r[2] = root[2];
        if (somvr_fb_sw_step(s, c, raw, root ? r : NULL, hold, t)) {
            if (first < 0) first = t;
            if (changes) (*changes)++;
        }
    }
    return first;
}

static void test_switch(void) {
    somvr_fb_sw_cfg c;
    somvr_fb_sw s;
    somvr_fb_sw_cfg_default(&c);
    CHECK(c.enabled == SOMVR_FB_ALL && NEAR(c.enter_s, 0.1, 1e-6) && NEAR(c.exit_s, 0.4, 1e-6) && NEAR(c.hold_s, 1.5, 1e-6) &&
          NEAR(c.sync_s, 0.6, 1e-6) && NEAR(c.sync_cm, 75, 1e-6), "defaults");
    somvr_fb_sw_init(&s);
    int ch = 0;
    CHECK(run_sw(&s, &c, 0, 0, 3, NULL, 0, 0, &ch) < 0 && ch == 0 && s.want == 0, "calm: first person");
    /* enter after the debounce (the start counts as held long ago) */
    double tc = run_sw(&s, &c, B(CINE), 3, 4, NULL, 0, 0, &ch);
    CHECK(s.want == 1 && ch == 1 && NEAR(tc - 3, 0.1, 1.5 / 90), "cine: third after %.3f s (want 0.1)", tc - 3);
    CHECK(s.cond == B(CINE) && s.raw == B(CINE), "cond %#x raw %#x", s.cond, s.raw);
    /* exit after its debounce, but not before the minimum hold since the enter */
    ch = 0;
    double te = run_sw(&s, &c, 0, 4, 7, NULL, 0, 0, &ch);
    CHECK(s.want == 0 && ch == 1 && NEAR(te - tc, 1.5, 1.5 / 90), "back after the hold: %.3f s after the enter", te - tc);
    /* a long one: the exit debounce counts from its end */
    somvr_fb_sw_init(&s);
    run_sw(&s, &c, B(MOUNT), 0, 5, NULL, 0, 0, NULL);
    te = run_sw(&s, &c, 0, 5 + 1 / 90.0, 7, NULL, 0, 0, NULL);
    CHECK(NEAR(te - 5, 0.4 + 1 / 90.0, 1.5 / 90), "mount: back %.3f s after its end (want 0.4)", te - 5);
    /* a flicker shorter than the debounce never switches, either way */
    somvr_fb_sw_init(&s);
    ch = 0;
    for (int k = 0; k < 40; k++) { /* 50 ms on, 150 ms off */
        run_sw(&s, &c, B(SCRIPTED), k * 0.2, k * 0.2 + 0.05, NULL, 0, 0, &ch);
        run_sw(&s, &c, 0, k * 0.2 + 0.06, k * 0.2 + 0.19, NULL, 0, 0, &ch);
    }
    CHECK(ch == 0 && s.want == 0, "a 50 ms flicker: %d changes", ch);
    run_sw(&s, &c, B(SCRIPTED), 10, 11.6, NULL, 0, 0, NULL);
    ch = 0;
    for (int k = 0; k < 20; k++) { /* in third person: 300 ms gaps never bring it back */
        run_sw(&s, &c, 0, 11.6 + k * 0.4, 11.6 + k * 0.4 + 0.3, NULL, 0, 0, &ch);
        run_sw(&s, &c, B(SCRIPTED), 11.6 + k * 0.4 + 0.31, 11.6 + k * 0.4 + 0.39, NULL, 0, 0, &ch);
    }
    CHECK(ch == 0 && s.want == 1, "300 ms gaps in third person: %d changes", ch);
    /* never more than one toggle per second, even with hold_s and the debounces set to 0 */
    somvr_fb_sw_cfg z = c;
    z.enter_s = z.exit_s = z.hold_s = 0;
    somvr_fb_sw_init(&s);
    ch = 0;
    double last = -10, mingap = 99;
    for (int i = 0; i < 900; i++) { /* the wish toggles every frame for 10 s */
        double t = i / 90.0;
        if (somvr_fb_sw_step(&s, &z, (i & 1) ? B(CINE) : 0, NULL, 0, t)) {
            ch++;
            if (t - last < mingap) mingap = t - last;
            last = t;
        }
    }
    CHECK(ch >= 5 && ch <= 11 && mingap >= 1.0 - 1e-9, "a wish toggling every frame: %d changes, min gap %.3f s", ch, mingap);
    /* pause holds the switch; the debounce starts again afterwards */
    somvr_fb_sw_init(&s);
    run_sw(&s, &c, B(CINE), 0, 2, NULL, 0, 1, &ch);
    CHECK(s.want == 0, "paused: no change");
    tc = run_sw(&s, &c, B(CINE), 2, 3, NULL, 0, 0, NULL);
    CHECK(s.want == 1 && NEAR(tc - 2, 0.1, 1.5 / 90), "after the pause: %.3f s", tc - 2);
    /* disabled conditions do not count; forced always does */
    c.enabled = B(CINE);
    somvr_fb_sw_init(&s);
    CHECK(run_sw(&s, &c, B(MOUNT) | B(DEATH), 0, 3, NULL, 0, 0, NULL) < 0 && s.want == 0 && s.cond == 0 &&
          s.raw == (B(MOUNT) | B(DEATH)), "disabled: cond %#x raw %#x", s.cond, s.raw);
    CHECK(run_sw(&s, &c, B(FORCED), 3, 4, NULL, 0, 0, NULL) > 0 && s.want == 1, "forced counts with any mask");
    somvr_fb_sw_cfg_default(&c);
    /* a paired action counts once it lasted sync_s ... */
    float root[3] = { 100, 0, 200 };
    somvr_fb_sw_init(&s);
    tc = run_sw(&s, &c, B(SYNC), 0, 2, root, 0, 0, NULL);
    CHECK(s.want == 1 && NEAR(tc, 0.6 + 0.1, 2.5 / 90), "a still sync: third at %.3f s (0.6 + the debounce)", tc);
    /* ... a short one (a counter) never does */
    somvr_fb_sw_init(&s);
    ch = 0;
    for (int k = 0; k < 10; k++) {
        run_sw(&s, &c, B(SYNC), k * 1.0, k * 1.0 + 0.5, root, 0, 0, &ch);
        run_sw(&s, &c, 0, k * 1.0 + 0.51, k * 1.0 + 0.99, root, 0, 0, &ch);
    }
    CHECK(ch == 0 && s.want == 0, "0.5 s counters in place: %d changes", ch);
    /* ... or once it carried Talion sync_cm (a lunge: 3 m/s -> 75 cm at 0.25 s) */
    somvr_fb_sw_init(&s);
    tc = run_sw(&s, &c, B(SYNC), 0, 1, root, 300, 0, NULL);
    CHECK(s.want == 1 && NEAR(tc, 0.25 + 0.1, 2.0 / 90), "a lunge: third at %.3f s", tc);
    CHECK(s.sync_on && s.sync_ok && s.sync_cm > 75, "its travel %.1f cm", s.sync_cm);
    /* the travel needs the root: without it only the time counts */
    somvr_fb_sw_init(&s);
    tc = run_sw(&s, &c, B(SYNC), 0, 1, NULL, 0, 0, NULL);
    CHECK(NEAR(tc, 0.7, 2.5 / 90), "no root: %.3f s", tc);
    /* sync with 0, 0: at once */
    c.sync_s = 0, c.sync_cm = 0;
    somvr_fb_sw_init(&s);
    tc = run_sw(&s, &c, B(SYNC), 0, 1, root, 0, 0, NULL);
    CHECK(NEAR(tc, 0.1, 1.5 / 90), "sync 0,0: %.3f s", tc);
    /* only the distance: a still one never counts */
    c.sync_s = 0, c.sync_cm = 75;
    somvr_fb_sw_init(&s);
    CHECK(run_sw(&s, &c, B(SYNC), 0, 5, root, 0, 0, NULL) < 0 && s.sync_on && !s.sync_ok, "distance only, still");
    /* a sync next to another condition: the other one counts at once */
    somvr_fb_sw_cfg_default(&c);
    somvr_fb_sw_init(&s);
    tc = run_sw(&s, &c, B(SYNC) | B(STRUGGLE), 0, 0.5, root, 0, 0, NULL);
    CHECK(NEAR(tc, 0.1, 1.5 / 90) && s.cond == B(STRUGGLE), "a struggle inside a sync: %.3f s cond %#x", tc, s.cond);
    printf("switch: debounce 0.1/0.4 s, hold 1.5 s, flickers ignored, at most one toggle per second, pause, masks, "
           "forced, paired actions by time (0.6 s) or travel (75 cm), short counters stay first person\n");
}

static void tr_run(somvr_fb_tr *tr, const somvr_fb_fade_cfg *c, int want, double t0, int frames, float *alpha,
                   int *shown, int *swapped) {
    for (int i = 0; i < frames; i++) {
        somvr_fb_tr_out o;
        somvr_fb_tr_step(tr, c, want, t0 + i / 90.0, &o);
        alpha[i] = o.alpha, shown[i] = o.shown, swapped[i] = o.swapped;
    }
}

static void test_transition(void) {
    somvr_fb_fade_cfg c;
    somvr_fb_fade_cfg_default(&c);
    CHECK(NEAR(c.out_s, 0.15, 1e-6) && NEAR(c.black_s, 0.06, 1e-6) && NEAR(c.in_s, 0.2, 1e-6), "defaults");
    somvr_fb_tr tr;
    somvr_fb_tr_init(&tr, 0);
    float a[200];
    int sh[200], sw[200];
    tr_run(&tr, &c, 0, 0, 20, a, sh, sw);
    int idle_ok = 1;
    for (int i = 0; i < 20; i++) idle_ok &= a[i] == 0 && sh[i] == 0 && !sw[i];
    CHECK(idle_ok && tr.phase == SOMVR_FBT_IDLE, "idle: clear, first person");
    /* to third person: out over 0.15 s (rising), the swap on the first black frame, >= 2 black frames, in over 0.2 s */
    tr_run(&tr, &c, 1, 1.0, 100, a, sh, sw);
    int swap_at = -1, nswap = 0, rising = 1, black = 0, last_black = -1, clear_at = -1;
    for (int i = 0; i < 100; i++) {
        if (sw[i]) nswap++, swap_at = swap_at < 0 ? i : swap_at;
        if (swap_at < 0) rising &= (i == 0 || a[i] >= a[i - 1]) && a[i] < 1 && sh[i] == 0;
        if (a[i] == 1.0f) black++, last_black = i;
        if (clear_at < 0 && swap_at >= 0 && a[i] == 0) clear_at = i;
    }
    CHECK(a[0] == 0 && rising, "fade out: rising from 0, first person until the swap");
    CHECK(nswap == 1 && NEAR(swap_at / 90.0, 0.15, 1.01 / 90) && a[swap_at] == 1.0f && sh[swap_at] == 1,
          "one swap at frame %d (%.3f s), black, third person", swap_at, swap_at / 90.0);
    CHECK(black >= 3 && NEAR((last_black - swap_at) / 90.0, 0.06, 1.01 / 90), "black frames %d, the last %.3f s after the swap",
          black, (last_black - swap_at) / 90.0);
    int falling = 1;
    for (int i = last_black + 1; i < clear_at; i++) falling &= a[i] < a[i - 1] && a[i] > 0 && sh[i] == 1;
    CHECK(falling && NEAR((clear_at - last_black) / 90.0, 0.2, 1.01 / 90) && tr.phase == SOMVR_FBT_IDLE && tr.shown == 1,
          "fade in: falling for %.3f s, then clear and idle", (clear_at - last_black) / 90.0);
    for (int i = clear_at; i < 100; i++) CHECK(a[i] == 0 && sh[i] == 1, "frame %d after the fade: %g", i, a[i]);
    /* the level at a known time: half way out = 0.5 */
    somvr_fb_tr_out o;
    somvr_fb_tr_init(&tr, 1);
    somvr_fb_tr_step(&tr, &c, 0, 10.0, &o);
    somvr_fb_tr_step(&tr, &c, 0, 10.075, &o);
    CHECK(NEAR(o.alpha, 0.5, 1e-4) && o.shown == 1 && tr.phase == SOMVR_FBT_OUT, "half way out: %g", o.alpha);
    /* turn-around before black: back in from 0.5, never black, never swapped */
    somvr_fb_tr_step(&tr, &c, 1, 10.085, &o);
    CHECK(o.alpha <= 0.5f + 1e-4 && o.alpha > 0.4f && tr.phase == SOMVR_FBT_IN && !o.swapped && o.shown == 1,
          "turned around: %g phase %d", o.alpha, tr.phase);
    int ok = 1;
    for (int i = 1; i < 40; i++) {
        somvr_fb_tr_step(&tr, &c, 1, 10.085 + i / 90.0, &o);
        ok &= o.alpha < 1 && !o.swapped && o.shown == 1;
    }
    CHECK(ok && o.alpha == 0 && tr.phase == SOMVR_FBT_IDLE && tr.swaps == 0, "back in without a swap");
    /* turn-around while coming back in: out again from the current level, then a swap */
    somvr_fb_tr_init(&tr, 0);
    double t = 20;
    for (; tr.phase != SOMVR_FBT_IN; t += 1 / 90.0) somvr_fb_tr_step(&tr, &c, 1, t, &o);
    somvr_fb_tr_step(&tr, &c, 1, t + 0.1, &o); /* half way in */
    float lv = o.alpha;
    somvr_fb_tr_step(&tr, &c, 0, t + 0.1 + 1 / 90.0, &o);
    CHECK(tr.phase == SOMVR_FBT_OUT && o.alpha >= lv - 1e-4 && o.alpha < 1 && o.shown == 1, "out again from %g: %g", lv,
          o.alpha);
    uint64_t sw0 = tr.swaps;
    for (int i = 2; i < 40; i++) somvr_fb_tr_step(&tr, &c, 0, t + 0.1 + i / 90.0, &o);
    CHECK(tr.swaps == sw0 + 1 && tr.shown == 0, "and swapped back: swaps %llu shown %d", (unsigned long long)tr.swaps,
          tr.shown);
    /* a change while black: swapped again at once, still black, the black starts over */
    somvr_fb_tr_init(&tr, 0);
    for (t = 30; tr.phase != SOMVR_FBT_BLACK; t += 1 / 90.0) somvr_fb_tr_step(&tr, &c, 1, t, &o);
    somvr_fb_tr_step(&tr, &c, 0, t, &o);
    CHECK(o.swapped && o.shown == 0 && o.alpha == 1 && tr.phase == SOMVR_FBT_BLACK && tr.swaps == 2, "swap again while black");
    somvr_fb_tr_step(&tr, &c, 0, t + 1 / 90.0, &o);
    CHECK(o.alpha == 1 && tr.phase == SOMVR_FBT_BLACK, "the black starts over");
    /* at least 2 black frames even with black 0 and huge frame gaps */
    somvr_fb_fade_cfg c0 = { 0.15f, 0, 0.2f };
    somvr_fb_tr_init(&tr, 0);
    somvr_fb_tr_step(&tr, &c0, 1, 40, &o);
    somvr_fb_tr_step(&tr, &c0, 1, 45, &o); /* a 5 s hitch: out completes, the swap */
    CHECK(o.swapped && o.alpha == 1, "a hitch completes the fade out");
    somvr_fb_tr_step(&tr, &c0, 1, 50, &o);
    CHECK(o.alpha == 1 && tr.phase == SOMVR_FBT_BLACK, "the second frame after the swap is black too");
    somvr_fb_tr_step(&tr, &c0, 1, 55, &o);
    CHECK(tr.phase == SOMVR_FBT_IN && o.alpha == 1, "then in, from black");
    somvr_fb_tr_step(&tr, &c0, 1, 60, &o);
    CHECK(o.alpha == 0 && tr.phase == SOMVR_FBT_IDLE, "and clear");
    /* a plain cut (all 0): the swap in the frame it is asked for, never black */
    somvr_fb_fade_cfg cut = { 0, 0, 0 };
    somvr_fb_tr_init(&tr, 0);
    somvr_fb_tr_step(&tr, &cut, 1, 70, &o);
    CHECK(o.swapped && o.shown == 1 && o.alpha == 0 && tr.phase == SOMVR_FBT_IDLE, "a plain cut");
    somvr_fb_tr_step(&tr, &cut, 1, 70.01, &o);
    CHECK(!o.swapped && o.shown == 1, "once");
    /* out 0: a cut to black, then the fade in */
    somvr_fb_fade_cfg cb = { 0, 0.06f, 0.2f };
    somvr_fb_tr_init(&tr, 0);
    somvr_fb_tr_step(&tr, &cb, 1, 80, &o);
    CHECK(o.swapped && o.alpha == 1 && o.shown == 1, "out 0: black and swapped in the first frame");
    /* NaN time never gives a NaN level */
    somvr_fb_tr_init(&tr, 0);
    somvr_fb_tr_step(&tr, &c, 1, 90, &o);
    somvr_fb_tr_step(&tr, &c, 1, NAN, &o);
    CHECK(o.alpha >= 0 && o.alpha <= 1, "NaN time: %g", o.alpha);
    /* the record's level */
    CHECK(somvr_fb_fade_level(0) == 0 && somvr_fb_fade_level(-1) == 0 && somvr_fb_fade_level(NAN) == 0 &&
          somvr_fb_fade_level(1) == 255 && somvr_fb_fade_level(2) == 255 && somvr_fb_fade_level(0.5f) == 128 &&
          somvr_fb_fade_level(0.0001f) == 1 && somvr_fb_fade_level(0.999f) == 255, "fade levels");
    printf("transition: out 0.15 s rising, the swap on the first black frame, >= 3 frames black (0.06 s), in 0.2 s; "
           "turn-arounds keep the level, a swap again while black, hitches, a plain cut, a cut to black, levels\n");
}

/* all three stages: a calm walk, a 2.5 s execution that carries Talion 2 m, a 0.4 s counter, a long cinematic, death */
static void test_sequence(void) {
    somvr_fb_sw_cfg sc;
    somvr_fb_fade_cfg fc;
    somvr_fb_sw s;
    somvr_fb_tr tr;
    somvr_fb_tags tags;
    somvr_fb_sw_cfg_default(&sc);
    somvr_fb_fade_cfg_default(&fc);
    somvr_fb_sw_init(&s);
    somvr_fb_tr_init(&tr, 0);
    somvr_fb_tags_parse(SOMVR_FB_TAGS_DEFAULT, &tags);
    int swaps = 0, third_frames = 0, black_frames = 0, prev_shown = 0, bad_swap = 0;
    double third_at[8], first_at[8];
    int nt = 0, nf = 0;
    for (int i = 0; i < 90 * 30; i++) {
        double t = i / 90.0;
        somvr_fb_in in = calm();
        float root[3] = { 0, 0, 0 };
        if (t >= 2 && t < 4.5) in.sync = 0xabc, root[2] = (float)(t - 2) * 80; /* the execution: 2 m in 2.5 s */
        if (t >= 8 && t < 8.4) in.sync = 0xdef;                                /* a counter in place */
        if (t >= 12 && t < 18) in.cine = 0x111;                                /* a cinematic */
        if (t >= 22) in.tag_classes = somvr_fb_tag_class(&tags, t < 25 ? "PreDeath_Kneeling" : "PreDeath");
        if (t >= 28) in.tag_classes = 0;                                       /* the respawn */
        uint32_t raw = somvr_fb_detect(&in);
        somvr_fb_sw_step(&s, &sc, raw, root, 0, t);
        somvr_fb_tr_out o;
        somvr_fb_tr_step(&tr, &fc, s.want, t, &o);
        if (o.swapped) {
            swaps++;
            bad_swap |= o.alpha != 1.0f;
            if (o.shown && nt < 8) third_at[nt++] = t;
            if (!o.shown && nf < 8) first_at[nf++] = t;
        }
        third_frames += o.shown;
        black_frames += o.alpha == 1.0f;
        bad_swap |= o.shown != prev_shown && !o.swapped;
        prev_shown = o.shown;
    }
    CHECK(!bad_swap, "every swap on a black frame, the presentation changes only with a swap");
    CHECK(swaps == 6 && nt == 3 && nf == 3, "3 times third person and back: %d swaps (%d/%d)", swaps, nt, nf);
    if (nt == 3 && nf == 3) {
        CHECK(third_at[0] > 2.8 && third_at[0] < 2.95, "the execution: third at %.3f s (0.6 s + 0.1 + the fade out; 75 cm only at "
              "2.94 s)", third_at[0]);
        CHECK(first_at[0] > 4.9 && first_at[0] < 5.2, "back at %.3f s (its end 4.5 + 0.4 + the fade)", first_at[0]);
        CHECK(third_at[1] > 12.2 && third_at[1] < 12.3, "the cinematic: %.3f s", third_at[1]);
        CHECK(first_at[1] > 18.4 && first_at[1] < 18.7, "back at %.3f s", first_at[1]);
        CHECK(third_at[2] > 22.2 && third_at[2] < 22.3, "death: %.3f s", third_at[2]);
        CHECK(first_at[2] > 28.4 && first_at[2] < 28.7, "the respawn: %.3f s", first_at[2]);
    }
    CHECK(black_frames >= 6 * 3, "black frames %d", black_frames);
    printf("sequence (30 s at 90 Hz): an execution (2 m in 2.5 s), a 0.4 s counter (first person), a cinematic, death by "
           "tag and the respawn: %d swaps, all black, %d frames in third person\n", swaps, third_frames);
}

/* a sync record that outlived Talion's part (boot-check-20261024-020824's 87 s counter stun) */
static void test_sync_age(void) {
    somvr_fb_sync_age a;
    memset(&a, 0, sizeof a);
    const uintptr_t R = 0x1000, R2 = 0x2000, A = 0x50, B = 0x60;
    const float S = SOMVR_FB_SYNC_STALE_S, M = SOMVR_FB_SYNC_MAX_S;
    CHECK(!somvr_fb_sync_live(&a, 0, 1, A, 0.0, S, M), "no record: none");
    /* the counter: its attack A for 1.8 s, then Talion recoils (B) and has none while the record stays */
    int live = 1, until = -1;
    for (int f = 0; f <= 90 * 6; f++) {
        double t = 10.0 + f / 90.0;
        uintptr_t atk = t < 11.8 ? A : (f / 45) % 2 ? B : 0;
        int l = somvr_fb_sync_live(&a, R, 1, atk, t, S, M);
        if (l) until = f;
        live &= f > 90 * 3 || l == (t < 11.8 + S);
    }
    CHECK(live && NEAR(until / 90.0, 1.8 + S, 0.02) && a.stale && a.stales == 1,
          "the counter: live while its attack runs and %.1f s after, then stale (%.3f s)", S, until / 90.0);
    CHECK(!somvr_fb_sync_live(&a, R, 1, 0, 20.0, S, M), "stale stays stale");
    CHECK(somvr_fb_sync_live(&a, R, 1, A, 20.1, S, M) && !a.stale, "its attack again: the same sync anew");
    /* a new record (a chained sync) starts fresh; the record cleared resets */
    somvr_fb_sync_live(&a, R, 1, B, 21.5, S, M);
    CHECK(somvr_fb_sync_live(&a, R, 1, B, 22.4, S, M) && !somvr_fb_sync_live(&a, R, 1, B, 22.5, S, M),
          "stale again after 1 s of another attack");
    CHECK(somvr_fb_sync_live(&a, R2, 1, B, 22.7, S, M) && a.atk0 == B, "a new record: live, its attack B");
    CHECK(!somvr_fb_sync_live(&a, 0, 1, B, 22.8, S, M) && !a.rec && !a.stale, "cleared");
    /* a record that began before the attack showed: the first attack within 0.5 s is its own */
    somvr_fb_sync_live(&a, R, 1, 0, 30.0, S, M);
    somvr_fb_sync_live(&a, R, 1, A, 30.1, S, M);
    CHECK(a.atk0 == A && somvr_fb_sync_live(&a, R, 1, A, 32.0, S, M), "the attack after the record: adopted");
    /* the attack flickering off for less than stale_s: live; no attack read: only the cap */
    CHECK(somvr_fb_sync_live(&a, R, 1, 0, 32.5, S, M) && somvr_fb_sync_live(&a, R, 1, A, 33.4, S, M) &&
              somvr_fb_sync_live(&a, R, 1, 0, 34.3, S, M),
          "a short gap: live");
    somvr_fb_sync_live(&a, R2, 0, 0, 40.0, S, M);
    CHECK(somvr_fb_sync_live(&a, R2, 0, 0, 40.0 + M - 0.01, S, M) && !somvr_fb_sync_live(&a, R2, 0, 0, 40.0 + M, S, M),
          "no attack read: stale at the %.0f s cap", M);
    somvr_fb_sync_live(&a, R, 1, 0, 45.0, S, M);
    CHECK(somvr_fb_sync_live(&a, R, 1, 0, 46.0, S, M) && somvr_fb_sync_live(&a, R, 1, B, 47.0, S, M) &&
              !somvr_fb_sync_live(&a, R, 1, 0, 45.0 + M, S, M),
          "no attack at its start (none adopted within 0.5 s): the cap alone");
    somvr_fb_sync_live(&a, 0, 1, 0, 49.0, S, M);
    somvr_fb_sync_live(&a, R, 1, A, 50.0, S, 0);
    CHECK(somvr_fb_sync_live(&a, R, 1, A, 500.0, S, 0), "cap 0: no cap");
    printf("sync age: the counter's record live while Talion's attack is its own and %.1f s after, then stale until it "
           "changes; the %.0f s cap without an attack read\n", S, M);
}


int main(void) {
    test_detect();
    test_tags();
    test_switch();
    test_transition();
    test_sequence();
    test_sync_age();
    printf("test_fallback: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
