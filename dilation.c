/* ============================================================================
 * ΔILATION — time moves when you move.
 *
 * A single-file SUPERHOT x Matrix homage built on the .murkk/.kkrieger rules:
 *   - one C file, no assets on disk, no engine
 *   - textures, levels, meshes, audio, font: all synthesized at startup/runtime
 *   - deterministic seed (it's a demo); --seed N to override, --level N to pick
 *   - stripped dynamic binary should stay tiny; -Os, immediate-mode GL2
 *
 * The gimmick: the simulation timescale follows YOUR motion (walk, look, act).
 * Stand still and the world freezes to a crawl — bullets hang mid-air on
 * oscilloscope trails. Everything that moves relative to you is shaded by
 * Doppler shift: blue approaching, red receding. The synth pitch-bends with
 * the timescale, so freezing time drops the whole soundtrack an octave.
 *
 * Agents are faceted low-poly humanoids with emerald eyes. One hit shatters
 * them into glowing polygon shards. RMB swings a katana that deflects bullets
 * back at the nearest agent. Clear all agents to win.
 *
 * Movement: SPACE jumps (twice — once more in the air), kicking off a wall
 * mid-air rebounds you upward, SHIFT/CTRL dodge-rolls under fire. Sectors are
 * built vertically: platforms, stairs, train roofs, mezzanines.
 *
 * build: gcc -Os dilation.c -o dilation -lSDL2 -lGL -lm
 * smoke: ./dilation --smoke      (headless-friendly; writes PPM screenshots)
 * license: CC0 / public domain. greets to .theprodukkt & SUPERHOT team.
 * ==========================================================================*/

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- GL loader
 * Pull GL2 entry points through SDL_GL_GetProcAddress. Bulletproof across
 * Mesa/NVIDIA without GL_GLEXT_PROTOTYPES link games. */
#define GLFUNCS \
  GF(PFNGLCREATESHADERPROC,      glCreateShader)      \
  GF(PFNGLSHADERSOURCEPROC,      glShaderSource)      \
  GF(PFNGLCOMPILESHADERPROC,     glCompileShader)     \
  GF(PFNGLGETSHADERIVPROC,       glGetShaderiv)       \
  GF(PFNGLGETSHADERINFOLOGPROC,  glGetShaderInfoLog)  \
  GF(PFNGLCREATEPROGRAMPROC,     glCreateProgram)     \
  GF(PFNGLATTACHSHADERPROC,      glAttachShader)      \
  GF(PFNGLLINKPROGRAMPROC,       glLinkProgram)       \
  GF(PFNGLGETPROGRAMIVPROC,      glGetProgramiv)      \
  GF(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
  GF(PFNGLUSEPROGRAMPROC,        glUseProgram)        \
  GF(PFNGLGETUNIFORMLOCATIONPROC,glGetUniformLocation)\
  GF(PFNGLUNIFORM1FPROC,         glUniform1f)         \
  GF(PFNGLUNIFORM1IPROC,         glUniform1i)         \
  GF(PFNGLUNIFORM3FPROC,         glUniform3f)         \
  GF(PFNGLUNIFORM3FVPROC,        glUniform3fv)        \
  GF(PFNGLUNIFORM4FVPROC,        glUniform4fv)        \
  GF(PFNGLUNIFORMMATRIX3FVPROC,  glUniformMatrix3fv)  \
  GF(PFNGLACTIVETEXTUREPROC,     glActiveTexture_)    \
  GF(PFNGLUNIFORM2FPROC,         glUniform2f)         \
  GF(PFNGLGENFRAMEBUFFERSPROC,   glGenFramebuffers)   \
  GF(PFNGLBINDFRAMEBUFFERPROC,   glBindFramebuffer)   \
  GF(PFNGLDELETEFRAMEBUFFERSPROC,  glDeleteFramebuffers)  \
  GF(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
  GF(PFNGLFRAMEBUFFERTEXTURE2DPROC,    glFramebufferTexture2D)    \
  GF(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
  GF(PFNGLGENRENDERBUFFERSPROC,  glGenRenderbuffers)  \
  GF(PFNGLBINDRENDERBUFFERPROC,  glBindRenderbuffer)  \
  GF(PFNGLRENDERBUFFERSTORAGEPROC,            glRenderbufferStorage) \
  GF(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
  GF(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
  GF(PFNGLBLITFRAMEBUFFERPROC,   glBlitFramebuffer)

#define GF(t,n) static t n;
GLFUNCS
#undef GF
/* GL2-era Macs and old Mesa expose the FBO calls only under their EXT names,
 * while everything since exposes the ARB/core spelling. Ask for the core name,
 * fall back to <name>EXT — the two are ABI-identical for everything we use. */
static void*gl_proc(const char*n){
  void*p=SDL_GL_GetProcAddress(n);
  if(!p){ char alt[64]; snprintf(alt,sizeof alt,"%sEXT",n); p=SDL_GL_GetProcAddress(alt); }
  return p;
}
static void load_gl(void){
#define GF(t,n) n = (t)gl_proc(#n[strlen(#n)-1]=='_'?"glActiveTexture":#n);
  GLFUNCS
#undef GF
}
/* per-location uniform cache. The figure draws re-send the same tint,
 * emissive, gloss and normal-scale hundreds of times a frame between
 * primitives that never changed them; skip the upload when the value is
 * unchanged. Locations are per program, so a program bind clears the cache
 * (uniform VALUES persist in the program object, only the bookkeeping resets). */
#define UCACHE 64
static float ucv[UCACHE][3]; static unsigned char ucok[UCACHE];
static void uni1f(GLint l,float v){
  if(l>=0&&l<UCACHE){ if(ucok[l]==1&&ucv[l][0]==v)return; ucok[l]=1; ucv[l][0]=v; }
  glUniform1f(l,v); }
static void uni3f(GLint l,float x,float y,float z){
  if(l>=0&&l<UCACHE){ if(ucok[l]==3&&ucv[l][0]==x&&ucv[l][1]==y&&ucv[l][2]==z)return;
    ucok[l]=3; ucv[l][0]=x;ucv[l][1]=y;ucv[l][2]=z; }
  glUniform3f(l,x,y,z); }
static void useprog(GLuint p){ memset(ucok,0,sizeof ucok); glUseProgram(p); }
#define glUniform1f uni1f
#define glUniform3f uni3f
#define glUseProgram useprog
/* enums we use that predate the headers on some SDKs */
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

/* ---------------------------------------------------------------- constants */
/* Two resolutions, and keeping them apart is the whole trick.
 *
 * HUDW/HUDH is a VIRTUAL 1280x720 space that every HUD coordinate is authored
 * in. draw_hud's ortho matrix maps it onto whatever the real framebuffer is, so
 * the ~40 hand-placed text and bar positions never have to know the resolution.
 *
 * fbW/fbH is the real drawable size in PIXELS, from SDL_GL_GetDrawableSize —
 * which on a Retina display is 2x the window size. Everything that allocates or
 * reads pixels (the post chain, the viewport, glReadPixels, the projection
 * aspect) uses these. They used to be the same fixed 1280x720 #define, so the
 * game rendered a 720p image and let the OS upscale it. */
#define HUDW 1280
#define HUDH 720
static int winW=HUDW, winH=HUDH;   /* window size, logical points  */
static int fbW=HUDW,  fbH=HUDH;    /* drawable size, real pixels   */
/* Above this many pixels we render the scene at a fraction and let the composite
 * upscale — a 6K display is 4x the fragments of 1440p for no visible gain on
 * geometry this clean, and the three-octave blur is the dominant cost. */
#define PIXEL_BUDGET (2560*1440)
static float renderScale=1.0f;
#define G 44              /* grid cells per side  */
#define CELL 2.0f         /* world units per cell */
#define EYE 1.62f
#define PI 3.14159265358979f
#define MAXENEMY 32
#define MAXITEM 32
#define MAXLIGHT 64
#define MAXTEMPL 16
#define MAXPART 512
#define MAXVOICE 16
#define MAXBUL 256
#define TRAILN 12         /* trail points per bullet */
#define MAXSHARD 512
#define SHLIGHTS 8        /* lights fed to the shader per frame */
#define MINTS 0.045f      /* simulation never fully stops (SUPERHOT creep) */
#define NLEVEL 4
#define SMOKE_SEEDS 48    /* layouts --smoke validates beyond the one it plays */
#define STEP 0.55f        /* max auto step-up: anything taller is a wall */
/* How far an agent will willingly step off. It used to be 0.65 with no falling
 * physics at all, which meant anything placed on a 1.1 platform, a 1.6 mezzanine
 * or a 2.35 train roof could never come down — strikers stranded up there could
 * not be reached by their own AI and just camped. Agents fall properly now, so
 * the limit only has to exclude drops that would look like a dive. */
#define AGENT_DROP 2.6f
#define GUN_MAX_RANGE 42.0f
#define GUN_MIN_RANGE 3.5f     /* point-blank floor: a fired round always reaches */
#define GUN_CHARGE_TIME 3.85f
#define GUN_IDLE_CHARGE 0.03f  /* tiny capacitor leak; movement does the work */
#define PLAYER_MAX_AMMO 18
#define PLAYER_BULLET_SPEED 21.5f
#define ROLL_TIME  0.42f       /* dodge roll duration */
#define ROLL_CD    0.65f       /* dodge roll cooldown  */
#define SWING_TIME 0.26f       /* katana active swing  */
#define FIRE_TIME  0.34f       /* pistol refire        */
#define GUN_SETTLE_TIME 0.22f  /* stillness before the aim settles on the look ray */
#define GUN_MIN_LASER   1.0f   /* the pointer never shrinks below this */
static float wallh=3.4f;  /* hall height, set per sector */

/* ---------------------------------------------------------------- rng/noise */
static unsigned rngs;
static unsigned xs(void){ rngs^=rngs<<13; rngs^=rngs>>17; rngs^=rngs<<5; return rngs; }
static float frand(void){ return (xs()&0xffffff)/(float)0x1000000; }
/* A SECOND stream, for anything purely visual (camera shake, HUD glitch slices).
 * Draws from the sim stream inside the render path would make the deterministic
 * --smoke gate depend on what happened to be on screen; keep them separate. */
static unsigned vrngs=0x9E3779B9u;
static float vrand(void){ vrngs^=vrngs<<13; vrngs^=vrngs>>17; vrngs^=vrngs<<5;
                          return (vrngs&0xffffff)/(float)0x1000000; }

static unsigned ihash(unsigned x){
  x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16; return x;
}
static float hash2(int x,int y,unsigned s){
  return (ihash((unsigned)x*374761393u + (unsigned)y*668265263u + s*1442695041u)&0xffffff)/(float)0x1000000;
}
/* tileable value noise on a power-of-two lattice */
static float vnoise(float x,float y,int per,unsigned s){
  int ix=(int)floorf(x), iy=(int)floorf(y);
  float fx=x-ix, fy=y-iy;
  fx=fx*fx*(3-2*fx); fy=fy*fy*(3-2*fy);
  int m=per-1;
  float a=hash2(ix&m,iy&m,s),     b=hash2((ix+1)&m,iy&m,s);
  float c=hash2(ix&m,(iy+1)&m,s), d=hash2((ix+1)&m,(iy+1)&m,s);
  return a+(b-a)*fx+(c-a)*fy+(a-b-c+d)*fx*fy;
}
static float fbm(float u,float v,int oct,int per,unsigned s){
  float sum=0,amp=0.5f; int p=per;
  for(int i=0;i<oct;i++){ sum+=amp*vnoise(u*p,v*p,p,s+i*131u); p<<=1; amp*=0.5f; }
  return sum;
}
static float clampf(float v,float lo,float hi){ return v<lo?lo:v>hi?hi:v; }
/* easing: the difference between animation and jank */
static float sstep(float x){ x=clampf(x,0,1); return x*x*(3-2*x); }
static float easeOutBack(float x){ x=clampf(x,0,1); float c=1.70158f,k=x-1;
  return 1.0f+(c+1.0f)*k*k*k+c*k*k; }
static float toward(float v,float t,float k){ return v+(t-v)*clampf(k,0,1); }
/* like toward, but on an angle: takes the short way around */
static float angto(float a,float b,float k){
  float d=fmodf(b-a+PI,2*PI); if(d<0)d+=2*PI; d-=PI;
  return a+d*clampf(k,0,1);
}

/* ---------------------------------------------------------------- mat3 (col-major) */
static void m3id(float*m){ memset(m,0,36); m[0]=m[4]=m[8]=1; }
static void m3rotY(float*m,float a){ float c=cosf(a),s=sinf(a);
  m[0]=c;m[1]=0;m[2]=-s; m[3]=0;m[4]=1;m[5]=0; m[6]=s;m[7]=0;m[8]=c; }
static void m3rotX(float*m,float a){ float c=cosf(a),s=sinf(a);
  m[0]=1;m[1]=0;m[2]=0; m[3]=0;m[4]=c;m[5]=s; m[6]=0;m[7]=-s;m[8]=c; }
static void m3rotZ(float*m,float a){ float c=cosf(a),s=sinf(a);
  m[0]=c;m[1]=s;m[2]=0; m[3]=-s;m[4]=c;m[5]=0; m[6]=0;m[7]=0;m[8]=1; }
static void m3mul(float*o,const float*a,const float*b){ float t[9];
  for(int j=0;j<3;j++)for(int i=0;i<3;i++)
    t[j*3+i]=a[i]*b[j*3]+a[3+i]*b[j*3+1]+a[6+i]*b[j*3+2];
  memcpy(o,t,36); }
static void m3scl(float*m,float x,float y,float z){
  m[0]*=x;m[1]*=x;m[2]*=x; m[3]*=y;m[4]*=y;m[5]*=y; m[6]*=z;m[7]*=z;m[8]*=z; }
static void m3v(const float*m,float x,float y,float z,float*o){
  o[0]=m[0]*x+m[3]*y+m[6]*z; o[1]=m[1]*x+m[4]*y+m[7]*z; o[2]=m[2]*x+m[5]*y+m[8]*z; }
/* pre-rotate basis B (world space) so unit direction u lands on unit v —
 * Rodrigues with the sine folded into the unnormalized axis w = u x v */
static void m3align(float*B,float ux,float uy,float uz,float vx,float vy,float vz){
  float wx=uy*vz-uz*vy, wy=uz*vx-ux*vz, wz=ux*vy-uy*vx;
  float c=ux*vx+uy*vy+uz*vz, s2=wx*wx+wy*wy+wz*wz;
  if(s2<1e-10f)return;                    /* already aligned (or antipodal) */
  float k=(1.0f-c)/s2;
  float R[9]={ c+k*wx*wx,  wz+k*wx*wy, -wy+k*wx*wz,
              -wz+k*wx*wy, c+k*wy*wy,   wx+k*wy*wz,
               wy+k*wx*wz,-wx+k*wy*wz,  c+k*wz*wz };
  m3mul(B,R,B);
}

/* ---------------------------------------------------------------- textures
 * The construct is black-on-black: dark panel walls (the digital rain is
 * painted by the fragment shader, not the texture), glossy obsidian floor
 * tiles with hairline emerald seams, slotted ceiling. */
enum { TX_WALL, TX_FLOOR, TX_CEIL, TX_GLOW, TX_COUNT };
static GLuint texAlb[TX_COUNT], texNrm[TX_COUNT];
#define TS 256

/* max anisotropy the driver will give us, capped at 8; 0 = extension absent.
 * Resolved once by gen_textures(). The floor grid runs to the horizon at a
 * grazing angle, where plain trilinear smears the emerald seams into mush. */
static float texAniso=0;
static GLuint mktex(unsigned char*px){
  GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
  glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
  if(texAniso>1.0f) glTexParameterf(GL_TEXTURE_2D,0x84FE/*MAX_ANISOTROPY_EXT*/,texAniso);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,TS,TS,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
  return t;
}
/* heightfield -> tangent-space normal map */
static void h2n(float*h,unsigned char*out,float str){
  for(int y=0;y<TS;y++)for(int x=0;x<TS;x++){
    float l=h[y*TS+((x-1)&(TS-1))], r=h[y*TS+((x+1)&(TS-1))];
    float u=h[((y-1)&(TS-1))*TS+x], d=h[((y+1)&(TS-1))*TS+x];
    float nx=(l-r)*str, ny=(u-d)*str, nz=1.0f;
    float il=1.0f/sqrtf(nx*nx+ny*ny+nz*nz);
    unsigned char*p=&out[(y*TS+x)*4];
    p[0]=(unsigned char)((nx*il*0.5f+0.5f)*255);
    p[1]=(unsigned char)((ny*il*0.5f+0.5f)*255);
    p[2]=(unsigned char)((nz*il*0.5f+0.5f)*255);
    p[3]=255;
  }
}
static void putrgb(unsigned char*p,float r,float g,float b){
  r=r<0?0:r>1?1:r; g=g<0?0:g>1?1:g; b=b<0?0:b>1?1:b;
  p[0]=(unsigned char)(r*255); p[1]=(unsigned char)(g*255); p[2]=(unsigned char)(b*255); p[3]=255;
}

static void gen_textures(void){
  if(SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic")){
    float mx=0; glGetFloatv(0x84FF/*MAX_MAX_ANISOTROPY_EXT*/,&mx);
    texAniso = mx>8.0f?8.0f:mx;
  }
  float *hh=malloc(TS*TS*sizeof(float));
  unsigned char *alb=malloc(TS*TS*4), *nrm=malloc(TS*TS*4);

  /* --- walls: brutalist black panels, faceted bevels, faint circuitry --- */
  for(int y=0;y<TS;y++)for(int x=0;x<TS;x++){
    float u=x/(float)TS, v=y/(float)TS;
    int px_=x&127, py=y&63;                       /* tall 1x0.5 panels */
    float dx=px_<64?px_:127-px_, dy=py<32?py:63-py;
    float d=dx<dy?dx:dy;
    float bevel=d/3.0f; if(bevel>1)bevel=1;
    bevel=bevel*bevel*(3-2*bevel);
    float grain=fbm(u,v,4,8,77u);
    /* per-panel identity: brightness jitter + the occasional recessed panel,
     * so a long wall stops reading as the same panel wallpapered 40 times */
    float pv=hash2(x>>7,y>>6,313u);
    hh[y*TS+x]=bevel*0.9f*(pv>0.86f?0.55f:1.0f)+grain*0.1f;
    /* NO CIRCUITRY. There used to be hashed "PCB traces" here — 8px dashes gated
     * by three hashes — and lit through the emissive mask they read as scattered
     * green confetti with no connectivity and no direction: mould stains, not
     * circuits. Worse, they competed directly with the digital rain, which is the
     * same colour, on the same surface, and is the thing actually worth looking
     * at. The wall's job is to be dark, panelled and structural so the rain has
     * something to fall down.
     *
     * What is left: a shallow vertical CHANNEL every half panel. It is cut into
     * the heightfield only, so it shows up as a normal-map crease that catches a
     * grazing highlight — structure you read as architecture rather than as
     * decoration, and it gives the rain a groove to run in. */
    int chx=x&31;
    float ch=(chx>13&&chx<18)?1.0f:0.0f;
    float chSoft=ch*(1.0f-fabsf((chx-15.5f)/2.5f))*0.35f;
    hh[y*TS+x]-=chSoft*(bevel>0.5f?1.0f:0.0f);
    float base = (0.030f + grain*0.014f)*(0.75f+0.50f*pv);
    if(bevel<0.4f) base*=0.45f;                   /* seams nearly black  */
    putrgb(&alb[(y*TS+x)*4], base*0.85f, base, base*0.95f);
    alb[(y*TS+x)*4+3]=0;                          /* walls emit nothing */
  }
  h2n(hh,nrm,3.0f);
  texAlb[TX_WALL]=mktex(alb); texNrm[TX_WALL]=mktex(nrm);

  /* --- floor: obsidian tiles, hairline emerald seams, high gloss --- */
  for(int y=0;y<TS;y++)for(int x=0;x<TS;x++){
    float u=x/(float)TS, v=y/(float)TS;
    int tx=x&127, ty=y&127;
    float dx=tx<64?tx:127-tx, dy=ty<64?ty:127-ty;
    float d=dx<dy?dx:dy;
    float bevel=d/4.0f; if(bevel>1)bevel=1;
    float grain=fbm(u,v,5,16,901u);
    hh[y*TS+x]=bevel*0.8f+grain*0.2f;
    float pv=hash2(x>>7,y>>7,717u);               /* per-tile brightness  */
    float base=(0.020f+grain*0.012f)*(0.80f+0.40f*pv);
    float seam=(d<2.0f)?1.0f:0.0f;                /* glowing grout line  */
    putrgb(&alb[(y*TS+x)*4],
      base + seam*0.015f,
      base + seam*0.110f,
      base + seam*0.050f);
    alb[(y*TS+x)*4+3]=(unsigned char)(seam*45.0f); /* seams feed the bloom */
  }
  h2n(hh,nrm,2.0f);
  texAlb[TX_FLOOR]=mktex(alb); texNrm[TX_FLOOR]=mktex(nrm);

  /* --- ceiling: black slabs with recessed light slots --- */
  for(int y=0;y<TS;y++)for(int x=0;x<TS;x++){
    float u=x/(float)TS, v=y/(float)TS;
    int sx_=x&63;
    float slot = (sx_>28&&sx_<35)?1.0f:0.0f;      /* strip every half cell */
    float grain=fbm(u,v,4,8,71u);
    hh[y*TS+x]=(1.0f-slot)*0.8f+grain*0.2f;
    float pv=hash2(x>>6,y>>6,551u);
    float base=(0.018f+grain*0.010f)*(0.85f+0.30f*pv);
    putrgb(&alb[(y*TS+x)*4],
      base+slot*0.022f, base+slot*0.13f, base+slot*0.06f);
    /* the light slots are actual emitters now: masked emissive -> bloom */
    alb[(y*TS+x)*4+3]=(unsigned char)(slot*200.0f);
  }
  h2n(hh,nrm,2.4f);
  texAlb[TX_CEIL]=mktex(alb); texNrm[TX_CEIL]=mktex(nrm);

  /* --- radial glow sprite --- */
  for(int y=0;y<TS;y++)for(int x=0;x<TS;x++){
    float dx=(x-128)/128.0f, dy=(y-128)/128.0f;
    float r=sqrtf(dx*dx+dy*dy);
    float a=1.0f-r; if(a<0)a=0; a=a*a*a;
    unsigned char*p=&alb[(y*TS+x)*4];
    p[0]=p[1]=p[2]=(unsigned char)(a*255); p[3]=(unsigned char)(a*255);
  }
  texAlb[TX_GLOW]=mktex(alb);

  free(hh); free(alb); free(nrm);
}

/* ---------------------------------------------------------------- 5x7 bitfont
 * 0-9 A-Z '-' '^' and '.' ; 7 row bytes per glyph, bit4 = leftmost column. */
static const unsigned char font[39][7]={
 {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
 {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
 {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
 {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
 {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
 {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
 {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
 {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
 {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
 {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
 {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
 {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
 {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
 {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
 {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
 {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
 {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
 {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
 {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
 {0x04,0x04,0x0A,0x0A,0x11,0x11,0x1F},   /* 37 = delta wordmark glyph */
 {0x00,0x00,0x00,0x00,0x00,0x00,0x04}};  /* 38 = full stop (decimal point) */

static float textw(const char*s,float sc){ return (float)strlen(s)*6*sc; }
/* the glyph pixels of one string go out as a single vertex-array draw: the
 * title rain alone used to be ~24k glVertex calls a frame */
static void draw_text(float x,float y,float sc,const char*s){
  static float tb[64*35*8]; int n=0;
  for(;*s;s++,x+=6*sc){
    int gi=-1; char c=*s;
    if(c>='0'&&c<='9')gi=c-'0'; else if(c>='A'&&c<='Z')gi=10+c-'A';
    else if(c=='-')gi=36; else if(c=='^')gi=37; else if(c=='.')gi=38;
    if(gi<0)continue;
    if(n+35*8>(int)(sizeof tb/sizeof tb[0]))break;
    for(int r=0;r<7;r++){ unsigned char row=font[gi][r];
      for(int col=0;col<5;col++) if(row&(0x10>>col)){
        float px=x+col*sc, py=y+r*sc, e=sc*0.92f;
        float*q=tb+n; n+=8;
        q[0]=px;q[1]=py; q[2]=px+e;q[3]=py; q[4]=px+e;q[5]=py+e; q[6]=px;q[7]=py+e;
      }}}
  if(!n)return;
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2,GL_FLOAT,0,tb);
  glDrawArrays(GL_QUADS,0,n/2);
  glDisableClientState(GL_VERTEX_ARRAY);
}

/* ---------------------------------------------------------------- level */
static unsigned char grid[G][G];           /* 1 = solid floor-to-ceiling   */
static float hgt[G][G];                    /* open cells: raised floor height
                                              (platforms, stairs, train roofs) */
static float startx,startz,startyaw;

typedef struct { float x,y,z,r; float cr,cg,cb; } Light;
static Light lights[MAXLIGHT]; static int nlights;
typedef struct { float x,y,z,r,life; float cr,cg,cb; } TempL;
static TempL templ_[MAXTEMPL];

typedef struct { float x,z; int type,taken,amt; float respawn,rtimer; } Item; /* 0 health 1 pistol/ammo; respawn>0 = timed re-arm cache */
static Item items[MAXITEM]; static int nitems;

typedef struct {
  float x,z,yaw,flash,anim,phase,state_t,armp;
  float lx,lz,vx,vz;            /* last pos -> velocity, for Doppler tint  */
  float hue;                    /* per-agent facet jitter                   */
  float y;                      /* floor height under the agent             */
  float moveb,fwdb,latb;        /* idle<->walk blend + local move direction */
  float flare,recoil;           /* smoothed eye flare, fire recoil 1->0     */
  float spdS;                   /* speed, attack-instant / release-eased     */
  float dieT;                   /* death collapse timer, world-time          */
  float lunRel;                 /* lunge follow-through, 1 -> 0 after the blow*/
  float headYaw,headPitch;      /* smoothed head-look delta toward the player*/
  float mzT,mzx,mzy,mzz;        /* muzzle flash: timer + the muzzle it fired from */
  int type;                     /* 0 shooter 1 striker 2 boss               */
  int state;                    /* 0 advance 1 aim 2 cooldown 3 lunge 4 dead*/
  int struck;                   /* lunge: the blow has already landed        */
  int hp;                       /* 0 = one-shot (normal); >1 boss health     */
  /* boss-only (type==2): leap physics + spiral/attack/melee clocks + phase   */
  float vy,spiralA,atkCD,jumpCD,roar,melT; int bphase;
} Enemy;
static Enemy en[MAXENEMY]; static int nen, nalive;

/* bullets: the slow, visible, deflectable kind. owner 0=enemy 1=player.
 * range>=0 means the round expires after that much travel. trail is a ring
 * buffer of past positions. */
typedef struct {
  float x,y,z,vx,vy,vz,life,range;
  int on,owner;
  float tr[TRAILN][3]; int tn,th; float trd;
} Bullet;
static Bullet bul[MAXBUL];

/* shards: agents shatter into these glowing polygons */
typedef struct {
  float x,y,z,vx,vy,vz,yaw,pit,wy,wp,sx,sy,sz,life,max;
  float r,g,b;
} Shard;
static Shard shards[MAXSHARD]; static int shHead=0;

/* level definitions: four hand-tuned sectors, each with its own climate —
 * same Matrix family, different temperature. */
typedef struct {
  const char*name; int style; unsigned seed;
  int nshoot,nstrike;
  int tier;                     /* difficulty rung, 0..3. Was derived from the
                                   curlevel INDEX, so reordering the campaign or
                                   adding a sector silently mis-tuned the AI. */
  float ceil;                   /* hall height */
  float fog[3];                 /* fog / horizon color  */
  float wallt[3];               /* wall tint            */
  float floort[3];              /* floor tint           */
} LevelDef;
static const LevelDef LEVELS[NLEVEL]={
  {"LOBBY",   0,0x1A0BB7u,10,3, 0, 5.4f,
    {0.0018f,0.0050f,0.0034f},{1.10f,1.22f,1.12f},{1.05f,1.15f,1.08f}},
  {"SUBWAY",  1,0x5ABBA7u,14,5, 1, 4.8f,
    {0.0042f,0.0030f,0.0015f},{1.25f,1.02f,0.78f},{1.10f,0.98f,0.82f}},
  {"TERMINAL",2,0x7E2211u,18,7, 2, 5.8f,
    {0.0015f,0.0034f,0.0050f},{0.82f,1.02f,1.22f},{0.85f,1.00f,1.18f}},
  /* OVERLORD: the final sector — a high black amphitheatre for the boss. no
     auto-spawned agents (nshoot/nstrike 0); the boss is placed by hand. tall
     ceiling for its leaps and the aerial ammo parkour. bruised violet climate. */
  {"OVERLORD",3,0xB055EDu, 0,0, 3, 8.0f,
    {0.0050f,0.0015f,0.0062f},{1.05f,0.72f,1.25f},{0.92f,0.78f,1.10f}},
};
static int curlevel=0;
static int bossIdx=-1;          /* index into en[] of the OVERLORD boss, or -1 */
static int bossMaxHp=60;        /* shots required to kill the boss             */

/* level mesh batches: 0 walls 1 floor 2 ceil 3 emissive edge trims;
 * interleaved p3 n3 uv2 ao1 — ao is baked per-vertex OCCLUSION (0 open,
 * ~0.5 boxed in), carried in the texcoord's third component so figures
 * drawn with plain glTexCoord2f (z=0) are naturally unoccluded. */
static float *batch[4]; static int bn[4], bcap[4];
static GLuint worldList[4];   /* the batches, compiled once per gen_level */
static void emit_v(int b,float px,float py,float pz,float nx,float ny,float nz,float ao){
  if(bn[b]+9>bcap[b]){ bcap[b]=bcap[b]?bcap[b]*2:4096; batch[b]=realloc(batch[b],bcap[b]*sizeof(float)); }
  /* tangent/bitangent from axis-aligned normal — must match shader */
  float tx,ty,tz,bx,by,bz;
  if(fabsf(ny)>0.5f){ tx=1;ty=0;tz=0; } else { tx=nz;ty=0;tz=-nx; }
  bx=ny*tz-nz*ty; by=nz*tx-nx*tz; bz=nx*ty-ny*tx;
  float u=(px*tx+py*ty+pz*tz)*0.5f, v=(px*bx+py*by+pz*bz)*0.5f;
  float*o=&batch[b][bn[b]];
  o[0]=px;o[1]=py;o[2]=pz; o[3]=nx;o[4]=ny;o[5]=nz; o[6]=u;o[7]=v; o[8]=ao;
  bn[b]+=9;
}
/* one trim vertex: batch 3, fixed UV at the glow sprite's white centre so the
 * strip reads as a solid emissive line regardless of the bound texture */
static void emit_trim(float px,float py,float pz,float nx,float ny,float nz){
  int b=3;
  if(bn[b]+9>bcap[b]){ bcap[b]=bcap[b]?bcap[b]*2:2048; batch[b]=realloc(batch[b],bcap[b]*sizeof(float)); }
  float*o=&batch[b][bn[b]];
  o[0]=px;o[1]=py;o[2]=pz; o[3]=nx;o[4]=ny;o[5]=nz; o[6]=0.5f;o[7]=0.5f; o[8]=0;
  bn[b]+=9;
}
/* emissive lip across the top of one riser face, from bottom corner (x0,z0) to
 * (x1,z1) up to height nh. Only platform/step edges get it — full walls to the
 * ceiling stay dark to keep the murk. */
static void emit_riser(float x0,float z0,float x1,float z1,float fh,float nh,float nx,float nz){
  if(nh>=wallh-0.05f)return;
  float t=nh-0.06f; if(t<fh)t=fh;
  emit_trim(x0,t,z0, nx,0,nz);  emit_trim(x1,t,z1, nx,0,nz);
  emit_trim(x1,nh,z1, nx,0,nz); emit_trim(x0,nh,z0, nx,0,nz);
}
static int circ_free(float x,float z,float r,float y);   /* defined with movement */
static int solid(int cx,int cz){ if(cx<0||cz<0||cx>=G||cz>=G)return 1; return grid[cz][cx]; }
/* floor-of-cell: solid cells are infinitely tall walls */
static float cellh(int cx,int cz){
  if(cx<0||cz<0||cx>=G||cz>=G)return 1e9f;
  return grid[cz][cx]? 1e9f : hgt[cz][cx];
}
/* floor height at a point, with solid cells flattened to ground level —
 * for effects and pickups that just need somewhere to sit */
static float floor_at(float x,float z){
  float h=cellh((int)floorf(x/CELL),(int)floorf(z/CELL));
  return h>100.0f?0:h;
}
/* baked corner occlusion: how boxed-in is the corner of cell (cx,cz) toward
 * (sx,sz)? Counts the three corner-sharing neighbours that rise above ref. */
static float corner_occ(int cx,int cz,int sx,int sz,float ref){
  int occ=0;
  if(cellh(cx+sx,cz )>ref+0.3f)occ++;
  if(cellh(cx, cz+sz)>ref+0.3f)occ++;
  if(cellh(cx+sx,cz+sz)>ref+0.3f)occ++;
  return 0.16f*occ;
}
static float ceil_occ(int cx,int cz,int sx,int sz){
  return 0.13f*(solid(cx+sx,cz)+solid(cx,cz+sz)+solid(cx+sx,cz+sz));
}
/* does a circle at (x,z) overlap cell (cx,cz)? */
static int circ_cell(float x,float z,float r,int cx,int cz){
  float bx0=cx*CELL,bx1=bx0+CELL,bz0=cz*CELL,bz1=bz0+CELL;
  float nx=x<bx0?bx0:(x>bx1?bx1:x), nz=z<bz0?bz0:(z>bz1?bz1:z);
  float dx=x-nx,dz=z-nz;
  return dx*dx+dz*dz < r*r;
}
static void carve(int x,int y,int w,int h){
  for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)
    if(i>=0&&j>=0&&i<G&&j<G){ grid[j][i]=0; hgt[j][i]=0; }
}
static void fill(int x,int y,int w,int h){
  for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++) if(i>=0&&j>=0&&i<G&&j<G) grid[j][i]=1;
}
/* open cells with a raised floor: platforms, desks, stairs, train roofs */
static void raise(int x,int y,int w,int h,float ht){
  for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)
    if(i>=0&&j>=0&&i<G&&j<G){ grid[j][i]=0; hgt[j][i]=ht; }
}
/* nudge a point out of solid geometry to the nearest open cell's centre —
 * TERMINAL's random monoliths could otherwise swallow a light orb or an item */
static void free_spot(float*x,float*z){
  int cx=(int)floorf(*x/CELL), cz=(int)floorf(*z/CELL);
  if(!solid(cx,cz))return;
  for(int r=1;r<4;r++)for(int dz=-r;dz<=r;dz++)for(int dx=-r;dx<=r;dx++)
    if(!solid(cx+dx,cz+dz)){ *x=(cx+dx+0.5f)*CELL; *z=(cz+dz+0.5f)*CELL; return; }
}
static void add_light(float x,float y,float z,float r,float cr,float cg,float cb){
  free_spot(&x,&z);
  if(nlights<MAXLIGHT){ Light*l=&lights[nlights++]; l->x=x;l->y=y;l->z=z;l->r=r;l->cr=cr;l->cg=cg;l->cb=cb; }
}
static void add_item_r(float x,float z,int type,int amt,float respawn){
  free_spot(&x,&z);
  /* recycle a spent pickup slot — but never a timed cache that is only waiting
   * out its re-arm, or an agent drop would quietly delete an OVERLORD perch */
  for(int i=0;i<nitems;i++) if(items[i].taken&&items[i].respawn<=0){
    items[i]=(Item){x,z,type,0,amt,respawn,0}; return; }
  if(nitems<MAXITEM) items[nitems++]=(Item){x,z,type,0,amt,respawn,0};
}
static void add_item(float x,float z,int type,int amt){ add_item_r(x,z,type,amt,0); }

/* ------------------------------------------------- reachability flood fill
 * Which open cells can the player actually GET to from the spawn? Nothing in
 * this game ever asked that question, which is how TERMINAL ended up able to
 * generate its reward terrace behind an unclimbable wall and place_agent ended
 * up able to strand a striker on a train roof. Four-connected BFS under the
 * same rules the mover enforces: rise at most JUMP_UP, fall any distance.
 *
 * JUMP_UP is the DOUBLE-jump apex, because the player has one: the ground jump
 * sets pvy=7.5 against g=18 (7.5^2/36 = 1.56) and the air jump adds pvy=6.6 from
 * there (6.6^2/36 = 1.21), so 2.77 total. 2.55 keeps a margin. That is generous
 * on purpose — it is exactly what SUBWAY's 2.35 train roofs need, and a
 * validator that rejects the shipped sectors is a validator nobody will run. */
#define JUMP_UP 2.55f
static unsigned char reach[G][G];
static int reachable(void){
  memset(reach,0,sizeof reach);
  int sx=(int)floorf(startx/CELL), sz=(int)floorf(startz/CELL);
  if(sx<0||sz<0||sx>=G||sz>=G||grid[sz][sx])return 0;
  static short qx[G*G],qz[G*G];      /* fixed queue; no malloc in this file */
  int head=0,tail=0,n=1;
  reach[sz][sx]=1; qx[tail]=(short)sx; qz[tail]=(short)sz; tail++;
  static const int D[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
  while(head<tail){
    int cx=qx[head], cz=qz[head]; head++;
    float h=hgt[cz][cx];
    for(int k=0;k<4;k++){
      int nx=cx+D[k][0], nz=cz+D[k][1];
      if(nx<0||nz<0||nx>=G||nz>=G||grid[nz][nx]||reach[nz][nx])continue;
      if(hgt[nz][nx]-h > JUMP_UP)continue;
      reach[nz][nx]=1; qx[tail]=(short)nx; qz[tail]=(short)nz; tail++; n++;
    }
  }
  return n;
}
/* is the cell under this world point one the player can stand on and get to? */
static int reach_at(float x,float z){
  int cx=(int)floorf(x/CELL), cz=(int)floorf(z/CELL);
  if(cx<0||cz<0||cx>=G||cz>=G)return 0;
  return reach[cz][cx];
}

/* small helper: a uniform integer in [0,n). frand() is strictly < 1, so the
 * cast can never reach n. Used everywhere the layout wants a seeded choice. */
static int irand(int n){ return n<=1?0:(int)(frand()*(float)n); }

/* ---------------------------------------------------- placement candidates
 * Every open, REACHABLE cell far enough from spawn to be a fight rather than an
 * ambush. Built once per generation attempt; agents sample it by INDEX.
 *
 * This is what makes the RNG draw count fixed. place_agent used to be a
 * rejection sampler that consumed anywhere from 2 to 400+ draws depending on
 * how cluttered the geometry happened to be — so the NUMBER of draws, not just
 * their values, depended on the layout. That, and not the call sites, was the
 * real reason the byte gate was so brittle. */
static short candX[G*G], candZ[G*G]; static int nCand;
static void build_candidates(int x0,int z0,int x1,int z1,float minSpawnD){
  nCand=0;
  if(x0<0)x0=0; if(z0<0)z0=0; if(x1>G)x1=G; if(z1>G)z1=G;
  for(int z=z0;z<z1;z++)for(int x=x0;x<x1;x++){
    if(grid[z][x]||!reach[z][x])continue;
    float wx=(x+0.5f)*CELL, wz=(z+0.5f)*CELL;
    float dx=wx-startx,dz=wz-startz;
    if(dx*dx+dz*dz<minSpawnD*minSpawnD)continue;
    candX[nCand]=(short)x; candZ[nCand]=(short)z; nCand++;
  }
}
/* Exactly five frand() draws, always: the index, then yaw/phase/stagger/hue.
 * If the sampled cell is too close to an already-placed agent we walk FORWARD
 * through the candidate list deterministically rather than re-rolling. */
static int place_agent(int type){
  if(nen>=MAXENEMY||nCand<=0)return 0;
  int start=irand(nCand);
  int placed=-1;
  for(int k=0;k<nCand;k++){
    int c=(start+k)%nCand;
    float wx=(candX[c]+0.5f)*CELL, wz=(candZ[c]+0.5f)*CELL;
    int ok=1;
    for(int i=0;i<nen;i++){ float ax=en[i].x-wx,az=en[i].z-wz;
      if(ax*ax+az*az<3*3){ok=0;break;} }
    if(ok){ placed=c; break; }
  }
  if(placed<0)return 0;
  int cx=candX[placed], cz=candZ[placed];
  Enemy*e=&en[nen++];
  memset(e,0,sizeof*e);
  e->x=e->lx=(cx+0.5f)*CELL; e->z=e->lz=(cz+0.5f)*CELL; e->type=type;
  e->y=hgt[cz][cx];
  e->yaw=frand()*2*PI; e->phase=frand()*6.28f;
  e->state=0; e->state_t=-frand()*2.0f;   /* stagger the first volley */
  e->hue=(frand()-0.5f)*0.08f;
  return 1;
}
/* an item on a reachable cell chosen from the candidate list */
static void add_item_cand(int type,int amt,float respawn){
  if(nCand<=0)return;
  int c=irand(nCand);
  add_item_r((candX[c]+0.5f)*CELL,(candZ[c]+0.5f)*CELL,type,amt,respawn);
}

/* ------------------------------------------------------------- generation
 * One ATTEMPT at a sector: carve the seeded layout, work out what is reachable,
 * populate it, and report whether the result is sound. gen_level below retries
 * until it gets a sound one, so nothing downstream ever sees a broken sector.
 *
 * Every sector keeps its own architectural grammar — that is what makes the
 * four places feel like places — but every PARAMETER of that grammar is now
 * drawn from the seed. Before this, three of the four sectors contained no
 * frand() at all and `--seed` was very nearly a no-op. */
static int gen_attempt(int li,unsigned seed){
  const LevelDef*L=&LEVELS[li];
  rngs=seed; if(!rngs)rngs=0xC0FFEEu;
  memset(grid,1,sizeof grid);
  memset(hgt,0,sizeof hgt);
  wallh=L->ceil;
  nlights=0; nitems=0; nen=0; bossIdx=-1;
  for(int b=0;b<4;b++)bn[b]=0;
  int ax0=0,az0=0,ax1=G,az1=G;                 /* agent placement region */

  switch(L->style){
    case 0:{ /* LOBBY: a corporate atrium — twin colonnades down a long hall, a
                mezzanine gallery over the north end reached by stair runs, low
                reception desks to vault or duck behind. ivory-emerald light. */
      int mg=3+irand(3);                         /* wall margin 3..5        */
      int x0=mg,x1=G-mg, z0=mg,z1=G-mg;
      carve(x0,z0,x1-x0,z1-z0);
      int mezD=3+irand(3);                       /* mezzanine depth 3..5    */
      raise(x0,z0,x1-x0,mezD,1.6f);
      /* stair runs down off the mezzanine's south edge: 1.2/0.8/0.4 puts every
         step inside STEP, so the gallery is walkable, not a jump puzzle */
      int nst=2+irand(2);
      for(int st=0;st<nst;st++){
        int sx=x0+1+irand(x1-x0-4);
        for(int r=0;r<3;r++) raise(sx,z0+mezD+r,3,1,1.2f-0.4f*r);
      }
      /* colonnades: two pillar rows flanking the central corridor */
      int inset=3+irand(3), pitch=3+irand(3);
      int cxA=x0+inset, cxB=x1-1-inset;
      for(int j=z0+mezD+4;j<z1-3;j+=pitch){ fill(cxA,j,1,1); fill(cxB,j,1,1); }
      /* desks, always between the colonnades so they read as furniture */
      int nd=2+irand(2);
      for(int d=0;d<nd;d++){
        int dw=4+irand(3);
        int dx=cxA+2+irand(cxB-cxA-dw-2);
        int dz=z0+mezD+6+irand(z1-z0-mezD-10);
        raise(dx,dz,dw,1,1.0f);
      }
      startx=(x0+x1)*0.5f*CELL; startz=(z1-1.5f)*CELL; startyaw=0;
      for(int j=z0+mezD+4;j<z1-3;j+=pitch*2){
        add_light((cxA+0.5f)*CELL,3.6f,(j+0.5f)*CELL,8.5f, 0.85f,0.95f,0.80f);
        add_light((cxB+0.5f)*CELL,3.6f,(j+0.5f)*CELL,8.5f, 0.85f,0.95f,0.80f);
      }
      add_light((x0+x1)*0.5f*CELL,4.6f,(z0+z1)*0.5f*CELL,16.0f, 0.30f,1.30f,0.65f);
      add_light((x0+x1)*0.5f*CELL,3.2f,(z0+mezD*0.5f)*CELL,11.0f, 0.60f,1.00f,0.78f);
      ax0=x0+1; az0=z0+1; ax1=x1-1; az1=z1-1;
      reachable();
      build_candidates(ax0,az0,ax1,az1,8.0f);
      /* the mezzanine ammo cache is the reward for taking the high ground, so
         it is placed ON the gallery rather than anywhere reachable */
      add_item((x0+x1)*0.5f*CELL,(z0+0.5f)*CELL,1,6);
      add_item_cand(0,35,0);
      add_item_cand(1,4,0);
    } break;

    case 1:{ /* SUBWAY: raised platforms over a track trench, a parked train
                whose roofs are a fighting surface, steps at both ends.
                sodium amber over the platforms, cold cyan over the rails. */
      int mg=3+irand(2);
      int hallZ=18+irand(5);
      int z0=(G-hallZ)/2, z1=z0+hallZ;
      int x0=mg, x1=G-mg;
      carve(x0,z0,x1-x0,hallZ);
      int platD=3+irand(2);
      raise(x0,z0,x1-x0,platD,1.1f);             /* north platform */
      raise(x0,z1-platD,x1-x0,platD,1.1f);       /* south platform */
      int tz0=z0+platD, tz1=z1-platD;            /* the trench      */
      /* the train: a seeded number of cars with gaps to thread between */
      int ncar=3+irand(3);
      int span=x1-x0-2, carLen=span/ncar-1; if(carLen<4)carLen=4;
      int carZ=tz0+1, carD=(tz1-tz0)-2; if(carD<3)carD=3;
      for(int c=0;c<ncar;c++){
        int cx=x0+1+c*(carLen+1);
        if(cx+carLen>x1-1)break;
        raise(cx,carZ,carLen,carD,2.35f);
      }
      /* track steps so the trench floor is never a pit you cannot leave */
      raise(x0,tz0,2,2,0.55f); raise(x1-2,tz1-2,2,2,0.55f);
      int ppitch=4+irand(3);
      for(int i=x0+2;i<x1-1;i+=ppitch){ fill(i,z0+platD-1,1,1); fill(i,z1-platD,1,1); }
      startx=(x0+1.5f)*CELL; startz=(z0+platD*0.5f)*CELL; startyaw=90;
      for(int i=x0+2;i<x1-1;i+=ppitch+2){
        add_light((i+0.5f)*CELL,3.7f,(z0+1.5f)*CELL,7.5f, 1.05f,0.72f,0.35f);
        add_light((i+0.5f)*CELL,3.7f,(z1-1.5f)*CELL,7.5f, 1.05f,0.72f,0.35f);
      }
      for(int i=x0+3;i<x1-1;i+=ppitch+4)
        add_light((i+0.5f)*CELL,3.3f,(tz0+tz1)*0.5f*CELL,9.5f, 0.25f,0.95f,1.05f);
      ax0=x0+2; az0=z0; ax1=x1-1; az1=z1;
      reachable();
      build_candidates(ax0,az0,ax1,az1,8.0f);
      /* ammo on a train roof: pick the middle car so the climb is the price */
      { int mc=ncar/2, cx=x0+1+mc*(carLen+1);
        add_item((cx+carLen*0.5f)*CELL,(carZ+carD*0.5f)*CELL,1,6); }
      add_item_cand(0,35,0);
      add_item_cand(0,35,0);
      add_item_cand(1,4,0);
    } break;

    case 3:{ /* OVERLORD: a black amphitheatre. wide open floor so the spiral
                reads, corner step-pyramids carrying respawning ammo you must
                platform up to, a ring of full-height cover pillars, and the
                boss enthroned dead centre. */
      int mg=3+irand(3);
      int x0=mg,x1=G-mg,z0=mg,z1=G-mg;
      carve(x0,z0,x1-x0,z1-z0);
      int mid=G/2;
      /* 2..4 step-pyramids, each tier a single jump above the last */
      int npy=2+irand(3);
      int corner[4][2]={{x0+1,z0+1},{x1-6,z0+1},{x0+1,z1-6},{x1-6,z1-6}};
      int order[4]={0,1,2,3};
      for(int k=3;k>0;k--){ int j=irand(k+1); int t=order[k];order[k]=order[j];order[j]=t; }
      for(int q=0;q<npy;q++){
        int bx=corner[order[q]][0], bz=corner[order[q]][1];
        raise(bx,  bz,  5,5,0.9f);
        raise(bx+1,bz+1,3,3,1.9f);
        raise(bx+2,bz+2,1,1,2.9f);
        float tx=(bx+2.5f)*CELL, tz=(bz+2.5f)*CELL;
        if(q&1) add_item_r(tx,tz,0,35,12.0f);
        else    add_item_r(tx,tz,1,10,7.0f);
        add_light(tx,3.4f,tz,7.0f, 0.55f,0.85f,1.05f);
      }
      /* ring of cover pillars at a seeded count and radius */
      int nring=6+irand(5);
      float rr=5.0f+(float)irand(3);
      for(int k=0;k<nring;k++){
        float a=k*2*PI/nring;
        fill(mid+(int)(cosf(a)*rr), mid+(int)(sinf(a)*rr),1,1);
      }
      add_light(mid*CELL,6.5f,mid*CELL,22.0f, 1.15f,0.30f,1.35f);  /* throne  */
      startx=mid*CELL; startz=(z1-1.5f)*CELL; startyaw=0;
      add_light(startx,3.0f,startz,9.0f, 0.70f,0.45f,1.10f);
      reachable();
      build_candidates(x0+1,z0+1,x1-1,z1-1,8.0f);
      add_item_cand(1,8,9.0f);              /* a ground cache near the fight  */
      /* enthrone the boss at centre */
      { Enemy*e=&en[nen++]; memset(e,0,sizeof*e);
        float bx=(mid+0.5f)*CELL, bz=(mid+0.5f)*CELL;
        e->x=e->lx=bx; e->z=e->lz=bz; e->type=2; e->hp=bossMaxHp=60;
        e->y=hgt[mid][mid]; e->yaw=PI; e->bphase=0;
        e->state=0; e->atkCD=2.2f; e->jumpCD=4.5f;
        bossIdx=nen-1;
      }
    } break;

    default:{ /* TERMINAL: vast ice-teal departure hall; monoliths at mixed
                 heights, and a SOLVED parkour chain up to a high corner
                 terrace. The chain used to be three blocks with clear air
                 between them, so the terrace was unreachable at every seed. */
      int mg=4+irand(2);
      int x0=mg,x1=G-mg,z0=mg,z1=G-mg;
      carve(x0,z0,x1-x0,z1-z0);
      int corner=irand(4);
      int tw=6+irand(2), td=5+irand(2);
      int tx = (corner&1)? x1-tw : x0;
      int tz = (corner&2)? z1-td : z0;
      raise(tx,tz,tw,td,2.8f);
      /* upper landing, FLUSH against the terrace in x; lower landing flush
         against the upper in z. 0 -> 1.2 -> 2.0 -> 2.8, every step one jump. */
      int ux = (corner&1)? tx-3 : tx+tw;
      int uz = tz + (td-3)/2;
      raise(ux,uz,3,3,2.0f);
      int lz = (corner&2)? uz-2 : uz+3;
      raise(ux,lz,3,2,1.2f);
      /* the climb corridor must stay clear of the monolith field, or a random
         block lands on a landing and the chain is broken again */
      int cx0=(ux<tx?ux:tx)-1, cx1=(ux+3>tx+tw?ux+3:tx+tw)+1;
      int cz0=(lz<tz?lz:tz)-1, cz1=(lz+2>tz+td?lz+2:tz+td)+1;
      startx=(x0+x1)*0.5f*CELL; startz=(z1-1.5f)*CELL; startyaw=0;
      int nmono=12+irand(8);
      for(int k=0;k<nmono;k++){
        int w=(xs()&1)?2:1, h=3-w;
        int mx2=x0+1+irand(x1-x0-3), mz2=z0+1+irand(z1-z0-3);
        if(mx2>=cx0&&mx2<=cx1&&mz2>=cz0&&mz2<=cz1)continue;   /* keep the climb honest */
        float sdx=(mx2+0.5f)*CELL-startx, sdz=(mz2+0.5f)*CELL-startz;
        if(sdx*sdx+sdz*sdz<7*7)continue;                      /* keep spawn clear      */
        if(k%5==0) fill(mx2,mz2,w,h);
        else       raise(mx2,mz2,w,h,(k%3==0)?2.0f:1.2f);
      }
      add_light((x0+x1)*0.5f*CELL,4.8f,(z0+z1)*0.5f*CELL,15.0f, 0.25f,1.10f,1.20f);
      add_light((tx+tw*0.5f)*CELL,4.4f,(tz+td*0.5f)*CELL,9.0f, 0.45f,1.05f,0.95f);
      reachable();
      build_candidates(x0+1,z0+1,x1-1,z1-1,8.0f);
      /* the terrace reward, then scattered caches on reachable ground */
      add_item((tx+tw*0.5f)*CELL,(tz+td*0.5f)*CELL,1,8);
      add_item_cand(0,35,0);
      add_item_cand(1,4,0);
      add_item_cand(0,35,0);
      /* seeded ambient lights on reachable floor */
      for(int k=0;k<8;k++){
        if(nCand<=0)break;
        int c=irand(nCand);
        add_light((candX[c]+0.5f)*CELL,3.6f,(candZ[c]+0.5f)*CELL,8.5f, 0.40f,0.85f,1.00f);
      }
    } break;
  }

  /* candidates are built per-style above (they need the layout AND the spawn).
   * If a style forgot, do it now so agent placement is never unseeded. */
  if(nCand<=0){ reachable(); build_candidates(ax0,az0,ax1,az1,8.0f); }

  int want=L->nshoot+L->nstrike+(L->style==3?1:0);
  for(int k=0;k<L->nshoot;k++)  place_agent(0);
  for(int k=0;k<L->nstrike;k++) place_agent(1);
  nalive=nen;

  /* ---- soundness. gen_level retries on a 0, so a bad roll never ships. ---- */
  float sy=cellh((int)floorf(startx/CELL),(int)floorf(startz/CELL));
  if(sy>100.0f||!circ_free(startx,startz,0.34f,sy))return 0;
  if(nen!=want)return 0;
  if(nCand<G*G/16)return 0;                 /* too cramped to be a sector */
  for(int i=0;i<nen;i++) if(!reach_at(en[i].x,en[i].z))return 0;
  for(int i=0;i<nitems;i++) if(!reach_at(items[i].x,items[i].z))return 0;
  if(nlights>=MAXLIGHT||nitems>=MAXITEM)return 0;
  return 1;
}

/* Generate sector `li`, retrying until the layout is sound. Every attempt is a
 * different seed, so a bad roll costs a few microseconds rather than shipping a
 * sector with an unreachable reward or a stranded striker. The last attempt is
 * accepted unconditionally: a playable-but-imperfect sector beats no sector, and
 * --seed-sweep exists to prove that branch is never actually needed. */
#define GEN_TRIES 24
static int genAttempts=0;          /* reported by the smoke gate */
static unsigned genSeedUsed=0;     /* shown on the title screen  */
static void gen_level(int li,unsigned seedmix){
  unsigned base=LEVELS[li].seed^seedmix;
  int a;
  for(a=0;a<GEN_TRIES;a++){
    genSeedUsed = base ^ ((unsigned)a*2654435761u);
    if(gen_attempt(li,genSeedUsed))break;
  }
  genAttempts=a+1;
  if(a>=GEN_TRIES) printf("[dilation] warning: %s fell back after %d attempts\n",
                          LEVELS[li].name,GEN_TRIES);

  /* mesh: floor at each cell's height, ceiling, and vertical faces — full
   * walls against solid cells, skirts where a neighbour's floor is higher */
  for(int z=0;z<G;z++)for(int x=0;x<G;x++){
    if(grid[z][x])continue;
    float x0=x*CELL,x1=x0+CELL,z0=z*CELL,z1=z0+CELL;
    float fh=hgt[z][x];
    /* floor corners darken where neighbouring cells rise; ceiling corners
     * where solid columns meet it — baked AO that finally separates an open
     * hall from a stairwell corner in a black-on-black construct */
    emit_v(1,x0,fh,z0, 0,1,0, corner_occ(x,z,-1,-1,fh));
    emit_v(1,x1,fh,z0, 0,1,0, corner_occ(x,z, 1,-1,fh));
    emit_v(1,x1,fh,z1, 0,1,0, corner_occ(x,z, 1, 1,fh));
    emit_v(1,x0,fh,z1, 0,1,0, corner_occ(x,z,-1, 1,fh));
    emit_v(2,x0,wallh,z0, 0,-1,0, ceil_occ(x,z,-1,-1));
    emit_v(2,x0,wallh,z1, 0,-1,0, ceil_occ(x,z,-1, 1));
    emit_v(2,x1,wallh,z1, 0,-1,0, ceil_occ(x,z, 1, 1));
    emit_v(2,x1,wallh,z0, 0,-1,0, ceil_occ(x,z, 1,-1));
    /* vertical faces, plus an emissive lip along each raised-floor riser top
     * (platforms, stairs, train roofs) — crisp edges that read the verticality.
     * Wall bases sink into contact shadow; tops shade only under the ceiling. */
    float nh,eps=0.016f;
    { float aob=0.42f;
    nh=solid(x-1,z)?wallh:hgt[z][x-1];
    if(nh>fh+0.001f){ float aot=(nh>=wallh-0.05f)?0.30f:0.0f;
                      emit_v(0,x0,fh,z0, 1,0,0,aob); emit_v(0,x0,fh,z1, 1,0,0,aob);
                      emit_v(0,x0,nh,z1, 1,0,0,aot); emit_v(0,x0,nh,z0, 1,0,0,aot);
                      emit_riser(x0+eps,z0, x0+eps,z1, fh,nh, 1,0); }
    nh=solid(x+1,z)?wallh:hgt[z][x+1];
    if(nh>fh+0.001f){ float aot=(nh>=wallh-0.05f)?0.30f:0.0f;
                      emit_v(0,x1,fh,z1, -1,0,0,aob); emit_v(0,x1,fh,z0, -1,0,0,aob);
                      emit_v(0,x1,nh,z0, -1,0,0,aot); emit_v(0,x1,nh,z1, -1,0,0,aot);
                      emit_riser(x1-eps,z1, x1-eps,z0, fh,nh, -1,0); }
    nh=solid(x,z-1)?wallh:hgt[z-1][x];
    if(nh>fh+0.001f){ float aot=(nh>=wallh-0.05f)?0.30f:0.0f;
                      emit_v(0,x1,fh,z0, 0,0,1,aob); emit_v(0,x0,fh,z0, 0,0,1,aob);
                      emit_v(0,x0,nh,z0, 0,0,1,aot); emit_v(0,x1,nh,z0, 0,0,1,aot);
                      emit_riser(x1,z0+eps, x0,z0+eps, fh,nh, 0,1); }
    nh=solid(x,z+1)?wallh:hgt[z+1][x];
    if(nh>fh+0.001f){ float aot=(nh>=wallh-0.05f)?0.30f:0.0f;
                      emit_v(0,x0,fh,z1, 0,0,-1,aob); emit_v(0,x1,fh,z1, 0,0,-1,aob);
                      emit_v(0,x1,nh,z1, 0,0,-1,aot); emit_v(0,x0,nh,z1, 0,0,-1,aot);
                      emit_riser(x0,z1-eps, x1,z1-eps, fh,nh, 0,-1); } }
  }
  /* the mesh never changes between gen_level calls, so compile the four
   * batches once instead of pushing ~400KB of client arrays through the
   * driver every frame. glDrawArrays dereferences the arrays at compile time.
   * (--seed-sweep runs before any GL context exists: skip the bake there.) */
  if(SDL_GL_GetCurrentContext()){
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    for(int b=0;b<4;b++){
      if(!worldList[b])worldList[b]=glGenLists(1);
      glNewList(worldList[b],GL_COMPILE);
      if(bn[b]>0){
        glVertexPointer(3,GL_FLOAT,36,batch[b]);
        glNormalPointer(GL_FLOAT,36,batch[b]+3);
        glTexCoordPointer(3,GL_FLOAT,36,batch[b]+6);
        glDrawArrays(GL_QUADS,0,bn[b]/9); }
      glEndList();
    }
    glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  }
}

/* ---------------------------------------------------------------- audio synth
 * Voices carry a pitch factor (Doppler) and a world flag: world-bound voices
 * advance at the simulation timescale, so freezing time pitch-bends every
 * sound down with it. g_ats is a single float written by the game thread and
 * read by the audio callback — a word-sized store, same lock-free style the
 * original used for voices[]. */
enum { V_SHOT, V_ESHOT, V_DEFLECT, V_SWING, V_SHATTER, V_HURT,
       V_PICK, V_STEP, V_CLICK, V_WIN, V_WHOOSH,
       V_ROLL, V_JUMP, V_KICK, V_LAND };
/* gl,gr: per-voice stereo gains (set at spawn). lp: this voice's own lowpass
 * state, so overlapping sounds no longer share one filter. */
typedef struct { int type,on,world; float t,p,gl,gr,lp; } Voice;
static Voice voices[MAXVOICE];
static int audioOK=0; static SDL_AudioDeviceID adev;
static volatile float g_ats=1.0f;
static volatile int g_track=0;   /* music: 0 MENU,1=LOBBY,2=SUBWAY,3=TERMINAL,4=OVERLORD */
static volatile int g_mute=0;    /* 'm' toggles: silences output, clocks keep running */
static unsigned arng=0xBADC0DEu;
static float arand(void){ arng^=arng<<13;arng^=arng>>17;arng^=arng<<5; return (arng&0xffffff)/(float)0x800000-1.0f; }
static float px,pz,pyaw,pvx,pvz; /* player pose+velocity; described with the game state below */

/* Fire a voice with its stereo gains already baked. `on` is written LAST: the
 * audio thread polls that flag, so every other field must be settled before the
 * slot goes live or the callback can mix one buffer of a half-built voice with
 * the previous occupant's pan. */
static void emit_voice(int type,float pitch,float gl,float gr){
  /* player-time voices: UI plus your own body — they never pitch-bend
   * with the frozen world, because you always move in real time */
  int world = !(type==V_CLICK||type==V_WIN||type==V_PICK||type==V_STEP
              ||type==V_ROLL||type==V_JUMP||type==V_KICK||type==V_LAND);
  SDL_LockAudioDevice(adev);   /* cheap: taken once per one-shot, not per sample */
  for(int i=0;i<MAXVOICE;i++) if(!voices[i].on){
    voices[i].type=type; voices[i].t=0; voices[i].p=pitch; voices[i].world=world;
    voices[i].lp=0; voices[i].gl=gl; voices[i].gr=gr;
    voices[i].on=1; break; }
  SDL_UnlockAudioDevice(adev);
}
static void sfxp(int type,float pitch){ if(audioOK) emit_voice(type,pitch,1.0f,1.0f); }
static void sfx(int type){ sfxp(type,1.0f); }
/* positional one-shot: pan + gentle distance rolloff from the camera. Computed
 * here on the game thread, so the callback just reads the baked gl/gr. */
static void sfx3(int type,float pitch,float x,float y,float z){
  (void)y; if(!audioOK)return;
  float yr=pyaw*PI/180.0f, dx=x-px, dz=z-pz;
  float dist=sqrtf(dx*dx+dz*dz);
  float pan = dist>0.001f ? (dx*cosf(yr)+dz*sinf(yr))/dist : 0; /* +1 = hard right */
  float g   = 1.0f/(1.0f+0.10f*dist);                          /* unity up close  */
  /* your own motion Doppler-shifts the world's sounds — the same physics the
   * renderer paints. Closing on a source pitches it up a touch. */
  if(dist>0.001f){
    float vr=-(pvx*dx+pvz*dz)/dist;
    pitch*=clampf(1.0f-vr*0.012f,0.8f,1.25f);
  }
  /* equal-power pan: the linear law dipped ~3dB dead-centre and snapped at
   * the edges; cos/sin keeps perceived loudness constant across the arc */
  float th=(pan+1.0f)*(PI*0.25f);
  emit_voice(type,pitch, g*cosf(th), g*sinf(th));
}

/* ---------------------------------------------------------------- music
 * Per-level driving trance, fully synthesized in the audio thread: a 16th-
 * note step sequencer (kick / hats / bass / supersaw lead / pad). It keeps
 * its own real-time clock and never detunes with the world (only SFX do), so
 * the groove stays steady. g_track picks the track; MENU is lighter. */
/* Anti-aliased sawtooth (2-point PolyBLEP). t and the per-sample phase
 * increment dt are in cycles [0,1); the blep rounds the wrap discontinuity so
 * high notes lose their harsh digital aliasing whine. */
static float psaw(float t,float dt){
  float v=2.0f*t-1.0f;
  if(dt>0.0f){
    if(t<dt){ float x=t/dt; v-=x+x-x*x-1.0f; }
    else if(t>1.0f-dt){ float x=(t-1.0f)/dt; v-=x*x+x+x+1.0f; }
  }
  return v;
}
/* 2^(semi/12) lookup so the per-sample oscillators never call powf. Filled by
 * music_init() for semi in [-48,48]; out-of-range notes clamp to the ends. */
static float semitab[97];
static float ntof(float root,int semi){ semi+=48; if(semi<0)semi=0; else if(semi>96)semi=96; return root*semitab[semi]; }

/* ------------------------------------------------------ note-string framework
 * Melodies and chord progressions are written as readable space-separated note
 * strings ("G1 G1 G2 D#2 ...") and parsed once at startup into semitone-offset
 * step arrays. Tokens: a note (A-G, optional #/b, scientific octave, A1=55Hz),
 * "." for a rest, and a "*N" suffix that repeats the previous step N sixteenths
 * (so a held note like "B1*12" re-plucks across its steps — the driving pulse). */
static int notemidi(const char*s){          /* "A#2" -> MIDI number (A1=33) */
  int pc; switch(s[0]){case 'C':pc=0;break;case 'D':pc=2;break;case 'E':pc=4;break;
    case 'F':pc=5;break;case 'G':pc=7;break;case 'A':pc=9;break;case 'B':pc=11;break;
    default:pc=0;} int i=1;
  if(s[i]=='#'){pc++;i++;} else if(s[i]=='b'){pc--;i++;}
  int neg=0; if(s[i]=='-'){neg=1;i++;} int oct=0;
  while(s[i]>='0'&&s[i]<='9'){oct=oct*10+(s[i]-'0');i++;}
  return ((neg?-oct:oct)+1)*12+pc;
}
static int parse_mel(const char*str,const char*rootname,int*out,int max){
  int root=notemidi(rootname),n=0; const char*p=str;
  while(*p){
    while(*p==' ')p++; if(!*p)break;
    int val;
    if(*p=='.'){ val=-100; p++; }                 /* rest */
    else { val=notemidi(p)-root; p++;             /* note -> offset from root */
      if(*p=='#'||*p=='b')p++; if(*p=='-')p++;
      while(*p>='0'&&*p<='9')p++; }
    int rep=1;
    if(*p=='*'){ p++; rep=0; while(*p>='0'&&*p<='9')rep=rep*10+(*p++-'0'); if(rep<1)rep=1; }
    while(rep-->0&&n<max) out[n++]=val;
  }
  return n;
}

/* bass = per-quarter; semitone offsets from the track root */
static const int bass_menu[]  ={0,0,-5,-5};
static const int bass_lobby[] ={0,0,0,-2, -5,-5,3,3};
static const int bass_subway[]={0,0,-5,3, -7,-7,0,0};
static const int bass_term[]  ={0,-2,-5,-7, 0,3,5,7};

/* One track table. root names the tonic (sets synth frequency and the parse
 * origin for mel/prog). mel = lead step string; div = sixteenths per melody note
 * (1 = one note per 16th; 2 = half-speed 8th-note lead). prog = chord-root string
 * (NULL = static key, no sidechain pump) advancing every proglen sixteenths. The
 * lead stays fixed over a moving prog, so boss chords sweep underneath the riff.
 * svf routes the lead through the resonant filtered-saw voice (the boss). */
typedef struct { const char*root; float bpm; const char*mel; int div; const char*prog;
                 int proglen; const int*bass; int bassn; int full; int svf; } Track;
static const Track TRACKS[5]={
  /* MENU  : ambient title pulse                 */ {"A1", 110.0f, "A1 E2 A2 E2 C2 E2 G2 E2", 1, 0, 16, bass_menu, 4, 0,0},
  /* LOBBY : dark G riff over an i-VI-III-VII pump*/ {"G1", 118.0f, "G1 G1 G1 G2 D2 D#2 D#2 D#2 D#2 D2", 2, "G1 D#1 A#1 F1", 16, bass_lobby, 8, 1,0},
  /* SUBWAY: A# octave / fifth / b9 driver        */ {"A#1",114.0f, "A#2 F2 A#1 B1 B1 B1 A#2 F2 A#2 B1 A#2 B1 A#2", 2, 0, 16, bass_subway, 8, 1,0},
  /* TERM  : pulsing B techno line                */ {"B1", 122.0f, "B1*12 E2*7 D2*7 A1 B1*12 D2 B1*12", 1, 0, 16, bass_term, 8, 1,0},
  /* OVERLORD: Daft-Punk lead riff over Em-D-A-C   */ {"E1", 120.0f, ".*6 G1*2 F#1*4 G1*2 A1*4 G1*4 F#1*4 G1*4 B1*2", 1, "E1 D1 A1 C1", 8, bass_term, 8, 1,1},
};

/* parsed melodies / progressions + tonic frequency, filled once by music_init() */
#define MELMAX 64
static int   mel_buf[5][MELMAX], mel_n[5];
static int   prog_buf[5][16],    prog_n[5];
static float track_hz[5];
static void music_init(void){
  for(int k=-48;k<=48;k++) semitab[k+48]=powf(2.0f,k/12.0f);
  for(int i=0;i<5;i++){
    mel_n[i]  = TRACKS[i].mel  ? parse_mel(TRACKS[i].mel ,TRACKS[i].root, mel_buf[i], MELMAX) : 0;
    prog_n[i] = TRACKS[i].prog ? parse_mel(TRACKS[i].prog,TRACKS[i].root, prog_buf[i], 16)    : 0;
    track_hz[i] = 440.0f*powf(2.0f,(notemidi(TRACKS[i].root)-69)/12.0f);
  }
}

static float music_sample(double mt,int track){
  if(track<0)track=0; if(track>4)track=4;
  const Track*T=&TRACKS[track];
  static double pmt=0; float dt=(float)(mt-pmt); pmt=mt;
  if(dt<0)dt=0; if(dt>0.05f)dt=0;            /* guard first call / track switch */

  double bps=T->bpm/60.0;
  double s16=mt*bps*4.0;                     /* sixteenth-note position         */
  long step=(long)s16;
  float in16=(float)(s16-(double)step)/(float)(bps*4.0); /* sec into this 16th  */
  float beatpos=(float)(mt*bps-floor(mt*bps));           /* 0..1 within quarter */
  float secInBeat=beatpos/(float)bps;                    /* sec into the beat   */
  float root=track_hz[track], out=0;

  /* progression: chord root moves every proglen 16ths; the kick ducks the
   * pad/bass via a sidechain envelope, giving the "pumping" breath. */
  int prog_on=prog_n[track]>0;
  int plen=T->proglen>0?T->proglen:16;
  int poff=prog_on ? prog_buf[track][(int)(step/plen)%prog_n[track]] : 0;
  float sc=prog_on ? 0.28f+0.72f*(1.0f-expf(-secInBeat*7.0f)) : 1.0f;

  /* pad: sustained open chord (root/5/oct over a prog, minor triad otherwise).
   * Oscillator phases live in cycles [0,1) so psaw needs no 2*PI scaling. */
  static float pp0=0,pp1=0,pp2=0;
  int o0=12, o1=prog_on?19:15, o2=prog_on?24:19;
  float d0=ntof(root,poff+o0)*dt, d1=ntof(root,poff+o1)*dt, d2=ntof(root,poff+o2)*dt;
  pp0+=d0; pp0-=floorf(pp0); pp1+=d1; pp1-=floorf(pp1); pp2+=d2; pp2-=floorf(pp2);
  float padlfo=0.6f+0.4f*sinf((float)mt*0.4f);
  out += (psaw(pp0,d0)*0.5f+psaw(pp1,d1)*0.4f+psaw(pp2,d2)*0.4f)
         *(prog_on?0.085f:0.05f)*padlfo*sc;

  /* bass: prog tracks the chord root (steady, sidechained); else the per-beat
   * pattern with an 8th-note pump. Warm one-pole lowpass either way. */
  static float pb=0, lpb=0;
  int bidx=(int)(((long)(mt*bps))%T->bassn); if(bidx<0)bidx+=T->bassn;
  int bsemi=prog_on ? poff : T->bass[bidx];
  float db=ntof(root,bsemi)*dt;
  pb+=db; pb-=floorf(pb);
  float bgate=prog_on ? sc : expf(-fmodf(secInBeat*(float)bps*2.0f,1.0f)*5.0f);
  lpb += 0.25f*(psaw(pb,db)*0.5f-lpb);
  out += lpb*bgate*(prog_on?0.34f:0.32f);

  /* lead: one detuned supersaw voice reading the parsed melody, div sixteenths
   * per note (so a higher div is a slower lead). The plain path is an LFO-swept
   * lowpass pluck; T->svf routes it through a resonant 2-pole filter plucked
   * open per note and sidechain-pumped — the boss "house" lead. */
  static float pa0=0,pa1=0,pa2=0, lpa=0, svf_lo=0, svf_bp=0;
  int div=T->div>0?T->div:1;
  double sNote=s16/(double)div; long lstep=(long)sNote;
  float inNote=(float)(sNote-(double)lstep)*(float)div/(float)(bps*4.0); /* sec into note */
  int aidx=mel_n[track]?(int)(lstep%mel_n[track]):0; if(aidx<0)aidx+=mel_n[track];
  int asemi=mel_n[track]?mel_buf[track][aidx]:-100;
  if(asemi>-50){
    float aatk=inNote<0.004f?inNote/0.004f:1.0f;
    float da=ntof(root,asemi+12)*dt, da0=da*0.994f, da2=da*1.007f;
    pa0+=da0; pa0-=floorf(pa0); pa1+=da; pa1-=floorf(pa1); pa2+=da2; pa2-=floorf(pa2);
    float saws=(psaw(pa0,da0)+psaw(pa1,da)+psaw(pa2,da2))*0.33f;
    if(T->svf){
      /* fat detuned saws → gentle grit → resonant lowpass whose cutoff is
       * plucked open per note and slow-swept by an LFO, then sidechain-pumped
       * against the four-on-the-floor kick (Benny-Benassi "Satisfaction" lead). */
      float drv=tanhf(saws*1.7f)*(expf(-inNote*4.0f)*aatk);
      float fenv=expf(-inNote*7.0f);
      float fc=(220.0f+fenv*2800.0f)*(0.70f+0.45f*(0.5f+0.5f*sinf((float)mt*0.5f)));
      if(fc>7000.0f)fc=7000.0f;
      float fco=2.0f*sinf(PI*fc/44100.0f), res=1.05f;  /* res<2; lower = more squelch */
      float hp=drv-svf_lo-res*svf_bp; svf_bp+=fco*hp; svf_lo+=fco*svf_bp;
      float pump=0.30f+0.70f*(1.0f-expf(-secInBeat*9.0f));
      out += svf_lo*pump*0.45f;
    } else {
      float cut=0.08f+0.20f*(0.5f+0.5f*sinf((float)mt*1.3f));
      float aenv=expf(-inNote*8.0f);                   /* tight staccato pluck */
      lpa += cut*(saws-lpa);
      out += (lpa*0.7f+saws*0.3f)*aenv*aatk*(T->full?0.34f:0.22f);
    }
  }

  if(T->full){
    /* kick: four-on-the-floor, pitch-dropping sine + click */
    float kt=secInBeat;
    float kph=2*PI*(45.0f*kt+(85.0f/35.0f)*(1.0f-expf(-kt*35.0f)));
    out += sinf(kph)*expf(-kt*8.0f)*0.85f;
    out += (kt<0.006f?(1.0f-kt/0.006f):0)*0.25f;
    /* hats: noise per 16th, accented/open on offbeats */
    float hdec=(step&1)?55.0f:120.0f, hamp=(step&1)?0.16f:0.09f;
    out += arand()*expf(-in16*hdec)*hamp;
  } else {
    /* menu: a soft heartbeat pulse instead of a hard kick */
    out += sinf(2*PI*60.0f*secInBeat)*expf(-secInBeat*6.0f)*0.22f;
  }
  return out;
}

static void audio_cb(void*ud,Uint8*stream,int len){
  (void)ud;
  float*out=(float*)stream; int n=len/8;   /* stereo: 2 floats per frame */
  static double mt=0;
  float ats=g_ats, mg=g_mute?0.0f:1.0f;
  for(int i=0;i<n;i++){
    /* per-level club track (replaces the old drone). Runs at a constant
     * tempo regardless of the world timescale — only the SFX below detune
     * with ats, so the groove stays steady while you move. Music is centered;
     * each voice pans into sL/sR by its baked gl/gr. */
    mt += 1.0/44100.0;
    float m=music_sample(mt,g_track)*0.85f;  /* headroom: stop pumping the SFX */
    float sL=m, sR=m;
    for(int v=0;v<MAXVOICE;v++){
      if(!voices[v].on)continue;
      float t=voices[v].t, p=voices[v].p, vs=0, lp=voices[v].lp;
      float atk=t<0.005f?t/0.005f:1.0f;   /* 5ms fade-in kills click transients */
      switch(voices[v].type){
        case V_SHOT:{ float nz=arand()*expf(-t*30)*0.30f*atk;
          float f=(170.0f-t*460.0f)*p; if(f<35)f=35;
          vs+=nz+sinf(2*PI*f*t)*expf(-t*15)*0.70f;
          if(t>0.45f)voices[v].on=0; }break;
        case V_ESHOT:{ float nz=arand()*expf(-t*24)*0.25f*atk;
          float f=(95.0f-t*180.0f)*p; if(f<28)f=28;
          vs+=nz+sinf(2*PI*f*t)*expf(-t*10)*0.6f;
          if(t>0.55f)voices[v].on=0; }break;
        case V_DEFLECT:{ /* metallic ping: inharmonic partials, softened top */
          vs+=(sinf(2*PI*1318*p*t)+0.6f*sinf(2*PI*2093*p*t)+0.25f*sinf(2*PI*2960*p*t))
             *expf(-t*9)*0.18f + arand()*expf(-t*70)*0.10f*atk;
          if(t>0.6f)voices[v].on=0; }break;
        case V_SWING:{ lp+=(0.04f+0.30f*t)*(arand()-lp);
          vs+=lp*sinf(1+t*40)*expf(-t*8)*1.2f;
          if(t>0.35f)voices[v].on=0; }break;
        case V_SHATTER:{ lp+=0.30f*(arand()-lp);   /* lowpassed hiss, octave down */
          vs+=lp*expf(-t*14)*0.22f*atk
            +(sinf(2*PI*1318*t)+sinf(2*PI*1760*t*1.013f))*expf(-t*12)*0.12f;
          if(t>0.8f)voices[v].on=0; }break;
        case V_HURT:{ /* warm descending thud: pure tone + sub octave, no buzz */
          float f=(180.0f-t*230.0f)*p; if(f<70)f=70;
          float env=expf(-t*7.0f)*atk;
          vs+=(sinf(2*PI*f*t)*0.5f + sinf(2*PI*f*0.5f*t)*0.3f)*env;
          if(t>0.4f)voices[v].on=0; }break;
        case V_PICK:{ /* phase-continuous: the raw f*t step clicked at 90ms */
          float phc = t<0.09f ? 660*t : 660*0.09f+990*(t-0.09f);
          vs+=sinf(2*PI*phc)*expf(-t*9)*0.28f;
          if(t>0.3f)voices[v].on=0; }break;
        case V_STEP: lp+=0.22f*(arand()-lp); vs+=lp*expf(-t*70)*0.8f;
          if(t>0.08f)voices[v].on=0; break;
        case V_CLICK: vs+=sinf(2*PI*1500*t)*expf(-t*170)*0.25f;
          if(t>0.05f)voices[v].on=0; break;
        case V_WIN:{ /* phase-continuous fanfare — each note used to click in */
          float phc = t<0.16f? 262*t
                    : t<0.32f? 262*0.16f+392*(t-0.16f)
                    : t<0.48f? (262+392)*0.16f+523*(t-0.32f)
                    :          (262+392+523)*0.16f+784*(t-0.48f);
          vs+=sinf(2*PI*phc)*expf(-(t>0.48f?(t-0.48f)*3:0))*0.26f;
          if(t>1.4f)voices[v].on=0; }break;
        case V_WHOOSH:{ lp+=(0.5f-0.4f*t)*(arand()-lp);   /* passing bullet */
          vs+=lp*0.9f*expf(-t*6)*p;
          if(t>0.5f)voices[v].on=0; }break;
        case V_ROLL:{ /* the dodge: a fast cloth-and-air whoosh */
          lp+=(0.08f+0.45f*t)*(arand()-lp);
          float env=1.0f-t*2.5f; if(env<0)env=0;
          vs+=lp*env*1.1f;
          if(t>0.4f)voices[v].on=0; }break;
        case V_JUMP:{ float f=(260.0f+t*1700.0f)*p;
          vs+=sinf(2*PI*f*t)*expf(-t*18)*0.30f + arand()*expf(-t*40)*0.15f;
          if(t>0.25f)voices[v].on=0; }break;
        case V_KICK:{ float f=(120.0f-t*160.0f)*p; if(f<40)f=40;
          vs+=sinf(2*PI*f*t)*expf(-t*14)*0.5f + arand()*expf(-t*50)*0.25f;
          if(t>0.3f)voices[v].on=0; }break;
        case V_LAND:{ vs+=sinf(2*PI*55.0f*t)*expf(-t*18)*0.55f*p
            + arand()*expf(-t*35)*0.2f;
          if(t>0.3f)voices[v].on=0; }break;
      }
      voices[v].lp=lp;
      sL += vs*voices[v].gl; sR += vs*voices[v].gr;
      voices[v].t += (voices[v].world?ats:1.0f)/44100.0f;
    }
    out[2*i]   = tanhf(sL*1.1f)*0.82f*mg;
    out[2*i+1] = tanhf(sR*1.1f)*0.82f*mg;
  }
}

/* ---------------------------------------------------------------- shaders
 * One program for everything lit. uRain paints procedural digital rain as
 * emissive emerald streaks (walls only). uGloss drives the specular that
 * sells "black reflective". uAlpha lets the floor blend over the mirrored
 * reflection pass. Fog folds everything into green-black murk. */
static GLuint prog;
static GLint uCam,uNL,uLpos,uLcol,uM3,uT,uTint,uBump,uEmis,uAlb,uNrm,
             uTime,uRain,uGloss,uAlpha,uFog,uRim,uRimCol,uTonemap,uEmisM,uNSc;
static const char*VS=
"#version 120\n"
"uniform mat3 uM3; uniform vec3 uT; uniform vec3 uNS;\n"
"varying vec3 vP; varying vec3 vN; varying vec2 vUV; varying float vAO;\n"
"void main(){\n"
"  vec3 wp = uM3*gl_Vertex.xyz + uT;\n"
"  vAO = gl_MultiTexCoord0.z;\n"
/* uNS = the non-uniform m3scl actually folded into uM3 (torso slabs, the boss's
 * breathing thorax). For uM3 = R*S the normal matrix is R*S^-1, so we need
 * uM3 * (n/S^2) = R*S*S^-2*n = R*S^-1*n — hence the SQUARE. Callers used to pass
 * 1/s and the shader multiplied, giving R*S*(1/s) = R: that cancelled the squash
 * instead of inverting it, so every slab was lit as an unsquashed cylinder.
 * Passing the plain scale here is the version that cannot be got wrong. */
"  vP=wp; vN=uM3*(gl_Normal/(uNS*uNS)); vUV=gl_MultiTexCoord0.xy;\n"
"  gl_Position = gl_ModelViewProjectionMatrix * vec4(wp,1.0);\n"
"}\n";
static const char*FS=
"#version 120\n"
"uniform sampler2D uAlb; uniform sampler2D uNrm;\n"
"uniform vec3 uCam; uniform int uNL;\n"
"uniform vec4 uLpos[8]; uniform vec3 uLcol[8];\n"
"uniform vec3 uTint; uniform float uBump; uniform float uEmis;\n"
"uniform float uTime; uniform float uRain; uniform float uGloss; uniform float uAlpha;\n"
"uniform float uEmisMask;\n"
"uniform vec3 uFog;\n"
"uniform float uRim; uniform vec3 uRimCol; uniform float uTonemap;\n"
"varying vec3 vP; varying vec3 vN; varying vec2 vUV; varying float vAO;\n"
"float h1(float x){ return fract(sin(x*127.1)*43758.5453); }\n"
"void main(){\n"
"  vec4 albS = texture2D(uAlb,vUV);\n"
"  vec3 base = albS.rgb * uTint;\n"
/* per-tile brightness jitter (uv = worldpos*0.5, so floor(vUV) is the 2m cell
 * id): breaks the wallpaper repeat that a single 256px texture tiled to the
 * horizon otherwise shows. World surfaces only — figures run with uBump=0. */
"  if(uBump>0.5) base *= 0.8231 + 0.36*h1(dot(floor(vUV),vec2(7.31,13.17)));\n"
"  vec3 N = normalize(vN);\n"
"  if(uBump>0.5){\n"
"    vec3 T = (abs(N.y)>0.5)? vec3(1.0,0.0,0.0) : vec3(N.z,0.0,-N.x);\n"
"    vec3 B = cross(N,T);\n"
"    vec3 tn = texture2D(uNrm,vUV).xyz*2.0-1.0;\n"
"    N = normalize(T*tn.x + B*tn.y + N*tn.z);\n"
"  }\n"
"  vec3 V = normalize(uCam - vP);\n"
"  float NdV = max(dot(N,V),0.0);\n"
/* Hemisphere ambient instead of a flat 5% floor: up-facing planes catch a
 * little more than down-facing ones, so the facet planes of a standing figure
 * separate even in a corner no light reaches. */
"  float aoF = 1.0-vAO;\n"
"  vec3 col = base*(0.032 + 0.048*(N.y*0.5+0.5))*aoF;\n"
/* GGX specular. The old Blinn-Phong exponent gave every surface the same
 * plastic dot; a real microfacet lobe plus Schlick Fresnel is what makes the
 * obsidian floor and the cut crystal limbs look like different materials. */
"  float rough = mix(0.62,0.13,uGloss);\n"
"  float al = rough*rough; float a2 = al*al;\n"
"  float kv = al*0.5;\n"
"  float kV = NdV*(1.0-kv)+kv;\n"
"  for(int i=0;i<8;i++){ if(i>=uNL)break;\n"
"    vec3 Ld = uLpos[i].xyz - vP;\n"
"    float d = length(Ld); Ld/=d;\n"
"    float a = max(0.0, 1.0 - d/uLpos[i].w); a*=a;\n"
/* most fragments are in range of none of the lights (measured: 70% of the
 * frame in the lobby), and a==0 contributes exactly +0.0 below — skip the
 * whole microfacet evaluation for them */
"    if(a<=0.0) continue;\n"
"    float NdL = max(dot(N,Ld),0.0);\n"
"    vec3 H = normalize(Ld+V);\n"
"    float NdH = max(dot(N,H),0.0);\n"
"    float dn = NdH*NdH*(a2-1.0)+1.0;\n"
"    float D = a2/(3.14159265*dn*dn);\n"
"    float Vs = 0.25/max((NdL*(1.0-kv)+kv)*kV,1e-3);\n"
"    float F = 0.04+0.96*pow(1.0-max(dot(H,V),0.0),5.0);\n"
"    float spec = min(D*Vs*F*NdL, 8.0)*(0.35+1.10*uGloss);\n"
/* One `a`, not two. The specular term used to sit INSIDE a second attenuation
 * multiply, so the highlight fell off as the square of the diffuse rate — which
 * is why the "glossy obsidian" floor and the cut-crystal limbs read as flat matte
 * black at any distance from an emitter. Diffuse and specular are lit by the same
 * light and take the same falloff. */
"    col += uLcol[i]*a*(base*NdL*aoF + vec3(spec));\n"
"  }\n"
/* Grazing-angle environment sheen. There is no cubemap and never will be, but
 * black obsidian only reads as polished if something brightens at the horizon
 * — this is that reflection, faked from a fixed emerald-teal sky tint. */
"  col += vec3(0.020,0.052,0.040)*pow(1.0-NdV,5.0)*uGloss;\n"
"  if(uRain>0.5){\n"
/* Digital rain. This is the wall's whole identity now that the circuitry is
 * gone, so it earns some real structure:
 *   - a 3x5 sub-cell bit pattern, so each cell is a blocky GLYPH rather than a
 *     solid lit square. This is the single change that makes it read as falling
 *     characters instead of falling confetti.
 *   - a white-hot leading cell with an emerald tail behind it, which is the one
 *     detail everybody actually remembers about the effect.
 *   - a softer tail exponent (6 rather than 10) so columns are long enough to
 *     overlap and the wall reads as a sheet rather than as dots.
 *   - more live columns, and per-column glyph churn rates, so neighbouring
 *     columns never scramble in lockstep. */
/* Cell size is chosen in WORLD units, not UV units. vUV is worldpos*0.5, so
 * vUV.x*9 is one column every ~22cm and vUV.y*12 one glyph every ~17cm — about
 * head-sized, which is what makes them read as characters. The previous 16x34
 * put cells at 6cm, and subdividing those 3x5 for the block font landed the
 * actual features at ~1cm, where they just alias into green noise. */
"    float cx  = floor(vUV.x*9.0);\n"
/* Density. Pulled a long way back: at 0.46 well over half the columns were live
 * and every surface in the room was raining, which stopped reading as a wall
 * with data on it and started reading as a screensaver. 0.80 leaves roughly one
 * column in five, so most of the wall is just wall and the rain is an accent you
 * notice rather than a texture you look through. */
"    float on  = step(0.80, h1(cx*3.7+11.0));\n"
"    float spd = 0.05 + 0.16*h1(cx*1.3);\n"
"    float head = fract(uTime*spd + h1(cx*7.7));\n"
"    float d2  = fract(head + vUV.y*0.34);\n"
"    float tail = pow(1.0-d2, 11.0);\n"      /* shorter streaks, not sheets */
"    float row = floor(vUV.y*12.0);\n"
/* the glyph: a 3x5 block font hashed per (column,row,tick). churn is per-column
 * so the sheet shimmers unevenly, the way the real thing does. */
"    float churn = floor(uTime*(7.0+9.0*h1(cx*2.3))) * 3.0;\n"
"    vec2  sub = floor(fract(vec2(vUV.x*9.0, vUV.y*12.0))*vec2(3.0,5.0));\n"
"    float bit = step(0.44, h1(cx*91.0 + row*17.0 + (sub.x+sub.y*3.0)*7.31 + churn));\n"
/* leading cell burns white; everything behind it is emerald */
"    float lead = smoothstep(0.90,1.0,1.0-d2);\n"
"    vec3  rc = mix(vec3(0.10,1.00,0.42), vec3(0.85,1.00,0.92), lead);\n"
"    col += rc*tail*bit*on*(0.13+0.17*h1(cx*5.1))*(1.0+1.5*lead);\n"
"  }\n"
/* uEmis is a lerp toward unlit albedo, and several callers stack a flash on top
 * of an already-high base (a locked-on agent reaches ~1.6). Left unclamped that
 * extrapolates PAST the albedo and subtracts the lit term, which the old
 * clipping tonemap hid as plain white. Clamp it to the 0..1 it was meant to be. */
"  col = mix(col, base, clamp(uEmis,0.0,1.0));\n"
/* the rim lives AFTER the emissive mix: it used to be scaled by (1-uEmis),
 * erasing the lock-on red edge and the boss glow exactly when they mattered */
"  if(uRim>0.0){\n"
"    /* crystalline fresnel edge: glanced facets glow, fronts stay dark */\n"
"    float fres = pow(1.0 - clamp(dot(N,V),0.0,1.0), 3.0);\n"
"    col += uRimCol*fres*uRim;\n"
"  }\n"
/* masked emissive (albedo alpha): ceiling light slots, wall circuit traces
 * and floor seams become real emitters that feed the bloom pass */
"  col += base*(albS.a*uEmisMask);\n"
/* Exponential-squared distance fog. The old linear ramp put a visible band
 * where it clamped; this one never fully saturates, so the far wall keeps a
 * trace of its own colour and the murk reads as depth rather than a curtain. */
"  float fz = max(distance(uCam,vP)-5.0, 0.0) * 0.042;\n"
"  float fd = 1.0 - exp(-fz*fz);\n"
"  col = mix(col, uFog, fd);\n"
/* Normally we output LINEAR HDR into a float target: no clamp, no tonemap, no
 * gamma. Emissives (eyes, trims, the charged beam) stay above 1.0 so the bloom
 * pass has real energy to work with, and the composite does the tonemap once.
 * uTonemap>0 is the no-FBO fallback: nothing downstream will grade the frame,
 * so fold the same ACES curve and transfer function in here instead. */
"  col = max(col,0.0);\n"
"  if(uTonemap>0.5){\n"
"    col *= 1.15;\n"
"    col = clamp((col*(2.51*col+0.03))/(col*(2.43*col+0.59)+0.14),0.0,1.0);\n"
"    col = pow(col, vec3(1.0/2.2));\n"
"    col += (h1(gl_FragCoord.x*3.137+gl_FragCoord.y*7.913)-0.5)*(1.0/255.0);\n"
"  }\n"
"  gl_FragColor = vec4(col,uAlpha);\n"
"}\n";

static GLuint shader(GLenum ty,const char*src){
  GLuint s=glCreateShader(ty);
  glShaderSource(s,1,&src,0); glCompileShader(s);
  GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
  if(!ok){ char log[2048]; glGetShaderInfoLog(s,2048,0,log);
    fprintf(stderr,"[dilation] shader fail:\n%s\n",log); exit(1); }
  return s;
}
static GLuint mkprog(const char*vs,const char*fs){
  GLuint p=glCreateProgram();
  glAttachShader(p,shader(GL_VERTEX_SHADER,vs));
  glAttachShader(p,shader(GL_FRAGMENT_SHADER,fs));
  glLinkProgram(p);
  GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok){ char log[2048]; glGetProgramInfoLog(p,2048,0,log);
    fprintf(stderr,"[dilation] link fail:\n%s\n",log); exit(1); }
  return p;
}
static void init_shaders(void){
  prog=mkprog(VS,FS);
  uCam=glGetUniformLocation(prog,"uCam");   uNL =glGetUniformLocation(prog,"uNL");
  uLpos=glGetUniformLocation(prog,"uLpos[0]"); uLcol=glGetUniformLocation(prog,"uLcol[0]");
  uM3 =glGetUniformLocation(prog,"uM3");    uT  =glGetUniformLocation(prog,"uT");
  uTint=glGetUniformLocation(prog,"uTint"); uBump=glGetUniformLocation(prog,"uBump");
  uEmis=glGetUniformLocation(prog,"uEmis");
  uAlb=glGetUniformLocation(prog,"uAlb");   uNrm=glGetUniformLocation(prog,"uNrm");
  uTime=glGetUniformLocation(prog,"uTime"); uRain=glGetUniformLocation(prog,"uRain");
  uGloss=glGetUniformLocation(prog,"uGloss"); uAlpha=glGetUniformLocation(prog,"uAlpha");
  uFog=glGetUniformLocation(prog,"uFog");
  uRim=glGetUniformLocation(prog,"uRim"); uRimCol=glGetUniformLocation(prog,"uRimCol");
  uTonemap=glGetUniformLocation(prog,"uTonemap");
  uEmisM=glGetUniformLocation(prog,"uEmisMask");
  uNSc=glGetUniformLocation(prog,"uNS");
}

/* ---------------------------------------------------------------- post stack
 * The scene renders into a multisampled RGBA16F target, resolves to a float
 * texture, and goes through bright-pass -> 3-octave separable Gaussian ->
 * composite. That last pass is where the frame actually becomes an image:
 * bloom, ACES tonemap, chromatic aberration, the time-dilation grade, vignette,
 * scanlines, grain and dither, all in one dependent-texture-free shader.
 *
 * This replaces the old per-emitter billboard "fake bloom": glow now falls out
 * of whatever is genuinely bright, so a charged beam, a lit trim and a locked
 * agent all bleed correctly without anyone hand-registering a glow point.
 *
 * Every stage degrades: no float target -> RGBA8, no multisample -> plain,
 * no FBOs at all -> postOK=0 and the game draws straight to the window. */
#define NBLOOM 3
/* ------------------------------------------------------------ quality tiers
 * On the machine this was written on the whole frame costs well under a
 * millisecond of CPU and the GPU never breaks a sweat. On an integrated part
 * the picture inverts completely: the multisampled RGBA16F scene target and the
 * three-octave separable blur are almost the entire frame, and they scale with
 * PIXELS, not with anything the game is doing.
 *
 * So the post chain is now tiered. Ordered so the cheapest thing to lose goes
 * first and the silhouette — which is the whole art direction — goes last:
 *   bloom octaves   the widest octave is a 1/8-res haze almost nobody can name
 *   HDR             RGBA8 halves scene bandwidth; emitters clip, bloom dulls
 *   scene scale     softens everything, but keeps the HUD and the edges crisp
 *   MSAA            dropped LAST: hard bright edges on black is the look
 * `lights` also clamps the fragment shader's light loop, which is the one
 * genuinely expensive thing in the world shader. */
typedef struct { int msaa,hdr,bloom,lights; float scale; const char*name; } Quality;
static const Quality QUAL[3]={
  { 0, 0, 1, 4, 0.70f, "LOW"    },
  { 2, 1, 2, 6, 1.00f, "MEDIUM" },
  { 4, 1, 3, 8, 1.00f, "HIGH"   },
};
static int qual=2;          /* index into QUAL; --quality overrides            */
static int qualAuto=1;      /* auto tier: step down when we cannot hold 60fps  */
static int postOK=0, postMS=0;
static GLuint fboMS, rbColMS, rbDepMS;      /* multisampled scene target */
static GLuint fboScene, texScene, rbDepth;  /* resolved (or direct) scene   */
static GLuint bloomFbo[NBLOOM][2], bloomTex[NBLOOM][2];
static int bloomW[NBLOOM], bloomH[NBLOOM];
/* SCENE resolution, which is fbW/fbH scaled by renderScale. The world renders
 * here; the composite upscales to the real framebuffer on its way to the window.
 * On anything up to 1440p these are just fbW/fbH. */
static int scW=HUDW, scH=HUDH;
static GLuint progBright, progBlur, progComp;
static GLint  bSrc,bTexel,bThresh,bKnee;
static GLint  lSrc,lDir;
static GLint  cScene,cB0,cB1,cB2,cTime,cTs,cDmg,cExp,cBloom,cBW;

static const char*PVS=
"#version 120\n"
"varying vec2 vUV;\n"
"void main(){ vUV=gl_MultiTexCoord0.xy; gl_Position=gl_Vertex; }\n";

/* bright pass doubles as the downsampler: with uThresh=uKnee=0 the weight
 * collapses to 1 and it is just a 4-tap box filter. */
static const char*BRIGHTFS=
"#version 120\n"
"uniform sampler2D uSrc; uniform vec2 uTexel;\n"
"uniform float uThresh, uKnee;\n"
"varying vec2 vUV;\n"
"void main(){\n"
"  vec3 c = texture2D(uSrc,vUV+vec2(-1.0,-1.0)*uTexel).rgb\n"
"         + texture2D(uSrc,vUV+vec2( 1.0,-1.0)*uTexel).rgb\n"
"         + texture2D(uSrc,vUV+vec2(-1.0, 1.0)*uTexel).rgb\n"
"         + texture2D(uSrc,vUV+vec2( 1.0, 1.0)*uTexel).rgb;\n"
"  c *= 0.25;\n"
"  float br = max(c.r,max(c.g,c.b));\n"
"  float soft = clamp(br-uThresh+uKnee, 0.0, 2.0*uKnee);\n"
"  soft = soft*soft/(4.0*uKnee+1e-4);\n"
"  float w = max(soft, br-uThresh)/max(br,1e-4);\n"
"  gl_FragColor = vec4(c*w, 1.0);\n"
"}\n";

/* 9-tap Gaussian folded to 5 taps by sampling between texels */
static const char*BLURFS=
"#version 120\n"
"uniform sampler2D uSrc; uniform vec2 uDir;\n"
"varying vec2 vUV;\n"
"void main(){\n"
"  vec3 c = texture2D(uSrc,vUV).rgb*0.2270270270;\n"
"  c += (texture2D(uSrc,vUV+uDir*1.3846153846).rgb\n"
"      + texture2D(uSrc,vUV-uDir*1.3846153846).rgb)*0.3162162162;\n"
"  c += (texture2D(uSrc,vUV+uDir*3.2307692308).rgb\n"
"      + texture2D(uSrc,vUV-uDir*3.2307692308).rgb)*0.0702702703;\n"
"  gl_FragColor = vec4(c,1.0);\n"
"}\n";

static const char*COMPFS=
"#version 120\n"
"uniform sampler2D uScene,uB0,uB1,uB2;\n"
"uniform float uTime,uTs,uDmg,uExp,uBloom;\n"
"uniform vec3 uBW;\n"
"varying vec2 vUV;\n"
"float h1(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }\n"
/* Narkowicz ACES fit: keeps saturation in the shoulder, which matters when the
 * only bright things in frame are pure emerald and pure violet emitters. */
"vec3 aces(vec3 x){\n"
"  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0);\n"
"}\n"
"void main(){\n"
"  vec2 d = vUV-0.5;\n"
"  float r2 = dot(d,d);\n"
/* Lateral chromatic aberration, strongest at the edges. It swells as the world
 * freezes and when you take a hit, so the lens itself reports the timescale. */
"  float ab = (0.0009 + 0.0090*(1.0-uTs) + 0.0130*uDmg) * r2*3.2;\n"
"  vec3 col;\n"
"  col.r = texture2D(uScene,vUV+d*ab).r;\n"
"  col.g = texture2D(uScene,vUV).g;\n"
"  col.b = texture2D(uScene,vUV-d*ab).b;\n"
"  vec3 bl = texture2D(uB0,vUV).rgb*uBW.x\n"
"          + texture2D(uB1,vUV).rgb*uBW.y\n"
/* anamorphic hint: the widest octave sampled through an x-compressed UV
 * stretches ~1.8x horizontally — emitters grow subtle lens streaks */
"          + texture2D(uB2, vec2((vUV.x-0.5)*0.55+0.5, vUV.y)).rgb*uBW.z;\n"
"  col += bl*uBloom;\n"
"  col *= uExp;\n"
/* The time-dilation grade: frozen time drains the colour toward a cold blue
 * cast, real time restores it. Same information the SFX pitch-bend carries,
 * on the channel you are actually looking at. */
"  float lum = dot(col, vec3(0.2126,0.7152,0.0722));\n"
"  vec3 frozen = mix(vec3(lum), col, 0.42) * vec3(0.66,0.92,1.32);\n"
"  col = mix(frozen, col, uTs);\n"
"  col = aces(col);\n"
"  col = pow(col, vec3(1.0/2.2));\n"
/* vignette, then a soft 4px scanline, then grain — order matters: all three
 * are display artefacts and belong after the transfer curve. */
"  col *= mix(1.0, 0.45, smoothstep(0.08,0.66,r2));\n"
"  float sl = 0.5+0.5*cos(gl_FragCoord.y*1.5707963);\n"
"  col *= 1.0 - 0.07*sl*sl;\n"
"  col += (h1(gl_FragCoord.xy + uTime*60.0)-0.5)*0.014;\n"
"  col += (h1(gl_FragCoord.xy*1.7)-0.5)*(1.0/255.0);\n"
"  gl_FragColor = vec4(col,1.0);\n"
"}\n";

/* colour texture for a render target; `hdr` picks RGBA16F over RGBA8 */
static GLuint mkrt(int w,int h,int hdr){
  GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
  glTexImage2D(GL_TEXTURE_2D,0,hdr?GL_RGBA16F:GL_RGBA8,w,h,0,GL_RGBA,GL_FLOAT,0);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  return t;
}
static int attach(GLuint fbo,GLuint tex){
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
}

/* Release every render target. Split out of init_post so a window resize can
 * rebuild the whole chain at the new size — the targets used to be allocated
 * once at a compile-time constant and there was no way to change them. The
 * shader PROGRAMS are deliberately not touched: they are resolution-independent
 * and recompiling them on every drag-resize would stutter badly. */
static void free_post(void){
  if(fboScene){ glDeleteFramebuffers(1,&fboScene); fboScene=0; }
  if(texScene){ glDeleteTextures(1,&texScene); texScene=0; }
  if(rbDepth){ glDeleteRenderbuffers(1,&rbDepth); rbDepth=0; }
  if(fboMS){ glDeleteFramebuffers(1,&fboMS); fboMS=0; }
  if(rbColMS){ glDeleteRenderbuffers(1,&rbColMS); rbColMS=0; }
  if(rbDepMS){ glDeleteRenderbuffers(1,&rbDepMS); rbDepMS=0; }
  for(int i=0;i<NBLOOM;i++)for(int k=0;k<2;k++){
    if(bloomFbo[i][k]){ glDeleteFramebuffers(1,&bloomFbo[i][k]); bloomFbo[i][k]=0; }
    if(bloomTex[i][k]){ glDeleteTextures(1,&bloomTex[i][k]); bloomTex[i][k]=0; }
  }
  postOK=0; postMS=0;
}

static void init_post(int msaa){
  if(!glGenFramebuffers||!glBindFramebuffer||!glFramebufferTexture2D
     ||!glCheckFramebufferStatus||!glGenRenderbuffers){
    printf("[dilation] no FBO support, post-processing off\n"); return; }
  const Quality*Q=&QUAL[qual];
  if(msaa>Q->msaa)msaa=Q->msaa;          /* the tier caps the scene target */
  int hdr = Q->hdr && (SDL_GL_ExtensionSupported("GL_ARB_texture_float")
                    || SDL_GL_ExtensionSupported("GL_APPLE_float_pixels"));

  /* pick the scene resolution: native up to the pixel budget, then scale down by
   * area so a 6K panel costs about what 1440p does. */
  renderScale = Q->scale;
  if((float)fbW*(float)fbH*renderScale*renderScale > (float)PIXEL_BUDGET)
    renderScale = sqrtf((float)PIXEL_BUDGET/((float)fbW*(float)fbH));
  scW=(int)(fbW*renderScale); scH=(int)(fbH*renderScale);
  if(scW<64)scW=64; if(scH<64)scH=64;

  /* resolved scene target */
  glGenFramebuffers(1,&fboScene);
  texScene=mkrt(scW,scH,hdr);
  if(!attach(fboScene,texScene) && hdr){       /* float unsupported after all */
    hdr=0; glDeleteTextures(1,&texScene); texScene=mkrt(scW,scH,0);
    if(!attach(fboScene,texScene)){ printf("[dilation] scene FBO incomplete\n");
      /* attach() leaves the (incomplete) fbo bound and nothing downstream ever
       * rebinds 0 once postOK stays 0 — so bailing out here without unbinding
       * turned the no-post fallback into a black window. Match the sibling
       * bail-outs below. */
      glBindFramebuffer(GL_FRAMEBUFFER,0); return; }
  }
  /* the resolved target needs its own depth only when we render straight into
   * it (no multisample path); attach it unconditionally, it is cheap */
  glGenRenderbuffers(1,&rbDepth);
  glBindRenderbuffer(GL_RENDERBUFFER,rbDepth);
  glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,scW,scH);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,rbDepth);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
    printf("[dilation] scene FBO incomplete\n"); glBindFramebuffer(GL_FRAMEBUFFER,0); return; }

  /* multisampled front end — the whole game is hard crystal edges against
   * near-black, so this is still the single biggest image-quality lever */
  if(msaa>1 && glRenderbufferStorageMultisample && glBlitFramebuffer){
    glGenFramebuffers(1,&fboMS);
    glBindFramebuffer(GL_FRAMEBUFFER,fboMS);
    glGenRenderbuffers(1,&rbColMS); glBindRenderbuffer(GL_RENDERBUFFER,rbColMS);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,msaa,hdr?GL_RGBA16F:GL_RGBA8,scW,scH);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rbColMS);
    glGenRenderbuffers(1,&rbDepMS); glBindRenderbuffer(GL_RENDERBUFFER,rbDepMS);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,msaa,GL_DEPTH_COMPONENT24,scW,scH);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,rbDepMS);
    postMS = glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
  }
  /* bloom chain: half, quarter, eighth (the tier may use fewer octaves, but we
   * allocate all three so a tier change never has to reallocate) */
  for(int i=0;i<NBLOOM;i++){
    bloomW[i]=scW>>(i+1); bloomH[i]=scH>>(i+1);
    if(bloomW[i]<2)bloomW[i]=2; if(bloomH[i]<2)bloomH[i]=2;
    for(int k=0;k<2;k++){
      glGenFramebuffers(1,&bloomFbo[i][k]);
      bloomTex[i][k]=mkrt(bloomW[i],bloomH[i],hdr);
      if(!attach(bloomFbo[i][k],bloomTex[i][k])){
        printf("[dilation] bloom FBO incomplete\n");
        glBindFramebuffer(GL_FRAMEBUFFER,0); return; }
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER,0);

  if(!progBright){
  progBright=mkprog(PVS,BRIGHTFS);
  bSrc=glGetUniformLocation(progBright,"uSrc");
  bTexel=glGetUniformLocation(progBright,"uTexel");
  bThresh=glGetUniformLocation(progBright,"uThresh");
  bKnee=glGetUniformLocation(progBright,"uKnee");
  progBlur=mkprog(PVS,BLURFS);
  lSrc=glGetUniformLocation(progBlur,"uSrc"); lDir=glGetUniformLocation(progBlur,"uDir");
  progComp=mkprog(PVS,COMPFS);
  cScene=glGetUniformLocation(progComp,"uScene");
  cB0=glGetUniformLocation(progComp,"uB0");
  cB1=glGetUniformLocation(progComp,"uB1");
  cB2=glGetUniformLocation(progComp,"uB2");
  cTime=glGetUniformLocation(progComp,"uTime");
  cTs=glGetUniformLocation(progComp,"uTs");
  cDmg=glGetUniformLocation(progComp,"uDmg");
  cExp=glGetUniformLocation(progComp,"uExp");
  cBloom=glGetUniformLocation(progComp,"uBloom");
  cBW=glGetUniformLocation(progComp,"uBW");
  }
  postOK=1;
  printf("[dilation] post: %s quality, %s, bloom x%d, %dx MSAA, %d lights, scene %dx%d -> window %dx%d\n",
    Q->name, hdr?"RGBA16F HDR":"RGBA8 LDR", Q->bloom, postMS?msaa:0, Q->lights,
    scW,scH, fbW,fbH);
}

/* a screen-filling quad in clip space — no matrices involved */
static void fsquad(void){
  glBegin(GL_QUADS);
  glTexCoord2f(0,0); glVertex2f(-1,-1);
  glTexCoord2f(1,0); glVertex2f( 1,-1);
  glTexCoord2f(1,1); glVertex2f( 1, 1);
  glTexCoord2f(0,1); glVertex2f(-1, 1);
  glEnd();
}
static void bind_tex(int unit,GLuint t){
  glActiveTexture_(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_2D,t);
}

/* point rendering at the scene target (or the window, if post is unavailable) */
static void post_begin(void){
  if(!postOK)return;
  glBindFramebuffer(GL_FRAMEBUFFER, postMS?fboMS:fboScene);
  glViewport(0,0,scW,scH);
}
/* resolve, build the bloom pyramid, and composite to the window */
static void post_end(float ts01,float dmg,float time){
  if(!postOK)return;
  if(postMS){   /* resolve multisample -> the sampleable float texture */
    glBindFramebuffer(GL_READ_FRAMEBUFFER,fboMS);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fboScene);
    glBlitFramebuffer(0,0,scW,scH, 0,0,scW,scH, GL_COLOR_BUFFER_BIT,GL_NEAREST);
  }
  glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDepthMask(GL_FALSE);

  /* bright pass into the half-res head of the chain; each octave then
   * downsamples the BLURRED octave above it and blurs again. The old two-loop
   * order box-halved UNBLURRED sources, so single-pixel emitters (bullet
   * heads, eye slits) aliased into sparkling, crawling halos. */
  int nb=QUAL[qual].bloom; if(nb<1)nb=1; if(nb>NBLOOM)nb=NBLOOM;
  for(int i=0;i<nb;i++){
    GLuint src = i? bloomTex[i-1][0] : texScene;
    int sw = i? bloomW[i-1] : scW, sh = i? bloomH[i-1] : scH;
    glUseProgram(progBright);
    glUniform1i(bSrc,0);
    glBindFramebuffer(GL_FRAMEBUFFER,bloomFbo[i][0]);
    glViewport(0,0,bloomW[i],bloomH[i]);
    glUniform2f(bTexel,1.0f/sw,1.0f/sh);
    /* only the first octave thresholds; the rest just carry the energy down.
     * The knee used to start at 1.30, which sat ABOVE most of the authored
     * emissive palette: the shin/forearm strips and the TRON belt peak at 1.15,
     * the player's eye slits at 1.25, so the things most obviously meant to glow
     * contributed nothing to the bloom and only the agent eyes and edge trims
     * (1.35-1.8) cleared it at all. 0.85 puts the whole palette inside the ramp. */
    glUniform1f(bThresh,i?0.0f:0.85f); glUniform1f(bKnee,i?0.0f:0.45f);
    bind_tex(0,src);
    fsquad();
    glUseProgram(progBlur);
    glUniform1i(lSrc,0);
    glBindFramebuffer(GL_FRAMEBUFFER,bloomFbo[i][1]);
    glUniform2f(lDir,1.0f/bloomW[i],0);
    bind_tex(0,bloomTex[i][0]); fsquad();
    glBindFramebuffer(GL_FRAMEBUFFER,bloomFbo[i][0]);
    glUniform2f(lDir,0,1.0f/bloomH[i]);
    bind_tex(0,bloomTex[i][1]); fsquad();
  }

  glBindFramebuffer(GL_FRAMEBUFFER,0);
  glViewport(0,0,fbW,fbH);
  glUseProgram(progComp);
  bind_tex(0,texScene);     glUniform1i(cScene,0);
  /* octaves the tier skipped are never rendered, so point their samplers at the
   * last one we DID render and scale the composite weights to match — sampling a
   * stale or undefined target would smear last-second garbage across the frame */
  bind_tex(1,bloomTex[0][0]);          glUniform1i(cB0,1);
  bind_tex(2,bloomTex[nb>1?1:0][0]);   glUniform1i(cB1,2);
  bind_tex(3,bloomTex[nb>2?2:nb-1][0]);glUniform1i(cB2,3);
  glUniform1f(cTime,time); glUniform1f(cTs,ts01); glUniform1f(cDmg,dmg);
  glUniform1f(cExp,1.15f); glUniform1f(cBloom,0.48f);
  glUniform3f(cBW, 1.0f, nb>1?0.85f:0.0f, nb>2?0.50f:0.0f);
  fsquad();

  /* leave the bloom textures bound and only restore the active unit: unbinding
   * left unit 1 pointing at the zero texture, which the world program samples
   * as uNrm and the driver complains about every frame. */
  glActiveTexture_(GL_TEXTURE0);
  glUseProgram(0);
  glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
}

/* ---------------------------------------------------------------- particles */
typedef struct { float x,y,z,vx,vy,vz,life,max; float cr,cg,cb; } Part;
static Part parts[MAXPART]; static int pHead=0;
static void spawn_parts(int n,float x,float y,float z,float spd,float cr,float cg,float cb){
  for(int i=0;i<n;i++){
    Part*p=&parts[pHead]; pHead=(pHead+1)%MAXPART;
    float a=frand()*2*PI, b=(frand()-0.5f)*PI;
    p->x=x;p->y=y;p->z=z;
    p->vx=cosf(a)*cosf(b)*spd*(0.4f+frand());
    p->vy=sinf(b)*spd*(0.4f+frand())+1.5f;
    p->vz=sinf(a)*cosf(b)*spd*(0.4f+frand());
    p->life=p->max=0.25f+frand()*0.3f;
    p->cr=cr;p->cg=cg;p->cb=cb;
  }
}
static void spawn_shards(float x,float y,float z,float vr,float scale){
  /* Momentum carry is what reads as "it was moving when it died": a charging
   * striker's debris should go where the striker was going, not puff out
   * symmetrically like a standing shooter's. Set by shatter_enemy. */
  extern float g_shardVX,g_shardVZ;
  /* a figure comes apart: glowing facets, tinted by its final Doppler.
   * scale grows count, spread, size and speed together — 1.0 for an agent
   * (bit-identical to the unscaled path), ~2.6 for the 4.5u OVERLORD, whose
   * death used to reuse the mook burst: 16 tiny chips in a 0.4u box. */
  int n=(int)(16*scale); if(n>96)n=96;
  for(int i=0;i<n;i++){
    Shard*s=&shards[shHead]; shHead=(shHead+1)%MAXSHARD;
    float a=frand()*2*PI, b=(frand()-0.4f)*PI*0.5f;
    float sp=(1.5f+frand()*3.5f)*(0.7f+0.3f*scale);
    s->x=x+(frand()-0.5f)*0.4f*scale; s->y=y+(0.3f+frand()*1.5f)*scale; s->z=z+(frand()-0.5f)*0.4f*scale;
    s->vx=cosf(a)*cosf(b)*sp+g_shardVX*0.35f;
    s->vy=sinf(b)*sp+2.0f;
    s->vz=sinf(a)*cosf(b)*sp+g_shardVZ*0.35f;
    s->yaw=frand()*2*PI; s->pit=frand()*2*PI;
    s->wy=(frand()-0.5f)*14; s->wp=(frand()-0.5f)*14;
    s->sx=(0.06f+frand()*0.16f)*scale; s->sy=(0.06f+frand()*0.22f)*scale; s->sz=(0.02f+frand()*0.05f)*scale;
    s->life=s->max=1.1f+frand()*0.7f;
    /* doppler-tinted emissive: vr<0 was closing on you when it died */
    float k=clampf(vr/8.0f,-1,1);
    if(k<0){ s->r=0.15f-0.05f*k; s->g=0.95f-0.30f*k; s->b=0.40f-1.10f*k; }
    else   { s->r=0.15f+1.30f*k; s->g=0.95f-0.62f*k; s->b=0.40f-0.27f*k; }
  }
}

float g_shardVX=0,g_shardVZ=0;   /* victim velocity, read by spawn_shards */
/* ---------------------------------------------------------------- game state */
enum { ST_TITLE, ST_PLAY, ST_DEAD, ST_WIN };
static int gstate=ST_TITLE;
static float py,ppitch,php;   /* px,pz,pyaw live with the audio block above */
static float pvy;                           /* (pvx/pvz live with the audio block) */
static int   pammo,jumps;                   /* finite pistol ammo; pickups keep you moving */
static float tscale=1, actT, mouseAcc;
static float tsEff=1;                       /* tscale with the hitstop dip folded
                                               in — what wdt/audio/grade consume */
static float rollT,rollCD,rollDX,rollDZ;    /* dodge roll: timer + direction  */
static float kvx,kvz;                       /* wall-kick horizontal impulse   */
static float pmoveb;                        /* idle<->run blend for the avatar*/
static float pspdS;                         /* speed follower; see update_enemies */
static float avYaw;                         /* smoothed avatar facing (rad)   */
static float camDist=3.05f,camYs=-1.0f;     /* smoothed camera boom + height  */
static float coyT;                          /* coyote time: late edge jumps   */
static float hurtCD;                        /* post-hit mercy window          */
static float mzT,mzX,mzY,mzZ;               /* avatar muzzle flash            */
static float landT,landTgt;                 /* landing absorb: eased / target */
static float airB;                          /* smoothed airborne blend 0..1   */
static float hitstop;                       /* impact freeze: pins the world  */
/* ADS: hold RMB to pull the camera out of the over-shoulder boom and into the
 * avatar's own head. Eased on RAW dt — this is the player's hands, not the
 * world, so it must not slow down when time does. `adsHold` is the button,
 * `ads` the 0..1 blend everything else reads through ads_amt(). */
static int   adsHold=0;
static float ads=0;
static float ads_amt(void){ return sstep(clampf(ads,0,1)); }
/* FOV half-angles, in degrees, that the transition interpolates between. Lerped
 * in TAN space (see the projection setup) — lerping the angle itself makes the
 * zoom visibly accelerate through the middle. */
#define FOV_HIP 35.0f
#define FOV_ADS 26.0f

static float player_height(void){ return rollT>0 ? 0.85f : 1.72f; }
static float player_camh(void){ return py + (rollT>0 ? 1.45f : 1.92f); }
static float fireCD,swingT,swingCD,dmgFlash,stepT,shake,bobT,winT,gtime,wtime,msgT;
static float winRealT,winSimT;   /* both clocks, frozen at the moment of victory */
static float rollPT;             /* roll dust accumulator: spawn on time, not per frame */
static float swRel,swStow;   /* katana exit blends: end-of-cut->carry, carry->pistol */
static float gunCharge;                       /* 0..1, controls laser/shot range */
static float aimSet=1,stableT;      /* physical aim: 0 = gun rides the running
                                       body, 1 = settled on the look ray after
                                       GUN_SETTLE_TIME of stillness */
static int laserTarget=-1;                    /* living enemy currently reachable by charged beam */
static unsigned gseed=0;                    /* xor'd into the level seed      */
static int smoke=0;
static int swRender=0;   /* GL_RENDERER is a software rasterizer (no frame budget) */
static int titlecap=0;   /* --titlecap: dump a numbered title-screen frame sequence for the README GIF */

/* title-screen sector preview: selecting with 1-4/arrows used to only set
 * curlevel, so the title kept rendering the OLD geometry tinted in the NEW
 * sector's climate under the new name. Regenerating on selection turns the
 * title into a real preview of the chosen sector (agents, lights and all). */
static unsigned reroll=0;   /* bumped by R on the title, mixed into the seed */
static void preview_level(void){ gen_level(curlevel,gseed^reroll); laserTarget=-1; }

static void reset_game(void){
  gen_level(curlevel,gseed^reroll);
  px=startx; pz=startz; pyaw=startyaw; ppitch=0; pvx=pvz=pvy=0;
  avYaw=startyaw*PI/180.0f;
  py=hgt[(int)(startz/CELL)][(int)(startx/CELL)];
  php=100; pammo=6; jumps=1;
  tscale=tsEff=1; actT=0; mouseAcc=0;
  aimSet=1; stableT=GUN_SETTLE_TIME;   /* you jack in standing still: settled */
  rollT=rollCD=rollDX=rollDZ=kvx=kvz=pmoveb=pspdS=0;
  coyT=hurtCD=mzT=landT=landTgt=hitstop=airB=0;
  ads=0; adsHold=0;   /* respawning mid-zoom used to drop you in already aimed */
  camDist=3.05f; camYs=-1.0f;
  fireCD=swingT=swingCD=dmgFlash=stepT=shake=bobT=winT=wtime=0;
  winRealT=winSimT=rollPT=0;
  swRel=swStow=0;
  gunCharge=0;
  msgT=3.0f;
  laserTarget=-1;
  for(int i=0;i<MAXTEMPL;i++)templ_[i].life=0;
  for(int i=0;i<MAXPART;i++)parts[i].life=0;
  for(int i=0;i<MAXSHARD;i++)shards[i].life=0;
  for(int i=0;i<MAXBUL;i++)bul[i].on=0;
}

/* circle-vs-grid, axis separated. y = mover's foot height: cells whose floor
 * is within STEP of the feet are walkable, anything taller blocks. */
static int circ_free(float x,float z,float r,float y){
  for(int dz=-1;dz<=1;dz++)for(int dx=-1;dx<=1;dx++){
    int cx=(int)floorf(x/CELL)+dx, cz=(int)floorf(z/CELL)+dz;
    if(cellh(cx,cz)<=y+STEP)continue;
    if(circ_cell(x,z,r,cx,cz)) return 0;
  }
  return 1;
}
static void move_circ(float*x,float*z,float dx,float dz,float r,float y){
  if(circ_free(*x+dx,*z,r,y)) *x+=dx;
  if(circ_free(*x,*z+dz,r,y)) *z+=dz;
}
/* the floor under a standing circle: highest reachable cell top it overlaps */
static float ground_h(float x,float z,float y){
  float g=0;
  for(int dz=-1;dz<=1;dz++)for(int dx=-1;dx<=1;dx++){
    int cx=(int)floorf(x/CELL)+dx, cz=(int)floorf(z/CELL)+dz;
    if(cx<0||cz<0||cx>=G||cz>=G||grid[cz][cx])continue;
    float h=hgt[cz][cx];
    if(h>y+STEP||h<=g)continue;
    if(circ_cell(x,z,0.30f,cx,cz)) g=h;
  }
  return g;
}
/* is there a kickable wall right beside us? returns its outward normal */
static int wall_kick(float x,float z,float y,float*nx,float*nz){
  static const float D[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
  for(int k=0;k<4;k++){
    int cx=(int)floorf((x+D[k][0]*0.55f)/CELL);
    int cz=(int)floorf((z+D[k][1]*0.55f)/CELL);
    if(cellh(cx,cz)>y+1.0f){ *nx=-D[k][0]; *nz=-D[k][1]; return 1; }
  }
  return 0;
}
/* DDA ray vs grid; returns hit distance (<= maxd). Height-aware: raised
 * floors block the ray where it passes below their top. */
static float ray_wall(float ox,float oy,float oz,float dx,float dy,float dz,float maxd){
  float t=0; int cx=(int)floorf(ox/CELL), cz=(int)floorf(oz/CELL);
  int sx=dx>0?1:-1, sz=dz>0?1:-1;
  float tdx=fabsf(dx)>1e-6f?CELL/fabsf(dx):1e9f, tdz=fabsf(dz)>1e-6f?CELL/fabsf(dz):1e9f;
  float nx=(sx>0?(cx+1)*CELL-ox:ox-cx*CELL), nz=(sz>0?(cz+1)*CELL-oz:oz-cz*CELL);
  float tx=fabsf(dx)>1e-6f?nx/fabsf(dx):1e9f, tz=fabsf(dz)>1e-6f?nz/fabsf(dz):1e9f;
  for(int it=0;it<160;it++){
    if(tx<tz){ t=tx; tx+=tdx; cx+=sx; } else { t=tz; tz+=tdz; cz+=sz; }
    if(t>maxd)return maxd;
    float h=cellh(cx,cz);
    if(h>1e8f)return t;                        /* full wall */
    if(h>0.001f){
      float t2=tx<tz?tx:tz; if(t2>maxd)t2=maxd;
      float y0=oy+dy*t, y1=oy+dy*t2;           /* entry / exit heights */
      if(y0<h||y1<h)return t;                  /* clipped the platform */
    }
  }
  return maxd;
}
static int los(float ax,float ay,float az,float bx,float by,float bz){
  float dx=bx-ax,dy=by-ay,dz=bz-az; float d=sqrtf(dx*dx+dy*dy+dz*dz);
  if(d<0.01f)return 1;
  return ray_wall(ax,ay,az,dx/d,dy/d,dz/d,d) >= d-0.05f;
}
static void add_templ(float x,float y,float z,float r,float life,float cr,float cg,float cb){
  for(int i=0;i<MAXTEMPL;i++) if(templ_[i].life<=0){
    templ_[i]=(TempL){x,y,z,r,life,cr,cg,cb}; return; }
}

/* ---------------------------------------------------------------- doppler
 * The signature shade: radial velocity of a thing relative to the player.
 * Negative vr = closing in = blueshift; positive = receding = redshift. */
static float radial_v(float x,float z,float vx,float vz){
  float dx=x-px, dz=z-pz, d=sqrtf(dx*dx+dz*dz);
  if(d<0.01f)return 0;
  return ((vx-pvx)*dx + (vz-pvz)*dz)/d;
}
static void dopp_rgb(float vr,float*r,float*g,float*b){
  float k=clampf(vr/8.0f,-1,1);
  if(k<0){ k=-k;
    *r=0.15f+(0.22f-0.15f)*k; *g=0.95f+(0.60f-0.95f)*k; *b=0.40f+(1.65f-0.40f)*k;
  } else {
    *r=0.15f+(1.55f-0.15f)*k; *g=0.95f+(0.30f-0.95f)*k; *b=0.40f+(0.12f-0.40f)*k;
  }
}

/* ---------------------------------------------------------------- combat */
static void hurt_player(float dmg){
  if(gstate!=ST_PLAY||hurtCD>0)return;   /* mercy: one fan can't double-tap */
  hurtCD=0.45f;
  php-=dmg; dmgFlash=0.6f; shake=0.3f; sfx(V_HURT);
  if(php<=0){ php=0; gstate=ST_DEAD; SDL_SetRelativeMouseMode(SDL_FALSE); }
}
static Bullet* new_bullet(void){
  for(int i=0;i<MAXBUL;i++) if(!bul[i].on){
    Bullet*b=&bul[i]; memset(b,0,sizeof*b); b->on=1; return b; }
  return 0;
}
static void spawn_bullet(float x,float y,float z,float dx,float dy,float dz,
                         float spd,int owner,float range){
  Bullet*b=new_bullet(); if(!b)return;
  float il=1.0f/sqrtf(dx*dx+dy*dy+dz*dz+1e-9f);
  b->x=x;b->y=y;b->z=z;
  b->vx=dx*il*spd; b->vy=dy*il*spd; b->vz=dz*il*spd;
  b->life=6.0f; b->owner=owner; b->range=range;
  b->tn=0; b->th=0; b->trd=0;
}
static void shatter_enemy(Enemy*e){
  if(e->state==4)return;
  e->state=4; nalive--;
  /* Death kinematics. The figure used to cease to exist on the exact frame the
   * shards spawned — in a game whose entire punch is a hitstop that HOLDS the
   * moment, the moment itself was a one-frame pop. dieT keeps the body drawable
   * for a beat while it squashes, bulges and blows out to white, so the shards
   * emerge FROM a body instead of replacing one. Decayed on world time next to
   * the shard integrator, so a kill at MINTS hangs exactly like the debris does. */
  e->dieT = e->type==2 ? 0.30f : 0.13f;
  /* the muzzle-flash timer stops ticking the moment an agent dies (update_enemies
   * decays it BELOW its state==4 early-out), so a corpse killed mid-volley used
   * to keep a full-brightness flash pinned at its last firing point for the rest
   * of the sector. Retire it with the body. */
  e->mzT=0;
  float vr=radial_v(e->x,e->z,e->vx,e->vz);
  int boss=e->type==2;
  float sc=boss?2.6f:1.0f;
  g_shardVX=e->vx; g_shardVZ=e->vz;
  spawn_shards(e->x,e->y,e->z,vr,sc);
  g_shardVX=g_shardVZ=0;
  spawn_parts(boss?48:14,e->x,e->y+1.1f*sc,e->z,2.5f*(boss?1.8f:1.0f), 0.2f,1.0f,0.5f);
  add_templ(e->x,e->y+1.2f*sc,e->z,6.0f*sc,boss?0.8f:0.35f, 0.3f,2.2f,1.0f);
  sfx3(V_SHATTER,boss?0.45f:1.0f,e->x,e->y,e->z);
  { float hs=boss?0.35f:0.055f; if(hitstop<hs)hitstop=hs; }
  if(boss)shake=0.9f;
  /* shooters drop their sidearm — but once you're bleeding out they drop a med
   * cache instead, so a bad fight doesn't spiral into an unrecoverable one.
   * The ammo roll is drawn either way to keep the RNG stream order fixed. */
  if(e->type==0){
    int amt=3+(int)(frand()*3);
    if(php<50) add_item(e->x,e->z,0,35); else add_item(e->x,e->z,1,amt);
  }
  if(nalive<=0 && gstate==ST_PLAY){
    /* Snapshot both clocks HERE. winT is only advanced inside the ST_PLAY
     * branch, so zeroing it on this edge (as we used to) meant the victory card
     * read "0 SECONDS REAL" every single time. wtime, meanwhile, keeps climbing
     * on the win screen — deliberately, since the victory slow-mo drives every
     * sinf(wtime*..) animation — so reading it live made the card's second
     * number drift upward while you looked at it. */
    winRealT=winT; winSimT=wtime;
    gstate=ST_WIN; sfx(V_WIN); SDL_SetRelativeMouseMode(SDL_FALSE);
  }
}
/* every round/melee hit funnels through here. normal agents carry hp 0, so the
 * hp>1 gate is false and they shatter on the first hit exactly as before — the
 * smoke regression stays byte-identical. the boss (hp bossMaxHp) bleeds and flashes
 * until the killing hit drops it through to shatter_enemy. */
static void damage_enemy(Enemy*e,int dmg){
  if(e->state==4)return;
  if(e->hp>1){
    e->hp-=dmg;
    if(e->hp>0){
      e->flash=0.10f;
      spawn_parts(5,e->x,e->y+2.6f,e->z,2.2f, 0.7f,1.3f,1.0f);
      add_templ(e->x,e->y+2.6f,e->z,3.0f,0.06f, 0.6f,1.9f,1.5f);
      sfx3(V_DEFLECT,1.25f,e->x,e->y+2.0f,e->z);
      return;
    }
  }
  shatter_enemy(e);
}
static void player_aim(float*dx,float*dy,float*dz){
  float yr=pyaw*PI/180, pr=ppitch*PI/180;
  *dx=sinf(yr)*cosf(pr); *dy=-sinf(pr); *dz=-cosf(yr)*cosf(pr);
}
/* one deterministic evaluation of the avatar's whole-body stance — shared by
 * draw_player and the sim-side aiming chain below, so the drawn figure and
 * the ballistics can never drift apart. */
typedef struct {
  int rolling,blade;
  float tuck,tk,walk,armw,s,spd,run,fwdb,latb,absorb,poseYaw,pcy;
  float M[9];
} PPose;
static void player_pose(PPose*P){
  P->rolling = rollT>0;
  float rp = P->rolling? sstep(1.0f-rollT/ROLL_TIME) : 0; /* roll progress    */
  P->tuck = P->rolling? sinf(rp*PI) : 0;               /* curl: 0 -> 1 -> 0  */
  P->tk = 1.0f-0.50f*P->tuck;                          /* anchors pull in    */
  P->walk = sinf(bobT*7.5f);
  /* contralateral arm phase: the arms were driven by sin (peaking at ph=PI/2),
   * 90 degrees out of phase with the legs' triangle stride (peaking at ph=0).
   * cos peaks with the stride: right arm forward as the LEFT leg plants. */
  P->armw = cosf(bobT*7.5f);
  P->s = swingT>0? clampf(swingT/SWING_TIME,0,1) : 0;  /* katana swing phase */
  P->blade = P->s>0 || swingCD>0;                      /* + follow-through   */
  /* ONE yaw for every stance. avYaw lives in the aim convention (forward =
   * (sin a, -cos a), like pyaw/player_aim) and eases toward the look yaw —
   * or toward the roll direction mid-roll — in the main loop. m3rotY() maps
   * local -Z forward to (-sin a, -cos a), so the model always takes the
   * NEGATED angle. The old code negated only the combat stance and fed the
   * raw avYaw to rolls, which mirrored sideways rolls (a strafe-right dodge
   * faced left) and popped the body by up to 180° entering/leaving them. */
  P->poseYaw = -avYaw;
  P->spd=sqrtf(pvx*pvx+pvz*pvz);
  P->run=clampf(P->spd/5.0f,0,1)*pmoveb;
  P->fwdb=0; P->latb=0;
  if(P->spd>0.05f){
    /* local move blends in the aim convention (the agents' formula) — these
     * were computed with the negated yaw, so lean/sway wandered with the
     * world heading: running forward gave fwdb=cos(2*yaw) instead of 1. */
    float ivx=pvx/P->spd, ivz=pvz/P->spd;
    P->fwdb=ivx*sinf(avYaw)-ivz*cosf(avYaw);
    P->latb=ivx*cosf(avYaw)+ivz*sinf(avYaw);
  }
  /* landing absorb: eased square so the dip hits hard and recovers soft. The
   * hips drop, the IK feet stay on the floor, and the knees fold to make up
   * the difference — the same trick the boss uses on its leap recoil. */
  P->absorb = P->rolling? 0 : landT*landT;
  float lean = P->rolling? rp*2*PI
             : (0.07f+0.07f*clampf(P->fwdb,0,1))*P->run + 0.20f*P->absorb
               + 0.10f*airB;                      /* slight airborne pitch-in */
  float sway = P->rolling? 0
             : (-0.10f*P->latb*P->run + 0.025f*P->walk*P->run); /* counter-tilt */
  float R[9],X[9],Z[9],S0[9];
  m3rotY(R,P->poseYaw); m3rotZ(Z,sway); m3rotX(X,lean); m3mul(S0,Z,X); m3mul(P->M,R,S0);
  P->pcy=py+0.55f+0.25f*P->tuck-0.26f*P->absorb
        + (P->rolling?0:0.025f*fabsf(P->walk)*P->run); /* ball clears floor */
}

/* the right arm + pistol, solved once for BOTH the renderer and the sim.
 * PHYSICAL AIMING: the gun is a thing the body carries, not a crosshair.
 * aimSet blends a low running carry — swinging with the stride, barely
 * pitch-aware — into the settled aim pose, and a final alignment rotates the
 * grip basis onto mix(barrel, look-ray, aimSet): at full settle the barrel
 * IS the look ray, mid-run it points wherever the pumping arm has it.
 * Bullets, laser, lock-on and muzzle flash all read this one basis, so what
 * the gun visibly points at is always exactly where the round will go. */
typedef struct {
  float A[9],F[9],GP[9];          /* upper arm / forearm / gun grip bases */
  float sx,sy,sz;                 /* shoulder joint (world)               */
  float gx,gy,gz;                 /* gun origin (world)                   */
  float tipx,tipy,tipz;           /* barrel tip — muzzle                  */
  float bdx,bdy,bdz;              /* barrel direction (unit)              */
} ArmR;
static void player_arm_r(const PPose*P,ArmR*o){
  float fr=clampf(fireCD/FIRE_TIME,0,1);
  float raise,swYaw,sw;
  if(P->rolling){ raise=0.35f+0.95f*P->tuck; swYaw=0; sw=1.0f; }
  else {
    float prad=ppitch*PI/180.0f;
    float rs=1.18f+0.12f*sstep(fr)-prad*0.90f;    /* settled: on the look ray */
    float rc=0.55f+0.30f*sstep(fr)-prad*0.30f;    /* carry: low, half-hearted */
    raise = rc+(rs-rc)*aimSet;
    swYaw = 0.06f*(1.0f-aimSet)+(-0.12f-0.05f*P->latb)*aimSet;
    float aimDamp=1.0f+(0.18f-1.0f)*aimSet;       /* settle kills the pumping */
    sw = P->armw*0.42f*P->run*aimDamp*clampf(1.3f-raise,0,1);
    raise += 0.42f*P->absorb*(1.0f-aimSet);       /* landings jolt the carry  */
    /* re-stow: the frame the katana leaves the hand the arm was at the
     * 0.52/0.42 carry — ease from there into the pistol pose. The alignment
     * below keeps the barrel exact regardless of the blended raise. */
    raise += (0.52f-raise)*swStow;
    swYaw += (0.42f-swYaw)*swStow;
    /* ADS: bring the gun UP to eye level and IN across the centreline, so from
     * inside the head it reads as a sight picture instead of a hand dangling in
     * the corner of the frame. This changes the gun's POSITION only — the
     * m3align below re-seats the barrel onto mix(pose, look ray, aimSet)
     * regardless, so the ballistics are untouched and the sights still cannot
     * disagree with where the round goes. Bringing it inboard also shrinks the
     * muzzle-to-eye parallax, which is what makes the reticle honest up close. */
    /* Raise target solved, not guessed: the eye sits 0.24 above the shoulder and
     * the arm is 0.68 long, so the hand reaches eye level at acos(-0.24/0.68) =
     * 1.93 rad. Below that the sights sit under the sight line and there is
     * nothing to align — which is the entire point of the mode. */
    { float ae=ads_amt();
      raise += (1.66f-raise)*ae;
      swYaw += (-0.42f-swYaw)*ae; }
  }
  /* Shouldering the weapon rolls the shoulder inboard AND presents it forward.
   * Inboard because the real motion is a cheek weld, and without it the gun
   * stays a hand's width to the right of where you are looking. Forward because
   * the arm is only 0.68 long: with the shoulder at the eye, the whole weapon
   * sits well inside half a metre and fills a zoomed frame. Pushing the shoulder
   * out along the look direction buys apparent distance, which is the only lever
   * available without giving the viewmodel its own projection. */
  { float ae=P->rolling?0.0f:ads_amt();
    /* all the way onto the centreline, not merely inboard: the sights have to be
     * under the eye or they are decoration */
    float sh[3]; m3v(P->M,0.27f-0.20f*ae,1.05f*P->tk,-0.22f*ae,sh);
    o->sx=px+sh[0]; o->sy=P->pcy+sh[1]; o->sz=pz+sh[2]; }
  float RX[9],RY[9],S[9],e[3],h[3];
  m3rotX(RX,raise+sw); m3rotY(RY,swYaw); m3mul(S,RY,RX); m3mul(o->A,P->M,S);
  m3v(o->A,0,-0.35f,0,e);
  float ex=o->sx+e[0], ey=o->sy+e[1], ez=o->sz+e[2];
  float RE[9]; m3rotX(RE,raise+sw+0.16f); m3mul(S,RY,RE); m3mul(o->F,P->M,S);
  m3v(o->F,0,-0.33f,0,h);
  float hx=ex+h[0], hy=ey+h[1], hz=ez+h[2];
  /* grip: pistol_sh() barrels down local -Z; roll the basis into the palm */
  float GY[9],GX[9],GZ[9],GT[9];
  m3rotY(GY,0.04f); m3rotX(GX,-PI*0.5f-0.06f); m3rotZ(GZ,0.04f);
  m3mul(GT,GY,GX); m3mul(GT,GT,GZ); m3mul(o->GP,o->F,GT);
  /* aim alignment: rotate the grip so the barrel lands on the blend between
   * where the pose has it and where the camera looks. At aimSet=1 the barrel
   * is exactly the look ray — zero divergence between beam and bullet. */
  { float b[3]; m3v(o->GP,0,0,-1,b);
    float ax,ay,az; player_aim(&ax,&ay,&az);
    float mx=b[0]+(ax-b[0])*aimSet, my_=b[1]+(ay-b[1])*aimSet, mz_=b[2]+(az-b[2])*aimSet;
    float il=1.0f/sqrtf(mx*mx+my_*my_+mz_*mz_+1e-9f);
    m3align(o->GP, b[0],b[1],b[2], mx*il,my_*il,mz_*il); }
  /* ROLL LEVELLING. m3align above pins the barrel DIRECTION but says nothing
   * about the gun's rotation around it — that comes from wherever the wrist
   * happens to be, which is fine for a carried weapon and useless for a sighted
   * one: raise the arm and the iron sights rotate off to the side. Roll the grip
   * about its own barrel until the gun's local up meets world up, blended in by
   * ads_amt() so the hip-fire pose keeps the wrist's character and the shouldered
   * pose gets a true sight picture. Rolling ABOUT the barrel cannot disturb it,
   * so the ballistics guarantee is untouched. */
  { float ae=P->rolling?0.0f:ads_amt();
    if(ae>0.001f){
      float f[3]; m3v(o->GP,0,0,-1,f);
      float u[3]; m3v(o->GP,0,1,0,u);
      /* world up with the barrel component removed = the up we want, unless we
       * are aiming straight up or down, where it degenerates and we leave it */
      float d=f[1];
      float vx=-f[0]*d, vy=1.0f-f[1]*d, vz=-f[2]*d;
      float vl=sqrtf(vx*vx+vy*vy+vz*vz);
      if(vl>0.15f){
        vx/=vl; vy/=vl; vz/=vl;
        float wx=u[0]+(vx-u[0])*ae, wy=u[1]+(vy-u[1])*ae, wz=u[2]+(vz-u[2])*ae;
        float wl=sqrtf(wx*wx+wy*wy+wz*wz);
        if(wl>1e-5f) m3align(o->GP, u[0],u[1],u[2], wx/wl,wy/wl,wz/wl);
      }
    } }
  /* anchor the grip into the palm (inverse of pistol_sh's grip offset), with
   * a small forearm-up lift that seats it visibly in the hand */
  float go[3],lift[3]; m3v(o->GP,0,0.145f,0.020f,go); m3v(o->F,0,0.125f,0,lift);
  o->gx=hx+go[0]+lift[0]; o->gy=hy+go[1]+lift[1]; o->gz=hz+go[2]+lift[2];
  float t[3]; m3v(o->GP,0,-0.010f,-0.40f,t);
  o->tipx=o->gx+t[0]; o->tipy=o->gy+t[1]; o->tipz=o->gz+t[2];
  float b2[3]; m3v(o->GP,0,0,-1,b2);
  o->bdx=b2[0]; o->bdy=b2[1]; o->bdz=b2[2];
}

/* ---------------------------------------------------------------- pose cache
 * player_pose + player_arm_r were solved FIVE times a frame from identical
 * inputs: twice by draw_player (mirror pass, then upright), once by
 * player_laser via draw_player_laser, once by laser_target, and once more by
 * the ADS eye. The solve is pure, so it can be shared — but only within a
 * PHASE. Between laser_target (simulation) and draw time, swingT/swingCD/swRel/
 * swStow/landT all still move, so a cache spanning the two would quietly change
 * behaviour rather than just speed. pose_dirty() is therefore called at exactly
 * two places: the top of the sim section and the top of the render section. */
static int   poseCached=0;
static PPose cPose;
static ArmR  cArm;
static void pose_dirty(void){ poseCached=0; }
static const PPose* pose_get(void){
  if(!poseCached){ player_pose(&cPose); player_arm_r(&cPose,&cArm); poseCached=1; }
  return &cPose;
}
static const ArmR* arm_get(void){ pose_get(); return &cArm; }

static void player_laser(float*mx,float*my,float*mz,float*dx,float*dy,float*dz,
                         float*hx,float*hy,float*hz,float*dist){
  const ArmR a=*arm_get();
  *mx=a.tipx; *my=a.tipy; *mz=a.tipz;
  *dx=a.bdx;  *dy=a.bdy;  *dz=a.bdz;
  /* The laser is the capacitor gauge: it grows outward by charge, but never
   * below GUN_MIN_LASER — the pointer is the aim, so it must always be
   * visible, swaying with the carried gun and all. Bullets still collide
   * with geometry normally; this distance is their energy budget. */
  float d=pammo>0 ? fmaxf(GUN_MIN_LASER, GUN_MAX_RANGE*clampf(gunCharge,0,1)) : 0.0f;
  *hx=*mx+*dx*d; *hy=*my+*dy*d; *hz=*mz+*dz*d; *dist=d;
}
static int laser_target(void){
  if(gstate!=ST_PLAY||pammo<=0||rollT>0||swingT>0||swingCD>0)return -1;
  float mx,my,mz,dx,dy,dz,hx,hy,hz,range;
  player_laser(&mx,&my,&mz,&dx,&dy,&dz,&hx,&hy,&hz,&range);
  float wall=ray_wall(mx,my,mz,dx,dy,dz,range);
  int best=-1; float bestt=range+1.0f;
  for(int i=0;i<nen;i++){
    Enemy*e=&en[i]; if(e->state==4)continue;
    /* Same readable hit volume as player bullets: a slim vertical capsule.
     * Test the charged ray against the enemy's horizontal radius, then check
     * the height at that first intersection. */
    float hr=e->type==2?1.5f:0.45f, htop=e->type==2?5.0f:2.0f;
    float ex=mx-e->x, ez=mz-e->z;
    float a=dx*dx+dz*dz;
    if(a<1e-6f)continue;
    float b=2.0f*(ex*dx+ez*dz);
    float c=ex*ex+ez*ez-hr*hr;
    float disc=b*b-4.0f*a*c;
    if(disc<0)continue;
    float t=(-b-sqrtf(disc))/(2.0f*a);
    if(t<0.02f)t=(-b+sqrtf(disc))/(2.0f*a);
    if(t<0.02f||t>range||t>wall+0.03f||t>=bestt)continue;
    float y=my + dy*t;
    if(y>e->y && y<e->y+htop){ best=i; bestt=t; }
  }
  return best;
}
static void fire(void){
  /* swingCD, not just swingT: the blade stays IN THE HAND through the whole
   * cooldown (player_pose's `blade` is `s>0 || swingCD>0`), and both the laser
   * and the lock-on already gate on it. Without it there was a ~0.24s window
   * where a round left a gun that is not drawn — flash, report and ammo spent
   * from a phantom pistol beside the katana. */
  if(fireCD>0||rollT>0||swingT>0||swingCD>0)return;
  if(pammo<=0){ sfx(V_CLICK); fireCD=0.3f; gunCharge=0; return; }
  /* sample the barrel FIRST: the round leaves the gun as it was at trigger
   * pull — where the gun physically points, not where the camera looks —
   * before the recoil state (fireCD raise, settle knock) kicks the pose.
   * pose_dirty() is mandatory here and not an optimisation detail: fire() runs
   * during event polling, BEFORE the simulation phase invalidates the cache, so
   * without this it would aim with last frame's pitch. The regression gate
   * caught exactly that — every shot around a trigger pull moved. */
  pose_dirty();
  float mx,my,mz,dx,dy,dz,hx,hy,hz,ld;
  player_laser(&mx,&my,&mz,&dx,&dy,&dz,&hx,&hy,&hz,&ld);
  fireCD=FIRE_TIME; actT=0.22f;
  pammo--;
  /* Recoil unsettles the aim so follow-ups want a beat. Knock the SETTLE CLOCK
   * back rather than the settled value: aimSet is re-derived from stableT every
   * frame, so multiplying it directly was a one-frame yank on the gun basis (and
   * therefore on the laser) that the eased path immediately undid. Taking 0.09s
   * off the clock produces the same recovery through the existing ease. */
  stableT-=0.09f; if(stableT<0)stableT=0;
  sfx(V_SHOT);
  /* a round is never wasted: even an uncharged pistol carries point-blank
   * reach, so spending the ammo always buys you something */
  if(ld<GUN_MIN_RANGE)ld=GUN_MIN_RANGE;
  spawn_bullet(mx,my,mz,dx,dy,dz,PLAYER_BULLET_SPEED,1,ld);
  gunCharge=0;
  add_templ(mx+dx*0.20f,my+dy*0.20f,mz+dz*0.20f,5.0f,0.07f, 1.2f,3.2f,1.8f);
  mzX=mx; mzY=my; mzZ=mz; mzT=0.06f;
}
static void katana(void){
  if(swingCD>0||swingT>0)return;
  /* Drawing the blade lowers the gun. ADS and the katana are the same hand, and
   * without this you could hold RMB to full zoom — camera inside the skull, near
   * plane pulled to 0.035 — and then swing a 1.05-unit blade whose centre passes
   * 6cm in front of the lens. The ads target below also gates on the blade, so
   * the two cannot re-enter each other during the follow-through. */
  adsHold=0;
  swingT=0.0001f; swingCD=0.5f; actT=0.26f;
  sfx(V_SWING);
}
/* the active swing window: kill close agents, bat bullets back */
static void katana_strike(void){
  float yr=pyaw*PI/180, fx=sinf(yr), fz=-cosf(yr);
  for(int i=0;i<nen;i++){
    Enemy*e=&en[i]; if(e->state==4)continue;
    float dx=e->x-px,dz=e->z-pz,d=sqrtf(dx*dx+dz*dz);
    float reach=e->type==2?3.0f:1.9f, vreach=e->type==2?3.0f:1.4f;
    if(d>reach||fabsf(e->y-py)>vreach)continue;
    if((dx*fx+dz*fz)/(d+1e-6f) < 0.45f)continue;
    damage_enemy(e,1);
  }
  for(int i=0;i<MAXBUL;i++){
    Bullet*b=&bul[i];
    if(!b->on||b->owner!=0)continue;
    float dx=b->x-px,dz=b->z-pz,d=sqrtf(dx*dx+dz*dz);
    if(d>2.5f||b->y<py||b->y>py+2.2f)continue;
    if((dx*fx+dz*fz)/(d+1e-6f) < 0.30f)continue;
    if(b->vx*dx+b->vz*dz > 0)continue;            /* must be inbound */
    /* deflect: retarget the nearest living agent in line of sight */
    int t=-1; float bd=1e9f;
    for(int j=0;j<nen;j++){ Enemy*e=&en[j]; if(e->state==4)continue;
      float ex=e->x-b->x,ez=e->z-b->z,ed=ex*ex+ez*ez;
      if(ed<bd && los(b->x,b->y,b->z,e->x,e->y+1.3f,e->z)){bd=ed;t=j;} }
    float ndx,ndy,ndz;
    if(t>=0){ ndx=en[t].x-b->x; ndy=(en[t].y+1.35f)-b->y; ndz=en[t].z-b->z; }
    else    { ndx=fx; ndy=0.02f; ndz=fz; }
    float il=1.0f/sqrtf(ndx*ndx+ndy*ndy+ndz*ndz+1e-9f);
    b->vx=ndx*il*19.0f; b->vy=ndy*il*19.0f; b->vz=ndz*il*19.0f;
    b->owner=1; b->life=6.0f;
    /* a clean deflection is also a free round: bat the bullet back AND pocket
     * the brass — rewards aggressive katana play, capped like any pickup */
    if(pammo<PLAYER_MAX_AMMO)pammo++;
    spawn_parts(6,b->x,b->y,b->z,2.0f, 0.4f,1.2f,1.8f);
    add_templ(b->x,b->y,b->z,4.0f,0.12f, 0.8f,2.4f,2.6f);
    sfx3(V_DEFLECT,0.9f+frand()*0.25f,b->x,b->y,b->z);
    if(hitstop<0.035f)hitstop=0.035f;   /* shorter than a kill: parries chain */
  }
}

/* ---------------------------------------------------------------- bullets */
static void update_bullets(float wdt){
  for(int i=0;i<MAXBUL;i++){
    Bullet*b=&bul[i]; if(!b->on)continue;
    b->life-=wdt; if(b->life<=0){b->on=0;continue;}
    float spd=sqrtf(b->vx*b->vx+b->vy*b->vy+b->vz*b->vz);
    float step=spd*wdt;
    int nsub=(int)(step/0.22f)+1;
    float sdt=wdt/nsub;
    for(int s=0;s<nsub && b->on;s++){
      b->x+=b->vx*sdt; b->y+=b->vy*sdt; b->z+=b->vz*sdt;
      float moved=spd*sdt;
      /* trail breadcrumb every 0.30 units of travel */
      b->trd+=moved;
      if(b->trd>0.30f){ b->trd=0;
        b->tr[b->th][0]=b->x; b->tr[b->th][1]=b->y; b->tr[b->th][2]=b->z;
        b->th=(b->th+1)%TRAILN; if(b->tn<TRAILN)b->tn++;
      }
      if(b->range>=0){
        b->range-=moved;
        if(b->range<=0){
          spawn_parts(4,b->x,b->y,b->z,1.6f, 0.25f,1.0f,0.55f);
          add_templ(b->x,b->y,b->z,2.2f,0.06f, 0.35f,1.7f,0.8f);
          b->on=0; break;
        }
      }
      /* walls / floors (raised or not) / ceiling */
      if(b->y>wallh-0.03f ||
         b->y<cellh((int)floorf(b->x/CELL),(int)floorf(b->z/CELL))+0.03f){
        spawn_parts(7,b->x,b->y,b->z,2.4f, 0.3f,1.1f,0.6f);
        add_templ(b->x,b->y,b->z,3.0f,0.10f, 0.5f,2.0f,1.0f);
        b->on=0; break;
      }
      if(b->owner==0){ /* enemy round vs player capsule — roll under it or
                          jump it: ankle grace makes the leap readable */
        float dx=b->x-px,dz=b->z-pz;
        float ph=player_height();
        if(dx*dx+dz*dz<0.34f*0.34f && b->y>py+0.28f && b->y<py+ph){
          hurt_player(22);
          spawn_parts(8,b->x,b->y,b->z,2.0f, 1.2f,0.3f,0.2f);
          b->on=0; break;
        }
        /* near miss: doppler whoosh, pitch from closing speed */
        float d2=dx*dx+dz*dz;
        if(d2<1.3f*1.3f && d2>1.0f && (b->vx*dx+b->vz*dz)>0 && frand()<0.5f)
          sfx3(V_WHOOSH, 0.7f+clampf(spd/14.0f,0,1)*0.8f, b->x,b->y,b->z);
      } else { /* player round vs agents */
        for(int j=0;j<nen;j++){
          Enemy*e=&en[j]; if(e->state==4)continue;
          float dx=b->x-e->x,dz=b->z-e->z;
          /* boss is a much larger body: widen its hit capsule accordingly */
          float hr=e->type==2?1.5f:0.45f, htop=e->type==2?5.0f:2.0f;
          if(dx*dx+dz*dz<hr*hr && b->y>e->y && b->y<e->y+htop){
            damage_enemy(e,1);
            b->on=0; break;
          }
        }
      }
    }
  }
}

/* ---------------------------------------------------------------- boss AI
 * The OVERLORD fights in three escalating phases keyed to its hit bar:
 *   p0 (>66%): a lazy 3-arm spiral fountain, occasional leaps, orbits at range.
 *   p1 (>33%): denser 5-arm spiral, closes tighter, leaps more often.
 *   p2 (<33%): enraged 6-arm twin counter-rotating spiral, frequent slam-leaps
 *              that detonate a radial burst on landing, hunts the player to melee.
 * Everything advances on world-time (wdt), so the bullet-hell freezes when the
 * player holds still and quickens as they platform — the SUPERHOT contract. */
static void update_boss(Enemy*e,float wdt){
  float dx=px-e->x, dz=pz-e->z, d=sqrtf(dx*dx+dz*dz)+1e-4f;
  if(e->flash>0)e->flash-=wdt;
  float hpf=(float)e->hp/(float)(bossMaxHp>0?bossMaxHp:1);
  { int nph = hpf>0.66f?0 : hpf>0.33f?1 : 2;
    if(nph!=e->bphase){
      /* phase escalation used to happen silently mid-volley — roar it out:
       * flash, shockwave ring of slow rounds, shake, and a beat of quiet
       * before the denser pattern opens up */
      e->bphase=nph;
      e->roar=1.0f; shake=0.75f; e->atkCD=1.0f; e->phase=1.2f;
      add_templ(e->x,e->y+2.5f,e->z,20.0f,0.5f, 1.4f,0.4f,1.8f);
      sfx3(V_SHATTER,0.35f,e->x,e->y+2.5f,e->z);
      for(int k=0;k<16;k++){ float a=k*PI/8;
        spawn_bullet(e->x,e->y+1.2f,e->z, sinf(a),0.05f,-cosf(a), 7.0f,0,-1); }
    } }
  int   arms = e->bphase==0?3 : e->bphase==1?5 : 6;
  float spin = e->bphase==0?1.4f : e->bphase==1?2.0f : 2.6f;
  float cad  = e->bphase==0?0.55f : e->bphase==1?0.45f : 0.36f;
  float bspd = 6.5f + e->bphase*1.5f;
  float my   = e->y+0.9f;        /* fire low so the spiral sweeps the floor   */

  /* vertical lead toward the player's torso. when they climb a corner pyramid
     or hang off the map edge to grab a cache, the boss pitches its fire up so
     the high ground stops being a free sniping perch. */
  float aimY    = py+0.9f;
  float vslope  = (aimY-my)/d;                 /* rise-over-run to the player  */
  int   elevated= py > e->y+1.2f;
  /* free-running rhythm clock: the fountain fires in waves with a short rest,
     so the floor always has a timed pocket to advance or grab a cache through
     — the breather that keeps it a dodgeable hell, not a solid wall of lead.  */
  e->state_t += wdt;
  float beatT  = e->bphase==2?2.6f:3.0f;
  int   resting= fmodf(e->state_t,beatT) > beatT-(e->bphase==2?0.45f:0.65f);

  /* Eased, not assigned. Every other pose channel on Enemy is a follower —
   * moveb 8/s, fwdb/latb 6/s, headYaw 6/s, flare 7/s, armp 8/s — and yaw, the
   * one the entire silhouette hangs off, was a hard atan2. Sprint past an agent
   * and its body rotated 150 degrees in a frame while its arms, coat and head
   * look all lagged their own rates: the figure visibly sheared. Worse, headYaw's
   * target is a DELTA from yaw, so a snap flipped its sign and the skull then
   * counter-swept for a quarter second chasing a body that had already arrived.
   * The boss turns slowly in the air so a leap commits to its direction. */
  { int inAir=(e->y>ground_h(e->x,e->z,e->y)+0.05f)||e->vy>0.01f;
    e->yaw=angto(e->yaw, atan2f(dx,-dz), wdt*(inAir?1.5f:5.0f)); }
  /* stride drivers: moveb is a smoothed 0..1 walk blend, anim the gait phase —
     its cadence rises with ground speed so the legs/arms read the movement */
  float spd2=sqrtf(e->vx*e->vx+e->vz*e->vz);
  e->moveb=toward(e->moveb, clampf(spd2/2.2f,0,1), wdt*6.0f);
  e->anim+=wdt*(3.0f+spd2*2.2f);
  e->roar=toward(e->roar, e->state==1?1.0f:(e->state==3?0.7f:0.15f), wdt*6.0f);
  /* smoothed pose blends so leaps ease in and OUT instead of snapping: armp is
     the airborne-pose amount (arms up / knees tucked), recoil the landing crouch
     that decays after touchdown. Both advance on world-time, freezing with it. */
  e->armp=toward(e->armp, e->state==1?1.0f:0.0f, wdt*8.0f);
  if(e->recoil>0){ e->recoil-=wdt*2.2f; if(e->recoil<0)e->recoil=0; }

  /* leap physics: gravity + a slam burst on touchdown */
  float gh=ground_h(e->x,e->z,e->y);
  int airborne = (e->y>gh+0.05f) || e->vy>0.01f;
  if(airborne){
    e->vy-=20.0f*wdt; e->y+=e->vy*wdt;
    /* clear low platforms on the arc, but never phase through a full-height
       wall — a boss that lands inside a cover pillar can't walk back out and
       the sector becomes unwinnable */
    move_circ(&e->x,&e->z, e->vx*wdt, e->vz*wdt, 0.9f, e->y);
    if(e->y<=gh){
      e->y=gh; e->vy=0; e->vx=e->vz=0; e->state=0; e->recoil=1.0f; shake=0.55f;
      sfx3(V_SHATTER,0.55f,e->x,e->y,e->z);
      add_templ(e->x,e->y+0.5f,e->z,9.0f,0.30f, 1.0f,0.5f,1.4f);
      for(int k=0;k<arms*2;k++){ float a=k*PI/arms;
        spawn_bullet(e->x,e->y+0.7f,e->z, sinf(a),0.04f,-cosf(a), bspd*0.9f,0,-1); }
    }
  } else {
    e->y=toward(e->y, gh, wdt*10.0f);
  }
  if(wdt>1e-4f){ e->vx=airborne?e->vx:(e->x-e->lx)/wdt; e->vz=airborne?e->vz:(e->z-e->lz)/wdt; }
  e->lx=e->x; e->lz=e->z;

  /* the spiral fountain — the dodgeable base pattern. its arms leave rotating
     gaps to thread between; when the player takes the high ground the whole
     cone tilts up to follow them. it falls silent during the rest beat so a
     navigable pocket opens on the floor every cycle. */
  e->spiralA += wdt*spin;
  e->atkCD   -= wdt;
  if(!airborne && !resting && e->atkCD<=0){
    e->atkCD=cad;
    float sv = elevated ? clampf(vslope*0.6f,0.0f,1.4f) : 0.02f;
    for(int k=0;k<arms;k++){ float a=e->spiralA + k*2*PI/arms;
      spawn_bullet(e->x,my,e->z, sinf(a),sv,-cosf(a), bspd,0,-1); }
    if(e->bphase==2)
      for(int k=0;k<arms;k++){ float a=-e->spiralA + k*2*PI/arms + 0.3f;
        spawn_bullet(e->x,my,e->z, sinf(a),sv,-cosf(a), bspd*0.85f,0,-1); }
    sfx3(V_ESHOT,0.6f,e->x,my,e->z);
  }

  /* aimed lance — the only volley with true vertical tracking. a tight fan
     loosed straight at the player and pitched to their height, so a perch or
     an over-the-edge ledge no longer dodges the boss. it keeps firing through
     the rest beat (the floor breather is for the spiral, not a free pass) and
     comes tighter and twice as often when the player is elevated, to deny
     camping the ammo pyramids. the fan's own gaps stay dodgeable by strafing. */
  e->phase -= wdt;
  if(!airborne && e->phase<=0){
    e->phase = (e->bphase==0?1.9f:e->bphase==1?1.5f:1.1f) * (elevated?0.55f:1.0f);
    int   N      = e->bphase==0?3:5;
    float base   = atan2f(dx,-dz);
    float spread = elevated?0.10f:0.17f;
    for(int k=0;k<N;k++){ float a=base+(k-(N-1)*0.5f)*spread;
      spawn_bullet(e->x,my+0.5f,e->z, sinf(a),vslope,-cosf(a), bspd*1.2f,0,-1); }
    sfx3(V_ESHOT,0.72f,e->x,my,e->z);
  }

  /* periodic leap toward the player */
  e->jumpCD -= wdt;
  if(!airborne && e->jumpCD<=0 && d>3.0f){
    e->vy=9.5f; e->state=1;
    float hop=clampf(d*0.55f,3.0f,9.0f);
    e->vx=dx/d*hop; e->vz=dz/d*hop;
    e->jumpCD = (e->bphase==2?2.6f:4.6f)+frand()*1.6f;
  }

  if(!airborne){
    /* orbit toward an ideal range, closer and angrier as phases advance */
    float want = e->bphase==2?5.0f:9.0f;
    float mx,mz;
    if(d>want+2.0f){ mx=dx/d; mz=dz/d; }
    else if(d<want-2.0f){ mx=-dx/d; mz=-dz/d; }
    else { mx=-dz/d; mz=dx/d; }
    float spd=1.6f+e->bphase*0.7f;
    move_circ(&e->x,&e->z, mx*spd*wdt, mz*spd*wdt, 0.9f, e->y);
    /* melee swat: a real swing, not the old contact aura (which ticked
     * 26-34 damage per mercy window with zero telegraph, and out-ranged the
     * katana). Entering reach starts a wound-up state-3 swat — the existing
     * lean/claw-hook pose IS the telegraph — and the blow lands only if the
     * player is still inside 2.6u when it falls, comfortably inside the
     * katana's 3.0 reach so a counter window exists. Then the arm needs a
     * recovery before the next swing. All on world-time: freezing time
     * freezes the wind-up mid-swing, exactly like a hanging bullet. */
    if(e->state==3){
      e->melT-=wdt;
      if(e->melT<=0){
        if(d<2.6f && fabsf(py-e->y)<2.5f) hurt_player(e->bphase==2?34:26);
        e->state=0; e->melT=1.2f;
      }
    } else {
      if(e->melT>0) e->melT-=wdt;
      if(e->melT<=0 && d<3.0f && fabsf(py-e->y)<2.5f){
        e->state=3; e->melT=e->bphase==2?0.32f:0.45f;
      }
    }
  }

  /* keep the boss off the player: back it out when they overlap (same
     convention the agents use — the figure yields, not the player) */
  if(d<2.0f){ float push=(2.0f-d)*0.5f; e->x-=dx/d*push; e->z-=dz/d*push; }
}

/* ---------------------------------------------------------------- agent AI
 * Shooters keep 4-14u of range, telegraph with an eye-flare aim phase, then
 * loose a slow round at where you ARE — stand still and it still creeps at
 * you, because the world only freezes, never stops. Strikers just run you
 * down and lunge. All of it advances on world-time. */
static void update_enemies(float wdt){
  /* aim tokens: only a few agents may draw on you at once, so dense
   * sectors stay a readable bullet hell instead of a firing squad */
  float diff=(float)LEVELS[curlevel].tier;
  float moveMul=1.0f+0.10f*diff;
  float aimTime=0.50f-0.07f*diff;
  float coolBase=0.70f-0.08f*diff;
  float shotMul=1.0f+0.10f*diff;
  float lungeWind=0.32f-0.045f*diff;
  int maxAim=4+(LEVELS[curlevel].tier>=2);
  int aimers=0;
  for(int i=0;i<nen;i++) if(en[i].state==1&&en[i].type!=2)aimers++; /* boss leaps reuse state 1 */
  for(int i=0;i<nen;i++){
    Enemy*e=&en[i];
    if(e->type==2){ if(e->state!=4)update_boss(e,wdt); continue; }
    if(e->flash>0)e->flash-=wdt;
    /* velocity estimate for the Doppler tint */
    /* 1e-3, not 1e-4: this is a finite difference divided by wdt, and wdt goes
     * to ~3e-4 at MINTS. Float noise in a coordinate of magnitude 40 then becomes
     * ~1% velocity noise, which lands straight in the stride amplitude — foot
     * jitter in exactly the frozen pose the player stares at longest. */
    if(wdt>1e-3f){ e->vx=(e->x-e->lx)/wdt; e->vz=(e->z-e->lz)/wdt; }
    e->lx=e->x; e->lz=e->z;
    if(e->state==4)continue;
    /* settle onto whatever floor is underfoot (stairs, platforms). Stepping UP
     * is an eased glide — that is what makes stairs read smoothly — but stepping
     * OFF is a real fall on the boss's gravity, so leaving a train roof arcs
     * instead of gliding down like an elevator. */
    { float gh=ground_h(e->x,e->z,e->y);
      if(e->y > gh+0.03f){ e->vy-=20.0f*wdt; e->y+=e->vy*wdt;
                           if(e->y<=gh){ e->y=gh; e->vy=0; } }
      else { e->y=toward(e->y,gh,wdt*10.0f); e->vy=0; } }
    /* animation blends, fed from real velocity so they're honest in every
     * state: walk amount + local move direction (forward vs lateral) */
    float sp2=sqrtf(e->vx*e->vx+e->vz*e->vz);
    e->moveb=toward(e->moveb, clampf(sp2/2.2f,0,1), wdt*8.0f);
    /* Attack-instant, release-eased speed. The stride's HALF-WIDTH is driven by
     * speed, and speed is a one-frame finite difference — so the frame an agent
     * stopped (entering the aim state stops it dead) both feet teleported to
     * directly under its hips and the knees kicked as the IK re-converged.
     * moveb was already smoothed but only multiplies the swing-foot LIFT, never
     * the horizontal sweep that owns the pose. Rising edge passes through
     * untouched, so the cadence law still cancels travel exactly and the planted
     * foot does not skate; falling edge trails ~0.14s and reads as weight
     * settling. Must feed the phase RATE and the half-stride both, or the
     * cancellation breaks. */
    e->spdS = sp2>e->spdS ? sp2 : toward(e->spdS,sp2,wdt*7.0f);
    if(sp2>0.1f){
      float ivx=e->vx/sp2, ivz=e->vz/sp2;
      e->fwdb=toward(e->fwdb, ivx*sinf(e->yaw)-ivz*cosf(e->yaw), wdt*6.0f);
      e->latb=toward(e->latb, ivx*cosf(e->yaw)+ivz*sinf(e->yaw), wdt*6.0f);
    }
    /* travel-locked cadence: the stride phase advances with the ground the
     * agent actually covered, not with its commanded speed. A blocked agent
     * now stops walking on the spot, and the planted foot in draw_agent's IK
     * tracks the floor. Same rate law as the avatar's bobT (see draw_player):
     * phase rate 4.65/unit pairs with a 0.33 half-stride to cancel skate. */
    e->anim += wdt*(0.9f + 4.65f*e->spdS);
    e->flare=toward(e->flare, e->state==1? sstep(e->armp):0.0f,
                    wdt*(e->state==1?7.0f:3.5f));
    if(e->recoil>0){ e->recoil-=wdt*3.5f; if(e->recoil<0)e->recoil=0; }
    /* Lunge follow-through. On the frame the striker left state 3 its arm went
     * from 1.85 rad to 0 and its torso from 0.62 to 0 — both in one frame, every
     * single lunge. 3.2/s recovers in 0.31s, inside the cooldown, so the arm is
     * always home before the next advance. */
    if(e->lunRel>0){ e->lunRel-=wdt*3.2f; if(e->lunRel<0)e->lunRel=0; }
    if(e->mzT>0){ e->mzT-=wdt; if(e->mzT<0)e->mzT=0; }
    /* the gun arm eases down in every state that isn't actively aiming —
     * state 1 overwrites this absolutely each frame. The LOS-abort path
     * (aim -> advance) used to skip the decay entirely, leaving the agent
     * patrolling with its arm welded at full raise, pistol out, forever. */
    e->armp-=wdt*2; if(e->armp<0)e->armp=0;

    float dx=px-e->x, dz=pz-e->z, d=sqrtf(dx*dx+dz*dz);
    if(d<1e-4f)d=1e-4f;   /* every branch below divides by d; a perfect overlap
                             would otherwise poison the agent's pose with NaN */
    int see=los(e->x,e->y+1.55f,e->z, px,py+1.2f,pz);
    /* head-look: the skull tracks the player even while the body strafes or
     * cools down — turns the mannequins from statues into things that watch
     * you. headYaw is the clamped delta from the body's facing. */
    { float want=atan2f(dx,-dz)-e->yaw;
      want=fmodf(want+PI,2*PI); if(want<0)want+=2*PI; want-=PI;
      want=clampf(want,-0.75f,0.75f);
      float wp=clampf(atan2f((py+1.2f)-(e->y+1.78f), d>0.4f?d:0.4f),-0.5f,0.5f);
      e->headYaw=toward(e->headYaw,want,wdt*6.0f);
      e->headPitch=toward(e->headPitch,wp,wdt*6.0f); }
    float spd = (e->type==1?4.4f:2.6f)*moveMul;
    switch(e->state){
      case 0:{ /* advance / reposition */
        e->yaw=angto(e->yaw, atan2f(dx,-dz), wdt*8.0f);   /* patrol turn ~0.13s */
        float mx=0,mz=0;
        if(e->type==1||!see||d>13.0f){ mx=dx/d; mz=dz/d; }
        else if(d<4.0f){ mx=-dx/d; mz=-dz/d; }
        else { float sgn=sinf(wtime*0.7f+e->phase)>0?1:-1;   /* strafe */
               mx=-dz/d*sgn; mz=dx/d*sgn; spd*=0.7f; }
        float ox=e->x,oz=e->z;
        move_circ(&e->x,&e->z,mx*spd*wdt,mz*spd*wdt,0.32f,e->y);
        if(fabsf(e->x-ox)+fabsf(e->z-oz) < spd*wdt*0.25f){
          float a=e->yaw+(sinf(wtime*3+e->phase)>0?1.4f:-1.4f);
          move_circ(&e->x,&e->z,sinf(a)*spd*wdt,-cosf(a)*spd*wdt,0.32f,e->y);
        }
        /* never walk off a high edge (train roofs, the mezzanine) */
        if(e->y-ground_h(e->x,e->z,e->y) > AGENT_DROP){ e->x=ox; e->z=oz; }
        e->state_t+=wdt;
        if(e->type==0){ if(see&&d<15.0f&&d>3.0f&&e->state_t>0&&aimers<maxAim){
                          e->state=1; e->state_t=0; aimers++; } }
        else          { if(d<1.7f&&fabsf(py-e->y)<1.2f&&e->state_t>0){ e->state=3; e->state_t=0; } }
      } break;
      case 1: /* aim: eyes flare, arm rises */
        e->yaw=angto(e->yaw, atan2f(dx,-dz), wdt*13.0f);  /* aim must be crisp  */
        e->state_t+=wdt;
        e->armp=clampf(e->state_t/0.25f,0,1);
        if(!see){ e->state=0; e->state_t=0; break; }
        if(e->state_t>aimTime){
          /* a three-round fan, aimed at your stance RIGHT NOW —
           * roll under it, jump over it, or stand still and study it */
          /* fire from the drawn RIGHT hand (shoulder +0.29 lateral, arm
           * raised forward), not the old chest-centreline point — the flash
           * and rounds now leave the pistol the pose is holding */
          float my=e->y+1.62f, aimY=py+1.28f;
          float hx=e->x+sinf(e->yaw)*0.55f+cosf(e->yaw)*0.29f;
          float hz=e->z-cosf(e->yaw)*0.55f+sinf(e->yaw)*0.29f;
          for(int k=-1;k<=1;k++){
            float sp=k*0.14f+(frand()-0.5f)*0.03f;
            float bdx=dx+(-dz)*sp, bdz=dz+dx*sp;
            spawn_bullet(hx,my,hz,bdx,aimY-my,bdz,(7.0f+frand()*2.5f)*shotMul,0,-1);
          }
          e->recoil=1.0f;
          /* agent fire had a temp light but no sprite: in a black sector the
           * rounds appeared out of nothing. Same two-billboard flash the
           * avatar gets, burning at the agent's own muzzle. */
          e->mzT=0.07f; e->mzx=hx; e->mzy=my; e->mzz=hz;
          sfx3(V_ESHOT,0.9f+frand()*0.2f,hx,my,hz);
          add_templ(hx,my,hz,4.0f,0.10f, 2.0f,1.2f,0.5f);
          e->state=2; e->state_t=-(coolBase+frand()*0.5f);
        } break;
      case 2: /* cooldown: drift sideways. Duration was rolled at entry (the
                 state_t below starts negative) — re-rolling frand() every
                 frame biased the length AND drew from the sim stream at a
                 framerate-dependent rate. */
        e->state_t+=wdt;
        { float sgn=sinf(e->phase*9)>0?1:-1;
          move_circ(&e->x,&e->z,-dz/d*sgn*1.8f*wdt,dx/d*sgn*1.8f*wdt,0.32f,e->y); }
        if(e->state_t>0){ e->state=0; e->state_t=0; }
        break;
      case 3: /* striker lunge: wind up, then hurl the body through the blow */
        /* Slow on purpose: an infinite turn rate made the lunge a homing missile
         * with no dodge window. The root motion below now follows e->yaw rather
         * than the live bearing, so the striker COMMITS and a sidestep beats it
         * — the counterplay the 0.18s follow-through was written for. */
        e->yaw=angto(e->yaw, atan2f(dx,-dz), wdt*6.0f);
        e->state_t+=wdt;
        if(e->state_t>=lungeWind){
          /* root motion through the strike: the throw pose used to play out
           * at a standstill — now the body carries the blow. Same edge guard
           * as the walk, so a dash never strands an agent off a roof. */
          float ox=e->x,oz=e->z;
          move_circ(&e->x,&e->z, sinf(e->yaw)*7.0f*wdt, -cosf(e->yaw)*7.0f*wdt, 0.32f, e->y);
          if(e->y-ground_h(e->x,e->z,e->y) > AGENT_DROP){ e->x=ox; e->z=oz; }
          if(!e->struck){
            float ndx=px-e->x, ndz=pz-e->z, nd=sqrtf(ndx*ndx+ndz*ndz);
            if(nd<2.0f && fabsf(py-e->y)<1.3f)hurt_player(26);
            e->struck=1;
          }
        }
        /* The old version dropped straight into cooldown on the strike frame,
         * which made draw_agent's forward-thrust pose unreachable — every lunge
         * you ever saw was pure wind-up. Hold the state through a short
         * follow-through so the blow itself reads. */
        if(e->state_t>lungeWind+0.18f){ e->lunRel=1.0f; e->state=2;
          e->state_t=0.4f-(coolBase+frand()*0.5f); e->struck=0; }
        break;
    }
    if(e->state!=4 && d<0.7f && d>0.001f && fabsf(py-e->y)<1.5f){
      e->x-=dx/d*(0.7f-d); e->z-=dz/d*(0.7f-d);
    }
  }
}

/* ---------------------------------------------------------------- drawing */
/* ------------------------------------------------------------ frustum culling
 * The only visibility test in this file used to be radial XZ distance, so at a
 * 102-degree horizontal FOV roughly two thirds of the agents inside the 38-unit
 * draw disc were BEHIND the camera and drawn in full — 34 shapes and ~125 GL
 * calls each, plus a set_lights selection, twice (mirror pass and upright).
 *
 * Gribb-Hartmann: the six clip planes fall straight out of the rows of
 * projection*modelview, which fixed-function GL will just hand us. Built once
 * per frame, right after the view matrix is set. */
static float frP[6][4];
static int cullTested=0, cullSkipped=0;   /* smoke reports the cull's actual yield */
static void frustum_build(void){
  float p[16],m[16],c[16];
  glGetFloatv(GL_PROJECTION_MATRIX,p);
  glGetFloatv(GL_MODELVIEW_MATRIX,m);
  /* GL matrices are column-major: element (row r, col k) is at [k*4+r]. */
  for(int col=0;col<4;col++)for(int r=0;r<4;r++){
    float s=0; for(int k=0;k<4;k++) s+=p[k*4+r]*m[col*4+k];
    c[col*4+r]=s;
  }
  #define ROW(r) c[0*4+(r)],c[1*4+(r)],c[2*4+(r)],c[3*4+(r)]
  float rw[4]={ROW(3)}, rx[4]={ROW(0)}, ry[4]={ROW(1)}, rz[4]={ROW(2)};
  #undef ROW
  for(int i=0;i<4;i++){
    frP[0][i]=rw[i]+rx[i];  frP[1][i]=rw[i]-rx[i];   /* left  / right */
    frP[2][i]=rw[i]+ry[i];  frP[3][i]=rw[i]-ry[i];   /* bottom/ top   */
    frP[4][i]=rw[i]+rz[i];  frP[5][i]=rw[i]-rz[i];   /* near  / far   */
  }
  for(int j=0;j<6;j++){
    float l=sqrtf(frP[j][0]*frP[j][0]+frP[j][1]*frP[j][1]+frP[j][2]*frP[j][2]);
    if(l>1e-9f){ frP[j][0]/=l; frP[j][1]/=l; frP[j][2]/=l; frP[j][3]/=l; }
  }
}
/* conservative: a sphere is kept if it is inside or straddling every plane */
static int frustum_sphere(float x,float y,float z,float r){
  cullTested++;
  for(int j=0;j<6;j++)
    if(frP[j][0]*x+frP[j][1]*y+frP[j][2]*z+frP[j][3] < -r){ cullSkipped++; return 0; }
  return 1;
}
/* The mirror pass draws figures y-flipped about the floor, so a figure whose
 * upright sphere is off screen may still have a visible reflection (and vice
 * versa). Test the reflected sphere for that pass. */
static int frustum_sphere_m(float x,float y,float z,float r,int mirrored){
  return frustum_sphere(x, mirrored? -y : y, z, r);
}

/* figure bounding sphere: centre and radius by type, so one place decides it */
static void fig_sphere(const Enemy*e,float*cy,float*r){
  if(e->type==2){ *cy=e->y+2.3f; *r=3.2f; } else { *cy=e->y+1.0f; *r=1.35f; }
}
static int refl=0;   /* mirror pass: flip Y of every model transform */
static void set_uM(const float*m,float tx,float ty,float tz){
  if(refl){ float f[9]; memcpy(f,m,36); f[1]=-f[1]; f[4]=-f[4]; f[7]=-f[7];
    glUniformMatrix3fv(uM3,1,GL_FALSE,f); glUniform3f(uT,tx,-ty,tz); }
  else { glUniformMatrix3fv(uM3,1,GL_FALSE,m); glUniform3f(uT,tx,ty,tz); }
}
/* Every figure colour funnels through these two so the mirror pass can FADE a
 * figure out as it lifts off the floor. The old pass cut the reflection at a
 * hard y>0.2, which popped it out of existence mid-jump while the contact
 * shadow right beside it faded smoothly across 2.2u of lift. Emissives (eyes,
 * blade, health spine) ride the same dim, so nothing survives the fade. */
static float figDim=1.0f;
static void tintf(float r,float g,float b){ glUniform3f(uTint,r*figDim,g*figDim,b*figDim); }
static void rimf (float r,float g,float b){ glUniform3f(uRimCol,r*figDim,g*figDim,b*figDim); }
/* display-list cache: the humanoids redraw the same handful of fixed shapes
 * hundreds of times a frame; bake each unique signature once and replay it.
 * Armed only inside the figure draws — shards/items have randomized sizes
 * that would churn the cache. Returns 0 emit, 1 replayed, 2 emit-and-record. */
static int primArm=0;
#define MAXPRIM 128
static struct { float p[3]; int seg,ring,type; GLuint l; } primc[MAXPRIM];
static int nprim;
static int prim_open(int type,float a,float b,float c,int seg,int ring){
  if(!primArm)return 0;
  for(int i=0;i<nprim;i++)
    if(primc[i].type==type&&primc[i].seg==seg&&primc[i].ring==ring
       &&primc[i].p[0]==a&&primc[i].p[1]==b&&primc[i].p[2]==c){
      glCallList(primc[i].l); return 1; }
  if(nprim>=MAXPRIM)return 0;
  GLuint l=glGenLists(1); if(!l)return 0;
  primc[nprim].type=type; primc[nprim].seg=seg; primc[nprim].ring=ring;
  primc[nprim].p[0]=a; primc[nprim].p[1]=b; primc[nprim].p[2]=c;
  primc[nprim].l=l; nprim++;
  glNewList(l,GL_COMPILE_AND_EXECUTE);
  return 2;
}

static void box_sh(float sx,float sy,float sz){ /* shader-lit box, centred */
  int pc=prim_open(0,sx,sy,sz,0,0); if(pc==1)return;
  float x=sx*0.5f,y=sy*0.5f,z=sz*0.5f;
  glBegin(GL_QUADS);
  glNormal3f(0,0,1);  glTexCoord2f(0.5f,0.5f);
  glVertex3f(-x,-y,z); glVertex3f(x,-y,z); glVertex3f(x,y,z); glVertex3f(-x,y,z);
  glNormal3f(0,0,-1);
  glVertex3f(x,-y,-z); glVertex3f(-x,-y,-z); glVertex3f(-x,y,-z); glVertex3f(x,y,-z);
  glNormal3f(1,0,0);
  glVertex3f(x,-y,z); glVertex3f(x,-y,-z); glVertex3f(x,y,-z); glVertex3f(x,y,z);
  glNormal3f(-1,0,0);
  glVertex3f(-x,-y,-z); glVertex3f(-x,-y,z); glVertex3f(-x,y,z); glVertex3f(-x,y,-z);
  glNormal3f(0,1,0);
  glVertex3f(-x,y,z); glVertex3f(x,y,z); glVertex3f(x,y,-z); glVertex3f(-x,y,-z);
  glNormal3f(0,-1,0);
  glVertex3f(-x,-y,-z); glVertex3f(x,-y,-z); glVertex3f(x,-y,z); glVertex3f(-x,-y,z);
  glEnd();
  if(pc==2)glEndList();
}
/* Chamfered box: 6 inset faces + 12 edge bevels + 8 corner tris, each with its
 * own flat normal. The cut edges catch the rim light, so blocky parts (health
 * spine, brow/jaw, pickups) read as faceted crystal instead of plain cuboids.
 * Culling is off, so winding is free; only the explicit normals matter. */
static void bevbox_sh(float sx,float sy,float sz,float bev){
  int pc=prim_open(4,sx,sy,sz,(int)(bev*1000.0f),0); if(pc==1)return;
  float x=sx*0.5f,y=sy*0.5f,z=sz*0.5f;
  float mn=x<y?(x<z?x:z):(y<z?y:z);
  float b=bev; if(b>mn*0.6f)b=mn*0.6f; if(b<0)b=0;
  float ix=x-b,iy=y-b,iz=z-b, r2=0.70710678f, r3=0.57735027f;
  glBegin(GL_QUADS); glTexCoord2f(0.5f,0.5f);
  glNormal3f(1,0,0);  glVertex3f(x,-iy,-iz);glVertex3f(x,iy,-iz);glVertex3f(x,iy,iz);glVertex3f(x,-iy,iz);
  glNormal3f(-1,0,0); glVertex3f(-x,-iy,-iz);glVertex3f(-x,iy,-iz);glVertex3f(-x,iy,iz);glVertex3f(-x,-iy,iz);
  glNormal3f(0,1,0);  glVertex3f(-ix,y,-iz);glVertex3f(ix,y,-iz);glVertex3f(ix,y,iz);glVertex3f(-ix,y,iz);
  glNormal3f(0,-1,0); glVertex3f(-ix,-y,-iz);glVertex3f(ix,-y,-iz);glVertex3f(ix,-y,iz);glVertex3f(-ix,-y,iz);
  glNormal3f(0,0,1);  glVertex3f(-ix,-iy,z);glVertex3f(ix,-iy,z);glVertex3f(ix,iy,z);glVertex3f(-ix,iy,z);
  glNormal3f(0,0,-1); glVertex3f(-ix,-iy,-z);glVertex3f(ix,-iy,-z);glVertex3f(ix,iy,-z);glVertex3f(-ix,iy,-z);
  for(int s=-1;s<=1;s+=2)for(int t=-1;t<=1;t+=2){
    glNormal3f(s*r2,t*r2,0);                       /* edges parallel to Z */
    glVertex3f(s*x,t*iy,-iz);glVertex3f(s*x,t*iy,iz);glVertex3f(s*ix,t*y,iz);glVertex3f(s*ix,t*y,-iz);
    glNormal3f(s*r2,0,t*r2);                       /* edges parallel to Y */
    glVertex3f(s*x,-iy,t*iz);glVertex3f(s*x,iy,t*iz);glVertex3f(s*ix,iy,t*z);glVertex3f(s*ix,-iy,t*z);
    glNormal3f(0,s*r2,t*r2);                       /* edges parallel to X */
    glVertex3f(-ix,s*y,t*iz);glVertex3f(ix,s*y,t*iz);glVertex3f(ix,s*iy,t*z);glVertex3f(-ix,s*iy,t*z);
  }
  glEnd();
  glBegin(GL_TRIANGLES); glTexCoord2f(0.5f,0.5f);
  for(int s=-1;s<=1;s+=2)for(int t=-1;t<=1;t+=2)for(int u=-1;u<=1;u+=2){
    glNormal3f(s*r3,t*r3,u*r3);
    glVertex3f(s*x,t*iy,u*iz); glVertex3f(s*ix,t*y,u*iz); glVertex3f(s*ix,t*iy,u*z);
  }
  glEnd();
  if(pc==2)glEndList();
}
/* one triangle with its true face normal (outward via the centroid test) —
 * wedge_sh's tapered sides are non-planar and their old axis-aligned normals
 * sat 15-25 degrees off, firing the rim/GGX on the wrong facets */
static void tri_n(float ax,float ay,float az,float bx,float by,float bz,
                  float cx,float cy,float cz){
  float ux=bx-ax,uy=by-ay,uz=bz-az, vx=cx-ax,vy=cy-ay,vz=cz-az;
  float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
  float mx=(ax+bx+cx),my_=(ay+by+cy),mz=(az+bz+cz);   /* 3x centroid */
  if(nx*mx+ny*my_+nz*mz<0){ nx=-nx;ny=-ny;nz=-nz; }
  float il=1.0f/sqrtf(nx*nx+ny*ny+nz*nz+1e-12f);
  glNormal3f(nx*il,ny*il,nz*il);
  glVertex3f(ax,ay,az); glVertex3f(bx,by,bz); glVertex3f(cx,cy,cz);
}
/* Angular tapered block: a cheap prism for hands/feet/gun facets so the
 * humanoids read as cut polygon mannequins instead of stacked cuboids. */
static void wedge_sh(float sx,float sy,float sz){
  int pc=prim_open(3,sx,sy,sz,0,0); if(pc==1)return;
  float x=sx*0.5f,y=sy*0.5f,z=sz*0.5f;
  float tx=x*0.72f,tzf=z*0.78f,tzb=z*0.48f;
  glBegin(GL_QUADS); glTexCoord2f(0.5f,0.5f);
  glNormal3f(0,-1,0);
  glVertex3f(-x,-y,-z); glVertex3f(x,-y,-z); glVertex3f(x,-y,z); glVertex3f(-x,-y,z);
  glNormal3f(0,1,0);
  glVertex3f(-tx,y,-tzb); glVertex3f(-tx*0.82f,y,tzf); glVertex3f(tx*0.82f,y,tzf); glVertex3f(tx,y,-tzb);
  glEnd();
  glBegin(GL_TRIANGLES); glTexCoord2f(0.5f,0.5f);
  /* each warped side split into two triangles with their own true normals */
  tri_n(-x,-y,z,  x,-y,z,  tx*0.82f,y,tzf);            /* front +z  */
  tri_n(-x,-y,z,  tx*0.82f,y,tzf, -tx*0.82f,y,tzf);
  tri_n( x,-y,-z, -x,-y,-z, -tx,y,-tzb);               /* back  -z  */
  tri_n( x,-y,-z, -tx,y,-tzb,  tx,y,-tzb);
  tri_n( x,-y,z,  x,-y,-z,  tx,y,-tzb);                /* right +x  */
  tri_n( x,-y,z,  tx,y,-tzb, tx*0.82f,y,tzf);
  tri_n(-x,-y,-z, -x,-y,z,  -tx*0.82f,y,tzf);          /* left  -x  */
  tri_n(-x,-y,-z, -tx*0.82f,y,tzf, -tx,y,-tzb);
  glEnd();
  if(pc==2)glEndList();
}
/* shader-lit tapered cylinder along local Y, centred. r0=bottom r1=top radius,
 * seg sides. ONE normal per facet (SUPERHOT cut): limbs read as crystal prisms,
 * each plane catching its own band of light. */
/* caps: bit0 = bottom disc, bit1 = top disc. Every limb joint is buried inside a
 * bead, a torso, a hand or a foot, so those discs are pure interior geometry —
 * and they were HALF of every segment's triangles (seg side tris + seg per cap).
 * prim_open's key is (type,a,b,c,seg,ring) and cyl_sh only ever passed ring=0,
 * so the flag rides along for free without splitting the display-list cache. */
static void cyl_shc(float r0,float r1,float h,int seg,int caps){
  int pc=prim_open(1,r0,r1,h,seg,caps); if(pc==1)return;
  float y0=-h*0.5f,y1=h*0.5f,dr=r1-r0;
  float nl=sqrtf(h*h+dr*dr); if(nl<1e-6f)nl=1;
  glBegin(GL_TRIANGLES); glTexCoord2f(0.5f,0.5f);
  for(int i=0;i<seg;i++){
    float a0=i*2*PI/seg,a1=(i+1)*2*PI/seg, am=(a0+a1)*0.5f;
    float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
    float bx0=r0*c0,bz0=r0*s0,tx0=r1*c0,tz0=r1*s0;
    float bx1=r0*c1,bz1=r0*s1,tx1=r1*c1,tz1=r1*s1;
    glNormal3f(cosf(am)*h/nl,-dr/nl,sinf(am)*h/nl);
    glVertex3f(bx0,y0,bz0); glVertex3f(bx1,y0,bz1); glVertex3f(tx1,y1,tz1);
    glVertex3f(bx0,y0,bz0); glVertex3f(tx1,y1,tz1); glVertex3f(tx0,y1,tz0);
    if((caps&1)&&r0>1e-4f){ glNormal3f(0,-1,0);
      glVertex3f(0,y0,0); glVertex3f(bx1,y0,bz1); glVertex3f(bx0,y0,bz0); }
    if((caps&2)&&r1>1e-4f){ glNormal3f(0,1,0);
      glVertex3f(0,y1,0); glVertex3f(tx0,y1,tz0); glVertex3f(tx1,y1,tz1); }
  }
  glEnd();
  if(pc==2)glEndList();
}
static void cyl_sh(float r0,float r1,float h,int seg){ cyl_shc(r0,r1,h,seg,3); }
/* one ellipsoid position (normal is emitted per patch, not per vertex) */
static void spos(float rx,float ry,float rz,float ph,float th){
  glVertex3f(rx*cosf(ph)*cosf(th), ry*sinf(ph), rz*cosf(ph)*sinf(th));
}
/* shader-lit faceted ellipsoid, centred. One analytic normal per patch,
 * evaluated at the patch centre — at low seg/ring it reads as a cut gem. */
static void sphere_sh(float rx,float ry,float rz,int seg,int ring){
  int pc=prim_open(2,rx,ry,rz,seg,ring); if(pc==1)return;
  glBegin(GL_TRIANGLES); glTexCoord2f(0.5f,0.5f);
  for(int j=0;j<ring;j++){
    float p0=PI*j/ring-PI*0.5f, p1=PI*(j+1)/ring-PI*0.5f, pm=(p0+p1)*0.5f;
    for(int i=0;i<seg;i++){
      float a0=2*PI*i/seg, a1=2*PI*(i+1)/seg, am=(a0+a1)*0.5f;
      float x=rx*cosf(pm)*cosf(am), y=ry*sinf(pm), z=rz*cosf(pm)*sinf(am);
      float nx=x/(rx*rx),ny=y/(ry*ry),nz=z/(rz*rz);
      float il=1.0f/sqrtf(nx*nx+ny*ny+nz*nz+1e-9f);
      glNormal3f(nx*il,ny*il,nz*il);
      /* At the poles both p0 (or both p1) samples collapse to the same point, so
       * one of the two triangles is degenerate — seg wasted triangles per pole,
       * 2*seg per sphere, and there are seven of these on every figure. Emit a
       * fan there instead of a quad. */
      int lo=cosf(p0)<1e-5f, hi=cosf(p1)<1e-5f;
      if(!lo){ spos(rx,ry,rz,p0,a0); spos(rx,ry,rz,p1,a1); spos(rx,ry,rz,p0,a1); }
      if(!hi){ spos(rx,ry,rz,p0,a0); spos(rx,ry,rz,p1,a0); spos(rx,ry,rz,p1,a1); }
      if(lo){  spos(rx,ry,rz,p0,a0); spos(rx,ry,rz,p1,a0); spos(rx,ry,rz,p1,a1); }
      else if(hi){ spos(rx,ry,rz,p0,a0); spos(rx,ry,rz,p1,a1); spos(rx,ry,rz,p0,a1); }
    }
  }
  glEnd();
  if(pc==2)glEndList();
}

/* bind a fixed model-local offset (ax,ay,az) added onto a base world point —
 * the m3v + set_uM idiom that precedes most figure draws. */
static void put(const float*M,float bx,float by,float bz,float ax,float ay,float az){
  float o[3]; m3v(M,ax,ay,az,o); set_uM(M,bx+o[0],by+o[1],bz+o[2]);
}
/* draw one tapered limb segment hanging off joint (jx,jy,jz) in basis L: bind at
 * the centre drop, emit the cylinder, and report the next joint at the end drop. */
/* A limb segment never shows either end: the proximal end is inside a torso or a
 * joint bead, the distal end inside the next bead, a hand or a foot. So it draws
 * with caps off. */
static void limb_seg(const float*L,float jx,float jy,float jz,float cdrop,
                     float r0,float r1,float h,int seg,float edrop,
                     float*nx,float*ny,float*nz){
  put(L,jx,jy,jz,0,cdrop,0); cyl_shc(r0,r1,h,seg,0);
  float e[3]; m3v(L,0,edrop,0,e); *nx=jx+e[0]; *ny=jy+e[1]; *nz=jz+e[2];
}
/* orthonormal basis (col-major) whose local -Y axis points along (dx,dy,dz):
 * aims a limb_seg from a joint straight at its child joint, for IK legs.
 * limb_seg hangs down local -Y, so local +Y maps to the reverse of the dir. */
static void aim_basis(float dx,float dy,float dz,float*M){
  float l=sqrtf(dx*dx+dy*dy+dz*dz); if(l<1e-5f){ m3id(M); return; }
  float yx=-dx/l, yy=-dy/l, yz=-dz/l;
  float rx=0,ry=0,rz=1; if(fabsf(yz)>0.85f){ rx=1; rz=0; }   /* ref not ∥ Y */
  float xx=ry*yz-rz*yy, xy=rz*yx-rx*yz, xz=rx*yy-ry*yx;       /* X = R×Y */
  float xl=sqrtf(xx*xx+xy*xy+xz*xz); if(xl<1e-5f)xl=1; xx/=xl;xy/=xl;xz/=xl;
  float zx=xy*yz-xz*yy, zy=xz*yx-xx*yz, zz=xx*yy-xy*yx;       /* Z = X×Y */
  M[0]=xx;M[1]=xy;M[2]=xz; M[3]=yx;M[4]=yy;M[5]=yz; M[6]=zx;M[7]=zy;M[8]=zz;
}
/* 2-bone IK: knee for a leg from hip H to foot T, bones L1/L2, bending toward
 * the world pole (px,pz) (knees forward). Writes the knee world position. */
static void ik2(float hx,float hy,float hz,float tx,float ty,float tz,
                float L1,float L2,float polex,float polez,
                float*kx,float*ky,float*kz){
  float dx=tx-hx,dy=ty-hy,dz=tz-hz;
  float dist=sqrtf(dx*dx+dy*dy+dz*dz); if(dist<1e-4f)dist=1e-4f;
  float ux=dx/dist,uy=dy/dist,uz=dz/dist;
  float d1=(dist*dist+L1*L1-L2*L2)/(2.0f*dist);
  float hh=L1*L1-d1*d1; hh=hh>0?sqrtf(hh):0;
  float pdot=polex*ux+polez*uz;                 /* project pole ⟂ to u */
  float nx=polex-ux*pdot, ny=-uy*pdot, nz=polez-uz*pdot;
  float nl=sqrtf(nx*nx+ny*ny+nz*nz);
  if(nl<1e-3f){ nx=0;ny=0;nz=1; nl=1; }
  nx/=nl;ny/=nl;nz/=nl;
  *kx=hx+ux*d1+nx*hh; *ky=hy+uy*d1+ny*hh; *kz=hz+uz*d1+nz*hh;
}

static void pistol_sh(const float*W,float gx,float gy,float gz,int ammo){
  float o[3],M2[9],RX[9];
  tintf(0.025f,0.030f,0.032f);
  /* The slide is a chamfered box, not a wedge. wedge_sh tapers inward toward +Y,
   * so the old slide was NARROWER on top than at the bottom — the opposite of a
   * real one, and it read as a doorstop. bevbox's cut edges also catch uRim,
   * which is the whole faceted-crystal language of these models. */
  m3v(W,0,-0.012f,-0.18f,o);
  set_uM(W,gx+o[0],gy+o[1],gz+o[2]); bevbox_sh(0.058f,0.056f,0.34f,0.011f);
  m3v(W,0,-0.010f,-0.38f,o);
  set_uM(W,gx+o[0],gy+o[1],gz+o[2]); box_sh(0.050f,0.045f,0.045f);
  m3rotX(RX,-0.58f); m3mul(M2,W,RX);
  m3v(W,0,-0.145f,-0.020f,o);
  set_uM(M2,gx+o[0],gy+o[1],gz+o[2]); wedge_sh(0.058f,0.210f,0.078f);
  /* trigger guard: one rotated chamfered box. Cheap, and it is most of what
   * separates a pistol silhouette from a rectangle with a handle. */
  { float GG[9],RG[9]; m3rotX(RG,0.22f); m3mul(GG,W,RG);
    m3v(W,0,-0.062f,-0.078f,o);
    set_uM(GG,gx+o[0],gy+o[1],gz+o[2]); bevbox_sh(0.046f,0.011f,0.072f,0.004f); }
  /* IRON SIGHTS. The entire ADS feature culminates in this object filling the
   * middle of a 26-degree frame about 40cm from the near plane, and it had no
   * sights at all — nothing to align, which is what an aim-down-sights blend is
   * FOR. Emissive so they read against a near-black slide. The front post sits
   * 0.26 further from the eye than the rear notch, which is enough parallax at
   * this scale to read as alignment; and because player_arm_r re-seats the whole
   * grip basis onto the look ray with m3align, the sights are automatically on
   * the round's real path. That is the game's stated guarantee, made visible. */
  glUniform1f(uEmis,0.55f); tintf(0.12f,0.95f,0.42f);
  m3v(W,0,0.040f,-0.315f,o);                          /* front post, at the muzzle */
  set_uM(W,gx+o[0],gy+o[1],gz+o[2]); box_sh(0.008f,0.030f,0.010f);
  for(int q=-1;q<=1;q+=2){                            /* rear notch: two blades    */
    m3v(W,q*0.017f,0.038f,-0.055f,o);
    set_uM(W,gx+o[0],gy+o[1],gz+o[2]); box_sh(0.009f,0.026f,0.014f);
  }
  tintf(0.025f,0.030f,0.032f);
  glUniform1f(uEmis,1.0f);
  int pip= ammo<6?ammo:6;
  for(int q=0;q<6;q++){
    float live=q<pip?1.0f:0.0f;
    tintf(0.05f+live*0.25f,0.18f+live*1.70f,0.08f+live*0.85f);
    m3v(W,-0.038f+q*0.015f,-0.045f,-0.185f,o);
    set_uM(W,gx+o[0],gy+o[1],gz+o[2]); box_sh(0.010f,0.024f,0.012f);
  }
  if(ammo>6){
    float rk=clampf((ammo-6)/12.0f,0,1);
    tintf(0.10f+0.20f*rk,0.55f+1.30f*rk,0.25f+0.55f*rk);
    m3v(W,0.065f,-0.010f,-0.060f,o);
    set_uM(W,gx+o[0],gy+o[1],gz+o[2]); box_sh(0.018f,0.085f*rk,0.018f);
  }
}

/* the OVERLORD: a hulking faceted alien ~4.5u tall, built from the same crystal
 * primitives but on a giant scale — splayed digitigrade legs, a bulbous thorax,
 * twin clawed arms, and a wide skull set with a cluster of eyes that burn from
 * emerald toward furnace-red as its phases escalate. */
static void draw_boss(Enemy*e){
  float die = e->state==4 ? 1.0f-clampf(e->dieT/0.30f,0,1) : 0.0f;
  if(e->state==4 && e->dieT<=0)return;
  primArm=1;
  float by=e->y;
  float M[9],R[9],X[9];
  /* a low forward hunch at rest, stooping further as it strides; rears back as
     it leaps (armp), then bows hard into the floor on impact (recoil) before
     recovering — never the upright T-stance */
  float crouch=e->recoil*e->recoil;                     /* eased landing absorb */
  float lean = 0.10f + 0.16f*e->moveb + 0.04f*sinf(wtime*1.2f+e->bphase);
  lean += -0.34f*e->armp + 0.45f*crouch;
  if(e->state==3) lean=0.34f;                           /* melee lunge overrides */
  m3rotY(R,-e->yaw); m3rotX(X,lean); m3mul(M,R,X);
  /* the collapse: squash down, bulge out. 0.30s for a 4.5-unit body, so the
     OVERLORD comes apart over a beat rather than blinking into confetti. */
  if(die>0) m3scl(M,1.0f+0.55f*die,1.0f-0.45f*die,1.0f+0.55f*die);

  float vr=radial_v(e->x,e->z,e->vx,e->vz);
  float dr,dg,db; dopp_rgb(vr,&dr,&dg,&db);
  float ph=(float)e->bphase;
  float fl=e->flash>0?0.7f:0.0f;
  /* bruised violet hide, reddening with each phase */
  float sr=0.18f+0.10f*ph + dr*0.10f;
  float sg=0.06f           + dg*0.08f;
  float sb=0.22f-0.06f*ph  + db*0.10f;
  glUniform1f(uBump,0); glUniform1f(uGloss,0.5f);
  glUniform1f(uEmis,0.10f+fl+0.05f*ph);
  tintf(sr+fl,sg+fl*0.6f,sb+fl*0.7f);
  glUniform1f(uRim,1.0f+0.4f*ph);
  rimf( 0.85f+0.65f*ph, 0.22f, 1.15f-0.45f*ph);
  /* the collapse blows the body out toward white as it comes apart, so the
   * shards look like they were shed by it rather than swapped in for it */
  float figDim0=figDim;              /* figDim belongs to the caller: restored below */
  if(die>0){ glUniform1f(uEmis,0.35f+0.90f*die); figDim*=1.0f+2.2f*die; }
  if(die>0){ glUniform1f(uEmis,0.35f+0.90f*die); figDim*=1.0f+2.2f*die; }
  float breath=1.0f+0.03f*sinf(wtime*2.2f+e->bphase);

  /* legs: two heavy two-bone limbs. A hip swing + knee bend drive a stride that
     scales with moveb; mid-leap the knees tuck up instead of pedalling. */
  for(int li=-1;li<=1;li+=2){
    float lph=e->anim+(li>0?0:PI);
    float ground=1.0f-e->armp;                                      /* 0 airborne..1 planted */
    float sw =sinf(lph)*0.55f*e->moveb*ground;                      /* hip fwd/back */
    float kb =(0.10f+0.55f*clampf(0.5f-0.5f*sinf(lph),0,1))*e->moveb*ground
              +0.55f*crouch;                                        /* + impact absorb */
    float tuck=0.75f*e->armp;
    float hip[3]; m3v(M,li*0.45f,0,0.05f,hip);
    float hx=e->x+hip[0], hy=by+1.65f-0.30f*crouch, hz=e->z+hip[2];
    float UL[9],RX[9],RZ[9],S[9];
    m3rotX(RX, sw+tuck); m3rotZ(RZ, li*0.14f); m3mul(S,RX,RZ); m3mul(UL,M,S);
    float kx,ky,kz; limb_seg(UL,hx,hy,hz,-0.46f, 0.30f,0.25f,0.95f,7, -0.92f, &kx,&ky,&kz);
    /* The bead bridges the two bones instead of sitting on them like a bead on a
     * string: it is 20% wider than either bone end, elongated ALONG the limb, and
     * bound in the LIMB basis so its facets share the bones' orientation. Bound
     * in the body basis (as all of these were) you saw three unrelated facet
     * patterns meeting at a point the moment the joint bent. */
    set_uM(UL,kx,ky,kz); sphere_sh(0.30f,0.34f,0.30f,6,4);
    float LL[9],RK[9]; m3rotX(RK, sw+kb+tuck*0.9f); m3mul(S,RK,RZ); m3mul(LL,M,S);
    float ax,ay,az; limb_seg(LL,kx,ky,kz,-0.42f, 0.25f,0.15f,0.86f,7, -0.84f, &ax,&ay,&az);
    put(LL,ax,ay,az,0,-0.04f,-0.22f); wedge_sh(0.34f,0.18f,0.55f);
  }
  /* thorax: barrel waist swelling into a bulbous chest. Everything above the
   * hips hangs off ONE pivot through the lean basis, so the hunting hunch,
   * the leap rear-back and the landing bow actually carry the mass forward
   * and down instead of rotating each part around its own fixed point. */
  float pvY=by+1.65f;
  float Tt[9]; memcpy(Tt,M,36); m3scl(Tt,breath,1.0f,breath*0.85f);
  glUniform3f(uNSc,breath,1,breath*0.85f);
  { float o[3]; m3v(M,0,0.40f,0,o);
    set_uM(Tt,e->x+o[0],pvY+o[1],e->z+o[2]); } cyl_sh(0.55f,0.72f,1.15f,10);
  { float o[3]; m3v(M,0,1.40f,0,o);
    set_uM(Tt,e->x+o[0],pvY+o[1],e->z+o[2]); } sphere_sh(0.80f,0.72f,0.66f,10,7);
  glUniform3f(uNSc,1,1,1);
  /* spine vents: a faint emissive ridge that brightens with phase */
  glUniform1f(uEmis,0.5f+0.5f*ph);
  tintf(0.6f+0.5f*ph,0.9f-0.3f*ph,0.4f);
  for(int sgi=0;sgi<3;sgi++){ put(M,e->x,pvY,e->z,0,0.85f+sgi*0.45f,0.45f); box_sh(0.05f,0.12f,0.08f); }
  glUniform1f(uEmis,0.10f+fl+0.05f*ph);
  tintf(sr+fl,sg+fl*0.6f,sb+fl*0.7f);

  /* arms: hang down-and-forward with only a slight outward splay (kills the
     T-pose), counter-swinging against the stride and never quite still; the
     lead claw hooks forward on a melee swat and both rear up before a leap */
  for(int ai=-1;ai<=1;ai+=2){
    float aph=e->anim+(ai>0?PI:0);
    float sw =sinf(aph)*0.40f*e->moveb*(1.0f-e->armp);
    float idle=0.06f*sinf(wtime*1.6f+ai);
    float reach=e->state==3?(ai==1?1.25f:0.30f):0.0f;
    /* up: down-forward base, rears up while airborne (armp), then thrust down to
       brace on impact (crouch) so the arms visibly come out of the leap pose */
    /* forward-POSITIVE, like the agents and the avatar: m3rotX(+t) swings the
     * limb toward local -Z, where the skull faces. The old expression was negated
     * throughout, so "down-and-forward" hung backward and the melee claw hooked
     * away from the player it was swatting. Mirrored wholesale to keep the tuning. */
    float up=0.50f+reach+0.85f*e->armp-0.40f*crouch-sw-idle;
    float eb=(e->state==3?0.35f:0.80f);            /* elbow bend                */
    float A[9],F[9],RX[9],RE[9],RZ[9],S[9];
    m3rotZ(RZ,ai*0.26f);
    m3rotX(RX,up);    m3mul(S,RX,RZ); m3mul(A,M,S);
    float sh[3]; m3v(M,ai*0.82f,1.80f,0,sh);
    float shx=e->x+sh[0], shy=pvY+sh[1], shz=e->z+sh[2];
    float ex,ey,ez; limb_seg(A,shx,shy,shz,-0.46f, 0.24f,0.19f,0.95f,7,-0.92f,&ex,&ey,&ez);
    set_uM(A,ex,ey,ez); sphere_sh(0.23f,0.26f,0.23f,6,4);
    m3rotX(RE,up+eb); m3mul(S,RE,RZ); m3mul(F,M,S);
    float wx,wy,wz; limb_seg(F,ex,ey,ez,-0.40f, 0.19f,0.12f,0.85f,7,-0.82f,&wx,&wy,&wz);
    for(int c=-1;c<=1;c++){ put(F,wx,wy,wz, c*0.12f,-0.18f,-0.10f); wedge_sh(0.06f,0.10f,0.46f); }
  }
  /* neck + wide skull on a low hunting carriage: the head sits in its own
     sub-basis (Hh) that lowers with phase and nods with the stride, so the
     face and eye cluster track together instead of staring level */
  float nod=-0.10f-0.05f*ph+0.07f*sinf(e->anim)*e->moveb+0.30f*crouch;
  float Hh[9],HX[9]; m3rotX(HX,nod); m3mul(Hh,M,HX);
  put(M,e->x,pvY,e->z,0,2.13f,0); cyl_sh(0.26f,0.40f,0.30f,8);
  float hb[3]; m3v(M,0,2.50f,0,hb);
  float hcx=e->x+hb[0], hcy=pvY+hb[1], hcz=e->z+hb[2];   /* skull centre */
  put(Hh,hcx,hcy,hcz,0,0,0); sphere_sh(0.58f,0.46f,0.62f,10,7);
  put(Hh,hcx,hcy,hcz,0,0.13f,-0.35f); bevbox_sh(0.62f,0.12f,0.30f,0.03f);  /* brow */
  /* swept horn crown: gives the skull a read from behind and above, where the
   * eye cluster is hidden and the boss was otherwise just a large lump */
  for(int hi=-1;hi<=1;hi+=2){
    float HR[9],RZh[9],RXh[9],Sh2[9],o[3];
    m3rotZ(RZh,hi*0.62f); m3rotX(RXh,0.30f); m3mul(Sh2,RZh,RXh); m3mul(HR,Hh,Sh2);
    m3v(Hh,hi*0.34f,0.36f,0.02f,o);
    set_uM(HR,hcx+o[0],hcy+o[1],hcz+o[2]); cyl_sh(0.125f,0.012f,0.66f,6);
  }
  /* eye cluster: emerald -> furnace red across phases, flaring with the roar */
  float glow=1.0f+e->roar*2.0f+0.5f*ph;
  glUniform1f(uEmis,1.0f);
  tintf((0.5f+0.9f*ph)*glow,(1.6f-0.6f*ph)*glow,0.35f*glow);
  float eyx[5]={-0.30f,-0.15f,0.0f,0.15f,0.30f}, eyy[5]={0.04f,0.11f,0.15f,0.11f,0.04f};
  for(int q=0;q<5;q++){ put(Hh,hcx,hcy,hcz, eyx[q],eyy[q],-0.50f); sphere_sh(0.075f,0.075f,0.05f,7,5); }
  glUniform1f(uEmis,0); glUniform1f(uRim,0);
  figDim=figDim0;
  primArm=0;
}

/* the agents: SUPERHOT-cut crystal humanoids in dark suits, emerald eyes.
 * Hard V-taper silhouette, flat facet planes, eased motion. Suit tint =
 * charcoal mixed with the Doppler shade of their motion. */
static void draw_agent(Enemy*e,float dim){
  if(e->state==4 && e->dieT<=0)return;
  primArm=1;
  /* 0 alive -> 1 fully come apart. Squash down and bulge out, and drive the
   * whole body toward white: the geometry is disintegrating into the shards that
   * are already flying. uNSc gets the same scale so the normals stay honest. */
  float die = e->state==4 ? 1.0f-clampf(e->dieT/0.13f,0,1) : 0.0f;
  float M[9],R[9],X[9];
  float by=e->y;
  float walk = sinf(e->anim), armw = cosf(e->anim); /* armw: contralateral arm phase */
  /* the striker's lunge, split into anticipation and blow. The windup length
   * tracks the AI's own difficulty-scaled lungeWind instead of the hardcoded
   * 0.32 that used to sit exactly ON the state change, and both halves ease
   * rather than snapping between two fixed leans. */
  float lunw=0.32f-0.045f*(float)LEVELS[curlevel].tier; if(lunw<0.08f)lunw=0.08f;
  float lrel  = sstep(e->lunRel);          /* 1 at the blow, eased to 0 after   */
  float lwind = e->state==3 ? sstep(clampf(e->state_t/lunw,0,1)) : 0.0f;
  float lhit  = e->state==3 ? sstep(clampf((e->state_t-lunw)/0.18f,0,1)) : lrel;
  float lean = e->state==3 ? (-0.38f*lwind*(1.0f-lhit) + 0.62f*lhit)
             : (0.62f*lrel + (e->type==1&&e->state==0 ? 0.18f*(1.0f-lrel) : 0.0f));
  lean -= e->recoil*e->recoil*0.10f;            /* fire recoil rocks the torso */
  /* Enemy AI yaw uses gameplay/shot direction (sin(+yaw), -cos(yaw)), while
   * m3rotY() maps the model's local -Z forward using the opposite handedness.
   * Negate only the visual yaw so raised arms/guns point toward their shots. */
  /* idle weight shift: a standing agent used to be a perfect statue, which is
   * exactly the pose you spend the most time staring at when the world is
   * frozen. A slow per-agent-phase roll off the hips (killed the moment it
   * starts walking) makes it a thing that is standing, not a mannequin. */
  float idlew=0.045f*sinf(wtime*0.85f+e->phase)*(1.0f-e->moveb);
  float Zi[9],Si[9];
  m3rotY(R,-e->yaw); m3rotZ(Zi,idlew); m3rotX(X,lean);
  m3mul(Si,Zi,X); m3mul(M,R,Si);
  if(die>0){ float sq=1.0f-0.45f*die, sw=1.0f+0.55f*die; m3scl(M,sw,sq,sw); }

  float vr=radial_v(e->x,e->z,e->vx,e->vz);
  float dr,dg,db; dopp_rgb(vr,&dr,&dg,&db);
  float mvel=clampf(sqrtf(e->vx*e->vx+e->vz*e->vz)/5.0f,0,1);
  float mixk=0.30f+0.55f*mvel;                  /* faster = stronger shift */
  float sr=(0.080f+e->hue*0.4f)*(1-mixk)+dr*0.22f*mixk;
  float sg=(0.090f+e->hue*0.3f)*(1-mixk)+dg*0.22f*mixk;
  float sb= 0.085f            *(1-mixk)+db*0.22f*mixk;
  float fl=e->flash>0?0.6f:0.0f;
  int locked = (gstate==ST_PLAY && laserTarget>=0 && laserTarget<nen && e==&en[laserTarget]);
  /* body tint, resolved once. The hit flash is warm-white, but a LOCKED agent
   * has to stay unmistakably RED: bleeding the flash into green and blue the
   * same way pushed it straight through pink into featureless white as soon as
   * the tonemap saw it. Lock keeps its flash almost entirely in the red. */
  float fr_=fl, fg_=fl*0.9f, fb_=fl*0.8f;
  if(locked){
    /* held below the ACES shoulder: the old peak (~2.2 red with 0.8 emissive)
     * blew straight through the tonemap into cream-white at pulse maxima */
    float p=0.72f+0.28f*sinf(gtime*13.0f);
    sr=0.80f+0.35f*p; sg=0.07f+0.06f*p; sb=0.05f;
    float lk=0.28f+0.20f*p;
    fr_=fl+lk; fg_=(fl+lk)*0.13f; fb_=(fl+lk)*0.08f;
  }
  float tr_=sr+fr_, tg_=sg+fg_, tb_=sb+fb_;
  glUniform1f(uBump,0); glUniform1f(uGloss,0.6f);
  glUniform1f(uEmis,(locked?0.65f:0.12f)+fl+0.88f*die);
  tintf(tr_+2.4f*die,tg_+2.4f*die,tb_+2.4f*die);
  /* crystalline rim: edge glow shaded by the agent's own Doppler — closing
   * agents flare blue, receding red — so the silhouette carries the mechanic.
   * Lock paints a hot red rim instead. */
  glUniform1f(uRim, locked?1.20f:0.85f);
  rimf( locked?1.40f:dr*0.9f, locked?0.18f:dg*0.9f, locked?0.10f:db*0.9f);

  /* legs: the same travel-locked stride the avatar walks on. Each foot gets a
   * world-space target — a triangle sweep along the agent's ACTUAL velocity
   * plus a lift on the swing half — and 2-bone IK finds the knee. The stance
   * foot's backward sweep exactly cancels the agent's travel, so the old FK
   * swing's skate is gone, and strafing reads as side-steps for free because
   * the stride runs along velocity rather than facing. */
  float asp=sqrtf(e->vx*e->vx+e->vz*e->vz);   /* live, for the stride DIRECTION */
  float aspS=e->spdS;                         /* eased, for its AMPLITUDE       */
  float arun=clampf(aspS/3.2f,0,1)*e->moveb;
  /* Half-stride solved from the cadence law rather than tuned at one speed.
   * The stance foot sweeps 2*half per half cycle in pi/rate seconds, so
   * half = pi*sp/(2*rate) cancels the travel EXACTLY — and agents walk at
   * four different speeds (advance, strafe, cooldown drift, striker charge),
   * which is precisely where a single tuned constant skates. Tends to the
   * avatar's authored 0.338 at speed, and to zero when stopped. */
  float ahalf=0.5f*PI*aspS/(0.9f+4.65f*aspS); if(ahalf>0.36f)ahalf=0.36f;
  float fdx,fdz;
  if(asp>0.2f){ fdx=e->vx/asp; fdz=e->vz/asp; }
  else { fdx=sinf(e->yaw); fdz=-cosf(e->yaw); }
  /* knee pole = the body's true forward (local -Z), never the travel
   * direction — a backpedalling agent must not bend its knees the wrong way */
  float polex=-M[6], polez=-M[8];
  { float pl=sqrtf(polex*polex+polez*polez);
    if(pl>1e-4f){ polex/=pl; polez/=pl; } else { polex=0; polez=-1; } }
  for(int li=0;li<2;li++){
    float side=li?0.14f:-0.14f;
    float hip[3]; m3v(M,side,-0.10f,0,hip);           /* through the pelvis pivot */
    float hx=e->x+hip[0], hy=by+1.02f+hip[1], hz=e->z+hip[2];
    float ph=fmodf(e->anim+(li?PI:0.0f), 2*PI); if(ph<0)ph+=2*PI;
    float tri   = ph<PI ? 1.0f-2.0f*ph/PI : -1.0f+2.0f*(ph-PI)/PI;
    float swing = ph<PI ? 0.0f : sinf(ph-PI);
    float along=ahalf*tri, lift=swing*0.13f*arun;
    float tgx=hx+fdx*along, tgz=hz+fdz*along, tgy=by+0.03f+lift;
    /* keep the target inside reach so the knee never snaps straight */
    float L1=0.50f,L2=0.46f, maxr=(L1+L2)*0.985f, minr=0.10f;
    float dxv=tgx-hx,dyv=tgy-hy,dzv=tgz-hz, dd=sqrtf(dxv*dxv+dyv*dyv+dzv*dzv);
    if(dd>maxr){ float k=maxr/dd; tgx=hx+dxv*k;tgy=hy+dyv*k;tgz=hz+dzv*k; }
    else if(dd<minr&&dd>1e-4f){ float k=minr/dd; tgx=hx+dxv*k;tgy=hy+dyv*k;tgz=hz+dzv*k; }
    float kx,ky,kz; ik2(hx,hy,hz,tgx,tgy,tgz,L1,L2,polex,polez,&kx,&ky,&kz);
    float UB[9]; aim_basis(kx-hx,ky-hy,kz-hz,UB);
    float jx,jy,jz; limb_seg(UB,hx,hy,hz,-L1*0.52f, 0.105f,0.084f,L1*1.04f,7, -L1, &jx,&jy,&jz);
    set_uM(UB,jx,jy,jz); sphere_sh(0.101f,0.121f,0.101f,6,4);   /* knee */
    float LB[9]; aim_basis(tgx-jx,tgy-jy,tgz-jz,LB);
    float ax,ay,az; limb_seg(LB,jx,jy,jz,-L2*0.52f, 0.084f,0.052f,L2*1.04f,7, -L2, &ax,&ay,&az);
    float FF[9],FR[9],FX[9]; m3rotY(FR,-e->yaw);
    m3rotX(FX,0.30f*swing*arun);     /* light dorsiflexion through the swing */
    m3mul(FF,FR,FX);
    set_uM(FF,ax,ay-0.003f,az); wedge_sh(0.12f,0.065f,0.27f);  /* solved ankle */
  }
  /* pelvis + chest: hips wider than the waist, chest flares to the shoulders
   * — the V-taper. Depth-squashed to a slab; idle gets a breathing swell. */
  float br=1.0f+0.018f*sinf(wtime*1.8f+e->phase)*(1.0f-e->moveb);
  float Mt[9]; memcpy(Mt,M,36); m3scl(Mt,1.0f,1.0f,0.60f);
  /* the whole upper body hangs off ONE pelvis pivot: every part above it is
   * an offset THROUGH the lean basis M, so a striker's 0.62-rad lunge lean
   * (and the idle weight shift) translates the chest and head instead of
   * spinning each piece in place around its own fixed world point */
  float pvY=by+1.02f;
  glUniform3f(uNSc,1,1,0.60f);          /* correct normals under the slab squash */
  { float o[3]; m3v(M,0,0,0,o);
    set_uM(Mt,e->x+o[0],pvY+o[1],e->z+o[2]); } cyl_sh(0.185f,0.165f,0.26f,9);
  float Mb[9]; memcpy(Mb,Mt,36); m3scl(Mb,br,1.0f,br);
  { float o[3]; m3v(M,0,0.38f,0,o);
    set_uM(Mb,e->x+o[0],pvY+o[1],e->z+o[2]); } cyl_sh(0.175f,0.27f,0.54f,9);
/* Trapezius yoke. The chest ended in a flat horizontal disc 0.54 across and the
 * neck was a 0.11 stick poking out of it, with nothing in between — from any
 * three-quarter or elevated angle (which is most of this game, given the
 * platforms) that is the definitive "stacked cans" read, on the part of the
 * figure the eye goes to first. A cone from the chest radius down to the neck
 * radius gives a ~46 degree shoulder slope: the silhouette cue for a
 * broad-shouldered figure in a suit. Shares the squashed/breathing basis, so it
 * inherits the z-squash and the uNSc normal correction already in flight. */
  { float o[3]; m3v(M,0,0.615f,0,o);
    set_uM(Mb,e->x+o[0],pvY+o[1],e->z+o[2]); } cyl_shc(0.255f,0.105f,0.16f,9,0);
  glUniform3f(uNSc,1,1,1);
  /* shoulders: small caps on a wide frame */
  for(int si=-1;si<=1;si+=2){ put(M,e->x,pvY,e->z,si*0.29f,0.62f,0); sphere_sh(0.088f,0.088f,0.088f,7,5); }
  /* tie: a darker sliver down the chest */
  tintf(0.02f,0.03f,0.025f);
  put(M,e->x,pvY,e->z, 0,0.40f,-0.132f); bevbox_sh(0.08f,0.42f,0.02f,0.012f);
  /* coat tails: two hanging panels off the back of the waist. They kick out with
   * the stride, which is what finally sells the agents as figures in long coats
   * rather than mannequins — the silhouette moves even when the limbs are still. */
  { float flap=0.22f*e->moveb+0.16f*walk*e->moveb;
    float CT[9],RXc[9]; m3rotX(RXc,-flap); m3mul(CT,M,RXc);
    tintf(tr_*0.72f,tg_*0.72f,tb_*0.72f);
    for(int ci=-1;ci<=1;ci+=2){
      float o[3]; m3v(CT,ci*0.092f,-0.26f,0.118f,o);
      set_uM(CT,e->x+o[0],by+1.02f+o[1],e->z+o[2]); bevbox_sh(0.175f,0.48f,0.042f,0.018f);
    } }
  tintf(tr_,tg_,tb_);
  /* arms: anticipation dip -> eased raise with overshoot -> recoil kick */
  for(int ai=0;ai<2;ai++){
    float t=e->armp, raise;
    if(ai){
      raise = t<0.22f ? -0.14f*sstep(t/0.22f)
                      : 1.45f*easeOutBack((t-0.22f)/0.78f);
      if(e->state==2) raise=1.45f*sstep(t);           /* eased lowering */
      if(e->state==3) raise=-0.30f*lwind+1.85f*lhit;   /* cock back, then throw */
      else if(lrel>0.001f) raise = 1.85f*lrel + raise*(1.0f-lrel);  /* ...and recover */
      raise += e->recoil*e->recoil*0.35f;
    } else raise = e->state==3? -0.30f*lwind+1.85f*lhit : 1.85f*lrel;
    /* idle arm drift, antiphase across the shoulders and offset per agent, so a
     * crowd of waiting agents never breathes in unison. Faded out by moveb and
     * by any raised arm, which owns its own eased motion. */
    raise += 0.055f*sinf(wtime*1.5f+e->phase+ai*2.1f)
             *(1.0f-e->moveb)*clampf(1.0f-raise*1.5f,0,1);
    float swA=(ai?armw:-armw)*0.4f*e->moveb*clampf(1.0f-raise*2.0f,0,1);
    float eb=0.30f+0.55f*sstep(clampf(raise/1.45f,0,1));
    float sh[3]; m3v(M,ai?0.29f:-0.29f,0.62f,0,sh);
    float shx=e->x+sh[0],shy=pvY+sh[1],shz=e->z+sh[2];
    float A[9],RX[9],RZ[9],S[9];
    /* SIGN. limb_seg hangs along local -Y, and m3rotX(+t) swings that toward
     * local -Z — which is the direction the face is drawn and the direction
     * gameplay fires (update_enemies spawns rounds 0.55 in FRONT). This whole
     * expression used to be negated, so every raised gun arm reached 180 degrees
     * BEHIND the agent while its bullets left the front: the aim telegraph, the
     * drawn pistol and the striker's claw all pointed the wrong way. draw_player
     * was fixed long ago (see its own note) and the fix never reached the agents.
     * Negated as a whole, so the stride swing keeps its tuned contralateral pairing. */
    m3rotX(RX,raise-swA*e->fwdb); m3rotZ(RZ,-swA*e->latb*0.5f);
    m3mul(S,RX,RZ); m3mul(A,M,S);
    float ex,ey,ez; limb_seg(A,shx,shy,shz,-0.18f, 0.072f,0.062f,0.38f,7, -0.37f, &ex,&ey,&ez);
    set_uM(A,ex,ey,ez); sphere_sh(0.075f,0.090f,0.075f,6,4);   /* elbow */
    float F[9],RE[9]; m3rotX(RE,raise-swA*e->fwdb+eb); m3mul(S,RE,RZ); m3mul(F,M,S);
    float hx2,hy2,hz2; limb_seg(F,ex,ey,ez,-0.17f, 0.062f,0.050f,0.36f,7, -0.36f, &hx2,&hy2,&hz2);
    set_uM(F,hx2,hy2,hz2); wedge_sh(0.072f,0.12f,0.060f);  /* cut mitt */
    if(ai&&e->type==0&&raise>0.05f){ /* pistol in the raised hand */
      tintf(0.03f,0.035f,0.04f);
      put(F,hx2,hy2,hz2,0,-0.10f,-0.14f); box_sh(0.06f,0.09f,0.24f);
      tintf(tr_,tg_,tb_);
    }
  }
  /* defined neck + cut-gem head with hard brow and jaw lines. The head rides a
   * sub-basis (Mh) that adds the smoothed head-look so the skull tracks you. */
  put(M,e->x,pvY,e->z, 0,0.645f,0); cyl_sh(0.055f,0.070f,0.15f,8);
  float Mh[9],HY[9],HX[9],HL[9];
  m3rotY(HY,-e->headYaw); m3rotX(HX,e->headPitch); m3mul(HL,HY,HX); m3mul(Mh,M,HL);
  float hb[3]; m3v(M,0,0.83f,0,hb);
  float hcx=e->x+hb[0], hcy=pvY+hb[1], hcz=e->z+hb[2];   /* head centre */
  set_uM(Mh,hcx,hcy,hcz); sphere_sh(0.125f,0.16f,0.135f,9,6);
  put(Mh,hcx,hcy,hcz,0,-0.12f,-0.045f); bevbox_sh(0.16f,0.09f,0.18f,0.022f);
  put(Mh,hcx,hcy,hcz,0,0.075f,-0.075f); bevbox_sh(0.20f,0.035f,0.10f,0.014f);
  /* the shades. Rails above and below, temple arms down the sides, and the eyes
   * reduced to two burning slits sitting in the gap between the rails — the one
   * silhouette cue that reads "agent" at any distance, in any lighting. */
  tintf(0.014f,0.016f,0.018f);
  put(Mh,hcx,hcy,hcz, 0, 0.050f,-0.112f); bevbox_sh(0.245f,0.022f,0.055f,0.010f);
  put(Mh,hcx,hcy,hcz, 0,-0.014f,-0.112f); bevbox_sh(0.245f,0.020f,0.055f,0.010f);
  for(int s=-1;s<=1;s+=2){
    put(Mh,hcx,hcy,hcz, s*0.120f,0.032f,-0.045f); bevbox_sh(0.026f,0.020f,0.110f,0.008f);
  }
  /* the eyes: emerald, flaring smoothly through the aim phase */
  float flare = 1.0f+e->flare*2.5f;
  glUniform1f(uEmis,1);
  tintf((0.25f+0.9f*(flare-1))*dim,1.8f*flare*dim,(0.8f*flare)*dim);
  for(int s=-1;s<=1;s+=2){
    put(Mh,hcx,hcy,hcz,s*0.058f,0.018f,-0.128f);
    box_sh(0.088f,0.026f,0.022f);
  }
  glUniform1f(uEmis,0);
  glUniform1f(uRim,0);
  primArm=0;
}

/* Body opacity during the ADS transition. There is no threshold that both avoids
 * clipping the head (which encloses the eye at full zoom) and avoids popping a
 * headless torso away while the camera is still out on the boom — the camera
 * passes THROUGH the body on the way in. So dissolve it instead, reusing the
 * uAlpha the mirrored floor pass already established. */
static void body_alpha(float a){
  if(a>=0.999f){ glDisable(GL_BLEND); glUniform1f(uAlpha,1.0f); }
  else { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
         glUniform1f(uAlpha,a); }
}
/* One avatar arm: upper arm, elbow cap, forearm, cut mitt and the emissive
 * forearm strip, reporting the solved hand. The pistol arm (posed by the shared
 * player_arm_r solver) and the loop arms had this exact six-call sequence with
 * all twelve dimension constants written out twice, verbatim. Leaves the suit
 * tint and emissive restored, so callers can hang a weapon off the hand. */
static void avatar_arm(const float*A,const float*F,
                       float shx,float shy,float shz,float side,int vm,
                       float*hx,float*hy,float*hz){
  float ex,ey,ez;
  /* vm = viewmodel (the camera is inside the head). The elbow ends up about 35cm
   * from the eye with the arm shouldered, so the upper arm and its cap fill a
   * third of a zoomed frame with a wall of near-plane geometry. Real viewmodels
   * show a forearm and a weapon; solve the chain either way so the hand lands in
   * exactly the same place, just skip drawing the part behind the elbow. */
  if(!vm){
    limb_seg(A,shx,shy,shz,-0.18f, 0.068f,0.058f,0.36f,7, -0.35f, &ex,&ey,&ez);
    set_uM(A,ex,ey,ez); sphere_sh(0.070f,0.084f,0.070f,6,4);   /* elbow */
  } else {
    float e[3]; m3v(A,0,-0.35f,0,e); ex=shx+e[0]; ey=shy+e[1]; ez=shz+e[2];
  }
  limb_seg(F,ex,ey,ez,-0.16f, 0.058f,0.046f,0.34f,7, -0.33f, hx,hy,hz);
  set_uM(F,*hx,*hy,*hz); wedge_sh(0.068f,0.110f,0.055f);
  glUniform1f(uEmis,0.85f); tintf(0.10f,1.15f,0.50f);        /* forearm strip */
  put(F,ex,ey,ez, side*0.046f,-0.16f,0); box_sh(0.008f,0.20f,0.008f);
  glUniform1f(uEmis,0.08f); tintf(0.04f,0.05f,0.045f);
}

/* third-person player avatar: compact, sleek, low-poly and readable from
 * behind. The whole figure hangs off a mid-body pivot so the dodge roll can
 * somersault it; the camera never rolls with it. */
static void draw_player(void){
  primArm=1;
  PPose P=*pose_get();
  /* bodyA: 1 while hip-firing, dissolving to 0 as the camera enters the head.
   * Only the weapon arm stays solid — that IS the first-person viewmodel, and
   * it is posed by the same shared solver the ballistics read, so the sights
   * cannot disagree with where the round goes.
   * The band is late on purpose: it has to track where the CAMERA is, not where
   * the blend is. At 0.55 the eye is still ~1.4 behind the body; by 0.88 it is
   * 0.37 away and the torso is engulfing the lens, which is exactly when fading
   * it reads as the camera passing through rather than as the body vanishing. */
  float bodyA = 1.0f - clampf((ads_amt()-0.55f)/0.33f,0,1);
  int hideBody = bodyA<=0.01f;
  int rolling=P.rolling, blade=P.blade;
  float tuck=P.tuck, tk=P.tk, walk=P.walk, armw=P.armw, s=P.s, spd=P.spd,
        run=P.run, absorb=P.absorb,
        poseYaw=P.poseYaw, pcy=P.pcy;
  float M[9]; memcpy(M,P.M,36);

  glUniform1f(uBump,0);
  glUniform1f(uGloss,0.55f);
  glUniform1f(uEmis,0.08f);
  tintf(0.04f,0.05f,0.045f);
  glUniform1f(uRim,0.70f); rimf(0.10f,1.00f,0.42f);  /* emerald crystal edge */
  body_alpha(bodyA);

  /* legs. Walking: 2-bone IK with foot-planting — each foot's stride sweeps
   * backward at exactly the locked cadence, so the planted foot stays put on
   * the floor instead of skating, while the swing foot arcs forward. Rolling
   * keeps the old tucked FK curl. Bones are a touch longer than hip height so
   * the feet reach the ground with a real, athletic knee bend. */
  float fdx,fdz;                                  /* world ground move dir (stride) */
  if(spd>0.2f){ fdx=pvx/spd; fdz=pvz/spd; }
  else { fdx=sinf(avYaw); fdz=-cosf(avYaw); }     /* aim convention, not the negated poseYaw */
  /* knee pole = the body's TRUE forward (local -Z, where the face is drawn), so
   * knees always bend forward even when backpedalling or strafing — never the
   * reversed insect-leg bend that pointing the pole along travel produced. */
  float polex=-M[6], polez=-M[8];
  { float pl=sqrtf(polex*polex+polez*polez);
    if(pl>1e-4f){ polex/=pl; polez/=pl; } else { polex=0; polez=-1; } }
  for(int li=0;li<2&&!hideBody;li++){
    float side=li?0.14f:-0.14f;
    float hip[3]; m3v(M,side,0.37f*tk,0,hip);
    float hx=px+hip[0], hy=pcy+hip[1], hz=pz+hip[2];
    if(rolling){
      /* Tucked FK curl, driven by the ROLL's own curl amount — not by fwdb,
       * which is a -1..1 movement blend that happens to sit near zero on a
       * sideways roll and left the legs sticking straight out of a
       * somersaulting torso. Hips and knees now fold with the tuck and
       * unfold as the avatar comes back to its feet. */
      float hipc=0.30f+1.10f*tuck, knee=0.55f+1.30f*tuck;
      float UL[9],RX[9]; m3rotX(RX,hipc); m3mul(UL,M,RX);
      float kx,ky,kz; limb_seg(UL,hx,hy,hz,-0.22f, 0.10f,0.078f,0.46f,7, -0.44f, &kx,&ky,&kz);
      set_uM(UL,kx,ky,kz); sphere_sh(0.094f,0.113f,0.094f,6,4);
      float LL[9],RK[9]; m3rotX(RK,hipc+knee); m3mul(LL,M,RK);
      float ax,ay,az; limb_seg(LL,kx,ky,kz,-0.20f, 0.078f,0.050f,0.42f,7, -0.40f, &ax,&ay,&az);
      set_uM(LL,ax,ay,az); wedge_sh(0.12f,0.055f,0.24f);
      continue;
    }
    /* stride phase: antiphase feet, triangle sweep + swing-half lift. The
     * triangle's backward slope is tuned to the cadence so stance feet lock. */
    float ph=fmodf(bobT*7.5f + (li?PI:0.0f), 2*PI); if(ph<0)ph+=2*PI;
    float tri   = ph<PI ? 1.0f-2.0f*ph/PI : -1.0f+2.0f*(ph-PI)/PI;
    float swing = ph<PI ? 0.0f : sinf(ph-PI);
    /* half-stride solved from the cadence law (bobT rate is 0.9+4.65*sp), so
     * the stance foot's backward sweep cancels the travel at EVERY speed —
     * the fixed 0.33*run only locked at full sprint and skated below it.
     * Same law the agents already walk on (see draw_agent). Airborne, the
     * stride collapses and the feet tuck up under the body instead of
     * pedalling a full ground cycle against nothing. */
    float strideHalf=0.5f*PI*pspdS/(0.9f+4.65f*pspdS);
    if(strideHalf>0.36f)strideHalf=0.36f;
    strideHalf*=(1.0f-airB);
    float along=strideHalf*tri, lift=swing*0.14f*run*(1.0f-airB);
    float tgx=hx+fdx*along, tgz=hz+fdz*along, tgy=py+0.03f+lift+0.26f*airB;
    /* keep the target inside the leg's reach so the knee never snaps straight */
    float L1=0.50f,L2=0.46f, maxr=(L1+L2)*0.985f, minr=0.10f;
    float dxv=tgx-hx,dyv=tgy-hy,dzv=tgz-hz, dd=sqrtf(dxv*dxv+dyv*dyv+dzv*dzv);
    if(dd>maxr){ float k=maxr/dd; tgx=hx+dxv*k;tgy=hy+dyv*k;tgz=hz+dzv*k; }
    else if(dd<minr&&dd>1e-4f){ float k=minr/dd; tgx=hx+dxv*k;tgy=hy+dyv*k;tgz=hz+dzv*k; }
    float kx,ky,kz; ik2(hx,hy,hz,tgx,tgy,tgz,L1,L2,polex,polez,&kx,&ky,&kz);
    /* upper leg hip->knee, lower leg knee->ankle; each limb_seg reports its end
     * joint, which we reuse for the next joint and the caps (no recomputation) */
    float UB[9]; aim_basis(kx-hx,ky-hy,kz-hz,UB);
    float jx,jy,jz; limb_seg(UB,hx,hy,hz,-L1*0.52f, 0.10f,0.078f,L1*1.04f,7, -L1, &jx,&jy,&jz);
    set_uM(UB,jx,jy,jz); sphere_sh(0.094f,0.113f,0.094f,6,4);  /* knee */
    float LB[9]; aim_basis(tgx-jx,tgy-jy,tgz-jz,LB);
    float ax,ay,az; limb_seg(LB,jx,jy,jz,-L2*0.52f, 0.078f,0.050f,L2*1.04f,7, -L2, &ax,&ay,&az);
    glUniform1f(uEmis,0.85f); tintf(0.10f,1.15f,0.50f);   /* shin light strip */
    put(LB,jx,jy,jz, li?0.043f:-0.043f,-0.24f,0); box_sh(0.007f,0.22f,0.007f);
    glUniform1f(uEmis,0.08f); tintf(0.04f,0.05f,0.045f);
    float FF[9],FR[9],FX[9]; m3rotY(FR,poseYaw);
    m3rotX(FX,0.30f*swing*run);      /* light dorsiflexion through the swing */
    m3mul(FF,FR,FX);
    /* the ankle the IK actually solved — the foot used to stay welded to
     * floor height while the shin lifted, detaching at every swing */
    set_uM(FF,ax,ay-0.003f,az); wedge_sh(0.12f,0.055f,0.26f);
  }

  /* pelvis / jacket: same V-taper language as the agents */
  if(!hideBody){
  float Mt[9]; memcpy(Mt,M,36); m3scl(Mt,1.0f,1.0f,0.62f);
  tintf(0.03f,0.04f,0.035f);
  glUniform3f(uNSc,1,1,0.62f);
  { float o[3];
    m3v(M,0,0.49f*tk,0,o); set_uM(Mt,px+o[0],pcy+o[1],pz+o[2]); cyl_sh(0.175f,0.155f,0.24f,9);
    m3v(M,0,0.87f*tk,0,o); set_uM(Mt,px+o[0],pcy+o[1],pz+o[2]); cyl_sh(0.165f,0.25f,0.50f,9);
    /* trapezius yoke — see draw_agent. Must sit INSIDE the uNSc=0.62 block: it
     * shares the squashed basis, so it needs the same normal correction. */
    m3v(M,0,1.00f*tk,0,o); set_uM(Mt,px+o[0],pcy+o[1],pz+o[2]); cyl_shc(0.235f,0.095f,0.15f,9,0); }
  glUniform3f(uNSc,1,1,1);
  /* coat tails — the agents' best silhouette trick, now on the avatar: two
   * hanging panels that kick out with the stride */
  if(!rolling){
    float flap=0.22f*run+0.16f*walk*run;
    float CT[9],RXc[9]; m3rotX(RXc,-flap); m3mul(CT,M,RXc);
    tintf(0.024f,0.032f,0.028f);
    for(int ci=-1;ci<=1;ci+=2){
      float o[3]; m3v(CT,ci*0.092f,-0.26f,0.118f,o);
      set_uM(CT,px+o[0],pcy+0.49f*tk+o[1],pz+o[2]); bevbox_sh(0.175f,0.48f,0.042f,0.018f);
    }
  }
  /* TRON belt bar: a thin emissive line across the waist front */
  glUniform1f(uEmis,0.85f); tintf(0.10f,1.15f,0.50f);
  put(M,px,pcy,pz,0,0.49f*tk,-0.112f); box_sh(0.16f,0.012f,0.010f);
  glUniform1f(uEmis,0.08f);
  tintf(0.05f,0.06f,0.055f);
  for(int si=-1;si<=1;si+=2){ put(M,px,pcy,pz,si*0.27f,1.05f*tk,0); sphere_sh(0.083f,0.083f,0.083f,7,5); }
  }   /* !hideBody: pelvis, coat, belt, shoulder caps */

  /* shoulders and arms. right arm: pistol carried/aimed via the SHARED solver
   * (player_arm_r — the same transform the laser and bullets read), katana
   * sweep on swing — wind-up across the left shoulder, cut down and across. */
  tintf(0.04f,0.05f,0.045f);
  body_alpha(1.0f);          /* the weapon arm is the viewmodel: always solid */
  if(!blade){
    const ArmR a=*arm_get();
    float hx2,hy2,hz2;
    avatar_arm(a.A,a.F,a.sx,a.sy,a.sz,+1.0f,hideBody,&hx2,&hy2,&hz2);
    tintf(0.03f,0.035f,0.04f);
    /* Shrink the weapon as it becomes a viewmodel. A UNIFORM scale is safe here
     * in a way a non-uniform one would not be: the fragment shader normalizes
     * vN, so an even scale leaves every normal (and therefore the lighting) and
     * the barrel direction exactly as they were — it only trims the bulk of a
     * model that is genuinely half a metre from the lens. The alternative, a
     * separate narrower projection for the viewmodel, would break this game's
     * one real guarantee: that the gun you see IS the gun the bullet leaves. */
    { float ae=ads_amt(), vs=1.0f-0.30f*ae;
      float GS[9]; memcpy(GS,a.GP,36); m3scl(GS,vs,vs,vs);
      pistol_sh(GS,a.gx,a.gy,a.gz,pammo); }
    glUniform1f(uEmis,0.08f);   /* pistol_sh leaves uEmis at 1.0 for its pips */
    tintf(0.04f,0.05f,0.045f);
  }
  for(int ai=0;ai<2;ai++){
    if(ai&&!blade)continue;            /* drawn above, off the shared solver */
    /* NB: the `continue` above means ai==1 implies blade==1. That made the whole
     * pistol-carry branch that used to live here — and PPose.aimStance with it —
     * unreachable: aimStance is `!rolling && !blade`, so it was provably 0 at
     * every one of its four uses. The live pistol pose is player_arm_r's. */
    float raise, swYaw=0;
    if(ai){
      if(s>0){
        /* Katana sweep: lock the torso in combat yaw and let the arm do the
         * work.  A compact S-curve avoids the old apparent 180-degree body
         * flip while still reading as a right-shoulder-to-left-hip cut. */
        float cut=sstep(s);
        raise = 1.08f + 0.32f*sinf(s*PI) - 0.30f*cut;
        swYaw = -0.62f + 1.36f*cut;
      }
      /* swRel=1 is exactly the end-of-cut pose (0.78/0.74): the arm now
       * eases into the carry instead of snapping the frame the cut ends */
      else { raise=0.52f+0.26f*swRel; swYaw=0.42f+0.32f*swRel; }
    } else raise = 0.10f + 0.055f*sinf(gtime*1.5f)*(1.0f-run); /* low-ready + breath */
    /* the free arm swings out to balance the landing; the weapon arm holds
     * its aim so the laser never jumps when you touch down */
    if(!ai) raise += 0.42f*absorb;   /* was !(ai&&(aimStance||blade)); blade is
                                        always set when ai is, so this is !ai */
    /* both arms hug the tucked knees — but ride the curl in and out instead of
     * snapping to the hug on the frame the roll starts */
    if(rolling){ raise=0.35f+0.95f*tuck; swYaw=0; }
    float armStride=ai?armw:-armw;   /* contralateral: opposes the same-side leg */
    float sw = rolling? 1.0f : armStride*0.34f*run*clampf(1.0f-raise*1.25f,0,1);
    float sh[3]; m3v(M,ai?0.27f:-0.27f,1.05f*tk,0,sh);
    float shx=px+sh[0], shy=pcy+sh[1], shz=pz+sh[2];
    float A[9],RX[9],RY[9],S[9];
    /* Positive X raise swings the arm's local -Y chain toward local -Z
     * (the avatar's forward).  Negative raise folds it backward over the
     * shoulder, which made the pistol look 180-degrees flipped. */
    m3rotX(RX,raise+sw); m3rotY(RY,swYaw); m3mul(S,RY,RX); m3mul(A,M,S);
    float F[9],RE[9]; m3rotX(RE,raise+sw+0.16f+(ai&&s>0?0.18f+0.18f*sinf(s*PI):0.0f));
    m3mul(S,RY,RE); m3mul(F,M,S);
    float hx2,hy2,hz2;
    avatar_arm(A,F,shx,shy,shz,ai?1.0f:-1.0f,hideBody,&hx2,&hy2,&hz2);
    if(ai&&blade&&!hideBody){
      /* the katana: dark blade, emissive emerald edge, square tsuba. The
       * !hideBody guard is the backstop for (1)-(3) above: whatever the blend
       * does, a metre of blade never gets drawn from inside the head. */
      float B[9],RB[9]; m3rotX(RB,1.35f); m3mul(B,F,RB);
      tintf(0.10f,0.16f,0.12f);
      put(B,hx2,hy2,hz2,0,-0.10f,0); box_sh(0.10f,0.02f,0.10f);
      tintf(0.45f,0.55f,0.50f); glUniform1f(uEmis,0.30f);
      put(B,hx2,hy2,hz2,0,-0.62f,0); box_sh(0.022f,1.05f,0.045f);
      tintf(0.30f,2.0f,0.9f); glUniform1f(uEmis,1.0f);
      put(B,hx2,hy2,hz2,-0.014f,-0.62f,0); box_sh(0.006f,1.05f,0.03f);
      glUniform1f(uEmis,0.08f);
      tintf(0.04f,0.05f,0.045f);
    }
  }

  body_alpha(bodyA);
  /* the katana when it is NOT in hand: slung diagonally across the back in a
   * dark saya with a lit seam. Previously the blade simply blinked out of
   * existence between swings, which read as the avatar having no weapon at all. */
  if(!blade&&!rolling&&swStow<0.35f&&!hideBody){
    float SB[9],RZs[9],RXs[9],S1[9],o[3];
    m3rotZ(RZs,0.62f); m3rotX(RXs,0.14f); m3mul(S1,RZs,RXs); m3mul(SB,M,S1);
    m3v(M,0.02f,0.86f*tk,0.20f,o);
    tintf(0.055f,0.065f,0.060f);
    set_uM(SB,px+o[0],pcy+o[1],pz+o[2]); box_sh(0.048f,0.98f,0.048f);
    tintf(0.10f,0.85f,0.42f); glUniform1f(uEmis,0.60f);
    set_uM(SB,px+o[0],pcy+o[1],pz+o[2]); box_sh(0.014f,1.00f,0.014f);
    glUniform1f(uEmis,0.08f); tintf(0.04f,0.05f,0.045f);
  }

  /* The health spine used to live here: five luminous pads up the avatar's back.
   * Removed — they read as backpack clutter on a figure whose silhouette is the
   * whole point, and they were the brightest thing on screen in a game shot
   * almost entirely from behind. Health moved to the HUD corner with the other
   * two readouts (see draw_hud), which is where a number belongs. */

  /* neck / head. The skull rides its own sub-basis (Mh) that pitches with the
   * look, exactly like the agents' head-track — the avatar's head used to be
   * welded level to the torso, so aiming up a stairwell it still stared at the
   * horizon. Damped to about half the look angle so the neck never breaks, and
   * dropped entirely during a roll, where the whole body is spinning. */
  if(hideBody){ body_alpha(1.0f); glUniform1f(uEmis,0); glUniform1f(uRim,0); primArm=0; return; }
  float hpit = rolling? 0 : clampf(-ppitch*PI/180.0f*0.55f,-0.45f,0.45f);
  float Mh[9],HX2[9]; m3rotX(HX2,hpit); m3mul(Mh,M,HX2);
  float ho[3]; m3v(M,0,1.29f*tk,0,ho);
  float hcx=px+ho[0], hcy=pcy+ho[1], hcz=pz+ho[2];   /* head centre, world */
  tintf(0.06f,0.07f,0.065f);
  put(M,px,pcy,pz,0,1.10f*tk,0); cyl_sh(0.052f,0.062f,0.13f,8);
  set_uM(Mh,hcx,hcy,hcz); sphere_sh(0.125f,0.155f,0.135f,9,6);
  /* sharp collar plus distinct angular shades — no mouth/smile geometry.
   * The collar stays on the torso; everything on the face rides Mh. */
  tintf(0.02f,0.02f,0.02f);
  put(M,px,pcy,pz,0,1.18f*tk,0.015f); bevbox_sh(0.19f,0.040f,0.12f,0.018f);
  for(int si=-1;si<=1;si+=2){
    put(Mh,hcx,hcy,hcz,si*0.055f,0.025f,-0.118f); bevbox_sh(0.088f,0.046f,0.030f,0.012f);
  }
  put(Mh,hcx,hcy,hcz,0,0.022f,-0.121f); bevbox_sh(0.036f,0.018f,0.026f,0.008f);
  for(int si=-1;si<=1;si+=2){
    put(Mh,hcx,hcy,hcz,si*0.124f,0.024f,-0.064f); bevbox_sh(0.030f,0.030f,0.095f,0.010f);
  }
  glUniform1f(uEmis,0.65f);
  tintf(0.08f,1.25f,0.55f);
  for(int si=-1;si<=1;si+=2){
    put(Mh,hcx,hcy,hcz,si*0.076f,0.036f,-0.137f); box_sh(0.030f,0.006f,0.006f);
  }
  glUniform1f(uEmis,0.0f);
  glUniform1f(uRim,0);
  body_alpha(1.0f);
  primArm=0;
}

static void draw_items(void){
  float M[9];
  for(int i=0;i<nitems;i++){
    if(items[i].taken)continue;
    float base=floor_at(items[i].x,items[i].z);
    if(refl){ /* pickups were the one thing on the glossy floor with no mirror
                 image; fade by height exactly like the figures do */
      float rf=clampf(1.0f-base/0.9f,0,1);
      if(rf<=0.02f)continue;
      figDim=rf*0.6f;
    }
    float bob=base+0.45f+0.1f*sinf(wtime*2.5f+i);
    m3rotY(M,wtime*1.5f+i);
    glUniform1f(uEmis,1); glUniform1f(uBump,0); glUniform1f(uGloss,0.4f);
    if(items[i].type==0){ /* health: a true 3-axis cross (was flat edge-on) */
      tintf(1.5f,1.5f,1.6f);
      set_uM(M,items[i].x,bob,items[i].z); bevbox_sh(0.30f,0.10f,0.10f,0.022f);
      set_uM(M,items[i].x,bob,items[i].z); bevbox_sh(0.10f,0.30f,0.10f,0.022f);
      set_uM(M,items[i].x,bob,items[i].z); bevbox_sh(0.10f,0.10f,0.30f,0.022f);
    } else {              /* pistol pickup */
      tintf(0.25f,1.5f,0.7f);
      set_uM(M,items[i].x,bob,items[i].z); bevbox_sh(0.09f,0.13f,0.30f,0.020f);
      float M2[9],R[9]; m3rotX(R,PI/3); m3mul(M2,M,R);
      set_uM(M2,items[i].x,bob-0.07f,items[i].z); bevbox_sh(0.07f,0.16f,0.07f,0.018f);
    }
    /* counter-rotating halo plates on world-time: they freeze with the world */
    for(int k=0;k<3;k++){
      float H[9]; m3rotY(H,-wtime*2.2f+i*0.7f+k*2.0943951f);
      float o[3]; m3v(H,0.30f,0,0,o);
      if(items[i].type==0) tintf(1.1f,1.1f,1.2f); else tintf(0.18f,1.2f,0.50f);
      set_uM(H,items[i].x+o[0],bob+o[1],items[i].z+o[2]); box_sh(0.02f,0.012f,0.14f);
    }
    figDim=1.0f;
  }
  glUniform1f(uEmis,0);
}

static void draw_shards(void){
  float M[9],RY[9],RX[9];
  glUniform1f(uEmis,1); glUniform1f(uBump,0); glUniform1f(uGloss,0.6f);
  for(int i=0;i<MAXSHARD;i++){
    Shard*s=&shards[i]; if(s->life<=0)continue;
    float a=s->life/s->max;
    m3rotY(RY,s->yaw); m3rotX(RX,s->pit); m3mul(M,RY,RX);
    glUniform3f(uTint,s->r*a*1.6f,s->g*a*1.6f,s->b*a*1.6f);
    set_uM(M,s->x,s->y,s->z);
    wedge_sh(s->sx,s->sy,s->sz);   /* shattered-glass facet, not a die */
  }
  glUniform1f(uEmis,0);
}

/* camera basis for billboards, refreshed once per frame from pyaw+ppitch.
 * The old sprites rotated only about Y, so every glow quad went edge-on the
 * moment you looked down — fatal in a game built on platforms and leaps. */
static float bbRx=1,bbRz=0, bbUx=0,bbUy=1,bbUz=0;
static void bb_quad(float x,float y,float z,float hr,float hu,
                    float r,float g,float b,float a){
  glColor4f(r,g,b,a);
  glTexCoord2f(0,0); glVertex3f(x-bbRx*hr-bbUx*hu, y-bbUy*hu, z-bbRz*hr-bbUz*hu);
  glTexCoord2f(1,0); glVertex3f(x+bbRx*hr-bbUx*hu, y-bbUy*hu, z+bbRz*hr-bbUz*hu);
  glTexCoord2f(1,1); glVertex3f(x+bbRx*hr+bbUx*hu, y+bbUy*hu, z+bbRz*hr+bbUz*hu);
  glTexCoord2f(0,1); glVertex3f(x-bbRx*hr+bbUx*hu, y+bbUy*hu, z-bbRz*hr+bbUz*hu);
}
static void billboard(float x,float y,float z,float s,float r,float g,float b,float a){
  bb_quad(x,y,z,s,s,r,g,b,a);
}

/* contact shadows: a soft dark disc laid flat on the floor under every figure,
 * grounding the cut humanoids the way the mirror alone can't. fixed-function,
 * the TX_GLOW radial sprite used as a falloff mask, drawn as an alpha-blend that
 * darkens the obsidian floor. caller wraps the whole batch (see draw_world):
 * program off, TX_GLOW bound, SRC_ALPHA/ONE_MINUS, depth test on / write off. */
static void blob_quad(float x,float y,float z,float s,float a){
  glColor4f(0,0,0,a);
  glTexCoord2f(0,0); glVertex3f(x-s,y,z-s);
  glTexCoord2f(1,0); glVertex3f(x+s,y,z-s);
  glTexCoord2f(1,1); glVertex3f(x+s,y,z+s);
  glTexCoord2f(0,1); glVertex3f(x-s,y,z+s);
}
/* place a figure's shadow: tight under the feet, fading and spreading as the
 * figure lifts off the ground (jump/roll/airborne agents read as floating). */
static void figure_shadow(float x,float footY,float z,float radius,float strength){
  float gy=floor_at(x,z);
  float lift=footY-gy; if(lift<0)lift=0;
  float a=strength*clampf(1.0f-lift/2.2f,0,1);
  if(a<=0.003f)return;
  float s=radius*(1.0f+0.45f*clampf(lift/2.2f,0,1));   /* penumbra widens with height */
  blob_quad(x,gy+0.02f,z,s,a);
}

/* oscilloscope trails + doppler-shaded bullet heads. my<0 mirrors into the
 * floor reflection. fixed-function additive pass. */
static void draw_bullets(float my){
  static float bcol[MAXBUL][3];   /* doppler shade per bullet, shared by both passes */
  glLineWidth(2.0f);
  for(int i=0;i<MAXBUL;i++){
    Bullet*b=&bul[i]; if(!b->on)continue;
    float vr=radial_v(b->x,b->z,b->vx,b->vz);
    float r,g,bl; dopp_rgb(vr,&r,&g,&bl);
    bcol[i][0]=r; bcol[i][1]=g; bcol[i][2]=bl;
    glBegin(GL_LINE_STRIP);
    int n=b->tn;
    for(int k=0;k<n;k++){
      int idx=(b->th-n+k+TRAILN*2)%TRAILN;
      float a=(float)(k+1)/(n+1);
      /* a faint sine wobble across the trail: the oscilloscope read */
      float wob=sinf(k*1.7f+wtime*9.0f)*0.015f;
      glColor4f(r*a,g*a,bl*a,a*0.8f);
      glVertex3f(b->tr[idx][0],my*(b->tr[idx][1]+wob),b->tr[idx][2]);
    }
    glColor4f(r,g,bl,1);
    glVertex3f(b->x,my*b->y,b->z);
    glEnd();
  }
  /* heads */
  glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,texAlb[TX_GLOW]);
  glBegin(GL_QUADS);
  for(int i=0;i<MAXBUL;i++){
    Bullet*b=&bul[i]; if(!b->on)continue;
    float r=bcol[i][0],g=bcol[i][1],bl=bcol[i][2];
    billboard(b->x,my*b->y,b->z,0.09f,r,g,bl,0.95f);
    billboard(b->x,my*b->y,b->z,0.22f,r*0.4f,g*0.4f,bl*0.4f,0.5f);
  }
  glEnd();
  glDisable(GL_TEXTURE_2D);
}

/* the aim-phase laser: a thin pulsing thread from agent gun to player */
static void draw_lasers(void){
  glLineWidth(1.0f);
  glBegin(GL_LINES);
  for(int i=0;i<nen;i++){
    Enemy*e=&en[i];
    if(e->type==2||e->state!=1||e->armp<0.6f)continue;  /* boss leaps reuse state 1 */
    float hx=e->x+sinf(e->yaw)*0.55f+cosf(e->yaw)*0.29f;
    float hz=e->z-cosf(e->yaw)*0.55f+sinf(e->yaw)*0.29f;
    float a=0.10f+0.10f*sinf(wtime*30+e->phase)+0.25f*e->armp*e->state_t;
    glColor4f(1.6f*a,0.6f*a,0.2f*a,a);
    glVertex3f(hx,e->y+1.62f,hz);
    glVertex3f(px,py+1.28f,pz);
  }
  glEnd();
}


/* player laser pointer: the replacement for the old screen-space crosshair. */
static void draw_player_laser(void){
  if(gstate!=ST_PLAY||pammo<=0||rollT>0||swingT>0||swingCD>0)return;
  float mx,my,mz,dx,dy,dz,hx,hy,hz,ld;
  player_laser(&mx,&my,&mz,&dx,&dy,&dz,&hx,&hy,&hz,&ld);
  /* the VISIBLE beam stops on geometry — the dot lands on the wall/floor the
   * gun is pointed at ("know where it's aimed"), even while the round's
   * energy budget (ld from player_laser) is what the sim actually uses */
  float vfull=ray_wall(mx,my,mz,dx,dy,dz,GUN_MAX_RANGE);
  if(dy<-1e-4f){ float tf=(my-(floor_at(mx+dx*vfull,mz+dz*vfull)+0.02f))/-dy;
                 if(tf>0&&tf<vfull)vfull=tf; }
  if(dy> 1e-4f){ float tc=(wallh-0.03f-my)/dy; if(tc>0&&tc<vfull)vfull=tc; }
  if(vfull<ld){ ld=vfull; hx=mx+dx*ld; hy=my+dy*ld; hz=mz+dz*ld; }
  float charge=clampf(gunCharge,0,1);
  int locked = laserTarget>=0;
  /* The beam exists because there is no crosshair in third person. Zoomed, the
   * reticle in draw_hud does that job better, and a full-strength beam down the
   * middle of a narrow lens is just glare — so cross-fade rather than keeping
   * both. The leading cap survives at partial strength: it is the charge gauge. */
  float beamF=1.0f-0.88f*ads_amt();
  /* Keep the charge read stable: length carries the information, not flicker. */
  float grow=sstep(charge);
  float pulse=0.98f+0.02f*sinf(gtime*6.0f);
  float fullx=mx+dx*vfull, fully=my+dy*vfull, fullz=mz+dz*vfull;
  float ghost=0.030f;
  /* Faint full-length rail so the growing active segment has a clear direction. */
  glLineWidth(0.75f);
  glBegin(GL_LINES);
  glColor4f(locked?0.22f:0.01f,locked?0.04f:0.18f,locked?0.03f:0.08f,ghost*(locked?1.8f:1.0f));
  glVertex3f(mx,my,mz);
  glColor4f(locked?0.16f:0.01f,locked?0.02f:0.10f,locked?0.02f:0.05f,ghost*(locked?0.9f:0.45f));
  glVertex3f(fullx,fully,fullz);
  glEnd();
  if(ld>0.03f){
    glLineWidth(1.4f+2.4f*grow);
    glBegin(GL_LINES);
    glColor4f((locked?1.45f:0.07f)*pulse,(locked?0.20f:1.25f)*pulse,(locked?0.10f:0.58f)*pulse,(0.36f+0.22f*grow)*beamF);
    glVertex3f(mx,my,mz);
    glColor4f((locked?1.05f:0.04f)*pulse,(locked?0.12f:0.90f)*pulse,(locked?0.08f:0.42f)*pulse,(0.28f+0.28f*grow)*beamF);
    glVertex3f(hx,hy,hz);
    glEnd();
  }
  glLineWidth(1.0f);
  glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,texAlb[TX_GLOW]);
  glBegin(GL_QUADS);
  /* March small ticks down the charged section so length growth is visible even at shallow angles. */
  int ticks=(int)(2+grow*11);
  for(int q=1;q<=ticks&&beamF>0.2f;q++){
    float f=(float)q/(ticks+1), a=(0.08f+0.18f*f)*(0.35f+0.65f*grow)*beamF;
    billboard(mx+(hx-mx)*f,my+(hy-my)*f,mz+(hz-mz)*f,0.020f+0.035f*grow,
      locked?0.95f:0.04f,locked?0.13f:0.70f+0.65f*grow,locked?0.08f:0.28f,a);
  }
  /* Leading cap: mostly constant size; position moving outward is the reload read. */
  billboard(hx,hy,hz,0.080f+0.055f*grow,locked?1.90f:0.28f,locked?0.28f:1.90f,locked?0.12f:0.78f,0.84f);
  billboard(hx,hy,hz,0.190f+0.090f*grow,locked?0.75f:0.04f,locked?0.10f:0.75f,locked?0.06f:0.28f,0.28f);
  glEnd();
  glDisable(GL_TEXTURE_2D);
}

/* the katana's wake: an emerald crescent swept across the cut plane,
 * strongest at the blade and fading along the arc behind it */
static void draw_slash(void){
  if(swingT<=0)return;
  float s=clampf(swingT/SWING_TIME,0,1);
  float yr=pyaw*PI/180.0f;
  float cut=sstep(s);
  float aS=yr+0.66f, aE=aS-1.42f*cut;   /* right-shoulder -> left-hip, like the arm */
  float cy=py+1.34f;
  /* two layers: a wide soft wake under a narrow boosted core */
  for(int layer=0;layer<2;layer++){
    float r0=layer?0.60f:0.50f, r1=layer?1.42f:1.62f;
    glBegin(GL_TRIANGLE_STRIP);
    for(int k=0;k<=16;k++){
      float f=k/16.0f, a=aS+(aE-aS)*f;
      float yk=cy+0.24f-0.58f*f;              /* high right -> low left diagonal */
      float al=0.62f*f*f*(0.35f+0.65f*cut)*(layer?1.25f:0.45f);
      float sa=sinf(a),ca=-cosf(a);
      if(layer) glColor4f(0.20f*al,1.00f*al,0.45f*al,al);
      else      glColor4f(0.10f*al,0.55f*al,0.30f*al,al);
      glVertex3f(px+sa*r0,yk,pz+ca*r0);
      glVertex3f(px+sa*r1,yk-0.08f,pz+ca*r1);
    }
    glEnd();
  }
}

/* upload the 8 nearest lights (static + temp) to (x,z). Once per frame for
 * the world batches — and once per FIGURE, so an agent 25u away is lit by
 * the emitters around IT, not whichever eight happen to hug the camera. */
static void set_lights(float x,float z){
  float lp[SHLIGHTS*4]={0}, lc[SHLIGHTS*3]={0}; int ln=0;
  /* Bounded insertion into an 8-slot array, one pass over the candidates. The
   * old version ran a partial SELECTION SORT — up to 8 passes over as many as
   * MAXLIGHT+MAXTEMPL = 80 entries — and it runs once per figure per pass, so a
   * crowded TERMINAL was doing ~50 of them a frame for eight results. */
  float bd[SHLIGHTS]; int bi[SHLIGHTS], bt[SHLIGHTS];
  for(int k=0;k<SHLIGHTS;k++){ bd[k]=1e30f; bi[k]=-1; bt[k]=0; }
  for(int pass=0;pass<2;pass++){
    int n = pass? MAXTEMPL : nlights;
    for(int i=0;i<n;i++){
      float lx,lz;
      if(pass){ if(templ_[i].life<=0)continue; lx=templ_[i].x; lz=templ_[i].z; }
      else    { lx=lights[i].x; lz=lights[i].z; }
      float dx=lx-x,dz=lz-z, d2=dx*dx+dz*dz;
      if(d2>=bd[SHLIGHTS-1])continue;              /* worse than our worst */
      int k=SHLIGHTS-1;
      while(k>0 && bd[k-1]>d2){ bd[k]=bd[k-1]; bi[k]=bi[k-1]; bt[k]=bt[k-1]; k--; }
      bd[k]=d2; bi[k]=i; bt[k]=pass;
    }
  }
  for(int k=0;k<SHLIGHTS;k++){
    if(bi[k]<0)break;
    if(bt[k]){ TempL*t=&templ_[bi[k]];
      lp[ln*4]=t->x;lp[ln*4+1]=t->y;lp[ln*4+2]=t->z;lp[ln*4+3]=t->r;
      lc[ln*3]=t->cr;lc[ln*3+1]=t->cg;lc[ln*3+2]=t->cb;
    } else { Light*l=&lights[bi[k]];
      lp[ln*4]=l->x;lp[ln*4+1]=l->y;lp[ln*4+2]=l->z;lp[ln*4+3]=l->r;
      lc[ln*3]=l->cr;lc[ln*3+1]=l->cg;lc[ln*3+2]=l->cb; }
    ln++;
  }
  /* skip the two array uploads when the selected set is bit-identical to the
   * last one — walking a corridor re-sends the same eight lights every figure */
  static float pl[SHLIGHTS*4], pc[SHLIGHTS*3]; static int pn=-1;
  if(pn==ln && !memcmp(pl,lp,sizeof lp) && !memcmp(pc,lc,sizeof lc)) return;
  pn=ln; memcpy(pl,lp,sizeof lp); memcpy(pc,lc,sizeof lc);
  if(ln>QUAL[qual].lights)ln=QUAL[qual].lights;   /* tier caps the shader loop */
  glUniform1i(uNL,ln);
  glUniform4fv(uLpos,SHLIGHTS,lp);
  glUniform3fv(uLcol,SHLIGHTS,lc);
}

static void draw_world(float camx,float camy,float camz){
  const LevelDef*L=&LEVELS[curlevel];
  glUseProgram(prog);
  glUniform3f(uCam,camx,camy,camz);
  glUniform1i(uAlb,0); glUniform1i(uNrm,1);
  glUniform1f(uTime,wtime); glUniform1f(uRain,0); glUniform1f(uAlpha,1);
  glUniform3f(uFog,L->fog[0],L->fog[1],L->fog[2]);
  glUniform1f(uRim,0);   /* figures opt into the rim; world stays matte */
  glUniform1f(uEmisM,0); /* only the world batches carry an emissive mask —
                            figures bind TX_GLOW whose alpha is 255 at centre */
  glUniform3f(uNSc,1,1,1);
  /* with the post stack up the scene stays linear HDR; without it, the world
   * program has to tonemap on its own or the frame reaches the window unlit */
  glUniform1f(uTonemap, postOK?0.0f:1.0f);

  set_lights(camx,camz);

  /* ---- the cheap planar mirror: draw the movers y-flipped under the
   * world, then lay the glossy floor OVER them at partial alpha. demo
   * trick, not physics — but on black obsidian it reads as reflection.
   *
   * Two passes over the same body: pass 0 mirrored (refl=1) then the world
   * geometry laid over it, pass 1 upright. This used to be a backwards `goto`
   * into the middle of the function, which worked but made the block impossible
   * to reason about or add a third path to. */
  for(int pass=0;pass<2;pass++){
  refl = (pass==0);
  /* props read the glow sprite's white centre texel: base = uTint verbatim */
  glActiveTexture_(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texAlb[TX_GLOW]);
  if(refl){
    /* mirror only grounded agents near the camera — elevated figures have
     * no floor to reflect in, and far ones aren't worth the double draw */
    for(int i=0;i<nen;i++){
      /* Skip corpses before paying for a set_lights selection — but NOT while
       * they are still collapsing: dieT keeps a body drawable for a beat after
       * death so the shards emerge from it. These skips were added as a pure
       * perf win before the death animation existed and silently suppressed it. */
      if(en[i].state==4 && en[i].dieT<=0)continue;
      float ddx=en[i].x-camx, ddz=en[i].z-camz;
      if(ddx*ddx+ddz*ddz>22.0f*22.0f)continue;
      { float fcy,fr; fig_sphere(&en[i],&fcy,&fr);
        if(!frustum_sphere_m(en[i].x,fcy,en[i].z,fr,1))continue; }
      /* fade the mirror image out over the same lift the contact shadow uses,
       * instead of blinking it off the instant the figure leaves the floor */
      float rf=clampf(1.0f-en[i].y/0.9f,0,1);
      if(rf<=0.02f)continue;
      figDim=rf;
      set_lights(en[i].x,en[i].z);
      if(en[i].type==2) draw_boss(&en[i]); else draw_agent(&en[i],0.6f);
      figDim=1.0f;
    }
    /* the avatar reflects too — it was the one figure on the glossy floor
     * casting no mirror image, which read as the player floating above it */
    if((gstate==ST_PLAY||gstate==ST_WIN) && ads_amt()<0.88f){
      float pf=clampf(1.0f-py/0.9f,0,1);
      if(pf>0.02f){ figDim=pf; set_lights(px,pz); draw_player(); figDim=1.0f; }
    }
    set_lights(camx,camz);
    draw_items();
    draw_shards();
    refl=0;
    /* mirrored additive trails, faint */
    glUseProgram(0);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    draw_bullets(-1.0f);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glUseProgram(prog);
    /* floor, blended over the mirror image */
    glActiveTexture_(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texAlb[TX_FLOOR]);
    glActiveTexture_(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,texNrm[TX_FLOOR]);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(uAlpha,0.78f); glUniform1f(uGloss,1.0f);
    glUniform3f(uTint,L->floort[0],L->floort[1],L->floort[2]);
    glUniform1f(uBump,1); glUniform1f(uEmis,0);
    glUniform1f(uEmisM,1.8f);          /* hairline seams feed the bloom */
    float I[9]; m3id(I); set_uM(I,0,0,0);
    glCallList(worldList[1]);
    glDisable(GL_BLEND);
    glUniform1f(uAlpha,1);
    /* walls (with rain) and ceiling, opaque, in the sector's climate */
    int texof[2]={TX_WALL,TX_CEIL}; int bid[2]={0,2}; float gls[2]={0.55f,0.3f};
    float emk[2]={3.5f,1.3f};   /* wall circuit traces / ceiling light slots */
    for(int b=0;b<2;b++){
      glActiveTexture_(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texAlb[texof[b]]);
      glActiveTexture_(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,texNrm[texof[b]]);
      glUniform1f(uGloss,gls[b]); glUniform1f(uRain,b==0?1.0f:0.0f);
      glUniform1f(uEmisM,emk[b]);
      float tk=b==0?1.0f:0.8f;
      glUniform3f(uTint,L->wallt[0]*tk,L->wallt[1]*tk,L->wallt[2]*tk);
      glCallList(worldList[bid[b]]);
    }
    glUniform1f(uRain,0);
    glUniform1f(uEmisM,0);
    /* emissive emerald lips along platform/step edges (white-centre glow texel
     * × emerald tint, near-full emissive) — crisp verticality, SUPERHOT-clean */
    if(bn[3]>0){
      glActiveTexture_(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texAlb[TX_GLOW]);
      glUniform1f(uGloss,0.0f); glUniform1f(uBump,0); glUniform1f(uEmis,0.92f);
      glUniform3f(uTint,0.20f,1.35f,0.55f);
      glCallList(worldList[3]);
      glUniform1f(uEmis,0);
    }
    continue;              /* pass 0 done; on to the upright pass */
  }
  glActiveTexture_(GL_TEXTURE0);

  /* contact shadows first, on the floor, under the upright figures */
  glUseProgram(0);
  glDepthMask(GL_FALSE);
  glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f,-1.0f);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,texAlb[TX_GLOW]);
  glBegin(GL_QUADS);
  for(int i=0;i<nen;i++){
    if(en[i].state==4 && en[i].dieT<=0)continue;
    float ddx=en[i].x-camx, ddz=en[i].z-camz;
    if(ddx*ddx+ddz*ddz>38.0f*38.0f)continue;
    if(!frustum_sphere(en[i].x,floor_at(en[i].x,en[i].z),en[i].z,2.0f))continue;
    if(en[i].type==2) figure_shadow(en[i].x,en[i].y,en[i].z,1.4f,0.6f);
    else figure_shadow(en[i].x,en[i].y,en[i].z,0.52f,0.55f);
  }
  if(gstate==ST_PLAY||gstate==ST_WIN) figure_shadow(px,py,pz,rollT>0?0.58f:0.46f,0.60f);
  for(int i=0;i<nitems;i++){
    if(items[i].taken)continue;
    float ddx=items[i].x-camx, ddz=items[i].z-camz;
    if(ddx*ddx+ddz*ddz>38.0f*38.0f)continue;
    float bob=floor_at(items[i].x,items[i].z)+0.45f+0.1f*sinf(wtime*2.5f+i);
    figure_shadow(items[i].x,bob,items[i].z,0.26f,0.5f);
  }
  glEnd();
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glDisable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(0,0);
  glDepthMask(GL_TRUE);
  glUseProgram(prog);

  for(int i=0;i<nen;i++){       /* beyond ~36u the fog has swallowed them */
    if(en[i].state==4 && en[i].dieT<=0)continue;   /* still collapsing? draw it */
    float ddx=en[i].x-camx, ddz=en[i].z-camz;
    if(ddx*ddx+ddz*ddz>38.0f*38.0f)continue;
    { float fcy,fr; fig_sphere(&en[i],&fcy,&fr);
      if(!frustum_sphere(en[i].x,fcy,en[i].z,fr))continue; }
    set_lights(en[i].x,en[i].z);   /* lit by the emitters around THEM */
    if(en[i].type==2) draw_boss(&en[i]); else draw_agent(&en[i],1.0f);
  }
  if(gstate==ST_PLAY||gstate==ST_WIN){ set_lights(px,pz); draw_player(); }
  set_lights(camx,camz);
  draw_items();
  draw_shards();
  glUseProgram(0);

  /* additive pass: light orbs, particles, trails, lasers */
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE);
  glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,texAlb[TX_GLOW]);
  glBegin(GL_QUADS);
  for(int i=0;i<nlights;i++)
    billboard(lights[i].x,lights[i].y,lights[i].z,0.30f,
      lights[i].cr*0.22f,lights[i].cg*0.22f,lights[i].cb*0.22f,1);
  if(mzT>0){ /* the avatar's muzzle flash: core + 4-point star spikes */
    float a=mzT/0.06f;
    billboard(mzX,mzY,mzZ,0.10f+0.10f*a, 0.9f,1.8f,1.0f,a);
    billboard(mzX,mzY,mzZ,0.30f, 0.25f,0.9f,0.45f,a*0.5f);
    bb_quad(mzX,mzY,mzZ,0.55f*a,0.030f, 0.7f,1.9f,1.0f,a*0.8f);
    bb_quad(mzX,mzY,mzZ,0.030f,0.30f*a, 0.7f,1.9f,1.0f,a*0.6f);
  }
  /* agent muzzle flashes: amber against the player's emerald, so at a glance
   * you can tell who fired. They live on world time, so a flash caught at
   * MINTS hangs in the air like everything else. */
  for(int i=0;i<nen;i++){
    if(en[i].state==4||en[i].mzT<=0)continue;   /* corpses don't keep flashing */
    float a=clampf(en[i].mzT/0.07f,0,1);
    billboard(en[i].mzx,en[i].mzy,en[i].mzz,0.09f+0.09f*a, 1.9f,1.0f,0.35f,a);
    billboard(en[i].mzx,en[i].mzy,en[i].mzz,0.28f, 0.90f,0.40f,0.12f,a*0.5f);
    bb_quad(en[i].mzx,en[i].mzy,en[i].mzz,0.40f*a,0.022f, 1.9f,1.0f,0.35f,a*0.7f);
    bb_quad(en[i].mzx,en[i].mzy,en[i].mzz,0.022f,0.22f*a, 1.9f,1.0f,0.35f,a*0.5f);
  }
  /* a soft aura over each pickup — the eye/blade halos are gone: real bloom
   * picks those up straight off the emissive geometry now */
  for(int i=0;i<nitems;i++){
    if(items[i].taken)continue;
    float bob=floor_at(items[i].x,items[i].z)+0.45f+0.1f*sinf(wtime*2.5f+i);
    if(items[i].type==0) billboard(items[i].x,bob,items[i].z,0.34f, 0.7f,0.7f,0.78f,0.5f);
    else                 billboard(items[i].x,bob,items[i].z,0.34f, 0.12f,0.8f,0.38f,0.5f);
  }
  /* particles as small glow sprites: fixed 4px GL_POINTS neither scaled
   * with distance nor matched the sprite language of every other emitter */
  for(int i=0;i<MAXPART;i++){
    Part*p=&parts[i]; if(p->life<=0)continue;
    float a=p->life/p->max;
    bb_quad(p->x,p->y,p->z,0.045f,0.045f,p->cr,p->cg,p->cb,a);
  }
  glEnd();
  glDisable(GL_TEXTURE_2D);
  draw_bullets(1.0f);
  draw_lasers();
  draw_player_laser();
  if(gstate==ST_PLAY)draw_slash();
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  }   /* end two-pass loop; pass 0 `continue`s after laying down the world */
}

/* ---------------------------------------------------------------- HUD
 * scanlines + signal glitch on damage. all text is the synthesized bitfont. */
/* The visible HUD box in virtual coordinates. Equals 0,0..HUDW,HUDH at 16:9 and
 * grows along the long axis otherwise — anything that fills or spans the screen
 * has to know, or a non-16:9 window shows unwashed strips down the sides.
 * Recomputed by draw_hud when it sets up the ortho. */
static float hudX0=0,hudY0=0,hudX1=HUDW,hudY1=HUDH;
static int rainInit=0;
static float rainX[40],rainSpd[40],rainPh[40]; static int rainLen[40];
static void draw_title_rain(void){
  if(!rainInit){ rainInit=1; unsigned sv=rngs; rngs=0xD161741u;
    for(int i=0;i<40;i++){ rainX[i]=frand()*HUDW; rainSpd[i]=60+frand()*180;   /* NB: spread across the visible box below */
      rainPh[i]=frand()*2000; rainLen[i]=6+(int)(frand()*9); }
    rngs=sv; }
  for(int i=0;i<40;i++){
    float span=HUDH+rainLen[i]*20.0f;
    float hy=fmodf(rainPh[i]+gtime*rainSpd[i],span)-rainLen[i]*20.0f;
    for(int k=0;k<rainLen[i];k++){
      float gy=hy-k*20.0f; if(gy<hudY0-20||gy>hudY1)continue;
      float a=(k==0)?0.9f:0.55f*(1.0f-(float)k/rainLen[i]);
      char cs[2]={0,0};
      unsigned h=ihash((unsigned)i*131u+(unsigned)k*17u+(unsigned)(gtime*(k==0?9:2)));
      cs[0]= (h&1)? 'A'+(h>>1)%26 : '0'+(h>>1)%10;
      if(k==0)glColor4f(0.75f,1.0f,0.8f,a); else glColor4f(0.05f,0.85f,0.35f,a);
      draw_text(hudX0+rainX[i]*(hudX1-hudX0)/HUDW,gy,2.0f,cs);
    }
  }
}
/* full-window quad in HUD ortho space; caller sets the colour first */
static void hud_fill(void){
  glBegin(GL_QUADS);
  glVertex2f(hudX0,hudY0); glVertex2f(hudX1,hudY0);
  glVertex2f(hudX1,hudY1); glVertex2f(hudX0,hudY1);
  glEnd();
}
static void draw_hud(void){
  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
  /* The HUD is authored once, in a virtual 1280x720 space, and this one ortho
   * call maps it onto whatever the drawable happens to be. That is why none of
   * the ~40 hand-placed coordinates below had to change when the window became
   * resizable — the alternative was rewriting every one of them in terms of fbW.
   * Aspect is preserved and the result letterboxed, so a stretched window does
   * not stretch the wordmark. */
  { float sa=(float)fbW/(float)fbH, ha=(float)HUDW/(float)HUDH;
    float vw=HUDW, vh=HUDH;
    if(sa>ha) vw=HUDH*sa;          /* wider than 16:9: widen the HUD box   */
    else      vh=HUDW/sa;          /* taller: heighten it                  */
    hudX0=(HUDW-vw)*0.5f; hudX1=(HUDW+vw)*0.5f;
    hudY0=(HUDH-vh)*0.5f; hudY1=(HUDH+vh)*0.5f;
    glOrtho(hudX0,hudX1,hudY1,hudY0,-1,1); }
  glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

  /* NOTE: the vignette and CRT scanlines used to be drawn here as geometry.
   * They live in the composite shader now (post_end), which puts them after
   * the tonemap where they belong and keeps the HUD text out from under them. */

  if(gstate==ST_PLAY||gstate==ST_WIN){
    if(rollCD>0){ /* minimal remaining HUD: roll recharge */
      float k=1.0f-rollCD/ROLL_CD;
      glColor4f(0.4f,0.9f,0.6f,0.55f);
      glBegin(GL_QUADS);
      glVertex2f(HUDW/2-28,HUDH-42);glVertex2f(HUDW/2-28+56*k,HUDH-42);
      glVertex2f(HUDW/2-28+56*k,HUDH-39);glVertex2f(HUDW/2-28,HUDH-39);
      glEnd();
    }

    /* ADS reticle. The world-space laser is the hip-fire affordance; zoomed, a
     * screen-space mark is both more precise and the only thing that still works
     * when the pistol is dry (draw_player_laser early-outs at pammo==0, which in
     * first person would leave you with no aiming reference at all). Four ticks
     * around a gap, so it never covers the thing you are shooting. Turns red on
     * a charged lock, matching the beam it replaced. */
    { float ar=ads_amt();
      if(ar>0.12f){
        float cx=HUDW*0.5f, cy=HUDH*0.5f;
        float gap=7.0f-3.0f*ar, len=8.0f, th=1.6f;
        float a=(ar-0.12f)/0.88f; a*=a;
        int lk = laserTarget>=0;
        /* spread opens with the gun's unsettled aim: at a dead stop the barrel
         * IS the look ray, mid-run it is not, and the reticle should say so */
        gap += 10.0f*(1.0f-aimSet);
        /* TWO passes: a black bar 1px fatter, then the mark on top. An emerald
         * reticle over an emerald agent lit by emerald bloom is invisible, which
         * is exactly the moment you need it — the outline is what makes it read
         * against the brightest thing in the frame. */
        for(int pass=0;pass<2;pass++){
          float t2 = pass? th : th+1.3f;
          float g2 = pass? 0.0f : 1.1f;      /* backing pokes past the tips */
          if(pass==0) glColor4f(0,0,0,0.72f*a);
          else if(lk) glColor4f(1.7f,0.30f,0.20f,0.98f*a);
          else        glColor4f(0.88f,1.0f,0.92f,0.95f*a);
          glBegin(GL_QUADS);
          for(int k=0;k<4;k++){
            float dx=(k==0)?-1.0f:(k==1)?1.0f:0.0f;
            float dz=(k==2)?-1.0f:(k==3)?1.0f:0.0f;
            float x0=cx+dx*(gap-g2), y0=cy+dz*(gap-g2);
            float x1=x0+dx*(len+2.0f*g2), y1=y0+dz*(len+2.0f*g2);
            float px_=fabsf(dx)>0?0:t2, pz_=fabsf(dz)>0?0:t2;
            glVertex2f(x0-px_,y0-pz_); glVertex2f(x1-px_,y1-pz_);
            glVertex2f(x1+px_,y1+pz_); glVertex2f(x0+px_,y0+pz_);
          }
          glEnd();
        }
        /* centre dot only once fully shouldered — it is the "settled" tell */
        if(ar>0.85f&&aimSet>0.9f){
          glColor4f(0,0,0,0.7f);
          glBegin(GL_QUADS);
          glVertex2f(cx-2.4f,cy-2.4f); glVertex2f(cx+2.4f,cy-2.4f);
          glVertex2f(cx+2.4f,cy+2.4f); glVertex2f(cx-2.4f,cy+2.4f);
          glEnd();
          glColor4f(lk?1.8f:0.95f,lk?0.25f:1.15f,lk?0.15f:1.0f,0.95f);
          glBegin(GL_QUADS);
          glVertex2f(cx-1.2f,cy-1.2f); glVertex2f(cx+1.2f,cy-1.2f);
          glVertex2f(cx+1.2f,cy+1.2f); glVertex2f(cx-1.2f,cy+1.2f);
          glEnd();
        }
      } }

    /* agents remaining, top right */
    { char b2[24]; snprintf(b2,24,"AGENTS %02d",nalive);
      glColor4f(0.6f,1.0f,0.75f,0.9f);
      draw_text(HUDW-30-textw(b2,2.6f),26,2.6f,b2); }
    { char b2[24]; snprintf(b2,24,"AMMO %02d",pammo);
      glColor4f(pammo>0?0.55f:1.0f,pammo>0?1.0f:0.25f,pammo>0?0.70f:0.18f,0.9f);
      draw_text(HUDW-30-textw(b2,2.2f),58,2.2f,b2); }
    /* Health. It used to be five emissive pads up the avatar's spine, which is a
     * lovely idea and was the brightest object on screen in a game viewed almost
     * entirely from behind. Here instead, in the same corner and the same voice
     * as AGENTS and AMMO: a number, plus a short bar so the trend reads at a
     * glance without being counted. Goes amber under 55, red under 25, and
     * pulses on world time once it is critical. */
    { int hp=(int)(php+0.5f); if(hp<0)hp=0;
      float f=clampf(php/100.0f,0,1);
      float hr,hg,hb;
      if(f>0.55f)      { hr=0.55f; hg=1.00f; hb=0.70f; }
      else if(f>0.25f) { hr=1.00f; hg=0.80f; hb=0.30f; }
      else             { float p=0.65f+0.35f*sinf(wtime*12.0f);
                         hr=1.00f*p; hg=0.28f*p; hb=0.20f*p; }
      char b2[24]; snprintf(b2,24,"VITALS %03d",hp);
      glColor4f(hr,hg,hb,0.9f);
      draw_text(HUDW-30-textw(b2,2.2f),90,2.2f,b2);
      float bw=textw(b2,2.2f), bx=HUDW-30-bw, by=114;
      glColor4f(hr*0.22f,hg*0.22f,hb*0.22f,0.55f);
      glBegin(GL_QUADS); glVertex2f(bx,by); glVertex2f(bx+bw,by);
        glVertex2f(bx+bw,by+4); glVertex2f(bx,by+4); glEnd();
      glColor4f(hr,hg,hb,0.85f);
      glBegin(GL_QUADS); glVertex2f(bx,by); glVertex2f(bx+bw*f,by);
        glVertex2f(bx+bw*f,by+4); glVertex2f(bx,by+4); glEnd(); }
    /* OVERLORD health bar: violet when full, bleeds toward red; phase ticks at
       the 66% and 33% thresholds where its behaviour escalates */
    if(bossIdx>=0 && bossIdx<nen && en[bossIdx].state!=4){
      float frac=clampf((float)en[bossIdx].hp/(float)(bossMaxHp>0?bossMaxHp:1),0,1);
      float bw=560, bx=(HUDW-bw)*0.5f, by=34, bh=16;
      glColor4f(0,0,0,0.55f);
      glBegin(GL_QUADS); glVertex2f(bx-3,by-3);glVertex2f(bx+bw+3,by-3);
        glVertex2f(bx+bw+3,by+bh+3);glVertex2f(bx-3,by+bh+3); glEnd();
      glColor4f(1.0f-0.30f*frac, 0.15f+0.20f*frac, 0.22f+0.78f*frac, 0.92f);
      glBegin(GL_QUADS); glVertex2f(bx,by);glVertex2f(bx+bw*frac,by);
        glVertex2f(bx+bw*frac,by+bh);glVertex2f(bx,by+bh); glEnd();
      glColor4f(0,0,0,0.6f);
      glBegin(GL_QUADS);
      for(float t=0.33f;t<0.7f;t+=0.33f){ float xx=bx+bw*t;
        glVertex2f(xx-1,by);glVertex2f(xx+1,by);glVertex2f(xx+1,by+bh);glVertex2f(xx-1,by+bh); }
      glEnd();
      glColor4f(0.92f,0.82f,1.0f,0.95f);
      draw_text((HUDW-textw("OVERLORD",2.6f))*0.5f, by+bh+8, 2.6f, "OVERLORD");
    }
    /* level intro card */
    if(msgT>0){
      float a=clampf(msgT,0,1);
      char b2[32]; snprintf(b2,32,"SECTOR %d - %s",curlevel+1,LEVELS[curlevel].name);
      glColor4f(0.7f,1.0f,0.8f,a);
      draw_text((HUDW-textw(b2,3.4f))/2,90,3.4f,b2);
      glColor4f(0.4f,0.9f,0.6f,a*0.8f);
      draw_text((HUDW-textw("TIME MOVES WHEN YOU DO",2.0f))/2,134,2.0f,"TIME MOVES WHEN YOU DO");
    }
    /* damage: signal distortion — torn horizontal slices + red wash */
    if(dmgFlash>0){
      glColor4f(0.5f,0.04f,0.03f,dmgFlash*0.45f);
      hud_fill();
      glBlendFunc(GL_SRC_ALPHA,GL_ONE);
      for(int k=0;k<5;k++){
        float gy=hudY0+vrand()*(hudY1-hudY0), gh=3+vrand()*14;
        float gx=(vrand()-0.5f)*70*dmgFlash;
        glColor4f(0.05f,0.8f,0.3f,dmgFlash*0.35f);
        glBegin(GL_QUADS);
        glVertex2f(hudX0+gx,gy);glVertex2f(hudX1+gx,gy);
        glVertex2f(hudX1+gx,gy+gh);glVertex2f(hudX0+gx,gy+gh);
        glEnd();
      }
      glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    }
  }
  if(gstate==ST_TITLE){
    glColor4f(0,0,0,0.55f);
    hud_fill();
    glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    draw_title_rain();
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    /* shadowed wordmark */
    glColor4f(0.0f,0.25f,0.10f,0.9f);
    draw_text((HUDW-textw("^ILATION",13))/2+5,125,13,"^ILATION");
    glColor4f(0.65f,1.0f,0.75f,1);
    draw_text((HUDW-textw("^ILATION",13))/2,120,13,"^ILATION");
    glColor4f(0.35f,0.85f,0.55f,1);
    draw_text((HUDW-textw("TIME MOVES WHEN YOU MOVE",2.6f))/2,250,2.6f,"TIME MOVES WHEN YOU MOVE");
    /* level select */
    for(int i=0;i<NLEVEL;i++){
      char b2[24]; snprintf(b2,24,"%d %s",i+1,LEVELS[i].name);
      float bw=textw(b2,2.6f)+40, bx=(HUDW-bw)/2, by=326+i*52;
      if(i==curlevel){
        glColor4f(0.06f,0.55f,0.25f,0.35f+0.12f*sinf(gtime*5));
        glBegin(GL_QUADS);glVertex2f(bx,by-12);glVertex2f(bx+bw,by-12);
        glVertex2f(bx+bw,by+32);glVertex2f(bx,by+32);glEnd();
        glColor4f(0.8f,1,0.85f,1);
      } else glColor4f(0.3f,0.6f,0.42f,0.9f);
      draw_text(bx+20,by,2.6f,b2);
    }
    glColor4f(0.85f,1.0f,0.6f,0.7f+0.3f*sinf(gtime*4));
    draw_text((HUDW-textw("CLICK TO JACK IN",3.2f))/2,548,3.2f,"CLICK TO JACK IN");
    /* the seed, so a good roll can be written down and played again */
    { char sb[64]; snprintf(sb,64,"SEED %08X - R REROLLS - Q QUALITY %s",
                            genSeedUsed,QUAL[qual].name);
      glColor4f(0.30f,0.62f,0.45f,0.85f);
      draw_text((HUDW-textw(sb,1.8f))/2,584,1.8f,sb); }
    glColor4f(0.35f,0.55f,0.42f,1);
    draw_text((HUDW-textw("WASD MOVE - SPACE JUMP AND JUMP AGAIN - WALLS KICK BACK - SHIFT ROLL",1.55f))/2,608,1.55f,
      "WASD MOVE - SPACE JUMP AND JUMP AGAIN - WALLS KICK BACK - SHIFT ROLL");
    draw_text((HUDW-textw("LMB FIRE - RMB AIM DOWN SIGHTS - F KATANA - MOVE TO CHARGE - 1-4 SECTOR",1.55f))/2,632,1.55f,
      "LMB FIRE - RMB AIM DOWN SIGHTS - F KATANA - MOVE TO CHARGE - 1-4 SECTOR");
  } else if(gstate==ST_DEAD){
    glColor4f(0,0,0,0.6f);
    hud_fill();
    glColor4f(1.0f,0.25f,0.18f,1);
    draw_text((HUDW-textw("SIGNAL LOST",9))/2,250,9,"SIGNAL LOST");
    glColor4f(0.8f,0.85f,0.8f,0.85f);
    draw_text((HUDW-textw("CLICK TO RE-ENTER",3))/2,430,3,"CLICK TO RE-ENTER");
    draw_text((HUDW-textw("ESC FOR SECTOR SELECT",2))/2,480,2,"ESC FOR SECTOR SELECT");
  } else if(gstate==ST_WIN){
    glColor4f(0,0,0,0.45f);
    hud_fill();
    glColor4f(0.3f,1.0f,0.6f,1);
    draw_text((HUDW-textw("SECTOR CLEAR",8))/2,250,8,"SECTOR CLEAR");
    glColor4f(0.8f,0.9f,0.85f,0.9f);
    /* tenths, not truncated whole seconds: a fast clear used to read "0 SECONDS
     * REAL" even once winT was accumulating correctly, which made the real bug
     * (see shatter_enemy) impossible to tell apart from a rounding artefact. */
    char b2[64]; snprintf(b2,64,"%.1f SECONDS REAL - %.1f SIMULATED",winRealT,winSimT);
    draw_text((HUDW-textw(b2,2.6f))/2,400,2.6f,b2);
    const char*nx = curlevel+1<NLEVEL
      ? "CLICK FOR NEXT SECTOR - ESC FOR SECTOR SELECT"
      : "CLICK TO REPLAY - ESC FOR SECTOR SELECT";
    draw_text((HUDW-textw(nx,2.2f))/2,460,2.2f,nx);
  }
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glMatrixMode(GL_PROJECTION); glPopMatrix();
  glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

/* ---------------------------------------------------------------- screenshot */
/* Captures are REQUESTED from the choreography (pendingShot) and taken right
 * before SwapWindow, while the back buffer still holds the fully rendered
 * frame. Reading it at the top of the next loop, after the swap, worked only
 * because tested drivers happen to preserve the back buffer — post-swap its
 * contents are undefined per spec (and GL_FRONT is empty under Xvfb). */
static const char*pendingShot=0;
static int strictShots, strictFails;      /* see the smoke-gate block below */
static void shot_ppm(const char*path){
  unsigned char*buf=malloc(fbW*fbH*3);
  glPixelStorei(GL_PACK_ALIGNMENT,1);
  glReadPixels(0,0,fbW,fbH,GL_RGB,GL_UNSIGNED_BYTE,buf);
  FILE*f=fopen(path,"wb");
  if(f){
    fprintf(f,"P6\n%d %d\n255\n",fbW,fbH);
    for(int y=fbH-1;y>=0;y--) fwrite(buf+y*fbW*3,1,fbW*3,f);
    fclose(f);
    printf("[dilation] wrote %s\n",path);
  }
  /* --strict: byte-compare against the recorded reference, in-process, so
   * proving a refactor visually neutral is one command and not a shell loop. */
  if(strictShots){
    char ref[128]; snprintf(ref,sizeof ref,"baseline/%s",path);
    FILE*r=fopen(ref,"rb");
    if(!r){ printf("[dilation] STRICT: no %s to compare against\n",ref); strictFails++; }
    else {
      char hdr[64]; int w=0,h=0,mx=0;
      if(fscanf(r,"%63s %d %d %d",hdr,&w,&h,&mx)!=4||w!=fbW||h!=fbH){
        printf("[dilation] STRICT: %s is %dx%d, we render %dx%d\n",ref,w,h,fbW,fbH);
        strictFails++;
      } else {
        fgetc(r);                                  /* the single whitespace */
        long diff=0; int mismatch=0;
        for(int y=fbH-1;y>=0;y--)for(int x=0;x<fbW*3;x++){
          int c=fgetc(r); if(c<0){mismatch=1;break;}
          int got=buf[y*fbW*3+x];
          if(c!=got){ diff++; }
        }
        if(mismatch||diff){
          printf("[dilation] STRICT: %s differs (%ld of %d subpixels)\n",
                 path,diff,fbW*fbH*3);
          strictFails++;
        } else printf("[dilation] STRICT: %s identical\n",path);
      }
      fclose(r);
    }
  }
  free(buf);
}

/* ------------------------------------------------------------ the smoke gate
 * The gate used to be "the nine PPMs are byte-identical to baseline/". That is a
 * fine way to prove a refactor neutral and a useless way to develop: any real
 * change to the art fails it, so it gets re-recorded, and then it is asserting
 * nothing. It is now split in two:
 *
 *   --smoke              structural invariants that survive visual change, run
 *                        over many seeds. This is the gate.
 *   --smoke --strict     additionally byte-compare each shot against baseline/.
 *                        Turn this on when you WANT to prove a change neutral.
 *   --seed-sweep N       invariants only, headless, across N seeds. Cheap, and
 *                        the thing that makes procedural generation safe.
 */
/* strictShots / strictFails are declared above shot_ppm, which consumes them. */
static int poolPeakBul=0, poolPeakPart=0, poolPeakShard=0;

/* Structural invariants for one generated sector. gen_level already retries
 * until gen_attempt() reports success, so this re-checks the SAME properties
 * from the outside: if it ever fails, the generator's own soundness test and
 * this one have drifted apart, which is exactly the bug you want caught. */
static int validate_level(int li,unsigned seed,int verbose){
  gen_level(li,seed);
  const LevelDef*L=&LEVELS[li];
  int want=L->nshoot+L->nstrike+(L->style==3?1:0);
  int nreach=reachable();
  int bad=0;
  float sy=cellh((int)floorf(startx/CELL),(int)floorf(startz/CELL));
  if(sy>100.0f||!circ_free(startx,startz,0.34f,sy)){
    printf("  FAIL %-8s seed %08x: spawn is inside geometry\n",L->name,seed); bad=1; }
  if(nen!=want){
    printf("  FAIL %-8s seed %08x: placed %d agents, wanted %d\n",L->name,seed,nen,want); bad=1; }
  if(nreach<G*G/16){
    printf("  FAIL %-8s seed %08x: only %d cells reachable from spawn\n",L->name,seed,nreach); bad=1; }
  for(int i=0;i<nen;i++) if(!reach_at(en[i].x,en[i].z)){
    printf("  FAIL %-8s seed %08x: agent %d at (%.1f,%.1f) h=%.2f is stranded\n",
           L->name,seed,i,en[i].x,en[i].z,en[i].y); bad=1; }
  for(int i=0;i<nitems;i++) if(!reach_at(items[i].x,items[i].z)){
    printf("  FAIL %-8s seed %08x: item %d at (%.1f,%.1f) is unreachable\n",
           L->name,seed,i,items[i].x,items[i].z); bad=1; }
  if(nlights>=MAXLIGHT){
    printf("  FAIL %-8s seed %08x: light pool full (%d)\n",L->name,seed,nlights); bad=1; }
  if(nitems>=MAXITEM){
    printf("  FAIL %-8s seed %08x: item pool full (%d)\n",L->name,seed,nitems); bad=1; }
  if(genAttempts>=GEN_TRIES){
    printf("  FAIL %-8s seed %08x: exhausted %d generation attempts\n",L->name,seed,GEN_TRIES); bad=1; }
  if(verbose)
    printf("[dilation] %-8s: %2d agents, %2d items, %2d lights, %4d/%d reachable, %d quads, %d try%s\n",
      L->name,nen,nitems,nlights,nreach,G*G,(bn[0]+bn[1]+bn[2])/36,
      genAttempts,genAttempts==1?"":"s");
  return !bad;
}

/* every sector, every seed in [0,n). Returns the number of failures. */
static int seed_sweep(int n,unsigned base){
  int fails=0;
  for(int s=0;s<n;s++)
    for(int l=0;l<NLEVEL;l++)
      if(!validate_level(l,base+(unsigned)s*2654435761u,0))fails++;
  printf("[dilation] seed sweep: %d seeds x %d sectors = %d layouts, %d failures\n",
         n,NLEVEL,n*NLEVEL,fails);
  return fails;
}

/* NaN is the failure mode that a screenshot comparison can never catch: one bad
 * frame poisons px/pvx and every later frame is garbage that still renders. */
static int nan_check(int frame){
  const float v[]={px,py,pz,pvx,pvy,pvz,pyaw,ppitch,tscale,tsEff,
                   gunCharge,camDist,camYs,avYaw,bobT,aimSet,php};
  for(unsigned i=0;i<sizeof v/sizeof*v;i++) if(v[i]!=v[i]){
    printf("[dilation] SMOKE FAIL: NaN in player state, slot %u, frame %d\n",i,frame);
    return 0; }
  for(int i=0;i<nen;i++){
    const Enemy*e=&en[i];
    if(e->x!=e->x||e->y!=e->y||e->z!=e->z||e->yaw!=e->yaw){
      printf("[dilation] SMOKE FAIL: NaN in agent %d, frame %d\n",i,frame); return 0; }
  }
  int nb=0;
  for(int i=0;i<MAXBUL;i++) if(bul[i].on){
    nb++;
    if(bul[i].x!=bul[i].x||bul[i].y!=bul[i].y||bul[i].z!=bul[i].z){
      printf("[dilation] SMOKE FAIL: NaN in bullet %d, frame %d\n",i,frame); return 0; }
  }
  if(nb>poolPeakBul)poolPeakBul=nb;
  int np=0,ns=0;
  for(int i=0;i<MAXPART;i++) if(parts[i].life>0)np++;
  for(int i=0;i<MAXSHARD;i++) if(shards[i].life>0)ns++;
  if(np>poolPeakPart)poolPeakPart=np;
  if(ns>poolPeakShard)poolPeakShard=ns;
  return 1;
}

/* WASD -> world-space move direction, relative to the view yaw */
static void wasd_dir(int w,int s,int a,int d,float*mx,float*mz){
  float yr=pyaw*PI/180;
  float fx=sinf(yr),fz=-cosf(yr),rx=cosf(yr),rz=sinf(yr);
  *mx=0;*mz=0;
  if(w){*mx+=fx;*mz+=fz;} if(s){*mx-=fx;*mz-=fz;}
  if(d){*mx+=rx;*mz+=rz;} if(a){*mx-=rx;*mz-=rz;}
}

/* ---------------------------------------------------------------- main */
int main(int argc,char**argv){
  unsigned t0;
  int sweepN=0;
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"--smoke"))smoke=1;
    else if(!strcmp(argv[i],"--strict"))strictShots=1;
    else if(!strcmp(argv[i],"--quality")&&i+1<argc){
      const char*q=argv[++i]; qualAuto=0;
      if(!strcmp(q,"low"))qual=0; else if(!strcmp(q,"medium")||!strcmp(q,"med"))qual=1;
      else if(!strcmp(q,"high"))qual=2; else { qualAuto=1; }
    }
    else if(!strcmp(argv[i],"--seed-sweep")) sweepN = (i+1<argc)? (int)strtol(argv[++i],0,0) : 64;
    else if(!strcmp(argv[i],"--titlecap"))titlecap=1;
    else if(!strcmp(argv[i],"--seed")&&i+1<argc)gseed=(unsigned)strtoul(argv[++i],0,0);
    else if(!strcmp(argv[i],"--level")&&i+1<argc){
      long l=strtol(argv[++i],0,0);
      curlevel=(int)(l<0?0:l>=NLEVEL?NLEVEL-1:l);
    }
  }
  /* --seed-sweep is pure CPU: gen_level, place_agent and the flood fill touch no
   * GL and no SDL, so the sweep runs before any window exists and exits. That is
   * what makes it usable as a fast pre-commit check over hundreds of layouts. */
  if(sweepN>0){
    printf("[dilation] sweeping %d seeds x %d sectors...\n",sweepN,NLEVEL);
    int fails=seed_sweep(sweepN,gseed);
    printf(fails? "[dilation] SWEEP FAIL\n" : "[dilation] SWEEP OK\n");
    return fails?1:0;
  }

  if(smoke||titlecap) SDL_setenv("SDL_AUDIODRIVER","dummy",1);

  if(SDL_Init(SDL_INIT_VIDEO)<0){ fprintf(stderr,"SDL: %s\n",SDL_GetError()); return 1; }
  SDL_InitSubSystem(SDL_INIT_AUDIO);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
  /* 4x MSAA. The whole game is hard-edged low-poly crystal against near-black,
   * which is the worst case for edge crawl — this is the single biggest look
   * upgrade available and costs nothing on any GPU from the last two decades.
   * Drop to no-AA and rebuild the window if the driver won't grant it. */
  int msaa=4;
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,msaa);
  /* HIGHDPI is the whole point: without it a Retina display hands us a 1280x720
   * backbuffer and upscales, which on geometry made entirely of hard bright edges
   * against black is the worst possible trade. RESIZABLE because the post chain
   * can now be rebuilt at any size (see free_post). */
  Uint32 wflags = SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI;
  /* ...except under the harness. With HIGHDPI the default framebuffer is 2x on a
   * Retina panel, and compositing a 1280x720 viewport into a 2560x1440
   * multisampled window shifts a few dozen subpixels by one LSB versus doing it
   * into a plain 1280x720 one. Harmless to look at, fatal to a byte gate that is
   * supposed to mean the same thing on every machine. */
  if(smoke||titlecap) wflags &= ~(Uint32)SDL_WINDOW_ALLOW_HIGHDPI;
  SDL_Window*win=SDL_CreateWindow("Δilation",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
    winW,winH,wflags);
  SDL_GLContext ctx=win?SDL_GL_CreateContext(win):0;
  if(!ctx){
    msaa=0;
    if(win)SDL_DestroyWindow(win);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,0);
    win=SDL_CreateWindow("Δilation",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
      winW,winH,wflags);
    ctx=win?SDL_GL_CreateContext(win):0;
  }
  if(!ctx){ fprintf(stderr,"GL: %s\n",SDL_GetError()); return 1; }
  /* the drawable is what we actually render into, and it is NOT the window size */
  SDL_GL_GetDrawableSize(win,&fbW,&fbH);
  SDL_GetWindowSize(win,&winW,&winH);
  /* --smoke and --titlecap must produce a fixed-size image regardless of the
   * display they run on, or the shots stop being comparable between machines. */
  if(smoke||titlecap){ fbW=HUDW; fbH=HUDH;
    /* pin the tier for determinism, but never override an explicit --quality */
    if(qualAuto)qual=2;
    qualAuto=0; }
  printf("[dilation] window %dx%d, drawable %dx%d\n",winW,winH,fbW,fbH);
  if(msaa){ SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES,&msaa);
            if(msaa>1)glEnable(GL_MULTISAMPLE); else msaa=0; }
  printf("[dilation] MSAA: %dx\n",msaa);
  SDL_GL_SetSwapInterval(smoke||titlecap?0:1);
  load_gl();
  printf("[dilation] GL: %s / %s\n",
    (const char*)glGetString(GL_RENDERER),(const char*)glGetString(GL_VERSION));
  /* a software rasterizer (the headless harness) runs the whole GPU on the CPU
   * clock; the frame budget below is a statement about hardware, so it is
   * reported but not enforced there */
  { const char*rn=(const char*)glGetString(GL_RENDERER);
    swRender = rn && (strstr(rn,"llvmpipe")||strstr(rn,"softpipe")||strstr(rn,"SWR")||strstr(rn,"Software")); }

  t0=SDL_GetTicks(); gen_textures();
  printf("[dilation] textures synthesized in %ums\n",SDL_GetTicks()-t0);
  if(smoke){
    /* Structural invariants, over a spread of seeds rather than just the one we
     * are about to play. The old version checked three things at one seed. */
    int fails=0;
    for(int l=0;l<NLEVEL;l++) if(!validate_level(l,gseed,1))fails++;
    fails += seed_sweep(SMOKE_SEEDS,gseed+1u);
    if(fails){ fprintf(stderr,"[dilation] SMOKE FAIL: %d bad layouts\n",fails); return 1; }
  }
  t0=SDL_GetTicks(); reset_game();
  printf("[dilation] world carved in %ums (%d quads)\n",SDL_GetTicks()-t0,(bn[0]+bn[1]+bn[2])/36);
  init_shaders();
  printf("[dilation] shaders up\n");
  init_post(msaa);

  music_init();   /* parse the note-string melodies into step arrays */
  SDL_AudioSpec want={0},have;
  want.freq=44100; want.format=AUDIO_F32SYS; want.channels=2; want.samples=512; want.callback=audio_cb;
  adev=SDL_OpenAudioDevice(0,0,&want,&have,0);
  if(adev){ audioOK=1; SDL_PauseAudioDevice(adev,0); printf("[dilation] audio up\n"); }
  else printf("[dilation] no audio device, running silent\n");

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glClearColor(0.004f,0.012f,0.008f,1);

  int running=1, frame=0, wdown=0,adown=0,sdown=0,ddown=0;
  int smokeBad=0;                                  /* sticky gate failure flag */
  /* CPU frame-time budget. Keep the samples: the MEAN is useless here because a
   * single descheduled frame (this runs on a shared desktop) can be 400ms and
   * drags the average by 2ms. The median and p95 describe the frame we actually
   * ship; max is kept only as a hitch indicator. */
  #define FMSCAP 512
  static float fms_[FMSCAP]; int fmsN=0;
  float fmsMin=1e9f,fmsMax=0;
  /* sub-ms clock: SDL_GetTicks is millisecond-grained, and two frames inside
   * the same millisecond gave dt==0 — which turned pvx into 0/0 NaN and
   * poisoned bobT (and with it the camera) until the next reset. */
  Uint64 lastPC=SDL_GetPerformanceCounter();
  double pcHz=(double)SDL_GetPerformanceFrequency();
  float titleYaw=0;
  gstate=ST_TITLE;

  while(running){
    SDL_Event ev;
    while(SDL_PollEvent(&ev)){
      if(ev.type==SDL_QUIT)running=0;
      /* The drawable changed: tear down and rebuild the whole post chain at the
       * new size. Cheap enough to do synchronously on the event (the shader
       * programs are kept, only the targets are reallocated), and SDL coalesces
       * drag-resizes so this fires far less often than it looks. */
      /* A held mouse button that goes away while we are not focused never sends
       * its BUTTONUP, so alt-tabbing mid-aim used to latch ADS on permanently. */
      else if(ev.type==SDL_WINDOWEVENT &&
              (ev.window.event==SDL_WINDOWEVENT_FOCUS_LOST||
               ev.window.event==SDL_WINDOWEVENT_LEAVE)){
        adsHold=0;
      }
      else if(ev.type==SDL_WINDOWEVENT &&
              (ev.window.event==SDL_WINDOWEVENT_SIZE_CHANGED||
               ev.window.event==SDL_WINDOWEVENT_RESIZED)){
        if(!smoke&&!titlecap){
          int nw,nh; SDL_GL_GetDrawableSize(win,&nw,&nh);
          SDL_GetWindowSize(win,&winW,&winH);
          if(nw>0&&nh>0&&(nw!=fbW||nh!=fbH)){
            fbW=nw; fbH=nh;
            free_post(); init_post(msaa);
          }
        }
      }
      else if(ev.type==SDL_KEYDOWN||ev.type==SDL_KEYUP){
        int d=ev.type==SDL_KEYDOWN;
        /* one-shot actions must ignore OS auto-repeat: a held SPACE was
         * re-entering the jump branch ~30x/s and silently spending the double
         * jump the moment the feet left the ground. WASD latches keep `d`. */
        int once=d&&!ev.key.repeat;
        switch(ev.key.keysym.sym){
          case SDLK_w:wdown=d;break; case SDLK_a:adown=d;break;
          case SDLK_s:sdown=d;break; case SDLK_d:ddown=d;break;
          case SDLK_SPACE:
            if(once&&gstate==ST_PLAY&&rollT<=0){
              float gh=ground_h(px,pz,py), wnx,wnz;
              if(py<=gh+0.001f||coyT>0){ /* coyote: late edge jumps count */
                pvy=7.5f; jumps=1; actT=0.20f; coyT=0; sfx(V_JUMP);
              }
              else if(wall_kick(px,pz,py,&wnx,&wnz)){
                /* kick off the wall: up and away, air jump restored */
                pvy=7.4f; kvx=wnx*6.0f; kvz=wnz*6.0f; jumps=1; actT=0.20f;
                spawn_parts(8,px-wnx*0.4f,py+0.8f,pz-wnz*0.4f,2.5f, 0.30f,1.2f,0.6f);
                sfx(V_KICK);
              }
              else if(jumps>0){ jumps--; pvy=6.6f; actT=0.20f;
                spawn_parts(10,px,py+0.1f,pz,2.2f, 0.25f,1.1f,0.55f);
                sfxp(V_JUMP,1.3f);
              }
            } break;
          case SDLK_LCTRL: case SDLK_RCTRL: case SDLK_c:
          case SDLK_LSHIFT: case SDLK_RSHIFT:
            if(once&&gstate==ST_PLAY&&rollCD<=0&&rollT<=0
               &&py<=ground_h(px,pz,py)+0.05f){
              /* dodge roll along current input, or straight ahead */
              float mx,mz;
              wasd_dir(wdown,sdown,adown,ddown,&mx,&mz);
              float ml2=sqrtf(mx*mx+mz*mz);
              if(ml2<0.01f){ float yr2=pyaw*PI/180;
                mx=sinf(yr2); mz=-cosf(yr2); ml2=1; }
              rollDX=mx/ml2; rollDZ=mz/ml2;
              rollT=ROLL_TIME; rollCD=ROLL_CD; actT=ROLL_TIME;
              sfx(V_ROLL);
            } break;
          case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
            if(once&&gstate==ST_TITLE){ curlevel=ev.key.keysym.sym-SDLK_1;
              if(curlevel>=NLEVEL)curlevel=NLEVEL-1;
              preview_level(); sfx(V_CLICK); }
            break;
          case SDLK_f: if(once&&gstate==ST_PLAY)katana(); break;
          /* Q cycles the quality tier by hand and pins it — once you have made a
           * choice the adaptive logic must stop second-guessing you. */
          case SDLK_q: if(once){
              qual=(qual+1)%3; qualAuto=0;
              free_post(); init_post(4);
              printf("[dilation] quality: %s\n",QUAL[qual].name);
            } break;
          /* R rerolls the sector. The layout is fully seeded now, so this is a
           * genuinely different building rather than the same one relit. */
          case SDLK_r: if(once&&gstate==ST_TITLE){
              reroll=reroll*1664525u+1013904223u;
              preview_level(); sfx(V_CLICK); } break;
          case SDLK_m: if(once){ g_mute=!g_mute; printf("[dilation] audio %s\n",g_mute?"muted":"unmuted"); } break;
          /* F11, or the platform-conventional Alt+Enter. FULLSCREEN_DESKTOP
           * rather than a mode switch: no resolution change, no black flash, and
           * the SIZE_CHANGED handler above rebuilds the post chain either way. */
          case SDLK_F11: case SDLK_RETURN:
            if(once && (ev.key.keysym.sym==SDLK_F11 || (ev.key.keysym.mod&KMOD_ALT))){
              Uint32 f=SDL_GetWindowFlags(win);
              SDL_SetWindowFullscreen(win,
                (f&SDL_WINDOW_FULLSCREEN_DESKTOP)?0:SDL_WINDOW_FULLSCREEN_DESKTOP);
            } break;
          case SDLK_LEFT: if(once&&gstate==ST_TITLE){ curlevel=(curlevel+NLEVEL-1)%NLEVEL; preview_level(); sfx(V_CLICK); } break;
          case SDLK_RIGHT:if(once&&gstate==ST_TITLE){ curlevel=(curlevel+1)%NLEVEL; preview_level(); sfx(V_CLICK); } break;
          case SDLK_ESCAPE:
            if(once){
              if(gstate==ST_TITLE)running=0;
              else { gstate=ST_TITLE; laserTarget=-1; adsHold=0;
                     SDL_SetRelativeMouseMode(SDL_FALSE); }
            } break;
        }
      }
      else if(ev.type==SDL_MOUSEMOTION && gstate==ST_PLAY && !smoke){
        /* Zoom has to slow the mouse or ADS is unusable: the same hand movement
         * sweeps the same number of DEGREES, which through a narrower lens is a
         * much larger fraction of the screen. Scale by the tan ratio, which is
         * the exact factor by which a degree got bigger on screen. mouseAcc gets
         * the same treatment — otherwise aiming while zoomed would unfreeze time
         * harder than aiming from the hip, for no reason the player can see. */
        float sens=0.13f*(tanf(FOV_ADS*PI/180)/tanf(FOV_HIP*PI/180)-1.0f)*ads_amt()+0.13f;
        pyaw  += ev.motion.xrel*sens;
        ppitch+= ev.motion.yrel*sens;
        ppitch=ppitch>89?89:ppitch<-89?-89:ppitch;
        mouseAcc+=(fabsf((float)ev.motion.xrel)+fabsf((float)ev.motion.yrel))*(sens/0.13f);
      }
      else if(ev.type==SDL_MOUSEBUTTONDOWN && ev.button.button==SDL_BUTTON_LEFT){
        if(gstate==ST_TITLE){ reset_game(); gstate=ST_PLAY; SDL_SetRelativeMouseMode(SDL_TRUE); sfx(V_CLICK); }
        else if(gstate==ST_DEAD||gstate==ST_WIN){
          /* clearing a sector jacks you into the next one — the campaign runs
           * end to end instead of dumping you back into the same fight. The
           * last sector loops so OVERLORD stays replayable. */
          if(gstate==ST_WIN && curlevel+1<NLEVEL) curlevel++;
          reset_game(); gstate=ST_PLAY; SDL_SetRelativeMouseMode(SDL_TRUE); }
        else fire();
      }
      /* RMB is ADS now; the katana moved to F. RMB-to-aim is the binding every
       * player already has in their hands, and the katana is a deliberate,
       * occasional commitment that reads better as its own key. */
      else if(ev.type==SDL_MOUSEBUTTONDOWN && ev.button.button==SDL_BUTTON_RIGHT)
        adsHold=1;
      else if(ev.type==SDL_MOUSEBUTTONUP && ev.button.button==SDL_BUTTON_RIGHT)
        adsHold=0;
    }

    Uint64 nowPC=SDL_GetPerformanceCounter();
    float dt=(smoke||titlecap)?1.0f/60:(float)((nowPC-lastPC)/pcHz);
    lastPC=nowPC;
    if(dt>0.05f)dt=0.05f;
    if(dt<1e-5f)dt=1e-5f;
    gtime+=dt;
    /* CPU cost of building this frame, measured up to SwapWindow (which blocks
     * on vsync and would swamp the signal). Reported by the smoke gate so a perf
     * regression shows up as a number instead of as a vibe. */
    Uint64 fStart=SDL_GetPerformanceCounter();

    /* title capture: hold the title screen and dump a numbered frame sequence
     * (every 3rd internal frame -> 20fps source) for the README GIF. A short
     * warmup skips the undefined first back-buffer. */
    if(titlecap){
      gstate=ST_TITLE;
      if(frame>=6 && frame%3==0){
        static char nm[64]; snprintf(nm,sizeof nm,"titlecap_%04d.ppm",(frame-6)/3);
        pendingShot=nm;
      }
      frame++;
      if(frame>=366){ printf("[dilation] TITLECAP wrote %d frames\n",(frame-6)/3); running=0; }
    }

    /* smoke choreography: gen-check done above; now title shot, jack in,
     * stage an agent, trade fire, shatter it, screenshot the lot. */
    if(smoke){
      frame++;
      if(frame==25)pendingShot="shot_title.ppm";
      if(frame==30){
        reset_game(); gstate=ST_PLAY;
        float best=0,besta=0;
        for(int k=0;k<64;k++){
          float a=k*2*PI/64;
          float d=ray_wall(px,EYE,pz,sinf(a),0,-cosf(a),40);
          if(d>best){best=d;besta=a;}
        }
        pyaw=besta*180/PI; ppitch=2;
      }
      /* Push the avatar through a short deterministic run so the harness
       * captures the third-person locomotion pose, not just idle/combat. */
      wdown = frame>=32 && frame<64;
      adown = sdown = ddown = 0;
      if(frame==40&&nen>0&&en[0].type!=2){ /* stage a shooter dead ahead, mid-aim */
        float yr=pyaw*PI/180;
        for(float d=5.5f;d>2.0f;d-=0.3f){
          float ex=px+sinf(yr)*d, ez=pz-cosf(yr)*d;
          if(circ_free(ex,ez,0.4f,py)&&los(px,py+EYE,pz,ex,1.4f,ez)){
            en[0].x=en[0].lx=ex; en[0].z=en[0].lz=ez;
            en[0].y=ground_h(ex,ez,py);
            en[0].type=0; en[0].state=1; en[0].state_t=0.3f; break; }
        }
      }
      if(frame==58)pendingShot="shot_run_pose.ppm";
      if(frame>=70&&frame<80&&nen>0){ /* track it for the camera */
        float dx=en[0].x-px, dz=en[0].z-pz;
        float d=sqrtf(dx*dx+dz*dz);
        pyaw=atan2f(dx,-dz)*180/PI;
        ppitch=atan2f(EYE-1.3f,d)*180/PI;
      }
      if(frame==76)fire();
      if(frame==82)pendingShot="shot_gun_pose.ppm";
      if(frame==84)pendingShot="shot_game.ppm";
      if(frame==100&&nen>0)shatter_enemy(&en[0]);
      /* mid-collapse: dieT is 0.13 world-seconds and the shatter lands at 100,
       * so 103 catches the body ~40% squashed with the shards already leaving.
       * Without this the gate had no coverage of the death animation at all. */
      if(frame==103)pendingShot="shot_death.ppm";
      if(frame==106)pendingShot="shot_shatter.ppm";
      if(frame==112)katana();
      if(frame==118)pendingShot="shot_katana_pose.ppm";

      /* Extended choreography: the pistol's charge gauge, a charged lock-on,
       * and a dodge roll. All staged strictly AFTER the six baseline shots
       * (frame<=118) so their frand() order and bytes stay untouched and the
       * regression gate against baseline/ remains byte-identical. */
      static float smkYaw=0;
      if(frame==122){                 /* fresh stage facing the longest open corridor */
        reset_game(); gstate=ST_PLAY; nen=nalive=0;
        float best=0,besta=0;
        for(int k=0;k<64;k++){ float a=k*2*PI/64;
          float d=ray_wall(px,EYE,pz,sinf(a),0,-cosf(a),40);
          if(d>best){best=d;besta=a;} }
        smkYaw=besta*180/PI;
      }
      /* CHARGE: a half-filled green beam grows down the empty corridor — the
       * range gauge filling from motion, no agent in reach yet. */
      if(frame>=122&&frame<130){ pyaw=smkYaw; ppitch=3; gunCharge=0.60f; }
      if(frame==129)pendingShot="shot_charge.ppm";
      /* LOCK-ON: stage an agent in that corridor and fully charge — the beam
       * reaches it and both the beam and the agent flare red (locked). */
      if(frame==130){
        float yr=smkYaw*PI/180;
        float ex=px+sinf(yr)*7.0f, ez=pz-cosf(yr)*7.0f;
        nen=nalive=1; memset(&en[0],0,sizeof en[0]);
        en[0].type=0; en[0].hue=0.30f;
        en[0].x=en[0].lx=ex; en[0].z=en[0].lz=ez; en[0].y=ground_h(ex,ez,py);
      }
      if(frame>=130&&frame<138){      /* hold the agent mid-aim and the lock steady */
        float dx=en[0].x-px, dz=en[0].z-pz, d=sqrtf(dx*dx+dz*dz);
        float muzY=py+1.52f, aimY=en[0].y+1.30f;
        pyaw=atan2f(dx,-dz)*180/PI;
        ppitch=atan2f(muzY-aimY,d)*180/PI;
        en[0].state=1; en[0].state_t=0; en[0].flash=0;
        gunCharge=1.0f;
      }
      if(frame==137)pendingShot="shot_lockon.ppm";
      /* DODGE ROLL: the agent's round streaks down the old sightline; the
       * player rolls sideways so the bullet passes through the vacated spot. */
      if(frame==138){
        float yr=pyaw*PI/180;
        /* 0.21 = half of the 0.42s roll: the shot lands on the tucked ball.
         * The old 0.5 sat PAST the roll's own duration, so draw_player clamped
         * the progress to zero and the regression shot only ever showed the
         * frame-one pose — the one place a broken tuck could hide. */
        rollT=ROLL_TIME*0.5f; rollCD=0.6f;
        rollDX=cosf(yr); rollDZ=sinf(yr);             /* strafe-right dodge      */
        float dx=px-en[0].x, dz=pz-en[0].z, dd=sqrtf(dx*dx+dz*dz)+1e-6f;
        float bdx=dx/dd, bdz=dz/dd;                    /* agent -> player          */
        float my=en[0].y+1.36f, ty=py+1.28f;
        float bx=px-bdx*0.8f, bz=pz-bdz*0.8f;          /* timed to arrive beside   */
        spawn_bullet(bx,my,bz,bdx,ty-my,bdz,8.0f,0,-1);
      }
      if(frame>=138&&frame<146)ppitch=4;
      if(frame==144)pendingShot="shot_dodge.ppm";

      /* AGENT AIM: the gate had no shot of a raised agent arm anywhere, which is
       * exactly how draw_agent kept a 180-degree-flipped gun arm through several
       * animation passes — every captured agent had its arms hanging at its
       * sides. Stage one facing us with the aim pose held at full raise. */
      if(frame==150){
        reset_game(); gstate=ST_PLAY;
        float yr=smkYaw*PI/180;
        float ex=px+sinf(yr)*6.0f, ez=pz-cosf(yr)*6.0f;
        nen=nalive=1; memset(&en[0],0,sizeof en[0]);
        en[0].type=0; en[0].x=en[0].lx=ex; en[0].z=en[0].lz=ez;
        en[0].y=ground_h(ex,ez,py);
        pyaw=smkYaw; ppitch=2;
      }
      if(frame>=150&&frame<160){       /* pin the aim pose: armp/flare fully up */
        float dx=en[0].x-px, dz=en[0].z-pz;
        en[0].yaw=atan2f(-dx,dz);       /* facing back down the sightline at us */
        en[0].state=1; en[0].state_t=0.30f; en[0].armp=1.0f; en[0].flare=1.0f;
        pyaw=atan2f(dx,-dz)*180/PI; ppitch=2;
      }
      if(frame==158)pendingShot="shot_agent_aim.ppm";

      /* BOSS: never captured at all before, so its pose, its scale and its own
       * flipped arms were entirely untested. */
      if(frame==162){
        curlevel=NLEVEL-1; reset_game(); gstate=ST_PLAY;
        if(bossIdx>=0){
          /* Stand off the throne at 22.5 degrees: the eight cover pillars sit on
           * exact 45-degree spokes, and the default spawn looks straight down one
           * of them — the first version of this shot photographed a pillar with
           * the boss entirely behind it. */
          float a=202.5f*PI/180.0f, R=11.0f;
          px=en[bossIdx].x+sinf(a)*R; pz=en[bossIdx].z-cosf(a)*R;
          free_spot(&px,&pz); py=ground_h(px,pz,0);
          float dx=en[bossIdx].x-px, dz=en[bossIdx].z-pz;
          pyaw=atan2f(dx,-dz)*180/PI; ppitch=-6;
        }
      }
      if(frame>=162&&frame<172&&bossIdx>=0){
        en[bossIdx].state=3; en[bossIdx].melT=0.20f;   /* melee swat telegraph */
        en[bossIdx].armp=0; en[bossIdx].roar=1.0f;
      }
      if(frame==170)pendingShot="shot_boss.ppm";

      /* WIN CARD: drive the sector to zero agents so the victory screen is
       * actually exercised. It never was, which is exactly how it shipped
       * reading "0 SECONDS REAL" on every single clear. */
      if(frame==174){ for(int i=0;i<nen;i++) if(en[i].state!=4) shatter_enemy(&en[i]); }
      if(frame==178){
        if(gstate!=ST_WIN){ printf("[dilation] SMOKE FAIL: clearing all agents did not win\n"); smokeBad=1; }
        else if(winRealT<=0.0f){ printf("[dilation] SMOKE FAIL: win card reports %.2f seconds real\n",winRealT); smokeBad=1; }
        pendingShot="shot_win.ppm";
      }

      /* ADS: mid-transition and fully shouldered, with an agent to aim at. The
       * mid shot is the one that matters — it is where the body cull, the boom
       * pull-in and the FOV lerp can disagree with each other. */
      if(frame==184){
        curlevel=0; reset_game(); gstate=ST_PLAY;
        float best=0,besta=0;
        for(int k=0;k<64;k++){ float a=k*2*PI/64;
          float d=ray_wall(px,EYE,pz,sinf(a),0,-cosf(a),40);
          if(d>best){best=d;besta=a;} }
        pyaw=besta*180/PI; ppitch=1;
        float yr=pyaw*PI/180;
        float ex=px+sinf(yr)*7.5f, ez=pz-cosf(yr)*7.5f;
        nen=nalive=1; memset(&en[0],0,sizeof en[0]);
        en[0].type=0; en[0].x=en[0].lx=ex; en[0].z=en[0].lz=ez;
        en[0].y=ground_h(ex,ez,py); en[0].yaw=atan2f(-sinf(yr),cosf(yr));
        adsHold=1; ads=0;
      }
      if(frame>=184){ adsHold=1; en[0].state=1; en[0].armp=1.0f; en[0].flare=1.0f; }
      if(frame==189)pendingShot="shot_ads_mid.ppm";   /* mid-dissolve */
      if(frame==200){
        /* toward() is exponential, so it asymptotes: 16 frames at rate 11 gets
         * to 1-(1-11/60)^16 = 0.96. The assertion is "the transition actually
         * completes", not "it reaches 1.0 in finite time". */
        if(ads<0.95f){ printf("[dilation] SMOKE FAIL: ads only reached %.3f\n",ads); smokeBad=1; }
        pendingShot="shot_ads.ppm";
      }

      if(!nan_check(frame))smokeBad=1;
      if(frame>=206){
        printf("[dilation] pool peaks: %d/%d bullets, %d/%d particles, %d/%d shards\n",
               poolPeakBul,MAXBUL,poolPeakPart,MAXPART,poolPeakShard,MAXSHARD);
        if(poolPeakBul>=MAXBUL||poolPeakPart>=MAXPART||poolPeakShard>=MAXSHARD){
          printf("[dilation] SMOKE FAIL: a fixed pool hit its cap\n"); smokeBad=1; }
        printf("[dilation] frustum cull: %d of %d figure tests skipped (%.0f%%)\n",
               cullSkipped,cullTested,cullTested?100.0*cullSkipped/cullTested:0.0);
        { /* insertion sort: n is at most FMSCAP and this runs once, at exit */
          for(int a=1;a<fmsN;a++){ float v=fms_[a]; int b=a-1;
            while(b>=0&&fms_[b]>v){ fms_[b+1]=fms_[b]; b--; } fms_[b+1]=v; }
          float med=fmsN?fms_[fmsN/2]:0, p95=fmsN?fms_[(int)(fmsN*0.95f)]:0;
          printf("[dilation] frame cpu ms: min %.2f median %.2f p95 %.2f max %.2f over %d frames\n",
                 fmsMin,med,p95,fmsMax,fmsN);
          if(med>8.0f){
            if(swRender) printf("[dilation] frame budget not enforced on a software rasterizer\n");
            else { printf("[dilation] SMOKE FAIL: median frame %.2fms exceeds budget\n",med); smokeBad=1; } } }
        if(strictShots)
          printf(strictFails? "[dilation] STRICT: %d shots differ from baseline/\n"
                            : "[dilation] STRICT: all shots identical\n", strictFails);
        if(strictShots&&strictFails)smokeBad=1;
        printf(smokeBad? "[dilation] SMOKE FAIL\n" : "[dilation] SMOKE OK\n");
        running=0;
      }
    }

    if(gstate==ST_TITLE){ titleYaw+=dt*7; pyaw=titleYaw; ppitch=4;
      px=startx; pz=startz; tscale=tsEff=1; wtime=gtime; }

    pose_dirty();   /* sim phase: the pose is re-solved from current state */
    /* ADS on raw dt — shouldering a weapon is player agency and must not slow
     * down when the world does. The MOTION of the transition counts as action
     * (so raising the gun nudges time forward the way firing does), but holding
     * it up does NOT: standing still while aimed has to stay frozen, because
     * that is the entire game. Hence the rate, not the level, feeds actT.
     *
     * OUTSIDE the ST_PLAY block on purpose. This used to live inside it, so
     * dying or winning while zoomed froze `ads` at whatever it held — and the
     * camera, which reads it unconditionally, stayed jammed inside the head of a
     * body that ST_DEAD does not even draw. The target already accounts for
     * gstate, so running it every frame is what makes the camera ease home. */
    { float prevAds=ads;
      int bladeOut = swingT>0||swingCD>0;
      ads = toward(ads, (gstate==ST_PLAY&&adsHold&&!bladeOut)?1.0f:0.0f, dt*11.0f);
      float rate=fabsf(ads-prevAds)/(dt>1e-6f?dt:1e-6f);
      if(rate>0.9f && actT<0.06f) actT=0.06f; }
    /* Screen-effect decays, out here for the same reason `ads` is. These three
     * used to live inside the ST_PLAY block while their consumers did not, and
     * both terminal states SET them on the way out: hurt_player assigns
     * shake=0.3/dmgFlash=0.6 and then gstate=ST_DEAD in the next statement, and
     * shatter_enemy assigns shake=0.9 for a boss kill immediately before the
     * nalive<=0 branch flips to ST_WIN. So the frame after you died or won, the
     * decay stopped running and the camera kept being fed +-1 to +-3 degrees of
     * shake, with the damage aberration pinned, until you clicked. */
    if(dmgFlash>0)dmgFlash-=dt;
    if(mzT>0)mzT-=dt;
    if(shake>0)shake-=dt*1.2f;
    /* THE mechanic: world time follows your motion. walking, looking and
     * acting each push the target timescale toward 1; stillness lets it
     * sink to the MINTS creep. the player always moves in real time. */
    float wdt=dt;
    if(gstate==ST_PLAY){
      float mx,mz;
      wasd_dir(wdown,sdown,adown,ddown,&mx,&mz);
      float ml=sqrtf(mx*mx+mz*mz);
      float ox=px,oz=pz;
      if(rollT>0){                 /* the roll owns the legs while it runs */
        rollT-=dt;
        if(rollT<0)rollT=0;
        move_circ(&px,&pz,rollDX*8.5f*dt,rollDZ*8.5f*dt,0.34f,py);
        /* dust on a TIMER, not per frame: spawning two puffs every frame made the
         * trail three times denser at 180fps than at 60, and drew from the sim
         * RNG at a framerate-dependent rate. 0.02s -> the old 60fps density. */
        for(rollPT+=dt; rollPT>0.02f; rollPT-=0.02f)
          spawn_parts(2,px-rollDX*0.4f,py+0.45f,pz-rollDZ*0.4f,0.6f, 0.10f,0.50f,0.25f);
      } else if(ml>0.01f){
        mx/=ml;mz/=ml;
        move_circ(&px,&pz,mx*5.0f*dt,mz*5.0f*dt,0.34f,py);
        /* footsteps on the stride's actual foot-plants (phase 0 and PI are the
         * two stance strikes), not the old fixed 0.40s timer that drifted
         * against the visible gait. stepT holds the previous half-cycle phase;
         * a wrap means a foot just planted. */
        float sph=fmodf(bobT*7.5f,PI);
        if(sph<stepT && py<=ground_h(px,pz,py)+0.01f) sfx(V_STEP);
        stepT=sph;
      }
      if(kvx*kvx+kvz*kvz>0.01f){   /* wall-kick momentum, easing off */
        move_circ(&px,&pz,kvx*dt,kvz*dt,0.34f,py);
        kvx-=kvx*clampf(dt*4.0f,0,1); kvz-=kvz*clampf(dt*4.0f,0,1);
      }
      if(rollCD>0)rollCD-=dt;
      pvx=(px-ox)/dt; pvz=(pz-oz)/dt;
      /* see the agents' e->spdS: same problem, same cure */
      { float sp=sqrtf(pvx*pvx+pvz*pvz);
        pspdS = sp>pspdS ? sp : toward(pspdS,sp,dt*7.0f); }
      pmoveb=toward(pmoveb,(ml>0.01f||rollT>0)?1.0f:0.0f,dt*9.0f);
      /* travel-locked cadence: stride phase advances with actual ground speed,
       * so the planted foot tracks the floor instead of skating. A faint idle
       * creep keeps the pose from freezing mid-step. (phase = bobT*7.5 in draw) */
      { float sp=sqrtf(pvx*pvx+pvz*pvz);
        bobT+=dt*(0.12f+0.62f*pspdS); (void)sp; }
      /* the avatar's facing eases toward the roll direction and back —
       * no yaw snap entering or leaving a sideways roll */
      avYaw=angto(avYaw, rollT>0? atan2f(rollDX,-rollDZ) : pyaw*PI/180.0f,
                  dt*(rollT>0?20.0f:14.0f));    /* near-180° rolls converge inside the roll */

      pvy-=18.0f*dt;
      py += pvy*dt;
      float gh=ground_h(px,pz,py);
      if(py<gh){
        if(pvy<-7.0f){           /* a real landing: thud, dust, camera dip */
          sfxp(V_LAND,clampf(-pvy/14.0f,0.5f,1.2f));
          spawn_parts(9,px,gh+0.05f,pz,1.6f, 0.18f,0.55f,0.30f);
          if(shake<0.06f)shake=0.06f;
        }
        /* knee absorb: any landing worth hearing also gets a visible dip, so
         * the avatar stops arriving on the floor rigid. Scaled by impact and
         * decayed on raw dt — this is pose feedback, not simulation. */
        /* landT is the knee absorb. It was ASSIGNED on the touchdown frame and
         * then decayed smoothly — so the hips dropped 26cm, the torso pitched 11
         * degrees and the free arm threw 24 degrees all in a single frame, and
         * only the recovery was animated. Physically it is backwards: a knee
         * absorb has a fast but finite attack and a slower release. landTgt is
         * the target the compression now runs TO over ~60ms. */
        { float imp=clampf(-pvy/15.0f,0,1);
          if(imp>landTgt)landTgt=imp; }
        py=gh; pvy=0; jumps=1;
      }
      int air = py>gh+0.01f;
      coyT = air? coyT-dt : 0.12f;
      airB = toward(airB, air?1.0f:0.0f, dt*7.0f);   /* pose blend, raw dt */

      /* mouseAcc is pixels-per-FRAME: normalize by dt or the same physical turn
       * unfreezes time twice as hard at 30fps as at 60. */
      float look=clampf(mouseAcc*0.05f/(dt*60.0f),0,1); mouseAcc=0;
      /* gun settle: the aim is PHYSICAL. Running, rolling, flying and hard
       * turns swing the gun with the body; stop, and the barrel SNAPS back
       * onto the look ray — an eased 0.22s, so the stop-time-aim-fire rhythm
       * never waits on the gun. Runs on raw dt: this is the player's own
       * body, which never dilates with the frozen world. */
      { int stable = !air && rollT<=0
                   && sqrtf(pvx*pvx+pvz*pvz)<0.8f && look<0.35f;
        stableT = stable? stableT+dt : 0;
        float want=sstep(clampf(stableT/GUN_SETTLE_TIME,0,1));
        aimSet = want>aimSet ? want : toward(aimSet,want,dt*8.0f); }
      if(actT>0)actT-=dt;
      float target=(ml>0.01f||rollT>0||air)?1.0f:0.0f;
      if(look>target)target=look;
      if(actT>0)target=1.0f;
      target=MINTS+(1.0f-MINTS)*target;
      tscale+=(target-tscale)*clampf(dt*14.0f,0,1);
      /* impact freeze: a kill or a deflect pins the world for ~50ms. In a game
       * whose whole language is timescale, the punch belongs in tscale rather
       * than in a camera kick — the shards hang in the air for a beat and the
       * SFX pitch-bend follows it down for free. Runs on raw dt: this is feel,
       * not simulation, and it must not scale with the thing it is freezing.
       * The dip is applied to a DERIVED value: multiplying the stored tscale
       * compounded per frame, freezing ~10x deeper at 300fps than at 30. */
      tsEff=tscale;
      if(hitstop>0){ hitstop-=dt; if(hitstop<0)hitstop=0; tsEff*=0.16f; }
      if(smoke)tscale=tsEff=1;    /* determinism for the harness */
      wdt=dt*tsEff;
      wtime+=wdt;

      if(fireCD>0)fireCD-=dt;
      if(pammo>0){
        /* Recharge from actual movement, not raw input/roll state. This avoids
         * hidden roll-time charge jumps and makes blocked movement charge less. */
        float groundDrive=clampf(sqrtf(pvx*pvx+pvz*pvz)/5.0f,0,1);
        if(rollCD>0.20f && groundDrive>0.55f)groundDrive=0.55f; /* dodge is evasive, not a free reload */
        float chargeDrive = groundDrive>0.02f ? groundDrive : GUN_IDLE_CHARGE;
        if(air && chargeDrive<0.45f)chargeDrive=0.45f;
        if(look>chargeDrive)chargeDrive=look;
        gunCharge=clampf(gunCharge+dt*chargeDrive/GUN_CHARGE_TIME,0,1);
      } else gunCharge=0;
      laserTarget=laser_target();
      if(swingCD>0){ swingCD-=dt;
        if(swingCD<=0)swStow=1.0f;      /* katana leaves the hand: ease back */
      }
      if(swingT>0){
        float pt=swingT; swingT+=dt;
        if(pt<0.10f&&swingT>=0.10f)katana_strike();  /* one strike per swing */
        if(swingT>SWING_TIME){ swingT=0; swRel=1.0f; } /* cut ends: ease down */
      }
      if(swRel>0){ swRel-=dt*4.0f; if(swRel<0)swRel=0; }
      if(swStow>0){ swStow-=dt*3.0f; if(swStow<0)swStow=0; }
      /* attack toward the target fast, then release with it as it decays */
      if(landTgt>0){ landTgt-=dt*3.6f; if(landTgt<0)landTgt=0; }
      landT = landT<landTgt ? toward(landT,landTgt,dt*17.0f) : landTgt;
      if(hurtCD>0)hurtCD-=dt;
      if(msgT>0)msgT-=dt;
      winT+=dt;

      update_enemies(wdt);
      update_bullets(wdt);

      for(int i=0;i<nitems;i++){
        if(items[i].taken){
          /* timed caches re-arm on world-time, so the player must keep
             platforming up to them through the fight */
          if(items[i].respawn>0){ items[i].rtimer-=wdt; if(items[i].rtimer<=0)items[i].taken=0; }
          continue;
        }
        float dx=items[i].x-px,dz=items[i].z-pz;
        if(dx*dx+dz*dz<0.8f*0.8f && fabsf(floor_at(items[i].x,items[i].z)-py)<1.4f){
          items[i].taken=1; items[i].rtimer=items[i].respawn; sfx(V_PICK);
          if(items[i].type==0){ php+=35; if(php>100)php=100; }
          else { pammo+=items[i].amt; if(pammo>PLAYER_MAX_AMMO)pammo=PLAYER_MAX_AMMO;
                 gunCharge=clampf(gunCharge+items[i].amt/24.0f,0,1); }
        }
      }
    }
    if(gstate==ST_DEAD){ wdt=dt*MINTS; wtime+=wdt; update_bullets(wdt); }
    if(gstate==ST_WIN){ wdt=dt*0.25f; wtime+=wdt; update_bullets(wdt); }  /* victory slow-mo */
    g_ats = gstate==ST_PLAY ? tsEff : (gstate==ST_TITLE?1.0f:0.3f);
    g_track = (gstate==ST_TITLE) ? 0 : curlevel+1;   /* MENU vs per-level track */

    for(int i=0;i<MAXPART;i++){
      Part*p=&parts[i]; if(p->life<=0)continue;
      p->life-=wdt; p->vy-=6.0f*wdt;
      p->x+=p->vx*wdt; p->y+=p->vy*wdt; p->z+=p->vz*wdt;
      float pg=floor_at(p->x,p->z);
      if(p->y<pg+0.02f){p->y=pg+0.02f;p->vy*=-0.3f;p->vx*=0.7f;p->vz*=0.7f;}
    }
    /* the death collapse runs on world time, so a kill at MINTS hangs exactly
     * as long as the shards it threw do */
    for(int i=0;i<nen;i++) if(en[i].dieT>0){
      en[i].dieT-=wdt; if(en[i].dieT<0)en[i].dieT=0; }
    for(int i=0;i<MAXSHARD;i++){
      Shard*s=&shards[i]; if(s->life<=0)continue;
      s->life-=wdt; s->vy-=7.0f*wdt;
      s->x+=s->vx*wdt; s->y+=s->vy*wdt; s->z+=s->vz*wdt;
      s->yaw+=s->wy*wdt; s->pit+=s->wp*wdt;
      float sg=floor_at(s->x,s->z);
      if(s->y<sg+s->sy*0.5f){ s->y=sg+s->sy*0.5f; s->vy*=-0.35f; s->vx*=0.6f; s->vz*=0.6f;
        s->wy*=0.5f; s->wp*=0.5f; }
    }
    for(int i=0;i<MAXTEMPL;i++) if(templ_[i].life>0)templ_[i].life-=wdt;

    /* render */
    pose_dirty();   /* render phase: solve once, share across every consumer */
    post_begin();
    { const LevelDef*LV=&LEVELS[curlevel];
      glClearColor(LV->fog[0],LV->fog[1],LV->fog[2],1); }
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    float adsE=ads_amt();
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    /* FOV lerped in TAN space, not in degrees: the tangent is what actually
     * scales on screen, so interpolating the angle makes the zoom lurch through
     * the middle of the transition. The near plane comes in as we zoom so the
     * carried pistol, which ends up about 40cm from the eye, does not clip. */
    { float tanF=tanf(FOV_HIP*PI/180)+(tanf(FOV_ADS*PI/180)-tanf(FOV_HIP*PI/180))*adsE;
      float zn=0.08f-0.045f*adsE, t=zn*tanF, a=(float)fbW/fbH;
      glFrustum(-t*a,t*a,-t,t,zn,80.0f); }
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    /* smoothed shake: per-frame white noise read as flicker, not impact.
     * The offset chases a random target, so it jolts and settles instead. */
    static float shxS=0,shyS=0;
    shxS=toward(shxS, shake>0?(vrand()-0.5f)*shake*7.0f:0.0f, dt*30.0f);
    shyS=toward(shyS, shake>0?(vrand()-0.5f)*shake*7.0f:0.0f, dt*30.0f);
    /* Shake is applied as raw DEGREES of view rotation. From behind the shoulder
     * that reads as a camera jolt; from inside the head the same angle is a
     * whiplash, and through a narrower lens it is worse again. Damp it as we
     * zoom, by the same tan ratio the mouse uses. */
    float shakeDamp=1.0f+(tanf(FOV_ADS*PI/180)/tanf(FOV_HIP*PI/180)*0.55f-1.0f)*adsE;
    float shx=shxS*shakeDamp, shy=shyS*shakeDamp;
    float yr=pyaw*PI/180.0f;
    float fx=sinf(yr), fz=-cosf(yr), rx=cosf(yr), rz=sinf(yr);
    /* third-person boom with occlusion: cast at the desired offset, snap IN
     * the moment something blocks (never clip), ease back OUT smoothly.
     * Vertical follows a smoothed pivot so rolls and landings don't pop. */
    float pivy=(gstate==ST_PLAY?player_camh():EYE)+0.10f;
    if(camYs<0)camYs=pivy;
    camYs=toward(camYs,pivy,dt*10.0f);
    float ox2=-fx*2.90f+rx*1.08f, oz2=-fz*2.90f+rz*1.08f;
    float boom=sqrtf(ox2*ox2+oz2*oz2);
    float ux=ox2/boom, uz=oz2/boom;
    float hit=ray_wall(px,camYs,pz,ux,0,uz,boom+0.30f);
    float maxd=hit-0.28f; if(maxd<0.40f)maxd=0.40f;
    float tgt=boom<maxd?boom:maxd;
    /* Occlusion snapped IN in a single frame. Behind the shoulder that is the
     * right call (never clip through a wall); from inside the head it is a
     * teleport. Ease both directions, fast inward so cover still works. */
    camDist=toward(camDist,tgt, dt*(tgt<camDist?18.0f:5.0f));
    /* ADS scales the SOLVED boom length. It must be applied here, after the
     * occlusion clamp — folding it into the offset would make the wall probe
     * shorten with the zoom and the camera would stop respecting cover. */
    float camDistA=camDist*(1.0f-adsE);
    float camx=px+ux*camDistA;
    float camz=pz+uz*camDistA;
    /* one dip per FOOTFALL (2x the per-leg rate — plants land at phase 0 and
     * PI), phase-locked with the stride and the step SFX; the old 7.0 beat
     * against the legs' 7.5 and drifted in and out of sync */
    float camy3=camYs+(gstate==ST_PLAY?sinf(bobT*15.0f)*0.016f:0.0f);
    /* The first-person eye comes from the POSE, not from EYE (1.62) — that
     * constant matches neither the third-person pivot (py+2.02) nor where the
     * avatar's eye slits are actually drawn. player_pose gives the mid-body
     * pivot; the head rides 1.29*tk above it and the slits 0.036 above that, so
     * this puts the camera exactly behind the eyes and it tracks the roll tuck
     * and the landing absorb for free. */
    float camyA=camy3;
    if(gstate==ST_PLAY){
      const PPose*EP=pose_get();
      camyA=EP->pcy+1.29f*EP->tk+0.036f;
    }
    float camy=camy3+(camyA-camy3)*adsE;
    /* Keep the eye inside the room. NB: on a double jump from a raised floor in
     * a low sector this can still lag the body, because the player has no
     * head-vs-ceiling collision — jumping your skull through the roof is legal.
     * Fixing that properly means capping py, which would nerf SUBWAY's train-roof
     * play, so it is left as a known edge. */
    camy=clampf(camy,0.25f,wallh-0.10f);
    glRotatef(ppitch+shy,1,0,0);
    glRotatef(pyaw+shx,0,1,0);
    glTranslatef(-camx,-camy,-camz);

    /* the view is final: harvest the six clip planes for this frame's culling */
    frustum_build();
    { float yrb=pyaw*PI/180.0f, prb=ppitch*PI/180.0f;   /* billboard basis */
      bbRx=cosf(yrb); bbRz=sinf(yrb);
      bbUx=sinf(yrb)*sinf(prb); bbUy=cosf(prb); bbUz=-cosf(yrb)*sinf(prb); }
    draw_world(camx,camy,camz);
    /* tonemap + bloom + grade. ts01 is the timescale remapped to 0..1 so the
     * composite can grade toward the frozen look without knowing about MINTS. */
    post_end(clampf((tsEff-MINTS)/(1.0f-MINTS),0,1), clampf(dmgFlash,0,1), gtime);
    draw_hud();

    { float fms=(float)((SDL_GetPerformanceCounter()-fStart)/pcHz)*1000.0f;
      /* skip warm-up (shader compiles, first-use uploads) and any frame that is
       * about to do a glReadPixels + PPM write — that is harness cost, and it
       * was landing on ~7% of frames, which is to say exactly on the p95. */
      if(frame>4 && !pendingShot){
        if(fms<fmsMin)fmsMin=fms; if(fms>fmsMax)fmsMax=fms;
        if(fmsN<FMSCAP)fms_[fmsN++]=fms; }
      /* Adaptive quality. We cannot ask GL2 what GPU this is in any way worth
       * trusting, so measure instead: a slow rolling average of the REAL frame
       * interval (not our CPU slice — the GPU is the wall on the hardware this
       * is for). Step down when we are missing the budget, and only step back up
       * from a comfortable margin, so a tier change can never start oscillating.
       * A manual Q press pins the tier and switches this off for good. */
      static float avgDt=0; static float holdT=0;
      if(qualAuto && !smoke && !titlecap && frame>40){
        avgDt = avgDt<=0? dt : avgDt+(dt-avgDt)*clampf(dt*1.5f,0,1);
        holdT += dt;
        if(holdT>1.5f){
          if(avgDt>0.0215f && qual>0){          /* under ~46fps: drop a tier */
            qual--; free_post(); init_post(4); holdT=0; avgDt=0;
            printf("[dilation] quality auto -> %s (%.1f fps)\n",QUAL[qual].name,1.0f/dt);
          } else if(avgDt<0.0092f && qual<2 && holdT>4.0f){ /* >108fps: room to spare */
            qual++; free_post(); init_post(4); holdT=0; avgDt=0;
            printf("[dilation] quality auto -> %s\n",QUAL[qual].name);
          }
        }
      } }
    if(pendingShot){ shot_ppm(pendingShot); pendingShot=0; }
    SDL_GL_SwapWindow(win);
  }

  if(adev)SDL_CloseAudioDevice(adev);
  SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(win); SDL_Quit();
  return 0;
}
