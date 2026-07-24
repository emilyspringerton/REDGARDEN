/* tests/test_arena_replay.c — headless test for packages/simulation/
 * arena_replay.c (NORTHSTAR §12 Phase C, EMILY/BACKLOG.md S170-30). Writes
 * a fixture JSONL file in the exact shape apps/arena/src/main.c's
 * arena_log_* functions produce, then verifies the parser + playback
 * driver reproduce it correctly. No SDL/GL dependency, same reasoning as
 * tests/test_arena_game.c: this box has no display, but the replay logic
 * underneath the (unrunnable-here) windowed observer mode has zero GL
 * dependency and is fully testable without one. */
#include <stdio.h>
#include <string.h>

#include "../packages/simulation/arena_game.h"
#include "../packages/simulation/arena_replay.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static const char *FIXTURE_PATH = "/tmp/test_arena_replay_fixture.jsonl";

static void write_fixture(void) {
    FILE *f = fopen(FIXTURE_PATH, "w");
    fprintf(f, "{\"event\":\"match_start\",\"ts_ms\":0}\n");
    fprintf(f, "{\"event\":\"snapshot\",\"ts_ms\":500,\"player\":{\"id\":\"local_player\",\"x\":0.00,\"z\":0.00,\"hp\":100},\"bot\":{\"id\":\"local_bot\",\"x\":10.00,\"z\":0.00,\"hp\":100}}\n");
    fprintf(f, "{\"event\":\"ability_cast\",\"player_id\":\"local_player\",\"ability\":\"Q\",\"ts_ms\":600}\n");
    fprintf(f, "{\"event\":\"snapshot\",\"ts_ms\":1000,\"player\":{\"id\":\"local_player\",\"x\":4.00,\"z\":0.00,\"hp\":100},\"bot\":{\"id\":\"local_bot\",\"x\":8.00,\"z\":0.00,\"hp\":88}}\n");
    fprintf(f, "{\"event\":\"match_end\",\"winner\":\"local_player\",\"ts_ms\":1200}\n");
    fclose(f);
}

static void test_load_missing_file_fails_closed(void) {
    ArenaReplay replay;
    int ok = arena_replay_load("/tmp/does-not-exist-arena-replay.jsonl", &replay);
    CHECK(ok == 0, "loading a nonexistent file returns 0, not a crash or garbage data");
}

static void test_load_parses_snapshots_and_winner(void) {
    write_fixture();
    ArenaReplay replay;
    int ok = arena_replay_load(FIXTURE_PATH, &replay);
    CHECK(ok == 1, "loading the fixture file succeeds");
    CHECK(replay.count == 2, "exactly the 2 snapshot lines are parsed, match_start/ability_cast/match_end skipped");
    CHECK(replay.snapshots[0].ts_ms == 500, "first snapshot ts_ms parsed correctly");
    CHECK(replay.snapshots[1].bot_hp == 88, "second snapshot's bot hp parsed correctly");
    CHECK(replay.has_winner == 1, "match_end line sets has_winner");
    CHECK(replay.winner == 1, "winner string \"local_player\" maps to winner=1 (matches ArenaState convention)");
    CHECK(replay.winner_ts_ms == 1200, "match_end ts_ms parsed correctly");
}

static void test_apply_before_first_snapshot_holds_first(void) {
    ArenaReplay replay;
    arena_replay_load(FIXTURE_PATH, &replay);
    ArenaState state;
    arena_init();
    state = arena_state;
    arena_replay_apply_at(&replay, 0, &state);
    CHECK(state.heroes[0].x == 0.0f && state.heroes[1].x == 10.0f,
          "ts_ms before the first snapshot holds the first snapshot's values");
}

static void test_apply_interpolates_between_snapshots(void) {
    ArenaReplay replay;
    arena_replay_load(FIXTURE_PATH, &replay);
    ArenaState state;
    arena_init();
    state = arena_state;
    /* Halfway between ts_ms=500 (player x=0) and ts_ms=1000 (player x=4). */
    arena_replay_apply_at(&replay, 750, &state);
    CHECK(state.heroes[0].x > 1.5f && state.heroes[0].x < 2.5f,
          "position at the midpoint between two snapshots is linearly interpolated, not snapped");
}

static void test_apply_after_last_snapshot_holds_last(void) {
    ArenaReplay replay;
    arena_replay_load(FIXTURE_PATH, &replay);
    ArenaState state;
    arena_init();
    state = arena_state;
    arena_replay_apply_at(&replay, 999999, &state);
    CHECK(state.heroes[0].x == 4.0f && state.heroes[1].hp == 88,
          "ts_ms past the last snapshot holds the last snapshot's values");
}

static void test_apply_sets_winner_at_match_end_time(void) {
    ArenaReplay replay;
    arena_replay_load(FIXTURE_PATH, &replay);
    ArenaState state;
    arena_init();
    state = arena_state;
    arena_replay_apply_at(&replay, 1000, &state);
    CHECK(state.winner == 0, "winner stays unset before the logged match_end time");
    arena_replay_apply_at(&replay, 1200, &state);
    CHECK(state.winner == 1, "winner is set once replay time reaches the logged match_end time");
}

int main(void) {
    printf("RED GARDEN arena_replay headless smoke test\n\n");
    test_load_missing_file_fails_closed();
    test_load_parses_snapshots_and_winner();
    test_apply_before_first_snapshot_holds_first();
    test_apply_interpolates_between_snapshots();
    test_apply_after_last_snapshot_holds_last();
    test_apply_sets_winner_at_match_end_time();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
