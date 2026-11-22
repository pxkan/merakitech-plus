/* src/mp/net.c — Wi-Fi at startup + self-heal (net guard), device HAL / model, the net state machine.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ====================================================================== */
/* ================= NET: wifi at startup + self-heal (v6.7) ============ */
/* ====================================================================== */
/* The stock UI brings wifi up ITSELF (libmi_wlan.so driven by /config/wifi/wlan.json) — demo.sh does
 * not. On a slot B boot the stock UI never starts, and the machine had no network (INSTALL-REPORT §3.1):
 * neither the driver (8188fu.ko), wpa_supplicant nor DHCP was started. The mechanism is reverse-proven on
 * the device (wlan.json, ssw01bInit.sh, libmi_wlan.so, demo_v9's _wifi_init/_wifi_open/_wifi_connect and
 * stock's own strace capture; see UI_v6.md "CHANGES-v6.7" + net_re/NET-REVERSE.md). Stock does:
 *   1. MI_WLAN_Init:    sh -c "source /config/wifi/ssw01bInit.sh dummy1 dummy2 "   (mkdir /tmp/wifi/run,
 *                       insmod /config/wifi/8188fu.ko, mdev -s, wait for wlan0 for up to 10 s)
 *   2. MI_WLAN_Open:    sh -c "ifconfig wlan0 up  ; wpa_supplicant -i wlan0 -Dnl80211 -c
 *                       /appconfigs/wpa_supplicant.conf  & sleep 1  ; "
 *   3. MI_WLAN_Connect: wpa_cli add_network / set_network ssid, psk, key_mgmt WPA-PSK / select_network /
 *                       enable_network / save_config  — save_config writes the network block into
 *                       /appconfigs/wpa_supplicant.conf (demo.sh copies .conf_bak over it on EVERY boot)
 *                       and:  udhcpc -i wlan0 -s /etc/init.d/udhcpc.script -a -q -b -t 20 -T 1 &
 *      (-q: udhcpc exits once it has a lease — which is why there is no udhcpc in normal operation)
 *   Environment for the children: LD_LIBRARY_PATH and PATH get ":/config/wifi" appended.
 * merakiplus does the same, but only when the stack is DOWN:
 *   - the init script is run ONLY when wlan0 does not exist (an insmod of a loaded driver fails);
 *   - the conf file is written directly from config.json (ssid/password) in EXACTLY the format save_config produces
 *     (byte-identical, md5-proven) — and only when the content differs; then wpa_supplicant is started with the
 *     same arguments, and 1 s later udhcpc with the same arguments — unless wlan0 still has its
 *     IP and default route (heal after a dead supplicant): then they are kept (same IP; stock never runs
 *     udhcpc again in operation either, -q);
 *   - a live wpa_supplicant is NEVER killed — merakiplus does not send a signal to any process (0 kill calls).
 *     If it is alive but wlan0 is administratively DOWN, "ifconfig wlan0 up" is done (like MI_WLAN_Open).
 *     If the IP is missing (or the default route, if there has been one) and no udhcpc is running, udhcpc is started
 *     (after NET_NOIP_MS / NET_NOGW_MS, with backoff). Without carrier it waits (the supplicant reconnects by itself).
 * Everything is non-blocking: the main loop calls net_step(); slow steps are child processes.
 * Log: /tmp/merakiplus_net.log (transitions only; the password is never written). MERAKIPLUS_NONET=1 turns the watchdog off. */
#define NET_IF          "wlan0"
#define NET_WIFI_DIR    "/config/wifi"
#define NET_WPA_BIN     NET_WIFI_DIR "/wpa_supplicant"
#define NET_INIT_SH     NET_WIFI_DIR "/ssw01bInit.sh"
#define NET_CONF        "/appconfigs/wpa_supplicant.conf"
#define NET_CONF_BAK    "/appconfigs/wpa_supplicant.conf_bak"
#define NET_RUN_DIR     "/tmp/wifi/run"
#define NET_CTRL        NET_RUN_DIR "/wpa_supplicant"
#define NET_DHCP_BIN    "/sbin/udhcpc"
#define NET_DHCP_SH     "/etc/init.d/udhcpc.script"
#define NET_LOG         "/tmp/merakiplus_net.log"
#define NET_LOG_MAX     131072
#define NET_CONF_HDR    "ctrl_interface=" NET_CTRL "\nupdate_config=1\n"
#define NET_PROBE_MS       1000     /* while something is in progress */
#define NET_PROBE_UP_MS    2000     /* healthy stack: one /proc lookup + carrier + SIOCGIFADDR */
#define NET_PROBE_IDLE_MS  5000     /* OFF / waiting for the next attempt */
#define NET_DEAD_MS        3000     /* wpa_supplicant gone this long -> bring-up (at startup: immediately) */
#define NET_WPA_SETTLE_MS  1000     /* stock: "& sleep 1" */
#define NET_NOIP_MS        8000     /* supplicant alive, no IP, no udhcpc -> udhcpc */
#define NET_NOGW_MS        3000     /* carrier + IP, but the default route is gone (e.g. after ifconfig down/up) -> udhcpc */
#define NET_IFDOWN_MS      3000     /* supplicant alive, wlan0 administratively down -> ifconfig wlan0 up */
#define NET_DHCP_MAX_MS    60000    /* backoff ceiling for repeated udhcpc starts within the same outage */
#define NET_INIT_TMO_MS    25000    /* the init script itself waits at most ~10 s for wlan0; after that it is logged (not killed) */
#define NET_BACKOFF0_MS    5000
#define NET_BACKOFF_MAX_MS 60000
#define NET_CFG_RECHECK_MS 30000
enum { NS_OFF=0, NS_DOWN, NS_STARTING, NS_CONNECTING, NS_UP };
enum { NB_NONE=0, NB_INIT, NB_WPA };
enum { NC_OK=0, NC_NOCFG, NC_WIFIOFF, NC_NOSSID, NC_BADPSK };
/* ip: a.b.c.d = a<<24|b<<16|c<<8|d; 0 = none. ifup = IFF_UP ("ifconfig wlan0 up"). gw = default route via wlan0 */
typedef struct { int iface, ifup, wpa_pid, dhcp_pid, carrier, gw; uint32_t ip; } NetProbe;
static struct {
    int on, state, step, cfg, cfg_logged, booted, was_up, acted, dead, job, orphan, init_hung, backoff, gw_seen, carrier_was, dhcp_wait, ifdown;
    NetProbe p;
    ms_t t_probe, t_dead, t_noip, t_step, t_retry, t_down, t_cfg, t_ifdown, heal_ms;
    int n_heal, n_boot, n_init, n_wpa, n_dhcp, n_conf, n_fail, n_ifup;
    char ssid[100], psk[80], conf_md5[33], evt[200]; int ssid_n;
} net;
static ms_t net_now=0; static int net_echo=0;
static const char*const NC_TXT[]={"ok","config.json cannot be read","wifionoff = 0","no (or too long) ssid in config.json","password: invalid length/characters (0 or 8..63 ASCII, like stock)"};

/* top-level STRING value (depth 1) w. JSON escapes -> out; returns the length, -1 if the key is missing / not a string */
static int cfg_find_str(const char*buf,int len,const char*key,char*out,int on){
    int depth=0,i=0,kl=(int)strlen(key);
    while(i<len){
        char c=buf[i];
        if(c=='"'){
            int s=i+1; i++;
            while(i<len&&buf[i]!='"'){ if(buf[i]=='\\') i++; i++; }
            int e=i; i++;
            if(depth==1&&e-s==kl&&!memcmp(buf+s,key,(size_t)kl)){
                int j=i; while(j<len&&(buf[j]==' '||buf[j]=='\t'||buf[j]=='\r'||buf[j]=='\n')) j++;
                if(j<len&&buf[j]==':'){
                    j++; while(j<len&&(buf[j]==' '||buf[j]=='\t')) j++;
                    if(j<len&&buf[j]=='"'){
                        int n=0; j++;
                        while(j<len&&buf[j]!='"'){
                            unsigned ch=(unsigned char)buf[j++];
                            if(ch=='\\'&&j<len){
                                char q=buf[j++];
                                if(q=='n') ch='\n'; else if(q=='t') ch='\t'; else if(q=='r') ch='\r'; else if(q=='b') ch='\b'; else if(q=='f') ch='\f';
                                else if(q=='u'){
                                    unsigned u=0; int k=0;
                                    for(;k<4&&j<len&&isxdigit((unsigned char)buf[j]);k++,j++){ char h=buf[j]; u=u*16+(unsigned)(h<='9'?h-'0':(h|32)-'a'+10); }
                                    if(k<4) return -1;
                                    if(u<0x80) ch=u;
                                    else if(u<0x800){ if(n+2>=on) return -1; out[n++]=(char)(0xC0|(u>>6)); ch=0x80|(u&0x3F); }
                                    else { if(n+3>=on) return -1; out[n++]=(char)(0xE0|(u>>12)); out[n++]=(char)(0x80|((u>>6)&0x3F)); ch=0x80|(u&0x3F); }
                                } else ch=(unsigned char)q;
                            }
                            if(n+1>=on) return -1;
                            out[n++]=(char)ch;
                        }
                        if(j>=len) return -1;
                        out[n]=0; return n;
                    }
                }
            }
            continue;
        }
        if(c=='{'||c=='[') depth++;
        else if(c=='}'||c==']') depth--;
        i++;
    }
    return -1;
}
/* wifionoff / ssid / password are read FRESH from config.json (own buffer: the settings model is not touched) */
static void net_cfg_load(void){
    static char b[CFG_MAX]; int a,e,on=1;
    net.ssid[0]=net.psk[0]=0; net.ssid_n=0;
    int fd=open(cfg_path,O_RDONLY); int n=fd>=0?(int)read(fd,b,CFG_MAX-1):-1; if(fd>=0) close(fd);
    if(n<=2){ net.cfg=NC_NOCFG; return; }
    b[n]=0;
    if(cfg_find(b,n,"wifionoff",&a,&e)) on=atoi(b+a);
    int sn=cfg_find_str(b,n,"ssid",net.ssid,(int)sizeof net.ssid), pn=cfg_find_str(b,n,"password",net.psk,(int)sizeof net.psk);
    if(pn<0){ pn=0; net.psk[0]=0; }
    if(sn<0){ sn=0; net.ssid[0]=0; }
    net.ssid_n=sn;
    if(!on){ net.cfg=NC_WIFIOFF; return; }
    if(sn<=0||sn>32){ net.cfg=NC_NOSSID; return; }
    if(pn!=0&&(pn<8||pn>63)){ net.cfg=NC_BADPSK; return; }
    for(int i=0;i<pn;i++) if((unsigned char)net.psk[i]<32||(unsigned char)net.psk[i]>=127){ net.cfg=NC_BADPSK; return; }
    net.cfg=NC_OK;
}
/* the conf file in save_config format. Global lines (ctrl_interface, update_config, ...) are taken from the vendor template
 * (.conf_bak — the one demo.sh copies on every boot), otherwise from the current conf, otherwise the default.
 * ssid: "text" — or hex without quotes if it has bytes < 32 or >= 127 (wpa_config_write_string). */
static int net_conf_build(char*out,int max){
    static const char*const src[2]={NET_CONF_BAK,NET_CONF};
    char hdr[1024]; int hn=0;
    for(int s=0;s<2&&!hn;s++){
        char raw[2048]; int fd=open(rp(src[s]),O_RDONLY); if(fd<0) continue;
        int n=(int)read(fd,raw,sizeof raw-1); close(fd); if(n<=0) continue;
        raw[n]=0;
        char*nb=strstr(raw,"network={"); if(nb) *nb=0;
        if(!strstr(raw,"ctrl_interface=")) continue;
        for(char*ln=raw;*ln;){
            char*nl=strchr(ln,'\n'); int L=nl?(int)(nl-ln):(int)strlen(ln), LL=L;
            while(LL>0&&(ln[LL-1]=='\r'||ln[LL-1]==' '||ln[LL-1]=='\t')) LL--;
            if(LL>0&&hn+LL+1<(int)sizeof hdr){ memcpy(hdr+hn,ln,(size_t)LL); hn+=LL; hdr[hn++]='\n'; }
            ln+=L+(nl?1:0);
        }
    }
    if(!hn){ hn=(int)strlen(NET_CONF_HDR); memcpy(hdr,NET_CONF_HDR,(size_t)hn); }
    hdr[hn]=0;
    int hex=0; for(int i=0;i<net.ssid_n;i++){ unsigned char c=(unsigned char)net.ssid[i]; if(c<32||c>=127) hex=1; }
    char ss[80];
    if(hex){ for(int i=0;i<net.ssid_n&&i<32;i++) snprintf(ss+2*i,3,"%02x",(unsigned char)net.ssid[i]); }
    else snprintf(ss,sizeof ss,"\"%s\"",net.ssid);
    int n;
    if(net.psk[0]) n=snprintf(out,(size_t)max,"%s\nnetwork={\n\tssid=%s\n\tpsk=\"%s\"\n\tkey_mgmt=WPA-PSK\n}\n",hdr,ss,net.psk);
    else           n=snprintf(out,(size_t)max,"%s\nnetwork={\n\tssid=%s\n\tkey_mgmt=NONE\n}\n",hdr,ss);
    return (n>0&&n<max)?n:-1;
}
/* 0 = the file already has exactly this content (not touched), 1 = written (tmp + fsync + rename), -1 = error */
static int net_conf_sync(void){
    char want[4096],have[4096],path[420],tmp[440];
    int wn=net_conf_build(want,sizeof want); if(wn<0) return -1;
    md5_hex((const uint8_t*)want,(size_t)wn,net.conf_md5);
    snprintf(path,sizeof path,"%s",rp(NET_CONF));
    int fd=open(path,O_RDONLY); int hn=fd>=0?(int)read(fd,have,sizeof have):-1; if(fd>=0) close(fd);
    if(hn==wn&&!memcmp(have,want,(size_t)wn)) return 0;
    snprintf(tmp,sizeof tmp,"%s.merakiplus.tmp",path);
    if(!write_all(tmp,want,wn,0)){ unlink(tmp); return -1; }
    if(rename(tmp,path)!=0){ unlink(tmp); return -1; }
    fsync_dir(path);
    return 1;
}
static const char* net_ipstr(uint32_t ip,char*b,size_t n){
    if(!ip) snprintf(b,n,"-"); else snprintf(b,n,"%u.%u.%u.%u",(unsigned)(ip>>24),(unsigned)((ip>>16)&255),(unsigned)((ip>>8)&255),(unsigned)(ip&255));
    return b;
}
static void net_log(const char*fmt,...){
    char m[200],path[420]; va_list ap; va_start(ap,fmt); vsnprintf(m,sizeof m,fmt,ap); va_end(ap);
    snprintf(net.evt,sizeof net.evt,"%s",m);
    snprintf(path,sizeof path,"%s",rp(NET_LOG)); struct stat st;
    if(stat(path,&st)==0&&st.st_size>NET_LOG_MAX){ char old[440]; snprintf(old,sizeof old,"%s.1",path); rename(path,old); }
    FILE*f=fopen(path,"a");
    if(f){
        time_t t=time(NULL); struct tm tmv; gmtime_r(&t,&tmv); char ts[32]; strftime(ts,sizeof ts,"%Y-%m-%dT%H:%M:%SZ",&tmv);
        fprintf(f,"%s up=%lld.%03d merakiplus[%d] NET %s\n",ts,(long long)(net_now/1000),(int)(net_now%1000),(int)getpid(),m); fclose(f);
    }
    if(net_echo){ printf("up=%lld.%03d NET %s\n",(long long)(net_now/1000),(int)(net_now%1000),m); fflush(stdout); }
}

/* ---- HAL: the device (real) / model (SIM + preview/selftest) ---- */
#ifdef DEVICE_RT
static int net_sock=-1;
static int net_slurp(const char*path,char*b,int n){
    int fd=open(path,O_RDONLY|O_CLOEXEC); if(fd<0) return -1;
    int k=(int)read(fd,b,(size_t)n-1); close(fd); if(k<0) k=0; b[k]=0; return k;
}
static int net_pid_is(int pid,const char*name){        /* argv[0]'s basename; zombies/kernel threads have an empty cmdline */
    char p[64],b[200]; snprintf(p,sizeof p,"/proc/%d/cmdline",pid);
    if(net_slurp(p,b,sizeof b)<=0) return 0;
    const char*a=strrchr(b,'/'); return !strcmp(a?a+1:b,name);
}
static void net_pid_scan(int*wpa,int*dhcp){            /* one pass over /proc; only the slots that are 0 are searched */
    DIR*d=opendir("/proc"); if(!d) return;
    struct dirent*de;
    while((de=readdir(d))&&((wpa&&!*wpa)||(dhcp&&!*dhcp))){
        if(de->d_name[0]<'1'||de->d_name[0]>'9') continue;
        char p[64],b[200]; snprintf(p,sizeof p,"/proc/%.20s/cmdline",de->d_name);
        if(net_slurp(p,b,sizeof b)<=0) continue;
        const char*a=strrchr(b,'/'); a=a?a+1:b;
        if(wpa&&!*wpa&&!strcmp(a,"wpa_supplicant")) *wpa=atoi(de->d_name);
        else if(dhcp&&!*dhcp&&!strcmp(a,"udhcpc")) *dhcp=atoi(de->d_name);
    }
    closedir(d);
}
static int net_ifreq(unsigned long req,struct ifreq*r){
    if(net_sock<0) net_sock=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);
    if(net_sock<0) return -1;
    return ioctl(net_sock,req,r);
}
static int net_has_gw(void){                           /* /proc/net/route: "wlan0  00000000  <gw>  <flags w. RTF_UP|RTF_GATEWAY>" */
    char b[2048]; if(net_slurp("/proc/net/route",b,sizeof b)<=0) return 0;
    for(char*l=b;l&&*l;){
        char ifn[20]; unsigned dst=1,gw=0,fl=0; char*nl=strchr(l,'\n');
        if(sscanf(l,"%19s %x %x %x",ifn,&dst,&gw,&fl)==4&&!strcmp(ifn,NET_IF)&&dst==0&&(fl&0x3)==0x3) return 1;
        l=nl?nl+1:NULL;
    }
    return 0;
}
static void nh_probe(NetProbe*p,ms_t now,int want_dhcp){
    (void)now; char b[16]; struct ifreq r;
    p->iface=access("/sys/class/net/" NET_IF,F_OK)==0;
    p->carrier=p->iface&&net_slurp("/sys/class/net/" NET_IF "/carrier",b,sizeof b)>0&&b[0]=='1';
    p->ip=0; p->ifup=0;
    memset(&r,0,sizeof r); snprintf(r.ifr_name,sizeof r.ifr_name,"%s",NET_IF);
    if(p->iface&&net_ifreq(SIOCGIFFLAGS,&r)==0) p->ifup=(r.ifr_flags&IFF_UP)!=0;
    memset(&r,0,sizeof r); snprintf(r.ifr_name,sizeof r.ifr_name,"%s",NET_IF);
    if(p->iface&&net_ifreq(SIOCGIFADDR,&r)==0) p->ip=ntohl(((struct sockaddr_in*)(void*)&r.ifr_addr)->sin_addr.s_addr);
    p->gw=p->ip?net_has_gw():0;
    want_dhcp=want_dhcp&&!(p->ip&&(p->gw||!net.gw_seen));    /* udhcpc is only searched for while the IP configuration is missing */
    if(p->wpa_pid>0&&!net_pid_is(p->wpa_pid,"wpa_supplicant")) p->wpa_pid=0;
    if(!want_dhcp) p->dhcp_pid=0; else if(p->dhcp_pid>0&&!net_pid_is(p->dhcp_pid,"udhcpc")) p->dhcp_pid=0;
    if(!p->wpa_pid||(want_dhcp&&!p->dhcp_pid)) net_pid_scan(&p->wpa_pid,want_dhcp?&p->dhcp_pid:NULL);
}
/* called in the CHILD after fork: the same environment addition as MI_WLAN_Init (wlan.json "env": "%s:%s" — appended
 * without a duplicate check; stock's wpa_supplicant therefore has ".../config/wifi:/config/wifi", measured in /proc/841/environ) */
static void net_child_env(void){
    const char*l=getenv("LD_LIBRARY_PATH"),*pa=getenv("PATH"); char b[1200];
    if(!l||!*l) setenv("LD_LIBRARY_PATH","/lib:" NET_WIFI_DIR,1);
    else { snprintf(b,sizeof b,"%s:%s",l,NET_WIFI_DIR); setenv("LD_LIBRARY_PATH",b,1); }
    if(!pa||!*pa) setenv("PATH","/bin:/sbin:/usr/bin:/usr/sbin:" NET_WIFI_DIR,1);
    else { snprintf(b,sizeof b,"%s:%s",pa,NET_WIFI_DIR); setenv("PATH",b,1); }
}
/* detach=1: double fork — the process is inherited by init (PPid 1, own session), like stock's "… &" from system().
 * The child inherits NONE of merakiplus's fds (fb, UART, touch grab) — stock's wpa_supplicant inherits all of them.
 * Returns: detach -> 1 on success; otherwise the child's pid; -1 on error. */
static int net_spawn(int detach,const char*out,const char*path,char*const argv[]){
    fflush(stdout); fflush(stderr);
    pid_t c=fork(); if(c<0) return -1;
    if(c==0){
        setsid();
        if(detach){ pid_t g=fork(); if(g!=0) _exit(g<0?1:0); }
        if(chdir("/")!=0){}
        net_child_env();
        int nul=open("/dev/null",O_RDWR), o=out?open(out,O_WRONLY|O_CREAT|O_APPEND,0644):-1;
        if(nul>=0){ dup2(nul,0); dup2(o>=0?o:nul,1); dup2(o>=0?o:nul,2); }
        for(int fd=3;fd<1024;fd++) close(fd);
        signal(SIGINT,SIG_DFL); signal(SIGTERM,SIG_DFL);
        execv(path,argv);
        _exit(127);
    }
    if(detach){ int st=0; while(waitpid(c,&st,0)<0&&errno==EINTR){} return (WIFEXITED(st)&&WEXITSTATUS(st)==0)?1:-1; }
    return (int)c;
}
static int nh_init_start(void){
    char*const a[]={(char*)"sh",(char*)"-c",(char*)"source " NET_INIT_SH " dummy1 dummy2 ",NULL};
    return net_spawn(0,NET_LOG,"/bin/sh",a);
}
static int nh_job_done(int pid,int*rc){
    int st=0; pid_t r=waitpid(pid,&st,WNOHANG); if(r==0) return 0;
    *rc=(r==pid&&WIFEXITED(st))?WEXITSTATUS(st):-1; return 1;
}
static void nh_prepare(void){          /* the part of the init script that wpa_supplicant needs when wlan0 already exists */
    mkdir("/tmp/wifi",0755); mkdir(NET_RUN_DIR,0777); chmod(NET_RUN_DIR,0777);
    unlink(NET_CTRL "/" NET_IF);      /* only called when no wpa_supplicant is alive: a leftover socket is dead */
}
static void nh_iface_up(void){         /* = "ifconfig wlan0 up" */
    struct ifreq r; memset(&r,0,sizeof r); snprintf(r.ifr_name,sizeof r.ifr_name,"%s",NET_IF);
    if(net_ifreq(SIOCGIFFLAGS,&r)==0&&!(r.ifr_flags&IFF_UP)){ r.ifr_flags|=IFF_UP; net_ifreq(SIOCSIFFLAGS,&r); }
}
static int nh_wpa_start(void){
    char*const a[]={(char*)"wpa_supplicant",(char*)"-i",(char*)NET_IF,(char*)"-Dnl80211",(char*)"-c",(char*)NET_CONF,NULL};
    return net_spawn(1,NULL,NET_WPA_BIN,a)>0;
}
static int nh_dhcp_start(void){
    char*const a[]={(char*)"udhcpc",(char*)"-i",(char*)NET_IF,(char*)"-s",(char*)NET_DHCP_SH,(char*)"-a",(char*)"-q",(char*)"-b",(char*)"-t",(char*)"20",(char*)"-T",(char*)"1",NULL};
    return net_spawn(1,NULL,NET_DHCP_BIN,a)>0;
}
#else
/* MODEL of the wifi stack (SIM build + preview/selftest): the same state machine, no processes, no network.
 * the init script provides wlan0 after NM_INIT_MS; a started supplicant gets carrier after NM_ASSOC_MS (if the AP is
 * there); udhcpc resets the IP at start (deconfig), gets a lease NM_LEASE_MS after carrier and exits (-q). */
#define NM_INIT_MS  600
#define NM_ASSOC_MS 1500
#define NM_LEASE_MS 1200
/* ifdown (ifconfig wlan0 down) removes the kernel's routes via wlan0, but not the IP — as on the device. init_hang: the script never finishes. */
static struct { int iface, ifup, wpa_pid, dhcp_pid, carrier, gw, ap, gw_offer, job, pid_next, init_fail, init_hang, wpa_fail, n_init, n_wpa, n_dhcp, n_prep, n_ifup; uint32_t ip, lease; ms_t now, t_init, assoc_at, lease_at; } nm;
static void nm_reset(int cold){
    memset(&nm,0,sizeof nm); nm.ap=1; nm.gw_offer=1; nm.pid_next=900; nm.lease=(192u<<24)|(168u<<16)|(1u<<8)|100u;
    if(!cold){ nm.iface=1; nm.ifup=1; nm.wpa_pid=841; nm.carrier=1; nm.ip=nm.lease; nm.gw=1; }
}
static void nm_step(ms_t now){
    nm.now=now;
    if(nm.wpa_pid&&nm.ap&&nm.iface&&nm.ifup){ if(!nm.carrier&&now>=nm.assoc_at) nm.carrier=1; } else nm.carrier=0;
    if(nm.dhcp_pid){
        if(!nm.carrier) nm.lease_at=0;
        else { if(!nm.lease_at) nm.lease_at=now+NM_LEASE_MS; if(now>=nm.lease_at){ nm.ip=nm.lease; nm.gw=nm.gw_offer; nm.dhcp_pid=0; nm.lease_at=0; } }
    }
}
static void nh_probe(NetProbe*p,ms_t now,int want_dhcp){ nm_step(now); p->iface=nm.iface; p->ifup=nm.iface&&nm.ifup; p->wpa_pid=nm.wpa_pid; p->dhcp_pid=want_dhcp?nm.dhcp_pid:0; p->carrier=nm.carrier; p->ip=nm.iface?nm.ip:0; p->gw=p->ip&&nm.gw; }
static int nh_init_start(void){ nm.n_init++; nm.job=nm.pid_next++; nm.t_init=nm.now; return nm.job; }
static int nh_job_done(int pid,int*rc){ (void)pid; if(nm.init_hang||nm.now-nm.t_init<NM_INIT_MS) return 0; nm.job=0; nm.iface=!nm.init_fail; nm.ifup=0; *rc=nm.init_fail?255:0; return 1; }
static void nh_prepare(void){ nm.n_prep++; }
static void nh_iface_up(void){ if(nm.iface&&!nm.ifup){ nm.ifup=1; nm.n_ifup++; nm.assoc_at=nm.now+NM_ASSOC_MS; } }
static int nh_wpa_start(void){ nm.n_wpa++; if(nm.wpa_fail||!nm.iface) return 1; nm.wpa_pid=nm.pid_next++; nm.ifup=1; nm.carrier=0; nm.assoc_at=nm.now+NM_ASSOC_MS; return 1; }
static int nh_dhcp_start(void){ nm.n_dhcp++; nm.dhcp_pid=nm.pid_next++; nm.ip=0; nm.gw=0; nm.lease_at=0; return 1; }
#endif

/* ---- the state machine (identical in all build types) ---- */
static void net_fail(ms_t now,const char*why){
    net.step=NB_NONE; net.job=0; net.n_fail++; net.state=NS_DOWN;
    net.backoff=net.backoff?(net.backoff*2>NET_BACKOFF_MAX_MS?NET_BACKOFF_MAX_MS:net.backoff*2):NET_BACKOFF0_MS;
    net.t_retry=now+net.backoff;
    net_log("bring-up FAILED: %s - retrying in %d s (failures in total: %d)",why,net.backoff/1000,net.n_fail);
}
static void net_off(ms_t now){
    if(net.state!=NS_OFF||net.cfg_logged!=net.cfg){ net.cfg_logged=net.cfg; net_log("OFF: %s - the network is not touched",NC_TXT[net.cfg]); }
    net.state=NS_OFF; net.step=NB_NONE; net.t_cfg=now+NET_CFG_RECHECK_MS;
}
/* the IP configuration is complete: an IP — and the default route, if there has ever been one (a network without a gateway is healthy too) */
static int net_ipok(const NetProbe*p){ return p->ip&&(p->gw||!net.gw_seen); }
static void net_dhcp(ms_t now,const char*why){
    if(nh_dhcp_start()){ net.n_dhcp++; net_log("%s: started udhcpc -i " NET_IF " -s " NET_DHCP_SH " -a -q -b -t 20 -T 1",why); }
    else net_log("%s: udhcpc could NOT be started",why);
    net.t_noip=now;
}
static void net_wpa_begin(ms_t now){
    char sv_[40]; int k=0; for(;k<net.ssid_n&&k<32;k++){ unsigned char c=(unsigned char)net.ssid[k]; sv_[k]=(c<32||c>=127)?'?':(char)c; } sv_[k]=0;
    nh_prepare();
    int c=net_conf_sync();
    if(c<0){ net_fail(now,"cannot write " NET_CONF); return; }
    if(c>0){ net.n_conf++; net_log(NET_CONF " written from config.json (ssid \"%s\", %s, md5 %.8s)",sv_,net.psk[0]?"WPA-PSK":"open network",net.conf_md5); }
    else net_log(NET_CONF " already has the correct content (ssid \"%s\", md5 %.8s) - not written",sv_,net.conf_md5);
    nh_iface_up();
    if(!nh_wpa_start()){ net_fail(now,"wpa_supplicant could not be started"); return; }
    net.n_wpa++; net.step=NB_WPA; net.t_step=now;
    net_log("started: wpa_supplicant -i " NET_IF " -Dnl80211 -c " NET_CONF " (waiting %d ms like stock)",NET_WPA_SETTLE_MS);
}
static void net_bringup(ms_t now){
    net_cfg_load();
    if(net.cfg!=NC_OK){ net_off(now); return; }
    net.acted=1; net.state=NS_STARTING;
    if(!net.p.iface){
        if(net.orphan){ net_fail(now,"an earlier init script is still running - not starting another"); return; }
        int j=nh_init_start(); if(j<=0){ net_fail(now,"the init script could not be started"); return; }
        net.job=j; net.init_hung=0; net.n_init++; net.step=NB_INIT; net.t_step=now;
        net_log(NET_IF " missing: running \"source " NET_INIT_SH " dummy1 dummy2\" (like MI_WLAN_Init)");
        return;
    }
    net_wpa_begin(now);
}
static void net_eval(ms_t now){
    NetProbe*p=&net.p; char ipb[20];
    int wpa=p->wpa_pid>0, first=!net.booted; net.booted=1;
    if(p->ip&&p->gw) net.gw_seen=1;
    if(net.step==NB_INIT){
        int rc=0;
        if(!nh_job_done(net.job,&rc)){
            if(now-net.t_step<NET_INIT_TMO_MS) return;
            if(!p->iface){                                    /* no kill calls: the script is allowed to finish, and no second one is started */
                if(!net.init_hung){ net.init_hung=1; net_log("the init script (pid %d) has been running for %d s without " NET_IF " - still waiting (it is not killed)",net.job,NET_INIT_TMO_MS/1000); }
                return;
            }
            net_log("the init script (pid %d) is still running after %d s, but " NET_IF " exists: continuing (the script is reaped when it ends)",net.job,(int)((now-net.t_step)/1000));
            net.orphan=net.job; net.job=0; net_wpa_begin(now); return;
        }
        net.job=0; nh_probe(p,now,0);
        if(!p->iface){ char w[72]; snprintf(w,sizeof w,"init script rc=%d, " NET_IF " still does not exist",rc); net_fail(now,w); return; }
        net_log("init script finished (rc=%d): " NET_IF " exists",rc);
        net_wpa_begin(now); return;
    }
    if(net.step==NB_WPA){
        if(now-net.t_step<NET_WPA_SETTLE_MS) return;
        if(!wpa){ net_fail(now,"wpa_supplicant not running after start"); return; }
        net.step=NB_NONE; net.state=NS_CONNECTING; net.t_noip=now; net.carrier_was=p->carrier; net.dhcp_wait=0;
        if(p->dhcp_pid) net_log("wpa_supplicant pid %d running; udhcpc pid %d already running",p->wpa_pid,p->dhcp_pid);
        else if(net_ipok(p)) net_log("wpa_supplicant pid %d running; " NET_IF " still has ip %s%s - kept (no udhcpc)",p->wpa_pid,net_ipstr(p->ip,ipb,sizeof ipb),p->gw?" and default route":"");
        else { char w[64]; snprintf(w,sizeof w,"wpa_supplicant pid %d running",p->wpa_pid); net_dhcp(now,w); }
        return;
    }
    if(wpa&&p->ifup&&p->carrier&&net_ipok(p)){              /* healthy: not touched */
        if(net.state!=NS_UP){
            int ms=(int)(now-net.t_down); net_ipstr(p->ip,ipb,sizeof ipb);
            if(first) net_log("UP at start: wpa_supplicant pid %d, ip %s%s - the stack is healthy and is not touched",p->wpa_pid,ipb,p->gw?", default route":"");
            else if(net.acted&&net.was_up){ net.n_heal++; net.heal_ms=ms; net_log("SELF-HEAL done in %d ms: wpa_supplicant pid %d, ip %s (heal no. %d)",ms,p->wpa_pid,ipb,net.n_heal); }
            else if(net.acted){ net.n_boot++; net.heal_ms=ms; net_log("UP %d ms after startup: wpa_supplicant pid %d, ip %s",ms,p->wpa_pid,ipb); }
            else net_log("UP again after %d ms without intervention: wpa_supplicant pid %d, ip %s",ms,p->wpa_pid,ipb);
        }
        net.state=NS_UP; net.was_up=1; net.acted=0; net.dead=0; net.backoff=0; net.t_retry=0; net.dhcp_wait=0; net.ifdown=0; net.carrier_was=1; return;
    }
    if(net.state==NS_UP||first) net.t_down=now;               /* the outage starts here */
    if(wpa){                                                  /* the supplicant is alive: it is NEVER killed */
        net.dead=0;
        if(net.state!=NS_CONNECTING){
            net.state=NS_CONNECTING; net.t_noip=now; net.carrier_was=p->carrier; net.dhcp_wait=0;
            net_log("wpa_supplicant pid %d alive, but %s - waiting (the supplicant is not touched)",p->wpa_pid,
                    !p->iface?NET_IF " missing":!p->ifup?NET_IF " is down":!p->carrier?"no carrier":!p->ip?"no IP":"no default route");
        }
        if(p->iface&&!p->ifup){                               /* "ifconfig wlan0 down": the supplicant just waits -> up again (MI_WLAN_Open) */
            if(!net.ifdown){ net.ifdown=1; net.t_ifdown=now; }
            if(now-net.t_ifdown<NET_IFDOWN_MS) return;
            net.acted=1; net.n_ifup++; net.t_ifdown=now; nh_iface_up();
            net_log(NET_IF " has been down for %d s with a live supplicant: ifconfig " NET_IF " up (like MI_WLAN_Open)",NET_IFDOWN_MS/1000);
            return;
        }
        net.ifdown=0;
        if(p->carrier&&!net.carrier_was) net.t_noip=now;      /* the time until udhcpc is measured from carrier */
        net.carrier_was=p->carrier;
        if(net_ipok(p)||p->dhcp_pid){ net.t_noip=now; return; }
        if(p->ip&&!p->carrier) return;                        /* IP but no route/carrier: a udhcpc now would only take the IP away */
        int wait=net.dhcp_wait?net.dhcp_wait:(p->ip?NET_NOGW_MS:NET_NOIP_MS);
        if(now-net.t_noip<wait) return;
        net_cfg_load(); if(net.cfg!=NC_OK){ net.t_noip=now; return; }
        net.acted=1; net.dhcp_wait=wait*2>NET_DHCP_MAX_MS?NET_DHCP_MAX_MS:wait*2;
        char w[80]; snprintf(w,sizeof w,p->ip?"no default route for %d s and no udhcpc":"no IP for %d s and no udhcpc",wait/1000);
        net_dhcp(now,w);
        return;
    }
    if(!net.dead){                                            /* the supplicant is gone */
        net.dead=1; net.t_dead=now; if(net.state!=NS_OFF) net.state=NS_DOWN;
        net_log("wpa_supplicant not running (" NET_IF " %s, carrier %d, ip %s)",p->iface?(p->ifup?"exists":"exists, down"):"missing",p->carrier,net_ipstr(p->ip,ipb,sizeof ipb));
    }
    if(net.state==NS_OFF&&now<net.t_cfg) return;
    if(!first&&now-net.t_dead<NET_DEAD_MS) return;
    if(now<net.t_retry) return;
    net_bringup(now);
}
static void net_step(ms_t now){
    if(!net.on||now<net.t_probe) return;
    net_now=now;
    if(net.orphan){ int rc=0; if(nh_job_done(net.orphan,&rc)){ net_log("the orphaned init script (pid %d) ended (rc=%d)",net.orphan,rc); net.orphan=0; } }
    nh_probe(&net.p,now,1);
    net_eval(now);
    int idle=net.step==NB_NONE&&(net.state==NS_OFF||(net.state==NS_DOWN&&now<net.t_retry));
    net.t_probe=now+(net.state==NS_UP?NET_PROBE_UP_MS:(idle?NET_PROBE_IDLE_MS:NET_PROBE_MS));
}
static void net_start(ms_t now,int enable){
    memset(&net,0,sizeof net); net_now=now; net.cfg_logged=-1; net.on=enable;
    if(!enable) return;
    net_cfg_load();
    net_log("net-vagt startet (" MERAKIPLUS_VERSION "): %s; ssid length %d, password length %d",NC_TXT[net.cfg],net.ssid_n,(int)strlen(net.psk));   /* "net-vagt startet" stays: tools/merakiplus.py greps the net log for it */
}


