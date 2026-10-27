/* somvr_headparts.c - SOMVR_HIDE_HEAD=parts' pure part (somvr_headparts.h; docs/re-xr-pipeline.md 26) */
#include "somvr_headparts.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const char *somvr_hp_pass_name(int pass)
{
    static const char *const n[] = { "deep", "forward", "late", "gbuffer", "velocity", "shadow", "other" };
    return pass >= 0 && pass <= SOMVR_HP_OTHER ? n[pass] : "?";
}

const char *somvr_hp_slot_name(int slot)
{
    /* FUN_023723f0's slots (the config field it takes): 0 body +0x80, 1 head +0xa0, 2 legs +0xa8, 3 helm +0xb8 (or
     * +0xb0), 4 hair +0xc0, 5 acc1 +0xd0, 6 acc2 +0xd8, 7 +0x90, 8 +0x88, 9-12 armour +0xf0..+0x108, 14 +0xc8 */
    static const char *const n[] = { "body", "head", "legs", "helm", "hair", "acc1", "acc2", "slot7", "slot8", "armour1",
                                     "armour2", "armour3", "armour4", "slot13", "slot14", "slot15", "slot16", "slot17",
                                     "slot18", "slot19" };
    return slot >= 0 && slot < SOMVR_HP_SLOTS ? n[slot]
           : slot == SOMVR_HP_MAIN                 ? "main"
           : slot == SOMVR_HP_WEAPON               ? "weapon"
           : slot == SOMVR_HP_CLOTH                ? "cloth"
                                                   : "?";
}

/* decimal digits at s (at most 3) -> *v; the end, or NULL */
static const char *dec(const char *s, unsigned *v)
{
    unsigned x = 0;
    int n = 0;
    for (; *s >= '0' && *s <= '9' && n < 3; s++, n++)
        x = x * 10 + (unsigned)(*s - '0');
    *v = x;
    return n && !(*s >= '0' && *s <= '9') ? s : NULL;
}

static int list_end(char c) { return !c || c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

int somvr_hp_parse_slots(const char *s, uint32_t *out)
{
    if (!s)
        return -1;
    if (!strncmp(s, "none", 4) && list_end(s[4]))
        return *out = 0, 0;
    static const struct {
        const char *w;
        uint32_t bits;
    } words[] = { { "main", SOMVR_HP_MAIN_BITS }, { "body", 1u << SOMVR_HP_MAIN }, { "cloth", 1u << SOMVR_HP_CLOTH } };
    uint32_t m = 0;
    for (;;) {
        unsigned a, z;
        const char *e;
        int w = -1;
        for (int i = 0; i < 3 && w < 0; i++) {
            size_t l = strlen(words[i].w);
            if (!strncmp(s, words[i].w, l) && (list_end(s[l]) || s[l] == ','))
                w = i;
        }
        if (w >= 0) {
            size_t l = strlen(words[w].w);
            m |= words[w].bits;
            if (s[l] != ',')
                return *out = m, 0;
            s += l + 1;
            continue;
        }
        e = dec(s, &a);
        if (!e || a >= SOMVR_HP_SLOTS)
            return -1;
        z = a;
        if (*e == '-' && (!(e = dec(e + 1, &z)) || z < a || z >= SOMVR_HP_SLOTS))
            return -1;
        for (unsigned i = a; i <= z; i++)
            m |= 1u << i;
        if (*e != ',')
            return list_end(*e) ? (*out = m, 0) : -1;
        s = e + 1;
    }
}

void somvr_hp_slots_str(uint32_t mask, char *out, size_t cap)
{
    size_t o = 0;
    if (!cap)
        return;
    out[0] = 0;
    const char *w = (mask & SOMVR_HP_MAIN_BITS) == SOMVR_HP_MAIN_BITS ? "main"
                    : mask >> SOMVR_HP_MAIN & 1                         ? "body"
                    : mask >> SOMVR_HP_CLOTH & 1                        ? "cloth"
                                                                        : NULL;
    if (w && cap > strlen(w))
        o += (size_t)snprintf(out, cap, "%s", w);
    for (int i = 0; i < SOMVR_HP_SLOTS && o + 4 < cap; i++)
        if (mask >> i & 1)
            o += (size_t)snprintf(out + o, cap - o, o ? ",%d" : "%d", i);
    if (!o)
        snprintf(out, cap, "none");
}

int somvr_hp_vec_n(uintptr_t begin, uintptr_t end, uint32_t stride, int max)
{
    if (end < begin || !stride || (end - begin) % stride || (!begin && end != begin))
        return -1;
    uintptr_t n = (end - begin) / stride;
    return n > (uintptr_t)(max < 0 ? 0 : max) ? -1 : (int)n;
}

float somvr_hp_box_dist(const float lo[3], const float hi[3], const float p[3])
{
    float d2 = 0;
    for (int k = 0; k < 3; k++) {
        if (!isfinite(lo[k]) || !isfinite(hi[k]) || !isfinite(p[k]) || lo[k] > hi[k])
            return -1;
        float d = p[k] < lo[k] ? lo[k] - p[k] : p[k] > hi[k] ? p[k] - hi[k] : 0;
        d2 += d * d;
    }
    return sqrtf(d2);
}

void somvr_hp_set_init(somvr_hp_set *s, uint64_t frame)
{
    memset(s, 0, sizeof *s);
    s->frame = frame;
}

int somvr_hp_match(const somvr_hp_set *s, uintptr_t node)
{
    if (!s || !node)
        return -1;
    for (uint32_t i = 0; i < s->n && i < SOMVR_HP_MAX; i++)
        if (s->node[i] == node)
            return (int)i;
    return -1;
}

int somvr_hp_set_add(somvr_hp_set *s, uintptr_t node, int slot)
{
    if (!node || s->n >= SOMVR_HP_MAX || somvr_hp_match(s, node) >= 0)
        return 0;
    s->node[s->n] = node;
    s->slot[s->n] = (uint8_t)slot;
    s->n++;
    return 1;
}

int somvr_hp_skip(int eye, int pass, const somvr_hp_set *s, uint64_t rt_frame, uintptr_t node)
{
    if (eye < 0 || pass < 0 || pass >= SOMVR_HP_PASSES || !s || !rt_frame || s->frame != rt_frame || !s->n)
        return 0;
    return somvr_hp_match(s, node) >= 0;
}

int somvr_hp_skip_pose(int eye, int pass, const somvr_hp_set *s, uint64_t rt_frame, uintptr_t node_pose)
{
    if (eye < 0 || pass < 0 || pass >= SOMVR_HP_PASSES || !s || !rt_frame || s->frame != rt_frame)
        return 0;
    return s->pose && node_pose == s->pose;
}

int somvr_hp_skip_mesh(int eye, int pass, const somvr_hp_set *s, uint64_t rt_frame, uintptr_t node_mesh)
{
    if (eye < 0 || pass < 0 || pass >= SOMVR_HP_PASSES || !s || !rt_frame || s->frame != rt_frame || !s->away || !node_mesh)
        return 0;
    for (uint32_t i = 0; i < s->nmesh && i < SOMVR_HP_MESHES; i++)
        if (s->mesh[i] == node_mesh)
            return 1;
    return 0;
}

int somvr_hp_add_mesh(somvr_hp_set *s, uintptr_t mesh)
{
    if (!mesh || s->nmesh >= SOMVR_HP_MESHES)
        return 0;
    for (uint32_t i = 0; i < s->nmesh; i++)
        if (s->mesh[i] == mesh)
            return 0;
    s->mesh[s->nmesh++] = mesh;
    return 1;
}

int somvr_hp_add_weapon(somvr_hp_set *s, uintptr_t node, uintptr_t mesh)
{
    if (!node || !mesh || s->nweap >= SOMVR_HP_WEAPONS)
        return 0;
    for (uint32_t i = 0; i < s->nweap; i++)
        if (s->wnode[i] == node)
            return 0;
    s->wnode[s->nweap] = node, s->wmesh[s->nweap] = mesh, s->nweap++;
    return 1;
}

int somvr_hp_copy_of(const somvr_hp_set *s, uintptr_t node, uintptr_t node_mesh)
{
    if (!s || !node || !node_mesh)
        return -1;
    int w = -1;
    for (uint32_t i = 0; i < s->nweap && i < SOMVR_HP_WEAPONS; i++) {
        if (s->wnode[i] == node)
            return -1; /* a weapon's own */
        if (s->wmesh[i] == node_mesh && (w < 0 || (somvr_hp_match(s, s->wnode[i]) >= 0 && somvr_hp_match(s, s->wnode[w]) < 0)))
            w = (int)i; /* the first, or a hidden one before a shown one */
    }
    return w;
}

int somvr_hp_skip_copy(int eye, int pass, const somvr_hp_set *s, uint64_t rt_frame, uintptr_t node, uintptr_t node_mesh)
{
    if (eye < 0 || pass < 0 || pass >= SOMVR_HP_PASSES || !s || !rt_frame || s->frame != rt_frame || !s->copies)
        return 0;
    int w = somvr_hp_copy_of(s, node, node_mesh);
    return w >= 0 && somvr_hp_match(s, s->wnode[w]) >= 0; /* a copy of a weapon this frame hides */
}

void somvr_hp_ring_init(somvr_hp_ring *r)
{
    for (int i = 0; i < SOMVR_HP_RING; i++) {
        atomic_store_explicit(&r->e[i].frame, 0, memory_order_relaxed);
        atomic_store_explicit(&r->e[i].n, 0, memory_order_relaxed);
        for (int k = 0; k < SOMVR_HP_MAX; k++) {
            atomic_store_explicit(&r->e[i].node[k], 0, memory_order_relaxed);
            atomic_store_explicit(&r->e[i].slot[k], 0, memory_order_relaxed);
        }
        for (int k = 0; k < 3; k++)
            atomic_store_explicit(&r->e[i].at[k], 0, memory_order_relaxed);
        atomic_store_explicit(&r->e[i].pose, 0, memory_order_relaxed);
        atomic_store_explicit(&r->e[i].away, 0, memory_order_relaxed);
        atomic_store_explicit(&r->e[i].nmesh, 0, memory_order_relaxed);
        for (int k = 0; k < SOMVR_HP_MESHES; k++)
            atomic_store_explicit(&r->e[i].mesh[k], 0, memory_order_relaxed);
        atomic_store_explicit(&r->e[i].copies, 0, memory_order_relaxed);
        atomic_store_explicit(&r->e[i].nweap, 0, memory_order_relaxed);
        for (int k = 0; k < SOMVR_HP_WEAPONS; k++) {
            atomic_store_explicit(&r->e[i].wnode[k], 0, memory_order_relaxed);
            atomic_store_explicit(&r->e[i].wmesh[k], 0, memory_order_relaxed);
        }
    }
    atomic_thread_fence(memory_order_release);
}

/* a seqlock per entry: frame = 0 while the fields are written, the frame after (release); the reader checks the frame
 * before and after its copy */
void somvr_hp_publish(somvr_hp_ring *r, const somvr_hp_set *s)
{
    if (!s->frame)
        return;
    __typeof__(r->e[0]) *e = &r->e[s->frame % SOMVR_HP_RING];
    uint32_t n = s->n < SOMVR_HP_MAX ? s->n : SOMVR_HP_MAX;
    atomic_store_explicit(&e->frame, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&e->n, n, memory_order_relaxed);
    for (uint32_t k = 0; k < SOMVR_HP_MAX; k++) {
        atomic_store_explicit(&e->node[k], k < n ? s->node[k] : 0, memory_order_relaxed);
        atomic_store_explicit(&e->slot[k], k < n ? s->slot[k] : 0, memory_order_relaxed);
    }
    for (int k = 0; k < 3; k++) {
        uint32_t b;
        memcpy(&b, &s->at[k], 4);
        atomic_store_explicit(&e->at[k], b, memory_order_relaxed);
    }
    atomic_store_explicit(&e->pose, s->pose, memory_order_relaxed);
    uint32_t nm = s->nmesh < SOMVR_HP_MESHES ? s->nmesh : SOMVR_HP_MESHES;
    atomic_store_explicit(&e->away, s->away, memory_order_relaxed);
    atomic_store_explicit(&e->nmesh, nm, memory_order_relaxed);
    for (uint32_t k = 0; k < SOMVR_HP_MESHES; k++)
        atomic_store_explicit(&e->mesh[k], k < nm ? s->mesh[k] : 0, memory_order_relaxed);
    uint32_t nw = s->nweap < SOMVR_HP_WEAPONS ? s->nweap : SOMVR_HP_WEAPONS;
    atomic_store_explicit(&e->copies, s->copies, memory_order_relaxed);
    atomic_store_explicit(&e->nweap, nw, memory_order_relaxed);
    for (uint32_t k = 0; k < SOMVR_HP_WEAPONS; k++) {
        atomic_store_explicit(&e->wnode[k], k < nw ? s->wnode[k] : 0, memory_order_relaxed);
        atomic_store_explicit(&e->wmesh[k], k < nw ? s->wmesh[k] : 0, memory_order_relaxed);
    }
    atomic_store_explicit(&e->frame, s->frame, memory_order_release);
}

int somvr_hp_lookup(somvr_hp_ring *r, uint64_t frame, somvr_hp_set *out)
{
    if (!frame)
        return 0;
    __typeof__(r->e[0]) *e = &r->e[frame % SOMVR_HP_RING];
    if (atomic_load_explicit(&e->frame, memory_order_acquire) != frame)
        return 0;
    somvr_hp_set s;
    somvr_hp_set_init(&s, frame);
    uint32_t n = atomic_load_explicit(&e->n, memory_order_relaxed);
    s.n = n < SOMVR_HP_MAX ? n : SOMVR_HP_MAX;
    for (uint32_t k = 0; k < s.n; k++) {
        s.node[k] = atomic_load_explicit(&e->node[k], memory_order_relaxed);
        s.slot[k] = atomic_load_explicit(&e->slot[k], memory_order_relaxed);
    }
    for (int k = 0; k < 3; k++) {
        uint32_t b = atomic_load_explicit(&e->at[k], memory_order_relaxed);
        memcpy(&s.at[k], &b, 4);
    }
    s.pose = atomic_load_explicit(&e->pose, memory_order_relaxed);
    s.away = atomic_load_explicit(&e->away, memory_order_relaxed);
    uint32_t nm = atomic_load_explicit(&e->nmesh, memory_order_relaxed);
    s.nmesh = nm < SOMVR_HP_MESHES ? nm : SOMVR_HP_MESHES;
    for (uint32_t k = 0; k < s.nmesh; k++)
        s.mesh[k] = atomic_load_explicit(&e->mesh[k], memory_order_relaxed);
    s.copies = atomic_load_explicit(&e->copies, memory_order_relaxed);
    uint32_t nw = atomic_load_explicit(&e->nweap, memory_order_relaxed);
    s.nweap = nw < SOMVR_HP_WEAPONS ? nw : SOMVR_HP_WEAPONS;
    for (uint32_t k = 0; k < s.nweap; k++) {
        s.wnode[k] = atomic_load_explicit(&e->wnode[k], memory_order_relaxed);
        s.wmesh[k] = atomic_load_explicit(&e->wmesh[k], memory_order_relaxed);
    }
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&e->frame, memory_order_relaxed) != frame)
        return 0;
    *out = s;
    return 1;
}
