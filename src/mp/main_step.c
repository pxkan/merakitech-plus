/* src/mp/main_step.c — ONE iteration of the main loop (expects a local `ms_t now`; `break` leaves the loop).
 * Part of the merakiplus.c unity build, but included INSIDE a loop body: by main()'s while(run) in src/mp/main.c (device + SIM)
 * and by mp_step() in src/mp/wasm_hal.c (the browser build, MERAKIPLUS_WASM; inside do{ }while(0)) — so all three run
 * ONE loop body. A fragment (not a function) because that keeps the device binary byte-identical. */
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
