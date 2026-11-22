/* src/mp/bootslot.c — boot slot + runtime switch to the stock UI.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ====================================================================== */
/* ================= BOOT SLOT + RUNTIME SWITCH (v6.2) ================== */
/* ====================================================================== */
/* Vendor boot (artifacts/demo.sh): the flag file /customer/new selects slot B (/new_customer)
 * over slot A (/customer). The watchdog /customer/test.sh only checks whether there is a
 * process whose ps line contains "demo_v9". In the boot integration merakiplus lives as
 * /new_customer/demo_v9 (slot B); the stock UI is /customer/demo_v9 (slot A).
 *
 *   BOOT UI (persistent)  = create/remove /customer/new. Applies to the NEXT reboot. Only allowed
 *                           when /new_customer is mounted AND its demo_v9 is merakiplus — otherwise
 *                           the flag belongs to the vendor's OTA mechanism, and we do not touch it.
 *   SWITCH TO ORIGINAL NOW = runtime, without reboot (BOOT.md §9.2): merakiplus closes UART/fb/touch,
 *                           starts /customer/demo_v9 directly and exits 400 ms later.
 * merakiplus NEVER sends a signal to any process — neither kill, SIGSTOP nor SIGCONT. */
/* (BOOT_FLAG, SLOT_*_EXE, SLOT_B_MNT and SW_LOG are defined at the string table above) */
/* what(1) marker: this is how the BOOT UI guard recognises a Meraki+ binary in slot B (the "@(#)merakiplus "
 * prefix is the needle). v6.1 had no marker and is recognised by a string from its exit message. */
#define MERAKIPLUS_MARK_HEAD "@(#)merakiplus "
static const char MERAKIPLUS_MARK[]=MERAKIPLUS_MARK_HEAD MERAKIPLUS_VERSION " meraki-plus display-ui";
#define MERAKIPLUS_MARK_N ((int)sizeof MERAKIPLUS_MARK_HEAD-1)
static const char MERAKIPLUS_MARK_V61[]="== merakiplus: config.json aendret";

enum { SLOT_OTHER=0, SLOT_A, SLOT_B };
enum { BL_OK=0, BL_NOMOUNT, BL_NOBIN, BL_VENDOR };
#define STOCK_MAX 4
static struct {
    int flag;                       /* /customer/new exists */
    int b_mnt, b_bin, b_ours, lock; /* slot B: mounted / demo_v9 executable / is merakiplus / BL_* */
    int a_bin;                      /* /customer/demo_v9 exists and is executable */
    int self_pid, self_slot; char self_exe[128];
    int stock[STOCK_MAX], n_stock, stock_run;   /* /customer/demo_v9 processes; stock_run = number NOT stopped */
    int sw_pid;                     /* pidof switcher (0 = not running) */
    int sw_log; char sw_evt[100];   /* 0 = log not readable, 1 = no switch line, 2 = event */
    ms_t t;
    dev_t m_dev; ino_t m_ino; off_t m_size; time_t m_mtime; int m_ours, m_valid;   /* marker cache */
} bs={.t=-100000};

static int slot_b_mounted(void){
    FILE*f=fopen(rp("/proc/mounts"),"r"); if(!f) return 0;
    char ln[512],dev[160],mnt[160]; int hit=0;
    while(fgets(ln,sizeof ln,f)) if(sscanf(ln,"%159s %159s",dev,mnt)==2&&!strcmp(mnt,SLOT_B_MNT)) hit=1;
    fclose(f); return hit;
}
/* does the file contain the merakiplus marker? (cached on dev/ino/size/mtime — the binary is only read when it has changed) */
static int bin_is_merakiplus(const char*path){
    struct stat st; if(stat(path,&st)!=0||!S_ISREG(st.st_mode)){ bs.m_valid=0; return 0; }
    if(bs.m_valid&&st.st_dev==bs.m_dev&&st.st_ino==bs.m_ino&&st.st_size==bs.m_size&&st.st_mtime==bs.m_mtime) return bs.m_ours;
    int ours=0, fd=open(path,O_RDONLY);
    if(fd>=0){
        static char buf[65536+64]; int keep=0,n; const int ov=(int)sizeof MERAKIPLUS_MARK_V61;
        while((n=read(fd,buf+keep,65536))>0){
            int tot=keep+n;
            if(memmem(buf,tot,MERAKIPLUS_MARK,MERAKIPLUS_MARK_N)||memmem(buf,tot,MERAKIPLUS_MARK_V61,sizeof MERAKIPLUS_MARK_V61-1)){ ours=1; break; }
            keep=tot<ov?tot:ov; memmove(buf,buf+tot-keep,keep);
        }
        close(fd);
    }
    bs.m_dev=st.st_dev; bs.m_ino=st.st_ino; bs.m_size=st.st_size; bs.m_mtime=st.st_mtime; bs.m_ours=ours; bs.m_valid=1;
    return ours;
}
/* /proc/<pid>/stat: state (field 3) and start time in seconds since boot (field 22 / 100) */
static int64_t proc_stat(int pid,char*state){   /* v6.6: 64 bit — the start time is in ticks (100/s) and passes 2^31 after 248 days of uptime */
    char p[420],b[640]; snprintf(p,sizeof p,"%s/proc/%d/stat",sysroot,pid);
    int fd=open(p,O_RDONLY); if(fd<0) return -1; int n=read(fd,b,sizeof b-1); close(fd); if(n<=0) return -1; b[n]=0;
    char*q=strrchr(b,')'); if(!q) return -1; q++;
    int64_t start=-1; int idx=0;
    for(char*t=strtok(q," \n");t;t=strtok(NULL," \n"),idx++){
        if(idx==0&&state) *state=t[0];
        if(idx==19){ start=atoll(t)/100; break; }
    }
    return start;
}
static void proc_scan(void){
    bs.n_stock=0; bs.stock_run=0; bs.sw_pid=0;
    DIR*d=opendir(rp("/proc")); if(!d) return; struct dirent*de;
    while((de=readdir(d))){
        if(de->d_name[0]<'1'||de->d_name[0]>'9') continue;
        int pid=atoi(de->d_name); if(pid<=0||pid==bs.self_pid) continue;
        char p[420],l[200]; snprintf(p,sizeof p,"%s/proc/%d/exe",sysroot,pid);
        ssize_t r=readlink(p,l,sizeof l-1);
        if(r>0){ l[r]=0;
            size_t al=strlen(SLOT_A_EXE);                 /* also ".../demo_v9 (deleted)" = the binary replaced under a running process */
            if(!strncmp(l,SLOT_A_EXE,al)&&(l[al]==0||l[al]==' ')&&bs.n_stock<STOCK_MAX){ char stt='?'; proc_stat(pid,&stt); bs.stock[bs.n_stock++]=pid; if(stt!='T'&&stt!='t') bs.stock_run++; } }
        snprintf(p,sizeof p,"%s/proc/%d/comm",sysroot,pid);
        int fd=open(p,O_RDONLY);
        if(fd>=0){ int n=read(fd,l,sizeof l-1); close(fd); if(n>0){ l[n]=0; l[strcspn(l,"\n")]=0; if(!strcmp(l,"switcher")&&!bs.sw_pid) bs.sw_pid=pid; } }
    }
    closedir(d);
}
static void utf8_trim(char*s){
    int L=(int)strlen(s), i=L; while(i>0&&((unsigned char)s[i-1]&0xC0)==0x80) i--;
    if(i>0){ unsigned char c=(unsigned char)s[i-1]; int need=c>=0xF0?4:(c>=0xE0?3:(c>=0xC0?2:1)); if(L-(i-1)<need) s[i-1]=0; }
}
/* a switch event in switcher.log: "switch A→B …" / "switch FAILED …" / "GESTURE TRIGGERED …";
 * "skift …" / "GESTUS …" are the same lines from switcher builds before the English log text */
static int swlog_is_event(const char*p){
    const char*t=p; if(p[0]=='['){ const char*q=strstr(p,"] "); if(q) t=q+2; }
    return !strncmp(t,"switch ",7)||strstr(t,"GESTURE TRIGGERED")||strstr(t,"skift")||strstr(t,"GESTUS");
}
/* latest switch event in switcher.log: the last event line -> "HH:MM:SS text" */
static void swlog_scan(void){
    bs.sw_log=0; bs.sw_evt[0]=0;
    int fd=open(rp(SW_LOG),O_RDONLY); if(fd<0) return;
    static char buf[4097]; off_t sz=lseek(fd,0,SEEK_END); off_t st=sz>4096?sz-4096:0; lseek(fd,st,SEEK_SET);
    int n=read(fd,buf,4096); close(fd); if(n<0) return; buf[n]=0; bs.sw_log=1;
    char*last=NULL;
    for(char*p=buf;p&&*p;){ char*e=strchr(p,'\n'); if(e)*e=0; if((st==0||p!=buf)&&swlog_is_event(p)) last=p; p=e?e+1:NULL; }
    if(!last) return;
    /* "[YYYY-MM-DD HH:MM:SS UTC] text" */
    if(last[0]=='['&&strlen(last)>26&&last[11]==' '&&last[24]==']') snprintf(bs.sw_evt,sizeof bs.sw_evt-4,"%.8s %s",last+12,last+26);
    else snprintf(bs.sw_evt,sizeof bs.sw_evt-4,"%s",last);
    utf8_trim(bs.sw_evt);                                  /* snprintf may have cut in the middle of a character */
    bs.sw_log=2;
}
static void boot_refresh(void){
    struct stat st;
    if(!bs.self_pid) bs.self_pid=(int)getpid();
    bs.flag=stat(rp(BOOT_FLAG),&st)==0;
    bs.b_mnt=slot_b_mounted();
    bs.b_bin=bs.b_mnt&&access(rp(SLOT_B_EXE),X_OK)==0;
    bs.b_ours=bs.b_bin&&bin_is_merakiplus(rp(SLOT_B_EXE));
    bs.lock=!bs.b_mnt?BL_NOMOUNT:(!bs.b_bin?BL_NOBIN:(!bs.b_ours?BL_VENDOR:BL_OK));
    bs.a_bin=access(rp(SLOT_A_EXE),X_OK)==0;
    ssize_t r=readlink(rp("/proc/self/exe"),bs.self_exe,sizeof bs.self_exe-1); bs.self_exe[r>0?r:0]=0;
    bs.self_slot=!strcmp(bs.self_exe,SLOT_B_EXE)?SLOT_B:(!strcmp(bs.self_exe,SLOT_A_EXE)?SLOT_A:SLOT_OTHER);
    proc_scan(); swlog_scan();
}
static void boot_log(const char*fmt,...){
    char p[340]; mkdir(cfg_bakdir,0755); snprintf(p,sizeof p,"%s/boot_ui.log",cfg_bakdir);
    FILE*f=fopen(p,"a"); if(!f) return;
    time_t t=time(NULL); struct tm tmv; gmtime_r(&t,&tmv); char ts[32]; strftime(ts,sizeof ts,"%Y-%m-%dT%H:%M:%SZ",&tmv);
    fprintf(f,"%s ",ts); va_list ap; va_start(ap,fmt); vfprintf(f,fmt,ap); va_end(ap); fputc('\n',f);
    fflush(f); fsync(fileno(f)); fclose(f);
}
/* BOOT UI: set (want_b=1, touch) or remove (want_b=0, rm) the flag. Idempotent. The guard
 * applies to BOTH directions. Returns 1 = the flag is as requested; 0 = nothing changed (boot_err set). */
static char boot_err[72]="";
static int boot_flag_set(int want_b){
    boot_refresh(); boot_err[0]=0;
    if(bs.lock!=BL_OK){ snprintf(boot_err,sizeof boot_err,"locked (%s)",bs.lock==BL_NOMOUNT?"/new_customer not mounted":(bs.lock==BL_NOBIN?"demo_v9 missing in slot B":"slot B is not merakiplus")); return 0; }
    int before=bs.flag; const char*p=rp(BOOT_FLAG); struct stat st;
    if(want_b){ int fd=open(p,O_WRONLY|O_CREAT,0644); if(fd<0){ snprintf(boot_err,sizeof boot_err,"touch: %s",strerror(errno)); return 0; } fsync(fd); close(fd); }
    else if(unlink(p)!=0&&errno!=ENOENT){ snprintf(boot_err,sizeof boot_err,"rm: %s",strerror(errno)); return 0; }
    fsync_dir(p); SYS_SYNC                                 /* BOOT.md §9.1: open(O_CREAT)/unlink + sync() (SIM: no global sync) */
    bs.flag=stat(p,&st)==0;
    if(bs.flag!=(want_b!=0)){ snprintf(boot_err,sizeof boot_err,"verification failed"); return 0; }
    if(before!=bs.flag) boot_log("BOOT-UI: flag %s %s (%d->%d): %s at next reboot",BOOT_FLAG,bs.flag?"created":"removed",before,bs.flag,bs.flag?"MERAKIPLUS (slot B)":"ORIGINAL (slot A)");
    return 1;
}
/* is a stock UI with an old RAM mirror waiting? True when RESTART_NEEDED exists AND a
 * /customer/demo_v9 is running that was started BEFORE the latest SAVE (same boot). A freshly started
 * stock UI has read config.json itself — then there is nothing to remind about. */
static int restart_pending(void){
    char p[340],b[1024]; snprintf(p,sizeof p,"%s/RESTART_NEEDED",cfg_bakdir);
    int fd=open(p,O_RDONLY); if(fd<0) return 0; int n=read(fd,b,sizeof b-1); close(fd); if(n<0)n=0; b[n]=0;
    if(bs.n_stock==0) return 0;
    char*q=strstr(b,"boot_id="),*m=strstr(b,"mono_s="); char was[64]="",cur[64];
    if(!q||!m||sscanf(q+8,"%63s",was)!=1||!strcmp(was,"?")) return 1;       /* v6.1 flag without a stamp: assume it applies */
    boot_id_read(cur,sizeof cur); if(!cur[0]) return 1;
    if(strcmp(cur,was)) return 0;                                           /* the machine has rebooted since the SAVE */
    int64_t t=atoll(m+7);
    for(int i=0;i<bs.n_stock;i++){ int64_t s=proc_stat(bs.stock[i],NULL); if(s<0||s<=t) return 1; }
    return 0;
}

/* ---- runtime switch: LD_LIBRARY_PATH for slot A ----
 * BOOT.md §9.2 specifies "/customer/libs:/config/wifi". demo.sh's A branch however sets
 * "$LD_LIBRARY_PATH:/customer/libs:/config/wifi" — i.e. the boot environment's prefix is kept. We do
 * the same: inherit our own path, remove the slot components (B's and A's libs + /config/wifi)
 * and append A's. Without a prefix (e.g. when the switcher has started us) the result is EXACTLY
 * "/customer/libs:/config/wifi"; with a prefix it is what a real A boot would give the stock UI. */
static void ld_path_for_a(const char*inh,char*out,size_t n){
    size_t o=0; out[0]=0;
    static const char*const drop[]={"/new_customer/libs","/customer/libs","/config/wifi"};
    for(const char*p=inh;p&&*p;){
        const char*e=strchr(p,':'); size_t l=e?(size_t)(e-p):strlen(p); int skip=(l==0);
        for(unsigned k=0;k<3&&!skip;k++) if(l==strlen(drop[k])&&!memcmp(p,drop[k],l)) skip=1;
        for(size_t i=0;i<l&&!skip;i++){ unsigned char c=(unsigned char)p[i]; if(c=='\''||c<0x21||c==0x7F) skip=1; }   /* must be able to sit inside '...' */
        if(!skip&&o+l+1+32<n){ memcpy(out+o,p,l); o+=l; out[o++]=':'; out[o]=0; }
        p=e?e+1:NULL;
    }
    snprintf(out+o,n-o,"/customer/libs:/config/wifi");
}
/* ---- runtime switch B->A: BOOT.md §9.2 steps 2 + 3 ----
 * Called AFTER merakiplus has closed UART/fb/touch (step 1, see do_switch). The child: own session
 * (setsid), cwd /customer, slot A's LD_LIBRARY_PATH, stdio -> /dev/null and ALL other fds
 * closed (an inherited touch fd would keep EVIOCGRAB alive inside the stock UI), then
 * execl("/customer/demo_v9","./demo_v9") — argv0 contains "demo_v9", so the watchdog grep
 * matches. The parent waits 400 ms while A initialises fb/UART; if the child has already died,
 * an error is reported, and merakiplus takes the screen back. Returns: stock pid (>0) or 0 w. err set. */
#define SW_WAIT_MS 400
enum { SWM_LAUNCH=0, SWM_CONT };
static int sw_dry=0;                                       /* MERAKIPLUS_SWITCH_DRY=1: log only */
static char sw_ldp[512]="";                                /* LD_LIBRARY_PATH given to stock (log/selftest) */
static int switch_mode(void){ return bs.n_stock>0?SWM_CONT:SWM_LAUNCH; }   /* SWM_CONT = run.sh mode: stock exists (paused) */
static int switch_locked(void){ return bs.n_stock==0&&!bs.a_bin; }         /* no stock UI to switch to */
/* what demo.sh starts at the next boot, given the flag + slot B's state. The flag alone is not
 * enough: without a mount slot A starts, and with a vendor binary in slot B it is NOT merakiplus. */
enum { BN_ORIGINAL=0, BN_OURS, BN_SLOTB };
_Static_assert(TX_BN_OURS==TX_BN_ORIGINAL+BN_OURS&&TX_BN_SLOTB==TX_BN_ORIGINAL+BN_SLOTB,"TX_BN_* must follow BN_*");
static int boot_next_kind(void){
    if(!bs.flag||bs.lock==BL_NOMOUNT) return BN_ORIGINAL;
    return bs.lock==BL_OK?BN_OURS:BN_SLOTB;
}
static const char* boot_next_name(void){ return TR(TX_BN_ORIGINAL+boot_next_kind()); }
enum { SWF_NONE=0, SWF_NOBIN, SWF_FORK, SWF_DIED };
static int sw_fail=SWF_NONE;                               /* why the latest launch failed (-> localised toast; err goes to the log) */
static int switch_launch(char*err,size_t en){
    char exe[420],dir[420]; snprintf(exe,sizeof exe,"%s",rp(SLOT_A_EXE)); snprintf(dir,sizeof dir,"%s",rp("/customer"));
    ld_path_for_a(getenv("LD_LIBRARY_PATH"),sw_ldp,sizeof sw_ldp); sw_fail=SWF_NONE;
    if(access(exe,X_OK)!=0){ sw_fail=SWF_NOBIN; snprintf(err,en,"%s missing",SLOT_A_EXE); return 0; }
    fflush(stdout); fflush(stderr);
    pid_t c=fork(); if(c<0){ sw_fail=SWF_FORK; snprintf(err,en,"fork: %s",strerror(errno)); return 0; }
    if(c==0){
        setsid();
        if(chdir(dir)!=0) _exit(126);
        setenv("LD_LIBRARY_PATH",sw_ldp,1);
        int nul=open("/dev/null",O_RDWR); if(nul>=0){ dup2(nul,0); dup2(nul,1); dup2(nul,2); }
        for(int fd=3;fd<1024;fd++) close(fd);
        execl(exe,"./demo_v9",(char*)NULL);
        _exit(127);
    }
    for(ms_t t0=now_ms();now_ms()-t0<SW_WAIT_MS;){          /* clock-driven: 400 ms regardless of timer resolution */
        struct timespec sl={0,10*1000*1000}; nanosleep(&sl,NULL);
        int st; if(waitpid(c,&st,WNOHANG)==c){ sw_fail=SWF_DIED; snprintf(err,en,"stock UI exited immediately (rc %d)",WIFEXITED(st)?WEXITSTATUS(st):-1); return 0; }
    }
    return (int)c;
}

