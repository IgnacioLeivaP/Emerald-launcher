#pragma once
/* Tiny column-major 4x4 matrix helpers (OpenGL conventions), header-only so
   both the C renderer and the C++ launcher can use them. m[col*4 + row]. */
#include <math.h>

typedef struct { float m[16]; } Mat4;

static inline Mat4 m4_identity(void) {
    Mat4 r = {{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }};
    return r;
}

/* r = a * b  (b is applied first) */
static inline Mat4 m4_mul(Mat4 a, Mat4 b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}

static inline Mat4 m4_translate(float x, float y, float z) {
    Mat4 r = m4_identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

static inline Mat4 m4_scale(float x, float y, float z) {
    Mat4 r = m4_identity();
    r.m[0] = x; r.m[5] = y; r.m[10] = z;
    return r;
}

static inline Mat4 m4_rot_x(float a) {
    Mat4 r = m4_identity();
    float c = cosf(a), s = sinf(a);
    r.m[5] = c;  r.m[6] = s;
    r.m[9] = -s; r.m[10] = c;
    return r;
}

/* Positive angle turns +Z toward +X. */
static inline Mat4 m4_rot_y(float a) {
    Mat4 r = m4_identity();
    float c = cosf(a), s = sinf(a);
    r.m[0] = c;  r.m[2] = -s;
    r.m[8] = s;  r.m[10] = c;
    return r;
}

static inline Mat4 m4_rot_z(float a) {
    Mat4 r = m4_identity();
    float c = cosf(a), s = sinf(a);
    r.m[0] = c;  r.m[1] = s;
    r.m[4] = -s; r.m[5] = c;
    return r;
}

static inline Mat4 m4_perspective(float fovy, float aspect, float zn, float zf) {
    Mat4 r = {{0}};
    float f = 1.0f / tanf(fovy * 0.5f);
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * zf * zn / (zn - zf);
    return r;
}

static inline Mat4 m4_look_at(float ex, float ey, float ez,
                              float cx, float cy, float cz,
                              float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;
    float vx = sy * fz - sz * fy, vy = sz * fx - sx * fz, vz = sx * fy - sy * fx;
    Mat4 r = m4_identity();
    r.m[0] = sx;  r.m[4] = sy;  r.m[8]  = sz;
    r.m[1] = vx;  r.m[5] = vy;  r.m[9]  = vz;
    r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
    r.m[12] = -(sx * ex + sy * ey + sz * ez);
    r.m[13] = -(vx * ex + vy * ey + vz * ez);
    r.m[14] =  (fx * ex + fy * ey + fz * ez);
    return r;
}

/* out = m * (x, y, z, 1) */
static inline void m4_apply(const Mat4 *m, float x, float y, float z, float out[4]) {
    for (int rw = 0; rw < 4; rw++)
        out[rw] = m->m[rw] * x + m->m[4 + rw] * y + m->m[8 + rw] * z + m->m[12 + rw];
}
