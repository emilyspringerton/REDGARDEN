#include "arena_replay.h"

#include <stdio.h>
#include <string.h>

int arena_replay_load(const char *path, ArenaReplay *out) {
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        uint32_t ts_ms;
        float px, pz, bx, bz;
        int php, bhp;

        if (sscanf(line,
                   "{\"event\":\"snapshot\",\"ts_ms\":%u,"
                   "\"player\":{\"id\":\"local_player\",\"x\":%f,\"z\":%f,\"hp\":%d},"
                   "\"bot\":{\"id\":\"local_bot\",\"x\":%f,\"z\":%f,\"hp\":%d}}",
                   &ts_ms, &px, &pz, &php, &bx, &bz, &bhp) == 7) {
            if (out->count < ARENA_REPLAY_MAX_SNAPSHOTS) {
                ArenaReplaySnapshot *s = &out->snapshots[out->count++];
                s->ts_ms = ts_ms;
                s->player_x = px; s->player_z = pz; s->player_hp = php;
                s->bot_x = bx; s->bot_z = bz; s->bot_hp = bhp;
            }
            continue;
        }

        char winner_buf[32];
        if (sscanf(line, "{\"event\":\"match_end\",\"winner\":\"%31[^\"]\",\"ts_ms\":%u}",
                   winner_buf, &ts_ms) == 2) {
            out->has_winner = 1;
            out->winner = (strcmp(winner_buf, "local_player") == 0) ? 1 : 2;
            out->winner_ts_ms = ts_ms;
            continue;
        }
        /* match_start / ability_cast / anything else: not needed to drive
         * hero position/HP/outcome, skipped rather than parsed. */
    }

    fclose(f);
    return 1;
}

/* lerp is a plain float lerp -- named locally rather than pulled from
 * packages/common/mat4.h to keep this file's only dependency arena_game.h,
 * matching the "minimal, self-contained" convention used elsewhere in this
 * codebase (hmac_sha256.h, http_client.h). */
static float arena_replay_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

void arena_replay_apply_at(const ArenaReplay *replay, uint32_t ts_ms, ArenaState *state) {
    if (replay->count == 0) return;

    if (ts_ms <= replay->snapshots[0].ts_ms) {
        const ArenaReplaySnapshot *s = &replay->snapshots[0];
        state->heroes[0].x = s->player_x; state->heroes[0].z = s->player_z; state->heroes[0].hp = s->player_hp;
        state->heroes[1].x = s->bot_x;    state->heroes[1].z = s->bot_z;    state->heroes[1].hp = s->bot_hp;
    } else if (ts_ms >= replay->snapshots[replay->count - 1].ts_ms) {
        const ArenaReplaySnapshot *s = &replay->snapshots[replay->count - 1];
        state->heroes[0].x = s->player_x; state->heroes[0].z = s->player_z; state->heroes[0].hp = s->player_hp;
        state->heroes[1].x = s->bot_x;    state->heroes[1].z = s->bot_z;    state->heroes[1].hp = s->bot_hp;
    } else {
        int i = 0;
        while (i + 1 < replay->count && replay->snapshots[i + 1].ts_ms < ts_ms) i++;
        const ArenaReplaySnapshot *a = &replay->snapshots[i];
        const ArenaReplaySnapshot *b = &replay->snapshots[i + 1];
        uint32_t span = b->ts_ms - a->ts_ms;
        float t = (span == 0) ? 0.0f : (float)(ts_ms - a->ts_ms) / (float)span;

        state->heroes[0].x = arena_replay_lerp(a->player_x, b->player_x, t);
        state->heroes[0].z = arena_replay_lerp(a->player_z, b->player_z, t);
        state->heroes[0].hp = (t < 0.5f) ? a->player_hp : b->player_hp;

        state->heroes[1].x = arena_replay_lerp(a->bot_x, b->bot_x, t);
        state->heroes[1].z = arena_replay_lerp(a->bot_z, b->bot_z, t);
        state->heroes[1].hp = (t < 0.5f) ? a->bot_hp : b->bot_hp;
    }

    if (replay->has_winner && ts_ms >= replay->winner_ts_ms) {
        state->winner = replay->winner;
    }
}
