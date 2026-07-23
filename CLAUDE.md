# REDGARDEN — CLAUDE.md

## What this is

A deck-based real-time strategy game: Clash Royale's card-hand/mana-economy model over a living
cellular-automata board (Neutral/Player/Enemy/Corrupted cells that spread and react on their own
2-second tick, independent of direct player action). Prototype proving-ground for FIELDOFFICE/
TrapX's territory-custody mechanic (`SHANKPIT/docs2/TRAPX_NORTHSTAR.md`) — build it here first,
port proven mechanics later. See `NORTHSTAR.md` for the full, current direction; this file is
commands/layout only.

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

## Commit Protocol (standing instruction)

Always commit and push completed work immediately — don't wait to be asked. This is the default for every repo in this monorepo.
