#ifndef ARENA_REPLAY_H
#define ARENA_REPLAY_H

#include <stdint.h>
#include "arena_game.h"

/* Observer mode, arena half (EMILY/BACKLOG.md S170-30, NORTHSTAR §12 Phase
 * C). Reads the JSONL match log Phase B (S170-29) already writes and drives
 * the exact same ArenaState the live renderer draws from -- "same draw
 * code, no second rendering path" per the founder's own requirement.
 *
 * Deliberately a fixed-format sscanf parser, not a general JSON parser:
 * these files are produced by exactly one writer (arena_log_* in
 * apps/arena/src/main.c) whose format is known and stable, matching the
 * same "no general parser for controlled, self-produced data" spirit as
 * packages/common/http_client.h's http_extract_json_string_field. */

#define ARENA_REPLAY_MAX_SNAPSHOTS 4096

typedef struct {
    uint32_t ts_ms;
    float player_x, player_z;
    int player_hp;
    float bot_x, bot_z;
    int bot_hp;
} ArenaReplaySnapshot;

typedef struct {
    ArenaReplaySnapshot snapshots[ARENA_REPLAY_MAX_SNAPSHOTS];
    int count;
    int has_winner;
    int winner; /* 0 = none, 1 = player, 2 = bot -- matches ArenaState.winner */
    uint32_t winner_ts_ms;
} ArenaReplay;

/* arena_replay_load parses path (a var/matches/arena-*.jsonl file) into
 * out. Returns 1 on success (even a log with zero snapshots is "success" --
 * an empty/aborted match is valid data, not a parse error), 0 if the file
 * could not be opened at all. Unrecognized or malformed lines are skipped,
 * not fatal -- a replay tool should be tolerant of a partially-written log
 * from a match that was still in progress when read (live-tailing use
 * case), not just a fully completed one. */
int arena_replay_load(const char *path, ArenaReplay *out);

/* arena_replay_apply_at drives state's two heroes' x/z/hp directly from
 * replay's snapshots at elapsed replay-time ts_ms, linearly interpolating
 * between the two bracketing snapshots (or holding the nearest one at the
 * ends) so playback looks smooth despite the 500ms logging interval. Sets
 * state->winner once ts_ms reaches the logged match_end time, same
 * semantics as the live sim. Does not touch ability/cooldown fields --
 * observer mode shows position/HP/outcome, not full kit state, this pass. */
void arena_replay_apply_at(const ArenaReplay *replay, uint32_t ts_ms, ArenaState *state);

#endif
