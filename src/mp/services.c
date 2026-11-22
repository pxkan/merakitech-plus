/* src/mp/services.c — power button, pre-arm + auto off, sound.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ================= v6.14: the physical POWER BUTTON (UI_v6.md §19 + CHANGES-v6.14) =================
 * The bug: the button did nothing in merakiplus — only the touch node (event0) was read. The button is the k62091 node (/dev/input/event1,
 * "Handlers=kbd event1", EV=3 = SYN+KEY, one key). The stock semantics, reversed from demo_v9 (symbols preserved) + the MCU firmware:
 *  - app_powerKey_thread 0x140AB4: open("/dev/input/event1", O_RDWR) — NO EVIOCGRAB; reads struct input_event; for EVERY
 *    EV_KEY (the code is not checked) value is stored in app+0x15A8 (1 = down, 0 = released).
 *  - SleepRefresh 0x149268 (lv_timer, 100 ms): only when the second changes, and only if isExtractionOrSteamSwitchOn 0x1481E0 is
 *    false (work_sta bit 12 / bit 14, word 22, word 24, word 6 = brew/steam/grind), e60 is counted up while the button is down; then
 *    dealPowerState 0x14911C(value) every cycle: e60 > 1 (= down across TWO second ticks, i.e. 1-2 s) and mirror word 0 != 0 ->
 *    powerOff 0x149074. Release before that = short press (only wakes stock's screensaver). If an activity is running, NOTHING happens —
 *    stock never stops an activity on power-off; the button only works once brew/steam/grind has ended (hot water does not block).
 *  - powerOff: mirror word 0 = 0; performSleepActions 0x148324 -> powerOffSend 0x14700C(0,0,0) = cmd8 (15 bytes: year u16, month,
 *    day, hour, minute, second, alarm day/hour/minute = 0/0/0 = no alarm; CRC) + usleep(80 ms); usart_send(6,0,2) = cmd6
 *    sub0 len2 [word 0 = 0, word 1 = the mirror (0 = awake)]; backlight off (pwm2 enable 0 + duty 0); the power-off page.
 *    1.5 s later: fb is closed, "echo mem > /sys/power/state" (SoC suspend); after resume "killall demo_v9" -> watchdog restarts
 *    stock -> app_init 0xD9170 PowerOn (word 0 = 1).
 *  - MCU (MEM03_V514_260923.bin): the status task sees word 0 = 0 (0x29D4, every 500 ms) and starts "sleep" (0x2F52), which DELETES
 *    boiler_heat, head_heat, status (= RX parser + replies: the controller is silent while it sleeps) and run, sets work_sta = 0 and
 *    resets the outputs (0x2F0C). It wakes ONLY on a hold of the button (own GPIO PC.8; 8 x 100 ms low after a release) or
 *    the alarm -> 0x326 "reset muc" (CPU reset; word 0 starts as 1, the 0x89 round comes -> merakiplus's re-push sends config again).
 * merakiplus mirrors this: HOLD 1.5 s -> the same two frames (cmd8 w. alarm 0/0/0, 80 ms, cmd6 sub0 len2 = [0,0] w. I_POWEROFF) ->
 * TURNING OFF… -> ACK -> OFF; the backlight is turned off 1.5 s after the press; keep-awake is paused while off.
 * Blocked as in stock (brew/steam/grind running: toast, nothing is sent) — and without link / with word 0 = 0 / read-only.
 * Power on: the button again -> light + TURNING ON… (the MCU wakes by itself from the same hold); if the controller answers with word 0 = 1 -> PowerOn
 * (sub0=[1] + sub1=[0], exactly as at startup) -> HOME. Otherwise back to OFF after 10 s. If the controller answers by itself
 * with word 0 = 1 while merakiplus is in OFF (alarm, or the button was held while merakiplus did not see it), it powers on the same way.
 * A tap on the dark screen shows the OFF page for 8 s — no frames (the MCU cannot receive anything while it sleeps).
 * Deliberate deviation: no SoC suspend and no "killall" — only the backlight; the net guard and telnet keep running. */
enum { PW_ON=0, PW_OFFING, PW_OFF, PW_WAKING };
#define PW_HOLD_MS  1500    /* stock: 2 second ticks (1.0-2.0 s); the middle */
#define PW_GAP_MS     80    /* stock performSleepActions: cmd8 ... usleep(80000) ... cmd6 sub0 len2 */
#define PW_BL_MS    1500    /* stock: 1.5 s after powerOff -> fb/light/suspend (lv_tick_elaps > 0x5DC); merakiplus: the light is turned off */
#define PW_PEEK_MS  8000    /* tap on the dark screen: the OFF page is shown this long */
#define PW_WAKE_MS 10000    /* TURNING ON…: how long to wait for word 0 = 1 from the controller */
#define PW_SEEN_MS   600    /* word 0 = 1 seen (w. link) this long -> the controller is awake */
static struct { int state, key_down, key_fired, blk_shown, kbd_ok, key_code, key_val, f8_ok, push_set;
                ms_t t_press, t_down, t_req, t_state, t_peek, t_on1, t_key; long push_seq;
                long n_ev, n_press, n_short, n_off, n_on, n_blk, n_fail, n_ack8, n_auto, n_wto;
                uint8_t f8[15]; char node[64]; } pw;
static void proto_keepawake(ms_t now,int first);
/* stock isExtractionOrSteamSwitchOn 0x1481E0: GetMaichinState(0x1000) | GetMaichinState(0x4000) | mirror word 22 | 24 | 6.
 * merakiplus's own start in flight (act.pending) counts too, for the same three activities. */
static int pw_busy(void){
    uint16_t ws=reg_seen[33]?reg[33]:0;
    return (ws&0x1000)||(ws&0x4000)||(reg_seen[22]&&reg[22])||(reg_seen[24]&&reg[24])||(reg_seen[6]&&reg[6])
         ||act.pending==ACT_GRIND||act.pending==ACT_BREW||act.pending==ACT_STEAM;
}
/* cmd8 like stock's powerOffSend 0x14700C: AA 55 08 | year lo, year hi | month 1-12 | day | hour | minute | second | alarm day, hour,
 * minute | CRC lo, hi (CRC_GetModbus16 over 13 bytes). MCU 0x9C88 copies the 10 bytes to 0x20000E18 and replies 0x88 w. echo;
 * the sleep task only uses the alarm (day + hour + minute = 0 -> none) — date/time there is overwritten from the MCU's own RTC. */
static int pw_cmd8(uint8_t b[15],const struct tm*tm,int ad,int ah,int am){
    int y=tm->tm_year+1900;
    b[0]=0xAA; b[1]=0x55; b[2]=0x08; b[3]=(uint8_t)(y&0xFF); b[4]=(uint8_t)(y>>8); b[5]=(uint8_t)(tm->tm_mon+1); b[6]=(uint8_t)tm->tm_mday;
    b[7]=(uint8_t)tm->tm_hour; b[8]=(uint8_t)tm->tm_min; b[9]=(uint8_t)tm->tm_sec; b[10]=(uint8_t)ad; b[11]=(uint8_t)ah; b[12]=(uint8_t)am;
    uint16_t c=crc16(b,13); b[13]=c&0xFF; b[14]=c>>8; return 15;
}
/* v6.16: the cmd8 date/time is stock's model time (_refresh_systime: SoC UTC + zone + DST) — v6.14 used localtime() of the SoC,
 * i.e. UTC on the device (no TZ). The MCU sets its RTC from these bytes, so the alarm below is compared against the same local time.
 * PREVIEW/selftest: clk_fake (default 2026-11-18 15:35:28 UTC = 16:35:28 in zone #48 without DST). */
static void pw_now_tm(struct tm*tm){ clk_now(tm); }
/* v6.16: pre-arm state (the cmd8 alarm while on) — see sched_step() */
static struct { int sent,ad,ah,am,f8_ok,src; long n_sent,n_ack,mcu_rs; ms_t t_sent; uint8_t f8[15]; } arm;
static void pw_ack8(const uint8_t*p){ if(pw.f8_ok&&!memcmp(p,pw.f8+3,10)) pw.n_ack8++; if(arm.f8_ok&&!memcmp(p,arm.f8+3,10)) arm.n_ack++; }
/* for the OFF view: an open detail is reverted (light/language preview back), no transition, no ripple, no held start */
static void pw_view(int v){
    if(view==V_DETAIL&&sel>=0){ if(SET[sel].flags&SF_BL) bl_set(SET[sel].val); if(SET[sel].flags&SF_LOCAL) lang_set(SET[sel].val); }
    view=v; sel=-1; xsel=-1; tr.type=TR_NONE; rip.on=0; memset(&tch,0,sizeof tch); stp.active=0;
    if(v==V_MAIN){ page=PG_HOME; act.gkind=-1; scroll[PG_HOME]=0; }
    static_dirty=1; force_all=1; ui_kick=1;
}
static void pw_off_begin(ms_t now){
    /* v6.16: the alarm = the next pre-heat occurrence, exactly as stock's performSleepActions -> powerOffSend(day, hour, minute);
     * no schedule (or no valid clock) -> 0/0/0 as in v6.14 */
    struct tm tm; int ad,ah,am; pw_now_tm(&tm); sched_alarm(clk_utc(),&ad,&ah,&am,NULL);
    pw_cmd8(pw.f8,&tm,ad,ah,am); pw.f8_ok=1; uart_send(pw.f8,15); arm.sent=0;   /* the off frame supersedes any pre-arm */
    pw.state=PW_OFFING; pw.t_req=pw.t_state=now; pw.push_set=0; pw.t_peek=0; pw.t_on1=0; pw.n_off++;
    toast.dur=0; pw_view(V_OFF);
}
static void pw_fail(ms_t now){   /* PowerOff did not get through: back to ON (the light has not been turned off yet) */
    pw.state=PW_ON; pw.t_state=now; pw.n_fail++; if(bl_is_off) bl_power(1); pw_view(V_MAIN); toast_show(TR(TX_T_PW_NOACK),C_ERR,now);
}
static void pw_on(ms_t now){
    pw.state=PW_ON; pw.t_state=now; pw.t_on1=0; pw.t_peek=0; pw.n_on++;
    arm.sent=0;                          /* v6.16: the MCU woke = CPU reset (0x326): its RAM alarm is gone -> sched_step re-arms */
    if(bl_is_off) bl_power(1);
    proto_keepawake(now,1);              /* PowerOn (sub0=[1]) + awake (sub1=[0]) — the same two frames as at startup (stock: app_init PowerOn) */
    pw_view(V_MAIN); toast_show(TR(TX_T_PW_ON),C_OK,now);
}
static void pw_wake(ms_t now){ pw.state=PW_WAKING; pw.t_state=now; pw.t_on1=0; pw.t_peek=0; if(bl_is_off) bl_power(1); static_dirty=1; ui_kick=1; }
/* button event from the k62091 node (all EV_KEY codes, like stock); value 2 (autorepeat) does not exist on the node and is ignored */
static void pw_key(int code,int val,ms_t now){
    pw.n_ev++; pw.key_code=code; pw.key_val=val; pw.t_key=now; ui_kick=1;
    idle_kick(now);                      /* v6.16 */
    if(val==1&&!pw.key_down){
        pw.key_down=1; pw.t_press=pw.t_down=now; pw.key_fired=0; pw.blk_shown=0; pw.n_press++;
        if(pw.state==PW_OFF){ pw.key_fired=1; pw_wake(now); }
    } else if(val==0&&pw.key_down){
        pw.key_down=0; if(pw.state==PW_ON&&!pw.key_fired) pw.n_short++;    /* short press: stock only wakes its screensaver — merakiplus has none */
        pw.key_fired=0;
    }
}
/* tap on the screen while off: swallowed (no navigation); the OFF page is shown for 8 s */
static int pw_touch(int ev,ms_t now){
    if(pw.state==PW_ON) return 0;
    if(ev==TE_DOWN&&pw.state==PW_OFF){ pw.t_peek=now+PW_PEEK_MS; if(bl_is_off) bl_power(1); }
    return 1;
}
/* the controller answers with word 0 = 1 (and has done so for PW_SEEN_MS): it is awake */
static int pw_mcu_on(ms_t now){
    if(link_up(now)&&reg_seen[0]&&reg[0]==1){ if(!pw.t_on1) pw.t_on1=now; return now-pw.t_on1>=PW_SEEN_MS; }
    pw.t_on1=0; return 0;
}
static void pw_step(ms_t now){
    if(pw.state==PW_ON&&pw.key_down&&!pw.key_fired){
        if(pw_busy()){ pw.t_down=now;                 /* stock: e60 does not count while the activity runs -> a new full hold after it */
            if(now-pw.t_press>=PW_HOLD_MS&&!pw.blk_shown){ pw.blk_shown=1; pw.n_blk++; toast_show(TR(TX_T_PW_BUSY),C_WARN,now); ui_kick=1; } }
        else if(now-pw.t_down<PW_HOLD_MS||view==V_SPLASH||view==V_BYE||sav.active){ /* still held / wait: startup, switch or SAVE in progress */ }
        else { pw.key_fired=1; ui_kick=1;
            if(ro_mode) toast_show(TR(TX_G_RO),C_WARN,now);
            else if(!link_up(now)||!reg_seen[0]||reg[0]==0){ pw.n_fail++; toast_show(TR(TX_T_PW_NOLINK),C_WARN,now); }   /* stock: mirror word 0 == 0 -> nothing */
            else pw_off_begin(now); }
    }
    switch(pw.state){
    case PW_OFFING:
        if(!pw.push_set&&now-pw.t_req>=PW_GAP_MS){
            uint16_t w[2]={0,0}; pw.push_set=1; pw.push_seq=wq.seq;
            if(!wq_push_t(0,w,2,I_POWEROFF)) pw_fail(now);
        } else if(pw.push_set&&wq.seq!=pw.push_seq&&!wq_busy()){
            if(wq.result==WR_OK){ pw.state=PW_OFF; pw.t_state=now; pw.t_on1=0; static_dirty=1; ui_kick=1; } else pw_fail(now);
        }
        break;
    case PW_OFF: {
        int lit=now-pw.t_req<PW_BL_MS||(pw.t_peek&&now<pw.t_peek);
        if(!lit){ pw.t_peek=0; if(!bl_is_off) bl_power(0); }
        if(pw_mcu_on(now)){ pw.n_auto++; pw_on(now); }
        break; }
    case PW_WAKING:
        if(pw_mcu_on(now)) pw_on(now);
        else if(now-pw.t_state>=PW_WAKE_MS){ pw.n_wto++; pw.state=PW_OFF; pw.t_state=now; pw.t_peek=0; static_dirty=1; ui_kick=1; }
        break;
    }
}
/* ================= v6.16: pre-arm + one-shot use + auto off (the stock semantics are at "TIME & SCHEDULING") =================
 * PRE-ARM: while ON (link, word 0 = 1, boot push done, write queue idle, not read-only) the alarm stock would send at power-off
 * (sched_alarm) is sent ahead with the same 15-byte cmd8 whenever it differs from what this MCU session last got: after a start,
 * after an MCU restart/wake (its RAM alarm is gone), after a schedule change, and once a day when the occurrence moves on.
 * Never while turning off/off (the power-off frame carries the alarm itself). No schedule and nothing armed -> nothing is sent
 * (v6.15 traffic unchanged); disarming sends alarm 0/0/0 once. At most one cmd8 per ARM_GAP_MS.
 * ONE-SHOT: once its occurrence (once_at) is a minute in the past it is switched off and saved (stock: deal_preheat_data at start).
 * AUTO OFF: no touch, no button and no activity (any active phase, not only brew/steam/grind) for N minutes -> the same power-off as
 * the button (pw_off_begin: cmd8 w. the alarm, 80 ms, cmd6 sub0 len2 [0,0]); a toast warns 30 s before. Only when the power-off
 * itself would be allowed (link, word 0 = 1, not read-only, no SAVE in flight). Stock: fixed 30 min (SleepRefresh 0x149268). */
#define ARM_GAP_MS   5000
#define AO_WARN_MS  30000
static ms_t idle_t0=0; static int ao_warned=0; static long ao_n=0;
static void idle_kick(ms_t now){ idle_t0=now; if(ao_warned){ ao_warned=0; toast.dur=0; } }   /* a touch also dismisses the 30 s warning */
static int sched_ok_to_send(ms_t now){
    return pw.state==PW_ON&&!ro_mode&&link_up(now)&&reg_seen[0]&&reg[0]==1&&(bp.state==BP_DONE||bp.state==BP_OFF)&&!wq_busy()&&view!=V_SPLASH&&view!=V_BYE;
}
static void sched_step(ms_t now){
    int64_t u=clk_utc();
    if(!idle_t0) idle_kick(now);                                                   /* first call: the idle timer starts now */
    if(sch.on&&!REP_MASK[sch.rep]&&sch.once_at>0&&clk_valid(u)&&u>=sch.once_at+60){   /* one-shot used */
        sch.on=0; sch.once_at=0; SET[S_PREHEAT].val=0; sched_save(); toast_show(TR(TX_T_ONCE_USED),C_OK,now); ui_kick=1;
    }
    if(arm.mcu_rs!=repush_count()){ arm.mcu_rs=repush_count(); arm.sent=0; }        /* MCU restart: the alarm in its RAM is gone */
    int ad,ah,am; int want=sched_alarm(u,&ad,&ah,&am,NULL);
    int need=arm.sent?(ad!=arm.ad||ah!=arm.ah||am!=arm.am):want;
    if(need&&sched_ok_to_send(now)&&(!arm.n_sent||now-arm.t_sent>=ARM_GAP_MS)){
        struct tm tm; pw_now_tm(&tm); pw_cmd8(arm.f8,&tm,ad,ah,am); arm.f8_ok=1; uart_send(arm.f8,15);
        arm.sent=want; arm.ad=ad; arm.ah=ah; arm.am=am; arm.t_sent=now; arm.n_sent++;
    }
    /* auto off */
    if(pw.state!=PW_ON||pw_busy()||phase_active(phase_raw(now))||act.pending!=ACT_NONE||pw.key_down){ idle_kick(now); return; }
    int mins=AO_MIN[SET[S_AUTOOFF].val>=0&&SET[S_AUTOOFF].val<5?SET[S_AUTOOFF].val:0];
    if(!mins||view==V_SPLASH||view==V_BYE||sav.active||ro_mode||!link_up(now)||!reg_seen[0]||reg[0]!=1){ if(!mins) ao_warned=0; return; }
    ms_t left=(ms_t)mins*60000-(now-idle_t0);
    if(left<=AO_WARN_MS&&!ao_warned){ ao_warned=1; toast_show(TR(TX_T_AUTOOFF_SOON),C_WARN,now); toast.dur=AO_WARN_MS; ui_kick=1; }
    if(left<=0){ ao_n++; idle_kick(now); pw_off_begin(now); }
}
/* ================= v6.18: SOUND — start jingle + one ready tone PER BOILER (docs/audio.md, UI_v6.md "CHANGES-v6.18") =================
 * STOCK (demo_v9, reversed): one sound thread (playpthread_init 0x137478 -> deal_play 0x13725C) waits on a semaphore that
 * set_volumn_type 0x1374B8 posts with a slot 0..10 (0x1A588C); the slot picks a file under /customer/audio/ (slide, keyboard,
 * LackWater, toasterror, fault, power, preheatCompele, nfc, brewcOrMilk x2, milkTempHight) and plays it with
 * mmplayer_play_audio_once 0x136CE8 -> mm_player_set_opts / mm_player_open / mm_player_set_volumn (libhvplayer.so, ffmpeg) ->
 * MI_AO (libmi_ao.so). Volume = config "volume" capped at 70 (deal_play 0x1372C4); libhvplayer maps it to
 * dB = min(vol*90/100 - 60, 15), vol 0 = mute (mm_player_set_volumn 0x563C). Nothing plays while the power-on logo runs
 * (model+0x15B0) or while a brew/steam animation video runs (model+0x15AD) — such a request is DROPPED.
 *  - START JINGLE: the audio track of video/power.mp4, played by the logo page (page_logo onEnter 0x1001B8) at every power-on.
 *  - "PRE-HEAT COMPLETE" (slot 6, preheatCompele.mp3): uartRxProcess 0x145BA0 — ONCE per power-on (latch model+0x15AA, cleared only by
 *    the logo page) when BOTH mirror words 62 AND 63 are > 99. The UI mirrors the MCU's registers at model+0xFE8+2*word, so
 *    model+0x1064/0x1066 = words 62/63 (also HOME's two arcs, home_date_update 0x10DFF0/0x10E0DC).
 * MCU (MEM03 boiler_heat 0x3502) writes words 62/63 as a heat STAGE 0/30/60/100, not a power:
 *    extract (62): e_boiler < 40.0 °C -> 0, < 60.0 -> 30, < brew set temp (word 11) -> 60, else 100 (+ an MCU-internal state test);
 *                  100 while work_sta bit 12. steam (63): s_boiler < 50.0 -> 0, < 80.0 -> 30, < steam target - 10.0 °C -> 60, else 100
 *                  (target by steam gear word 14: 1 = 120.0, 2 = 125.0, 3 = 130.0, else 135.0 °C); 100 while work_sta bit 5.
 * MERAKI+: the same signal, but PER BOILER — a ready tone when the EXTRACT boiler's stage first reaches 100 and a different one when
 * the STEAM boiler's does (fallback if a word was never received: word 40 >= word 11 / word 47 >= steam target - 10 °C).
 *  - ONCE per heat-up; RE-ARM only after that boiler has really cooled: stage <= 30, or 15.0 °C below its ready point (so the
 *    dips of a brew or a steam session never re-trigger it).
 *  - START JINGLE once per power-on: OFF/sleep -> on, and the first "on" after a cold boot of the display (SoC uptime < 120 s).
 *  - v6.19: at EVERY "on" (power-on with the jingle, and a Meraki+ start on a machine that was already on — switch from the stock
 *    UI, watchdog restart — without it) a boiler that is already hot (stage 100, decided over the first SND_SETTLE_MS so stale
 *    first samples do not count) counts as announced: a ready tone only for a real cold -> hot transition during the session
 *    (stock plays preheatCompele right after its logo even when both are hot; the user found jingle + 2 tones redundant).
 *  - SILENT while off / sleeping / TURNING ON… (nothing is queued), and a ready tone that comes due while an activity runs is
 *    dropped like stock drops it during its animations (logged "suppressed"). OFFLINE (no link) holds everything (no decision).
 *  - SOUND off (own file) / READY TONE per boiler / config volume 0 = mute: the event is logged, nothing is played.
 * Playback (device): the helper bin merakiplus_snd (glibc, dlopens the display's own libmi_sys.so + libmi_ao.so) is started per
 * sound with the file and the volume — WAV = our own PCM, sent to MI_AO exactly like libhvplayer's mm_audio_init/mm_audio_play
 * (48 kHz, 16 bit, 6 x 2048 frames, SetVolume in dB), MP3 (stock sounds) via libhvplayer's mm_player like stock. One sound at a
 * time, the rest queued (max SND_QMAX); the helper ends itself after 8 s (alarm) — merakiplus sends no signal; if one still runs
 * after SND_TIMEOUT_MS, sounds are skipped ("noplayer") until it has exited. No UART frame, display + audio only. */
#define SND_BOOT_WINDOW_S 120     /* SoC uptime at Meraki+'s start below this = a cold boot of the display (mains on) */
#define SND_REARM_DROP    150     /* 15.0 °C below the ready point re-arms a boiler's tone */
#define SND_VOL_CAP        70     /* stock deal_play: volume > 70 -> 70 */
#define SND_QMAX            4
#define SND_TIMEOUT_MS  10000     /* the helper ends itself after 8 s (alarm); merakiplus never sends it a signal */
#define SND_LOGN            8
#define SND_SETTLE_MS    1500     /* a start on a running machine: "already hot" is decided over the first 1.5 s of link */
enum { SE_JINGLE=0, SE_READY_E, SE_READY_S, SE_PREVIEW, NSE };
static const char*const SE_KEY[NSE]={"start_jingle","boiler_ready_extract","boiler_ready_steam","preview"};
enum { SO_PLAYED=0, SO_OFF, SO_MUTED, SO_SUPPRESSED, SO_NOPLAYER, SO_DROPPED, NSO };
static const char*const SO_KEY[NSO]={"played","off","muted","suppressed","noplayer","dropped"};
#define SND_DIR   "/new_customer/merakiplus"
#define SND_HELPER SND_DIR "/merakiplus_snd"
static const char*const SND_OURS[NSE]={SND_DIR "/audio/start.wav",SND_DIR "/audio/ready_extract.wav",SND_DIR "/audio/ready_steam.wav",SND_DIR "/audio/ready_extract.wav"};
static const char*const SND_STOCK[NSE]={"/customer/audio/power.mp3","/customer/audio/preheatCompele.mp3","/customer/audio/preheatCompele.mp3","/customer/audio/keyboard.mp3"};
static struct {
    int init, st, seen_off, cold_boot, settle, armed[2], ready[2];
    int n[NSE], n_played[NSE], n_out[NSO];
    ms_t t_on;
    struct { ms_t t; signed char ev, out; } log[SND_LOGN]; int log_n;
    int q[SND_QMAX], qn; int pid; ms_t t_start; int cur, last_rc, n_spawn, stuck, n_stuck;
    char last_file[64];
} snd;
static int snd_steam_thr(void){   /* MCU boiler_heat 0x36F6..0x3714 (target by gear) + 0x37F4 (ready = target - 10.0 °C) */
    int g=reg[14], t=g==1?1200:(g==2?1250:(g==3?1300:1350)); return t-100;
}
static int snd_ready(int b){
    int w=b?63:62;
    if(reg_seen[w]) return reg[w]>=100;                               /* the MCU's own heat stage (stock: > 99) */
    if(!b) return reg_seen[40]&&reg_seen[11]&&(int)reg[40]>=(int)reg[11];
    return reg_seen[47]&&(int)reg[47]>=snd_steam_thr();
}
static int snd_cooled(int b){
    int w=b?63:62;
    if(reg_seen[w]&&reg[w]<=30) return 1;                            /* stage 0/30: extract < 60 °C, steam < 80 °C */
    if(!b) return reg_seen[40]&&reg_seen[11]&&(int)reg[40]<(int)reg[11]-SND_REARM_DROP;
    return reg_seen[47]&&(int)reg[47]<snd_steam_thr()-SND_REARM_DROP;
}
/* 0 = no decision (no link while the UI thinks the machine is on), 1 = off / sleeping / turning on, 2 = on */
static int snd_machine(ms_t now){
    if(pw.state!=PW_ON||view==V_OFF) return 1;
    if(!link_up(now)) return 0;
    if(phase_raw(now)==PH_SLEEP||(reg_seen[0]&&reg[0]!=1)) return 1;
    return 2;
}
static int snd_vol(void){ int v=SET[S_VOL].loaded?SET[S_VOL].val:SET[S_VOL].def; return v<0?0:(v>SND_VOL_CAP?SND_VOL_CAP:v); }
static double snd_uptime_ovr=-1;                                  /* preview/selftest: a fixed SoC uptime (s) */
static int snd_cold_boot(void){
    char b[64]; int fd=snd_uptime_ovr>=0?-1:open(rp("/proc/uptime"),O_RDONLY); double up=snd_uptime_ovr;
    if(fd>=0){ int n=(int)read(fd,b,sizeof b-1); close(fd); if(n>0){ b[n]=0; up=atof(b); } }
    if(up<0) up=(double)now_ms()/1000.0;                             /* CLOCK_MONOTONIC = time since boot */
    return up<SND_BOOT_WINDOW_S;
}
static const char* snd_file(int ev){
    if(SET[S_SOUND].val==2&&access(rp(SND_STOCK[ev]),R_OK)==0) return SND_STOCK[ev];   /* the stock UI's own files (never shipped) */
    return SND_OURS[ev];
}
#ifdef DEVICE_RT
static int snd_spawn(int ev){
    char vb[8]; snprintf(vb,sizeof vb,"%d",snd_vol()); const char*f=snd_file(ev);
    if(access(SND_HELPER,X_OK)!=0||access(f,R_OK)!=0) return -1;
    fflush(stdout); fflush(stderr);
    pid_t c=fork(); if(c<0) return -1;
    if(c==0){
        setsid(); if(chdir("/")!=0){}
        int nul=open("/dev/null",O_RDWR), o=open("/tmp/merakiplus_snd.log",O_WRONLY|O_CREAT|O_APPEND,0644);
        if(nul>=0){ dup2(nul,0); dup2(o>=0?o:nul,1); dup2(o>=0?o:nul,2); }
        for(int fd=3;fd<1024;fd++) close(fd);
        signal(SIGINT,SIG_DFL); signal(SIGTERM,SIG_DFL);
        setenv("LD_LIBRARY_PATH","/lib:/customer/libs",1);           /* libmi_*.so (/lib) + libhvplayer & ffmpeg (stock's libs dir) */
        execl(SND_HELPER,"merakiplus_snd",f,vb,(char*)NULL);
        _exit(127);
    }
    snprintf(snd.last_file,sizeof snd.last_file,"%s",f);
    return (int)c;
}
#endif
/* the player: one at a time; device = helper process (reaped with waitpid, never signalled), SIM/preview = the event log only */
static void snd_service(ms_t now){
#ifdef DEVICE_RT
    if(snd.pid>0){
        int st=0; pid_t r=waitpid(snd.pid,&st,WNOHANG);
        if(r==snd.pid||r<0){ snd.last_rc=(r==snd.pid&&WIFEXITED(st))?WEXITSTATUS(st):-1; snd.pid=0; snd.stuck=0; }
        else { if(now-snd.t_start>SND_TIMEOUT_MS&&!snd.stuck){ snd.stuck=1; snd.n_stuck++; snd.qn=0; } return; }   /* a hung helper: no new sound until it is gone */
    }
    while(snd.qn>0&&snd.pid==0){
        int ev=snd.q[0]; memmove(snd.q,snd.q+1,(size_t)(--snd.qn)*sizeof snd.q[0]);
        int c=snd_spawn(ev); if(c>0){ snd.pid=c; snd.t_start=now; snd.cur=ev; snd.n_spawn++; } else snd.last_rc=127;
    }
#else
    (void)now; snd.qn=0;                                              /* SIM / preview: "played" = queued for the speaker */
#endif
}
static void snd_log(int ev,int out,ms_t now){
    snd.n[ev]++; snd.n_out[out]++; if(out==SO_PLAYED) snd.n_played[ev]++;
    memmove(snd.log+1,snd.log,sizeof snd.log[0]*(SND_LOGN-1)); snd.log[0].t=now; snd.log[0].ev=(signed char)ev; snd.log[0].out=(signed char)out;
    if(snd.log_n<SND_LOGN) snd.log_n++;
#ifdef DEVICE_RT
    { FILE*f=fopen("/tmp/merakiplus_snd.log","a");                   /* for the on-device check: one line per event */
      if(f){ fprintf(f,"%lld %s %s vol=%d mode=%d ready=%d\n",(long long)now,SE_KEY[ev],SO_KEY[out],snd_vol(),SET[S_SOUND].val,SET[S_SNDRDY].val); fclose(f); } }
#endif
}
static void snd_emit(int ev,ms_t now){
    int out=SO_PLAYED, m=SET[S_SOUND].val, r=SET[S_SNDRDY].val;
    if(m<=0) out=SO_OFF;
    else if((ev==SE_READY_E&&!(r&1))||(ev==SE_READY_S&&!(r&2))) out=SO_OFF;
    else if(snd_vol()<=0) out=SO_MUTED;                               /* stock: volume 0 = mute */
#ifdef DEVICE_RT
    else if(access(SND_HELPER,X_OK)!=0||snd.stuck) out=SO_NOPLAYER;
#endif
    else if(snd.qn>=SND_QMAX) out=SO_DROPPED;
    if(out==SO_PLAYED){
#ifndef DEVICE_RT
        snprintf(snd.last_file,sizeof snd.last_file,"%s",snd_file(ev));
#endif
        snd.q[snd.qn++]=ev; snd_service(now); }
    snd_log(ev,out,now);
}
static void snd_flush(void){ snd.qn=0; }                              /* off/sleep: nothing still waiting may play later */
static int snd_activity(ms_t now){ return phase_active(phase_raw(now))||steam_bits()||act.pending!=ACT_NONE; }
static void snd_tick(ms_t now){
    if(!snd.init){ memset(&snd,0,sizeof snd); snd.init=1; snd.cold_boot=snd_cold_boot(); snd.armed[0]=snd.armed[1]=1; }
    snd_service(now);
    int st=snd_machine(now);
    if(st==0) return;                                                 /* OFFLINE: hold (a link glitch is not a power cycle) */
    if(st==1){
        if(snd.st!=1){ snd.seen_off=1; snd.armed[0]=snd.armed[1]=1; snd_flush(); }
        snd.st=1; return;
    }
    if(snd.st!=2){                                                    /* -> on */
        int power_on=snd.seen_off||snd.cold_boot;
        snd.st=2; snd.t_on=now; snd.cold_boot=0;
        if(power_on) snd_emit(SE_JINGLE,now);
        snd.settle=1;          /* v6.19: power-on too — a boiler already hot now is announced (only the jingle plays); before: both armed */
    }
    if(snd.settle){            /* "already hot?" = the LAST sample of the window: the first ones after a (re)start / wake may be stale */
        for(int b=0;b<2;b++){ snd.ready[b]=snd_ready(b); snd.armed[b]=!snd.ready[b]; }
        if(now-snd.t_on>=SND_SETTLE_MS) snd.settle=0;
        return;
    }
    int busy=snd_activity(now);
    for(int b=0;b<2;b++){
        int r=snd_ready(b);
        if(!snd.armed[b]&&!r&&snd_cooled(b)) snd.armed[b]=1;
        if(snd.armed[b]&&r){ snd.armed[b]=0; if(busy) snd_log(b?SE_READY_S:SE_READY_E,SO_SUPPRESSED,now); else snd_emit(b?SE_READY_S:SE_READY_E,now); }
        snd.ready[b]=r;
    }
}
static void snd_preview(ms_t now){ if(snd_machine(now)==2) snd_emit(SE_PREVIEW,now); }
/* DIAGNOSTICS: "ON · 31" / "STOCK · 31" / "OFF" / "VOLUME 0" / "NO PLAYER" + the last event "BREW · PLAYED"; returns the mode cell's colour */
static uint32_t snd_diag_text(char*m,size_t mn,char*l,size_t ln){
    static const short EV[NSE]={TX_DV_SE_JINGLE,TX_DV_SE_BREW,TX_DV_SE_STEAM,TX_DV_SE_PREVIEW};
    static const short OUT[NSO]={TX_DV_SO_PLAYED,TX_DV_SND_OFF,TX_DV_SO_MUTED,TX_DV_SO_SKIP,TX_DV_SO_FAIL,TX_DV_SO_DROP};
    uint32_t c=C_TXT; int v=snd_vol(), md=SET[S_SOUND].val;
    if(md<=0){ snprintf(m,mn,"%s",TR(TX_DV_SND_OFF)); c=C_TXT2; }
    else if(v<=0){ snprintf(m,mn,"%s",TR(TX_DV_SND_MUTE)); c=C_WARN; }
#ifdef DEVICE_RT
    else if(access(SND_HELPER,X_OK)!=0){ snprintf(m,mn,"%s",TR(TX_DV_SND_NOHELPER)); c=C_WARN; }
#endif
    else snprintf(m,mn,TR(md==2?TX_DV_SND_STOCK:TX_DV_SND_ON),v);
    if(snd.log_n) snprintf(l,ln,"%s · %s",TR(EV[(int)snd.log[0].ev]),TR(OUT[(int)snd.log[0].out]));
    else snprintf(l,ln,"%s",TR(TX_DV_T_NONE));
    return c;
}
static void ui_touch(int ev,int x,int y,ms_t now){
    ui_kick=1;
    idle_kick(now);                                                  /* v6.16: every touch restarts the auto-off timer (stock touch_feedback_cb) */
    if(pw_touch(ev,now)) return;                                     /* v6.14: off — the screen shows only the OFF page */
    if(ev==TE_DOWN){
        memset(&tch,0,sizeof tch); tch.down=1; tch.x0=tch.x=x; tch.y0=tch.y=y; tch.t0=now;
        tch.target=hit_test(x,y,now); tch.scroll0=scroll[page];
        if(tch.target){ rip.x=x; rip.y=y; rip.t0=now; rip.on=1; }
        if((tch.target==T_MINUS||tch.target==T_PLUS)&&!sav.active){
            if(view==V_SYS) x_edit=(tch.target==T_PLUS);               /* BOOT UI: − = ORIGINAL, + = MERAKIPLUS */
            else { edit_step(tch.target==T_PLUS?1:-1,1); tch.rep_next=now+420; }
        }
    } else if(ev==TE_MOVE&&tch.down){
        tch.x=x; tch.y=y; int dx=x-tch.x0, dy=y-tch.y0;
        if(!tch.moved&&(abs(dx)>14||abs(dy)>14)){
            tch.moved=1; tch.axis=abs(dy)>abs(dx)?2:1;                       /* v6.11: axis lock at first movement (1 = horizontal, 2 = vertical) */
            if(view==V_MAIN&&page!=PG_HOME&&tch.axis==2&&tch.y0>=LIST_VY0) tch.scrolling=1;   /* lists + DIAGNOSE (v6.17: a drag that starts on the header is the close gesture) */
        }
        if(tch.scrolling) scroll[page]=clampf(tch.scroll0-dy,0,scroll_max(page));
    } else if(ev==TE_UP&&tch.down){
        tch.down=0; int dx=tch.x-tch.x0, dy=tch.y-tch.y0;
        if(tch.scrolling) return;
        int stepper=tch.target==T_MINUS||tch.target==T_PLUS;
        if(!tr.type&&dx>0&&swipe_h(tch.axis,dx,dy)&&!stepper){                 /* v6.17: swipe right = back (the chevron's shortcut) */
            if(view==V_MAIN&&page!=PG_HOME){ nav_back(now); return; }
            if(view==V_DETAIL||view==V_SYS){ activate(T_CANCEL,now); return; }
        }
        if((view==V_DETAIL||view==V_SYS)&&!tr.type&&swipe_down(tch.axis,tch.target,dx,dy)){ activate(T_CANCEL,now); return; }   /* v6.11: swipe down = close */
        if(view==V_MAIN&&page!=PG_HOME&&!tr.type&&tch.y0<LIST_VY0&&swipe_down(tch.axis,tch.target,dx,dy)){ nav_back(now); return; }   /* v6.17: pulling the header down closes a page (inside the list a vertical drag scrolls) */
        if(!tch.moved&&tch.target&&!tch.fired&&tch.target!=T_MINUS&&tch.target!=T_PLUS&&hit_test(tch.x,tch.y,now)==tch.target) activate(tch.target,now);
    }
}
static int pressed(int t){ return tch.down&&!tch.moved&&tch.target==t; }

