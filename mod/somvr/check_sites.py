"""check_sites.py SOURCE.c BINARY - check somvr's patch sites against the game binary on disk (read-only).

Reads the SITE(...) lines of every SITES-BEGIN ... SITES-END block in SOURCE.c and, for each, compares the expected bytes
with the binary's file contents (addresses mapped through the PT_LOAD program headers): the context bytes, the call
target (K_CALL: an E8 rel32; K_JMP: an E9 rel32 tail jump), slot value (K_VSLOT) or function entry (K_ENTRY: a function
somvr only calls, addr == target, the context = its first bytes), and the first bytes of the target. Also checks the
GNU build-id. The SOMVR_POINTER block (POINTER_SITES_BEGIN ... POINTER_SITES_END, docs/re-ui-pointer.md 6) is read and
counted on its own (7 sites: LP_GUARD_HOVER/_CLICK and the five functions it calls) and checked the same way, as is
the SOMVR_FALLBACK block (FALLBACK_SITES_BEGIN ... FALLBACK_SITES_END: 17 read-only sites, re-xr-pipeline.md 6.8),
the SOMVR_COMBAT_REACT block (REACT_SITES_BEGIN ... REACT_SITES_END: the 4 K_ENTRY functions it calls and the
read-only K_CALL RX_ATK_GATE, 6.3.1) and
the SOMVR_CLIMB block (CLIMB_SITES_BEGIN ... CLIMB_SITES_END: 10 read-only sites, re-xr-pipeline.md 12 and 50.1; and its
CLEDGE_SITES_BEGIN ... CLEDGE_SITES_END block, SOMVR_CLIMB_LEDGES' patched K_VSLOT CL_CTXACT, 32.7) and the
SOMVR_BOW block (BOW_SITES_BEGIN ... BOW_SITES_END: the free-aim call site BOW_FA_CAMXF, 2 read-only K_ENTRY
sites of the arrow counter and, since P9, the 6 read-only sites of our own shot, re-xr-pipeline.md 13; build 11's
called K_ENTRY BOW_ACTIVATE, the weapon-mode component calls BOW_MAT_*, BOW_SLOW_*, BOW_FX_* and BOW_FXALT_*,
re-bow.md 12) and the
SOMVR_LOCO / SOMVR_ANCHOR block (LOCO_SITES_BEGIN ... LOCO_SITES_END: the patched call LOCO_ROOTDELTA, the read-only
call LOCO_ATTACK_Q and 9 read-only K_ENTRY sites, re-xr-pipeline.md 17) and the SOMVR_THREAT block
(THREAT_SITES_BEGIN ... THREAT_SITES_END: the key-time, target and attack proofs, GetNode, the projectile slot and
its position proof, the 3 sound functions, re-xr-pipeline.md 18), the SOMVR_BODYMOMENTS block
(BODYMOMENTS_SITES_BEGIN ... BODYMOMENTS_SITES_END: 3 read-only K_ENTRY proofs of Talion's attack, combat state and
dead flag, re-xr-pipeline.md 20) and the SOMVR_SLOTS block (SLOTS_SITES_BEGIN ...
SLOTS_SITES_END: the patched tail jump SL_SHEATH_JMP, the called SL_SHEATHE / SL_REATTACH / SL_ACTIVATE, the read-only
SL_REATTACH_NODE call, SL_ITEM, SL_INVENTORY and the 3 sound functions, re-xr-pipeline.md 21) and the SOMVR_DEFENCE
block (DEFENCE_SITES_BEGIN ... DEFENCE_SITES_END: the patched call DF_IMPACT, the read-only window and type proofs, the
recoil and AttackData functions, re-xr-pipeline.md 22) and the SOMVR_STEP / SOMVR_DASH / SOMVR_PHASE block of
somvr_step_game.c (the file SOURCE.c includes; STEP_SITES_BEGIN ... STEP_SITES_END: 6 read-only sites, the collision
and vision proofs, re-xr-pipeline.md 23) and the SOMVR_FINISH block (FINISH_SITES_BEGIN ... FINISH_SITES_END in
somvr_finish_game.c, re-xr-pipeline.md 24: 18 read-only K_ENTRY sites and the object system's K_VSLOT) and the
SOMVR_HIDE_HEAD=parts block of somvr_headparts_game.c (HEADPARTS_SITES_BEGIN ... HEADPARTS_SITES_END, re-xr-pipeline.md
26: the five patched FUN_0081ff80 calls HP_DRAW_*, the read-only slot proofs and the shadow submitter; 26.7: the cloth's
eight patched FUN_007ccec0 calls HP_CUST_* and its ten read-only proofs) and the
SOMVR_PRACTICE block of somvr_practice_game.c (PRACTICE_SITES_BEGIN ... PRACTICE_SITES_END, re-xr-pipeline.md 27: 6
read-only K_ENTRY functions and 4 read-only K_VSLOT slots; and its PRQUIET_SITES_BEGIN ... PRQUIET_SITES_END block, the 4
read-only K_ENTRY proofs of SOMVR_PRACTICE_QUIET's spawn manager calls; its PRNAV block; build 17's PRFILL_SITES_BEGIN ...
PRFILL_SITES_END, the 3 K_ENTRY calls of the practice page's refills) and the SOMVR_BODY=hands block of somvr_body_game.c
(HANDS_SITES_BEGIN ... HANDS_SITES_END, re-xr-pipeline.md 28: the patched K_VSLOT BD_REND_COPY and 3 read-only K_ENTRY
proofs) and the SOMVR_MINIMAP=head block of somvr_minimap_game.c (MINIMAP_SITES_BEGIN ... MINIMAP_SITES_END,
re-xr-pipeline.md 28.2: the patched call MM_CAMQ, the read-only option proof MM_OPT and the called MM_OPTIONS) and the
SOMVR_SKIP_SPLASH block of somvr_session_game.c (SESSION_SITES_BEGIN ... SESSION_SITES_END, re-launcher.md 8: the
patched call SS_SPLASH and the read-only no-splash test SS_SPLASH_FLAG) and the SOMVR_MUSIC / SOMVR_HINTS block of
somvr_gameopts_game.c (GAMEOPTS_SITES_BEGIN ... GAMEOPTS_SITES_END, re-xr-pipeline.md 34: the sound manager's patched
slot GO_RTPC, the HUD text delegates' patched slots GO_HINT / GO_TUTORIAL / GO_PROMPT, the read-only GO_OPTIONS,
GO_APPLY and GO_RECNAME) and the SOMVR_PAD_PROMPTS block of somvr_prompts_game.c (PADPROMPTS_SITES_BEGIN ...
PADPROMPTS_SITES_END, re-bow.md 13: the HUD prompt delegates' patched slots PP_SHOW / PP_HIDE / PP_HOLD) and the
SOMVR_BOW_HITLOG block of somvr_bowhit_game.c (BOWHIT_SITES_BEGIN ... BOWHIT_SITES_END: the patched call BOW_PJ_HIT) and
the SOMVR_EXECUTE block of somvr_execute_game.c (EXECUTE_SITES_BEGIN ... EXECUTE_SITES_END: 5 read-only K_ENTRY proofs of
the hit counter's charge, its stock spend and its clear, the game's sever and the counter timer's expiry,
re-xr-pipeline.md 30.23) and the SOMVR_HUD_FX block of somvr_hudfx_game.c (HUDFX_SITES_BEGIN ... HUDFX_SITES_END: the
patched tail jump UF_CREATE of CBaseUI::CreateUIFXGroup, re-xr-pipeline.md 31.5) and the SOMVR_VERBS_GLOW block of
somvr_vbglow_game.c (VBGLOW_SITES_BEGIN ... VBGLOW_SITES_END: the patched tail jumps VG_FXC / VG_FXC1 of ClientFX's two
create thunks, re-xr-pipeline.md 39.7) and the SOMVR_FRONT block of somvr_front_game.c (FRONT_SITES_BEGIN ...
FRONT_SITES_END: the patched calls FR_SCORE, FR_GRANT, FR_CAMCOPY_A/B, FR_DMG_ATK and the read-only FR_CAMTERM, FR_VF59,
FR_REARM, FR_DMG_ALL, re-xr-pipeline.md 42.8) and the SOMVR_OBJECTIVE block of somvr_objective_game.c
(OBJECTIVE_SITES_BEGIN ... OBJECTIVE_SITES_END: the patched call OB_EACH of the nav markers' placement, the called
OB_POS and OB_RANGE, re-xr-pipeline.md 44) and the SOMVR_GLYPH block of somvr_collect_game.c (COLLECT_SITES_BEGIN ...
COLLECT_SITES_END: LookAtTrigger's patched tick slot CO_LAT_TICK, the patched call CO_VLOOK of the vision-mode
look-at, the called CO_LAT_SEE and CO_LAT_VIEW, the read-only proof CO_OBJ_PROPS, re-xr-pipeline.md 45.9; and
SOMVR_TEXT_BUTTONS' patched call CO_TXT_ICON of the UI text's button icons, 45.10). Files
named by an #include "somvr_*_game.c" line in SOURCE.c (somvr_step_game.c, somvr_finish_game.c, somvr_headparts_game.c,
somvr_practice_game.c, somvr_body_game.c, somvr_minimap_game.c, somvr_session_game.c, somvr_gameopts_game.c,
somvr_prompts_game.c, somvr_bowhit_game.c, somvr_render_game.c) are read with it. The SOMVR_FLATFX block of
somvr_render_game.c (RENDER_SITES_BEGIN ... RENDER_SITES_END, re-xr-pipeline.md 86: the patched lens-flare slot, the
sun shafts' and the lens drops' patched calls, SOMVR_FX_TEST's four patched particle/sprite draw slots) is counted on
its own.
Exit 0 = all sites match.
"""
import os
import re
import struct
import sys

BUILDID = "87ec5d6fd94afd47a24d5da7c9b0c2b74b30c6e9"


def loads(data):
    if data[:4] != b"\x7fELF" or data[4] != 2:
        sys.exit("not an ELF64 file")
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    segs = []
    for i in range(phnum):
        p_type, _flags, off, vaddr, _paddr, filesz, _memsz, _align = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * phentsize)
        segs.append((p_type, off, vaddr, filesz))
    return segs


def read(data, segs, addr, n):
    for p_type, off, vaddr, filesz in segs:
        if p_type == 1 and vaddr <= addr and addr + n <= vaddr + filesz:
            return data[off + addr - vaddr: off + addr - vaddr + n]
    return None


def build_id(data, segs):
    for p_type, off, _vaddr, filesz in segs:
        if p_type != 4:
            continue
        p, end = off, off + filesz
        while p + 12 <= end:
            namesz, descsz, ntype = struct.unpack_from("<III", data, p)
            name = data[p + 12: p + 12 + namesz]
            desc_at = p + 12 + ((namesz + 3) & ~3)
            if ntype == 3 and name == b"GNU\0":
                return data[desc_at: desc_at + descsz].hex()
            p = desc_at + ((descsz + 3) & ~3)
    return None


def main():
    src, binpath = sys.argv[1], sys.argv[2]
    text = open(src, encoding="utf-8").read()
    for inc in re.findall(r'^#include "(somvr_\w+_game\.c)"', text, re.M):  # its included game-side parts
        text += "\n" + open(os.path.join(os.path.dirname(os.path.abspath(src)), inc), encoding="utf-8").read()
    blocks = [b.split("SITES-END", 1)[0] for b in text.split("SITES-BEGIN")[1:]]
    sites = [m for b in blocks for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b)]
    if len(sites) != 74:
        sys.exit("expected 74 SITE lines (16 hooks H1 H2 H3 H4 H6 H9 H8 AUDIO PM IN_TICK IN_VALUE IN_VALUE2 IN_ISON RUM1 "
                 "RUM2 ARM_TRK_EVAL, 4 save-guard slots, 7 read-only D3D/Vulkan slots, the read-only G12_NODEXF slot, 10 "
                 "read-only CB_* combat sites, 5 read-only IN_* input sites, the 10 read-only GL_* pad-glyph sites, the "
                 "10 EX hooks RES_*, HEAD_SHADOW_*, HUD_PASS_EYE, HUD_PASS_COVER and UI_PRESENT_COMPOSITE, the read-only "
                 "ES_RUNSTRING, the 9 read-only head-hiding sites G12_HIDEPIECE..HEAD_NODE_SETMASK, the read-only "
                 "ARM_MODELOF), found %d" % len(sites))
    ptr = [m for b in text.split("POINTER_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("POINTER_SITES_END", 1)[0])]
    if len(ptr) != 7:
        sys.exit("expected 7 SITE lines in the POINTER_SITES block (LP_GUARD_HOVER, LP_GUARD_CLICK, LP_IM_DOWN, LP_IM_UP, "
                 "LP_IM_MOVE, LP_LOCALPLAYER, LP_IFS_FRONTEND), found %d" % len(ptr))
    fb = [m for b in text.split("FALLBACK_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("FALLBACK_SITES_END", 1)[0])]
    if len(fb) != 17:
        sys.exit("expected 17 SITE lines in the FALLBACK_SITES block (SOMVR_FALLBACK's read-only proofs FB_LIFE .. "
                 "FB_ATTACH), found %d" % len(fb))
    rx = [m for b in text.split("REACT_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("REACT_SITES_END", 1)[0])]
    if len(rx) != 5 or [m[1] for m in rx] != ["K_ENTRY"] * 4 + ["K_CALL"]:
        sys.exit("expected 5 SITE lines in the REACT_SITES block (RX_CHARREC, RX_ROLL, RX_NOTIFY, RX_BLOCKING K_ENTRY, "
                 "the read-only RX_ATK_GATE K_CALL), found %d" % len(rx))
    cl = [m for b in text.split("CLIMB_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("CLIMB_SITES_END", 1)[0])]
    if len(cl) != 10:
        sys.exit("expected 10 SITE lines in the CLIMB_SITES block (SOMVR_CLIMB's read-only CL_RAYCAST .. CL_PC_GETPOS), "
                 "found %d" % len(cl))
    cle = [m for b in text.split("CLEDGE_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("CLEDGE_SITES_END", 1)[0])]
    if len(cle) != 1 or cle[0][1] != "K_VSLOT":
        sys.exit("expected 1 SITE line in the CLEDGE_SITES block (SOMVR_CLIMB_LEDGES' patched CL_CTXACT K_VSLOT), "
                 "found %d" % len(cle))
    bw = [m for b in text.split("BOW_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("BOW_SITES_END", 1)[0])]
    if len(bw) != 18 or [m[1] for m in bw] != ["K_CALL", "K_ENTRY", "K_ENTRY", "K_ENTRY", "K_CALL", "K_ENTRY", "K_ENTRY",
                                               "K_ENTRY", "K_ENTRY", "K_ENTRY"] + ["K_CALL"] * 8:
        sys.exit("expected 18 SITE lines in the BOW_SITES block (BOW_FA_CAMXF K_CALL, BOW_AMMO and BOW_AMMO_RES K_ENTRY, "
                 "our own shot's BOW_FIRE K_ENTRY, BOW_FIRE_ITEM K_CALL, BOW_ITEM, BOW_INVENTORY, BOW_AIM_OBJ, "
                 "BOW_TOKENS_ADD and BOW_ACTIVATE K_ENTRY, the weapon-mode component calls BOW_MAT_*, BOW_SLOW_*, "
                 "BOW_FX_* and BOW_FXALT_* K_CALL), found %d" % len(bw))
    lo = [m for b in text.split("LOCO_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("LOCO_SITES_END", 1)[0])]
    if len(lo) != 11 or [m[1] for m in lo[:2]] != ["K_CALL", "K_CALL"] or any(m[1] != "K_ENTRY" for m in lo[2:]):
        sys.exit("expected 11 SITE lines in the LOCO_SITES block (LOCO_ROOTDELTA and LOCO_ATTACK_Q K_CALL, then 9 K_ENTRY "
                 "LOCO_COMBAT_OWNS .. LOCO_OBJTIMER), found %d" % len(lo))
    th = [m for b in text.split("THREAT_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("THREAT_SITES_END", 1)[0])]
    if len(th) != 10 or [m[1] for m in th] != ["K_CALL", "K_CALL", "K_CALL", "K_ENTRY", "K_ENTRY", "K_VSLOT", "K_CALL",
                                              "K_ENTRY", "K_ENTRY", "K_ENTRY"]:
        sys.exit("expected 10 SITE lines in the THREAT_SITES block (the read-only TH_ATK_START, TH_WS, TH_KEYTABLE "
                 "K_CALL, TH_CURATK, TH_GETNODE K_ENTRY, the TH_PJ_UPDATE slot, the read-only TH_PJ_MOVE K_CALL, "
                 "TH_SND_REGOBJ, TH_SND_SETPOS, TH_SND_POST K_ENTRY), found %d" % len(th))
    bm = [m for b in text.split("BODYMOMENTS_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("BODYMOMENTS_SITES_END", 1)[0])]
    if len(bm) != 3 or any(m[1] != "K_ENTRY" for m in bm):
        sys.exit("expected 3 K_ENTRY SITE lines in the BODYMOMENTS_SITES block (SOMVR_BODYMOMENTS' read-only BM_CURATK, "
                 "BM_STATE, BM_DEAD), found %d" % len(bm))
    sl = [m for b in text.split("SLOTS_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("SLOTS_SITES_END", 1)[0])]
    if len(sl) != 10 or [m[1] for m in sl] != ["K_JMP", "K_ENTRY", "K_ENTRY", "K_CALL", "K_ENTRY", "K_ENTRY", "K_ENTRY",
                                              "K_ENTRY", "K_ENTRY", "K_ENTRY"]:
        sys.exit("expected 10 SITE lines in the SLOTS_SITES block (SL_SHEATH_JMP K_JMP, SL_SHEATHE, SL_REATTACH K_ENTRY, "
                 "the read-only SL_REATTACH_NODE K_CALL, SL_ACTIVATE, SL_ITEM, SL_INVENTORY, SL_SND_REGOBJ, SL_SND_SETPOS, "
                 "SL_SND_POST K_ENTRY), found %d" % len(sl))
    df = [m for b in text.split("DEFENCE_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("DEFENCE_SITES_END", 1)[0])]
    if len(df) != 5 or [m[1] for m in df] != ["K_CALL"] + ["K_ENTRY"] * 4:
        sys.exit("expected 5 SITE lines in the DEFENCE_SITES block (DF_IMPACT K_CALL, then DF_WINDOW, DF_TYPE, DF_RECOIL, "
                 "DF_ATTACKDATA K_ENTRY), found %d" % len(df))
    # SOMVR_STEP / _DASH / _PHASE: their block lives in somvr_step_game.c, which SOURCE.c includes (read with it above)
    st = []
    if re.search(r'^#include "somvr_step_game\.c"', text, re.M):
        st = [m for b in text.split("STEP_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("STEP_SITES_END", 1)[0])]
        if len(st) != 6 or [m[1] for m in st] != ["K_ENTRY"] * 4 + ["K_CALL", "K_ENTRY"]:
            sys.exit("expected 6 SITE lines in somvr_step_game.c's STEP_SITES block (the read-only ST_COL_SOLVER, "
                     "ST_COL_MOVE7, ST_COL_OFF, ST_COL_DEFAULT K_ENTRY, ST_VISION_GET K_CALL, ST_VISION_ON K_ENTRY), "
                     "found %d" % len(st))
    fi = [m for b in text.split("FINISH_SITES_BEGIN")[1:] for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("FINISH_SITES_END", 1)[0])]
    if len(fi) != 19 or [m[1] for m in fi] != ["K_ENTRY"] * 16 + ["K_VSLOT"] + ["K_ENTRY"] * 2:
        sys.exit("expected 19 SITE lines in the FINISH_SITES block (FI_HITCOUNT .. FI_FX_RELEASE K_ENTRY, the FI_OBJSYS_MH "
                 "slot, FI_ITEM and FI_KBMNODE K_ENTRY), found %d" % len(fi))
    # SOMVR_HIDE_HEAD=parts: its block lives in somvr_headparts_game.c, which SOURCE.c includes (read with it above)
    hp = []
    if re.search(r'^#include "somvr_headparts_game\.c"', text, re.M):
        hp = [m for b in text.split("HEADPARTS_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("HEADPARTS_SITES_END", 1)[0])]
        kinds = ["K_CALL"] * 5 + ["K_VSLOT"] + ["K_ENTRY"] * 5 + ["K_CALL"] * 8 + ["K_ENTRY"] * 5 + ["K_VSLOT"] * 5
        if len(hp) != 29 or [m[1] for m in hp] != kinds:
            sys.exit("expected 29 SITE lines in somvr_headparts_game.c's HEADPARTS_SITES block (the patched HP_DRAW_DEEP, "
                     "_FWD, _LATE, _GBUF, _VEL K_CALL, the read-only HP_SLOT_SET K_VSLOT, HP_SLOT_STORE, HP_HEAD_SLOT, "
                     "HP_HELM_SLOT, HP_HAIR_SLOT, HP_SUBMIT_SHADOW K_ENTRY; the cloth's patched HP_CUST_DEEP .. _VEL_B "
                     "K_CALL, the read-only HP_CLOTH_STORE, HP_CLOTH_SET, HP_CLOTH_ENTRY, HP_CLOTH_HANDLE, HP_CUST_INLINE "
                     "K_ENTRY and HP_CLOTH_VT, HP_VIS_VT, HP_CHANDLE_VT, HP_CNODE_VT, HP_CUST_CREATE K_VSLOT), found %d"
                     % len(hp))
    # SOMVR_PRACTICE: its block lives in somvr_practice_game.c, which SOURCE.c includes (read with it above)
    pr = []
    if re.search(r'^#include "somvr_practice_game\.c"', text, re.M):
        pr = [m for b in text.split("PRACTICE_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("PRACTICE_SITES_END", 1)[0])]
        if len(pr) != 10 or [m[1] for m in pr] != ["K_ENTRY"] * 6 + ["K_VSLOT"] * 4:
            sys.exit("expected 10 SITE lines in somvr_practice_game.c's PRACTICE_SITES block (the read-only PR_TMPL_BYNAME, "
                     "PR_SPAWN_AI, PR_ZONE_AT, PR_SETLEVEL, PR_SETMINHEALTH, PR_TOD_NAME K_ENTRY, PR_RAYCAST, PR_NPC_SETZONE, "
                     "PR_OBJSYS_REMOVE, PR_WEATHER K_VSLOT), found %d" % len(pr))
        # SOMVR_PRACTICE_QUIET (build 6): the spawn manager's Enable / Disable / RemoveAllAmbientAI and its flag's store
        prq = [m for b in text.split("PRQUIET_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("PRQUIET_SITES_END", 1)[0])]
        if len(prq) != 4 or [m[1] for m in prq] != ["K_ENTRY"] * 4:
            sys.exit("expected 4 K_ENTRY SITE lines in somvr_practice_game.c's PRQUIET_SITES block (the read-only "
                     "PRQ_ENABLE, PRQ_DISABLE, PRQ_REMOVEALL, PRQ_FLAG_ON), found %d" % len(prq))
        # the story drill's round 2: "practice status nav" (the nav markers' places): the probe's F_NAV_POS
        prn = [m for b in text.split("PRNAV_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("PRNAV_SITES_END", 1)[0])]
        if len(prn) != 1 or [m[1] for m in prn] != ["K_ENTRY"]:
            sys.exit("expected 1 K_ENTRY SITE line in somvr_practice_game.c's PRNAV_SITES block (the read-only "
                     "PRN_NAV_POS), found %d" % len(prn))
        # build 17: the practice page's refills (the arrows' tokens add, the energy's fill, the heal)
        prf = [m for b in text.split("PRFILL_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("PRFILL_SITES_END", 1)[0])]
        if len(prf) != 3 or [m[1] for m in prf] != ["K_ENTRY"] * 3:
            sys.exit("expected 3 K_ENTRY SITE lines in somvr_practice_game.c's PRFILL_SITES block (the read-only "
                     "PRF_TOKENS_ADD, PRF_ENERGY_FULL, PRF_HEAL), found %d" % len(prf))
        pr = pr + prq + prn + prf
    bd = []
    if re.search(r'^#include "somvr_body_game\.c"', text, re.M):
        bd = [m for b in text.split("HANDS_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("HANDS_SITES_END", 1)[0])]
        if len(bd) != 4 or [m[1] for m in bd] != ["K_VSLOT"] + ["K_ENTRY"] * 3:
            sys.exit("expected 4 SITE lines in somvr_body_game.c's HANDS_SITES block (the patched BD_REND_COPY K_VSLOT, the "
                     "read-only BD_LISTEN_CALL, BD_COPY_SRC, BD_COPY_LOOP K_ENTRY), found %d" % len(bd))
    # SOMVR_MINIMAP=head: its block lives in somvr_minimap_game.c, which SOURCE.c includes (read with it above)
    mm = []
    if re.search(r'^#include "somvr_minimap_game\.c"', text, re.M):
        mm = [m for b in text.split("MINIMAP_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("MINIMAP_SITES_END", 1)[0])]
        if len(mm) != 3 or [m[1] for m in mm] != ["K_CALL", "K_CALL", "K_ENTRY"]:
            sys.exit("expected 3 SITE lines in somvr_minimap_game.c's MINIMAP_SITES block (the patched MM_CAMQ and the "
                     "read-only MM_OPT K_CALL, MM_OPTIONS K_ENTRY), found %d" % len(mm))
    # SOMVR_SKIP_SPLASH (build 6): its block lives in somvr_session_game.c (read with SOURCE.c above)
    ssp = []
    if re.search(r'^#include "somvr_session_game\.c"', text, re.M):
        ssp = [m for b in text.split("SESSION_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("SESSION_SITES_END", 1)[0])]
        if len(ssp) != 2 or [m[1] for m in ssp] != ["K_CALL", "K_ENTRY"]:
            sys.exit("expected 2 SITE lines in somvr_session_game.c's SESSION_SITES block (the patched SS_SPLASH K_CALL, "
                     "the read-only SS_SPLASH_FLAG K_ENTRY), found %d" % len(ssp))
    # SOMVR_MUSIC / SOMVR_HINTS (build 10): its block lives in somvr_gameopts_game.c (read with SOURCE.c above)
    go = []
    if re.search(r'^#include "somvr_gameopts_game\.c"', text, re.M):
        go = [m for b in text.split("GAMEOPTS_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("GAMEOPTS_SITES_END", 1)[0])]
        if len(go) != 7 or [m[1] for m in go] != ["K_VSLOT", "K_ENTRY", "K_ENTRY", "K_VSLOT", "K_VSLOT", "K_VSLOT", "K_ENTRY"]:
            sys.exit("expected 7 SITE lines in somvr_gameopts_game.c's GAMEOPTS_SITES block (the patched GO_RTPC K_VSLOT, the "
                     "read-only GO_OPTIONS, GO_APPLY K_ENTRY, the patched GO_HINT, GO_TUTORIAL, GO_PROMPT K_VSLOT, the "
                     "read-only GO_RECNAME K_ENTRY), found %d" % len(go))
    # SOMVR_PAD_PROMPTS (the bow's round 2): its block lives in somvr_prompts_game.c (read with SOURCE.c above)
    pp = []
    if re.search(r'^#include "somvr_prompts_game\.c"', text, re.M):
        pp = [m for b in text.split("PADPROMPTS_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("PADPROMPTS_SITES_END", 1)[0])]
        if len(pp) != 3 or [m[1] for m in pp] != ["K_VSLOT"] * 3:
            sys.exit("expected 3 SITE lines in somvr_prompts_game.c's PADPROMPTS_SITES block (the patched PP_SHOW, PP_HIDE, "
                     "PP_HOLD K_VSLOT), found %d" % len(pp))
    # SOMVR_BOW_HITLOG (the bow's round 2): its block lives in somvr_bowhit_game.c (read with SOURCE.c above)
    bh = []
    if re.search(r'^#include "somvr_bowhit_game\.c"', text, re.M):
        bh = [m for b in text.split("BOWHIT_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("BOWHIT_SITES_END", 1)[0])]
        if len(bh) != 1 or bh[0][1] != "K_CALL":
            sys.exit("expected 1 SITE line in somvr_bowhit_game.c's BOWHIT_SITES block (the patched BOW_PJ_HIT K_CALL), "
                     "found %d" % len(bh))
    # SOMVR_EXECUTE: its block lives in somvr_execute_game.c (read with SOURCE.c above)
    xc = []
    if re.search(r'^#include "somvr_execute_game\.c"', text, re.M):
        xc = [m for b in text.split("EXECUTE_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("EXECUTE_SITES_END", 1)[0])]
        if len(xc) != 5 or [m[1] for m in xc] != ["K_ENTRY"] * 5:
            sys.exit("expected 5 read-only K_ENTRY SITE lines in somvr_execute_game.c's EXECUTE_SITES block (XC_CHARGE, "
                     "XC_SPEND, XC_CLEAR, XC_SEVER, XC_LAPSE), found %d" % len(xc))
    sites = sites + ptr + fb + rx + cl + cle + bw + lo + th + bm + sl + df + st + fi + hp + pr + bd + mm + ssp + go + pp + bh + xc
    # SOMVR_HUD_FX (the build 12 HUD round): its block lives in somvr_hudfx_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_hudfx_game\.c"', text, re.M):
        uf = [m for b in text.split("HUDFX_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("HUDFX_SITES_END", 1)[0])]
        if len(uf) != 1 or uf[0][1] != "K_JMP":
            sys.exit("expected 1 SITE line in somvr_hudfx_game.c's HUDFX_SITES block (the patched UF_CREATE K_JMP), "
                     "found %d" % len(uf))
        sites = sites + uf
    # SOMVR_PARRY (build 18): its block lives in somvr_parry_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_parry_game\.c"', text, re.M):
        py = [m for b in text.split("PARRY_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("PARRY_SITES_END", 1)[0])]
        if len(py) != 1 or py[0][1] != "K_ENTRY":
            sys.exit("expected 1 read-only K_ENTRY SITE line in somvr_parry_game.c's PARRY_SITES block (PY_STREAK), "
                     "found %d" % len(py))
        sites = sites + py
    # SOMVR_BOW_SNDLOG (build 16): its block lives in somvr_quiver_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_quiver_game\.c"', text, re.M):
        bs = [m for b in text.split("BOWSND_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("BOWSND_SITES_END", 1)[0])]
        if len(bs) != 2 or [m[1] for m in bs] != ["K_CALL"] * 2:
            sys.exit("expected 2 SITE lines in somvr_quiver_game.c's BOWSND_SITES block (the patched BS_POST_A, BS_POST_B "
                     "K_CALLs), found %d" % len(bs))
        sites = sites + bs
    # SOMVR_VERBS_GLOW (build 13): its block lives in somvr_vbglow_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_vbglow_game\.c"', text, re.M):
        vg = [m for b in text.split("VBGLOW_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("VBGLOW_SITES_END", 1)[0])]
        if len(vg) != 2 or [m[1] for m in vg] != ["K_JMP"] * 2:
            sys.exit("expected 2 SITE lines in somvr_vbglow_game.c's VBGLOW_SITES block (the patched VG_FXC, VG_FXC1 "
                     "K_JMPs), found %d" % len(vg))
        sites = sites + vg
    # SOMVR_FRONT (build 14): its block lives in somvr_front_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_front_game\.c"', text, re.M):
        ff = [m for b in text.split("FRONT_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("FRONT_SITES_END", 1)[0])]
        want = ["K_CALL", "K_CALL", "K_CALL", "K_CALL", "K_CALL", "K_VSLOT", "K_ENTRY", "K_JMP", "K_CALL"]
        if [m[1] for m in ff] != want:
            sys.exit("expected 9 SITE lines in somvr_front_game.c's FRONT_SITES block (the patched FR_SCORE, FR_GRANT, "
                     "FR_CAMCOPY_A/B, FR_DMG_ATK; the read-only FR_CAMTERM, FR_VF59, FR_REARM, FR_DMG_ALL), found %d" % len(ff))
        sites = sites + ff
    # SOMVR_OBJECTIVE (build 15): its block lives in somvr_objective_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_objective_game\.c"', text, re.M):
        ob = [m for b in text.split("OBJECTIVE_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("OBJECTIVE_SITES_END", 1)[0])]
        if [m[1] for m in ob] != ["K_CALL", "K_ENTRY", "K_ENTRY"]:
            sys.exit("expected 3 SITE lines in somvr_objective_game.c's OBJECTIVE_SITES block (the patched OB_EACH K_CALL, "
                     "the called OB_POS, OB_RANGE K_ENTRY), found %d" % len(ob))
        sites = sites + ob
    # SOMVR_FIGHT_SPACE_CM (build 16): its block lives in somvr_fight_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_fight_game\.c"', text, re.M):
        sp = [m for b in text.split("SPACE_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("SPACE_SITES_END", 1)[0])]
        if [m[1] for m in sp] != ["K_VSLOT"]:
            sys.exit("expected 1 SITE line in somvr_fight_game.c's SPACE_SITES block (the patched FS_AIMOVE K_VSLOT), "
                     "found %d" % len(sp))
        sites = sites + sp
    # SOMVR_INSPECT / SOMVR_GLYPH (build 17): its block lives in somvr_collect_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_collect_game\.c"', text, re.M):
        co = [m for b in text.split("COLLECT_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("COLLECT_SITES_END", 1)[0])]
        if [m[1] for m in co] != ["K_VSLOT", "K_CALL", "K_ENTRY", "K_ENTRY", "K_VSLOT", "K_CALL"] + ["K_CALL"] * 8 + ["K_ENTRY"]:
            sys.exit("expected 15 SITE lines in somvr_collect_game.c's COLLECT_SITES block (the patched CO_LAT_TICK K_VSLOT "
                     "and CO_VLOOK K_CALL, the called CO_LAT_SEE, CO_LAT_VIEW K_ENTRY, the read-only CO_OBJ_PROPS K_VSLOT, "
                     "SOMVR_TEXT_BUTTONS' patched CO_TXT_ICON K_CALL, SOMVR_GLYPH=head's CO_TGEOM_A/B and CO_TSCORE_1..6 "
                     "K_CALL, the called CO_COLLECT K_ENTRY), found %d" % len(co))
        sites = sites + co
    # SOMVR_BOW_RETICLE (build 17): its block lives in somvr_reticle_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_reticle_game\.c"', text, re.M):
        xh = [m for b in text.split("RETICLE_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("RETICLE_SITES_END", 1)[0])]
        if [m[1] for m in xh] != ["K_VSLOT"]:
            sys.exit("expected 1 SITE line in somvr_reticle_game.c's RETICLE_SITES block (the patched XH_SET K_VSLOT), "
                     "found %d" % len(xh))
        sites = sites + xh
    # SOMVR_COUNTER_ICON (build 18): its block lives in somvr_cprompt_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_cprompt_game\.c"', text, re.M):
        cp = [m for b in text.split("CPROMPT_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("CPROMPT_SITES_END", 1)[0])]
        if [m[1] for m in cp] != ["K_CALL", "K_CALL"]:
            sys.exit("expected 2 SITE lines in somvr_cprompt_game.c's CPROMPT_SITES block (the patched CP_FX and CP_MARK "
                     "K_CALLs), found %d" % len(cp))
        sites = sites + cp
    # SOMVR_FLATFX / SOMVR_FX_TEST (build 21): its block lives in somvr_render_game.c (read with SOURCE.c above)
    if re.search(r'^#include "somvr_render_game\.c"', text, re.M):
        rd = [m for b in text.split("RENDER_SITES_BEGIN")[1:] for m in re.findall(
            r'SITE\("(\w+)", (K_CALL|K_JMP|K_VSLOT|K_ENTRY), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
            r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b.split("RENDER_SITES_END", 1)[0])]
        if [m[1] for m in rd] != ["K_VSLOT", "K_CALL", "K_CALL"] + ["K_VSLOT"] * 6 + ["K_ENTRY"] * 2:
            sys.exit("expected 11 SITE lines in somvr_render_game.c's RENDER_SITES block (the patched RD_LENSFLARE K_VSLOT, "
                     "RD_SHAFTS and RD_RAIN K_CALLs, SOMVR_FX_TEST's RD_PSYS, RD_GPUPSYS, RD_SPRITE, RD_FLARESPR, "
                     "RD_PSYS_UPD, RD_PSYS_PREP K_VSLOTs, the read-only RD_VIS_INIT and RD_VIS_REQ K_ENTRYs), "
                     "found %d" % len(rd))
        sites = sites + rd
    data = open(binpath, "rb").read()
    segs = loads(data)
    bid = build_id(data, segs)
    bad = 0
    print("binary %s build-id %s %s" % (binpath, bid, "ok" if bid == BUILDID else "MISMATCH"))
    bad += bid != BUILDID
    for name, kind, addr, ctx, ctx_hex, target, pro_hex in sites:
        addr, ctx, target = int(addr, 16), int(ctx, 16), int(target, 16)
        exp, pro = bytes.fromhex(ctx_hex), bytes.fromhex(pro_hex)
        problems = []
        live = read(data, segs, ctx, len(exp))
        if live != exp:
            problems.append("context bytes differ: %s" % (live.hex(" ") if live else "unmapped"))
        rel32 = kind in ("K_CALL", "K_JMP")
        if addr < ctx or addr - ctx + (5 if rel32 else 8) > len(exp) or not pro:
            # somvr.c's check_site refuses such a spec at runtime ("bad site spec"): the context must cover the site
            problems.append("bad site spec: the context bytes must cover the site (%d bytes from %#x, at least %d)" % (
                len(exp), ctx, addr - ctx + (5 if rel32 else 8)))
        site = read(data, segs, addr, 5 if rel32 else 8)
        if kind == "K_ENTRY":
            if addr != target or ctx != addr:
                problems.append("entry %#x: want addr == ctx == target %#x" % (addr, target))
        elif site is None:
            problems.append("site unmapped")
        elif rel32:
            rel, = struct.unpack("<i", site[1:5])
            op = 0xE8 if kind == "K_CALL" else 0xE9
            if site[0] != op or addr + 5 + rel != target:
                problems.append("not a %s to %#x (op %02x, target %#x)" % (
                    "call" if op == 0xE8 else "jmp", target, site[0], addr + 5 + rel))
        else:
            val, = struct.unpack("<Q", site)
            if val != target or addr & 7:
                problems.append("slot holds %#x, want %#x" % (val, target))
        tp = read(data, segs, target, len(pro))
        if tp != pro:
            problems.append("target prologue differs: %s" % (tp.hex(" ") if tp else "unmapped"))
        print("%s %s %#x -> %#x: %s" % (name, kind, addr, target, "; ".join(problems) if problems else "ok"))
        bad += bool(problems)
    sys.exit(1 if bad else 0)


main()
