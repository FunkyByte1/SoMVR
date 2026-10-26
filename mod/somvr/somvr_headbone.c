/* somvr_headbone.c - see somvr_headbone.h */
#include "somvr_headbone.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int hb_tree_build(hb_tree *t, const uint8_t *parent, const uint32_t *hash, int n, uint32_t head_hash, char *why,
                  size_t cap) {
    memset(t, 0, sizeof *t);
    t->head = t->parent = -1;
    if (!parent || !hash || n < 2 || n > SA_MAXN) return snprintf(why, cap, "bad skeleton (%d nodes)", n), -1;
    if (parent[0] != 0xff) return snprintf(why, cap, "node 0 has a parent (%u)", parent[0]), -1;
    for (int i = 1; i < n; i++)
        if (parent[i] >= i) return snprintf(why, cap, "node %d's parent %u does not precede it", i, parent[i]), -1;
    int h = -1;
    for (int i = 0; i < n; i++)
        if (hash[i] == head_hash) {
            if (h >= 0) return snprintf(why, cap, "two nodes hash like Head (%#x): %d and %d", head_hash, h, i), -1;
            h = i;
        }
    if (h < 1) return snprintf(why, cap, h ? "no Head (%#x) in the skeleton" : "Head (%#x) is the root", head_hash), -1;
    uint8_t in[SA_MAXN];
    memset(in, 0, sizeof in);
    in[h] = 1;
    for (int i = h + 1; i < n; i++) { /* one forward pass: a node is below Head when its parent is */
        if (!in[parent[i]]) continue;
        in[i] = 1;
        t->sub[t->nsub++] = (int16_t)i;
    }
    if (!t->nsub) return snprintf(why, cap, "Head %d has no nodes below it", h), -1;
    for (int k = 0; k < t->nsub; k++) {
        int i = t->sub[k];
        if (t->nrun && t->run[t->nrun - 1][1] == i - 1) {
            t->run[t->nrun - 1][1] = (int16_t)i;
            continue;
        }
        if (t->nrun == HB_MAXRUN)
            return snprintf(why, cap, "the %d nodes below Head %d lie in more than %d index runs", t->nsub, h, HB_MAXRUN), -1;
        t->run[t->nrun][0] = t->run[t->nrun][1] = (int16_t)i;
        t->nrun++;
    }
    t->head = h, t->parent = parent[h];
    int o = snprintf(why, cap, "Head %d (in %d), %d nodes below in %d run%s:", h, t->parent, t->nsub, t->nrun,
                     t->nrun == 1 ? "" : "s");
    for (int r = 0; r < t->nrun && o > 0 && (size_t)o < cap; r++)
        o += snprintf(why + o, cap - (size_t)o, " %d..%d", t->run[r][0], t->run[r][1]);
    return 0;
}

int hb_collapse(sa_node *pose, int n, const hb_tree *t, float s) {
    if (!pose || !t || t->head < 1 || t->head >= n || t->nsub < 1 || !(s >= 0 && s <= 1)) return 0;
    const float *c = pose[t->head].pos;
    if (!isfinite(c[0]) || !isfinite(c[1]) || !isfinite(c[2])) return 0;
    for (int k = 0; k < t->nsub; k++)
        if (t->sub[k] <= t->head || t->sub[k] >= n) return 0;
    for (int k = 0; k < t->nsub; k++) {
        sa_node *d = &pose[t->sub[k]];
        for (int a = 0; a < 3; a++) {
            float v = c[a] + s * (d->pos[a] - c[a]);
            d->pos[a] = isfinite(v) ? v : c[a]; /* a NaN child joins the head joint */
        }
        d->pad = 0;
    }
    return 1;
}
