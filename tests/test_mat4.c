/* tests/test_mat4.c — headless numeric check for packages/common/mat4.h.
 * No SDL/GL dependency, same reasoning as test_arena_game.c: this box has
 * no display, but the math underneath the renderer has no display
 * dependency and is fully checkable without one. Written specifically
 * because a manual read-through of apps/arena/src/main.c already caught
 * one dead-code camera bug tonight -- numeric checks catch more than
 * reading can. */
#include <stdio.h>
#include <math.h>

#include "../packages/common/mat4.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static int nearly(float a, float b) { return fabsf(a - b) < 0.001f; }

static void test_identity(void) {
    Mat4 id = mat4_identity();
    int ok = 1;
    for (int i = 0; i < 16; i++) {
        float expect = (i % 5 == 0) ? 1.0f : 0.0f; /* diagonal in column-major 4x4 */
        if (!nearly(id.m[i], expect)) ok = 0;
    }
    CHECK(ok, "identity matrix has 1s on the diagonal, 0s elsewhere");
}

static void test_translate_transforms_point(void) {
    Mat4 t = mat4_translate(1.0f, 2.0f, 3.0f);
    /* Manually transform point (0,0,0,1) by t: result should be (1,2,3,1). */
    float px = t.m[12], py = t.m[13], pz = t.m[14], pw = t.m[15];
    CHECK(nearly(px, 1.0f) && nearly(py, 2.0f) && nearly(pz, 3.0f) && nearly(pw, 1.0f),
          "translate matrix moves the origin to (1,2,3)");
}

static void test_multiply_identity_is_noop(void) {
    Mat4 t = mat4_translate(5.0f, -2.0f, 9.0f);
    Mat4 id = mat4_identity();
    Mat4 r1 = mat4_multiply(&t, &id);
    Mat4 r2 = mat4_multiply(&id, &t);
    int ok = 1;
    for (int i = 0; i < 16; i++) {
        if (!nearly(r1.m[i], t.m[i]) || !nearly(r2.m[i], t.m[i])) ok = 0;
    }
    CHECK(ok, "multiplying by identity on either side is a no-op");
}

static void test_multiply_order_translate_then_scale(void) {
    /* (translate * scale) applied to (1,1,1,1) should scale first, then
       translate -- i.e. the scale's effect on the point happens in the
       object's local space before the translation moves it. */
    Mat4 tr = mat4_translate(10.0f, 0.0f, 0.0f);
    Mat4 sc = mat4_scale(2.0f, 2.0f, 2.0f);
    Mat4 model = mat4_multiply(&tr, &sc);
    /* Transform point (1,1,1,1) by model manually (column-major: out = M * v). */
    float v[4] = {1, 1, 1, 1};
    float out[4];
    for (int row = 0; row < 4; row++) {
        float sum = 0;
        for (int col = 0; col < 4; col++) sum += model.m[col * 4 + row] * v[col];
        out[row] = sum;
    }
    CHECK(nearly(out[0], 12.0f) && nearly(out[1], 2.0f) && nearly(out[2], 2.0f),
          "translate*scale scales in local space, then translates -- (1,1,1) -> (12,2,2)");
}

static void test_perspective_projects_far_point_behind_near_point(void) {
    Mat4 p = mat4_perspective(60.0f, 1.0f, 0.1f, 100.0f);
    /* For a point on the -Z axis (in front of the camera), further points
       should map to a larger normalized depth (z/w) than nearer ones,
       preserving depth ordering after the perspective divide. */
    float near_z = -1.0f, far_z = -50.0f;
    float near_clip_z = p.m[10] * near_z + p.m[14];
    float near_clip_w = p.m[11] * near_z;
    float far_clip_z = p.m[10] * far_z + p.m[14];
    float far_clip_w = p.m[11] * far_z;
    float near_ndc = near_clip_z / near_clip_w;
    float far_ndc = far_clip_z / far_clip_w;
    CHECK(far_ndc > near_ndc, "perspective matrix preserves depth ordering after the divide");
}

static void test_orbit_view_places_eye_at_expected_distance(void) {
    Mat4 v = mat4_orbit_view(0, 0, 0, 0.0f, 0.0f, 10.0f);
    /* At yaw=0, pitch=0, the eye sits at (0,0,10) looking toward the
       origin -- so transforming the origin by this view matrix should
       place it at (0,0,-10) in view space (in front of the camera, at
       the orbit distance). */
    float v_origin_z = v.m[14]; /* translation component for the origin */
    CHECK(nearly(v_origin_z, -10.0f),
          "orbit view at yaw=0/pitch=0 places the focus 10 units in front of the eye");
}

int main(void) {
    printf("RED GARDEN mat4 headless numeric test\n\n");
    test_identity();
    test_translate_transforms_point();
    test_multiply_identity_is_noop();
    test_multiply_order_translate_then_scale();
    test_perspective_projects_far_point_behind_near_point();
    test_orbit_view_places_eye_at_expected_distance();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
