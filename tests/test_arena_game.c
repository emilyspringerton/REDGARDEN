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

int main(void) {
    printf("RED GARDEN arena_game headless smoke test\n\n");
    test_movement_reaches_target();
    test_bounds_clamped();
    test_combat_and_win_condition();
    test_bot_steers_toward_player();
    test_click_near_enemy_becomes_attack_move();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
