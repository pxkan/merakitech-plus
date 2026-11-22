/* src/mp/main.c — device/SIM entry point: release/reacquire, net CLI, main loop.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
static void inp_poll(ms_t now){
    if(ifd<0) return;
    struct input_event ev;
    while(INP_READ(ev)){
        if(ev.type==EV_ABS){
            if(ev.code==ABS_X||ev.code==ABS_MT_POSITION_X){ in_x=scalex(ev.value); in_moved=1; }
            else if(ev.code==ABS_Y||ev.code==ABS_MT_POSITION_Y){ in_y=scaley(ev.value); in_moved=1; }
            else if(ev.code==ABS_MT_TRACKING_ID){ if(ev.value>=0) in_pdown=1; else in_pup=1; }
        } else if(ev.type==EV_KEY&&ev.code==BTN_TOUCH){ if(ev.value) in_pdown=1; else in_pup=1; }
        else if(ev.type==EV_SYN&&ev.code==SYN_REPORT){
            if(in_pdown&&!in_down){ in_down=1; ui_touch(TE_DOWN,in_x,in_y,now); }
            else if(in_down&&in_moved) ui_touch(TE_MOVE,in_x,in_y,now);
            if(in_pup&&in_down){ in_down=0; ui_touch(TE_UP,in_x,in_y,now); }
            in_pdown=in_pup=in_moved=0;
        }
    }
}

#ifdef DEVICE_RT
/* BOOT.md §9.2 step 1: stop own activity — UART, touch grab and fb are released BEFORE stock A is started */
static void dev_release_impl(void){
    uart_flush();
    if(ufd>=0){ tcdrain(ufd); close(ufd); ufd=-1; }
    if(ifd>=0){ ioctl(ifd,EVIOCGRAB,0); close(ifd); ifd=-1; }
    if(kfd>=0){ close(kfd); kfd=-1; }                                       /* v6.14 */
    if(bl_is_off) bl_power(1);                                              /* v6.14: the stock UI must not start in the dark */
    if(fbmem){ munmap(fbmem,fblen); fbmem=NULL; }
    if(ffd>=0){ close(ffd); ffd=-1; }
}
static void dev_reacquire_impl(void){      /* the stock UI did not come up: merakiplus takes the screen back */
    uart_open(); fb_open(); inp_open(); kbd_open();
    RL full; full.n=1; full.r[0]=FULL; pres_prev=full;
}
/* v6.7: the net watchdog alone, without screen/UART/touch — for status and for the self-heal proof on the device:
 *   merakiplus --net-status            (read-only: probe + comparison of the conf file with what config.json yields)
 *   merakiplus --net-watch [seconds]   (same net_step() as the UI's main loop; also logs to stdout) */
static int net_cli(int argc,char**argv){
    ms_t now=now_ms(); const char*e;
    if((e=getenv("MERAKIPLUS_CONFIG"))) snprintf(cfg_path,sizeof cfg_path,"%s",e);
    if(!strcmp(argv[1],"--net-status")){
        NetProbe p; char want[4096],have[4096],mw[33]="-",mh[33]="-",ipb[20]; int wn=-1;
        memset(&p,0,sizeof p); memset(&net,0,sizeof net); net_now=now; nh_probe(&p,now,1); net_cfg_load();
        if(net.cfg==NC_OK&&(wn=net_conf_build(want,sizeof want))>0) md5_hex((const uint8_t*)want,(size_t)wn,mw);
        int fd=open(NET_CONF,O_RDONLY); int hn=fd>=0?(int)read(fd,have,sizeof have):-1; if(fd>=0) close(fd);
        if(hn>0) md5_hex((const uint8_t*)have,(size_t)hn,mh);
        printf("merakiplus " MERAKIPLUS_VERSION " net-status (read-only)\n");
        printf("probe : " NET_IF " %s, wpa_supplicant pid %d, udhcpc pid %d, carrier %d, ip %s, default route %d\n",p.iface?(p.ifup?"present (UP)":"present (DOWN)"):"missing",p.wpa_pid,p.dhcp_pid,p.carrier,net_ipstr(p.ip,ipb,sizeof ipb),p.gw);
        printf("config: %s -> %s; ssid length %d, password length %d\n",cfg_path,NC_TXT[net.cfg],net.ssid_n,(int)strlen(net.psk));
        printf("conf  : " NET_CONF " on disk %d bytes md5 %s\n",hn,mh);
        printf("conf  : built from config.json %d bytes md5 %s -> %s\n",wn,mw,(wn>0&&hn==wn&&!memcmp(have,want,(size_t)wn))?"IDENTICAL (would not be written)":"DIFFERS (would be written on bring-up)");
        printf("dom   : %s\n",(p.wpa_pid>0&&p.ifup&&p.carrier&&p.ip)?"healthy stack - nothing to do":(p.wpa_pid>0?"wpa_supplicant alive - not touched (udhcpc if needed)":"wpa_supplicant not running - bring-up"));
        return 0;
    }
    if(!strcmp(argv[1],"--net-watch")){
        int secs=argc>2?atoi(argv[2]):0; net_echo=1;
        net_start(now,!getenv("MERAKIPLUS_NONET"));
        ms_t end=secs>0?now+(ms_t)secs*1000:0;
        while(run&&(!end||now_ms()<end)){ net_step(now_ms()); struct timespec sl={0,20*1000*1000}; nanosleep(&sl,NULL); }
        net_now=now_ms(); net_log("net watchdog (--net-watch) done: heal %d, wpa starts %d, udhcpc starts %d, conf writes %d, errors %d",net.n_heal,net.n_wpa,net.n_dhcp,net.n_conf,net.n_fail);
        return 0;
    }
    fprintf(stderr,"usage: merakiplus [--net-status | --net-watch [seconds]]\n"); return 2;
}
#define NET_CLI(argc,argv) if(argc>1) return net_cli(argc,argv);
#define NET_ENABLED (!getenv("MERAKIPLUS_NONET")&&!sysroot[0])      /* under a test root (MERAKIPLUS_ROOT) the real network is not touched */
#endif
#ifdef SIM_RT
#define NET_CLI(argc,argv) (void)argc; (void)argv;
#define NET_ENABLED sim_net_on
#endif
int main(int argc,char**argv){
    signal(SIGINT,onsig); signal(SIGTERM,onsig);
    NET_CLI(argc,argv)
    dev_release=dev_release_impl; dev_reacquire=dev_reacquire_impl;
    const char*e;
    if((e=getenv("MERAKIPLUS_CONFIG"))) snprintf(cfg_path,sizeof cfg_path,"%s",e);
    if((e=getenv("MERAKIPLUS_BAKDIR"))) snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s",e);
    if(getenv("MERAKIPLUS_NOBL")) bl_enabled=0;
    if(getenv("MERAKIPLUS_READONLY")) ro_mode=1;
    if((e=getenv("MERAKIPLUS_ROOT"))) snprintf(sysroot,sizeof sysroot,"%s",e);
    if((e=getenv("MERAKIPLUS_LANGFILE"))) snprintf(lang_path,sizeof lang_path,"%s",e);
    if((e=getenv("MERAKIPLUS_SCHEDFILE"))) snprintf(sched_path,sizeof sched_path,"%s",e);   /* v6.16 */
    if((e=getenv("MERAKIPLUS_SOUNDFILE"))) snprintf(snd_path,sizeof snd_path,"%s",e);       /* v6.18 */
    if(getenv("MERAKIPLUS_SWITCH_DRY")) sw_dry=1;
    if(getenv("MERAKIPLUS_NOBOOTPUSH")) bp_on=0;
    SIM_ENV
    bs.self_pid=(int)getpid();
    lang_load();                           /* v6.3: UI language BEFORE the first frame (splash); missing/unreadable/unknown -> English */
    gfx_init(); settings_load();
    boot_refresh();                        /* warm up (binary marker scan of slot B) during splash, not on the first tap */
    uart_open(); fb_open(); inp_open();
#ifdef DEVICE_RT
    kbd_open();                            /* v6.14: the power button (k62091) */
#endif
    SIM_START
    ms_t t0=now_ms();
    net_start(t0,NET_ENABLED);             /* v6.7: wifi at startup + self-heal (first evaluation in the first loop iteration) */
    proto_keepawake(t0,1);                 /* power=1 + word1=0 (awake) */
#ifdef DEVICE_RT
    if(bp_on&&!ro_mode) boot_push_start(1);   /* v6.5: boot push like stock (sub1 len30 + sub112 len3) w/ activity guard */
#endif
    RL full; full.n=1; full.r[0]=FULL; pres_prev=full;
    while(run){
        ms_t now=now_ms();
        uint8_t b[512]; int n;
        while((n=read(ufd,b,sizeof b))>0) rx_feed(b,n,now);
        rx_feed(b,0,now);                  /* v6.5: the resync watchdog must also run when nothing arrives */
        wq_step(now);
        loop_poll(now);                    /* v6.6: the timers live in lt (64 bit) — see loop_poll() */
        proto_keepawake(now,0);
        uart_flush();
        loop_graph(now);
        inp_poll(now);
#ifdef DEVICE_RT
        kbd_poll(now);                     /* v6.14: the power button */
#endif
        pw_step(now);                      /* v6.14: hold -> off frames / OFF / TURNING ON… */
        sched_step(now);                   /* v6.16: pre-arm the pre-heat alarm, one-shot use, auto off */
        snd_tick(now);                     /* v6.18: start jingle + per-boiler ready tones (display/audio only, no frames) */
        net_step(now);                     /* v6.7: non-blocking; does nothing between two probes */
#ifdef DEVICE_RT
        boot_push_step(now);
        repush_step(now);                  /* v6.6: MCU restart (0x89 again / defaults signature) -> boot push from scratch */
#endif
        SIM_TICK(now)
        if(!run) break;                    /* REVERT / runtime switch: after step 1 fb/UART are closed — do not touch them any more */
        if(loop_render_due(now)){
            struct timespec fa,fb; clock_gettime(CLOCK_MONOTONIC,&fa);
            ui_frame(now);
            clock_gettime(CLOCK_MONOTONIC,&fb);
            loop_rendered(now,(fb.tv_sec-fa.tv_sec)*1e3f+(fb.tv_nsec-fa.tv_nsec)/1e6f);
            SIM_FRAME(now)
        }
        struct timespec sl={0, 2*1000*1000}; nanosleep(&sl,NULL);
    }
    /* run.sh mode: last frame (fb is still open; on a real B->A switch it has already been shown and fb closed) */
    if(view==V_BYE&&fbmem){ tr.type=TR_NONE; tr_was=0; static_dirty=1; force_all=1; ui_frame(now_ms()); }
    if(sw_child) printf(TR(TX_O_SWITCHED),sw_child);
    if(sel>=0&&(SET[sel].flags&SF_BL)) bl_set(SET[sel].val);   /* undo an unfinished brightness preview */
    /* same reminder as the exit screen — and only when it is true (a stock UI w/ a stale RAM mirror is waiting) */
    boot_refresh();
    if(restart_pending()) fputs(TR(TX_O_RESTART),stdout);
    else if(cfg_writes) printf(TR(TX_O_CFGCHANGED),cfg_writes);
    if(wq.n_block) printf(TR(TX_O_BLOCKED),wq.n_block);
    fflush(stdout); fflush(stderr);
    SIM_EXIT
    if(ufd>=0) close(ufd);
    if(ifd>=0){ INP_UNGRAB close(ifd); }
#ifdef DEVICE_RT
    if(kfd>=0) close(kfd);
#endif
    if(bl_is_off) bl_power(1);             /* v6.14: never leave the screen dark (run.sh -> the stock UI continues) */
    if(fbmem) munmap(fbmem,fblen);
    if(ffd>=0) close(ffd);
    return MAIN_RC;
}
