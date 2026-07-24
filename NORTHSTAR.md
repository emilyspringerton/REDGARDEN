# NORTHSTAR — RED GARDEN

**Status:** Core product (elevated 2026-07-19) — real-time scoping session, capturing founder
direction accurately before implementation continues, per the Emily Way's "spec before
implementation."

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

**Deliberately not done yet — a real fork, not an oversight.** shankpit-460 reports FPS
`kills`/`deaths` to IDUNA's `/api/v1/players/{id}/session` endpoint; that schema is FPS-specific.
REDGARDEN is a card-RTS with a `match_winner` field (win/loss), not kills/deaths — forcing one
into the other's columns would corrupt shared WOTAN profile semantics across every game using that
table. Reporting REDGARDEN results into IDUNA needs either a genre-agnostic schema addition
(`wins`/`losses`/`matches_played` columns) or a separate endpoint. That's a real IDUNA schema
decision, flagged here rather than guessed past — see `EMILY/BACKLOG.md` S170-26.

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

**Not done yet — the other 10 heroes** (Donkey, Ghost, Frog, Tree, Pizza, Retrieval Cart, Doc
Wheel, TYLER, Flamel, Druid). Each is its own follow-on pass, not bundled into this one — "not all
at once, obviously in phases" applies inside Phase D too.

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

Nothing above is built. Each phase depends on the one before it — the sequence is the plan, not
just the list.
