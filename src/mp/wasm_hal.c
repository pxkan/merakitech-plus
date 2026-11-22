/* src/mp/wasm_hal.c — the browser build's HAL (MERAKIPLUS_WASM, Sim Studio: sim/README.md "Sim Studio").
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c after main.c, only when MERAKIPLUS_WASM is defined.
 *
 * The browser build is the SIM build (SIM_RT, src/mp/sim_hal.c) compiled for wasm32-wasi as a WASI "reactor": there is no
 * main() and no blocking loop. The JavaScript host (sim/studio/meraki.js + sim/studio/wasi.js) owns the clock, the files
 * and the UART, and drives the UI through these exports:
 *   mp_init()            main()'s startup sequence (src/mp/main_setup.c, the SAME fragment main() runs)
 *   mp_step()            ONE main-loop iteration (src/mp/main_step.c, the SAME fragment main()'s while(run) runs);
 *                        returns bit0 = the fb changed during this call, bit1 = the UI has stopped (run == 0)
 *   mp_touch(x,y,kind)   a touch sample in screen pixels: kind 1 = down, 2 = move, 0 = up — queued as the raw evdev records
 *                        the device's touch node delivers, decoded by the SAME inp_poll() on the next mp_step()
 *   mp_fb() / mp_fb_rgba()  the 480x480 framebuffer (BGRA as on the device) / an RGBA copy for a canvas ImageData
 *   mp_fb_seq()          frame counter (increments when fb_present() changed pixels)
 *   mp_inbuf()           8 KB buffer the host writes a NUL-terminated JSON request into, then calls
 *   mp_ctrl()            -> the SIM's test-hook dispatcher (sim_dispatch: ping, layout, action, save, page, touch, key, clock,
 *                        raw_write, config_reload, …; sim/CONTRACT.md) — returns a pointer to the NUL-terminated JSON reply
 *   mp_state()           writes a fresh state JSON line (sim/CONTRACT.md §3) to MERAKIPLUS_SIM_STATE, which the host's
 *                        WASI shim keeps as "the latest line"
 * Time: now_ms() is clock_gettime(CLOCK_MONOTONIC) -> WASI clock_time_get, answered by the host from ITS clock (pause,
 * 1x, 20x …). UART: MERAKIPLUS_SIM_TTY names a WASI device file the host implements as a byte pipe to its machine model,
 * so the UART path (uart_open/read/uart_flush) is the SIM's own. Nothing here changes what goes over the UART. */
#define WX(name) __attribute__((export_name(#name),used))

/* ---- POSIX calls WASI lacks (declared in wasm_compat.h): never reached in the browser, fail cleanly ---- */
pid_t fork(void){ errno=ENOSYS; return -1; }
pid_t setsid(void){ errno=ENOSYS; return -1; }
int dup2(int a,int b){ (void)a; (void)b; errno=ENOSYS; return -1; }
int execl(const char*p,const char*a,...){ (void)p; (void)a; errno=ENOSYS; return -1; }
int mkfifo(const char*p,mode_t m){ (void)p; (void)m; errno=ENOSYS; return -1; }
int socket(int a,int b,int c){ (void)a; (void)b; (void)c; errno=ENOSYS; return -1; }
int bind(int a,const struct sockaddr*b,unsigned c){ (void)a; (void)b; (void)c; errno=ENOSYS; return -1; }
int listen(int a,int b){ (void)a; (void)b; errno=ENOSYS; return -1; }
pid_t waitpid(pid_t p,int*st,int o){ (void)p; (void)st; (void)o; errno=ECHILD; return -1; }
/* termios: the "pty" is a WASI device file — raw already, nothing to configure */
int tcgetattr(int fd,struct termios*t){ (void)fd; memset(t,0,sizeof *t); return 0; }
int tcsetattr(int fd,int a,const struct termios*t){ (void)fd; (void)a; (void)t; return 0; }
void cfmakeraw(struct termios*t){ (void)t; }
int cfsetispeed(struct termios*t,speed_t s){ (void)t; (void)s; return 0; }
int cfsetospeed(struct termios*t,speed_t s){ (void)t; (void)s; return 0; }
int tcflush(int fd,int q){ (void)fd; (void)q; return 0; }
int tcdrain(int fd){ (void)fd; return 0; }

/* ---- lifecycle ---- */
static int wasm_inited=0;
WX(mp_init) int mp_init(void){
    if(wasm_inited) return 1;
    wasm_inited=1;
#include "main_setup.c"
    return 0;
}
WX(mp_step) int mp_step(void){
    if(!wasm_inited||!run) return 2;
    long seq0=sim_fb_seq;
    ms_t now=now_ms();
    do {
#include "main_step.c"
    } while(0);
    return (sim_fb_seq!=seq0?1:0)|(run?0:2);
}

/* ---- touch: the records tinj / the touch FIFO deliver (sim_hal.c's queue), due now ---- */
WX(mp_touch) void mp_touch(int x,int y,int kind){
    ms_t now=now_ms(), due=now>sim_ev_last()?now:sim_ev_last();
    int rx=clampi((clampi(x,0,W-1)*4096+479)/480,0,4095), ry=clampi((clampi(y,0,H-1)*4096+479)/480,0,4095);   /* scalex() returns exactly x */
    if(kind==1) sim_ev_push(EV_ABS,ABS_MT_TRACKING_ID,1,due);
    if(kind!=0){ sim_ev_push(EV_ABS,ABS_MT_POSITION_X,rx,due); sim_ev_push(EV_ABS,ABS_MT_POSITION_Y,ry,due); sim_ev_push(EV_ABS,ABS_X,rx,due); sim_ev_push(EV_ABS,ABS_Y,ry,due); }
    if(kind==1) sim_ev_push(EV_KEY,BTN_TOUCH,1,due);
    if(kind==0){ sim_ev_push(EV_ABS,ABS_MT_TRACKING_ID,-1,due); sim_ev_push(EV_KEY,BTN_TOUCH,0,due); }
    sim_ev_push(EV_SYN,SYN_REPORT,0,due);
}

/* ---- framebuffer ---- */
static uint32_t wasm_rgba[W*H];
WX(mp_fb) uint32_t* mp_fb(void){ return fbmem; }
WX(mp_fb_seq) long mp_fb_seq(void){ return sim_fb_seq; }
WX(mp_fb_rgba) uint32_t* mp_fb_rgba(void){              /* BGRA (0xAARRGGBB words) -> RGBA bytes for ImageData */
    if(!fbmem) return wasm_rgba;
    for(int i=0;i<W*H;i++){ uint32_t p=fbmem[i]; wasm_rgba[i]=(p&0xFF00FF00u)|((p>>16)&0xFFu)|((p&0xFFu)<<16); }
    return wasm_rgba;
}

/* ---- test hooks + state ---- */
static char wasm_in[8192], wasm_out[16384];
WX(mp_inbuf) char* mp_inbuf(void){ return wasm_in; }
WX(mp_inbuf_size) int mp_inbuf_size(void){ return (int)sizeof wasm_in; }
WX(mp_ctrl) const char* mp_ctrl(void){
    wasm_in[sizeof wasm_in-1]=0;
    sim_dispatch(wasm_in,wasm_out,sizeof wasm_out,now_ms());
    return wasm_out;
}
WX(mp_state) void mp_state(void){ sim_save_track(); sim_state_emit(now_ms(),1); }
WX(mp_running) int mp_running(void){ return run; }
