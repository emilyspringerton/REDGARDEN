# RED GARDEN — VS0 Hero Kits & Starting Item Roster

*Written: 2026-07-23 | Status: content pass, not implemented in code yet*

Per founder direction: this is a direct content pass on top of the hero queue in `NORTHSTAR.md`
§7 — no ability framework/engine work first, just concrete kits. Stat system leans heavily on
LoL/DOTA-familiar terms (attack damage, ability power, armor, magic resist, penetration, CDR,
lifesteal) on purpose — the roster already has enough that's unique; a totally foreign stat
system on top of that would be too much. Ability descriptions are written loosely enough to
interact with each other and with the living grid (see the RED GARDEN-specific passives below)
rather than being cleanly siloed — the LoL-style "spaghetti interactions" are a feature, not a
bug, of a healthy kit roster.

Shared archetypes across the roster (a hero can straddle two): **Fighter, Mage, Assassin, Tank,
Support, Indirect-Control** (the last one is a deliberate roster feature per NORTHSTAR §7's
"indirect-control archetypes" note — some heroes aren't directly commanded at all).

---

## Stat Legend

| Stat | Meaning |
|---|---|
| AD | Attack Damage — physical hit/ability scaling |
| AP | Ability Power — magic ability scaling |
| Armor | Reduces incoming physical damage |
| MR | Magic Resist — reduces incoming magic damage |
| Pen | Penetration — ignores a flat or % amount of the target's Armor/MR |
| CDR | Cooldown Reduction |
| AS | Attack Speed |
| MS | Movement Speed |
| HP / HP5 | Health / Health regen per 5s |

---

## Hero Kits

### The Donkey (S, #38 — Middle Kingdom Heirs) — **Indirect-Control**

Folds flat like paper when not needed, unfolds instantly when it is. The Donkey is never
directly commanded — it rides folded (inert, untargetable, no collision) alongside its owner
until a trigger condition fires, then unfolds for a short window.

- **Passive — Immortal's Fold**: Folded by default. Unfolds automatically for 4s whenever the
  owner drops below 25% HP, immediately granting the owner a flat damage shield and a burst of MS
  — then re-folds. Cannot be targeted, killed, or interacted with while folded.
- **W — Load Bearing**: While unfolded, the Donkey can carry one ally's dropped item/resource
  drop for the rest of the fold cycle, delivering it to the owner on re-fold.
- **Passive interaction**: stacks oddly with burst/execute effects aimed at the owner — an
  execute that would've killed the owner can be shielded out entirely if the Fold triggers first,
  making the Donkey a strong answer to assassin dive comps.

### The Duck ("A Duck, Reportedly Telekinetic", S, #103) — **Fighter/Assassin**

- **Q — Telekinetic Yank**: Pulls the nearest enemy unit toward the Duck, dealing AD-scaled
  damage on impact.
- **W — Government Clearance**: Passive — the Duck takes reduced damage from towers/objective
  structures ("technically authorized to be here"), and objectives take bonus damage from it.
- **E — Chosen One**: On landing the killing blow on a hero, briefly gain AD + MS ("of course it
  was me").
- **R — Total Telekinesis**: Channel, then violently yank every nearby enemy unit into a cluster
  at the Duck's position, dealing AoE AD damage on landing — combos hard with any AoE mage kit
  standing nearby (an intentional spaghetti interaction, not a bug).

### The Unicorn ("Allegedly a Robot", S, #104) — **Tank**

- **Passive — Chassis Claim**: Bonus flat Armor and MR ("I'm a robot, technically"), plus immunity
  to the first crowd-control effect applied to it each fight ("robots don't feel that").
- **Q — Diagnostic Charge**: Dash forward, dealing AD damage and briefly revealing stealthed units
  hit ("running a scan").
- **W — Spaghetti Vent**: Passive — while eating (channeling this ability does nothing visible),
  regenerates HP5; can be cast defensively to bait enemies into wasting an engage on a Unicorn
  that's just standing there regenerating.
- **R — Full Disclosure**: Taunts all nearby enemies to attack the Unicorn for 3s, during which its
  Armor/MR are doubled.

### The Ghost ("Not Really a Ghost, I'm an Alien", S, #105) — **Mage/Support**

- **Passive — Mid-Piano**: Every few seconds, the Ghost's next ability cast is silent (undodgeable
  — no visible cast animation), themed as it never actually stops playing piano.
- **Q — Alien Frequency**: AP-scaled magic damage skillshot; enemies hit are Silenced briefly.
- **W — Not a Ghost**: Become intangible (untargetable, can pass through units/terrain) for 1.5s.
- **R — Recital**: Channel an AoE zone that deals AP damage over time and heals allies standing in
  it — the same zone, same tick, opposite effect depending on team, a deliberately spaghetti
  double-edged ultimate.

### The Frog (Keeper of an Untold Secret, S, #106) — **Support/Indirect-Control**

- **Passive — Never Told Anyone**: The Frog's abilities have no visible cooldown UI for enemies
  (real cooldowns still apply) — bluffing is part of the kit.
- **Q — Loop Back**: Rewind the Frog (and only the Frog) to its position/HP from 3s ago.
- **W — Borrowed Time**: Target ally's next ability has its cooldown refunded on cast.
- **R — The Secret**: The Frog vanishes from the map entirely (no presence, can't be targeted or
  seen) for 5s, then reappears at any visited location this game — a hero that opts out of a fight
  entirely, on demand, rather than escaping it.

### The Tree (Keeper of the Universe's Greatest Secret, in French, S, #107) — **Tank — RED GARDEN passive**

- **Passive — Root Network (map interaction)**: While the Tree stands on a cell, that cell's
  `alignment_pressure` (see §3's `GridCell` struct) drifts toward the Tree owner's side over time
  — a slow, standing-still form of territory conversion distinct from card-deployed pressure.
  Rewards planting the Tree and defending it rather than roaming.
- **Q — Vine Lash**: AoE root in a cone in front of the Tree.
- **W — Untranslated**: Passive — abilities used near the Tree that would normally be interrupted
  by CC get one guaranteed cast per fight instead ("the secret" protects nearby allies once).
- **R — Grand Secret**: The Tree roots itself permanently in place, becoming immune to
  displacement and gaining massive HP/Armor, but cannot move again until the ability is
  recast (min. 8s) — a full commitment to holding ground, which pairs directly with the passive.

### The Pizza (On Fire, Uninvestigated, S, #108) — **Mage — RED GARDEN passive**

- **Passive — Uninvestigated Fire**: The Pizza is permanently on fire and immune to its own burn
  damage. Enemies near it take AP-scaled burn damage over time; the cell the Pizza currently
  stands on gains `CORRUPTED`-leaning pressure the longer it stays — the Pizza is, mechanically,
  a slow-moving corruption source nobody in the fiction ever notices.
  fire) get one guaranteed cast per fight instead.
- **Q — Nobody Checked**: Throw a burning slice; deals AP damage and leaves a burning patch on the
  ground.
- **W — I Am The Chosen One**: Passive — the Pizza's declared "ultimate form" (visual only) makes
  it look like it's about to cast something huge; enemies who flinch/reposition in response take
  no actual consequence either way (a pure mind-game ability with a real bait use case).
- **R — Nobody Ever Checks**: The Pizza can't be reduced below 1 HP for 4s (true to the lore — it's
  on fire this entire time and nobody has ever confirmed it's actually hurt by that).

### The Retrieval Cart (S, #10 — Jiangshi Syndicate) — **Indirect-Control (world event, not a unit)**

Matches NORTHSTAR §7's framing exactly: not a piloted hero. It's a recurring, unrequested
delivery event attached to a lane.

- **Passive only — Already Waiting**: On an irregular timer (not player-triggered), the Cart
  appears at a random point along a lane with a random buff/resource, and grants it to whichever
  allied unit is nearest when it arrives — no requester was ever logged, none is needed.
- **RED GARDEN passive (map interaction)**: Each delivery nudges the cell it appears on slightly
  toward the delivering side's `alignment_pressure`, modeling a passive supply line quietly
  converting territory over the course of a match.
- Design note: has no active-use kit at all by design — the entire hook is "you don't control
  this, you just benefit from it on its own schedule."

### Buer, "Doc Wheel" (S, #25 — Goetia Court) — **Support — RED GARDEN passive**

Played mundane on purpose, per the lore: no combat power, no flashy ultimate. The entire kit is
being the correct ally to have nearby.

- **Passive — Extremely Good At Medicine**: Doc Wheel's heals scale up the lower the target's
  current HP% is (best-in-class emergency healer, worst-in-class throughput healer on full-HP
  targets).
- **Q — Bedside Manner**: Single-target heal + cleanses one debuff.
- **W — House Call**: Move to an ally's location instantly, on a long cooldown ("always shows up").
- **RED GARDEN passive (map interaction)**: Whenever Doc Wheel heals a unit standing on a
  `CORRUPTED` cell, that cell's corruption pressure decays slightly faster — thematically, good
  medicine calms the ground it's practiced on.
- **R — No Combat Power, As Advertised**: Doc Wheel has no ultimate that deals damage. Instead,
  this fully removes all debuffs and grants max-HP-scaled shields to every ally in a wide radius —
  a pure defensive teamwide panic button, nothing else.

### TYLER — **Fighter/Assassin — exact reskin of DOTA's OG Meepo**

Founder-requested: an exact copy of Meepo's classic kit, including the original (pre-rework,
unforgiving) "OG" clone-death rule, reskinned as TYLER rather than renamed into something softer.

- **Q — Earthbind**: Fires a net at a target area; any enemy hit is rooted and treated as a bigger
  hitbox for a few seconds (classic setup for the blink-strike below).
- **W — Poof**: After a short delay, TYLER and every active clone teleport to the target point,
  dealing AoE AD damage both where each one left from and where each one lands. Chainable with
  itself across clones — a full-team dive tool in the hands of a good player.
  Yes, this stacks with itself across every clone TYLER is currently split into.
- **E — Geostrike (passive)**: Every melee attack from TYLER or any clone reduces the target's
  Armor and applies a stacking poison DoT.
- **R — Divided We Stand**: Splits TYLER into an additional clone (up to 5 total), each with a
  percentage of TYLER's stats and sharing TYLER's items and cooldowns. **OG rule, exactly as
  requested**: all clones share a single pool of fate — if any one TYLER dies, every TYLER dies,
  no exceptions. High-risk, high-reward, exactly like the original.

---

## Starting Item Roster (VS0)

LoL Season 3 is the explicit stylistic northstar here — not its item *names*, its item *stat-line
archetypes* and the distinct build paths they enable (crit carry, on-hit carry, burst mage, DPS/
utility mage, tank initiator, tank/MR, lifesteal duelist, anti-tank penetration, support aura).
Mixed in, not copy-pasted 1:1.

| Item | Archetype (LoL/DOTA analogue) | Stat line |
|---|---|---|
| Seedling Charm | Cheap starter stat-stick (Doran's-line) | +AD or +AP (choose on purchase), +HP, minor HP5 |
| Bramble Fang | AD crit carry core (Infinity Edge-line) | +AD, +Crit Chance, +Crit Damage |
| Thornrender | On-hit AD carry (Blade of the Ruined King-line) | +AD, +AS, on-hit: bonus %-current-HP damage + slow |
| Bloomheart Core | Burst-AP mage core (Rabadon's-line) | Large +AP, % bonus to total AP |
| Wanecall Grimoire | DPS/utility mage (Athene's/Morello-line) | +AP, +CDR, applies Grievous Wounds (reduced healing) on ability hit |
| Ironbark Plate | Tank initiator (Sunfire/Randuin's-line) | +HP, +Armor, passive AoE damage aura, slows attackers |
| Willowveil | Tank/MR (Banshee's-line) | +MR, +HP, one-time incoming-spell block (recharges) |
| Vampiric Bloom | Lifesteal duelist (Bloodthirster-line) | +AD, +Lifesteal, overheal converts to a temporary shield |
| Splinterfang | Anti-tank physical penetration (Last Whisper-line) | +AD, % Armor Penetration |
| Hollow Needle | Anti-tank magic penetration (Void Staff-line) | +AP, % Magic Penetration |
| Rootrunner Treads | Mobility tier (Boots-line) | Flat +MS, minor situational bonus (choose on upgrade) |
| Gardener's Ward | Support aura (Locket/Redemption-line) | +HP, active: AoE shield + heal to allies, long cooldown |

---

## Open / Deferred

- No numeric balance pass — every number above is a placeholder to be tuned once these are wired
  into `packages/simulation/local_game.c`.
- No card-hand integration decided yet: whether heroes replace or sit alongside the existing
  4-card roster (Militia/Scout/Swarmlings/Outpost) is a separate design question.
- Remaining compendium entries beyond this queue are out of scope for this pass.
