/* src/mp/runtime.c — boot push, MCU-restart re-push, main-loop timers, signal handler.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ---- BOOT-PUSH (PARITY §1.4) — v6.4: SIM/preview; v6.5: ALSO THE DEVICE ----
 * At startup the stock UI's fuart_thread sends its config mirror to the controller:
 *     cmd6 sub1 len30 (words 1..30)   +   cmd6 sub112 len3 (words 112..114)
 * merakiplus now does the same in all build types. Without it, after a slot B boot (where the stock UI
 * never starts) the controller sits on its own power-on defaults (REGISTERMAP §2: word 11 = 920,
 * word 114 = 50, word 14 = 4, word 25 = 550 …) instead of the config's. In run.sh mode the
 * paused stock UI has already sent the blocks; merakiplus then sends the same values again (no effect).
 * The word layout is stock's (model_data_init 0xcef40 + fuart_thread 0x146810): words 2/4/11/14/17/23/25
 * from config, words 5/13/15 = 60, all other words in 1..30 = 0, words 112/113 packed, word 114 = headTemper.
 * The blocks go through the SAME gate as everything else (wq_push without intent -> frame policy): start words
 * and words 26/27/30 are 0, and a config value out of spec gets the WHOLE block rejected and counted (BLOK) —
 * it is never sent. Each block is sent once (w/ the queue's ACK wait + single retransmit); if the
 * ACK does not arrive, it continues with the next block. The result is shown on DIAGNOSE (and in the SIM's state JSON).
 * After a completed SAVE, a push that was rejected, not ACKed or lacked config is repeated.
 *
 * ACTIVITY GUARD (guard, device): the block writes the start words 3/6/9/16/22/24 as 0 and could
 * thereby abort a brew that is already running — e.g. when run.sh is started in the middle of an activity,
 * or merakiplus is restarted during one. Hence it is decided ONCE, the first time the link is up and
 * work_sta (word 33) has been read: if grind/brew/water/steam is running (phase_active), the push is SKIPPED
 * (BPR_SKIP) — it is not deferred and not repeated by a later SAVE. The controller then has the
 * values that activity was started with; every SAVE and every start block still writes its words.
 * Without the guard (the SIM contract, unchanged from v6.4) the push is instead DEFERRED until the activity has ended.
 * An activity that starts AFTER the decision defers the remaining blocks (both modes). */
/* v6.7 — WORD 113 IN THE BOOT PUSH: the lo byte (breakTime) is sent as 0, not as config's breakTime. Stock's own
 * boot push was captured on the device (boot/ondevice/GROUND-TRUTH.md): sub112 len3 = 04 01 | 00 06 | 46 00, i.e.
 * word 113 = 0x0600 with breakTime = 5 in config — fuart_thread clears the lo byte at boot. PARITY §1.3's
 * packing (time<<8 | breakTime) applies to the SAVE path (setting_mcu_push / act_push), which is unchanged. No effect
 * on brewing: the controller (0x1E10) overwrites the lo byte itself on the next brew. The hi byte (pre-inf. time >= 5)
 * keeps the word != 0, so the re-push signature (word 112 = 0 and word 113 = 0) is still unambiguous. */
static void boot_push_words(uint16_t a[30],uint16_t p[3]){   /* a[k] = word k+1 */
    memset(a,0,30*sizeof a[0]);
    a[1-1]=0; a[2-1]=(uint16_t)sv(S_GRINDMODE); a[4-1]=(uint16_t)sv(S_GRINDW); a[5-1]=60;
    a[11-1]=(uint16_t)sv(S_BREWT); a[13-1]=60; a[14-1]=(uint16_t)sv(S_STEAMG); a[15-1]=60;
    a[17-1]=(uint16_t)sv(S_HOTW); a[23-1]=(uint16_t)sv(S_BREWW); a[25-1]=(uint16_t)sv(S_STEAMT);
    p[0]=pack112(sv(S_PISW),sv(S_PIPRESS)); p[1]=pack113(sv(S_PITIME),0); p[2]=(uint16_t)sv(S_HEADT);   /* v6.7: word 113 lo = 0 — see below */
}
static int boot_push_cfg_ok(void){ for(int i=0;i<NSET;i++) if(SET[i].mcu!=M_NONE&&!SET[i].loaded) return 0; return 1; }
static void boot_push_start(int guard){ bp.state=BP_WAIT_A; bp.ra=bp.rb=BPR_NONE; bp.guard=guard; bp.t0_set=0; }
#define BP_LEFT(now) do{ if(!bp.t0_set){ bp.t0=(now); bp.t0_set=1; } }while(0)   /* first block queued / push decided */
static void boot_push_step(ms_t now){
    if(bp.state==BP_OFF||bp.state==BP_DONE||ro_mode) return;
    if(bp.state==BP_A||bp.state==BP_B){
        if(wq.seq==bp.seq) return;                         /* waiting for ACK / timeout */
        int r=(wq.result==WR_OK)?BPR_OK:BPR_NOACK;
        if(bp.state==BP_A){ bp.ra=r; bp.state=BP_WAIT_B; } else { bp.rb=r; bp.state=BP_DONE; bp.runs++; }
        return;
    }
    if(bp.guard){                                          /* activity guard: decided once, when work_sta is known */
        if(!link_up(now)||!reg_seen[33]) return;
        bp.guard=0;
        if(phase_active(phase_raw(now))){ bp.ra=bp.rb=BPR_SKIP; bp.state=BP_DONE; bp.runs++; BP_LEFT(now); return; }
    }
    if(!link_up(now)||wq_busy()||sav.active||act.pending!=ACT_NONE||phase_active(phase_raw(now))) return;
    if(!boot_push_cfg_ok()){ bp.ra=bp.rb=BPR_NOCFG; bp.state=BP_DONE; bp.runs++; BP_LEFT(now); return; }   /* no config = nothing to push */
    uint16_t a[30],p[3]; boot_push_words(a,p); long seq=wq.seq;
    BP_LEFT(now);
    if(bp.state==BP_WAIT_A){ if(wq_push(1,a,30)){ bp.seq=seq; bp.state=BP_A; } else { bp.ra=BPR_BLOCKED; bp.state=BP_WAIT_B; } }
    else { if(wq_push(112,p,3)){ bp.seq=seq; bp.state=BP_B; } else { bp.rb=BPR_BLOCKED; bp.state=BP_DONE; bp.runs++; } }
}
/* a SAVE has gone through: a push that was rejected / not ACKed / lacked config is retried
 * (config may now be in spec). A push that the guard skipped is not repeated. */
static int bp_failed(int r){ return r==BPR_NOACK||r==BPR_BLOCKED||r==BPR_NOCFG; }
static void boot_push_resave(void){ if(bp_on&&bp.state==BP_DONE&&(bp_failed(bp.ra)||bp_failed(bp.rb))) boot_push_start(0); }

/* ---- MCU RESTART -> RE-PUSH (v6.6; the open item in CHANGES-v6.5 item 8) ----
 * The controller's register file lives in RAM and gets fixed power-on defaults at every MCU startup (REGISTERMAP §2:
 * word 4 = 180, 11 = 920, 14 = 4, 17 = 360, 23 = 360, 25 = 550, 112 = 0, 113 = 0, 114 = 50). If the MCU
 * restarts while merakiplus is running, the boot push is gone. The stock UI has the same gap; merakiplus closes it:
 *  (a) THE VERSION QUERY COMES BACK. The MCU sends cmd 0x89 every 20 ms from its startup until it
 *      has received a reply (PROTOCOL §2.2) — and only then (0 of them in 12 min of live traffic, STEAMLIVE §2). A 0x89
 *      arriving more than RP_STRAY_MS after the boot push left WAITING is therefore an MCU startup
 *      AFTER our push. (The first handshake precedes the push; a straggler from it falls within
 *      RP_STRAY_MS.) Once the burst has settled (RP_SETTLE_MS without a new 0x89) and any push in progress
 *      has finished, the boot push is restarted from scratch.
 *  (b) THE DEFAULTS SIGNATURE: word 112 = 0 AND word 113 = 0. These are the MCU's power-on defaults, and they can never
 *      be in-spec config values (word 112 lo = pre-inf. pressure 3..4; word 113 hi = pre-inf. time >= 5)
 *      — so the signature unambiguously says that words 112..114 have not been written since the MCU's startup. It must
 *      hold for RP_SIG_READS fresh reads in a row (poll every 1 s) while the machine is idle and config is
 *      in spec. Catches a restart where the 0x89 burst was lost — and a boot push the guard skipped
 *      while the controller was actually on its defaults (slot B boot with the wand open).
 * The re-push IS the boot push: same blocks (sub1 len30 + sub112 len3), same frame policy, same
 * write queue (ACK wait + one retransmit) and the SAME ACTIVITY GUARD — decided on a work_sta read AFTER
 * the restart (reg_seen[33] is cleared). If an activity is running at that moment it is skipped; if
 * the signature is still there afterwards, (b) tries again when the machine is idle.
 * Rate limit: at least RP_MIN_MS between two re-pushes, and at most RP_BUDGET signature-triggered ones per
 * RP_WINDOW_MS (a controller that does not accept them does not cause an endless stream of writes).
 * Counted on DIAGNOSE (the row is then labelled "RE-PUSH 2 · 1-30" | "112-114") and in the SIM's state JSON. */
#define RP_STRAY_MS  200
#define RP_SETTLE_MS 300
#define RP_MIN_MS    5000
#define RP_SIG_READS 3
#define RP_BUDGET    3
#define RP_WINDOW_MS 600000
enum { RPC_NONE=0, RPC_VER, RPC_DEF };
static struct { unsigned vn, sig_n0; int armed, sig_on, started, last, n_ver, n_def, def_k; ms_t t_start, def_t[RP_BUDGET]; } rps;
static int bp_guard_cfg=1;             /* the guard for a re-push: device 1; SIM follows MERAKIPLUS_SIM_BOOTPUSH_GUARD */
static int repush_count(void){ return rps.n_ver+rps.n_def; }
static int repush_cfg_sane(void){ for(int i=0;i<NSET;i++){ const Setting*q=&SET[i]; if(q->mcu!=M_NONE&&(!q->loaded||set_oor(q))) return 0; } return 1; }
static int mcu_defaults_sig(void){ return reg_seen[112]&&reg_seen[113]&&reg[112]==0&&reg[113]==0; }
static void repush_begin(int cause,ms_t now){
    rps.armed=0; rps.sig_on=0; rps.last=cause; rps.started=1; rps.t_start=now;
    if(cause==RPC_VER) rps.n_ver++; else { rps.n_def++; rps.def_t[rps.def_k++%RP_BUDGET]=now; }
    reg_seen[33]=0;                    /* the guard is decided on a work_sta read AFTER the restart */
    boot_push_start(bp_guard_cfg);
}
static void repush_step(ms_t now){
    if(!bp_on||ro_mode||bp.state==BP_OFF) return;
    if(ver_n!=rps.vn){ rps.vn=ver_n; if(bp.t0_set&&ver_ms-bp.t0>=RP_STRAY_MS) rps.armed=1; }       /* (a) */
    if(bp.state!=BP_DONE){ rps.sig_on=0; return; }                                               /* one push at a time */
    if(rps.started&&now-rps.t_start<RP_MIN_MS){ rps.sig_on=0; return; }
    if(rps.armed){ if(now-ver_ms>=RP_SETTLE_MS) repush_begin(RPC_VER,now); return; }
    int sig=link_up(now)&&mcu_defaults_sig()&&repush_cfg_sane()&&!phase_active(phase_raw(now))&&!wq_busy()&&!sav.active&&act.pending==ACT_NONE;   /* (b) */
    if(!sig){ rps.sig_on=0; return; }
    if(!rps.sig_on){ rps.sig_on=1; rps.sig_n0=rx_n112; return; }
    if(rx_n112-rps.sig_n0<RP_SIG_READS) return;                                                  /* 3 fresh reads, all while the machine is idle */
    if(rps.def_k>=RP_BUDGET&&now-rps.def_t[rps.def_k%RP_BUDGET]<RP_WINDOW_MS) return;              /* the budget is used up */
    repush_begin(RPC_DEF,now);
}

/* ---- the main loop's timers (v6.6) ----
 * Gathered in one place and in 64 bits, so the selftest runs EXACTLY the code main() runs (the wraparound test
 * drives loop_poll / loop_graph / loop_render_due / loop_rendered across 2^31 and 2^32 ms).
 * The deadlines are absolute times in ms_t; with the 32-bit clock up to and including v6.5 a deadline in
 * the clock's last milliseconds could be skipped and only become true again 49.7 days later. */
static struct { ms_t poll, slow, ext, render, graph, ft_reset; unsigned alt; } lt;
/* poll: sub0/sub33 @50 ms alternating + sub56 @200 ms (~10 Hz, like v4) + words 112..114 @1 s */
static void loop_poll(ms_t now){
    if(now>=lt.poll){ lt.poll=now+50; if(lt.alt++&1) frame_query(33,7); else frame_query(0,56); }
    if(now>=lt.slow){ lt.slow=now+200; frame_query(56,56); }
    if(now>=lt.ext){ lt.ext=now+1000; frame_query(112,3); }
}
static void loop_graph(ms_t now){
    if(reg_seen[51]&&now-lt.graph>=100){ lt.graph=now; graph_push(now,reg[51],wsrc_tick(now)); }   /* v6.9: latched weight source */
}
static int loop_render_due(ms_t now){ if(ui_kick){ ui_kick=0; lt.render=now; } return now>=lt.render; }
static void loop_rendered(ms_t now,float ft){
    ft_last=ft; if(now-lt.ft_reset>5000){ lt.ft_reset=now; ft_max=0; } if(ft_last>ft_max) ft_max=ft_last;
    lt.render=now+((ui_anim||tch.down||tr.type)?33:100);
}

/* ====================================================================== */
/* ================= main: device / SIM / preview ======================= */
/* ====================================================================== */
static void onsig(int s){ (void)s; SIM_SIG(s) run=0; }

