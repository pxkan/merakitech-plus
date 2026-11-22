/* src/mp/ui_state.c — UI design tokens + state, stable weight source, row model, formatting, activity guards, save flow, navigation, system actions, hit-test, gestures.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ====================================================================== */
/* ================= UI: design tokens, state, pages ==================== */
/* ====================================================================== */
/* v7.0 DESIGN TOKENS — the OEM language: white on copper. Hierarchy comes from size and weight first, then from a warm cream;
 * colour is reserved for what must not be missed (errors, blocked starts), which also switch the background to a dark
 * espresso layer. C_* names are kept from v6 so the logic that picks a colour (ok / warn / err / accent) did not change. */
#define C_TXT    rgb(255,255,255)
#define C_TXT2   rgb(255,236,220)     /* secondary: warm cream */
#define C_TXT3   rgb(250,220,196)     /* captions */
#define C_DIS    rgb(222,160,122)     /* disabled: dissolves into the copper on purpose */
#define C_SURF   rgb(150,70,30)       /* pressed / filled surfaces on copper */
#define C_SURF2  rgb(164,78,34)
#define C_SURF3  rgb(196,104,54)
#define C_LINE   rgb(226,150,108)     /* hairlines (row separators) */
#define C_ACC    rgb(255,255,255)     /* the accent IS white (values, rings, SAVE) */
#define C_ACCD   rgb(140,62,24)       /* text on a white pill: deep copper */
#define C_OK     rgb(255,255,255)
#define C_ERR    rgb(255,214,74)      /* errors on copper: a hot yellow, always with an icon; the error SCREEN is dark with red */
#define C_WARN   rgb(255,226,120)     /* blocked / attention: pale yellow (with an icon where space allows) */
#define C_RED    rgb(255,92,72)       /* the dark error layer's accent */
#define C_STOP_T  rgb(255,255,255)    /* STOP: a solid white pill with deep copper text — the brightest thing on screen */
#define C_STOP_B  rgb(255,247,240)
#define C_STOP_FG rgb(140,52,18)
/* phase colours (v6: a rainbow) are monochrome in v7: white on copper; the gauge/progress uses white, ERROR/OFFLINE get their own */
static uint32_t ph_hi(int p){ switch(p){ case PH_ERROR: return C_RED; case PH_OFFLINE: case PH_SLEEP: return rgb(240,200,176); default: return C_TXT; } }
static uint32_t ph_lo(int p){ switch(p){ case PH_ERROR: return rgb(150,40,30); default: return rgb(236,176,140); } }
static const int ACT_PH[4]={PH_GRIND,PH_EXT,PH_WATER,PH_STEAM};
static const char*const ACT_ICON[4]={IC_BEAN,IC_CUP,IC_DROP,IC_STEAM};
static const short ACT_NAME[4]={TX_A_GRIND,TX_A_BREW,TX_A_WATER,TX_A_STEAM};
/* v6.13: STOP button, "STOPPING …" toast and "… STOPPED" toast per activity (index = ACT_*) */
static const short STOP_BTN[4]={TX_B_STOP_GRIND,TX_B_STOP_BREW,TX_B_STOP_WATER,TX_B_STOP_STEAM};
static const short STOP_SENT[4]={TX_T_STOP_GRIND,TX_T_STOP_BREW,TX_T_STOP_WATER,TX_T_STOP_SENT};
static const short STOP_DONE[4]={TX_T_STOPPED_GRIND,TX_T_STOPPED_BREW,TX_T_STOPPED_WATER,TX_T_STOPPED_STEAM};

enum { V_SPLASH=0, V_MAIN, V_DETAIL, V_SYS, V_BYE, V_OFF };   /* v6.14: V_OFF = the power button's TURNING OFF / OFF / TURNING ON */   /* V_SYS: boot/switch/exit view (xsel); V_BYE: last frame before runtime switch */
/* v6.17 INFORMATION ARCHITECTURE (docs/ux-audit-v617.md). HOME is the root and the live view of whatever runs. Everything else is a
 * SHEET that is opened from the thing it belongs to and closed with the chevron at its top (or a swipe right / a pull down on the header):
 *   HOME icon (tap)   -> that feature's page: GRIND / BREW / WATER / STEAM = live line + ITS settings as rows + HOLD TO START
 *   HOME icon (hold)  -> start, as before (the icon now carries its target underneath: "18.5 G", "36 G", "360 G", "65°")
 *   HOME gear         -> SYSTEM: four collapsible sections (DISPLAY, CLOCK, SCHEDULE, STOCK UI) + the DIAGNOSTICS entry
 *   SYSTEM row        -> DIAGNOSTICS (advanced; also opened by tapping HOME's error / offline hint)
 * Before v6.17 the pages were a carousel (HOME | BREW | STEAM · WATER | SYSTEM | DIAGNOSTICS: swipe, invisible edge zones, page dots) and
 * the settings lived in three lists that followed the spec's tiers, not the activity they belong to. */
enum { PG_HOME=0, PG_GRIND, PG_BREW, PG_WATER, PG_STEAM, PG_SYS, PG_DIAG, NPAGE };
#define PG_T0 PG_GRIND     /* first list page (page = PG_T0 + SET[].tier) */
enum { T_NONE=0, T_ACT0, T_ACT1, T_ACT2, T_ACT3, T_STOP, T_MINUS, T_PLUS, T_SAVE, T_CANCEL, T_START, T_DIAG, T_GRAPH, T_PINFO, T_GEAR, T_BACK, T_STOPS, T_ROLL, T_ROW0=32 };   /* v6.11: T_GEAR (HOME -> SYSTEM), T_BACK (close chevron); v6.17: T_START (a feature page's HOLD TO START), T_DIAG (HOME's error/offline hint -> DIAGNOSTICS) */
static int pinfo_hit(int x,int y);   /* v6.10: tap on the chart / BAR field -> toast w. the basis for ≈BAR (defined at draw_chart) */
static int diag_hint_shown(void);    /* v6.17: HOME's error/offline hint is on screen (the chart field is empty) -> it is the tap target for DIAGNOSTICS */
#define SPLASH_MS 1500
#define HOLD_MS 650

static int view=V_SPLASH, page=PG_HOME, sel=-1, edit_val=0, edit_orig=0;
static float scroll[NPAGE];
static ms_t ui_start=-1, ui_last=0; static int ui_anim=1; static volatile int run=1;
static int ui_kick=0; static float ft_last=0, ft_max=0;   /* frame time (ms) measured on device, shown on DIAGNOSE */
static struct { int down,x0,y0,x,y,moved,target,scrolling,fired,rep_n,axis,roll_n; ms_t t0,rep_next; float scroll0; } tch;
static float roll_off=0;          /* v7.0: the editor roller's drag offset in px, eased back to 0 after the release */
static struct { char msg[96]; ms_t t0; int dur; uint32_t col; } toast;
static struct { float x,y; ms_t t0; int on; } rip;
static Rect ov_prev[2];
static struct { int raw,ph,last_ph,pending,gkind; ms_t t_act0,t_done,done_ms,t_req; long seq; uint32_t c_from_hi,c_from_lo; ms_t t_col; } act={PH_OFFLINE,PH_OFFLINE,PH_READY,ACT_NONE,-1,0,-100000,0,0,0,0,0,0};
/* ---- v6.9: STABLE WEIGHT SOURCE for chart, hero and stat field (word 38 = grind weight, word 39 = brew weight; both signed 0.1 g) ----
 * The user saw periodic weight spikes, even when the machine was not touched. Up to and including v6.9-wip the graph chose the weight word per
 * sample by phase (phase_main()==GRIND ? 38 : 39), and the hero by the shown phase — so any phase flicker (a single
 * wrong work_sta sample, a link gap, the maintenance pulses 0x0400/0x0800/0x2000/0x0C00/0x2800 at rest) could put one sample
 * of the OTHER weight word into the curve (e.g. 18.7 g grind weight in the middle of 36.7 g brew weight). Now the source is a LATCH:
 *  - default = the brew weight (word 39);
 *  - the grind weight (word 38) only while a grind IS running (work_sta bit 6) and while its DONE screen is shown (8 s) — the
 *    latter only if the source was already latched to 38 (a real grind, not one wrong sample);
 *  - a switch requires the wanted source to have held for >= WSRC_DEB_MS (300 ms) — one deviating sample never switches;
 *  - exception: if the UI itself started the activity (act_request, < 4 s ago) and the activity's bits are set, it switches
 *    immediately — the activity is real (we asked for it, and the MCU has set its bits).
 * DISPLAY ONLY: control, frame policy, auto-stop expectations, guards and DIAGNOSE read words 38/39 directly and raw. */
#define WSRC_DEB_MS 300
static struct { int src,cand,n_sw,req; ms_t t_cand,t_req; } wsl={39,39,0,ACT_NONE,0,-100000};
static int wsrc_want(ms_t now){
    int pm=phase_main(now);
    if(pm==PH_GRIND) return 38;                                              /* bit 6: the grinder is running */
    if(pm==PH_PRE||pm==PH_EXT||pm==PH_WATER) return 39;                      /* brew/water bits */
    if(wsl.src==38&&(act.ph==PH_DONE||act.ph==PH_GRIND)&&act.last_ph==PH_GRIND) return 38;   /* the grind result is on screen (DONE) —
                                                                             only after a REAL grind (the source was already latched);
                                                                             otherwise one bit-6 sample gives 8 s of "DONE" w. grind weight */
    return 39;
}
static int wsrc_tick(ms_t now){
    int w=wsrc_want(now);
    if(w==wsl.src){ wsl.cand=w; return w; }
    if(w!=wsl.cand){ wsl.cand=w; wsl.t_cand=now; }
    int pm=phase_main(now);
    int req=wsl.req!=ACT_NONE&&now>=wsl.t_req&&now-wsl.t_req<4000&&phase_active(pm)&&((wsl.req==ACT_GRIND)==(pm==PH_GRIND))&&((w==38)==(pm==PH_GRIND));
    if(req||now-wsl.t_cand>=WSRC_DEB_MS||now<wsl.t_cand){ wsl.src=w; wsl.n_sw++; }
    return wsl.src;
}
static struct { int active,stage,idx,val,rollback; long seq; } sav;
/* v6.2: system actions. X_BOOTUI + X_SWITCH are rows at the bottom of SYSTEM; X_EXIT is the exit reminder */
enum { X_BOOTUI=0, X_SWITCH, X_EXIT };
static int xsel=-1, x_edit=0, x_orig=0, x_from_switch=0, x_pending=0, bye_mode=0;
static int sw_sim=0;       /* preview only: SWITCH NOW starts no process */
/* device hooks for BOOT.md §9.2 step 1: release UART/touch/fb BEFORE stock is started; reacquire them on failure */
static void (*dev_release)(void)=NULL;
static void (*dev_reacquire)(void)=NULL;
static int sw_child=0, sw_seq=0, sw_seq_frame=0, sw_seq_release=0, sw_seq_launch=0;

#define TOAST_TW 312       /* text width that fits in the toast pill (356 - 44); longer text is truncated w. … */
static void toast_show(const char*m,uint32_t col,ms_t now){
    snprintf(toast.msg,sizeof toast.msg-4,"%s",m); utf8_trim(toast.msg); fit_text(toast.msg,F_XS,TOAST_TW);
    toast.t0=now; toast.dur=2400; toast.col=col;
    SIM_TOAST(m,col)
}
/* ---- v6.17: THE ROW MODEL. Every list page is a list of rows of ONE pitch; what a row is, is decided here and nowhere else
 * (hit-test, drawing, the SIM layout and the selftest all walk rows_get()).
 *   feature pages : that feature's settings, most used first (FEAT_ROWS)
 *   SYSTEM        : section headers (RK_SEC; tap = open/close, one open at a time) + the open section's rows (SEC_ROWS: settings and
 *                   the two system actions BOOT UI / SWITCH TO ORIGINAL NOW) + the DIAGNOSTICS entry (RK_DIAG)
 * Every setting of SET[] is in exactly one of the two tables — except the WATER SENSOR (read-only word 40), which the WATER page shows
 * as its live line instead of as a row that looks like a setting (the selftest checks both). */
enum { SEC_DISPLAY=0, SEC_CLOCK, SEC_SCHED, SEC_STOCK, NSEC };
enum { RK_SET=0, RK_SEC, RK_X, RK_DIAG };
typedef struct { signed char kind, a; } Row;      /* a: SET[] index / section / X_BOOTUI|X_SWITCH / 0 */
#define ROW_MAX 20
#define RX(x) (-2-(x))                            /* a system action inside SEC_ROWS */
static const signed char FEAT_ROWS[4][8]={
 {S_GRINDW,S_GRINDMODE,-1},
 {S_BREWW,S_BREWT,S_PISW,S_PITIME,S_PIPRESS,S_HEADT,-1},
 {S_HOTW,S_HOTMODE,-1},
 {S_STEAMT,S_STEAMG,-1} };
static const signed char SEC_ROWS[NSEC][10]={
 [SEC_DISPLAY]={S_UILANG,S_BL,S_TUNIT,S_SOUND,S_SNDRDY,S_VOL,-1},          /* v6.18: + sound; VOLUME moved here (it now sets Meraki+'s sounds too) */
 [SEC_CLOCK]  ={S_TIMEMODE,S_DST,S_TZ,-1},
 [SEC_SCHED]  ={S_PREHEAT,S_PHAT,S_PHREP,S_AUTOOFF,-1},
 [SEC_STOCK]  ={RX(X_BOOTUI),RX(X_SWITCH),S_LANG,S_HOTSC,S_CLEARTIP,S_AUTOTIME,S_BREAK,-1} };   /* what only matters to / leads to the stock UI */
static const short SEC_NAME[NSEC]={TX_SEC_DISPLAY,TX_SEC_CLOCK,TX_SEC_SCHED,TX_SEC_STOCK};
static int sec_open=-1;                           /* SYSTEM: the open section (-1 = all closed: five rows, no scrolling) */
static int pg_feat(int pg){ return pg>=PG_GRIND&&pg<=PG_STEAM; }
static int pg_list(int pg){ return pg>=PG_GRIND&&pg<=PG_SYS; }
static int tier_of_page(int pg){ return pg-PG_T0; }
static int rows_get(int pg,Row*r){
    int n=0;
    if(pg_feat(pg)){ for(const signed char*p=FEAT_ROWS[pg-PG_GRIND];*p!=-1;p++){ r[n].kind=RK_SET; r[n++].a=*p; } }
    else if(pg==PG_SYS){
        for(int sc=0;sc<NSEC;sc++){
            r[n].kind=RK_SEC; r[n++].a=(signed char)sc;
            if(sc==sec_open) for(const signed char*p=SEC_ROWS[sc];*p!=-1;p++){ if(*p<-1){ r[n].kind=RK_X; r[n++].a=(signed char)(-2-*p); } else { r[n].kind=RK_SET; r[n++].a=*p; } }
        }
        r[n].kind=RK_DIAG; r[n++].a=0;
    }
    return n;
}
#if defined(MERAKIPLUS_PREVIEW)||defined(SIM_RT)      /* test hooks only: the product code walks rows_get() */
static int sec_of_set(int i){ for(int sc=0;sc<NSEC;sc++) for(const signed char*p=SEC_ROWS[sc];*p!=-1;p++) if(*p==i) return sc; return -1; }
#endif
#ifdef MERAKIPLUS_PREVIEW
static int row_find(int pg,int kind,int a){ Row r[ROW_MAX]; int n=rows_get(pg,r); for(int k=0;k<n;k++) if(r[k].kind==kind&&r[k].a==a) return k; return -1; }
#endif
#define ROW_H 50          /* v7.0: rows are white text on copper with a hairline between them (v6: 50 px chips, 58 px pitch) */
#define ROW_P 50
#define LIST_Y0 92
#define LIST_VY0 86
#define LIST_VY1 404
#define FEAT_VY1 344       /* v6.17: a feature page's list ends above its HOLD TO START pill */
static int list_vy1(int pg){ return pg_feat(pg)?FEAT_VY1:LIST_VY1; }
static int page_rows(int pg){ Row r[ROW_MAX]; return rows_get(pg,r); }
static int row_y0(int pg,int k){ (void)pg; return LIST_Y0+k*ROW_P; }   /* content coordinate (before scroll) */
/* where a page's close chevron leads: DIAGNOSTICS back to where it was opened from (SYSTEM, or HOME via the error hint), the rest to HOME */
static int diag_from=PG_SYS;
static int page_parent(int pg){ return pg==PG_DIAG?diag_from:PG_HOME; }
#define DG_Y0 94
#define DG_P 42
/* v6.11: DIAGNOSE is grouped. At the top STATUS (4 rows: link, phase, error codes, awake, net, version, boot push) — what you look
 * for first; then BOOT · SLOT · SWITCHER, NET, GRAPH and PRESSURE · ESTIMATE as before; RAW COUNTERS (rx, crc, work_sta, cmd6, current,
 * frame time, mirror, config md5) got their own section below the pressure estimate; ABOUT at the bottom. The cells' content is unchanged
 * (diag_top_text, index 0..15) — only the placement (DG_SLOT) is new. */
#define DG_BOOT (DG_Y0+4*DG_P+32)          /* first boot row on DIAGNOSE (v6.11: 4 status rows at the top; v6.5-v6.10: 8) */
#define DG_GRAPH (DG_BOOT+7*DG_P)          /* v6.9: the GRAPH row (smooth / raw) — the only tap target on DIAGNOSE */
#define DG_PRESS (DG_BOOT+8*DG_P+6)        /* v6.10: heading of the PRESSURE · ESTIMATE block (anchor, current value, 2 note lines) — where ABOUT was in v6.9 */
#define DG_RAW (DG_PRESS+28+2*DG_P+20)      /* v6.11: the RAW COUNTERS heading (where ABOUT was in v6.10); 4 rows below it. v6.17: the two note lines under the pressure estimate are gone (-32) */
#define DG_PWR (DG_RAW+28+4*DG_P+10)        /* v6.14: POWER BUTTON (state, word 0, press, last button, node) — where ABOUT was in v6.13 */
#define DG_TIME (DG_PWR+28+3*DG_P+10)       /* v6.16: TIME · SCHEDULE (clock, zone/DST, auto off, last alarm sent, next pre-heat) — where ABOUT was in v6.15 */
#define DG_SND (DG_TIME+28+3*DG_P+10)        /* v6.18: SOUND (mode + volume, last event + outcome) — where ABOUT was in v6.17 */
#define DG_ABOUT (DG_SND+28+DG_P+10)         /* v6.18: + SOUND. v6.14: + POWER BUTTON. v6.11: + RAW COUNTERS. v6.3: heading of the ABOUT block (UI language, wordmark, tagline); v6.7: + the NET line; v6.9: + the GRAPH row; v6.10: + PRESSURE · ESTIMATE */
#define DG_BOTTOM (DG_ABOUT+76+24+11+22)
static float scroll_max(int pg){
    if(pg==PG_DIAG) return DG_BOTTOM-(LIST_VY1-14);
    if(!pg_list(pg)) return 0;
    int n=page_rows(pg); float m=n*ROW_P-8-(list_vy1(pg)-LIST_VY0-20); return m>0?m:0;
}

/* ---- formatting ---- */
static int use_c(void){ return sv(S_TUNIT)!=0; }
static void fmt_fix(char*b,size_t n,int v,int dec){
    if(dec==1) snprintf(b,n,"%s%d.%d",v<0?"-":"",abs(v)/10,abs(v)%10);
    else if(dec==-1) snprintf(b,n,"%d",(v+(v<0?-5:5))/10);
    else snprintf(b,n,"%d",v);
}
static void fmt_t10(char*b,size_t n,int v10,int dec){   /* temperature in 0.1 °C -> text in the selected unit */
    if(!use_c()) v10=v10*9/5+320;
    fmt_fix(b,n,v10,dec?1:-1);
}
static const char* t_unit(void){ return TR(use_c()?TX_U_C:TX_U_F); }
static void set_fmt(int i,int v,char*b,size_t n){
    const Setting*s=&SET[i];
    if((s->flags&SF_SENSOR)&&!s->loaded){ snprintf(b,n,"--"); return; }
    if(s->kind==K_ENUM){ if(v>=s->min&&v<=s->max) snprintf(b,n,"%s",TR(s->opts[v-s->min])); else snprintf(b,n,"(%d)",v); return; }
    if(s->flags&SF_HHMM){ fmt_hm(b,n,v/60,v%60,1); return; }                               /* v6.16 */
    if(i==S_TZ&&v>=0&&v<TZ_N){ snprintf(b,n,"%s",TZT[v].utc); return; }                 /* v6.16: the stock label "UTC+01:00" */
    if(s->flags&SF_TEMP10){ fmt_t10(b,n,v,1); return; }
    if(s->flags&SF_TEMP1){ fmt_t10(b,n,v*10,0); return; }
    fmt_fix(b,n,v,s->dec);
}
static const char* set_unit(int i){ const Setting*s=&SET[i]; if(s->flags&(SF_TEMP10|SF_TEMP1)) return t_unit(); return TR(s->unit); }
static int s16(int r){ return (int)(int16_t)reg[r]; }

/* ---- activity guards (the UI is the only guard) ---- */
static const char* act_guard(int kind,ms_t now){
    if(ro_mode) return TR(TX_G_RO);
    int p=phase_raw(now);
    if(p==PH_OFFLINE) return TR(TX_G_NOLINK);
    if(p==PH_ERROR)   return TR(TX_G_ERROR);
    if(p==PH_SLEEP)   return TR(TX_G_SLEEP);
    if(phase_active(p)) return TR(TX_G_BUSY);
    if(wq_busy()||act.pending!=ACT_NONE||sav.active) return TR(TX_G_WAIT);
    /* all config values that go into a cmd6 frame must be loaded and within spec */
    for(int i=0;i<NSET;i++){
        const Setting*s=&SET[i]; if(s->mcu==M_NONE) continue;
        if(!s->loaded) return TR(TX_G_NOCFG);
        if(set_oor(s)) return TR(TX_G_OOR);
    }
    if(kind==ACT_GRIND&&sv(S_GRINDMODE)!=1) return TR(TX_G_GRINDTIME);
    if(kind==ACT_WATER&&sv(S_HOTMODE)!=1)   return TR(TX_G_WATERMAN);
    return NULL;
}
static void act_request(int kind,ms_t now){
    const char*g=act_guard(kind,now);
    if(g){ toast_show(g,C_WARN,now); return; }
    long seq=wq.seq;
    if(!act_push(kind)){ toast_show(TR(wq_rej==WJ_TRIG||wq_rej==WJ_RANGE?TX_T_POLICY:TX_T_QFULL),C_ERR,now); return; }
    act.pending=kind; act.t_req=now; act.seq=seq;
    wsl.req=kind; wsl.t_req=now;                                    /* v6.9: UI-started activity -> the weight source may switch immediately */
}

/* ---- save flow: MCU write (ACK) -> config.json (atomic) ; all-or-nothing ---- */
static void detail_close(ms_t now);
static void boot_push_resave(void);
static void snd_preview(ms_t now);                                   /* v6.18 (the sound engine is after sched_step) */
static void save_begin(ms_t now){
    if(sav.active||sel<0) return;
    Setting*s=&SET[sel];
    if(s->flags&SF_LOCK) return;
    if(edit_val==s->val){ detail_close(now); return; }
    if(!set_valid(s,edit_val)){ toast_show(TR(TX_T_RANGE),C_ERR,now); return; }
    if(s->flags&SF_LOCAL){   /* v6.3: UI language — own file; no MCU write, no config.json, no RESTART_NEEDED */
        int ok=lang_save(edit_val);
        s->val=edit_val; lang_set(edit_val); if(!ok) lang_src=LS_SESSION;      /* the language applies now in any case */
        toast_show(TR(ok?TX_T_LANG_SAVED:TX_T_LANG_NOFILE),ok?C_OK:C_WARN,now); /* confirmation in the NEW language */
        detail_close(now); return;
    }
    if(s->flags&SF_OWN){     /* v6.16: pre-heat schedule / auto off — merakiplus's own file; no MCU write, no config.json */
        s->val=edit_val;
        if(sel==S_PREHEAT) sch.on=edit_val; else if(sel==S_PHAT) sch.hm=edit_val; else if(sel==S_PHREP) sch.rep=edit_val; else sch.ao=edit_val;
        sch.once_at=(sch.on&&!REP_MASK[sch.rep])?sched_once_target(clk_utc()):0;   /* a one-shot is for the next hh:mm after now */
        int ok=sched_save(); idle_kick(now);
        if(sch.on&&!clk_valid(clk_utc())) toast_show(TR(TX_T_NOCLOCK),C_WARN,now);
        else toast_show(TR(ok?TX_T_OWN_SAVED:TX_T_OWN_NOFILE),ok?C_OK:C_WARN,now);
        detail_close(now); return;
    }
    if(s->flags&SF_SND){     /* v6.18: sound on/off + ready tones — merakiplus's own file; no MCU write, no config.json */
        s->val=edit_val; if(sel==S_SOUND) sndc.mode=edit_val; else sndc.ready=edit_val;
        int ok=snd_cfg_save();
        toast_show(TR(ok?TX_T_SND_SAVED:TX_T_OWN_NOFILE),ok?C_OK:C_WARN,now);
        detail_close(now); snd_preview(now); return;
    }
    if(!cfg_ok||!s->loaded){ toast_show(TR(TX_T_CFG_UNAVAIL),C_ERR,now); return; }
    sav.idx=sel; sav.val=edit_val; sav.rollback=0;
    if(s->mcu!=M_NONE){
        if(ro_mode){ toast_show(TR(TX_G_RO),C_WARN,now); return; }
        if(!link_up(now)){ toast_show(TR(TX_T_NOLINK_SAVE),C_ERR,now); return; }
        if(phase_active(phase_raw(now))){ toast_show(TR(TX_T_WAIT_ACT),C_WARN,now); return; }
        if(wq_busy()){ toast_show(TR(TX_G_WAIT),C_WARN,now); return; }
        sav.seq=wq.seq;
        if(setting_mcu_push(sel,edit_val)<0){ toast_show(TR(TX_T_WRITE_REJ),C_ERR,now); return; }
        sav.active=1; sav.stage=1;
    } else { sav.active=1; sav.stage=2; }
}
static void save_step(ms_t now){
    if(!sav.active) return;
    Setting*s=&SET[sav.idx];
    if(sav.stage==1){
        if(wq.seq==sav.seq) return;
        if(wq.result!=WR_OK){ sav.active=0; toast_show(TR(TX_T_NOACK_SAVE),C_ERR,now); return; }
        sav.stage=2;
    }
    if(sav.stage==2){
        if(cfg_set_int(s->key,sav.val)){
            s->val=sav.val; sav.active=0;
            boot_push_resave();
            toast_show(TR(s->mcu!=M_NONE?TX_T_SAVED_MCU:TX_T_SAVED_CFG),C_OK,now);
            if(view==V_DETAIL) detail_close(now);
            if(sav.idx==S_VOL) snd_preview(now);                       /* v6.18: hear the new volume */
        } else {
            /* config failed: roll the MCU back to the old value so both sides are unchanged */
            if(s->mcu!=M_NONE){ sav.seq=wq.seq; if(setting_mcu_push(sav.idx,s->val)>0) sav.stage=3; else sav.active=0; }
            else sav.active=0;
            toast_show(TR(TX_T_CFG_ERR),C_ERR,now);
            if(s->flags&SF_BL) bl_set(s->val);
            edit_val=s->val;
        }
    } else if(sav.stage==3){ if(wq.seq!=sav.seq) sav.active=0; }
}

/* ---- page navigation / transitions ---- */
static void compose(ms_t now);
static void arc_targets(ms_t now);
static void go_page(int np,int dir,ms_t now){
    if(np==page||tr.type) return;
    memcpy(snapA,canvas,(size_t)W*H*4);
    page=np; static_dirty=1; if(np==PG_HOME) act.gkind=-1; arc_targets(now); compose(now);
    memcpy(snapB,canvas,(size_t)W*H*4);
    tr.type=TR_SLIDE; tr.t0=now; tr.dur=260; tr.dir=dir;
}
static void fade_to(ms_t now,int dur){ tr.type=TR_FADE; tr.t0=now; tr.dur=dur; tr.dir=0; }
/* v6.17: sheets. nav_open() slides a page in from the right, nav_back() slides back to its parent. SYSTEM always opens with its sections
 * closed and at the top; DIAGNOSTICS remembers where it was opened from. */
static void nav_open(int np,ms_t now){
    if(np==page||tr.type) return;
    if(np==PG_DIAG) diag_from=(page==PG_SYS)?PG_SYS:PG_HOME;
    if(np==PG_SYS&&page==PG_HOME){ sec_open=-1; scroll[PG_SYS]=0; }
    if(pg_feat(np)) scroll[np]=0;
    go_page(np,1,now);
}
static void nav_back(ms_t now){ if(page!=PG_HOME&&!tr.type) go_page(page_parent(page),-1,now); }
static void sec_toggle(int sc){
    sec_open=(sec_open==sc)?-1:sc;
    scroll[PG_SYS]=clampf(sec_open<0?0:(float)(sc*ROW_P),0,scroll_max(PG_SYS));   /* the opened header goes to the top of the field */
}
static void detail_open(int i,ms_t now){
    memcpy(snapA,canvas,(size_t)W*H*4);
    view=V_DETAIL; sel=i; edit_val=edit_orig=SET[i].val; static_dirty=1; garc.f_d=0; arc_targets(now); compose(now);
    memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,200);
}
static void detail_close(ms_t now){
    if(sel>=0&&(SET[sel].flags&SF_BL)) bl_set(SET[sel].val);
    if(sel>=0&&(SET[sel].flags&SF_LOCAL)) lang_set(SET[sel].val);          /* CANCEL: back to the saved language */
    memcpy(snapA,canvas,(size_t)W*H*4);
    view=V_MAIN; sel=-1; static_dirty=1; act.gkind=-1; arc_targets(now); compose(now);
    memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,200);
}
/* ---- v6.2: system actions (BOOT UI, SWITCH TO ORIGINAL NOW, exit reminder) ---- */
static int sys_locked(void){ return xsel==X_BOOTUI? bs.lock!=BL_OK : (xsel==X_SWITCH? switch_locked() : 0); }
static void boot_poll(ms_t now){ boot_refresh(); bs.t=now; x_pending=restart_pending(); }
static void sys_open(int x,ms_t now){
    boot_poll(now);
    memcpy(snapA,canvas,(size_t)W*H*4);
    view=V_SYS; xsel=x; x_edit=x_orig=bs.flag; static_dirty=1; garc.f_d=0; arc_targets(now); compose(now);
    memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,200);
}
static void sys_close(ms_t now){
    memcpy(snapA,canvas,(size_t)W*H*4);
    view=V_MAIN; xsel=-1; x_from_switch=0; static_dirty=1; act.gkind=-1; arc_targets(now); compose(now);
    memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,200);
}
static const char* switch_guard(ms_t now){
    if(phase_active(phase_raw(now))) return TR(TX_T_WAIT_ACT);
    if(wq_busy()||sav.active||act.pending!=ACT_NONE) return TR(TX_G_WAIT);
    return NULL;
}
static void bye(int mode){ bye_mode=mode; view=V_BYE; xsel=-1; tr.type=TR_NONE; rip.on=0; toast.dur=0; static_dirty=1; run=0; }   /* the last frame freezes: no ripple/toast */
/* SWITCH TO ORIGINAL NOW — BOOT.md §9.2, in the order the specification requires:
 *   (0) last frame on screen   (1) stop own activity: close UART, touch grab and fb
 *   (2) fork + setsid + chdir /customer + LD_LIBRARY_PATH + execl ./demo_v9   (3) wait 400 ms, exit
 *   (4) the flag is not touched; no signals   (5) exit releases the rest   (6) log w. time + reason.
 * If the stock UI cannot be started, merakiplus takes UART/fb/touch back and keeps running.
 * run.sh mode (stock exists, paused): nothing is started — merakiplus exits, run.sh SIGCONTs. */
static const uint32_t* ui_frame(ms_t now);
static void do_switch(ms_t now){
    char err[72]="", rn[340]; int mode=switch_mode();
    if(sw_sim){ bye(mode); return; }
    if(mode==SWM_CONT){
        boot_log("RUNTIME SWITCH to original: reason=user-button, run.sh mode (stock UI pid %d is paused): merakiplus exits, nothing is started",bs.stock[0]);
        bye(mode); return;
    }
    if(sw_dry){
        ld_path_for_a(getenv("LD_LIBRARY_PATH"),sw_ldp,sizeof sw_ldp);
        boot_log("RUNTIME SWITCH DRY-RUN: would close UART/touch/fb, start %s (cwd /customer, LD_LIBRARY_PATH=%s), wait %d ms and exit",SLOT_A_EXE,sw_ldp,SW_WAIT_MS);
        SIM_DRY_BYE(mode,now)
        toast_show(TR(TX_T_DRYRUN),C_WARN,now); return;
    }
    int v0=view, x0=xsel;
    bye(mode); force_all=1; ui_frame(now); sw_seq_frame=++sw_seq;        /* (0) */
    if(dev_release) dev_release();         sw_seq_release=++sw_seq;        /* (1) */
    int hp=switch_launch(err,sizeof err);  sw_seq_launch=++sw_seq;         /* (2)+(3) */
    if(hp<=0){
        if(dev_reacquire) dev_reacquire();
        run=1; view=v0; xsel=x0; static_dirty=1; force_all=1;
        toast_show(TR(sw_fail==SWF_NOBIN?TX_T_SW_FAIL_NOBIN:(sw_fail==SWF_FORK?TX_T_SW_FAIL_FORK:TX_T_SW_FAIL_DIED)),C_ERR,now);   /* the detail (err) is in the log */
        boot_log("RUNTIME SWITCH FAILED: %s — merakiplus continues",err);
        return;
    }
    sw_child=hp;
    snprintf(rn,sizeof rn,"%s/RESTART_NEEDED",cfg_bakdir); int had=unlink(rn)==0;    /* stock has started fresh and reads config itself */
    boot_log("RUNTIME SWITCH B->A: reason=user-button, stock UI pid %d (%s, argv0 ./demo_v9), LD_LIBRARY_PATH=%s%s",hp,SLOT_A_EXE,sw_ldp,had?", RESTART_NEEDED removed":"");
}
static void sys_confirm(ms_t now){
    if(xsel==X_BOOTUI){
        if(x_edit==x_orig){ sys_close(now); return; }
        int want=x_edit;
        if(boot_flag_set(want)){ toast_show(TR(want?TX_T_BOOT_SAVED_OURS:TX_T_BOOT_SAVED_ORIG),C_OK,now); sys_close(now); }
        else { toast_show(TR(bs.lock!=BL_OK?TX_T_BOOT_LOCKED:TX_T_BOOT_ERR),C_ERR,now); x_edit=x_orig=bs.flag; static_dirty=1; fprintf(stderr,"merakiplus: BOOT-UI: %s\n",boot_err); }
    } else if(xsel==X_SWITCH){
        const char*g=switch_guard(now); if(g){ toast_show(g,C_WARN,now); return; }
        boot_poll(now);
        if(switch_locked()){ static_dirty=1; return; }
        if(bs.n_stock>0&&x_pending){ x_from_switch=1; sys_open(X_EXIT,now); return; }   /* paused stock UI w. stale mirror: say so first */
        do_switch(now);
    } else if(xsel==X_EXIT){
        if(x_from_switch) do_switch(now); else run=0;
    }
}
/* v6.8: the REVERT button on HOME has been removed. The same way out exists on SYSTEM: "SWITCH TO ORIGINAL NOW" is, in run.sh
 * mode, a plain exit (run.sh SIGCONTs; the reminder first if config has been saved), and in slot B boot it starts the
 * stock UI. In addition there is the switcher gesture and run.sh's trap. */
static void edit_step(int dir,int mult){
    Setting*s=&SET[sel]; if(s->flags&SF_LOCK) return;
    int v=edit_val+dir*s->step*mult;
    if(mult>1) v=(v/(s->step*mult))*(s->step*mult);   /* snap to coarse grid during acceleration */
    v=clampi(v,s->lo,s->hi);
    v=s->lo+((v-s->lo)/s->step)*s->step;              /* always on the stock picker's step grid */
    edit_val=v;
    if(s->flags&SF_BL) bl_set(edit_val);
    if(s->flags&SF_LOCAL) lang_set(edit_val);          /* v6.3: live preview — the whole UI switches language immediately; SAVE makes it permanent */
}

/* ---- hit-test ---- */
static int in_r(int x,int y,int rx,int ry,int rw,int rh){ return x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh; }
static int in_c(int x,int y,int cx,int cy,int r){ int dx=x-cx,dy=y-cy; return dx*dx+dy*dy<=r*r; }
/* v7.0: HOME's ring grid (the stock layout: grind, water / brew, steam) and the mode HOME is drawn in */
static const short RING_X[4]={160,320,160,320}, RING_Y[4]={154,154,326,326};
#define RING_R 72
static const int RING_ACT[4]={ACT_GRIND,ACT_WATER,ACT_BREW,ACT_STEAM};
static int ring_of_act(int k){ for(int i=0;i<4;i++) if(RING_ACT[i]==k) return i; return 0; }
enum { HM_IDLE=0, HM_DIM, HM_ACTIVE, HM_ERROR };
static int hm_mode=-1;
static int home_mode_of(int ph){
    if(phase_active(ph)||ph==PH_DONE) return HM_ACTIVE;
    if(ph==PH_ERROR) return HM_ERROR;
    if(ph==PH_OFFLINE||ph==PH_SLEEP) return HM_DIM;
    return HM_IDLE;
}
#define HOME_CHEV_Y 16
#define HOME_STAT_Y 46
#define HOME_CLOCK_Y 428
#define STAT_PITCH 100
#define HINT_W 304         /* v6.11: the chart area's hint lines (the chart itself is 244 wide; the hint text may use 304) */
#define BACK_X0 (CX-60)    /* v6.11: close chevron in detail/system: 88 x 46 px tap area above the title */
#define BACK_Y0 4
#define BACK_W 120
#define BACK_H 54
/* v7.0: the editor (a roller like the stock pickers + −/+ rings + an OK ring) and the system action (two pills) */
#define ED_TITLE_Y 32
#define TITLE_W 214        /* v7.0: a title near the top must fit the circle's chord there (2 x chord at the cap top, y 32) — F_L, else F_M, else F_S (+ … ); 232 let the ends of long F_L titles reach under the bezel */
#define ED_RANGE_Y 66
#define ED_CY 190          /* the roller's centre line */
#define ED_MX 82
#define ED_PX 398
#define ED_PM_R 29
#define ED_ROLL_X0 124
#define ED_ROLL_X1 356
#define ED_STEP_PX 50      /* roller: one value per 58 px (drag and drawing) */
#define ED_STAT_Y 300
#define ED_NOTE_Y 322
#define ED_OK_Y 390
#define ED_OK_R 36
#define SB_Y 350
#define SB_H 50
#define SB_W 136
#define SB_X0 (CX-SB_W-8-4)
#define SB_X1 (CX+8-4)
/* v6.17: a page's header = chevron (y 22..30) + title (y 44) + one line under it (y 70). The whole header is the close target. */
#define PBACK_X0 (CX-80)
#define PBACK_Y0 4
#define PBACK_W 160
#define PBACK_H 74
#define PG_CHEV_Y 16
#define PG_TITLE_Y 32
#define PG_LIVE_Y 64
/* v6.17: HOLD TO START on a feature page — same hold (HOLD_MS) and same guard as the HOME icon */
#define START_R 33         /* v7.0: HOLD TO START is a ring button (play icon) with its caption under it, like the stock screens */
#define START_CY 384
#define START_CAP_Y 428
#define START_HIT_W 200
#define START_HIT_Y 348
#define START_HIT_H 98
/* v6.13: the STOP buttons' tap areas (hit) and drawn pills. ONE button: same tap area as v6.6's STOP STEAM (180 x 56), the pill
 * 160 x 44 in F_S (v6.6-v6.12: 150 — "STOP WATER" / "STOP KVÆRN" are 123 / 122 px and need 14 px of space on each side).
 * TWO buttons (main activity + latched steam): two areas of 148 x 56 each side by side, no gap between; the pills 136 x 44 in F_XS. */
/* v6.17: STOP is the most important control while something runs, so it is the largest and brightest thing below the numbers: ONE button
 * 216 x 54 in F_M (tap area 232 x 60), TWO buttons 152 x 54 in F_S (tap areas 160 x 60, no gap between) — bright red with white text
 * (v6.13-v6.16: 160 x 44 / 136 x 44 in a dark red that read as "disabled"). */
#define STOP_PILL1_W 200
#define STOP_PILL2_W 148
#define STOP_PILL_H 56
#define STOP_PILL_Y 364
#define STOP_Y 358
#define STOP_H 68
static Rect stop_hit(int n,int i){ if(n<=1) return R(CX-110,STOP_Y,220,STOP_H); return i==0?R(76,STOP_Y,164,STOP_H):R(240,STOP_Y,164,STOP_H); }
static Rect stop_pill(int n,int i){ if(n<=1) return R(CX-STOP_PILL1_W/2,STOP_PILL_Y,STOP_PILL1_W,STOP_PILL_H); return i==0?R(86,STOP_PILL_Y,STOP_PILL2_W,STOP_PILL_H):R(246,STOP_PILL_Y,STOP_PILL2_W,STOP_PILL_H); }
static int stop_font(int n){ return n>1?F_S:F_M; }
static int diag_hint(int ph){ return ph==PH_ERROR||ph==PH_OFFLINE; }      /* HOME's hint line points to DIAGNOSTICS -> it is a tap target */
static int hit_test(int x,int y,ms_t now){
    if(view==V_SPLASH||view==V_BYE||view==V_OFF) return T_NONE;
    if(tr.type&&!(view==V_MAIN&&page==PG_HOME&&hm_mode==HM_ACTIVE)) return T_NONE;   /* v7.0: the fade/slide INTO the live view must not swallow STOP */
    if((view==V_SYS||view==V_DETAIL)&&in_r(x,y,BACK_X0,BACK_Y0,BACK_W,BACK_H)) return T_BACK;   /* v6.11: close chevron at the top */
    if(view==V_SYS){
        if(sys_locked()){ if(in_r(x,y,170,SB_Y-4,140,SB_H+8)) return T_CANCEL; return T_NONE; }
        if(xsel==X_BOOTUI){ if(in_c(x,y,ED_MX,ED_CY,40)) return T_MINUS; if(in_c(x,y,ED_PX,ED_CY,40)) return T_PLUS; }
        if(in_r(x,y,SB_X0,SB_Y-4,SB_W+8,SB_H+8)) return T_CANCEL;
        if(in_r(x,y,SB_X1,SB_Y-4,SB_W+8,SB_H+8)) return T_SAVE;
        return T_NONE;
    }
    if(view==V_DETAIL){
        if(SET[sel].flags&SF_LOCK){ if(in_c(x,y,CX,ED_OK_Y,ED_OK_R+8)) return T_CANCEL; return T_NONE; }   /* locked/sensor: BACK only */
        if(in_c(x,y,ED_MX,ED_CY,40)) return T_MINUS;
        if(in_c(x,y,ED_PX,ED_CY,40)) return T_PLUS;
        if(in_c(x,y,CX,ED_OK_Y,ED_OK_R+8)) return T_SAVE;
        if(in_r(x,y,ED_ROLL_X0,ED_CY-110,ED_ROLL_X1-ED_ROLL_X0,220)) return T_ROLL;
        return T_NONE;
    }
    if(page==PG_HOME){
        int sk[2], sn=stop_btns(now,sk);
        if(sn){ for(int i=0;i<sn;i++){ Rect r=stop_hit(sn,i); if(in_r(x,y,r.x0,r.y0,r.x1-r.x0,r.y1-r.y0)) return i?T_STOPS:T_STOP; } }   /* v6.13: STOP for grind/brew/water + latched steam */
        if(hm_mode==HM_ACTIVE){ if(pinfo_hit(x,y)) return T_PINFO; return T_NONE; }
        if(diag_hint_shown()&&in_r(x,y,70,HOME_STAT_Y-2,340,32)) return T_DIAG;   /* offline: the status line -> DIAGNOSTICS (before the chevron, whose area reached down over it) */
        if(in_r(x,y,CX-60,0,120,58)) return T_GEAR;                                 /* v7.0: the chevron at the top -> SYSTEM */
        if(hm_mode==HM_ERROR){ if(in_r(x,y,60,96,360,250)) return T_DIAG; return T_NONE; }
        if(!phase_active(phase_raw(now))) for(int i=0;i<4;i++) if(in_c(x,y,RING_X[i],RING_Y[i],RING_R+4)) return T_ACT0+RING_ACT[i];
        return T_NONE;
    }
    if(in_r(x,y,PBACK_X0,PBACK_Y0,PBACK_W,PBACK_H)) return T_BACK;              /* v6.17: every page but HOME closes with its header chevron */
    if(pg_list(page)){
        if(y>=LIST_VY0&&y<list_vy1(page)&&x>=64&&x<416){
            int n=page_rows(page);
            for(int k=0;k<n;k++){ float ry=row_y0(page,k)-scroll[page]; if(y>=ry-4&&y<ry+ROW_H+4) return T_ROW0+k; }
        }
        if(pg_feat(page)&&in_r(x,y,CX-START_HIT_W/2,START_HIT_Y,START_HIT_W,START_HIT_H)) return T_START;
        return T_NONE;
    }
    if(page==PG_DIAG&&y>=LIST_VY0&&y<LIST_VY1){                      /* v6.9: the GRAPH pill (smooth / raw) */
        float py=DG_GRAPH+12-scroll[PG_DIAG]; if(x>=CX-150&&x<CX+150&&y>=py-8&&y<py+36) return T_GRAPH;   /* v6.11: 44 px high (v6.10: 40) */
    }
    return T_NONE;
}
/* v6.13: STOP pressed (or the ctrl hook). Queues stop frame(s) and shows "STOPPING …"; stop_step() confirms when the bits are
 * cleared ("… STOPPED"), or reports an error (no ACK after retransmit / not cleared within STOP_CONFIRM_MS). */
static void stop_request(int kind,ms_t now){
    if(kind<ACT_GRIND||kind>ACT_STEAM) return;
    if(ro_mode){ toast_show(TR(TX_G_RO),C_WARN,now); return; }
    if(stp.active&&stp.kind==kind&&wq_busy()){ toast_show(TR(STOP_SENT[kind]),C_TXT,now); return; }   /* the stop frame is already in the queue */
    if(!act_push_stop(kind)){ stp.n_fail++; toast_show(TR(TX_T_STOP_FAIL),C_ERR,now); return; }
    stp.n_req++; stp.kind=kind; stp.t_req=now; stp.seq=wq.seq;
    stp.active=link_up(now)&&stop_running(kind);            /* only a running activity can be confirmed (the ctrl hook can send without one) */
    toast_show(TR(STOP_SENT[kind]),C_TXT,now);
}
static void stop_step(ms_t now){
    if(!stp.active) return;
    if(!link_up(now)){ stp.active=0; return; }                                                       /* the OFFLINE view takes over */
    if(!stop_running(stp.kind)){ stp.active=0; stp.n_ok++; toast_show(TR(STOP_DONE[stp.kind]),C_OK,now); return; }
    if(wq.seq!=stp.seq&&wq.result==WR_FAIL&&!wq_busy()){ stp.active=0; stp.n_fail++; toast_show(TR(TX_T_STOP_NOACK),C_ERR,now); return; }
    if(now-stp.t_req>=STOP_CONFIRM_MS){ stp.active=0; stp.n_fail++; toast_show(TR(TX_T_STOP_UNCONF),C_WARN,now); }
}
static void activate(int t,ms_t now){
    if(t==T_STOP||t==T_STOPS){ int sk[2]; stop_btns(now,sk); int k=sk[t==T_STOPS]; if(k!=ACT_NONE) stop_request(k,now); return; }   /* v6.13 */
    if(t>=T_ACT0&&t<=T_ACT3){ nav_open(PG_GRIND+(t-T_ACT0),now); return; }          /* v6.17: a TAP opens the feature's page (v6.6-v6.16: only a toast "HOLD THE BUTTON TO START"); the HOLD still starts */
    if(t==T_START){ const char*g=act_guard(page-PG_GRIND,now); toast_show(g?g:TR(TX_T_HOLD_HINT),g?C_WARN:C_TXT,now); return; }   /* a tap on HOLD TO START: say what is missing */
    if(t==T_GEAR){ nav_open(PG_SYS,now); return; }                                   /* v6.11; v6.17: -> SYSTEM */
    if(t==T_DIAG){ nav_open(PG_DIAG,now); return; }                                  /* v6.17 */
    if(t==T_BACK&&view==V_MAIN){ nav_back(now); return; }                            /* v6.17: a page's header chevron */
    if(t==T_BACK) t=T_CANCEL;                                                        /* v6.11: chevron = CANCEL/BACK */
    if(t==T_GRAPH){ cd.raw=!cd.raw; hf.ok=0; return; }          /* v6.9: display only — no config, no frames, forgotten on restart */
    if(t==T_PINFO){ char b[96]; snprintf(b,sizeof b,TR(TX_T_PINFO),p_anchor_bar(),P_ANCHOR_RAW); toast_show(b,C_TXT,now); return; }   /* v6.10: the basis for ≈BAR */
    if(t==T_CANCEL){ if(view==V_SYS){ sys_close(now); return; } if(!sav.active) detail_close(now); return; }
    if(t==T_SAVE){ if(view==V_SYS){ sys_confirm(now); return; } save_begin(now); return; }
    if(t>=T_ROW0){
        Row r[ROW_MAX]; int n=rows_get(page,r), k=t-T_ROW0; if(k>=n) return;
        if(r[k].kind==RK_SET) detail_open(r[k].a,now);
        else if(r[k].kind==RK_SEC) sec_toggle(r[k].a);
        else if(r[k].kind==RK_X) sys_open(r[k].a,now);                 /* system action (SYSTEM > STOCK UI) */
        else nav_open(PG_DIAG,now);
        return;
    }
}
enum { TE_DOWN, TE_MOVE, TE_UP };
/* v6.11: SWIPE GUARD. Page switch only for a gesture that STARTED horizontally (axis lock at the first 14 px), is >= 60 px long and
 * at most ~30° from horizontal (|dx| >= 1.7·|dy|). v6.10 allowed |dy| <= 50 regardless of direction, so a diagonal drag (e.g. dx 70, dy 45) on a
 * list that started slightly horizontal switched page. A gesture that starts vertically can never switch page — not even
 * on pages without scrolling (HOME).
 * v6.17: there is no carousel any more. The only horizontal gesture is "swipe RIGHT = back" (same guard), on every page and editor
 * except HOME — a shortcut for the visible chevron. A swipe LEFT does nothing, and nothing on HOME reacts to a swipe. */
static int swipe_h(int axis,int dx,int dy){ return axis==1&&abs(dx)>=60&&10*abs(dx)>=17*abs(dy); }
/* v6.11: swipe DOWN (>= 70 px, vertically locked, |dy| >= 1.7·|dx|) closes the detail/system view like CANCEL — not if
 * the gesture started on −/+ (there the press-down has already changed the value) */
static int swipe_down(int axis,int target,int dx,int dy){ return axis==2&&dy>=70&&10*dy>=17*abs(dx)&&target!=T_MINUS&&target!=T_PLUS&&target!=T_ROLL; }
