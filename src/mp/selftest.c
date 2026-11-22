/* src/mp/selftest.c — PREVIEW build: --selftest, bench, previews, entry point.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ====================== v6.5: selftest — boot push on the device + RX resync ====================== */
static int mk_frame(uint8_t*o,uint8_t cmd,uint8_t sub,const uint16_t*w,int len){
    int n=0; o[n++]=0xAA; o[n++]=0x55; o[n++]=cmd; o[n++]=sub; o[n++]=(uint8_t)len;
    for(int k=0;k<len;k++){ o[n++]=w[k]&0xFF; o[n++]=w[k]>>8; }
    uint16_t c=crc16(o,n); o[n++]=c&0xFF; o[n++]=c>>8; return n;
}
static void rx_zero(void){ rxlen=0; rx_pend_ms=rx_last_ms=0; rx_ok=rx_bad=rx_resync=0; last_rx_ms=last_bad_ms=-100000; }
/* one iteration of main(): first whatever has arrived, then the watchdog call */
static void rx_tick(const uint8_t*d,int n,ms_t t){ if(n>0) rx_feed(d,n,t); rx_feed(NULL,0,t); }
/* status reply as the controller sends it: 0x83 sub0 len56 (119 bytes); word 55 = sequence number k */
#define RXV_N 119
static int rx_v(uint8_t*o,int k){ uint16_t w[56]; memset(w,0,sizeof w); w[0]=1; w[33]=0x0101; w[55]=(uint16_t)k; return mk_frame(o,0x83,0,w,56); }
static void st_status(uint16_t ws,ms_t now){ uint8_t f[32]; uint16_t w[7]={ws,0,0,0,0,0,0}; int n=mk_frame(f,0x83,33,w,7); rx_feed(f,n,now); }
/* the controller's ACK to the most recently sent cmd6 frame: 0x86 w/ the same sub/len and an echo of the payload */
static void st_ack_last(ms_t now){ uint8_t a[80]; int n=last_tx_len; if(n<7) return;   /* v6.13: nothing sent yet (the mutant tests) -> no ACK */ memcpy(a,last_tx,n); a[2]=0x86; uint16_t c=crc16(a,n-2); a[n-2]=c&0xFF; a[n-1]=c>>8; rx_feed(a,n,now); }
static void st_bp_reset(void){
    memset(&wq,0,sizeof wq); memset(&bp,0,sizeof bp); memset(&sav,0,sizeof sav); act.pending=ACT_NONE; txq_len=0; last_tx_len=0;
    memset(reg,0,sizeof reg); memset(reg_seen,0,sizeof reg_seen); rx_zero(); ro_mode=0;
}
/* fixture config in the stock UI's cJSON format; ALL boot-push fields differ from artifacts/config.json and from the MCU's power-on defaults */
static int st_fixture(const char*path,int brew_temp,int with_brew){
    char b[1400]; int n=snprintf(b,sizeof b,"{\n\t\"Temperature\":\t1,\n\t\"timemode\":\t1,\n\t\"Automatically\":\t1,\n\t\"timeZone\":\t48,\n\t\"backlight\":\t9,\n\t\"volume\":\t31,\n"
        "\t\"sleepMode\":\t1,\n\t\"sleepModeSetting\":\t0,\n\t\"clearTip\":\t0,\n\t\"brewMode\":\t0,\n");
    if(with_brew) n+=snprintf(b+n,sizeof b-n,"\t\"brewTemper\":\t%d,\n",brew_temp);
    n+=snprintf(b+n,sizeof b-n,"\t\"autoGrindWeight\":\t172,\n\t\"grindMode\":\t2,\n\t\"steamGears\":\t2,\n\t\"preInfusionPressure\":\t3,\n\t\"preInfusionTime\":\t9,\n"
        "\t\"breakTime\":\t7,\n\t\"headTemper\":\t60,\n\t\"preInfusionSwitch\":\t0,\n\t\"sumtime\":\t0,\n\t\"autobrewWeight\":\t420,\n\t\"language\":\t0,\n"
        "\t\"AtuoSteamTarge\":\t600,\n\t\"hotWaterShortcut\":\t1,\n\t\"autoHotWaterTarget\":\t1500,\n\t\"presetStore\":\t{\n\t},\n\t\"softversion\":\t\"1.1.1-69\",\n\t\"hotWaterMode\":\t1\n}");
    return put_bytes(path,b,n);
}
/* independent vectors (computed outside the program, own CRC): stock's boot push for the fixture config */
#define BPV_A "aa 55 06 01 1e 00 00 02 00 00 00 ac 00 3c 00 00 00 00 00 00 00 00 00 00 00 a2 03 00 00 3c 00 02 00 3c 00 00 00 dc 05 00 00 00 00 00 00 00 00 00 00 a4 01 00 00 58 02 00 00 00 00 00 00 00 00 00 00 88 89"
#define BPV_B "aa 55 06 70 03 03 00 00 09 3c 00 e8 da"        /* v6.7: word 113 lo byte = 0 (stock's captured boot push); CRC computed outside the program */
/* ... and for artifacts/config.json (870 / 185 / 1 / 3 / 400 / 360 / 650) */
#define BPV_A_REAL "aa 55 06 01 1e 00 00 01 00 00 00 b9 00 3c 00 00 00 00 00 00 00 00 00 00 00 66 03 00 00 3c 00 03 00 3c 00 00 00 90 01 00 00 00 00 00 00 00 00 00 00 68 01 00 00 8a 02 00 00 00 00 00 00 00 00 00 00 62 3e"
static void st_bootpush_device(const char*cfgsrc){
    printf("== selftest: v6.5 boot push on the device (activity guard ON; fixture config; ACKs through rx_feed) ==\n");
    __typeof__(wq) wq0=wq; uint16_t reg0[REGN]; int seen0[REGN]; memcpy(reg0,reg,sizeof reg); memcpy(seen0,reg_seen,sizeof reg_seen);
    ms_t rxl0=last_rx_ms, ok0=rx_ok, bad0=rx_bad; int tx0=txq_len, ro0=ro_mode, bpon0=bp_on;
    char dir[300], fx[340]; snprintf(dir,sizeof dir,"/tmp/merakiplus_v6_selftest_%d",(int)getpid()); mkdir(dir,0755); snprintf(fx,sizeof fx,"%s/bootpush_fixture.json",dir);
    ms_t T=30000000L;
    /* --- the real config: the block is stock's, byte for byte --- */
    st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T); wq_step(T);
    st_check("artifacts/config.json: sub1 len30 on the wire = independent vector (… 66 03 … b9 00 … 62 3e), 67 bytes",bp.state==BP_A&&hexeq(last_tx,last_tx_len,BPV_A_REAL)&&txq_len==67);
    /* --- fixture --- */
    int fxok=st_fixture(fx,930,1); snprintf(cfg_path,sizeof cfg_path,"%s",fx); settings_load();
    { uint16_t a[30],p[3]; boot_push_words(a,p);
      static const uint16_t ea[30]={0,2,0,172,60,0,0,0,0,0,930,0,60,2,60,0,1500,0,0,0,0,0,420,0,600,0,0,0,0,0};
      st_check("fixture config loaded; words 1..30 = 0,2,0,172,60,…,930,0,60,2,60,0,1500,…,420,0,600,0…; words 112..114 = 0003 0900 60 (v6.7: the breakTime byte is 0 in the boot push)",
               fxok&&cfg_ok&&boot_push_cfg_ok()&&!memcmp(a,ea,sizeof ea)&&p[0]==0x0003&&p[1]==0x0900&&p[2]==60); }
    /* --- the guard waits until work_sta is known --- */
    st_bp_reset(); boot_push_start(1); boot_push_step(T);
    int w1=bp.state==BP_WAIT_A&&bp.guard&&wq.n==0;                                    /* no link */
    { static const uint8_t vr[8]={0xAA,0x55,0x89,0x01,0x00,0x00,0x6F,0x81}; rx_feed(vr,8,T+5); }   /* the controller's version query: link up, work_sta unknown */
    boot_push_step(T+6); int w2=link_up(T+6)&&!reg_seen[33]&&bp.state==BP_WAIT_A&&bp.guard&&wq.n==0&&txq_len==15;   /* only the version reply has been sent */
    st_check("guard: nothing is sent without a link, nor with a link as long as work_sta (word 33) is unknown",w1&&w2);
    /* --- idle machine: sub1 len30 -> ACK -> sub112 len3 -> ACK --- */
    st_status(0x0101,T+10); boot_push_step(T+10);
    int qa=bp.state==BP_A&&!bp.guard&&wq.n==1&&wq.q[0].sub==1&&wq.q[0].len==30; const WFrame fa=wq.q[0];
    wq_step(T+10);
    st_check("idle (work_sta 0101): sub1 len30 on the wire = independent vector for the fixture (67 bytes)",qa&&hexeq(last_tx,last_tx_len,BPV_A)&&txq_len==15+67);
    { int z=1; static const int zero[]={1,3,6,7,8,9,10,12,16,18,19,20,21,22,24,26,27,28,29,30}; for(unsigned q=0;q<sizeof zero/sizeof zero[0];q++) if(fa.w[zero[q]-1]) z=0;
      st_check("trigger-word policy: words 26/27/30 = 0, start words 3/6/9/16/22/24 = 0, words 5/13/15 = 60, other unknown words = 0 (independent audit)",
               z&&frame_trig_audit(&fa,0)&&fa.w[4]==60&&fa.w[12]==60&&fa.w[14]==60); }
    st_ack_last(T+35); boot_push_step(T+36); int a1=bp.ra==BPR_OK&&bp.state==BP_WAIT_B;
    boot_push_step(T+36); wq_step(T+36); int b1=bp.state==BP_B&&hexeq(last_tx,last_tx_len,BPV_B)&&txq_len==15+67+13;
    st_ack_last(T+60); boot_push_step(T+61);
    st_check("ACK (0x86 w/ echo) -> sub112 len3 = independent vector (… 03 00 00 09 3c 00 e8 da) -> ACK -> done: OK / OK, 0 retransmits, 0 failures, 0 blocks",
             a1&&b1&&bp.state==BP_DONE&&bp.rb==BPR_OK&&bp.runs==1&&wq.n==0&&wq.n_ok==2&&wq.n_retx==0&&wq.n_fail==0&&wq.n_block==0&&txq_len==15+67+13);
    st_check("controller mirror after ACK = config: word 11 = 930, word 4 = 172, word 2 = 2, word 14 = 2, word 17 = 1500, word 23 = 420, word 25 = 600, words 112..114 = 0003 0900 60",
             reg[11]==930&&reg[4]==172&&reg[2]==2&&reg[14]==2&&reg[17]==1500&&reg[23]==420&&reg[25]==600&&reg[112]==0x0003&&reg[113]==0x0900&&reg[114]==60&&reg[5]==60&&reg[30]==0);
    { ms_t t=T+70; for(int k=0;k<5000;k++){ t+=2; rx_feed(NULL,0,t); wq_step(t); boot_push_step(t); }
      st_check("the boot push is sent ONCE: 10 s later nothing new has been sent",bp.state==BP_DONE&&bp.runs==1&&txq_len==15+67+13&&wq.n==0); }
    { uint32_t c0,c1; const char*s0=bp_text(0,&c0),*s1=bp_text(1,&c1);
      st_check("DIAGNOSE: BOOT-PUSH 1-30 = OK (green), BOOT-PUSH 112-114 = OK (green)",s0==TR(TX_DV_OK)&&s1==TR(TX_DV_OK)&&c0==C_OK&&c1==C_OK); }
    /* --- ACK wait + one retransmit --- */
    st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T); wq_step(T);
    wq_step(T+149); int r0=wq.n_retx==0&&txq_len==67; wq_step(T+150); int r1=wq.n_retx==1&&txq_len==2*67&&hexeq(last_tx,last_tx_len,BPV_A);
    wq_step(T+160); int r2=txq_len==2*67; st_ack_last(T+170); boot_push_step(T+171);
    st_check("sub1 len30 without ACK: no retransmit before 150 ms, exactly ONE (same bytes) at 150 ms; ACK to it -> OK",r0&&r1&&r2&&bp.ra==BPR_OK&&wq.n_fail==0);
    boot_push_step(T+171); wq_step(T+171); wq_step(T+171+150); wq_step(T+171+150+299); int n0=wq.n==1&&wq.n_retx==2; wq_step(T+171+150+300); boot_push_step(T+171+450);
    st_check("sub112 len3 with no ACK at all: 1 retransmit, given up after 150 + 300 ms -> NO ACK; the push is done (no further attempts on its own)",
             n0&&bp.state==BP_DONE&&bp.ra==BPR_OK&&bp.rb==BPR_NOACK&&wq.n==0&&wq.n_fail==1&&txq_len==2*67+2*13);
    st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T); wq_step(T); wq_step(T+150); wq_step(T+450); boot_push_step(T+450); boot_push_step(T+450); wq_step(T+450);
    st_check("sub1 len30 with no ACK at all: NO ACK, and sub112 len3 is sent anyway",bp.ra==BPR_NOACK&&bp.state==BP_B&&hexeq(last_tx,last_tx_len,BPV_B)&&wq.n_retx==1&&wq.n_fail==1);
    /* --- the activity guard --- */
    { static const uint16_t act_ws[]={0x0141,0x1107,0x1307,0x030B,0x4121}; int ok=1;   /* grind, pre-inf., extraction, hot water, steam (SESSION.md §6) */
      for(unsigned q=0;q<sizeof act_ws/sizeof act_ws[0];q++){
          st_bp_reset(); boot_push_start(1); st_status(act_ws[q],T); boot_push_step(T);
          int skip=bp.state==BP_DONE&&bp.ra==BPR_SKIP&&bp.rb==BPR_SKIP&&bp.runs==1&&wq.n==0&&txq_len==0;
          ms_t t=T; for(int k=0;k<300;k++){ t+=100; st_status(k<50?act_ws[q]:0x0101,t); wq_step(t); boot_push_step(t); }   /* the activity ends after 5 s; 25 s idle */
          boot_push_resave();                                                                                         /* a later SAVE does not resume it either */
          boot_push_step(t+100);
          if(!(skip&&bp.state==BP_DONE&&bp.ra==BPR_SKIP&&wq.n==0&&txq_len==0&&wq.n_block==0)){ ok=0; printf("     work_sta %04X: state=%d ra=%d wq.n=%d tx=%d\n",act_ws[q],bp.state,bp.ra,wq.n,txq_len); } }
      st_check("ACTIVITY GUARD: work_sta 0141/1107/1307/030B/4121 at startup -> the push is SKIPPED: 0 bytes sent — not after the activity or a SAVE either",ok);
      uint32_t c0; st_check("DIAGNOSE: BOOT-PUSH = SPRUNGET OVER / SKIPPED (neutral colour)",bp_text(0,&c0)==TR(TX_DV_BP_SKIP)&&c0==C_TXT&&bp_text(1,&c0)==TR(TX_DV_BP_SKIP)); }
    { static const uint16_t idle_ws[]={0x0101,0x0001,0x0000}; int ok=1;                 /* awake-idle, sleep/drain, MCU sleep task */
      for(unsigned q=0;q<3;q++){ st_bp_reset(); boot_push_start(1); st_status(idle_ws[q],T); boot_push_step(T); if(!(bp.state==BP_A&&wq.n==1&&wq.q[0].sub==1)) ok=0; }
      st_check("the guard does NOT block an idle machine: work_sta 0101 (awake), 0001 (sleep/drain) and 0000 -> the push is sent",ok); }
    st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T); wq_step(T); st_ack_last(T+25); boot_push_step(T+26);
    { int d=bp.state==BP_WAIT_B; ms_t t=T+30; for(int k=0;k<300;k++){ t+=100; st_status(0x1307,t); wq_step(t); boot_push_step(t); if(bp.state!=BP_WAIT_B||wq.n) d=0; }
      st_status(0x0101,t+100); boot_push_step(t+100); wq_step(t+100);
      st_check("activity started AFTER the decision (between the two blocks): sub112 len3 is DEFERRED 30 s and sent when the machine is idle",d&&bp.state==BP_B&&hexeq(last_tx,last_tx_len,BPV_B)); }
    /* --- config out of spec / missing / read-only / disabled --- */
    st_fixture(fx,1200,1); settings_load(); st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T);
    int blk=bp.ra==BPR_BLOCKED&&bp.state==BP_WAIT_B&&wq.n==0&&wq.n_block==1&&wq_rej==WJ_RANGE&&txq_len==0;
    boot_push_step(T); wq_step(T); blk&=hexeq(last_tx,last_tx_len,BPV_B); st_ack_last(T+25); boot_push_step(T+26);
    st_check("fixture w/ brewTemper=1200: sub1 len30 REJECTED by the frame policy (BLOK +1, 0 bytes), sub112 len3 sent and ACKed",blk&&bp.state==BP_DONE&&bp.rb==BPR_OK);
    { uint32_t c0; st_check("DIAGNOSE: BOOT-PUSH 1-30 = AFVIST / REJECTED (amber)",bp_text(0,&c0)==TR(TX_DV_BP_BLOCKED)&&c0==C_WARN); }
    SET[S_BREWT].val=930; boot_push_resave();                                             /* the user has corrected the value with a SAVE */
    { int rs=bp.state==BP_WAIT_A&&!bp.guard; boot_push_step(T+100); wq_step(T+100); rs&=hexeq(last_tx,last_tx_len,BPV_A); st_ack_last(T+125); boot_push_step(T+126);
      boot_push_step(T+126); wq_step(T+126); st_ack_last(T+150); boot_push_step(T+151);
      st_check("after a SAVE that brings config into spec: the push is repeated — sub1 len30 (now = the vector) + sub112 len3, both ACKed",rs&&bp.state==BP_DONE&&bp.ra==BPR_OK&&bp.rb==BPR_OK&&bp.runs==2&&wq.n_block==1);
      boot_push_resave(); st_check("a completed push (OK / OK) is NOT repeated by a later SAVE",bp.state==BP_DONE&&bp.runs==2); }
    st_fixture(fx,0,0); settings_load(); st_bp_reset(); boot_push_start(1); st_status(0x0101,T); boot_push_step(T);
    st_check("fixture without brewTemper: NO CONFIG — nothing is sent (no guessed value to the controller)",bp.state==BP_DONE&&bp.ra==BPR_NOCFG&&bp.rb==BPR_NOCFG&&wq.n==0&&txq_len==0);
    st_fixture(fx,930,1); settings_load(); st_bp_reset(); ro_mode=1; boot_push_start(1); st_status(0x0101,T); boot_push_step(T); int ro=wq.n==0&&txq_len==0&&bp.ra==BPR_NONE; ro_mode=0;
    st_bp_reset(); bp_on=0; boot_push_resave(); st_status(0x0101,T); boot_push_step(T); int off=bp.state==BP_OFF&&wq.n==0&&txq_len==0; uint32_t c0; off&=(bp_text(0,&c0)==TR(TX_DV_BP_OFF));
    st_check("MERAKIPLUS_READONLY: nothing is sent; MERAKIPLUS_NOBOOTPUSH (not started): nothing is sent, DIAGNOSE shows FRA / OFF",ro&&off);
    /* --- clean up: restore the real config --- */
    bp_on=bpon0; ro_mode=ro0; snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load(); unlink(fx);
    wq=wq0; memset(&bp,0,sizeof bp); memcpy(reg,reg0,sizeof reg); memcpy(reg_seen,seen0,sizeof reg_seen); rx_zero(); last_rx_ms=rxl0; rx_ok=ok0; rx_bad=bad0; txq_len=tx0; last_tx_len=0;
    st_check("the real config is loaded again after the fixture tests (0104 / 0605 / 70)",cfg_ok&&pack112(sv(S_PISW),sv(S_PIPRESS))==0x0104&&pack113(sv(S_PITIME),sv(S_BREAK))==0x0605&&sv(S_HEADT)==70);
}
static void st_rx_resync(void){
    printf("== selftest: v6.5 RX resync watchdog (RX_RESYNC_MS = %d; main() calls the parser every 2 ms) ==\n",RX_RESYNC_MS);
    __typeof__(wq) wq0=wq; uint16_t reg0[REGN]; int seen0[REGN]; memcpy(reg0,reg,sizeof reg); memcpy(seen0,reg_seen,sizeof reg_seen);
    ms_t rxl0=last_rx_ms, ok0=rx_ok, bad0=rx_bad, lb0=last_bad_ms; int tx0=txq_len;
    static uint8_t V[8][RXV_N], C[RXV_N], blob[8192]; ms_t T=40000000L, t;
    for(int k=0;k<8;k++) rx_v(V[k],k+1);
    /* --- baseline: valid frames, however they are split up --- */
    { int ok=1; static const int chunk[]={1,7,37,119,512};
      for(unsigned q=0;q<5;q++){ rx_zero(); int n=0; for(int k=0;k<5;k++){ memcpy(blob+n,V[k],RXV_N); n+=RXV_N; }
          t=T; for(int o=0;o<n;o+=chunk[q]){ int m=n-o<chunk[q]?n-o:chunk[q]; rx_tick(blob+o,m,t++); }
          if(!(rx_ok==5&&reg[55]==5&&rx_bad==0&&rx_resync==0&&rxlen==0)) ok=0; }
      st_check("baseline: 5 valid frames in chunks of 1 / 7 / 37 / 119 / 512 bytes -> 5 received, 0 CRC errors, 0 resync, empty buffer",ok); }
    /* --- corrupt length byte: impossible length (the v6.4 finding) --- */
    memcpy(C,V[7],RXV_N); C[4]=0xC8;                                                     /* LEN 56 -> 200 words: does not exist */
    rx_zero(); { int n=0; memcpy(blob,C,RXV_N); n=RXV_N; for(int k=0;k<3;k++){ memcpy(blob+n,V[k],RXV_N); n+=RXV_N; } rx_feed(blob,n,T); }
    st_check("corrupt length byte (LEN = 200 words): the candidate is discarded IMMEDIATELY — the 3 frames behind it are received in the same call (0 ms), RESYNC +1",rx_ok==3&&reg[55]==3&&rx_resync==1&&rxlen==0);
    { rx_zero(); rx_tick(C,RXV_N,T); ms_t gap=0,last=T; int k=0; for(t=T+2;t<=T+3000;t+=2){ ms_t o=rx_ok; if((t-T)%50==0){ rx_v(blob,++k); rx_tick(blob,RXV_N,t); } else rx_tick(NULL,0,t); if(rx_ok!=o){ if(t-last>gap) gap=t-last; last=t; } }
      st_check("the v6.4 scenario: corrupt LEN followed by 3 s of poll traffic -> all 60 replies received, largest gap 50 ms, link up the whole way (v6.4: approx. 3.5 s without RX)",rx_ok==60&&reg[55]==60&&gap<=50&&rx_resync==1&&link_up(T+3000)); }
    /* --- corrupt length byte: possible, but too long (56 -> 120 words = 247 bytes) --- */
    memcpy(C,V[7],RXV_N); C[4]=0x78;
    { rx_zero(); ms_t first=-1,all=-1; for(t=T;t<=T+300;t+=2){ const uint8_t*d=NULL; if(t==T) d=C; else if(t==T+50) d=V[0]; else if(t==T+100) d=V[1]; else if(t==T+150) d=V[2];
          rx_tick(d,d?RXV_N:0,t); if(first<0&&rx_ok>=1) first=t; if(all<0&&rx_ok>=3) all=t; }
      st_check("corrupt LEN (120 words = 247 bytes), traffic continues: the CRC rejects the candidate as soon as 247 bytes have arrived — frames behind it received at +100 ms, all 3 at +150 ms, 0 lost",
               first==T+100&&all==T+150&&reg[55]==3&&rx_bad==1&&rx_resync==0&&rxlen==0); }
    { rx_zero(); ms_t got=-1; for(t=T;t<=T+1000;t+=2){ const uint8_t*d=NULL; if(t==T) d=C; else if(t==T+50) d=V[0]; rx_tick(d,d?RXV_N:0,t); if(got<0&&rx_ok>=1) got=t; }
      st_check("corrupt LEN (247 bytes), then silence: the WATCHDOG discards the candidate after exactly RX_RESYNC_MS — the frame behind it is then received (+400 ms), RESYNC +1, empty buffer",
               got==T+RX_RESYNC_MS&&reg[55]==1&&rx_resync==1&&rxlen==0); }
    /* --- truncated frame --- */
    { rx_zero(); int held=0,gone=0; for(t=T;t<=T+998;t+=2){ rx_tick(t==T?V[7]:NULL,t==T?60:0,t); if(t==T+RX_RESYNC_MS-2) held=rxlen==60; if(t==T+RX_RESYNC_MS) gone=rxlen==0&&rx_resync==1; }
      rx_tick(V[0],RXV_N,T+1000); int a=rx_ok==1; rx_tick(V[1],RXV_N,T+1050);
      st_check("truncated frame (60 of 119 bytes), 1 s of silence: the fragment is held for 398 ms, discarded at 400 ms; the next frames are received as soon as they arrive",held&&gone&&a&&rx_ok==2&&reg[55]==2&&rx_resync==1&&rx_bad==0); }
    { rx_zero(); memset(reg,0,sizeof reg); uint8_t sf[32]; uint16_t w7[7]={0x0101,0,0,0,0,0,0}; int sn=mk_frame(sf,0x83,33,w7,7);
      rx_feed(V[7],60,T); rx_feed(sf,sn,T+1000);                                         /* 60 + 21 bytes: the fragment's 119 bytes are never filled up */
      st_check("truncated frame, and the parser is not called again until a short reply (21 bytes) arrives 1 s later: the watchdog is decided ON ARRIVAL — reply received in the same call (0 ms), RESYNC +1",
               rx_ok==1&&reg[33]==0x0101&&rx_resync==1&&rx_bad==0&&rxlen==0);
      rx_zero(); rx_feed(V[7],60,T); rx_feed(V[0],RXV_N,T+1000);                           /* 60 + 119 bytes: the fragment is filled up, and the CRC rejects it */
      st_check("truncated frame followed by a long reply 1 s later: the CRC rejects the fragment — reply received in the same call (0 ms), 1 CRC error",rx_ok==1&&reg[55]==1&&rx_bad==1&&rxlen==0); }
    rx_zero(); rx_tick(V[7],60,T); rx_tick(V[0],RXV_N,T+10); rx_tick(V[1],RXV_N,T+60);
    st_check("truncated frame followed by valid frames 10 ms later: the CRC rejects the fragment — both frames received immediately, 1 CRC error, 0 resync",rx_ok==2&&reg[55]==2&&rx_bad==1&&rx_resync==0&&rxlen==0);
    { rx_zero(); static const uint8_t st4[4]={0xAA,0x55,0x83,0x00}; int gone=0; for(t=T;t<=T+600;t+=2){ rx_tick(t==T?st4:NULL,t==T?4:0,t); if(t==T+RX_RESYNC_MS) gone=rxlen==0; } rx_tick(V[0],RXV_N,T+602);
      st_check("fragment under 7 bytes (AA 55 83 00): discarded at 400 ms; next frame received",gone&&rx_ok==1&&rx_resync==1); }
    /* --- noise burst: random bytes + the worst prefixes, in chunks of 1..24 bytes per 10 ms (like the sim's garbage) --- */
    { static const uint8_t nasty[9][5]={{0xAA},{0xAA,0x55},{0xAA,0x55,0x03},{0xAA,0x55,0x08},{0xAA,0x55,0x88,0x12,0x34},{0xAA,0x55,0x83,0x00,0x38},{0xAA,0x55,0x09,0xFF},{0xAA,0x55,0x86,0x0B,0x81},{0xAA,0x55,0x83,0x00,0xC8}};
      static const int nlen[9]={1,2,3,4,5,5,4,5,5};
      rx_zero(); uint32_t x=0x6502u; int maxbuf=0, sent=0, nn=0; t=T;
      while(sent<4096){ int m=1+(int)((x=x*1103515245u+12345u)>>16)%24; for(int k=0;k<m;k++) blob[k]=(uint8_t)((x=x*1103515245u+12345u)>>16);
          if(sent/200!=(sent+m)/200){ memcpy(blob+m,nasty[nn%9],nlen[nn%9]); m+=nlen[nn%9]; nn++; }
          for(int q=0;q<5;q++){ rx_tick(q==0?blob:NULL,q==0?m:0,t); t+=2; } sent+=m; if(rxlen>maxbuf) maxbuf=rxlen; }
      int quiet=rx_ok==0; ms_t tv=t, first=-1;
      for(int k=0;k<5;k++){ for(int q=0;q<25;q++){ rx_tick(q==0?V[k]:NULL,q==0?RXV_N:0,t); if(first<0&&rx_ok>=1) first=t; t+=2; } }
      char nm[260]; snprintf(nm,sizeof nm,"noise burst: %d random bytes with %d inserted worst-case prefixes (AA 55 03 / 08 / 88 / long LEN …): 0 false frames, buffer at most %d bytes; the 5 frames right after are ALL received, the first %ld ms after arrival",sent,nn,maxbuf,(long)(first-tv));
      st_check(nm,quiet&&rx_ok==5&&reg[55]==5&&first>=0&&first-tv<=RX_RESYNC_MS&&maxbuf<=265+29+RXV_N);
      /* each prefix on its own, right before valid frames: (a) traffic continues, (b) one frame and then silence */
      int ok=1; ms_t worst=0;
      for(int q=0;q<9;q++) for(int mode=0;mode<2;mode++){
          rx_zero(); ms_t got=-1;
          for(t=T;t<=T+700;t+=2){ const uint8_t*d=NULL; int m=0;
              if(t==T){ d=nasty[q]; m=nlen[q]; } else if(t==T+10){ d=V[0]; m=RXV_N; } else if(!mode&&t==T+60){ d=V[1]; m=RXV_N; } else if(!mode&&t==T+110){ d=V[2]; m=RXV_N; }
              rx_tick(d,m,t); if(got<0&&rx_ok>=1) got=t; }
          int want=mode?1:3; if(got<0||got-T>RX_RESYNC_MS||rx_ok!=want||reg[55]!=want||rxlen!=0){ ok=0; printf("     prefix %d mode %d: rx_ok=%u got=%ld rxlen=%d\n",q,mode,rx_ok,(long)(got-T),rxlen); }
          if(got-T>worst) worst=got-T; }
      snprintf(nm,sizeof nm,"each of the 9 worst prefixes right before valid frames (traffic continues / ONE frame and then silence): no frame lost, first frame received at most %ld ms after the noise (limit %d)",(long)worst,RX_RESYNC_MS);
      st_check(nm,ok&&worst<=RX_RESYNC_MS); }
    /* --- valid frames interleaved with errors: each valid frame exactly once, in order --- */
    { rx_zero(); memset(&wq,0,sizeof wq); txq_len=0; int ok=1, nbad=0, prev=0; uint32_t x=77u; t=T; ms_t tack=-1;
      for(int k=1;k<=40;k++){ int n=0; uint8_t f[RXV_N]; rx_v(f,k);
          if(k%3==0){ for(int j=0;j<9;j++) blob[n++]=(uint8_t)((x=x*1103515245u+12345u)>>16); }
          if(k%5==0){ memcpy(blob+n,f,RXV_N); blob[n+40]^=0x10; n+=RXV_N; nbad++; }               /* bit error in payload -> CRC error */
          if(k%7==0){ memcpy(blob+n,f,RXV_N); blob[n+4]=0xC8; n+=RXV_N; }                         /* impossible length */
          if(k%11==0){ memcpy(blob+n,f,RXV_N); blob[n+4]=0x78; n+=RXV_N; nbad++; }                /* too long a length -> CRC error once 247 bytes have arrived */
          if(k%13==0){ memcpy(blob+n,f,45); n+=45; nbad++; }                                      /* truncated -> CRC error when the next frame fills it up */
          if(k%17==0){ blob[n++]=0xAA; blob[n++]=0x55; blob[n++]=0x03; nbad++; }                  /* cmd 3 header + 4 bytes of the next frame -> CRC error (v6.4 swallowed them without CRC) */
          memcpy(blob+n,f,RXV_N); n+=RXV_N;
          if(k==20){ wq_push(11,(uint16_t[1]){880},1); wq_step(t); }                              /* SAVE write is sent … */
          if(k==21){ uint8_t a[16]; uint16_t w=880; int an=mk_frame(a,0x86,11,&w,1); memcpy(blob+n,a,an); n+=an; }   /* … and the ACK arrives mid-stream */
          for(int o=0,q=0;q<25;q++){ int m=n-o<37?n-o:37; rx_tick(m>0?blob+o:NULL,m>0?m:0,t); o+=m>0?m:0; wq_step(t); if(tack<0&&wq.result==WR_OK) tack=t;
              if(reg[55]<prev||reg[55]>k) ok=0; prev=reg[55]; t+=2; } }
      for(int q=0;q<300;q++){ rx_tick(NULL,0,t); t+=2; }
      char nm[260]; snprintf(nm,sizeof nm,"interleaved: 40 valid replies + 1 ACK mixed with noise, CRC errors, impossible/too long LEN, truncated frames and AA 55 03: all 41 received ONCE in order, %d CRC errors counted, the ACK released the write queue",nbad);
      if(!(ok&&rx_ok==41&&rx_bad==nbad)) printf("     ok=%d rx_ok=%u reg55=%u rx_bad=%u (expected %d) resync=%u rxlen=%d tack=%ld\n",ok,rx_ok,reg[55],rx_bad,nbad,rx_resync,rxlen,(long)(tack-T));
      st_check(nm,ok&&rx_ok==41&&reg[55]==40&&rx_bad==nbad&&rxlen==0&&tack>=0&&wq.result==WR_OK&&wq.n_fail==0&&wq.n_retx==0); memset(&wq,0,sizeof wq); txq_len=0; }
    /* --- slow but valid traffic must not be discarded --- */
    { rx_zero(); static const int gaps[]={100,300,456,1000,1400}; t=T; int k=0; rx_tick(V[k++],RXV_N,t);
      for(unsigned q=0;q<5;q++){ ms_t e=t+gaps[q]; for(t+=2;t<e;t+=2) rx_tick(NULL,0,t); rx_tick(V[k++],RXV_N,t); }
      st_check("pauses BETWEEN frames of 100 / 300 / 456 / 1000 / 1400 ms (poll cadence and longest measured poll gap): all 6 received, 0 resync",rx_ok==6&&reg[55]==6&&rx_resync==0&&rx_bad==0); }
    { int ok=1; static const int pause[]={20,300,398}; for(unsigned q=0;q<3;q++){ rx_zero(); for(t=T;t<=T+pause[q];t+=2){ const uint8_t*d=NULL; int m=0; if(t==T){ d=V[0]; m=60; } else if(t==T+pause[q]){ d=V[0]+60; m=RXV_N-60; } rx_tick(d,m,t); }
          if(!(rx_ok==1&&rx_resync==0&&rx_bad==0&&rxlen==0)) ok=0; }
      st_check("pauses IN THE MIDDLE of a frame of 20 / 300 / 398 ms (longest measured MCU silence: 279 ms): the frame is received, 0 resync",ok); }
    { rx_zero(); for(t=T;t<=T+450;t+=2){ const uint8_t*d=NULL; int m=0; if(t==T){ d=V[0]; m=60; } else if(t==T+450){ d=V[0]+60; m=RXV_N-60; } rx_tick(d,m,t); }
      int lost=rx_ok==0&&rx_resync==1; rx_tick(V[1],RXV_N,T+500);
      st_check("pause in the middle of a frame of 450 ms (beyond the window): THAT frame is dropped (RESYNC +1) — the next one is received",lost&&rx_ok==1&&reg[55]==2&&rxlen==0); }
    { rx_zero(); txq_len=0; static const uint8_t vr[8]={0xAA,0x55,0x89,0x01,0x00,0x00,0x6F,0x81}; rx_tick(vr,3,T); rx_tick(vr+3,5,T+20); int one=txq_len==15&&rx_ok==1; rx_tick(vr,8,T+40);
      static const uint8_t q3[7]={0xAA,0x55,0x03,0x00,0x38,0xDC,0x06}; rx_tick(q3,7,T+60);
      st_check("version request 0x89 (split in two) is answered ONCE per request; a valid cmd 3 frame (AA 55 03 00 38 DC 06) is skipped without error",one&&txq_len==30&&rx_ok==2&&rx_bad==0&&rx_resync==0&&rxlen==0); txq_len=0; }
    /* --- a stuck candidate in front of an ACK must not cost a SAVE (window < ACK budget 450 ms) --- */
    { rx_zero(); memset(&wq,0,sizeof wq); txq_len=0; wq_push(11,(uint16_t[1]){890},1); ms_t tok=-1; uint8_t a[16]; uint16_t w=890; int an=mk_frame(a,0x86,11,&w,1);
      static uint8_t stuck[25]={0xAA,0x55,0x83,0x00,0x81};                                 /* promises 265 bytes, delivers 25 */
      for(t=T;t<=T+460;t+=2){ const uint8_t*d=NULL; int m=0; if(t==T+4){ d=stuck; m=25; } else if(t==T+24){ d=a; m=an; } rx_tick(d,m,t); wq_step(t); if(tok<0&&wq.result==WR_OK) tok=t; }
      st_check("ACK caught behind a stuck candidate, no other traffic: released by the watchdog 400 ms after the candidate (+404 ms) — before the write queue gives up at 450 ms; SAVE succeeds",
               tok==T+4+RX_RESYNC_MS&&wq.n_fail==0&&wq.n_retx==1&&reg[11]==890&&rx_resync==1); memset(&wq,0,sizeof wq); txq_len=0; }
    st_check("the window lies between the longest measured MCU silence (279 ms) and the write queue's ACK budget (150 + 300 ms), and below the OFFLINE limit (1500 ms)",RX_RESYNC_MS>279&&RX_RESYNC_MS<WQ_ACK1_MS+WQ_ACK2_MS&&RX_RESYNC_MS<1500);
    wq=wq0; memcpy(reg,reg0,sizeof reg); memcpy(reg_seen,seen0,sizeof reg_seen); rx_zero(); last_rx_ms=rxl0; rx_ok=ok0; rx_bad=bad0; last_bad_ms=lb0; txq_len=tx0; last_tx_len=0;
}
/* ====================================================================== */
/* ============= v6.6: phase semantics · 64-bit clock · re-push ========= */
/* ====================================================================== */
/* the controller's replies as they arrive on the line: 0x83 sub0 len56 (words 0..55) and 0x83 sub33 len7 (words 33..39) respectively */
static void st_s0(uint16_t ws,uint16_t w24,uint16_t w44,uint16_t w46,uint16_t w48,ms_t t){
    uint8_t f[128]; uint16_t w[56]; memcpy(w,reg,sizeof w); w[33]=ws; w[24]=w24; w[44]=w44; w[46]=w46; w[48]=w48; rx_feed(f,mk_frame(f,0x83,0,w,56),t);
}
static void st_s33(uint16_t ws,ms_t t){ uint8_t f[32]; uint16_t w[7]; memcpy(w,reg+33,sizeof w); w[0]=ws; rx_feed(f,mk_frame(f,0x83,33,w,7),t); }
static int st_has(const char*hay,const char*needle){ return strstr(hay,needle)!=NULL; }
/* idle machine with a fresh link at time t (HOME, no toast, empty write queue) */
static ms_t st_home(void){
    sim_reset(); sim_run(400); memset(&wq,0,sizeof wq); txq_len=0; toast.dur=0;
    return sim_now;
}
static void st_phases(void){
    printf("== selftest: v6.6 phase semantics per STEAMLIVE.md (two steam paths via word 24 + work_sta + words 44/46; concurrent phases) ==\n");
    char a[32],b[96]; ms_t t;
    /* ---- (1) LATCHED auto steam: STEAMLIVE §4.1 + §5.1, row by row ---- */
    t=st_home(); reg[25]=600;                                   /* the MCU's target (word 25) = 60.0° — deliberately different from config (65.0°) */
    st_s0(0x0101,1,0x000C,0,123,t+=100); ui_frame(t);            /* the start write is ACKed: word 24 = 1, bits 5+14 not yet set */
    int pre=stm.kind==SK_NONE&&act.ph!=PH_STEAM;
    st_s33(0x4121,t+=220); ui_frame(t);
    fmt_t10(a,sizeof a,600,1); snprintf(b,sizeof b,TR(TX_H_AUTOSTOP_T),a);
    st_check("LATCHED (word 24 = 1, work_sta 4121, wand switch 0): chip = STEAM·AUTO / DAMP·AUTO, phase STEAM",
             pre&&stm.kind==SK_AUTO&&!stm.wand&&act.ph==PH_STEAM&&phase_raw(t)==PH_STEAM&&hm.chip==TR(TX_PH_STEAM_AUTO)&&!hm.conc&&!hm.badge);
    st_check("LATCHED: the target is WORD 25 (60.0°, not the config's 65.0°) — the auto-stop expectation ('STOPS AT 60.0°', said once: the label is just 'MILK') and the gauge marker",
             !strcmp(hm.label,TR(TX_H_MILK))&&!st_has(hm.label,a)&&st_has(hm.sub,a)&&!strcmp(hm.sub,b)&&fabsf(hm.gmark-(600-200)/600.f)<1e-4f&&steam_target()==600&&sv(S_STEAMT)==650);
    st_check("LATCHED: the STOP button is shown and hittable (124,342,232x60; v6.6-v6.16: 150,344,180x56); no start buttons",steam_stop_visible()&&hit_test(240,372,t)==T_STOP&&hit_test(150,372,t)==T_STOP&&hit_test(330,372,t)==T_STOP&&hit_test(120,372,t)==T_NONE&&hit_test(360,372,t)==T_NONE&&hit_test(240,340,t)==T_NONE);
    { int ok=1; static const uint16_t pulse[]={0x6121,0x4521,0x4121,0x6121};      /* bit 10/13 pulses on top of steam (STEAMLIVE §3) */
      for(unsigned q=0;q<4;q++){ st_s0(pulse[q],1,0x000C,0,(uint16_t)(288+q*40),t+=100); ui_frame(t); if(stm.kind!=SK_AUTO||hm.chip!=TR(TX_PH_STEAM_AUTO)) ok=0; }
      st_check("LATCHED: maintenance pulses (6121 / 4521) change nothing — still STEAM·AUTO",ok); }
    /* manual STOP: the ACK echo sets word 24 = 0 BEFORE the MCU clears bits 5+14 */
    { __typeof__(stm) k=stm; uint16_t r24=reg[24]; int ok=1;
      for(int q=0;q<3;q++){ st_s0(0x4121,0,0x000C,0,430,t+=50); ui_frame(t); if(stm.kind!=SK_AUTO||hm.chip!=TR(TX_PH_STEAM_AUTO)||!steam_stop_visible()) ok=0; }
      st_check("LATCHED is sticky: word 24 = 0 (ACK of a STOP write) while bits 5+14 are still set -> still STEAM·AUTO, never a WAND flash",ok);
      stm=k; reg[24]=r24; }
    /* auto-stop, exactly as in the log: 96.363 / 96.437 / 96.676 / 96.838 */
    st_s0(0x6121,1,0x000C,0,646,t+=100); ui_frame(t); st_s33(0x6121,t+=74); ui_frame(t);
    int on=stm.kind==SK_AUTO&&act.ph==PH_STEAM;
    st_s33(0x2101,t+=239); ui_frame(t);                                            /* bits 5+14 cleared, word 24 still 1 */
    int stop1=stm.kind==SK_NONE&&stm.last_kind==SK_AUTO&&act.ph==PH_DONE&&act.last_ph==PH_STEAM&&!steam_stop_visible()&&hit_test(240,372,t)!=T_STOP;
    st_s0(0x0501,0,0x000C,0,652,t+=162); ui_frame(t);                              /* the MCU resets word 24 itself; milk 65.2° */
    st_check("AUTO-STOP (STEAMLIVE §5.1: 6121 -> 2101 with word 24 = 1 -> 0501 with word 24 = 0): DONE, STOP button gone, and merakiplus has sent NOTHING (no stop write)",
             on&&stop1&&act.ph==PH_DONE&&wq.n==0&&txq_len==0&&wq.n_ok==0);
    { char d[8]; fmt_fix(d,sizeof d,use_c()?MILK_RISE10:MILK_RISE10*9/5,-1); char need[16]; snprintf(need,sizeof need,"\xE2\x89\x88%s\xC2\xB0",d);
      int note=st_has(hm.sub,need)&&st_has(hm.label,a);
      for(int q=0;q<12;q++){ st_s0(0x0101,0,0x000C,0,652,t+=100); ui_frame(t); }  /* v6.9: the hero is conditioned (median-3 + EMA) — let it settle */
      fmt_t10(a,sizeof a,652,1); int h1=!strcmp(hm.hero,a);
      st_s0(0x0101,0,0x000C,0,689,t+=4000); ui_frame(t); fmt_t10(a,sizeof a,689,1);
      st_check("after auto-stop: the subline states the post-stop rise (approx. +4 °C), and the hero keeps following the probe (65.2 -> 68.9) — information only",note&&h1&&!strcmp(hm.hero,a)&&act.ph==PH_DONE&&!strcmp(d,"4"));
      for(int q=0;q<50;q++){ st_s33(0x0101,t+=100); ui_frame(t); }
      st_check("8 s after stop: back to READY; the steam tracking is reset",act.ph==PH_READY&&stm.kind==SK_NONE); }
    /* ---- (2) MOMENTARY wand/flush alone ---- */
    t=st_home();
    st_s0(0x0101,0,0x000C,0,514,t+=100); ui_frame(t);
    st_s33(0x4121,t+=76); ui_frame(t);                                             /* bits set; words 44/46 only follow ~0.4 s later */
    int w0=stm.kind==SK_WAND&&!stm.wand&&hm.chip==TR(TX_PH_STEAM_WAND);
    st_s0(0x4121,0,0x000D,1,523,t+=391); ui_frame(t);
    st_check("WAND (work_sta 4121 WITHOUT word 24 = 1): chip = STEAM·WAND / DAMP·WAND immediately; word 44 bit0 + word 46 = 1 confirm 0.4 s later",
             w0&&stm.kind==SK_WAND&&stm.wand&&wand_sw()&&act.ph==PH_STEAM&&phase_raw(t)==PH_STEAM&&hm.chip==TR(TX_PH_STEAM_WAND));
    st_check("WAND: no auto-stop expectation — label 'MILK PROBE · NO TARGET', subline 'NO AUTO-STOP · RELEASE WAND', no target marker on the gauge",
             !strcmp(hm.label,TR(TX_H_MILK_PROBE))&&!strcmp(hm.sub,TR(TX_H_WAND_NOSTOP))&&hm.gmark<0&&!st_has(hm.sub,"AUTO-STOP A")&&!st_has(hm.sub,"AUTO-STOP V"));
    st_check("WAND: NO STOP button (a STOP write would do nothing) and no start buttons; start is blocked (ACTIVITY IN PROGRESS)",
             !steam_stop_visible()&&hit_test(240,372,t)==T_NONE&&hit_test(210,372,t)==T_NONE&&act_guard(ACT_BREW,t)==TR(TX_G_BUSY)&&act_guard(ACT_STEAM,t)==TR(TX_G_BUSY));
    { char v[DG_N][28]; uint32_t col[DG_N]; diag_top_text(v,col,t); st_check("DIAGNOSE: the PHASE cell shows the steam path (STEAM·WAND)",!strcmp(v[1],TR(TX_PH_STEAM_WAND))); }
    /* ---- (3) wand over the target: no auto-stop ---- */
    { int ok=1, never_done=1, sent=0; static const uint16_t milk[]={560,610,650,669,683,695,695,695,691,688};   /* past 650 and beyond, as in STEAMLIVE §6.2 */
      for(unsigned q=0;q<10;q++){ for(int k=0;k<3;k++){ st_s0(0x4121,0,0x000D,1,milk[q],t+=100); ui_touch(TE_DOWN,240,372,t); ui_frame(t); ui_touch(TE_UP,240,372,t); ui_frame(t+=33); wq_step(t);
              if(stm.kind!=SK_WAND||hm.chip!=TR(TX_PH_STEAM_WAND)||strcmp(hm.sub,TR(TX_H_WAND_NOSTOP))||hm.gmark>=0||steam_stop_visible()) ok=0;
              if(act.ph!=PH_STEAM) never_done=0; if(wq.n||txq_len||toast.dur) sent=1; } }
      st_check("WAND OVER TARGET: probe 56.0 -> 69.5° (target 65.0°) in 4 s — STEAM·WAND the whole way, never DONE, never an auto-stop text or a target",ok&&never_done&&reg[48]>steam_target());
      st_check("WAND OVER TARGET: a tap where the STOP button would be does nothing — merakiplus sends no frame (no stop write, no toast)",!sent&&wq.n_ok==0&&wq.n_block==0);
      st_s33(0x0101,t+=100); ui_frame(t);
      st_check("wand released (bits 5+14 cleared): DONE with a plain 'DONE IN …' — no post-stop rise note (that belongs to auto-stop)",
               act.ph==PH_DONE&&stm.kind==SK_NONE&&stm.last_kind==SK_WAND&&!st_has(hm.sub,"\xE2\x89\x88")&&!strcmp(hm.label,TR(TX_H_MILK_PROBE))); }
    /* ---- (4) CONCURRENT phases: flush during brew, STEAMLIVE §6.2 row by row ---- */
    t=st_home(); reg[39]=0; reg[51]=360; reg[52]=360;
    st_s0(0x1107,0,0x000C,0,514,t+=100); ui_frame(t);                              /* brew: pre-infusion */
    ms_t tb0=act.t_act0; int pre_ok=act.ph==PH_PRE&&tb0==t;
    reg[39]=216; reg[51]=1301; reg[52]=191;
    st_s0(0x1307,0,0x000C,0,514,t+=20500); ui_frame(t);                            /* 239.726: extraction, wand OFF */
    int e0=act.ph==PH_EXT&&!hm.conc&&!hm.badge&&hm.sl[2]==TR(TX_ST_FLOW)&&hm.chip==ph_name(PH_EXT);
    st_s33(0x5327,t+=76); ui_frame(t);                                             /* 239.802: steam bits added on top of the brew */
    st_check("CONCURRENT (work_sta 5327 = brew 1+2+9+12 + steam 5+14, word 24 = 0): the contract's phase_raw() is STEAM (unchanged); HOME's main phase is EXTRACTION",
             pre_ok&&e0&&phase_raw(t)==PH_STEAM&&phase_main(t)==PH_EXT&&act.ph==PH_EXT&&act.raw==PH_EXT);
    st_check("CONCURRENT: BOTH named — chip = EXTRACTION, steam badge = STEAM·WAND (words 44/46 still 0: the wand path follows from word 24 = 0)",
             hm.conc&&hm.chip==ph_name(PH_EXT)&&hm.badge==TR(TX_PH_STEAM_WAND)&&stm.kind==SK_WAND&&!stm.wand);
    reg[39]=223; st_s0(0x5327,0,0x000D,1,523,t+=391); ui_frame(t);                 /* 240.193: wand ON */
    for(int q=0;q<14;q++){ st_s0(0x5327,0,0x000D,1,523,t+=100); ui_frame(t); }     /* v6.9: the hero is conditioned — let it settle at 22.3 (1.4 s is taken from the next gap) */
    { char h[16],m1[16]; fmt_fix(h,sizeof h,223,1); fmt_t10(a,sizeof a,523,1); snprintf(m1,sizeof m1,"%s\xC2\xB0",a); fmt_fix(a,sizeof a,sv(S_BREWW),1); snprintf(b,sizeof b,TR(TX_H_AUTOSTOP_G),a);
      st_check("CONCURRENT: the brew's gauge/weight stays (hero 22.3 G, progress ring, AUTO-STOP AT 36.0 G), and the milk temperature is in the stat row (TIME · ≈BAR* · MILK 52.3°)",
               stm.wand&&!strcmp(hm.hero,h)&&hm.unit==TR(TX_U_G)&&hm.pvis&&!strcmp(hm.sub,b)&&hm.sl[0]==TR(TX_ST_TIME)&&hm.sl[1]==TR(TX_ST_BAR)&&hm.sl[2]==TR(TX_ST_MILK)&&!strcmp(hm.svv[2],m1)); }
    reg[39]=262; st_s0(0x5327,0,0x000D,1,669,t+=1473-1400); ui_frame(t); reg[39]=272; st_s0(0x5327,0,0x000D,1,695,t+=480); ui_frame(t);   /* 241.666 / 242.146: probe above 650 */
    { fmt_t10(a,sizeof a,695,1); char m1[16]; snprintf(m1,sizeof m1,"%s\xC2\xB0",a);
      int sk[2], sn=stop_btns(t,sk);   /* v6.13: the brew has its own STOP button — the wand still has none */
      st_check("CONCURRENT + wand over the target (69.5° > 65.0°): still EXTRACTION + STEAM·WAND; no STEAM STOP (only v6.13's STOP BREW), no frame sent; the brew timer is not reset",
               hm.conc&&hm.badge==TR(TX_PH_STEAM_WAND)&&!strcmp(hm.svv[2],m1)&&act.ph==PH_EXT&&!steam_stop_visible()&&sn==1&&sk[0]==ACT_BREW&&hit_test(240,372,t)==T_STOP&&wq.n==0&&txq_len==0&&act.t_act0==tb0); }
    st_s33(0x1307,t+=768); ui_frame(t);                                            /* 242.914: wand released, the brew continues */
    st_check("wand released during the brew: badge gone, EXTRACTION continues without a DONE view; 3rd stat field is FLOW again; same brew timer",
             !hm.conc&&!hm.badge&&act.ph==PH_EXT&&stm.kind==SK_NONE&&hm.sl[2]==TR(TX_ST_FLOW)&&act.t_act0==tb0);
    reg[39]=352; st_s33(0x2101,t+=2936); ui_frame(t); st_s0(0x0101,0,0x000C,0,600,t+=180); ui_frame(t);
    { char tbx[24]; snprintf(tbx,sizeof tbx,"%ld.%ld",(long)(act.done_ms/1000),(long)((act.done_ms%1000)/100));
      st_check("the brew stops: DONE for the BREW (weight in hero, 'DONE IN' = the whole brew time from pre-infusion — not the steam time)",
               act.ph==PH_DONE&&act.last_ph==PH_EXT&&hm.unit==TR(TX_U_G)&&st_has(hm.sub,tbx)&&act.done_ms>26000&&act.done_ms<27500); }
    /* ---- (5) variants ---- */
    t=st_home(); reg[39]=120;
    st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); st_s0(0x5327,1,0x000C,0,310,t+=100); ui_frame(t);
    { int sk[2], sn=stop_btns(t,sk);   /* v6.13: two buttons — left STOP BREW, right STOP STEAM */
      st_check("concurrent brew + LATCHED steam (word 24 = 1): the badge is STEAM·AUTO, and the STOP button is shown (latched steam can always be stopped) — v6.13: next to STOP BREW",
             hm.conc&&hm.badge==TR(TX_PH_STEAM_AUTO)&&act.ph==PH_EXT&&steam_stop_visible()&&sn==2&&sk[0]==ACT_BREW&&sk[1]==ACT_STEAM&&hit_test(160,372,t)==T_STOP&&hit_test(320,372,t)==T_STOPS); }
    t=st_home(); reg_seen[24]=0;
    st_s33(0x4121,t+=100); ui_frame(t);
    st_check("word 24 not read yet (sub33 replies only): neutral chip STEAM / DAMP, no auto-stop promise — and the STOP button is shown (always safe)",
             stm.kind==SK_UNKNOWN&&hm.chip==TR(TX_PH_STEAM)&&hm.sub[0]==0&&steam_stop_visible()&&hit_test(240,372,t)==T_STOP);
    st_s0(0x4121,0,0x000D,1,400,t+=50); ui_frame(t);
    st_check("… as soon as word 24 = 0 is read: STEAM·WAND",stm.kind==SK_WAND&&hm.chip==TR(TX_PH_STEAM_WAND)&&!steam_stop_visible());
    t=st_home(); reg[38]=123; reg[39]=7; reg[58]=640; reg_seen[51]=1; memset(&lt,0,sizeof lt); g_n=0; g_head=0;
    st_s0(0x0141,0,0x000C,0,300,t+=100); ui_frame(t); loop_graph(t);
    for(int q=0;q<4;q++){ st_s0(0x4161,0,0x000D,1,340,t+=100); ui_frame(t); loop_graph(t); }   /* v6.9: a source switch takes 300 ms (no UI start here) */
    st_check("grind + wand (0141 | 4020): the main phase is GRINDING with a steam badge, 3rd stat field = MILK, and the graph still takes the GRIND weight (word 38; v6.9: after 300 ms debounce; v6.15: the 300 ms of brew-scale samples are re-anchored away — 2 samples left, all word 38)",
             act.ph==PH_GRIND&&hm.conc&&hm.badge==TR(TX_PH_STEAM_WAND)&&hm.sl[2]==TR(TX_ST_MILK)&&hm.sl[1]==TR(TX_ST_CURRENT)&&g_n==2&&g_w[0]==123&&g_w[1]==123&&gr.resets==1&&gr.reanchors==1&&wsl.src==38&&wsl.n_sw==1);
    t=st_home(); reg[39]=200;
    st_s0(0x5327,0,0x000D,1,560,t+=100); ui_frame(t); ms_t t_st=stm.t0;
    st_s0(0x4121,0,0x000D,1,640,t+=9000); ui_frame(t);                             /* brew finished, wand still open */
    { char tbx[24]; ms_t el=t-t_st; snprintf(tbx,sizeof tbx,"%ld.%ld",(long)(el/1000),(long)((el%1000)/100));
      st_check("the brew ends while the wand is still open: the main phase becomes STEAM·WAND with no DONE in between; the TIME field is the STEAM's time",
               act.ph==PH_STEAM&&!hm.conc&&hm.chip==TR(TX_PH_STEAM_WAND)&&st_has(hm.svv[0],tbx)&&t_st==stm.t0); }
    last_rx_ms=t-2000; ui_frame(t+=10);
    st_check("the link drops during steam: OFFLINE, and the steam tracking is reset (no lingering STOP button)",act.ph==PH_OFFLINE&&stm.kind==SK_NONE&&!steam_stop_visible());
    t=st_home(); act.pending=ACT_STEAM; act.t_req=t; ui_frame(t+=33);
    snprintf(b,sizeof b,TR(TX_H_STARTING),TR(ACT_NAME[ACT_STEAM]));
    st_check("steam requested from the UI, bits not yet set: 'STARTING STEAM…' (display steam starts by itself ~0.2 s after the write; the wand does not need to be opened)",!strcmp(hm.sub,b));
    act.pending=ACT_NONE;
    { int ok=1; for(int lg=0;lg<NLANG;lg++){ int kl=ui_lang; lang_set(lg);
          for(int k=SK_UNKNOWN;k<=SK_WAND;k++){ float w=text_wf(steam_label(k),F_XS)+30; LAY(w,254,"steam badge (label field 260)",steam_label(k)); }
          lang_set(kl); }
      st_check("the steam badge fits in the label field in both languages (all three texts)",ok); }
    sim_reset();
}

/* ---- 64-bit clock: wraparound test. B31 = 2^31 ms (where v6.5's 32-bit 'long' wrapped: 24.86 days), B32 = 2^32 ms (49.7 days) ---- */
#define B31 ((ms_t)2147483648LL)
#define B32 ((ms_t)4294967296LL)
typedef struct { int n_fast,n_slow,n_ext,frames; ms_t gap_max,last_fast; } WrapStat;
/* one iteration of main(): poll timers + (if due) a frame — with the SAME functions main() uses */
static void wrap_iter(ms_t t,WrapStat*s){
    int l0=txq_len; loop_poll(t);
    for(int i=l0;i+7<=txq_len;i+=7){ uint8_t sub=txq[i+3];
        if(sub==0||sub==33){ s->n_fast++; if(s->last_fast&&t-s->last_fast>s->gap_max) s->gap_max=t-s->last_fast; s->last_fast=t; }
        else if(sub==56) s->n_slow++; else if(sub==112) s->n_ext++; }
    txq_len=0;
    last_rx_ms=t;                                               /* the link answers */
    if(loop_render_due(t)){ ui_frame(t); loop_rendered(t,1.0f); s->frames++; }
}
/* run the main loop from B-3000 to B+5000: 2 ms steps, and ONE long step (a slow frame) from B-12 to B+28, across
 * the poll, slow and ext deadlines, which are all due at B-10 */
static WrapStat wrap_loop(ms_t B){
    WrapStat s; memset(&s,0,sizeof s); st_home(); memset(&lt,0,sizeof lt); txq_len=0; ui_last=B-3010-33;   /* same UI state in every run: only the clock differs */
    ms_t t=B-3010;                                             /* first iteration: all timers fire; ext is then due at B-2010, B-1010, B-10 */
    for(;t<B-12;t+=2) wrap_iter(t,&s);
    wrap_iter(B-12,&s);
    for(t=B+28;t<=B+5000;t+=2) wrap_iter(t,&s);
    return s;
}
static int wrap_hold(ms_t t0){          /* hold-to-start across t0+650: 1 = started exactly at 650 ms */
    st_home(); ms_t t=t0; last_rx_ms=t; ui_frame(t);
    ui_touch(TE_DOWN,210,372,t); int early=0;
    for(;t<t0+649;t+=7){ last_rx_ms=t; ui_frame(t); if(tch.fired) early=1; }
    last_rx_ms=t0+649; ui_frame(t0+649); int before=!tch.fired&&act.pending==ACT_NONE;
    last_rx_ms=t0+650; ui_frame(t0+650); int at=tch.fired&&act.pending==ACT_BREW&&wq.n>=1&&wq.q[wq.n-1].sub==5&&wq.q[wq.n-1].len==18;
    ui_touch(TE_UP,210,372,t0+660); return !early&&before&&at;
}
static int wrap_stepper(ms_t t0){       /* hold + for 2.6 s on BREW WEIGHT: returns the editor value (repeat with acceleration) */
    st_home(); page=PG_T0; ms_t t=t0; last_rx_ms=t; ui_frame(t); detail_open(S_HOTW,t); tr.type=0; edit_val=SET[S_HOTW].lo;
    ui_touch(TE_DOWN,396,184,t);
    for(;t<=t0+2600;t+=33){ last_rx_ms=t; ui_frame(t); }
    int v=edit_val; ui_touch(TE_UP,396,184,t); detail_close(t); tr.type=0; return v;
}
static void st_wrap(void){
    printf("== selftest: v6.6 64-bit clock — wraparound at 2^31 ms (24.86 days: where v6.5's 32-bit 'long' wrapped) and 2^32 ms (49.7 days) ==\n");
    char nm[420];
    { struct timespec a={2592000,999000000}, b={4294967,296000000}, c={3155760000LL,0};      /* 30 days · 2^32 ms · 100 years */
      st_check("the time base is 64 bit, and second->ms is computed in 64 bit: 30 days = 2 592 000 999 ms, 4 294 967.296 s = 2^32 ms, 100 years = 3 155 760 000 000 ms",
               sizeof(ms_t)==8&&sizeof now_ms()==8&&ts_ms(&a)==(ms_t)2592000999LL&&ts_ms(&b)==B32&&ts_ms(&c)==(ms_t)3155760000000LL); }
    { ms_t a=now_ms(), b=now_ms(); st_check("now_ms() is monotonic (CLOCK_MONOTONIC) and non-negative",b>=a&&a>=0); }
    st_home();
    /* ---- the main loop's timers ---- */
    WrapStat ref=wrap_loop((ms_t)50000000), w31=wrap_loop(B31), w32=wrap_loop(B32), wfar=wrap_loop((ms_t)1<<45);
    snprintf(nm,sizeof nm,"the main loop's poll across 2^31 ms (a 40 ms step skips B-10, where the poll, slow and ext deadlines are): %d + %d + %d queries in 8 s, largest gap %ld ms — same as far from the boundary (%d + %d + %d, %ld ms)",
             w31.n_fast,w31.n_slow,w31.n_ext,(long)w31.gap_max,ref.n_fast,ref.n_slow,ref.n_ext,(long)ref.gap_max);
    st_check(nm,w31.n_fast==ref.n_fast&&w31.n_slow==ref.n_slow&&w31.n_ext==ref.n_ext&&w31.gap_max==ref.gap_max&&ref.n_fast>=155&&ref.n_slow>=38&&ref.n_ext>=8&&ref.gap_max<=50+40);   /* the gap = 50 ms + the long step */
    snprintf(nm,sizeof nm,"the render timer across 2^31 ms: %d frames in 8 s (far from the boundary: %d) — no frozen screen",w31.frames,ref.frames);
    st_check(nm,w31.frames==ref.frames&&ref.frames>=75);
    st_check("same across 2^32 ms and at 2^45 ms (1115 years): poll and frame counts are identical to the reference",
             w32.n_fast==ref.n_fast&&w32.n_slow==ref.n_slow&&w32.n_ext==ref.n_ext&&w32.frames==ref.frames&&w32.gap_max==ref.gap_max&&wfar.n_fast==ref.n_fast&&wfar.frames==ref.frames);
    /* ---- keep-awake ---- */
    { int ok=1; static const ms_t Bs[2]={B31,B32}; ms_t at[2][4]; int n[2];
      for(int q=0;q<2;q++){ ms_t B=Bs[q], t=B-20000; st_home(); memset(&wq,0,sizeof wq); txq_len=0; act.pending=ACT_NONE; n[q]=0;
          last_rx_ms=t; proto_keepawake(t,1); wq_step(t); st_ack_last(t+10); wq_step(t+12); st_ack_last(t+20); wq_step(t+22);      /* startup: PowerOn + awake; next deadline B+10000 */
          int k0=(int)wq.n_ok;
          for(t+=24;t<=B+45000;t+=2){ last_rx_ms=t; proto_keepawake(t,0); wq_step(t);
              if(wq.n&&wq.state==WQ_WAIT&&wq.sent_ms==t&&wq.tries==1){ if(wq.q[0].sub!=1||wq.q[0].len!=1||wq.q[0].w[0]!=0) ok=0; if(n[q]<4) at[q][n[q]]=t-B; n[q]++; }
              if(wq.n&&wq.state==WQ_WAIT&&t-wq.sent_ms>=20) st_ack_last(t); }
          if(wq.n_ok-k0!=n[q]||wq.n_fail||wq.n_retx) ok=0; }
      snprintf(nm,sizeof nm,"keep-awake (word 1 = 0 every 30 s) across 2^31 and 2^32 ms: exactly 2 writes in 65 s, at B+%ld and B+%ld ms (2^32: B+%ld / B+%ld) — no pause, no stream of writes",
               (long)at[0][0],(long)at[0][1],(long)at[1][0],(long)at[1][1]);
      st_check(nm,ok&&n[0]==2&&n[1]==2&&at[0][0]>=10000&&at[0][0]<=10004&&at[0][1]-at[0][0]>=30000&&at[0][1]-at[0][0]<=30004&&at[1][0]==at[0][0]&&at[1][1]==at[0][1]); }
    /* ---- ACK budget and RX resync window ---- */
    { st_home(); memset(&wq,0,sizeof wq); txq_len=0; ms_t t=B31-100; wq_push(11,(uint16_t[1]){880},1); wq_step(t);
      wq_step(B31+49); int r0=wq.n_retx==0; wq_step(B31+50); int r1=wq.n_retx==1&&wq.tries==2; wq_step(B31+349); int f0=wq.n_fail==0&&wq.n==1; wq_step(B31+350);
      st_check("the write queue's ACK budget across 2^31 ms: sent B-100, retransmit exactly at +150 ms (B+50), given up exactly 300 ms later (B+350)",r0&&r1&&f0&&wq.n_fail==1&&wq.n==0&&wq.result==WR_FAIL);
      rx_zero(); static const uint8_t cand[9]={0xAA,0x55,0x83,0x00,0x38,1,0,2,0};                 /* candidate promising 119 bytes */
      rx_feed(cand,9,B32-200); rx_tick(NULL,0,B32+199); int w0=rx_resync==0&&rxlen==9; rx_tick(NULL,0,B32+200);
      st_check("the RX resync window (400 ms) across 2^32 ms: incomplete candidate from B-200 is still there at B+199 and is discarded exactly at B+200",w0&&rx_resync==1&&rxlen==0);
      rx_zero(); }
    /* ---- hold-to-start + stepper repeat ---- */
    st_check("hold-to-start (650 ms) across 2^31 and 2^32 ms: nothing at 649 ms, the brew block (sub5 len18) queued exactly at 650 ms",wrap_hold(B31-300)&&wrap_hold(B32-300)&&wrap_hold((ms_t)70000000));
    { int vr=wrap_stepper((ms_t)80000000), v1=wrap_stepper(B31-1000), v2=wrap_stepper(B32-1000);
      snprintf(nm,sizeof nm,"hold on + for 2.6 s (repeat with acceleration) across 2^31 and 2^32 ms: same value as far from the boundary (%d = %d = %d)",v1,v2,vr);
      st_check(nm,v1==vr&&v2==vr&&vr>SET[S_HOTW].lo+10*SET[S_HOTW].step); }
    /* ---- toast, page transition, frame time ---- */
    { st_home(); ms_t t=B31-1000; last_rx_ms=t; toast_show("WRAP",C_OK,t); int on0=!r_empty(toast_rect(t+1))&&!r_empty(toast_rect(B31+1399)), off=r_empty(toast_rect(B31+1400));
      st_check("toast (2400 ms) across 2^31 ms: visible from B-1000 to B+1399, gone at B+1400",on0&&off);
      st_home(); t=B32-100; last_rx_ms=t; ui_frame(t); go_page(PG_T0,1,t); int in0=tr.type==TR_SLIDE; last_rx_ms=B32+100; ui_frame(B32+100); int mid=tr.type==TR_SLIDE; last_rx_ms=B32+161; ui_frame(B32+161);
      st_check("page transition (260 ms) across 2^32 ms: in progress at B+100, finished at B+161 — the page has changed",in0&&mid&&tr.type==TR_NONE&&page==PG_T0);
      memset(&lt,0,sizeof lt); ft_max=0; loop_rendered(B31-1000,42.0f); loop_rendered(B31+3999,1.0f); int keep=ft_max==42.0f; loop_rendered(B31+4002,1.0f);
      st_check("the frame time statistics (MAX reset every 5 s) across 2^31 ms: max is held until B+3999 and reset after 5 s",keep&&ft_max==1.0f&&ft_last==1.0f); }
    /* ---- differences over 2^31 ms: v6.5's 32-bit differences went negative after 24.86 days ---- */
    { const ms_t D=B31+60000;                                   /* 24.86 days + 1 min. later */
      ms_t t0=st_home(); last_rx_ms=t0; int up0=link_up(t0+10), dead=!link_up(t0+D);
      st_check("a link that has been silent for 24.86 days is still DOWN (32-bit: the difference went negative and the link looked 'up')",up0&&dead);
      t0=st_home(); toast_show("OLD",C_OK,t0); last_rx_ms=t0+D; int gone=r_empty(toast_rect(t0+D));
      act.t_done=t0; act.raw=PH_READY; act.ph=PH_READY; ui_frame(t0+D);
      st_check("24.86 days after the last activity and the last toast: no DONE chip and no toast coming back",gone&&act.ph==PH_READY&&!(toast.dur&&(t0+D)-toast.t0<toast.dur+50));
      rx_bad=3; last_bad_ms=t0; int crc=(t0+D)-last_bad_ms<10000&&rx_bad>0; rx_bad=0; last_bad_ms=-100000;
      st_check("24.86 days after the last CRC error: the CRC warning on HOME does not come back",!crc); }
    /* ---- seconds since boot + /proc start time ---- */
    { char root[300], p[400]; snprintf(root,sizeof root,"/tmp/merakiplus_v6_selftest_%d/wrap",(int)getpid()); snprintf(p,sizeof p,"%s/proc/4711",root); mk_p(root,"/proc/4711");
      char ksr[sizeof sysroot]; strcpy(ksr,sysroot); snprintf(sysroot,sizeof sysroot,"%s",root);
      mock_put(root,"/proc/4711/stat","4711 (demo_v9) T 1 4711 4711 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 300000000000 1404928 91 4294967295\n",120,0644);
      char stt='?'; int64_t s=proc_stat(4711,&stt); strcpy(sysroot,ksr);
      int64_t kf=mono_fake; mono_fake=(int64_t)5000000000LL; int64_t m=mono_s(); mono_fake=kf;
      st_check("/proc start time and seconds since boot are 64 bit: 300 000 000 000 ticks = 3 000 000 000 s (32-bit atol: saturated at 2^31 after 248 days); mono_s 5 000 000 000",
               s==(int64_t)3000000000LL&&stt=='T'&&m==(int64_t)5000000000LL); }
    { uint32_t k=rx_ok; rx_ok=0xFFFFFFFFu; rx_zero(); rx_ok=0xFFFFFFFFu; uint8_t f[128]; rx_feed(f,rx_v(f,1),(ms_t)1000); int z=rx_ok==0; rx_zero(); rx_ok=k;
      st_check("the RX counters are uint32 and wrap in a well-defined way (4 294 967 295 + 1 = 0) — no signed overflow after years in service",z); }
    sim_reset();
}

/* ---- MCU restart -> re-push ---- */
#define BPV_B_REAL "aa 55 06 70 03 04 01 00 06 46 00 c7 ce"   /* v6.7: = stock's captured frame (boot/ondevice/GROUND-TRUTH.md) */
typedef struct { ms_t t; uint16_t ws, p[3]; int have_p, ack, deaf; int n_a, n_b, n_other, n_verq, n_vera; ms_t t_a[12]; } RpSim;
static const uint8_t ST_VREQ[8]={0xAA,0x55,0x89,0x01,0x00,0x00,0x6F,0x81};
/* one iteration of main() in main()'s order: RX, write queue, boot push, re-push. The "simulated MCU": status (word 33) every
 * 50 ms, words 112..114 every 1 s, ACK 20 ms after a write (ack=1) — and it keeps what is written to words 112..114 */
static void rp_tick(RpSim*r){
    r->t+=2;
    if(r->t%50==0) st_status(r->ws,r->t);
    if(r->have_p&&r->t%1000==0){ uint8_t f[16]; rx_feed(f,mk_frame(f,0x83,112,r->p,3),r->t); }
    if(r->ack&&wq.n&&wq.state==WQ_WAIT&&r->t-wq.sent_ms>=20){ if(wq.q[0].sub==112&&wq.q[0].len==3&&!r->deaf) memcpy(r->p,wq.q[0].w,sizeof r->p); st_ack_last(r->t); }
    rx_feed(NULL,0,r->t); wq_step(r->t); boot_push_step(r->t); repush_step(r->t);
    if(wq.n&&wq.state==WQ_WAIT&&wq.sent_ms==r->t&&wq.tries==1){
        if(wq.q[0].sub==1&&wq.q[0].len==30){ if(r->n_a<12) r->t_a[r->n_a]=r->t; r->n_a++; } else if(wq.q[0].sub==112&&wq.q[0].len==3) r->n_b++; else r->n_other++; }
    for(int i=0;i+2<txq_len;i++) if(txq[i]==0xAA&&txq[i+1]==0x55&&txq[i+2]==0x09) r->n_vera++;
    txq_len=0;
}
static void rp_run(RpSim*r,ms_t ms){ ms_t e=r->t+ms; while(r->t<e) rp_tick(r); }
/* MCU restart: the register file at its start values, and n version requests 20 ms apart */
static void rp_reboot(RpSim*r,int n){
    r->p[0]=0; r->p[1]=0; r->p[2]=50; uint8_t f[16]; rx_feed(f,mk_frame(f,0x83,112,r->p,3),r->t);
    for(int k=0;k<n;k++){ rx_feed(ST_VREQ,8,r->t); r->n_verq++; rp_run(r,20); }
}
static void rp_init(RpSim*r,ms_t T,int guard){
    st_bp_reset(); memset(&rps,0,sizeof rps); ver_n=0; ver_ms=0; rx_n112=0; bp_guard_cfg=guard; bp_on=1;
    memset(r,0,sizeof *r); r->t=T; r->ws=0x0101; r->ack=1; r->have_p=1; r->p[2]=50;            /* the MCU is at its start values (slot B boot) */
    boot_push_start(guard);
}
static void st_repush(const char*cfgsrc){
    printf("== selftest: v6.6 MCU restart -> re-push (0x89 again / start-value signature; same blocks, same frame policy, same activity guard) ==\n");
    int bpon0=bp_on, g0=bp_guard_cfg, ro0=ro_mode; RpSim r; char nm[420]; const ms_t T=60000000;
    char dir[300], fx[340]; snprintf(dir,sizeof dir,"/tmp/merakiplus_v6_selftest_%d",(int)getpid()); mkdir(dir,0755); snprintf(fx,sizeof fx,"%s/repush_fixture.json",dir);
    snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load();
    /* ---- (1) the first handshake is NOT a restart ---- */
    rp_init(&r,T,1);
    for(int k=0;k<3;k++){ rx_feed(ST_VREQ,8,r.t); r.n_verq++; rx_feed(NULL,0,r.t); boot_push_step(r.t); repush_step(r.t); for(int i=0;i+2<txq_len;i++) if(txq[i]==0xAA&&txq[i+2]==0x09) r.n_vera++; txq_len=0; r.t+=20; }
    int quiet=wq.n==0&&bp.state==BP_WAIT_A&&!bp.t0_set;
    rp_run(&r,60);                                             /* status -> the guard is decided -> sub1 len30 queued */
    int started=bp.t0_set&&r.n_a==1;
    rx_feed(ST_VREQ,8,r.t); r.n_verq++;                        /* straggler from the first handshake, < 200 ms after the push went out */
    rp_run(&r,12000);
    st_check("startup: 3 version requests BEFORE the boot push + a straggler right after it = the first handshake — answered (4 x cmd 9), no re-push",
             quiet&&started&&r.n_vera==4&&r.n_a==1&&r.n_b==1&&bp.state==BP_DONE&&bp.ra==BPR_OK&&bp.rb==BPR_OK&&bp.runs==1&&repush_count()==0&&!rps.armed);
    st_check("the controller was at its start values (words 112..114 = 0 / 0 / 50) and now has the config's (0104 / 0600 / 70 — word 113 lo = 0 like stock); 12 s later nothing new has been sent",
             r.p[0]==0x0104&&r.p[1]==0x0600&&r.p[2]==70&&reg[112]==0x0104&&r.n_other==0&&wq.n_ok==2);
    /* ---- (2) MCU restart mid-session: 0x89 comes again ---- */
    int a0=r.n_a, b0=r.n_b; ms_t t_rb=r.t;
    rp_reboot(&r,5);                                           /* 5 requests in 100 ms; the last at t_rb+80 */
    int during=r.n_a==a0&&rps.armed&&repush_count()==0;
    while(r.t<t_rb+80+RP_SETTLE_MS-2) rp_tick(&r);
    int settle=r.n_a==a0&&repush_count()==0;                   /* 298 ms after the last 0x89: nothing yet */
    rp_run(&r,4); int begun=repush_count()==1&&rps.n_ver==1&&rps.last==RPC_VER&&bp.state!=BP_DONE;
    rp_run(&r,60);
    int a_sent=r.n_a==a0+1&&hexeq(last_tx,last_tx_len,BPV_A_REAL);
    rp_run(&r,40); int b_sent=r.n_b==b0+1&&hexeq(last_tx,last_tx_len,BPV_B_REAL);
    rp_run(&r,3000);
    st_check("MCU RESTART (0x89 again, 5 of them): all answered; nothing is sent until the round has been quiet for 300 ms and a FRESH work_sta has been read",
             during&&settle&&begun&&r.n_vera==4+5);
    st_check("RE-PUSH = the boot push: sub1 len30 (independent vector, 67 bytes) -> ACK -> sub112 len3 (… 04 01 00 06 46 00 c7 ce) -> ACK; OK / OK, 0 retransmit, 0 block",
             a_sent&&b_sent&&bp.state==BP_DONE&&bp.ra==BPR_OK&&bp.rb==BPR_OK&&bp.runs==2&&wq.n_ok==4&&wq.n_retx==0&&wq.n_fail==0&&wq.n_block==0&&r.n_other==0);
    st_check("after the re-push the controller is back on config (words 112..114 = 0104 / 0600 / 70, word 11 = 870); counter: 1 (cause 0x89); 3 s later nothing new",
             r.p[0]==0x0104&&r.p[1]==0x0600&&r.p[2]==70&&reg[11]==870&&reg[25]==650&&repush_count()==1&&rps.n_def==0&&r.n_a==a0+1);
    { char v[DG_N][28],lb[48],lb2[48],want[48]; uint32_t col[DG_N]; last_rx_ms=r.t; diag_top_text(v,col,r.t); uint32_t lc=diag_label(14,lb,sizeof lb), lc2=diag_label(15,lb2,sizeof lb2); snprintf(want,sizeof want,TR(TX_DL_BP_A_RE),1);
      st_check("DIAGNOSE: the boot push row is now called 'RE-PUSH 1 · 1-30' (accent colour) | '112-114'; the cells show the outcome of the latest push: OK / OK (green)",
               !strcmp(lb,want)&&st_has(lb,"RE-PUSH 1 ")&&lc==C_ACC&&!strcmp(lb2,TR(TX_DL_BP_B_RE))&&lc2==C_TXT3&&!strcmp(v[14],TR(TX_DV_OK))&&!strcmp(v[15],TR(TX_DV_OK))&&col[14]==C_OK&&col[15]==C_OK); }
    /* ---- (3) two restarts close together: at least RP_MIN_MS between two re-pushes ---- */
    { ms_t ta=r.t_a[r.n_a-1]; a0=r.n_a; rp_reboot(&r,3); rp_run(&r,1000); int held=r.n_a==a0&&rps.armed;   /* 4.2 s after the previous one: the round has settled, but the spacing is not reached */
      rp_run(&r,6000);
      snprintf(nm,sizeof nm,"new restart 3.2 s after the previous re-push: held back until %d ms have passed (here %ld ms between the two sub1 len30), then sent — counter 2",RP_MIN_MS,(long)(r.t_a[r.n_a-1]-ta));
      st_check(nm,held&&r.n_a==a0+1&&repush_count()==2&&r.t_a[r.n_a-1]-ta>=RP_MIN_MS&&r.t_a[r.n_a-1]-ta<RP_MIN_MS+200&&bp.ra==BPR_OK&&bp.rb==BPR_OK); }
    /* ---- (4) the activity guard also applies to the re-push ---- */
    rp_run(&r,6000); a0=r.n_a; b0=r.n_b;
    r.ws=0x4121; rp_reboot(&r,4); rp_run(&r,2000);              /* restart, and the user has the wand open (steam bits set) */
    { uint32_t c0; int skip=bp.state==BP_DONE&&bp.ra==BPR_SKIP&&bp.rb==BPR_SKIP&&r.n_a==a0&&r.n_b==b0&&rps.n_ver==3&&bp_text(0,&c0)==TR(TX_DV_BP_SKIP);
      rp_run(&r,6000); int still=r.n_a==a0&&rps.n_def==0;       /* the signature is present (0 / 0), but the machine is not idle */
      st_check("THE GUARD: restart while an activity is running (work_sta 4121) -> the re-push is SKIPPED, 0 bytes sent; the signature triggers nothing either while the activity runs",skip&&still);
      r.ws=0x0101; rp_run(&r,1900); int notyet=r.n_a==a0;       /* idle: 2 reads are not enough */
      rp_run(&r,2400);
      st_check("… and when the machine is idle, the START-VALUE SIGNATURE (word 112 = 0, word 113 = 0 in 3 reads) catches it: the push is sent — cause 'start values', OK / OK",
               notyet&&r.n_a==a0+1&&r.n_b==b0+1&&rps.n_def==1&&rps.last==RPC_DEF&&bp.ra==BPR_OK&&bp.rb==BPR_OK&&r.p[0]==0x0104); }
    /* ---- (5) the signature alone (0x89 round lost) ---- */
    rp_run(&r,6000); a0=r.n_a; int d0=rps.n_def;
    r.p[0]=0; r.p[1]=0; r.p[2]=50;                               /* restart without merakiplus seeing a 0x89 */
    { ms_t ts=r.t; while(r.n_a==a0&&r.t<ts+10000) rp_tick(&r); ms_t took=r.t-ts; rp_run(&r,3000);
      snprintf(nm,sizeof nm,"restart WITHOUT 0x89 (round lost): the signature triggers the re-push after 3 fresh reads (%ld ms) — controller back on config, counter +1",(long)took);
      st_check(nm,r.n_a==a0+1&&rps.n_def==d0+1&&took>=3000&&took<=4100&&r.p[0]==0x0104&&r.p[1]==0x0600&&bp.ra==BPR_OK); }
    { a0=r.n_a; d0=rps.n_def; rp_run(&r,6000); uint16_t keep[3]; memcpy(keep,r.p,sizeof keep);
      r.p[0]=0; r.p[1]=0; rp_run(&r,2100); r.p[0]=keep[0]; r.p[1]=keep[1]; rp_run(&r,1000); r.p[0]=0; r.p[1]=0; rp_run(&r,2000); int no=r.n_a==a0;
      memcpy(r.p,keep,sizeof keep); rp_run(&r,3000);
      st_check("the signature must hold for 3 reads IN A ROW: 2 x (0 / 0), 1 x config, 2 x (0 / 0) triggers nothing",no&&r.n_a==a0&&rps.n_def==d0); }
    /* ---- (6) budget: a controller that does not accept ---- */
    { rp_init(&r,T+10000000,1); rp_run(&r,3000); int base=r.n_a==1&&bp.ra==BPR_OK;
      r.deaf=1; r.p[0]=0; r.p[1]=0; r.p[2]=50;                   /* ACKs, but the values do not stick */
      ms_t ts=r.t; rp_run(&r,RP_WINDOW_MS-30000); int n1=rps.n_def, a1=r.n_a;
      ms_t gap=r.t_a[2]-r.t_a[1];
      rp_run(&r,30000+gap-1000); int n2=rps.n_def;               /* 10 min. after the FIRST one its slot is free again (the next one only 'gap' later) */
      snprintf(nm,sizeof nm,"BUDGET: the signature persists (the controller does not accept): %d re-pushes in the first %d.5 min. (%ld ms apart), then quiet; %d min. after the first, ONE new one is allowed — no endless stream of writes",
               n1,(int)((RP_WINDOW_MS-30000)/60000),(long)gap,(int)(RP_WINDOW_MS/60000));
      if(!(base&&n1==RP_BUDGET&&a1==1+RP_BUDGET&&n2==RP_BUDGET+1)) printf("     base=%d n1=%d a1=%d n2=%d gap=%ld\n",base,n1,a1,n2,(long)gap);
      st_check(nm,base&&n1==RP_BUDGET&&a1==1+RP_BUDGET&&gap>=RP_MIN_MS&&n2==RP_BUDGET+1&&r.t-ts>RP_WINDOW_MS); }
    /* ---- (7) restart IN THE MIDDLE of a push ---- */
    { rp_init(&r,T+20000000,1); rp_run(&r,60); int a_out=r.n_a==1&&bp.state==BP_A;
      rp_run(&r,24); int a_ok=bp.ra==BPR_OK;                     /* block A ACKed */
      r.ack=0; rp_run(&r,230);                                   /* the MCU restarts: block B gets no ACK … */
      rx_feed(ST_VREQ,8,r.t); r.n_verq++; rp_run(&r,20); rx_feed(ST_VREQ,8,r.t); r.n_verq++;   /* … and it asks for the version again (> 200 ms after the push started) */
      int armed=rps.armed&&bp.state!=BP_DONE;
      rp_run(&r,400); int nb=bp.rb==BPR_NOACK||bp.state!=BP_DONE;
      r.ack=1; rp_run(&r,6000);
      st_check("restart IN THE MIDDLE of the boot push (block A ACKed, block B without ACK, 0x89 again): the push is completed (NO ACK), and then BOTH blocks are sent again from the start — OK / OK",
               a_out&&a_ok&&armed&&nb&&r.n_a==2&&r.n_b==2&&rps.n_ver==1&&bp.state==BP_DONE&&bp.ra==BPR_OK&&bp.rb==BPR_OK&&r.p[0]==0x0104); }
    /* ---- (8) the SIM contract (no guard): postpone instead of skipping ---- */
    { rp_init(&r,T+30000000,0); rp_run(&r,3000); a0=r.n_a;
      r.ws=0x1307; rp_reboot(&r,3); rp_run(&r,8000); int wait=r.n_a==a0&&bp.state==BP_WAIT_A&&rps.n_ver==1;
      r.ws=0x0101; rp_run(&r,500);
      st_check("without the guard (the SIM contract): a re-push during an activity is POSTPONED and sent when the machine is idle",wait&&r.n_a==a0+1&&bp.ra==BPR_OK&&bp.rb==BPR_OK); }
    /* ---- (9) where there is NO re-push ---- */
    { int ok=1;
      st_fixture(fx,1200,1); snprintf(cfg_path,sizeof cfg_path,"%s",fx); settings_load();           /* config out of spec */
      rp_init(&r,T+40000000,1); rp_run(&r,3000); int blk=bp.ra==BPR_BLOCKED; long nb0=wq.n_block; a0=r.n_a;
      r.p[0]=0; r.p[1]=0; rp_run(&r,20000); if(!(blk&&r.n_a==a0&&rps.n_def==0&&wq.n_block==nb0&&!repush_cfg_sane())) ok=0;
      snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load(); unlink(fx);
      rp_init(&r,T+50000000,1); ro_mode=1; rp_run(&r,3000); rp_reboot(&r,3); rp_run(&r,20000); if(r.n_a||r.n_b||repush_count()||r.n_vera!=3) ok=0; ro_mode=0;
      rp_init(&r,T+60000000,1); memset(&bp,0,sizeof bp); bp_on=0; rp_run(&r,3000); rp_reboot(&r,3); rp_run(&r,20000); if(r.n_a||r.n_b||repush_count()||r.n_vera!=3) ok=0; bp_on=1;
      st_check("no re-push: config out of spec (the block would be rejected again — no stream of BLOCK counts), MERAKIPLUS_READONLY, MERAKIPLUS_NOBOOTPUSH; 0x89 is still answered",ok); }
    { char la[48],lb[48]; int ok=1;
      for(int lg=0;lg<NLANG;lg++){ int kl=ui_lang; lang_set(lg); rps.n_ver=600; rps.n_def=600; diag_label(14,la,sizeof la); diag_label(15,lb,sizeof lb);
          float a=text_wf(la,F_XS), b2=text_wf(lb,F_XS); LAY(a/2+b2/2+8,DG_COL_R-DG_COL_L,"diagnose label pair with re-push counter",la); LAY(a,2*(DG_COL_L-40),"diagnose label L",la);
          if(!st_has(la,"RE-PUSH 999 ")) ok=0; rps.n_ver=rps.n_def=0; diag_label(14,la,sizeof la); diag_label(15,lb,sizeof lb); if(strcmp(la,TR(TX_DL_BP_A))||strcmp(lb,TR(TX_DL_BP_B))) ok=0;
          lang_set(kl); }
      st_check("DIAGNOSE: the label pair with the re-push counter (cap 999) does not touch in any language; without a re-push the row is unchanged 'BOOT PUSH 1-30' | 'BOOT PUSH 112-114'",ok); }
    /* ---- clean up ---- */
    bp_on=bpon0; bp_guard_cfg=g0; ro_mode=ro0; snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load();
    st_bp_reset(); memset(&rps,0,sizeof rps); ver_n=0; rx_n112=0; sim_reset();
}

/* ============================ v6.7: the net guard (wifi at startup + self-heal) ============================ */
/* The model (nm_*) stands in for wlan0/wpa_supplicant/udhcpc; the state machine is the same code as on the device.
 * The conf vectors are computed outside the program (python hashlib) — same template as the device's .conf_bak (61 bytes). */
#define NETV_HDR  "ctrl_interface=/tmp/wifi/run/wpa_supplicant\nupdate_config=1\n"
#define NETV_PSK  NETV_HDR "\nnetwork={\n\tssid=\"TestNet\"\n\tpsk=\"abcdefgh12\"\n\tkey_mgmt=WPA-PSK\n}\n"
#define NETV_PSK_MD5 "5a970359d863288e79358cae148dc9a9"
#define NETV_OPEN_MD5 "c1a621ab7f10b7829bb2b6865d9284b0"
static void st_net_run(ms_t*t,int ms){ for(int k=0;k<ms;k+=20){ *t+=20; net_step(*t); } }
static int st_net_cfg(const char*path,int on,const char*ssid_json,const char*pw_json){
    char b[600]; int n=snprintf(b,sizeof b,"{\n\t\"Temperature\":\t1,\n\t\"wifionoff\":\t%d,\n\t\"ssid\":\t\"%s\",\n\t\"password\":\t\"%s\",\n\t\"presetStore\":\t{\n\t\t\"ssid\":\t\"NOT-THIS-ONE\"\n\t},\n\t\"init\":\t1\n}",on,ssid_json,pw_json);
    return put_bytes(path,b,n);
}
static int st_file_md5(const char*path,char out[33]){
    static char b[8192]; int fd=open(path,O_RDONLY); if(fd<0){ strcpy(out,"-"); return -1; } int n=(int)read(fd,b,sizeof b); close(fd); if(n<0) n=0;
    md5_hex((const uint8_t*)b,(size_t)n,out); return n;
}
static void st_net(const char*dir){
    printf("== selftest: v6.7 the net guard (wifi at startup + self-heal; model of wlan0/wpa_supplicant/udhcpc) ==\n");
    char ksr[sizeof sysroot],kcfg[sizeof cfg_path],root[360],p[460],cf[460],conf[460],lg[460],m[33]; char t1[64]; uint32_t col;
    strcpy(ksr,sysroot); strcpy(kcfg,cfg_path);
    snprintf(root,sizeof root,"%s/netroot",dir); mkdir(root,0755);
    snprintf(p,sizeof p,"%s/tmp",root); mkdir(p,0755); snprintf(p,sizeof p,"%s/appconfigs",root); mkdir(p,0755);
    snprintf(sysroot,sizeof sysroot,"%s",root);
    snprintf(cf,sizeof cf,"%s/config.json",root); snprintf(cfg_path,sizeof cfg_path,"%s",cf);
    snprintf(conf,sizeof conf,"%s" NET_CONF,root); snprintf(lg,sizeof lg,"%s" NET_LOG,root);
    snprintf(p,sizeof p,"%s" NET_CONF_BAK,root); put_bytes(p,NETV_HDR "\n",61);
    ms_t T=50000000L, t;
    /* --- the conf format --- */
    st_net_cfg(cf,1,"TestNet","abcdefgh12"); net_cfg_load();
    { char b[4096]; int n=net_conf_build(b,sizeof b); md5_hex((const uint8_t*)b,(size_t)n,m);
      st_check("conf = the save_config format: the template's global lines + empty line + network={ ssid=\"…\" psk=\"…\" key_mgmt=WPA-PSK } (125 bytes, independent md5 5a970359…)",
               net.cfg==NC_OK&&n==125&&!memcmp(b,NETV_PSK,125)&&!strcmp(m,NETV_PSK_MD5)); }
    st_net_cfg(cf,1,"Caf\\u00e9",""); net_cfg_load();
    { char b[4096]; int n=net_conf_build(b,sizeof b); md5_hex((const uint8_t*)b,(size_t)n,m);
      st_check("ssid with JSON escape (\\u00e9 -> UTF-8) is written as hex without quotes, open network -> key_mgmt=NONE (105 bytes, md5 c1a621ab…)",net.cfg==NC_OK&&n==105&&!strcmp(m,NETV_OPEN_MD5)); }
    { int ok=1;
      st_net_cfg(cf,0,"TestNet","abcdefgh12"); net_cfg_load(); ok&=net.cfg==NC_WIFIOFF;
      st_net_cfg(cf,1,"","abcdefgh12"); net_cfg_load(); ok&=net.cfg==NC_NOSSID;
      st_net_cfg(cf,1,"TestNet","kort"); net_cfg_load(); ok&=net.cfg==NC_BADPSK;
      st_net_cfg(cf,1,"TestNet","abc\\u0001defghij"); net_cfg_load(); ok&=net.cfg==NC_BADPSK;
      unlink(cf); net_cfg_load(); ok&=net.cfg==NC_NOCFG;
      st_net_cfg(cf,1,"TestNet","abcdefgh12"); net_cfg_load(); ok&=net.cfg==NC_OK&&!strcmp(net.ssid,"TestNet");   /* presetStore's "ssid" (depth 2) is ignored */
      st_check("config guard: wifionoff 0 / empty ssid / password < 8 or with control characters / no config -> the guard is OFF; only the top-level keys are read",ok); }
    { /* v6.20 R6 (review persist #2): a password that is PRESENT but unusable is a bad password — never an open network */
      char pw90[100]; memset(pw90,'a',90); pw90[90]=0; int ok=1, sub[6];
      st_net_cfg(cf,1,"TestNet",pw90); net_cfg_load(); sub[0]=net.cfg==NC_BADPSK;
      st_net_cfg(cf,1,"TestNet","abcdefgh\\\\u12"); net_cfg_load(); sub[1]=net.cfg==NC_OK&&!strcmp(net.psk,"abcdefgh\\u12");   /* an escaped backslash is fine */
      st_net_cfg(cf,1,"TestNet","abcdefgh\\\\"); net_cfg_load(); sub[1]&=net.cfg==NC_OK&&!strcmp(net.psk,"abcdefgh\\");
      st_net_cfg(cf,1,"TestNet","abcdefgh\\uZZ12"); net_cfg_load(); sub[2]=net.cfg==NC_BADPSK;                            /* a malformed \u escape */
      const char*NUL="{\n\t\"wifionoff\":\t1,\n\t\"ssid\":\t\"TestNet\",\n\t\"password\":\tnull\n}";
      put_bytes(cf,NUL,(int)strlen(NUL)); net_cfg_load(); sub[3]=net.cfg==NC_BADPSK;
      const char*NOPW="{\n\t\"wifionoff\":\t1,\n\t\"ssid\":\t\"TestNet\"\n}";
      put_bytes(cf,NOPW,(int)strlen(NOPW)); net_cfg_load(); sub[4]=net.cfg==NC_OK&&net.psk[0]==0;           /* missing = open (as before) */
      const char*NL="{\n\t\"wifionoff\":\t1,\n\t\"ssid\":\t\"TestNet\",\n\t\"password\":\n\t\"abcdefgh12\"\n}";
      put_bytes(cf,NL,(int)strlen(NL)); net_cfg_load(); sub[5]=net.cfg==NC_OK&&!strcmp(net.psk,"abcdefgh12");  /* a line break after the colon is still the string */
      for(int i=0;i<6;i++){ if(!sub[i]) printf("     R6 case %d failed (cfg=%d)\n",i,net.cfg); ok&=sub[i]; }
      st_check("R6 net guard: a password that is present but unusable (90 bytes, a malformed \\u escape, null) is BAD PASSWORD — the network is left alone, never key_mgmt=NONE; a missing key is still an open network",ok); }
    /* --- 1. healthy stack at start (runtime B): left alone --- */
    unlink(conf); unlink(lg); nm_reset(0); t=T; net_start(t,1); st_net_run(&t,60000);
    diag_net_text(t1,sizeof t1,&col);
    st_check("HEALTHY STACK at start (stock's wpa_supplicant 841 + ip): UP on the first probe, and for 60 s: 0 init, 0 wpa starts, 0 udhcpc, the conf file not even written",
             net.state==NS_UP&&nm.n_init==0&&nm.n_wpa==0&&nm.n_dhcp==0&&nm.n_prep==0&&nm.n_ifup==0&&access(conf,F_OK)!=0&&file_has(lg,"UP at start: wpa_supplicant pid 841"));
    st_check("DIAGNOSE NET row: '841 / 192.168.1.100 / UP' (green)",st_has(t1,"841 / 192.168.1.100 / ")&&st_has(t1,TR(TX_NS_UP))&&col==C_OK);
    /* --- 2. slot B cold start: no wlan0, no supplicant, no IP --- */
    nm_reset(1); t=T; net_start(t,1); net_step(t);
    int init1=nm.n_init==1&&net.step==NB_INIT&&net.state==NS_STARTING;
    diag_net_text(t1,sizeof t1,&col); int dstart=st_has(t1,"- / - / ")&&st_has(t1,TR(TX_NS_STARTING))&&col==C_WARN;
    ms_t up_at=0; for(int k=0;k<1500&&!up_at;k++){ t+=20; net_step(t); if(net.state==NS_UP) up_at=t; }
    st_file_md5(conf,m);
    st_check("COLD START (slot B): the init script runs ONCE (like MI_WLAN_Init), conf is written from config.json (= the vector), wpa_supplicant + udhcpc are started ONCE each",
             init1&&nm.n_init==1&&nm.n_wpa==1&&nm.n_dhcp==1&&net.n_conf==1&&!strcmp(m,NETV_PSK_MD5));
    snprintf(t1,sizeof t1,"UP %d ms after startup",(int)(up_at-T));
    st_check("COLD START: UP (ip 192.168.1.100) within 10 s in the model; DIAGNOSE showed STARTING along the way; the log has every step",
             up_at&&up_at-T<10000&&dstart&&net.n_boot==1&&net.p.ip==nm.lease&&file_has(lg,"missing: running \"source /config/wifi/ssw01bInit.sh dummy1 dummy2\"")&&file_has(lg,"written from config.json")&&file_has(lg,"started: wpa_supplicant -i wlan0 -Dnl80211 -c /appconfigs/wpa_supplicant.conf")&&file_has(lg,"started udhcpc -i wlan0 -s /etc/init.d/udhcpc.script -a -q -b -t 20 -T 1")&&file_has(lg,t1));
    st_check("the password never appears in the log",!file_has(lg,"abcdefgh12"));
    /* --- 3. wpa_supplicant killed (IP + route stay on wlan0) -> self-heal, same IP, no udhcpc --- */
    { int w0=nm.n_wpa,d0=nm.n_dhcp,c0=net.n_conf; uint32_t ip0=net.p.ip; st_net_run(&t,10000); ms_t tk=t;
      nm.wpa_pid=0; nm.dhcp_pid=0; nm.carrier=0;
      ms_t det=0,back=0; int pid0=0; for(int k=0;k<1500&&!back;k++){ t+=20; net_step(t); if(!det&&net.dead) det=t; if(det&&net.state==NS_UP){ back=t; pid0=net.p.wpa_pid; } }
      snprintf(t1,sizeof t1,"SELF-HEAL done in");
      st_check("KILL wpa_supplicant: detected within 2 s, bring-up after 3 s; NEW supplicant, SAME IP, no udhcpc (ip + route kept), conf not rewritten",
               det&&det-tk<=2100&&back&&nm.n_wpa==w0+1&&nm.n_dhcp==d0&&net.n_conf==c0&&net.p.ip==ip0&&pid0!=841&&pid0>0&&net.n_heal==1&&file_has(lg,"kept (no udhcpc)")&&file_has(lg,t1));
      snprintf(t1,sizeof t1,"heal in %d ms (limit 10 s in the model)",(int)(back-tk));
      st_check(t1,back-tk<10000&&net.heal_ms>0); }
    /* --- 4. wpa killed AND IP gone -> wpa + udhcpc (like stock) --- */
    { int w0=nm.n_wpa,d0=nm.n_dhcp; nm.wpa_pid=0; nm.carrier=0; nm.ip=0; nm.gw=0;
      for(int k=0;k<1500&&!(net.state==NS_UP&&nm.n_wpa>w0);k++){ t+=20; net_step(t); }
      st_check("KILL wpa_supplicant + IP gone: wpa_supplicant and then udhcpc (stock's order) -> UP, heal no. 2",net.state==NS_UP&&nm.n_wpa==w0+1&&nm.n_dhcp==d0+1&&net.n_heal==2); }
    /* --- 5. the supplicant is alive, the IP disappears -> udhcpc after 8 s; the supplicant is left alone --- */
    { int w0=nm.n_wpa,d0=nm.n_dhcp; ms_t tk=t; nm.ip=0; nm.gw=0; ms_t dh=0;
      for(int k=0;k<1500&&!(dh&&net.state==NS_UP);k++){ t+=20; net_step(t); if(!dh&&nm.n_dhcp>d0) dh=t; }
      st_check("NO IP (the supplicant is alive): udhcpc is started after approx. 8 s (not before), NO new wpa_supplicant -> UP",dh&&dh-tk>=8000&&dh-tk<=10500&&nm.n_wpa==w0&&nm.n_dhcp==d0+1&&net.state==NS_UP); }
    /* --- 6. ifconfig wlan0 down (the supplicant is alive, the routes gone) -> ifconfig up -> udhcpc for the route --- */
    { int w0=nm.n_wpa,d0=nm.n_dhcp,u0=nm.n_ifup; ms_t tk=t; nm.ifup=0; nm.carrier=0; nm.gw=0; ms_t upa=0;
      for(int k=0;k<2000&&!(upa&&net.state==NS_UP);k++){ t+=20; net_step(t); if(!upa&&nm.n_ifup>u0) upa=t; }
      st_check("WLAN0 DOWN (the supplicant is alive): 'ifconfig wlan0 up' after approx. 3 s, then udhcpc for the lost default route -> UP; NO new wpa_supplicant",
               upa&&upa-tk>=3000&&upa-tk<=5200&&nm.n_ifup==u0+1&&nm.n_dhcp==d0+1&&nm.n_wpa==w0&&net.state==NS_UP&&net.p.gw&&file_has(lg,"ifconfig wlan0 up (like MI_WLAN_Open)")); }
    /* --- 7. AP gone for 2 min: the supplicant reconnects by itself -> no intervention --- */
    { int w0=nm.n_wpa,d0=nm.n_dhcp,u0=nm.n_ifup,i0=nm.n_init; nm.ap=0; nm.carrier=0; st_net_run(&t,120000);
      int quiet=nm.n_wpa==w0&&nm.n_dhcp==d0&&nm.n_ifup==u0&&nm.n_init==i0&&net.state==NS_CONNECTING;
      nm.ap=1; nm.assoc_at=t+NM_ASSOC_MS; st_net_run(&t,4000);
      st_check("AP GONE for 2 min (no carrier, IP stays): no intervention at all — the supplicant reconnects by itself; then 'UP again … without intervention'",quiet&&net.state==NS_UP&&file_has(lg,"without intervention")); }
    /* --- 8. a network without a gateway is healthy too (no udhcpc stream) --- */
    { nm_reset(1); nm.gw_offer=0; t=T; net_start(t,1); st_net_run(&t,90000);
      st_check("network WITHOUT a default route (DHCP gives no router): UP after cold start, and for 90 s only the one udhcpc (no stream of restarts)",net.state==NS_UP&&nm.n_dhcp==1&&!net.gw_seen); }
    /* --- 9. the init script hangs: no kill, no second insmod --- */
    { nm_reset(1); nm.init_hang=1; t=T; net_start(t,1); st_net_run(&t,60000);
      int hung=nm.n_init==1&&net.step==NB_INIT&&net.init_hung&&file_has(lg,"it is not killed");
      nm.iface=1; st_net_run(&t,8000);
      st_check("THE INIT SCRIPT HANGS: after 25 s it is logged (no kill call, no new insmod); when wlan0 appears, it continues to wpa_supplicant -> UP",hung&&nm.n_init==1&&nm.n_wpa==1&&net.state==NS_UP&&net.orphan); }
    /* --- 10. wpa_supplicant will not start: backoff 5, 10, 20, 40, 60 s --- */
    { nm_reset(0); nm.wpa_pid=0; nm.carrier=0; nm.wpa_fail=1; t=T; net_start(t,1); st_net_run(&t,300000);
      st_check("wpa_supplicant does not start: backoff 5 -> 10 -> 20 -> 40 -> 60 s (cap) — 8 attempts in 5 min, not hundreds",nm.n_wpa>=6&&nm.n_wpa<=9&&net.backoff==NET_BACKOFF_MAX_MS&&net.state==NS_DOWN); }
    /* --- 11. wifi turned off in config: the guard touches nothing, not even a cold stack --- */
    { st_net_cfg(cf,0,"TestNet","abcdefgh12"); unlink(conf); nm_reset(1); t=T; net_start(t,1); st_net_run(&t,120000);
      diag_net_text(t1,sizeof t1,&col);
      st_check("wifionoff = 0: OFF — 0 init, 0 wpa, 0 udhcpc, conf not written; DIAGNOSE 'OFF' (neutral)",net.state==NS_OFF&&nm.n_init==0&&nm.n_wpa==0&&nm.n_dhcp==0&&access(conf,F_OK)!=0&&st_has(t1,TR(TX_NS_OFF))&&col==C_TXT2);
      net_start(t,0); nm_reset(1); st_net_run(&t,30000);
      st_check("MERAKIPLUS_NONET / test root: the guard is fully disabled (no probe, nothing started)",!net.on&&nm.n_init==0&&nm.n_wpa==0); }
    /* --- the NET row fits in both languages, also in the worst case (5-digit pid, longest IP, longest state) --- */
    { int ok=1, kl=ui_lang;
      for(int lgx=0;lgx<NLANG;lgx++){ lang_set(lgx); for(int st=NS_OFF;st<=NS_UP;st++){ char w[96]; snprintf(w,sizeof w,"32767 / 255.255.255.255 / %s",TR(TX_NS_OFF+st)); LAY(text_wf(w,diag_net_font(w)),DG_FULL_W,"NET row",w); }
        LAY(text_wf(TR(TX_DG_L_NET),F_XS),DG_FULL_W,"NET-label",TR(TX_DG_L_NET)); }
      lang_set(kl);
      { char w[64]; snprintf(w,sizeof w,"841 / 192.168.1.100 / %s",TR(TX_NS_UP)); ok&=diag_net_font(w)==F_S; }   /* the healthy line is in F_S */
      st_check("DIAGNOSE NET row fits in EN and DA: the healthy line in F_S; in the worst case ('32767 / 255.255.255.255 / CONNECTING') it switches to F_XS and stays within the full-width field",ok); }
    /* cleanup: back to the preview default (healthy stack) */
    strcpy(sysroot,ksr); strcpy(cfg_path,kcfg);
    nm_reset(0); memset(&net,0,sizeof net); net.on=1; net.cfg_logged=-1; nh_probe(&net.p,sim_now,1); net.state=NS_UP; net.booted=net.was_up=net.gw_seen=1;
}
/* ====================================================================== */
/* ===== v6.9: display conditioning + stable weight source + deadband ===== */
/* ====================================================================== */
/* synthetic sequences (like sim/mcu_sim.py + BREW.md/STEAMLIVE.md): brew plateau with ADC noise and deadhead pulses, steps, idle with
 * maintenance pulses. The tests read the ring buffer (raw g_* and displayed g_*d), the hero/stat model and the chart scale (cst). */
static int cs_ws=0x1307, cs_p=1320, cs_w39=0, cs_w38=0, cs_pulse=1, cs_glitch=0;
static void tick_cs(ms_t t){
    reg[33]=(uint16_t)cs_ws; reg[39]=(uint16_t)cs_w39; reg[38]=(uint16_t)cs_w38;
    int p=cs_p+((t/97)%41)-20;                                         /* ADC noise ±20 */
    if(cs_pulse&&t>=500&&(t%4000)<100) p=16000;                                /* deadhead pulse, 1 sample every 4 s */
    if(cs_pulse&&(t%6000)>=2500&&(t%6000)<2700) p=15500;               /* deadhead pulse, 2 samples every 6 s (together < 10 % of samples) */
    reg[51]=(uint16_t)(p<0?0:p); reg[52]=cs_ws==0x0101?0:360;
}
/* idle + maintenance pulses (work_sta 0x0101 | 0x0400/0x0800/0x2000/0x0C00/0x2800 — BREW.md §5, mcu_sim.py) and optionally one
 * single work_sta sample with bit 6 (glitch) — the weight words stay put: 38 = last grind, 39 = last cup */
static void tick_idle_maint(ms_t t){
    static const uint16_t PB[5]={0x0400,0x0800,0x2000,0x0C00,0x2800};
    int k=(int)((t/1700)%5), on=(t%1700)<250;
    reg[33]=(uint16_t)(0x0101|(on?PB[k]:0)); reg[51]=on?(uint16_t)(3000+k*2700):(uint16_t)(t%2); reg[52]=0;
    if(cs_glitch&&t>=cs_glitch&&t<cs_glitch+100) reg[33]=0x0141;         /* ONE sample with bit 6 */
    reg[38]=(uint16_t)cs_w38; reg[39]=(uint16_t)cs_w39;
}
static int g_last(const int*a){ return a[(g_head+GN-1)%GN]; }
static float g_lastf(const float*a){ return a[(g_head+GN-1)%GN]; }
static void st_chart_now(void){ home_model(sim_now); static_dirty=1; force_all=1; ui_frame(sim_now+1); }
static void st_cond(void){
    printf("== selftest: v6.9 display conditioning (median-3 + EMA) + robust chart scale + stable weight source + weight deadband ==\n");
    char a[32],b[64];
    /* ---- (1) spike train: brew plateau 1320 ± 20 with deadhead pulses 16000 (1 sample / 4 s) and 15500 (2 samples / 6 s) ----
     *      (the preview harness's sample rate is 132 ms; the pulses are together approx. 8 % of the active samples — p90 tolerates < 10 %) */
    sim_reset(); cs_ws=0x1307; cs_p=1320; cs_w39=200; cs_pulse=1; sim_tick=tick_cs; sim_run(20000); st_chart_now();
    { float dmax=0; int rmax=0; for(int i=0;i<110;i++){ int k=(g_head+GN-1-i)%GN; if(g_pd[k]>dmax) dmax=g_pd[k]; if(g_p[k]>rmax) rmax=g_p[k]; }
      snprintf(b,sizeof b,"%.0f",(double)dmax);
      st_check("SPIKE TRAIN: raw peak >= 15500, DISPLAYED peak <= plateau + 5 % (median-3 removes the 1-sample pulse, the pulse gate the 2-sample pulse)",rmax>=15500&&dmax<=1340*1.05f);
      printf("       raw max %d · displayed max %s (last 110 samples = 14.5 s)\n",rmax,b);
      st_check("SPIKE TRAIN: chart top = p90 of active samples x 1.15 (1340..1800) — the curve is not flattened; the pulses are shown as ticks at the top edge",
               cst.active&&cst.mode==0&&cst.top>=1340&&cst.top<=1800&&cst.ticks>0&&cst.disp_max<=1400&&cst.na>140);
      { int keep=cd.gate; cd.gate=0; sim_reset(); cs_ws=0x1307; cs_p=1320; cs_w39=200; cs_pulse=1; sim_tick=tick_cs; sim_run(20000); float g0=0; for(int i=0;i<110;i++){ int k=(g_head+GN-1-i)%GN; if(g_pd[k]>g0) g0=g_pd[k]; }
        cd.gate=keep; sim_reset(); cs_ws=0x1307; cs_p=1320; cs_w39=200; cs_pulse=1; sim_tick=tick_cs; sim_run(20000); st_chart_now();
        st_check("… control: without the pulse gate (cd.gate=0) the 2-sample pulse gets through median-3 (displayed > 3000) — the gate is what keeps it down",g0>3000);
        printf("       without gate: displayed max %.0f\n",(double)g0); }
      int top_pct=cst.top; cd.raw=1; st_chart_now(); int rawtop=cst.top, rawd=cst.disp_max; cd.raw=0; st_chart_now();
      printf("       chart top: smooth (p90) %d · RAW = v6.8 (raw series + max scale) %d\n",top_pct,rawtop);
      st_check("… vs v6.8 = DIAGNOSE → GRAPH = RAW (unconditioned series, max scale): top >= 16000 x 1.08, so the plateau filled less than 1/10 of the height; back to SMOOTH afterwards",
               rawd>=15500&&rawtop>=16000*1.08f-1&&1340*10<rawtop&&!cd.raw&&cst.top==top_pct); }
    /* ---- (1b) the first seconds of an extraction while the window is mostly NOT brew. v6.15: idle -> brew clears the graph, so this
     * now only happens when the brew starts during steam (no idle -> active transition): 36 s of steam (pressure 0), then brew + steam ---- */
    sim_reset(); cs_ws=0x4121; cs_p=0; cs_w39=0; cs_pulse=0; sim_tick=tick_cs; sim_run(36000); cs_ws=0x5327; cs_p=1320; sim_run(3000); st_chart_now();
    { int all[GN],n=0; ms_t t0=sim_now-40000; for(int i=0;i<g_n;i++){ int k=(g_head+GN-g_n+i)%GN; if(g_t[k]>=t0) all[n++]=g_p[k]; } qsort(all,(size_t)n,sizeof all[0],cmp_int);
      int p90all=all[(n-1)*90/100];
      st_check("3 s into an extraction: p90 of ACTIVE samples keeps the plateau in scale (top >= 1320), where p90 of the WHOLE window would be ~0 and squash the curve against the ceiling",
               cst.top>=1320&&cst.top<=1800&&p90all<40&&cst.clip==0); printf("       p90 whole window %d · top %d\n",p90all,cst.top); }
    /* ---- (2) step within the same phase: 360 -> 1320 (no phase change = no snap) ---- */
    sim_reset(); cs_ws=0x1307; cs_p=360; cs_w39=100; cs_pulse=0; sim_tick=tick_cs; sim_run(5000);
    { int i0=g_n; cs_p=1320; sim_run(4000); int n=g_n-i0, settle=-1, over=0; float first=g_pd[(g_head+GN-n)%GN];
      for(int i=0;i<n;i++){ int k=(g_head+GN-n+i)%GN; if(settle<0&&fabsf(g_pd[k]-(float)g_p[k])<=0.02f*1320) settle=i; if(g_pd[k]>1340+0.5f) over=1; }
      st_check("STEP 360 -> 1320 within the same phase: first displayed sample is still at 360 (median-3), no overshoot, within 2 % in <= 2.5 s (25 samples)",first<400&&!over&&settle>=0&&settle<=25);
      printf("       settled after %d samples (%.1f s)\n",settle,settle/10.0); }
    /* ---- (3) snap on phase change and set-point change ---- */
    sim_reset(); cs_ws=0x1107; cs_p=358; cs_w39=0xFFFD; cs_pulse=0; sim_tick=tick_cs; sim_run(3000);
    { cs_ws=0x1307; cs_p=1320; int i0=g_n; sim_run(400); int n=g_n-i0; float v[4]; int r[4]; for(int i=0;i<n&&i<4;i++){ int k=(g_head+GN-n+i)%GN; v[i]=g_pd[k]; r[i]=g_p[k]; }
      int ok=n>=3&&v[0]<400&&fabsf(v[1]-(float)med3(358,r[0],r[1]))<0.01f&&v[2]>1290;
      st_check("PHASE CHANGE (PRE-INF. 1107 -> EXTRACTION 1307): the first 2 samples follow median-3 directly (no EMA) — the step is complete on the 2nd sample, nothing is smeared out",ok);
      printf("       displayed %.0f / %.0f / %.0f  (raw %d / %d / %d)\n",(double)v[0],(double)v[1],(double)v[2],r[0],r[1],r[2]); }
    sim_reset(); cs_ws=0x1307; cs_p=1320; cs_w39=300; cs_pulse=0; sim_tick=tick_cs; sim_run(4000);
    { reg_seen[51]=1; cs_ws=0x0101; cs_p=0; sim_tick=NULL; reg[33]=0x0101; reg[51]=16000; reg[52]=0; int i0=g_n; sim_run(132); reg[51]=12; sim_run(1500);
      float mx=0; for(int i=g_n-i0-1;i>=0;i--){ int k=(g_head+GN-1-i)%GN; if(g_pd[k]>mx) mx=g_pd[k]; }
      st_check("PHASE CHANGE right in a deadhead pulse (brew stops, first idle sample = 16000): the pulse does not get through (median-3 at the change) — displayed max <= the plateau",mx<=1345); }
    sim_reset(); sim_tick=NULL; reg[40]=870; reg[11]=870; sim_run(3000); home_model(sim_now);
    { reg[40]=905; sim_run(100); home_model(sim_now); fmt_t10(a,sizeof a,905,1); int lag=strcmp(hm.hero,a)!=0;
      reg[11]=900; home_model(sim_now); int snap=!strcmp(hm.hero,a);
      st_check("SET-POINT CHANGE (boiler 87.0 -> 90.0): the hero jumps to the raw value (90.5) in the same frame; without a change it glides (median-3 + EMA)",lag&&snap); }
    sim_reset(); cs_ws=0x0101; cs_w38=0; cs_w39=367; sim_tick=tick_idle_maint; sim_run(3000);
    { cs_w38=12; tick_idle_maint(0); reg[33]=0x0141; sim_run(100); home_model(sim_now); }
    /* ---- (3b) idle after a brew with maintenance pulses: the pressure curve is flat, the pulses are ticks on the baseline ---- */
    sim_reset(); cs_ws=0x1307; cs_p=1320; cs_w39=300; cs_pulse=0; sim_tick=tick_cs; sim_run(10000);
    { cs_w38=0; cs_w39=367; sim_tick=tick_idle_maint; sim_run(20000); st_chart_now(); int ok=act.ph==PH_READY&&cst.active&&cst.mode==0;
      float mx=0; int rmx=0; for(int i=0;i<140;i++){ int k=(g_head+GN-1-i)%GN; float v=g_a[k]?g_pd[k]:0; if(v>mx) mx=v; if(g_p[k]>rmx) rmx=g_p[k]; }
      st_check("IDLE AFTER BREW with maintenance pulses (0.25 s, up to 13800): the chart still shows the brew; the 20 s of idle are drawn flat (0), the pulses as ticks on the baseline; scale = the brew plateau",
               ok&&rmx>=8000&&mx==0&&cst.bticks>0&&cst.top>=1320&&cst.top<=1800);
      sim_run(31000); st_chart_now();
      st_check("… when the brew is out of the 40 s window: 'NO ACTIVITY' (v6.8: every maintenance pulse lit up a spike chart)",!cst.active&&cst.bticks>0); }
    /* ---- (4) the weight source: maintenance pulses + one bit-6 glitch while idle — no flicker ---- */
    sim_reset(); cs_w38=186; cs_w39=367; cs_glitch=7300; sim_tick=tick_idle_maint;
    { int nphg=0,n0=g_n,bad=0,pulses=0; for(int q=0;q<200;q++){ sim_run(100); if(phase_main(sim_now)==PH_GRIND) nphg++; if((reg[33]&0x2C00)) pulses++; if(g_last(g_w)!=367) bad++; }
      cs_glitch=0;
      st_check("IDLE + maintenance pulses (0x0400/0x0800/0x2000/0x0C00/0x2800) + ONE bit-6 sample: the weight source stays word 39 (0 switches), all 200 samples = 36.7 g — no 18.6 g spike",
               g_n-n0>=195&&nphg>=1&&pulses>=20&&bad==0&&wsl.src==39&&wsl.n_sw==0);
      printf("       %d samples with phase GRIND (glitch) · %d with maintenance bits · %d samples from the wrong word\n",nphg,pulses,bad);
      st_chart_now(); snprintf(b,sizeof b,"36.7 %s",TR(TX_U_G));
      st_check("… and the WEIGHT stat field while idle shows 36.7 g (conditioned)",!strcmp(hm.svv[2],b)); }
    /* real grind (physical start, no UI request): switch after 300 ms, DONE keeps word 38, back to 39 afterwards */
    { cs_w38=0; int bad=0,n_early=0; ms_t t0=sim_now;
      for(int q=0;q<50;q++){ cs_w38=q*4; tick_idle_maint(0); sim_tick=NULL; reg[33]=0x0141; reg[38]=(uint16_t)cs_w38; reg[39]=367; sim_run(100);
          if(sim_now-t0<=300&&g_last(g_w)==367) n_early++; if(sim_now-t0>=500&&g_last(g_w)!=(int)(int16_t)reg[38]) bad++; }
      int sw1=wsl.n_sw==1&&wsl.src==38;
      st_check("REAL GRIND without UI start (bit 6 for 6.6 s): the source switches ONCE to word 38 after 300 ms debounce; after that every sample is the grind weight",sw1&&bad==0&&n_early>=2);
      reg[33]=0x0101; int dbad=0; for(int q=0;q<50;q++){ sim_run(100); if(g_last(g_w)!=196) dbad++; }   /* sim_run(100) = 4 x 33 ms: 50 x 132 ms = 6.6 s */
      st_check("… the DONE screen (8 s; measured over the first 6.6 s) keeps word 38 (the grind result 19.6 g) — no switch",act.ph==PH_DONE&&wsl.src==38&&wsl.n_sw==1&&dbad==0);
      sim_run(2000);
      st_check("… DONE -> READY: back to word 39 (ONE switch); 2 switches in total for the whole grind",act.ph==PH_READY&&wsl.src==39&&wsl.n_sw==2&&g_last(g_w)==367);
      { int ok=1; tick_idle_maint(0); reg[38]=196; reg[39]=367; sim_tick=NULL; sim_reset(); reg[38]=196; reg[39]=367; reg[33]=0x0141; sim_run(1000);   /* grind -> 38 latched */
        reg[33]=0x0101; for(int q=0;q<75;q++){ sim_run(100); home_model(sim_now); if(act.ph==PH_READY&&g_lsrc==39&&fabsf(g_lastf(g_wd)-367.f)>0.5f) ok=0;
            if(act.ph==PH_READY&&strstr(hm.svv[2],"19.6")) ok=0; }
        st_check("… the source switch 38 -> 39 carries no history over: the first sample from word 39 is shown as 36.7 (no 19.6 g flash in the stat field)",ok&&wsl.src==39); } }
    /* UI-started grind: immediately on the first sample with bit 6; then a brew (UI-started) immediately back to 39 */
    sim_reset(); sim_tick=NULL; reg[38]=0xFFFF; reg[39]=367; sim_run(1000);
    { act_request(ACT_GRIND,sim_now); int req=act.pending==ACT_GRIND; sim_run(150); reg[33]=0x0141; reg[38]=0; sim_run(100);
      st_check("UI-started GRIND (act_request): the source switches IMMEDIATELY on the first sample with bit 6 — the hero shows 0.0 g (tare), not the cup's 36.7 g",req&&wsl.src==38&&wsl.n_sw==1&&g_last(g_w)==0);
      for(int q=0;q<20;q++){ reg[38]=(uint16_t)(q*9); sim_run(100); } reg[33]=0x0101; sim_run(1500);
      memset(&wq,0,sizeof wq); act.pending=ACT_NONE; reg[39]=0xFFFD; act_request(ACT_BREW,sim_now); sim_run(150); reg[33]=0x1107; sim_run(100);
      st_check("… and a UI-started BREW right after (grind DONE still showing): immediately back to word 39 — the brew hero never shows the grind weight",wsl.src==39&&wsl.n_sw==2&&g_last(g_w)==-3); }
    /* cup off / on while idle: the stat field jumps (without a 2 s tail) */
    sim_reset(); sim_tick=NULL; reg[39]=364; sim_run(3000);
    { reg[39]=0xFFFE; sim_run(300); home_model(sim_now); snprintf(b,sizeof b,"0.0 %s",TR(TX_U_G)); int off=!strcmp(hm.svv[2],b);
      reg[39]=91; sim_run(300); home_model(sim_now); snprintf(a,sizeof a,"9.1 %s",TR(TX_U_G));
      st_check("CUP OFF / ON while idle (36.4 g -> -0.2 g -> 9.1 g): the stat field shows the new level after 2 samples (median-3 + step snap > 2 g) — without a 2 s tail",off&&!strcmp(hm.svv[2],a)); }
    /* ---- (5) deadband: |weight| < 0.25 g is shown as 0 (hero, chart, stat field); raw is kept ---- */
    sim_reset(); sim_tick=NULL;
    { static const int V[]={-2,-1,0,1,2,3,-3,25}; static const char*const S[]={"0.0","0.0","0.0","0.0","0.0","0.3","-0.3","2.5"}; int ok=1;
      for(int q=0;q<8;q++){ reg[33]=0x0101; reg[39]=(uint16_t)V[q]; sim_run(4000); home_model(sim_now); snprintf(b,sizeof b,"%s %s",S[q],TR(TX_U_G));
          if(strcmp(hm.svv[2],b)){ ok=0; printf("       idle, word39=%d: stat '%s', expected '%s'\n",V[q],hm.svv[2],b); } }
      st_check("DEADBAND (idle, stat field): -0.2 / -0.1 / 0 / 0.1 / 0.2 g -> 0.0 g; 0.3 g and -0.3 g are shown; 2.5 g is shown",ok); }
    { int ok=1; static const int V[]={2,-2,1,3,24}; static const char*const S[]={"0.0","0.0","0.0","0.3","2.4"};
      for(int q=0;q<5;q++){ reg[33]=0x1307; reg[51]=1300; reg[39]=(uint16_t)V[q]; sim_run(2500); st_chart_now();
          float cw=wdb(g_lastf(g_wd));
          if(strcmp(hm.hero,S[q])||(abs(V[q])<3&&cw!=0.f)){ ok=0; printf("       brew, word39=%d: hero '%s' (expected '%s'), chart weight %.2f\n",V[q],hm.hero,S[q],(double)cw); } }
      st_check("DEADBAND (brew: hero + chart weight): 0.2 / -0.2 / 0.1 g -> 0.0 and the chart line at 0; 0.3 g and 2.4 g are shown",ok); }
    { reg[39]=2; sim_run(1000); int raw=g_last(g_w)==2&&s16(39)==2&&reg[39]==2;
      cd.raw=1; st_chart_now(); int rv=!strcmp(hm.hero,"0.2"); cd.raw=0; st_chart_now();
      st_check("… display only: the ring buffer, reg[] and the state JSON's regs are raw (0.2 g); DIAGNOSE → GRAPH = RAW shows 0.2",raw&&rv&&!strcmp(hm.hero,"0.0")); }
    /* ---- (6) signed tare: 0xFFFD (-0.3 g) mixed with 0 / 2 / 0xFFFF — no smearing towards 6553.3 g ---- */
    sim_reset(); sim_tick=NULL; reg[33]=0x1107; reg[51]=358;
    { static const uint16_t T[]={0xFFFD,0x0000,0xFFFD,0x0002,0xFFFF,0xFFFD,0xFFFD,0x0000}; float lo=1e9f,hi=-1e9f; int ok=1;
      for(int q=0;q<80;q++){ reg[39]=T[q%8]; sim_run(100); float v=g_lastf(g_wd); if(v<lo) lo=v; if(v>hi) hi=v; if(g_last(g_w)<-3||g_last(g_w)>2) ok=0; }
      st_chart_now(); int cw=(int)lroundf(wdb(g_lastf(g_wd)));
      Cond u; cond_snap(&u,0); float um=0; for(int q=0;q<80;q++){ float v=cond_step(&u,(int)T[q%8],cd.a_chart); if(v>um) um=v; }   /* same filter on UNSIGNED words: that is what the sim bug looked like */
      st_check("SIGNED TARE: word 39 = 0xFFFD/0/2/0xFFFF is filtered as -3..2 (shown -0.3..0.2 g) — never towards 65533; hero 0.0, chart weight 0",ok&&lo>=-3.01f&&hi<=2.01f&&!strcmp(hm.hero,"0.0")&&cw==0);
      printf("       signed: displayed %.2f..%.2f · same filter on unsigned words: up to %.0f (= %.1f g)\n",(double)lo,(double)hi,(double)um,(double)um/10);
      st_check("… control: the same filter on UNSIGNED words smears the tare out to > 1000 g — the test has teeth",um>10000); }
    sim_reset(); sim_tick=NULL;
}
/* ============================ v6.10: pressure estimate ============================ */
static int str_any(const char*needle){ for(int lg=0;lg<NLANG;lg++) for(int i=0;i<NSTR;i++) if(STR[lg][i]&&strstr(STR[lg][i],needle)) return 1; return 0; }
static void st_press(const char*cfgsrc,const char*dir){
    printf("== selftest: v6.10 pressure estimate (anchor: pre-infusion %d raw in the capture = config preInfusionPressure; k = anchor / P0; > %.0f bar = pulse) ==\n",P_ANCHOR_RAW,(double)P_EST_MAX);
    char b[96],e[96];
    /* ---- (1) the scale math ---- */
    sim_reset();
    { float k=p_k(); int ok=P_ANCHOR_RAW==358&&SET[S_PIPRESS].loaded&&sv(S_PIPRESS)==4&&p_anchor_bar()==4&&fabsf(k-4.f/358.f)<1e-7f;
      ok&=fabsf(p_bar(358)-4.f)<0.001f&&fabsf(p_bar(1346)-15.039f)<0.01f&&fabsf(p_bar(1379)-15.408f)<0.01f&&p_bar(0)==0.f;
      snprintf(b,sizeof b,"%.1f / %.1f / %.1f",(double)p_bar(358),(double)p_bar(1346),(double)p_bar(1379));
      st_check("SCALE: P0 = 358 raw, anchor = config preInfusionPressure 4 -> k = 4/358 = 0.011173 bar/raw; 358 -> 4.0, 1346 -> 15.0, 1379 -> 15.4, 0 -> 0",ok&&!strcmp(b,"4.0 / 15.0 / 15.4"));
      printf("       k = %.6f bar/raw · pre 358 -> %.2f · ext 1346 -> %.2f · ext-top 1379 -> %.2f\n",(double)k,(double)p_bar(358),(double)p_bar(1346),(double)p_bar(1379)); }
    /* ---- (2) HOME: the BAR field during pre-infusion and extraction ---- */
    sim_reset(); cs_ws=0x1107; cs_p=358; cs_w39=0xFFFD; cs_w38=0; cs_pulse=0; cs_glitch=0; sim_tick=tick_cs; sim_run(4000); st_chart_now();
    { float v=(float)atof(hm.svv[1]);
      st_check("HOME, PRE-INF. (plateau 358 +- 20 noise): the BAR field shows 4.0 (+- 0.1) under the label '≈BAR'",act.ph==PH_PRE&&hm.sl[1]==TR(TX_ST_BAR)&&fabsf(v-4.f)<=0.1f&&strstr(TR(TX_ST_BAR),"\xE2\x89\x88"));
      printf("       pre: '%s' = '%s'\n",hm.sl[1],hm.svv[1]); }
    cs_ws=0x1307; cs_p=1346; sim_run(6000); st_chart_now();
    { float v=(float)atof(hm.svv[1]);
      st_check("HOME, EXTRACTION (plateau 1346): the BAR field shows 15.0 (+- 0.2) — linear anchor, pump side (UI_v6.md §16)",act.ph==PH_EXT&&hm.sl[1]==TR(TX_ST_BAR)&&fabsf(v-15.f)<=0.2f);
      printf("       ext: '%s' = '%s'\n",hm.sl[1],hm.svv[1]); }
    st_check("v6.17: the BAR field's label is '≈BAR' (EN + DA; the ≈ says 'estimate' — v6.11-v6.16 added 'EST.' on top of it), the chart caption is short ('PRESSURE' / 'TRYK'); 'UKALIBRERET' / 'UNCAL' no longer appear in any string",
             !strcmp(STR[LANG_EN][TX_ST_BAR],"≈BAR")&&!strcmp(STR[LANG_DA][TX_ST_BAR],"≈BAR")&&!strcmp(STR[LANG_EN][TX_C_PRESS],"PRESSURE")&&!strcmp(STR[LANG_DA][TX_C_PRESS],"TRYK")&&!str_any("UKALIB")&&!str_any("UNCAL"));
    /* ---- (3) the basis: toast on tapping the chart / the BAR field (no frames, no phase change) ---- */
    { snprintf(e,sizeof e,TR(TX_T_PINFO),4,358); int n0=wq.n; long ok0=wq.n_ok;
      toast.dur=0; tap(240,268); int t1=toast.dur>0&&!strcmp(toast.msg,e); sim_run(3000);
      toast.dur=0; tap(CX,318); int t2=toast.dur>0&&!strcmp(toast.msg,e);
      st_check("BASIS: tapping the chart and the BAR field during a brew -> toast 'PRE-INF. 4 BAR = RAW 358 · LINEAR' (no frames sent, brew unchanged)",t1&&t2&&wq.n==n0&&wq.n_ok==ok0&&act.ph==PH_EXT);
      printf("       toast: '%s'\n",toast.msg);
      sim_reset(); sim_run(1500); toast.dur=0; tap(240,268); int t3=toast.dur==0;
      st_check("… while idle with no brew in the window ('NO ACTIVITY') the chart gives no toast",t3&&!cst.active); }
    /* ---- (4) the config anchor: preInfusionPressure 3 in a config copy changes the scale ---- */
    { char op[400],kb[400]; snprintf(op,sizeof op,"%s/press_anchor.json",dir); snprintf(kb,sizeof kb,"%s",cfg_bakdir);
      int ok=copy_file(cfgsrc,op); snprintf(cfg_path,sizeof cfg_path,"%s",op); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/pressbak",dir);
      ok&=cfg_set_int("preInfusionPressure",3);
      sim_reset(); cs_ws=0x1307; cs_p=1346; cs_pulse=0; sim_tick=tick_cs; sim_run(6000); st_chart_now();
      char anc[64],nw[64]; uint32_t c; diag_press_text(anc,sizeof anc,nw,sizeof nw,&c);
      snprintf(b,sizeof b,TR(TX_DV_PANCH),3,358,(double)(3.f/358.f)); snprintf(e,sizeof e,TR(TX_T_PINFO),3,358); toast.dur=0; tap(240,268);
      float v=(float)atof(hm.svv[1]);
      st_check("CONFIG ANCHOR: preInfusionPressure = 3 (config copy, settings_load) -> k = 3/358; ext 1346 -> 11.3; the DIAGNOSE anchor and the toast say 3 BAR",
               ok&&p_anchor_bar()==3&&fabsf(p_k()-3.f/358.f)<1e-7f&&fabsf(v-11.3f)<=0.2f&&!strcmp(anc,b)&&!strcmp(toast.msg,e));
      printf("       anchor 3: BAR '%s' · '%s'\n",hm.svv[1],anc);
      snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s",kb);
      sim_reset(); cs_ws=0x1307; cs_p=1346; sim_tick=tick_cs; sim_run(6000); st_chart_now(); v=(float)atof(hm.svv[1]);
      int kv=SET[S_PIPRESS].val; SET[S_PIPRESS].val=0; int d0=p_anchor_bar(); SET[S_PIPRESS].val=99; int d99=p_anchor_bar(); SET[S_PIPRESS].val=kv;
      st_check("… back on the real config (4) -> 15.0; an anchor value outside 1..15 (0 / 99) gives the default 4 bar",p_anchor_bar()==4&&fabsf(v-15.f)<=0.2f&&d0==4&&d99==4); }
    /* ---- (5) deadhead / maintenance: never a false bar figure ---- */
    { float lim=P_EST_MAX/p_k(); int ok=p_bar(16670)<0.f&&p_bar(15716)<0.f&&p_bar(floorf(lim))>=0.f&&p_bar(ceilf(lim)+1)<0.f;
      st_check("LIMIT: linear > 20 bar (raw > 1790) is a pulse -> no figure; 16670 (186 bar linear) and 15716 (176) are pulses; 1790 = 20.0 is shown",ok); }
    sim_reset(); cs_ws=0x1307; cs_p=1346; cs_pulse=0; sim_tick=tick_cs; sim_run(5000); cs_p=16670; sim_run(1500); st_chart_now();
    { int no186=1; for(int i=0;i<3;i++) if(strstr(hm.svv[i],"186")||strstr(hm.svv[i],"17")==hm.svv[i]) no186=0;
      st_check("HOME, sustained deadhead (16670 for 1.5 s during EXTRACTION): the BAR field shows 'PULSE' — not '186.2'",act.ph==PH_EXT&&!strcmp(hm.svv[1],TR(TX_ST_PULSE))&&no186);
      printf("       deadhead: '%s' = '%s' (raw %u)\n",hm.sl[1],hm.svv[1],reg[51]);
      char anc[64],nw[64]; uint32_t c; diag_press_text(anc,sizeof anc,nw,sizeof nw,&c); snprintf(e,sizeof e,TR(TX_DV_PPULSE),(int)reg[51]);
      st_check("… the DIAGNOSE now line during a brew: 'RAW 16670 · PULSE, NO PRESSURE' (amber), no bar figure",!strcmp(nw,e)&&c==C_WARN&&!strstr(nw,"BAR")); }
    sim_reset(); cs_w38=0; cs_w39=367; cs_glitch=0; sim_tick=tick_idle_maint; sim_run(3000); sim_tick=NULL; reg[33]=0x0101; reg[51]=16670; sim_run(100); st_chart_now();
    { char anc[64],nw[64]; uint32_t c; diag_press_text(anc,sizeof anc,nw,sizeof nw,&c); snprintf(e,sizeof e,TR(TX_DV_PMAINT),16670);
      st_check("IDLE + maintenance pulse 16670: DIAGNOSE shows 'RAW 16670 · MAINTENANCE PULSE' (amber) — not 186 bar; HOME shows no BAR (the chart: ticks on the baseline)",
               act.ph==PH_READY&&!strcmp(nw,e)&&c==C_WARN&&!strstr(nw,"186")&&hm.sl[1]!=TR(TX_ST_BAR));
      printf("       idle: '%s'\n",nw);
      reg[51]=12; diag_press_text(anc,sizeof anc,nw,sizeof nw,&c); snprintf(e,sizeof e,TR(TX_DV_PNOW),12,(double)(12*4.f/358.f));
      st_check("… and idle without a pulse (raw 12): 'RAW 12 = ≈0.1 BAR'",!strcmp(nw,e)&&c==C_TXT); }
    /* ---- (6) the RAW view (DIAGNOSE → GRAPH) is untouched: raw figures, raw ring buffer ---- */
    sim_reset(); cs_ws=0x1307; cs_p=1346; cs_pulse=0; sim_tick=tick_cs; sim_run(6000); st_chart_now();
    { char glat[16]; snprintf(glat,sizeof glat,"%s",hm.svv[1]); int rb=g_last(g_p)==(int)reg[51];
      cd.raw=1; hf.ok=0; st_chart_now(); snprintf(b,sizeof b,"%d",(int)reg[51]); int r1=!strcmp(hm.svv[1],b)&&hm.sl[1]==TR(TX_ST_PRAW)&&cst.mode==0&&cst.raw_max>=1326;
      printf("       SMOOTH '%s' · RAW '%s' = '%s'\n",glat,hm.sl[1],hm.svv[1]);
      cs_p=16670; sim_run(400); st_chart_now(); snprintf(b,sizeof b,"%d",(int)reg[51]); int r2=!strcmp(hm.svv[1],b);
      cd.raw=0; hf.ok=0; cs_p=1346; sim_run(6000); st_chart_now();
      printf("       raw pulse '%s' · back in SMOOTH '%s' = '%s' (rb %d r1 %d r2 %d)\n",b,hm.sl[1],hm.svv[1],rb,r1,r2);
      st_check("RAW VIEW: the BAR field shows word 51 as a raw figure (label 'PRESSURE RAW'), also a pulse (16670 — no clamping in raw); the ring buffer is raw; back to SMOOTH -> ≈bar again",
               rb&&r1&&r2&&g_last(g_p)==(int)reg[51]&&hm.sl[1]==TR(TX_ST_BAR)&&fabsf((float)atof(hm.svv[1])-15.f)<=0.2f); }
    sim_reset(); sim_tick=NULL;
}
/* ============================ v6.11: UX review (touch targets, contrast, swipe guard, close chevron, gear, hints, plain language, DIAGNOSE groups) ============================ */
static float st_lum(uint32_t c){ float v[3]={(float)((c>>16)&255),(float)((c>>8)&255),(float)(c&255)}; for(int i=0;i<3;i++){ v[i]/=255.f; v[i]=v[i]<=0.03928f?v[i]/12.92f:powf((v[i]+0.055f)/1.055f,2.4f); } return 0.2126f*v[0]+0.7152f*v[1]+0.0722f*v[2]; }
static float st_cr(uint32_t a,uint32_t b){ float x=st_lum(a),y=st_lum(b); if(x<y){ float t=x; x=y; y=t; } return (x+0.05f)/(y+0.05f); }
/* the touch area for target t, MEASURED through hit_test() (not the drawing constants): bounding box and pixel count */
static int st_hitbox(int t,int*w,int*h){ int x0=W,y0=H,x1=-1,y1=-1,n=0;
    for(int y=0;y<H;y++) for(int x=0;x<W;x++) if(hit_test(x,y,sim_now)==t){ n++; if(x<x0)x0=x; if(x>x1)x1=x; if(y<y0)y0=y; if(y>y1)y1=y; }
    *w=n?x1-x0+1:0; *h=n?y1-y0+1:0; return n; }
static void st_gest(const int*p,int np){ ui_touch(TE_DOWN,p[0],p[1],sim_now); for(int i=1;i<np;i++){ sim_now+=16; ui_touch(TE_MOVE,p[2*i],p[2*i+1],sim_now); } ui_touch(TE_UP,p[2*np-2],p[2*np-1],sim_now); }
static void st_settle(void){ sim_run(400); }
/* ---- v6.15: the graph resets when an activity starts (display only) ---- */
static ms_t g_oldest(void){ return g_n?g_t[(g_head+GN-g_n)%GN]:-1; }
static int g_maxw(void){ int mx=-100000; for(int i=0;i<g_n;i++){ int v=g_w[(g_head+GN-g_n+i)%GN]; if(v>mx) mx=v; } return mx; }
static void tick_cupgrind(ms_t t){                     /* cup (250 g) on the brew scale, portafilter tared on the grind scale; a PHYSICAL grind start at 10 s */
    reg[39]=2500;
    if(t<10000){ reg[33]=0x0101; reg[38]=0; reg[58]=5; }
    else if(t<19000){ reg[33]=0x0141; reg[38]=(uint16_t)((t-10000)*185/9000); reg[58]=640+(t/113)%40; }
    else { reg[33]=0x0101; reg[38]=187; reg[58]=5; }
}
static void st_graph(void){
    printf("== selftest: v6.15 graph reset on activity start (idle -> active clears the ring + chart filters; a concurrent activity does not) ==\n");
    /* (1) 250 g cup at rest, then a brew started at the machine (no UI frames at all) */
    sim_reset(); cb_late=0; cb_flush0=30000; cb_flush1=36000; sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(19900); st_chart_now();
    int n_idle=g_n, cup=0; for(int i=0;i<g_n;i++) if(g_w[(g_head+GN-g_n+i)%GN]==2500) cup++;
    st_check("at rest with a 250 g cup: the ring holds the idle history incl. the cup (20 s, >= 140 samples of word 39 = 2500), no reset yet",n_idle>=140&&cup==n_idle&&gr.resets==0);
    ms_t ts=sim_t0+20000; int wq0=wq.n, txq0=txq_len;
    sim_run(1100); st_chart_now();
    st_check("1 s after the brew starts: ONE reset; the ring holds only the ~8 samples since the start, none with the cup's 250 g",
             gr.resets==1&&gr.reanchors==0&&g_n>=6&&g_n<=11&&g_oldest()>=ts&&g_maxw()<2000);
    printf("       1 s in: ring %d samples · weight top %d · pressure top %d\n",g_n,cst.wtop,cst.top);
    st_check("… the y-autoscale is recomputed from the fresh ring: weight axis top = the 10 g floor x 1.08 (108), not 250 g x 1.08 (2700); pressure top from the plateau (< 600)",
             cst.active&&cst.mode==0&&cst.wtop>=100&&cst.wtop<=110&&cst.top>=400&&cst.top<600);
    sim_run(12000); st_chart_now();                                               /* t = 33.0 s: wand flush (0x5327) during the extraction */
    st_check("a wand flush DURING the brew (secondary, concurrent): NO reset — the brew curve is kept (oldest sample = the brew start, ring keeps growing)",
             hm.conc&&act.ph==PH_EXT&&gr.resets==1&&gr.reanchors==0&&g_oldest()>=ts&&g_oldest()<ts+150&&g_n>=90);
    sim_run(10500); st_chart_now();                                               /* t = 43.5 s: ~35 g, flush over */
    { int pct=cst.wtop?(int)(wdb(g_lastf(g_wd))*100/cst.wtop):0, tare_ok=1;
      for(int i=0;i<3;i++){ int v=g_w[(g_head+GN-g_n+i)%GN]; if(v<-5||v>5) tare_ok=0; }
      printf("       43.5 s: weight now %.0f, max %d (0.1 g) · axis top %d · curve height %d %%\n",(double)g_lastf(g_wd),cst.wmax,cst.wtop,pct);
      st_check("0 -> 35 g renders at FULL SCALE: the weight series starts at the tare (first samples -0.3..0 g) and the line's current point is at >= 85 % of the weight axis",
               tare_ok&&cst.wmax>=330&&cst.wmax<=360&&pct>=85&&gr.resets==1);
      st_check("… display only: word 39 is raw (~350), no frame was queued or sent by the reset, the brew phase is untouched",
               abs((int)reg[39]-350)<=3&&wq.n==wq0&&txq_len==txq0&&act.ph==PH_EXT);
      cd.raw=1; st_chart_now(); int rawmax=cst.wmax==g_maxw(); cd.raw=0; st_chart_now();
      st_check("… DIAGNOSE → GRAPH = RAW still shows the RAW (unconditioned) series of the same post-start ring",rawmax&&!cd.raw);
      /* the same session with v6.14's behaviour (no reset): the bug */
      gr_off=1; sim_reset(); cb_late=0; cb_flush0=cb_flush1=0; sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(43500); st_chart_now(); gr_off=0;
      int pct14=cst.wtop?(int)(wdb(g_lastf(g_wd))*100/cst.wtop):0;
      printf("       v6.14 behaviour: weight max %d · axis top %d · curve height %d %%\n",cst.wmax,cst.wtop,pct14);
      st_check("… vs v6.14 (no reset): the cup step (250 g) sets the weight axis (top >= 2700) and the 35 g point sits at < 15 % of the height — the user's flat curve",
               cst.wtop>=2700&&pct14<15); }
    /* (2) the brew ends -> DONE; the next activity (water) starts within the DONE screen: a second reset */
    sim_reset(); cb_flush0=cb_flush1=0; sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(46000);
    { int done=act.ph==PH_DONE; ms_t tw=sim_now; sim_tick=tick_water; sim_t0=sim_now; sim_run(2000); st_chart_now();
      st_check("DONE -> the next activity (water) 2 s later: a second reset; the ring starts at the water start (no brew/cup samples)",
               done&&act.ph==PH_WATER&&gr.resets==2&&g_oldest()>=tw+300&&g_maxw()<100); }
    /* (3) the tare lands late (word 39 stays 250 g for 300 ms after the bits): re-anchored once */
    sim_reset(); cb_late=300; sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(21500); st_chart_now(); cb_late=0;
    st_check("late tare (+300 ms): ONE re-anchor within 1.5 s of the start; no 250 g sample survives, weight axis top 108",
             gr.resets==1&&gr.reanchors==1&&g_maxw()<2000&&cst.wtop<=110&&g_oldest()>=sim_t0+20300);
    /* (4) a link gap during the brew is not an idle -> active transition */
    sim_reset(); sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(30000); ms_t o0=g_oldest(); sim_link=0; sim_run(2500); int off=phase_main(sim_now)==PH_OFFLINE; sim_link=1; sim_run(1500);
    st_check("OFFLINE for 2.5 s during the extraction, then back: no reset (the curve since the brew start is kept)",off&&gr.resets==1&&g_oldest()==o0&&act.ph==PH_EXT);
    /* (5) a PHYSICAL grind start with a cup on the brew scale: the source latches to word 38 after 300 ms -> re-anchored */
    sim_reset(); sim_tick=tick_cupgrind; sim_t0=sim_now; sim_run(14000); st_chart_now();
    { int no39=1; for(int i=0;i<g_n;i++) if(g_w[(g_head+GN-g_n+i)%GN]==2500) no39=0;
      printf("       grind 4 s in: ring %d · reanchors %d · top %d (target %d)\n",g_n,gr.reanchors,cst.top,sv(S_GRINDW));
      st_check("physical grind start (cup on the brew scale): reset + ONE re-anchor at the 300 ms source latch; only word 38 in the ring; grind scale = target x 1.08 (not the cup)",
               act.ph==PH_GRIND&&cst.mode==1&&gr.resets==1&&gr.reanchors==1&&no39&&cst.top<=sv(S_GRINDW)*1.2f&&wsl.src==38); }
    /* (6) steam alone is a main activity (idle -> steam = reset); a brew started DURING steam is not idle -> active (no reset) */
    sim_reset(); sim_tick=NULL; sim_run(5000); reg[33]=0x4121; reg[24]=0; sim_run(1000); int r1=gr.resets; ms_t os=g_oldest();
    reg[33]=0x5327; sim_run(2000);
    st_check("idle -> steam: reset (the milk curve starts at the steam start); brew started during steam: no second reset, the steam samples stay",
             r1==1&&gr.resets==1&&g_oldest()==os&&phase_main(sim_now)==PH_EXT);
    sim_reset();
}
static void st_v611(void){
    printf("== selftest: v6.11 UX review (touch targets >= 44/48 px measured via hit_test, contrast, swipe guard, close chevron, gear, empty-state hint, plain language, DIAGNOSE groups) ==\n");
    char b[160]; int w,h,ok;
    /* ---- (1) touch targets: measured touch area (width x height) ---- */
    sim_reset(); sim_run(400);
    ok=1; for(int i=0;i<4;i++){ st_hitbox(T_ACT0+i,&w,&h); printf("       HOME icon %d: %d x %d px\n",i,w,h); ok&=w>=48&&h>=48; }
    st_check("touch: the four activity icons each have a touch area >= 48 x 48 px (circle r 30 = 61 px; the drawn icon is 50 px)",ok);
    st_hitbox(T_GEAR,&w,&h); printf("       HOME gear: %d x %d px\n",w,h);
    st_check("touch: the gear (settings) has a touch area >= 44 x 44 px, without overlapping the icons",w>=44&&h>=44&&hit_test(ACT_X[1],ACT_Y+28,sim_now)==T_ACT1&&hit_test(CX,GEAR_Y,sim_now)==T_GEAR);
    page=PG_BREW; static_dirty=1; sim_run(300);
    ok=1; for(int k=0;k<3;k++){ st_hitbox(T_ROW0+k,&w,&h); printf("       BREW row %d: %d x %d px\n",k,w,h); ok&=w>=300&&h>=48; }
    st_check("touch: list rows have a touch area >= 48 px high (50 px row + 4 px above/below) and >= 300 px wide",ok);
    activate(T_ROW0+1,sim_now); sim_run(400);
    { int r[6]={T_MINUS,T_PLUS,T_CANCEL,T_SAVE,T_BACK,0}; const char*nm[5]={"-","+","CANCEL","SAVE","close chevron"}; ok=1;
      for(int i=0;i<5;i++){ st_hitbox(r[i],&w,&h); printf("       detail %s: %d x %d px\n",nm[i],w,h); ok&=w>=(i==4?44:48)&&h>=(i==4?44:48); }
      st_check("touch: detail −/+ (circle r 42), CANCEL/SAVE (136 x 64) >= 48 px; the close chevron >= 44 px",ok); }
    detail_close(sim_now); sim_run(400);
    page=PG_DIAG; scroll[PG_DIAG]=DG_GRAPH-LIST_VY0-60; static_dirty=1; sim_run(300); st_hitbox(T_GRAPH,&w,&h); printf("       DIAGNOSE GRAPH pill: %d x %d px\n",w,h);
    st_check("touch: the DIAGNOSE GRAPH pill has a touch area >= 44 px high (v6.10: 40)",w>=280&&h>=44);
    /* ---- (2) contrast (WCAG 2.x) ---- */
    { uint32_t bg=bgpure[CY*W+CX], bge=bgpure[(CY+150)*W+CX]; float c3[3]={st_cr(C_TXT3,bg),st_cr(C_TXT3,C_SURF),st_cr(C_TXT3,C_SURF2)}, c2[3]={st_cr(C_TXT2,bg),st_cr(C_TXT2,C_SURF),st_cr(C_TXT2,C_SURF2)};
      float d=st_cr(C_DIS,bg), o3=st_cr(0x00686F7Eu,bg);
      printf("       C_TXT3: %.2f / %.2f / %.2f (background / SURF / SURF2; v6.10's 104,111,126: %.2f on background) · C_TXT2: %.2f / %.2f / %.2f · C_DIS (disabled): %.2f · background edge %.2f\n",c3[0],c3[1],c3[2],o3,c2[0],c2[1],c2[2],d,st_cr(C_TXT3,bge));
      st_check("contrast: small captions (C_TXT3) >= 4.5:1 on background, SURF and SURF2 (list rows); C_TXT2 >= 7:1 on background and SURF, >= 6:1 on SURF2",c3[0]>=4.5f&&c3[1]>=4.5f&&c3[2]>=4.5f&&st_cr(C_TXT3,bge)>=4.5f&&c2[0]>=7.f&&c2[1]>=7.f&&c2[2]>=6.f);
      st_check("contrast: disabled (C_DIS) is visibly weaker than C_TXT3, but >= 3:1 (large glyphs: icon, stepper sign, offline hero)",d>=3.f&&d<c3[0]-1.f&&o3<4.5f); }
    /* ---- (3) swipe guard (v6.17: the only horizontal gesture is "swipe right = back") ---- */
    sim_reset(); page=PG_BREW; static_dirty=1; sim_run(300);
    { int g[]={240,200, 262,205, 310,245}; st_gest(g,3); st_settle(); int a=page==PG_BREW;                    /* diagonal: dx 70, dy 45 (~33°) */
      int g2[]={240,200, 222,203, 140,210}; st_gest(g2,3); st_settle(); int b2=page==PG_BREW;                 /* horizontal LEFT: dx -100, dy 10 -> nothing (no carousel) */
      float s0=scroll[PG_BREW];
      int g3[]={240,200, 243,220, 330,250}; st_gest(g3,3); st_settle(); int c=page==PG_BREW;                  /* starts vertical (scroll), ends horizontal right */
      scroll[PG_BREW]=0; int g5[]={140,200, 158,203, 240,210}; st_gest(g5,3); st_settle(); int d=page==PG_HOME;   /* horizontal RIGHT: dx 100, dy 10 -> back */
      printf("       diagonal 70/45: page %s · left -100/10: %s · vertical start + horizontal end: page %s (scroll %.0f -> %.0f) · right 100/10: %s\n",a?"unchanged":"CHANGED",b2?"nothing":"PAGE CHANGED",c?"unchanged":"CHANGED",(double)s0,(double)scroll[PG_BREW],d?"back to HOME":"NOT BACK");
      st_check("swipe guard: a diagonal drag (dx 70, dy 45) on a page does nothing; a horizontal swipe LEFT does nothing (v6.17: no carousel); a horizontal swipe RIGHT (dx 100, dy 10) goes back to HOME",a&&b2&&d);
      st_check("swipe guard: a gesture that starts vertical (scroll) never closes the page, even if it ends horizontal",c);
      sim_reset(); sim_run(300); int g4[]={240,200, 242,222, 120,236}; st_gest(g4,3); st_settle(); int h1=page==PG_HOME&&!tr.type;
      int g6[]={140,200, 160,202, 260,206}; st_gest(g6,3); st_settle(); int g7[]={300,200, 280,202, 180,206}; st_gest(g7,3); st_settle();
      st_check("swipe guard: HOME reacts to no swipe at all (left, right, vertical start) — everything on HOME is a visible button",h1&&page==PG_HOME&&!tr.type&&view==V_MAIN); }
    /* ---- (4) close chevron + swipe down ---- */
    sim_reset(); page=PG_BREW; static_dirty=1; sim_run(300); activate(T_ROW0+1,sim_now); sim_run(400);
    { int i=sel, v0=SET[i].val; tap(396,184); sim_run(10); int chg=edit_val!=v0, ht=hit_test(CX,57,sim_now);
      tap(CX,57); sim_run(400); int a=view==V_MAIN&&SET[i].val==v0&&!sav.active;
      printf("       chevron: sel %d v0 %d edit %d chg %d hit %d view %d val %d\n",i,v0,edit_val,chg,ht,view,SET[i].val);
      activate(T_ROW0+1,sim_now); sim_run(400); int g[]={240,250, 242,268, 246,340}; st_gest(g,3); sim_run(400); int c=view==V_MAIN;
      activate(T_ROW0+1,sim_now); sim_run(400); int e0=edit_val; int g2[]={84,184, 86,204, 90,280}; st_gest(g2,3); sim_run(400); int d=view==V_DETAIL&&edit_val==e0-SET[i].step;
      int g3[]={240,250, 262,268, 310,320}; st_gest(g3,3); sim_run(400); int e=view==V_DETAIL;                      /* diagonal (45°) -> do not close */
      int g8[]={150,250, 170,252, 260,256}; st_gest(g8,3); sim_run(400); int f=view==V_MAIN&&page==PG_BREW&&SET[i].val==v0;   /* v6.17: swipe right = back (to the page the editor was opened from) */
      st_check("close chevron: a tap at the top (240,57) closes the detail like CANCEL — a changed value is NOT saved",chg&&a);
      st_check("swipe down (>= 70 px, vertical) closes the detail; started on − it does not close (the press has already counted a step); diagonal does not close; v6.17: swipe right closes it too (nothing saved)",c&&d&&e&&f); }
    sim_reset(); sys_open(X_BOOTUI,sim_now); sim_run(400); tap(CX,57); sim_run(400);
    st_check("the close chevron also works in the system view (BOOT UI): back to the list, the flag untouched",view==V_MAIN&&xsel==-1);
    /* ---- (5) gear ---- */
    sim_reset(); sim_run(400); { int n0=wq.n; int q0=txq_len; tap(CX,GEAR_Y); st_settle(); int a=page==PG_SYS&&wq.n==n0&&txq_len==q0;
      int e1=hit_test(20,240,sim_now)==T_NONE&&hit_test(460,240,sim_now)==T_NONE; tap(20,240); st_settle(); tap(460,240); st_settle(); int c=page==PG_SYS;
      st_check("the gear on HOME opens SYSTEM (no frame); v6.17: there are no invisible edge zones any more (a tap at the left / right edge does nothing)",a&&e1&&c); }
    /* ---- (6) empty-state hint + subline ---- */
    sim_reset(); sim_run(1500); home_model(sim_now);
    { int a=!strcmp(hm.hint1,TR(TX_C_HINT))&&hm.hint2==NULL&&!cst.active; char t[40]; fmt_t10(t,sizeof t,870,1); snprintf(b,sizeof b,TR(TX_H_SET_TO),t);
      int s1=!strcmp(hm.sub,b)&&sub_font(&hm)==F_S;
      reg[35]=1; sim_run(400); home_model(sim_now); int e=act.ph==PH_ERROR&&strstr(hm.hint1,"E2")&&!strstr(hm.sub,"E2")&&!strcmp(hm.sub,TR(TX_ER_E2_BREW)); reg[35]=0;
      sim_link=0; sim_run(3000); home_model(sim_now); int o=act.ph==PH_OFFLINE&&!strcmp(hm.hint1,TR(TX_C_DIAGREF))&&!strcmp(hm.sub,TR(TX_H_NORESP)); sim_link=1; sim_run(400);
      reg[1]=1; sim_run(400); home_model(sim_now); int z=act.ph==PH_SLEEP&&!hm.hint1[0]&&!strcmp(hm.sub,TR(TX_H_WAKING)); reg[1]=0; sim_run(400);
      printf("       ready: '%s' · subline '%s' (F_S %d) · error '%s' / '%s' · offline '%s' / '%s'\n",TR(TX_C_HINT),b,s1,TR(TX_ER_E2_BREW),TR(TX_C_ERRCODE),TR(TX_H_NORESP),TR(TX_C_DIAGREF));
      st_check("HOME idle: the chart field says what you can DO in ONE line ('HOLD = START · TAP = SETTINGS'; v6.11-v6.16: two lines) instead of 'NO ACTIVITY'; the subline is 'TARGET 87.0°' in F_S",a&&s1);
      st_check("error: the subline is plain language (no E code); the code is shown dimmed in the chart field with a reference to DIAGNOSE · offline: 'NO CONNECTION TO THE MACHINE' + DIAGNOSE reference · asleep: 'WAKING UP…'",e&&o&&z); }
    { sim_reset(); reg[33]=0x1307; sim_run(1500); home_model(sim_now); int a=cst.active;
      st_check("the hint is shown only in the empty chart (during a brew the curve is drawn as before)",a&&act.ph==PH_EXT); }
    /* ---- (7) plain language: no word numbers / raw codes in user-facing strings (only DIAGNOSE and console) ---- */
    ok=1; for(int i=0;i<NSTR;i++){ int k=STR_K[i]; if(k==KDL||k==KDV||k==KDF||k==KCON||!strncmp(STR_ID[i],"TX_DL_",6)||!strncmp(STR_ID[i],"TX_DV_",6)||!strncmp(STR_ID[i],"TX_DG_",6)) continue;
        for(int lg=0;lg<NLANG;lg++){ const char*s=STR[lg][i]; if(!s) continue;
            for(const char*p=s;(p=strstr(p,lg==LANG_DA?"ORD ":"WORD "));p++){ const char*q=p+(lg==LANG_DA?4:5); if(isdigit((unsigned char)*q)&&(p==s||!isalpha((unsigned char)p[-1]))){ ok=0; printf("     [%s] %s: '%s'\n",LANG_CODE[lg],STR_ID[i],s); break; } }
            if(strstr(s," ACK")||strstr(s,"FRAME-POLI")||strstr(s,"FRAME POLI")||strstr(s,"WRITE-K")||strstr(s,"WRITE QUEUE")){ ok=0; printf("     [%s] %s: '%s'\n",LANG_CODE[lg],STR_ID[i],s); } } }
    st_check("plain language: no user-facing string (HOME, toasts, notes, footers) mentions word numbers, ACK, frame policy or write queue — they appear only on DIAGNOSE",ok);
    /* ---- (8) DIAGNOSE groups ---- */
    { int perm[DG_N]={0}, okp=1; for(int i=0;i<DG_N;i++){ if(DG_SLOT[i]<0||DG_SLOT[i]>=DG_N||perm[DG_SLOT[i]]++) okp=0; }
      static const int st[]={0,1,5,7,8,13,14,15}; int s8=1; for(int i=0;i<8;i++) s8&=DG_SLOT[st[i]]<8;
      int order=DG_Y0+3*DG_P+30<DG_BOOT-28&&DG_GRAPH<DG_PRESS&&DG_PRESS+28+DG_P+30<DG_RAW-11&&DG_RAW+28+3*DG_P+30<DG_ABOUT&&dg_cell_y(2)>DG_RAW&&dg_cell_y(12)>DG_RAW;
      printf("       STATUS y %d..%d · BOOT %d · GRAPH %d · PRESSURE %d · RAW COUNTERS %d · ABOUT %d · bottom %d\n",DG_Y0,DG_Y0+3*DG_P,DG_BOOT,DG_GRAPH,DG_PRESS,DG_RAW,DG_ABOUT,DG_BOTTOM);
      st_check("DIAGNOSE: STATUS at the top (link, phase, E1/E2/E3, awake, net, version, boot push), then boot/net/graph/pressure, RAW COUNTERS (rx, crc, work_sta, cmd6, current, frame, mirror, md5) in their own section, ABOUT at the bottom; every cell has exactly one slot",okp&&s8&&order); }
    sim_reset(); sim_tick=NULL;
}
/* ============================ v6.17: IA consolidation (docs/ux-audit-v617.md) ============================ */
static int st_q_snap(char*o,size_t n){   /* the write queue as hex, frame by frame (what WILL go out) */
    size_t l=0; o[0]=0;
    for(int k=0;k<wq.n;k++){ uint8_t b[80]; int m=build_write(b,wq.q[k].sub,wq.q[k].w,wq.q[k].len); for(int i=0;i<m&&l+4<n;i++) l+=(size_t)snprintf(o+l,n-l,"%02x",b[i]); if(l+2<n){ o[l++]='|'; o[l]=0; } }
    return wq.n;
}
static int st_hold(int x,int y,ms_t ms){   /* a real hold: DOWN, frames for ms (the write queue is NOT stepped, so what was queued stays), UP */
    ms_t t=sim_now; ui_touch(TE_DOWN,x,y,t); for(ms_t e=t+ms;t<e;t+=11){ if(sim_link) last_rx_ms=t; ui_frame(t); } int f=tch.fired; ui_touch(TE_UP,x,y,t); sim_now=t; return f;
}
/* ============================ v6.18: sound (start jingle + per-boiler ready tones; docs/audio.md) ============================ */
static int st_own_save(int i,int val,ms_t t);
static int st_sn(int ev){ return snd.n[ev]; }
static int st_sp(int ev){ return snd.n_played[ev]; }
static int st_slast(int ev,int out){ return snd.log_n&&snd.log[0].ev==ev&&snd.log[0].out==out; }
static void st_v618(const char*dir){
    printf("== selftest: v6.18 sound (stock: deal_play 0x13725C, page_logo power.mp4, preheatCompele once per power-on when words 62 AND 63 > 99; MCU boiler_heat 0x3502 heat stages) ==\n");
    char keep_sp[256]; snprintf(keep_sp,sizeof keep_sp,"%s",snd_path); snprintf(snd_path,sizeof snd_path,"%s/sound",dir); unlink(snd_path);
    int tx0, sq0;
    /* ---- (1) settings: own file, DISPLAY · SOUND section, volume = stock config ---- */
    settings_load();
    st_check("SOUND (OFF / ON / STOCK SOUNDS) and READY TONE (OFF / BREW BOILER / STEAM BOILER / BOTH BOILERS) are merakiplus's own (SF_SND): no config key, no MCU word; defaults ON + BOTH",
             !SET[S_SOUND].key&&!SET[S_SNDRDY].key&&SET[S_SOUND].mcu==M_NONE&&SET[S_SNDRDY].mcu==M_NONE&&(SET[S_SOUND].flags&SF_SND)&&(SET[S_SNDRDY].flags&SF_SND)
             &&SET[S_SOUND].val==1&&SET[S_SNDRDY].val==3&&sndc.src==SCS_DEFAULT&&SET[S_SNDRDY].max==3&&SET[S_SOUND].max==2);
    st_check("DISPLAY · SOUND holds UI LANGUAGE, BRIGHTNESS, TEMP. UNIT, SOUND, READY TONE, VOLUME (VOLUME moved from STOCK UI: it sets Meraki+'s sounds too; still config \"volume\")",
             sec_of_set(S_SOUND)==SEC_DISPLAY&&sec_of_set(S_SNDRDY)==SEC_DISPLAY&&sec_of_set(S_VOL)==SEC_DISPLAY&&!strcmp(SET[S_VOL].key,"volume")&&SEC_ROWS[SEC_DISPLAY][3]==S_SOUND&&SEC_ROWS[SEC_DISPLAY][5]==S_VOL);
    { sim_reset(); int a=st_own_save(S_SOUND,0,sim_now); char rb[96]; int fd=open(snd_path,O_RDONLY); int rn=fd>=0?(int)read(fd,rb,sizeof rb-1):-1; if(fd>=0) close(fd); if(rn<0) rn=0; rb[rn]=0;
      int b=st_own_save(S_SNDRDY,1,sim_now); fd=open(snd_path,O_RDONLY); char rb2[96]; int rn2=fd>=0?(int)read(fd,rb2,sizeof rb2-1):-1; if(fd>=0) close(fd); if(rn2<0) rn2=0; rb2[rn2]=0;
      char tp[300]; snprintf(tp,sizeof tp,"%s.tmp",snd_path);
      st_check("SAVE writes the sound file atomically: \"sound=off\\nready=both\\n\", then \"sound=off\\nready=brew\\n\"; toast SOUND SAVED; no tmp file left",
               a&&b&&!strcmp(rb,"sound=off\nready=both\n")&&!strcmp(rb2,"sound=off\nready=brew\n")&&access(tp,F_OK)!=0&&!strcmp(toast.msg,TR(TX_T_SND_SAVED)));
      settings_load(); int c=SET[S_SOUND].val==0&&SET[S_SNDRDY].val==1&&sndc.src==SCS_FILE;
      put_bytes(snd_path,"sound=loud\nready=7\nvolume=99\n",28); settings_load(); int d=SET[S_SOUND].val==1&&SET[S_SNDRDY].val==3;
      put_bytes(snd_path,"sound=stock\nready=steam\n",24); settings_load(); int e=SET[S_SOUND].val==2&&SET[S_SNDRDY].val==2;
      unlink(snd_path); settings_load(); int f=SET[S_SOUND].val==1&&SET[S_SNDRDY].val==3&&sndc.src==SCS_DEFAULT;
      st_check("read back after a restart; unknown values -> the default; \"stock\"/\"steam\" parse; no file -> ON + BOTH",c&&d&&e&&f); }
    /* ---- (2) a cold boot: jingle once, then one tone per boiler as each reaches its heat stage 100 ---- */
    sim_reset(); snd_uptime_ovr=25; reg[62]=30; reg[63]=0; reg[40]=520; reg[47]=310; tx0=txq_len; sq0=(int)wq.seq;
    sim_run(1500);
    st_check("cold boot of the display (uptime 25 s < 120 s): the START JINGLE plays once on the first \"on\"; cold boilers (stage 30 / 0) -> no ready tone",
             st_sn(SE_JINGLE)==1&&st_sp(SE_JINGLE)==1&&st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0&&snd.armed[0]&&snd.armed[1]&&!strcmp(snd.last_file,SND_OURS[SE_JINGLE]));
    reg[62]=60; reg[40]=850; sim_run(600); int e60=st_sn(SE_READY_E)==0;
    reg[62]=100; reg[40]=871; sim_run(300);
    st_check("EXTRACT boiler: stage 60 (60 °C .. set temp) -> nothing; stage 100 (MCU: e boiler >= word 11) -> its OWN tone once, the steam boiler still heating",
             e60&&st_sp(SE_READY_E)==1&&st_sn(SE_READY_S)==0&&st_slast(SE_READY_E,SO_PLAYED)&&!strcmp(snd.last_file,SND_OURS[SE_READY_E]));
    reg[63]=60; sim_run(600); int s60=st_sn(SE_READY_S)==0; reg[63]=100; reg[47]=1255; sim_run(300);
    st_check("STEAM boiler: stage 100 (MCU: s boiler >= steam target - 10 °C) -> a DIFFERENT tone (ready_steam.wav vs ready_extract.wav), once",
             s60&&st_sp(SE_READY_S)==1&&st_slast(SE_READY_S,SO_PLAYED)&&!strcmp(snd.last_file,SND_OURS[SE_READY_S])&&strcmp(SND_OURS[SE_READY_S],SND_OURS[SE_READY_E])&&strcmp(SND_OURS[SE_JINGLE],SND_OURS[SE_READY_S]));
    /* ---- (3) no spam: brew/steam dips, link glitches ---- */
    for(int k=0;k<6;k++){ reg[62]=60; reg[40]=835; reg[63]=60; reg[47]=1215; sim_run(400); reg[62]=100; reg[40]=872; reg[63]=100; reg[47]=1262; sim_run(400); }
    st_check("no spam: six dips to stage 60 (3.5 °C below the brew set temp, 4 °C below the steam threshold — a shot, a steam session) re-trigger nothing",
             st_sn(SE_READY_E)==1&&st_sn(SE_READY_S)==1&&!snd.armed[0]&&!snd.armed[1]);
    sim_link=0; last_rx_ms=sim_now-5000; sim_run(3000); int off_held=snd.st==2; sim_link=1; sim_run(800);
    st_check("a link glitch (OFFLINE 3 s) is not a power cycle: no second jingle, nothing re-armed",off_held&&st_sn(SE_JINGLE)==1&&st_sn(SE_READY_E)==1&&!snd.armed[0]);
    /* ---- (4) re-arm only after real cooling ---- */
    reg[62]=60; reg[40]=(uint16_t)(reg[11]-SND_REARM_DROP+5); sim_run(600); int still=!snd.armed[0];
    reg[40]=(uint16_t)(reg[11]-SND_REARM_DROP-5); sim_run(400); int re=snd.armed[0]&&!snd.armed[1];
    reg[62]=100; reg[40]=reg[11]; sim_run(300);
    st_check("re-arm: 14.5 °C below the set temp -> still announced; 15.5 °C below -> re-armed; back at stage 100 -> the extract tone again (2), the steam tone not",
             still&&re&&st_sp(SE_READY_E)==2&&st_sp(SE_READY_S)==1);
    reg[63]=30; sim_run(300); int sre=snd.armed[1]; reg[63]=100; sim_run(300);
    st_check("re-arm by the MCU stage: steam stage <= 30 (< 80 °C) -> re-armed; stage 100 -> the steam tone again",sre&&st_sp(SE_READY_S)==2);
    st_check("the jingle played exactly ONCE across all of the above, and no audio event queued a UART frame (txq + write queue unchanged)",
             st_sn(SE_JINGLE)==1&&txq_len==tx0&&(int)wq.seq==sq0);
    /* ---- (5) off / sleep: silent; power-on: jingle + both tones (stock: the logo latch resets at every power-on) ---- */
    { int n0=snd.n[SE_JINGLE]+snd.n[SE_READY_E]+snd.n[SE_READY_S];
      reg[1]=1; reg[33]=0x0001; sim_run(800); int sl=snd.st==1&&snd.armed[0]&&snd.armed[1];
      reg[62]=30; reg[63]=30; sim_run(500); reg[62]=100; reg[63]=100; sim_run(500);
      int quiet=snd.n[SE_JINGLE]+snd.n[SE_READY_E]+snd.n[SE_READY_S]==n0&&snd.qn==0;
      reg[1]=0; reg[33]=0x0101; sim_run(SND_SETTLE_MS+800);
      st_check("SLEEP: nothing plays (heat stages change while asleep: no event); WAKE = power-on: the jingle again — and (v6.19) NO ready tone, both boilers are already hot",
               sl&&quiet&&st_sn(SE_JINGLE)==2&&st_sn(SE_READY_E)==2&&st_sn(SE_READY_S)==2&&snd.log[0].ev==SE_JINGLE&&!snd.armed[0]&&!snd.armed[1]); }
    { pw.state=PW_OFF; view=V_OFF; sim_link=0; reg[0]=0; last_rx_ms=sim_now-5000; sim_run(600);   /* OFF: the MCU sleeps and does not reply */
      int n0=snd.n[SE_JINGLE], ne0=snd.n[SE_READY_E]; reg[62]=30; sim_run(300); reg[62]=100; sim_run(300);
      int quiet=snd.n[SE_JINGLE]==n0&&snd.n[SE_READY_E]==ne0&&snd.st==1;
      int n1=st_sn(SE_READY_E)+st_sn(SE_READY_S);
      sim_link=1; reg[0]=1; reg[62]=60; sim_run(2500);                         /* the controller answers with word 0 = 1 -> merakiplus powers on (pw_step); extract still heating */
      int j3=st_sn(SE_JINGLE)==3&&st_sn(SE_READY_E)+st_sn(SE_READY_S)==n1&&snd.armed[0]&&!snd.armed[1];
      reg[62]=100; sim_run(300);
      st_check("the UI's OFF (power button): silent while OFF; ON again -> the jingle (3rd power-on) only; the extract boiler (stage 60 at the power-on) heats up -> its tone, the hot steam boiler stays quiet",
               quiet&&j3&&st_sp(SE_READY_E)==3&&st_sn(SE_READY_S)==2); }
    /* ---- (6) a Meraki+ start on a machine that is already on (switch from the stock UI): no jingle, hot boilers count as announced ---- */
    sim_reset(); sim_run(1500);
    st_check("Meraki+ started on a running machine (uptime 1 day): no jingle, both boilers already hot -> no ready tone",st_sn(SE_JINGLE)==0&&st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0&&!snd.armed[0]&&!snd.armed[1]);
    sim_reset(); reg[63]=60; reg[47]=1100; sim_run(2200); reg[63]=100; reg[47]=1300; sim_run(300);
    st_check("... a boiler still heating then is armed: its tone plays when it gets hot (steam), the hot one stays quiet",st_sn(SE_JINGLE)==0&&st_sp(SE_READY_S)==1&&st_sn(SE_READY_E)==0);
    sim_reset(); reg[62]=0; reg[63]=0; sim_run(500); reg[62]=100; reg[63]=100; sim_run(2500);
    st_check("... stale first samples (words 62/63 = 0 for 0.5 s, then 100) inside the 1.5 s settle window do not count as \"heated up\": no tone",
             st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0&&!snd.settle&&!snd.armed[0]&&!snd.armed[1]);
    /* ---- (7) fallback thresholds when words 62/63 were never received ---- */
    sim_reset(); snd_uptime_ovr=10; reg_seen[62]=reg_seen[63]=0; reg[11]=900; reg[40]=880; reg[14]=1; reg[47]=1050; sim_run(SND_SETTLE_MS+800);
    int f0=st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0;
    reg[40]=900; sim_run(300); int f1=st_sp(SE_READY_E)==1;
    reg[47]=1099; sim_run(300); int f2=st_sn(SE_READY_S)==0; reg[47]=1100; sim_run(300); int f3=st_sp(SE_READY_S)==1;
    reg[14]=4; reg[40]=900-SND_REARM_DROP-1; sim_run(300); reg[40]=900; sim_run(300);
    st_check("fallback without words 62/63: extract = word 40 >= word 11 (88.0 < 90.0 -> no, 90.0 -> tone); steam = word 47 >= gear target - 10 °C (gear 1: 110.0 °C)",
             f0&&f1&&f2&&f3&&snd_steam_thr()==1250&&st_sp(SE_READY_E)==2);
    /* ---- (8) mute, off, per boiler, volume cap, stock files ---- */
    sim_reset(); snd_uptime_ovr=10; SET[S_VOL].val=0; reg[62]=reg[63]=30; sim_run(SND_SETTLE_MS+800); reg[62]=reg[63]=100; sim_run(400);
    st_check("config volume 0 = mute (stock: 0 -> MI_AO mute): jingle + both tones are logged \"muted\", nothing is played",
             st_sn(SE_JINGLE)==1&&st_sp(SE_JINGLE)==0&&snd.n_out[SO_MUTED]==3&&st_sp(SE_READY_E)==0&&st_sp(SE_READY_S)==0);
    sim_reset(); snd_uptime_ovr=10; SET[S_SOUND].val=0; reg[62]=reg[63]=30; sim_run(SND_SETTLE_MS+800); reg[62]=reg[63]=100; sim_run(400);
    st_check("SOUND OFF: no jingle, no tone (logged \"off\")",snd.n_out[SO_OFF]==3&&snd.n_out[SO_PLAYED]==0);
    sim_reset(); snd_uptime_ovr=10; SET[S_SNDRDY].val=1; reg[62]=reg[63]=30; sim_run(SND_SETTLE_MS+800); reg[62]=reg[63]=100; sim_run(400);
    int pb=st_sp(SE_JINGLE)==1&&st_sp(SE_READY_E)==1&&st_sn(SE_READY_S)==1&&st_sp(SE_READY_S)==0;
    sim_reset(); snd_uptime_ovr=10; SET[S_SNDRDY].val=2; reg[62]=reg[63]=30; sim_run(SND_SETTLE_MS+800); reg[62]=reg[63]=100; sim_run(400);
    st_check("READY TONE per boiler: BREW BOILER -> only the extract tone; STEAM BOILER -> only the steam tone; the jingle is independent",pb&&st_sp(SE_READY_E)==0&&st_sp(SE_READY_S)==1&&st_sp(SE_JINGLE)==1);
    sim_reset(); SET[S_VOL].val=100; int c1=snd_vol()==SND_VOL_CAP; SET[S_VOL].val=40; int c2=snd_vol()==40; SET[S_VOL].loaded=0; int c3=snd_vol()==SET[S_VOL].def;
    st_check("volume = config \"volume\" capped at 70 like stock deal_play (100 -> 70, 40 -> 40; no config -> the default 40)",c1&&c2&&c3);
    sim_reset(); SET[S_SOUND].val=2; int s1=!strcmp(snd_file(SE_JINGLE),SND_OURS[SE_JINGLE]);
    { char d1[400],d2[400]; snprintf(d1,sizeof d1,"%s/customer",sysroot); mkdir(d1,0755); snprintf(d2,sizeof d2,"%s/customer/audio",sysroot); mkdir(d2,0755);
      put_bytes(rp(SND_STOCK[SE_JINGLE]),"ID3",3); int s2=!strcmp(snd_file(SE_JINGLE),SND_STOCK[SE_JINGLE]); unlink(rp(SND_STOCK[SE_JINGLE]));
      st_check("STOCK SOUNDS: the stock UI's own /customer/audio/*.mp3 when present (never shipped), else Meraki+'s tones",s1&&s2&&!strcmp(SND_STOCK[SE_READY_E],"/customer/audio/preheatCompele.mp3")); }
    /* ---- (9) activity: a tone that comes due while something runs is dropped (stock: no sound during its animations) ---- */
    sim_reset(); snd_uptime_ovr=10; reg[63]=60; sim_run(SND_SETTLE_MS+800); reg[33]=0x1307; reg[63]=100; sim_run(400); int sup=snd.n_out[SO_SUPPRESSED]==1&&st_sp(SE_READY_S)==0;
    reg[33]=0x0101; sim_run(800);
    st_check("brew running when the steam boiler gets hot: the tone is dropped (logged \"suppressed\"), and NOT played late after the shot",sup&&st_sp(SE_READY_S)==0&&!snd.armed[1]);
    /* ---- (10) preview tone on SAVE ---- */
    sim_reset(); snprintf(snd_path,sizeof snd_path,"%s/sound",dir); sim_run(800); int n0=st_sn(SE_PREVIEW); st_own_save(S_SNDRDY,3,sim_now); int pv1=st_sn(SE_PREVIEW)==n0;   /* unchanged value: no save, no tone */
    st_own_save(S_SNDRDY,2,sim_now); int pv2=st_sp(SE_PREVIEW)==n0+1;
    st_own_save(S_SOUND,0,sim_now); int pv3=st_sn(SE_PREVIEW)==n0+2&&st_sp(SE_PREVIEW)==n0+1&&st_slast(SE_PREVIEW,SO_OFF);
    st_check("SAVE of SOUND / READY TONE plays a short preview at the current volume (unless the result is OFF); an unchanged value plays nothing",pv1&&pv2&&pv3);
    /* ---- (11) names + DIAGNOSTICS ---- */
    st_check("event / outcome names (state JSON \"audio\", /tmp/merakiplus_snd.log) = start_jingle, boiler_ready_extract, boiler_ready_steam, preview / played, off, muted, suppressed",
             !strcmp(SE_KEY[SE_JINGLE],"start_jingle")&&!strcmp(SE_KEY[SE_READY_E],"boiler_ready_extract")&&!strcmp(SE_KEY[SE_READY_S],"boiler_ready_steam")&&!strcmp(SE_KEY[SE_PREVIEW],"preview")
             &&!strcmp(SO_KEY[SO_PLAYED],"played")&&!strcmp(SO_KEY[SO_MUTED],"muted")&&!strcmp(SO_KEY[SO_SUPPRESSED],"suppressed")&&!strcmp(SO_KEY[SO_OFF],"off"));
    { unlink(snd_path); sim_reset(); snd_uptime_ovr=20; reg[63]=60; sim_run(SND_SETTLE_MS+800); reg[63]=100; sim_run(400); char m1[40],l1[64],m2[40],l2[64],e1[48],e2[64];
      uint32_t c1=snd_diag_text(m1,sizeof m1,l1,sizeof l1); SET[S_VOL].val=0; uint32_t c2=snd_diag_text(m2,sizeof m2,l2,sizeof l2); SET[S_VOL].val=31;
      snprintf(e1,sizeof e1,TR(TX_DV_SND_ON),31); snprintf(e2,sizeof e2,"%s · %s",TR(TX_DV_SE_STEAM),TR(TX_DV_SO_PLAYED));
      printf("       DIAGNOSTICS SOUND: '%s' | '%s' | muted '%s' (DG_SND %d, ABOUT %d)\n",m1,l1,m2,DG_SND,DG_ABOUT);
      st_check("DIAGNOSTICS › SOUND: mode · volume (\"ON · 31\"; amber \"VOLUME 0\" when muted) and the last event + outcome (\"STEAM · PLAYED\"); the block sits between TIME and ABOUT",
               !strcmp(m1,e1)&&c1==C_TXT&&!strcmp(m2,TR(TX_DV_SND_MUTE))&&c2==C_WARN&&!strcmp(l1,e2)&&DG_SND>DG_TIME+28+2*DG_P+30&&DG_SND+28+DG_P<DG_ABOUT); }
    unlink(snd_path); snprintf(snd_path,sizeof snd_path,"%s",keep_sp); sim_reset(); settings_load();
}
/* ============ v6.19: no ready tone at a power-on with hot boilers (user: jingle + 2 tones back-to-back is redundant) ============ */
static void st_v619(void){
    printf("== selftest: v6.19 boot-already-hot (a boiler at stage 100 at power-on counts as announced; only the jingle plays) ==\n");
    int tx0, sq0;
    /* boot-already-hot: a cold boot of the display, both boilers at stage 100 */
    sim_reset(); snd_uptime_ovr=25; reg[62]=reg[63]=100; tx0=txq_len; sq0=(int)wq.seq; sim_run(SND_SETTLE_MS+1500);
    st_check("boot already hot (uptime 25 s, words 62 = 63 = 100): ONLY the start jingle plays — no ready tone (not even logged); both latches already fired",
             st_sn(SE_JINGLE)==1&&st_sp(SE_JINGLE)==1&&st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0&&!snd.armed[0]&&!snd.armed[1]&&snd.log_n==1&&!snd.settle);
    /* the dips of normal use do not re-arm; real cooling does, and a cold -> hot transition DURING the session plays the tone */
    reg[62]=60; reg[40]=(uint16_t)(reg[11]-40); sim_run(600); reg[62]=100; reg[40]=reg[11]; sim_run(400); int dip=st_sn(SE_READY_E)==0;
    reg[62]=30; sim_run(400); int re=snd.armed[0]&&!snd.armed[1]; reg[62]=60; sim_run(400); reg[62]=100; sim_run(400);
    st_check("... a shot's dip (stage 60) re-arms nothing; real cooling (stage 30) re-arms the extract boiler; cold -> hot during the session -> its tone (once), the steam boiler stays quiet",
             dip&&re&&st_sp(SE_READY_E)==1&&st_sn(SE_READY_S)==0&&st_slast(SE_READY_E,SO_PLAYED));
    reg[63]=0; sim_run(400); reg[63]=100; sim_run(400);
    st_check("... the same for the steam boiler (stage 0 -> 100): its own tone; the jingle never repeats; no UART frame from the sound engine",
             st_sp(SE_READY_S)==1&&st_sn(SE_JINGLE)==1&&txq_len==tx0&&(int)wq.seq==sq0);
    /* cold boot with cold boilers: the tones still come as each boiler heats */
    sim_reset(); snd_uptime_ovr=25; reg[62]=30; reg[63]=0; sim_run(SND_SETTLE_MS+500); int arm=snd.armed[0]&&snd.armed[1]&&st_sn(SE_JINGLE)==1;
    reg[62]=100; sim_run(400); int e=st_sp(SE_READY_E)==1&&st_sn(SE_READY_S)==0; reg[63]=100; sim_run(400);
    st_check("cold boot with cold boilers (stage 30 / 0): jingle, both armed; each boiler's tone when it reaches stage 100 (cold -> hot)",arm&&e&&st_sp(SE_READY_S)==1);
    /* mixed: extract hot, steam still heating at the power-on */
    sim_reset(); snd_uptime_ovr=25; reg[62]=100; reg[63]=60; sim_run(SND_SETTLE_MS+500); int mx=!snd.armed[0]&&snd.armed[1];
    reg[63]=100; sim_run(400);
    st_check("power-on with the extract boiler hot and the steam boiler at stage 60: jingle + only the STEAM tone (when it gets hot)",
             mx&&st_sn(SE_JINGLE)==1&&st_sn(SE_READY_E)==0&&st_sp(SE_READY_S)==1);
    /* stale samples at the power-on: the decision is the LAST sample of the window */
    sim_reset(); snd_uptime_ovr=25; reg[62]=reg[63]=100; sim_run(400); reg[62]=60; sim_run(SND_SETTLE_MS+300); int st1=snd.armed[0]&&!snd.armed[1];
    reg[62]=100; sim_run(400);
    st_check("stale \"hot\" samples from before the power-on (100 for 0.4 s, then the real stage 60) do not latch: armed, tone when really hot",st1&&st_sp(SE_READY_E)==1&&st_sn(SE_READY_S)==0);
    /* sleep -> wake, already hot: no ready spam; heating off while asleep (stages 0) and stale 0s right after the wake do not count */
    sim_reset(); snd_uptime_ovr=25; reg[62]=reg[63]=100; sim_run(SND_SETTLE_MS+500);
    for(int k=0;k<3;k++){
        reg[1]=1; reg[33]=0x0001; reg[62]=reg[63]=0; sim_run(1500);                    /* asleep: heating off -> the MCU writes stage 0 */
        reg[1]=0; reg[33]=0x0101; sim_run(400); reg[62]=reg[63]=100; sim_run(SND_SETTLE_MS+400);   /* awake: 0.4 s of stale 0s, then hot */
    }
    st_check("3 x sleep -> wake with hot boilers (stages 0 while asleep, 0.4 s stale after the wake): 4 jingles (boot + 3 wakes), NO ready tone at all",
             st_sn(SE_JINGLE)==4&&st_sn(SE_READY_E)==0&&st_sn(SE_READY_S)==0&&!snd.armed[0]&&!snd.armed[1]);
    reg[1]=1; reg[33]=0x0001; reg[62]=reg[63]=0; sim_run(1500); reg[1]=0; reg[33]=0x0101; reg[62]=30; reg[63]=60; sim_run(SND_SETTLE_MS+500);
    int cold=snd.armed[0]&&snd.armed[1]; reg[62]=reg[63]=100; sim_run(400);
    st_check("... a long sleep that let both boilers cool (stage 30 / 60 at the wake): armed, and both tones play as they heat back up",cold&&st_sp(SE_READY_E)==1&&st_sp(SE_READY_S)==1&&st_sn(SE_JINGLE)==5);
    sim_reset();
}
/* ============================ v6.20: code review 2026-11 (docs/dev/REVIEW-2026-11.md) ============================ */
static void st_r3(void);
static void st_r5(const char*dir);
static void st_v620(const char*dir){
    printf("== selftest: v6.20 review fixes (stepper repeat after a failed save, ...) ==\n");
    /* R1: a stepper press DURING SAVING, then the save fails (no ACK) while the finger stays down */
    { sim_reset(); wq.sim_ack=0; page=PG_BREW; sim_run(300); detail_open(S_BREWW,sim_now); sim_run(600);
      tap(396,184); sim_run(200); int v0=edit_val; const Setting*s=&SET[S_BREWW];
      tap(310,316); sim_run(100); int saving=sav.active;
      ui_touch(TE_DOWN,396,184,sim_now); ms_t t_dn=sim_now; int v_dn=edit_val;
      int guard=0; while(sav.active&&guard++<200) sim_run(33);
      sim_run(33); int reps=tch.rep_n, v1=edit_val; ms_t held=sim_now-t_dn;
      ui_touch(TE_UP,396,184,sim_now); sim_run(100);
      st_check("R1 stepper: + pressed during SAVING, the save fails (no ACK), finger still down: no catch-up burst (repeats bounded by the hold time), the value does not jump to its limit",
               saving&&v_dn==v0&&!sav.active&&view==V_DETAIL&&reps<=1+(int)(held/170)&&v1!=s->hi&&abs(v1-v0)<=s->step*(1+(int)(held/170)));
      /* belt and braces: a stale deadline (a frame gap of minutes) gives at most one step */
      sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_BREWW,sim_now); sim_run(600);
      ui_touch(TE_DOWN,396,184,sim_now); sim_run(100); int va=edit_val; tch.rep_next=sim_now-600000; sim_run(33); int vb=edit_val, rn=tch.rep_n;
      ui_touch(TE_UP,396,184,sim_now); sim_run(100);
      st_check("R1 stepper: a stale repeat deadline (10 min in the past) gives at most one step in the next frame",rn<=1&&abs(vb-va)<=SET[S_BREWW].step);
      sim_reset(); }
    /* R2: SAVE of an MCU setting while a start is pending (ACKed, status bits not yet set) is refused like the reverse */
    { char kc[sizeof cfg_path], cp[400]; snprintf(kc,sizeof kc,"%s",cfg_path); snprintf(cp,sizeof cp,"%s/_r2_config.json",dir); copy_file(kc,cp); snprintf(cfg_path,sizeof cfg_path,"%s",cp);   /* a copy: a regression must not save into the fixture */
      sim_reset(); sim_run(1500); act_request(ACT_STEAM,sim_now); sim_run(300); int pend=act.pending==ACT_STEAM&&!wq_busy();
      page=PG_STEAM; detail_open(S_STEAMT,sim_now); sim_run(600); tap(396,184); sim_run(200); int chg=edit_val!=SET[S_STEAMT].val;
      int tx0=txq_len; long n0=wq.n_ok; tap(310,316); sim_run(100);
      st_check("R2 SAVE during a pending steam start (bits not yet set): refused with ONE MOMENT…, nothing queued or sent, the editor stays open",
               pend&&chg&&!sav.active&&wq.n==0&&txq_len==tx0&&wq.n_ok==n0&&view==V_DETAIL&&!strcmp(toast.msg,TR(TX_G_WAIT)));
      snprintf(cfg_path,sizeof cfg_path,"%s",kc); sim_reset(); }
    st_r3();
    st_r5(dir);
    /* R4: a STOP queued behind a frame that fails (no ACK twice) is still transmitted; the failed frame's own group and starts are not */
    { sim_reset(); wq.sim_ack=0; ms_t t=sim_now; uint16_t v=(uint16_t)sv(S_BREWW); long f0=wq.n_fail;
      int q=wq_push(23,&v,1)&&act_push_stop(ACT_GRIND)&&wq.n==2; int stop_tx=0, acked=0;
      for(int i=0;i<100&&wq.n;i++){ t+=10; wq_step(t); if(last_tx_len==9&&last_tx[3]==6&&last_tx[4]==1){ stop_tx++; if(!acked){ acked=1; st_ack_last(t+5); } } }
      st_check("R4 STOP (sub6) queued behind a write that gets no ACK twice: the failed write is dropped (FAIL counted), the STOP is still sent and ACKed",
               q&&wq.n_fail==f0+1&&stop_tx==1&&acked&&wq.n==0&&wq.result==WR_OK&&reg[6]==0);
      /* the failed frame's own group (a two-frame water stop whose first frame fails) and non-stop frames behind it are dropped */
      sim_reset(); wq.sim_ack=0; t=sim_now; reg_seen[19]=1; reg[19]=1; f0=wq.n_fail;
      int q2=act_push_stop(ACT_WATER)&&wq.n==2&&wq_push(23,&v,1)&&wq.n==3&&act_push_stop(ACT_BREW)&&wq.n==4; int subs[8], ns=0;
      for(int i=0;i<200&&wq.n;i++){ t+=10; int before=wq.state; wq_step(t); if(wq.state==WQ_WAIT&&before==WQ_IDLE&&ns<8) subs[ns++]=last_tx[3]; if(wq.n&&wq.q[0].sub==22&&wq.state==WQ_WAIT) st_ack_last(t+5); }
      st_check("R4 ... a failing water stop (16 + 19) drops its own second frame and the plain write behind it, but the brew STOP queued after them is still sent",
               q2&&ns==2&&subs[0]==16&&subs[1]==22&&wq.n_fail==f0+1&&wq.n==0);
      /* a start queued behind a failing frame is still dropped (never sent late) */
      sim_reset(); wq.sim_ack=0; t=sim_now; f0=wq.n_fail; int q3=wq_push(23,&v,1)&&act_push(ACT_GRIND)&&wq.n==2; int st_tx=0;
      for(int i=0;i<100&&wq.n;i++){ t+=10; wq_step(t); if(last_tx[3]==3) st_tx++; }
      st_check("R4 ... a START queued behind a failing frame is still dropped (only STOP frames survive a failure)",q3&&st_tx==0&&wq.n==0&&wq.n_fail==f0+1);
      sim_reset(); }
}
/* R3 helper: a NORMAL run whose MCU clears the start word 0.4 s after the bits (mcu_sim / BREW §5.1: word 6 is consumed last) */
static int r3_kind=0; static ms_t r3_t=0;
static void tick_r3(ms_t t){
    static const uint16_t WS[4]={0x0141,0x1307,0x010B,0x4121}; static const uint8_t SW[4]={6,22,16,24};
    if(t<r3_t) reg[33]=WS[r3_kind]; else { reg[33]=0x0101; if(t>=r3_t+400) reg[SW[r3_kind]]=0; }
}
static void st_r3(void){
    static const char*const NM[4]={"GRIND (word 6)","BREW (word 22)","WATER (word 16)","STEAM (word 24)"}; static const uint8_t SW[4]={6,22,16,24};
    static const uint8_t SUB[4]={6,22,16,24};
    for(int k=0;k<4;k++){
        sim_reset(); sim_run(1500); act_request(k,sim_now); sim_run(400);
        int acked=act.pending==k&&!wq_busy()&&reg[SW[k]]==1, sk[2], sn0=stop_btns(sim_now,sk);
        sim_run(k==ACT_STEAM?15200:3700);
        int nostart=act.pending==ACT_NONE&&!strcmp(toast.msg,TR(TX_T_NOSTART));
        int sn=stop_btns(sim_now,sk), busy=pw_busy();
        int tgt=hit_test(stop_hit(sn,0).x0+10,stop_hit(sn,0).y0+10,sim_now);
        st_check(k==ACT_STEAM?"R3 latched start word, STEAM (word 24): ACKed, bits never come -> NO START, and HOME offers ITS STOP (the only button) — not during the pending start":
                 "R3 latched start word: ACKed, bits never come -> NO START, and HOME offers that activity's STOP — not during the pending start",
                 acked&&sn0==0&&nostart&&sn==1&&sk[0]==k&&tgt==T_STOP&&busy==(k!=ACT_WATER));   /* pw_busy = stock's words 6/22/24 (not 16) */
        int tx0=txq_len; tap(stop_hit(1,0).x0+20,stop_hit(1,0).y0+20); int q=wq.n>=1&&wq.q[0].sub==SUB[k];
        sim_run(600); sn=stop_btns(sim_now,sk); (void)tx0;
        char nm[200]; snprintf(nm,sizeof nm,"R3 %s: the tap sends the existing stop frame (sub%d, all zeros), the ACK clears the word: STOPPED toast, no STOP button, power button / auto off free again",NM[k],SUB[k]);
        st_check(nm,q&&reg[SW[k]]==0&&sn==0&&!pw_busy()&&!strcmp(toast.msg,TR(STOP_DONE[k]))&&stp.n_ok==1);
    }
    /* no false STOP after a NORMAL run: the bits clear, the MCU clears the word 0.4 s later */
    { int ok=1; for(int k=0;k<4;k++){ sim_reset(); sim_run(1500); act_request(k,sim_now); sim_run(300); r3_kind=k; r3_t=2500; sim_t0=sim_now; sim_tick=tick_r3;
        int seen=0, sk[2]; for(int i=0;i<200;i++){ sim_run(33); if(!stop_running(k)&&stop_btns(sim_now,sk)) seen=1; }
        ok&=!seen&&reg[SW[k]]==0; sim_tick=NULL; }
      st_check("R3 no false STOP: after a normal run of each activity (bits clear, the MCU clears its start word 0.4 s later) no STOP button is offered",ok); }
    /* asleep or offline: never offered */
    { sim_reset(); sim_run(1500); reg_seen[6]=1; reg[6]=1; sim_run(3500); int sk[2]; int awake=stop_btns(sim_now,sk)==1;
      reg[1]=1; reg[33]=0x0001; sim_run(200); int sl=stop_btns(sim_now,sk)==0; reg[1]=0; reg[33]=0x0101; sim_link=0; sim_run(2000); int off=stop_btns(sim_now,sk)==0;
      st_check("R3 a latched word 6 found at startup (no start of ours) is offered too; never while the controller sleeps or the link is down",awake&&sl&&off);
      sim_reset(); }
}
static void st_v617(void){
    printf("== selftest: v6.17 IA consolidation (HOME = live root; one page per feature w. its settings + HOLD TO START; SYSTEM in sections; DIAGNOSTICS behind SYSTEM; one way back) ==\n");
    int ok,w,h; char a[96],b[96];
    /* ---- (1) the row model: every parameter has exactly one home ---- */
    { int seen[NSET]={0}, tier_ok=1, n_feat=0, n_sys=0, n_x=0;
      for(int f=0;f<4;f++) for(const signed char*q=FEAT_ROWS[f];*q!=-1;q++){ seen[(int)*q]++; n_feat++; if(SET[(int)*q].tier!=f) tier_ok=0; }
      for(int sc=0;sc<NSEC;sc++) for(const signed char*q=SEC_ROWS[sc];*q!=-1;q++){ if(*q<-1){ n_x++; continue; } seen[(int)*q]++; n_sys++; if(SET[(int)*q].tier!=4) tier_ok=0; }
      ok=1; for(int i=0;i<NSET;i++) if(seen[i]!=(i==S_HOTT?0:1)){ ok=0; printf("     SET[%d] (%s) is listed %d time(s)\n",i,TR(SET[i].label),seen[i]); }
      printf("       %d settings: GRIND %d · BREW %d · WATER %d · STEAM %d · SYSTEM %d in %d sections + %d actions (v6.16: BREW 9 · STEAM·WATER 5 · SYSTEM 15 + 2 actions, three flat lists)\n",NSET,
             page_rows(PG_GRIND),page_rows(PG_BREW),page_rows(PG_WATER),page_rows(PG_STEAM),n_sys,NSEC,n_x);
      st_check("row model: every one of the 31 parameters (v6.18: + SOUND, READY TONE) is on exactly ONE page (its SET[].tier), nothing is dropped; the only one that is not a row is the read-only WATER SENSOR (it is the WATER page's live line)",
               ok&&tier_ok&&n_feat+n_sys==NSET-1&&n_x==2&&(SET[S_HOTT].flags&SF_SENSOR)&&SET[S_HOTT].tier==2);
      st_check("feature pages carry what the feature uses: GRIND 2 rows, BREW 6, WATER 2, STEAM 2 — each at most 2 rows away from fitting without scrolling (4 rows are in view above HOLD TO START)",
               page_rows(PG_GRIND)==2&&page_rows(PG_BREW)==6&&page_rows(PG_WATER)==2&&page_rows(PG_STEAM)==2&&scroll_max(PG_GRIND)==0&&scroll_max(PG_WATER)==0&&scroll_max(PG_STEAM)==0&&scroll_max(PG_BREW)<=2*ROW_P); }
    /* ---- (2) reachability with real taps: every setting in <= 3 taps from HOME, and back with the chevron — no frame on the way ---- */
    { int maxt=0, maxd=0; ok=1;
      for(int i=0;i<NSET;i++){ if(i==S_HOTT) continue;
          st_home(); int taps=0, drags=0, pg=PG_T0+SET[i].tier;
          if(pg_feat(pg)){ tap(ACT_X[pg-PG_GRIND],ACT_Y); taps++; st_settle(); }
          else { tap(CX,GEAR_Y); taps++; st_settle(); tap(240,row_y0(PG_SYS,sec_of_set(i))+25); taps++; st_settle(); }
          int k=row_find(page,RK_SET,i);
          if(page!=pg||k<0){ ok=0; printf("     %s: page %d (want %d), row %d\n",TR(SET[i].label),page,pg,k); continue; }
          for(int q=0;q<8;q++){ int y=row_y0(page,k)-(int)scroll[page]; if(y>=LIST_VY0+10&&y+ROW_H<=list_vy1(page)-10) break; int g[]={240,300, 240,280, 240,200}; st_gest(g,3); sim_run(100); drags++; }
          tap(240,row_y0(page,k)-(int)scroll[page]+25); taps++; st_settle();
          if(!(view==V_DETAIL&&sel==i)){ ok=0; printf("     %s: the row tap did not open its editor (view %d sel %d)\n",TR(SET[i].label),view,sel); }
          tap(CX,57); st_settle(); if(!(view==V_MAIN&&page==pg)) ok=0;
          tap(CX,PG_TITLE_Y+6); st_settle(); if(page!=PG_HOME) ok=0;
          if(wq.n||txq_len){ ok=0; printf("     %s: navigating queued/sent a frame\n",TR(SET[i].label)); }
          if(taps>maxt) maxt=taps; if(drags>maxd) maxd=drags; }
      printf("       worst case: %d taps (+ %d scroll drag(s)) from HOME to an editor\n",maxt,maxd);
      st_check("reachability: every setting's editor opens by TAPS from HOME — feature settings in 2 (icon, row), SYSTEM settings in 3 (gear, section, row) — and the chevron leads back the same way; no frame is sent while navigating",ok&&maxt<=3);
      st_home(); tap(CX,GEAR_Y); st_settle(); tap(240,row_y0(PG_SYS,SEC_STOCK)+25); st_settle(); int kb=row_find(PG_SYS,RK_X,X_BOOTUI);
      tap(240,row_y0(PG_SYS,kb)-(int)scroll[PG_SYS]+25); st_settle(); int x1=view==V_SYS&&xsel==X_BOOTUI; tap(CX,57); st_settle();
      tap(240,row_y0(PG_SYS,kb+1)-(int)scroll[PG_SYS]+25); st_settle(); int x2=view==V_SYS&&xsel==X_SWITCH; tap(CX,57); st_settle();
      tap(240,row_y0(PG_SYS,SEC_STOCK)-(int)scroll[PG_SYS]+25); st_settle(); int cl=sec_open==-1&&scroll[PG_SYS]==0&&page_rows(PG_SYS)==NSEC+1;
      tap(240,row_y0(PG_SYS,NSEC)+25); st_settle(); int dg=page==PG_DIAG&&diag_from==PG_SYS; tap(CX,PG_TITLE_Y+6); st_settle(); int bs1=page==PG_SYS; tap(CX,PG_TITLE_Y+6); st_settle();
      st_check("SYSTEM: BOOT UI and SWITCH TO ORIGINAL NOW open from STOCK UI; a tap on the open header closes the section again; the DIAGNOSTICS row opens DIAGNOSTICS, whose chevron leads back to SYSTEM and from there to HOME",x1&&x2&&cl&&dg&&bs1&&page==PG_HOME&&wq.n==0&&txq_len==0);
      st_home(); tap(CX,GEAR_Y); st_settle(); tap(240,row_y0(PG_SYS,SEC_DISPLAY)+25); st_settle(); int o1=sec_open==SEC_DISPLAY&&row_find(PG_SYS,RK_SET,S_UILANG)==1;
      { int g[]={240,360, 240,330, 240,100}; st_gest(g,3); sim_run(100); }                                      /* DISPLAY is open: SCHEDULE's header is below the fold -> scroll (v6.18: 6 rows in DISPLAY · SOUND) */
      tap(240,row_y0(PG_SYS,row_find(PG_SYS,RK_SEC,SEC_SCHED))-(int)scroll[PG_SYS]+25); st_settle();
      int o2=sec_open==SEC_SCHED&&row_find(PG_SYS,RK_SET,S_UILANG)<0&&row_find(PG_SYS,RK_SET,S_PREHEAT)==SEC_SCHED+1&&(int)scroll[PG_SYS]==(int)clampf((float)(SEC_SCHED*ROW_P),0,scroll_max(PG_SYS));
      tap(CX,PG_TITLE_Y+6); st_settle(); tap(CX,GEAR_Y); st_settle();
      st_check("SYSTEM sections: one open at a time (opening SCHEDULE closes DISPLAY and brings its header to the top); leaving SYSTEM and coming back shows it closed again",o1&&o2&&page==PG_SYS&&sec_open==-1&&scroll[PG_SYS]==0); }
    /* ---- (3) tap vs hold: the same icon / pill, two meanings, one frame set ---- */
    { char qi[4][400], qs[4][400]; int okt=1, okh=1, oks=1;
      for(int k=0;k<4;k++){
          st_home(); int f=st_hold(ACT_X[k],ACT_Y,700); int n1=st_q_snap(qi[k],sizeof qi[k]); if(!(f&&act.pending==k&&page==PG_HOME&&view==V_MAIN&&n1>=1)) okh=0;
          st_home(); tap(ACT_X[k],ACT_Y); st_settle(); if(!(page==PG_GRIND+k&&view==V_MAIN&&wq.n==0&&txq_len==0&&!toast.dur&&act.pending==ACT_NONE)) okt=0;
          tap(CX,START_Y+START_H/2); sim_run(100); if(!(page==PG_GRIND+k&&wq.n==0&&txq_len==0&&toast.dur&&!strcmp(toast.msg,TR(TX_T_HOLD_HINT)))) oks=0; toast.dur=0;
          int f2=st_hold(CX,START_Y+START_H/2,700); int n2=st_q_snap(qs[k],sizeof qs[k]); int pend=act.pending==k; sim_run(500);
          if(!(f2&&pend&&n2==n1&&!strcmp(qi[k],qs[k])&&page==PG_HOME)){ oks=0; printf("     %s: icon hold '%s' vs HOLD TO START '%s' (page %d)\n",TR(ACT_NAME[k]),qi[k],qs[k],page); } }
      st_check("HOME icon: a TAP opens that feature's page — no frame, no toast, nothing started (v6.16: a toast 'HOLD THE BUTTON TO START' and a dead end)",okt);
      st_check("HOME icon: a HOLD (650 ms) still starts the activity directly, HOME stays",okh);
      st_check("feature page: HOLD TO START queues byte for byte the SAME frames as the icon hold (grind, brew, water, steam) and returns to HOME (the live view w. STOP); a short tap on it only says 'KEEP HOLDING TO START'",oks);
      st_hitbox(T_ACT0,&w,&h); st_home(); page=PG_BREW; static_dirty=1; sim_run(300); st_hitbox(T_START,&w,&h); printf("       HOLD TO START: %d x %d px (pill %d x %d)\n",w,h,START_W,START_H);
      st_check("touch: HOLD TO START has a touch area >= 48 px high and >= 200 px wide, clear of the list above it",w>=200&&h>=48&&START_HIT_Y>=FEAT_VY1&&hit_test(CX,FEAT_VY1-6,sim_now)!=T_START);
      st_hitbox(T_BACK,&w,&h); printf("       page header (close): %d x %d px\n",w,h);
      st_check("touch: a page's close target is its whole header (chevron + title), >= 48 px high and >= 120 px wide, clear of the first row",w>=120&&h>=48&&PBACK_Y0+PBACK_H<=LIST_VY0&&hit_test(CX,LIST_Y0+25,sim_now)==T_ROW0); }
    /* ---- (4) the live line: values when a start is possible, the reason when it is not ---- */
    { st_home(); page=PG_BREW; uint32_t c=page_live_text(PG_BREW,a,sizeof a,sim_now); char t1[16],t2[16]; fmt_t10(t1,sizeof t1,reg[40],1); fmt_t10(t2,sizeof t2,reg[42],0); snprintf(b,sizeof b,TR(TX_LV_BREW),t1,t2);
      int v1=!strcmp(a,b)&&c==C_TXT2; page_live_text(PG_WATER,a,sizeof a,sim_now); snprintf(b,sizeof b,TR(TX_LV_WATER),t1); int v2=!strcmp(a,b);
      int kg=SET[S_GRINDMODE].val; SET[S_GRINDMODE].val=2; c=page_live_text(PG_GRIND,a,sizeof a,sim_now); int g1=!strcmp(a,TR(TX_G_GRINDTIME))&&c==C_WARN; c=page_live_text(PG_BREW,a,sizeof a,sim_now); int g2=c==C_TXT2; SET[S_GRINDMODE].val=kg;
      uint16_t w1[1]={(uint16_t)sv(S_BREWW)}; wq_push(23,w1,1); c=page_live_text(PG_BREW,a,sizeof a,sim_now); int bz=act_guard(ACT_BREW,sim_now)==TR(TX_G_WAIT)&&c==C_TXT2&&strcmp(a,TR(TX_G_WAIT)); memset(&wq,0,sizeof wq);
      sim_link=0; sim_run(3000); c=page_live_text(PG_STEAM,a,sizeof a,sim_now); int off=!strcmp(a,TR(TX_G_NOLINK))&&c==C_WARN; int f=st_hold(CX,START_Y+START_H/2,700); int nf=wq.n==0&&act.pending==ACT_NONE&&toast.dur&&!strcmp(toast.msg,TR(TX_G_NOLINK)); (void)f; sim_link=1; sim_run(400);
      st_check("feature page live line: BREW 'BOILER 99.9° · GROUP 70°', WATER 'WATER 99.9°' (word 40 — the row that looked like a setting in v6.16); GRIND MODE = TIME shows the reason on GRIND only; a write in transit does not flicker 'ONE MOMENT…'",v1&&v2&&g1&&g2&&bz);
      st_check("feature page offline: the live line says NO CONNECTION (amber), HOLD TO START starts nothing and says why",off&&nf); }
    /* ---- (5) something starts running while a page is open: HOME (with STOP) takes over ---- */
    { st_home(); tap(ACT_X[ACT_GRIND],ACT_Y); st_settle(); int on=page==PG_GRIND; reg[33]=0x1307; sim_run(700); int h1=page==PG_HOME&&view==V_MAIN&&hit_test(240,372,sim_now)==T_STOP;
      st_home(); tap(CX,GEAR_Y); st_settle(); reg[33]=0x1307; sim_run(700); int s1=page==PG_SYS;
      st_home(); tap(ACT_X[ACT_BREW],ACT_Y); st_settle(); tap(240,LIST_Y0+25); st_settle(); int ed=view==V_DETAIL&&sel==S_BREWW; reg[33]=0x1307; sim_run(700); int e1=view==V_DETAIL; tap(CX,57); sim_run(900); int e2=view==V_MAIN&&page==PG_HOME;
      st_home(); reg[33]=0x1307; sim_run(400); int ic=hit_test(ACT_X[0],ACT_Y,sim_now)!=T_ACT0;
      st_check("an activity starts (on the machine) while a feature page is open: the UI slides to HOME, where STOP is; SYSTEM stays (deliberate); an open editor stays until it is closed — then HOME; no feature page can be opened while something runs",on&&h1&&s1&&ed&&e1&&e2&&ic); }
    /* ---- (6) one way back: chevron/header, swipe right, pull the header down; DIAGNOSTICS from HOME's error hint ---- */
    { st_home(); tap(CX,GEAR_Y); st_settle(); int g1[]={240,40, 240,60, 240,130}; st_gest(g1,3); st_settle(); int p1=page==PG_HOME;
      tap(ACT_X[ACT_BREW],ACT_Y); st_settle(); int g2[]={240,150, 240,170, 240,260}; st_gest(g2,3); st_settle(); int p2=page==PG_BREW&&scroll[PG_BREW]==0;
      int g3[]={240,300, 240,280, 240,200}; st_gest(g3,3); st_settle(); int p3=page==PG_BREW&&scroll[PG_BREW]>0;
      st_home(); int nohint=hit_test(240,262,sim_now)!=T_DIAG; reg[35]=1; sim_run(500); int hd=hit_test(240,262,sim_now)==T_DIAG&&strstr(hm.hint1,"E2")!=NULL; tap(240,262); st_settle(); int d1=page==PG_DIAG&&diag_from==PG_HOME;
      tap(CX,PG_TITLE_Y+6); st_settle(); int d2=page==PG_HOME; reg[35]=0; sim_run(300);
      sim_link=0; sim_run(3000); int ho=hit_test(240,262,sim_now)==T_DIAG&&!strcmp(hm.hint1,TR(TX_C_DIAGREF)); sim_link=1; sim_run(400);
      st_check("back: pulling a page's header down closes it; a vertical drag inside the list scrolls (never closes)",p1&&p2&&p3);
      st_check("error / offline on HOME: the hint 'CODE E2 · TAP FOR DETAILS' IS the way to DIAGNOSTICS (tap -> DIAGNOSTICS, chevron -> back to HOME); when all is well that area is no target",nohint&&hd&&d1&&d2&&ho); }
    /* ---- (7) STOP: the biggest, brightest thing under the numbers ---- */
    { Rect r1=stop_pill(1,0), r2=stop_pill(2,0), r3=stop_pill(2,1); uint32_t bg=bgpure[372*W+CX];
      float ct=st_cr(C_STOP_FG,C_STOP_T), cb=st_cr(C_STOP_FG,C_STOP_B), cbg=st_cr(C_STOP_T,bg), old=st_cr(rgb(120,40,44),bg);
      st_home(); reg[33]=0x1307; sim_run(400); st_hitbox(T_STOP,&w,&h); int gw,gh; st_hitbox(T_GEAR,&gw,&gh);
      printf("       STOP: pill %d x %d (v6.16: 160 x 44), touch %d x %d · white on red %.2f / %.2f · red on background %.2f (v6.16's dark red: %.2f)\n",r1.x1-r1.x0,r1.y1-r1.y0,w,h,ct,cb,cbg,old);
      st_check("STOP: one pill 216 x 54 (v6.16: 160 x 44 — 66 % larger), two pills 152 x 54 with a gap, all inside the widget and the circle's chord; touch area >= 232 x 60 and the largest target on HOME while something runs",
               r1.x1-r1.x0==216&&r1.y1-r1.y0==54&&r2.x1-r2.x0==152&&r3.x0-r2.x1>=8&&r2.x0>=72&&r3.x1<=408&&(r1.x1-r1.x0)/2<=chord_half(STOP_PILL_Y,STOP_PILL_H)&&r3.x1-CX<=chord_half(STOP_PILL_Y,STOP_PILL_H)&&w>=232&&h>=60&&w*h>gw*gh);
      st_check("STOP contrast: white text >= 4.5:1 on the whole red gradient; the red >= 3:1 against the background (v6.16's dark red was below 2:1 — it read as disabled)",ct>=4.5f&&cb>=4.5f&&cbg>=3.f&&old<2.f); }
    /* ---- (8) less text: what is on screen, measured ---- */
    { st_home(); page=PG_BREW; static_dirty=1; sim_run(300); int ne=1;
      static const int probe[]={S_BREWT,S_HOTW,S_UILANG,S_PHAT,S_TZ,S_VOL};
      for(unsigned q=0;q<sizeof probe/sizeof probe[0];q++){ detail_open(probe[q],sim_now); sim_run(500); force_all=1; ui_frame(sim_now+1);
          for(int y=362;y<426;y++) for(int x=140;x<340;x++) if(canvas[y*W+x]!=bgpure[y*W+x]){ ne=0; y=H; break; }
          detail_close(sim_now); sim_run(400); }
      st_check("editor: nothing is drawn below CANCEL / SAVE any more (v6.16: two footer lines on every editor — 'CONTROLLER NOW + CONFIG', 'STOCK UI: AFTER RESTART', file paths)",ne);
      detail_open(S_HOTW,sim_now); sim_run(400); uint32_t c=detail_status(a,sizeof a); int s1=!strcmp(a,TR(TX_D_HOLDFAST))&&c==C_TXT3; tap(396,184); sim_run(50); detail_status(a,sizeof a); int s2=strstr(a,TR(TX_D_HOLDFAST))==NULL&&a[0]; detail_close(sim_now); sim_run(400);
      detail_open(S_PISW,sim_now); sim_run(400); detail_status(a,sizeof a); int s3=!a[0]&&!detail_holdable(&SET[S_PISW])&&!detail_holdable(&SET[S_BREWT])&&!detail_holdable(&SET[S_TZ])&&detail_holdable(&SET[S_VOL])&&detail_holdable(&SET[S_PHAT]); detail_close(sim_now); sim_run(400);
      st_check("editor status line: untouched + a long range -> 'HOLD - OR + TO GO FAST' (the stepper's hidden ability, said where it applies); changed -> 'CHANGED · WAS …'; an ON/OFF or short range -> empty ('SAVED VALUE' is gone)",s1&&s2&&s3);
      int bad=0; for(int i=0;i<NSTR;i++){ int k=STR_K[i]; if(k==KCON||k==KDL||k==KDV||k==KDF) continue; for(int lg=0;lg<NLANG;lg++){ const char*t=STR[lg][i];
            if(strstr(t,"CONFIG ONLY")||strstr(t,"KUN CONFIG")||strstr(t,"CMD8")||strstr(t,"CONFIG.JSON")||strstr(t,"OR STOP")||strstr(t,"ELLER STOP")||strstr(t,"EST.")){ bad++; printf("     [%s] %s: '%s'\n",LANG_CODE[lg],STR_ID[i],t); } } }
      printf("       string table: %d ids (v6.16: 449)\n",NSTR);
      st_check("text: no user-facing string says CONFIG ONLY / CONFIG.JSON / CMD8 / '· OR STOP' / 'EST.' any more; the string table is smaller than v6.16's 449 ids although v6.17 adds pages (v6.18's sound block aside)",bad==0&&NSTR-(TX_DV_SO_DROP-TX_SL_SOUND+1)<449);   /* v6.18: the sound feature's own strings (one contiguous block) are not counted */
      st_home(); home_model(sim_now); int one=!strcmp(hm.hint1,TR(TX_C_HINT))&&hm.hint2==NULL&&!strcmp(hm.label,TR(TX_H_BOILER))&&!st_has(hm.label,"\xC2\xB7");
      reg[33]=0x0141; sim_run(600); home_model(sim_now); fmt_fix(a,sizeof a,sv(S_GRINDW),1); snprintf(b,sizeof b,TR(TX_H_AUTOSTOP_G),a); int g=!strcmp(hm.label,TR(TX_H_DOSE))&&!st_has(hm.label,a)&&!strcmp(hm.sub,b)&&sub_font(&hm)==F_S;
      st_check("HOME: one hint line; the label is the name of the number only ('BREW BOILER', 'DOSE' — no '· NOW', no repeated target); during an activity the target is said once, in F_S ('STOPS AT 18.5 G')",one&&g); }
    /* ---- (9) the icons say what they will do ---- */
    { st_home(); home_model(sim_now); char e[4][16]; char u[16];
      fmt_fix(u,sizeof u,sv(S_GRINDW),1); snprintf(e[0],16,"%s %s",u,TR(TX_U_G)); fmt_fix(u,sizeof u,sv(S_BREWW),-1); snprintf(e[1],16,"%s %s",u,TR(TX_U_G));
      fmt_fix(u,sizeof u,sv(S_HOTW),-1); snprintf(e[2],16,"%s %s",u,TR(TX_U_G)); fmt_t10(u,sizeof u,sv(S_STEAMT),0); snprintf(e[3],16,"%s\xC2\xB0",u);
      ok=1; for(int k=0;k<4;k++) ok&=!strcmp(hm.alab[k],e[k]);
      printf("       icon targets: %s · %s · %s · %s\n",hm.alab[0],hm.alab[1],hm.alab[2],hm.alab[3]);
      int kv=SET[S_BREWW].val, ktu=SET[S_TUNIT].val; SET[S_BREWW].val=420; SET[S_TUNIT].val=0; home_model(sim_now); int chg=!strcmp(hm.alab[1],"42 G")&&strstr(hm.alab[3],"149")!=NULL; SET[S_BREWW].val=kv; SET[S_TUNIT].val=ktu;
      int kl=SET[S_HOTW].loaded; SET[S_HOTW].loaded=0; home_model(sim_now); int nl=!strcmp(hm.alab[2],"--"); SET[S_HOTW].loaded=kl; home_model(sim_now);
      st_check("HOME icons carry their target underneath (grind dose, brew weight, water amount, milk temperature in the chosen unit) and follow the settings; a value that is not loaded shows '--'",ok&&chg&&nl);
      char sm[40]; sec_summary(SEC_DISPLAY,sm,sizeof sm); int q1=!strcmp(sm,TR(TX_UL_EN+ui_lang)); sec_summary(SEC_CLOCK,sm,sizeof sm); char ck[24]; home_clock_text(ck,sizeof ck); int q2=!strcmp(sm,ck);
      int kon=sch.on,khm=sch.hm; sch.on=0; sec_summary(SEC_SCHED,sm,sizeof sm); int q3=!strcmp(sm,TR(TX_E_OFF)); sch.on=1; sch.hm=390; sec_summary(SEC_SCHED,sm,sizeof sm); int q4=!strcmp(sm,"06:30"); sch.on=kon; sch.hm=khm; sec_summary(SEC_STOCK,sm,sizeof sm);
      uint32_t dc=diag_row_text(a,sizeof a,sim_now); int q5=!strcmp(a,TR(TX_DV_OK))&&dc==C_OK; reg[35]=1; dc=diag_row_text(a,sizeof a,sim_now); int q6=!strcmp(a,"E2")&&dc==C_ERR; reg[35]=0;
      st_check("SYSTEM's closed headers say what is inside: DISPLAY = the UI language's own name, CLOCK = the time, SCHEDULE = OFF / the pre-heat time; the DIAGNOSTICS row shows OK / the error code",q1&&q2&&q3&&q4&&!sm[0]&&q5&&q6); }
    sim_reset(); sim_tick=NULL;
}
/* ============================ v6.14: the power button ============================ */
#define PW_V_CMD8 "aa 55 08 ea 07 0b 12 10 23 1c 00 00 00 cc df"   /* 2026-11-18 16:35:28, alarm 0/0/0 (independently computed CRC) */
#define PW_V_OFF  "aa 55 06 00 02 00 00 00 00 56 94"               /* stock powerOff: usart_send(6,0,2), word 0 = 0, word 1 = 0 */
#define PW_V_ON   "aa 55 06 00 01 01 00 c5 5f"                     /* PowerOn 0x146EF8: usart_send(6,0,1), word 0 = 1 */
#define PW_V_KA   "aa 55 06 01 01 00 00 c5 33"                     /* awake: word 1 = 0 */
/* txq -> list of frames (hex), in the order they went out */
static int st_txq_frames(char out[][96],int max){
    int n=0, i=0;
    while(i+5<=txq_len&&n<max){
        if(txq[i]!=0xAA||txq[i+1]!=0x55){ i++; continue; }
        int c=txq[i+2], L= c==0x08?15:(c==0x06?2*txq[i+4]+7:(c==0x03?7:(c==0x09?txq[i+3]+7:7)));
        if(i+L>txq_len) break;
        char*p=out[n]; p[0]=0; for(int k=0;k<L&&k<31;k++) p+=sprintf(p,k?" %02x":"%02x",txq[i+k]);
        n++; i+=L;
    }
    return n;
}
/* main loop in 10 ms steps: link (as the controller's replies), write queue, auto-ACK of every cmd6, button state, keep-awake */
static ms_t st_pw_loop(ms_t t,ms_t dur,int link,int ack){
    ms_t end=t+dur;
    for(;t<end;t+=10){
        if(link) last_rx_ms=t;
        wq_step(t);
        if(ack&&wq.n&&wq.state==WQ_WAIT&&t-wq.sent_ms>=20) st_ack_last(t);
        pw_step(t); proto_keepawake(t,0);
    }
    ui_frame(t); return t;
}
static ms_t st_pw_home(void){ ms_t t=st_home(); ka_next=t+30000; return t; }   /* keep-awake: next deadline in 30 s (as after startup) */
/* ================= v6.16: TIME & SCHEDULING selftest ================= */
#define TM_V_ARM "aa 55 08 ea 07 0b 12 10 23 1c 13 06 1e be b2"   /* 2026-11-18 16:35:28 local, alarm day 19 06:30 (CRC computed independently) */
#define TM_V_OFF0 PW_V_CMD8                                       /* the same time, alarm 0/0/0 */
static int st_echo8=1;                     /* v6.20: the controller echoes every pre-arm cmd8 with 0x88 (0 = the echo is lost) */
static void st_echo_arm(ms_t t){ uint8_t a[15]; memcpy(a,arm.f8,15); a[2]=0x88; uint16_t c=crc16(a,13); a[13]=c&0xFF; a[14]=c>>8; rx_feed(a,15,t); }
static ms_t st_tm_loop(ms_t t,ms_t dur,int link){
    ms_t end=t+dur;
    for(;t<end;t+=10){
        if(link) last_rx_ms=t;
        wq_step(t);
        if(wq.n&&wq.state==WQ_WAIT&&t-wq.sent_ms>=20) st_ack_last(t);
        long ns=arm.n_sent;
        pw_step(t); sched_step(t); proto_keepawake(t,0);
        if(st_echo8&&link&&arm.n_sent!=ns) st_echo_arm(t+5);
    }
    ui_frame(t); return t;
}
static int st_cmd8_frames(char fr[][96],int max,char only[][96]){   /* only the cmd8 frames in the TX queue, in order */
    char all[64][96]; int n=st_txq_frames(all,64), k=0;
    for(int i=0;i<n&&k<max;i++) if(!strncmp(all[i],"aa 55 08",8)) strcpy(only[k++],all[i]);
    (void)fr; return k;
}
static int st_own_save(int i,int val,ms_t t){ detail_open(i,t); edit_val=val; save_begin(t); return view==V_MAIN&&SET[i].val==val; }
static int64_t st_utc(int y,int mo,int d,int h,int mi,int s){ return days_civil(y,mo,d)*86400+h*3600LL+mi*60LL+s; }
static void st_time(const char*dir){
    printf("== selftest: v6.16 time & scheduling (stock update_time 0xD5944, _refresh_systime 0x135548, performSleepActions 0x148324, powerOffSend 0x14700C; MCU 0x9C88 + sleep task 0x2F52) ==\n");
    char b[64], fr[16][96]; int n; ms_t t;
    int keep_tm=SET[S_TIMEMODE].val, keep_tz=SET[S_TZ].val, keep_dst=SET[S_DST].val;
    char keep_sp[256]; snprintf(keep_sp,sizeof keep_sp,"%s",sched_path); snprintf(sched_path,sizeof sched_path,"%s/schedule",dir); unlink(sched_path);
    /* ---- (1) TIME FORMAT = stock ---- */
    st_check("timemode semantics = stock: OPT_TIME[0] = 12-HOUR, OPT_TIME[1] = 24-HOUR (update_time XORs model[0x59] = timemode != 0 with 1: 1 -> \"%02d:%02d\", 0 -> A.M./P.M.)",
             OPT_TIME[0]==TX_E_12H&&OPT_TIME[1]==TX_E_24H&&SET[S_TIMEMODE].min==0&&SET[S_TIMEMODE].max==1&&!strcmp(SET[S_TIMEMODE].key,"timemode"));
    { SET[S_TIMEMODE].val=1; set_fmt(S_TIMEMODE,1,b,sizeof b); int a=!strcmp(b,TR(TX_E_24H)); set_fmt(S_TIMEMODE,0,b,sizeof b); a&=!strcmp(b,TR(TX_E_12H));
      st_check("the TIME FORMAT row shows 24-HOUR for config timemode = 1 (the stock switch \"24-Hour Format\" is ON) and 12-HOUR for 0",a); }
    { static const struct { int mode,h,m; const char*want; } V[]={{1,0,5,"00:05"},{1,13,7,"13:07"},{1,23,59,"23:59"},{0,0,5,"12:05 A.M."},{0,11,59,"11:59 A.M."},
          {0,12,0,"12:00 P.M."},{0,13,7,"01:07 P.M."},{0,23,59,"11:59 P.M."}}; int ok=1;
      for(unsigned k=0;k<sizeof V/sizeof V[0];k++){ SET[S_TIMEMODE].val=V[k].mode; fmt_hm(b,sizeof b,V[k].h,V[k].m,1); if(strcmp(b,V[k].want)){ ok=0; printf("     timemode %d %02d:%02d -> '%s' (want '%s')\n",V[k].mode,V[k].h,V[k].m,b,V[k].want); } }
      st_check("clock format = stock update_time byte for byte: 24 h \"%02d:%02d\"; 12 h 0 -> 12 A.M., 1..11 A.M., 12 -> 12 P.M., 13..23 -> 1..11 P.M. (8 vectors)",ok); }
    /* ---- (2) local time + DST ---- */
    { int ok=TZ_N==136&&TZT[48].off==3600&&!strcmp(TZT[48].utc,"UTC+01:00")&&TZT[43].off==0&&TZT[0].off==-43200&&TZT[135].off==50400&&TZT[30].off==-9000;
      st_check("zone table = stock 0x1A5890: 136 entries (#0 UTC-12, #43 UTC, #48 UTC+01:00 Amsterdam/Berlin, the stock quirks #30 = -2:30 and #135 = +14:00 kept)",ok); }
    { SET[S_TZ].val=48; SET[S_DST].val=0; clk_fake=PV_CLK; struct tm lt; int v=clk_now(&lt);
      st_check("local = SoC UTC + zone offset (+ DST): 15:35:28 UTC in zone #48 = 16:35:28 Wednesday 2026-11-18",v&&lt.tm_hour==16&&lt.tm_min==35&&lt.tm_sec==28&&lt.tm_wday==3&&lt.tm_mday==18); }
    { static const struct { int tz; int64_t u; int want; const char*what; } D[]={
        {48,0,0,"EU 2026-03-29 00:59:59 UTC: standard"},{48,1,1,"EU 01:00:00 UTC: DST"},{48,2,1,"EU 2026-10-25 00:59:59 UTC: DST"},{48,3,0,"EU 01:00:00 UTC: standard"},
        {13,4,0,"US Central 2026-03-08 07:59:59 UTC: standard"},{13,5,1,"US 08:00 UTC (02:00 CST): DST"},{13,6,1,"US 2026-11-01 06:59:59 UTC: DST"},{13,7,0,"US 07:00 UTC: standard"},
        {116,8,1,"AU Sydney January: DST (southern)"},{116,9,0,"AU July: standard"},{101,10,0,"Beijing (no rule): never"}};
      int64_t U[11]={st_utc(2026,3,29,0,59,59),st_utc(2026,3,29,1,0,0),st_utc(2026,10,25,0,59,59),st_utc(2026,10,25,1,0,0),
                     st_utc(2026,3,8,7,59,59),st_utc(2026,3,8,8,0,0),st_utc(2026,11,1,6,59,59),st_utc(2026,11,1,7,0,0),
                     st_utc(2026,1,15,12,0,0),st_utc(2026,7,15,12,0,0),st_utc(2026,7,15,12,0,0)}; int ok=1;
      for(unsigned k=0;k<sizeof D/sizeof D[0];k++){ int g=dst_on(D[k].tz,U[D[k].u]); if(g!=D[k].want){ ok=0; printf("     %s: got %d\n",D[k].what,g); } }
      SET[S_TZ].val=48; SET[S_DST].val=1; struct tm lt; clk_local_at(U[1],&lt); int a=lt.tm_hour==3&&lt.tm_min==0; SET[S_DST].val=0; clk_local_at(U[1],&lt); a&=lt.tm_hour==2;
      st_check("DST windows (sumtime = 1): EU last Sun Mar/Oct 01:00 UTC, US 2nd Sun Mar / 1st Sun Nov 02:00 local, AU southern; zones without a rule never (11 vectors); sumtime = 0 -> never +1 h",ok&&a); }
    /* ---- (3) stock's next-occurrence algorithm (performSleepActions, one entry) ---- */
    { struct V { int y,mo,d,h,mi,hm,mask,k,ad; const char*what; };
      static const struct V V[]={
        {2026,11,18,16,35,390,0x7F,1,19,"daily 06:30 at Wed 16:35 -> tomorrow 19"},{2026,11,18,16,35,1020,0x7F,0,18,"daily 17:00 at 16:35 -> today"},
        {2026,11,18,6,30,390,0x7F,1,19,"daily 06:30 at 06:30 -> strictly after now: tomorrow"},{2026,11,20,16,35,390,0x3E,3,23,"Mon-Fri at Fri 16:35 -> Mon 23"},
        {2026,11,18,16,35,480,0x41,3,21,"Sat-Sun 08:00 at Wed -> Sat 21"},{2026,11,18,16,35,390,0x00,1,19,"once 06:30 at 16:35 -> tomorrow"},
        {2026,11,18,16,35,1020,0x00,0,18,"once 17:00 at 16:35 -> today"},{2026,11,30,20,0,390,0x7F,1,1,"Nov 30 -> Dec 1 (month wrap, stock biz_days_in_month)"},
        {2026,12,31,20,0,390,0x7F,1,1,"Dec 31 -> Jan 1"},{2028,2,28,20,0,390,0x7F,1,29,"2028-02-28 -> 29 (leap year)"},{2026,11,18,16,35,390,0x08,7,25,"Wed-only 06:30 at Wed 16:35 -> next Wed 25"}};
      int ok=1; for(unsigned k=0;k<sizeof V/sizeof V[0];k++){ struct tm tm; memset(&tm,0,sizeof tm); tm.tm_year=V[k].y-1900; tm.tm_mon=V[k].mo-1; tm.tm_mday=V[k].d; tm.tm_hour=V[k].h; tm.tm_min=V[k].mi; tm.tm_wday=dow_civil(V[k].y,V[k].mo,V[k].d);
          int ad; int kk=sched_next(&tm,V[k].hm,V[k].mask,&ad); if(kk!=V[k].k||ad!=V[k].ad){ ok=0; printf("     %s: got +%d day %d\n",V[k].what,kk,ad); } }
      st_check("next occurrence = stock performSleepActions (today if the weekday matches and hh:mm > now, else the next flagged weekday; one-shot today/tomorrow; day wrapped by the month's length) — 11 vectors",ok); }
    /* ---- (4) the alarm frame byte for byte ---- */
    t=st_pw_home(); SET[S_TIMEMODE].val=1;
    { sch.on=1; sch.hm=390; sch.rep=1; sch.once_at=0; struct tm tm; int ad,ah,am; pw_now_tm(&tm); int w=sched_alarm(clk_utc(),&ad,&ah,&am,NULL); uint8_t f[15]; pw_cmd8(f,&tm,ad,ah,am);
      st_check("pre-heat daily 06:30 at Wed 16:35:28 -> cmd8 = " TM_V_ARM " (= stock powerOffSend(19, 6, 30): alarm day = day of month, hour, minute)",w&&hexeq(f,15,TM_V_ARM));
      sch.on=0; w=sched_alarm(clk_utc(),&ad,&ah,&am,NULL); pw_cmd8(f,&tm,ad,ah,am);
      st_check("no pre-heat -> alarm 0/0/0 = " TM_V_OFF0 " (stock: no entry -> powerOffSend(0, 0, 0); the MCU skips day+hour+minute = 0)",!w&&hexeq(f,15,TM_V_OFF0));
      sch.on=1; clk_fake=10000; w=sched_alarm(clk_utc(),&ad,&ah,&am,NULL); clk_fake=PV_CLK;
      st_check("SoC clock not set (before 2025): no alarm (0/0/0) — a garbage alarm would wake the machine at a random time",!w&&!ad&&!ah&&!am); sch.on=0; }
    /* ---- (5) the schedule file + SAVE path (no config.json write, no MCU word) ---- */
    { cfg_read(); char m0[33]; strcpy(m0,cfg_md5); int w0=cfg_writes; t=st_pw_home(); txq_len=0;
      int a=st_own_save(S_PREHEAT,1,t)&&st_own_save(S_PHAT,395,t)&&st_own_save(S_PHREP,2,t)&&st_own_save(S_AUTOOFF,3,t);
      char rb[256]; int fd=open(sched_path,O_RDONLY); int rn=fd>=0?(int)read(fd,rb,sizeof rb-1):-1; if(fd>=0) close(fd); if(rn<0) rn=0; rb[rn]=0;
      cfg_read(); char tp[300]; snprintf(tp,sizeof tp,"%s.tmp",sched_path);
      st_check("SAVE of the 4 time rows -> " LC "merakiplus/schedule" UC " = preheat=1, at=06:35, repeat=mon-fri, autooff=30; atomic (no tmp left); toast SAVED · MERAKI+ SCHEDULE",
               a&&!strcmp(rb,"preheat=1\nat=06:35\nrepeat=mon-fri\nautooff=30\nonce_at=0\n")&&access(tp,F_OK)!=0&&!strcmp(toast.msg,TR(TX_T_OWN_SAVED)));
      st_check("... and config.json is NOT written (md5 unchanged, 0 config writes) and no cmd6 frame was queued for it",!strcmp(cfg_md5,m0)&&cfg_writes==w0&&wq.n==0);
      sch.on=0; sch.hm=0; sch.rep=0; sch.ao=0; settings_load();
      st_check("reload: the file gives the rows back (PRE-HEAT ON, 06:35, MON-FRI, 30 MIN)",sch.on==1&&sch.hm==395&&sch.rep==2&&sch.ao==3&&SET[S_PHAT].val==395&&SET[S_AUTOOFF].val==3&&sch.src==SCS_FILE);
      put_bytes(sched_path,"preheat=7\nat=25:99\nrepeat=hourly\nautooff=45\n",44); settings_load();
      st_check("bad values -> defaults (pre-heat OFF, 06:30, EVERY DAY, auto off OFF) — never a surprise alarm",sch.on==0&&sch.hm==390&&sch.rep==1&&sch.ao==0);
      unlink(sched_path); settings_load(); st_check("no file -> defaults, source DEFAULT",sch.on==0&&sch.src==SCS_DEFAULT);
      st_check("the 4 time rows are merakiplus's own (SF_OWN): no config key, no MCU word, not in --dump-table; the stepper for PRE-HEAT AT is 5 min, 00:00..23:55",
               !SET[S_PREHEAT].key&&!SET[S_PHAT].key&&SET[S_PHAT].mcu==M_NONE&&(SET[S_PHAT].flags&SF_OWN)&&SET[S_PHAT].step==5&&SET[S_PHAT].max==1435); }
    /* ---- (6) pre-arm while on ---- */
    t=st_pw_home(); txq_len=0; t=st_tm_loop(t,8000,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("no schedule: nothing is sent while on (v6.15 traffic unchanged)",n==0&&arm.n_sent==0);
    st_own_save(S_PHREP,1,t); st_own_save(S_PREHEAT,1,t); txq_len=0; t=st_tm_loop(t,200,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("schedule ON (every day 06:30): ONE cmd8 pre-arm = " TM_V_ARM,n==1&&!strcmp(fr[0],TM_V_ARM)&&arm.sent&&arm.ad==19&&arm.ah==6&&arm.am==30);
    t=st_tm_loop(t,20000,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("... no repeat while the alarm is unchanged (20 s); the 0x88 echo is counted",n==1&&arm.n_ack==1);
    rps.n_ver++; t=st_tm_loop(t,6000,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("MCU restart (re-push counter): the RAM alarm is gone -> re-armed once",n==2&&!strcmp(fr[1],TM_V_ARM));
    ro_mode=1; arm.sent=0; t=st_tm_loop(t,6000,1); n=st_cmd8_frames(NULL,16,fr); ro_mode=0;
    st_check("read-only mode: no pre-arm",n==2);
    t=st_tm_loop(t,6000,1); n=st_cmd8_frames(NULL,16,fr);
    st_own_save(S_PREHEAT,0,t); t=st_tm_loop(t,6000,1); int n2=st_cmd8_frames(NULL,16,fr); t=st_tm_loop(t,8000,1); int n3=st_cmd8_frames(NULL,16,fr);
    st_check("schedule OFF: ONE disarm frame with alarm 0/0/0, then nothing",n2==n+1&&!strcmp(fr[n2-1],TM_V_OFF0)&&n3==n2&&!arm.sent);
    /* ---- (7) power-off carries the alarm (stock), no pre-arm while off, re-arm after the wake ---- */
    t=st_pw_home(); st_own_save(S_PREHEAT,1,t); t=st_tm_loop(t,300,1); txq_len=0;
    pw_key(116,1,t); t=st_tm_loop(t,1510,1); pw_key(116,0,t); t=st_tm_loop(t,200,1);
    { char all[16][96]; int na=st_txq_frames(all,16);
      st_check("power-off with a schedule: cmd8 = " TM_V_ARM " (alarm 19 06:30, stock performSleepActions) -> cmd6 sub0 len2 " PW_V_OFF " — nothing else",na==2&&!strcmp(all[0],TM_V_ARM)&&!strcmp(all[1],PW_V_OFF)&&pw.state==PW_OFF); }
    txq_len=0; t=st_tm_loop(t,12000,0); n=st_cmd8_frames(NULL,16,fr);
    st_check("OFF: no pre-arm (the power-off frame carries the alarm; the MCU does not listen while asleep)",n==0&&pw.state==PW_OFF);
    reg[0]=1; t=st_tm_loop(t,1000,1); n=st_cmd8_frames(NULL,16,fr);   /* the controller answers again (alarm wake, word 0 = 1) -> pw_on */
    t=st_tm_loop(t,6000,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("wake (word 0 = 1 again = MCU reset): PowerOn + the alarm is re-armed for the NEXT occurrence",pw.state==PW_ON&&n==1&&!strcmp(fr[0],TM_V_ARM));
    /* ---- (8) one-shot ---- */
    t=st_pw_home(); st_own_save(S_PHREP,0,t); st_own_save(S_PREHEAT,1,t);
    { int64_t want=st_utc(2026,11,19,5,30,0); int ad,ah,am; sched_alarm(clk_utc(),&ad,&ah,&am,NULL);
      st_check("ONCE 06:30 saved at Wed 16:35: once_at = Thu 19 05:30 UTC (06:30 local) -> alarm 19 06:30; the file has once_at",sch.once_at==want&&ad==19&&ah==6&&am==30);
      clk_fake=want+30; t=st_tm_loop(t,100,1); int still=sch.on;
      clk_fake=want+61; t=st_tm_loop(t,100,1); settings_load();
      st_check("ONCE: still ON within its minute; a minute later it switches itself OFF and saves it (stock deal_preheat_data), toast ONE-TIME PRE-HEAT USED",still&&!sch.on&&!SET[S_PREHEAT].val&&!strcmp(toast.msg,TR(TX_T_ONCE_USED)));
      clk_fake=PV_CLK; }
    /* ---- (9) auto off ---- */
    t=st_pw_home(); st_own_save(S_PREHEAT,0,t); st_own_save(S_AUTOOFF,1,t); txq_len=0; toast.dur=0; idle_kick(t);
    t=st_tm_loop(t,14*60000+29000,1); int quiet=pw.state==PW_ON&&!toast.dur;
    t=st_tm_loop(t,2000,1); int warn=toast.dur&&!strcmp(toast.msg,TR(TX_T_AUTOOFF_SOON));
    ui_touch(TE_DOWN,240,240,t); ui_touch(TE_UP,240,240,t+50); t=st_tm_loop(t,60000,1); int reset=pw.state==PW_ON&&!toast.dur;
    reg[33]=0x0101|0x1000; reg[22]=1; t=st_tm_loop(t,16*60000,1); int busy=pw.state==PW_ON; reg[33]=0x0101; reg[22]=0;
    txq_len=0; t=st_tm_loop(t,15*60000+500,1); static char all[64][96]; int na=st_txq_frames(all,64);
    st_check("AUTO OFF 15 MIN: quiet until 14:29; the warning toast 30 s before; a touch restarts the timer and dismisses it",quiet&&warn&&reset);
    st_check("... an activity (brew bits) keeps it on (stock isExtractionOrSteamSwitchOn -> recodeTime)",busy);
    st_check("... after 15 min idle: the stock power-off sequence (cmd8 w. alarm 0/0/0 -> cmd6 sub0 len2 " PW_V_OFF ")",pw.state!=PW_ON&&ao_n>=1&&na>=2&&!strcmp(all[na-2],TM_V_OFF0)&&!strcmp(all[na-1],PW_V_OFF));
    st_own_save(S_AUTOOFF,0,st_pw_home());
    /* ---- (10) HOME clock ---- */
    { t=st_pw_home(); SET[S_TIMEMODE].val=1; char c[24]; int v=home_clock_text(c,sizeof c); int a=v&&!strcmp(c,"16:35");
      SET[S_TIMEMODE].val=0; v=home_clock_text(c,sizeof c); a&=v&&!strcmp(c,"04:35 P.M."); SET[S_TIMEMODE].val=1;
      clk_fake=10000; v=home_clock_text(c,sizeof c); clk_fake=PV_CLK;
      st_check("HOME top line: \"16:35\" (timemode 1) / \"04:35 P.M.\" (timemode 0); clock not set -> the wordmark",a&&!v); }
    { t=st_pw_home(); sch.on=1; sch.rep=1; sch.hm=390; sch.once_at=0; char h[72]; int v=home_preheat_text(h,sizeof h); char want[72]; snprintf(want,sizeof want,TR(TX_C_PREHEAT),TR(TX_WD_THU),"06:30");
      home_model(t); st_check("HOME hint: a planned pre-heat is shown as the second hint line \"{clock} PRE-HEAT · THU 06:30\" (the next occurrence)",v&&!strcmp(h,want)&&hm.hint2&&!strcmp(hm.hint2,want));
      sch.on=0; home_model(t); st_check("... no pre-heat: no second line (v6.17: one hint line)",hm.hint2==NULL&&!strcmp(hm.hint1,TR(TX_C_HINT))); }
    unlink(sched_path); snprintf(sched_path,sizeof sched_path,"%s",keep_sp);
    SET[S_TIMEMODE].val=keep_tm; SET[S_TZ].val=keep_tz; SET[S_DST].val=keep_dst; clk_fake=PV_CLK; settings_load();
}
static void st_power(void){
    printf("== selftest: v6.14 the power button (k62091 / event1; stock app_powerKey_thread 0x140AB4 + dealPowerState 0x14911C + powerOff 0x149074; MCU sleep-task 0x2F52) ==\n");
    char fr[16][96]; int nf; ms_t t, t0; uint8_t b[16];
    /* ---- (1) frames byte for byte ---- */
    { struct tm tm; pw_now_tm(&tm); pw_cmd8(b,&tm,0,0,0);
      st_check("cmd8 time/alarm frame = stock powerOffSend(0,0,0): " PW_V_CMD8 " (year u16 LE, month 1-12, day, hour, minute, second, alarm 0/0/0 = none; CRC-16/MODBUS over 13 bytes)",hexeq(b,15,PW_V_CMD8)); }
    { uint16_t w[2]={0,0}; int n=build_write(b,0,w,2); st_check("PowerOff = cmd6 sub0 len2 [word 0 = 0, word 1 = 0]: " PW_V_OFF " (stock powerOff: mirror word 0 = 0, usart_send(6,0,2); word 1 = awake)",hexeq(b,n,PW_V_OFF)); }
    { uint16_t w=1; int n=build_write(b,0,&w,1); st_check("PowerOn = cmd6 sub0 len1 [word 0 = 1]: " PW_V_ON " (stock PowerOn 0x146EF8; same frame as merakiplus's startup)",hexeq(b,n,PW_V_ON)); }
    /* ---- (2) frame policy: word 0 = 0 only with power-off intent ---- */
    { uint16_t z2[2]={0,0}; wq.n=0; int a=wq_push_t(0,z2,2,I_POWEROFF)==1&&wq.n==1; wq.n=0;
      st_check("policy: sub0 len2 [0,0] WITH I_POWEROFF is queued; WITHOUT intent it is rejected (WJ_TRIG) — also as sub0 len1 [0]",a&&rej(0,z2,2,0,WJ_TRIG)&&rej(0,W1(0),1,0,WJ_TRIG));
      st_check("policy: I_POWEROFF allows nothing else — start word (word 9 = 1), reboot (word 30 = 1) and word 1 = 1 (sleep/drain) are still rejected; PowerOn (word 0 = 1) without intent is unchanged OK",
               rej(9,W1(1),1,I_POWEROFF,WJ_TRIG)&&rej(30,W1(1),1,I_POWEROFF,WJ_TRIG)&&rej(1,W1(1),1,I_POWEROFF,WJ_RANGE)&&rej(0,W1(2),1,I_POWEROFF,WJ_RANGE)&&wq_push(0,W1(1),1)&&(wq.n=0,1)); }
    /* ---- (3) short press: nothing ---- */
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1400,1,1); pw_key(116,0,t); t=st_pw_loop(t,3000,1,1);
    st_check("short press (1.4 s < 1.5 s): no frames, ON, counted as a short press (stock: only wakes its screensaver)",txq_len==0&&pw.state==PW_ON&&pw.n_short==1&&pw.n_off==0&&view==V_MAIN);
    /* ---- (4) HOLD: cmd8 -> 80 ms -> cmd6 sub0 len2 -> ACK -> OFF ---- */
    t0=t=st_pw_home(); page=PG_STEAM; static_dirty=1; pw_key(116,1,t); t=st_pw_loop(t,1490,1,0);
    int before=txq_len==0&&pw.state==PW_ON;
    t=st_pw_loop(t,20,1,0); nf=st_txq_frames(fr,16);
    int at=nf==1&&!strcmp(fr[0],PW_V_CMD8)&&pw.state==PW_OFFING&&view==V_OFF&&pw.t_req-t0>=PW_HOLD_MS&&pw.t_req-t0<PW_HOLD_MS+20;
    ms_t treq=pw.t_req; t=st_pw_loop(t,60,1,0); int gap1=st_txq_frames(fr,16)==1;
    t=st_pw_loop(t,30,1,0); nf=st_txq_frames(fr,16); int gap2=nf==2&&!strcmp(fr[1],PW_V_OFF)&&wq.sent_ms-treq>=PW_GAP_MS&&pw.state==PW_OFFING;
    st_check("HOLD: nothing before 1.5 s; at 1.5 s cmd8 (" PW_V_CMD8 ") goes out, and TURNING OFF… is shown (the power-off view, not the settings page)",before&&at);
    st_check("HOLD: cmd6 sub0 len2 = " PW_V_OFF " goes out >= 80 ms after cmd8 (stock: usleep(80000) between the two); TURNING OFF… until ACK",gap1&&gap2);
    st_ack_last(t+5); t=st_pw_loop(t,40,1,0);
    st_check("ACK (0x86 with echo) -> OFF; HOME on the next power-on",pw.state==PW_OFF&&pw.n_off==1&&wq.n==0&&view==V_OFF);
    { uint8_t a[15]; memcpy(a,pw.f8,15); a[2]=0x88; uint16_t c=crc16(a,13); a[13]=c&0xFF; a[14]=c>>8; rx_feed(a,15,t); }
    st_check("the controller's 0x88 receipt for cmd8 (echo) is counted for DIAGNOSE (stock does not wait for it — neither does merakiplus)",pw.n_ack8==1);
    int blon=!bl_is_off; t=st_pw_loop(t,treq+PW_BL_MS-t+10,0,0);
    st_check("the backlight is turned off 1.5 s after the power-off press (stock: lv_tick_elaps > 0x5DC) — not before",blon&&bl_is_off);
    nf=st_txq_frames(fr,16);
    st_check("EXACTLY two frames in the power-off sequence: cmd8 + cmd6 sub0 len2 — no stop frames, no keep-awake, nothing else",nf==2&&!strcmp(fr[0],PW_V_CMD8)&&!strcmp(fr[1],PW_V_OFF));
    /* ---- (5) keep-awake stays idle while powered off ---- */
    txq_len=0; t=st_pw_loop(t,65000,0,1); int quiet0=txq_len==0;
    reg[0]=0; t=st_pw_loop(t,65000,1,1); int quiet1=txq_len==0&&pw.state==PW_OFF;
    st_check("keep-awake paused while OFF: 2 x 65 s (controller silent / answering with word 0 = 0) -> 0 frames (no word 1 = 0, which would fight it)",quiet0&&quiet1);
    { ms_t u=st_pw_home(); u=st_pw_loop(u,65000,1,1); nf=st_txq_frames(fr,16); int nka=0; for(int k=0;k<nf;k++) nka+=!strcmp(fr[k],PW_V_KA);
      st_check("control: ON for 65 s -> keep-awake runs as before (2 x word 1 = 0)",nka==2&&nf==2); }
    /* ---- (6) tap on the powered-off screen ---- */
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); t=st_pw_loop(t,2000,0,1); txq_len=0;
    int off=pw.state==PW_OFF&&bl_is_off;
    ui_touch(TE_DOWN,240,240,t); int peek=!bl_is_off; ui_touch(TE_UP,240,240,t+60); t=st_pw_loop(t,200,0,1);
    int sw=view==V_OFF&&page==PG_HOME&&hit_test(240,372,t)==T_NONE&&hit_test(CX,GEAR_Y,t)==T_NONE&&txq_len==0;
    ui_touch(TE_DOWN,100,240,t); ui_touch(TE_MOVE,300,240,t+30); ui_touch(TE_UP,300,240,t+60); int noswipe=view==V_OFF&&page==PG_HOME&&tr.type==0;
    t=st_pw_loop(t,PW_PEEK_MS+100,0,1);
    st_check("tap on the powered-off screen: the light turns on (the OFF page for 8 s), the tap/swipe is swallowed (no page, no frames), the light turns off again",off&&peek&&sw&&noswipe&&bl_is_off&&txq_len==0);
    /* ---- (7) POWER ON: the button -> TURNING ON… -> the controller answers with word 0 = 1 -> PowerOn + awake -> HOME ---- */
    reg[0]=0; pw_key(116,1,t); int wk=pw.state==PW_WAKING&&!bl_is_off; t=st_pw_loop(t,800,1,1);
    int still=pw.state==PW_WAKING&&txq_len==0;                                     /* word 0 = 0 (old reply): not awake */
    reg[0]=1; t=st_pw_loop(t,PW_SEEN_MS-100,1,0); int wait=pw.state==PW_WAKING;
    t=st_pw_loop(t,200,1,1); t=st_pw_loop(t,200,1,1); pw_key(116,0,t); nf=st_txq_frames(fr,16);
    st_check("POWER ON: the button while OFF -> light on + TURNING ON… (no frames: the controller is asleep and cannot receive)",wk&&still);
    st_check("POWER ON: only once the controller has answered with word 0 = 1 for 0.6 s -> PowerOn " PW_V_ON " + awake " PW_V_KA " (as at startup) -> ON, HOME, green toast",
             wait&&nf==2&&!strcmp(fr[0],PW_V_ON)&&!strcmp(fr[1],PW_V_KA)&&pw.state==PW_ON&&view==V_MAIN&&page==PG_HOME&&!strcmp(toast.msg,TR(TX_T_PW_ON))&&pw.n_on==1&&!bl_is_off);
    txq_len=0; t=st_pw_loop(t,31000,1,1); nf=st_txq_frames(fr,16);
    st_check("after POWER ON keep-awake runs again (word 1 = 0 after 30 s)",nf==1&&!strcmp(fr[0],PW_V_KA));
    /* ---- (8) POWER ON without a reply: back to OFF after 10 s ---- */
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); t=st_pw_loop(t,2000,0,1); txq_len=0;
    pw_key(116,1,t); t=st_pw_loop(t,300,0,1); pw_key(116,0,t); t=st_pw_loop(t,PW_WAKE_MS,0,1);
    st_check("POWER ON, but the controller does not answer (button released too early): after 10 s back to OFF, light off, 0 frames",pw.state==PW_OFF&&pw.n_wto==1&&bl_is_off&&txq_len==0);
    /* ---- (9) the controller wakes up by itself (alarm / button merakiplus did not see) ---- */
    reg[0]=1; t=st_pw_loop(t,PW_SEEN_MS+100,1,1); nf=st_txq_frames(fr,16);
    st_check("OFF, and the controller answers by itself with word 0 = 1 -> ON + PowerOn/awake (no eternal OFF screen)",pw.state==PW_ON&&pw.n_auto==1&&nf>=1&&!strcmp(fr[0],PW_V_ON)&&view==V_MAIN);
    /* ---- (10) blocked as in stock: brew / steam / grind; hot water does NOT block and is not stopped ---- */
    { static const struct { int w; uint16_t v, ws; const char*nm; } B[]={{22,1,0x1307,"brew (word 22, work_sta 1307)"},{24,1,0x4121,"steam (word 24, work_sta 4121)"},{6,1,0x0141,"grind (word 6)"},{0,0,0x1307,"bit 12 alone (physical brew switch)"}};
      int ok=1;
      for(unsigned q=0;q<sizeof B/sizeof B[0];q++){
          t=st_pw_home(); if(B[q].w) reg[B[q].w]=B[q].v; reg[33]=B[q].ws; pw_key(116,1,t); t=st_pw_loop(t,4000,1,1);
          int blk=txq_len==0&&pw.state==PW_ON&&pw.n_blk==1&&!strcmp(toast.msg,TR(TX_T_PW_BUSY))&&toast.col==C_WARN;
          if(B[q].w) reg[B[q].w]=0; reg[33]=0x0101; t=st_pw_loop(t,1400,1,1); int notyet=txq_len==0;
          t=st_pw_loop(t,300,1,1); nf=st_txq_frames(fr,16); pw_key(116,0,t);
          printf("       %-36s blocked %s · after the activity + full hold: %s\n",B[q].nm,blk?"yes":"NO",nf>=1&&!strcmp(fr[0],PW_V_CMD8)?"off":"-");
          if(!(blk&&notyet&&nf>=1&&!strcmp(fr[0],PW_V_CMD8))) ok=0; }
      st_check("BLOCKED during brew / steam / grind (stock isExtractionOrSteamSwitchOn 0x1481E0: work_sta bit 12/14, words 22/24/6): nothing is sent, toast 'STOP THE ACTIVITY FIRST'; once it has ended, a NEW full hold powers off",ok); }
    t=st_pw_home(); reg[16]=1; reg[33]=0x030B; pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); t=st_pw_loop(t,300,1,1); nf=st_txq_frames(fr,16);
    { int stop=0; for(int k=0;k<nf;k++) if(strstr(fr[k],"aa 55 06 10")||strstr(fr[k],"aa 55 06 16")||strstr(fr[k],"aa 55 06 06")) stop=1;
      st_check("hot water does NOT block (not in stock either) and is not stopped by merakiplus: only cmd8 + sub0 len2 — the MCU's sleep task resets work_sta + outputs (0x2F52/0x2F0C)",
               nf==2&&!strcmp(fr[0],PW_V_CMD8)&&!strcmp(fr[1],PW_V_OFF)&&!stop&&pw.state==PW_OFF); }
    /* ---- (11) no connection / word 0 = 0 / read-only: nothing is sent ---- */
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1600,0,1); pw_key(116,0,t); int nl=txq_len==0&&pw.state==PW_ON&&!strcmp(toast.msg,TR(TX_T_PW_NOLINK));
    t=st_pw_home(); reg[0]=0; pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); int z0=txq_len==0&&pw.state==PW_ON;
    t=st_pw_home(); ro_mode=1; pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); int ro=txq_len==0&&pw.state==PW_ON&&!strcmp(toast.msg,TR(TX_G_RO)); ro_mode=0;
    st_check("no connection -> toast, nothing is sent (the button may just be waking a sleeping controller); word 0 = 0 -> nothing (like stock); READ-ONLY -> nothing",nl&&z0&&ro);
    /* ---- (12) no ACK -> back to ON with an error toast ---- */
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1600,1,0); pw_key(116,0,t); t=st_pw_loop(t,600,1,0); nf=st_txq_frames(fr,16);
    st_check("no ACK (not on the retransmit either): back to ON/HOME, light never turned off, toast 'POWER OFF: NO REPLY'",
             pw.state==PW_ON&&pw.n_fail==1&&!bl_is_off&&view==V_MAIN&&!strcmp(toast.msg,TR(TX_T_PW_NOACK))&&toast.col==C_ERR&&nf==3&&!strcmp(fr[1],PW_V_OFF)&&!strcmp(fr[2],PW_V_OFF));
    /* ---- (13) open detail with brightness preview: reverted before powering off ---- */
    t=st_pw_home(); page=PG_SYS; sec_open=SEC_DISPLAY; static_dirty=1; view=V_DETAIL; sel=S_BL; edit_val=SET[S_BL].val+1; pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); t=st_pw_loop(t,300,1,1);
    st_check("power off in the middle of an open detail: the detail is closed without SAVE (sel -1), TURNING OFF…/OFF is shown",view==V_OFF&&sel<0&&pw.state==PW_OFF&&cfg_writes>=0);
    /* ---- (14) texts and layout ---- */
    { int ok=1; static const short T[]={TX_PW_OFFING,TX_PW_OFF,TX_PW_WAKING,TX_PW_SUB_OFFING,TX_PW_SUB_OFF,TX_PW_SUB_WAKING,TX_PW_NOTE_OFF};
      for(int lg=0;lg<NLANG;lg++) for(unsigned k=0;k<sizeof T/sizeof T[0];k++){ float w=text_wf(STR[lg][T[k]],k<3?F_M:F_XS); if(w>340){ ok=0; printf("     [%s] %s: %.0f px\n",LANG_CODE[lg],STR_ID[T[k]],w); } }
      st_check("TURNING OFF / OFF / TURNING ON + sublines fit in the field (<= 340 px) in EN and DA",ok); }
    { int ok=DG_RAW+28+4*DG_P<=DG_PWR&&DG_PWR+28+3*DG_P<=DG_ABOUT&&DG_ABOUT+76+24<DG_BOTTOM;
      char a[28],c[28],d[28],e[40],f[80]; uint32_t col; diag_pw_text(a,&col,c,d,e,f,sizeof f,t);
      printf("       DIAGNOSE POWER: y %d (RAW COUNTERS %d, ABOUT %d) · '%s' · word 0 '%s' · '%s' · '%s' · '%s'\n",DG_PWR,DG_RAW,DG_ABOUT,a,c,d,e,f);
      st_check("DIAGNOSE: POWER BUTTON block between RAW COUNTERS and ABOUT (state, word 0, presses · off · on, latest key, node)",ok&&!strcmp(a,TR(TX_DV_PW_OFF))&&strstr(f,KBD_NAME)!=NULL); }
    t=st_pw_home(); pw_key(116,1,t); t=st_pw_loop(t,1600,1,1); pw_key(116,0,t); t=st_pw_loop(t,3000,0,1); static_dirty=1; force_all=1; ui_frame(t);
    { int black=1; static const int P[][2]={{240,40},{240,440},{40,240},{440,240},{240,370}}; for(unsigned k=0;k<5;k++) if((canvas[P[k][1]*W+P[k][0]]&0xFFFFFF)!=0) black=0;
      st_check("the OFF page is black outside the symbol and text (no arcs, no HOME leftovers)",black&&!garc.vis&&!parc.vis); }
    sim_reset(); sim_tick=NULL;
}
/* ============================ v6.13: manual STOP for grind / brew / water ============================ */
#define ST_V_GRIND "aa 55 06 06 01 00 00 c4 47"
#define ST_V_BREW  "aa 55 06 16 01 00 00 c0 87"
#define ST_V_WATER "aa 55 06 10 01 00 00 c0 0f"
#define ST_V_WMAN  "aa 55 06 13 01 00 00 c0 4b"
#define ST_V_STEAM "aa 55 06 18 08 00 00 8a 02 00 00 00 00 00 00 00 00 00 00 00 00 0c f7"
static int st_tx_is(const char*hex){ return hexeq(last_tx,last_tx_len,hex); }
/* tap (down + up) at (x,y) like a finger: activation happens on release, as on the screen */
static ms_t st_tap(int x,int y,ms_t t){ ui_touch(TE_DOWN,x,y,t); ui_frame(t+=33); ui_touch(TE_UP,x,y,t+=40); ui_frame(t+=33); return t; }
/* the touch area's width along y and height along x for a given target (measured via hit_test, like v6.11's UX check) */
static void st_hit_size(int tgt,int x,int y,ms_t t,int*w,int*h){ *w=*h=0; for(int i=0;i<W;i++) if(hit_test(i,y,t)==tgt) (*w)++; for(int i=0;i<H;i++) if(hit_test(x,i,t)==tgt) (*h)++; }
static void st_stop(void){
    printf("== selftest: v6.13 manual STOP for grind / brew / water (stop words reversed from the MCU firmware + the stock UI's own stop calls) ==\n");
    ms_t t; int sk[2], sn;
    char stops[40]; snprintf(stops,sizeof stops,"%s",STR[ui_lang][TX_H_AUTOSTOP_G]); { char*q=strstr(stops,"%s"); if(q) *q=0; }   /* "STOPS AT " / "STOPPER VED " */
    /* ---- (1) frames byte for byte ---- */
    t=st_home();
    act_push_stop(ACT_GRIND); st_check("GRIND stop = cmd6 sub6 len1, word 6 = 0: " ST_V_GRIND " (MCU grind-ctl 0x19C6 runs while word 6 != 0; stock onClick 0x10928C usart_send(6,6,1))",wq.n==1&&q_hex(0,ST_V_GRIND)); wq.n=0;
    act_push_stop(ACT_BREW);  st_check("BREW stop  = cmd6 sub22 len1, word 22 = 0: " ST_V_BREW " (MCU brew-ctl 0x1E9C: running = word 22 != 0 on change; auto-stop 0x1F92 writes the same)",wq.n==1&&q_hex(0,ST_V_BREW)); wq.n=0;
    reg_seen[19]=1; reg[19]=0;
    act_push_stop(ACT_WATER); st_check("WATER stop = cmd6 sub16 len1, word 16 = 0: " ST_V_WATER " (MCU hotwater-ctl 0x1B8C: active = word 19 | word 16; stock hotWaterStartStop 0x11468C usart_send(6,16,1))",wq.n==1&&q_hex(0,ST_V_WATER)); wq.n=0;
    reg[19]=1; act_push_stop(ACT_WATER);
    st_check("WATER stop with word 19 != 0 (manual mode, stock): + sub19 len1 word 19 = 0: " ST_V_WMAN " (stock 0x114720 usart_send(6,19,1)) — both in the same queue round",wq.n==2&&q_hex(0,ST_V_WATER)&&q_hex(1,ST_V_WMAN)); wq.n=0; reg[19]=0;
    act_push_stop(ACT_STEAM); st_check("STEAM stop = unchanged (sub24 len8, word 24 = 0)",wq.n==1&&q_hex(0,ST_V_STEAM)); wq.n=0;
    /* ---- (2) policy: a stop frame can never start anything and is never rejected ---- */
    { int ok=1; for(int k=ACT_GRIND;k<=ACT_STEAM;k++){ wq.n=0; reg_seen[19]=1; reg[19]=1; if(!act_push_stop(k)){ ok=0; continue; }
          for(int q=0;q<wq.n;q++){ const WFrame*f=&wq.q[q]; if(!frame_trig_audit(f,0)||frame_policy(f->sub,f->w,f->len,I_NONE)) ok=0;
              for(int i=0;i<f->len;i++) if(f->w[i]!=0&&!(f->sub==24&&i==1)) ok=0;                       /* zeros only (steam: the target word) */
              if(f->sub!=24&&(frame_covers(f,26)||frame_covers(f,27)||frame_covers(f,30))) ok=0; } }   /* the steam block covers 24..31 — there they are zeros (checked above) */
      wq.n=0; reg[19]=0;
      st_check("policy: all stop frames are zeros (steam: + the target word), pass frame_policy WITHOUT intent and cover words 26/27/30 only in the steam block (as zeros)",ok); }
    st_check("policy: the same words with value 1 WITHOUT intent are still rejected (sub6=1, sub22=1, sub16=1: WJ_TRIG; sub19=1: WJ_RANGE) — the STOP path cannot start anything",
             rej(6,W1(1),1,0,WJ_TRIG)&&rej(22,W1(1),1,0,WJ_TRIG)&&rej(16,W1(1),1,0,WJ_TRIG)&&rej(19,W1(1),1,0,WJ_RANGE));
    /* ---- (3) visible per phase + touch area >= 48 px + subline "· OR STOP" ---- */
    { static const struct { uint16_t ws; int kind; const char*nm; } P[]={{0x0141,ACT_GRIND,"grind 0141"},{0x1107,ACT_BREW,"pre-inf. 1107"},{0x1307,ACT_BREW,"extraction 1307"},{0x030B,ACT_WATER,"water 030B"}};
      int ok=1, okw=1, oks=1;
      for(unsigned q=0;q<sizeof P/sizeof P[0];q++){
          t=st_home(); st_s0(P[q].ws,0,0x000C,0,300,t+=100); ui_frame(t); sn=stop_btns(t,sk);
          int w,h; st_hit_size(T_STOP,240,372,t,&w,&h);
          printf("       %-17s STOP button: %s · touch area %d x %d px\n",P[q].nm,sn?TR(STOP_BTN[sk[0]]):"(none)",w,h);
          if(!(sn==1&&sk[0]==P[q].kind&&hit_test(240,372,t)==T_STOP&&hit_test(ACT_X[0],ACT_Y,t)!=T_ACT0&&hit_test(ACT_X[2],ACT_Y,t)!=T_ACT2)) ok=0;
          if(w<48||h<48) okw=0;
          if(strncmp(hm.sub,stops,strlen(stops))||st_has(hm.sub,"\xC2\xB7")||sub_font(&hm)!=F_S) oks=0; }
      st_check("STOP is shown during GRIND, PRE-INF., EXTRACTION and WATER (one button, same place as STOP STEAM; no start icons)",ok);
      st_check("touch: the STOP button's touch area is >= 48 x 48 px (232 x 60)",okw);
      st_check("the subline is the auto-stop expectation and nothing else ('STOPS AT 36.0 G' / 'STOPPER VED 36.0 G' in F_S — v6.13-v6.16: 'AUTO-STOP AT 36.0 G · OR STOP' in F_XS; the STOP button is right below)",oks); }
    t=st_home(); st_s0(0x0101,0,0x000C,0,300,t+=100); ui_frame(t); sn=stop_btns(t,sk);
    st_check("idle (0101): no STOP button — the start icons are there",sn==0&&hit_test(ACT_X[0],ACT_Y,t)==T_ACT0&&hit_test(240,372,t)!=T_STOP);
    st_s0(0x4121,1,0x000C,0,300,t+=100); ui_frame(t); sn=stop_btns(t,sk);
    st_check("latched steam alone: one button = STOP STEAM (unchanged behaviour, same touch area as the other STOP buttons)",sn==1&&sk[0]==ACT_STEAM&&hit_test(240,372,t)==T_STOP&&hit_test(150,372,t)==T_STOP&&hit_test(330,372,t)==T_STOP&&hit_test(360,372,t)==T_NONE);
    /* ---- (4) WATER all the way: tap -> STOPPING WATER… -> frame -> retransmit -> ACK -> bits cleared -> WATER STOPPED ---- */
    t=st_home(); reg[16]=1; reg[17]=400; reg_seen[19]=1; reg[19]=0;
    st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t);
    t=st_tap(240,372,t);
    int q1=wq.n==1&&q_hex(0,ST_V_WATER)&&!strcmp(toast.msg,TR(TX_T_STOP_WATER))&&toast.col==C_TXT&&stp.active&&stp.kind==ACT_WATER;
    wq_step(t); int tx1=st_tx_is(ST_V_WATER); int l0=txq_len;
    wq_step(t+100); int noretx=txq_len==l0; wq_step(t+160); int retx=txq_len==l0+9&&wq.n_retx==1&&st_tx_is(ST_V_WATER);
    st_ack_last(t+190); ui_frame(t+=200);
    int acked=wq.n==0&&wq.result==WR_OK&&reg[16]==0&&stp.active&&!strcmp(toast.msg,TR(TX_T_STOP_WATER));   /* an ACK alone is not a confirmation: the bits are still set */
    st_check("WATER: tap STOP -> toast 'STOPPING WATER…' / 'STOPPER VAND…' immediately, and the stop frame is queued",q1);
    st_check("WATER: the frame is sent byte for byte; no ACK within 150 ms -> exactly one retransmit (same bytes); ACK with echo -> queue free",tx1&&noretx&&retx&&acked);
    st_s0(0x0101,0,0x000C,0,300,t+=150); ui_frame(t); sn=stop_btns(t,sk);
    st_check("WATER: the MCU clears bits 1+3 (030B -> 0101) -> toast 'WATER STOPPED' / 'VAND STOPPET' (green), DONE view, STOP button gone",
             !strcmp(toast.msg,TR(TX_T_STOPPED_WATER))&&toast.col==C_OK&&!stp.active&&stp.n_ok==1&&stp.n_fail==0&&act.ph==PH_DONE&&act.last_ph==PH_WATER&&sn==0);
    /* ---- (5) BREW and GRIND the same way ---- */
    { static const struct { uint16_t ws; int kind; const char*v; short tx_s, tx_d; } P[]={{0x1307,ACT_BREW,ST_V_BREW,TX_T_STOP_BREW,TX_T_STOPPED_BREW},{0x0141,ACT_GRIND,ST_V_GRIND,TX_T_STOP_GRIND,TX_T_STOPPED_GRIND}};
      for(unsigned q=0;q<2;q++){
          t=st_home(); if(P[q].kind==ACT_BREW) reg[22]=1; else reg[6]=1;
          st_s0(P[q].ws,0,0x000C,0,300,t+=100); ui_frame(t); t=st_tap(240,372,t);
          int a=wq.n==1&&q_hex(0,P[q].v)&&!strcmp(toast.msg,TR(P[q].tx_s)); wq_step(t); int b=st_tx_is(P[q].v); st_ack_last(t+25); ui_frame(t+=40);
          int c=wq.n==0&&wq.result==WR_OK&&(P[q].kind==ACT_BREW?reg[22]==0:reg[6]==0)&&stp.active;
          st_s0(0x0101,0,0x000C,0,300,t+=150); ui_frame(t);
          char nm[200]; snprintf(nm,sizeof nm,"%s: tap STOP -> '%s' -> %s sent + ACK -> bits cleared -> '%s'",P[q].kind==ACT_BREW?"BREW (1307)":"GRIND (0141)",TR(P[q].tx_s),P[q].kind==ACT_BREW?"sub22":"sub6",TR(P[q].tx_d));
          st_check(nm,a&&b&&c&&!strcmp(toast.msg,TR(P[q].tx_d))&&toast.col==C_OK&&!stp.active&&act.ph==PH_DONE); } }
    /* brew: the MCU's valve pulse after stop (BREW.md §4: 0105 for 0.21 s) is not a brew — no STOP button, no new phase */
    t=st_home(); reg[22]=1; st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); t=st_tap(240,372,t); wq_step(t); st_ack_last(t+25);
    st_s0(0x0101,0,0x000C,0,300,t+=150); ui_frame(t); st_s33(0x0105,t+=100); ui_frame(t); sn=stop_btns(t,sk);
    st_check("BREW: the valve pulse after stop (work_sta 0105, only bit 2) gives no STOP button and no new phase",sn==0&&act.ph==PH_DONE&&stp.n_ok==1);
    /* ---- (6) failure: no ACK -> error toast, the STOP button stays (the bits are still set) ---- */
    t=st_home(); st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); t=st_tap(240,372,t);
    wq_step(t); wq_step(t+160); wq_step(t+470); st_s33(0x030B,t+=480); ui_frame(t); sn=stop_btns(t,sk);
    st_check("no ACK (not on the retransmit either): toast 'STOP: NO REPLY · TRY AGAIN' (red); the STOP button stays, so you can try again",
             wq.result==WR_FAIL&&!strcmp(toast.msg,TR(TX_T_STOP_NOACK))&&toast.col==C_ERR&&!stp.active&&stp.n_fail==1&&sn==1&&sk[0]==ACT_WATER);
    t=st_tap(240,372,t); wq_step(t); st_ack_last(t+20); st_s0(0x0101,0,0x000C,0,300,t+=150); ui_frame(t);
    st_check("… a second tap works: new frame, ACK, bits cleared -> 'WATER STOPPED'",!strcmp(toast.msg,TR(TX_T_STOPPED_WATER))&&stp.n_ok==1&&stp.n_req==2);
    /* ---- (7) ACK, but the bits are not cleared (e.g. a brew from a physical switch: word 22 was 0) -> after 5 s 'STOP NOT CONFIRMED' ---- */
    t=st_home(); st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); t=st_tap(240,372,t); wq_step(t); st_ack_last(t+20);
    { int mid=1; for(int q=0;q<40&&t-stp.t_req<STOP_CONFIRM_MS-250;q++){   /* at most 40 rounds (10 s), even if STOP was never requested */ st_s33(0x1307,t+=250); ui_frame(t); if(!stp.active) mid=0; }
      for(int q=0;q<3;q++){ st_s33(0x1307,t+=250); ui_frame(t); }
      st_check("ACK but the bits are not cleared within 5 s: toast 'STOP NOT CONFIRMED · TRY AGAIN' (yellow) — no false 'STOPPED'",
               mid&&!stp.active&&!strcmp(toast.msg,TR(TX_T_STOP_UNCONF))&&toast.col==C_WARN&&stp.n_ok==0&&stp.n_fail==1); }
    /* ---- (8) double tap while the frame is in the queue: no extra frame ---- */
    t=st_home(); st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); t=st_tap(240,372,t); wq_step(t); t=st_tap(240,372,t+30);
    st_check("double tap: a second tap while the stop frame waits for its ACK does not queue another frame (only the toast is repeated)",wq.n==1&&stp.n_req==1&&!strcmp(toast.msg,TR(TX_T_STOP_WATER)));
    /* ---- (9) two buttons: brew + latched steam ---- */
    t=st_home(); reg[22]=1; st_s0(0x1307,0,0x000C,0,300,t+=100); ui_frame(t); st_s0(0x5327,1,0x000C,0,310,t+=100); ui_frame(t); sn=stop_btns(t,sk);
    { int wl,hl,wr,hr; st_hit_size(T_STOP,160,372,t,&wl,&hl); st_hit_size(T_STOPS,320,372,t,&wr,&hr);
      printf("       two buttons: %s %d x %d px · %s %d x %d px\n",sn>0?TR(STOP_BTN[sk[0]]):"-",wl,hl,sn>1?TR(STOP_BTN[sk[1]]):"-",wr,hr);
      st_check("brew + latched steam (5327, word 24 = 1): TWO buttons — left STOP BREW, right STOP STEAM — each >= 48 x 48 px",sn==2&&sk[0]==ACT_BREW&&sk[1]==ACT_STEAM&&wl>=48&&hl>=48&&wr>=48&&hr>=48); }
    t=st_tap(160,372,t); int lft=wq.n==1&&q_hex(0,ST_V_BREW)&&!strcmp(toast.msg,TR(TX_T_STOP_BREW)); wq_step(t); st_ack_last(t+20); ui_frame(t+=30);
    t=st_tap(320,372,t); int rgt=wq.n==1&&q_hex(0,ST_V_STEAM)&&!strcmp(toast.msg,TR(TX_T_STOP_SENT));
    st_check("the left button sends the brew stop (sub22), the right the steam stop (sub24) — a frame each",lft&&rgt);
    /* ---- (10) AUTO-STOP is unchanged: the MCU stops by itself -> merakiplus sends nothing and shows no stop toast ---- */
    { int ok=1; static const uint16_t ws[]={0x030B,0x1307,0x0141};
      for(int q=0;q<3;q++){ t=st_home(); st_s0(ws[q],0,0x000C,0,300,t+=100); ui_frame(t); for(int k=0;k<5;k++){ st_s33(ws[q],t+=200); ui_frame(t); wq_step(t); }
          st_s0(0x0101,0,0x000C,0,300,t+=200); ui_frame(t); wq_step(t); if(!(act.ph==PH_DONE&&wq.n==0&&txq_len==0&&wq.n_ok==0&&stp.n_req==0&&toast.dur==0)) ok=0; }
      st_check("AUTO-STOP unchanged (water / brew / grind): the MCU clears the bits itself -> DONE; merakiplus has not sent a single frame and shows no stop toast",ok); }
    /* ---- (11) read-only mode ---- */
    t=st_home(); st_s0(0x030B,0,0x000C,0,300,t+=100); ui_frame(t); ro_mode=1; t=st_tap(240,372,t); ro_mode=0;
    st_check("MERAKIPLUS_READONLY=1: STOP sends nothing, the toast says why (read-only)",wq.n==0&&stp.n_req==0&&!strcmp(toast.msg,TR(TX_G_RO)));
    sim_reset();
}
/* v6.20 R5: a pre-arm / disarm cmd8 whose 0x88 echo is lost is sent again (at most 2 more copies, ARM_GAP_MS apart); with the echo nothing changes */
static int st_alarm_of(const char*f,int*d,int*h,int*m){ unsigned v[15]; if(sscanf(f,"%x %x %x %x %x %x %x %x %x %x %x %x %x",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9],&v[10],&v[11],&v[12])!=13) return 0; *d=(int)v[10]; *h=(int)v[11]; *m=(int)v[12]; return 1; }
static void st_r5(const char*dir){
    char keep_sp[256]; snprintf(keep_sp,sizeof keep_sp,"%s",sched_path); snprintf(sched_path,sizeof sched_path,"%s/schedule_r5",dir); unlink(sched_path);
    int keep_tz=SET[S_TZ].val, keep_dst=SET[S_DST].val; SET[S_TZ].val=48; SET[S_DST].val=0;
    char fr[16][96]; int n, d,h,m, ok; ms_t t;
    /* (a) first arm, echo lost: 3 copies of the same alarm, then quiet */
    sim_reset(); t=st_pw_home(); st_echo8=0; st_own_save(S_PHREP,1,t); st_own_save(S_PREHEAT,1,t); txq_len=0; t=st_tm_loop(t,30000,1); n=st_cmd8_frames(NULL,16,fr);
    ok=n==3; for(int i=0;i<n;i++){ ok&=st_alarm_of(fr[i],&d,&h,&m)&&d==19&&h==6&&m==30; }
    st_check("R5 pre-arm (daily 06:30) with the 0x88 echo LOST: the same alarm 19 06:30 is sent 3 times in total (2 retries, >= 5 s apart), then nothing in 30 s",ok&&arm.n_retry==2&&arm.n_ack==0);
    /* (b) disarm, echo lost: 3 x 0/0/0 */
    st_echo8=1; sim_reset(); t=st_pw_home(); st_own_save(S_PHREP,1,t); st_own_save(S_PREHEAT,1,t); t=st_tm_loop(t,300,1); int armed=arm.n_ack==1;
    st_echo8=0; txq_len=0; st_own_save(S_PREHEAT,0,t); t=st_tm_loop(t,30000,1); n=st_cmd8_frames(NULL,16,fr);
    ok=n==3; for(int i=0;i<n;i++){ ok&=st_alarm_of(fr[i],&d,&h,&m)&&d==0&&h==0&&m==0; }
    st_check("R5 disarm (PRE-HEAT OFF) with the echo LOST: alarm 0/0/0 is sent 3 times in total — the MCU must not keep the old 06:30 alarm",armed&&ok);
    /* (c) re-arm 06:30 -> 08:00, the first echo lost, the second arrives: exactly 2 frames */
    st_echo8=1; sim_reset(); t=st_pw_home(); st_own_save(S_PHREP,1,t); st_own_save(S_PREHEAT,1,t); t=st_tm_loop(t,300,1);
    st_echo8=0; txq_len=0; st_own_save(S_PHAT,480,t); t=st_tm_loop(t,7000,1); int first=arm.n_ack==1&&!arm.acked; st_echo8=1; t=st_tm_loop(t,30000,1); n=st_cmd8_frames(NULL,16,fr);
    ok=n==2; for(int i=0;i<n;i++){ ok&=st_alarm_of(fr[i],&d,&h,&m)&&d==19&&h==8&&m==0; }
    st_check("R5 re-arm 06:30 -> 08:00: first echo lost, the retry is echoed -> exactly 2 frames (alarm 19 08:00), then quiet",first&&ok&&arm.acked);
    /* (d) with the echo: unchanged traffic (one frame) */
    sim_reset(); t=st_pw_home(); txq_len=0; st_own_save(S_PHREP,1,t); st_own_save(S_PREHEAT,1,t); t=st_tm_loop(t,30000,1); n=st_cmd8_frames(NULL,16,fr);
    st_check("R5 with the echo: ONE pre-arm frame, no retry (the v6.16-v6.19 traffic)",n==1&&arm.n_retry==0&&arm.n_ack==1);
    st_own_save(S_PREHEAT,0,t); t=st_tm_loop(t,300,1);
    /* R7 (review persist #4): a ONE-SHOT saved while the SoC clock is unset is armed once the clock becomes valid, and used up as usual */
    { sim_reset(); t=st_pw_home(); clk_fake=10000; st_own_save(S_PHAT,390,t); st_own_save(S_PHREP,0,t); st_own_save(S_PREHEAT,1,t);
      int saved=sch.on&&sch.once_at==0&&!strcmp(toast.msg,TR(TX_T_NOCLOCK)); txq_len=0; t=st_tm_loop(t,6000,1); int quiet=st_cmd8_frames(NULL,16,fr)==0;
      clk_fake=PV_CLK; t=st_tm_loop(t,6000,1); n=st_cmd8_frames(NULL,16,fr); int64_t want=st_utc(2026,11,19,5,30,0);
      char rb[256]; int fd=open(sched_path,O_RDONLY); int rn=fd>=0?(int)read(fd,rb,sizeof rb-1):-1; if(fd>=0) close(fd); if(rn<0) rn=0; rb[rn]=0; char ws[64]; snprintf(ws,sizeof ws,"once_at=%lld",(long long)want);
      int armed=sch.once_at==want&&n==1&&st_alarm_of(fr[0],&d,&h,&m)&&d==19&&h==6&&m==30&&strstr(rb,ws);
      clk_fake=want+61; t=st_tm_loop(t,200,1); int used=!sch.on&&!strcmp(toast.msg,TR(TX_T_ONCE_USED)); clk_fake=PV_CLK;
      st_check("R7 ONCE 06:30 saved with the clock unset (NO CLOCK toast, nothing sent): when the clock becomes valid it is armed for the next 06:30 (once_at saved, cmd8 alarm 19 06:30) and used up a minute after it",
               saved&&quiet&&armed&&used); }
    st_echo8=1; unlink(sched_path); snprintf(sched_path,sizeof sched_path,"%s",keep_sp); SET[S_TZ].val=keep_tz; SET[S_DST].val=keep_dst; settings_load(); sim_reset();
}
static int selftest(const char*cfgsrc){
    int base_lang=ui_lang;
    printf("== selftest (UI language during the test: %s) ==\n",LANG_CODE[ui_lang]);
    printf("== selftest: frame vectors (observed bytes) ==\n");
    snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load();
    st_check("config loaded",cfg_ok);
    memset(reg_seen,0,sizeof reg_seen); memset(&wq,0,sizeof wq);
    uint8_t b[80]; int n;
    uint16_t aw=0; n=build_write(b,1,&aw,1);
    st_check("keep-awake word1=0 = AA 55 06 01 01 00 00 C5 33 (SESSION §10.1)",hexeq(b,n,"aa 55 06 01 01 00 00 c5 33"));
    act_push(ACT_GRIND);
    st_check("GRIND sub3 len4    = BREW.md W1",wq.n==1&&q_hex(0,"aa 55 06 03 04 01 00 b9 00 3c 00 01 00 5d 6e")); wq.n=0;
    act_push(ACT_BREW);
    st_check("BREW sub5 len18    = BREW.md W3",wq.n==1&&q_hex(0,"aa 55 06 05 12 3c 00 00 00 00 00 00 00 01 00 00 00 66 03 00 00 3c 00 03 00 3c 00 00 00 90 01 00 00 00 00 00 00 00 00 01 00 21 df")); wq.n=0;
    act_push(ACT_WATER);
    st_check("WATER sub9 len11   = HOTWATER.md W1",wq.n==1&&q_hex(0,"aa 55 06 09 0b 01 00 00 00 66 03 00 00 3c 00 03 00 3c 00 01 00 90 01 00 00 00 00 08 3e")); wq.n=0;
    act_push(ACT_STEAM);
    st_check("STEAM start sub24  = SESSION §10.2",wq.n==1&&q_hex(0,"aa 55 06 18 08 01 00 8a 02 00 00 00 00 00 00 00 00 00 00 00 00 cd 67")); wq.n=0;
    act_push_steam_stop();
    st_check("STEAM stop sub24   = SESSION §10.3",wq.n==1&&q_hex(0,"aa 55 06 18 08 00 00 8a 02 00 00 00 00 00 00 00 00 00 00 00 00 0c f7")); wq.n=0;
    uint16_t bad=1; st_check("write band: word 33 rejected",rej(33,&bad,1,0,WJ_BAND)); st_check("write band: word 32 rejected",rej(32,&bad,1,0,WJ_BAND));
    st_check("write band: sub30 len4 rejected",rej(30,(uint16_t[4]){0,0,0,0},4,0,WJ_BAND)); st_check("write band: sub115 rejected",rej(115,&bad,1,0,WJ_BAND));
    st_check("write band: sub112 len3 ok",wq_push(112,(uint16_t[3]){0x104,0x605,70},3)); wq.n=0;
    /* ACK + one retransmit + failure */
    ms_t t=1000; wq_push(11,(uint16_t[1]){880},1); wq_step(t); int l0=txq_len; wq_step(t+100); int l1=txq_len; wq_step(t+160); int l2=txq_len;
    st_check("retransmit: none before 150 ms, exactly one after",l1==l0&&l2==l0+9);
    wq_step(t+300); wq_step(t+470); st_check("failure after the 2nd timeout (queue emptied, result=FAIL)",wq.n==0&&wq.result==WR_FAIL&&txq_len==l2);
    wq_push(11,(uint16_t[1]){880},1); wq_step(t+500);
    uint8_t ack[9]={0xAA,0x55,0x86,11,1,0x70,0x03,0,0}; uint16_t c=crc16(ack,7); ack[7]=c&0xFF; ack[8]=c>>8; rx_feed(ack,9,t+520);
    st_check("ACK 0x86 with echo releases the queue (result=OK)",wq.n==0&&wq.result==WR_OK&&reg[11]==880);
    memset(reg_seen,0,sizeof reg_seen); memset(reg,0,sizeof reg); memset(&wq,0,sizeof wq); txq_len=0;

    printf("== selftest: packing of words 112/113/114 (PARITY §3.7) ==\n");
    st_check("word112 = (switch<<8)|pressure: (1,4)=0104 (0,4)=0004 (1,3)=0103 (0,3)=0003",pack112(1,4)==0x0104&&pack112(0,4)==0x0004&&pack112(1,3)==0x0103&&pack112(0,3)==0x0003);
    st_check("word113 = (time<<8)|pause:   (6,5)=0605 (15,0)=0F00 (5,255)=05FF",pack113(6,5)==0x0605&&pack113(15,0)==0x0F00&&pack113(5,255)==0x05FF);
    { int ok=1; for(int sw=0;sw<=1;sw++) for(int pr=3;pr<=4;pr++) for(int tt=5;tt<=60;tt++) for(int br=0;br<=255;br++){
          uint16_t a=pack112(sw,pr), d=pack113(tt,br); if((a>>8)!=sw||(a&0xFF)!=pr||(d>>8)!=tt||(d&0xFF)!=br) ok=0; }
      st_check("pack/unpack round trip for all (switch, pressure 3-4, time 5-60, pause 0-255)",ok); }
    st_check("the current config packs to 0104 / 0605 / 70",pack112(sv(S_PISW),sv(S_PIPRESS))==0x0104&&pack113(sv(S_PITIME),sv(S_BREAK))==0x0605&&sv(S_HEADT)==70);
    setting_mcu_push(S_PISW,0);    st_check("SAVE pre-infusion OFF = sub112 len2: aa 55 06 70 02 04 00 05 06 df c6",wq.n==1&&q_hex(0,"aa 55 06 70 02 04 00 05 06 df c6")); wq.n=0;
    setting_mcu_push(S_PIPRESS,3); st_check("SAVE pre-inf. pressure 3 = sub112 len2: aa 55 06 70 02 03 01 05 06 8f 72",wq.n==1&&q_hex(0,"aa 55 06 70 02 03 01 05 06 8f 72")); wq.n=0;
    setting_mcu_push(S_PITIME,7);  st_check("SAVE pre-inf. time 7  = sub113 len1: aa 55 06 71 01 05 07 9d 61",wq.n==1&&q_hex(0,"aa 55 06 71 01 05 07 9d 61")); wq.n=0;
    setting_mcu_push(S_HEADT,80);  st_check("SAVE group temp 80    = sub114 len1: aa 55 06 72 01 50 00 e3 b7",wq.n==1&&q_hex(0,"aa 55 06 72 01 50 00 e3 b7")); wq.n=0;
    setting_mcu_push(S_GRINDMODE,1); st_check("SAVE grind mode       = sub2 len1 (NOT stock's len2 with start word 3)",wq.n==1&&wq.q[0].sub==2&&wq.q[0].len==1&&q_hex(0,"aa 55 06 02 01 01 00 c4 e7")); wq.n=0;
    st_check("pre-inf. pressure: only 3 and 4 can be selected (MCU 0x1E10)",SET[S_PIPRESS].lo==3&&SET[S_PIPRESS].hi==4&&setting_mcu_push(S_PIPRESS,5)==-1&&wq_rej==WJ_RANGE&&setting_mcu_push(S_PIPRESS,2)==-1); wq.n=0;

    printf("== selftest: trigger word audit (PARITY §1.5/§6.2: words 3/6/9 + 26/27/30) ==\n");
    { int ok=1, nfr=0;
      for(int i=0;i<NSET;i++){ Setting*s=&SET[i]; if(s->mcu==M_NONE) continue;
          int vals[3]={s->lo,s->hi,s->val};
          for(int k=0;k<3;k++){ wq.n=0; int r=setting_mcu_push(i,vals[k]);
              if(r!=1||wq.n!=1){ ok=0; printf("     %s=%d: push r=%d rej=%d\n",s->key,vals[k],r,wq_rej); continue; }
              const WFrame*f=&wq.q[0]; nfr++;
              if(!frame_trig_audit(f,0)) ok=0;
              static const int never[]={3,6,9,16,22,24,26,27,30}; for(unsigned q=0;q<sizeof never/sizeof never[0];q++) if(frame_covers(f,never[q])){ ok=0; printf("     %s: frame covers word %d\n",s->key,never[q]); } } }
      wq.n=0; char nm[120]; snprintf(nm,sizeof nm,"settings SAVE: %d frames (min/max/current) NEVER cover a trigger/start word",nfr); st_check(nm,ok&&nfr>=30); }
    { uint16_t on=1,awk=0; int ok=wq_push(0,&on,1)&&wq_push(1,&awk,1)&&wq.n==2&&frame_trig_audit(&wq.q[0],0)&&frame_trig_audit(&wq.q[1],0)&&q_hex(0,"aa 55 06 00 01 01 00 c5 5f")&&q_hex(1,"aa 55 06 01 01 00 00 c5 33");
      wq.n=0; st_check("keep-awake/PowerOn: sub0=[1], sub1=[0] — no trigger words",ok); }
    act_push(ACT_GRIND); st_check("GRIND: only start words 3+6 set (explicit intent)",wq.n==1&&frame_trig_audit(&wq.q[0],I_GRIND)&&!frame_trig_audit(&wq.q[0],0)); wq.n=0;
    act_push(ACT_WATER); st_check("WATER:  only start words 9+16 set; word 19 (manual) = 0",wq.n==1&&frame_trig_audit(&wq.q[0],I_WATER)&&wq.q[0].w[10]==0); wq.n=0;
    act_push(ACT_BREW);  st_check("BREW:   only start words 9+22 set; words 6/16 = 0",wq.n==1&&frame_trig_audit(&wq.q[0],I_BREW)&&wq.q[0].w[1]==0&&wq.q[0].w[11]==0); wq.n=0;
    act_push(ACT_STEAM);
    { const WFrame*f=&wq.q[0]; st_check("STEAM start: words 26/27/28/29/30/31 = 0 (atlas §3.4)",wq.n==1&&f->sub==24&&f->len==8&&f->w[0]==1&&!f->w[2]&&!f->w[3]&&!f->w[4]&&!f->w[5]&&!f->w[6]&&!f->w[7]&&frame_trig_audit(f,I_STEAM)); } wq.n=0;
    act_push_steam_stop();
    { const WFrame*f=&wq.q[0]; st_check("STEAM stop:  word 24 = 0 and words 26..31 = 0",wq.n==1&&f->w[0]==0&&!f->w[2]&&!f->w[3]&&!f->w[4]&&!f->w[5]&&!f->w[6]&&!f->w[7]&&frame_trig_audit(f,0)); } wq.n=0;
    /* brew with a diverging mirror: sync frames + start, all audited */
    sim_ready(5000); reg[23]=300; reg[112]=0x0003; act_push(ACT_BREW);
    st_check("BREW with a diverging mirror: sub23 len1 + sub112 len3 + sub5 len18, only the last has start words",wq.n==3&&wq.q[0].sub==23&&wq.q[1].sub==112&&wq.q[1].len==3&&wq.q[2].sub==5
             &&frame_trig_audit(&wq.q[0],0)&&frame_trig_audit(&wq.q[1],0)&&frame_trig_audit(&wq.q[2],I_BREW)&&q_hex(1,"aa 55 06 70 03 04 01 05 06 46 00 c7 02")); wq.n=0;
    sim_ready(5000); reg[113]=0x0606; act_push(ACT_BREW);
    st_check("BREW after the MCU has overwritten word113 lo (0606): no re-sync (only the hi byte is compared)",wq.n==1&&wq.q[0].sub==5); wq.n=0;
    /* negative: the policy rejects */
    ms_t nb0=wq.n_block;
    st_check("REJECTED: steam block with word30=1 (MCU REBOOT)",rej(24,(uint16_t[8]){1,650,0,0,0,0,1,0},8,I_STEAM,WJ_TRIG));
    st_check("REJECTED: steam block with word26=100 (grind calibration)",rej(24,(uint16_t[8]){1,650,100,0,0,0,0,0},8,I_STEAM,WJ_TRIG));
    st_check("REJECTED: steam block with word27=500 (extraction calibration)",rej(24,(uint16_t[8]){1,650,0,500,0,0,0,0},8,I_STEAM,WJ_TRIG));
    st_check("REJECTED: sub30 len1 = 1, sub26 len1 = 100, sub27 len1 = 200",rej(30,W1(1),1,0,WJ_TRIG)&&rej(26,W1(100),1,0,WJ_TRIG)&&rej(27,W1(200),1,0,WJ_TRIG));
    st_check("REJECTED: intent naming word 30 — even with a zero payload",rej(24,(uint16_t[8]){0,650,0,0,0,0,0,0},8,I_STEAM|WBIT(30),WJ_TRIG));
    st_check("REJECTED: start words without intent (sub3=1, sub9=1, sub24=1, sub22=1, sub16=1)",rej(3,W1(1),1,0,WJ_TRIG)&&rej(9,W1(1),1,0,WJ_TRIG)&&rej(24,W1(1),1,0,WJ_TRIG)&&rej(22,W1(1),1,0,WJ_TRIG)&&rej(16,W1(1),1,0,WJ_TRIG));
    st_check("REJECTED: brew block with the wrong intent (I_GRIND)",rej(5,(uint16_t[18]){60,0,0,0,1,0,870,0,60,3,60,0,400,0,0,0,0,1},18,I_GRIND,WJ_TRIG));
    st_check("REJECTED: stock's sub2 len2 with word3=1 (grind mode + start)",rej(2,(uint16_t[2]){1,1},2,0,WJ_TRIG));
    st_check("REJECTED: service words — word1=1 (sleep/drain), word0=0 (power off) WITHOUT power-off intent (v6.14: WJ_TRIG), word8=1 (self-test)",rej(1,W1(1),1,0,WJ_RANGE)&&rej(0,W1(0),1,0,WJ_TRIG)&&rej(8,W1(1),1,0,WJ_RANGE));
    st_check("REJECTED: service words — word20=1 (back-flush), word28/29=1 (drain), word19=1 (manual water), word21=1",rej(20,W1(1),1,0,WJ_RANGE)&&rej(28,W1(1),1,0,WJ_RANGE)&&rej(29,W1(1),1,0,WJ_RANGE)&&rej(19,W1(1),1,0,WJ_RANGE)&&rej(21,W1(1),1,0,WJ_RANGE));
    st_check("REJECTED: values out of spec — temp 1200/840, group 110, milk 710, dose 310/9",rej(11,W1(1200),1,0,WJ_RANGE)&&rej(11,W1(840),1,0,WJ_RANGE)&&rej(114,W1(110),1,0,WJ_RANGE)&&rej(25,W1(710),1,0,WJ_RANGE)&&rej(4,W1(310),1,0,WJ_RANGE)&&rej(4,W1(9),1,0,WJ_RANGE));
    st_check("REJECTED: values out of spec — water 250/5100, brew weight 95/1000, level 0/5, mode 0/3",rej(17,W1(250),1,0,WJ_RANGE)&&rej(17,W1(5100),1,0,WJ_RANGE)&&rej(23,W1(95),1,0,WJ_RANGE)&&rej(23,W1(1000),1,0,WJ_RANGE)&&rej(14,W1(0),1,0,WJ_RANGE)&&rej(14,W1(5),1,0,WJ_RANGE)&&rej(2,W1(0),1,0,WJ_RANGE)&&rej(2,W1(3),1,0,WJ_RANGE));
    st_check("REJECTED: word112 with pressure 5 / switch byte 2; word113 with time 4; recipe word 13 != 60",rej(112,(uint16_t[2]){0x0105,0x0605},2,0,WJ_RANGE)&&rej(112,(uint16_t[2]){0x0204,0x0605},2,0,WJ_RANGE)&&rej(113,W1(0x0405),1,0,WJ_RANGE)&&rej(13,W1(340),1,0,WJ_RANGE));
    st_check("rejected frames are counted (DIAGNOSE 'BLK') and nothing is queued",wq.n_block>nb0+30&&wq.n==0);
    { int keep=SET[S_STEAMT].val; SET[S_STEAMT].val=9999; int r=act_push_steam_stop(); int ok=r&&wq.n==1&&wq.q[0].w[0]==0&&wq.q[0].w[1]==700; wq.n=0; SET[S_STEAMT].val=keep;
      st_check("STOP STEAM cannot be blocked: the target word is clamped to spec (9999 -> 700)",ok); }

    printf("== selftest: boot push equivalence (PARITY §1.3-1.4) ==\n");
    { /* stock: model_data_init fills the mirror from config; fuart_thread sends sub1 len30 + sub112 len3.
         Independent model straight from the config fields (not via SET[]). */
      int m[129]; memset(m,0,sizeof m); int v,sw=0,pr=0,ti=0,br=0,ok=1;
      ok&=cfg_get_int("brewTemper",&v); m[11]=v; ok&=cfg_get_int("autoGrindWeight",&v); m[4]=v; ok&=cfg_get_int("grindMode",&v); m[2]=v;
      ok&=cfg_get_int("steamGears",&v); m[14]=v; ok&=cfg_get_int("autobrewWeight",&v); m[23]=v; ok&=cfg_get_int("AtuoSteamTarge",&v); m[25]=v;
      ok&=cfg_get_int("autoHotWaterTarget",&v); m[17]=v; ok&=cfg_get_int("preInfusionSwitch",&sw); ok&=cfg_get_int("preInfusionPressure",&pr);
      ok&=cfg_get_int("preInfusionTime",&ti); ok&=cfg_get_int("breakTime",&br); ok&=cfg_get_int("headTemper",&v); m[114]=v;
      m[112]=(sw?0x100:0)|(pr&0xFF); m[113]=(ti<<8)|(br&0xFF); m[5]=m[13]=m[15]=60;
      st_check("config fields for the boot push model found",ok);
      int same=1,pol=1,nw=0;
      for(int i=0;i<NSET;i++){ Setting*s=&SET[i]; if(s->mcu==M_NONE) continue; wq.n=0;
          if(setting_mcu_push(i,s->val)!=1){ same=0; continue; }
          const WFrame*f=&wq.q[0]; for(int k=0;k<f->len;k++){ nw++; if(f->w[k]!=(uint16_t)m[f->sub+k]){ same=0; printf("     %s: word %d = %u, boot push would give %d\n",s->key,f->sub+k,f->w[k],m[f->sub+k]); } } }
      wq.n=0;
      st_check("SAVE writes exactly the words/values that stock's boot push would set from the same config",same&&nw>=12);
      static const int bw[]={2,4,5,11,13,14,15,17,23,25,112,113,114}; for(unsigned q=0;q<sizeof bw/sizeof bw[0];q++) if(!word_ok(bw[q],(uint16_t)m[bw[q]])) pol=0;
      st_check("all boot push words from the current config are within the frame policy",pol);
      uint16_t p3[3]={(uint16_t)m[112],(uint16_t)m[113],(uint16_t)m[114]}; n=build_write(b,112,p3,3);
      st_check("the SAVE path's packing (PARITY §1.3: time<<8 | breakTime) of the same config = aa 55 06 70 03 04 01 05 06 46 00 c7 02 (word 113 = 0605)",hexeq(b,n,"aa 55 06 70 03 04 01 05 06 46 00 c7 02"));
      /* v6.7: stock's OWN boot push was captured on the device (boot/ondevice/GROUND-TRUTH.md): word 113 lo byte = 0, not breakTime */
      { uint16_t g3[3]={(uint16_t)m[112],(uint16_t)(ti<<8),(uint16_t)m[114]}; n=build_write(b,112,g3,3);
        st_check("v6.7 VECTOR 1: stock's captured boot push sub112 len3 = aa 55 06 70 03 04 01 00 06 46 00 c7 ce (word 113 = 0600: the breakTime byte is 0)",br==5&&hexeq(b,n,"aa 55 06 70 03 04 01 00 06 46 00 c7 ce"));
        uint16_t a30g[30],p3g[3]; boot_push_words(a30g,p3g); n=build_write(b,1,a30g,30);
        st_check("v6.7 VECTOR 2: the boot push's sub1 len30 = stock's captured 67 bytes (… 62 3e), byte for byte",
                 hexeq(b,n,"aa 55 06 01 1e 00 00 01 00 00 00 b9 00 3c 00 00 00 00 00 00 00 00 00 00 00 66 03 00 00 3c 00 03 00 3c 00 00 00 90 01 00 00 00 00 00 00 00 00 00 00 68 01 00 00 8a 02 00 00 00 00 00 00 00 00 00 00 62 3e"));
        n=build_write(b,112,p3g,3);
        st_check("v6.7: boot_push_words() gives words 112..114 = 0104 / 0600 / 70 = stock's captured sub112 len3, byte for byte",p3g[0]==0x0104&&p3g[1]==0x0600&&p3g[2]==70&&hexeq(b,n,"aa 55 06 70 03 04 01 00 06 46 00 c7 ce"));
        int bk=SET[S_BREAK].val, okb=1;
        for(int q=0;q<3;q++){ static const int tv[3]={0,9,255}; SET[S_BREAK].val=tv[q]; boot_push_words(a30g,p3g); okb&=(p3g[1]==0x0600);
            wq.n=0; setting_mcu_push(S_PITIME,7); okb&=(wq.n==1&&wq.q[0].sub==113&&wq.q[0].w[0]==(uint16_t)(0x0700|tv[q])); wq.n=0; }
        SET[S_BREAK].val=bk;
        st_check("v6.7: breakTime 0 / 9 / 255 in config -> the boot push's word 113 is still 0600; the SAVE path still packs breakTime (07xx)",okb);
        st_check("v6.7: word 113 = 0600 is within the frame policy, and is not the MCU's start-value signature (hi byte >= 5)",word_ok(113,0x0600)&&SET[S_PITIME].lo>=5); }
      /* v6.4: the SIM build's boot push (boot_push_step WITHOUT the activity guard = the contract mode) — same code as in
         SIM, run against the write queue here. v6.5: the device uses the same code WITH the guard — see "boot push on the device" below. */
      { __typeof__(wq) wq0=wq; ms_t rx0=last_rx_ms; uint16_t ws0=reg[33]; int tx0=txq_len, bt0=SET[S_BREWT].val; ms_t T=sim_now+500000;
        uint16_t a30[30],p3b[3]; boot_push_words(a30,p3b); int eq=1;
        for(int k=1;k<=30;k++) if(a30[k-1]!=(uint16_t)m[k]) eq=0;
        for(int k=0;k<3;k++) if(p3b[k]!=(uint16_t)(k==1?(ti<<8):m[112+k])) eq=0;      /* v6.7: word 113 lo = 0 in the boot push */
        st_check("v6.4 SIM boot push: words 1..30 + 112..114 = the independent config model (start/trigger words = 0, words 5/13/15 = 60)",eq&&a30[0]==0&&!a30[2]&&!a30[5]&&!a30[8]&&!a30[15]&&!a30[21]&&!a30[23]&&!a30[25]&&!a30[26]&&!a30[29]);
        memset(&wq,0,sizeof wq); wq.sim_ack=1; memset(&bp,0,sizeof bp); memset(&sav,0,sizeof sav); act.pending=ACT_NONE; reg[33]=0x0101; txq_len=0;
        last_rx_ms=T-5000; boot_push_start(0); boot_push_step(T);
        int off=(bp.state==BP_WAIT_A&&wq.n==0);                                  /* no link: wait */
        last_rx_ms=T; reg[33]=0x0141; boot_push_step(T); off&=(bp.state==BP_WAIT_A&&wq.n==0);   /* grind running: wait (the block zeroes the start words) */
        reg[33]=0x0101;
        st_check("v6.4 SIM boot push: postponed without a link and while an activity is running",off);
        boot_push_step(T);
        int okA=bp.state==BP_A&&wq.n==1&&wq.q[0].sub==1&&wq.q[0].len==30&&frame_trig_audit(&wq.q[0],0);
        wq_step(T); wq_step(T+50); boot_push_step(T+50); boot_push_step(T+50);
        int okB=bp.ra==BPR_OK&&bp.state==BP_B&&wq.n==1&&q_hex(0,"aa 55 06 70 03 04 01 00 06 46 00 c7 ce");
        wq_step(T+60); wq_step(T+110); boot_push_step(T+110);
        st_check("v6.4 SIM boot push: sub1 len30 (no start/trigger words) -> ACK -> sub112 len3 = the stock vector -> done",okA&&okB&&bp.state==BP_DONE&&bp.rb==BPR_OK&&wq.n==0&&wq.n_ok==2&&wq.n_block==0);
        /* config out of spec: the WHOLE sub1 block is rejected by the frame policy and counted; nothing is sent */
        SET[S_BREWT].val=1200; memset(&wq,0,sizeof wq); wq.sim_ack=1; txq_len=0; boot_push_start(0); boot_push_step(T+200);
        int blk=bp.ra==BPR_BLOCKED&&bp.state==BP_WAIT_B&&wq.n==0&&wq.n_block==1&&wq_rej==WJ_RANGE&&txq_len==0;
        boot_push_step(T+200); blk&=(bp.state==BP_B&&wq.q[0].sub==112); wq_step(T+200); wq_step(T+250); boot_push_step(T+250);
        SET[S_BREWT].val=bt0;
        st_check("v6.4 SIM boot push: brewTemper=1200 -> sub1 len30 REJECTED by the frame policy (BLK +1, 0 bytes sent); sub112 len3 is sent",blk&&bp.state==BP_DONE&&bp.rb==BPR_OK);
        /* no ACK: the block fails after one retransmit, the next block is sent anyway */
        memset(&wq,0,sizeof wq); txq_len=0; boot_push_start(0); boot_push_step(T+400); wq_step(T+400); wq_step(T+550); wq_step(T+860); boot_push_step(T+860);
        int na=bp.ra==BPR_NOACK&&bp.state==BP_WAIT_B&&wq.n_retx==1&&wq.n_fail==1; boot_push_step(T+860); na&=(bp.state==BP_B&&wq.n==1&&wq.q[0].sub==112);
        st_check("v6.4 SIM boot push: without ACK -> 1 retransmit, FAILED, and sub112 len3 is sent anyway",na);
        st_check("v6.5: the write queue's block limit is 30 words in ALL build types (the v6.4 device: 24)",WQ_WMAX==30&&sizeof(((WFrame*)0)->w)==60&&sizeof last_tx>=2*WQ_WMAX+7);
        wq=wq0; memset(&bp,0,sizeof bp); last_rx_ms=rx0; reg[33]=ws0; txq_len=tx0; } }
    st_bootpush_device(cfgsrc);
    st_rx_resync();

    printf("== selftest: settings table, limits and editor ==\n");
    { int ok=1; static const char*const gone[]={"brewMode","sleepMode","sleepModeSetting","ssid","password","wifionoff","theme","presetStore","init","patch"};
      for(int i=0;i<NSET;i++) for(unsigned q=0;q<sizeof gone/sizeof gone[0];q++) if(SET[i].key&&!strcmp(SET[i].key,gone[q])) ok=0;
      st_check("not in the table: brewMode, sleepMode, sleepModeSetting, wifi fields, theme, presetStore",ok); }
    st_check("hot water TEMPERATURE is a sensor (word 40): no config key, no write, locked",SET[S_HOTT].key==NULL&&SET[S_HOTT].mcu==M_NONE&&(SET[S_HOTT].flags&SF_SENSOR)&&(SET[S_HOTT].flags&SF_LOCK)&&SET[S_HOTT].def==40);
    st_check("breakTime is locked (no editor) but is still packed into word113 lo",(SET[S_BREAK].flags&SF_LOCK)&&SET[S_BREAK].lo==SET[S_BREAK].hi&&SET[S_BREAK].mcu==M_W113);
    st_check("language unlocked 0..10 (11 names); time zone still locked",!(SET[S_LANG].flags&SF_LOCK)&&SET[S_LANG].lo==0&&SET[S_LANG].hi==10&&sizeof OPT_LANG/sizeof OPT_LANG[0]==11&&(SET[S_TZ].flags&SF_LOCK));
    { int ok=1; for(int i=0;i<NSET;i++){ Setting*s=&SET[i]; if(s->kind==K_ENUM&&(s->flags&SF_LOCK)) ok=0; if((s->flags&SF_EST)&&!s->hint) ok=0;
          if(!(s->flags&(SF_LOCK|SF_SENSOR))&&((s->max-s->min)%s->step||s->min%s->step)) ok=0; }
      st_check("table invariants: estimates have a hint, max lies on the step grid",ok); }
    { int ok=1; for(int i=0;i<NSET;i++){ Setting*s=&SET[i]; if(set_oor(s)){ printf("     %s=%d outside %d..%d\n",s->key,s->val,s->lo,s->hi); ok=0; } if(!(s->flags&SF_SENSOR)&&!s->loaded){ printf("     %s missing from config\n",s->key); ok=0; } }
      st_check("all config values are loaded and within the spec limits",ok); }
    { int ok=1; view=V_DETAIL;
      sel=S_BREWT; edit_val=960; edit_step(1,1); ok&=edit_val==960; edit_val=850; edit_step(-1,1); ok&=edit_val==850; edit_val=856; edit_step(-1,1); ok&=edit_val==850; edit_val=856; edit_step(1,1); ok&=edit_val==860;
      sel=S_HOTW; edit_val=400; edit_step(1,1); ok&=edit_val==500; edit_step(1,5); ok&=edit_val==1000; edit_val=5000; edit_step(1,5); ok&=edit_val==5000; edit_val=300; edit_step(-1,5); ok&=edit_val==300;
      sel=S_GRINDW; edit_val=309; edit_step(1,1); ok&=edit_val==309; edit_val=10; edit_step(-1,1); ok&=edit_val==10; edit_val=185; edit_step(1,1); ok&=edit_val==186;
      sel=S_BREWW; edit_val=360; edit_step(1,1); ok&=edit_val==370; edit_val=990; edit_step(1,1); ok&=edit_val==990;
      sel=S_PIPRESS; edit_val=4; edit_step(1,1); ok&=edit_val==4; edit_step(-1,1); ok&=edit_val==3; edit_step(-1,1); ok&=edit_val==3;
      sel=S_BREAK; edit_val=5; edit_step(1,1); ok&=edit_val==5; sel=S_TZ; edit_val=48; edit_step(1,1); ok&=edit_val==48; sel=S_HOTT; edit_val=900; edit_step(1,1); ok&=edit_val==900;
      sel=-1; view=V_MAIN; st_check("stepper: clamp to the spec interval, step grid, locked rows cannot be changed",ok); }
    for(int lg=0;lg<NLANG;lg++){ /* layout: all texts in the detail view lie within ring/chord (otherwise they are silently clipped) — both languages */
      int ok=1, nell=0; char l[112]; lang_set(lg);
      int ktu=SET[S_TUNIT].val;
      for(int i=0;i<NSET;i++) for(int tu=0;tu<2;tu++){ const Setting*s=&SET[i]; const char*lab=TR(s->label);
          if(tu&&!(s->flags&(SF_TEMP10|SF_TEMP1))) continue;                 /* temperature rows: both °C and °F */
          SET[S_TUNIT].val=(s->flags&(SF_TEMP10|SF_TEMP1))?tu:ktu; detail_text(i,l,sizeof l);
          char iv[200]; snprintf(iv,sizeof iv,"%s%s%s",l,(s->hint&&!(s->flags&SF_LOCK))?" · ":"",(s->hint&&!(s->flags&SF_LOCK))?TR(s->hint):"");
          LAY(text_wf(lab,F_S),2*ring_half(82,14)-12,"detail label",lab);
          LAY(text_wf(iv,F_XS),2*ring_half(106,11)-12,"interval+hint (inside the gauge ring)",iv);
          LAY(text_wf(TR(s->note),F_XS),KB[KNOTE].maxw,"note",TR(s->note));
          if(detail_holdable(s)) LAY(text_wf(TR(TX_D_HOLDFAST),F_XS),KB[KST].maxw,"hold hint (status line)",TR(TX_D_HOLDFAST));
          char vb[40]; int xr=(s->flags&SF_LOCK)?388:372;
          if(s->kind==K_ENUM){
              LAY(94+text_w(lab,F_S)+12+60,xr,"list row: label leaves < 60 px for the value",lab);
              for(int v=s->min;v<=s->max;v++){ set_fmt(i,v,vb,sizeof vb-4); LAY(text_w(vb,F_M),228,"enum-hero (F_M)",vb);
                  int room; if(row_val_font(lab,vb,xr,&room)==F_XS&&text_w(vb,F_XS)>room){ nell++; char full[40]; strcpy(full,vb); fit_text(vb,F_XS,room); LAY(text_w(vb,F_XS),room,"truncated enum name",vb);
                      printf("     [%s] %s: '%s' is shown truncated in the list as '%s' (full name in the detail)\n",LANG_CODE[ui_lang],lab,full,vb); } }
          } else {   /* number row: label + widest value + unit (+ estimate marker) */
              const char*u=set_unit(i); int uw=*u?text_w(u,F_XS)+6:0, vw=0;
              int probe[3]={s->lo,s->hi,(s->flags&SF_SENSOR)?1200:s->val}; for(int q=0;q<3;q++){ int kl=s->loaded; ((Setting*)s)->loaded=1; set_fmt(i,probe[q],vb,sizeof vb); ((Setting*)s)->loaded=kl; if(text_w(vb,F_M)>vw) vw=text_w(vb,F_M); }
              LAY(94+text_w(lab,F_S)+12+vw+uw+((s->hint&&!(s->flags&SF_LOCK))?18:0),xr,"list row (label + value + unit)",lab);
              set_fmt(i,s->hi,vb,sizeof vb); LAY(text_wf(vb,F_M)+(*u?text_wf(u,F_M)+10:0),228,"number hero (smallest font F_M)",vb);
          } }
      SET[S_TUNIT].val=ktu;
      char nm[200]; snprintf(nm,sizeof nm,"layout: label, interval+hint, note, hold hint, hero and list row fit for all %d rows (%d enum name(s) truncated in the list)",NSET,nell);
      st_check(stl(nm),ok&&nell<=1); }
    lang_set(base_lang);

    printf("== selftest: config persist (atomic, byte-exact, md5 log) ==\n");
    char dir[300]; snprintf(dir,sizeof dir,"/tmp/merakiplus_v6_selftest_%d",(int)getpid()); mkdir(dir,0755);
    snprintf(cfg_path,sizeof cfg_path,"%s/config.json",dir); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/cfgbak",dir);
    st_check("copy of config.json",copy_file(cfgsrc,cfg_path));
    cfg_read(); cfg_writes=0; char m0[33]; strcpy(m0,cfg_md5); char orig[CFG_MAX]; int on=cfglen; memcpy(orig,cfgbuf,on);
    printf("     md5 baseline = %s (%d bytes)\n",m0,on);
    st_check("brewTemper 870 -> 880",cfg_set_int("brewTemper",880));
    int diff=0; if(cfglen!=on) diff=-1; else for(int i=0;i<on;i++) if(cfgbuf[i]!=orig[i]) diff++;
    st_check("exactly 1 byte changed, same length (number token only)",diff==1);
    char m1[33]; strcpy(m1,cfg_md5); printf("     md5 after    = %s\n",m1);
    char p[400]; char bb[CFG_MAX]; snprintf(p,sizeof p,"%s/config.json.orig",cfg_bakdir); int fd=open(p,O_RDONLY); int bn=fd>=0?read(fd,bb,sizeof bb):-1; if(fd>=0)close(fd);
    st_check("pristine backup == baseline",bn==on&&!memcmp(bb,orig,on));
    st_check("volume 31 -> 100 (longer token)",cfg_set_int("volume",100)&&cfglen==on+1);
    st_check("volume 100 -> 31",cfg_set_int("volume",31));
    st_check("hotWaterMode (field after presetStore) 1 -> 2 -> 1",cfg_set_int("hotWaterMode",2)&&cfg_set_int("hotWaterMode",1));
    st_check("unknown field rejected (merakiplus never adds keys)",!cfg_set_int("doesNotExist",1)&&!cfg_set_int("theme",0));
    st_check("string field (ssid) rejected",!cfg_set_int("ssid",1));
    st_check("brewTemper 880 -> 870 (revert)",cfg_set_int("brewTemper",870));
    st_check("md5 after revert == baseline",!strcmp(cfg_md5,m0));
    /* the whole table: every editable field is saved as min, max and back */
    { int ok=1,nf=0; for(int i=0;i<NSET;i++){ Setting*s=&SET[i]; if(s->flags&(SF_LOCK|SF_SENSOR|SF_LOCAL|SF_OWN|SF_SND)) continue; int o; nf++;
          if(!cfg_get_int(s->key,&o)||!cfg_set_int(s->key,s->min)||!cfg_set_int(s->key,s->max)||!cfg_set_int(s->key,o)){ ok=0; printf("     %s: %s\n",s->key,cfg_err); } }
      char nm[120]; snprintf(nm,sizeof nm,"all %d editable fields: save min -> max -> original",nf); st_check(nm,ok&&nf==21);
      st_check("md5 after a full table round trip == baseline",!strcmp(cfg_md5,m0)&&cfglen==on&&!memcmp(cfgbuf,orig,on)); }
    snprintf(p,sizeof p,"%s/config.json.orig",cfg_bakdir); fd=open(p,O_RDONLY); bn=fd>=0?read(fd,bb,sizeof bb):-1; if(fd>=0)close(fd);
    st_check("pristine backup still untouched",bn==on&&!memcmp(bb,orig,on));
    snprintf(p,sizeof p,"%s/.merakiplus_cfg.tmp",dir); st_check("no tmp file left behind",access(p,F_OK)!=0);
    /* a key hidden inside presetStore must not be mistaken for the top level */
    const char*nest="{\n\t\"presetStore\":\t{\n\t\t\"a\":\t{\"brewTemper\":\t111}\n\t},\n\t\"brewTemper\":\t870\n}";
    int a,e2; st_check("nested key ignored (depth 1)",cfg_find(nest,strlen(nest),"brewTemper",&a,&e2)&&atoi(nest+a)==870);
    snprintf(p,sizeof p,"%s/config_writes.log",cfg_bakdir); FILE*lf=fopen(p,"r"); int lines=0; char ln[400];
    if(lf){ while(fgets(ln,sizeof ln,lf)){ lines++; if(lines<=2||lines==6) printf("     log: %s",ln); } fclose(lf); }
    { char nm[80]; snprintf(nm,sizeof nm,"md5 log has one line per write (%d)",cfg_writes); st_check(nm,lines==cfg_writes&&lines>=6+63); }
    snprintf(p,sizeof p,"%s/RESTART_NEEDED",cfg_bakdir); st_check("RESTART_NEEDED flag written",access(p,F_OK)==0);

    printf("== selftest: config out of spec (changed bypassing the stock UI) ==\n");
    { char op[400]; snprintf(op,sizeof op,"%s/oor.json",dir); char keepb[256]; strcpy(keepb,cfg_bakdir);
      int ok=copy_file(cfgsrc,op); snprintf(cfg_path,sizeof cfg_path,"%s",op); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/oorbak",dir);
      ok&=cfg_set_int("brewTemper",990)&&cfg_set_int("headTemper",90)&&cfg_set_int("preInfusionTime",20)&&cfg_set_int("preInfusionPressure",6)&&cfg_set_int("grindMode",0)&&cfg_set_int("language",11);
      st_check("test copy with brewTemper=990, headTemper=90, preInfusionTime=20, pressure=6, grindMode=0, language=11",ok);
      settings_load(); char vb[24];
      st_check("a proven limit is NOT widened: brewTemper 990 is marked out of spec (850..960)",set_oor(&SET[S_BREWT])&&SET[S_BREWT].hi==960);
      st_check("an estimated max is widened to the stock-saved value: group temp 90 (<= 100), pre-inf. time 20 (<= 60)",!set_oor(&SET[S_HEADT])&&SET[S_HEADT].hi==90&&!set_oor(&SET[S_PITIME])&&SET[S_PITIME].hi==20);
      set_fmt(S_GRINDMODE,sv(S_GRINDMODE),vb,sizeof vb); int e1=!strcmp(vb,"(0)"); set_fmt(S_LANG,sv(S_LANG),vb,sizeof vb);
      st_check("an enum outside the table is shown as a raw number — no lookup outside the name table",e1&&!strcmp(vb,"(11)")&&set_oor(&SET[S_GRINDMODE])&&set_oor(&SET[S_LANG])&&set_oor(&SET[S_PIPRESS]));
      sim_ready(9000); memset(&wq,0,sizeof wq); memset(&sav,0,sizeof sav); act.pending=ACT_NONE;
      const char*g=act_guard(ACT_BREW,9000); st_check("activity start blocked: CONFIG OUT OF SPEC",g&&g==TR(TX_G_OOR)&&act_guard(ACT_STEAM,9000)==g);
      st_check("the frame policy rejects the brew block even if the UI guard is bypassed",!act_push(ACT_BREW)&&wq_rej==WJ_RANGE&&wq.n==0);
      view=V_DETAIL; sel=S_BREWT; edit_val=sv(S_BREWT); edit_step(-1,1); int e3=edit_val==960; edit_val=sv(S_BREWT); edit_step(1,1); e3&=edit_val==960; sel=-1; view=V_MAIN;
      st_check("the stepper brings an out-of-spec value into the interval (990 -> 960)",e3);
      ok=cfg_set_int("headTemper",120); settings_load();
      st_check("an estimate above the cap is not widened: group temp 120 > 100 is out of spec",ok&&set_oor(&SET[S_HEADT])&&SET[S_HEADT].hi==80);
      strcpy(cfg_bakdir,keepb); snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load(); }
    /* ============================ v6.2 ============================ */
    printf("== selftest: boot slot + BOOT UI flag (mock root) ==\n");
    char root[360]; snprintf(root,sizeof root,"%s/root",dir); snprintf(sysroot,sizeof sysroot,"%s",root);
    char keepbak[256]; strcpy(keepbak,cfg_bakdir); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/bootbak",dir);
    char fl[440],bl[440],rn[440],cdir[440],tx[400];
    snprintf(fl,sizeof fl,"%s%s",root,BOOT_FLAG); snprintf(bl,sizeof bl,"%s/boot_ui.log",cfg_bakdir);
    snprintf(rn,sizeof rn,"%s/RESTART_NEEDED",cfg_bakdir); snprintf(cdir,sizeof cdir,"%s/customer",root);
    bs.self_pid=MOCK_SELF_PID;
    mock_reset(root); mock_slot_b(root,0,MB_NONE); mock_self(root,"/new_customer/merakiplus/merakiplus"); boot_refresh();
    st_check("mock root: the flag path is <root>/customer/new; the stock binary exists in slot A",!strcmp(rp(BOOT_FLAG),fl)&&bs.a_bin);
    st_check("slot B not mounted: LOCKED — touch rejected, the flag is not created",bs.lock==BL_NOMOUNT&&!boot_flag_set(1)&&access(fl,F_OK)!=0&&strstr(boot_err,"not mounted"));
    mock_slot_b(root,1,MB_NONE);
    st_check("mounted, but demo_v9 missing in slot B: LOCKED",!boot_flag_set(1)&&bs.lock==BL_NOBIN&&access(fl,F_OK)!=0);
    mock_slot_b(root,1,MB_OURS_NOEXEC);
    st_check("merakiplus in slot B without the x bit (cannot boot): LOCKED",!boot_flag_set(1)&&bs.lock==BL_NOBIN&&access(fl,F_OK)!=0);
    mock_slot_b(root,1,MB_VENDOR); mock_flag(root,1);
    { int a=boot_flag_set(0), e1=access(fl,F_OK)==0, b2=boot_flag_set(1);
      st_check("vendor binary in slot B: LOCKED in BOTH directions — the vendor's OTA flag is not touched; the next boot is shown as SLOT B",!a&&!b2&&e1&&bs.lock==BL_VENDOR&&boot_next_kind()==BN_SLOTB&&boot_next_name()==TR(TX_BN_SLOTB)); }
    mock_flag(root,0); mock_slot_b(root,1,MB_OURS); boot_refresh();
    int nc0=dir_count(cdir);
    st_check("merakiplus in slot B + mounted: UNLOCKED; no flag = ORIGINAL on the next boot",bs.lock==BL_OK&&!bs.flag&&boot_next_kind()==BN_ORIGINAL);
    { struct stat fs; int ok=boot_flag_set(1)&&stat(fl,&fs)==0&&S_ISREG(fs.st_mode)&&fs.st_size==0&&bs.flag==1&&dir_count(cdir)==nc0+1;
      st_check("choose MERAKIPLUS: touch /customer/new (empty file) — nothing else in /customer changed",ok&&boot_next_kind()==BN_OURS&&!strcmp(boot_next_name(),PRODUCT)); }
    st_check("choose MERAKIPLUS again: idempotent",boot_flag_set(1)&&bs.flag==1&&dir_count(cdir)==nc0+1);
    st_check("choose ORIGINAL: rm /customer/new — /customer is exactly as before",boot_flag_set(0)&&access(fl,F_OK)!=0&&!bs.flag&&dir_count(cdir)==nc0);
    st_check("choose ORIGINAL again: idempotent (a missing file is not an error)",boot_flag_set(0)&&!bs.flag);
    { chmod(cdir,0555); int r1=boot_flag_set(1), f1=bs.flag, e1=boot_err[0]!=0; chmod(cdir,0755);
      mock_flag(root,1); boot_refresh(); chmod(cdir,0555); int r2=boot_flag_set(0), f2=bs.flag; chmod(cdir,0755); mock_flag(root,0); boot_refresh();
      if(geteuid()==0) printf("     (running as root — the write-protection test is not meaningful, skipped)\n");
      else st_check("read-only /customer (§9.1): the error is reported, the flag and the displayed state are unchanged — in both directions",!r1&&!f1&&e1&&!r2&&f2==1); }
    st_check("boot_ui.log: one line per ACTUAL change (2), none for the idempotent calls",file_lines(bl)==2&&file_has(bl,"created")&&file_has(bl,"removed"));
    file_print(bl,"     log: ");
    { mock_flag(root,1); boot_refresh(); int a=bs.flag; mock_flag(root,0); boot_refresh();
      st_check("the flag state is read live (stat): an external change (telnet touch/rm) is seen",a==1&&bs.flag==0); }
    boot_flag_set(1); mock_slot_b(root,0,MB_OURS);
    st_check("slot B disappears after the flag is set: LOCKED, the flag is not removed behind the user's back; the next boot is shown honestly as ORIGINAL",!boot_flag_set(0)&&access(fl,F_OK)==0&&bs.lock==BL_NOMOUNT&&boot_next_kind()==BN_ORIGINAL);
    mock_slot_b(root,1,MB_OURS); boot_flag_set(0);
    mock_slot_b(root,1,MB_V61); boot_refresh(); st_check("v6.1 binary in slot B is recognised (the exit string) -> unlocked",bs.lock==BL_OK);
    mock_slot_b(root,1,MB_OURS_EDGE); boot_refresh(); st_check("a marker straddling the 64 KiB read boundary is still found",bs.lock==BL_OK);
    { char vb[400]; snprintf(vb,sizeof vb,"%s",cfgsrc); char*q=strrchr(vb,'/'); char*t=q?q+1:vb; snprintf(t,sizeof vb-(size_t)(t-vb),"demo_v9");
      if(access(vb,R_OK)==0){ bs.m_valid=0; st_check("the real vendor binary (artifacts/demo_v9) is NOT recognised as merakiplus",!bin_is_merakiplus(vb)); bs.m_valid=0; }
      else printf("     (no demo_v9 next to config — check of the real vendor binary skipped)\n"); }
    mock_slot_b(root,1,MB_OURS);
    { mock_self(root,SLOT_B_EXE); boot_refresh(); int sB=bs.self_slot; mock_self(root,SLOT_A_EXE); boot_refresh(); int sA=bs.self_slot; mock_self(root,"/new_customer/merakiplus/merakiplus"); boot_refresh();
      st_check("exe slot via /proc/self/exe: /new_customer/demo_v9 = B, /customer/demo_v9 = A, anything else = run.sh/manual",sB==SLOT_B&&sA==SLOT_A&&bs.self_slot==SLOT_OTHER&&!strcmp(bs.self_exe,"/new_customer/merakiplus/merakiplus")); }
    st_check("no stock process, no switcher -> a switch would START stock (launch)",bs.n_stock==0&&bs.sw_pid==0&&switch_mode()==SWM_LAUNCH);
    mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',40); mock_proc(root,MOCK_SW_PID,"/new_customer/merakiplus/boot/switcher","switcher",'S',12);
    mock_proc(root,MOCK_SELF_PID,SLOT_A_EXE,"demo_v9",'R',95); mock_proc(root,77,"/bin/busybox","sh",'S',3); boot_refresh();
    st_check("SIGSTOPped stock process + switcher found (pidof); own pid and foreign processes do not count -> switch = SIGCONT only",bs.n_stock==1&&bs.stock[0]==MOCK_STOCK_PID&&bs.stock_run==0&&bs.sw_pid==MOCK_SW_PID&&switch_mode()==SWM_CONT);
    { mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'S',40); boot_refresh(); int runs=bs.stock_run;
      mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE " (deleted)","demo_v9",'T',40); boot_refresh();
      st_check("a RUNNING stock UI next to merakiplus is flagged; exe '... (deleted)' is still stock",runs==1&&bs.n_stock==1&&bs.stock_run==0); }
    mock_proc(root,MOCK_STOCK_PID,SLOT_B_EXE,"demo_v9",'S',40); boot_refresh();
    st_check("a demo_v9 from slot B is not stock",bs.n_stock==0);
    { mock_swlog(root,NULL); boot_refresh(); int g0=bs.sw_log;
      mock_swlog(root,"[2026-11-17 12:40:41 UTC] switcher started (pid 301)\n[2026-11-17 12:40:41 UTC] input: /dev/input/event0\n"); boot_refresh(); int g1=bs.sw_log;
      mock_swlog(root,MOCK_SWLOG); boot_refresh();
      st_check("switcher.log: missing -> 'not readable'; no switch -> 'no event'; otherwise the last switch line with time of day",g0==0&&g1==1&&bs.sw_log==2&&!strncmp(bs.sw_evt,"12:41:07 switch A",17)&&strstr(bs.sw_evt,"no reboot"));
      printf("     event: %s\n",bs.sw_evt);
      mock_swlog(root,"[2026-11-17 12:41:07 UTC] skift A\xE2\x86\x92" "B F\xC3\x86RDIG: merakiplus k\xC3\xB8rer, stock-A exit'et, ingen reboot\n"); boot_refresh();
      st_check("switcher.log from a pre-English switcher build ('skift …'): still recognised as a switch event",bs.sw_log==2&&!strncmp(bs.sw_evt,"12:41:07 skift A",16)); }
    { static char big[9000]; int o=0,k=0; while(o<6000) o+=snprintf(big+o,sizeof big-o,"[2026-11-17 12:50:%02d UTC] gesture: valid corner long-press counted (1/3)\n",(k++)%60);
      o+=snprintf(big+o,sizeof big-o,"[2026-11-17 12:59:59 UTC] switch FAILED: B (pid 9) did not come up \xE2\x80\x94 fallback: SIGCONT A, kill B\n[2026-11-17 13:00:00 UTC] input: POLLERR/HUP \xE2\x80\x94 reopening\n");
      mock_swlog(root,big); boot_refresh(); st_check("large log (> 4 KiB): only the tail is read, the last switch line is found",bs.sw_log==2&&!strncmp(bs.sw_evt,"12:59:59 switch FAILED",22)); }
    { char o[512]; int ok=1;
      ld_path_for_a(NULL,o,sizeof o); ok&=!strcmp(o,"/customer/libs:/config/wifi");
      ld_path_for_a("",o,sizeof o); ok&=!strcmp(o,"/customer/libs:/config/wifi");
      ld_path_for_a(":/new_customer/libs:/config/wifi",o,sizeof o); ok&=!strcmp(o,"/customer/libs:/config/wifi");
      ld_path_for_a("/new_customer/libs:/config/wifi",o,sizeof o); ok&=!strcmp(o,"/customer/libs:/config/wifi");
      ld_path_for_a("/lib:/config/lib:/new_customer/libs:/config/wifi",o,sizeof o); ok&=!strcmp(o,"/lib:/config/lib:/customer/libs:/config/wifi");
      ld_path_for_a("/lib:/customer/libs:/config/wifi",o,sizeof o); ok&=!strcmp(o,"/lib:/customer/libs:/config/wifi");
      ld_path_for_a("/a'b:/lib:/x y",o,sizeof o); ok&=!strcmp(o,"/lib:/customer/libs:/config/wifi");
      st_check("LD_LIBRARY_PATH for slot A: B's libs out, system prefix kept, A's libs + /config/wifi last (like demo.sh's A branch)",ok); }

    printf("== selftest: the exit reminder — when is it true? ==\n");
#define RN_PUT(txt) do{ FILE*rf=fopen(rn,"w"); if(rf){ fputs(txt,rf); fclose(rf); } }while(0)
    char bid[64]; boot_id_read(bid,sizeof bid);
    unlink(rn); mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',40); boot_refresh();
    st_check("no RESTART_NEEDED -> no reminder",bid[0]&&!restart_pending());
    snprintf(tx,sizeof tx,"config.json changed by merakiplus.\nboot_id=%s mono_s=5000\n",bid); RN_PUT(tx);
    st_check("stock UI started (t=40 s) BEFORE the latest SAVE (t=5000 s), same boot -> reminder",restart_pending());
    mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',6000); boot_refresh();
    st_check("stock UI started AFTER the latest SAVE -> no reminder (it has read config itself)",!restart_pending());
    mock_proc(root,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',40); boot_refresh();
    RN_PUT("config.json changed by merakiplus.\nboot_id=aaaaaaaa-0000-0000-0000-000000000000 mono_s=5000\n");
    st_check("the machine has rebooted since SAVE (different boot_id) -> no reminder",!restart_pending());
    RN_PUT("config.json aendret af merakiplus. (v6.1 format, no stamp)\n");
    st_check("v6.1 flag without a stamp -> assume the reminder applies",restart_pending());
    mock_proc_rm(root,MOCK_STOCK_PID); boot_refresh();
    st_check("no stock UI alive (slot B boot) -> no reminder, regardless of the flag",!restart_pending());
    { char cp[440],kp[256]; snprintf(cp,sizeof cp,"%s/config_rn.json",dir); copy_file(cfgsrc,cp); strcpy(kp,cfg_path); snprintf(cfg_path,sizeof cfg_path,"%s",cp);
      mono_fake=7777; int ok=cfg_set_int("volume",32); mono_fake=-1; snprintf(tx,sizeof tx,"boot_id=%s mono_s=7777\n",bid); ok&=file_has(rn,tx); strcpy(cfg_path,kp);
      st_check("SAVE stamps RESTART_NEEDED with boot_id + monotonic time",ok); }

    printf("== selftest: runtime switch B->A per BOOT.md §9.2 (mock root; 'stock' is a copy of this test binary) ==\n");
    { char ss[440],rel[440],real[700]="",want[800],er[72]=""; ms_t tn=20000; int pp[2]={-1,-1};
      snprintf(ss,sizeof ss,"%s/tmp/stock_started",root); snprintf(rel,sizeof rel,"%s/tmp/released",root);
#define SW_PREP() do{ sim_ready(tn); memset(&wq,0,sizeof wq); memset(&sav,0,sizeof sav); act.pending=ACT_NONE; view=V_MAIN; page=PG_SYS; sec_open=SEC_STOCK; sel=-1; xsel=-1; tr.type=0; run=1; toast.dur=0; rip.on=0; \
                      sw_child=0; sw_seq=sw_seq_frame=sw_seq_release=sw_seq_launch=0; st_rel=st_reacq=0; unlink(ss); unlink(rel); }while(0)
      st_relfile=rel; dev_release=st_release_hook; dev_reacquire=st_reacq_hook; sw_sim=0; sw_dry=0;
      if(!self_bin||!strchr(self_bin,'/')||access(self_bin,X_OK)!=0){ printf("     (own binary path unknown — launch tests skipped)\n"); st_check("launch test could run",0); }
      else {
        /* --- successful switch --- */
        mock_boot_b(root); snprintf(tx,sizeof tx,"%s%s",root,SLOT_A_EXE); int cp=copy_big(self_bin,tx,0755);
        if(realpath(cdir,real)==NULL) snprintf(real,sizeof real,"%s",cdir);
        mkdir(cfg_bakdir,0755); snprintf(tx,sizeof tx,"x\nboot_id=%s mono_s=5000\n",bid); RN_PUT(tx); unlink(bl);
        setenv("MERAKIPLUS_MOCK_STOCK",ss,1); setenv("MERAKIPLUS_MOCK_RELEASED",rel,1); setenv("LD_LIBRARY_PATH","/lib:/config/lib:/new_customer/libs:/config/wifi",1);
        if(pipe(pp)!=0){} fcntl(pp[0],F_SETFL,O_NONBLOCK); st_relfd=pp[1];          /* pp[1] plays merakiplus's UART/fb/touch fd */
        SW_PREP(); sys_open(X_SWITCH,tn); tr.type=0;
        double t0=msnow(); sys_confirm(tn); double el=msnow()-t0;
        st_check("mock stock placed as <root>/customer/demo_v9; SWITCH NOW completed: run=0, last frame shown, stock pid known",cp&&run==0&&view==V_BYE&&bye_mode==SWM_LAUNCH&&sw_child>0);
        st_check("order (§9.2): last frame -> step 1 release UART/touch/fb -> step 2 start stock; fb is not taken back",sw_seq_frame==1&&sw_seq_release==2&&sw_seq_launch==3&&st_rel==1&&st_reacq==0);
        { char nm[160]; snprintf(nm,sizeof nm,"step 3: merakiplus waits %d ms before exit (measured %.0f ms; requirement 300-500 ms)",SW_WAIT_MS,el); st_check(nm,el>=300&&el<=520&&SW_WAIT_MS>=300&&SW_WAIT_MS<=500); }
        snprintf(want,sizeof want,"pid=%d sid=%d argv0=./demo_v9 cwd=%s extra_fds=0 stdio_null=1 released_before_start=1\n",sw_child,sw_child,real);
        st_check("the stock process: own session (setsid), argv0 = ./demo_v9, cwd = /customer, stdio = /dev/null, 0 inherited fds, started AFTER step 1",file_has(ss,want));
        st_check("LD_LIBRARY_PATH for stock: B's libs out, prefix kept, A's libs last",file_has(ss,"\nLD=/lib:/config/lib:/customer/libs:/config/wifi\n"));
        { char c1; int r=(int)read(pp[0],&c1,1); st_check("merakiplus's 'device fd' is closed everywhere (EOF on the pipe) — touch grab/fb/UART cannot survive into the stock UI",r==0); }
        st_check("step 4: the flag /customer/new is untouched by the runtime switch",access(fl,F_OK)==0);
        st_check("step 6: boot_ui.log has the switch with time + reason=user-button; RESTART_NEEDED removed (stock started fresh)",file_has(bl,"RUNTIME SWITCH B->A: reason=user-button, stock UI pid")&&file_has(bl,"RESTART_NEEDED removed")&&access(rn,F_OK)!=0);
        file_print(ss,"     mock-stock: "); file_print(bl,"     boot_ui.log: ");
        { int ws=0; waitpid(sw_child,&ws,0); st_check("mock stock kept running after merakiplus's 'exit' and ended by itself (rc 0) — no signals sent",WIFEXITED(ws)&&WEXITSTATUS(ws)==0); }
        close(pp[0]);
        /* without an inherited prefix: exactly the BOOT.md value */
        unsetenv("LD_LIBRARY_PATH"); SW_PREP(); st_relfd=-1; sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn);
        st_check("without an inherited path stock gets EXACTLY LD_LIBRARY_PATH=/customer/libs:/config/wifi (BOOT.md §9.2)",run==0&&sw_child>0&&file_has(ss,"\nLD=/customer/libs:/config/wifi\n"));
        if(sw_child>0) waitpid(sw_child,NULL,0);
        /* --- stock dies immediately: merakiplus must NOT exit --- */
        setenv("MERAKIPLUS_MOCK_STOCK_FAIL","1",1); RN_PUT("x\n"); unlink(bl);
        SW_PREP(); sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn);
        st_check("stock UI dies immediately: merakiplus does NOT exit, takes UART/fb/touch back, shows the error; RESTART_NEEDED untouched",run==1&&view==V_SYS&&xsel==X_SWITCH&&sw_child==0&&st_rel==1&&st_reacq==1&&sw_fail==SWF_DIED&&!strcmp(toast.msg,TR(TX_T_SW_FAIL_DIED))&&access(rn,F_OK)==0&&file_has(bl,"RUNTIME SWITCH FAILED"));
        unsetenv("MERAKIPLUS_MOCK_STOCK_FAIL");
        /* --- MERAKIPLUS_SWITCH_DRY --- */
        sw_dry=1; unlink(bl); SW_PREP(); sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn); sw_dry=0;
        st_check("MERAKIPLUS_SWITCH_DRY=1: nothing is released, nothing is started, merakiplus keeps running; the log says what WOULD happen",run==1&&st_rel==0&&sw_child==0&&access(ss,F_OK)!=0&&file_has(bl,"DRY-RUN: would close UART/touch/fb")&&!strcmp(toast.msg,TR(TX_T_DRYRUN)));
        /* --- run.sh mode: stock exists (paused) -> nothing is started, no signal; clean exit --- */
        mock_runsh(root,6000); snprintf(tx,sizeof tx,"%s%s",root,SLOT_A_EXE); copy_big(self_bin,tx,0755); unlink(bl); unlink(rn);
        SW_PREP(); sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn);
        st_check("run.sh mode: SWITCH NOW = clean exit like REVERT — NOTHING is started (never two demo_v9), run.sh SIGCONTs",run==0&&view==V_BYE&&bye_mode==SWM_CONT&&sw_child==0&&st_rel==0&&access(ss,F_OK)!=0&&file_has(bl,"run.sh mode"));
        unsetenv("MERAKIPLUS_MOCK_STOCK"); unsetenv("MERAKIPLUS_MOCK_RELEASED");
      }
      /* --- the stock binary is missing entirely --- */
      mock_boot_b(root); mock_rm(root,SLOT_A_EXE); boot_refresh();
      st_check("/customer/demo_v9 missing: switch_launch refuses before fork",switch_locked()&&!switch_launch(er,sizeof er)&&strstr(er,"missing")&&sw_fail==SWF_NOBIN);
      dev_release=NULL; dev_reacquire=NULL; st_relfile=NULL; st_relfd=-1; view=V_MAIN; xsel=-1; run=1; sw_child=0; }

    printf("== selftest: UI routes (SWITCH NOW, BOOT UI; v6.8: no REVERT button) ==\n");
    { ms_t tn=20000; int ns;
      sw_sim=1; sim_ready(tn); memset(&wq,0,sizeof wq); memset(&sav,0,sizeof sav); act.pending=ACT_NONE; view=V_MAIN; page=PG_HOME; sel=-1; xsel=-1; tr.type=0; run=1; toast.dur=0; rip.on=0;
      mock_boot_b(root); unlink(rn);
      sys_open(X_SWITCH,tn); tr.type=0;
      st_check("SWITCH TO ORIGINAL NOW in a slot B boot (no stock UI): the switch confirmation opens (SWITCH + BACK are hittable)",view==V_SYS&&xsel==X_SWITCH&&run==1&&switch_mode()==SWM_LAUNCH&&hit_test(310,316,tn)==T_SAVE&&hit_test(168,316,tn)==T_CANCEL);
      reg[33]=0x4121; sys_confirm(tn);
      st_check("SWITCH NOW during an activity (steam): refused with a message, merakiplus keeps running",run==1&&view==V_SYS&&!strcmp(toast.msg,TR(TX_T_WAIT_ACT)));
      reg[33]=0x0101; rip.on=1; sys_confirm(tn);
      st_check("SWITCH NOW: last frame (without ripple/toast) + run=0 — mode 'start stock'",run==0&&view==V_BYE&&bye_mode==SWM_LAUNCH&&!rip.on&&!toast.dur);
      run=1; view=V_MAIN; xsel=-1; tr.type=0; mock_runsh(root,6000); unlink(rn);
      sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn);
      st_check("SWITCH NOW in run.sh mode without saved changes: clean exit (run.sh SIGCONTs) — what the REVERT button did up to and including v6.7",run==0&&view==V_BYE&&bye_mode==SWM_CONT&&sw_child==0);
      run=1; view=V_MAIN; xsel=-1; tr.type=0; mock_runsh(root,40); snprintf(tx,sizeof tx,"x\nboot_id=%s mono_s=5000\n",bid); RN_PUT(tx);
      sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn); tr.type=0;
      int remind=view==V_SYS&&xsel==X_EXIT&&x_from_switch&&run==1;
      activate(T_CANCEL,tn); tr.type=0;
      st_check("  after SAVE: the exit reminder is shown BEFORE exit; BACK cancels, nothing is started",remind&&view==V_MAIN&&run==1&&sw_child==0);
      run=1; view=V_MAIN; xsel=-1; tr.type=0;
      sys_open(X_SWITCH,tn); tr.type=0; sys_confirm(tn); tr.type=0;
      int viaexit=view==V_SYS&&xsel==X_EXIT&&x_from_switch&&run==1; sys_confirm(tn);
      st_check("SWITCH NOW in run.sh mode after SAVE: the reminder first, then exit (run.sh SIGCONTs)",viaexit&&run==0&&view==V_BYE&&bye_mode==SWM_CONT);
      run=1; view=V_MAIN; xsel=-1; tr.type=0; x_from_switch=0;
      mock_boot_b(root); mock_rm(root,SLOT_A_EXE); sys_open(X_SWITCH,tn); tr.type=0;
      int lk=sys_locked()&&hit_test(310,316,tn)==T_NONE&&hit_test(240,316,tn)==T_CANCEL; sys_confirm(tn); lk&=run==1;
      sys_close(tn); tr.type=0;
      st_check("the stock binary is missing: SWITCH is locked (BACK only), merakiplus keeps running",lk&&run==1&&view==V_MAIN);
      run=1; view=V_MAIN; xsel=-1; tr.type=0;
      { int z=1; page=PG_HOME; sel=-1; static const uint16_t ws[2]={0x0101,0x1307};          /* v6.8: the REVERT pill is gone */
        for(int q=0;q<2;q++){ reg[33]=ws[q]; for(int y=404;y<480;y+=2) for(int x=150;x<330;x+=2){ int t=hit_test(x,y,tn);
            if(t!=T_NONE&&!(t==T_GEAR&&in_r(x,y,CX-GEAR_HW,GEAR_Y-22,2*GEAR_HW,46))) z=0; } }   /* v6.11: only the gear (page change, no frame) */
        reg[33]=0x0101; int nr=1; for(int lg=0;lg<NLANG;lg++) for(int i=0;i<NSTR;i++) if(STR[lg][i]&&strstr(STR[lg][i],"REVERT")) nr=0;
        st_check("v6.8 HOME: no touch zone where the REVERT pill used to be (the area 150..329 x 404..479 gives T_NONE — idle and during a brew — except v6.11's gear, which only changes page); no UI string (EN/DA) mentions REVERT",z&&nr); }
      /* v6.17: SYSTEM > STOCK UI. Closed: 5 rows (4 sections + DIAGNOSTICS), no scrolling. A tap on the STOCK UI header opens it and
       * brings it to the top; BOOT UI and SWITCH TO ORIGINAL NOW are its first two rows. */
      mock_boot_b(root); page=PG_SYS; sec_open=-1; scroll[PG_SYS]=0;
      int closed=page_rows(PG_SYS)==NSEC+1&&scroll_max(PG_SYS)==0&&row_find(PG_SYS,RK_X,X_BOOTUI)<0&&row_find(PG_SYS,RK_DIAG,0)==NSEC;
      activate(T_ROW0+SEC_STOCK,tn); ns=row_find(PG_SYS,RK_X,X_BOOTUI);
      int yb=(int)(row_y0(PG_SYS,ns)-scroll[PG_SYS])+25, ys=(int)(row_y0(PG_SYS,ns+1)-scroll[PG_SYS])+25;
      st_check("SYSTEM: closed = 4 section headers + DIAGNOSTICS (5 rows, no scrolling); tapping STOCK UI opens it at the top: BOOT UI and SWITCH TO ORIGINAL NOW are its first two rows, both hittable (v6.16: rows 16 and 17 of one list)",
               closed&&sec_open==SEC_STOCK&&ns==SEC_STOCK+1&&row_find(PG_SYS,RK_X,X_SWITCH)==ns+1&&hit_test(240,yb,tn)==T_ROW0+ns&&hit_test(240,ys,tn)==T_ROW0+ns+1&&ys+25<LIST_VY1&&yb-25>=LIST_VY0);
      activate(T_ROW0+ns,tn); tr.type=0;
      st_check("BOOT UI opens with the current choice (flag set -> MERAKIPLUS)",view==V_SYS&&xsel==X_BOOTUI&&x_edit==1&&x_orig==1);
      ui_touch(TE_DOWN,84,184,tn); ui_touch(TE_UP,84,184,tn);
      st_check("− selects ORIGINAL; nothing is written before SAVE",x_edit==0&&x_orig==1&&access(fl,F_OK)==0);
      activate(T_SAVE,tn); tr.type=0;
      st_check("SAVE removes the flag, closes the view and confirms",access(fl,F_OK)!=0&&view==V_MAIN&&!strcmp(toast.msg,TR(TX_T_BOOT_SAVED_ORIG)));
      activate(T_ROW0+ns,tn); tr.type=0; ui_touch(TE_DOWN,396,184,tn); ui_touch(TE_UP,396,184,tn); int e1=x_edit; activate(T_CANCEL,tn); tr.type=0;
      st_check("+ and then CANCEL: the flag is unchanged",e1==1&&access(fl,F_OK)!=0&&view==V_MAIN);
      if(geteuid()!=0){   /* §9.1: write error -> show the error, keep the old state */
          activate(T_ROW0+ns,tn); tr.type=0; ui_touch(TE_DOWN,396,184,tn); ui_touch(TE_UP,396,184,tn);
          chmod(cdir,0555); activate(T_SAVE,tn); chmod(cdir,0755);
          st_check("SAVE on a read-only file system: error message, the view stays with the OLD state, no flag",view==V_SYS&&xsel==X_BOOTUI&&!strcmp(toast.msg,TR(TX_T_BOOT_ERR))&&x_edit==0&&x_orig==0&&access(fl,F_OK)!=0);
          tr.type=0; activate(T_CANCEL,tn); tr.type=0; toast.dur=0; }
      mock_slot_b(root,1,MB_VENDOR); activate(T_ROW0+ns,tn); tr.type=0;
      int lk2=sys_locked()&&hit_test(84,184,tn)==T_NONE&&hit_test(396,184,tn)==T_NONE&&hit_test(310,316,tn)==T_NONE&&hit_test(240,316,tn)==T_CANCEL;
      x_edit=1; sys_confirm(tn);
      st_check("locked BOOT UI: no −/+/SAVE in the hit test (BACK only) — and a direct SAVE call is refused by the guard",lk2&&access(fl,F_OK)!=0);
      view=V_MAIN; xsel=-1; tr.type=0; page=PG_HOME; run=1; toast.dur=0; sw_sim=0; }

    printf("== selftest: layout (all states, both languages: text within ring/circle/field) ==\n");
    for(int lg=0;lg<NLANG;lg++){ int ok=1; SysT t; view=V_SYS; lang_set(lg);
      for(int x=X_BOOTUI;x<=X_EXIT;x++) for(int lock=0;lock<4;lock++) for(int f=0;f<2;f++) for(int e=0;e<2;e++) for(int m=0;m<4;m++){
          xsel=x; bs.lock=lock; bs.flag=f; x_orig=f; x_edit=e; bs.a_bin=!(m==3); bs.n_stock=(m==1||m==2)?1:0; bs.stock[0]=18712; x_pending=(m==2); bs.sw_pid=(lock&1)?301:0;
          sys_text(&t);
          float tw=text_wf(t.title,F_S)+(x==X_EXIT?text_wf(IC_WARN,F_IC)+9:0);
          LAY(tw,2*ring_half(78,20),"title",t.title); LAY(text_wf(t.sub,F_XS),2*ring_half(106,11),"subline",t.sub);
          if(t.hero) LAY(text_wf(t.hero,sys_hero_font(t.hero)),x==X_BOOTUI?212:2*ring_half(151,44),"hero",t.hero);
          if(t.status) LAY(text_wf(t.status,F_XS),KB[KST].maxw,"status (field 360 px)",t.status);
          LAY(text_wf(t.note,F_XS),KB[KNOTE].maxw,"note (field 400 px)",t.note);
          LAY(text_wf(t.l1,F_XS),KB[KF1].maxw,"footer 1 (clear of the gauge ends)",t.l1); LAY(text_wf(t.l2,F_XS),2*chord_half(416,11)-12,"footer 2",t.l2);
          if(t.ok) LAY(text_wf(t.ok,x==X_SWITCH?F_XS:F_S),104,"button",t.ok); LAY(text_wf(t.cancel,F_XS),104,"button",t.cancel);
      }
      LAY(text_wf(TR(TX_Y_EXIT_L1),F_S),2*ring_half(138,14)-12,"exit line 1",TR(TX_Y_EXIT_L1)); LAY(text_wf(TR(TX_Y_EXIT_L2),F_S),2*ring_half(162,17)-12,"exit line 2",TR(TX_Y_EXIT_L2));
      LAY(text_wf(TR(TX_Y_EXIT_CMD),F_M)+40,2*ring_half(188,38)-12,"command field",TR(TX_Y_EXIT_CMD));
      st_check(stl("the system views (BOOT UI / SWITCH / exit reminder): title, hero, status, note, footers and buttons fit in all states"),ok);
      ok=1; view=V_MAIN;
      for(int lock=0;lock<4;lock++) for(int f=0;f<2;f++) for(int a=0;a<2;a++) for(int x=0;x<=X_SWITCH;x++){
          const char*label,*sub,*val; int lk; bs.lock=lock; bs.flag=f; bs.a_bin=a; bs.n_stock=0; xrow_text(x,&label,&sub,&val,&lk);
          LAY(text_wf(sub,F_XS),KB[KXSUB].maxw,"row subline",sub); LAY(94+text_wf(label,F_S)+12+(val?text_wf(val,F_M):0),lk?388:372,"row label+value",label);
      }
      /* v6.17: section headers (name + the widest summary + chevron) and the DIAGNOSTICS entry */
      for(int q=0;q<NSEC;q++){ char sm[40]; int ktm=SET[S_TIMEMODE].val, kon=sch.on, khm=sch.hm; SET[S_TIMEMODE].val=0; sch.on=1; sch.hm=22*60+45; sec_summary(q,sm,sizeof sm); SET[S_TIMEMODE].val=ktm; sch.on=kon; sch.hm=khm;
          LAY(94+text_wf(TR(SEC_NAME[q]),F_S)+14+text_wf(sm,F_XS),366,"section header (name + summary)",TR(SEC_NAME[q])); }
      LAY(94+text_wf(TR(TX_PG_DIAG),F_S)+14+text_wf(TR(TX_DV_DOWN),F_S),366,"DIAGNOSTICS entry (name + status)",TR(TX_PG_DIAG));
      st_check(stl("SYSTEM rows BOOT UI / SWITCH: label, value and subline (incl. all lock reasons) fit in the row"),ok);
      /* ---- DIAGNOSE: the top 14 cells, the boot block and the ABOUT block ---- */
      ok=1; { char v[DG_NB][28]; uint32_t col[DG_NB]; char exe[160],evt[128],pids[48],lng[96]; char tv[DG_N][28]; uint32_t tc[DG_N];
      for(int i=0;i<DG_N+DG_NB;i+=2){ const char*la=TR(DG_LAB[i]),*lb=TR(DG_LAB[i+1]); float a=text_wf(la,F_XS),b2=text_wf(lb,F_XS);
          LAY(a/2+b2/2+8,DG_COL_R-DG_COL_L,"diagnose label pair (must not touch)",la); LAY(a,2*(DG_COL_L-40),"diagnose label L",la); LAY(b2,2*(440-DG_COL_R),"diagnose label R",lb); }
      { int kro=ro_mode,kw=cfg_writes,kok=cfg_ok,kph=act.ph; ms_t ko=rx_ok,kb=rx_bad; uint16_t kr[REGN]; memcpy(kr,reg,sizeof reg); float kf=ft_last,km=ft_max;
        ro_mode=1; cfg_writes=1; rx_ok=4294967295u; rx_bad=9999; rx_resync=9999; reg[33]=0xFFFF; reg[34]=reg[35]=reg[36]=0xFF; reg[49]=255; reg[61]=60; reg[50]=reg[58]=9999; reg[112]=reg[113]=0xFFFF; reg[114]=100; reg_seen[112]=1; ft_last=99.9f; ft_max=99.9f;
        wq.n_ok=9999; wq.n_retx=99; wq.n_fail=99; wq.n_block=99;
        for(int c=0;c<2;c++) for(int ph=0;ph<NPHASE;ph++) for(int br=0;br<=BPR_SKIP+2;br++){ act.ph=ph; cfg_ok=c; diag_top_text(tv,tc,1000000000L);
            memset(&bp,0,sizeof bp); if(br<=BPR_SKIP){ bp.ra=bp.rb=br; bp.state=br?BP_DONE:BP_OFF; } else bp.state=br==BPR_SKIP+1?BP_WAIT_A:BP_A;
            diag_top_text(tv,tc,1000000000L);
            for(int i=0;i<DG_N;i+=2){ float a=text_wf(tv[i],F_S),b2=text_wf(tv[i+1],F_S); LAY(a/2+b2/2+6,DG_COL_R-DG_COL_L,"diagnose cell pair",tv[i]); LAY(a,2*(DG_COL_L-40),"diagnose cell L",tv[i]); LAY(b2,2*(440-DG_COL_R),"diagnose cell R",tv[i+1]); } }
        ro_mode=kro; cfg_writes=kw; cfg_ok=kok; act.ph=kph; rx_ok=ko; rx_bad=kb; rx_resync=0; memset(&bp,0,sizeof bp); memcpy(reg,kr,sizeof reg); ft_last=kf; ft_max=km; memset(&wq,0,sizeof wq); }
      for(int i=0;i<DG_NB;i++) LAY(text_wf(TR(DG_LAB[DG_N+i]),F_XS),KB[KDL].maxw,"diagnose label",TR(DG_LAB[DG_N+i]));
      for(int lock=0;lock<4;lock++) for(int f=0;f<2;f++) for(int sl=0;sl<3;sl++) for(int m=0;m<3;m++) for(int g=0;g<3;g++){
          bs.lock=lock; bs.flag=f; bs.self_slot=sl; bs.self_pid=32767; bs.sw_pid=m?32766:0; bs.n_stock=m?1:0; bs.stock[0]=32765; bs.stock_run=(m==2); bs.sw_log=g;
          snprintf(bs.self_exe,sizeof bs.self_exe,"%s",sl==SLOT_B?SLOT_B_EXE:(g?"/new_customer/merakiplus/a/very/long/path/that/cannot/ever/fit/on/one/line/merakiplus_v6_testbuild":""));
          snprintf(bs.sw_evt,sizeof bs.sw_evt,"12:59:59 switch FAILED: B (pid 32767) did not come up \xE2\x80\x94 fallback: SIGCONT A, kill B");
          bs.n_stock=m?(m==2?2:1):0;
          diag_boot_text(v,col,exe,sizeof exe,evt,sizeof evt,pids,sizeof pids);
          for(int i=0;i<DG_NB;i++) LAY(text_wf(v[i],F_S),KB[KDV].maxw,"diagnose cell",v[i]);
          LAY(text_wf(pids,F_S),DG_FULL_W,"diagnose pid line",pids);
          LAY(text_wf(exe,F_XS),DG_FULL_W,"diagnose exe",exe); LAY(text_wf(evt,F_XS),DG_FULL_W,"diagnose event",evt);
      }
      LAY(text_wf(TR(TX_DG_CAP),F_XS),250,"diagnose heading",TR(TX_DG_CAP)); LAY(text_wf(TR(TX_DG_ABOUT),F_XS),250,"diagnose heading",TR(TX_DG_ABOUT));
      { int ks=lang_src; for(lang_src=LS_DEFAULT;lang_src<=LS_SESSION;lang_src++){ diag_lang_text(lng,sizeof lng); LAY(text_wf(lng,F_S),DG_FULL_W,"ABOUT: UI language line",lng); } lang_src=ks; }
      LAY(text_wf(PRODUCT " " MERAKIPLUS_VERSION,F_S),DG_FULL_W,"ABOUT: wordmark + version",PRODUCT " " MERAKIPLUS_VERSION);
      /* v6.10: the PRESSURE · ESTIMATE block — anchor 1..15 bar, the now line with a 5-digit raw value in all three forms, the notes, the toast */
      { uint16_t k51=reg[51]; int kph=act.ph,kv=SET[S_PIPRESS].val; char anc[64],nw[64],tb[96]; uint32_t c;
        LAY(text_wf(TR(TX_DG_PCAP),F_XS),250,"diagnose heading",TR(TX_DG_PCAP));
        for(int av=1;av<=15;av+=14) for(int q=0;q<4;q++){ SET[S_PIPRESS].val=av; reg[51]=(uint16_t)(q==0?1790*av/4:(q==1?1790*4/av:65535)); act.ph=q==3?PH_EXT:PH_READY;
            diag_press_text(anc,sizeof anc,nw,sizeof nw,&c);
            LAY(text_wf(anc,F_S),DG_FULL_W,"PRESSURE · ESTIMATE: anchor line",anc); LAY(text_wf(nw,F_S),DG_FULL_W,"PRESSURE · ESTIMATE: now line",nw);
            snprintf(tb,sizeof tb,TR(TX_T_PINFO),p_anchor_bar(),P_ANCHOR_RAW); LAY(text_wf(tb,F_XS),TOAST_TW,"basis toast",tb); }
        reg[51]=k51; act.ph=kph; SET[S_PIPRESS].val=kv; }
      LAY(text_wf(TR(TX_ST_PULSE),F_M),STAT_PITCH-8,"BAR field: PULSE",TR(TX_ST_PULSE)); LAY(text_wf("65535",F_M),STAT_PITCH-8,"BAR field: raw value","65535"); }
      st_check(stl("DIAGNOSE: label/cell pairs do not touch, boot cells <= column width, long paths/events truncated with …, PRESSURE · ESTIMATE and the ABOUT block fit"),ok);
      ok=1; for(bye_mode=0;bye_mode<2;bye_mode++) LAY(text_wf(bye_sub(),F_XS),340,"goodbye",bye_sub()); bye_mode=0;
      LAY(text_wf(TR(TX_Y_BYE_T),F_M),340,"goodbye",TR(TX_Y_BYE_T)); LAY(text_wf(TR(TX_Y_BYE_CLOSED),F_XS),340,"goodbye",TR(TX_Y_BYE_CLOSED));
      { char q[64]="LC-test " LC "kill $(pidof demo_v9)" UC " OK"; int w0=text_w(q,F_XS); fit_text(q,F_XS,90); ok&=text_w(q,F_XS)<=90&&w0>90&&strstr(q,"\xE2\x80\xA6")!=NULL;
        ok&=text_w(LC "demo_v9",F_S)<text_w("DEMO_V9",F_S)&&text_w(LC UC,F_S)==0; }
      st_check(stl("switch screen + code spans (lowercase only between LC/UC, control characters take 0 px, truncation with …)"),ok);
      /* ---- HOME + splash (v6.3) ---- */
      ok=1;
      for(int ss=0;ss<NSS;ss++){ float w[3]; for(int i=0;i<3;i++) w[i]=text_wf(TR(STAT_SET[ss][i]),F_XS);
          LAY(w[0],2*(CX-STAT_PITCH-96),"stat label L (field 96..384)",TR(STAT_SET[ss][0])); LAY(w[2],2*(384-CX-STAT_PITCH),"stat label R",TR(STAT_SET[ss][2]));
          LAY(w[0]/2+w[1]/2+STAT_GAP,STAT_PITCH,"stat label pair (v6.11: >= 16 px gap)",TR(STAT_SET[ss][1])); LAY(w[1]/2+w[2]/2+STAT_GAP,STAT_PITCH,"stat label pair (v6.11: >= 16 px gap)",TR(STAT_SET[ss][2]));
          if(STAT_SET[ss][1]==TX_ST_BAR){ float wr=text_wf(TR(TX_ST_PRAW),F_XS); LAY(w[0]/2+wr/2+STAT_GAP,STAT_PITCH,"stat label pair (RAW, v6.10)",TR(TX_ST_PRAW)); LAY(wr/2+w[2]/2+STAT_GAP,STAT_PITCH,"stat label pair (RAW, v6.10)",TR(TX_ST_PRAW)); } }
      { static const short cp[3][2]={{TX_C_PRESS,TX_C_WEIGHT},{TX_C_WEIGHT,TX_C_TARGET},{TX_C_MILK,TX_C_TARGET}};
        for(int i=0;i<3;i++) LAY(text_wf(TR(cp[i][0]),F_XS)+text_wf(TR(cp[i][1]),F_XS)+12,236,"chart captions (field 236)",TR(cp[i][0]));
        for(int i=0;i<3;i++) LAY(text_wf(TR(cp[i][0]),F_XS)+text_wf(TR(TX_C_RAW),F_XS)+12,236,"chart caption + RAW tag (v6.9)",TR(TX_C_RAW));
        /* v6.11: the hint lines in the empty chart field (field HINT_W, 4 px margin on each side) — every text that can appear there */
        { static const short hl[]={TX_C_HINT,TX_C_DIAGREF,TX_G_RO,TX_G_BUSY,TX_G_WAIT,TX_G_NOCFG,TX_G_OOR};
          for(size_t i=0;i<sizeof hl/sizeof hl[0];i++) LAY(text_wf(TR(hl[i]),F_XS),HINT_W-8,"chart hint (field 304, v6.11)",TR(hl[i]));
          char eb[96]; snprintf(eb,sizeof eb,TR(TX_C_ERRCODE),"E3"); LAY(text_wf(eb,F_XS),HINT_W-8,"chart hint (field 304, v6.11)",eb); } }
      for(int k=0;k<4;k++){ LAY(text_wf(TR(STOP_BTN[k]),stop_font(1)),STOP_PILL1_W-28,"STOP button (v6.17: pill 216, F_M)",TR(STOP_BTN[k]));
                            LAY(text_wf(TR(STOP_BTN[k]),stop_font(2)),STOP_PILL2_W-28,"STOP button, two side by side (v6.17: pill 152, F_S)",TR(STOP_BTN[k])); }
      /* v6.17: HOLD TO START (icon + text in the pill), the page titles (icon + name, in the chord at y 44..62), the live lines (widest
       * values, °C and °F; every guard text that can stand there) and the icons' targets on HOME (pitch 60 px, >= 8 px between two) */
      for(int k=0;k<4;k++) LAY(text_wf(ACT_ICON[k],F_IC)+10+text_wf(TR(TX_B_START),F_S),START_W-32,"HOLD TO START pill",TR(TX_B_START));
      for(int pg=PG_GRIND;pg<=PG_DIAG;pg++) LAY(page_title_w(pg),2*chord_half(PG_TITLE_Y,20)-16,"page title",page_title(pg));
      { int ktu=SET[S_TUNIT].val; uint16_t kr[REGN]; int ks[REGN]; memcpy(kr,reg,sizeof reg); memcpy(ks,reg_seen,sizeof reg_seen); ms_t klr=last_rx_ms; char lv[96];
        for(int tu=0;tu<2;tu++){ SET[S_TUNIT].val=tu; reg[38]=(uint16_t)-9999; reg[40]=reg[42]=reg[47]=reg[48]=1500; reg_seen[38]=reg_seen[40]=reg_seen[42]=reg_seen[47]=reg_seen[48]=1; reg[33]=0x0101; reg[34]=reg[35]=reg[36]=0; reg[1]=0; last_rx_ms=1000000000L;
            for(int pg=PG_GRIND;pg<=PG_SYS;pg++){ page_live_text(pg,lv,sizeof lv,1000000000L); LAY(text_wf(lv,F_XS),LIVE_W,"page live line",lv); LAY(text_wf(lv,F_XS),2*chord_half(PG_LIVE_Y,11)-12,"page live line (chord)",lv); } }
        { static const short gl[]={TX_G_RO,TX_G_NOLINK,TX_G_ERROR,TX_G_SLEEP,TX_G_BUSY,TX_G_NOCFG,TX_G_OOR,TX_G_GRINDTIME,TX_G_WATERMAN,TX_DV_VERSION_RO};
          for(size_t i=0;i<sizeof gl/sizeof gl[0];i++) LAY(text_wf(TR(gl[i]),F_XS),LIVE_W,"page live line (guard text)",TR(gl[i])); }
        memcpy(reg,kr,sizeof reg); memcpy(reg_seen,ks,sizeof reg_seen); last_rx_ms=klr;
        for(int tu=0;tu<2;tu++){ SET[S_TUNIT].val=tu; int kv[4]={SET[S_GRINDW].val,SET[S_BREWW].val,SET[S_HOTW].val,SET[S_STEAMT].val};
            SET[S_GRINDW].val=SET[S_GRINDW].hi; SET[S_BREWW].val=SET[S_BREWW].hi; SET[S_HOTW].val=SET[S_HOTW].hi; SET[S_STEAMT].val=SET[S_STEAMT].hi; home_model(1000000000L);
            for(int k=0;k<4;k++){ LAY(text_wf(hm.alab[k],F_XS),60-8,"icon target (pitch 60)",hm.alab[k]); }
            LAY(text_wf(hm.alab[0],F_XS)/2+(ACT_X[1]-ACT_X[0])*1.5f,chord_half(ACT_LAB_Y,11)-4,"icon targets (outermost within the chord)",hm.alab[0]);
            SET[S_GRINDW].val=kv[0]; SET[S_BREWW].val=kv[1]; SET[S_HOTW].val=kv[2]; SET[S_STEAMT].val=kv[3]; }
        SET[S_TUNIT].val=ktu; }
      for(int k=0;k<4;k++){ char sb[96]; snprintf(sb,sizeof sb,TR(TX_H_STARTING),TR(ACT_NAME[k])); LAY(text_wf(sb,F_XS),KB[KSUB].maxw,"HOME subline",sb); }
      LAY(hypotf(brand_wf(F_XS)/2,(float)(CY-HOME_BRAND_Y)),196.5f-4,"HOME title line (must lie within the progress ring r 196.5)",PRODUCT);
      LAY(HOME_BRAND_Y+(int)FS[F_XS].cap+8,76,"HOME title line (clear of the phase chip y 76)",PRODUCT);
      LAY(brand_wf(F_XL),SPLASH_W-8,"splash wordmark",PRODUCT);
      { int kok=cfg_ok; char sb[96]; for(cfg_ok=0;cfg_ok<2;cfg_ok++){ splash_status(sb,sizeof sb); LAY(text_wf(sb,F_XS),SPLASH_W-8,"splash status",sb); } cfg_ok=kok; }
      st_check(stl("HOME + splash: stat labels, chart texts, buttons, 'STARTING …', title line and wordmark fit"),ok);
      xsel=-1; view=V_MAIN; }
    lang_set(base_lang);

    /* ============================ v6.3 ============================ */
    printf("== selftest: i18n — the string table (%d ids x %d languages) ==\n",NSTR,NLANG);
    { int ok=1,same=0;
      for(int i=0;i<NSTR;i++) for(int lg=0;lg<NLANG;lg++){ const char*s=STR[lg][i]; if(!s||(!*s&&i!=TX_NONE)){ ok=0; printf("     %s missing in %s\n",STR_ID[i],LANG_CODE[lg]); } }
      st_check("every string id has both EN and DA (no NULL, no empty ones apart from TX_NONE)",ok&&NSTR>300&&!STR[LANG_EN][TX_NONE][0]);
      ok=1; for(int i=0;i<NSTR;i++){ char a[64],b[64]; fmt_sig(STR[LANG_EN][i],a,sizeof a); fmt_sig(STR[LANG_DA][i],b,sizeof b); if(strcmp(a,b)){ ok=0; printf("     %s: printf conversions differ: EN '%s' / DA '%s'\n",STR_ID[i],a,b); } }
      st_check("same printf conversions in the same order in EN and DA (format safety)",ok);
      ok=1; for(int i=0;i<NSTR;i++){ char a[96],b[96]; num_sig(STR[LANG_EN][i],a,sizeof a); num_sig(STR[LANG_DA][i],b,sizeof b); if(strcmp(a,b)){ ok=0; printf("     %s: numbers differ: EN '%s' / DA '%s'\n",STR_ID[i],a,b); }
          if(!strncmp(STR_ID[i],"TX_U_",5)&&strcmp(STR[LANG_EN][i],STR[LANG_DA][i])){ ok=0; printf("     %s: unit differs\n",STR_ID[i]); } }
      { static const short u[]={TX_U_G,TX_U_S,TX_U_BAR,TX_U_C,TX_U_F}; static const char*const w[]={"G","S","BAR","\xC2\xB0" "C","\xC2\xB0" "F"}; for(int q=0;q<5;q++) for(int lg=0;lg<NLANG;lg++) if(strcmp(STR[lg][u[q]],w[q])) ok=0; }
      st_check("numbers, decimal point and units (G, S, BAR, °C, °F) are identical in EN and DA — only words are translated",ok);
      ok=1; { static const char*const ban[]={"GEN 1","GEN1","GEN-1","GEN 2","GEN2","GEN-2","GENERATION","MERAKITECH"};
        char vt[700];
        for(int i=0;i<NSTR;i++) for(int lg=0;lg<NLANG;lg++){ vis_text(STR[lg][i],vt,sizeof vt,1);
            for(unsigned q=0;q<sizeof ban/sizeof ban[0];q++) if(strstr(vt,ban[q])){ ok=0; printf("     %s [%s] contains '%s'\n",STR_ID[i],LANG_CODE[lg],ban[q]); } }
        vis_text(MERAKIPLUS_MARK,vt,sizeof vt,1); for(unsigned q=0;q<sizeof ban/sizeof ban[0];q++) if(strstr(vt,ban[q])) ok=0;
        vis_text(PRODUCT " " MERAKIPLUS_VERSION,vt,sizeof vt,1); for(unsigned q=0;q<sizeof ban/sizeof ban[0];q++) if(strstr(vt,ban[q])) ok=0; }
      st_check("no generation name (see the ban list) in any string, in the binary marker or in the product name — in both tables",ok&&!strcmp(PRODUCT,"MERAKI+"));
      ok=1; { char vt[700]; for(int i=0;i<NSTR;i++) for(int lg=0;lg<NLANG;lg++){ if(STR_K[i]==KCON) continue; vis_text(STR[lg][i],vt,sizeof vt,0);
            if(strstr(vt,"MERAKIPLUS")){ ok=0; printf("     %s [%s]: the identifier appears in visible text\n",STR_ID[i],LANG_CODE[lg]); } } }
      st_check("the product name is " PRODUCT ": the identifier 'merakiplus' appears only in code spans (paths) and on the console",ok);
      ok=1; { char ex[700],bad[16]; for(int i=0;i<NSTR;i++) for(int lg=0;lg<NLANG;lg++){ if(STR_K[i]==KCON) continue; fmt_fill(STR[lg][i],ex,sizeof ex);
            if(!glyphs_ok(ex,bad,sizeof bad)){ ok=0; printf("     %s [%s]: the character '%s' does not exist in the stroke font\n",STR_ID[i],LANG_CODE[lg],bad); } } }
      st_check("every character in both tables has a glyph in the stroke font (nothing is drawn as '?')",ok);
      ok=1; { char ex[700]; int nk=0;
        for(int i=0;i<NSTR;i++){ int k=STR_K[i]; if(k==K0||k==KCON) continue; nk++;
            for(int lg=0;lg<NLANG;lg++){ fmt_fill(STR[lg][i],ex,sizeof ex); int w=text_w(ex,KB[k].font);
                if(w>KB[k].maxw){ ok=0; printf("     %s [%s] (%s): '%s' is %d px, room %d px\n",STR_ID[i],LANG_CODE[lg],KB[k].what,ex,w,KB[k].maxw); } } }
        char nm[160]; snprintf(nm,sizeof nm,"pixel budget per class: %d classified strings x 2 languages fit in their field (toasts, notes, footers, buttons, titles, cells)",nk); st_check(nm,ok&&nk>150); }
      for(int i=0;i<NSTR;i++) if(!strcmp(STR[LANG_EN][i],STR[LANG_DA][i])) same++;
      st_check("endonyms: ENGLISH / DANSK are named the same in both tables (you can always recognise your own language)",!strcmp(STR[LANG_EN][TX_UL_DA],"DANSK")&&!strcmp(STR[LANG_DA][TX_UL_DA],"DANSK")&&!strcmp(STR[LANG_EN][TX_UL_EN],STR[LANG_DA][TX_UL_EN]));
      printf("     %d of %d strings are identical in EN and DA (units, proper names, technical words like OFFLINE/FLOW/SLOT B, commands)\n",same,NSTR);
      { int k=ui_lang; ui_lang=LANG_EN; const char*e=TR(TX_B_SAVE); ui_lang=LANG_DA; const char*d=TR(TX_B_SAVE); ui_lang=7; const char*u=TR(TX_B_SAVE); const char*o=TR(99999); ui_lang=k;
        st_check("TR(): lookup per call (EN 'SAVE' / DA 'GEM'); unknown language -> English; unknown id -> empty string",!strcmp(e,"SAVE")&&!strcmp(d,"GEM")&&u==e&&o&&!*o); } }

    printf("== selftest: i18n — the language file (%s; here: <work dir>/lang) ==\n",lang_path);
    { char lp[440],lt[460],keepl[256],nodir[460]; strcpy(keepl,lang_path); int keeps=lang_src;
      st_check("default path = /new_customer/merakiplus/lang (merakiplus's own directory — not /customer, not config.json)",!strcmp(keepl,"/new_customer/merakiplus/lang")||getenv("MERAKIPLUS_LANGFILE")!=NULL||strstr(keepl,"_preview_lang")!=NULL);
      snprintf(lp,sizeof lp,"%s/lang",dir); snprintf(lt,sizeof lt,"%s.tmp",lp); snprintf(lang_path,sizeof lang_path,"%s",lp);
      unlink(lp); ui_lang=LANG_DA; lang_load();
      st_check("file missing -> ENGLISH (default), source = default",ui_lang==LANG_EN&&lang_src==LS_DEFAULT);
      st_check("\"da\" -> DANISH, \"en\" -> ENGLISH (source = file)",lang_from(lp,"da",2)==LANG_DA&&lang_src==LS_FILE&&lang_from(lp,"en",2)==LANG_EN&&lang_src==LS_FILE);
      st_check("tolerant reading: \"da\\n\" (echo da > lang), \" DA \\r\\n\", \"Da\" -> DANISH; \"EN\\n\" -> ENGLISH",lang_from(lp,"da\n",3)==LANG_DA&&lang_from(lp," DA \r\n",6)==LANG_DA&&lang_from(lp,"Da",2)==LANG_DA&&lang_from(lp,"EN\n",3)==LANG_EN);
      { int ok=1; static const struct { const char*d; int n; } bad[]={{"de",2},{"dansk",5},{"d",1},{"daa",3},{"danish\n",7},{"e n",3},{"sv",2},{"\xff\xfe",2},{"da da",5},{"0",1},{"da\0x",4},{"en;rm -rf /",11},{"da..............................",32}};
        for(unsigned q=0;q<sizeof bad/sizeof bad[0];q++){ if(lang_from(lp,bad[q].d,bad[q].n)!=LANG_EN||lang_src!=LS_UNKNOWN){ ok=0; printf("     '%s' did not give EN/unknown\n",bad[q].d); } }
        st_check("anything other than en/da (de, dansk, d, daa, sv, binary, too long, …) -> ENGLISH, source = unknown value",ok);
        st_check("empty file -> ENGLISH (unknown value)",lang_from(lp,"",0)==LANG_EN&&lang_src==LS_UNKNOWN); }
      unlink(lp); mkdir(lp,0755); ui_lang=LANG_DA; lang_load(); int e1=ui_lang==LANG_EN&&lang_src==LS_DEFAULT; rmdir(lp);
      st_check("unreadable (the path is a directory: read fails) -> ENGLISH",e1);
      if(geteuid()!=0){ put_bytes(lp,"da",2); chmod(lp,0000); ui_lang=LANG_DA; lang_load(); int e2=ui_lang==LANG_EN&&lang_src==LS_DEFAULT; chmod(lp,0644); st_check("unreadable (no read permission) -> ENGLISH",e2); }
      unlink(lp); lang_load();
      st_check("lang_save(DA): the file contains EXACTLY \"da\" (2 bytes), no tmp file left behind, reads back as DANISH",lang_save(LANG_DA)&&file_is(lp,"da")&&access(lt,F_OK)!=0&&lang_load()==LANG_DA&&lang_src==LS_FILE);
      st_check("lang_save(EN) on top: EXACTLY \"en\"; an invalid language (-1, 2, 7) is rejected and does not touch the file",lang_save(LANG_EN)&&file_is(lp,"en")&&!lang_save(-1)&&!lang_save(NLANG)&&!lang_save(7)&&file_is(lp,"en"));
      snprintf(nodir,sizeof nodir,"%s/does-not-exist/lang",dir); snprintf(lang_path,sizeof lang_path,"%s",nodir);
      st_check("the directory does not exist: lang_save reports an error and creates nothing",!lang_save(LANG_DA)&&access(nodir,F_OK)!=0);
      snprintf(lang_path,sizeof lang_path,"%s",lp);
      if(geteuid()!=0){ char rd[460],rl[480]; snprintf(rd,sizeof rd,"%s/rolang",dir); mkdir(rd,0755); snprintf(rl,sizeof rl,"%s/lang",rd); put_bytes(rl,"en",2); chmod(rd,0555);
          snprintf(lang_path,sizeof lang_path,"%s",rl); int r=lang_save(LANG_DA); chmod(rd,0755); snprintf(lang_path,sizeof lang_path,"%s",lp);
          char rt[500]; snprintf(rt,sizeof rt,"%s.tmp",rl);
          st_check("read-only directory: lang_save fails, the old file is intact (\"en\"), no tmp file",!r&&file_is(rl,"en")&&access(rt,F_OK)!=0); }

      /* ---- through the UI: SYSTEM -> UI LANGUAGE ---- */
      printf("== selftest: i18n — the UI LANGUAGE row (live switch, CANCEL, SAVE, independent of the stock UI's config.language) ==\n");
      ms_t tn=40000; char cp[460],kcfg[256],kbak[256],rnl[500]; strcpy(kcfg,cfg_path); strcpy(kbak,cfg_bakdir);
      snprintf(cp,sizeof cp,"%s/config_lang.json",dir); copy_file(cfgsrc,cp); snprintf(cfg_path,sizeof cfg_path,"%s",cp); snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/langbak",dir); snprintf(rnl,sizeof rnl,"%s/RESTART_NEEDED",cfg_bakdir);
      unlink(lp); lang_load(); settings_load(); char md0[33]; strcpy(md0,cfg_md5); int cw0=cfg_writes, stock0=sv(S_LANG);
#define UL_PREP() do{ sim_ready(tn); memset(&wq,0,sizeof wq); memset(&sav,0,sizeof sav); act.pending=ACT_NONE; view=V_MAIN; page=PG_SYS; sec_open=SEC_DISPLAY; sel=-1; xsel=-1; tr.type=0; run=1; toast.dur=0; rip.on=0; memset(scroll,0,sizeof scroll); txq_len=0; }while(0)
#define UL_ROW (T_ROW0+1)   /* v6.17: SYSTEM > DISPLAY is open: row 0 = the DISPLAY header, row 1 = UI LANGUAGE */
      UL_PREP();
      st_check("UI LANGUAGE / UI-SPROG is the FIRST row of SYSTEM's first section (DISPLAY; the closed header shows the language's own name), is not a config key and has no MCU word; the stock row (config.language) still exists (STOCK UI section)",
               SEC_ROWS[SEC_DISPLAY][0]==S_UILANG&&row_find(PG_SYS,RK_SET,S_UILANG)==1&&SET[S_UILANG].key==NULL&&SET[S_UILANG].mcu==M_NONE&&(SET[S_UILANG].flags&SF_LOCAL)&&hit_test(240,row_y0(PG_SYS,1)+25,tn)==UL_ROW&&!strcmp(SET[S_LANG].key,"language")&&!(SET[S_LANG].flags&SF_LOCAL)&&sec_of_set(S_LANG)==SEC_STOCK);
      activate(UL_ROW,tn); tr.type=0;
      st_check("the row opens the stepper detail with the active language (ENGLISH); the options are ENGLISH / DANSK",view==V_DETAIL&&sel==S_UILANG&&edit_val==LANG_EN&&SET[S_UILANG].lo==0&&SET[S_UILANG].hi==1&&hit_test(396,184,tn)==T_PLUS&&hit_test(310,316,tn)==T_SAVE);
      const char*t_en=TR(TX_B_CANCEL); static_dirty=0;
      ui_touch(TE_DOWN,396,184,tn); ui_touch(TE_UP,396,184,tn);
      st_check("+ : LIVE — the whole UI switches to Danish immediately (lookup per render, static layer redrawn); nothing is written yet",edit_val==LANG_DA&&ui_lang==LANG_DA&&static_dirty==1&&TR(TX_B_CANCEL)!=t_en&&!strcmp(TR(TX_B_CANCEL),"ANNULLER")&&SET[S_UILANG].val==LANG_EN&&access(lp,F_OK)!=0);
      ui_frame(tn+40); tr.type=0;
      { char l[112], sm[40]; detail_text(S_UILANG,l,sizeof l); sec_summary(SEC_DISPLAY,sm,sizeof sm); st_check("  the detail shows 'ENGLISH / DANSK' (endonyms); the DISPLAY header's summary is the live language's own name",!strcmp(l,"ENGLISH / DANSK")&&!strcmp(sm,"DANSK")); }
      activate(T_CANCEL,tn); tr.type=0;
      st_check("CANCEL: back to English, no file",view==V_MAIN&&ui_lang==LANG_EN&&SET[S_UILANG].val==LANG_EN&&access(lp,F_OK)!=0);
      activate(UL_ROW,tn); tr.type=0; ui_touch(TE_DOWN,396,184,tn); ui_touch(TE_UP,396,184,tn); activate(T_SAVE,tn); tr.type=0;
      st_check("SAVE: file = \"da\", the language is Danish, the confirmation is in the NEW language, the view closes",file_is(lp,"da")&&ui_lang==LANG_DA&&SET[S_UILANG].val==LANG_DA&&lang_src==LS_FILE&&view==V_MAIN&&!sav.active&&!strcmp(toast.msg,STR[LANG_DA][TX_T_LANG_SAVED]));
      cfg_read(); int v_lang=-1; cfg_get_int("language",&v_lang);
      st_check("SAVE does NOT touch stock: config.json md5 unchanged, config.language unchanged, 0 config writes, no RESTART_NEEDED, no backup directory",!strcmp(cfg_md5,md0)&&v_lang==stock0&&cfg_writes==cw0&&access(rnl,F_OK)!=0&&access(cfg_bakdir,F_OK)!=0);
      st_check("SAVE sends NO cmd6 frame (write queue empty, 0 bytes to the UART) — the frame policy is untouched",wq.n==0&&txq_len==0&&wq.n_block==0);
      ui_lang=LANG_EN; lang_src=LS_DEFAULT; lang_load(); settings_load();
      st_check("'restart' (lang_load + settings_load): Danish is remembered from the file",ui_lang==LANG_DA&&lang_src==LS_FILE&&SET[S_UILANG].val==LANG_DA);
      UL_PREP(); activate(UL_ROW,tn); tr.type=0; ui_touch(TE_DOWN,84,184,tn); ui_touch(TE_UP,84,184,tn); int live_en=ui_lang==LANG_EN; activate(T_SAVE,tn); tr.type=0;
      st_check("− and SAVE: back to English, file = \"en\", confirmation in English",live_en&&file_is(lp,"en")&&ui_lang==LANG_EN&&!strcmp(toast.msg,STR[LANG_EN][TX_T_LANG_SAVED]));
      { int ok=cfg_set_int("language",3); settings_load(); int a=sv(S_LANG)==3&&ui_lang==LANG_EN&&file_is(lp,"en"); ok&=cfg_set_int("language",stock0); settings_load();
        st_check("conversely: the stock LANGUAGE (config.language 0 -> 3 -> back) changes neither the UI language nor the lang file",ok&&a&&sv(S_LANG)==stock0&&ui_lang==LANG_EN); }
      snprintf(lang_path,sizeof lang_path,"%s",nodir); UL_PREP(); activate(UL_ROW,tn); tr.type=0; ui_touch(TE_DOWN,396,184,tn); ui_touch(TE_UP,396,184,tn); activate(T_SAVE,tn); tr.type=0;
      st_check("the file cannot be written: the language applies anyway (this session), warning in a toast, DIAGNOSE source = not saved",ui_lang==LANG_DA&&lang_src==LS_SESSION&&view==V_MAIN&&!strcmp(toast.msg,STR[LANG_DA][TX_T_LANG_NOFILE])&&access(nodir,F_OK)!=0);
      { int ok=1; for(int lg=0;lg<NLANG;lg++){ lang_set(lg); settings_load(); UL_PREP(); static_dirty=1; force_all=1; ui_frame(tn+100);
            const char*g=act_guard(ACT_BREW,tn+100); ok&=(g==NULL); reg[35]=1; ok&=!strcmp(err_text(),STR[lg][TX_ER_E2_BREW])&&!strcmp(ph_name(PH_ERROR),STR[lg][TX_PH_ERROR]); reg[35]=0;
            home_model(tn+100); ok&=!strcmp(hm.sl[2],STR[lg][TX_ST_WEIGHT])&&!strcmp(hm.label,STR[lg][TX_H_BOILER]); }
        st_check("live lookup everywhere: phase name, error text, HOME label and stat labels follow the language without a restart",ok); }
      snprintf(lang_path,sizeof lang_path,"%s",keepl); lang_src=keeps; lang_set(base_lang); strcpy(cfg_path,kcfg); strcpy(cfg_bakdir,kbak); toast.dur=0; view=V_MAIN; sel=-1; page=PG_HOME; }
    mock_boot_b(root); boot_refresh();
    strcpy(cfg_bakdir,keepbak);    mock_boot_b(root); boot_refresh();
    strcpy(cfg_bakdir,keepbak); snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); settings_load();
    lang_set(base_lang);
    /* ============================ v6.6 ============================ */
    st_phases();
    st_stop();                         /* v6.13 */
    st_power();                        /* v6.14 */
    st_time(dir);                      /* v6.16 */
    st_wrap();
    st_repush(cfgsrc);
    lang_set(base_lang);
    /* ============================ v6.7 ============================ */
    st_net(dir);
    /* ============================ v6.9 ============================ */
    st_cond();
    /* ============================ v6.10 ============================ */
    st_press(cfgsrc,dir);
    /* ============================ v6.11 ============================ */
    st_v611();
    st_v617();
    st_v618(dir);                      /* v6.18 */
    st_v619();                         /* v6.19 */
    st_v620(dir);                      /* v6.20: review fixes */
    /* ============================ v6.15 ============================ */
    st_graph();
    printf("== selftest [%s]: %s (%d checks, %d failed) — work dir %s ==\n",LANG_CODE[base_lang],st_fail?"FAILED":"ALL OK",st_n,st_fail,dir);
    return st_fail?1:0;
}
static void dump_table(void){
    printf("#key\tlabel\tpage\tspec_tier\tmin\tmax\tstep\tdefault\thard\tlock\test\tsensor\tmcu\thint\n");
    static const char*const MW[]={"-","4","23","11","112","113","114","25","14","17","2"};
    for(int i=0;i<NSET;i++){ const Setting*s=&SET[i];
        if(s->flags&(SF_LOCAL|SF_OWN|SF_SND)) continue;   /* merakiplus's own settings (UI language, v6.16 schedule, v6.18 sound) are not spec parameters */
        printf("%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n",s->key?s->key:"-",TR(s->label),TR(TIER_NAME[s->tier]),s->ptier,s->min,s->max,s->step,s->def,s->hard,
               !!(s->flags&SF_LOCK),!!(s->flags&SF_EST),!!(s->flags&SF_SENSOR),MW[s->mcu],s->hint?TR(s->hint):"-"); }
}
/* the string table as TSV (id, class, EN, DA). Code spans are shown in `backticks`, icons as {iconN}, line breaks as \\n. */
static void dump_str1(const char*s){
    int lc=0;
    for(const unsigned char*p=(const unsigned char*)s;*p;p++){
        if(*p==0x0E){ if(!lc){ putchar('`'); lc=1; } }
        else if(*p==0x0F){ if(lc){ putchar('`'); lc=0; } }
        else if(*p=='\n') fputs("\\n",stdout);
        else if(*p<32) printf("{icon%d}",*p);
        else putchar(*p);
    }
    if(lc) putchar('`');
}
static void dump_strings(void){
    printf("#id\tclass\tEN\tDA\n");
    for(int i=0;i<NSTR;i++){ printf("%s\t%s\t",STR_ID[i],KNAME[STR_K[i]]); dump_str1(STR[LANG_EN][i]); putchar('\t'); dump_str1(STR[LANG_DA][i]); putchar('\n'); }
}
static void bench(void){
    sim_reset(); sim_tick=tick_brew; sim_run(8000);
    double t0=msnow(), mx=0; int n=0;
    for(int i=0;i<600;i++){ double a=msnow(); sim_run(33); double d=msnow()-a; if(d>mx)mx=d; n++; }
    double avg=(msnow()-t0)/n;
    printf("bench brew (animated gauge+chart+numbers): avg %.3f ms/frame, max %.3f ms (host)\n",avg,mx);
    t0=msnow(); for(int i=0;i<200;i++){ force_all=1; static_dirty=1; compose(sim_now); } printf("bench full rebuild (static+all):       %.3f ms/frame (host)\n",(msnow()-t0)/200);
    sim_reset(); sim_run(500); t0=msnow(); n=0; go_page(PG_BREW,1,sim_now); while(tr.type){ sim_run(33); n++; } printf("bench slide transition:               %.3f ms/frame over %d frames (host)\n",(msnow()-t0)/(n?n:1),n);
    sim_reset(); sim_run(3000); t0=msnow(); for(int i=0;i<300;i++) sim_run(33); printf("bench idle (no change):               %.3f ms/frame (host)\n",(msnow()-t0)/300);
}
int main(int argc,char**argv){
    if(getenv("MERAKIPLUS_MOCK_STOCK")) return mock_stock_main(argv[0]);
    self_bin=argv[0];
    int ai=1;
    if(argc>2&&!strcmp(argv[1],"--lang")){ int l=lang_parse(argv[2],(int)strlen(argv[2])); if(l<0){ fprintf(stderr,"usage: --lang en|da\n"); return 2; } pv_lang=l; ai=3; }
    const char*out=argc>ai?argv[ai]:"/tmp/merakiplus_v6_preview_out";
    const char*cfgsrc=argc>ai+1?argv[ai+1]:"artifacts/config.json";
    gfx_init(); lang_set(pv_lang);
    snprintf(sched_path,sizeof sched_path,"%s/_preview_schedule",out);   /* v6.16: never the host's /new_customer (the selftest uses its work dir) */
    snprintf(snd_path,sizeof snd_path,"%s/_preview_sound",out);           /* v6.18: likewise */
    if(!strcmp(out,"--selftest")){ snprintf(sched_path,sizeof sched_path,"/nonexistent/merakiplus_preview_schedule"); snprintf(snd_path,sizeof snd_path,"/nonexistent/merakiplus_preview_sound"); return selftest(cfgsrc); }
    if(!strcmp(out,"--dump-table")){ dump_table(); return 0; }
    if(!strcmp(out,"--dump-strings")){ dump_strings(); return 0; }
    if(!strcmp(out,"--textw")){ for(int i=ai+1;i<argc;i++) printf("%4d %4d %4d %4d %4d  %s\n",text_w(argv[i],F_XS),text_w(argv[i],F_S),text_w(argv[i],F_M),text_w(argv[i],F_L),text_w(argv[i],F_XL),argv[i]); return 0; }
    snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc);
    if(!strcmp(out,"--bench")){ bench(); return 0; }
    if(!strcmp(out,"--is-merakiplus")){ int all=1; for(int i=ai+1;i<argc;i++){ bs.m_valid=0; int o=bin_is_merakiplus(argv[i]); printf("%d %s\n",o,argv[i]); all&=o; } return all?0:1; }
    mkdir(out,0755);
    /* v6.2: the boot state is read from a mock root in the out directory — never from the host's file system */
    static char pvr[400]; snprintf(pvr,sizeof pvr,"%s/_preview_root",out); snprintf(sysroot,sizeof sysroot,"%s",pvr);
    mock_boot_b(pvr); sw_sim=1; mono_fake=5000;
    /* save scenarios work on a COPY of config.json in the out directory */
    char cpy[400]; snprintf(cpy,sizeof cpy,"%s/_preview_config.json",out); copy_file(cfgsrc,cpy);
    snprintf(cfg_bakdir,sizeof cfg_bakdir,"%s/_preview_cfgbak",out);
    /* v6.3: the language file also lives in the out directory. English = no file (default); Danish = the file "da" */
    snprintf(lang_path,sizeof lang_path,"%s/_preview_lang",out);
#define PV_LANG_BASE() do{ unlink(lang_path); lang_src=LS_DEFAULT; if(pv_lang!=LANG_EN) lang_save(pv_lang); lang_set(pv_lang); }while(0)
    PV_LANG_BASE();
    printf("preview language: %s (%s)\n",LANG_CODE[pv_lang],STR[pv_lang][TX_UL_EN+pv_lang]);

    sim_reset(); view=V_SPLASH; ui_start=sim_now; sim_run(900); dump(out,"00_splash");
    sim_reset(); sim_run(1500); dump(out,"01_home_ready");
    sim_reset(); reg[40]=612; reg[41]=612; reg[42]=431; reg[47]=884; sim_run(1500); dump(out,"02_home_heating");
    sim_reset(); sim_run(400); sim_tick=tick_grind; sim_t0=sim_now; sim_run(6600); dump(out,"03_home_grind");
    sim_reset(); sim_run(400); sim_tick=tick_brew; sim_t0=sim_now; sim_run(4200); dump(out,"04_home_brew_pre");
    sim_run(15300); dump(out,"05_home_brew_ext");
    sim_run(12000); dump(out,"08_home_done");
    sim_reset(); sim_run(400); sim_tick=tick_water; sim_t0=sim_now; sim_run(5200); dump(out,"06_home_hotwater");
    sim_reset(); sim_run(400); sim_tick=tick_steam; sim_t0=sim_now; sim_run(18500); dump(out,"07_home_steam");
    sim_reset(); sim_run(1500); ui_touch(TE_DOWN,210,372,sim_now); sim_run(380); dump(out,"09_home_hold_brew"); ui_touch(TE_UP,210,372,sim_now);
    sim_reset(); sim_link=0; last_rx_ms=sim_now-9000; sim_run(1500); dump(out,"10_home_offline");
    sim_reset(); reg[1]=1; reg[33]=0x0001; reg[40]=852; reg[47]=1025; sim_run(1500); dump(out,"11_home_sleep");
    sim_reset(); reg[35]=0x01; sim_run(1500); dump(out,"12_home_error");
    sim_reset(); rx_bad=3; last_bad_ms=sim_now; sim_run(1500); dump(out,"13_home_crc_warning");
#define PV_SEC(sc_) do{ page=PG_SYS; sec_open=(sc_); scroll[PG_SYS]=clampf((float)((sc_)*ROW_P),0,scroll_max(PG_SYS)); static_dirty=1; }while(0)   /* v6.17: SYSTEM with one section opened */
    /* v6.17: the BREW page (tap on the cup icon): title, live line, the feature's six settings (four in view), HOLD TO START */
    sim_reset(); page=PG_BREW; sim_run(600); dump(out,"14_page_brew");
    sim_reset(); page=PG_BREW; sim_run(300); ui_touch(TE_DOWN,240,300,sim_now); sim_run(60); ui_touch(TE_MOVE,240,220,sim_now); sim_run(60); ui_touch(TE_MOVE,240,150,sim_now); sim_run(120); dump(out,"15_page_brew_scrolled"); ui_touch(TE_UP,240,150,sim_now);
    sim_reset(); page=PG_STEAM; sim_run(300); ui_touch(TE_DOWN,240,180,sim_now); sim_run(100); dump(out,"16_page_steam_row_pressed"); memset(&tch,0,sizeof tch);
    sim_reset(); page=PG_SYS; sim_run(600); dump(out,"17_system");                 /* v6.17: SYSTEM closed — four sections + DIAGNOSTICS, no scrolling */
    sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_BREWT,sim_now); sim_run(900); dump(out,"18_detail_brewtemp");
    tap(396,184); sim_run(120); tap(396,184); sim_run(200); ui_touch(TE_DOWN,396,184,sim_now); sim_run(150); dump(out,"19_detail_brewtemp_changed"); ui_touch(TE_UP,396,184,sim_now);
    sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_PISW,sim_now); sim_run(900); dump(out,"20_detail_enum");
    sim_reset(); PV_SEC(SEC_CLOCK); sim_run(300); detail_open(S_TZ,sim_now); sim_run(900); dump(out,"21_detail_locked");
    sim_reset(); page=PG_GRIND; sim_run(300); detail_open(S_GRINDW,sim_now); sim_run(700);
    ui_touch(TE_DOWN,84,184,sim_now); sim_run(2600); dump(out,"22_detail_hold_accel"); ui_touch(TE_UP,84,184,sim_now);
    /* save OK: MCU ACK simulated + real config persist on a copy */
    snprintf(cfg_path,sizeof cfg_path,"%s",cpy);
    sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_BREWT,sim_now); sim_run(600); tap(396,184); sim_run(100); tap(396,184); sim_run(200);
    tap(310,316); sim_run(700); dump(out,"23_saved_toast");
    printf("   (save scenario: brewTemper=%d, md5 %s, cmd6 ok=%ld)\n",sv(S_BREWT),cfg_md5,wq.n_ok);
    /* save FAILED: no ACK */
    sim_reset(); wq.sim_ack=0; page=PG_BREW; sim_run(300); detail_open(S_BREWW,sim_now); sim_run(600); tap(396,184); sim_run(200);
    tap(310,316); sim_run(160); dump(out,"24_detail_saving"); sim_run(700); dump(out,"25_save_fail_no_ack");
    printf("   (failure scenario: autobrewWeight=%d unchanged, retx=%ld fail=%ld)\n",sv(S_BREWW),wq.n_retx,wq.n_fail);
    snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc);
    sim_reset(); page=PG_DIAG; sim_run(600); dump(out,"26_diag");
    sim_reset(); sim_run(1500); tap(ACT_X[ACT_BREW],ACT_Y); sim_run(77); dump(out,"27_slide_transition");   /* v6.17: a tap on the cup icon slides the BREW page in */
    sim_reset(); sim_run(1500); tap(150,372); sim_run(700); dump(out,"28_page_grind_from_tap");   /* v6.17: a tap on the bean icon opens the GRIND page (v6.16: a toast "HOLD THE BUTTON TO START") */
    sim_reset(); sim_run(1500); ui_touch(TE_DOWN,330,372,sim_now); sim_run(800); ui_touch(TE_UP,330,372,sim_now); sim_run(500); dump(out,"29_home_steam_armed");
    /* ---- v6.1: spec alignment ---- */
    sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_PITIME,sim_now); sim_run(900); dump(out,"30_detail_estimate_hint");
    sim_reset(); page=PG_WATER; sim_run(300); detail_open(S_HOTT,sim_now); sim_run(900); dump(out,"31_detail_sensor_vandtemp");
    sim_reset(); PV_SEC(SEC_STOCK); sim_run(300); detail_open(S_BREAK,sim_now); sim_run(900); dump(out,"32_detail_breaktime_locked");
    sim_reset(); PV_SEC(SEC_STOCK); sim_run(300); detail_open(S_LANG,sim_now); sim_run(600); for(int k=0;k<7;k++){ tap(396,184); sim_run(80); } sim_run(300); dump(out,"33_detail_language");
    sim_reset(); page=PG_STEAM; sim_run(300); detail_open(S_STEAMG,sim_now); sim_run(900); dump(out,"34_detail_steam_gear");
    sim_reset(); page=PG_SYS; sim_run(300); tap(240,row_y0(PG_SYS,SEC_STOCK)+25); sim_run(500);
    dump(out,"35_system_stock_open");         /* v6.17: a tap on the STOCK UI header opens the section at the top: BOOT UI + SWITCH first, then the stock-only config rows */
    /* config out of spec (copy with brewTemper=990): marking, blocked start, the stepper brings it back in */
    { char oor[400]; snprintf(oor,sizeof oor,"%s/_preview_config_oor.json",out); copy_file(cfgsrc,oor);
      snprintf(cfg_path,sizeof cfg_path,"%s",oor); cfg_set_int("brewTemper",990); cfg_set_int("preInfusionPressure",6);
      sim_reset(); page=PG_BREW; sim_run(600); dump(out,"36_page_brew_out_of_spec");
      sim_reset(); page=PG_BREW; sim_run(300); detail_open(S_BREWT,sim_now); sim_run(900); dump(out,"37_detail_out_of_spec");
      sim_reset(); sim_run(1500); ui_touch(TE_DOWN,210,372,sim_now); sim_run(900); ui_touch(TE_UP,210,372,sim_now); sim_run(300); dump(out,"38_home_start_blocked_out_of_spec");
      printf("   (out-of-spec scenario: brew start %s, cmd6 queued=%d, blocked=%ld)\n",act.pending==ACT_NONE?"BLOCKED":"STARTED",wq.n,wq.n_block);
      snprintf(cfg_path,sizeof cfg_path,"%s",cfgsrc); }
    /* ---- v6.2: boot slot, runtime switch, exit reminder (all against the mock root) ---- */
#define SYS_BOTTOM() do{ sim_reset(); PV_SEC(SEC_STOCK); }while(0)                                  /* v6.17: SYSTEM > STOCK UI (v6.2-v6.16: the bottom of the SYSTEM list) */
#define ROW_TAP(k) tap(240,(int)(row_y0(PG_SYS,(k))-scroll[PG_SYS])+25)
    int nsys=SEC_STOCK+1;                                                                              /* the row of BOOT UI when STOCK UI is open */
    mock_reset(pvr); mock_slot_b(pvr,0,MB_NONE); mock_flag(pvr,0); mock_self(pvr,"/new_customer/merakiplus/merakiplus"); mock_proc(pvr,MOCK_STOCK_PID,SLOT_A_EXE,"demo_v9",'T',40); mock_rm(pvr,SLOT_A_EXE);
    SYS_BOTTOM(); sim_run(600); dump(out,"39_system_stock_boot_locked");
    mock_boot_b(pvr);
    SYS_BOTTOM(); sim_run(300); ROW_TAP(nsys+X_BOOTUI); sim_run(900); dump(out,"40_bootui_detail");
    tap(84,184); sim_run(500); dump(out,"41_bootui_changed");
    tap(310,316); sim_run(700); dump(out,"42_bootui_saved_toast");
    printf("   (BOOT UI scenario: flag %s after SAVE, %s)\n",mock_exists(pvr,BOOT_FLAG)?"EXISTS":"removed",bs.flag?"MERAKIPLUS":"ORIGINAL on next restart");
    mock_boot_b(pvr); mock_slot_b(pvr,1,MB_VENDOR);
    SYS_BOTTOM(); sim_run(300); ROW_TAP(nsys+X_BOOTUI); sim_run(900); dump(out,"43_bootui_locked");
    mock_boot_b(pvr);
    SYS_BOTTOM(); sim_run(300); ROW_TAP(nsys+X_SWITCH); sim_run(900); dump(out,"44_switch_confirm_boot");
    tap(310,316); sim_run(200); dump(out,"47_switching_bye");
    printf("   (switch scenario: view=%s, run=%d, stock started=%s)\n",view==V_BYE?"BYE":"?",(int)run,sw_child?"YES":"no (preview)");
    mock_runsh(pvr,6000);
    SYS_BOTTOM(); sim_run(300); ROW_TAP(nsys+X_SWITCH); sim_run(900); dump(out,"45_switch_confirm_runsh");
    mock_boot_b(pvr);
    SYS_BOTTOM(); sim_tick=tick_steam; sim_t0=sim_now; sim_run(4000); ROW_TAP(nsys+X_SWITCH); sim_run(600); tap(310,316); sim_run(400); dump(out,"46_switch_blocked_activity");
    /* exit reminder: run.sh mode, stock UI started BEFORE the latest SAVE (RESTART_NEEDED from scenario 23) */
    mock_runsh(pvr,40);
    /* v6.8: via SWITCH NOW (the REVERT button is gone). Afterwards the clock is set to where the v6.6 scenario ended (sim_run(1500) + tap + sim_run(900)),
     * so the later scenarios' animation phase (pulse, ripple) is unchanged and can be compared byte for byte with v6.6. */
    { sim_reset(); ms_t t48=sim_now; SYS_BOTTOM(); sim_run(1500); ROW_TAP(nsys+X_SWITCH); sim_run(900); tap(310,316); sim_run(900); dump(out,"48_exit_banner"); sim_now=t48+1518+99+924; }   /* sim_run moves in 33 ms steps: 1500 -> 1518, tap 70 -> 99, 900 -> 924 */
    printf("   (exit scenario: reminder %s, run=%d)\n",(view==V_SYS&&xsel==X_EXIT)?"SHOWN":"not shown",(int)run);
    mock_boot_b(pvr);
#define DG_SC_BOOT (DG_BOOT-28-LIST_VY0-26)   /* scroll that shows the whole boot block (the heading at the top of the field) */
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_SC_BOOT; sim_run(600); dump(out,"49_diag_boot");
    mock_runsh(pvr,40);
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_SC_BOOT; sim_run(600); dump(out,"50_diag_boot_runsh");
    mock_boot_b(pvr);
    /* ---- v6.3: UI language (own file), live switch, ABOUT block ---- */
    { int other=pv_lang==LANG_EN?LANG_DA:LANG_EN, bx=other>pv_lang?396:84;       /* + towards DANSK, − towards ENGLISH */
      sim_reset(); PV_SEC(SEC_DISPLAY); sim_run(300); detail_open(S_UILANG,sim_now); sim_run(900); dump(out,"51_detail_ui_language");
      tap(bx,184); sim_run(500); dump(out,"52_ui_language_live_preview");
      printf("   (language scenario: live preview = %s, saved = %s, file %s)\n",LANG_CODE[ui_lang],LANG_CODE[SET[S_UILANG].val],access(lang_path,F_OK)==0?"exists":"does not exist");
      tap(310,316); sim_run(700); dump(out,"53_ui_language_saved_toast");
      { char lb[8]=""; int fd=open(lang_path,O_RDONLY); int n=fd>=0?(int)read(fd,lb,sizeof lb-1):0; if(fd>=0) close(fd); lb[n>0?n:0]=0;
        printf("   (language scenario: SAVE -> %s contains \"%s\", UI language %s, cmd6 queued=%d, config writes=%d)\n",lang_path,lb,LANG_CODE[ui_lang],wq.n,cfg_writes); }
      sim_run(2600); page=PG_HOME; static_dirty=1; sim_run(600); dump(out,"54_home_other_language");   /* same run, HOME in the newly chosen language */
      PV_LANG_BASE(); (void)other; }
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=scroll_max(PG_DIAG); sim_run(600); dump(out,"55_diag_about");
    /* v6.5: DIAGNOSE scrolled by one row: RX CRC / RESYNC at the top (1 CRC error, 2 resync) and the boot push row at the bottom */
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_P; rx_bad=1; rx_resync=2; sim_run(600); dump(out,"56_diag_bootpush"); rx_bad=0; rx_resync=0;
    /* v6.6: phase semantics per STEAMLIVE (two steam paths, concurrent phases) + the re-push counter */
    sim_reset(); sim_run(400); sim_tick=tick_wand; sim_t0=sim_now; sim_run(2000); dump(out,"57_home_steam_wand");
    sim_run(2600); dump(out,"58_home_wand_over_target");                       /* probe 69.5° > target 65.0°: still STEAM·WAND, no auto-stop */
    sim_reset(); sim_run(400); sim_tick=tick_brew_wand; sim_t0=sim_now; sim_run(22400); dump(out,"59_home_brew_wand");   /* work_sta 5327 */
    sim_reset(); sim_run(400); sim_tick=tick_steam; sim_t0=sim_now; sim_run(34000); dump(out,"60_home_steam_done");       /* auto-stop: DONE + post-stop rise */
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_P; rps.n_ver=1; rps.n_def=1; rps.last=RPC_VER; sim_run(600); dump(out,"61_diag_repush"); memset(&rps,0,sizeof rps);
    /* v6.7: the NET row in the middle of a self-heal (wpa_supplicant killed, the IP still on wlan0) */
    /* v6.10: DIAGNOSE is longer (PRESSURE · ESTIMATE) — 62 and 66 are scrolled to v6.9's bottom (DG_SC_V69), so the NET and GRAPH rows are in the same place */
#define DG_SC_V69 (DG_PRESS+133-(LIST_VY1-14))
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_SC_V69; nm.wpa_pid=0; nm.carrier=0; nh_probe(&net.p,sim_now,1); net.state=NS_STARTING; sim_run(600); dump(out,"62_diag_net_heal");
    /* v6.9: display conditioning — brew with deadhead pulses (SMOOTH + RAW), idle after a brew with maintenance pulses, DIAGNOSE GRAPH = RAW */
    sim_reset(); cs_ws=0x1107; cs_p=358; cs_w39=0xFFFD; cs_w38=0; cs_pulse=0; cs_glitch=0; sim_tick=tick_cs; sim_t0=sim_now; sim_run(3000);
    cs_ws=0x1307; cs_p=1320; cs_pulse=1; for(int q=0;q<14;q++){ cs_w39=q*22; sim_run(1000); } dump(out,"63_home_brew_spikes");
    cd.raw=1; sim_run(200); dump(out,"64_home_brew_spikes_raw"); cd.raw=0;
    cs_w39=367; sim_tick=tick_idle_maint; sim_run(20000); dump(out,"65_home_idle_after_brew_pulses");
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_SC_V69; cd.raw=1; sim_run(600); dump(out,"66_diag_graph_raw"); cd.raw=0;
    /* v6.10: pressure estimate — DIAGNOSE PRESSURE · ESTIMATE during extraction (1346 = ≈15.0 BAR) and idle with a maintenance pulse (16670 = MAINTENANCE PULSE),
     * and the basis toast on tapping the chart during a brew */
#define DG_SC_PRESS (DG_PRESS-LIST_VY0-62)
    sim_reset(); cs_ws=0x1107; cs_p=358; cs_w39=0xFFFD; cs_w38=0; cs_pulse=0; cs_glitch=0; sim_tick=tick_cs; sim_t0=sim_now; sim_run(3000);
    cs_ws=0x1307; cs_p=1346; for(int q=0;q<6;q++){ cs_w39=q*30; sim_run(1000); } page=PG_DIAG; scroll[PG_DIAG]=DG_SC_PRESS; static_dirty=1; sim_run(600); dump(out,"67_diag_pressure_ext");
    sim_reset(); cs_w38=0; cs_w39=367; sim_tick=tick_idle_maint; sim_run(3000); sim_tick=NULL; reg[33]=0x0101|0x0800; reg[51]=16670; reg[52]=0;
    page=PG_DIAG; scroll[PG_DIAG]=DG_SC_PRESS; sim_run(600); dump(out,"68_diag_pressure_maint_pulse");
    sim_reset(); cs_ws=0x1107; cs_p=358; cs_w39=0xFFFD; cs_w38=0; cs_pulse=0; sim_tick=tick_cs; sim_t0=sim_now; sim_run(3000);
    cs_ws=0x1307; cs_p=1346; for(int q=0;q<8;q++){ cs_w39=q*25; sim_run(1000); } tap(240,268); sim_run(500); dump(out,"69_home_brew_pinfo_toast");
    /* v6.11: UX review — press feedback on the gear and the close chevron (held down), and DIAGNOSE's RAW COUNTERS section */
    sim_reset(); sim_run(1500); ui_touch(TE_DOWN,CX,GEAR_Y,sim_now); sim_run(120); dump(out,"70_home_gear_pressed"); memset(&tch,0,sizeof tch);
    sim_reset(); page=PG_BREW; static_dirty=1; sim_run(400); activate(T_ROW0+1,sim_now); sim_run(400); ui_touch(TE_DOWN,CX,57,sim_now); sim_run(120); dump(out,"71_detail_back_pressed"); memset(&tch,0,sizeof tch);
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_RAW-LIST_VY0-12; sim_run(600); dump(out,"72_diag_raw_counters");
    /* v6.13: manual STOP — water: the button held down, "STOPPING WATER…" after release, "WATER STOPPED" once the MCU has cleared the bits;
     * two buttons (brew + latched steam); and "STOP NOT CONFIRMED" when the bits stay set for 5 s */
    sim_reset(); sim_run(400); sim_tick=tick_water; sim_t0=sim_now; sim_run(4200); ui_touch(TE_DOWN,240,372,sim_now); sim_run(120); dump(out,"73_home_stop_water_pressed");
    ui_touch(TE_UP,240,372,sim_now); sim_run(170); dump(out,"74_home_stop_water_sending");
    sim_tick=NULL; reg[33]=0x0101; reg[16]=0; reg[51]=3; reg[52]=0; sim_run(400); dump(out,"75_home_stop_water_stopped");
    sim_reset(); sim_run(400); sim_tick=tick_brew_latched; sim_t0=sim_now; sim_run(12000); dump(out,"76_home_stop_dual_brew_steam");
    sim_reset(); sim_run(400); sim_tick=tick_water; sim_t0=sim_now; sim_run(1200); tap(240,372); sim_run(5300); dump(out,"77_home_stop_unconfirmed");
    /* v6.14: the power button — HOLD 1.5 s: TURNING OFF… (cmd8 + sub0 len2 on the way), OFF (controller silent), TURNING ON… (the button again);
     * blocked during a brew (toast); DIAGNOSE's POWER BUTTON block */
    sim_reset(); sim_run(400); pw_key(116,1,sim_now); sim_run(1560); dump(out,"78_power_turning_off");
    pw_key(116,0,sim_now); sim_link=0; sim_run(2000); dump(out,"79_power_off");
    pw_key(116,1,sim_now); sim_run(300); dump(out,"80_power_turning_on"); pw_key(116,0,sim_now);
    sim_reset(); sim_run(400); sim_tick=tick_brew; sim_t0=sim_now; sim_run(6000); pw_key(116,1,sim_now); sim_run(1700); dump(out,"81_home_power_blocked_brew"); pw_key(116,0,sim_now);
    sim_reset(); page=PG_DIAG; scroll[PG_DIAG]=DG_PWR-LIST_VY0-12; pw.n_ev=6; pw.n_press=3; pw.n_off=1; pw.n_on=1; pw.key_code=116; pw.key_val=0; pw.t_key=sim_now-4000; sim_run(600); dump(out,"82_diag_power");
    /* v6.15: 250 g cup at rest, then a brew — the graph starts from the tare (0 -> 36 g fills the weight axis) */
    sim_reset(); cb_late=0; cb_flush0=cb_flush1=0; sim_tick=tick_cupbrew; sim_t0=sim_now; sim_run(43800); dump(out,"83_home_brew_after_cup");
    /* v6.16: time & scheduling — the preview clock is 2026-11-18 16:35 local (Wednesday), zone #48 */
#define PV_SCHED(a_,b_,c_,d_) do{ sch.on=(a_); sch.hm=(b_); sch.rep=(c_); sch.ao=(d_); sch.once_at=0; SET[S_PREHEAT].val=sch.on; SET[S_PHAT].val=sch.hm; SET[S_PHREP].val=sch.rep; SET[S_AUTOOFF].val=sch.ao; }while(0)
    sim_reset(); SET[S_TIMEMODE].val=0; PV_SCHED(1,390,1,0); sim_run(1500); dump(out,"84_home_clock12_preheat");
    sim_reset(); PV_SCHED(1,390,2,3); PV_SEC(SEC_SCHED); sim_run(600); dump(out,"85_system_schedule_open");
    sim_reset(); PV_SCHED(1,390,1,0); PV_SEC(SEC_SCHED); sim_run(300); detail_open(S_PHAT,sim_now); sim_run(400); edit_val=405; sim_run(200); dump(out,"86_detail_preheat_at");
    sim_reset(); PV_SEC(SEC_CLOCK); sim_run(300); detail_open(S_TZ,sim_now); sim_run(400); dump(out,"87_detail_timezone");
    sim_reset(); PV_SCHED(1,390,2,3); arm.n_sent=2; arm.n_ack=2; arm.ad=19; arm.ah=6; arm.am=30; idle_t0=sim_now-754000; page=PG_DIAG; scroll[PG_DIAG]=DG_TIME-LIST_VY0-12; sim_run(600); dump(out,"88_diag_time");
    sim_reset(); PV_SCHED(0,390,1,1); pv_sched=1; idle_t0=sim_now-(15*60000-20000); sim_run(1200); dump(out,"89_home_autooff_warn"); pv_sched=0;
    /* v6.17: the per-feature pages and the consolidated SYSTEM (docs/ux-audit-v617.md) */
    sim_reset(); page=PG_BREW; sim_run(600); ui_touch(TE_DOWN,CX,START_Y+START_H/2,sim_now); sim_run(400); dump(out,"90_page_brew_hold_start"); memset(&tch,0,sizeof tch);
    sim_reset(); page=PG_WATER; sim_run(600); dump(out,"91_page_water");
    sim_reset(); page=PG_STEAM; sim_run(600); dump(out,"92_page_steam");
    sim_reset(); sim_link=0; last_rx_ms=sim_now-9000; page=PG_BREW; sim_run(1500); dump(out,"93_page_brew_offline");                 /* the live line says why HOLD TO START is off */
    sim_reset(); SET[S_GRINDMODE].val=2; page=PG_GRIND; sim_run(600); dump(out,"94_page_grind_mode_time");
    sim_reset(); page=PG_SYS; sim_run(300); tap(240,row_y0(PG_SYS,SEC_DISPLAY)+25); sim_run(500); dump(out,"95_system_display_open");
    sim_reset(); SET[S_TIMEMODE].val=0; PV_SCHED(1,390,2,3); page=PG_SYS; sim_run(600); dump(out,"96_system_summaries_12h");            /* closed headers: language, clock (12 h), next pre-heat */
    sim_reset(); PV_SEC(SEC_CLOCK); sim_run(600); dump(out,"97_system_clock_open");
    sim_reset(); reg[35]=0x01; sim_run(1500); tap(240,262); sim_run(700); dump(out,"98_diag_from_home_error");                         /* HOME's "CODE E2 · TAP FOR DETAILS" -> DIAGNOSTICS */
    sim_reset(); page=PG_SYS; sim_run(400); ui_touch(TE_DOWN,CX,PG_TITLE_Y+6,sim_now); sim_run(120); dump(out,"99_page_back_pressed"); memset(&tch,0,sizeof tch);
    /* v6.18: sound — DISPLAY · SOUND scrolled to its sound rows, the two own editors and the (moved) VOLUME editor */
    sim_reset(); PV_SEC(SEC_DISPLAY); scroll[PG_SYS]=clampf((float)(3*ROW_P),0,scroll_max(PG_SYS)); sim_run(600); dump(out,"100_system_display_sound");
    sim_reset(); PV_SEC(SEC_DISPLAY); sim_run(300); detail_open(S_SOUND,sim_now); sim_run(900); dump(out,"101_detail_sound");
    sim_reset(); PV_SEC(SEC_DISPLAY); sim_run(300); detail_open(S_SNDRDY,sim_now); sim_run(900); dump(out,"102_detail_ready_tone");
    sim_reset(); PV_SEC(SEC_DISPLAY); sim_run(300); detail_open(S_VOL,sim_now); sim_run(900); dump(out,"103_detail_volume");
    sim_reset(); PV_SEC(SEC_DISPLAY); sim_run(300); detail_open(S_SNDRDY,sim_now); edit_val=1; save_begin(sim_now); sim_run(500); dump(out,"104_sound_saved_toast");
    unlink(snd_path); sim_reset(); snd_uptime_ovr=20; reg[63]=60; sim_run(SND_SETTLE_MS+800); reg[63]=100; sim_run(400); page=PG_DIAG; scroll[PG_DIAG]=DG_SND-LIST_VY0-12; static_dirty=1; force_all=1; sim_run(600); dump(out,"105_diag_sound");
    return 0;
}
