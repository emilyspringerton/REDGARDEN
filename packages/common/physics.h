#ifndef PHYSICS_H
#define PHYSICS_H

#include <math.h>

static inline float clampf(float v, float min_v, float max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static inline int clampi(int v, int min_v, int max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static inline float dist2(float ax, float az, float bx, float bz) {
    float dx = ax - bx;
    float dz = az - bz;
    return (dx * dx) + (dz * dz);
}

#endif
