/* src/mp/proto.c — CRC, register mirror, UART, write queue + frame policy (declaration), boot-push state, RX parser + resync.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ================= CRC-16/MODBUS ================= */
static uint16_t crc16(const uint8_t *d, int n){
    uint16_t c=0xFFFF;
    for(int i=0;i<n;i++){ c^=d[i]; for(int k=0;k<8;k++) c = (c&1)? (c>>1)^0xA001 : c>>1; }
    return c;
}

/* ================= register mirror ================= */
static uint16_t reg[REGN];
static int reg_seen[REGN];
static uint32_t rx_ok=0, rx_bad=0;      /* v6.6: uint32 — wraps in a well-defined way (2^32 frames = approx. 5 years at 26/s) */
static ms_t last_rx_ms=-100000, last_bad_ms=-100000;

/* ================= UART ================= */
static int ufd=-1;
static uint8_t rxbuf[8192]; static int rxlen=0;
static uint8_t txq[1024]; static int txq_len=0;
static int ro_mode=0;                 /* MERAKIPLUS_READONLY=1: no cmd6 writes at all */

#ifndef MERAKIPLUS_PREVIEW
static void uart_open(void){
    ufd=open(UART_DEV, O_RDWR|O_NOCTTY|O_NONBLOCK);
    if(ufd<0){ perror("open ttyS1"); exit(UART_ERR_RC); }
    struct termios t; tcgetattr(ufd,&t);
    cfmakeraw(&t);
    cfsetispeed(&t,B57600); cfsetospeed(&t,B57600);
    t.c_cflag &= ~(PARENB|CSTOPB|CRTSCTS);
    t.c_cflag |= CS8|CREAD|CLOCAL;
    t.c_cc[VMIN]=0; t.c_cc[VTIME]=0;
    tcsetattr(ufd,TCSANOW,&t);
    tcflush(ufd,TCIOFLUSH);
}
static void uart_flush(void){
    int off=0;
    while(off<txq_len){
        int n=write(ufd,txq+off,txq_len-off);
        if(n<=0){ if(errno==EAGAIN||errno==EWOULDBLOCK) break; UART_PERR("write uart"); break; }
        off+=n;
    }
    if(off>0){ memmove(txq,txq+off,txq_len-off); txq_len-=off; }
}
#endif
static void uart_send(const uint8_t*b,int n){
    if(txq_len+n<=(int)sizeof(txq)){ memcpy(txq+txq_len,b,n); txq_len+=n; }
}
static void frame_query(uint8_t sub,uint8_t len){
    uint8_t b[7]={0xAA,0x55,0x03,sub,len,0,0};
    uint16_t c=crc16(b,5); b[5]=c&0xFF; b[6]=c>>8;
    uart_send(b,7);
}
/* builds a cmd6 frame in out (>= 2*len+7 bytes); returns the length */
static int build_write(uint8_t*b,uint8_t sub,const uint16_t*w,int len){
    int n=0;
    b[n++]=0xAA;b[n++]=0x55;b[n++]=0x06;b[n++]=sub;b[n++]=(uint8_t)len;
    for(int i=0;i<len;i++){ b[n++]=w[i]&0xFF; b[n++]=w[i]>>8; }
    uint16_t c=crc16(b,n); b[n++]=c&0xFF; b[n++]=c>>8;
    return n;
}
static void frame_version(void){
    const char*v="1.1.1-69"; int L=strlen(v);
    uint8_t b[64]; int n=0;
    b[n++]=0xAA;b[n++]=0x55;b[n++]=0x09;b[n++]=(uint8_t)L;b[n++]=0x00;
    memcpy(b+n,v,L); n+=L;
    uint16_t c=crc16(b,n); b[n++]=c&0xFF; b[n++]=c>>8;
    uart_send(b,n);
}

/* ---- write queue: sequential cmd6 writes, each w. CRC + ACK wait + ONE retransmit ----
 * ACK = 0x86 w. the same sub/len and echo == payload. Timeout 150 ms -> retransmit,
 * then 300 ms -> FAIL (the rest of the queue is dropped; the UI shows the error).
 * v6.20 (review 2026-11, protocol #3): STOP frames queued BEHIND the failed frame are kept and sent (keep = the stop's group id,
 * set by act_push_stop); the failed frame's own group and every other frame are still dropped, so starts, sync blocks and boot push
 * blocks are never sent half. Before, a STOP tapped while e.g. a keep-awake was in flight was discarded without ever being sent. */
#define WQ_MAX 8
#define WQ_WMAX 30         /* the boot push block sub1 len30 (v6.5: all build types; the v6.4 device had 24) */
#define WQ_ACK1_MS 150
#define WQ_ACK2_MS 300
typedef struct { uint8_t sub,len,keep; uint16_t w[WQ_WMAX]; } WFrame;   /* keep: v6.20 stop group (0 = dropped on a failure) */
enum { WQ_IDLE=0, WQ_WAIT };
enum { WR_NONE=0, WR_OK, WR_FAIL };
static struct { WFrame q[WQ_MAX]; int n; int state; ms_t sent_ms; int tries; int result; long seq; int sim_ack; long n_ok,n_fail,n_retx,n_block; } wq;
static uint8_t last_tx[80]; static int last_tx_len=0;   /* latest cmd6 frame (selftest/diag) */

/* ---- FRAME POLICY (PARITY §1.5 + §6.1-6.4): last line of defence before the UART ----
 * The MCU has NO range checks (cmd6 = memcpy + ACK), and certain words are triggers that
 * the MCU consumes: 3/6/9 = start/tare, 26/27 = weight calibration (writes MCU flash),
 * 30 = MCU REBOOT. Therefore EVERY cmd6 frame — settings, activities, keep-awake — passes
 * this one gate, which rejects frames that
 *   (a) lie outside the write bands (words 0..31 / 112..114),
 *   (b) have a trigger word != 0 without the caller explicitly asking for it (intent), or
 *   (c) have a word outside its spec interval (word_ok(), derived from SET[]).
 * No intent can allow TRIG_NEVER — merakiplus never calibrates and never reboots. */
#define WBIT(n) (1u<<(n))
#define TRIG_NEVER (WBIT(26)|WBIT(27)|WBIT(30))
#define TRIG_START (WBIT(3)|WBIT(6)|WBIT(9)|WBIT(16)|WBIT(22)|WBIT(24))
#define I_NONE  0u
#define I_GRIND (WBIT(3)|WBIT(6))      /* grind-start + grind-tare   (sub3 len4)  */
#define I_BREW  (WBIT(9)|WBIT(22))     /* start trigger + brew active (sub5 len18) */
#define I_WATER (WBIT(9)|WBIT(16))     /* start trigger + auto flag  (sub9 len11) */
#define I_STEAM (WBIT(24))             /* steam switch               (sub24 len8) */
/* v6.14: word 0 = 0 (PowerOff) is not a start word but a "power off" word: it may ONLY be sent with this explicit intent
 * (pw_off_step, stock powerOff 0x149074: usart_send(6,0,2) with word 0 = 0). Without intent it is rejected (WJ_TRIG). */
#define I_POWEROFF (WBIT(0))
_Static_assert(((I_GRIND|I_BREW|I_WATER|I_STEAM)&TRIG_NEVER)==0,"no intent may allow words 26/27/30");
_Static_assert(((I_GRIND|I_BREW|I_WATER|I_STEAM)&~TRIG_START)==0,"intent may only name start words");
_Static_assert((I_POWEROFF&(TRIG_NEVER|TRIG_START))==0,"power-off intent is neither a start word nor a service word");
_Static_assert((TRIG_NEVER&TRIG_START)==0,"the trigger classes must not overlap");
enum { WJ_NONE=0, WJ_RO, WJ_FULL, WJ_BAND, WJ_TRIG, WJ_RANGE };
static int wq_rej=WJ_NONE;             /* reason for the latest rejection (selftest/diag) */
static int frame_policy(uint8_t sub,const uint16_t*w,int len,uint32_t intent);   /* defined after SET[] */

static int wq_busy(void){ return wq.n>0; }
static int wq_push_t(uint8_t sub,const uint16_t*w,int len,uint32_t intent){
    wq_rej=WJ_NONE;
    if(ro_mode){ wq_rej=WJ_RO; return 0; }
    if(wq.n>=WQ_MAX||len>WQ_WMAX||len<1){ wq_rej=WJ_FULL; return 0; }
    /* write-band guard (REGISTERMAP §3): words 0..31 or words 112..114 */
    int end=sub+len;
    if(!(end<33 || (sub>=112 && end>=113 && end<=115))){ wq_rej=WJ_BAND; wq.n_block++; return 0; }
    int pj=frame_policy(sub,w,len,intent);
    if(pj){ wq_rej=pj; wq.n_block++; return 0; }
    WFrame*f=&wq.q[wq.n++]; f->sub=sub; f->len=(uint8_t)len; f->keep=0; memcpy(f->w,w,len*2);
    if(wq.n==1){ wq.state=WQ_IDLE; wq.result=WR_NONE; }
    return 1;
}
static int wq_push(uint8_t sub,const uint16_t*w,int len){ return wq_push_t(sub,w,len,I_NONE); }   /* without intent: no start words */
static void wq_tx(ms_t now){
    WFrame*f=&wq.q[0]; uint8_t b[2*WQ_WMAX+7]; int n=build_write(b,f->sub,f->w,f->len);
    uart_send(b,n); memcpy(last_tx,b,n); last_tx_len=n; wq.sent_ms=now;
}
static void wq_pop(int ok){
    if(ok){ wq.n_ok++; memmove(&wq.q[0],&wq.q[1],sizeof(WFrame)*(wq.n-1)); wq.n--; if(wq.n==0){ wq.result=WR_OK; wq.seq++; } }
    else { wq.n_fail++; uint8_t g=wq.q[0].keep; int m=0;
           for(int i=1;i<wq.n;i++) if(wq.q[i].keep&&wq.q[i].keep!=g) wq.q[m++]=wq.q[i];   /* v6.20: queued STOPs survive (not the failed one's group) */
           wq.n=m; wq.result=WR_FAIL; wq.seq++; }
    wq.state=WQ_IDLE;
}
static void wq_step(ms_t now){
    if(wq.n==0) return;
    if(wq.state==WQ_IDLE){ wq.tries=1; wq.state=WQ_WAIT; wq_tx(now); return; }
    if(wq.sim_ack && now-wq.sent_ms>=40){   /* preview only: simulated ACK */
        WFrame*f=&wq.q[0]; for(int k=0;k<f->len&&f->sub+k<REGN;k++) reg[f->sub+k]=f->w[k];
        wq_pop(1); return;
    }
    if(wq.tries==1 && now-wq.sent_ms>=WQ_ACK1_MS){ wq.tries=2; wq.n_retx++; wq_tx(now); }
    else if(wq.tries>=2 && now-wq.sent_ms>=WQ_ACK2_MS){ wq_pop(0); }
}
static void wq_on_ack(uint8_t sub,uint8_t L,const uint8_t*payload){
    if(wq.n==0||wq.state!=WQ_WAIT) return;
    WFrame*f=&wq.q[0];
    if(f->sub!=sub||f->len!=L) return;
    for(int k=0;k<L;k++) if(f->w[k]!=(uint16_t)(payload[2*k]|(payload[2*k+1]<<8))) return;
    wq_pop(1);
}

/* ---- BOOT PUSH: state (v6.5: all build types; the logic is at boot_push_step) ---- */
enum { BP_OFF=0, BP_WAIT_A, BP_A, BP_WAIT_B, BP_B, BP_DONE };
enum { BPR_NONE=0, BPR_OK, BPR_NOACK, BPR_BLOCKED, BPR_NOCFG, BPR_SKIP };
static struct { int state, ra, rb, runs, guard; long seq; ms_t t0; int t0_set; } bp;   /* t0: when the push left WAITING (block queued / settled) */
static int bp_on=1;                    /* MERAKIPLUS_NOBOOTPUSH / MERAKIPLUS_SIM_NOBOOTPUSH turn the boot push off */
static int repush_count(void);         /* v6.6: number of re-pushes after an MCU restart (repush_step) */

/* ---- RX frame parse (v4 logic + ACK queue + timestamps) — v6.5: RESYNC WATCHDOG ----
 * A header candidate (AA 55 …) only becomes a frame when the WHOLE frame has been received and the CRC
 * matches; otherwise it advances one byte and searches for the next AA 55. No bytes are consumed
 * without a CRC verdict (v6.4 skipped cmd 3 and cmd 8/0x88 without CRC and could thus swallow the start of
 * a valid frame right after noise). The only thing that can keep the parser stuck is a candidate whose
 * length field promises more bytes than have arrived. That situation has two guards:
 *  (1) IMPOSSIBLE LENGTH: LEN > 129 words does not exist (the register file is 129 words) -> discarded immediately.
 *      (v6.4: the parser stayed stuck here until the 8 kB buffer overflowed — approx. 3.5 s without RX.)
 *  (2) WATCHDOG: an incomplete candidate that has waited RX_RESYNC_MS is discarded; the rest of
 *      the buffer is searched for the next AA 55, so valid frames that arrived BEHIND it are not lost.
 *      Bytes whose latest arrival is RX_RESYNC_MS old cannot be a frame in transit and
 *      are discarded the same way. main() calls rx_feed(…,0,now) on every pass, so the guard also
 *      works when nothing arrives.
 * RX_RESYNC_MS = 400 ms, chosen from the measurements (UI_v6.md CHANGES-v6.5 item 3):
 *  - the longest valid frame (2*129+7 = 265 bytes) takes 46 ms on the line at 57600 8N1, and
 *    the MCU sends a frame in one go — a valid gap IN THE MIDDLE of a frame has not been seen;
 *  - the MCU's longest measured silence is 279 ms (first ACK after a start write, BREW.md §3.2);
 *    400 ms is 1.4x that — a frame split by such a long gap is still received;
 *  - 400 ms is below the write queue's ACK budget (150 + 300 = 450 ms), so a stuck candidate in front of
 *    an ACK does not by itself turn into "SAVE FAILED", and far below the OFFLINE limit (1500 ms).
 * Gaps BETWEEN frames (poll cadence ~100 ms, measured up to 456 ms) do not affect the watchdog:
 * the buffer is empty in between, and it only measures a candidate waiting for the rest of itself.
 * rx_resync (DIAGNOSTICS "RX CRC / RESYNC") counts discarded candidates: impossible length, watchdog
 * (one per call) and buffer overflow. CRC errors on a complete frame are counted in rx_bad as before. */
#define RX_RESYNC_MS 400
static uint32_t rx_resync=0;
/* v6.6 (MCU restart -> re-push, see repush_step): the controller's version requests and fresh reads of words 112..113 */
static unsigned ver_n=0, rx_n112=0; static ms_t ver_ms=0;
static ms_t rx_pend_ms=0, rx_last_ms=0;     /* when the head of rxbuf started waiting / when the latest byte arrived */
static void pw_ack8(const uint8_t*p);       /* v6.14: the controller's acknowledgement (0x88) of the power-off sequence's time/alarm frame */
static void rx_feed(const uint8_t*d,int n,ms_t now){
    if(n<=0&&rxlen==0) return;
    if(rxlen+n>(int)sizeof(rxbuf)){ rxlen=0; rx_resync++; }
    int old_n=rxlen; ms_t old_last=rx_last_ms;
    if(n>0){ if(rxlen==0) rx_pend_ms=now; memcpy(rxbuf+rxlen,d,n); rxlen+=n; rx_last_ms=now; }
    /* the latest time bytes from position i can have started waiting: the head has its own clock,
     * older bytes behind it arrived at the latest at the previous arrival, and new bytes arrived now */
#define RX_REF(i) ((i)==0?rx_pend_ms:((i)<old_n?old_last:now))
    int i=0, fired=0;
    while(i+7<=rxlen){
        if(rxbuf[i]!=0xAA||rxbuf[i+1]!=0x55){ i++; continue; }
        uint8_t cmd=rxbuf[i+2]; int ver=(cmd==0x09||cmd==0x89), tot;
        if(ver) tot=rxbuf[i+3]+7;                           /* version frames: LEN is bytes (PROTOCOL §2.2) */
        else if(cmd==0x08||cmd==0x88) tot=15;               /* power/time: fixed 15 bytes (§2.3) */
        else if(cmd==0x03) tot=7;                           /* query: no payload */
        else if(rxbuf[i+4]>REGN){ rx_resync++; i++; continue; }   /* (1) impossible length */
        else tot=2*rxbuf[i+4]+7;
        if(i+tot>rxlen){                                    /* incomplete: wait — but at most RX_RESYNC_MS */
            if(now-RX_REF(i)<RX_RESYNC_MS) break;
            fired=1; i++; continue;                         /* (2) watchdog */
        }
        uint16_t rx=crc16(rxbuf+i,tot-2);
        uint16_t got=rxbuf[i+tot-2]|(rxbuf[i+tot-1]<<8);
        if(rx!=got){ rx_bad++; last_bad_ms=now; i++; continue; }
        if(ver){ rx_ok++; last_rx_ms=now; if(cmd==0x89){ frame_version(); ver_n++; ver_ms=now; } }
        else if(cmd==0x88) pw_ack8(rxbuf+i+3);                 /* v6.14: only counted for DIAGNOSTICS — stock does not wait for it either */
        else if(cmd!=0x08&&cmd!=0x88&&cmd!=0x03){
            uint8_t sub=rxbuf[i+3], L=rxbuf[i+4];
            rx_ok++; last_rx_ms=now;
            if(cmd==0x83||cmd==0x86){
                for(int k=0;k<L;k++){
                    int idx=sub+k; if(idx>=REGN) break;
                    reg[idx]=rxbuf[i+5+2*k]|(rxbuf[i+6+2*k]<<8); reg_seen[idx]=1;
                }
                if(cmd==0x83&&sub<=112&&sub+L>113) rx_n112++;
                if(cmd==0x86) wq_on_ack(sub,L,rxbuf+i+5);
            }
        }
        i+=tot;
    }
    /* fewer than 7 bytes left: keep only what can be the start of a header — and only while it is fresh */
    if(i+7>rxlen){
        while(i<rxlen&&!(rxbuf[i]==0xAA&&(i+1>=rxlen||rxbuf[i+1]==0x55))) i++;
        if(i<rxlen&&now-RX_REF(i)>=RX_RESYNC_MS){ fired=1; i=rxlen; }
    }
    if(fired) rx_resync++;
    if(i>0){ rx_pend_ms=RX_REF(i); memmove(rxbuf,rxbuf+i,rxlen-i); rxlen-=i; }
#undef RX_REF
}

