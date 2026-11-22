/* src/mp/config.c — MD5, sysroot + boot id, config.json read + atomic field persist.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ================= MD5 (for the config log; RFC 1321) ================= */
typedef struct { uint32_t s[4]; uint64_t len; uint8_t buf[64]; int nb; } MD5;
static const uint32_t MD5K[64]={
 0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
 0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
 0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
 0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
 0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
 0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
 0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
 0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
static const uint8_t MD5R[64]={7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
 4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
static void md5_block(MD5*m,const uint8_t*p){
    uint32_t a=m->s[0],b=m->s[1],c=m->s[2],d=m->s[3],x[16];
    for(int i=0;i<16;i++) x[i]=p[4*i]|(p[4*i+1]<<8)|(p[4*i+2]<<16)|((uint32_t)p[4*i+3]<<24);
    for(int i=0;i<64;i++){
        uint32_t f; int g;
        if(i<16){ f=(b&c)|(~b&d); g=i; }
        else if(i<32){ f=(d&b)|(~d&c); g=(5*i+1)&15; }
        else if(i<48){ f=b^c^d; g=(3*i+5)&15; }
        else { f=c^(b|~d); g=(7*i)&15; }
        uint32_t t=d; d=c; c=b;
        uint32_t r=a+f+MD5K[i]+x[g]; b=b+((r<<MD5R[i])|(r>>(32-MD5R[i]))); a=t;
    }
    m->s[0]+=a; m->s[1]+=b; m->s[2]+=c; m->s[3]+=d;
}
static void md5_hex(const uint8_t*data,size_t n,char out[33]){
    MD5 m; m.s[0]=0x67452301; m.s[1]=0xefcdab89; m.s[2]=0x98badcfe; m.s[3]=0x10325476;
    size_t i=0; for(;i+64<=n;i+=64) md5_block(&m,data+i);
    uint8_t t[128]; size_t r=n-i; memcpy(t,data+i,r); t[r++]=0x80;
    size_t pad=(r<=56)?56:120; memset(t+r,0,pad-r);
    uint64_t bits=(uint64_t)n*8; for(int k=0;k<8;k++) t[pad+k]=(uint8_t)(bits>>(8*k));
    md5_block(&m,t); if(pad==120) md5_block(&m,t+64);
    for(int k=0;k<4;k++) for(int j=0;j<4;j++) sprintf(out+8*k+2*j,"%02x",(m.s[k]>>(8*j))&0xFF);
    out[32]=0;
}

/* ================= sysroot + boot-id (v6.2) =================
 * All boot-related paths (/customer, /new_customer, /proc, /tmp) go through rp(), so
 * the flag/switch logic can be run against a mock root (MERAKIPLUS_ROOT / selftest). Empty = real root. */
static char sysroot[200]="";
static const char* rp(const char*p){
    static char b[4][400]; static int k;
    if(!sysroot[0]) return p;
    char*o=b[k++&3]; snprintf(o,400,"%s%s",sysroot,p); return o;
}
static int64_t mono_fake=-1;                               /* selftest only */
static int64_t mono_s(void){ return mono_fake>=0?mono_fake:now_ms()/1000; }   /* seconds since boot (64 bit, v6.6) */
static void boot_id_read(char*out,size_t n){
    out[0]=0; int fd=open(rp("/proc/sys/kernel/random/boot_id"),O_RDONLY); if(fd<0) return;
    int k=read(fd,out,n-1); close(fd); if(k<0)k=0; out[k]=0; out[strcspn(out," \r\n")]=0;
}

/* ================= config.json: read + atomic field persist =================
 * Pattern from WRITETEST.md §7: the byte-exact cJSON format is preserved (only the number token of
 * the one field is replaced), tmp file in the SAME directory + fsync + verify + rename().
 * demo_v9 (SIGSTOPped) holds a write fd on the old inode -> no race.
 * Before the first write: pristine backup (config.json.orig, never overwritten).
 * Before every write: rolling backup (config.json.bak). Every write is logged w. md5. */
#define CFG_MAX 16384
static char cfg_path[256]="/customer/config.json";
static char cfg_bakdir[256]="/new_customer/merakiplus/cfgbak";
static char cfgbuf[CFG_MAX]; static int cfglen=0; static int cfg_ok=0;
static char cfg_md5[33]="-"; static int cfg_writes=0; static char cfg_err[64]="";

static int cfg_read(void){
    cfg_ok=0; cfglen=0;
    int fd=open(cfg_path,O_RDONLY); if(fd<0){ snprintf(cfg_err,sizeof cfg_err,"open: %s",strerror(errno)); return 0; }
    int n=read(fd,cfgbuf,CFG_MAX-1); close(fd);
    if(n<=2){ snprintf(cfg_err,sizeof cfg_err,"empty file"); return 0; }
    if(n>=CFG_MAX-1){ snprintf(cfg_err,sizeof cfg_err,"too large"); return 0; }
    cfglen=n; cfgbuf[n]=0; cfg_ok=1; md5_hex((uint8_t*)cfgbuf,cfglen,cfg_md5);
    return 1;
}
/* find the value span for a top-level key (depth 1; ignores presetStore contents) */
static int cfg_find(const char*buf,int len,const char*key,int*vs,int*ve){
    int depth=0,i=0,kl=strlen(key),hits=0;
    while(i<len){
        char c=buf[i];
        if(c=='"'){
            int s=i+1; i++;
            while(i<len&&buf[i]!='"'){ if(buf[i]=='\\') i++; i++; }
            int e=i; i++;
            if(depth==1){
                int j=i; while(j<len&&(buf[j]==' '||buf[j]=='\t'||buf[j]=='\r'||buf[j]=='\n')) j++;
                if(j<len&&buf[j]==':'&&e-s==kl&&!memcmp(buf+s,key,kl)){
                    j++; while(j<len&&(buf[j]==' '||buf[j]=='\t')) j++;
                    int a=j; if(j<len&&buf[j]=='-') j++;
                    while(j<len&&buf[j]>='0'&&buf[j]<='9') j++;
                    if(j>a&&(buf[j]==','||buf[j]=='\n'||buf[j]=='\r'||buf[j]=='}'||buf[j]==' '||buf[j]=='\t')){ *vs=a; *ve=j; hits++; }
                }
            }
            continue;
        }
        if(c=='{'||c=='[') depth++;
        else if(c=='}'||c==']') depth--;
        i++;
    }
    return hits==1;
}
static int cfg_get_int(const char*key,int*out){
    int a,b; if(!cfg_ok||!cfg_find(cfgbuf,cfglen,key,&a,&b)) return 0;
    *out=atoi(cfgbuf+a); return 1;
}
static int write_all(const char*path,const char*data,int n,int excl){
    int fd=open(path,O_WRONLY|O_CREAT|(excl?O_EXCL:O_TRUNC),0644); if(fd<0) return 0;
    int off=0; while(off<n){ int k=write(fd,data+off,n-off); if(k<=0){ close(fd); return 0; } off+=k; }
    if(fsync(fd)!=0){ close(fd); return 0; }
    close(fd); return 1;
}
static void fsync_dir(const char*filepath){
    char d[256]; snprintf(d,sizeof d,"%s",filepath); char*s=strrchr(d,'/'); if(!s) return; if(s==d) s[1]=0; else *s=0;
    int fd=open(d,O_RDONLY); if(fd>=0){ fsync(fd); close(fd); }
}
/* returns 1 on success; on failure config.json is unchanged and cfg_err is set */
static int cfg_set_int(const char*key,int newv){
    char cur[CFG_MAX]; char md_before[33],md_after[33];
    /* 1) fresh read (never from cache) */
    int fd=open(cfg_path,O_RDONLY); if(fd<0){ snprintf(cfg_err,sizeof cfg_err,"open: %s",strerror(errno)); return 0; }
    int n=read(fd,cur,CFG_MAX-1); close(fd);
    if(n<=2||n>=CFG_MAX-1){ snprintf(cfg_err,sizeof cfg_err,"read error"); return 0; }
    cur[n]=0; md5_hex((uint8_t*)cur,n,md_before);
    int a,b; if(!cfg_find(cur,n,key,&a,&b)){ snprintf(cfg_err,sizeof cfg_err,"field missing: %s",key); return 0; }
    int oldv=atoi(cur+a);
    /* 2) backups */
    mkdir(cfg_bakdir,0755);
    char p[320];
    snprintf(p,sizeof p,"%s/config.json.orig",cfg_bakdir);
    if(access(p,F_OK)!=0){ if(!write_all(p,cur,n,1)){ snprintf(cfg_err,sizeof cfg_err,"orig backup failed"); return 0; } fsync_dir(p); }
    char pt[340]; snprintf(p,sizeof p,"%s/config.json.bak",cfg_bakdir); snprintf(pt,sizeof pt,"%s.tmp",p);
    if(!write_all(pt,cur,n,0)||rename(pt,p)!=0){ snprintf(cfg_err,sizeof cfg_err,"backup failed"); return 0; }
    /* 3) build the new content: only the number token is replaced */
    char nb[CFG_MAX]; char num[16]; int nl=snprintf(num,sizeof num,"%d",newv);
    int nn=a+nl+(n-b); if(nn>=CFG_MAX-1){ snprintf(cfg_err,sizeof cfg_err,"too large"); return 0; }
    memcpy(nb,cur,a); memcpy(nb+a,num,nl); memcpy(nb+a+nl,cur+b,n-b); nb[nn]=0;
    /* 4) tmp in the same directory -> fsync -> re-read+compare -> rename */
    char tmp[300]; snprintf(tmp,sizeof tmp,"%s",cfg_path);
    char*sl=strrchr(tmp,'/'); if(sl) snprintf(sl+1,sizeof tmp-(sl+1-tmp),".merakiplus_cfg.tmp"); else snprintf(tmp,sizeof tmp,".merakiplus_cfg.tmp");
    if(!write_all(tmp,nb,nn,0)){ snprintf(cfg_err,sizeof cfg_err,"tmp-write: %s",strerror(errno)); unlink(tmp); return 0; }
    char chk[CFG_MAX]; fd=open(tmp,O_RDONLY); int cn=fd>=0?read(fd,chk,CFG_MAX):-1; if(fd>=0) close(fd);
    if(cn!=nn||memcmp(chk,nb,nn)){ snprintf(cfg_err,sizeof cfg_err,"tmp verification failed"); unlink(tmp); return 0; }
    if(rename(tmp,cfg_path)!=0){ snprintf(cfg_err,sizeof cfg_err,"rename: %s",strerror(errno)); unlink(tmp); return 0; }
    fsync_dir(cfg_path);
    md5_hex((uint8_t*)nb,nn,md_after);
    /* 5) log + flag */
    snprintf(p,sizeof p,"%s/config_writes.log",cfg_bakdir);
    FILE*lf=fopen(p,"a");
    if(lf){
        time_t t=time(NULL); struct tm tmv; gmtime_r(&t,&tmv); char ts[32]; strftime(ts,sizeof ts,"%Y-%m-%dT%H:%M:%SZ",&tmv);
        fprintf(lf,"%s %s %d->%d md5_before=%s md5_after=%s size=%d->%d\n",ts,key,oldv,newv,md_before,md_after,n,nn);
        fflush(lf); fsync(fileno(lf)); fclose(lf);
    }
    snprintf(p,sizeof p,"%s/RESTART_NEEDED",cfg_bakdir);
    lf=fopen(p,"w"); if(lf){ fprintf(lf,"config.json changed by merakiplus. The controller already has the values (cmd6 on SAVE).\n"
                           "The stock UI (demo_v9) reads config ONLY at startup: restart it (kill $(pidof demo_v9);\n"
                           "the watchdog starts it again) BEFORE stock settings are used. At startup it boot-pushes\n"
                           "the config values to the controller (cmd6 sub1 len30 + sub112 len3).\n");
                /* stamp for restart_pending(): a stock UI started AFTER this has read config itself */
                char bid[64]; boot_id_read(bid,sizeof bid); fprintf(lf,"boot_id=%s mono_s=%lld\n",bid[0]?bid:"?",(long long)mono_s()); fclose(lf); }
    memcpy(cfgbuf,nb,nn+1); cfglen=nn; cfg_ok=1; strcpy(cfg_md5,md_after); cfg_writes++;
    return 1;
}

