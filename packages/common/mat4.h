#ifndef MAT4_H
#define MAT4_H

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Column-major 4x4, matching OpenGL's expected layout for glUniformMatrix4fv. */
typedef struct { float m[16]; } Mat4;

static inline Mat4 mat4_identity(void) {
    Mat4 r = {0};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static inline Mat4 mat4_multiply(const Mat4 *a, const Mat4 *b) {
    Mat4 r = {0};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a->m[k * 4 + row] * b->m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

static inline Mat4 mat4_translate(float x, float y, float z) {
    Mat4 r = mat4_identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

static inline Mat4 mat4_scale(float sx, float sy, float sz) {
    Mat4 r = mat4_identity();
    r.m[0] = sx; r.m[5] = sy; r.m[10] = sz;
    return r;
}

static inline Mat4 mat4_perspective(float fov_deg, float aspect, float znear, float zfar) {
    Mat4 r = {0};
    float f = 1.0f / tanf(fov_deg * 0.5f * (float)M_PI / 180.0f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

/* Builds a view matrix orbiting a fixed focus point at (yaw, pitch) degrees, `dist` away. */
static inline Mat4 mat4_orbit_view(float focus_x, float focus_y, float focus_z,
                                    float yaw_deg, float pitch_deg, float dist) {
    float yaw = yaw_deg * (float)M_PI / 180.0f;
    float pitch = pitch_deg * (float)M_PI / 180.0f;

    float eye_x = focus_x + dist * cosf(pitch) * sinf(yaw);
    float eye_y = focus_y + dist * sinf(pitch);
    float eye_z = focus_z + dist * cosf(pitch) * cosf(yaw);

    float fx = focus_x - eye_x, fy = focus_y - eye_y, fz = focus_z - eye_z;
    float flen = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= flen; fy /= flen; fz /= flen;

    float upx = 0.0f, upy = 1.0f, upz = 0.0f;
    /* s = f x up */
    float sx = fy * upz - fz * upy;
    float sy = fz * upx - fx * upz;
    float sz = fx * upy - fy * upx;
    float slen = sqrtf(sx * sx + sy * sy + sz * sz);
    sx /= slen; sy /= slen; sz /= slen;
    /* u = s x f */
    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;

    Mat4 r = mat4_identity();
    r.m[0] = sx; r.m[4] = sy; r.m[8] = sz;
    r.m[1] = ux; r.m[5] = uy; r.m[9] = uz;
    r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
    r.m[12] = -(sx * eye_x + sy * eye_y + sz * eye_z);
    r.m[13] = -(ux * eye_x + uy * eye_y + uz * eye_z);
    r.m[14] = fx * eye_x + fy * eye_y + fz * eye_z;
    return r;
}

#endif
