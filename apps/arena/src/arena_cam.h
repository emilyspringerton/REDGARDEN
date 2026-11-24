/* arena_cam.h -- REDGARDEN arena camera host glue (BACKLOG SECTION 636, UNIFY-1).
 *
 * Thin glue ONLY: every decision (wheel -> zoom arm, easing, first-person flag + hysteresis, recenter
 * on/off, yaw/pitch/focus easing, move-ahead target) is a PARENA mod, compiled to C and checked in as
 * packages/simulation/camera_unify_mod.c (source: PARENA/stdlib/camera (four .prn files)). This header owns the state
 * ints the stateless mods are handed back each frame, converts float <-> the mods' fixed-point ABI
 * (milli = units x1000, mdeg = degrees x1000, permille), and holds the four user options. No SDL, no GL, so
 * tests/test_arena_cam.c exercises the exact same code the game runs, headless.
 *
 * The four third-person options (and nothing else): follow style, recenter speed, terrain adaptation,
 * smoothing. Defaults for REDGARDEN (a MOBA): Free / normal / Off / Off. Wall pull-in is not an option and
 * the arena has no camera collision anyway. First person ignores all four. */
#ifndef ARENA_CAM_H
#define ARENA_CAM_H

#include <math.h>
#include "../../../packages/simulation/camera_unify_mod_host.h"

#define ARENA_CAM_MAX_ARM_MILLI 30000 /* zoom-out limit, same 30 units the old wheel clamp had */
#define ARENA_CAM_START_ARM_MILLI 16000 /* same default cam_dist = 16 as before */
#define ARENA_CAM_EYE_HEIGHT 1.4f /* first-person eye height above the ground plane */
#define ARENA_CAM_EYE_RAMP_MILLI 6000 /* the eye rises from 0 to full height as the arm closes from 6 units to 0 */
#define ARENA_CAM_LOOK_DEG_PER_PX 0.3f /* same sensitivity as the pre-existing right-drag orbit */
#define ARENA_CAM_PITCH_MIN_3P 10.0f
#define ARENA_CAM_PITCH_MAX 80.0f
#define ARENA_CAM_PITCH_MIN_FP (-80.0f)
#define ARENA_CAM_PITCH_EASE_TAU_MS 140 /* leaving first person: look-up pitch eases back to the orbit floor */

enum { ARENA_CAM_OPT_FOLLOW = 0, ARENA_CAM_OPT_RECENTER, ARENA_CAM_OPT_TERRAIN, ARENA_CAM_OPT_SMOOTH, ARENA_CAM_OPT_COUNT };

typedef struct {
    /* zoom */
    int arm_milli, target_milli, fp;
    /* the four options: follow 0 Free/1 Behind when moving/2 Always behind; recenter 0 slow/1 normal/2 fast;
       terrain 0 Off/1 Slope; smoothing 0 off/1 low/2 high */
    int opt[ARENA_CAM_OPT_COUNT];
    /* follow state */
    int idle_ms, speed_milli_per_s;
    int pitch_off_mdeg;
    int focus_x_milli, focus_z_milli, focus_y_milli;
    int prev_x_milli, prev_z_milli, prev_valid, focus_init;
    /* outputs the renderer / raycaster read */
    float focus_x, focus_y, focus_z; /* orbit focus (y includes the first-person eye-height ramp) */
    float dist, pitch_eff;           /* arm in world units (never 0), pitch with the terrain offset applied */
    int hide_self;                   /* own body is hidden while the arm is shorter than the first-notch-out floor */
} ArenaCam;

typedef struct {
    float hero_x, hero_z, hero_y;
    float facing_rad;     /* hero facing, atan2(dx, dz) convention (hero_facing_rad) */
    int slope_permille;   /* ground rise/run along the view direction; the flat arena passes 0 */
    int manual_input;     /* the player orbited/looked this frame */
    int cam_locked;       /* C toggle: frozen orbit angle -- auto-recenter must not fight it */
} ArenaCamIn;

static inline void arena_cam_init(ArenaCam *c) {
    int i;
    for (i = 0; i < (int)sizeof(*c); i++) ((char *)c)[i] = 0;
    c->arm_milli = c->target_milli = ARENA_CAM_START_ARM_MILLI;
    c->opt[ARENA_CAM_OPT_FOLLOW] = 0;
    c->opt[ARENA_CAM_OPT_RECENTER] = 1;
    c->opt[ARENA_CAM_OPT_TERRAIN] = 0;
    c->opt[ARENA_CAM_OPT_SMOOTH] = 0;
    c->idle_ms = 60000;
    c->dist = ARENA_CAM_START_ARM_MILLI / 1000.0f;
    c->pitch_eff = 40.0f;
}

/* SDL wheel.y: positive = scroll up = zoom in. min 0 makes first person reachable. */
static inline void arena_cam_wheel(ArenaCam *c, int clicks) {
    c->target_milli = zoom_cam_wheel_target_milli(c->target_milli, clicks, 0, 0, ARENA_CAM_MAX_ARM_MILLI);
}

static inline float arena_cam_pitch_min(const ArenaCam *c) { return c->fp ? ARENA_CAM_PITCH_MIN_FP : ARENA_CAM_PITCH_MIN_3P; }

static inline float arena_cam_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Mouse deltas -> yaw/pitch. mouselook (first person, cursor captured): moving right turns the view right
 * (yaw decreases, because the orbit camera sits on the opposite side of the look direction). Otherwise it is
 * the pre-existing right-drag orbit (drag right -> yaw increases). Pitch sign is shared: positive = looking
 * down, so moving the mouse up (dy < 0) looks up. */
static inline void arena_cam_look(const ArenaCam *c, float *yaw, float *pitch, int dx, int dy, int mouselook) {
    *yaw += (mouselook ? -(float)dx : (float)dx) * ARENA_CAM_LOOK_DEG_PER_PX;
    *pitch = arena_cam_clampf(*pitch + (float)dy * ARENA_CAM_LOOK_DEG_PER_PX, arena_cam_pitch_min(c), ARENA_CAM_PITCH_MAX);
}

static inline int arena_cam_iround(float v) { return (int)(v < 0.0f ? v - 0.5f : v + 0.5f); }

/* One frame. Updates zoom arm / first-person flag, and (third person only) the auto-recenter yaw, the
 * terrain pitch offset + focus height, and the smoothed focus. yaw/pitch are the host's own orbit angles. */
static inline void arena_cam_step(ArenaCam *c, int dt_ms, const ArenaCamIn *in, float *yaw_deg, float *pitch_deg) {
    int hx = arena_cam_iround(in->hero_x * 1000.0f), hz = arena_cam_iround(in->hero_z * 1000.0f);
    int hy = arena_cam_iround(in->hero_y * 1000.0f);
    if (dt_ms < 0) dt_ms = 0;

    c->arm_milli = zoom_cam_ease_arm_milli(c->arm_milli, c->target_milli, dt_ms, zoom_cam_tau_ms());
    c->fp = zoom_cam_is_first_person(c->arm_milli, c->fp);
    c->hide_self = c->arm_milli < zoom_cam_out_floor_milli();

    /* speed from the position delta (a teleport/respawn reads as one fast frame; harmless) */
    c->speed_milli_per_s = 0;
    if (c->prev_valid && dt_ms > 0) {
        float ddx = (float)(hx - c->prev_x_milli), ddz = (float)(hz - c->prev_z_milli);
        c->speed_milli_per_s = (int)(sqrtf(ddx * ddx + ddz * ddz) * 1000.0f / (float)dt_ms);
    }
    c->prev_x_milli = hx; c->prev_z_milli = hz; c->prev_valid = 1;

    if (c->fp) {
        /* first person ignores the follow options: no lag, no recenter, no terrain offset */
        c->focus_x_milli = hx; c->focus_z_milli = hz; c->focus_y_milli = hy;
        c->pitch_off_mdeg = 0;
        c->idle_ms = cam_follow_manual_idle_ms(c->idle_ms, dt_ms, 1); /* entering/leaving counts as manual: no instant snap on exit */
        *pitch_deg = arena_cam_clampf(*pitch_deg, ARENA_CAM_PITCH_MIN_FP, ARENA_CAM_PITCH_MAX);
    } else {
        int manual = in->manual_input;
        int active, y_m, behind_m;
        c->idle_ms = cam_follow_manual_idle_ms(c->idle_ms, dt_ms, manual);
        active = !in->cam_locked && cam_follow_recenter_active(c->opt[ARENA_CAM_OPT_FOLLOW], c->speed_milli_per_s, c->idle_ms, 0);
        if (active) {
            y_m = cam_ease_wrap_mdeg(arena_cam_iround(fmodf(*yaw_deg, 360.0f) * 1000.0f));
            behind_m = cam_ease_wrap_mdeg(arena_cam_iround(fmodf(in->facing_rad * 57.29578f, 360.0f) * 1000.0f) + 180000);
            *yaw_deg = (float)cam_follow_yaw_step_mdeg(y_m, behind_m, dt_ms, c->opt[ARENA_CAM_OPT_RECENTER], 1) / 1000.0f;
        }
        /* terrain: slope -> pitch offset target, eased; the arena has no wall pull-in, so the arm is always whole (1000) */
        c->pitch_off_mdeg = cam_follow_pitch_ease_mdeg(c->pitch_off_mdeg,
            cam_follow_slope_pitch_target_mdeg(c->opt[ARENA_CAM_OPT_TERRAIN], in->slope_permille), dt_ms);
        c->focus_y_milli = c->focus_init ? cam_follow_focus_y_step_milli(c->opt[ARENA_CAM_OPT_TERRAIN], c->focus_y_milli, hy, dt_ms) : hy;
        /* smoothing lags the focus; off returns the target exactly */
        if (!c->focus_init) { c->focus_x_milli = hx; c->focus_z_milli = hz; }
        else {
            c->focus_x_milli = cam_follow_smooth_step_milli(c->opt[ARENA_CAM_OPT_SMOOTH], c->focus_x_milli, hx, dt_ms);
            c->focus_z_milli = cam_follow_smooth_step_milli(c->opt[ARENA_CAM_OPT_SMOOTH], c->focus_z_milli, hz, dt_ms);
        }
        /* leaving first person: the look-up pitch eases back up to the orbit floor instead of snapping */
        if (*pitch_deg < ARENA_CAM_PITCH_MIN_3P) {
            int pm = cam_ease_step(arena_cam_iround(*pitch_deg * 1000.0f), 10000, dt_ms, ARENA_CAM_PITCH_EASE_TAU_MS);
            *pitch_deg = (float)pm / 1000.0f;
        }
        if (*pitch_deg > ARENA_CAM_PITCH_MAX) *pitch_deg = ARENA_CAM_PITCH_MAX;
    }
    c->focus_init = 1;

    c->focus_x = (float)c->focus_x_milli / 1000.0f;
    c->focus_z = (float)c->focus_z_milli / 1000.0f;
    {
        int a = c->arm_milli < ARENA_CAM_EYE_RAMP_MILLI ? c->arm_milli : ARENA_CAM_EYE_RAMP_MILLI;
        float ramp = 1.0f - (float)a / (float)ARENA_CAM_EYE_RAMP_MILLI;
        c->focus_y = (float)c->focus_y_milli / 1000.0f + ARENA_CAM_EYE_HEIGHT * ramp;
    }
    c->dist = (float)c->arm_milli / 1000.0f;
    if (c->dist < 0.01f) c->dist = 0.01f; /* the orbit math divides by the arm length */
    c->pitch_eff = arena_cam_clampf(*pitch_deg + (float)c->pitch_off_mdeg / 1000.0f,
                                    c->fp ? -88.0f : 1.0f, 88.0f);
}

/* First-person WASD -> a stick in the world XZ plane, permille, from the camera yaw. The look direction is
 * -(sin yaw, cos yaw) (the orbit camera sits at +(sin, cos)); right = (-fwd.z, fwd.x). Diagonals normalized. */
static inline void arena_cam_wasd_dir(float yaw_deg, int w, int a, int s, int d, int *dir_x_permille, int *dir_z_permille) {
    float yr = yaw_deg * 0.017453293f;
    float fx = -sinf(yr), fz = -cosf(yr);
    float rx = -fz, rz = fx;
    float fwd = (float)(w - s), str = (float)(d - a);
    float vx = fx * fwd + rx * str, vz = fz * fwd + rz * str;
    float len = sqrtf(vx * vx + vz * vz);
    if (len > 1.0f) { vx /= len; vz /= len; }
    *dir_x_permille = arena_cam_iround(vx * 1000.0f);
    *dir_z_permille = arena_cam_iround(vz * 1000.0f);
}

/* Move-ahead target for first person (ASSUMPTION flagged in the changelog: REDGARDEN is click-to-move, so
 * WASD issues a move target a fixed lookahead ahead of the hero). Returns 0 when the stick is idle. */
static inline int arena_cam_ahead_target(float hero_x, float hero_z, int dir_x_permille, int dir_z_permille, float *tx, float *tz) {
    if (!click_move_ahead_active(dir_x_permille, dir_z_permille)) return 0;
    *tx = (float)click_move_ahead_milli(arena_cam_iround(hero_x * 1000.0f), dir_x_permille) / 1000.0f;
    *tz = (float)click_move_ahead_milli(arena_cam_iround(hero_z * 1000.0f), dir_z_permille) / 1000.0f;
    return 1;
}

/* ---- settings pane rows (the four options, click to cycle) ---- */
static inline int arena_cam_opt_count(int row) {
    static const int n[ARENA_CAM_OPT_COUNT] = {3, 3, 2, 3};
    return (row >= 0 && row < ARENA_CAM_OPT_COUNT) ? n[row] : 1;
}
static inline void arena_cam_opt_cycle(ArenaCam *c, int row) {
    if (row < 0 || row >= ARENA_CAM_OPT_COUNT) return;
    c->opt[row] = (c->opt[row] + 1) % arena_cam_opt_count(row);
}
static inline const char *arena_cam_opt_name(int row) {
    static const char *const n[ARENA_CAM_OPT_COUNT] = {"FOLLOW", "RECENTER SPEED", "TERRAIN", "SMOOTHING"};
    return (row >= 0 && row < ARENA_CAM_OPT_COUNT) ? n[row] : "";
}
static inline const char *arena_cam_opt_label(int row, int v) {
    static const char *const f[3] = {"FREE", "BEHIND WHEN MOVING", "ALWAYS BEHIND"};
    static const char *const r[3] = {"SLOW", "NORMAL", "FAST"};
    static const char *const t[2] = {"OFF", "SLOPE"};
    static const char *const s[3] = {"OFF", "LOW", "HIGH"};
    if (v < 0 || v >= arena_cam_opt_count(row)) return "?";
    switch (row) {
        case ARENA_CAM_OPT_FOLLOW: return f[v];
        case ARENA_CAM_OPT_RECENTER: return r[v];
        case ARENA_CAM_OPT_TERRAIN: return t[v];
        default: return s[v];
    }
}

#endif /* ARENA_CAM_H */
