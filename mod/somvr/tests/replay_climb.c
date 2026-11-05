/* replay_climb.c - som-b37-climb2 (docs/re-xr-pipeline.md 172.5): a player's recorded climb (a bug mark's input.inrec,
 * a session's <log>.inrec) replayed offline through the climb logic (somvr_climb_step) against a model of the rock: the
 * player's own grips, squeezes, sticks, eyes and rig frame by frame at their recorded rate; Talion's body moved by the
 * logic's own requests (the next frame, as the PM hook), its capsule (CAP_CM round the root) kept out of the rock. The
 * world: a wedge of two vertical faces (each a point and its normal out of the rock, the session's own hits) - an
 * outside corner. Out: the view's least distance from the rock, the frames it was under 8 cm (HEADROCK's fade), the
 * grabs, the slide's and the keep's counts - the same motion with the head's keep off and on.
 * Not part of build.sh --check (a tool on a recording):
 *   gcc -O2 -I. -I../../third_party/OpenXR-SDK-1.1.61/include -o build/tests/replay_climb tests/replay_climb.c \
 *       somvr_climb.c somvr_inrec.c somvr_combat.c somvr_slots.c somvr_smooth.c somvr_hands.c somvr_eyes.c somvr_pose.c -lm
 *   build/tests/replay_climb FILE T0 T1 "X1 Z1 NX1 NZ1  X2 Z2 NX2 NZ2" [keep 0|1] [side 0|1]
 * T0 T1: the game clock (somvr.log's t=). The faces: world cm, the normals level, out of the rock. RC_REACH=CM and
 * RC_LIBERAL=1: the reach and the liberal rays (build 39's env: 35, 1); RC_KEEP_CM: the keep's distance (20). */
#include "../somvr_climb.h"
#include "../somvr_eyes.h"
#include "../somvr_hands.h"
#include "inrec_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAP_CM 35.0f /* the capsule's radius round the root (the game's keeps the root 47-57 cm off the faces) */
static float g_f[2][4];  /* the faces: x z nx nz (out of the rock) */

/* the level distance from p to the wedge (both faces' half-planes behind them = rock); < 0 inside; *n the way out */
static float wedge_dist(float x, float z, float *nx, float *nz) {
    float d[2];
    for (int i = 0; i < 2; i++) d[i] = (x - g_f[i][0]) * g_f[i][2] + (z - g_f[i][1]) * g_f[i][3];
    if (d[0] < 0 && d[1] < 0) { /* inside: out by the nearer face */
        int k = d[0] > d[1] ? 0 : 1;
        if (nx) *nx = g_f[k][2], *nz = g_f[k][3];
        return d[k];
    }
    /* outside: the nearest point of the wedge's boundary (each face's ray from the edge, or the edge) */
    float best = 1e30f, bx = 0, bz = 0;
    float a0 = g_f[0][2], b0 = g_f[0][3], a1 = g_f[1][2], b1 = g_f[1][3], c0 = a0 * g_f[0][0] + b0 * g_f[0][1],
          c1 = a1 * g_f[1][0] + b1 * g_f[1][1], det = a0 * b1 - a1 * b0;
    float ex = (c0 * b1 - c1 * b0) / det, ez = (a0 * c1 - a1 * c0) / det; /* the edge */
    for (int i = 0; i < 2; i++) {
        int j = 1 - i;
        float tx = -g_f[i][3], tz = g_f[i][2]; /* along face i */
        if (tx * g_f[j][2] + tz * g_f[j][3] > 0) tx = -tx, tz = -tz; /* away from face j's front: along the rock */
        float s = (x - ex) * tx + (z - ez) * tz;
        if (s < 0) s = 0;
        float px = ex + tx * s, pz = ez + tz * s, dd = hypotf(x - px, z - pz);
        if (dd < best) best = dd, bx = px, bz = pz;
    }
    if (nx) {
        float l = fmaxf(best, 1e-3f);
        *nx = (x - bx) / l, *nz = (z - bz) / l;
    }
    return best;
}
static int wedge_ray(void *ctx, int hand, sv_vec3 from, sv_vec3 dir, float len, somvr_climb_hit *out) {
    (void)ctx, (void)hand;
    memset(out, 0, sizeof *out);
    float t0 = 0, t1 = len;
    for (int i = 0; i < 2; i++) { /* inside face i's half-plane: (p - F) . n <= 0 */
        float s0 = (from.x - g_f[i][0]) * g_f[i][2] + (from.z - g_f[i][1]) * g_f[i][3];
        float ds = dir.x * g_f[i][2] + dir.z * g_f[i][3];
        if (fabsf(ds) < 1e-6f) {
            if (s0 > 0) return 1;
            continue;
        }
        float t = -s0 / ds;
        if (ds < 0) t0 = fmaxf(t0, t); /* entering */
        else t1 = fminf(t1, t);
    }
    if (t0 > t1 || t0 <= 0) return 1; /* (from inside: no hit, as the game's) */
    out->hit = 1, out->climbable = 1, out->dist = t0;
    out->point = (sv_vec3){ from.x + dir.x * t0, from.y + dir.y * t0, from.z + dir.z * t0 };
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "replay_climb FILE T0 T1 \"X1 Z1 NX1 NZ1 X2 Z2 NX2 NZ2\" [keep 0|1] [side 0|1]\n");
        return 2;
    }
    irr_rec r;
    if (irr_load_file(&r, argv[1])) return 1;
    double T0 = atof(argv[2]), T1 = atof(argv[3]);
    if (sscanf(argv[4], "%f %f %f %f %f %f %f %f", &g_f[0][0], &g_f[0][1], &g_f[0][2], &g_f[0][3], &g_f[1][0], &g_f[1][1],
               &g_f[1][2], &g_f[1][3]) != 8)
        return 2;
    for (int i = 0; i < 2; i++) {
        float l = hypotf(g_f[i][2], g_f[i][3]);
        g_f[i][2] /= l, g_f[i][3] /= l;
    }
    int keep = argc > 5 ? atoi(argv[5]) : 1, side = argc > 6 ? atoi(argv[6]) : 0;
    somvr_climb s;
    somvr_climb_cfg c;
    somvr_climb_init(&s);
    somvr_climb_cfg_default(&c);
    /* the session's switches (scripts/headset-build26.env) that act on a face with no tops or bumps */
    c.letgo = SOMVR_CLIMB_LETGO_HANG, c.fling = 1, c.fling_up = 1, c.fling_min = 110, c.fling_gain = 1.15f;
    c.fling_side = 0.7f, c.fling_max = 650, c.face_toward = 1, c.hug = 1, c.fling_down = 1;
    c.head_keep = keep, c.side = side;
    if (getenv("RC_REACH")) c.reach_cm = (float)atof(getenv("RC_REACH")); /* (the env's: 35) */
    if (getenv("RC_LIBERAL")) c.liberal = atoi(getenv("RC_LIBERAL"));
    if (getenv("RC_KEEP_CM")) c.keep_cm = (float)atof(getenv("RC_KEEP_CM"));
    sv_vec3 dB = { 0, 0, 0 }, pend = { 0, 0, 0 };
    int have0 = 0, n = 0, grabs = 0, under8 = 0, under15 = 0;
    sv_vec3 B0 = { 0, 0, 0 };
    float least = 1e9f, least_root = 1e9f;
    double least_t = 0;
    for (int i = 0; i < r.nf; i++) {
        const somvr_ir_frame *f = &r.f[i];
        if (f->t < T0 || f->t > T1) continue;
        if (!have0) B0 = (sv_vec3){ f->base[0], f->base[1], f->base[2] }, have0 = 1;
        dB = (sv_vec3){ dB.x + pend.x, dB.y + pend.y, dB.z + pend.z };
        pend = (sv_vec3){ 0, 0, 0 };
        /* the root's capsule out of the rock (the game's collision) */
        sv_vec3 eyes = { B0.x + dB.x, B0.y + dB.y, B0.z + dB.z };
        float nx, nz, rd = wedge_dist(eyes.x, eyes.z, &nx, &nz);
        if (rd < CAP_CM) dB.x += nx * (CAP_CM - rd), dB.z += nz * (CAP_CM - rd), eyes.x += nx * (CAP_CM - rd), eyes.z += nz * (CAP_CM - rd);
        somvr_rig rg;
        somvr_rig_init(&rg);
        rg.have_ref = 1, rg.p0 = (sv_vec3){ f->rig_p0[0], f->rig_p0[1], f->rig_p0[2] }, rg.yaw0 = f->rig_yaw0, rg.scale = f->scale;
        somvr_base b = { eyes, f->base_yaw };
        somvr_climb_in in;
        memset(&in, 0, sizeof in);
        in.t = f->t, in.live = 1, in.allow = 1, in.head_ok = 1;
        in.head_x = eyes.x, in.head_y = eyes.y, in.head_z = eyes.z;
        in.root_ok = 1, in.root = (sv_vec3){ eyes.x, eyes.y - 70.0f, eyes.z }; /* (the root under the eyes, as the game's) */
        in.rig_yaw = f->base_yaw - f->rig_yaw0, in.rig_scale = f->scale;
        somvr_posef eye[2];
        for (int e = 0; e < 2; e++) {
            memcpy(eye[e].p, f->eye_p[e], sizeof eye[e].p);
            memcpy(eye[e].q, f->eye_q[e], sizeof eye[e].q);
        }
        sv_quat vq;
        somvr_rig_head(&rg, b, eye, &in.view, &vq);
        in.view_ok = 1;
        for (int h = 0; h < 2; h++) {
            somvr_climb_hand *hd = &in.hand[h];
            if (!f->hand[h].valid) continue;
            somvr_hand sh;
            somvr_ir_hand_get(&f->hand[h], f->xr_time, &sh, NULL);
            sv_vec3 p, v;
            sv_quat q;
            somvr_hand_game(&sh, &p, &q, &v, NULL);
            sv_quat qw = somvr_rig_quat(&rg, b, q);
            hd->valid = 1, hd->p = p, hd->pw = somvr_rig_point(&rg, b, p);
            hd->palm = sv_qrot(qw, (sv_vec3){ h == 0 ? 1.0f : -1.0f, 0, 0 }), hd->fwd = sv_qrot(qw, (sv_vec3){ 0, 0, 1 });
            hd->squeeze = f->hand[h].squeeze, hd->buttons = 1;
        }
        in.stick_x = f->hand[0].stick[0], in.stick_y = f->hand[0].stick[1]; /* the move stick: the left */
        in.stick = hypotf(in.stick_x, in.stick_y);
        somvr_climb_out o;
        uint64_t g0 = s.grabs;
        somvr_climb_step(&s, &c, &in, wedge_ray, NULL, &o);
        grabs += (int)(s.grabs - g0);
        if (o.move && o.active) pend = o.move_w;
        if (s.active) {
            float vd = wedge_dist(in.view.x, in.view.z, NULL, NULL);
            n++;
            if (vd < least) least = vd, least_t = f->t;
            least_root = fminf(least_root, wedge_dist(in.root.x, in.root.z, NULL, NULL));
            under8 += vd < 8.0f, under15 += vd < 15.0f;
        }
    }
    printf("replay_climb %s t=%.1f..%.1f keep %d side %d: %d climbing frames, %d grabs; the view's least %.1f cm from the "
           "rock (t=%.2f), under 8 cm %d frames, under 15 cm %d; the root's least %.1f; keep episodes %llu (cut %.0f cm, out "
           "%.0f cm); hug in %.0f cm\n", argv[1], T0, T1, keep, side, n, grabs, least, least_t, under8, under15, least_root,
           (unsigned long long)s.keep_eps, s.keep_cut_cm, s.keep_push_cm, s.hug_in_cm);
    irr_free(&r);
    return 0;
}
