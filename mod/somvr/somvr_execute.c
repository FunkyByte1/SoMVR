/* somvr_execute.c - SOMVR_EXECUTE's pure part (somvr_execute.h) */
#include "somvr_execute.h"

#include <math.h>
#include <string.h>

int somvr_ex_ready(const somvr_ex_hc *h) {
    if (!h || !h->valid || h->need <= 0) return 0;
    return h->charged || (h->enabled && h->charge >= h->need);
}

int somvr_ex_spend(const somvr_ex_hc *h, int16_t *charge_to, uint8_t *uses_to) {
    *charge_to = h ? h->charge : 0, *uses_to = h ? h->uses : 0;
    if (!h || !h->valid || h->need <= 0) return 0;
    if (h->uses > 1) { /* movzbl 0xd2; cmp 1; jbe ...; sub 1; mov 0xd2 */
        *uses_to = (uint8_t)(h->uses - 1);
        return 1;
    }
    if (!h->charge) return 0; /* cmpw 0, 0xc6; je: nothing to set */
    *charge_to = 0;
    return 2;
}

int somvr_ex_trigger(int down, float v) {
    if (!(v == v)) return 0;
    return down ? v >= 0.4f : v >= 0.6f;
}

const char *somvr_ex_judge_name(int j) {
    static const char *const n[] = {"none", "execute", "not ready", "not a heavy hit", "no trigger"};
    return j >= 0 && j <= SOMVR_EXJ_NO_TRIGGER ? n[j] : "?";
}

int somvr_ex_judge(int ready, int trigger, int tier) {
    if (!trigger) return SOMVR_EXJ_NO_TRIGGER;
    if (!ready) return SOMVR_EXJ_NOT_READY;
    return tier == 2 ? SOMVR_EXJ_EXECUTE : SOMVR_EXJ_NOT_HEAVY;
}

int somvr_ex_behind(const float c[3], const float o[3], float yaw, float back_deg, float *off_deg) {
    float dx = c[0] - o[0], dz = c[2] - o[2];
    float off = 0;
    if (dx * dx + dz * dz > 1e-6f) {
        off = remainderf(atan2f(dx, dz) - yaw, 6.2831853f) * 57.29578f;
    }
    if (off_deg) *off_deg = off;
    return fabsf(off) > back_deg;
}

int somvr_ex_pick(const char *names, unsigned turn, char *out, int cap) {
    const char *p[16];
    int len[16], n = 0;
    out[0] = 0;
    for (const char *s = names; s && *s && n < 16;) {
        size_t l = strcspn(s, ",");
        if (l) p[n] = s, len[n++] = (int)l;
        s += l + (s[l] == ',');
    }
    if (!n || cap <= 1) return 0;
    int k = (int)(turn % (unsigned)n), l = len[k] < cap - 1 ? len[k] : cap - 1;
    memcpy(out, p[k], (size_t)l);
    out[l] = 0;
    return 1;
}

double somvr_ex_lapse(const somvr_ex_hc *h, int running, double delay, float lapse_s, float ready_s) {
    if (!h || !h->valid || h->streak <= 0 || !running || !(delay > 0) || !isfinite(delay)) return 0;
    double want = somvr_ex_ready(h) && ready_s > 0 ? ready_s : lapse_s;
    return want > delay + 1e-3 ? want : 0;
}
