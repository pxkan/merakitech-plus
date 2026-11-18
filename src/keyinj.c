/* keyinj.c — EV_KEY injector for TESTING v6.14's power button on event1 (k62091).
 * Linux evdev write injection (same mechanism as tinj on event0): write() of a
 * struct input_event is delivered to all readers of the node — including ourui's kbd_poll.
 * Code 28 = the k62091 button (B: KEY=10000000). No EVIOCGRAB, no hardware actuator.
 *
 * Why: on this device a PHYSICAL hold (>= ~1.5 s) cuts power to the display
 * (kernel/PMIC) WITHOUT delivering input events — so ourui's power-off path cannot be
 * triggered physically here. Injected events prove the power-off path (cmd8 + cmd6 sub0 len2 [0,0] + deep sleep)
 * on the LIVE v6.14 binary with the REAL MCU while the display stays alive (telnet/fb intact).
 * The power-ON half still requires a physical hold (the MCU only wakes on its own GPIO).
 *
 * Usage: keyinj click <ms> | keyinj down | keyinj up   (node: KEYINJ_DEV, default /dev/input/event1;
 *       code: KEYINJ_CODE, default 28. NOTE: the input core discards injected codes the node does not
 *       declare in keybit — on event0 (touch) use a declared code, e.g. 10; ourui counts ALL EV_KEY codes.)
 * Build: zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -o keyinj keyinj.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
static int fd=-1;
static void send(__u16 type,__u16 code,__s32 val){
    struct input_event ev; memset(&ev,0,sizeof ev);
    ev.type=type; ev.code=code; ev.value=val;
    if(write(fd,&ev,sizeof ev)!=sizeof ev){ perror("write ev"); exit(1); }
}
static void syn(void){ send(EV_SYN,SYN_REPORT,0); }
int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"usage: keyinj click <ms> | down | up\n"); return 2; }
    const char*dev=getenv("KEYINJ_DEV"); if(!dev||!*dev) dev="/dev/input/event1";
    const char*kc=getenv("KEYINJ_CODE"); int code=kc&&*kc?atoi(kc):28;
    fd=open(dev,O_WRONLY);
    if(fd<0){ perror("open keyinj-dev"); return 1; }
    if(strcmp(argv[1],"click")==0&&argc>=3){
        int ms=atoi(argv[2]);
        send(EV_KEY,code,1); syn();
        usleep(ms*1000);
        send(EV_KEY,code,0); syn();
        printf("click %d ms code %d\n",ms,code);
    } else if(strcmp(argv[1],"down")==0){ send(EV_KEY,code,1); syn(); printf("down\n"); }
    else if(strcmp(argv[1],"up")==0){ send(EV_KEY,code,0); syn(); printf("up\n"); }
    else { fprintf(stderr,"unknown mode\n"); return 2; }
    close(fd);
    return 0;
}
