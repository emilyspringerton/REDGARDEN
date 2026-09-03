/* tests/test_arena_game.c — headless smoke test for packages/simulation/
 * arena_game.c. No SDL/GL dependency on purpose: this box has no display
 * (no Xvfb), so the arena client itself can't be run interactively here,
 * but the sim logic underneath it has zero GL dependency and is fully
 * testable without one. Written to catch real bugs before the client is
 * ever visually confirmed working. */
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static ArenaProjectile *find_active_projectile(void) {
    for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
        if (arena_state.projectiles[i].active) return &arena_state.projectiles[i];
    }
    return NULL;
}

static void test_movement_reaches_target(void) {
    arena_init();
    /* S170-228: arena_bot_tick's own movement now comes from a trained RL policy (see
       packages/common/rl_policy_weights.h), not the old fixed hand-picked net -- a learned
       policy closing distance aggressively enough to trigger combat before hero 0 reaches a
       short target is a real, expected behavior change, not a bug in either the trained policy
       or hero 0's own move-target logic this test actually exists to check. Disabled/restored
       the same way test_arena_bot_enabled_gates_kit_casts_too below already does, so this test
       goes back to checking exactly what its own name says -- movement targeting -- independent
       of whatever the bot AI currently does. */
    arena_bot_enabled = 0;
    /* Hero0 starts at (-6,0); target is close (~4.2 units away, ~1s at
       ARENA_HERO_SPEED). Deliberately does NOT touch the bot's HP/alive
       state -- killing it mid-test would trigger the win condition and
       freeze arena_update() (it returns immediately once winner != 0),
       which is exactly the bug this test caught on the first pass. */
    arena_set_move_target(0, -3.0f, 3.0f);
    int ticks = 0;
    while (arena_state.heroes[0].moving && arena_state.winner == 0 && ticks < 500) {
        arena_update(16);
        ticks++;
    }
    float dx = arena_state.heroes[0].x - (-3.0f);
    float dz = arena_state.heroes[0].z - 3.0f;
    float dist = sqrtf(dx * dx + dz * dz);
    CHECK(dist < 0.1f, "hero reaches its move target");
    CHECK(arena_state.winner == 0, "match still in progress -- combat didn't interrupt a short move");
    arena_bot_enabled = 1; /* restore the default for any test run after this one */
}

static void test_bounds_clamped(void) {
    arena_init();
    arena_set_move_target(0, 999.0f, -999.0f);
    CHECK(arena_state.heroes[0].target_x <= ARENA_HALF_EXTENT + 0.001f,
          "move target clamped to arena bounds (x)");
    CHECK(arena_state.heroes[0].target_z >= -ARENA_HALF_EXTENT - 0.001f,
          "move target clamped to arena bounds (z)");
}

static void test_combat_and_win_condition(void) {
    arena_init();
    /* Place the heroes already adjacent so combat starts immediately. */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 0.5f; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].hp = 8; /* one hit from dying */
    arena_set_move_target(0, 0.0f, 0.0f); /* player holds position */

    int ticks = 0;
    while (arena_state.winner == 0 && ticks < 5000) {
        arena_update(16);
        ticks++;
    }
    CHECK(arena_state.winner != 0, "match reaches a winner instead of running forever");
    CHECK(arena_state.winner == 1, "player wins when bot's HP is set near zero");
    CHECK(!arena_state.heroes[1].alive, "loser is marked not-alive");
}

static void test_bot_steers_toward_player(void) {
    arena_init();
    arena_state.heroes[1].x = 6.0f; arena_state.heroes[1].z = 0.0f;
    arena_state.heroes[0].x = -6.0f; arena_state.heroes[0].z = 0.0f;
    arena_bot_tick(16);
    /* Bot is east of the player -- its steering target should move it west (toward smaller x). */
    CHECK(arena_state.heroes[1].target_x < arena_state.heroes[1].x,
          "bot brain steers toward the player, not away");
}

static void test_click_near_enemy_becomes_attack_move(void) {
    arena_init();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 5.0f; arena_state.heroes[1].z = 0.0f;
    /* Click 1 unit short of the bot -- inside the attack-move re-target
       radius (ARENA_ATTACK_RANGE * 3 = 4.8) but not yet in attack range. */
    arena_set_move_target(0, 4.0f, 0.0f);
    float bot_x_before = arena_state.heroes[1].x, bot_z_before = arena_state.heroes[1].z;
    arena_update(16);
    /* Compare against the bot's pre-tick position: the bot also steers and
       moves within this same update() call, so its post-tick position has
       already drifted a little (~0.06 units at one 16ms tick) from where
       the re-target snapped to. */
    float dx = arena_state.heroes[0].target_x - bot_x_before;
    float dz = arena_state.heroes[0].target_z - bot_z_before;
    CHECK(sqrtf(dx * dx + dz * dz) < 0.01f,
          "clicking near the bot re-targets onto the bot (attack-move), not the exact click point");
}

/* --- The Unicorn's kit (docs/HEROES_VS0.md, EMILY/BACKLOG.md S170-18) --- */

static void test_unicorn_q_dashes_and_damages(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    float start_x = h->x;
    /* Move the foe adjacent to where the dash will land, in the direction
       of the hero's current move target, so the hit-radius check succeeds. */
    arena_set_move_target(0, h->x + 4.0f, h->z);
    foe->x = h->x + ARENA_UNICORN_Q_DASH_DIST;
    foe->z = h->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(0);

    CHECK(h->x > start_x, "Q dashes the hero forward");
    CHECK(foe->hp < foe_hp_before, "Q damages the foe when the dash lands in range");
    CHECK(h->q_cooldown_ms == ARENA_UNICORN_Q_COOLDOWN_MS, "Q starts on cooldown after cast");
}

static void test_unicorn_q_respects_cooldown(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    arena_set_move_target(0, h->x + 4.0f, h->z);
    arena_cast_q(0);
    float x_after_first = h->x;
    arena_cast_q(0); /* should no-op, still on cooldown */
    CHECK(h->x == x_after_first, "Q does not re-cast while on cooldown");
}

static void test_unicorn_w_regen_toggle(void) {
    arena_init();
    /* S170-228: this test is about the W regen mechanic in isolation, not bot-vs-bot combat
       outcome -- disabled the same way test_movement_reaches_target above now does, since a
       full 1000ms tick is plenty of time for the trained-RL-driven bot to close distance and
       land real damage, which would otherwise mask (or outright reverse) the regen this test
       actually checks. */
    arena_bot_enabled = 0;
    ArenaHero *h = &arena_state.heroes[0];
    h->hp = 50; /* below max so regen has room to matter */
    arena_toggle_w(0);
    CHECK(h->w_active == 1, "W toggles on");
    arena_update(1000); /* 1 second of regen */
    CHECK(h->hp > 50, "W regenerates HP while active");
    arena_toggle_w(0);
    CHECK(h->w_active == 0, "W toggles back off");
    arena_bot_enabled = 1; /* restore the default for any test run after this one */
}

static void test_mp_starts_full(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    CHECK(h->mp == ARENA_MP_MAX, "a fresh hero starts with a full mana pool");
    CHECK(h->max_mp == ARENA_MP_MAX, "max_mp is set on init, not left at zero");
}

static void test_mp_regenerates_over_time(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    h->mp = 0;

    arena_update(1000); /* 1 second of regen */

    CHECK(h->mp > 0, "mana regenerates every tick with no cast at all");
    CHECK(h->mp <= ARENA_MP_MAX, "regen never overshoots the pool's own max");
}

static void test_mp_deducted_on_landed_q_cast(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    arena_set_move_target(0, h->x + 4.0f, h->z);
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = h->x + ARENA_UNICORN_Q_DASH_DIST;
    foe->z = h->z;
    int mp_before = h->mp;

    arena_cast_q(0);

    CHECK(h->mp == mp_before - ARENA_MP_COST_Q, "a landed Q spends exactly its own flat mana cost");
}

static void test_mp_blocks_cast_when_insufficient(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    arena_set_move_target(0, h->x + 4.0f, h->z);
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = h->x + ARENA_UNICORN_Q_DASH_DIST;
    foe->z = h->z;
    h->mp = ARENA_MP_COST_Q - 1; /* one short */
    float x_before = h->x;
    int foe_hp_before = foe->hp;

    arena_cast_q(0);

    CHECK(h->x == x_before, "insufficient mana blocks the cast entirely -- no dash");
    CHECK(foe->hp == foe_hp_before, "insufficient mana blocks the cast entirely -- no damage either");
    CHECK(h->q_cooldown_ms == 0, "a cast blocked by mana never starts its cooldown, same as a whiff");
}

/* S170-181, founder: "instead of initial mana cost toggle spells should drain mana over
 * time" -- activating a true toggle no longer charges ARENA_MP_COST_W up front; it drains
 * ARENA_MP_DRAIN_W_PER_SEC continuously via tick_hero_kit for as long as it stays on. */
static void test_mp_toggle_w_activates_free_drains_over_time(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    int mp_before = h->mp;

    arena_toggle_w(0);
    CHECK(h->w_active == 1, "W toggles on");
    CHECK(h->mp == mp_before, "activating a toggle no longer spends a flat cost up front");

    arena_update(1000); /* one full second of drain */
    CHECK(h->mp == mp_before - ARENA_MP_DRAIN_W_PER_SEC, "held on, it drains at the documented per-second rate");

    int mp_after_drain = h->mp;
    arena_toggle_w(0);
    CHECK(h->w_active == 0, "W toggles back off");
    CHECK(h->mp == mp_after_drain, "deactivating a toggle is free -- canceling isn't a new cast, and stops the drain");
    arena_update(1000);
    /* Out-of-combat mana regen (ARENA_MP_REGEN_PER_SEC) is free to apply once the toggle's
       own drain stops -- >= rather than == so this doesn't fight that separate, correct
       behavior; what actually matters here is that mp never goes back DOWN once w_active
       is off. */
    CHECK(h->mp >= mp_after_drain, "no further drain once toggled off");
}

static void test_mp_toggle_w_blocked_at_zero_mana(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    h->mp = 0;

    arena_toggle_w(0);

    CHECK(h->w_active == 0, "zero mana blocks activating a toggle -- there's nothing left to drain");
    CHECK(h->mp == 0, "a blocked activation doesn't go negative");
}

static void test_mp_toggle_w_auto_deactivates_when_drained_to_empty(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    h->mp = 2; /* less than one second's worth of drain */
    h->combat_timer_ms = 5000; /* in combat -- caps regen at the slow trickle (1/sec), well
                                   under the 5/sec drain rate, so mana actually runs out
                                   instead of the two rates roughly canceling out */

    arena_toggle_w(0);
    CHECK(h->w_active == 1, "W toggles on with only a little mana left -- activation itself is free now");

    arena_update(1000);

    CHECK(h->w_active == 0, "the toggle can't be held on for free once mana actually runs out -- auto-deactivates");
    CHECK(h->mp == 0, "mana is clamped at 0, never negative");
}

static void test_unicorn_r_doubles_armor_temporarily(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    float base_armor = arena_hero_armor(h);
    arena_cast_r(0);
    CHECK(arena_hero_armor(h) == base_armor * 2.0f, "R doubles armor while active");
    CHECK(h->r_cooldown_ms == ARENA_UNICORN_R_COOLDOWN_MS, "R starts on cooldown after cast");
    /* Advance past the buff's duration but not its cooldown. */
    arena_update(ARENA_UNICORN_R_DURATION_MS + 100);
    CHECK(arena_hero_armor(h) == base_armor, "R's armor buff expires after its duration");
}

static void test_unicorn_armor_reduces_incoming_damage(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    ArenaHero *bot = &arena_state.heroes[1];
    /* Default roster is Unicorn (player) vs Duck (bot, S170-31) -- Duck has
       no passive armor, same numeric result as the old "plain melee" bot
       had, but for a different reason now (a real kit with zero armor, not
       an absence of a kit). Confirm the hero's armor actually reduces what
       it takes, not just that armor is nonzero. */
    CHECK(arena_hero_armor(h) > 0.0f, "The Unicorn has nonzero passive armor");
    CHECK(arena_hero_armor(bot) == 0.0f, "The Duck has no passive armor");
}

/* --- The Duck's kit (docs/HEROES_VS0.md, EMILY/BACKLOG.md S170-31) --- */

static void test_duck_q_pulls_foe_and_damages(void) {
    arena_init(); /* player=Unicorn, bot=Duck */
    ArenaHero *duck = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = duck->x + 4.0f; /* within ARENA_DUCK_Q_RANGE */
    foe->z = duck->z;
    float foe_x_before = foe->x;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->x < foe_x_before, "Q pulls the foe toward the Duck");
    CHECK(foe->hp < foe_hp_before, "Q damages the foe when the pull lands in range");
    CHECK(duck->q_cooldown_ms == ARENA_DUCK_Q_COOLDOWN_MS, "Q starts on cooldown after cast");
}

static void test_duck_q_out_of_range_whiffs(void) {
    arena_init();
    ArenaHero *duck = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = duck->x + ARENA_DUCK_Q_RANGE + 5.0f; /* well beyond range */
    foe->z = duck->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does no damage");
    CHECK(duck->q_cooldown_ms == 0, "Q out of range does not consume its cooldown -- it whiffed, not cast");
}

static void test_duck_q_never_pulls_past_the_duck(void) {
    arena_init();
    ArenaHero *duck = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    /* Foe closer than the pull distance -- must stop at the Duck, not fly past it. */
    foe->x = duck->x + 1.0f;
    foe->z = duck->z;

    arena_cast_q(1);

    CHECK(fabsf(foe->x - duck->x) < 0.01f, "a close foe is pulled to the Duck's position, not past it");
}

static void test_duck_r_bigger_pull_and_damage_than_q(void) {
    /* Both sides Duck (no armor on either), so the damage dealt isn't
       confounded by the default foe (Unicorn) having passive armor. */
    arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_DUCK);
    ArenaHero *duck = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = duck->x + 8.0f; /* within R's range, beyond Q's */
    foe->z = duck->z;
    int foe_hp_before = foe->hp;

    arena_cast_r(1);

    CHECK(foe->hp == foe_hp_before - ARENA_DUCK_R_DAMAGE, "R deals its own (larger) damage amount");
    CHECK(duck->r_cooldown_ms == ARENA_DUCK_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
}

static void test_duck_has_no_w(void) {
    arena_init();
    ArenaHero *duck = &arena_state.heroes[1];
    /* Government Clearance needs objective structures that don't exist in
       this arena -- toggling W for a Duck must no-op, not crash or silently
       borrow Unicorn's regen-toggle behavior. */
    arena_toggle_w(1);
    CHECK(duck->w_active == 0, "toggling W for The Duck is a no-op -- it has no W in this arena");
}

static void test_hero_dispatch_is_by_hero_not_owner_slot(void) {
    /* S170-31's whole point: kit dispatch generalized away from S170-18's
       "owner 0 == Unicorn" hardcoding. Swap the roster and confirm Unicorn's
       kit still works correctly from owner slot 1. */
    arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_UNICORN);
    ArenaHero *unicorn = &arena_state.heroes[1];
    float base_armor = arena_hero_armor(unicorn);
    arena_cast_r(1);
    CHECK(arena_hero_armor(unicorn) == base_armor * 2.0f,
          "Unicorn's R still doubles armor when Unicorn is in owner slot 1, not slot 0");
}

/* --- The Ghost's kit (docs/HEROES_VS0.md, EMILY/BACKLOG.md S170-32) --- */

/* S170-140: Ghost's Q (Alien Frequency) converted from an instant hit to a
 * real projectile -- same test shape as Gary's Q (test_gary_q_*), plus one
 * extra check that its on-hit silence actually lands via the generic
 * on_hit_silence_ms field. */
static void test_ghost_q_cast_spawns_projectile_no_instant_effect(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GHOST_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before,
          "casting Q does not deal instant damage -- it fires a projectile instead");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].silenced_ms == 0, "no instant silence either -- it lands on hit, not on cast");
    ArenaProjectile *p = find_active_projectile();
    CHECK(p != NULL, "a projectile is actually spawned on cast");
    CHECK(arena_state.heroes[0].q_cooldown_ms == ARENA_GHOST_Q_COOLDOWN_MS,
          "cooldown is spent on cast, regardless of the shot's eventual outcome");
}

static void test_ghost_q_out_of_range_whiffs_no_projectile(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GHOST_Q_RANGE + 5.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_q(0);

    CHECK(find_active_projectile() == NULL, "no projectile spawns when no foe is in range at cast time");
    CHECK(arena_state.heroes[0].q_cooldown_ms == 0, "an out-of-range whiff doesn't consume the cooldown");
}

static void test_ghost_q_projectile_damages_and_silences_on_hit(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GHOST_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before, "a stationary target is hit once the projectile travels far enough to reach it");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].silenced_ms == ARENA_GHOST_Q_SILENCE_MS, "Q silences the foe on a landed hit, carried by the projectile's on_hit_silence_ms");
    CHECK(find_active_projectile() == NULL, "the projectile deactivates on hit, doesn't linger");
}

static void test_ghost_q_projectile_misses_and_no_silence_if_target_steps_off_line(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GHOST_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    arena_state.heroes[ARENA_TEAM_SIZE].z = 10.0f; /* real dodge -- steps off the firing line */
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "a target that dodges takes no damage");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].silenced_ms == 0, "...and isn't silenced either -- the whole effect rides the hit, not the cast");
}

static void test_silenced_hero_cannot_cast(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *unicorn = &arena_state.heroes[0];
    unicorn->silenced_ms = 500;
    float x_before = unicorn->x;

    arena_cast_q(0); /* Unicorn's Q would normally dash it forward */

    CHECK(unicorn->x == x_before, "a silenced hero's Q cast is a no-op");
    CHECK(unicorn->q_cooldown_ms == 0, "the no-op cast does not consume a cooldown either");
}

static void test_ghost_w_grants_intangibility_and_expires(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];

    arena_toggle_w(1);
    CHECK(ghost->intangible_ms == ARENA_GHOST_W_INTANGIBLE_MS, "W grants intangibility");
    CHECK(ghost->w_cooldown_ms == ARENA_GHOST_W_COOLDOWN_MS, "W starts on its own cooldown");

    arena_update(ARENA_GHOST_W_INTANGIBLE_MS + 100);
    CHECK(ghost->intangible_ms == 0, "intangibility expires after its duration");
}

static void test_intangible_hero_cannot_be_hit(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *unicorn = &arena_state.heroes[0];
    ArenaHero *ghost = &arena_state.heroes[1];
    ghost->intangible_ms = 1000;
    /* Adjacent, in auto-attack range, so resolve_combat would normally hit. */
    ghost->x = unicorn->x + 0.5f;
    ghost->z = unicorn->z;
    int ghost_hp_before = ghost->hp;

    arena_update(16);

    CHECK(ghost->hp == ghost_hp_before, "an intangible hero takes no auto-attack damage");
}

/* S170-144: "ensure aoe damage spells hit creeps" -- AoE zone/aura ticks now hit node-guardian and
 * lane creeps too, not just heroes. */
static void test_ghost_r_zone_damages_enemy_node_guardian(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    /* S170-161: team creeps now spawn at their team's graveyard and march
       toward any node their team doesn't own -- team 1 owns every node
       here so its creep has nowhere to march, staying put at the
       graveyard for the whole test. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 2; /* team 1 owns everything */
    arena_tick_creeps(16); /* spawn */
    float gx, gz;
    arena_graveyard_position(1, &gx, &gz);
    /* Within zone radius but outside melee attack range -- isolates this to
       the zone-damage path, distinct from the existing, separate
       arena_hero_attack_creeps melee mechanic (see the sibling
       "does not damage own team" test's own comment for why this matters). */
    arena_state.heroes[0].x = gx + 3.0f;
    arena_state.heroes[0].z = gz;
    int hp_before = arena_state.creeps[0].hp;

    arena_cast_r(0);
    arena_update_teams(1000); /* one full zone tick */

    CHECK(arena_state.creeps[0].hp < hp_before, "Ghost's R zone damages an enemy team-flavored node-guardian creep standing in it");
}

static void test_ghost_r_zone_does_not_damage_own_team_node_guardian(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    /* S170-161: team 0 owns everything -- its creep has nowhere to march,
       stays at its graveyard spawn for the whole test. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team 0 owns everything */
    arena_tick_creeps(16);
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    /* Positioned within the zone radius (ARENA_GHOST_R_RADIUS) but OUTSIDE
       melee attack range (ARENA_ATTACK_RANGE) of the creep -- isolates this
       to the zone-damage path specifically, since a hero standing directly
       on top of a node-guardian creep would also melee-auto-attack it via the
       existing, separate arena_hero_attack_creeps mechanic (which lets any
       hero attack any creep regardless of flavor; only the reward differs). */
    arena_state.heroes[0].x = gx + 3.0f;
    arena_state.heroes[0].z = gz;
    int hp_before = arena_state.creeps[0].hp;

    arena_cast_r(0);
    arena_update_teams(1000);

    CHECK(arena_state.creeps[0].hp == hp_before, "Ghost's R zone does not damage the caster's own team's node-guardian creep");
}

static void test_ghost_r_zone_damages_enemy_lane_creep(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    ArenaLaneCreep *lc = &arena_state.lane_creeps[0];
    lc->active = 1; lc->alive = 1; lc->team = 1; /* enemy lane creep */
    lc->hp = lc->max_hp = ARENA_LANE_CREEP_HP;
    /* Within zone radius but outside melee attack range -- isolates this to
       the zone-damage path, distinct from arena_hero_attack_lane_creeps. */
    lc->x = 3.0f; lc->z = 0;

    arena_cast_r(0);
    arena_update_teams(1000);

    CHECK(lc->hp < ARENA_LANE_CREEP_HP, "Ghost's R zone damages an enemy lane creep standing in it");
}

static void test_pizza_aura_damages_enemy_node_guardian(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_PIZZA;
    /* S170-161: team 1 owns everything -- its creep has nowhere to march. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 2;
    arena_tick_creeps(16);
    float gx, gz;
    arena_graveyard_position(1, &gx, &gz);
    /* Within the aura radius (ARENA_PIZZA_AURA_RADIUS) but outside melee
       attack range -- isolates this to the aura-damage path, distinct from
       arena_hero_attack_creeps. */
    arena_state.heroes[0].x = gx + 3.0f;
    arena_state.heroes[0].z = gz;
    int hp_before = arena_state.creeps[0].hp;

    arena_update_teams(1000); /* Pizza's aura is always-on, no cast needed */

    CHECK(arena_state.creeps[0].hp < hp_before, "Pizza's always-on burn aura damages a nearby enemy node-guardian creep, not just heroes");
}

static void test_ghost_r_zone_damages_foe_over_time(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = ghost->x + 3.0f;
    foe->z = ghost->z;
    int foe_hp_before = foe->hp;

    arena_cast_r(1);
    CHECK(ghost->r_active_ms == ARENA_GHOST_R_DURATION_MS, "R starts its zone duration on cast");
    CHECK(ghost->r_cooldown_ms == ARENA_GHOST_R_COOLDOWN_MS, "R starts on its own cooldown after cast");

    /* Ghost occupies owner slot 1 ("the bot"), so this same arena_update
       call also runs the bot brain, which may chase into melee range and
       land an auto-attack in the same tick -- an inequality, not exact
       equality, so this test isn't fragile against that separate, correct
       behavior. What it actually proves either way: the zone dealt at
       least its own DPS-worth of damage. */
    arena_update(1000); /* one full zone tick */
    CHECK(foe->hp <= foe_hp_before - ARENA_GHOST_R_DPS,
          "the zone deals at least one DPS-worth of damage per second the foe stands in it");
}

static void test_ghost_r_zone_stays_fixed_when_foe_moves_away(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = ghost->x + 3.0f;
    foe->z = ghost->z;

    arena_cast_r(1);
    /* Foe steps outside the zone radius right after the cast. */
    foe->x = ghost->x + ARENA_GHOST_R_RADIUS + 5.0f;
    int foe_hp_before = foe->hp;

    arena_update(1000);

    CHECK(foe->hp == foe_hp_before, "a foe standing outside the fixed zone takes no zone damage");
}

/* --- The Frog's kit (docs/HEROES_VS0.md, EMILY/BACKLOG.md S170-33) --- */

static void test_frog_q_rewinds_position_and_hp(void) {
    arena_init_with_heroes(ARENA_HERO_FROG, ARENA_HERO_UNICORN);
    ArenaHero *frog = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    /* Placed at opposite extremes so the bot-controlled foe's chase (S170-31's
       heuristic AI, unrelated to this test) can't close to melee range
       during the history-building window and confound the HP value. */
    frog->x = -12.0f; frog->z = 0.0f;
    foe->x = 12.0f; foe->z = 0.0f;

    /* Build more than 3s of loopback history at this position/HP. */
    for (int i = 0; i < 14; i++) arena_update(250); /* 14 * 250ms = 3500ms */
    float historical_x = frog->x;
    int historical_hp = frog->hp;

    /* Simulate a fight happening after the history was recorded. */
    frog->x = -2.0f;
    frog->hp = 30;

    arena_cast_q(0);

    CHECK(frog->hp == historical_hp, "Q restores HP from ~3s ago");
    CHECK(fabsf(frog->x - historical_x) < 0.01f, "Q restores position from ~3s ago");
    CHECK(frog->q_cooldown_ms == ARENA_FROG_Q_COOLDOWN_MS, "Q starts on cooldown after cast");
}

static void test_frog_q_uses_oldest_available_history_before_3s_elapsed(void) {
    arena_init_with_heroes(ARENA_HERO_FROG, ARENA_HERO_UNICORN);
    ArenaHero *frog = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    frog->x = -12.0f; foe->x = 12.0f;

    arena_update(250); /* exactly one sample, well under the 3s window */
    int historical_hp = frog->hp;

    frog->hp = 10; /* simulate damage */
    arena_cast_q(0);

    CHECK(frog->hp == historical_hp,
          "with less than 3s of history, Q rewinds to the oldest sample available instead of refusing to cast");
}

static void test_frog_r_vanishes(void) {
    arena_init_with_heroes(ARENA_HERO_FROG, ARENA_HERO_UNICORN);
    ArenaHero *frog = &arena_state.heroes[0];

    arena_cast_r(0);

    CHECK(frog->intangible_ms == ARENA_FROG_R_VANISH_MS, "R grants intangibility for the vanish duration");
    CHECK(frog->r_cooldown_ms == ARENA_FROG_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
}

static void test_frog_w_noop_in_1v1_no_ally(void) {
    /* Borrowed Time is wired for real now (S170-45, arena_nearest_ally) --
       this is no longer "unimplemented," it's a real no-op because 1v1
       genuinely has no teammate to target, same as Ghost's R ally-heal
       side and Doc Wheel's whole kit in this same mode. */
    arena_init_with_heroes(ARENA_HERO_FROG, ARENA_HERO_UNICORN);
    ArenaHero *frog = &arena_state.heroes[0];
    int cooldown_before = frog->w_cooldown_ms;

    arena_toggle_w(0);

    CHECK(frog->w_active == 0 && frog->intangible_ms == 0,
          "toggling W for The Frog leaves w_active/intangible_ms untouched -- Borrowed Time targets an ally, not self");
    CHECK(frog->w_cooldown_ms == cooldown_before,
          "no ally in 1v1 means the cast whiffs -- cooldown is not consumed");
}

/* Regression test, found live 2026-07-24 (NORTHSTAR §13, the MOBA-is-the-
 * product pivot): arena_bot_enabled was added to stop the internal bot from
 * *moving* owner 1 once a real second player connects (apps/arena_server),
 * but bot_cast_kit_if_ready (ability casts -- including Duck's Q, which
 * pulls the foe) was still being called unconditionally. A real second
 * player's hero would still get yanked around and attacked by the "disabled"
 * bot. Confirmed live against a real arena_server with zero clients
 * connected: owner 0 moved and took damage despite never sending a move
 * command, because Duck's Q kept firing. Fixed by gating the kit-cast call
 * the same way as the movement call. */
static void test_arena_bot_enabled_gates_kit_casts_too(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    arena_bot_enabled = 0;
    /* Put the Duck (owner 1) in range of the Unicorn (owner 0) with its Q
       off cooldown -- if kit-casting weren't gated, this alone would pull
       and damage owner 0 within a handful of ticks. z=15 keeps both heroes
       clear of every ArenaNode's node-guardian-creep aggro radius (S170-119: the
       map's center node now sits at (0,0), which this test used to use
       directly -- a creep spawning on the hero would confound this test's
       own signal with an unrelated system). */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 15.0f;
    arena_state.heroes[1].x = 3.0f; arena_state.heroes[1].z = 15.0f;
    int start_hp = arena_state.heroes[0].hp;
    float start_x = arena_state.heroes[0].x;

    for (int i = 0; i < 200; i++) arena_update(16); /* 3.2s of sim time */

    CHECK(arena_state.heroes[0].hp == start_hp,
          "with arena_bot_enabled=0, the bot's kit-cast AI never damages owner 0 (no real input sent)");
    CHECK(arena_state.heroes[0].x == start_x,
          "with arena_bot_enabled=0, owner 0 is never pulled/moved by the bot's kit AI either");
    arena_bot_enabled = 1; /* restore the default for any test run after this one */
}

/* ---- Team mode (10v10), 2026-07-24, NORTHSTAR §13 cont'd ---- */

static void test_arena_init_teams_sets_up_both_sides(void) {
    arena_init_teams();
    int team0 = 0, team1 = 0;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        CHECK(arena_state.heroes[i].active, "every one of the 20 slots is active in team mode");
        CHECK(arena_state.heroes[i].alive, "every slot starts alive");
        if (arena_state.heroes[i].team == 0) team0++; else team1++;
    }
    CHECK(team0 == ARENA_TEAM_SIZE && team1 == ARENA_TEAM_SIZE,
          "exactly ARENA_TEAM_SIZE heroes on each team");
}

static void test_nearest_enemy_finds_closest_on_other_team(void) {
    arena_init_teams();
    /* Owner 0 (team 0) -- put two team-1 heroes at different distances. */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 5; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;     /* far */
    arena_state.heroes[ARENA_TEAM_SIZE + 1].x = 1; arena_state.heroes[ARENA_TEAM_SIZE + 1].z = 0; /* near */

    ArenaHero *nearest = arena_nearest_enemy(0);
    CHECK(nearest == &arena_state.heroes[ARENA_TEAM_SIZE + 1],
          "arena_nearest_enemy picks the closer of two enemies on the other team");
}

static void test_nearest_enemy_ignores_teammates_and_dead_heroes(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    /* A teammate right next to owner 0 should never be picked. */
    arena_state.heroes[1].x = 0.1f; arena_state.heroes[1].z = 0;
    /* The nearest enemy is dead -- should be skipped in favor of a living one further out. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.5f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 0;
    arena_state.heroes[ARENA_TEAM_SIZE + 1].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE + 1].z = 0;

    ArenaHero *nearest = arena_nearest_enemy(0);
    CHECK(nearest == &arena_state.heroes[ARENA_TEAM_SIZE + 1],
          "arena_nearest_enemy skips teammates entirely and dead heroes on the enemy team");
}

static void test_team_melee_converges_multiple_attackers_on_one_target(void) {
    arena_init_teams();
    /* Deactivate everyone except: owner 0 + owner 1 (team 0), and one lone
       team-1 hero within melee range of both -- a real "two attackers, one
       target" team-fight case the old 1v1 pairwise resolve_combat never had
       to express. */
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;

    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1.0f; arena_state.heroes[1].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.5f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    /* S170-204: a first tick only BEGINS the windup now (no more instant damage on the tick
       range/cooldown are first satisfied) -- a second tick worth the full windup duration
       actually lands the hit. */
    arena_update_teams(16);
    arena_update_teams((unsigned int)ARENA_ATTACK_WINDUP_MS);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < 100,
          "the lone enemy hero takes damage from being in melee range of two attackers at once");
}

/* S170-204, NORTHSTAR §17.1 -- LoL parity for auto-attacks: "does the champion stop when
 * auto-attacking? yes." These tests exercise the windup/backswing state machine directly: no
 * instant damage the moment range+cooldown are satisfied, damage lands only once the windup
 * genuinely elapses, a real reposition cancels it outright (no damage, no cooldown spent), and
 * the bot AI's own noisy re-affirmation of "stay roughly here" does NOT spuriously cancel it
 * (the whole reason the cancel check compares against the hero's own attack range, not a bare
 * position-changed flag -- see arena_set_move_target's own doc comment). */

static void test_melee_windup_begins_with_no_instant_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_update_teams(16);

    CHECK(arena_state.heroes[0].attack_windup_ms_remaining > 0, "a fresh attack begins a real windup, not an instant hit");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "no damage yet -- the swing hasn't landed");
}

static void test_melee_windup_completes_and_deals_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* 0 base armor -- exact hit-damage math */
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_update_teams(16);
    arena_update_teams((unsigned int)ARENA_ATTACK_WINDUP_MS);

    CHECK(arena_state.heroes[0].attack_windup_ms_remaining == 0, "the windup clears itself once it completes");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before - ARENA_ATTACK_DAMAGE,
          "the full windup elapsing with nothing interrupting it lands the hit");
    CHECK(arena_state.heroes[0].attack_cooldown_ms == ARENA_ATTACK_COOLDOWN_MS, "the full cooldown starts only once the swing actually fires");
}

static void test_melee_windup_canceled_by_a_real_reposition(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_update_teams(16);
    CHECK(arena_state.heroes[0].attack_windup_ms_remaining > 0, "sanity: windup is in progress");

    /* A real retreat -- well beyond the hero's own attack range of where it's currently
       standing, the same "genuinely asking to go somewhere else" test NORTHSTAR §17.1 calls
       for. Checked immediately, not after further ticking: walking the full 20 units away takes
       real simulated time (ARENA_HERO_SPEED is finite), so the hero would still be in range for
       a few more ticks and could legitimately start a brand-new attack cycle against the same
       foe in the meantime -- a separate, correct behavior this test isn't trying to exercise.
       What this test checks is narrower and unambiguous: the ORIGINAL canceled swing itself
       cost nothing. */
    arena_set_move_target(0, arena_state.heroes[0].x + 20.0f, arena_state.heroes[0].z);
    CHECK(arena_state.heroes[0].attack_windup_ms_remaining == 0, "a real move command cancels the windup outright");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "a canceled swing deals no damage");
    CHECK(arena_state.heroes[0].attack_cooldown_ms == 0, "no cooldown penalty for a canceled swing -- free to reattempt immediately");
}

static void test_melee_windup_survives_bot_ai_noise(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* 0 base armor -- exact hit-damage math */
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_update_teams(16);
    CHECK(arena_state.heroes[0].attack_windup_ms_remaining > 0, "sanity: windup is in progress");

    /* apps/arena_bot's own engage branch re-sends a move command every ~100ms decision tick
       even while already in melee range, as part of its approach-angle positioning -- a target
       barely within the hero's own attack range of where it's ALREADY standing, not a genuine
       "go elsewhere." Without the range-gated comparison in arena_set_move_target, this exact
       pattern would cancel every windup before it could ever complete, silently breaking melee
       damage for every bot-controlled hero in a real match. */
    arena_set_move_target(0, arena_state.heroes[0].x + 0.1f, arena_state.heroes[0].z);
    CHECK(arena_state.heroes[0].attack_windup_ms_remaining > 0, "a move command that barely differs from the hero's own current position does not cancel the windup");

    arena_update_teams((unsigned int)ARENA_ATTACK_WINDUP_MS);
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before - ARENA_ATTACK_DAMAGE,
          "the swing still completes and lands normally despite the bot AI's own repeated noisy move commands");
}

static void test_melee_windup_canceled_by_stun(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_update_teams(16);
    CHECK(arena_state.heroes[0].attack_windup_ms_remaining > 0, "sanity: windup is in progress");

    arena_state.heroes[0].stunned_ms = 500;
    arena_update_teams((unsigned int)ARENA_ATTACK_WINDUP_MS);

    CHECK(arena_state.heroes[0].attack_windup_ms_remaining == 0, "a stun landing mid-windup interrupts it");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "an interrupted swing deals no damage");
}

static void test_team_wipe_alone_does_not_win_the_match(void) {
    /* S170-153: team-wipe was the ORIGINAL win condition (S170-45) but was
       replaced by the Arathi-Basin-style resource race -- a wiped team can
       still come back via graveyard/node respawns and isn't eliminated
       just because every hero happens to be down or deactivated right now. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* Only owner 0 (team 0) is left active and alive -- team 1 is wiped. */
    arena_state.heroes[1].active = 0;

    arena_update_teams(16);

    CHECK(arena_state.winner == 0, "team 1 being wiped no longer ends the match on its own -- resources decide it now");
}

/* S170-45: allies. arena_nearest_ally is the enabling primitive for every
 * ally-targeted kit piece previously skipped for having no target in 1v1
 * (Ghost's R heal side, Frog's W, Doc Wheel's entire kit). */

static void test_nearest_ally_finds_closest_teammate(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 5; arena_state.heroes[1].z = 0;  /* far teammate */
    arena_state.heroes[2].x = 1; arena_state.heroes[2].z = 0;  /* near teammate */

    ArenaHero *nearest = arena_nearest_ally(0);
    CHECK(nearest == &arena_state.heroes[2], "arena_nearest_ally picks the closer of two teammates");
}

static void test_nearest_ally_ignores_enemies_and_dead_teammates(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    /* An enemy right next to owner 0 should never be picked. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.1f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    /* The nearest teammate is dead -- should be skipped in favor of a living one further out. */
    arena_state.heroes[1].x = 0.5f; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].alive = 0;
    arena_state.heroes[2].x = 3.0f; arena_state.heroes[2].z = 0;

    ArenaHero *nearest = arena_nearest_ally(0);
    CHECK(nearest == &arena_state.heroes[2],
          "arena_nearest_ally skips enemies entirely and dead teammates");
}

static void test_nearest_ally_never_returns_self(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[1].x = 0; arena_state.heroes[1].z = 0; /* exactly on top of owner 0 */

    ArenaHero *nearest = arena_nearest_ally(0);
    CHECK(nearest == &arena_state.heroes[1] && nearest != &arena_state.heroes[0],
          "arena_nearest_ally never returns owner itself, even at distance 0 from another candidate");
}

static void test_nearest_ally_null_in_1v1(void) {
    /* 1v1 (arena_init_with_heroes) sets heroes[0].team=0, heroes[1].team=1 --
       no teammate exists at all, so every ally-targeted kit piece must
       degrade to a safe no-op here, not crash. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    CHECK(arena_nearest_ally(0) == NULL, "arena_nearest_ally returns NULL in 1v1 -- no teammate exists");
}

static void test_ghost_r_zone_heals_ally_in_team_mode(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 0; arena_state.heroes[1].z = 0; /* ally, inside the zone radius */
    arena_state.heroes[1].hp = 50; arena_state.heroes[1].max_hp = 100;

    arena_cast_r(0);
    /* One full 1000ms zone tick, via the public per-tick entry point --
       tick_hero_kit itself is static to arena_game.c. */
    arena_update_teams(1000);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_GHOST_R_DPS,
          "Recital's ally-heal side heals a teammate standing in the zone");
}

static void test_ghost_r_zone_does_not_heal_ally_outside_radius(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = ARENA_GHOST_R_RADIUS * 3.0f; arena_state.heroes[1].z = 0; /* well outside */
    arena_state.heroes[1].hp = 50; arena_state.heroes[1].max_hp = 100;

    arena_cast_r(0);
    arena_update_teams(1000);

    CHECK(arena_state.heroes[1].hp == 50, "Recital's ally-heal side does not reach an ally outside the zone radius");
}

static void test_frog_w_refunds_ally_next_cast_cooldown(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FROG;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].hero_id = ARENA_HERO_UNICORN;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0;
    /* Unicorn's Q dashes toward its move target if moving, else toward a
       foe -- neither exists by default in this isolated 2-hero setup, so
       give it a move target or the dash (and thus the cooldown-setting
       code path) never actually runs. */
    arena_state.heroes[1].moving = 1;
    arena_state.heroes[1].target_x = 5; arena_state.heroes[1].target_z = 0;

    arena_toggle_w(0); /* Frog's Borrowed Time on the nearest ally (owner 1) */
    CHECK(arena_state.heroes[1].next_cast_refund == 1,
          "Borrowed Time places the refund buff on the nearest ally, not the caster");

    arena_cast_q(1); /* Unicorn's Q would normally set a long cooldown */
    CHECK(arena_state.heroes[1].q_cooldown_ms == 0,
          "the buffed ally's next cast is refunded to zero cooldown");
    CHECK(arena_state.heroes[1].next_cast_refund == 0,
          "the refund buff is consumed after one cast, not reusable");
}

static void test_frog_w_whiffs_with_no_ally_cooldown_not_consumed(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FROG;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].w_cooldown_ms == 0,
          "Borrowed Time whiffs with no living ally -- cooldown is not consumed");
}

static void test_doc_wheel_q_heals_more_at_lower_hp(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].max_hp = 100;
    arena_state.heroes[1].hp = 95; /* near-full HP */

    arena_cast_q(0);
    int healed_near_full = arena_state.heroes[1].hp - 95;

    /* Reset and try again from low HP. */
    arena_state.heroes[0].q_cooldown_ms = 0;
    arena_state.heroes[1].hp = 10; /* near-empty HP */
    arena_cast_q(0);
    int healed_near_empty = arena_state.heroes[1].hp - 10;

    CHECK(healed_near_empty > healed_near_full,
          "Bedside Manner heals more the lower the target's current HP%% is");
}

static void test_doc_wheel_q_cleanses_silence(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 100;
    arena_state.heroes[1].silenced_ms = 2000;

    arena_cast_q(0);

    CHECK(arena_state.heroes[1].silenced_ms == 0, "Bedside Manner cleanses an active silence");
}

static void test_doc_wheel_q_whiffs_with_no_ally_cooldown_not_consumed(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;

    arena_cast_q(0);

    CHECK(arena_state.heroes[0].q_cooldown_ms == 0,
          "Bedside Manner whiffs with no living ally -- cooldown is not consumed");
}

static void test_doc_wheel_w_teleports_to_ally(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 7.5f; arena_state.heroes[1].z = -3.0f;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].x == 7.5f && arena_state.heroes[0].z == -3.0f,
          "House Call teleports Doc Wheel to the nearest ally's exact position");
}

static void test_doc_wheel_r_heals_allies_in_radius_only(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* in radius */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[2].x = ARENA_DOC_WHEEL_R_RADIUS * 3.0f; arena_state.heroes[2].z = 0; /* out of radius */
    arena_state.heroes[2].max_hp = 100; arena_state.heroes[2].hp = 50;

    arena_cast_r(0);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_DOC_WHEEL_R_HEAL,
          "No Combat Power heals an ally within radius");
    CHECK(arena_state.heroes[2].hp == 50,
          "No Combat Power does not reach an ally outside radius");
}

static void test_doc_wheel_r_consumes_cooldown_even_with_zero_allies(void) {
    /* A real ultimate commitment, not a whiff-refunded poke -- unlike Q,
       which no-ops (and doesn't spend its cooldown) with no ally, R always
       "lands" per its own header comment. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;

    arena_cast_r(0);

    CHECK(arena_state.heroes[0].r_cooldown_ms == ARENA_DOC_WHEEL_R_COOLDOWN_MS,
          "No Combat Power consumes its cooldown even when zero allies are in range");
}

/* S170-46: territory/node system + Tree, Pizza, and merged Flamel (absorbed
 * former Druid). */

static void test_node_channel_starts_and_flips_node_neutral_immediately(void) {
    /* Node starts owned by team 1 (as if team 0 had already captured team
       1's home node in some earlier state) -- team 1 shows up alone and
       begins a channel. The node must go neutral the instant the channel
       starts, not stay owned by team 1 until the channel finishes -- this
       is the "neutral period... as you wait for it to finish capturing"
       the founder asked for. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.nodes[0].owner = 2;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].owner == 0,
          "a node flips to neutral the instant a lone team begins channeling it, before the channel finishes");
    CHECK(arena_state.nodes[0].capturing_team == 0, "the channel is now attributed to the team that started it");
}

static void test_node_channel_completes_to_capturing_team(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(ARENA_NODE_CAPTURE_CHANNEL_MS - 1);
    CHECK(arena_state.nodes[0].owner == 0, "the node is still neutral one tick before the channel completes");

    arena_tick_nodes(1);
    CHECK(arena_state.nodes[0].owner == 1, "the node flips to the channeling team's ownership once the channel completes");
    CHECK(arena_state.nodes[0].capturing_team == -1, "the channel clears once it completes, ready for the next contest");
}

static void test_node_channel_interrupted_by_mixed_presence_loses_all_progress(void) {
    /* Team 0 channels alone for a while, then an enemy shows up --
       "interruptable": progress is lost entirely, and since the node had
       already flipped neutral, it STAYS neutral rather than reverting to
       whatever it was before -- the actual teeth behind "losing due to
       ignoring the objective." */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(ARENA_NODE_CAPTURE_CHANNEL_MS / 2);
    CHECK(arena_state.nodes[0].capture_progress_ms > 0, "the channel has real progress partway through");

    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x;
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;
    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == -1, "an enemy showing up interrupts the channel");
    CHECK(arena_state.nodes[0].capture_progress_ms == 0, "all progress is lost on interrupt, not preserved");
    CHECK(arena_state.nodes[0].owner == 0,
          "the node stays neutral after an interrupt -- it is not handed back to the original owner for free");
}

static void test_node_channel_interrupted_when_capturing_team_leaves(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(ARENA_NODE_CAPTURE_CHANNEL_MS / 2);
    CHECK(arena_state.nodes[0].capturing_team == 0, "team 0 is channeling");

    arena_state.heroes[0].x = 1000.0f; arena_state.heroes[0].z = 1000.0f;
    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == -1, "the channel is interrupted once the channeling team leaves");
    CHECK(arena_state.nodes[0].capture_progress_ms == 0, "leaving loses all progress, same as being contested");
}

static void test_node_already_owned_by_present_team_has_no_channel(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.nodes[0].owner = 1; /* already team 0's -- standing on your own node shouldn't spin up a channel */
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == -1, "no channel runs on a node the present team already owns");
    CHECK(arena_state.nodes[0].owner == 1, "owner is unchanged since there was nothing to capture");
}

/* Node towers (2026-07-30, founder: "add towers around the nodes so beginning of game is a
 * little slower"). arena_towers_reset() is called explicitly in each of these -- unlike
 * arena_creeps_reset, it is deliberately NOT part of arena_init_teams() itself (see that
 * function's own doc comment), so every OTHER test above/below that calls arena_init_teams()
 * without also calling this keeps seeing towers at their memset-zero default (alive=0, a no-op),
 * exactly the pre-tower behavior they were already written against. */
static void test_tower_blocks_capture_while_alive(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_towers_reset();
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(ARENA_NODE_CAPTURE_CHANNEL_MS);

    CHECK(arena_state.towers[0].alive, "sanity: the tower is alive");
    CHECK(arena_state.nodes[0].capturing_team == -1, "no channel starts while the node's tower is alive");
    CHECK(arena_state.nodes[0].capture_progress_ms == 0, "no progress accrues while blocked by a tower");
    CHECK(arena_state.nodes[0].owner == 0, "the node never flips while its tower stands, even after a full channel's worth of time");
}

static void test_tower_destroyed_removes_capture_block(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_towers_reset();
    arena_state.towers[0].alive = 0; /* as if it had already been destroyed */
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(ARENA_NODE_CAPTURE_CHANNEL_MS);

    CHECK(arena_state.nodes[0].owner == 1, "once the tower is gone, the node captures exactly like it always could");
}

static void test_hero_attack_towers_damages_and_kills_it(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_towers_reset();
    arena_state.towers[0].hp = arena_state.towers[0].max_hp = ARENA_ATTACK_DAMAGE; /* exactly one hit from dying */
    arena_state.heroes[0].x = arena_state.towers[0].x;
    arena_state.heroes[0].z = arena_state.towers[0].z;
    arena_state.heroes[0].attack_cooldown_ms = 0;

    arena_hero_attack_towers(16);

    CHECK(!arena_state.towers[0].alive, "a tower dies once a hero's attack exhausts its HP");
    CHECK(arena_state.heroes[0].flow >= ARENA_TOWER_KILL_FLOW, "the killer is paid the tower kill bounty");
}

static void test_tower_attacks_nearby_hero(void) {
    /* 2026-07-30, founder: "show the tower damage as projectiles" -- arena_tick_towers now only
       spawns a travelling ArenaProjectile, it no longer applies damage instantly; a second
       arena_tick_projectiles call is what actually resolves the hit, same two-step shape every
       other projectile-based attack in this file already has. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_towers_reset();
    arena_state.heroes[0].x = arena_state.towers[0].x;
    arena_state.heroes[0].z = arena_state.towers[0].z;
    int hp_before = arena_state.heroes[0].hp;

    arena_tick_towers(16);
    arena_tick_projectiles(16);

    CHECK(arena_state.heroes[0].hp < hp_before, "a living tower's projectile hits a hero standing in its aggro radius");
}

static void test_hero_damages_tower_even_with_creep_alive_at_same_spot(void) {
    /* 2026-07-30, founder real-time: "towers are basically invincible and can never be
       destroyed." Real bug: the node-guardian creep at this same node sits at the exact same
       (x,z) as the tower, and arena_hero_attack_creeps (which runs first every tick) always won
       the hero's once-per-cooldown attack whenever the creep was alive, permanently starving the
       tower of any damage. Fixed by never letting that creep spawn while the tower still stands
       (see arena_tick_creeps' own doc comment) -- this test proves the fix: tick creeps first (so
       a creep would exist here if the bug were still present), then confirm the creep never
       actually spawned and a hero's attack lands on the tower instead. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_towers_reset();
    arena_state.heroes[0].x = arena_state.towers[0].x;
    arena_state.heroes[0].z = arena_state.towers[0].z;
    arena_state.heroes[0].attack_cooldown_ms = 0;
    int tower_hp_before = arena_state.towers[0].hp;

    arena_tick_creeps(16); /* would spawn the neutral creep here if the starvation bug still existed */
    CHECK(!arena_state.creeps[0].alive, "the node-guardian creep does not spawn while its node's tower is still alive");

    arena_hero_attack_creeps(16); /* no-op: nothing alive to attack */
    arena_hero_attack_towers(16);

    CHECK(arena_state.towers[0].hp < tower_hp_before, "a hero's attack lands on the tower, unblocked by a co-located creep");
}

static void test_tree_doubles_channel_speed(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TREE;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(1000);

    CHECK(arena_state.nodes[0].capture_progress_ms == (int)(1000.0f * ARENA_TREE_CHANNEL_SPEED_MULT),
          "Root Network: a Tree on the channeling team doubles capture progress this tick");
}

static void test_flamel_mark_speeds_up_channel_on_marked_ground(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.nodes[0].marked_by_team = 0;

    arena_tick_nodes(1000);

    CHECK(arena_state.nodes[0].capture_progress_ms == 1000 + ARENA_FLAMEL_MARK_CHANNEL_BONUS_MS,
          "Overgrowth: capturing on ground marked by the capturing team's own Flamel finishes faster");
}

static void test_pizza_corrupts_any_channel_regardless_of_side(void) {
    /* A Pizza on the SAME team as the sole capturer still corrupts the
       attempt -- corruption doesn't pick a side, matching the doc's
       original "regardless of team composition" framing. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(1000);
    CHECK(arena_state.nodes[0].capturing_team == 0, "team 0 channels normally with no Pizza around");

    arena_state.heroes[0].hero_id = ARENA_HERO_PIZZA;
    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == -1,
          "a Pizza's presence corrupts the channel even on her own team's attempt");
}

static void test_tree_q_roots_and_damages_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TREE;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* no armor, so damage isn't reduced */
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_TREE_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_TREE_Q_DAMAGE,
          "Vine Lash damages an enemy in range");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == ARENA_TREE_Q_ROOT_MS,
          "Vine Lash roots the enemy it hits");
}

static void test_tree_q_out_of_range_whiffs(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TREE;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_TREE_Q_RANGE * 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_q(0);

    CHECK(arena_state.heroes[0].q_cooldown_ms == 0, "Vine Lash whiffs out of range -- cooldown is not consumed");
}

static void test_tree_r_self_roots_grants_armor_and_heals(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TREE;
    arena_state.heroes[0].hp = 50; arena_state.heroes[0].max_hp = 100;

    arena_cast_r(0);

    CHECK(arena_state.heroes[0].rooted_ms == ARENA_TREE_R_ROOT_MS, "Grand Secret self-roots the Tree");
    CHECK(arena_state.heroes[0].hp == 50 + ARENA_TREE_R_HEAL, "Grand Secret heals the Tree");
    CHECK(arena_hero_armor(&arena_state.heroes[0]) == (float)ARENA_TREE_R_ARMOR_BONUS,
          "Grand Secret grants the armor bonus while active");
}

static void test_tree_r_makes_immune_to_duck_pull(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_TREE;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;
    arena_cast_r(ARENA_TEAM_SIZE); /* Tree self-roots first */
    float rooted_x = arena_state.heroes[ARENA_TEAM_SIZE].x;

    arena_cast_q(0); /* Duck's Telekinetic Yank */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].x == rooted_x,
          "Grand Secret's self-root makes the Tree immune to Duck's pull -- position unchanged");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < 100,
          "the pull is blocked but the Duck's damage still lands");
}

static void test_pizza_q_damages_and_applies_burn(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_PIZZA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* no armor, so damage isn't reduced */
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_PIZZA_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_PIZZA_Q_DAMAGE,
          "Nobody Checked damages an enemy in range");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].burning_ms == ARENA_PIZZA_Q_BURN_MS,
          "Nobody Checked applies a burn DoT to the enemy it hits");
}

static void test_pizza_burn_ticks_damage_over_time(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].burning_ms = ARENA_PIZZA_Q_BURN_MS;
    arena_state.heroes[0].burn_dps = ARENA_PIZZA_Q_BURN_DPS;

    arena_update_teams(1000); /* one full 1000ms burn tick */

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_PIZZA_Q_BURN_DPS,
          "an active burn deals its DPS once per 1000ms tick");
}

static void test_pizza_passive_aura_damages_nearby_foe(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_PIZZA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_PIZZA_AURA_RADIUS - 0.5f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_update_teams(1000);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_PIZZA_AURA_DPS,
          "Uninvestigated Fire's always-on aura damages a nearby enemy without any cast");
    CHECK(arena_state.heroes[0].hp == arena_state.heroes[0].max_hp,
          "Pizza is immune to its own burn aura");
}

static void test_pizza_r_prevents_death_for_duration(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_PIZZA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].hp = 1; arena_state.heroes[0].max_hp = 100; /* one hit from death */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_DUCK_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_r(0); /* Nobody Ever Checks */
    arena_cast_q(ARENA_TEAM_SIZE); /* Duck's Telekinetic Yank, would normally kill a 1-HP target */

    CHECK(arena_state.heroes[0].hp == 1, "the damage floor holds Pizza at 1 HP against lethal damage");
    CHECK(arena_state.heroes[0].alive, "Pizza survives what would otherwise be a killing blow");
}

static void test_flamel_q_roots_without_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FLAMEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_FLAMEL_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == ARENA_FLAMEL_Q_ROOT_MS,
          "Vine Growth roots an enemy in range");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100,
          "Vine Growth is pure crowd control -- it deals no damage");
}

static void test_flamel_w_heals_allies_in_radius(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FLAMEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* in radius */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[2].x = ARENA_FLAMEL_W_RADIUS * 3.0f; arena_state.heroes[2].z = 0; /* out of radius */
    arena_state.heroes[2].max_hp = 100; arena_state.heroes[2].hp = 50;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_FLAMEL_W_HEAL_BASE,
          "Philosopher's Bloom heals an ally within radius at the base rate");
    CHECK(arena_state.heroes[2].hp == 50, "Philosopher's Bloom does not reach an ally outside radius");
}

static void test_flamel_w_heals_more_on_marked_ground(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FLAMEL;
    arena_state.heroes[0].x = arena_state.nodes[0].x; arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[1].x = arena_state.nodes[0].x + 1.0f; arena_state.heroes[1].z = arena_state.nodes[0].z;
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.nodes[0].marked_by_team = arena_state.heroes[0].team; /* pre-marked, as if Flamel had stood here already */

    arena_toggle_w(0);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_FLAMEL_W_HEAL_MARKED,
          "Philosopher's Bloom heals for more when cast on Flamel's own marked ground");
}

static void test_flamel_r_roots_enemies_and_heals_allies_in_zone(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_FLAMEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally, inside the zone */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[ARENA_TEAM_SIZE].x = -1; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* enemy, inside the zone */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);
    arena_update_teams(1000); /* one full 1000ms zone tick */

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_FLAMEL_R_HEAL_PER_TICK,
          "Elixir of Wild Growth heals an ally standing in the zone");
    /* > 0, not an exact value: the root is applied mid-loop (hero 0's
       iteration) and then generically decremented by the same dt_ms during
       the target's OWN iteration later in the same arena_update_teams
       call -- an artifact of iteration order within one tick, same
       reasoning as why other status effects are asserted right after a
       standalone cast rather than after a full update tick. */
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms > 0,
          "Elixir of Wild Growth roots an enemy standing in the zone");
}

static void test_flamel_r_mass_marks_nodes_in_radius(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FLAMEL;
    arena_state.heroes[0].x = arena_state.nodes[0].x; arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_cast_r(0);

    CHECK(arena_state.nodes[0].marked_by_team == arena_state.heroes[0].team,
          "Elixir of Wild Growth mass-marks nodes within radius at cast time");
}

static void test_rooted_hero_cannot_move(void) {
    arena_init();
    arena_state.heroes[0].rooted_ms = 1000;
    arena_set_move_target(0, 5.0f, 5.0f);
    float x0 = arena_state.heroes[0].x, z0 = arena_state.heroes[0].z;

    arena_update(16);

    CHECK(arena_state.heroes[0].x == x0 && arena_state.heroes[0].z == z0,
          "a rooted hero does not move even with a move command queued");
}

/* S170-184, founder: "add more status effects use GFD [as a reference]" (GoblinFoxDragon's
 * server/status package) -- generic stun (hard CC: blocks movement, casting, and auto-attack)
 * and slow (proportional move-speed debuff), the exact gap hero_status_label's own doc comment
 * used to flag as missing. */

static void test_stunned_hero_cannot_move(void) {
    arena_init();
    arena_apply_stun(0, 1000);
    arena_set_move_target(0, 5.0f, 5.0f);
    float x0 = arena_state.heroes[0].x, z0 = arena_state.heroes[0].z;

    arena_update(16);

    CHECK(arena_state.heroes[0].x == x0 && arena_state.heroes[0].z == z0,
          "a stunned hero does not move even with a move command queued");
}

static void test_stunned_hero_cannot_cast(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    h->mp = h->max_mp;
    arena_apply_stun(0, 1000);

    arena_cast_q(0);
    CHECK(h->q_cooldown_ms == 0, "a stunned hero's Q cast is blocked entirely, never starts its cooldown");

    arena_toggle_w(0);
    CHECK(h->w_active == 0, "a stunned hero's W toggle is blocked entirely");

    arena_cast_r(0);
    CHECK(h->r_cooldown_ms == 0, "a stunned hero's R cast is blocked entirely, never starts its cooldown");
}

static void test_stunned_hero_cannot_auto_attack(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;
    arena_apply_stun(0, 5000);

    for (int i = 0; i < 100; i++) arena_update_teams(16); /* well past ARENA_ATTACK_COOLDOWN_MS if it were going to land */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "a stunned hero lands no auto-attacks, melee range notwithstanding");
}

static void test_stun_ticks_down_and_expires(void) {
    arena_init();
    arena_apply_stun(0, 500);

    arena_update(400);
    CHECK(arena_state.heroes[0].stunned_ms == 100, "stun ticks down like every other status effect");

    arena_update(200);
    CHECK(arena_state.heroes[0].stunned_ms == 0, "stun expires (clamped at 0, not negative) once its duration elapses");
    arena_set_move_target(0, 5.0f, 5.0f);
    arena_update(16);
    CHECK(arena_state.heroes[0].x != 0.0f || arena_state.heroes[0].z != 0.0f, "movement resumes once stun expires");
}

static void test_apply_stun_refresh_never_shortens(void) {
    arena_init();
    arena_apply_stun(0, 3000);
    arena_apply_stun(0, 500); /* weaker, shorter follow-up */

    CHECK(arena_state.heroes[0].stunned_ms == 3000, "a shorter stun application never shortens an already-longer remaining duration");

    arena_apply_stun(0, 5000); /* longer, real refresh */
    CHECK(arena_state.heroes[0].stunned_ms == 5000, "a longer stun application does extend the duration");
}

static void test_slowed_hero_moves_proportionally_slower(void) {
    /* Team mode, not the 1v1 local demo (arena_init/arena_update) -- that path has its own
       "close enough to the bot, treat the move as an attack-move" special case (arena_update's
       own comment) that overrides a move target near an opponent, which would confound this
       test's distance measurement. arena_update_teams has no such override. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaHero *h = &arena_state.heroes[0];
    float x0 = h->x, z0 = h->z;
    arena_set_move_target(0, x0 + 100.0f, z0);
    arena_update_teams(1000);
    float unslowed_dist = h->x - x0;

    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    h = &arena_state.heroes[0];
    x0 = h->x; z0 = h->z;
    arena_apply_slow(0, 2000, 0.5f);
    arena_set_move_target(0, x0 + 100.0f, z0);
    arena_update_teams(1000);
    float slowed_dist = h->x - x0;

    CHECK(slowed_dist > 0.0f && slowed_dist < unslowed_dist, "a 50%% slow covers real but reduced ground versus the unslowed baseline");
    CHECK(fabsf(slowed_dist - unslowed_dist * 0.5f) < 0.01f, "the reduction matches the applied slow_pct proportionally");
}

static void test_slow_ticks_down_and_expires(void) {
    arena_init();
    arena_apply_slow(0, 500, 0.5f);

    arena_update(400);
    CHECK(arena_state.heroes[0].slowed_ms == 100, "slow ticks down like every other status effect");

    arena_update(200);
    CHECK(arena_state.heroes[0].slowed_ms == 0, "slow expires (clamped at 0, not negative) once its duration elapses");
}

static void test_respawn_clears_stun_and_slow(void) {
    arena_init_teams();
    arena_apply_stun(0, 5000);
    arena_apply_slow(0, 5000, 0.5f);
    arena_state.heroes[0].alive = 0;

    for (int i = 0; i < 2000 && !arena_state.heroes[0].alive; i++) arena_update_teams(16);

    CHECK(arena_state.heroes[0].alive, "sanity: the hero actually respawned");
    CHECK(arena_state.heroes[0].stunned_ms == 0, "stun does not survive a respawn");
    CHECK(arena_state.heroes[0].slowed_ms == 0, "slow does not survive a respawn");
}

/* S170-47: Morrigan (TYLER #68) and Dagda (TYLER #69). */

static void test_morrigan_passive_grants_armor_on_contested_node(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MORRIGAN;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.nodes[0].owner = 0; /* contested */

    CHECK(arena_hero_armor(&arena_state.heroes[0]) == (float)ARENA_MORRIGAN_PASSIVE_ARMOR_BONUS,
          "Contested Ground grants armor while standing on a contested node");

    arena_state.nodes[0].owner = 1; /* claimed by a team -- no longer contested */
    CHECK(arena_hero_armor(&arena_state.heroes[0]) == 0.0f,
          "Contested Ground grants no armor once the node is claimed");
}

static void test_morrigan_q_executes_harder_at_low_hp(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MORRIGAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* no armor */
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_MORRIGAN_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 95; /* near-full */

    arena_cast_q(0);
    int dmg_near_full = 95 - arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_state.heroes[0].q_cooldown_ms = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 10; /* near-empty */
    arena_cast_q(0);
    int dmg_near_empty = 10 - arena_state.heroes[ARENA_TEAM_SIZE].hp;

    CHECK(dmg_near_empty > dmg_near_full,
          "The Washer's Strike deals more damage the lower the target's current HP%% is");
}

static void test_morrigan_w_teleports_and_roots_nearest_enemy(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MORRIGAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 9.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = -4.0f;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].x == 9.0f && arena_state.heroes[0].z == -4.0f,
          "Three Forms teleports Morrigan to the nearest enemy's exact position");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == ARENA_MORRIGAN_W_ROOT_MS,
          "Three Forms roots the enemy on arrival");
}

static void test_morrigan_r_zone_executes_harder_at_low_hp(void) {
    /* Enemy positioned inside the R radius but outside melee attack range,
       so the zone tick's own damage isn't confounded by an auto-attack
       landing in the same update. Two separate setups (near-full vs
       near-empty target HP), comparing the tick's damage delta rather than
       an absolute post-tick HP -- same pattern as the Q execute test,
       avoiding HP-floor clamping at 0 for the near-empty case. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MORRIGAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_MORRIGAN_R_RADIUS - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 1000;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 950; /* near-full */

    arena_cast_r(0);
    arena_update_teams(1000);
    int dmg_near_full = 950 - arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MORRIGAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_MORRIGAN_R_RADIUS - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 1000;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 50; /* near-empty */

    arena_cast_r(0);
    arena_update_teams(1000);
    int dmg_near_empty = 50 - arena_state.heroes[ARENA_TEAM_SIZE].hp;

    CHECK(dmg_near_empty > dmg_near_full,
          "The Crow Confirms It ticks harder against a near-dead enemy standing in the zone");
}

static void test_dagda_passive_regenerates_hp(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DAGDA;
    arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].hp = 50;

    arena_update_teams(1000);

    CHECK(arena_state.heroes[0].hp == 50 + ARENA_DAGDA_PASSIVE_REGEN_PER_SEC,
          "The Undry passively regenerates HP every tick with no cast at all");
}

static void test_dagda_q_kills_when_enemy_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DAGDA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_DAGDA_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_DAGDA_Q_KILL_DAMAGE,
          "the club's killing end damages an enemy in range");
}

static void test_dagda_q_revives_when_only_hurt_ally_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DAGDA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;

    arena_cast_q(0);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_DAGDA_Q_REVIVE_HEAL,
          "the club's reviving end heals a hurt ally when no enemy is in range");
}

static void test_dagda_w_heals_allies_and_cc_enemies_at_once(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_DAGDA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally, in radius */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[ARENA_TEAM_SIZE].x = -1; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* enemy, in radius */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_DAGDA_W_ALLY_HEAL,
          "Uaithne's joy strain heals an ally in radius");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == ARENA_DAGDA_W_ROOT_MS,
          "Uaithne's sorrow strain roots an enemy in radius");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].silenced_ms == ARENA_DAGDA_W_SILENCE_MS,
          "Uaithne's sleep strain silences an enemy in radius, in the same cast");
}

static void test_dagda_r_floor_and_heal(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DAGDA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 1; /* one hit from death */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* melee range */

    arena_cast_r(0); /* the porridge: floor + heal */
    CHECK(arena_state.heroes[0].hp == 1 + ARENA_DAGDA_R_HEAL,
          "the porridge heals Dagda for real, not just holding him at a floor");

    /* Repeated melee auto-attacks, well within the 3000ms floor window,
       dealing far more cumulative damage than the healed HP total -- would
       be lethal without the floor. */
    for (int i = 0; i < 180; i++) arena_update_teams(16); /* ~2880ms */

    CHECK(arena_state.heroes[0].alive && arena_state.heroes[0].hp == 1,
          "the damage floor holds Dagda at 1 HP against repeated attacks that would otherwise be lethal");
}

/* S170-48: The Courier (Ratatoskr, TYLER #32). */

static void test_courier_q_dashes_and_damages(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_COURIER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* no armor */
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_COURIER_Q_DASH_DIST; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_q(0);

    CHECK(arena_state.heroes[0].x > 0.0f, "The Insult, Lightly Edited dashes The Courier toward the enemy");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_COURIER_Q_DAMAGE,
          "the dash damages the enemy on arrival");
}

static void test_courier_q_cleanses_self_debuffs(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_COURIER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].rooted_ms = 500;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    /* silenced_ms > 0 would block the cast entirely (per arena_cast_q's own
       gate), so only rooted_ms is exercised here -- the cleanse still runs
       on a landed cast regardless. */
    arena_cast_q(0);

    CHECK(arena_state.heroes[0].rooted_ms == 0,
          "Lightly Edited cleanses The Courier's own active root on a landed cast");
}

static void test_courier_w_teleports_to_farther_node(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_COURIER;
    /* Stand exactly on node 0 -- whichever OTHER node is farthest from here
       is computed below rather than hardcoded, so this test stays valid
       across any ARENA_NODE_COUNT/layout (S170-119: was a 2-node map with
       a hardcoded "node 1 is farther" expectation; now 5 nodes). */
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    int expected = 0;
    float best_dist = -1.0f;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        float dx = arena_state.nodes[n].x - arena_state.heroes[0].x;
        float dz = arena_state.nodes[n].z - arena_state.heroes[0].z;
        float dist = dx * dx + dz * dz;
        if (dist > best_dist) { best_dist = dist; expected = n; }
    }

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].x == arena_state.nodes[expected].x && arena_state.heroes[0].z == arena_state.nodes[expected].z,
          "Between Eagle and Serpent teleports to whichever node is farther away");
}

static void test_courier_r_drains_life_from_nearest_enemy(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_COURIER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 50;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_COURIER_R_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_COURIER_R_DRAIN,
          "The Debt Collector's Due drains HP from the nearest enemy");
    CHECK(arena_state.heroes[0].hp == 50 + ARENA_COURIER_R_DRAIN,
          "...and delivers it to The Courier");
}

static void test_courier_r_out_of_range_whiffs(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_COURIER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_COURIER_R_RANGE * 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_r(0);

    CHECK(arena_state.heroes[0].r_cooldown_ms == 0,
          "The Debt Collector's Due whiffs out of range -- cooldown is not consumed");
}

/* S170-51: territorial dynamic node-guardian creeps. */

static void test_creep_spawns_on_first_tick_with_flavor_from_node_owner(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.nodes[0].owner = 1; /* team 0's */
    arena_state.nodes[1].owner = 0; /* neutral/contested */

    arena_tick_creeps(16);

    CHECK(arena_state.creeps[0].alive, "a creep spawns on the very first tick of a match");
    CHECK(arena_state.creeps[0].flavor == ARENA_CREEP_TEAM0 && arena_state.creeps[0].hp == ARENA_CREEP_TEAM_HP,
          "a creep on a team-owned node spawns as that team's flavor, at the weaker team HP");
    CHECK(arena_state.creeps[1].flavor == ARENA_CREEP_NEUTRAL && arena_state.creeps[1].hp == ARENA_CREEP_NEUTRAL_HP,
          "a creep on a contested node spawns as the tougher neutral flavor");
}

/* S170-161: "add node-guardian creeps use the redgarden dynamic creep ecosystem
 * something simple to start" -- graveyard spawn + march/fan-out toward
 * unowned nodes for team-flavored creeps specifically. */

static void test_team_creep_spawns_at_graveyard_not_node_position(void) {
    arena_init_teams();
    arena_state.nodes[0].owner = 1; /* team 0's node -- creep flavor becomes TEAM0 */

    arena_tick_creeps(16); /* spawn */

    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    CHECK(arena_state.creeps[0].x == gx && arena_state.creeps[0].z == gz,
          "a team-flavored creep spawns at its team's graveyard, not its node's own position");
    CHECK(arena_state.creeps[0].x != arena_state.nodes[0].x || arena_state.creeps[0].z != arena_state.nodes[0].z,
          "the graveyard and the node are genuinely different points");
}

static void test_neutral_creep_still_spawns_at_node_position(void) {
    arena_init_teams();
    arena_state.nodes[0].owner = 0; /* neutral/contested -- no team, no graveyard to spawn from */

    arena_tick_creeps(16);

    CHECK(arena_state.creeps[0].x == arena_state.nodes[0].x && arena_state.creeps[0].z == arena_state.nodes[0].z,
          "a NEUTRAL creep is unaffected by the graveyard-spawn change -- still spawns at its own node's position");
}

static void test_team_creep_marches_toward_nearest_unowned_node(void) {
    arena_init_teams();
    arena_state.nodes[0].owner = 1; /* team 0 owns only node 0 -- nodes 1..4 stay neutral/unowned */

    arena_tick_creeps(16); /* spawn at the graveyard */
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    float start_dist_sq = (arena_state.creeps[0].x - gx) * (arena_state.creeps[0].x - gx)
                         + (arena_state.creeps[0].z - gz) * (arena_state.creeps[0].z - gz);
    CHECK(start_dist_sq == 0.0f, "sanity: the creep starts exactly at the graveyard");

    arena_tick_creeps(2000); /* two real seconds of marching */

    float moved_dist_sq = (arena_state.creeps[0].x - gx) * (arena_state.creeps[0].x - gx)
                         + (arena_state.creeps[0].z - gz) * (arena_state.creeps[0].z - gz);
    CHECK(moved_dist_sq > 0.01f,
          "a team-flavored creep marches away from its graveyard spawn toward an unowned node -- 'fan out' from owned nodes");
}

static void test_team_creep_idles_once_its_team_owns_every_node(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team 0 owns everything */

    arena_tick_creeps(16); /* spawn */
    float x_after_spawn = arena_state.creeps[0].x, z_after_spawn = arena_state.creeps[0].z;

    arena_tick_creeps(5000); /* five real seconds -- plenty of time to march if it were going to */

    CHECK(arena_state.creeps[0].x == x_after_spawn && arena_state.creeps[0].z == z_after_spawn,
          "a team-flavored creep idles in place once its own team already owns every node -- nothing left to fan out into");
}

static void test_team_creep_march_redirects_when_target_node_gets_captured(void) {
    /* The march target is recomputed live every tick, not locked in once at
       spawn -- if the node a creep was heading toward becomes owned by its
       own team mid-march, it should stop closing on that now-owned node
       (there's nothing further to gain by continuing toward a point that's
       already been resolved), matching S170-161's "recomputed live, reacts
       to ownership changing mid-march" design. */
    arena_init_teams();
    arena_state.nodes[0].owner = 1; /* team 0's home node */
    for (int n = 1; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0; /* all others neutral/unowned */

    arena_tick_creeps(16); /* spawn at the graveyard */
    arena_tick_creeps(2000); /* march partway toward whichever unowned node is nearest */
    float x_partway = arena_state.creeps[0].x, z_partway = arena_state.creeps[0].z;

    /* Team 0 suddenly captures every remaining node -- nothing left unowned. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1;
    arena_tick_creeps(2000); /* the creep should now hold its ground, not keep closing on a node that's already theirs */

    CHECK(arena_state.creeps[0].x == x_partway && arena_state.creeps[0].z == z_partway,
          "a marching creep stops advancing the instant its own team ends up owning every node, mid-march");
}

static void test_creep_attacks_nearby_hero(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK; /* 0 base armor -- exact hit-damage math (S170-211) */
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16); /* spawn */
    arena_tick_creeps(ARENA_CREEP_ATTACK_COOLDOWN_MS); /* long enough for one attack */

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_CREEP_NEUTRAL_DAMAGE,
          "a node-guardian creep auto-attacks a hero standing within its aggro radius");
}

static void test_hero_does_not_attack_creep_while_an_enemy_hero_is_in_range(void) {
    /* Creeps are a secondary objective -- a hero already trading blows with
       an enemy hero shouldn't split attention onto a nearby creep too. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x + 1.0f; /* within ARENA_ATTACK_RANGE */
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;

    arena_tick_creeps(16); /* spawn */
    int hp_before = arena_state.creeps[0].hp;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.creeps[0].hp == hp_before,
          "a hero with an enemy hero already in range does not also attack a nearby creep this tick");
}

static void test_hero_kills_creep_and_queues_correct_respawn_timer(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* S170-161: team-flavored creeps spawn at their team's graveyard, not
       the node's own position -- team 0 owns everything so it has nowhere
       to march, staying exactly at the graveyard. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team-flavored, so ARENA_CREEP_TEAM_RESPAWN_MS applies */
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    arena_state.heroes[0].x = gx;
    arena_state.heroes[0].z = gz;

    arena_tick_creeps(16); /* spawn */
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE; /* one hit from death */
    arena_hero_attack_creeps(16);

    CHECK(!arena_state.creeps[0].alive, "the creep dies once its HP is reduced to 0");
    CHECK(arena_state.creeps[0].respawn_ms_remaining == ARENA_CREEP_TEAM_RESPAWN_MS,
          "a team-flavored creep queues the fast team respawn timer, not the slow neutral one");
    CHECK(arena_state.creeps[0].last_attacked_by_owner == 0, "the killing hero is credited as the last attacker");
}

static void test_neutral_creep_kill_grants_capture_bonus_only_while_channeling(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.nodes[0].owner = 0; /* neutral -- ARENA_CREEP_NEUTRAL flavor */

    arena_tick_creeps(16); /* spawn */
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_state.nodes[0].capturing_team = -1; /* not channeling right now */
    arena_state.nodes[0].capture_progress_ms = 0;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.nodes[0].capture_progress_ms == 0,
          "killing the neutral creep grants no capture bonus if the killer's team isn't actually channeling that node");

    arena_tick_creeps(16); /* respawn is queued, not immediate -- re-force it alive for the second half of this test */
    arena_state.creeps[0].alive = 1;
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_state.creeps[0].flavor = ARENA_CREEP_NEUTRAL;
    arena_state.heroes[0].attack_cooldown_ms = 0; /* the first kill above set this; nothing ticks it down outside the full update loop */
    arena_state.nodes[0].capturing_team = 0;
    arena_state.nodes[0].capture_progress_ms = 0;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.nodes[0].capture_progress_ms == ARENA_CREEP_NEUTRAL_KILL_CAPTURE_BONUS_MS,
          "killing the neutral creep while your team is channeling that node grants the big capture bonus");
}

static void test_team_creep_kill_by_owning_team_heals(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team 0 owns everything -- creep has nowhere to march */
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    arena_state.heroes[0].x = gx;
    arena_state.heroes[0].z = gz;
    arena_state.heroes[0].hp = 50; arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16);
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.heroes[0].hp == 50 + ARENA_CREEP_TEAM_KILL_HEAL,
          "killing your own team's node-guardian creep on your own territory heals you (home-turf resupply)");
}

static void test_team_creep_kill_by_enemy_team_helps_flip_the_node(void) {
    /* Team 1 farms team 0's own node-guardian creep while team 1 is mid-channel
       trying to flip that node -- the counter-play tool against a
       turtling opponent. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    /* 2026-07-29: hero[0] was never deactivated here, unlike this test's own sibling scenarios
       just above (test_neutral_creep_kill_grants_capture_bonus_only_while_channeling,
       test_team_creep_kill_by_owning_team_heals both deactivate every hero but the one actually
       under test) -- harmless while hero[0]'s default spawn (the old map-center-ish spawn line)
       sat far from team 0's graveyard, surfaced as a real collision once initial spawn moved
       TO the graveyard (same commit): hero[0] now defaults to exactly the point this test
       stages its farming scenario at, an uninvolved second attacker skewing the single-attacker
       deny-bonus check below. */
    arena_state.heroes[0].active = 0;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team 0 owns everything -- its creep has nowhere to march */
    /* S170-161: node[0] still needs to be the one actually captured below
       (capturing_team/capture_progress_ms live per-node), but the creep
       itself now spawns at team 0's graveyard, not node[0]'s position. */
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    arena_state.heroes[ARENA_TEAM_SIZE].x = gx;
    arena_state.heroes[ARENA_TEAM_SIZE].z = gz;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_tick_creeps(16);
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_state.nodes[0].capturing_team = 1; /* team 1 is trying to flip it */
    arena_state.nodes[0].capture_progress_ms = 0;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100, "the enemy killer gets no heal -- that reward is owning-team-only");
    CHECK(arena_state.nodes[0].capture_progress_ms == ARENA_CREEP_TEAM_KILL_DENY_CAPTURE_BONUS_MS,
          "farming the enemy's own node-guardian creep while channeling their node grants the deny capture bonus");
}

/* S170-139: lane creep waves. */

static void test_lane_creep_wave_spawns_for_both_teams_after_initial_delay(void) {
    /* arena_init_teams() arms both teams' wave timer at
       ARENA_LANE_WAVE_INITIAL_DELAY_MS (a short real-MOBA-style grace
       period, not an instant 0:00 spawn) -- confirmed both that nothing
       spawns before it elapses and that a full wave spawns for both teams
       the instant it does. */
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;

    arena_tick_lane_creeps(16);
    int any_active_before_delay = 0;
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) {
        if (arena_state.lane_creeps[i].active) any_active_before_delay = 1;
    }
    CHECK(!any_active_before_delay, "no lane creep wave spawns before the initial delay elapses");

    arena_tick_lane_creeps(ARENA_LANE_WAVE_INITIAL_DELAY_MS);

    int team0_count = 0, team1_count = 0;
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) {
        ArenaLaneCreep *c = &arena_state.lane_creeps[i];
        if (!c->active) continue;
        if (c->team == 0) team0_count++;
        else team1_count++;
    }
    CHECK(team0_count == ARENA_LANE_CREEPS_PER_WAVE, "team 0's first wave spawns a full wave once the initial delay elapses");
    CHECK(team1_count == ARENA_LANE_CREEPS_PER_WAVE, "team 1's first wave spawns a full wave once the initial delay elapses, same timer start as team 0");
}

static void test_lane_creep_wave_spawns_a_melee_caster_mix(void) {
    /* S170-218: every wave should carry ARENA_LANE_WAVE_CASTER_COUNT casters (with the
       caster's own, lower ARENA_LANE_CREEP_CASTER_HP) and the rest melee (the original
       ARENA_LANE_CREEP_HP, unchanged) -- "roles exist at all," not exact League parity. */
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;

    arena_tick_lane_creeps(ARENA_LANE_WAVE_INITIAL_DELAY_MS);

    int team0_melee = 0, team0_caster = 0;
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) {
        ArenaLaneCreep *c = &arena_state.lane_creeps[i];
        if (!c->active || c->team != 0) continue;
        if (c->role == ARENA_LANE_CREEP_CASTER) {
            team0_caster++;
            CHECK(c->max_hp == ARENA_LANE_CREEP_CASTER_HP, "a caster spawns with the caster HP value, not the melee one");
        } else {
            team0_melee++;
            CHECK(c->max_hp == ARENA_LANE_CREEP_HP, "a melee creep spawns with the original, unchanged HP value");
        }
    }
    CHECK(team0_caster == ARENA_LANE_WAVE_CASTER_COUNT, "each wave carries the documented number of casters");
    CHECK(team0_melee == ARENA_LANE_CREEPS_PER_WAVE - ARENA_LANE_WAVE_CASTER_COUNT, "the rest of the wave is melee");
}

static void test_lane_creep_caster_engages_from_farther_than_melee(void) {
    /* The actual point of the role split: a caster should be willing to stop and fight an
       enemy hero from farther away than a melee creep would, ARENA_LANE_CREEP_CASTER_RANGE vs.
       ARENA_LANE_CREEP_AGGRO_RADIUS. Distance chosen is past melee's own range but still
       within the caster's. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;
    float dist = (ARENA_LANE_CREEP_AGGRO_RADIUS + ARENA_LANE_CREEP_CASTER_RANGE) / 2.0f; /* past melee's range, within caster's */

    ArenaLaneCreep *melee = &arena_state.lane_creeps[0];
    melee->active = 1; melee->alive = 1; melee->team = 1; melee->role = ARENA_LANE_CREEP_MELEE;
    melee->hp = melee->max_hp = ARENA_LANE_CREEP_HP;
    melee->x = 0.0f; melee->z = -5.0f; /* far enough from the caster below that they don't aggro each other */

    ArenaLaneCreep *caster = &arena_state.lane_creeps[1];
    caster->active = 1; caster->alive = 1; caster->team = 1; caster->role = ARENA_LANE_CREEP_CASTER;
    caster->hp = caster->max_hp = ARENA_LANE_CREEP_CASTER_HP;
    caster->x = 0.0f; caster->z = 5.0f;

    arena_state.heroes[0].x = dist; arena_state.heroes[0].z = -5.0f; /* near the melee creep, past its range */
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;
    int hero_hp_before = arena_state.heroes[0].hp;

    arena_tick_lane_creeps(ARENA_LANE_CREEP_ATTACK_COOLDOWN_MS);
    CHECK(arena_state.heroes[0].hp == hero_hp_before, "sanity: the hero is past melee's own range, so nothing has hit it yet from that side");

    arena_state.heroes[0].x = dist; arena_state.heroes[0].z = 5.0f; /* now near the caster, same distance */
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_lane_creeps(ARENA_LANE_CREEP_ATTACK_COOLDOWN_MS);
    CHECK(arena_state.heroes[0].hp == 100 - ARENA_LANE_CREEP_CASTER_DAMAGE, "the caster engages from a range melee couldn't, dealing its own (lower) damage");
}

static void test_lane_creep_marches_toward_center_when_no_target(void) {
    /* Isolated single creep, real wave timers suppressed -- avoids the real
       opposing wave's own march/clash behavior clouding this specific
       "no target in range, just advance along the path" assertion. */
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1;
    creep->alive = 1;
    creep->team = 0;
    creep->waypoint_index = 0;
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = -8.0f; /* team 0's spawn line, S170-139 */
    creep->z = 0.0f;

    for (int t = 0; t < 50; t++) arena_tick_lane_creeps(16); /* 0.8s of marching, well short of reaching the center waypoint */

    CHECK(creep->x > -8.0f, "a lane creep with no target in range marches toward the enemy spawn line (team 0 marches +x)");
}

static void test_lane_creep_attacks_nearby_enemy_hero_and_does_not_advance(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1;
    creep->alive = 1;
    creep->team = 1; /* enemy of hero 0 (team 0) */
    creep->waypoint_index = 0;
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 0.0f;
    creep->z = 0.0f;

    arena_state.heroes[0].x = 1.0f; /* within ARENA_LANE_CREEP_AGGRO_RADIUS */
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_lane_creeps(16);

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_LANE_CREEP_DAMAGE,
          "a lane creep auto-attacks a hittable enemy hero within its aggro radius");
    CHECK(creep->x == 0.0f, "a lane creep that stops to fight does not advance along its waypoint path this tick");
}

static void test_lane_creep_aggro_redirects_to_attacker_over_a_closer_bystander(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1;
    creep->alive = 1;
    creep->team = 1; /* enemy of team 0 */
    creep->waypoint_index = 0;
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 0.0f;
    creep->z = 0.0f;

    /* The attacker: farther from the creep than the bystander below, but still within
       ARENA_LANE_CREEP_AGGRO_RADIUS (3.5) -- just recently hit one of the creep's own team's
       heroes (heroes[ARENA_TEAM_SIZE] below). Real minion aggro should pull onto this hero
       regardless of the plain-nearest pick. */
    arena_state.heroes[0].active = 1;
    arena_state.heroes[0].alive = 1;
    arena_state.heroes[0].x = 3.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    /* The bystander: geometrically nearer to the creep, never attacked anyone -- would win
       the plain-nearest pick if aggro-redirect didn't exist. */
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[1].x = 1.0f;
    arena_state.heroes[1].z = 0.0f;
    arena_state.heroes[1].hp = arena_state.heroes[1].max_hp = 100;

    /* The ally: on the creep's own team, just hit by the attacker (heroes[0], owner index 0). */
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;
    arena_state.heroes[ARENA_TEAM_SIZE].last_attacked_by_owner = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].combat_timer_ms = 1000;

    arena_tick_lane_creeps(16);

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_LANE_CREEP_DAMAGE,
          "minion-aggro-redirect: the creep attacks the hero who just attacked its own ally");
    CHECK(arena_state.heroes[1].hp == 100,
          "the geometrically-nearer bystander who attacked nobody takes no damage");
}

static void test_lane_creeps_fight_each_other_when_opposing_teams_meet(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    ArenaLaneCreep *a = &arena_state.lane_creeps[0];
    a->active = 1; a->alive = 1; a->team = 0; a->waypoint_index = 1;
    a->hp = a->max_hp = ARENA_LANE_CREEP_HP; a->x = -1.0f; a->z = 0.0f;

    ArenaLaneCreep *b = &arena_state.lane_creeps[1];
    b->active = 1; b->alive = 1; b->team = 1; b->waypoint_index = 1;
    b->hp = b->max_hp = ARENA_LANE_CREEP_HP; b->x = 1.0f; b->z = 0.0f;

    arena_tick_lane_creeps(16);

    CHECK(a->hp == ARENA_LANE_CREEP_HP - ARENA_LANE_CREEP_DAMAGE, "an opposing-team lane creep in aggro range takes damage from the other wave");
    CHECK(b->hp == ARENA_LANE_CREEP_HP - ARENA_LANE_CREEP_DAMAGE, "both sides of a wave clash damage each other the same tick -- the actual push mechanic");
}

static void test_hero_last_hits_a_lane_creep_already_weakened_by_the_wave_clash(void) {
    /* S170-217: lane-creep-vs-lane-creep damage (arena_tick_lane_creeps) and hero-vs-lane-creep
       damage (arena_hero_attack_lane_creeps) are two entirely independent sources converging on
       the same ArenaLaneCreep.hp field -- confirm a hero finishing off a creep the WAVE already
       weakened still gets full kill credit, the real last-hit mechanic, not new code, just
       proving the existing convergence already does the right thing. Uses both real damage
       paths for real, not a hand-set hp shortcut. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;
    arena_state.heroes[0].x = 100.0f; /* far away -- not yet in range for the wave-clash tick below */
    arena_state.heroes[0].z = 100.0f;

    ArenaLaneCreep *enemy = &arena_state.lane_creeps[0];
    enemy->active = 1; enemy->alive = 1; enemy->team = 1; enemy->waypoint_index = 1;
    enemy->hp = 10; enemy->max_hp = ARENA_LANE_CREEP_HP; /* already weak, well under ARENA_ATTACK_DAMAGE */
    enemy->x = 0.0f; enemy->z = 0.0f;

    ArenaLaneCreep *ally = &arena_state.lane_creeps[1];
    ally->active = 1; ally->alive = 1; ally->team = 0; ally->waypoint_index = 1;
    ally->hp = ally->max_hp = ARENA_LANE_CREEP_HP;
    ally->x = 1.0f; ally->z = 0.0f; /* within ARENA_LANE_CREEP_AGGRO_RADIUS of enemy */

    arena_tick_lane_creeps(16); /* real wave clash: enemy takes ARENA_LANE_CREEP_DAMAGE from ally */
    CHECK(enemy->alive && enemy->hp == 10 - ARENA_LANE_CREEP_DAMAGE, "sanity: the wave clash weakened the enemy creep but didn't kill it");

    arena_state.heroes[0].x = enemy->x; /* now move the hero in for the real finishing blow */
    arena_state.heroes[0].z = enemy->z;
    arena_hero_attack_lane_creeps(16);

    CHECK(!enemy->alive, "the hero's follow-up hit finishes off the wave-weakened creep");
    CHECK(arena_state.heroes[0].flow == ARENA_LANE_CREEP_KILL_FLOW, "last-hit: the finishing hero gets full kill credit even though the wave dealt some of the damage");
    CHECK(arena_state.heroes[0].xp == ARENA_LANE_CREEP_KILL_XP, "last-hit: full XP credit too, not split by damage contribution");
}

static void test_hero_kills_lane_creep_in_range(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    arena_state.heroes[0].x = 0.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].attack_cooldown_ms = 0;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1; creep->alive = 1; creep->team = 1; /* enemy of hero 0 */
    creep->hp = ARENA_ATTACK_DAMAGE; creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 0.0f; creep->z = 0.0f;

    arena_hero_attack_lane_creeps(16);

    CHECK(!creep->alive, "a hero kills a lane creep within attack range");
    CHECK(!creep->active, "a dead lane creep frees its pool slot immediately, unlike node-guardian creeps' delayed respawn");
}

static void test_hero_does_not_attack_own_team_lane_creep(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    arena_state.heroes[0].x = 0.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].attack_cooldown_ms = 0;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1; creep->alive = 1; creep->team = 0; /* SAME team as hero 0 */
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 0.0f; creep->z = 0.0f;

    arena_hero_attack_lane_creeps(16);

    CHECK(creep->hp == ARENA_LANE_CREEP_HP, "a hero does not attack its own team's lane creep above the deny threshold (50% HP)");
}

static void test_hero_can_deny_own_team_lane_creep_below_half_hp(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    arena_state.heroes[0].x = 0.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].attack_cooldown_ms = 0;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1; creep->alive = 1; creep->team = 0; /* SAME team as hero 0 */
    creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->hp = ARENA_LANE_CREEP_HP / 2 - 1; /* just below the 50% deny threshold */
    creep->x = 0.0f; creep->z = 0.0f;

    arena_hero_attack_lane_creeps(16);

    CHECK(creep->hp < ARENA_LANE_CREEP_HP / 2 - 1, "S170-215: a hero CAN deny its own team's lane creep once it's below 50% HP");
}

static void test_hero_does_not_attack_lane_creep_while_enemy_hero_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;

    arena_state.heroes[0].x = 0.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].attack_cooldown_ms = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; /* within ARENA_ATTACK_RANGE */
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1; creep->alive = 1; creep->team = 1;
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 0.3f; creep->z = 0.0f; /* also within ARENA_ATTACK_RANGE */

    arena_hero_attack_lane_creeps(16);

    CHECK(creep->hp == ARENA_LANE_CREEP_HP,
          "a hero with an enemy hero already in range does not also attack a nearby lane creep this tick");
}

static void test_lane_creep_despawns_at_final_waypoint_with_no_reward(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1; creep->alive = 1; creep->team = 0; creep->waypoint_index = ARENA_LANE_WAYPOINT_COUNT - 1;
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 8.0f; creep->z = 0.0f; /* team 0's final waypoint -- the enemy's spawn line, S170-139 */

    arena_tick_lane_creeps(16);

    CHECK(!creep->active, "a lane creep that reaches the final waypoint despawns -- no structure exists yet to push against");
}

static void test_lane_creep_wave_respawns_after_the_interval(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* Skip past the initial delay -- this test is about the steady-state
       respawn interval, not the match-opening grace period (already covered
       by test_lane_creep_wave_spawns_for_both_teams_after_initial_delay). */
    arena_state.lane_wave_timer_ms[0] = 0;
    arena_state.lane_wave_timer_ms[1] = 0;

    arena_tick_lane_creeps(16); /* first wave, both teams */
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) arena_state.lane_creeps[i].active = 0; /* as if the wave was wiped */

    arena_tick_lane_creeps(ARENA_LANE_WAVE_INTERVAL_MS); /* advance the timer past a full interval in one tick */

    int active_count = 0;
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) if (arena_state.lane_creeps[i].active) active_count++;
    CHECK(active_count == ARENA_LANE_CREEPS_PER_WAVE * 2, "a fresh wave spawns for both teams once the wave timer elapses again");
}

static void test_stealthed_hero_captures_undetected_through_a_crowd_of_visible_enemies(void) {
    /* The archetypal WoW Arathi Basin moment, brought forward on purpose:
       a stealthed capper (Frog's R, which the doc itself describes as
       "vanishes... can't be targeted or seen") solo-caps a node while a
       crowd of visible enemies stands right on top of it, none the wiser. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    for (int i = ARENA_TEAM_SIZE + 1; i < ARENA_TEAM_SIZE + 6; i++) {
        arena_state.heroes[i].active = 1;
        arena_state.heroes[i].alive = 1;
        arena_state.heroes[i].x = arena_state.nodes[0].x;
        arena_state.heroes[i].z = arena_state.nodes[0].z;
    }
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x;
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;
    arena_state.heroes[1].active = 0;

    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].intangible_ms = 5000; /* stealthed, e.g. mid-Frog's-R */

    arena_tick_nodes(1000);

    CHECK(arena_state.nodes[0].capturing_team == 0,
          "a lone stealthed hero channels a node even with six visible enemies standing right on it");
    CHECK(arena_state.nodes[0].capture_progress_ms > 0, "the undetected channel makes real progress, not just registering as attempted");
}

static void test_two_visible_teams_still_interrupt_normally_even_near_a_stealthed_ally(void) {
    /* Guards against the stealth exception swallowing the ordinary
       mixed-presence interrupt rule: if BOTH sides have a normal, visible
       presence, it's a contest as usual regardless of a stealthed hero
       loitering nearby. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x;
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;

    arena_state.heroes[0].x = arena_state.nodes[0].x; /* visible team-0 presence */
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[1].x = arena_state.nodes[0].x; /* a stealthed team-0 ally, also present */
    arena_state.heroes[1].z = arena_state.nodes[0].z;
    arena_state.heroes[1].intangible_ms = 5000;

    arena_tick_nodes(1000);

    CHECK(arena_state.nodes[0].capturing_team == -1,
          "a visible enemy still interrupts normally even when a stealthed ally is also present at the node");
}

static void test_starting_a_channel_breaks_the_capturer_stealth(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x;
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].intangible_ms = 5000; /* stealthed, sneaking in past the crowd */

    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == 0, "the sneak-capture starts undetected as before");
    CHECK(arena_state.heroes[0].intangible_ms == 0,
          "interacting with the flag breaks the capturer's own stealth the instant the channel starts, real Arathi Basin's own rule");
}

static void test_damage_to_channeling_team_interrupts_the_capture(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_nodes(ARENA_NODE_CAPTURE_CHANNEL_MS / 2);
    CHECK(arena_state.nodes[0].capturing_team == 0 && arena_state.nodes[0].capture_progress_ms > 0,
          "the channel is progressing normally, undamaged");

    /* apply_damage is static to arena_game.c and not linkable from here --
       set the flag it sets directly, same as this file already sets other
       status-effect fields (silenced_ms, rooted_ms, etc.) straight on the
       struct for test setup rather than going through a cast function. */
    arena_state.heroes[0].damaged_this_tick = 1;
    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == -1 && arena_state.nodes[0].capture_progress_ms == 0,
          "taking damage interrupts the capture channel, same as real Arathi Basin's flag-channel pushback");
}

static void test_dead_hero_respawns_at_graveyard_when_team_owns_no_node(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;

    ArenaHero *h = &arena_state.heroes[0];
    h->alive = 0;
    h->hero_id = ARENA_HERO_GHOST;

    /* Before the wave arrives, still dead even though the team owns nothing. */
    arena_update_teams(ARENA_RESPAWN_WAVE_MS - 100);
    CHECK(!h->alive, "wave hasn't arrived yet, still dead");

    arena_update_teams(200); /* crosses the wave boundary */
    float gx, gz;
    arena_graveyard_position(h->team, &gx, &gz);
    CHECK(h->alive, "wave arrived -- hero respawns even though the team owns no node");
    CHECK(h->x == gx && h->z == gz, "falls back to the team's permanent graveyard");
    CHECK(h->hero_id == ARENA_HERO_GHOST, "respawning preserves which hero this slot is playing");
}

static void test_dead_hero_respawns_at_owned_node_on_wave(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.nodes[0].owner = 1; /* team 0 owns node 0 */
    arena_state.creeps[0].alive = 0; /* isolate respawn correctness from the node's own creep aggro */
    arena_state.creeps[0].respawn_ms_remaining = ARENA_RESPAWN_WAVE_MS * 10;

    ArenaHero *h = &arena_state.heroes[0];
    h->alive = 0;
    h->hero_id = ARENA_HERO_GHOST;

    arena_update_teams(ARENA_RESPAWN_WAVE_MS - 100);
    CHECK(!h->alive, "wave hasn't arrived yet, still dead");

    arena_update_teams(200);
    CHECK(h->alive, "wave arrived and the team owns a node -- hero respawns");
    CHECK(h->hp == h->max_hp, "respawns at full HP");
    CHECK(h->x == arena_state.nodes[0].x && h->z == arena_state.nodes[0].z,
          "respawns at the owned node's position");
    CHECK(h->hero_id == ARENA_HERO_GHOST, "respawning preserves which hero this slot is playing");
}

static void test_respawn_wave_brings_back_all_dead_heroes_together(void) {
    /* S170-154, founder: "respawns happen in 30 second waves" -- heroes that
       died at very different times still come back on the exact same tick,
       not staggered by their own individual death timers. */
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;

    ArenaHero *early = &arena_state.heroes[0];
    ArenaHero *late = &arena_state.heroes[1];
    early->alive = 1;
    late->alive = 1;

    arena_update_teams(ARENA_RESPAWN_WAVE_MS / 4);
    early->alive = 0; /* dies early in the wave cycle */

    arena_update_teams(ARENA_RESPAWN_WAVE_MS / 2);
    late->alive = 0; /* dies much later, same cycle */
    CHECK(!early->alive && !late->alive, "both still dead mid-cycle");

    /* Advance to just past the wave boundary (timer started at 0, so the
       wave lands at ARENA_RESPAWN_WAVE_MS total elapsed). */
    arena_update_teams(ARENA_RESPAWN_WAVE_MS / 4 + 100);

    CHECK(early->alive && late->alive, "both heroes respawn together on the same wave tick");
}

static void test_resource_win_condition_replaces_team_wipe(void) {
    /* S170-153: a fully wiped team no longer instantly loses -- the match
       is decided by the resource race, not by hero deaths. */
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;

    for (int i = 0; i < ARENA_TEAM_SIZE; i++) arena_state.heroes[i].alive = 0;

    arena_update_teams(16);
    CHECK(arena_state.winner == 0, "a full team wipe alone no longer ends the match");
}

static void test_resource_accumulates_faster_with_more_owned_nodes(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.nodes[0].owner = 1; /* team 0 owns exactly one node */

    arena_state.resources[0] = 0;
    arena_state.resources[1] = 0;

    arena_update_teams(ARENA_RESOURCE_TICK_MS);
    CHECK(arena_state.resources[0] > 0, "team 0 gains resources from its owned node");
    CHECK(arena_state.resources[1] == 0, "team 1 owns nothing -- gains nothing");

    int gain_with_one_node = arena_state.resources[0];
    arena_state.nodes[1].owner = 1; /* team 0 now owns two nodes */
    arena_state.resources[0] = 0;

    arena_update_teams(ARENA_RESOURCE_TICK_MS);
    CHECK(arena_state.resources[0] > gain_with_one_node,
          "owning more nodes yields a bigger per-tick resource gain");
}

static void test_resource_cap_wins_the_match(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;

    arena_state.resources[0] = ARENA_RESOURCE_CAP;
    arena_state.resources[1] = 0;
    arena_update_teams(16);
    CHECK(arena_state.winner == 1, "team 0 hitting the resource cap wins for team 0");

    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.resources[0] = 0;
    arena_state.resources[1] = ARENA_RESOURCE_CAP;
    arena_update_teams(16);
    CHECK(arena_state.winner == 2, "team 1 hitting the resource cap wins for team 1");
}

static void test_sudden_death_does_not_fire_before_max_duration(void) {
    /* S170-157, founder: "i think there may be zombie games with infinite
       win cons." All nodes neutral -- zero resource gain regardless of how
       many resource ticks land inside this one big dt_ms, isolating the
       sudden-death clock from the primary resource-cap check. */
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.resources[0] = 500;
    arena_state.resources[1] = 500;

    arena_update_teams(ARENA_MATCH_MAX_DURATION_MS - 100);
    CHECK(arena_state.winner == 0,
          "no forced winner before the sudden-death clock runs out, even with tied resources and no nodes owned");
}

static void test_sudden_death_picks_team_ahead_on_resources(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.resources[0] = 800;
    arena_state.resources[1] = 300;
    arena_state.match_elapsed_ms = ARENA_MATCH_MAX_DURATION_MS - 8;

    arena_update_teams(16); /* small dt -- crosses the threshold without also feeding the resource-tick accumulator */
    CHECK(arena_state.winner == 1, "sudden death: team 0 is ahead on resources once the clock runs out -- team 0 wins outright");
}

static void test_sudden_death_tiebreaks_by_nodes_owned(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.nodes[0].owner = 2; /* team 1 owns exactly one node, team 0 owns none */
    arena_state.resources[0] = 500;
    arena_state.resources[1] = 500;
    arena_state.match_elapsed_ms = ARENA_MATCH_MAX_DURATION_MS - 8;

    arena_update_teams(16);
    CHECK(arena_state.winner == 2,
          "resources are exactly tied at the sudden-death clock -- falls back to nodes currently owned, team 1 has one");
}

static void test_sudden_death_full_tie_resolves_to_team_zero(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.resources[0] = 0;
    arena_state.resources[1] = 0;
    arena_state.match_elapsed_ms = ARENA_MATCH_MAX_DURATION_MS - 8;

    arena_update_teams(16);
    CHECK(arena_state.winner == 1,
          "resources AND nodes owned both exactly tied -- last-resort deterministic fallback picks team 0");
}

/* S170-162/163: NORTHSTAR §17's click-to-attack system -- attack-target
 * lock, chase, and Gary's homing ranged auto-attack. Team mode only. */

/* arena_stop_unit (NORTHSTAR.md §24 Milestone 2, 2026-07-31) -- the real WC3 "Stop" command,
 * first of the group-order vocabulary that section names for Tyler's own clone control. */

static void test_stop_unit_cancels_move_target(void) {
    arena_init_teams();
    ArenaHero *h = &arena_state.heroes[0];
    float start_x = h->x, start_z = h->z;
    arena_set_move_target(0, h->x + 20.0f, h->z);
    CHECK(h->moving == 1, "sanity: the move command is in effect");

    arena_stop_unit(0);

    CHECK(h->moving == 0, "Stop cancels the move order");
    CHECK(h->target_x == start_x && h->target_z == start_z, "Stop sets the target back to the unit's own current position, not left stale");
}

static void test_stop_unit_cancels_attack_target(void) {
    arena_init_teams();
    arena_set_attack_target(0, 10);
    CHECK(arena_state.heroes[0].attack_target == 10, "sanity: the lock is set");

    arena_stop_unit(0);

    CHECK(arena_state.heroes[0].attack_target == -1, "Stop clears the attack-target lock too");
}

static void test_stop_unit_out_of_range_owner_is_a_safe_no_op(void) {
    arena_init_teams();
    arena_stop_unit(-1);
    arena_stop_unit(ARENA_HEROES_ARRAY_SIZE);
    CHECK(1, "out-of-range owner indices don't crash -- same bounds-check convention arena_set_move_target/arena_set_attack_target already use");
}

/* Attack-move (NORTHSTAR.md §17.4 + §24 Milestone 2, 2026-07-31) -- real LoL/WC3 "A + click." */

static void test_attack_move_walks_toward_destination_when_nothing_nearby(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0; /* no enemies anywhere in range */
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;

    arena_set_attack_move_target(0, 15.0f, 0.0f);
    CHECK(arena_state.heroes[0].attack_move_active == 1, "sanity: attack-move is armed");
    CHECK(arena_state.heroes[0].moving == 1 && arena_state.heroes[0].target_x == 15.0f, "sanity: it starts walking toward the destination like a plain move");

    arena_tick_attack_move(100);

    CHECK(arena_state.heroes[0].attack_target == -1, "nothing in range -- no opportunistic engage");
    CHECK(arena_state.heroes[0].target_x == 15.0f && arena_state.heroes[0].target_z == 0.0f,
          "still walking toward the original attack-move destination");
}

static void test_attack_move_opportunistically_engages_enemy_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f; /* well within ARENA_ATTACK_RANGE */

    arena_set_attack_move_target(0, 50.0f, 0.0f); /* destination is far past the enemy */
    arena_tick_attack_move(100);

    CHECK(arena_state.heroes[0].attack_target == ARENA_TEAM_SIZE,
          "attack-move opportunistically locks onto an enemy that comes within range along the way, without it ever being the original destination");
}

static void test_attack_move_resumes_destination_once_nothing_left_to_engage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;

    arena_set_attack_move_target(0, 15.0f, 0.0f);
    /* Simulate a chase having overwritten target_x/z toward some now-dead enemy's last position
       (arena_tick_attack_targets' own real "the attack command wins while it's active"
       behavior) -- attack_target itself already cleared (that function's own job, not this
       one's), attack_move_active still set. */
    arena_state.heroes[0].target_x = 3.0f;
    arena_state.heroes[0].target_z = 3.0f;
    arena_state.heroes[0].attack_target = -1;

    arena_tick_attack_move(100);

    CHECK(arena_state.heroes[0].target_x == 15.0f && arena_state.heroes[0].target_z == 0.0f,
          "with nothing left to engage, attack-move resumes the ORIGINAL destination instead of standing wherever the chase left off");
    CHECK(arena_state.heroes[0].moving == 1, "and actually resumes walking, not left idle");
}

static void test_attack_move_cleared_by_plain_move_command(void) {
    arena_init_teams();
    arena_set_attack_move_target(0, 15.0f, 0.0f);
    CHECK(arena_state.heroes[0].attack_move_active == 1, "sanity: attack-move is armed");

    arena_set_move_target(0, 5.0f, 5.0f);

    CHECK(arena_state.heroes[0].attack_move_active == 0, "a fresh plain move command clears attack-move, same 'a new command always wins' convention as attack_target");
}

static void test_attack_move_cleared_by_attack_target_and_stop(void) {
    arena_init_teams();
    arena_set_attack_move_target(0, 15.0f, 0.0f);
    arena_set_attack_target(0, 10);
    CHECK(arena_state.heroes[0].attack_move_active == 0, "a fresh attack-target command clears attack-move too");

    arena_set_attack_move_target(0, 15.0f, 0.0f);
    arena_stop_unit(0);
    CHECK(arena_state.heroes[0].attack_move_active == 0, "and so does Stop");
}

/* Hold Position (NORTHSTAR.md §24 Milestone 2, 2026-07-31) -- real WC3 "Hold Position," third of
 * the group-order vocabulary. */

static void test_hold_position_halts_movement_in_place(void) {
    arena_init_teams();
    ArenaHero *h = &arena_state.heroes[0];
    float start_x = h->x, start_z = h->z;
    arena_set_move_target(0, h->x + 20.0f, h->z);
    CHECK(h->moving == 1, "sanity: the move command is in effect");

    arena_hold_position(0);

    CHECK(h->moving == 0, "Hold Position halts movement");
    CHECK(h->target_x == start_x && h->target_z == start_z, "target is set back to the unit's own current position, same convention as Stop");
    CHECK(h->hold_position == 1, "sanity: the flag itself is set");
}

static void test_hold_position_does_not_chase_target_leaving_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f; /* within range */

    arena_hold_position(0);
    arena_state.heroes[0].attack_target = ARENA_TEAM_SIZE; /* simulate already being engaged */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 20.0f; /* the foe retreats well out of range */

    arena_tick_attack_targets(100);

    CHECK(arena_state.heroes[0].moving == 0, "a held unit does not chase a target that leaves range");
    CHECK(arena_state.heroes[0].attack_target == -1, "the lock drops instead of persisting on a foe that's left -- lets a closer new target be re-acquired next tick");
}

static void test_hold_position_opportunistically_engages_enemy_in_range(void) {
    /* Real value of Hold Position: a stationary unit still defends itself, unlike a plain
       stopped/idle one -- this is what actually makes ranged heroes (whose basic attacks only
       ever fire through attack_target, unlike melee's always-on flat proximity loop) able to
       fight back at all while holding. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f; /* within range */

    arena_hold_position(0);
    arena_tick_attack_move(100);

    CHECK(arena_state.heroes[0].attack_target == ARENA_TEAM_SIZE, "a held unit opportunistically engages an enemy that comes within its own range");
}

static void test_hold_position_cleared_by_other_commands(void) {
    arena_init_teams();
    arena_hold_position(0);
    arena_set_move_target(0, 5.0f, 5.0f);
    CHECK(arena_state.heroes[0].hold_position == 0, "a fresh move command clears Hold Position");

    arena_hold_position(0);
    arena_set_attack_target(0, 10);
    CHECK(arena_state.heroes[0].hold_position == 0, "a fresh attack-target command clears it too");

    arena_hold_position(0);
    arena_set_attack_move_target(0, 15.0f, 0.0f);
    CHECK(arena_state.heroes[0].hold_position == 0, "and so does attack-move");

    arena_hold_position(0);
    arena_stop_unit(0);
    CHECK(arena_state.heroes[0].hold_position == 0, "and Stop");
}

/* Patrol (NORTHSTAR.md §24 Milestone 2, 2026-07-31) -- real WC3 "Patrol," fourth and last of the
 * group-order vocabulary. */

static void test_patrol_starts_walking_toward_b_first(void) {
    arena_init_teams();
    ArenaHero *h = &arena_state.heroes[0];
    float start_x = h->x, start_z = h->z;

    arena_set_patrol_target(0, 20.0f, 5.0f);

    CHECK(h->patrol_active == 1, "sanity: patrol is armed");
    CHECK(h->patrol_a_x == start_x && h->patrol_a_z == start_z, "point A is the unit's own position at the moment of issue");
    CHECK(h->patrol_b_x == 20.0f && h->patrol_b_z == 5.0f, "point B is the clicked point");
    CHECK(h->patrol_going_to_b == 1, "real WC3 behavior: the clicked point is always the first leg");
    CHECK(h->moving == 1 && h->target_x == 20.0f && h->target_z == 5.0f, "starts walking toward B like a plain move");
}

static void test_patrol_flips_direction_on_arrival(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0; /* no enemies, isolate the arrival/flip behavior */
    ArenaHero *h = &arena_state.heroes[0];
    arena_set_patrol_target(0, 20.0f, 0.0f);
    h->x = 20.0f; h->z = 0.0f; /* simulate having arrived at B */

    arena_tick_patrol(100);

    CHECK(h->patrol_going_to_b == 0, "arriving at B flips the leg toward A");
    CHECK(h->target_x == h->patrol_a_x && h->target_z == h->patrol_a_z, "and starts walking back toward A");
    CHECK(h->moving == 1, "still actually moving, not left idle");
}

static void test_patrol_opportunistically_engages_enemy_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f; /* within range */

    arena_set_patrol_target(0, 50.0f, 0.0f); /* destination is far past the enemy */
    arena_tick_patrol(100);

    CHECK(arena_state.heroes[0].attack_target == ARENA_TEAM_SIZE,
          "patrol opportunistically engages an enemy encountered along the route, same as attack-move");
}

static void test_patrol_cleared_by_other_commands(void) {
    arena_init_teams();
    arena_set_patrol_target(0, 20.0f, 0.0f);
    arena_set_move_target(0, 5.0f, 5.0f);
    CHECK(arena_state.heroes[0].patrol_active == 0, "a fresh move command clears patrol");

    arena_set_patrol_target(0, 20.0f, 0.0f);
    arena_set_attack_target(0, 10);
    CHECK(arena_state.heroes[0].patrol_active == 0, "a fresh attack-target command clears it too");

    arena_set_patrol_target(0, 20.0f, 0.0f);
    arena_set_attack_move_target(0, 15.0f, 0.0f);
    CHECK(arena_state.heroes[0].patrol_active == 0, "and attack-move");

    arena_set_patrol_target(0, 20.0f, 0.0f);
    arena_hold_position(0);
    CHECK(arena_state.heroes[0].patrol_active == 0, "and Hold Position");

    arena_set_patrol_target(0, 20.0f, 0.0f);
    arena_stop_unit(0);
    CHECK(arena_state.heroes[0].patrol_active == 0, "and Stop");
}

static void test_attack_target_clears_on_fresh_move_command(void) {
    arena_init_teams();
    arena_set_attack_target(0, 10);
    CHECK(arena_state.heroes[0].attack_target == 10, "sanity: the lock is set");

    arena_set_move_target(0, 5.0f, 5.0f);

    CHECK(arena_state.heroes[0].attack_target == -1,
          "a fresh move command immediately clears the attack-target lock (NORTHSTAR SS17.1)");
}

static void test_attack_target_chases_out_of_range_enemy(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1; /* keep the actual target alive -- the blanket deactivation above would otherwise also deactivate it */
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].target_x = 0.0f; arena_state.heroes[0].target_z = 0.0f;
    arena_state.heroes[0].moving = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 20.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f; /* well outside ARENA_ATTACK_RANGE */

    arena_set_attack_target(0, ARENA_TEAM_SIZE);
    arena_update_teams(16);

    CHECK(arena_state.heroes[0].moving,
          "an out-of-range attack-target lock starts the hero moving toward it");
    CHECK(arena_state.heroes[0].target_x == arena_state.heroes[ARENA_TEAM_SIZE].x
          && arena_state.heroes[0].target_z == arena_state.heroes[ARENA_TEAM_SIZE].z,
          "chase targets the enemy's actual live position, pure pursuit");
}

static void test_attack_target_re_chases_a_fleeing_target(void) {
    /* The literal "if auto attacking and a character runs away do you
       follow it" ask -- yes, automatically, every tick, no re-click. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 20.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;

    arena_set_attack_target(0, ARENA_TEAM_SIZE);
    arena_update_teams(16);
    float first_target_x = arena_state.heroes[0].target_x;

    /* The enemy flees further away without either side re-issuing any command. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 25.0f;
    arena_update_teams(16);

    CHECK(arena_state.heroes[0].target_x != first_target_x
          && arena_state.heroes[0].target_x == arena_state.heroes[ARENA_TEAM_SIZE].x,
          "the chase re-targets the enemy's new position every tick, following a fleeing target with no re-click");
}

static void test_attack_target_clears_when_target_dies(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 20.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;
    arena_set_attack_target(0, ARENA_TEAM_SIZE);

    arena_state.heroes[ARENA_TEAM_SIZE].alive = 0;
    arena_update_teams(16);

    CHECK(arena_state.heroes[0].attack_target == -1,
          "the lock clears on its own once the target dies -- no dangling reference to a dead hero");
}

static void test_attack_target_rejects_own_team(void) {
    arena_init_teams();
    arena_state.heroes[0].team = 0;
    arena_state.heroes[1].team = 0;
    arena_set_attack_target(0, 1); /* both team 0 -- never a valid attack target */

    arena_update_teams(16);

    CHECK(arena_state.heroes[0].attack_target == -1,
          "a lock onto a hero on the same team is rejected/cleared, never chased or attacked");
}

static void test_gary_fires_homing_shot_at_locked_target_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_ATTACK_RANGE - 1.0f; /* within Gary's ranged auto-attack range */
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;

    arena_set_attack_target(0, ARENA_TEAM_SIZE);
    /* S170-204: begins windup on the first tick, fires on a second tick worth the full
       ARENA_GARY_ATTACK_WINDUP_MS duration -- Gary's homing shot no longer fires instantly the
       moment he's in range. */
    arena_update_teams(16);
    arena_update_teams((unsigned int)ARENA_GARY_ATTACK_WINDUP_MS);

    int found = 0;
    for (int p = 0; p < ARENA_MAX_PROJECTILES; p++) {
        if (arena_state.projectiles[p].active && arena_state.projectiles[p].homing_target == ARENA_TEAM_SIZE
            && arena_state.projectiles[p].owner == 0) {
            found = 1;
        }
    }
    CHECK(found, "Gary fires a real homing projectile at his locked target once it's in range");
}

static void test_gary_does_not_deal_flat_melee_damage(void) {
    /* Founder: "gary auto attacks are projetiles ... you cant juke them" --
       Gary's plain auto-attack is exclusively the homing shot above, never
       the flat proximity melee tick every other hero still uses. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f; /* within melee range too */
    int hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;
    /* Deliberately no attack_target set -- Gary's own homing shot never
       fires without an explicit lock (see arena_tick_attack_targets), and
       he's excluded from the generic melee loop, so nothing should land
       from mere proximity alone. */

    arena_update_teams(ARENA_ATTACK_COOLDOWN_MS);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == hp_before,
          "Gary standing next to an enemy with no attack command issued deals no damage -- unlike every other hero's ambient melee tick");
}

static void test_homing_shot_hits_target_that_moves_off_the_original_line(void) {
    /* The exact opposite of how a skill-shot ArenaProjectile already
       behaves (dodgeable by stepping off the line) -- a homing shot keeps
       tracking and still connects. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) { if (i != ARENA_TEAM_SIZE) arena_state.heroes[i].active = 0; } /* isolate: no other hero can accidentally be in the shot's path */
    ArenaProjectile *shot = arena_spawn_projectile(0, 0, ARENA_HERO_GARY,
        0.0f, 0.0f, 10.0f, 0.0f, ARENA_GARY_ATTACK_SPEED, 0.6f, ARENA_GARY_ATTACK_DAMAGE, 100.0f);
    shot->homing_target = ARENA_TEAM_SIZE;
    arena_state.heroes[ARENA_TEAM_SIZE].team = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 10.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;
    int hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    /* The target immediately steps well off the shot's original firing line. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 10.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 8.0f;

    for (int i = 0; i < 60 && arena_state.projectiles[0].active; i++) {
        arena_tick_projectiles(16);
    }

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < hp_before,
          "a homing shot still connects even after the target juked off the original firing line -- can't be dodged by positioning");
}

static void test_homing_shot_fizzles_if_target_dies_before_arrival(void) {
    arena_init_teams();
    ArenaProjectile *shot = arena_spawn_projectile(0, 0, ARENA_HERO_GARY,
        0.0f, 0.0f, 10.0f, 0.0f, ARENA_GARY_ATTACK_SPEED, 0.6f, ARENA_GARY_ATTACK_DAMAGE, 100.0f);
    shot->homing_target = ARENA_TEAM_SIZE;
    arena_state.heroes[ARENA_TEAM_SIZE].team = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 10.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 0; /* dead before the shot ever gets there */

    arena_tick_projectiles(16);

    CHECK(!arena_state.projectiles[0].active,
          "a homing shot fizzles (no floating hit) the instant its target is no longer a valid hit");
}

static void test_paimon_q_damages_and_roots_in_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_PAIMON);
    ArenaHero *paimon = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = paimon->x + 4.0f; /* within ARENA_PAIMON_Q_RANGE */
    foe->z = paimon->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in range");
    CHECK(foe->rooted_ms == ARENA_PAIMON_Q_ROOT_MS, "Q roots the foe on a landed hit");
    CHECK(paimon->q_cooldown_ms == ARENA_PAIMON_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_paimon_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_PAIMON);
    ArenaHero *paimon = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = paimon->x + ARENA_PAIMON_Q_RANGE + 5.0f;
    foe->z = paimon->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does not damage the foe");
    CHECK(foe->rooted_ms == 0, "Q out of range does not root the foe");
}

static void test_paimon_w_damages_and_silences_in_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_PAIMON);
    ArenaHero *paimon = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = paimon->x + 4.0f; /* within ARENA_PAIMON_W_RANGE */
    foe->z = paimon->z;
    int foe_hp_before = foe->hp;

    arena_toggle_w(1);

    CHECK(foe->hp < foe_hp_before, "Speaks With Total Authority damages the nearest enemy in range");
    CHECK(foe->silenced_ms == ARENA_PAIMON_W_SILENCE_MS, "Speaks With Total Authority silences the nearest enemy");
    CHECK(paimon->w_cooldown_ms == ARENA_PAIMON_W_COOLDOWN_MS, "W starts on its own cooldown after cast");
}

static void test_paimon_passive_silences_nearest_enemy_periodically(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_PAIMON);
    /* S170-228: Paimon (hero 1, "the bot") now moves via the trained RL policy instead of the
       old fixed net -- a single big interval-length tick is real time for it to walk away from
       the fixed foe->x/z this test sets up, breaking the "stays within aura radius" setup this
       test actually depends on. Disabled the same way other tests in this file already handle
       the same root cause. */
    arena_bot_enabled = 0;
    ArenaHero *paimon = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = paimon->x + 2.0f; /* within ARENA_PAIMON_PASSIVE_AURA_RADIUS */
    foe->z = paimon->z;
    foe->target_x = foe->x; /* don't wander out of aura range before the tick lands */
    foe->target_z = foe->z;

    arena_update(ARENA_PAIMON_PASSIVE_INTERVAL_MS);

    CHECK(foe->silenced_ms > 0, "Keeping the Peace silences the nearest enemy in range without being cast");
    arena_bot_enabled = 1; /* restore the default for any test run after this one */
}

static void test_paimon_r_zone_damages_enemy_and_heals_ally(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_PAIMON;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally, inside the zone */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[ARENA_TEAM_SIZE].x = -1; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* enemy, inside the zone */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);
    CHECK(arena_state.heroes[0].r_active_ms == ARENA_PAIMON_R_DURATION_MS, "R starts its zone duration on cast");
    CHECK(arena_state.heroes[0].r_cooldown_ms == ARENA_PAIMON_R_COOLDOWN_MS, "R starts on its own cooldown after cast");

    arena_update_teams(1000); /* one full 1000ms zone tick */

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_PAIMON_R_HEAL_PER_TICK,
          "Two Hundred Legions heals an ally standing in the zone");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp <= 100 - ARENA_PAIMON_R_DPS,
          "Two Hundred Legions damages an enemy standing in the zone");
}

static void test_cast_flash_slot_set_on_q(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = ghost->x + 4.0f;
    foe->z = ghost->z;

    arena_cast_q(1);

    CHECK(ghost->cast_flash_slot == 1, "a successful Q cast sets cast_flash_slot to 1");
}

static void test_cast_flash_slot_set_on_w_toggle_hero(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *unicorn = &arena_state.heroes[0];

    arena_toggle_w(0);

    CHECK(unicorn->cast_flash_slot == 2, "toggling a pure-toggle W (Unicorn) sets cast_flash_slot to 2");
}

static void test_cast_flash_slot_set_on_r(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];

    arena_cast_r(1);

    CHECK(ghost->cast_flash_slot == 3, "a successful R cast sets cast_flash_slot to 3");
}

static void test_cast_flash_slot_not_set_when_q_blocked_by_cooldown(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ghost->q_cooldown_ms = 1000;

    arena_cast_q(1);

    CHECK(ghost->cast_flash_slot == 0, "a Q blocked by its own cooldown does not set cast_flash_slot");
}

static void test_cast_flash_slot_not_set_when_w_blocked_by_its_own_cooldown(void) {
    /* Ghost's W is instant-with-cooldown (not a pure toggle like Unicorn's) --
       exactly the case S170-124's arena_toggle_w gating exists to handle correctly. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ghost->w_cooldown_ms = 1000;

    arena_toggle_w(1);

    CHECK(ghost->cast_flash_slot == 0, "a cooldown-gated W blocked by its own cooldown does not set cast_flash_slot");
}

static void test_noor1_q_damages_and_roots_in_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_NOOR1);
    ArenaHero *noor1 = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = noor1->x + 4.0f; /* within ARENA_NOOR1_Q_RANGE */
    foe->z = noor1->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in range");
    CHECK(foe->rooted_ms == ARENA_NOOR1_Q_ROOT_MS, "Q roots the foe on a landed hit");
    CHECK(noor1->q_cooldown_ms == ARENA_NOOR1_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_noor1_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_NOOR1);
    ArenaHero *noor1 = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = noor1->x + ARENA_NOOR1_Q_RANGE + 5.0f;
    foe->z = noor1->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does not damage the foe");
    CHECK(foe->rooted_ms == 0, "Q out of range does not root the foe");
}

static void test_noor1_w_grants_intangibility_and_cooldown(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_NOOR1);
    ArenaHero *noor1 = &arena_state.heroes[1];

    arena_toggle_w(1);

    CHECK(noor1->intangible_ms == ARENA_NOOR1_W_INTANGIBLE_MS, "Sent In Clean grants self-intangibility");
    CHECK(noor1->w_cooldown_ms == ARENA_NOOR1_W_COOLDOWN_MS, "W starts on its own cooldown after cast");
}

static void test_noor1_passive_silences_nearest_enemy_periodically(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_NOOR1);
    /* S170-228: same root cause and fix as test_paimon_passive_silences_nearest_enemy_periodically
       above -- the bot hero now moves via the trained RL policy, which can walk far enough
       during one big interval-length tick to break this test's own fixed-position setup. */
    arena_bot_enabled = 0;
    ArenaHero *noor1 = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = noor1->x + 2.0f; /* within ARENA_NOOR1_PASSIVE_AURA_RADIUS */
    foe->z = noor1->z;
    foe->target_x = foe->x; /* don't wander out of aura range before the tick lands */
    foe->target_z = foe->z;

    arena_update(ARENA_NOOR1_PASSIVE_INTERVAL_MS);

    CHECK(foe->silenced_ms > 0, "About Four Days Behind silences the nearest enemy in range without being cast");
    arena_bot_enabled = 1; /* restore the default for any test run after this one */
}

static void test_noor1_r_zone_damages_enemy_no_ally_heal(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_NOOR1;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally, inside the zone */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[ARENA_TEAM_SIZE].x = -1; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* enemy, inside the zone */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);
    CHECK(arena_state.heroes[0].r_active_ms == ARENA_NOOR1_R_DURATION_MS, "R starts its zone duration on cast");
    CHECK(arena_state.heroes[0].r_cooldown_ms == ARENA_NOOR1_R_COOLDOWN_MS, "R starts on its own cooldown after cast");

    arena_update_teams(1000); /* one full 1000ms zone tick */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp <= 100 - ARENA_NOOR1_R_DPS,
          "Do Not Approach damages an enemy standing in the zone");
    CHECK(arena_state.heroes[1].hp == 50,
          "Do Not Approach has no ally-heal side -- an ally standing in the zone is unaffected");
}

static void test_cain_passive_grants_flat_armor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    ArenaHero *cain = &arena_state.heroes[1];
    CHECK(arena_hero_armor(cain) == (float)ARENA_CAIN_PASSIVE_ARMOR,
          "Cain's founded city grants a flat, always-on armor bonus");
}

static void test_cain_q_executes_harder_at_low_hp(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    ArenaHero *cain = &arena_state.heroes[1];
    ArenaHero *full_hp_foe = &arena_state.heroes[0];
    full_hp_foe->x = cain->x + 4.0f;
    full_hp_foe->z = cain->z;
    int hp_before_full = full_hp_foe->hp;
    arena_cast_q(1);
    int dmg_at_full_hp = hp_before_full - full_hp_foe->hp;

    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    cain = &arena_state.heroes[1];
    ArenaHero *low_hp_foe = &arena_state.heroes[0];
    low_hp_foe->x = cain->x + 4.0f;
    low_hp_foe->z = cain->z;
    low_hp_foe->hp = low_hp_foe->max_hp; /* keep it alive after the hit so the damage delta is measurable --
                                             the point is the execute *scaling*, not landing a kill */
    /* Simulate "near death" via max_hp rather than a tiny hp value: execute_scale_damage reads
       hp/max_hp, so a huge max_hp with the same low hp ratio gets the same scaling without risking
       the foe actually dying (which would make the post-hit hp delta unmeasurable). */
    low_hp_foe->max_hp = 1000;
    low_hp_foe->hp = 10; /* 1% HP -- near the low_hp_dmg end of the scale */
    int hp_before_low = low_hp_foe->hp;
    arena_cast_q(1);
    int dmg_at_low_hp = hp_before_low - low_hp_foe->hp;

    CHECK(dmg_at_low_hp >= dmg_at_full_hp, "The First Murder deals at least as much damage against a near-dead foe as a full-HP one");
    CHECK(cain->q_cooldown_ms == ARENA_CAIN_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_cain_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    ArenaHero *cain = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = cain->x + ARENA_CAIN_Q_RANGE + 5.0f;
    foe->z = cain->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does not damage the foe");
    CHECK(cain->q_cooldown_ms == 0, "Q out of range does not start its cooldown");
}

static void test_cain_w_dashes_away_from_foe_and_cleanses(void) {
    /* silenced_ms is deliberately NOT pre-set here: silence gates the entire
       cast at arena_toggle_w's own top-level check (same as every hero), so
       pre-silencing Cain to test the cleanse would just block the cast that's
       supposed to do the cleansing -- rooted_ms is the meaningful cleanse to
       verify, since roots don't block casting, only movement. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    ArenaHero *cain = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = cain->x + 2.0f;
    foe->z = cain->z;
    cain->rooted_ms = 500;
    float dist_before = fabsf(cain->x - foe->x);

    arena_toggle_w(1);

    float dist_after = fabsf(cain->x - foe->x);
    CHECK(dist_after > dist_before, "Cursed to Wander dashes Cain away from the nearest enemy, increasing distance");
    CHECK(cain->rooted_ms == 0, "Cursed to Wander cleanses Cain's own root");
    CHECK(cain->w_cooldown_ms == ARENA_CAIN_W_COOLDOWN_MS, "W starts on its own cooldown after cast");
}

static void test_cain_r_arms_the_survive_floor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    ArenaHero *cain = &arena_state.heroes[1];

    arena_cast_r(1);
    CHECK(cain->survive_floor_ms == ARENA_CAIN_R_FLOOR_MS, "R sets the survive floor for its duration");
    CHECK(cain->r_cooldown_ms == ARENA_CAIN_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
}

static void test_gunnr_passive_grants_flat_armor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    CHECK(arena_hero_armor(gunnr) == (float)ARENA_GUNNR_PASSIVE_ARMOR,
          "Gunnr's shieldmaiden stance grants a flat, always-on armor bonus");
}

static void test_gunnr_q_damages_in_melee_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = gunnr->x + 1.5f; /* within ARENA_GUNNR_Q_RANGE */
    foe->z = gunnr->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in melee range");
    CHECK(gunnr->q_cooldown_ms == ARENA_GUNNR_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_gunnr_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = gunnr->x + ARENA_GUNNR_Q_RANGE + 5.0f;
    foe->z = gunnr->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of melee range does not damage the foe");
    CHECK(gunnr->q_cooldown_ms == 0, "Q out of range does not start its cooldown");
}

static void test_gunnr_w_consecration_starts_zone_at_own_feet_on_cooldown(void) {
    /* 2026-07-30, founder: "gunnr w switch it to consecration just like wow" -- W is now a real
       cast on a real cooldown (was a free toggle), a ground zone at Gunnr's OWN position, not a
       targeted ability -- no foe needs to be nearby to cast it at all. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    int mp_before = gunnr->mp;

    arena_toggle_w(1);

    CHECK(gunnr->r_active_ms == ARENA_GUNNR_W_DURATION_MS, "Consecration starts its zone duration on cast");
    CHECK(gunnr->r_zone_x == gunnr->x && gunnr->r_zone_z == gunnr->z, "the zone is centered on Gunnr's own position");
    CHECK(gunnr->w_cooldown_ms == ARENA_GUNNR_W_COOLDOWN_MS, "W starts on its own real cooldown after cast, no longer a free toggle");
    CHECK(gunnr->mp < mp_before, "casting Consecration spends mana, unlike the old free toggle");
}

static void test_gunnr_w_consecration_damages_foe_over_time(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = gunnr->x + 2.0f; /* within ARENA_GUNNR_W_RADIUS */
    foe->z = gunnr->z;
    int foe_hp_before = foe->hp;

    arena_toggle_w(1);

    /* Same "at least one DPS-worth, not exact equality" reasoning
       test_ghost_r_zone_damages_foe_over_time already documents -- Gunnr occupies the bot slot
       here too, so the same arena_update call may also land a real auto-attack. */
    arena_update(1000); /* one full zone tick */
    CHECK(foe->hp <= foe_hp_before - ARENA_GUNNR_W_DPS,
          "Consecration deals at least one DPS-worth of damage per second the foe stands in it");
}

static void test_gunnr_r_executes_harder_at_low_hp(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    ArenaHero *full_hp_foe = &arena_state.heroes[0];
    full_hp_foe->x = gunnr->x + 4.0f;
    full_hp_foe->z = gunnr->z;
    int hp_before_full = full_hp_foe->hp;
    arena_cast_r(1);
    int dmg_at_full_hp = hp_before_full - full_hp_foe->hp;

    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    gunnr = &arena_state.heroes[1];
    ArenaHero *low_hp_foe = &arena_state.heroes[0];
    low_hp_foe->x = gunnr->x + 4.0f;
    low_hp_foe->z = gunnr->z;
    low_hp_foe->max_hp = 1000;
    low_hp_foe->hp = 10; /* 1% HP -- near the low_hp_dmg end of the scale */
    int hp_before_low = low_hp_foe->hp;
    arena_cast_r(1);
    int dmg_at_low_hp = hp_before_low - low_hp_foe->hp;

    CHECK(dmg_at_low_hp >= dmg_at_full_hp, "Valhalla Has Yet To Admit It deals at least as much damage against a near-dead foe as a full-HP one");
    CHECK(gunnr->r_cooldown_ms == ARENA_GUNNR_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
}

static void test_gunnr_r_stuns_foe_in_range(void) {
    /* 2026-07-31, founder: "give gunnrs e a stun" -- same target, same range check R's
       existing damage already uses, no separate targeting pass. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = gunnr->x + 4.0f; /* within ARENA_GUNNR_R_RANGE (6.0) */
    foe->z = gunnr->z;

    arena_cast_r(1);

    CHECK(foe->stunned_ms == ARENA_GUNNR_R_STUN_MS, "Valhalla Has Yet To Admit It stuns a foe in range");
}

static void test_gunnr_r_out_of_range_whiffs_but_still_starts_cooldown(void) {
    /* Gunnr's R dispatch sets the cooldown unconditionally, not gated on a helper's
       return value like Q is -- a deliberate, documented choice for this kit (the R is
       inlined directly in arena_cast_r's switch, not routed through a landed/whiff helper). */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = gunnr->x + ARENA_GUNNR_R_RANGE + 5.0f;
    foe->z = gunnr->z;
    int foe_hp_before = foe->hp;

    arena_cast_r(1);

    CHECK(foe->hp == foe_hp_before, "R out of range does not damage the foe");
    CHECK(foe->stunned_ms == 0, "R out of range does not stun the foe either");
    CHECK(gunnr->r_cooldown_ms == ARENA_GUNNR_R_COOLDOWN_MS, "R still starts its cooldown even on a whiff");
}

/* Warrior (REDGARDEN_GUI_NORTHSTAR.md Milestone 1, 2026-07-31): the first job ported from
 * DragonsNShit's real weapon-skill system, not a TYLER hero -- see arena_game.h's own doc
 * comment on ARENA_WARRIOR_* for the real Hard Slash/Power Slash/Frostbite sourcing. All three
 * are plain melee-range instant hits, same test shape as Gunnr's Q / Zagan's W above. */
static void test_warrior_q_hard_slash_damages_in_melee_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_WARRIOR);
    ArenaHero *warrior = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = warrior->x + 1.5f; /* within ARENA_WARRIOR_Q_RANGE */
    foe->z = warrior->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Hard Slash deals damage when in melee range");
    CHECK(warrior->q_cooldown_ms == ARENA_WARRIOR_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_warrior_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_WARRIOR);
    ArenaHero *warrior = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0]; /* default spawn is well outside melee range */
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Hard Slash out of melee range does not damage the foe");
    CHECK(warrior->q_cooldown_ms == 0, "Q out of range does not start its cooldown");
}

static void test_warrior_w_power_slash_hits_harder_than_q(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_WARRIOR);
    ArenaHero *warrior = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = warrior->x + 1.5f; /* within ARENA_WARRIOR_W_RANGE */
    foe->z = warrior->z;
    int foe_hp_before = foe->hp;

    arena_toggle_w(1);

    CHECK(foe->hp < foe_hp_before, "Power Slash deals damage when in melee range");
    CHECK(ARENA_WARRIOR_W_DAMAGE > ARENA_WARRIOR_Q_DAMAGE, "Power Slash's real weapon-skill damage exceeds Hard Slash's, real FFXI mid-tier > starter WS progression");
    CHECK(warrior->w_cooldown_ms == ARENA_WARRIOR_W_COOLDOWN_MS, "W starts on cooldown after a landed hit");
}

static void test_warrior_r_frostbite_hits_hardest(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_WARRIOR);
    ArenaHero *warrior = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = warrior->x + 1.5f; /* within ARENA_WARRIOR_R_RANGE */
    foe->z = warrior->z;
    int foe_hp_before = foe->hp;

    arena_cast_r(1);

    CHECK(foe->hp < foe_hp_before, "Frostbite deals damage when in melee range");
    CHECK(ARENA_WARRIOR_R_DAMAGE > ARENA_WARRIOR_W_DAMAGE, "Frostbite's real weapon-skill damage exceeds Power Slash's, real FFXI finisher > mid-tier WS progression");
    CHECK(warrior->r_cooldown_ms == ARENA_WARRIOR_R_COOLDOWN_MS, "R starts on cooldown after a landed hit");
}

/* Skillchain resonance (REDGARDEN_GUI_NORTHSTAR.md Milestone 2, 2026-07-31): Warrior's own Q
 * (Scission) and R (Induration+Reverberation) are the one real, already-buildable in-kit pair
 * that closes per `server/skillchain`'s own real combination table -- {Scission, Reverberation}
 * = Distortion, Tier 2, 35% bonus (server/skillchain.go's own `combinationTable`). Q and R use
 * separate cooldown fields, so both land in the same test with no cooldown reset needed. */
static void test_warrior_q_then_r_closes_a_real_skillchain(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_WARRIOR);
    ArenaHero *warrior = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = warrior->x + 1.5f; /* within both ARENA_WARRIOR_Q_RANGE and ARENA_WARRIOR_R_RANGE */
    foe->z = warrior->z;

    arena_cast_q(1); /* Scission lands, opens the chain window */
    CHECK(foe->sc_pending_attr_count == 1, "Hard Slash opens a pending skillchain window on the target");
    CHECK(foe->skillchain_flash_tier == 0, "the FIRST weapon skill on a fresh target never closes a chain by itself");

    int hp_before_r = foe->hp;
    arena_cast_r(1); /* Induration+Reverberation lands within the window -- Reverberation closes vs. the pending Scission */
    int dmg_with_chain = hp_before_r - foe->hp;

    CHECK(foe->skillchain_flash_tier == 2, "Scission+Reverberation closes a real Tier 2 (Distortion) skillchain");
    /* apply_armor floors damage at 1 and subtracts a flat armor value, so the raw constant
       times the multiplier isn't exactly reproducible here -- assert the chained hit did
       strictly more damage than the unchained baseline instead, same "measure it, don't assume
       armor math" discipline test_warrior_r_frostbite_hits_hardest's own sibling tests use. */
    int baseline_armor_reduced = ARENA_WARRIOR_R_DAMAGE - (int)arena_hero_armor(foe);
    if (baseline_armor_reduced < 1) baseline_armor_reduced = 1;
    CHECK(dmg_with_chain > baseline_armor_reduced, "a closed skillchain deals strictly more damage than Frostbite alone would");
}

static void test_warrior_skillchain_window_expires(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_WARRIOR);
    ArenaHero *warrior = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = warrior->x + 1.5f;
    foe->z = warrior->z;

    arena_cast_q(1);
    foe->sc_pending_age_ms = ARENA_SKILLCHAIN_WINDOW_MS + 1; /* simulate the window having closed */

    arena_cast_r(1);

    CHECK(foe->skillchain_flash_tier == 0, "no chain closes once the pending window has expired");
}

/* The Cart (TYLER multiverse_heroes.md #10, NORTHSTAR §24 Milestone 2, 2026-07-31). */

static void test_cart_q_heals_self_capped_at_max(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CART);
    ArenaHero *cart = &arena_state.heroes[1];
    cart->hp = cart->max_hp - 3; /* less than ARENA_CART_Q_HEAL away from full, forces the cap */

    arena_cast_q(1);

    CHECK(cart->hp == cart->max_hp, "Q heals the Cart, capped at max_hp rather than overhealing");
    CHECK(cart->q_cooldown_ms == ARENA_CART_Q_COOLDOWN_MS, "Q starts on cooldown after use");
}

static void test_cart_w_opens_a_zone_at_own_position(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CART);
    ArenaHero *cart = &arena_state.heroes[1];

    arena_toggle_w(1);

    CHECK(cart->r_active_ms == ARENA_CART_W_DURATION_MS, "W opens the delivery zone for its real duration");
    CHECK(cart->r_zone_x == cart->x && cart->r_zone_z == cart->z, "the zone is centered on the Cart's own position, no target needed");
    CHECK(cart->zone_radius == ARENA_CART_W_RADIUS, "zone_radius records W's own radius, not R's");
    CHECK(cart->w_cooldown_ms == ARENA_CART_W_COOLDOWN_MS, "W starts on cooldown after cast");
}

static void test_cart_r_zone_is_bigger_and_longer_cooldown_than_w(void) {
    /* Real, structural check on the constants themselves -- same "R is the bigger version of the
       kit's theme" convention every other hero's own R follows (Warrior's Frostbite > Power
       Slash, etc.), pinned down for the Cart's own delivery zone. */
    CHECK(ARENA_CART_R_RADIUS > ARENA_CART_W_RADIUS, "R's delivery zone reaches farther than W's");
    CHECK(ARENA_CART_R_COOLDOWN_MS > ARENA_CART_W_COOLDOWN_MS, "R's longer reach costs a longer cooldown");
}

static void test_cart_zone_triggers_delivery_on_contact_then_deactivates(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CART);
    /* Same "isolate from the internal practice-opponent AI" fix
       test_arena_bot_enabled_gates_kit_casts_too's own comment documents -- without this, the
       Cart's own bot_cast_kit_if_ready heuristic (owner 1 is bot-driven by default) immediately
       re-casts R right after this test's own manual W cast, since both start off cooldown and
       W/R share the same r_zone fields (see arena_cast_r's own CART case doc comment). Every
       other hero's own zone tests never hit this because their W/R don't share overwriteable
       state the way the Cart's do. */
    arena_bot_enabled = 0;
    ArenaHero *cart = &arena_state.heroes[1];
    ArenaHero *other = &arena_state.heroes[0];
    other->x = cart->x + 1.0f; /* well within ARENA_CART_W_RADIUS (3.0) */
    other->z = cart->z;
    int hp_before = other->hp, mp_before = other->mp, flow_before = other->flow;
    int hp_max_before = other->hp == other->max_hp;

    arena_toggle_w(1);
    arena_update(100); /* one real tick -- the sweep runs every tick, not on a fixed interval like a DPS zone */

    CHECK(cart->r_active_ms == 0, "the zone deactivates immediately once it delivers -- single-use, not a repeat-tick zone");
    /* Exactly one of 4 real outcomes landed -- don't assert which (real rand()), just that
       SOMETHING real happened rather than nothing. hp_max_before guards against a false negative
       if the heal outcome rolled but other was already at full HP. */
    int something_changed = (other->hp != hp_before) || (other->mp != mp_before) ||
                             (other->flow != flow_before) || (other->slowed_ms > 0) || hp_max_before;
    CHECK(something_changed, "the delivery zone applies one of its 4 real outcomes to whoever steps in, ally or foe");
    arena_bot_enabled = 1; /* restore the default for any test run after this one */
}

static void test_cart_zone_can_trigger_on_the_cart_itself(void) {
    /* "nobody, including its own controller, gets to request what" -- the Cart's own lore.
       With no other hero in range, the zone must still be able to fire on the Cart itself, not
       silently expire unused. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CART);
    arena_bot_enabled = 0; /* see the sibling test above for why this is needed here */
    ArenaHero *cart = &arena_state.heroes[1];
    ArenaHero *other = &arena_state.heroes[0];
    other->x = cart->x + 500.0f; /* far outside any real zone radius */

    arena_toggle_w(1);
    arena_update(100);

    CHECK(cart->r_active_ms == 0, "with nobody else in range, the zone fires on the Cart's own controller instead of expiring unused");
    arena_bot_enabled = 1; /* restore the default for any test run after this one */
}

static void test_vassago_passive_regenerates_hp(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_VASSAGO);
    ArenaHero *vassago = &arena_state.heroes[1];
    vassago->hp = 50;

    arena_update(1000); /* one full second of passive regen */

    CHECK(vassago->hp > 50, "The passive regenerates HP every tick with no cast at all");
}

static void test_vassago_q_damages_and_silences_in_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_VASSAGO);
    ArenaHero *vassago = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = vassago->x + 4.0f; /* within ARENA_VASSAGO_Q_RANGE */
    foe->z = vassago->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in range");
    CHECK(foe->silenced_ms == ARENA_VASSAGO_Q_SILENCE_MS, "Reveal the Gentle Maybe silences the foe on a landed hit");
    CHECK(vassago->q_cooldown_ms == ARENA_VASSAGO_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_vassago_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_VASSAGO);
    ArenaHero *vassago = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = vassago->x + ARENA_VASSAGO_Q_RANGE + 5.0f;
    foe->z = vassago->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does not damage the foe");
    CHECK(foe->silenced_ms == 0, "Q out of range does not silence the foe");
}

static void test_vassago_w_grants_ally_next_cast_refund(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_VASSAGO;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally */

    arena_toggle_w(0);

    CHECK(arena_state.heroes[1].next_cast_refund == 1, "The Soft Foresight grants the nearest ally next_cast_refund");
    CHECK(arena_state.heroes[0].w_cooldown_ms == ARENA_VASSAGO_W_COOLDOWN_MS, "W starts on its own cooldown after cast");
}

static void test_vassago_w_no_ally_in_1v1_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_VASSAGO);
    ArenaHero *vassago = &arena_state.heroes[1];

    arena_toggle_w(1);

    CHECK(vassago->w_cooldown_ms == 0, "no ally in 1v1 means the cast whiffs -- cooldown is not consumed");
}

static void test_vassago_r_zone_silences_but_deals_no_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_VASSAGO;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    /* 3.0 units away: inside ARENA_VASSAGO_R_RADIUS (4.5) but outside melee auto-attack
       range (~1.6) -- close enough for x=1 would let the two heroes auto-attack each other
       for ordinary melee damage in the same tick, which isn't what this test measures. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);
    CHECK(arena_state.heroes[0].r_active_ms == ARENA_VASSAGO_R_DURATION_MS, "R starts its zone duration on cast");
    CHECK(arena_state.heroes[0].r_cooldown_ms == ARENA_VASSAGO_R_COOLDOWN_MS, "R starts on its own cooldown after cast");

    arena_update_teams(1000); /* one full 1000ms zone tick */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].silenced_ms > 0, "The Gentle Maybe silences an enemy standing in the zone");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100, "The Gentle Maybe deals no damage at all -- pure control, not a hit");
}

static void test_he_xiangu_passive_regenerates_hp(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_HE_XIANGU);
    ArenaHero *he_xiangu = &arena_state.heroes[1];
    he_xiangu->hp = 50;

    arena_update(1000);

    CHECK(he_xiangu->hp > 50, "The passive regenerates HP every tick with no cast at all");
}

/* Moira Orb redesign (2026-08-26): Q no longer instant-hitscans -- see
   arena_game.h's own ARENA_HE_XIANGU_Q_ORB_SPEED doc comment for the full founder-quote
   chain. Casting now only SPAWNS a real homing projectile (arena_spawn_projectile +
   homing_target); damage lands once it actually travels there, so these tests tick the sim
   forward after casting instead of checking foe->hp immediately. Self-heal is still
   immediate (cast-time, not hit-dependent -- a real, deliberate simplification, not an
   oversight, see the same header comment). */
static void test_he_xiangu_q_spawns_a_homing_orb_and_heals_self_on_cast(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_HE_XIANGU);
    ArenaHero *he_xiangu = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = he_xiangu->x + 4.0f;
    foe->z = he_xiangu->z;
    he_xiangu->hp = 50;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(he_xiangu->hp == 50 + ARENA_HE_XIANGU_Q_ORB_SELF_HEAL, "the self-heal fires immediately on cast, not on hit");
    CHECK(he_xiangu->q_cooldown_ms == ARENA_HE_XIANGU_Q_COOLDOWN_MS, "Q starts on cooldown on cast, not on landing");

    int found_homing_orb = 0;
    for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
        if (arena_state.projectiles[i].active && arena_state.projectiles[i].owner == 1 &&
            arena_state.projectiles[i].homing_target == 0) {
            found_homing_orb = 1;
        }
    }
    CHECK(found_homing_orb, "casting Q spawns a real homing projectile locked onto the nearest enemy");

    for (int t = 0; t < 200 && foe->hp == foe_hp_before; t++) arena_update(16);
    CHECK(foe->hp < foe_hp_before, "the homing orb actually lands and damages the foe once it travels there");
}

/* "no matter how far away" (founder, real-time, 2026-08-26): Q no longer has a range limit at
   all -- it auto-targets the nearest enemy anywhere on the map (arena_nearest_enemy has no
   range cap of its own). This replaces the old test_he_xiangu_q_out_of_range_whiffs, which
   tested a "range gate" concept that no longer exists by design. */
static void test_he_xiangu_q_has_no_range_limit(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_HE_XIANGU);
    ArenaHero *he_xiangu = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = he_xiangu->x + ARENA_HE_XIANGU_Q_RANGE + 30.0f; /* well past the old, now-dead range constant */
    foe->z = he_xiangu->z;
    he_xiangu->hp = 50;

    arena_cast_q(1);

    CHECK(he_xiangu->hp == 50 + ARENA_HE_XIANGU_Q_ORB_SELF_HEAL,
          "Q still casts (and heals) against a foe far past the old range limit -- no more whiffing on distance");
}

static void test_he_xiangu_w_is_a_free_toggle_regen(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_HE_XIANGU);
    ArenaHero *he_xiangu = &arena_state.heroes[1];
    he_xiangu->hp = 50;

    arena_toggle_w(1);
    CHECK(he_xiangu->w_active, "W toggles on");
    CHECK(he_xiangu->w_cooldown_ms == 0, "W is a free toggle, no cooldown");

    arena_update(1000);
    CHECK(he_xiangu->hp > 52, "Self-Denial regenerates HP on top of the base passive while toggled on");
}

static void test_he_xiangu_r_zone_heals_ally_no_enemy_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_HE_XIANGU;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally, inside the zone */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    /* 3.0 units away: inside ARENA_HE_XIANGU_R_RADIUS but outside melee auto-attack range,
       same reasoning as Vassago's equivalent test -- isolates the zone's own heal-only
       property from ordinary melee combat between the two heroes. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);
    CHECK(arena_state.heroes[0].r_active_ms == ARENA_HE_XIANGU_R_DURATION_MS, "R starts its zone duration on cast");
    CHECK(arena_state.heroes[0].r_cooldown_ms == ARENA_HE_XIANGU_R_COOLDOWN_MS, "R starts on its own cooldown after cast");

    arena_update_teams(1000);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_HE_XIANGU_R_HEAL_PER_TICK,
          "Never Once Framed It As Sacrifice heals an ally standing in the zone");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100,
          "the zone deals no damage at all -- pure support, not a hit");
}

static void test_beleth_passive_grants_flat_armor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_BELETH);
    ArenaHero *beleth = &arena_state.heroes[1];
    CHECK(arena_hero_armor(beleth) == (float)ARENA_BELETH_PASSIVE_ARMOR,
          "Beleth's own survival grants a flat, always-on armor bonus");
}

static void test_beleth_q_damages_and_burns_foe(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_BELETH);
    ArenaHero *beleth = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = beleth->x + 4.0f; /* within ARENA_BELETH_Q_RANGE */
    foe->z = beleth->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in range");
    CHECK(foe->burning_ms == ARENA_BELETH_Q_BURN_MS, "Q applies the burn DoT");
    CHECK(foe->burn_dps == ARENA_BELETH_Q_BURN_DPS, "the burn ticks at Beleth's own Q burn rate");
    CHECK(beleth->q_cooldown_ms == ARENA_BELETH_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_beleth_w_silences_no_damage(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_BELETH);
    ArenaHero *beleth = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = beleth->x + 4.0f; /* within ARENA_BELETH_W_RANGE */
    foe->z = beleth->z;
    int foe_hp_before = foe->hp;

    arena_toggle_w(1);

    CHECK(foe->silenced_ms == ARENA_BELETH_W_SILENCE_MS, "W silences the nearest enemy");
    CHECK(foe->hp == foe_hp_before, "W is escalation-denial only -- it deals no damage at all");
    CHECK(beleth->w_cooldown_ms == ARENA_BELETH_W_COOLDOWN_MS, "W starts on its own cooldown after a landed decree");
}

static void test_beleth_r_marks_zone_no_immediate_damage(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_BELETH);
    ArenaHero *beleth = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = beleth->x + 3.0f; /* within ARENA_BELETH_R_RANGE, and later inside R_RADIUS too */
    foe->z = beleth->z;
    int foe_hp_before = foe->hp;

    arena_cast_r(1);

    CHECK(beleth->r_active_ms == ARENA_BELETH_R_FUSE_MS, "R starts the fuse on cast");
    CHECK(beleth->r_cooldown_ms == ARENA_BELETH_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
    CHECK(foe->hp == foe_hp_before, "the detonation hasn't happened yet -- no damage at cast time, only a mark");
}

static void test_beleth_r_detonates_after_fuse(void) {
    /* Team mode, not arena_init_with_heroes/arena_update: the 1v1 local-demo path's
       arena_update runs an autonomous chase-bot on owner 1 (arena_bot_enabled defaults on)
       that would close the distance to melee range well within the fuse's 1.8s window at
       ARENA_HERO_SPEED, contaminating the burst-damage check with an extra melee trade --
       found via this exact test failing. Team mode's arena_update_teams has no such chase
       AI (update_hero_motion only moves a hero toward an explicitly-set target), same
       reasoning as Vassago's/He Xiangu's own R tests using team mode for their zone checks. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) {
        if (i == ARENA_TEAM_SIZE) continue;
        arena_state.heroes[i].active = 0;
    }
    arena_state.heroes[0].hero_id = ARENA_HERO_BELETH;
    /* z=15: off every node's aggro/capture footprint (the Blacksmith node sits at (0,0), same
       real bug this session already hit once for a different hero's test -- a node-guardian creep
       spawning on the node dealt real damage the strict-equality check misattributed to the
       ability itself). heroes[ARENA_TEAM_SIZE]: the enemy team, same convention as Vassago's/
       He Xiangu's own team-mode R tests (heroes[0]/[1] are the SAME team by default). 3.0
       units: inside ARENA_BELETH_R_RADIUS but outside melee auto-attack range, isolating the
       zone's own effect from ordinary melee. */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 15.0f;
    /* arena_init_teams() leaves every hero_id at its own ARENA_HERO_UNICORN placeholder
       ("until the real client's draft pick overrides it", per its own comment) -- Unicorn
       carries a flat +4 armor passive, which would silently eat 4 of this test's exact
       damage figure. Duck has no passive at all, so the burst lands un-mitigated. */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 15.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_r(0);
    arena_update_teams(ARENA_BELETH_R_FUSE_MS); /* one big tick, past the whole fuse */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before - ARENA_BELETH_R_DAMAGE,
          "the fuse hitting zero deals ONE full, un-mitigated burst to whoever's still in the zone");
    CHECK(arena_state.heroes[0].r_active_ms == 0, "the fuse doesn't go negative or wrap, it pins at zero");
}

static void test_beleth_r_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_BELETH);
    ArenaHero *beleth = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = beleth->x + ARENA_BELETH_R_RANGE + 5.0f;
    foe->z = beleth->z;

    arena_cast_r(1);

    CHECK(beleth->r_active_ms == 0, "R out of range doesn't start the fuse -- it whiffed, not cast");
    CHECK(beleth->r_cooldown_ms == 0, "a whiffed R doesn't consume the cooldown either");
}

static void test_mnm_passive_grants_flat_armor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_MNM);
    ArenaHero *mnm = &arena_state.heroes[1];
    CHECK(arena_hero_armor(mnm) == (float)ARENA_MNM_PASSIVE_ARMOR,
          "MnM's shell grants a flat, always-on armor bonus even with W off");
}

static void test_mnm_w_burrow_grants_intangible_and_root(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_MNM);
    ArenaHero *mnm = &arena_state.heroes[1];

    arena_toggle_w(1);

    CHECK(mnm->intangible_ms == ARENA_MNM_BURROW_DURATION_MS, "Burrow makes MnM untargetable for its duration");
    CHECK(mnm->rooted_ms == ARENA_MNM_BURROW_DURATION_MS, "Burrow roots MnM in place -- he resurfaces where he went under");
    CHECK(mnm->mnm_burrow_ms == ARENA_MNM_BURROW_DURATION_MS, "Burrow's own dedicated countdown starts");
    CHECK(mnm->w_cooldown_ms == ARENA_MNM_BURROW_COOLDOWN_MS, "Burrow starts on a real cooldown, not a free toggle");
}

static void test_mnm_w_burrow_no_longer_grants_armor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_MNM);
    ArenaHero *mnm = &arena_state.heroes[1];
    float before = arena_hero_armor(mnm);

    arena_toggle_w(1);

    CHECK(arena_hero_armor(mnm) == before, "S170-208: Burrow replaced the old free armor-stack toggle -- casting it doesn't change armor at all");
    CHECK(arena_hero_armor(mnm) == (float)ARENA_MNM_PASSIVE_ARMOR, "armor is just the flat passive now, no toggle bonus exists any more");
}

static void test_mnm_w_burrow_respects_cooldown(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_MNM);
    ArenaHero *mnm = &arena_state.heroes[1];

    arena_toggle_w(1);
    int first_burrow_ms = mnm->mnm_burrow_ms;
    arena_toggle_w(1); /* still on cooldown -- must no-op, not refresh/stack */

    CHECK(mnm->mnm_burrow_ms == first_burrow_ms, "a second Burrow attempt while on cooldown does nothing");
}

static void test_mnm_w_burrow_erupts_for_aoe_damage_on_resurface(void) {
    /* arena_init_teams() + arena_update_teams(), not arena_init_with_heroes() +
       arena_update() -- the latter pair runs the legacy 1v1 resolve_combat path AND the
       internal bot AI (bot_cast_kit_if_ready fires for owner 1 every tick, S170-208's own
       burrow gate doesn't cover ability casts, just auto-attacks), both of which would
       contaminate the exact "no damage until the eruption" timing this test depends on.
       Same team-mode setup test_gary_w_completes_and_deals_damage_after_full_duration and
       test_mnm_r_survive_floor_actually_blocks_lethal_damage above already use. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_MNM;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_UNICORN;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; /* within ARENA_MNM_BURROW_RADIUS when he resurfaces */
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    ArenaHero *mnm = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[ARENA_TEAM_SIZE];
    int foe_hp_before = foe->hp;

    arena_toggle_w(0);
    /* Two ticks spanning the full duration -- mnm_burrow_ms is decremented within
       arena_update_teams itself (same "begin and tick-down share one call" shape as the
       auto-attack windup elsewhere in this file), so a single oversized tick risks
       clipping the exact zero-crossing. */
    arena_update_teams(ARENA_MNM_BURROW_DURATION_MS - 16);
    CHECK(foe->hp == foe_hp_before, "no eruption damage yet -- MnM is still underground");
    arena_update_teams(32); /* crosses zero */

    CHECK(mnm->mnm_burrow_ms == 0, "Burrow's countdown reaches zero and stays there");
    CHECK(foe->hp < foe_hp_before, "the eruption on resurfacing deals AoE damage to whoever's standing there");
}

static void test_mnm_w_burrow_no_eruption_damage_out_of_radius(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_MNM;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_UNICORN;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_MNM_BURROW_RADIUS + 5.0f; /* well outside the eruption radius */
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    ArenaHero *foe = &arena_state.heroes[ARENA_TEAM_SIZE];
    int foe_hp_before = foe->hp;

    arena_toggle_w(0);
    arena_update_teams(ARENA_MNM_BURROW_DURATION_MS + 32);

    CHECK(foe->hp == foe_hp_before, "a foe standing outside the eruption radius takes no damage");
}

static void test_mnm_q_damages_and_roots_in_melee_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_MNM);
    ArenaHero *mnm = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = mnm->x + 1.5f; /* within ARENA_MNM_Q_RANGE */
    foe->z = mnm->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in melee range");
    CHECK(foe->rooted_ms == ARENA_MNM_Q_ROOT_MS, "Q roots the foe on a landed hit");
    CHECK(mnm->q_cooldown_ms == ARENA_MNM_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_mnm_r_roots_self_and_grants_survive_floor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_MNM);
    ArenaHero *mnm = &arena_state.heroes[1];

    arena_cast_r(1);

    CHECK(mnm->rooted_ms == ARENA_MNM_R_ROOT_MS, "R roots MnM in place");
    CHECK(mnm->survive_floor_ms == ARENA_MNM_R_SURVIVE_FLOOR_MS, "R grants the guaranteed-survival window");
    CHECK(mnm->r_cooldown_ms == ARENA_MNM_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
}

static void test_mnm_r_survive_floor_actually_blocks_lethal_damage(void) {
    /* Same real-ability-not-fake-damage pattern as test_pizza_r_prevents_death_for_duration:
       a real Duck Q (Telekinetic Yank), not a synthetic damage injection. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MNM;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].hp = 1; arena_state.heroes[0].max_hp = 100; /* one hit from death */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_DUCK_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_r(0); /* Absorbing Hits Meant For Somebody Else */
    arena_cast_q(ARENA_TEAM_SIZE); /* Duck's Telekinetic Yank, would normally kill a 1-HP target */

    CHECK(arena_state.heroes[0].hp == 1, "the shell absorbs it -- HP floors at 1 against lethal damage");
    CHECK(arena_state.heroes[0].alive, "MnM survives a hit that would kill anyone else on the roster outright");
}

/* S170-136: Gary's Q is now a real travelling projectile, not an instant
 * hit -- these tests exercise the whole shape: cast spawns a projectile
 * (no immediate damage), the projectile travels and lands after enough
 * ticks, a target that steps off the line before it arrives genuinely
 * dodges it, and an unhit shot despawns cleanly once it exceeds its range
 * rather than lingering forever. */

static void test_gary_q_cast_spawns_projectile_no_instant_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before,
          "casting Q does not deal instant damage -- it fires a projectile instead");
    ArenaProjectile *p = find_active_projectile();
    CHECK(p != NULL, "a projectile is actually spawned on cast");
    CHECK(arena_state.heroes[0].q_cooldown_ms == ARENA_GARY_Q_COOLDOWN_MS,
          "cooldown is spent on cast, same as every other ability, regardless of the shot's eventual outcome");
}

static void test_gary_q_out_of_range_whiffs_no_projectile(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_Q_RANGE + 5.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_q(0);

    CHECK(find_active_projectile() == NULL, "no projectile spawns when no foe is in range at cast time");
    CHECK(arena_state.heroes[0].q_cooldown_ms == 0, "an out-of-range whiff doesn't consume the cooldown, same convention as every other Q");
}

static void test_gary_q_projectile_lands_after_travel_time(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "no damage the instant the shot is fired");

    /* Foe stays put -- the shot should reach it well within one full second
       of flight given ARENA_GARY_Q_PROJECTILE_SPEED. */
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before, "a stationary target is hit once the projectile travels far enough to reach it");
    CHECK(find_active_projectile() == NULL, "the projectile deactivates on hit, doesn't linger");
}

static void test_gary_q_projectile_misses_if_target_steps_off_line(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    ArenaProjectile *p = find_active_projectile();
    CHECK(p != NULL, "projectile spawned");

    /* Real dodge: step far off the original firing line before the shot
       arrives, well clear of the projectile's hit radius. */
    arena_state.heroes[ARENA_TEAM_SIZE].z = 10.0f;
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before,
          "a target that steps off the firing line before the shot arrives takes no damage -- a real dodge, not homing");
}

static void test_gary_q_projectile_despawns_after_max_range_unhit(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_q(0);
    /* Move the foe out of the way immediately, then run well past the time
       the shot would need to exceed its own max_range. */
    arena_state.heroes[ARENA_TEAM_SIZE].z = 10.0f;
    for (int i = 0; i < 200; i++) arena_tick_projectiles(16);

    CHECK(find_active_projectile() == NULL, "an unhit projectile despawns once it exceeds its max range, doesn't travel forever");
}

/* S170-203: Gary's W (Aimed Shot) -- a real WoW Hunter-style cast-time nuke. Founder: "switch
 * gary w to aimed shot just like wow hunter cast time big damage for now movement interrupts
 * cast damage does not interrupt cast silence does." These tests exercise the whole shape:
 * cast begins only with a hittable foe in range, damage lands only after the full wind-up,
 * movement interrupts (self-moved OR forced), silence interrupts, damage taken does not. */

static void test_gary_w_cast_begins_with_hittable_foe_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_W_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].casting_slot == 2, "W begins a real cast (slot 2), not an instant effect");
    CHECK(arena_state.heroes[0].cast_time_remaining_ms == ARENA_GARY_W_CAST_MS, "cast starts at the full wind-up duration");
    CHECK(arena_state.heroes[0].cast_target == ARENA_TEAM_SIZE, "the hittable foe in range at cast start is locked in as the target");
    CHECK(arena_state.heroes[0].w_cooldown_ms == ARENA_GARY_W_COOLDOWN_MS, "cooldown is spent the instant the cast begins");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "no damage yet -- the shot hasn't fired, only begun aiming");
}

static void test_gary_w_no_foe_in_range_is_noop(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_W_RANGE + 5.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].casting_slot == 0, "no foe in range at cast time -- no cast begins, same 'needs a shot lined up' commitment as Q");
    CHECK(arena_state.heroes[0].w_cooldown_ms == 0, "a whiff doesn't consume the cooldown");
}

static void test_gary_w_completes_and_deals_damage_after_full_duration(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    /* Duck, not the default hero_id 0 (Unicorn) arena_init_teams would otherwise leave this
       slot at -- Unicorn's own passive base armor would mitigate the exact-value CHECK below;
       Duck carries no such passive, so the raw ARENA_GARY_W_DAMAGE arrives unmitigated. */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_W_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_toggle_w(0);
    arena_update_teams((unsigned int)ARENA_GARY_W_CAST_MS);

    CHECK(arena_state.heroes[0].casting_slot == 0, "the cast clears itself once it completes");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before - ARENA_GARY_W_DAMAGE,
          "the full wind-up elapsing with nothing interrupting it lands the real Aimed Shot damage");
}

static void test_gary_w_movement_interrupts_cast(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_W_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_toggle_w(0);
    /* Any real drift off the cast-start anchor interrupts, whether it's a deliberate move
       command or a forced displacement (a pull, a knockback) -- mutating x directly here
       covers both cases identically, since the check is purely position-based. */
    arena_state.heroes[0].x = 1.0f;
    arena_update_teams((unsigned int)ARENA_GARY_W_CAST_MS);

    CHECK(arena_state.heroes[0].casting_slot == 0, "moving mid-cast cancels it");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "an interrupted cast deals no damage");
}

static void test_gary_w_damage_does_not_interrupt_cast(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    /* Duck, not the default hero_id 0 (Unicorn) -- see the completes-and-deals-damage test's
       own comment for why the exact-value CHECK below needs an armor-passive-free target. */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_W_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_toggle_w(0);
    arena_state.heroes[0].hp -= 10; /* the caster takes damage mid-cast -- founder, explicit: this must not interrupt */
    arena_update_teams((unsigned int)ARENA_GARY_W_CAST_MS);

    CHECK(arena_state.heroes[0].casting_slot == 0, "the cast still completes normally");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before - ARENA_GARY_W_DAMAGE,
          "taking damage mid-cast does not interrupt it -- the shot still lands");
}

static void test_gary_w_silence_interrupts_cast(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_W_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_toggle_w(0);
    arena_state.heroes[0].silenced_ms = 500; /* lands mid-cast */
    /* Two steps, not one big tick spanning the whole cast: silenced_ms and the interrupt check
       that reads it both live in the same per-tick pass, so a single tick covering the entire
       remaining duration would decay silenced_ms to 0 (500 - 1500, clamped) in the same instant
       the check runs, silently passing the interrupt check it's supposed to trip. A small first
       tick leaves silenced_ms genuinely > 0 when the check evaluates it, same as it would be at
       any real, non-test frame rate. */
    arena_update_teams(100);
    CHECK(arena_state.heroes[0].casting_slot == 0, "silence lands mid-cast and cancels it");
    arena_update_teams((unsigned int)ARENA_GARY_W_CAST_MS);
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "an interrupted cast deals no damage even once the original wind-up duration has fully elapsed");
}

/* S170-140: Tyler's Q (Earthbind) converted from an instant hit to a real
 * projectile, carrying both root and burn as on-hit effects. */
static void test_tyler_q_cast_spawns_projectile_no_instant_effect(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_TYLER_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "casting Q does not deal instant damage -- it fires a projectile instead");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == 0, "no instant root either");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].burning_ms == 0, "no instant burn either");
    CHECK(find_active_projectile() != NULL, "a projectile is actually spawned on cast");
    CHECK(arena_state.heroes[0].q_cooldown_ms == ARENA_TYLER_Q_COOLDOWN_MS, "cooldown is spent on cast");
}

static void test_tyler_q_projectile_roots_and_burns_on_hit(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_TYLER_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before, "a stationary target is hit once the net reaches it");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == ARENA_TYLER_Q_ROOT_MS, "Earthbind roots the foe on a landed hit");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].burning_ms == ARENA_TYLER_Q_BURN_MS, "...and applies the burn DoT too, both carried by the same projectile");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].burn_dps == ARENA_TYLER_Q_BURN_DPS, "burn DoT rate matches Tyler's own Q burn rate");
}

static void test_tyler_q_projectile_misses_if_target_steps_off_line(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_TYLER_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    arena_state.heroes[ARENA_TEAM_SIZE].z = 10.0f;
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "a target that steps off the net's line takes no damage");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == 0, "...and isn't rooted");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].burning_ms == 0, "...or burned -- a real dodge, not homing");
}

/* S170-141: Tyler's puppet clones ("true Meepo parity"). See
 * docs/HEROES_VS0.md's Tyler section for the full design/scope note. */
static void test_tyler_r_spawns_clones_linked_to_caster(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 5.0f; arena_state.heroes[0].z = 2.0f;

    arena_cast_r(0);

    int clone_count = 0;
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *c = &arena_state.heroes[i];
        if (!c->active) continue;
        clone_count++;
        CHECK(c->is_clone == 1, "each spawned puppet is marked is_clone");
        CHECK(c->clone_owner == 0, "each spawned puppet links back to Tyler's own owner slot");
        CHECK(c->team == arena_state.heroes[0].team, "a clone shares Tyler's team");
        CHECK(c->hp == (int)(arena_state.heroes[0].max_hp * ARENA_TYLER_CLONE_HP_PCT),
              "a clone spawns with the documented fraction of Tyler's max HP");
    }
    CHECK(clone_count == ARENA_TYLER_R_CLONE_COUNT, "R spawns the documented number of clones");
}

static void test_tyler_clone_stays_put_without_its_own_command(void) {
    /* 2026-07-30, Tyler "Divided We Stand" rework -- founder: "clones multi control drag click
       all of it." Real Meepo parity means each net is independently commanded, not an auto-
       following puppet -- replaces the old test of the exact opposite behavior (S170-141's
       "clones mirror Tyler's own move-target," now deliberately removed). */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;

    arena_cast_r(0);
    arena_set_move_target(0, 20.0f, 0.0f); /* Tyler's own move command only */

    for (int i = 0; i < 30; i++) arena_update_teams(16);

    int any_clone_moved = 0;
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *c = &arena_state.heroes[i];
        if (c->active && (fabsf(c->x - 0.0f) > 0.5f || fabsf(c->z - 0.0f) > 0.5f)) any_clone_moved = 1;
    }
    CHECK(!any_clone_moved, "a clone does NOT auto-follow Tyler's own move command anymore -- it needs its own");
}

static void test_tyler_clone_moves_and_fights_on_its_own_independent_command(void) {
    /* The other half of the same rework: a clone given its OWN explicit move command (exactly
       the way a real player would drag-select it and click, see apps/arena's own selection UI)
       advances and fights through the generic combat loop, completely independent of whatever
       Tyler himself is doing. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;

    arena_cast_r(0);
    int clone_owner = -1;
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        if (arena_state.heroes[i].active) { clone_owner = i; break; }
    }
    CHECK(clone_owner >= 0, "sanity: a clone actually spawned");

    /* Tyler himself gets no command at all -- only the clone does. */
    arena_set_move_target(clone_owner, 20.0f, 0.0f);

    /* Place a lone enemy exactly where the clone is marching through, far from Tyler himself, so
       only the clone (not the real Tyler, who never moved) can be the one that lands the hit. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.5f;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    for (int i = 0; i < 30; i++) arena_update_teams(16);

    CHECK(arena_state.heroes[0].x < 1.0f, "sanity: Tyler himself never moved, no command was sent to him");
    CHECK(arena_state.heroes[clone_owner].x > 0.5f, "the clone advances on its own explicit move command");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before,
          "an enemy near the independently-commanded clone takes real damage -- fights through the generic melee loop");
}

static void test_arena_owner_controls_self_and_own_clones_only(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_cast_r(0);
    int clone_owner = -1;
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        if (arena_state.heroes[i].active) { clone_owner = i; break; }
    }
    CHECK(clone_owner >= 0, "sanity: a clone actually spawned");

    CHECK(arena_owner_controls(0, 0), "Tyler controls himself");
    CHECK(arena_owner_controls(0, clone_owner), "Tyler controls his own active clone");
    CHECK(!arena_owner_controls(1, clone_owner), "a different owner does not control Tyler's clone");
    CHECK(!arena_owner_controls(0, 1), "Tyler does not control another real hero's slot");
    CHECK(!arena_owner_controls(0, -1), "an out-of-range target is never controlled by anyone");
    CHECK(!arena_owner_controls(0, ARENA_HEROES_ARRAY_SIZE), "an out-of-range target past the end is never controlled by anyone");
}

static void test_tyler_w_teleports_the_whole_clone_army(void) {
    /* S170-170, "true meepo parity" follow-up: docs/HEROES_VS0.md's own
       S170-141 scope note flagged this as "a real next step, not attempted
       this pass" -- W used to move only Tyler's own body. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_cast_r(0); /* spawn the clone army, far from the eventual W target */

    arena_state.heroes[ARENA_TEAM_SIZE].x = 30.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 1000;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].x == 30.0f && arena_state.heroes[0].z == 0.0f,
          "Tyler himself still blinks to the target as before");
    int clones_teleported = 0;
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *c = &arena_state.heroes[i];
        if (!c->active) continue;
        if (c->x == 30.0f && c->z == 0.0f) clones_teleported++;
    }
    CHECK(clones_teleported == ARENA_TYLER_R_CLONE_COUNT,
          "every active clone teleports alongside Tyler to the exact same point, not just his own body");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before,
          "the target takes real arrival damage from the concentrated clone-army landing");
}

/* S170-175: Flow/XP economy + FFXI/WoW-slot item shop. See NORTHSTAR.md
 * §19 for the design, docs/FFXI_ITEM_PARITY_SEED.md for the real item
 * names. Item indices below are fixed positions in ARENA_ITEMS -- 0 is
 * Seedling Charm (WEAPON, 300 Flow, +8 AD/+40 HP), 16 is Battle Gloves
 * (HANDS, 400 Flow, +12 AD), matching arena_game.c's own catalog. */

static void test_shop_buy_deducts_flow_and_equips_item(void) {
    arena_init_teams();
    arena_state.heroes[0].team = 0;
    float sx, sz;
    arena_shop_position(0, &sx, &sz);
    arena_state.heroes[0].x = sx; arena_state.heroes[0].z = sz;
    arena_state.heroes[0].flow = 1000;

    int ok = arena_shop_buy(0, 0); /* Seedling Charm */

    CHECK(ok, "a real purchase within shop range with enough Flow succeeds");
    CHECK(arena_state.heroes[0].flow == 1000 - ARENA_ITEMS[0].cost, "the item's cost is deducted from spendable Flow");
    CHECK(arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_WEAPON] == 0, "the item is auto-equipped into its own slot");
}

static void test_shop_buy_fails_outside_shop_radius(void) {
    arena_init_teams();
    arena_state.heroes[0].team = 0;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f; /* nowhere near either shop */
    arena_state.heroes[0].flow = 1000;

    int ok = arena_shop_buy(0, 0);

    CHECK(!ok, "buying far from any shop fails");
    CHECK(arena_state.heroes[0].flow == 1000, "a failed purchase spends no Flow");
    CHECK(arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_WEAPON] == -1, "a failed purchase equips nothing");
}

static void test_shop_buy_fails_insufficient_flow(void) {
    arena_init_teams();
    arena_state.heroes[0].team = 0;
    float sx, sz;
    arena_shop_position(0, &sx, &sz);
    arena_state.heroes[0].x = sx; arena_state.heroes[0].z = sz;
    arena_state.heroes[0].flow = 10; /* nowhere near ARENA_ITEMS[0]'s cost */

    int ok = arena_shop_buy(0, 0);

    CHECK(!ok, "buying without enough Flow fails");
    CHECK(arena_state.heroes[0].flow == 10, "a failed purchase spends no Flow");
}

static void test_shop_buy_auto_sells_occupied_slot(void) {
    arena_init_teams();
    arena_state.heroes[0].team = 0;
    float sx, sz;
    arena_shop_position(0, &sx, &sz);
    arena_state.heroes[0].x = sx; arena_state.heroes[0].z = sz;
    arena_state.heroes[0].flow = 5000;
    arena_shop_buy(0, 0);   /* Seedling Charm into WEAPON */
    int flow_after_first_buy = arena_state.heroes[0].flow;

    int ok = arena_shop_buy(0, 1); /* Bramble Fang, also WEAPON -- should replace, not stack */

    CHECK(ok, "buying into an already-occupied slot succeeds");
    CHECK(arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_WEAPON] == 1, "the new item replaces the old one in that slot");
    int expected_refund = (ARENA_ITEMS[0].cost * ARENA_ITEM_SELL_REFUND_PCT) / 100;
    CHECK(arena_state.heroes[0].flow == flow_after_first_buy + expected_refund - ARENA_ITEMS[1].cost,
          "the old item is auto-sold at the same refund rate an explicit sell would give, netting the price difference");
}

static void test_shop_sell_refunds_partial_flow_and_clears_slot(void) {
    arena_init_teams();
    arena_state.heroes[0].team = 0;
    float sx, sz;
    arena_shop_position(0, &sx, &sz);
    arena_state.heroes[0].x = sx; arena_state.heroes[0].z = sz;
    arena_state.heroes[0].flow = 1000;
    arena_shop_buy(0, 0);
    int flow_after_buy = arena_state.heroes[0].flow;

    int ok = arena_shop_sell(0, ARENA_ITEM_SLOT_WEAPON);

    CHECK(ok, "selling an equipped item in range succeeds");
    CHECK(arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_WEAPON] == -1, "the slot is empty after selling -- no bag to move it into");
    int expected_refund = (ARENA_ITEMS[0].cost * ARENA_ITEM_SELL_REFUND_PCT) / 100;
    CHECK(arena_state.heroes[0].flow == flow_after_buy + expected_refund, "selling refunds less than the original purchase price");
    CHECK(expected_refund < ARENA_ITEMS[0].cost, "sanity: the refund really is a loss, not a full refund");
}

static void test_shop_sell_fails_on_empty_slot(void) {
    arena_init_teams();
    arena_state.heroes[0].team = 0;
    float sx, sz;
    arena_shop_position(0, &sx, &sz);
    arena_state.heroes[0].x = sx; arena_state.heroes[0].z = sz;
    arena_state.heroes[0].flow = 500;

    int ok = arena_shop_sell(0, ARENA_ITEM_SLOT_HEAD);

    CHECK(!ok, "selling an empty slot fails -- nothing there to sell");
    CHECK(arena_state.heroes[0].flow == 500, "a failed sell changes nothing");
}

/* S170-205, founder: "add blink dagger 1400 flow it gives a new keybind on screen for tilda" ->
 * "+6ap +6hp". The one item in the catalog with a real active ability, not just stats -- these
 * tests exercise arena_use_blink directly (equipped_item is set by hand, the same "skip the
 * shop, exercise the mechanic" convention every other ability test in this file already uses
 * rather than routing through arena_shop_buy first). */

static void test_blink_noop_without_dagger_equipped(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].target_x = 20.0f; arena_state.heroes[0].target_z = 0;
    arena_state.heroes[0].moving = 1;

    arena_use_blink(0);

    CHECK(arena_state.heroes[0].x == 0, "no Blink Dagger equipped -- no dash happens at all");
    CHECK(arena_state.heroes[0].blink_cooldown_ms == 0, "no cooldown spent on a no-op");
}

static void test_blink_dashes_toward_move_target(void) {
    arena_init_teams();
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_TRINKET] = ARENA_BLINK_DAGGER_ITEM_ID;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].target_x = 100.0f; arena_state.heroes[0].target_z = 0; /* well beyond ARENA_BLINK_RANGE */
    arena_state.heroes[0].moving = 1;

    arena_use_blink(0);

    CHECK(fabsf(arena_state.heroes[0].x - ARENA_BLINK_RANGE) < 0.01f,
          "blinks exactly ARENA_BLINK_RANGE toward a move target well beyond that range, not the whole remaining distance");
    CHECK(arena_state.heroes[0].z == 0, "no lateral drift -- straight line toward the target");
    CHECK(arena_state.heroes[0].blink_cooldown_ms == ARENA_BLINK_COOLDOWN_MS, "cooldown is spent on a real blink");
}

static void test_blink_toward_close_move_target_does_not_overshoot(void) {
    arena_init_teams();
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_TRINKET] = ARENA_BLINK_DAGGER_ITEM_ID;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].target_x = 3.0f; arena_state.heroes[0].target_z = 0; /* well within ARENA_BLINK_RANGE */
    arena_state.heroes[0].moving = 1;

    arena_use_blink(0);

    CHECK(fabsf(arena_state.heroes[0].x - 3.0f) < 0.01f, "a close move target is reached exactly, not overshot past it");
}

static void test_blink_toward_nearest_foe_when_not_moving(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_TRINKET] = ARENA_BLINK_DAGGER_ITEM_ID;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].moving = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 5.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_use_blink(0);

    CHECK(fabsf(arena_state.heroes[0].x - 5.0f) < 0.01f, "not moving -- blinks toward the nearest living enemy instead, same fallback unicorn_cast_q already uses");
}

static void test_blink_respects_its_own_cooldown(void) {
    arena_init_teams();
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_TRINKET] = ARENA_BLINK_DAGGER_ITEM_ID;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].target_x = 100.0f; arena_state.heroes[0].target_z = 0;
    arena_state.heroes[0].moving = 1;

    arena_use_blink(0);
    float x_after_first = arena_state.heroes[0].x;
    arena_state.heroes[0].moving = 1; /* still "trying" to move further */
    arena_use_blink(0);

    CHECK(arena_state.heroes[0].x == x_after_first, "a second blink attempt while still on cooldown does nothing");
}

static void test_blink_blocked_by_stun(void) {
    arena_init_teams();
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_TRINKET] = ARENA_BLINK_DAGGER_ITEM_ID;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].target_x = 100.0f; arena_state.heroes[0].target_z = 0;
    arena_state.heroes[0].moving = 1;
    arena_state.heroes[0].stunned_ms = 500;

    arena_use_blink(0);

    CHECK(arena_state.heroes[0].x == 0, "a stunned hero can't blink -- stun blocks all action, same as every other kit ability");
}

static void test_blink_not_blocked_by_silence(void) {
    arena_init_teams();
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_TRINKET] = ARENA_BLINK_DAGGER_ITEM_ID;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].target_x = 100.0f; arena_state.heroes[0].target_z = 0;
    arena_state.heroes[0].moving = 1;
    arena_state.heroes[0].silenced_ms = 500;

    arena_use_blink(0);

    CHECK(fabsf(arena_state.heroes[0].x - ARENA_BLINK_RANGE) < 0.01f,
          "a silenced hero can still blink -- using an item isn't a cast, only stun blocks it");
}

static void test_blink_dagger_catalog_entry_costs_1400(void) {
    CHECK(ARENA_ITEMS[ARENA_BLINK_DAGGER_ITEM_ID].cost == 1400, "Blink Dagger costs the documented 1400 Flow");
    CHECK(ARENA_ITEMS[ARENA_BLINK_DAGGER_ITEM_ID].bonus_ad == 6, "Blink Dagger grants +6 AD");
    CHECK(ARENA_ITEMS[ARENA_BLINK_DAGGER_ITEM_ID].bonus_max_hp == 6, "Blink Dagger grants +6 HP");
}

/* S170-206, NORTHSTAR §16, founder: "add the weatherman and donkey" -> [clarified: no
 * non-piloted-unit system needed] "donkey should be an item" -> "3.2k flow" -> "tilda should
 * make the hero do the paper airplane glide thing" -> "longish range high speed escape can move
 * above obstacles" -> "long ish cooldown" -> "2 minute cooldown on paper plane fly mode" -> "but
 * the thing where it unfolds and fights for you thats a passive". Donkey shipped as an item
 * (sidestepping §16.1's whole non-piloted-unit blocker), not a hero -- these tests exercise both
 * of its independent effects directly. */

static void test_donkey_fold_triggers_below_hp_threshold(void) {
    arena_init_teams();
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_BACK] = ARENA_DONKEY_ITEM_ID;
    arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].hp = 20; /* below ARENA_DONKEY_FOLD_HP_FRACTION (0.25) */

    arena_update_teams(16);

    CHECK(arena_state.heroes[0].donkey_fold_ms > 0, "Immortal's Fold triggers automatically once HP crosses below the threshold");
    CHECK(arena_state.heroes[0].survive_floor_ms > 0, "grants the simplified damage-floor shield");
    CHECK(arena_state.heroes[0].donkey_fold_proc_cooldown_ms > 0, "the proc's own cooldown starts immediately");
}

static void test_donkey_fold_does_not_trigger_without_item(void) {
    arena_init_teams();
    arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].hp = 20;

    arena_update_teams(16);

    CHECK(arena_state.heroes[0].donkey_fold_ms == 0, "no Donkey equipped -- Immortal's Fold never triggers regardless of HP");
}

static void test_donkey_fold_respects_proc_cooldown(void) {
    arena_init_teams();
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_BACK] = ARENA_DONKEY_ITEM_ID;
    arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].hp = 20;
    arena_update_teams(16);
    CHECK(arena_state.heroes[0].donkey_fold_ms > 0, "sanity: first proc fired");

    /* Let the fold window itself fully expire, then drop below the threshold again -- the proc's
       own separate cooldown should still be blocking a second trigger. */
    arena_update_teams((unsigned int)ARENA_DONKEY_FOLD_MS);
    CHECK(arena_state.heroes[0].donkey_fold_ms == 0, "sanity: the fold window itself has expired");
    arena_state.heroes[0].hp = 20; /* still/again below threshold */
    arena_update_teams(16);

    CHECK(arena_state.heroes[0].donkey_fold_ms == 0, "a second proc attempt while still on its own cooldown does nothing");
}

static void test_donkey_fold_fights_back_damages_nearest_enemy(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* 0 base armor -- clean HP math */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 1000; arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 1000;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* within ARENA_DONKEY_FOLD_FIGHT_RADIUS */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_BACK] = ARENA_DONKEY_ITEM_ID;
    arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].hp = 20;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_update_teams(16);
    CHECK(arena_state.heroes[0].donkey_fold_ms > 0, "sanity: fold triggered");
    arena_update_teams(1000); /* one full fight-back DPS tick */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before,
          "\"it unfolds and fights for you\" -- the unfolded window deals real periodic damage to the nearest enemy, not just a passive shield");
}

static void test_donkey_glide_noop_without_item(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_use_donkey_glide(0);

    CHECK(!arena_state.heroes[0].moving, "no Donkey equipped -- no glide happens at all");
    CHECK(arena_state.heroes[0].donkey_glide_cooldown_ms == 0, "no cooldown spent on a no-op");
}

static void test_donkey_glide_moves_away_from_nearest_enemy(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_BACK] = ARENA_DONKEY_ITEM_ID;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].moving = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 5.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* foe to the east */

    arena_use_donkey_glide(0);

    CHECK(arena_state.heroes[0].moving, "glide sets a real move target and starts moving toward it");
    CHECK(arena_state.heroes[0].target_x < 0.0f, "the destination is AWAY from the foe (west), a real escape, not toward it");
    /* 2026-07-30, founder: "donkey glide needs to be 6 times as far" -- ARENA_DONKEY_GLIDE_RANGE
       (96.0, post-6x) now exceeds ARENA_HALF_EXTENT (~51.78), so a glide starting from map
       center (as this test's hero does) hits arena_set_move_target's own map-boundary clamp
       before ever reaching the full nominal range -- a real, honest consequence of the range
       now being larger than half the map, not a bug. Asserts the clamped value instead of the
       raw range, same as this test would need to for ANY move target past the map edge. */
    CHECK(fabsf(arena_state.heroes[0].target_x - (-ARENA_HALF_EXTENT)) < 0.01f,
          "glides toward the full range, clamped to the map boundary since 6x range now exceeds ARENA_HALF_EXTENT");
    CHECK(arena_state.heroes[0].donkey_airborne_ms == ARENA_DONKEY_GLIDE_DURATION_MS, "airborne window starts at its full duration");
    CHECK(arena_state.heroes[0].intangible_ms == ARENA_DONKEY_GLIDE_DURATION_MS, "untargetable for the same window");
    CHECK(arena_state.heroes[0].donkey_glide_cooldown_ms == ARENA_DONKEY_GLIDE_COOLDOWN_MS, "cooldown is spent on a real glide");
}

static void test_donkey_glide_respects_cooldown(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_BACK] = ARENA_DONKEY_ITEM_ID;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 5.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_use_donkey_glide(0);
    float target_after_first = arena_state.heroes[0].target_x;
    arena_state.heroes[0].moving = 0; /* pretend we already arrived, to isolate the cooldown check */
    arena_use_donkey_glide(0);

    CHECK(arena_state.heroes[0].target_x == target_after_first, "a second glide attempt while still on cooldown does nothing");
}

static void test_donkey_glide_grants_high_speed(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].target_x = 50.0f; arena_state.heroes[0].target_z = 0;
    arena_state.heroes[0].moving = 1;
    arena_state.heroes[0].donkey_airborne_ms = ARENA_DONKEY_GLIDE_DURATION_MS;

    arena_update_teams(100); /* 0.1s */

    float expected = (ARENA_HERO_SPEED) * ARENA_DONKEY_GLIDE_SPEED_MULT * 0.1f;
    CHECK(fabsf(arena_state.heroes[0].x - expected) < 0.05f,
          "movement while airborne uses ARENA_DONKEY_GLIDE_SPEED_MULT, a real high-speed traversal, not base move speed");
}

static void test_donkey_glide_flies_over_obstacles(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].target_x = 2.0f; arena_state.heroes[0].target_z = 0;
    arena_state.heroes[0].moving = 1;
    arena_state.heroes[0].donkey_airborne_ms = ARENA_DONKEY_GLIDE_DURATION_MS;
    /* A real obstacle directly on the flight path, big enough that any grounded hero moving
       through it would get pushed back out by resolve_hero_obstacle_collision. */
    arena_state.obstacles[0].x = 1.0f; arena_state.obstacles[0].z = 0; arena_state.obstacles[0].radius = 2.0f;

    arena_update_teams(60); /* enough time, at the boosted speed, to have crossed well into the obstacle */

    CHECK(arena_state.heroes[0].x > arena_state.obstacles[0].x - arena_state.obstacles[0].radius,
          "while airborne, the hero's real position moved past where a grounded collision push-out would have stopped it -- it flew over the obstacle instead");
}

/* Body blocking (S202-27, 2026-08-26). Founder real-time: "we need to add body blocking" ->
 * "currently players can ghost through eachother". Same real, honest analog as the obstacle-
 * collision tests just above -- a hero is now also a circle every OTHER hero pushes itself back
 * out of, via resolve_hero_hero_collision (arena_game.c, static, exercised here only through
 * the real top-level arena_update_teams tick, never called directly). */
static void test_body_blocking_pushes_a_hero_back_out_of_another(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1; /* re-activate the one enemy slot this test needs */
    ArenaHero *mover = &arena_state.heroes[0];
    ArenaHero *blocker = &arena_state.heroes[ARENA_TEAM_SIZE];
    blocker->x = 1.0f; blocker->z = 0.0f; /* stationary, directly in mover's path */
    mover->x = 0.0f; mover->z = 0.0f;
    mover->target_x = 5.0f; mover->target_z = 0.0f;
    mover->moving = 1;

    /* Enough real ticks, at real move speed, to have walked well past the blocker if nothing
       stopped it -- a real multi-tick simulation, not one big dt_ms step, so the collision
       push-out has to hold up tick after tick, not just once. */
    for (int i = 0; i < 200; i++) arena_update_teams(16);

    float dx = mover->x - blocker->x, dz = mover->z - blocker->z;
    float dist = sqrtf(dx * dx + dz * dz);
    CHECK(dist >= ARENA_HERO_COLLISION_RADIUS * 2.0f - 0.01f,
          "a hero moving toward a target beyond another hero is stopped at the real collision boundary, not walking through it");
    CHECK(mover->x < blocker->x + 0.5f,
          "the mover never actually crosses past the blocker's own position -- genuinely blocked, not just slowed");
}

static void test_body_blocking_applies_to_allies_too(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaHero *mover = &arena_state.heroes[0];
    ArenaHero *ally_blocker = &arena_state.heroes[1]; /* same team as mover */
    ally_blocker->x = 1.0f; ally_blocker->z = 0.0f;
    mover->x = 0.0f; mover->z = 0.0f;
    mover->target_x = 5.0f; mover->target_z = 0.0f;
    mover->moving = 1;

    for (int i = 0; i < 200; i++) arena_update_teams(16);

    float dx = mover->x - ally_blocker->x, dz = mover->z - ally_blocker->z;
    float dist = sqrtf(dx * dx + dz * dz);
    CHECK(dist >= ARENA_HERO_COLLISION_RADIUS * 2.0f - 0.01f,
          "real MOBA body-blocking applies to allies too, not just enemies -- an ally standing in the way still blocks");
}

static void test_body_blocking_does_not_apply_to_dead_heroes(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1; /* re-activate the one enemy slot this test needs */
    ArenaHero *mover = &arena_state.heroes[0];
    ArenaHero *corpse = &arena_state.heroes[ARENA_TEAM_SIZE];
    corpse->x = 1.0f; corpse->z = 0.0f;
    corpse->alive = 0; /* dead -- should not block */
    mover->x = 0.0f; mover->z = 0.0f;
    mover->target_x = 5.0f; mover->target_z = 0.0f;
    mover->moving = 1;

    arena_update_teams(2000); /* 2s, plenty to cross a dead hero's old position */

    CHECK(mover->x > corpse->x + 0.5f, "a dead hero's old position doesn't body-block -- the mover walks straight through it");
}

static void test_body_blocking_skips_donkey_glide_same_as_obstacles(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1; /* re-activate the one enemy slot this test needs */
    ArenaHero *mover = &arena_state.heroes[0];
    ArenaHero *blocker = &arena_state.heroes[ARENA_TEAM_SIZE];
    blocker->x = 1.0f; blocker->z = 0.0f;
    mover->x = 0.0f; mover->z = 0.0f;
    mover->target_x = 2.0f; mover->target_z = 0.0f;
    mover->moving = 1;
    mover->donkey_airborne_ms = ARENA_DONKEY_GLIDE_DURATION_MS; /* flying -- same carve-out as obstacle collision */

    arena_update_teams(60);

    CHECK(mover->x > blocker->x - 0.5f,
          "Paper Glide flies over other heroes too, same as it flies over terrain obstacles -- collision is skipped while airborne");
}

static void test_donkey_catalog_entry_costs_3200(void) {
    CHECK(ARENA_ITEMS[ARENA_DONKEY_ITEM_ID].cost == 3200, "Donkey costs the documented 3200 Flow");
    CHECK(ARENA_ITEMS[ARENA_DONKEY_ITEM_ID].slot == ARENA_ITEM_SLOT_BACK, "Donkey occupies the Back slot");
}

/* S170-207, founder: "add a haste trinket" -> "passive haste lowers cd and auto attack cd make
 * it a modest improvement 6%". Unlike Blink Dagger/Donkey, Haste Trinket is pure passive stats
 * (no named ARENA_..._ITEM_ID constant, since nothing needs to check "is THIS SPECIFIC item
 * equipped" -- arena_recompute_item_stats already sums bonus_cdr_pct generically for any item).
 * find_haste_trinket_item_id searches by name rather than hardcoding an index (the catalog's
 * own item count/order can and does keep changing as new items ship). */
static int find_haste_trinket_item_id(void) {
    for (int i = 0; i < ARENA_ITEM_COUNT; i++) {
        if (strcmp(ARENA_ITEMS[i].name, "Haste Trinket") == 0) return i;
    }
    return -1;
}

static void test_haste_trinket_catalog_entry(void) {
    int id = find_haste_trinket_item_id();
    CHECK(id >= 0, "Haste Trinket exists in the catalog");
    CHECK(ARENA_ITEMS[id].cost == 900, "Haste Trinket costs the documented judgment-call price");
    CHECK(ARENA_ITEMS[id].bonus_cdr_pct == 6, "grants the documented modest 6% CDR");
    CHECK(ARENA_ITEMS[id].bonus_ad == 0 && ARENA_ITEMS[id].bonus_max_hp == 0,
          "no flat stat bonuses -- pure CDR, matching the founder's own \"modest improvement\" framing");
}

static void test_haste_trinket_reduces_ability_cooldown(void) {
    arena_init_teams();
    int id = find_haste_trinket_item_id();
    arena_state.heroes[0].hero_id = ARENA_HERO_UNICORN;
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_TRINKET] = id;
    arena_recompute_item_stats(&arena_state.heroes[0]);

    arena_cast_q(0); /* Unicorn's own Q always lands -- no target/range gate to satisfy */

    int expected = ARENA_UNICORN_Q_COOLDOWN_MS - (ARENA_UNICORN_Q_COOLDOWN_MS * 6) / 100;
    CHECK(arena_state.heroes[0].q_cooldown_ms == expected, "a real 6% reduction applies to an ability cooldown");
}

static void test_haste_trinket_reduces_auto_attack_cooldown(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    int id = find_haste_trinket_item_id();
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_TRINKET] = id;
    arena_recompute_item_stats(&arena_state.heroes[0]);
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;

    arena_update_teams(16); /* begins windup */
    arena_update_teams((unsigned int)ARENA_ATTACK_WINDUP_MS); /* completes it, sets attack_cooldown_ms */

    int expected = ARENA_ATTACK_COOLDOWN_MS - (ARENA_ATTACK_COOLDOWN_MS * 6) / 100;
    CHECK(arena_state.heroes[0].attack_cooldown_ms == expected, "a real 6% reduction applies to the auto-attack cooldown too");
}

static void test_haste_trinket_does_not_shrink_windup(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    int id = find_haste_trinket_item_id();
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_TRINKET] = id;
    arena_recompute_item_stats(&arena_state.heroes[0]);
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;

    arena_update_teams(16);

    /* Windup begins AND ticks down by this same 16ms within the one combined call (the melee
       loop that starts it runs before arena_tick_attack_windups, which decrements it, both
       inside arena_update_teams) -- so after one tick it's ARENA_ATTACK_WINDUP_MS minus that
       16ms, not the full untouched value. The real claim under test is narrower: that value is
       NOT also reduced by the item's own 6% CDR on top of the normal per-tick decrement. */
    CHECK(arena_state.heroes[0].attack_windup_ms_remaining == ARENA_ATTACK_WINDUP_MS - 16,
          "NORTHSTAR SS17.1: real League's own windup fraction does not shrink as attack speed increases -- only the cooldown/backswing compresses");
}

/* "Expand the play space" first pass (2026-08-11) -- Gae Bolg/Masamune/Muramasa/Balance Ring/
 * Empress Hairpin/Ninja Tekko. find_item_id_by_name mirrors find_haste_trinket_item_id's own
 * "search by name, don't hardcode an index" reasoning. */
static int find_item_id_by_name(const char *name) {
    for (int i = 0; i < ARENA_ITEM_COUNT; i++) {
        if (strcmp(ARENA_ITEMS[i].name, name) == 0) return i;
    }
    return -1;
}

static void test_item_catalog_reaches_shop_page_4(void) {
    /* 2026-08-26: was == 33 -- Kite String (S202-34) pushed the catalog to 34. Checking >= 34
       (not == 34) so the next item added doesn't require touching this assertion again -- the
       real thing this test protects (page 4 still exists, SHOP_PAGE_COUNT still derives cleanly
       from ARENA_ITEM_COUNT alone) holds for any count at or above the current one. */
    CHECK(ARENA_ITEM_COUNT >= 34, "Kite String landed, pushing the catalog from 33 to 34+ -- apps/arena's own SHOP_PAGE_COUNT (ceil(ARENA_ITEM_COUNT/9)) derives page 4 from this alone, no separate paging code needed");
}

static void test_luck_of_the_draw_boosts_in_combat_mp_regen_only(void) {
    /* S205-87: "Luck of the Draw" -- +1 flat mp/sec on top of ARENA_MP_REGEN_IN_COMBAT_PER_SEC,
       in combat only. Real, deliberate contrast with test_gae_bolg/test_masamune above: this
       verifies a NON-combat mechanic (a passive regen rate, not a damage/heal event on a landed
       attack), so the test drives arena_update directly rather than waiting out a real attack
       windup. */
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    int id = find_item_id_by_name("Luck of the Draw");
    CHECK(id >= 0, "Luck of the Draw exists in the catalog");
    h->equipped_item[ARENA_ITEM_SLOT_TRINKET] = id;
    arena_recompute_item_stats(h);
    CHECK(h->item_bonus_mp_regen_combat == 1, "the item's own +1 bonus is reflected in the recomputed cache");

    h->mp = 0;
    h->max_mp = 100;
    h->combat_timer_ms = 5000; /* in combat */
    arena_update(1000); /* exactly one real second */
    CHECK(h->mp == ARENA_MP_REGEN_IN_COMBAT_PER_SEC + 1,
          "in combat, equipping Luck of the Draw regens the base trickle PLUS its own +1, not just +1 alone or the base alone");

    /* Real, deliberate scope check matching this item's own founder quote ("mana regen during
       combat"): the bonus must NOT leak into the out-of-combat rate, unlike bonus_cdr_pct's own
       deliberately wider (both cooldown types) scope. */
    h->mp = 0;
    h->combat_timer_ms = 0; /* out of combat */
    arena_update(1000);
    CHECK(h->mp == ARENA_MP_REGEN_PER_SEC,
          "out of combat, the bonus does not apply -- only the normal out-of-combat rate, exactly matching the founder's own 'during combat' scope");
}

static void test_gae_bolg_true_damage_bypasses_armor(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    int id = find_item_id_by_name("Gae Bolg");
    CHECK(id >= 0, "Gae Bolg exists in the catalog");
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_WEAPON] = id;
    arena_recompute_item_stats(&arena_state.heroes[0]);
    /* A huge armor value that would normally floor real damage down to apply_armor's 1-hp
       minimum -- proves true damage is added AFTER that floor, not folded into the armor-reduced
       total, since a floored 1 + true_dmg is trivially distinguishable from a real armor
       calculation gone differently wrong. */
    arena_state.heroes[ARENA_TEAM_SIZE].item_bonus_armor = 500;
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_update_teams(16);
    arena_update_teams((unsigned int)ARENA_ATTACK_WINDUP_MS);

    int expected_dmg = 1 /* apply_armor's floor against 500 armor */ + ARENA_ITEMS[id].bonus_true_dmg;
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before - expected_dmg,
          "Gae Bolg's true damage lands in full even against overwhelming armor -- the armor-reduced floor plus the true damage on top, not swallowed by the floor");
}

static void test_masamune_lifesteal_heals_attacker(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    /* The Duck has no passive armor of its own, unlike the Unicorn placeholder hero_id
       arena_init_teams leaves every hero at by default (arena_hero_base_armor's own doc
       comment) -- without this, the foe's own real 4.0 passive armor would silently reduce
       final_dmg below this test's own expected-damage math, which (correctly, matching
       production behavior) doesn't have any armor to subtract against a target it doesn't know
       will secretly have some. */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    /* Real combat is mutual -- both heroes are in range of each other and both start with
       attack_cooldown_ms == 0, so without this the "foe" would ALSO windup and land a counter-
       attack on hero 0 within these same two ticks, muddying hero 0's own hp delta with damage
       taken as well as lifesteal gained. Parking its cooldown past this test's own tick budget
       (16ms + ARENA_ATTACK_WINDUP_MS, well under a full ARENA_ATTACK_COOLDOWN_MS) isolates the
       one-directional attack this test actually wants to measure. */
    arena_state.heroes[ARENA_TEAM_SIZE].attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    int id = find_item_id_by_name("Masamune");
    CHECK(id >= 0, "Masamune exists in the catalog");
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_WEAPON] = id;
    arena_recompute_item_stats(&arena_state.heroes[0]);
    arena_state.heroes[0].hp = 50; /* well under max_hp so the heal has headroom, not clamped away */
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;

    arena_update_teams(16);
    arena_update_teams((unsigned int)ARENA_ATTACK_WINDUP_MS);

    /* item_bonus_armor defaults to 0 (arena_init_teams' memset) and apply_armor is `raw - armor`
       floored at 1 -- with 0 armor the floor never engages, so the final damage is simply
       ARENA_ATTACK_DAMAGE + Masamune's own bonus_ad, inlined here rather than calling
       apply_armor directly (it's `static` inside arena_game.c, not visible to this test TU). */
    int final_dmg = ARENA_ATTACK_DAMAGE + ARENA_ITEMS[id].bonus_ad;
    int expected_heal = (final_dmg * ARENA_ITEMS[id].bonus_lifesteal_pct) / 100;
    CHECK(arena_state.heroes[0].hp == 50 + expected_heal, "Masamune heals the attacker for its documented percent of the final damage dealt");
}

static void test_muramasa_extreme_glass_cannon_catalog_entry(void) {
    int id = find_item_id_by_name("Muramasa");
    CHECK(id >= 0, "Muramasa exists in the catalog");
    CHECK(ARENA_ITEMS[id].bonus_ad > ARENA_ITEMS[find_item_id_by_name("Kraken Club")].bonus_ad,
          "Muramasa is a more extreme glass cannon than Kraken Club, its own real-lore \"benevolent vs cursed\" counterpart Masamune's opposite number");
    CHECK(ARENA_ITEMS[id].bonus_max_hp == 0 && ARENA_ITEMS[id].bonus_armor == 0 && ARENA_ITEMS[id].bonus_max_mp == 0,
          "genuinely zero of every defensive stat -- the most extreme risk/reward weapon in the catalog");
}

static void test_balance_ring_armor_scales_with_missing_hp(void) {
    arena_init_teams();
    int id = find_item_id_by_name("Balance Ring");
    CHECK(id >= 0, "Balance Ring exists in the catalog");
    /* The Duck has no passive armor of its own (arena_hero_base_armor's own doc comment) --
       Unicorn, the default placeholder hero_id arena_init_teams leaves every hero at, has a real
       nonzero passive armor, which would make the "no bonus at full HP" absolute-zero check
       below fail for a reason unrelated to Balance Ring itself. */
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_RING] = id;
    arena_state.heroes[0].max_hp = 100;

    arena_state.heroes[0].hp = 100; /* full HP */
    float armor_at_full_hp = arena_hero_armor(&arena_state.heroes[0]);

    arena_state.heroes[0].hp = 10; /* critically low */
    float armor_at_low_hp = arena_hero_armor(&arena_state.heroes[0]);

    CHECK(armor_at_full_hp == 0.0f, "no comeback bonus at full HP");
    CHECK(armor_at_low_hp > armor_at_full_hp + 30.0f, "a real, large comeback bonus once critically low -- computed live off current HP, not a flat purchase-time stat");
}

static void test_new_items_catalog_entries(void) {
    int hairpin = find_item_id_by_name("Empress Hairpin");
    int tekko = find_item_id_by_name("Ninja Tekko");
    CHECK(hairpin >= 0 && ARENA_ITEMS[hairpin].bonus_max_mp == 100, "Empress Hairpin grants the documented mana bonus");
    CHECK(tekko >= 0 && ARENA_ITEMS[tekko].bonus_ad > 0 && ARENA_ITEMS[tekko].bonus_move_speed > 0.0f,
          "Ninja Tekko is a real AD+move-speed hybrid, not a straight upgrade to the existing Hands item");
}

static void test_weatherman_q_knocks_back_no_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_WEATHERMAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].x > 3.0f, "Barometric Shove pushes the target further away from Weatherman");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "displacement-only -- no damage component at all, unlike every other roster Q");
}

static void test_weatherman_q_out_of_range_whiffs(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_WEATHERMAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_WEATHERMAN_Q_RANGE + 5.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].x == ARENA_WEATHERMAN_Q_RANGE + 5.0f, "a foe out of range is untouched");
    CHECK(arena_state.heroes[0].q_cooldown_ms == 0, "an out-of-range whiff doesn't consume the cooldown");
}

static void test_weatherman_w_grounds_airborne_enemy(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_WEATHERMAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].donkey_airborne_ms = 400;
    arena_state.heroes[ARENA_TEAM_SIZE].intangible_ms = 400;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].donkey_airborne_ms == 0, "\"the debt catches up to you\" -- an airborne enemy is grounded immediately");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].intangible_ms == 0, "grounding also ends the untargetable glide window");
    CHECK(arena_state.heroes[0].w_cooldown_ms == ARENA_WEATHERMAN_W_COOLDOWN_MS, "a real landed cast spends the cooldown");
}

static void test_weatherman_w_extends_airborne_ally(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_WEATHERMAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 3.0f; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].donkey_airborne_ms = 400;
    arena_state.heroes[1].intangible_ms = 400;
    arena_state.heroes[1].moving = 0; /* already arrived -- isolates the duration-only half of the extension */

    arena_toggle_w(0);

    CHECK(arena_state.heroes[1].donkey_airborne_ms == 400 + ARENA_DONKEY_GLIDE_DURATION_MS,
          "a tailwind, not a headwind -- an ally's glide is EXTENDED, not ended");
    CHECK(arena_state.heroes[1].intangible_ms == 400 + ARENA_DONKEY_GLIDE_DURATION_MS, "untargetable window extends to match");
}

static void test_weatherman_w_noop_when_nobody_airborne(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_WEATHERMAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    /* nobody is airborne -- the overwhelmingly common case */

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].w_cooldown_ms == 0, "whiffs (no cooldown consumed) when nobody nearby is currently mid-glide, same convention every other conditional W on this roster follows");
}

static void test_weatherman_r_zone_damages_over_time(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 1000; arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 1000;
    arena_state.heroes[0].hero_id = ARENA_HERO_WEATHERMAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_r(0);
    CHECK(arena_state.heroes[0].r_active_ms == ARENA_WEATHERMAN_R_DURATION_MS, "R starts a real fixed-duration zone");
    arena_update_teams(1000); /* one full DPS tick */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before, "The Debt Compounds -- the zone deals real periodic damage to enemies standing in it");
}

static void test_zagan_passive_grants_flow_when_enemy_crosses_half_hp(void) {
    /* Team mode + arena_update_teams, not arena_init_with_heroes + arena_update -- the latter
       pair's bot_cast_kit_if_ready would fire for owner 1 every tick and contaminate the exact
       "did the passive fire" signal this test depends on, same reasoning MnM's/Weatherman's own
       timing tests above already give. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_ZAGAN;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp / 2 - 1; /* already below 50% */
    int flow_before = arena_state.heroes[0].flow;

    arena_update_teams(16);

    CHECK(arena_state.heroes[0].flow == flow_before + ARENA_ZAGAN_PASSIVE_CONFESSION_FLOW,
          "Base Metal Screams: Zagan gains Flow the instant ANY enemy crosses below 50% HP, no proximity or damage-source requirement");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].zagan_confessed, "the enemy's own confessed flag is now set");
}

static void test_zagan_passive_does_not_retrigger_the_same_confession(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_ZAGAN;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp / 2 - 1;

    arena_update_teams(16); /* first crossing -- fires once */
    int flow_after_first = arena_state.heroes[0].flow;
    arena_update_teams(16); /* still below 50%, second tick -- should NOT fire again */

    CHECK(arena_state.heroes[0].flow == flow_after_first, "one confession per life -- staying below 50% doesn't keep paying out every tick");
}

static void test_zagan_q_damages_and_shreds_armor(void) {
    arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_ZAGAN); /* Duck: 0 base armor, exact math */
    ArenaHero *zagan = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = zagan->x - 2.0f; /* within ARENA_ZAGAN_Q_RANGE */
    foe->z = zagan->z;
    int foe_hp_before = foe->hp;
    float foe_armor_before = arena_hero_armor(foe);

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Calcination damages the foe when in range");
    CHECK(foe->zagan_calcination_ms == ARENA_ZAGAN_Q_DURATION_MS, "Calcination starts the armor-shred debuff");
    CHECK(arena_hero_armor(foe) == foe_armor_before - (float)ARENA_ZAGAN_Q_ARMOR_SHRED, "the shred actually reduces the foe's effective armor");
    CHECK(zagan->q_cooldown_ms == ARENA_ZAGAN_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_zagan_q_armor_shred_expires_after_duration(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_ZAGAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 2.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    ArenaHero *foe = &arena_state.heroes[ARENA_TEAM_SIZE];
    float base_armor = arena_hero_armor(foe);

    arena_cast_q(0);
    CHECK(arena_hero_armor(foe) == base_armor - (float)ARENA_ZAGAN_Q_ARMOR_SHRED, "shred is active right after the hit");
    arena_update_teams(ARENA_ZAGAN_Q_DURATION_MS + 32); /* past the debuff's own duration */

    CHECK(foe->zagan_calcination_ms == 0, "the debuff's own countdown reaches zero and stays there");
    CHECK(arena_hero_armor(foe) == base_armor, "armor is back to normal once Calcination expires");
}

static void test_zagan_w_stuns_foe_in_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_ZAGAN);
    ArenaHero *zagan = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = zagan->x - 2.0f; /* within ARENA_ZAGAN_W_RANGE */
    foe->z = zagan->z;

    arena_toggle_w(1);

    CHECK(foe->stunned_ms == ARENA_ZAGAN_W_STUN_MS, "The Standstill stuns a foe in range");
    CHECK(zagan->w_cooldown_ms == ARENA_ZAGAN_W_COOLDOWN_MS, "W starts on cooldown after a landed stun");
}

static void test_zagan_w_no_effect_out_of_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_ZAGAN);
    ArenaHero *zagan = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0]; /* default spawn is 12 units away, well outside ARENA_ZAGAN_W_RANGE */

    arena_toggle_w(1);

    CHECK(foe->stunned_ms == 0, "no stun lands on a foe out of range");
    CHECK(zagan->w_cooldown_ms == 0, "a whiffed W spends no cooldown, same convention every other targeted cast in this file uses");
}

static void test_zagan_r_mirrors_target_armor(void) {
    arena_init_with_heroes(ARENA_HERO_MNM, ARENA_HERO_ZAGAN); /* MnM: flat ARENA_MNM_PASSIVE_ARMOR, a clean nonzero comparison value */
    ArenaHero *zagan = &arena_state.heroes[1];
    ArenaHero *target = &arena_state.heroes[0];
    target->x = zagan->x - 2.0f; /* within ARENA_ZAGAN_R_RANGE */
    target->z = zagan->z;
    float zagan_armor_before = arena_hero_armor(zagan);

    CHECK(zagan_armor_before != (float)ARENA_MNM_PASSIVE_ARMOR, "sanity: Zagan's own normal armor differs from MnM's, or this test proves nothing");

    arena_cast_r(1);

    CHECK(zagan->zagan_r_target == target->owner, "Conjunction locks onto the target's owner slot");
    CHECK(zagan->r_active_ms == ARENA_ZAGAN_R_DURATION_MS, "R starts a real fixed duration");
    CHECK(zagan->r_cooldown_ms == ARENA_ZAGAN_R_COOLDOWN_MS, "R starts on its own cooldown");
    CHECK(arena_hero_armor(zagan) == (float)ARENA_MNM_PASSIVE_ARMOR, "Conjunction: Zagan's TOTAL armor becomes exactly the target's, a true mirror");
    CHECK(arena_hero_armor(zagan) == arena_hero_armor(target), "both sides of the mirror read identical, live");
}

static void test_zagan_r_falls_back_when_target_no_longer_hittable(void) {
    arena_init_with_heroes(ARENA_HERO_MNM, ARENA_HERO_ZAGAN);
    ArenaHero *zagan = &arena_state.heroes[1];
    ArenaHero *target = &arena_state.heroes[0];
    target->x = zagan->x - 2.0f;
    target->z = zagan->z;
    float zagan_armor_before = arena_hero_armor(zagan);

    arena_cast_r(1);
    CHECK(arena_hero_armor(zagan) == (float)ARENA_MNM_PASSIVE_ARMOR, "sanity: the mirror is active");
    target->alive = 0; /* target dies mid-duration */

    CHECK(arena_hero_armor(zagan) == zagan_armor_before, "the mirror silently falls back to Zagan's own real armor once the target stops being hittable -- no special-case cleanup needed");
}

static void test_item_stats_apply_to_hp_mp_armor_ad_speed(void) {
    arena_init_teams();
    arena_state.heroes[0].team = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST; /* no hero-specific armor passive to muddy the item-only comparison */
    float sx, sz;
    arena_shop_position(0, &sx, &sz);
    arena_state.heroes[0].x = sx; arena_state.heroes[0].z = sz;
    arena_state.heroes[0].flow = 5000;
    int base_max_hp = arena_state.heroes[0].max_hp;

    arena_shop_buy(0, 0);  /* Seedling Charm: WEAPON, +8 AD, +40 HP */
    arena_shop_buy(0, 15); /* Haubergeon: BODY, +18 Armor */
    arena_shop_buy(0, 18); /* Creek F. Boots: FEET, +0.6 move speed */

    CHECK(arena_state.heroes[0].max_hp == base_max_hp + ARENA_ITEMS[0].bonus_max_hp,
          "equipped items' HP bonus raises max_hp");
    CHECK(arena_state.heroes[0].item_bonus_ad == ARENA_ITEMS[0].bonus_ad, "equipped items' AD bonus is cached for damage call sites to read");
    CHECK(arena_hero_armor(&arena_state.heroes[0]) == (float)ARENA_ITEMS[15].bonus_armor,
          "arena_hero_armor() includes equipped items' armor bonus for a hero with no hero-specific armor passive");
    CHECK(arena_state.heroes[0].item_bonus_move_speed == ARENA_ITEMS[18].bonus_move_speed,
          "equipped items' move-speed bonus is cached for update_hero_motion to read");
}

static void test_node_guardian_kill_grants_flow_and_xp(void) {
    arena_init_teams();
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_creeps(16); /* spawn */
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_hero_attack_creeps(16);

    CHECK(!arena_state.creeps[0].alive, "sanity: the creep actually died");
    CHECK(arena_state.heroes[0].flow == ARENA_NODE_GUARDIAN_KILL_FLOW, "a node-guardian creep kill grants the documented Flow bounty");
    CHECK(arena_state.heroes[0].flow_earned == ARENA_NODE_GUARDIAN_KILL_FLOW, "flow_earned tracks the same amount");
    CHECK(arena_state.heroes[0].xp == ARENA_NODE_GUARDIAN_KILL_XP, "a node-guardian creep kill grants the documented XP");
}

static void test_lane_creep_kill_grants_flow_and_xp(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaLaneCreep *lc = &arena_state.lane_creeps[0];
    lc->active = 1; lc->alive = 1; lc->team = 1; /* enemy wave */
    lc->hp = ARENA_ATTACK_DAMAGE;
    lc->x = arena_state.heroes[0].x; lc->z = arena_state.heroes[0].z;

    arena_hero_attack_lane_creeps(16);

    CHECK(!lc->alive, "sanity: the lane creep actually died");
    CHECK(arena_state.heroes[0].flow == ARENA_LANE_CREEP_KILL_FLOW, "a lane creep kill grants the documented Flow bounty -- previously rewarded nothing at all (S170-139's own flagged gap)");
    CHECK(arena_state.heroes[0].xp == ARENA_LANE_CREEP_KILL_XP, "a lane creep kill grants the documented XP");
}

static void test_lane_creep_kill_shares_xp_with_nearby_allies_but_not_far_ones(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaLaneCreep *lc = &arena_state.lane_creeps[0];
    lc->active = 1; lc->alive = 1; lc->team = 1; /* enemy wave */
    lc->hp = ARENA_ATTACK_DAMAGE;
    lc->x = arena_state.heroes[0].x; lc->z = arena_state.heroes[0].z;

    /* heroes[1]: nearby ally, within ARENA_LANE_CREEP_XP_SHARE_RADIUS (8.0) of the kill but not
       the one who landed it -- should share in the XP. */
    arena_state.heroes[1].x = lc->x + 2.0f;
    arena_state.heroes[1].z = lc->z;

    /* heroes[2]: far ally, well outside the share radius -- should get nothing. */
    arena_state.heroes[2].x = lc->x + (ARENA_LANE_CREEP_XP_SHARE_RADIUS + 5.0f);
    arena_state.heroes[2].z = lc->z;

    arena_hero_attack_lane_creeps(16);

    CHECK(!lc->alive, "sanity: the lane creep actually died");
    CHECK(arena_state.heroes[0].flow == ARENA_LANE_CREEP_KILL_FLOW, "the killer's Flow stays individual/precise, unaffected by XP-share");
    CHECK(arena_state.heroes[0].xp == ARENA_LANE_CREEP_KILL_XP, "the killer still gets the XP");
    CHECK(arena_state.heroes[1].xp == ARENA_LANE_CREEP_KILL_XP, "S170-216: a nearby ally who didn't land the kill still shares in the XP");
    CHECK(arena_state.heroes[2].xp == 0, "an ally outside the XP-share radius gets nothing");
}

static void test_hero_kill_grants_flow_xp_kills_and_deaths(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 1; /* apply_armor floors at 1 dmg, so any landed hit finishes it regardless of the default Unicorn armor */

    for (int i = 0; i < 500 && arena_state.heroes[ARENA_TEAM_SIZE].alive; i++) arena_update_teams(16);

    CHECK(!arena_state.heroes[ARENA_TEAM_SIZE].alive, "sanity: the enemy hero actually died");
    CHECK(arena_state.heroes[0].flow == ARENA_HERO_KILL_FLOW, "a hero kill grants the documented Flow bounty");
    CHECK(arena_state.heroes[0].xp == ARENA_HERO_KILL_XP, "a hero kill grants the documented XP");
    CHECK(arena_state.heroes[0].kills == 1, "the killer's kill count increments");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].deaths == 1, "the victim's death count increments");
    CHECK(arena_state.heroes[0].multikill_count == 1, "a fresh kill starts a streak at count 1");
}

/* Multi-kill streak reward (2026-07-29, founder: "add exponential reward for double tripple
 * penta kills etc" -> "like a double kill should give the reward of 3 kills and then use the
 * fib"). Real kills are driven through actual arena_update_teams ticks, same convention every
 * other kill test in this file already uses -- only the killer's PRE-EXISTING streak state
 * (multikill_count/multikill_timer_ms) is set up directly rather than earned via a first real
 * kill, same "simulate the setup, exercise the real path for what's actually under test"
 * precedent test_assist_expires_outside_the_tracking_window's own doc comment above already
 * uses for timer state. */
static void test_second_kill_within_window_scales_by_fib_and_stacks_to_3x(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 1;

    /* Owner 0 already has one kill 4s into its 10s window -- this next one is streak kill 2,
       "Double Kill" in real-MOBA terms. */
    arena_state.heroes[0].multikill_count = 1;
    arena_state.heroes[0].multikill_timer_ms = ARENA_MULTIKILL_WINDOW_MS - 4000;
    arena_state.heroes[0].flow = ARENA_HERO_KILL_FLOW; /* the first kill's own bounty, already banked */

    for (int i = 0; i < 500 && arena_state.heroes[ARENA_TEAM_SIZE].alive; i++) arena_update_teams(16);

    CHECK(!arena_state.heroes[ARENA_TEAM_SIZE].alive, "sanity: the second kill actually landed");
    CHECK(arena_state.heroes[0].multikill_count == 2, "streak count advances to 2 (Double Kill)");
    CHECK(arena_state.heroes[0].flow == ARENA_HERO_KILL_FLOW * 3,
          "two kills forming a Double Kill total 1x + 2x (fib(1)+fib(2)) = 3x a normal kill's worth -- the founder's own stated example");
}

static void test_multikill_streak_resets_after_window_expires(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 1;

    /* Owner 0 was mid-streak (count 3, as if Triple Kill already landed) but the window has
       fully lapsed since -- same "age the timer out directly" idiom as the assist-expiry test
       above. tick_hero_kit's own decrement path (exercised by every real tick below) must clear
       multikill_count back to 0 once the timer actually hits 0, BEFORE the next kill lands, or
       this next kill would wrongly continue the old streak as count 4 instead of starting a
       fresh one at count 1. */
    arena_state.heroes[0].multikill_count = 3;
    arena_state.heroes[0].multikill_timer_ms = 0;

    for (int i = 0; i < 500 && arena_state.heroes[ARENA_TEAM_SIZE].alive; i++) arena_update_teams(16);

    CHECK(!arena_state.heroes[ARENA_TEAM_SIZE].alive, "sanity: the kill landed");
    CHECK(arena_state.heroes[0].multikill_count == 1, "an expired window starts a brand new streak at 1, not a continuation at 4");
    CHECK(arena_state.heroes[0].flow == ARENA_HERO_KILL_FLOW, "a fresh streak's first kill pays the normal, unscaled bounty");
}

static void test_dying_resets_own_multikill_streak(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 1;

    /* The VICTIM here has its own in-progress streak (as killer of someone else, off-screen)
       going into its own death -- dying should end that streak regardless of who did the
       killing, a real-MOBA convention (see ARENA_MULTIKILL_WINDOW_MS's own doc comment). */
    arena_state.heroes[ARENA_TEAM_SIZE].multikill_count = 4;
    arena_state.heroes[ARENA_TEAM_SIZE].multikill_timer_ms = 5000;

    for (int i = 0; i < 500 && arena_state.heroes[ARENA_TEAM_SIZE].alive; i++) arena_update_teams(16);

    CHECK(!arena_state.heroes[ARENA_TEAM_SIZE].alive, "sanity: the victim died");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].multikill_count == 0, "dying clears the victim's own streak count");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].multikill_timer_ms == 0, "and its own streak timer");
}

/* S170-187, founder: "assists should gen flow" -- anyone who damaged the victim within the
 * recent tracking window shares in a smaller bounty, not just whoever lands the killing blow.
 * Driven entirely through real arena_update_teams ticks (record_assist_damage is static),
 * same "run real ticks until it happens" convention the tests above already use. */
static void test_hero_kill_awards_assist_to_recent_damager(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_GHOST; /* 0 base armor -- keeps the hit-damage math exact (ARENA_ATTACK_DAMAGE per hit) */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = ARENA_ATTACK_DAMAGE * 3; /* needs 3 total hits */
    arena_state.heroes[0].x = arena_state.heroes[1].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[1].z = arena_state.heroes[ARENA_TEAM_SIZE].z;

    /* Both owner 0 and owner 1 are in melee range from tick 1 -- S170-204: a first tick only
       BEGINS both windups now (no more instant damage the moment range/cooldown are satisfied),
       a second tick worth the full windup duration lands both hits together (the victim
       survives owner 0's hit, so owner 1's own attack still finds it as a valid target in the
       same completion pass), leaving 1 hit's worth of HP and both attackers on cooldown. Both
       are now real, ticked-in recent damagers. */
    arena_update_teams(16);
    arena_update_teams((unsigned int)ARENA_ATTACK_WINDUP_MS);
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == ARENA_ATTACK_DAMAGE, "sanity: both landed exactly one hit once their windups completed");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].alive, "sanity: the victim survived that first exchange");

    /* Move owner 1 far away -- it played a real part in the fight but won't land the actual
       kill, the whole point of testing assist credit specifically. */
    arena_state.heroes[1].x += 100.0f;

    for (int i = 0; i < 500 && arena_state.heroes[ARENA_TEAM_SIZE].alive; i++) arena_update_teams(16);

    CHECK(!arena_state.heroes[ARENA_TEAM_SIZE].alive, "sanity: the victim eventually died to owner 0's own follow-up hit");
    CHECK(arena_state.heroes[0].kills == 1, "owner 0 landed the actual kill");
    CHECK(arena_state.heroes[0].flow == ARENA_HERO_KILL_FLOW, "the killer gets the full kill bounty, not an assist-sized one");
    CHECK(arena_state.heroes[1].flow == ARENA_HERO_ASSIST_FLOW, "owner 1 gets the smaller assist bounty for its earlier real hit");
    CHECK(arena_state.heroes[1].xp == ARENA_HERO_ASSIST_XP, "and the matching assist XP");
    CHECK(arena_state.heroes[1].kills == 0, "an assist is not counted as a kill");
}

static void test_assist_expires_outside_the_tracking_window(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = ARENA_ATTACK_DAMAGE * 3;
    arena_state.heroes[0].x = arena_state.heroes[1].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[1].z = arena_state.heroes[ARENA_TEAM_SIZE].z;

    arena_update_teams(16); /* owner 1's one real hit, same setup as above */
    arena_state.heroes[1].x += 100.0f;

    /* Let the assist window fully lapse (ARENA_ASSIST_WINDOW_MS) before owner 0 ever lands
       the finishing blow -- simulated here by directly aging the tracked timer out, since
       waiting out a real 10s of 16ms ticks would be thousands of iterations for no different
       coverage than exercising the same tick_hero_kit decrement path the stun/slow tests
       above already verify works correctly. */
    for (int a = 0; a < ARENA_MAX_ASSIST_TRACK; a++) {
        if (arena_state.heroes[ARENA_TEAM_SIZE].assist_owner[a] == 1) {
            arena_state.heroes[ARENA_TEAM_SIZE].assist_ms[a] = 0;
        }
    }

    for (int i = 0; i < 500 && arena_state.heroes[ARENA_TEAM_SIZE].alive; i++) arena_update_teams(16);

    CHECK(!arena_state.heroes[ARENA_TEAM_SIZE].alive, "sanity: the victim still died to owner 0");
    CHECK(arena_state.heroes[1].flow == 0, "an assist window that's fully expired grants no bounty");
}

static void test_ability_kill_grants_no_flow(void) {
    /* Same "not every damage source needs full reward wiring, flagged not
       faked" precedent arena_zone_damage_creeps' own doc comment already
       sets for AoE-vs-creep kills -- Ghost's R zone finishes the kill
       here, an ability, not the melee/homing-shot loop last_attacked_by_owner
       is only ever set from. */
    arena_init_with_heroes(ARENA_HERO_GHOST, ARENA_HERO_UNICORN);
    /* S170-228: hero 1 (the 1-hp "foe" here) now moves via the trained RL policy instead of
       the old fixed net -- a single 1000ms tick is real time for it to walk out of Ghost's own
       R zone radius before the zone's own damage tick lands, which would mask the exact
       mechanic (an ability finishing a kill) this test exists to check. Disabled the same way
       other tests in this file already handle the same root cause. */
    arena_bot_enabled = 0;
    ArenaHero *ghost = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = ghost->x + 3.0f;
    foe->z = ghost->z;
    foe->hp = 1;

    arena_cast_r(0);
    arena_update(1000); /* one full zone tick, enough to finish a 1-hp foe */

    CHECK(!foe->alive, "sanity: the zone actually finished the kill");
    CHECK(ghost->flow == 0, "a kill finished by an ability grants no Flow this pass -- only melee/homing-shot kills do");
    arena_bot_enabled = 1; /* restore the default for any test run after this one */
}

static void test_flow_earned_does_not_decrease_on_purchase(void) {
    arena_init_teams();
    arena_state.heroes[0].team = 0;
    float sx, sz;
    arena_shop_position(0, &sx, &sz);
    arena_state.heroes[0].x = sx; arena_state.heroes[0].z = sz;
    arena_state.heroes[0].flow = 1000;
    arena_state.heroes[0].flow_earned = 1000;

    arena_shop_buy(0, 0);

    CHECK(arena_state.heroes[0].flow < 1000, "sanity: spendable Flow went down");
    CHECK(arena_state.heroes[0].flow_earned == 1000, "flow_earned never decreases on a purchase -- the character pane's own stat, not the spendable balance");
}

static void test_respawn_preserves_economy_and_equipped_items(void) {
    arena_init_teams();
    arena_state.heroes[0].flow = 777;
    arena_state.heroes[0].flow_earned = 999;
    arena_state.heroes[0].xp = 321;
    arena_state.heroes[0].kills = 3;
    arena_state.heroes[0].deaths = 1;
    float sx, sz;
    arena_shop_position(0, &sx, &sz);
    arena_state.heroes[0].x = sx; arena_state.heroes[0].z = sz;
    arena_shop_buy(0, 0);
    int flow_before_death = arena_state.heroes[0].flow;

    /* Bypasses apply_damage's own death/reward handling on purpose -- that
       path (kills++/deaths++ on a real kill) is already covered by
       test_hero_kill_grants_flow_xp_kills_and_deaths below. This test's
       only job is the respawn-preservation contract, so kills/deaths are
       pre-seeded above and must come through unchanged, not incremented. */
    arena_state.heroes[0].alive = 0;
    for (int i = 0; i < 2000 && !arena_state.heroes[0].alive; i++) arena_update_teams(16); /* run real ticks until the wave respawns it */

    CHECK(arena_state.heroes[0].alive, "sanity: the hero actually respawned");
    CHECK(arena_state.heroes[0].flow == flow_before_death, "Flow survives death -- earned across the whole match, not reset by dying");
    CHECK(arena_state.heroes[0].flow_earned == 999, "flow_earned survives death");
    CHECK(arena_state.heroes[0].xp == 321, "XP survives death");
    CHECK(arena_state.heroes[0].kills == 3 && arena_state.heroes[0].deaths == 1, "kills and deaths both survive a respawn cycle unchanged");
    CHECK(arena_state.heroes[0].equipped_item[ARENA_ITEM_SLOT_WEAPON] == 0, "equipped items survive death -- dying doesn't unequip you");
    CHECK(arena_state.heroes[0].max_hp == 100 + ARENA_ITEMS[0].bonus_max_hp,
          "item stat bonuses are correctly re-applied after the respawn clear resets max_hp to the flat base");
}

/* apply_damage/arena_tick_respawns are static to arena_game.c -- the three
 * tests below drive real kills entirely through the public arena_update_teams
 * loop (a lone, heavily-buffed enemy hero parked in melee range), the same
 * "run real ticks until it happens" style already used elsewhere in this
 * suite (e.g. test_dead_hero_respawns_at_owned_node_once_timer_expires). */

static void test_tyler_shared_fate_clone_death_kills_tyler_and_siblings(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 500; /* outlasts the clone's own counter-attacks */
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;

    arena_cast_r(0);
    ArenaHero *clone = &arena_state.heroes[ARENA_MAX_HEROES];
    CHECK(clone->active && clone->alive, "sanity: the first clone slot is alive before combat");
    ArenaHero *sibling = &arena_state.heroes[ARENA_MAX_HEROES + 1];
    /* Separate the clone from Tyler and park the sibling clone far out of
       reach, so the lone enemy below can only ever fight the ONE clone --
       isolates this to "a clone's own death cascades," not a mixed brawl. */
    clone->x = 5.0f; clone->z = 0.0f;
    sibling->x = -50.0f; sibling->z = -50.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 5.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;

    for (int i = 0; i < 500 && arena_state.heroes[0].alive; i++) arena_update_teams(16);

    CHECK(!clone->alive, "the clone that was actually hit dies");
    CHECK(!arena_state.heroes[0].alive, "the real Tyler dies too -- literal OG shared fate, no exceptions");
    CHECK(!sibling->alive, "every other linked clone dies in the same cascade");
    CHECK(!sibling->active, "a dead clone's slot frees immediately, no respawn queue");
}

static void test_tyler_death_kills_his_clones_too(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 500;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].max_hp = arena_state.heroes[0].hp = 40; /* converges quickly under real combat */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;

    arena_cast_r(0);
    /* Park both clones far away -- isolates this to Tyler's OWN death
       triggering the cascade, not a clone dying alongside him in the same
       fight. */
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        arena_state.heroes[i].x = -50.0f;
        arena_state.heroes[i].z = -50.0f;
    }
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;

    for (int i = 0; i < 500 && arena_state.heroes[0].alive; i++) arena_update_teams(16);

    CHECK(!arena_state.heroes[0].alive, "Tyler himself dies from real combat");
    ArenaHero *clone = &arena_state.heroes[ARENA_MAX_HEROES];
    CHECK(!clone->alive && !clone->active, "Tyler dying kills his clones too, same shared-fate link in the other direction");
}

/* S170-188, real bug found while auditing S170-141's own clone-fights-through-the-generic-
 * melee-loop design: a clone landing the actual killing blow used to credit Flow/XP/kills to
 * the clone's own disposable ArenaHero slot -- lost the instant that slot gets reused on
 * Tyler's next R, never reaching Tyler, the real player whose army earned the kill. */
static void test_clone_kill_credits_tyler_not_the_clone(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_GHOST; /* 0 base armor -- exact hit-damage math */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 1; /* any landed hit finishes it */
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = -50.0f; arena_state.heroes[0].z = -50.0f; /* far away -- only the clone can ever reach the target */

    arena_cast_r(0);
    ArenaHero *clone = &arena_state.heroes[ARENA_MAX_HEROES];
    CHECK(clone->active && clone->alive, "sanity: the first clone slot is alive before combat");
    ArenaHero *sibling = &arena_state.heroes[ARENA_MAX_HEROES + 1];
    sibling->x = -50.0f; sibling->z = -50.0f; /* parked with Tyler, out of the fight entirely */
    clone->x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    clone->z = arena_state.heroes[ARENA_TEAM_SIZE].z;

    for (int i = 0; i < 500 && arena_state.heroes[ARENA_TEAM_SIZE].alive; i++) arena_update_teams(16);

    CHECK(!arena_state.heroes[ARENA_TEAM_SIZE].alive, "sanity: the clone actually landed the kill");
    CHECK(arena_state.heroes[0].flow == ARENA_HERO_KILL_FLOW, "the real Tyler gets the full kill bounty for his clone's kill");
    CHECK(arena_state.heroes[0].kills == 1, "and the kill count, credited to Tyler, not the clone");
    CHECK(clone->active && clone->flow == 0 && clone->kills == 0, "the clone's own (disposable) slot gets nothing -- this is exactly the bug: crediting here would be lost the moment the slot is reused");
}

/* S170-143: hover casting (WoW-macro-style mouseover targeting), starting with Doc Wheel. */

static void test_hover_ally_or_nearest_falls_back_when_nothing_hovered(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* nearest ally */
    arena_state.heroes[2].x = 10; arena_state.heroes[2].z = 0; /* farther ally */

    ArenaHero *result = arena_hover_ally_or_nearest(0);

    CHECK(result == &arena_state.heroes[1], "with no hover target set (-1 default after init), falls back to the nearest ally exactly as before");
}

static void test_hover_ally_or_nearest_prefers_hover_target_over_nearest(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* nearest ally -- should be skipped */
    arena_state.heroes[2].x = 10; arena_state.heroes[2].z = 0; /* the hovered, farther ally */

    arena_set_hover_target(0, 2);
    ArenaHero *result = arena_hover_ally_or_nearest(0);

    CHECK(result == &arena_state.heroes[2], "a real WoW-macro mouseover target wins over nearest-ally targeting, even when farther away");
}

static void test_hover_ally_or_nearest_falls_back_for_enemy_target(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* nearest ally */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.5f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* hovered, but an ENEMY */

    arena_set_hover_target(0, ARENA_TEAM_SIZE);
    ArenaHero *result = arena_hover_ally_or_nearest(0);

    CHECK(result == &arena_state.heroes[1], "hovering an enemy hero never redirects an ally-heal onto them -- falls back to nearest ally");
}

static void test_hover_ally_or_nearest_falls_back_for_dead_target(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* nearest, living ally */
    arena_state.heroes[2].x = 2; arena_state.heroes[2].z = 0;
    arena_state.heroes[2].alive = 0; /* hovered, but dead */

    arena_set_hover_target(0, 2);
    ArenaHero *result = arena_hover_ally_or_nearest(0);

    CHECK(result == &arena_state.heroes[1], "hovering a dead ally falls back to nearest ally rather than returning the corpse");
}

static void test_set_hover_target_out_of_range_owner_is_a_safe_noop(void) {
    arena_init_teams();
    arena_set_hover_target(-1, 0); /* must not crash or write out of bounds */
    arena_set_hover_target(ARENA_MAX_HEROES, 0);
    CHECK(1, "arena_set_hover_target with an out-of-range owner does not crash");
}

static void test_doc_wheel_q_heals_hover_target_over_nearest_ally(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* nearest ally -- should NOT be healed */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[2].x = 10; arena_state.heroes[2].z = 0; /* the mouseover-hovered ally */
    arena_state.heroes[2].max_hp = 100; arena_state.heroes[2].hp = 50;

    arena_set_hover_target(0, 2);
    arena_cast_q(0);

    CHECK(arena_state.heroes[2].hp > 50, "Bedside Manner heals the hovered ally, a real WoW-style mouseover heal");
    CHECK(arena_state.heroes[1].hp == 50, "...not the nearer, un-hovered ally -- the whole point of hover casting");
}

/* S170-147: healing fountains, 2 corners, neutral (heals any team). */

static void test_fountain_heals_hero_in_radius(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 50;

    arena_tick_fountains(1000); /* one full heal tick */

    CHECK(arena_state.heroes[0].hp == 50 + ARENA_FOUNTAIN_HEAL_PER_SEC,
          "a hero standing at a fountain's position heals for one tick's worth");
}

static void test_fountain_does_not_heal_hero_outside_radius(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx + ARENA_FOUNTAIN_RADIUS + 5.0f; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 50;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].hp == 50, "a hero well outside the fountain's radius is not healed");
}

static void test_fountain_heals_either_team_neutral(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].team = 1; /* team 1 hero at team 0's own default index -- fountains don't care */
    float fx, fz;
    arena_fountain_position(1, &fx, &fz); /* the OTHER fountain, still neutral */
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 50;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].hp == 50 + ARENA_FOUNTAIN_HEAL_PER_SEC,
          "fountains heal any team, a genuinely neutral contestable resource");
}

static void test_fountain_caps_healing_at_max_hp(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 100 - 1;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].hp == 100, "fountain healing caps at max_hp, doesn't overheal");
}

static void test_fountain_does_not_heal_dead_hero(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 0;
    arena_state.heroes[0].alive = 0;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].hp == 0, "a dead hero standing at a fountain's position is not healed");
}

static void test_fountain_restores_mana(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_mp = ARENA_MP_MAX; arena_state.heroes[0].mp = 10;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].mp == 10 + ARENA_FOUNTAIN_MANA_PER_SEC,
          "founder: 'fountains should also restore mana' -- one tick's worth restored");
}

static void test_fountain_mana_restore_caps_at_max(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_mp = ARENA_MP_MAX; arena_state.heroes[0].mp = ARENA_MP_MAX - 1;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].mp == ARENA_MP_MAX, "fountain mana restore caps at max_mp, doesn't overfill");
}

/* S170-190, founder: "add berserker and health regen powerups like from warsong gulch in
 * between the nodes." */

static void test_powerup_pickup_grants_buff_and_deactivates(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaPowerup *berserker = &arena_state.powerups[ARENA_POWERUP_BERSERKER];
    arena_state.heroes[0].x = berserker->x; arena_state.heroes[0].z = berserker->z;
    CHECK(berserker->active, "sanity: the powerup starts active on layout reset");

    arena_tick_powerups(16);

    CHECK(arena_state.heroes[0].berserker_ms == ARENA_POWERUP_BUFF_MS, "walking onto the Berserker powerup grants its full buff duration");
    CHECK(!berserker->active, "the powerup deactivates once grabbed");
    CHECK(berserker->respawn_ms_remaining == ARENA_POWERUP_RESPAWN_MS, "and starts its respawn countdown");
}

static void test_powerup_out_of_radius_grants_nothing(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaPowerup *berserker = &arena_state.powerups[ARENA_POWERUP_BERSERKER];
    arena_state.heroes[0].x = berserker->x + ARENA_POWERUP_PICKUP_RADIUS + 5.0f;
    arena_state.heroes[0].z = berserker->z;

    arena_tick_powerups(16);

    CHECK(arena_state.heroes[0].berserker_ms == 0, "well outside the pickup radius grants nothing");
    CHECK(berserker->active, "and the powerup stays on the map");
}

static void test_powerup_respawns_after_cooldown(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaPowerup *regen = &arena_state.powerups[ARENA_POWERUP_REGEN];
    arena_state.heroes[0].x = regen->x; arena_state.heroes[0].z = regen->z;
    arena_tick_powerups(16);
    CHECK(!regen->active, "sanity: grabbed and now inactive");

    arena_state.heroes[0].x = -999.0f; /* well away, so it can't just get re-grabbed the instant it respawns */
    for (int i = 0; i < ARENA_POWERUP_RESPAWN_MS / 16 + 2; i++) arena_tick_powerups(16);

    CHECK(regen->active, "the powerup respawns once its full cooldown elapses");
}

static void test_berserker_buff_adds_bonus_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_GHOST; /* 0 base armor -- exact hit-damage math */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 1000;
    arena_state.heroes[0].x = arena_state.heroes[ARENA_TEAM_SIZE].x;
    arena_state.heroes[0].z = arena_state.heroes[ARENA_TEAM_SIZE].z;
    arena_state.heroes[0].berserker_ms = ARENA_POWERUP_BUFF_MS;

    /* S170-204: first tick begins windup, second (a full windup duration) lands the hit. */
    arena_update_teams(16);
    arena_update_teams((unsigned int)ARENA_ATTACK_WINDUP_MS);

    int expected_dmg = ARENA_ATTACK_DAMAGE + ARENA_BERSERKER_BONUS_AD;
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 1000 - expected_dmg,
          "an active Berserker buff adds its flat bonus on top of the normal auto-attack damage");
}

static void test_regen_buff_heals_over_time(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].hp = 50;
    arena_state.heroes[0].regen_ms = ARENA_POWERUP_BUFF_MS;

    arena_update_teams(1000); /* one full second */

    CHECK(arena_state.heroes[0].hp == 50 + ARENA_POWERUP_REGEN_HP_PER_SEC, "an active Regen buff heals at its documented per-second rate");
}

static void test_powerup_buffs_tick_down_and_expire(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].berserker_ms = 500;
    arena_state.heroes[0].regen_ms = 500;

    arena_update_teams(400);
    CHECK(arena_state.heroes[0].berserker_ms == 100, "Berserker ticks down like every other timed effect");
    CHECK(arena_state.heroes[0].regen_ms == 100, "Regen ticks down the same way");

    arena_update_teams(200);
    CHECK(arena_state.heroes[0].berserker_ms == 0, "Berserker expires (clamped at 0) once its duration elapses");
    CHECK(arena_state.heroes[0].regen_ms == 0, "Regen expires the same way");
}

/* S170-148: mana visibility + combat-gated regen. */

static void test_mana_regenerates_out_of_combat(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].max_mp = ARENA_MP_MAX;
    arena_state.heroes[0].mp = 0;
    arena_state.heroes[0].combat_timer_ms = 0; /* out of combat */

    arena_update_teams(1000);

    CHECK(arena_state.heroes[0].mp > 0, "mana regenerates normally once combat_timer_ms has expired");
}

static void test_mana_regenerates_slowly_in_combat(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].max_mp = ARENA_MP_MAX;
    arena_state.heroes[0].mp = 0;
    arena_state.heroes[0].combat_timer_ms = ARENA_COMBAT_TIMEOUT_MS; /* just took damage */

    arena_update_teams(1000); /* combat_timer_ms ticks down but stays > 0 the whole second */

    CHECK(arena_state.heroes[0].mp == ARENA_MP_REGEN_IN_COMBAT_PER_SEC,
          "founder: 'have mana tic up slowly 1 per second always' -- a slow trickle even mid-fight, not a dead stop");
}

static void test_mana_regenerates_faster_out_of_combat_than_in_combat(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].max_mp = ARENA_MP_MAX;
    arena_state.heroes[0].mp = 0;
    arena_state.heroes[0].combat_timer_ms = ARENA_COMBAT_TIMEOUT_MS;
    arena_state.heroes[1].max_mp = ARENA_MP_MAX;
    arena_state.heroes[1].mp = 0;
    arena_state.heroes[1].combat_timer_ms = 0;

    arena_update_teams(1000);

    CHECK(arena_state.heroes[0].mp == ARENA_MP_REGEN_IN_COMBAT_PER_SEC, "in-combat hero regens at the slow trickle rate");
    CHECK(arena_state.heroes[1].mp == ARENA_MP_REGEN_PER_SEC, "out-of-combat hero regens at the full rate");
    CHECK(arena_state.heroes[1].mp > arena_state.heroes[0].mp, "out-of-combat regen is genuinely faster than the in-combat trickle");
}

static void test_mana_regen_accumulates_correctly_across_many_small_ticks(void) {
    /* S170-150 bugfix: this is the actual production tick shape
       (arena_server always calls with dt_ms=16) -- (int)(6 * 16 / 1000.0)
       truncates to 0 on every single call without a persistent fractional
       accumulator, so a naive per-tick cast would silently never regen
       mana at all in real gameplay. 63 ticks of 16ms = 1008ms, just over a
       full second. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* Keep one living hero on team 1 (deactivating ALL of it would instantly
       trigger a real team-wipe win condition on the first tick -- team 1
       alive-count 0 and owning no node -- which then freezes every
       subsequent arena_update_teams() call in the loop below for the rest
       of the test, a real gotcha this session already hit once before in
       bot-mode testing with an undersized lobby). */
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1000.0f; /* far away -- not a combat participant, just present */
    arena_state.heroes[0].max_mp = ARENA_MP_MAX;
    arena_state.heroes[0].mp = 0;
    arena_state.heroes[0].combat_timer_ms = 0;

    for (int i = 0; i < 63; i++) arena_update_teams(16);

    CHECK(arena_state.heroes[0].mp >= ARENA_MP_REGEN_PER_SEC,
          "mana actually regenerates over many real-sized (16ms) ticks, not just in single large-dt_ms test steps");
}

static void test_taking_damage_rearms_the_combat_timer(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].combat_timer_ms = 0;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.5f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].attack_cooldown_ms = 0;

    /* S170-204: first tick begins the enemy's windup, second (a full windup duration) lands
       their hit. */
    arena_update_teams(16);
    arena_update_teams((unsigned int)ARENA_ATTACK_WINDUP_MS);

    CHECK(arena_state.heroes[0].combat_timer_ms > 0, "taking damage re-arms the combat timer, gating mana regen again");
}

static void test_combat_timer_counts_down_to_zero(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].combat_timer_ms = 500;

    arena_update_teams(1000); /* more than enough to fully expire it */

    CHECK(arena_state.heroes[0].combat_timer_ms == 0, "the combat timer counts down and pins at 0, doesn't go negative");
}

/* S170-152: "capturing node should not make the user take damage" -- a team-flavored
 * node-guardian creep no longer attacks its own owning team, only the opposing one. */

static void test_team_creep_does_not_attack_own_owning_team(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* S170-161: team 0 owns everything -- its creep has nowhere to march,
       staying at its graveyard spawn for the whole test. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* creep flavor becomes TEAM0 */
    arena_state.heroes[0].team = 0;
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    arena_state.heroes[0].x = gx;
    arena_state.heroes[0].z = gz;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16); /* spawn */
    arena_tick_creeps(ARENA_CREEP_ATTACK_COOLDOWN_MS); /* long enough for one attack, if it were going to land */

    CHECK(arena_state.heroes[0].hp == 100,
          "a team-flavored creep does not attack a hero of its own owning team standing at its graveyard spawn");
}

static void test_team_creep_still_attacks_opposing_team(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team 0 owns everything -- creep has nowhere to march */
    arena_state.heroes[0].team = 1; /* the enemy, trying to flip it */
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK; /* 0 base armor -- exact hit-damage math (S170-211) */
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    arena_state.heroes[0].x = gx;
    arena_state.heroes[0].z = gz;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16); /* spawn -- all 5 nodes are team 0's, so all 5 creeps spawn at the same graveyard point */
    /* Unlike arena_hero_attack_creeps (one hero-initiated hit per tick,
       first in-range creep wins via its own `break`), arena_tick_creeps'
       creep-initiated attack loop has no such per-tick cap -- every alive
       creep independently checks and can attack. With all 5 creeps
       stacked on the same graveyard tile this tick, the hero would take
       5x the intended single-creep hit. Isolate the one creep this test
       actually cares about by killing the other 4 off before the attack
       tick -- same "reduce the moving parts to what's actually being
       tested" convention this file already uses elsewhere. */
    for (int i = 1; i < ARENA_MAX_CREEPS; i++) { arena_state.creeps[i].alive = 0; arena_state.creeps[i].respawn_ms_remaining = ARENA_CREEP_TEAM_RESPAWN_MS * 10; }
    arena_tick_creeps(ARENA_CREEP_ATTACK_COOLDOWN_MS);

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_CREEP_TEAM_DAMAGE,
          "a team-flavored creep still attacks the OPPOSING team -- the real counter-play, unchanged");
}

static void test_neutral_creep_still_attacks_anyone(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.nodes[0].owner = 0; /* neutral/contested -- creep flavor stays NEUTRAL */
    arena_state.heroes[0].team = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK; /* 0 base armor -- exact hit-damage math (S170-211) */
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16); /* spawn */
    arena_tick_creeps(ARENA_CREEP_ATTACK_COOLDOWN_MS);

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_CREEP_NEUTRAL_DAMAGE,
          "a NEUTRAL/contested creep still attacks anyone regardless of team -- the real 'fight through the prize' challenge, unchanged");
}

/* Jungle Camps -- The Four Heavenly Kings, Milestone 1 smoke tests (2026-08-10).
   GoblinFoxDragon/docs2/JUNGLE_CAMPS_NORTHSTAR.md §3.1-3.2. Mirrors the verification GFD's own
   fork already did for the same code: 4 camps at the correct N/S/E/W positions, minions spawn
   from the opening bell (no initial delay, unlike lane creeps), and a hero standing in a camp
   takes real damage over time. */

static void test_camp_positions_are_the_four_cardinal_edge_midpoints(void) {
    float edge = ARENA_HALF_EXTENT - 8.0f;
    float x, z;

    arena_camp_position(0, &x, &z);
    CHECK(x == 0.0f && z == edge, "camp 0 (N) sits at the north edge midpoint");

    arena_camp_position(1, &x, &z);
    CHECK(x == 0.0f && z == -edge, "camp 1 (S) sits at the south edge midpoint");

    arena_camp_position(2, &x, &z);
    CHECK(x == edge && z == 0.0f, "camp 2 (E) sits at the east edge midpoint");

    arena_camp_position(3, &x, &z);
    CHECK(x == -edge && z == 0.0f, "camp 3 (W) sits at the west edge midpoint");
}

static void test_camp_minions_wave_spawn_from_the_opening_bell(void) {
    /* Unlike lane creeps (ARENA_LANE_WAVE_INITIAL_DELAY_MS grace period), camps have no initial
       delay -- docs2/JUNGLE_CAMPS_NORTHSTAR.md §3.2: "Live from the opening bell." */
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;

    arena_tick_camp_minions(16); /* one tiny tick -- first wave should already be up */

    int active_count = 0;
    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
        if (arena_state.camp_minions[i].active) active_count++;
    }
    CHECK(active_count == ARENA_CAMP_MINIONS_PER_WAVE * ARENA_CAMP_COUNT,
          "all 4 camps spawn a full wave immediately, no initial delay unlike lane creeps");
}

static void test_camp_minion_attacks_nearby_hero(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK; /* 0 base armor -- exact hit-damage math */
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_camp_minions(16); /* spawn camp 0's (N) wave */
    float cx, cz;
    arena_camp_position(0, &cx, &cz);
    arena_state.heroes[0].x = cx;
    arena_state.heroes[0].z = cz;

    /* Isolate to a single attacker: ARENA_CAMP_MINIONS_PER_WAVE is 2, both within aggro range of
       a hero standing dead-center of the camp -- ported verbatim from GFD's own implementation,
       which doesn't cap "one attacker per target per tick" on the minion side (only the hero's
       OWN attack is one-target-per-swing). Real, intended gang-up behavior, not a bug -- deactivate
       the second minion so this test isolates exactly one attacker's damage, same "reduce the
       moving parts to what's actually being tested" convention this file already uses elsewhere. */
    int seen_first = 0;
    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
        if (!arena_state.camp_minions[i].active) continue;
        if (!seen_first) { seen_first = 1; continue; }
        arena_state.camp_minions[i].active = 0;
    }

    arena_tick_camp_minions(ARENA_CAMP_MINION_ATTACK_COOLDOWN_MS);

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_CAMP_MINION_DAMAGE,
          "a hero standing in a jungle camp takes real damage from its neutral minions");
}

static void test_hero_kills_camp_minion_and_earns_reward(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 500; /* survive the return fire */
    arena_state.heroes[0].flow = 0;
    arena_state.heroes[0].xp = 0;

    arena_tick_camp_minions(16); /* spawn camp 0's (N) wave */
    float cx, cz;
    arena_camp_position(0, &cx, &cz);
    arena_state.heroes[0].x = cx;
    arena_state.heroes[0].z = cz;

    /* Isolate the single minion this test cares about, same "reduce moving parts" convention
       test_neutral_creep_still_attacks_anyone's own file already uses. */
    int target_idx = -1;
    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
        if (arena_state.camp_minions[i].active) { target_idx = i; break; }
    }
    CHECK(target_idx >= 0, "setup: at least one camp minion is active to attack");

    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
        if (i != target_idx) arena_state.camp_minions[i].active = 0;
    }

    int hp_before = arena_state.camp_minions[target_idx].hp;
    arena_hero_attack_camp_minions(0);
    CHECK(arena_state.camp_minions[target_idx].hp < hp_before, "hero's auto-attack damages the camp minion");

    /* Whittle it down the rest of the way -- attack_cooldown_ms is spent, not ticked, by
       arena_hero_attack_camp_minions itself (same idiom as lane creeps), so reset it manually
       between swings same as this file's other multi-swing kill tests do. */
    while (arena_state.camp_minions[target_idx].active) {
        arena_state.heroes[0].attack_cooldown_ms = 0;
        arena_hero_attack_camp_minions(0);
    }

    CHECK(arena_state.heroes[0].flow == ARENA_CAMP_MINION_KILL_FLOW, "killing a camp minion grants ARENA_CAMP_MINION_KILL_FLOW");
    CHECK(arena_state.heroes[0].xp == ARENA_CAMP_MINION_KILL_XP, "killing a camp minion grants ARENA_CAMP_MINION_KILL_XP");
}

/* §3.4 Anti-stall escalation smoke tests (2026-08-10). */

static void test_camp_does_not_escalate_before_the_threshold(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;

    arena_tick_camp_minions(16); /* spawn */
    arena_tick_camp_minions(ARENA_CAMP_ESCALATION_THRESHOLD_MS - 1000);
    CHECK(!arena_state.camp_escalated[0], "a camp with a living minion doesn't escalate before the threshold elapses");

    float x_before = -999.0f;
    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
        if (arena_state.camp_minions[i].active && arena_state.camp_minions[i].camp_index == 0) { x_before = arena_state.camp_minions[i].x; break; }
    }
    arena_tick_camp_minions(16);
    float x_after = -999.0f;
    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
        if (arena_state.camp_minions[i].active && arena_state.camp_minions[i].camp_index == 0) { x_after = arena_state.camp_minions[i].x; break; }
    }
    CHECK(x_before == x_after, "an unescalated camp's minions stay stationary, no drift");
}

static void test_camp_escalates_and_minions_march_toward_nearest_node(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;

    arena_tick_camp_minions(16); /* spawn camp 0's (N) wave */
    /* Cross the threshold with a small final tick (32ms) rather than one giant jump -- a giant
       dt_ms both crosses the threshold AND covers the march's full march-speed*dt_sec distance
       in the SAME call (escalation state is computed before the movement loop within one tick),
       leaving nothing left to observe moving afterward. A small final tick crosses the boundary
       with only a tiny (2.0 * 0.032 = 0.064 unit) march step, so there's real "before" state
       left for the dedicated march tick below to move further from. */
    arena_tick_camp_minions(ARENA_CAMP_ESCALATION_THRESHOLD_MS - 32);
    CHECK(!arena_state.camp_escalated[0], "setup: not escalated yet, one tick before the threshold");
    arena_tick_camp_minions(32);
    CHECK(arena_state.camp_escalated[0], "a camp with a continuously-living minion escalates once the threshold elapses");

    int found = -1;
    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
        if (arena_state.camp_minions[i].active && arena_state.camp_minions[i].camp_index == 0) { found = i; break; }
    }
    CHECK(found >= 0, "setup: camp 0 still has a living minion to march");
    float x0 = arena_state.camp_minions[found].x, z0 = arena_state.camp_minions[found].z;
    float nx, nz;
    /* Nearest node to the North camp's own fixed position -- same helper the sim itself uses. */
    float best_dist = -1.0f;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        float dx = arena_state.nodes[n].x - x0, dz = arena_state.nodes[n].z - z0;
        float d = dx * dx + dz * dz;
        if (best_dist < 0.0f || d < best_dist) { best_dist = d; nx = arena_state.nodes[n].x; nz = arena_state.nodes[n].z; }
    }
    float dist_to_node_before = sqrtf((nx - x0) * (nx - x0) + (nz - z0) * (nz - z0));

    arena_tick_camp_minions(1000); /* march for a real second */
    float x1 = arena_state.camp_minions[found].x, z1 = arena_state.camp_minions[found].z;
    float dist_to_node_after = sqrtf((nx - x1) * (nx - x1) + (nz - z1) * (nz - z1));

    CHECK(x0 != x1 || z0 != z1, "an escalated minion actually moves, unlike an unescalated one");
    CHECK(dist_to_node_after < dist_to_node_before, "an escalated minion marches TOWARD the nearest node, not a random direction");
}

static void test_camp_escalation_rearms_once_fully_cleared(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;

    arena_tick_camp_minions(16);
    arena_tick_camp_minions(ARENA_CAMP_ESCALATION_THRESHOLD_MS);
    CHECK(arena_state.camp_escalated[0], "setup: camp 0 is escalated");

    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
        if (arena_state.camp_minions[i].camp_index == 0) arena_state.camp_minions[i].active = 0;
    }
    arena_tick_camp_minions(16);
    CHECK(!arena_state.camp_escalated[0], "fully clearing a camp re-arms its escalation state");
    CHECK(arena_state.camp_uncleared_ms[0] == 0, "...and resets the uncleared timer back to 0, not just the escalated flag");
}

/* Jungle Camps -- The Four Heavenly Kings, Milestone 2 smoke tests (2026-08-10).
   docs2/JUNGLE_CAMPS_NORTHSTAR.md §3.3. One test per King's distinct buff mechanic, plus the
   spawn-timer gate and a real kill-to-death loop. */

static void test_king_does_not_spawn_before_one_minute(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;

    arena_tick_kings(ARENA_KING_SPAWN_DELAY_MS - 1000);
    int any_active = 0;
    for (int c = 0; c < ARENA_CAMP_COUNT; c++) if (arena_state.kings[c].active) any_active = 1;
    CHECK(!any_active, "no King spawns before the 1:00 delay elapses");

    arena_tick_kings(1000);
    int all_active = 1;
    for (int c = 0; c < ARENA_CAMP_COUNT; c++) if (!arena_state.kings[c].active) all_active = 0;
    CHECK(all_active, "all 4 Kings spawn silently once the 1:00 delay elapses");
}

static void test_king_respawns_on_its_own_timer_after_death(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 1000;

    arena_tick_kings(ARENA_KING_SPAWN_DELAY_MS);
    float kx, kz;
    arena_camp_position(0, &kx, &kz);
    arena_state.heroes[0].x = kx;
    arena_state.heroes[0].z = kz;
    while (arena_state.kings[0].active) {
        arena_state.heroes[0].attack_cooldown_ms = 0;
        arena_hero_attack_kings(0);
    }
    CHECK(!arena_state.kings[0].active, "setup: the North King is dead");

    arena_tick_kings(ARENA_KING_RESPAWN_MS - 1000);
    CHECK(!arena_state.kings[0].active, "a defeated King does not respawn before its own ARENA_KING_RESPAWN_MS timer elapses");

    arena_tick_kings(1000);
    CHECK(arena_state.kings[0].active, "a defeated King respawns once ARENA_KING_RESPAWN_MS elapses");
    CHECK(arena_state.kings[0].alive, "...alive again");
    CHECK(arena_state.kings[0].hp == ARENA_KING_HP, "...at full HP");
    CHECK(arena_state.kings[0].x == kx && arena_state.kings[0].z == kz, "...back at its own camp's fixed position");
}

/* §25.3 Synergy decay smoke tests (2026-08-10) -- a live-match comeback mechanic, distinct from
   noisy-gestalt's own training-time concept despite the shared "synergy" word. */

static void test_synergy_starts_fully_decayed_no_ambient_bonus(void) {
    arena_init_teams();
    CHECK(arena_state.synergy_tier[0] == ARENA_SYNERGY_TIER_COUNT - 1,
          "a fresh team match starts at the fully-decayed (weakest, zero-bonus) synergy tier");
    CHECK(arena_state.synergy_tier[1] == ARENA_SYNERGY_TIER_COUNT - 1, "...for both teams");
}

/* Shared setup for the two scenarios below -- a fresh arena_init_teams() + fresh Unicorn each
   time avoids any residual state (mana, moving/target, cooldown) leaking between the "no bonus"
   and "full bonus" measurements, same isolation every other test in this file already gets by
   construction (each test function starts with its own arena_init_teams() call). */
static int synergy_cdr_scenario_cast(int tier) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* Real bug caught writing this test: deactivating every OTHER hero for isolation
       (the common pattern this whole file uses) also deactivates heroes[ARENA_TEAM_SIZE] --
       exactly the slot arena_synergy_cdr_pct's own team-mode guard checks -- which silently
       makes the guard read "not team mode" and suppress the bonus regardless of tier. Restore
       it: this guard's real-gameplay invariant (that slot's `active` stays 1 for the whole
       match once team mode starts, only `alive` toggles on death) doesn't hold under this
       test's own artificial deactivation, so it has to be corrected back explicitly. */
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_UNICORN;
    /* Unicorn's Q dashes toward the move target if moving, else toward a foe -- with every
       other hero deactivated (no foe) it needs a real move target or it silently no-ops
       (unicorn_cast_q's own "nothing to dash toward" early-return). */
    arena_state.heroes[0].moving = 1;
    arena_state.heroes[0].target_x = arena_state.heroes[0].x + 5.0f;
    arena_state.heroes[0].target_z = arena_state.heroes[0].z;
    arena_state.synergy_tier[0] = tier;
    arena_cast_q(0);
    return arena_state.heroes[0].q_cooldown_ms;
}

static void test_synergy_tier_scales_the_ambient_cdr_bonus(void) {
    int cd_no_bonus = synergy_cdr_scenario_cast(ARENA_SYNERGY_TIER_COUNT - 1);
    int cd_full_bonus = synergy_cdr_scenario_cast(0);

    int expected_full_bonus = ARENA_UNICORN_Q_COOLDOWN_MS - (ARENA_UNICORN_Q_COOLDOWN_MS * ARENA_SYNERGY_TIER0_CDR_PCT) / 100;
    CHECK(cd_no_bonus == ARENA_UNICORN_Q_COOLDOWN_MS, "fully-decayed tier applies no ambient CDR bonus");
    CHECK(cd_full_bonus == expected_full_bonus, "tier 0 applies the full ARENA_SYNERGY_TIER0_CDR_PCT ambient bonus");
    CHECK(cd_full_bonus < cd_no_bonus, "...genuinely lower than the no-bonus cooldown");
}

static void test_synergy_bonus_does_not_apply_in_1v1_mode(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_UNICORN);
    /* Simulate the exact stale-state scenario that motivated the team-mode guard: force tier 0
       (max bonus) directly, same as if a prior team-mode test's residual state leaked through --
       the guard must hold regardless of synergy_tier's own value, purely off heroes[ARENA_TEAM_SIZE]. */
    arena_state.synergy_tier[0] = 0;
    arena_cast_q(0);
    CHECK(arena_state.heroes[0].q_cooldown_ms == ARENA_UNICORN_Q_COOLDOWN_MS,
          "the ambient synergy bonus never applies in 1v1 mode, regardless of synergy_tier's own value");
}

static void test_synergy_tier_rerolls_on_its_own_interval(void) {
    arena_init_teams();
    arena_tick_synergy(ARENA_SYNERGY_ROLL_INTERVAL_MS - 1000);
    CHECK(arena_state.synergy_tier[0] == ARENA_SYNERGY_TIER_COUNT - 1,
          "no re-roll yet before the interval elapses -- stays at its initial fully-decayed tier");

    /* After the interval elapses, a real roll happens -- can't assert an exact tier (stochastic
       by design), but tier must land in the valid range. */
    arena_tick_synergy(1000);
    CHECK(arena_state.synergy_tier[0] >= 0 && arena_state.synergy_tier[0] < ARENA_SYNERGY_TIER_COUNT,
          "a real roll after the interval lands on a valid tier");
}

static void test_synergy_lead_shifts_probability_toward_higher_tiers(void) {
    /* Statistical test, not exact-value (the roll is genuinely stochastic by design, founder:
       "there needs to be a random chance... not always happen") -- a team with a real resource
       lead should average a meaningfully HIGHER (more decayed) tier than a team that's even,
       across enough independent rolls to smooth out noise. */
    arena_init_teams();
    arena_state.resources[0] = 1800; /* team 0 way ahead */
    arena_state.resources[1] = 200;

    long sum_ahead = 0, sum_even = 0;
    const int trials = 300;
    for (int i = 0; i < trials; i++) {
        arena_tick_synergy(ARENA_SYNERGY_ROLL_INTERVAL_MS); /* team 0: way ahead */
        sum_ahead += arena_state.synergy_tier[0];
        sum_even += arena_state.synergy_tier[1]; /* team 1: way behind, symmetric case covered by the assert below via the SAME resource gap */
    }
    float avg_ahead = (float)sum_ahead / trials;
    float avg_behind = (float)sum_even / trials;
    CHECK(avg_ahead > avg_behind,
          "the team with a real resource lead averages a meaningfully higher (more decayed) synergy tier than the team that's behind, across many rolls");
}

static void test_hero_kills_north_king_and_gains_wealth_aura(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 1000;
    arena_state.heroes[0].team = 0;

    arena_tick_kings(ARENA_KING_SPAWN_DELAY_MS);
    float kx, kz;
    arena_camp_position(0, &kx, &kz); /* camp 0 = North = Wealth */
    arena_state.heroes[0].x = kx;
    arena_state.heroes[0].z = kz;

    while (arena_state.kings[0].active) {
        arena_state.heroes[0].attack_cooldown_ms = 0;
        arena_hero_attack_kings(0);
    }

    CHECK(arena_state.heroes[0].flow == ARENA_KING_KILL_FLOW, "killing the North King grants ARENA_KING_KILL_FLOW");
    CHECK(arena_state.heroes[0].king_wealth_ms == ARENA_KING_WEALTH_DURATION_MS, "killing the North King grants the killer Bulwark (king_wealth_ms)");

    /* Proximity half: an ally standing near the holder gets bonus armor; one standing far away
       does not. Isolates arena_hero_armor's own aura scan from everything else. */
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[1].team = 0;
    arena_state.heroes[1].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[1].x = kx + 1.0f;
    arena_state.heroes[1].z = kz;
    float near_armor = arena_hero_armor(&arena_state.heroes[1]);

    arena_state.heroes[1].x = kx + 500.0f; /* well outside ARENA_KING_WEALTH_AURA_RADIUS */
    float far_armor = arena_hero_armor(&arena_state.heroes[1]);

    CHECK(near_armor > far_armor, "a nearby ally gets the Bulwark aura's armor bonus; a far-away one does not");
}

static void test_hero_kills_south_king_and_stacks_growth_on_takedown(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 1000;
    arena_state.heroes[0].team = 0;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[1].team = 1; /* enemy, for the takedown half below */
    arena_state.heroes[1].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[1].hp = 1;
    arena_state.heroes[1].max_hp = 100;

    arena_tick_kings(ARENA_KING_SPAWN_DELAY_MS);
    float kx, kz;
    arena_camp_position(1, &kx, &kz); /* camp 1 = South = Growth */
    arena_state.heroes[0].x = kx;
    arena_state.heroes[0].z = kz;

    while (arena_state.kings[1].active) {
        arena_state.heroes[0].attack_cooldown_ms = 0;
        arena_hero_attack_kings(0);
    }
    CHECK(arena_state.heroes[0].king_growth_stacks == 1, "killing the South King grants the killer 1 Bloodroar stack");

    /* Real takedown while holding it: land the killing blow through the real melee path
       (arena_update_teams) so apply_damage's last_attacked_by_owner credit fires, same real
       path any hero-kill always goes through -- apply_damage itself has internal (static)
       linkage, not callable directly from this test file, same reason arena_respawn_hero below
       goes through arena_update_teams's own real respawn-wave path instead of being called
       directly. */
    arena_state.heroes[0].x = arena_state.heroes[1].x;
    arena_state.heroes[0].z = arena_state.heroes[1].z;
    arena_state.heroes[0].attack_cooldown_ms = 0;
    for (int i = 0; i < 50 && arena_state.heroes[1].alive; i++) arena_update_teams(16);
    CHECK(!arena_state.heroes[1].alive, "setup: the real melee loop actually killed the weakened enemy");

    CHECK(arena_state.heroes[0].king_growth_stacks == 2, "a real takedown while holding Growth adds a second stack -- arena_hero_bonus_ad's own multiplication by king_growth_stacks (arena_game.c) is the tested, deterministic consequence of this count, not re-verified separately here since arena_hero_bonus_ad has internal (static) linkage and isn't callable from this test file");

    /* Fragile: dying wipes the buff entirely, no drop, no relay -- the deliberate opposite of
       Music. Trigger a real respawn via the wave system (same pattern
       test_dead_hero_respawns_at_graveyard_when_team_owns_no_node above already uses) so the
       real arena_respawn_hero (internal linkage, not directly callable here) actually runs. */
    arena_state.heroes[0].alive = 0;
    arena_update_teams(ARENA_RESPAWN_WAVE_MS - 100);
    arena_update_teams(200);
    CHECK(arena_state.heroes[0].alive, "setup: the wave respawned the hero");
    CHECK(arena_state.heroes[0].king_growth_stacks == 0, "Bloodroar's stacks do not survive a respawn -- fragile, no relay");
}

static void test_hero_kills_east_king_and_music_spreads_on_respawn(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 1000;
    arena_state.heroes[0].team = 0;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[1].team = 0; /* ally, for the team-viral half below */
    arena_state.heroes[1].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[1].hp = arena_state.heroes[1].max_hp = 100;

    arena_tick_kings(ARENA_KING_SPAWN_DELAY_MS);
    float kx, kz;
    arena_camp_position(2, &kx, &kz); /* camp 2 = East = Music */
    arena_state.heroes[0].x = kx;
    arena_state.heroes[0].z = kz;

    while (arena_state.kings[2].active) {
        arena_state.heroes[0].attack_cooldown_ms = 0;
        arena_hero_attack_kings(0);
    }
    CHECK(arena_state.heroes[0].king_music_carrier, "killing the East King makes the killer a Catchy Song carrier");
    CHECK(arena_state.heroes[1].king_music_carrier, "...and every OTHER living teammate too -- team-viral, not individual");

    /* Attack-speed half: land a real attack (apply_cdr has internal linkage, not directly
       callable here) with vs without the buff and compare the resulting attack_cooldown_ms,
       same "observe through the real call path" fix this file's other King tests already use
       for apply_damage/arena_hero_bonus_ad/arena_respawn_hero. */
    arena_tick_camp_minions(16); /* spawn a fresh camp-minion wave at camp 2 to attack */
    int found = -1;
    for (int m = 0; m < ARENA_MAX_CAMP_MINIONS; m++) if (arena_state.camp_minions[m].active) { found = m; break; }
    CHECK(found >= 0, "setup: a camp minion is active to test Catchy Song's attack-speed half against");
    arena_state.camp_minions[found].x = arena_state.heroes[0].x;
    arena_state.camp_minions[found].z = arena_state.heroes[0].z;
    arena_state.camp_minions[found].hp = arena_state.camp_minions[found].max_hp = 999999; /* survive both test swings below */

    arena_state.heroes[0].attack_cooldown_ms = 0;
    arena_hero_attack_camp_minions(0);
    int carrier_cd = arena_state.heroes[0].attack_cooldown_ms;

    arena_state.heroes[0].king_music_carrier = 0;
    arena_state.heroes[0].attack_cooldown_ms = 0;
    arena_hero_attack_camp_minions(0);
    int no_buff_cd = arena_state.heroes[0].attack_cooldown_ms;
    arena_state.heroes[0].king_music_carrier = 1;

    CHECK(carrier_cd < no_buff_cd, "Catchy Song's attack-speed half genuinely lowers the attack cooldown");

    /* Death and respawn: hero 0 dies (loses it personally), but hero 1 still carries it, so
       hero 0 picks it back up on respawn -- the actual "outlives death" mechanic this King
       exists to prove out. Real respawn triggered via the wave system (arena_respawn_hero has
       internal linkage, not directly callable here), same pattern
       test_dead_hero_respawns_at_graveyard_when_team_owns_no_node already uses. */
    arena_state.heroes[0].alive = 0;
    arena_update_teams(ARENA_RESPAWN_WAVE_MS - 100);
    arena_update_teams(200);
    CHECK(arena_state.heroes[0].alive, "setup: the wave respawned the hero");
    CHECK(arena_state.heroes[0].king_music_carrier, "a carrier who respawns re-picks-up the buff if a teammate still carries it");

    /* Now the real end condition: if EVERY carrier is simultaneously dead, a later respawn does
       NOT revive it -- no live relay left. */
    arena_state.heroes[0].alive = 0;
    arena_state.heroes[1].alive = 0;
    arena_update_teams(ARENA_RESPAWN_WAVE_MS - 100);
    arena_update_teams(200);
    CHECK(arena_state.heroes[0].alive, "setup: the wave respawned the hero again");
    CHECK(!arena_state.heroes[0].king_music_carrier, "once every carrier is simultaneously dead, the buff has permanently lapsed -- no relay left to respawn into");
}

static void test_hero_kills_west_king_and_gains_team_wide_farsight(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 1000;
    arena_state.heroes[0].team = 0;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[1].team = 1; /* opposing team -- should NOT get the buff */

    arena_tick_kings(ARENA_KING_SPAWN_DELAY_MS);
    float kx, kz;
    arena_camp_position(3, &kx, &kz); /* camp 3 = West = All-Seeing */
    arena_state.heroes[0].x = kx;
    arena_state.heroes[0].z = kz;

    while (arena_state.kings[3].active) {
        arena_state.heroes[0].attack_cooldown_ms = 0;
        arena_hero_attack_kings(0);
    }
    CHECK(arena_state.king_allseeing_team_ms[0] == ARENA_KING_ALLSEEING_DURATION_MS, "killing the West King grants the killer's TEAM Farsight, team-wide flat timer");
    CHECK(arena_state.king_allseeing_team_ms[1] == 0, "the opposing team does not get Farsight");

    /* Econ half: a camp-minion kill while Farsight is active is worth more Flow. */
    arena_tick_camp_minions(16); /* spawn a fresh wave to kill */
    int found = -1;
    for (int m = 0; m < ARENA_MAX_CAMP_MINIONS; m++) if (arena_state.camp_minions[m].active) { found = m; break; }
    CHECK(found >= 0, "setup: a camp minion is active to test the Farsight gold bonus against");
    for (int m = 0; m < ARENA_MAX_CAMP_MINIONS; m++) if (m != found) arena_state.camp_minions[m].active = 0;
    arena_state.camp_minions[found].x = arena_state.heroes[0].x;
    arena_state.camp_minions[found].z = arena_state.heroes[0].z;
    int flow_before = arena_state.heroes[0].flow;
    while (arena_state.camp_minions[found].active) {
        arena_state.heroes[0].attack_cooldown_ms = 0;
        arena_hero_attack_camp_minions(0);
    }
    int gained = arena_state.heroes[0].flow - flow_before;
    CHECK(gained > ARENA_CAMP_MINION_KILL_FLOW, "a jungle-monster kill while Farsight is active earns bonus Flow on top of the normal amount");
}

int main(void) {
    printf("RED GARDEN arena_game headless smoke test\n\n");
    test_movement_reaches_target();
    test_bounds_clamped();
    test_combat_and_win_condition();
    test_bot_steers_toward_player();
    test_click_near_enemy_becomes_attack_move();
    test_unicorn_q_dashes_and_damages();
    test_unicorn_q_respects_cooldown();
    test_unicorn_w_regen_toggle();
    test_mp_starts_full();
    test_mp_regenerates_over_time();
    test_mp_deducted_on_landed_q_cast();
    test_mp_blocks_cast_when_insufficient();
    test_mp_toggle_w_activates_free_drains_over_time();
    test_mp_toggle_w_blocked_at_zero_mana();
    test_mp_toggle_w_auto_deactivates_when_drained_to_empty();
    test_unicorn_r_doubles_armor_temporarily();
    test_unicorn_armor_reduces_incoming_damage();
    test_duck_q_pulls_foe_and_damages();
    test_duck_q_out_of_range_whiffs();
    test_duck_q_never_pulls_past_the_duck();
    test_duck_r_bigger_pull_and_damage_than_q();
    test_duck_has_no_w();
    test_hero_dispatch_is_by_hero_not_owner_slot();
    test_ghost_q_cast_spawns_projectile_no_instant_effect();
    test_ghost_q_out_of_range_whiffs_no_projectile();
    test_ghost_q_projectile_damages_and_silences_on_hit();
    test_ghost_q_projectile_misses_and_no_silence_if_target_steps_off_line();
    test_silenced_hero_cannot_cast();
    test_ghost_w_grants_intangibility_and_expires();
    test_intangible_hero_cannot_be_hit();
    test_ghost_r_zone_damages_enemy_node_guardian();
    test_ghost_r_zone_does_not_damage_own_team_node_guardian();
    test_ghost_r_zone_damages_enemy_lane_creep();
    test_pizza_aura_damages_enemy_node_guardian();
    test_ghost_r_zone_damages_foe_over_time();
    test_ghost_r_zone_stays_fixed_when_foe_moves_away();
    test_frog_q_rewinds_position_and_hp();
    test_frog_q_uses_oldest_available_history_before_3s_elapsed();
    test_frog_r_vanishes();
    test_frog_w_noop_in_1v1_no_ally();
    test_arena_bot_enabled_gates_kit_casts_too();
    test_arena_init_teams_sets_up_both_sides();
    test_nearest_enemy_finds_closest_on_other_team();
    test_nearest_enemy_ignores_teammates_and_dead_heroes();
    test_team_melee_converges_multiple_attackers_on_one_target();
    test_melee_windup_begins_with_no_instant_damage();
    test_melee_windup_completes_and_deals_damage();
    test_melee_windup_canceled_by_a_real_reposition();
    test_melee_windup_survives_bot_ai_noise();
    test_melee_windup_canceled_by_stun();
    test_team_wipe_alone_does_not_win_the_match();
    test_nearest_ally_finds_closest_teammate();
    test_nearest_ally_ignores_enemies_and_dead_teammates();
    test_nearest_ally_never_returns_self();
    test_nearest_ally_null_in_1v1();
    test_ghost_r_zone_heals_ally_in_team_mode();
    test_ghost_r_zone_does_not_heal_ally_outside_radius();
    test_frog_w_refunds_ally_next_cast_cooldown();
    test_frog_w_whiffs_with_no_ally_cooldown_not_consumed();
    test_doc_wheel_q_heals_more_at_lower_hp();
    test_doc_wheel_q_cleanses_silence();
    test_doc_wheel_q_whiffs_with_no_ally_cooldown_not_consumed();
    test_doc_wheel_w_teleports_to_ally();
    test_doc_wheel_r_heals_allies_in_radius_only();
    test_doc_wheel_r_consumes_cooldown_even_with_zero_allies();
    test_node_channel_starts_and_flips_node_neutral_immediately();
    test_node_channel_completes_to_capturing_team();
    test_node_channel_interrupted_by_mixed_presence_loses_all_progress();
    test_node_channel_interrupted_when_capturing_team_leaves();
    test_node_already_owned_by_present_team_has_no_channel();
    test_tower_blocks_capture_while_alive();
    test_tower_destroyed_removes_capture_block();
    test_hero_attack_towers_damages_and_kills_it();
    test_tower_attacks_nearby_hero();
    test_hero_damages_tower_even_with_creep_alive_at_same_spot();
    test_tree_doubles_channel_speed();
    test_flamel_mark_speeds_up_channel_on_marked_ground();
    test_pizza_corrupts_any_channel_regardless_of_side();
    test_tree_q_roots_and_damages_in_range();
    test_tree_q_out_of_range_whiffs();
    test_tree_r_self_roots_grants_armor_and_heals();
    test_tree_r_makes_immune_to_duck_pull();
    test_pizza_q_damages_and_applies_burn();
    test_pizza_burn_ticks_damage_over_time();
    test_pizza_passive_aura_damages_nearby_foe();
    test_pizza_r_prevents_death_for_duration();
    test_flamel_q_roots_without_damage();
    test_flamel_w_heals_allies_in_radius();
    test_flamel_w_heals_more_on_marked_ground();
    test_flamel_r_roots_enemies_and_heals_allies_in_zone();
    test_flamel_r_mass_marks_nodes_in_radius();
    test_rooted_hero_cannot_move();
    test_stunned_hero_cannot_move();
    test_stunned_hero_cannot_cast();
    test_stunned_hero_cannot_auto_attack();
    test_stun_ticks_down_and_expires();
    test_apply_stun_refresh_never_shortens();
    test_slowed_hero_moves_proportionally_slower();
    test_slow_ticks_down_and_expires();
    test_respawn_clears_stun_and_slow();
    test_morrigan_passive_grants_armor_on_contested_node();
    test_morrigan_q_executes_harder_at_low_hp();
    test_morrigan_w_teleports_and_roots_nearest_enemy();
    test_morrigan_r_zone_executes_harder_at_low_hp();
    test_dagda_passive_regenerates_hp();
    test_dagda_q_kills_when_enemy_in_range();
    test_dagda_q_revives_when_only_hurt_ally_in_range();
    test_dagda_w_heals_allies_and_cc_enemies_at_once();
    test_dagda_r_floor_and_heal();
    test_courier_q_dashes_and_damages();
    test_courier_q_cleanses_self_debuffs();
    test_courier_w_teleports_to_farther_node();
    test_courier_r_drains_life_from_nearest_enemy();
    test_courier_r_out_of_range_whiffs();
    test_creep_spawns_on_first_tick_with_flavor_from_node_owner();
    test_team_creep_spawns_at_graveyard_not_node_position();
    test_neutral_creep_still_spawns_at_node_position();
    test_team_creep_marches_toward_nearest_unowned_node();
    test_team_creep_idles_once_its_team_owns_every_node();
    test_team_creep_march_redirects_when_target_node_gets_captured();
    test_creep_attacks_nearby_hero();
    test_hero_does_not_attack_creep_while_an_enemy_hero_is_in_range();
    test_hero_kills_creep_and_queues_correct_respawn_timer();
    test_neutral_creep_kill_grants_capture_bonus_only_while_channeling();
    test_team_creep_kill_by_owning_team_heals();
    test_team_creep_kill_by_enemy_team_helps_flip_the_node();
    test_lane_creep_wave_spawns_for_both_teams_after_initial_delay();
    test_lane_creep_wave_spawns_a_melee_caster_mix();
    test_lane_creep_caster_engages_from_farther_than_melee();
    test_lane_creep_marches_toward_center_when_no_target();
    test_lane_creep_attacks_nearby_enemy_hero_and_does_not_advance();
    test_lane_creep_aggro_redirects_to_attacker_over_a_closer_bystander();
    test_lane_creeps_fight_each_other_when_opposing_teams_meet();
    test_hero_last_hits_a_lane_creep_already_weakened_by_the_wave_clash();
    test_hero_kills_lane_creep_in_range();
    test_hero_does_not_attack_own_team_lane_creep();
    test_hero_can_deny_own_team_lane_creep_below_half_hp();
    test_hero_does_not_attack_lane_creep_while_enemy_hero_in_range();
    test_lane_creep_despawns_at_final_waypoint_with_no_reward();
    test_lane_creep_wave_respawns_after_the_interval();
    test_camp_positions_are_the_four_cardinal_edge_midpoints();
    test_camp_minions_wave_spawn_from_the_opening_bell();
    test_camp_minion_attacks_nearby_hero();
    test_hero_kills_camp_minion_and_earns_reward();
    test_camp_does_not_escalate_before_the_threshold();
    test_camp_escalates_and_minions_march_toward_nearest_node();
    test_camp_escalation_rearms_once_fully_cleared();
    test_king_does_not_spawn_before_one_minute();
    test_king_respawns_on_its_own_timer_after_death();
    test_synergy_starts_fully_decayed_no_ambient_bonus();
    test_synergy_tier_scales_the_ambient_cdr_bonus();
    test_synergy_bonus_does_not_apply_in_1v1_mode();
    test_synergy_tier_rerolls_on_its_own_interval();
    test_synergy_lead_shifts_probability_toward_higher_tiers();
    test_hero_kills_north_king_and_gains_wealth_aura();
    test_hero_kills_south_king_and_stacks_growth_on_takedown();
    test_hero_kills_east_king_and_music_spreads_on_respawn();
    test_hero_kills_west_king_and_gains_team_wide_farsight();
    test_stealthed_hero_captures_undetected_through_a_crowd_of_visible_enemies();
    test_two_visible_teams_still_interrupt_normally_even_near_a_stealthed_ally();
    test_starting_a_channel_breaks_the_capturer_stealth();
    test_damage_to_channeling_team_interrupts_the_capture();
    test_stop_unit_cancels_move_target();
    test_stop_unit_cancels_attack_target();
    test_stop_unit_out_of_range_owner_is_a_safe_no_op();
    test_attack_move_walks_toward_destination_when_nothing_nearby();
    test_attack_move_opportunistically_engages_enemy_in_range();
    test_attack_move_resumes_destination_once_nothing_left_to_engage();
    test_attack_move_cleared_by_plain_move_command();
    test_attack_move_cleared_by_attack_target_and_stop();
    test_hold_position_halts_movement_in_place();
    test_hold_position_does_not_chase_target_leaving_range();
    test_hold_position_opportunistically_engages_enemy_in_range();
    test_hold_position_cleared_by_other_commands();
    test_patrol_starts_walking_toward_b_first();
    test_patrol_flips_direction_on_arrival();
    test_patrol_opportunistically_engages_enemy_in_range();
    test_patrol_cleared_by_other_commands();
    test_attack_target_clears_on_fresh_move_command();
    test_attack_target_chases_out_of_range_enemy();
    test_attack_target_re_chases_a_fleeing_target();
    test_attack_target_clears_when_target_dies();
    test_attack_target_rejects_own_team();
    test_gary_fires_homing_shot_at_locked_target_in_range();
    test_gary_does_not_deal_flat_melee_damage();
    test_homing_shot_hits_target_that_moves_off_the_original_line();
    test_homing_shot_fizzles_if_target_dies_before_arrival();
    test_dead_hero_respawns_at_graveyard_when_team_owns_no_node();
    test_dead_hero_respawns_at_owned_node_on_wave();
    test_respawn_wave_brings_back_all_dead_heroes_together();
    test_resource_win_condition_replaces_team_wipe();
    test_resource_accumulates_faster_with_more_owned_nodes();
    test_resource_cap_wins_the_match();
    test_sudden_death_does_not_fire_before_max_duration();
    test_sudden_death_picks_team_ahead_on_resources();
    test_sudden_death_tiebreaks_by_nodes_owned();
    test_sudden_death_full_tie_resolves_to_team_zero();
    test_paimon_q_damages_and_roots_in_range();
    test_paimon_q_out_of_range_whiffs();
    test_paimon_w_damages_and_silences_in_range();
    test_paimon_passive_silences_nearest_enemy_periodically();
    test_paimon_r_zone_damages_enemy_and_heals_ally();
    test_cast_flash_slot_set_on_q();
    test_cast_flash_slot_set_on_w_toggle_hero();
    test_cast_flash_slot_set_on_r();
    test_cast_flash_slot_not_set_when_q_blocked_by_cooldown();
    test_cast_flash_slot_not_set_when_w_blocked_by_its_own_cooldown();
    test_noor1_q_damages_and_roots_in_range();
    test_noor1_q_out_of_range_whiffs();
    test_noor1_w_grants_intangibility_and_cooldown();
    test_noor1_passive_silences_nearest_enemy_periodically();
    test_noor1_r_zone_damages_enemy_no_ally_heal();
    test_cain_passive_grants_flat_armor();
    test_cain_q_executes_harder_at_low_hp();
    test_cain_q_out_of_range_whiffs();
    test_cain_w_dashes_away_from_foe_and_cleanses();
    test_cain_r_arms_the_survive_floor();
    test_gunnr_passive_grants_flat_armor();
    test_gunnr_q_damages_in_melee_range();
    test_gunnr_q_out_of_range_whiffs();
    test_gunnr_w_consecration_starts_zone_at_own_feet_on_cooldown();
    test_gunnr_w_consecration_damages_foe_over_time();
    test_gunnr_r_executes_harder_at_low_hp();
    test_gunnr_r_stuns_foe_in_range();
    test_gunnr_r_out_of_range_whiffs_but_still_starts_cooldown();
    test_warrior_q_hard_slash_damages_in_melee_range();
    test_warrior_q_out_of_range_whiffs();
    test_warrior_w_power_slash_hits_harder_than_q();
    test_warrior_r_frostbite_hits_hardest();
    test_warrior_q_then_r_closes_a_real_skillchain();
    test_warrior_skillchain_window_expires();
    test_cart_q_heals_self_capped_at_max();
    test_cart_w_opens_a_zone_at_own_position();
    test_cart_r_zone_is_bigger_and_longer_cooldown_than_w();
    test_cart_zone_triggers_delivery_on_contact_then_deactivates();
    test_cart_zone_can_trigger_on_the_cart_itself();
    test_vassago_passive_regenerates_hp();
    test_vassago_q_damages_and_silences_in_range();
    test_vassago_q_out_of_range_whiffs();
    test_vassago_w_grants_ally_next_cast_refund();
    test_vassago_w_no_ally_in_1v1_whiffs();
    test_vassago_r_zone_silences_but_deals_no_damage();
    test_he_xiangu_passive_regenerates_hp();
    test_he_xiangu_q_spawns_a_homing_orb_and_heals_self_on_cast();
    test_he_xiangu_q_has_no_range_limit();
    test_he_xiangu_w_is_a_free_toggle_regen();
    test_he_xiangu_r_zone_heals_ally_no_enemy_damage();
    test_beleth_passive_grants_flat_armor();
    test_beleth_q_damages_and_burns_foe();
    test_beleth_w_silences_no_damage();
    test_beleth_r_marks_zone_no_immediate_damage();
    test_beleth_r_detonates_after_fuse();
    test_beleth_r_out_of_range_whiffs();
    test_mnm_passive_grants_flat_armor();
    test_mnm_w_burrow_grants_intangible_and_root();
    test_mnm_w_burrow_no_longer_grants_armor();
    test_mnm_w_burrow_respects_cooldown();
    test_mnm_w_burrow_erupts_for_aoe_damage_on_resurface();
    test_mnm_w_burrow_no_eruption_damage_out_of_radius();
    test_mnm_q_damages_and_roots_in_melee_range();
    test_mnm_r_roots_self_and_grants_survive_floor();
    test_mnm_r_survive_floor_actually_blocks_lethal_damage();
    test_gary_q_cast_spawns_projectile_no_instant_damage();
    test_gary_q_out_of_range_whiffs_no_projectile();
    test_gary_q_projectile_lands_after_travel_time();
    test_gary_q_projectile_misses_if_target_steps_off_line();
    test_gary_q_projectile_despawns_after_max_range_unhit();
    test_gary_w_cast_begins_with_hittable_foe_in_range();
    test_gary_w_no_foe_in_range_is_noop();
    test_gary_w_completes_and_deals_damage_after_full_duration();
    test_gary_w_movement_interrupts_cast();
    test_gary_w_damage_does_not_interrupt_cast();
    test_gary_w_silence_interrupts_cast();
    test_tyler_q_cast_spawns_projectile_no_instant_effect();
    test_tyler_q_projectile_roots_and_burns_on_hit();
    test_tyler_q_projectile_misses_if_target_steps_off_line();
    test_tyler_r_spawns_clones_linked_to_caster();
    test_tyler_w_teleports_the_whole_clone_army();
    test_shop_buy_deducts_flow_and_equips_item();
    test_shop_buy_fails_outside_shop_radius();
    test_shop_buy_fails_insufficient_flow();
    test_shop_buy_auto_sells_occupied_slot();
    test_shop_sell_refunds_partial_flow_and_clears_slot();
    test_shop_sell_fails_on_empty_slot();
    test_blink_noop_without_dagger_equipped();
    test_blink_dashes_toward_move_target();
    test_blink_toward_close_move_target_does_not_overshoot();
    test_blink_toward_nearest_foe_when_not_moving();
    test_blink_respects_its_own_cooldown();
    test_blink_blocked_by_stun();
    test_blink_not_blocked_by_silence();
    test_blink_dagger_catalog_entry_costs_1400();
    test_donkey_fold_triggers_below_hp_threshold();
    test_donkey_fold_does_not_trigger_without_item();
    test_donkey_fold_respects_proc_cooldown();
    test_donkey_fold_fights_back_damages_nearest_enemy();
    test_donkey_glide_noop_without_item();
    test_donkey_glide_moves_away_from_nearest_enemy();
    test_donkey_glide_respects_cooldown();
    test_donkey_glide_grants_high_speed();
    test_donkey_glide_flies_over_obstacles();
    test_body_blocking_pushes_a_hero_back_out_of_another();
    test_body_blocking_applies_to_allies_too();
    test_body_blocking_does_not_apply_to_dead_heroes();
    test_body_blocking_skips_donkey_glide_same_as_obstacles();
    test_donkey_catalog_entry_costs_3200();
    test_haste_trinket_catalog_entry();
    test_haste_trinket_reduces_ability_cooldown();
    test_haste_trinket_reduces_auto_attack_cooldown();
    test_haste_trinket_does_not_shrink_windup();
    test_item_catalog_reaches_shop_page_4();
    test_luck_of_the_draw_boosts_in_combat_mp_regen_only();
    test_gae_bolg_true_damage_bypasses_armor();
    test_masamune_lifesteal_heals_attacker();
    test_muramasa_extreme_glass_cannon_catalog_entry();
    test_balance_ring_armor_scales_with_missing_hp();
    test_new_items_catalog_entries();
    test_weatherman_q_knocks_back_no_damage();
    test_weatherman_q_out_of_range_whiffs();
    test_weatherman_w_grounds_airborne_enemy();
    test_weatherman_w_extends_airborne_ally();
    test_weatherman_w_noop_when_nobody_airborne();
    test_weatherman_r_zone_damages_over_time();
    test_zagan_passive_grants_flow_when_enemy_crosses_half_hp();
    test_zagan_passive_does_not_retrigger_the_same_confession();
    test_zagan_q_damages_and_shreds_armor();
    test_zagan_q_armor_shred_expires_after_duration();
    test_zagan_w_stuns_foe_in_range();
    test_zagan_w_no_effect_out_of_range();
    test_zagan_r_mirrors_target_armor();
    test_zagan_r_falls_back_when_target_no_longer_hittable();
    test_item_stats_apply_to_hp_mp_armor_ad_speed();
    test_node_guardian_kill_grants_flow_and_xp();
    test_lane_creep_kill_grants_flow_and_xp();
    test_lane_creep_kill_shares_xp_with_nearby_allies_but_not_far_ones();
    test_hero_kill_grants_flow_xp_kills_and_deaths();
    test_second_kill_within_window_scales_by_fib_and_stacks_to_3x();
    test_multikill_streak_resets_after_window_expires();
    test_dying_resets_own_multikill_streak();
    test_hero_kill_awards_assist_to_recent_damager();
    test_assist_expires_outside_the_tracking_window();
    test_ability_kill_grants_no_flow();
    test_flow_earned_does_not_decrease_on_purchase();
    test_respawn_preserves_economy_and_equipped_items();
    test_tyler_clone_stays_put_without_its_own_command();
    test_tyler_clone_moves_and_fights_on_its_own_independent_command();
    test_arena_owner_controls_self_and_own_clones_only();
    test_tyler_shared_fate_clone_death_kills_tyler_and_siblings();
    test_tyler_death_kills_his_clones_too();
    test_clone_kill_credits_tyler_not_the_clone();
    test_hover_ally_or_nearest_falls_back_when_nothing_hovered();
    test_hover_ally_or_nearest_prefers_hover_target_over_nearest();
    test_hover_ally_or_nearest_falls_back_for_enemy_target();
    test_hover_ally_or_nearest_falls_back_for_dead_target();
    test_set_hover_target_out_of_range_owner_is_a_safe_noop();
    test_doc_wheel_q_heals_hover_target_over_nearest_ally();
    test_fountain_heals_hero_in_radius();
    test_fountain_does_not_heal_hero_outside_radius();
    test_fountain_heals_either_team_neutral();
    test_fountain_caps_healing_at_max_hp();
    test_fountain_does_not_heal_dead_hero();
    test_fountain_restores_mana();
    test_fountain_mana_restore_caps_at_max();
    test_powerup_pickup_grants_buff_and_deactivates();
    test_powerup_out_of_radius_grants_nothing();
    test_powerup_respawns_after_cooldown();
    test_berserker_buff_adds_bonus_damage();
    test_regen_buff_heals_over_time();
    test_powerup_buffs_tick_down_and_expire();
    test_mana_regenerates_out_of_combat();
    test_mana_regenerates_slowly_in_combat();
    test_mana_regenerates_faster_out_of_combat_than_in_combat();
    test_mana_regen_accumulates_correctly_across_many_small_ticks();
    test_taking_damage_rearms_the_combat_timer();
    test_combat_timer_counts_down_to_zero();
    test_team_creep_does_not_attack_own_owning_team();
    test_team_creep_still_attacks_opposing_team();
    test_neutral_creep_still_attacks_anyone();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
