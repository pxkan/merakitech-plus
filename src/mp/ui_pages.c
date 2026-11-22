/* src/mp/ui_pages.c — per-frame logic, drawing, pages (HOME, lists, editor, system action, power, DIAGNOSTICS, splash), composition.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ================= per-frame logic ================= */
static int gauge_kind(int ph){ switch(ph){ case PH_GRIND: case PH_WATER: return 1; case PH_PRE: case PH_EXT: return 2; case PH_STEAM: return 3; default: return 0; } }
typedef struct { int ph,eff; uint32_t hi,lo; char hero[16]; const char*unit; char label[64],sub[80]; const char*sl[3]; char svv[3][16]; float gf,gmark,pf; int pvis;
                 int sk,conc; const char*chip,*badge;
                 char hint1[72]; const char*hint2; uint32_t hc1; char hint2b[72]; char alab[4][16]; float ef; } HomeM;   /* v6.17: alab = each icon's target, drawn under it */   /* v6.11: empty-state hint in the chart area (only when the chart is inactive) */   /* v6.6: steam class, concurrent steam, chip text, steam marker (NULL = none) */
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
        float fr=tgt>0? (float)w/tgt : 0; m->ef=clampf(fr,0,1);
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
        m->gf=clampf((mk-200)/600.f,0,1); m->ef=(sk==SK_WAND||tgt<=210)?m->gf:clampf((mk-200)/(float)(tgt-200),0,1);
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
        garc.vis=1; garc.c_hi=garc.c_lo=C_TXT; garc.track=rgb(196,110,64); garc.mark=-1; garc.f_t=garc.f_d=ease_io(t); parc.vis=0; ui_anim=1;
    } else if(view==V_DETAIL||view==V_SYS){ garc.vis=0; parc.vis=0;
    } else if(view==V_BYE||view==V_OFF){ garc.vis=0; parc.vis=0;
    } else if(page==PG_HOME){
        home_model(now);
        garc.vis=hm_mode==HM_ACTIVE; garc.f_t=hm.ef; garc.mark=-1; garc.c_hi=garc.c_lo=C_TXT; garc.track=rgb(204,122,80);
        parc.vis=0;
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
            while(now>=tch.rep_next){
                tch.rep_n++; int mult=tch.rep_n>28?5:1;
                edit_step(tch.target==T_PLUS?1:-1,mult);
                tch.rep_next+= tch.rep_n<5?170:(tch.rep_n<14?90:45);
            }
            ui_anim=1;
        }
    }
    /* v7.0: HOME's mode (rings / dimmed rings / live view / error) changes the static layer -> rebuild, and cross-fade if it is on screen */
    { int nm=home_mode_of(act.ph);
      if(nm!=hm_mode){
          int vis=view==V_MAIN&&page==PG_HOME&&!tr.type&&hm_mode>=0;
          if(vis) memcpy(snapA,canvas,(size_t)W*H*4);
          hm_mode=nm; static_dirty=1; if(nm==HM_ACTIVE) garc.f_d=0;
          if(vis){ arc_targets(now); compose(now); memcpy(snapB,canvas,(size_t)W*H*4); fade_to(now,280); }
      } }
    arc_targets(now);
    if(garc.vis&&view!=V_SPLASH) ui_anim|=arc_ease(&garc,dt);
    if(parc.vis) ui_anim|=arc_ease(&parc,dt);
    if(roll_off!=0&&!(tch.down&&tch.target==T_ROLL)){ roll_off*=expf(-dt/60.f); if(fabsf(roll_off)<0.6f) roll_off=0; ui_anim=1; }   /* v7.0: the roller settles */
    if(rip.on){ if(now-rip.t0>360) rip.on=0; ui_anim=1; }
    if(toast.dur&&now-toast.t0<toast.dur+50) ui_anim=1;
    if(sav.active||tr.type||tch.scrolling) ui_anim=1;
}

/* ================= drawing: shared parts ================= */
/* The wordmark (v7.0): "Meraki" in the style's face + a small raised "+" in a lighter weight. brand_at() returns x after the mark. */
static int brand_disc(int f){ return FS[f].cap>=24; }
static int brand_plus_f(int f){ return FS[f].cap>=24? F_M : F_XS; }
static float brand_wf(int f){ return text_wf(BRAND_WORD,f)+FS[f].cap*0.08f+text_wf(BRAND_BADGE,brand_plus_f(f)); }
static float brand_at(float x,int y,int f,uint32_t cw,int a){
    x=text_at(x,y,BRAND_WORD,f,cw,a)-FS[f].track+FS[f].cap*0.08f;
    int pf=brand_plus_f(f);
    return text_at(x,y-(int)(FS[pf].cap*0.15f),BRAND_BADGE,pf,cw,a);
}
static void brand_c(int cx,int y,int f,uint32_t cw,int a){ brand_at(cx-brand_wf(f)/2,y,f,cw,a); }
/* v7.0 buttons: an outline pill (secondary) or a solid white pill with deep copper text (primary) */
static void draw_pill_btn(Rect r,const char*txt,int font,uint32_t bg_t,uint32_t bg_b,uint32_t fg,int press){
    int w=r.x1-r.x0,h=r.y1-r.y0; (void)bg_b;
    if(bg_t==C_TXT){ rrect(r.x0,r.y0+3,w,h,h/2.f,rgb(60,20,6),55); rrect(r.x0,r.y0,w,h,h/2.f,press?rgb(255,230,212):C_TXT,255); }
    else { if(press) rrect(r.x0,r.y0,w,h,h/2.f,C_TXT,40); rrect_line(r.x0+1,r.y0+1,w-2,h-2,(h-2)/2.f,1.2f,C_TXT,fg==C_DIS?110:235); }
    text_c((r.x0+r.x1)/2,r.y0+(h-(int)FS[font].cap)/2,txt,font,bg_t==C_TXT?C_ACCD:fg,255);
}
/* an outline ring button with something in it (OK, play, −, +) */
static void draw_ring_btn(float cx,float cy,float r,int en,int pr,float hp){
    if(pr&&en) disc(cx,cy,r-1,C_TXT,36);
    ring(cx,cy,r-1.5f,r+1.0f,C_TXT,en?240:110);
    if(hp>0) arc_aa(cx,cy,r+7,2.2f,-90,-90+360*hp,C_TXT,255);
}
static void draw_play(float cx,float cy,float s,uint32_t c,int a){
    float x0=cx-s*0.38f, x1=cx+s*0.62f;
    capsule(x0,cy-s*0.6f,x0,cy+s*0.6f,1.4f,c,a); capsule(x0,cy-s*0.6f,x1,cy,1.4f,c,a); capsule(x0,cy+s*0.6f,x1,cy,1.4f,c,a);
}
#define AV_CHART_Y 226
static int toast_y(void){ return (view==V_MAIN&&page==PG_HOME&&hm_mode==HM_ACTIVE)?AV_CHART_Y+10:90; }   /* the live view: over the trace, not the number */
static Rect toast_rect(ms_t now){ Rect z={0,0,0,0}; if(!toast.dur||now-toast.t0>=toast.dur) return z; return R(52,toast_y()-8,376,52); }
static Rect rip_rect(void){ Rect z={0,0,0,0}; if(!rip.on) return z; return R((int)rip.x-52,(int)rip.y-52,104,104); }
static void overlays_damage(ms_t now){
    Rect c0=toast_rect(now), c1=rip_rect();
    if(!r_empty(ov_prev[0])) rl_add(&dmg,ov_prev[0]); if(!r_empty(c0)) rl_add(&dmg,c0); ov_prev[0]=c0;
    if(!r_empty(ov_prev[1])) rl_add(&dmg,ov_prev[1]); if(!r_empty(c1)) rl_add(&dmg,c1); ov_prev[1]=c1;
}
/* toast: a deep espresso pill with white text; a warning/error carries the warning icon in yellow */
static void overlays_draw(ms_t now){
    cv=canvas;
    Rect c1=rip_rect();
    if(!r_empty(c1)){
        set_clip(c1); float t=(now-rip.t0)/360.f; float e=ease_out(clampf(t,0,1));
        ring(rip.x,rip.y,6+e*30,8+e*36,C_TXT,(int)(70*(1-t)));
        rl_add(&pres,c1);
    }
    Rect c0=toast_rect(now);
    if(!r_empty(c0)){
        set_clip(c0); float t=(float)(now-toast.t0); float a=clampf(t/160.f,0,1)*clampf((toast.dur-t)/260.f,0,1);
        int warn=toast.col==C_WARN||toast.col==C_ERR;
        float iw=warn?text_wf(IC_WARN,F_XS)+8:0;
        int w=(int)(text_w(toast.msg,F_XS)+iw)+40; if(w>370)w=370; int x=CX-w/2, y=toast_y()-(int)((1-ease_out(clampf(t/200.f,0,1)))*8);
        rrect(x,y+3,w,36,18,rgb(30,10,2),(int)(80*a));
        rrect(x,y,w,36,18,rgb(66,28,10),(int)(242*a));
        float tx=x+20;
        if(warn){ text_at(tx,y+11,IC_WARN,F_XS,toast.col,(int)(255*a)); tx+=iw; }
        text_at(tx,y+11,toast.msg,F_XS,C_TXT,(int)(255*a));
        rl_add(&pres,c0);
    }
    set_clip(FULL);
}

/* ================= page: HOME ================= */
enum { W_CHIP=1, W_LABEL, W_HERO, W_SUB, W_CHART, W_STATS, W_ACT, W_FOOT, W_LIST, W_SCROLL, W_DHERO, W_DCHG, W_MINUS, W_PLUS, W_CANCEL, W_SAVE, W_DIAG, W_SPL, W_XNOTE, W_XFOOT, W_BYE, W_GEAR, W_BACKCH, W_OFF, W_CLOCK, W_LIVE, W_START, W_RING0=60, W_RING1, W_RING2, W_RING3 };   /* v6.16: W_CLOCK = HOME's top line (clock / wordmark); v6.17: W_LIVE / W_START (a page's live line, HOLD TO START) */
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
/* ================= v7.0 HOME: four ring buttons in the OEM language =================
 * Idle: the stock 2x2 grid of white ring buttons (Grinder, Hot Water / Brew, Steam), each with a line icon, its target inside
 * ("18.5 g", "36 g", "65°") and its name set along the ring's arc in a gap of the ring. A ring whose boiler is still heating
 * shows the heat-up as a filling arc (a faint track + a white arc = temperature / ready point). Tap = the feature's page,
 * hold = start (an inner arc sweeps around during the hold). The chevron at the top (tap or pull down) opens SYSTEM; the clock
 * sits at the bottom. A thin status line under the chevron says only what matters now (heating, blocked, no connection, a
 * planned pre-heat). While something runs, HOME becomes the live view: the big white number, a thin progress ring at the
 * edge, a fine trace, three numbers and a solid white STOP. An error turns the screen dark with a red ring. */
#define RING_HW 1.5f
static float ring_lab_ang(int i){ static const float A[4]={225,315,135,45}; return A[i]; }   /* the label faces the display's rim */
static int ring_lab_out(int i){ return i<2; }                                                  /* upper rings: tops outward */
static const char* ring_label(int i){ static const short L[4]={TX_R_GRIND,TX_R_WATER,TX_R_BREW,TX_R_STEAM}; return TR(L[i]); }
static float ring_gap_deg(int i){ return text_arc_deg(ring_label(i),F_LAB,RING_R)/2+7.f; }
/* how far a ring's boiler is: 1 = ready (or no boiler: the grinder), else the share of the way from 20 °C to its ready point */
static float ring_heat(int i){
    int k=RING_ACT[i];
    if(k==ACT_GRIND) return 1;
    int b=(k==ACT_STEAM), tw=b?47:40;
    int r=b?snd_steam_thr():(reg_seen[11]?(int)reg[11]:sv(S_BREWT));
    if(!reg_seen[tw]) return snd_ready(b)?1:0;
    int t=reg[tw];
    if(t>=r-20&&(snd_ready(b)||t>=r)) return 1;                  /* the MCU's heat stage (or the temperature itself) says ready */
    if(r<=210) return 0;
    return clampf((t-200)/(float)(r-200),0,0.98f);                /* the way from 20 °C to the ready point */
}
static void draw_chev(int cx,int y,uint32_t c,int a,float hw){ capsule(cx-10,y,cx,y+8,hw,c,a); capsule(cx,y+8,cx+10,y,hw,c,a); }
static void draw_chev_r(int x,int cy,uint32_t c){ capsule(x-3,cy-6,x+3,cy,1.3f,c,255); capsule(x+3,cy,x-3,cy+6,1.3f,c,255); }
static void static_home(void){
    if(hm_mode==HM_ERROR){ memcpy(bgbase,bgdark,(size_t)W*H*4); return; }
    if(hm_mode==HM_ACTIVE) return;
    int dim=hm_mode==HM_DIM; int a=dim?120:255;
    for(int i=0;i<4;i++){
        text_arc(RING_X[i],RING_Y[i],RING_R,ring_lab_ang(i),ring_lab_out(i),ring_label(i),F_LAB,C_TXT,a);
        const char*ic=ACT_ICON[RING_ACT[i]]; float iw=text_wf(ic,F_ICL);
        text_at(RING_X[i]-iw/2,RING_Y[i]-34,ic,F_ICL,C_TXT,a);
    }
}
/* one ring: track/heat arc (with the label gap), pressed fill, hold sweep, the target inside */
static void draw_ring(int i,float heat,int en,int pr,float hp,const char*val,int dim){
    float cx=RING_X[i], cy=RING_Y[i], la=ring_lab_ang(i), g=ring_gap_deg(i);
    float a0=la+g, span=360-2*g;
    int base=dim?110:255;
    if(pr&&en) disc(cx,cy,RING_R-3,C_TXT,30);
    if(heat>=1) arc_aa(cx,cy,RING_R,RING_HW,a0,a0+span,C_TXT,base);
    else {
        arc_aa(cx,cy,RING_R,RING_HW*0.8f,a0,a0+span,C_TXT,dim?60:95);
        if(heat>0.01f) arc_aa(cx,cy,RING_R,RING_HW+0.4f,a0,a0+span*heat,C_TXT,base);
    }
    if(hp>0) arc_aa(cx,cy,RING_R-10,2.2f,-90,-90+360*hp,C_TXT,255);
    text_c((int)cx,(int)cy+20,val,F_S,dim?C_DIS:C_TXT2,255);
}
static void draw_status_line(const char*t,uint32_t c,int warn){
    if(!t[0]) return;
    if(warn){ char b[96]; snprintf(b,sizeof b,IC_WARN " %s",t); fit_text(b,F_XS,270); text_c(CX,HOME_STAT_Y,b,F_XS,c,255); }
    else text_c(CX,HOME_STAT_Y,t,F_XS,c,255);
}
/* HOME's status line (shared w/ the selftest): what matters now, or nothing */
static uint32_t home_status_text(const HomeM*m,char*o,size_t n,int*warn,ms_t now){
    o[0]=0; *warn=0;
    int ph=m->ph;
    if(ph==PH_OFFLINE||ph==PH_SLEEP){ snprintf(o,n,"%s",m->sub); *warn=ph==PH_OFFLINE; return ph==PH_OFFLINE?C_WARN:C_TXT2; }
    if(now-last_bad_ms<10000&&rx_bad>0){ snprintf(o,n,TR(TX_H_CRC),rx_bad); *warn=1; return C_WARN; }   /* line noise: CRC errors in the last 10 s */
    if(m->hint1[0]&&m->hc1==C_WARN){ snprintf(o,n,"%s",m->hint1); *warn=1; return C_WARN; }
    if(act.pending!=ACT_NONE||(tch.down&&!tch.moved&&tch.target>=T_ACT0&&tch.target<=T_ACT3)){ snprintf(o,n,"%s",m->sub); return C_TXT; }
    if(ph==PH_HEAT){ snprintf(o,n,"%s",m->sub); return C_TXT2; }
    if(m->hint2){ snprintf(o,n,"%s",m->hint2); return C_TXT2; }
    return C_TXT2;
}
static void draw_stop_pill(Rect r,const char*txt,int font,int press){
    int w=r.x1-r.x0,h=r.y1-r.y0;
    rrect(r.x0,r.y0+3,w,h,h/2.f,rgb(60,20,6),60);
    rrect(r.x0,r.y0,w,h,h/2.f,press?rgb(255,232,214):C_STOP_T,255);
    text_c((r.x0+r.x1)/2,r.y0+(h-(int)FS[font].cap)/2,txt,font,C_STOP_FG,255);
}
/* live view (an activity runs, or its DONE screen): phase name, the big number + unit, the target line, the trace, 3 numbers, STOP */
#define AV_PH_Y 62
#define AV_HERO_Y 98
#define AV_SUB_Y 196
#define AV_STAT_Y 298

static void draw_home_active(ms_t now){
    const HomeM*m=&hm; uint32_t h; char b[64];
    int crc=now-last_bad_ms<10000&&rx_bad>0;
    h=hi(hi(hs(hs(H0,m->chip),m->badge?m->badge:""),crc?(int)rx_bad:0),m->ph==PH_DONE);
    if(W_begin(W_CHIP,R(90,AV_PH_Y-6,300,28),h)){
        if(m->badge){ snprintf(b,sizeof b,"%s · %s",m->chip,m->badge); text_c(CX,AV_PH_Y,b,F_S,C_TXT2,255); }
        else text_c(CX,AV_PH_Y,m->chip,F_S,C_TXT2,255);
        if(crc){ snprintf(b,sizeof b,TR(TX_H_CRC),rx_bad); text_c(CX,AV_PH_Y+18,b,F_TINY,C_WARN,255); }
        W_end();
    }
    h=hi(hs(hs(H0,m->hero),m->unit),m->ph);
    if(W_begin(W_HERO,R(70,AV_HERO_Y-6,340,96),h)){
        float nw=text_wf(m->hero,F_XXL), uw=text_wf(m->unit,F_L); float x=CX-(nw+8+uw)/2;
        text_at(x,AV_HERO_Y,m->hero,F_XXL,C_TXT,255);
        text_at(x+nw+8,AV_HERO_Y+(int)FS[F_XXL].cap-(int)FS[F_L].cap,m->unit,F_L,C_TXT,255);
        W_end();
    }
    if(W_begin(W_SUB,R(SUB_X0,AV_SUB_Y-4,SUB_W,26),hi(hs(H0,m->sub),m->ph))){
        int sf=text_wf(m->sub,F_S)<=SUB_W-8?F_S:F_XS; text_c(CX,AV_SUB_Y,m->sub,sf,C_TXT2,255); W_end();
    }
    if(W_begin(W_CHART,R(CX-HINT_W/2,AV_CHART_Y,HINT_W,60),hs(hs(hi(hi(hi(hi(hi(H0,g_head),g_n),(int)m->hi),m->sk*8+(m->eff==PH_STEAM)),cd.raw*16+cd.scale),m->hint1),m->hint2?m->hint2:""))){ draw_chart(R(CX-130,AV_CHART_Y,260,60),now,m); W_end(); }
    h=H0; for(int i=0;i<3;i++){ h=hs(h,m->sl[i]); h=hs(h,m->svv[i]); }
    if(W_begin(W_STATS,R(84,AV_STAT_Y-4,312,56),h)){
        for(int i=0;i<3;i++){ int x=CX+(i-1)*STAT_PITCH; text_c(x,AV_STAT_Y,m->svv[i],F_STAT,C_TXT,255); text_c(x,AV_STAT_Y+31,m->sl[i],F_TINY,C_TXT3,255); }
        W_end();
    }
    int sk[2], sn=stop_btns(now,sk);
    h=hi(hi(hi(hi(H0,sn*25+(sk[0]+1)*5+sk[1]+1),pressed(T_STOP)),pressed(T_STOPS)),ui_lang);
    if(W_begin(W_ACT,R(64,STOP_Y,352,STOP_H+6),h)){
        for(int i=0;i<sn;i++) draw_stop_pill(stop_pill(sn,i),TR(STOP_BTN[sk[i]]),stop_font(sn),pressed(i?T_STOPS:T_STOP));
        W_end();
    }
}
static void draw_home_error(ms_t now){
    const HomeM*m=&hm; (void)now;
    if(W_begin(W_HERO,R(60,96,360,250),hi(hs(hs(H0,m->sub),m->hint1),pressed(T_DIAG)))){
        float cy=170; ring(CX,cy,44,47,C_RED,255);
        capsule(CX,cy-22,CX,cy+6,3.2f,C_TXT,255); disc(CX,cy+20,3.6f,C_TXT,255);
        char t[64]; snprintf(t,sizeof t,"%s",err_code()); text_c(CX,238,t,F_L,C_TXT,255);
        char s[96]; snprintf(s,sizeof s-4,"%s",m->sub); fit_text(s,F_S,330); text_c(CX,276,s,F_S,C_TXT2,255);
        text_c(CX,312,m->hint1,F_XS,C_TXT3,255);
        W_end();
    }
}
static void draw_home(ms_t now){
    const HomeM*m=&hm; uint32_t h;
    /* the chevron (SYSTEM) at the top */
    if(hm_mode!=HM_ACTIVE&&W_begin(W_GEAR,R(CX-30,HOME_CHEV_Y-6,60,20),hi(hi(H0,pressed(T_GEAR)),hm_mode))){
        draw_chev(CX,HOME_CHEV_Y,C_TXT,pressed(T_GEAR)?255:200,1.4f); W_end(); }
    /* the clock at the bottom (the wordmark while the clock is not set) */
    if(hm_mode!=HM_ACTIVE){ char c[24]; int ok=home_clock_text(c,sizeof c);
      if(W_begin(W_CLOCK,R(CX-70,HOME_CLOCK_Y-4,140,26),hs(hi(hi(H0,ok),hm_mode),ok?c:""))){
          if(ok) text_c(CX,HOME_CLOCK_Y,c,F_CLK,hm_mode==HM_DIM?C_TXT2:C_TXT,255); else brand_c(CX,HOME_CLOCK_Y,F_S,C_TXT2,255);
          W_end(); } }
    if(hm_mode==HM_ACTIVE){ draw_home_active(now); return; }
    if(hm_mode==HM_ERROR){ draw_home_error(now); return; }
    { char st[96]; int warn; uint32_t c=home_status_text(m,st,sizeof st-8,&warn,now);
      if(W_begin(W_CHIP,R(70,HOME_STAT_Y-4,340,22),hi(hi(hs(H0,st),(int)c),warn))){ draw_status_line(st,c,warn); W_end(); } }
    float hp=0; int hk=-1;
    if(tch.down&&!tch.moved&&tch.target>=T_ACT0&&tch.target<=T_ACT3){ hk=tch.target-T_ACT0; hp=clampf((now-tch.t0)/(float)HOLD_MS,0,1); }
    int dim=hm_mode==HM_DIM;
    for(int i=0;i<4;i++){
        int k=RING_ACT[i]; int en=!act_guard(k,now); int pr=(hk==k); float ht=ring_heat(i);
        float hpi=(pr&&en)?hp:0;
        h=hi(hi(hi(hi(hi(hs(H0,m->alab[k]),en),pr),(int)(hpi*60)),(int)(ht*200)),dim);
        if(W_begin(W_RING0+i,R(RING_X[i]-RING_R-4,RING_Y[i]-RING_R-4,2*RING_R+8,2*RING_R+8),h)){
            draw_ring(i,dim?1:ht,en,pr,hpi,m->alab[k],dim); W_end();
        }
    }
}

/* ================= page: SETTINGS LIST ================= */
/* HOME: discreet title line at the top inside the gauge ring — the wordmark w/ "+" badge */
/* v6.17: a page's title. Feature pages: the feature's icon in its colour + its name (the same icon as on HOME, so the page is
 * recognisably "the thing behind that button"); SYSTEM / DIAGNOSTICS: the name. No descriptive sub-line any more. */
static int title_font(const char*t){ return text_wf(t,F_L)<=TITLE_W?F_L:(text_wf(t,F_M)<=TITLE_W?F_M:F_S); }
static const char* page_title(int pg){ return pg==PG_DIAG?TR(TX_PG_DIAG):TR(TIER_NAME[clampi(tier_of_page(pg),0,4)]); }
static float page_title_w(int pg){ return text_wf(page_title(pg),title_font(page_title(pg))); }
static void title_c(int y,const char*t,uint32_t c){ char b[96]; snprintf(b,sizeof b-4,"%s",t); int f=title_font(b); if(f==F_S) fit_text(b,F_S,TITLE_W); text_c(CX,y+((int)FS[F_L].cap-(int)FS[f].cap)/2,b,f,c,255); }
static void static_page_title(void){ title_c(PG_TITLE_Y,page_title(page),C_TXT); }
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
            arc_aa(CX,CY,228,1.2f,-22,22,C_TXT,70);
            float vis=(list_vy1(page)-LIST_VY0)/content_h, span=44*vis, a0=-22+(44-span)*(scroll[page]/mx);
            arc_aa(CX,CY,228,1.6f,a0,a0+span,C_TXT,230);
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
    if(W_begin(W_BACKCH,R(CX-30,PG_CHEV_Y-8,60,24),hi(hi(H0,pr),page))){
        if(pr) rrect(CX-24,PG_CHEV_Y-7,48,22,11,C_TXT,40);
        draw_chev(CX,PG_CHEV_Y,C_TXT,pr?255:200,1.4f);
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
    int k=page-PG_GRIND; const char*g=act_guard(k,now); int en=!g, pr=pressed(T_START);
    float hp=(pr&&en)?clampf((now-tch.t0)/(float)HOLD_MS,0,1):0;
    Rect wr=R(CX-START_HIT_W/2,START_HIT_Y,START_HIT_W,START_HIT_H);
    if(W_begin(W_START,wr,hi(hi(hi(hi(hi(H0,k),en),pr),(int)(hp*60)),ui_lang))){
        draw_ring_btn(CX,START_CY,START_R,en,pr,hp);
        draw_play(CX+2,START_CY,13,C_TXT,en?255:110);
        text_c(CX,START_CAP_Y,TR(TX_B_START),F_XS,en?C_TXT2:C_DIS,255);
        W_end();
    }
}
static void draw_row_sep(int y){ hspan(y,84,396,C_TXT,46); }
static void draw_list(ms_t now){
    Row rw[ROW_MAX]; int n=rows_get(page,rw), vy1=list_vy1(page); int sc=(int)lroundf(scroll[page]); int prow=-1;
    draw_pback();
    { char lv[96]; uint32_t lc=page_live_text(page,lv,sizeof lv-8,now); int warn=lc==C_WARN; char b2[104];
      if(warn){ snprintf(b2,sizeof b2,IC_WARN " %s",lv); } else snprintf(b2,sizeof b2,"%s",lv); fit_text(b2,F_XS,LIVE_W);
      if(W_begin(W_LIVE,R(CX-LIVE_W/2-2,PG_LIVE_Y-3,LIVE_W+4,19),hi(hi(hs(H0,b2),(int)lc),page))){ text_c(CX,PG_LIVE_Y,b2,F_XS,lc,255); W_end(); } }
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
            if(page==PG_SYS&&rw[k].kind!=RK_SEC&&rw[k].kind!=RK_DIAG){ if(c0<0) c0=y; c1=y+ROW_H; }
            if(y+ROW_H<LIST_VY0||y>vy1) continue;
            int pr=(k==prow); int ty=y+(ROW_H-(int)FS[F_M].cap)/2;
            if(pr) rrect(76,y+3,328,ROW_H-6,12,C_TXT,34);
            if(k<n-1&&!(page==PG_SYS&&rw[k+1].kind==RK_SEC&&rw[k].kind!=RK_SEC&&0)) draw_row_sep(y+ROW_H);
            if(rw[k].kind==RK_SET){
                int i=rw[k].a; const Setting*s=&SET[i]; int lock=(s->flags&SF_LOCK)!=0;
                int oor=set_oor(s); const char*lab=TR(s->label); int indent=(page==PG_SYS)?14:0;
                text_at(88+indent,ty,lab,F_M,lock?C_TXT2:C_TXT,255);
                set_fmt(i,s->val,b,sizeof b-4); const char*u=set_unit(i);
                int xr=lock?398:378;
                if(*u&&s->kind==K_NUM){ int uw=text_w(u,F_XS); text_r(xr,ty+(int)FS[F_M].cap-(int)FS[F_XS].cap,u,F_XS,C_TXT2,255); xr-=uw+5; }
                int vf=F_MB, room=xr-(88+indent+text_w(lab,F_M)+12);
                if(text_w(b,vf)>room){ vf=F_S; if(text_w(b,vf)>room){ vf=F_XS; fit_text(b,F_XS,room); } }
                int vy=ty+(int)FS[F_M].cap-(int)FS[vf].cap;
                if(oor){ float iw=text_wf(IC_WARN,F_XS)+5; text_r(xr,vy,b,vf,C_ERR,255); text_at(xr-text_wf(b,vf)-iw,ty+2,IC_WARN,F_XS,C_ERR,255); }
                else text_r(xr,vy,b,vf,lock?C_TXT2:C_TXT,255);
                if(s->hint&&!lock&&!oor) text_r(xr-text_w(b,vf)-6,ty+3,"≈",F_XS,C_TXT2,255);
                if(!lock) draw_chev_r(394,y+ROW_H/2,C_TXT2);
            } else if(rw[k].kind==RK_SEC){
                int open=(rw[k].a==sec_open); char sm[40]; sec_summary(rw[k].a,sm,sizeof sm-4);
                text_at(88,ty,TR(SEC_NAME[rw[k].a]),F_M,C_TXT,255);
                if(sm[0]&&!open){ fit_text(sm,F_S,372-(88+text_w(TR(SEC_NAME[rw[k].a]),F_M)+14)); text_r(378,ty+(int)FS[F_M].cap-(int)FS[F_S].cap,sm,F_S,C_TXT2,255); }
                if(open) draw_chev(390,y+ROW_H/2-3,C_TXT,255,1.3f); else draw_chev_r(394,y+ROW_H/2,C_TXT2);
            } else if(rw[k].kind==RK_X){
                const char*label,*sub,*val; int lock; xrow_text(rw[k].a,&label,&sub,&val,&lock);
                text_at(102,y+9,label,F_M,lock?C_TXT2:C_TXT,255);
                { char sb[96]; snprintf(sb,sizeof sb-4,"%s",sub); fit_text(sb,F_TINY,val?200:270); text_at(102,y+31,sb,F_TINY,lock?C_WARN:C_TXT2,255); }
                if(val) text_r(lock?398:378,y+9,val,F_S,lock?C_TXT2:C_TXT,255);
                if(!lock) draw_chev_r(394,y+ROW_H/2,C_TXT2);
            } else {
                text_at(88,ty,TR(TX_PG_DIAG),F_M,C_TXT,255);
                text_r(378,ty+(int)FS[F_M].cap-(int)FS[F_S].cap,dgs,F_S,dgc==C_OK?C_TXT2:C_ERR,255);
                draw_chev_r(394,y+ROW_H/2,C_TXT2);
            }
        }
        if(c0>=0){ int ya=c0<LIST_VY0?LIST_VY0:c0, yb=c1>vy1?vy1:c1; if(yb>ya) fill_rect(88,ya+10,2,yb-ya-20>0?yb-ya-20:0,C_TXT,120); }
        edge_fade(vr,1,1);
        W_end();
    }
    draw_scroll_ind(n*ROW_P);
    if(pg_feat(page)) draw_start(now);
}

/* ================= page: EDITOR (v7.0: a roller like the stock pickers) =================
 * The value sits on a vertical roller: the selected value large and white in the middle, its neighbours above and below,
 * smaller and fading out. Drag the roller up/down (one step per ED_STEP_PX, it follows the finger), tap or hold −/+ (hold
 * repeats and accelerates, as before), OK saves (the same save path as before: MCU write + ACK, then config.json). The chevron
 * at the top or a pull down cancels. Locked rows and sensors show their value and an OK that just closes. */
static void detail_text(int i,char*l,size_t n){
    const Setting*s=&SET[i]; char a[24],b[24]; int lock=(s->flags&SF_LOCK)!=0, sens=(s->flags&SF_SENSOR)!=0;
    if(sens) snprintf(l,n,"%s",TR(TX_D_MEASURED));
    else if(lock) snprintf(l,n,"%s",TR(TX_D_NOEDIT));
    else if(s->kind==K_ENUM){ if(s->max-s->min==1) snprintf(l,n,"%s / %s",TR(s->opts[0]),TR(s->opts[1])); else snprintf(l,n,TR(TX_D_NOPTS),s->max-s->min+1); }
    else { const char*u=set_unit(i); set_fmt(i,s->lo,a,sizeof a); set_fmt(i,s->hi,b,sizeof b); snprintf(l,n,*u?"%s - %s %s":"%s - %s",a,b,u); }
}
static int detail_holdable(const Setting*s){ return s->kind==K_NUM&&!(s->flags&SF_LOCK)&&s->step>0&&(s->hi-s->lo)/s->step>=20; }
static void static_detail(void){
    const Setting*s=&SET[sel]; char l[72]; int lock=(s->flags&SF_LOCK)!=0;
    detail_text(sel,l,sizeof l);
    title_c(ED_TITLE_Y,TR(s->label),C_TXT);
    if(s->hint&&!lock){
        const char*hint=TR(s->hint);
        float w1=text_wf(l,F_XS), w2=text_wf(" · ",F_XS), w3=text_wf(hint,F_XS); float x=CX-(w1+w2+w3)/2;
        x=text_at(x,ED_RANGE_Y,l,F_XS,C_TXT2,255); x=text_at(x,ED_RANGE_Y," · ",F_XS,C_TXT2,255); text_at(x,ED_RANGE_Y,hint,F_XS,C_WARN,255);
    } else text_c(CX,ED_RANGE_Y,l,F_XS,C_TXT2,255);
    { char nt[96]; snprintf(nt,sizeof nt-4,"%s",TR(s->note)); fit_text(nt,F_XS,330); text_c(CX,ED_NOTE_Y,nt,F_XS,(lock&&!(s->flags&SF_SENSOR)&&s->mcu!=M_NONE)?C_WARN:C_TXT2,255); }
}
static uint32_t detail_status(char*l,size_t n){
    const Setting*s=&SET[sel]; char o[40]; int lock=(s->flags&SF_LOCK)!=0; l[0]=0;
    if(sav.active){ snprintf(l,n,"%s",TR(TX_D_SAVING)); return C_TXT; }
    if(!lock&&!set_valid(s,edit_val)){ snprintf(l,n,"%s",TR(TX_D_OOR)); return C_ERR; }
    if(edit_val!=edit_orig){ set_fmt(sel,edit_orig,o,sizeof o); snprintf(l,n,TR(TX_D_CHANGED),o,s->kind==K_NUM?set_unit(sel):""); return C_TXT; }
    if(sel==S_TZ&&edit_val>=0&&edit_val<TZ_N){ snprintf(l,n-4,"%s",TZT[edit_val].name); fit_text(l,F_XS,276); return C_TXT2; }
    if(detail_holdable(s)) snprintf(l,n,"%s",TR(TX_D_HOLDFAST));
    return C_TXT2;
}
static void draw_backch(void){
    int pr=pressed(T_BACK);
    if(W_begin(W_BACKCH,R(CX-30,PG_CHEV_Y-8,60,24),hi(H0,pr))){
        if(pr) rrect(CX-24,PG_CHEV_Y-7,48,22,11,C_TXT,40);
        draw_chev(CX,PG_CHEV_Y,C_TXT,pr?255:200,1.4f);
        W_end();
    }
}
/* the value k steps away from v on the editor's grid (clamped: returns 0 when out of range) */
#define ROLL_UP 114
#define ROLL_DN 106
static int roll_val(const Setting*s,int v,int k,int*out){
    if(s->kind==K_ENUM){ int r=v+k; if(r<s->min||r>s->max) return 0; *out=r; return 1; }
    int r=v+k*s->step; if(r<s->lo||r>s->hi) return 0; *out=r; return 1;
}
static void draw_roller(const Setting*s,int lock,int eoor){
    char b[40]; const char*u=set_unit(sel); int num=s->kind==K_NUM; float off=roll_off;
    for(int k=-2;k<=2;k++){
        int v; if(k==0) v=edit_val; else if(lock||!roll_val(s,edit_val,k,&v)) continue;
        set_fmt(sel,v,b,sizeof b-4);
        float yc=ED_CY+k*ED_STEP_PX+off; float dist=fabsf(yc-ED_CY)/ED_STEP_PX;
        int f=dist<0.5f?F_ROLL:F_ROLL2;
        if(f==F_ROLL&&text_wf(b,f)>(num?200:240)){ f=F_L; if(text_wf(b,f)>250) fit_text(b,f,250); }
        if(f==F_ROLL2&&text_wf(b,f)>230) fit_text(b,F_ROLL2,230);
        int a=(int)(255*clampf(1.1f-dist*0.45f,0.12f,1));
        uint32_t c=(k==0)?(eoor?C_ERR:(lock?C_TXT2:C_TXT)):C_TXT2;
        float nw=text_wf(b,f); int y=(int)lroundf(yc-FS[f].cap/2);
        float uw=(num&&*u&&k==0)?text_wf(u,F_MB)+8:0;
        text_at(CX-(nw+uw)/2,y,b,f,c,a);
        if(uw>0) text_at(CX-(nw+uw)/2+nw+8,y+(int)FS[f].cap-(int)FS[F_MB].cap,u,F_MB,c,a);
    }
}
static void draw_pm(int k,int en,int pr){
    int cx=k?ED_PX:ED_MX;
    draw_ring_btn(cx,ED_CY,ED_PM_R,en,pr,0);
    capsule(cx-10,ED_CY,cx+10,ED_CY,1.4f,C_TXT,en?255:110);
    if(k) capsule(cx,ED_CY-10,cx,ED_CY+10,1.4f,C_TXT,en?255:110);
}
static void draw_detail(ms_t now){
    (void)now; draw_backch(); const Setting*s=&SET[sel]; char l[112];
    int lock=(s->flags&SF_LOCK)!=0;
    int eoor=!lock&&!set_valid(s,edit_val);
    if(W_begin(W_DHERO,R(ED_ROLL_X0,ED_CY-ROLL_UP,ED_ROLL_X1-ED_ROLL_X0,ROLL_UP+ROLL_DN),hi(hi(hi(hi(H0,edit_val),eoor),(int)lroundf(roll_off*4)),sv(S_TUNIT)*2+ui_lang))){
        draw_roller(s,lock,eoor); edge_fade(R(ED_ROLL_X0,ED_CY-ROLL_UP,ED_ROLL_X1-ED_ROLL_X0,ROLL_UP+ROLL_DN),1,1); W_end();
    }
    { uint32_t stc=detail_status(l,sizeof l);
      if(W_begin(W_DCHG,R(80,ED_STAT_Y-3,320,20),hi(hs(H0,l),(int)stc))){ if(l[0]) text_c(CX,ED_STAT_Y,l,F_XS,stc,255); W_end(); } }
    for(int k=0;k<2&&!lock;k++){
        int t=k?T_PLUS:T_MINUS; int en=!sav.active&&(eoor||(k?edit_val<s->hi:edit_val>s->lo)); int pr=pressed(t);
        int cx=k?ED_PX:ED_MX;
        if(W_begin(k?W_PLUS:W_MINUS,R(cx-ED_PM_R-4,ED_CY-ED_PM_R-4,2*ED_PM_R+8,2*ED_PM_R+8),hi(hi(H0,en),pr))){ draw_pm(k,en,pr); W_end(); }
    }
    { int chg=edit_val!=edit_orig, pr=pressed(lock?T_CANCEL:T_SAVE);
      if(W_begin(W_SAVE,R(CX-ED_OK_R-10,ED_OK_Y-ED_OK_R-10,2*ED_OK_R+20,2*ED_OK_R+20),hi(hi(hi(hi(H0,pr),chg),sav.active),ui_lang))){
          if(chg&&!sav.active&&!lock) disc(CX,ED_OK_Y,ED_OK_R-1,C_TXT,255);
          draw_ring_btn(CX,ED_OK_Y,ED_OK_R,!sav.active,pr,0);
          const char*t=sav.active?"…":TR(TX_B_OK);
          text_c(CX,ED_OK_Y-(int)FS[F_M].cap/2,t,F_M,(chg&&!sav.active&&!lock)?C_ACCD:C_TXT,255);
          W_end(); } }
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
        float iw=text_wf(IC_WARN,F_L)+10, tw=text_wf(t.title,F_L); float x=CX-(iw+tw)/2;
        text_at(x,ED_TITLE_Y,IC_WARN,F_L,C_WARN,255); text_at(x+iw,ED_TITLE_Y,t.title,F_L,C_TXT,255);
    } else title_c(ED_TITLE_Y,t.title,C_TXT);
    { char sb[96]; snprintf(sb,sizeof sb-4,"%s",t.sub); fit_text(sb,F_XS,320); text_c(CX,ED_RANGE_Y,sb,F_XS,C_TXT2,255); }
}
static void draw_sys(ms_t now){
    (void)now; SysT t; sys_text(&t); int lock=sys_locked(); draw_backch();
    if(xsel==X_EXIT){
        if(W_begin(W_DHERO,R(56,120,368,130),hi(H0,xsel))){
            text_c(CX,128,TR(TX_Y_EXIT_L1),F_S,C_TXT,255);
            text_c(CX,154,TR(TX_Y_EXIT_L2),F_S,C_TXT,255);
            int w=text_w(TR(TX_Y_EXIT_CMD),F_M)+40;
            rrect_line(CX-w/2,190,w,42,21,1.2f,C_TXT,235);
            text_c(CX,190+(42-(int)FS[F_M].cap)/2,TR(TX_Y_EXIT_CMD),F_M,C_TXT,255);
            W_end();
        }
    } else {
        int f=sys_hero_font(t.hero);
        if(W_begin(W_DHERO,xsel==X_BOOTUI?R(ED_ROLL_X0,ED_CY-50,ED_ROLL_X1-ED_ROLL_X0,100):R(80,ED_CY-50,320,100),hi(hi(hs(H0,t.hero),lock),xsel))){
            text_c(CX,ED_CY-(int)FS[f].cap/2,t.hero,f,lock?C_TXT2:C_TXT,255);
            W_end();
        }
        for(int k=0;k<2&&xsel==X_BOOTUI&&!lock;k++){
            int tt=k?T_PLUS:T_MINUS; int en=(k?!x_edit:x_edit); int pr=pressed(tt); int cx=k?ED_PX:ED_MX;
            if(W_begin(k?W_PLUS:W_MINUS,R(cx-ED_PM_R-4,ED_CY-ED_PM_R-4,2*ED_PM_R+8,2*ED_PM_R+8),hi(hi(H0,en),pr))){ draw_pm(k,en,pr); W_end(); }
        }
    }
    if(W_begin(W_DCHG,R(60,ED_STAT_Y-3,360,20),hi(hs(H0,t.status?t.status:""),(int)t.c_status))){ if(t.status) text_c(CX,ED_STAT_Y,t.status,F_XS,t.c_status,255); W_end(); }
    if(W_begin(W_XNOTE,R(40,ED_NOTE_Y-3,400,20),hi(hs(H0,t.note),(int)t.c_note))){ char nb[110]; snprintf(nb,sizeof nb-4,"%s",t.note); fit_text(nb,F_XS,340); text_c(CX,ED_NOTE_Y,nb,F_XS,t.c_note,255); W_end(); }
    if(W_begin(W_CANCEL,lock?R(166,BTN_Y-6,148,BTN_H+12):R(BTN_CX0-4,BTN_Y-6,BTN_W+8,BTN_H+12),hi(hi(H0,pressed(T_CANCEL)),lock))){
        draw_pill_btn(lock?R(170,BTN_Y,140,BTN_H):R(BTN_CX0,BTN_Y,BTN_W,BTN_H),t.cancel,F_S,C_SURF,C_SURF,C_TXT,pressed(T_CANCEL)); W_end();
    }
    int act_ok=(xsel!=X_BOOTUI)||(x_edit!=x_orig);
    if(!lock&&W_begin(W_SAVE,R(BTN_CX1-4,BTN_Y-6,BTN_W+8,BTN_H+12),hi(hi(hs(H0,t.ok),pressed(T_SAVE)),act_ok))){
        if(act_ok) draw_pill_btn(R(BTN_CX1,BTN_Y,BTN_W,BTN_H),t.ok,text_wf(t.ok,F_S)>BTN_W-24?F_XS:F_S,C_TXT,C_TXT,C_ACCD,pressed(T_SAVE));
        else draw_pill_btn(R(BTN_CX1,BTN_Y,BTN_W,BTN_H),t.ok,F_S,C_SURF,C_SURF,C_DIS,0);
        W_end();
    }
    if(W_begin(W_XFOOT,R(84,414,312,36),hi(hs(hs(H0,t.l1),t.l2),(int)t.c_l1))){
        char a1[110],a2[110]; snprintf(a1,sizeof a1-4,"%s",t.l1); snprintf(a2,sizeof a2-4,"%s",t.l2?t.l2:""); fit_text(a1,F_TINY,300); fit_text(a2,F_TINY,270);
        text_c(CX,416,a1,F_TINY,t.c_l1==C_TXT3?C_TXT2:t.c_l1,255); text_c(CX,434,a2,F_TINY,C_TXT2,255); W_end();
    }
}
/* last frame before the runtime switch: stays on screen until the stock UI draws */
static const char* bye_sub(void){ return TR(bye_mode==SWM_CONT?TX_Y_BYE_CONT:TX_Y_BYE_START); }
static void draw_bye(void){
    if(W_begin(W_BYE,R(50,130,380,220),hi(hi(H0,bye_mode),ui_lang))){
        float iw=text_wf(IC_CUP,F_ICL); text_at(CX-iw/2,150,IC_CUP,F_ICL,C_TXT,255);
        text_c(CX,214,TR(TX_Y_BYE_T),F_L,C_TXT,255);
        text_c(CX,256,bye_sub(),F_XS,C_TXT2,255);
        text_c(CX,280,TR(TX_Y_BYE_CLOSED),F_XS,C_TXT2,255);
        W_end();
    }
}
/* ================= v6.14: TURNING OFF… / OFF / TURNING ON… (the power button) — v7.0: the dark layer, a white power ring ================= */
#define OFF_IC_Y 172
static void draw_off(ms_t now){
    (void)now; int st=pw.state;
    if(W_begin(W_OFF,R(50,110,380,220),hi(hi(H0,st),ui_lang))){
        int a=st==PW_OFF?140:255;
        arc_aa(CX,OFF_IC_Y,32,1.8f,-50,230,C_TXT,a);
        capsule(CX,OFF_IC_Y-42,CX,OFF_IC_Y-8,1.8f,C_TXT,a);
        text_c(CX,236,TR(st==PW_OFFING?TX_PW_OFFING:(st==PW_WAKING?TX_PW_WAKING:TX_PW_OFF)),F_L,st==PW_OFF?C_TXT2:C_TXT,255);
        text_c(CX,276,TR(st==PW_OFFING?TX_PW_SUB_OFFING:(st==PW_WAKING?TX_PW_SUB_WAKING:TX_PW_SUB_OFF)),F_XS,C_TXT2,255);
        if(st==PW_OFF) text_c(CX,298,TR(TX_PW_NOTE_OFF),F_XS,rgb(200,170,150),255);
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
    if(W_begin(W_SPL,R(CX-SPLASH_W/2,130,SPLASH_W,210),hi(hi(H0,q),ui_lang))){
        int a=(int)(255*clampf(t*2.5f,0,1));
        float y=6*(1-ease_out(clampf(t*2.2f,0,1)));
        float iw=text_wf(IC_BEAN,F_ICL); text_at(CX-iw/2,140+(int)y,IC_BEAN,F_ICL,C_TXT,a);
        brand_c(CX,204+(int)y,F_H,C_TXT,a);
        int a2=(int)(255*clampf(t*3-0.7f,0,1)); char b[96];
        text_c(CX,262,TR(TX_TAGLINE),F_XS,C_TXT2,a2);
        splash_status(b,sizeof b); text_c(CX,286,b,F_TINY,cfg_ok?C_TXT2:C_WARN,a2);
        W_end();
    }
}

/* ================= composition ================= */
static void static_build(void){
    memcpy(bgbase,bgpure,(size_t)W*H*4);
    cv=bgbase; set_clip(FULL);
    if(view==V_OFF) memcpy(bgbase,bgdark,(size_t)W*H*4);             /* v6.14: the OFF page (v7.0: the dark layer; the backlight goes off anyway) */
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

