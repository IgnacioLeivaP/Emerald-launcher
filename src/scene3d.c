#include "scene3d.h"
#include "gl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#  define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#  define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_DEPTH_COMPONENT24
#  define GL_DEPTH_COMPONENT24 0x81A6
#endif

/* MSAA sample count for the offscreen target. The Switch shares its memory
   between CPU and GPU, so it gets the lighter setting. */
#ifdef __SWITCH__
#  define EL_MSAA 2
#else
#  define EL_MSAA 4
#endif
/* Cap on the offscreen width (a maximized 4K window renders at 1920 wide). */
#define MAX_TARGET_W 1920

/* ── Shaders ───────────────────────────────────────────────────────────── */
static const char *BOX_VS =
    GLSL_VERSION
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_nrm;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "out vec3 v_wpos;\n"
    "out vec3 v_nrm;\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  vec4 wp = u_model * vec4(a_pos, 1.0);\n"
    "  v_wpos = wp.xyz;\n"
    "  v_nrm  = mat3(u_model) * a_nrm;\n"
    "  v_uv   = a_uv;\n"
    "  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

static const char *BOX_FS =
    GLSL_VERSION
    "in vec3 v_wpos;\n"
    "in vec3 v_nrm;\n"
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec3  u_color;\n"
    "uniform float u_bright;\n"
    "uniform float u_spec;\n"
    "uniform float u_shine;\n"
    "uniform float u_alpha;\n"
    "uniform vec3  u_eye;\n"
    "uniform vec3  u_light;\n"
    "uniform vec3  u_reflect;\n"   /* x>0.5: mirrored pass, y: floor y, z: fade */
    "void main(){\n"
    "  if (u_reflect.x > 0.5 && v_wpos.y > u_reflect.y + 0.002) discard;\n"
    "  vec3 n = normalize(v_nrm);\n"
    "  vec3 base = texture(u_tex, v_uv).rgb * u_color;\n"
    "  float diff = max(dot(n, u_light), 0.0);\n"
    "  vec3 v = normalize(u_eye - v_wpos);\n"
    "  vec3 h = normalize(u_light + v);\n"
    "  float sp = pow(max(dot(n, h), 0.0), u_shine) * u_spec;\n"
    "  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.06;\n"
    "  vec3 col = (base * (0.52 + 0.60 * diff) + vec3(sp + rim)) * u_bright;\n"
    "  float a = u_alpha;\n"
    "  if (u_reflect.x > 0.5)\n"
    "    a *= clamp(1.0 - (u_reflect.y - v_wpos.y) / u_reflect.z, 0.0, 1.0);\n"
    "  frag = vec4(col, a);\n"
    "}\n";

/* Effects: textured composite, soft ellipse, floor, glow, backdrop. */
static const char *FX_VS =
    GLSL_VERSION
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "uniform int  u_screen;\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  v_uv = a_uv;\n"
    "  if (u_screen == 1) gl_Position = vec4(a_pos.xy * 2.0, 0.0, 1.0);\n"
    "  else               gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

static const char *FX_FS =
    GLSL_VERSION
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "uniform int  u_mode;\n"
    "uniform vec4 u_color;\n"
    "uniform vec4 u_p0;\n"
    "uniform vec4 u_p1;\n"
    "uniform sampler2D u_tex;\n"
    "void main(){\n"
    "  if (u_mode == 0) {\n"                         /* textured */
    "    frag = texture(u_tex, v_uv) * u_color;\n"
    "  } else if (u_mode == 1) {\n"                  /* soft ellipse */
    "    float d = length(v_uv * 2.0 - 1.0);\n"
    "    float a = 1.0 - smoothstep(u_p0.x, 1.0, d);\n"
    "    frag = vec4(u_color.rgb, u_color.a * a * a);\n"
    "  } else if (u_mode == 2) {\n"                  /* floor, fades with distance */
    "    vec2 p = (v_uv - 0.5) * u_p1.xy - u_p0.xy;\n"
    "    float a = 1.0 - smoothstep(u_p0.z, u_p0.w, length(p));\n"
    "    frag = vec4(u_color.rgb, u_color.a * a);\n"
    "  } else if (u_mode == 3) {\n"                  /* glow around a rounded rect (additive) */
    "    vec2 p = (v_uv - 0.5) * u_p1.xy;\n"
    "    vec2 q = abs(p) - u_p0.xy + u_p0.w;\n"
    "    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_p0.w;\n"
    "    float g = 1.0 - clamp(d / u_p0.z, 0.0, 1.0);\n"
    "    frag = vec4(u_color.rgb * (g * g * u_color.a), 1.0);\n"
    "  } else {\n"                                   /* backdrop: spotlight + vignette */
    "    vec2 q = (v_uv - u_p0.xy) / u_p0.zw;\n"
    "    float spot = exp(-dot(q, q) * 1.6);\n"
    "    vec2 c = (v_uv - 0.5) * vec2(1.0, 0.75);\n"
    "    float vig = smoothstep(0.30, 0.78, length(c)) * u_p1.x;\n"
    "    frag = vec4(u_color.rgb * (spot * u_color.a), vig);\n"
    "  }\n"
    "}\n";

typedef struct {
    GLint mvp, model, color, bright, spec, shine, alpha, eye, light, reflect, tex;
} BoxLocs;
typedef struct {
    GLint mvp, screen, mode, color, p0, p1, tex;
} FxLocs;

static GLuint  s_box_prog, s_fx_prog;
static BoxLocs s_bl;
static FxLocs  s_fl;
static GLuint  s_box_vao, s_box_vbo, s_quad_vao, s_quad_vbo;
static GLuint  s_white_tex;
static float   s_aniso = 0.0f;
static int     s_ready = 0;

static Mat4  s_view, s_proj, s_vpm;
static float s_eye[3] = { 0.0f, 0.0f, 5.0f };
static float s_light[3];

/* Offscreen target */
static GLuint s_fbo_ms, s_rb_ms_color, s_rb_ms_depth;   /* multisampled scene     */
static GLuint s_fbo_res, s_tex_res, s_rb_res_depth;     /* resolved (or direct)   */
static int    s_tw = 0, s_th = 0, s_samples = 0, s_target_ok = 0;
static int    s_fail_w = -1, s_fail_h = -1;
static GLint  s_vp[4];

/* ── Helpers ───────────────────────────────────────────────────────────── */
static GLuint compile(GLenum type, const char *src) {
    GLuint s = gl_CreateShader(type);
    gl_ShaderSource(s, 1, &src, NULL);
    gl_CompileShader(s);
    GLint ok = 0;
    gl_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl_GetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "scene3d shader: %s\n", log);
        gl_DeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link(const char *vs_src, const char *fs_src) {
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        if (vs) gl_DeleteShader(vs);
        if (fs) gl_DeleteShader(fs);
        return 0;
    }
    GLuint p = gl_CreateProgram();
    gl_AttachShader(p, vs);
    gl_AttachShader(p, fs);
    gl_LinkProgram(p);
    gl_DeleteShader(vs);
    gl_DeleteShader(fs);
    GLint ok = 0;
    gl_GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl_GetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "scene3d link: %s\n", log);
        gl_DeleteProgram(p);
        return 0;
    }
    return p;
}

static void clear_gl_errors(void) {
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {}
}

typedef struct { float p[3], n[3], uv[2]; } Vtx;

static void setup_attribs(void) {
    gl_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0);
    gl_EnableVertexAttribArray(0);
    gl_VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(3 * sizeof(float)));
    gl_EnableVertexAttribArray(1);
    gl_VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(6 * sizeof(float)));
    gl_EnableVertexAttribArray(2);
}

/* Rounded-edge ("chamfered") box meshes, one per shape, all in one VBO.
   Per shape: [main: 5 faces + 12 edge strips + 8 corners][back, atlas edge
   color][back, full UVs for a separate back-cover texture]. Edge strips carry
   the normals of both faces they join, so light rolls over the fold like on
   a real cardboard box. */
#define MAIN_VERTS   (5 * 6 + 12 * 6 + 8 * 3)
#define SHAPE_VERTS  (MAIN_VERTS + 12)
static int s_shape_first[BOXSHAPE_COUNT];

/* Face with outward normal N, right R and up U (R x U = N) as seen from
   outside, at distance hn, half extents hr x hu, inset by the bevel b. */
static void emit_face(Vtx *v, int *n, const float N[3], const float R[3], const float U[3],
                      float hn, float hr, float hu, float b,
                      float u0, float v0, float u1, float v1) {
    static const float sr[4] = { -1.0f, -1.0f, 1.0f, 1.0f }, su[4] = { 1.0f, -1.0f, -1.0f, 1.0f };
    const float qu[4] = { u0, u0, u1, u1 }, qv[4] = { v0, v1, v1, v0 };
    Vtx q[4];
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 3; k++) {
            q[i].p[k] = N[k] * hn + R[k] * sr[i] * (hr - b) + U[k] * su[i] * (hu - b);
            q[i].n[k] = N[k];
        }
        q[i].uv[0] = qu[i];
        q[i].uv[1] = qv[i];
    }
    v[(*n)++] = q[0]; v[(*n)++] = q[1]; v[(*n)++] = q[2];
    v[(*n)++] = q[0]; v[(*n)++] = q[2]; v[(*n)++] = q[3];
}

/* Triangle, flipped if needed so it faces `out`. */
static void emit_tri(Vtx *v, int *n, Vtx a, Vtx b, Vtx c, const float out[3]) {
    float e1[3], e2[3], cr[3];
    for (int k = 0; k < 3; k++) { e1[k] = b.p[k] - a.p[k]; e2[k] = c.p[k] - a.p[k]; }
    cr[0] = e1[1] * e2[2] - e1[2] * e2[1];
    cr[1] = e1[2] * e2[0] - e1[0] * e2[2];
    cr[2] = e1[0] * e2[1] - e1[1] * e2[0];
    if (cr[0] * out[0] + cr[1] * out[1] + cr[2] * out[2] < 0.0f) { Vtx t = b; b = c; c = t; }
    v[(*n)++] = a; v[(*n)++] = b; v[(*n)++] = c;
}

static void build_shape(Vtx *v, int *n, const BoxShape *sh) {
    const float hx = sh->w * 0.5f, hy = sh->h * 0.5f, hz = sh->d * 0.5f, b = sh->bevel;
    const float half[3] = { hx, hy, hz };
    const float AW = (float)sh->atlas_w, AH = (float)sh->atlas_h;
    const float fw = (float)sh->fw, fh = (float)sh->fh, sd = (float)sh->sd;
    /* UV rects (half-texel insets keep neighbors from bleeding in). */
    const float fu0 = 0.5f / AW, fu1 = (fw - 0.5f) / AW, fv0 = 0.5f / AH, fv1 = (fh - 0.5f) / AH;
    const float su0 = (fw + 0.5f) / AW, su1 = (fw + sd - 0.5f) / AW;
    const float tv0 = (fh + 0.5f) / AH, tv1 = (fh + sd - 0.5f) / AH;
    const float pu0 = (fw + 3.0f) / AW, pu1 = (fw + sd - 3.0f) / AW;
    const float pv0 = (fh + 3.0f) / AH, pv1 = (fh + sd - 3.0f) / AH, pvm = (pv0 + pv1) * 0.5f;

    static const float PX[3] = { 1, 0, 0 }, NX[3] = { -1, 0, 0 };
    static const float PY[3] = { 0, 1, 0 }, NY[3] = { 0, -1, 0 };
    static const float PZ[3] = { 0, 0, 1 }, NZ[3] = { 0, 0, -1 };

    emit_face(v, n, PZ, PX, PY, hz, hx, hy, b, fu0, fv0, fu1, fv1);   /* front          */
    emit_face(v, n, NX, PZ, PY, hx, hz, hy, b, su0, fv0, su1, fv1);   /* left: spine    */
    emit_face(v, n, PX, NZ, PY, hx, hz, hy, b, su0, fv0, su1, fv1);   /* right: spine   */
    emit_face(v, n, PY, PX, NZ, hy, hx, hz, b, fu0, tv0, fu1, tv1);   /* top            */
    emit_face(v, n, NY, PX, PZ, hy, hx, hz, b, fu0, tv0, fu1, tv1);   /* bottom         */

    /* 12 edge strips: along axis e, joining the faces on axes a and c. */
    for (int e = 0; e < 3; e++) {
        const int a = (e + 1) % 3, c = (e + 2) % 3;
        for (int sa = -1; sa <= 1; sa += 2)
            for (int sc = -1; sc <= 1; sc += 2) {
                Vtx q[4];
                float out[3] = { 0, 0, 0 };
                out[a] = (float)sa; out[c] = (float)sc;
                for (int i = 0; i < 4; i++) {
                    const bool on_a = i < 2;                 /* 0,1 on face a; 2,3 on face c */
                    const float se = (i == 0 || i == 3) ? -1.0f : 1.0f;
                    q[i].p[e] = se * (half[e] - b);
                    q[i].p[a] = (float)sa * (on_a ? half[a] : half[a] - b);
                    q[i].p[c] = (float)sc * (on_a ? half[c] - b : half[c]);
                    q[i].n[0] = q[i].n[1] = q[i].n[2] = 0.0f;
                    if (on_a) q[i].n[a] = (float)sa; else q[i].n[c] = (float)sc;
                    q[i].uv[0] = se < 0.0f ? pu0 : pu1;
                    q[i].uv[1] = pvm;
                }
                emit_tri(v, n, q[0], q[1], q[2], out);
                emit_tri(v, n, q[0], q[2], q[3], out);
            }
    }
    /* 8 corner triangles. */
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                const float sg[3] = { (float)sx, (float)sy, (float)sz };
                Vtx q[3];
                for (int i = 0; i < 3; i++) {
                    for (int k = 0; k < 3; k++) {
                        q[i].p[k] = sg[k] * (k == i ? half[k] : half[k] - b);
                        q[i].n[k] = k == i ? sg[k] : 0.0f;
                    }
                    q[i].uv[0] = (pu0 + pu1) * 0.5f;
                    q[i].uv[1] = pvm;
                }
                emit_tri(v, n, q[0], q[1], q[2], sg);
            }
    /* Back: edge color from the atlas, or a full separate texture. */
    emit_face(v, n, NZ, NX, PY, hz, hx, hy, b, pu0, pv0, pu1, pv1);
    emit_face(v, n, NZ, NX, PY, hz, hx, hy, b, 0.0f, 0.0f, 1.0f, 1.0f);
}

static void build_box(void) {
    static Vtx vtx[BOXSHAPE_COUNT * SHAPE_VERTS];
    int n = 0;
    for (int i = 0; i < BOXSHAPE_COUNT; i++) {
        s_shape_first[i] = n;
        build_shape(vtx, &n, boxshape_get(i));
    }

    gl_GenVertexArrays(1, &s_box_vao);
    gl_BindVertexArray(s_box_vao);
    gl_GenBuffers(1, &s_box_vbo);
    gl_BindBuffer(GL_ARRAY_BUFFER, s_box_vbo);
    gl_BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vtx) * (size_t)n), vtx, GL_STATIC_DRAW);
    setup_attribs();
    gl_BindVertexArray(0);

    /* Unit quad (triangle strip), v = 0 at the bottom. */
    Vtx q[4] = {
        {{-0.5f, -0.5f, 0.0f}, {0, 0, 1}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {0, 0, 1}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, 0.0f}, {0, 0, 1}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f, 0.0f}, {0, 0, 1}, {1.0f, 1.0f}},
    };
    gl_GenVertexArrays(1, &s_quad_vao);
    gl_BindVertexArray(s_quad_vao);
    gl_GenBuffers(1, &s_quad_vbo);
    gl_BindBuffer(GL_ARRAY_BUFFER, s_quad_vbo);
    gl_BufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
    setup_attribs();
    gl_BindVertexArray(0);
    gl_BindBuffer(GL_ARRAY_BUFFER, 0);
}

bool scene3d_init(void) {
    if (s_ready) return true;
    clear_gl_errors();
    s_box_prog = link(BOX_VS, BOX_FS);
    s_fx_prog  = link(FX_VS, FX_FS);
    if (!s_box_prog || !s_fx_prog) {
        fprintf(stderr, "scene3d: shader setup failed — 3D shelf unavailable\n");
        scene3d_shutdown();
        return false;
    }
    s_bl.mvp     = gl_GetUniformLocation(s_box_prog, "u_mvp");
    s_bl.model   = gl_GetUniformLocation(s_box_prog, "u_model");
    s_bl.color   = gl_GetUniformLocation(s_box_prog, "u_color");
    s_bl.bright  = gl_GetUniformLocation(s_box_prog, "u_bright");
    s_bl.spec    = gl_GetUniformLocation(s_box_prog, "u_spec");
    s_bl.shine   = gl_GetUniformLocation(s_box_prog, "u_shine");
    s_bl.alpha   = gl_GetUniformLocation(s_box_prog, "u_alpha");
    s_bl.eye     = gl_GetUniformLocation(s_box_prog, "u_eye");
    s_bl.light   = gl_GetUniformLocation(s_box_prog, "u_light");
    s_bl.reflect = gl_GetUniformLocation(s_box_prog, "u_reflect");
    s_bl.tex     = gl_GetUniformLocation(s_box_prog, "u_tex");
    s_fl.mvp     = gl_GetUniformLocation(s_fx_prog, "u_mvp");
    s_fl.screen  = gl_GetUniformLocation(s_fx_prog, "u_screen");
    s_fl.mode    = gl_GetUniformLocation(s_fx_prog, "u_mode");
    s_fl.color   = gl_GetUniformLocation(s_fx_prog, "u_color");
    s_fl.p0      = gl_GetUniformLocation(s_fx_prog, "u_p0");
    s_fl.p1      = gl_GetUniformLocation(s_fx_prog, "u_p1");
    s_fl.tex     = gl_GetUniformLocation(s_fx_prog, "u_tex");
    gl_UseProgram(s_box_prog);
    if (s_bl.tex >= 0) gl_Uniform1i(s_bl.tex, 0);
    gl_UseProgram(s_fx_prog);
    if (s_fl.tex >= 0) gl_Uniform1i(s_fl.tex, 0);
    gl_UseProgram(0);

    build_box();

    const unsigned char white[4] = { 255, 255, 255, 255 };
    s_white_tex = scene3d_texture_rgba(white, 1, 1, false);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "scene3d: GL error 0x%x during setup — 3D shelf unavailable\n", (unsigned)err);
        scene3d_shutdown();
        return false;
    }

    /* Anisotropic filtering keeps the steeply angled spines legible. */
    clear_gl_errors();
    float maxa = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxa);
    s_aniso = (glGetError() == GL_NO_ERROR && maxa >= 2.0f) ? fminf(maxa, 8.0f) : 0.0f;

    /* Key light from the upper left, in front of the boxes. */
    float lx = -0.45f, ly = 0.55f, lz = 0.70f;
    float ll = sqrtf(lx * lx + ly * ly + lz * lz);
    s_light[0] = lx / ll; s_light[1] = ly / ll; s_light[2] = lz / ll;

    s_view = m4_identity();
    s_proj = m4_identity();
    s_vpm  = m4_identity();
    s_ready = 1;
    return true;
}

static void destroy_targets(void) {
    if (s_fbo_ms)        { gl_DeleteFramebuffers(1, &s_fbo_ms);        s_fbo_ms = 0; }
    if (s_rb_ms_color)   { gl_DeleteRenderbuffers(1, &s_rb_ms_color);  s_rb_ms_color = 0; }
    if (s_rb_ms_depth)   { gl_DeleteRenderbuffers(1, &s_rb_ms_depth);  s_rb_ms_depth = 0; }
    if (s_fbo_res)       { gl_DeleteFramebuffers(1, &s_fbo_res);       s_fbo_res = 0; }
    if (s_tex_res)       { glDeleteTextures(1, &s_tex_res);            s_tex_res = 0; }
    if (s_rb_res_depth)  { gl_DeleteRenderbuffers(1, &s_rb_res_depth); s_rb_res_depth = 0; }
    s_target_ok = 0;
    s_tw = s_th = 0;
    s_samples = 0;
}

static bool create_targets(int w, int h, int samples) {
    clear_gl_errors();
    glGenTextures(1, &s_tex_res);
    glBindTexture(GL_TEXTURE_2D, s_tex_res);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    gl_GenFramebuffers(1, &s_fbo_res);
    gl_BindFramebuffer(GL_FRAMEBUFFER, s_fbo_res);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_tex_res, 0);
    if (samples == 0) {
        gl_GenRenderbuffers(1, &s_rb_res_depth);
        gl_BindRenderbuffer(GL_RENDERBUFFER, s_rb_res_depth);
        gl_RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        gl_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_rb_res_depth);
    }
    bool ok = gl_CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    if (ok && samples > 0) {
        gl_GenRenderbuffers(1, &s_rb_ms_color);
        gl_BindRenderbuffer(GL_RENDERBUFFER, s_rb_ms_color);
        gl_RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
        gl_GenRenderbuffers(1, &s_rb_ms_depth);
        gl_BindRenderbuffer(GL_RENDERBUFFER, s_rb_ms_depth);
        gl_RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
        gl_GenFramebuffers(1, &s_fbo_ms);
        gl_BindFramebuffer(GL_FRAMEBUFFER, s_fbo_ms);
        gl_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_rb_ms_color);
        gl_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_rb_ms_depth);
        ok = gl_CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    gl_BindRenderbuffer(GL_RENDERBUFFER, 0);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (glGetError() != GL_NO_ERROR) ok = false;
    if (!ok) { destroy_targets(); return false; }
    s_tw = w; s_th = h; s_samples = samples; s_target_ok = 1;
    return true;
}

static void ensure_targets(int w, int h) {
    if (s_target_ok && s_tw == w && s_th == h) return;
    if (!s_target_ok && s_fail_w == w && s_fail_h == h) return;   /* don't retry every frame */
    destroy_targets();
    GLint maxs = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxs);
    clear_gl_errors();
    int want = EL_MSAA < maxs ? EL_MSAA : (int)maxs;
    if (want >= 2 && create_targets(w, h, want)) return;
    if (create_targets(w, h, 0)) {
        fprintf(stderr, "scene3d: no MSAA target, rendering without anti-aliasing\n");
        return;
    }
    fprintf(stderr, "scene3d: offscreen target unavailable, drawing directly\n");
    s_fail_w = w; s_fail_h = h;
}

void scene3d_release_targets(void) {
    destroy_targets();
    s_fail_w = s_fail_h = -1;
}

void scene3d_shutdown(void) {
    destroy_targets();
    if (s_box_prog)  { gl_DeleteProgram(s_box_prog); s_box_prog = 0; }
    if (s_fx_prog)   { gl_DeleteProgram(s_fx_prog);  s_fx_prog = 0; }
    if (s_box_vbo)   { gl_DeleteBuffers(1, &s_box_vbo);  s_box_vbo = 0; }
    if (s_quad_vbo)  { gl_DeleteBuffers(1, &s_quad_vbo); s_quad_vbo = 0; }
    if (s_box_vao)   { gl_DeleteVertexArrays(1, &s_box_vao);  s_box_vao = 0; }
    if (s_quad_vao)  { gl_DeleteVertexArrays(1, &s_quad_vao); s_quad_vao = 0; }
    if (s_white_tex) { glDeleteTextures(1, &s_white_tex); s_white_tex = 0; }
    s_ready = 0;
}

/* ── Textures ──────────────────────────────────────────────────────────── */
unsigned scene3d_texture_rgba(const unsigned char *rgba, int w, int h, bool mipmaps) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (mipmaps) {
        gl_GenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        if (s_aniso > 0.0f)
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, s_aniso);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return (unsigned)t;
}

void scene3d_texture_free(unsigned tex) {
    GLuint t = (GLuint)tex;
    if (t) glDeleteTextures(1, &t);
}

/* ── Frame ─────────────────────────────────────────────────────────────── */
void scene3d_begin(void) {
    glGetIntegerv(GL_VIEWPORT, s_vp);
    int w = s_vp[2], h = s_vp[3];
    if (w > MAX_TARGET_W) { h = (int)((long)h * MAX_TARGET_W / w); w = MAX_TARGET_W; }
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    ensure_targets(w, h);
    if (s_target_ok) {
        gl_BindFramebuffer(GL_FRAMEBUFFER, s_samples ? s_fbo_ms : s_fbo_res);
        glViewport(0, 0, s_tw, s_th);
    }
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthFunc(GL_LEQUAL);
}

static void fx_quad(int screen, int mode, const Mat4 *model, const float color[4],
                    const float p0[4], const float p1[4], GLuint tex) {
    gl_UseProgram(s_fx_prog);
    if (!screen && model) {
        Mat4 mvp = m4_mul(s_vpm, *model);
        gl_UniformMatrix4fv(s_fl.mvp, 1, GL_FALSE, mvp.m);
    }
    gl_Uniform1i(s_fl.screen, screen);
    gl_Uniform1i(s_fl.mode, mode);
    gl_Uniform4fv(s_fl.color, 1, color);
    if (p0) gl_Uniform4fv(s_fl.p0, 1, p0);
    if (p1) gl_Uniform4fv(s_fl.p1, 1, p1);
    gl_ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex ? tex : s_white_tex);
    gl_BindVertexArray(s_quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void scene3d_end(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glDepthMask(GL_TRUE);
    if (s_target_ok) {
        if (s_samples) {
            gl_BindFramebuffer(GL_READ_FRAMEBUFFER, s_fbo_ms);
            gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, s_fbo_res);
            gl_BlitFramebuffer(0, 0, s_tw, s_th, 0, 0, s_tw, s_th, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(s_vp[0], s_vp[1], s_vp[2], s_vp[3]);
        glDisable(GL_BLEND);
        const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        fx_quad(1, 0, NULL, white, NULL, NULL, s_tex_res);
    }
    gl_BindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    gl_UseProgram(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

void scene3d_set_camera(const Mat4 *view, const Mat4 *proj, const float eye[3]) {
    s_view = *view;
    s_proj = *proj;
    s_vpm  = m4_mul(s_proj, s_view);
    s_eye[0] = eye[0]; s_eye[1] = eye[1]; s_eye[2] = eye[2];
}

/* ── Boxes ─────────────────────────────────────────────────────────────── */
static void box_uniforms(const Mat4 *model, const BoxMaterial *mat, float alpha,
                         float refl, float floor_y, float fade) {
    Mat4 mvp = m4_mul(s_vpm, *model);
    gl_UseProgram(s_box_prog);
    gl_UniformMatrix4fv(s_bl.mvp, 1, GL_FALSE, mvp.m);
    gl_UniformMatrix4fv(s_bl.model, 1, GL_FALSE, model->m);
    if (mat->atlas) gl_Uniform3f(s_bl.color, 1.0f, 1.0f, 1.0f);
    else            gl_Uniform3f(s_bl.color, mat->color[0], mat->color[1], mat->color[2]);
    gl_Uniform1f(s_bl.bright, mat->brightness);
    gl_Uniform1f(s_bl.spec, mat->spec);
    gl_Uniform1f(s_bl.shine, mat->shine > 1.0f ? mat->shine : 1.0f);
    gl_Uniform1f(s_bl.alpha, alpha);
    gl_Uniform3f(s_bl.eye, s_eye[0], s_eye[1], s_eye[2]);
    gl_Uniform3f(s_bl.light, s_light[0], s_light[1], s_light[2]);
    gl_Uniform3f(s_bl.reflect, refl, floor_y, fade);
}

static void box_geometry(const BoxMaterial *mat) {
    const int shape = (mat->shape >= 0 && mat->shape < BOXSHAPE_COUNT) ? mat->shape : 0;
    const int first = s_shape_first[shape];
    gl_ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mat->atlas ? mat->atlas : s_white_tex);
    gl_BindVertexArray(s_box_vao);
    if (mat->atlas && mat->back) {
        glDrawArrays(GL_TRIANGLES, first, MAIN_VERTS);
        glBindTexture(GL_TEXTURE_2D, mat->back);
        glDrawArrays(GL_TRIANGLES, first + MAIN_VERTS + 6, 6);
    } else {
        glDrawArrays(GL_TRIANGLES, first, MAIN_VERTS + 6);
    }
}

void scene3d_material_for_shape(BoxMaterial *mat) {
    const BoxShape *sh = boxshape_get(mat->shape);
    if (sh->style == BOXSTYLE_CASE) { mat->spec = 0.38f; mat->shine = 70.0f; }   /* glossy plastic */
    else                            { mat->spec = 0.07f; mat->shine = 9.0f;  }   /* printed cardboard */
}

void scene3d_draw_box(const Mat4 *model, const BoxMaterial *mat) {
    if (!s_ready) return;
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    box_uniforms(model, mat, 1.0f, 0.0f, 0.0f, 1.0f);
    box_geometry(mat);
}

void scene3d_draw_box_reflection(const Mat4 *model, const BoxMaterial *mat,
                                 float floor_y, float strength, float fade) {
    if (!s_ready || strength <= 0.0f) return;
    /* Mirror across the floor plane: y' = 2*floor_y - y (flips winding). */
    Mat4 mirror = m4_mul(m4_translate(0.0f, 2.0f * floor_y, 0.0f), m4_scale(1.0f, -1.0f, 1.0f));
    Mat4 m = m4_mul(mirror, *model);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    box_uniforms(&m, mat, strength, 1.0f, floor_y, fade);
    box_geometry(mat);
    glFrontFace(GL_CCW);
}

void scene3d_draw_box_glow(const Mat4 *model, int shape, const float rgb[3], float strength,
                           float margin) {
    if (!s_ready || strength <= 0.0f) return;
    const BoxShape *sh = boxshape_get(shape);
    float qw = sh->w + 2.0f * margin, qh = sh->h + 2.0f * margin;
    Mat4 m = m4_mul(*model, m4_mul(m4_translate(0.0f, 0.0f, -sh->d * 0.5f - 0.004f),
                                   m4_scale(qw, qh, 1.0f)));
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    const float color[4] = { rgb[0], rgb[1], rgb[2], strength };
    const float p0[4] = { sh->w * 0.5f, sh->h * 0.5f, margin, 0.04f + sh->bevel };
    const float p1[4] = { qw, qh, 0.0f, 0.0f };
    fx_quad(0, 3, &m, color, p0, p1, 0);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* ── Stage ─────────────────────────────────────────────────────────────── */
void scene3d_draw_backdrop(float cx, float cy, float rx, float ry,
                           const float spot_rgb[3], float spot_a, float vignette) {
    if (!s_ready) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    const float color[4] = { spot_rgb[0], spot_rgb[1], spot_rgb[2], spot_a };
    const float p0[4] = { cx, cy, rx, ry };
    const float p1[4] = { vignette, 0.0f, 0.0f, 0.0f };
    fx_quad(1, 4, NULL, color, p0, p1, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void scene3d_draw_floor(float floor_y, const float rgb[3], float alpha) {
    if (!s_ready) return;
    const float sx = 40.0f, sz = 24.0f, zc = -6.0f;
    Mat4 m = m4_mul(m4_translate(0.0f, floor_y, zc),
                    m4_mul(m4_rot_x(-1.5707963f), m4_scale(sx, sz, 1.0f)));
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const float color[4] = { rgb[0], rgb[1], rgb[2], alpha };
    /* Fade centered near the front of the stage (quad-local y = -world z). */
    const float p0[4] = { 0.0f, -(0.8f - zc), 3.0f, 10.5f };
    const float p1[4] = { sx, sz, 0.0f, 0.0f };
    fx_quad(0, 2, &m, color, p0, p1, 0);
    glDepthMask(GL_TRUE);
}

void scene3d_draw_shadow(float x, float z, float yaw, float floor_y,
                         float half_w, float half_d, float alpha) {
    if (!s_ready || alpha <= 0.0f) return;
    Mat4 m = m4_mul(m4_translate(x, floor_y + 0.003f, z),
                    m4_mul(m4_rot_y(yaw),
                           m4_mul(m4_rot_x(-1.5707963f), m4_scale(half_w * 2.0f, half_d * 2.0f, 1.0f))));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const float color[4] = { 0.0f, 0.0f, 0.0f, alpha };
    const float p0[4] = { 0.15f, 0.0f, 0.0f, 0.0f };
    fx_quad(0, 1, &m, color, p0, NULL, 0);
    glDepthMask(GL_TRUE);
}
