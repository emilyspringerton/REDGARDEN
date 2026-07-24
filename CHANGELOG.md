# Changelog

## 2026-07-24 (30)

- fix(ops): redgarden-bot-pool.service never set REDGARDEN_TICKET_SECRET (S170-72) -- the real
  root cause of "no entities visible," not a rendering or death bug. Live investigation of the
  founder's "i cant see myself or team or enemies" / "maybe the game isnt actually working right
  theres no entities" turned up ~55 accumulated zombie arena_server processes, all stuck at
  "0/20 connected" until their 60s no-progress timeout. The matchmaker log showed lobbies filling
  and spawning a real dedicated server every time, but no client ever completed PACKET_CONNECT to
  it. Root cause: only the two matchmaker systemd units had REDGARDEN_TICKET_SECRET set --
  redgarden-bot-pool.service (the unit that actually runs the 19 apps/arena_bot processes) never
  did, since it was written. Bots could queue fine (no ticket needed for that) but silently failed
  to mint a valid connect ticket for the actual game-server handshake, so matches formed and then
  sat empty forever. Fixed by adding Environment=REDGARDEN_TICKET_SECRET=... to the bot-pool unit.
  Verified live: killed the zombie servers, restarted all three services, watched real
  CLIENT N CONNECTED lines climb to 18-20/20 across several matches -- the system is now capped
  only by needing a 20th (human) player, not by a broken connect path.

## 2026-07-24 (29)

- fix(arena): the actual instant "YOU WIN" bug (S170-66) -- three more `#ifndef _WIN32` guards
  reintroduced by the newer 10v10 networked-PvP code, all in `apps/arena/src/main.c`: the
  `net_poll_snapshots()` call site, the click-to-move `net_send_move()` call site, and the Q/W/E
  `net_send_cast()` call site. On Windows (the founder's actual platform) all three silently
  compiled out -- no error -- so the client fell through to the local single-player practice
  simulation instead of ever applying real server snapshots, resolving near-instantly and
  producing a "YOU WIN" completely disconnected from the real networked match. `grep -n "#ifndef
  _WIN32"` now returns zero hits in this file; every remaining guard is a correctly-scoped
  `#ifdef _WIN32` around an actual platform difference. Second real blocker found + fixed in the
  same pass: `redgarden-bot-pool.service` (S170-65) launched exactly 20 bots into a
  `--lobby-size 20` matchmaker, permanently filling the lobby with bots alone -- dropped to 19 so
  a human always has an open slot. Also added, absorbing part of S170-68's scope per the
  founder's own real-time narrowing ("terminal launching the client is fine for now" / "auto
  draft is fine for now"): `net_send_pick()` + auto-draft (sends a `PACKET_ARENA_PICK` the moment
  `net_phase` reports `ARENA_PHASE_DRAFT`, console-logged) so a match never hangs waiting on a
  pick that never comes; and a click-to-continue "OK" requeue button on the win/lose screen in
  net_mode, reusing the same `net_find_and_connect`/`net_connect` path used at startup. Verified:
  `scripts/build_arena.sh` clean, `scripts/test_arena.sh` all green, and a full local mingw
  cross-compile (same toolchain/flags as CI) produced a clean 0-warning `RedGarden.exe`.

## 2026-07-24 (28)

- ops: real systemd units for the arena matchmaker pools + persistent bot pool (S170-65). Founder,
  after the S170-63 fix: "fix is not pushed" -- correctly pointing at the actual gap, since the
  code fix genuinely was pushed and merged, but the live matchmaker processes had *never* run
  under systemd at all, ever, on this box -- only ever manually nohup'd, which is exactly why
  S170-63's outage happened in the first place (died silently, stayed dead until someone
  noticed). New `ops/systemd/redgarden-matchmaker-bots.service`,
  `redgarden-matchmaker-players.service`, `redgarden-bot-pool.service` (+ new
  `scripts/run_bot_pool.sh`, a foreground wrapper so systemd can actually supervise the bot set),
  matching the existing `fatbaby-newssite.service`/`gfd-mud.service` pattern. Deployed and
  verified live: killed the manual processes, started the units, confirmed `Restart=on-failure`
  actually works (one stray leftover `arena_server` was squatting on the matchmaker's own port;
  once killed, systemd auto-relaunched the matchmaker within its restart window with no manual
  intervention).

## 2026-07-24 (27)

- fix(ops+ci): matchmakers had died (bots orphaned, queue packets going nowhere) and `PLAY.bat`
  never set `REDGARDEN_TICKET_SECRET`, so even after restarting the matchmakers the client failed
  silently at the ticket-mint step -- no human login flow exists yet, so `--queue` falls back to
  self-minting via that env var, which has to be set client-side too (S170-63, found live while
  a founder was actually trying to connect). Restarted both matchmaker pools with the shared test
  secret the live bot pool already uses. Fixed `PLAY.bat` to set the same secret before launching
  and added `pause` so a failure is actually readable instead of the window closing before the
  error prints. Also: briefly misdiagnosed this as an IDUNA-vs-server ticket-signing-secret
  mismatch (real, but irrelevant to the self-mint path actually in use here) and accidentally
  spawned one broken test bot mid-investigation that spammed the pool with failed
  connect/requeue cycles -- corrected, orphans cleaned up.

## 2026-07-24 (26)

- fix(ci): `PLAY.bat`'s hardcoded `127.0.0.1` was wrong for the actual distributed client
  (S170-59). Found live: a founder actually downloaded and ran the CI-built Windows client, and
  it hung "queuing for a match" at `127.0.0.1:7778` -- loopback, which only makes sense if the
  matchmaker is running on that same Windows machine. Fixed `PLAY.bat` to point at this box's
  real address (`198.58.107.85`) and print what it's connecting to before launching, instead of
  a silent `start` that gave no feedback about which server it was even trying to reach.

## 2026-07-24 (25)

- CI green end to end (S170-54 closed): confirmed via the GitHub Actions API (no `gh` CLI on this
  box, public API works without a token for a public repo) that commit `276614c`'s run passed
  every step -- headless tests, the bot-pool soak test, Linux server-side build, Linux arena
  client build, the mingw-w64 install, the Windows cross-compile, artifact bundling, and upload.
  `red-garden-build` now contains a real `RedGarden_Client_*.zip` (Windows .exe + SDL2.dll +
  PLAY.bat) and `RedGarden_Server_*.zip` (Linux server-side binaries), matching what a founder
  actually asked for: "the github artifact for REDGARDEN is unsuitable... no executable... SDL
  dll not bundled... check shankpit for the protopattern."

## 2026-07-24 (24)

- fix(arena): the actual root cause of the Windows build failure, found by locally reproducing
  the cross-compile (downloaded `gcc-mingw-w64-x86-64-win32` + deps via `apt-get download`,
  extracted with `dpkg-deb -x`, no sudo/root needed) instead of guessing blind against CI (S170-54
  cont'd). The whole networking section of `apps/arena/src/main.c` (ticket minting, WOTAN
  registration, `net_connect`, `net_find_and_connect`, snapshot polling — ~300 lines) was still
  wrapped in one big `#ifndef _WIN32`, so none of it was ever compiled on Windows at all despite
  the earlier per-call portability fixes — `main()`'s calls to these functions produced "implicit
  declaration" + linker "undefined reference" errors. Removed that outer guard now that the
  platform differences inside are each handled individually (winsock includes, ioctlsocket/fcntl,
  closesocket/close, mkdir, and one more found this pass: `getpid()` is POSIX-only, added a
  `GetCurrentProcessId()` branch). Also silenced two real `sendto()` type-mismatch warnings
  (Winsock wants `const char *`, POSIX accepts anything pointer-shaped). **Verified: a real
  `RedGarden.exe` (PE32+, Windows) now builds clean locally with zero errors and zero warnings**,
  Linux side (`build_arena.sh`, full test suite) still green. Same fix pushed for CI to confirm
  independently.

## 2026-07-24 (23)

- fix(arena): real Windows portability for `apps/arena/src/main.c`'s networking, found by
  actually watching the S170-54 CI run fail rather than trusting the workflow blind. Root cause:
  the file's `#ifndef _WIN32` guard around POSIX socket headers had no matching `#ifdef _WIN32`
  branch including `winsock2.h`/`ws2tcpip.h` at all -- so under MinGW, `sockaddr_in`/`AF_INET`/
  `SOCK_DGRAM` etc. were simply undeclared. Fixed to match `apps/server/src/main.c`'s already-
  correct pattern exactly: `winsock2.h`/`ws2tcpip.h`/`windows.h` + `#pragma comment(lib,
  "ws2_32.lib")` on Windows, the POSIX headers on everything else. Also fixed `fcntl(F_SETFL,
  O_NONBLOCK)` (POSIX-only) → `ioctlsocket(FIONBIO)` on Windows at both non-blocking-socket call
  sites, `close()` → `closesocket()`, and added the `WSAStartup` call Windows sockets need before
  first use. Along the way, found `--connect`/`--queue` were explicitly stubbed out on Windows
  builds entirely ("not supported... yet") -- now that the underlying networking actually
  compiles correctly cross-platform, enabled it for real rather than leaving the stub in place
  once its excuse was fixed. Verified: `scripts/build_arena.sh` (Linux) and the full
  `scripts/test_arena.sh`/`test_10_bots.sh` suites still green; the actual Windows cross-compile
  is CI-verified on push (still no `mingw-w64` locally, no sudo here).

## 2026-07-24 (22)

- fix(ci): `.github/workflows/ci.yml` rebuilt to produce a distributable artifact (S170-54).
  Founder, live: "the github artifact for REDGARDEN is unsuitable... no executable... SDL dll
  not bundled... check shankpit for the protopattern." Root cause: CI only ran `build.sh` (RTS
  server-side binaries) and never built `apps/arena` -- the actual product since today's MOBA
  pivot -- and uploaded bare Linux ELFs with no runtime bundled either way, nothing a founder
  could download and run. Mirrored `SHANKPIT/.github/workflows/release.yml`'s proven pattern:
  cross-compile the arena client to Windows via `mingw-w64` + the official `SDL2-devel-2.30.10-
  mingw` package, zip `RedGarden.exe` + `SDL2.dll` + a `PLAY.bat` as a separate Client artifact
  from the Linux server-side binaries (Server artifact). No `-lglu32` needed, unlike SHANKPIT's
  client -- `apps/arena` is shader-based and never depended on GLU. Also added `test_arena.sh` +
  `test_10_bots.sh` as real CI gates before packaging -- neither was run in CI before this,
  despite being the actual verification for everything built today. Linux side (tests, `build.sh`,
  `build_arena.sh`) re-verified locally; the Windows cross-compile itself is CI-only-verified for
  now (no `mingw-w64` installed locally, no sudo here -- queued as `sudo-queue/09-mingw-w64.sh`
  for a local dry-run if wanted).

## 2026-07-24 (21)

- feat(arena): territory capture redesigned to a real Arathi Basin-style channel + territorial jungle creeps + memorable bot names (S170-50/51). Replaced the S170-46 ambient-pressure territory model entirely with exclusive-presence channel capture: a node flips neutral the instant a channel starts (not on completion), interrupts (mixed presence, Pizza's corruption, damage taken, or the channeling team leaving) lose all progress with no free revert, and stealth (Frog's R) lets a lone capper channel undetected through a crowd of visible enemies -- but starting the channel breaks that stealth, and any damage to the channeling team interrupts it, both matching real Arathi Basin rules exactly. Added territorial jungle creeps: one per node, re-rolled from the node's current owner on every respawn, two flavors with genuinely different rewards (a rare contested-node creep grants a big capture-progress swing; a common owned-node creep heals its own team or helps the enemy flip the node, depending who kills it) -- a real counter-play tool against turtling comps. Activated real WOTAN stats tracking for the persistent bot pool (was silently running on self-minted tickets all session -- an env var oversight, not a code gap) and gave bots a curated pool of memorable display names via a new `--index` flag. 28 new headless tests (251 total). Verified live: a full ~2.5-minute 20-hero match ran to completion without crashing on the redesigned system; confirmed real, named player identities registering and the public leaderboard accumulating real stats.

## 2026-07-24 (20)

- docs(heroes): The Donkey — Paper Glide, a second auto-trigger ability (S170-49). Founder direction: "launching itself into the air while folding into a paper airplane... movement mobility and escape... fly over trees etc." Specified in `docs/HEROES_VS0.md` as Q, consistent with the existing Indirect-Control identity (auto-triggered alongside the Immortal's Fold passive, not player-cast): launches airborne, refolds into a paper-airplane shape mid-launch, glides clear of danger, ignoring ground terrain/obstacles and immune to ground-based CC while airborne. Docs only -- The Donkey (and the rest of the Indirect-Control archetype) stays blocked on a non-piloted-unit system that doesn't exist in `arena_game.c` yet, flagged explicitly in the entry rather than shoehorned into the owner-piloted sim.

## 2026-07-24 (19)

- feat(arena): The Courier, Ratatoskr (S170-48) -- eleventh hero, roster 10 → 11. TYLER `multiverse_heroes.md` #32 is already nicknamed "The Courier"; his messenger-between-two-fixed-points framing (the eagle at Yggdrasil's crown, Nidhogg at its root) maps directly onto the two existing `ArenaNode` positions -- W (Between Eagle and Serpent) is a pure fixed-geography teleport to whichever node is farther away, distinct from every other hero's ally/foe-relative teleport. Q is a Unicorn-shaped dash-strike whose landed cast also cleanses The Courier's own active debuffs (the passive, "editing the message" back to him). R is a flat life-drain execute on the nearest enemy. 7 new headless tests (223 total). Pick-validation bound and draft modulo widened 10→11. Verified live after cleaning up a stray leftover-process port conflict: all 11 hero_ids drafted across a real 22-bot pool, left running on the current build.

## 2026-07-24 (18)

- feat(arena): territory/node system + five new heroes -- Tree, Pizza, Flamel (absorbing the former Druid), Morrigan, and Dagda (S170-46/47). Founder picked territory/resource economy over multi-unit-per-player or non-piloted units as the next system to build, since it unblocks the most queued heroes at once. Extended the two previously-decorative `ArenaNode` markers with signed `pressure` (-100..100), threshold-derived `owner`, and `marked_by_team`/`mark_ms_remaining`; new `arena_tick_nodes()` sums weighted team presence per node each tick (Tree counts double, Root Network) and drifts/decays pressure toward a derived owner, called from both `arena_update()` and `arena_update_teams()` with no special-casing. Added a centralized `apply_damage()` helper (every damage call site now routes through it) so Pizza's R -- a real damage floor, not simplified away -- works consistently everywhere. Mid-build founder redirect ("druid and flamel should be the same hero"): merged Druid into Flamel in `docs/HEROES_VS0.md` first (TYLER lore check confirmed Druid had zero named-character backing, Flamel is a real one), keeping Flamel's identity and folding Druid's kit in as flavor. Then two more founder-driven additions on the same pass: Morrigan ("meta jungler," TYLER #68) built as an affinity for contested/unclaimed node ground since no standalone jungle-camp system exists; Dagda ("the two-natured hammer," TYLER #69) built with a literally dual-natured Q (kills a hittable enemy in range, else heals a hurt ally in range instead -- the same tool, either direction, depending on what's there). `apps/arena_server`'s pick-validation bound and `apps/arena_bot`'s draft modulo widened 5→8→10 heroes along the way. 62 new headless tests (216 total, up from 154), including a caught-and-fixed test bug (an exact-value assertion on Morrigan's execute-tick damage invalidated by a same-tick melee auto-attack and HP-floor clamping -- fixed by comparing damage deltas instead). Verified live: relaunched the persistent bot pool on the freshest build, all 10 hero_ids (0-9) drafted successfully across a real 20-bot match, pool left running (not torn down) so bots are actively playing the current roster.

## 2026-07-24 (17)

- docs(arena): S170-14 (3/3) — ranked matchmaking design pass, `docs/RANKED_MATCHMAKING.md`. Plain ELO (K=32 flat, starting 1000) recommended over Glicko/TrueSkill -- the uncertainty modeling those solve for doesn't apply to a symmetric 1v1-only pool yet. New `redgarden_ranked_stats` table, kept separate from casual `player_game_stats` (ranked rating and casual win/loss are different questions). Widening-rating-search-window queue design, explicitly scoped as its own future build pass since it doesn't fit the existing spawn-on-fill `apps/matchmaker` binary. Design only, no code landed -- this was a design gap, not a code gap, per the backlog item's own framing.

## 2026-07-24 (16)

- feat(arena): allies + fifth hero, Doc Wheel (S170-45). Founder decision: build allies/multi-hero-per-team in arena rather than territory or declaring the 4-hero roster complete. Team-mode infra already existed from the 10v10 pivot -- the actual gap was just an ally-targeting primitive. Added `arena_nearest_ally(int owner)` (mirrors `arena_nearest_enemy` exactly) and threaded an `ally` param through `tick_hero_kit`. Unblocked: Ghost's Recital ally-heal side (previously skipped), Frog's Borrowed Time (W, places a generic `next_cast_refund` buff on the nearest ally -- refunds whoever holds it their next Q/W/R cooldown to zero), and Doc Wheel (Buer) as a full new hero -- HP%-scaled heal+cleanse (Q), teleport-to-ally (W), teamwide cleanse+heal (R, simplified from a literal shield, flagged not faked). `apps/arena_bot`'s draft picker and `apps/arena_server`'s pick-validation bound updated so Doc Wheel is actually draftable over the wire. Found and fixed a real bug writing the Borrowed Time test: a Unicorn with no move target and no foe never reaches its cooldown-setting code path at all, so the refund check silently never ran -- the original assertion was passing by coincidence, not because the mechanism worked. 16 new headless tests, all green alongside the full existing suite. Verified live: two separate real bot matches (10-bot, 20-bot lobbies) both drafted Doc Wheel without incident.

## 2026-07-24 (15)

- feat(arena): player-only matchmaking pool (S170-14, 2/3) -- `scripts/launch_arena_pools.sh` stands up a second, entirely separate matchmaker instance on its own port (7779, `--lobby-size 2`), with zero bots ever configured to queue into it. Pool separation is operational (two matchmaker processes, two ports), not new code inside the matchmaker itself. Lobby size is 1v1, not 10v10, since a 10v10 player-only queue would never fill with near-zero real concurrent players today. Verified live: ran both pools simultaneously (bot pool with 2 bots + player-only pool), two real `--queue` human clients matched into a genuine 1v1 on the player-only pool ("Lobby full (2 players) -- internal bot AI disabled, entering draft"), and confirmed by grepping every bot's logs that none ever touched the player-only pool's ports. Ranked pool (3/3) stays explicitly undesigned -- no rank model, MMR, or queue rules exist yet, a design gap not a code gap.

## 2026-07-24 (14)

- feat(arena): `red_garden_arena --queue <matchmaker_host>` (`--matchmaker-port`, default 7778) -- a human player can now join whatever match the persistent bot pool is currently matchmaking into, instead of only supporting `--connect host:port` to an already-known server. Reuses `apps/arena_bot`'s exact queue pattern (`PACKET_FIND_MATCH`/`PACKET_MATCH_FOUND`, ~5s retry) and `net_connect`'s existing ticket-mint/handshake for the actual game connection -- pure client-side addition, no server changes needed. Verified live: started a real matchmaker + one persistent bot, ran the human client with `--queue 127.0.0.1`, confirmed it queued, matched with the bot, connected, and was assigned hero slot 1 on the same server the bot connected to (slot 0). First attempt failed on a test-setup mistake (matchmaker started without `REDGARDEN_TICKET_SECRET` exported, so the spawned server failed closed on all connects, correctly) -- not a code bug, fixed by restarting the stack with the secret actually set. Still bounded by the same known gap as before: no Xvfb on this box, so the client hits SDL_Init with no display right after connecting -- the join is proven, playing a full match still needs a real display.

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
