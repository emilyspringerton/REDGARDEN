#include "arena_game.h"
#include <math.h>
#include <string.h>

ArenaState arena_state;

/* ---- Tiny hand-authored feed-forward "brain" for the bot hero ----
 * Same shape as SHANKPIT's bot brain (packages/simulation/neural_net.h,
 * dense_layer(): out = activation(W*in + b)) rather than a copy of it --
 * SHANKPIT's net is trained (PyTorch-exported weights in brain_weights.h)
 * against FPS-specific inputs (yaw/pitch/strafe/shoot) that don't exist in
 * this top-down click-to-move arena. This is the same forward-pass
 * mechanism (dense layer -> ReLU -> dense layer -> Tanh) re-sized for this
 * game's inputs/outputs, with hand-picked (not trained) weights -- there's
 * no training pipeline wired up here yet. Real training data/pipeline is a
 * fast-follow; this is the honest "or equivalent" for tonight.
 */
static float dense_relu(const float *in, const float *w, const float *b, int i, int in_size) {
    float sum = b[i];
    for (int j = 0; j < in_size; j++) sum += in[j] * w[i * in_size + j];
    return sum > 0.0f ? sum : 0.0f;
}

/* inputs: [dx_norm, dz_norm, dist_norm, hp_frac_diff] */
static void bot_brain_forward(const float in[4], float out[2]) {
    /* Layer 1: 4 -> 6, ReLU. Neurons 0-3 split dx/dz into +/- halves so the
       output layer can recombine them with relu(x)-relu(-x) == x -- i.e.
       the net's steering output reduces to "turn toward the target,"
       computed through the same layered structure as a trained net would
       use. Neurons 4-5 carry distance/hp-diff signal, wired in with zero
       output weight for now -- left as the hook a future trained pass
       would use for kiting/retreat behavior. */
    static const float w1[6 * 4] = {
        1, 0, 0, 0,
        -1, 0, 0, 0,
        0, 1, 0, 0,
        0, -1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    static const float b1[6] = {0, 0, 0, 0, 0, 0};
    float h[6];
    for (int i = 0; i < 6; i++) h[i] = dense_relu(in, w1, b1, i, 4);

    /* Layer 2: 6 -> 2, Tanh. */
    static const float w2[2 * 6] = {
        1, -1, 0, 0, 0, 0,
        0, 0, 1, -1, 0, 0,
    };
    static const float b2[2] = {0, 0};
    for (int i = 0; i < 2; i++) {
        float sum = b2[i];
        for (int j = 0; j < 6; j++) sum += h[j] * w2[i * 6 + j];
        out[i] = tanhf(sum);
    }
}

void arena_init(void) {
    memset(&arena_state, 0, sizeof(arena_state));

    arena_state.heroes[0].x = -6.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].target_x = -6.0f;
    arena_state.heroes[0].target_z = 0.0f;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].owner = 0;
    arena_state.heroes[0].alive = 1;

    arena_state.heroes[1].x = 6.0f;
    arena_state.heroes[1].z = 0.0f;
    arena_state.heroes[1].target_x = 6.0f;
    arena_state.heroes[1].target_z = 0.0f;
    arena_state.heroes[1].hp = arena_state.heroes[1].max_hp = 100;
    arena_state.heroes[1].owner = 1;
    arena_state.heroes[1].alive = 1;

    arena_state.nodes[0].x = -4.0f;
    arena_state.nodes[0].z = 4.0f;
    arena_state.nodes[1].x = 4.0f;
    arena_state.nodes[1].z = -4.0f;

    arena_state.winner = 0;
}

void arena_set_move_target(int owner, float x, float z) {
    if (owner < 0 || owner > 1) return;
    if (x < -ARENA_HALF_EXTENT) x = -ARENA_HALF_EXTENT;
    if (x > ARENA_HALF_EXTENT) x = ARENA_HALF_EXTENT;
    if (z < -ARENA_HALF_EXTENT) z = -ARENA_HALF_EXTENT;
    if (z > ARENA_HALF_EXTENT) z = ARENA_HALF_EXTENT;
    arena_state.heroes[owner].target_x = x;
    arena_state.heroes[owner].target_z = z;
    arena_state.heroes[owner].moving = 1;
}

void arena_bot_tick(unsigned int dt_ms) {
    (void)dt_ms;
    ArenaHero *bot = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    if (!bot->alive || !foe->alive) return;

    float dx = foe->x - bot->x;
    float dz = foe->z - bot->z;
    float dist = sqrtf(dx * dx + dz * dz);

    float in[4];
    in[0] = dx / ARENA_HALF_EXTENT;
    in[1] = dz / ARENA_HALF_EXTENT;
    in[2] = dist / (ARENA_HALF_EXTENT * 2.0f);
    in[3] = ((float)bot->hp / bot->max_hp) - ((float)foe->hp / foe->max_hp);

    float out[2];
    bot_brain_forward(in, out);

    /* Steer a few units ahead in the net's suggested direction each tick --
       cheap re-evaluation gives continuous chase without full pathfinding. */
    float step = 3.0f;
    arena_set_move_target(1, bot->x + out[0] * step, bot->z + out[1] * step);
}

static void update_hero_motion(ArenaHero *h, float dt_sec) {
    if (!h->alive || !h->moving) return;
    float dx = h->target_x - h->x;
    float dz = h->target_z - h->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.05f) {
        h->moving = 0;
        return;
    }
    float step = ARENA_HERO_SPEED * dt_sec;
    if (step >= dist) {
        h->x = h->target_x;
        h->z = h->target_z;
        h->moving = 0;
    } else {
        h->x += dx / dist * step;
        h->z += dz / dist * step;
    }
}

static void resolve_combat(unsigned int dt_ms) {
    ArenaHero *a = &arena_state.heroes[0];
    ArenaHero *b = &arena_state.heroes[1];
    if (a->attack_cooldown_ms > 0) a->attack_cooldown_ms -= (int)dt_ms;
    if (b->attack_cooldown_ms > 0) b->attack_cooldown_ms -= (int)dt_ms;
    if (!a->alive || !b->alive) return;

    float dx = b->x - a->x;
    float dz = b->z - a->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > ARENA_ATTACK_RANGE) return;

    if (a->attack_cooldown_ms <= 0) {
        b->hp -= ARENA_ATTACK_DAMAGE;
        a->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }
    if (b->attack_cooldown_ms <= 0) {
        a->hp -= ARENA_ATTACK_DAMAGE;
        b->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }
    if (a->hp <= 0) { a->hp = 0; a->alive = 0; }
    if (b->hp <= 0) { b->hp = 0; b->alive = 0; }
}

void arena_update(unsigned int dt_ms) {
    if (arena_state.winner != 0) return;
    float dt_sec = (float)dt_ms / 1000.0f;

    arena_bot_tick(dt_ms);

    /* If the player's hero is close enough to the bot, treat the last
       move-target as an attack-move: keep closing until in range. */
    ArenaHero *a = &arena_state.heroes[0];
    ArenaHero *b = &arena_state.heroes[1];
    if (a->alive && b->alive) {
        float dx = b->x - a->x, dz = b->z - a->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (a->moving && dist <= ARENA_HALF_EXTENT * 4.0f) {
            float tdx = a->target_x - b->x, tdz = a->target_z - b->z;
            if (sqrtf(tdx * tdx + tdz * tdz) < ARENA_ATTACK_RANGE * 3.0f && dist > ARENA_ATTACK_RANGE) {
                a->target_x = b->x;
                a->target_z = b->z;
            }
        }
    }

    update_hero_motion(&arena_state.heroes[0], dt_sec);
    update_hero_motion(&arena_state.heroes[1], dt_sec);
    resolve_combat(dt_ms);

    if (!arena_state.heroes[0].alive) arena_state.winner = 2;
    else if (!arena_state.heroes[1].alive) arena_state.winner = 1;
}
