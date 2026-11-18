/* tinj.c — touch injector for TESTING ourui's touch zones (evdev write injection).
 *
 * Linux evdev supports write() of struct input_event → input_inject_event,
 * so events are delivered to all readers of /dev/input/event0 (including ourui).
 * Used ONLY for page-switch tap/swipe + REVERT tap during the verification round.
 * Coordinates are chosen so they are also background on the original UI (between the four
 * round buttons / below them) → no actuator press if demo_v9 reads leftover events
 * after SIGCONT. GRIND/BREW coordinates are NEVER injected.
 *
 * Usage: tinj tap <x> <y>
 *       tinj swipe <x1> <y1> <x2> <y2>
 * Build: zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -o tinj tinj.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>

static int fd=-1;
/* raw range auto via EVIOCGABS; fallback = the device's constant 0..480 (FINDINGS
 * 'Gestus-debug' — not 4095). Same formula as switcher/ourui/tinj_hold. */
static int xmin=0,xmax=480,ymin=0,ymax=480;
static void send(__u16 type,__u16 code,__s32 val){
    struct input_event ev; memset(&ev,0,sizeof ev);
    ev.type=type; ev.code=code; ev.value=val;
    if(write(fd,&ev,sizeof ev)!=sizeof ev){ perror("write ev"); exit(1); }
}
static void syn(void){ send(EV_SYN,SYN_REPORT,0); }
static int rawx(int x){ return xmin+(long)x*(xmax-xmin+1)/480; }
static int rawy(int y){ return ymin+(long)y*(ymax-ymin+1)/480; }
static void down(int x,int y){
    send(EV_ABS,ABS_MT_TRACKING_ID,1);
    send(EV_ABS,ABS_MT_POSITION_X,rawx(x));
    send(EV_ABS,ABS_MT_POSITION_Y,rawy(y));
    send(EV_ABS,ABS_X,rawx(x));
    send(EV_ABS,ABS_Y,rawy(y));
    send(EV_KEY,BTN_TOUCH,1);
    syn();
}
static void move(int x,int y){
    send(EV_ABS,ABS_MT_POSITION_X,rawx(x));
    send(EV_ABS,ABS_MT_POSITION_Y,rawy(y));
    send(EV_ABS,ABS_X,rawx(x));
    send(EV_ABS,ABS_Y,rawy(y));
    syn();
}
static void up(void){
    send(EV_ABS,ABS_MT_TRACKING_ID,-1);
    send(EV_KEY,BTN_TOUCH,0);
    syn();
}
int main(int argc,char**argv){
    if(argc<4){ fprintf(stderr,"usage: tinj tap x y | tinj swipe x1 y1 x2 y2\n"); return 2; }
    fd=open("/dev/input/event0",O_WRONLY);
    if(fd<0){ perror("open event0"); return 1; }
    struct input_absinfo ai;
    if(ioctl(fd,EVIOCGABS(ABS_X),&ai)==0){ xmin=ai.minimum; xmax=ai.maximum; }
    if(ioctl(fd,EVIOCGABS(ABS_Y),&ai)==0){ ymin=ai.minimum; ymax=ai.maximum; }
    if(ioctl(fd,EVIOCGABS(ABS_MT_POSITION_X),&ai)==0){ xmin=ai.minimum; xmax=ai.maximum; }
    if(ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&ai)==0){ ymin=ai.minimum; ymax=ai.maximum; }
    if(strcmp(argv[1],"tap")==0&&argc>=4){
        int x=atoi(argv[2]),y=atoi(argv[3]);
        down(x,y); usleep(80000); up();
        printf("tap %d %d\n",x,y);
    } else if(strcmp(argv[1],"swipe")==0&&argc>=6){
        int x1=atoi(argv[2]),y1=atoi(argv[3]),x2=atoi(argv[4]),y2=atoi(argv[5]);
        down(x1,y1); usleep(50000);
        for(int i=1;i<=12;i++){
            move(x1+(x2-x1)*i/12, y1+(y2-y1)*i/12);
            usleep(20000);
        }
        usleep(50000); up();
        printf("swipe %d %d -> %d %d\n",x1,y1,x2,y2);
    } else { fprintf(stderr,"unknown mode\n"); return 2; }
    close(fd);
    return 0;
}