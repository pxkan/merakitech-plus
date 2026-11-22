/* src/mp/gfx.c — graphics engine: canvas, circle clip, AA primitives, background, stroke font, dirty rects, gauge LUT, compositor, framebuffer, transitions.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ====================================================================== */
/* ================= GRAPHICS: canvas, circle clip, AA primitives ====== */
/* ====================================================================== */
typedef struct { short x0,y0,x1,y1; } Rect;      /* [x0,x1) x [y0,y1) */
#define RL_MAX 16
typedef struct { int n; Rect r[RL_MAX]; } RL;
static const Rect FULL={0,0,W,H};

static uint32_t *canvas,*bgpure,*bgbase,*slay,*snapA,*snapB,*tbuf,*cv;
static Rect clipr={0,0,W,H};
static int cx0[H], cx1[H];   /* chord mask: safe x interval per row (GLOBAL CIRCLE CLIP) */

static void clip_init(void){
    for(int y=0;y<H;y++){
        int dy=y-CY;
        if(dy<=-RSAFE||dy>=RSAFE){ cx0[y]=1; cx1[y]=0; continue; }
        int h=(int)sqrt((double)(RSAFE*RSAFE-dy*dy));
        cx0[y]=CX-h; cx1[y]=CX+h;
    }
}
static inline uint32_t rgb(int r,int g,int b){ return (uint32_t)b | ((uint32_t)g<<8) | ((uint32_t)r<<16) | 0xFF000000u; }
static inline uint32_t mix(uint32_t d,uint32_t s,int a){
    a+=a>>7;
    uint32_t rb=(((d&0xFF00FF)*(256-a)+(s&0xFF00FF)*a)>>8)&0xFF00FF;
    uint32_t g =(((d&0x00FF00)*(256-a)+(s&0x00FF00)*a)>>8)&0x00FF00;
    return 0xFF000000u|rb|g;
}
static inline uint32_t lerpc(uint32_t a,uint32_t b,float t){ if(t<=0) return a; if(t>=1) return b; return mix(a,b,(int)(t*255)); }
static inline float clampf(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }
static void set_clip(Rect r){ clipr=r; if(clipr.x0<0)clipr.x0=0; if(clipr.y0<0)clipr.y0=0; if(clipr.x1>W)clipr.x1=W; if(clipr.y1>H)clipr.y1=H; }
static Rect R(int x,int y,int w,int h){ Rect r={(short)x,(short)y,(short)(x+w),(short)(y+h)}; return r; }

/* pset: ALL primitives end here or in hspan -> nothing outside the circle, ever */
static inline void pset(int x,int y,uint32_t c,int a){
    if(a<=0||y<clipr.y0||y>=clipr.y1||x<clipr.x0||x>=clipr.x1) return;
    if(x<cx0[y]||x>cx1[y]) return;
    uint32_t*p=&cv[y*W+x]; *p = a>=255? c : mix(*p,c,a);
}
static void hspan(int y,int x0,int x1,uint32_t c,int a){
    if(a<=0||y<clipr.y0||y>=clipr.y1) return;
    if(x0<clipr.x0)x0=clipr.x0; if(x1>clipr.x1)x1=clipr.x1;
    if(x0<cx0[y])x0=cx0[y]; if(x1>cx1[y]+1)x1=cx1[y]+1;
    uint32_t*p=cv+y*W;
    if(a>=255) for(int x=x0;x<x1;x++) p[x]=c;
    else for(int x=x0;x<x1;x++) p[x]=mix(p[x],c,a);
}
static void fill_rect(int x,int y,int w,int h,uint32_t c,int a){ for(int j=0;j<h;j++) hspan(y+j,x,x+w,c,a); }
/* rounded rectangle w. AA corners and vertical gradient ct->cb */
static void rrect_g(int x,int y,int w,int h,float r,uint32_t ct,uint32_t cb,int a){
    if(w<=0||h<=0) return;
    if(r>h/2.f) r=h/2.f; if(r>w/2.f) r=w/2.f; int ri=(int)ceilf(r);
    for(int j=0;j<h;j++){
        int yy=y+j; if(yy<clipr.y0||yy>=clipr.y1) continue;
        uint32_t c=(ct==cb||h<2)? ct : lerpc(ct,cb,(float)j/(h-1));
        float dy=0; if(j<ri) dy=r-0.5f-j; else if(j>=h-ri) dy=j+0.5f-(h-r);
        if(dy<=0){ hspan(yy,x,x+w,c,a); continue; }
        for(int i=0;i<ri;i++){
            float dx=r-0.5f-i; float cov=r+0.5f-sqrtf(dx*dx+dy*dy);
            if(cov<=0) continue; if(cov>1)cov=1; int aa=(int)(a*cov);
            pset(x+i,yy,c,aa); pset(x+w-1-i,yy,c,aa);
        }
        hspan(yy,x+ri,x+w-ri,c,a);
    }
}
static void rrect(int x,int y,int w,int h,float r,uint32_t c,int a){ rrect_g(x,y,w,h,r,c,c,a); }
static void disc(float cx,float cy,float r,uint32_t c,int a){
    int y0=(int)floorf(cy-r-1), y1=(int)ceilf(cy+r+1);
    for(int y=y0;y<y1;y++){
        if(y<clipr.y0||y>=clipr.y1) continue;
        float dy=y+0.5f-cy; float hh=(r+1)*(r+1)-dy*dy; if(hh<=0) continue;
        float hx=sqrtf(hh); int xa=(int)floorf(cx-hx), xb=(int)ceilf(cx+hx);
        float in=(r-1)*(r-1)-dy*dy; int ia=xb, ib=xb;
        if(in>0){ float ix=sqrtf(in); ia=(int)ceilf(cx-ix); ib=(int)floorf(cx+ix); }
        for(int x=xa;x<xb;x++){
            if(x>=ia&&x<ib){ hspan(y,ia,ib,c,a); x=ib-1; continue; }
            float dx=x+0.5f-cx; float cov=r+0.5f-sqrtf(dx*dx+dy*dy);
            if(cov<=0) continue; if(cov>1)cov=1; pset(x,y,c,(int)(a*cov));
        }
    }
}
/* annulus r0..r1 (AA) */
static void ring(float cx,float cy,float r0,float r1,uint32_t c,int a){
    int y0=(int)floorf(cy-r1-1), y1=(int)ceilf(cy+r1+1);
    for(int y=y0;y<y1;y++){
        if(y<clipr.y0||y>=clipr.y1) continue;
        float dy=y+0.5f-cy; float hh=(r1+1)*(r1+1)-dy*dy; if(hh<=0) continue;
        float hx=sqrtf(hh); int xa=(int)floorf(cx-hx), xb=(int)ceilf(cx+hx);
        float in=(r0-1)*(r0-1)-dy*dy; int ia=xb, ib=xb;
        if(r0>1&&in>0){ float ix=sqrtf(in); ia=(int)ceilf(cx-ix); ib=(int)floorf(cx+ix); }
        for(int x=xa;x<xb;x++){
            if(x>=ia&&x<ib){ x=ib-1; continue; }
            float dx=x+0.5f-cx; float d=sqrtf(dx*dx+dy*dy);
            float cov=fminf(d-r0+0.5f,r1-d+0.5f);
            if(cov<=0) continue; if(cov>1)cov=1; pset(x,y,c,(int)(a*cov));
        }
    }
}
static inline float seg_dist(float px,float py,float x0,float y0,float x1,float y1){
    float vx=x1-x0, vy=y1-y0, wx=px-x0, wy=py-y0;
    float l2=vx*vx+vy*vy; float t=l2>0? (wx*vx+wy*vy)/l2 : 0; if(t<0)t=0; if(t>1)t=1;
    float dx=wx-t*vx, dy=wy-t*vy; return sqrtf(dx*dx+dy*dy);
}
/* thick AA line w. round caps */
static void capsule(float x0,float y0,float x1,float y1,float hw,uint32_t c,int a){
    int xa=(int)floorf(fminf(x0,x1)-hw-1), xb=(int)ceilf(fmaxf(x0,x1)+hw+1);
    int ya=(int)floorf(fminf(y0,y1)-hw-1), yb=(int)ceilf(fmaxf(y0,y1)+hw+1);
    if(ya<clipr.y0)ya=clipr.y0; if(yb>clipr.y1)yb=clipr.y1; if(xa<clipr.x0)xa=clipr.x0; if(xb>clipr.x1)xb=clipr.x1;
    for(int y=ya;y<yb;y++) for(int x=xa;x<xb;x++){
        float cov=hw+0.5f-seg_dist(x+0.5f,y+0.5f,x0,y0,x1,y1);
        if(cov<=0) continue; if(cov>1)cov=1; pset(x,y,c,(int)(a*cov));
    }
}
/* generic AA arc (degrees, screen coords: 0=right, 90=down), round caps; for small arcs */
static void arc_aa(float cx,float cy,float r,float hw,float a0,float a1,uint32_t c,int a){
    if(a1<=a0) return;
    float sx=cx+r*cosf(a0*(float)M_PI/180), sy=cy+r*sinf(a0*(float)M_PI/180);
    float ex=cx+r*cosf(a1*(float)M_PI/180), ey=cy+r*sinf(a1*(float)M_PI/180);
    float sweep=a1-a0; float ro=r+hw+1;
    int y0=(int)floorf(cy-ro), y1=(int)ceilf(cy+ro);
    for(int y=y0;y<y1;y++){
        if(y<clipr.y0||y>=clipr.y1) continue;
        float dy=y+0.5f-cy; float hh=ro*ro-dy*dy; if(hh<=0) continue;
        float hx=sqrtf(hh); int xa=(int)floorf(cx-hx), xb=(int)ceilf(cx+hx);
        for(int x=xa;x<xb;x++){
            float dx=x+0.5f-cx; float rho=sqrtf(dx*dx+dy*dy);
            if(rho<r-hw-1) continue;
            float d;
            if(sweep>=360) d=fabsf(rho-r);
            else {
                float ang=atan2f(dy,dx)*180/(float)M_PI; float rel=fmodf(ang-a0+720,360);
                if(rel<=sweep) d=fabsf(rho-r);
                else { float d1=hypotf(x+0.5f-sx,y+0.5f-sy), d2=hypotf(x+0.5f-ex,y+0.5f-ey); d=fminf(d1,d2); }
            }
            float cov=hw+0.5f-d; if(cov<=0) continue; if(cov>1)cov=1; pset(x,y,c,(int)(a*cov));
        }
    }
}

/* ================= background: radial vignette w. ordered dither (no banding) ================= */
static void bg_build(void){
    static const uint8_t B4[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        uint32_t*p=&bgpure[y*W+x];
        if(x<cx0[y]||x>cx1[y]){ *p=0xFF000000u; continue; }
        float dx=x+0.5f-CX, dy=y+0.5f-CY; float d=sqrtf(dx*dx+dy*dy)/RSAFE;
        float t=powf(d,1.7f);
        /* slight light offset upwards (soft top highlight) */
        float lift=clampf(0.5f-dy/480.f,0,1)*0.35f;
        float r=(23-17*t)+3*lift*(1-t), g=(25-18*t)+3*lift*(1-t), b=(31-21*t)+4*lift*(1-t);
        float th=(B4[(y&3)*4+(x&3)]+0.5f)/16.f-0.5f;
        int ri=(int)floorf(r+th+0.5f), gi=(int)floorf(g+th+0.5f), bi=(int)floorf(b+th+0.5f);
        *p=rgb(ri<0?0:ri,gi<0?0:gi,bi<0?0:bi);
    }
}

/* ================= STROKE FONT (custom; AA via distance field, glyph cache) =================
 * Glyphs are stroke drawings on a 10-unit capital grid: M x,y | L x,y | E cx,cy,rx,ry,a0,a1.
 * Rasterized per (style, glyph) the first time and cached as 8-bit alpha. Digits are tabular
 * (all digits adv=6). Only capitals + digits + ÆØÅ + symbols/icons — the typography is
 * deliberately all-caps w. letter spacing. */
#define NG 160
#define G_LC 128            /* a..z in code spans (paths/commands): G_LC + (c - 'a') */
enum { G_AE=96, G_OE, G_AA, G_DEG, G_MDOT, G_BEAN, G_CUP, G_DROP, G_STEAM, G_CHECK, G_CROSS, G_MOON, G_WARN, G_APPROX, G_BOLT, G_ELL, G_SECT, G_CLOCK };
typedef struct { float adv; const char*path; } GDef;
static const GDef GD[NG]={
 [' ']={2.8f,""},
 ['0']={6,"M0,3 L0,7 E3,7,3,3,0,180 M6,7 L6,3 E3,3,3,3,180,360"},
 ['1']={6,"M1.6,2.3 L3.9,0 L3.9,10"},
 ['2']={6,"E3,3,3,3,180,405 M5.12,5.12 L0,10 L6,10"},
 ['3']={6,"E3,2.55,2.7,2.55,205,450 E3,7.45,3,2.55,270,515"},
 ['4']={6,"M4.6,10 L4.6,0 L0,7 L6,7"},
 ['5']={6,"M5.4,0 L1,0 L0.6,4.4 E2.9,6.75,3.1,3.25,228,505"},
 ['6']={6,"E3,6.75,3,3.25,0,360 M0,6.75 L0,4.4 E4,4.4,4,4.4,180,282"},
 ['7']={6,"M0,0 L6,0 L2.4,10"},
 ['8']={6,"E3,2.45,2.6,2.45,0,360 E3,7.4,3,2.6,0,360"},
 ['9']={6,"E3,3.25,3,3.25,0,360 M6,3.25 L6,5.6 E2,5.6,4,4.4,0,102"},
 ['A']={6.4f,"M0,10 L3.2,0 L6.4,10 M1.2,6.6 L5.2,6.6"},
 ['B']={6,"M0,0 L0,10 M0,0 L3.2,0 E3.2,2.4,2.4,2.4,270,450 M3.4,4.8 L0,4.8 E3.2,7.4,2.8,2.6,270,450 M3.2,10 L0,10"},
 ['C']={6.2f,"E3.4,5,3.4,5,52,308"},
 ['D']={6,"M0,0 L0,10 M0,0 L2.4,0 E2.4,5,3.6,5,270,450 M2.4,10 L0,10"},
 ['E']={5.4f,"M5.4,0 L0,0 L0,10 L5.4,10 M0,5 L4.6,5"},
 ['F']={5.4f,"M5.4,0 L0,0 L0,10 M0,5 L4.4,5"},
 ['G']={6.8f,"E3.4,5,3.4,5,0,308 M6.8,5 L3.9,5"},
 ['H']={6.2f,"M0,0 L0,10 M6.2,0 L6.2,10 M0,5 L6.2,5"},
 ['I']={0,"M0,0 L0,10"},
 ['J']={4,"M4,0 L4,7.2 E1.9,7.2,2.1,2.8,0,165"},
 ['K']={6,"M0,0 L0,10 M5.8,0 L0,6.4 M2.2,4.3 L6,10"},
 ['L']={5,"M0,0 L0,10 L5,10"},
 ['M']={8,"M0,10 L0,0 L4,7.4 L8,0 L8,10"},
 ['N']={6.2f,"M0,10 L0,0 L6.2,10 L6.2,0"},
 ['O']={7,"E3.5,5,3.5,5,0,360"},
 ['P']={6,"M0,10 L0,0 L3.2,0 E3.2,2.8,2.8,2.8,270,450 M3.2,5.6 L0,5.6"},
 ['Q']={7,"E3.5,5,3.5,5,0,360 M4.4,7.4 L7,10.6"},
 ['R']={6,"M0,10 L0,0 L3.2,0 E3.2,2.8,2.8,2.8,270,450 M3.2,5.6 L0,5.6 M3,5.6 L6,10"},
 ['S']={6,"E3,2.55,2.8,2.55,90,325 E3,7.45,3,2.55,270,505"},
 ['T']={6.4f,"M0,0 L6.4,0 M3.2,0 L3.2,10"},
 ['U']={6.2f,"M0,0 L0,6.8 E3.1,6.8,3.1,3.2,0,180 M6.2,6.8 L6.2,0"},
 ['V']={6.4f,"M0,0 L3.2,10 L6.4,0"},
 ['W']={9.2f,"M0,0 L2.3,10 L4.6,2.4 L6.9,10 L9.2,0"},
 ['X']={6.2f,"M0,0 L6.2,10 M6.2,0 L0,10"},
 ['Y']={6.2f,"M0,0 L3.1,5.2 L6.2,0 M3.1,5.2 L3.1,10"},
 ['Z']={6,"M0,0 L6,0 L0,10 L6,10"},
 [G_AE]={8.6f,"M0,10 L4.2,0 L8.6,0 M4.5,0 L4.5,10 L8.6,10 M4.5,5 L7.8,5 M1.7,6.6 L4.5,6.6"},
 [G_OE]={7,"E3.5,5,3.5,5,0,360 M0.6,9.9 L6.4,0.1"},
 [G_AA]={6.4f,"M0,10 L3.2,2.8 L6.4,10 M1.3,7.3 L5.1,7.3 E3.2,0.6,1.1,1.1,0,360"},
 ['.']={0,"M0,9.9 L0,10"},
 [',']={0.3f,"M0.3,9.8 L-0.4,11.6"},
 [':']={0,"M0,3.2 L0,3.3 M0,9.9 L0,10"},
 ['-']={4,"M0.3,5.4 L3.7,5.4"},
 ['+']={5,"M0,5.4 L5,5.4 M2.5,2.9 L2.5,7.9"},
 ['/']={3.8f,"M0,10.4 L3.8,-0.4"},
 ['%']={7.2f,"M0.6,10 L6.6,0 E1.5,2.1,1.5,2.1,0,360 E5.7,7.9,1.5,2.1,0,360"},
 ['*']={4,"M2,0.2 L2,4.2 M0.2,1.2 L3.8,3.2 M3.8,1.2 L0.2,3.2"},
 ['(']={1.7f,"E3.4,5,3.2,6.4,125,235"},
 [')']={1.7f,"E-1.7,5,3.2,6.4,-55,55"},
 ['<']={3.6f,"M3.6,1.4 L0,5 L3.6,8.6"},
 ['>']={3.6f,"M0,1.4 L3.6,5 L0,8.6"},
 ['!']={0,"M0,0 L0,6.6 M0,9.9 L0,10"},
 ['?']={5,"E2.5,2.6,2.5,2.6,180,450 M2.5,5.2 L2.5,6.6 M2.5,9.9 L2.5,10"},
 ['=']={5,"M0,3.6 L5,3.6 M0,7 L5,7"},
 ['_']={5,"M0,10 L5,10"},
 [G_DEG]={2.8f,"E1.4,1.5,1.4,1.5,0,360"},
 [G_MDOT]={0,"M0,5 L0,5.1"},
 [G_APPROX]={5,"E1.25,4,1.25,1,180,360 E3.75,4,1.25,1,0,180 E1.25,7,1.25,1,180,360 E3.75,7,1.25,1,0,180"},
 /* icons (10x10 box) */
 [G_BEAN]={10,"E5,5,3.7,4.9,0,360 E3.7,2.7,1.5,2.4,-72,72 E6.3,7.3,1.5,2.4,108,252"},
 [G_CUP]={10.2f,"M1,3 L1,5.6 E4.6,5.6,3.6,3.4,0,180 M8.2,5.6 L8.2,3 M0.2,3 L9,3 E8.6,4.9,1.5,1.5,280,440 M2.2,10 L7,10"},
 [G_DROP]={10,"M5,0.4 L2.25,4.58 M5,0.4 L7.75,4.58 E5,6.4,3.3,3.3,326.6,573.4"},
 [G_STEAM]={10,"E2,2.75,1.1,2.25,90,270 E2,7.25,1.1,2.25,270,450 E5,2.75,1.1,2.25,90,270 E5,7.25,1.1,2.25,270,450 E8,2.75,1.1,2.25,90,270 E8,7.25,1.1,2.25,270,450"},
 [G_CHECK]={10,"M0.5,5.4 L3.6,8.6 L9.5,1.6"},
 [G_CROSS]={10,"M1.2,1.2 L8.8,8.8 M8.8,1.2 L1.2,8.8"},
 [G_MOON]={8,"E5,5,4.5,4.5,60,300 E10,5,4.77,4.77,125.2,234.8"},
 [G_WARN]={10,"M5,0.8 L9.6,9.2 L0.4,9.2 L5,0.8 M5,3.8 L5,6.2 M5,7.9 L5,8"},
 [G_BOLT]={6,"M4,0 L0.4,5.6 L3.2,5.6 L2,10 L5.6,4.2 L2.9,4.2 L4,0"},
 [G_ELL]={5.6f,"M0,9.9 L0,10 M2.8,9.9 L2.8,10 M5.6,9.9 L5.6,10"},
 ['$']={6,"E3,2.55,2.8,2.55,90,325 E3,7.45,3,2.55,270,505 M3,-1.3 L3,11.3"},
 [G_CLOCK]={10,"E5,5,4.8,4.8,0,360 M5,1.9 L5,5 L7.3,6.4"},   /* v6.16 */
 [G_SECT]={5.6f,"E2.8,1.8,2.4,1.8,95,325 E2.8,5,2.7,1.6,0,360 E2.8,8.2,2.4,1.8,275,505"},
 ['\'']={0,"M0,0 L0,2.6"},
 /* lowercase letters (x-height 3.6..10, descender to 12.8) — ONLY in code spans (LC ... UC):
  * the shell is case-sensitive, so paths and commands are shown as they must be typed. */
 [G_LC+0] ={5.2f,"E2.6,6.8,2.6,3.2,0,360 M5.2,3.6 L5.2,10"},
 [G_LC+1] ={5.2f,"M0,0 L0,10 E2.6,6.8,2.6,3.2,0,360"},
 [G_LC+2] ={4.7f,"E2.6,6.8,2.6,3.2,52,308"},
 [G_LC+3] ={5.2f,"E2.6,6.8,2.6,3.2,0,360 M5.2,0 L5.2,10"},
 [G_LC+4] ={5.2f,"E2.6,6.8,2.6,3.2,48,360 M5.2,6.8 L0,6.8"},
 [G_LC+5] ={3.6f,"M1.3,10 L1.3,2.2 E3.3,2.2,2,2.2,180,285 M0,3.8 L3.3,3.8"},
 [G_LC+6] ={5.2f,"E2.6,6.8,2.6,3.2,0,360 M5.2,3.6 L5.2,10.4 E2.6,10.4,2.6,2.4,0,150"},
 [G_LC+7] ={5,"M0,0 L0,10 E2.5,6.1,2.5,2.5,180,360 M5,6.1 L5,10"},
 [G_LC+8] ={0,"M0,3.6 L0,10 M0,0.7 L0,0.8"},
 [G_LC+9] ={1.6f,"M1.6,3.6 L1.6,10.8 E-0.2,10.8,1.8,2,0,120 M1.6,0.7 L1.6,0.8"},
 [G_LC+10]={4.6f,"M0,0 L0,10 M4.3,3.6 L0,7.7 M1.6,6.2 L4.6,10"},
 [G_LC+11]={0,"M0,0 L0,10"},
 [G_LC+12]={7.6f,"M0,3.6 L0,10 E1.9,5.6,1.9,2,180,360 M3.8,5.6 L3.8,10 E5.7,5.6,1.9,2,180,360 M7.6,5.6 L7.6,10"},
 [G_LC+13]={5,"M0,3.6 L0,10 E2.5,6.1,2.5,2.5,180,360 M5,6.1 L5,10"},
 [G_LC+14]={5.2f,"E2.6,6.8,2.6,3.2,0,360"},
 [G_LC+15]={5.2f,"M0,3.6 L0,12.8 E2.6,6.8,2.6,3.2,0,360"},
 [G_LC+16]={5.2f,"E2.6,6.8,2.6,3.2,0,360 M5.2,3.6 L5.2,12.8"},
 [G_LC+17]={3.4f,"M0,3.6 L0,10 E2.4,6,2.4,2.4,180,300"},
 [G_LC+18]={4.5f,"E2.25,5.23,2.1,1.63,90,325 E2.25,8.37,2.25,1.63,270,505"},
 [G_LC+19]={3.5f,"M1.3,1.2 L1.3,8 E3.3,8,2,2,90,180 M0,3.8 L3.4,3.8"},
 [G_LC+20]={5,"M0,3.6 L0,7.5 E2.5,7.5,2.5,2.5,0,180 M5,3.6 L5,10"},
 [G_LC+21]={5,"M0,3.6 L2.5,10 L5,3.6"},
 [G_LC+22]={7.6f,"M0,3.6 L1.9,10 L3.8,5 L5.7,10 L7.6,3.6"},
 [G_LC+23]={4.8f,"M0,3.6 L4.8,10 M4.8,3.6 L0,10"},
 [G_LC+24]={5,"M0,3.6 L2.5,10 M5,3.6 L1.4,12.8"},
 [G_LC+25]={4.6f,"M0,3.6 L4.6,3.6 L0,10 L4.6,10"},
};
typedef struct { float cap, stroke, gap, track; } FStyle;
enum { F_XS=0, F_S, F_M, F_MB, F_L, F_XL, F_XXL, F_IC, F_ICL, F_H, NFONT };
static const FStyle FS[NFONT]={
 [F_XS] ={11, 1.55f,0.20f,1.3f},
 [F_S]  ={14, 1.95f,0.19f,1.0f},
 [F_M]  ={18, 2.4f, 0.18f,0.6f},
 [F_MB] ={18, 3.0f, 0.18f,0.6f},
 [F_L]  ={26, 3.4f, 0.16f,0.5f},
 [F_XL] ={44, 5.2f, 0.13f,0},
 [F_XXL]={66, 7.2f, 0.11f,0},
 [F_IC] ={20, 2.1f, 0.10f,0},
 [F_ICL]={30, 2.8f, 0.10f,0},
 [F_H]  ={34, 4.3f, 0.14f,0.2f},     /* v6.2: word hero between −/+ (MERAKIPLUS / ORIGINAL) */
};
#define GSEG_MAX 128
static float *gseg[NG]; static int gnseg[NG]; static char gparsed[NG];
typedef struct { uint8_t*a; short w,h,ox,oy; float adv; char st; } GBmp;   /* st: 0=not built, 1=ok, 2=empty */
static GBmp gcache[NFONT][NG];

static void glyph_parse(int g){
    gparsed[g]=1; const char*p=GD[g].path; if(!p||!*p){ gnseg[g]=0; return; }
    float buf[4*GSEG_MAX]; int n=0; float px=0,py=0;
    while(*p){
        while(*p==' ')p++;
        char cmd=*p; if(!cmd) break; p++;
        float v[6]; int nv=(cmd=='E')?6:2;
        for(int k=0;k<nv;k++){ char*e; v[k]=strtof(p,&e); p=e; if(*p==',')p++; }
        if(cmd=='M'){ px=v[0]; py=v[1]; }
        else if(cmd=='L'){ if(n<GSEG_MAX){ buf[4*n]=px; buf[4*n+1]=py; buf[4*n+2]=v[0]; buf[4*n+3]=v[1]; n++; } px=v[0]; py=v[1]; }
        else if(cmd=='E'){
            int ns=(int)ceilf(fabsf(v[5]-v[4])/12.f); if(ns<3)ns=3;
            float ax=v[0]+v[2]*cosf(v[4]*(float)M_PI/180), ay=v[1]+v[3]*sinf(v[4]*(float)M_PI/180);
            for(int k=1;k<=ns;k++){
                float ang=(v[4]+(v[5]-v[4])*k/ns)*(float)M_PI/180;
                float bx=v[0]+v[2]*cosf(ang), by=v[1]+v[3]*sinf(ang);
                if(n<GSEG_MAX){ buf[4*n]=ax; buf[4*n+1]=ay; buf[4*n+2]=bx; buf[4*n+3]=by; n++; }
                ax=bx; ay=by;
            }
        }
    }
    gnseg[g]=n; gseg[g]=malloc(sizeof(float)*4*n); memcpy(gseg[g],buf,sizeof(float)*4*n);
}
static GBmp* glyph_get(int f,int g){
    GBmp*b=&gcache[f][g]; if(b->st) return b;
    if(!gparsed[g]) glyph_parse(g);
    const FStyle*fs=&FS[f]; float hw=fs->stroke/2, s=(fs->cap-fs->stroke)/10.f;
    b->adv=GD[g].adv*s+fs->stroke+fs->cap*fs->gap+fs->track;
    int n=gnseg[g]; if(n==0){ b->st=2; if(g==' ') b->adv=GD[g].adv*s+fs->track; return b; }
    float sg[4*GSEG_MAX]; float mnx=1e9f,mny=1e9f,mxx=-1e9f,mxy=-1e9f;
    for(int k=0;k<4*n;k++){ sg[k]=hw+gseg[g][k]*s; if(k&1){ if(sg[k]<mny)mny=sg[k]; if(sg[k]>mxy)mxy=sg[k]; } else { if(sg[k]<mnx)mnx=sg[k]; if(sg[k]>mxx)mxx=sg[k]; } }
    int x0=(int)floorf(mnx-hw-1), x1=(int)ceilf(mxx+hw+1), y0=(int)floorf(mny-hw-1), y1=(int)ceilf(mxy+hw+1);
    b->w=(short)(x1-x0); b->h=(short)(y1-y0); b->ox=(short)x0; b->oy=(short)y0;
    b->a=calloc(1,(size_t)b->w*b->h);
    float rj=hw+1.f;
    for(int k=0;k<n;k++){
        float ax=sg[4*k],ay=sg[4*k+1],bx=sg[4*k+2],by=sg[4*k+3];
        int ia=(int)floorf(fminf(ax,bx)-rj)-x0, ib=(int)ceilf(fmaxf(ax,bx)+rj)-x0;
        int ja=(int)floorf(fminf(ay,by)-rj)-y0, jb=(int)ceilf(fmaxf(ay,by)+rj)-y0;
        if(ia<0)ia=0; if(ja<0)ja=0; if(ib>b->w)ib=b->w; if(jb>b->h)jb=b->h;
        for(int j=ja;j<jb;j++) for(int i=ia;i<ib;i++){
            float cov=hw+0.5f-seg_dist(x0+i+0.5f,y0+j+0.5f,ax,ay,bx,by);
            if(cov<=0) continue; if(cov>1)cov=1;
            int v=(int)(cov*255); uint8_t*q=&b->a[j*b->w+i]; if(v>*q)*q=(uint8_t)v;
        }
    }
    b->st=1; return b;
}
/* UTF-8 -> glyph id (uppercasing; ÆØÅ, °, ·, ≈; \x01.. = icons).
 * LC/UC (\x0e/\x0f) open/close a code span in which a..z are NOT uppercased; they take up
 * no space themselves (return -1). The state is reset by text_wf()/text_at() for each string. */
static int g_lc=0;
static int glyph_next(const unsigned char**pp){
    const unsigned char*p=*pp; int c=*p++;
    if(c<0x80){
        *pp=p;
        if(c==0x0E){ g_lc=1; return -1; }
        if(c==0x0F){ g_lc=0; return -1; }
        if(c>='a'&&c<='z'){ if(g_lc) return G_LC+(c-'a'); c-=32; }
        if(c>=1&&c<=9){ static const int ic[10]={0,G_BEAN,G_CUP,G_DROP,G_STEAM,G_CHECK,G_CROSS,G_MOON,G_WARN,G_BOLT}; return ic[c]; }
        if(c==0x0B) return G_CLOCK;                       /* v6.16 */
        if(c<32||c>95) return '?';
        return c;
    }
    int g='?';
    /* v6.20 (review 2026-11): only CONTINUATION bytes are consumed — a lead byte cut off by a truncation ("A\xC3", "A\xE2\x80")
     * used to swallow the terminating NUL, and text_wf/text_at then read past the end of the string */
#define CONT() (((*p)&0xC0)==0x80?*p++:0)
    if(c==0xC3){ int d=CONT(); if(d==0x86||d==0xA6) g=G_AE; else if(d==0x98||d==0xB8) g=G_OE; else if(d==0x85||d==0xA5) g=G_AA; else if(d==0x97) g='X'; }
    else if(c==0xC2){ int d=CONT(); if(d==0xB0) g=G_DEG; else if(d==0xB7) g=G_MDOT; else if(d==0xA7) g=G_SECT; }
    else if(c==0xE2){ int d=CONT(); int e=d?CONT():0; if(d==0x89&&e==0x88) g=G_APPROX; else if(d==0x80&&(e==0x93||e==0x94)) g='-'; else if(d==0x80&&e==0xA6) g=G_ELL; else if(d==0x86&&e==0x92) g='>'; }
    else { while((*p&0xC0)==0x80) p++; }
#undef CONT
    *pp=p; return g;
}
static float text_wf(const char*s,int f){
    const unsigned char*p=(const unsigned char*)s; float w=0,last=0; g_lc=0;
    while(*p){ int g=glyph_next(&p); if(g<0) continue; GBmp*b=glyph_get(f,g); w+=b->adv; last=FS[f].cap*FS[f].gap+FS[f].track; }
    return w>0? w-last : 0;
}
static int text_w(const char*s,int f){ return (int)ceilf(text_wf(s,f)); }
static void glyph_blit(const GBmp*b,int gx,int gy,uint32_t c,int a){
    for(int j=0;j<b->h;j++){
        int y=gy+j; if(y<clipr.y0||y>=clipr.y1) continue;
        int xa=gx, xb=gx+b->w;
        if(xa<clipr.x0)xa=clipr.x0; if(xb>clipr.x1)xb=clipr.x1;
        if(xa<cx0[y])xa=cx0[y]; if(xb>cx1[y]+1)xb=cx1[y]+1;
        const uint8_t*src=b->a+j*b->w-gx; uint32_t*dst=cv+y*W;
        for(int x=xa;x<xb;x++){ int v=src[x]; if(!v) continue; v=(v*a+127)/255; dst[x]= v>=255? c : mix(dst[x],c,v); }
    }
}
/* text: (x,y) = left edge / cap top */
static float text_at(float x,int y,const char*s,int f,uint32_t c,int a){
    const unsigned char*p=(const unsigned char*)s; g_lc=0;
    while(*p){ int g=glyph_next(&p); if(g<0) continue; GBmp*b=glyph_get(f,g); if(b->st==1) glyph_blit(b,(int)lroundf(x)+b->ox,y+b->oy,c,a); x+=b->adv; }
    return x;
}
static void text_c(int cx,int y,const char*s,int f,uint32_t c,int a){ text_at(cx-text_wf(s,f)/2,y,s,f,c,a); }
static void text_r(int xr,int y,const char*s,int f,uint32_t c,int a){ text_at(xr-text_wf(s,f),y,s,f,c,a); }
/* truncate s (buffer w. >= 4 bytes of headroom) with "…" until it is <= maxw px wide; cuts at UTF-8 boundaries */
static void fit_text(char*s,int f,int maxw){
    if(text_w(s,f)<=maxw) return;
    size_t n=strlen(s);
    while(n>0){ do{ n--; }while(n>0&&((unsigned char)s[n]&0xC0)==0x80); memcpy(s+n,"\xE2\x80\xA6",4); if(text_w(s,f)<=maxw) return; }
}
static void font_prewarm(void){
    static const char*const dig="0123456789.-";
    for(const char*p=dig;*p;p++){ glyph_get(F_XXL,*p); glyph_get(F_XL,*p); glyph_get(F_L,*p); glyph_get(F_M,*p); glyph_get(F_MB,*p); }
}

/* ================= dirty rect lists ================= */
static int r_empty(Rect r){ return r.x1<=r.x0||r.y1<=r.y0; }
static int r_touch(Rect a,Rect b){ return a.x0<=b.x1&&b.x0<=a.x1&&a.y0<=b.y1&&b.y0<=a.y1; }
static int r_isect(Rect a,Rect b){ return a.x0<b.x1&&b.x0<a.x1&&a.y0<b.y1&&b.y0<a.y1; }
static Rect r_union(Rect a,Rect b){ Rect r={a.x0<b.x0?a.x0:b.x0,a.y0<b.y0?a.y0:b.y0,a.x1>b.x1?a.x1:b.x1,a.y1>b.y1?a.y1:b.y1}; return r; }
static void rl_add(RL*l,Rect r){
    if(r.x0<0)r.x0=0; if(r.y0<0)r.y0=0; if(r.x1>W)r.x1=W; if(r.y1>H)r.y1=H;
    if(r_empty(r)) return;
    for(;;){
        int merged=0;
        for(int i=0;i<l->n;i++) if(r_touch(l->r[i],r)){ r=r_union(l->r[i],r); l->r[i]=l->r[--l->n]; merged=1; break; }
        if(!merged) break;
    }
    if(l->n<RL_MAX) l->r[l->n++]=r;
    else l->r[0]=r_union(l->r[0],r);
}
static int rl_hits(const RL*l,Rect r){ for(int i=0;i<l->n;i++) if(r_isect(l->r[i],r)) return 1; return 0; }
static void blit_rect(uint32_t*dst,const uint32_t*src,Rect r){
    if(r.x0<0)r.x0=0; if(r.y0<0)r.y0=0; if(r.x1>W)r.x1=W; if(r.y1>H)r.y1=H;
    if(r_empty(r)) return;
    for(int y=r.y0;y<r.y1;y++) memcpy(dst+y*W+r.x0,src+y*W+r.x0,(size_t)(r.x1-r.x0)*4);
}

/* ================= GAUGE ARC w. LUT (track + glow + ticks) =================
 * The geometry is precomputed per pixel once (angle fraction t, coverage, glow).
 * Per frame, ONLY pixels in the changed angle interval are recomputed, directly in the
 * semi-static layer 'slay' — the changed bbox is reported as damage. */
typedef struct { uint32_t off; float t; short x,y; uint8_t cov,glow,kind; } AEnt;
typedef struct {
    float rm,hw,gw,a0,sweep; int nt; AEnt*e; int n;
    float f_d,f_t,f_drawn;                 /* shown / target / last drawn fill (0..1) */
    uint32_t c_lo,c_hi,d_lo,d_hi;          /* fill colour (target) / last drawn */
    float mark,mark_drawn;                 /* target marker (0..1; <0 = none) */
    int vis,vis_drawn; uint32_t track; Rect mrect;
} Arc;
static Arc garc, parc;                      /* outer gauge + thin progress ring */
#define COL_TICK_MIN rgb(50,55,66)
#define COL_TICK_MAJ rgb(98,106,122)
static void arc_build(Arc*a,float rm,float hw,float gw,float a0,float sweep,int nticks,float tr0,float tr1){
    a->rm=rm; a->hw=hw; a->gw=gw; a->a0=a0; a->sweep=sweep; a->nt=nticks;
    float ro=fmaxf(rm+hw+gw,tr1)+2, ri=rm-hw-gw-2;
    float sx=CX+rm*cosf(a0*(float)M_PI/180), sy=CY+rm*sinf(a0*(float)M_PI/180);
    float ex=CX+rm*cosf((a0+sweep)*(float)M_PI/180), ey=CY+rm*sinf((a0+sweep)*(float)M_PI/180);
    int cap=0; a->n=0; a->e=NULL;
    for(int pass=0;pass<2;pass++){
        int n=0;
        for(int y=0;y<H;y++) for(int x=cx0[y];x<=cx1[y];x++){
            float dx=x+0.5f-CX, dy=y+0.5f-CY; float rho=sqrtf(dx*dx+dy*dy);
            if(rho>ro||rho<ri) continue;
            float ang=atan2f(dy,dx)*180/(float)M_PI; float rel=fmodf(ang-a0+720,360);
            float t,d; int in=rel<=sweep;
            if(in){ t=rel/sweep; d=fabsf(rho-rm); }
            else if(rel>sweep+(360-sweep)/2){ t=-1; d=hypotf(x+0.5f-sx,y+0.5f-sy); }
            else { t=2; d=hypotf(x+0.5f-ex,y+0.5f-ey); }
            float cov=clampf(hw+0.5f-d,0,1), gl=0;
            if(gw>0&&d>hw){ float g=1-(d-hw)/gw; if(g>0) gl=g*g; }
            if(cov>0||gl>0.01f){
                if(pass){ AEnt*e=&a->e[n]; e->off=(uint32_t)(y*W+x); e->t=t; e->x=(short)x; e->y=(short)y; e->cov=(uint8_t)(cov*255); e->glow=(uint8_t)(gl*255); e->kind=0; }
                n++; continue;
            }
            if(nticks>0&&in&&rho>=tr0-1&&rho<=tr1+1){
                float step=sweep/nticks; int k=(int)lroundf(rel/step); int major=(k%5)==0;
                float r0=major?tr0:tr0+2, r1=major?tr1:tr1-2;
                float dpx=fabsf(rel-k*step)*(float)M_PI/180*rho;
                float c=clampf((major?1.0f:0.75f)+0.5f-dpx,0,1)*clampf(fminf(rho-r0+0.5f,r1-rho+0.5f),0,1);
                if(c>0.02f){
                    if(pass){ AEnt*e=&a->e[n]; e->off=(uint32_t)(y*W+x); e->t=k*step/sweep; e->x=(short)x; e->y=(short)y; e->cov=(uint8_t)(c*255); e->glow=0; e->kind=major?2:1; }
                    n++;
                }
            }
        }
        if(!pass){ cap=n; a->e=malloc(sizeof(AEnt)*(cap>0?cap:1)); } else a->n=n;
    }
    a->f_d=a->f_t=0; a->f_drawn=-1; a->mark=a->mark_drawn=-1; a->vis=0; a->vis_drawn=-1; a->track=rgb(34,38,47);
    a->c_lo=a->c_hi=a->d_lo=a->d_hi=rgb(255,255,255);
}
static void arc_pt(const Arc*a,float f,float r,float*x,float*y){
    float ang=(a->a0+a->sweep*f)*(float)M_PI/180; *x=CX+r*cosf(ang); *y=CY+r*sinf(ang);
}
/* draw the changed interval into slay; return the damage bbox in *out (empty if nothing) */
static int arc_paint(Arc*a,int force,Rect*out){
    int all=force||a->vis!=a->vis_drawn||a->c_lo!=a->d_lo||a->c_hi!=a->d_hi||a->mark!=a->mark_drawn||a->f_drawn<0;
    if(!all&&a->f_d==a->f_drawn) return 0;
    float capf=(a->hw+a->gw+2)/(a->rm*a->sweep*(float)M_PI/180);
    float lo=fminf(a->f_d,a->f_drawn)-capf, hi=fmaxf(a->f_d,a->f_drawn)+capf;
    float ex,ey; arc_pt(a,a->f_d,a->rm,&ex,&ey);
    int lit=a->vis&&a->f_d>0.002f;
    Rect bb={W,H,0,0};
    if(all&&!r_empty(a->mrect)){ blit_rect(slay,bgbase,a->mrect); bb=a->mrect; a->mrect=(Rect){0,0,0,0}; }
    for(int i=0;i<a->n;i++){
        const AEnt*e=&a->e[i]; float t=e->t;
        if(!all){ if(t<0){ if(lo>0) continue; } else if(t>1.5f){ if(hi<1) continue; } else if(t<lo||t>hi) continue; }
        uint32_t p=bgbase[e->off];
        if(a->vis){
            if(e->kind==0){
                if(e->cov) p=mix(p,a->track,e->cov);
                if(lit){
                    int lc=0,lg=0; float tt=t;
                    if(t<0){ lc=e->cov; lg=e->glow; tt=0; }
                    else if(t<=a->f_d){ lc=e->cov; lg=e->glow; }
                    else if(t<=a->f_d+capf||t>1.5f){
                        float d=hypotf(e->x+0.5f-ex,e->y+0.5f-ey);
                        float c=clampf(a->hw+0.5f-d,0,1); lc=(int)(c*255); if(lc>e->cov&&t<=1.5f&&e->cov) lc=e->cov;
                        if(a->gw>0&&d>a->hw){ float g=1-(d-a->hw)/a->gw; if(g>0) lg=(int)(g*g*255); }
                        tt=a->f_d;
                    }
                    if(lc||lg){
                        float u=tt/a->f_d; uint32_t col=lerpc(a->c_lo,a->c_hi,0.18f+0.82f*u*u);
                        if(lg) p=mix(p,col,lg*70/255);
                        if(lc) p=mix(p,col,lc);
                    }
                }
            } else {
                int on=lit&&t<=a->f_d+0.001f;
                p=mix(p,on?a->c_hi:(e->kind==2?COL_TICK_MAJ:COL_TICK_MIN),on?e->cov:e->cov);
            }
        }
        slay[e->off]=p;
        if(e->x<bb.x0)bb.x0=e->x; if(e->x+1>bb.x1)bb.x1=e->x+1; if(e->y<bb.y0)bb.y0=e->y; if(e->y+1>bb.y1)bb.y1=e->y+1;
    }
    /* head highlight + target marker are drawn on top (in slay) */
    uint32_t*save=cv; Rect sc=clipr; cv=slay; set_clip(FULL);
    if(lit&&a->hw>=4){ disc(ex,ey,a->hw-2.2f,rgb(255,255,255),215); }
    if(a->vis&&a->mark>=0){
        float x0,y0,x1,y1; arc_pt(a,a->mark,a->rm-a->hw-5,&x0,&y0); arc_pt(a,a->mark,a->rm+a->hw+5,&x1,&y1);
        capsule(x0,y0,x1,y1,1.6f,rgb(250,251,253),255);
        Rect mr={(short)(fminf(x0,x1)-4),(short)(fminf(y0,y1)-4),(short)(fmaxf(x0,x1)+5),(short)(fmaxf(y0,y1)+5)}; bb=r_empty(bb)?mr:r_union(bb,mr); a->mrect=mr;
    }
    cv=save; clipr=sc;
    a->f_drawn=a->f_d; a->d_lo=a->c_lo; a->d_hi=a->c_hi; a->mark_drawn=a->mark; a->vis_drawn=a->vis;
    if(r_empty(bb)) return 0;
    *out=bb; return 1;
}
/* eased fill: exponential approach (tau ~110 ms) */
static int arc_ease(Arc*a,float dt_ms){
    float d=a->f_t-a->f_d; if(d==0) return 0;
    if(fabsf(d)<0.0012f){ a->f_d=a->f_t; return 1; }
    a->f_d+=d*(1-expf(-dt_ms/110.f)); return 1;
}

/* ================= compositor: slay layer + widgets + overlays + present ================= */
#define NWID 96
static RL dmg, pres, pres_prev;
static uint32_t wsig[NWID];
static int force_all=1, static_dirty=1;      /* static_dirty is forward-declared at lang_set() */
static uint32_t hs(uint32_t h,const char*s){ while(*s){ h^=(unsigned char)*s++; h*=16777619u; } return h; }
static uint32_t hi(uint32_t h,int v){ for(int k=0;k<4;k++){ h^=(v>>(8*k))&0xFF; h*=16777619u; } return h; }
#define H0 2166136261u
/* widget: drawn only if the signature has changed or its rect is hit by damage */
static int W_begin(int id,Rect r,uint32_t sig){
    if(sig==0) sig=1;
    if(!force_all&&wsig[id]==sig&&!rl_hits(&dmg,r)) return 0;
    wsig[id]=sig; blit_rect(canvas,slay,r); rl_add(&pres,r); cv=canvas; set_clip(r); return 1;
}
static void W_end(void){ set_clip(FULL); }

/* ---- framebuffer (device) ---- */
static int ffd=-1; static uint32_t *fbmem=NULL; static long fblen=0; static int line_px=W; static int curpage=0;
#ifdef SIM_RT
/* SIM: fb = one 480x480 BGRA buffer in memory (same pixel format as the device; no page flip).
 * fb_present() copies ONLY the rects the compositor reports as changed — exactly like the device — so a
 * bug in the dirty-rect logic would also be visible in the SIM images. sim_frame() writes the buffer to a file. */
static long sim_fb_seq=0;
static void fb_open(void){
    (void)line_px; (void)curpage;
    fblen=(long)W*H*4;
    fbmem=mmap(NULL,fblen,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(fbmem==MAP_FAILED){ perror("mmap sim-fb"); exit(4); }
    for(long i=0;i<(long)W*H;i++) fbmem[i]=0xFF000000u;
}
#endif
#ifdef DEVICE_RT
static struct fb_var_screeninfo vi;
static void fb_open(void){
    ffd=open(FB,O_RDWR);
    if(ffd<0){ perror("open fb0"); exit(1); }
    struct fb_fix_screeninfo fi;
    ioctl(ffd,FBIOGET_VSCREENINFO,&vi);
    ioctl(ffd,FBIOGET_FSCREENINFO,&fi);
    line_px = fi.line_length/4;
    fblen = (long)vi.yres_virtual*fi.line_length;
    fbmem=mmap(NULL,fblen,PROT_READ|PROT_WRITE,MAP_SHARED,ffd,0);
    if(fbmem==MAP_FAILED){ perror("mmap fb"); exit(1); }
    curpage = (vi.yoffset/H)&1;
    /* clear BOTH fb pages completely (also outside the circle) to pure black */
    int pages=(int)(vi.yres_virtual/H); if(pages>2)pages=2;
    for(int pg=0;pg<pages;pg++){ uint32_t*p=fbmem+(long)pg*H*line_px; for(long i=0;i<(long)H*line_px;i++) p[i]=0xFF000000u; }
}
#endif
/* present: the back page lacks changes from the previous AND this frame -> copy the union, flip */
static void fb_present(const uint32_t*src,const RL*now){
#ifdef DEVICE_RT
    if(now->n==0&&pres_prev.n==0) return;
    uint32_t*dst=fbmem+(long)curpage*H*line_px;
    for(int l=0;l<2;l++){
        const RL*rl=l?&pres_prev:now;
        for(int i=0;i<rl->n;i++){ Rect r=rl->r[i]; for(int y=r.y0;y<r.y1;y++) memcpy(dst+(long)y*line_px+r.x0,src+y*W+r.x0,(size_t)(r.x1-r.x0)*4); }
    }
    struct fb_var_screeninfo v=vi; v.yoffset=curpage*H; v.xoffset=0;
    ioctl(ffd,FBIOPAN_DISPLAY,&v);
    curpage^=1;
#elif defined(SIM_RT)
    if(fbmem&&now->n){
        for(int i=0;i<now->n;i++){ Rect r=now->r[i]; for(int y=r.y0;y<r.y1;y++) memcpy(fbmem+(long)y*W+r.x0,src+y*W+r.x0,(size_t)(r.x1-r.x0)*4); }
        sim_fb_seq++;
    }
#else
    (void)src;
#endif
    pres_prev=*now;
}

/* ---- transitions: slide (page change) + crossfade (detail/splash) ---- */
enum { TR_NONE=0, TR_SLIDE, TR_FADE };
static struct { int type; ms_t t0; int dur; int dir; } tr;
static float ease_out(float t){ float u=1-t; return 1-u*u*u; }
static float ease_io(float t){ return t<0.5f? 4*t*t*t : 1-powf(-2*t+2,3)/2; }
static int trans_frame(ms_t now){
    float t=(float)(now-tr.t0)/tr.dur;
    if(t>=1){ tr.type=TR_NONE; return 0; }
    if(t<0) t=0;                                                        /* v6.20 (review 2026-11): b[sxp-W] is only in range for 0 <= off <= W */
    if(tr.type==TR_SLIDE){
        int off=(int)(ease_io(t)*W);
        for(int y=0;y<H;y++){
            uint32_t*d=tbuf+y*W; const uint32_t*a=snapA+y*W,*b=snapB+y*W;
            for(int x=cx0[y];x<=cx1[y];x++){
                int sxp=tr.dir>0? x+off : x-off;
                d[x]= (sxp>=0&&sxp<W)? a[sxp] : (tr.dir>0? b[sxp-W] : b[sxp+W]);
            }
        }
    } else {
        int al=(int)(ease_out(t)*255);
        for(int y=0;y<H;y++){
            uint32_t*d=tbuf+y*W; const uint32_t*a=snapA+y*W,*b=snapB+y*W;
            for(int x=cx0[y];x<=cx1[y];x++) d[x]=mix(a[x],b[x],al);
        }
    }
    RL full; full.n=1; full.r[0]=FULL; fb_present(tbuf,&full);
    return 1;
}
static void gfx_init(void){
    size_t n=(size_t)W*H*4;
    canvas=calloc(1,n); bgpure=calloc(1,n); bgbase=calloc(1,n); slay=calloc(1,n); snapA=calloc(1,n); snapB=calloc(1,n); tbuf=calloc(1,n);
    for(size_t i=0;i<(size_t)W*H;i++){ canvas[i]=0xFF000000u; tbuf[i]=0xFF000000u; }
    cv=canvas; clip_init(); bg_build();
    arc_build(&garc,214,7,5,135,270,50,225.5f,232.5f);
    arc_build(&parc,198,1.5f,0,135,270,0,0,0);
    font_prewarm();
}

