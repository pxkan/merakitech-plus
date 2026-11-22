/* src/mp/sim_hal.c — SIM HAL (sim/CONTRACT.md): sandbox, touch FIFO, state JSON, ctrl socket + test hooks.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
#ifdef SIM_RT
/* ====================================================================== */
/* ================= SIM-HAL (v6.4 · sim/CONTRACT.md) =================== */
/* ====================================================================== */
/* Everything from here to #endif exists ONLY in the SIM build. It replaces the device's three file nodes
 * (fb0, ttyS1, event0) and adds a test surface on top — the UI, protocol, save and drawing code
 * is the same as on the device:
 *   MERAKIPLUS_SIM_TTY    pty slave instead of /dev/ttyS1              (uart_open: UART_DEV)
 *   MERAKIPLUS_SIM_FB     fb buffer -> file (tmp+rename, <= 10 Hz + on change)
 *   MERAKIPLUS_SIM_TOUCH  FIFO w/ raw 16-byte evdev records; played back according to the records' timestamps
 *   MERAKIPLUS_SIM_STATE  state JSONL (one line per rendered frame whose content changed; at least ~10 Hz)
 *   MERAKIPLUS_SIM_CTRL   unix socket w/ JSONL hooks (ping, fb_dump, exit, action, save, page, touch,
 *                    boot_flag, runtime_switch, raw_write, config_reload, state_now, layout)
 *   MERAKIPLUS_SIM_ROOT   sandbox root for /customer /new_customer /proc /tmp (required)
 * The hooks call the SAME functions a tap would (act_request, save_begin, sys_confirm …);
 * they bypass neither guards nor frame policy. The only exception is raw_write (stock emulation),
 * see sim_raw_push(). Nothing is written outside the sandbox besides the paths the runner itself designates. */
static char sim_fb_path[300]="", sim_state_path[300]="", sim_touch_path[300]="", sim_ctrl_path[300]="";
static FILE*sim_sf=NULL;
static ms_t sim_t0=0;
static int sim_tfd_w=-1, sim_lfd=-1, sim_bp_guard=0;

/* the runner does not read stderr continuously (pipe): never a stream of error lines */
static void sim_perr(const char*s){ static int n; if(n<3){ n++; perror(s); } }
static void sim_die(int rc,const char*fmt,...){
    va_list ap; va_start(ap,fmt); fputs("merakiplus-sim: ",stderr); vfprintf(stderr,fmt,ap); va_end(ap); fputc('\n',stderr); exit(rc);
}

/* ---- sandbox ---- */
static void sim_mkparents(const char*path){
    char b[460]; snprintf(b,sizeof b,"%s",path);
    for(char*q=b+1;*q;q++) if(*q=='/'){ *q=0; mkdir(b,0755); *q='/'; }
}
static void sim_put(const char*rel,const char*data,int mode){   /* creates ONLY if the path is missing */
    char p[460]; snprintf(p,sizeof p,"%s%s",sysroot,rel); struct stat st; if(lstat(p,&st)==0) return;
    sim_mkparents(p); int fd=open(p,O_WRONLY|O_CREAT|O_EXCL,mode); if(fd<0) return;
    int n=(int)strlen(data); if(n&&write(fd,data,n)!=n){} close(fd); chmod(p,mode);
}
/* The runner's sandbox (contract §8) has /customer, /new_customer/merakiplus/boot and three /proc files.
 * The BOOT UI guard and SWITCH NOW read more than that: /proc/mounts (is slot B mounted?), slot B's
 * demo_v9 (is it merakiplus?), slot A's demo_v9 (is there a stock UI?) and /proc/self/exe. Without them
 * both would be LOCKED. Here whatever is missing is filled in — a slot B boot, like the preview's
 * mock_boot_b(). Nothing existing is overwritten; MERAKIPLUS_SIM_NOPROVISION=1 skips it. */
static void sim_provision(void){
    char mk[200], p[460]; struct stat st;
    sim_put("/proc/mounts","rootfs / rootfs rw 0 0\nmock-a /customer mockfs rw 0 0\nmock-b " SLOT_B_MNT " mockfs rw 0 0\n",0644);
    sim_put("/proc/sys/kernel/random/boot_id","6f757275-6973-4000-8000-73696d626f6f\n",0644);
    snprintf(mk,sizeof mk,"#!/bin/sh\n# SIM mock of slot B's demo_v9: %s\nexit 0\n",MERAKIPLUS_MARK); sim_put(SLOT_B_EXE,mk,0755);
    sim_put(SLOT_A_EXE,"#!/bin/sh\n# SIM mock of the stock UI (slot A)\nexec sleep 3\n",0755);
    snprintf(p,sizeof p,"%s/proc/self/exe",sysroot);
    if(lstat(p,&st)!=0){ sim_mkparents(p); if(symlink(SLOT_B_EXE,p)!=0){} }
}
static void sim_env_path(const char*name,char*dst,size_t n,int required){
    const char*e=getenv(name);
    if(!e||!*e){ if(required) sim_die(2,"%s missing (required in the SIM build, see sim/CONTRACT.md §2)",name); return; }
    if(strlen(e)>=n) sim_die(2,"%s is too long (%d chars, max %d)",name,(int)strlen(e),(int)n-1);
    snprintf(dst,n,"%s",e);
}
/* v6.16: "YYYY-MM-DDTHH:MM:SSZ" (UTC) -> seconds; -1 = malformed */
static int64_t sim_parse_utc(const char*e){
    int Y,M,D,h,m,sec; if(sscanf(e,"%d-%d-%dT%d:%d:%d",&Y,&M,&D,&h,&m,&sec)!=6||M<1||M>12||D<1||D>31||h<0||h>23||m<0||m>59||sec<0||sec>59) return -1;
    return days_civil(Y,M,D)*86400+h*3600LL+m*60LL+sec;
}
static void sim_env(void){
    const char*e; struct stat st;
    sim_t0=now_ms();
    signal(SIGPIPE,SIG_IGN);
    if((e=getenv("MERAKIPLUS_SIM_ROOT"))&&*e){ if(strlen(e)>=sizeof sysroot) sim_die(2,"MERAKIPLUS_SIM_ROOT is too long"); snprintf(sysroot,sizeof sysroot,"%s",e); }
    else if((e=getenv("MERAKIPLUS_ROOT"))&&strlen(e)>=sizeof sysroot) sim_die(2,"MERAKIPLUS_ROOT is too long");
    size_t L=strlen(sysroot); while(L>1&&sysroot[L-1]=='/') sysroot[--L]=0;
    if(!sysroot[0]||!strcmp(sysroot,"/")) sim_die(2,"MERAKIPLUS_SIM_ROOT missing: the SIM build must never touch the host's /customer, /new_customer, /proc or /tmp");
    if(stat(sysroot,&st)!=0||!S_ISDIR(st.st_mode)) sim_die(2,"sandbox root does not exist: %s",sysroot);
    /* paths without their own env follow the root (on the device the language file does NOT follow MERAKIPLUS_ROOT — unchanged) */
    if((e=getenv("MERAKIPLUS_CONFIG"))){ if(strlen(e)>=sizeof cfg_path) sim_die(2,"MERAKIPLUS_CONFIG is too long"); } else snprintf(cfg_path,sizeof cfg_path,"%s/customer/config.json",sysroot);
    if((e=getenv("MERAKIPLUS_BAKDIR"))){ if(strlen(e)>=sizeof cfg_bakdir) sim_die(2,"MERAKIPLUS_BAKDIR is too long"); } else snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/new_customer/merakiplus/cfgbak",sysroot);
    if((e=getenv("MERAKIPLUS_LANGFILE"))){ if(strlen(e)>=sizeof lang_path) sim_die(2,"MERAKIPLUS_LANGFILE is too long"); } else snprintf(lang_path,sizeof lang_path,"%s/new_customer/merakiplus/lang",sysroot);
    if((e=getenv("MERAKIPLUS_SCHEDFILE"))){ if(strlen(e)>=sizeof sched_path) sim_die(2,"MERAKIPLUS_SCHEDFILE is too long"); snprintf(sched_path,sizeof sched_path,"%s",e); }
    else snprintf(sched_path,sizeof sched_path,"%s/new_customer/merakiplus/schedule",sysroot);   /* v6.16 */
    if((e=getenv("MERAKIPLUS_SOUNDFILE"))){ if(strlen(e)>=sizeof snd_path) sim_die(2,"MERAKIPLUS_SOUNDFILE is too long"); snprintf(snd_path,sizeof snd_path,"%s",e); }
    else snprintf(snd_path,sizeof snd_path,"%s/new_customer/merakiplus/sound",sysroot);         /* v6.18 */
    if((e=getenv("MERAKIPLUS_SIM_CLOCK"))&&*e){ int64_t u=sim_parse_utc(e); if(u<0) sim_die(2,"MERAKIPLUS_SIM_CLOCK: expected YYYY-MM-DDTHH:MM:SSZ"); clk_off=u-(int64_t)time(NULL); }   /* v6.16: the wall clock (UTC) */
    sim_env_path("MERAKIPLUS_SIM_TTY",sim_tty,sizeof sim_tty,1);
    sim_env_path("MERAKIPLUS_SIM_FB",sim_fb_path,sizeof sim_fb_path-8,1);
    sim_env_path("MERAKIPLUS_SIM_STATE",sim_state_path,sizeof sim_state_path,1);
    sim_env_path("MERAKIPLUS_SIM_TOUCH",sim_touch_path,sizeof sim_touch_path,0);
    sim_env_path("MERAKIPLUS_SIM_CTRL",sim_ctrl_path,sizeof sim_ctrl_path,0);
    bl_enabled=0;                                          /* the backlight sysfs is never written in SIM */
    if(getenv("MERAKIPLUS_SIM_NOBOOTPUSH")) bp_on=0;
    if(getenv("MERAKIPLUS_SIM_BOOTPUSH_GUARD")) sim_bp_guard=1;   /* v6.5: the device's activity guard (skip instead of defer) */
    bp_guard_cfg=sim_bp_guard;                             /* v6.6: a re-push uses the same guard choice as the boot push */
    if((e=getenv("MERAKIPLUS_SIM_UPTIME_MS"))&&*e){ sim_clk_off=0; sim_clk_off=strtoll(e,NULL,0)-(int64_t)now_ms(); sim_t0=now_ms(); }   /* v6.6: the clock starts at a chosen value (wraparound test) */
    if(!getenv("MERAKIPLUS_SIM_NOPROVISION")) sim_provision();
    /* v6.7: the wifi MODEL. Default = healthy stack (like runtime B: stock's wpa_supplicant is alive); "cold" = slot B cold start
     * (no wlan0, no supplicant, no IP); "off" = the watchdog disabled. The log lives in the sandbox's /tmp. */
    { e=getenv("MERAKIPLUS_SIM_NET"); nm_reset(e&&!strcmp(e,"cold")); sim_net_on=!(e&&!strcmp(e,"off")); mkdir(rp("/tmp"),0755); mkdir(rp("/appconfigs"),0755); }
}

/* ---- touch: FIFO w/ raw evdev records -> timed queue -> inp_poll() (same decoding as the device) ----
 * The runner writes a whole gesture at once; the duration lies in the records' timestamps
 * (contract §4). The queue therefore delivers each record once its timestamp offset from the gesture's
 * first record has elapsed on the wall clock — so hold-to-start (650 ms) is measured as on the device. */
#define SIM_EVQ 4096
static struct { struct input_event ev; ms_t due; } sim_evq[SIM_EVQ];
static int sim_evh=0, sim_evn=0;
static uint8_t sim_tb[16]; static int sim_tbn=0;
static ms_t sim_ev_bw=0, sim_ev_be=0;                      /* anchor: wall time / record time of the gesture's first record */
static ms_t sim_ev_last(void){ return sim_evn? sim_evq[(sim_evh+sim_evn-1)%SIM_EVQ].due : 0; }
static void sim_ev_push(uint16_t type,uint16_t code,int32_t value,ms_t due){
    if(sim_evn>=SIM_EVQ) return;
    struct input_event ev={0,0,type,code,value}; int k=(sim_evh+sim_evn)%SIM_EVQ; sim_evq[k].ev=ev; sim_evq[k].due=due; sim_evn++;
}
static void sim_touch_read(ms_t now){
    if(ifd<0||!sim_touch_path[0]) return;
    uint8_t b[2048]; int n;
    while((n=(int)read(ifd,b,sizeof b))>0){
        for(int i=0;i<n;i++){
            sim_tb[sim_tbn++]=b[i]; if(sim_tbn<16) continue;
            sim_tbn=0; struct input_event ev; memcpy(&ev,sim_tb,16);
            ms_t ems=(ms_t)ev.sec*1000+ev.usec/1000, due=sim_ev_bw+(ems-sim_ev_be);
            if(sim_evn==0||ems<sim_ev_be||due<now-50||due>now+120000){ sim_ev_bw=now; sim_ev_be=ems; due=now; }   /* new gesture: new anchor */
            if(due<sim_ev_last()) due=sim_ev_last();
            sim_ev_push(ev.type,ev.code,ev.value,due);
        }
    }
}
static int sim_ev_next(struct input_event*ev,ms_t now){
    sim_touch_read(now);
    if(!sim_evn||sim_evq[sim_evh].due>now) return 0;
    *ev=sim_evq[sim_evh].ev; sim_evh=(sim_evh+1)%SIM_EVQ; sim_evn--; return 1;
}
/* ctrl "touch": same record sequence as tinj_hold, put in the same queue (down now, up after ms) */
static void sim_touch_inject(int x,int y,int ms,ms_t now){
    int rx=clampi((x*4096+479)/480,0,4095), ry=clampi((y*4096+479)/480,0,4095);   /* rounded up: scalex() returns exactly x */
    ms_t due=now>sim_ev_last()?now:sim_ev_last()+1; if(ms<1) ms=1;
    sim_ev_push(EV_ABS,ABS_MT_TRACKING_ID,1,due); sim_ev_push(EV_ABS,ABS_MT_POSITION_X,rx,due); sim_ev_push(EV_ABS,ABS_MT_POSITION_Y,ry,due);
    sim_ev_push(EV_ABS,ABS_X,rx,due); sim_ev_push(EV_ABS,ABS_Y,ry,due); sim_ev_push(EV_KEY,BTN_TOUCH,1,due); sim_ev_push(EV_SYN,SYN_REPORT,0,due);
    sim_ev_push(EV_ABS,ABS_MT_TRACKING_ID,-1,due+ms); sim_ev_push(EV_KEY,BTN_TOUCH,0,due+ms); sim_ev_push(EV_SYN,SYN_REPORT,0,due+ms);
}
/* no EVIOCGRAB and no EVIOCGABS in SIM: the raw range is 0..4095 (screen = raw*480/4096) */
static void inp_open(void){
    struct stat st;
    if(!sim_touch_path[0]){ ifd=open("/dev/null",O_RDONLY); return; }      /* no FIFO: only ctrl "touch" */
    if(stat(sim_touch_path,&st)!=0&&mkfifo(sim_touch_path,0600)!=0) sim_die(4,"mkfifo %s: %s",sim_touch_path,strerror(errno));
    ifd=open(sim_touch_path,O_RDONLY|O_NONBLOCK);
    if(ifd<0) sim_die(4,"open touch %s: %s",sim_touch_path,strerror(errno));
    sim_tfd_w=open(sim_touch_path,O_WRONLY|O_NONBLOCK);    /* own write end: no EOF when the runner closes between injections */
}
static void dev_release_impl(void){                        /* BOOT.md §9.2 step 1 in SIM: UART + touch are released (fb is memory) */
    uart_flush();
    if(ufd>=0){ tcdrain(ufd); close(ufd); ufd=-1; }
    if(ifd>=0){ close(ifd); ifd=-1; }
    if(sim_tfd_w>=0){ close(sim_tfd_w); sim_tfd_w=-1; }
}
static void dev_reacquire_impl(void){
    uart_open(); inp_open();
    RL full; full.n=1; full.r[0]=FULL; pres_prev=full;
}

/* ---- JSON out ---- */
typedef struct { char*p,*e; } JB;
static void jb(JB*j,const char*fmt,...){
    if(j->p>=j->e) return;
    va_list ap; va_start(ap,fmt); int n=vsnprintf(j->p,(size_t)(j->e-j->p),fmt,ap); va_end(ap);
    if(n>0) j->p+=(n<j->e-j->p)?n:(j->e-j->p);
}
/* JSON string of a UI text: font switches (LC/UC) are removed, icon codes are written as \u00XX */
static void jb_s(JB*j,const char*s){
    jb(j,"\"");
    for(;s&&*s;s++){ unsigned char c=(unsigned char)*s;
        if(c==0x0e||c==0x0f) continue;
        if(c=='"'||c=='\\') jb(j,"\\%c",c); else if(c<0x20) jb(j,"\\u%04x",c); else if(j->p<j->e) *j->p++=(char)c; }
    jb(j,"\"");
}

/* ---- state that exists only for the state JSON ---- */
static const char*const PH_KEY[NPHASE]={"OFFLINE","SLEEP","ERROR","HEAT","READY","GRIND","PRE","EXT","WATER","STEAM","DONE"};
static const char*const ACT_KEY[4]={"grind","brew","water","steam"};
static const char*const STOP_KEY[4]={"grind_stop","brew_stop","water_stop","steam_stop"};   /* v6.13: ctrl action + layout names */
/* parity_spec id -> SET[] row (sim_e2e.py checks the table against parity_spec.json + --dump-table) */
static const struct { const char*id; int idx; } SIM_PARAM[]={
 {"grind_weight",S_GRINDW},{"grind_mode",S_GRINDMODE},{"brew_weight",S_BREWW},{"brew_temp",S_BREWT},
 {"preinfusion_switch",S_PISW},{"preinfusion_time",S_PITIME},{"preinfusion_pressure",S_PIPRESS},{"break_time",S_BREAK},
 {"head_temp",S_HEADT},{"steam_milk_target",S_STEAMT},{"steam_gear",S_STEAMG},{"hotwater_weight",S_HOTW},
 {"hotwater_mode",S_HOTMODE},{"hotwater_temp",S_HOTT},{"ui_language",S_UILANG},{"backlight",S_BL},{"volume",S_VOL},
 {"temp_unit",S_TUNIT},{"language",S_LANG},{"hotwater_shortcut",S_HOTSC},{"sumtime",S_DST},{"cleartip",S_CLEARTIP},
 {"timemode",S_TIMEMODE},{"automatically",S_AUTOTIME},{"timezone",S_TZ},
 {"preheat",S_PREHEAT},{"preheat_at",S_PHAT},{"preheat_repeat",S_PHREP},{"auto_off",S_AUTOOFF},   /* v6.16: merakiplus's own (schedule file) */
 {"sound",S_SOUND},{"ready_tone",S_SNDRDY}};                                                         /* v6.18: merakiplus's own (sound file) */
#define SIM_NPARAM ((int)(sizeof SIM_PARAM/sizeof SIM_PARAM[0]))
static int sim_param_idx(const char*id){
    for(int k=0;k<SIM_NPARAM;k++) if(!strcmp(SIM_PARAM[k].id,id)) return SIM_PARAM[k].idx;
    for(int i=0;i<NSET;i++) if(SET[i].key&&!strcmp(SET[i].key,id)) return i;               /* the config key works too */
    return -1;
}
static const char* sim_param_id(int idx){ for(int k=0;k<SIM_NPARAM;k++) if(SIM_PARAM[k].idx==idx) return SIM_PARAM[k].id; return "?"; }

/* toast: which string id was shown? (TR() returns the table's own pointer) */
static struct { int id; uint32_t col; } sim_tst={-1,0};
static void sim_toast(const char*m,uint32_t col){
    sim_tst.id=-1; sim_tst.col=col;
    for(int l=0;l<NLANG&&sim_tst.id<0;l++){ int lg=(ui_lang+l)%NLANG; for(int i=1;i<NSTR;i++) if(STR[lg][i]==m){ sim_tst.id=i; break; } }
}
/* latest SAVE (hook or tap): pending -> ok/fail; rejected/unchanged is set by the hook */
enum { SSV_NONE=0, SSV_PENDING, SSV_OK, SSV_FAIL, SSV_REJECTED, SSV_UNCHANGED };
static const char*const SSV_KEY[]={"none","pending","ok","fail","rejected","unchanged"};
static struct { int state, idx, val; char param[40]; } ssave;
static void sim_save_track(void){
    if(sav.active&&!(ssave.state==SSV_PENDING&&ssave.idx==sav.idx&&ssave.val==sav.val)){
        ssave.state=SSV_PENDING; ssave.idx=sav.idx; ssave.val=sav.val; snprintf(ssave.param,sizeof ssave.param,"%s",sim_param_id(sav.idx));
    }
    if(ssave.state==SSV_PENDING&&!sav.active){
        ssave.state=(SET[ssave.idx].val==ssave.val)?SSV_OK:SSV_FAIL;   /* since v6.5 the boot-push retry after a SAVE lives in save_step() (all build types) */
    }
}

/* ---- fb -> file (tmp + rename: the file is always one whole frame) ---- */
static long sim_fb_dumped=-1; static ms_t sim_fb_last=-100000;
static int sim_fb_write(const char*path){
    char tmp[460]; snprintf(tmp,sizeof tmp,"%s.tmp",path);
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd<0) return 0;
    const char*d=(const char*)fbmem; long off=0, tot=(long)W*H*4;
    while(off<tot){ long k=write(fd,d+off,(size_t)(tot-off)); if(k<=0){ close(fd); unlink(tmp); return 0; } off+=k; }
    close(fd);
    if(rename(tmp,path)!=0){ unlink(tmp); return 0; }
    return 1;
}

/* ---- state JSONL (contract §3 + addendum, see sim/CONTRACT.md "ADDENDUM v6.4") ---- */
static long sim_frames=0; static ms_t sim_state_last=-100000; static double sim_ft_sum=0; static float sim_ft_win=0, sim_ft_peak=0;
static uint32_t sim_sig=0;
static int sim_bye_on=0; static ms_t sim_bye_until=0;
static const char* sim_page_key(void){
    if(view==V_BYE) return "bye"; if(view==V_DETAIL) return "detail"; if(view==V_SYS) return "sys"; if(view==V_OFF) return "off";
    if(view==V_SPLASH||page==PG_HOME) return "home";
    return page==PG_DIAG?"diag":"settings";
}
static const char* sim_bpr(int r){ static const char*const k[]={"none","ok","noack","blocked","nocfg","skipped"}; return k[r]; }
static void sim_state_emit(ms_t now,int force){   /* force=0: only if the line's content changed, or 90 ms have passed */
    if(!sim_sf) return;
    static char buf[12288]; JB j={buf,buf+sizeof buf-4};
    static const char*const VW[]={"splash","main","detail","sys","bye","off"};
    static const char*const TAB[]={"home","grind","brew","water","steam","system","diag"};   /* v6.17: one page per feature + SYSTEM (v6.4-v6.16: home/brew/steam/system/diag) */
    static const short SHOWN[]={0,1,2,3,4,6,9,11,14,16,17,19,22,23,24,25,33,34,35,36,37,38,39,40,41,42,44,46,47,48,50,51,52,58,62,63,112,113,114};   /* v6.6: + 44, 46 (the wand switch); v6.18: + 62, 63 (heat stages) */
    int pr=phase_raw(now); struct stat st; char rn[340], stamp[160]="", a[40], b[40];
    static uint32_t last_h; jb(&j,"\"client\":\"merakiplus-sim\",\"version\":\"" MERAKIPLUS_VERSION "\"");
    jb(&j,",\"view\":\"%s\",\"view_raw\":\"%s\",\"page\":\"%s\",\"tab\":\"%s\"",view==V_BYE?"bye":"main",VW[view],sim_page_key(),TAB[page]);
    jb(&j,",\"phase\":\"%s\",\"phase_ui\":\"%s\",\"work_sta\":%u,\"link_up\":%s",PH_KEY[pr],PH_KEY[act.ph],reg[33],link_up(now)?"true":"false");
    /* v6.6: phase = the contract's phase_raw() (steam first, unchanged); phase_main = HOME's main activity; steam = the steam path */
    { static const char*const SKK[]={"none","unknown","auto","wand"}; int on=link_up(now)&&steam_bits(), pm=phase_main(now);
      jb(&j,",\"phase_main\":\"%s\",\"phases\":[",PH_KEY[pm]);
      if(phase_active(pm)&&pm!=PH_STEAM) jb(&j,"\"%s\"%s",PH_KEY[pm],on?",":""); if(on) jb(&j,"\"STEAM\""); if(!phase_active(pm)) jb(&j,"%s\"%s\"",on?",":"",PH_KEY[pm]);
      jb(&j,"],\"steam\":{\"on\":%s,\"kind\":\"%s\",\"last_kind\":\"%s\",\"concurrent\":%s,\"wand_sw\":%s,\"wand_seen\":%s,\"ord24\":%u,\"target\":%d,\"milk\":%u,\"stop_btn\":%s}",
         on?"true":"false",SKK[stm.kind],SKK[stm.last_kind],(on&&phase_active(pm)&&pm!=PH_STEAM)?"true":"false",wand_sw()?"true":"false",stm.wand?"true":"false",reg[24],steam_target(),reg[48],steam_stop_visible()?"true":"false"); }
    { int sk[2], sn=link_up(now)?stop_btns(now,sk):0;               /* v6.13: the STOP buttons (left -> right) + latest stop request */
      jb(&j,",\"stop\":{\"btns\":["); for(int i=0;i<sn;i++) jb(&j,"%s\"%s\"",i?",":"",ACT_KEY[sk[i]]);
      jb(&j,"],\"pending\":"); if(stp.active) jb(&j,"\"%s\"",ACT_KEY[stp.kind]); else jb(&j,"null");
      jb(&j,",\"last\":"); if(stp.n_req) jb(&j,"\"%s\"",ACT_KEY[stp.kind]); else jb(&j,"null");
      jb(&j,",\"req\":%ld,\"ok\":%ld,\"fail\":%ld}",stp.n_req,stp.n_ok,stp.n_fail); }
    jb(&j,",\"regs\":{"); for(unsigned k=0;k<sizeof SHOWN/sizeof SHOWN[0];k++) jb(&j,"%s\"%d\":%u",k?",":"",SHOWN[k],reg[SHOWN[k]]);
    jb(&j,"},\"regs_s\":{\"38\":%d,\"39\":%d}",s16(38),s16(39));
    /* v6.18 (addendum): the sound engine — events by name with their outcome; log = newest first "event:outcome" */
    { static const char*const MK[]={"off","on","stock"};
      jb(&j,",\"audio\":{\"mode\":\"%s\",\"ready_mask\":%d,\"vol\":%d,\"machine\":%d,\"armed\":[%d,%d],\"ready\":[%d,%d],\"cold_boot\":%s",
         MK[clampi(SET[S_SOUND].val,0,2)],SET[S_SNDRDY].val,snd_vol(),snd.st,snd.armed[0],snd.armed[1],snd.ready[0],snd.ready[1],snd.cold_boot?"true":"false");
      for(int e=0;e<NSE;e++) jb(&j,",\"n_%s\":%d,\"played_%s\":%d",SE_KEY[e],snd.n[e],SE_KEY[e],snd.n_played[e]);
      jb(&j,",\"n_suppressed\":%d,\"n_muted\":%d,\"n_off\":%d,\"log\":\"",snd.n_out[SO_SUPPRESSED],snd.n_out[SO_MUTED],snd.n_out[SO_OFF]);
      for(int k=snd.log_n-1;k>=0;k--) jb(&j,"%s%s:%s",k==snd.log_n-1?"":",",SE_KEY[(int)snd.log[k].ev],SO_KEY[(int)snd.log[k].out]);
      jb(&j,"\",\"file\":"); jb_s(&j,snd.last_file); jb(&j,",\"last\":"); if(snd.log_n) jb(&j,"\"%s\"",SE_KEY[(int)snd.log[0].ev]); else jb(&j,"null"); jb(&j,"}"); }
    /* v6.9 (addendum): display conditioning + latest chart scale (display only — regs above are raw) */
    jb(&j,",\"cond\":{\"raw\":%s,\"a_chart\":%.3f,\"a_hero\":%.3f,\"med\":%d,\"scale\":\"%s\",\"pct\":%d,\"hold\":%d,\"head\":%.2f,\"hero_snaps\":%d,\"wdb\":%.1f,\"gate\":%d}",
       cd.raw?"true":"false",cd.a_chart,cd.a_hero,cd.med,CS_KEY[cd.scale],cd.pct,cd.hold,cd.head,hf.n_snap,(double)CD_WDB,cd.gate);
    /* v6.9: the weight source (latch) and the displayed weight (0.1 g, signed, conditioned + deadband) — regs/regs_s are raw */
    jb(&j,",\"weight\":{\"src\":%d,\"switches\":%d,\"shown\":%d}",wsl.src,wsl.n_sw,w_disp(wsl.src,now));
    { float pb=p_bar((float)reg[51]);                       /* v6.10: the pressure estimate (display only; regs are raw) — bar = null above P_EST_MAX (pulse) */
      jb(&j,",\"press\":{\"raw\":%u,\"k\":%.6f,\"anchor_bar\":%d,\"p0\":%d,\"max_bar\":%.1f",reg[51],(double)p_k(),p_anchor_bar(),P_ANCHOR_RAW,(double)P_EST_MAX);
      if(pb<0.f) jb(&j,",\"bar\":null,\"pulse\":true}"); else jb(&j,",\"bar\":%.2f,\"pulse\":false}",(double)pb); }
    jb(&j,",\"chart\":{\"mode\":%d,\"active\":%s,\"full\":%d,\"top\":%d,\"raw_max\":%d,\"disp_max\":%d,\"ticks\":%d,\"base_ticks\":%d,\"clipped\":%d,\"n\":%d,\"n_active\":%d",
       cst.mode,cst.active?"true":"false",cst.full,cst.top,cst.raw_max,cst.disp_max,cst.ticks,cst.bticks,cst.clip,cst.n,cst.na);
    /* v6.15: graph reset on activity start — buf = samples in the ring, w_max/w_top = the weight line's max + axis top (mode 0),
     * resets/reanchors = counters, act_ms = ms since the current curve started (-1 = none since start) */
    jb(&j,",\"buf\":%d,\"w_max\":%d,\"w_top\":%d,\"resets\":%d,\"reanchors\":%d,\"act_ms\":%lld}",
       g_n,cst.wmax,cst.wtop,gr.resets,gr.reanchors,(long long)(gr.resets?now-gr.t0:-1));
    /* what the screen shows */
    jb(&j,",\"shown\":{");
    if(view==V_MAIN&&page==PG_HOME){
        jb(&j,"\"chip\":"); jb_s(&j,hm.chip?hm.chip:ph_name(act.ph)); jb(&j,",\"badge\":"); if(hm.badge) jb_s(&j,hm.badge); else jb(&j,"null"); jb(&j,",\"hero\":"); jb_s(&j,hm.hero); jb(&j,",\"unit\":"); jb_s(&j,hm.unit?hm.unit:"");
        jb(&j,",\"label\":"); jb_s(&j,hm.label); jb(&j,",\"sub\":"); jb_s(&j,hm.sub); jb(&j,",\"stats\":[");
        for(int k=0;k<3;k++){ jb(&j,"%s[",k?",":""); jb_s(&j,hm.sl[k]?hm.sl[k]:""); jb(&j,","); jb_s(&j,hm.svv[k]); jb(&j,"]"); }
        jb(&j,"]");
    } else if(view==V_DETAIL&&sel>=0){
        set_fmt(sel,edit_val,a,sizeof a); set_fmt(sel,edit_orig,b,sizeof b);
        jb(&j,"\"param\":\"%s\",\"label\":",sim_param_id(sel)); jb_s(&j,TR(SET[sel].label)); jb(&j,",\"value\":"); jb_s(&j,a);
        jb(&j,",\"unit\":"); jb_s(&j,set_unit(sel)); jb(&j,",\"saved\":"); jb_s(&j,b);
        jb(&j,",\"raw\":%d,\"min\":%d,\"max\":%d,\"step\":%d,\"locked\":%s,\"saving\":%s",edit_val,SET[sel].lo,SET[sel].hi,SET[sel].step,(SET[sel].flags&SF_LOCK)?"true":"false",sav.active?"true":"false");
    } else if(view==V_SYS){
        jb(&j,"\"x\":\"%s\",\"edit\":%d,\"locked\":%s",xsel==X_BOOTUI?"bootui":(xsel==X_SWITCH?"switch":"exit"),x_edit,sys_locked()?"true":"false");
    } else if(view==V_MAIN&&pg_list(page)){
        static const char*const SECK[]={"display","clock","schedule","stock"}; char lv[96]; page_live_text(page,lv,sizeof lv,now);
        jb(&j,"\"title\":"); jb_s(&j,page_title(page)); jb(&j,",\"rows\":%d,\"scroll\":%d,\"live\":",page_rows(page),(int)scroll[page]); jb_s(&j,lv);
        jb(&j,",\"section\":"); if(page==PG_SYS&&sec_open>=0) jb(&j,"\"%s\"",SECK[sec_open]); else jb(&j,"null");
    } else if(view==V_BYE){ jb(&j,"\"sub\":"); jb_s(&j,bye_sub()); }
    jb(&j,"}");
    /* toast: text = the table's DANISH text (the scenarios' assert language), shown = the text on screen */
    if(toast.dur&&now-toast.t0<toast.dur){
        uint32_t c=sim_tst.col; const char*kind=c==C_OK?"ok":(c==C_ERR?"err":(c==C_WARN?"warn":"info"));
        jb(&j,",\"toast\":{\"text\":"); jb_s(&j,sim_tst.id>=0?STR[LANG_DA][sim_tst.id]:toast.msg);
        jb(&j,",\"shown\":"); jb_s(&j,toast.msg); jb(&j,",\"id\":\"%s\",\"kind\":\"%s\"}",sim_tst.id>=0?STR_ID[sim_tst.id]:"",kind);
    } else jb(&j,",\"toast\":null");
    /* language: lang = the stock UI's config.language (contract); ui_lang = Meraki+'s own language */
    jb(&j,",\"lang\":%d,\"ui_lang\":\"%s\",\"lang_src\":\"%s\"",sv(S_LANG),LANG_CODE[ui_lang],lang_src==LS_FILE?"file":(lang_src==LS_UNKNOWN?"unknown":(lang_src==LS_SESSION?"session":"default")));
    /* boot: the flag is read fresh; the rest is the UI's own view (bs) — the state export does not change it */
    int flag=stat(rp(BOOT_FLAG),&st)==0, nb=(!flag||bs.lock==BL_NOMOUNT)?BN_ORIGINAL:(bs.lock==BL_OK?BN_OURS:BN_SLOTB);
    jb(&j,",\"boot_flag\":%s,\"next_boot\":\"%s\",\"boot_lock\":\"%s\",\"exe_slot\":\"%s\",\"self_exe\":",flag?"true":"false",
       nb==BN_OURS?"ours":(nb==BN_SLOTB?"slot_b":"original"),bs.lock==BL_OK?"ok":(bs.lock==BL_NOMOUNT?"nomount":(bs.lock==BL_NOBIN?"nobin":"vendor")),
       bs.self_slot==SLOT_B?"B":(bs.self_slot==SLOT_A?"A":"other")); jb_s(&j,bs.self_exe);
    jb(&j,",\"switch\":{\"mode\":\"%s\",\"locked\":%s,\"dry\":%s,\"bye\":%s,\"stock_n\":%d,\"switcher_pid\":%d,\"child\":%d,\"last_evt\":",switch_mode()==SWM_CONT?"cont":"launch",
       switch_locked()?"true":"false",sw_dry?"true":"false",view==V_BYE?"true":"false",bs.n_stock,bs.sw_pid,sw_child); jb_s(&j,bs.sw_evt); jb(&j,"}");
    snprintf(rn,sizeof rn,"%s/RESTART_NEEDED",cfg_bakdir);
    { int fd=open(rn,O_RDONLY), have=fd>=0; if(have){ static char rb[1024]; int n=(int)read(fd,rb,sizeof rb-1); close(fd); if(n<0)n=0; rb[n]=0;
        char*q=strstr(rb,"boot_id="); if(q){ snprintf(stamp,sizeof stamp,"%s",q); stamp[strcspn(stamp,"\r\n")]=0; } }
      jb(&j,",\"restart_needed\":{\"file\":%s,\"pending\":%s,\"stamp\":",have?"true":"false",restart_pending()?"true":"false"); jb_s(&j,stamp); jb(&j,"}"); }
    /* link + write queue */
    jb(&j,",\"crc_bad\":%u,\"cmd6\":{\"ok\":%ld,\"retx\":%ld,\"fail\":%ld,\"block\":%ld},\"blok\":%ld,\"wq_n\":%d",rx_bad,wq.n_ok,wq.n_retx,wq.n_fail,wq.n_block,wq.n_block,wq.n);
    jb(&j,",\"boot_pushed\":%s,\"boot_push\":{\"enabled\":%s,\"sub1_len30\":\"%s\",\"sub112_len3\":\"%s\",\"runs\":%d}",(bp.state==BP_DONE&&bp.ra!=BPR_NOCFG&&bp.ra!=BPR_SKIP)?"true":"false",bp_on?"true":"false",sim_bpr(bp.ra),sim_bpr(bp.rb),bp.runs);
    jb(&j,",\"rx_resync\":%u",rx_resync);
    jb(&j,",\"repush\":{\"count\":%d,\"ver\":%d,\"defaults\":%d,\"last\":\"%s\",\"armed\":%s,\"ver_reqs\":%u,\"defaults_sig\":%s}",repush_count(),rps.n_ver,rps.n_def,
       rps.last==RPC_VER?"ver":(rps.last==RPC_DEF?"defaults":"none"),rps.armed?"true":"false",ver_n,mcu_defaults_sig()?"true":"false");
    jb(&j,",\"ka_hold\":%s,\"backlight_preview\":%d",sim_ka_hold?"true":"false",sim_bl);
    { struct tm lt; int64_t u=clk_utc(); int ok=clk_now(&lt), d=ok&&SET[S_DST].val&&dst_on(tz_idx(),u); char c[24]="", st[72]="";   /* v6.16: clock + schedule */
      if(ok) fmt_hm(c,sizeof c,lt.tm_hour,lt.tm_min,1);
      jb(&j,",\"clock\":{\"utc\":%lld,\"valid\":%s,\"local\":\"%04d-%02d-%02dT%02d:%02d:%02d\",\"wday\":%d,\"timemode\":%d,\"tz\":%d,\"dst\":%s,\"shown\":",
         (long long)u,ok?"true":"false",lt.tm_year+1900,lt.tm_mon+1,lt.tm_mday,lt.tm_hour,lt.tm_min,lt.tm_sec,lt.tm_wday,SET[S_TIMEMODE].val,tz_idx(),d?"true":"false"); jb_s(&j,c); jb(&j,"}");
      int ad,ah,am; int64_t at; int w=sched_alarm(u,&ad,&ah,&am,&at); int hp=home_preheat_text(st,sizeof st);
      jb(&j,",\"sched\":{\"preheat\":%d,\"at\":\"%02d:%02d\",\"repeat\":\"%s\",\"auto_off\":%d,\"once_at\":%lld,\"file\":\"%s\",\"alarm\":[%d,%d,%d],\"next_utc\":%lld",
         sch.on,sch.hm/60,sch.hm%60,REP_KEY[sch.rep],AO_MIN[sch.ao],(long long)sch.once_at,sch.src==SCS_FILE?"file":(sch.src==SCS_SESSION?"session":"default"),ad,ah,am,(long long)(w?at:0));
      jb(&j,",\"armed\":"); if(arm.n_sent) jb(&j,"[%d,%d,%d]",arm.ad,arm.ah,arm.am); else jb(&j,"null");
      jb(&j,",\"arm_sent\":%ld,\"arm_ack\":%ld,\"idle_s\":%d,\"auto_offs\":%ld,\"home\":",arm.n_sent,arm.n_ack,(int)((now-idle_t0)/1000),ao_n); if(hp) jb_s(&j,st); else jb(&j,"null"); jb(&j,"}"); }
    { static const char*const PWS[]={"on","turning_off","off","turning_on"};   /* v6.14: the power button */
      jb(&j,",\"power\":{\"state\":\"%s\",\"backlight_off\":%s,\"presses\":%ld,\"short\":%ld,\"offs\":%ld,\"ons\":%ld,\"blocked\":%ld,\"fails\":%ld,\"ack8\":%ld,\"auto_on\":%ld,\"wake_timeouts\":%ld}",
         PWS[pw.state],bl_is_off?"true":"false",pw.n_press,pw.n_short,pw.n_off,pw.n_on,pw.n_blk,pw.n_fail,pw.n_ack8,pw.n_auto,pw.n_wto); }
    { static const char*const NSK[]={"off","down","starting","connecting","up"}; char ipb[20];   /* v6.7: the net watchdog */
      jb(&j,",\"net\":{\"enabled\":%s,\"state\":\"%s\",\"iface\":%s,\"ifup\":%s,\"wpa_pid\":%d,\"dhcp_pid\":%d,\"carrier\":%s,\"ip\":\"%s\",\"gw\":%s,\"heals\":%d,\"boots\":%d,\"heal_ms\":%d,\"init_runs\":%d,\"wpa_starts\":%d,\"dhcp_starts\":%d,\"conf_writes\":%d,\"fails\":%d}",
         net.on?"true":"false",NSK[net.on?net.state:NS_OFF],net.p.iface?"true":"false",net.p.ifup?"true":"false",net.p.wpa_pid,net.p.dhcp_pid,net.p.carrier?"true":"false",net_ipstr(net.p.ip,ipb,sizeof ipb),net.p.gw?"true":"false",
         net.n_heal,net.n_boot,(int)net.heal_ms,net.n_init,net.n_wpa,net.n_dhcp,net.n_conf,net.n_fail); }
    /* config */
    jb(&j,",\"config_missing\":%s,\"config_writes\":%d,\"config_md5\":\"%s\",\"config_oor\":[",cfg_ok?"false":"true",cfg_writes,cfg_md5);
    for(int i=0,n=0;i<NSET;i++) if(set_oor(&SET[i])) jb(&j,"%s{\"param\":\"%s\",\"field\":\"%s\",\"value\":%d,\"min\":%d,\"max\":%d}",n++?",":"",sim_param_id(i),SET[i].key?SET[i].key:"",SET[i].val,SET[i].lo,SET[i].hi);
    jb(&j,"]");
    if(ssave.state==SSV_NONE) jb(&j,",\"save\":null");
    else jb(&j,",\"save\":{\"state\":\"%s\",\"param\":\"%s\",\"value\":%d}",SSV_KEY[ssave.state],ssave.param,ssave.val);
    if(act.pending==ACT_NONE) jb(&j,",\"activity_pending\":null"); else jb(&j,",\"activity_pending\":\"%s\"",ACT_KEY[act.pending]);
    /* counters outside the content comparison: RX counter + frame time (last, max since previous line, mean, max over the run; HOST) */
    char*tail=j.p; uint32_t h=H0; for(char*q=buf;q<tail;q++){ h^=(unsigned char)*q; h*=16777619u; }
    jb(&j,",\"crc_ok\":%u,\"frame_ms\":%.3f,\"frame_ms_max\":%.3f,\"frame_ms_avg\":%.3f,\"frame_ms_peak\":%.3f,\"frames\":%ld,\"fb_seq\":%ld}",
       rx_ok,ft_last,sim_ft_win>ft_last?sim_ft_win:ft_last,sim_frames?sim_ft_sum/sim_frames:0.0,sim_ft_peak,sim_frames,sim_fb_seq);
    if(j.p>=j.e){ static int warned; if(!warned){ warned=1; fputs("merakiplus-sim: state line too long\n",stderr); } return; }
    if(!force&&h==last_h&&now-sim_state_last<90) return;
    last_h=h; *j.p++='\n'; fprintf(sim_sf,"{\"t_ms\":%lld,",(long long)(now-sim_t0)); fwrite(buf,1,(size_t)(j.p-buf),sim_sf); fflush(sim_sf); sim_state_last=now; sim_ft_win=0;
}
/* after each rendered frame: frame statistics, state line (if the content changed, otherwise ~10 Hz), fb file */
static void sim_frame(ms_t now){
    sim_frames++; sim_ft_sum+=ft_last; if(ft_last>sim_ft_win) sim_ft_win=ft_last; if(ft_last>sim_ft_peak) sim_ft_peak=ft_last;
    uint32_t g=H0; int ton=toast.dur&&now-toast.t0<toast.dur;
    g=hi(g,view); g=hi(g,page); g=hi(g,sel); g=hi(g,xsel); g=hi(g,phase_raw(now)); g=hi(g,act.ph); g=hi(g,ton?(int)(toast.t0&0x7FFFFFFF):0);
    g=hi(g,ssave.state); g=hi(g,ssave.val); g=hi(g,act.pending); g=hi(g,(int)(wq.n_ok+wq.n_fail*7+wq.n_block*31+wq.n_retx*131));
    g=hi(g,ui_lang); g=hi(g,sv(S_LANG)); g=hi(g,cfg_writes); g=hi(g,bp.state); g=hi(g,stm.kind*4+stm.wand); g=hi(g,repush_count()); g=hi(g,edit_val); g=hi(g,x_edit); g=hi(g,access(rp(BOOT_FLAG),F_OK)==0);
    int chg=(g!=sim_sig); sim_sig=g; sim_state_emit(now,chg);   /* every frame if anything in the line changed; otherwise at least every 90 ms */
    if(sim_fb_seq!=sim_fb_dumped&&(chg||now-sim_fb_last>=100)){ if(sim_fb_write(sim_fb_path)){ sim_fb_last=now; sim_fb_dumped=sim_fb_seq; } }
}

/* ---- JSON in: one flat object per line ---- */
typedef struct { const char*k; int kl; const char*v; int vl; char t; } JKV;   /* t: 's' string (without quotes), 'a' array, 'o' object, 'n' number/true/false/null */
static const char* js_ws(const char*s){ while(*s==' '||*s=='\t'||*s=='\r'||*s=='\n') s++; return s; }
static const char* js_strend(const char*s){ while(*s&&*s!='"'){ if(*s=='\\'&&s[1]) s++; s++; } return s; }   /* s points past the opening quote */
static int js_parse(const char*s,JKV*kv,int max){
    int n=0; s=js_ws(s); if(*s!='{') return -1; s++;
    for(;;){
        s=js_ws(s); if(*s=='}') return n; if(*s!='"') return -1;
        const char*k=s+1,*ke=js_strend(k); if(!*ke) return -1;
        s=js_ws(ke+1); if(*s!=':') return -1; s=js_ws(s+1);
        JKV e={k,(int)(ke-k),s,0,'n'};
        if(*s=='"'){ const char*ve=js_strend(s+1); if(!*ve) return -1; e.v=s+1; e.vl=(int)(ve-s-1); e.t='s'; s=ve+1; }
        else if(*s=='['||*s=='{'){ int d=0; const char*q=s; e.t=(*s=='[')?'a':'o';
            for(;*q;q++){ if(*q=='"'){ q=js_strend(q+1); if(!*q) return -1; } else if(*q=='['||*q=='{') d++; else if(*q==']'||*q=='}'){ if(--d==0){ q++; break; } } }
            if(d) return -1; e.vl=(int)(q-s); s=q; }
        else { const char*q=s; while(*q&&*q!=','&&*q!='}'&&*q!=' '&&*q!='\r'&&*q!='\n') q++; e.vl=(int)(q-s); s=q; }
        if(n<max) kv[n++]=e;
        s=js_ws(s); if(*s==','){ s++; continue; } if(*s=='}') return n; return -1;
    }
}
static const JKV* js_get(const JKV*kv,int n,const char*key){ int L=(int)strlen(key); for(int i=0;i<n;i++) if(kv[i].kl==L&&!memcmp(kv[i].k,key,L)) return &kv[i]; return NULL; }
static int js_str(const JKV*kv,int n,const char*key,char*out,size_t on){
    const JKV*e=js_get(kv,n,key); out[0]=0; if(!e||e->t!='s') return 0;
    size_t o=0; for(int i=0;i<e->vl&&o+1<on;i++){ char c=e->v[i];
        if(c=='\\'&&i+1<e->vl){ c=e->v[++i]; if(c=='n') c='\n'; else if(c=='t') c='\t'; else if(c=='u'&&i+4<e->vl){ char h[5]; memcpy(h,e->v+i+1,4); h[4]=0; c=(char)strtol(h,NULL,16); i+=4; } }
        out[o++]=c; }
    out[o]=0; return 1;
}
static int js_int(const JKV*kv,int n,const char*key,int def){
    const JKV*e=js_get(kv,n,key); if(!e) return def;
    if(e->t=='n'){ if(e->vl>=4&&!memcmp(e->v,"true",4)) return 1; if(e->vl>=5&&!memcmp(e->v,"false",5)) return 0; if(e->vl>=4&&!memcmp(e->v,"null",4)) return def; }
    if(e->t=='n'||e->t=='s') return (int)strtod(e->v,NULL);
    return def;
}
static double js_num(const JKV*kv,int n,const char*key,double def){ const JKV*e=js_get(kv,n,key); return (e&&(e->t=='n'||e->t=='s')&&e->v[0]!='n')?strtod(e->v,NULL):def; }
static int js_words(const JKV*kv,int n,const char*key,uint16_t*w,int max){
    const JKV*e=js_get(kv,n,key); if(!e||e->t!='a') return -1;
    int c=0; const char*s=e->v+1,*end=e->v+e->vl-1;
    while(s<end){ s=js_ws(s); if(s>=end) break; char*q; long v=strtol(s,&q,0); if(q==s) return -1; if(c>=max||v<0||v>65535) return -1; w[c++]=(uint16_t)v; s=js_ws(q); if(*s==',') s++; }
    return c;
}

/* ---- test hooks ---- */
static int sim_def_kind=0, sim_def_a=0, sim_def_b=0; static ms_t sim_def_until=0; static char sim_def_param[40]="";
/* end splash/transition and close an open detail/system view — like a completed tap on BACK, without animation */
static void sim_go_main(void){
    if(sim_bye_on){ sim_bye_on=0; sim_bye_until=0; if(view==V_BYE) view=V_MAIN; }
    if(view==V_DETAIL&&sel>=0){ if(SET[sel].flags&SF_BL) bl_set(SET[sel].val); if(SET[sel].flags&SF_LOCAL) lang_set(SET[sel].val); }
    if(view!=V_MAIN&&view!=V_BYE){ view=V_MAIN; act.gkind=-1; }
    sel=-1; xsel=-1; x_from_switch=0; tr.type=TR_NONE; static_dirty=1; force_all=1; ui_kick=1;
}
static void sim_set_page(int np){ if(page!=np){ page=np; static_dirty=1; if(np==PG_HOME) act.gkind=-1; } }
/* v6.17: the page a row lives on — and, on SYSTEM, its section opened (as the user would have had to) */
static void sim_set_row(int idx){ sim_set_page(PG_T0+SET[idx].tier); if(SET[idx].tier==4&&sec_of_set(idx)>=0){ sec_open=sec_of_set(idx); scroll[PG_SYS]=clampf((float)(sec_open*ROW_P),0,scroll_max(PG_SYS)); } }
static void sim_set_stock(void){ sim_set_page(PG_SYS); sec_open=SEC_STOCK; scroll[PG_SYS]=clampf((float)(SEC_STOCK*ROW_P),0,scroll_max(PG_SYS)); }
/* SAVE as via stepper + SAVE: open the row, set the value the stepper would show, tap SAVE (save_begin) */
static int sim_save_now(int idx,int val,const char*param,ms_t now){
    ssave.idx=idx; ssave.val=val; snprintf(ssave.param,sizeof ssave.param,"%s",param);
    if(idx<0||(SET[idx].flags&SF_LOCK)){                    /* no editor: unknown/not exposed, locked or sensor */
        ssave.state=SSV_REJECTED; ssave.idx=0; toast_show(TR(TX_D_LOCKED),C_WARN,now); ui_kick=1; return 0;
    }
    Setting*s=&SET[idx]; int was=s->val;
    sim_go_main(); sim_set_row(idx);
    detail_open(idx,now);
    if(!set_valid(s,val)||(s->step>1&&(val-s->lo)%s->step)){   /* outside the range or between the stepper's steps */
        ssave.state=SSV_REJECTED; toast_show(TR(TX_T_RANGE),C_ERR,now); detail_close(now); return 0;
    }
    edit_val=val; if(s->flags&SF_BL) bl_set(val); if(s->flags&SF_LOCAL) lang_set(val);
    save_begin(now);
    if(sav.active){ ssave.state=SSV_PENDING; return 1; }
    if(was==val){ ssave.state=SSV_UNCHANGED; return 1; }    /* SAVE without a change just closes the editor */
    if(s->flags&(SF_LOCAL|SF_OWN|SF_SND)){ ssave.state=SSV_OK; return 1; }
    ssave.state=SSV_REJECTED; if(view==V_DETAIL) detail_close(now);   /* a guard rejected it (toast shown): the user cancels */
    return 0;
}
/* activity as via the button (the hold time is skipped; the guards are the same) */
static int sim_action_now(int kind,ms_t now){
    sim_go_main(); sim_set_page(PG_HOME);
    act_request(kind,now); return act.pending==kind;
}
/* raw_write: emulates a STOCK UI write (contract §5), e.g. MachSleep word1=1, which merakiplus itself never
 * sends. Same band and trigger guard as the frame policy (words 26/27/30 and start words cannot
 * be set), and all other words must lie within their spec range — the only relaxation is word1 ∈ {0,1}.
 * word1=1 also pauses merakiplus's keep-awake (otherwise the UI wakes the controller again within
 * 3 s — that is the v6 behaviour); a raw_write w/ word1=0 lifts the pause. */
static int sim_raw_push(int sub,const uint16_t*w,int len){
    if(ro_mode||wq.n>=WQ_MAX||len<1||len>WQ_WMAX||sub<0||sub>255) return 0;
    int end=sub+len;
    if(!(end<33||(sub>=112&&end>=113&&end<=115))){ wq.n_block++; return 0; }
    for(int k=0;k<len;k++){
        int idx=sub+k; uint16_t v=w[k];
        if(idx<32&&v!=0&&((TRIG_NEVER|TRIG_START)&WBIT(idx))){ wq.n_block++; return 0; }
        if(idx==1){ if(v>1){ wq.n_block++; return 0; } continue; }
        if(!word_ok(idx,v)){ wq.n_block++; return 0; }
    }
    WFrame*f=&wq.q[wq.n++]; f->sub=(uint8_t)sub; f->len=(uint8_t)len; memcpy(f->w,w,(size_t)len*2);
    if(wq.n==1){ wq.state=WQ_IDLE; wq.result=WR_NONE; }
    if(sub<=1&&end>1) sim_ka_hold=(w[1-sub]==1);
    return 1;
}
/* SWITCH NOW in dry-run: SIM shows the goodbye screen for 1 s and returns (device: only log + toast) */
static int sim_bye_begin(int mode,ms_t now){
    bye_mode=mode; view=V_BYE; xsel=-1; x_from_switch=0; tr.type=TR_NONE; rip.on=0; toast.dur=0; static_dirty=1; force_all=1;
    sim_bye_on=1; sim_bye_until=now+1000; ui_kick=1; return 1;
}
static void sim_hooks_step(ms_t now){
    if(sim_bye_on&&now>=sim_bye_until){
        sim_bye_on=0;
        if(view==V_BYE&&run){ view=V_MAIN; static_dirty=1; force_all=1; act.gkind=-1; toast_show(TR(TX_T_DRYRUN),C_WARN,now); ui_kick=1; }
    }
    if(sim_def_kind){                                       /* deferred hook: wait (at most 600 ms) until the write queue is empty, like a tap a moment later */
        int busy=wq_busy()||sav.active;
        if(!busy||now>=sim_def_until){ int k=sim_def_kind; sim_def_kind=0; if(k==1) sim_save_now(sim_def_a,sim_def_b,sim_def_param,now); else sim_action_now(sim_def_a,now); }
    }
}
static void sim_dispatch(const char*line,char*out,size_t on,ms_t now){
    JKV kv[24]; int n=js_parse(line,kv,24); char cmd[32]="", name[40]="", path[420]="";
    JB j={out,out+on-48};
    if(n<0){ jb(&j,"{\"ok\":false,\"error\":\"invalid JSON\""); goto done; }
    js_str(kv,n,"cmd",cmd,sizeof cmd); js_str(kv,n,"name",name,sizeof name);
    if(!strcmp(cmd,"ping")){ jb(&j,"{\"ok\":true,\"t\":%.3f,\"page\":\"%s\",\"phase\":\"%s\",\"client\":\"merakiplus-sim\",\"version\":\"" MERAKIPLUS_VERSION "\"",(now-sim_t0)/1000.0,sim_page_key(),PH_KEY[phase_raw(now)]); }
    else if(!strcmp(cmd,"state_now")){ sim_state_emit(now,1); jb(&j,"{\"ok\":true"); }
    else if(!strcmp(cmd,"fb_dump")){
        if(!js_str(kv,n,"path",path,sizeof path-8)) snprintf(path,sizeof path,"%s",sim_fb_path);
        jb(&j,"{\"ok\":%s,\"bytes\":%d",sim_fb_write(path)?"true":"false",W*H*4);
    }
    else if(!strcmp(cmd,"exit")){ sim_rc=js_int(kv,n,"code",0); run=0; jb(&j,"{\"ok\":true"); }
    else if(!strcmp(cmd,"key")){   /* v6.14: the power button (SIM has no k62091 node): {"cmd":"key","value":1|0[,"code":116]} */
        int v=js_int(kv,n,"value",-1), c=js_int(kv,n,"code",116);
        if(v!=0&&v!=1) jb(&j,"{\"ok\":false,\"error\":\"value must be 0 or 1\"");
        else { pw_key(c,v,now); jb(&j,"{\"ok\":true,\"power\":\"%s\"",pw.state==PW_ON?"on":(pw.state==PW_OFF?"off":(pw.state==PW_OFFING?"turning_off":"turning_on"))); } }
    else if(!strcmp(cmd,"action")){
        int kind=-1; for(int k=0;k<4;k++) if(!strcmp(name,ACT_KEY[k])) kind=k;
        int stk=-1; for(int k=0;k<4;k++) if(!strcmp(name,STOP_KEY[k])) stk=k;     /* v6.13: grind_stop / brew_stop / water_stop (+ steam_stop as before) */
        if(stk>=0){ int n0=wq.n; stop_request(stk,now); ui_kick=1; jb(&j,"{\"ok\":%s",wq.n>n0?"true":"false"); }
        else if(kind<0) jb(&j,"{\"ok\":false,\"error\":\"unknown activity\"");
        else if((wq_busy()||sav.active)&&act.pending==ACT_NONE){ sim_def_kind=2; sim_def_a=kind; sim_def_until=now+600; jb(&j,"{\"ok\":true,\"deferred\":true"); }
        else { int ok=sim_action_now(kind,now); jb(&j,"{\"ok\":%s",ok?"true":"false"); if(!ok&&sim_tst.id>=0) jb(&j,",\"guard\":\"%s\"",STR_ID[sim_tst.id]); }
    }
    else if(!strcmp(cmd,"save")){
        char param[40]=""; js_str(kv,n,"param",param,sizeof param); int idx=sim_param_idx(param), val=js_int(kv,n,"value",0);
        if(idx>=0&&!(SET[idx].flags&SF_LOCK)&&(sav.active||(SET[idx].mcu!=M_NONE&&wq_busy()))){
            sim_def_kind=1; sim_def_a=idx; sim_def_b=val; snprintf(sim_def_param,sizeof sim_def_param,"%s",param); sim_def_until=now+600; jb(&j,"{\"ok\":true,\"deferred\":true");
        } else { int ok=sim_save_now(idx,val,param,now); jb(&j,"{\"ok\":%s,\"state\":\"%s\",\"row\":",ok?"true":"false",SSV_KEY[ssave.state]); jb_s(&j,idx>=0&&SET[idx].key?SET[idx].key:(idx>=0?"(ui)":"")); }
    }
    else if(!strcmp(cmd,"page")){
        char param[40]=""; js_str(kv,n,"param",param,sizeof param); int ok=1;
        sim_go_main();
        /* v6.17: grind | brew | water | steam | system by name ("settings" + tab 0..4 = the same five pages; tab 1 = BREW is the default);
         * "section" (display | clock | schedule | stock) opens that SYSTEM section */
        static const char*const PGN[]={"grind","brew","water","steam","system"}; int np=-1; char sn[16]="";
        for(int k=0;k<5;k++) if(!strcmp(name,PGN[k])) np=PG_GRIND+k;
        if(!strcmp(name,"settings")) np=PG_GRIND+clampi(js_int(kv,n,"tab",1),0,4);
        if(!strcmp(name,"home")) { if(page!=PG_HOME) go_page(PG_HOME,-1,now); }
        else if(np>=0){
            if(np==PG_SYS){ static const char*const SECK[]={"display","clock","schedule","stock"}; sec_open=-1; scroll[PG_SYS]=0;
                if(js_str(kv,n,"section",sn,sizeof sn)) for(int k=0;k<NSEC;k++) if(!strcmp(sn,SECK[k])){ sec_open=k; scroll[PG_SYS]=clampf((float)(k*ROW_P),0,scroll_max(PG_SYS)); } }
            if(page!=np) go_page(np,1,now); }
        else if(!strcmp(name,"diag")){ if(page!=PG_DIAG){ diag_from=PG_SYS; go_page(PG_DIAG,1,now); } }
        else if(!strcmp(name,"detail")){ int idx=param[0]?sim_param_idx(param):S_GRINDW; if(idx<0) ok=0; else { sim_set_row(idx); detail_open(idx,now); } }
        else if(!strcmp(name,"sys")){ sim_set_stock(); sys_open(js_int(kv,n,"x",X_BOOTUI)==X_SWITCH?X_SWITCH:X_BOOTUI,now); }
        else ok=0;
        jb(&j,"{\"ok\":%s,\"page\":\"%s\"",ok?"true":"false",sim_page_key());
    }
    else if(!strcmp(cmd,"touch")){ sim_touch_inject(js_int(kv,n,"x",CX),js_int(kv,n,"y",CY),js_int(kv,n,"ms",80),now); jb(&j,"{\"ok\":true"); }
    else if(!strcmp(cmd,"boot_flag")){                      /* SYSTEM -> BOOT UI -> choose -> SAVE (sys_confirm -> boot_flag_set w/ guard) */
        int want=js_int(kv,n,"value",0)!=0; struct stat st;
        sim_go_main(); sim_set_stock(); sys_open(X_BOOTUI,now); x_edit=want; sys_confirm(now);
        if(view==V_SYS) sys_close(now);                     /* locked / error: toast is shown, the user taps BACK */
        int have=stat(rp(BOOT_FLAG),&st)==0;
        jb(&j,"{\"ok\":%s,\"flag\":%s,\"lock\":%d",have==want?"true":"false",have?"true":"false",bs.lock);
    }
    else if(!strcmp(cmd,"runtime_switch")){                 /* SYSTEM -> SWITCH TO ORIGINAL NOW -> SWITCH NOW (sys_confirm -> do_switch) */
        sim_go_main(); sim_set_stock(); sys_open(X_SWITCH,now); sys_confirm(now);
        int went=sim_bye_on||!run;
        if(view==V_SYS&&xsel==X_SWITCH) sys_close(now);     /* blocked (activity/locked): toast shown, the user goes back */
        jb(&j,"{\"ok\":%s,\"dry\":%s",went?"true":"false",sw_dry?"true":"false"); if(!went&&sim_tst.id>=0) jb(&j,",\"guard\":\"%s\"",STR_ID[sim_tst.id]);
    }
    else if(!strcmp(cmd,"raw_write")){
        uint16_t w[WQ_WMAX]; int len=js_words(kv,n,"words",w,WQ_WMAX), ok=len>0&&sim_raw_push(js_int(kv,n,"sub",-1),w,len);
        jb(&j,"{\"ok\":%s,\"ka_hold\":%s",ok?"true":"false",sim_ka_hold?"true":"false");
    }
    else if(!strcmp(cmd,"config_reload")){                  /* as at startup: read language file + config, and (SIM) boot-push again */
        sim_go_main(); lang_load(); settings_load(); if(bp_on) boot_push_start(sim_bp_guard);
        int oor=0; for(int i=0;i<NSET;i++) oor+=set_oor(&SET[i]);
        jb(&j,"{\"ok\":%s,\"oor\":%d",cfg_ok?"true":"false",oor);
    }
    else if(!strcmp(cmd,"clock")){                          /* v6.16: the wall clock — {"utc":"YYYY-MM-DDTHH:MM:SSZ"} and/or {"advance_s":N} */
        char u[40]=""; js_str(kv,n,"utc",u,sizeof u); int adv=js_int(kv,n,"advance_s",0), ok=1;
        if(u[0]){ int64_t t=sim_parse_utc(u); if(t<0) ok=0; else clk_off=t-(int64_t)time(NULL); }
        if(ok) clk_off+=adv; ui_kick=1; static_dirty=1;
        jb(&j,"{\"ok\":%s,\"utc\":%lld",ok?"true":"false",(long long)clk_utc());
    }
    else if(!strcmp(cmd,"cond")){                           /* v6.9: display conditioning — all fields optional; without fields = read */
        char sc[8]=""; js_str(kv,n,"scale",sc,sizeof sc);
        cd.a_chart=clampf((float)js_num(kv,n,"a_chart",cd.a_chart),0.01f,1); cd.a_hero=clampf((float)js_num(kv,n,"a_hero",cd.a_hero),0.01f,1);
        cd.med=js_int(kv,n,"med",cd.med)>=3?3:1; cd.hold=clampi(js_int(kv,n,"hold",cd.hold),1,50); cd.pct=clampi(js_int(kv,n,"pct",cd.pct),50,100);
        cd.head=clampf((float)js_num(kv,n,"head",cd.head),1,3); cd.raw=js_int(kv,n,"raw",cd.raw)!=0; cd.gate=clampi(js_int(kv,n,"gate",cd.gate),0,10);
        for(int k=0;k<3;k++) if(!strcmp(sc,CS_KEY[k])) cd.scale=k;
        if(js_get(kv,n,"reset")||js_get(kv,n,"a_chart")||js_get(kv,n,"med")) cond_reset();
        hf.ok=0; ui_kick=1;
        jb(&j,"{\"ok\":true,\"raw\":%s,\"a_chart\":%.3f,\"a_hero\":%.3f,\"med\":%d,\"scale\":\"%s\",\"hold\":%d,\"pct\":%d,\"head\":%.2f,\"gate\":%d",
           cd.raw?"true":"false",cd.a_chart,cd.a_hero,cd.med,CS_KEY[cd.scale],cd.hold,cd.pct,cd.head,cd.gate);
    }
    else if(!strcmp(cmd,"net")){                            /* v6.7: the wifi model — kill | ip_drop | if_down | cold | ap_down | ap_up | status */
        int ok=1; char ipb[20];
        if(!strcmp(name,"kill")){ nm.wpa_pid=0; nm.dhcp_pid=0; nm.carrier=0; }                       /* kill wpa_supplicant (+ udhcpc): the IP and the route stay on wlan0 */
        else if(!strcmp(name,"ip_drop")){ nm.ip=0; nm.gw=0; }                                       /* ifconfig wlan0 0.0.0.0 */
        else if(!strcmp(name,"if_down")){ nm.ifup=0; nm.carrier=0; nm.gw=0; }                       /* ifconfig wlan0 down: the IP stays, the routes disappear */
        else if(!strcmp(name,"cold")){ nm.iface=0; nm.ifup=0; nm.wpa_pid=0; nm.dhcp_pid=0; nm.carrier=0; nm.ip=0; nm.gw=0; }
        else if(!strcmp(name,"ap_down")){ nm.ap=0; nm.carrier=0; }
        else if(!strcmp(name,"ap_up")){ nm.ap=1; nm.assoc_at=now+NM_ASSOC_MS; }
        else if(strcmp(name,"status")) ok=0;
        if(ok&&strcmp(name,"status")) net.t_probe=0;        /* evaluate in the next loop iteration */
        jb(&j,"{\"ok\":%s,\"state\":%d,\"wpa_pid\":%d,\"ip\":\"%s\",\"heals\":%d",ok?"true":"false",net.state,nm.wpa_pid,net_ipstr(nm.ip,ipb,sizeof ipb),net.n_heal);
    }
    else if(!strcmp(cmd,"layout")){                         /* addendum: the REAL tap targets in the current view (hit_test's geometry) */
        jb(&j,"{\"ok\":true,\"page\":\"%s\",\"targets\":[",sim_page_key()); int c=0;
#define LT_C(nm,x,y,r) jb(&j,"%s{\"name\":\"%s\",\"x\":%d,\"y\":%d,\"r\":%d}",c++?",":"",nm,x,y,r)
#define LT_R(nm,x,y,w,h) jb(&j,"%s{\"name\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",c++?",":"",nm,(x)+(w)/2,(y)+(h)/2,w,h)
        if(view==V_DETAIL){                                                    /* v7.0: the roller editor */
            LT_R("cancel",BACK_X0,BACK_Y0,BACK_W,BACK_H);                       /* the chevron at the top (= CANCEL; a pull down does the same) */
            if(SET[sel].flags&SF_LOCK) LT_C("back",CX,ED_OK_Y,ED_OK_R+8);
            else { LT_C("minus",ED_MX,ED_CY,40); LT_C("plus",ED_PX,ED_CY,40); LT_C("save",CX,ED_OK_Y,ED_OK_R+8); LT_R("roller",ED_ROLL_X0,ED_CY-110,ED_ROLL_X1-ED_ROLL_X0,220); }
        } else if(view==V_SYS){
            if(sys_locked()) LT_R("back",170,SB_Y-4,140,SB_H+8);
            else { if(xsel==X_BOOTUI){ LT_C("minus",ED_MX,ED_CY,40); LT_C("plus",ED_PX,ED_CY,40); } LT_R("cancel",SB_X0,SB_Y-4,SB_W+8,SB_H+8); LT_R("save",SB_X1,SB_Y-4,SB_W+8,SB_H+8); }
        } else if(view==V_MAIN&&page==PG_HOME){
            int sk[2], sn=stop_btns(now,sk);                                   /* v6.13: STOP for grind/brew/water + latched steam */
            if(sn){ for(int i=0;i<sn;i++){ Rect r=stop_hit(sn,i); LT_R(STOP_KEY[sk[i]],r.x0,r.y0,r.x1-r.x0,r.y1-r.y0); } }
            else if(!phase_active(phase_raw(now))&&hm_mode!=HM_ERROR) for(int k=0;k<4;k++){ int i=ring_of_act(k); LT_C(ACT_KEY[k],RING_X[i],RING_Y[i],RING_R); }   /* v7.0: the rings: tap = the feature's page, hold = start */
            if(hm_mode==HM_ERROR) LT_R("diag",60,96,360,250);
            else if(diag_hint_shown()) LT_R("diag",70,HOME_STAT_Y-2,340,32);
            LT_R("gear",CX-60,0,120,hm_mode==HM_ACTIVE?44:58);               /* v7.0: the chevron at the top -> SYSTEM */
        } else if(view==V_MAIN){                                               /* v6.17: every page but HOME: "back" (the header) */
            LT_R("back",PBACK_X0,PBACK_Y0,PBACK_W,PBACK_H);
            if(pg_list(page)){
                static const char*const SECK[]={"sec_display","sec_clock","sec_schedule","sec_stock"};
                Row rw[ROW_MAX]; int nr=rows_get(page,rw), vy1=list_vy1(page);
                for(int k=0;k<nr;k++){ int y=(int)(row_y0(page,k)-scroll[page]); if(y<LIST_VY0-4||y+ROW_H>vy1+4) continue;
                    LT_R(rw[k].kind==RK_SET?sim_param_id(rw[k].a):(rw[k].kind==RK_SEC?SECK[rw[k].a]:(rw[k].kind==RK_DIAG?"diag":(rw[k].a==X_BOOTUI?"boot_ui":"switch_now"))),64,y,352,ROW_H); }
                if(pg_feat(page)) LT_R("start",CX-START_HIT_W/2,START_HIT_Y,START_HIT_W,START_HIT_H);
            } else if(page==PG_DIAG){ int py=(int)(DG_GRAPH+12-scroll[PG_DIAG]); if(py-8>=LIST_VY0&&py+36<=LIST_VY1) LT_R("graph",CX-150,py-8,300,44); }
        }
#undef LT_C
#undef LT_R
        jb(&j,"],\"swipe\":\"right >= 60 px = back (every page and editor but home); no carousel since v6.17; v7.0: pull down from the top of home = system\"");
    }
    else jb(&j,"{\"ok\":false,\"error\":\"unknown cmd\"");
done:
    { const JKV*id=n>0?js_get(kv,n,"id"):NULL; if(id&&id->t=='n'&&id->vl<24) jb(&j,",\"id\":%.*s}\n",id->vl,id->v); else jb(&j,",\"id\":null}\n"); }
    j.e=out+on-1; if(j.p>j.e) j.p=j.e; *j.p=0;
}
/* ---- ctrl socket: JSONL, one request per line, reply w/ the same id ---- */
#define SIM_NCLI 8
static struct { int fd, n; char buf[4096]; } sim_cli[SIM_NCLI];
static void sim_ctrl_open(void){
    struct sockaddr_un a; memset(&a,0,sizeof a); a.sun_family=AF_UNIX;
    for(int i=0;i<SIM_NCLI;i++) sim_cli[i].fd=-1;
    if(!sim_ctrl_path[0]) return;
    if(strlen(sim_ctrl_path)>=sizeof a.sun_path) sim_die(2,"MERAKIPLUS_SIM_CTRL is too long for a unix socket (%d chars, max %d)",(int)strlen(sim_ctrl_path),(int)sizeof a.sun_path-1);
    unlink(sim_ctrl_path); snprintf(a.sun_path,sizeof a.sun_path,"%s",sim_ctrl_path);
    sim_lfd=socket(AF_UNIX,SOCK_STREAM,0);
    if(sim_lfd<0||bind(sim_lfd,(struct sockaddr*)&a,sizeof a)!=0||listen(sim_lfd,8)!=0) sim_die(2,"ctrl-socket %s: %s",sim_ctrl_path,strerror(errno));
    fcntl(sim_lfd,F_SETFL,fcntl(sim_lfd,F_GETFL,0)|O_NONBLOCK);
}
static void sim_ctrl_io(ms_t now){
    if(sim_lfd<0) return;
    for(;;){
        int c=accept(sim_lfd,NULL,NULL); if(c<0) break;
        int k=0; while(k<SIM_NCLI&&sim_cli[k].fd>=0) k++;
        if(k==SIM_NCLI){ close(c); break; }
        fcntl(c,F_SETFL,fcntl(c,F_GETFL,0)|O_NONBLOCK); sim_cli[k].fd=c; sim_cli[k].n=0;
    }
    for(int k=0;k<SIM_NCLI;k++){
        if(sim_cli[k].fd<0) continue;
        int r=(int)read(sim_cli[k].fd,sim_cli[k].buf+sim_cli[k].n,sizeof sim_cli[k].buf-1-(size_t)sim_cli[k].n);
        if(r<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)) continue;
        if(r<=0){ close(sim_cli[k].fd); sim_cli[k].fd=-1; continue; }
        sim_cli[k].n+=r; sim_cli[k].buf[sim_cli[k].n]=0;
        char*s=sim_cli[k].buf,*nl;
        while((nl=strchr(s,'\n'))){
            *nl=0; const char*q=js_ws(s);
            if(*q){ static char resp[4096]; sim_dispatch(q,resp,sizeof resp,now);
                    int L=(int)strlen(resp), off=0; while(off<L){ int w=(int)write(sim_cli[k].fd,resp+off,(size_t)(L-off)); if(w<=0) break; off+=w; } }
            s=nl+1;
        }
        int rest=(int)(sim_cli[k].buf+sim_cli[k].n-s); memmove(sim_cli[k].buf,s,(size_t)rest); sim_cli[k].n=rest;
        if(sim_cli[k].n>=(int)sizeof sim_cli[k].buf-1) sim_cli[k].n=0;      /* line without an end: discard it */
    }
}

/* ---- lifecycle (called from main via the SIM_* macros) ---- */
static void sim_start(void){
    sim_sf=fopen(sim_state_path,"a"); if(!sim_sf) sim_die(2,"cannot open MERAKIPLUS_SIM_STATE %s: %s",sim_state_path,strerror(errno));
    if(!sim_fb_write(sim_fb_path)) sim_die(4,"cannot write MERAKIPLUS_SIM_FB %s: %s",sim_fb_path,strerror(errno));
    sim_ctrl_open();                                       /* listens BEFORE the first state line (the runner pings as soon as the line exists) */
    if(bp_on) boot_push_start(sim_bp_guard);
}
static void sim_tick(ms_t now){
    sim_save_track();                                      /* also catches a SAVE started by a tap in this iteration */
    sim_ctrl_io(now);
    sim_hooks_step(now);
    sim_save_track();
    boot_push_step(now);
    repush_step(now);
}
static void sim_exit(void){
    ms_t now=now_ms();
    sim_save_track(); sim_state_emit(now,1);
    if(fbmem) sim_fb_write(sim_fb_path);
    for(int k=0;k<SIM_NCLI;k++) if(sim_cli[k].fd>=0){ close(sim_cli[k].fd); sim_cli[k].fd=-1; }
    if(sim_lfd>=0){ close(sim_lfd); sim_lfd=-1; unlink(sim_ctrl_path); }
    if(sim_sf){ fclose(sim_sf); sim_sf=NULL; }
    if(sim_tfd_w>=0){ close(sim_tfd_w); sim_tfd_w=-1; }
}
#endif /* SIM_RT */
