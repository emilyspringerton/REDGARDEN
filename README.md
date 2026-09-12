# RED GARDEN

## How to Play (Knights of the Void — the arena MOBA, `apps/arena`)

*S170-97: the real keybind contract, synthesized in one place rather than scattered as code
comments in `apps/arena/src/main.c` and left implicit across every hero entry in
`docs/HEROES_VS0.md`.*

| Input | Does |
|---|---|
| **Left click** | Move to that point, or lock onto and auto-attack a live enemy hero if the click landed on one (team matches only — see NORTHSTAR §17). Walking into range of an enemy also auto-attacks it, so there's no separate "attack" input for melee range. Commands whichever of your own units are currently selected — for every hero except Tyler that's always just yourself, so this is unchanged from the description above unless you've drag-selected (below). |
| **Left click + drag** (Tyler only) | Box-select your own puppet clones (2026-07-30, "Divided We Stand" rework — true Meepo parity, clones no longer auto-follow). Drag a box over yourself and/or your active clones to select just them, then a plain click commands only the selected units — split your army and send nets in different directions. A quick click (below the drag threshold) is never treated as a drag, so ordinary move/attack clicks are unaffected. An empty box resets selection back to "just yourself." No other hero has anything to select, so this row is a no-op for the rest of the roster. |
| **Q / W / E** | Your three ability slots, in order. Every hero's kit maps its abilities to exactly these three — `docs/HEROES_VS0.md` lists each hero's real Q/W/R names and effects, but the *keys* are always Q/W/E, never anything hero-specific. W is either an instant effect on cooldown or a hold-on/hold-off toggle depending on the hero (`arena_hero_w_is_toggle()`) — toggles drain mana continuously while held rather than charging a flat cost up front. |
| **`** (backquote/tilde) | Use your equipped active item — Blink Dagger (short blink toward the cursor) or Donkey's Paper Glide (dash away from the nearest enemy), whichever one you actually have equipped. No-op with neither equipped. |
| **B** | Toggle the shop panel (S170-175) manually. The panel also opens/closes on its own once you walk within `ARENA_SHOP_RADIUS` of your own team's shop and leave it again (2026-07-30) — `B` still works on top of that for whenever you want it open away from the shop. |
| **1–9** (shop open) | Buy the item in that row of the *current* shop page — a single action, no confirm step. The 33-item catalog is paginated 9-per-page (2026-07-30) so every visible row always has a live `1`-`9` keybind, unlike the old single giant list. Click an occupied slot in the loadout column to sell it back for half price. Purchases only resolve for real within `ARENA_SHOP_RADIUS` of your own team's shop. |
| **Shift+1 through Shift+4** (shop open) | Jump straight to shop page 1/2/3/4. Four clickable page-number buttons above the item list do the same thing for mouse users. Page 4 (2026-08-11, "add page 4 to the shop") holds the 6 newest items — see the item table below. |
| **Held TAB** | Scoreboard: every hero's kills/deaths/Flow/XP, plus a team-aggregate row, both teams side by side. |
| **H** | Toggle an ability-description overlay for your own hero's Q/W/E. |
| **Right click + drag** | Rotate the camera around your hero. No-op while camera lock (`C`) is on. |
| **Mouse wheel** | Zoom the camera in/out. Always works, even while camera lock is on. |
| **C** | Toggle camera lock (NORTHSTAR §15.1). The camera already always follows your hero's position — locking freezes the rotation angle too, so you can't look away from them. Shows "CAM LOCKED" on screen while on. Starts unlocked every match. |
| **F11** | Toggle the APM (actions-per-minute) overlay. Works in any mode. |
| **R** | Restart the match. Local practice mode only (against the built-in bot) — disabled entirely once you're in a real networked match, since a real match has other players in it. |
| **Click "OK"** | After a match ends (win or loss), requeues you for another one. Networked mode only. |

**Zone abilities (S170-200):** 8 heroes' R (Ghost, Flamel, Morrigan, Paimon, NOOR-1, Vassago, He
Xiangu, Beleth) cast a real ground-radius zone, not a single-target effect — plus Gunnr's W
(Consecration, 2026-07-30, "just like WoW"), the one zone ability on this slot instead of R.
Whenever one of these is ready to cast, a faint ring shows exactly where and how big it'll land
before you press the key — every zone always casts centered on your own current position, so
that's always where the ring is. Once cast, a filled, pulsing circle marks the real zone for its
actual duration, visible identically to everyone in the match (allies and enemies), not just the
caster.

Draft is automatic right now — no pick UI yet, you're assigned a hero based on your slot in the
lobby (`docs/HEROES_VS0.md` documents every hero's kit if you want to know what you're about to
play before the match starts). Team matches are **10v10** (`ARENA_TEAM_SIZE`, S170-183 —
briefly 7v7 under S170-178, reverted). A separate, dedicated **3v3** queue also exists (`:7780`,
`redgarden-matchmaker-bots-3v3.service`/`redgarden-bot-pool-3v3.service`) — the only queue where
the trained team-RL checkpoint's `team_rl_engage_nudge()` actually influences bot behavior
(hard-gated no-op everywhere else, since that checkpoint is a fixed `team_size=3` shape). See
"Arena Bot AI" below.

### Flow, XP, and the item shop (S170-175)

Every hero earns two currencies from kills — node-guardian creeps, lane creeps, and enemy heroes, melee
or Gary's homing shot only, not ability-finished kills:

- **Flow** — the spendable currency (this game's "gold"). Shown live in the always-on character
  pane, bottom-left of the HUD, alongside your current HP/MP/AD/Armor and K/D.
- **XP** — a separate running total, also shown in the character pane. No leveling system yet;
  it's tracked for the scoreboard and future power-curve work (NORTHSTAR §19.4).

Two shops sit in the two map corners that don't already have a healing fountain, one per team —
visible in-world as an amber structure with your own team's color trim, everyone else's in the
enemy's. Buying auto-equips into that item's slot (11 slots, a mix of FFXI and WoW vocabulary:
Weapon/Head/Body/Hands/Legs/Feet/Ring/Neck/Back/Waist/Trinket) and auto-sells whatever was already
there first — there's no bag, and no way to unequip into one; selling is the only way back out of
a slot, at half the item's cost. `docs/FFXI_ITEM_PARITY_SEED.md` is the real-FFXI-name source for
the catalog's generic-tier items.

### Item stats

The full 43-item catalog (`ARENA_ITEMS`, `packages/simulation/arena_game.c`, `ARENA_ITEM_COUNT`)
— every stat bonus applies the instant you buy, no equip delay. Weapon carries 12 named items
from `docs/HEROES_VS0.md`'s own "Season 3 LoL" starting roster plus 2 "weird" items with unusual
stat shapes pulled from real FFXI end-game weapon reputations (Kraken Club: huge AD, zero
defense; Ridill: an oddly-even split across all three defensive/offensive stats). Every other
slot gets one plain, real FFXI-named item (`docs/FFXI_ITEM_PARITY_SEED.md`), plus three items
with real mechanics instead of/alongside flat stats — Blink Dagger (S170-205, the `` ` `` active),
Donkey (S170-206, Immortal's Fold + Paper Glide), Haste Trinket (S170-207, flat CDR%).

**Page 4, added 2026-08-11** ("expand the play space" pass, founder: *"using ffxi items and your
own best judgement on how the new items with unique qualities... push the meta forward"*): 6 more
real-FFXI-named items, introducing this catalog's first two genuinely new damage mechanics —
**true damage** (bypasses armor entirely, applied after `apply_armor`) and **lifesteal** (heals
the attacker off final post-armor damage) — plus the catalog's first **dynamic, live-computed**
stat (Balance Ring's armor scales with the wearer's own missing-HP fraction every tick, a
rubber-band comeback mechanic in the same spirit as §25.3's synergy decay below).

**Costs tripled 2026-08-13** (founder real-time: "the page 4 items need tripple costs") — a
first pass at page-4-only pricing, with other items flagged for a later balance iteration, not
touched in this pass.

**Kite String, added 2026-08-26** (founder: *"add an item that increases auto attack range by 4%
3333 flow 'Kite String' trinket"*) — no flat stats at all, same "this *is* the item, not a bonus
on top of one" shape Haste Trinket already established: +4% auto-attack range
(`bonus_attack_range_pct`), the catalog's first range-boosting stat.

**Luck of the Draw, added 2026-09-03** (founder, cruise-queue: *"we should have a weapon that is
on like page 5 for 2.2k flow a trinket called 'luck of the draw' that gives some mana regen
during combat"*) — same "this is the item" shape as Kite String: a flat +1 mp/sec regen while in
combat (`bonus_mp_regen_combat`), a real, modest improvement, not a build-defining spike.

**8 more, added 2026-09-11 — a real GFD-item-database-inspired pass** (founder: *"iterate on
REDGARDEN add some more items look into the GFD item database for inspiration bring in AD not
just AP"*): real source, not invented — `GoblinFoxDragon/data/items.json`, a genuine FFXI-styled
155-item catalog with a real Attack/STR/DEX (physical, "AD") vs. Magic Attack Bonus/INT/MND
(magic, "AP") split. This engine has no ability-power-scaling mechanic at all (every hero's Q/W/R
deals its own flat, hardcoded damage) — confirmed with the founder before writing any code — so
"AP" items translate onto the existing mana/CDR stats as the closest real "caster" analog, not a
new damage stat; real ability-power scaling across all 26 hero kits is separate, larger,
not-yet-attempted follow-on work. Five of the eight close a real gap found by directly counting
the catalog: Body, Legs, Feet, Neck, and Waist each had exactly **one** item before this pass —
zero real build choice in 5 of 11 slots. The other three are new flagship Weapon items, including
**Excalibur** — by a wide margin GFD's own single strongest item, now this catalog's new most
powerful (and most expensive) item. This pass is what pushes the shop into a genuine 5th page for
the first time (43 items ÷ 9/page).

| Item | Slot | Cost | AD | HP | MP | Armor | Move Speed | CDR% | True Dmg | Lifesteal% | Atk Range% | MP Regen (Combat) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Seedling Charm | Weapon | 300 | +8 | +40 | — | — | — | — | — | — | — | — |
| Bramble Fang | Weapon | 1000 | +35 | — | — | — | — | — | — | — | — | — |
| Thornrender | Weapon | 950 | +28 | +10 | — | — | — | — | — | — | — | — |
| Bloomheart Core | Weapon | 1100 | +45 | — | — | — | — | — | — | — | — | — |
| Wanecall Grimoire | Weapon | 950 | +25 | — | +60 | — | — | — | — | — | — | — |
| Ironbark Plate | Weapon | 900 | +10 | +150 | — | +20 | — | — | — | — | — | — |
| Willowveil | Weapon | 850 | — | +120 | — | +25 | — | — | — | — | — | — |
| Vampiric Bloom | Weapon | 1000 | +32 | +30 | — | — | — | — | — | — | — | — |
| Splinterfang | Weapon | 900 | +30 | — | — | — | — | — | — | — | — | — |
| Hollow Needle | Weapon | 900 | +30 | — | +40 | — | — | — | — | — | — | — |
| Rootrunner Treads | Weapon | 500 | — | +10 | — | — | +0.8 | — | — | — | — | — |
| Gardener's Ward | Weapon | 800 | — | +100 | — | +15 | — | — | — | — | — | — |
| Kraken Club *(weird)* | Weapon | 1200 | +60 | — | — | — | — | — | — | — | — | — |
| Ridill *(weird)* | Weapon | 1100 | +20 | +20 | — | +20 | — | — | — | — | — | — |
| Optical Hat | Head | 400 | — | +60 | — | — | — | — | — | — | — | — |
| Haubergeon | Body | 450 | — | — | — | +18 | — | — | — | — | — | — |
| Battle Gloves | Hands | 400 | +12 | — | — | — | — | — | — | — | — | — |
| Iron Ram Trousers | Legs | 400 | — | — | — | +18 | — | — | — | — | — | — |
| Creek F. Boots | Feet | 400 | — | — | — | — | +0.6 | — | — | — | — | — |
| Astral Ring | Ring | 350 | — | — | +50 | — | — | — | — | — | — | — |
| Justice Badge | Neck | 400 | — | — | — | +14 | — | — | — | — | — | — |
| Forager's Mantle | Back | 350 | +8 | — | — | — | +0.4 | — | — | — | — | — |
| Warwolf Belt | Waist | 400 | — | +80 | — | — | — | — | — | — | — | — |
| Peace Earring | Trinket | 350 | — | +30 | +40 | — | — | — | — | — | — | — |
| Blink Dagger *(weird)* | Trinket | 1400 | +6 | +6 | — | — | — | — | — | — | — | — |
| Donkey *(weird)* | Back | 3200 | — | — | — | — | — | — | — | — | — | — |
| Haste Trinket | Trinket | 900 | — | — | — | — | — | 6% | — | — | — | — |
| Gae Bolg *(weird)* | Weapon | 3000 | — | — | — | — | — | — | 18 | — | — | — |
| Masamune *(weird)* | Weapon | 3300 | +15 | — | — | — | — | — | — | 15% | — | — |
| Muramasa *(weird)* | Weapon | 3450 | +70 | — | — | — | — | — | — | — | — | — |
| Balance Ring *(weird)* | Ring | 2700 | — | — | — | *dynamic* | — | — | — | — | — | — |
| Empress Hairpin | Head | 1350 | — | — | +100 | — | — | 4% | — | — | — | — |
| Ninja Tekko | Hands | 1500 | +20 | — | — | — | +1.0 | — | — | — | — | — |
| Kite String | Trinket | 3333 | — | — | — | — | — | — | — | — | 4% | — |
| Luck of the Draw | Trinket | 2200 | — | — | — | — | — | — | — | — | — | +1/s |
| Wizard's Coat | Body | 1400 | — | — | +80 | +10 | — | 3% | — | — | — | — |
| Chain Leggings | Legs | 1100 | — | +70 | — | +14 | — | — | — | — | — | — |
| Boots of Winter | Feet | 1200 | — | — | +60 | — | +0.5 | — | — | — | — | — |
| Mage's Earring | Neck | 900 | — | — | +70 | — | — | — | — | — | — | — |
| Venerer's Belt | Waist | 1300 | +12 | — | — | — | — | — | — | — | 2% | — |
| Mikazuki *(weird)* | Weapon | 2400 | +34 | — | — | — | — | 5% | — | — | — | — |
| Dojigiri *(weird)* | Weapon | 2800 | +58 | — | — | — | +1.0 | — | — | — | — | — |
| Excalibur *(weird)* | Weapon | 4200 | +50 | +120 | — | +20 | — | — | — | — | — | — |

### Item mechanics beyond the stat columns

The table above only shows flat stat bonuses. Several items carry real coded behavior the
columns can't express — surfaced here explicitly rather than left implicit in code comments:

- **Blink Dagger** (`` ` `` active) — grants a short instant blink toward the cursor
  (`arena_use_blink`), on top of its +6 AD/+6 HP. The catalog's first active-ability item; unlike
  every other item, the code has to check "is *this specific* item equipped" by index rather than
  just summing stat fields.
- **Donkey** (`` ` `` active, zero flat stats — the "hidden ability" case) — two real, separate
  mechanics, both keyed off the same tilde key Blink Dagger uses (you equip one or the other, not
  both, since they share a Trinket/Back-adjacent active slot):
  - *Immortal's Fold* — an **automatic passive**, not player-triggered: the instant the wearer's
    HP crosses below 25% (`ARENA_DONKEY_FOLD_HP_FRACTION`), it grants a temporary damage floor
    plus a "fights back for you" window (4s, `ARENA_DONKEY_FOLD_MS`) that deals passive
    melee-range damage to nearby enemies, then goes on a real 30s cooldown before it can trigger
    again.
  - *Paper Glide* — the tilde-activated half: not an instant teleport like Blink Dagger, a
    longer, slower, longer-range traversal (real range 96 units vs. Blink Dagger's 12) with a
    genuinely long 2-minute cooldown, matching its FFXI-inspired identity as the bigger,
    slower-to-reset escape tool.
- **Haste Trinket** — 6% cooldown reduction (`bonus_cdr_pct`) applying to both ability cooldowns
  (Q/W/R) and the auto-attack cooldown. No flat stats at all; this *is* the item, not a bonus on
  top of one.
- **Gae Bolg** — 18 flat **true damage** per auto-attack, applied *after* armor is subtracted, not
  before — the catalog's first stat that bypasses armor entirely. A real counter-build against
  armor-stacking compositions, not just a bigger number.
- **Masamune** — 15% **lifesteal**: heals the wielder for that fraction of the *final*
  (post-armor) damage on a landed auto-attack. The catalog's first sustain/lifesteal mechanic.
  Thematically paired with Muramasa below (benevolent vs. cursed half of the same legendary-blade
  pairing) — a real build-around choice between the two, not two flavors of the same stat.
- **Muramasa** — no hidden mechanic; the most extreme stat-shape-only item in the catalog (+70 AD,
  genuinely zero of anything else) — Masamune's "cursed" counterpart by theme, not by code.
- **Balance Ring** — its armor bonus is **dynamic**, not a fixed number: computed live every tick
  as a function of the wearer's own missing-HP fraction (the more hurt you are, the more armor it
  grants), the catalog's first state-dependent stat. A rubber-band comeback mechanic in the same
  spirit as the synergy-decay system described under "Arena bot AI research program" below.
- **Empress Hairpin** — +100 MP plus 4% CDR (reuses Haste Trinket's own mechanic, not a
  duplicate implementation) — a caster/ability-spam support item, not just a bigger-MP Optical
  Hat.
- **Ninja Tekko** — plain flat stats (+20 AD, +1.0 move speed), no hidden mechanic — an
  assassin-shaped hybrid alternative to Battle Gloves' pure-AD Hands item.
- **Kite String** — +4% auto-attack range (`bonus_attack_range_pct`), specifically basic
  auto-attacks, not ability ranges. No flat stats at all — same "this *is* the item" shape as
  Haste Trinket, just for range instead of cooldown.
- **Luck of the Draw** — +1 flat mp/sec regen, but *only while in combat*
  (`bonus_mp_regen_combat`, added on top of the base in-combat regen rate). No flat stats
  otherwise — a real, modest ability-spam sustain pickup, not a build-defining spike.
- **Wizard's Coat / Chain Leggings / Boots of Winter / Mage's Earring / Venerer's Belt** — the
  GFD-inspired pass's five gap-fillers: a genuine second option in the Body/Legs/Feet/Neck/Waist
  slots (each previously had exactly one item). Every one is a real stat-shape *tradeoff* against
  that slot's original item, not a strict upgrade — e.g. Wizard's Coat trades Haubergeon's flat
  armor for mana + CDR%, Chain Leggings trades Iron Ram Trousers' armor for HP, Venerer's Belt
  trades Warwolf Belt's HP for AD + a small auto-attack-range bonus.
- **Mikazuki / Dojigiri / Excalibur** *(weird)* — three new flagship Weapon items, named after
  real legendary swords (two real-world Japanese National Treasures plus Excalibur itself), stat
  *shape* picked to reflect each blade's own real reputation: Mikazuki is a fast, cooldown-heavy
  skirmisher's weapon; Dojigiri pairs the GFD database's single highest raw-attack stat with real
  mobility, a "mobile duelist" glass cannon distinct from Muramasa's immobile one; Excalibur is a
  well-rounded AD/armor/HP blend at the catalog's new highest price — the new top of the power
  curve, not another one-note glass cannon.

### Hero passives

Beyond a hero's own Q/W/R kit, one hero currently carries a real, always-on **passive** — an
automatic behavior the player never presses a button for, same "surfaced explicitly, not left
implicit in code" discipline the item-mechanics section above uses:

- **Tree** — the map's own decorative jungle trees (`ARENA_OBSTACLE_TREE`, the same scattered
  scenery dotted around every lane) are, for this one hero only, a real interactive resource, not
  just scenery. Whenever Tree has no enemy hero within auto-attack range, and their own attack
  isn't on cooldown, they automatically auto-attack the *nearest* jungle tree instead — a real
  enemy hero always takes priority; this only fires when there's genuinely nothing else to fight.
  Each strike:
  - Deals **10 damage** to the tree's own hit points (`ARENA_TREE_PASSIVE_DAMAGE`) — trees start
    at **120 hp** (`ARENA_TREE_HP`) and passively regenerate **6 hp/second**
    (`ARENA_TREE_REGEN_PER_SEC`) whether or not anyone's hitting them, so a tree left alone
    fully recovers.
  - Heals Tree for **4 hp** (`ARENA_TREE_PASSIVE_HEAL_PER_HIT`), capped at their own max hp — the
    whole point of the passive: a jungle-camping Tree sustains off the scenery itself instead of
    needing to recall to base.
  - Runs on its own **1.2s cooldown** (`ARENA_TREE_PASSIVE_COOLDOWN_MS`, subject to the same CDR
    stat every other cooldown in this catalog respects), separate from — and gated the same way
    as — Tree's own real auto-attack-vs-hero cooldown, so it can't double-fire alongside a real
    fight.
  - Never destroys the tree — hp clamps at 0, but the obstacle itself, its collision, and its
    position are permanent. A tree is a renewable resource to camp near, not a kill target.
  - Triggers a client-side jiggle/squish hit-reaction on the tree's own canopy (not its trunk),
    purely cosmetic, mirroring the same squish animation a hero plays when they take a hit.

  Server-authoritative like every other real mechanic in this codebase: the strike is decided and
  applied inside the game server's own simulation tick (`arena_hero_tree_passive`,
  `packages/simulation/arena_game.c`), then synced to every connected client via each tree's own
  `obstacle_hp` field in the regular snapshot broadcast — a client never decides this locally.
  **PARENA-mod-driven**: the actual damage/heal application routes through a real compiled PARENA
  mod (`stdlib/redgarden/tree_passive_mod.prn` in the PARENA repo) rather than being inlined
  directly in the C simulation code, the same "PARENA mod is the trigger, host C does the
  mutation" split every mod-driven mechanic in this codebase uses.

  **Real bug, found and fixed 2026-08-25**: this passive was wired into the team-mode simulation
  tick (`arena_update_teams`) when it first shipped, but not into the separate 1v1 tick
  (`arena_update`) that solo practice/1v1 matches actually run through — so it silently never
  fired outside team-mode matches for a stretch of the same day it landed. Fixed and covered by a
  regression test that exercises the real top-level tick function directly, not just the passive
  in isolation, so a repeat of this specific class of gap fails loudly instead of shipping quiet.

### Suggested heroes for new players

26 heroes is a lot to pick from blind. These four cover the roster's main roles with the most
forgiving kits — no clone armies to manage (Tyler), no blink mind-games (Loki), no stealth timing
(Frog/Ghost/NOOR-1/Bacon+Puck) — just a straightforward kit you can read at a glance:

- **MnM, the Shapeshifting Crab** — *Tank.* Q is a melee poke that roots, W (Burrow) digs
  underground on a cooldown — untargetable and rooted in place for the duration, then erupts back
  up on the same spot for a small AoE hit — R makes you unkillable (HP can't drop below 1) for a
  few seconds. Walk up, root, survive — about as simple as a kit gets.
- **The Duck** — *Fighter/Assassin.* Q yanks the nearest enemy toward you and deals AD damage on
  impact. W (Smoke Bomb, S202-10) drops a real AoE cloud on yourself — anyone standing inside it
  can't be targeted by a hero outside it, arena's own honest analog for "vision-blocking" since
  no real fog-of-war system exists here. One or two buttons cover most of the kit.
- **Gary** — *Marksman.* The only hero with zero dash/blink/gap-closer in his whole kit — he just
  aims and shoots from a stationary position, no positioning tech to learn beyond "stand at
  range." The easiest hero to understand a MOBA marksman role through.
- **He Xiangu** — *Support/Sustain.* Every ability heals — herself on Q (a ranged bolt that heals
  off a fraction of the damage it deals), a free regen toggle on W, an ally heal-zone on R. No
  damage combos to time, no enemies to predict; just keep the heals up.

## Current Status (2026-08-26)

See `NORTHSTAR.md` for the full, up-to-date direction — this README's "Acceptance Criteria" and
"Full Technical Design Document" sections below are the original design capture and are not all
built yet. What's actually real, right now:

- **VS0 (bot-vs-bot matches)** and **VS1 (online play, matchmaking, accounts)** are both validated
  end to end: `scripts/test_10_bots.sh` boots a matchmaker + 10 headless bots, confirms 5
  concurrent matches spawn and connect, and survives 10s of sustained play with zero crashes.
- **Accounts**: connect-ticket auth (HMAC-SHA256, same scheme as sibling repo shankpit-460) — see
  `packages/common/hmac_sha256.h`. `apps/server` verifies tickets on connect, fails closed without
  `REDGARDEN_TICKET_SECRET`.
- **Matchmaking**: `apps/matchmaker` — this simulation is one match per process by design, so
  matchmaking means pairing queued clients and spawning a dedicated `red_garden_server --port <N>`
  per match. **R&D vs. Stable deployment split (2026-08-10):** this checkout (`:7778`/`:7779`
  matchmakers) is the fast-iteration R&D target and can break; a separate checkout,
  `redgarden-stable`, serves GoblinFoxDragon's Battlegrounds (`:8778`) and is only promoted
  manually — see `CLAUDE.md`'s "Deployments" table for the full split.
- **Content (not yet wired into code)**: `docs/HEROES_VS0.md` (hero kits, including TYLER as an
  exact reskin of DOTA's classic Meepo) and `docs/CONSUMABLES_AND_COOKING.md` (item/consumable
  names, cooking/crafting direction).
- **Not yet built**: `apps/lobby` (the real SDL2/OpenGL rendered client) builds, but isn't wired
  into the matchmaker/ticket flow above; no packaged/distributable client exists yet.

### Jungle Camps — Four Heavenly Kings (all milestones done)

NORTHSTAR §8/Jungle Camps: one boss-scale creep camp per compass direction (N/S/E/W), silent
until 1:00, killable by either team, each granting a distinct team- or individual-scoped buff —
East/Music (team-viral Catchy Song, outlives individual death via a respawn relay),
South/Growth (individual stacking Bloodroar), West/All-Seeing (team-wide Farsight + bonus Flow
on jungle kills), North/Wealth (proximity-aura Bulwark, armor + gold trickle to nearby allies).
All 5 real milestones (buffs, anti-stall §3.4 lane-march escalation, King respawn timer) are done
in both this repo and, as of 2026-08-10, ported into GoblinFoxDragon's own
`apps2/battlegrounds_gui` fork via a real 3-way merge (Apple #12868).

### PARENA-mod-driven hero mechanics (2026-08-25/26)

Real, live PARENA mods now compile into REDGARDEN's own C binaries and drive real gameplay —
"the mod is the trigger, host C does the real work" — see `EMILY/BACKLOG.md` S202-08/10/14/22/27
and `PARENA/stdlib/redgarden/` for the source:

- **Tree passive** (`tree_passive_mod.prn`) — The Tree auto-attacks nearby decorative jungle
  trees for a slow self-heal, giving the map's own scenery a real interaction for the one hero
  whose kit fits it.
- **Build templates** (`build_template_mod.prn`) — named, ordered item presets (Bruiser/Assassin/
  Caster) that auto-buy from the shop in cheapest-affordable-first order; manual item-by-item
  purchase is unchanged.
- **Item curriculum** (`item_curriculum_mod.prn`) — a real generation primitive (NORTHSTAR
  §26.3.2) that blends two catalog items' stats into a new, runtime-mutable item slot — the v0
  autocurriculum-tunes-the-meta scope expansion's own generation half.
- **Duck's Smoke Bomb** (`duck_smoke_bomb_mod.prn`, S202-10) — Duck's W, a real AoE cloud that
  blocks targeting from outside it (see "How to Play" above).
- **Body blocking** (S202-27, pure engine mechanic, no PARENA mod) — real hero-hero collision:
  heroes can no longer walk through each other (allies included, matching real MOBA convention),
  skipped during Paper Glide the same way terrain collision already was.

### Arena bot AI research program (NORTHSTAR §25-28)

The multi-agent RL research thread — role discovery, synergy decay, commander/soldier hierarchy,
autocurriculum, cross-game transfer — lives and iterates here. Real results so far, all via
`scripts/rl_train_team.py`/`rl_env_team.py`:

- **Role discovery prerequisite**: `sim_get_obs_team` appends a team-size-long agent-identity
  one-hot to each agent's observation, the minimal signal a shared policy needs to differentiate
  behavior by slot at all. The full learned-embedding module (ROMA/RODE-style) is unbuilt.
- **Synergy decay (§25.3)**: a live comeback mechanic — team cohesion tier re-rolls every 8s,
  weighted toward more decay the further ahead a team is, scaling a small ambient CDR/move-speed
  bonus.
- **Commander posture, first step (§26.3)**: `commander_posture_multiplier()` scales bot
  tower-siege patience by the live resource race (ahead → patient, behind → aggressive) — a
  rule-based first step, not yet the full learned hierarchical version.
- **Noisy-gestalt training**: 500K timesteps, alternating Johnny/Spike reward schedule, final
  eval 20W/0L/0D (100%) vs. the fixed heuristic.
- **PFSP autocurriculum training**: a separate real end-to-end 500K-timestep run
  (`--autocurriculum`), opponent-pool sampling against past checkpoints instead of one fixed
  heuristic. Final eval 15W/5L/0D (75%) vs. the same fixed heuristic — honestly lower than the
  noisy-gestalt run, plausibly because training time was split across a growing opponent pool
  instead of over-specializing on the one heuristic it's evaluated against. Whether that means
  "more general" or "genuinely weaker" isn't resolved yet — the real open next step is a
  pool-based eval comparing both final policies against a shared varied opponent set, not built.
- **Live consumer**: the noisy-gestalt checkpoint is wired into `apps/arena_bot`'s
  `team_rl_engage_nudge()`, gated to a real, separate **3v3 queue** (`:7780`) — hard no-op in the
  existing 10v10 pool since the checkpoint is a fixed `team_size=3` shape.
- **Open, founder-flagged decision points, not yet built**: a draft-phase commander agent
  (blocked on ban-phase systems not existing yet); v0-autocurriculum tuning the game via new
  items (a real scope expansion past §25.4's original boundary); a repeatable GPT-2-assisted
  item-name-generation pipeline (prompt `gpt2-alpine-c` with 2 seed item names, capture
  candidates) to replace hand-picking FFXI names one at a time.

### Build & Run

```bash
bash scripts/build.sh              # builds red_garden_server, _bot, _lobby, _matchmaker into build/
bash scripts/test_10_bots.sh        # VS0/VS1 validation: matchmaker + 10 headless bots
bash scripts/test_10_bots.sh 4      # or pass a different bot count (must be even)
```

`REDGARDEN_TICKET_SECRET` must be set for any client to connect (fails closed otherwise) — the
test script sets a default automatically.

### Arena Bot AI — Training on Colab

NORTHSTAR §18's unsupervised-pretraining stage (S170-194/195/220): a real, working pipeline that
turns actual match play into a trained checkpoint AND a C-embeddable weight file, no local GPU
needed. **You do NOT upload the repo to Drive** — the notebook clones REDGARDEN straight from
GitHub inside Colab. Drive is only used to hold the training corpus (input), the SSH key (for
the git-sync step), and the full HF checkpoint (output).

1. **Collect real match data.** Play or run some bot matches (`scripts/test_10_bots.sh`, or a
   real 10v10 via `scripts/launch_arena_pools.sh`) — every live `apps/arena_server` match writes
   `var/corpus/arena-corpus-<port>-<ts>.jsonl` automatically (`packages/simulation/
   arena_ai_bridge.c`'s `arena_corpus_record()`, wired into the server's own tick loop). More
   matches, more corpus.
2. **Aggregate it locally:**
   ```bash
   python3 scripts/build_ai_corpus.py --min-records 1000
   ```
   Combines every `var/corpus/arena-corpus-*.jsonl` into `var/corpus/combined.jsonl`.
3. **Upload just that one file to Drive**, at `MyDrive/redgarden-training/redgarden-corpus.jsonl`
   (the path `scripts/colab_train.py`'s defaults expect — override via the `DRIVE_FOLDER` env var
   in the notebook's own bootstrap cell if you'd rather use a different Drive layout).
4. **(Optional, for git-sync) Put a real SSH deploy/personal key with push access to this repo
   at `MyDrive/.ssh/id_ed25519`** (override the filename via `REDGARDEN_DRIVE_SSH_KEY`). No key
   there → training and the C weight export still run, the git-push step just skips itself.
5. **Open the notebook straight from GitHub** — in Colab: File → Open notebook → GitHub tab →
   `emilyspringerton/REDGARDEN` → `notebooks/redgarden_gpt2_pretrain_colab.ipynb`. Run the single
   bootstrap cell: it mounts Drive (approve the OAuth prompt), clones/pulls REDGARDEN fresh, and
   runs `scripts/colab_train.py` — all real training logic lives in that script, in git, so a
   future change ships as a commit and the same notebook cell just picks it up next run.
6. **Result**: three artifacts. `checkpoint-unsupervised-pretrain.tar.gz` (the full HF
   checkpoint — the STARTING WEIGHTS for §12 Phase E's later supervised, NORN-graded fine-tune,
   not a finished playing policy by itself) saved to Drive; `weights/redgarden-arena-bot.bin`
   (the flat binary format `packages/common/gpt2_infer.c`'s ported C inference engine loads via
   `gpt2_model_load_weights`) committed and pushed straight to `origin/main` if step 4's SSH key
   was present, otherwise also just saved to Drive alongside the checkpoint.

**Why the model is small, trained from scratch, not a GPT-2-small fine-tune (S170-220):**
GPT-2-small's real weights are ~497MB as raw float32 — too large to reasonably commit to this
repo every training run, and almost certainly too slow for real-time CPU inference inside a game
loop. `scripts/colab_train.py`'s default config (4 layers, 128 dim, 4 heads — override via
`--n-layer`/`--n-embd`/`--n-head`/`--n-ctx`) is small enough for both, at the real cost of
losing GPT-2's own public-English pretraining as a warm start (a from-scratch small model can't
load a 768-dim/12-layer checkpoint's weights into a 128-dim/4-layer shape).

`packages/common/gpt2_infer.c`/`.h` is a verbatim port of the sibling `gpt2-alpine-c` repo's own
`src/gpt2.c` inference engine (fully parameterized by n_vocab/n_ctx/n_embd/n_layer/n_head, so the
same file serves both repos' very different model sizes) — see `tests/test_gpt2_infer.c` for a
headless smoke test against synthetic weights, and this pass's own commit message for an
end-to-end verification (a real small model, exported by `scripts/colab_train.py`'s own export
function, successfully loaded and forward-passed through this exact C engine).

**Not yet built:** wiring this inference engine into the LIVE bot AI decision loop
(`arena_game.c`'s `bot_cast_kit_if_ready` or a generalization of it) — this pipeline trains,
exports, and syncs the weights; nothing in a real match actually calls them yet. Real, scoped
future work — flagged here, not faked.

---


## RED GARDEN — Original Vertical Slice Design Capture

*Kept as history per "Current Status" above — the design doc this repo started from, not all
built yet. De-duplicated 2026-08-13 (this file had accidentally carried two copies of the same
content, one unformatted, one formatted, since an earlier pass).*

## Acceptance Criteria (Vertical Slice)

- Render a 20×20 isometric grid with visible cell boundaries.
- Run a cellular-automata tick every 2 seconds for grid state updates.
- Support grid states: Neutral, Player, Enemy, Corrupted.
- Place static Frontier Villages during map generation.
- Provide a card hand UI with 5 slots at the bottom of the screen.
- Implement mouse drag-and-drop to place cards onto valid grid cells.
- Show a ghost preview and valid/invalid placement feedback while dragging.
- Enforce influence cost and cooldown per card when placing.
- Include exactly 4 playable cards: Militia, Scout, Swarmlings, Outpost.
- Implement autonomous unit behaviors for Militia, Scout, and Swarmlings.
- Implement Outpost as a spawner that produces Militia on a timer.
- Implement a minimal Dominion tech tree with 2 tiers:
  - Tier 1: Militia +20 HP.
  - Tier 2: Outpost spawns Militia at 2× speed.
- Implement a win condition: hold 60% of cells for 60 seconds or destroy the enemy Outpost.

## Full Technical Design Document

### 1. Core Concept Refinement

**What this game is:** A deck-building ecosystem RTS where you seed autonomous agents into a living cellular automaton battlefield. You do not control units—you introduce pressure and watch systems collide.

**Mental model**
- Clash Royale card deployment + hand management.
- Conway's Game of Life emergent map behavior.
- Command & Conquer strategic pacing.
- League of Legends objective control.
- Diablo II creature ecology.

**Core loop**
1. Draw cards from an evolving deck.
2. Drag-drop spawners/units onto valid grid cells.
3. Watch autonomous behaviors create frontlines.
4. Tech tree upgrades mutate cards (not raw unit control).
5. Capture objectives to evolve deck mid-match.

### 2. Art Direction: Low-Poly Imperative Brutalism

**Visual pillars**
- C99 + SDL2 + OpenGL immediate mode.
- No shaders, no textures, flat colors, vertex lighting only.
- Everything under 100 triangles per entity.

**Neon Brutalism style**
- Solid matte black cores (RGB 0.02, 0.02, 0.02).
- Wireframe neon cages with procedural color.
- High-contrast silhouettes for instant readability.
- Geometric purity: cubes, wedges, capsules.

**Color language**
- Player Units: Hot Pink (1.0, 0.0, 0.8)
- Enemy Units: Acid Green (0.0, 1.0, 0.4)
- Neutral Towns: Cyan (0.0, 1.0, 1.0)
- Pillager Compounds: Blood Red (1.0, 0.0, 0.0)
- Terrain Grid: Deep Blue (0.0, 0.2, 0.4)
- Tech Nodes: Electric Yellow (1.0, 1.0, 0.0)

**Camera**
- Fixed high-tilt orthographic (Age of Empires II style).
- No rotation, slight zoom only.
- Grid always visible.

### 3. The Living Grid System

**Cell struct**
```c
typedef struct {
    int state; // NEUTRAL=0, PLAYER=1, ENEMY=2, CORRUPTED=3
    int population; // 0-255
    int alignment_pressure; // -127 to +127
    int growth_rate; // -10 to +10
    float stability; // 0.0 to 1.0
} GridCell;
```

**Update rules (every 2 seconds)**
- If 3+ neighbors share alignment → convert (if pressure > 50).
- If population > 200 → split to adjacent cells.
- If population < 20 for 5 ticks → revert to neutral.
- If 4+ corrupted neighbors → become corrupted.

**Visual feedback**
- Cell color intensity = population density.
- Pulsing borders = conversion in progress.
- Cracks for overpopulation.
- Tendrils for corruption.

### 4. Card UI System (Clash Royale DNA)

**Layout**
- Bottom-center: 5-card active hand.
- Top-left: tech tree indicator.
- Bottom strip: resource + deck preview.

**Card anatomy**
```c
typedef struct {
    int card_id;
    int cost; // Influence points
    float cooldown; // 0.0 = ready, 1.0 = just used
    int tech_level; // 0-3, affects stats
    char name[32];
    float color_r, color_g, color_b; // Neon accent
} Card;
```

**Mouse interaction**
```c
typedef struct {
    float world_x, world_z;
    int grid_x, grid_z;
    int dragging_card_idx; // -1 = none
    int hover_cell;
} MouseState;
```

**Valid placement**
```c
int is_valid_spawn(GridCell *cell, Card *card) {
    if (cell->state == CORRUPTED) return 0;
    if (card->cost > player_influence) return 0;
    if (cell->population > 200) return 0;
    return 1;
}
```

### 5. Entity Roster (16 Units + 8 Structures)

#### Units

**Tier 1**
- Militia: frontline bruiser, forms shield walls.
- Scout: ranged kiter, long aggro.
- Swarmlings: fast horde, weakest-target focus.
- Ravager: objective breaker, ignores units initially.

**Tier 2**
- Hexbound: debuffer, spreads corruption.
- Verdant Behemoth: slow anchor, stabilizes cells.
- Shade Stalker: stealth assassin, backstab.
- Pyromancer: AoE caster, splash + burn.

**Tier 3**
- Warden: defensive specialist, reflect damage.
- Tide Caller: healer/support, resurrection path.
- Siege Golem: tank with siege mode.
- Void Reaver: self-draining, explosive death.

**Tier 4**
- Archon: hero unit, buffs + ultimate.
- Chaos Spawn: random outcomes, volatility.
- Wraith King: revive loops, summons militia.
- Dragon: map boss, unlockable via quest.

#### Structures

1. Outpost: Militia spawner, alignment pressure.
2. Mana Well: influence generator, town-adjacent.
3. Corruption Spire: Hexbound spawner + corruption.
4. Grove Heart: Behemoth spawner + healing.
5. Siege Workshop: Golem spawner + repairs.
6. Shadow Sanctum: Stalker spawner + stealth.
7. Inferno Tower: AoE defense.
8. Nexus Core: win-condition structure.

### 6. NPC Entities (Third Faction)

**Towns**
- Frontier Village: peasants + easy flip.
- Walled Hamlet: guards + hard flip.
- Jungle Enclave: hunters + expansion.
- Blighted Settlement: cultists + instability.

**Creep camps**
- Goblin Warren: 3 goblins, loot influence.
- Orc Stronghold: orcs, unlocks Ravager.
- Dragon Roost: neutral dragon, major objective.

**Pillager compounds**
- Corruption Node: spreads corruption, spawns pillagers.
- Pillagers: Marauder, Destroyer, Corruptor.

### 7. Tech Tree System

**Design rules**
- 3 vertical paths, pick 2 per match.
- Tech upgrades cards (not global stats).
- Unlocks via objectives, not just time.

**Doctrines**

**Dominion**
- T1: Militia +20 HP, Outpost 2× spawn, +10% structure HP.
- T2: Shield Bash, Barracks upgrade, Siege Workshop unlocked.
- T3: Captain upgrade, Nexus Core unlocked, +50% structure HP.
- T4: Archon unlocked, Unbreakable buff.

**Symbiosis**
- T1: Scout +2 vision, faster cell growth, towns never defect.
- T2: Behemoth regen, Grove Heart unlocked, camps neutral.
- T3: Behemoth spawns Militia, towns auto-upgrade, permanent conversions.
- T4: World Tree unlocked, territory heal, hostile environment for enemies.

**Corruption**
- T1: Hexbound radius +1, faster corruption, pillager hijack.
- T2: Corruption Spire + Void Reaver unlocked, structure corruption damage.
- T3: Chaos Spawn unlocked, corrupted cell explosions, volatile deaths.
- T4: Cataclysm Beacon unlocked, permanent corruption, zombie effect.

**Quest unlocks**
- First Blood → Pyromancer
- Fortify → Warden
- Treasure Hunter → +5 hand size
- Dragon Slayer → Dragon card + influence cap
- Ecosystem Collapse → Chaos Spawn
- Necromancer → Wraith King

### 8. Game Modes

1. Skirmish (1v1 Ranked): 30 min, destroy Nexus or hold 50% map.
2. Survival (Co-op PvE): survive 20 waves.
3. Ecosystem War (2v2): shared structures + dragon objective.
4. Chaos Mode (FFA): corruption expands, last player standing.

### 9. UI Specification

**Main layout**
- Top bar: tech tier + influence.
- Center: 3D isometric battlefield.
- Bottom: 5-card hand + deck preview.
- Cooldowns shown as radial fills.

### 10. Conway Integration

```c
void update_town_ecology() {
    for (int i = 0; i < town_count; i++) {
        Town *t = &towns[i];
        int friend_count = 0;
        int enemy_count = 0;
        int neutral_count = 0;
        for (int j = 0; j < town_count; j++) {
            if (i == j) continue;
            float dist = distance(t->x, t->z, towns[j].x, towns[j].z);
            if (dist < 3 * CELL_SIZE) {
                if (towns[j].alignment == t->alignment) friend_count++;
                else if (towns[j].alignment != NEUTRAL) enemy_count++;
                else neutral_count++;
            }
        }
        if (friend_count == 0 && enemy_count >= 2) t->morale -= 20;
        if (friend_count >= 3 && neutral_count >= 1) {
            spawn_child_town(t, find_adjacent_empty_cell(t->x, t->z));
        }
        if (t->morale <= 0) convert_to_ruins(t);
    }
}
```

### 11. Networking Adaptation

**New packets**
- PACKET_CARD_PLAY: client → server card placement.
- PACKET_TECH_UNLOCK: server → clients tech progression.
- PACKET_QUEST_COMPLETE: server → client quest rewards.

```c
typedef struct {
    unsigned char card_id;
    int grid_x, grid_z;
    unsigned int timestamp;
} CardPlayCmd;

typedef struct {
    unsigned char quest_id;
    unsigned char reward_card_id;
    int bonus_influence;
} QuestComplete;
```

**Server simulation**
- Authoritative 60 TPS.
- Validates card placement.
- Broadcasts entity snapshots + cell states.
- Handles NPC AI (towns, creeps, pillagers).

### 12. Fork Adaptation Guide

**Keep**
- SDL2 window management.
- OpenGL immediate mode rendering.
- Network stack (UDP sockets).

**Remove**
- FPS camera.
- Weapon system.
- Jump/crouch physics.

**Add**
- Orthographic/isometric camera.
- Grid-based spatial partition.
- Card deck system.
- Mouse → world raycast.
- Cellular automata updater (2s tick).
- Quest tracker + tech tree.

**Key files**
- `card_system.h`
- `grid.h`
- `entity_behaviors.h`
- `quest_system.h`
- `tech_tree.h`
- `mouse_input.h`

### 13. Emergence Examples

1. **The Cascade**: corruption + pillagers + pyromancer → chain wipe.
2. **The Living Wall**: Grove Hearts create a pushing defensive front.
3. **The Dragon Gambit**: last-hit dragon swings a late-game siege.

### 14. Visual Identity Summary

**Palette**
- Deep Space Blue: `#050514`
- Hot Pink: `#FF00CC`
- Acid Green: `#00FF66`
- Cyan: `#00FFFF`
- Blood Red: `#FF0000`
- Electric Yellow: `#FFFF00`
- Matte Black: `#050505`

**Silhouettes**
- Bruisers: cubes + wide stance.
- Skirmishers: tall capsules.
- Swarms: triangular shards.
- Casters: spheres + floaters.
- Tanks: stacked cubes.
- Support: ribbon geometry.

**Typography**
- Wireframe vector font (2px line thickness).
- Cyan for info, yellow for warnings, red for errors.
