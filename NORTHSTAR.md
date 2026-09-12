# NORTHSTAR — RED GARDEN

**Status:** Core product (elevated 2026-07-19). **Product pivot 2026-07-24 — see §13: `apps/arena`
(the hero click-to-move MOBA) is the product now, not the card-RTS.** Read §13 before assuming
anything in §1-§12 below still reflects where new work should go — those sections predate the
pivot and are kept for the card-RTS's own (now secondary) history, not as current direction.

---

## 1. What this is

A deck-based real-time strategy game: **Clash Royale's model** (card hand, mana/influence
economy, drag-and-drop deployment onto a live board, real-time not turn-based) — not Clash of
Clans (base-building), not an autobattler (units aren't auto-positioned/auto-fought without
player input; the player actively deploys). Autonomous unit *behavior* after deployment (per the
original README's Diablo II/Conway's Game of Life references) stays — the correction tonight was
specifically about genre, not about removing autonomous post-deployment AI.

**This is the prototype for FIELDOFFICE/TrapX** (`SHANKPIT/docs2/TRAPX_NORTHSTAR.md`) — REDGARDEN's
cellular-automata territory-control grid (Neutral/Player/Enemy/Corrupted cells, automata ticks
spreading control) is the sandbox proving-ground for TrapX's own territory-custody mechanics
before those land in the larger DragonsNShit-backed product. Build the mechanic here first, where
it's cheap to iterate; port proven mechanics to TrapX once they hold up.

## 2. Real-time direction notes from tonight (2026-07-19), captured accurately

- Card hand UI should have **League of Legends-style affordances** — the polish/legibility bar
  (clean cooldown sweep, cost/availability feedback, hover/target clarity), not a genre change.
  Explicitly *not* an autobattler, corrected in the same breath it was raised.
- **All shop/menu surfaces need high-APM affordances, designed for pro-level play speed
  (2026-07-23, refined).** Generalized from the cooking/crafting direction
  (`docs/CONSUMABLES_AND_COOKING.md`) into a hard cross-cutting constraint: item shop, cooking,
  crafting, and any future menu must be fast enough for a high-speed pro player, not a casual
  point-and-click inventory screen. This is **not** keybind-only — click targeting/placement is
  still core to this game (card drag-and-drop, hero movement/targeting) — the bar is that both
  input paths (keybind and click) need to resolve instantly with no menu-diving, the way LoL's
  actual competitive scene uses a mix of hotkeys and precise clicks at high APM, not either one
  exclusively. **Must still read clearly to a casual player** (2026-07-23) — same "easy to learn,
  hard to master" bar already set for WEAKNIGHT's F1 handling in the sibling SHANKPIT repo: speed
  is optional depth for pro players, not a requirement just to understand the menu, and
  default/obvious options must stay legible without memorizing keybinds first.
- **Multiplayer** is required — not scoped further yet (real-time PvP matching the Clash Royale
  model implies synchronous multiplayer as the actual target mode, local/bot play as the
  dev/testing mode it already has).
- **Cross-platform: Android, iOS, and Desktop.** The current stack (C99 + SDL2 + OpenGL immediate
  mode, matching SHANKPIT's approach) supports Desktop directly; SDL2 itself supports
  Android/iOS as compile targets, but getting there is real cross-compilation/packaging work, not
  a given — scope honestly in a later pass rather than assumed solved by "SDL2 technically
  supports it."

## 3. Current implementation state (verified, not assumed)

Repo: `github.com/emilyspringerton/REDGARDEN`. Full historical design context lives in the repo's
wiki (`REDGARDEN.wiki` — `SPEC-4.md`, `SPEC-5.md`, `stadium.md`), which specs a considerably
larger target (40×40 grid, `MAX_ENTITIES 512`, a full mouse-input handler, a "Deterministic Dragon
System") than what's currently built.

What's actually implemented today (`packages/simulation/local_game.c`, 274 lines):
- 20×20 grid (not the wiki's 40×40), 4 cell states, a working cellular-automata tick (2s interval,
  neighbor-majority rule, corruption spreads at ≥4 corrupted neighbors).
- All 4 acceptance-criteria cards (Militia/Scout/Swarmlings/Outpost) with real costs, cooldowns,
  and per-owner influence economy.
- Outpost spawner (2500ms at tech tier 2, 5000ms at tier 1) — the 2-tier tech tree from the
  acceptance criteria is implemented (`tech_tier`, auto-promotes owner 1 at 15s match time — this
  auto-promotion looks like a placeholder/test stand-in for a real tech-tree UI, not a finished
  design, worth confirming).
- Win condition: 60% cell control held 60s, or enemy Outpost destroyed.

**Build status, verified tonight:** `apps/server` and `apps/client` (bot) compile clean (warnings
only — implicit `usleep` declaration, missing `<unistd.h>` include, trivial fix). `apps/lobby`
(the actual SDL2/OpenGL rendered client — grid rendering, card hand UI, drag-and-drop) **fails to
build on this box**: `GL/glu.h` missing, same root cause hit tonight on `shankpit-460`. Fix queued
at `~/sudo-queue/05-install-glu-dev.sh` (not yet run — needs sudo).

**Update 2026-07-23 — `GL/glu.h` fixed, VS0/VS1 validated, matchmaking + accounts shipped:**
`libglu1-mesa-dev` installed (`~/sudo-queue/05-install-glu-dev.sh` run); `apps/lobby` and
`apps/arena` now build clean too. Also fixed the `usleep` warning at the root cause —
`-std=c99` was hiding the POSIX declaration even with `<unistd.h>` included; added
`-D_DEFAULT_SOURCE` to `scripts/build.sh`'s `COMMON_FLAGS`.

VS0 (bot-vs-bot match works) and VS1 (online play validated with 10 independent headless bots)
are both done, exercised together by the new `scripts/test_10_bots.sh`:
- **Accounts**: connect-ticket auth, same scheme as shankpit-460 (`packages/common/hmac_sha256.h`
  ported verbatim, RFC 4231 test vectors re-verified in this repo). `apps/server` gates
  `PACKET_CONNECT` on a valid ticket, fails closed if `REDGARDEN_TICKET_SECRET` is unset. Test
  bots self-mint tickets with the shared secret (mirrors shankpit-460's `emily-bot` pattern) —
  no real IDUNA account/JWT needed for headless QA.
- **Matchmaking**: new `apps/matchmaker` — REDGARDEN's simulation is one match per process (a
  single global `ServerState`, owners indexed 0-2), so "matchmaking" here means queuing
  `PACKET_FIND_MATCH` requests, pairing two at a time, and spawning a fresh `red_garden_server`
  on its own port per match (`fork`+`exec`, `SIGCHLD` ignored to auto-reap). `apps/server` gained
  a `--port` flag to support this. New `PACKET_FIND_MATCH`/`PACKET_MATCH_FOUND`/`MatchFoundMsg`
  wire additions in `packages/common/protocol.h`.
- **Validated**: `scripts/test_10_bots.sh` boots the matchmaker, launches N bots (default 10),
  confirms the expected number of matches spawn, all bots connect, and everything survives 10s of
  sustained play with zero crashes. Ran clean at 10 bots / 5 concurrent matches.
- **Scope note**: this validates the existing card-RTS server+bot (`apps/server`,
  `apps/client`), not `apps/lobby` (still no build target wiring lobby into this flow) or
  `apps/arena` (separate demo, unaffected).

## 3.5. `apps/arena` — playable single-hero click-to-move demo (2026-07-23)

Founder deadline: playable against a bot before this-time-tomorrow. New, separate, additive build
target — does not touch `apps/lobby`/`apps/server`/`apps/client`/`packages/simulation/local_game.c`.
A new sim (`packages/simulation/arena_game.c`) drives one player hero and one bot hero: click-to-
move, melee combat, simple win condition. The bot's steering runs through a small hand-authored
feed-forward net (dense → ReLU → dense → Tanh, same shape as `SHANKPIT/packages/simulation/
neural_net.h`'s `bot_brain_forward`) rather than an if/else heuristic — weights are hand-picked, not
trained (no training pipeline wired up), the honest "or equivalent" for tonight; two of the six
hidden units carry distance/HP-difference signal with zero output weight, left as the hook a future
trained pass would use for kiting/retreat behavior.

The client (`apps/arena/src/main.c`) is **shader-based (modern GL) on purpose** — it only needs
`GL/gl.h` + `SDL_GL_GetProcAddress` function loading, not GLU, so it sidesteps the exact dependency
blocking `apps/lobby` above (confirmed: `ldd build/red_garden_arena` shows no `libGLU` at all).
Colored-cube placeholders for heroes/nodes, lit by a basic one-directional fragment shader; right-
drag-to-orbit + scroll-to-zoom camera; left-click-to-move with an animated expanding/fading ring
marker at the target point; HP bars + win/lose banner reuse the existing lobby-style immediate-mode
HUD text (GL context requested as compatibility profile specifically so that legacy `glBegin` text
drawing still works alongside the new shader path, without needing a second text-rendering system).

`scripts/build_arena.sh` compiles clean. **Not yet verified interactively** — this box is headless
(no `DISPLAY`, no Xvfb installed; confirmed via `SDL_VIDEODRIVER=dummy` that the binary starts and
fails cleanly at window creation rather than crashing). `~/sudo-queue/
06-install-xvfb-for-arena-testing.sh` queued (needs sudo) so an actual render can be smoke-tested;
usage instructions for `Xvfb` + `LIBGL_ALWAYS_SOFTWARE=1` are in the script's own comments.

**Status check (2026-07-23, later same day):** confirmed via direct inspection — `apps/arena` is
real, builds clean, and is genuinely the mouse-driven MOBA mode the roster work (`docs/
HEROES_VS0.md`) is meant to iterate toward. **Gap found, not closed**: it currently drives generic
colored-cube placeholder heroes, not the named roster — none of the 11 heroes' kits are wired into
`arena_game.c` yet. Next concrete step for this mode: wire at least one real hero (kit + ability
inputs replacing the placeholder cube's plain click-to-move/melee) into `arena_game.c`, proving the
integration path before attempting the full roster. Not attempted this pass — flagged so the next
session picks this up as the actual next step, not another content pass.

Explicitly deferred past tonight (not attempted): the 10v10/5-node map, jungle ecology grafted onto
`GoblinFoxDragon`'s mob/NM/loot systems (§8 below), terrain heightfield (`SHANKPIT/packages/world/
terrain.c` is a fork candidate), team vision-sharing + minimap (needs a real teammate concept
first — this demo is 1 hero vs. 1 bot), real skeletal/keyframe animation, and any BRAWLPIT-derived
assets (checked — that fork is also 100% old-style immediate-mode GL, nothing shader-based to
reuse; its character/stage struct pattern is a loose future reference only).

## 4. Gap between wiki spec and live code

The wiki's `SPEC-4.md`/`SPEC-5.md` describe a materially larger system than what's built:
`entity_behaviors.h` (aggro/targeting), `grid_tick.h` (40×40, `MAX_STRUCTURES`/`MAX_TOWNS`), a full
`mouse_input.h` for drag-and-drop against SDL2, and a "Deterministic Dragon System" not present in
current code at all. Whether the wiki represents an already-superseded earlier design pass or the
actual target still to build toward is an open question worth resolving explicitly before writing
more code against either — flagged, not assumed.

## 4.5. Engagement design principles (founder direction, 2026-07-19)

Explicitly requested, and explicitly confirmed serious after a "was that a joke" check ("as a
joke" / "but it is very real") — the same honest-clarification pattern used elsewhere tonight
when a request sounded surprising. Real design pillars, not a throwaway line:

- **Hype modes** — limited-time or event-driven game modes, the kind that generate real
  community excitement.
- **Mystery** — unrevealed content/mechanics as a genuine engagement hook (not information
  withheld to manipulate spend — see next point).
- **FOMO** — time-limited content, the standard live-service-game engagement lever.

**Explicit, load-bearing constraint: no dark patterns, per the honor code.** "The honor code" is a
real, already-established concept in this ecosystem — `IDUNA/docs/kikoryu/VS0_IDENTITY_GATE.md`
frames it as an accepted player-conduct agreement tied to tournament/identity onboarding on the
KIKORYU platform. The instruction here extends that same ethical bar to game design itself: hype,
mystery, and FOMO are legitimate, widely-used engagement tools — the constraint is building them
without the predatory mechanics (manipulative timers, pay-to-not-wait, obscured odds, etc.) that
usually ride alongside them in mobile live-service games. This needs a real design pass before
any of it is built — flagged as a real requirement here, not scoped further in this document.

## 5. What this explicitly does not do (yet)

No multiplayer networking exists yet (current entities/state are single-process, local only). No
mobile build targets exist. No LoL-style UI polish pass has started. This document is the
accurate capture of tonight's real-time scoping, not a claim that any of it is built.

## 6. Forward reference — hero/card lore (2026-07-23)

`TYLER/multiverse_heroes.md` is a 112-entry lore compendium (names, history, faction, archetype —
deliberately no abilities/stats/roles yet) built for "a League of Legends-like, Diablo II-like game
... drawing from TYLER lore, tying in real mythologies ... wide multiverse." RED GARDEN is named,
founder-confirmed, as the closest existing product this could feed: a card-hand RTS already aiming
for LoL-style card affordances (§1, §2) with only four generic cards implemented (Militia/Scout/
Swarmlings/Outpost, §3) — no named heroes yet. Nothing here commits RED GARDEN to that roster or to
any specific card mechanics; per the compendium's own stated order, abilities and stats get designed
*from* the lore in a later pass, once specific heroes are chosen to work on, not assumed now. Noted
as a real, live cross-repo connection rather than left implicit.

## 7. Hero implementation queue (started 2026-07-23)

Founder-tiered picks from `TYLER/multiverse_heroes.md`, tracked here as they're chosen — this list
is the actual "which ones get built first" answer §6 explicitly left open. Append-only as more get
picked; update status in place as work starts/lands. No abilities/stats/card mechanics designed for
any entry below yet — that's still the deliberately-deferred later pass §6 already named.

| Tier | Hero | Compendium # | Status |
|---|---|---|---|
| S | Zhang Guo Lao's Donkey ("The Donkey") | #38 (Faction 4 — Middle Kingdom Heirs) | Not started |
| S | A Duck, Reportedly Telekinetic ("The Duck") | #103 (Faction 10 — Springerton Engine) | Not started |
| S | The Unicorn, Allegedly a Robot ("The Unicorn") | #104 (Faction 10 — Springerton Engine) | Not started |
| S | The Ghost, Self-Identified as Alien ("The Ghost") | #105 (Faction 10 — Springerton Engine) | Not started |
| S | The Frog, Keeper of a Secret It Won't Share ("The Frog") | #106 (Faction 10 — Springerton Engine) | Not started |
| S | The Tree, Speaking French ("The Tree") | #107 (Faction 10 — Springerton Engine) | Not started |
| S | The Pizza, On Fire, Uninvestigated ("The Pizza") | #108 (Faction 10 — Springerton Engine) | Not started |
| S | The Retrieval Cart ("The Cart") | #10 (Faction 1 — Jiangshi Syndicate) | Not started |
| S | Buer, the Wheel-Physician ("Doc Wheel") | #25 (Faction 2 — Goetia Court) | Not started |

**#38, for context:** "The famous Immortal's donkey, given its own story for once." Traditionally
folds up like paper when not needed and unfolds when it is — the founder's S-tier pick turns that
into its own character rather than a joke prop: a hero whose entire nature is showing up exactly
when required and disappearing, without complaint, the rest of the time.

**#103–#108, for context:** the complete `just_a_duck.md` ensemble is now in the queue, S-tier,
end to end. Duck (telekinesis, government job, "the chosen one"), unicorn ("I'm a robot in
disguise," said while eating spaghetti), ghost ("I'm not really a ghost, I'm an alien," said mid-
piano), frog (top hat, claims to know the secret of time travel, has never told anyone — including
the duck, who covers for it as "shy"), tree (claims to be "the keeper of the universe's greatest
secret" — in French, untranslated), and the pizza (announces "I am the chosen one," then catches
fire, and nobody in either the source transcript or its continuation
(`TYLER/episodes/x00_the_custody_of_a_duck.md`) ever checks on it). Not one claim across all six is
confirmed or denied anywhere in the record. That's the whole cast, and it's the entire point of the
pick: six characters whose only real competition is over whose story is the least believable, and
the source material refuses, on principle, to ever settle it.

**#10, for context — the most mechanically unusual pick so far:** the Jiangshi Syndicate's
Retrieval Cart, a recurring anomaly where a requested document turns out to already be waiting,
with no requester ever logged. Flagged as genuinely interesting for card-hand gameplay specifically
because its core identity isn't "a unit that acts" — it's "something that delivers unrequested
utility on its own schedule." That's a different shape than fighter/mage/support: closer to an
uncontrollable recurring world event attached to a lane than a traditional hero. No mechanics
designed yet (same discipline as everything else in this queue) — flagged here specifically because
that unusual shape is worth designing toward on purpose, not smoothing into a conventional kit.

**#25, "Doc Wheel," for context — the queue's first deliberate support pick:** Goetia Court's Buer,
played mundane in the lore (a healer whose only trick is being extremely good at medicine, no
combat power at all). Founder-flagged as "a good support archetype" specifically because the lore
already refuses the flashy version — no shield-bash, no team-wide ultimate, just a hero whose whole
identity is being the ally worth having in the lane for reasons that are never dramatic and never
wrong. No mechanics designed yet, same discipline as the rest of this queue.

## 8. Ecology + MOBA map concept (2026-07-23)

Founder direction: tie the living cellular-automata board (§1's Neutral/Player/Enemy/Corrupted
grid) into the hero pass (§6/§7) as one system, not two parallel ones.

- **The board is alive, not just contested.** The automata grid already models territory as
  something that spreads and reacts on its own tick, independent of direct player action (§1). The
  addition here: some of what's out there should have persistent identity — living ecology whose
  "DNA" remains in the world and interacts with real player-controlled heroes, rather than neutral
  scenery that resets every tick. Concretely, this points at certain compendium entries (the
  dungeon-boss / notorious-monster candidates already flagged for `GoblinFoxDragon/docs2/
  HERO_CONTENT_FRAMEWORK.md`) existing as ecology-driven presences that persist, react, and evolve
  across a match the same way the corruption-spread automata already does — distinct from the cards
  a player actively deploys. §1's existing "not an autobattler" line still holds: players still
  actively deploy their hand; the ecology is the board itself reacting back, not another player.
- **Setting note (2026-07-23):** the concept overlaps heavily with FIELDOFFICE/TrapX's alive-city
  metaverse, and a TrapX-skinned version of RED GARDEN is worth exploring later — but this pass
  develops RED GARDEN on its own simpler premise first, without forcing every hero/mob into a
  city-dweller frame. A Highlands-style nature setting (matching LoL's own Summoner's Rift
  register) reads better for the actual target audience than an urban TrapX reskin would.
- **Map shape: Arathi Basin, with more jungle — 10v10, 5 nodes.** A direct reference to WoW's
  classic capture-and-hold battleground: 5 resource nodes (Arathi Basin's own count — Stables, Gold
  Mine, Lumber Mill, Farm, Blacksmith, or this game's equivalent naming) spread across open ground,
  each worth a running resource tick while held, no single chokepoint deciding the match. Team size
  set at 10v10 — large enough that node control is a genuine map-wide coordination problem, not a
  small-team skirmish. The addition on top of that shape: more jungle — MOBA-style neutral camps
  woven into the terrain between the 5 nodes, giving the automata grid (§1) real geography to spread
  across and giving the ecology-driven heroes above actual territory to inhabit rather than being
  placed arbitrarily.
- **The jungle is alive and dynamic, not static camps.** The neutral jungle above isn't a fixed set
  of respawn-timer monster pens — it should behave like the rest of this board: reactive, spreading,
  changing over the course of a match the same way the automata's Corrupted cells do (§1). Grafted
  directly onto the mob/NM/loot systems already real and working in `GoblinFoxDragon`'s MUD (`server/
  mob`, `server/nm`'s placeholder/window/respawn system, `server/loot`'s treasure pools) rather than
  building a second, separate creature system from scratch — that substrate is live and already
  play-tested (this session's own worm-grinding and Poison-bug fix ran on exactly this system). The
  graft is the design decision; wiring it into RED GARDEN's own build is a later, separate pass.

Not scoped further yet — no map file, no automata-to-hero binding code, no concrete node layout, no
actual code connecting RED GARDEN to GFD's mob systems. Captured here as real design direction before
either the ecology grid or the hero queue (§7) get built out further, so the two don't end up
designed against each other.

**Indirect-control archetypes are a deliberate roster feature, not a gap (2026-07-23).** Founder
observation: a meaningful slice of the queued heroes already don't fit the standard MOBA taxonomy
(mage / assassin / tank / support) — the Retrieval Cart (§7, #10) is explicitly "a world event
attached to a lane" rather than a unit a player commands, and the ecology-driven presences above are
by definition not directly piloted at all. This is read as a real, load-bearing part of the roster's
identity, not smoothed toward conventional kits: RED GARDEN's hero pool should keep room for heroes
whose whole hook is indirect control — something the player influences, times, or benefits from
rather than something the player directly plays as fighter/mage/assassin does in a traditional MOBA.
No taxonomy or ratio decided yet (how many direct-control vs. indirect-control heroes a healthy
roster needs is an open question) — flagged here so the later mechanics pass (§6, §7) designs
toward that mix on purpose rather than defaulting every hero into a conventional role by habit.

## 9. Hero + item content pass (2026-07-23)

`docs/HEROES_VS0.md` — concrete VS0 ability kits for all nine queued §7 heroes plus TYLER (an
exact reskin of DOTA's classic "OG" Meepo, including the unforgiving all-clones-share-one-death
rule, per direct founder request), and a starting item roster styled on LoL Season 3's item
*archetypes* (crit carry, on-hit carry, burst mage, utility mage, tank initiator, tank/MR,
lifesteal duelist, penetration lines, support aura) rather than its specific names. Several heroes
(The Tree, The Pizza, The Retrieval Cart, Doc Wheel) get a RED GARDEN-specific passive that
touches the living cellular-automata grid (§1/§8) directly, not just combat stats. Content only —
no code wired into `packages/simulation/local_game.c` yet, no balance pass.

`docs/BACKSTORY.md` (2026-07-23) — the in-fiction reason the roster is a roster: Tyler forms a
loose motorcycle gang, and every §7 hero is a member of it, recruited the way Tyler recruits
everyone (shows up, doesn't explain himself, stays until someone else can carry it). Explains why
several heroes leave a mark on the living grid itself rather than just fighting on it — the gang
was never visiting the board, it was always already there.

## 10. Match history, replays, spectator mode, esports (2026-07-23) — future direction, not this pass

Founder direction, explicitly deferred beyond the 24-hour VS0/VS1 validation push (§2 update):
match history is needed both for future ML bot training (the existing neural-net bot approach —
see `apps/arena`'s hand-authored feed-forward net, and `SHANKPIT/packages/simulation/neural_net.h`
which it borrows its shape from — implies a training pipeline will eventually want real match data
to learn from) and for community moderation/maintenance (dispute resolution, anti-cheat review).
Replays, a spectator mode, and eventual esports support are the natural next layer on top of that
same data, but are explicitly *not* in scope for the current push — "esports is not in 24 hours."

**What this pass actually adds, as the minimum hook to not make that harder later:** each
`red_garden_server` instance (one per match, per §2's matchmaker) appends a simple newline-
delimited JSON event log for its own match — connects, card plays, and the eventual win condition
— to `var/matches/<port>-<timestamp>.jsonl`. This is deliberately just a data-capture hook, not a
replay system: no player-facing playback, no spectator wire protocol, no ranking/ladder work. It
exists so that when replays/ML-training/esports work actually starts, there's already a real
corpus of match data to build against instead of starting from zero.

## 11. Cooking + crafting (2026-07-23) — future direction, not this pass

`docs/CONSUMABLES_AND_COOKING.md` — a curated consumable/item name pool mined from
`gitlab.com/mailtruck/creepy-carrots` (tone-matched to the roster's existing absurdist register),
plus the founder's cooking (mid-match, resources → cooked buffs) and crafting (mid-game, resources
→ items alongside the direct-purchase roster) direction. Neither is mechanically designed or
implemented yet — captured so later passes on the item roster or the resource economy don't design
against it by accident.

## 12. Full roster, replays/observer-mode, WOTAN, and a Game AI northstar (2026-07-24)

Northstar only — nothing in this section is built. Picks up directly from §10 (which already
predicted this moment: "when replays/ML-training/esports work actually starts, there's already a
real corpus of match data to build against"). Phased, per founder direction ("not all at once
obviously in phases"):

**Phase A — WOTAN player identity (prerequisite, not parallel). Started 2026-07-24 (S170-26).**
Founder's own dependency reasoning: "how can we find replays if we don't have players on wotan ya
know?" A replay is only useful once it's attributable to someone — WOTAN
(`okemily.com/tournaments.html`'s existing product, not a new one) needs a real player-stats/
identity surface before replays are worth watching, not after. This phase must land before Phase B
is worth anything, not just before it.

**Done so far:** `apps/server/src/main.c` was already verifying a real IDUNA-minted `player_id`
inside every connect ticket, but discarding it right after verification. Now captured per client
slot (`client_player_id[MAX_CLIENTS]` / `client_has_player_id[MAX_CLIENTS]`) — the actual
prerequisite Phase B needs to key replay events to a real player, not a slot index. Also ported
`packages/common/http_client.h` (verbatim from shankpit-460's S156-04 original) and IDUNA agent
config loading (`IDUNA_BASE_URL`/`IDUNA_AGENT_NAME`/`IDUNA_AGENT_SECRET`), matching shankpit-460's
pattern exactly.

**Schema decision resolved, 2026-07-24 (S170-41).** shankpit-460 reports FPS `kills`/`deaths` to
IDUNA's `/api/v1/players/{id}/session`; forcing REDGARDEN's `match_winner` (win/loss) into those
columns would have corrupted shared WOTAN profile semantics. Went with the separate-endpoint path:
IDUNA gained a genre-agnostic `player_game_stats` table (`player_id`, `game`, `wins`, `losses`,
`matches_played`) plus three new endpoints — `POST /api/v1/redgarden/ticket` (mints a connect
ticket on behalf of an already-registered `player_id`, gated by a new `redgarden.ticket.mint`
permission — REDGARDEN bots have no OAuth login, so unlike shankpit's ticket handler this mints on
behalf of a caller-supplied player_id rather than the caller's own, restricted to
`provider=redgarden_bot` rows so it can never mint a ticket impersonating a real human), `POST
/api/v1/redgarden/game-result` (`redgarden.match.write`, same trust model as
`shankpit.match.write`), and a public `GET /api/v1/redgarden/leaderboard`. New M2M agent
`REDGARDEN-BOTS` provisioned (mirrors `SHANKPIT460-SERVER` exactly). Verified live end-to-end, not
just unit tests: agent-login → register a real player → mint a real ticket → the ticket verified
against the actual C `hmac_sha256.h`/`verify_connect_ticket` code → posted match results → read
back off the public leaderboard.

**Bots now carry real WOTAN identities, 2026-07-24 (S170-41).** `apps/client/bot_main.c` tries the
real register+mint round trip first (same `IDUNA_BASE_URL`/`IDUNA_AGENT_NAME`/`IDUNA_AGENT_SECRET`
env vars `apps/server` already loads) and falls back to the old self-minted ticket (shankpit-460's
own known emily-bot simplification) on any failure, so a transient IDUNA hiccup can't hang a
headless test run. Verified live: two bots each registered distinct, real, persistent `player_id`s,
connected through the real matchmaker, and the match log shows real WOTAN identities on every
`connect`/`card_play` event instead of random bytes. `scripts/test_10_bots.sh` re-run clean
afterward (10 bots, 5 matches, self-mint fallback path since that script doesn't set the IDUNA env
vars) — confirms the change is backward compatible, not just additive in isolation.

**Done — `apps/server` now reports match results at match end, 2026-07-24 (S170-41 cont'd).**
`report_match_result()` runs once, right where `match_log_win` already fires: agent-logs in, then
posts `win`/`loss` to `/api/v1/redgarden/game-result` for each connected client's real `player_id`
(formatted from the captured 16 raw bytes into the canonical dashed UUID string IDUNA's Go side
parses). No-op if IDUNA isn't configured; best-effort otherwise, so a WOTAN reporting hiccup can
never crash or hang a live match. Verified live end-to-end, not just in isolation: ran a real
2-bot match to natural completion (`match_winner` resolved from actual card-RTS play, not forced),
confirmed the match log's `match_end` winner matched the public leaderboard afterward exactly —
the winner's `wins` incremented, the loser's `losses` incremented, both keyed to their real,
persistent WOTAN identities. `scripts/test_10_bots.sh` and `scripts/test_arena.sh` both re-verified
clean afterward.

Known, accepted gap: clients using the self-minted fallback ticket (IDUNA not configured for that
bot) report results under 16 pseudo-random bytes reformatted as a UUID-shaped string that doesn't
match any real `players` row — harmless, since the leaderboard's `INNER JOIN players` naturally
filters those rows out; flagged in `report_match_result`'s own doc comment rather than silently
left unexplained.

**Phase B — Replay logging. Started 2026-07-24 (S170-28).** Extends §10's already-specified
minimum hook (`red_garden_server` per-match `var/matches/<port>-<timestamp>.jsonl` event log) to
`apps/arena`'s MOBA mode too — per-tick hero position/HP/ability-cast snapshots, keyed to the
WOTAN player identity from Phase A so a replay says *whose* match it is, not just *a* match.

**Done so far — the RTS half (`apps/server`), exactly as §10 originally spec'd, now with player
identity attached.** Each server instance opens `var/matches/<port>-<timestamp>.jsonl` at startup
and appends one JSON line per `match_start`, `connect` (now including the Phase A `player_id`,
hex-encoded, or `"unregistered"` for a ticket without one), `card_play` (client, player_id, card,
grid position), and `match_end` (winner) — verified against real match logs from
`scripts/test_10_bots.sh`, not just read from the code. `var/` added to `.gitignore` (generated
data, not source).

**Done — the MOBA half (`apps/arena`), S170-29.** `apps/arena/src/main.c` now opens
`var/matches/arena-<timestamp>.jsonl` (fresh file per match, including on `R`-key restart) and
appends `match_start`, a `snapshot` event every 500ms (both heroes' x/z/hp — a fixed low rate
rather than true per-physics-tick, to keep log size sane, same spirit as real esports replay
systems), `ability_cast` (Q/W/R presses), and `match_end`. **Real gap flagged, not papered over:**
`apps/arena` has no networking or connect-ticket auth at all, so there's no real WOTAN `player_id`
to attach here — events use `"local_player"`/`"local_bot"` placeholders. Real identity attribution
for arena replays is blocked on arena getting connect-ticket auth in the first place, which is a
separate, larger, not-yet-scoped piece of work. Also flagged: this box has no display (no Xvfb),
so this was verified by code review + a clean compile (`scripts/build_arena.sh`), not by actually
running the windowed client end-to-end the way `apps/server`'s log was verified against real
`test_10_bots.sh` output.

Phase B is now closed for both halves (RTS + MOBA) under this box's constraints.

**Phase C — Observer mode, first-class. Started 2026-07-24 (S170-30), arena half.** Founder:
"observer mode is a first class citizen." Not a bolted-on debug view — a real client mode that
reads Phase B's logs (live-tailing an in-progress match, or fully played back after) through the
existing renderer, same draw code, no second rendering path. "I want to start watching replays
asap" is the actual product pressure behind this phase; Phase A/B are the real prerequisites
standing between here and that, not extra scope invented along the way.

**Done — arena playback.** New `packages/simulation/arena_replay.h`/`.c`: a fixed-format parser
(same "no general JSON parser for self-produced, controlled data" spirit as `http_client.h`) reads
an `apps/arena` match log into an `ArenaReplay` (parsed snapshots + winner), and
`arena_replay_apply_at()` drives `ArenaState.heroes[0]/[1].x/z/hp` directly from it, linearly
interpolating between the 500ms-spaced snapshots so playback isn't choppy. `red_garden_arena
--observe var/matches/arena-<ts>.jsonl` runs this through the *exact same* render loop as live
play — camera control still works, movement clicks/kit casts/live-match restart are disabled,
`R` restarts *playback* from the beginning instead. 6 new headless tests
(`tests/test_arena_replay.c`) cover the parser and the interpolation/winner-timing logic — this
part doesn't need a display to verify, unlike the windowed rendering itself, which (per S170-29's
same standing constraint) couldn't be run end-to-end on this box.

**Not done yet — playback for the RTS side (`apps/server`'s logs) and true live-tailing (reading a
log file that's still being appended to, not just a completed one).** Both are real, separate next
steps within Phase C, not silently folded into this pass.

**Phase D — Full roster in arena. Started 2026-07-24 (S170-31), second hero: The Duck.** Extends
S170-18's proof-of-concept (The Unicorn, player-hero-only, one kit) to the rest of
`docs/HEROES_VS0.md`'s roster, both sides (bot included) — the actual "iterate on the MOBA version
with the roster" ask, once the integration path S170-18 proved is trusted enough to repeat ten
more times.

**Done — kit dispatch generalized, second hero wired, both sides proven.** `ArenaHero` gained a
`hero_id` field; `arena_cast_q`/`arena_toggle_w`/`arena_cast_r` now switch on it instead of
S170-18's hardcoded `owner == 0` check — either owner slot can carry either hero. **The Duck**'s
Q (Telekinetic Yank — pull the foe toward the Duck + AD damage) and R (Total Telekinesis — bigger
pull + damage, longer cooldown) are wired; its **W (Government Clearance)** needs objective
structures that don't exist in this 1v1 arena, and its **E (Chosen One)** triggers on a killing
blow that would also end the match under this arena's win condition, giving the buff a zero-length
observable window — both skipped and flagged, not faked. `arena_init()` now defaults to
player=Unicorn, bot=Duck, with simple heuristic bot logic (cast Q/R when off cooldown and the foe
is in range) — the bot side has a real kit for the first time, not just plain melee, satisfying
Phase D's explicit "both sides" requirement. 6 new headless tests, including one proving dispatch
works from *either* owner slot (Unicorn cast from slot 1), all green alongside the full existing
suite (`test_arena.sh`, `test_10_bots.sh`).

**Third hero: The Ghost, plus a roster-fit audit (2026-07-24, S170-32).** Before picking the next
hero, checked all 10 remaining roster entries against arena's actual structural constraints (1v1
only, self/foe targeting only — no allies, no `GridCell`/`alignment_pressure` territory system, no
multi-unit-per-player) rather than assuming every hero fits the way Unicorn/Duck did:

- **Blocked on the RED GARDEN grid** (Tree, Pizza, Druid, half of Doc Wheel) — their identity is
  `alignment_pressure`/`GridCell` interaction, which `arena_game.h` (`ArenaNode`, not `GridCell`)
  doesn't have.
- **Blocked on needing allies** (Doc Wheel fully; Frog's W partially).
- **Blocked on not being a piloted hero** (Retrieval Cart — "no active-use kit at all by design";
  Donkey — automatic HP-triggered unfold, never directly commanded).
- **Blocked on multi-unit-per-player** (TYLER — Meepo-style clones; `heroes[2]` is one unit per side).
- **Blocked on the cooking system** (Flamel — "entire kit is the cooking system made literal").
- **Actually fits**: Ghost and Frog. This audit is a real finding about arena's ceiling as a
  full-roster testbed, kept here even independent of Ghost's own build below.

Wired **The Ghost**'s Q (Alien Frequency: skillshot simplified to instant-hit-if-in-range, damage
+ Silence) and W (Not a Ghost: instant intangibility on its own cooldown, not a toggle like
Unicorn's) and R (Recital: fixed-position zone, enemy-damage side only — the ally-heal side has no
target in 1v1, flagged not faked). Passive (Mid-Piano, silent undodgeable casts) is a cast-
animation/UI concept with nothing to simulate here — skipped, flagged.

This is arena's first kit needing real status-effect state rather than just cooldowns/toggles:
added `silenced_ms` (blocks Q/W/R casts) and `intangible_ms` (blocks auto-attacks and ability
damage alike, via a new `hero_is_hittable()` check used everywhere a hit used to just check
`foe->alive`) as generic `ArenaHero` fields, not Ghost-specific ones — any future kit can apply
them to any hero. The zone's damage-over-time uses a fixed-interval tick (once per accumulated
1000ms, not fractional-per-tick DPS) so it's correct at any real frame rate — notably, this
sidesteps a real rounding bug already latent in Unicorn's W regen (which computes fractional HP
per 16ms tick and truncates to 0 almost every tick in real gameplay, only "working" in tests that
advance time in one big 1000ms step); that pre-existing bug is flagged here, not fixed, since
fixing it is unrelated to this pass's scope.

7 new headless tests, all green alongside the full existing suite.

**Fourth hero: The Frog (2026-07-24, S170-33)** — the last clean-fit hero from the audit above.
Q (Loop Back: rewind own position/HP to ~3s ago) needed arena's first history mechanism: a
per-hero ring buffer (`loopback_x/z/hp`, 16 slots at a 250ms sample rate — 4s of coverage), sampled
generically for every hero in `tick_hero_kit` the same way status effects are, not gated to
whichever kit currently reads it. If cast before 3s of real history exists (e.g. early in a match),
it rewinds to the oldest sample available rather than refusing to cast — an honest degrade, not a
silent no-op. R (The Secret) is simplified to reuse Ghost's `intangible_ms` mechanic at a longer
duration; "reappear at any visited location" needs its own location-memory system, not built here,
flagged as a simplification rather than faked as the full ability. W (Borrowed Time, ally-targeted)
and the passive (bluffing/UI-only) are skipped, same reasoning as other ally- and UI-dependent
skips elsewhere in this phase. Bot heuristic is defensive-shaped (rewind when hurt, vanish when
critical) rather than "attack when in range," since Frog has no damage-dealing ability at all. 4
new tests, all green alongside the full existing suite — including one proving the "not enough
history yet" degrade path specifically, not just the common case.

**Not done yet — the 8 heroes structurally blocked by the roster audit above** (Donkey, Tree,
Pizza, Retrieval Cart, Doc Wheel, TYLER, Flamel, Druid). Arena has now absorbed every hero that
fits its current constraints without arena itself growing new systems (allies, grid, multi-unit,
cooking) — the next roster move is either building one of those systems, or treating arena's
4-hero roster as complete for this testbed's purposes. That's a real decision point, not
continuing the same "pick the next one" pattern blindly.

**Decision made, allies built (2026-07-24, S170-45): `arena_nearest_ally`.** Founder chose
"build allies/multi-hero-per-team in arena" over the other options (build territory, or declare
the roster complete). Team-mode infrastructure (`ARENA_TEAM_SIZE`/`ARENA_MAX_HEROES`,
`arena_init_teams`, `arena_nearest_enemy`) already existed from the earlier 10v10 pivot — the
actual missing piece was purely an ally-targeting primitive, not a from-scratch system.
`arena_nearest_ally(int owner)` mirrors `arena_nearest_enemy` exactly (nearest active, living,
same-team hero, excluding self); `tick_hero_kit` gained an `ally` parameter alongside its
existing `foe` one.

**Unblocked and wired with the new primitive:**
- **Ghost's Recital (R), ally-heal side** — previously "only the enemy-damage side is
  implemented... flagged not faked." Now the zone's fixed-interval tick also heals a living ally
  standing in it, same rate as the enemy-damage side.
- **Frog's Borrowed Time (W)** — previously skipped entirely for having no ally target. Now
  places a generic `next_cast_refund` buff on the nearest ally; the *next* successful Q/W/R that
  ally casts (any hero, any ability — the mechanism is generic, not Frog-specific) has its
  cooldown refunded to zero instead of the normal value. Found and fixed one real bug in the
  headless test writing this: a test asserting the refund fired against a Unicorn with no move
  target and no foe — `unicorn_cast_q` returns early in that exact case (nothing to dash toward),
  so the refund path never actually executed; the assertion had been passing by coincidence
  (default cooldown state already read as 0), not because the refund fired. Fixed the test to
  give Unicorn a real move target so its Q genuinely executes.
- **Doc Wheel (Buer) — fifth hero kit, the first ally-only kit** ("the entire kit is being the
  correct ally to have nearby"). Passive (Extremely Good At Medicine) scales heal amount from
  `ARENA_DOC_WHEEL_Q_HEAL_BASE` at 100% target HP up to `..._LOW_HP` at 0%. Q (Bedside Manner):
  single-target heal + silence-cleanse on the nearest ally, whiffs (no cooldown consumed) with no
  ally. W (House Call): instant teleport to the nearest ally's position, long cooldown. R (No
  Combat Power, As Advertised): teamwide cleanse + heal to every ally in radius — **simplified
  from a literal absorb-shield**, which would need a new generic damage-absorption mechanic
  touching every damage call site in this file for one ability's sake; deferred rather than built
  shallow, flagged not faked, same reasoning as other simplified (not faked) pieces in this
  roster. Unlike Q, R always consumes its cooldown even with zero allies in range — a real
  ultimate commitment, not a whiff-refunded poke. The RED GARDEN passive (CORRUPTED-cell decay on
  heal) stays skipped — arena has no `GridCell`/territory system, same blocker as Tree/Pizza/
  Druid from the original audit.
- `apps/arena_bot`'s draft picker (`my_owner % 4` → `% 5`) and `apps/arena_server`'s pick-
  validation bound (`> ARENA_HERO_FROG` → `> ARENA_HERO_DOC_WHEEL`) updated so Doc Wheel is
  actually draftable over the wire, not just in headless tests.

**Verified live:** two separate real matches (10-bot and 20-bot lobbies) both drafted Doc Wheel
without incident — `CLIENT 4 picked hero_id=4`, `CLIENT 9 picked hero_id=4`, "All N heroes picked
-- match live" in both. 16 new headless tests (ally-targeting primitive, both ally-heal/refund
completions, and Doc Wheel's full kit) all pass alongside the complete existing suite.

**Not done, still real blockers:** Doc Wheel's teammates' 1v1-only bot heuristic
(`bot_cast_kit_if_ready`) has an intentional no-op case for Doc Wheel — that heuristic only ever
runs in the local 1v1 demo, where Doc Wheel's entire ally-dependent kit has nothing to do;
`apps/arena_bot`'s own simpler "cast Q periodically" heuristic already exercises it correctly in
team mode. The remaining 7 heroes (Donkey, Tree, Pizza, Retrieval Cart, TYLER, Flamel, Druid) stay
blocked on grid/territory or multi-unit-per-hero, neither of which this pass built.

**Phase E — Game AI: reuse existing org tech, don't invent a parallel stack.** Founder: "using the
full depth breadth and width of einhorn ai tech for games" → "incorporate all of the tech into the
REDGARDEN bots." A full-repo scan (610 `.md` files, EMILY/BACKLOG.md S170-19) found the pattern to
extend already written and spec'd: **`gpt2-alpine-c/docs/GAME_AI_NORTHSTAR.md`** (2026-06-18) —
GPT-2 as a game policy network (serialize state → generate action tokens → decode), with a
replay-log → fine-tune → self-play flywheel already milestoned end-to-end for SHANKPIT/BedWars.
REDGARDEN's own bot brain (`packages/simulation/arena_game.c`'s hand-authored feed-forward net,
explicitly the same shape as `SHANKPIT/packages/simulation/neural_net.h`'s trained one, with its
own code comment already calling a real training pipeline "a fast-follow") is the same lineage —
this phase is that fast-follow, applied to REDGARDEN specifically rather than reinvented:
- Extend `GAME_AI_NORTHSTAR.md`'s state-serializer/action-decoder pattern to REDGARDEN's state
  (card-hand economy + living grid for the RTS mode, hero/ability state for the MOBA mode) instead
  of a REDGARDEN-specific format designed from scratch.
- Phase B's replay logs are the training corpus — the same "no replay data → no fine-tune data"
  dependency `GAME_AI_NORTHSTAR.md` Milestone 7 already names.
- **NORN's propose→grade→gate→promote loop kernel** (`pkg/norn`, `EMILY/docs/hq-specs/HQ-SPEC-
  PRIME-101-norn-loop-kernel.md`) is the natural fit for formally evaluating each bot generation —
  `GAME_AI_NORTHSTAR.md` Milestone 10's own acceptance criterion ("second-gen bots measurably
  different from first-gen") is exactly a NORN grading job, not a manual eyeball check. "Bots need
  personalities that evolve and learn on their previous matches" is this flywheel, named plainly.

**Started 2026-07-24 (S170-36) — Milestone-6 equivalent: state serializer + action decoder for
arena.** `GAME_AI_NORTHSTAR.md` itself calls this milestone "the contract... everything downstream
depends on" — the right first slice, not the full fine-tune/self-play loop (Milestones 7-10 need
an external Colab GPU run and a human to trigger it, not buildable end-to-end in this
environment). New `packages/simulation/arena_ai_bridge.h`/`.c`:

- `arena_serialize_state(owner, tick_ms, ...)` writes a stable, natural-language state string from
  either hero's point of view (`self`/`foe`, matching `GAME_AI_NORTHSTAR.md`'s own SHANKPIT
  framing) — hero name, position, HP, and every generic cooldown/status field Phase D
  introduced (`q_cd`, `w_active`, `w_cd`, `r_cd`, `r_active`, `silenced`, `intangible`). Same
  input always produces the same output.
- `arena_decode_action(...)` parses a `"move:x,z cast_q:0/1 cast_w:0/1 cast_r:0/1"` action string
  back into a move target + cast flags, defaulting missing fields to a safe no-op rather than
  garbage, and failing closed (returns 0, "do nothing") on a string with nothing recognizable in
  it at all.

7 new headless tests, all green alongside the full existing suite. **Not wired into the live bot
yet** — this is the contract only; feeding it into `bot_cast_kit_if_ready` (or replacing it) via
an actual `:8088` GPT-2 inference call, and the replay-log → fine-tune → self-play flywheel behind
that, are separate, later slices gated on this contract existing first — same sequencing
discipline used between Phases B and C.

Each phase depends on the one before it — the sequence is the plan, not just the list.

## 13. Product pivot — the MOBA is the product (2026-07-24)

Direct founder correction, in order: "i need pvp not the autometa pvp that got validated as
boring" → "this is a fucking pivot as i framed it" → "the card game is fucking boring" →
"cancel it" → "pivooooot to the moba." This followed an attempt (mid-plan-mode, canceled before
any code was touched) to scale bot-vs-bot matchmaking to a 10v10 automated-battle mode — the
founder rejected that outright: bot-vs-bot "autometa" combat (bots walking into range and
auto-trading hits) has already been judged boring, and scaling the team size to 20 heroes doesn't
fix that, it just produces more of it.

**`apps/arena` — the hero click-to-move MOBA — is REDGARDEN's real product now.** Everything in
§1-§12 above (the Clash Royale card-hand RTS, `apps/server`/`packages/simulation/local_game.c`,
the matchmaker/WOTAN work built against it) stays as real, working, tested infrastructure — it is
not being ripped out — but it is no longer what new work builds toward. `apps/arena`'s own history
this session is real too and doesn't restart from zero: colored-cube heroes with basic shading,
right-drag-orbit + scroll-zoom camera, click-to-move with an animated ring marker, a 4-hero roster
with real ability kits (Unicorn/Duck/Ghost/Frog, §12 Phase D), and — as of S170-41 — real WOTAN
player identities and match-result reporting, all headless-tested and, for the sim logic, verified
without needing a display.

**What "real PvP, not autometa" means concretely: `apps/arena` has zero networking.** A human can
run the SDL2 client and fight the sim's own hand-authored bot locally, but there is no way for a
second connection — human or bot — to join that same match over a network. That is the actual gap
between "bots fighting bots" and "PvP": the next real technical priority is giving the existing,
already-playable client a way to connect to a real match server, so a human's own input drives a
hero instead of only ever facing (or being) a bot brain. **Explicitly deferred until 1v1
human-playable networked PvP is proven fun:** 10v10, N-player lobbies, persistent bot fleets, team
assignment. Scaling team size before the core loop is confirmed fun was the mistake in the
canceled plan; not repeating it here.

**Done — real 1v1 networked PvP, same day.** New `apps/arena_server` (server-authoritative UDP,
2 hero slots, ports the proven connect-ticket/WOTAN pieces from `apps/server` rather than
re-deriving them) and a `--connect <host>` mode added to the existing `apps/arena` client (network
handshake happens before SDL/window creation, so it's fully testable on a headless box up to that
point). New wire packets in `protocol.h` (`PACKET_ARENA_MOVE/CAST/SNAPSHOT`). The existing
matchmaker is untouched — this is a direct-connect first step, matchmaking for the MOBA is a
later slice.

Verified live, twice, catching two real bugs along the way rather than assuming the first pass
was correct:

1. **Kit-cast bug.** `arena_bot_enabled` (added to stop the internal bot from *moving* owner 1 once
   a real second player connects) didn't also gate `bot_cast_kit_if_ready` — a real second player's
   hero was still getting autonomously yanked and attacked by the bot's kit AI (Duck's Q pulls the
   foe), found by testing against a real server, not by review. Fixed by gating both calls the
   same way; a regression test (`test_arena_bot_enabled_gates_kit_casts_too`) now covers it
   headless.
2. **Sim-clock-starts-too-early bug.** With only one real client connected, the default
   `arena_bot_enabled=1` (correct for local solo-vs-bot play) meant the bot immediately started
   fighting an empty second slot, and the match could fully resolve before a second real player
   ever got the chance to join. Fixed at the server level: `arena_update()` only runs once both
   real slots are filled (`client_active[0] && client_active[1]`) — before that, the match idles,
   broadcasting a static "waiting" snapshot.

Final verified state: two real clients, each with a distinct real WOTAN identity, connect to
`apps/arena_server`; the match correctly waits with both heroes stationary until both are present;
once both connect, the internal bot is fully disabled (movement and kit-casts alike) and both
heroes sit at full HP with no unprompted movement or combat — genuine PvP, waiting on real input
from both sides, not two bots fighting each other. `scripts/test_arena.sh` and
`scripts/test_10_bots.sh` both re-verified clean.

**Still not done, on purpose:** the client's rendering/input loop for a live network match hasn't
been visually verified on this box (no Xvfb, same standing constraint as the local demo). Match
results reporting to WOTAN reuses `report_match_result`'s exact shape but posts under
`"game":"redgarden-arena"` rather than `"redgarden"`, correctly keeping card-RTS and MOBA stats
separate on the same genre-agnostic `player_game_stats` table. Matchmaking (vs. direct `--connect`)
for the MOBA, 10v10, and everything else in §13's original deferred list stays deferred.

**Update, same day — "22 bots in the pool," a real persistent bot pool via real matchmaking.**
Founder, direct: "10 v 10 22 bots in the pool" → "i did not ask you to wait for human validation
we have a deadline keep building" → "the human will join the bot games to validate for now bot
first feedback loop." Read plainly: bots keep the world populated and playing continuously; a human
drops in to validate, rather than 1v1-human-PvP being a hard gate before any scaling work starts.

Shipped, in order:
- **Team-mode sim** (`packages/simulation/arena_game.c`/`.h`): `ArenaHero` gains `team`/`active`;
  `heroes[2]` grows to `heroes[ARENA_MAX_HEROES]` (20, `ARENA_TEAM_SIZE`=10 per side).
  `arena_nearest_enemy()` generalizes what used to be a hardcoded "the other slot" foe lookup —
  `arena_cast_q`/`toggle_w`/`arena_cast_r` now use it (with NULL-safety added for "no living enemy
  right now"), verified to produce byte-identical behavior for the existing 1v1 path via the full
  pre-existing test suite (zero regressions). New `arena_init_teams()`/`arena_update_teams()` are
  additive — the 1v1 local demo's own functions are untouched. 5 new headless tests cover team
  init, nearest-enemy targeting (including multiple attackers converging on one target — a real
  team-fight case the old pairwise combat never had to express), and team-wipe win condition.
- **Draft phase**: heroes were hardcoded (Unicorn vs Duck); new `PACKET_ARENA_PICK` +
  `ARENA_PHASE_WAITING/DRAFT/LIVE` — the match clock only starts once every real slot has both
  connected *and* picked a hero.
- **`apps/arena_server` generalized** to `--lobby-size N` (default 2 — the original 1v1 path is
  byte-for-byte unchanged behavior). `ArenaSnapshotMsg` gained a `count` field (same
  "count + fixed array" convention as `NetEntity`/`entity_count`) so the wire format scales from 2
  to `ARENA_SNAPSHOT_MAX_HEROES` (20) without a second message type.
- **`apps/arena_bot`** (new binary): a real networked bot — not the sim's internal hand-authored
  practice AI, which is explicitly disabled the moment any real client connects. Gets a real WOTAN
  identity (register+ticket-mint, ported from `apps/client/bot_main.c`'s pattern), queues via a
  matchmaker, drafts a hero, plays using only the snapshot data any client sees (no access to the
  authoritative `ArenaState`), and loops back to the matchmaker after the match ends — genuinely
  persistent, not a one-shot script.
- **`apps/matchmaker` generalized**: `--lobby-size`/`--listen-port`/`--first-game-port` flags, one
  binary now serves both the original card-RTS pairing role and an arena N-player lobby role
  (`--server-bin ./build/red_garden_arena_server`), passing `--lobby-size` through to the spawned
  server.

**Three real bugs found and fixed by actually running a persistent bot pool, not by review:**
1. A "persistent" bot was re-registering a **brand-new WOTAN identity every single match**
   (`provider_sub` keyed off `time(NULL)`, called on every reconnect) instead of keeping one stable
   identity — confirmed live via `player_game_stats` showing dozens of one-match player rows
   instead of a growing record. Fixed by registering once per process lifetime and only re-minting
   the ticket (which is meant to be short-lived) on each reconnect.
2. **Match servers never terminated after the match ended** — they kept broadcasting
   `PACKET_ARENA_SNAPSHOT` forever to clients that had long since moved on to their next match.
   For a persistent bot (one UDP socket reused across many matches, no `PACKET_DISCONNECT` in this
   protocol), every prior match server it ever played kept blasting stale packets at its socket,
   and that pileup was silently swallowing the real `PACKET_WELCOME` for its *next* connection —
   not a client bug, a server-lifecycle bug, seen live as intermittent "failed to connect."
   Fixed: the server now does a few final broadcasts after the winner is set, then exits for real.
3. **A UDP retry race in the matchmaker protocol**: the bot's `PACKET_FIND_MATCH` retry (originally
   every ~1s) could arrive at the matchmaker just after it had already paired and dequeued that
   client, silently re-enqueuing a phantom entry nobody would ever come back to claim — later
   falsely paired with a genuinely new client, spawning a match only one side ever connects to.
   Found live: spawned match-log files with a `match_start` and nothing else, ever, despite zero
   logged connect failures on either bot. Mitigated (not fully eliminated — this is a same-box
   UDP protocol with no idempotency/ack, a deeper fix is a real follow-up, flagged not silently
   left) by slowing the retry interval to ~5s, far outside a same-box matchmaker's normal
   millisecond reply time. **Defensive complement, since the race isn't fully closed:** the server
   now also self-terminates if a lobby makes no real progress (never reaches `ARENA_PHASE_LIVE`)
   within 60s, so any phantom that still slips through cleans itself up instead of leaking forever.

**Verified live, extensively — a real soak test, not a single match:** two persistent
`apps/arena_bot` processes, real WOTAN identities, playing through a real arena matchmaker
(`--lobby-size 2`) continuously. Confirmed: identity stays stable at exactly 1 registration per bot
across 20+ matches each; win/loss records accumulate correctly (`player_game_stats` showing bots
with 23 matches played, real accumulated W/L); zero connect failures after the retry-interval fix;
phantom match servers (when they do occur) self-terminate within roughly 60-100s instead of leaking
forever (confirmed by watching one exit in real time, not just inferring it).

**Update, same day — the actual 10v10 path run live end-to-end, not assumed passing by extension.**
`--lobby-size 20` against 20 real `apps/arena_bot` processes: all 20 connected, all 20 drafted a
hero (team 0 = owners 0-9, team 1 = owners 10-19, confirmed correct in the wire snapshots), combat
resolved across 20 heroes simultaneously, and the match correctly ended on a real team-wipe
(`{"event":"match_end","winner":1,...}` with team 1's owners 10-19 all showing `alive:0` and team
0 having real survivors at real HP values — not a forced/simulated result). All 20 bots then
persisted and requeued into a **second** full 20-player match without any manual intervention —
confirmed via every bot log showing exactly 2 "match ended" lines and still exactly 1 "WOTAN: real
identity" line each (the identity-persistence fix holds at 20-bot scale, not just 1v1). Server
process count stayed healthy (single digits) throughout, not the dozens-of-zombies pileup seen
before the shutdown-timer/timeout fixes. This closes the "still unverified" gap from earlier the
same day — 10v10 is real, not just headless-tested code assumed to generalize correctly.

**Still genuinely unverified, honestly flagged:** the human-facing SDL2 client's rendering of a
live networked match (team-colored heroes, HUD, camera) has not been visually confirmed on this
box (no Xvfb) — everything verified above is from the wire protocol and match logs, not from
looking at a rendered frame. That remains the one real gap between "the system works" and "a human
has seen it work."

**A real gap closed, 2026-07-24 (S170-44): a human player can now join whatever match the bot
pool is currently matchmaking into.** Until today, `apps/arena`'s human client only supported
`--connect host:port` — a direct connection to an *already-known* server address. That's useless
against a persistent bot pool, where the matchmaker dynamically assigns a new port per match; there
was no way for a human to actually queue into the same pool the bots were playing in. Added
`--queue <matchmaker_host>` (`--matchmaker-port`, default 7778, matching `apps/arena_bot`'s
existing default): sends `PACKET_FIND_MATCH` to the matchmaker, waits for `PACKET_MATCH_FOUND`
(same ~5s retry interval as `apps/arena_bot`'s own queue logic, for the same same-box-race reason),
then reuses `net_connect`'s existing ticket-mint/`PACKET_CONNECT` handshake against whatever port
comes back — no new server-side code needed, this was purely a client-side gap.

**Verified live:** started a real matchmaker (`--lobby-size 2`) + one persistent `apps/arena_bot`,
then ran `red_garden_arena --queue 127.0.0.1`. Confirmed end to end: `Queuing for a match...` →
`Match found on port 7510 -- connecting...` → `Connected -- assigned hero slot 1`, matched against
the bot (which logged `connected -- hero slot 0`) on the *same* spawned server. This is the join
mechanism fully proven at the protocol level — the human client reached the same match a live bot
was in, via the same pool, the same way a bot would. (First attempt at this test failed with the
server rejecting all connects; root cause was a test-setup mistake, not a code bug — the matchmaker
process had been started in a shell missing `REDGARDEN_TICKET_SECRET`, so the arena_server it spawned
inherited no ticket secret and failed closed, correctly, exactly as designed. Restarting the whole
stack with the secret actually exported fixed it immediately.)

**Still bounded by the same known gap, not a new one:** the client then hit `SDL_Init`/window
creation with no display (no Xvfb on this box, same limitation noted above) before it could send a
draft pick or move — so no full match was played end-to-end visually. The *join* is proven; playing
once joined still needs either a real display or Xvfb, unchanged from before.

**S170-14 (2/3): the player-only pool, verified live.** Two of the three matchmaking pools the
founder asked for (bot games, player-only, ranked) are now scoped: bot games was already S170-43's
persistent pool; ranked stays explicitly undesigned (no rank model, MMR, or queue rules exist —
not a code gap, a design gap). The player-only pool is now real: `scripts/launch_arena_pools.sh`
stands up a **second, entirely separate matchmaker instance** on its own port (7779, `--lobby-size
2`), with zero bots ever configured to queue into it — pool separation is operational (two
processes, two ports), not a new access-control layer inside the matchmaker, matching this
codebase's existing pattern of generalizing one binary via flags rather than building bespoke
machinery per mode. Lobby size is 1v1, not 10v10 — with near-zero real concurrent human players
today, a 10v10 player-only queue would never fill; 1v1 is the smallest already-verified real-PvP
case (S170-42), the same "don't build for traffic that doesn't exist yet" reasoning as not running
S24-05's load test without real traffic. **Verified live:** ran the bot pool (2 bots, `--lobby-size
20`) and the player-only pool simultaneously; two real `red_garden_arena --queue` human clients
matched into a genuine 1v1 on the player-only pool's own spawned server (port 7600), which logged
`Lobby full (2 players) -- internal bot AI disabled, entering draft` — the standard 1v1 no-bot
path. Cross-checked both directions: the player-only matchmaker's log shows only those two human
connections, ever; grepping every bot's log for the player-only pool's ports (7779/7600) found
nothing — confirmed clean isolation, not just assumed from the two ports being different.

**S170-14 (3/3): ranked pool — design pass, `docs/RANKED_MATCHMAKING.md`.** The last of the three
pools was explicitly a design gap, not a code gap, so this pass writes the design rather than
skipping ahead to code against an undecided rating model. Recommends plain ELO (K=32 flat, no
provisional-period scaling, starting rating 1000) over Glicko/TrueSkill — those solve a rating-
uncertainty problem that doesn't exist yet in a symmetric 1v1-only pool with no team-composition
variance; revisit if ranked ever grows past 1v1. A new `redgarden_ranked_stats` table, kept
separate from casual `player_game_stats` (ranked rating and casual win/loss are different
questions, conflating them would corrupt the already-shipped casual leaderboard). Queue rules:
widening rating-search-window matching, which doesn't fit the existing spawn-on-fill
`apps/matchmaker` binary as-is — a real queue rewrite, explicitly scoped as its own future pass,
not bolted on as a flag. Design only; no schema, endpoint, or queue code landed. Golden-indexed
as REDGARDEN-RANKED.

**S170-46/47: territory system + five new heroes (Tree, Pizza, Flamel, Morrigan, Dagda) — the
roster more than doubles, 5 → 10.** Direct continuation of the allies/Doc Wheel pass's own roster
audit: once allies were exhausted, three systems remained blocking the rest of the queued heroes
(territory/resource economy, multi-unit-per-player, non-piloted units). Asked directly which to
build next; founder picked **territory/resource economy** — it unblocks the most heroes at once
(Tree, Pizza, and what was then still a separate Druid) and is Flamel's own cooking prerequisite.

**The territory system itself:** the two `ArenaNode` markers already existed but were purely
decorative (rendered as flat placeholder markers in `apps/arena/src/main.c`, zero gameplay logic).
Extended with `pressure` (signed, -100..100), `owner` (derived from a threshold crossing),
`marked_by_team`/`mark_ms_remaining` (Flamel's Overgrowth marking). `arena_tick_nodes()` sums
weighted living-hero presence per team within a capture radius each tick (Tree counts double, Root
Network), drifts pressure toward whichever team is ahead (or decays toward neutral if tied), and
recomputes owner. Called from both `arena_update()` (1v1) and `arena_update_teams()` (team mode)
with zero special-casing — the same "generalizes cleanly" precedent as `arena_nearest_ally`/
`arena_nearest_enemy` before it. A `apply_damage()` helper was added to centralize every damage
call site's HP-floor/death logic in one place (previously duplicated at each site) — needed for
real, not a nice-to-have refactor, since Pizza's R is an actual damage floor status effect (not a
simplified-away shield the way Doc Wheel's R was) and every damage site needed to honor it
consistently.

**Founder mid-build redirect: "druid and flamel should be the same hero."** Arrived after the
territory design was settled but before any hero code was written. Cross-checked `TYLER/
multiverse_heroes.md` first: "Druid" had zero lore entries anywhere — a pure REDGARDEN-side generic
archetype — while Flamel (#110, Nicolas Flamel) is a fully-realized named historical figure. Kept
Flamel's name/identity, folded Druid's kit into it as flavor (his alchemy *is* literal cultivation):
Passive merges Great Work + Overgrowth (marking); Q is Druid's self-contained Vine Growth (root, no
economy dependency); W merges Bloom + Philosopher's Batch into one AoE ally heal with a marked-
ground bonus; R merges Elixir of Life's team-ultimate framing with Wild Growth's AoE shape (zone
root + heal-over-time + mass-mark). Documented in `docs/HEROES_VS0.md` before any code, same
docs-before-software discipline used for every other hero this session.

**Tree and Pizza** built against the same territory hooks: Tree's Root Network passive needs no
ability code at all (arena_tick_nodes reads hero_id directly); its Q/R are cone/until-recast
abilities simplified to the same instant-range-check and fixed-duration patterns already
established for Ghost/Frog. Pizza's Uninvestigated Fire is a real always-on burn aura (not cast-
gated) plus a node-corruption pull (simplified from the doc's true 4-state CORRUPTED concept to
"pulls contested pressure toward neutral," flagged); its R (a real HP-floor status, not simplified
away) is what forced the `apply_damage()` centralization above.

**Then, mid-session, two more founder-driven additions on top of the same pass: "add the morrigan
as a meta jungler for the dynamic jungle," then "add the other irish guy too with the two natured
hammer."** Checked `TYLER/multiverse_heroes.md` before designing either — both are real, adjacent
entries (#68 Morrigan, #69 Dagda), and `docs/HEROES_VS0.md` already had a "flagged, not built" note
about a Morrigan/Druid counter-play relationship from an earlier pass, now resolved for real against
Flamel instead of the discarded Druid name. No standalone jungle-camp system exists in this arena,
so Morrigan's "jungler" identity was built as an affinity for *contested* (not yet claimed) node
ground, rather than inventing a second system alongside the one just built. Dagda's signature "same
club, either direction" is implemented literally in his Q: swings the killing end at a hittable
enemy in range if one's there, otherwise the reviving end (simplified to a strong heal — no respawn
system exists to revive a dead ally into) on a hurt ally instead.

**Verified live** across the full 10-hero roster: relaunched the bot pool (`scripts/
launch_arena_pools.sh start 20`) and grepped every persistent bot's draft log — hero_ids 0 through
9 all drafted successfully (2 picks each across 20 bots), confirming the pick-validation bound
(`apps/arena_server`) and draft modulo (`apps/arena_bot`) were both correctly widened from the
previous 8-hero roster. Headless coverage: 62 new assertions across territory mechanics and all
five heroes (`tests/test_arena_game.c`), all passing (216 total, up from 154 before this pass) —
including one caught-and-fixed test bug of the same shape as this session's earlier Frog test bug:
an initial exact-value assertion on Morrigan's R execute-tick damage was invalidated by an
unaccounted-for melee auto-attack landing in the same update tick, and separately by HP-floor
clamping at 0 making an exact post-damage value impossible for the near-dead case — fixed by
comparing damage *deltas* across two isolated setups (matching the pattern already used for
Doc Wheel's and Morrigan's own HP%-scaling tests) instead of asserting an absolute value.

**S170-48: The Courier (Ratatoskr, TYLER #32) — eleventh hero, roster 10 → 11.** Founder: "add The
Courier (ratatoskr)." TYLER's #32 entry is already nicknamed exactly "The Courier" — the messenger
between the eagle at Yggdrasil's crown and Nidhogg at its root, who's "started editing" the
messages after a long tenure. That two-fixed-endpoint framing maps directly onto this arena's two
existing `ArenaNode` positions rather than needing a third system: The Courier's W (Between Eagle
and Serpent) is a pure fixed-geography teleport — always jumps to whichever node is farther away,
distinct from every other hero's ally/foe-relative teleports. Q (a dash-strike, same shape as
Unicorn's Diagnostic Charge) doubles as the passive's trigger: a landed cast cleanses The Courier's
own active debuffs ("editing the message" addressed back to him). R (The Debt Collector's Due) is a
flat life-drain execute on the nearest enemy — "a job that was never meant to involve judgment, and
has, over a very long tenure, started to." 7 new headless tests (223 total). Pick-validation bound
and draft modulo widened once more (10 → 11). Verified live: relaunched the persistent bot pool
(22 bots) after a stray-process port conflict from the previous session's leftover matchmaker was
cleaned up (`pkill -9 -f red_garden`, then a clean relaunch) — all 11 hero_ids (0-10) drafted
successfully, pool left running on the current build.

**S170-50/51: territory capture redesigned from ambient pressure to a real Arathi Basin-style
channel, plus territorial jungle creeps.** Founder, mid-session, direct and specific: "we need the
arathi basin true click to channel capture interruptable a neutral period after the flag flips as
you wait for it to finish capturing — adds objective-focused play and the possibility of losing
due to ignoring the objective, not just presence-based." The old model (S170-46: signed `pressure`
drifting toward whichever team had more weighted bodies nearby, owner derived from a threshold) is
gone entirely — that model *was* the "just presence based" thing being replaced, not something to
layer under the new one.

**New model:** exactly one team can channel a node at a time. Exclusive single-team presence
starts or continues that team's channel; the instant a channel starts against a node NOT already
owned by the channeling team, the node flips to neutral *immediately* — the "neutral period... as
you wait for it to finish capturing" the channel spends genuinely open and uncaptured for its whole
duration, not just at the end. Mixed presence, a Pizza's corruption (redesigned from a pressure-pull
to a hard channel-interrupt, still "regardless of team composition"), or the channeling team
leaving all interrupt it — progress resets to 0 and the node does **not** revert to its pre-channel
owner. A defender who denies an attacker doesn't get the node handed back for free; they have to
start their own channel too. This is the actual teeth behind "losing due to ignoring the
objective." Tree's Root Network redesigned from a doubled capture-weight to a doubled channel-speed
multiplier; Flamel's Overgrowth mark redesigned from a pressure-pull bonus to a flat channel-speed
bonus on the marking team's own capture — same flavor, new mechanic underneath both.

**Two more authentic Arathi Basin rules added on top, both founder-specified in exact, recognizable
terms:** (1) *"capturing a flag start channel breaks stealth"* — interacting with the flag reveals
a stealthed capper (Frog's R, the only real stealth in this roster: "vanishes... can't be targeted
or seen") the instant their channel starts, not before and not for its whole duration — the sneak
-in part of the moment is over the moment the channel begins; whether the crowd standing there
reacts in time is down to their own attention, not a standing invisibility loophole. (2) *"hitting
the channeling character interrupts the capture"* — a new generic `damaged_this_tick` flag, set by
`apply_damage()` (every damage source in this file already routes through it, so this needed no new
call sites), checked by `arena_tick_nodes` to interrupt a channel the instant any hero of the
channeling team takes damage in radius — real WoW Arathi Basin's own flag-channel pushback rule.
Required moving `arena_tick_nodes`'s call site to run *after* combat/kit-ticks in both
`arena_update()` and `arena_update_teams()` (previously first) so it can see the whole tick's
damage before deciding whether anyone's channel survives it.

**The archetypal moment itself, brought forward on purpose and explicitly requested:** *"like a
stealthed character shooting in and [ninja]ing an objective while 6 clueless opponents run around
nearby... a lineage of WoW Arathi PvP."* A lone stealthed hero can channel-capture a node with a
crowd of visible enemies standing right on top of it, undetected, for as long as the sneaking-in
part lasts — the stealth-exception rule above (if a team's *entire* presence at a node is
stealthed, the other team's presence never registers a contest) makes this a real, reproducible
mechanic, not just flavor text; a dedicated headless test proves it directly (six visible enemies
in radius, one stealthed capper, channel proceeds anyway).

**Territorial dynamic jungle creeps, the other half of the same request:** *"territories are how
you control macro and economy, objectives are how the game is won... territory advantage gives the
ability to influence the meta in terms of what dynamic territorial creeps emerge... controlling the
flavor and cadence of the jungle helps create the meta to counter certain comps."* One creep per
node, index-matched, re-rolled on every respawn from that node's *current* owner (not fixed at
spawn) — the jungle's own population reacts to who controls the ground under it, matching the
earlier NORTHSTAR §8 "alive and dynamic, not static camps" direction, now built rather than just
specified. Two flavors, two different rewards, not just two HP totals: a **contested node's** creep
is rare, tanky, slow-respawning — killing it (only while your team is actually channeling that
node) grants a large one-time capture-progress bonus, a real tempo swing worth fighting over
regardless of side. An **owned node's** creep is common, weak, fast-respawning — killed by the
*owning* team it's a small steady home-turf-resupply heal; killed by the *opposing* team (while
they're channeling to flip it) it's a smaller capture-progress kick instead — a real counter-play
tool against a team that's turtled onto a lot of territory, matching "helps create the meta to
counter certain comps or play styles" directly, not just flavor. Numbers are the difficulty-tiering
*spirit* of GoblinFoxDragon's real mob archetypes (`server/mob/hills.go`: passive-until-attacked
low-HP vs. a tougher, rarer target) adapted rather than ported verbatim — GFD's mobs carry a full
aggro-cone/leash-range system this arena's click-to-move model has no equivalent for.

28 new headless assertions across this pass (251 total). Verified live: rebuilt and relaunched the persistent bot pool
with real WOTAN credentials now exported (`IDUNA_AGENT_NAME=REDGARDEN-BOTS`) so bot match results
actually post to the leaderboard (previously silently falling back to self-minted tickets all
session, since those env vars were never set when launching the pool — fixed as a prerequisite to
"track the stats of the bots across matches," the other half of this same request); confirmed a
real ~2.5-minute, 20-hero match ran to completion end-to-end on the redesigned system without
crashing (291 logged snapshots, a real `match_end` event). One separate, pre-existing operational
quirk noted, not caused by this pass: the matchmaker occasionally races a freshly-spawned server's
socket bind against clients' immediate connect attempts under rapid repeated match cycling; bots
self-heal via their own retry loop, and a single human `--queue` connection is far less likely to
hit it than 20 bots cycling in a tight loop the way this pass's stress-testing did.

**Replays, the other founder ask this same session ("also watch replays"), status: not built this
pass.** `packages/simulation/arena_replay.c`'s existing parser only understands the old 1v1
`hero0`/`hero1` snapshot shape; `apps/arena_server`'s team-mode snapshot log already writes a
richer `heroes:[...]` array (owner/team/x/z/hp/alive) but omits `hero_id` (no way to know which
sprite/kit to render per owner) and has no `ability_cast` logging at all. Extending the parser to
the array format, adding `hero_id` to the snapshot log, and building a team-mode playback path
(`arena_init_teams`-shaped, not `arena_init_with_heroes`-shaped) is real, scoped, separate follow-on
work -- flagged here rather than silently rolled into this pass, which was already large.

**Observation-phase prep: memorable bot names, real WOTAN identities confirmed active.** Founder:
"prep for an observation phase i want to watch the stats of the bots evolve as games progress and
the bots should have interesting memorable names." Two real gaps closed, not just one wish granted:
(1) discovered mid-pass that the persistent bot pool had been running all session on *self-minted*
tickets, not real WOTAN identities -- `IDUNA_AGENT_NAME`/`IDUNA_AGENT_SECRET` were simply never
exported when launching it, so every bot's match results were silently going nowhere. Fixed
operationally (the code path already existed, correctly, from S170-41): relaunched with real
`REDGARDEN-BOTS` credentials exported, confirmed via `/api/v1/players/{id}` and the public
leaderboard that stats now genuinely accumulate. (2) `apps/arena_bot` never sent IDUNA's own
`display_name` field on registration (`POST /api/v1/players/register` silently defaults to
`player-<8 chars of provider_sub>` when it's absent) -- added a curated 25-name pool (Irish/Norse-
flavored, matching this roster's own mythological register: "Copper Crow," "The Undry Cup," "Ash
Ratatoskr," etc.) plus a `--index N` flag so `scripts/launch_arena_pools.sh`'s spawn loop assigns
each pool slot a stable, non-colliding name across restarts (falls back to a pid-derived pick for
direct/manual launches without the flag). Verified live: queried a real registered player_id back
through IDUNA and confirmed `display_name":"Rootbound"` came back correctly. The actual watch
surface already existed from earlier this session: `okemily.com/tournaments.html`'s live REDGARDEN
leaderboard section, fetching the same public `/api/v1/redgarden/leaderboard` endpoint -- confirmed
reachable from outside this box. Nothing new to build there; the founder can watch the named bots'
records evolve at that URL as the persistent pool keeps playing.

## 14. Draft-phase bans — decided against for now (2026-07-24, S170-56)

Real design conversation, not a spec handed down whole. Worth keeping the reasoning, not just the
conclusion, because the reasoning is what should govern this decision if it gets revisited later:

Founder's starting ask was concrete — 3 bans per team, added to the existing draft phase. What
followed was working through the actual order live: an attempted literal ban/pick sequence, a
self-correction ("but in reverse"), an "or something" acknowledging the sequence itself wasn't the
real question yet, a specific structural idea (last 2 bans land right before the last 4 picks —
1 ban + 2 picks per team, back-loaded rather than front-loaded), a genuine open question about
*starting* on a ban vs. a pick, and a real concern surfacing mid-thought: **starting the draft on
a ban optimizes for a toxic community dynamic (banning what you personally dislike) over a
meta-focused one (addressing what's actually strong).** That concern is the load-bearing part of
this whole thread.

"Use fibonacci to figure it out" followed as a heuristic suggestion for shaping the pick/ban
cadence (an escalating or structured rhythm rather than strict alternation) -- floated, not
committed to, before the thread resolved.

**Resolution: skip bans entirely for now.** Founder's own close: "i really think actually skip
bans all together for now it has a huge impact." Read plainly: bans are a high-leverage,
hard-to-reverse social-dynamics lever (they shape whether a community forms around counter-play
and meta-adaptation, or around punishing whatever's currently popular), and that's not a decision
to bolt on as a minor draft-phase feature without first knowing which community dynamic the
11-hero (soon 12) roster is actually cultivating. Nothing built. If bans get revisited later, the
open questions above (order, back-loading, ban-vs-pick-first, the toxicity-vs-meta framing
specifically) are the actual design surface to resolve -- not just "add 3 bans per team" as
originally framed.

## 15. Camera lock/unlock + fog of war (2026-07-25, S170-125) -- spec only, no code yet

Founder, real-time: "specdd unlockable and lockable camera and fog of war." Read as: capture this
as a real design, same treatment as §14's draft-ban thread -- not "add 3 bans per team" scope, an
actual spec with real open questions named, before anything gets built. Two related but separable
features.

### 15.1 Camera lock/unlock

Today (`apps/arena/src/main.c`, per the README's "How to Play" section): the camera is always in
free-orbit mode -- right-click-drag rotates yaw/pitch, mouse wheel zooms, and it never
automatically re-centers on anything. That's *unlocked* by definition; there's no locked mode to
toggle away from yet.

**Proposed locked mode:** camera yaw/pitch/distance become read-only (right-drag/wheel become
no-ops, or are simply not gated -- open question below) and the camera hard-centers every frame on
`arena_state.heroes[my_owner]`'s current position, same `focus_x`/`focus_z` the unlocked mode
already computes for its orbit pivot -- locking removes the player's ability to *look away* from
their own hero, it doesn't change what "centered on" means.

**Toggle:** a dedicated key, not currently bound to anything -- `C` for "camera," clean of every
existing binding (left-click move, Q/W/E abilities, right-click-drag rotate, wheel zoom, F11 APM,
R local-restart). Starts unlocked (today's behavior), matching "don't regress what already works."

**Open questions, not resolved here:**
- Does locked mode still allow *zoom* (wheel) while forbidding rotation, or lock both? Most real
  MOBAs (League, Dota) lock rotation/pan but leave zoom free -- likely the right default, not
  confirmed with the founder yet.
- Should locking be per-player-preference (persisted somewhere) or always start unlocked each
  match? No account/settings-persistence layer exists yet for this client at all (S170-105-
  adjacent territory -- accounts exist for connect-tickets, not client-side preferences), so
  "always starts unlocked" is the only option that doesn't require new infrastructure.

### 15.2 Fog of war

**Explicit scope decision (founder confirmed, 2026-07-25): client-side visual only for a first
pass, not real server-side vision culling.** Named and accepted, not hidden: a modified/custom
client could still see everything, because the server would keep broadcasting every hero's real
position in the snapshot regardless of who's "supposed" to see whom -- this pass only changes what
the *stock* client chooses to render. Real anti-cheat fog (server only ever sends a client the
subset of `ArenaHeroSnapshot` entries their team can actually see) is a materially bigger change --
touches `server_broadcast()`'s per-client payload (today one identical broadcast goes to everyone),
needs a real per-team vision-set computed every tick, and is explicitly **deferred, not this pass.**

**Proposed first pass, purely client-side (`apps/arena/src/main.c`):**
- A fixed vision radius around the local player's own hero (`arena_state.heroes[my_owner]`) --
  reuse an existing distance constant in the same neighborhood as `ARENA_NODE_CAPTURE_RADIUS`/
  `ARENA_CREEP_AGGRO_RADIUS` for a sense of scale, not invented from nothing.
- Enemy heroes (not allies -- allies should always be visible, matching every real MOBA's "you
  always see your own team" convention) outside that radius: skip the 3D model draw and the
  floating health bar/name/hover-tooltip (S170-69) entirely, rather than dimming them -- this map
  has no terrain/occlusion geometry to justify a soft fade, a hard radius cutoff is the honest
  match for what the data actually supports.
- No minimap exists in this client at all yet -- fog of war without a minimap only ever hides/
  reveals the main 3D view, which is still a real, useful signal (matches what a player standing
  in the world would actually be able to see) but is a narrower feature than "real" MOBA fog,
  worth naming so it isn't assumed to include minimap vision dots that don't exist.

**Open questions, not resolved here:**
- Team vision sharing: does a teammate's vision radius count toward yours (the real-MOBA norm), or
  is vision strictly per-hero? Team vision sharing needs each ally's position (already available
  client-side) but is a real design choice about how much information asymmetry the game wants,
  not just an implementation detail.
- Interaction with capture nodes: should owning a node grant vision around it (a real strategic
  reward for territory control, echoing S170-121's "controlling a node enables its spawn for your
  team" -- territory keeps mattering mechanically, not just cosmetically)? Not decided.
- Whether jungle creeps (S170-51) should be vision-gated the same as enemy heroes, or always
  visible (they're neutral/environmental, not a "the enemy team hid something from you" case) --
  leans toward always-visible, not confirmed.

Nothing built this pass. If either half gets promoted to real work, the open questions above are
the actual design surface to resolve first -- not just "add camera lock" or "add fog of war" as
originally framed.

## 16. Weatherman + Donkey, and the non-piloted-unit gap (2026-07-25, S170-93)

Founder, real-time, part of the same batched hero-wave as He Xiangu/Gunnr/Vassago/Beleth: "add the
weatherman and donkey specific donkey paper airplane weatherman interractions." Scoped via
AskUserQuestion to spec-first, same treatment as §15 -- Donkey is not a stock kit-per-lore-entry
job like this session's other additions (Cain, Gunnr, Vassago, He Xiangu, Beleth all reuse the
existing generic status-effect toolkit on an owner-piloted hero), and Weatherman has zero kit
writeup at all yet, only TYLER lore.

**Status update (2026-07-29, S170-206):** built, both halves. §16.1's whole premise -- that Donkey
needs a genuinely new non-piloted-unit system -- turned out to be avoidable: asked to clarify the
"owner" ambiguity in Donkey's own kit text, the founder's answer was "donkey should be an item."
Shipped as an equippable item (3200 Flow, Back slot) instead of a hero: Immortal's Fold (automatic,
HP < 25% -> damage floor + periodic fight-back damage) and Paper Glide (tilde-activated -- the same
key Blink Dagger uses, generalized to `arena_use_active_item` -- a real high-speed traversal, flies
over obstacles, untargetable for the window, 2-minute cooldown) both trigger on whichever hero
wears it, no second targetable entity, no companion-slot system, no new render/collision path --
every one of §16.1's stated requirements sidestepped, not solved. Weatherman shipped as a stock
owner-piloted hero (#27) exactly as §16.2 specified, with §16.3's Donkey interaction on W working
against the item's own `donkey_airborne_ms` field instead of a companion entity's state. §16.4's
open questions: resolved in practice rather than left open -- Donkey stayed a single-item design,
not generalized to a reusable companion-slot system (no second need has materialized); Weatherman's
Q knockback vs. node capture channels wasn't specifically addressed (the generic "mixed presence
interrupts" rule already covers displacement the same as any other movement); W's grounding effect
ends the Donkey wearer's `intangible_ms` too, a beat of vulnerability, matching the stronger reading
the open question already leaned toward.

### 16.1 The actual blocker: no non-piloted-unit system exists

`docs/HEROES_VS0.md`'s own Donkey entry already names this precisely: Donkey is
**Indirect-Control** -- never directly commanded, rides folded (inert, untargetable, no collision)
alongside its owner, and unfolds automatically on trigger conditions (owner drops below 25% HP;
owner needs an escape). Every hero actually wired into `packages/simulation/arena_game.c` today is
owner-piloted: `ArenaHero.owner` maps one input stream (or one bot brain) to one fully-controlled
unit. Donkey needs a second, structurally different kind of thing: a passenger entity whose
`active`/visible/targetable state is *derived* from its owner's state and an internal trigger
timer, not from any player input at all.

**What that would actually require, at the level of a real design (not code):**
- A companion-slot concept on `ArenaHero` (or a small parallel array, index-matched to owner slot,
  same pattern `ArenaCreep`/`ArenaNode` already use for non-hero entities) -- `folded`/`unfolded`,
  `unfold_ms_remaining`, `airborne` (for Paper Glide specifically), and a copy of the trigger
  conditions (owner HP fraction, owner's most recent movement-based "needs distance" signal).
- A per-tick check, run alongside `tick_hero_kit`, that evaluates the trigger conditions against
  the *owner's* current state and flips `folded`/`unfolded` accordingly -- structurally the same
  shape as `arena_tick_respawns` (a system that changes hero-adjacent state on a timer/condition,
  not on a cast command), reused rather than invented from nothing.
- Collision/targeting rules that respect `folded` (untargetable, no collision, doesn't block
  movement or count toward node capture) the same way `intangible_ms` already gates hit-eligibility
  via `hero_is_hittable` -- likely folded Donkey reuses that exact function rather than a parallel
  check.
- A render-side concept of "this slot has a companion" so `apps/arena/src/main.c` knows to draw a
  second, smaller model near the owner when unfolded -- no such second-model-per-owner rendering
  path exists today; every draw call today is one model per active hero slot.

None of this is large in the sense of touching many files (unlike, say, the mana system's 63-site
scripted pass) -- it's large in the sense of being a genuinely new *kind* of simulated entity this
engine hasn't needed before, and getting the trigger-timing/collision edge cases right the first
time matters more than the line count.

### 16.2 Weatherman -- full kit (new; TYLER `multiverse_heroes.md` #45, "Ao Guang's Weather-Debt Collector")

Real TYLER canon, not yet a hero-kit entry anywhere: 9.0 Hz, "collects on storms owed and storms
overdrawn, for the Dragon King of the East Sea" -- a demigod whose entire ledger is meteorological,
every flood/drought/unseasonable calm somewhere on his books, balanced against a debt system
nobody outside his office fully understands. Seed phrase: "the debt compounds with the
barometer." Archetype: **Fighter/Support**, the roster's first hero built around wind/displacement
rather than direct damage-focused kit shapes.

- **Passive -- The Ledger**: flavor-only for a first pass (no new generic status-effect field
  needed) -- alternates narratively between "storm owed" and "storm overdrawn," but mechanically
  reuses the existing always-on regen shape (Dagda's Undry) rather than inventing a new oscillating
  buff system this pass. A richer version (a real alternating buff/debuff cycle) is a legitimate
  follow-on, not required to ship a first kit.
- **Q -- Barometric Shove**: a ranged wind gust that knocks the target back a fixed distance
  instead of dealing the roster's usual flat damage-on-hit -- the first real displacement-only Q on
  this roster (Duck's R pulls inward; this pushes outward). Mechanically: on landed hit, move the
  target's `x`/`z` directly away from Weatherman by a fixed distance, same "instant position
  change, no travel-time projectile" simplification `duck_pull_foe` already established, clamped to
  the arena bounds the same way `update_hero_motion` already clamps normal movement.
- **W -- Collects On What's Owed**: the specific Donkey interaction the founder asked for, see
  §16.3.
- **R -- The Debt Compounds**: an AoE ultimate, fixed zone (reuses `r_zone_x`/`r_zone_z`/
  `r_zone_tick_ms`, same shape as Ghost's Recital/Paimon's Two Hundred Legions) that deals periodic
  damage to enemies standing in it -- the literal storm finally collecting, biggest and simplest
  ability on the kit by design, so the interesting design surface stays on W where the actual ask
  was.

### 16.3 The specific interaction: W reads ally-vs-enemy, same shape as Ghost's Recital

**Collects On What's Owed**, cast on the nearest hero (ally or enemy, `arena_nearest_enemy`/
`arena_nearest_ally` already both exist and are already used this way by several W's this session,
e.g. Vassago's/Frog's ally-targeted refund):

- **Cast on an enemy who is currently airborne** (Donkey's Paper Glide, `airborne` from §16.1):
  immediately ends the glide and grounds them in place -- "the debt catches up to you no matter how
  far you fly." A hard, thematically exact counter to the escape Donkey's whole kit exists to
  provide, giving Weatherman a real reason to be picked into a Donkey-carrying comp.
- **Cast on an allied Donkey currently mid-glide**: extends the glide's remaining airborne duration
  and travel distance instead -- a tailwind, not a headwind. Same button, opposite effect depending
  on team, the exact "same zone, opposite effect depending on team" precedent Ghost's Recital
  already set for this roster (`docs/HEROES_VS0.md`'s own line), just applied to a targeted cast
  instead of a zone.
- **Cast on anyone not currently airborne at all** (the overwhelmingly common case, since Donkey's
  Paper Glide is a rare auto-trigger, not a constant state): no-op, cooldown not consumed -- same
  "whiffed cast costs nothing" convention every other conditional W on this roster already follows
  (Frog's Borrowed Time, Vassago's own W, Morrigan's Three Forms).

This is deliberately a narrow, high-specificity interaction rather than a generic "wind affects
movement speed" buff/debuff -- the founder asked for *this* interaction, between *these* two
characters, not a reusable systemic wind mechanic. A broader wind-affects-everyone system would be
scope creep past what was actually requested.

### 16.4 Open questions, not resolved here

- Does §16.1's companion-slot system get built generically enough that a *second* Indirect-Control
  hero (none currently planned) could reuse it, or does it stay Donkey-specific until a second
  need actually materializes? Leans toward Donkey-specific first (same "don't build for hypothetical
  future requirements" discipline as everything else in this codebase), not confirmed.
- Weatherman's Q (knockback) interacting with node capture channels: does a knocked-back capturing
  hero have their channel interrupted the same way `damaged_this_tick` already interrupts it, or is
  displacement-without-damage a separate case? Not decided.
- Whether W's grounding effect on an enemy Donkey should apply a brief `silenced_ms` too (can't
  immediately re-trigger a new escape) or just end the current flight -- "the debt catches up to
  you" reads stronger with a beat of vulnerability after, but isn't confirmed as intended balance.

Nothing built this pass. §16.1's companion-slot system is the actual prerequisite before either
hero can be wired in for real -- Weatherman alone (without the Donkey-specific half of W) could
technically ship early as a stock owner-piloted kit, but that would mean building W's ally/enemy
branch against a Donkey `airborne` flag that doesn't exist yet, the kind of "papers over a real gap"
shortcut this doc's own Donkey entry already explicitly declined to take.

## 17. Auto-attack movement model — League of Legends parity (2026-07-28, S170-158)

Founder, real-time: a request for a detailed northstar on exactly how League of Legends' click-based
auto-attacking works with respect to movement -- does the champion stop when auto-attacking, does it
follow a target that runs away, and how ranged auto-attacks are similar to and different from melee --
with LoL treated as the literal gold standard to hit exact parity against. This section is that
reference, plus the honest gap between it and REDGARDEN's actual combat model.

**Status update (2026-07-28, S170-204):** §17.4's first three bullets are now built, in two passes.
S170-162/163 (landed the same day this section was originally written, but never reflected back into
it until now) shipped the distinct attack command (`PACKET_ARENA_ATTACK`/`arena_set_attack_target`),
the persistent attack-target lock with pure-pursuit chase (`arena_tick_attack_targets`), and Gary's own
real homing ranged basic attack (`ArenaProjectile.homing_target`) -- §17.3's own gap analysis below was
already stale about all three before S170-204 even started. S170-204 then built the actual core "does
the champion stop" mechanic neither of those touched: a real windup/backswing state machine
(`attack_windup_ms_remaining`) applied to both the flat melee loop and Gary's ranged attack --
movement freezes during windup, a genuine reposition (or a stun) cancels it outright with no damage/no
cooldown spent, and a completed windup fires the hit/projectile with the target re-validated only at
that moment, not continuously. **Status update (2026-07-31):** attack-move ("A" + click) also now
shipped -- see §17.4's own updated checklist entry. Still genuinely unbuilt: a per-hero `is_ranged`
flag/homing basic attack for anyone besides Gary (every other hero's plain auto-attack is still the
flat melee tick, regardless of lore). §17.3's gap analysis below is kept as originally written, for
the historical record of what motivated this section, not edited to retroactively look prescient.

### 17.1 League of Legends' actual model (the gold standard)

**Two distinct commands, not one.** Right-click empty ground is a pure **move command** -- it never
initiates combat, even if the resulting path crosses an enemy champion. Right-click *on* an enemy unit
(champion, minion, monster, ward, structure) is a distinct **attack command**. This split is the whole
foundation of everything below -- REDGARDEN currently has no equivalent distinction (§17.2).

**Does the champion stop to auto-attack? Yes.** Every basic attack is a three-phase sequence, not an
instant tick:

1. **Windup (a.k.a. "cast time")**: the moment an attack command is issued against a target in range,
   the champion instantly snaps to face the target (no turn-rate delay, unlike movement, which does have
   a turn radius) and stands still for a fixed fraction of the total attack-time. The champion **cannot
   move during windup without canceling the attack outright** -- issuing a move command mid-windup
   cancels the swing entirely (no damage, but no cooldown penalty either; the attack simply didn't
   happen and can be re-attempted immediately).
2. **The attack fires** at the end of windup -- for melee this is effectively instant/hitscan; for
   ranged this is when the projectile actually leaves the champion (see §17.2 on travel time). Damage
   application timing differs between the two for exactly this reason.
3. **Backswing (recovery)**: the remaining portion of the attack animation after the hit/projectile has
   already been committed. Critically, **a move command issued during backswing cancels the recovery
   animation but does NOT undo the attack that already fired** -- the champion is free to reposition
   immediately. This is the mechanical basis of **kiting** / **orb-walking**: alternate attack commands
   and move commands so that only the (uncancellable) windup ever costs you movement, never the
   (cancellable) backswing. A perfectly executed kite pattern loses zero attacks compared to standing
   still, while still moving during roughly the backswing fraction of every attack cycle.

Total time between the start of one windup and the next = `1 / attack speed` (attacks per second). The
windup fraction of that total is fixed per champion (a base "attack cast time" ratio) and does not
shrink as attack speed increases -- only the backswing fraction compresses, which is *why* kiting has a
practical ceiling (very high attack speed compresses backswing toward zero, leaving almost the whole
cycle as uncancellable windup). REDGARDEN does not need to reproduce this exact ratio-based formula for
a first pass (§17.5), but the *shape* -- windup is stop-and-commit, backswing is cancel-free -- is the
actual parity target, not a cosmetic detail.

**If the target runs away, do you follow it? Yes, automatically, indefinitely.** A single attack-command
click is not "attack once" -- it sets a **persistent attack-target lock**. Every subsequent frame, while
that lock is still valid, the client automatically re-evaluates: if the target is out of attack range,
walk toward the target's *current* live position (recomputed continuously -- this is **pure pursuit**,
not intercept/lead pursuit; League does not predict where the target will be, it always paths at where
the target currently is, same simplification a naive chase-AI would use); the instant the target is back
in range, movement stops and the windup/fire/backswing cycle above begins again, with zero need to
re-click. This chase-lock persists until exactly one of: the target dies, the target becomes
untargetable (stealth, certain spell-shields, banished-type effects), or the player issues a **new**
command -- a different attack-target, or any move command, which immediately clears the lock. Attack
range, movement speed of both parties, and terrain are the only things that determine whether a chase
actually lands a hit; there is no leash range or "give up chasing after N seconds" timeout in vanilla
League.

**Attack-move ("A" + click) is a third, related command**, distinct from a direct unit right-click:
it moves toward a *location* (like a move command) but auto-diverts to attack the first valid enemy
that comes within range along the way, without needing that enemy to have been the original target of
anything. A direct right-click on a unit is a hard lock on *that* unit specifically (chases it even past
closer, equally-valid targets); attack-move is opportunistic and re-targets to whatever's nearest/most
threatening as it walks. Both commands share the exact same windup/backswing mechanics once an attack
actually begins -- the difference is purely in *what* gets targeted and *whether* the lock survives a
target dying (attack-move re-acquires a new target automatically; a direct unit-lock does not).

### 17.2 Ranged auto-attacks: what's the same, what's different

**Same as melee:** the champion still fully stops for the windup -- there is no "walk and shoot" for
plain basic attacks in vanilla League regardless of range (a handful of champion passives grant this as
an explicit exception, e.g. some abilities/traits that state "can move while attacking" -- the existence
of those as *named exceptions* is itself confirmation that stopping is the baseline rule, not an
oversight). Backswing-cancel kiting works identically for ranged and melee champions; only the practical
value differs (a ranged champion kiting a melee chaser keeps them permanently out of *their* attack
range, which is the single biggest reason ranged carries feel fundamentally different to play than melee
bruisers, despite an identical underlying attack state machine).

**Different:** a ranged basic attack fires a real **projectile with non-zero travel time** (typical
values across the champion roster range from roughly 1300 to 2200+ units/second, versus melee's
effectively-instant 0 travel time at point-blank range). That projectile **homes/tracks the target** --
it is emphatically **not a skillshot**. Once fired, if the target was in range and targetable at the
moment of firing, the shot will connect regardless of how the target moves afterward (short of the
target becoming untargetable or leaving true unit-collision/vision in ways that matter only at the
margins) -- this is the critical, easy-to-get-wrong detail: a ranged auto-attack behaves like a
homing missile locked at launch, not like a projectile that can be dodged by strafing, the way a
skillshot ability can be. The travel-time window is also exactly why a ranged champion can start moving
the instant the projectile leaves (once it's fired, the champion's own subsequent movement has zero
effect on whether it lands) -- the attack "landing" and the champion's own freedom to move again are
decoupled by that flight time, on top of the ordinary backswing-cancel freedom every attack already has.

### 17.3 REDGARDEN's current model (gap analysis, grounded in the actual code)

None of the above exists today. Current combat (`resolve_combat`, `arena_hero_attack_creeps`,
the team-mode melee loop in `arena_update_teams`) is a single, flat, always-on proximity check:
every tick, for every pair of hittable opposing units, if `distance <= ARENA_ATTACK_RANGE` (1.6 units,
one flat constant for the entire 26-hero roster, no ranged/melee distinction) and the attacker's
`attack_cooldown_ms` has reached 0, damage is applied and the cooldown resets to
`ARENA_ATTACK_COOLDOWN_MS` (700ms) -- completely decoupled from movement. There is no windup, no
backswing, no "stop to attack" state of any kind: a hero can be actively walking toward a
`PACKET_ARENA_MOVE` target and will keep dealing/taking auto-attack damage to/from anything that happens
to be in range on the way past, with zero interruption to either the movement or the combat.

There is also no attack command at all, distinct from move -- only `PACKET_ARENA_MOVE` (a raw x/z
point) and `PACKET_ARENA_CAST` (an ability slot) exist on the wire (`packages/common/protocol.h`).
"Chasing" a fleeing target today is not a server-side behavior at all -- it only happens if the client
(a human re-clicking the fleeing hero's position, or a bot re-sending a fresh move target toward the
enemy's last-known snapshot position on its own ~100ms decision loop, see `apps/arena_bot/src/main.c`'s
`play_one_match`) keeps re-issuing move commands manually. There is no persistent target-lock of any
kind server-side, and no ranged basic auto-attack at all -- every hero, regardless of lore/kit
(gun-wielders, casters, melee brawlers alike) shares the identical 1.6-unit melee range for plain
auto-attacks; only *ability* casts (Q/W/R) currently spawn real projectiles
(`arena_spawn_projectile`/`ArenaProjectile`), and those are explicitly **non-homing skillshots** --
fixed velocity computed once at cast time toward the target's position *at that instant*, genuinely
dodgeable by stepping off the line afterward (see that struct's own doc comment in
`packages/simulation/arena_game.h`). That skillshot physics model is the *opposite* of what a real
ranged basic auto-attack needs (§17.1's homing/tracking behavior) -- reusing `ArenaProjectile` as-is for
basic attacks would get the auto-attack feel backwards, not just approximately right.

### 17.4 Target design for parity

- [x] **A distinct attack command.** `PACKET_ARENA_ATTACK` carrying a target hero slot, alongside
  `PACKET_ARENA_MOVE` -- shipped S170-162.
- [x] **Windup/backswing state on `ArenaHero`.** `attack_windup_ms_remaining` -- shipped S170-204.
  Movement freezes while it's active (`update_hero_motion`); a genuinely new move command (not the
  attack-target chase's own internal re-affirmation, and not the bot AI's noisy ~100ms re-send of
  "stay roughly here") or a stun cancels it outright, no damage/no cooldown spent; when it reaches 0,
  damage applies (or, for Gary, a projectile spawns) and the existing flat `attack_cooldown_ms` starts
  -- which already behaves exactly like backswing (free movement, doesn't undo the hit that already
  landed), so no second field was needed for that half.
- [x] **Persistent attack-target lock.** `attack_target` on `ArenaHero`, pure-pursuit chase
  (`arena_tick_attack_targets`) -- shipped S170-162.
- [ ] **Ranged vs melee split, roster-wide.** Only Gary has a real homing ranged basic attack
  (S170-163) and his own longer `ARENA_GARY_ATTACK_RANGE`. Every other hero's plain auto-attack is
  still the flat `ARENA_ATTACK_RANGE` melee tick regardless of lore (gun-wielders, casters, etc.) --
  a per-hero `is_ranged` flag/second homing-projectile-for-basic-attacks system generalizing what
  Gary already has is real, scoped, future work, not done.
- [x] **Attack-move command** (LoL's "A" + click). Shipped 2026-07-31, driven by §24 Milestone 2's
  own real WC3 group-order vocabulary work rather than this section's own original priority
  order -- `PACKET_ARENA_ATTACK_MOVE`/`arena_set_attack_move_target`/`arena_tick_attack_move`.
  Held-key detection (`SDL_SCANCODE_A` at the moment of a ground click, same "held, not toggled"
  idiom the Tab scoreboard already uses), not a separate mode-toggle keypress. Opportunistically
  acquires `attack_target` from whatever enemy comes within range en route (re-scans if the
  acquired target dies, real "attack-move re-acquires automatically" §17.1 behavior a direct
  attack-target lock doesn't have), and resumes the ORIGINAL destination once nothing's left to
  engage -- a new `attack_move_x/z` pair remembers that destination since `target_x/z` gets
  overwritten during a chase. Team-mode only, same scoping `attack_target`/chase itself already
  has. 5 new tests, full suite + 10-bot stability green.

### 17.5 Open questions, not resolved here

- Attack-speed scaling (windup shrinks less than backswing, per-champion floor) is real League depth
  this spec deliberately does not require for a first pass -- REDGARDEN has no attack-speed stat or item
  economy at all yet (the same gold/XP-economy gap §12/§13's sprint-plan notes already flag elsewhere),
  so a first pass should use one flat, non-scaling windup/backswing split per hero (or even roster-wide)
  until an economy exists to meaningfully feed an attack-speed stat.
- Exact windup:backswing ratio to use -- real League varies this per champion (roughly 10%-40% windup
  depending on champion, "canceled" further compressed by attack speed) and isn't published as a single
  universal number; REDGARDEN choosing one flat ratio (e.g. 25% windup / 75% backswing of the existing
  700ms `ARENA_ATTACK_COOLDOWN_MS`) as a first-pass approximation is reasonable but not confirmed as
  final tuning.
- Whether a knocked-back/rooted/silenced hero's *movement*-based chase should be interrupted the same
  way an active windup already must be, or whether status effects only ever gate the windup/cast itself
  -- likely "yes, chase is just movement, and movement is already gated by root/knockback elsewhere," but
  not confirmed against every existing status-effect interaction.
- Does canceling backswing via a move command and then immediately re-issuing `PACKET_ARENA_ATTACK` on
  the *same* target re-engage without walking back into range (since only a frame's worth of movement
  happened)? Yes by construction of the design above -- called out here as confirmed-by-design, not an
  open question, so a future implementer doesn't second-guess it.

This section's own job -- pin down *exact target behavior* before building against a moving target,
per the founder's "exact parity with LoL, LoL as the gold standard" framing -- is done. All of §17.4's
structural additions except the roster-wide ranged split and attack-move have since shipped (S170-162,
S170-163, S170-204; see this section's own status update near the top), built against the behavior
pinned down here rather than guessed at.

## 18. Unsupervised learning for the bot AI — general + per-hero, cross-hero transfer (2026-07-28, S170-167) -- spec only, no code yet

Founder, real-time: **"write the northstar for unsupervised learning - it will have to be both
general and per hero - for example experience playing a hero will help inform decisions playing
with and against it on another hero,"** immediately followed by two grounding clarifications:
**"also look for archetype engine fwiw"** and **"we are going to want to do long running per
personality bot training but for now we need generalized ai for the different heroes."** This
section is that northstar. Same "spec only, no code yet" treatment as §15-§17 -- it exists to pin
down the actual shape and sequencing before any of it gets built, and to make sure it extends
§12 Phase E's already-committed plan rather than standing up a competing one.

### 18.1 Scope and sequencing (founder direction, load-bearing)

Two explicitly different timescales, not one blurred ask:

- **Near-term (what this section actually specs for building soon): a generalized AI that plays
  reasonably across the whole hero roster**, sharing what it learns from any hero's matches with
  every other hero, not siloed per hero from day one.
- **Long-term (named, deferred, not specced in implementation detail here): long-running
  per-personality bot training** -- individual heroes' AI eventually diverging into distinct,
  persistent "personalities" shaped by extended training specifically on that hero. §18.5 below
  sketches the shape this likely takes so the general-layer design doesn't accidentally paint it
  into a corner, but building it is explicitly out of scope for this pass.

The "general vs per-hero" framing from the founder's first message maps onto this sequencing
directly: general = near-term priority, per-hero-personality = the long-running thing that comes
after general exists to build on.

### 18.2 What already exists -- reuse, don't reinvent (checked before writing this)

Two real pieces of org tech already exist and are directly relevant, at two different tiers:

**Tier 1 (already this repo's own committed plan): §12 Phase E's GPT-2 policy-network flywheel.**
`gpt2-alpine-c/docs/GAME_AI_NORTHSTAR.md` (serialize state → generate action tokens → decode),
extended to REDGARDEN via `packages/simulation/arena_ai_bridge.h`/`.c` --
`arena_serialize_state(owner, tick_ms, ...)` (a stable, natural-language `self`/`foe`-framed state
string -- hero name, position, HP, every generic cooldown/status field) and
`arena_decode_action(...)` (parses a `"move:x,z cast_q:0/1 cast_w:0/1 cast_r:0/1"` action string
back into a move target + cast flags). Built as a contract (S170-36) but **not wired into a live
bot yet** -- no inference call, no replay-log fine-tune, no self-play loop running. NORN's
propose→grade→gate→promote loop (`pkg/norn`) is already named as the evaluation mechanism for
"is this bot generation actually better." This tier is fast, per-tick, and low-level -- it decides
*what to do this instant* (move here, cast this).

**Tier 2 (found via this session's own "look for archetype engine" check):
`EMILY/docs/ARCHETYPE_ENGINE_NORTHSTAR.md`** ("Dynamic Hybrid AI Agent Archetype Engine"), with
real, partially-built Go code already in `EMILY/emily-agent/pkg/archetypes/`
(`field.go`/`selector.go`/`spirits.go`) and `EMILY/emily-agent/cmd/archetype-engine/main.go` (HTTP
service, `:8090`, `POST /invoke`). This is **not** a hero/kit classification system -- despite the
name overlap with REDGARDEN's own informal "Fighter/Mage/Assassin/Tank/Support" hero-archetype
vocabulary (`docs/HEROES_VS0.md`), which is just labeling, no engine behind it. The real Archetype
Engine is an LLM-routing layer: two Claude personas run in parallel per invocation -- **E₁
"Carrier"** (deterministic, low-temperature) and **E₂ "Explorer"** (high-temperature, divergent) --
modulated by up to 3 of 72 "Goetia spirit" personas selected by intent-matching, with a computed
phase-difference (embedding cosine similarity → degrees) classifying a resonance state (Coherent
Lock / Golden Band / Chaos Edge / Collapse) that weights how E₁/E₂ get synthesized into one output.
Already named as an intended consumer for "SHANKPIT's game AI" in its own northstar, never
connected to REDGARDEN. Per its own milestone table, most of the engine (dual-persona invocation,
resonance/synthesizer) is **not started** as spec, though the Go scaffolding already exists. This
tier is slow, occasional, and high-level -- it would decide *what kind of hero/what disposition
this is right now*, not the per-tick mechanics.

These two tiers don't compete and were never connected to each other. §18.3 proposes how they fit
together for REDGARDEN specifically.

### 18.3 Proposed two-tier architecture (how the pieces fit)

- **Tactical tier (Tier 1, GPT-2 policy net)**: per-tick action decisions -- move/cast -- driven
  by `arena_serialize_state`/`arena_decode_action`, exactly §12 Phase E's existing plan, unchanged
  by anything in this section. This is where §18.4's unsupervised general layer actually lives.
- **Strategic tier (Tier 2, Archetype Engine)**: occasional, higher-level framing -- not called
  every tick, but periodically (e.g. once per fight, once per objective-contest window) to
  classify "what disposition should this hero take right now" (aggressive dive vs. peel-and-kite
  vs. turtle-the-node), expressed as a short intent string that becomes part of Tier 1's next
  several `arena_serialize_state` calls (a "current disposition" field alongside the existing
  hero-name/position/HP/cooldown fields) -- steering, not overriding, the fast tactical loop.
  This is the natural home for §18.5's eventual per-hero "personality" divergence: a hero whose
  long-running training has shaped a distinct personality expresses it here, at the strategic
  tier, rather than needing every single per-tick action decision to carry personality weight
  directly.

Nothing here requires building the Archetype Engine's still-not-started resonance/synthesis
machinery just to unblock REDGARDEN -- Tier 1 alone (§18.4) is a complete, shippable "generalized
AI across heroes," matching the founder's explicit "for now we need generalized ai" priority.
Tier 2 is named and slotted in now so Tier 1's design doesn't have to be redone later to make room
for it.

### 18.4 The general layer -- genuinely unsupervised, near-term (what to actually build first)

The word "unsupervised" is doing real technical work here, not just flavor -- it names a specific,
buildable stage that sits *before* §12 Phase E's own Milestone 7+ (supervised fine-tune on
win/loss-labeled replay outcomes, NORN-graded). GPT-2's own native training objective (next-token
prediction) is itself self-supervised/unsupervised: no human-authored labels are needed, just raw
sequences.

- **Corpus**: every replay log from every hero, from every match, undifferentiated -- the exact
  same corpus §12 Phase E already names ("Phase B's replay logs are the training corpus"), used
  here for a *different, earlier* purpose than the later supervised stage.
- **Objective**: next-token prediction over `arena_serialize_state`'s own natural-language
  state-string format, extended with an action-token suffix per tick (state → the action that was
  actually taken next) -- learn "what tends to happen next," not "what wins." No win/loss label,
  no reward signal, no NORN grading at this stage -- purely descriptive of how matches actually
  unfold. This is where general tactical concepts live: closing distance, retreating below some
  HP threshold, contesting a node, kiting when faster than a chaser, grouping with allies -- none
  of which are hero-specific facts, all of which are learnable from ANY hero's replay data.
  Concretely, this is one shared set of weights, not N per-hero models -- every match, regardless
  of which hero(es) it features, updates the same general representation.
- **Where it plugs in**: this is the pretraining stage for Tier 1's own GPT-2 policy net --
  §12 Phase E's already-planned supervised fine-tune (Milestone 7+, NORN-graded, win/loss-labeled)
  becomes the SECOND stage, starting from these unsupervised-pretrained weights instead of the
  base GPT-2 checkpoint cold. Standard, real ML pipeline shape (unsupervised pretrain → supervised
  fine-tune) applied to this repo's own already-chosen architecture, not a parallel invention.

### 18.5 The per-hero layer -- named, shaped, explicitly deferred (long-running personality training)

Not built this pass, per §18.1's own sequencing -- sketched only so §18.4's general-layer design
doesn't foreclose it:

- Long-running, per-hero fine-tuning/adaptation on top of the shared general-layer weights from
  §18.4 -- likely a small adapter per hero (a LoRA-style low-rank delta, not a fully separate
  model; keeps the general layer as the shared backbone every hero's adapter sits on, so §18.6's
  cross-hero transfer keeps working even after per-hero specialization exists) rather than N fully
  independent models trained from scratch.
  "Personality" divergence over a long training run is naturally where NORN's own
  propose→grade→gate→promote loop earns its keep per-hero, not just per overall bot generation --
  each hero's adapter gets its own gated promotion track once this stage exists.
- This is the natural landing spot for the Archetype Engine's Carrier/Explorer dual-persona
  concept (§18.2/§18.3) taken literally: a hero's long-run "personality" could quite directly be
  expressed as where that hero's own trained disposition tends to sit between Carrier
  (deterministic, reliable, plays the numbers) and Explorer (divergent, riskier, plays for the
  outlier) -- not decided, just flagged as a strong, cheap-to-explore fit given Tier 2 already
  exists in that exact shape.
- Not decided: whether every hero eventually gets a personality adapter, or only a subset (the
  roster's own "indirect-control archetypes are a deliberate feature, not a gap" precedent, §8,
  suggests non-piloted heroes like Donkey/Retrieval Cart may never need one at all).

### 18.6 The actual cross-hero transfer mechanism -- the founder's own worked example, made concrete

**"Experience playing a hero will help inform decisions playing with and against it on another
hero"** is the one requirement this whole section exists to satisfy technically, not just
gesture at. Two concrete levers, both cheap, both already close to existing infrastructure:

1. **Shared weights are the implicit lever.** Because §18.4's general layer is ONE model trained
   on ALL heroes' replay data (not per-hero silos), any experience -- playing Gary a lot, say --
   updates the shared representation of concepts like "ranged auto-attack range management" or
   "homing-shot pressure," which is available to every OTHER hero's tactical decisions the moment
   it's learned, including heroes the model has little or no direct experience with, as long as
   they share enough mechanical shape with what was already learned.
2. **Archetype/kit-shape tagging is the explicit lever, and the stronger one.** Add a small set of
   mechanical-shape tags to `arena_serialize_state`'s own output alongside the existing
   hero-name/position/HP/cooldown fields -- e.g. `ranged`/`melee`, `has_homing_attack`
   (S170-163's own new mechanic), `has_knockback`, `has_heal`, `has_dash`, `has_stealth` --
   describing WHAT a hero's kit mechanically does, not just WHICH hero it is. This is what makes
   transfer *reliable* rather than merely emergent: a future ranged hero that also gets a homing
   basic attack (§17's own target design, currently only Gary) would carry the same
   `has_homing_attack` tag Gary's own training data already carries, so kiting/positioning
   patterns learned against Gary specifically transfer to that new hero on day one of its own
   existence, before it has accumulated any replay data of its own at all -- the literal "playing
   against it on another hero" half of the founder's example. `arena_serialize_state` already
   frames every state from both `self` and `foe` perspectives (§12 Phase E's own existing design,
   built for a completely different reason), which is exactly the plumbing this needs: the same
   archetype tags need to appear on both sides of that framing so the model learns a kit-shape's
   pattern from BOTH playing it and facing it, not just one or the other.

### 18.7 Open questions, not resolved here

- Exact tag vocabulary for §18.6's archetype/kit-shape tags -- not enumerated against the full
  26-hero roster here; a real design pass of its own once this stage is actually being built,
  likely grounded in `docs/HEROES_VS0.md`'s existing kit writeups rather than invented fresh.
- Whether Tier 2 (Archetype Engine) integration happens before or after §18.5's per-hero
  personality layer, or whether Tier 1's unsupervised+supervised stages (§18.4, then §12 Phase
  E's own Milestone 7+) are sufficient on their own for longer than expected before Tier 2 is
  worth building out at all -- genuinely open, not a sequencing claim.
- Whether the general layer (§18.4) trains once against the full existing replay corpus and then
  freezes, or keeps training continuously as new matches accumulate (an actual online/continual
  learning question, not addressed here) -- likely the latter eventually, given "bots need
  personalities that evolve and learn on their previous matches" is already on record as founder
  intent (§12 Phase E), but not scoped for this pass.

Nothing built this pass. §18.4 (unsupervised pretraining on the existing replay corpus) is the
actual next buildable step once someone picks this up -- it slots directly in front of §12 Phase
E's own already-specced Milestone 7, not after it, and needs nothing from §18.5/Tier 2 to be
useful on its own.

## 19. Gold/XP economy + structures (2026-07-28, S170-174)

**Status update (2026-07-28, S170-175/180-188): the economy half of this spec is built and
shipped.** Renamed Flow (not gold, founder real-time correction, caught before any code shipped
under the old name) -- per-hero currency + XP, sourced from kills exactly as §19.2 below
specced, spent in a real two-shop item system (§19.3, generic/weird/specific tiers, 11-slot
FFXI+WoW equip system), with a character stat pane and shop panel affordance, bot AI shop
interaction, and hero-kill assists (§19.2's own "fed by kills" extended to whoever contributed,
not just the killing blow, S170-187) all live. Full sprint history: `EMILY/BACKLOG.md` S170-175
(sprints 1-5) and S170-187. **§19.5 (structures) is now built too (2026-07-30), in a materially
different shape than originally specced below** -- see §19.5's own update note for exactly what
shipped versus what this section originally proposed.

Founder, real-time: **"continue the backlog for redgarden."** Picks up the earlier sprint plan's
own items 4 and 5, both explicitly flagged as needing "a real design pass of its own before any
single feature... gets built against a placeholder" -- named separately there but designed together
here on purpose: item 5's own text already says structures are "the actual 'push' payoff lane creep
waves are currently missing," and a payoff needs something to pay out *into*, which is exactly what
item 4's gold economy is. Same "spec only, no code yet" treatment as §15-§18 -- this pinned down the
shape before either got built; item 4 (gold/Flow) now has, item 5 (structures) still doesn't.

### 19.1 A real conflict this design has to resolve first

`resources[2]` (S170-153, `packages/simulation/arena_game.c`'s `arena_tick_resources`) is **the win
condition** -- first team to `ARENA_RESOURCE_CAP` wins outright. `docs/CONSUMABLES_AND_COOKING.md`
(written 2026-07-23, before the resource-race win condition existed) assumed cooking would spend
from "the same resource/influence economy §3 of NORTHSTAR.md already tracks" -- i.e., the exact same
pool. That assumption is now stale and, if followed literally, actively harmful: spending team
resources on personal items/cooking would directly slow down your own team's progress toward
*winning the match*, an odd and probably-unfun coupling nobody actually designed on purpose (it's an
artifact of the cooking doc predating the win-condition redesign by exactly one session).

**Resolution: two separate currencies, real MOBA precedent.** `resources[team]` stays exactly what
it is today -- a team-level, win-condition-only meter, untouched by anything below. A **new,
separate, per-hero currency (gold)** is introduced for personal power progression, fed by kills, not
by node control -- matching how LoL/Dota already separate "objectives" (turrets, dragons/Roshan --
team-wide advantages) from "gold" (per-player, spent on items) instead of taxing one pool for both
jobs. `docs/CONSUMABLES_AND_COOKING.md`'s own worked example ("holding an orange grove node grants
Oranges over time") still works fine under this split -- it was never actually describing
`resources[team]` mechanically, just informally reusing that section number; §19.6 below updates it
to point at gold explicitly instead of the win-condition meter.

### 19.2 Gold: sources

- **Jungle creep kills** (S170-51): `ArenaCreep.last_attacked_by_owner` already exists and already
  correctly attributes the killing blow -- `creep_die()` already reads it for the existing
  heal/capture-bonus rewards. Awarding a flat gold amount to that same owner on top is additive, no
  new attribution plumbing needed.
- **Lane creep kills** (S170-139): explicitly named in that section's own doc comment as rewarding
  "nothing" today -- "lane creep kills (by heroes or by each other) remove a threat and nothing
  more; wiring them into a future economy is a natural follow-on once one exists, not invented here
  as a standalone reward just for this pass." This is that follow-on. `ArenaLaneCreep` doesn't
  currently track a last-attacker field the way jungle creeps do -- needs the same field added
  before this can attribute kills correctly (small, mechanical, same pattern already proven).
- **Hero kills**: the obvious real-MOBA source, not currently rewarded at all beyond Duck's
  E ("Chosen One") which is a stat buff, not currency. A flat or HP%-scaled bounty on the killing
  blow, same `apply_damage`'s-death-branch attribution point jungle creeps already use.
- **Structures** (§19.5): a real gold payout on destruction is what makes a lane creep wave's
  eventual push actually matter economically, not just territorially.

Deliberately **not** a passive per-second gold trickle (unlike LoL's own base gold-over-time) --
this map has no "laning phase" concept to protect with passive income; every source above is
earned through actual map presence/combat, matching the roster's existing "no free stat gains,
everything ties to doing something" pattern (mana trickles slowly even out of combat, but that's
a *resource*, not power progression).

### 19.3 Gold: what it buys

`docs/HEROES_VS0.md`'s **Starting Item Roster** (12 items, written 2026-07-23) is already a
complete, real, LoL-Season-3-archetype-styled stat-line spec -- crit carry, on-hit carry, burst
mage, DPS/utility mage, tank initiator, tank/MR, lifesteal duelist, physical/magic penetration,
mobility, support aura -- flagged in its own "Open/Deferred" section as "placeholder numbers... to
be tuned once wired in," never actually connected to any code. This is that wiring's target, not a
new item design pass: gold buys from this existing 12-item list, prices to be assigned when this
gets built (not decided here).

Mechanically, most of these stat lines land on hooks that already exist or are trivial extensions
of ones that do -- `arena_hero_armor()` already exists as a per-hero armor lookup (currently a flat
function of hero_id only; would need to become `base_armor + item_bonus_armor` instead of purely
hero_id-driven), `ARENA_ATTACK_DAMAGE`/`ARENA_ATTACK_COOLDOWN_MS` are currently flat roster-wide
constants (Gary is the one exception now, S170-163's own `ARENA_GARY_ATTACK_DAMAGE`/
`ARENA_GARY_ATTACK_COOLDOWN_MS`, carved out this same session for his homing auto-attack) that a
real AD/attack-speed item needs to become per-hero variables instead of shared `#define`s for
everyone else too (a real, nontrivial refactor, not a one-line change -- the other 25 heroes still
share identical base auto-attack numbers). Crit chance/damage, lifesteal, and penetration are
genuinely new mechanics this engine has never needed before (no RNG at all exists yet, per §17.2's
own finding while researching the auto-attack northstar) -- a real scope item for whoever builds
this, not hand-waved here.

### 19.4 XP and leveling: deliberately not over-built

A full per-level ability-point allocation system (the other half of a "real MOBA" reading of
"economy") is **explicitly out of scope for a first pass** -- this roster already has exactly one
kit slot per Q/W/R (no rank-up choices anywhere in `docs/HEROES_VS0.md`), and building a leveling
system this map/roster was never designed around risks the same "papers over a real gap" shortcut
§16's own Donkey entry already declined to take. First-pass shape instead: a flat, roster-wide power
curve keyed to elapsed match time + personal kill count (both already computable -- match time from
`match_elapsed_ms`, S170-157; kills from the same attribution points gold already uses) feeding a
small, generic stat multiplier (a scalar on top of `ARENA_ATTACK_DAMAGE`/HP/armor, same shape
attack-speed items in §19.3 already need) -- "you get stronger by playing and fighting," not "you
choose a build path via level-up screen." A real ability-point system is a legitimate future
expansion once this simpler shape is live and proven fun, not a prerequisite for shipping *an*
economy at all.

### 19.5 Structures: single-lane, matching the map this actually is -- original spec, superseded below

REDGARDEN's map is Arathi-Basin-shaped -- open field, 5 nodes, **no 3-lane structure** the way a
classic MOBA base has. Lane creep waves (S170-139) already march a single lane: each team's own
spawn line to the contested center node to the enemy's spawn line. Structures follow that same
single-lane geometry rather than inventing a 3-lane map layout this game was never built around --
one structure per team, placed roughly where each team's spawn line meets the open field (the
natural "last line of defense before the enemy reaches your base" position, same real-MOBA identity
a tower has, just one lane instead of three).

- **What it does**: blocks/slows enemy lane-creep advance (a real reason for a wave that wins its
  clash to keep pushing, S170-139's own "the actual 'push' payoff... is currently missing" gap,
  finally closed), attacks enemy heroes that linger in range (own HP/armor/attack-damage stats,
  same generic hero-adjacent-entity shape jungle creeps already established), and pays a real gold
  bounty on destruction (§19.2) -- the concrete "held ground matters beyond the resource-race meter
  at the top of the screen" identity both S170-139 and Duck's own W ("Government Clearance,"
  blocked on this exact gap since S170-31) were separately waiting on.
- **Not decided**: exact HP/damage/gold-bounty numbers; whether destroying a structure also affects
  `resources[team]` in any way (leaning toward *no*, keeping §19.1's separation clean -- a
  structure's payoff is gold + tempo, not a shortcut into the win-condition meter) or stays purely
  gold/tempo; whether a destroyed structure ever respawns (real MOBA precedent says no, permanent
  loss of that lane's defense) or this map's own graveyard/wave-respawn conventions suggest
  otherwise.

**Superseded (2026-07-30): what actually got built is node towers, not lane towers.** Founder,
real-time: "add towers around the nodes so beginning of game is a little slower" -- a different,
more specific ask than this section's own original single-lane-defense proposal above. Rather than
one structure per team gating the lane-creep push, what shipped is **one neutral tower per node
(all 5, `ArenaTower`, index-matched to `nodes[]`)**, hostile to BOTH teams equally, that directly
gates `arena_tick_nodes`' own capture channel: while a node's tower is alive, that node cannot be
captured by either side, full stop -- the actual mechanism that makes "the beginning of game" (the
opening node-grab race) slower, rather than this section's own original "slows the lane push"
framing. Reuses the exact hero-vs-creep combat shape (flat damage in, `apply_armor`'d damage out,
last-hit kill credit) rather than inventing a new one; never respawns once destroyed, a one-time
early-game gate rather than a recurring lane defense. Team-mode only (no towers in the 1v1 local
demo), same scope lane creep waves already carry. `ARENA_TOWER_MAX_HP`/`DAMAGE`/`KILL_FLOW`/`XP`
are judgment calls, not founder-specified, same "spec the model, leave the numbers open" precedent
this file uses elsewhere. This section's original single-lane-structure idea is not built and has
no current plan to be -- node towers cover the actual ask; a separate lane-push structure remains
a real, still-open idea if a future pass wants it, but is not scheduled. Wire-synced
(`ArenaTowerSnapshot`) and rendered client-side (tall stone-gray spire, darkening toward red as HP
drops, aggro-radius ring) -- the full loop a real online match needs, not just server logic.

### 19.6 Cooking direction, updated

`docs/CONSUMABLES_AND_COOKING.md`'s own worked example ("orange grove node → Oranges over time →
cooked into a mana-regen consumable") should be read as spending **gold** (§19.2), not
`resources[team]` -- the node-control-flavored *sourcing* (which specific node you hold determines
which raw ingredient you passively accumulate) can stay exactly as originally envisioned; only the
*currency* changes from the now-repurposed win-condition meter to the new per-hero gold pool. Not a
redesign of that doc's actual direction, just a currency correction now that §19.1's conflict is
resolved.

### 19.7 Sequencing and open questions

Gold (§19.2/19.3) is the real prerequisite -- structures' own gold-bounty-on-destruction payoff
(§19.5) needs gold to exist first to pay into, same dependency direction the original sprint-plan
item 5 already named ("the same blocker already named for Duck's W since S170-31... two independent
asks now point at the same missing system"). Recommended build order once someone picks this up:
gold sources/sinks (lane creep last-attacker attribution, hero-kill bounty, at least a handful of
the 12 items wired to real stats) before structures, structures before or alongside the XP power
curve (§19.4, lower-stakes, additive on top of whichever hero-power hooks item-stats end up using
anyway).

Not resolved here: exact gold values per source/item; which of the 12 items are simplest to wire
first (Seedling Charm -- flat +AD/+AP/+HP/+HP5 -- has no new mechanic at all and is the obvious
first item, unlike crit/lifesteal/penetration which each need real new mechanics this engine has
never had); whether structures get their own NORTHSTAR-numbered constants file section or fold into
the existing jungle/lane-creep constant blocks given how structurally similar they are to those
entities already.

## 20. Full creep overhaul — League of Legends parity (2026-07-29, S170-209) -- spec only, no code yet

Founder, real-time: "full creep overhaul lol parity northstar doc first currently creeps are spooky
too strong and hard to reason about." Same discipline §17 (auto-attack movement) already applied:
pin down exactly what real League's minion/jungle model actually does, name the honest gap against
REDGARDEN's current code, and propose a target design -- *before* touching a single damage number.
"Spooky" isn't a named creep type anywhere in this codebase (checked) -- it's the founder's own word
for how the current system plays: opaque and overtuned. That reading matters for how this section is
scoped: a pure numeric nerf pass would treat the symptom; this section treats it as a *model* problem
first, numbers second, same reasoning §17.5 gave for not resolving exact windup:backswing ratios in
that section either.

### 20.1 League's actual model (the gold standard)

**Lane minions are three roles, not one flat unit.** Each of League's three lanes spawns a wave every
30 seconds: 3 **melee** minions (front line, tankiest, highest single-target damage), 3 **caster**
minions (ranged poke, lower HP, fire from the back of the group), and every third wave adds one
**siege/cannon** minion (much tankier, much harder-hitting, and the priority target for actually
threatening a tower -- the mechanical reason "cannon wave" is a real, plannable moment top/mid/bot
laners route around, not incidental flavor). All three roles auto-fight anything they aggro exactly
the same way; the role only changes their own stat block and (for melee) their position at the front
of the clash.

**Wave clashes are fully automatic -- players don't control minions at all.** The moment an enemy
wave (or an enemy champion) enters a minion's aggro range, it engages on its own; two opposing waves
meeting mid-lane fight each other with zero player input, the same "passive-until-approached, active
once engaged" shape most aggro-based creeps everywhere share, League included.

**Minion aggro can be redirected by combat, not just proximity.** A champion who attacks an enemy
champion while that enemy is within an allied minion's aggro range draws that minion's aggro onto the
attacker -- "minion aggro" -- discouraging free, riskless poke onto an enemy standing in front of
their own wave. Minions do not, however, chase indefinitely: aggro has a leash, and a minion that
chases too far off its own wave's position resets back to defending the wave/pushing the lane.

**Last-hit is the entire skill expression of laning, and it exists because of a very specific
mechanical split: minions do almost all the damage to each other automatically; the player's own
auto-attack is only there to land the final, precisely-timed point of damage.** Gold for a minion
kill goes ONLY to whichever champion's damage brought it to 0 HP -- not to whoever dealt the most
damage, not split among participants. XP, by contrast, is NOT killer-exclusive: every allied champion
within a fixed radius of the dying minion gets full XP regardless of who landed the kill (a support
standing next to their laner's wave still levels up). This split -- gold is precise and individual,
XP is generous and shared -- is itself the design, not an incidental implementation detail.

**Deny is last-hit's mirror image.** Once an allied minion drops below 50% HP, an enemy champion
can no longer kill it (a real League rule: minions can't be finished off by the enemy team below that
threshold) -- but an ALLIED champion can still attack their own low-HP minion to kill it themselves,
denying the enemy team the gold entirely and roughly halving the XP they'd have gotten from it. This
is why competent laning involves attacking your own minions, not just the enemy's.

**Structures give minions somewhere to matter beyond the kill reward.** A wave that wins its clash
keeps marching and eventually reaches a tower, which minions (especially a cannon minion) meaningfully
damage over time if left unanswered -- the actual reason winning lane matters beyond a KDA line: an
uncontested, winning wave is slow, inexorable structure pressure, not just a farm/XP faucet.

### 20.2 REDGARDEN's current model (gap analysis, grounded in the actual code)

**Two entirely separate systems exist today, and they map onto DIFFERENT halves of League's model --
neither one is really "the jungle" or "the lane" in the League sense, which is itself likely a real
source of the "hard to reason about" complaint.**

**Lane creeps (`ArenaLaneCreep`, S170-139) are the closer analog to League's minion waves, but
collapsed to one role.** A single lane (spawn line x=-8/+8 to the center node at 0,0 to the enemy
spawn line -- `packages/simulation/arena_game.h`'s own doc comment above `ARENA_LANE_WAYPOINT_COUNT`
already names this as a deliberate single-lane MVP simplification of Arathi Basin's open-field shape,
not an oversight) spawns a 3-creep wave every 20 seconds (`ARENA_LANE_WAVE_INTERVAL_MS`), all three
creeps sharing one identical stat block (`ARENA_LANE_CREEP_HP` 60, `ARENA_LANE_CREEP_DAMAGE` 7) --
no melee/caster/siege split of any kind, and no stat growth over the course of a match. They DO
already fight each other automatically on aggro (`arena_tick_lane_creeps`' own nearest-hero-or-nearest-
opposing-creep target selection) and DO stop to fight rather than march past -- the actual "wave
clash" shape is real. But: no minion-aggro-redirect-on-champion-attack exists at all (a hero can poke
an enemy standing right next to their own wave with zero retaliation risk from the creeps
themselves); no deny (`arena_hero_attack_lane_creeps` explicitly skips a hero's own team's creeps --
`if (creep->team == h->team) continue`, so a friendly creep can't even be targeted, let alone denied);
gold/XP (`ARENA_LANE_CREEP_KILL_FLOW`/`_XP`) goes only to whoever's hit brought a creep's HP to 0,
which happens to already be a real last-hit-shaped reward (since creep-vs-creep damage and hero
auto-attacks are two independent sources converging on the same HP pool -- this half of League's
split is closer to already-correct than it looks, see §20.3) -- but there is no XP-share radius at
all, so a support standing next to their laner gets nothing from a kill they didn't personally land.
No tower exists (already flagged in this file's own header comment above `ARENA_LANE_WAYPOINT_COUNT`
and in §19's structures section) -- a wave that survives to the enemy spawn line just despawns.

**Jungle creeps (`ArenaCreep`, S170-51/161) are NOT League jungle camps at all -- they're node-
ownership guardians, a fundamentally different mechanic wearing jungle-creep terminology.** One
creep per capturable node (`ARENA_MAX_CREEPS` == `ARENA_NODE_COUNT`, index-matched), in two flavors:
a NEUTRAL camp sitting at any unowned/contested node (static, tanky -- 80 HP, the "prize" a team
fights through to help capture that node), and a TEAM-flavored creep that spawns once a team owns a
node and then actively marches toward whichever node ITS OWN team doesn't yet own (`ARENA_CREEP_
MARCH_SPEED`, S170-161) -- a "home-turf projecting outward" push mechanic with no equivalent
anywhere in real League jungle camps (Blue/Red buff camps, Krugs, Raptors, Dragon, Baron, Herald --
none of them move, none of them belong to a team, none of them exist because of node/objective
ownership). There is no camp variety (no buff-on-kill, no epic-monster global-relevant objective, no
smiting), and — critically for the "too strong" complaint — team-flavored creeps deal FLAT, unmitigated
damage (`ARENA_CREEP_TEAM_DAMAGE`, applied via a raw `apply_damage` call with no `apply_armor` pass,
unlike every hero-vs-hero and hero-vs-lane-creep damage source in this codebase) at a relatively high
tick rate (1500ms cooldown) against low-HP early heroes, with a march path that's dynamic and
unpredictable rather than a fixed camp position a player can learn and route around -- exactly the
"hard to reason about" shape a static, on-a-fixed-timer League jungle camp doesn't have. Notably,
this was already tuned down once (S170-161 cut team-creep HP 40→26 and split out a lower
`ARENA_CREEP_TEAM_DAMAGE` from the old shared damage constant, founder: "tone down the strength of
the team creeps just a bit they are so strong") -- the founder calling them "spooky... strong" again
now suggests either that pass wasn't enough, or (more likely, given this section's own framing) the
complaint was never really about the raw numbers at all, but about the armor-bypass + unpredictable-
position combination making them feel unfair rather than just difficult. Jungle creeps here also
never fight each other (their aggro loop only ever scans `ARENA_MAX_HEROES`, never other creeps) --
so unlike lane creeps, there is no last-hit skill expression possible on a jungle creep at all: it's
a flat race to whoever's already in range when its HP hits 0, generally whichever hero has simply
been trading blows with it the whole time.

**§8's own "the jungle is alive and dynamic, not static camps... grafted onto GoblinFoxDragon's mob/
NM/loot systems" direction is a real, different, FUTURE system** -- not what `ArenaCreep` is today.
Conflating "the thing currently called jungle creeps in the code" with "the actual League-style
jungle §8 describes" is itself a likely source of confusion for anyone reasoning about this system,
founder included; §20.3 below treats them as two separate concerns rather than trying to make one
system satisfy both descriptions at once.

### 20.3 Target design for parity

- [ ] **Split the single lane's wave into melee + caster roles**, sharing the existing one-lane
  geometry (§20.2's own citation of the header comment already treats multi-lane as unscoped, not
  a gap this pass needs to close -- see §20.4). Siege/cannon-every-third-wave is real League depth
  but a reasonable stretch goal, not required for a first pass at "roles exist at all."
- [ ] **Minion-aggro-redirect on lane creeps**: a hero attacking an enemy hero within an opposing
  lane creep's aggro radius should draw that creep's aggro onto the attacker, the single biggest
  missing piece of real lane-trading risk (§20.1's own "minion aggro" paragraph) and the most
  legible near-term win for making lane creeps read as a real hazard rather than passive scenery.
- [ ] **Deny for lane creeps**: allow a hero to target their OWN team's lane creep (currently
  impossible -- `arena_hero_attack_lane_creeps` filters same-team creeps out entirely) once it's
  below 50% HP, killing it and denying the enemy the reward. Confirm/decide whether REDGARDEN wants
  the exact "can't be killed by the enemy below 50%" half of the real rule too, or just the "an ally
  CAN kill their own" half -- these are two separate rule halves league bundles together and either
  could ship independently.
- [ ] **XP-share radius on lane creep kills**: currently killer-only (`h->xp += ARENA_LANE_CREEP_
  KILL_XP` on the single hero whose hit landed); real parity grants XP to every allied hero within
  some radius of the kill regardless of who landed it, keeping gold individual/precise.
- [ ] **Confirm (not rebuild) that last-hit already basically works for lane creeps.** Because
  lane-creep-vs-lane-creep damage (`arena_tick_lane_creeps`) and hero-vs-lane-creep damage
  (`arena_hero_attack_lane_creeps`) are two independent sources hitting the same `hp` field, a hero
  landing the final point of damage on a creep already-weakened by its own wave's clash already
  reproduces the actual shape of League's last-hit mechanic -- this needs tests confirming the
  behavior and a doc-comment callout, not new code, and should be verified before assuming it's a
  gap that needs building.
- [ ] **Route jungle (node-guardian) creep damage through `apply_armor`**, same as every other
  damage source in this codebase, so hero armor items actually matter against them -- named in
  §20.2 as a likely real contributor to "too strong," independent of any HP/damage retuning.
  Retuning the actual numbers is explicitly NOT decided here (§20.4).
- [ ] **Legibility pass on node-guardian creeps**: a visible aggro-radius ring (same idiom as the
  existing R-zone/cast-radius circles, S170-200) so a player can SEE the boundary rather than
  learning it by taking an unexpected hit, particularly valuable given their march path already
  makes their position unpredictable in a way a fixed camp wouldn't be.
- [ ] **Rename/reframe node-guardian creeps away from "jungle creep" terminology** if they stay
  structurally as-is (§20.4's first open question) -- they are not League jungle camps, and naming
  them like one is a likely contributor to "hard to reason about" independent of any mechanical
  change: the mental model a player brings from "jungle creep" (a static camp you route to on your
  own time) doesn't match what this entity actually does (marches at you, tied to node ownership).

### 20.4 Open questions, not resolved here

- **Does REDGARDEN keep the current node-guardian creep system as its own (renamed/reframed) thing,
  with a SEPARATE new true jungle-camp system added alongside it per §8's own ecology direction --
  or should node-guardians be reworked in place to absorb real jungle-camp behavior instead?** These
  read as two different games mechanically (a capture-point guardian vs. a routeable neutral camp
  with its own buff/objective economy) and this section deliberately does not force a merger. Given
  §8 already describes the real jungle as a FUTURE graft onto GoblinFoxDragon's live mob/NM/loot
  systems, "keep node-guardians as their own thing, build real jungle camps as a genuinely separate
  later system" reads like the lower-risk default, but is not decided here.
- **Multi-lane geometry** (a real 3-lane split vs. the current single center lane) is a map-shape
  question, not a creep-model question -- §8's own "not scoped further yet, no map file, no concrete
  node layout" line still holds as of this section being written. This section's role/aggro/deny/
  XP-share proposals in §20.3 are all designed to work unchanged whether REDGARDEN ships one lane or
  three; the lane COUNT is explicitly out of scope here.
- **Exact numeric retuning** (HP/damage/respawn/cooldown values, for either creep system) is
  deliberately not decided in this section -- same "spec the model, not the numbers" discipline
  §17.5 already applied to windup:backswing ratios. §20.3's armor-mitigation and legibility items are
  proposed as likely bigger levers on the "too strong and hard to reason about" complaint than a
  flat numeric nerf pass would be, but that's a hypothesis for whoever picks this up to verify against
  actual playtesting, not a conclusion this section reaches on its own.
- **Cannon/siege minions and structure-pushing** (§20.1's last two paragraphs) are real League depth
  this section intentionally treats as stretch goals, gated behind the same missing-structures gap
  §19's own structures section already tracks -- a wave with nothing to push against can't
  meaningfully carry a siege minion's whole reason for existing yet.

This section's job -- pin down the real League model, name the actual gap in REDGARDEN's own code
(two different systems, neither one a clean match for either half of League's model), and propose a
sequenced target design -- is done, per the founder's own "lol parity northstar doc first" framing.
No code changes accompany this section; S170-209's implementation phase (sprints against §20.3's
checklist) is separate, future work.

### 20.5 Cannon/siege minions -- re-checked 2026-08-14, real blocker found, not the one §20.4 named

Founder real-time: "at some point ensure the CANNON/siege-minion feature lands clean." Re-checked
against the actual current codebase before touching anything, since §20.4 (above) is now three
weeks stale and a lot has shipped since: §20.4's own stated blocker -- "a wave with nothing to
push against" -- is **resolved**. Real towers exist now (`ARENA_TOWER_MAX_HP`/`_DAMAGE`/
`_KILL_FLOW`, real projectile combat, `packages/simulation/arena_game.h`), and §20.3's melee/
caster role split shipped for real too (S170-218, `ARENA_LANE_CREEP_MELEE`/`_CASTER`).

But a **different, deeper blocker** turned up that neither §20.4 nor S170-218 flagged: lane
creeps and towers are on two geometrically unrelated systems, with **zero code path between
them today** (confirmed: grepping the whole simulation for any lane-creep-vs-tower interaction
returns nothing). Lane creeps walk a fixed 3-point path (`lane_creep_waypoint` --
team-spawn → center (0,0) → enemy-spawn, a straight line). Towers sit at the 5 scattered
Arathi-Basin-style capture nodes (`ARENA_NODE_COUNT`, S170-119's own "real Arathi Basin has 5"
framing) -- a capture-point layout, not a lane-tower layout, and not necessarily anywhere near
the lane's own straight-line path. A cannon minion's entire identity is "hits towers harder";
building that on top of a wave that structurally can't reach a tower at all would ship a
reskinned melee creep with a name that lies about what it does.

**Real open question, not resolved here** (matches this section's own established discipline of
naming the gap rather than forcing an answer): does the lane path get re-anchored so its final
waypoint(s) sit at/near real node positions (making "push the lane" and "capture a node" the same
act, a bigger design change than it sounds), or does tower engagement need its own separate
lane-to-node proximity check bolted on without moving the path itself (smaller, but a lane creep
"sieging" a tower it never geometrically approached would look wrong)? Neither is decided here on
purpose -- this is a real design call, not an implementation detail.

**Once that's settled**, cannon minions themselves are a small, well-understood addition on top --
same shape as S170-218's own caster role: a third `ArenaLaneCreepRole` value, spawned in place of
one melee slot on a wave-count cadence (a `lane_wave_count[2]` counter needs adding first, doesn't
exist yet either), higher HP/damage than melee, plus a flat bonus specifically against tower HP.
Not built this pass -- the geometry question above needs a real answer first, and guessing at one
risks exactly the kind of half-integrated feature this section's own §20.4 was written to avoid.

## 21. Reinforcement learning for the arena bot AI — reward-driven, Unity ML-Agents-shaped (2026-07-29, S170-223)

**Status update (2026-07-29, S170-224/225/226/227): the full pipeline this section specs is now
built AND actually run, live, end to end.** `apps/arena_training/src/headless.c` (the C
environment API) and `packages/common/mlp_infer.c`/`.h` (the embedded-MLP inference engine) are
real, compiled, and covered by hand-verifiable headless tests. `gymnasium`/`stable-baselines3`
(not installable via a normal `pip install` in this environment -- no venv module,
externally-managed system Python, no sudo) were installed via `pip --break-system-packages` once
the founder explicitly asked to actually run this here, closing the "written to spec, not run"
gap every earlier verification note in this pipeline had flagged: a real 4000-timestep PPO smoke
run trained cleanly against a real `gymnasium.Env` (`SubprocVecEnv`, real checkpointing, real
evaluation -- 5W/0L/0D vs. the heuristic bot AI, too short a run to read much into that number
itself). Exporting that REAL trained model caught a genuine bug the earlier hand-built synthetic
test network never hit (exact-integer weight values -- real in an actual trained model's own
untrained biases, essentially never in random synthetic test data -- produced invalid C float
literals); fixed, and re-verified against the real model: PyTorch's own output and the compiled
C header's forward pass match to float32 precision. Still not done, honestly: only a short smoke
run has happened, not a real training run long enough to produce a meaningfully strong policy,
and wiring a trained policy into the LIVE bot AI decision loop is separate, future work -- this
pipeline trains, exports, and syncs weights; nothing in a real match calls them yet.

Founder, real-time, immediately after S170-220's corpus-based unsupervised pretraining pipeline
shipped: "running training on a corpus of games is cool but thats not what i actually want right
now i want unsupervised learning with rewards like in the unity ml-agents plugin." A real
correction, not an extension -- next-token prediction over a static replay corpus (S170-194/195/
220, and NORTHSTAR §12 Phase E / gpt2-alpine-c's own `GAME_AI_NORTHSTAR.md`) has no notion of
"good" or "bad": it learns to continue a sequence the way the corpus already did, whether that
corpus was full of winning or losing play. What the founder is describing is genuine
reinforcement learning -- an agent takes actions, a reward signal scores them, a policy improves
by directly optimizing expected reward, the actual Unity ML-Agents shape (Agent.CollectObservations
→ policy → Agent.OnActionReceived → Agent.AddReward, trained via PPO against a Python
process, then exported to run standalone at inference time). Neither this repo nor the two AI
docs above spec that anywhere -- this section does, and S170-220's own GPT-2 infrastructure is
**not replaced or wasted by this** -- it's a different tool already built for §12 Phase E's own
different, still-valid imitation-learning-plus-self-play direction; this section adds RL as a
second, separate lineage, not a swap.

### 21.1 The real precedent already in this org: SHANKPIT

A repo-wide check (this session's own "check what already exists first" discipline) found the
right shape already real, in the sibling SHANKPIT repo, not invented from scratch here:

- **`SHANKPIT/apps/training/headless.c`** -- a minimal, real, already-compiling C shim exposing
  exactly three functions for a Python process to call directly (via `ctypes`, no network, no
  serialization protocol): `sim_init(bots)`, `sim_step(fwd, strafe, yaw, pitch, shoot, jump)`,
  `sim_get_state()` (returns a pointer to the live `ServerState`). Player 0 is "the Agent"; every
  other player in the match is the existing hand-authored bot AI, playing as the opponent --
  training needs no separate opponent-AI system, it reuses what already exists to practice
  against.
- **`SHANKPIT/packages/simulation/neural_net.h` + `brain_weights.h`** -- a small, literal,
  compiled-in-C-arrays MLP (`dense_layer`, ReLU/tanh, `bot_brain_forward`: 8 inputs → 256 → 128 →
  4 outputs) -- this is the actual "embed the weights right into the C code" pattern the founder
  described two sections ago, just realized with a small fixed-size numeric policy net instead of
  GPT-2's token-generation shape (S170-220's own research already found gpt2-alpine-c does NOT do
  this -- SHANKPIT does, for a differently-shaped model). A small MLP is also the standard shape
  Unity ML-Agents itself defaults to (fixed observation/action vectors, not token sequences) --
  this is the closer analog to what was actually asked for.
- **`SHANKPIT/packages/simulation/local_game.h`'s own reward shaping** (`accumulated_reward`,
  ticked in `update_entity`) -- dense per-tick shaping (`+0.05` alive, `+0.1` engaged in combat
  within 25 units) plus event-based reward (`+0.5 * damage_dealt`) is real, working precedent for
  the shape §21.2's own reward function below follows, adapted to REDGARDEN's own MOBA state
  instead of SHANKPIT's FPS one.
- **What SHANKPIT does NOT have**: any Python-side training loop at all. `headless.c`'s own
  README section documents an "Expected Functionality" training loop in pseudocode, not real
  code -- no `gym`/`stable_baselines3`/`PPO` usage anywhere in that repo. The C-side environment
  API shape is real, proven precedent; the actual RL algorithm/trainer is not -- this section
  specs building that part for the first time, informed by but not copying nonexistent code.

### 21.2 Target architecture for REDGARDEN

**Environment (C side, new, small, mechanical):** `apps/arena_training/headless.c`, same
three-function shape as SHANKPIT's own --
- `void sim_init(int hero0_id, int hero1_id)` → `arena_init_with_heroes(...)`, the exact same 1v1
  local-demo path this whole repo's own tests already exercise headlessly, no display needed.
- `void sim_step(float move_x, float move_z, int cast_q, int cast_w, int cast_r, unsigned int
  dt_ms)` → sets hero 0's ("the Agent") move target and cast flags directly via the same
  `arena_set_move_target`/`arena_cast_q`/`arena_toggle_w`/`arena_cast_r` calls `apps/arena`'s own
  local-mode client already uses, then one `arena_update(dt_ms)` tick. Hero 1 needs no separate
  opponent code at all -- `arena_bot_enabled` (already defaults to 1) drives it through the
  existing `bot_cast_kit_if_ready`/`arena_bot_tick` heuristic AI, same "practice against what
  already exists" reasoning as SHANKPIT's own Player-0-vs-bots framing.
- `ArenaState *sim_get_state(void)` → returns `&arena_state` directly (already an extern global).
- A `sim_reset(int hero0_id, int hero1_id)` alias for starting a fresh episode without a fresh
  process (episodes need to be cheap -- PPO needs thousands of them).

**Environment (Python side, new):** a `gymnasium.Env` subclass loading the compiled shared
library via `ctypes`, walking `ArenaState`'s own field layout (mirrored as a `ctypes.Structure`,
not re-serialized to text -- numeric fields read directly, no string round-trip) to build a fixed
observation vector (self hp/mp/position/cooldowns, foe hp/position, distance, same information
`arena_serialize_state` already exposes in text form, just numeric here since a small MLP policy
wants floats, not tokens) and a small discrete/continuous action space (move direction + 3 binary
cast flags, matching `sim_step`'s own signature exactly).

**Reward function (S170-223, founder: "do all the reward engineering" -- designed here, not
deferred), dense shaping + sparse terminal, same two-tier shape SHANKPIT's own precedent and the
standard MOBA-RL literature (OpenAI Five, AlphaStar) both use -- shaping keeps the learning signal
alive long before the agent can reliably land a kill; the terminal term is the actual objective
and is weighted to dominate any one episode's accumulated shaping:**
- Damage dealt to foe: `+0.01` per HP.
- Damage taken from foe: `-0.01` per HP (symmetric).
- Foe killed: `+5.0`. Self died: `-5.0`.
- Flow gained: `+0.001` per point (a MOBA agent that farms competently is on-track before its
  first kill, same reasoning real MOBA junior-jungle/laning coaching gives).
- XP gained: `+0.0005` per point (smaller, secondary economy signal).
- Alive this tick: `+0.001` (SHANKPIT's own "alive = good" term, deliberately tiny so it can't
  outweigh actually engaging -- a purely passive agent should not out-score an aggressive one).
- Episode ends in a win: `+10.0`. Loss: `-10.0`. Timeout/draw: `0`.
All deltas computed in Python between consecutive `sim_get_state()` snapshots, not accumulated
in C (unlike SHANKPIT's own `accumulated_reward` field) -- keeps the reward function itself
iterable without a C recompile every time it's tuned, a real practical win during RL
experimentation specifically.

**Trainer:** Stable-Baselines3's PPO against the `gymnasium.Env` above -- the standard, actively
maintained choice for a custom Gym environment (no existing PPO/RL trainer anywhere in this org
to reuse instead, confirmed by the SHANKPIT check in §21.1). Runs equally well locally (this
sim has no display dependency, confirmed by this whole repo's own headless test suite) or on
Colab, same delivery pattern S170-220 already established for the corpus pipeline.

**Weight embed + git-sync:** reuses S170-220's own established shape, adapted to the smaller
network -- after training, extract the PPO policy network's weights (SB3's default `net_arch`,
a small MLP, not GPT-2-shaped) and write them as literal C float arrays (SHANKPIT's own
`brain_weights.h` pattern -- genuinely appropriate here, unlike GPT-2-small, since a PPO policy
MLP is small enough -- a few thousand params, not millions), then commit + push to `origin/main`
via the same SSH-key-in-`MyDrive/.ssh` flow `git_sync_weights_to_repo()` already implements.
Inference: a new small, dependency-free C module mirroring SHANKPIT's own `neural_net.h`
(`dense_layer`, matmul + bias + ReLU/tanh) -- deliberately NOT `packages/common/gpt2_infer.c`,
which is the wrong shape for a small fixed-size-vector policy net.

### 21.3 What this section deliberately does not resolve

- **Exact network architecture** (hidden layer sizes, activation functions) for the PPO policy
  net -- SB3's own default (`net_arch=[64, 64]`, two hidden layers) is a reasonable starting
  point, not confirmed as final tuning, same "spec the model, not the numbers" reasoning §17.5/
  §20.4 already applied elsewhere in this file.
- **Self-play / curriculum.** This first pass trains against the existing rule-based heuristic
  bot AI only (matching SHANKPIT's own current precedent) -- training against a population of
  past policy checkpoints (real self-play, AlphaStar/OpenAI-Five-style) is real, valuable, later
  depth this section doesn't build.
- **Team-mode (10v10) training.** §21.2's environment still targets 1v1 combat specifically
  (one agent, one opponent, no nodes/squads/teammates in the observation or reward at all) --
  multi-agent RL (coordinating a full team, objective-aware) is a substantially harder problem
  than single-agent PPO against a fixed opponent, still out of scope. What DID land (2026-07-29):
  the 1v1 training arena no longer always starts both heroes fixed near map-center
  (`scripts/rl_env.py`'s `reset()` now calls the new `sim_set_hero_position()` to randomize both
  spawns across the real map extent each episode, `MOVE_TARGET_RANGE` matching the real
  `ARENA_HALF_EXTENT` instead of a conservative fixed 20.0) -- this closes the specific
  coordinate-frame generalization gap found below, but the policy still has no concept of a
  teammate, a node, or an objective; it only ever reasons about itself and one foe.
- **Wiring the trained policy into a LIVE multiplayer match.** RESOLVED for the real networked
  match bots (2026-07-29, REDGARDEN Apple #11301): `apps/arena_bot`'s 19 real bots now consult
  `rl_policy_forward()` for a bounded movement nudge during close-range engagement (additive on
  top of the existing hand-authored heuristic's anti-stack angle spread, not a full replacement
  -- see that Apple's own doc comment for the full design). Real coordinate-frame mismatch found
  in the process: the policy's own action output is an ABSOLUTE world-space target, clipped to a
  range tuned for the small, always-near-origin 1v1 training arena; reusing it unmodified for a
  hero anywhere on the much larger real map would aim at map-center nonsense. Worked around on
  the C side with a nudge-not-teleport reinterpretation, and being fixed at the source by the
  spawn-randomization/action-range change noted just above -- a policy trained under the new
  environment should need that C-side workaround less, not more, once promoted. `apps/arena_server`
  itself (the solo 1v1 local-practice mode, `arena_game.c`'s own `arena_bot_tick`) has called
  `rl_policy_forward()` since S170-228 already; what was missing until now was the real
  networked-match path.

## 22. Real jungle camps — mob roster + GFD-pattern lifecycle (2026-07-30) -- spec only, no code yet

Founder, real-time: "we want to make the jungle more dynamic and alive those concepts come from
the original game" -> "the jungle right now is like nothing we need more going on" -> "use it as
inspiration in terms of mob types and write it into a northstar." Resolves §20.4's own open
question left deliberately unanswered one day ago: **REDGARDEN builds a genuinely separate true
jungle-camp system alongside the existing node-guardian creeps (`ArenaCreep`), not a rework of
them in place.** §20.4's own reasoning for why that's the lower-risk default still holds --
node-guardians are a capture-point mechanic wearing jungle terminology (§20.2 already
established this in detail); a real jungle camp is a different game object with a different job
(routeable neutral ground a player learns and plays around, not a thing that marches at you
because of who owns a node). This section is that second system's design.

### 22.1 Where the mob-type ideas come from, and what actually transfers

The founder pointed at `REDGARDEN/wiki/SPEC-4` ("RED GARDEN: CORE SYSTEMS IMPLEMENTATION") as the
inspiration source -- a full three-file C spec (`entity_behaviors.h`/`grid_tick.h`/
`card_system.h`) for a completely different, unbuilt game mode: a deck-based Card-RTS layered on
a Conway's-Game-of-Life living map. That whole system is NOT what's being adopted here -- it
targets `packages/simulation/local_game.c`'s own card/deck/influence economy (a different game
mode from the arena/MOBA this whole file specs), and its `Entity`/`GridCell` type names collide
outright with `local_game.h`'s own existing, incompatible definitions of both (checked directly:
`local_game.h`'s `Entity` is `id/type/owner/grid_x/grid_z/x/z/hp/cooldown_ms`; SPEC-4's is a much
richer combat-AI struct — dropping SPEC-4's headers in as-is would not compile against the
existing card-RTS code). None of that is what "use it as inspiration" is asking for. Two ideas
FROM it genuinely transfer to the arena jungle, independent of the rest of the spec:

1. **A tiered mob roster with distinct archetypes, not one flat stat block.** SPEC-4's
   `ENTITY_STAT_TABLE`/`AI_WEIGHT_TABLE` give each of 16 unit types a real personality: a
   frontline brawler that clumps and fights (Militia), a kiting ranged harasser that never lets
   melee close (Scout), a zerg-rush swarm that dies fast but comes in numbers (Swarmling), an
   objective-hunter that ignores units in favor of structures (Ravager), a pure-support buffer
   that never attacks (Hexbound/Tidecaller), an anchor tank (Behemoth), a high-threat assassin
   that targets whoever hits hardest (Shade), an AoE caster (Pyromancer), and escalating elite/
   boss tiers up to a deterministic Dragon. REDGARDEN's current node-guardian creeps have exactly
   one behavior each (sit still, or march at a node) -- nothing like this variety exists anywhere
   in the arena today.
2. **Weighted, per-archetype target scoring**, not a single hardcoded rule. SPEC-4's
   `find_best_target()` scores every valid enemy on a weighted sum (closest / lowest-HP /
   highest-threat, each archetype's own `AIWeights` picking which terms matter and by how much)
   instead of always picking one fixed criterion. REDGARDEN's current jungle/lane creep AI always
   just targets nearest-hero-or-nearest-creep -- adopting a weighted-scoring model (ported as a
   design pattern in C, not literal shared code, matching how this file already treats every
   other cross-language/cross-repo borrowing) is a real, load-bearing upgrade independent of
   anything else in SPEC-4.

**What does NOT transfer:** the card/deck/hand/influence economy (that's `local_game.c`'s own
domain, a different game mode this file's own header table already separates from the arena);
the Conway grid-cell ecology (`grid_tick.h`'s conversion/corruption/stability simulation) --
REDGARDEN's arena already has its OWN living-board layer (§1's automata grid, referenced
throughout §8) and does not need a second, competing one grafted in from a different spec;
SPEC-4's own generic-fantasy names (Militia, Ravager, Behemoth, Hexbound) -- see §22.4's open
question on tone.

### 22.2 The other half: §8's own GFD-graft direction, not abandoned

§8 already committed to a specific architecture for this, before SPEC-4 ever entered the
conversation: "grafted directly onto the mob/NM/loot systems already real and working in
`GoblinFoxDragon`'s MUD... rather than building a second, separate creature system from scratch."
Checked directly (`GoblinFoxDragon/server/mob/mob.go`, `server/nm/nm.go`): GFD's mob system is a
real, tested (2,509 lines across mob/nm, with real test coverage) Go state machine --
`Idle → Pursuing → [leash exceeded] → Returning → Idle`, `Dead` as an absorbing state, tag-on-
first-hit claim rules (FFXI-style: whoever lands the first hit gets kill credit/loot rights,
permanent for the mob's lifetime, matching this arena's own existing last-hit-shaped reward
philosophy §20.2 already confirmed) -- plus a separate NM (Notorious Monster) package implementing
FFXI's real placeholder/window/respawn model: a rare monster either spawns in a probabilistic time
window after a specific weaker "placeholder" mob dies, or on its own fixed schedule, with a
configurable respawn timer once killed.

A literal code graft isn't possible -- GFD is a Go MUD server process, REDGARDEN's arena is a C
simulation with a completely different runtime, tick model, and network protocol. "Graft" here
means what every other cross-language borrowing in this file already means: port the DESIGN
(state machine shape, tag-on-first-hit, placeholder/window/respawn), not the code. Concretely:

- **Camp state machine**, one enum per camp instead of the aggro-radius-only shape node-guardians
  use today: `IDLE → PURSUING → RETURNING (leash exceeded) → DEAD → (respawn timer) → IDLE`.
  Leash range keeps a camp from being kited across the whole map (a real, current gap: nothing
  stops a hero from pulling a node-guardian creep arbitrarily far from its node today).
- **Tag-on-first-hit**, reusing the exact `last_attacked_by_owner` sentinel convention
  `apply_damage`'s own hero-kill-credit logic already established this session (§20.2's own "gold
  is precise and individual" confirmation applies here too) -- whoever lands the first hit on a
  camp claims kill credit for its lifetime, no last-hit-sniping from a third party who did
  nothing but land the final blow.
- **A placeholder/window/respawn boss**, the single biggest "the jungle feels alive and
  important" lever available: one rare, powerful, roaming elite camp (SPEC-4's own Dragon tier is
  the right shape, not necessarily the right name -- §22.4) that only becomes killable after its
  weaker placeholder camp(s) die and a window opens, matching League's Baron Nashor/Dota's
  Roshan real-MOBA precedent of a single epic, contestable, game-swinging objective the whole map
  reacts to. SPEC-4's own "deterministic dragon" section (seed RNG off spawn-tick + frame-count,
  no `rand()` calls) is directly reusable for exactly the reason it names: client/server
  determinism for a networked boss fight, and REDGARDEN's arena already has a server-authoritative
  tick this slots into cleanly.

### 22.3 How this coexists with node-guardians and lane creeps

Three creature systems, three different jobs, deliberately not merged (extends §20.2's own "two
entirely separate systems... map onto DIFFERENT halves of League's model" finding to three):

| System | What it actually is | Status |
|---|---|---|
| Lane creeps (`ArenaLaneCreep`) | League's minion-wave analog -- automatic, aggro-based, pushes a lane | Built (S170-139), role-split proposed in §20.3 |
| Node-guardian creeps (`ArenaCreep`) | Capture-point guardian, reflavored/reframed per §20.3/§20.4 | Built (S170-51/161), armor/legibility fixes proposed in §20.3 |
| **Real jungle camps (this section)** | **Routeable neutral ground with real archetype variety + a rare boss objective** | **Spec only, this section** |

None of §22's proposed camps live on or near the 5 capture nodes (that ground already belongs to
node-guardians) -- they occupy the open jungle terrain between nodes/lanes that §8 already named
as needing "real geography... rather than being placed arbitrarily," using the jungle obstacle
layout (`arena_obstacles_reset_layout`, S170-138/191) as the actual terrain they inhabit.

### 22.4 Open questions, not resolved here

- **Tone**: REDGARDEN's actual hero roster is absurdist (Unicorn, Duck, Bacon Puck, Flute Debt,
  Zagan) -- SPEC-4's generic-dark-fantasy camp names (Ravager, Behemoth, Hexbound, Voidreaver)
  read tonally mismatched against it. Real MOBA precedent cuts both ways (Dota's own jungle
  camps -- Centaurs, Satyrs, Wolves -- are played dead straight even against a wildly varied hero
  cast), so this isn't automatically wrong, but it's a real choice, not resolved here: keep
  SPEC-4's archetypes as reskinned-straight jungle fauna (serious tone, comic relief stays on the
  hero side only), or run every camp name through the same absurdist voice the hero roster and
  `TYLER/just_a_duck.md`'s own source material already established. Whoever picks this section up
  should decide before naming a single camp.
- **Exact roster size and numbers.** Same "spec the model, not the numbers" discipline §17.5/§20.4
  already applied -- how many camp archetypes REDGARDEN actually needs (SPEC-4's own 16-unit tier
  list is almost certainly more than a first pass needs), and all HP/damage/respawn/leash-range
  values, are explicitly not decided here.
- **Where exactly camps sit on the map** and how many total (SPEC-4's own card-RTS had no jungle
  geography to place them against; REDGARDEN's does, via the existing obstacle layout) is a
  concrete follow-up, not resolved here.
- **Does the placeholder/window boss need its own dedicated node**, or does it roam freely across
  the whole jungle the way GFD's own roaming mobs do? Not decided here.
- **Wire/HUD work**: a visible leash-range or aggro-range ring (same idiom §20.3 already proposes
  for node-guardians, S170-200's existing R-zone-circle convention) would matter at least as much
  here as it does there, given these camps are explicitly meant to be routeable/learnable terrain,
  but is not scoped further in this section. §22.5 adds a related detail: a growing/pulsing
  telegraph over the last few seconds of a camp's respawn timer, same legibility instinct.
- **Do camps grant a real player-power buff on kill** (§22.5), and if so, on what infrastructure
  -- extend the existing Warsong-Gulch-style map powerups (S170-190), extend the generic
  status-effect fields (`stunned_ms`/`slowed_ms`-shaped), or something new? Not decided here.

This section's job -- resolve §20.4's deferred architecture question, name what actually
transfers from the founder's own cited inspiration source versus what doesn't, and ground the
"alive" half of the design in a real, already-tested reference system (GFD's mob/NM packages)
rather than inventing a third parallel creature model -- is done. No code changes accompany this
section; implementation is separate, future work.

### 22.5 ECOWAR wiki follow-up (2026-07-30): what else transfers, and what doesn't

Founder, real-time, after §22.1-22.4 above already landed: "continue the jungle creep work -
check the EMILY wiki on github for ecowar" -> "i know thats another version of the game but some
of the bvibes are useful." Cloned `EMILY.wiki` fresh and read all three of its RED GARDEN origin
documents in full: `ECOWAR-game-spec-1.md`, `ECOWAR-game-spec-2.md`, and
`REDGARDEN-(ECOWAR)-SPEC-3.md`. Same discipline as §22.1: name what actually transfers,
independent of the rest, rather than treating this as a blueprint -- the founder's own framing is
explicit that ECOWAR is "another version of the game," not this one.

**First finding: spec-2 and spec-3 add nothing new.** Spec-2's "16 Units + 8 Structures" roster
(Militia, Scout, Swarmlings, Ravager, Hexbound, Verdant Behemoth, Shade Stalker, ...) is the same
material §22.1 already pulled from `REDGARDEN/wiki/SPEC-4` -- both documents are separate copies/
revisions of the same underlying Card-RTS design conversation, not independent sources. Spec-3 is
purely a scope/sequencing memo for that Card-RTS's first vertical slice (grid size, which 4 cards
ship first, a 5-day build order) -- entirely about `packages/simulation/local_game.c`'s domain,
nothing MOBA-shaped in it at all. Neither changes anything already written in §22.1-22.4.

**Second finding: spec-1 has one genuinely new, MOBA-shaped section §22.1 didn't have a source
for yet** -- "6. Jungle Camps & Dragons (MOBA DNA)," explicitly labeled as such in the original
document, distinct from the Card-RTS unit-roster material around it. Three concrete ideas from it
that are real gaps in §22.1-22.4 as written so far, named honestly against what would need to
exist first:

1. **Camps should visibly telegraph before they spawn/respawn**, not just pop back into existence
   on a bare timer the way §22.2's placeholder/window/respawn state machine currently implies. A
   real MOBA precedent (and the one this source names outright) for making the jungle "feel alive"
   rather than mechanical -- a growing/pulsing visual over the last few seconds of the respawn
   timer, same "the affordance is legible before you need it" instinct already behind this arena's
   own fountain-radius ring (S170-147) and R-zone-circle convention (S170-200). Needs no new
   system, just a wire/HUD detail on top of §22.2's own respawn timer -- folded into §22.4's
   existing "wire/HUD work" open question rather than a new one.
2. **Camps granting a real, temporary player-power buff on kill is a genuinely missing mechanic**,
   not just a naming detail. §22.1/§22.2 as written only give a jungle camp two rewards: kill
   credit (tag-on-first-hit) and presumably gold/loot, the same shape node-guardians and lane
   creeps already have. This source's "Neutral Camps... provide buffs" is the actual load-bearing
   MOBA jungle mechanic real games use it for (Dota's camp-stack buffs, League's own smite-buffs)
   that nothing in §22 currently specs at all -- a jungle camp that only pays gold isn't
   meaningfully different from a lane creep that pays more gold. This is a real, new open question
   (added to §22.4 below), not resolved here: REDGARDEN has no existing "temporary buff applied to
   a hero" primitive to hang this on (the closest analogs are the Warsong-Gulch-style map powerups,
   S170-190, and status-effect fields like `stunned_ms`/`slowed_ms` -- neither is quite "buff," both
   are candidate infrastructure to extend rather than something to build from zero).
3. **A boss's death should change something about how the rest of the match plays, not just grant
   a buff and end the fight.** This source's per-biome dragon effects (accelerates decay / spreads
   villages / mutates automata rules) don't transfer literally -- REDGARDEN's arena has no biome
   system (see below) for a biome-specific effect to key off of -- but the underlying pattern is
   real and cheap to keep in mind for whoever eventually builds §22.2's placeholder/window boss:
   its death should be a match-altering event (a global buff to the killing team, a permanent map
   change, an alert every player sees), not merely "a bigger creep with more HP and better loot."
   Not resolved here, just named so it isn't lost.

**What does NOT transfer, named explicitly:** spec-1's multi-biome map concept (Verdant Wilds /
Ash Barrens / Frozen Reach / Blighted Grid, each with its own gameplay-altering rules and its own
dragon) is a much larger, separate idea than "jungle camps" -- REDGARDEN's arena is one map with
one jungle-obstacle layout (`arena_obstacles_reset_layout`), not a set of biome zones, and nothing
in this session's scope is proposing to build one. Worth remembering as a real, bigger future
idea if the jungle work above ever ships and the map wants more variety, but it is explicitly out
of scope for §22 and not added to §22.4's open-question list -- naming it here is enough.

This closes out the founder's "continue the jungle creep work... check the EMILY wiki" direction:
both ECOWAR-sourced documents beyond SPEC-4 have now been read, what transfers has been named
(camp spawn telegraph, camp buffs as a real missing mechanic, boss-death-as-match-event as a
design pattern), and what doesn't (the Card-RTS material already covered via SPEC-4, and the
multi-biome map idea) has been named just as explicitly. No code changes accompany this section.

## 23. Expanded item roster — more FFXI-DNA items, more effect variety (2026-07-30) -- spec only, no code yet

Founder, real-time: "do a northstar for expanded items we just need more more variety more
different effects etc same DNA ffxi item names even the stats on some may be useful to design the
items system." Same discipline §20/§22 already applied: name the current shape and its real gap
before proposing numbers.

### 23.1 Current shape (grounded in the actual code)

27 items live (`ARENA_ITEMS`, `packages/simulation/arena_game.c`), across 11 equip slots
(`ArenaItemSlot`) and 3 purely cosmetic tiers (`ArenaItemTier` -- the tier label doesn't affect
mechanics at all, just how the shop groups things). Mechanically, `ArenaItemDef` has exactly six
stat fields, all flat and additive: `bonus_ad`, `bonus_max_hp`, `bonus_max_mp`, `bonus_armor`,
`bonus_move_speed`, `bonus_cdr_pct` (the newest, S170-207) -- every item in the catalog is a
weighted combination of those six numbers, with exactly **two** exceptions that introduce a real
new mechanic instead of just stats: Blink Dagger (S170-205, an instant tilde-bound teleport) and
Donkey (S170-206, an HP-threshold-triggered damage floor + fight-back, plus a separate
tilde-bound glide). Haste Trinket (S170-207) is the newest *stat*, not mechanic -- the first item
whose bonus compresses time (cooldown %) rather than adding a flat number.

**The FFXI-names choice is already made, already shipped, and already self-aware about the
tension it creates.** `ArenaItemTier`'s own doc comment states it plainly: `ARENA_ITEM_TIER_
GENERIC` is "real FFXI names, used verbatim" and `ARENA_ITEM_TIER_WEIRD` is "real FFXI end-game
weapons with real unusual reputations (Kraken Club, Ridill)." This is a real, direct divergence
from `docs/FFXI_ITEM_PARITY_SEED.md`'s own explicitly stated purpose, worth naming plainly rather
than glossing over: that doc's own header says its real FFXI names are "**not for direct use in
the shipped game**" and exist purely as seed/training material for `gpt2-alpine-c` to generate
KNIGHTS_OF_THE_VOID's own *original* names from ("reframed, not reproduced," the same relationship
`TYLER/multiverse_heroes.md` has to its own real-world mythology sources) -- explicitly "for true
ip." REDGARDEN's actual shipped catalog took the shortcut that doc was written specifically to
avoid: 24 of 27 current items are exact, verbatim, real Square Enix trademarked names (Kraken
Club, Ridill, Haubergeon, Peace Earring, and so on), not generated originals. The founder's own
framing this pass ("same DNA ffxi item names") reads as a clear, informed choice to keep doing
this, not a gap to silently fix -- so this section does NOT propose ripping out or renaming the
existing 27. It's named here so the choice is on the record as a real, live IP-exposure decision
rather than something nobody noticed, and so whoever eventually ships this commercially can weigh
it explicitly rather than discover it later.

### 23.2 What "more variety, more different effects" actually means mechanically

Six flat stats is a real, narrow ceiling -- most of what makes FFXI's own item system feel deep
isn't more slots, it's **effect categories this engine has never had at all**. Concretely, real
FFXI mechanic archetypes that translate into genuinely new `ArenaItemDef` fields (or small new
subsystems), each grounded in a shape this codebase already has partial precedent for:

- **Latent effects (conditional stats).** Real FFXI convention: "Latent effect: activates when HP
  is less than 25%." REDGARDEN already has exactly this SHAPE, just not generalized -- Donkey's
  own Immortal's Fold (`ARENA_DONKEY_FOLD_HP_FRACTION`) is a hard-coded, single-item version of a
  latent effect. Generalizing it (a `latent_hp_threshold`/`latent_bonus_*` pair any item could
  set, not just Donkey's own hardcoded one) turns one bespoke mechanic into a real, reusable
  category -- "clutch" items that come online only when a fight is going badly, real FFXI/DOTA
  design language neither this catalog nor any real MOBA item shop typically has.
- **Proc effects (Store TP+, Zanshin, Subtle Blow, Triple/Double Attack).** Real FFXI weapons
  frequently grant a chance-based bonus on a normal hit -- extra damage, an extra attack, reduced
  chance of being interrupted. This engine has genuinely never needed randomness before (§17.2,
  researching auto-attack parity, already found and flagged "no RNG at all exists yet" as a real
  gap) -- a proc-based item is real, new engine work (a seeded RNG source, `bonus_proc_chance`/
  `bonus_proc_effect` fields), not a stat-table addition, and should be scoped as such rather than
  hand-waved into "just add a field."
- **Regen/Refresh (ticking HP/MP).** A flat `bonus_hp_regen_per_sec`/`bonus_mp_regen_per_sec` --
  mechanically trivial (this file already has `ARENA_MP_REGEN_PER_SEC`/`_IN_COMBAT_PER_SEC` as
  roster-wide constants; an item-driven per-hero addition on top is the same shape `bonus_cdr_pct`
  already proved out for cooldowns). The easiest category here to actually ship first.
- **Relic/Mythic/Empyrean-tier unique-mechanic weapons.** `ARENA_ITEM_TIER_WEIRD` already exists
  as exactly this concept for exactly two items (Kraken Club's stat SHAPE, Ridill's own real
  "chance to double-attack" reputation -- currently unimplemented as an actual proc, just
  flavor-matched stats per that tier's own doc comment) -- expanding this tier with more real FFXI
  relic-class weapons (Aymur, the seed doc's own §6), each given ONE genuinely unique mechanic
  (following Blink Dagger/Donkey's own precedent of "one new primitive per item," not a shared
  generic system), is the highest-ceiling, most "more different effects" category directly
  requested, at the cost of being the most individually-bespoke to build (no shared plumbing
  reduces the per-item cost the way a generalized proc/latent/regen field would).
- **Enmity-adjacent aggro manipulation.** Real FFXI gear routinely raises/lowers "enmity" (how
  strongly a mob targets you). REDGARDEN's own creep/tower aggro (node-guardians, lane creeps,
  §19.5's proposed structures) currently has no threat/enmity concept at all -- just aggro-radius
  and (for structures) whoever's simply standing in range. An item that makes a hero more/less of
  a creep-aggro priority is a real, novel tank/support item category, but needs a real enmity
  primitive built first (bigger scope, flagged here as the least shovel-ready of this list, not
  attempted without that prerequisite).
- **Elemental/damage-type resistance.** Real FFXI gear routinely resists specific elements. This
  engine has exactly one damage type today (physical, mitigated by `arena_hero_armor()`) -- no
  magic/physical split, no elements. An elemental-resistance item literally cannot exist until
  REDGARDEN decides whether it ever wants damage types at all, a genuinely bigger design question
  this section does not resolve (see §23.4) -- named here as the one FFXI category that doesn't
  translate without a prerequisite decision nobody's made yet, not silently dropped.

### 23.3 Roster expansion, following the existing per-slot/per-category shape

Not a full item-by-item list (same "representative, not exhaustive" discipline
`FFXI_ITEM_PARITY_SEED.md` itself already uses, and the same restraint §22's own roster section
took) -- a real expansion pass should roughly double-to-triple the current 27 against the SAME 11
slots, using the seed doc's own already-curated real-name lists (`docs/FFXI_ITEM_PARITY_SEED.md`
§3/§4/§6) as the literal source list to pull additional verbatim names from, consistent with
§23.1's "this choice is already made" framing -- e.g., real weapon-skill-category names not yet
represented at all in the live 12 SPECIFIC weapons (Hand-to-Hand: Life Knuckles; Katana: Kikoku;
Archery: Composite Bow; Marksmanship: Firebird Musketoon), and the seed doc's own real armor lists
for slots that currently have only one GENERIC item each (Head: Chivalrous Chain, Genin Kabuto
alongside the existing Optical Hat; Earrings/Rings: Bat Earring, Toreador's Ring, Sniper's Ring
alongside Peace Earring/Astral Ring). Each new item should be assigned to ONE of §23.2's categories
on purpose (not just a new stat-line permutation of the existing six flat fields), so the roster
actually grows in *kind*, not just count -- the founder's own explicit ask.

### 23.4 Open questions, not resolved here

- **RNG.** Proc effects (§23.2) are gated on this engine gaining a real random-number source at
  all -- §17.2 already named this gap for auto-attack parity reasons; this section independently
  arrives at the same prerequisite from the item side. Whoever builds either should probably build
  the RNG primitive once, shared.
- **Damage types.** Elemental resistance items have no meaning until REDGARDEN decides whether
  magic/physical (or full elemental) damage typing is ever wanted at all -- a real, separate design
  question, not decided here, and probably out of scope unless a real need for it shows up
  elsewhere first (e.g., a caster hero whose kit wants "true damage" or "magic damage" as a
  distinct category).
- **Enmity/threat.** A real aggro-priority system for creeps/towers doesn't exist yet at all --
  needed before any enmity-flavored item means anything mechanically.
- **Generalizing Donkey's latent-effect shape** into a reusable `ArenaItemDef` field (§23.2) versus
  leaving it as Donkey's own bespoke mechanic and hand-building each new latent item the same
  one-off way Blink Dagger/Donkey themselves were built -- both are legitimate, not decided here.
- **Exact stats/costs/counts** for any new item -- same "spec the model, not the numbers"
  discipline every other spec-only section in this file already applies.
- **The IP question named in §23.1** is explicitly not re-litigated or resolved here -- flagged as
  a live, informed, standing decision, not reopened.

This section's job -- name the real ceiling on "more variety" (six flat stats, two bespoke
mechanics), translate real FFXI mechanic archetypes into concrete new `ArenaItemDef`-shaped
categories with honest prerequisites named per category, and put the already-made "verbatim real
names" choice on the record rather than let it stay an implicit, unexamined precedent -- is done.
No code changes accompany this section; implementation is separate, future work.

## 24. Full unit control affordances — Warcraft 3 northstar (2026-07-31) -- Milestones 0-2 done (spec, The Cart, and the full real WC3 group-order vocabulary for Tyler's clones); Milestone 3 (unit production/resource loop) open, not decided

Founder, real-time: "redgarden full unit control affordances northstar warcraft 3." Same
discipline §20/§22/§23 already applied: name the current shape and its real gap before proposing
anything, not invent a new system on top of an assumed-clean slate.

### 24.1 Current shape (grounded in the actual code)

REDGARDEN today is DOTA-shaped, not WC3-shaped: every hero is **owner-piloted** — one input
stream (or one bot brain) maps to exactly one fully-controlled `ArenaHero` slot
(`arena_game.h`'s own `ArenaHero.owner` field). Lane creeps (`ArenaLaneCreep`, §20) are
autonomous team-aggro AI, never player-commanded — real MOBA convention, and the literal opposite
of WC3's own "you lead an army" feel. §16.1 already named the real, general blocker precisely
while scoping Donkey: *no non-piloted/companion-unit system exists* — and that section's own
"status update" is honest that the blocker was **sidestepped, not solved** (Donkey shipped as an
equippable item instead, specifically to avoid building a companion-slot concept with no second
consumer yet).

The one real, already-shipped exception is Tyler's own clone system (`is_clone`/`clone_owner` on
`ArenaHero`, `apps/arena/src/main.c`'s drag-select: `selected_units[]`/`selected_unit_count`,
`ARENA_MAX_SELECTED_UNITS`, `left_drag_active`, real box-select-and-command-group UX) — founder's
own words when it shipped: *"clones multi control drag click all of it."* This is the actual,
closest real precedent for what "full unit control" means: multiple simultaneously-selectable,
directly-commandable units under one player's ownership, box-select and group-move/attack already
real and tested. It is currently **Tyler-only, hardcoded**: `ARENA_MAX_SELECTED_UNITS` is sized
to `1 + ARENA_TYLER_R_CLONE_COUNT` specifically, `is_clone`/`clone_owner` are set only by Tyler's
own R cast, and every other hero's `selected_unit_count` stays permanently 0 (which
`selected_or_self()` resolves to "just me," so nothing broke for the other 28 heroes when this
landed — it was additive, not a general system yet).

### 24.2 What "Warcraft 3" actually names, concretely

Not asking "what does WC3 do" in the abstract — grounding the comparison in what real WC3 heroes
have that REDGARDEN's don't:
- **A hero leads units it didn't have to be born with** (WC3 heroes summon/train/command
  creatures and structures over a match, not a fixed clone count from one R cast).
- **Units survive independent of a single cast's own lifetime** — Tyler's clones are bound to
  R's own duration/cooldown cycle; WC3 summons/creeps persist and are re-ordered turn to turn.
- **Group orders, not just group selection** — real WC3 vocabulary is attack-move, hold position,
  patrol, stop, not just "move this whole group to X" (which is as far as Tyler's own drag-select
  UX goes today, per §24.1).
- **Some real resource/production loop** gates how many units you can field (WC3: gold/lumber,
  food cap) — REDGARDEN has Flow (§19) but nothing in the item/ability system currently spends it
  on *producing units* rather than buying stats.

### 24.3 A real, buildable path — generalize what already shipped, don't invent from zero

Same "port real, don't invent" discipline this whole session has held everywhere else (REDGARDEN
↔ DragonsNShit crossovers included, e.g. this session's own DragonsNShit SMN-Avatars work pulling
Zagan/Beleth/Vassago from this exact roster the other direction). The tractable path is
generalizing Tyler's own already-tested mechanism, not building a second, parallel one:

| # | Milestone | What it actually requires | Status |
|---|---|---|---|
| 0 | This northstar | Written, current shape + real gap named, registered | DONE |
| 1 | Generalize `is_clone`/`clone_owner`/`selected_units` off Tyler-only | See §24.3.1 CORRECTION — closer to already-done than this row originally claimed | **DONE (found, not built)** 2026-07-31 |
| 2 | Real group-order vocabulary for Tyler's own clones | Attack-move, hold, patrol, stop for Tyler's drag-selected clone group — the actual WC3 command surface §24.2 names as currently missing, not just group-move. Corrected 2026-07-31 (§24.3.2, founder: "the unit controls are supposed to be for tyler") from this row's original framing ("a second hero gets real units") — full unit control deepens Tyler's own mechanic, doesn't spread to a new hero. | **DONE** 2026-07-31, all four commands shipped same day — Stop (`10faf25`, `S` keybind), Attack-move (`PACKET_ARENA_ATTACK_MOVE`, held-`A`-then-click, opportunistic engage + destination-resume, also closed §17.4's own long-open checklist item), Hold Position (`PACKET_ARENA_HOLD`, `D` keybind — real WC3 "H" was already taken by ability-help — never chases, still defends via the same opportunistic-engage scan attack-move uses), Patrol (`PACKET_ARENA_PATROL`, `P` keybind, walks A↔B forever, same opportunistic engagement, arrival-triggered direction flip). All four share one `arena_owner_controls`-authorized command surface and the "a new command always wins" convention (each clears the other three). `arena_find_opportunistic_target` factored out once patrol needed the same scan attack-move/hold already had. 13 new tests across the four commands, full `scripts/test_arena.sh` suite + `scripts/test_10_bots.sh` stability green at every step. |
| — | ~~First non-Tyler hero with real controllable units~~ | Superseded by §24.3.2's correction — not this section's real goal. The Cart (built same session) is separate, real, lore-faithful Indirect-Control content, not this milestone. | SUPERSEDED |
| 3 | (Separate, bigger, explicitly not decided here) Unit production/resource loop | Spending Flow to field units over time rather than a fixed cast-bound count — a real pivot toward WC3's own RTS-economy half, named as a distinct, much larger decision surfaced for the founder, not assumed. | OPEN QUESTION |

#### 24.3.1 CORRECTION (2026-07-31, same day, found while starting Milestone 1)

Milestone 1 as originally written assumed the drag-select/ownership mechanism was Tyler-coupled
at the code level and needed a rename/refactor pass to generalize. Checked directly instead of
assumed: it isn't. Every real gate in both `arena_game.c` (`arena_owner_controls`,
`tyler_clone_cascade_kill`, the hittable/targeting checks) and the client
(`apps/arena/src/main.c`'s drag-select, selection-ownership, and rendering code) already branches
on `is_clone`/`clone_owner` alone — there is no `if (hero_id == ARENA_HERO_TYLER)` gate anywhere
in that path. The mechanism was additive and hero-agnostic from the moment it shipped; only the
*trigger* (`tyler_spawn_clones`, called exclusively from Tyler's own R) and one sizing constant
(`ARENA_MAX_SELECTED_UNITS`, currently `1 + ARENA_TYLER_R_CLONE_COUNT`) are Tyler-specific, and
both of those depend on a second hero's *real* kit numbers to generalize correctly — which don't
exist without picking that hero first. Generalizing them now, ungrounded, would mean inventing
numbers with no real kit behind them, the exact thing this file's own discipline exists to avoid
(§23's "spec the model, not the numbers," applied here to a constant instead of an item stat).
**Net effect: Milestone 1 collapses into Milestone 2** — there is no standalone infrastructure
work left to do before a real second hero is picked; the moment one is, sizing
`ARENA_MAX_SELECTED_UNITS` and writing that hero's own `_spawn_units`-shaped function is normal
kit-building work, not a separate generalization pass. Also corrects this section's own
mis-citation of `HERO_CONTENT_FRAMEWORK.md` as this roster's content pipeline — that file is
GoblinFoxDragon's, for DragonsNShit's separate multiverse-lore-hero system; this roster's real
pipeline is §7's own table, sourced from `TYLER/multiverse_heroes.md` directly.

#### 24.3.2 CORRECTION (2026-07-31, same day, founder direction while The Cart was mid-build)

Founder, real-time: **"the unit controls are supposed to be for tyler."** This section's original
Milestone 2 framing ("first NON-Tyler hero with real controllable units") had it backwards — the
founder's actual intent for §24's own "full unit control affordances" is deepening Tyler's own
already-shipped clone/drag-select mechanic, not spreading direct unit command to a second hero.
The Cart, built the same session under this same section, was **never actually Milestone 2's
"direct control" goal in the first place** — §24.3.1 already flagged Cart's kit as
Indirect-Control (a delivery zone, not a directly-commanded unit), a real, separate archetype
that stays true to the character's own lore. So the Cart's completion doesn't close Milestone 2
as originally scoped; it's real, shipped, lore-faithful content on an adjacent but different
axis. **Milestone 2, corrected: real WC3-shaped group-order vocabulary — attack-move, hold,
patrol, stop — for Tyler's own existing clone selection**, not a second hero's kit. Old Milestone
3 (group-order vocabulary) and corrected Milestone 2 are now the same item; renumbered below.

### 24.4 What this section deliberately does not decide

- **Milestone 3's own scope** — whether REDGARDEN grows a real unit-production economy at all is
  a product-direction call bigger than this doc, named not resolved, same treatment §23.1's IP
  question got.
- **Numbers** (unit counts, costs, cooldowns) for any of it — same "spec the model, not the
  numbers" discipline every other spec-only section in this file already applies.

This section's job — name the real current shape (owner-piloted heroes, autonomous lane creeps,
one real but Tyler-only multi-unit precedent), name §16.1's own already-honest "sidestepped, not
solved" companion-unit gap as still-open and directly relevant, and lay out a path that
generalizes what's real rather than inventing a second parallel system — is done. No code changes
accompany this section; implementation is separate, future work.

## 25. Multi-agent RL: team environment, role discovery, noisy gestalt, synergy decay (2026-08-10) -- VS0 (team env), noisy-gestalt alternating schedule, and synergy decay all built; role discovery/autocurriculum spec only

Founder, real-time, sourced from a long personal research conversation (Gemini transcript,
ingested into `CarePyre/source/gemini-transcript-2026-08-09.md` for an unrelated reason — that
repo's own real business plan starts around that file's line 5024; everything before it is a
sprawling ML/RL brainstorm that drifted into ungrounded territory by the end — DOD-grant framing,
invented acronyms, "reality hack" jargon applied to writing letters to senators). Most of that
material is explicitly NOT being adopted here — see this section's own §25.0 for exactly what was
kept and what was dropped, and why. What *is* real and worth building: a cluster of genuine,
named multi-agent RL research directions (role discovery, diversity-preserving team training,
autocurriculum), applied to a gap §21.3 already flagged explicitly as real and unsolved:
**"Team-mode (10v10) training... multi-agent RL (coordinating a full team, objective-aware) is a
substantially harder problem than single-agent PPO against a fixed opponent, still out of
scope."** This section is that scope, picked back up.

### 25.0 What came from the source conversation, and what didn't

The founder's own framing used invented vocabulary ("noisy gestalt," "frame break," acronym
chains like ROSANNE/POINT/MIDASX) mixed with real, correctly-used ML concepts. This section
translates the real concepts into their actual names and drops everything else:

| Founder's term | What it actually maps to | Kept? |
|---|---|---|
| "noisy gestalt hive mind... unique strategies instead of general ones" | Diversity-preserving MARL — prevent policy homogenization across agents (population-based training with a diversity/novelty bonus, or a shared-vs-individual policy split with an information bottleneck) | **Kept** — §25.2 |
| "dynamic persona vectors... determined by game features, not human-defined words" | Learned (not hand-labeled) per-agent embeddings that condition behavior — standard in role-based MARL (QMIX-style role clustering, RODE, ROMA) | **Kept** — §25.2, folded into role discovery |
| "flip back and forth between unique and optimal... MTG archetypes" | Phased/alternating training curriculum: alternate diversity-bonus and pure-win-rate optimization phases | **Kept** — §25.2.3, as a training schedule, not a new theory |
| "peanut butter and jelly 2 bots, recursive combination" | Policy distillation / crossover between checkpoints | **Not kept** — real technique (policy distillation exists), but no concrete REDGARDEN use case yet; noted, not spec'd |
| "dynamically adjust cross-attention... comeback mechanic when a team is winning too good" | A real, buildable game-design mechanic: team coordination bonus decays under a win-streak condition | **Kept** — §25.3 (renamed "synergy decay," not attention-mechanism-literal) |
| "auto-curriculum engine... UED to evolve the game as you play it" | Unsupervised Environment Design (UED/PAIRED/POET-family): the opponent/environment curriculum adapts to the current policy's weaknesses instead of being fixed | **Kept**, scoped to opponent-curriculum only (not "the game evolves itself," which is a content-design claim this section doesn't make) — §25.4 |
| "contrastive state encoders" | Real technique (contrastive representation learning for RL state encoders) — real but no concrete gap it fills here yet | **Not kept** — noted for later if a real use case shows up |
| Physics-informed neural networks, "reality hacking" physics, ROSANNE/POINT/MIDASX/AGI synthesis, DOD grants, resilient autonomous swarms as a funding pitch | Not ML — either unrelated to a game bot AI system, physically ungrounded, or a funding/military framing with no product behind it | **Dropped entirely** |

Everything below is scoped to REDGARDEN's own arena bot AI, building directly on §21's real,
already-running pipeline — not a new system, not AGI, not a swarm-weapons pitch.

### 25.1 What exists today (verified by reading the actual code, not assumed)

- `packages/simulation/arena_game.h` already has real team-mode primitives the RL pipeline has
  never used: `ARENA_TEAM_SIZE` (10), `ARENA_MAX_HEROES` (`ARENA_TEAM_SIZE * 2` = 20),
  `arena_init_teams(void)`. The real networked arena (`apps/arena_bot`'s 19 real bots) already
  plays full team matches through this — team-mode is NOT a gap in the game simulation, only in
  the RL training environment, exactly as §21.3 said.
- `apps/arena_training/src/headless.c` (S170-224) is 1v1 only: `sim_init(hero0_id, hero1_id)` →
  `arena_init_with_heroes`, a fixed `ARENA_TRAINING_OBS_SIZE` (18 + 2×`ARENA_HERO_COUNT`) flat
  float observation for exactly one agent vs. one fixed heuristic-driven opponent
  (`arena_bot_tick_heuristic`/`bot_cast_kit_if_ready`, deliberately never the policy currently
  being trained — see that file's own doc comment on why a moving-target opponent would be
  circular).
- `scripts/rl_env.py`'s `ArenaTrainingEnv` wraps that 1v1 C API as a single-agent
  `gymnasium.Env`; `scripts/rl_train.py` trains it with Stable-Baselines3 PPO;
  `scripts/export_rl_policy_to_c.py` extracts the trained MLP into
  `packages/common/rl_policy_weights.h`, consumed by `rl_policy_forward()` (real, wired into
  `apps/arena_bot`'s live match bots as an additive movement nudge since S170-228/Apple #11301).
- None of gymnasium/stable-baselines3/PyTorch have ever been run end-to-end from a Claude Code
  session in this environment except once (§21's own status update, a 4000-timestep smoke run,
  founder explicitly requested it that one time) — no venv module, no sudo, externally-managed
  system Python. This section's own VS0 work (below) was written and locally unit-tested at the
  C/ctypes boundary the same way §21.2 verified its own environment logic — real, but not a real
  multi-day training run, which needs a normal Python environment (Colab, same pattern §21 and
  the GPT-2 pipeline both already established) to actually produce a trained policy.

### 25.2 Role discovery + noisy gestalt: diversity-preserving team training

**The problem this solves**: naive multi-agent PPO with fully shared parameters converges every
agent on the team toward the same policy (the literal "hive mind" the founder's own source
material was reacting against) — real, documented MARL failure mode, not a hypothetical. The fix
is not a new invention; it's picking one of the field's standard answers and applying it here.

**25.2.1 Team environment (VS0 — built this session, see §25.5 for what "built" means here).**
`headless.c` gains a second API surface, additive to the existing 1v1 one (nothing about §21's
existing 1v1 pipeline changes):
- `void sim_init_team(int team_size, const int *hero_ids_a, const int *hero_ids_b)` →
  `arena_init_teams()`, then places `team_size` heroes per side (capped at `ARENA_TEAM_SIZE`, so
  this scales from 2v2 up to the real 10v10 without a second code path — start small for the
  first real training runs, same "spec the model, not the number" discipline every other section
  in this file already applies to hyperparameters).
- `void sim_step_team(const float *actions, int n_agents)` → applies one action per
  team-A agent (the agents actually being trained) via the same
  `arena_set_move_target`/`arena_cast_*` calls §21.2 already established, then a single
  `arena_update(dt_ms)` tick advances everyone (both teams) at once. Team B is driven by the same
  stable `arena_bot_tick_heuristic` §21's 1v1 opponent already uses — a full team of the existing
  heuristic AI is the fixed, non-circular opponent for team-A's new multi-agent policy, exact
  same non-circularity reasoning §21.2 already used for the 1v1 case, just applied per-opponent
  instead of once.
- `void sim_get_obs_team(float *out_obs, int n_agents)` → per-agent observation, each agent's own
  slice matching §21's existing single-agent 18+2×`ARENA_HERO_COUNT` layout (self state, nearest
  foe, hero-id one-hots) **plus** a new teammate block: nearest-N-teammates' `(hp_frac, dx, dz,
  alive)`, N fixed at team_size-1 for now (small teams only in the first pass — a 10v10
  observation with 9 teammates is a valid but much later target). This is what makes it
  "multi-agent" in the observation sense: every agent can see its own team's state, not just
  itself and one foe.

**25.2.2 Role discovery (2026-08-10 -- identity-conditioning PREREQUISITE implemented, the full
learned-embedding technique still spec only).** Rather than hand-assigning roles (support/carry/
tank — REDGARDEN's own hero kits already imply this informally), let roles emerge from training
the way QMIX-family role-discovery methods do (ROMA/RODE are the two most directly applicable
named techniques): each agent's policy is conditioned on a learned per-agent embedding vector,
trained jointly with the policy itself rather than fixed per hero. Agents with similar embeddings
behave similarly; the training process — not a hand-written rule — decides how many distinct
roles emerge and which agents end up in which. This directly answers the founder's own "dynamic
persona vectors... determined by game features, not human-defined words."

Real minimal mechanism these methods all depend on: the policy needs SOME signal for "which of
the team_size shared-parameter copies am I" before it can possibly differentiate behavior by
slot — without it, differentiating is mathematically impossible regardless of whether it would
score higher, the exact "hive mind" collapse this section's own problem statement names.
`sim_get_obs_team` (`apps/arena_training/src/headless.c`) now appends a `team_size`-long agent-
identity one-hot to each agent's own observation. This is the prerequisite a dedicated ROMA/RODE-
style learned-embedding module would consume, landed honestly short of the full technique — the
embedding module itself (and the training-time machinery to actually cluster agents by learned
similarity) remains unbuilt. Verified: `rl_env_team.py --smoke-test` confirms the new obs_size
(86→89 at team_size=3) and correct one-hot values, plus a real SB3 VecEnv rollout completes
without error. REDGARDEN commit `6d6e853`.

**25.2.3 Noisy gestalt: diversity-preserving training schedule (2026-08-10 -- alternating variant
IMPLEMENTED, `--noisy-gestalt` in `scripts/rl_train_team.py`/`rl_env_team.py`).** Founder:
"ensure we are doing some of the new exotic training." Chose **alternating** (the "flip back and
forth" framing this section already names as the actual content of that quote, not the blended
fallback): `ArenaTeamVecEnv` alternates Johnny phase (a synergy reward -- proximity to a living
teammate, read straight off `sim_get_obs_team`'s existing teammate block, no new C-side plumbing
needed) and Spike phase (synergy reward off, plain baseline reward) every `--gestalt-phase-ticks`
env ticks. A grounded, simpler stand-in for the diversity-bonus formulation above (novelty/
diversity-from-teammates' current policy is a harder, model-internals-dependent signal SB3's
stock PPO doesn't expose per-tick) -- proximity-based team coordination, not full behavioral
diversity, but a real first step in the same alternating-schedule spirit, immediately runnable.
Verified: ctypes smoke test passes, a short real PPO run confirms phases alternate correctly by
tick count; a real 500k-timestep training run with this enabled was launched the same session
(`rl_team_checkpoints/`, gitignored -- generated artifact, not committed). To stop full
parameter sharing from collapsing every agent into the same policy, alternate two training
phases, matching the founder's own "flip back and forth between unique and optimal" framing —
this is a real, named pattern (population-based training with a diversity bonus, alternated
against pure reward optimization), not a new theory:
- **Diversity phase**: add a per-agent novelty/diversity bonus to the reward (e.g., an
  intrinsic reward for behaving differently from teammates' current policies, standard
  diversity-is-all-you-need-style regularization), so agents differentiate.
- **Optimization phase**: drop the diversity bonus, train purely on §21.2's existing win/loss +
  shaping reward, so differentiated agents still get pulled toward being individually strong, not
  just different for its own sake.
Alternating (not blending) the two objectives is the actual content of "flip back and forth" —
blending them into one weighted-sum reward is the more common approach in the literature and is
the fallback if alternating proves unstable in practice; both are legitimate, this section
doesn't pre-commit to one.

### 25.3 Synergy decay: a comeback mechanic, not a training technique

Separate from the training system above — this is a **live-match game-design mechanic**, real
and shippable independent of whether role discovery/noisy gestalt training ever gets built. A
team's effective coordination bonus (whatever numeric bonus a future team-aware reward or
in-match buff represents "playing well together") decays when that team is significantly ahead
(gold/kills threshold, numbers TBD — same "spec the model, not the numbers" discipline), with a
random per-tick chance of the decay applying rather than a deterministic trigger (founder,
explicit: "there needs to be a random chance of synergy decay at different levels... not always
happen"). This is a rubber-band comeback mechanic in the same design family as Mario Kart items or
League's own catch-up XP/gold — named honestly as that, not as a training-time RL concept, even
though the founder's own source material described it in RL-attention-mechanism language.

**IMPLEMENTED (2026-08-10, `packages/simulation/arena_game.c`/`.h`).** `arena_state.synergy_tier
[2]` (0=full cohesion .. `ARENA_SYNERGY_TIER_COUNT`-1=fully decayed) re-rolls every
`ARENA_SYNERGY_ROLL_INTERVAL_MS` (8s), weighted toward higher tiers the further ahead that team
is in the real resource race — the coordination bonus itself reuses East/Music's own attack-
speed/move-speed shape (`ARENA_SYNERGY_TIER0_CDR_PCT`/`MOVE_SPEED_PCT`), decaying linearly to 0
at the weakest tier. Source design: the CarePyre transcript's own `StochasticSynergyController`,
base_probs `[0.60, 0.25, 0.10, 0.05]` ported faithfully — but one real bug in the source's own
formula was found and NOT reproduced: adding the same constant to every tier's logit before
softmax is a mathematical no-op (softmax is shift-invariant), so this implementation scales the
lead-driven shift BY tier index instead, which actually produces the intended effect. Two more
real bugs caught during implementation (both fixed): the field's memset default (0) meant
"strongest tier" unlike every other buff field's own "0 = off" convention, which would have
silently granted every team-mode match the full bonus from the opening whistle and broke several
pre-existing exact-cooldown-value tests elsewhere in this file; and a team-mode detection guard
that was itself broken by this suite's own common "deactivate every other hero for isolation"
test pattern (fixed in the tests, not the guard — the guard's real-gameplay invariant holds).
5 new smoke tests, full `test_arena.sh` + `test_10_bots.sh` green. REDGARDEN commit `cc7f560`.

### 25.4 Autocurriculum: opponent curriculum instead of a fixed heuristic-only opponent

**Critical pre-existing bug found and fixed while scoping this (2026-08-10).** `sim_step_team`
(`apps/arena_training/src/headless.c`, written earlier the same day this section first landed)
was calling `arena_update(dt_ms)` — the **1v1-only** simulation tick, which hardcodes
`heroes[0]`/`heroes[1]` as the only two heroes it ever calls `update_hero_motion`/
`resolve_combat`/`tick_hero_kit` on. Every other hero in a team match (`team_size`-1 more per
side) never moved, fought, or got ticked at all for the entire time this training pipeline has
existed — confirmed directly (gave team-A slot 2 a real move command, position didn't change).
This affected every team-mode training run since `sim_step_team` was written, including a real,
multi-hour, in-progress background PPO run — founder, real-time: "ok if there is an issue fix
it, if you must cancel current training and restart after thats ok." Fixed: both `sim_step_team`
and the new `sim_step_team_vs_actions` (below) now call `arena_update_teams(dt_ms)`, the real
team-mode tick. Verified via `--smoke-test`: a real episode now actually ends (winner decided at
a real tick count, meaningfully varied per-agent rewards) instead of the frozen-statue degenerate
result the bug produced. All prior checkpoints (`rl_team_checkpoints/`, gitignored) were deleted
as invalid — trained entirely on the broken environment — and the background training run was
killed and restarted fresh with the fix in place. REDGARDEN commit `e6effc1`.

**Autocurriculum prerequisite implemented the same pass.** `sim_step_team_vs_actions` and
`sim_get_obs_team_any` (generalizes `sim_get_obs_team` to either team, needed so an externally-
driven team B gets its own real perspective, not team A's) together close the real gap this
section's own concrete design needs: previously team B was driven ENTIRELY inside the C sim via
a fixed heuristic, with no mechanism at all for Python to inject externally-computed team-B
actions (a loaded self-play checkpoint's own predictions). `actions_b=NULL` falls back to the
exact same heuristic byte-for-byte, so every existing caller is unaffected. Verified with 4
targeted ctypes checks (backward compatibility, correct team-B perspective, NULL fallback
matches exactly, real external actions genuinely drive team B). The Python-side opponent-pool
sampling logic itself (maintain past checkpoints + the heuristic, bias sampling toward whichever
the current policy loses to most) is NOT built yet — this lands the C-level capability it needs.

**Python-side opponent-pool sampler now implemented (2026-08-10, same day).** `ArenaTeamVecEnv`
(`scripts/rl_env_team.py`, `autocurriculum=True`) maintains `opponent_pool` (permanent
"heuristic" slot + up to `MAX_CHECKPOINT_OPPONENTS=5` past self-play checkpoints, oldest evicted
first), samples one opponent per episode via Prioritized Fictitious Self-Play (AlphaStar's own
`(1 - win_rate)^2` weighting, Beta(1,1)-smoothed so untested checkpoints still get sampled, a
`PFSP_MIN_WEIGHT` floor so a fully-solved opponent never drops out of rotation), and — when a
checkpoint opponent is sampled — drives team B via `sim_get_obs_team_any`(own perspective) +
that checkpoint's loaded SB3 policy + `sim_step_team_vs_actions`, instead of the fixed heuristic.
`rl_train_team.py --autocurriculum` feeds each new checkpoint into the running pool as it saves.
Verified via a standalone ctypes+SB3 script against 2 real checkpoints from this session's own
in-progress training run (opponent sampling/switching across episodes, real model.predict()-driven
team-B actions, pool eviction at the cap) — NOT yet exercised inside a full end-to-end
`model.learn()` training run to see whether it actually measurably improves the resulting policy
over the fixed-heuristic baseline; that's the real remaining open question, not the plumbing.

Named directly per the founder's ask ("autocurriculum engine"). Real, established field:
Unsupervised Environment Design (UED) and its named instances PAIRED, POET, and (closer to what's
useful here) prioritized-replay-style autocurricula that pick training opponents/scenarios biased
toward ones the current policy is currently weak against, instead of a fixed opponent for the
whole run. Scoped narrowly and honestly:
- **What this means concretely here**: instead of training-team-B always being
  `arena_bot_tick_heuristic` at fixed strength, maintain a small population of past checkpoints
  of the policy being trained (self-play, which §21.3 already named as valuable-later-depth) plus
  the heuristic AI, and sample the next episode's opponent biased toward whichever one the
  current policy has been losing to most — a real, standard autocurriculum, not "the game
  evolves new mechanics as you play," which is a much larger, separate, unscoped claim this
  section does not make.
- **Explicitly not claimed**: procedural generation of new game content/mechanics driven by
  training (the source conversation's "Mario Party mini-games as weight adjustments" idea) — that
  conflates curriculum-over-opponents with curriculum-over-game-design; the former is this
  section's real scope, the latter is not spec'd here at all.

### 25.4.1 League training (AlphaStar-style) — real fix for cyclic dominance (2026-09-12)

Founder real-time, citing a real, named source: a YouTube explainer of DeepMind's own AlphaStar
league (timestamps 13:01-13:55). §25.4's own existing autocurriculum above is plain PFSP against
ONE policy's own growing checkpoint pool — exactly the "simple self-play" failure mode the video
names: training against your current self makes you better against that one opponent while
quietly getting worse against others, a real rock-paper-scissors dynamic (**cyclic dominance**).

**The real fix, per the source**: a league of three distinct agent roles, trained simultaneously,
each contributing PERMANENT (never evicted) checkpoints back into one shared pool:
- **MAIN** — trains against the whole league (PFSP-weighted, biased toward whatever it currently
  loses to most). Pushes the skill frontier forward; the same real objective §25.4's single
  policy already has, now sampling from a shared multi-lineage pool instead of only its own
  history.
- **MAIN_EXPLOITER** — has ONE job: find and break Main's CURRENT weaknesses. Challenges Main's
  freshest checkpoint directly by default; if it can't win consistently (no learning signal),
  "climbs down" to Main's own historical checkpoints instead, biased toward ones it can ALREADY
  beat, to build up real skill before re-challenging. Periodically resets to a freshly
  initialized network so it can't converge onto one narrow trick forever.
- **LEAGUE_EXPLOITER** — same whole-league PFSP sampling as Main, but a SEPARATE lineage — its
  whole purpose is finding weaknesses that persist across the ENTIRE league, so the group never
  holds a permanent blind spot.

**Shipped 2026-09-12**: new `scripts/rl_league.py` — `LeagueRole` enum, the shared PFSP weighting
function (`pfsp_weight`/`pfsp_sample`, now the one canonical implementation §25.4's own
`ArenaTeamVecEnv._sample_opponent` still keeps its own pre-existing copy of, unchanged, for the
plain single-pool path), `LeagueManager` (a real, permanent, append-only, cross-process JSON-file
registry — no locking needed: each registration is a brand-new file, atomically renamed into
place, never an overwrite of another process's), and the three roles' real sampling functions
(`sample_for_main`/`sample_for_main_exploiter`/`sample_for_league_exploiter`) plus Main
Exploiter's own struggle-detection (`is_struggling_vs_main`) and reset cadence
(`should_reset_main_exploiter`). 29 real unit tests (`scripts/test_rl_league.py`, pure Python, no
gymnasium/SB3/compiled-.so dependency) — caught and fixed one real bug live (`str(LeagueRole.MAIN)`
returns `"LeagueRole.MAIN"`, not `"main"` — `Enum.__str__` wins over the `str` mixin despite
`class LeagueRole(str, enum.Enum)` — fixed via a real `_role_str()` normalizer, not silently
worked around at every call site).

Wired into `scripts/rl_env_team.py`'s `ArenaTeamVecEnv` (new `league_manager`/`league_role`
constructor params, fully additive — `league_manager=None`, the default, is byte-for-byte the
pre-existing §25.4 behavior, zero regression, verified via the existing `--smoke-test` still
producing an identical real episode) and `scripts/rl_train_team.py` (new `--league`/
`--league-role`/`--league-dir`/`--league-reset-every-n-generations` flags; a real, found-and-fixed
bug caught during this same pass: naively re-reading `model.num_timesteps` as the loop's own
running total silently breaks the moment Main Exploiter's own periodic reset zeroes a fresh
model's counter — fixed by tracking the delta from before each `.learn()` call instead of the raw
value, so the outer loop's total-timesteps accounting survives a mid-run reset correctly). New
`scripts/run_league.sh` launches all three roles as separate concurrent processes against one
shared `--league-dir`, matching `run_bot_pool.sh`'s own orchestration conventions.

**Real, honest, necessary scope note on "current Main"**: each role runs as a SEPARATE process —
there is no single shared in-memory model. "Current Main," from Main Exploiter's own perspective,
means Main's most-recently-REGISTERED checkpoint on disk (re-read from the shared registry), not
the exact in-memory weights of a live-training process at that instant. This is the real, honest
tradeoff of a checkpoint-based multi-process league (not a hack — any file-based league without
extra IPC works this way), named directly rather than smoothed over.

**Real, honest, this module's own interpretation, not a verbatim reproduction**: the source video
describes Main Exploiter's "climb down" BEHAVIOR, not an exact formula. This is implemented here
as the same PFSP weighting formula with the bias direction inverted (`favor_hard=False`: weight ∝
win_rate^p instead of (1-win_rate)^p) — biasing toward historical Main checkpoints it can ALREADY
beat rather than the hardest ones. A real, defensible, tested choice; not claimed to be
DeepMind's own exact published algorithm.

**NOT yet run end-to-end** (needs three real, long, concurrent training processes and real
GPU/CPU time — a real cost/founder-scheduling decision, matching this file's own established
posture for every prior autocurriculum/noisy-gestalt run, not attempted speculatively in the same
pass that built the mechanism): whether the real three-role league measurably beats plain
`--autocurriculum` at reducing cyclic dominance is the actual open question, same honesty
convention §25.4/§25.5 already established for those features. `ArenaTeamVecEnv`'s own new league
wiring also could not be exercised via a live SB3 rollout in this specific pass — gymnasium/
stable-baselines3 are not installed in this sandbox as of 2026-09-12 (they WERE installed as of
2026-08-10 per §25.5's own note; whoever runs this next should check current environment state
before assuming either way). The pure `rl_league.py` logic is fully unit-tested regardless of
that; the `ArenaTeamVecEnv`/`rl_train_team.py` integration layer was reviewed directly (a real bug
already found and fixed in it, per the timesteps-counter note above) but not run.

### 25.5 What "built this session" actually means, honestly

Updated 2026-08-10 (same day, later pass): `sim_init_team`/`sim_step_team`/`sim_get_obs_team`
(§25.2.1), noisy gestalt's alternating Johnny/Spike schedule (§25.2.2), and the autocurriculum
opponent sampler (§25.4) are now real, running code — gymnasium/stable-baselines3 turned out to
already be installed in this environment, closing the "needs Colab" gap this note originally
flagged. Role discovery's full learned-embedding module (ROMA/RODE-style, beyond the identity
one-hot §25.2's `sim_get_obs_team` already writes) and synergy decay's RL-training-loop tuning
(beyond the live C-level gameplay mechanic §25.3 already ships and tests) remain specified, not
implemented — same honesty convention §21's own "Status update" and §24.4 already use in this
file. Neither noisy-gestalt nor autocurriculum has completed a full end-to-end training run yet
(both are mid-training or ctypes-verified only as of this note) — the plumbing is real, whether
either one measurably improves the resulting policy is still an open, unanswered question.

**Updated 2026-08-11: the noisy-gestalt run finished (Apple #12985) and its checkpoint is now a
real live consumer, closing the exact gap this note's own §25.5 flagged ("what consumes a
team-shaped input vector? NORTHSTAR §25.5 doesn't resolve this").** 500K timesteps, ~5.5h, zero
errors, Johnny/Spike alternation confirmed firing live in the training log, final eval 20W/0L/0D
(100%) vs. the fixed heuristic team — founder, real-time, on why that number isn't dismissed as
noise: "if they are exploiting heuristics they are exploiting the current meta which is the
point." `apps/arena_bot/src/main.c` gained `team_rl_engage_nudge()` (own doc comment has the full
design), the live analog of `rl_engage_nudge`'s existing 1v1 wiring — reconstructs
`sim_get_obs_team_any`'s exact observation layout from wire data (`BotSnapshotView`) instead of
training-only sim state, additive on top of the existing angle-spread/flocking movement, same
"nudge, not replacement" philosophy as the 1v1 version.

**Real, hard constraint found while wiring this up, not glossed over**: this checkpoint was
trained at `--team-size 3`, a fixed shape baked into the exported header
(`packages/common/rl_policy_weights_team.h`) at export time — it cannot correctly drive the live
10v10 R&D pool (`:7778`, `ARENA_TEAM_SIZE`), a real dimension mismatch, not a rounding gap.
`team_rl_engage_nudge` hard-gates on `world.count/2 == 3` and is a guaranteed no-op in the 10v10
pool for exactly that reason. Real, separate 3v3 queue stood up instead
(`ops/systemd/redgarden-matchmaker-bots-3v3.service` + `redgarden-bot-pool-3v3.service`, port
`:7780`, 5 bots + 1 open human slot so the founder can queue in directly) — deployed live,
verified via a real 6-bot test match (draft → live play → no crashes) before switching to the
5-bot founder-facing config. A `team_size=10`-trained checkpoint that could drive the existing
10v10 pool directly does not exist yet — real future work, not attempted this pass.

**Two real bugs found and fixed while wiring this, both flagged honestly**: (1)
`export_rl_policy_to_c.py`'s own `symbol_prefix` mechanism (landed 2026-08-10 specifically so two
exported headers could coexist in one translation unit) never actually prefixed the per-layer
`layer{i}_w`/`layer{i}_b` weight-array symbols themselves — only macros/function names got it —
a real gap invisible until this pass, the first time `rl_policy_weights.h` and
`rl_policy_weights_team.h` actually got `#include`d together (`"conflicting types for
'layer0_w'"`). Fixed by prefixing every generated identifier, not just the subset the original
fix covered. (2) `scripts/run_bot_pool.sh`'s own orphan-guard `pkill` pattern — already fixed
once (2026-08-10) to scope by checkout path so the REDGARDEN/redgarden-stable split wouldn't
cross-kill each other's bots — had the SAME bug class recur one level finer: two pools sharing
ONE checkout (the 10v10 R&D pool and this new 3v3 pool) but different matchmaker ports would
still cross-kill on restart, since the path-only pattern didn't distinguish ports. Fixed by
additionally scoping the pattern to `--matchmaker-port` — REDGARDEN commit (this pass).

### 25.6 What this section deliberately does not resolve

- **Team size for the first real training run** — anywhere from 2v2 up to the real 10v10 is
  representable by §25.2.1's own `team_size` parameter; which one to actually train first is a
  compute/time tradeoff call for whoever runs it, not decided here.
- **Whether diversity-phase/optimization-phase alternation or reward-blending is the better
  noisy-gestalt implementation** — §25.2.3 names both, doesn't pick.
- **Numeric thresholds for synergy decay** (gold/kill lead trigger, decay magnitude, per-tick
  probability) — named as a real mechanic, not tuned.
- **Policy distillation / checkpoint crossover** ("peanut butter and jelly 2 bots") — real
  technique, no concrete REDGARDEN use case identified yet, intentionally left out of scope
  rather than spec'd speculatively.

## 26. Cross-game strategic transfer layer — fractal command hierarchy, contrastive state encoders (2026-08-10) -- spec only, no code yet

Second research thread from the same source conversation §25 draws its multi-agent RL material
from (full research notes: `docs2/MULTI_AGENT_RD_RESEARCH_NOTES.md` §2). Distinct question from
§25's own team-coordination scope: once a squad of bots has learned real team-play "intangibles"
in REDGARDEN specifically, is any of that transferable to a different game, or even outside games
entirely — the way a strong competitive player in one game often ramps faster in a new one than
someone with no competitive background at all?

### 26.1 The actual claim, scoped honestly

This is NOT a claim that a REDGARDEN-trained policy can literally play a different game. It's a
narrower, real research question: can the *representation* a policy learns be factored into two
layers — a game-specific mechanical layer (this exact game's controls/kit/numbers) and a more
general strategic layer (positioning discipline, commit-vs-retreat timing, tempo/resource
management) — such that the general layer transfers to a new game *faster than training from
scratch*, even though the mechanical layer still needs real fine-tuning per game. This is a real,
studied idea in the RL literature (representation transfer, meta-RL) — REDGARDEN would be the
first, and so far only, concrete environment to actually test it against here; no second game
environment exists yet to transfer *into*, so this section specs the representation-learning side
only, not a completed transfer experiment.

### 26.2 Contrastive state encoders

The concrete technique named for extracting that transferable layer: train a state encoder with a
contrastive objective (pull together representations of states that lead to similar strategic
outcomes, push apart states that don't) on top of §25's own team observation vectors
(`sim_get_obs_team`), instead of feeding raw floats directly into the policy network. If the
resulting embedding space captures "what matters strategically" rather than "what the raw numbers
happen to be," it's the natural thing to test for cross-game transfer later — and it's also useful
purely within REDGARDEN on its own, independent of any transfer claim: a better state
representation is a real, standalone lever for a stronger single-game policy too (representation
quality is a known bottleneck in small-observation-vector RL).

### 26.3 Fractal commander/soldier hierarchy

A structural idea for scaling team coordination past what one flat multi-agent policy can
represent well: nest the same policy shape recursively — a "commander" agent whose action space
is high-level directives to a handful of "commander-soldier" sub-agents, each of which in turn
directs its own "soldiers" (§25's own individual bot policies). This is a real, named pattern in
hierarchical MARL (feudal/hierarchical reinforcement learning is the established term for this
shape). At REDGARDEN's real team size (10 per side, `ARENA_TEAM_SIZE`), a two-level hierarchy
(one commander, ~3 commander-soldiers of ~3 soldiers each) is a plausible first structure to try
— not committed here, since §25.6 already left team size for the first real training run
undecided, and this compounds that same open question rather than resolving it.

**First real step (2026-08-10, `apps/arena_bot/src/main.c`).** The full hierarchical-RL version
above stays unbuilt — it needs a restructured, learned training loop, and this section's own
hierarchy-depth/branching question is still open. Built instead: a real, rule-based (not learned)
team-wide "Commander" signal that genuinely changes individual squad decisions, not just a spec.
`commander_posture_multiplier()` reads the live resource race (S170-153) and returns a posture
scalar — team meaningfully ahead → PATIENT (protects the lead), team meaningfully behind →
AGGRESSIVE (can't afford to wait passively while falling further behind) — real MOBA precedent,
not invented. Applied to the tower-siege-patience heuristic (`ARENA_BOT_DAMAGED_TOWER_PATIENCE_
BONUS`, itself from a founder ask to fold that behavior into this exact thread rather than leave
it standalone): a team's actual patience level now depends on team-wide state, not just local
per-bot information. Smaller in scope than the full commander/soldier hierarchy (one flat
posture scalar, not a real multi-level action hierarchy), but a genuine structural step in the
same direction. Verified: full regression suite + live 10-bot matchmaker/server stability, both
green. REDGARDEN commit `eb44a2e`.

### 26.3.1 Above the commander: a draft-phase agent (2026-08-11, research note, not scoped)

Founder real-time, while the first live team-trained checkpoint was being wired into
`apps/arena_bot` (§25.2.1's own real-world deployment, see EMILY/BACKLOG.md's squad-training
thread): a level ABOVE §26.3's own in-match commander. §26.3's commander directs live play once a
match has already started; this idea is an even-higher agent trained on the **pick/ban draft
phase itself** — which heroes to ban, which to pick, in what order — sitting above both the
commander and the soldiers in the hierarchy, deciding the team composition those lower levels
then have to execute with.

**Explicitly named as blocked on real game systems that don't exist yet** (founder: "obviously
those game systems need to be added to explore that") — there is currently no real ban phase
anywhere in this codebase. `apps/arena_server`'s live draft (confirmed live while testing the new
3v3 queue this same pass, `matchmaker-bots-3v3.log`: `"Lobby full (6 players) -- internal bot AI
disabled, entering draft."` / `"CLIENT 0 picked hero_id=20 (1/6 picked)"`) is PICK-only, no bans,
first-come-first-served rather than a structured turn order. A draft-phase RL agent has nothing
real to train against until banning and a real turn order exist as actual game systems.

**Draft-order shape, founder's own real-time sketch (self-flagged as uncertain, not committed)**:
"each team bans 1 then teach [sic] team bans another -> then each team pick one -> then each team
picks another -> then each team bans 2 more -> the they pick 2 -> then they ban 3 -> and pick 3 ->
then 5" -- then, in the founder's own words, "im not sure if thats too elaborate or can be
simplified." Transcribed faithfully, not smoothed over: this is an escalating alternating
ban/pick sequence (1 ban each, 1 pick each, 1 more pick each, 2 bans each, 2 more picks each, 3
bans each, 3 more picks each, then an unclear "5" -- ban or pick unspecified), described loosely
as "fibonacci." Flagged honestly: 1,1,1,1,2,2,2,2,3,3,3,3 isn't the literal Fibonacci sequence
(1,1,2,3,5,8...) -- it reads as an escalating/doubling RHYTHM the founder was reaching for, not a
specific formula to implement as-is. The founder's own closing doubt is the load-bearing part of
this note: **do not silently commit to a specific ban/pick count sequence from this transcript
alone** -- confirm with the founder before building a real draft-order system, same "spec the
model, don't guess the founder's own uncertainty away" discipline this file uses throughout.
Real structural precedent that DOES exist to build from: League of Legends' own tournament draft
(ban phase 1 -> pick phase 1 -> ban phase 2 -> pick phase 2, alternating between teams) is the
closest real, external reference point for "escalating alternating ban/pick phases," if a
concrete starting point is needed once the founder confirms direction.

**Also named, not yet scoped**: "humans will need to understand the process" (founder) -- any
real draft-order system needs a legible client-side affordance (whose turn is it, what's banned,
what's left) for a human player, not just something a training pipeline can parse. This is a real
UX requirement on top of the game-systems dependency above, not optional because bots don't need
to see a UI.

**Softened immediately after by the founder's own follow-up**: "but we do have an initial draft
without bans we can start to train on that i suppose in terms of the match overall team comp does
matter on some level." Real and correct — the pick-only draft confirmed live above (§ above,
`matchmaker-bots-3v3.log`) already produces a genuine team composition every match, and team comp
plausibly affects outcomes even with zero bans. So the "blocked on real game systems" framing two
paragraphs up is not a hard blocker on ALL draft-phase training, only on a full BAN-aware version
of it — a pick-only draft-composition signal is trainable against TODAY, using exactly the same
win/loss signal §25's own opponent-pool sampling already reads. Not built this pass (still a real
scoping decision the founder should make deliberately, not something to start building as a side
effect of this note), but the dependency chain below is revised to reflect it.

**Status: a real, logged research direction, not started.** Revised sequencing: (0) a pick-only
(no-ban) draft-composition training signal is ALREADY trainable against today's real game
systems, if/when the founder wants to prioritize it; (1) real ban-phase + structured turn-order
game systems (server + client-legible UX) remain the actual blocker for the FULL ban-aware
version described above; (2) once bans are real and playable, a draft-phase agent sitting above
§26.3's commander becomes a coherent next research step for the full version. Logged in
EMILY/BACKLOG.md's squad-training thread as a decision point, not silently dropped.

### 26.3.2 v0 autocurriculum scope expansion: tuning the game via items (2026-08-11, founder real-time)

A second, distinct addition from the same real-time thread, arriving right after the draft-phase
note above: "i want v0 autocurriculum to be tuning the game via the items." This is a real,
explicit SCOPE EXPANSION past §25.4's own current, deliberate boundary — that section's own text
says, verbatim: *"Explicitly not claimed: procedural generation of new game content/mechanics
driven by training (the source conversation's 'Mario Party mini-games as weight adjustments'
idea) — that conflates curriculum-over-opponents with curriculum-over-game-design; the former is
this section's real scope, the latter is not spec'd here at all."* The founder is now asking for
exactly that latter thing, as a real v0 goal, not a someday-maybe. Not contradictory or a mistake
to flag as an inconsistency — §25.4 described the boundary AS IT STOOD, honestly, at the time it
was written; a founder deciding to move that boundary later is a real scope decision, not a bug
in the earlier note.

**What this concretely means, read against real UED/PAIRED/POET literature** (the same family
§25.4 already cites for opponent-curriculum): those techniques don't just pick opponents, they
can also adapt the ENVIRONMENT itself to the current policy's weaknesses/strengths — POET
specifically co-evolves terrain difficulty alongside agent populations. "Tuning the game via
items" maps onto this directly: instead of (or alongside) sampling WHICH opponent to train
against, also sample or perturb ARENA_ITEMS' own stat values (`packages/simulation/arena_game.c`'s
existing 24-item catalog, S170-175) as part of the training curriculum — a real, established
technique (environment-parameter curricula), not an invented one.

**Clarified immediately after, narrowing which of the two**: "like it introduces new items to
meta break the top teams" — this is the LARGER of the two readings above: genuinely NEW items,
not just perturbed stats on existing ones, and specifically framed as a counter-strategy tool —
introduce new items to disrupt whichever team/composition is CURRENTLY dominant ("meta break the
top teams"). This is a direct game-design analog of PFSP's own "bias toward whatever the current
policy is losing to/being beaten by" logic (§25.4), just applied to CONTENT GENERATION instead of
opponent selection — genuinely closer to the "Mario Party mini-games as weight adjustments"
framing §25.4 originally, explicitly declined, now being asked for directly. Real and worth
naming honestly: this is a substantially bigger claim than opponent-pool sampling (procedural
item generation + an evaluation loop for "did this counter the current meta" is new machinery,
not a parameter tweak), not something to start building from this one-line real-time ask alone.

**Not scoped or attempted here** — a real design pass is still needed before building anything:
even granting "new items, not just perturbed stats," per the clarification above, does a newly
introduced item get evaluated the same PFSP-style way §25.4 evaluates opponents (did it measurably
counter the currently-dominant composition), or does it need its own separate objective (e.g.
maximize build diversity, or minimize one-item-dominates-every-build degeneracy)? What actually
generates a candidate new item's stats — a bounded random perturbation of the existing 24-item
catalog's own stat ranges, or something more generative? These are real founder-facing decisions,
not implementation details to guess through. Logged as a real, named v0 goal in
EMILY/BACKLOG.md's squad-training thread — scoping conversation, not code, is the next real step.

**Update 2026-08-25: the generation primitive is now real, built.** Founder real-time:
"continue the exotic auto curriculum redgarden work" → "training" → "parena mod driven first" —
the founder's own sequencing answer to "what generates a candidate new item's stats" above:
start with the PARENA-mod entry point, following the exact "mod is the trigger, host C does the
mutation" split every other REDGARDEN mod (Bloodflower, Tree passive, Build templates) already
uses. Landed: `stdlib/redgarden/item_curriculum_mod.prn` (PARENA repo) → `on-generate-counter-item`
→ `redgarden_host_item_curriculum_generate_counter_item` (`arena_game.c`), which blends two
existing `ARENA_ITEMS` catalog entries' own stat fields (average + a small, deterministic
per-field jitter — reproducible from the same two base items, not `rand()`-driven) into one of
`ARENA_ITEM_CURRICULUM_SLOT_COUNT=4` runtime-mutable slots kept SEPARATE from the fixed, const,
compile-time-sized `ARENA_ITEMS[]` catalog itself (every existing raw-int-id call site — shop UI,
inventory application, network snapshot item ids — would need auditing before a generated item
could safely enter live gameplay; deliberately not attempted here). 15 new tests, live round-trip
through the real compiled mod, full `test_arena.sh` green, all 4 build paths wired up front.
REDGARDEN `6d0443d`, PARENA `d63590a`.

**Still genuinely open, not resolved by this pass** — this is the "bounded random perturbation"
half of the two-part design question above, chosen as the honest, buildable v0 (not the "more
generative" alternative, which remains unscoped). The bigger, still-untouched half: WHICH two
items to blend from (reading which items the currently-dominant team composition is using isn't
even observable to the Python training loop today — `sim_get_obs_team_any`'s observation vector
carries no item-purchase state at all) and the evaluation objective (did a generated item
measurably counter the dominant composition, vs. some other diversity/degeneracy metric) are
both still real, unresolved training-loop questions. This pass is the generation primitive a
future consumer calls into, not the autocurriculum loop itself.

### 26.4 What this section deliberately does not resolve

- Whether representation transfer actually generalizes across games at all — untestable until a
  second game environment with the same contrastive-encoder training exists; not claimed as
  proven or even attempted yet, only specified as a real, worthwhile experiment once §25's own
  team environment has a trained policy to extract representations from in the first place.
- Hierarchy depth/branching factor for §26.3 — named as a real pattern, not tuned.
- Any application of this outside games (the source conversation's own "distill game theory,
  apply it to markets" extension) — real, interesting, and explicitly out of scope: this section
  specs a representation-learning technique for REDGARDEN's own bots, not a market-strategy
  product.

## 27. Physics-informed simulation research (2026-08-10) -- research note, not scoped to a REDGARDEN feature yet

Third research thread from the same source conversation (full notes: `docs2/
MULTI_AGENT_RD_RESEARCH_NOTES.md` §4). Physics-informed neural networks (PINNs) are a real,
established technique — neural networks trained with a loss term that penalizes violating a known
governing differential equation, used to approximate solutions to PDEs faster than classical
numerical solvers in some regimes. The source conversation asked whether a
fractal/hierarchical-multi-layer version of PINNs could apply to REDGARDEN or a related sim.

**Named honestly, not adopted as a spec here**: unlike §25/§26, this section does not scope a
concrete REDGARDEN feature, because none of this org's current physics needs (REDGARDEN's own
collision/movement code is a plain circle-vs-circle push-out, deliberately not a physics engine —
see `resolve_hero_obstacle_collision`'s own doc comment in `packages/simulation/arena_game.c`) are
PDE-shaped problems PINNs would apply to. The nearest real fit anywhere in this org is
`GOLDENBAND`'s `.gband` animation/BVH pipeline (HQ-SPEC-SIM-100), which is the closer candidate if
this thread gets picked up again — worth a founder decision on whether it's worth scoping there,
not decided in this document.

**Explicitly not carried forward from the source conversation**: claims from that same thread
about using resonance to demolish physical structures cheaply, and speculative "4-state
transistor" hardware for implementing physics computation directly — these aren't PINN research,
they're a different, ungrounded claim from later in the same conversation, and this section does
not adopt or specify them as anything to build.

## 28. Frame-break prompt pattern (2026-08-10) -- technique reference, not a REDGARDEN feature

Fourth item from the same source conversation (full notes: `docs2/MULTI_AGENT_RD_RESEARCH_NOTES.md`
§5) — distinct from §25-27 in kind: not a game-AI research thread at all, a reusable
prompt-engineering pattern that emerged during the conversation and was applied repeatedly there.
**The pattern**: given a surface-level request, respond by naming the underlying
structural/systemic pattern the request is one instance of — one level of abstraction up — rather
than answering the literal surface request directly.

This is real and reusable as a prompting technique on its own terms. It has no REDGARDEN product
surface (nothing here plays a bot, trains a policy, or ships a feature), so it doesn't get a
milestone/VS0 shape the way §25-26 do. Recorded here because it came from the same research
session and the founder asked for the research to be captured, not because it's REDGARDEN scope —
if/where it's worth applying in this org's own tooling (emily-agent's own prompting, HEIMDAL
sprint translation, etc.) is a separate, undecided question this section doesn't resolve.

## 29. ECOWAR — RTS command layer + MOBA combat layer, deck-building as the interface (2026-08-13) -- scoping only, no code yet

Founder real-time, across several messages: name the mode **ECOWAR** (the original vertical-slice
design doc's own "Ecosystem War (2v2): shared structures + dragon objective" game mode, §8 of the
"RED GARDEN — Original Vertical Slice Design Capture" section of `README.md`) — but reframed as a
genre synthesis rather than a literal build of that doc as originally scoped: *"bringing the moba
full circle back to RTS with TCG as the moonchild... deck building as the affordance for
commanding your army"* — i.e., the RTS's card-based command interface directs a MOBA-style
hero-driven combat layer, not the original doc's own 16-unit/8-structure autonomous-behavior
roster. Explicitly its own thing: **separate game mode, separate matchmaking, separate client**
("we will unify clients later" — for now, the existing headless test-bot client is the
affordance for quick iteration, no dedicated rendered client needed yet).

### What exists today, checked directly against real code

Two genuinely separate, already-isolated codepaths this mode can draw from — neither has ever
shared state or a process with the other:

- **The RTS/card layer** — `packages/simulation/local_game.c`/`.h` (274/54 lines), driven by
  `apps/server`, `apps/client` (headless bot, `bot_main.c`), `apps/matchmaker`. Real, working, but
  a genuine MVP slice of the original 14-section design doc, not the full thing: a 20×20
  `GridCell` automaton (`tick_automata`, neighbor-based state conversion, matches the doc's own
  §3 update rules), exactly **4 cards** (`CARD_MILITIA`/`SCOUT`/`SWARMLINGS`/`OUTPOST`, real costs
  `{2,3,1,5}` and cooldowns, `PACKET_CARD_PLAY` wire protocol already shipped), an influence
  economy, and a `tech_tier[3]` field that exists in `ServerState` but isn't yet read by any tech
  effect (`local_apply_card` doesn't branch on it). No Tier 2-4 tech, no 16-unit roster (just the
  4 cards' own entity types), no structures beyond Outpost, no quest system, no win condition
  check found in `local_update`'s own real logic. `[3]`-sized team arrays (`influence`,
  `tech_tier`, `control_hold_ms`) already assume up to 3 owners, not just 2 — worth checking
  whether that's already 2v2-shaped or just headroom that happened to be sized that way.
- **The MOBA/hero layer** — `packages/simulation/arena_game.c`/`.h`, `apps/arena`,
  `apps/arena_bot`, `apps/matchmaker` (same binary, different `--lobby-size`/`--server-bin` flags
  — see that file's own header comment; this dual-role pattern is the direct precedent for how
  ECOWAR's own "separate matchmaking" should probably work: a third matchmaker mode/binary
  invocation, not a new matchmaker written from scratch). 26 heroes, 33-item shop, Jungle Camps,
  the full arena bot AI research program (§25-28) — see `README.md`'s own "Current Status" and
  "Arena bot AI research program" sections for the current real state.

### The real design question this doc does NOT resolve

How a card-driven command interface actually directs hero-driven combat units is a genuine open
architecture question, not assumed or invented here:

- Does the player also get one directly-controlled hero of their own (arena-style click-to-move),
  with cards spawning additional army units around them — or is the player *purely* a card-playing
  commander with zero directly-piloted unit, every hero on the field spawned/ordered via cards?
  The founder's own phrase ("deck building as the affordance for **commanding your army**") reads
  closer to the second, but doesn't fully rule out the first.
- Are card-spawned army units full hero kits (Q/W/E abilities, items, the existing 26-hero roster)
  or a simpler unit tier below hero complexity — closer to the original doc's Militia/Scout/
  Swarmlings automaton-driven behavior, just re-skinned onto arena's movement/combat code instead
  of `local_game.c`'s?
- "Shared structures + dragon objective" (the original Ecosystem War framing) still needs a real
  mechanical definition — is the dragon a Jungle-Camps-style neutral boss (reusing that system,
  see README's Jungle Camps section) or a new, separate objective?
- Which simulation loop does ECOWAR actually run on — a new third `ServerState`-shaped struct
  reusing pieces of both, or one of the two existing loops extended with the other's concepts
  grafted in? This is the single biggest real engineering decision and isn't guessed at here.

### Addendum (2026-08-20): two of the four open questions resolved, card/economy design added

Founder, real-time, across many short fragments (`emily observe` posts ~03:30–03:41Z this
session, before this write-up per Principle 18): 1v1, a separate binary "same model as all our
other games," online matchmaking with bots first, hero mechanics, MOBA-style skill upgrades for
build variety, RTS building + commanding troops, diplomatic relations, media, street cred, all of
it deck-based (22-card deck + hero pick), some cards hero-specific and some generic, a design rule
that "any affordance that doesn't involve directly moving your hero, pets, or clones can be a
card," rarity tiers, a marble-bag + Fibonacci-pity pull algorithm for both in-match draw and pack
acquisition, non-depleting Clash-Royale-style card cycling, a resource speed-up/catch-up mechanic
after ~2 minutes, packs purchasable with Flow, card art farmed from Prompt-o-verse taxonomy node
combinations (e.g. Medusa) as reference/placeholder art with real 3D models to follow later, and
that simple tower models already exist in the codebase as a real starting asset for RTS
structures.

**Resolves open question 1 (directly-piloted hero vs. pure card-commander):** it's both, not
either/or. "We will have the hero mechanics" plus the explicit card-boundary rule ("anything that
doesn't involve directly moving your hero, pets, or clones can be a card") settles this cleanly —
the player directly click-to-moves one hero, arena-style, same as REDGARDEN's existing MOBA layer,
*and* directly controls that hero's pets/clones the same way. Everything else — RTS building,
commanding troops (as opposed to directly walking them), diplomatic relations, media, street cred,
buffs/spells/summons — is the deck's job. This reframes the founder's own original "commanding
your army" phrase from this section's first pass: the army isn't *entirely* card-summoned-and-
forgotten, only the parts that aren't a hero/pet/clone are.

**Resolves open question 4 (which simulation loop) — recommendation, not yet built:** "same model
as all our other games" plus a directly-piloted hero point at `packages/simulation/arena_game.c`/
`apps/arena` (the MOBA/hero layer) as ECOWAR's base simulation loop, extended with card-driven RTS
systems grafted on — not a fresh third `ServerState`, and not `local_game.c`'s automaton (per this
repo's own established convention: "MOBA" means `apps/arena`, the card-RTS is a different, separate
thing — do not conflate them). Matchmaking follows the arena/arena_bot dual-role precedent this
section already cites: a third `--lobby-size`/`--server-bin` invocation of the existing
matchmaker binary, 1v1-sized, bots-first for queue-fill the same way REDGARDEN's other modes
already do it — not a new matchmaker service. This is a recommendation grounded in tonight's real
fragments, not yet implemented or confirmed with the founder as final.

**Still open, unresolved by tonight's fragments:** card-spawned-troop complexity (open question 2
— "commanding troops" via cards reads like a simpler unit tier below full hero-kit complexity,
consistent with this section's own original guess, but not explicitly confirmed) and the dragon/
shared-structures objective (open question 3, untouched tonight).

**A real naming collision, flagged rather than silently resolved:** REDGARDEN already has a "Flow"
(§19, kill-fed per-hero shop currency, distinct from the team-level `resources[team]` win-condition
meter) and TRAPX/SHANKPIT has a separate, unrelated "Flow" (Field-Office territory-generation
currency, `SHANKPIT/docs2/TRAPX_NORTHSTAR.md`). Tonight's "packs purchasable with Flow" doesn't
specify which — and two more fragments the same session (an account apparently already holding
Flow in GoblinFoxDragon; a floated reuse of Flow as the currency for IDUNA's legacy-documented
poker-tournament-platform phase-2 concept) suggest the founder may be leaning toward one *shared*
Flow economy across REDGARDEN/GFD/GTA7/ECOWAR/poker rather than these staying separate systems
that happen to share a name. That's a real, monorepo-wide economy-architecture decision, bigger
than this one doc — flagged here, not decided here.

**Card & economy design, net-new (no prior art found anywhere in this repo, TRAPX_NORTHSTAR.md,
or the REDGARDEN-wiki specs for rarity/pack/pity mechanics — confirmed via a real doc survey
before writing this):**

- **Deck:** 22 cards per deck, some hero-specific (locked to whichever hero you picked that match)
  and some generic (usable by any hero) — mirrors the hero-specific-vs-shared item/ability split
  MOBAs already use, applied to the card layer instead.
- **Draw:** non-depleting, Clash Royale-style rolling cycle — the deck never runs dry mid-match,
  cards cycle back in rather than being consumed permanently.
- **Rarity tiers:** Normal (base tier, confirmed) → Rare (placeholder single tier above Normal for
  now) → Legendary (locked in) → Mythic / Archonic (floated as candidates above Legendary, final
  naming and count of tiers between Rare and Legendary still TBD).
- **Pull algorithm — one mechanism, two call sites:** a weighted marble-bag selection with a
  Fibonacci pity counter (a rare/legendary tier that's gone unusually long without appearing gets
  progressively more likely rather than staying flatly improbable forever) drives *both* in-match
  card draw *and* player pack-opening/card acquisition. The same mechanism was independently
  floated this session for Prompt-o-verse's own style/subject discovery, which currently has no
  comparable fairness floor — real cross-pollination opportunity, worth building once as a shared
  utility rather than twice as unrelated bespoke code (see the published Prompt-o-verse sprite-
  engine dialogue post, 2026-08-20, for the fuller version of this idea).
- **Acquisition:** packs purchased with Flow (see the naming-collision flag above).
- **Resource pacing:** a Clash-Royale-style catch-up/speed-up effect kicks in after roughly 2
  minutes, same genre-standard shape as elixir/resource acceleration in comparable games.
- **Card art:** farmed from Prompt-o-verse taxonomy node combinations (e.g. the real
  `medusa-ffxi` generation already used as BRAWLPIT/GOLDENBAND's own visual reference this
  session) as reference/placeholder art — a 3D card object is an acceptable placeholder shape too.
  Real 3D models are the intended eventual asset pipeline, same "gold standard before going more
  realistic" staging this session already established for DragonsNShit's own rendering roadmap —
  not assumed to arrive alongside this doc.
- **RTS structures:** simple tower models already exist in the codebase (unconfirmed exactly
  where as of this writing — flagged for the next session that touches asset pipelines to locate
  and confirm) as a real starting point for the building/commanding-troops card set, rather than
  needing new models before any RTS-layer card can be prototyped.
- **Faction/diplomacy/media/street-cred prior art:** TRAPX's real Faction Reputation system
  (`SHANKPIT/docs2/TRAPX_NORTHSTAR.md`, and GTA7's own live implementation of it —
  `FactionManager`/`MediaManager` in `GTA7/plugin`) is the closest existing mechanical precedent
  for these four card-driven systems, even though it currently lives on a completely different
  engine (Paper/Minecraft, not this repo's own simulation loop) — worth reading before designing
  ECOWAR's own version from scratch.

### Status

Scoping only. No milestone plan, no code. The next real step is settling the two still-open
questions above (card-spawned-troop complexity, the dragon/shared-structures objective) — likely
via a founder AskUserQuestion-style pass — before a phased build plan can be written honestly
(matching this repo's own established discipline: `NORTHSTAR.md`'s other "spec only" sections
don't invent milestone numbers ahead of a real design decision either).
