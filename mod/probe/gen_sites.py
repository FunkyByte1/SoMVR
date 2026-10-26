"""gen_sites.py BINARY - print the SITE(...) lines of mod/probe/somvr_probe.c from the game binary (read-only).

K_CALL: context = the 16 bytes before the call + the call (21 bytes); K_VSLOT: the 8-byte slot; K_ENTRY: the
function's first 16 bytes (here: functions the probe calls, never patched). Target prologue = 15 bytes.
RO: the console sites (docs/re-m1m2.md section 9; G22_REPO_FIND is the probe's own) and more slots the probe calls
through (OBJSYS_MODELHANDLE, SND_POSTGLOBAL, PHYS_RAYCAST; P9: NPC_SETZONE, OBJSYS_REMOVE), never patched;
each with its own context range, as the doc prints them (a None target = the slot's value in this build).
"""
import struct
import sys

BUILDID = "87ec5d6fd94afd47a24d5da7c9b0c2b74b30c6e9"

# the 19 calls of FUN_00a09e70(cs, fxFile, modelHandle, flags, parent) in the binary (0x26fc738 = the WEAPONFX key)
FXI = [0xa07fe1, 0xa08081, 0xe1e6d9, 0xe22df2, 0x26e5e34, 0x26e674b, 0x26e6de8, 0x26edeab, 0x26eeeca, 0x26f34f1,
       0x26f3659, 0x26fc738, 0x26fcbd7, 0x273c276, 0x273c4fb, 0x27bd11b, 0x27bd205, 0x27bd30e, 0x27bdf60]
HOOKS = [  # name, kind, site, expected target
    ("P_INPUT", "K_VSLOT", 0x34798c0, 0x9023b0),      # CWBInput::vf_04 per-frame device/binding update
    ("P_VALUE", "K_VSLOT", 0x3479950, 0x903590),      # CWBInput::vf_22 command value
    ("P_VALUE2", "K_VSLOT", 0x3479958, 0x903990),     # CWBInput::vf_23 second value query (camera axes)
    ("P_ISON", "K_VSLOT", 0x3479968, 0x9011e0),       # CWBInput::vf_25 command is on
    ("P_SERVER", "K_VSLOT", 0x3987690, 0x2777c50),    # CGameServerShell::vf_10 server update
    ("P_VIS", "K_VSLOT", 0x335cd90, 0x702e70),        # CRendererHandle::vf_107 visibility request (after H1)
    ("P_UI105", "K_VSLOT", 0x335cd80, 0x7051b0),      # CRendererHandle::vf_105 queue the Scaleform display
    ("P_ANIM_A", "K_CALL", 0xa41a55, 0xa51210),       # FUN_00a51210(PlayerBody*) in FUN_00a416a0
    ("P_ANIM_B", "K_CALL", 0xa41363, 0xa51210),       # same in FUN_00a40fe0
    ("P_CAMPOST_A", "K_CALL", 0xa41ab7, 0xa35340),    # FUN_00a35340(Player*) camera post in FUN_00a416a0
    ("P_CAMPOST_B", "K_CALL", 0xa413c5, 0xa35340),    # same in FUN_00a40fe0
    ("P_ATK_OBJ", "K_CALL", 0xa8cb19, 0x26edd90),     # player attack start (aim object) in FUN_00a8c700
    ("P_ATK_POS", "K_CALL", 0xa8d02b, 0x26edd40),     # player attack start (aim position)
    ("P_ATK_NOPOS", "K_CALL", 0xa8d04d, 0x26ed9b0),   # player attack start (no aim)
    ("P_DEALHIT", "K_CALL", 0x26e5468, 0x26d4ff0),    # APPLYDAMAGE range test -> Combat_DealHit
    ("P_APPLYDMG", "K_CALL", 0x26e5af5, 0x26e49a0),   # APPLYDAMAGE key -> range/angle test
    ("P_COUNTER", "K_CALL", 0x26fc32b, 0x26e5f90),    # BPW key -> counter prompt (orc, windowMs)
    ("P_HITEV", "K_CALL", 0x2819a9a, 0x26f2580),      # hit event (targetCombat, ev)
    ("P_DEATH", "K_CALL", 0x281985d, 0x26f61f0),      # death (victim, attack, killer)
    ("P_PMROOT", "K_VSLOT", 0x34fc838, 0xab5e70),     # PlayerMovement::vf_11 root motion + the extra delta (pmcount)
    # P7 (docs/runtime-probes.md section 2 P7; re-combat-vr.md, re-combat-items.md, re-hud.md)
    ("P_STIM_START", "K_CALL", 0x26ed119, 0x26c7180),  # Combat_StartAttack -> FUN_026c7180 (the attack's AI stimuli)
    ("P_STIM_KEY", "K_CALL", 0x26fd1d7, 0x26c7180),    # Combat_OnModelString STIMULUS key -> FUN_026c7180
    ("P_STIM_STOP", "K_CALL", 0x26eed28, 0x26c7180),   # Combat_StopAttack -> FUN_026c7180
    ("P_REGSTIM", "K_CALL", 0x26c747f, 0x293e820),     # FUN_026c7180 -> CAIStimulusMgr::RegisterStimulus (the list)
    ("P_REGSTIM_P", "K_CALL", 0x26c76bf, 0x293e820),   # the same for PersistentStimulus
    ("P_AI_GOAL", "K_VSLOT", 0x3a4e0f8, 0x29d6d80),    # CAIGoalCounterMelee::vf_07
    ("P_AI_BLOCK_OK", "K_VSLOT", 0x3a3d828, 0x29c5510),  # CAIActionCounterMeleeBlock::vf_19 (context check)
    ("P_AI_BLOCK_GO", "K_VSLOT", 0x3a3d7e8, 0x29c61d0),  # CAIActionCounterMeleeBlock::vf_11
    ("P_AI_DODGE_OK", "K_VSLOT", 0x3a3e428, 0x29c6590),  # CAIActionCounterMeleeDodge::vf_19
    ("P_AI_DODGE_GO", "K_VSLOT", 0x3a3e3e8, 0x29c7e90),  # CAIActionCounterMeleeDodge::vf_11
    ("P_KEYTIMES", "K_CALL", 0x26eb77a, 0x2263590),    # Combat_StartAttack -> the key-time table FUN_02263590(ws, hObj, attack)
    ("P_PJ_UPDATE", "K_VSLOT", 0x3998748, 0x27c5d20),  # CProjectile::vf_13 per-frame update
    ("P_PJ_DAMAGE", "K_CALL", 0x27bf8ba, 0x26c8e10),   # projectile impact -> FUN_026c8e10 (the projectile DealHit)
    ("P_SND_POST", "K_CALL", 0x8c9865, 0x1467870),     # FUN_008c9840 -> AK::SoundEngine::PostEvent(name, obj, ...)
    ("P_SND_POST_CB", "K_CALL", 0x8c988d, 0x1467870),  # FUN_008c9870 (with a callback) -> the same
    ("P_UISCENE", "K_CALL", 0xc1ee09, 0x9f2550),       # FUN_00c1e370 -> FUN_009f2550(renderer, view): the UI scene view
] + [("P_FXI_%X" % a, "K_CALL", a, 0xa09e70) for a in FXI] + [  # every call of the CLIENTFX_CREATESTRUCT init (fxlog)
    ("P_RES3D", "K_CALL", 0x741ceb, 0x73fce0),        # the 3D surface desc FUN_0073fce0(dev, &dev+0xd8, w, h, ..) (restall)
    # P8 (docs/runtime-probes.md P8; re-arms-ik.md 5, re-ui-pointer.md 6, re-traversal.md 4)
    ("P_IK_SETUP_A", "K_CALL", 0x21b875e, 0x2185900),  # IK::vf_07 -> FUN_02185900(chain, hObj, record) (ikwatch)
    ("P_IK_SETUP_B", "K_CALL", 0x21b8c8a, 0x2185900),  # IK::vf_12 -> the same
    ("P_IK_CHAIN", "K_CALL", 0x20a51d4, 0x2185590),    # the anim manager's per-frame FUN_02185590(chain), 4 slots (ikwatch)
    ("P_IEM_ADDCB", "K_VSLOT", 0x3484260, 0x91ad70),   # IEngineModel slot 38 AddNodeCallback (ikcb)
    ("P_IEM_REMCB", "K_VSLOT", 0x3484268, 0x91adf0),   # IEngineModel slot 39 RemoveNodeCallback (ikcb)
    ("P_TRK_EVAL", "K_VSLOT", 0x38df398, 0x23899b0),   # CSimpleModelTrackerNode::vf_03, the phase-2 write point (armpose)
    ("P_ATTACH_LISTEN", "K_VSLOT", 0x38d8958, 0x235a660),  # CSimpleModelAttachNode::vf_02, phase 7 (armpose's sword check)
    ("P_LP_GUARD_HOVER", "K_CALL", 0xb4e5d8, 0xa38e80),  # the menu item hover callback's pad guard FUN_00a38e80(P) (padguard)
    ("P_LP_GUARD_CLICK", "K_CALL", 0xb4e62c, 0xa38e80),  # the menu item click callback's pad guard
    ("P_LP_SYS_DOWN", "K_VSLOT", 0x34e4230, 0x9f4120),   # CGameSystemEventHandler::vf_07 mouse down (mouselog)
    ("P_LP_SYS_UP", "K_VSLOT", 0x34e4238, 0x9f41c0),     # vf_08 mouse up
    ("P_LP_SYS_MOVE", "K_VSLOT", 0x34e4240, 0x9f4260),   # vf_09 mouse move
    ("P_LP_VIEW_DOWN", "K_VSLOT", 0x3482248, 0x90b6d0),  # CUIView::vf_65 -> GFx MouseDown (mouselog)
    ("P_LP_VIEW_UP", "K_VSLOT", 0x3482250, 0x90b770),    # CUIView::vf_66 MouseUp
    ("P_LP_VIEW_MOVE", "K_VSLOT", 0x3482258, 0x90b810),  # CUIView::vf_67 MouseMove
    ("P_LP_GFX_EVENT", "K_VSLOT", 0x3763ec0, 0x124a3d0),  # Scaleform::GFx::MovieImpl::vf_36 HandleEvent (mouselog)
    ("P_GRAV", "K_VSLOT", 0x34fc810, 0xab58a0),        # PlayerMovement::vf_06, the gravity wish (grav)
    # P9 (docs/runtime-probes.md P9; re-combat-spawn.md 2/4 CS_*, re-bow.md 1.4 BOW_FA_CAMXF)
    ("P_AIB_GATE", "K_CALL", 0x26eb818, 0x229aef0),    # Combat_StartAttack at an NPC -> FUN_0229aef0(node, attack) (aiblockwatch)
    ("P_AIB_REC", "K_CALL", 0x26ecbc7, 0x26ff330),     # -> FUN_026ff330: the target NPC's character combat record
    ("P_AIB_ROLL", "K_CALL", 0x26ecd50, 0xc73440),     # -> FUN_00c73440(0, 1): the AIBlockTable roll, chance in xmm2 (blockroll)
    ("P_AIB_NOTIFY", "K_CALL", 0x26ecd6a, 0x29ac890),  # -> FUN_029ac890(attackerOwner, npc): the incoming-attack fact
    ("P_GOAL_REL", "K_VSLOT", 0x3a4e0d8, 0x29d6b90),   # CAIGoalCounterMelee::vf_03 (relevance -> goal+0x10)
    ("P_SHIELD", "K_CALL", 0x28177c4, 0x28adf30),      # the damage context's shield test FUN_028adf30(shield, attack, ..)
    ("P_BOW_CAMXF", "K_CALL", 0xa9a1d0, 0xa5b8b0),     # free aim FUN_00a9a140 -> FUN_00a5b8b0(cam, &xf): the aim ray (aimray)
    # P10 (docs/runtime-probes.md P10; design-locomotion.md 2.6 LOCO_ROOTDELTA)
    ("P_ROOTDELTA", "K_CALL", 0xab5ea9, 0xa513a0),     # PlayerMovement::vf_11 -> FUN_00a513a0(body, &delta): the root delta (speed, loco)
]
FUNCS = [  # functions the probe calls (checked, never patched)
    ("F_CMD_ON", 0xaab660), ("F_CMD_OFF", 0xaab5d0), ("F_CMD_ACTIVE", 0xaab900), ("F_INPUT_MODE", 0xa0eeb0),
    ("F_DEALHIT", 0x26d4ff0), ("F_ATK_BYNAME", 0x2166790), ("F_MODELOF", 0x8f3c20), ("F_GETNODE", 0x91a750),
    ("F_NODEXF", 0x91c360), ("F_HASHINDEX", 0x91ac20), ("F_GETSOCKET", 0x91b410), ("F_NUMPIECES", 0x91ae60),
    ("F_GETPIECE", 0x91aeb0), ("F_PIECENAME", 0x91b510), ("F_HIDEPIECE", 0x91b310), ("F_VIDEOCOVER", 0xa10d30),
    ("F_BODYSETCAM", 0xa4bef0), ("F_CAMRESET", 0xa5afb0), ("F_LOCALPLAYER", 0x9e3570),
    # P7
    ("F_STIM", 0x26c7180), ("F_ATTACKDATA", 0xa91300), ("F_STIM_REMOVE", 0x293e150),
    ("F_TIMER_CREATE", 0x8f8fc0), ("F_TIMER_ATTACH", 0x8f8f60), ("F_TIMER_DETACH", 0x8f8f90),
    ("F_TIMER_SETOBJ", 0x8f9050), ("F_TIMER_GETOBJ", 0x8f8ec0), ("F_TIMER_RELEASE", 0x9ec850),
    ("F_GETCURANIM", 0x91ba10), ("F_GETANIMTIME", 0x91a520), ("F_GETANIMRATE", 0x91a580), ("F_SETANIMRATE", 0x91bd90),
    ("F_KEYLOOKUP", 0x91b220), ("F_SAMPLENODE", 0x91c410), ("F_FINDTRACKER", 0xc73800),
    ("F_FX_CSINIT", 0xa09e70), ("F_FX_CREATE", 0xd8c7f0), ("F_FX_CSFREE", 0x9ec750), ("F_FX_RELEASE", 0x2aebfd0),
    ("F_ITEM_REATTACH", 0x231e2e0), ("F_ITEM_VISIBLE", 0x231e700),
    ("F_SND_REGOBJ", 0x8c97c0), ("F_SND_SETPOS", 0x8c9b80), ("F_SND_POST", 0x8c9840), ("F_NAV_POS", 0xebc700),
    ("F_SUPPORTED", 0xa36db0),
    # P8
    ("F_AI_SCRIPTTGT", 0x296f870), ("F_AI_SCRIPTCLR", 0x296f940), ("F_IM_MOVE", 0xa0fa20), ("F_IM_DOWN", 0xa0f760),
    ("F_IM_UP", 0xa0f8c0), ("F_VIEW_MSTATE", 0x90b960), ("F_VIEW_TOSTAGE", 0xc83ba0), ("F_CLIP_VIEW", 0x9783b0),
    ("F_CLIP_HITTEST", 0x979db0), ("F_SURF_FROM_RAY", 0x2ae5d90),
    # P9
    ("F_SPAWN_AI", 0x29a4830), ("F_TMPL_BYNAME", 0x2153910), ("F_ZONE_AT", 0x22a2a80), ("F_SETLEVEL", 0x26ee7c0),
    ("F_FM_BIND", 0x2975eb0), ("F_FM_ACTIVATE", 0x22cdad0), ("F_FACTION_TARGET", 0x23d3f80), ("F_FM_TEMPLATE", 0x22d71b0),
    ("F_CHAR_COMBATREC", 0x26ff330), ("F_AI_FACT", 0x29ac890), ("F_TOD_NAME", 0x25ccd10), ("F_WM_CMDID", 0xe24570),
    # P10
    ("F_GRAPH_VAR", 0x208dd90), ("F_GRAPH_GET", 0x208de50), ("F_VELMODE", 0x20c6ff0),
]
RO = [  # name, kind, site, context start, context length, expected target (the exec word)
    ("G22_RUNSTRING", "K_VSLOT", 0x344d650, 0x344d640, 40, 0x8bd8b0),   # CWBEngine::vf_32 RunConsoleString
    ("G22_SERVERGATE", "K_VSLOT", 0x344d7c8, 0x344d7c0, 24, 0x8bd4f0),  # CWBEngine::vf_79 *(*0x466aee0+0xc58) != 0
    ("G22_EXEC", "K_ENTRY", 0x8b9ac0, 0x8b9ac0, 16, 0x8b9ac0),          # the executor FUN_008b9ac0
    ("G22_REPO_FIND", "K_VSLOT", 0x3910d00, 0x3910d00, 8, 0x2455e60),   # CConsoleRepository::vf_08 find(name)
    ("OBJSYS_MODELHANDLE", "K_VSLOT", 0x3453170, 0x3453170, 8, None),   # the object system's vtbl+0x118 (weaponfx)
    ("SND_POSTGLOBAL", "K_VSLOT", 0x346da80, 0x346da80, 8, 0x8ed730),   # CSoundMgr::vf_13 PostEventGlobal (sound)
    ("PHYS_RAYCAST", "K_VSLOT", 0x3472458, 0x3472458, 8, 0x8f4fd0),     # IEnginePhysics::vf_64 -> FUN_008de5e0 (surf)
    ("NPC_SETZONE", "K_VSLOT", 0x3a2b658, 0x3a2b658, 8, 0x29639b0),     # NPC vtbl+0x288 (the debug spawn's post step, spawn)
    ("OBJSYS_REMOVE", "K_VSLOT", 0x34530b0, 0x34530b0, 8, 0x8c6f60),    # the object system's vtbl+0x58: deferred remove (despawn)
]


def loads(data):
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    return [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phentsize) for i in range(phnum)]


def read(data, segs, addr, n):
    for p_type, _f, off, vaddr, _pa, filesz, _m, _al in segs:
        if p_type == 1 and vaddr <= addr and addr + n <= vaddr + filesz:
            return data[off + addr - vaddr: off + addr - vaddr + n]
    sys.exit("unmapped %#x" % addr)


def hx(b):
    return " ".join("%02x" % c for c in b)


def main():
    data = open(sys.argv[1], "rb").read()
    segs = loads(data)
    for name, kind, site, target in HOOKS:
        if kind == "K_CALL":
            b = read(data, segs, site, 5)
            rel, = struct.unpack("<i", b[1:5])
            assert b[0] == 0xE8 and site + 5 + rel == target, name
            ctx, cb = site - 16, read(data, segs, site - 16, 21)
        else:
            v, = struct.unpack("<Q", read(data, segs, site, 8))
            assert v == target, name
            ctx, cb = site, read(data, segs, site, 8)
        print('SITE("%s", %s, %#x, %#x, "%s", %#x, "%s"),' % (name, kind, site, ctx, hx(cb), target,
                                                            hx(read(data, segs, target, 15))))
    print("--")
    for name, addr in FUNCS:
        print('SITE("%s", K_ENTRY, %#x, %#x, "%s", %#x, "%s"),' % (name, addr, addr, hx(read(data, segs, addr, 16)),
                                                                 addr, hx(read(data, segs, addr, 15))))
    print("--")
    for name, kind, site, ctx, n, target in RO:
        if kind == "K_VSLOT":
            v, = struct.unpack("<Q", read(data, segs, site, 8))
            assert target is None or v == target, name
            target = v
        print('SITE("%s", %s, %#x, %#x, "%s", %#x, "%s"),' % (name, kind, site, ctx, hx(read(data, segs, ctx, n)),
                                                            target, hx(read(data, segs, target, 15))))


main()
