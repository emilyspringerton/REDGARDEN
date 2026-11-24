/* tests/test_arena_cam.c -- the REDGARDEN arena camera host glue (apps/arena/src/arena_cam.h), driven through
 * the real compiled PARENA camera mods (packages/simulation/camera_unify_mod.c). Headless. Expected numbers are
 * derived by hand in the comments. BACKLOG SECTION 636, UNIFY-1. */
#include "arena_cam.h"
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0, g_checks = 0;
#define CHECK(c, m) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL  %s (line %d)\n", m, __LINE__); } } while (0)
#define NEARF(a, b, tol, m) do { g_checks++; double _a = (a), _b = (b); if (fabs(_a - _b) > (tol)) { g_fail++; \
    printf("FAIL  %s: got %f want %f (line %d)\n", m, _a, _b, __LINE__); } } while (0)

static ArenaCamIn still(void) { ArenaCamIn i = {0}; return i; }
static void run(ArenaCam *c, int ms, ArenaCamIn *in, float *yaw, float *pitch) {
    int t; for (t = 0; t < ms; t += 16) arena_cam_step(c, 16, in, yaw, pitch);
}

static void test_zoom(void) {
    ArenaCam c; ArenaCamIn in = still(); float yaw = 45, pitch = 40; int i;
    arena_cam_init(&c);
    CHECK(c.arm_milli == 16000 && !c.fp && !c.hide_self, "starts at the old 16-unit orbit, third person");
    arena_cam_wheel(&c, 1);
    /* one notch in = 1.15x closer: 16000 * 1000 / 1150 = 13913 */
    NEARF(c.target_milli, 13913, 3, "one notch in");
    run(&c, 2000, &in, &yaw, &pitch);
    NEARF(c.arm_milli, c.target_milli, 2, "eased onto the target (frame-rate independent exp ease)");
    for (i = 0; i < 20; i++) arena_cam_wheel(&c, 1);
    CHECK(c.target_milli == 0, "scrolling all the way in reaches 0 (first person reachable, min is no longer 4)");
    run(&c, 2000, &in, &yaw, &pitch);
    CHECK(c.fp && c.hide_self, "all the way in = first person, own body hidden");
    NEARF(c.dist, 0.01, 1e-6, "arm never 0 for the orbit math");
    NEARF(c.focus_y, ARENA_CAM_EYE_HEIGHT, 1e-3, "eye at full first-person height");
    /* one notch out of 0 jumps to the 2000 floor (clear of the 800 exit line), eases out */
    arena_cam_wheel(&c, -1);
    CHECK(c.target_milli == 2000, "first notch out = 2 units");
    run(&c, 64, &in, &yaw, &pitch);
    CHECK(c.fp, "hysteresis: still first person while the arm is under 0.8");
    run(&c, 2000, &in, &yaw, &pitch);
    CHECK(!c.fp && c.arm_milli == 2000 && !c.hide_self, "third person again at 2 units, body visible");
    for (i = 0; i < 60; i++) arena_cam_wheel(&c, -1);
    CHECK(c.target_milli == 30000, "zoom-out clamps at 30 units");
    arena_cam_wheel(&c, 1);
    NEARF(c.target_milli, 26086, 3, "30000/1.15 = 26087");
}

static void test_look(void) {
    ArenaCam c; float yaw = 0, pitch = 40; arena_cam_init(&c);
    arena_cam_look(&c, &yaw, &pitch, 10, 0, 0);
    NEARF(yaw, 3.0, 1e-4, "drag right 10px: +3 deg (old right-drag orbit)");
    yaw = 0; arena_cam_look(&c, &yaw, &pitch, 10, 0, 1);
    NEARF(yaw, -3.0, 1e-4, "mouselook right 10px: -3 deg (turns the view right)");
    pitch = 40; arena_cam_look(&c, &yaw, &pitch, 0, -1000, 0);
    NEARF(pitch, 10.0, 1e-4, "third person pitch floor 10");
    c.fp = 1; pitch = 0; arena_cam_look(&c, &yaw, &pitch, 0, -1000, 1);
    NEARF(pitch, -80.0, 1e-4, "first person can look up to -80");
    arena_cam_look(&c, &yaw, &pitch, 0, 100000, 1);
    NEARF(pitch, 80.0, 1e-4, "and down to 80");
}

static void test_wasd(void) {
    int x, z; float tx = 0, tz = 0;
    /* yaw 0: look dir -z, right +x */
    arena_cam_wasd_dir(0, 1, 0, 0, 0, &x, &z); CHECK(x == 0 && z == -1000, "W at yaw 0 -> -z");
    arena_cam_wasd_dir(0, 0, 0, 0, 1, &x, &z); CHECK(x == 1000 && z == 0, "D at yaw 0 -> +x");
    arena_cam_wasd_dir(0, 0, 1, 0, 0, &x, &z); CHECK(x == -1000 && z == 0, "A -> -x");
    arena_cam_wasd_dir(0, 0, 0, 1, 0, &x, &z); CHECK(x == 0 && z == 1000, "S -> +z");
    arena_cam_wasd_dir(0, 1, 0, 0, 1, &x, &z); /* W+D: (1,-1)/sqrt2 = (.7071, -.7071) */
    CHECK(abs(x - 707) <= 1 && abs(z + 707) <= 1, "diagonal normalized to 0.707");
    arena_cam_wasd_dir(0, 1, 0, 1, 0, &x, &z); CHECK(x == 0 && z == 0, "W+S cancels");
    arena_cam_wasd_dir(90, 1, 0, 0, 0, &x, &z); CHECK(abs(x + 1000) <= 1 && abs(z) <= 1, "W at yaw 90 -> -x");
    arena_cam_wasd_dir(90, 0, 0, 0, 1, &x, &z); CHECK(abs(x) <= 1 && abs(z + 1000) <= 1, "D at yaw 90 -> -z");
    /* move-ahead: hero (10,5), stick -z => target (10, 5 - 3) = (10, 2) */
    CHECK(arena_cam_ahead_target(10, 5, 0, -1000, &tx, &tz), "stick active");
    NEARF(tx, 10.0, 1e-3, "ahead x"); NEARF(tz, 2.0, 1e-3, "ahead z (3 unit lookahead)");
    CHECK(!arena_cam_ahead_target(10, 5, 0, 0, &tx, &tz), "idle stick: no target");
}

static void test_follow(void) {
    ArenaCam c; ArenaCamIn in; float yaw, pitch; int t;
    /* hero runs +x (facing_rad = atan2(1,0) = 90 deg) at 5 u/s; behind = yaw 270 */
    for (int style = 0; style < 3; style++) {
        arena_cam_init(&c); c.opt[ARENA_CAM_OPT_FOLLOW] = style; c.opt[ARENA_CAM_OPT_RECENTER] = 2;
        in = still(); in.facing_rad = 1.5707963f; yaw = 0; pitch = 40;
        for (t = 0; t < 4000; t += 16) { in.hero_x += 5.0f * 0.016f; arena_cam_step(&c, 16, &in, &yaw, &pitch); }
        if (style == 0) NEARF(yaw, 0.0, 1e-3, "Free: yaw never moves on its own");
        else NEARF(fmod(yaw + 360.0, 360.0), 270.0, 1.0, "recenters behind the runner (shortest arc 0 -> 270)");
    }
    /* Behind when moving: standing still does not recenter; Always behind does */
    arena_cam_init(&c); c.opt[ARENA_CAM_OPT_FOLLOW] = 1; in = still(); in.facing_rad = 1.5707963f; yaw = 0; pitch = 40;
    run(&c, 4000, &in, &yaw, &pitch); NEARF(yaw, 0.0, 1e-3, "style 1 idle: no recenter");
    arena_cam_init(&c); c.opt[ARENA_CAM_OPT_FOLLOW] = 2; yaw = 0;
    run(&c, 4000, &in, &yaw, &pitch); NEARF(fmod(yaw + 360.0, 360.0), 270.0, 1.0, "style 2 idle: recenters");
    /* manual orbit pauses recenter 800 ms: ~500 ms of manual input leaves yaw untouched, recenter resumes after */
    arena_cam_init(&c); c.opt[ARENA_CAM_OPT_FOLLOW] = 2; yaw = 0; in.manual_input = 1;
    run(&c, 500, &in, &yaw, &pitch); NEARF(yaw, 0.0, 1e-3, "manual input wins");
    in.manual_input = 0; run(&c, 640, &in, &yaw, &pitch); NEARF(yaw, 0.0, 1e-3, "still inside the 800 ms pause");
    run(&c, 4000, &in, &yaw, &pitch); NEARF(fmod(yaw + 360.0, 360.0), 270.0, 1.0, "recenters after the pause");
    /* cam_locked freezes it */
    arena_cam_init(&c); c.opt[ARENA_CAM_OPT_FOLLOW] = 2; yaw = 0; in.cam_locked = 1;
    run(&c, 4000, &in, &yaw, &pitch); NEARF(yaw, 0.0, 1e-3, "cam_locked: no auto-recenter");
    /* first person ignores the options entirely */
    arena_cam_init(&c); c.opt[ARENA_CAM_OPT_FOLLOW] = 2; c.opt[ARENA_CAM_OPT_SMOOTH] = 2; in.cam_locked = 0; yaw = 0;
    c.target_milli = 0; run(&c, 2000, &in, &yaw, &pitch); CHECK(c.fp, "in first person");
    {   float y0 = yaw; run(&c, 2000, &in, &yaw, &pitch); NEARF(yaw, y0, 1e-3, "first person: no recenter"); }
    in.hero_x = 50.0f; arena_cam_step(&c, 16, &in, &yaw, &pitch);
    NEARF(c.focus_x, 50.0, 1e-3, "first person: focus is rigid on the hero despite smoothing high");
}

static void test_smooth(void) {
    ArenaCam c; ArenaCamIn in = still(); float yaw = 0, pitch = 40; int m;
    for (m = 0; m < 3; m++) {
        arena_cam_init(&c); c.opt[ARENA_CAM_OPT_SMOOTH] = m;
        arena_cam_step(&c, 16, &in, &yaw, &pitch);
        in.hero_x = 10.0f; arena_cam_step(&c, 16, &in, &yaw, &pitch);
        if (m == 0) NEARF(c.focus_x, 10.0, 1e-3, "smoothing off: exact");
        /* tau 60: 1 - e^(-16/60) = 0.234 -> 2.34 u ; tau 160: 1 - e^(-0.1) = 0.0952 -> 0.95 u */
        if (m == 1) NEARF(c.focus_x, 2.34, 0.1, "low lag after one frame");
        if (m == 2) NEARF(c.focus_x, 0.95, 0.1, "high lag after one frame");
        in.hero_x = 0.0f;
    }
}

static void test_options(void) {
    ArenaCam c; int i; arena_cam_init(&c);
    CHECK(c.opt[0] == 0 && c.opt[1] == 1 && c.opt[2] == 0 && c.opt[3] == 0, "REDGARDEN defaults Free/normal/off/off");
    arena_cam_opt_cycle(&c, 0); arena_cam_opt_cycle(&c, 0); CHECK(c.opt[0] == 2, "follow cycles 0->1->2");
    arena_cam_opt_cycle(&c, 0); CHECK(c.opt[0] == 0, "wraps");
    arena_cam_opt_cycle(&c, 2); arena_cam_opt_cycle(&c, 2); CHECK(c.opt[2] == 0, "terrain is 2-valued");
    CHECK(ARENA_CAM_OPT_COUNT == 4, "exactly four options");
    for (i = 0; i < 4; i++) { CHECK(arena_cam_opt_label(i, 0)[0] != '?', "label"); CHECK(arena_cam_opt_label(i, arena_cam_opt_count(i))[0] == '?', "oob label"); }
    /* terrain on a flat arena changes nothing; on a slope it offsets pitch */
    {   ArenaCamIn in = still(); float yaw = 0, pitch = 40;
        c.opt[ARENA_CAM_OPT_TERRAIN] = 1; run(&c, 2000, &in, &yaw, &pitch);
        NEARF(c.pitch_eff, 40.0, 1e-3, "flat ground: no pitch offset");
        /* 200 permille uphill: (200-60) * 22 = 3080 mdeg magnitude, negative uphill => 40 - 3.08 */
        in.slope_permille = 200; run(&c, 3000, &in, &yaw, &pitch);
        NEARF(c.pitch_eff, 36.92, 0.1, "uphill lowers the camera pitch by ~3.08 deg"); }
    /* leaving first person: pitch below the orbit floor eases back, never snaps */
    {   ArenaCamIn in = still(); float yaw = 0, pitch = -40; arena_cam_init(&c);
        arena_cam_step(&c, 16, &in, &yaw, &pitch);
        CHECK(pitch > -40.0f && pitch < 10.0f, "eases toward 10 rather than snapping");
        run(&c, 2000, &in, &yaw, &pitch); NEARF(pitch, 10.0, 0.05, "reaches the orbit floor"); }
}

int main(void) {
    test_zoom(); test_look(); test_wasd(); test_follow(); test_smooth(); test_options();
    if (g_fail) { printf("test_arena_cam: %d of %d FAILED\n", g_fail, g_checks); return 1; }
    printf("test_arena_cam: ok: %d checks, 0 failed\n", g_checks);
    return 0;
}
