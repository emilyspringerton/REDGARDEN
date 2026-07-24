#ifndef ARENA_AI_BRIDGE_H
#define ARENA_AI_BRIDGE_H

#include <stddef.h>
#include "arena_game.h"

/* Game AI bridge, Milestone-6 equivalent (EMILY/BACKLOG.md S170-36,
 * NORTHSTAR §12 Phase E). Extends gpt2-alpine-c/docs/GAME_AI_NORTHSTAR.md's
 * state-serializer/action-decoder pattern to arena's own state, rather than
 * inventing a REDGARDEN-specific format from scratch -- same natural-
 * language token style as that doc's SHANKPIT/BedWars examples, just a new
 * vocabulary for arena's hero/ability state.
 *
 * This is the contract milestone only: serialize state, decode an action
 * string back into arena's control primitives. It does not call the GPT-2
 * inference server (:8088) -- wiring live inference in, and the fine-tune/
 * self-play loop behind it, are later, separate slices (that pipeline needs
 * an external Colab GPU run and a human to trigger it, not buildable
 * end-to-end in this environment). */

/* arena_hero_name returns a short lowercase token name for hero_id
 * ("unicorn", "duck", "ghost", "frog"), matching the vocabulary style
 * GAME_AI_NORTHSTAR.md uses for its own domain tokens (weapon names, etc).
 * Returns "unknown" for an out-of-range value rather than a garbage
 * pointer or crash. */
const char *arena_hero_name(ArenaHeroID hero_id);

/* arena_serialize_state writes a stable, natural-language state token
 * string for the match as seen from owner's point of view ("self" =
 * owner's hero, "foe" = the other) into out (NUL-terminated, truncated to
 * out_len-1 if needed). owner must be 0 or 1; invalid input writes an
 * empty string rather than garbage. Same input always produces the same
 * output (no timestamps/randomness beyond the tick counter itself). */
void arena_serialize_state(int owner, unsigned int tick_ms, char *out, size_t out_len);

typedef struct {
    float move_x, move_z;
    int has_move; /* 1 if the action string included a move: token */
    int cast_q, cast_w, cast_r;
} ArenaAction;

/* arena_decode_action parses a GAME_AI_NORTHSTAR.md-style action token
 * string (e.g. "move:4.20,1.00 cast_q:1 cast_w:0 cast_r:0") into out.
 * Missing fields default to "no move, no casts" rather than garbage --
 * a policy that only emits "cast_q:1" still gets a safe, valid action.
 * Returns 1 if at least one recognized token was found, 0 if the string
 * had nothing usable in it at all (fails closed: caller should treat that
 * as "do nothing this tick," not retry with garbage). */
int arena_decode_action(const char *action_str, ArenaAction *out);

#endif
