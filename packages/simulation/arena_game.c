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
    arena_state.nodes[0].marked_by_team = -1;
    arena_state.nodes[1].marked_by_team = -1;

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
    /* rooted_ms (S170-46): a queued move command is preserved (not
       cancelled) but doesn't advance while rooted -- matches how silence
       blocks casting without clearing the ability off cooldown. */
    if (!h->alive || !h->moving || h->rooted_ms > 0) return;
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
    if (h->hero_id == ARENA_HERO_UNICORN) {
        float armor = (float)ARENA_UNICORN_ARMOR;
        if (h->r_active_ms > 0) armor *= 2.0f;
        return armor;
    }
    /* Tree's Grand Secret (R, S170-46): flat armor bonus while self-rooted. */
    if (h->hero_id == ARENA_HERO_TREE && h->r_active_ms > 0) {
        return (float)ARENA_TREE_R_ARMOR_BONUS;
    }
    /* Morrigan's Contested Ground (passive, S170-47): bonus armor while
       standing within capture radius of a node that's still contested
       (owner == 0, neither team has claimed it) -- a war goddess belongs
       to the unresolved fight, her jungler tie to the territory system. */
    if (h->hero_id == ARENA_HERO_MORRIGAN) {
        for (int n = 0; n < ARENA_NODE_COUNT; n++) {
            const ArenaNode *node = &arena_state.nodes[n];
            if (node->owner != 0) continue;
            float dx = h->x - node->x, dz = h->z - node->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_NODE_CAPTURE_RADIUS) {
                return (float)ARENA_MORRIGAN_PASSIVE_ARMOR_BONUS;
            }
        }
    }
    return 0.0f;
}

static int apply_armor(int raw_damage, float armor) {
    int dmg = raw_damage - (int)armor;
    return dmg < 1 ? 1 : dmg;
}

/* apply_damage (S170-46): centralizes "subtract HP, clamp at 0, mark dead"
 * across every damage call site, so Pizza's R (a real damage floor, not a
 * simplified-away shield like Doc Wheel's) only needs one place to check
 * survive_floor_ms rather than duplicating the check at every site. Armor
 * is already applied by the caller via apply_armor -- this only handles the
 * HP-floor/death half. */
static void apply_damage(ArenaHero *target, int amount) {
    target->hp -= amount;
    if (target->hp <= 0) {
        if (target->survive_floor_ms > 0) {
            target->hp = 1;
        } else {
            target->hp = 0;
            target->alive = 0;
        }
    }
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
 * (S170-45) -- the enabling primitive for every ally-targeted kit piece
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

/* arena_tick_nodes (S170-46): advances every ArenaNode's capture contest by
 * dt_ms. One pass per node: sum weighted living-hero presence per team
 * within ARENA_NODE_CAPTURE_RADIUS (Tree counts double, Root Network),
 * refresh/decay Flamel's Overgrowth mark, drift pressure toward whichever
 * team is ahead (or decay toward neutral if tied/uncontested), apply a
 * bonus pull on a marked-but-still-neutral node toward the marking team
 * (deterministic simplification of "increased chance of converting",
 * flagged), apply Pizza's corruption pull toward neutral regardless of team
 * composition (simplification of the doc's 4-state CORRUPTED concept,
 * flagged), then clamp and recompute owner from the threshold. Generalizes
 * across 1v1 and team mode with no special-casing, same as
 * arena_nearest_ally/arena_nearest_enemy -- 1v1 already sets team=0/1 on
 * its two hardcoded heroes. */
void arena_tick_nodes(unsigned int dt_ms) {
    float dt_sec = (float)dt_ms / 1000.0f;

    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        ArenaNode *node = &arena_state.nodes[n];
        int old_owner = node->owner; /* "still neutral" gate for the marked-node bonus pull, below */
        float team_weight[2] = {0.0f, 0.0f};
        int pizza_in_radius = 0;
        int flamel_marker_team = -1;

        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->active || !h->alive) continue;
            float dx = h->x - node->x, dz = h->z - node->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_NODE_CAPTURE_RADIUS) continue;
            int weight = (h->hero_id == ARENA_HERO_TREE) ? ARENA_TREE_CAPTURE_WEIGHT : 1;
            team_weight[h->team] += (float)weight;
            if (h->hero_id == ARENA_HERO_PIZZA) pizza_in_radius = 1;
            if (h->hero_id == ARENA_HERO_FLAMEL) flamel_marker_team = h->team;
        }

        if (flamel_marker_team >= 0) {
            node->marked_by_team = flamel_marker_team;
            node->mark_ms_remaining = ARENA_FLAMEL_MARK_MS;
        } else if (node->mark_ms_remaining > 0) {
            node->mark_ms_remaining -= (int)dt_ms;
            if (node->mark_ms_remaining <= 0) {
                node->mark_ms_remaining = 0;
                node->marked_by_team = -1;
            }
        }

        float diff = team_weight[0] - team_weight[1];
        if (diff > 0.0f) {
            node->pressure += ARENA_NODE_PRESSURE_RATE * dt_sec;
        } else if (diff < 0.0f) {
            node->pressure -= ARENA_NODE_PRESSURE_RATE * dt_sec;
        } else if (node->pressure > 0.0f) {
            node->pressure -= ARENA_NODE_DECAY_RATE * dt_sec;
            if (node->pressure < 0.0f) node->pressure = 0.0f;
        } else if (node->pressure < 0.0f) {
            node->pressure += ARENA_NODE_DECAY_RATE * dt_sec;
            if (node->pressure > 0.0f) node->pressure = 0.0f;
        }

        if (old_owner == 0 && node->marked_by_team == 0) {
            node->pressure += ARENA_FLAMEL_MARK_BONUS_RATE * dt_sec;
        } else if (old_owner == 0 && node->marked_by_team == 1) {
            node->pressure -= ARENA_FLAMEL_MARK_BONUS_RATE * dt_sec;
        }

        if (pizza_in_radius) {
            if (node->pressure > 0.0f) {
                node->pressure -= ARENA_PIZZA_CORRUPT_PULL_RATE * dt_sec;
                if (node->pressure < 0.0f) node->pressure = 0.0f;
            } else if (node->pressure < 0.0f) {
                node->pressure += ARENA_PIZZA_CORRUPT_PULL_RATE * dt_sec;
                if (node->pressure > 0.0f) node->pressure = 0.0f;
            }
        }

        if (node->pressure > 100.0f) node->pressure = 100.0f;
        if (node->pressure < -100.0f) node->pressure = -100.0f;

        if (node->pressure >= ARENA_NODE_OWNER_THRESHOLD) node->owner = 1;
        else if (node->pressure <= -ARENA_NODE_OWNER_THRESHOLD) node->owner = 2;
        else node->owner = 0;
    }
}

/* cast_cooldown: applies the generic next_cast_refund buff (S170-45,
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
        if (hero_is_hittable(b)) apply_damage(b, apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(b)));
        a->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }
    if (b->attack_cooldown_ms <= 0) {
        if (hero_is_hittable(a)) apply_damage(a, apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(a)));
        b->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }
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
            apply_damage(foe, apply_armor(ARENA_UNICORN_Q_DAMAGE, arena_hero_armor(foe)));
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
    /* rooted_ms (S170-46): Tree's Grand Secret is "immune to displacement" --
       the pull is skipped but damage still lands, same as any other root not
       blocking incoming damage. */
    if (dist > 0.01f && foe->rooted_ms <= 0) {
        float pull = pull_dist < dist ? pull_dist : dist; /* never pull the foe past the Duck */
        foe->x += dx / dist * pull;
        foe->z += dz / dist * pull;
    }
    apply_damage(foe, apply_armor(damage, arena_hero_armor(foe)));
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
    apply_damage(foe, apply_armor(ARENA_GHOST_Q_DAMAGE, arena_hero_armor(foe)));
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
 * ARENA_DOC_WHEEL_Q_HEAL_LOW_HP at 0% target HP (S170-45). */
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

/* tree_cast_q: Vine Lash, simplified from "AoE root in a cone in front" to
 * an instant hit-if-in-range check, same precedent as Ghost's Alien
 * Frequency. Returns 1 if it landed (cooldown only consumed on a hit), 0 on
 * a whiff. */
static int tree_cast_q(ArenaHero *tree, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - tree->x, dz = foe->z - tree->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_TREE_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_TREE_Q_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_TREE_Q_ROOT_MS;
    return 1;
}

/* pizza_cast_q: Nobody Checked, simplified from "throw a burning slice +
 * ground patch" to direct damage + a burn DoT applied straight to the foe --
 * no persistent ground-hazard system exists in this arena, so the
 * lingering-patch half is dropped, not faked. Returns 1 if it landed, 0 on
 * a whiff. */
static int pizza_cast_q(ArenaHero *pizza, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - pizza->x, dz = foe->z - pizza->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_PIZZA_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_PIZZA_Q_DAMAGE, arena_hero_armor(foe)));
    foe->burning_ms = ARENA_PIZZA_Q_BURN_MS;
    foe->burn_dps = ARENA_PIZZA_Q_BURN_DPS;
    return 1;
}

/* flamel_cast_q: Vine Growth (absorbed from the former Druid), simplified
 * from "wall of vines in a line" to an instant root-if-in-range check on
 * the nearest enemy -- same cone/line-to-single-target simplification as
 * Tree's Q. Pure crowd control, no damage, matching the doc's own ability
 * (blocks movement, nothing else). Returns 1 if it landed, 0 on a whiff. */
static int flamel_cast_q(ArenaHero *flamel, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - flamel->x, dz = foe->z - flamel->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_FLAMEL_Q_RANGE) return 0;
    foe->rooted_ms = ARENA_FLAMEL_Q_ROOT_MS;
    return 1;
}

/* flamel_cast_w: Philosopher's Bloom (Bloom + Philosopher's Batch merged,
 * S170-46) -- heals every living ally within radius at once, healing for
 * more if Flamel himself is standing within capture radius of a node his
 * own team has marked (Overgrowth, absorbed from Druid). Always "lands"
 * and consumes the cooldown, same always-commits convention as Doc Wheel's
 * R -- an AoE heal isn't a single-target poke that can whiff. */
static void flamel_cast_w(ArenaHero *flamel, int owner) {
    int on_marked_ground = 0;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        ArenaNode *node = &arena_state.nodes[n];
        if (node->marked_by_team != flamel->team) continue;
        float ndx = flamel->x - node->x, ndz = flamel->z - node->z;
        if (sqrtf(ndx * ndx + ndz * ndz) <= ARENA_NODE_CAPTURE_RADIUS) { on_marked_ground = 1; break; }
    }
    int heal = on_marked_ground ? ARENA_FLAMEL_W_HEAL_MARKED : ARENA_FLAMEL_W_HEAL_BASE;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *ally = &arena_state.heroes[i];
        if (i == owner || !ally->active || !ally->alive) continue;
        if (ally->team != flamel->team) continue;
        float dx = ally->x - flamel->x, dz = ally->z - flamel->z;
        if (sqrtf(dx * dx + dz * dz) > ARENA_FLAMEL_W_RADIUS) continue;
        ally->hp += heal;
        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
    }
}

/* execute_scale_damage: linearly scales from base_dmg at 100% target HP up
 * to low_hp_dmg at ~0% target HP -- same shape as doc_wheel_heal_amount,
 * inverted for damage instead of healing (Morrigan's death-omen kit,
 * S170-47: "the crow confirms the kill"). */
static int execute_scale_damage(const ArenaHero *target, int base_dmg, int low_hp_dmg) {
    if (target->max_hp <= 0) return base_dmg;
    float hp_pct = (float)target->hp / (float)target->max_hp;
    if (hp_pct < 0.0f) hp_pct = 0.0f;
    if (hp_pct > 1.0f) hp_pct = 1.0f;
    float dmg = base_dmg + (low_hp_dmg - base_dmg) * (1.0f - hp_pct);
    return (int)dmg;
}

/* morrigan_cast_q: The Washer's Strike, instant hit-if-in-range (same
 * precedent as Ghost/Tree/Pizza's Q), execute-scaled via
 * execute_scale_damage. Returns 1 if it landed, 0 on a whiff. */
static int morrigan_cast_q(ArenaHero *morrigan, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - morrigan->x, dz = foe->z - morrigan->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_MORRIGAN_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(execute_scale_damage(foe, ARENA_MORRIGAN_Q_DAMAGE_BASE, ARENA_MORRIGAN_Q_DAMAGE_LOW_HP),
                                   arena_hero_armor(foe)));
    return 1;
}

/* morrigan_cast_w: Three Forms -- teleports to the nearest enemy's position
 * and roots them on arrival ("she appears where he doesn't expect, in
 * another form"). No range check -- a sudden appearance, not a skillshot.
 * Returns 1 if it landed, 0 with no living enemy at all. */
static int morrigan_cast_w(ArenaHero *morrigan, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    morrigan->x = foe->x;
    morrigan->z = foe->z;
    morrigan->moving = 0;
    foe->rooted_ms = ARENA_MORRIGAN_W_ROOT_MS;
    return 1;
}

/* dagda_cast_q: "the same tool, either direction, depending only on which
 * end swings first" -- built literally. A hittable enemy in range takes
 * priority (the killing end); absent that, a hurt living ally in range
 * gets the reviving end, simplified to a heal since no respawn system
 * exists to revive a dead ally into. Returns 1 if either end landed, 0 on
 * a full whiff (nothing valid in range at all). */
static int dagda_cast_q(ArenaHero *dagda, ArenaHero *foe, ArenaHero *ally) {
    if (foe && hero_is_hittable(foe)) {
        float dx = foe->x - dagda->x, dz = foe->z - dagda->z;
        if (sqrtf(dx * dx + dz * dz) <= ARENA_DAGDA_Q_RANGE) {
            apply_damage(foe, apply_armor(ARENA_DAGDA_Q_KILL_DAMAGE, arena_hero_armor(foe)));
            return 1;
        }
    }
    if (ally && ally->alive && ally->hp < ally->max_hp) {
        float dx = ally->x - dagda->x, dz = ally->z - dagda->z;
        if (sqrtf(dx * dx + dz * dz) <= ARENA_DAGDA_Q_RANGE) {
            ally->hp += ARENA_DAGDA_Q_REVIVE_HEAL;
            if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
            return 1;
        }
    }
    return 0;
}

/* dagda_cast_w: Uaithne, called by name -- all three master strains played
 * over the whole hall in one go. One AoE cast, everyone in radius
 * experiences a different strain depending on side: allies get joy (heal),
 * hittable enemies get sorrow+sleep (root+silence) at once. Always lands
 * and consumes the cooldown, same always-commits convention as other AoE
 * ultimates in this roster (Doc Wheel's R) -- a hall-filling cast isn't a
 * single-target poke that can whiff. */
static void dagda_cast_w(ArenaHero *dagda, int owner) {
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *other = &arena_state.heroes[i];
        if (i == owner || !other->active || !other->alive) continue;
        float dx = other->x - dagda->x, dz = other->z - dagda->z;
        if (sqrtf(dx * dx + dz * dz) > ARENA_DAGDA_W_RADIUS) continue;
        if (other->team == dagda->team) {
            other->hp += ARENA_DAGDA_W_ALLY_HEAL;
            if (other->hp > other->max_hp) other->hp = other->max_hp;
        } else if (hero_is_hittable(other)) {
            other->rooted_ms = ARENA_DAGDA_W_ROOT_MS;
            other->silenced_ms = ARENA_DAGDA_W_SILENCE_MS;
        }
    }
}

/* courier_cast_q: The Insult, Lightly Edited -- dashes a fixed distance
 * toward the nearest enemy (same shape as Unicorn's Diagnostic Charge:
 * fixed dash length, not clamped to the foe's own distance, so it can
 * overshoot past a close target same as Unicorn's does) and deals damage
 * if it lands within hit radius. Also cleanses The Courier's own active
 * debuffs on cast (Lightly Edited passive) -- "editing the message"
 * addressed back to him, regardless of whether the damage half connects.
 * Returns 1 if there was a living enemy to dash toward at all, 0 if not. */
static int courier_cast_q(ArenaHero *courier, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - courier->x, dz = foe->z - courier->z;
    float len = sqrtf(dx * dx + dz * dz);
    if (len > 0.01f) {
        float nx = courier->x + dx / len * ARENA_COURIER_Q_DASH_DIST;
        float nz = courier->z + dz / len * ARENA_COURIER_Q_DASH_DIST;
        if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
        if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
        if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
        if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
        courier->x = nx;
        courier->z = nz;
        courier->moving = 0;
    }
    float fdx = foe->x - courier->x, fdz = foe->z - courier->z;
    if (sqrtf(fdx * fdx + fdz * fdz) <= ARENA_COURIER_Q_HIT_RADIUS) {
        apply_damage(foe, apply_armor(ARENA_COURIER_Q_DAMAGE, arena_hero_armor(foe)));
    }
    courier->silenced_ms = 0;
    courier->rooted_ms = 0;
    return 1;
}

/* courier_toggle_w: Between Eagle and Serpent -- instantly repositions to
 * whichever of the two map nodes is farther from The Courier's current
 * position, always making real progress "along the tree" rather than
 * bouncing back and forth to the same one. Pure fixed-geography teleport,
 * distinct from every other hero's ally/foe-relative one. Always lands
 * (there are always exactly two nodes) -- no whiff case. */
static void courier_toggle_w(ArenaHero *courier) {
    float d0x = arena_state.nodes[0].x - courier->x, d0z = arena_state.nodes[0].z - courier->z;
    float d1x = arena_state.nodes[1].x - courier->x, d1z = arena_state.nodes[1].z - courier->z;
    float dist0 = sqrtf(d0x * d0x + d0z * d0z);
    float dist1 = sqrtf(d1x * d1x + d1z * d1z);
    int target = (dist1 > dist0) ? 1 : 0;
    courier->x = arena_state.nodes[target].x;
    courier->z = arena_state.nodes[target].z;
    courier->moving = 0;
}

/* courier_cast_r: The Debt Collector's Due -- a flat life-drain execute on
 * the nearest enemy in range. "A job that was never meant to involve
 * judgment, and has, over a very long tenure, started to" -- The Courier
 * takes a cut off what passes through him by force. Returns 1 if it
 * landed, 0 on a whiff. */
static int courier_cast_r(ArenaHero *courier, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - courier->x, dz = foe->z - courier->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_COURIER_R_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_COURIER_R_DRAIN, arena_hero_armor(foe)));
    courier->hp += ARENA_COURIER_R_DRAIN;
    if (courier->hp > courier->max_hp) courier->hp = courier->max_hp;
    return 1;
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
    case ARENA_HERO_TREE:
        if (tree_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_TREE_Q_COOLDOWN_MS);
        }
        break;
    case ARENA_HERO_PIZZA:
        if (pizza_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_PIZZA_Q_COOLDOWN_MS);
        }
        break;
    case ARENA_HERO_FLAMEL:
        if (flamel_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_FLAMEL_Q_COOLDOWN_MS);
        }
        break;
    case ARENA_HERO_MORRIGAN:
        if (morrigan_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_MORRIGAN_Q_COOLDOWN_MS);
        }
        break;
    case ARENA_HERO_DAGDA:
        if (dagda_cast_q(h, foe, arena_nearest_ally(owner))) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_DAGDA_Q_COOLDOWN_MS);
        }
        break;
    case ARENA_HERO_COURIER:
        if (courier_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_COURIER_Q_COOLDOWN_MS);
        }
        break;
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
    case ARENA_HERO_FLAMEL:
        /* Philosopher's Bloom: AoE ally heal, always lands (see
           flamel_cast_w) -- same always-commits convention as Doc Wheel's
           R, not a whiff-refunded single-target poke. */
        if (h->w_cooldown_ms > 0) return;
        flamel_cast_w(h, owner);
        h->w_cooldown_ms = cast_cooldown(h, ARENA_FLAMEL_W_COOLDOWN_MS);
        break;
    case ARENA_HERO_MORRIGAN:
        /* Three Forms: gap-close + root on the nearest enemy. No-op,
           cooldown not consumed, if there's no living enemy at all
           (1v1's own bot could still die mid-match). */
        if (h->w_cooldown_ms > 0) return;
        if (morrigan_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_MORRIGAN_W_COOLDOWN_MS);
        }
        break;
    case ARENA_HERO_DAGDA:
        /* Uaithne, called by name: AoE hits everyone in radius, always
           lands (see dagda_cast_w) -- same always-commits convention as
           Flamel's W above. */
        if (h->w_cooldown_ms > 0) return;
        dagda_cast_w(h, owner);
        h->w_cooldown_ms = cast_cooldown(h, ARENA_DAGDA_W_COOLDOWN_MS);
        break;
    case ARENA_HERO_COURIER:
        /* Between Eagle and Serpent: always lands, there are always
           exactly two nodes to jump between. */
        if (h->w_cooldown_ms > 0) return;
        courier_toggle_w(h);
        h->w_cooldown_ms = cast_cooldown(h, ARENA_COURIER_W_COOLDOWN_MS);
        break;
    default:
        /* No-op for any hero without a real W in this arena, not a crash
           or a silent wrong kit: Duck's W (Government Clearance) needs
           objective structures that don't exist here. Tree's W
           (Untranslated) and Pizza's W (I Am The Chosen One) both fall here
           too -- unbuildable/pure-visual, flagged in the header comments. */
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
           arena_nearest_ally exists (S170-45) -- see tick_hero_kit's zone
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
    case ARENA_HERO_TREE:
        /* Grand Secret, simplified from "roots until recast, min 8s" to a
           fixed-duration self-root + armor buff + heal -- same
           fixed-duration simplification already used for Frog's R and
           Ghost's R zone. rooted_ms doubles as "immune to displacement"
           (see duck_pull_foe). */
        h->rooted_ms = ARENA_TREE_R_ROOT_MS;
        h->r_active_ms = ARENA_TREE_R_ROOT_MS;
        h->hp += ARENA_TREE_R_HEAL;
        if (h->hp > h->max_hp) h->hp = h->max_hp;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_TREE_R_COOLDOWN_MS);
        break;
    case ARENA_HERO_PIZZA:
        /* Nobody Ever Checks: HP cannot drop below 1 for the duration -- a
           real damage floor via apply_damage's survive_floor_ms check, not
           a simplified-away shield (contrast Doc Wheel's R, deferred for
           exactly that reason). */
        h->survive_floor_ms = ARENA_PIZZA_R_FLOOR_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_PIZZA_R_COOLDOWN_MS);
        break;
    case ARENA_HERO_FLAMEL:
        /* Elixir of Wild Growth (Elixir of Life + Wild Growth merged): a
           fixed zone (reusing Ghost's r_zone_x/z/tick_ms fields) that roots
           enemies and heals allies each tick for its duration -- see
           tick_hero_kit's zone tick below -- plus a one-time mass-mark of
           nodes in radius at cast time. The doc's "heavy slow" is
           simplified to a full root: no per-hero movement-speed-multiplier
           system exists in this arena yet, flagged. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_FLAMEL_R_DURATION_MS;
        for (int n = 0; n < ARENA_NODE_COUNT; n++) {
            ArenaNode *node = &arena_state.nodes[n];
            float ndx = h->x - node->x, ndz = h->z - node->z;
            if (sqrtf(ndx * ndx + ndz * ndz) <= ARENA_FLAMEL_R_RADIUS) {
                node->marked_by_team = h->team;
                node->mark_ms_remaining = ARENA_FLAMEL_MARK_MS;
            }
        }
        h->r_cooldown_ms = cast_cooldown(h, ARENA_FLAMEL_R_COOLDOWN_MS);
        break;
    case ARENA_HERO_MORRIGAN:
        /* The Crow Confirms It: a fixed zone (reusing Ghost's
           r_zone_x/z/tick_ms fields) that deals execute-scaled DPS to
           enemies inside for its duration -- see tick_hero_kit's zone tick
           below. No ally-heal side (unlike Ghost/Flamel's R) -- a war
           goddess's ultimate isn't a support tool. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_MORRIGAN_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_MORRIGAN_R_COOLDOWN_MS);
        break;
    case ARENA_HERO_DAGDA:
        /* The force-fed porridge: a real damage floor (like Pizza's R) plus
           a real heal on top -- "eats every bite, unhurt, fights the next
           day regardless," enduring AND coming out ahead, not just
           surviving. */
        h->survive_floor_ms = ARENA_DAGDA_R_FLOOR_MS;
        h->hp += ARENA_DAGDA_R_HEAL;
        if (h->hp > h->max_hp) h->hp = h->max_hp;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_DAGDA_R_COOLDOWN_MS);
        break;
    case ARENA_HERO_COURIER:
        if (courier_cast_r(h, foe)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_COURIER_R_COOLDOWN_MS);
        }
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
    /* rooted_ms/survive_floor_ms (S170-46): generic status effects, any
       kit's ability can apply them, same reasoning as silence/intangible
       above. */
    if (h->rooted_ms > 0) {
        h->rooted_ms -= (int)dt_ms;
        if (h->rooted_ms < 0) h->rooted_ms = 0;
    }
    if (h->survive_floor_ms > 0) {
        h->survive_floor_ms -= (int)dt_ms;
        if (h->survive_floor_ms < 0) h->survive_floor_ms = 0;
    }
    /* burning_ms/burn_dps (S170-46, Pizza's Q): fixed-interval DoT tick,
       same 1000ms-accumulator pattern as Ghost's R zone. burn_tick_ms
       resets when the burn ends so a later re-application starts clean. */
    if (h->burning_ms > 0) {
        if (h->alive) {
            h->burn_tick_ms += (int)dt_ms;
            while (h->burn_tick_ms >= 1000 && h->burning_ms > 0) {
                h->burn_tick_ms -= 1000;
                apply_damage(h, h->burn_dps);
            }
        }
        h->burning_ms -= (int)dt_ms;
        if (h->burning_ms <= 0) { h->burning_ms = 0; h->burn_tick_ms = 0; }
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
                        apply_damage(foe, apply_armor(ARENA_GHOST_R_DPS, arena_hero_armor(foe)));
                    }
                }
                /* Ally-heal side (S170-45): "same zone, opposite effect
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
    case ARENA_HERO_TREE:
        /* Grand Secret's fixed-duration armor/root window (see arena_cast_r) --
           rooted_ms already decrements generically above; this only owns
           r_active_ms, same pattern as Unicorn/Ghost. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
        }
        break;
    case ARENA_HERO_PIZZA:
        /* Uninvestigated Fire: an always-on burn aura, not a cast -- ticks
           independently of Q/W/R cooldowns. Pizza is immune to its own
           burn (per the doc) since this only ever damages `foe`, never h
           itself. The node-corruption half of this passive is handled
           generically in arena_tick_nodes, not here. Only checks the
           single nearest-foe parameter (same limitation as Ghost's R zone
           in team mode -- an existing, accepted precedent, not a new one). */
        if (h->alive) {
            h->aura_tick_ms += (int)dt_ms;
            while (h->aura_tick_ms >= 1000) {
                h->aura_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->x, dz = foe->z - h->z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_PIZZA_AURA_RADIUS) {
                        apply_damage(foe, ARENA_PIZZA_AURA_DPS);
                    }
                }
            }
        }
        break;
    case ARENA_HERO_FLAMEL:
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_FLAMEL_R_RADIUS) {
                        foe->rooted_ms = ARENA_FLAMEL_R_ROOT_MS;
                    }
                }
                if (ally && ally->alive) {
                    float adx = ally->x - h->r_zone_x, adz = ally->z - h->r_zone_z;
                    if (sqrtf(adx * adx + adz * adz) <= ARENA_FLAMEL_R_RADIUS) {
                        ally->hp += ARENA_FLAMEL_R_HEAL_PER_TICK;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_MORRIGAN:
        /* The Crow Confirms It: execute-scaled DPS zone tick, same
           fixed-interval pattern as Ghost/Flamel's R. No ally-heal side. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_MORRIGAN_R_RADIUS) {
                        apply_damage(foe, apply_armor(
                            execute_scale_damage(foe, ARENA_MORRIGAN_R_DAMAGE_BASE, ARENA_MORRIGAN_R_DAMAGE_LOW_HP),
                            arena_hero_armor(foe)));
                    }
                }
            }
        }
        break;
    case ARENA_HERO_DAGDA:
        /* The Undry: passive self HP regen, always on, no cooldown/cast
           gate at all -- "no one leaves it unsatisfied." */
        if (h->alive) {
            float regen = ARENA_DAGDA_PASSIVE_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
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
           present (S170-45). Doc Wheel is a real, working pick in team
           mode via apps/arena_bot's own simpler "cast Q periodically"
           heuristic, which the server-side dispatch already handles
           correctly regardless of hero. Intentional no-op here, not a
           missing case. */
        break;
    case ARENA_HERO_TREE:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_TREE_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->hp < bot->max_hp / 3 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_PIZZA:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_PIZZA_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_FLAMEL:
        /* Q is the only foe-targeted piece of this kit -- W/R are ally-AoE
           and have no useful action in the 1v1 local demo's bot heuristic,
           same reasoning as Doc Wheel above. */
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_FLAMEL_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_MORRIGAN:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_MORRIGAN_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->w_cooldown_ms <= 0) {
            arena_toggle_w(bot->owner); /* Three Forms: closes distance on its own */
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_MORRIGAN_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_DAGDA:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_DAGDA_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->hp < bot->max_hp / 3 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_COURIER:
        if (bot->q_cooldown_ms <= 0) {
            arena_cast_q(bot->owner); /* dash-strike, closes distance on its own like Morrigan's W */
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_COURIER_R_RANGE) {
            arena_cast_r(bot->owner);
        }
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
    arena_tick_nodes(dt_ms);
    resolve_combat(dt_ms);
    /* No ally in the 1v1 local path (S170-45: arena_nearest_ally only
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
    arena_state.nodes[0].marked_by_team = -1;
    arena_state.nodes[1].marked_by_team = -1;
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
    arena_tick_nodes(dt_ms);

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
        if (hero_is_hittable(foe)) apply_damage(foe, apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(foe)));
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
