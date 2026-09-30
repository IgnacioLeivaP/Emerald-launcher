#include "renderer.h"
#include "gl.h"
#include "el_libretro.h"
#include "core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Passthrough renderer: uploads the libretro framebuffer as-is and draws it
   letterboxed (4:3) inside a 1280x720 window.
   Shader modes: 0=None, 1=ScaleFX-9x, 2=Scanlines, 3=CRT, 4=LCD, 5=Bloom */

/* ---- Shared vertex shader -------------------------------------------- */
static const char *s_vert =
    GLSL_VERSION
    "out vec2 v_uv;\n"
    "uniform vec4 u_rect;\n"
    "void main(){\n"
    "  vec2 pos[4]=vec2[](vec2(0,1),vec2(0,0),vec2(1,1),vec2(1,0));\n"
    "  vec2 uv[4] =vec2[](vec2(0,0),vec2(0,1),vec2(1,0),vec2(1,1));\n"
    "  vec2 p=pos[gl_VertexID];\n"
    "  gl_Position=vec4(u_rect.x+p.x*u_rect.z, u_rect.y+p.y*u_rect.w, 0, 1);\n"
    "  v_uv=uv[gl_VertexID];\n"
    "}\n";

/* ---- Identity vertex shader (no Y-flip): for multipass FBO chains ----- */
static const char *s_vert_id =
    GLSL_VERSION
    "out vec2 v_uv;\n"
    "uniform vec4 u_rect;\n"
    "void main(){\n"
    "  vec2 pos[4]=vec2[](vec2(0,1),vec2(0,0),vec2(1,1),vec2(1,0));\n"
    "  vec2 p=pos[gl_VertexID];\n"
    "  gl_Position=vec4(u_rect.x+p.x*u_rect.z, u_rect.y+p.y*u_rect.w, 0, 1);\n"
    "  v_uv=p;\n"   /* uv == pos: preserves orientation across FBOs */
    "}\n";

/* ---- Main fragment shader (modes 0-4) --------------------------------- */
static const char *s_frag =
    GLSL_VERSION
    "in vec2 v_uv;\n"
    "out vec4 fc;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_shader;\n"
    "void main(){\n"
    "  vec4 c = texture(u_tex, v_uv);\n"
    "  if (u_shader == 2) {\n"
    "    float row = mod(floor(gl_FragCoord.y), 2.0);\n"
    "    c.rgb *= mix(0.68, 1.0, row);\n"
    "  } else if (u_shader == 3) {\n"
    "    float row = mod(floor(gl_FragCoord.y), 2.0);\n"
    "    c.rgb *= mix(0.62, 1.0, row);\n"
    "    vec2 d = (v_uv - 0.5) * 2.0;\n"
    "    float vig = 1.0 - dot(d, d) * 0.20;\n"
    "    c.rgb *= clamp(vig, 0.0, 1.0);\n"
    "  } else if (u_shader == 4) {\n"
    "    vec2 grid = mod(floor(gl_FragCoord.xy), 3.0);\n"
    "    float mask = (grid.x == 2.0 || grid.y == 2.0) ? 0.55 : 1.0;\n"
    "    c.rgb *= mask;\n"
    "  }\n"
    "  fc = c;\n"
    "}\n";

/* ---- Hardware-frame blit (samples a sub-rect of the core's FBO texture) */
static const char *s_frag_hw =
    GLSL_VERSION
    "in vec2 v_uv; out vec4 fc;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_uvscale;\n"  /* used region: (w/max_w, h/max_h) */
    "uniform int u_flip;\n"      /* 1 = top-left origin → flip Y */
    "uniform int u_shader;\n"
    "void main(){\n"
    "  vec2 uv;\n"
    "  uv.x = v_uv.x * u_uvscale.x;\n"
    "  uv.y = (u_flip==1 ? (1.0 - v_uv.y) : v_uv.y) * u_uvscale.y;\n"
    "  vec4 c = texture(u_tex, uv);\n"
    "  if (u_shader == 2) {\n"
    "    float row = mod(floor(gl_FragCoord.y), 2.0);\n"
    "    c.rgb *= mix(0.68, 1.0, row);\n"
    "  } else if (u_shader == 3) {\n"
    "    float row = mod(floor(gl_FragCoord.y), 2.0);\n"
    "    c.rgb *= mix(0.62, 1.0, row);\n"
    "    vec2 d = (v_uv - 0.5) * 2.0;\n"
    "    c.rgb *= clamp(1.0 - dot(d, d) * 0.20, 0.0, 1.0);\n"
    "  } else if (u_shader == 4) {\n"
    "    vec2 g = mod(floor(gl_FragCoord.xy), 3.0);\n"
    "    c.rgb *= (g.x == 2.0 || g.y == 2.0) ? 0.55 : 1.0;\n"
    "  }\n"
    "  fc = c;\n"
    "}\n";

/* ---- ScaleFX pass 0: per-pixel color metric (1x) ---------------------- */
static const char *s_sfx_p0 =
    GLSL_VERSION
    "in vec2 v_uv; out vec4 fc;\n"
    "uniform sampler2D u_tex; uniform vec2 u_texel;\n"
    "float eq(vec3 A,vec3 B){\n"
    "  float r=0.5*(A.r+B.r); vec3 d=A-B;\n"
    "  return 1.0-sqrt(dot(vec3(2.0+r,4.0,3.0-r)*d,d))/3.0;\n"
    "}\n"
    "#define T(x,y) texture(u_tex,v_uv+vec2(float(x),float(y))*u_texel)\n"
    "void main(){\n"
    "  vec3 A=T(-1,-1).rgb,B=T(0,-1).rgb,C=T(1,-1).rgb;\n"
    "  vec3 E=T(0,0).rgb,F=T(1,0).rgb;\n"
    "  fc=vec4(eq(E,A),eq(E,B),eq(E,C),eq(E,F));\n"
    "}\n";

/* ---- ScaleFX pass 1: junction resolution (1x) ------------------------- */
static const char *s_sfx_p1 =
    GLSL_VERSION
    "in vec2 v_uv; out vec4 fc;\n"
    "uniform sampler2D u_tex; uniform vec2 u_texel;\n"
    "#define GE(x,y) (1.0-step(x,y))\n"
    "#define LEQ(x,y) step(x,y)\n"
    "#define NOT(x) (1.0-(x))\n"
    "const float THR=0.65;\n"
    "vec4 str(vec4 crn,vec4 ort){\n"
    "  return GE(crn,vec4(THR))*max(2.0*crn-(ort+ort.wxyz),vec4(0.0));}\n"
    "vec4 dom(vec3 sx,vec3 sy,vec3 sz,vec3 sw){\n"
    "  return vec4(max(2.0*sx.y-(sx.x+sx.z),0.0),max(2.0*sy.y-(sy.x+sy.z),0.0),\n"
    "              max(2.0*sz.y-(sz.x+sz.z),0.0),max(2.0*sw.y-(sw.x+sw.z),0.0));}\n"
    "float clr(vec2 crn,vec4 ort){\n"
    "  vec4 r=LEQ(crn.xyxy,vec4(THR))+LEQ(crn.xyxy,ort)+LEQ(crn.xyxy,ort.wxyz);\n"
    "  return min(r.x*r.y*r.z*r.w,1.0);}\n"
    "#define T(x,y) texture(u_tex,v_uv+vec2(float(x),float(y))*u_texel)\n"
    "void main(){\n"
    "  vec4 A=T(-1,-1),B=T(0,-1),C=T(1,-1);\n"
    "  vec4 D=T(-1,0),E=T(0,0),F=T(1,0);\n"
    "  vec4 G=T(-1,1),H=T(0,1),I=T(1,1);\n"
    "  vec4 J=T(-1,2),K=T(0,2),L=T(1,2);\n"
    "  vec4 M=T(-2,-1),N=T(-2,0),O=T(-2,1);\n"
    "  vec4 P=T(2,-1),Q=T(2,0),R=T(2,1);\n"
    "  vec4 As=str(vec4(M.z,B.x,D.z,D.x),vec4(A.y,A.w,D.y,M.w));\n"
    "  vec4 Bs=str(vec4(A.z,C.x,E.z,E.x),vec4(B.y,B.w,E.y,A.w));\n"
    "  vec4 Cs=str(vec4(B.z,P.x,F.z,F.x),vec4(C.y,C.w,F.y,B.w));\n"
    "  vec4 Ds=str(vec4(N.z,E.x,G.z,G.x),vec4(D.y,D.w,G.y,N.w));\n"
    "  vec4 Es=str(vec4(D.z,F.x,H.z,H.x),vec4(E.y,E.w,H.y,D.w));\n"
    "  vec4 Fs=str(vec4(E.z,Q.x,I.z,I.x),vec4(F.y,F.w,I.y,E.w));\n"
    "  vec4 Gs=str(vec4(O.z,H.x,J.z,J.x),vec4(G.y,G.w,J.y,O.w));\n"
    "  vec4 Hs=str(vec4(G.z,I.x,K.z,K.x),vec4(H.y,H.w,K.y,G.w));\n"
    "  vec4 Is=str(vec4(H.z,R.x,L.z,L.x),vec4(I.y,I.w,L.y,H.w));\n"
    "  vec4 jSx=vec4(As.z,Bs.w,Es.x,Ds.y),jDx=dom(As.yzw,Bs.zwx,Es.wxy,Ds.xyz);\n"
    "  vec4 jSy=vec4(Bs.z,Cs.w,Fs.x,Es.y),jDy=dom(Bs.yzw,Cs.zwx,Fs.wxy,Es.xyz);\n"
    "  vec4 jSz=vec4(Es.z,Fs.w,Is.x,Hs.y),jDz=dom(Es.yzw,Fs.zwx,Is.wxy,Hs.xyz);\n"
    "  vec4 jSw=vec4(Ds.z,Es.w,Hs.x,Gs.y),jDw=dom(Ds.yzw,Es.zwx,Hs.wxy,Gs.xyz);\n"
    "  vec4 jx=GE(jDx,vec4(0.0))*GE(jDx+jDx.zwxy,jDx.yzwx+jDx.wxyz);\n"
    "  vec4 jy=GE(jDy,vec4(0.0))*GE(jDy+jDy.zwxy,jDy.yzwx+jDy.wxyz);\n"
    "  vec4 jz=GE(jDz,vec4(0.0))*GE(jDz+jDz.zwxy,jDz.yzwx+jDz.wxyz);\n"
    "  vec4 jw=GE(jDw,vec4(0.0))*GE(jDw+jDw.zwxy,jDw.yzwx+jDw.wxyz);\n"
    "  vec4 res;\n"
    "  res.x=min(jx.z+(NOT(jx.y)*NOT(jx.w))*(GE(jSx.z,0.0)*(jx.x+GE(jSx.x+jSx.z,jSx.y+jSx.w))),1.0);\n"
    "  res.y=min(jy.w+(NOT(jy.z)*NOT(jy.x))*(GE(jSy.w,0.0)*(jy.y+GE(jSy.y+jSy.w,jSy.x+jSy.z))),1.0);\n"
    "  res.z=min(jz.x+(NOT(jz.w)*NOT(jz.y))*(GE(jSz.x,0.0)*(jz.z+GE(jSz.x+jSz.z,jSz.y+jSz.w))),1.0);\n"
    "  res.w=min(jw.y+(NOT(jw.x)*NOT(jw.z))*(GE(jSw.y,0.0)*(jw.w+GE(jSw.y+jSw.w,jSw.x+jSw.z))),1.0);\n"
    "  res=min(res*(vec4(jx.z,jy.w,jz.x,jw.y)+NOT(res.wxyz*res.yzwx)),vec4(1.0));\n"
    "  vec4 cv;\n"
    "  cv.x=clr(vec2(D.z,E.x),vec4(A.w,E.y,D.w,D.y));\n"
    "  cv.y=clr(vec2(E.z,F.x),vec4(B.w,F.y,E.w,E.y));\n"
    "  cv.z=clr(vec2(H.z,I.x),vec4(E.w,I.y,H.w,H.y));\n"
    "  cv.w=clr(vec2(G.z,H.x),vec4(D.w,H.y,G.w,G.y));\n"
    "  vec4 low=max(vec4(E.y,E.w,H.y,D.w),vec4(THR));\n"
    "  vec4 hori=vec4(low.x<max(D.w,A.w)?1.0:0.0,low.x<max(E.w,B.w)?1.0:0.0,\n"
    "                 low.z<max(E.w,H.w)?1.0:0.0,low.z<max(D.w,G.w)?1.0:0.0)*cv;\n"
    "  vec4 vert=vec4(low.w<max(E.y,D.y)?1.0:0.0,low.y<max(E.y,F.y)?1.0:0.0,\n"
    "                 low.y<max(H.y,I.y)?1.0:0.0,low.w<max(H.y,G.y)?1.0:0.0)*cv;\n"
    "  vec4 orie=vec4(A.w<D.y?1.0:0.0,B.w<=F.y?1.0:0.0,H.w<I.y?1.0:0.0,G.w<=G.y?1.0:0.0);\n"
    "  fc=(res+2.0*hori+4.0*vert+8.0*orie)/15.0;\n"
    "}\n";

/* ---- ScaleFX pass 2: edge level + subpixel tags (1x) ------------------ */
static const char *s_sfx_p2 =
    GLSL_VERSION
    "in vec2 v_uv; out vec4 fc;\n"
    "uniform sampler2D u_tex; uniform vec2 u_texel;\n"
    "bvec4 lCorn(vec4 x){return bvec4(floor(mod(x*15.0+0.5,2.0)));}\n"
    "bvec4 lHori(vec4 x){return bvec4(floor(mod(x*7.5+0.25,2.0)));}\n"
    "bvec4 lVert(vec4 x){return bvec4(floor(mod(x*3.75+0.125,2.0)));}\n"
    "bvec4 lOr  (vec4 x){return bvec4(floor(mod(x*1.875+0.0625,2.0)));}\n"
    "#define T(x,y) texture(u_tex,v_uv+vec2(float(x),float(y))*u_texel)\n"
    "void main(){\n"
    "  vec4 E=T(0,0);\n"
    "  vec4 D=T(-1,0),D0=T(-2,0),D1=T(-3,0);\n"
    "  vec4 F=T(1,0), F0=T(2,0), F1=T(3,0);\n"
    "  vec4 B=T(0,-1),B0=T(0,-2),B1=T(0,-3);\n"
    "  vec4 H=T(0,1), H0=T(0,2), H1=T(0,3);\n"
    "  bvec4 Ec=lCorn(E),Eh=lHori(E),Ev=lVert(E),Eo=lOr(E);\n"
    "  bvec4 Dc=lCorn(D), Dh=lHori(D),Do=lOr(D), D0c=lCorn(D0),D0h=lHori(D0),D1h=lHori(D1);\n"
    "  bvec4 Fc=lCorn(F), Fh=lHori(F),Fo=lOr(F), F0c=lCorn(F0),F0h=lHori(F0),F1h=lHori(F1);\n"
    "  bvec4 Bc=lCorn(B), Bv=lVert(B),Bo=lOr(B), B0c=lCorn(B0),B0v=lVert(B0),B1v=lVert(B1);\n"
    "  bvec4 Hc=lCorn(H), Hv=lVert(H),Ho=lOr(H), H0c=lCorn(H0),H0v=lVert(H0),H1v=lVert(H1);\n"
    "  bvec2 l2x=bvec2((Ec.x&&Eh.y)&&Dc.z,(Ec.y&&Eh.x)&&Fc.w);\n"
    "  bvec2 l2y=bvec2((Ec.y&&Ev.z)&&Bc.w,(Ec.z&&Ev.y)&&Hc.x);\n"
    "  bvec2 l2z=bvec2((Ec.w&&Eh.z)&&Dc.y,(Ec.z&&Eh.w)&&Fc.x);\n"
    "  bvec2 l2w=bvec2((Ec.x&&Ev.w)&&Bc.z,(Ec.w&&Ev.x)&&Hc.y);\n"
    "  bvec2 l3x=bvec2(l2x.y&&(Dh.y&&Dh.x)&&Fh.z, l2w.y&&(Bv.w&&Bv.x)&&Hv.z);\n"
    "  bvec2 l3y=bvec2(l2x.x&&(Fh.x&&Fh.y)&&Dh.w, l2y.y&&(Bv.z&&Bv.y)&&Hv.w);\n"
    "  bvec2 l3z=bvec2(l2z.x&&(Fh.w&&Fh.z)&&Dh.x, l2y.x&&(Hv.y&&Hv.z)&&Bv.x);\n"
    "  bvec2 l3w=bvec2(l2z.y&&(Dh.z&&Dh.w)&&Fh.y, l2w.x&&(Hv.x&&Hv.w)&&Bv.y);\n"
    "  bvec2 l4x=bvec2((Dc.x&&Dh.y&&Eh.x&&Eh.y&&Fh.x&&Fh.y)&&(D0c.z&&D0h.w),(Bc.x&&Bv.w&&Ev.x&&Ev.w&&Hv.x&&Hv.w)&&(B0c.z&&B0v.y));\n"
    "  bvec2 l4y=bvec2((Fc.y&&Fh.x&&Eh.y&&Eh.x&&Dh.y&&Dh.x)&&(F0c.w&&F0h.z),(Bc.y&&Bv.z&&Ev.y&&Ev.z&&Hv.y&&Hv.z)&&(B0c.w&&B0v.x));\n"
    "  bvec2 l4z=bvec2((Fc.z&&Fh.w&&Eh.z&&Eh.w&&Dh.z&&Dh.w)&&(F0c.x&&F0h.y),(Hc.z&&Hv.y&&Ev.z&&Ev.y&&Bv.z&&Bv.y)&&(H0c.x&&H0v.w));\n"
    "  bvec2 l4w=bvec2((Dc.w&&Dh.z&&Eh.w&&Eh.z&&Fh.w&&Fh.z)&&(D0c.y&&D0h.x),(Hc.w&&Hv.x&&Ev.w&&Ev.x&&Bv.w&&Bv.x)&&(H0c.y&&H0v.z));\n"
    "  bvec2 l5x=bvec2(l4x.x&&(F0h.x&&F0h.y)&&(D1h.z&&D1h.w),l4y.x&&(D0h.y&&D0h.x)&&(F1h.w&&F1h.z));\n"
    "  bvec2 l5y=bvec2(l4y.y&&(H0v.y&&H0v.z)&&(B1v.w&&B1v.x),l4z.y&&(B0v.z&&B0v.y)&&(H1v.x&&H1v.w));\n"
    "  bvec2 l5z=bvec2(l4w.x&&(F0h.w&&F0h.z)&&(D1h.y&&D1h.x),l4z.x&&(D0h.z&&D0h.w)&&(F1h.x&&F1h.y));\n"
    "  bvec2 l5w=bvec2(l4x.y&&(H0v.x&&H0v.w)&&(B1v.z&&B1v.y),l4w.y&&(B0v.w&&B0v.x)&&(H1v.y&&H1v.z));\n"
    "  bvec2 l6x=bvec2(l5x.y&&(D1h.y&&D1h.x),l5w.y&&(B1v.w&&B1v.x));\n"
    "  bvec2 l6y=bvec2(l5x.x&&(F1h.x&&F1h.y),l5y.y&&(B1v.z&&B1v.y));\n"
    "  bvec2 l6z=bvec2(l5z.x&&(F1h.w&&F1h.z),l5y.x&&(H1v.y&&H1v.z));\n"
    "  bvec2 l6w=bvec2(l5z.y&&(D1h.z&&D1h.w),l5w.x&&(H1v.x&&H1v.w));\n"
    "  vec4 crn;\n"
    "  crn.x=(Ec.x&&Eo.x||l3x.x&&Eo.y||l4x.x&&Do.x||l6x.x&&Fo.y)?5.:(Ec.x||l3x.y&&!Eo.w||l4x.y&&!Bo.x||l6x.y&&!Ho.w)?1.:l3x.x?3.:l3x.y?7.:l4x.x?2.:l4x.y?6.:l6x.x?4.:l6x.y?8.:0.;\n"
    "  crn.y=(Ec.y&&Eo.y||l3y.x&&Eo.x||l4y.x&&Fo.y||l6y.x&&Do.x)?5.:(Ec.y||l3y.y&&!Eo.z||l4y.y&&!Bo.y||l6y.y&&!Ho.z)?3.:l3y.x?1.:l3y.y?7.:l4y.x?4.:l4y.y?6.:l6y.x?2.:l6y.y?8.:0.;\n"
    "  crn.z=(Ec.z&&Eo.z||l3z.x&&Eo.w||l4z.x&&Fo.z||l6z.x&&Do.w)?7.:(Ec.z||l3z.y&&!Eo.y||l4z.y&&!Ho.z||l6z.y&&!Bo.y)?3.:l3z.x?1.:l3z.y?5.:l4z.x?4.:l4z.y?8.:l6z.x?2.:l6z.y?6.:0.;\n"
    "  crn.w=(Ec.w&&Eo.w||l3w.x&&Eo.z||l4w.x&&Do.w||l6w.x&&Fo.z)?7.:(Ec.w||l3w.y&&!Eo.x||l4w.y&&!Ho.w||l6w.y&&!Bo.x)?1.:l3w.x?3.:l3w.y?5.:l4w.x?2.:l4w.y?8.:l6w.x?4.:l6w.y?6.:0.;\n"
    "  vec4 mid;\n"
    "  mid.x=(l2x.x&&Eo.x||l2x.y&&Eo.y||l5x.x&&Do.x||l5x.y&&Fo.y)?5.:l2x.x?1.:l2x.y?3.:l5x.x?2.:l5x.y?4.:(Ec.x&&Dc.z&&Ec.y&&Fc.w)?(Eo.x?Eo.y?5.:3.:1.):0.;\n"
    "  mid.y=(l2y.x&&!Eo.y||l2y.y&&!Eo.z||l5y.x&&!Bo.y||l5y.y&&!Ho.z)?3.:l2y.x?5.:l2y.y?7.:l5y.x?6.:l5y.y?8.:(Ec.y&&Bc.w&&Ec.z&&Hc.x)?(!Eo.y?!Eo.z?3.:7.:5.):0.;\n"
    "  mid.z=(l2z.x&&Eo.w||l2z.y&&Eo.z||l5z.x&&Do.w||l5z.y&&Fo.z)?7.:l2z.x?1.:l2z.y?3.:l5z.x?2.:l5z.y?4.:(Ec.z&&Fc.x&&Ec.w&&Dc.y)?(Eo.z?Eo.w?7.:1.:3.):0.;\n"
    "  mid.w=(l2w.x&&!Eo.x||l2w.y&&!Eo.w||l5w.x&&!Bo.x||l5w.y&&!Ho.w)?1.:l2w.x?5.:l2w.y?7.:l5w.x?6.:l5w.y?8.:(Ec.w&&Hc.y&&Ec.x&&Bc.z)?(!Eo.w?!Eo.x?1.:5.:7.):0.;\n"
    "  fc=(crn+9.0*mid)/80.0;\n"
    "}\n";

/* ---- ScaleFX pass 3: subpixel output (3x) ----------------------------- */
static const char *s_sfx_p3 =
    GLSL_VERSION
    "in vec2 v_uv; out vec4 fc;\n"
    "uniform sampler2D u_tex;\n"   /* pass2 data */
    "uniform sampler2D u_orig;\n"  /* original game frame */
    "uniform vec2 u_src_size;\n"   /* game frame dimensions */
    "uniform vec2 u_texel;\n"      /* 1/src_size */
    "vec4 lCrn(vec4 x){return floor(mod(x*80.0+0.5,9.0));}\n"
    "vec4 lMid(vec4 x){return floor(mod(x*8.888888+0.055555,9.0));}\n"
    "void main(){\n"
    "  vec4 E=texture(u_tex,v_uv);\n"
    "  vec4 crn=lCrn(E), mid=lMid(E);\n"
    "  vec2 fp=floor(3.0*fract(v_uv*u_src_size));\n"
    "  float sp=fp.y==0.?(fp.x==0.?crn.x:fp.x==1.?mid.x:crn.y)\n"
    "            :(fp.y==1.?(fp.x==0.?mid.w:fp.x==1.?0.:mid.y)\n"
    "            :(fp.x==0.?crn.w:fp.x==1.?mid.z:crn.z));\n"
    "  vec2 res=sp==0.?vec2(0,0):sp==1.?vec2(-1,0):sp==2.?vec2(-2,0)\n"
    "          :sp==3.?vec2(1,0):sp==4.?vec2(2,0):sp==5.?vec2(0,-1)\n"
    "          :sp==6.?vec2(0,-2):sp==7.?vec2(0,1):vec2(0,2);\n"
    "  fc=texture(u_orig,v_uv+u_texel*res);\n"
    "}\n";

/* ---- Bloom pass 1: threshold + downsample ----------------------------- */
static const char *s_frag_threshold =
    GLSL_VERSION
    "in vec2 v_uv;\n"
    "out vec4 fc;\n"
    "uniform sampler2D u_tex;\n"
    "void main(){\n"
    "  vec4 c = texture(u_tex, v_uv);\n"
    "  float br = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
    "  float t = 0.50;\n"
    "  float factor = clamp((br - t) / (1.0 - t), 0.0, 1.0);\n"
    "  fc = vec4(c.rgb * factor, 1.0);\n"
    "}\n";

/* ---- Bloom pass 2: horizontal Gaussian blur --------------------------- */
static const char *s_frag_blur_h =
    GLSL_VERSION
    "in vec2 v_uv;\n"
    "out vec4 fc;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texel;\n"
    "void main(){\n"
    "  float w[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);\n"
    "  vec3 col = texture(u_tex, v_uv).rgb * w[0];\n"
    "  for (int i = 1; i < 5; i++) {\n"
    "    float off = float(i) * u_texel.x;\n"
    "    col += texture(u_tex, v_uv + vec2(off, 0.0)).rgb * w[i];\n"
    "    col += texture(u_tex, v_uv - vec2(off, 0.0)).rgb * w[i];\n"
    "  }\n"
    "  fc = vec4(col, 1.0);\n"
    "}\n";

/* ---- Bloom pass 3: vertical blur + additive composite ----------------- */
static const char *s_frag_blur_v =
    GLSL_VERSION
    "in vec2 v_uv;\n"
    "out vec4 fc;\n"
    "uniform sampler2D u_tex;\n"   /* bloom after h-blur */
    "uniform sampler2D u_orig;\n"  /* original game frame */
    "uniform vec2 u_texel;\n"
    "void main(){\n"
    "  float w[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);\n"
    "  vec3 bloom = texture(u_tex, v_uv).rgb * w[0];\n"
    "  for (int i = 1; i < 5; i++) {\n"
    "    float off = float(i) * u_texel.y;\n"
    "    bloom += texture(u_tex, v_uv + vec2(0.0, off)).rgb * w[i];\n"
    "    bloom += texture(u_tex, v_uv - vec2(0.0, off)).rgb * w[i];\n"
    "  }\n"
    "  vec3 orig = texture(u_orig, v_uv).rgb;\n"
    "  fc = vec4(clamp(orig + bloom * 0.85, 0.0, 1.0), 1.0);\n"
    "}\n";

/* ---- State ------------------------------------------------------------ */
static GLuint s_prog;           /* main shader (modes 0,2-4) */
static GLuint s_prog_hw;       /* hardware-frame blit */
static GLuint s_sfx_prog[4];   /* ScaleFX passes 0-3 */
static GLuint s_sfx_fbo[3];    /* intermediate FBOs (pass0/1/2 output, 1x) */
static GLuint s_sfx_tex[3];
static GLuint s_sfx_fbo3;     /* pass3 output FBO at exactly 3x source */
static GLuint s_sfx_tex3;
static unsigned s_sfx_fw = 0;  /* current 1x FBO dimensions */
static unsigned s_sfx_fh = 0;
static GLuint s_prog_thresh;    /* bloom pass 1 */
static GLuint s_prog_blur_h;    /* bloom pass 2 */
static GLuint s_prog_blur_v;    /* bloom pass 3 + composite */
static GLuint s_vao;
static GLuint s_tex;            /* game frame texture */
static GLuint s_bloom_fbo[2];   /* [0]=threshold, [1]=h-blur result */
static GLuint s_bloom_tex[2];
static int    s_pixel_fmt  = RETRO_PIXEL_FORMAT_RGB565;
static int    s_shader_id  = 0;
static GLint  s_loc_shader = -1;
static unsigned s_frame_w  = 0; /* last uploaded game frame dimensions */
static unsigned s_frame_h  = 0;

/* Hardware-rendered frame (core drew into its own FBO texture) */
static int      s_hw_active = 0;
static GLuint   s_hw_tex    = 0;
static unsigned s_hw_w = 0, s_hw_h = 0, s_hw_mw = 0, s_hw_mh = 0;
static int      s_hw_bl     = 0;

/* Letterboxed 16:9 region of the real window where content is presented. */
static int s_vp_x = 0, s_vp_y = 0, s_vp_w = 1280, s_vp_h = 720;
void renderer_set_screen_viewport(int x, int y, int w, int h) {
    s_vp_x = x; s_vp_y = y; s_vp_w = w; s_vp_h = h;
}

/* Bloom FBO resolution: half of output window */
#define BLOOM_W 640
#define BLOOM_H 360

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = gl_CreateShader(type);
    gl_ShaderSource(s, 1, &src, NULL);
    gl_CompileShader(s);
    GLint ok = 0;
    gl_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        gl_GetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "renderer shader: %s\n", log);
    }
    return s;
}

static GLuint link_prog_vf(const char *vert_src, const char *frag_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    if (!vs || !fs) return 0;
    GLuint prog = gl_CreateProgram();
    gl_AttachShader(prog, vs);
    gl_AttachShader(prog, fs);
    gl_LinkProgram(prog);
    gl_DeleteShader(vs);
    gl_DeleteShader(fs);
    GLint ok = 0;
    gl_GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        gl_GetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "renderer link: %s\n", log);
        return 0;
    }
    return prog;
}

static GLuint link_prog(const char *frag_src) {
    return link_prog_vf(s_vert, frag_src);
}

static void make_bloom_fbo(int idx, int w, int h) {
    glGenTextures(1, &s_bloom_tex[idx]);
    glBindTexture(GL_TEXTURE_2D, s_bloom_tex[idx]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    gl_GenFramebuffers(1, &s_bloom_fbo[idx]);
    gl_BindFramebuffer(GL_FRAMEBUFFER, s_bloom_fbo[idx]);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, s_bloom_tex[idx], 0);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Draw a fullscreen quad with the currently bound program. */
static void draw_fullscreen(GLuint prog) {
    static const float full[4] = {-1.0f, -1.0f, 2.0f, 2.0f};
    gl_UseProgram(prog);
    GLint loc = gl_GetUniformLocation(prog, "u_rect");
    if (loc >= 0) gl_Uniform4fv(loc, 1, full);
    gl_BindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_BindVertexArray(0);
}

int renderer_init(void) {
    s_prog = link_prog(s_frag);
    if (!s_prog) return 0;

    gl_UseProgram(s_prog);
    GLint loc = gl_GetUniformLocation(s_prog, "u_tex");
    if (loc >= 0) gl_Uniform1i(loc, 0);
    s_loc_shader = gl_GetUniformLocation(s_prog, "u_shader");
    if (s_loc_shader >= 0) gl_Uniform1i(s_loc_shader, 0);

    /* Hardware-frame blit program (identity vertex: no Y-flip baked in) */
    s_prog_hw = link_prog_vf(s_vert_id, s_frag_hw);
    if (s_prog_hw) {
        gl_UseProgram(s_prog_hw);
        loc = gl_GetUniformLocation(s_prog_hw, "u_tex");
        if (loc >= 0) gl_Uniform1i(loc, 0);
    }

    /* ScaleFX passes */
    const char *sfx_srcs[4] = {s_sfx_p0, s_sfx_p1, s_sfx_p2, s_sfx_p3};
    for (int i = 0; i < 4; i++) {
        s_sfx_prog[i] = link_prog_vf(s_vert_id, sfx_srcs[i]);
        if (s_sfx_prog[i]) {
            gl_UseProgram(s_sfx_prog[i]);
            GLint tl = gl_GetUniformLocation(s_sfx_prog[i], "u_tex");
            if (tl >= 0) gl_Uniform1i(tl, 0);
        }
    }
    /* pass3 also needs u_orig on unit 1 */
    if (s_sfx_prog[3]) {
        gl_UseProgram(s_sfx_prog[3]);
        GLint tl = gl_GetUniformLocation(s_sfx_prog[3], "u_orig");
        if (tl >= 0) gl_Uniform1i(tl, 1);
    }

    s_prog_thresh = link_prog(s_frag_threshold);
    s_prog_blur_h = link_prog(s_frag_blur_h);
    s_prog_blur_v = link_prog(s_frag_blur_v);

    if (s_prog_thresh) {
        gl_UseProgram(s_prog_thresh);
        loc = gl_GetUniformLocation(s_prog_thresh, "u_tex");
        if (loc >= 0) gl_Uniform1i(loc, 0);
    }
    if (s_prog_blur_h) {
        gl_UseProgram(s_prog_blur_h);
        loc = gl_GetUniformLocation(s_prog_blur_h, "u_tex");
        if (loc >= 0) gl_Uniform1i(loc, 0);
        loc = gl_GetUniformLocation(s_prog_blur_h, "u_texel");
        if (loc >= 0) gl_Uniform2f(loc, 1.0f / BLOOM_W, 1.0f / BLOOM_H);
    }
    if (s_prog_blur_v) {
        gl_UseProgram(s_prog_blur_v);
        loc = gl_GetUniformLocation(s_prog_blur_v, "u_tex");
        if (loc >= 0) gl_Uniform1i(loc, 0);
        loc = gl_GetUniformLocation(s_prog_blur_v, "u_orig");
        if (loc >= 0) gl_Uniform1i(loc, 1);
        loc = gl_GetUniformLocation(s_prog_blur_v, "u_texel");
        if (loc >= 0) gl_Uniform2f(loc, 1.0f / BLOOM_W, 1.0f / BLOOM_H);
    }

    gl_GenVertexArrays(1, &s_vao);

    glGenTextures(1, &s_tex);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    make_bloom_fbo(0, BLOOM_W, BLOOM_H);
    make_bloom_fbo(1, BLOOM_W, BLOOM_H);

    /* Reset frame-size state so ScaleFX FBOs are (re)created on the first
       frame of this game — static vars persist across init/shutdown cycles. */
    s_frame_w = s_frame_h = 0;
    s_sfx_fw  = s_sfx_fh  = 0;
    s_hw_active = 0;
    s_hw_tex    = 0;

    s_shader_id = 0;
    return 1;
}

void renderer_set_hw_frame(unsigned tex, unsigned w, unsigned h,
                           unsigned max_w, unsigned max_h, int bottom_left) {
    s_hw_active = 1;
    s_hw_tex = tex;
    s_hw_w = w;  s_hw_h = h;
    s_hw_mw = max_w ? max_w : w;
    s_hw_mh = max_h ? max_h : h;
    s_hw_bl  = bottom_left;
}

void renderer_set_shader(int id) {
    if (id < 0 || id > 5) id = 0;
    s_shader_id = id;
    /* ScaleFX (1) does its own interpolation — needs NEAREST source samples.
       CRT (3) and Bloom (5) benefit from LINEAR pre-filtering. */
    GLint filter = (id == 3 || id == 5) ? GL_LINEAR : GL_NEAREST;
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glBindTexture(GL_TEXTURE_2D, 0);
}

#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_TEXTURE_SWIZZLE_R
#define GL_TEXTURE_SWIZZLE_R 0x8E42
#endif
#ifndef GL_TEXTURE_SWIZZLE_B
#define GL_TEXTURE_SWIZZLE_B 0x8E44
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_BLUE
#define GL_BLUE 0x1905
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif

void renderer_set_frame(const void *data, unsigned w, unsigned h,
                        size_t pitch, int pixel_fmt) {
    if (!data || !w || !h) return;
    s_hw_active = 0;   /* this is a CPU frame */
    s_pixel_fmt = pixel_fmt;

    /* Recreate ScaleFX intermediate FBOs if source size changed */
    if (w != s_frame_w || h != s_frame_h) {
        for (int i = 0; i < 3; i++) {
            if (s_sfx_fbo[i]) { gl_DeleteFramebuffers(1, &s_sfx_fbo[i]); s_sfx_fbo[i] = 0; }
            if (s_sfx_tex[i]) { glDeleteTextures(1, &s_sfx_tex[i]);       s_sfx_tex[i] = 0; }
            glGenTextures(1, &s_sfx_tex[i]);
            glBindTexture(GL_TEXTURE_2D, s_sfx_tex[i]);
            /* Passes pack non-color data unpacked bit-by-bit later: need float
               precision, not 8-bit unorm. */
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, (GLsizei)w, (GLsizei)h,
                         0, GL_RGBA, GL_FLOAT, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            gl_GenFramebuffers(1, &s_sfx_fbo[i]);
            gl_BindFramebuffer(GL_FRAMEBUFFER, s_sfx_fbo[i]);
            gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                    GL_TEXTURE_2D, s_sfx_tex[i], 0);
            gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        /* Pass3 FBO at exactly 3x source resolution */
        if (s_sfx_fbo3) { gl_DeleteFramebuffers(1, &s_sfx_fbo3); s_sfx_fbo3 = 0; }
        if (s_sfx_tex3) { glDeleteTextures(1, &s_sfx_tex3);       s_sfx_tex3 = 0; }
        glGenTextures(1, &s_sfx_tex3);
        glBindTexture(GL_TEXTURE_2D, s_sfx_tex3);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)(w*3), (GLsizei)(h*3),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl_GenFramebuffers(1, &s_sfx_fbo3);
        gl_BindFramebuffer(GL_FRAMEBUFFER, s_sfx_fbo3);
        gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, s_sfx_tex3, 0);
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);

        s_sfx_fw = w;  s_sfx_fh = h;
    }

    s_frame_w = w;
    s_frame_h = h;
    glBindTexture(GL_TEXTURE_2D, s_tex);
    if (pixel_fmt == RETRO_PIXEL_FORMAT_RGB565) {
#ifdef EL_GLES_API
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_BLUE);
#endif
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(pitch / 2));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)w, (GLsizei)h, 0,
                     GL_RGB, GL_UNSIGNED_SHORT_5_6_5, data);
    } else {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(pitch / 4));
#ifdef EL_GLES_API
        /* GLES 3.0 has no GL_BGRA upload format. XRGB8888 is B,G,R,X in memory,
           so upload the bytes as RGBA and swizzle R<->B to correct the channels. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data);
#else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
#endif
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void renderer_draw(void) {
    /* Letterbox the game at its display aspect inside the 16:9 screen
       (e.g. GBA 3:2 is wider than the old hard-coded 4:3). */
    float aspect = core_get_avinfo().aspect;
    if (aspect <= 0.0f) aspect = 4.0f/3.0f;
    float nw = aspect * (720.0f / 1280.0f) * 2.0f;
    if (nw > 2.0f) nw = 2.0f;   /* clamp ultra-wide to screen width */
    float nh = 2.0f;
    float nx = -nw / 2.0f;
    float ny = -1.0f;

    /* === Hardware-rendered frame: blit from the core's FBO to screen === */
    if (s_hw_active && core_hw_fbo()) {
        /* Clear the whole window (black bars), then blit the core's FBO into
           the centered 4:3 letterbox. bottom_left_origin=1 → no Y flip. */
        gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(s_vp_x, s_vp_y, s_vp_w, s_vp_h);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        /* Letterbox at the core's aspect inside the 16:9 screen viewport. */
        float ar = core_get_avinfo().aspect;
        if (ar <= 0.0f) ar = 4.0f/3.0f;
        float frac = ar * 9.0f / 16.0f;          /* game width / 16:9 width */
        if (frac > 1.0f) frac = 1.0f;
        const GLint dxm = (GLint)(s_vp_w * (1.0f - frac) * 0.5f);
        const GLint dx0 = s_vp_x + dxm, dx1 = s_vp_x + s_vp_w - dxm;
        const GLint dy0 = s_vp_y, dy1 = s_vp_y + s_vp_h;
        gl_BindFramebuffer(GL_READ_FRAMEBUFFER, core_hw_fbo());
        gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        if (s_hw_bl)
            gl_BlitFramebuffer(0, 0, (GLint)s_hw_w, (GLint)s_hw_h,
                               dx0, dy0, dx1, dy1, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        else
            gl_BlitFramebuffer(0, (GLint)s_hw_h, (GLint)s_hw_w, 0,
                               dx0, dy0, dx1, dy1, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (s_shader_id == 5) {
        /* === Bloom: 3-pass pipeline === */

        /* Pass 1: threshold -> bloom FBO[0] at half res */
        gl_BindFramebuffer(GL_FRAMEBUFFER, s_bloom_fbo[0]);
        glViewport(0, 0, BLOOM_W, BLOOM_H);
        glClear(GL_COLOR_BUFFER_BIT);
        gl_ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_tex);
        draw_fullscreen(s_prog_thresh);

        /* Pass 2: horizontal blur -> bloom FBO[1] */
        gl_BindFramebuffer(GL_FRAMEBUFFER, s_bloom_fbo[1]);
        glClear(GL_COLOR_BUFFER_BIT);
        gl_ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_bloom_tex[0]);
        draw_fullscreen(s_prog_blur_h);

        /* Pass 3: vertical blur + composite -> screen (letterboxed) */
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(s_vp_x, s_vp_y, s_vp_w, s_vp_h);

        gl_UseProgram(s_prog_blur_v);
        GLint loc = gl_GetUniformLocation(s_prog_blur_v, "u_rect");
        if (loc >= 0) {
            float r[4] = {nx, ny, nw, nh};
            gl_Uniform4fv(loc, 1, r);
        }
        gl_ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_bloom_tex[1]);
        gl_ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, s_tex);
        gl_BindVertexArray(s_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl_BindVertexArray(0);

        /* Restore texture unit 1 */
        glBindTexture(GL_TEXTURE_2D, 0);
        gl_ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);

    } else if (s_shader_id == 1 && s_sfx_prog[3] && s_sfx_fw > 0) {
        /* === ScaleFX-9x: 4-pass pipeline === */
        float fw = (float)s_sfx_fw, fh = (float)s_sfx_fh;
        float tx = 1.0f / fw, ty = 1.0f / fh;
        static const float full[4] = {-1.0f, -1.0f, 2.0f, 2.0f};
        GLint loc;

        /* Passes 0-2: render to intermediate FBOs at source resolution */
        for (int p = 0; p < 3; p++) {
            if (!s_sfx_prog[p]) continue;
            gl_BindFramebuffer(GL_FRAMEBUFFER, s_sfx_fbo[p]);
            glViewport(0, 0, (GLsizei)s_sfx_fw, (GLsizei)s_sfx_fh);
            glClear(GL_COLOR_BUFFER_BIT);
            gl_UseProgram(s_sfx_prog[p]);
            loc = gl_GetUniformLocation(s_sfx_prog[p], "u_texel");
            if (loc >= 0) gl_Uniform2f(loc, tx, ty);
            loc = gl_GetUniformLocation(s_sfx_prog[p], "u_rect");
            if (loc >= 0) gl_Uniform4fv(loc, 1, full);
            gl_ActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, p == 0 ? s_tex : s_sfx_tex[p-1]);
            gl_BindVertexArray(s_vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            gl_BindVertexArray(0);
        }

        /* Pass 3: scale to 3x FBO (exact 3x, required for correct subpixel math) */
        gl_BindFramebuffer(GL_FRAMEBUFFER, s_sfx_fbo3);
        glViewport(0, 0, (GLsizei)(s_sfx_fw*3), (GLsizei)(s_sfx_fh*3));
        glClear(GL_COLOR_BUFFER_BIT);
        gl_UseProgram(s_sfx_prog[3]);
        loc = gl_GetUniformLocation(s_sfx_prog[3], "u_src_size");
        if (loc >= 0) gl_Uniform2f(loc, fw, fh);
        loc = gl_GetUniformLocation(s_sfx_prog[3], "u_texel");
        if (loc >= 0) gl_Uniform2f(loc, tx, ty);
        loc = gl_GetUniformLocation(s_sfx_prog[3], "u_rect");
        if (loc >= 0) gl_Uniform4fv(loc, 1, full);
        gl_ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_sfx_tex[2]);
        gl_ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, s_tex);
        gl_BindVertexArray(s_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl_BindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl_ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);

        /* Final blit: letterbox the 3x image to screen */
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(s_vp_x, s_vp_y, s_vp_w, s_vp_h);
        gl_UseProgram(s_prog);
        if (s_loc_shader >= 0) gl_Uniform1i(s_loc_shader, 0);
        loc = gl_GetUniformLocation(s_prog, "u_rect");
        if (loc >= 0) {
            float r[4] = {nx, ny, nw, nh};
            gl_Uniform4fv(loc, 1, r);
        }
        gl_ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_sfx_tex3);
        gl_BindVertexArray(s_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl_BindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);

    } else {
        /* === Standard single-pass shaders (0, 2-4) === */
        gl_UseProgram(s_prog);
        if (s_loc_shader >= 0)
            gl_Uniform1i(s_loc_shader, s_shader_id);

        GLint loc = gl_GetUniformLocation(s_prog, "u_rect");
        if (loc >= 0) {
            float r[4] = {nx, ny, nw, nh};
            gl_Uniform4fv(loc, 1, r);
        }
        gl_ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_tex);
        gl_BindVertexArray(s_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl_BindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void renderer_shutdown(void) {
    if (s_prog)    { gl_DeleteProgram(s_prog);    s_prog = 0; }
    if (s_prog_hw) { gl_DeleteProgram(s_prog_hw); s_prog_hw = 0; }
    for (int i = 0; i < 4; i++) {
        if (s_sfx_prog[i]) { gl_DeleteProgram(s_sfx_prog[i]); s_sfx_prog[i] = 0; }
    }
    for (int i = 0; i < 3; i++) {
        if (s_sfx_fbo[i]) { gl_DeleteFramebuffers(1, &s_sfx_fbo[i]); s_sfx_fbo[i] = 0; }
        if (s_sfx_tex[i]) { glDeleteTextures(1, &s_sfx_tex[i]);       s_sfx_tex[i] = 0; }
    }
    if (s_sfx_fbo3) { gl_DeleteFramebuffers(1, &s_sfx_fbo3); s_sfx_fbo3 = 0; }
    if (s_sfx_tex3) { glDeleteTextures(1, &s_sfx_tex3);       s_sfx_tex3 = 0; }
    if (s_prog_thresh)  { gl_DeleteProgram(s_prog_thresh);  s_prog_thresh = 0; }
    if (s_prog_blur_h) { gl_DeleteProgram(s_prog_blur_h); s_prog_blur_h = 0; }
    if (s_prog_blur_v) { gl_DeleteProgram(s_prog_blur_v); s_prog_blur_v = 0; }
    if (s_vao)         { gl_DeleteVertexArrays(1, &s_vao); s_vao = 0; }
    if (s_tex)         { glDeleteTextures(1, &s_tex); s_tex = 0; }
    if (s_bloom_fbo[0]) { gl_DeleteFramebuffers(1, &s_bloom_fbo[0]); s_bloom_fbo[0] = 0; }
    if (s_bloom_fbo[1]) { gl_DeleteFramebuffers(1, &s_bloom_fbo[1]); s_bloom_fbo[1] = 0; }
    if (s_bloom_tex[0]) { glDeleteTextures(1, &s_bloom_tex[0]); s_bloom_tex[0] = 0; }
    if (s_bloom_tex[1]) { glDeleteTextures(1, &s_bloom_tex[1]); s_bloom_tex[1] = 0; }
}
