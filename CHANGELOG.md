# Changelog

## 2026-07-24 (8)

- NORTHSTAR §12 Phase E (S170-36) started: Milestone-6 equivalent (state serializer + action
  decoder) from `gpt2-alpine-c/docs/GAME_AI_NORTHSTAR.md`, extended to arena's hero/ability state
  instead of a REDGARDEN-specific format. New `packages/simulation/arena_ai_bridge.h`/`.c`:
  `arena_serialize_state()` writes a stable self/foe natural-language state string;
  `arena_decode_action()` parses a `move:x,z cast_q/w/r:0|1` action string, defaulting missing
  fields to a safe no-op and failing closed on garbage. 7 new headless tests, all green alongside
  the full existing suite. Not wired into the live bot or the GPT-2 inference server (`:8088`)
  yet -- contract only, same sequencing discipline as Phases B→C.

## 2026-07-24 (7)

- NORTHSTAR §12 Phase D (S170-33) — fourth hero, **The Frog**, the last clean-fit hero from
  S170-32's roster audit: Q (Loop Back) rewinds the Frog's own position/HP to ~3s ago via a new
  per-hero loopback ring buffer (16 slots, 250ms sample rate, sampled generically for every hero);
  degrades to the oldest available sample rather than refusing to cast if less than 3s of history
  exists yet. R (The Secret) reuses Ghost's `intangible_ms` mechanic at a longer duration --
  "reappear at a chosen location" isn't built, flagged as a simplification. W (ally-targeted) and
  the passive (UI-only) are skipped, same reasoning as other skips this phase. Bot heuristic is
  defensive (rewind when hurt, vanish when critical) since Frog deals no damage. 4 new tests, all
  green alongside the full existing suite. Arena has now absorbed every roster-fit hero from the
  audit -- the 8 structurally-blocked heroes need arena to grow new systems first, a real decision
  point flagged in the northstar rather than continuing to just pick the next one.

## 2026-07-24 (6)

- NORTHSTAR §12 Phase D (S170-32) — third hero, **The Ghost**: Q (skillshot simplified to
  instant-hit-if-in-range, damage + Silence), W (instant intangibility on its own cooldown, not a
  toggle), R (fixed-position damage zone, enemy-only side of Recital). First kit needing real
  status-effect state: new generic `silenced_ms`/`intangible_ms` `ArenaHero` fields (any hero can
  carry them) and a `hero_is_hittable()` check used everywhere a hit used to just check
  `foe->alive`. Zone DPS uses a fixed 1000ms tick interval rather than fractional-per-tick math --
  flagged, but did not fix, a related pre-existing rounding bug in Unicorn's W regen (works in
  tests that jump a full second, silently truncates to 0 at real 16ms frame rates). Also: a
  roster-fit audit of the remaining 10 heroes found most (Tree/Pizza/Druid/Doc Wheel/Retrieval
  Cart/Donkey/TYLER/Flamel) structurally blocked by systems arena doesn't have (grid pressure,
  allies, multi-unit, cooking) -- only Frog remains a clean fit. 7 new headless tests, all green
  alongside the full existing suite.

## 2026-07-24 (5)

- NORTHSTAR §12 Phase D (full roster in arena, S170-31) started: generalized `arena_cast_q`/
  `arena_toggle_w`/`arena_cast_r` to dispatch on a new `ArenaHero.hero_id` field instead of
  S170-18's hardcoded `owner == 0` check, then wired **The Duck** as the second kit (Q/R only --
  its W needs objectives that don't exist here, its E's trigger coincides with match-end, both
  flagged and skipped). `arena_init()` now defaults player=Unicorn, bot=Duck with simple
  heuristic bot-casting, giving the bot side a real kit for the first time. 6 new headless tests
  (including cross-slot dispatch verification), all green alongside the full existing suite.
  10 heroes remain, each a separate follow-on pass.

## 2026-07-24 (4)

- NORTHSTAR §12 Phase C (observer mode, S170-30) started, arena half: new
  `packages/simulation/arena_replay.h`/`.c` parses an `apps/arena` match log and drives
  `ArenaState` directly from it (linear interpolation between the 500ms snapshots). New
  `red_garden_arena --observe <path>` flag plays a logged match back through the same render
  loop as live play (camera control active, live-match input disabled, `R` restarts playback).
  6 new headless tests (`tests/test_arena_replay.c`), all green; `build_arena.sh` and
  `test_arena.sh` updated to include the new files. RTS-side playback and true live-tailing
  remain open, separate next steps.

## 2026-07-24 (3)

- NORTHSTAR §12 Phase B (replay logging, S170-29) closed for the MOBA half: `apps/arena` now
  opens `var/matches/arena-<timestamp>.jsonl` (fresh per match, including on restart) and appends
  `match_start`/`snapshot` (every 500ms, both heroes' x/z/hp)/`ability_cast`/`match_end` events.
  No connect-ticket auth exists in this client, so events use `"local_player"`/`"local_bot"`
  placeholders rather than a guessed WOTAN player_id -- flagged as a real gap, not silently faked.
  Verified by clean compile (`scripts/build_arena.sh`) and code review only -- this box has no
  display, so unlike the RTS half this couldn't be run end-to-end. `scripts/test_arena.sh`
  (headless sim tests, untouched by this change) still green.

## 2026-07-24 (2)

- NORTHSTAR §12 Phase B (replay logging, S170-28) started for the RTS half: `apps/server` now
  opens `var/matches/<port>-<timestamp>.jsonl` per match and appends `match_start`/`connect`
  (with Phase A's `player_id`)/`card_play`/`match_end` events — exactly §10's originally-spec'd
  minimum hook, now with player identity attached. Verified real log output from
  `scripts/test_10_bots.sh`. `var/` added to `.gitignore`. The MOBA half (`apps/arena`'s per-tick
  hero-state logging) is not started -- distinct next step, not covered by this pass.

## 2026-07-24 (1)

- NORTHSTAR §12 Phase A (WOTAN player identity, S170-26) started: `apps/server` now captures the
  real IDUNA-minted `player_id` from every connect ticket instead of discarding it after
  verification (`client_player_id`/`client_has_player_id`, keyed per client slot) — the prerequisite
  Phase B (replay logging) needs to attribute matches to real players. Ported
  `packages/common/http_client.h` (verbatim from shankpit-460) and IDUNA agent config loading.
  Reporting REDGARDEN win/loss results into IDUNA is deliberately not wired yet — its
  `/api/v1/players/{id}/session` endpoint is FPS-shaped (kills/deaths), REDGARDEN's `match_winner`
  isn't; flagged as an open schema question rather than forced in wrong. All existing tests
  (`test_10_bots.sh`, `test_arena.sh`) still pass.

## 2026-07-23

- Fixed `GL/glu.h` missing (installed `libglu1-mesa-dev`) — `apps/lobby` and `apps/arena` now build clean.
- Fixed `usleep` implicit-declaration warning at the root cause: `-std=c99` was hiding the POSIX declaration; added `-D_DEFAULT_SOURCE` to `scripts/build.sh`.
- Added connect-ticket accounts (HMAC-SHA256, same scheme as shankpit-460): `packages/common/hmac_sha256.h` ported verbatim, `apps/server` verifies tickets on `PACKET_CONNECT` (fails closed without `REDGARDEN_TICKET_SECRET`), test bots self-mint tickets like shankpit-460's `emily-bot`.
- Added simple matchmaking: new `apps/matchmaker` pairs `PACKET_FIND_MATCH` requests and spawns a dedicated `red_garden_server --port <N>` per match; new `PACKET_FIND_MATCH`/`PACKET_MATCH_FOUND`/`MatchFoundMsg` wire types.
- Validated VS0 (bot-vs-bot match) and VS1 (10 independent headless bots, 5 concurrent matches, matchmaking + accounts, 10s sustained load, zero crashes) via new `scripts/test_10_bots.sh`.
