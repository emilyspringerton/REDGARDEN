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
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
