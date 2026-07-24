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
