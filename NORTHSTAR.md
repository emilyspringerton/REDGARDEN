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
