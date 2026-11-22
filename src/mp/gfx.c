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
/* v7.0: the OUTLINE of a rounded rectangle (stroke centred on its edge, width 2·hw), AA via the signed distance */
static void rrect_line(float x,float y,float w,float h,float r,float hw,uint32_t c,int a){
    if(r>h/2)r=h/2; if(r>w/2)r=w/2;
    float cx=x+w/2, cy=y+h/2, bx=w/2-r, by=h/2-r;
    int y0=(int)floorf(y-hw-1), y1=(int)ceilf(y+h+hw+1), x0=(int)floorf(x-hw-1), x1=(int)ceilf(x+w+hw+1);
    if(y0<clipr.y0)y0=clipr.y0; if(y1>clipr.y1)y1=clipr.y1; if(x0<clipr.x0)x0=clipr.x0; if(x1>clipr.x1)x1=clipr.x1;
    for(int j=y0;j<y1;j++){
        float qy=fabsf(j+0.5f-cy)-by;
        for(int i=x0;i<x1;i++){
            float qx=fabsf(i+0.5f-cx)-bx;
            float ox=qx>0?qx:0, oy=qy>0?qy:0, d=sqrtf(ox*ox+oy*oy)+fminf(fmaxf(qx,qy),0)-r;
            float cov=hw+0.5f-fabsf(d); if(cov<=0) continue; if(cov>1) cov=1;
            if(!(qx<-r-hw-1&&qy<-hw-1)) pset(i,j,c,(int)(a*cov));
        }
        if(qy<-hw-1){ /* the straight middle part: only the two vertical edges are near */ }
    }
}
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

/* ================= background (v7.0): the OEM copper with a fine guilloche, pre-rendered once =================
 * A warm burnt-orange disc, a little lighter towards the upper left and darker towards the rim, with a very fine,
 * low-contrast line texture: rings of a rounded square (|x|^p + |y|^p)^(1/p) blended with a circle, so the lines form soft
 * chevrons on the diagonals like the stock screen's. Built once at start (two layers: copper, and "dark" for errors and
 * the power-off page) with an ordered dither, so nothing has to be computed per frame. Pure black outside R=234. */
static uint32_t *bgdark;
#define BG_C0R 182
#define BG_C0G 90
#define BG_C0B 40
static void bg_build(void){
    static const uint8_t B4[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        uint32_t*p=&bgpure[y*W+x], *q=&bgdark[y*W+x];
        if(x<cx0[y]||x>cx1[y]){ *p=*q=0xFF000000u; continue; }
        float dx=x+0.5f-CX, dy=y+0.5f-CY; float rr=sqrtf(dx*dx+dy*dy), d=rr/RSAFE;
        float t=powf(d,2.2f);
        float lift=clampf(0.55f-(dx+dy)/700.f,0,1);                       /* soft light from the upper left */
        float r=BG_C0R-50*t+10*lift*(1-t), g=BG_C0G-30*t+6*lift*(1-t), b=BG_C0B-16*t+3*lift*(1-t);
        /* guilloche: distance in a blend of a rounded square and a circle, lines every 4.6 px, AA'd, fading out at the centre */
        float ax=fabsf(dx), ay=fabsf(dy), sq=powf(powf(ax,3.2f)+powf(ay,3.2f),1/3.2f), dd=0.62f*sq+0.38f*rr;
        float ph=dd/4.6f; ph-=floorf(ph); float ln=1-fabsf(ph-0.5f)*2; ln=clampf((ln-0.62f)/0.38f,0,1);
        float amp=(0.035f+0.05f*clampf((d-0.12f)/0.5f,0,1));
        r+=ln*amp*255*0.42f; g+=ln*amp*255*0.30f; b+=ln*amp*255*0.20f;
        float th=(B4[(y&3)*4+(x&3)]+0.5f)/16.f-0.5f;
        int ri=(int)floorf(r+th+0.5f), gi=(int)floorf(g+th+0.5f), bi=(int)floorf(b+th+0.5f);
        *p=rgb(clampi(ri,0,255),clampi(gi,0,255),clampi(bi,0,255));
        /* dark: espresso brown-black, the same texture at half strength */
        float r2=34-22*t+ln*amp*60, g2=22-15*t+ln*amp*36, b2=19-13*t+ln*amp*26;
        ri=(int)floorf(r2+th+0.5f); gi=(int)floorf(g2+th+0.5f); bi=(int)floorf(b2+th+0.5f);
        *q=rgb(clampi(ri,0,255),clampi(gi,0,255),clampi(bi,0,255));
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
 [G_BEAN]={10.4f,"R3.00,4.10,2.3,3.45,-30,0,360 M1.63,1.72 L2.18,2.03 L2.63,2.41 L2.90,2.89 L3.00,3.46 L3.00,4.10 L3.00,4.74 L3.10,5.31 L3.37,5.79 L3.82,6.17 L4.38,6.48 R7.10,6.00,2.3,3.45,28,0,360 M8.39,3.57 L8.42,4.21 L8.34,4.79 L8.08,5.27 L7.64,5.67 L7.10,6.00 L6.56,6.33 L6.12,6.73 L5.86,7.21 L5.78,7.79 L5.81,8.43"},
 [G_CUP]={10.4f,"M1.4,2.6 L7.4,2.6 L7.4,6.4 E5.8,6.4,1.6,1.6,0,90 L3.0,8.0 E3.0,6.4,1.6,1.6,90,180 L1.4,2.6 E7.4,4.5,1.55,1.35,-90,90 M0,9.6 L9.6,9.6"},
 [G_DROP]={10,"M5,0.4 L2.2,4.9 M5,0.4 L7.8,4.9 E5,6.5,3.3,3.3,330,570 M3.2,6.9 E3.95,6.9,0.75,0.6,180,360 E5.45,6.9,0.75,0.6,0,180 E6.2,6.9,0,0,0,0"},
 [G_STEAM]={10.6f,"E3.0,4.6,2.1,2.1,100,270 E5.6,3.2,2.7,2.7,195,345 E8.0,5.0,1.9,1.9,255,440 M2.6,6.7 L3.6,6.7 M7.0,6.9 L7.9,6.9 M5.3,3.9 L5.3,9.6 M3.8,8.1 L5.3,9.6 L6.8,8.1"},
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
/* ================= TYPE: Montserrat outlines (src/mp/font_data.c) + stroke icons =================
 * v7.0: the UI's text is real typography — Montserrat (SIL OFL 1.1) in four weights, baked into font_data.c by
 * tools/gen_font.py as TrueType outlines + advances + GPOS pair kerning. A glyph is rasterized the first time a style
 * needs it (exact-area coverage, the signed-area accumulation method: every outline edge adds its covered area to the
 * cells it crosses, a prefix sum per row gives the coverage) and cached as 8-bit alpha, in 4 sub-pixel phases so text
 * keeps its true spacing at small sizes. Labels set along a ring are rasterized rotated (text_arc). Digits can be tabular.
 * Text is drawn exactly as written (no uppercasing). The control bytes \x01..\x0b are line icons from the stroke engine
 * above (GD[]), sized to the style's cap height. A style: face (weight), em size, tracking, tabular digits. */
enum { FACE_LIGHT=0, FACE_MED, FACE_SEMI, FACE_BOLD };
typedef struct { signed char face; float em, track; char tnum; float cap, stroke; } FStyle;   /* cap = em·capHeight/UPM (set at init) */
enum { F_XS=0, F_S, F_M, F_MB, F_L, F_XL, F_XXL, F_IC, F_ICL, F_H, F_CLK, F_LAB, F_NUM, F_TINY, F_ROLL, F_ROLL2, F_STAT, NFONT };
static FStyle FS[NFONT]={
 [F_TINY]={FACE_SEMI, 13.0f,0.3f,1,0,1.4f},    /* micro captions (DIAGNOSTICS cell labels) */
 [F_XS] ={FACE_MED,  15.5f,0.1f,1,0,1.6f},     /* captions, toasts, hints */
 [F_S]  ={FACE_MED,  18.0f,0,   1,0,1.9f},     /* body text, live lines */
 [F_M]  ={FACE_SEMI, 21.0f,0,   1,0,2.2f},     /* row labels, buttons */
 [F_MB] ={FACE_BOLD, 21.0f,0,   1,0,2.4f},     /* row values */
 [F_L]  ={FACE_SEMI, 28.0f,0,   1,0,2.6f},     /* page titles */
 [F_LAB]={FACE_SEMI, 22.0f,0.4f,0,0,2.4f},     /* ring labels set along the arc (HOME) */
 [F_CLK]={FACE_BOLD, 20.0f,0.2f,1,0,2.2f},     /* the clock under the rings */
 [F_NUM]={FACE_LIGHT,40.0f,0,   1,0,2.4f},     /* medium numbers (stats while brewing) */
 [F_XL] ={FACE_LIGHT,60.0f,0,   1,0,3.0f},     /* editor value */
 [F_XXL]={FACE_LIGHT,112.0f,-1, 1,0,4.0f},     /* the hero number (weight in the cup, milk temperature) */
 [F_H]  ={FACE_SEMI, 36.0f,0,   0,0,3.0f},     /* word hero in the system action */
 [F_ROLL]={FACE_LIGHT,64.0f,0,  1,0,3.0f},     /* roller: the selected value */
 [F_ROLL2]={FACE_MED,30.0f,0,   1,0,2.2f},     /* roller: neighbours */
 [F_IC] ={FACE_MED,  22.0f,0,   0,0,2.3f},     /* inline icons */
 [F_ICL]={FACE_MED,  48.0f,0,   0,0,2.6f},     /* large icons (in the HOME rings) */
 [F_STAT]={FACE_LIGHT,30.0f,0,  1,0,2.2f},     /* the three numbers while something runs */
};
#define G_ICON 0x1000                       /* glyph ids >= G_ICON are stroke icons: G_ICON + GD index */
#define GSEG_MAX 160
static float *gseg[NG]; static int gnseg[NG]; static char gparsed[NG];
typedef struct { uint8_t*a; short w,h,ox,oy; char st; } GBmp;   /* st: 0 = not built, 1 = ok, 2 = empty */
static GBmp gcache[NFONT][FG_N][4];        /* outline glyphs per style and 1/4-px phase */
static GBmp icache[NFONT][NG];             /* stroke icons per style */
static float fg_tnum[FG_NFACE];            /* tabular digit advance per face (font units) */
static int fg_digit0=-1;                   /* glyph index of '0' */

static void glyph_parse(int g){
    gparsed[g]=1; const char*p=GD[g].path; if(!p||!*p){ gnseg[g]=0; return; }
    float buf[4*GSEG_MAX]; int n=0; float px=0,py=0;
    while(*p){
        while(*p==' ')p++;
        char cmd=*p; if(!cmd) break; p++;
        float v[7]; int nv=(cmd=='E')?6:(cmd=='R'?7:2);
        for(int k=0;k<nv;k++){ char*e; v[k]=strtof(p,&e); p=e; if(*p==',')p++; }
        if(cmd=='M'){ px=v[0]; py=v[1]; }
        else if(cmd=='L'){ if(n<GSEG_MAX){ buf[4*n]=px; buf[4*n+1]=py; buf[4*n+2]=v[0]; buf[4*n+3]=v[1]; n++; } px=v[0]; py=v[1]; }
        else if(cmd=='R'){       /* v7.0: rotated ellipse arc: R cx,cy,rx,ry,rot,a0,a1 (degrees) */
            int ns=(int)ceilf(fabsf(v[6]-v[5])/9.f); if(ns<4)ns=4;
            float cr=cosf(v[4]*(float)M_PI/180), sr=sinf(v[4]*(float)M_PI/180), ax=0, ay=0;
            for(int k=0;k<=ns;k++){
                float ang=(v[5]+(v[6]-v[5])*k/ns)*(float)M_PI/180, ex=v[2]*cosf(ang), ey=v[3]*sinf(ang);
                float bx=v[0]+ex*cr-ey*sr, by=v[1]+ex*sr+ey*cr;
                if(k&&n<GSEG_MAX){ buf[4*n]=ax; buf[4*n+1]=ay; buf[4*n+2]=bx; buf[4*n+3]=by; n++; }
                ax=bx; ay=by;
            }
        }
        else if(cmd=='E'){
            int ns=(int)ceilf(fabsf(v[5]-v[4])/10.f); if(ns<3)ns=3;
            float ax=v[0]+v[2]*cosf(v[4]*(float)M_PI/180), ay=v[1]+v[3]*sinf(v[4]*(float)M_PI/180);
            for(int k=1;k<=ns;k++){
                float ang=(v[4]+(v[5]-v[4])*k/ns)*(float)M_PI/180;
                float bx=v[0]+v[2]*cosf(ang), by=v[1]+v[3]*sinf(ang);
                if(n<GSEG_MAX){ buf[4*n]=ax; buf[4*n+1]=ay; buf[4*n+2]=bx; buf[4*n+3]=by; n++; }
                ax=bx; ay=by;
            }
        }
    }
    gnseg[g]=n; gseg[g]=malloc(sizeof(float)*4*(n?n:1)); memcpy(gseg[g],buf,sizeof(float)*4*n);
}
/* a stroke icon (10-unit box) at the style's size: box = 1.3 x cap height, stroke = the style's icon stroke */
static float ico_box(int f){ return FS[f].cap*1.3f; }
static GBmp* ico_get(int f,int g){
    GBmp*b=&icache[f][g]; if(b->st) return b;
    if(!gparsed[g]) glyph_parse(g);
    const FStyle*fs=&FS[f]; float hw=fs->stroke/2, s=(ico_box(f)-fs->stroke)/10.f;
    int n=gnseg[g]; if(n==0){ b->st=2; return b; }
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
static float ico_adv(int f,int g){ return GD[g].adv*(ico_box(f)-FS[f].stroke)/10.f+FS[f].stroke+FS[f].cap*0.35f; }

/* ---- the outline rasterizer ---- */
static float*ras_a=NULL; static size_t ras_n=0;
static void ras_line(float*A,int w,int h,float x0,float y0,float x1,float y1){
    if(fabsf(y0-y1)<=1e-6f) return;
    float dir=1; if(y0>y1){ float t; t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; dir=-1; }
    float dxdy=(x1-x0)/(y1-y0), x=x0;
    int ya=(int)y0; if(y0<0){ x-=y0*dxdy; ya=0; }
    int yb=(int)ceilf(y1); if(yb>h) yb=h;
    for(int y=ya;y<yb;y++){
        float*row=A+(size_t)y*w;
        float dy=fminf((float)(y+1),y1)-fmaxf((float)y,y0), xn=x+dxdy*dy, d=dy*dir;
        float xa=x<xn?x:xn, xb=x<xn?xn:x;
        float xaf=floorf(xa); int xai=(int)xaf; float xbc=ceilf(xb); int xbi=(int)xbc;
        if(xai<0){ x=xn; continue; }
        if(xbi<=xai+1){
            float xm=0.5f*(x+xn)-xaf;
            if(xai+1<w){ row[xai]+=d-d*xm; row[xai+1]+=d*xm; }
        } else {
            float s=1.f/(xb-xa), x0f=xa-xaf, a0=0.5f*s*(1-x0f)*(1-x0f), x1f=xb-xbc+1, am=0.5f*s*x1f*x1f;
            if(xbi>=w){ x=xn; continue; }
            row[xai]+=d*a0;
            if(xbi==xai+2) row[xai+1]+=d*(1-a0-am);
            else {
                float a1=s*(1.5f-x0f); row[xai+1]+=d*(a1-a0);
                for(int xi=xai+2;xi<xbi-1;xi++) row[xi]+=d*s;
                float a2=a1+(xbi-xai-3)*s; row[xbi-1]+=d*(1-a2-am);
            }
            row[xbi]+=d*am;
        }
        x=xn;
    }
}
/* rasterize outline glyph gi of a face with the affine map X = m0·x + m1·y + tx, Y = m2·x + m3·y + ty (font units -> px) into b */
static void ras_glyph(int face,int gi,const float m[6],GBmp*b){
    const unsigned o0=FG_OPS[face][gi][0], o1=FG_OPS[face][gi+1][0], p0=FG_OPS[face][gi][1], p1=FG_OPS[face][gi+1][1];
    if(o1==o0||p1==p0){ b->st=2; return; }
    float mnx=1e9f,mny=1e9f,mxx=-1e9f,mxy=-1e9f;
    for(unsigned k=p0;k<p1;k++){ float x=FG_PT[2*k], y=FG_PT[2*k+1]; float X=m[0]*x+m[1]*y+m[4], Y=m[2]*x+m[3]*y+m[5];
        if(X<mnx)mnx=X; if(X>mxx)mxx=X; if(Y<mny)mny=Y; if(Y>mxy)mxy=Y; }
    int x0=(int)floorf(mnx)-1, y0=(int)floorf(mny)-1, x1=(int)ceilf(mxx)+2, y1=(int)ceilf(mxy)+1;
    int w=x1-x0, h=y1-y0; size_t need=(size_t)(w+2)*h;
    if(need>ras_n){ free(ras_a); ras_a=malloc(need*sizeof(float)); ras_n=ras_a?need:0; if(!ras_a){ b->st=2; return; } }
    memset(ras_a,0,need*sizeof(float)); int aw=w+2;
    float cx=0,cy=0,sx=0,sy=0; unsigned pi=p0;
#define FPX(k) (m[0]*FG_PT[2*(k)]+m[1]*FG_PT[2*(k)+1]+m[4]-x0)
#define FPY(k) (m[2]*FG_PT[2*(k)]+m[3]*FG_PT[2*(k)+1]+m[5]-y0)
    for(unsigned k=o0;k<o1;k++){
        switch(FG_OP[k]){
        case 0: cx=sx=FPX(pi); cy=sy=FPY(pi); pi++; break;
        case 1: { float nx=FPX(pi), ny=FPY(pi); pi++; ras_line(ras_a,aw,h,cx,cy,nx,ny); cx=nx; cy=ny; } break;
        case 2: { float qx=FPX(pi), qy=FPY(pi), nx=FPX(pi+1), ny=FPY(pi+1); pi+=2;
                  float ddx=cx-2*qx+nx, ddy=cy-2*qy+ny; float dev=ddx*ddx+ddy*ddy;
                  int ns=dev<0.333f?1:1+(int)floorf(sqrtf(sqrtf(3.f*dev)));
                  float px=cx, py=cy;
                  for(int i=1;i<=ns;i++){ float t=(float)i/ns, u=1-t; float ex=u*u*cx+2*u*t*qx+t*t*nx, ey=u*u*cy+2*u*t*qy+t*t*ny;
                      ras_line(ras_a,aw,h,px,py,ex,ey); px=ex; py=ey; }
                  cx=nx; cy=ny; } break;
        default: ras_line(ras_a,aw,h,cx,cy,sx,sy); cx=sx; cy=sy; break;
        }
    }
#undef FPX
#undef FPY
    b->w=(short)w; b->h=(short)h; b->ox=(short)x0; b->oy=(short)y0; b->a=malloc((size_t)w*h);
    if(!b->a){ b->st=2; return; }
    for(int j=0;j<h;j++){ float acc=0; const float*r=ras_a+(size_t)j*aw; uint8_t*q=b->a+(size_t)j*w;
        for(int i=0;i<w;i++){ acc+=r[i]; float v=fabsf(acc); if(v>1)v=1; q[i]=(uint8_t)(v*255.f+0.5f); } }
    b->st=1;
}
static float fg_scale(int f){ return FS[f].em/(float)FG_UPM; }
static GBmp* glyph_get(int f,int gi,int phase){
    GBmp*b=&gcache[f][gi][phase&3]; if(b->st) return b;
    float s=fg_scale(f); float m[6]={s,0,0,-s,(phase&3)*0.25f,0};
    ras_glyph(FS[f].face,gi,m,b); return b;
}
static int fg_index(int cp){ int lo=0,hi=FG_N-1; while(lo<=hi){ int md=(lo+hi)/2; if(FG_CP[md]==cp) return md; if(FG_CP[md]<cp) lo=md+1; else hi=md-1; } return -1; }
static int fg_kern(int face,int l,int r){
    const unsigned short*k=FK_K[face]; unsigned key=(unsigned)(l<<8|r); int lo=0,hi=FK_N[face]-1;
    while(lo<=hi){ int md=(lo+hi)/2; if(k[md]==key) return FK_V[face][md]; if(k[md]<key) lo=md+1; else hi=md-1; } return 0;
}
static int fg_isdigit(int gi){ return fg_digit0>=0&&gi>=fg_digit0&&gi<fg_digit0+10; }
/* advance of glyph gi in style f, in px (tabular digits: the widest digit) */
static float fg_adv(int f,int gi){ const FStyle*fs=&FS[f]; float a=(fs->tnum&&fg_isdigit(gi))?fg_tnum[fs->face]:FG_ADV[fs->face][gi]; return a*fg_scale(f)+fs->track; }
/* where the glyph sits inside its advance (tabular digits are centred in the wider cell) */
static float fg_lsb(int f,int gi){ const FStyle*fs=&FS[f]; if(fs->tnum&&fg_isdigit(gi)) return (fg_tnum[fs->face]-FG_ADV[fs->face][gi])*0.5f*fg_scale(f); return 0; }
/* UTF-8 -> glyph id: an outline glyph index, or G_ICON + GD index for the control bytes, -1 = takes no space */
static int glyph_next(const unsigned char**pp){
    const unsigned char*p=*pp; int c=*p++, cp;
    if(c<0x80){
        *pp=p;
        if(c==0x0E||c==0x0F) return -1;                       /* code-span markers (all text is drawn as written since v7.0) */
        if(c>=1&&c<=9){ static const int ic[10]={0,G_BEAN,G_CUP,G_DROP,G_STEAM,G_CHECK,G_CROSS,G_MOON,G_WARN,G_BOLT}; return G_ICON+ic[c]; }
        if(c==0x0B) return G_ICON+G_CLOCK;
        if(c<32) return -1;
        cp=c;
    } else if((c&0xE0)==0xC0&&(p[0]&0xC0)==0x80){ cp=((c&0x1F)<<6)|(p[0]&0x3F); p+=1; *pp=p; }
    else if((c&0xF0)==0xE0&&(p[0]&0xC0)==0x80&&(p[1]&0xC0)==0x80){ cp=((c&0x0F)<<12)|((p[0]&0x3F)<<6)|(p[1]&0x3F); p+=2; *pp=p; }
    else { while((*p&0xC0)==0x80) p++; *pp=p; cp='?'; }
    int gi=fg_index(cp); if(gi<0) gi=fg_index('?');
    return gi;
}
static float text_wf(const char*s,int f){
    const unsigned char*p=(const unsigned char*)s; float w=0; int prev=-1, face=FS[f].face, any=0;
    while(*p){ int g=glyph_next(&p); if(g<0) continue;
        if(g>=G_ICON){ w+=ico_adv(f,g-G_ICON); prev=-1; any=1; continue; }
        if(prev>=0) w+=fg_kern(face,prev,g)*fg_scale(f);
        w+=fg_adv(f,g); prev=g; any=1; }
    return any? w-FS[f].track : 0;
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
/* text: (x,y) = left edge / cap top (the baseline is y + cap) */
static float text_at(float x,int y,const char*s,int f,uint32_t c,int a){
    const unsigned char*p=(const unsigned char*)s; int prev=-1, face=FS[f].face; int base=y+(int)lroundf(FS[f].cap);
    while(*p){ int g=glyph_next(&p); if(g<0) continue;
        if(g>=G_ICON){ int ig=g-G_ICON; GBmp*b=ico_get(f,ig); float box=ico_box(f);
            if(b->st==1) glyph_blit(b,(int)lroundf(x+FS[f].cap*0.05f)+b->ox,base-(int)lroundf(box*0.5f+FS[f].cap*0.5f)+b->oy,c,a);
            x+=ico_adv(f,ig); prev=-1; continue; }
        if(prev>=0) x+=fg_kern(face,prev,g)*fg_scale(f);
        float gx=x+fg_lsb(f,g); float fl=floorf(gx); int ph=(int)((gx-fl)*4.f+0.5f); int ix=(int)fl; if(ph>=4){ ph=0; ix++; }
        GBmp*b=glyph_get(f,g,ph); if(b->st==1) glyph_blit(b,ix+b->ox,base+b->oy,c,a);
        x+=fg_adv(f,g); prev=g; }
    return x;
}
static void text_c(int cx,int y,const char*s,int f,uint32_t c,int a){ text_at(cx-text_wf(s,f)/2,y,s,f,c,a); }
static void text_r(int xr,int y,const char*s,int f,uint32_t c,int a){ text_at(xr-text_wf(s,f),y,s,f,c,a); }
/* truncate s (buffer w. >= 4 bytes of headroom) with "…" until it is <= maxw px wide; cuts at UTF-8 boundaries */
static void fit_text(char*s,int f,int maxw){
    if(text_w(s,f)<=maxw) return;
    size_t n=strlen(s);
    while(n>0){ do{ n--; }while(n>0&&((unsigned char)s[n]&0xC0)==0x80); while(n>0&&s[n-1]==' ') n--; memcpy(s+n,"\xE2\x80\xA6",4); if(text_w(s,f)<=maxw) return; }
}
/* the angular width (degrees) a text takes on a circle of radius r */
static float text_arc_deg(const char*s,int f,float r){ return text_wf(s,f)/r*180.f/(float)M_PI; }
/* text set along a circle (cx,cy), centred on angle ang (degrees; screen: 0 = right, 90 = down). The text's vertical
 * centre (half the cap height) lies on radius r. out=1: the tops point away from the centre and the text runs clockwise
 * (labels on the upper half); out=0: the tops point to the centre and it runs counter-clockwise (lower half), so both read
 * upright-ish. Rasterized per glyph with rotation, straight into cv (meant for static layers: nothing is cached). */
static void text_arc(float cx,float cy,float r,float ang,int out,const char*s,int f,uint32_t c,int a){
    float tw=text_wf(s,f), s0=-tw/2, sc=fg_scale(f), cap=FS[f].cap; int face=FS[f].face, prev=-1;
    float rb=out? r-cap/2 : r+cap/2;                     /* the baseline radius */
    const unsigned char*p=(const unsigned char*)s; float pen=0;
    while(*p){ int g=glyph_next(&p); if(g<0||g>=G_ICON) continue;
        if(prev>=0) pen+=fg_kern(face,prev,g)*sc;
        float adv=fg_adv(f,g), mid=s0+pen+fg_lsb(f,g)+FG_ADV[face][g]*sc/2;
        float th=(ang+(out?1:-1)*mid/r*180.f/(float)M_PI)*(float)M_PI/180.f, ct=cosf(th), st=sinf(th);
        float Tx=out?-st:st, Ty=out?ct:-ct, Ux=out?ct:-ct, Uy=out?st:-st;   /* tangent (reading direction) and "up" */
        float Px=cx+rb*ct, Py=cy+rb*st, h=FG_ADV[face][g]*sc/2;
        /* screen = P + (x·s − h)·T − (y·s)·(−U)  →  font y is up, U is the screen-space up */
        float m[6]={sc*Tx,sc*Ux,sc*Ty,sc*Uy,Px-h*Tx,Py-h*Ty};
        GBmp b={0}; ras_glyph(face,g,m,&b);
        if(b.st==1){ glyph_blit(&b,b.ox,b.oy,c,a); free(b.a); }
        pen+=adv; prev=g; }
}
static void font_init(void){
    for(int f=0;f<NFONT;f++) FS[f].cap=FS[f].em*FG_METRIC[(int)FS[f].face][2]/(float)FG_UPM;
    fg_digit0=fg_index('0');
    for(int fc=0;fc<FG_NFACE;fc++){ float m=0; for(int d=0;d<10;d++) if(FG_ADV[fc][fg_digit0+d]>m) m=FG_ADV[fc][fg_digit0+d]; fg_tnum[fc]=m; }
}
static void font_prewarm(void){
    static const char*const dig="0123456789.-";
    for(const char*p=dig;*p;p++){ int g=fg_index(*p); for(int ph=0;ph<4;ph++){ glyph_get(F_XXL,g,ph); glyph_get(F_XL,g,ph); glyph_get(F_NUM,g,ph); glyph_get(F_M,g,ph); } }
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
    canvas=calloc(1,n); bgpure=calloc(1,n); bgbase=calloc(1,n); slay=calloc(1,n); bgdark=calloc(1,n); snapA=calloc(1,n); snapB=calloc(1,n); tbuf=calloc(1,n);
    for(size_t i=0;i<(size_t)W*H;i++){ canvas[i]=0xFF000000u; tbuf[i]=0xFF000000u; }
    cv=canvas; clip_init(); bg_build();
    arc_build(&garc,229,1.6f,0,-90,360,0,0,0);     /* v7.0: the thin progress ring at the edge (weight / target, milk / target, the splash sweep) */
    arc_build(&parc,198,1.5f,0,135,270,0,0,0);
    font_init(); font_prewarm();
}

