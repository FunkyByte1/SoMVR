/* somvr_captain.c - SOMVR_CAPTAIN's pure part (somvr_captain.h; docs/re-xr-pipeline.md 88) */
#include "somvr_captain.h"

#include <math.h>
#include <string.h>

void somvr_cp_traits_init(somvr_cp_traits *t) { memset(t, 0, sizeof *t); }

static int prefix(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }

void somvr_cp_traits_add(somvr_cp_traits *t, const char *name)
{
    static const struct { const char *pre; int rule; } r[] = {
        { "Immune_", SOMVR_CPT_IMMUNE }, { "Vulnerable_", SOMVR_CPT_VULNERABLE },
        { "Resistant_", SOMVR_CPT_RESISTANT }, { "DamagedBy_", SOMVR_CPT_DAMAGEDBY } };
    static const struct { const char *w; int kind; } k[] = {
        { "Combat", SOMVR_CPF_COMBAT }, { "Stealth", SOMVR_CPF_STEALTH }, { "Ranged", SOMVR_CPF_RANGED },
        { "GrabKnockdown", SOMVR_CPF_GRAB } };
    if (!name || !*name)
        return;
    t->n++;
    if (!strcmp(name, "Marker_Warchief"))
        t->warchief = 1;
    t->fears += prefix(name, "Fear_");
    t->hates += prefix(name, "Hate_");
    for (size_t i = 0; i < sizeof r / sizeof r[0]; i++) {
        if (!prefix(name, r[i].pre))
            continue;
        const char *w = name + strlen(r[i].pre);
        for (size_t j = 0; j < sizeof k / sizeof k[0]; j++) /* the whole word: "Block_Immune_Combat" is no rule */
            if (!strcmp(w, k[j].w) && r[i].rule > t->rule[k[j].kind])
                t->rule[k[j].kind] = (uint8_t)r[i].rule;
    }
}

const char *somvr_cp_kind_name(int kind)
{
    static const char *const n[SOMVR_CPF_N] = { "combat finishers", "stealth", "ranged", "grab" };
    return kind >= 0 && kind < SOMVR_CPF_N ? n[kind] : "?";
}

const char *somvr_cp_rule_name(int rule)
{
    static const char *const n[] = { "-", "damaged by", "resistant", "vulnerable", "immune" };
    return rule >= SOMVR_CPT_NONE && rule <= SOMVR_CPT_IMMUNE ? n[rule] : "?";
}

int somvr_cp_force_kill(const somvr_cp_traits *t, int kind)
{
    int r = t && kind >= 0 && kind < SOMVR_CPF_N ? t->rule[kind] : SOMVR_CPT_NONE;
    return r == SOMVR_CPT_IMMUNE ? -1 : r == SOMVR_CPT_VULNERABLE ? 1 : 0;
}

int somvr_cp_eligible(const somvr_cp_member *m, const char **why)
{
    const char *w = !m->in_hierarchy                       ? "not in the hierarchy"
                    : m->warchief                          ? "a warchief (his bodyguards)"
                    : (m->flags & SOMVR_CP_FL_DEAD)        ? "dead"
                    : (m->flags & SOMVR_CP_FL_MOCKDEAD)    ? "thought dead"
                    : (m->flags & SOMVR_CP_FL_INWORLD)     ? "in the world now"
                    : !m->has_template                     ? "no template"
                                                           : NULL;
    if (why)
        *why = w ? w : "eligible";
    return !w;
}

int somvr_cp_next(const somvr_cp_member *m, int n, int last)
{
    if (!m || n <= 0)
        return -1;
    int start = last >= 0 && last < n ? last + 1 : 0;
    for (int k = 0; k < n; k++) {
        int i = (start + k) % n;
        if (somvr_cp_eligible(&m[i], NULL))
            return i;
    }
    return -1;
}

void somvr_cp_intro_default(somvr_cp_intro_cfg *c)
{
    c->range_cm = 1500.0f, c->half_deg = 60.0f;
}

int somvr_cp_intro_world(const somvr_cp_intro_cfg *c, const float head[3], const float fwd[3], const float cap[3],
                         float *dist_cm, float *off_deg)
{
    float d[3] = { cap[0] - head[0], cap[1] + 120.0f - head[1], cap[2] - head[2] };
    float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    float cs = l > 1.0f && fl > 1e-6f ? (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]) / (l * fl) : 1.0f;
    float off = acosf(cs < -1.0f ? -1.0f : cs > 1.0f ? 1.0f : cs) * 57.29578f;
    if (dist_cm)
        *dist_cm = l;
    if (off_deg)
        *off_deg = off;
    return isfinite(l) && isfinite(off) && l <= c->range_cm && off <= c->half_deg;
}

float somvr_cp_cut_factor(int committed, float k, float cut_x)
{
    if (!committed || !(cut_x > 1.0f))
        return 1.0f;
    float kk = isfinite(k) && k > 0 ? (k < 1.0f ? k : 1.0f) : (isfinite(k) ? 0.0f : 1.0f);
    float x = cut_x * kk;
    return x > 1.0f ? x : 1.0f;
}
