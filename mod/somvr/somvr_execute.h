/* somvr_execute.h - SOMVR_EXECUTE=1|log: the execution (the combat finisher) in VR (build 13 candidate; design-vr.md 3
 * "Executions and finishers", the user's D1: VR-native - you do the motion, the orc plays its part, the view is never
 * dragged; docs/re-xr-pipeline.md 30.23). Pure: no globals, no game reads (somvr_execute_game.c feeds it). Tested by
 * tests/test_execute.c.
 *
 * The game's hit counter (Combat+0x1f0; static RE 2026-10-25, re-xr-pipeline.md 30.23): the streak (+0xc4), the
 * execution charge (+0xc6: every change of the streak moves it by the same amount, FUN_02727960), the charge it needs
 * (+0xc8, the character's HitCounterChargeValue: 8, 5 with the skill), the charge's enable (+0xd3), the charged flag
 * (+0xd4, set by FUN_02724fe0 "HitCounter Charge TRUE") and the extra uses (+0xd2, from a stat when the charge fills).
 * The stock attack start spends it (FUN_02728110 +0xaa7, the bytes of XC_SPEND): an attack whose
 * AttackData.ConsumeHitCounterCharge is set takes one use (uses > 1: uses - 1, the charge stays) or the charge (to 0).
 *
 * The streak's lapse (round 2, static RE 2026-10-25, re-xr-pipeline.md 30.23.1): the counter's own timer (+0x38 its start,
 * +0x40 its delay as a double, +0x48 the paused time, +0x58 the clock (its +0x18 = now), +0x60 paused, +0x61 running;
 * the expiry check at FUN_027259c0+0x46, XC_LAPSE's bytes): every positive change of the streak and every
 * ResetHitCounterTimer hit restart it with the game's delay (3.0 s here: the character's stat 0xb, else a record's float);
 * once now - start >= delay the update returns 0 and FUN_02727960 clears the counter (FUN_02727200: the streak and the
 * charge to 0, "charged" off). So a full charge has no timeout of its own: it goes with the streak's.
 *
 * Ready = charged; an execution = a heavy hit (somvr_combat's tier) landed with the right trigger held (the design's
 * "wraith charge", a hysteresis 0.6 on / 0.4 off) while ready. The stock records come in a front and a back family (the
 * contact's side on the orc: more than back_deg off its facing = from behind); successive executions take the next
 * record of the family in turn, as combat's tiers do.
 */
#ifndef SOMVR_EXECUTE_H
#define SOMVR_EXECUTE_H

#include <stdint.h>

/* the hit counter's fields (as read; somvr_execute_game.c) */
typedef struct {
    int valid;           /* the counter was read */
    int16_t streak;      /* +0xc4 */
    int16_t charge;      /* +0xc6 */
    int16_t need;        /* +0xc8: <= 0 = no execution charge for this character */
    uint8_t uses;        /* +0xd2 */
    uint8_t enabled;     /* +0xd3 */
    uint8_t charged;     /* +0xd4 */
} somvr_ex_hc;

/* 1 = an execution is ready: the game's charged flag, or what sets it (enabled, need > 0, charge >= need) */
int somvr_ex_ready(const somvr_ex_hc *h);

/* the stock spend (EX_SPEND's rule) on h: 1 = a use taken (uses - 1, the charge kept), 2 = the charge to 0 (*charge_to
 * = 0: the game's setter does it), 0 = nothing to spend (no need, or the charge already 0) */
int somvr_ex_spend(const somvr_ex_hc *h, int16_t *charge_to, uint8_t *uses_to);

/* the trigger's held state with hysteresis (on >= 0.6, off < 0.4); NaN = released */
int somvr_ex_trigger(int down, float v);

enum { SOMVR_EXJ_NONE = 0, SOMVR_EXJ_EXECUTE, SOMVR_EXJ_NOT_READY, SOMVR_EXJ_NOT_HEAVY, SOMVR_EXJ_NO_TRIGGER };
const char *somvr_ex_judge_name(int j);
/* a contact that would deal a hit of tier (somvr_combat's SC_TIER_*: heavy = 2): EXECUTE when ready, the trigger held
 * and heavy; NOT_READY / NOT_HEAVY when the trigger is held (the player asked: one line); NO_TRIGGER otherwise (an
 * ordinary hit, nothing to say) */
int somvr_ex_judge(int ready, int trigger, int tier);

/* the side of the contact on the orc: 1 = from behind (the angle between the orc's facing and the direction from its
 * root to the contact > back_deg), 0 = from the front. Game space: heading yaw = atan2(x, z) */
int somvr_ex_behind(const float contact[3], const float orc[3], float orc_yaw, float back_deg, float *off_deg);

/* the family's next record: names "a,b,c" (at most 16), turn = the running index; out = the name (empty = none) */
int somvr_ex_pick(const char *names, unsigned turn, char *out, int cap);

/* SOMVR_EXECUTE_LAPSE_S / _READY_S: the delay to write into the running timer, or 0 = leave it. Only while the streak is
 * up (> 0) and the timer runs with a delay of its own (> 0: the game sets 0 at the expiry); the target = ready_s when an
 * execution is ready and ready_s > 0, else lapse_s; it only ever lengthens the game's own delay (a longer one, a skill's,
 * stays), and the game writes its own again at the next hit */
double somvr_ex_lapse(const somvr_ex_hc *h, int running, double delay, float lapse_s, float ready_s);

#endif
