/* src/mp/preview.c — PREVIEW build: synthetic scenarios, mock root, i18n helpers.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ================= PREVIEW: synthetic scenarios -> raw fb files (no device) ================= */
static ms_t sim_now=200000, sim_t0=0; static int sim_link=1;
static int pv_lang=LANG_EN;                /* --lang en|da: the language previews/selftest run in */
static int pv_sched=0;                     /* v6.16: run sched_step() in sim_run() */
static void (*sim_tick)(ms_t)=NULL;
static const uint32_t* sim_last=NULL;
static void sim_base(void){
    memset(reg,0,sizeof reg); for(int i=0;i<REGN;i++) reg_seen[i]=1;
    reg[0]=1; reg[1]=0; reg[33]=0x0101; reg[2]=1; reg[4]=185; reg[11]=870; reg[14]=3; reg[17]=400; reg[23]=360; reg[25]=650;
    reg[40]=999; reg[41]=999; reg[42]=702; reg[47]=1297; reg[48]=226; reg[49]=236; reg[61]=50; reg[50]=5; reg[58]=5;
    reg[37]=16; reg[71]=41; reg[72]=39; reg[112]=0x0104; reg[113]=0x0605; reg[114]=70;
    reg[62]=100; reg[63]=100;                                         /* v6.18: both boilers at their heat stage 100 (hot) */
    rx_ok=8794; rx_bad=0; last_bad_ms=-100000;
}
static void sim_run(ms_t ms){
    static ms_t lg=0; ms_t end=sim_now+ms;
    while(sim_now<end){
        sim_now+=33;
        if(sim_tick) sim_tick(sim_now-sim_t0);
        if(sim_link){ last_rx_ms=sim_now; rx_ok++; }
        if(sim_now-lg>=100){ lg=sim_now; graph_push(sim_now,reg[51],wsrc_tick(sim_now)); }
        wq_step(sim_now);
        pw_step(sim_now);                  /* v6.14 */
        if(pv_sched) sched_step(sim_now);  /* v6.16 (only where a scenario/selftest switches it on) */
        snd_tick(sim_now);                 /* v6.18: the sound engine (the preview build only logs) */
        sim_last=ui_frame(sim_now);
    }
}
#define PV_CLK (days_civil(2026,11,18)*86400+15*3600+35*60+28)   /* v6.16: preview clock = 2026-11-18 15:35:28 UTC (16:35:28 in zone #48) */
static void sim_reset(void){
    clk_fake=PV_CLK; idle_t0=0; ao_warned=0; memset(&arm,0,sizeof arm);
    view=V_MAIN; page=PG_HOME; sel=-1; sec_open=-1; diag_from=PG_SYS; memset(scroll,0,sizeof scroll); memset(&tch,0,sizeof tch);
    toast.dur=0; rip.on=0; tr.type=0; memset(&sav,0,sizeof sav); memset(ov_prev,0,sizeof ov_prev);
    xsel=-1; x_from_switch=0; x_pending=0; bye_mode=0; run=1; bs.t=-100000; lang_set(pv_lang);
    act.raw=PH_OFFLINE; act.ph=PH_OFFLINE; act.last_ph=PH_READY; act.pending=ACT_NONE; act.gkind=-1; act.t_done=-100000; act.t_col=-100000;
    g_n=0; g_head=0; static_dirty=1; force_all=1; garc.f_d=0; parc.f_d=0; g_prim=0; gr.resets=gr.reanchors=0; gr.t0=-100000;
    cond_reset(); memset(&hf,0,sizeof hf); cd.raw=0;               /* v6.9: the filters restart (the display state from DIAGNOSE is reset) */
    wsl.src=wsl.cand=39; wsl.n_sw=0; wsl.req=ACT_NONE; wsl.t_req=-100000; g_lsrc=39;
    sim_now+=100000; ui_start=sim_now-20000; ui_last=sim_now; sim_tick=NULL; sim_link=1; sim_t0=sim_now;
    memset(&wq,0,sizeof wq); wq.sim_ack=1; sim_base(); settings_load();
    memset(&bp,0,sizeof bp); bp.state=BP_DONE; bp.ra=bp.rb=BPR_OK; bp.runs=1; rx_resync=0;   /* v6.5: normal operation — the boot push has completed */
    memset(&stm,0,sizeof stm); memset(&stp,0,sizeof stp); stp.kind=ACT_NONE; memset(&rps,0,sizeof rps); memset(&lt,0,sizeof lt); ver_n=0; rx_n112=0;      /* v6.6 */
    memset(&pw,0,sizeof pw); pw.kbd_ok=1; snprintf(pw.node,sizeof pw.node,"%s · %s",INP_KBD,KBD_NAME); bl_is_off=0; ka_next=0;   /* v6.14: the button node as on the device */
    nm_reset(0); memset(&net,0,sizeof net); net.on=1; net.cfg_logged=-1; nh_probe(&net.p,sim_now,1); net.state=NS_UP; net.booted=net.was_up=net.gw_seen=1;   /* v6.7: healthy stack (DIAGNOSE: 841 / 192.168.1.100 / UP) */
    memset(&snd,0,sizeof snd); snd_uptime_ovr=86400;               /* v6.18: the sound engine restarts; the display has been up for a day (no cold boot) */
}
static void tick_brew(ms_t t){
    if(t<300){ reg[33]=0x0101; reg[51]=2; reg[52]=0; reg[39]=0; }
    else if(t<6950){ reg[33]=0x1107; reg[51]=350+(t/97)%17; reg[52]=360; reg[50]=1533; reg[39]=(uint16_t)(int16_t)-3; }
    else if(t<28900){ reg[33]=0x1307; reg[51]=(t<7300)?1481:1379-(int)((t-7300)*54/21600)+(t/131)%9; reg[52]=(t<8000)?640:150; reg[50]=1046;
        reg[39]= t<9200? 0 : (uint16_t)((t-9200)*344/(28900-9200)); }
    else { reg[33]=0x0101; reg[51]=t<29600?(int)(1300-(t-28900)*1.8):12; reg[52]=0; reg[50]=5; reg[39]=(uint16_t)(344+ (t<30500?(t-28900)*22/1600:22)); }
}
static void tick_grind(ms_t t){
    if(t<400){ reg[33]=0x0101; reg[38]=0; }
    else if(t<9900){ reg[33]=0x0141; reg[38]=(uint16_t)((t-400)*184/9500); reg[58]=640+(t/113)%40; }
    else { reg[33]=0x0101; reg[38]=186; reg[58]=5; }
}
/* v6.15: a 250 g cup on the brew scale at rest for 20 s, then a brew; the MCU tares word 39 at the start (cb_late ms after the bits,
 * 0 = the same sample, as mcu_sim). cb_flush0..1 = a wand flush (steam bits 0x4020) during the brew. Pressure 350..366 / 1340..1348. */
static int cb_late=0, cb_flush0=0, cb_flush1=0;
static void tick_cupbrew(ms_t t){
    if(t<20000){ reg[33]=0x0101; reg[51]=2; reg[52]=0; reg[39]=2500; }
    else if(t<24000){ reg[33]=0x1107; reg[51]=350+(t/97)%17; reg[52]=360; reg[50]=1533; reg[39]=t<20000+cb_late?2500:(uint16_t)(int16_t)-3; }
    else if(t<44000){ reg[33]=0x1307; reg[51]=1340+(t/131)%9; reg[52]=150; reg[50]=1046; reg[39]=t<25000?0:(uint16_t)((t-25000)*360/19000); }
    else { reg[33]=0x0101; reg[51]=12; reg[52]=0; reg[50]=5; reg[39]=362; }
    if(cb_flush1>cb_flush0&&t>=cb_flush0&&t<cb_flush1){ reg[33]|=0x4020; reg[24]=0; reg[44]=1; } else reg[44]=0;
}
static void tick_water(ms_t t){
    if(t<300){ reg[33]=0x0101; reg[39]=0; }
    else if(t<9300){ reg[33]=0x030B; reg[39]=(uint16_t)((t-300)*400/9000); reg[51]=1290+(t/89)%21; reg[52]=520; }
    else { reg[33]=0x0101; reg[39]=404; reg[51]=3; reg[52]=0; }
}
/* v6.6: display steam as in STEAMLIVE §5 — word 24 = 1 from the start write, bits 5+14, the wand switch (word 44 bit0 / word 46)
 * is NOT activated; at the target the MCU clears the bits and then word 24, and the probe keeps rising ~3.9 °C over ~11 s */
static void tick_steam(ms_t t){
    reg[44]=0x000C; reg[46]=0;
    if(t<300){ reg[33]=0x0101; reg[24]=0; }
    else { int m=226+(int)((t-300)*14/1000); reg[24]=1; reg[33]=0x4121; reg[48]=(uint16_t)(m>650?650:m); reg[47]=1262-(t/400)%9;
           if(m>=650){ ms_t ts=300+(650-226)*1000/14, d=t-ts; reg[33]=0x0101; reg[24]=0; reg[48]=(uint16_t)(650+(d>=11000?39:(int)(d*39/11000))); } }
}
/* momentary wand/flush (STEAMLIVE §6.2): bits 5+14 WITHOUT word 24 = 1; word 44 bit0 + word 46 follow approx. 0.4 s later; the probe
 * is heated past the target (650) to 695, and the steam continues until the wand is released */
static void tick_wand(ms_t t){
    reg[24]=0;
    if(t<300){ reg[33]=0x0101; reg[44]=0x000C; reg[46]=0; }
    else { int on=t>=700, m=514+(int)((t-300)*60/1000); reg[33]=0x4121; reg[44]=on?0x000D:0x000C; reg[46]=(uint16_t)on; reg[48]=(uint16_t)(m>695?695:m); reg[47]=(uint16_t)(1295-(t-300)*116/10000); }
}
/* flush DURING extraction (STEAMLIVE §7): 0x1307 | 0x4020 = 0x5327 for 3.1 s; the brew continues undisturbed */
static void tick_brew_wand(ms_t t){
    tick_brew(t); reg[24]=0; reg[44]=0x000C; reg[46]=0; reg[48]=514;
    if(t>=20600&&t<23700){ int on=t>=21000, m=514+(int)((t-20600)*60/1000); reg[33]=0x5327; reg[44]=on?0x000D:0x000C; reg[46]=(uint16_t)on; reg[48]=(uint16_t)(m>695?695:m); }
    else if(t>=23700){ int m=667-(int)((t-23700)/50); reg[48]=(uint16_t)(m<420?420:m); }
}
/* v6.13: brew (extraction) + LATCHED steam concurrently (word 24 = 1, bits 5+14 on top of the brew bits) — two STOP buttons */
static void tick_brew_latched(ms_t t){
    tick_brew(t); reg[24]=1; reg[44]=0x000C; reg[46]=0; reg[48]=(uint16_t)(300+t/60>640?640:300+t/60); reg[47]=1262;
    if(t>=300&&t<28900) reg[33]|=0x4020;
}
static void dump(const char*dir,const char*name){
    char path[400]; snprintf(path,sizeof path,"%s/%s.bin",dir,name);
    FILE*f=fopen(path,"wb"); if(!f){ perror(path); exit(1); }
    fwrite(sim_last,1,(size_t)W*H*4,f); fclose(f); printf("preview %s\n",path);
}
static void tap(int x,int y){ ui_touch(TE_DOWN,x,y,sim_now); sim_run(70); ui_touch(TE_UP,x,y,sim_now); }
static int copy_file(const char*src,const char*dst){
    char b[CFG_MAX]; int fd=open(src,O_RDONLY); if(fd<0) return 0; int n=read(fd,b,sizeof b); close(fd); if(n<=0) return 0;
    return write_all(dst,b,n,0);
}
static double msnow(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1e3+ts.tv_nsec/1e6; }

/* ---- v6.2: mock root for the boot/switch logic (preview/selftest only; never touches real paths) ---- */
enum { MB_NONE=0, MB_OURS, MB_VENDOR, MB_V61, MB_OURS_EDGE, MB_OURS_NOEXEC };
static void mk_p(const char*root,const char*rel){
    char p[520]; snprintf(p,sizeof p,"%s%s",root,rel);
    for(char*q=p+1;*q;q++) if(*q=='/'){ *q=0; mkdir(p,0755); *q='/'; }
    mkdir(p,0755);
}
static int mock_put(const char*root,const char*rel,const char*data,int n,int mode){
    char p[520]; snprintf(p,sizeof p,"%s%s",root,rel);
    int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,mode); if(fd<0) return 0;
    if(n>0&&write(fd,data,n)!=n){ close(fd); return 0; }
    close(fd); chmod(p,mode); return 1;
}
static int mock_exists(const char*root,const char*rel){ char p[520]; struct stat st; snprintf(p,sizeof p,"%s%s",root,rel); return lstat(p,&st)==0; }
static void mock_rm(const char*root,const char*rel){ char p[520]; snprintf(p,sizeof p,"%s%s",root,rel); unlink(p); }
static void mock_reset(const char*root){
    if(!strstr(root,"_preview_root")&&!strstr(root,"merakiplus_v6_selftest")){ fprintf(stderr,"mock root: unexpected path %s\n",root); exit(2); }
    char c[640]; snprintf(c,sizeof c,"rm -rf '%s'",root); if(system(c)!=0){}
    mk_p(root,"/customer/libs"); mk_p(root,"/new_customer/libs"); mk_p(root,"/proc/self"); mk_p(root,"/proc/sys/kernel/random"); mk_p(root,"/tmp");
    static const char stock[]="\x7f" "ELF mock: vendor stock demo_v9 (slot A)\n";
    mock_put(root,SLOT_A_EXE,stock,sizeof stock-1,0755);
    static const char bid[]="0f6c1b1e-52a7-4f0b-9a3d-7e1c5a9b2d44\n";
    mock_put(root,"/proc/sys/kernel/random/boot_id",bid,sizeof bid-1,0644);
    bs.m_valid=0;
}
static void mock_slot_b(const char*root,int mounted,int kind){
    char m[400]; int n=snprintf(m,sizeof m,"rootfs / rootfs rw 0 0\nubi0:rootfs / ubifs ro,relatime 0 0\nproc /proc proc rw,relatime 0 0\nubi0:customer /customer ubifs rw,relatime 0 0\n%s",
                                mounted?"ubi0:new_customer /new_customer ubifs rw,relatime 0 0\n":"");
    mock_put(root,"/proc/mounts",m,n,0644);
    mock_rm(root,SLOT_B_EXE); bs.m_valid=0;
    if(kind==MB_NONE) return;
    static char big[66000]; int bn;
    if(kind==MB_VENDOR) bn=snprintf(big,sizeof big,"\x7f" "ELF mock: vendor demo_v9 in slot B (OTA) - no merakiplus marker\n");
    else if(kind==MB_V61) bn=snprintf(big,sizeof big,"\x7f" "ELF mock: merakiplus V6.1 ... %s %%d gang(e) ...\n",MERAKIPLUS_MARK_V61);
    else if(kind==MB_OURS_EDGE){ memset(big,'.',sizeof big); bn=65530+snprintf(big+65530,sizeof big-65530,"%s\n",MERAKIPLUS_MARK); }   /* the marker straddles the 64 KiB read limit */
    else bn=snprintf(big,sizeof big,"\x7f" "ELF mock: merakiplus in slot B ... %s ...\n",MERAKIPLUS_MARK);
    mock_put(root,SLOT_B_EXE,big,bn,kind==MB_OURS_NOEXEC?0644:0755);
}
static void mock_flag(const char*root,int on){ if(on) mock_put(root,BOOT_FLAG,"",0,0644); else mock_rm(root,BOOT_FLAG); }
static void mock_self(const char*root,const char*exe){ char p[520]; snprintf(p,sizeof p,"%s/proc/self/exe",root); unlink(p); if(symlink(exe,p)!=0){} }
static void mock_proc_rm(const char*root,int pid){
    char r[64]; static const char*const f[]={"exe","comm","cmdline","stat"};
    for(int i=0;i<4;i++){ snprintf(r,sizeof r,"/proc/%d/%s",pid,f[i]); mock_rm(root,r); }
    char p[520]; snprintf(p,sizeof p,"%s/proc/%d",root,pid); rmdir(p);
}
/* state: 'T' = stopped (SIGSTOP), 'S'/'R' = running, 'Z' = zombie (empty cmdline) */
static void mock_proc(const char*root,int pid,const char*exe,const char*comm,char state,int64_t start_s){
    char r[64],b[256],p[520]; mock_proc_rm(root,pid);
    snprintf(r,sizeof r,"/proc/%d",pid); mk_p(root,r);
    snprintf(p,sizeof p,"%s/proc/%d/exe",root,pid); if(state!='Z'&&symlink(exe,p)!=0){}
    snprintf(r,sizeof r,"/proc/%d/comm",pid); int n=snprintf(b,sizeof b,"%s\n",comm); mock_put(root,r,b,n,0644);
    snprintf(r,sizeof r,"/proc/%d/cmdline",pid); n=(state=='Z')?0:snprintf(b,sizeof b,"./%s",comm); mock_put(root,r,b,n,0644);
    snprintf(r,sizeof r,"/proc/%d/stat",pid);
    n=snprintf(b,sizeof b,"%d (%s) %c 1 %d %d 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 %lld 1404928 91 4294967295\n",pid,comm,state,pid,pid,(long long)(start_s*100));
    mock_put(root,r,b,n,0644);
}
static void mock_swlog(const char*root,const char*text){ if(text) mock_put(root,SW_LOG,text,(int)strlen(text),0644); else mock_rm(root,SW_LOG); }
#define MOCK_SELF_PID 412
#define MOCK_STOCK_PID 187
#define MOCK_SW_PID 301
static const char MOCK_SWLOG[]=
 "[2026-11-17 12:40:41 UTC] switcher started (pid 301): gesture = 3\xC3\x97long-press \xE2\x89\xA5" "1500 ms in corner x<90,y<90 within 6 s\n"
 "[2026-11-17 12:40:41 UTC] input: /dev/input/event0 (name 'TECHWIN_TS', raw x 0..4095 y 0..4095) \xE2\x80\x94 READ-ONLY, no grab\n"
 "[2026-11-17 12:41:02 UTC] gesture: valid corner long-press counted (1/3)\n"
 "[2026-11-17 12:41:04 UTC] gesture: valid corner long-press counted (2/3)\n"
 "[2026-11-17 12:41:06 UTC] GESTURE TRIGGERED + mode A \xE2\x86\x92 runtime switch to merakiplus\n"
 "[2026-11-17 12:41:06 UTC] switch A\xE2\x86\x92" "B: SIGSTOP pid 187 (watchdog still sees the PID)\n"
 "[2026-11-17 12:41:06 UTC] switch A\xE2\x86\x92" "B: launched B pid 412 (/new_customer/demo_v9)\n"
 "[2026-11-17 12:41:07 UTC] switch A\xE2\x86\x92" "B: B verified (exe link) \xE2\x80\x94 SIGKILL pid 187\n"
 "[2026-11-17 12:41:07 UTC] switch A\xE2\x86\x92" "B DONE: merakiplus running, stock A exited, no reboot\n";
/* default state for previews: booted in slot B (flag set), merakiplus = /new_customer/demo_v9, switcher running */
static void mock_boot_b(const char*root){
    mock_reset(root); mock_slot_b(root,1,MB_OURS); mock_flag(root,1); mock_self(root,SLOT_B_EXE);
    mock_proc(root,MOCK_SW_PID,"/new_customer/merakiplus/boot/switcher","switcher",'S',12);
    mock_proc(root,MOCK_SELF_PID,SLOT_B_EXE,"demo_v9",'R',95);
    mock_swlog(root,MOCK_SWLOG);
    bs.self_pid=MOCK_SELF_PID; bs.t=-100000;
}
/* run.sh mode: A boot (no flag), merakiplus run from /new_customer/merakiplus, stock UI SIGSTOPped, no switcher */
static void mock_runsh(const char*root,int64_t stock_start_s){
    mock_reset(root); mock_slot_b(root,1,MB_OURS); mock_flag(root,0); mock_self(root,"/new_customer/merakiplus/merakiplus");
    mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',stock_start_s);
    mock_proc(root,MOCK_SELF_PID,"/new_customer/merakiplus/merakiplus","merakiplus",'R',5200);
    mock_swlog(root,NULL);
    bs.self_pid=MOCK_SELF_PID; bs.t=-100000;
}
static int file_has(const char*path,const char*needle){
    static char b[65536]; int fd=open(path,O_RDONLY); if(fd<0) return 0; int n=read(fd,b,sizeof b-1); close(fd); if(n<0)n=0; b[n]=0; return strstr(b,needle)!=NULL;
}
static int file_lines(const char*path){ FILE*f=fopen(path,"r"); if(!f) return -1; int n=0,c; while((c=fgetc(f))!=EOF) if(c=='\n') n++; fclose(f); return n; }
static void file_print(const char*path,const char*pre){ FILE*f=fopen(path,"r"); if(!f) return; char ln[600]; while(fgets(ln,sizeof ln,f)) printf("%s%s",pre,ln); fclose(f); }
static int dir_count(const char*path){ DIR*d=opendir(path); if(!d) return -1; int n=0; struct dirent*de; while((de=readdir(d))) if(strcmp(de->d_name,".")&&strcmp(de->d_name,"..")) n++; closedir(d); return n; }

static int copy_big(const char*src,const char*dst,int mode){
    int a=open(src,O_RDONLY); if(a<0) return 0; int b=open(dst,O_WRONLY|O_CREAT|O_TRUNC,mode); if(b<0){ close(a); return 0; }
    static char buf[65536]; int n,ok=1; while((n=read(a,buf,sizeof buf))>0) if(write(b,buf,n)!=n){ ok=0; break; }
    close(a); close(b); chmod(dst,mode); return ok&&n==0;
}
/* mock "stock" for the launch selftest: a copy of this binary, placed as <root>/customer/demo_v9 and started
 * by switch_launch(). It reports what a real demo_v9 would inherit, and stays alive a little longer. */
static int mock_stock_main(const char*argv0){
    const char*out=getenv("MERAKIPLUS_MOCK_STOCK"),*rel=getenv("MERAKIPLUS_MOCK_RELEASED"),*ld=getenv("LD_LIBRARY_PATH");
    char cwd[600]=""; if(!getcwd(cwd,sizeof cwd)) cwd[0]=0;
    int nfd=0; for(int fd=3;fd<256;fd++) if(fcntl(fd,F_GETFD)!=-1) nfd++;
    struct stat a,b; int nul=stat("/dev/null",&a)==0; for(int fd=0;fd<3&&nul;fd++) if(fstat(fd,&b)!=0||b.st_rdev!=a.st_rdev) nul=0;
    int released=rel&&access(rel,F_OK)==0;
    FILE*f=fopen(out,"w");
    if(f){ fprintf(f,"pid=%d sid=%d argv0=%s cwd=%s extra_fds=%d stdio_null=%d released_before_start=%d\nLD=%s\n",(int)getpid(),(int)getsid(0),argv0,cwd,nfd,nul,released,ld?ld:""); fclose(f); }
    if(getenv("MERAKIPLUS_MOCK_STOCK_FAIL")) return 3;
    usleep(900000); return 0;
}
static const char*self_bin=NULL;
static int st_rel=0, st_reacq=0, st_relfd=-1; static const char*st_relfile=NULL;
static void st_release_hook(void){ st_rel++; if(st_relfd>=0){ close(st_relfd); st_relfd=-1; } if(st_relfile){ int fd=open(st_relfile,O_WRONLY|O_CREAT,0644); if(fd>=0) close(fd); } }
static void st_reacq_hook(void){ st_reacq++; }
static double msnow(void);
static int st_fail=0, st_n=0;
static void st_check(const char*name,int ok){ printf("  [%s] %s\n",ok?"PASS":"FAIL",name); st_n++; if(!ok) st_fail++; }
static int hexeq(const uint8_t*b,int n,const char*hex){
    int i=0; while(*hex){ while(*hex==' ')hex++; if(!*hex) break; unsigned v; if(sscanf(hex,"%2x",&v)!=1) return 0; hex+=2; if(i>=n||b[i]!=v) return 0; i++; }
    return i==n;
}
/* independent audit of a queued frame (does NOT reuse frame_policy): no trigger word != 0 beyond 'allowed' */
static int frame_trig_audit(const WFrame*f,uint32_t allowed){
    for(int k=0;k<f->len;k++){
        int idx=f->sub+k; if(idx>=32||f->w[k]==0) continue;
        if(idx==26||idx==27||idx==30) return 0;
        if((idx==3||idx==6||idx==9||idx==16||idx==22||idx==24)&&!(allowed&WBIT(idx))) return 0;
    }
    return 1;
}
static int frame_covers(const WFrame*f,int idx){ return idx>=f->sub&&idx<f->sub+f->len; }
static int q_hex(int k,const char*hex){ uint8_t b[80]; int n=build_write(b,wq.q[k].sub,wq.q[k].w,wq.q[k].len); return hexeq(b,n,hex); }
static int rej(uint8_t sub,const uint16_t*w,int len,uint32_t intent,int why){ int n0=wq.n; int r=wq_push_t(sub,w,len,intent); int ok=(r==0&&wq_rej==why&&wq.n==n0); wq.n=n0; return ok; }
#define W1(v) (uint16_t[1]){(uint16_t)(v)}
/* like chord_half, but inside the gauge ring's inner edge (r 204) — for views where the ring is drawn */
static float ring_half(int y,int h){ int d0=abs(y-CY), d1=abs(y+h-CY); int d=d0>d1?d0:d1; return d>=204?0:sqrtf((float)(204*204-d*d)); }
static float chord_half(int y,int h){ int d0=abs(y-CY), d1=abs(y+h-CY); int d=d0>d1?d0:d1; return d>=RSAFE?0:sqrtf((float)(RSAFE*RSAFE-d*d)); }
static void sim_ready(ms_t now){   /* minimal "READY" state without simulator */
    memset(reg,0,sizeof reg); for(int i=0;i<REGN;i++) reg_seen[i]=1;
    reg[0]=1; reg[33]=0x0101; reg[11]=870; reg[40]=900; reg[23]=360; reg[112]=0x0104; reg[113]=0x0605; reg[114]=70; last_rx_ms=now;
}
/* ---- v6.3: i18n helpers ---- */
#define LAY(w,lim,what,st) do{ if((w)>(lim)){ ok=0; printf("     [%s] %s: '%s' is %.0f px, room %.0f px\n",LANG_CODE[ui_lang],what,st,(double)(w),(double)(lim)); } }while(0)
static const char* stl(const char*name){ static char b[4][420]; static int k; char*o=b[k++&3]; snprintf(o,420,"[%s] %s",LANG_CODE[ui_lang],name); return o; }
/* pixel budget per string class (font, max width) — same numbers as the drawing code uses */
static const struct { int font,maxw; const char*what; } KB[NKLASS]={
 [KT]={F_XS,TOAST_TW,"toast"},[KSUB]={F_XS,SUB_W-8,"HOME subline"},[KLAB]={F_XS,254,"HOME label (field 260)"},[KCHIP]={F_S,150,"phase chip"},
 [KBTN]={F_XS,104,"button"},[KBTNS]={F_S,104,"button"},[KTITLE]={F_M,224,"page title"},[KPSUB]={F_XS,298,"page subtitle"},[KSPL]={F_XS,SPLASH_W-8,"tagline"},
 [KDL]={F_XS,170,"diagnose label"},[KDV]={F_S,168,"diagnose value"},[KDF]={F_XS,DG_FULL_W,"full-width line"},[KNOTE]={F_XS,392,"note (field 400)"},
 [KF1]={F_XS,264,"footer 1 (clear of the gauge ends)"},[KF2]={F_XS,269,"footer 2"},[KXSUB]={F_XS,300,"row subline"},[KST]={F_XS,348,"status (field 360)"},
};
static const char*const KNAME[NKLASS]={"-","toast","home-sub","home-label","phase-chip","button","button","page-title","page-sub","tagline","diag-label","diag-value","full-line","note","foot-1","foot-2","row-sub","status","console"};
/* the printf conversions in a string, in order ("%s|%.34s|%ld|") */
static void fmt_sig(const char*s,char*o,size_t n){
    size_t k=0; o[0]=0;
    for(;*s;s++){ if(*s!='%') continue; if(s[1]=='%'){ s++; continue; }
        const char*e=s+1; while(*e&&!strchr("sduXxfc",*e)) e++; if(!*e) break;
        for(const char*q=s;q<=e&&k+2<n;q++) o[k++]=*q; if(k+2<n) o[k++]='|'; o[k]=0; s=e; }
}
/* worst-case fill: %s -> "888.8", integer -> "88888", %f -> "88.8" (for width measurement) */
static void fmt_fill(const char*s,char*o,size_t n){
    size_t k=0;
    for(;*s&&k+8<n;s++){
        if(*s!='%'){ o[k++]=*s; continue; } if(s[1]=='%'){ o[k++]='%'; s++; continue; }
        const char*e=s+1; while(*e&&!strchr("sduXxfc",*e)) e++; if(!*e) break;
        const char*r=*e=='s'?"888.8":(*e=='f'?"88.8":"88888"); while(*r) o[k++]=*r++; s=e; }
    o[k]=0;
}
/* number signature: all digits + '.'/',' that sit BETWEEN two digits — must be identical in all languages */
static void num_sig(const char*s,char*o,size_t n){
    size_t k=0;
    for(const char*p=s;*p&&k+1<n;p++){
        if(isdigit((unsigned char)*p)) o[k++]=*p;
        else if((*p=='.'||*p==',')&&p>s&&isdigit((unsigned char)p[-1])&&isdigit((unsigned char)p[1])) o[k++]=*p; }
    o[k]=0;
}
/* visible text (upper-cased, without code spans and control chars) — for name checks */
static void vis_text(const char*s,char*o,size_t n,int keep_code){
    size_t k=0; int lc=0;
    for(const unsigned char*p=(const unsigned char*)s;*p&&k+1<n;p++){
        if(*p==0x0E){ lc=1; continue; } if(*p==0x0F){ lc=0; continue; }
        if(lc&&!keep_code) continue;
        o[k++]=(char)toupper(*p); }
    o[k]=0;
}
/* does the stroke font have a glyph for every char? (unknown chars are otherwise drawn as '?') */
static int glyphs_ok(const char*s,char*bad,size_t bn){
    const unsigned char*p=(const unsigned char*)s; g_lc=0;
    while(*p){ const unsigned char*q=p; int g=glyph_next(&p);
        if(g<0) continue;
        if((g=='?'&&*q!='?')||g>=NG||!GD[g].path){ snprintf(bad,bn,"%.*s",(int)(p-q),(const char*)q); g_lc=0; return 0; } }
    g_lc=0; return 1;
}
static int put_bytes(const char*path,const char*d,int n){ int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd<0) return 0; int ok=n==0||write(fd,d,n)==n; close(fd); return ok; }
static int file_is(const char*path,const char*want){ char b[64]; int fd=open(path,O_RDONLY); if(fd<0) return 0; int n=(int)read(fd,b,sizeof b); close(fd); return n==(int)strlen(want)&&!memcmp(b,want,n); }
static int lang_from(const char*path,const char*d,int n){ put_bytes(path,d,n); ui_lang=LANG_DA; static_dirty=0; return lang_load(); }   /* starting point DA: an unknown value must actively yield EN */
