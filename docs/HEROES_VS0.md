# RED GARDEN — VS0 Hero Kits & Starting Item Roster

*Written: 2026-07-23 | Status: content pass, not implemented in code yet*

See `docs/BACKSTORY.md` for why these nine specific characters are a roster at all — they're
Tyler's motorcycle gang, not nine strangers a deck-builder happened to draft.

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
- **Q — Paper Glide** (2026-07-24, founder direction: "launching itself into the air while folding
  into a paper airplane... movement mobility and escape... fly over trees etc"): while unfolded,
  launches itself into the air and refolds mid-launch into a paper-airplane shape, gliding a long
  distance to carry the owner clear of immediate danger before landing and re-folding. Airborne for
  the glide's duration — flies *over* terrain and ground-based obstacles rather than around them,
  and is untargetable by ground-based crowd control for the same window (nothing can root or catch
  what's currently in the air). Same Indirect-Control identity as the rest of the kit: this isn't a
  player-cast ability with its own keybind, it's a second auto-trigger condition alongside the
  Immortal's Fold passive — fires when the owner needs distance fast (a bad engage, a closing
  gap-closer), not on command.
- **Passive interaction**: stacks oddly with burst/execute effects aimed at the owner — an
  execute that would've killed the owner can be shielded out entirely if the Fold triggers first,
  making the Donkey a strong answer to assassin dive comps. Paper Glide compounds this further:
  an owner who escapes airborne mid-engage denies an execute its window entirely, not just its
  damage.
- **Not built in code this pass**: The Donkey (and the rest of the Indirect-Control archetype) stays
  blocked on a non-piloted-unit system that doesn't exist in `packages/simulation/arena_game.c` yet
  — every hero currently implemented is directly owner-piloted. This is a real, flagged gap (see
  NORTHSTAR §12's roster audit), not something this doc entry papers over; the ability is fully
  specified here so implementation is a clean follow-on once that system exists, not a fresh design
  question.

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

### Flamel — **Support — Alchemist-Gardener** (merged with the former "Druid" entry, 2026-07-24)

Nicolas Flamel, already in `TYLER/multiverse_heroes.md`'s roster of reframed real historical
figures. Founder direction: Druid and Flamel should be the same hero, not two separate roster
slots — the generic "Druid" archetype below was never actually a named TYLER lore character (no
entry for it in `multiverse_heroes.md`), while Flamel is; consolidating a nameless archetype into
an already-established named figure rather than carrying two adjacent support/growth heroes.
Flamel keeps his name and his lore ("The Great Work" is the real historical term for the
alchemical magnum opus, and reads naturally as *cultivation*, not just cooking); Druid's kit
supplies the actual moveset, since it was the more self-contained and less economy-blocked of the
two (three of its four pieces need no cooking/resource economy at all — only Q needed nothing
extra, and the old Flamel kit was entirely ally/economy-dependent with no self-contained piece).
The result: Flamel is the roster's first hero whose entire kit is the territory/growth system
made literal, not the cooking system — cooking (`docs/CONSUMABLES_AND_COOKING.md`) stays a
future, separate economy layer this kit doesn't require to function.

- **Passive — The Great Work**: Flamel's presence marks the ground he crosses (folded in from
  Druid's Overgrowth) — a marked cell that's still `NEUTRAL` at tick's end has an increased
  chance of converting toward his side. Cooked consumables prepared on his own marked ground are
  stronger and cheaper than the same recipe prepared elsewhere — alchemy is literally cultivation
  here, not recipe-following.
- **Q — Vine Growth** (from Druid, self-contained — no ally or economy needed): grows a temporary
  wall of vines in a line, rooting anything caught in it; decays after a few seconds.
- **W — Philosopher's Bloom** (Druid's Bloom + Flamel's Philosopher's Batch, merged): heals every
  nearby ally at once instead of one at a time, healing for more if cast on ground Flamel has
  marked — rewards playing on your own seeded plot.
- **R — Elixir of Wild Growth** (Flamel's Elixir framing, Druid's Wild Growth shape): a single
  powerful team-wide cast, cooked/grown once per game (long cooldown) — a large area rapidly
  overgrows, heavy-slowing enemies caught in it, healing-over-time to allies standing in it, and
  marking every cell inside simultaneously. The "master" part of master cook/gardener, reserved
  for exactly the moment a team decides to commit.

### The Morrigan (TYLER `multiverse_heroes.md` #68) — **Assassin/Duelist — meta jungler**

Built to resolve this doc's own earlier "flagged, not built" note below: the founder-observed
Morrigan/Flamel (formerly Druid) relationship — a shared Highland-Court territory tie with real
rock-paper-scissors counter-play (her war/death kit against Flamel's life/growth kit) — realized
directly through the territory/node system both heroes now hook into. Founder direction called her
a "meta jungler": with no standalone jungle-camp system in this arena yet, her jungler identity is
expressed as an affinity for *contested* ground rather than claimed territory — she belongs to the
unresolved fight, not the settled one.

- **Passive — Contested Ground**: gains bonus armor while standing on a node that's still
  contested (neither team has claimed it) — she's drawn to ground that hasn't picked a side yet.
- **Q — The Washer's Strike**: a ranged strike that hits harder the lower the target's current HP
  is — an execute, not flat damage; "the crow confirms the kill" the closer death already is.
- **W — Three Forms** (the eel underfoot, the wolf stampeding cattle, the hornless heifer leading
  it): closes distance instantly onto the nearest enemy and roots them on arrival — she appears
  where he doesn't expect, in another shape entirely.
- **R — The Crow Confirms It**: a fixed battlefield zone that ticks execute-scaled damage to any
  enemy standing in it for its duration — the lower an enemy's HP drops inside the zone, the harder
  each following tick lands. No ally-support side, unlike Ghost/Flamel's R zones — a war goddess's
  ultimate isn't a support tool.

### The Dagda (TYLER `multiverse_heroes.md` #69) — **Bruiser/Support — two-natured**

"The wheeled club settles every argument twice" — one end kills, the other revives, same tool,
depending only on which end swings first. Paired with the Morrigan in the lore (the one alliance
in TYLER's roster "both parties remember happening and neither one explains") and built alongside
her here.

- **Passive — The Undry**: the cauldron that never runs empty — passive, always-on HP regeneration,
  no cast required. "No one leaves it unsatisfied."
- **Q — The Wheeled Club**: literally two-natured, exactly as the lore describes — swings the
  killing end at a hittable enemy in range if one's there; otherwise swings the reviving end,
  healing a hurt living ally in range instead. Simplified from a literal revive (no respawn system
  exists in this arena to revive a dead ally into) to a strong heal — the same tool, either
  direction, depending only on what's actually in range when it swings.
- **W — Uaithne, Called By Name**: the harp's three master strains — sorrow, joy, and sleep —
  played over the whole hall in one go, exactly as in the myth. One AoE cast: allies in range get
  joy (healed), hittable enemies in range get sorrow and sleep at once (rooted and silenced).
- **R — The Porridge**: force-fed enough to kill an ordinary man, eaten unhurt, then still fighting
  the next day regardless — a real damage floor (HP cannot drop below 1 for the duration) plus a
  real heal on top, not just survival. Enduring **and** coming out ahead.

### The Courier (TYLER `multiverse_heroes.md` #32, "Ratatoskr's Debt-Collector") — **Fighter/Assassin**

Has carried insults up and down Yggdrasil, between the eagle at the crown and Nidhogg at the root,
for centuries — and has started editing them along the way, a job that was never meant to involve
judgment. That two-fixed-endpoint framing maps directly onto this arena's two existing `ArenaNode`
positions rather than needing a new system: The Courier's whole kit is about running the line
between two points and taking a cut off whatever passes through.

- **Passive — Lightly Edited**: whenever The Courier's Q lands a hit, it cleanses any active debuff
  on him (silence, root) — he edits the message addressed back to him out of the delivery.
- **Q — The Insult, Lightly Edited**: dashes toward the nearest enemy (same shape as Unicorn's
  Diagnostic Charge) and deals damage on arrival — the insult delivered in person.
- **W — Between Eagle and Serpent**: instantly repositions to whichever of the two map nodes is
  farther from his current position — always making real progress along the tree, a pure mobility
  tool distinct from every other hero's teleport (which are all ally- or foe-relative; this one is
  fixed-geography).
- **R — The Debt Collector's Due**: a long-tenured job has, over time, started to involve judgment
  it was never meant to — seizes a flat amount of HP from the nearest enemy and delivers it to
  himself, a forced collection rather than a fair trade.

---

### Loki (TYLER `multiverse_heroes.md` #37, "Loki, Who Isn't Here") — **Trickster/Bruiser** (S170-79)

The one figure in his own myth the compendium left out of the document — every other account of
him arrives secondhand, through Sigyn (#34) holding the venom-bowl "for as long as the myth
demands, and then kept holding it." His whole kit works the same way: not a straightforward
stat-check like most of the roster, but repositioning and endurance, presence registered only as
interference on someone else's reading rather than a signal of his own.

- **Q — Interference, Not a Signal**: an instant positional swap with the nearest enemy — no
  travel time, no dash arc, unlike every other dash-shaped Q in this roster. He's simply, suddenly,
  where the enemy was; a small hit lands on arrival if the swap puts them in range of each other.
  No range limit — the trade-off is a real cooldown, not a whiff condition.
- **W — Bound Where the Myth Says**: a free toggle (no cooldown, same convention as Unicorn's
  regen), granting a flat armor bonus while active — a defensive stance, not sustain.
- **R — Held For As Long As The Myth Demands**: self-cast survive-floor window (same
  `survive_floor_ms` mechanic Pizza and Dagda's ultimates already use) — HP can't drop below 1 for
  the duration. Someone else holds the outcome open for him, the way the bowl does in the myth.

---

### Gary (TYLER `multiverse_heroes.md` #35, "Gary, Bifrost Security (Off-Duty)") — **Marksman** (S170-91)

No magic — "extraordinary eyesight, extraordinary aim," and a job that never actually ends because
someone always has to be watching the bridge. The one hero in this roster with no dash, no
teleport, no gap-closer of any kind: Gary doesn't chase, he watches from where he's standing.

- **Q — The Property**: a stationary long-range precision shot at the nearest enemy — no movement
  at all, range-gated instead of a hit-radius-after-a-dash like most of the roster's Qs.
- **W — Watching the Bridge**: a free toggle (no cooldown) that extends Q's own range while
  active, rather than granting a stat bonus like every other toggle in this roster.
- **R — "Slow Down, This Isn't a Track Meet"**: a fixed-duration root on the nearest enemy — the
  same "slow simplified to a full stop" convention Tree's R/Flamel's R already use.

---

### Flute Debt (TYLER `multiverse_heroes.md` #42, "Han Xiangzi's Flute-Debt") — **DoT/Payoff** (S170-91)

One of the Eight Immortals, patron of musicians — "owes something to every wrong note ever played
near him, and eventually collects." The kit is a real debt mechanic, not a metaphor: apply it with
Q, cash it in with R.

- **Q — The Wrong Note**: modest immediate damage plus the shared `burning_ms`/`burn_dps` DoT
  fields (Pizza's mechanic, S170-46) — the debt accruing.
- **W — Recouping Interest**: a free toggle self-heal-over-time (same shape as Unicorn's regen) —
  passively collecting even outside a fight.
- **R — Eventually Collects**: always lands and consumes the cooldown, but deals real bonus damage
  if the target still has the Q's debt active, base damage otherwise. The actual payoff of the
  kit's theme — the debt has to still be open for it to collect big.

---

### Bacon+Puck (TYLER `multiverse_heroes.md` #5 + #67, merged) — **Trickster/Skirmisher** (S170-94)

Two entries merged into one hero, same pattern as Flamel/Druid. Bacon's whole character is
withholding — "custodian of the one location nobody's allowed to know yet," seed phrase "ask
again later." Puck's is an unresolved duality nobody can confirm the real version of. Combined:
a kit built around not being pinned down.

- **Q — Ask Again Later**: self `intangible_ms` (the shared can't-be-hit status, S170-32) — for
  longer while W is toggled on.
- **W — Which One Is The Real One**: a free toggle (no cooldown) that extends Q's own
  intangibility duration, rather than granting a stat like most toggles in this roster.
- **R — The Trick Was Always the Same**: real damage plus a self-heal off a fraction of it —
  the mischief pays for itself either way.

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
- **Built (2026-07-24, S170-47)**: the Morrigan/Druid relationship flagged in the previous version
  of this note is now realized — Druid merged into Flamel (see the Flamel entry above), and the
  Morrigan built alongside the Dagda, both hooking into the territory/node system so the
  rock-paper-scissors counter-play (Morrigan's war/death kit vs. Flamel's life/growth kit) is real,
  not just a design intention.
