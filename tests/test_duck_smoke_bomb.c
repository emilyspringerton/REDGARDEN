/* tests/test_duck_smoke_bomb.c -- headless smoke test for Duck's Smoke Bomb (W)
 * (packages/simulation/arena_game.c, S202-10, 2026-08-25). Same "no SDL/GL dependency"
 * reasoning as test_arena_game.c's own header comment, same file-per-mod precedent
 * test_bloodflower.c/test_tree_passive.c/test_build_templates.c/test_item_curriculum.c
 * already set.
 *
 * Founder real-time: "ok do fog of war as an ability" -> "add to the duck" -> "there is no
 * natural fog of war just duck smoke bomb" -> "server authorittive" -> "as a parena mod" ->
 * "mod first dev."
 *
 * Same "live round-trip tested, not just compile-checked" bar test_bloodflower.c/
 * test_tree_passive.c already set for a PARENA mod: this actually calls arena_toggle_w and
 * asserts the cast lands through the real compiled mod (on_duck_smoke_bomb_cast ->
 * redgarden_host_duck_smoke_bomb_cast), not a direct call to the host function. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_duck_w_cast_via_real_parena_mod(void) {
    arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_UNICORN);
    ArenaHero *duck = &arena_state.heroes[0];
    duck->x = 12.0f;
    duck->z = -4.0f;
    duck->mp = 999;

    CHECK(duck->duck_smoke_ms == 0, "setup: no cloud active before casting");
    arena_toggle_w(0);

    CHECK(duck->duck_smoke_ms == ARENA_DUCK_W_DURATION_MS,
          "casting W through the real compiled PARENA mod sets duck_smoke_ms to the full duration");
    CHECK(duck->duck_smoke_x == 12.0f && duck->duck_smoke_z == -4.0f,
          "the cloud is centered on the Duck's own position at cast time");
    CHECK(duck->w_cooldown_ms == ARENA_DUCK_W_COOLDOWN_MS, "cooldown is spent on cast");
}

static void test_duck_w_gated_by_cooldown(void) {
    arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_UNICORN);
    ArenaHero *duck = &arena_state.heroes[0];
    duck->mp = 999;
    duck->w_cooldown_ms = 5000; /* still on cooldown */

    arena_toggle_w(0);

    CHECK(duck->duck_smoke_ms == 0, "a cast blocked by cooldown never sets duck_smoke_ms");
}

static void test_duck_w_gated_by_mana(void) {
    arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_UNICORN);
    ArenaHero *duck = &arena_state.heroes[0];
    duck->mp = 0;

    arena_toggle_w(0);

    CHECK(duck->duck_smoke_ms == 0, "a cast blocked by insufficient mana never sets duck_smoke_ms");
}

static void test_duck_w_only_fires_for_duck(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *unicorn = &arena_state.heroes[0];
    unicorn->mp = 999;

    arena_toggle_w(0); /* Unicorn's own W is a free toggle (w_active), not Smoke Bomb */

    CHECK(unicorn->duck_smoke_ms == 0, "a non-Duck hero's W never sets duck_smoke_ms");
}

/* hero_obscured_from is static to arena_game.c -- exercised indirectly through
 * arena_nearest_enemy, its one real caller, same "test the real entry point, not the
 * mechanic directly" discipline test_tree_passive.c's own header comment establishes. */
static void test_smoke_cloud_blocks_targeting_from_outside(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *viewer = &arena_state.heroes[0]; /* team 0, outside the cloud */
    ArenaHero *duck = &arena_state.heroes[1];   /* team 1, will stand inside its own cloud */
    viewer->x = 0.0f; viewer->z = 0.0f;
    duck->x = 20.0f; duck->z = 20.0f; /* well outside ARENA_DUCK_W_RADIUS of (0,0) */
    duck->mp = 999;

    CHECK(arena_nearest_enemy(0) == duck, "setup: viewer can target the Duck before any cloud exists");

    arena_toggle_w(1); /* Duck casts Smoke Bomb centered on its own (20,20) position */

    CHECK(arena_nearest_enemy(0) == NULL,
          "a hero standing inside its own active smoke cloud can't be targeted by a viewer outside it");
}

static void test_smoke_cloud_does_not_block_a_viewer_also_inside(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    ArenaHero *duck = &arena_state.heroes[0]; /* team 0 */
    ArenaHero *foe = &arena_state.heroes[ARENA_TEAM_SIZE]; /* team 1, will walk into the same cloud */
    duck->hero_id = ARENA_HERO_DUCK;
    duck->x = 5.0f; duck->z = 5.0f;
    duck->mp = 999;
    foe->x = 5.0f; foe->z = 5.0f; /* standing right on top of Duck -- well inside ARENA_DUCK_W_RADIUS */

    arena_toggle_w(0); /* cloud now centered on (5,5), both heroes inside it */

    CHECK(arena_nearest_enemy(ARENA_TEAM_SIZE) == duck,
          "a viewer standing INSIDE the same cloud as the target can still see/target it -- smoke blocks the outside looking in, not everyone inside from each other");
}

static void test_smoke_cloud_expires_and_targeting_resumes(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *viewer = &arena_state.heroes[0];
    ArenaHero *duck = &arena_state.heroes[1];
    viewer->x = 0.0f; viewer->z = 0.0f;
    duck->x = 20.0f; duck->z = 20.0f;
    duck->mp = 999;

    arena_toggle_w(1);
    CHECK(arena_nearest_enemy(0) == NULL, "setup: obscured while the cloud is active");

    duck->duck_smoke_ms = 1; /* about to expire */
    arena_update(2); /* real top-level 1v1 tick -- decrements past 0, not a direct field write */

    CHECK(duck->duck_smoke_ms == 0, "arena_update() (the 1v1 tick) decrements duck_smoke_ms toward 0");
    CHECK(arena_nearest_enemy(0) == duck, "once the cloud expires, targeting resumes normally");
}

/* Real bug class this session already found and fixed once for Tree's own passive (S202-23):
 * arena_update (1v1) and arena_update_teams (team mode) are two SEPARATE top-level tick
 * functions, and a mechanic wired into only one of them silently never fires in the other.
 * duck_smoke_ms is decremented inside tick_hero_kit specifically because that's the one
 * function BOTH arena_update and arena_update_teams already call -- this test proves that
 * choice actually holds, through the real team-mode entry point, not assumed by inspection. */
static void test_duck_smoke_decrements_through_real_team_mode_arena_update_teams(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    ArenaHero *duck = &arena_state.heroes[0];
    duck->hero_id = ARENA_HERO_DUCK;
    duck->duck_smoke_ms = 1;
    duck->duck_smoke_x = duck->x;
    duck->duck_smoke_z = duck->z;

    arena_update_teams(2); /* the REAL team-mode tick function -- not a direct field write */

    CHECK(duck->duck_smoke_ms == 0,
          "arena_update_teams() (the team-mode tick) also decrements duck_smoke_ms, same as arena_update() does");
}

/* S205-87: "duck smoke bomb should have a 50% chance to slow each enemy hit by it." An enemy
 * standing inside the cloud radius at cast time is the "hit" this vision-only ability has. */
static void test_duck_w_can_slow_an_enemy_caught_in_the_cloud(void) {
    arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_UNICORN);
    ArenaHero *duck = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    duck->x = 10.0f; duck->z = 10.0f;
    duck->mp = 999;
    foe->x = 10.0f; foe->z = 10.0f; /* standing right on top of Duck -- well inside ARENA_DUCK_W_RADIUS */

    /* redgarden_host_duck_smoke_bomb_cast rolls rand() % 100 per enemy caught in the cloud --
     * seed + retry a bounded number of times rather than assert a specific rand() sequence
     * (which isn't a portable guarantee across libc implementations); real assertion is just
     * that the slow CAN actually land through the real cast path, not a specific roll value. */
    int landed = 0;
    for (int seed = 0; seed < 200 && !landed; seed++) {
        foe->slowed_ms = 0;
        foe->slow_pct = 0.0f;
        srand((unsigned)seed);
        redgarden_host_duck_smoke_bomb_cast(0);
        if (foe->slowed_ms > 0) landed = 1;
    }
    CHECK(landed, "an enemy caught in the cloud can be slowed by the real cast path (found within 200 seeds)");
    CHECK(foe->slow_pct == ARENA_DUCK_W_SLOW_PCT, "the applied slow uses the real Smoke Bomb slow pct");
    CHECK(foe->slowed_ms == ARENA_DUCK_W_SLOW_MS, "the applied slow uses the real Smoke Bomb slow duration");
}

static void test_duck_w_slow_chance_is_roughly_fifty_percent(void) {
    const int trials = 1000;
    int slowed_count = 0;
    srand(42); /* fixed seed for a reproducible, non-flaky statistical check */
    for (int i = 0; i < trials; i++) {
        arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_UNICORN);
        ArenaHero *duck = &arena_state.heroes[0];
        ArenaHero *foe = &arena_state.heroes[1];
        duck->x = 0.0f; duck->z = 0.0f;
        duck->mp = 999;
        foe->x = 0.0f; foe->z = 0.0f;
        redgarden_host_duck_smoke_bomb_cast(0);
        if (foe->slowed_ms > 0) slowed_count++;
    }
    /* Wide band (30-70%) on purpose -- this proves the roll is genuinely ~50/50, not that it's
     * exactly 50.0%, avoiding a flaky test over normal binomial variance at n=1000. */
    CHECK(slowed_count > trials * 30 / 100 && slowed_count < trials * 70 / 100,
          "the slow lands on roughly 50% of enemies hit across many casts, not ~0% or ~100%");
}

static void test_duck_w_slow_does_not_hit_an_enemy_outside_the_cloud(void) {
    arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_UNICORN);
    ArenaHero *duck = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    duck->x = 0.0f; duck->z = 0.0f;
    duck->mp = 999;
    foe->x = 50.0f; foe->z = 50.0f; /* well outside ARENA_DUCK_W_RADIUS */

    srand(1);
    redgarden_host_duck_smoke_bomb_cast(0);

    CHECK(foe->slowed_ms == 0, "an enemy standing outside the cloud radius is never slowed, regardless of the roll");
}

static void test_duck_w_slow_does_not_hit_an_ally(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaHero *duck = &arena_state.heroes[0];
    ArenaHero *ally = &arena_state.heroes[1]; /* same team as Duck */
    duck->hero_id = ARENA_HERO_DUCK;
    duck->x = 0.0f; duck->z = 0.0f;
    duck->mp = 999;
    ally->x = 0.0f; ally->z = 0.0f; /* right on top of Duck, but same team */

    srand(1);
    redgarden_host_duck_smoke_bomb_cast(0);

    CHECK(ally->slowed_ms == 0, "a teammate standing in Duck's own cloud is never slowed -- only enemies");
}

int main(void) {
    test_duck_w_cast_via_real_parena_mod();
    test_duck_w_gated_by_cooldown();
    test_duck_w_gated_by_mana();
    test_duck_w_only_fires_for_duck();
    test_smoke_cloud_blocks_targeting_from_outside();
    test_smoke_cloud_does_not_block_a_viewer_also_inside();
    test_smoke_cloud_expires_and_targeting_resumes();
    test_duck_smoke_decrements_through_real_team_mode_arena_update_teams();
    test_duck_w_can_slow_an_enemy_caught_in_the_cloud();
    test_duck_w_slow_chance_is_roughly_fifty_percent();
    test_duck_w_slow_does_not_hit_an_enemy_outside_the_cloud();
    test_duck_w_slow_does_not_hit_an_ally();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
