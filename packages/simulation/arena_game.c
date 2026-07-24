#include "arena_game.h"
#include <math.h>
#include <string.h>

ArenaState arena_state;
int arena_bot_enabled = 1;

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

void arena_init_with_heroes(ArenaHeroID player_hero, ArenaHeroID bot_hero) {
    memset(&arena_state, 0, sizeof(arena_state));

    arena_state.heroes[0].x = -6.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].target_x = -6.0f;
    arena_state.heroes[0].target_z = 0.0f;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].owner = 0;
    arena_state.heroes[0].alive = 1;
    arena_state.heroes[0].active = 1;
    arena_state.heroes[0].team = 0;
    arena_state.heroes[0].hero_id = player_hero;

    arena_state.heroes[1].x = 6.0f;
    arena_state.heroes[1].z = 0.0f;
    arena_state.heroes[1].target_x = 6.0f;
    arena_state.heroes[1].target_z = 0.0f;
    arena_state.heroes[1].hp = arena_state.heroes[1].max_hp = 100;
    arena_state.heroes[1].owner = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].team = 1;
    arena_state.heroes[1].hero_id = bot_hero;

    arena_state.nodes[0].x = -4.0f;
    arena_state.nodes[0].z = 4.0f;
    arena_state.nodes[1].x = 4.0f;
    arena_state.nodes[1].z = -4.0f;

    arena_state.winner = 0;
}

void arena_init(void) {
    /* Player=Unicorn, bot=Duck: both slots carry a real kit (S170-31),
     * proving Phase D's "both sides" requirement rather than just adding a
     * second player-selectable option. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
}

void arena_set_move_target(int owner, float x, float z) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
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

/* arena_hero_armor: effective armor including Full Disclosure's temporary
 * double. Only The Unicorn has passive armor (S170-18); The Duck (S170-31)
 * has none -- dispatch is by hero_id now, not by owner slot, so either side
 * gets Unicorn's armor if either side is playing Unicorn. */
float arena_hero_armor(const ArenaHero *h) {
    if (h->hero_id != ARENA_HERO_UNICORN) return 0.0f;
    float armor = (float)ARENA_UNICORN_ARMOR;
    if (h->r_active_ms > 0) armor *= 2.0f;
    return armor;
}

static int apply_armor(int raw_damage, float armor) {
    int dmg = raw_damage - (int)armor;
    return dmg < 1 ? 1 : dmg;
}

/* arena_nearest_enemy: the nearest active, living hero on a different team
 * than `owner` -- generalizes what used to be a hardcoded "the other slot"
 * lookup (1v1-only) so the same cast functions work for both the 1v1 local
 * demo (where it trivially resolves to the one other hero) and team mode
 * (where it picks a real target out of up to 19 others). Returns NULL if
 * owner is out of range or nobody qualifies (e.g. owner's whole team is the
 * only one left, or owner itself isn't active). */
ArenaHero *arena_nearest_enemy(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return NULL;
    ArenaHero *self = &arena_state.heroes[owner];
    if (!self->active) return NULL;
    ArenaHero *best = NULL;
    float best_dist = 0.0f;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *cand = &arena_state.heroes[i];
        if (!cand->active || !cand->alive) continue;
        if (cand->team == self->team) continue;
        float dx = cand->x - self->x, dz = cand->z - self->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (!best || dist < best_dist) { best = cand; best_dist = dist; }
    }
    return best;
}

/* arena_nearest_ally: the nearest active, living hero on the SAME team as
 * `owner`, excluding owner itself. Mirrors arena_nearest_enemy exactly
 * (S170-34) -- the enabling primitive for every ally-targeted kit piece
 * previously skipped for having no target in 1v1. */
ArenaHero *arena_nearest_ally(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return NULL;
    ArenaHero *self = &arena_state.heroes[owner];
    if (!self->active) return NULL;
    ArenaHero *best = NULL;
    float best_dist = 0.0f;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        if (i == owner) continue;
        ArenaHero *cand = &arena_state.heroes[i];
        if (!cand->active || !cand->alive) continue;
        if (cand->team != self->team) continue;
        float dx = cand->x - self->x, dz = cand->z - self->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (!best || dist < best_dist) { best = cand; best_dist = dist; }
    }
    return best;
}

/* cast_cooldown: applies the generic next_cast_refund buff (S170-34,
 * Frog's Borrowed Time) -- returns 0 and consumes the buff if it's set on
 * h, else returns normal_ms unchanged. Every Q/W/R cooldown-assignment
 * site in this file routes through this so any future ally-buff kit gets
 * the same refund semantics for free. */
static int cast_cooldown(ArenaHero *h, int normal_ms) {
    if (h->next_cast_refund) {
        h->next_cast_refund = 0;
        return 0;
    }
    return normal_ms;
}

/* hero_is_hittable: The Ghost's W (S170-32) is the first ability in this
 * arena that needs a "can this hero currently be hit at all" concept,
 * distinct from just being alive -- used by auto-attacks and ability
 * damage alike so intangibility means the same thing everywhere. */
static int hero_is_hittable(const ArenaHero *h) {
    /* NULL-safe: arena_nearest_enemy (team mode) returns NULL when nobody
       qualifies (e.g. the last enemy died mid-tick) -- treat "no target" the
       same as "not hittable" rather than crashing. */
    return h && h->alive && h->intangible_ms <= 0;
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
        if (hero_is_hittable(b)) b->hp -= apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(b));
        a->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }
    if (b->attack_cooldown_ms <= 0) {
        if (hero_is_hittable(a)) a->hp -= apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(a));
        b->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }
    if (a->hp <= 0) { a->hp = 0; a->alive = 0; }
    if (b->hp <= 0) { b->hp = 0; b->alive = 0; }
}

/* --- Kit dispatch (S170-31 generalized this from S170-18's Unicorn-only,
   owner-hardcoded version -- everything below dispatches on hero_id, so
   either owner slot can carry either hero). --- */

static void unicorn_cast_q(ArenaHero *h, ArenaHero *foe) {
    /* Dash toward the current move target if moving, else toward the foe --
       a dash ability needs a direction, and "toward whatever you last
       clicked, or the enemy if you didn't" is the simplest honest default.
       foe may be NULL in team mode (no living enemy at all right now) --
       fall back to "moving" only in that case; if neither gives a
       direction, there's nothing to dash toward, so just no-op. */
    float dx, dz;
    if (h->moving) {
        dx = h->target_x - h->x;
        dz = h->target_z - h->z;
    } else if (foe) {
        dx = foe->x - h->x;
        dz = foe->z - h->z;
    } else {
        return;
    }
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return; /* no meaningful direction, e.g. already on top of foe */
    dx /= len; dz /= len;

    float nx = h->x + dx * ARENA_UNICORN_Q_DASH_DIST;
    float nz = h->z + dz * ARENA_UNICORN_Q_DASH_DIST;
    if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
    if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
    if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
    if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
    h->x = nx;
    h->z = nz;
    h->moving = 0;

    if (foe && hero_is_hittable(foe)) {
        float fdx = foe->x - h->x, fdz = foe->z - h->z;
        if (sqrtf(fdx * fdx + fdz * fdz) <= ARENA_UNICORN_Q_HIT_RADIUS) {
            foe->hp -= apply_armor(ARENA_UNICORN_Q_DAMAGE, arena_hero_armor(foe));
            if (foe->hp <= 0) { foe->hp = 0; foe->alive = 0; }
        }
    }
    h->q_cooldown_ms = cast_cooldown(h, ARENA_UNICORN_Q_COOLDOWN_MS);
}

/* duck_pull_foe: shared logic for Telekinetic Yank (Q) and the bigger
 * Total Telekinesis (R) -- both pull the foe toward the Duck by pull_dist
 * (clamped so it can't overshoot past the Duck) and deal damage, only if
 * the foe starts out within max_range. Returns 1 if it landed (so the
 * caller only consumes the cooldown on an actual hit, not a whiff), 0 if
 * the foe was out of range or currently unhittable (e.g. Ghost's W). */
static int duck_pull_foe(ArenaHero *duck, ArenaHero *foe, float pull_dist, int damage, float max_range) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = duck->x - foe->x;
    float dz = duck->z - foe->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > max_range) return 0; /* out of range -- no whiff-damage, no partial pull */
    if (dist > 0.01f) {
        float pull = pull_dist < dist ? pull_dist : dist; /* never pull the foe past the Duck */
        foe->x += dx / dist * pull;
        foe->z += dz / dist * pull;
    }
    foe->hp -= apply_armor(damage, arena_hero_armor(foe));
    if (foe->hp <= 0) { foe->hp = 0; foe->alive = 0; }
    return 1;
}

/* ghost_cast_q: Alien Frequency, simplified to an instant hit-if-in-range
 * check rather than a simulated travelling projectile -- same "instant,
 * not animated" simplification precedent as Duck's pull abilities. Damages
 * and silences the foe if it's hittable and within range. Returns 1 if it
 * landed (cooldown only consumed on a real hit, same convention as
 * duck_pull_foe), 0 on a whiff. */
static int ghost_cast_q(ArenaHero *ghost, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - ghost->x, dz = foe->z - ghost->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_GHOST_Q_RANGE) return 0;
    foe->hp -= apply_armor(ARENA_GHOST_Q_DAMAGE, arena_hero_armor(foe));
    if (foe->hp <= 0) { foe->hp = 0; foe->alive = 0; }
    foe->silenced_ms = ARENA_GHOST_Q_SILENCE_MS;
    return 1;
}

/* frog_cast_q: Loop Back, rewinds the Frog's own position/HP to
 * ARENA_FROG_Q_REWIND_MS ago using the loopback ring buffer any hero
 * accumulates in tick_hero_kit. Self-targeted, so it always "lands" once
 * called (unlike Duck/Ghost, there's no range/hittability check -- you
 * can't whiff a rewind on yourself). If less history exists than the full
 * rewind window (e.g. cast in the first few seconds of a match), rewinds
 * as far back as is actually recorded rather than refusing to cast at all. */
static void frog_cast_q(ArenaHero *frog) {
    if (frog->loopback_count == 0) return; /* no history yet at all */
    int slots_back = ARENA_FROG_Q_REWIND_MS / ARENA_FROG_LOOPBACK_SAMPLE_MS;
    if (slots_back >= frog->loopback_count) slots_back = frog->loopback_count - 1;
    int idx = ((frog->loopback_next_slot - 1 - slots_back) % ARENA_FROG_LOOPBACK_SLOTS
               + ARENA_FROG_LOOPBACK_SLOTS) % ARENA_FROG_LOOPBACK_SLOTS;
    frog->x = frog->loopback_x[idx];
    frog->z = frog->loopback_z[idx];
    frog->hp = frog->loopback_hp[idx];
    frog->moving = 0;
}

/* doc_wheel_heal_amount: Extremely Good At Medicine -- linearly scales from
 * ARENA_DOC_WHEEL_Q_HEAL_BASE at 100% target HP up to
 * ARENA_DOC_WHEEL_Q_HEAL_LOW_HP at 0% target HP (S170-34). */
static int doc_wheel_heal_amount(const ArenaHero *target) {
    if (target->max_hp <= 0) return ARENA_DOC_WHEEL_Q_HEAL_BASE;
    float hp_pct = (float)target->hp / (float)target->max_hp;
    if (hp_pct < 0.0f) hp_pct = 0.0f;
    if (hp_pct > 1.0f) hp_pct = 1.0f;
    float heal = ARENA_DOC_WHEEL_Q_HEAL_BASE +
                 (ARENA_DOC_WHEEL_Q_HEAL_LOW_HP - ARENA_DOC_WHEEL_Q_HEAL_BASE) * (1.0f - hp_pct);
    return (int)heal;
}

static void doc_wheel_heal_and_cleanse(ArenaHero *target, int amount) {
    target->hp += amount;
    if (target->hp > target->max_hp) target->hp = target->max_hp;
    target->silenced_ms = 0; /* Bedside Manner: "cleanses one debuff" -- the only debuff arena has today */
}

void arena_cast_q(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    ArenaHero *foe = arena_nearest_enemy(owner);
    if (!h->alive || h->silenced_ms > 0 || h->q_cooldown_ms > 0) return;

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        unicorn_cast_q(h, foe);
        break;
    case ARENA_HERO_DUCK:
        if (duck_pull_foe(h, foe, ARENA_DUCK_Q_PULL_DIST, ARENA_DUCK_Q_DAMAGE, ARENA_DUCK_Q_RANGE)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_DUCK_Q_COOLDOWN_MS);
        }
        break;
    case ARENA_HERO_GHOST:
        if (ghost_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_GHOST_Q_COOLDOWN_MS);
        }
        break;
    case ARENA_HERO_FROG:
        frog_cast_q(h);
        h->q_cooldown_ms = cast_cooldown(h, ARENA_FROG_Q_COOLDOWN_MS);
        break;
    case ARENA_HERO_DOC_WHEEL: {
        /* Bedside Manner: single-target heal + cleanse, on the nearest
           ally. No ally (1v1, or ally already dead) -- no-op, cooldown not
           consumed, same "whiff doesn't cost you the cooldown" convention
           as Duck/Ghost's Q. */
        ArenaHero *ally = arena_nearest_ally(owner);
        if (ally && ally->alive) {
            doc_wheel_heal_and_cleanse(ally, doc_wheel_heal_amount(ally));
            h->q_cooldown_ms = cast_cooldown(h, ARENA_DOC_WHEEL_Q_COOLDOWN_MS);
        }
        break;
    }
    }
}

void arena_toggle_w(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    if (!h->alive || h->silenced_ms > 0) return;

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        h->w_active = !h->w_active;
        break;
    case ARENA_HERO_GHOST:
        /* Not a Ghost: an instant-use buff on its own cooldown, not a
           toggle -- reuses the W slot but isn't a hold-on/hold-off state
           like Unicorn's regen. */
        if (h->w_cooldown_ms > 0) return;
        h->intangible_ms = ARENA_GHOST_W_INTANGIBLE_MS;
        h->w_cooldown_ms = cast_cooldown(h, ARENA_GHOST_W_COOLDOWN_MS);
        break;
    case ARENA_HERO_FROG: {
        /* Borrowed Time: places the refund buff on the nearest ally --
           wired for real now that arena_nearest_ally exists (was skipped
           for no ally target in 1v1, S170-33). No-op, cooldown not
           consumed, if there's no living ally to target. */
        if (h->w_cooldown_ms > 0) return;
        ArenaHero *ally = arena_nearest_ally(owner);
        if (ally && ally->alive) {
            ally->next_cast_refund = 1;
            h->w_cooldown_ms = cast_cooldown(h, ARENA_FROG_W_COOLDOWN_MS);
        }
        break;
    }
    case ARENA_HERO_DOC_WHEEL:
        /* House Call: instant teleport to the nearest ally's location, on
           a long cooldown ("always shows up"). No-op if there's no ally. */
        if (h->w_cooldown_ms > 0) return;
        {
            ArenaHero *ally = arena_nearest_ally(owner);
            if (ally && ally->alive) {
                h->x = ally->x;
                h->z = ally->z;
                h->moving = 0;
                h->w_cooldown_ms = cast_cooldown(h, ARENA_DOC_WHEEL_W_COOLDOWN_MS);
            }
        }
        break;
    default:
        /* No-op for any hero without a real W in this arena, not a crash
           or a silent wrong kit: Duck's W (Government Clearance) needs
           objective structures that don't exist here. */
        break;
    }
}

void arena_cast_r(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    ArenaHero *foe = arena_nearest_enemy(owner);
    if (!h->alive || h->silenced_ms > 0 || h->r_cooldown_ms > 0) return;

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        h->r_active_ms = ARENA_UNICORN_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_UNICORN_R_COOLDOWN_MS);
        break;
    case ARENA_HERO_DUCK:
        if (duck_pull_foe(h, foe, ARENA_DUCK_R_PULL_DIST, ARENA_DUCK_R_DAMAGE, ARENA_DUCK_R_RANGE)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_DUCK_R_COOLDOWN_MS);
        }
        break;
    case ARENA_HERO_GHOST:
        /* Recital: the ally-heal side (docs/HEROES_VS0.md: "same zone,
           opposite effect depending on team") is wired for real now that
           arena_nearest_ally exists (S170-34) -- see tick_hero_kit's zone
           tick below for the actual heal application, since it needs the
           `ally` parameter that loop already threads through. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_GHOST_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_GHOST_R_COOLDOWN_MS);
        break;
    case ARENA_HERO_FROG:
        /* The Secret, simplified: reuses the intangible_ms mechanic at a
           longer duration. "Reappear at any visited location" needs its
           own location-memory system -- not implemented, so this reappears
           in place, flagged as a simplification rather than the full
           ability. */
        h->intangible_ms = ARENA_FROG_R_VANISH_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_FROG_R_COOLDOWN_MS);
        break;
    case ARENA_HERO_DOC_WHEEL:
        /* No Combat Power, As Advertised: teamwide cleanse + heal within
           radius, simplified from a literal shield (see header comment).
           Unlike Q's single-target heal, this always "lands" and consumes
           the cooldown even with zero allies in range -- a real ultimate
           commitment, not a whiff-refunded poke. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *ally = &arena_state.heroes[i];
            if (i == owner || !ally->active || !ally->alive) continue;
            if (ally->team != h->team) continue;
            float dx = ally->x - h->x, dz = ally->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_DOC_WHEEL_R_RADIUS) {
                doc_wheel_heal_and_cleanse(ally, ARENA_DOC_WHEEL_R_HEAL);
            }
        }
        h->r_cooldown_ms = cast_cooldown(h, ARENA_DOC_WHEEL_R_COOLDOWN_MS);
        break;
    }
}

static void tick_hero_kit(ArenaHero *h, ArenaHero *foe, ArenaHero *ally, unsigned int dt_ms) {
    /* Cooldowns and status effects (silence, intangibility) are generic --
       any hero can carry them, not just whichever kit currently applies
       them (S170-32). */
    if (h->q_cooldown_ms > 0) h->q_cooldown_ms -= (int)dt_ms;
    if (h->w_cooldown_ms > 0) h->w_cooldown_ms -= (int)dt_ms;
    if (h->r_cooldown_ms > 0) h->r_cooldown_ms -= (int)dt_ms;
    if (h->silenced_ms > 0) {
        h->silenced_ms -= (int)dt_ms;
        if (h->silenced_ms < 0) h->silenced_ms = 0;
    }
    if (h->intangible_ms > 0) {
        h->intangible_ms -= (int)dt_ms;
        if (h->intangible_ms < 0) h->intangible_ms = 0;
    }

    /* Loop Back's history ring buffer (S170-33) is sampled for every hero,
       not just whoever's playing Frog -- same "generic state, only one
       kit reads it today" reasoning as the status-effect fields. Samples
       while alive only: rewinding into a pre-death state is the ability's
       whole point, but there's nothing meaningful to record once a match
       has already ended for this hero. */
    if (h->alive) {
        h->loopback_since_sample_ms += (int)dt_ms;
        while (h->loopback_since_sample_ms >= ARENA_FROG_LOOPBACK_SAMPLE_MS) {
            h->loopback_since_sample_ms -= ARENA_FROG_LOOPBACK_SAMPLE_MS;
            int slot = h->loopback_next_slot;
            h->loopback_x[slot] = h->x;
            h->loopback_z[slot] = h->z;
            h->loopback_hp[slot] = h->hp;
            h->loopback_next_slot = (slot + 1) % ARENA_FROG_LOOPBACK_SLOTS;
            if (h->loopback_count < ARENA_FROG_LOOPBACK_SLOTS) h->loopback_count++;
        }
    }

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
        }
        if (h->w_active && h->alive) {
            float regen = ARENA_UNICORN_W_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        break;
    case ARENA_HERO_GHOST:
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            /* Fixed-interval zone tick (once per 1000ms of accumulated
               time in the zone's duration), not fractional-per-tick DPS --
               correct at any real frame rate, same reasoning as the match
               event log's snapshot interval elsewhere in this codebase. */
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_GHOST_R_RADIUS) {
                        foe->hp -= apply_armor(ARENA_GHOST_R_DPS, arena_hero_armor(foe));
                        if (foe->hp <= 0) { foe->hp = 0; foe->alive = 0; }
                    }
                }
                /* Ally-heal side (S170-34): "same zone, opposite effect
                   depending on team" -- the nearest living ally standing in
                   the zone heals for the same rate the foe takes damage. */
                if (ally && ally->alive) {
                    float adx = ally->x - h->r_zone_x, adz = ally->z - h->r_zone_z;
                    if (sqrtf(adx * adx + adz * adz) <= ARENA_GHOST_R_RADIUS) {
                        ally->hp += ARENA_GHOST_R_DPS;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                    }
                }
            }
        }
        break;
    default:
        break;
    }
}

/* bot_cast_kit_if_ready: simple heuristic AI for whichever hero the bot is
 * playing -- cast Q (then R, once available) whenever off cooldown and the
 * foe is within that ability's range. Not a real decision-making bot brain
 * (that's Phase E's problem, GAME_AI_NORTHSTAR.md), just enough to prove
 * the bot side can actually use a kit at all (Phase D's "both sides"). */
static void bot_cast_kit_if_ready(ArenaHero *bot, ArenaHero *foe) {
    if (!bot->alive || !foe->alive) return;
    float dx = foe->x - bot->x, dz = foe->z - bot->z;
    float dist = sqrtf(dx * dx + dz * dz);

    switch (bot->hero_id) {
    case ARENA_HERO_DUCK:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_DUCK_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_DUCK_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_UNICORN:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_UNICORN_Q_HIT_RADIUS * 2.0f) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_GHOST:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_GHOST_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_GHOST_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_FROG:
        /* Defensive kit, so the heuristic is defensive too: rewind when
           hurt, vanish when critical -- not "attack when in range" like
           the other three, since Frog has no damage-dealing ability. */
        if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        } else if (bot->hp < bot->max_hp / 2 && bot->q_cooldown_ms <= 0) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_DOC_WHEEL:
        /* This heuristic is 1v1-only local-demo AI, and Doc Wheel's entire
           kit is ally-targeted -- no useful action exists with no ally
           present (S170-34). Doc Wheel is a real, working pick in team
           mode via apps/arena_bot's own simpler "cast Q periodically"
           heuristic, which the server-side dispatch already handles
           correctly regardless of hero. Intentional no-op here, not a
           missing case. */
        break;
    }
}

void arena_update(unsigned int dt_ms) {
    if (arena_state.winner != 0) return;
    float dt_sec = (float)dt_ms / 1000.0f;

    if (arena_bot_enabled) arena_bot_tick(dt_ms);

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
    /* No ally in the 1v1 local path (S170-34: arena_nearest_ally only
       exists for team mode) -- NULL is the correct value, same NULL-safety
       hero_is_hittable already relies on elsewhere. */
    tick_hero_kit(&arena_state.heroes[0], &arena_state.heroes[1], NULL, dt_ms);
    tick_hero_kit(&arena_state.heroes[1], &arena_state.heroes[0], NULL, dt_ms);
    /* Gated the same as arena_bot_tick (movement) above -- without this, a
       real second player (owner 1) would still get their kit cast
       autonomously by the internal bot AI (including Duck's Q, which pulls
       the foe), fighting their own real cast commands. Found live, 2026-07-24:
       hero0 (owner 0, no move command ever sent) still moved and took
       damage in a server with zero connected clients, because this call
       wasn't gated -- Duck's Q was yanking it every time it came off
       cooldown. */
    if (arena_bot_enabled) bot_cast_kit_if_ready(&arena_state.heroes[1], &arena_state.heroes[0]);

    if (!arena_state.heroes[0].alive) arena_state.winner = 2;
    else if (!arena_state.heroes[1].alive) arena_state.winner = 1;
}

/* ---- Team mode (2026-07-24, NORTHSTAR §13 cont'd: 10v10, up to
   ARENA_TEAM_SIZE per side). Additive, not a replacement for the 1v1 local
   demo above -- arena_update()/arena_init_with_heroes() are untouched, so
   nothing about the existing solo-vs-bot practice mode changes. Every slot
   in team mode is filled by a real network client (human or a real
   apps/arena_bot process) -- there is no internal-bot-AI fallback here,
   unlike the 1v1 path's arena_bot_tick/bot_cast_kit_if_ready. ---- */

void arena_init_teams(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        int team = (i < ARENA_TEAM_SIZE) ? 0 : 1;
        int slot_in_team = (team == 0) ? i : (i - ARENA_TEAM_SIZE);
        /* Two spread-out spawn lines, one per side, mirroring the 1v1
           demo's -6/+6 split but fanned out along z so a full team doesn't
           spawn stacked on one point. */
        h->x = (team == 0) ? -8.0f : 8.0f;
        h->z = (slot_in_team - (ARENA_TEAM_SIZE - 1) / 2.0f) * 2.0f;
        h->target_x = h->x;
        h->target_z = h->z;
        h->hp = h->max_hp = 100;
        h->owner = i;
        h->team = team;
        h->active = 1;
        h->alive = 1;
        h->hero_id = ARENA_HERO_UNICORN; /* placeholder until the real client's draft pick overrides it */
    }
    arena_state.nodes[0].x = -4.0f;
    arena_state.nodes[0].z = 4.0f;
    arena_state.nodes[1].x = 4.0f;
    arena_state.nodes[1].z = -4.0f;
    arena_state.winner = 0;
}

/* arena_team_alive_count: how many active heroes on `team` are still
 * alive -- the team-wipe win condition needs this rather than a single
 * hardcoded pair (§ arena_update's `!heroes[0].alive` check above, which
 * only ever made sense for exactly 2 heroes). */
static int arena_team_alive_count(int team) {
    int count = 0;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (h->active && h->alive && h->team == team) count++;
    }
    return count;
}

void arena_update_teams(unsigned int dt_ms) {
    if (arena_state.winner != 0) return;
    float dt_sec = (float)dt_ms / 1000.0f;

    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active) continue;
        update_hero_motion(h, dt_sec);
    }

    /* Melee combat: each active, alive hero independently attacks its own
       nearest enemy if one is in range and its cooldown is ready -- this is
       the N-hero generalization of the 1v1 resolve_combat's hardcoded pair,
       and multiple heroes on one side can converge on the same target
       (a real team-fight dynamic the 1v1 pairwise version never had to
       handle). */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active) continue;
        if (h->attack_cooldown_ms > 0) h->attack_cooldown_ms -= (int)dt_ms;
        if (!h->alive) continue;
        ArenaHero *foe = arena_nearest_enemy(i);
        if (!foe) continue;
        float dx = foe->x - h->x, dz = foe->z - h->z;
        if (sqrtf(dx * dx + dz * dz) > ARENA_ATTACK_RANGE) continue;
        if (h->attack_cooldown_ms > 0) continue;
        if (hero_is_hittable(foe)) foe->hp -= apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(foe));
        if (foe->hp <= 0) { foe->hp = 0; foe->alive = 0; }
        h->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }

    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active) continue;
        tick_hero_kit(h, arena_nearest_enemy(i), arena_nearest_ally(i), dt_ms);
    }

    int team0_alive = arena_team_alive_count(0);
    int team1_alive = arena_team_alive_count(1);
    if (team0_alive == 0) arena_state.winner = 2;
    else if (team1_alive == 0) arena_state.winner = 1;
}
