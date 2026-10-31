/* somvr_menus_layer.c - build 6 (docs/re-xr-pipeline.md 28): the game's menus on a board in front of the head, the
 * wrist minimap, the arrival controls card, touch. Included by somvr_vklayer.c after the VR menu's region (it uses X,
 * cfg, I and VM); the frame code calls in through the mn_* functions declared there. X.mx held, the present thread.
 * Every part is off unless its switch is on (../somvr_menus.h for the pure parts):
 *   SOMVR_XR_MENU=front    the menu quad (mono frames) and the HUD quad while a game menu is open (the preload's
 *                          SOMVR_HUDF_MENU on the present's HUD entry) on a board SOMVR_XR_MENU_QUAD ahead of the head,
 *                          placed at each open (the first mono frame after a stereo one / the menu bit rising), kept
 *                          while it stays open; the laser (SOMVR_POINTER) points at the HUD board too. The world HUD is
 *                          placed again ahead of the head when a menu closes.
 *   SOMVR_XR_MINIMAP=wrist the minimap rect (SOMVR_XR_MINIMAP_RECT) of the HUD image is copied into an extra column of
 *                          the HUD swapchain (and not shown in the head-level HUD quad), shown as a watch face on the back
 *                          of the off hand's wrist while it faces the eyes (somvr_mm_pose / somvr_mm_facing).
 *   SOMVR_XR_CARD=on       at the first arrival in the practice area (the preload's pstate: here, the blink over) the VR
 *                          menu's Controls page is shown once as a card in front of the player; its X box, A, opening
 *                          the wrist menu or a grip squeezed (a draw, a grab: it holds the blade off) put it away (once
 *                          a session).
 *   SOMVR_XR_TOUCH=on      a pointing fingertip (the trigger untouched) pokes the boards, the wrist panel and the card;
 *                          the laser stays. A press must cross the surface (not a push from behind it); the open wrist
 *                          panel closes when its wrist turns over to the watch (SOMVR_XR_MINIMAP=wrist).
 *   SOMVR_XR_WRIST_HUD=on  build 8: the hit streak and the target panel (SOMVR_XR_WRIST_RECTS) join the minimap on the
 *                          watch: a column beside the map toward the elbow (the health ring, the focus arc and the arrow
 *                          count are the minimap's own: on the wrist since build 6). Needs SOMVR_XR_MINIMAP=wrist.
 *   SOMVR_XR_PROMPTS=on    build 8: the hints and notifications, the XP gains and the button prompts
 *                          (SOMVR_XR_PROMPTS_RECTS) cut out of the head-level HUD into one band (XL_BAND), larger
 *                          (SOMVR_XR_PROMPTS_QUAD: an 18-px line at 0.85 deg) and lower than the HUD, placed with it.
 *   SOMVR_XR_POPUPS=gaze   build 21 (with SOMVR_XR_PROMPTS=on, the HUD world-locked): the band's tiles each their own
 *                          quad (XL_BAND..XL_BAND3), shown while the HUD image has something in them (a reading of their
 *                          rows after each stereo frame's copy), placed in front of the gaze when it comes up, held still
 *                          in the room while it shows, eased to the new front when the head turns away more than 40 deg
 *                          (SOMVR_XR_POPUPS_GEOM; ../somvr_menus.h 2c). Default band (build 20c).
 *   SOMVR_XR_SUBTITLES=on  build 22 (with SOMVR_XR_PROMPTS=on): the game's subtitle line (SOMVR_XR_SUBTITLES_RECT, default
 *                          400,755,1120,185 since build 23: the speaker's banner over it too - a captain's introduction
 *                          card) a band row of its own, the last - with gaze its own popup (XL_BAND4) at
 *                          SOMVR_XR_SUBTITLES_SCALE % (80) of the band's scale; a band tile overlapping it is cut to the
 *                          side of it with more of the tile (build 22: the prompts' 1120,860,800,220 -> 1120,940,800,140;
 *                          build 23's band defaults touch it nowhere). Build 21 cut the line in two: its right part in the
 *                          prompts' popup, the rest in the world HUD quad (../somvr_menus.h 2d). Default off.
 *   SOMVR_XR_TEST=PATH     headless tests only: test hands, buttons, sticks and on-demand dumps from that file
 *                          (../somvr_menus.h 4; scripts/menus-drive.sh writes it, scripts/xr_compose.py reads the dumps).
 */

static struct {
    /* the switches */
    int menu, menu_bad;
    float mq[3];            /* SOMVR_XR_MENU_QUAD: distance, width, drop */
    float pitch_max;        /* SOMVR_XR_MENU_PITCH: a game menu's board down the gaze, at most this many deg (0 = level) */
    int pitch_bad;
    float pitch_up;         /* build 19, SOMVR_XR_MENU_PITCH_UP: ... up a raised gaze, at most this many deg (0 = level) */
    int pitch_up_bad;
    int mm, mm_bad;
    int mm_rect[4];         /* SOMVR_XR_MINIMAP_RECT (1920x1080 pixels) */
    somvr_mm_cfg mmc;
    int card, card_bad;
    int touch, touch_bad;
    somvr_tc_cfg tcc;
    /* the boards */
    int mono_latched;       /* the menu quad's board is placed (mono_pose) */
    somvr_posef mono_pose;
    uint64_t mono_stereo;   /* X.n_stereo at the last mono frame: a stereo frame since = a new open */
    uint32_t hud_kind;      /* the HUD entry's kind of the frame hud_quad builds (set by its caller) */
    int hud_menu;           /* this stereo frame's HUD quad is a game menu on the board */
    int hud_latched;        /* the HUD board is placed (hud_pose) */
    somvr_posef hud_pose;
    int was_menu;           /* the last HUD frame was a menu */
    uint64_t n_mono_boards, n_hud_boards, n_menu_frames;
    /* the minimap */
    uint32_t extra;         /* the HUD swapchain's extra width (the tiles' area), set with the swapchain */
    int px[4];              /* the minimap's rect in the HUD image */
    int split;              /* this frame's HUD copy put the tiles into the extra area (not a menu frame) */
    /* build 8: the wrist HUD and the prompt band (../somvr_menus.h 2b) */
    int wh, wh_bad, pb, pb_bad;
    somvr_ht_set wh_set, wh_clear, pb_set; /* 1920x1080 pixels (the switches) */
    somvr_ht_set wh_px, wc_px, pb_px;      /* ... in the HUD image's pixels (mn_layout) */
    somvr_pb_cfg pbc;
    somvr_hud_layout lay;   /* this frame's layout (the off hand's side for the watch's column) */
    uint32_t lay_w, lay_h;  /* the HUD image size the rects were scaled for */
    somvr_posef band_pose;  /* world: placed with the HUD quad */
    int band_placed, band_shown;
    XrCompositionLayerQuad bq;
    uint64_t n_band, n_capped;
    /* build 21: the popups where the player looks (SOMVR_XR_POPUPS=gaze; ../somvr_menus.h 2c) */
    int pp, pp_bad, ppg_bad;
    somvr_pp_cfg ppc;
    somvr_pp_state pps;
    somvr_pp_read pp_rd[SOMVR_PP_MAX]; /* the latest reading of each tile */
    XrCompositionLayerQuad pq[SOMVR_PP_MAX];
    uint32_t pp_has;                   /* bit i: tile i's quad in the last stereo frame (reuse frames repeat them) */
    /* build 22: the subtitles (SOMVR_XR_SUBTITLES=on; ../somvr_menus.h 2d) */
    int sub, sub_bad, sub_scale, sub_idx, sub_cut; /* sub_idx: its index in pb_set (the last; -1 none) */
    somvr_ht_set sub_set;
    double pp_t;                       /* the last step (s; a gap of 0.5 s starts afresh) */
    struct dev_st *pp_ds;              /* the readings' buffer: its device */
    VkBuffer pp_buf;
    VkDeviceMemory pp_mem;
    uint8_t *pp_map;
    int pp_dead;                       /* no readback this session: the band as one quad */
    VkDeviceSize pp_esz;               /* an entry's bytes */
    struct {
        const slot_st *sl;      /* the game slot whose submit copies it (its fence), NULL = free */
        uint64_t seq;
        int n, w[SOMVR_PP_MAX], nrow[SOMVR_PP_MAX];
        VkDeviceSize off[SOMVR_PP_MAX];
    } pp_e[4];
    int pp_armed;
    uint64_t pp_sub, pp_seq_read, pp_nread, pp_nlate, n_pp_placed, n_pp_eased, n_pp_over, n_pp_frames;
    uint32_t pp_lines;
    double pp_us_max;                  /* the longest reading's measure (us) */
    int shown;              /* the watch face is seen (hysteresis) */
    XrCompositionLayerQuad q;
    uint64_t n_split, n_shown;
    /* touch */
    somvr_tc_state tg, tv;  /* on the game's menus; on the wrist panel and the card */
    int vtouch;             /* this vm_frame's pointer is the fingertip's */
    somvr_tc_out tvo;
    uint64_t n_press_g, n_press_v;
    /* the card */
    int card_state;         /* 0 waiting for the practice area, 1 shown, 2 put away (once a session) */
    somvr_posef card_pose;
    uint8_t a_presses;      /* the right hand's A press counter when it came up */
    uint8_t sq_low;         /* bit h: hand h's grip was let go since it came up (a grip held through it closes nothing) */
    uint32_t lines;
    /* the wrist panel or the card of the last stereo frame that showed one (SOMVR_XR_TEST's touch lines) */
    somvr_posef panel;
    float panel_sz[2];
    uint64_t panel_n;       /* X.n_stereo + 1 then (0 = never) */
} MN;

#define MN_CARD_W_M 0.30f   /* the card: the panel's picture, 30 x 45 cm, 0.65 m ahead, 0.12 m low: 26 deg wide */
#define MN_CARD_DIST_M 0.65f
#define MN_CARD_DROP_M 0.12f
#define MN_MARK_M 0.009f    /* the fingertip's marker: a 9 mm disc (HoF2's) */
static void mn_xt_log(void); /* SOMVR_XR_TEST (below) */

static void mn_config(void) {
    MN.menu = somvr_mn_parse(getenv("SOMVR_XR_MENU"), &MN.menu_bad);
    int b = 0;
    somvr_mn_parse_quad(getenv("SOMVR_XR_MENU_QUAD"), MN.mq, &b);
    MN.menu_bad |= b << 1;
    { /* SOMVR_XR_MENU_PITCH=DEG (0..60, default 0 = build 16's level board) */
        const char *pv = getenv("SOMVR_XR_MENU_PITCH");
        char *e = NULL;
        float d = pv && *pv ? strtof(pv, &e) : 0;
        MN.pitch_bad = pv && *pv && (!e || *e || !(d >= 0 && d <= 60));
        MN.pitch_max = MN.pitch_bad ? 0 : d;
    }
    { /* build 19: SOMVR_XR_MENU_PITCH_UP=DEG (0..60, default 0 = build 18's level board for a raised gaze) */
        const char *pv = getenv("SOMVR_XR_MENU_PITCH_UP");
        char *e = NULL;
        float d = pv && *pv ? strtof(pv, &e) : 0;
        MN.pitch_up_bad = pv && *pv && (!e || *e || !(d >= 0 && d <= 60));
        MN.pitch_up = MN.pitch_up_bad ? 0 : d;
    }
    MN.mm = somvr_mm_parse(getenv("SOMVR_XR_MINIMAP"), &MN.mm_bad);
    b = 0;
    somvr_mm_parse_rect(getenv("SOMVR_XR_MINIMAP_RECT"), MN.mm_rect, &b);
    MN.mm_bad |= b << 1;
    somvr_mm_cfg_default(&MN.mmc);
    b = 0;
    somvr_mm_parse_quad(getenv("SOMVR_XR_MINIMAP_QUAD"), &MN.mmc, &b);
    MN.mm_bad |= b << 2;
    const char *p = getenv("SOMVR_XR_CARD");
    MN.card = p && (!strcmp(p, "on") || !strcmp(p, "1"));
    MN.card_bad = p && *p && !MN.card && strcmp(p, "off") && strcmp(p, "0");
    MN.touch = somvr_tc_parse(getenv("SOMVR_XR_TOUCH"), &MN.touch_bad);
    somvr_tc_cfg_default(&MN.tcc);
    MN.wh = somvr_wh_parse(getenv("SOMVR_XR_WRIST_HUD"), &MN.wh_bad);
    b = 0;
    somvr_ht_parse(getenv("SOMVR_XR_WRIST_RECTS"), SOMVR_WH_RECTS_DEFAULT, &MN.wh_set, &b);
    MN.wh_bad |= b << 1;
    b = 0;
    somvr_ht_parse(getenv("SOMVR_XR_WRIST_CLEAR"), SOMVR_WH_CLEAR_DEFAULT, &MN.wh_clear, &b);
    MN.wh_bad |= b << 2;
    MN.pb = somvr_pb_parse(getenv("SOMVR_XR_PROMPTS"), &MN.pb_bad);
    b = 0;
    somvr_ht_parse(getenv("SOMVR_XR_PROMPTS_RECTS"), SOMVR_PB_RECTS_DEFAULT, &MN.pb_set, &b);
    MN.pb_bad |= b << 1;
    somvr_pb_cfg_default(&MN.pbc);
    b = 0;
    somvr_pb_parse_quad(getenv("SOMVR_XR_PROMPTS_QUAD"), &MN.pbc, &b);
    MN.pb_bad |= b << 2;
    MN.sub = somvr_sub_parse(getenv("SOMVR_XR_SUBTITLES"), &MN.sub_bad); /* build 22 */
    b = 0;
    MN.sub_scale = somvr_sub_parse_scale(getenv("SOMVR_XR_SUBTITLES_SCALE"), &b);
    MN.sub_bad |= b << 1;
    b = 0;
    somvr_ht_parse(getenv("SOMVR_XR_SUBTITLES_RECT"), SOMVR_SUB_RECT_DEFAULT, &MN.sub_set, &b);
    if (b || MN.sub_set.n != 1) MN.sub_bad |= 4, somvr_ht_parse(NULL, SOMVR_SUB_RECT_DEFAULT, &MN.sub_set, &b);
    MN.sub_idx = -1, MN.sub_cut = 0;
    if (MN.sub && MN.pb) MN.sub_idx = somvr_sub_add(&MN.pb_set, MN.sub_set.r[0], &MN.sub_cut);
    MN.pp = somvr_pp_parse(getenv("SOMVR_XR_POPUPS"), &MN.pp_bad); /* build 21 */
    somvr_pp_cfg_default(&MN.ppc);
    somvr_pp_parse_geom(getenv("SOMVR_XR_POPUPS_GEOM"), &MN.ppc, &MN.ppg_bad);
    mn_xt_config();
}

/* a rect set as text: "x,y wxh; ...", rows apart with " | " */
static void mn_set_text(const somvr_ht_set *t, char *out, size_t cap) {
    size_t k = 0;
    out[0] = 0;
    for (int i = 0; i < t->n && k < cap; i++)
        k += (size_t)snprintf(out + k, cap - k, "%s%d,%d %dx%d", !i ? "" : t->row[i] != t->row[i - 1] ? " | " : "; ",
                              t->r[i][0], t->r[i][1], t->r[i][2], t->r[i][3]);
}

/* the session's start (after the HUD, the pointer and the VR menu): the lines, nothing latched */
static void mn_setup(void) {
    MN.mono_latched = MN.hud_latched = MN.was_menu = MN.hud_menu = MN.split = MN.shown = 0;
    MN.mono_stereo = 0, MN.extra = 0, MN.card_state = 0, MN.vtouch = 0, MN.lines = 0;
    MN.n_mono_boards = MN.n_hud_boards = MN.n_menu_frames = MN.n_split = MN.n_shown = MN.n_press_g = MN.n_press_v = 0;
    MN.band_placed = MN.band_shown = 0, MN.n_band = MN.n_capped = 0, MN.lay_w = MN.lay_h = 0;
    somvr_pp_reset(&MN.pps), memset(MN.pp_rd, 0, sizeof MN.pp_rd), memset(MN.pp_e, 0, sizeof MN.pp_e);
    MN.pp_has = 0, MN.pp_t = 0, MN.pp_dead = 0, MN.pp_armed = -1, MN.pp_lines = 0;
    MN.pp_sub = MN.pp_seq_read = MN.pp_nread = MN.pp_nlate = MN.n_pp_placed = MN.n_pp_eased = MN.n_pp_over = 0;
    MN.n_pp_frames = 0, MN.pp_us_max = 0;
    memset(&MN.lay, 0, sizeof MN.lay);
    somvr_tc_reset(&MN.tg), somvr_tc_reset(&MN.tv);
    if (MN.menu_bad & 1) lg("xr: menus: SOMVR_XR_MENU=%s is not front or off: off", getenv("SOMVR_XR_MENU"));
    if (MN.menu_bad & 2) lg("xr: menus: SOMVR_XR_MENU_QUAD=%s is not DIST,WIDTH,DROP: the defaults", getenv("SOMVR_XR_MENU_QUAD"));
    if (MN.pitch_bad) lg("xr: menus: SOMVR_XR_MENU_PITCH=%s is not 0..60 (deg): 0 (the level board)", getenv("SOMVR_XR_MENU_PITCH"));
    if (MN.pitch_max > 0)
        lg("xr: menus: SOMVR_XR_MENU_PITCH=%.0f: a game menu opened with the head looking down gets its board down the gaze "
           "(at most %.0f deg below the level)", MN.pitch_max, MN.pitch_max);
    if (MN.pitch_up_bad)
        lg("xr: menus: SOMVR_XR_MENU_PITCH_UP=%s is not 0..60 (deg): 0 (the level board)", getenv("SOMVR_XR_MENU_PITCH_UP"));
    if (MN.pitch_up > 0)
        lg("xr: menus: SOMVR_XR_MENU_PITCH_UP=%.0f: a game menu opened with the head looking up (a glyph's Ithildin page) "
           "gets its board up the gaze (at most %.0f deg above the level)", MN.pitch_up, MN.pitch_up);
    if (MN.mm_bad & 1) lg("xr: minimap: SOMVR_XR_MINIMAP=%s is not wrist or off: off", getenv("SOMVR_XR_MINIMAP"));
    if (MN.mm_bad & 2) lg("xr: minimap: SOMVR_XR_MINIMAP_RECT=%s is not X,Y,W,H in 1920x1080: the default",
                          getenv("SOMVR_XR_MINIMAP_RECT"));
    if (MN.mm_bad & 4) lg("xr: minimap: SOMVR_XR_MINIMAP_QUAD=%s is not SIZE,BACK,OUT: the defaults",
                          getenv("SOMVR_XR_MINIMAP_QUAD"));
    if (MN.card_bad) lg("xr: card: SOMVR_XR_CARD=%s is not on or off: off", getenv("SOMVR_XR_CARD"));
    if (MN.touch_bad) lg("xr: touch: SOMVR_XR_TOUCH=%s is not on or off: off", getenv("SOMVR_XR_TOUCH"));
    if (MN.wh_bad & 1) lg("xr: wrist hud: SOMVR_XR_WRIST_HUD=%s is not on or off: off", getenv("SOMVR_XR_WRIST_HUD"));
    if (MN.wh_bad & 2) lg("xr: wrist hud: SOMVR_XR_WRIST_RECTS=%s is not X,Y,W,H[;X,Y,W,H..] (at most %d) in 1920x1080: "
                          "the defaults", getenv("SOMVR_XR_WRIST_RECTS"), SOMVR_HT_MAX);
    if (MN.wh_bad & 4) lg("xr: wrist hud: SOMVR_XR_WRIST_CLEAR=%s is not X,Y,W,H[;X,Y,W,H..] (at most %d) in 1920x1080: "
                          "the defaults", getenv("SOMVR_XR_WRIST_CLEAR"), SOMVR_HT_MAX);
    if (MN.pb_bad & 1) lg("xr: prompts: SOMVR_XR_PROMPTS=%s is not on or off: off", getenv("SOMVR_XR_PROMPTS"));
    if (MN.pb_bad & 2) lg("xr: prompts: SOMVR_XR_PROMPTS_RECTS=%s is not X,Y,W,H[;X,Y,W,H..][|X,Y,W,H..] (at most %d) in "
                          "1920x1080: the defaults", getenv("SOMVR_XR_PROMPTS_RECTS"), SOMVR_HT_MAX);
    if (MN.pb_bad & 4) lg("xr: prompts: SOMVR_XR_PROMPTS_QUAD=%s is not DIST,LINE_DEG,DOWN_DEG: the defaults",
                          getenv("SOMVR_XR_PROMPTS_QUAD"));
    mn_xt_log();
    float wdeg = 2 * atanf(0.5f * MN.mq[1] / MN.mq[0]) * 57.29578f;
    lg("xr: menus: %s (the menu quad and the HUD in a game menu: a %.2f m wide board %.2f m ahead of the head, %.2f m "
       "below the eyes, %.0f deg wide, placed at each open, kept while open%s)",
       MN.menu ? "on" : "off (SOMVR_XR_MENU unset: the menu quad 2 m ahead of the space's origin)", MN.mq[1], MN.mq[0],
       MN.mq[2], wdeg, X.ptr_on ? "; the laser points at it" : "");
    lg("xr: minimap: %s (the rect %d,%d %dx%d of a 1920x1080 HUD; a %.0f cm watch face %.0f cm behind the off hand's grip, "
       "%.1f cm off the back of the hand, shown within %.0f deg of the eyes)",
       MN.mm ? (X.hud ? "on the wrist" : "on the wrist, but no HUD quad this session") : "off (it stays in the HUD)",
       MN.mm_rect[0], MN.mm_rect[1], MN.mm_rect[2], MN.mm_rect[3], MN.mmc.size_m * 100, MN.mmc.back_m * 100,
       MN.mmc.out_m * 100, acosf(MN.mmc.face_on) * 57.29578f);
    char wr[320] = "", cr[320] = "", pr[320] = "";
    mn_set_text(&MN.wh_set, wr, sizeof wr), mn_set_text(&MN.wh_clear, cr, sizeof cr), mn_set_text(&MN.pb_set, pr, sizeof pr);
    lg("xr: wrist hud: %s (the rects %s beside the map, toward the elbow; %s cleared from the HUD)",
       !MN.wh ? "off" : !MN.mm ? "on, but no wrist minimap (SOMVR_XR_MINIMAP): off" : !X.hud ? "on, but no HUD quad" : "on",
       wr, cr);
    lg("xr: prompts: %s (the rects %s in one band %.2f m from the eyes, %.0f deg below them, an 18-px line %.2f deg; "
       "placed with the HUD quad)", !MN.pb ? "off" : !X.hud ? "on, but no HUD quad" : "on", pr, MN.pbc.dist_m,
       MN.pbc.down_deg, MN.pbc.line_deg);
    if (MN.sub_bad & 1) lg("xr: subtitles: SOMVR_XR_SUBTITLES=%s is not on or off: off", getenv("SOMVR_XR_SUBTITLES"));
    if (MN.sub_bad & 2) lg("xr: subtitles: SOMVR_XR_SUBTITLES_SCALE=%s is not 50..150 (%%): %d", getenv("SOMVR_XR_SUBTITLES_SCALE"),
                           SOMVR_SUB_SCALE_DEFAULT);
    if (MN.sub_bad & 4) lg("xr: subtitles: SOMVR_XR_SUBTITLES_RECT=%s is not one X,Y,W,H in 1920x1080: %s",
                           getenv("SOMVR_XR_SUBTITLES_RECT"), SOMVR_SUB_RECT_DEFAULT);
    lg("xr: subtitles: %s", !MN.sub ? "off (the subtitle line where the band's tiles leave it: build 21)"
       : !MN.pb ? "on, but no prompt band (SOMVR_XR_PROMPTS): off" : MN.sub_idx < 0 ? "on, but no room in the band: off" : "on");
    if (MN.sub_idx >= 0)
        lg("xr: subtitles: the rect %d,%d %dx%d a band row of its own (tile %d of the band, its last; %d tile%s cut to keep "
           "clear of it)%s at %d %% of the band's scale (a 24-px line %.2f deg)", MN.sub_set.r[0][0], MN.sub_set.r[0][1],
           MN.sub_set.r[0][2], MN.sub_set.r[0][3], MN.sub_idx, MN.sub_cut, MN.sub_cut == 1 ? "" : "s",
           MN.pp == SOMVR_PP_GAZE ? ": its own popup under the gaze" : ": in the band", MN.sub_scale,
           MN.pbc.line_deg * 24.0f / 18.0f * (float)MN.sub_scale * 0.01f);
    if (MN.pp_bad) lg("xr: popups: SOMVR_XR_POPUPS=%s is not gaze or band: band", getenv("SOMVR_XR_POPUPS"));
    if (MN.ppg_bad) lg("xr: popups: SOMVR_XR_POPUPS_GEOM=%s is not DOWN_DEG,TURN_DEG,EASE_MS (0..40, 10..120, 0..2000): "
                       "the defaults", getenv("SOMVR_XR_POPUPS_GEOM"));
    lg("xr: popups: %s", MN.pp != SOMVR_PP_GAZE ? "band (the prompt band's tiles in one quad, placed with the HUD)"
       : !MN.pb ? "gaze, but no prompt band (SOMVR_XR_PROMPTS): none" : cfg.hud == SOMVR_HUD_HEAD ? "gaze, but the HUD is "
       "head-locked (SOMVR_XR_HUD=head): the band as it is" : "gaze");
    if (MN.pp == SOMVR_PP_GAZE && MN.pb && cfg.hud != SOMVR_HUD_HEAD)
        lg("xr: popups: gaze: each of the band's tiles its own quad, shown while the HUD image has something in it (every "
           "%dth row read, alpha %d+ on %d texels), placed in front of the gaze as it comes up (its content %.0f deg under "
           "the gaze, %.0f..%+.0f deg from the level), still in the room while it shows, eased over %.2f s to the new front "
           "when the head turns more than %.0f deg from it, over %.1f s after it empties; one coming up over another goes "
           "under it", SOMVR_PP_STEP, SOMVR_PP_ALPHA, SOMVR_PP_MIN_PX, MN.ppc.down_deg, MN.ppc.pitch_min_deg,
           MN.ppc.pitch_max_deg, MN.ppc.ease_s, MN.ppc.turn_deg, MN.ppc.hold_s);
    lg("xr: card: %s; touch: %s (the fingertip %.1f cm past the aim pose; hover %.0f cm, press %.1f cm through after a front "
       "approach, release at %.1f cm; One Euro %.1f Hz + %.0f/(m/s))",
       MN.card ? (VM.on ? "the Controls page at the first arrival in the practice area" : "on, but no VR menu: none")
               : "off",
       MN.touch ? "on" : "off", MN.tcc.reach_m * 100, MN.tcc.hover_m * 100, MN.tcc.press_m * 100, MN.tcc.release_m * 100,
       MN.tcc.min_cutoff, MN.tcc.beta);
}

/* a recentre: every board is placed again at its next frame */
static void mn_replace(void) { MN.mono_latched = MN.hud_latched = 0; }

/* the head's two eye poses at this frame's display time (mono frames); 0 = not located */
static int mn_eyes(somvr_posef eye[2]) {
    XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO,
                           .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                           .displayTime = X.fs.predictedDisplayTime,
                           .space = X.space};
    XrViewState vs = {.type = XR_TYPE_VIEW_STATE};
    XrView v[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
    uint32_t nv = 0;
    const XrViewStateFlags need = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    XrResult r = X.xrLocateViews(X.session, &li, &vs, 2, &nv, v);
    if (XR_FAILED(r) || nv != 2 || (vs.viewStateFlags & need) != need) return 0;
    for (int e = 0; e < 2; e++) memcpy(&eye[e], &v[e].pose, sizeof eye[e]);
    return 1;
}

static void mn_qrot(const float q[4], const float v[3], float out[3]); /* (below) */
static void mn_log_pose(const char *what, const somvr_posef *p, float w, float h) {
    static const float ez[3] = {0, 0, 1};
    float n[3];
    mn_qrot(p->q, ez, n); /* its normal's pitch: the tilt toward the eyes (a board down the gaze tilts up) */
    lg("xr: menus: %s placed at %+.3f,%+.3f,%+.3f facing yaw %+.1f deg, %.3f x %.3f m (tilt %+.1f deg)", what, p->p[0],
       p->p[1], p->p[2], 2.0 * atan2(p->q[1], p->q[3]) * 57.29578, w, h, asinf(fmaxf(-1.f, fminf(1.f, n[1]))) * 57.29578);
}

/* the mono frame's menu quad (the whole game image): on the board instead of 2 m ahead of the space's origin */
static void mn_mono_quad(XrCompositionLayerQuad *q, uint32_t w, uint32_t h) {
    if (!MN.menu) return;
    if (!MN.mono_latched || MN.mono_stereo != X.n_stereo) { /* the first mono frame after the world: an open */
        somvr_posef eye[2];
        if (mn_eyes(eye)) {
            somvr_mn_board_pose_gaze2(eye, MN.mq[0], MN.mq[2], MN.pitch_max, MN.pitch_up, &MN.mono_pose);
            MN.mono_latched = 1, MN.n_mono_boards++;
            if (MN.lines++ < 100) mn_log_pose("the menu quad's board", &MN.mono_pose, MN.mq[1], MN.mq[1] * h / w);
        }
    }
    MN.mono_stereo = X.n_stereo;
    if (!MN.mono_latched) return; /* no head yet: the old place this frame */
    float sz[2];
    somvr_quad_size(w, h, MN.mq[1], sz);
    memcpy(&q->pose, &MN.mono_pose, sizeof q->pose);
    q->size.width = sz[0], q->size.height = sz[1];
}

/* the end of hud_quad: a game menu over the world on the board (placed at its open), the world HUD placed again ahead
 * of the head when it closes; the minimap's column never shows in it */
static void mn_set_kind(uint32_t kind) { MN.hud_kind = kind; }
static void mn_hud_quad(XrCompositionLayerQuad *q, const somvr_rec *rec) {
    int menu = MN.menu && (MN.hud_kind & SOMVR_HUDF_MENU);
    MN.hud_menu = menu;
    if (!menu) {
        if (MN.was_menu) X.hud_latched = 0; /* a menu closed: the world HUD ahead of the head again (next frame) */
        MN.was_menu = 0, MN.hud_latched = 0;
        return;
    }
    if (!MN.hud_latched || !MN.was_menu) {
        somvr_posef eye[2] = {rec->eye[0].pose, rec->eye[1].pose};
        somvr_mn_board_pose_gaze2(eye, MN.mq[0], MN.mq[2], MN.pitch_max, MN.pitch_up, &MN.hud_pose);
        MN.hud_latched = 1, MN.n_hud_boards++;
        if (MN.lines++ < 100) mn_log_pose("a game menu's board (the HUD quad)", &MN.hud_pose, MN.mq[1],
                                          MN.mq[1] * (float)X.hud_h / (float)X.hud_w);
    }
    MN.was_menu = 1, MN.n_menu_frames++;
    float sz[2];
    somvr_quad_size(X.hud_w, X.hud_h, MN.mq[1], sz);
    memcpy(&q->pose, &MN.hud_pose, sizeof q->pose);
    q->space = X.space;
    q->size.width = sz[0], q->size.height = sz[1];
}
static int mn_hud_menu(void) { return MN.hud_menu; }

/* the off hand (the watch's, the wrist panel's): the right one when the menu's main hand is the left */
static int mn_off_hand(void) { return VM.on && VM.main_hand == SOMVR_HAND_L ? SOMVR_HAND_R : SOMVR_HAND_L; }
/* this frame's layout of the tiles for a w x h HUD image (the watch's column on the off hand's elbow side); 0 = none */
static int mn_layout(uint32_t w, uint32_t h) {
    if (MN.lay_w != w || MN.lay_h != h) { /* the rects in this image's pixels */
        MN.lay_w = w, MN.lay_h = h;
        int ok = MN.mm && somvr_mm_rect_for(MN.mm_rect, w, h, MN.px);
        if (!ok) MN.px[2] = 0;
        somvr_ht_scale(&MN.wh_set, w, h, &MN.wh_px), somvr_ht_scale(&MN.wh_clear, w, h, &MN.wc_px);
        somvr_ht_scale(&MN.pb_set, w, h, &MN.pb_px);
    }
    const int *mm = MN.mm && MN.px[2] ? MN.px : NULL;
    return somvr_hud_layout_make(w, h, mm, MN.wh ? &MN.wh_px : NULL, mn_off_hand() == SOMVR_HAND_R, &MN.wc_px,
                                 MN.pb ? &MN.pb_px : NULL, &MN.lay);
}
/* the HUD swapchain's extra width for a w x h HUD image: the tiles' area (0 = none; the same for either wrist) */
static uint32_t mn_hud_extra(uint32_t w, uint32_t h) {
    MN.extra = 0, MN.lay_w = MN.lay_h = 0;
    if ((MN.mm || MN.pb) && mn_layout(w, h)) MN.extra = MN.lay.extra_w;
    return MN.extra;
}

/* eyes_submit: the HUD image's regions into the HUD swapchain image. 0 = one plain copy (the extra area, if any, is
 * left as it was); n = a clear of the whole image first, then these n regions (the tiles moved into the extra area) */
static int mn_hud_regions(const struct dev_st *ds, const hud_st *hd, VkImageCopy *c, int cap) {
    MN.split = 0;
    if (!MN.extra || !ds->CmdClearColorImage || (MN.menu && (hd->kind & SOMVR_HUDF_MENU)) ||
        (hd->kind & 0xffu) == SOMVR_HUDK_VIDEO || hd->w != X.hud_w || hd->h != X.hud_h || !mn_layout(hd->w, hd->h) ||
        MN.lay.extra_w > MN.extra)
        return 0;
    int32_t r[SOMVR_HUD_SPLIT_MAX][6];
    int n = somvr_hud_split(hd->w, hd->h, &MN.lay, r, cap < SOMVR_HUD_SPLIT_MAX ? cap : SOMVR_HUD_SPLIT_MAX);
    if (!n) return 0;
    VkImageSubresourceLayers sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    for (int i = 0; i < n; i++)
        c[i] = (VkImageCopy){.srcSubresource = sub, .srcOffset = {r[i][0], r[i][1], 0}, .dstSubresource = sub,
                             .dstOffset = {r[i][2], r[i][3], 0}, .extent = {(uint32_t)r[i][4], (uint32_t)r[i][5], 1}};
    MN.split = 1, MN.n_split++;
    return n;
}

/* ... the clear before them (the image in TRANSFER_DST_OPTIMAL; the copies wait for it) */
static void mn_hud_clear(struct dev_st *ds, VkCommandBuffer cmd, VkImage img) {
    VkClearColorValue zero = {.float32 = {0, 0, 0, 0}};
    VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    ds->CmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
    VkImageMemoryBarrier b;
    img_barrier(&b, img, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

static void mn_qrot(const float q[4], const float v[3], float out[3]) { /* v rotated by the unit quaternion q */
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}
/* the stereo frame's watch face (after hud_quad): the column of the HUD swapchain on the off hand's wrist while it
 * faces the eyes; NULL = none this frame */
static const XrCompositionLayerBaseHeader *mn_mm_layer(const somvr_rec *rec) {
    if (!MN.split || !MN.lay.watch[2] || !X.hud_sc || !I.on) return NULL;
    int h = mn_off_hand(); /* (the panel's) */
    const somvr_hand_in *hp = &I.st.hand[h];
    somvr_posef face;
    int wrist = vh_watch_on(); /* build 10: SOMVR_XR_WATCH=wrist (the hand model's wrist, the debounced facing) */
    if (!(hp->active & SOMVR_HACT_POSE) || (hp->grip_flags & 3u) != 3u ||
        !(wrist ? vh_watch_pose(h, &hp->grip, &face) : somvr_mm_pose(&hp->grip, h, &MN.mmc, &face))) {
        MN.shown = 0;
        if (wrist) vh_watch_reset();
        return NULL;
    }
    float eye[3];
    for (int i = 0; i < 3; i++) eye[i] = 0.5f * (rec->eye[0].pose.p[i] + rec->eye[1].pose.p[i]);
    int was = MN.shown;
    MN.shown = wrist ? vh_watch_facing(&face, eye) : somvr_mm_facing(&face, eye, &MN.mmc, was);
    if (MN.shown != was && MN.lines++ < 100)
        lg("xr: minimap: the watch face %s (frame %llu, the %s wrist at %+.3f,%+.3f,%+.3f)", MN.shown ? "seen" : "away",
           (unsigned long long)rec->frame, h == SOMVR_HAND_L ? "left" : "right", face.p[0], face.p[1], face.p[2]);
    if (!MN.shown) return NULL;
    MN.n_shown++;
    /* the watch's region (build 8: the column beside the map); the map's centre stays at the face's place */
    const int *wr = MN.lay.watch, *mp = MN.lay.mm;
    float k = MN.mmc.size_m / (float)mp[2]; /* metres per pixel */
    float dx = k * ((float)wr[0] + 0.5f * (float)wr[2] - (float)mp[0] - 0.5f * (float)mp[2]);
    float dy = -k * ((float)wr[1] + 0.5f * (float)wr[3] - (float)mp[1] - 0.5f * (float)mp[3]);
    float ax[3], ay[3];
    static const float ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0};
    mn_qrot(face.q, ex, ax), mn_qrot(face.q, ey, ay);
    for (int i = 0; i < 3; i++) face.p[i] += dx * ax[i] + dy * ay[i];
    MN.q = (XrCompositionLayerQuad){
        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                      (cfg.hud_alpha == SOMVR_HUD_STRAIGHT ? XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT : 0),
        .space = X.space,
        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
        .subImage = {X.hud_sc, {{wr[0], wr[1]}, {wr[2], wr[3]}}, 0},
        .size = {k * (float)wr[2], k * (float)wr[3]}};
    memcpy(&MN.q.pose, &face, sizeof MN.q.pose);
    return (const XrCompositionLayerBaseHeader *)&MN.q;
}
/* a reuse frame repeats the last stereo frame's watch face (its image is the HUD swapchain's last released one) */
static const XrCompositionLayerBaseHeader *mn_mm_reuse(void) {
    return MN.split && MN.shown ? (const XrCompositionLayerBaseHeader *)&MN.q : NULL;
}

/* ---- build 8: the prompt band ------------------------------------------------------------------------------------ */
/* its distance and angle: SOMVR_XR_PROMPTS_QUAD's, the distance scaled like the HUD's (the VR menu's HUD distance) */
static void mn_band_geom(float *dist, float *down) {
    float g[3];
    vm_hud_geom(g);
    float k = cfg.hud_quad[0] > 0 && g[0] > 0 ? g[0] / cfg.hud_quad[0] : 1;
    *dist = MN.pbc.dist_m * k, *down = MN.pbc.down_deg * 0.01745329f;
}
/* hud_quad, when it places the world-locked HUD quad: the band from the same eyes (their yaw, down_deg below them) */
static void mn_band_place(const somvr_posef eye[2]) {
    if (!MN.pb) return;
    float d, a;
    mn_band_geom(&d, &a);
    somvr_mn_board_pose(eye, d * cosf(a), d * sinf(a), &MN.band_pose);
    MN.band_placed = 1;
    if (MN.lines++ < 100) mn_log_pose("the prompt band", &MN.band_pose, somvr_pb_width(MN.lay.band[2], MN.pbc.line_deg, d),
                                      somvr_pb_width(MN.lay.band[3], MN.pbc.line_deg, d));
}
/* the stereo frame's band (after hud_quad): the extra area's band region, world-locked where it was placed (SOMVR_XR_HUD
 * =head: in the VIEW space, down_deg below the view); NULL = none this frame (a menu, a video, no band) */
static const XrCompositionLayerBaseHeader *mn_band_layer(void) {
    int was = MN.band_shown;
    MN.band_shown = 0;
    if (!MN.pb || !MN.split || !MN.lay.band[2] || !X.hud_sc || (cfg.hud != SOMVR_HUD_HEAD && !MN.band_placed)) return NULL;
    const int *b = MN.lay.band;
    float d, a;
    mn_band_geom(&d, &a);
    float wm = somvr_pb_width(b[2], MN.pbc.line_deg, d);
    MN.bq = (XrCompositionLayerQuad){
        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                      (cfg.hud_alpha == SOMVR_HUD_STRAIGHT ? XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT : 0),
        .space = X.space,
        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
        .subImage = {X.hud_sc, {{b[0], b[1]}, {b[2], b[3]}}, 0},
        .size = {wm, wm * (float)b[3] / (float)b[2]}};
    if (cfg.hud == SOMVR_HUD_HEAD) {
        MN.bq.space = X.view_space;
        MN.bq.pose = (XrPosef){{-sinf(0.5f * a), 0, 0, cosf(0.5f * a)}, {0, -d * sinf(a), -d * cosf(a)}};
    } else {
        memcpy(&MN.bq.pose, &MN.band_pose, sizeof MN.bq.pose);
    }
    MN.band_shown = 1, MN.n_band++;
    if (!was && MN.lines++ < 100)
        lg("xr: prompts: the band shown (frame %llu): %.3f x %.3f m, %d x %d px of the extra area at %d,%d, %.2f m away",
           (unsigned long long)X.n_stereo, MN.bq.size.width, MN.bq.size.height, b[2], b[3], b[0], b[1], d);
    return (const XrCompositionLayerBaseHeader *)&MN.bq;
}
static const XrCompositionLayerBaseHeader *mn_band_reuse(void) {
    return MN.band_shown ? (const XrCompositionLayerBaseHeader *)&MN.bq : NULL;
}

/* ---- build 21: the popups where the player looks (SOMVR_XR_POPUPS=gaze) ------------------------------------------- */
_Static_assert(XL_BAND4 - XL_BAND + 1 == SOMVR_PP_MAX, "a layer slot per popup tile");
#define MN_PP_NE 4 /* readings in flight at once */
/* the band's tiles placed on their own this frame: their count (0 = the band as one quad: off, a head-locked HUD, a
 * menu, more tiles than SOMVR_PP_MAX, no readback) and the first one's index in MN.lay */
static int mn_pp_tiles(int *first) {
    int nb = MN.pb ? MN.pb_px.n : 0;
    if (MN.pp != SOMVR_PP_GAZE || !MN.pb || cfg.hud == SOMVR_HUD_HEAD || MN.pp_dead || !MN.lay.band[2] || nb < 1 ||
        nb > SOMVR_PP_MAX || MN.lay.ntile < nb)
        return 0;
    *first = MN.lay.ntile - nb;
    return nb;
}
static void mn_pp_free(struct dev_st *ds) {
    if (ds && MN.pp_map) ds->UnmapMemory(ds->dev, MN.pp_mem);
    if (ds && MN.pp_buf) ds->DestroyBuffer(ds->dev, MN.pp_buf, NULL);
    if (ds && MN.pp_mem) ds->FreeMemory(ds->dev, MN.pp_mem, NULL);
    MN.pp_map = NULL, MN.pp_buf = VK_NULL_HANDLE, MN.pp_mem = VK_NULL_HANDLE, MN.pp_ds = NULL;
    memset(MN.pp_e, 0, sizeof MN.pp_e);
}
static void pp_destroy(void) { mn_pp_free(MN.pp_ds); } /* xr_teardown_session, after sc_wait_all */
/* the readback buffer for entries of esz bytes (made at the first reading); 0 = ready */
static int mn_pp_buf(struct dev_st *ds, VkDeviceSize esz) {
    if (MN.pp_map) return MN.pp_ds == ds && esz <= MN.pp_esz ? 0 : -1;
    if (MN.pp_dead) return -1;
    MN.pp_dead = 1;
    const char *what = "the device lacks the readback's functions";
    if (ds->CmdCopyImageToBuffer && ds->CreateBuffer && ds->DestroyBuffer && ds->GetBufferMemoryRequirements &&
        ds->AllocateMemory && ds->FreeMemory && ds->BindBufferMemory && ds->MapMemory && ds->UnmapMemory &&
        ds->is->GetPhysicalDeviceMemoryProperties) {
        VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = esz * MN_PP_NE,
                                  .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        what = "vkCreateBuffer";
        if (ds->CreateBuffer(ds->dev, &bci, NULL, &MN.pp_buf) != VK_SUCCESS) MN.pp_buf = VK_NULL_HANDLE;
        else {
            VkMemoryRequirements mr;
            VkPhysicalDeviceMemoryProperties mp;
            ds->GetBufferMemoryRequirements(ds->dev, MN.pp_buf, &mr);
            ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
            int t = somvr_pick_host_memory(&mp, mr.memoryTypeBits);
            VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                       .memoryTypeIndex = (uint32_t)t};
            void *m = NULL;
            what = "no HOST_VISIBLE|HOST_COHERENT memory type";
            if (t >= 0 && (what = "vkAllocateMemory", ds->AllocateMemory(ds->dev, &ai, NULL, &MN.pp_mem)) != VK_SUCCESS)
                MN.pp_mem = VK_NULL_HANDLE;
            else if (t >= 0 && (what = "vkBindBufferMemory", ds->BindBufferMemory(ds->dev, MN.pp_buf, MN.pp_mem, 0)) ==
                                   VK_SUCCESS &&
                     (what = "vkMapMemory", ds->MapMemory(ds->dev, MN.pp_mem, 0, VK_WHOLE_SIZE, 0, &m)) == VK_SUCCESS) {
                MN.pp_map = m, MN.pp_ds = ds, MN.pp_dead = 0, MN.pp_esz = esz;
                memset(m, 0, (size_t)(esz * MN_PP_NE));
                return 0;
            }
        }
    }
    lg("xr: popups: no readback this session: %s (the band as one quad, placed with the HUD)", what);
    mn_pp_free(ds);
    return -1;
}
/* eyes_submit, after the HUD's copy (the HUD image in TRANSFER_SRC_OPTIMAL): every SOMVR_PP_STEP-th row of each band
 * tile into a free entry */
static void pp_cmds(struct dev_st *ds, VkCommandBuffer cmd, const slot_st *sl, const hud_st *hd) {
    MN.pp_armed = -1;
    int first = 0, n = mn_pp_tiles(&first);
    if (!n || !MN.split || (hd->kind & 0xffu) != SOMVR_HUDK_WORLD || hd->w != MN.lay_w || hd->h != MN.lay_h) return;
    if (!(hd->fmt == VK_FORMAT_R8G8B8A8_UNORM || hd->fmt == VK_FORMAT_R8G8B8A8_SRGB || hd->fmt == VK_FORMAT_B8G8R8A8_UNORM ||
          hd->fmt == VK_FORMAT_B8G8R8A8_SRGB || hd->fmt == VK_FORMAT_A8B8G8R8_UNORM_PACK32 ||
          hd->fmt == VK_FORMAT_A8B8G8R8_SRGB_PACK32)) {
        if (!MN.pp_dead) lg("xr: popups: no readback this session: the HUD image's format %d (not 8-bit RGBA/BGRA): the band "
                            "as one quad", hd->fmt);
        MN.pp_dead = 1;
        return;
    }
    VkDeviceSize esz = 0;
    int rows = 0;
    for (int i = 0; i < n; i++) {
        const int *r = MN.lay.src[first + i];
        esz += (VkDeviceSize)r[2] * (VkDeviceSize)(r[3] / SOMVR_PP_STEP) * 4, rows += r[3] / SOMVR_PP_STEP;
    }
    if (!esz || rows > 1024 || mn_pp_buf(ds, esz)) return;
    int k = -1;
    for (int i = 0; i < MN_PP_NE && k < 0; i++)
        if (!MN.pp_e[i].sl) k = i;
    if (k < 0) return; /* all in flight: no reading this frame */
    VkBufferImageCopy reg[1024];
    int nr = 0;
    VkDeviceSize off = (VkDeviceSize)k * MN.pp_esz;
    for (int i = 0; i < n; i++) {
        const int *r = MN.lay.src[first + i];
        int nrow = r[3] / SOMVR_PP_STEP;
        MN.pp_e[k].w[i] = r[2], MN.pp_e[k].nrow[i] = nrow, MN.pp_e[k].off[i] = off;
        for (int j = 0; j < nrow; j++, off += (VkDeviceSize)r[2] * 4)
            reg[nr++] = (VkBufferImageCopy){.bufferOffset = off, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                            .imageOffset = {r[0], r[1] + j * SOMVR_PP_STEP + SOMVR_PP_STEP / 2, 0},
                                            .imageExtent = {(uint32_t)r[2], 1, 1}};
    }
    ds->CmdCopyImageToBuffer(cmd, hd->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, MN.pp_buf, (uint32_t)nr, reg);
    VkMemoryBarrier hb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                          .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
    MN.pp_e[k].sl = sl, MN.pp_e[k].seq = ++MN.pp_sub, MN.pp_e[k].n = n;
    MN.pp_armed = k;
}
/* eyes_submit's end: its submit went (ok) or not (the entry is free again) */
static void pp_submitted(int ok) {
    if (MN.pp_armed >= 0 && !ok) MN.pp_e[MN.pp_armed].sl = NULL;
    MN.pp_armed = -1;
}
/* entry i's submit is done: each tile's reading (an older one than the last taken is dropped) */
static void mn_pp_take(int i) {
    uint64_t seq = MN.pp_e[i].seq;
    MN.pp_e[i].sl = NULL;
    if (!MN.pp_map) return;
    if (seq <= MN.pp_seq_read) {
        MN.pp_nlate++;
        return;
    }
    MN.pp_seq_read = seq, MN.pp_nread++;
    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    for (int t = 0; t < MN.pp_e[i].n && t < SOMVR_PP_MAX; t++) {
        int was = MN.pp_rd[t].present;
        MN.pp_rd[t].present = somvr_pp_measure(MN.pp_map + MN.pp_e[i].off[t], MN.pp_e[i].w[t], MN.pp_e[i].nrow[t],
                                               SOMVR_PP_STEP, MN.pp_rd[t].bb);
        if (MN.pp_rd[t].present != was && MN.pp_lines++ < 300) {
            if (MN.pp_rd[t].present)
                lg("xr: popups: tile %d has something (reading %llu: its box %d,%d..%d,%d)", t,
                   (unsigned long long)MN.pp_nread, MN.pp_rd[t].bb[0], MN.pp_rd[t].bb[1], MN.pp_rd[t].bb[2],
                   MN.pp_rd[t].bb[3]);
            else
                lg("xr: popups: tile %d empty (reading %llu)", t, (unsigned long long)MN.pp_nread);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    double us = (double)(ts1.tv_sec - ts0.tv_sec) * 1e6 + (double)(ts1.tv_nsec - ts0.tv_nsec) * 1e-3;
    if (us > MN.pp_us_max) MN.pp_us_max = us;
    if (MN.pp_nread <= 2 || MN.pp_nread % 1800 == 0)
        lg("xr: popups: reading %llu (%d tiles, %llu late; its measure %.0f us, the most so far %.0f us)",
           (unsigned long long)MN.pp_nread, MN.pp_e[i].n, (unsigned long long)MN.pp_nlate, us, MN.pp_us_max);
}
/* slot_begin (its fence waited) and sc_free_slots: the readings that slot's last submit carried; idle = 0: dropped */
static void pp_slot_done(const slot_st *sl, int idle) {
    for (int i = 0; i < MN_PP_NE; i++)
        if (MN.pp_e[i].sl == sl) {
            if (idle) mn_pp_take(i);
            else MN.pp_e[i].sl = NULL;
        }
}
/* xr_frame, every present: the readings whose submits are done (their fences) */
static void pp_poll(struct dev_st *ds) {
    if (MN.pp != SOMVR_PP_GAZE || !ds->GetFenceStatus) return;
    for (int i = 0; i < MN_PP_NE; i++)
        if (MN.pp_e[i].sl && ds->GetFenceStatus(ds->dev, MN.pp_e[i].sl->fence) == VK_SUCCESS) mn_pp_take(i);
}
/* the stereo frame's band (after hud_quad): with gaze each tile's own quad where it was placed (XL_BAND + i), else the
 * band as one quad */
static void mn_band_layers(xr_comp *c, const somvr_posef eye[2]) {
    int first = 0, n = mn_pp_tiles(&first);
    MN.pp_has = 0;
    if (!n || !MN.split || !X.hud_sc) {
        c->l[XL_BAND] = mn_band_layer();
        return;
    }
    MN.band_shown = 0;
    double t = (double)X.fs.predictedDisplayTime * 1e-9;
    if (MN.pp_t > 0 && t - MN.pp_t > 0.5) /* a menu, a video, the cover between: afresh */
        somvr_pp_reset(&MN.pps), memset(MN.pp_rd, 0, sizeof MN.pp_rd);
    MN.pp_t = t;
    float d, a;
    mn_band_geom(&d, &a);
    float k = somvr_pb_width(MN.lay.band[2], MN.pbc.line_deg, d) / (float)MN.lay.band[2]; /* the band's metres a pixel */
    float sz[SOMVR_PP_MAX][2];
    int px[SOMVR_PP_MAX][2];
    for (int i = 0; i < n; i++) {
        const int *r = MN.lay.src[first + i];
        float ki = MN.sub_idx >= 0 && i == n - 1 ? k * (float)MN.sub_scale * 0.01f : k; /* build 22: the subtitles' scale */
        px[i][0] = r[2], px[i][1] = r[3], sz[i][0] = ki * (float)r[2], sz[i][1] = ki * (float)r[3];
    }
    uint32_t ev = somvr_pp_step(&MN.pps, &MN.ppc, t, eye, d, n, MN.pp_rd, (const float(*)[2])sz, (const int(*)[2])px);
    MN.n_pp_frames++;
    float gy, gp, mid[3];
    somvr_pp_gaze(eye, &gy, &gp, mid);
    for (int i = 0; i < n; i++) {
        const somvr_pp_tile *tl = &MN.pps.t[i];
        if (ev & (1u << i)) MN.n_pp_placed++;
        if (ev & (1u << (8 + i))) MN.n_pp_eased++;
        if (ev & (1u << (16 + i))) MN.n_pp_over++;
        if ((ev & (0x10101u << i)) && MN.pp_lines++ < 300) {
            if (ev & (1u << i))
                lg("xr: popups: tile %d placed (stereo frame %llu): its content at yaw %+.1f pitch %+.1f deg from the eyes "
                   "(the gaze %+.1f %+.1f), %.2f m away, %.1f x %.1f deg%s", i, (unsigned long long)X.n_stereo,
                   tl->yaw * 57.29578f, (tl->pitch - tl->dpitch) * 57.29578f, gy * 57.29578f, gp * 57.29578f, d,
                   tl->wrad * 57.29578f, tl->hrad * 57.29578f, tl->under >= 0 ? ", under another" : "");
            if (ev & (1u << (8 + i)))
                lg("xr: popups: tile %d eases to the new front (stereo frame %llu): the head %.0f deg from it (yaw %+.1f -> "
                   "%+.1f over %.2f s)", i, (unsigned long long)X.n_stereo,
                   fabsf(remainderf(gy - tl->e_from[0], 6.2831853f)) * 57.29578f, tl->e_from[0] * 57.29578f,
                   tl->e_to[0] * 57.29578f, MN.ppc.ease_s);
            if (ev & (1u << (16 + i)))
                lg("xr: popups: tile %d over (stereo frame %llu): empty for %.1f s, shown %.1f s", i,
                   (unsigned long long)X.n_stereo, MN.ppc.hold_s, t - tl->shown_t);
        }
        if (!tl->shown) continue;
        const int *dst = MN.lay.dst[first + i];
        MN.pq[i] = (XrCompositionLayerQuad){
            .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
            .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                          (cfg.hud_alpha == SOMVR_HUD_STRAIGHT ? XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT : 0),
            .space = X.space,
            .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
            .subImage = {X.hud_sc, {{dst[0], dst[1]}, {px[i][0], px[i][1]}}, 0},
            .size = {sz[i][0], sz[i][1]}};
        somvr_posef pose;
        somvr_pp_pose(tl, d, &pose);
        memcpy(&MN.pq[i].pose, &pose, sizeof MN.pq[i].pose);
        c->l[XL_BAND + i] = (const XrCompositionLayerBaseHeader *)&MN.pq[i];
        MN.pp_has |= 1u << i;
    }
    if (MN.pp_has) MN.n_band++;
}
/* a reuse frame: the last stereo frame's band (or popups) again */
static void mn_band_reuse_layers(xr_comp *c) {
    if (!MN.pp_has) {
        c->l[XL_BAND] = mn_band_reuse();
        return;
    }
    for (int i = 0; i < SOMVR_PP_MAX; i++)
        if (MN.pp_has >> i & 1) c->l[XL_BAND + i] = (const XrCompositionLayerBaseHeader *)&MN.pq[i];
}
/* xr_end_comp: at most the system's layer count (xrGetSystemProperties; the spec's 16): a frame with more (every part
 * at once: 17 slots) goes without the band */
static void xl_cap(xr_comp *c) {
    uint32_t n = 0, max = X.max_layers ? X.max_layers : XL_MAX, nb = 0;
    for (int i = 0; c->l[XL_OWN] && i < XL_N; i++) n += c->l[i] != NULL;
    for (int i = XL_BAND; i <= XL_BAND4; i++) nb += c->l[i] != NULL; /* build 21: the popups' four; build 22: five */
    if (n > max + nb && c->l[XL_HANDS]) /* build 10: then our hands (the game's are hidden) */
        c->l[XL_HANDS] = NULL, n--;
    for (int i = XL_MBD; i <= XL_MBK && n > max + nb; i++) /* then the menus' backdrop */
        if (c->l[i]) c->l[i] = NULL, n--;
    if (n > max + nb && c->l[XL_OBJL]) /* build 17: then the drop's line (its ring stays) */
        c->l[XL_OBJL] = NULL, n--;
    if (n > max + nb && c->l[XL_OBJ]) /* build 15: then the objective's marker */
        c->l[XL_OBJ] = NULL, n--;
    if (n <= max || !nb) return;
    for (int i = XL_BAND; i <= XL_BAND4; i++) c->l[i] = NULL;
    if (MN.n_capped++ < 3 || MN.n_capped % 600 == 0)
        lg("xr: prompts: a frame of %u layers (the runtime takes %u): without the band (%llu so far)", n, max,
           (unsigned long long)MN.n_capped);
}
/* vm_frame: the open wrist panel's hand h turned over so that the watch faces the eyes (the panel would be seen from
 * behind, mirrored, over the watch: run 162428): 1 = close the panel. The watch's own facing rule (66 deg), never with
 * the palm toward the eyes (the panel's 45 deg). Only with the wrist minimap (its column in the HUD swapchain). */
static int mn_watch_over(int h, const somvr_vm_gin *gi) {
    if (!MN.mm || !MN.extra || !gi->hand_ok || !gi->head_ok) return 0;
    somvr_posef face;
    if (vh_watch_on()) /* build 10: the watch on the wrist (its own facing angle, undebounced: the panel's rule) */
        return vh_watch_pose(h, &gi->grip, &face) && somvr_mm_facing(&face, gi->head.p, &MN.mmc, 0);
    return somvr_mm_pose(&gi->grip, h, &MN.mmc, &face) && somvr_mm_facing(&face, gi->head.p, &MN.mmc, 0);
}

/* ---- touch ---------------------------------------------------------------------------------------------------- */
/* hand h may poke: its aim pose located, the index off the trigger (untouched, not pulled) */
static int mn_pointing(int h) {
    const somvr_hand_in *hp = &I.st.hand[h];
    if (!I.on || !(hp->active & SOMVR_HACT_POSE) || (hp->aim_flags & 3u) != 3u) return 0;
    if (!(hp->active & SOMVR_HACT_INPUT)) return 1;
    return !(hp->buttons & SOMVR_BTN_TRIGGER_TOUCH) && hp->trigger < 0.1f;
}
static void mn_tc_in(somvr_tc_in *ti, uint32_t hands, uint32_t quad, const somvr_posef *qp, const float qs[2]) {
    memset(ti, 0, sizeof *ti);
    ti->t = (double)(I.st.time ? I.st.time : X.fs.predictedDisplayTime) * 1e-9;
    for (int h = 0; h < 2; h++) {
        ti->ok[h] = (hands >> h & 1u) && mn_pointing(h);
        if (ti->ok[h] && !vh_touch_tip(h, ti->tip[h])) /* build 10: our drawn index tip, while we draw the hands */
            somvr_tc_tip(&I.st.hand[h].aim, MN.tcc.reach_m, ti->tip[h]);
    }
    ti->quad = quad;
    if (quad) memcpy(&ti->quad_pose, qp, sizeof ti->quad_pose), ti->quad_size[0] = qs[0], ti->quad_size[1] = qs[1];
}
/* the fingertip as the pointer: o rewritten while it hovers or presses (the ray's own edges dropped) */
static void mn_touch_to_ptr(const somvr_tc_out *t, somvr_ptr_out *o) {
    uint32_t press = (t->edges & SOMVR_TCE_PRESS) ? SOMVR_PTRE_PRESS : 0u;
    uint32_t rel = (t->edges & SOMVR_TCE_RELEASE) ? SOMVR_PTRE_RELEASE : 0u;
    o->flags = SOMVR_PTRF_ON | (t->down ? SOMVR_PTRF_DOWN : 0u);
    o->edges = press | rel | ((t->edges & SOMVR_TCE_HOVER) ? SOMVR_PTRE_ENTER : 0u);
    o->hand = t->hand;
    o->uv[0] = t->uv[0], o->uv[1] = t->uv[1];
    memcpy(o->hit, t->surf, sizeof o->hit);
    memcpy(o->origin, t->tip, sizeof o->origin);
    o->dist = t->depth > 0 ? t->depth : 0;
}
/* the marker quads (the pointer image's dot): the touching fingertip, its point on the surface; n of them into q */
static uint32_t mn_touch_marks(const somvr_tc_out *t, const somvr_posef *quad, const float *eye, XrCompositionLayerQuad *q) {
    if (!X.ptr_draw || !X.ptr_sc || !t->on) return 0;
    somvr_posef mp;
    memcpy(mp.q, quad->q, sizeof mp.q); /* turned like the surface (the eye sees it from the front) */
    (void)eye;
    memcpy(mp.p, t->tip, sizeof mp.p);
    q[0] = ptr_layer(0, &mp, MN_MARK_M, MN_MARK_M);
    memcpy(mp.p, t->surf, sizeof mp.p);
    float d = t->down ? 0.012f : 0.006f + 0.2f * (t->depth > 0 ? t->depth : 0); /* closer = smaller, pressed = big */
    q[1] = ptr_layer(0, &mp, d, d);
    return 2;
}

/* ptr_frame: a fingertip on the game's menu (the menu quad or the HUD board) wins over the ray; 1 = it does (o is
 * the fingertip's; *nq marker layers in q) */
static int mn_ptr_touch(uint32_t quad, const somvr_ptr_in *in, somvr_ptr_out *o, const float *eye,
                        XrCompositionLayerQuad *q, uint32_t *nq) {
    if (!MN.touch) return 0;
    somvr_tc_in ti;
    mn_tc_in(&ti, 3u, quad, &in->quad_pose, in->quad_size);
    if (quad != SOMVR_PTRQ_MENU && !(quad == SOMVR_PTRQ_HUD && MN.hud_menu)) ti.quad = 0; /* a world HUD: no touch */
    somvr_tc_out t;
    somvr_tc_update(&MN.tg, &MN.tcc, &ti, &t);
    if (t.edges & SOMVR_TCE_PRESS) MN.n_press_g++;
    if (t.edges && MN.lines++ < 300)
        lg("xr: touch: %s edges 0x%x hand %c uv %.3f,%.3f depth %+.3f m (presses %llu)", quad == SOMVR_PTRQ_HUD ? "the menu "
           "board" : "the menu quad", t.edges, t.hand >= 0 ? "LR"[t.hand] : '-', t.uv[0], t.uv[1], t.depth,
           (unsigned long long)MN.n_press_g);
    if (!t.on && !(t.edges & SOMVR_TCE_RELEASE)) return 0;
    mn_touch_to_ptr(&t, o);
    *nq = mn_touch_marks(&t, &in->quad_pose, eye, q);
    return 1;
}
/* the published click counters: the ray's and the fingertip's */
static uint32_t mn_downs(void) { return MN.tg.downs; }
static uint32_t mn_ups(void) { return MN.tg.ups; }

/* vm_frame: the main hand's fingertip on the wrist panel (or either on the card); 1 = it is the pointer now (o) */
static int mn_vm_touch(int card, const somvr_posef *panel, const float size[2], somvr_ptr_out *o) {
    MN.vtouch = 0;
    MN.panel = *panel, MN.panel_sz[0] = size[0], MN.panel_sz[1] = size[1], MN.panel_n = X.n_stereo + 1;
    if (!MN.touch) return 0;
    somvr_tc_in ti;
    mn_tc_in(&ti, card ? 3u : 1u << (VM.main_hand & 1), card ? 2u : 1u, panel, size);
    somvr_tc_update(&MN.tv, &MN.tcc, &ti, &MN.tvo);
    if (MN.tvo.edges & SOMVR_TCE_PRESS) MN.n_press_v++;
    if (!MN.tvo.on && !(MN.tvo.edges & SOMVR_TCE_RELEASE)) return 0;
    mn_touch_to_ptr(&MN.tvo, o);
    MN.vtouch = 1;
    return 1;
}
static void mn_vm_reset_touch(void) {
    somvr_tc_in ti;
    memset(&ti, 0, sizeof ti);
    somvr_tc_update(&MN.tv, &MN.tcc, &ti, &MN.tvo);
    MN.vtouch = 0;
}

/* ---- the card --------------------------------------------------------------------------------------------------- */
static void mn_card_close(const char *why) {
    MN.card_state = 2;
    lg("xr: card: put away (%s)", why);
}
/* vm_frame, after the gesture (ev: it opened +1 / closed -1 the wrist panel): 1 = the card is shown this frame at
 * *panel (*have = 1) */
static int mn_card(int ev, const somvr_vm_gin *gi, somvr_posef *panel, int *have) {
    if (!MN.card || !VM.on || MN.card_state == 2) return 0;
    if (MN.card_state == 0) {
        uint32_t ps = (VM.obj->avail & SOMVR_VMA_PRACTICE) ? atomic_load_explicit(&VM.obj->pstate, memory_order_acquire) : 0;
        if (!(ps & SOMVR_VMA_PR_HERE) || (ps & SOMVR_VMA_PR_BUSY) || !gi->head_ok || VM.gs.open) return 0;
        somvr_posef eye[2] = {gi->head, gi->head}; /* (vm_head: the views' midpoint, view 0's orientation) */
        somvr_mn_board_pose(eye, MN_CARD_DIST_M, MN_CARD_DROP_M, &MN.card_pose);
        MN.card_state = 1;
        MN.a_presses = I.st.hand[SOMVR_HAND_R].presses[0];
        MN.sq_low = 0;
        mn_log_pose("the controls card (the arrival in the practice area)", &MN.card_pose, MN_CARD_W_M,
                    MN_CARD_W_M * SOMVR_VM_H_M / SOMVR_VM_W_M);
        lg("xr: card: shown (the Controls page; its X box, A, the wrist menu or a grip put it away)");
    }
    if (ev > 0 || VM.gs.open) {
        mn_card_close("the wrist menu opened");
        return 0;
    }
    if (I.on && I.st.hand[SOMVR_HAND_R].presses[0] != MN.a_presses) {
        mn_card_close("A");
        return 0;
    }
    /* a grip squeezed (let go first): the player draws the sword or grabs; the card holds the blade off (vm_open) */
    for (int h = 0; I.on && h < 2; h++) {
        float sq = I.st.hand[h].squeeze;
        if (sq < 0.4f)
            MN.sq_low |= (uint8_t)(1u << h);
        else if (sq > 0.7f && (MN.sq_low >> h & 1u)) {
            mn_card_close(h == SOMVR_HAND_R ? "the right grip squeezed" : "the left grip squeezed");
            return 0;
        }
    }
    *panel = MN.card_pose, *have = 1;
    return 1;
}
static void mn_card_size(int card, float out[2]) {
    float k = card ? MN_CARD_W_M / SOMVR_VM_W_M : 1.0f;
    out[0] = SOMVR_VM_W_M * k, out[1] = SOMVR_VM_H_M * k;
}

/* ---- the dump: the HUD swapchain image of a dumped stereo frame (its RGB and alpha), the frame's quads ---------- */
static int mn_dump_hud;       /* the armed submit also copied the HUD swapchain image into D.buf after the eyes */
static uint64_t mn_dump_frame; /* X.n_eyes of that dump: its quads are logged when the frame is ended */
static VkDeviceSize mn_dump_extra(void) {
    return X.hud_sc && X.hud_w ? (VkDeviceSize)(X.hud_w + MN.extra) * X.hud_h * 4 : 0;
}
/* eyes_submit, after the HUD copies, while a stereo dump is armed: the HUD swapchain image -> D.buf after the eye
 * image's bytes; returns its layout now (TRANSFER_SRC when copied, else still TRANSFER_DST) */
static VkImageLayout mn_hud_dump_cmds(struct dev_st *ds, VkCommandBuffer cmd, VkImage img) {
    mn_dump_hud = 0;
    VkDeviceSize off = (VkDeviceSize)X.sc_w * X.sc_h * 4, need = mn_dump_extra();
    if (!D.armed || !need || D.size < off + need || !ds->CmdCopyImageToBuffer) return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VkImageMemoryBarrier t;
    img_barrier(&t, img, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                           &t);
    VkBufferImageCopy r = {.bufferOffset = off, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                           .imageExtent = {X.hud_w + MN.extra, X.hud_h, 1}};
    ds->CmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, D.buf, 1, &r);
    mn_dump_hud = 1;
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}
/* dump_write, after the stereo file (its fence waited): <base>.hudN.ppm (the RGB as stored) and <base>.hudN.alpha.pgm */
static void mn_dump_write(void) {
    if (!mn_dump_hud) return;
    mn_dump_hud = 0;
    mn_dump_frame = X.n_eyes;
    int srgb = 0, bgra = somvr_dump_order(X.hud_fmt, &srgb);
    if (bgra < 0) return;
    uint32_t w = X.hud_w + MN.extra, h = X.hud_h;
    const uint8_t *px = (const uint8_t *)D.map + (size_t)X.sc_w * X.sc_h * 4;
    char path[sizeof D.base + 48], apath[sizeof D.base + 48];
    snprintf(path, sizeof path, "%s.hud%llu.ppm", D.base, (unsigned long long)X.n_eyes);
    snprintf(apath, sizeof apath, "%s.hud%llu.alpha.pgm", D.base, (unsigned long long)X.n_eyes);
    FILE *f = fopen(path, "we"), *a = fopen(apath, "we");
    uint8_t *row = f && a ? malloc((size_t)w * 3) : NULL, *arow = row ? malloc(w) : NULL;
    int ok = arow && fprintf(f, "P6\n%u %u\n255\n", w, h) > 0 && fprintf(a, "P5\n%u %u\n255\n", w, h) > 0;
    for (uint32_t y = 0; ok && y < h; y++) {
        const uint8_t *s = px + (size_t)y * w * 4;
        somvr_dump_row(s, row, w, bgra);
        for (uint32_t x = 0; x < w; x++) arow[x] = s[4 * x + 3];
        ok = fwrite(row, 3, w, f) == w && fwrite(arow, 1, w, a) == w;
    }
    free(row), free(arow);
    if (f) ok &= fclose(f) == 0;
    if (a) ok &= fclose(a) == 0;
    lg("xr: dump hud frame %llu -> %s + .alpha.pgm (%ux%u: the HUD image %ux%u%s, format %d)%s",
       (unsigned long long)X.n_eyes, path, w, h, X.hud_w, X.hud_h, MN.extra ? " and the minimap's column" : "", X.hud_fmt,
       ok ? "" : ": write failed");
}
static void mn_log_quad(const char *what, int slot, const XrCompositionLayerQuad *q) {
    lg("xr: dump layers frame %llu: %s pose %+.4f,%+.4f,%+.4f q %+.4f,%+.4f,%+.4f,%+.4f size %.4f x %.4f rect %d,%d %dx%d "
       "flags 0x%llx slot %d%s", (unsigned long long)mn_dump_frame, what, q->pose.position.x, q->pose.position.y,
       q->pose.position.z, q->pose.orientation.x, q->pose.orientation.y, q->pose.orientation.z, q->pose.orientation.w,
       q->size.width, q->size.height, q->subImage.imageRect.offset.x, q->subImage.imageRect.offset.y,
       q->subImage.imageRect.extent.width, q->subImage.imageRect.extent.height, (unsigned long long)q->layerFlags, slot,
       q->space == X.space ? "" : " space other");
}
/* the VR menu's picture of the dumped frame (its panel or card): <base>.vrmenuN.ppm + .alpha.pgm (premultiplied) */
static void mn_dump_vrmenu(void) {
    if (!VM.on || !VM.map || !VM.released) return;
    char path[sizeof D.base + 48], apath[sizeof D.base + 48];
    snprintf(path, sizeof path, "%s.vrmenu%llu.ppm", D.base, (unsigned long long)mn_dump_frame);
    snprintf(apath, sizeof apath, "%s.vrmenu%llu.alpha.pgm", D.base, (unsigned long long)mn_dump_frame);
    FILE *f = fopen(path, "we"), *a = fopen(apath, "we");
    int ok = f && a && fprintf(f, "P6\n%d %d\n255\n", SOMVR_VM_TEX_W, SOMVR_VM_TEX_H) > 0 &&
             fprintf(a, "P5\n%d %d\n255\n", SOMVR_VM_TEX_W, SOMVR_VM_TEX_H) > 0;
    const uint8_t *px = (const uint8_t *)VM.map;
    for (int i = 0; ok && i < SOMVR_VM_TEX_W * SOMVR_VM_TEX_H; i++) {
        const uint8_t *s = px + 4 * i;
        uint8_t rgb[3] = {s[VM.bgra ? 2 : 0], s[1], s[VM.bgra ? 0 : 2]};
        ok = fwrite(rgb, 1, 3, f) == 3 && fputc(s[3], a) != EOF;
    }
    if (f) ok &= fclose(f) == 0;
    if (a) ok &= fclose(a) == 0;
    lg("xr: dump vrmenu frame %llu -> %s + .alpha.pgm (%dx%d, premultiplied%s)%s", (unsigned long long)mn_dump_frame,
       path, SOMVR_VM_TEX_W, SOMVR_VM_TEX_H, MN.card_state == 1 ? ", the card" : "", ok ? "" : ": write failed");
}
/* the stereo frame that was dumped, before its xrEndFrame: its eyes and every quad of its composition c in their
 * order (scripts/xr_compose.py draws them), the VR menu's picture */
static void mn_dump_layers(const XrCompositionLayerProjectionView pv[2], const xr_comp *c) {
    if (!mn_dump_frame) return;
    for (int e = 0; e < 2; e++)
        lg("xr: dump layers frame %llu: eye %d pose %+.4f,%+.4f,%+.4f q %+.4f,%+.4f,%+.4f,%+.4f fov %+.4f %+.4f %+.4f %+.4f "
           "rect %d,%d %dx%d", (unsigned long long)mn_dump_frame, e, pv[e].pose.position.x, pv[e].pose.position.y,
           pv[e].pose.position.z, pv[e].pose.orientation.x, pv[e].pose.orientation.y, pv[e].pose.orientation.z,
           pv[e].pose.orientation.w, pv[e].fov.angleLeft, pv[e].fov.angleRight, pv[e].fov.angleUp, pv[e].fov.angleDown,
           pv[e].subImage.imageRect.offset.x, pv[e].subImage.imageRect.offset.y, pv[e].subImage.imageRect.extent.width,
           pv[e].subImage.imageRect.extent.height);
    for (int i = XL_OWN + 1; i < XL_N; i++) {
        const XrCompositionLayerBaseHeader *l = c->l[i];
        if (!l || l->type != XR_TYPE_COMPOSITION_LAYER_QUAD) continue;
        const char *what = i == XL_HUD ? (MN.hud_menu ? "hud (a menu board)" : "hud") : i == XL_MMAP ? "minimap"
                           : i == XL_BAND && !MN.pp_has ? "hud band"
                           : i >= XL_BAND && i <= XL_BAND4 ? "hud popup" /* build 21: SOMVR_XR_POPUPS=gaze's tiles */
                           : i == XL_VM0 ? (MN.card_state == 1 ? "vrmenu card" : "vrmenu panel")
                           : i == XL_VM1 || i == XL_VM2 ? "vrmenu marker" : i == XL_PTR0 || i == XL_PTR1 ? "pointer marker"
                           : i == XL_RING ? (CF.sc_w && ((const XrCompositionLayerQuad *)l)->subImage.swapchain == CF.sc_w
                                                 ? "comfort ring wraith" /* build 12: the white-blue tunnel */
                                                 : "comfort ring")
                           : i == XL_BLINK ? (CF.sc_w && ((const XrCompositionLayerQuad *)l)->subImage.swapchain == CF.sc_w
                                                  ? "comfort blink wraith" : "comfort blink")
                           : i == XL_FADE ? "fade"
                           : i == XL_MBD ? "menu dim" : i == XL_MBK ? "menu backdrop" /* SOMVR_XR_MENU_BACK */
                           : i == XL_THE0 || i == XL_THE1 ? "threat edge" /* build 10: the bands (a VIEW space) */
                           : i == XL_OBJ || i == XL_OBJL ? "objective" /* build 15: the marker (the edge arrow: a VIEW
                                                                         * space); build 17: the drop's line */
                                         : "threat cue";
        mn_log_quad(what, i, (const XrCompositionLayerQuad *)l);
    }
    if (c->l[XL_VM0]) mn_dump_vrmenu();
    if (c->l[XL_OBJ] || c->l[XL_OBJL]) ob_dump(mn_dump_frame); /* build 15: the objective marker's picture */
    mn_dump_frame = 0;
}

/* ---- SOMVR_XR_TEST=PATH: test hands for headless boots (../somvr_menus.h 4; never in a player's build) ----------- */
static struct {
    int on;
    const char *path;
    struct timespec mt;      /* the file's last mtime and size: a change = its lines run */
    off_t size;
    uint64_t n_sync, n_runs, n_bad;
    struct {
        int on;              /* the hand's pose is ours */
        somvr_posef from, to, cur;
        double t0, t1;       /* the move: from at t0, to at t1 (s) */
        uint32_t btn;        /* SOMVR_BTN_* held */
        float trigger, squeeze, stick[2];
        int input;           /* the buttons/values are ours */
        float shake[2];      /* build 20: `shake CM DEG` over the pose (0 0: none) */
    } h[2];
} XT;
static void mn_xt_config(void) {
    const char *p = getenv("SOMVR_XR_TEST");
    memset(&XT, 0, sizeof XT);
    XT.on = p && *p, XT.path = p;
}
static double xt_now(void) { return (double)X.fs.predictedDisplayTime * 1e-9; }
static void xt_move(int h, const somvr_posef *to, float sec) {
    double t = xt_now();
    if (XT.h[h].on) XT.h[h].from = XT.h[h].cur;
    else XT.h[h].from = *to;
    XT.h[h].to = *to, XT.h[h].t0 = t, XT.h[h].t1 = t + sec, XT.h[h].on = 1, XT.h[h].input = 1;
    if (!(sec > 0)) XT.h[h].cur = *to;
}
/* the quad a touch line aims at: 1 = *pose, size */
static int xt_quad(int what, somvr_posef *pose, float size[2]) {
    if (what == SOMVR_XTS_BOARD && X.mono_ok && X.last_stereo == 0) {
        /* build 22: a mono frame shown last - its quad as shown: the board, or the theatre's screen (SOMVR_XR_THEATRE:
         * the window); before the HUD board, whose pose stays latched from the stereo frames before the flat ones (run
         * 20261029-004550: the ray 4-6 cm off the board, the theatre never hit) */
        memcpy(pose, &X.mono_pose, sizeof *pose), size[0] = X.mono_size[0], size[1] = X.mono_size[1];
        return 1;
    }
    if (what == SOMVR_XTS_BOARD && MN.hud_menu && MN.hud_latched) {
        *pose = MN.hud_pose, size[0] = X.last_quad.size.width, size[1] = X.last_quad.size.height;
        return 1;
    }
    if (what == SOMVR_XTS_BOARD && MN.mono_latched && X.last_stereo == 0) {
        *pose = MN.mono_pose, size[0] = MN.mq[1], size[1] = MN.mq[1] * 9.0f / 16.0f;
        return 1;
    }
    if (what == SOMVR_XTS_PANEL && MN.panel_n + 2 >= X.n_stereo && MN.panel_n) {
        *pose = MN.panel;
        size[0] = MN.panel_sz[0], size[1] = MN.panel_sz[1];
        return 1;
    }
    if (what == SOMVR_XTS_WATCH && MN.shown) {
        memcpy(pose, &MN.q.pose, sizeof *pose);
        size[0] = MN.q.size.width, size[1] = MN.q.size.height;
        return 1;
    }
    return 0;
}
static void xt_run(const char *line) {
    if (vd_xt(line) || th_xt(line)) return; /* `stall MS` (./somvr_void_layer.c), `head X Y Z` (./somvr_testhead_layer.c) */
    if (pf_xt(line)) return; /* build 27: `perf NAME` (./somvr_perf_layer.c) */
    somvr_xt_cmd c;
    int r = somvr_xt_parse(line, &c);
    if (r == 0) return;
    char shown[96];
    snprintf(shown, sizeof shown, "%.*s", (int)strcspn(line, "\r\n"), line);
    if (r < 0) {
        XT.n_bad++;
        lg("xr: test: not understood: '%s'", shown);
        return;
    }
    int h = c.hand;
    const char *res = "ok";
    somvr_posef head, to;
    float sz[2];
    switch (c.op) {
    case SOMVR_XT_DUMP:
        res = dump_next_k((uint64_t)c.v[0]); /* build 21: `dump K` = K frames ahead (two lines: consecutive frames) */
        break;
    case SOMVR_XT_SIZE: res = vh_resize(c.v[0], c.v[1]); break;
    case SOMVR_XT_LUMLOG: res = vh_lumlog((int)c.v[0]); break;
    case SOMVR_XT_POPUPS: /* build 21: SOMVR_XR_POPUPS live */
        MN.pp = (int)c.v[0], MN.pp_has = 0, MN.pp_t = 0;
        somvr_pp_reset(&MN.pps), memset(MN.pp_rd, 0, sizeof MN.pp_rd);
        res = MN.pp == SOMVR_PP_GAZE ? "popups gaze" : "popups band";
        break;
    case SOMVR_XT_OFF: XT.h[h].on = 0, XT.h[h].input = 0, XT.h[h].btn = 0, XT.h[h].shake[0] = XT.h[h].shake[1] = 0; break;
    case SOMVR_XT_HEAD:
        if (!vm_head(&head)) {
            res = "no head now";
            break;
        }
        somvr_xt_head_pose(&head, c.v, c.v + 3, &to);
        xt_move(h, &to, c.sec);
        break;
    case SOMVR_XT_TOUCH:
        if (!xt_quad(c.what, &to, sz)) {
            res = "that quad is not shown now";
            break;
        }
        if (!vh_touch_pose(h, &to, sz, c.v[0], c.v[1], 0.01f * c.v[2], &head)) /* build 10: our hands' tip */
            somvr_xt_touch_pose(&to, sz, c.v[0], c.v[1], 0.01f * c.v[2], MN.tcc.reach_m, &head);
        xt_move(h, &head, c.sec);
        break;
    case SOMVR_XT_AIM: /* build 20: the ray's origin D cm out of the point, into the quad */
        if (!xt_quad(c.what, &to, sz)) {
            res = "that quad is not shown now";
            break;
        }
        somvr_xt_touch_pose(&to, sz, c.v[0], c.v[1], 0.01f * c.v[2], 0, &head);
        xt_move(h, &head, c.sec);
        break;
    case SOMVR_XT_SHAKE: XT.h[h].shake[0] = c.v[0], XT.h[h].shake[1] = c.v[1]; break; /* build 20 */
    case SOMVR_XT_BTN: {
        static const uint32_t bit[] = {SOMVR_BTN_MENU, SOMVR_BTN_LOWER, SOMVR_BTN_UPPER, SOMVR_BTN_STICK, 0, 0};
        XT.h[h].input = 1;
        if (c.what == SOMVR_XTB_TRIGGER) XT.h[h].trigger = c.v[0];
        else if (c.what == SOMVR_XTB_SQUEEZE) XT.h[h].squeeze = c.v[0];
        else XT.h[h].btn = c.v[0] > 0.5f ? XT.h[h].btn | bit[c.what] : XT.h[h].btn & ~bit[c.what];
        break;
    }
    case SOMVR_XT_STICK: XT.h[h].input = 1, XT.h[h].stick[0] = c.v[0], XT.h[h].stick[1] = c.v[1]; break;
    default: break;
    }
    if (XT.n_runs < 2000)
        lg("xr: test: '%s': %s (frame %llu)", shown, res, (unsigned long long)X.n_eyes);
}
/* the test file: its lines once each time it changes (stat every 5th sync) */
static void xt_poll(void) {
    if (XT.n_sync++ % 5) return;
    struct stat st;
    if (stat(XT.path, &st)) return;
    if (st.st_mtim.tv_sec == XT.mt.tv_sec && st.st_mtim.tv_nsec == XT.mt.tv_nsec && st.st_size == XT.size) return;
    XT.mt = st.st_mtim, XT.size = st.st_size;
    FILE *f = fopen(XT.path, "re");
    if (!f) return;
    char line[256];
    XT.n_runs++;
    while (fgets(line, sizeof line, f)) xt_run(line);
    fclose(f);
}
static void mn_xt_log(void) {
    if (XT.on) lg("xr: test: SOMVR_XR_TEST=%s: test hands and dumps from that file (headless tests only)", XT.path);
}
/* in_sync's button loop: the held test buttons join hand h's */
static uint32_t mn_xt_buttons(int h) { return XT.on && XT.h[h].input ? XT.h[h].btn : 0u; }
/* in_sync, after its reads: the test hands' poses and values over the runtime's (none headless) */
static void mn_xt_apply(void) {
    if (!XT.on) return;
    xt_poll();
    double t = xt_now();
    for (int h = 0; h < 2; h++) {
        somvr_hand_in *hi = &I.st.hand[h];
        if (XT.h[h].input) {
            hi->trigger = XT.h[h].trigger, hi->squeeze = XT.h[h].squeeze;
            hi->stick[0] = XT.h[h].stick[0], hi->stick[1] = XT.h[h].stick[1];
            hi->active |= SOMVR_HACT_INPUT;
        }
        if (!XT.h[h].on) continue;
        double span = XT.h[h].t1 - XT.h[h].t0;
        float k = span > 0 ? (float)((t - XT.h[h].t0) / span) : 1.0f;
        somvr_xt_lerp(&XT.h[h].from, &XT.h[h].to, k, &XT.h[h].cur);
        hi->grip = hi->aim = XT.h[h].cur;
        if (XT.h[h].shake[0] > 0 || XT.h[h].shake[1] > 0) /* build 20: a shaking hand over the scripted pose */
            somvr_xt_shake(&XT.h[h].cur, XT.h[h].shake[0], XT.h[h].shake[1], t, h, &hi->grip), hi->aim = hi->grip;
        hi->grip_flags = hi->aim_flags = 0xf;
        memset(hi->lin_vel, 0, sizeof hi->lin_vel), memset(hi->ang_vel, 0, sizeof hi->ang_vel);
        hi->active |= SOMVR_HACT_POSE;
    }
}
