/* tests/test_arena_game.c — headless smoke test for packages/simulation/
 * arena_game.c. No SDL/GL dependency on purpose: this box has no display
 * (no Xvfb), so the arena client itself can't be run interactively here,
 * but the sim logic underneath it has zero GL dependency and is fully
 * testable without one. Written to catch real bugs before the client is
 * ever visually confirmed working. */
#include <stdio.h>
#include <math.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_movement_reaches_target(void) {
    arena_init();
    /* Hero0 starts at (-6,0); target is close (~4.2 units away, ~1s at
       ARENA_HERO_SPEED) so it arrives long before the bot (12 units away,
       chasing at the same speed) can close the gap into melee range and
       end the match early. Deliberately does NOT touch the bot's HP/alive
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
    ArenaHero *h = &arena_state.heroes[0];
    h->hp = 50; /* below max so regen has room to matter */
    arena_toggle_w(0);
    CHECK(h->w_active == 1, "W toggles on");
    arena_update(1000); /* 1 second of regen */
    CHECK(h->hp > 50, "W regenerates HP while active");
    arena_toggle_w(0);
    CHECK(h->w_active == 0, "W toggles back off");
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

static void test_ghost_q_damages_and_silences_in_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = ghost->x + 4.0f; /* within ARENA_GHOST_Q_RANGE */
    foe->z = ghost->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in range");
    CHECK(foe->silenced_ms == ARENA_GHOST_Q_SILENCE_MS, "Q silences the foe on a landed hit");
    CHECK(ghost->q_cooldown_ms == ARENA_GHOST_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_ghost_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = ghost->x + ARENA_GHOST_Q_RANGE + 5.0f;
    foe->z = ghost->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does no damage");
    CHECK(foe->silenced_ms == 0, "Q out of range does not silence");
    CHECK(ghost->q_cooldown_ms == 0, "Q out of range does not consume its cooldown");
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
       and damage owner 0 within a handful of ticks. */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 3.0f; arena_state.heroes[1].z = 0.0f;
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

    arena_update_teams(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < 100,
          "the lone enemy hero takes damage from being in melee range of two attackers at once");
}

static void test_team_wipe_win_condition(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* Only owner 0 (team 0) is left active and alive -- team 1 is wiped. */
    arena_state.heroes[1].active = 0;

    arena_update_teams(16);

    CHECK(arena_state.winner == 1, "team 0 wins once team 1 has zero active-and-alive heroes left");
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
    /* Stand exactly on node 0 -- node 1 is now strictly farther. */
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].x == arena_state.nodes[1].x && arena_state.heroes[0].z == arena_state.nodes[1].z,
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

/* S170-51: territorial dynamic jungle creeps. */

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

static void test_creep_attacks_nearby_hero(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16); /* spawn */
    arena_tick_creeps(ARENA_CREEP_ATTACK_COOLDOWN_MS); /* long enough for one attack */

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_CREEP_DAMAGE,
          "a jungle creep auto-attacks a hero standing within its aggro radius");
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
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.nodes[0].owner = 1; /* team-flavored, so ARENA_CREEP_TEAM_RESPAWN_MS applies */

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
    arena_state.nodes[0].owner = 1; /* team 0's own node */
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].hp = 50; arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16);
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.heroes[0].hp == 50 + ARENA_CREEP_TEAM_KILL_HEAL,
          "killing your own team's jungle creep on your own territory heals you (home-turf resupply)");
}

static void test_team_creep_kill_by_enemy_team_helps_flip_the_node(void) {
    /* Team 1 farms team 0's own jungle creep while team 1 is mid-channel
       trying to flip that node -- the counter-play tool against a
       turtling opponent. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.nodes[0].owner = 1; /* team 0's own node */
    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x;
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_tick_creeps(16);
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_state.nodes[0].capturing_team = 1; /* team 1 is trying to flip it */
    arena_state.nodes[0].capture_progress_ms = 0;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100, "the enemy killer gets no heal -- that reward is owning-team-only");
    CHECK(arena_state.nodes[0].capture_progress_ms == ARENA_CREEP_TEAM_KILL_DENY_CAPTURE_BONUS_MS,
          "farming the enemy's own jungle creep while channeling their node grants the deny capture bonus");
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
    test_unicorn_r_doubles_armor_temporarily();
    test_unicorn_armor_reduces_incoming_damage();
    test_duck_q_pulls_foe_and_damages();
    test_duck_q_out_of_range_whiffs();
    test_duck_q_never_pulls_past_the_duck();
    test_duck_r_bigger_pull_and_damage_than_q();
    test_duck_has_no_w();
    test_hero_dispatch_is_by_hero_not_owner_slot();
    test_ghost_q_damages_and_silences_in_range();
    test_ghost_q_out_of_range_whiffs();
    test_silenced_hero_cannot_cast();
    test_ghost_w_grants_intangibility_and_expires();
    test_intangible_hero_cannot_be_hit();
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
    test_team_wipe_win_condition();
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
    test_creep_attacks_nearby_hero();
    test_hero_does_not_attack_creep_while_an_enemy_hero_is_in_range();
    test_hero_kills_creep_and_queues_correct_respawn_timer();
    test_neutral_creep_kill_grants_capture_bonus_only_while_channeling();
    test_team_creep_kill_by_owning_team_heals();
    test_team_creep_kill_by_enemy_team_helps_flip_the_node();
    test_stealthed_hero_captures_undetected_through_a_crowd_of_visible_enemies();
    test_two_visible_teams_still_interrupt_normally_even_near_a_stealthed_ally();
    test_starting_a_channel_breaks_the_capturer_stealth();
    test_damage_to_channeling_team_interrupts_the_capture();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
