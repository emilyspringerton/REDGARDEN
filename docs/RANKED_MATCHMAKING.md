# RED GARDEN — Ranked Matchmaking (Design Pass)

`status: design only, not implemented` — S170-14 (3/3): "ranked pool — competitive matchmaking,
no design done yet (rank model, MMR, queue rules all undecided)." This document is that design.
Docs before software, same discipline as everything else in this repo — nothing in this pass
touches `apps/matchmaker` or IDUNA's schema.

## 1. What's already real, and what ranked adds on top

WOTAN already tracks genre-agnostic `player_game_stats` (`player_id`, `game`, `wins`, `losses`,
`matches_played`) — the casual/bot-pool win-loss record REDGARDEN reports today via
`POST /api/v1/redgarden/game-result`. That table has no rating column and isn't meant to grow
one: casual and ranked are different questions ("did you win" vs. "how good are you relative to
the field"), and conflating them would make the casual leaderboard (already shipped,
`GET /api/v1/redgarden/leaderboard`) start reflecting ranked anxiety it was never designed to
carry. Ranked gets its own table.

## 2. Rating model

**Recommendation: plain ELO, not Glicko/Glicko-2, not TrueSkill.**

- **Why not Glicko/TrueSkill:** both exist to model *uncertainty* well in low-game-count,
  irregular-play populations (chess correspondence players, asymmetric team sizes). REDGARDEN's
  ranked pool is currently 1v1 only (S170-14 2/3's player-only pool, the mode ranked extends) —
  symmetric, one opponent, no team-composition variance to reason about. That's exactly ELO's
  home turf. Glicko's rating-deviation math is real complexity bought for a problem this pool
  doesn't have yet. Revisit if/when ranked ever supports team sizes > 1.
- **Formula:** standard ELO. `E_a = 1 / (1 + 10^((R_b - R_a) / 400))`, `R_a' = R_a + K * (S_a - E_a)`
  where `S_a` is 1 (win), 0 (loss), no draws (arena has no draw condition today — a stalemate
  isn't a real game state, see `arena_game.c`'s win-condition logic).
- **K-factor: 32 flat, no provisional-period scaling, for v0.** Real ELO systems often use a
  higher K (e.g. 40) for a player's first N games to converge faster, then drop to 20-24 for
  established ratings. That's a legitimate v1 refinement, explicitly deferred here — a flat K
  is honest, simple, and correct; it just converges a little slower for new players. Don't build
  the provisional-period special case until there's a real population large enough to notice
  the slow-convergence problem it solves.
- **Starting rating: 1000.** Arbitrary but conventional (matches chess.com's rapid-pool default
  register); no meaning attaches to the number itself, only to relative standing.

## 3. Schema (proposed, not applied this pass)

```sql
-- New table, not an extension of player_game_stats (see §1).
CREATE TABLE IF NOT EXISTS redgarden_ranked_stats (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    player_id       CHAR(36)    NOT NULL UNIQUE,
    rating          INTEGER     NOT NULL DEFAULT 1000,
    wins            INTEGER     NOT NULL DEFAULT 0,
    losses          INTEGER     NOT NULL DEFAULT 0,
    matches_played  INTEGER     NOT NULL DEFAULT 0,
    last_played_at  DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_redgarden_ranked_rating ON redgarden_ranked_stats(rating);
```

A `POST /api/v1/redgarden/ranked-result` endpoint (mirroring the existing `game-result` handler's
shape) would take `{winner_player_id, loser_player_id}` (not a single player + win/loss flag,
since ELO needs *both* ratings at once to compute the delta) and apply the ELO update to both
rows atomically.

## 4. Queue rules

The existing matchmaker (`apps/matchmaker`) pairs whoever's queued, in arrival order, once the
lobby fills — correct for the bot pool and the player-only pool (S170-14 1/3 and 2/3), where
"any two humans" or "any N bots" is a fine match. Ranked needs to pair *similar* ratings, which
is a genuinely different queue shape from what exists today:

1. **Widening search window.** Start narrow (±50 rating), widen by a fixed step (e.g. +25) every
   N seconds of continued waiting, capped at a max window (e.g. ±300) so a queue never waits
   forever for a mirror-rating opponent that doesn't exist yet in a small population.
2. **Queue-side change, not matchmaker-binary-side.** This doesn't fit `apps/matchmaker`'s
   current model (accept connections, pair first N, spawn a server) — it needs a live queue that
   re-evaluates pairing continuously as new players join and existing waits age, closer to a
   proper matchmaking service than the current spawn-on-fill binary. Likely its own small Go
   service or a real rewrite of `apps/matchmaker`'s queue logic, not a `--ranked` flag bolted onto
   the existing one. Scoping the actual implementation is explicitly out of this pass.
3. **No decay, no seasons, for v0.** Rating decay for inactive players and seasonal soft-resets
   are real, common ranked-ladder features — deliberately deferred; a population too small to
   need widening-window matchmaking is also too small to need decay/seasons yet.

## 5. Explicit non-goals of this document

- Implementing any of the above (this is the design S170-14 (3/3) asked for, not the build).
- Team-based ranked (2v2+) — ELO's simplicity assumption (§2) breaks down there; revisit rating
  model choice if/when that's actually on the table.
- Placement matches, rank tiers/badges (Bronze/Silver/etc.), or any UI surface for rating —
  product decisions for whoever picks up the actual build, not pre-decided here.
- Anti-smurfing, queue dodging penalties, or any integrity tooling — real concerns for a shipped
  ranked mode, irrelevant to a design pass for a mode with no players in it yet.

## 6. What "done" looks like for the next pass

A build pass against this design should land, in order: the schema (§3), the ranked-result
endpoint, then the queue rewrite (§4) — schema and reporting first since they're independently
useful even before the queue itself understands rating (a flat-K ELO number sitting unused is
harmless; a queue that can't fill because it's waiting for a rating band with nobody in it yet
is the actual risk, hence the widening-window design rather than a fixed band).
