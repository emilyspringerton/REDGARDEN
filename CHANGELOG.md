# Changelog

## 2026-07-24 (13)

- Verified the actual `--lobby-size 20` (10v10) path live, end-to-end, not just via the headless-tested code shared with 1v1: 20 real `apps/arena_bot` connections, 20 real drafts, correct team assignment (0-9/10-19), combat across 20 heroes, and a real team-wipe win condition (`match_end` winner matched exactly which team had zero living heroes left). All 20 bots then persisted and requeued into a second full 20-player match automatically -- identity stayed stable (1 registration each) across both. Server process count stayed healthy throughout (not the earlier zombie pileup). This closes the "10v10 unverified" gap flagged earlier the same day. Remaining honest gap: the SDL2 client's visual rendering of a live match is still unconfirmed (no Xvfb on this box).

## 2026-07-24 (12)

- MOBA 10v10 scaling + persistent bot pool (NORTHSTAR §13 cont'd): team-mode sim (`arena_game.c`/`.h` -- heroes[2] -> heroes[20], `team`/`active` fields, `arena_nearest_enemy()` generalizing foe lookup, `arena_init_teams`/`arena_update_teams` additive to the existing 1v1 path, 5 new tests, zero regressions in the full existing suite). Draft phase (`PACKET_ARENA_PICK`, `ARENA_PHASE_WAITING/DRAFT/LIVE`) -- heroes were hardcoded, now every real slot picks before the clock starts. `apps/arena_server` generalized to `--lobby-size N`. New `apps/arena_bot` -- a real networked bot (not the sim's internal practice AI), real WOTAN identity, plays via matchmaker, persistent. `apps/matchmaker` generalized (`--lobby-size`/`--listen-port`/`--first-game-port`), one binary serves both the card-RTS and arena roles now.
- Three real bugs found running an actual persistent bot-pool soak test (not by review): (1) bots were re-registering a brand-new WOTAN identity every match instead of keeping one stable identity -- fixed, register once per process; (2) match servers never terminated after match end, flooding a persistent bot's socket with stale packets from every prior match and silently swallowing its next connection's WELCOME packet -- fixed, servers now exit shortly after the match ends; (3) a UDP retry race in the matchmaker protocol could spawn phantom matches nobody ever connects to -- mitigated (slower retry interval) plus a defensive 60s no-progress server timeout so any phantom that still slips through self-cleans instead of leaking forever. Verified via an extensive soak test: 2 persistent bots, stable identity across 20+ matches each, real accumulating win/loss records, zero connect failures.
- Explicitly unverified yet: the actual `--lobby-size 20` path live end-to-end (same tested code as 1v1, not yet run with 20 real connections), and the SDL2 client's visual rendering of a live networked match (no Xvfb on this box).

## 2026-07-24 (11)

- Product pivot (NORTHSTAR §13): apps/arena (the MOBA) is the product now, not the card-RTS. Real 1v1 networked PvP: new `apps/arena_server` (ports connect-ticket/WOTAN pieces from apps/server), `--connect <host>` mode added to apps/arena's client, new `PACKET_ARENA_MOVE/CAST/SNAPSHOT` wire packets. Verified live, catching and fixing two real bugs: `arena_bot_enabled` wasn't gating `bot_cast_kit_if_ready` (a real second player would still get yanked/attacked by the bot's kit AI), and the sim clock started before both real players connected (a match could resolve before player 2 ever joined). Fixed both; server now only ticks once both slots are filled. Two real clients with distinct WOTAN identities verified sitting still at full HP, waiting for real input -- genuine PvP, not bots fighting bots. `scripts/test_arena.sh` (+1 regression test) and `scripts/test_10_bots.sh` both re-verified clean.

## 2026-07-24 (10)

- WOTAN player identity, S170-41 cont'd: `apps/server` now reports match results at match_end via `report_match_result()` -- agent-login, then `POST /api/v1/redgarden/game-result` per connected client's real player_id. Verified live end-to-end with a real 2-bot match played to natural completion: match log's `match_end` winner matched the public leaderboard afterward exactly (winner's wins +1, loser's losses +1). `scripts/test_10_bots.sh` + `scripts/test_arena.sh` both re-verified clean.

## 2026-07-24 (9)

- WOTAN player identity, S170-41: `apps/client/bot_main.c` now tries a real IDUNA register+ticket-mint round trip (falls back to the old self-mint on any failure) instead of always self-minting a fake ticket. Verified live: two bots registered distinct real `player_id`s, connected via the real matchmaker, match log shows real identities on every event. `scripts/test_10_bots.sh` re-verified clean (backward compatible). Companion IDUNA-side change (new `REDGARDEN-BOTS` agent, `player_game_stats` table, `/api/v1/redgarden/{ticket,game-result,leaderboard}` endpoints) landed in the IDUNA repo, verified live end-to-end there too.

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
