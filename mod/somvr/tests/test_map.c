/* test_map.c - unit tests of somvr_map.c (build 27, som-b27-world): SOMVR_MAP_POINT's parse; a click's region (the
 * cross = A, the tower popup's rows = X / Y only with a travel point, elsewhere a pan); a press's planned pan at once
 * (the direction, the stick's sign, its time from the pan's rate and start, the shortest nudge, its end); a held press steering toward the pointer and
 * stopping at its release (after its own pan); a new press cancelling a pan; the map closed mid-pan; off = nothing ever. */
#include "../somvr_map.h"

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
#define DT (1.0 / 35) /* the headless input tick (boot 20261030-073117: 35 ticks a second in the map) */

static void test_parse(void) {
    somvr_map_cfg c;
    CHECK(somvr_map_parse(NULL, &c) == 0 && !c.on && c.pan_pxs == 310.0f && fabsf(c.lag_s + 0.2f) < 1e-6f, "unset: off, the defaults");
    CHECK(somvr_map_parse("on", &c) == 0 && c.on, "on");
    CHECK(somvr_map_parse("off", &c) == 0 && !c.on, "off");
    CHECK(somvr_map_parse("on,800,50,300", &c) == 0 && c.on && c.pan_pxs == 800.0f && fabsf(c.lag_s - 0.05f) < 1e-6f &&
              c.steer_px == 300.0f, "on with the numbers");
    CHECK(somvr_map_parse("on,800,50", &c) == -1 && !c.on, "two numbers: bad, off");
    CHECK(somvr_map_parse("on,10,50,300", &c) == -1 && !c.on, "a rate out of range: bad");
    CHECK(somvr_map_parse("on,310,-200,360", &c) == 0 && c.on && fabsf(c.lag_s + 0.2f) < 1e-6f, "a negative start");
    CHECK(somvr_map_parse("onx", &c) == -1 && !c.on, "a wrong word");
    CHECK(somvr_map_parse("on,800,50,300x", &c) == -1 && !c.on, "trailing junk");
}

static void test_region(void) {
    somvr_map_cfg c;
    somvr_map_parse("on", &c);
    float p0[2] = { 10, -12 }, px[2] = { 0, -100 }, py[2] = { 60, -50 }, pf[2] = { 300, 200 }, pn[2] = { NAN, 0 };
    CHECK(somvr_map_region(&c, p0, 0) == SOMVR_MAPR_A, "the cross: A");
    CHECK(somvr_map_region(&c, p0, 1) == SOMVR_MAPR_A, "the cross with the popup: A");
    CHECK(somvr_map_region(&c, px, 1) == SOMVR_MAPR_X, "the popup's first row: X");
    CHECK(somvr_map_region(&c, py, 1) == SOMVR_MAPR_Y, "the popup's second row: Y");
    CHECK(somvr_map_region(&c, px, 0) == SOMVR_MAPR_PAN, "no travel point: the rows are map, a pan");
    CHECK(somvr_map_region(&c, pf, 1) == SOMVR_MAPR_PAN, "away: a pan");
    CHECK(somvr_map_region(&c, pn, 1) == SOMVR_MAPR_PAN, "NaN: a pan (no press)");
}

/* run ticks until t1 with the given input; returns the axis ticks and the last output */
static int run(somvr_map_st *s, const somvr_map_cfg *c, somvr_map_in *in, double t1, somvr_map_out *last, float *sum) {
    int n = 0;
    somvr_map_out o;
    while (in->t < t1 - 1e-9) {
        in->t += DT;
        somvr_map_step(s, c, in, &o);
        in->down = 0;
        if (o.naxis == 2) n++, sum[0] += o.axis[0] * (float)DT, sum[1] += o.axis[1] * (float)DT;
        *last = o;
    }
    return n;
}

static void test_click_pan(void) {
    somvr_map_cfg c;
    somvr_map_parse("on,640,0,360", &c);
    somvr_map_st s;
    somvr_map_init(&s);
    somvr_map_in in = { .map = 1, .ptr = 1, .t = 100.0 };
    somvr_map_out o, last;
    float sum[2] = { 0, 0 };
    /* a press 320 px right, 240 px up of the cross: the pan starts at once */
    in.px[0] = 320, in.px[1] = -240, in.down = 1, in.pressing = 1;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    in.down = 0;
    CHECK(o.event == SOMVR_MAPE_CLICK && s.phase == SOMVR_MAPP_PAN && o.naxis == 2, "the press: the pan starts at once");
    CHECK(fabsf(o.axis[0] - 0.8f) < 1e-4f && fabsf(o.axis[1] - 0.6f) < 1e-4f, "the stick toward it, up positive: %.3f %.3f",
          o.axis[0], o.axis[1]);
    CHECK(fabsf(s.plan_px - 400.0f) < 1e-3f && fabsf(s.plan_s - 0.625f) < 1e-4f, "400 px at 640 px/s: %.3f s", s.plan_s);
    sum[0] = o.axis[0] * (float)DT, sum[1] = o.axis[1] * (float)DT;
    run(&s, &c, &in, in.t + 0.1, &last, sum);
    in.pressing = 0; /* let go after 0.1 s: the pan goes on */
    double t0 = in.t;
    in.px[0] = 900, in.px[1] = 900; /* the pointer elsewhere meanwhile: the planned pan does not care */
    int n = run(&s, &c, &in, t0 + 1.0, &last, sum);
    CHECK(s.phase == SOMVR_MAPP_IDLE, "the pan ended");
    float moved = sqrtf(sum[0] * sum[0] + sum[1] * sum[1]) * c.pan_pxs;
    CHECK(fabsf(moved - 400.0f) < 640.0f * DT * 1.01f, "the stick's time x rate = the distance: %.1f px (%d ticks)", moved, n);
    CHECK(s.pans == 1 && s.clicks == 1 && s.steers == 0, "counters");
    /* the measured start: 310 px/s, -200 ms - 100 px = 0.12 s, 20 px = the shortest touch 0.05 s */
    somvr_map_parse("on", &c);
    in.px[0] = 100, in.px[1] = 0, in.down = 1, in.pressing = 1;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    CHECK(fabsf(s.plan_s - (100.0f / 310.0f - 0.2f)) < 1e-4f, "100 px: %.3f s", s.plan_s);
    in.down = 0, in.pressing = 0;
    run(&s, &c, &in, in.t + 0.5, &last, sum);
    in.px[0] = 0, in.px[1] = 20, in.down = 1, in.pressing = 1;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    CHECK(s.region == SOMVR_MAPR_A, "20 px: the cross (A)");
    in.down = 0, in.pressing = 0;
    run(&s, &c, &in, in.t + 0.5, &last, sum);
    in.px[0] = 0, in.px[1] = 40, in.down = 1, in.pressing = 1;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    CHECK(s.phase == SOMVR_MAPP_PAN && fabsf(s.plan_s - 0.05f) < 1e-6f && o.axis[1] < 0, "40 px down: a nudge %.3f s",
          s.plan_s);
    in.down = 0, in.pressing = 0;
    run(&s, &c, &in, in.t + 0.5, &last, sum);
    /* ysign -1: up is negative */
    c.ysign = -1;
    in.px[0] = 0, in.px[1] = -200, in.down = 1, in.pressing = 1;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    CHECK(o.naxis == 2 && fabsf(o.axis[1] + 1.0f) < 1e-4f && fabsf(o.axis[0]) < 1e-4f, "ysign -1: %.3f", o.axis[1]);
}

static void test_buttons(void) {
    somvr_map_cfg c;
    somvr_map_parse("on", &c);
    somvr_map_st s;
    somvr_map_init(&s);
    somvr_map_in in = { .map = 1, .ptr = 1, .t = 10.0, .travel = 1 };
    somvr_map_out o, last;
    float sum[2] = { 0, 0 };
    struct { float x, y; int cmd0, n; } k[] = { { 5, 5, SOMVR_MAP_CMD_ENTER, 2 }, { 20, -100, SOMVR_MAP_CMD_TRAVEL, 1 },
                                                { 20, -50, SOMVR_MAP_CMD_TIME, 1 } };
    for (int i = 0; i < 3; i++) {
        in.px[0] = k[i].x, in.px[1] = k[i].y, in.down = 1, in.pressing = 1;
        in.t += DT;
        somvr_map_step(&s, &c, &in, &o);
        in.down = 0, in.pressing = 0;
        CHECK(o.ncmd == k[i].n && o.cmd[0] == k[i].cmd0 && o.naxis == 0, "click %d: command %d (%d)", i, o.cmd[0], o.ncmd);
        int held = 1;
        double t0 = in.t;
        while (in.t < t0 + 0.5) {
            in.t += DT;
            somvr_map_step(&s, &c, &in, &o);
            held += o.ncmd > 0;
        }
        CHECK(held >= 4 && held <= 6 && s.phase == SOMVR_MAPP_IDLE, "click %d: held %d ticks (0.12 s), then idle", i, held);
    }
    CHECK(s.a == 1 && s.x == 1 && s.y == 1 && s.pans == 0, "A, X, Y once each, no pan");
    (void)last, (void)sum;
}

static void test_steer_cancel_close(void) {
    somvr_map_cfg c;
    somvr_map_parse("on,640,0,360", &c);
    somvr_map_st s;
    somvr_map_init(&s);
    somvr_map_in in = { .map = 1, .ptr = 1, .t = 0.0 };
    somvr_map_out o, last;
    float sum[2] = { 0, 0 };
    /* held: the pan to the press (720 px left: 1.125 s), then steering, full speed 720 px out, half at 180 px */
    in.px[0] = -720, in.px[1] = 0, in.down = 1, in.pressing = 1;
    run(&s, &c, &in, 1.0, &last, sum);
    CHECK(s.phase == SOMVR_MAPP_PAN && last.naxis == 2 && last.axis[0] < -0.99f, "held 1 s: the pan under way");
    run(&s, &c, &in, 1.3, &last, sum);
    CHECK(s.phase == SOMVR_MAPP_STEER && last.naxis == 2 && fabsf(last.axis[0] + 1.0f) < 1e-4f, "then steering left at full: "
          "%.3f", last.axis[0]);
    in.px[0] = 0, in.px[1] = 180;
    run(&s, &c, &in, 1.4, &last, sum);
    CHECK(last.naxis == 2 && fabsf(last.axis[1] + 0.5f) < 1e-4f && fabsf(last.axis[0]) < 1e-4f, "down at half: %.3f",
          last.axis[1]);
    in.ptr = 0;
    run(&s, &c, &in, 1.5, &last, sum);
    CHECK(s.phase == SOMVR_MAPP_STEER && last.naxis == 0, "off the board: no pan, still held");
    in.ptr = 1, in.pressing = 0;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    CHECK(o.event == SOMVR_MAPE_STEER_OFF && s.phase == SOMVR_MAPP_IDLE && o.naxis == 0, "let go: stops at once");
    /* a click's pan cancelled by a new press */
    in.px[0] = 600, in.px[1] = 0, in.down = 1, in.pressing = 1;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    in.down = 0, in.pressing = 0;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    CHECK(s.phase == SOMVR_MAPP_PAN, "a pan");
    in.px[0] = -300, in.down = 1, in.pressing = 1;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    CHECK(s.phase == SOMVR_MAPP_PAN && o.axis[0] < 0 && s.cancels == 1, "a new press: the old pan cancelled, the new one");
    in.down = 0, in.pressing = 0;
    in.map = 0;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    CHECK(o.event == SOMVR_MAPE_CLOSED && s.phase == SOMVR_MAPP_IDLE && o.naxis == 0, "the map closed: stops");
    /* off: nothing ever */
    somvr_map_parse("off", &c);
    somvr_map_init(&s);
    in.map = 1, in.px[0] = 5, in.px[1] = 5, in.down = 1, in.pressing = 1;
    in.t += DT;
    somvr_map_step(&s, &c, &in, &o);
    in.down = 0, in.pressing = 0;
    CHECK(o.ncmd == 0 && o.naxis == 0 && s.clicks == 0, "off: nothing");
}

int main(void) {
    test_parse();
    test_region();
    test_click_pan();
    test_buttons();
    test_steer_cancel_close();
    printf("test_map: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
