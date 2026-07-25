#ifndef ARENA_GAME_H
#define ARENA_GAME_H

#define ARENA_HALF_EXTENT 12.0f
#define ARENA_HERO_SPEED 4.0f      /* units/sec */
#define ARENA_ATTACK_RANGE 1.6f
#define ARENA_ATTACK_DAMAGE 8
#define ARENA_ATTACK_COOLDOWN_MS 700
#define ARENA_NODE_COUNT 2

/* Territorial dynamic jungle creeps (S170-51). Founder direction: territory
 * is the macro/economy layer, objectives (the team-wipe win condition) are
 * how the game is actually won, and gameplay should let territory control
 * shape what jungle creeps emerge -- "controlling the flavor and cadence of
 * the jungle helps create the meta to counter certain comps or play
 * styles." One creep per node, tied to that node's `owner`, re-rolled on
 * every respawn rather than fixed at map-init -- the jungle's own
 * population reacts to who currently controls the ground under it, not a
 * static camp table (matching the earlier NORTHSTAR §8 "alive and dynamic,
 * not static camps" direction, now built rather than just specified).
 * Numbers are the "beginner/danger-tier" spirit of GoblinFoxDragon's real
 * mob archetypes (server/mob/hills.go: passive-until-attacked low-HP vs. a
 * tougher, rarer high-value target), adapted rather than ported verbatim --
 * GFD's mobs carry a full aggro-cone/leash-range system this arena's
 * click-to-move model has no equivalent for; this keeps just the
 * difficulty-tiering idea. Two flavors, two different rewards, not just
 * two HP totals -- that's the actual "flavor" half of the ask:
 *   - A CONTESTED node's creep (owner==0) is the rare, tanky, slow-respawn
 *     prize -- killing it hands the killer's team a large one-time capture-
 *     progress bonus toward THAT node, a real tempo swing worth fighting
 *     over regardless of side.
 *   - An OWNED node's creep is common, weak, fast-respawning -- a small
 *     steady "home-turf resupply" heal when the OWNING team kills it (their
 *     own jungle sustains them), but a smaller capture-progress kick toward
 *     FLIPPING the node when the OPPOSING team kills it instead -- a real
 *     counter-play tool against a team that's turtled onto a lot of
 *     territory: you can whittle down their jungle advantage by farming it
 *     out from under them, not just by fighting them directly. */
typedef enum {
    ARENA_CREEP_NEUTRAL = 0, /* mirrors node owner 0 exactly -- see ArenaCreep.node_index */
    ARENA_CREEP_TEAM0 = 1,
    ARENA_CREEP_TEAM1 = 2,
} ArenaCreepFlavor;
#define ARENA_MAX_CREEPS ARENA_NODE_COUNT /* one creep per node, index-matched */
#define ARENA_CREEP_NEUTRAL_HP              80
#define ARENA_CREEP_TEAM_HP                 40
#define ARENA_CREEP_NEUTRAL_RESPAWN_MS       30000 /* the rare prize -- slow cadence */
#define ARENA_CREEP_TEAM_RESPAWN_MS          12000 /* home-turf resupply -- fast cadence, rewards holding ground */
#define ARENA_CREEP_AGGRO_RADIUS             4.0f  /* passive-until-approached, same spirit as GFD's Rabbit */
#define ARENA_CREEP_DAMAGE                   6
#define ARENA_CREEP_ATTACK_COOLDOWN_MS       1500
#define ARENA_CREEP_NEUTRAL_KILL_CAPTURE_BONUS_MS 5000 /* big swing for winning the contested prize */
#define ARENA_CREEP_TEAM_KILL_HEAL                20   /* home-turf resupply, owning team only */
#define ARENA_CREEP_TEAM_KILL_DENY_CAPTURE_BONUS_MS 1500 /* counter-play: farming an enemy's own jungle creep helps flip their node */

/* Team-scale arena (2026-07-24, NORTHSTAR §13 cont'd): the array grows from
 * 2 to ARENA_MAX_HEROES so a full 10v10 match fits in the same ArenaState
 * the 1v1 local demo and apps/arena_server (1v1) already use. The 1v1 path
 * (arena_init/arena_init_with_heroes) still only ever populates heroes[0]/
 * [1] and leaves the rest zeroed/inactive -- see the `active` field below. */
#define ARENA_TEAM_SIZE 10
#define ARENA_MAX_HEROES (ARENA_TEAM_SIZE * 2)

/* Hero roster (docs/HEROES_VS0.md), NORTHSTAR §12 Phase D. hero_id
 * generalizes kit dispatch away from S170-18's "owner 0 == The Unicorn"
 * hardcoding -- either owner slot can carry either hero now. Growing this
 * list is the actual "full roster" work; ten more heroes from the doc are
 * follow-on passes, not this one (EMILY/BACKLOG.md S170-31). */
typedef enum {
    ARENA_HERO_UNICORN = 0,
    ARENA_HERO_DUCK = 1,
    ARENA_HERO_GHOST = 2,
    ARENA_HERO_FROG = 3,
    ARENA_HERO_DOC_WHEEL = 4,
    ARENA_HERO_TREE = 5,
    ARENA_HERO_PIZZA = 6,
    ARENA_HERO_FLAMEL = 7, /* merged with the former "Druid" archetype, 2026-07-24 -- see docs/HEROES_VS0.md */
    ARENA_HERO_MORRIGAN = 8,
    ARENA_HERO_DAGDA = 9,
    ARENA_HERO_COURIER = 10, /* Ratatoskr, TYLER multiverse_heroes.md #32 */
    ARENA_HERO_LOKI = 11, /* TYLER multiverse_heroes.md #37, "Loki, Who Isn't Here" (S170-79) */
    ARENA_HERO_GARY = 12, /* TYLER multiverse_heroes.md #35, "Gary, Bifrost Security (Off-Duty)" (S170-91) */
    ARENA_HERO_FLUTE_DEBT = 13, /* TYLER multiverse_heroes.md #42, "Han Xiangzi's Flute-Debt" (S170-91) */
} ArenaHeroID;
#define ARENA_HERO_COUNT 14

/* The Unicorn — first real hero kit wired in (S170-18). */
#define ARENA_UNICORN_ARMOR         4    /* passive: Chassis Claim, flat dmg reduction */
#define ARENA_UNICORN_Q_DASH_DIST   4.0f /* Diagnostic Charge */
#define ARENA_UNICORN_Q_DAMAGE      12
#define ARENA_UNICORN_Q_HIT_RADIUS  1.8f
#define ARENA_UNICORN_Q_COOLDOWN_MS 4000
#define ARENA_UNICORN_W_REGEN_PER_SEC 6  /* Spaghetti Vent, while toggled on */
#define ARENA_UNICORN_R_COOLDOWN_MS 15000
#define ARENA_UNICORN_R_DURATION_MS 3000 /* Full Disclosure: armor doubled */

/* The Duck — second hero kit (S170-31). Q/R only: W (Government Clearance)
 * needs towers/objective structures that don't exist in this 1v1 arena, and
 * E (Chosen One) triggers on a killing blow, but arena's win condition ends
 * the match on that same kill -- the buff window and match-end coincide, so
 * it would have zero observable effect here. Both skipped, not faked. */
#define ARENA_DUCK_Q_PULL_DIST      5.0f /* Telekinetic Yank: how far the foe gets pulled */
#define ARENA_DUCK_Q_DAMAGE         10
#define ARENA_DUCK_Q_RANGE          6.0f /* max distance the yank can reach */
#define ARENA_DUCK_Q_COOLDOWN_MS    5000
#define ARENA_DUCK_R_PULL_DIST      9.0f /* Total Telekinesis: bigger yank */
#define ARENA_DUCK_R_DAMAGE         20
#define ARENA_DUCK_R_RANGE          9.0f
#define ARENA_DUCK_R_COOLDOWN_MS    18000

/* The Ghost — third hero kit (S170-32). First kit needing real status-effect
 * state (silence, intangibility) rather than just cooldowns/toggles. R's
 * ally-heal side (docs/HEROES_VS0.md: "same zone, opposite effect depending
 * on team") has no target in a 1v1 -- only the enemy-damage side is
 * implemented, flagged not faked. Passive (Mid-Piano, silent undodgeable
 * casts) is a cast-animation/UI concept with no gameplay effect to model in
 * this arena -- skipped, flagged, same reasoning as other UI-only passives. */
#define ARENA_GHOST_Q_RANGE         7.0f  /* Alien Frequency: skillshot range */
#define ARENA_GHOST_Q_DAMAGE        9
#define ARENA_GHOST_Q_SILENCE_MS    1500
#define ARENA_GHOST_Q_COOLDOWN_MS   4500
#define ARENA_GHOST_W_INTANGIBLE_MS 1500 /* Not a Ghost */
#define ARENA_GHOST_W_COOLDOWN_MS   10000
#define ARENA_GHOST_R_RADIUS        4.0f  /* Recital: zone stays fixed where cast */
#define ARENA_GHOST_R_DURATION_MS   4000
#define ARENA_GHOST_R_DPS           6     /* damage/sec to enemies standing in the zone */
#define ARENA_GHOST_R_COOLDOWN_MS   20000

/* The Frog — fourth hero kit (S170-33), the last clean-fit pick from
 * S170-32's roster audit at the time (before allies existed, S170-45 below).
 * R (The Secret) is simplified to reuse Ghost's intangible_ms mechanic at a
 * longer duration; "reappear at any visited location" needs its own
 * location-memory system, deferred, not faked as the full ability. Passive
 * (Never Told Anyone, no visible cooldown UI for enemies) is a bluffing/UI
 * concept -- arena has no separate enemy-facing view to hide anything from,
 * skipped, flagged.
 * W (Borrowed Time) was originally skipped for having no ally target in
 * 1v1 -- wired for real (S170-45) now that arena_nearest_ally exists. Uses
 * the generic next_cast_refund buff field, same mechanism any future
 * ally-buff kit would reuse. */
#define ARENA_FROG_LOOPBACK_SAMPLE_MS 250 /* Q — Loop Back: how often position/HP is sampled */
#define ARENA_FROG_LOOPBACK_SLOTS     16  /* 16 * 250ms = 4000ms of history, enough to rewind 3s */
#define ARENA_FROG_Q_REWIND_MS      3000
#define ARENA_FROG_Q_COOLDOWN_MS    8000
#define ARENA_FROG_R_VANISH_MS      5000  /* The Secret, simplified (see comment above) */
#define ARENA_FROG_R_COOLDOWN_MS    25000
#define ARENA_FROG_W_COOLDOWN_MS    12000 /* Borrowed Time: places the refund buff on an ally */

/* Doc Wheel (Buer) — fifth hero kit (S170-45), the first ally-targeted-only
 * kit ("the entire kit is being the correct ally to have nearby" per
 * docs/HEROES_VS0.md) and the reason arena_nearest_ally exists at all. The
 * RED GARDEN passive (CORRUPTED-cell decay on heal) is skipped -- arena has
 * no GridCell/territory system (same blocker as Tree/Pizza/Druid, S170-32's
 * audit). R ("No Combat Power, As Advertised" -- teamwide debuff-cleanse +
 * shield) is simplified to teamwide cleanse + heal, not a literal absorb-
 * shield -- shields would be a new generic damage-absorption mechanic
 * touching every damage call site in this file for a single ability's
 * sake; deferred rather than built shallow, same reasoning as other
 * simplified (not faked) pieces elsewhere in this roster. */
#define ARENA_DOC_WHEEL_Q_HEAL_BASE   14   /* Bedside Manner: heal at 100% target HP */
#define ARENA_DOC_WHEEL_Q_HEAL_LOW_HP 28   /* heal amount at ~0% target HP -- passive scaling */
#define ARENA_DOC_WHEEL_Q_COOLDOWN_MS 3500
#define ARENA_DOC_WHEEL_W_COOLDOWN_MS 16000 /* House Call: teleport to ally's location */
#define ARENA_DOC_WHEEL_R_RADIUS      6.0f
#define ARENA_DOC_WHEEL_R_HEAL        20   /* teamwide heal (R, simplified from a shield) */
#define ARENA_DOC_WHEEL_R_COOLDOWN_MS 30000

/* Territory / node system (S170-46, NORTHSTAR §13 cont'd; redesigned
 * S170-50). The founder's original "territory/resource economy" pick over
 * allies-scaling or non-piloted units, then explicitly redirected away from
 * ambient presence-math toward a real Arathi Basin-style flag: "true click
 * to channel capture, interruptable, a neutral period after the flag flips
 * as you wait for it to finish capturing -- adds objective-focused play and
 * the possibility of losing due to ignoring the objective, not just
 * presence-based." The old model (signed `pressure` drifting toward
 * whichever team had more weighted bodies nearby, owner derived from a
 * threshold) is gone entirely, not layered under this -- it was exactly
 * the "just presence based" thing being moved away from.
 *
 * New model: exactly one team can be channeling a node at a time.
 *   - Exclusive presence (only team A's living heroes in radius, zero from
 *     team B) starts or continues team A's channel.
 *   - The instant a channel starts against a node NOT already owned by the
 *     channeling team, the node flips to neutral (owner=0) immediately --
 *     this is the "neutral period... as you wait for it to finish
 *     capturing": the node sits open, genuinely uncaptured, for the whole
 *     channel duration, not just at the end.
 *   - Mixed presence (both teams in radius) or the channeling team fully
 *     leaving interrupts the channel: progress resets to 0, capturing_team
 *     clears. The node does NOT revert to its pre-channel owner -- a
 *     defender who interrupts an attacker still has to walk over and start
 *     their own channel to reclaim it. This is the actual teeth behind
 *     "the possibility of losing due to ignoring the objective": leaving a
 *     flag undefended costs it the instant the enemy commits, and even a
 *     successful defense doesn't hand it back for free.
 *   - Reaching ARENA_NODE_CAPTURE_CHANNEL_MS flips owner to the channeling
 *     team and clears the channel state.
 *
 * This is the enabling system for Tree (Root Network), Pizza (corruption),
 * and Flamel (Overgrowth marking, absorbed from the former Druid) -- the
 * three heroes S170-32's roster audit flagged as blocked on exactly this;
 * all three are redesigned below to hook into the channel instead of the
 * retired pressure-drift. */
#define ARENA_NODE_CAPTURE_RADIUS        5.0f
#define ARENA_NODE_CAPTURE_CHANNEL_MS    8000  /* base channel duration, no bonuses -- Arathi Basin's own real cap timer is in this ballpark */
#define ARENA_TREE_CHANNEL_SPEED_MULT    2.0f  /* Root Network: a Tree among the channeling team's present heroes doubles progress this tick */
#define ARENA_FLAMEL_MARK_MS             6000  /* Overgrowth: how long a mark persists once Flamel leaves */
#define ARENA_FLAMEL_MARK_CHANNEL_BONUS_MS 200 /* extra channel progress per tick while capturing on ground the capturing team has marked -- deterministic simplification of the doc's "increased chance of converting," flagged */

/* Tree — sixth hero kit (S170-46). Passive (Root Network) needs no ability
 * code at all -- arena_tick_nodes reads hero_id directly and applies
 * ARENA_TREE_CHANNEL_SPEED_MULT. Q (Vine Lash) simplifies "AoE root in a
 * cone in front" to an instant hit-if-in-range check, same precedent as
 * Ghost's Alien Frequency. W (Untranslated, ally CC-immunity) is
 * unbuildable -- arena's own ability casts are all instant, nothing to
 * interrupt there -- skipped, flagged, same reasoning as other
 * mechanic-less passives (the node *capture* channel added by S170-50 is a
 * map-objective mechanic, a different thing from an ability-cast channel).
 * R (Grand Secret) simplifies "roots permanently until recast, min 8s" to a
 * fixed-duration self-root + armor buff, same "fixed duration" simplification
 * already used for Frog's R and Ghost's R zone. */
#define ARENA_TREE_Q_RANGE         6.0f
#define ARENA_TREE_Q_DAMAGE        10
#define ARENA_TREE_Q_ROOT_MS       1500
#define ARENA_TREE_Q_COOLDOWN_MS   5000
#define ARENA_TREE_R_ROOT_MS       8000  /* Grand Secret: self-root, min 8s per the doc */
#define ARENA_TREE_R_ARMOR_BONUS   8
#define ARENA_TREE_R_HEAL          30
#define ARENA_TREE_R_COOLDOWN_MS   25000

/* Pizza — seventh hero kit (S170-46, corruption redesigned S170-50). Passive
 * (Uninvestigated Fire) is an always-on burn aura (AP-scaling simplified to
 * flat DPS, same precedent as Ghost's flat R_DPS) plus a corruption effect
 * on the channel-capture mechanic, handled generically in arena_tick_nodes:
 * Pizza's mere presence in radius forces any in-progress channel on that
 * node to interrupt, regardless of which team she's on or whether her
 * presence would otherwise count as "exclusive" -- corruption doesn't care
 * whose side you're on, a direct carry-over of the same "regardless of team
 * composition" framing from the old pressure model. Q (Nobody Checked)
 * simplifies "throw a burning slice + ground patch" to direct damage + a
 * burn DoT applied straight to the foe -- no persistent ground-hazard
 * system exists, so the lingering-patch half is dropped, not faked. W (I Am
 * The Chosen One) is pure-visual, zero mechanical effect per the doc
 * itself -- skipped, flagged, same reasoning as Duck's W and Ghost's
 * passive. R (Nobody Ever Checks) is built for real: a damage floor status
 * effect, the one piece of this roster's simplifications that needed
 * apply_damage() centralized rather than shortcut. */
#define ARENA_PIZZA_AURA_RADIUS    3.5f
#define ARENA_PIZZA_AURA_DPS       4
#define ARENA_PIZZA_Q_RANGE        6.0f
#define ARENA_PIZZA_Q_DAMAGE       8
#define ARENA_PIZZA_Q_BURN_MS      3000
#define ARENA_PIZZA_Q_BURN_DPS     5
#define ARENA_PIZZA_Q_COOLDOWN_MS  4500
#define ARENA_PIZZA_R_FLOOR_MS     4000
#define ARENA_PIZZA_R_COOLDOWN_MS  28000

/* Flamel — eighth hero kit (S170-46), merged with the former "Druid" archetype
 * per founder direction ("druid and flamel should be the same hero") --
 * docs/HEROES_VS0.md carries the full merge rationale. Passive (Great Work +
 * Overgrowth) needs no ability code for the marking half (arena_tick_nodes
 * reads hero_id directly, same as Tree); the cooking-bonus half is out of
 * scope this pass (docs/CONSUMABLES_AND_COOKING.md isn't wired to any hero
 * kit yet) -- skipped, flagged, not faked. Q (Vine Growth) simplifies "wall
 * of vines in a line" to an instant root-if-in-range check on the nearest
 * enemy, same cone/line-to-single-target-range simplification as Tree's Q.
 * W (Philosopher's Bloom) merges Bloom + Philosopher's Batch into one AoE
 * ally heal with a marked-node bonus. R (Elixir of Wild Growth) merges
 * Elixir's team-ultimate framing with Wild Growth's AoE shape: a fixed
 * zone (reusing Ghost's r_zone_x/z/tick_ms fields) that roots enemies and
 * heals allies each tick for its duration, plus a one-time mass-mark of
 * nodes in radius at cast time. The "heavy slow" from the doc is simplified
 * to a full root -- no per-hero movement-speed-multiplier system exists in
 * this arena yet, flagged. */
#define ARENA_FLAMEL_Q_RANGE         5.5f
#define ARENA_FLAMEL_Q_ROOT_MS       1500
#define ARENA_FLAMEL_Q_COOLDOWN_MS   5500
#define ARENA_FLAMEL_W_RADIUS        4.5f
#define ARENA_FLAMEL_W_HEAL_BASE     10
#define ARENA_FLAMEL_W_HEAL_MARKED   18  /* Philosopher's Bloom: more healing cast on Flamel's own marked ground */
#define ARENA_FLAMEL_W_COOLDOWN_MS   9000
#define ARENA_FLAMEL_R_RADIUS        5.0f
#define ARENA_FLAMEL_R_DURATION_MS   4000
#define ARENA_FLAMEL_R_ROOT_MS       1200 /* refreshed each 1000ms tick an enemy stays in the zone */
#define ARENA_FLAMEL_R_HEAL_PER_TICK 8
#define ARENA_FLAMEL_R_COOLDOWN_MS   32000

/* Morrigan — ninth hero kit (S170-47, TYLER multiverse_heroes.md #68). A
 * war/death goddess whose whole hook (per the doc's own "flagged, not
 * built" note in HEROES_VS0.md) is rock-paper-scissors counter-play
 * against Flamel's life/growth kit -- founder direction calls her a "meta
 * jungler." No standalone jungle-camp system exists in this arena, so her
 * jungler identity is expressed the same way Tree/Pizza/Flamel's territory
 * hooks are: tied to the ArenaNode contest that already exists, rather than
 * inventing a second system. Passive rewards standing in neutral/contested
 * ground (a war goddess belongs to the unresolved fight, not settled
 * territory). Q and R both scale up against a low-HP target -- "the crow
 * confirms the kill," matching the lore's death-omen framing, and mirroring
 * (inverted) Doc Wheel's heal-more-when-hurt math. W (the eel/wolf/heifer
 * animal-form harassment scene) is a sudden gap-close + root onto the
 * nearest enemy -- "she appears where he doesn't expect." */
#define ARENA_MORRIGAN_PASSIVE_ARMOR_BONUS 4   /* Contested Ground: bonus armor while standing on a contested (owner==0) node */
#define ARENA_MORRIGAN_Q_RANGE          6.0f
#define ARENA_MORRIGAN_Q_DAMAGE_BASE    8      /* The Washer's Strike, at 100% target HP */
#define ARENA_MORRIGAN_Q_DAMAGE_LOW_HP  18     /* at ~0% target HP -- an execute, damage scales up as the target dies */
#define ARENA_MORRIGAN_Q_COOLDOWN_MS    4000
#define ARENA_MORRIGAN_W_ROOT_MS        1200   /* Three Forms: gap-close + root on arrival */
#define ARENA_MORRIGAN_W_COOLDOWN_MS    7000
#define ARENA_MORRIGAN_R_RADIUS         4.5f
#define ARENA_MORRIGAN_R_DURATION_MS    3500
#define ARENA_MORRIGAN_R_DAMAGE_BASE    4      /* The Crow Confirms It: per-tick execute DPS, at 100% target HP */
#define ARENA_MORRIGAN_R_DAMAGE_LOW_HP  12     /* per-tick DPS at ~0% target HP */
#define ARENA_MORRIGAN_R_COOLDOWN_MS    24000

/* Dagda — tenth hero kit (S170-47, TYLER multiverse_heroes.md #69). "The
 * wheeled club settles every argument twice" -- one end kills, the other
 * revives, same tool, depending only on which end swings first. Built
 * literally: Q checks what's in range and picks the end. The cauldron
 * (Undry, "never runs empty") is a passive sustain regen. The harp
 * (Uaithne's three master strains, sorrow/joy/sleep, played over an entire
 * hall in one go) is one AoE cast hitting everyone in range at once --
 * enemies get sorrow+sleep (root+silence), allies get joy (heal). The
 * force-fed porridge scene ("eats every bite, unhurt, fights the next day
 * regardless") is a damage floor + a real heal, not just survival --
 * enduring AND coming out ahead. */
#define ARENA_DAGDA_PASSIVE_REGEN_PER_SEC 3   /* The Undry: passive self HP regen, always on */
#define ARENA_DAGDA_Q_RANGE           5.5f
#define ARENA_DAGDA_Q_KILL_DAMAGE     16      /* the killing end of the club */
#define ARENA_DAGDA_Q_REVIVE_HEAL     16      /* the reviving end, simplified to a heal -- no respawn system exists to revive into */
#define ARENA_DAGDA_Q_COOLDOWN_MS     5000
#define ARENA_DAGDA_W_RADIUS          4.5f
#define ARENA_DAGDA_W_ROOT_MS         1200    /* sorrow */
#define ARENA_DAGDA_W_SILENCE_MS      1200    /* sleep */
#define ARENA_DAGDA_W_ALLY_HEAL       10      /* joy */
#define ARENA_DAGDA_W_COOLDOWN_MS     11000
#define ARENA_DAGDA_R_FLOOR_MS        3000    /* the porridge: a real damage floor */
#define ARENA_DAGDA_R_HEAL            30      /* ...and still comes out ahead, not just surviving */
#define ARENA_DAGDA_R_COOLDOWN_MS     26000

/* The Courier — eleventh hero kit (S170-48, TYLER multiverse_heroes.md #32,
 * "Ratatoskr's Debt-Collector"). Runs constantly between two fixed points
 * (the eagle at Yggdrasil's crown, Nidhogg at its root) -- maps directly
 * onto this arena's two existing ArenaNode positions rather than needing a
 * new system. Passive cleanses The Courier's own debuffs on a landed Q hit
 * ("editing the message" addressed back to him). Q is a dash-strike, same
 * shape as Unicorn's Diagnostic Charge. W is a pure fixed-geography
 * teleport (distinct from every other hero's ally/foe-relative teleports --
 * this one always jumps to whichever node is farther away, "making
 * progress along the tree"). R is a flat single-target life-drain execute
 * ("started to involve judgment" -- taking a cut by force). */
#define ARENA_COURIER_Q_DASH_DIST   5.0f
#define ARENA_COURIER_Q_DAMAGE      10
#define ARENA_COURIER_Q_HIT_RADIUS  1.8f
#define ARENA_COURIER_Q_COOLDOWN_MS 4500
#define ARENA_COURIER_W_COOLDOWN_MS 9000
#define ARENA_COURIER_R_RANGE       6.0f
#define ARENA_COURIER_R_DRAIN       18
#define ARENA_COURIER_R_COOLDOWN_MS 20000

/* Loki, Who Isn't Here (S170-79, TYLER multiverse_heroes.md #37) -- a hero
 * defined by absence, so his kit works through repositioning and endurance
 * rather than a straightforward stat-check. Q is an instant swap with the
 * nearest enemy (no travel time, no dash arc -- he's just suddenly where the
 * enemy was, which is what "present only as interference on adjacent
 * readings" means mechanically) plus a small hit on arrival. W is a toggled
 * flat armor bonus ("bound where the myth says," a defensive stance, not
 * regen -- distinct from Unicorn's toggle). R borrows the same
 * survive_floor_ms mechanic Pizza/Dagda already use, cast on himself: "the
 * bowl does not need to be believed to be held" (Sigyn, #34) -- someone else
 * holding the outcome open for him for a fixed window, same as the myth. */
#define ARENA_LOKI_Q_DAMAGE         10
#define ARENA_LOKI_Q_HIT_RADIUS     2.0f
#define ARENA_LOKI_Q_COOLDOWN_MS    5000
#define ARENA_LOKI_W_ARMOR_BONUS    5 /* free toggle, no cooldown -- same convention as Unicorn's W */
#define ARENA_LOKI_R_FLOOR_MS       3500
#define ARENA_LOKI_R_COOLDOWN_MS    24000

/* Gary, Bifrost Security (Off-Duty) (S170-91, TYLER multiverse_heroes.md #35) -- pure
 * marksman, no magic, "extraordinary eyesight, extraordinary aim." Q is a stationary
 * long-range precision shot (no dash, no movement -- Gary doesn't chase, he watches).
 * W is a free toggle that extends Q's own range rather than granting a stat like every
 * other toggle so far ("watching the bridge" further out, a genuinely different toggle
 * shape). R is a fixed-duration root on the nearest enemy -- "slow down, this isn't a
 * track meet," simplified to a full stop the same way Tree's R/Flamel's R already
 * simplify a slow down to a root rather than adding a real speed-multiplier system. */
#define ARENA_GARY_Q_RANGE          6.0f
#define ARENA_GARY_Q_RANGE_WATCHING 9.0f /* Q's range while W is toggled on */
#define ARENA_GARY_Q_DAMAGE         11
#define ARENA_GARY_Q_COOLDOWN_MS    3500
#define ARENA_GARY_R_RANGE          6.0f
#define ARENA_GARY_R_ROOT_MS        2000
#define ARENA_GARY_R_COOLDOWN_MS    16000

/* Han Xiangzi's Flute-Debt (S170-91, TYLER multiverse_heroes.md #42) -- "owes something to
 * every wrong note ever played near him, and eventually collects." Q applies a real debt:
 * modest damage plus the shared burning_ms/burn_dps DoT fields (S170-46), standing in for
 * the wrong note accruing. W is a free toggle self-heal-over-time ("recouping interest"
 * passively, even outside a fight -- reuses the same toggle-regen shape as Unicorn's W,
 * distinct role). R is the actual payoff, "eventually collects": bonus damage against a
 * target that still has the Q debt active when R lands, base damage otherwise -- always
 * commits and consumes the cooldown either way (same "always lands" convention as Doc
 * Wheel's/Flamel's R), the debt just decides how much it collects. */
#define ARENA_FLUTE_DEBT_Q_DAMAGE      6
#define ARENA_FLUTE_DEBT_Q_HIT_RADIUS  1.8f
#define ARENA_FLUTE_DEBT_Q_BURN_DPS    4
#define ARENA_FLUTE_DEBT_Q_BURN_MS     4000
#define ARENA_FLUTE_DEBT_Q_COOLDOWN_MS 3800
#define ARENA_FLUTE_DEBT_W_REGEN_PER_SEC 3
#define ARENA_FLUTE_DEBT_R_RANGE        5.5f
#define ARENA_FLUTE_DEBT_R_DAMAGE_BASE  8
#define ARENA_FLUTE_DEBT_R_DAMAGE_DEBT  22 /* dealt instead of BASE if the target's debt (burning_ms) is still active */
#define ARENA_FLUTE_DEBT_R_COOLDOWN_MS  18000

typedef struct {
    float x, z;
    float target_x, target_z;
    int moving;
    int hp;
    int max_hp;
    int attack_cooldown_ms;
    int owner; /* 0 = player, 1 = bot in the 1v1 local demo; a slot index 0..ARENA_MAX_HEROES-1 in team mode */
    int alive;
    int team;   /* 2026-07-24: which side, for team-mode nearest-enemy targeting. 1v1 local demo sets 0/1 explicitly. */
    int active; /* 2026-07-24: was this slot ever populated by arena_init_with_heroes/arena_init_teams? Distinct from `alive` (which also goes 0 on death) -- lets a generalized loop over ARENA_MAX_HEROES skip never-used padding slots in 1v1 mode without mistaking them for "already dead" participants. */
    ArenaHeroID hero_id;
    /* Generic ability state, shared field names across kits (Unicorn's
     * Q/W/R and Duck's Q/R both use these) rather than one struct per hero
     * -- simplest thing that works for a 2-kit roster; revisit if a future
     * kit needs state shape these fields can't express. */
    int q_cooldown_ms;
    int w_active;      /* Unicorn's Spaghetti Vent toggle; unused by Duck/Ghost */
    int w_cooldown_ms; /* Ghost's Not a Ghost; Unicorn's W is a free toggle and doesn't use this */
    int r_cooldown_ms;
    int r_active_ms;   /* Unicorn's armor-double / Ghost's Recital zone duration; unused by Duck */
    float r_zone_x, r_zone_z; /* Ghost's Recital: fixed zone position at cast time */
    int r_zone_tick_ms; /* Ghost's Recital: counts up to 1000ms, then ticks one DPS-worth of damage --
                          * a fixed-interval tick rather than fractional-per-tick accumulation, so it
                          * behaves correctly at any real frame rate, not just in a single big test step. */
    /* Status effects -- generic, any hero's kit can apply these to any
     * other hero, not just Ghost's own state (S170-32 is the first kit to
     * apply them, but the fields aren't Ghost-specific). */
    int silenced_ms;    /* > 0: cannot cast Q/W/R */
    int intangible_ms;  /* > 0: cannot be hit by attacks or ability damage */
    /* rooted_ms (S170-46, Tree's Q/R and Flamel's Q/R): > 0: cannot move,
     * even with a move command already queued -- gated in
     * update_hero_motion. Also read by duck_pull_foe as "immune to
     * displacement," honoring Tree's R without a separate generic
     * displacement-immunity field: rooted already means "an external force
     * can't move you" is a natural extension of "you can't move yourself." */
    int rooted_ms;
    /* burning_ms/burn_dps (S170-46, Pizza's Q): a generic damage-over-time
     * debuff, any hero's kit could apply it, not Pizza-specific storage --
     * same reasoning as the other status-effect fields above. Ticks down
     * and deals burn_dps once per 1000ms via burn_tick_ms, mirroring Ghost's
     * R zone's fixed-interval tick. */
    int burning_ms;
    int burn_dps;
    int burn_tick_ms;
    /* survive_floor_ms (S170-46, Pizza's R "Nobody Ever Checks"): > 0: this
     * hero's HP cannot be reduced below 1 by apply_damage, no matter how
     * much raw damage lands -- built for real (not simplified away) since
     * it's the entire point of the ability, using the same centralized
     * apply_damage() every damage call site already routes through. */
    int survive_floor_ms;
    /* aura_tick_ms (S170-46, Pizza's always-on burn aura passive): generic
     * fixed-interval accumulator for a passive that ticks independently of
     * any cast, distinct from r_zone_tick_ms (which is cast-scoped). */
    int aura_tick_ms;
    /* damaged_this_tick (S170-51 cont'd): set by apply_damage() on ANY hit
     * from ANY source (melee, ability, creep, burn tick -- matching real
     * WoW Arathi Basin's "any damage interrupts your capture channel"),
     * read and cleared once per tick by arena_tick_nodes. A simplification
     * of the real mechanic's "the specific channeling character" down to
     * "any hero of the channeling team gets hit interrupts the team's
     * channel" -- this arena tracks capture channels per-team, not per-
     * individual-capturing-hero, flagged here rather than silently
     * narrowed. */
    int damaged_this_tick;
    /* next_cast_refund: generic ally-buff flag (S170-45, Frog's Borrowed
     * Time places this on an ally, not itself) -- the next successful Q/W/R
     * cast by whoever carries this flag has its cooldown refunded to 0
     * instead of the normal value, then the flag clears. Generic so any
     * future ally-buff kit can reuse it, same reasoning as the status-
     * effect fields above. */
    int next_cast_refund;
    /* The Frog's Loop Back (S170-33): a small ring buffer of this hero's
     * own past (x, z, hp), sampled every ARENA_FROG_LOOPBACK_SAMPLE_MS.
     * Generic per-hero state, not Frog-specific storage, same reasoning as
     * the status-effect fields above -- nothing else uses it yet. */
    float loopback_x[ARENA_FROG_LOOPBACK_SLOTS];
    float loopback_z[ARENA_FROG_LOOPBACK_SLOTS];
    int loopback_hp[ARENA_FROG_LOOPBACK_SLOTS];
    int loopback_count;       /* how many slots have ever been written (caps at ARENA_FROG_LOOPBACK_SLOTS) */
    int loopback_next_slot;   /* next slot to write (wraps) */
    int loopback_since_sample_ms;
} ArenaHero;

typedef struct {
    float x, z;
    /* owner, capturing_team, capture_progress_ms, marked_by_team (S170-46,
     * capture mechanic redesigned S170-50): the territory contest state,
     * all recomputed every tick by arena_tick_nodes -- not set directly
     * anywhere else except test setup. */
    int owner;              /* 0 = neutral/contested, 1 = team 0, 2 = team 1 */
    int capturing_team;     /* -1 = no active channel, else 0/1: which team is currently channeling this node */
    int capture_progress_ms; /* 0..ARENA_NODE_CAPTURE_CHANNEL_MS (plus bonuses); resets to 0 on interrupt or on completion */
    int marked_by_team;  /* -1 = unmarked, else team index (Flamel's Overgrowth, absorbed from Druid) */
    int mark_ms_remaining;
} ArenaNode;

/* ArenaCreep (S170-51): one jungle creep per node, index-matched
 * (creeps[i] always belongs to nodes[i]). See the header comment above
 * ARENA_MAX_CREEPS for the full design. */
typedef struct {
    float x, z;
    int hp, max_hp;
    int alive;
    ArenaCreepFlavor flavor;
    int attack_cooldown_ms;
    int respawn_ms_remaining; /* only meaningful while !alive */
    int last_attacked_by_owner; /* -1 = never hit since spawning, else the owner index of whoever last damaged it -- who gets credit on the kill */
} ArenaCreep;

typedef struct {
    ArenaHero heroes[ARENA_MAX_HEROES];
    ArenaNode nodes[ARENA_NODE_COUNT];
    ArenaCreep creeps[ARENA_MAX_CREEPS];
    int winner; /* 0 = none yet, 1 = player/team 0, 2 = bot/team 1 */
} ArenaState;

extern ArenaState arena_state;

/* When nonzero (the default), arena_update() drives owner 1 via the
 * internal hand-authored bot brain (arena_bot_tick) every tick -- correct
 * for local single-player-vs-bot play (apps/arena's existing local mode).
 * apps/arena_server (2026-07-24 pivot, NORTHSTAR §13) sets this to 0 once a
 * real second client connects, so a real remote player's own move/cast
 * commands aren't immediately overwritten by the bot AI each tick. */
extern int arena_bot_enabled;

/* arena_init defaults to player=Unicorn, bot=Duck (S170-31) -- both slots
 * carry a real kit now, proving Phase D's "both sides" requirement, not
 * just a second player-selectable option. arena_init_with_heroes lets a
 * caller (tests, a future hero-select menu) pick explicitly. */
void arena_init(void);
void arena_init_with_heroes(ArenaHeroID player_hero, ArenaHeroID bot_hero);
void arena_update(unsigned int dt_ms);
void arena_set_move_target(int owner, float x, float z);
void arena_bot_tick(unsigned int dt_ms);

/* Team-mode entry points (2026-07-24, NORTHSTAR §13 cont'd): a real N-vs-N
 * match (up to ARENA_TEAM_SIZE per side). arena_init_teams sets up
 * ARENA_MAX_HEROES slots (team 0 = owners 0..ARENA_TEAM_SIZE-1, team 1 =
 * the rest), all defaulting to ARENA_HERO_UNICORN until each slot's real
 * client sends its own draft pick (apps/arena_server owns that protocol,
 * not this sim layer). arena_update_teams drives all active heroes each
 * tick via nearest-enemy targeting -- no internal bot AI involved (every
 * slot in team mode is filled by a real network client, human or bot). */
void arena_init_teams(void);
void arena_update_teams(unsigned int dt_ms);
ArenaHero *arena_nearest_enemy(int owner);

/* arena_nearest_ally (S170-45): the nearest active, living hero on the SAME
 * team as `owner`, excluding `owner` itself. Mirrors arena_nearest_enemy's
 * exact shape/NULL-safety, the enabling primitive for every ally-targeted
 * kit piece previously skipped for having no target in 1v1 (Ghost's R heal
 * side, Frog's W, Doc Wheel's entire kit). Returns NULL in 1v1 (no
 * teammate exists) or if owner has no living ally right now -- callers
 * must already be NULL-safe the same way they are for arena_nearest_enemy. */
ArenaHero *arena_nearest_ally(int owner);

/* arena_tick_nodes (S170-46, capture mechanic redesigned S170-50): advances
 * the Arathi Basin-style channel capture for every ArenaNode by dt_ms --
 * exclusive single-team presence within ARENA_NODE_CAPTURE_RADIUS starts or
 * continues that team's channel (flipping an enemy-owned node to neutral
 * immediately, not just on completion); mixed presence, a Pizza's
 * corruption, or the channeling team leaving interrupts it (progress lost,
 * owner unchanged -- no free revert); Flamel's Overgrowth mark
 * decays/refreshes and grants a channel-speed bonus on the marking team's
 * own capture. Called from both arena_update() (1v1, nodes[] already
 * positioned) and arena_update_teams(), same "generalizes cleanly, no
 * special-casing" precedent as arena_nearest_ally/arena_nearest_enemy. */
void arena_tick_nodes(unsigned int dt_ms);

/* arena_tick_creeps (S170-51): advances every jungle creep by dt_ms --
 * respawns a dead creep once its timer elapses (re-rolling flavor/HP from
 * its node's CURRENT owner, not whatever it was when it last spawned),
 * ticks its attack cooldown, and has it auto-attack the nearest hittable
 * hero within ARENA_CREEP_AGGRO_RADIUS if one's there (passive-until-
 * approached). Does not itself apply any hero-side damage to the creep or
 * grant kill rewards -- that's arena_hero_attack_creeps, called
 * separately so both halves of creep combat can be reasoned about
 * independently. Called from both arena_update() and arena_update_teams(). */
void arena_tick_creeps(unsigned int dt_ms);

/* arena_hero_attack_creeps (S170-51): each active, alive hero without a
 * closer enemy HERO in range instead auto-attacks a living creep within
 * ARENA_ATTACK_RANGE if one's there -- creeps are a secondary objective,
 * so a hero already trading blows with an enemy hero doesn't get split
 * attention. On a kill, applies the flavor-specific reward (see the
 * ARENA_MAX_CREEPS header comment) to the killer's team. Called from both
 * arena_update() and arena_update_teams(), after resolve_combat/the melee
 * loop so hero-vs-hero combat is always resolved first each tick. */
void arena_hero_attack_creeps(unsigned int dt_ms);

/* Kit casts dispatch on the hero's hero_id, not a hardcoded owner check
 * (S170-31 generalized this from S170-18's Unicorn-only version). No-ops
 * if the hero's kit doesn't have that ability, or if it's on cooldown. */
void arena_cast_q(int owner);
void arena_toggle_w(int owner);
void arena_cast_r(int owner);
float arena_hero_armor(const ArenaHero *h); /* effective armor, incl. Unicorn R's buff */

#endif
