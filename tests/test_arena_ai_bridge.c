/* tests/test_arena_ai_bridge.c — headless test for packages/simulation/
 * arena_ai_bridge.c (NORTHSTAR §12 Phase E, Milestone-6 equivalent,
 * EMILY/BACKLOG.md S170-36). No SDL/GL dependency, same reasoning as the
 * other arena test files. */
#include <stdio.h>
#include <string.h>

#include "../packages/simulation/arena_game.h"
#include "../packages/simulation/arena_ai_bridge.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_hero_name_covers_full_roster(void) {
    CHECK(strcmp(arena_hero_name(ARENA_HERO_UNICORN), "unicorn") == 0, "Unicorn's name token");
    CHECK(strcmp(arena_hero_name(ARENA_HERO_DUCK), "duck") == 0, "Duck's name token");
    CHECK(strcmp(arena_hero_name(ARENA_HERO_GHOST), "ghost") == 0, "Ghost's name token");
    CHECK(strcmp(arena_hero_name(ARENA_HERO_FROG), "frog") == 0, "Frog's name token");
    CHECK(strcmp(arena_hero_name((ArenaHeroID)99), "unknown") == 0,
          "an out-of-range hero_id returns \"unknown\", not garbage or a crash");
}

static void test_serialize_is_stable_for_a_fixed_state(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    arena_state.heroes[0].x = -6.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].hp = 80;
    arena_state.heroes[1].x = 4.0f; arena_state.heroes[1].z = 2.0f;
    arena_state.heroes[1].hp = 60;

    char a[512], b[512];
    arena_serialize_state(0, 1234, a, sizeof(a));
    arena_serialize_state(0, 1234, b, sizeof(b));

    CHECK(strcmp(a, b) == 0, "serializing the same fixed state twice produces an identical string");
    CHECK(strstr(a, "tick:1234") != NULL, "serialized state includes the tick");
    CHECK(strstr(a, "self hero:unicorn") != NULL, "self section names the correct hero");
    CHECK(strstr(a, "hp:80") != NULL, "self section includes current HP");
    CHECK(strstr(a, "foe hero:duck") != NULL, "foe section names the correct hero");
    CHECK(strstr(a, "hp:60") != NULL, "foe section includes current HP");
}

static void test_serialize_self_and_foe_swap_by_owner(void) {
    arena_init_with_heroes(ARENA_HERO_GHOST, ARENA_HERO_FROG);
    char from_owner0[512], from_owner1[512];
    arena_serialize_state(0, 0, from_owner0, sizeof(from_owner0));
    arena_serialize_state(1, 0, from_owner1, sizeof(from_owner1));

    CHECK(strstr(from_owner0, "self hero:ghost") != NULL, "owner 0's view: self is Ghost");
    CHECK(strstr(from_owner0, "foe hero:frog") != NULL, "owner 0's view: foe is Frog");
    CHECK(strstr(from_owner1, "self hero:frog") != NULL, "owner 1's view: self is Frog");
    CHECK(strstr(from_owner1, "foe hero:ghost") != NULL, "owner 1's view: foe is Ghost");
}

static void test_serialize_invalid_owner_writes_empty_string(void) {
    arena_init();
    char buf[64] = "not empty to start";
    arena_serialize_state(5, 0, buf, sizeof(buf));
    CHECK(buf[0] == '\0', "an out-of-range owner writes an empty string, not garbage");
}

static void test_decode_full_action_string(void) {
    ArenaAction act;
    int ok = arena_decode_action("move:4.20,1.00 cast_q:1 cast_w:0 cast_r:1", &act);
    CHECK(ok == 1, "a well-formed action string decodes successfully");
    CHECK(act.has_move == 1, "has_move is set when a move: token is present");
    CHECK(act.move_x > 4.19f && act.move_x < 4.21f, "move_x parsed correctly");
    CHECK(act.move_z > 0.99f && act.move_z < 1.01f, "move_z parsed correctly");
    CHECK(act.cast_q == 1, "cast_q parsed correctly");
    CHECK(act.cast_w == 0, "cast_w parsed correctly");
    CHECK(act.cast_r == 1, "cast_r parsed correctly");
}

static void test_decode_partial_action_defaults_safely(void) {
    ArenaAction act;
    int ok = arena_decode_action("cast_q:1", &act);
    CHECK(ok == 1, "a partial action string (only cast_q) still decodes as found");
    CHECK(act.has_move == 0, "no move: token means has_move stays 0, not garbage coordinates");
    CHECK(act.cast_q == 1, "the one token that was present still parses correctly");
    CHECK(act.cast_w == 0 && act.cast_r == 0, "missing cast tokens default to 0, a safe no-op");
}

static void test_decode_garbage_fails_closed(void) {
    ArenaAction act;
    CHECK(arena_decode_action("this is not a valid action string at all", &act) == 0,
          "a string with none of the recognized tokens returns 0 (do nothing), not a crash");
    CHECK(arena_decode_action(NULL, &act) == 0, "a NULL action string returns 0, not a crash");
    CHECK(arena_decode_action("", &act) == 0, "an empty action string returns 0");
}

int main(void) {
    printf("RED GARDEN arena_ai_bridge headless smoke test\n\n");
    test_hero_name_covers_full_roster();
    test_serialize_is_stable_for_a_fixed_state();
    test_serialize_self_and_foe_swap_by_owner();
    test_serialize_invalid_owner_writes_empty_string();
    test_decode_full_action_string();
    test_decode_partial_action_defaults_safely();
    test_decode_garbage_fails_closed();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
