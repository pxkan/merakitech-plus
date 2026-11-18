/* keyinj.c — EV_KEY-injektor til TEST af v6.14's tænd/sluk-knap på event1 (k62091).
 * Linux evdev write-injection (samme mekanisme som tinj på event0): write() af
 * struct input_event leveres til alle læsere af noden — herunder ourui's kbd_poll.
 * Kode 28 = k62091-knappen (B: KEY=10000000). Ingen EVIOCGRAB, ingen hardware-aktuator.
 *
 * Hvorfor: på denne enhed skærer et FYSISK hold (>= ~1,5 s) strømmen til displayet
 * (kernel/PMIC) UDEN at levere input-events — ourui's sluk-vej kan derfor ikke udløses
 * fysisk her. Injicerede events beviser sluk-vejen (cmd8 + cmd6 sub0 len2 [0,0] + dvale)
 * på den LIVE v6.14-binær med den ÆGTE MCU, mens displayet lever (telnet/fb intakt).
 * TÆND-halvdelen kræver stadig et fysisk hold (MCU'en vågner kun af sin egen GPIO).
 *
 * Brug: keyinj click <ms> | keyinj down | keyinj up   (node: KEYINJ_DEV, default /dev/input/event1;
 *       kode: KEYINJ_CODE, default 28. OBS: input-core kasserer injicerede koder noden ikke deklarerer
 *       i keybit — på event0 (touch) bruges en deklareret kode, fx 10; ourui tæller ALLE EV_KEY-koder.)
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
    if(argc<2){ fprintf(stderr,"brug: keyinj click <ms> | down | up\n"); return 2; }
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
    else { fprintf(stderr,"ukendt mode\n"); return 2; }
    close(fd);
    return 0;
}
