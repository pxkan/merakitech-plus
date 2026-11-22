/* src/mp/main_setup.c — main()'s startup sequence (after the signal handlers and the net CLI).
 * Part of the merakiplus.c unity build, but included INSIDE a function body: by main() in src/mp/main.c (device + SIM)
 * and by mp_init() in src/mp/wasm_hal.c (the browser build, MERAKIPLUS_WASM) — so all three run ONE startup sequence.
 * A fragment (not a function) because that keeps the device binary byte-identical. Declares locals: e, t0, full. */
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
