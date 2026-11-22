/* src/mp/input.c — touch input: raw evdev events -> DOWN/MOVE/UP.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ---- touch: raw events -> DOWN/MOVE/UP per SYN_REPORT ---- */
static int ifd=-1;
static int absx_min=0,absx_max=4095,absy_min=0,absy_max=4095;
static int in_x=-1,in_y=-1,in_down=0,in_pdown=0,in_pup=0,in_moved=0;
#ifdef DEVICE_RT
static void inp_open(void){
    ifd=open(INP,O_RDONLY|O_NONBLOCK);
    if(ifd<0){ ifd=-1; return; }
    struct input_absinfo ai;
    if(ioctl(ifd,EVIOCGABS(ABS_X),&ai)==0){ absx_min=ai.minimum; absx_max=ai.maximum; }
    if(ioctl(ifd,EVIOCGABS(ABS_Y),&ai)==0){ absy_min=ai.minimum; absy_max=ai.maximum; }
    if(ioctl(ifd,EVIOCGABS(ABS_MT_POSITION_X),&ai)==0){ absx_min=ai.minimum; absx_max=ai.maximum; }
    if(ioctl(ifd,EVIOCGABS(ABS_MT_POSITION_Y),&ai)==0){ absy_min=ai.minimum; absy_max=ai.maximum; }
    /* EVIOCGRAB: exclusive input while merakiplus runs, so the SIGSTOPped demo_v9 does not
     * get our touches in its evdev queue and replay them after SIGCONT (phantom touches in
     * the stock UI). Released automatically on close/exit. MERAKIPLUS_NOGRAB=1 disables it
     * (needed for tinj injection from another process). */
    if(!getenv("MERAKIPLUS_NOGRAB")){ if(ioctl(ifd,EVIOCGRAB,1)!=0) perror("EVIOCGRAB"); }
}
/* v6.14: THE POWER BUTTON — the k62091 node, read-only and WITHOUT EVIOCGRAB (stock does not grab it either; app_powerKey_thread
 * 0x140AB4 opens /dev/input/event1). Selection: MERAKIPLUS_KBD=<path> -> else the event node (not the touch node) whose name contains
 * "k62091" -> else the first one with EV_KEY and without EV_ABS -> else /dev/input/event1, if it can be opened. */
static int kfd=-1;
static int kbd_try(const char*path,int need_name,int*fdo,char*name,size_t nn){
    int fd=open(path,O_RDONLY|O_NONBLOCK); if(fd<0) return 0;
    name[0]=0; if(ioctl(fd,EVIOCGNAME((int)nn-1),name)<0) name[0]=0; name[nn-1]=0;
    unsigned long evb=0; if(ioctl(fd,EVIOCGBIT(0,sizeof evb),&evb)<0) evb=0;
    int ok= need_name==1 ? strstr(name,KBD_NAME)!=NULL : (need_name==2 ? ((evb&(1UL<<EV_KEY))&&!(evb&(1UL<<EV_ABS))) : 1);
    if(!ok){ close(fd); return 0; }
    *fdo=fd; return 1;
}
static void kbd_open(void){
    char path[48], name[40]; const char*e=getenv("MERAKIPLUS_KBD"); pw.kbd_ok=0; kfd=-1;
    if(e){ snprintf(path,sizeof path,"%s",e); if(!kbd_try(path,0,&kfd,name,sizeof name)) kfd=-1; }
    for(int pass=1;kfd<0&&!e&&pass<=2;pass++)
        for(int i=0;i<10&&kfd<0;i++){ snprintf(path,sizeof path,"/dev/input/event%d",i); if(!strcmp(path,INP)) continue; kbd_try(path,pass,&kfd,name,sizeof name); }
    if(kfd<0&&!e){ snprintf(path,sizeof path,"%s",INP_KBD); kbd_try(path,0,&kfd,name,sizeof name); }
    if(kfd<0){ pw.node[0]=0; return; }
    pw.kbd_ok=1; snprintf(pw.node,sizeof pw.node,"%s · %s",path,name[0]?name:"?");
}
static void kbd_poll(ms_t now){
    if(kfd<0) return;
    struct input_event ev;
    while(read(kfd,&ev,sizeof ev)==(ssize_t)sizeof ev) if(ev.type==EV_KEY) pw_key(ev.code,ev.value,now);
}
#endif
static int scalex(int v){ return (int)((long)(v-absx_min)*W/(absx_max-absx_min+1)); }
static int scaley(int v){ return (int)((long)(v-absy_min)*H/(absy_max-absy_min+1)); }
