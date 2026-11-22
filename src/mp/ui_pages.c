/* src/mp/ui_pages.c — per-frame logic, drawing, pages (HOME, lists, editor, system action, power, DIAGNOSTICS, splash), composition.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ================= per-frame logic ================= */
static int gauge_kind(int ph){ switch(ph){ case PH_GRIND: case PH_WATER: return 1; case PH_PRE: case PH_EXT: return 2; case PH_STEAM: return 3; default: return 0; } }
typedef struct { int ph,eff; uint32_t hi,lo; char hero[16]; const char*unit; char label[64],sub[80]; const char*sl[3]; char svv[3][16]; float gf,gmark,pf; int pvis;
                 int sk,conc; const char*chip,*badge;
                 char hint1[72]; const char*hint2; uint32_t hc1; char hint2b[72]; char alab[4][16]; } HomeM;   /* v6.17: alab = each icon's target, drawn under it */   /* v6.11: empty-state hint in the chart area (only when the chart is inactive) */   /* v6.6: steam class, concurrent steam, chip text, steam marker (NULL = none) */
/* the three stat labels per HOME state (the selftest measures each trio against the columns, in both languages) */
enum { SS_IDLE=0, SS_BREW, SS_GRIND, SS_WATER, SS_STEAM, SS_BREW_M, SS_GRIND_M, SS_WATER_M, NSS };   /* _M: concurrent steam — the 3rd field is the milk temperature */
_Static_assert(SS_GRIND_M-SS_GRIND==SS_BREW_M-SS_BREW&&SS_WATER_M-SS_WATER==SS_BREW_M-SS_BREW,"SS_*_M must be at the same offset from SS_*");
static const short STAT_SET[NSS][3]={
 [SS_IDLE] ={TX_ST_GROUP,TX_ST_STEAMBOILER,TX_ST_WEIGHT},
 [SS_BREW] ={TX_ST_TIME,TX_ST_BAR,TX_ST_FLOW},
 [SS_GRIND]={TX_ST_TIME,TX_ST_CURRENT,TX_ST_GRINDSTEP},
 [SS_WATER]={TX_ST_TIME,TX_ST_FLOW,TX_ST_BOILER},
 [SS_STEAM]={TX_ST_TIME,TX_ST_BOILER,TX_ST_LEVEL},          /* v6.11: BOILER (the steam context) — "STEAM" next to "LEVEL" reads as "STEAM LEVEL" */
 [SS_BREW_M] ={TX_ST_TIME,TX_ST_BAR,TX_ST_MILK},
 [SS_GRIND_M]={TX_ST_TIME,TX_ST_CURRENT,TX_ST_MILK},
 [SS_WATER_M]={TX_ST_TIME,TX_ST_FLOW,TX_ST_MILK},
};
#define STAT_PITCH 94
#define STAT_GAP 16        /* v6.11: minimum space between two stat labels (v6.10: 6 px — "GROUP STEAM BOILER WEIGHT" read as one line) */
#define SUB_X0 72          /* the HOME subline's area (v6.3: 336 px — also room for °F values and the English texts) */
#define SUB_W 336
static HomeM hm;
static int steam_target(void){ return reg_seen[25]?reg[25]:sv(S_STEAMT); }
#define MILK_RISE10 39     /* the milk probe keeps rising approx. 3.9 °C after auto-stop (STEAMLIVE §5.2: 650 -> 689 in ~11 s) */
/* v6.9: the hero filter (median-3 + EMA at a 10 Hz rate). key = phase<<8 | source word, set = setpoint/target: if either changes,
 * the number snaps to the raw value. Returns the shown value in raw units (rounded) — RAW when DIAGNOSE → GRAPH is RAW. */
static int hero_v(int key,int raw,int set,ms_t now){
    if(cd.raw){ hf.ok=0; return raw; }
    if(!hf.ok||key!=hf.key||set!=hf.set||now-hf.t>500||now<hf.t){ cond_snap(&hf.f,raw); hf.key=key; hf.set=set; hf.t=now; hf.ok=1; hf.n_snap++; return raw; }
    while(now-hf.t>=100){
        hf.t+=100; cond_step(&hf.f,raw,cd.a_hero);
        if(abs(hf.f.m-(int)lroundf(hf.f.y))>CD_HERO_JUMP){ cond_snap(&hf.f,hf.f.m); hf.n_snap++; }   /* real step (the median has moved far): no gliding */
    }
    return (int)lroundf(hf.f.y);
}
/* v6.16: HOME texts — the top line (local clock as stock's update_time; wordmark while the clock is not set) and the planned pre-heat */
#define HOME_BRAND_Y 54
static int home_clock_text(char*b,size_t n){
    struct tm lt; if(!clk_now(&lt)) return 0;
    fmt_hm(b,n,lt.tm_hour,lt.tm_min,1); return 1;
}
static int home_preheat_text(char*b,size_t n){
    int ad,ah,am; int64_t at; if(!sched_alarm(clk_utc(),&ad,&ah,&am,&at)) return 0;
    struct tm lt; clk_local_at(at,&lt); char t[24]; fmt_hm(t,sizeof t,ah,am,1);
    snprintf(b,n,TR(TX_C_PREHEAT),TR(WD_TX[lt.tm_wday]),t); return 1;
}
static void home_model(ms_t now){
    HomeM*m=&hm; memset(m,0,sizeof *m);
    int ph=act.ph, e=(ph==PH_DONE)?act.last_ph:ph; m->ph=ph; m->eff=e;
    m->hi=ph_hi(ph); m->lo=ph_lo(ph); m->gmark=-1; m->pvis=0;
    ms_t el=phase_active(ph)? now-(ph==PH_STEAM?stm.t0:act.t_act0) : act.done_ms; char tb[16]; snprintf(tb,sizeof tb,"%ld.%ld %s",(long)(el/1000),(long)((el%1000)/100),TR(TX_U_S));
    char a[16],b[16]; int ss=SS_IDLE;
    if(e==PH_GRIND||e==PH_WATER||e==PH_PRE||e==PH_EXT){
        int tgt=(e==PH_GRIND)?sv(S_GRINDW):(e==PH_WATER?sv(S_HOTW):sv(S_BREWW));
        int src=wsl.src, w=wdbi(hero_v(ph<<8|src,s16(src),tgt,now)); if(w<0&&w>-30)w=0; if(w<-999)w=-999; if(w>9999)w=9999;   /* v6.9: latched source, conditioned, deadband */
        fmt_fix(m->hero,sizeof m->hero,w,1); m->unit=TR(TX_U_G);
        fmt_fix(a,sizeof a,tgt,1);
        snprintf(m->label,sizeof m->label,"%s",TR(e==PH_GRIND?TX_H_DOSE:TX_H_CUP));   /* v6.17: the target is said ONCE — in the sub-line ("STOPS AT 36.0 G"); v6.16 had it in the label too */
        if(ph==PH_DONE) snprintf(m->sub,sizeof m->sub,TR(TX_H_DONE_IN),tb);
        else snprintf(m->sub,sizeof m->sub,TR(TX_H_AUTOSTOP_G),a);
        float fr=tgt>0? (float)w/tgt : 0;
        if(e==PH_PRE||e==PH_EXT){
            float pd=p_disp(now);                                   /* v6.9: gauge + BAR = conditioned pressure (like the chart) */
            m->gf=clampf(pd/1600.f,0,1); m->pf=clampf(fr,0,1); m->pvis=1; ss=SS_BREW;
            snprintf(m->svv[0],16,"%s",tb);
            if(cd.raw) snprintf(m->svv[1],16,"%d",(int)lroundf(pd));   /* v6.10: the RAW view = word 51 as a raw number (label PRESSURE RAW) */
            else { float pb=p_bar(pd); if(pb<0) snprintf(m->svv[1],16,"%s",TR(TX_ST_PULSE)); else snprintf(m->svv[1],16,"%.1f",pb); }   /* v6.10: ≈bar, pulse > 20 bar = no number */
            snprintf(m->svv[2],16,"%u",reg[52]);
        } else {
            m->gf=clampf(fr/1.15f,0,1); m->gmark=1/1.15f;
            snprintf(m->svv[0],16,"%s",tb);
            if(e==PH_GRIND){ ss=SS_GRIND; snprintf(m->svv[1],16,"%u",reg[58]); snprintf(m->svv[2],16,"%u",reg[37]); }
            else { ss=SS_WATER; snprintf(m->svv[1],16,"%u",reg[52]); fmt_t10(b,sizeof b,reg[40],1); snprintf(m->svv[2],16,"%s°",b); }
        }
        /* v6.6: steam CONCURRENT with the main activity (e.g. flush during brew, work_sta 5327): weight and gauge
         * stay, the steam marker is shown, and the 3rd stat field becomes the milk temperature */
        if(stm.kind!=SK_NONE&&phase_active(ph)){ m->conc=1; m->sk=stm.kind; ss+=SS_BREW_M-SS_BREW; fmt_t10(b,sizeof b,reg[48],1); snprintf(m->svv[2],16,"%s°",b); }
    } else if(e==PH_STEAM){
        int sk=(ph==PH_DONE)?stm.last_kind:stm.kind, tgt=steam_target(); m->sk=sk;
        int mk=hero_v(ph<<8|48,reg[48],tgt*4+sk,now);              /* v6.9: conditioned milk temperature */
        fmt_t10(m->hero,sizeof m->hero,mk,1); m->unit=t_unit(); ss=SS_STEAM;
        fmt_t10(a,sizeof a,tgt,1);
        m->gf=clampf((mk-200)/600.f,0,1);
        if(sk==SK_WAND){            /* momentary wand/flush: no target, no auto-stop expectation — not even above the target */
            snprintf(m->label,sizeof m->label,"%s",TR(TX_H_MILK_PROBE));
            if(ph==PH_DONE) snprintf(m->sub,sizeof m->sub,TR(TX_H_DONE_IN),tb);
            else snprintf(m->sub,sizeof m->sub,"%s",TR(TX_H_WAND_NOSTOP));
            m->gmark=-1;
        } else {                    /* latched (AUTO) — or word 24 not read yet: the target is shown, but without a promise of auto-stop */
            if(sk==SK_AUTO&&ph!=PH_DONE) snprintf(m->label,sizeof m->label,"%s",TR(TX_H_MILK));   /* v6.17: the sub-line carries the target ("STOPS AT 65.0°") */
            else snprintf(m->label,sizeof m->label,TR(TX_H_MILK_TGT),a);
            if(ph==PH_DONE){
                if(sk==SK_AUTO){ fmt_fix(b,sizeof b,use_c()?MILK_RISE10:MILK_RISE10*9/5,-1); snprintf(m->sub,sizeof m->sub,TR(TX_H_DONE_RISE),tb,b); }
                else snprintf(m->sub,sizeof m->sub,TR(TX_H_DONE_IN),tb);
            } else if(sk==SK_AUTO) snprintf(m->sub,sizeof m->sub,TR(TX_H_AUTOSTOP_T),a);
            m->gmark=clampf((tgt-200)/600.f,0,1);
        }
        snprintf(m->svv[0],16,"%s",tb);
        fmt_t10(b,sizeof b,reg[47],0); snprintf(m->svv[1],16,"%s°",b);
        snprintf(m->svv[2],16,"%d",sv(S_STEAMG));
    } else {
        int set=reg_seen[11]?reg[11]:sv(S_BREWT);
        if(ph==PH_OFFLINE){ snprintf(m->hero,sizeof m->hero,"--.-"); m->gf=0; }
        else { int bt=hero_v(ph<<8|40,reg[40],set,now);           /* v6.9: conditioned boiler temperature */
               fmt_t10(m->hero,sizeof m->hero,bt,1); m->gf=clampf((bt-200)/1000.f,0,1); m->gmark=clampf((set-200)/1000.f,0,1); }
        m->unit=t_unit(); fmt_t10(a,sizeof a,set,1);
        snprintf(m->label,sizeof m->label,"%s",TR(TX_H_BOILER));
        switch(ph){
        case PH_OFFLINE: snprintf(m->sub,sizeof m->sub,"%s",TR(TX_H_NORESP)); break;
        case PH_SLEEP:   snprintf(m->sub,sizeof m->sub,"%s",TR(TX_H_WAKING)); break;
        case PH_ERROR:   snprintf(m->sub,sizeof m->sub,"%s",err_text()); break;
        case PH_HEAT:    snprintf(m->sub,sizeof m->sub,TR(TX_H_HEATING_TO),a); break;
        default:         snprintf(m->sub,sizeof m->sub,TR(TX_H_SET_TO),a); break;
        }
        fmt_t10(b,sizeof b,reg[42],0); snprintf(m->svv[0],16,"%s°",b);
        fmt_t10(b,sizeof b,reg[47],0); snprintf(m->svv[1],16,"%s°",b);
        int w=s16(39); if(w<-50||ph==PH_OFFLINE) snprintf(m->svv[2],16,"--"); else { fmt_fix(b,sizeof b,w_disp(39,now),1); snprintf(m->svv[2],16,"%s %s",b,TR(TX_U_G)); }   /* v6.9: conditioned + deadband (the "--" threshold on raw) */
        if(ph==PH_OFFLINE){ snprintf(m->svv[0],16,"--"); snprintf(m->svv[1],16,"--"); }
    }
    for(int i=0;i<3;i++) m->sl[i]=TR(STAT_SET[ss][i]);
    /* v6.11: hint in the empty chart area — what the user can DO now (replaces v6.10's "NO ACTIVITY", which repeated the phase chip).
     * If a start is possible: "HOLD AN ICON TO START"; otherwise the reason (the guard's text) — and on error/offline a pointer to DIAGNOSE,
     * where the raw code (E1/E2/E3) and the link counters are. Line 2 points to the settings (swipe + the gear). */
    { const char*g=act_guard(ACT_BREW,now); m->hint2=NULL; m->hc1=C_TXT3;      /* v6.17: ONE hint line ("HOLD = START · TAP = SETTINGS"); v6.11-v6.16: two */
      if(home_preheat_text(m->hint2b,sizeof m->hint2b)) m->hint2=m->hint2b;    /* v6.16: a planned pre-heat is the second line */
      if(!g) snprintf(m->hint1,sizeof m->hint1,"%s",TR(TX_C_HINT));
      else if(ph==PH_ERROR) snprintf(m->hint1,sizeof m->hint1,TR(TX_C_ERRCODE),err_code());
      else if(ph==PH_OFFLINE) snprintf(m->hint1,sizeof m->hint1,"%s",TR(TX_C_DIAGREF));
      else if(ph==PH_SLEEP) m->hint1[0]=0;                                   /* the subline already says "WAKING UP…" */
      else { snprintf(m->hint1,sizeof m->hint1,"%s",g); m->hc1=C_TXT2; if(g==TR(TX_G_NOCFG)||g==TR(TX_G_OOR)||g==TR(TX_G_RO)) m->hc1=C_WARN; }
      if(g&&diag_hint(ph)) m->hc1=C_TXT2; }
    /* v6.17: what each icon will do, under the icon (grind dose, brew weight, water amount, milk temperature) */
    { char u[16];
      fmt_fix(u,sizeof u,sv(S_GRINDW),1); snprintf(m->alab[ACT_GRIND],16,"%s %s",u,TR(TX_U_G));
      fmt_fix(u,sizeof u,sv(S_BREWW),-1); snprintf(m->alab[ACT_BREW],16,"%s %s",u,TR(TX_U_G));
      fmt_fix(u,sizeof u,sv(S_HOTW),-1);  snprintf(m->alab[ACT_WATER],16,"%s %s",u,TR(TX_U_G));
      fmt_t10(u,sizeof u,sv(S_STEAMT),0); snprintf(m->alab[ACT_STEAM],16,"%s\xC2\xB0",u);
      for(int k=0;k<4;k++){ static const int si[4]={S_GRINDW,S_BREWW,S_HOTW,S_STEAMT}; if(!SET[si[k]].loaded) snprintf(m->alab[k],16,"--"); } }
    if(cd.raw&&(ss==SS_BREW||ss==SS_BREW_M)) m->sl[1]=TR(TX_ST_PRAW);   /* v6.10: the RAW view shows raw numbers — so ≈BAR is not shown above them */
    m->chip=(ph==PH_STEAM)?steam_label(stm.kind):ph_name(ph);
    m->badge=m->conc?steam_label(m->sk):NULL;
    /* pending / hold override the subline. v6.6: steam is "STARTING …" too — display steam starts by
     * itself approx. 0.2 s after the start write, without the wand switch being activated (STEAMLIVE §3 + §6.3) */
    if(act.pending!=ACT_NONE&&!phase_active(ph)) snprintf(m->sub,sizeof m->sub,TR(TX_H_STARTING),TR(ACT_NAME[act.pending]));
    else if(tch.down&&!tch.moved&&tch.target>=T_ACT0&&tch.target<=T_ACT3&&!act_guard(tch.target-T_ACT0,now)){
        int k=tch.target-T_ACT0;
        if(k==ACT_GRIND){ fmt_fix(a,sizeof a,sv(S_GRINDW),1); snprintf(m->sub,sizeof m->sub,TR(TX_H_HOLD_GRIND),a); }
        else if(k==ACT_BREW){ fmt_fix(a,sizeof a,sv(S_BREWW),1); snprintf(m->sub,sizeof m->sub,TR(TX_H_HOLD_BREW),a); }
        else if(k==ACT_WATER){ fmt_fix(a,sizeof a,sv(S_HOTW),1); snprintf(m->sub,sizeof m->sub,TR(TX_H_HOLD_WATER),a); }
        else { fmt_t10(a,sizeof a,sv(S_STEAMT),1); snprintf(m->sub,sizeof m->sub,TR(TX_H_HOLD_STEAM),a); }
    }
}
/* gauge targets for the current view (called per frame AND before the snapshot on transitions) */
static void arc_targets(ms_t now){
    if(view==V_SPLASH){
        float t=clampf((now-ui_start)/(float)(SPLASH_MS-250),0,1);
        garc.vis=1; garc.c_hi=C_ACC; garc.c_lo=rgb(120,80,36); garc.mark=-1; garc.f_t=garc.f_d=ease_io(t); parc.vis=0; ui_anim=1;
    } else if(view==V_DETAIL){
        const Setting*s=&SET[sel]; float span=(float)(s->hi-s->lo);
        garc.vis=1; garc.c_hi=C_ACC; garc.c_lo=rgb(120,80,36); parc.vis=0;
        garc.f_t=span>0? clampf((edit_val-s->lo)/span,0,1) : 0;
        garc.mark=(span>0&&!(s->flags&SF_SENSOR))? clampf((edit_orig-s->lo)/span,0,1) : -1;
    } else if(view==V_SYS){
        parc.vis=0; garc.vis=1; garc.mark=-1;
        if(xsel==X_EXIT){ garc.c_hi=C_WARN; garc.c_lo=rgb(128,80,24); garc.f_t=1; }            /* full warning ring */
        else {
            garc.c_hi=C_ACC; garc.c_lo=rgb(120,80,36); garc.f_t=0;
            if(xsel==X_BOOTUI&&!sys_locked()){ garc.f_t=x_edit?1:0; garc.mark=x_orig?1:0; }
        }
    } else if(view==V_BYE||view==V_OFF){ garc.vis=0; parc.vis=0;
    } else if(page==PG_HOME){
        home_model(now);
        int gk=gauge_kind(hm.eff); if(gk!=act.gkind){ act.gkind=gk; garc.f_d=0; }   /* sweep from zero on a new quantity */
        float ct=clampf((now-act.t_col)/300.f,0,1);
        garc.vis=1; garc.f_t=hm.gf; garc.mark=hm.gmark;
        if(ct<1){ int q=(int)(ct*8); garc.c_hi=lerpc(act.c_from_hi,hm.hi,q/8.f); garc.c_lo=lerpc(act.c_from_lo,hm.lo,q/8.f); ui_anim=1; }
        else { garc.c_hi=hm.hi; garc.c_lo=hm.lo; }
        parc.vis=hm.pvis; parc.c_hi=C_TXT; parc.c_lo=rgb(160,167,180);   /* v6.11: v6.10's C_TXT2 — the progress ring is unchanged (C_TXT2 was raised for text) */ parc.f_t=hm.pf; parc.mark=-1;
    } else { garc.vis=0; parc.vis=0; }
}
static void ui_update(ms_t now){
    if(ui_start<0){ ui_start=now; ui_last=now; }
    float dt=(float)(now-ui_last); if(dt>200)dt=200; ui_last=now; ui_anim=0;
    /* phase tracking (v6.6: the steam class first; the main phase is phase_main — grind/brew/water take precedence over steam) */
    steam_track(now);
    int p=phase_main(now);
    if(p!=act.raw){
        if(phase_active(p)&&!phase_active(act.raw)){ act.t_act0=now; act.pending=ACT_NONE; }
        if(!phase_active(p)&&phase_active(act.raw)){ act.t_done=now; act.done_ms=now-(act.raw==PH_STEAM?stm.t0:act.t_act0); }
        act.raw=p;
    }
    if(phase_active(p)) act.last_ph=p;
    int ph=p; if(p>=PH_HEAT&&!phase_active(p)&&now-act.t_done<8000) ph=PH_DONE;
    if(ph!=act.ph){ act.c_from_hi=garc.c_hi; act.c_from_lo=garc.c_lo; act.t_col=now; act.ph=ph; }
    /* pending activity: ACK result / start timeout */
    if(act.pending!=ACT_NONE){
        if(wq.seq!=act.seq&&wq.result==WR_FAIL&&!wq_busy()){ act.pending=ACT_NONE; toast_show(TR(TX_T_NOACK),C_ERR,now); }
        else if(now-act.t_req>(act.pending==ACT_STEAM?15000:3500)){ act.pending=ACT_NONE; toast_show(TR(TX_T_NOSTART),C_WARN,now); }
    }
    stop_step(now);                                                  /* v6.13: manual STOP — confirmation / failure */
    save_step(now);
    /* v6.17: a feature page is the setup for something that is NOT running. As soon as an activity runs (started here, on the machine or
     * by the wand), the UI is on HOME — the one live view, where STOP is. SYSTEM / DIAGNOSTICS and open editors stay (deliberate). */
    if(view==V_MAIN&&pg_feat(page)&&phase_active(p)&&!tr.type) go_page(PG_HOME,-1,now);
    /* boot state is live (stat + /proc), but only while it is actually shown */
    if((view==V_SYS||(view==V_MAIN&&(page==PG_SYS||page==PG_DIAG)))&&(now-bs.t>=1000||now<bs.t)){
        int l0=(view==V_SYS)?sys_locked():0, m0=switch_mode(), n0=boot_next_kind();
        boot_poll(now);
        if(view==V_SYS){
            if(xsel==X_BOOTUI){ if(x_edit==x_orig) x_edit=bs.flag; x_orig=bs.flag; }
            if(sys_locked()!=l0||switch_mode()!=m0||boot_next_kind()!=n0) static_dirty=1;     /* static text depends on the state */
        }
    }
    /* sensor rows (read-only) follow the controller register */
    for(int i=0;i<NSET;i++){
        Setting*s=&SET[i]; if(!(s->flags&SF_SENSOR)) continue;
        s->loaded=link_up(now)&&reg_seen[s->def]; s->val=s->loaded?reg[s->def]:0;
        if(view==V_DETAIL&&sel==i) edit_val=edit_orig=s->val;
    }
    /* splash -> home */
    if(view==V_SPLASH&&now-ui_start>=SPLASH_MS&&!tr.type){
        memcpy(snapA,canvas,(size_t)W*H*4); view=V_MAIN; static_dirty=1; garc.f_d=0; act.gkind=-1; arc_targets(now); garc.f_d=0; compose(now);
        memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,320);
    }
    /* hold-to-start + stepper repeat w. acceleration */
    if(tch.down&&!tch.moved){
        if(tch.target>=T_ACT0&&tch.target<=T_ACT3){
            ui_anim=1;
            if(!tch.fired&&now-tch.t0>=HOLD_MS){ tch.fired=1; act_request(tch.target-T_ACT0,now); }
        }
        if(tch.target==T_START&&view==V_MAIN&&pg_feat(page)){      /* v6.17: HOLD TO START on the feature page — the same request as the icon's hold */
            ui_anim=1;
            if(!tch.fired&&now-tch.t0>=HOLD_MS){ int k=page-PG_GRIND; tch.fired=1; act_request(k,now); if(act.pending==k) go_page(PG_HOME,-1,now); }
        }
        if((tch.target==T_MINUS||tch.target==T_PLUS)&&view==V_DETAIL&&!sav.active){
            if(tch.rep_next<now-500) tch.rep_next=now;                 /* v6.20: never catch up on a stale deadline (at most one step after a long gap) */
            while(now>=tch.rep_next){
                tch.rep_n++; int mult=tch.rep_n>28?5:1;
                edit_step(tch.target==T_PLUS?1:-1,mult);
                tch.rep_next+= tch.rep_n<5?170:(tch.rep_n<14?90:45);
            }
            ui_anim=1;
        }
    }
    arc_targets(now);
    if(garc.vis&&view!=V_SPLASH) ui_anim|=arc_ease(&garc,dt);
    if(parc.vis) ui_anim|=arc_ease(&parc,dt);
    if(rip.on){ if(now-rip.t0>360) rip.on=0; ui_anim=1; }
    if(toast.dur&&now-toast.t0<toast.dur+50) ui_anim=1;
    if(sav.active||tr.type||tch.scrolling) ui_anim=1;
}

/* ================= drawing: shared parts ================= */
/* The wordmark: BRAND_WORD + "+" badge. Large fonts (cap height >= 24 px): crema disc with
 * a dark plus, raised like a superscript. Small fonts: the plus glyph in the accent colour.
 * brand_at() returns x after the mark (incl. normal letter spacing), so text can continue. */
static int brand_disc(int f){ return FS[f].cap>=24; }
static float brand_wf(int f){
    if(!brand_disc(f)) return text_wf(PRODUCT,f);
    return text_wf(BRAND_WORD,f)+FS[f].cap*0.14f+FS[f].cap*0.60f;
}
static float brand_at(float x,int y,int f,uint32_t cw,int a){
    float cap=FS[f].cap, trail=cap*FS[f].gap+FS[f].track;
    x=text_at(x,y,BRAND_WORD,f,cw,a);
    if(!brand_disc(f)) return text_at(x,y,BRAND_BADGE,f,C_ACC,a);
    float r=cap*0.30f, bx=x-trail+cap*0.14f+r, by=y+r+cap*0.02f, l=r*0.50f;
    disc(bx,by,r,C_ACC,a);
    capsule(bx-l,by,bx+l,by,cap*0.052f,C_ACCD,a); capsule(bx,by-l,bx,by+l,cap*0.052f,C_ACCD,a);
    return bx+r+trail;
}
static void brand_c(int cx,int y,int f,uint32_t cw,int a){ brand_at(cx-brand_wf(f)/2,y,f,cw,a); }
static void draw_pill_btn(Rect r,const char*txt,int font,uint32_t bg_t,uint32_t bg_b,uint32_t fg,int press){
    int w=r.x1-r.x0,h=r.y1-r.y0;
    rrect_g(r.x0,r.y0+2,w,h,h/2.f,rgb(0,0,0),rgb(0,0,0),70);              /* soft shadow */
    rrect_g(r.x0,r.y0,w,h,h/2.f,bg_t,bg_b,255);
    if(press) rrect(r.x0,r.y0,w,h,h/2.f,rgb(255,255,255),36);
    text_c((r.x0+r.x1)/2,r.y0+(h-(int)FS[font].cap)/2,txt,font,fg,255);
}
static Rect toast_rect(ms_t now){ Rect z={0,0,0,0}; if(!toast.dur||now-toast.t0>=toast.dur) return z; return R(56,90,368,50); }
static Rect rip_rect(void){ Rect z={0,0,0,0}; if(!rip.on) return z; return R((int)rip.x-52,(int)rip.y-52,104,104); }
static void overlays_damage(ms_t now){
    Rect c0=toast_rect(now), c1=rip_rect();
    if(!r_empty(ov_prev[0])) rl_add(&dmg,ov_prev[0]); if(!r_empty(c0)) rl_add(&dmg,c0); ov_prev[0]=c0;
    if(!r_empty(ov_prev[1])) rl_add(&dmg,ov_prev[1]); if(!r_empty(c1)) rl_add(&dmg,c1); ov_prev[1]=c1;
}
static void overlays_draw(ms_t now){
    cv=canvas;
    Rect c1=rip_rect();
    if(!r_empty(c1)){
        set_clip(c1); float t=(now-rip.t0)/360.f; float e=ease_out(clampf(t,0,1));
        ring(rip.x,rip.y,6+e*30,10+e*38,rgb(255,255,255),(int)(54*(1-t)));
        rl_add(&pres,c1);
    }
    Rect c0=toast_rect(now);
    if(!r_empty(c0)){
        set_clip(c0); float t=(float)(now-toast.t0); float a=clampf(t/160.f,0,1)*clampf((toast.dur-t)/260.f,0,1);
        int w=text_w(toast.msg,F_XS)+44; if(w>356)w=356; int x=CX-w/2, y=98-(int)((1-ease_out(clampf(t/200.f,0,1)))*8);
        rrect_g(x,y+3,w,34,17,rgb(0,0,0),rgb(0,0,0),(int)(90*a));
        rrect_g(x,y,w,34,17,rgb(58,63,76),rgb(42,46,57),(int)(250*a));
        disc(x+17,y+17,4,toast.col,(int)(255*a));
        text_at(x+29,y+12,toast.msg,F_XS,C_TXT,(int)(255*a));
        rl_add(&pres,c0);
    }
    set_clip(FULL);
}

/* ================= page: HOME ================= */
enum { W_CHIP=1, W_LABEL, W_HERO, W_SUB, W_CHART, W_STATS, W_ACT, W_FOOT, W_LIST, W_SCROLL, W_DHERO, W_DCHG, W_MINUS, W_PLUS, W_CANCEL, W_SAVE, W_DIAG, W_SPL, W_XNOTE, W_XFOOT, W_BYE, W_GEAR, W_BACKCH, W_OFF, W_CLOCK, W_LIVE, W_START };   /* v6.16: W_CLOCK = HOME's top line (clock / wordmark); v6.17: W_LIVE / W_START (a page's live line, HOLD TO START) */
/* v6.9: latest chart scale (state JSON "chart" + selftest) */
static struct { int mode,active,full,top,raw_max,disp_max,ticks,bticks,n,clip,na,wmax,wtop; } cst;   /* v6.15: + wmax/wtop (weight line, mode 0) */
static int cmp_int(const void*a,const void*b){ int x=*(const int*)a,y=*(const int*)b; return (x>y)-(x<y); }
/* v6.10: the chart, when it shows pressure (latest drawing: active, mode 0), and the BAR field during brew are tap targets for the basis toast */
static int pinfo_hit(int x,int y){
    if(cst.active&&cst.mode==0&&in_r(x,y,118,240,244,54)) return 1;
    return (hm.eff==PH_PRE||hm.eff==PH_EXT)&&in_r(x,y,CX-STAT_PITCH/2,298,STAT_PITCH,40);
}
static int diag_hint_shown(void){ return diag_hint(act.ph)&&!cst.active&&hm.hint1[0]; }
static void draw_chart(Rect r,ms_t now,const HomeM*m){
    int x0=r.x0+4, w=r.x1-r.x0-8, yb=r.y1-5, h=r.y1-r.y0-20, raw=cd.raw;
    static int c1[260], c2[260], cr[260], ci[260], vs[GN], rs[GN];
    /* series choice per phase: brew/water = pressure (area) + weight (line); grind = weight; steam = milk temp */
    int mode=(m->eff==PH_GRIND)?1:(m->eff==PH_STEAM?2:0);
    for(int i=0;i<w;i++){ c1[i]=-1; c2[i]=-100000; cr[i]=-1; ci[i]=-1; }
    ms_t t0=now-40000; int m1=0,m2=0,mr=0,nv=0,nact=0;
    for(int i=0;i<g_n;i++){
        int idx=(g_head+GN-g_n+i)%GN; ms_t t=g_t[idx]; if(t<t0||t>now) continue;
        int c=(int)((t-t0)*(w-1)/40000); if(c<0||c>=w) continue;
        int r1= mode==1? (g_w[idx]<0?0:g_w[idx]) : (mode==2? (g_m[idx]>200?g_m[idx]-200:0) : g_p[idx]);          /* raw */
        /* v6.9: the pressure curve (mode 0) is drawn only for samples during brew/pre-inf./water (g_a). Outside the activity, word 51 is
         * maintenance/pump pulses (BREW.md §5-7, STEAMLIVE.md §5.3) — they are shown as small ticks on the baseline. */
        int ia= mode!=0||g_a[idx];
        int v1= raw? r1 : (!ia? 0 : (int)lroundf(mode==1? fmaxf(wdb(g_wd[idx]),0) : (mode==2? fmaxf(g_md[idx]-200,0) : g_pd[idx])));   /* what is drawn (weight: deadband) */
        if(ia){ if(r1>cr[c]) cr[c]=r1; } else if(r1>ci[c]) ci[c]=r1;
        if(r1>mr) mr=r1; if(mode==0&&g_a[idx]) nact++;
        if(v1>c1[c]) c1[c]=v1; if(v1>m1) m1=v1;
        vs[nv]=v1; rs[nv]=raw||ia?r1:0; nv++;
        if(mode==0){ int w2=raw?g_w[idx]:(int)lroundf(wdb(g_wd[idx])); if(w2>c2[c]) c2[c]=w2; if(w2>m2) m2=w2; }
    }
    int active=phase_active(m->ph)||m->ph==PH_DONE||(mode==0&&(raw?mr>=40:nact>0));   /* v6.9: the idle chart is "active" when the window contains a brew/water */
    cst.mode=mode; cst.active=active; cst.raw_max=mr; cst.disp_max=m1; cst.n=nv; cst.ticks=0; cst.bticks=0; cst.clip=0; cst.na=0;
    cst.wmax=mode==0?m2:0; cst.wtop=mode==0?(int)((m2<100?100:m2)*1.08f):0;
    if(!active){
        for(int x=x0+2;x<x0+w;x+=8) hspan(yb,x,x+3,C_SURF3,255);
        if(!raw) for(int i=0;i<w;i++) if(mode==0&&ci[i]>=40){ cst.bticks++; for(int yy=yb-4;yy<yb;yy++) pset(x0+i,yy,C_DIS,200); }   /* pulses at rest: small ticks on the baseline */
        if(m->hint1[0]&&m->hint2){ text_c(CX,r.y0+8,m->hint1,F_XS,m->hc1,255); text_c(CX,r.y0+26,m->hint2,F_XS,C_TXT3,255); }   /* v6.11: hint instead of "NO ACTIVITY" */
        else if(m->hint1[0]||m->hint2) text_c(CX,r.y0+17,m->hint1[0]?m->hint1:m->hint2,F_XS,m->hint1[0]?m->hc1:C_TXT3,255);   /* v6.17: one line, centred */
        cst.full=cst.top=0;
        return;
    }
    hspan(yb,x0,x0+w,C_LINE,255);
    int stgt=steam_target(), tline=(mode==1)||(mode==2&&m->sk!=SK_WAND);   /* wand/flush has no target: no target line */
    int floor1= mode==0? 400 : (mode==1? sv(S_GRINDW) : stgt-200);
    int full1=floor1; float top;
    if(raw||cd.scale==CS_MAX){ if(m1>full1) full1=m1; if(full1<1) full1=1; top=full1*1.08f; }   /* v6.8 */
    else {
        int lv=0;
        if(cd.scale==CS_PCT){ int na=0; for(int i=0;i<nv;i++) if(rs[i]>=(mode==0?40:1)) vs[na++]=rs[i];   /* p90 of the ACTIVE raw samples */
                              if(na>0){ qsort(vs,(size_t)na,sizeof vs[0],cmp_int); lv=vs[(na-1)*cd.pct/100]; } cst.na=na; }
        else { int k=cd.hold<1?1:cd.hold; for(int i=k-1;i<nv;i++){ int mn=rs[i]; for(int j=1;j<k;j++) if(rs[i-j]<mn) mn=rs[i-j]; if(mn>lv) lv=mn; } }   /* highest level HELD for k samples */
        if(full1<1) full1=1;
        top=fmaxf(full1*1.08f,lv*cd.head); if(lv>full1) full1=lv;
    }
    cst.full=full1; cst.top=(int)top;
    float s1=h/top, s2=h/((m2<100?100:m2)*1.08f);
    uint32_t pc=m->hi;
    if(tline){  /* target line (auto-stop level) */
        int ty=yb-(int)((mode==1?sv(S_GRINDW):stgt-200)*s1);
        for(int x=x0;x<x0+w;x+=7) hspan(ty,x,x+3,C_TXT2,200);
    }
    int last=-1; float ly=0, ytop=(float)(yb-h);
    for(int i=0;i<w;i++){
        if(c1[i]<0) continue;
        float y=yb-c1[i]*s1; if(y<ytop){ y=ytop; cst.clip++; }   /* v6.9: above the top -> clipped at the ceiling (the v6.8 scale never gets up here) */
        for(int yy=(int)y;yy<yb;yy++) pset(x0+i,yy,pc,22+(yy-(int)y<40?40-(yy-(int)y):0));
        if(last>=0&&i-last<=3) capsule(x0+last+0.5f,ly,x0+i+0.5f,y,0.9f,pc,255);
        last=i; ly=y;
    }
    if(!raw) for(int i=0;i<w;i++){
        if(cr[i]>top){ cst.ticks++; for(int yy=yb-h;yy<yb-h+3;yy++) pset(x0+i,yy,pc,170); }               /* raw sample above the top: small tick just below the ceiling (not in the headings) */
        if(ci[i]>=40){ cst.bticks++; for(int yy=yb-4;yy<yb;yy++) pset(x0+i,yy,C_DIS,200); } }           /* pulses outside the activity: the baseline */
    last=-1;
    if(mode==0) for(int i=0;i<w;i++){
        if(c2[i]<=-100000) continue; int v=c2[i]<0?0:c2[i]; float y=yb-v*s2;
        if(last>=0&&i-last<=3) capsule(x0+last+0.5f,ly,x0+i+0.5f,y,0.7f,C_TXT,170);
        last=i; ly=y;
    }
    { const char*cap=TR(mode==0?TX_C_PRESS:(mode==1?TX_C_WEIGHT:TX_C_MILK)); text_at(r.x0+4,r.y0+1,cap,F_XS,pc,235); }
    if(raw) text_r(r.x1-4,r.y0+1,TR(TX_C_RAW),F_XS,C_WARN,235);                  /* v6.9: the raw view is marked (instead of the right-hand heading) */
    else if(mode==0||tline) text_r(r.x1-4,r.y0+1,TR(mode==0?TX_C_WEIGHT:TX_C_TARGET),F_XS,C_TXT2,235);
}
/* v6.11: the sub-line at idle (TARGET 87.0°, NO CONNECTION …, errors in plain language) is the screen's most important message -> F_S when it
 * fits in the field; during activities (AUTO-STOP AT …) and for too-long text F_XS as before */
/* v6.17: the activity sub-lines are short now ("STOPS AT 36.0 G"), so they get F_S too — readable from where you stand while brewing */
static int sub_font(const HomeM*m){ return text_wf(m->sub,F_S)<=SUB_W-8?F_S:F_XS; }
/* v6.11: gear (settings) drawn with primitives — no new glyph in the font */
static void draw_gear(float cx,float cy,float r,uint32_t c,uint32_t hole){
    for(int k=0;k<8;k++){ float a=k*(float)M_PI/4.f, ca=cosf(a), sa=sinf(a); capsule(cx+ca*r*0.55f,cy+sa*r*0.55f,cx+ca*r*0.92f,cy+sa*r*0.92f,r*0.16f,c,255); }
    disc(cx,cy,r*0.70f,c,255); disc(cx,cy,r*0.30f,hole,255);
}
static void draw_home(ms_t now){
    const HomeM*m=&hm; uint32_t h; char b[64];
    { char c[24]; int ok=home_clock_text(c,sizeof c);                  /* v6.16: the clock (stock update_time format) instead of the wordmark */
      if(W_begin(W_CLOCK,R(CX-70,HOME_BRAND_Y-6,140,24),hs(hi(H0,ok),ok?c:""))){
          if(ok) text_c(CX,HOME_BRAND_Y-2,c,F_S,C_TXT2,255); else brand_c(CX,HOME_BRAND_Y,F_XS,C_TXT3,255);
          W_end(); } }
    /* phase chip (+ CRC warning) */
    int crc=now-last_bad_ms<10000&&rx_bad>0;
    h=hi(hs(H0,m->chip),crc?(int)rx_bad:0);
    if(W_begin(W_CHIP,R(110,74,260,32),h)){
        const char*nm=m->chip; int tw=text_w(nm,F_S); int w=tw+40; int x=CX-w/2; if(crc) x-=40;
        rrect(x,76,w,28,14,mix(C_SURF,m->hi,38),255);
        disc(x+15,90,4.2f,m->hi,255); text_at(x+26,83,nm,F_S,m->hi,255);
        if(crc){ snprintf(b,sizeof b,TR(TX_H_CRC),rx_bad); int cw2=text_w(b,F_XS)+20; rrect(x+w+8,78,cw2,24,12,mix(C_SURF,C_WARN,44),255); text_at(x+w+18,85,b,F_XS,C_WARN,255); }
        W_end();
    }
    /* v6.6: during concurrent steam the steam badge (STEAM·WAND / STEAM·AUTO) sits on the label line — the main activity's
     * target stays in the sub-line ("AUTO-STOP AT … G"), so both activities are named on screen */
    if(W_begin(W_LABEL,R(110,110,260,18),hi(hs(hs(H0,m->label),m->badge?m->badge:""),m->conc))){
        if(m->badge){
            uint32_t sc=ph_hi(PH_STEAM); int bw=text_w(m->badge,F_XS)+30, bx=CX-bw/2;
            rrect(bx,110,bw,18,9,mix(C_SURF,sc,44),255); disc(bx+11,119,3.2f,sc,255); text_at(bx+20,114,m->badge,F_XS,sc,255);
        } else text_c(CX,113,m->label,F_XS,C_TXT3,255);
        W_end();
    }
    h=hi(hs(hs(H0,m->hero),m->unit),m->ph==PH_OFFLINE||m->ph==PH_SLEEP);
    if(W_begin(W_HERO,R(92,130,296,80),h)){
        float nw=text_wf(m->hero,F_XXL), uw=text_wf(m->unit,F_M); float x=CX-(nw+10+uw)/2;
        uint32_t c=(m->ph==PH_OFFLINE||m->ph==PH_SLEEP)?C_DIS:C_TXT;
        text_at(x,136,m->hero,F_XXL,c,255); text_at(x+nw+10,136+66-18,m->unit,F_M,C_TXT2,255);
        W_end();
    }
    if(W_begin(W_SUB,R(SUB_X0,212,SUB_W,24),hi(hs(H0,m->sub),m->ph))){
        int sf=sub_font(m); text_c(CX,sf==F_S?215:217,m->sub,sf,(m->ph==PH_ERROR)?C_ERR:(phase_active(m->ph)||m->ph==PH_DONE?m->hi:C_TXT2),255); W_end();
    }
    if(W_begin(W_CHART,R(CX-HINT_W/2,240,HINT_W,54),hs(hs(hi(hi(hi(hi(hi(H0,g_head),g_n),(int)m->hi),m->sk*8+(m->eff==PH_STEAM)),cd.raw*16+cd.scale),m->hint1),m->hint2?m->hint2:""))){ draw_chart(R(118,240,244,54),now,m); W_end(); }
    h=H0; for(int i=0;i<3;i++){ h=hs(h,m->sl[i]); h=hs(h,m->svv[i]); }
    if(W_begin(W_STATS,R(96,298,288,40),h)){
        for(int i=0;i<3;i++){ int x=CX+(i-1)*STAT_PITCH; text_c(x,300,m->sl[i],F_XS,C_TXT3,255); text_c(x,317,m->svv[i],F_M,C_TXT,255); }
        W_end();
    }
    /* action row */
    int running=phase_active(m->ph); int akind=m->ph==PH_GRIND?0:(m->ph==PH_WATER?2:(m->ph==PH_STEAM?3:1));
    int st_on=stm.kind!=SK_NONE, sk[2], sn=stop_btns(now,sk);   /* v6.6: steam STOP only for latched steam; v6.13: + STOP for grind/brew/water */
    float hp=0; int hk=-1;
    if(tch.down&&!tch.moved&&tch.target>=T_ACT0&&tch.target<=T_ACT3){ hk=tch.target-T_ACT0; hp=clampf((now-tch.t0)/(float)HOLD_MS,0,1); }
    h=hi(hi(hi(hi(H0,m->ph),hk),(int)(hp*40)),act.pending); h=hi(h,pressed(T_STOP)); h=hi(h,pressed(T_STOPS)); h=hi(h,sn*25+(sk[0]+1)*5+sk[1]+1); h=hi(h,stm.kind); if(running) h=hi(h,(int)((now/66)&0x7FFFFFFF)); h=hi(h,sv(S_GRINDMODE)*4+sv(S_HOTMODE)); for(int i=0;i<NSET;i++) h=hi(h,set_oor(&SET[i]));
    for(int i=0;i<4;i++) h=hs(h,m->alab[i]);
    if(W_begin(W_ACT,R(72,338,336,82),h)){                        /* v6.17: 336 x 82 (STOP 216 / 2 x 152, + the icons' targets); v6.13: 296 x 64 */
        if(sn){
            for(int i=0;i<sn;i++) draw_pill_btn(stop_pill(sn,i),TR(STOP_BTN[sk[i]]),stop_font(sn),C_STOP_T,C_STOP_B,C_STOP_FG,pressed(i?T_STOPS:T_STOP));
        } else {
            for(int i=0;i<4;i++){
                int en=!running&&!act_guard(i,now); uint32_t ac=ph_hi(ACT_PH[i]); int pr=(hk==i);
                if(running&&(i==akind||(i==ACT_STEAM&&st_on))){   /* active activity (v6.6: also steam on top of another): filled button + pulsing ring */
                    uint32_t on_c=(i==ACT_STEAM)?ph_hi(PH_STEAM):m->hi;
                    float pu=0.5f+0.5f*sinf((float)(now&0xFFFFFF)/260.f);   /* v6.6: the phase is taken from the lower 24 bits — a float can't hold a clock of several days in ms */
                    ring(ACT_X[i],ACT_Y,27.5f+pu*2,29.5f+pu*2,on_c,(int)(150-90*pu));
                    disc(ACT_X[i],ACT_Y,25,on_c,255);
                    float iw2=text_wf(ACT_ICON[i],F_IC); text_at(ACT_X[i]-iw2/2,ACT_Y-10,ACT_ICON[i],F_IC,rgb(16,18,22),255);
                    continue;
                }
                disc(ACT_X[i],ACT_Y+2,25,rgb(0,0,0),70);
                disc(ACT_X[i],ACT_Y,25,pr?C_SURF3:C_SURF2,255);
                ring(ACT_X[i],ACT_Y,24,25.2f,en?mix(C_SURF3,ac,90):C_SURF3,255);
                float iw=text_wf(ACT_ICON[i],F_IC);
                text_at(ACT_X[i]-iw/2,ACT_Y-10,ACT_ICON[i],F_IC,en?ac:C_DIS,255);
                if(pr&&en&&hp>0) arc_aa(ACT_X[i],ACT_Y,28.5f,1.8f,-90,-90+360*hp,ac,255);
                if(!running) text_c(ACT_X[i],ACT_LAB_Y,m->alab[i],F_XS,en?C_TXT2:C_DIS,255);   /* v6.17: the icon's target */
            }
        }
        W_end();
    }
    /* v6.11: visible path to the settings; v6.17: the gear opens SYSTEM (each feature's settings are behind its own icon), no page dots */
    if(W_begin(W_GEAR,R(CX-22,GEAR_Y-20,44,40),hi(H0,pressed(T_GEAR)))){
        int pr=pressed(T_GEAR); uint32_t bg=pr?C_SURF3:C_SURF2;
        disc(CX,GEAR_Y+1,15,rgb(0,0,0),60); disc(CX,GEAR_Y,15,bg,255); ring(CX,GEAR_Y,14,15.2f,C_LINE,255);
        draw_gear(CX,GEAR_Y,9,pr?C_TXT:C_TXT2,bg); W_end();
    }
}

/* ================= page: SETTINGS LIST ================= */
/* HOME: discreet title line at the top inside the gauge ring — the wordmark w/ "+" badge */
static void static_home(void){ }   /* v6.16: the wordmark is drawn by W_CLOCK (draw_home) — it gives way to the clock */
/* v6.17: a page's title. Feature pages: the feature's icon in its colour + its name (the same icon as on HOME, so the page is
 * recognisably "the thing behind that button"); SYSTEM / DIAGNOSTICS: the name. No descriptive sub-line any more. */
static const char* page_title(int pg){ return pg==PG_DIAG?TR(TX_PG_DIAG):TR(TIER_NAME[clampi(tier_of_page(pg),0,4)]); }
static float page_title_w(int pg){ return text_wf(page_title(pg),F_M)+(pg_feat(pg)?text_wf(ACT_ICON[pg-PG_GRIND],F_IC)+8:0); }
static void static_page_title(void){
    const char*t=page_title(page); float x=CX-page_title_w(page)/2;
    if(pg_feat(page)){ int k=page-PG_GRIND; float iw=text_wf(ACT_ICON[k],F_IC); text_at(x,PG_TITLE_Y-1,ACT_ICON[k],F_IC,ph_hi(ACT_PH[k]),255); x+=iw+8; }
    text_at(x,PG_TITLE_Y,t,F_M,C_TXT,255);
}
static void static_list(void){ static_page_title(); }
/* soft edges: fade to background at top/bottom of a scroll field */
static void edge_fade(Rect vr,int top,int bottom){
    for(int j=0;j<14;j++){
        int a=255-j*255/14;
        for(int e=0;e<2;e++){
            if(e?!bottom:!top) continue;
            int y=e?vr.y1-1-j:vr.y0+j;
            int xa=vr.x0<cx0[y]?cx0[y]:vr.x0, xb=vr.x1>cx1[y]+1?cx1[y]+1:vr.x1;
            for(int x=xa;x<xb;x++) canvas[y*W+x]=mix(canvas[y*W+x],slay[y*W+x],a);
        }
    }
}
/* scroll indicator: short arc at the right edge */
static void draw_scroll_ind(float content_h){
    float mx=scroll_max(page); int sc=(int)lroundf(scroll[page]);
    if(W_begin(W_SCROLL,R(444,140,30,200),hi(hi(hi(H0,sc),(int)mx),page))){
        if(mx>0){
            arc_aa(CX,CY,228,1.4f,-22,22,C_SURF3,255);
            float vis=(list_vy1(page)-LIST_VY0)/content_h, span=44*vis, a0=-22+(44-span)*(scroll[page]/mx);
            arc_aa(CX,CY,228,1.6f,a0,a0+span,C_TXT2,255);
        }
        W_end();
    }
}
/* v6.2: texts for the system-action rows at the bottom of SYSTEM (shared w/ the selftest's layout check) */
/* short = the list row's sub-line (<= 300 px); long = the detail view's note w/ the actual path */
static const char* boot_lock_text(int l,int lng){
    if(l==BL_NOMOUNT) return TR(lng?TX_BL_NOMOUNT_L:TX_BL_NOMOUNT);
    if(l==BL_NOBIN)   return TR(lng?TX_BL_NOBIN_L:TX_BL_NOBIN);
    return TR(lng?TX_BL_VENDOR_L:TX_BL_VENDOR);
}
/* BOOT.md §9.1: "exists -> our UI (B) at boot; does not exist -> original (A) at boot" — but never
 * "our UI" if slot B is not mounted (then A starts) or does not contain merakiplus. From v6.3
 * the product name is shown instead of "our UI". */
static const char* boot_state_text(void){
    int k=boot_next_kind();
    return TR(k==BN_OURS?TX_BS_OURS:(k==BN_ORIGINAL?TX_BS_ORIG:TX_BS_SLOTB));
}
static void xrow_text(int x,const char**label,const char**sub,const char**val,int*lock){
    if(x==X_BOOTUI){
        *label=TR(TX_X_BOOTUI); *lock=bs.lock!=BL_OK; *val=boot_next_name();
        *sub=*lock?boot_lock_text(bs.lock,0):TR(TX_BH1);
    } else {
        *label=TR(TX_X_SWITCH); *lock=switch_locked(); *val=NULL;
        *sub=TR(*lock?TX_SW_LOCK:TX_X_SWITCH_SUB);
    }
}
/* list row: font for an enum value — the largest that fits to the right of the label */
static int row_val_font(const char*label,const char*val,int xr,int*room){
    *room=xr-(94+text_w(label,F_S)+12);
    if(text_w(val,F_M)<=*room) return F_M;
    return text_w(val,F_S)<=*room?F_S:F_XS;
}
/* v6.17: the close chevron of a PAGE (feature / SYSTEM / DIAGNOSTICS): the same glyph as the editors' (v6.11), at the very top;
 * the tap area is the whole header (PBACK_*). */
static void draw_pback(void){
    int pr=pressed(T_BACK);
    if(W_begin(W_BACKCH,R(CX-26,PG_CHEV_Y-8,52,24),hi(hi(H0,pr),page))){
        if(pr) rrect(CX-24,PG_CHEV_Y-7,48,22,11,C_SURF3,255);
        uint32_t c=pr?C_TXT:C_TXT2;
        capsule(CX-9,PG_CHEV_Y,CX,PG_CHEV_Y+8,1.5f,c,255); capsule(CX,PG_CHEV_Y+8,CX+9,PG_CHEV_Y,1.5f,c,255);
        W_end();
    }
}
/* v6.17: the line under a page's title (shared w/ the selftest). Feature pages: WHY start is not possible (the guard's text, amber) —
 * otherwise the live values that matter before starting: what is on the scale, how hot the boiler / group / milk is.
 * (TX_G_WAIT is transient — a write is on its way — and is not shown: the line would flicker after every SAVE.) SYSTEM: product + version. */
#define LIVE_W 296
static uint32_t page_live_text(int pg,char*o,size_t n,ms_t now){
    char a[16],b[16]; o[0]=0;
    if(pg==PG_SYS){ snprintf(o,n,"%s",TR(ro_mode?TX_DV_VERSION_RO:TX_DV_VERSION)); return C_TXT3; }
    if(!pg_feat(pg)) return C_TXT3;
    int k=pg-PG_GRIND, up=link_up(now); const char*g=act_guard(k,now);
    if(g&&g!=TR(TX_G_WAIT)){ snprintf(o,n,"%s",g); return C_WARN; }
#define LV_T(dst,r_,dec_) do{ if(up&&reg_seen[r_]) fmt_t10(dst,sizeof dst,reg[r_],dec_); else snprintf(dst,sizeof dst,"--"); }while(0)
    if(k==ACT_GRIND){ if(up&&reg_seen[38]){ int w=wdbi(s16(38)); if(w<-999)w=-999; if(w>9999)w=9999; fmt_fix(a,sizeof a,w,1); } else snprintf(a,sizeof a,"--"); snprintf(o,n,TR(TX_LV_GRIND),a); }
    else if(k==ACT_BREW){ LV_T(a,40,1); LV_T(b,42,0); snprintf(o,n,TR(TX_LV_BREW),a,b); }
    else if(k==ACT_WATER){ LV_T(a,40,1); snprintf(o,n,TR(TX_LV_WATER),a); }       /* word 40 = the stock UI's "water sensor" (SET[S_HOTT]) */
    else { LV_T(a,47,0); LV_T(b,48,1); snprintf(o,n,TR(TX_LV_STEAM),a,b); }
#undef LV_T
    return C_TXT2;
}
/* v6.17: a section header's summary on SYSTEM (shared w/ the selftest): what you would otherwise have to open the section to see */
static void sec_summary(int sc,char*o,size_t n){
    o[0]=0;
    if(sc==SEC_DISPLAY) snprintf(o,n,"%s",TR(TX_UL_EN+ui_lang));
    else if(sc==SEC_CLOCK){ if(!home_clock_text(o,n)) snprintf(o,n,"%s",TR(TX_DV_T_NOTSET)); }
    else if(sc==SEC_SCHED){ if(sch.on) fmt_hm(o,n,sch.hm/60,sch.hm%60,1); else snprintf(o,n,"%s",TR(TX_E_OFF)); }
}
/* the DIAGNOSTICS entry's status: OK / the error code / DOWN */
static uint32_t diag_row_text(char*o,size_t n,ms_t now){
    if(!link_up(now)){ snprintf(o,n,"%s",TR(TX_DV_DOWN)); return C_ERR; }
    if(reg[34]|reg[35]|reg[36]){ snprintf(o,n,"%s",err_code()); return C_ERR; }
    snprintf(o,n,"%s",TR(TX_DV_OK)); return C_OK;
}
static void draw_start(ms_t now){
    int k=page-PG_GRIND, en=!act_guard(k,now), pr=pressed(T_START); uint32_t ac=ph_hi(ACT_PH[k]);
    float hp=(pr&&en)?clampf((now-tch.t0)/(float)HOLD_MS,0,1):0;
    Rect wr=R(CX-START_HIT_W/2,START_HIT_Y,START_HIT_W,START_HIT_H);
    if(W_begin(W_START,wr,hi(hi(hi(hi(hi(H0,k),en),pr),(int)(hp*48)),ui_lang))){
        int x=CX-START_W/2, y=START_Y; float rr=START_H/2.f; const char*t=TR(TX_B_START);
        rrect(x,y+2,START_W,START_H,rr,rgb(0,0,0),70);
        rrect(x,y,START_W,START_H,rr,en?mix(C_SURF3,ac,150):C_SURF3,255);                 /* outline in the feature's colour */
        rrect_g(x+2,y+2,START_W-4,START_H-4,rr-2,pr?C_SURF3:C_SURF2,pr?C_SURF2:C_SURF,255);
        if(hp>0){ set_clip(R(x,y,(int)(START_W*hp),START_H)); rrect(x+2,y+2,START_W-4,START_H-4,rr-2,mix(C_SURF2,ac,120),255); set_clip(wr); }   /* the hold fills the pill left -> right */
        float iw=text_wf(ACT_ICON[k],F_IC), tw=text_wf(t,F_S), tx=CX-(iw+10+tw)/2;
        text_at(tx,y+START_H/2-10,ACT_ICON[k],F_IC,en?ac:C_DIS,255);
        text_at(tx+iw+10,y+START_H/2-7,t,F_S,en?C_TXT:C_DIS,255);
        W_end();
    }
}
static void draw_list(ms_t now){
    Row rw[ROW_MAX]; int n=rows_get(page,rw), vy1=list_vy1(page); int sc=(int)lroundf(scroll[page]); int prow=-1;
    draw_pback();
    { char lv[96]; uint32_t lc=page_live_text(page,lv,sizeof lv-4,now); fit_text(lv,F_XS,LIVE_W);
      if(W_begin(W_LIVE,R(CX-LIVE_W/2-2,PG_LIVE_Y-3,LIVE_W+4,17),hi(hi(hs(H0,lv),(int)lc),page))){ text_c(CX,PG_LIVE_Y,lv,F_XS,lc,255); W_end(); } }
    if(tch.down&&!tch.moved&&tch.target>=T_ROW0) prow=tch.target-T_ROW0;
    char dgs[16]; uint32_t dgc=diag_row_text(dgs,sizeof dgs,now);
    uint32_t h=hi(hi(hi(hi(H0,sc),prow),page),sec_open*8+ui_lang);
    for(int k=0;k<n;k++){ h=hi(h,rw[k].kind*64+rw[k].a); if(rw[k].kind==RK_SET){ const Setting*q=&SET[rw[k].a]; h=hi(hi(h,q->val),q->loaded); } }
    h=hi(h,sv(S_TUNIT));
    if(page==PG_SYS){ char sm[40]; h=hi(hi(hi(h,bs.flag*8+bs.lock),switch_locked()),switch_mode()); for(int q=0;q<NSEC;q++){ sec_summary(q,sm,sizeof sm); h=hs(h,sm); } h=hi(hs(h,dgs),(int)dgc); }
    Rect vr=R(60,LIST_VY0,360,vy1-LIST_VY0);
    if(W_begin(W_LIST,vr,h)){
        char b[40]; int c0=-1,c1=-1;
        for(int k=0;k<n;k++){
            int y=row_y0(page,k)-sc;
            if(page==PG_SYS&&rw[k].kind!=RK_SEC&&rw[k].kind!=RK_DIAG){ if(c0<0) c0=y; c1=y+ROW_H; }   /* the open section's rows */
            if(y+ROW_H<LIST_VY0||y>vy1) continue;
            int pr=(k==prow);
            if(rw[k].kind==RK_SET){
                int i=rw[k].a; const Setting*s=&SET[i]; int lock=(s->flags&SF_LOCK)!=0;
                int oor=set_oor(s); const char*lab=TR(s->label);
                rrect_g(72,y,336,ROW_H,16,pr?C_SURF3:C_SURF2,pr?C_SURF2:C_SURF,255);
                if(oor) rrect(72,y,336,ROW_H,16,C_ERR,34);                                        /* out of spec: red tint */
                text_at(94,y+18,lab,F_S,lock?C_TXT2:C_TXT,255);
                set_fmt(i,s->val,b,sizeof b-4); const char*u=set_unit(i);
                int xr=lock?388:372;
                if(*u&&s->kind==K_NUM){ int uw=text_w(u,F_XS); text_r(xr,y+21,u,F_XS,C_TXT3,255); xr-=uw+6; }
                int vf=F_M, room=0;
                if(s->kind==K_ENUM||(s->flags&SF_HHMM)||i==S_TZ){ vf=row_val_font(lab,b,xr,&room); if(vf==F_XS) fit_text(b,F_XS,room); }   /* v6.16: + clock times, zone label */   /* long enum names: M -> S -> XS -> … */
                text_r(xr,y+(vf==F_M?16:(vf==F_S?18:20)),b,vf,oor?C_ERR:(lock?C_TXT3:C_ACC),255); xr-=text_w(b,vf);
                if(s->hint&&!lock&&!oor) text_r(xr-7,y+21,"≈",F_XS,C_WARN,255);                   /* estimate marker */
                if(!lock) text_at(380,y+19,">",F_XS,C_TXT3,255);
            } else if(rw[k].kind==RK_SEC){   /* v6.17: section header — name, summary, chevron (right = closed, down = open) */
                int open=(rw[k].a==sec_open); char sm[40]; sec_summary(rw[k].a,sm,sizeof sm-4);
                rrect_g(72,y,336,ROW_H,16,pr?C_SURF3:(open?C_SURF3:C_SURF2),pr?C_SURF2:(open?C_SURF2:C_SURF),255);
                text_at(94,y+18,TR(SEC_NAME[rw[k].a]),F_S,open?C_ACC:C_TXT,255);
                if(sm[0]&&!open){ fit_text(sm,F_XS,372-(94+text_w(TR(SEC_NAME[rw[k].a]),F_S)+14)); text_r(366,y+20,sm,F_XS,C_TXT3,255); }
                if(open){ capsule(378,y+22,384,y+28,1.3f,C_ACC,255); capsule(384,y+28,390,y+22,1.3f,C_ACC,255); }
                else text_at(380,y+19,">",F_XS,C_TXT3,255);
            } else if(rw[k].kind==RK_X){     /* v6.2: system actions — two-line rows (label + explanation) */
                const char*label,*sub,*val; int lock; xrow_text(rw[k].a,&label,&sub,&val,&lock);
                rrect_g(72,y,336,ROW_H,16,pr?C_SURF3:C_SURF2,pr?C_SURF2:C_SURF,255);
                text_at(94,y+10,label,F_S,lock?C_TXT2:C_TXT,255);
                text_at(94,y+31,sub,F_XS,lock?C_WARN:C_TXT3,255);
                if(val) text_r(lock?388:372,y+8,val,F_M,lock?C_TXT3:C_ACC,255);
                if(!lock) text_at(380,y+(val?12:19),">",F_XS,C_TXT3,255);
            } else {                          /* v6.17: the DIAGNOSTICS entry (advanced) w. the machine's status */
                rrect_g(72,y,336,ROW_H,16,pr?C_SURF3:C_SURF2,pr?C_SURF2:C_SURF,255);
                text_at(94,y+18,TR(TX_PG_DIAG),F_S,C_TXT,255);
                text_r(366,y+18,dgs,F_S,dgc,255);
                text_at(380,y+19,">",F_XS,C_TXT3,255);
            }
        }
        if(c0>=0){ int ya=c0<LIST_VY0?LIST_VY0:c0, yb=c1>vy1?vy1:c1; if(yb>ya) fill_rect(64,ya,2,yb-ya,C_ACC,150); }   /* the open section's rows are tied together by a thin accent rail */
        edge_fade(vr,1,1);
        W_end();
    }
    draw_scroll_ind(n*ROW_P);
    if(pg_feat(page)) draw_start(now);
}

/* ================= page: STEPPER DETAIL ================= */
/* texts for the detail view (shared w/ the selftest's layout check) */
/* v6.17: the two footer lines ("CONTROLLER NOW + CONFIG" / "STOCK UI: AFTER RESTART", file paths, "SHOWN FROM CONFIG.JSON" …) are gone:
 * they described the implementation on every single editor. What matters is said once — by the SAVED toast, by the row's note, and by
 * the STOCK UI section the stock-only rows now live in. */
static void detail_text(int i,char*l,size_t n){
    const Setting*s=&SET[i]; char a[24],b[24]; int lock=(s->flags&SF_LOCK)!=0, sens=(s->flags&SF_SENSOR)!=0;
    if(sens) snprintf(l,n,"%s",TR(TX_D_MEASURED));
    else if(lock) snprintf(l,n,"%s",TR(TX_D_NOEDIT));
    else if(s->kind==K_ENUM){ if(s->max-s->min==1) snprintf(l,n,"%s / %s",TR(s->opts[0]),TR(s->opts[1])); else snprintf(l,n,TR(TX_D_NOPTS),s->max-s->min+1); }
    else { const char*u=set_unit(i); set_fmt(i,s->lo,a,sizeof a); set_fmt(i,s->hi,b,sizeof b); snprintf(l,n,*u?"%s - %s %s":"%s - %s",a,b,u); }
}
/* v6.17: is the stepper worth holding? (hold = repeat w. acceleration; shown as a micro-hint while the value is unchanged) */
static int detail_holdable(const Setting*s){ return s->kind==K_NUM&&!(s->flags&SF_LOCK)&&s->step>0&&(s->hi-s->lo)/s->step>=20; }
static void static_detail(void){
    const Setting*s=&SET[sel]; char l[72]; int lock=(s->flags&SF_LOCK)!=0, sens=(s->flags&SF_SENSOR)!=0;
    detail_text(sel,l,sizeof l);
    text_c(CX,82,TR(s->label),F_S,C_TXT2,255);
    /* range line (+ visible hint when max/mapping is an estimate, PARITY §6.6) */
    if(s->hint&&!lock){
        const char*hint=TR(s->hint);
        float w1=text_wf(l,F_XS), w2=text_wf(" · ",F_XS), w3=text_wf(hint,F_XS); float x=CX-(w1+w2+w3)/2;
        x=text_at(x,106,l,F_XS,C_TXT3,255); x=text_at(x,106," · ",F_XS,C_TXT3,255); text_at(x,106,hint,F_XS,C_WARN,255);
    } else text_c(CX,106,l,F_XS,C_TXT3,255);
    text_c(CX,254,TR(s->note),F_XS,(lock&&!sens&&s->mcu!=M_NONE)?C_WARN:C_TXT3,255);
}
/* v6.11: close chevron at the top of the detail/system view (tap = CANCEL/BACK; swiping down does the same). The down arrow is
 * the "pull down to close" handle; the tap field is 88 x 46 px (BACK_*). */
/* v6.17: the editor's status line (shared w/ the selftest): SAVING… / out of spec / CHANGED · WAS … / the zone's place names / — when the
 * value is untouched — the one thing about −/+ you cannot see: holding repeats and accelerates. Otherwise empty ("SAVED VALUE" is gone). */
static uint32_t detail_status(char*l,size_t n){
    const Setting*s=&SET[sel]; char o[40]; int lock=(s->flags&SF_LOCK)!=0; l[0]=0;
    if(sav.active){ snprintf(l,n,"%s",TR(TX_D_SAVING)); return C_ACC; }
    if(!lock&&!set_valid(s,edit_val)){ snprintf(l,n,"%s",TR(TX_D_OOR)); return C_ERR; }
    if(edit_val!=edit_orig){ set_fmt(sel,edit_orig,o,sizeof o); snprintf(l,n,TR(TX_D_CHANGED),o,s->kind==K_NUM?set_unit(sel):""); return C_ACC; }
    if(sel==S_TZ&&edit_val>=0&&edit_val<TZ_N){ snprintf(l,n-4,"%s",TZT[edit_val].name); fit_text(l,F_XS,276); return C_TXT2; }   /* v6.16: the zone's place names */
    if(detail_holdable(s)) snprintf(l,n,"%s",TR(TX_D_HOLDFAST));
    return C_TXT3;
}
static void draw_backch(void){
    int pr=pressed(T_BACK);
    if(W_begin(W_BACKCH,R(CX-26,42,52,30),hi(H0,pr))){
        if(pr) rrect(CX-24,44,48,26,13,C_SURF3,255);
        uint32_t c=pr?C_TXT:C_TXT2;
        capsule(CX-9,53,CX,61,1.5f,c,255); capsule(CX,61,CX+9,53,1.5f,c,255);
        W_end();
    }
}
static void draw_detail(ms_t now){
    (void)now; draw_backch(); const Setting*s=&SET[sel]; char b[40],l[112];
    int lock=(s->flags&SF_LOCK)!=0; set_fmt(sel,edit_val,b,sizeof b); const char*u=set_unit(sel);
    int eoor=!lock&&!set_valid(s,edit_val);
    if(W_begin(W_DHERO,R(122,128,236,90),hi(hs(hs(H0,b),u),eoor))){
        float uw=(*u&&s->kind==K_NUM)?text_wf(u,F_M)+10:0;
        int f=(s->kind==K_NUM)?F_XXL:F_XL;                       /* largest font that fits in the field */
        while(f>F_M&&text_wf(b,f)+uw>228) f=(f==F_XXL)?F_XL:(f==F_XL?F_L:F_M);
        float nw=text_wf(b,f); float x=CX-(nw+uw)/2; int y=173-(int)FS[f].cap/2;
        text_at(x,y,b,f,eoor?C_ERR:(lock?C_TXT2:C_TXT),255);
        if(uw>0) text_at(x+nw+10,y+(int)FS[f].cap-18,u,F_M,C_TXT2,255);
        W_end();
    }
    int chg=edit_val!=edit_orig;
    { uint32_t stc=detail_status(l,sizeof l);
      if(W_begin(W_DCHG,R(100,224,280,22),hi(hs(H0,l),(int)stc))){ if(l[0]) text_c(CX,228,l,F_XS,stc,255); W_end(); } }
    for(int k=0;k<2;k++){
        int cx=k?396:84, t=k?T_PLUS:T_MINUS; int en=!lock&&!sav.active&&(eoor||(k?edit_val<s->hi:edit_val>s->lo)); int pr=pressed(t);
        if(W_begin(k?W_PLUS:W_MINUS,R(cx-38,146,76,78),hi(hi(hi(H0,en),pr),lock))){
            if(!lock){
                disc(cx,186,30,rgb(0,0,0),70);
                disc(cx,184,30,pr&&en?C_SURF3:C_SURF2,255);
                ring(cx,184,29,30.2f,en?C_LINE:C_SURF2,255);
                const char*g=k?"+":"-"; float gw=text_wf(g,F_XL);
                text_at(cx-gw/2,184-24,g,F_XL,en?C_TXT:C_DIS,255);
            }
            W_end();
        }
    }
    if(W_begin(W_CANCEL,lock?R(170,286,140,60):R(100,286,136,60),hi(hi(H0,pressed(T_CANCEL)),sav.active))){
        draw_pill_btn(lock?R(178,292,124,48):R(108,292,124,48),TR(lock?TX_B_BACK:TX_B_CANCEL),F_XS,C_SURF2,C_SURF,sav.active?C_DIS:C_TXT,pressed(T_CANCEL)); W_end();
    }
    if(!lock&&W_begin(W_SAVE,R(244,286,136,60),hi(hi(hi(hi(H0,pressed(T_SAVE)),chg),sav.active),lock))){
        {
            if(chg&&!sav.active) draw_pill_btn(R(248,292,124,48),TR(TX_B_SAVE),F_S,rgb(250,204,140),rgb(232,174,100),C_ACCD,pressed(T_SAVE));
            else draw_pill_btn(R(248,292,124,48),TR(sav.active?TX_B_SAVING:TX_B_SAVE),F_S,C_SURF2,C_SURF,C_DIS,0);
        }
        W_end();
    }
}

/* ================= v6.2: SYSTEM ACTION (BOOT UI / SWITCH / EXIT REMINDER) =================
 * Same geometry as the stepper detail: title 82, sub-line 106, hero 128..218, status 228,
 * note 254, buttons 286..346, two footer lines 398/416. All text comes from sys_text(), which
 * the selftest also measures against the circle's chord. */
typedef struct { const char*title,*sub,*hero,*status,*note,*l1,*l2,*ok,*cancel; uint32_t c_status,c_note,c_l1; } SysT;
static void sys_text(SysT*t){
    static char st[96],nt[96],sb[96]; int lock=sys_locked();
    memset(t,0,sizeof *t); t->c_status=C_TXT3; t->c_note=C_TXT2; t->c_l1=C_TXT3; t->cancel=TR(lock?TX_B_BACK:TX_B_CANCEL);
    if(xsel==X_BOOTUI){
        int chg=x_edit!=x_orig;
        t->title=TR(TX_X_BOOTUI); t->sub=boot_state_text(); t->hero=lock?boot_next_name():TR(x_edit?TX_BN_OURS:TX_BN_ORIGINAL); t->ok=TR(TX_B_SAVE);
        if(lock) t->status=TR(TX_D_LOCKED);
        else if(chg){ snprintf(st,sizeof st,TR(TX_Y_CHANGED),TR(x_orig?TX_BN_OURS:TX_BN_ORIGINAL)); t->status=st; t->c_status=C_ACC; }
        /* v6.17: unchanged = no status line ("SAVED CHOICE" was noise) */
        t->note=lock?boot_lock_text(bs.lock,1):TR(TX_BH1); if(lock) t->c_note=C_WARN;
        if(lock){ t->l1=TR(bs.flag?TX_Y_FLAG_YES:TX_Y_FLAG_NO); t->l2=TR(TX_Y_FLAG_HANDSOFF); }
        else { t->l1=TR(TX_BH2); t->l2=TR(TX_BH3); }               /* the §9.1 help text */
    } else if(xsel==X_SWITCH){
        int cont=switch_mode()==SWM_CONT;
        snprintf(sb,sizeof sb,TR(TX_Y_NEXT),boot_next_name());      /* the runtime switch does not touch the boot choice */
        t->title=TR(TX_X_SWITCH); t->sub=sb; t->hero=TR(TX_BN_ORIGINAL); t->ok=TR(TX_B_SWITCH);
        if(lock){ t->status=TR(TX_D_LOCKED); t->note=TR(TX_SW_LOCK_L); t->c_note=C_WARN; t->l1=TR(TX_Y_NOSTOCK); t->l2=TR(TX_Y_STAYS); return; }
        t->status=TR(cont?TX_Y_CLOSE_CONT:TX_Y_CLOSE_START); t->c_status=C_TXT2;
        if(cont){
            if(x_pending){ t->note=TR(TX_Y_REMINDER_FOLLOWS); t->c_note=C_WARN; }
            else { snprintf(nt,sizeof nt,TR(TX_Y_PAUSED_PID),bs.stock[0]); t->note=nt; }
            t->l1=TR(TX_Y_RUNSH); t->l2=TR(TX_Y_BACK_RUNSH);
        } else {
            t->note=TR(TX_Y_FRESH);
            if(bs.sw_pid){ t->l1=TR(TX_Y_BACK_GESTURE1); t->l2=TR(TX_Y_BACK_GESTURE2); }
            else { t->l1=TR(TX_Y_NOSWITCHER); t->c_l1=C_WARN; t->l2=TR(bs.flag?TX_Y_BACK_RESTART:TX_Y_BACK_TELNET); }
        }
    } else {   /* X_EXIT */
        t->title=TR(TX_Y_EXIT_TITLE); t->sub=TR(TX_Y_EXIT_SUB); t->ok=TR(TX_B_EXIT); t->cancel=TR(TX_B_BACK);
        t->note=TR(TX_Y_EXIT_NOTE); t->c_note=C_TXT3;
        t->l1=TR(TX_Y_EXIT_F1); t->l2=TR(TX_Y_EXIT_F2); t->c_l1=C_TXT3;
    }
}
/* the exit reminder's body text (the v6.1 integration note): TX_Y_EXIT_L1 / _L2 + the command TX_Y_EXIT_CMD */
static int sys_hero_font(const char*hero){ if(xsel==X_BOOTUI) return F_H; int f=F_XL; while(f>F_M&&text_wf(hero,f)>296) f=(f==F_XL)?F_L:F_M; return f; }   /* BOOT UI: both choices in the SAME font */
static void static_sys(void){
    SysT t; sys_text(&t);
    if(xsel==X_EXIT){
        float iw=text_wf(IC_WARN,F_IC), tw=text_wf(t.title,F_S); float x=CX-(iw+9+tw)/2;
        text_at(x,78,IC_WARN,F_IC,C_WARN,255); text_at(x+iw+9,82,t.title,F_S,C_WARN,255);
    } else text_c(CX,82,t.title,F_S,C_TXT2,255);
    text_c(CX,106,t.sub,F_XS,C_TXT3,255);
}
static void draw_sys(ms_t now){
    (void)now; SysT t; sys_text(&t); int lock=sys_locked(); draw_backch();
    if(xsel==X_EXIT){
        if(W_begin(W_DHERO,R(60,130,360,102),hi(H0,xsel))){
            text_c(CX,138,TR(TX_Y_EXIT_L1),F_S,C_TXT,255);
            text_c(CX,162,TR(TX_Y_EXIT_L2),F_S,C_TXT,255);
            int w=text_w(TR(TX_Y_EXIT_CMD),F_M)+40;
            rrect_g(CX-w/2,188,w,38,12,C_SURF3,C_SURF2,255);
            text_c(CX,188+9,TR(TX_Y_EXIT_CMD),F_M,C_ACC,255);
            W_end();
        }
    } else {
        int f=sys_hero_font(t.hero);
        /* BOOT UI: the hero field must not overlap −/+ (x 46..122 / 358..434) — otherwise a hero redraw erases the button edges */
        if(W_begin(W_DHERO,xsel==X_BOOTUI?R(122,128,236,90):R(92,128,296,90),hi(hi(hs(H0,t.hero),lock),xsel))){
            text_c(CX,173-(int)FS[f].cap/2,t.hero,f,lock?C_TXT2:C_TXT,255);
            W_end();
        }
        for(int k=0;k<2&&xsel==X_BOOTUI;k++){
            int cx=k?396:84, tt=k?T_PLUS:T_MINUS; int en=!lock&&(k?!x_edit:x_edit); int pr=pressed(tt);
            if(W_begin(k?W_PLUS:W_MINUS,R(cx-38,146,76,78),hi(hi(hi(H0,en),pr),lock))){
                if(!lock){
                    disc(cx,186,30,rgb(0,0,0),70);
                    disc(cx,184,30,pr&&en?C_SURF3:C_SURF2,255);
                    ring(cx,184,29,30.2f,en?C_LINE:C_SURF2,255);
                    const char*g=k?"+":"-"; float gw=text_wf(g,F_XL);
                    text_at(cx-gw/2,184-24,g,F_XL,en?C_TXT:C_DIS,255);
                }
                W_end();
            }
        }
    }
    if(W_begin(W_DCHG,R(60,224,360,22),hi(hs(H0,t.status?t.status:""),(int)t.c_status))){ if(t.status) text_c(CX,228,t.status,F_XS,t.c_status,255); W_end(); }
    if(W_begin(W_XNOTE,R(40,xsel==X_EXIT?238:250,400,22),hi(hs(H0,t.note),(int)t.c_note))){ text_c(CX,xsel==X_EXIT?242:254,t.note,F_XS,t.c_note,255); W_end(); }
    if(W_begin(W_CANCEL,lock?R(170,286,140,60):R(100,286,136,60),hi(hi(H0,pressed(T_CANCEL)),lock))){
        draw_pill_btn(lock?R(178,292,124,48):R(108,292,124,48),t.cancel,F_XS,C_SURF2,C_SURF,C_TXT,pressed(T_CANCEL)); W_end();
    }
    int act_ok=(xsel!=X_BOOTUI)||(x_edit!=x_orig);
    if(!lock&&W_begin(W_SAVE,R(244,286,136,60),hi(hi(hs(H0,t.ok),pressed(T_SAVE)),act_ok))){
        if(act_ok) draw_pill_btn(R(248,292,124,48),t.ok,xsel==X_SWITCH?F_XS:F_S,rgb(250,204,140),rgb(232,174,100),C_ACCD,pressed(T_SAVE));
        else draw_pill_btn(R(248,292,124,48),t.ok,F_S,C_SURF2,C_SURF,C_DIS,0);
        W_end();
    }
    if(W_begin(W_XFOOT,R(70,392,340,40),hi(hs(hs(H0,t.l1),t.l2),(int)t.c_l1))){
        text_c(CX,398,t.l1,F_XS,t.c_l1,255); text_c(CX,416,t.l2,F_XS,C_TXT3,255); W_end();
    }
}
/* last frame before the runtime switch: stays on screen until the stock UI draws */
static const char* bye_sub(void){ return TR(bye_mode==SWM_CONT?TX_Y_BYE_CONT:TX_Y_BYE_START); }
static void draw_bye(void){
    if(W_begin(W_BYE,R(60,150,360,180),hi(hi(H0,bye_mode),ui_lang))){
        float iw=text_wf(IC_CUP,F_ICL); text_at(CX-iw/2,152,IC_CUP,F_ICL,C_ACC,255);
        text_c(CX,206,TR(TX_Y_BYE_T),F_M,C_TXT,255);
        text_c(CX,240,bye_sub(),F_XS,C_TXT2,255);
        text_c(CX,262,TR(TX_Y_BYE_CLOSED),F_XS,C_TXT3,255);
        W_end();
    }
}

/* ================= v6.14: TURNING OFF… / OFF / TURNING ON… (the power button) =================
 * Black page, power symbol (ring w/ opening at the top + bar), title and one or two lines. The backlight goes off 1.5 s after the off press
 * (like stock), so the OFF page is only seen for a moment — or for 8 s after a tap on the screen. */
#define OFF_IC_Y 178
static void draw_off(ms_t now){
    (void)now; int st=pw.state;
    if(W_begin(W_OFF,R(60,120,360,210),hi(hi(H0,st),ui_lang))){
        uint32_t ic=st==PW_OFF?C_DIS:C_ACC;
        arc_aa(CX,OFF_IC_Y,30,3.0f,-50,230,ic,255);                                   /* the ring: open from -130° to -50° (at the top) */
        capsule(CX,OFF_IC_Y-40,CX,OFF_IC_Y-6,3.0f,ic,255);                            /* the bar */
        text_c(CX,232,TR(st==PW_OFFING?TX_PW_OFFING:(st==PW_WAKING?TX_PW_WAKING:TX_PW_OFF)),F_M,st==PW_OFF?C_TXT2:C_TXT,255);
        text_c(CX,268,TR(st==PW_OFFING?TX_PW_SUB_OFFING:(st==PW_WAKING?TX_PW_SUB_WAKING:TX_PW_SUB_OFF)),F_XS,C_TXT3,255);
        if(st==PW_OFF) text_c(CX,290,TR(TX_PW_NOTE_OFF),F_XS,C_DIS,255);
        W_end();
    }
}

/* ================= page: DIAGNOSE ================= */
static void static_diag(void){ static_page_title(); }   /* v6.17: title only (the sub-line listed the sections) */
#define DG_N 16
#define DG_NB 6
#define DG_FULL_W 336
#define DG_COL_L 154
#define DG_COL_R 326
/* v6.11: cell i -> slot (0..7 = the STATUS block at the top, 8..15 = RAW COUNTERS); even = left column */
static const signed char DG_SLOT[DG_N]={0,1,8,9,10,2,11,3,4,12,13,14,15,5,6,7};
static int dg_cell_y(int i){ int k=DG_SLOT[i]; return k<8? DG_Y0+(k/2)*DG_P : DG_RAW+28+((k-8)/2)*DG_P; }
static const short DG_LAB[DG_N+DG_NB]={TX_DL_LINK,TX_DL_PHASE,TX_DL_RXOK,TX_DL_RXCRC,TX_DL_WORKSTA,TX_DL_E123,TX_DL_CMD6,TX_DL_AWAKE,TX_DL_MAINS,TX_DL_CURRENT,TX_DL_FRAMEMS,TX_DL_MIRROR,TX_DL_CFGMD5,TX_DL_VERSION,TX_DL_BP_A,TX_DL_BP_B,
                                       TX_DL_FLAG,TX_DL_NEXTBOOT,TX_DL_STARTED,TX_DL_SLOTB,TX_DL_SWITCHER,TX_DL_STOCK};
/* the boot block's texts (shared w/ the selftest). v[0..5] = cells, exe/evt = full-width lines */
static void diag_boot_text(char v[DG_NB][28],uint32_t col[DG_NB],char*exe,size_t en,char*evt,size_t vn,char*pids,size_t pn){
    for(int i=0;i<DG_NB;i++) col[i]=C_TXT;
    snprintf(v[0],28,"%s",TR(bs.flag?TX_DV_FLAG_YES:TX_DV_FLAG_NO));
    snprintf(v[1],28,"%s",boot_next_name()); if(bs.flag&&bs.lock!=BL_OK) col[1]=C_WARN;       /* flag set, but slot B is not merakiplus */
    snprintf(v[2],28,"%s",TR(bs.self_slot==SLOT_B?TX_DV_SLOTB:(bs.self_slot==SLOT_A?TX_DV_SLOTA:(bs.self_exe[0]?TX_DV_RUNSH:TX_DV_UNKNOWN))));
    snprintf(v[3],28,"%s",TR(bs.lock==BL_OK?TX_DV_B_OK:(bs.lock==BL_NOMOUNT?TX_DV_B_NOMOUNT:(bs.lock==BL_NOBIN?TX_DV_B_NOBIN:TX_DV_B_VENDOR)))); col[3]=bs.lock==BL_OK?C_OK:C_WARN;
    snprintf(v[4],28,"%s",TR(bs.sw_pid?TX_DV_RUNNING:TX_DV_NOTRUNNING)); col[4]=bs.sw_pid?C_OK:C_WARN;
    snprintf(v[5],28,"%s",TR(!bs.n_stock?TX_DV_NONE:(bs.stock_run?TX_DV_ALSO:TX_DV_PAUSED))); if(bs.stock_run) col[5]=C_ERR;
    { char a[16]="-",b[16]="-"; if(bs.n_stock) snprintf(a,sizeof a,"%d",bs.stock[0]); if(bs.sw_pid) snprintf(b,sizeof b,"%d",bs.sw_pid);
      snprintf(pids,pn,"%d / %s%s / %s",bs.self_pid,a,bs.n_stock>1?" +":"",b); }
    if(bs.self_exe[0]) snprintf(exe,en-4,LC "%s",bs.self_exe); else snprintf(exe,en-4,"%s",TR(TX_DG_NOEXE));
    utf8_trim(exe); fit_text(exe,F_XS,DG_FULL_W);
    /* the event text comes verbatim from the switcher daemon's log (boot/switcher.c) and is not translated */
    snprintf(evt,vn-4,"%s",bs.sw_log==2?bs.sw_evt:TR(bs.sw_log==1?TX_DG_NOEVT:TX_DG_NOLOG));
    utf8_trim(evt); fit_text(evt,F_XS,DG_FULL_W);
}
/* v6.5: boot-push status per block (0 = sub1 len30, 1 = sub112 len3) */
static const char* bp_text(int blk,uint32_t*col){
    int r=blk?bp.rb:bp.ra, sent=blk?bp.state==BP_B:bp.state==BP_A; *col=C_TXT;
    if(r==BPR_OK){ *col=C_OK; return TR(TX_DV_OK); }
    if(r==BPR_NOACK){ *col=C_WARN; return TR(TX_DV_BP_NOACK); }
    if(r==BPR_BLOCKED){ *col=C_WARN; return TR(TX_DV_BP_BLOCKED); }
    if(r==BPR_NOCFG){ *col=C_WARN; return TR(TX_DV_BP_NOCFG); }
    if(r==BPR_SKIP) return TR(TX_DV_BP_SKIP);
    if(bp.state==BP_OFF) return TR(TX_DV_BP_OFF);
    return TR(sent?TX_DV_BP_SENT:TX_DV_BP_WAIT);
}
/* the top 16 cells (shared w/ the selftest) */
static void diag_top_text(char v[DG_N][28],uint32_t col[DG_N],ms_t now){
    int up=link_up(now);
    snprintf(v[0],28,"%s",TR(up?TX_DV_OK:TX_DV_DOWN)); snprintf(v[1],28,"%s",act.ph==PH_STEAM?steam_label(stm.kind):ph_name(act.ph));
    snprintf(v[2],28,"%u",rx_ok); snprintf(v[3],28,"%u / %u",rx_bad,rx_resync);
    snprintf(v[4],28,"%04X",reg[33]); snprintf(v[5],28,"%X / %X / %X",reg[34],reg[35],reg[36]);
    snprintf(v[6],28,"%ld / %ld / %ld / %ld",wq.n_ok,wq.n_retx,wq.n_fail,wq.n_block); snprintf(v[7],28,"%s",!reg_seen[1]?"?":TR(reg[1]==0?TX_DV_YES:TX_DV_NO));
    snprintf(v[8],28,TR(TX_DV_MAINS),reg[49],reg[61]); snprintf(v[9],28,"%u / %u",reg[50],reg[58]);
    snprintf(v[10],28,"%.1f / %.1f",ft_last,ft_max);
    if(reg_seen[112]) snprintf(v[11],28,"%04X %04X %u",reg[112],reg[113],reg[114]); else snprintf(v[11],28,"-");
    if(cfg_ok) snprintf(v[12],28,"%.8s%s",cfg_md5,cfg_writes?" *":""); else snprintf(v[12],28,"%s",TR(TX_DV_MISSING));
    snprintf(v[13],28,"%s",TR(ro_mode?TX_DV_VERSION_RO:TX_DV_VERSION));
    uint32_t bc[2]; snprintf(v[14],28,"%s",bp_text(0,&bc[0])); snprintf(v[15],28,"%s",bp_text(1,&bc[1]));
    for(int i=0;i<DG_N;i++){ uint32_t c=C_TXT; if(i==0) c=up?C_OK:C_ERR; if(i==3&&(rx_bad||rx_resync)) c=C_WARN; if(i>=14) c=bc[i-14]; if(i==5&&(reg[34]|reg[35]|reg[36])) c=C_ERR; if(i==6&&(wq.n_fail||wq.n_block)) c=C_WARN; if(i==1) c=ph_hi(act.ph); col[i]=c; }
}
/* v6.6: cell label. After a re-push the boot-push row is labelled "RE-PUSH 2 · 1-30" | "112-114" (2 = the boot push has been resent
 * from scratch twice after an MCU restart), the first in the accent colour; the cells show the outcome of the LATEST push.
 * Without a re-push — and for all other cells — the label is the table's text. */
static uint32_t diag_label(int i,char*o,size_t n){
    int rc=repush_count(); if(rc>999) rc=999;
    if(i==14&&rc>0){ snprintf(o,n,TR(TX_DL_BP_A_RE),rc); return C_ACC; }
    if(i==15&&rc>0){ snprintf(o,n,"%s",TR(TX_DL_BP_B_RE)); return C_TXT3; }
    snprintf(o,n,"%s",TR(DG_LAB[i])); return C_TXT3;
}
/* the ABOUT block at the bottom: which UI language is active, and where it comes from */
static void diag_lang_text(char*o,size_t n){
    static const short f[]={[LS_DEFAULT]=TX_DG_LANG_DEFAULT,[LS_FILE]=TX_DG_LANG_FILE,[LS_UNKNOWN]=TX_DG_LANG_UNKNOWN,[LS_SESSION]=TX_DG_LANG_SESSION};
    snprintf(o,n,TR(f[lang_src]),TR(TX_UL_EN+ui_lang));
}
/* v6.7: the NET line (shared w/ the selftest): wpa_supplicant pid / IP / net watchdog state. F_S; if the line gets too wide
 * (5-digit pid + 15-char IP + CONNECTING), it is drawn in F_XS so it is never clipped by the circle. */
static int diag_net_font(const char*t){ return text_wf(t,F_S)<=DG_FULL_W?F_S:F_XS; }
static void diag_net_text(char*o,size_t n,uint32_t*col){
    char ipb[20],pid[16]="-"; int s=net.on?net.state:NS_OFF;
    if(net.p.wpa_pid>0) snprintf(pid,sizeof pid,"%d",net.p.wpa_pid);
    snprintf(o,n,"%s / %s / %s",pid,net_ipstr(net.p.ip,ipb,sizeof ipb),TR(TX_NS_OFF+s));
    *col=s==NS_UP?C_OK:(s==NS_OFF?C_TXT2:C_WARN);
}
/* v6.10: the PRESSURE · ESTIMATE block (shared w/ the selftest): the anchor line, the now line (raw word 51 -> ≈bar; above P_EST_MAX a pulse —
 * at idle "MAINTENANCE PULSE", during an activity "PULSE, NO PRESSURE", never a bar value) and note line 2 (the limit) */
static void diag_press_text(char*anc,size_t an,char*nw,size_t nn,uint32_t*col){
    snprintf(anc,an,TR(TX_DV_PANCH),p_anchor_bar(),P_ANCHOR_RAW,(double)p_k());
    int r=reg[51]; float b=p_bar((float)r);
    if(b>=0.f){ snprintf(nw,nn,TR(TX_DV_PNOW),r,(double)b); *col=C_TXT; }
    else { snprintf(nw,nn,TR(!phase_active(act.ph)&&act.ph!=PH_DONE?TX_DV_PMAINT:TX_DV_PPULSE),r); *col=C_WARN; }
}
static void dg_caption(int yc,const char*cap,Rect vr){
    int tw=text_w(cap,F_XS);
    if(yc+11>=vr.y0&&yc<=vr.y1){ text_c(CX,yc,cap,F_XS,C_ACC,255); hspan(yc+5,84,CX-tw/2-12,C_LINE,255); hspan(yc+5,CX+tw/2+12,396,C_LINE,255); }
}
/* v6.14: the POWER BUTTON block's texts (shared w/ the selftest) */
static void diag_pw_text(char st[28],uint32_t*stc,char w0[28],char cnt[28],char key[40],char*node,size_t nn,ms_t now){
    static const short S[4]={TX_DV_PW_ON,TX_DV_PW_OFFING,TX_DV_PW_OFF,TX_DV_PW_WAKING};
    snprintf(st,28,"%s",TR(S[pw.state])); *stc=pw.state==PW_ON?C_OK:(pw.state==PW_OFF?C_TXT2:C_WARN);
    if(link_up(now)&&reg_seen[0]) snprintf(w0,28,"%u",(unsigned)reg[0]); else snprintf(w0,28,"%s",TR(TX_DV_PW_NOREPLY));
    snprintf(cnt,28,"%ld · %ld · %ld",pw.n_press,pw.n_off,pw.n_on);
    if(pw.n_ev) snprintf(key,40,TR(TX_DV_PW_KEY),pw.key_code,pw.key_val,(long)((now-pw.t_key)/1000)); else snprintf(key,40,"%s",TR(TX_DV_PW_NOKEY));
    if(pw.kbd_ok) snprintf(node,nn,LC "%s",pw.node); else snprintf(node,nn,"%s",TR(TX_DV_PW_NONODE));   /* the path as a code span (lower case) */
}
/* v6.16: the TIME · SCHEDULE block's texts (shared w/ the selftest) */
static void diag_time_text(char clk[28],uint32_t*cc,char zone[40],char ao[28],char al[40],char nx[72],ms_t now){
    struct tm lt; int64_t u=clk_utc(); int ok=clk_now(&lt), d=ok&&SET[S_DST].val&&dst_on(tz_idx(),u);
    if(ok){ fmt_hm(clk,28,lt.tm_hour,lt.tm_min,1); *cc=C_TXT; } else { snprintf(clk,28,"%s",TR(TX_DV_T_NOTSET)); *cc=C_WARN; }
    snprintf(zone,40,TR(d?TX_DV_T_DST:TX_DV_T_STD),TZT[tz_idx()].utc);
    int a=SET[S_AUTOOFF].val; if(a>0&&a<5){ ms_t id=now-idle_t0; if(id<0) id=0; snprintf(ao,28,TR(TX_DV_T_IDLE),TR(OPT_AUTOOFF[a]),(int)(id/60000),(int)(id/1000%60)); }
    else snprintf(ao,28,"%s",TR(TX_E_OFF));
    if(arm.n_sent) snprintf(al,40,"%d %02d:%02d · %ld/%ld",arm.ad,arm.ah,arm.am,arm.n_sent,arm.n_ack); else snprintf(al,40,"%s",TR(TX_DV_T_NONE));
    if(!home_preheat_text(nx,72)) snprintf(nx,72,"%s",TR(sch.on&&!ok?TX_T_NOCLOCK:TX_DV_T_NOSCHED));
}
static void draw_diag(ms_t now){
    char v[DG_N+DG_NB][28]; uint32_t col[DG_N+DG_NB]; char exe[160],evt[128],pids[48],lng[96],nets[64],panc[64],pnow[64]; uint32_t netc,pcol;
    diag_top_text(v,col,now); diag_net_text(nets,sizeof nets,&netc); diag_press_text(panc,sizeof panc,pnow,sizeof pnow,&pcol);
    diag_boot_text(v+DG_N,col+DG_N,exe,sizeof exe,evt,sizeof evt,pids,sizeof pids);
    diag_lang_text(lng,sizeof lng); draw_pback();
    char pst[28],pw0[28],pcnt[28],pkey[40],pnode[80]; uint32_t pstc; diag_pw_text(pst,&pstc,pw0,pcnt,pkey,pnode,sizeof pnode,now);   /* v6.14 */
    char tck[28],tzn[40],tao[28],tal[40],tnx[72]; uint32_t tcc; diag_time_text(tck,&tcc,tzn,tao,tal,tnx,now);   /* v6.16 */
    int sc=(int)lroundf(scroll[PG_DIAG]); float mx=scroll_max(PG_DIAG);
    uint32_t h=hi(hi(hi(hi(hi(H0,sc),ui_lang),repush_count()),cd.raw),pressed(T_GRAPH)); for(int i=0;i<DG_N+DG_NB;i++) h=hi(hs(h,v[i]),(int)col[i]); h=hs(hs(hs(hs(h,exe),evt),pids),lng); h=hi(hs(h,nets),(int)netc); h=hi(hs(hs(h,panc),pnow),(int)pcol);
    h=hi(hs(hs(hs(hs(hs(h,pst),pw0),pcnt),pkey),pnode),(int)pstc);
    h=hi(hs(hs(hs(hs(hs(h,tck),tzn),tao),tal),tnx),(int)tcc);
    Rect vr=R(40,LIST_VY0,400,LIST_VY1-LIST_VY0);
    if(W_begin(W_DIAG,vr,h)){
        for(int i=0;i<DG_N+DG_NB;i++){
            int cx=((i<DG_N?DG_SLOT[i]:i)&1)?DG_COL_R:DG_COL_L, y=(i<DG_N? dg_cell_y(i) : DG_BOOT+((i-DG_N)/2)*DG_P)-sc;
            if(y+30<vr.y0||y>vr.y1) continue;
            { char lb[48]; uint32_t lc=diag_label(i,lb,sizeof lb); text_c(cx,y,lb,F_XS,lc,255); }
            text_c(cx,y+16,v[i],F_S,col[i],255);
        }
        dg_caption(DG_BOOT-28-sc,TR(TX_DG_CAP),vr);                               /* section heading */
        for(int k=0;k<3;k++){   /* full-width lines: PIDs, exe path, latest switch */
            int y=DG_BOOT+(3+k)*DG_P-sc; if(y+30<vr.y0||y>vr.y1) continue;
            text_c(CX,y,TR(k==0?TX_DG_L_PID:(k==1?TX_DG_L_EXE:TX_DG_L_EVT)),F_XS,C_TXT3,255);
            if(k==0) text_c(CX,y+16,pids,F_S,C_TXT,255);
            else text_c(CX,y+16,k==2?evt:exe,F_XS,(k==2&&bs.sw_log!=2)?C_TXT2:C_TXT,255);
        }
        { int y=DG_BOOT+6*DG_P-sc;                                                  /* v6.7: NET — wpa pid / IP / state */
          if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_NET),F_XS,C_TXT3,255); text_c(CX,y+16,nets,diag_net_font(nets),netc,255); } }
        { int y=DG_GRAPH-sc;                                                        /* v6.9: GRAPH — smooth / raw (tap) */
          if(y+40>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_GRAPH),F_XS,C_TXT3,255);
              uint32_t gc=cd.raw?C_WARN:C_TXT; rrect(CX-150,y+12,300,26,13,cd.raw?mix(C_SURF,C_WARN,40):C_SURF2,255);
              if(pressed(T_GRAPH)) rrect(CX-150,y+12,300,26,13,rgb(255,255,255),36);
              text_c(CX,y+18,TR(cd.raw?TX_DV_G_RAW:TX_DV_G_SMOOTH),F_XS,gc,255); } }
        /* v6.10: PRESSURE · ESTIMATE — anchor (pre-infusion in the capture = config pressure), current value (raw -> ≈bar / pulse), caveat */
        dg_caption(DG_PRESS-sc,TR(TX_DG_PCAP),vr);
        { int y=DG_PRESS+28-sc;
          if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_PANCH),F_XS,C_TXT3,255); text_c(CX,y+16,panc,F_S,C_TXT,255); }
          y+=DG_P; if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_PNOW),F_XS,C_TXT3,255); text_c(CX,y+16,pnow,F_S,pcol,255); } }   /* v6.17: the two caveat lines moved to docs/calibration.md */
        dg_caption(DG_RAW-sc,TR(TX_DG_RAWCAP),vr);                               /* v6.11: RAW COUNTERS (the cells are drawn in the loop above) */
        /* v6.14: POWER BUTTON — state, the controller's word 0, press/off/on, latest key event, node */
        dg_caption(DG_PWR-sc,TR(TX_DG_PWCAP),vr);
        { static const short L[4]={TX_DL_PW_STATE,TX_DL_PW_W0,TX_DL_PW_COUNT,TX_DL_PW_LAST}; const char*vv[4]={pst,pw0,pcnt,pkey};
          for(int k=0;k<4;k++){ int cx=(k&1)?DG_COL_R:DG_COL_L, y=DG_PWR+28+(k/2)*DG_P-sc; if(y+30<vr.y0||y>vr.y1) continue;
              text_c(cx,y,TR(L[k]),F_XS,C_TXT3,255); text_c(cx,y+16,vv[k],k==3?F_XS:F_S,k==0?pstc:C_TXT,255); }
          int y=DG_PWR+28+2*DG_P-sc;
          if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_PWNODE),F_XS,C_TXT3,255); text_c(CX,y+16,pnode,F_XS,pw.kbd_ok?C_TXT:C_WARN,255); } }
        /* v6.16: TIME · SCHEDULE — local clock, zone + DST, auto off + idle time, last cmd8 alarm sent (day h:m · sent/acked), next pre-heat */
        dg_caption(DG_TIME-sc,TR(TX_DG_TCAP),vr);
        { static const short L[4]={TX_DL_T_CLOCK,TX_DL_T_ZONE,TX_DL_T_AUTOOFF,TX_DL_T_ALARM}; const char*vv[4]={tck,tzn,tao,tal};
          for(int k=0;k<4;k++){ int cx=(k&1)?DG_COL_R:DG_COL_L, y=DG_TIME+28+(k/2)*DG_P-sc; if(y+30<vr.y0||y>vr.y1) continue;
              text_c(cx,y,TR(L[k]),F_XS,C_TXT3,255); text_c(cx,y+16,vv[k],k==0?F_S:F_XS,k==0?tcc:C_TXT,255); }
          int y=DG_TIME+28+2*DG_P-sc;
          if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_NEXT),F_XS,C_TXT3,255); text_c(CX,y+16,tnx,F_XS,sch.on?C_TXT:C_TXT2,255); } }
        /* v6.18: SOUND — mode · volume (amber: muted / no player), the last event and what became of it */
        dg_caption(DG_SND-sc,TR(TX_DG_SNDCAP),vr);
        { char sm[40], sl[64]; uint32_t smc=snd_diag_text(sm,sizeof sm,sl,sizeof sl); const short L[2]={TX_DL_SND,TX_DL_SND_LAST}; const char*vv[2]={sm,sl};
          for(int k=0;k<2;k++){ int cx=k?DG_COL_R:DG_COL_L, y=DG_SND+28-sc; if(y+30<vr.y0||y>vr.y1) continue;
              text_c(cx,y,TR(L[k]),F_XS,C_TXT3,255); text_c(cx,y+16,vv[k],F_XS,k?C_TXT:smc,255); } }
        /* v6.3: ABOUT — UI language (+ source), wordmark w/ version, tagline */
        dg_caption(DG_ABOUT-sc,TR(TX_DG_ABOUT),vr);
        { int y=DG_ABOUT+28-sc;
          if(y+30>=vr.y0&&y<=vr.y1){ text_c(CX,y,TR(TX_DG_L_LANG),F_XS,C_TXT3,255); text_c(CX,y+16,lng,F_S,(lang_src==LS_UNKNOWN||lang_src==LS_SESSION)?C_WARN:C_TXT,255); }   /* amber: the file has an unknown value / could not be written */
          y=DG_ABOUT+76-sc;
          if(y+36>=vr.y0&&y<=vr.y1){
              float w=text_wf(PRODUCT " " MERAKIPLUS_VERSION,F_S), x=brand_at(CX-w/2,y,F_S,C_TXT,255);
              text_at(x,y," " MERAKIPLUS_VERSION,F_S,C_TXT2,255);
              text_c(CX,y+24,TR(TX_TAGLINE),F_XS,C_TXT3,255);
          } }
        edge_fade(vr,sc>0,sc<(int)mx);
        W_end();
    }
    draw_scroll_ind(DG_BOTTOM-DG_Y0+16);
}

/* ================= boot splash ================= */
#define SPLASH_W 300
static void splash_status(char*b,size_t n){ snprintf(b,n,MERAKIPLUS_VERSION " · %s",TR(cfg_ok?TX_SPL_CFG_OK:TX_SPL_CFG_MISSING)); }
static void draw_splash(ms_t now){
    float t=(now-ui_start)/(float)SPLASH_MS; int q=(int)(t*30);
    if(W_begin(W_SPL,R(CX-SPLASH_W/2,140,SPLASH_W,200),hi(hi(H0,q),ui_lang))){
        int a=(int)(255*clampf(t*3,0,1));
        float y=4*(1-ease_out(clampf(t*2.2f,0,1)));
        float iw=text_wf(IC_BEAN,F_ICL); text_at(CX-iw/2,152+(int)y,IC_BEAN,F_ICL,C_ACC,a);
        brand_c(CX,204+(int)y,F_XL,C_TXT,a);                       /* wordmark: MERAKI + "+" badge */
        int a2=(int)(255*clampf(t*3-0.6f,0,1)); char b[96];
        text_c(CX,270,TR(TX_TAGLINE),F_XS,C_TXT2,a2);
        splash_status(b,sizeof b); text_c(CX,296,b,F_XS,cfg_ok?C_TXT3:C_WARN,a2);
        W_end();
    }
}

/* ================= composition ================= */
static void static_build(void){
    memcpy(bgbase,bgpure,(size_t)W*H*4);
    cv=bgbase; set_clip(FULL);
    if(view==V_OFF) memset(bgbase,0,(size_t)W*H*4);                   /* v6.14: the OFF page is black (the backlight is off anyway) */
    else if(view==V_DETAIL) static_detail();
    else if(view==V_SYS) static_sys();
    else if(view==V_MAIN){
        if(pg_list(page)) static_list();
        else if(page==PG_DIAG) static_diag();
        else static_home();
    }
    memcpy(slay,bgbase,(size_t)W*H*4);
    cv=canvas;
}
static void compose(ms_t now){
    dmg.n=0; pres.n=0;
    if(static_dirty){ static_build(); static_dirty=0; force_all=1; }
    Rect bb;
    if(arc_paint(&garc,force_all,&bb)) rl_add(&dmg,bb);
    if(arc_paint(&parc,force_all,&bb)) rl_add(&dmg,bb);
    overlays_damage(now);
    if(force_all){ dmg.n=0; rl_add(&dmg,FULL); }
    for(int i=0;i<dmg.n;i++){ blit_rect(canvas,slay,dmg.r[i]); rl_add(&pres,dmg.r[i]); }
    cv=canvas; set_clip(FULL);
    if(view==V_SPLASH) draw_splash(now);
    else if(view==V_DETAIL) draw_detail(now);
    else if(view==V_SYS) draw_sys(now);
    else if(view==V_BYE) draw_bye();
    else if(view==V_OFF) draw_off(now);                               /* v6.14 */
    else if(page==PG_HOME) draw_home(now);
    else if(page==PG_DIAG) draw_diag(now);
    else draw_list(now);
    overlays_draw(now);
    force_all=0;
}
static int tr_was=0;
/* one UI frame: update, compose, present. Returns the buffer that was shown. */
static const uint32_t* ui_frame(ms_t now){
    ui_update(now);
    if(tr.type){ tr_was=1; if(trans_frame(now)) return tbuf; }
    if(tr_was){ tr_was=0; force_all=1; }
    compose(now);
    fb_present(canvas,&pres);
    return canvas;
}

/* protocol maintenance: keep-awake = word1 written as 0 (FIX: v4 wrote 1 = sleep/drain) */
static ms_t ka_next=0;
static void proto_keepawake(ms_t now,int first){
    if(ro_mode) return;
    if(pw.state!=PW_ON) return;            /* v6.14: switched off with the button — keep-awake does not fight it (the MCU is asleep and does not reply) */
    SIM_KA_HOLD
    if(first){ uint16_t on=1, awake=0; wq_push(0,&on,1); wq_push(1,&awake,1); ka_next=now+30000; return; }
    int asleep=link_up(now)&&reg_seen[1]&&reg[1]!=0;
    if((now>=ka_next||(asleep&&now>=ka_next-27000))&&!wq_busy()&&act.pending==ACT_NONE&&!sav.active){
        uint16_t awake=0; wq_push(1,&awake,1); ka_next=now+30000;
    }
}

