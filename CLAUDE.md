# REDGARDEN — CLAUDE.md

## What this is

A deck-based real-time strategy game: Clash Royale's card-hand/mana-economy model over a living
cellular-automata board (Neutral/Player/Enemy/Corrupted cells that spread and react on their own
2-second tick, independent of direct player action). Prototype proving-ground for FIELDOFFICE/
TrapX's territory-custody mechanic (`SHANKPIT/docs2/TRAPX_NORTHSTAR.md`) — build it here first,
port proven mechanics later. See `NORTHSTAR.md` for the full, current direction; this file is
commands/layout only.

**2026-08-10: REDGARDEN is now dedicated to R&D** (founder: "REDGARDEN is now dedicated to R and
D"). The arena bot AI research program (`NORTHSTAR.md` §25-28 — multi-agent RL, role discovery,
noisy gestalt, synergy decay, autocurriculum, cross-game transfer) lives and iterates here, fast,
including things that break the live `:7778` R&D matchmaker/bot-pool deployment. The actual live
product built on REDGARDEN's tech — GoblinFoxDragon's Battlegrounds — no longer depends on this
deployment at all; see "Deployments" below.

## Deployments (R&D vs. stable — hard separation, 2026-08-10)

Two full, independent deployments of the same source, never sharing a process or a port:

| | R&D (this repo's fast-iteration target) | Stable (serves GFD's Battlegrounds) |
|---|---|---|
| Checkout | `/home/fatbaby/REDGARDEN` (active dev) | `/home/fatbaby/redgarden-stable` (separate clone, manually promoted) |
| Matchmaker (bot pool, 10v10) | `:7778` — `redgarden-matchmaker-bots.service` | `:8778` — `redgarden-stable-matchmaker-bots.service` |
| Matchmaker (player-only, 1v1) | `:7779` — `redgarden-matchmaker-players.service` | none yet |
| Bot pool | `redgarden-bot-pool.service` | `redgarden-stable-bot-pool.service` |
| Auto-deploy | `redgarden-auto-deploy.timer` (polls CI, restarts R&D units only) | none — promoted manually: `cd /home/fatbaby/redgarden-stable && git pull && bash scripts/build.sh && systemctl --user restart redgarden-stable-matchmaker-bots.service redgarden-stable-bot-pool.service` |
| Consumed by | REDGARDEN's own dev/test loop | `GoblinFoxDragon/apps2/mud/main.go`'s `redgardenMatchmakerPort` (Battlegrounds) |

`apps/arena_bot`'s matchmaker port is a runtime flag now (`--matchmaker-port`, default 7778) —
this is what makes the same unmodified source safely serve both deployments; never hardcode a
port assumption back into that binary. `scripts/run_bot_pool.sh` takes the matchmaker port as its
second argument for the same reason, and scopes its own orphan-guard `pkill` to its checkout's
absolute path specifically — a bare relative-path pkill pattern would kill both deployments' bots
at once (a real bug, found and fixed the same session this split was built).

## Current status

See `README.md`'s "Current Status" section and `NORTHSTAR.md` for what's actually built vs.
aspirational. Short version: VS0 (bot-vs-bot matches) and VS1 (online play, matchmaking,
connect-ticket accounts) are both validated; hero/item/cooking content is written but not wired
into code; no packaged/distributable client exists yet.

## Build & test

```bash
bash scripts/build.sh              # builds red_garden_server, _bot, _lobby, _matchmaker into build/
bash scripts/test_10_bots.sh        # VS0/VS1 validation: matchmaker + 10 headless bots
bash scripts/test_arena.sh          # headless smoke tests for apps/arena's sim logic
```

## Repo map

| Path | What it is |
|---|---|
| `apps/server` | Card-RTS game server — one match per process (single global `ServerState`) |
| `apps/client` | Headless test bot (`bot_main.c`) |
| `apps/matchmaker` | Pairs queued clients, spawns a dedicated server per match |
| `apps/lobby` | SDL2/OpenGL rendered client — not yet wired into the matchmaker/ticket flow |
| `apps/arena` | Separate, additive single-hero click-to-move demo — doesn't touch the above |
| `packages/common` | Wire protocol (`protocol.h`), `hmac_sha256.h` (connect-ticket auth) |
| `packages/simulation` | `local_game.c` — grid, cards, entities, tech tree, win condition |
| `docs/HEROES_VS0.md` | Hero ability kits (content only, not wired into code yet) |
| `docs/CONSUMABLES_AND_COOKING.md` | Item names + cooking/crafting direction |

## Accounts

Connect-ticket auth, same HMAC-SHA256 scheme as sibling repo shankpit-460 (`packages/common/
hmac_sha256.h`, ported verbatim, RFC 4231 test vectors re-verified here). `apps/server` verifies
tickets on `PACKET_CONNECT`, fails closed without `REDGARDEN_TICKET_SECRET` set. Test bots
self-mint tickets (mirrors shankpit-460's `emily-bot` pattern) — no real IDUNA account needed for
headless QA.

## UI constraint (cross-cutting, see NORTHSTAR §2)

All shop/menu surfaces (item shop, cooking, crafting) need high-APM affordances — both keybind
and click paths must resolve instantly, no menu-diving, designed for pro-level play speed while
staying legible to a casual player standing next to them.

## CHANGELOG Protocol

Append a dated bullet to `CHANGELOG.md` for any meaningful change.

## Apple Filing Protocol

```bash
emily apples post -t completion -repo REDGARDEN "<title>" "<body with commit hash>"
```
Then mark the item done in `EMILY/BACKLOG.md` and commit.

## Golden Doc Registration

If you create a new NORTHSTAR.md, architecture spec, or mission-critical design doc in this repo,
append a row to `EMILY/context/golden-docs-index.md` so Emily Prime picks it up on the next cycle.
Then commit and push EMILY.

## Related Repos

- `SHANKPIT` — sibling C/SDL2/OpenGL + Go engine; shares the server-authoritative UDP model
- `shankpit-460` — source of the connect-ticket auth pattern this repo reuses
- `TYLER` — `multiverse_heroes.md` is the lore compendium the hero queue draws from
- `GoblinFoxDragon` — mob/NM/loot systems the jungle-ecology direction (NORTHSTAR §8) grafts onto
- `EMILY` — RSI loop / backlog coordination for cross-repo work
- `OKEMILY` — `redgarden.html` early-access waitlist page

## Founder Real-Time Direction

Whenever the founder gives real-time direction — a new ask, a correction, a "can we also..." —
route it through `emily observe -s info "Founder real-time: <summary>"` first, even if it isn't
this repo's usual domain, then sprint-plan it into `EMILY/BACKLOG.md` (`emily backlog curate`,
scoped into a real SECTION/sub-item, not just a one-line log), and only then implement. See
`EMILY/docs/THE_EMILY_WAY.md` Principle 18 ("Pave the Cow Paths").

## README Reality — SAGA reconciliation (standing instruction, monorepo-wide)

Founder real-time, 2026-09-18: if a change of yours **substantially changes the claim of this project's core README**,
then per SAGA protocols (`EMILY/docs/SAGA_SYSTEM_AUDIT_2026-07-18.md`, HQ-SPEC-DOC-102: intent ↔ claim ledger ↔ reality)
you **must update `README.md` in the same unit of work** so it reflects current reality. The README is the project's public
claim; it must not lag behind the code.

- **When it applies:** a capability is added or removed; status moves ("design only" → "working", "planned" → "shipped");
  the stack, build, run or install steps change; a claim in the README is now false or stale; or you add a **meaningful,
  genuinely interesting piece of kit** (a new tool, engine capability, protocol, pipeline, game system). For that last case
  especially: put it in the README — what it is, how to run it, and its honest status and limits.
- **When it does not:** ordinary fixes, refactors and small features that leave the README's claims true.
- **How:** re-read the README against what you just changed; fix or delete stale lines (including "not built yet" notes that
  are now built); verify any new claim by actually running it, and mark anything untested as untested; commit the README
  with (or immediately after) the change, and mention it in the CHANGELOG entry.

## Frame-Break Reframing

Founder-sourced prompting technique (REDGARDEN/NORTHSTAR.md §28, full origin in
REDGARDEN/docs2/MULTI_AGENT_RD_RESEARCH_NOTES.md §5): given a request, name the underlying
structural/systemic pattern it's one instance of — one level of abstraction up — as an added
lens during planning/triage/judgment calls. Use it to spot the general case behind a specific
ask. It augments judgment, it does not replace doing the work: direct, concrete execution of
the literal task asked for still happens every time.

## Commit Protocol (standing instruction)

Always commit and push completed work immediately — don't wait to be asked. This is the default for every repo in this monorepo.

Every commit — human-written or produced by automated code paths (git-commit helpers in emily-agent, emily.cli, IDUNA handlers, etc.) — must carry the active `emily session` fingerprint as a `session: <tag>` trailer (blank line, then the trailer). This was silently missing from several independently-implemented automated commit helpers across the monorepo until an audit on 2026-08-10 (founder, real-time: "where in the fuck is my llm session id anywhere"). If you add a new automated git-commit code path anywhere, wire in the session tag the same way — don't assume an existing helper already does it.
