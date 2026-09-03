#ifndef ARENA_GAME_H
#define ARENA_GAME_H

/* ARENA_HALF_EXTENT (S170-191, founder: "use golden ratio to expand map size"): 32.0 * phi
 * (1.618034), an explicit golden-ratio scale-up from the previous widen (20->28->32 through
 * S170-138/the earlier "a little bigger" pass) instead of another ad-hoc bump. Left as a real
 * expression, not a pre-computed literal, so the phi relationship to the old 32.0 is visible in
 * the code itself, not just a comment. Everything positioned relative to the map edge
 * (fountains, node layout, jungle obstacles, powerups) was re-derived or re-scaled alongside
 * this -- see each of their own S170-191 doc comments. */
#define ARENA_HALF_EXTENT (32.0f * 1.618034f) /* ~51.78 */
#define ARENA_HERO_SPEED 4.0f      /* units/sec */
#define ARENA_ATTACK_RANGE 1.6f
#define ARENA_ATTACK_DAMAGE 8
#define ARENA_ATTACK_COOLDOWN_MS 700
/* ARENA_ATTACK_WINDUP_MS (S170-204, NORTHSTAR §17.1's real League parity target: "the champion
 * cannot move during windup... issuing a move command mid-windup cancels the swing outright").
 * §17.5's own open-questions section names this exact ratio ("e.g. 25% windup / 75% backswing
 * of the existing 700ms ARENA_ATTACK_COOLDOWN_MS") as a reasonable first-pass approximation,
 * not confirmed final tuning -- used verbatim rather than inventing a different split. The
 * remaining ~75% ("backswing") needs no separate field: movement is already free the instant
 * this reaches 0 and attack_cooldown_ms starts counting down, which is exactly backswing's real
 * behavior (cancelable, doesn't undo the hit that already landed) with zero new state. */
#define ARENA_ATTACK_WINDUP_MS (ARENA_ATTACK_COOLDOWN_MS / 4)
/* Gary's ranged basic auto-attack (S170-162/163, NORTHSTAR §17's
 * click-to-attack system, team mode only): the first hero on this roster
 * whose plain auto-attack is a real homing projectile instead of the flat
 * melee-proximity tick every other hero still uses. Range chosen to read
 * as genuinely "ranged" against ARENA_ATTACK_RANGE's melee 1.6, and to
 * comfortably clear ARENA_CREEP_AGGRO_RADIUS (4.0)/ARENA_NODE_CAPTURE_RADIUS
 * so a Gary player can actually threaten a node from outside both. Speed
 * sits in the middle of real League's own basic-attack-projectile range
 * (roughly 1300-2200+ units/sec across the champion roster, per NORTHSTAR
 * §17.2) rescaled to this arena's much smaller map. */
/* 2026-07-30, founder: "double the range of gary auto attack and abilities" -- ARENA_GARY_ATTACK_
 * RANGE/Q_RANGE/W_RANGE/R_RANGE all doubled from their original values (6/6/9/6 -> 12/12/18/12).
 * Every call site already keys off these named constants rather than a hardcoded literal
 * (targeting checks, the homing auto-attack's own max_range, Q's projectile max_range, even the
 * bot AI's own decision distances), so doubling the #defines alone is sufficient -- confirmed by
 * reading every use site before changing this, not assumed.
 * 2026-07-30 follow-up, founder: "reduce garys range by 26%" -- applied as an additional * 0.74f
 * factor on top of the doubling above (12/12/18/12 -> 8.88/8.88/13.32/8.88), same "write the
 * scaling as a visible multiplier, not a pre-computed literal" convention this file already uses
 * elsewhere (e.g. the golden-ratio map-scale constants) so the history stays traceable. */
#define ARENA_GARY_ATTACK_RANGE (6.0f * 2.0f * 0.74f)
#define ARENA_GARY_ATTACK_SPEED 14.0f
#define ARENA_GARY_ATTACK_DAMAGE 7
#define ARENA_GARY_ATTACK_COOLDOWN_MS 900
#define ARENA_GARY_ATTACK_WINDUP_MS (ARENA_GARY_ATTACK_COOLDOWN_MS / 4) /* S170-204: same 25% ratio as the flat melee windup */
#define ARENA_NODE_COUNT 5 /* S170-119: was 2 -- real Arathi Basin has 5 (Stables/Farm/Blacksmith/Lumber Mill/Gold Mine) */

/* Static jungle terrain (NORTHSTAR §8, "add rocks and trees so we naturally
 * start to create some lanes"): rock/tree boxes -- same "boxes for now"
 * silhouette approach as the hero models below, not sculpted geometry.
 * Fixed layout, never mutated at runtime (no owner/HP/state), so unlike
 * nodes/creeps there's no wire sync -- apps/arena and apps/arena_server
 * both call arena_obstacles_reset_layout() from the same init path and end
 * up with identical local copies, one less thing for ArenaSnapshotMsg to
 * carry. Placed in two flank walls between each team's spawn column
 * (x=+-8) and the flank nodes (x=+-18, see arena_nodes_reset_layout),
 * spanning roughly z=-5.5..5.5 -- wide enough to block a straight line to
 * a flank node, but never touching the mid lane (heroes going straight to
 * the x=0 center node never cross x=+-9) or the 1v1 local demo's own
 * movement-test coordinates (spawn (-6,0)/(6,0), all test paths stay
 * within |x|<7). The result: reaching a flank node means routing around
 * the top or the bottom of the wall -- a top/bottom lane either side of a
 * jungle you can't walk through, same shape as the real MOBA reference
 * this map is already modeled on (NORTHSTAR §8's Arathi Basin comparison). */
typedef enum {
    ARENA_OBSTACLE_ROCK = 0,
    ARENA_OBSTACLE_TREE = 1,
} ArenaObstacleKind;
#define ARENA_OBSTACLE_COUNT 32 /* S170-191: was 22 -- scaled positions plus 10 new pieces, "add more jungle obstacles" to fill the golden-ratio-expanded map instead of leaving new open space empty */
#define ARENA_HERO_COLLISION_RADIUS 0.6f /* how close a hero's own footprint can get to an obstacle's edge before being pushed back out */

/* Healing fountains (S170-147). Founder: "add healing fountains at 2
 * corners of the map across from each other." Two static, fixed-position
 * healing zones at diagonally-opposite map corners -- a real MOBA fountain
 * ("go here to top off") rather than a passive regen tick. Deliberately
 * NEUTRAL, not team-exclusive: the founder's own wording asked for "2
 * corners... across from each other" (a real map-geography placement), not
 * "one per team's base" (which real MOBA fountains usually are) -- read as
 * a genuinely contestable resource, matching this map's existing "structures
 * are neutral/contestable" pattern (nodes, node-guardian creeps) rather than
 * guessing which team owns which corner. Flagged here as a real design
 * choice, not silently assumed, in case the founder actually meant
 * team-exclusive home-base fountains -- easy to flip later (gate the heal
 * on `hero->team == fountain_owner_team` instead of healing anyone) if so.
 * Positions are deterministic and computed identically client- and
 * server-side (same "no wire sync needed for a static layout" precedent
 * jungle obstacles already established) -- only the resulting HP change
 * needs to reach the client, and it already does via the existing hero HP
 * sync + S170-143's generic heal-flash (fires on ANY HP increase, any
 * source). */
#define ARENA_FOUNTAIN_COUNT 2
#define ARENA_FOUNTAIN_RADIUS 3.0f
#define ARENA_FOUNTAIN_HEAL_PER_SEC 15 /* strong, deliberate -- "go here to top off," not a passive trickle */
#define ARENA_FOUNTAIN_MANA_PER_SEC 15 /* S170-148, founder: "fountains should also restore mana" -- same rate as the heal, one consistent "resource top-off" spot */

/* Warsong Gulch-style powerups (S170-190, founder: "add berserker and health regen powerups
 * like from warsong gulch in between the nodes"). Real WoW WSG mechanic: two neutral map
 * pickups (Berserker = damage buff, Restoration = health-regen buff), grabbed by walking near
 * them, granting a timed buff on pickup; the pickup itself goes inactive and respawns after a
 * cooldown. Positioned between the node clusters (ArenaPowerupKind's own doc comment on
 * arena_powerup_position has the exact layout reasoning) -- neutral, contestable ground,
 * equidistant from both teams, same fairness principle the fountains/graveyards already
 * follow. Hero-only, same "narrower blast radius on purpose" scoping Tyler's clones are
 * already excluded from (fountains, node capture, creep targeting) -- one more system that
 * doesn't need touching to ship the core mechanic. */
typedef enum {
    ARENA_POWERUP_BERSERKER = 0,
    ARENA_POWERUP_REGEN = 1,
    ARENA_POWERUP_COUNT
} ArenaPowerupKind;

typedef struct {
    float x, z;
    ArenaPowerupKind kind;
    int active;                /* 1 = sitting on the map, ready to be picked up */
    int respawn_ms_remaining;  /* counts down while inactive; irrelevant while active */
} ArenaPowerup;

#define ARENA_POWERUP_PICKUP_RADIUS 2.0f
#define ARENA_POWERUP_RESPAWN_MS   60000 /* 60s -- real WSG's own ~90-120s scaled down for this game's faster match pace */
#define ARENA_POWERUP_BUFF_MS      20000 /* 20s -- same scaling reasoning */
#define ARENA_BERSERKER_BONUS_AD      15 /* flat, on top of item_bonus_ad -- same "flat bonus, not a multiplier" shape items already use */
#define ARENA_POWERUP_REGEN_HP_PER_SEC 8 /* faster than fountain healing (15/sec) is close-range, deliberate -- this is a portable, weaker version you carry with you */

/* Territorial dynamic node-guardian creeps (S170-51). Founder direction: territory
 * is the macro/economy layer, objectives (the team-wipe win condition) are
 * how the game is actually won, and gameplay should let territory control
 * shape what node-guardian creeps emerge -- "controlling the flavor and cadence of
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
#define ARENA_CREEP_TEAM_HP                 26 /* S170-161, founder: "tone down the strength of the team creeps just a bit they are so strong" -- was 40. Neutral untouched (still the rare, deliberately-tanky contested prize). */
#define ARENA_CREEP_NEUTRAL_RESPAWN_MS       30000 /* the rare prize -- slow cadence */
#define ARENA_CREEP_TEAM_RESPAWN_MS          12000 /* home-turf resupply -- fast cadence, rewards holding ground */
#define ARENA_CREEP_AGGRO_RADIUS             4.0f  /* passive-until-approached, same spirit as GFD's Rabbit */
#define ARENA_CREEP_NEUTRAL_DAMAGE           6
#define ARENA_CREEP_TEAM_DAMAGE              4 /* S170-161: same "tone down team creeps" pass as ARENA_CREEP_TEAM_HP above -- split out from the old single ARENA_CREEP_DAMAGE so neutral stays untouched */
#define ARENA_CREEP_ATTACK_COOLDOWN_MS       1500
/* ARENA_CREEP_MARCH_SPEED (S170-161), founder: "have the team creeps spawn and fan out from
 * owned nodes marching towards unowned nodes." A team-flavored creep is no longer a stationary
 * camp -- it continuously walks toward whichever node its own team doesn't currently own
 * (nearest one, recomputed live every tick so it reacts to ownership changing mid-march),
 * starting from its team's own graveyard rather than the node it's nominally attached to
 * (founder: "initially they spawn from the graveyards behind the nodes not the center"). Each
 * owned node's creep picks its own nearest target independently -- no coordination between
 * creeps needed for the aggregate effect to read as a real fan-out across the map. Slower than
 * ARENA_LANE_CREEP_SPEED (2.5) -- these are still meant to feel like a home-turf presence
 * projecting outward, not a lane-pushing wave. Idles in place once its own team owns every
 * node (nothing left to march toward). Neutral creeps are unaffected -- no home team to push
 * outward from, they stay put at their own node exactly as before. */
#define ARENA_CREEP_MARCH_SPEED              1.5f
#define ARENA_CREEP_MARCH_STOP_EPSILON       0.5f
#define ARENA_CREEP_NEUTRAL_KILL_CAPTURE_BONUS_MS 5000 /* big swing for winning the contested prize */
#define ARENA_CREEP_TEAM_KILL_HEAL                20   /* home-turf resupply, owning team only */
#define ARENA_CREEP_TEAM_KILL_DENY_CAPTURE_BONUS_MS 1500 /* counter-play: farming an enemy's own node-guardian creep helps flip their node */

/* Node towers (2026-07-30, founder: "add towers around the nodes so beginning of game is a
 * little slower"). One permanent, neutral tower per node (towers[n] belongs to nodes[n], same
 * index-matched convention as ArenaCreep), hostile to both teams equally -- unlike node-guardian
 * creeps, a tower has no flavor/owner, because the whole point is gating capture BEFORE anyone
 * owns anything. arena_tick_nodes checks tower->alive before it will let a channel start or
 * continue on that node at all: while the tower stands, the node can't flip to either team, full
 * stop, no exceptions. Real MOBA turret precedent (nobody caps a base objective while its own
 * defenses stand) directly matching the founder's own "slower start" framing. Never respawns once
 * destroyed -- this is a ONE-TIME early-game gate, not a recurring one; a torn-down tower's node
 * behaves exactly like it always has (arena_tick_nodes simply stops checking a dead tower).
 * Reuses the exact hero-vs-creep combat shape (flat damage in, apply_armor'd damage out, last-hit
 * kill credit via last_attacked_by_owner) rather than inventing a new one.
 *
 * 2026-07-30, founder real-time after first playtest: "towers are basically invincible and can
 * never be destroyed" -> "the color is changing scale down the tower max hp some its taking way
 * too long and the tower just kos even tanks." Two real, independent problems, both fixed
 * together: (1) HP was sized against ARENA_ATTACK_DAMAGE (8) without weighing it against a
 * hero's own base HP (100) -- 1600 meant 200 solo hits, effectively unkillable in a real match's
 * timescale. (2) 40 flat damage every ARENA_TOWER_ATTACK_COOLDOWN_MS is 40% of a hero's BASE hp
 * per hit -- a 2-3 shot kill on anyone, "tank" or not, this early. Rebalanced against the hero
 * HP scale directly rather than against other creep numbers: MAX_HP now costs a real early
 * commitment (roughly 30-45s of a lone hero's sustained fire, ~10-15s for 3 heroes focusing
 * together -- a genuine teamfight-or-skip decision, not a wall) without being a coinflip solo
 * dive; DAMAGE now chips (~15% of base HP per hit) rather than deletes, so a hero can trade a few
 * hits while working on it instead of getting 2-shot for trying. Still a judgment call pending
 * further live tuning, not re-derived from founder-specified numbers -- same "spec the model,
 * leave the numbers open" precedent NORTHSTAR §17.5/§20.4 already established. */
#define ARENA_TOWER_MAX_HP              420
#define ARENA_TOWER_DAMAGE              14
#define ARENA_TOWER_ATTACK_COOLDOWN_MS  1000
#define ARENA_TOWER_AGGRO_RADIUS        6.0f /* wider than ARENA_NODE_CAPTURE_RADIUS (5.0) -- already threatening before anyone gets close enough to even attempt a channel */
#define ARENA_TOWER_KILL_FLOW           400
#define ARENA_TOWER_KILL_XP             40
/* 2026-07-30, founder: "show the tower damage as projectiles" -- a tower's shot is now a real
 * travelling ArenaProjectile (same system Gary's homing auto-attack and every ability skill-shot
 * already use) instead of an instant, invisible hit, so a shot is visible and dodgeable in
 * principle rather than damage just silently appearing. Deliberately NON-homing (an ordinary
 * skill-shot toward the target's position at the moment the tower fires, same "ability shots grant
 * no kill credit" scope Tyler's Q/Ghost's Q already hold to) -- a tower has no real hero `owner`
 * to safely thread through the homing-reward path (arena_tick_projectiles only ever dereferences
 * `heroes[p->owner]` inside the homing branch, which staying non-homing never enters), and
 * ARENA_PROJECTILE_NO_OWNER below tells the client draw code to skip its own owner-based
 * self/ally/enemy color lookup and use a fixed neutral tower-shot color instead. Speed/radius
 * borrow Gary's own basic-attack numbers as a reasonable "ranged auto-attack" reference point --
 * max_range is a hair past ARENA_TOWER_AGGRO_RADIUS so a shot at a target right at the edge of
 * aggro range never legitimately whiffs from falling just short. */
#define ARENA_TOWER_PROJECTILE_SPEED    14.0f
#define ARENA_TOWER_PROJECTILE_RADIUS   0.6f
#define ARENA_TOWER_PROJECTILE_MAX_RANGE (ARENA_TOWER_AGGRO_RADIUS + 2.0f)
/* Sentinel ArenaProjectile.owner value meaning "no owning hero" (a tower's shot, not a hero's) --
 * 255 fits the wire snapshot's uint8_t and is always outside the real 0..ARENA_MAX_HEROES-1 range,
 * so both a stale `heroes[owner]` read attempt and the client's own bounds check can treat it as an
 * unambiguous "not a real hero slot" signal. */
#define ARENA_PROJECTILE_NO_OWNER       255

/* Team-scale arena (2026-07-24, NORTHSTAR §13 cont'd): the array grows from
 * 2 to ARENA_MAX_HEROES so a full 10v10 match fits in the same ArenaState
 * the 1v1 local demo and apps/arena_server (1v1) already use. The 1v1 path
 * (arena_init/arena_init_with_heroes) still only ever populates heroes[0]/
 * [1] and leaves the rest zeroed/inactive -- see the `active` field below.
 * ARENA_TEAM_SIZE was briefly 7 (7v7, S170-178) then reverted -- founder,
 * real-time: "ok move back to 10 v 10" (S170-183), mid a live-pool
 * queueing investigation. A single #define change, every other
 * array/wire-struct size in this codebase derives from ARENA_MAX_HEROES
 * rather than hardcoding 10 or 20. */
#define ARENA_TEAM_SIZE 10
#define ARENA_MAX_HEROES (ARENA_TEAM_SIZE * 2)

/* ARENA_MAX_CLONE_SLOTS/ARENA_HEROES_ARRAY_SIZE (S170-141, Tyler's puppet
 * clones): a small pool of extra hero slots APPENDED after the real
 * per-player range (0..ARENA_MAX_HEROES-1, always exactly claimed by real
 * connected clients in every lobby size this codebase actually runs --
 * always either 2 or ARENA_MAX_HEROES, never partial) so a puppet clone
 * never competes with an actual connecting client for a slot. Real owner
 * indices (draft picks, PACKET_ARENA_PICK, the wire snapshot) never reach
 * into this range -- it exists purely for arena_state.heroes[]' own
 * simulation-side bookkeeping, not networking (ARENA_SNAPSHOT_MAX_HEROES in
 * protocol.h stays at ARENA_MAX_HEROES, unchanged; clones aren't wire-synced
 * yet, same "sim-only for now" precedent as node-guardian/lane creeps). Sized for
 * up to 4 simultaneous Tyler R casts' worth of clones -- generous headroom,
 * not a hard design target. */
#define ARENA_MAX_CLONE_SLOTS 8
#define ARENA_HEROES_ARRAY_SIZE (ARENA_MAX_HEROES + ARENA_MAX_CLONE_SLOTS)

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
    ARENA_HERO_BACON_PUCK = 14, /* TYLER multiverse_heroes.md #5 + #67, merged (S170-94) */
    ARENA_HERO_ABRAHAM = 15, /* TYLER multiverse_heroes.md #113, "Abraham of Worms, the Mage" (S170-103) */
    ARENA_HERO_ADA = 16, /* TYLER multiverse_heroes.md #112, "Ada Lovelace, Pilot" (S170-103) */
    ARENA_HERO_TYLER = 17, /* docs/HEROES_VS0.md's own pre-existing design, never implemented until now (S170-111) */
    ARENA_HERO_PAIMON = 18, /* TYLER multiverse_heroes.md #20, "Paimon, the Court Voice", channeled by the real John Dee (S170-55) */
    ARENA_HERO_NOOR1 = 19, /* TYLER multiverse_heroes.md #3, "NOOR-1 (Four Days Behind)", in-game form: a snowman (S170-104) */
    ARENA_HERO_CAIN = 20, /* TYLER multiverse_heroes.md #80, "Cain, East of Eden" (S170-105, founder: "replace adelle with Cain") */
    ARENA_HERO_GUNNR = 21, /* TYLER multiverse_heroes.md #30, "Gunnr, Who Argued With a Raven" (S170-93) */
    ARENA_HERO_VASSAGO = 22, /* TYLER multiverse_heroes.md #16, "Vassago, the Soft Foresight" (S170-93); also real TYLER canon, Goetia 11.11 Hz */
    ARENA_HERO_HE_XIANGU = 23, /* TYLER multiverse_heroes.md #39, "He Xiangu, Who Stopped Eating" (S170-93) */
    ARENA_HERO_BELETH = 24, /* TYLER multiverse_heroes.md #14, "Beleth, the Detonation" (S170-93) */
    ARENA_HERO_MNM = 25, /* TYLER multiverse_heroes.md #114, "MnM, the Shapeshifting Crab" (S170-134) */
    ARENA_HERO_WEATHERMAN = 26, /* TYLER multiverse_heroes.md #45, "Ao Guang's Weather-Debt Collector" (S170-206, NORTHSTAR §16.2) */
    ARENA_HERO_ZAGAN = 27, /* TYLER multiverse_heroes.md #19, "Zagan, the Standstill's Confessor" (S170-230) */
    ARENA_HERO_WARRIOR = 28, /* GoblinFoxDragon REDGARDEN_GUI_NORTHSTAR.md Milestone 1: not a TYLER
                                 hero -- DragonsNShit's Warrior job, ported as Battlegrounds
                                 content. Appended to this enum as the cheapest correct home for
                                 it until Milestone 3's real job-select entry point lands; the
                                 job-vs-hero pick-screen distinction is that milestone's problem,
                                 not this one's. */
    ARENA_HERO_CART = 29, /* TYLER multiverse_heroes.md #10, "The Retrieval Cart" (NORTHSTAR §24
                              Milestone 2, 2026-07-31). Founder pick from §7's own queue -- the
                              one entry never built. multiverse_heroes.md's own 2026-07-23
                              gameplay note names the real design constraint: the Cart's whole
                              identity is delivering something nobody asked for, on its own
                              schedule, and "nobody, including its own controller, gets to
                              request what" -- the opposite of direct unit command. Founder
                              confirmed (AskUserQuestion, this session): Indirect-Control, true to
                              lore, same shape §16.1 already built for Donkey -- not a WC3-style
                              directly-commanded unit. §24's own Milestone 2 goal (a real
                              directly-controlled-unit hero) stays open after this. */
} ArenaHeroID;
#define ARENA_HERO_COUNT 30

/* The Unicorn — first real hero kit wired in (S170-18). */
#define ARENA_UNICORN_ARMOR         4    /* passive: Chassis Claim, flat dmg reduction */
#define ARENA_UNICORN_Q_DASH_DIST   4.0f /* Diagnostic Charge */
#define ARENA_UNICORN_Q_DAMAGE      12
#define ARENA_UNICORN_Q_HIT_RADIUS  1.8f
#define ARENA_UNICORN_Q_COOLDOWN_MS 4000
#define ARENA_UNICORN_W_REGEN_PER_SEC 6  /* Spaghetti Vent, while toggled on */
#define ARENA_UNICORN_R_COOLDOWN_MS 15000
#define ARENA_UNICORN_R_DURATION_MS 3000 /* Full Disclosure: armor doubled */

/* The Duck — second hero kit (S170-31). Originally Q/R only: W (Government
 * Clearance, needs towers/objective structures that don't exist in this 1v1
 * arena) and E (Chosen One, triggers on a killing blow that also ends the
 * match, so the buff window would have zero observable effect) were both
 * skipped, not faked. W's own empty slot is filled for real by S202-10
 * below -- a different, new ability, not a late implementation of
 * Government Clearance (that gap is still real and still unaddressed). */
#define ARENA_DUCK_Q_PULL_DIST      5.0f /* Telekinetic Yank: how far the foe gets pulled */
#define ARENA_DUCK_Q_DAMAGE         10
#define ARENA_DUCK_Q_RANGE          6.0f /* max distance the yank can reach */
#define ARENA_DUCK_Q_COOLDOWN_MS    5000
#define ARENA_DUCK_R_PULL_DIST      9.0f /* Total Telekinesis: bigger yank */
#define ARENA_DUCK_R_DAMAGE         20
#define ARENA_DUCK_R_RANGE          9.0f
#define ARENA_DUCK_R_COOLDOWN_MS    18000

/* Duck W -- Smoke Bomb (S202-10). Founder real-time: "ok do fog of war as an
 * ability" -> "add to the duck" -> "there is no natural fog of war just duck
 * smoke bomb" -> "server authorittive" -> "as a parena mod" -> "mod first
 * dev." This engine has no vision/fog-of-war/line-of-sight system anywhere
 * (the King All-Seeing buff's own doc comment already names that as a real,
 * out-of-scope gap) -- Smoke Bomb doesn't invent one. It implements the one
 * concrete, honest analog that's actually buildable on top of this engine's
 * real targeting primitive (arena_nearest_enemy): a hero standing inside an
 * active cloud can't be selected as a target by anyone casting from OUTSIDE
 * that same cloud (hero_obscured_from, arena_game.c). Always-lands AoE,
 * self-centered at cast time (no click-to-place targeting exists in this
 * input model, same "here is the only honest landing spot" reasoning
 * arena's other zone abilities already use), same instant-cast-on-cooldown
 * shape as Flamel/Dagda's own W. */
#define ARENA_DUCK_W_RADIUS         4.5f  /* Smoke Bomb: cloud radius */
#define ARENA_DUCK_W_DURATION_MS    6000  /* Smoke Bomb: how long the cloud lingers */
#define ARENA_DUCK_W_COOLDOWN_MS    16000

/* Smoke Bomb slow (S205-87, kanban priority-queue card: "duck smoke bomb should have a 50%
 * chance to slow each enemy hit by it"). Rolled independently per enemy caught in the cast-time
 * radius check below (redgarden_host_duck_smoke_bomb_cast) -- "each enemy hit by it" means each
 * enemy standing inside the cloud at the moment it's thrown, the only real "hit" this ability
 * has (it has no damage/tick component, purely vision-blocking otherwise). Duration/pct values
 * follow the exact same real precedent ARENA_CART_DELIVERY_SLOW_MS/_PCT already established. */
#define ARENA_DUCK_W_SLOW_CHANCE_PCT 50     /* integer 0-100: 50% independent roll per enemy hit */
#define ARENA_DUCK_W_SLOW_MS         2000
#define ARENA_DUCK_W_SLOW_PCT        0.30f

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
/* S170-140: Alien Frequency is explicitly documented as a skillshot
 * (docs/HEROES_VS0.md) but was still an instant hit-if-in-range check --
 * the second real travelling projectile in this arena, same convention as
 * Gary's Q (S170-136). Faster than Gary's shot (a "frequency," not a bullet)
 * -- reads as a quick zap rather than a slow, dodgeable sniper round. */
#define ARENA_GHOST_Q_PROJECTILE_SPEED  18.0f
#define ARENA_GHOST_Q_PROJECTILE_RADIUS 0.5f
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
 * R is a fixed-duration root on the nearest enemy -- "slow down, this isn't a
 * track meet," simplified to a full stop the same way Tree's R/Flamel's R already
 * simplify a slow down to a root rather than adding a real speed-multiplier system.
 *
 * W -- Aimed Shot (S170-203, founder: "switch gary w to aimed shot just like wow hunter
 * cast time big damage for now movement interrupts cast damage does not interrupt cast
 * silence does"). Was a free toggle extending Q's own range; now a real WoW Hunter-style
 * cast-time nuke, the first ability in this roster to use the generic casting_slot/
 * cast_time_remaining_ms/cast_anchor_x/z/cast_target state (arena_game.h's own doc comment
 * on those fields). Real commitment, same "Gary needs a shot lined up to fire at all"
 * convention his Q already holds itself to -- requires a hittable foe in range at cast
 * START, or the ability simply doesn't fire (no cooldown/mana spent), and the target is
 * re-checked (still alive, still in range of where the cast began) only at completion, not
 * continuously -- stepping out of range mid-cast still costs Gary the cast, exactly like a
 * foe dodging his Q's travel time. */
#define ARENA_GARY_Q_RANGE          (6.0f * 2.0f * 0.74f) /* 2026-07-30: doubled, then reduced 26%, see ARENA_GARY_ATTACK_RANGE's own doc comment */
#define ARENA_GARY_Q_DAMAGE         11
#define ARENA_GARY_Q_COOLDOWN_MS    3500
#define ARENA_GARY_W_RANGE          (9.0f * 2.0f * 0.74f) /* 2026-07-30: doubled, then reduced 26%, see ARENA_GARY_ATTACK_RANGE's own doc comment -- same reach the old Q-range toggle used to grant, now also rescaled */
#define ARENA_GARY_W_DAMAGE         30    /* real burst -- comparable to a strong R-tier ultimate elsewhere on the roster, not a Q-tier poke */
#define ARENA_GARY_W_CAST_MS        1500  /* stand-still wind-up; interruptible the whole time */
#define ARENA_GARY_W_COOLDOWN_MS    6000
/* S170-136: Q is now a real travelling projectile (first one in the game),
 * not an instant hit -- fired straight at the foe's position at cast time,
 * no homing, so a foe that moves off the line after the shot is fired
 * genuinely dodges it. Speed is fast enough to still read as "precision
 * shot" but slow enough (relative to ARENA_HERO_SPEED) that sidestepping is
 * a real, learnable counterplay: at 14 u/s over up to 9 units of range, the
 * shot is in the air for up to ~0.64s, in which a hero moving at 4 u/s can
 * shift ~2.5 units off the original line. */
#define ARENA_GARY_Q_PROJECTILE_SPEED  14.0f
#define ARENA_GARY_Q_PROJECTILE_RADIUS 0.6f
#define ARENA_GARY_R_RANGE          (6.0f * 2.0f * 0.74f) /* 2026-07-30: doubled, then reduced 26%, see ARENA_GARY_ATTACK_RANGE's own doc comment */
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

/* TYLER (S170-111) -- docs/HEROES_VS0.md already wrote this kit as "an exact copy of Meepo's
 * classic kit... reskinned as TYLER," including the original OG clone-death rule: every clone
 * shares one pool of fate, one dies, all die. That's not buildable as written on this engine
 * -- ArenaHero slots are one-per-connected-client, not multi-entity-per-player, and adding real
 * clone spawning would mean touching the draft/pick/connection model this whole roster depends
 * on. Simplified, documented here rather than silently narrowed the way every other "doesn't
 * fit this engine" gap in this roster already is (Frog's R, Tree's R, Courier's cleansed-debuff
 * passive): Q keeps Earthbind's root+setup role, W keeps Poof's blink-and-strike shape, E's
 * "geostrike on every melee attack" folds into Q's DoT since there's no generic per-attack
 * status hook to hang a real passive off, and R keeps the actual point of "Divided We Stand" --
 * real risk/reward -- as a self-buff that hits harder while making Tyler take more damage for
 * its duration (his own armor goes negative), rather than literal clones sharing literal HP. */
#define ARENA_TYLER_Q_DAMAGE          8
#define ARENA_TYLER_Q_RANGE           4.5f
#define ARENA_TYLER_Q_ROOT_MS         1600
#define ARENA_TYLER_Q_BURN_DPS        3
#define ARENA_TYLER_Q_BURN_MS         3500
#define ARENA_TYLER_Q_COOLDOWN_MS     4200
/* S170-140: Earthbind's own original-design wording ("Fires a net at a
 * target area," docs/HEROES_VS0.md) is a real thrown-object skillshot, not
 * an instant hit -- the third real travelling projectile in this arena.
 * Slower than Ghost's zap (a thrown net, not a beam) -- the root+burn payoff
 * is real counterplay-able, matching the same "dodgeable, not guaranteed"
 * bar Gary's Q set. */
#define ARENA_TYLER_Q_PROJECTILE_SPEED  10.0f
#define ARENA_TYLER_Q_PROJECTILE_RADIUS 0.7f
#define ARENA_TYLER_W_DAMAGE          12
#define ARENA_TYLER_W_COOLDOWN_MS     5500
#define ARENA_TYLER_R_DAMAGE          16
#define ARENA_TYLER_R_RANGE           4.0f
#define ARENA_TYLER_R_VULNERABLE_MS   3500 /* r_active_ms window: Tyler's own armor goes negative for this long */
#define ARENA_TYLER_R_NEGATIVE_ARMOR  6.0f
#define ARENA_TYLER_R_COOLDOWN_MS     19000
/* S170-141: real puppet clones, on top of the existing self-buff -- see
 * docs/HEROES_VS0.md's Tyler section for the full design/scope note.
 * ARENA_TYLER_R_CLONE_COUNT is a deliberate simplification of the OG kit's
 * "up to 5" (Divided We Stand can be cast more than once in the original;
 * this arena's R is a single-cast-per-cooldown ability like every other R,
 * so a fixed, modest count per cast reads better than trying to replicate
 * stacking casts). ARENA_TYLER_CLONE_HP_PCT matches the OG kit's "each with
 * a percentage of TYLER's stats." */
#define ARENA_TYLER_R_CLONE_COUNT     2
#define ARENA_TYLER_CLONE_HP_PCT      0.5f

/* Bacon+Puck, merged (S170-94, TYLER multiverse_heroes.md #5 + #67) -- Bacon's whole
 * character is withholding ("custodian of the one location nobody's allowed to know yet,"
 * seed phrase "ask again later"); Puck's is an unresolved duality between two versions of
 * himself nobody can confirm is the real one. Combined kit: Q is a real "ask again later" --
 * self intangible_ms, the shared can't-be-hit status effect (S170-32) -- and W (Puck's
 * duality) is a free toggle that extends how long the secret stays withheld, i.e. Q's own
 * intangibility duration, rather than granting a stat like most toggles. R pays off the
 * mischief: real damage plus a self-heal off it, "the trick was always the same" either way. */
#define ARENA_BACON_PUCK_Q_INTANGIBLE_MS          1500
#define ARENA_BACON_PUCK_Q_INTANGIBLE_MS_WATCHING 3000 /* dead since the 2026-08-26 W redesign (Shadow Step) -- see bacon_puck_cast_q's own doc comment */
#define ARENA_BACON_PUCK_Q_COOLDOWN_MS             6000
/* Shadow Step (2026-08-26, founder: "make bacon buck w instead of a toggle have it turn into
   sghadow step use the targeting system you had for abraham fireball before we changed it" ->
   "but have it click on a hero to teleport roughly behind them" -> "add a sense of hero
   direction i guess so we can actually teleport behind them" -> "hive it a generous range but
   not crazy like give it the same range as the ranged auto attacks"): replaces the old
   toggle. Reuses the client's own ground-targeting reticle/aiming-mode machinery (left in
   place, not ripped out, when Abraham's W moved off it) -- but the confirm click now detects a
   HOVERED HERO (g_hover_target, the same mechanism the plain click-to-attack flow already
   uses), not a ground point. */
#define ARENA_BACON_PUCK_W_RANGE ARENA_GARY_ATTACK_RANGE /* "the same range as the ranged auto attacks," literally */
#define ARENA_BACON_PUCK_W_BEHIND_OFFSET 2.0f /* how far past the target's own position, along their real facing_rad, the blink lands */
#define ARENA_BACON_PUCK_W_COOLDOWN_MS 8000
#define ARENA_BACON_PUCK_R_RANGE                   2.2f
#define ARENA_BACON_PUCK_R_DAMAGE                  16
#define ARENA_BACON_PUCK_R_HEAL_PCT                0.5f /* fraction of R's damage returned as self-heal */
#define ARENA_BACON_PUCK_R_COOLDOWN_MS              15000

/* Abraham of Worms, the Mage (S170-103, TYLER multiverse_heroes.md #113) -- a caster whose
 * whole real-world hook is a book whose ritual made a real man (Crowley) organize a life
 * around it. Q is a real ranged magic bolt. R is "the Guardian Angel, contacted": a full
 * self-cleanse (every debuff field this roster tracks) plus a real heal, the ritual's
 * actual real-world promised payoff.
 *
 * W rework (S202-34/S202-30, founder real-time: "xehingu [He Xiangu, unrelated] ... give
 * abraham a real targetable slow moving projectile fireball that moves a long distance and
 * damages enemies it passes through, replace his dumbest ability" -> "usually w"): the old
 * W was a free toggle whose only job was boosting Q's damage (ARENA_ABRAHAM_Q_DAMAGE_CHANNELING)
 * -- judged the weaker/more redundant half of the kit vs. Q's own always-useful poke, so W is
 * what got replaced, not Q. Q now always deals the old "channeling" damage value (there's no
 * more toggle to gate it on) -- a deliberate, stated choice to net-buff Q rather than silently
 * leave it worse off after W's removal. New W ("A Line of Fire," a ground-targeted skill-shot):
 * click-to-aim (client shows a green reticle after pressing W, per founder: "the targeter is
 * green when you are ready to cast"), no real range limit ("just have it go whatever direction
 * is the click"), pierces every enemy it touches rather than stopping on the first hit ("damages
 * enemies it passes through"), and is intentionally slow (ARENA_HERO_SPEED-relative) so landing
 * it is a real read on the target's movement, not a guaranteed poke. A 400ms pre-cast windup
 * plays a GOLDENBAND-driven ease-in/out hero-facing rotation (assets/anim/rotation_ease.gband,
 * PARENA's abraham_fireball_mod -- see arena_toggle_w's ARENA_HERO_ABRAHAM case and
 * tick_hero_kit's completion branch) plus a client-side squish/flick animation (founder:
 * "squish way down... to about 20 percent... windup animation go medium fast and then snap up
 * quite quick as a flick... take .4 seconds") -- see apps/arena/src/main.c's own
 * ABRAHAM_FIREBALL_SQUISH_DOWN_MS/UP_MS split using the same golden-ratio constant
 * (1.618034f) ARENA_HALF_EXTENT above already establishes as this repo's own convention for
 * "explicit, visible golden-ratio scaling" rather than an ad-hoc split. */
#define ARENA_ABRAHAM_Q_DAMAGE            15 /* was 9 base / 15 "channeling" -- always the higher value now, see doc comment above */
#define ARENA_ABRAHAM_Q_RANGE             5.5f
#define ARENA_ABRAHAM_Q_COOLDOWN_MS       3200
#define ARENA_ABRAHAM_R_HEAL              20
#define ARENA_ABRAHAM_R_COOLDOWN_MS       17000
#define ARENA_ABRAHAM_FIREBALL_DAMAGE      14
#define ARENA_ABRAHAM_FIREBALL_SPEED       4.0f    /* units/sec -- bumped from 3.0 (2026-08-26, founder: "make the fireball move just a bit faster") to match ARENA_HERO_SPEED exactly -- still a real dodgeable read against a stationary target, just no longer slower than a hero can walk */
#define ARENA_ABRAHAM_FIREBALL_RADIUS      0.9f
#define ARENA_ABRAHAM_FIREBALL_COOLDOWN_MS 9000
#define ARENA_ABRAHAM_FIREBALL_WINDUP_MS   400     /* matches assets/anim/rotation_ease.gband's real baked duration (16 ticks @ 40Hz) exactly */
#define ARENA_ABRAHAM_FIREBALL_MAX_RANGE   (ARENA_HALF_EXTENT * 4.0f) /* "no real range limit" -- comfortably longer than any real line of sight across the whole map (ARENA_HALF_EXTENT corners to corner) rather than a literal infinite/unbounded travel distance, which arena_spawn_projectile's own max_range field isn't designed to represent */
/* Ignite (2026-08-26, founder: "make it so that the fireball ignites the enemies it touches
   making them have burning too"): a real burn DoT on every hit, using the existing generic
   on_hit_burn_ms/on_hit_burn_dps ArenaProjectile mechanic (Pizza's Q, Flute Debt's Q, Tyler's Q
   all already apply burn the same way) -- the hit-resolution code that applies it already runs
   once per enemy a piercing shot passes through, so this needed no new plumbing, only setting
   the two fields on the fireball's own projectile at spawn. Same duration/DPS as Pizza's own Q
   burn (a real, established "how strong is a burn tick" baseline in this catalog), not a new
   number invented from nothing. */
#define ARENA_ABRAHAM_FIREBALL_BURN_MS     3000
#define ARENA_ABRAHAM_FIREBALL_BURN_DPS    5
/* Abraham's ranged basic auto-attack (S202-34, founder: "make his auto attack ranged like
 * garys with a different color projectile"): reuses Gary's exact homing-auto-attack mechanic
 * (ArenaProjectile.homing_target, see that field's own doc comment) rather than inventing a
 * second one -- only the range/speed/damage/color differ. Client picks the projectile's visual
 * color from hero_id already carried on ArenaProjectile, so "different color" needs no new
 * wire field, just a new client-side color branch. */
#define ARENA_ABRAHAM_ATTACK_RANGE ARENA_GARY_ATTACK_RANGE
#define ARENA_ABRAHAM_ATTACK_SPEED ARENA_GARY_ATTACK_SPEED
#define ARENA_ABRAHAM_ATTACK_DAMAGE ARENA_ATTACK_DAMAGE
#define ARENA_ABRAHAM_ATTACK_COOLDOWN_MS ARENA_ATTACK_COOLDOWN_MS
#define ARENA_ABRAHAM_ATTACK_WINDUP_MS (ARENA_ABRAHAM_ATTACK_COOLDOWN_MS / 4)

/* Ada Lovelace, Pilot (S170-103, TYLER multiverse_heroes.md #112) -- "wrote the operating
 * logic for a frame before the frame existed." A heavy, deliberate tank/controller: Q
 * computes the nearest enemy's movement to a halt (a real root, matching Tree's/Flamel's
 * "slow simplified to a stop" convention), W is a free-toggle armor bonus (the frame's own
 * plating, same shape as Loki's but a different hero's reason for it), R is the engine
 * finally executing: a real burst of damage plus a short follow-up root, "the first
 * program, run a century late." */
#define ARENA_ADA_Q_RANGE          5.0f
#define ARENA_ADA_Q_ROOT_MS        1800
#define ARENA_ADA_Q_COOLDOWN_MS    6000
#define ARENA_ADA_W_ARMOR_BONUS    6
#define ARENA_ADA_R_RANGE          2.5f
#define ARENA_ADA_R_DAMAGE         18
#define ARENA_ADA_R_ROOT_MS        1200
#define ARENA_ADA_R_COOLDOWN_MS    16000

/* Paimon (channeled by John Dee) -- nineteenth hero kit (S170-121, docs/HEROES_VS0.md). Passive
 * (Keeping the Peace) is an always-on silence aura, same aura_tick_ms pattern as Pizza's burn
 * (S170-46). Q (Teaches All Arts) is an instant-hit-if-in-range damage+root, same simplification
 * as Ghost/Tree/Flamel's Q. W (Speaks With Total Authority) is an instant damage+silence decree,
 * same shape as Ghost's Q but on the W slot with its own cooldown. R (Two Hundred Legions) is a
 * fixed zone dealing periodic damage to enemies and healing allies, same shape as Ghost's
 * Recital/Flamel's Elixir of Wild Growth. */
#define ARENA_PAIMON_PASSIVE_AURA_RADIUS   3.5f
#define ARENA_PAIMON_PASSIVE_SILENCE_MS    800
#define ARENA_PAIMON_PASSIVE_INTERVAL_MS  4000 /* "periodically," not every tick like Pizza's DPS aura -- talking a fight down takes longer than burning */
#define ARENA_PAIMON_Q_RANGE                5.5f
#define ARENA_PAIMON_Q_DAMAGE               9
#define ARENA_PAIMON_Q_ROOT_MS              1400
#define ARENA_PAIMON_Q_COOLDOWN_MS          4500
#define ARENA_PAIMON_W_RANGE                6.0f
#define ARENA_PAIMON_W_DAMAGE                7
#define ARENA_PAIMON_W_SILENCE_MS           1800
#define ARENA_PAIMON_W_COOLDOWN_MS          8000
#define ARENA_PAIMON_R_RADIUS                4.5f
#define ARENA_PAIMON_R_DURATION_MS          4000
#define ARENA_PAIMON_R_DPS                    6
#define ARENA_PAIMON_R_HEAL_PER_TICK          6
#define ARENA_PAIMON_R_COOLDOWN_MS         26000

/* NOOR-1 (S170-104, "add NOOR-1 as a snowman"): passive periodic-silence aura (same idiom as
 * Pizza's/Paimon's), Q a ranged damage+root bolt, W a self-cast intangibility on its own
 * cooldown (same mechanic as Ghost's Not a Ghost, themed as "sent in clean" -- going quiet and
 * unreadable herself), R a fixed cold zone dealing periodic damage to enemies, same shape as
 * Ghost's Recital/Paimon's Two Hundred Legions but with no ally-heal side -- "do not approach"
 * is a one-sided instruction. */
#define ARENA_NOOR1_PASSIVE_AURA_RADIUS    3.5f
#define ARENA_NOOR1_PASSIVE_SILENCE_MS      700
#define ARENA_NOOR1_PASSIVE_INTERVAL_MS    4000
#define ARENA_NOOR1_Q_RANGE                  6.0f
#define ARENA_NOOR1_Q_DAMAGE                 8
#define ARENA_NOOR1_Q_ROOT_MS              1300
#define ARENA_NOOR1_Q_COOLDOWN_MS          4500
#define ARENA_NOOR1_W_INTANGIBLE_MS        1500
#define ARENA_NOOR1_W_COOLDOWN_MS         10000
#define ARENA_NOOR1_R_RADIUS                  4.0f
#define ARENA_NOOR1_R_DURATION_MS          4000
#define ARENA_NOOR1_R_DPS                     7
#define ARENA_NOOR1_R_COOLDOWN_MS         24000

/* Cain (S170-105, "replace adelle with Cain"): passive flat armor bonus, always on -- "the man
 * cast out to wander settled down and built civilization anyway," the one thing about him that's
 * permanent (same "always-on flat armor" shape as Unicorn's own passive, minus the R-doubling).
 * Q an execute-scaled damage bolt ("the first murder," same shape as Morrigan's Q -- a killing
 * blow that gets easier the closer the target already is to death). W a self-dash directly AWAY
 * from the nearest enemy plus a self-debuff cleanse ("cursed to wander," the mirror of Courier's
 * Q dash-toward). R a survive-floor panic button, same shape as Pizza's/Loki's R -- "a mark that
 * is a curse and a protection at the same time," made literal: for its duration he cannot be
 * killed, even by the thing that marked him. */
#define ARENA_CAIN_PASSIVE_ARMOR            4
#define ARENA_CAIN_Q_RANGE                  6.0f
#define ARENA_CAIN_Q_DAMAGE_BASE            8   /* at 100% target HP */
#define ARENA_CAIN_Q_DAMAGE_LOW_HP         18   /* at ~0% target HP -- an execute */
#define ARENA_CAIN_Q_COOLDOWN_MS         4200
#define ARENA_CAIN_W_DASH_DIST              4.0f
#define ARENA_CAIN_W_COOLDOWN_MS         9000
#define ARENA_CAIN_R_FLOOR_MS            3800
#define ARENA_CAIN_R_COOLDOWN_MS        27000

/* Gunnr (S170-93): passive flat armor bonus, always on -- "quietly been right about three more
 * things," the shieldmaiden's stance, same shape as Cain's own passive. Q a melee-range direct
 * strike, no status effect -- "argued with a raven and was right," a plain correction, not a
 * flourish. R an execute-scaled burst, same shape as Morrigan's/Cain's Q -- "Valhalla has yet to
 * admit it," the vindication finally landing hardest against a target who's already nearly beaten.
 *
 * 2026-07-31, founder: "give gunnrs e a stun" -- REDGARDEN only has three cast slots (Q/W/R, no
 * fourth), read as R, the third/final slot, matching the LoL-style Q/W/E/R mental model minus
 * this roster's own missing 4th slot. R now also stuns whatever it hits, on top of its existing
 * execute-scaled damage -- same target, same ARENA_GUNNR_R_RANGE check already there, no new
 * targeting pass. Duration copied from Zagan's own W (The Standstill, S170-230, this roster's
 * first-ever `arena_apply_stun` call) rather than invented, as an independently-tunable literal
 * per this file's own established "copy the number, name it separately" convention.
 *
 * 2026-07-30, founder: "gunnr w switch it to consecration just like wow" -- W was a free toggle
 * self-regen (ARENA_GUNNR_W_REGEN_PER_SEC, removed); now a real WoW Paladin Consecration: a
 * ground zone at Gunnr's own feet, on a real cooldown, that damages any enemy standing in it every
 * second for its duration -- a shieldmaiden holding ground, not kiting for sustain. Reuses the
 * exact r_zone_x/z/r_active_ms/r_zone_tick_ms fields and arena_hero_r_zone_radius dispatch every
 * other zone ability (Ghost/Flamel/Morrigan/Paimon/NOOR-1/Vassago/He Xiangu's own R's) already
 * shares -- a zone is a zone regardless of which slot cast it, same "reuse the existing shape"
 * discipline this file already holds itself to elsewhere; Gunnr's is simply the first one
 * triggered from W instead of R. Founder, same-turn follow-up: "same dot cast radius cd" -- DPS/
 * radius/duration/cooldown copied from Ghost's own R zone (ARENA_GHOST_R_DPS/RADIUS/
 * DURATION_MS/COOLDOWN_MS, the simplest existing "flat DPS zone, no extra mechanic" template --
 * unlike Ghost's own version, Gunnr's has no ally-heal side, matching real Consecration's
 * enemies-only damage). Copied as literal values into Gunnr's own named constants rather than
 * aliased to Ghost's, so the two stay independently tunable later. */
#define ARENA_GUNNR_PASSIVE_ARMOR            4
#define ARENA_GUNNR_Q_RANGE                  2.2f  /* melee range -- close, not a skillshot */
#define ARENA_GUNNR_Q_DAMAGE                10
#define ARENA_GUNNR_Q_COOLDOWN_MS         3200
#define ARENA_GUNNR_W_RADIUS                 4.0f  /* Consecration -- same as ARENA_GHOST_R_RADIUS */
#define ARENA_GUNNR_W_DURATION_MS         4000      /* same as ARENA_GHOST_R_DURATION_MS */
#define ARENA_GUNNR_W_DPS                     6     /* same as ARENA_GHOST_R_DPS -- damage/sec to enemies standing in the zone */
#define ARENA_GUNNR_W_COOLDOWN_MS        20000      /* same as ARENA_GHOST_R_COOLDOWN_MS */
#define ARENA_GUNNR_R_RANGE                  6.0f
#define ARENA_GUNNR_R_DAMAGE_BASE           10   /* at 100% target HP */
#define ARENA_GUNNR_R_DAMAGE_LOW_HP         24   /* at ~0% target HP -- an execute */
#define ARENA_GUNNR_R_STUN_MS              1100   /* same as ARENA_ZAGAN_W_STUN_MS -- founder: "give gunnrs e a stun" */
#define ARENA_GUNNR_R_COOLDOWN_MS        20000

/* ArenaResonance (REDGARDEN_GUI_NORTHSTAR.md Milestone 2, 2026-07-31): a straight C port of
 * `GoblinFoxDragon/server/skillchain.Resonance` -- same 14 real FFXI-archetype elements, same
 * 3-tier structure (tier 1 same-element closure, tier 2 cross-element, tier 3 compound), same
 * real damage multipliers (see ARENA_SKILLCHAIN_TIER*_MULT below). Two separate Go and C
 * codebases can't share one Go type, so this enum plus `resonance_combo` (arena_game.c) are a
 * deliberate re-implementation of the same real table `server/skillchain.combinationTable`
 * already holds -- ported, not reinvented, per the northstar's own Milestone 2 language. */
typedef enum {
    ARENA_RESONANCE_NONE = 0,
    ARENA_RESONANCE_LIQUEFACTION,
    ARENA_RESONANCE_IMPACTION,
    ARENA_RESONANCE_DETONATION,
    ARENA_RESONANCE_SCISSION,
    ARENA_RESONANCE_REVERBERATION,
    ARENA_RESONANCE_INDURATION,
    ARENA_RESONANCE_COMPRESSION,
    ARENA_RESONANCE_TRANSFIXION,
    ARENA_RESONANCE_FUSION,
    ARENA_RESONANCE_FRAGMENTATION,
    ARENA_RESONANCE_GRAVITATION,
    ARENA_RESONANCE_DISTORTION,
    ARENA_RESONANCE_LIGHT,
    ARENA_RESONANCE_DARKNESS,
} ArenaResonance;

#define ARENA_SC_MAX_ATTRS       2       /* no weapon skill in this system carries more than 2 real resonance attrs (see Frostbite) */
#define ARENA_SKILLCHAIN_WINDOW_MS 8000  /* same as server/skillchain.DefaultChainWindow (8s) */
#define ARENA_SKILLCHAIN_TIER1_MULT 0.20f /* same as server/skillchain's real Tier1 multiplier */
#define ARENA_SKILLCHAIN_TIER2_MULT 0.35f /* same as server/skillchain's real Tier2 multiplier */
#define ARENA_SKILLCHAIN_TIER3_MULT 0.50f /* same as server/skillchain's real Tier3 multiplier */

/* Warrior (REDGARDEN_GUI_NORTHSTAR.md Milestone 1, 2026-07-31): the first job ported from
 * DragonsNShit's real `apps2/mud`/`server/skillchain`/`server/job` systems into Battlegrounds
 * ability content, not invented. Three real Great Sword weapon skills from
 * `server/skillchain.CanonicalWeaponSkills` (WAR's real FFXI-archetype weapon per
 * `server/job.jobStats[WAR]`'s STR-8/VIT-8 profile -- the roster's most physically front-loaded
 * job stat block) sit on Q/W/R in real FFXI progression order (starter -> mid -> finisher WS).
 * Resonance attributes are carried in these doc comments and now (Milestone 2, same day) real,
 * live skillchain detection via apply_weapon_skill_damage/arena_skillchain_try below.
 * `apps2/mud`'s weapon skills all share one real, uniform cost: `server/combat.TPWSThreshold`
 * (100 TP) via `TPState.UseWeaponSkill()`. REDGARDEN has no TP resource, so MP (this file's own
 * existing affordance, `ARENA_MP_COST_*`) substitutes rather than a new TP bar being invented --
 * an honest amendment, not a literal port, matching founder direction ("we want our old systems
 * like skillchains etc [to] work with redgarden affordances"). Melee range/cooldown magnitudes
 * matched to the existing roster (Gunnr's Q/R above) rather than a new damage-scaling formula. */
#define ARENA_WARRIOR_Q_RANGE                2.2f  /* melee range, same as Gunnr's Q */
#define ARENA_WARRIOR_Q_DAMAGE               12    /* Hard Slash -- Scission; real FFXI starter GSword WS */
#define ARENA_WARRIOR_Q_COOLDOWN_MS        3000
#define ARENA_WARRIOR_W_RANGE                2.2f
#define ARENA_WARRIOR_W_DAMAGE               18    /* Power Slash -- Transfixion; real FFXI mid-tier GSword WS */
#define ARENA_WARRIOR_W_COOLDOWN_MS        8000
#define ARENA_WARRIOR_R_RANGE                2.2f
#define ARENA_WARRIOR_R_DAMAGE               30    /* Frostbite -- Induration+Reverberation (dual resonance); real FFXI GSword finisher WS */
#define ARENA_WARRIOR_R_COOLDOWN_MS       20000

/* The Cart (TYLER multiverse_heroes.md #10, NORTHSTAR §24 Milestone 2, 2026-07-31): Q is a
 * minimal, thematically-consistent self-heal ("the cart provides for its own maintenance") --
 * the character isn't a combatant per its own lore, so Q stays deliberately small, not padded
 * out with an invented attack. W/R are the real signature mechanic: a delivery zone at the
 * Cart's own position (same r_zone_x/z/r_active_ms fields every other zone ability already
 * shares) that grants ONE random outcome -- good or bad, ally or foe, whoever steps in first --
 * to whoever triggers it, then vanishes. Reuses this file's own existing zone-tick idiom
 * (Gunnr's Consecration, Vassago's R) rather than a new struct; R is the same mechanic with a
 * bigger radius/better-weighted outcomes and a longer cooldown, matching every other hero's own
 * "R is a bigger version of the kit's theme" convention. */
#define ARENA_CART_Q_HEAL                    8     /* small self-heal, not a combat ability */
#define ARENA_CART_Q_COOLDOWN_MS          10000
#define ARENA_CART_W_RADIUS                  3.0f
#define ARENA_CART_W_DURATION_MS         15000      /* how long the delivery sits, waiting for someone to trigger it */
#define ARENA_CART_W_COOLDOWN_MS         25000
#define ARENA_CART_R_RADIUS                  5.0f
#define ARENA_CART_R_DURATION_MS         15000
#define ARENA_CART_R_COOLDOWN_MS         45000
/* Delivery outcomes (S202-42, "more impactful/powered-up abilities" + this doc's own
 * long-stated-but-never-built "R has bigger radius/BETTER-WEIGHTED outcomes" intent just above
 * -- W and R used to call the exact same rand()%4 with no distinction at all). Magnitudes
 * bumped up from the original flat pass (heal/mana 25%->35%, Flow 50->90), and a real 5th
 * outcome added: a King's Growth buff grant, the founder's own literal example for the
 * "general random-buff system... a random hero occasionally gets a King buff" ask -- Cart's
 * existing delivery mechanic ("whoever steps into the zone first" is already the "occasionally,
 * to a random hero" trigger this ask wanted, not a new separate global timer system). Picked via
 * arena_marble_bag_pick (real weighted marble-bag + Fibonacci pity, NORTHSTAR's own
 * long-documented-but-never-implemented pull algorithm, first real build anywhere in this repo)
 * instead of a flat rand()%N -- see ARENA_CART_DELIVERY_W_WEIGHTS/R_WEIGHTS below for the actual
 * per-slot weighting that finally realizes "R is better-weighted." */
#define ARENA_CART_DELIVERY_HEAL_PCT         0.35f
#define ARENA_CART_DELIVERY_MANA_PCT         0.35f
#define ARENA_CART_DELIVERY_SLOW_MS       3000
#define ARENA_CART_DELIVERY_SLOW_PCT         0.30f
#define ARENA_CART_DELIVERY_FLOW            90
#define ARENA_CART_DELIVERY_OUTCOME_COUNT       5   /* heal, mana, slow, flow, king-growth-buff */
#define ARENA_CART_DELIVERY_OUTCOME_HEAL        0
#define ARENA_CART_DELIVERY_OUTCOME_MANA        1
#define ARENA_CART_DELIVERY_OUTCOME_SLOW        2
#define ARENA_CART_DELIVERY_OUTCOME_FLOW        3
#define ARENA_CART_DELIVERY_OUTCOME_KING_BUFF   4
/* Per-slot outcome weights (ARENA_CART_DELIVERY_W_WEIGHTS/R_WEIGHTS) live in arena_game.c, next
 * to cart_trigger_delivery -- the one real consumer, not header-wide state. */

/* Vassago (S170-93): passive small HP regen, always on, same shape as Dagda's Undry -- ambient
 * restorative foresight, sensing and softening harm before it fully lands. Q a ranged bolt,
 * damage + silence (same shape as Ghost's Q) -- foresight cuts off the enemy's next intended
 * action before they take it. W grants the nearest ally next_cast_refund (same mechanic as
 * Frog's Borrowed Time) -- "the soft foresight," extended outward, lets a teammate's next cast
 * come free. R a fixed zone, silence-only, no damage at all -- the one hero on this roster whose
 * ultimate is pure control, matching "soft" literally: not a hit, a held breath. */
#define ARENA_VASSAGO_PASSIVE_REGEN_PER_SEC   2
#define ARENA_VASSAGO_Q_RANGE                 6.5f
#define ARENA_VASSAGO_Q_DAMAGE                 7
#define ARENA_VASSAGO_Q_SILENCE_MS          1400
#define ARENA_VASSAGO_Q_COOLDOWN_MS         4500
#define ARENA_VASSAGO_W_COOLDOWN_MS        11000
#define ARENA_VASSAGO_R_RADIUS                 4.5f
#define ARENA_VASSAGO_R_DURATION_MS         3500
#define ARENA_VASSAGO_R_SILENCE_MS          1200  /* > the 1000ms tick interval, same margin as Flamel's ROOT_MS -- a shorter value would leave real gaps where a continuously-standing foe isn't silenced between ticks */
#define ARENA_VASSAGO_R_COOLDOWN_MS        23000

/* He Xiangu (S170-93): passive small HP regen, always on, same shape as Dagda's Undry --
 * subsisting on almost nothing, one of the traditional Eight Immortals. Q a ranged bolt that
 * heals her for a fraction of the damage it deals -- "moonlight is also a kind of eating," the
 * same heal-off-a-fraction-of-damage mechanic as Bacon+Puck's R, but on a repeatable Q instead
 * of a one-off burst: the first hero on this roster with real sustain-through-combat on every
 * cast, not a single moment of it. W a free toggle boosting her own regen further, same shape as
 * Flute Debt's Recouping Interest -- self-denial as discipline, not deprivation. R a fixed zone,
 * heal-only, no enemy damage at all -- the roster's first purely-supportive ultimate, the mirror
 * of Vassago's purely-controlling one: she shares her sustenance, doesn't hurt anyone. */
#define ARENA_HE_XIANGU_PASSIVE_REGEN_PER_SEC   2
/* Moira Orb redesign (2026-08-26, founder real-time: "switch the targeted ability to another
   hero who has a trash w and give them moira orb from overwatch" -> "give them moira orb on
   their q" -> "give it to xehinhshu" -> "keep the code paths for the original fireball
   ability... move it to that hero and it doesnt have to fully work for now"): He Xiangu's own
   old Q (instant hitscan, ARENA_HE_XIANGU_Q_RANGE=6, heal-on-hit) is replaced by a real,
   auto-targeting (arena_nearest_enemy, no range cap -- same "keep the code path" reuse as
   Abraham's own W redesign) homing projectile, matching Overwatch's Biotic Orb in shape if not
   in exact mechanics. Real, deliberate simplification per the founder's own "doesn't have to
   fully work" allowance: the self-heal fires at CAST time (a flat amount, not tied to whether
   the orb actually lands) rather than adding new on-hit-effect plumbing to ArenaProjectile for
   a heal-the-caster case nothing else in this catalog needs yet -- an honest, smaller scope,
   not a silently-dropped feature. ARENA_HE_XIANGU_Q_RANGE/Q_DAMAGE/Q_HEAL_PCT above are now
   dead (left in place, not deleted, matching the founder's own "keep the code paths" framing)
   -- the new ability uses its own constants below. */
#define ARENA_HE_XIANGU_Q_ORB_SPEED              6.0f  /* faster than Abraham's slow fireball (3.0) -- Moira Orb reads as a moderate-paced projectile in its own source material, not a glacial skillshot */
#define ARENA_HE_XIANGU_Q_ORB_RADIUS             0.5f
#define ARENA_HE_XIANGU_Q_ORB_DAMAGE             9
#define ARENA_HE_XIANGU_Q_ORB_MAX_RANGE   (ARENA_HALF_EXTENT * 4.0f) /* map-spanning, same "no matter how far away" reuse as Abraham's own fireball range */
#define ARENA_HE_XIANGU_Q_ORB_SELF_HEAL          5     /* flat self-heal on cast -- see the redesign comment above for why this isn't heal-on-hit */
#define ARENA_HE_XIANGU_Q_RANGE                 6.0f
#define ARENA_HE_XIANGU_Q_DAMAGE                 7
#define ARENA_HE_XIANGU_Q_HEAL_PCT               0.6f  /* fraction of Q's damage returned as self-heal */
#define ARENA_HE_XIANGU_Q_COOLDOWN_MS         4200
#define ARENA_HE_XIANGU_W_REGEN_PER_SEC          4
/* Light/Dark stance (2026-08-26, founder: "someone that has a toggle have their toggle switch
   between light and dark"): her existing free W toggle now genuinely switches between two
   real stances instead of "buff when on, nothing when off" -- Light (w_active=1) keeps the
   original regen-while-active mechanic unchanged; Dark (w_active=0, now the OTHER real stance
   rather than just "off") grants a flat armor bonus, same "flat stat while toggled" shape
   Ada's own W plating (ARENA_ADA_W_ARMOR_BONUS) already established -- reusing that pattern,
   not inventing a new one. */
#define ARENA_HE_XIANGU_DARK_ARMOR_BONUS          8
#define ARENA_HE_XIANGU_R_RADIUS                 4.5f
#define ARENA_HE_XIANGU_R_DURATION_MS         4000
#define ARENA_HE_XIANGU_R_HEAL_PER_TICK          7
#define ARENA_HE_XIANGU_R_COOLDOWN_MS        25000

/* Beleth, the Detonation (S170-93 batch, TYLER multiverse_heroes.md #14, "Beleth, the
 * Detonation" -- 2.22 Hz, emotional detonation/escalation, "every love triangle in the record
 * traces back to her," seed phrase "hope is a terror I leash with song"). Passive flat armor,
 * same always-on shape as Cain's/Gunnr's own (S170-105/S170-93) -- she's survived every
 * escalation she's ever caused. Q a ranged bolt + burn, same shape as Pizza's Q (S170-46) --
 * damage that keeps paying out after contact, matching "no love story... resolves without her
 * frequency somewhere in its last act." W an instant silence-only decree on the nearest enemy,
 * same instant-in-range shape as Paimon's Speaks With Total Authority (S170-55) but with the
 * damage component removed -- pure escalation-denial, not a hit. R, "The Detonation" itself: a
 * genuinely novel shape on this roster -- marks the target's position at cast time (not a
 * continuously-ticking zone like Ghost's/Vassago's/He Xiangu's own R zones), counts down a fuse
 * via r_active_ms, and deals ONE large burst to whoever's still standing in it the instant the
 * fuse hits zero -- "hope is a terror I leash with song": the threat builds in total silence and
 * only resolves once, all at once, exactly like the thing she's named for. */
#define ARENA_BELETH_PASSIVE_ARMOR              3
#define ARENA_BELETH_Q_RANGE                    6.5f
#define ARENA_BELETH_Q_DAMAGE                   7
#define ARENA_BELETH_Q_BURN_MS               2800
#define ARENA_BELETH_Q_BURN_DPS                 6
#define ARENA_BELETH_Q_COOLDOWN_MS           4300
#define ARENA_BELETH_W_RANGE                    6.0f
#define ARENA_BELETH_W_SILENCE_MS            1900
#define ARENA_BELETH_W_COOLDOWN_MS           8500
#define ARENA_BELETH_R_RANGE                    7.0f
#define ARENA_BELETH_R_RADIUS                   3.0f
#define ARENA_BELETH_R_FUSE_MS                1800
#define ARENA_BELETH_R_DAMAGE                  32
#define ARENA_BELETH_R_COOLDOWN_MS           26000

/* MnM, the Shapeshifting Crab (S170-134, TYLER multiverse_heroes.md #114). Founder: "add MnM a
 * shapeshifting rapping crab tank from detroit" -- "tank" is the archetype ask, translated into
 * this roster's existing toolkit rather than a new mechanic: high passive armor (Cain's/Gunnr's
 * flat-armor shape), a toggle for sustained extra tankiness while active (Loki's/Ada's W-armor
 * shape), a melee root+poke Q (Paimon's Q shape), and an R that's the literal mechanical
 * translation of the lore's own framing of "shapeshifting" -- Mid-Piano's line that it's just
 * what happens to a body that's absorbed hits meant for somebody else, built here as a
 * self-root + guaranteed-survival window (Tree's R root+buff shape, with survive_floor_ms in
 * place of Tree's armor bonus): for a few seconds nothing can bring MnM below 1 HP, the shell
 * takes the hit instead of the crab underneath it. */
#define ARENA_MNM_PASSIVE_ARMOR              6
#define ARENA_MNM_Q_RANGE                     2.4f /* melee-range clamp, not a skillshot */
#define ARENA_MNM_Q_DAMAGE                    9
#define ARENA_MNM_Q_ROOT_MS                1300
#define ARENA_MNM_Q_COOLDOWN_MS            4000
#define ARENA_MNM_R_ROOT_MS                6000
#define ARENA_MNM_R_SURVIVE_FLOOR_MS        6000
#define ARENA_MNM_R_COOLDOWN_MS            27000
/* Burrow (S170-208, W rework -- founder: "switch MnM w to burrow where he digs down below
 * the map and is untargetable in that time dealing small aoe damage when he comes back up").
 * Replaces the old free-toggle armor stack ("Wasn't That Shape A Second Ago") with a real
 * cast on a real cooldown: untargetable AND rooted in place for the duration (same
 * intangible_ms + rooted_ms combo the R already reaches for, just shorter), then a small
 * eruption AoE centered on wherever he burrowed -- "resurfaces where he burrowed, not
 * somewhere else" per the founder's own phrasing, so no reposition component at all, unlike
 * Donkey's own Paper Glide which this is otherwise structurally closest to. */
#define ARENA_MNM_BURROW_DURATION_MS       1500
#define ARENA_MNM_BURROW_COOLDOWN_MS      14000
#define ARENA_MNM_BURROW_RADIUS               3.0f
#define ARENA_MNM_BURROW_DAMAGE              16

/* Weatherman (S170-206, TYLER multiverse_heroes.md #45, "Ao Guang's Weather-Debt Collector" --
 * NORTHSTAR §16.2). Fighter/Support, the roster's first kit built around wind/displacement
 * rather than direct damage. Passive (The Ledger) is flavor-only for a first pass, reusing
 * Dagda's Undry regen shape exactly (ARENA_DAGDA_PASSIVE_REGEN_PER_SEC) rather than inventing a
 * new oscillating storm-debt buff/debuff cycle -- a real, legitimate follow-on, not required to
 * ship a first kit. Q (Barometric Shove) is a ranged wind gust, displacement-only, no damage --
 * the first real push-outward Q on this roster (Duck's own Q/R pull inward). W (Collects On
 * What's Owed) is the Donkey interaction -- see weatherman_cast_w's own doc comment for the
 * ally/enemy-airborne branching logic (NORTHSTAR §16.3). R (The Debt Compounds) is a fixed AoE
 * zone, same r_zone_x/r_zone_z/r_zone_tick_ms shape as Ghost's Recital/Paimon's Two Hundred
 * Legions/NOOR-1's Do Not Approach -- values calibrated directly against NOOR-1's own R. */
#define ARENA_WEATHERMAN_PASSIVE_REGEN_PER_SEC 3
#define ARENA_WEATHERMAN_Q_RANGE            6.0f
#define ARENA_WEATHERMAN_Q_KNOCKBACK_DIST   5.0f
#define ARENA_WEATHERMAN_Q_COOLDOWN_MS      5500
#define ARENA_WEATHERMAN_W_RANGE            8.0f
#define ARENA_WEATHERMAN_W_COOLDOWN_MS      9000
#define ARENA_WEATHERMAN_R_RADIUS           4.5f
#define ARENA_WEATHERMAN_R_DURATION_MS      4500
#define ARENA_WEATHERMAN_R_DPS              7
#define ARENA_WEATHERMAN_R_COOLDOWN_MS     24000

/* Zagan (S170-230, TYLER multiverse_heroes.md #19, "Zagan, the Standstill's Confessor"). Built
 * directly from the lore's own primary source, TYLER/lore/activation_47_transmutation.md (the
 * full 47-minute monologue transcript deriving the Riemann Hypothesis through six alchemical
 * stages) plus two okemily.com blog posts ("Activation #114," "Ten Heroes Worth a Closer Look")
 * that both independently land on the same design thesis: Zagan's power should stay an
 * unconfirmed, hedged claim rather than a clean verified one -- funnier and more interesting for
 * being unresolved. Founder, real-time: "hero ZAGAN" -> "unique kit adds stun" -> "think of a
 * way to give ZAGAN a unique kit that changes meta."
 *
 * Passive -- Base Metal Screams: the transcript's own COAGULATION stage framing ("base metal
 * screams when it remembers that it was never anything but gold with a wrong address") made
 * literal -- the first time ANY enemy hero's HP crosses below 50% in their current life, Zagan
 * "hears" it (no proximity or damage-source requirement -- he presides, he doesn't have to land
 * the hit) and gains a flat Flow bounty. An event-triggered (threshold-crossing), not
 * periodic/proximity-gated, passive shape -- new to this roster.
 *
 * Q -- Calcination: the transcript's own first stage ("primes are the incorruptible seeds...
 * everything else is alloy") -- a burn that strips armor over its duration, the "impurity"
 * being burned away to reveal what was underneath.
 *
 * W -- The Standstill: the literal mechanical translation of "Standstill's Confessor" -- forces
 * stillness onto an enemy. This roster's first-ever kit to actually call arena_apply_stun();
 * the generic stun infrastructure (stunned_ms, gating on move/cast/attack) has existed since
 * S170-184 but no hero used it until now.
 *
 * R -- Conjunction: the actual meta-changing lever, built from the transcript's own CONJUNCTION
 * stage ("the exact conjunction... where body and spirit are wed in equal measure," mirrored at
 * Re(s)=1/2, "the only place the mirror permits mass to rest"). For the duration, Zagan's TOTAL
 * armor (arena_hero_armor, base+items) becomes exactly equal to his locked target's -- a true
 * live mirror, not an additive steal. This is deliberately NOT a strict buff: R against a
 * squishy target makes ZAGAN squishier too, a real cost that punishes always-R-the-biggest-
 * threat play and rewards diving a tank instead -- no other ability on this roster can make its
 * own caster weaker as the direct cost of using it, which is the actual "changes the meta" hook,
 * not just a new number. */
#define ARENA_ZAGAN_PASSIVE_CONFESSION_FLOW   60
#define ARENA_ZAGAN_Q_RANGE                    5.0f
#define ARENA_ZAGAN_Q_DAMAGE                   6
#define ARENA_ZAGAN_Q_ARMOR_SHRED              4
#define ARENA_ZAGAN_Q_DURATION_MS           3000
#define ARENA_ZAGAN_Q_COOLDOWN_MS           5000
#define ARENA_ZAGAN_W_RANGE                    3.0f
#define ARENA_ZAGAN_W_STUN_MS               1100
#define ARENA_ZAGAN_W_COOLDOWN_MS          13000
#define ARENA_ZAGAN_R_RANGE                    6.0f
#define ARENA_ZAGAN_R_DURATION_MS           6000
#define ARENA_ZAGAN_R_COOLDOWN_MS          32000

/* Lane creep waves (S170-139). Founder: "add subsystems needed to make
 * creeps a reality" -- clarified as classic MOBA lane-pushing waves,
 * distinct from S170-51's node-guardian creeps (per-node, stationary, aggro-only).
 * This arena's map has no lanes in the geometric sense (NORTHSTAR §8: "no
 * single chokepoint deciding the match," the whole point of the Arathi
 * Basin open-field design) -- rather than inventing a second map layout, the
 * lane is a straight path along the existing spawn axis: each team's spawn
 * line (x=-8/+8, matching arena_find_owned_node_for_respawn's home_x) to the
 * contested center node (0,0, "Blacksmith" in arena_nodes_reset_layout) to
 * the enemy's spawn line. Waves spawn on a fixed timer per team (no scaling
 * or catch-up rubber-banding -- the simplest honest MVP), march toward the
 * enemy spawn, and stop to fight the nearest hittable enemy hero OR
 * opposing-team lane creep within aggro range instead of marching past a
 * fight in progress -- the actual "push" mechanic: a wave that wins its
 * clash keeps advancing, one that loses stops mattering.
 *
 * No structure/tower/base-HP system exists in this arena yet (the same gap
 * Duck's W, "Government Clearance," is already blocked on) -- a wave that
 * survives all the way to the enemy spawn line currently just despawns
 * rather than damaging anything, flagged not faked, same as every other
 * "doesn't fit this engine yet" gap in this roster. (This comment used to
 * also say no gold/XP economy exists to reward a kill -- that's since gone
 * stale: S170-216/S170-217 added real Flow/XP kill credit, see
 * ARENA_LANE_CREEP_KILL_FLOW/XP below.) */
#define ARENA_LANE_WAYPOINT_COUNT      3
#define ARENA_LANE_CREEPS_PER_WAVE     3
#define ARENA_MAX_LANE_CREEPS          (ARENA_LANE_CREEPS_PER_WAVE * 2 * 2) /* both teams, generous headroom for the previous wave still marching when the next spawns */
#define ARENA_LANE_WAVE_INTERVAL_MS    20000
#define ARENA_LANE_WAVE_INITIAL_DELAY_MS 5000 /* real MOBA precedent (LoL's own first wave isn't at 0:00 either) -- also gives a match's opening seconds breathing room before waves are on the board, same spirit as a real "minions spawn in..." countdown */
#define ARENA_LANE_CREEP_HP            60
#define ARENA_LANE_CREEP_SPEED         2.5f /* units/sec -- slower than ARENA_HERO_SPEED (4.0) so a hero can always outrun or intercept a wave */
#define ARENA_LANE_CREEP_DAMAGE        7
#define ARENA_LANE_CREEP_ATTACK_COOLDOWN_MS 1000
#define ARENA_LANE_CREEP_AGGRO_RADIUS  3.5f /* doubles as attack range, same simplification as ARENA_CREEP_AGGRO_RADIUS */
#define ARENA_LANE_CREEP_WAYPOINT_EPSILON 0.15f

/* S170-218: melee + caster roles ("roles exist at all" per the backlog's own framing, not
 * required to be exact League parity -- multi-lane/siege-every-third-wave stay explicitly out
 * of scope per NORTHSTAR §20.4). ARENA_LANE_CREEP_MELEE stays value 0 and reuses every constant
 * above unchanged (HP/DAMAGE/AGGRO_RADIUS) specifically so every existing test that hand-builds
 * an ArenaLaneCreep without setting `role` at all keeps behaving exactly as before -- a
 * zero-initialized creep is a melee creep, same as the single-role system this replaces.
 * ARENA_LANE_WAVE_CASTER_COUNT of each ARENA_LANE_CREEPS_PER_WAVE-strong wave spawn as casters
 * (the rest melee) -- 1 of 3 per wave, the closest whole-number approximation to real MOBA's
 * roughly-even split that fits without changing total wave headcount (a broader balance/perf
 * question, not asked for here). Casters trade HP and per-hit damage for a real range
 * advantage, the actual "role" distinction -- they engage from farther away and die faster once
 * something reaches them, same real MOBA melee/caster minion tradeoff. */
typedef enum {
    ARENA_LANE_CREEP_MELEE = 0,
    ARENA_LANE_CREEP_CASTER = 1,
} ArenaLaneCreepRole;
#define ARENA_LANE_WAVE_CASTER_COUNT      1
#define ARENA_LANE_CREEP_CASTER_HP        36 /* 60% of melee's 60 -- squishier, matching real MOBA caster minions */
#define ARENA_LANE_CREEP_CASTER_DAMAGE     5 /* less per-hit than melee's 7 -- the range is the tradeoff, not raw damage */
#define ARENA_LANE_CREEP_CASTER_RANGE     6.0f /* real range advantage over melee's 3.5 -- the actual point of the role */

typedef struct {
    int active; /* pool slot in use */
    int alive;
    int team;   /* which team this creep fights for -- attacks the OTHER team's heroes/lane creeps only */
    float x, z;
    int hp, max_hp;
    int waypoint_index; /* 0..ARENA_LANE_WAYPOINT_COUNT-1: next waypoint this creep is marching toward */
    int attack_cooldown_ms;
    ArenaLaneCreepRole role; /* S170-218: melee (0, default) or caster -- see the enum's own doc comment above */
} ArenaLaneCreep;

/* ARENA_HERO_RESPAWN_MS (S170-121, "controlling a node enables its spawn
 * for your team"): team-mode-only hero respawn timer. Before this, death
 * was permanent within a match (arena_update_teams only checked team-wipe
 * for the win condition) -- there was no respawn system at all.
 *
 * S170-153 revision, founder: "add graveyards behind the spawns that never
 * despawn so there is always a place to respawn." A dead hero still prefers
 * respawning at the nearest node their team currently owns (unchanged), but
 * no longer stays dead forever if their team owns nothing -- a fixed,
 * permanent graveyard behind each team's own spawn line (see
 * arena_graveyard_position()) is always available as a fallback. This is
 * the real Arathi Basin shape: you always come back at your own base,
 * losing every flag costs you tempo and territory, not the ability to
 * exist. It's also *why* the team-wipe win condition had to go -- with an
 * always-available respawn point, a team can no longer be permanently
 * eliminated, so "wipe the enemy" stopped being a reachable win condition
 * at all. See arena_tick_resources() for what replaced it. */
#define ARENA_HERO_RESPAWN_MS 8000 /* S170-154: no longer the actual respawn gate -- see ARENA_RESPAWN_WAVE_MS below. Kept as the "how long ago did I die" bookkeeping value death still writes into respawn_ms_remaining (tests/telemetry reference it), just not what arena_tick_respawns waits on anymore. */

/* ARENA_RESPAWN_WAVE_MS (S170-154, founder: "respawns happen in 30 second
 * waves"): every dead hero on a team comes back TOGETHER, on a fixed global
 * clock, rather than each individually N seconds after their own death --
 * a real battleground-style wave respawn (matching this map's own Arathi
 * Basin lineage). Dying right before a wave costs you almost nothing;
 * dying right after one costs you almost the full 30s -- that timing
 * tension is the actual point, not a smoothed-out per-hero countdown. */
#define ARENA_RESPAWN_WAVE_MS 30000

/* Arathi Basin resource-race win condition (S170-153). Real WoW Arathi
 * Basin: resources tick up over time, faster the more of the map's nodes
 * ("bases") your team controls, and the first team to the cap wins --
 * "objectives are how the game is won," not attrition. Numbers here are
 * this arena's own tuning (not a claim of exact parity with the real game),
 * chosen to keep the same *shape*: holding more territory should feel
 * meaningfully faster than holding one node, and holding every node should
 * feel like a genuine sprint to the finish, not just linear scaling. */
#define ARENA_RESOURCE_CAP        2000
#define ARENA_RESOURCE_TICK_MS    2000 /* real Arathi Basin's own resource tick is on this same ~2s cadence */

/* ARENA_MATCH_MAX_DURATION_MS (S170-157): real gap found in the resource-
 * race redesign itself, founder: "i think there may be zombie games with
 * infinite win cons." Removing the team-wipe win condition (S170-153)
 * removed the one guarantee that used to make a live match always
 * eventually end -- if node control keeps flipping without either team
 * sustaining ownership long enough to fill the meter, nothing forces
 * resolution anymore, and apps/arena_server's own LIVE-phase loop has no
 * timeout at all (waiting_ticks_ms only ever counts during
 * WAITING/DRAFT). This is the sudden-death fallback: once a live team
 * match runs this long without either side reaching ARENA_RESOURCE_CAP,
 * whoever's ahead on resources wins outright (ties broken by nodes
 * currently owned) -- see the bottom of arena_update_teams(). 12 minutes:
 * long enough that a real, competitive back-and-forth match is never cut
 * short (this arena's own live matches have run 10-20 real minutes under
 * the old team-wipe condition), short enough that a truly stalled match
 * can't run forever in the persistent bot pool. */
#define ARENA_MATCH_MAX_DURATION_MS (12 * 60 * 1000)

/* Mana (S170-132): flat, roster-wide -- see ArenaHero.mp's own doc comment above. Regen fills
 * an empty pool in a bit under 17s; Q is the cheapest, spammable a few times before running dry,
 * R is the most expensive, deliberately not repeatable back-to-back even when off cooldown.
 * S170-148 ("mana should slowly regenerate when not in combat"): regen is now gated on
 * combat_timer_ms hitting 0 -- see that field's own doc comment on ArenaHero for the full
 * design (keyed off damage taken, real WoW-style "any hit re-arms the timer"). The rate
 * itself (ARENA_MP_REGEN_PER_SEC) is unchanged -- already reads as "slowly" (~17s for a full
 * bar) once it's actually gated to only run outside combat, the real fix the ask needed. */
#define ARENA_MP_MAX             100
#define ARENA_MP_REGEN_PER_SEC     6
#define ARENA_MP_REGEN_IN_COMBAT_PER_SEC 1 /* S170-150, founder: "have mana tic up slowly 1 per second always" -- a slow trickle that runs even mid-fight, distinct from the faster out-of-combat rate above */
#define ARENA_COMBAT_TIMEOUT_MS 4000 /* WoW-adjacent -- long enough that a brief lull mid-fight doesn't falsely read as "out of combat" */
#define ARENA_MP_COST_Q            20
#define ARENA_MP_COST_W            20 /* still the flat cost for the INSTANT-effect W heroes (Ghost, Frog, etc.) -- see ARENA_MP_DRAIN_W_PER_SEC below for the true toggle heroes */
#define ARENA_MP_COST_R            45
/* ARENA_MP_DRAIN_W_PER_SEC (S170-181, founder: "instead of initial mana cost toggle spells
 * should drain mana over time"): replaces the old flat ARENA_MP_COST_W activation charge for
 * every TRUE toggle W (arena_toggle_w's own `w_active = !w_active` cases) -- activating now
 * only requires mp > 0, and tick_hero_kit drains this rate continuously for as long as
 * w_active stays on, auto-deactivating the instant mp hits 0 (see ArenaHero.w_drain_accum's
 * own doc comment). 5/sec against ARENA_MP_MAX=100 means a full bar sustains a toggle for
 * ~20s -- roughly the same total spend as the old flat 20-cost activation if left on for the
 * first ~4s, but scales with how long the toggle is actually used instead of charging the
 * same amount whether it's held for one second or the whole fight. */
#define ARENA_MP_DRAIN_W_PER_SEC    5

/* ---- Flow/XP economy + item shop (S170-175) ----
 * Founder, real-time: "do a first pass shop interface have there be 2
 * shops in the other 2 corner of the maps that dont have fountains use
 * the ffxi items doc as a reference" / "add a cobination of ffxi and wow
 * for the equipable item slots" / "i want trinkets too" / "buying an item
 * auto equips it for now no bag you can sell it back for less but no
 * unequip into bag for now" / "we call gold flow". See NORTHSTAR.md §19
 * for the full design this implements (a genuinely separate currency from
 * resources[team], which stays win-condition-only, untouched by any of
 * this). */

/* ArenaItemSlot: 11 slots, combining FFXI's real equip-slot vocabulary
 * (Weapon/Head/Body/Hands/Legs/Feet/Ring/Neck/Back/Waist,
 * docs/FFXI_ITEM_PARITY_SEED.md §4) with WoW's Trinket -- the one slot
 * FFXI's own set doesn't really have an equivalent for, added because the
 * founder specifically asked for it ("i want trinkets too thats cool").
 * One item per slot per hero; buying into an occupied slot auto-sells
 * whatever was there first (see arena_shop_buy). */
typedef enum {
    ARENA_ITEM_SLOT_WEAPON = 0,
    ARENA_ITEM_SLOT_HEAD,
    ARENA_ITEM_SLOT_BODY,
    ARENA_ITEM_SLOT_HANDS,
    ARENA_ITEM_SLOT_LEGS,
    ARENA_ITEM_SLOT_FEET,
    ARENA_ITEM_SLOT_RING,
    ARENA_ITEM_SLOT_NECK,
    ARENA_ITEM_SLOT_BACK,
    ARENA_ITEM_SLOT_WAIST,
    ARENA_ITEM_SLOT_TRINKET,
    ARENA_ITEM_SLOT_COUNT
} ArenaItemSlot;

/* ArenaItemTier: purely descriptive (for the character pane / shop UI to
 * label with, per the founder's own three-way split), doesn't affect
 * mechanics at all -- a "weird" item isn't mechanically special-cased
 * beyond having an unusual stat *shape* (see ARENA_ITEMS' own doc comment
 * below), it's just labeled differently in the shop. */
typedef enum {
    ARENA_ITEM_TIER_GENERIC = 0, /* real FFXI names, used verbatim, plain single-stat bonuses */
    ARENA_ITEM_TIER_WEIRD,       /* real FFXI end-game weapons with real unusual reputations (Kraken Club, Ridill), stat SHAPE reflects that (glass-cannon / oddly-balanced) rather than a new RNG mechanic -- see ARENA_ITEMS' doc comment */
    ARENA_ITEM_TIER_SPECIFIC     /* docs/HEROES_VS0.md's existing 12-item LoL-Season-3-styled roster ("season 3 lol is the gold standard for the best meta ever") */
} ArenaItemTier;

typedef struct {
    const char *name;
    ArenaItemSlot slot;
    ArenaItemTier tier;
    int cost;        /* Flow */
    int bonus_ad;
    int bonus_max_hp;
    int bonus_max_mp;
    int bonus_armor;
    float bonus_move_speed; /* units/sec, additive on top of ARENA_HERO_SPEED */
    /* bonus_cdr_pct (S170-207, Haste Trinket, founder: "add a haste trinket" -> "passive haste
     * lowers cd and auto attack cd make it a modest improvement 6%"): a %-reduction to both
     * ability cooldowns (Q/W/R, via cast_cooldown/apply_cdr) and the auto-attack cooldown --
     * the first cooldown-reduction stat this catalog has ever needed, every other stat above is
     * a flat additive bonus, none of them compress time. Added at the end of the struct
     * (positional initializers with fewer values than members zero-fill the rest in standard C)
     * so none of the existing 26 items' own initializer rows needed touching -- only Haste
     * Trinket's own entry sets it. */
    int bonus_cdr_pct;
    /* bonus_true_dmg / bonus_lifesteal_pct (2026-08-11, founder real-time: "do a first pass
     * generating weird items that expand the play space" / "using ffxi items and your own best
     * judgement on how the new items with unique qualities... push the meta forward" -- an
     * expansion-of-the-shop-catalog first pass, page 4, see arena_game.c's own ARENA_ITEMS doc
     * comment for the full item-by-item design). Two genuinely NEW mechanic categories, not more
     * of the same flat-stat blends every existing item already has: bonus_true_dmg (Gae Bolg) is
     * flat damage applied AFTER apply_armor, not before -- the first armor-piercing stat this
     * catalog has ever had, opening a real counter-build against armor-stacking. bonus_
     * lifesteal_pct (Masamune) heals the attacker for that percent of the FINAL (post-armor)
     * damage dealt on a landed auto-attack -- the first sustain/lifesteal mechanic in this
     * engine at all. Both zero-fill for every existing item via the same "positional
     * initializers with fewer values than members zero-fill the rest" convention bonus_cdr_pct's
     * own doc comment already established -- no existing item's initializer row needs touching. */
    int bonus_true_dmg;
    int bonus_lifesteal_pct;
    /* bonus_attack_range_pct (S202-34, Kite String trinket, founder: "add an item that
     * increases auto attack range by 4% 3333 flow 'Kite String' trinket"): a %-increase to
     * basic-auto-attack range specifically (arena_hero_attack_range()), not ability ranges --
     * the founder's own ask names "auto attack range" specifically, same narrow scope
     * bonus_cdr_pct's own Haste Trinket precedent holds itself to (ability cooldowns AND
     * auto-attack cooldown, but nothing wider than what was actually asked for). Zero-fills for
     * every existing item via the same positional-initializer convention every other trailing
     * stat on this struct already relies on. */
    int bonus_attack_range_pct;
    /* bonus_mp_regen_combat (S205-87, "Luck of the Draw" trinket, founder, cruise-queue: "we
     * should have a weapon that is on like page 5 for 2.2k flow a trinket called 'luck of the
     * draw' that gives some mana regen during combat") -- a flat bonus added directly to
     * ARENA_MP_REGEN_IN_COMBAT_PER_SEC (arena_game.c's own real mana-regen tick), not a
     * percentage: that base in-combat rate is itself a flat int (1), so a flat bonus is the
     * consistent unit, not the %-shape bonus_cdr_pct/bonus_attack_range_pct use for their own
     * different base mechanics. Zero-fills for every existing item via the same trailing-field
     * positional-initializer convention every prior append (bonus_cdr_pct, bonus_true_dmg/
     * bonus_lifesteal_pct, bonus_attack_range_pct) already established. */
    int bonus_mp_regen_combat;
} ArenaItemDef;

extern const ArenaItemDef ARENA_ITEMS[];
#define ARENA_ITEM_COUNT 35 /* 2026-09-03: was 34 -- +1 for Luck of the Draw (S205-87, founder,
    cruise-queue: "we should have a weapon that is on like page 5 for 2.2k flow a trinket called
    'luck of the draw' that gives some mana regen during combat"), appended at the end of the
    catalog, same "indices stay stable" convention every prior append already used. Real, honest
    note: at this catalog size SHOP_PAGE_COUNT (apps/arena/src/main.c, ceil(ARENA_ITEM_COUNT/
    SHOP_ITEMS_PER_PAGE), 9 items/page) computes to page 4, not page 5 -- the founder's own "page
    5" was descriptive ("somewhere further down the shop"), not a literal page-count requirement
    to hit exactly; not padded with filler items just to force a 5th page that doesn't reflect a
    real 37th+ item existing yet. 2026-08-26: was 33 -- +1 for Kite String (S202-34, founder:
    "add an item that increases auto attack range by 4% 3333 flow 'Kite String' trinket"),
    appended at the end of the catalog, same "indices stay stable" convention every prior append
    already used. 2026-08-11: was 27 -- +6 for the "expand the play space" first pass (Gae Bolg,
    Masamune, Muramasa, Balance Ring, Empress Hairpin, Ninja Tekko), pushing the shop UI's own
    SHOP_PAGE_COUNT from 3 to a real 4th page -- founder: "add page 4 to the shop." No
    client-side paging code needed for this: SHOP_PAGE_COUNT is entirely derived from this one
    constant already. */
/* ARENA_BALANCE_RING_ITEM_ID (2026-08-11, "expand the play space" pass): a named index, same
 * reasoning ARENA_BLINK_DAGGER_ITEM_ID's own doc comment gives -- Balance Ring's comeback armor
 * bonus scales LIVE with the wearer's own missing-HP fraction (computed inside
 * arena_hero_armor() itself, same "computed live, not copied" idiom the King Wealth aura bonus
 * already uses in that same function), which can't be pre-summed once at purchase time the way
 * every flat-stat item's own bonus can -- code needs to check "is THIS SPECIFIC item equipped"
 * by index, not just read a cached aggregate field. */
#define ARENA_BALANCE_RING_ITEM_ID 30
#define ARENA_BALANCE_RING_MAX_ARMOR_BONUS 40 /* the bonus at 0 HP (approached, never quite reached while alive) -- roughly double Iron Ram Trousers' own flat 18, since this only reaches near its max value while critically low, not all the time like a flat item */
/* ARENA_BLINK_DAGGER_ITEM_ID (S170-205, founder: "add blink dagger 1400 flow it gives a new
 * keybind on screen for tilda"): a named index into ARENA_ITEMS, not just a stat entry -- the
 * only item in the catalog whose value comes from an ACTIVE ability (arena_use_blink) rather
 * than passive stats alone, so unlike every other item, code needs to check "is THIS SPECIFIC
 * item equipped" by index, not just sum stat fields generically the way
 * arena_recompute_item_stats already does for everyone. A fixed literal, not "last item in the
 * catalog" (ARENA_ITEM_COUNT - 1) -- true when this was written, but S170-206 (Donkey) added a
 * second active-ability item after it, so "last" stopped meaning Blink Dagger. Both items were
 * appended in catalog-array order, so indices stay stable regardless of how many more items
 * get added later (equipped_item[] wire values, shop quick-buy 1-9 keys, and any other
 * index-based reference all stay correct either way). */
#define ARENA_BLINK_DAGGER_ITEM_ID 24
/* ARENA_DONKEY_ITEM_ID (S170-206, founder: "donkey should be an item" -> "3.2k flow" -> "tilda
 * should make the hero do the paper airplane glide thing"): see ARENA_BLINK_DAGGER_ITEM_ID's own
 * doc comment just above -- same reasoning, second (and currently last) fixed-index active item. */
#define ARENA_DONKEY_ITEM_ID 25

/* ---- Item curriculum (NORTHSTAR.md §26.3.2, 2026-08-25) ----
 * Founder real-time: "continue the exotic auto curriculum redgarden work" -> "training" ->
 * "parena mod driven first" -- §25.4's existing autocurriculum picks WHICH OPPONENT to train
 * against from a pool of past checkpoints; §26.3.2 is a real, explicit scope expansion asked
 * for directly by the founder: also curriculum-generate NEW ITEMS meant to "meta break the top
 * teams" (counter whichever team composition the current policy is losing to), a POET/PAIRED-
 * style environment-parameter curriculum layered on top of the opponent one, not a replacement
 * for it.
 *
 * This section is the GENERATION PRIMITIVE only, PARENA-mod-driven per the founder's own
 * sequencing ("parena mod driven first"): redgarden_host_item_curriculum_generate_counter_item
 * blends two existing ARENA_ITEMS catalog entries' own stat fields (average + a small
 * deterministic jitter, reproducible from the same two base items rather than truly random)
 * into one of a small number of runtime-mutable "curriculum slots". Deciding WHICH two items
 * to blend from (reading which items the currently-dominant team composition is using -- not
 * even observable to the Python training loop yet, `sim_get_obs_team_any`'s observation vector
 * carries no item-purchase state today) and evaluating whether a generated item actually
 * counters that composition are real, unresolved training-loop questions, same honesty
 * convention NORTHSTAR.md §26.3.2 itself already uses -- NOT built in this pass.
 *
 * Curriculum items live in a SEPARATE, runtime-mutable array (ARENA_ITEM_CURRICULUM_SLOTS
 * below), not appended into the fixed, `const`, compile-time-sized ARENA_ITEMS[] catalog --
 * every existing call site that indexes ARENA_ITEMS by a raw int id (shop UI, inventory
 * application, network snapshot item ids, ARENA_BLINK_DAGGER_ITEM_ID-style fixed indices) would
 * need auditing to become curriculum-slot-aware before a generated item could safely enter live
 * gameplay -- deliberately not attempted here. This is training-side machinery a future
 * consumer reads via redgarden_host_item_curriculum_get, same "plumbing first, consumption
 * later" shape §25.4's own C-level prerequisite (sim_step_team_vs_actions) was built in. */
#define ARENA_ITEM_CURRICULUM_SLOT_COUNT 4
extern ArenaItemDef ARENA_ITEM_CURRICULUM_SLOTS[ARENA_ITEM_CURRICULUM_SLOT_COUNT];

int redgarden_host_item_curriculum_generate_counter_item(int base_item_a, int base_item_b, int slot_index);
const ArenaItemDef *redgarden_host_item_curriculum_get(int slot_index);

#define ARENA_ITEM_SELL_REFUND_PCT 50 /* founder: "sell it back for less" */
#define ARENA_SHOP_RADIUS 3.0f /* same "stand near it" convention as ARENA_FOUNTAIN_RADIUS */

/* Blink Dagger (S170-205, founder: "add blink dagger 1400 flow it gives a new keybind on screen
 * for tilda"). Real DOTA 2 item, "the premier mobility item" -- an instant, short, no-cast-time
 * teleport on its own cooldown, not a stat stick with an ability bolted on (the +6 AD/+6 HP the
 * founder also asked for are real but secondary). Bound to a dedicated key (tilde/backtick),
 * distinct from Q/W/E, since it's an item activation, not a kit ability -- doesn't touch mana
 * (items in this catalog never have, only Flow to buy them) or the Q/W/R cooldown fields, a
 * fully separate cooldown track. Direction is derived the same way Unicorn's own Q dash already
 * derives one (toward the current move target if moving, else toward the nearest foe, else a
 * no-op) -- reused rather than inventing a second "which way do I dash" convention, and matches
 * real DOTA's own "blink toward wherever you're pointed" feel close enough for this engine's
 * click-to-move input model, which has no separate cursor-position wire field to blink toward
 * more literally. ARENA_BLINK_RANGE is deliberately the single longest gap-closer/escape
 * distance on the whole roster (every kit dash tops out well under this) -- matching Blink
 * Dagger's real identity as strictly the best mobility tool in the game, not just "one more
 * dash." ARENA_BLINK_COOLDOWN_MS matches real DOTA's own Blink Dagger cooldown exactly (15s),
 * not rescaled -- unlike map-distance constants, a cooldown is already engine-agnostic. Not
 * blocked by silenced_ms (this engine's own convention: silence blocks CASTING specifically;
 * using an item isn't a cast) -- only by stunned_ms (which blocks all action, the stronger of
 * the two generic movement/action blockers everywhere else in this file). */
#define ARENA_BLINK_RANGE 12.0f
#define ARENA_BLINK_COOLDOWN_MS 15000

/* Donkey (S170-206, NORTHSTAR §16, real TYLER lore/docs/HEROES_VS0.md kit -- founder direction
 * across this whole arc: "add the weatherman and donkey" -> [asked to clarify the non-piloted-
 * unit blocker] -> "donkey should be an item" -> "3.2k flow" -> "tilda should make the hero do
 * the paper airplane glide thing" -> "longish range high speed escape can move above obstacles"
 * -> "long ish cooldown" -> "2 minute cooldown on paper plane fly mode" -> "but the thing where
 * it unfolds and fights for you thats a passive"). This single founder
 * clarification ("should be an item") sidesteps NORTHSTAR §16.1's entire stated blocker -- a
 * whole new non-piloted companion-entity system, with its own collision/targeting/rendering
 * problems -- by making Donkey an equippable item whose effects trigger on whichever hero wears
 * it, not a second targetable unit at all. No new entity, no new render path, no new
 * collision/targeting rules: everything reuses generic per-hero status-effect fields, the exact
 * "reused rather than invented from nothing" discipline this file's own doc comments repeat
 * throughout.
 *
 * Two independent effects, two independent cooldowns, same item:
 *   - Immortal's Fold (automatic passive -- confirmed explicitly, not player-activated): the
 *     instant the wearer's HP crosses below ARENA_DONKEY_FOLD_HP_FRACTION, grants a temporary
 *     damage floor (reusing the existing generic survive_floor_ms field -- a deliberate
 *     simplification of the lore's own "flat damage shield," the same "no literal shield-absorb
 *     mechanic exists yet, simplify to the floor mechanic that does" call this file already made
 *     for Doc Wheel's own R) AND makes the unfolded Donkey itself fight back -- periodic damage
 *     to the nearest enemy in range for the fold's own duration, "it unfolds and fights for you,"
 *     not just a passive shield. Tracked on its own dedicated donkey_fold_ms field (distinct from
 *     the shared survive_floor_ms it also sets) specifically so the fight-back damage only ever
 *     fires from Donkey's own fold window, never from an unrelated hero's own survive_floor_ms-
 *     granting ability (Pizza/Dagda/Cain/MnM's own R's all set that same shared field for
 *     entirely different reasons). Gated by its own proc cooldown so it can't retrigger every
 *     tick while still under the threshold.
 *   - Paper Glide (tilde-activated, same key as Blink Dagger -- see arena_use_active_item):
 *     unlike Blink Dagger's instant teleport, a brief high-speed traversal (ARENA_DONKEY_GLIDE_
 *     SPEED_MULT on top of base move speed) toward a real destination point, covering
 *     ARENA_DONKEY_GLIDE_RANGE -- longer reach than Blink Dagger's own 12.0, matching "longish
 *     range" and Paper Glide's real lore identity as the bigger, slower-to-reset escape tool.
 *     Obstacle collision is skipped for the glide's own duration ("flies over trees etc," the
 *     founder's original 2026-07-24 direction on this ability, predating this whole item pivot)
 *     and the wearer is untargetable the same way (donkey_airborne_ms doubles as intangible_ms
 *     for the same window, reusing hero_is_hittable's existing gate rather than touching that
 *     function). Direction is away from the nearest living enemy -- a real escape, matching the
 *     lore's own "carry the owner clear of immediate danger" -- falling back to the current move
 *     target if no enemy is nearby to escape from, or a no-op if neither gives a direction.
 *     ARENA_DONKEY_GLIDE_COOLDOWN_MS is a real commitment (2 minutes, the founder's own explicit
 *     number) -- meaningfully longer than Blink Dagger's 15s, matching "long ish cooldown" and
 *     the fact that Paper Glide covers more ground and grants real untargetability, not just
 *     Blink's instant reposition. */
#define ARENA_DONKEY_FOLD_HP_FRACTION 0.25f
#define ARENA_DONKEY_FOLD_MS 4000            /* how long the floor + fight-back window holds once triggered */
#define ARENA_DONKEY_FOLD_PROC_COOLDOWN_MS 30000
#define ARENA_DONKEY_FOLD_FIGHT_RADIUS 4.0f  /* "fights for you" -- same modest melee-adjacent scale as this file's other passive-aura radii */
#define ARENA_DONKEY_FOLD_FIGHT_DPS 6
/* 2026-07-30, founder: "donkey glide needs to be 6 times as far" -- RANGE alone going 16->96
 * wouldn't actually change anything: the glide's real reach is bounded by how far
 * ARENA_DONKEY_GLIDE_SPEED_MULT*ARENA_HERO_SPEED can travel within
 * ARENA_DONKEY_GLIDE_DURATION_MS (the original 16.0 was sized to match speed*duration almost
 * exactly, per this comment's own original "covers the full range comfortably inside the
 * duration window" note) -- setting a farther unreachable target just means the hero coasts
 * toward it at normal speed, fully targetable again, once the airborne window ends early. Scaled
 * DURATION 6x alongside RANGE (not SPEED_MULT) so the extra distance reads as a genuinely longer
 * multi-second glide across the map -- matching Paper Glide's own established "bigger, slower
 * escape tool" identity (this comment's own doc text a few lines up) -- rather than an
 * ~5x-faster near-instant zip that SPEED_MULT scaling would have produced instead. Speed itself
 * (28 units/sec) is untouched. */
#define ARENA_DONKEY_GLIDE_RANGE (16.0f * 6.0f)
#define ARENA_DONKEY_GLIDE_DURATION_MS (600 * 6)   /* airborne/untargetable window -- real transit time, not an instant blink */
#define ARENA_DONKEY_GLIDE_SPEED_MULT 7.0f   /* on top of ARENA_HERO_SPEED -- covers the full range comfortably inside the duration window above */
#define ARENA_DONKEY_GLIDE_COOLDOWN_MS 120000

/* Flow/XP kill rewards (S170-175). Melee/homing-shot kills only -- ability
 * casts don't set last_attacked_by_owner, so a kill finished by a spell
 * grants nothing this pass, same "not every damage source needs full
 * reward wiring, flagged not faked" precedent arena_zone_damage_creeps
 * already set for AoE-vs-creep kills (that function's own doc comment).
 * Hero kills pay the most by a wide margin -- real MOBA precedent (a
 * creep kill is routine map presence, a hero kill is a real fight won). */
/* S170-197, founder: "the economy is too slow i can never buy anything increase flow gained by
 * 10x from all sources." All 4 Flow-earning constants below x10 (XP left untouched -- not
 * mentioned, and XP has no spend pressure the way Flow does, so slow XP was never the complaint).
 * Values were the original S170-175 amounts times 10, not independently re-tuned. */
#define ARENA_NODE_GUARDIAN_KILL_FLOW  150
#define ARENA_NODE_GUARDIAN_KILL_XP     10
#define ARENA_LANE_CREEP_KILL_FLOW     80
#define ARENA_LANE_CREEP_KILL_XP        6
/* S170-216: XP-share radius on lane creep kills -- real MOBA parity keeps gold/Flow
 * individual/precise (only the hero whose hit landed gets ARENA_LANE_CREEP_KILL_FLOW above) but
 * shares XP generously with every allied hero nearby, not just the killer. Bigger than this
 * file's typical combat-ability radii (3.5-6.0) on purpose -- XP-share is meant to reward
 * "present for the wave," not "landed inside a tight hitbox." */
#define ARENA_LANE_CREEP_XP_SHARE_RADIUS 8.0f

/* Jungle Camps -- The Four Heavenly Kings, Milestone 1 (2026-08-10). Founder real-time
 * direction: "ok for arena in the north south east and west (the bases are at the corners
 * between the corners if you fold it you get 4 spots we can spawn minions from and spawn boss
 * monsters that give buffs)" -> "this way those camps will spawn mobs that will eventually
 * assault the towers so it becomes difficult to stale out the game by never attacking towers"
 * -> "the four heavenly kings" (boss naming). Full design: GoblinFoxDragon/docs2/
 * JUNGLE_CAMPS_NORTHSTAR.md. Originally scoped to build in GoblinFoxDragon's own fork
 * (`apps2/battlegrounds_gui`) first and backport later -- founder redirected real-time to build
 * in REDGARDEN's own apps/arena_server FIRST instead (table stakes for the NORTHSTAR §25/§26
 * autocurriculum/UED work: bots need real jungle-camp objectives to learn to contest before
 * curriculum-generation research is meaningful), validate here, THEN port to GFD.
 *
 * 4 camps at the edge midpoints (N/S/E/W), between the 2 fountain corners and the 2 shop
 * corners -- same "-margin so it's never buried in terrain" idiom arena_fountain_position
 * already uses. Each camp waves neutral-hostile minions from the opening bell (no owning team,
 * aggros either side -- same ARENA_CREEP_NEUTRAL flavor node guardians already use). Stationary
 * (guard the camp, don't march) in this first pass -- §3.4's anti-stall escalation (uncleared
 * camps eventually march toward an objective) is real, separate, NOT-STARTED follow-up work per
 * that doc's own milestone table, not built here. The Kings themselves (§3.3 -- one boss per
 * camp, silent until 1:00, each granting a distinct buff mechanic) are Milestone 2, also NOT
 * built in this pass -- this is minion waves only. */
#define ARENA_CAMP_COUNT               4
#define ARENA_CAMP_MINIONS_PER_WAVE    2
#define ARENA_MAX_CAMP_MINIONS         (ARENA_CAMP_MINIONS_PER_WAVE * ARENA_CAMP_COUNT * 2) /* 2x headroom, same reasoning ARENA_MAX_LANE_CREEPS already uses for its own multiplier */
#define ARENA_CAMP_WAVE_INTERVAL_MS    25000 /* slightly slower than lane creeps' 20s -- a camp is a detour, not the main lane, real MOBA jungle camps respawn slower than lane waves too */
#define ARENA_CAMP_MINION_HP           45    /* between lane creep melee (60) and caster (36) -- a real but not dominant jungle fight */
#define ARENA_CAMP_MINION_DAMAGE       6     /* matches ARENA_CREEP_NEUTRAL_DAMAGE -- same "neutral creep" damage tier as node guardians */
#define ARENA_CAMP_MINION_AGGRO_RADIUS 4.0f  /* matches ARENA_CREEP_AGGRO_RADIUS -- same "passive until approached" neutral-camp convention */
#define ARENA_CAMP_MINION_ATTACK_COOLDOWN_MS 1500 /* matches ARENA_CREEP_ATTACK_COOLDOWN_MS */
#define ARENA_CAMP_MINION_KILL_FLOW     60
#define ARENA_CAMP_MINION_KILL_XP        5

/* §3.4 Anti-stall escalation (2026-08-10): "so it becomes difficult to stale out the game by
 * never attacking towers." A camp that's had at least one active minion continuously for
 * ARENA_CAMP_ESCALATION_THRESHOLD_MS stops being purely passive -- its minions march toward the
 * nearest ArenaNode instead of standing still, reusing the same step-toward-target idiom lane
 * creeps already use for their own waypoint march. Deliberately targets nodes, not a new
 * base-siege/tower-attack system: §1's own open question ("does 'assault the towers' mean the
 * existing node-guard towers, or motivate a new objective-tower system") is resolved here by
 * NOT building new tower-attack code -- lane creeps themselves don't attack towers either (they
 * despawn at the enemy spawn line with "no structure to hit," see arena_tick_lane_creeps' own
 * comment), so escalated camp minions matching that same limitation is consistency, not a cut
 * corner. An escalated camp's pressure is real board presence at a contested node (forces a
 * response), not simulated siege damage that doesn't exist anywhere else in this file yet.
 * Re-arms (camp_uncleared_ms resets to 0, camp_escalated clears) the instant a camp has zero
 * active minions -- "uncleared" specifically means "never fully cleared," not "reached some
 * total elapsed time since the match started." */
#define ARENA_CAMP_ESCALATION_THRESHOLD_MS 90000 /* 1:30 -- longer than a King's own 1:00 spawn gate (early jungle contests shouldn't already be under pressure), real MOBA precedent for "camps left rotting create real map pressure inside ~2 minutes" */
#define ARENA_CAMP_MINION_MARCH_SPEED 2.0f /* slower than a lane creep's own 2.5 -- a camp minion escalating is a secondary, slower threat, not a second full wave */
#define ARENA_CAMP_MINION_WAYPOINT_EPSILON 0.15f /* matches ARENA_LANE_CREEP_WAYPOINT_EPSILON's own arrival tolerance */

typedef struct {
    int active;
    int alive;
    float x, z;
    int hp, max_hp;
    int attack_cooldown_ms;
    int camp_index; /* which of the ARENA_CAMP_COUNT camps spawned this minion -- needed for §3.4's per-camp escalation state and to pick a stable march target */
} ArenaCampMinion;

/* Jungle Camps Milestone 2 -- The Four Heavenly Kings (2026-08-10). docs2/
 * JUNGLE_CAMPS_NORTHSTAR.md §3.3. One boss per camp, silent until ARENA_KING_SPAWN_DELAY_MS,
 * killable by either team. camp_index doubles as King identity -- arena_camp_position's own
 * N/S/E/W convention (0/1/2/3) maps to North/Vaisravana-Wealth, South/Virudhaka-Growth,
 * East/Dhrtarastra-Music, West/Virupaksha-All-Seeing, checked against the real Shitennō
 * (Buddhism's Four Heavenly Kings), not an arbitrary assignment -- see the northstar's own §3.3
 * for the full mythology grounding. */
#define ARENA_KING_SPAWN_DELAY_MS      60000 /* 1:00 into the match, real MOBA jungle-boss precedent -- longer than lane creeps' own initial delay, boss-scale gate */
/* Milestone 4, King respawn (2026-08-10): §5's own open question -- "does a defeated King
 * respawn later, or is each King a one-time kill per match? Not specified yet -- real MOBA
 * precedent (jungle bosses respawning) suggests yes, but the founder hasn't confirmed a timer."
 * Not founder-confirmed; resolved here as a real, documented judgment call rather than left
 * open indefinitely, same "spec the model, not the numbers, but still commit to real numbers"
 * discipline every other timer in this file already uses. Chose: yes, respawns, on a timer
 * shorter than real MOBA jungle-boss respawns (Baron ~6-7 min, Roshan ~8-11 min) -- this game's
 * own match pace is already deliberately faster than real-MOBA precedent throughout (lane waves
 * 20s not League's ~30s, camp waves 25s, King's own FIRST spawn at 1:00 not 5+ minutes), same
 * "scaled down for this game's faster match pace" reasoning ARENA_POWERUP_RESPAWN_MS's own doc
 * comment already gives for an analogous real-WSG-precedent timer. */
#define ARENA_KING_RESPAWN_MS         150000 /* 2:30 -- long enough that farming one King twice is a real commitment, short enough to matter again within a single fast match */
#define ARENA_KING_HP                    500 /* boss-scale, comparable to a tower (420) -- a real fight, not a lane-creep reskin */
#define ARENA_KING_DAMAGE                 14 /* roughly 2x a neutral camp minion's 6 -- a real threat, not instant-kill */
#define ARENA_KING_AGGRO_RADIUS          5.0f /* slightly wider than a camp minion's 4.0 -- boss-scale presence */
#define ARENA_KING_ATTACK_COOLDOWN_MS   1200 /* faster swings than a camp minion's 1500 -- boss-scale threat */
#define ARENA_KING_KILL_FLOW             300 /* real objective-tier reward, between a lane creep (80) and a hero kill (1000) */
#define ARENA_KING_KILL_XP                25

typedef struct {
    int active;
    int alive;
    float x, z;
    int hp, max_hp;
    int attack_cooldown_ms;
} ArenaKing;

/* East/Dhrtarastra, God of Music -- Catchy Song: attack speed + move speed. The one King
 * mechanic with no existing primitive to extend -- team-viral, spreads on respawn, survives
 * individual deaths (§3.3: "the buff persists on the TEAM as long as at least one living member
 * currently carries it... the moment ANY teammate respawns, they pick it up too"). Implemented
 * as ArenaHero.king_music_carrier (see that field's own doc comment) plus a relay check in
 * arena_respawn_hero -- deliberately NOT a per-hero timer like every other buff in this file,
 * because the whole point is that it outlives any single hero's death. */
#define ARENA_KING_MUSIC_ATTACK_SPEED_PCT 20 /* additional CDR pct on top of items, same apply_cdr path Haste Trinket already uses */
#define ARENA_KING_MUSIC_MOVE_SPEED_PCT   20 /* multiplicative move-speed bonus, same speed_mult path slows already use */

/* South/Virudhaka, God of Growth -- Bloodroar: individual, stacking, fragile. Each takedown
 * while holding it adds a stack (more damage) and refreshes the duration; the instant the
 * holder dies, the buff and every stack are gone -- no drop, no relay, the deliberate opposite
 * of Music. Both fields are wiped by the ordinary memset in arena_respawn_hero with no special
 * handling needed (unlike king_music_carrier). */
#define ARENA_KING_GROWTH_AD_PER_STACK      6 /* flat bonus AD per stack, same "flat, not multiplier" shape as ARENA_BERSERKER_BONUS_AD */
#define ARENA_KING_GROWTH_DURATION_MS   15000 /* refreshed on every takedown while held */

/* West/Virupaksha, The All-Seeing -- Farsight: team-wide, flat timer, utility not combat --
 * "deliberately the simplest of the four." Genuinely team-wide (everyone gets it the instant
 * it's claimed), so this is ArenaState.king_allseeing_team_ms[2], not a per-hero field.
 * Implemented: bonus Flow from camp-minion/King kills while active (an econ reward, matching
 * the real domain). NOT implemented: the vision-reveal half of the original design -- no
 * fog-of-war system exists anywhere in this simulation yet (NORTHSTAR §15 is spec-only, no
 * code), so there is nothing to reveal against. Flagged honestly rather than silently dropped;
 * revisit once §15 lands. */
#define ARENA_KING_ALLSEEING_DURATION_MS 45000 /* longer than Growth -- explicitly the low-stakes, simplest King */
#define ARENA_KING_ALLSEEING_BONUS_FLOW_PCT 50 /* +50% Flow from jungle-monster kills while active */

/* North/Vaisravana, God of Wealth (chief of the Four Kings) -- Bulwark: proximity aura, not a
 * carried buff -- "an umbrella large enough to shelter a group, not one person." Held by
 * whoever claimed it (ArenaHero.king_wealth_ms), but the flat armor bonus + gold trickle apply
 * to any teammate within ARENA_KING_WEALTH_AURA_RADIUS of a current holder, computed live in
 * arena_hero_armor()/arena_tick_kings() -- not copied onto nearby allies' own fields. */
#define ARENA_KING_WEALTH_DURATION_MS   30000
#define ARENA_KING_WEALTH_ARMOR_BONUS     12 /* flat armor, this engine's damage model is flat-subtraction (apply_armor), not percentage -- comparable in scale to real armor items (e.g. Iron Ram Trousers' 18) */
#define ARENA_KING_WEALTH_AURA_RADIUS    6.0f /* wider than a normal ability radius on purpose -- "shelter a group" */

/* Day/night cycle + Bloodflower world event (2026-08-25). Founder real-time: "bring in the day
 * night cycle from SHANKPIT main" -> "including the lighting" -> "but have the moon trigger an
 * event called the bloodflower" -> "the bloodflower triggers when the moon is at its highest
 * point in the sky" -> "but bring it into GFD totally as parena" -> "fuck it do it into
 * redgarden first" (upstream-first, same precedent as Jungle Camps/Four Heavenly Kings, see
 * that section's own doc comment above) -> "but it should all be events with parena mods ya
 * know?" (event-driven: the host fires a named event, a PARENA-compiled mod handles it -- same
 * shape PITVIPER's S192-01 wheel-scroll mod already proved, see stdlib/redgarden/
 * bloodflower_mod.prn's own header comment for the exact ABI).
 *
 * Time-of-day math (sun/moon direction, ambient tint) is ported from SHANKPIT's
 * packages/render/retro_sky.c/retro_lighting.c reference implementation (the founder's own
 * named source), adapted for this game's top-down/isometric camera: no 3D sky dome, just an
 * ambient RGB tint driven by the same time_sec -> phase math, consumed by apps/arena's
 * glClearColor background. Core cycle state lives in arena_game.c (server-authoritative, real
 * simulation state, synced to clients the same way King/camp state already is) -- only the
 * Bloodflower's *trigger*, at moon zenith, goes through the PARENA mod surface, matching how
 * PITVIPER's own underlying scrollback mechanism already existed and only the wheel-event
 * trigger went through the mod. */
#define ARENA_DAYNIGHT_ORBIT_SPEED 0.025f /* radians/sec, ported verbatim from SHANKPIT retro_sky.c's retro_sky_eval_sun_dir -- same real orbit rate, not re-tuned for this game. Natural period = 2*PI/0.025 = ~251s (~4:11), giving roughly two full day/night cycles in a typical under-15-min match */
#define ARENA_DAYNIGHT_TILT        0.40f /* radians, same ported constant as SHANKPIT's own `tilt` local in retro_sky_eval_sun_dir */
#define ARENA_DAYNIGHT_ZENITH_REARM_THRESHOLD 0.30f /* moon_height must drop back below this before daynight_zenith_fired re-arms -- clearly past the peak, not a near-zenith wobble; same smoothstep-scale magnitude retro_lighting.c's own sun_visibility/moon_visibility thresholds use (0.22-0.28) */

/* Bloodflower (2026-08-25): a real, server-authoritative world object that spawns at map
 * center (0,0 -- same "deterministic, real coordinate" convention as arena_fountain_position/
 * arena_camp_position, see those functions' own doc comments) the instant the moon crosses
 * zenith, grants a real Flow bonus to whichever team's hero claims it first (walks within
 * ARENA_BLOODFLOWER_CLAIM_RADIUS), then despawns -- deliberately simple and demonstrable rather
 * than a guessed-at deeper gameplay system, since the founder specified only the trigger
 * condition ("triggers when the moon is at its highest point"), not the effect. Reuses the same
 * "econ reward, computed live, not copied onto a hero field" shape King/All-Seeing's own Flow
 * bonus already established, rather than inventing a new buff-application idiom. */
#define ARENA_BLOODFLOWER_LIFETIME_MS      20000 /* despawns unclaimed after 20s -- real time pressure, shorter than a King's own multi-minute presence since this is a single-tick econ pickup, not a boss fight */
#define ARENA_BLOODFLOWER_CLAIM_RADIUS      2.0f /* tight -- a hero has to actually walk onto it, not proximity-aura like King/Wealth */
#define ARENA_BLOODFLOWER_CLAIM_FLOW         150 /* between a camp minion (60) and a King (300) -- a real but not dominant reward, matching this file's existing tiering */
#define ARENA_KING_WEALTH_GOLD_PER_SEC      4 /* small trickle to nearby allies -- deliberately smaller than All-Seeing's own bonus, "a smaller bonus-gold trickle" per the holder's real domain */

/* §25.3 Synergy decay -- a REAL LIVE-MATCH COMEBACK MECHANIC, not a training technique
 * (NORTHSTAR §25.3 explicitly separates this from §25.2's diversity-preserving TRAINING
 * schedule -- distinct concept, same "synergy" word, don't conflate with noisy-gestalt or with
 * this doc's OWN unrelated §3.4 anti-stall camp-minion escalation, another same-word naming
 * collision). A team's cohesion "tier" (0 = full cohesion .. ARENA_SYNERGY_TIER_COUNT-1 = fully
 * decayed) re-rolls every ARENA_SYNERGY_ROLL_INTERVAL_MS, stochastically -- founder, explicit:
 * "there needs to be a random chance of synergy decay at different levels... not always
 * happen." Higher tiers get more likely the further AHEAD (in the real resource race, S170-153)
 * that team is -- the team pulling ahead risks losing a small team-wide "playing well together"
 * bonus, giving the losing side real openings, same rubber-band spirit as Mario Kart items or
 * League's own catch-up gold (NORTHSTAR §25.3's own framing). Source design: the CarePyre
 * transcript's own StochasticSynergyController (docs2/MULTI_AGENT_RD_RESEARCH_NOTES.md),
 * base_probs [0.60, 0.25, 0.10, 0.05] for tiers 0-3 -- ported faithfully EXCEPT one real bug
 * found while porting: the source's own score-lead shift (`logits = log(base_probs) +
 * score_diff * 0.15`) adds the SAME constant to every tier's logit, which softmax normalization
 * makes a mathematical no-op (adding a constant to every logit before softmax never changes the
 * resulting probabilities) -- not silently reproduced. This implementation instead scales the
 * shift BY tier index (higher tiers pushed up more as the lead grows), which actually does what
 * the source design describes. Numbers TBD per NORTHSTAR §25.3's own framing -- a real,
 * documented first pass, not tuned against actual match data. */
#define ARENA_SYNERGY_TIER_COUNT            4
#define ARENA_SYNERGY_ROLL_INTERVAL_MS    8000 /* "shouldn't flicker frame-by-frame" per the source design's own "realistic temporal boundaries" note -- real MOBA-scale cadence, not a per-tick coin flip */
#define ARENA_SYNERGY_LEAD_SHIFT_SCALE  0.003f /* per resource-race point (cap 2000, ARENA_RESOURCE_CAP) of lead, per tier index -- tuned so a real, not-noise-level lead (ARENA_COMMANDER_RESOURCE_LEAD_THRESHOLD-equivalent, ~300 points, see apps/arena_bot's own analogous constant) meaningfully shifts probability mass toward tier 3 without making it a certainty */
/* Cohesion bonus per tier -- deliberately reuses the exact same attack-speed/move-speed SHAPE
 * East/Music's Catchy Song already established (apply_cdr/update_hero_motion), not a new bonus
 * category. Tier 0 = full cohesion gets the full bonus; each tier down linearly scales it
 * toward 0 at the fully-decayed tier. */
#define ARENA_SYNERGY_TIER0_CDR_PCT           8 /* smaller than Music's own 20% -- an ambient team-cohesion bonus, not a dedicated jungle-objective reward */
#define ARENA_SYNERGY_TIER0_MOVE_SPEED_PCT    8

#define ARENA_HERO_KILL_FLOW         1000
#define ARENA_HERO_KILL_XP             60
/* Assists (S170-187, founder: "assists should gen flow"). Real MOBA convention: anyone else
 * who damaged the victim within a recent window before the kill (not just the hero who landed
 * the killing blow) shares in a smaller bounty -- rewards real team fights, not just the last
 * hit. ARENA_ASSIST_WINDOW_MS mirrors League's own ~10s assist window. Reward is roughly a
 * third of the full kill bounty, same "meaningfully less than the kill, but a real reward, not
 * a token amount" shape ARENA_LANE_CREEP_KILL_FLOW already takes relative to
 * ARENA_NODE_GUARDIAN_KILL_FLOW. */
#define ARENA_ASSIST_WINDOW_MS       10000
#define ARENA_HERO_ASSIST_FLOW        350 /* S170-197: x10, see ARENA_NODE_GUARDIAN_KILL_FLOW's own comment */
#define ARENA_HERO_ASSIST_XP           20
#define ARENA_MAX_ASSIST_TRACK           4 /* how many distinct recent attackers a hero remembers at once -- LRU-evicts the oldest if a 5th lands a hit before this one expires */

/* Multi-kill streak bonus (2026-07-29, founder: "add exponential reward for double tripple
 * penta kills etc" -> "a penta kill gives a huge reward hit and a double kill gives a little
 * more than two normal kills would rewards wise" -> "like a double kill should give the reward
 * of 3 kills and then use the fib[onacci sequence]"). ARENA_MULTIKILL_WINDOW_MS mirrors
 * ARENA_ASSIST_WINDOW_MS's own real-MOBA ~10s precedent -- a hero's Nth kill only counts as
 * part of the SAME streak if it lands within this many ms of their (N-1)th; otherwise the
 * streak resets to 1 (a fresh kill, not a continuation).
 *
 * Growth: streak kill N pays ARENA_HERO_KILL_FLOW/XP times the Nth term of arena_multikill_fib
 * (packages/simulation/arena_game.c) -- 1, 2, 3, 5, 8, 13, 21, ... (Fibonacci, conventionally
 * indexed from N=1 so the sequence doesn't repeat its own leading 1 -- that repeat is exactly
 * why plain 0-indexed Fibonacci wasn't used verbatim). Kills accumulate their own marginal bounty
 * as they land, so the CUMULATIVE total across a streak is the running sum of that sequence:
 * Double (1+2=3x a normal kill's worth -- "the reward of 3 kills," the founder's own example,
 * confirming this exact indexing), Triple (1+2+3=6x), Quadra (1+2+3+5=11x), Penta
 * (1+2+3+5+8=19x -- "a huge reward hit" per the founder's own framing above). No explicit cap: a
 * streak beyond Penta (Hexa+) keeps compounding rather than flattening out, same "no hardcoded
 * ceiling on how good a real teamfight can get" spirit as everything else scaling off game state
 * here rather than a fixed table. Real MOBA naming for reference only (not wired into any
 * HUD/announcement text this pass, flagged not built): 1 Single, 2 Double, 3 Triple, 4 Quadra,
 * 5 Penta. */
#define ARENA_MULTIKILL_WINDOW_MS    10000


typedef struct {
    float x, z;
    float target_x, target_z;
    int moving;
    int hp;
    int max_hp;
    /* mp/max_mp (S170-132, founder: "add mp so toggling stuff has a cost spells cant be
       spammed unless its a zero mana spell or ability"): a second resource layered on top of
       cooldowns, not a replacement for them -- a Q/W/R can be off cooldown and still blocked
       for lack of mana. Regenerates passively (see tick_hero_kit); ARENA_MP_COST_Q/W/R are the
       current flat per-slot rate, applied uniformly across the roster. The "zero mana ability"
       exception the founder named isn't in use by any kit yet, but the cost is already a named
       constant per slot rather than inlined at each call site, so making one specific ability
       free later is a one-line change, not a redesign. */
    int mp;
    int max_mp;
    int attack_cooldown_ms;
    /* attack_windup_ms_remaining (S170-204, NORTHSTAR §17, founder: real-time request for exact
     * League of Legends auto-attack parity -- "does the champion stop when auto-attacking,"
     * confirmed yes). >0 while a basic attack (melee or Gary's ranged homing shot) is mid-windup
     * -- movement is frozen the same way rooted_ms/stunned_ms already freeze it
     * (update_hero_motion), and a genuinely new move command (arena_set_move_target, not the
     * attack-target chase system's own internal re-affirmation) cancels the swing outright: no
     * damage, no cooldown spent, free to reattempt immediately -- the literal §17.1 "canceling
     * the attack outright" behavior. Damage/the projectile only fires once this reaches 0, at
     * which point attack_cooldown_ms is set for real (the backswing + ready window is free
     * movement, no separate field needed -- see this field's own arena_game.c doc comment for
     * why). Not wire-synced/rendered as a cast bar -- unlike Gary's W (a rare, deliberate,
     * multi-second commitment), a basic-attack windup fires constantly in any real fight
     * (~175ms every ~700ms), and a UI bar for every single swing would be noise, not signal;
     * flagged as a deliberate scope decision, not an oversight. */
    int attack_windup_ms_remaining;
    /* blink_cooldown_ms (S170-205, Blink Dagger): a fully separate cooldown track from
     * q/w/r_cooldown_ms -- an item activation, not a kit ability, so it doesn't share or
     * interfere with the ability slots at all. */
    int blink_cooldown_ms;
    /* Donkey fields (S170-206): see ARENA_DONKEY_FOLD_HP_FRACTION's own doc comment for the
     * full design. donkey_fold_ms is the fold's own dedicated duration (distinct from the
     * shared survive_floor_ms it also sets, so the fight-back DPS tick below can tell "is this
     * MY fold" from "some other hero's own R just happens to be using the same generic field
     * right now"); donkey_fight_tick_ms is its fixed-1000ms-interval accumulator, same idiom as
     * every other DPS-zone tick in this file (e.g. r_zone_tick_ms). donkey_fold_proc_cooldown_ms
     * and donkey_glide_cooldown_ms are the item's two independent internal cooldowns.
     * donkey_airborne_ms doubles as this hero's own intangible_ms for the same window when
     * Paper Glide is active (see arena_use_donkey_glide) -- kept as its own field anyway so
     * Weatherman's W (NORTHSTAR §16.3) can specifically target "currently mid-glide," not just
     * "intangible for any reason at all" (Ghost's Not a Ghost/Frog's R also set intangible_ms). */
    int donkey_fold_ms;
    int donkey_fight_tick_ms;
    int donkey_fold_proc_cooldown_ms;
    int donkey_glide_cooldown_ms;
    int donkey_airborne_ms;
    /* mnm_burrow_ms (S170-208, Burrow): dedicated countdown for MnM's own W, distinct from
     * the shared intangible_ms/rooted_ms it also sets at cast time (same "MY window, not
     * anyone else's" reasoning donkey_fold_ms's own comment above gives) -- tick_hero_kit
     * watches specifically for THIS hitting zero to fire the resurface eruption exactly
     * once, since intangible_ms/rooted_ms alone give no "did it just expire" edge of their
     * own once other kits are also free to set them. Also gates all three auto-attack loops
     * (hero-vs-hero, node-guardian creep, lane creep) while > 0 -- he's literally not present on
     * the battlefield surface to swing at anything until he resurfaces. */
    int mnm_burrow_ms;
    /* zagan_r_target (S170-230, Conjunction): the hero-slot index R is currently locked onto,
     * -1 sentinel (same "explicit non-zero sentinel" convention last_attacked_by_owner/
     * cast_target already use -- 0 would wrongly mean "owner slot 0's armor"). Re-validated for
     * hittability every tick arena_hero_armor reads it (unlike cast_target, which only
     * validates once at cast completion) -- if the target dies or otherwise stops being
     * hittable mid-duration, the mirror just falls back to computing Zagan's own real armor
     * normally, no special-case cleanup needed. Reset to -1 on respawn, same as
     * attack_target/cast_target. */
    int zagan_r_target;
    /* zagan_confessed (S170-230, Base Metal Screams): has THIS hero already crossed below 50%
     * HP and triggered Zagan's passive once this life? Lives on every hero, not just Zagan's
     * own slot, since any enemy hero could be the one who "confesses" -- harmless/unused when
     * no Zagan is in the match. Reset to 0 on respawn (own life, own confession). */
    int zagan_confessed;
    /* zagan_calcination_ms (S170-230, Calcination/Q): the armor-shred debuff, lives on the
     * TARGET (any hero can carry it), not on Zagan -- same "generic status effect any hero can
     * carry" shape as slowed_ms/burning_ms below, decremented generically in tick_hero_kit
     * regardless of who cast it. Read by arena_hero_armor. */
    int zagan_calcination_ms;
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
    int duck_smoke_ms;   /* Duck W, Smoke Bomb (S202-10): remaining cloud duration, >0 while active.
                           * Hero-specific rather than reusing r_zone_x/r_active_ms above -- this is a
                           * W ability, and Duck's own R (Total Telekinesis) is an instant pull with no
                           * zone of its own, so there's no real R-zone slot to share here. */
    float duck_smoke_x, duck_smoke_z; /* Duck W: fixed cloud position at cast time */
    /* zone_radius (NORTHSTAR §24 Milestone 2, 2026-07-31): every zone-ability hero before the
     * Cart has exactly ONE zone-shaped ability, so its radius was always just a fixed constant
     * read directly in tick_hero_kit -- no need to store it on the hero. The Cart's W and R are
     * BOTH zone-shaped and share the same r_zone_x/z/r_active_ms fields above (see arena_cast_r's
     * own CART case doc comment on why they aren't split into two separate zone slots), so which
     * radius applies is genuinely ambiguous without this -- set by whichever of W/R most recently
     * activated the zone, read by tick_hero_kit's own CART case. Unused (0) by every other hero. */
    float zone_radius;
    /* cart_delivery_pity (S202-42): per-outcome Fibonacci-pity counters for
     * arena_marble_bag_pick, one entry per ARENA_CART_DELIVERY_OUTCOME_* index. Hero-specific
     * storage (not match-wide) since pity is a per-Cart-player streak, matching the founder's
     * own "occasionally" framing -- a Cart player who keeps rolling Slow should see it get
     * genuinely rarer for THEM specifically, not have their luck shared with the enemy team's
     * own Cart. Zero-initializes correctly with the rest of ArenaHero (fib(0)=1, a real nonzero
     * base weight, not a locked-out one -- see arena_fibonacci's own doc comment). */
    int cart_delivery_pity[ARENA_CART_DELIVERY_OUTCOME_COUNT];
    /* Cast-time ability state (S170-203, founder: "switch gary w to aimed shot just like wow
     * hunter cast time big damage for now movement interrupts cast damage does not interrupt
     * cast silence does"). Generic across any slot/hero, same "shared field names across kits"
     * reasoning as everything else in this block -- Gary's W (Aimed Shot) is the first ability
     * to use it, not the only one this is meant to ever support. casting_slot is 0 when not
     * casting, else 1/2/3 for Q/W/R (same convention as cast_flash_slot). cast_anchor_x/z is
     * the caster's own position the instant the cast began -- checked every tick against the
     * caster's CURRENT position; any real drift (self-directed movement OR forced displacement,
     * e.g. a pull) interrupts, matching "movement interrupts" literally rather than only
     * catching a deliberate move-click. cast_target is the hittable enemy locked in at cast
     * start (-1 if the cast has no single-target component), re-validated for range/hittability
     * only at completion, not every tick -- a target that steps out of range mid-cast still
     * costs the caster the cast, same "real commitment, not a guaranteed poke" convention every
     * other Gary ability already holds itself to. cast_total_ms is the fixed duration the cast
     * started with, kept alongside cast_time_remaining_ms purely so the client can compute a
     * remaining/total progress fraction for the cast-bar UI without needing to know each
     * ability's cast time itself. */
    int casting_slot;
    int cast_time_remaining_ms;
    int cast_total_ms;
    float cast_anchor_x, cast_anchor_z;
    int cast_target;
    /* cast_target_x/z (S202-34, Abraham's Fireball): the ground point
     * locked in at cast start, for a ground-targeted (skillshot) ability --
     * generic, same reasoning as cast_target just above, just for a point
     * instead of a hittable-enemy index. Unused (0) by every cast that has
     * no ground-target component. */
    float cast_target_x, cast_target_z;
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
    /* stunned_ms (S170-184, founder: "add more status effects use GFD [as a reference]" --
     * GoblinFoxDragon's server/status package, Paralyze). > 0: cannot move, cannot cast
     * Q/W/R, cannot auto-attack -- the generic "hard CC" hero_status_label's own doc comment
     * already flagged as missing ("'Stun' and 'slow' aren't modeled as their own generic
     * fields yet ... adding a real stun/slow mechanic is separate kit work, not a HUD task").
     * Distinct from rooted_ms (movement-only) and silenced_ms (casting-only): stun blocks all
     * three action types at once, GFD's own "hard CC" category (GFD models it as a
     * probabilistic per-action-fail-chance roll; this is a hard block for the whole duration
     * instead, simpler and matching this repo's existing `_ms` timer convention for every
     * other status effect above). No kit applies this yet -- arena_apply_stun() is the hook a
     * future ability-kit pass wires up, same "generic infrastructure first, hero-specific kit
     * wiring later" precedent silenced_ms/rooted_ms/burning_ms themselves were built under. */
    int stunned_ms;
    /* slowed_ms/slow_pct (S170-184, GFD's Slow -- there, a negative-haste-percentage stacked
     * against a Haste buff; no generic Haste field exists here yet, out of scope for this pass,
     * flagged not faked, so this is debuff-only). > 0 ms: move speed multiplied by
     * (1.0 - slow_pct) for that duration, read in update_hero_motion. slow_pct is 0.0-1.0 (a
     * fraction, not a flat unit reduction like item_bonus_move_speed) so it scales
     * proportionally regardless of a hero's current speed, including any item bonus already
     * applied. arena_apply_slow() is the kit-wiring hook, same "no kit uses it yet" scope as
     * stunned_ms above. */
    int slowed_ms;
    float slow_pct;
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
    /* combat_timer_ms (S170-148, "mana should slowly regenerate when not in
     * combat"): counts down from ARENA_COMBAT_TIMEOUT_MS, reset to that
     * value by apply_damage() every time this hero takes damage from ANY
     * source (same single choke point damaged_this_tick already uses,
     * matching real WoW's own "any damage taken re-arms the combat timer"
     * rule). Mana regen (tick_hero_kit) is gated on this hitting 0. Honest
     * simplification, flagged not silently narrowed: keyed off damage
     * TAKEN, not damage dealt -- threading an attacker-side signal through
     * every one of this file's damage call sites (melee, every kit's Q/W/R,
     * projectiles, creeps, zone ticks) would be a much larger, riskier
     * change for a case (a hero purely poking from a safe distance,
     * landing hits while never being hit back) that's rare in practice --
     * real fights are overwhelmingly mutual, so "did I take damage
     * recently" already covers the vast majority of "am I actually
     * fighting" correctly. */
    int combat_timer_ms;
    /* mp_regen_accum (S170-150 bugfix): mana regen used to compute
     * `(int)(rate * dt_ms / 1000.0f)` fresh every call with no persistence
     * across ticks -- at this codebase's own real production tick rate
     * (arena_server always calls arena_update()/arena_update_teams() with
     * dt_ms=16), that's `(int)(6 * 16 / 1000.0) == (int)0.096 == 0`, EVERY
     * single tick, for every regen rate this file has ever used. Mana
     * regen had silently never actually worked in real gameplay -- only in
     * tests, which happen to call with large dt_ms=1000 "one full tick"
     * steps that mask the truncation. A persistent float accumulator fixes
     * this the same way a real game's resource regen has to: fractional
     * progress carries over between ticks instead of being discarded each
     * time, and a whole point is applied (and subtracted back out of the
     * accumulator) once enough of it has built up. */
    float mp_regen_accum;
    /* berserker_ms (S170-190, founder: "add berserker and health regen powerups like from
     * warsong gulch"): > 0 while the Berserker powerup buff is active, adding
     * ARENA_BERSERKER_BONUS_AD on top of item_bonus_ad at the same damage call sites items
     * already flow through (melee, node-guardian/lane creeps, Gary's homing shot) -- same "flat bonus,
     * not a new damage model" shape as items. regen_ms/regen_accum: the Restoration powerup's
     * HP-per-second buff, same fractional-accumulator idiom as mp_regen_accum just above (a
     * flat per-tick add would truncate to 0 at this game's real 16ms tick rate, same reasoning
     * that field's own doc comment already gives). */
    int berserker_ms;
    int regen_ms;
    float regen_accum;
    /* King buffs (Jungle Camps Milestone 2, 2026-08-10) -- docs2/JUNGLE_CAMPS_NORTHSTAR.md §3.3.
     * king_music_carrier: East/Music's Catchy Song -- unlike every other buff field in this
     * struct, this is NOT a countdown timer. It's a persistent flag that survives this hero's
     * OWN death (the memset-on-respawn in arena_respawn_hero clears it like everything else,
     * but arena_respawn_hero then explicitly re-grants it if any OTHER living teammate still
     * carries it -- "the song reaches them"). The buff effect itself (apply_cdr, update_hero_
     * motion) only ever reads this while alive, so a stale value on a dead hero is harmless.
     * king_growth_stacks/king_growth_ms: South/Growth's Bloodroar -- ordinary timer + counter,
     * both correctly wiped by the plain memset on death/respawn, no special handling (the
     * deliberate opposite of Music).
     * king_wealth_ms: North/Wealth's Bulwark -- held by whoever claimed it; the proximity aura
     * itself is computed live off whichever hero(es) currently have this > 0 (see arena_hero_
     * armor()), never copied onto nearby allies' own fields.
     * West/All-Seeing's Farsight is genuinely team-wide, not per-hero -- see ArenaState's own
     * king_allseeing_team_ms[2] instead. */
    int king_music_carrier;
    int king_growth_stacks;
    int king_growth_ms;
    int king_wealth_ms;
    /* king_allseeing_display, 2026-08-20: client-network-parse-only mirror of ArenaState's
     * team-wide king_allseeing_team_ms[2] (see that field's own doc comment for why All-Seeing
     * is team-wide, not per-hero, unlike everything else in this struct) -- ArenaHeroSnapshot's
     * king_buff_flags bit 3 packs the killer's own team's value onto every hero snapshot so the
     * client's buff HUD can read one consistent per-hero field like the other three buffs,
     * without needing a separate team-wide network message. Never read or written by the
     * server-side simulation itself -- purely a display mirror. */
    int king_allseeing_display;
    /* w_drain_accum (S170-181, founder: "instead of initial mana cost toggle spells should
     * drain mana over time"): same fractional-accumulator idiom as mp_regen_accum right above,
     * for the same reason -- ARENA_MP_DRAIN_W_PER_SEC * dt_ms/1000 truncates to 0 almost every
     * real 16ms tick if applied directly, so the fractional remainder has to persist across
     * ticks instead of being discarded. Only ever nonzero while a TRUE toggle ability (Unicorn/
     * Loki/Gary/Flute Debt/Bacon Puck/Abraham/Ada/Gunnr/He Xiangu/MnM's own w_active, per
     * arena_toggle_w's own case list) is active; drains nothing for the OTHER W-slot heroes
     * (Ghost, Frog, etc.) whose W is an instant effect on cooldown, not a hold state. */
    float w_drain_accum;
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
    /* respawn_ms_remaining (S170-121): only meaningful while !alive in team
     * mode. Set to ARENA_HERO_RESPAWN_MS on death; counts down to 0, then
     * arena_update_teams holds the hero at 0 and retries the node-control
     * check each tick until the team owns a node to respawn onto. Unused by
     * the 1v1 local demo (arena_update), which still ends on first death. */
    int respawn_ms_remaining;
    /* cast_flash_slot (S170-124, "particle effects for spells"): 0 = none,
     * 1/2/3 = Q/W/R -- set unconditionally the instant a cast clears its
     * gate (alive, not silenced, off cooldown) in arena_cast_q/toggle_w/
     * cast_r, regardless of whether that specific cast goes on to hit
     * anything. A real cast animation fires on cast, not just on a landed
     * hit, same convention as any real MOBA. Consumed once per tick by
     * server_broadcast (packaged into the wire snapshot) and cleared right
     * after, same one-tick-lifetime idiom as damaged_this_tick. Unused by
     * the 1v1 local demo, which renders straight off arena_state with no
     * wire hop needed. */
    int cast_flash_slot;
    /* Skillchain state (REDGARDEN_GUI_NORTHSTAR.md Milestone 2, 2026-07-31): tracked on the
     * TARGET, not the caster -- real FFXI closes a chain when a SECOND weapon skill lands on
     * the SAME target within the window of the FIRST, from any source (self-chain or a
     * teammate's own weapon skill both count). sc_pending_attr_count==0 means no open window.
     * Set/read by apply_weapon_skill_damage (arena_game.c), the one choke point every real
     * weapon-skill cast (warrior_cast_q/w/r today, future jobs' own kits later) routes through
     * -- ordinary abilities and basic attacks never touch this, matching real FFXI (only WS
     * close/continue chains, not spells or melee swings). */
    ArenaResonance sc_pending_attrs[ARENA_SC_MAX_ATTRS];
    int sc_pending_attr_count;
    unsigned int sc_pending_age_ms;
    /* skillchain_flash_tier (Milestone 2): 0 = no chain this tick, 1/2/3 = the tier that just
     * closed on this hero -- a new, distinct wire-visible event, deliberately NOT folded into
     * cast_flash_slot or the generic hit-feedback path, per the northstar's own explicit
     * requirement ("rendered as a real, distinct visual event"). Same one-tick set-then-clear
     * lifetime as cast_flash_slot; client rendering of it is a real follow-up gap (this
     * milestone is server-authoritative simulation only, same scoping as Milestone 1's own
     * client-rendering gap). */
    int skillchain_flash_tier;
    /* is_clone/clone_owner (S170-141, Tyler's "true Meepo parity" puppet
     * clones): is_clone=1 marks this slot as an AI-driven puppet, never
     * client-owned -- clone_owner is the real owner index (Tyler's own
     * slot) it's linked to for move-mirroring and the shared-fate death
     * rule. Unused (0/-1) by every real, client-owned hero slot. */
    int is_clone;
    int clone_owner;
    /* attack_target (S170-162, NORTHSTAR §17's click-to-attack system):
     * -1 = no lock, else the owner slot this hero is currently
     * attack-commanded to engage. Set by PACKET_ARENA_ATTACK
     * (arena_set_attack_target), cleared by a fresh PACKET_ARENA_MOVE
     * (arena_set_move_target), by the target dying/becoming unhittable, or
     * by re-attacking a different target. Consumed by
     * arena_tick_attack_targets: while set and the target is out of this
     * hero's own attack range, movement is overridden to chase the
     * target's live position every tick (pure pursuit, no intercept
     * prediction -- matches real League exactly, see §17.1). Melee heroes'
     * actual damage still comes from the existing proximity-based combat
     * loop once chase closes the distance (unchanged); ranged heroes (Gary
     * so far) fire their homing auto-attack (ArenaProjectile.homing_target)
     * directly at this lock once in range. */
    int attack_target;
    /* attack_move_active/attack_move_x/z (NORTHSTAR.md §17.4's own unchecked "attack-move
     * command" item, and §24 Milestone 2's real WC3 group-order vocabulary -- the same real gap,
     * closed once, 2026-07-31). Real LoL/WC3 "A + click": moves toward attack_move_x/z like a
     * plain move, but opportunistically diverts to attack_target the moment a valid enemy comes
     * within range along the way (arena_tick_attack_move), unlike a direct attack-target lock
     * (survives the acquired target dying -- re-scans for a new one instead of going idle) and
     * unlike a plain move (which never initiates combat at all, §17.1). target_x/z get
     * overwritten during a chase (arena_tick_attack_targets' own "the attack command wins while
     * it's active" precedent) -- attack_move_x/z remember the ORIGINAL destination so movement
     * can resume there once nothing's left to opportunistically engage. Cleared by any other
     * move/attack/stop command, same "a new command always wins" convention every one of those
     * already enforces on attack_target. */
    int attack_move_active;
    float attack_move_x, attack_move_z;
    /* hold_position (§24 Milestone 2, real WC3 "Hold Position," 2026-07-31): third of the group-
     * order vocabulary. A held unit never moves to chase (arena_tick_attack_targets' own chase
     * branch skips movement and drops the lock instead when a held unit's target leaves range --
     * "defend this spot," not "give up entirely," since arena_tick_attack_move's own scan (also
     * extended to run for held units, not just attack-move ones) re-acquires whoever's actually
     * in range next tick, possibly the same target wandering back or a new one). Melee heroes'
     * real damage already comes from the always-on flat proximity loop regardless of any lock
     * (§17.3), so holding "just works" for them the moment they stop moving; ranged heroes
     * (Gary so far) only ever fire through attack_target, which is why the scan extension above
     * is needed for holding to mean anything for them at all. */
    int hold_position;
    /* patrol_active/patrol_a_x/z/patrol_b_x/z/patrol_going_to_b (§24 Milestone 2, real WC3
     * "Patrol," 2026-07-31): fourth and last of the group-order vocabulary. Point A is the
     * unit's own position at the moment patrol was issued, point B is the clicked point --
     * arena_tick_patrol walks the unit back and forth between them forever (patrol_going_to_b
     * flips once the current leg's destination is reached), opportunistically engaging whatever
     * comes within range along the way (same shared scan arena_tick_attack_move/hold already
     * use), same real WC3 "patrol a route, fight anything you run into" behavior. Cleared by any
     * other move/attack/attack-move/hold/stop command, same "a new command always wins"
     * convention every other group order already follows. */
    int patrol_active;
    float patrol_a_x, patrol_a_z;
    float patrol_b_x, patrol_b_z;
    int patrol_going_to_b;
    /* flow/xp (S170-175, founder: "we need a character display pane that
     * shows current stats" / "tracking xp and flow" / "we call gold
     * flow"): the two per-hero progression resources NORTHSTAR §19 spec'd
     * as deliberately separate from resources[team] (the team-level
     * win-condition meter, untouched by any of this) -- Flow is spent at a
     * team's own shop (arena_shop_buy/sell) on equipped items
     * (equipped_item[] below); XP feeds a flat, roster-wide power curve
     * (§19.4, not a per-level ability-point system). Both start at 0 and
     * only ever grow via real kills (node-guardian/lane creep, hero) -- no
     * passive trickle, matching real MOBA "you earn it by doing
     * something" precedent, same reasoning §19.2 already gives for why
     * this isn't a LoL-style base-gold-over-time tick. */
    int flow;
    /* flow_earned (S170-175, founder: "(flow earned not counting spent)"):
     * lifetime cumulative total, only ever increases -- flow above is the
     * current SPENDABLE balance (decreases on purchase, used for shop
     * affordability checks); the character pane displays this field
     * instead, since "how much have I actually earned" is the real stat,
     * not a number that goes back down every time you buy something. */
    int flow_earned;
    int xp;
    /* kills/deaths (S170-175, founder: "stats page shows team and
     * individual kd ratio flow and xp"): hero kills only, same real-MOBA
     * KDA convention -- creep kills feed Flow/XP (above) but never these
     * two, so the ratio reflects hero-vs-hero fighting specifically. Team
     * K/D is never stored separately -- it's just the sum of each team's
     * own heroes' kills/deaths, computed client-side for display, no new
     * synced field needed for that aggregate. */
    int kills;
    int deaths;
    /* multikill_count/multikill_timer_ms (2026-07-29, see ARENA_MULTIKILL_WINDOW_MS's own doc
     * comment for the full reward-curve design): how many kills long this hero's CURRENT streak
     * is, and how much longer (ms) a fresh kill still counts as a continuation of it rather than
     * starting a new streak. Reset to 0 whenever this hero itself dies -- a real MOBA convention
     * (dying ends your own streak) and also just correct: a dead hero can't be mid-streak. */
    int multikill_count;
    int multikill_timer_ms;
    /* last_attacked_by_owner (S170-175): same "who gets credit on the
     * kill" idiom ArenaCreep already established -- -1 = never hit since
     * spawning/respawning, else the owner index of whoever last damaged
     * this hero. Set only at the hero-vs-hero melee/homing-shot damage
     * sites (resolve_combat, the team-mode melee loop,
     * arena_tick_attack_targets's Gary branch) -- ability casts don't set
     * this, so a kill finished by a spell grants no Flow/XP bounty this
     * pass, same "not every damage source needs full reward wiring,
     * flagged not faked" precedent arena_zone_damage_creeps already set
     * for AoE-vs-creep kills. */
    int last_attacked_by_owner;
    /* assist_owner/assist_ms (S170-187, founder: "assists should gen flow"): a small recent-
     * attackers memory, separate from last_attacked_by_owner above (which only ever remembers
     * the SINGLE most recent hit, exactly what a kill-attribution check needs, not what an
     * assist check needs -- multiple different heroes can each have damaged this hero within
     * the assist window). -1 in assist_owner[i] means that slot is empty; assist_ms[i] is how
     * much longer that attacker still counts for an assist if this hero dies. Recorded at the
     * exact same damage call sites last_attacked_by_owner already is (melee, homing-shot),
     * same "ability casts don't grant reward credit" scoping. */
    int assist_owner[ARENA_MAX_ASSIST_TRACK];
    int assist_ms[ARENA_MAX_ASSIST_TRACK];
    /* equipped_item (S170-175): -1 = empty, else an index into
     * ARENA_ITEMS, one slot per ArenaItemSlot. No inventory/bag this
     * pass (founder: "no bag... no unequip into bag for now") -- buying
     * auto-equips (replacing and auto-selling whatever was there), an
     * explicit sell empties a slot for a partial refund. */
    int equipped_item[ARENA_ITEM_SLOT_COUNT];
    /* item_bonus_* (S170-175): cached aggregate of every equipped item's
     * stat bonuses, recomputed by arena_recompute_item_stats whenever the
     * loadout changes (buy/sell/respawn) rather than summed fresh every
     * tick -- max_hp/max_mp are applied directly at recompute time (see
     * that function), these four are consumed elsewhere: item_bonus_armor
     * by arena_hero_armor(), item_bonus_ad by the melee/homing-shot damage
     * call sites, item_bonus_move_speed by update_hero_motion,
     * item_bonus_cdr_pct (S170-207, Haste Trinket) by apply_cdr. */
    int item_bonus_armor;
    int item_bonus_ad;
    float item_bonus_move_speed;
    int item_bonus_cdr_pct;
    /* item_bonus_true_dmg / item_bonus_lifesteal_pct (2026-08-11 "expand the play space" pass):
     * same recomputed-cache shape as the fields above, see ArenaItemDef's own doc comment on
     * bonus_true_dmg/bonus_lifesteal_pct for the full design. */
    int item_bonus_true_dmg;
    int item_bonus_lifesteal_pct;
    /* item_bonus_attack_range_pct (S202-34, Kite String trinket, founder: "add an item that
     * increases auto attack range by 4% 3333 flow"): same recomputed-cache shape as the fields
     * above, consumed by arena_hero_attack_range(). */
    int item_bonus_attack_range_pct;
    /* item_bonus_mp_regen_combat (S205-87, Luck of the Draw trinket): same recomputed-cache
     * shape as the fields above, consumed directly by the mana-regen tick's own in-combat rate
     * (arena_game.c, "Mana regen" comment). */
    int item_bonus_mp_regen_combat;
    /* facing_rad (S202-40, Bacon+Puck's Shadow Step, founder: "add a sense of hero direction i
     * guess so we can actually teleport behind them"): a real, server-authoritative facing
     * angle, updated in update_hero_motion from the live movement direction (atan2f(dx, dz),
     * same x/z-ordering convention apps/arena's own CLIENT-side hero_facing_rad already used
     * purely for rendering) -- this is the first time a facing concept exists server-side, not
     * just interpolated client-visual. Preserved (not reset) while stationary, matching every
     * real MOBA's own "you keep facing whichever way you last moved" convention. Radians,
     * standard atan2f range (-pi, pi]. */
    float facing_rad;
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

/* ArenaCreep (S170-51): one node-guardian creep per node, index-matched
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

/* ArenaTower (2026-07-30): one per node, index-matched (towers[n] belongs to nodes[n]). See the
 * ARENA_TOWER_MAX_HP header comment above for the full design. No flavor/respawn fields, unlike
 * ArenaCreep -- a tower is always neutral-hostile to both teams and never comes back once dead. */
typedef struct {
    float x, z;
    int hp, max_hp;
    int alive;
    int attack_cooldown_ms;
    int last_attacked_by_owner; /* -1 = never hit since spawning, else who gets kill credit */
} ArenaTower;

/* ArenaProjectile (S170-136, on-hit status effects generalized S170-140): a
 * real travelling skill-shot, not an instant hit. Straight-line, no homing:
 * velocity is fixed at spawn from the caster's position toward the target's
 * position AT CAST TIME, so a target that moves after the shot is fired can
 * genuinely dodge it by stepping off the original line. One shared pool
 * serves every projectile-casting hero; hero_id is carried along so the
 * client can pick a distinct visual per spell (not just per Q/W/R slot).
 *
 * on_hit_silence_ms/on_hit_root_ms/on_hit_burn_ms/on_hit_burn_dps (S170-140):
 * generic optional status effects applied to whoever the shot actually hits,
 * on top of the flat `damage` every projectile already deals -- 0 means "this
 * shot doesn't apply that effect," same "generic field, not hero-specific
 * storage" convention as the identically-named fields already on ArenaHero.
 * Added converting Ghost's Q (Alien Frequency, silence) and Tyler's Q
 * (Earthbind, root+burn) from instant-hit to real projectiles -- Gary's Q
 * (plain damage only) leaves all four at their zeroed default via
 * arena_spawn_projectile. Server-only, same as damage/radius/velocity --
 * the client only ever needs to draw where the shot currently is. */
#define ARENA_MAX_PROJECTILES 32

typedef struct {
    int active;
    int owner;   /* hero slot that fired it -- for the client's self/team/enemy color convention */
    int team;    /* cached at spawn: which team it damages the OPPOSITE of, even if the caster dies/respawns mid-flight */
    ArenaHeroID hero_id; /* which spell this is, for client-side visual style */
    float x, z;
    float vx, vz;     /* units/sec -- ignored every tick a homing shot re-steers, see homing_target below */
    float radius;
    int damage;
    float max_range;  /* total travel distance before despawning unhit (a whiff) */
    float traveled;
    int on_hit_silence_ms;
    int on_hit_root_ms;
    int on_hit_burn_ms;
    int on_hit_burn_dps;
    /* homing_target (S170-163, founder: "gary auto attacks are projetiles
     * that always hit (visually projectile) they can still miss or crit as
     * normal but you cant juke them"): -1 = an ordinary skill-shot, exactly
     * the fixed-velocity/dodgeable behavior this struct's own doc comment
     * above describes, unchanged. >=0 = a homing basic auto-attack (Gary's
     * so far) -- the owner slot it's actively tracking. arena_tick_projectiles
     * re-aims vx/vz toward that target's LIVE position every tick instead
     * of flying the fixed line set at spawn, so it connects regardless of
     * how the target moves afterward (not a skillshot, matches NORTHSTAR
     * §17.2's real-League ranged-auto-attack behavior exactly) -- the
     * "can't juke them" part of the ask. Fizzles without dealing damage if
     * the target dies/becomes unhittable before the shot arrives (no floating
     * hit registers on a target that's no longer there). This engine has no
     * miss/crit RNG at all today (checked before building this -- every
     * attack in this codebase is flat, deterministic damage), so "they can
     * still miss or crit as normal" is a no-op against a mechanic that
     * doesn't exist yet; homing only ever changes whether a shot connects
     * via POSITIONING, never whether it connects via chance. */
    int homing_target;
    /* pierce/pierced_mask (S202-34, Abraham's Fireball): 0 = the existing
     * behavior above (deactivates on first hit, one target only). 1 = a
     * real piercing skill-shot -- keeps travelling and can hit MULTIPLE
     * enemies, one bit per hero slot in pierced_mask tracking who this
     * exact shot has already damaged so a slow-moving pierce can't double-
     * or triple-tick the same target while it's still overlapping them.
     * Still despawns at max_range like any other shot; still real armor/
     * on-hit-status application per hit, just without the early return.
     * ARENA_MAX_HEROES <= 32 (checked at the one real call site) so a
     * plain uint32_t bitmask is enough, no array needed. */
    int pierce;
    unsigned int pierced_mask;
} ArenaProjectile;

/* ArenaObstacle: static jungle terrain, see the ARENA_OBSTACLE_COUNT
 * comment above for placement rationale. `radius` is the collision/visual
 * footprint (a circle -- the client draws it as one or two boxes, see
 * draw_obstacle in apps/arena, but collision itself stays circle-vs-circle
 * for the same cheap-and-good-enough reason hero-vs-hero would be).
 *
 * hp/max_hp (2026-08-25, ARENA_HERO_TREE passive, see this file's own
 * "Tree passive" section below): only meaningful for ARENA_OBSTACLE_TREE --
 * rocks leave both at 0/unused, same "field exists but only one kind reads
 * it" convention ArenaHero's own kit-specific fields (e.g. king_growth_ms)
 * already use rather than a separate per-kind struct. Position/radius/kind
 * stay static and never wire-synced (this struct's own doc comment above);
 * hp is the one genuinely dynamic field and IS wire-synced (protocol.h's
 * ArenaSnapshotMsg.obstacle_hp), same "static layout, dynamic state
 * synced separately" split powerups already use for kind/active vs
 * position. */
typedef struct {
    float x, z;
    float radius;
    ArenaObstacleKind kind;
    int hp, max_hp;
} ArenaObstacle;

/* ArenaDamageLogEntry (S189-01, "go ahead and add the damage log to REDGARDEN"): one real
 * damage event, rolling last-N feed (standard real-MOBA combat-log UX -- League of Legends and
 * Dota 2 both do this, not a scrollable persistent history). v0 scope, deliberately: damage
 * events only, not kills/buffs/objectives (those are real, separate features).
 *
 * source_hero_id honest limitation: apply_damage() (this file's own single choke point for all
 * hero damage, ~50 call sites) is NOT changed to thread an attacker through every site -- that
 * would mean touching all ~50 call sites' argument lists under real time pressure, a bigger,
 * riskier change than this feature needs. ArenaHero's own last_attacked_by_owner field looked
 * like a shortcut but isn't reliably fresh for this purpose (S170-175's own doc comment: "only
 * ever set at the melee/homing-shot damage sites," left stale by ability-damage calls that don't
 * touch it) -- using it here would misattribute ability damage to a stale prior melee attacker.
 * So: apply_damage()'s own default path logs with source_hero_id = ARENA_HERO_COUNT (a real,
 * out-of-range sentinel -- every real ArenaHeroID is < ARENA_HERO_COUNT) meaning "unattributed."
 * The one path upgraded to real attribution is resolve_combat's direct hero-vs-hero duel (both
 * hero pointers already in scope there, zero risk) via apply_damage_ex(). Ability/creep/tower
 * damage stays unattributed in this pass -- flagged as a real, deliberate scope narrowing, not
 * silently dropped. */
#define ARENA_DAMAGE_LOG_CAPACITY 12
typedef struct {
    ArenaHeroID target_hero_id;
    ArenaHeroID source_hero_id; /* ARENA_HERO_COUNT sentinel = unattributed, see doc comment above */
    int amount;
} ArenaDamageLogEntry;

typedef struct {
    ArenaHero heroes[ARENA_HEROES_ARRAY_SIZE]; /* S170-141: real per-player range 0..ARENA_MAX_HEROES-1, puppet-clone range after it -- see ARENA_HEROES_ARRAY_SIZE's own doc comment */
    ArenaNode nodes[ARENA_NODE_COUNT];
    ArenaCreep creeps[ARENA_MAX_CREEPS];
    ArenaTower towers[ARENA_NODE_COUNT]; /* 2026-07-30: index-matched to nodes, same convention as creeps */
    ArenaProjectile projectiles[ARENA_MAX_PROJECTILES];
    ArenaObstacle obstacles[ARENA_OBSTACLE_COUNT];
    ArenaPowerup powerups[ARENA_POWERUP_COUNT]; /* S170-190 */
    ArenaLaneCreep lane_creeps[ARENA_MAX_LANE_CREEPS]; /* S170-139 */
    int lane_wave_timer_ms[2]; /* S170-139: per-team countdown to next wave; starts at 0 (memset), so both teams' first wave spawns on the first tick, matching a real MOBA's 0:00 wave */
    ArenaCampMinion camp_minions[ARENA_MAX_CAMP_MINIONS]; /* Jungle camps Milestone 1 */
    int camp_wave_timer_ms[ARENA_CAMP_COUNT]; /* per-camp countdown to next wave; starts at 0 (memset) -- camps wave from the opening bell, docs2/JUNGLE_CAMPS_NORTHSTAR.md §3.2 */
    int camp_uncleared_ms[ARENA_CAMP_COUNT]; /* §3.4 anti-stall escalation -- ticks up while a camp has any active minion, resets to 0 the instant it's fully cleared */
    int camp_escalated[ARENA_CAMP_COUNT]; /* 1 once camp_uncleared_ms crosses ARENA_CAMP_ESCALATION_THRESHOLD_MS -- that camp's minions march instead of standing still */
    ArenaKing kings[ARENA_CAMP_COUNT]; /* Jungle camps Milestone 2 -- index-matched to camps (0=N/Wealth, 1=S/Growth, 2=E/Music, 3=W/All-Seeing) */
    int king_spawn_timer_ms[ARENA_CAMP_COUNT]; /* per-camp countdown, dual-purpose (Milestone 4): counts toward ARENA_KING_SPAWN_DELAY_MS before a King's first-ever spawn (gated on max_hp == 0, see arena_tick_kings), or toward ARENA_KING_RESPAWN_MS after a kill (reset to 0 the instant a King dies, gated on !active with max_hp > 0) -- one field serves both, since a King is never simultaneously "never spawned" and "dead," so which threshold applies is never ambiguous. Lives in arena_state (not a function-static) same as every other per-match timer in this file, so arena_init_teams()'s memset correctly resets it between matches */
    int king_allseeing_team_ms[2]; /* West/All-Seeing's Farsight -- genuinely team-wide (see ArenaHero's own king_wealth_ms doc comment for why this one's different from the other three) */
    int wealth_gold_tick_ms; /* North/Wealth's gold-trickle accumulator -- see arena_tick_kings' own doc comment; arena_state, not a function-static, same reasoning as king_spawn_timer_ms above */
    int synergy_tier[2]; /* §25.3 -- current cohesion tier per team, 0 (memset default) = full cohesion */
    int synergy_roll_timer_ms; /* single shared timer -- both teams re-roll on the same cadence, see arena_tick_synergy's own doc comment */
    int fountain_tick_ms; /* S170-147: fixed-interval (1000ms) accumulator for the fountain heal tick, same idiom as every other DPS/heal zone's own r_zone_tick_ms -- global, not per-hero, since a fountain heals whoever's nearby, not a single caster's target */
    /* resources[2]/resource_tick_ms (S170-153, "true arathi basin node
     * control resource management as a win con instead of team wipe"):
     * per-team accumulated resource points, real Arathi-Basin-style --
     * ticks up over time based on how many nodes each team currently
     * controls (more territory = faster race to the cap), first team to
     * ARENA_RESOURCE_CAP wins. Team-mode only, see arena_tick_resources()'s
     * own doc comment for the full design. */
    int resources[2];
    int resource_tick_ms;
    int match_elapsed_ms; /* S170-157: team-mode-only running clock, ticks up every arena_update_teams call -- feeds the sudden-death fallback below, see ARENA_MATCH_MAX_DURATION_MS's own doc comment */
    int respawn_wave_timer_ms; /* S170-154: global, ticks 0->ARENA_RESPAWN_WAVE_MS continuously and wraps -- every dead hero respawns together the instant it wraps, not on their own independent per-hero timer */
    /* hover_target (S170-143, "hover casting like in wow macros"): per-owner,
     * real per-player range only (clones never cast independently, see
     * S170-141) -- which hero slot owner[i] was hovering the instant they
     * last cast, -1 = none. Set by arena_set_hover_target() right before
     * dispatching a cast (both the networked path via apps/arena_server and
     * the local 1v1 demo's own direct keybind handler), consulted by
     * arena_hover_ally_or_nearest(). Explicitly reset to -1 after every
     * memset (0 would wrongly mean "owner slot 0", not "no hover target" --
     * same sentinel-after-memset idiom as ArenaCreep's
     * last_attacked_by_owner). */
    int hover_target[ARENA_MAX_HEROES];
    /* ground_target (S202-34, Abraham's Fireball): same per-owner,
     * set-right-before-dispatch shape as hover_target above, for
     * ground-targeted (skillshot) abilities instead of unit-targeted ones.
     * has_ground_target[i] is 0 unless the owner's most recent cast packet
     * carried a real click point (ArenaCastCmd.has_ground_target); the
     * individual cast function decides whether it actually cares (only
     * Abraham's W does today), same "generic, not hero-specific" idiom as
     * hover_target. Set by arena_set_ground_target(), called from both the
     * networked path (apps/arena_server's PACKET_ARENA_CAST handler) and
     * the local 1v1 demo's own direct keybind handler. */
    int has_ground_target[ARENA_MAX_HEROES];
    float ground_target_x[ARENA_MAX_HEROES];
    float ground_target_z[ARENA_MAX_HEROES];
    float time_of_day_sec; /* day/night cycle accumulator (seconds, not ms -- matches SHANKPIT retro_sky.c's own time_sec convention directly, no unit conversion at the call site), ticked from the same arena_update_teams path arena_tick_kings already uses */
    float prev_moon_height; /* moon_dir_y from the previous tick -- local-maximum (zenith) detection compares consecutive samples instead of computing an exact analytical crossing, robust to the accumulator running indefinitely with no explicit wrap logic needed */
    int moon_was_rising; /* 1 if moon_height was still increasing as of the previous tick -- "was rising, now falling" is the zenith-just-passed condition */
    int daynight_zenith_fired; /* edge-trigger guard: 1 once this cycle's moon-zenith event has fired, reset to 0 once moon_height drops back below ARENA_DAYNIGHT_ZENITH_REARM_THRESHOLD (clearly-descended, not near zenith) -- without this the mod would fire on every tiny wobble near the peak instead of exactly once per real cycle */
    int bloodflower_active; /* 1 while an unclaimed Bloodflower exists in the world */
    float bloodflower_x, bloodflower_z; /* always (0,0), map center -- kept as real fields (not a hardcoded literal at every read site) so a future non-center spawn point is a one-line change, same pattern ArenaCampMinion.x/z use even though camp positions are currently deterministic too */
    int bloodflower_ms_remaining; /* counts down from ARENA_BLOODFLOWER_LIFETIME_MS; despawns at 0 */
    ArenaDamageLogEntry damage_log[ARENA_DAMAGE_LOG_CAPACITY]; /* S189-01: real combat damage log, ring buffer -- see ArenaDamageLogEntry's own doc comment */
    int damage_log_head; /* next write index, wraps -- 0 (memset default) is correct at match start */
    int damage_log_count; /* how many entries are actually valid so far, caps at ARENA_DAMAGE_LOG_CAPACITY -- distinct from head so the UI doesn't render stale zeroed slots before the buffer's first lap */
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
/* arena_bot_tick_heuristic/bot_cast_kit_if_ready (S170-228): the stable, never-RL-driven
 * heuristic bot AI arena_bot_tick itself used before this pass -- kept public specifically so
 * apps/arena_training/src/headless.c's own training harness can drive owner 1 (the training
 * opponent) with a fixed, independent heuristic instead of arena_update's own automatic
 * bot-tick path, which arena_bot_tick itself now routes through the trained RL policy. See
 * arena_bot_tick_heuristic's own doc comment in arena_game.c for the full "why training needs
 * this" reasoning. */
void arena_bot_tick_heuristic(unsigned int dt_ms);
void bot_cast_kit_if_ready(ArenaHero *bot, ArenaHero *foe);

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

/* arena_set_hover_target (S170-143): records which hero slot `owner` was
 * hovering at the moment of a cast, -1 for none. Called right before
 * dispatching a cast from both apps/arena_server's PACKET_ARENA_CAST
 * handler and the local 1v1 demo's own direct keybind handler -- generic on
 * purpose (not Doc-Wheel-specific storage), so any future hover-aware
 * ability reuses the same field, same "generic, not hero-specific" idiom as
 * the status-effect fields on ArenaHero. No-op if owner is out of the real
 * per-player range. */
void arena_set_hover_target(int owner, int target);

/* arena_set_ground_target (S202-34): records the ground point `owner`'s
 * most recent cast packet carried, for ground-targeted (skillshot)
 * abilities -- see arena_state.has_ground_target's own doc comment.
 * has_target 0 means "no ground point on this cast" (ordinary unit-
 * targeted/self-targeted cast), in which case x/z are ignored. No-op if
 * owner is out of the real per-player range. */
void arena_set_ground_target(int owner, int has_target, float x, float z);

/* arena_fibonacci (S202-09/S202-42): fib(0)=fib(1)=1 (a real "never fully locked out" floor for
 * a fresh/reset pity counter -- the textbook fib(0)=0 would zero out an outcome's weight
 * entirely), grows the standard way after that. Real, generic, not Cart-specific -- exposed for
 * arena_marble_bag_pick's own use and any future caller. */
int arena_fibonacci(int n);

/* arena_marble_bag_pick (S202-09, NORTHSTAR's own long-documented "weighted marble-bag +
 * Fibonacci-pity pull algorithm," first real implementation anywhere in this repo -- flagged in
 * that doc as "worth building once as a shared utility rather than twice as unrelated bespoke
 * code" for exactly this reason, so this is a real, generic, non-Cart-specific primitive, not
 * buried as a static helper): weighted-random pick among `n` outcomes. `pity[i]` is how many
 * consecutive picks outcome i has been passed over; effective weight = weights[i] *
 * arena_fibonacci(min(pity[i], ARENA_MARBLE_BAG_MAX_PITY_TIER)) -- an outcome that keeps losing
 * gets progressively more likely, uncapped in principle but tier-capped in practice so the
 * multiplier doesn't run away. Updates `pity` in place: the winner resets to 0, every other
 * outcome's pity increments by 1. Caller owns `pity`'s storage (per-hero, per-match, wherever a
 * given use case wants pity scoped to) and passes it in fresh each call -- no hidden global
 * state. Returns -1 if n<=0 or every effective weight is 0 (caller error, not a real pick). */
#define ARENA_MARBLE_BAG_MAX_PITY_TIER 10 /* fib(10)=55 -- a real but bounded ceiling, not runaway growth */
int arena_marble_bag_pick(const int *weights, int *pity, int n);

/* arena_set_attack_target (S170-162): PACKET_ARENA_ATTACK's server-side
 * entry point -- locks `owner` onto `target` (a real, in-range-of-the-real-
 * roster hero slot; -1 clears the lock outright). No validity/team checks
 * here on purpose, same "record intent, validate on consumption" split
 * arena_set_hover_target already uses -- arena_tick_attack_targets is what
 * actually clears a lock that turns out to be invalid (dead, own team,
 * etc.) each tick, so a target that becomes invalid AFTER the lock was set
 * (e.g. dies mid-chase) self-heals on the very next tick without needing a
 * second code path here. */
void arena_set_attack_target(int owner, int target);

/* arena_stop_unit (NORTHSTAR.md §24 Milestone 2, 2026-07-31): see the .c definition's own doc
 * comment. Cancels owner's current move/attack order in place -- the real WC3 "Stop" command. */
void arena_stop_unit(int owner);

/* arena_set_attack_move_target / arena_tick_attack_move (NORTHSTAR.md §17.4 + §24 Milestone 2,
 * 2026-07-31): see the .c definitions' own doc comments and ArenaHero's own
 * attack_move_active/x/z field comment. Real LoL/WC3 "A + click." */
void arena_set_attack_move_target(int owner, float x, float z);
void arena_tick_attack_move(unsigned int dt_ms);

/* arena_hold_position (§24 Milestone 2, 2026-07-31): see the .c definition's own doc comment and
 * ArenaHero's own hold_position field comment. Real WC3 "Hold Position." */
void arena_hold_position(int owner);

/* arena_set_patrol_target / arena_tick_patrol (§24 Milestone 2, 2026-07-31): see the .c
 * definitions' own doc comments and ArenaHero's own patrol_active/a/b field comment. Real WC3
 * "Patrol," last of the group-order vocabulary. */
void arena_set_patrol_target(int owner, float x, float z);
void arena_tick_patrol(unsigned int dt_ms);

/* arena_owner_controls (2026-07-30, Tyler "Divided We Stand" rework): see the .c definition's own
 * doc comment for the full design. True if `sender_owner` may issue a move/attack command for
 * `target_owner` -- itself, or one of its own active puppet clones. */
int arena_owner_controls(int sender_owner, int target_owner);

/* arena_apply_stun (S170-184, founder: "add more status effects use GFD [as a reference]" --
 * GoblinFoxDragon's server/status package, Paralyze): the kit-wiring hook for the generic
 * stunned_ms field (see its own doc comment on ArenaHero). Blocks movement, casting, and
 * auto-attacks for duration_ms. No hero kit calls this yet -- infrastructure first, same
 * precedent silenced_ms/rooted_ms/burning_ms were built under before any kit used them. Takes
 * the max of the existing remaining duration and duration_ms (a refresh never shortens what's
 * already active). No-op if owner is out of range. */
void arena_apply_stun(int owner, int duration_ms);

/* arena_apply_slow (S170-184, GFD's Slow): the kit-wiring hook for the generic slowed_ms/
 * slow_pct pair. pct is a 0.0-1.0 fraction of move speed removed (proportional, not a flat
 * unit reduction, so it scales correctly against any item move-speed bonus already applied).
 * Same "max of existing/new duration" refresh rule as arena_apply_stun; pct is always
 * overwritten by the newer application. No-op if owner is out of range. */
void arena_apply_slow(int owner, int duration_ms, float pct);

/* arena_tick_attack_targets (S170-162, NORTHSTAR §17's click-to-attack
 * system, team mode only): for every active/alive hero with a valid
 * attack_target lock (a real enemy that's active/alive/hittable -- clears
 * the lock otherwise), chases the target's LIVE position (pure pursuit, no
 * intercept prediction, matching real League exactly -- see NORTHSTAR
 * §17.1) whenever it's out of that hero's own attack range, overriding
 * whatever move target was previously set. Once in range: melee heroes
 * are untouched here -- their actual damage still comes from the existing
 * proximity-based combat loops exactly as before, chase just gets them
 * close enough for those to naturally fire. Gary specifically fires his
 * homing basic auto-attack (a real ArenaProjectile with homing_target set,
 * S170-163) directly at the lock instead, on his own cooldown -- see
 * ARENA_GARY_ATTACK_RANGE's own doc comment for why he's handled
 * separately from every other hero's melee tick. */
void arena_tick_attack_targets(unsigned int dt_ms);

/* arena_hover_ally_or_nearest (S170-143): the real WoW-macro fallback chain
 * -- "cast on unit=mouseover, or default" -- for ally-targeted abilities.
 * Returns the hovered hero if `owner` has a hover_target recorded AND it's
 * a valid, active, alive, SAME-TEAM hero other than owner itself; otherwise
 * falls back to arena_nearest_ally(owner) exactly as before this existed.
 * Same NULL-safety as arena_nearest_ally (returns NULL if neither
 * resolves). */
ArenaHero *arena_hover_ally_or_nearest(int owner);

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

/* arena_fountain_position (S170-147): fills (x,z) with the deterministic,
 * fixed position of fountain `index` (0..ARENA_FOUNTAIN_COUNT-1). Shared by
 * both the sim's own tick (below) and the client's renderer
 * (apps/arena/src/main.c), so the two never drift out of sync -- the same
 * "one source of truth" reasoning arena_obstacles_reset_layout's static
 * table already follows for jungle obstacles. Clamps out-of-range index
 * defensively rather than reading past the internal table. */
void arena_fountain_position(int index, float *x, float *z);

/* arena_powerups_reset_layout (S170-190): fills arena_state.powerups[] with the two Warsong
 * Gulch-style pickups (Berserker, Regen), positioned between the node clusters -- midway
 * between the two "top" nodes (Stables/Lumber Mill) for Berserker, midway between the two
 * "bottom" nodes (Farm/Gold Mine) for Regen, both offset from the center node so they read as
 * distinct contestable ground rather than stacking on Blacksmith. Both start active (ready to
 * grab) on layout reset, same "spawn ready, not on a delay" convention creeps/nodes already
 * follow. Called from both arena_init_with_heroes and arena_init_teams, same "this system works
 * in both modes" precedent fountains/nodes already established. */
void arena_powerups_reset_layout(void);

/* arena_tick_powerups (S170-190): counts down inactive (already-grabbed) powerups toward
 * respawn, and checks every active powerup against every real hero (not clones, same "narrower
 * blast radius on purpose" scoping fountains/node-capture/creep-targeting already exclude
 * clones from) for a pickup within ARENA_POWERUP_PICKUP_RADIUS -- grants the matching timed
 * buff (berserker_ms or regen_ms) and deactivates the powerup. Called from both arena_update
 * and arena_update_teams, same "works in both modes" precedent as arena_tick_fountains. */
void arena_tick_powerups(unsigned int dt_ms);

/* arena_obstacles_reset_layout (S170-138, made public S170-148): fills
 * arena_state.obstacles[] with the static, deterministic jungle terrain
 * layout. Public specifically so a client (apps/arena) can call it directly
 * after any full-state reset (e.g. the requeue-after-a-networked-match
 * handler's own memset(&arena_state, 0, ...)) -- obstacles are never
 * wire-synced, the same "static layout, no sync needed" precedent
 * arena_fountain_position() above also relies on, so nothing else will ever
 * repopulate this after a reset except calling it again explicitly. */
void arena_obstacles_reset_layout(void);

/* arena_graveyard_position (S170-153): fills (x,z) with the fixed,
 * permanent respawn point behind team `team`'s own spawn line -- never
 * gated by node ownership, always a valid fallback. Deterministic and
 * shared between sim and client the same way arena_fountain_position()
 * already is (no wire sync needed for a static position). */
void arena_graveyard_position(int team, float *x, float *z);

/* arena_recompute_item_stats (S170-175): re-derives max_hp/max_mp (applied
 * directly, with current hp/mp adjusted by the delta so buying/selling an
 * HP or MP item feels like a real top-up/loss, not a silent cap change)
 * and the item_bonus_armor/item_bonus_ad/item_bonus_move_speed cache
 * fields from h->equipped_item[] -- called after any loadout change
 * (arena_shop_buy/sell) and once on respawn (equipped items survive
 * death, but max_hp/max_mp get reset to the flat base by the respawn
 * clear, so this re-applies the bonuses on top immediately after). */
void arena_recompute_item_stats(ArenaHero *h);

/* arena_shop_position (S170-175): fills (x,z) with team `team`'s (0 or 1)
 * shop location -- a fixed offset from that team's own graveyard
 * (arena_graveyard_position), same corner arithmetic, so each team's shop
 * sits near their own permanent respawn point without exactly overlapping
 * it. Founder: "have there be 2 shops in the other 2 corner of the maps
 * that dont have fountains" -- the graveyard corners already ARE the two
 * corners the fountains don't occupy (S170-153/156), so the shop just
 * needs its own distinct point in that same corner region. */
void arena_shop_position(int team, float *x, float *z);

/* arena_shop_buy (S170-175): the real purchase path -- validates owner is
 * a real, active, alive hero within ARENA_SHOP_RADIUS of their OWN team's
 * shop, item_id is real, and h->flow covers item cost (net of an
 * automatic sell-back if item_id's slot is already occupied, "buying an
 * item auto equips it... no bag"). Silent no-op on any failure (same
 * "whiffed cast costs nothing" convention every ability cast in this file
 * already follows), returns 1 on a real purchase. */
int arena_shop_buy(int owner, int item_id);

/* arena_shop_sell (S170-175): sells whatever's in `slot` for
 * ARENA_ITEM_SELL_REFUND_PCT of its purchase cost, emptying the slot.
 * Same shop-proximity gate and silent-no-op-on-failure convention as
 * arena_shop_buy. Founder: "you can sell it back for less but no unequip
 * into bag for now" -- there's no bag to move it into, selling is the
 * only way to clear a slot. */
int arena_shop_sell(int owner, ArenaItemSlot slot);

/* ---------------- Build templates (2026-08-25) ----------------
 * Founder real-time, fragmented: "ok in redgarden lets experiment with the idea that tech trees
 * are just item templates" -> "choosing a build can let you auto buy at the shop" -> "or you can
 * make your own build obviously" -> "or do a custom build to buy your items" -> "or some
 * combination a build doesn't have to define all items" -> "and there can be complex ordering
 * rules" -> "all powered by parena scripting and parena mods" -> "not sure on the affordances
 * command based via the chat for now is fine."
 *
 * Reading: a build is a NAMED, ORDERED, POSSIBLY-PARTIAL list of items (it doesn't have to fill
 * all ARENA_ITEM_SLOT_COUNT slots) that the shop auto-buys from, in order, as Flow allows -- the
 * "complex ordering rules" this pass implements as the literal purchase-priority order baked
 * into each template (cheapest-affordable-first within a theme, so partial Flow still buys
 * something useful rather than stalling on one expensive first pick). "Or you can make your own
 * build obviously" reads as: item-by-item manual purchase (arena_shop_buy, already real, already
 * shipped) stays exactly as available as it always was -- a template is a shortcut on top of
 * that, not a replacement requiring a new build-EDITOR UI, which isn't attempted this pass.
 *
 * Affordance: apps/arena has no chat/command box at all (that's specific to GFD's own
 * apps2/battlegrounds_gui fork) -- the founder's own "not sure... chat... is fine for now" left
 * this genuinely open, so this instead extends the shop's EXISTING click-based page UI with one
 * more page listing build presets, matching the affordance the shop already trains players on
 * rather than inventing a second, unrelated input surface. Chat-based selection can still be
 * added later if GFD's own chat pattern gets ported upstream into apps/arena; not a dead end. */
#define ARENA_BUILD_TEMPLATE_MAX_ITEMS 6 /* headroom above every preset below (4 items each); a template need not use them all -- item_count says how many are real */

typedef struct {
    const char *name;
    const char *desc;
    int item_ids[ARENA_BUILD_TEMPLATE_MAX_ITEMS]; /* PURCHASE ORDER -- the "complex ordering rules" -- cheapest-first within the theme */
    int item_count;
} ArenaBuildTemplate;

extern const ArenaBuildTemplate ARENA_BUILD_TEMPLATES[];
#define ARENA_BUILD_TEMPLATE_COUNT 3 /* Bruiser, Assassin, Caster -- a first, generic (any-hero) pass; per-hero curated builds are real, separate future scope, not attempted here */

/* arena_hero_apply_build_template: buys as many of template_id's items as `owner` can currently
 * afford, IN ORDER, skipping any item already equipped in its own slot (idempotent re-click --
 * clicking the same build twice never re-buys what you already have) and STOPPING (not failing)
 * at the first item that's unaffordable right now -- so a partial Flow balance still buys real
 * progress toward the build instead of an all-or-nothing purchase, matching "a build doesn't
 * have to define all items" applying just as much to what a *player* can afford as to what the
 * template author chose to list. Same shop-proximity/alive/active gating as arena_shop_buy
 * (delegated to it directly -- every individual purchase in the sequence IS a real
 * arena_shop_buy call, not a parallel reimplementation). Each successful purchase routes through
 * the PARENA-compiled on_apply_build_template_item (the trigger, per "all powered by parena
 * scripting and parena mods"), which calls back into redgarden_host_buy_build_item -- same
 * "mod is the trigger, host C does the mutation" split bloodflower_mod.prn/tree_passive_mod.prn
 * both already established. Returns the number of items actually purchased this call (0 if the
 * template is fully owned already, out of range, or the hero can't afford/reach the shop at
 * all). */
int arena_hero_apply_build_template(int owner, int template_id);

/* redgarden_host_buy_build_item: the real host-side implementation the PARENA-compiled
 * on_apply_build_template_item calls back into (see build_template_mod_host.h). Thin wrapper
 * around arena_shop_buy -- exists as its own function (rather than calling arena_shop_buy
 * directly from the mod) only because the mod boundary needs a stable, minimal C ABI to cross,
 * same reasoning redgarden_host_spawn_bloodflower/redgarden_host_tree_passive_strike both already
 * establish. Returns 1 on a real purchase, 0 otherwise -- arena_hero_apply_build_template uses
 * this to know whether to keep advancing through the template or stop. */
int redgarden_host_buy_build_item(int hero_index, int item_id);

/* arena_use_blink (S170-205): activates Blink Dagger -- no-op (no cooldown spent) unless the
 * hero has ARENA_BLINK_DAGGER_ITEM_ID actually equipped, is alive, not stunned, and
 * blink_cooldown_ms has elapsed. Direction: toward the current move target if moving, else
 * toward the nearest living enemy, else no-op (same fallback chain unicorn_cast_q already
 * uses) -- clamped to ARENA_BLINK_RANGE and the map bounds. */
void arena_use_blink(int owner);

/* arena_use_donkey_glide (S170-206): activates Donkey's Paper Glide -- no-op unless
 * ARENA_DONKEY_ITEM_ID is equipped, alive, not stunned, and donkey_glide_cooldown_ms has
 * elapsed. Direction: away from the nearest living enemy (a real escape, matching the item's
 * own lore), else toward the current move target if no enemy is nearby, else no-op. Sets a real
 * move target ARENA_DONKEY_GLIDE_RANGE away at ARENA_DONKEY_GLIDE_SPEED_MULT speed rather than
 * an instant teleport -- update_hero_motion reads donkey_airborne_ms for both the speed boost
 * and skipping obstacle collision for the glide's duration. Also sets intangible_ms for the
 * same window (untargetable while airborne). */
void arena_use_donkey_glide(int owner);

/* arena_use_active_item (S170-206): PACKET_ARENA_BLINK's own server-side handler, generalized
 * past its original single-item name once Donkey shipped as a second tilde-bound active --
 * dispatches to whichever active item the hero actually has equipped (Blink Dagger checked
 * first, then Donkey; a hero holding both, an edge case no real build would deliberately create
 * since they're both expensive single-purpose mobility items, just gets Blink Dagger). No-op if
 * neither is equipped. */
void arena_use_active_item(int owner);

/* arena_tick_resources (S170-153): advances each team's Arathi-Basin-style
 * resource race by dt_ms. Fixed ARENA_RESOURCE_TICK_MS interval, same
 * accumulator idiom as every other periodic tick in this file. Gain per
 * tick scales with how many of the ARENA_NODE_COUNT nodes that team
 * currently owns (0 nodes = 0 gain, more nodes = a real, more-than-linear
 * acceleration toward the cap) -- the actual "objectives are how the game
 * is won" identity this replaces team-wipe with. Does not itself set
 * arena_state.winner -- that's checked once per tick by the caller
 * (arena_update_teams), same "tick computes, caller decides" split as
 * every other subsystem in this file. Team mode only. */
void arena_tick_resources(unsigned int dt_ms);

/* arena_tick_fountains (S170-147): heals every active, alive, hittable hero
 * within ARENA_FOUNTAIN_RADIUS of either fountain by ARENA_FOUNTAIN_HEAL_PER_SEC
 * per second (fixed-interval tick, same 1000ms-accumulator idiom as every
 * other DPS/heal zone in this file), capped at max_hp. Neutral -- heals any
 * team, see the header comment above ARENA_FOUNTAIN_COUNT for why. Called
 * from both arena_update() and arena_update_teams(). */
void arena_tick_fountains(unsigned int dt_ms);

/* arena_tick_creeps (S170-51): advances every node-guardian creep by dt_ms --
 * respawns a dead creep once its timer elapses (re-rolling flavor/HP from
 * its node's CURRENT owner, not whatever it was when it last spawned),
 * ticks its attack cooldown, and has it auto-attack the nearest hittable
 * hero within ARENA_CREEP_AGGRO_RADIUS if one's there (passive-until-
 * approached). Does not itself apply any hero-side damage to the creep or
 * grant kill rewards -- that's arena_hero_attack_creeps, called
 * separately so both halves of creep combat can be reasoned about
 * independently. Called from both arena_update() and arena_update_teams(). */
void arena_tick_creeps(unsigned int dt_ms);

/* arena_spawn_projectile (S170-136, returns a pointer S170-140): fills the
 * first free slot in arena_state.projectiles with a straight-line shot from
 * (x,z) toward (target_x,target_z) at `speed` units/sec, and returns a
 * pointer to it so the caller can optionally set on_hit_silence_ms/
 * on_hit_root_ms/on_hit_burn_ms/on_hit_burn_dps right after (all default to
 * 0 -- "no extra effect" -- so a plain-damage caster like Gary's Q can
 * ignore the return value entirely). Returns NULL if the pool is full
 * (ARENA_MAX_PROJECTILES headroom is generous relative to current cast-rate,
 * so this should never actually happen in practice) -- callers that use the
 * return value must check it first, same NULL-safety convention as
 * arena_nearest_enemy/arena_nearest_ally. */
ArenaProjectile *arena_spawn_projectile(int owner, int team, ArenaHeroID hero_id,
                             float x, float z, float target_x, float target_z,
                             float speed, float radius, int damage, float max_range);

/* arena_tick_projectiles (S170-136, on-hit status effects S170-140):
 * advances every active projectile by dt_ms along its fixed velocity, checks
 * collision against every hittable enemy hero within `radius`, applies
 * damage + armor plus any nonzero on_hit_* status effect on the first hit,
 * and deactivates -- and deactivates unhit projectiles once `traveled`
 * reaches `max_range` (a whiff). Called from both arena_update() and
 * arena_update_teams(), same convention as arena_tick_creeps. */
void arena_tick_projectiles(unsigned int dt_ms);

/* arena_hero_attack_creeps (S170-51): each active, alive hero without a
 * closer enemy HERO in range instead auto-attacks a living creep within
 * ARENA_ATTACK_RANGE if one's there -- creeps are a secondary objective,
 * so a hero already trading blows with an enemy hero doesn't get split
 * attention. On a kill, applies the flavor-specific reward (see the
 * ARENA_MAX_CREEPS header comment) to the killer's team. Called from both
 * arena_update() and arena_update_teams(), after resolve_combat/the melee
 * loop so hero-vs-hero combat is always resolved first each tick. */
void arena_hero_attack_creeps(unsigned int dt_ms);

/* arena_towers_reset (2026-07-30): (re)spawns every node's tower at full HP, positioned exactly
 * at its node's own (x,z) -- must run AFTER arena_nodes_reset_layout so node positions already
 * exist to read. Unlike arena_creeps_reset, sets `alive`/`hp` directly rather than relying on a
 * lazy tick-driven first-spawn -- towers exist from tick 0, there's no "grace period before the
 * jungle populates" the way creeps' own respawn-timer-starts-at-0 convention implies.
 *
 * Deliberately NOT called from arena_init_teams() itself, unlike arena_creeps_reset -- that
 * shared sim-level function is also called directly by ~300 existing unit tests that place heroes
 * at convenient coordinates (some literally at the Blacksmith node's own (0,0)) never expecting
 * anything to auto-attack them there. Towers default to dead (memset-zero alive=0) everywhere
 * except the one real call site that matters: apps/arena_server/src/main.c calls this explicitly
 * immediately after its own arena_init_teams() call, for real team-mode matches only. Not called
 * for 1v1 (arena_init_with_heroes/arena_init) at all -- towers are team-mode only, same scope as
 * lane creep waves. */
void arena_towers_reset(void);

/* arena_tick_towers (2026-07-30): ticks every living tower's attack cooldown and has it
 * auto-attack the nearest hittable hero of EITHER team within ARENA_TOWER_AGGRO_RADIUS (unlike
 * node-guardian creeps, a tower has no owning team to spare from its own aggro -- it's neutral
 * infrastructure standing in the way of BOTH sides' capture attempts equally). Does not itself
 * apply hero-side damage to the tower -- that's arena_hero_attack_towers, same "two independent
 * halves of combat" split arena_tick_creeps/arena_hero_attack_creeps already established. Called
 * from arena_update_teams() only -- see arena_towers_reset's own doc comment for why towers are
 * team-mode only; a no-op everywhere else since every tower is left dead (alive=0) by default. */
void arena_tick_towers(unsigned int dt_ms);

/* arena_hero_attack_towers (2026-07-30): same shape as arena_hero_attack_creeps -- each active,
 * alive hero without a closer enemy hero or creep already occupying its attack this tick
 * (checked via the same shared attack_cooldown_ms gate both functions read/write) instead
 * auto-attacks a living tower within ARENA_ATTACK_RANGE if one's there. On a kill, grants
 * ARENA_TOWER_KILL_FLOW/XP to whoever landed it (last_attacked_by_owner, same last-hit-credit
 * convention as every other killable entity in this file) and permanently sets the tower dead --
 * no respawn, see the ARENA_TOWER_MAX_HP header comment for why. Called from arena_update_teams()
 * only, same team-mode-only scope as arena_tick_towers, after arena_hero_attack_creeps so a hero
 * already mid-swing on a creep this tick doesn't also get a free tower hit. */
void arena_hero_attack_towers(unsigned int dt_ms);

/* arena_tick_lane_creeps (S170-139): see the header comment above
 * ARENA_LANE_WAYPOINT_COUNT for the full design. Advances each team's wave
 * spawn timer (spawning a fresh ARENA_LANE_CREEPS_PER_WAVE-strong wave at
 * that team's spawn line when it elapses), then advances every active lane
 * creep: if a hittable enemy hero or opposing-team lane creep is within
 * ARENA_LANE_CREEP_AGGRO_RADIUS, stops to fight it instead of advancing;
 * otherwise marches toward its current waypoint, advancing to the next one
 * on arrival, or despawning (no reward, no structure to hit) once it reaches
 * the final waypoint at the enemy's spawn line. Called from both
 * arena_update() and arena_update_teams(), same convention as
 * arena_tick_creeps. */
void arena_tick_lane_creeps(unsigned int dt_ms);

/* arena_hero_attack_lane_creeps (S170-139): mirrors arena_hero_attack_creeps
 * exactly -- each active, alive hero without a closer enemy HERO in range
 * instead auto-attacks the nearest OPPOSING-team lane creep within
 * ARENA_ATTACK_RANGE if one's there. Shares the same attack_cooldown_ms gate
 * as arena_hero_attack_creeps (called immediately after it in both update
 * loops), so a hero that already spent this tick's attack on a node-guardian creep
 * does not also get a free hit on a lane creep the same tick. No kill
 * reward (see the ARENA_LANE_WAYPOINT_COUNT header comment on why). Called
 * from both arena_update() and arena_update_teams().
 *
 * S170-215: a hero's OWN team's lane creeps are also a valid target once
 * that creep drops below 50% HP -- deny, the real League mechanic where an
 * ally can kill their own dying minion to keep the enemy from getting the
 * reward. Same kill-reward path either way (no separate reduced-reward
 * tuning here, matching this file's "spec the model, not the numbers"
 * discipline elsewhere). */
void arena_hero_attack_lane_creeps(unsigned int dt_ms);

/* arena_camp_position (Jungle Camps Milestone 1): fills (x,z) with the deterministic position of
 * jungle camp `index` (0=N, 1=S, 2=E, 3=W by convention, GoblinFoxDragon/docs2/
 * JUNGLE_CAMPS_NORTHSTAR.md §3.1) -- the 4 edge midpoints between the 2 fountain corners and the
 * 2 shop corners, same "-margin so it's never buried in terrain" idiom as arena_fountain_position.
 * `index` is clamped into [0, ARENA_CAMP_COUNT) same as arena_fountain_position's own convention. */
void arena_camp_position(int index, float *x, float *z);

/* arena_tick_camp_minions (Jungle Camps Milestone 1): advances each camp's own wave timer
 * (spawning a fresh ARENA_CAMP_MINIONS_PER_WAVE-strong wave at that camp's position when it
 * elapses, from the opening bell -- no initial delay unlike lane creeps, see
 * ARENA_CAMP_WAVE_INTERVAL_MS's own doc comment), then advances every active camp minion:
 * neutral-hostile (aggros either team, same ARENA_CREEP_NEUTRAL flavor as node guardians),
 * stationary -- attacks the nearest hittable hero within ARENA_CAMP_MINION_AGGRO_RADIUS if one's
 * there, otherwise idle (no waypoint march -- that's §3.4's anti-stall escalation, real, separate,
 * NOT built here). Called from arena_update_teams() only, same team-mode-only scope as
 * arena_tick_lane_creeps. */
void arena_tick_camp_minions(unsigned int dt_ms);

/* arena_hero_attack_camp_minions: mirrors arena_hero_attack_lane_creeps -- each active, alive
 * hero without a closer enemy hero already occupying its attack this tick instead auto-attacks
 * the nearest camp minion within ARENA_ATTACK_RANGE if one's there. Grants
 * ARENA_CAMP_MINION_KILL_FLOW/XP to whoever lands the kill. Called from arena_update_teams()
 * only, after arena_hero_attack_lane_creeps so a hero already mid-swing this tick doesn't also
 * get a free camp-minion hit. */
void arena_hero_attack_camp_minions(unsigned int dt_ms);

/* arena_tick_kings (Jungle Camps Milestones 2+4): arms each camp's King spawn timer at
 * ARENA_KING_SPAWN_DELAY_MS (1:00, unlike camp minions' opening-bell start), spawns it silently
 * once the delay elapses, then advances every active King: same neutral-aggro/stationary-attack
 * shape as arena_tick_camp_minions, boss-scale numbers. Milestone 4 (King respawn, §5): a King
 * killed by heroes respawns on ARENA_KING_RESPAWN_MS, resetting the same king_spawn_timer_ms
 * countdown this function already uses for the first spawn (see king_spawn_timer_ms's own doc
 * comment for how one field serves both without ambiguity). Also decrements every hero's
 * king_growth_ms/king_wealth_ms and the team-wide king_allseeing_team_ms[2] -- the timer side of
 * the 3 timer-based King buffs (Music's king_music_carrier is not a timer, see its own field doc
 * comment). Called from arena_update_teams() only. */
void arena_tick_kings(unsigned int dt_ms);

/* arena_tick_daynight (2026-08-25): advances time_of_day_sec, computes the current moon height
 * (ported from SHANKPIT retro_sky.c's sun/moon orbit math -- moon_dir_y = -sun_dir_y), and
 * detects the instant the moon passes its zenith (local maximum of moon height) via consecutive-
 * sample comparison (see ArenaState.prev_moon_height/moon_was_rising's own doc comments) rather
 * than an exact analytical crossing -- simpler, and exact enough for a gameplay trigger. On a
 * real zenith crossing, calls into the PARENA-compiled on_moon_zenith (stdlib/redgarden/
 * bloodflower_mod.prn) at map center, which calls back into redgarden_host_spawn_bloodflower
 * below -- a real round-trip through compiled PARENA code, not a direct call to the spawn logic.
 * Also advances bloodflower_ms_remaining and despawns/claims it. Called from arena_update_teams()
 * only, same as arena_tick_kings -- see that function's own doc comment. */
void arena_tick_daynight(unsigned int dt_ms);

/* arena_hero_claim_bloodflower: each active, alive hero within ARENA_BLOODFLOWER_CLAIM_RADIUS of
 * an active Bloodflower claims it (first hero checked each tick wins -- heroes array order is
 * the same implicit priority arena_hero_attack_creeps' own nearest-target scan already relies
 * on elsewhere in this file), granting ARENA_BLOODFLOWER_CLAIM_FLOW and despawning it
 * immediately. Called from arena_update_teams() right after arena_tick_daynight. */
void arena_hero_claim_bloodflower(void);

/* arena_daynight_ambient_rgb: pure query, no side effects -- recomputes sun_height from
 * arena_state.time_of_day_sec fresh each call (same formula arena_tick_daynight already uses)
 * and maps it to an ambient tint, same smoothstep-driven day/night blend shape SHANKPIT
 * retro_lighting.c's own ambient_r/g/b formula uses (ported constants, adapted: this game's
 * top-down camera has no sky dome to light, so this feeds a background/ambient clear-color tint
 * directly rather than per-surface Lambertian shading). Callers: apps/arena/src/main.c's
 * in-match render loop (not the pre-match queuing/draft screens -- day/night doesn't apply
 * before a match's own clock is running). */
void arena_daynight_ambient_rgb(float *out_r, float *out_g, float *out_b);

/* redgarden_host_spawn_bloodflower: the real host-side implementation the PARENA-compiled
 * on_moon_zenith calls back into (see bloodflower_mod_host.h). Sets bloodflower_active/x/z/
 * ms_remaining on arena_state -- the actual, real world-state mutation; on_moon_zenith itself
 * has no logic beyond this one call, matching vterm_mod.prn's own "one function, no dispatch
 * table" minimalism. */
void redgarden_host_spawn_bloodflower(int x, int z);

/* ---------------- Tree passive (2026-08-25) ----------------
 * Founder real-time: "can you add a passive to tree that when he is close enough to a tree to
 * auto attack it he auto attacks it and slowly regenerates health?" -> "the tree he attacks
 * never does or anything have it jiggle animate extra squishy" -> "as a parena first mod led dev
 * cycle." "tree" = ARENA_HERO_TREE; "a tree" = the existing decorative ARENA_OBSTACLE_TREE
 * jungle-obstacle pieces (arena_obstacles_reset_layout) -- those had zero interaction before this,
 * pure static collision geometry. Same "PARENA mod is the trigger, host C does the real work"
 * split as bloodflower_mod.prn/on_moon_zenith (S194-01), see tree_passive_mod.prn's own header
 * comment and tree_passive_mod_host.h.
 *
 * Trees are a permanent, regenerating resource, not a kill target -- they never fully deplete or
 * despawn (ARENA_TREE_REGEN_PER_SEC always ticks them back toward max_hp), matching "slowly
 * regenerates health" being about a repeatable sustain tool, not a one-time farm-and-destroy
 * mechanic. Numbers picked using this file's own existing tiering (camp minion HP=45, King
 * HP=huge) rather than asked for -- same "founder specifies the trigger, reasonable numbers fill
 * the rest" precedent Bloodflower's own ARENA_BLOODFLOWER_CLAIM_FLOW documented. */
#define ARENA_TREE_HP                   120  /* above a camp minion (45) -- meant to be leaned on repeatedly, not felled */
#define ARENA_TREE_REGEN_PER_SEC          6  /* keeps a tree from staying empty forever if left alone between visits */
#define ARENA_TREE_PASSIVE_DAMAGE        10  /* light -- a sustain tool, not a kill-target's worth of damage */
#define ARENA_TREE_PASSIVE_HEAL_PER_HIT   4  /* Tree hero's own self-heal per successful strike */
#define ARENA_TREE_PASSIVE_COOLDOWN_MS 1200  /* slower than ARENA_ATTACK_COOLDOWN_MS's hero-vs-hero pace -- passive sustain, not meant to out-tempo real combat */

/* arena_hero_tree_passive: gated to ARENA_HERO_TREE only (this is a kit-specific passive, not a
 * general mechanic every hero gets). Mirrors arena_hero_attack_camp_minions' own precedence
 * check (skips a hero already busy with an enemy hero in range) and shares the hero's normal
 * attack_cooldown_ms rather than a separate timer, so the Tree hero can't also attack a camp
 * minion/enemy hero the same tick this fires -- one basic-attack-shaped action per cooldown,
 * same as every other auto-attack type in this file. On finding the nearest ARENA_OBSTACLE_TREE
 * within ARENA_ATTACK_RANGE, calls the PARENA-compiled on_tree_passive_strike (not
 * redgarden_host_tree_passive_strike directly) -- the mod call IS the trigger, per the founder's
 * explicit "as a parena first mod led dev cycle." */
void arena_hero_tree_passive(unsigned int dt_ms);

/* arena_tick_obstacles: regenerates tree obstacles' hp toward max_hp at ARENA_TREE_REGEN_PER_SEC.
 * Rocks (hp/max_hp always 0) are a no-op pass-through, cheap enough not to bother skipping. */
void arena_tick_obstacles(unsigned int dt_ms);

/* redgarden_host_tree_passive_strike: the real host-side implementation the PARENA-compiled
 * on_tree_passive_strike calls back into (see tree_passive_mod_host.h). Applies
 * ARENA_TREE_PASSIVE_DAMAGE to the obstacle (clamped at 0, never destroyed/despawned -- see this
 * section's own doc comment on why trees are permanent), heals the hero
 * ARENA_TREE_PASSIVE_HEAL_PER_HIT (clamped to max_hp), and arms the tree's hit-reaction by
 * resetting its hp to a value the client can observe decrease -- the actual squish/jiggle
 * animation itself is purely client-side (apps/arena/src/main.c's own squish_age_ms idiom,
 * array-indexed by obstacle instead of hero, triggered on any wire-synced hp decrease), same
 * "server is authoritative for state, client owns purely cosmetic reaction" split fountains'
 * heal-flash already uses. */
void redgarden_host_tree_passive_strike(int hero_index, int obstacle_index);

/* ---------------- Duck W: Smoke Bomb (S202-10, 2026-08-25) ----------------
 * Same "PARENA mod is the trigger, host C does the real work" split as
 * Bloodflower/Tree-passive/build-templates/item-curriculum above -- see
 * ARENA_DUCK_W_RADIUS's own doc comment (arena_game.h) for the full
 * founder-quote chain and design reasoning, and hero_obscured_from
 * (arena_game.c, static) for the actual targeting-denial mechanic. */

/* redgarden_host_duck_smoke_bomb_cast: the real host-side implementation the
 * PARENA-compiled on_duck_smoke_bomb_cast calls back into (see
 * duck_smoke_bomb_mod_host.h). Sets duck_smoke_ms/x/z on the casting hero --
 * the actual world-state mutation; on_duck_smoke_bomb_cast itself has no
 * logic beyond this one call, matching vterm_mod.prn's own "one function, no
 * dispatch table" minimalism every other real mod in this repo already uses. */
void redgarden_host_duck_smoke_bomb_cast(int hero_index);

/* arena_hero_attack_kings: mirrors arena_hero_attack_camp_minions -- each active, alive hero
 * without a closer enemy hero or camp minion already occupying its attack this tick instead
 * auto-attacks the nearest King within ARENA_ATTACK_RANGE if one's there. On a kill, grants
 * ARENA_KING_KILL_FLOW/XP plus that King's own distinct buff (§3.3, keyed off which camp_index
 * this King belongs to) to the killing team. Called from arena_update_teams() only, after
 * arena_hero_attack_camp_minions so a hero already mid-swing this tick doesn't also get a free
 * King hit. */
void arena_hero_attack_kings(unsigned int dt_ms);

/* arena_tick_synergy (§25.3, live-match comeback mechanic, not a training technique -- see
 * ARENA_SYNERGY_TIER_COUNT's own doc comment for the full design): every ARENA_SYNERGY_ROLL_
 * INTERVAL_MS, re-rolls each team's synergy_tier stochastically, weighted toward higher (more
 * decayed) tiers the further that team is currently ahead in the real resource race. The
 * resulting tier scales a small team-wide attack-speed/move-speed bonus (ARENA_SYNERGY_TIER0_
 * CDR_PCT/MOVE_SPEED_PCT at tier 0, linearly toward 0 at the fully-decayed tier), read by
 * apply_cdr/update_hero_motion exactly like East/Music's own Catchy Song buff. Called from
 * arena_update_teams() only, team-mode-only scope (the resource race itself is team-mode-only,
 * S170-153). */
void arena_tick_synergy(unsigned int dt_ms);

/* Kit casts dispatch on the hero's hero_id, not a hardcoded owner check
 * (S170-31 generalized this from S170-18's Unicorn-only version). No-ops
 * if the hero's kit doesn't have that ability, or if it's on cooldown. */
void arena_cast_q(int owner);
void arena_toggle_w(int owner);
void arena_cast_r(int owner);
float arena_hero_armor(const ArenaHero *h); /* effective armor, incl. Unicorn R's buff */
/* arena_hero_r_zone_radius (S170-200, founder: "zone abilities dont read at all we need true
 * aoe cast circle... show cast radius... circle on the ground... showing to all participants
 * that the spell was cast there so it reads"): a single lookup from hero_id to that hero's real
 * R-ability AoE radius (the exact same ARENA_*_R_RADIUS constant tick_hero_kit's own zone-tick
 * damage/heal check already applies), 0.0f for every hero whose R isn't a real ground-radius
 * ability at all. One source of truth reused by both arena_cast_r's mechanical radius check
 * (unchanged, already correct) and the client's new cast-radius rendering (previously nothing
 * rendered a real radius at all -- see apps/arena/src/main.c's own S170-200 doc comment). */
float arena_hero_r_zone_radius(ArenaHeroID hero_id);

#endif
