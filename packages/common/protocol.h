#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define MAX_CLIENTS 16
#define MAX_ENTITIES 2048
#define GRID_DIM 20
#define HAND_SIZE 5

#define PACKET_CONNECT 0
#define PACKET_CARD_PLAY 1
#define PACKET_SNAPSHOT 2
#define PACKET_WELCOME 3
#define PACKET_FIND_MATCH 4   /* client -> matchmaker: queue for a match */
#define PACKET_MATCH_FOUND 5  /* matchmaker -> client: game-server port assigned */
#define PACKET_ARENA_MOVE 6     /* client -> arena_server: new move target (x,z) */
#define PACKET_ARENA_CAST 7     /* client -> arena_server: cast q/w/r */
#define PACKET_ARENA_SNAPSHOT 8 /* arena_server -> client: both heroes' state */
#define PACKET_ARENA_PICK 9     /* client -> arena_server: hero pick during draft */
#define PACKET_ARENA_ATTACK 10  /* client -> arena_server: lock onto and auto-attack a specific hero slot, S170-162 */
#define PACKET_ARENA_SHOP_BUY 11  /* client -> arena_server: buy an item by catalog index, S170-175 */
#define PACKET_ARENA_SHOP_SELL 12 /* client -> arena_server: sell an equipped item by slot, S170-175 */
#define PACKET_ARENA_BLINK 13     /* client -> arena_server: use Blink Dagger, S170-205 -- no payload, direction is derived server-side same as Unicorn's Q dash (toward move target, else nearest foe) */
#define PACKET_ARENA_SNAPSHOT_HEROES 14 /* arena_server -> client: one ARENA_SNAPSHOT_HERO_CHUNK_SIZE-hero slice, S170-193 -- see ArenaSnapshotHeroesMsg's own doc comment */
#define PACKET_ARENA_STOP 15 /* client -> arena_server: cancel one of the sending client's own commandable units' current move/attack order in place, NORTHSTAR.md §24 Milestone 2 (2026-07-31) -- see ArenaStopCmd's own doc comment */
#define PACKET_ARENA_ATTACK_MOVE 16 /* client -> arena_server: real LoL/WC3 "A + click", NORTHSTAR.md §17.4 + §24 Milestone 2 (2026-07-31) -- see ArenaAttackMoveCmd's own doc comment */
#define PACKET_ARENA_HOLD 17 /* client -> arena_server: real WC3 "Hold Position", NORTHSTAR.md §24 Milestone 2 (2026-07-31) -- see ArenaHoldCmd's own doc comment */
#define PACKET_ARENA_PATROL 18 /* client -> arena_server: real WC3 "Patrol", NORTHSTAR.md §24 Milestone 2 (2026-07-31) -- see ArenaPatrolCmd's own doc comment */
#define PACKET_ARENA_APPLY_BUILD_TEMPLATE 19 /* client -> arena_server: auto-buy a named build template's items in order, 2026-08-25 -- see ArenaApplyBuildTemplateCmd's own doc comment */

#define ARENA_PHASE_WAITING 0 /* fewer than 2 real players connected yet */
#define ARENA_PHASE_DRAFT   1 /* both connected, waiting on hero picks */
#define ARENA_PHASE_LIVE    2 /* both picked, match clock running */

typedef enum {
    CELL_NEUTRAL = 0,
    CELL_PLAYER = 1,
    CELL_ENEMY = 2,
    CELL_CORRUPTED = 3
} CellState;

typedef enum {
    CARD_MILITIA = 0,
    CARD_SCOUT = 1,
    CARD_SWARMLINGS = 2,
    CARD_OUTPOST = 3,
    CARD_COUNT = 4
} CardId;

typedef enum {
    ENTITY_NONE = 0,
    ENTITY_MILITIA = 1,
    ENTITY_SCOUT = 2,
    ENTITY_SWARMLING = 3,
    ENTITY_OUTPOST = 4,
    ENTITY_VILLAGE = 5
} EntityType;

typedef struct {
    uint8_t type;
    uint8_t client_id;
    uint16_t sequence;
    uint32_t timestamp;
    uint16_t entity_count;
} NetHeader;

typedef struct {
    uint16_t sequence;
    uint32_t timestamp;
    uint8_t card_id;
    int16_t grid_x;
    int16_t grid_z;
} CardPlayCmd;

typedef struct {
    uint16_t id;
    uint8_t type;
    uint8_t owner;
    float x;
    float z;
    uint16_t hp;
    uint8_t state;
} NetEntity;

// Sent by the matchmaker after PACKET_MATCH_FOUND's NetHeader: the UDP port
// of the freshly-spawned red_garden_server instance the client should now
// connect to (see apps/matchmaker/src/main.c). seed/mode added for
// GoblinFoxDragon/docs2/DUNGEON_NORTHSTAR.md's Milestone 1 ("a way to pass a
// seed... into the spawned process" / "PACKET_MATCH_FOUND telling the client
// which mode it's joining") -- real, minimal first slice: seed is generated
// fresh per match and passed both to the spawned server (--seed) and the
// client here, so a future dungeon server's generator and the client that
// joins it can agree on the same instance without a separate round-trip.
// mode is 0 for every existing arena/card-RTS match (unchanged real
// behavior) -- a real dungeon server variant is not built yet, so nothing
// non-zero is ever sent today.
typedef struct {
    uint16_t port;
    uint32_t seed;
    uint8_t mode;
} MatchFoundMsg;

// ---- apps/arena_server wire structs (2026-07-24 pivot: the MOBA is the
// product) ----

// PACKET_ARENA_MOVE payload: a new move target for one of the sending client's own commandable
// units. unit_owner (2026-07-30, Tyler "Divided We Stand" rework -- founder: "clones multi
// control drag click all of it") is which owner slot this specific command is FOR -- almost
// always the sender's own hero slot (the pre-existing, only-ever-possible behavior before this),
// but a player currently playing Tyler with active clones can also target one of THOSE slots
// directly, now that clones are independently commandable instead of auto-mirroring Tyler's own
// move-target. The server still infers WHO sent this (same client_id trust model as
// PACKET_CARD_PLAY) -- unit_owner only says which of that sender's OWN units the command is for,
// and is rejected (see arena_owner_controls) if it names anything the sender doesn't actually
// control, same trust boundary this packet already had, just widened from exactly one controllable
// slot to a small owned set.
typedef struct {
    float target_x;
    float target_z;
    uint8_t unit_owner;
} ArenaMoveCmd;

// PACKET_ARENA_CAST payload: which ability slot (0=Q, 1=W, 2=R) to cast.
// hover_target (S170-143, "hover casting like in wow macros"): which hero slot
// the caster's mouse was over at the moment of casting, -1 (encoded as -1,
// int8_t so it round-trips over the wire unlike uint8_t) if nothing was
// hovered. Consulted by hover-aware abilities (Doc Wheel's Q so far) via
// arena_hover_ally_or_nearest() as a preferred target, falling back to the
// existing nearest-ally targeting when nothing's hovered -- the "macro"
// itself is client-side (only WHICH target rides the packet), matching the
// real WoW mouseover-macro pattern of "cast on unit=mouseover, or default."
//
// has_ground_target/target_x/target_z (Abraham's Fireball, S202-34):
// generic support for a ground-targeted (skillshot) ability, not
// Abraham-specific -- has_ground_target is 0 for every existing
// unit-targeted/self-targeted cast (hover_target alone still covers those,
// unchanged), 1 when the client is in ground-targeting mode (green
// reticle, founder: "the targeter is green when you are ready to cast")
// and the player has clicked a world point. The server is the sole judge
// of whether a given hero/slot combination is actually a ground-targeted
// ability -- a client setting this for a non-ground-targeted slot is
// simply ignored server-side, same trust boundary every other command
// payload already holds itself to.
typedef struct {
    uint8_t slot;
    int8_t hover_target;
    uint8_t has_ground_target;
    float target_x;
    float target_z;
} ArenaCastCmd;

// PACKET_ARENA_PICK payload: which hero (ArenaHeroID) the sending client
// wants to play, sent during ARENA_PHASE_DRAFT.
typedef struct {
    uint8_t hero_id;
} ArenaPickCmd;

// PACKET_ARENA_ATTACK payload (S170-162, NORTHSTAR §17's click-to-attack
// system): which hero slot the sending client's own hero should lock onto
// and auto-attack. Distinct from PACKET_ARENA_MOVE -- §17.1's "right-click
// ground vs right-click a unit" split. The lock persists server-side
// (arena_set_attack_target) until the target dies/becomes unhittable or a
// new PACKET_ARENA_MOVE/PACKET_ARENA_ATTACK arrives; the server chases
// automatically while the target is out of range (pure pursuit, see
// arena_tick_attack_targets), same "yes, automatically, indefinitely" chase
// behavior §17.1 documents for real League.
typedef struct {
    uint8_t target_owner;
    // commander_unit (2026-07-30, same Tyler clone-control rework as ArenaMoveCmd's own
    // unit_owner): which of the sender's OWN units should execute this attack-command --
    // target_owner above is the ENEMY being locked onto, this is which of the sender's
    // controllable slots (self, or one of Tyler's own active clones) does the locking. Same
    // arena_owner_controls authorization as the move command.
    uint8_t commander_unit;
} ArenaAttackCmd;

// PACKET_ARENA_STOP payload (NORTHSTAR.md §24 Milestone 2, 2026-07-31, founder: "the unit
// controls are supposed to be for tyler" -- real WC3-shaped group-order vocabulary for Tyler's
// own drag-selected clone group, the first of attack-move/hold/patrol/stop to land). Cancels
// unit_owner's current move target and attack-target lock in place -- same
// arena_owner_controls authorization as ArenaMoveCmd/ArenaAttackCmd's own unit_owner/
// commander_unit fields, so a client can only stop its own hero or its own active clones.
typedef struct {
    uint8_t unit_owner;
} ArenaStopCmd;

// PACKET_ARENA_ATTACK_MOVE payload (NORTHSTAR.md §17.4 + §24 Milestone 2, 2026-07-31): real
// LoL/WC3 "A + click" -- moves toward (target_x, target_z) like ArenaMoveCmd, but
// arena_tick_attack_move opportunistically diverts unit_owner to attack whatever enemy comes
// within range along the way, unlike a plain move (never initiates combat, §17.1) and unlike a
// direct ArenaAttackCmd lock (which doesn't re-acquire a new target if the locked one dies).
// Same shape and arena_owner_controls authorization as ArenaMoveCmd's own unit_owner field.
typedef struct {
    float target_x;
    float target_z;
    uint8_t unit_owner;
} ArenaAttackMoveCmd;

// PACKET_ARENA_HOLD payload (NORTHSTAR.md §24 Milestone 2, 2026-07-31): real WC3 "Hold
// Position" -- unit_owner halts in place and defends (auto-engages whoever comes within its own
// attack range) without ever chasing a target that leaves range. Same shape and
// arena_owner_controls authorization as ArenaStopCmd.
typedef struct {
    uint8_t unit_owner;
} ArenaHoldCmd;

// PACKET_ARENA_PATROL payload (NORTHSTAR.md §24 Milestone 2, 2026-07-31): real WC3 "Patrol" --
// unit_owner walks back and forth between its own position at the moment of issue and
// (target_x, target_z) forever, opportunistically engaging anything encountered along the way,
// until a new command arrives. Same shape and arena_owner_controls authorization as
// ArenaAttackMoveCmd.
typedef struct {
    float target_x;
    float target_z;
    uint8_t unit_owner;
} ArenaPatrolCmd;

// PACKET_ARENA_SHOP_BUY payload (S170-175, NORTHSTAR §19's shop system):
// which item (index into packages/simulation/arena_game.c's ARENA_ITEMS
// catalog) the sending client's own hero wants to buy. Server validates
// shop-proximity + Flow balance (arena_shop_buy) -- same trust model as
// every other Arena*Cmd, the server infers "which hero" from the sending
// client's own slot.
typedef struct {
    uint8_t item_id;
} ArenaShopBuyCmd;

// PACKET_ARENA_SHOP_SELL payload: which equip slot (ArenaItemSlot) to sell
// back. No bag -- selling just empties the slot (arena_shop_sell).
typedef struct {
    uint8_t slot;
} ArenaShopSellCmd;

// PACKET_ARENA_APPLY_BUILD_TEMPLATE payload (2026-08-25, build templates): which preset
// (index into packages/simulation/arena_game.c's ARENA_BUILD_TEMPLATES catalog) to auto-buy
// from. Server validates shop-proximity + Flow per item, same trust model as
// PACKET_ARENA_SHOP_BUY -- this is just that same real purchase path called in a loop
// (arena_hero_apply_build_template), not a separate mechanism.
typedef struct {
    uint8_t template_id;
} ArenaApplyBuildTemplateCmd;

// ARENA_SNAPSHOT_ITEM_SLOT_COUNT must match packages/simulation/arena_game.h's
// ARENA_ITEM_SLOT_COUNT (S170-175), same duplication reasoning as every
// other ARENA_SNAPSHOT_* size constant in this file. Needed ahead of
// ArenaHeroSnapshot below since it sizes a fixed array member, unlike the
// others which only size top-level ArenaSnapshotMsg arrays.
#define ARENA_SNAPSHOT_ITEM_SLOT_COUNT 11

// Per-hero state broadcast in PACKET_ARENA_SNAPSHOT. Originally minimal
// (position/HP/alive/hero_id only, no ability-state sync) -- enough for a
// human to see and fight a real remote hero; full status-effect sync
// (silence/intangible/etc.) is a later slice once 1v1 human PvP itself is
// confirmed fun (NORTHSTAR §13).
typedef struct {
    float x, z;
    uint16_t hp;
    uint16_t max_hp;
    uint8_t alive;
    uint8_t hero_id;
    // is_clone/clone_owner (2026-07-30, Tyler "Divided We Stand" rework -- founder: "his kit
    // was stubbed in" / "clones multi control drag click all of it"): real gap found while
    // building independent clone control -- these puppet slots (ARENA_MAX_HEROES..
    // ARENA_HEROES_ARRAY_SIZE-1) were never synced to ANY client at all before this (the hero-
    // chunk loop only ever covered 0..lobby_size-1), so Tyler's clones existed and fought
    // server-side but were completely invisible in every real networked match -- not a control-
    // scheme gap, a rendering gap underneath it. clone_owner is -1 (int8_t) for a real hero,
    // else the owner slot of the Tyler that owns this puppet -- the client needs this both to
    // know which slots are its OWN commandable units (clone_owner == my_owner) and to derive
    // team correctly, since a clone's slot index falls outside the normal "first half team 0,
    // second half team 1" range the ordinary per-hero team-derivation formula below assumes.
    uint8_t is_clone;
    int8_t clone_owner;
    // cast_flash_slot (S170-124, "particle effects for spells"): 0 = none,
    // 1/2/3 = Q/W/R -- set the tick a cast clears its gate (alive, not
    // silenced, off cooldown), regardless of whether it goes on to hit
    // anything, same "cast fires the effect, not just a landed hit"
    // convention as any real MOBA. One-tick lifetime: the server clears its
    // own copy right after broadcasting it, so this is only ever nonzero in
    // the single snapshot immediately following the cast.
    uint8_t cast_flash_slot;
    // Ability-readiness fields (S170-137, "QWER animation frames need to
    // indicate visually if an ability is ready to cast or not"): net_mode
    // never calls arena_update() locally -- apps/arena_server is
    // authoritative and the client only ever reads what's broadcast here
    // (see this struct's own history, e.g. the S170-87 node-sync fix) -- so
    // before this, a networked client's own q/w/r_cooldown_ms and mp just
    // sat at zero forever and the HUD's ability tiles rendered every
    // ability as permanently ready, cooldown and mana state notwithstanding,
    // in the one mode (real PvP) where the answer actually matters. Synced
    // per-slot for every hero, not just the local player's, since the
    // struct's already per-slot and singling one out would need its own
    // wire path for one field. max_mp isn't included: it's flat and
    // roster-wide (ARENA_MP_MAX in arena_game.h), so the client already
    // knows it without a wire round-trip.
    uint16_t q_cooldown_ms;
    uint16_t w_cooldown_ms;
    uint16_t r_cooldown_ms;
    uint8_t mp;
    // attack_target (S170-162, NORTHSTAR §17's click-to-attack system,
    // founder: "up our visual affordances for auto attacks so its
    // readable" / "auto target should still have visual affordances"):
    // -1 (encoded as int8_t, same convention as ArenaCastCmd's own
    // hover_target) if this hero has no attack lock right now, else the
    // owner slot it's currently locked onto. Synced for every hero, not
    // just the local player's, so the lock reads clearly to any hero
    // watching the fight -- not just the two heroes actually involved.
    int8_t attack_target;
    // Economy/equipment fields (S170-175, NORTHSTAR §19's Flow/XP + item
    // shop). flow is the current spendable balance; flow_earned only ever
    // increases (the character pane's own lifetime-earned stat, distinct
    // from the spendable balance -- see ArenaHero.flow_earned's own doc
    // comment in packages/simulation/arena_game.h). kills/deaths are hero
    // kills only, same real-MOBA KDA convention. equipped_item is
    // ARENA_SNAPSHOT_ITEM_SLOT_COUNT slots, -1 (int8_t sentinel, same
    // convention as attack_target/hover_target above) if that slot is
    // empty, else an index into the ARENA_ITEMS catalog -- not synced
    // itself, since it's static roster-wide data both sides already build
    // from the same source, same reasoning as max_mp above.
    uint16_t flow;
    uint16_t flow_earned;
    uint16_t xp;
    uint8_t kills;
    uint8_t deaths;
    int8_t equipped_item[ARENA_SNAPSHOT_ITEM_SLOT_COUNT];
    // w_active (S170-180 bugfix, founder: "it seems like toggelable abilities arent
    // working"): real root cause -- this field never existed on the wire at all, so a
    // networked client's own local ArenaHero.w_active sat at whatever memset left it
    // (always 0/off) no matter what arena_toggle_w actually did server-side. The W
    // ability tile's "active" highlight (and anything else client-side that reads
    // w_active) was correspondingly always wrong in net_mode -- looked broken because it
    // was, not a toggle-logic bug. Synced for every hero, not just the local player's,
    // same "the whole battlefield should read clearly" convention as attack_target above.
    uint8_t w_active;
    // Status effects (S170-184 bugfix, found while adding stunned_ms/slowed_ms: NONE of these
    // fields were ever on the wire, the same class of bug w_active's own doc comment above
    // just described -- the client's "status label above the health bar" HUD feature
    // (hero_status_label, S170-133) has been silently non-functional in every real networked
    // match this whole time, since its local ArenaHero copy never received these from the
    // server. Fixed the same way: synced for every hero, not just the local player's own.
    uint16_t silenced_ms;
    uint16_t rooted_ms;
    uint16_t intangible_ms;
    uint16_t burning_ms;
    uint16_t survive_floor_ms;
    uint16_t stunned_ms;   // S170-184: new generic hard-CC field, see ArenaHero's own doc comment
    uint16_t slowed_ms;    // S170-184: new generic move-speed debuff
    uint8_t slow_pct_x100; // 0-100, slow_pct*100 -- quantized same "lossy is fine" precedent as mp (uint8_t)
    uint16_t berserker_ms; // S170-190: Warsong Gulch-style powerup buff, damage bonus
    uint16_t regen_ms;     // S170-190: Warsong Gulch-style powerup buff, HP regen
    // r_zone_x/r_zone_z/r_active_ms (S170-200, founder: "zone abilities dont read at all we
    // need true aoe cast circle... show cast radius... circle on the ground... showing to all
    // participants that the spell was cast there so it reads"): several heroes' R abilities are
    // real fixed-position ground zones (Ghost/Flamel/Morrigan/Paimon/NOOR-1/Vassago/He Xiangu)
    // or a delayed-detonation marker (Beleth) that persist for several real seconds -- none of
    // that state was ever on the wire, so a networked client had no way to know a zone even
    // existed, let alone where or how big (arena_hero_r_zone_radius, arena_game.h, supplies the
    // "how big" half; this supplies "where" and "for how much longer"). Synced for every hero,
    // not just the local player's own, same "the whole battlefield should read clearly"
    // convention as attack_target/w_active above.
    float r_zone_x, r_zone_z;
    uint16_t r_active_ms;
    // zone_radius_x10 (S202-42, Cart's own zone-circle gap): arena_hero_r_zone_radius(hero_id)
    // supplies the radius for every OTHER zone hero (a fixed per-hero-id constant, so it never
    // needed a wire field of its own) -- but Cart's W (delivery, ARENA_CART_W_RADIUS) and R
    // (ARENA_CART_R_RADIUS) share these same r_active_ms/r_zone_x/z fields with two DIFFERENT
    // real radii (see ArenaHero.zone_radius's own doc comment), so a fixed constant can't be
    // right for him and a networked client had no way to know which one applied. Quantized
    // radius*10 in a uint8_t, same "lossy is fine" precedent slow_pct_x100 already uses --
    // comfortably covers both of Cart's real values (3.0/5.0) with room to spare. 0 for every
    // other hero (arena_hero_r_zone_radius's own constant still supplies their radius).
    uint8_t zone_radius_x10;
    // casting_slot/cast_time_remaining_ms/cast_total_ms (S170-203, founder: "switch gary w to
    // aimed shot just like wow hunter cast time big damage" -> "ensure cast bar affordance
    // shown to user"): generic cast-time-ability state (ArenaHero's own doc comment in
    // arena_game.h) -- Gary's Aimed Shot (W) is the first ability to use it. Synced for every
    // hero, not just the local player's own, same "the whole battlefield should read clearly"
    // convention as attack_target/w_active/r_zone_x above -- a cast bar only a player can see
    // over their own head isn't the affordance that was asked for; every hero watching a Gary
    // wind up a shot needs to see it too, same as every other cast/status affordance already on
    // this wire. cast_anchor_x/z and cast_target aren't synced -- purely server-side
    // interrupt/damage bookkeeping the client's own cast-bar rendering never needs to read.
    uint8_t casting_slot;
    uint16_t cast_time_remaining_ms;
    uint16_t cast_total_ms;
    // blink_cooldown_ms (S170-205, founder: "add blink dagger 1400 flow it gives a new keybind
    // on screen for tilda"): synced so the tilde keybind's own on-screen tile (same "net_mode
    // never calls arena_update() locally, so cooldowns just sat at zero forever without this"
    // gap S170-137 already fixed for q/w/r_cooldown_ms) shows real cooldown state, not a
    // permanently-ready tile. Local player's own hero only really needs this, but synced for
    // every hero same as every other per-hero field on this struct, for consistency.
    uint16_t blink_cooldown_ms;
    // donkey_glide_cooldown_ms (S170-206): same reasoning as blink_cooldown_ms just above, for
    // Donkey's own tilde-bound active (Paper Glide) -- donkey_fold_ms/donkey_airborne_ms aren't
    // synced, same "purely server-side bookkeeping the client's own rendering never needs" call
    // r_zone_x's own doc comment already made for cast_anchor_x/cast_target.
    uint16_t donkey_glide_cooldown_ms;
    // King buff status, 2026-08-20: same class of gap as ArenaKingSnapshot/ArenaCampMinionSnapshot
    // above (real simulation state -- ArenaHero.king_music_carrier/king_growth_stacks/
    // king_growth_ms/king_wealth_ms, plus team-wide king_allseeing_team_ms -- had zero wire
    // representation). Founder, real-time: "i killed the purple king and couldnt even tell if i
    // got a buff." king_buff_flags bit 0 = Music (bool carrier), bit 1 = Growth (ms>0), bit 2 =
    // Wealth (ms>0), bit 3 = All-Seeing (team ms>0, read off the killer's own team so every hero
    // on that team reports the same bit without needing a separate team-wide snapshot field).
    // king_growth_stacks synced separately since it's a real number the HUD needs to show
    // (Bloodroar visibly stacking), not just an on/off state.
    uint8_t king_buff_flags;
    uint8_t king_growth_stacks;
    // duck_smoke_x/duck_smoke_z/duck_smoke_ms (S202-10, Duck's Smoke Bomb): same
    // "every zone-ability hero's ground effect needs to be on the wire or a
    // networked client can't render it" reasoning r_zone_x/r_zone_z/r_active_ms's
    // own doc comment above already established -- this is a W-slot zone, not an
    // R-slot one (Duck's own R, Total Telekinesis, is an instant pull with no
    // zone), so it gets its own fields rather than overloading r_zone_*. Synced
    // for every hero, not just the local player's own, same "the whole
    // battlefield should read clearly" convention as every other field here.
    float duck_smoke_x, duck_smoke_z;
    uint16_t duck_smoke_ms;
} ArenaHeroSnapshot;

// ARENA_SNAPSHOT_MAX_HEROES must match packages/simulation/arena_game.h's
// ARENA_MAX_HEROES (ARENA_TEAM_SIZE*2 = 20, S170-183: reverted back to
// 10v10 after briefly being 14/7v7 under S170-178) -- duplicated here
// rather than included, since protocol.h is a lower-level shared header
// that doesn't otherwise depend on the sim package.
#define ARENA_SNAPSHOT_MAX_HEROES 20

// ARENA_SNAPSHOT_HEROES_ARRAY_SIZE mirrors arena_game.h's ARENA_HEROES_ARRAY_SIZE (ARENA_MAX_HEROES
// + ARENA_MAX_CLONE_SLOTS = 20+8 = 28) -- the full addressable hero-slot range including Tyler's
// puppet-clone pool, same duplication reasoning as ARENA_SNAPSHOT_MAX_HEROES above. Distinct from
// it on purpose: ARENA_SNAPSHOT_MAX_HEROES/lobby_size still means exactly what it always has (how
// many REAL player slots this match uses, the team-split-math bound) -- this constant only widens
// how much of heroes[] the chunk system below addresses, so clone slots get synced too.
#define ARENA_SNAPSHOT_HEROES_ARRAY_SIZE 28

// Per-node territory state broadcast in PACKET_ARENA_SNAPSHOT (S170-87 fix
// -- this didn't exist before, and its absence is the real root cause of
// "the two capture nodes render compressed onto one point in net_mode":
// arena_state.nodes[] was never populated client-side at all, left at
// whatever memset zeroed it to (both nodes at the same (0,0) origin).
// capturing_team mirrors ArenaNode's own -1-or-team-index convention but as
// a signed int8 over the wire (uint8 can't represent -1).
typedef struct {
    float x, z;
    uint8_t owner;            /* 0=neutral/contested, 1=team0, 2=team1 -- matches ArenaNode.owner exactly */
    int8_t capturing_team;    /* -1 = no active channel, else 0/1 */
    uint16_t capture_progress_ms;
} ArenaNodeSnapshot;

// ARENA_SNAPSHOT_NODE_COUNT must match packages/simulation/arena_game.h's
// ARENA_NODE_COUNT, same duplication reasoning as ARENA_SNAPSHOT_MAX_HEROES.
#define ARENA_SNAPSHOT_NODE_COUNT 5 /* S170-119: was 2, mirrors arena_game.h's ARENA_NODE_COUNT */

// Per-projectile state broadcast in PACKET_ARENA_SNAPSHOT (S170-136): the
// first travelling skill-shot in this arena (Gary's Q). Only what a client
// needs to render and react to a shot in flight -- position + which spell
// it is (for visual style) + which owner fired it (for the client's
// existing self/team/enemy color convention, same as ArenaHeroSnapshot).
// Damage/radius/velocity stay server-only; the client only ever needs to
// draw where it currently is.
typedef struct {
    float x, z;
    uint8_t owner;
    uint8_t hero_id;
} ArenaProjectileSnapshot;

// ARENA_SNAPSHOT_MAX_PROJECTILES must match packages/simulation/arena_game.h's
// ARENA_MAX_PROJECTILES, same duplication reasoning as the others above.
#define ARENA_SNAPSHOT_MAX_PROJECTILES 32

// Per-node-guardian-creep state (S170-146, "wire-sync jungle/lane creeps -- the
// single biggest 'looks unfinished in a live match' gap"). Always exactly
// ARENA_SNAPSHOT_CREEP_COUNT entries, index-matched to nodes[] (one creep
// per node, same convention arena_game.h's ArenaCreep already uses) -- a
// fixed-size array like ArenaHeroSnapshot/ArenaNodeSnapshot, not a sparse
// count+array like projectiles, since node-guardian creeps are always fully
// populated (dead ones still occupy their slot, just alive=0).
typedef struct {
    float x, z;
    uint16_t hp;
    uint16_t max_hp;
    uint8_t alive;
    uint8_t flavor; /* 0=neutral, 1=team0, 2=team1 -- matches ArenaCreepFlavor exactly */
} ArenaCreepSnapshot;

// ARENA_SNAPSHOT_CREEP_COUNT must match packages/simulation/arena_game.h's
// ARENA_MAX_CREEPS, same duplication reasoning as the others above.
#define ARENA_SNAPSHOT_CREEP_COUNT 5

// Per-node tower state (2026-07-30, founder: "add towers around the nodes so beginning of game is
// a little slower"). Same "always fully populated, index-matched to nodes[]" convention as
// ArenaCreepSnapshot -- one tower per node, dead ones just sit at alive=0 forever (no respawn, so
// no flavor field needed either: a tower is always neutral-hostile to both teams for as long as
// it's alive, nothing to distinguish over the wire).
typedef struct {
    float x, z;
    uint16_t hp;
    uint16_t max_hp;
    uint8_t alive;
} ArenaTowerSnapshot;

// ARENA_SNAPSHOT_TOWER_COUNT must match packages/simulation/arena_game.h's
// ARENA_NODE_COUNT, same duplication reasoning as every other ARENA_SNAPSHOT_* size constant.
#define ARENA_SNAPSHOT_TOWER_COUNT 5

// Per-powerup state (S170-190, Warsong Gulch-style Berserker/Regen pickups). Unlike fountains
// (static/deterministic, never synced -- see arena_fountain_position's own doc comment),
// powerups have real dynamic state (grabbed or not) that changes from real gameplay events, so
// the client genuinely needs this over the wire. Always exactly ARENA_SNAPSHOT_POWERUP_COUNT
// entries, same "always fully populated" convention as creeps above (an inactive/just-grabbed
// powerup still occupies its slot, just active=0) -- position synced too rather than requiring
// the client to duplicate arena_powerups_reset_layout's own hardcoded midpoint math.
typedef struct {
    float x, z;
    uint8_t kind;   /* 0=Berserker, 1=Regen -- matches ArenaPowerupKind exactly */
    uint8_t active; /* 1 = sitting on the map, ready to be picked up */
} ArenaPowerupSnapshot;

// ARENA_SNAPSHOT_POWERUP_COUNT must match packages/simulation/arena_game.h's
// ARENA_POWERUP_COUNT, same duplication reasoning as every other ARENA_SNAPSHOT_* size
// constant.
#define ARENA_SNAPSHOT_POWERUP_COUNT 2

// Per-lane-creep state (S170-146). Sparse pool (most slots inactive at any
// given tick, waves come and go), same "count + fixed array" convention as
// projectiles.
typedef struct {
    float x, z;
    uint16_t hp;
    uint16_t max_hp;
    uint8_t team;
    uint8_t role; /* S170-218: ArenaLaneCreepRole (0=melee, 1=caster) -- lets the client render
                     and eventually the client itself distinguish the two without guessing from
                     max_hp alone */
} ArenaLaneCreepSnapshot;

// Per-camp-minion state (Jungle Camps Milestone 1). Sparse pool, most slots inactive at any
// given tick (camps wave periodically), same "count + fixed array" convention as projectiles
// and lane creeps above. Simulated server-side since REDGARDEN commit 1402702 but never had a
// wire representation until now -- the client-visibility gap this closes.
typedef struct {
    float x, z;
    uint16_t hp;
    uint16_t max_hp;
    uint8_t camp_index; /* which camp spawned this minion, 0..ARENA_SNAPSHOT_CAMP_COUNT-1 */
} ArenaCampMinionSnapshot;

// ARENA_SNAPSHOT_MAX_CAMP_MINIONS must match packages/simulation/arena_game.h's
// ARENA_MAX_CAMP_MINIONS (ARENA_CAMP_MINIONS_PER_WAVE(2) * ARENA_CAMP_COUNT(4) * 2 = 16), same
// duplication reasoning as every other ARENA_SNAPSHOT_* size constant.
#define ARENA_SNAPSHOT_MAX_CAMP_MINIONS 16

// ARENA_SNAPSHOT_CAMP_COUNT must match arena_game.h's ARENA_CAMP_COUNT.
#define ARENA_SNAPSHOT_CAMP_COUNT 4

// ARENA_SNAPSHOT_OBSTACLE_COUNT must match arena_game.h's ARENA_OBSTACLE_COUNT. Tree passive
// (2026-08-25): obstacles are otherwise a static, never-wire-synced layout (both sides compute
// the same deterministic positions independently, see ArenaObstacle's own doc comment) -- hp is
// the one genuinely dynamic field, "always fully populated" same as kings/creeps/towers below
// rather than a sparse pool, since the layout itself never changes size or order mid-match. Only
// ARENA_OBSTACLE_TREE entries carry a real value; rocks stay 0.
#define ARENA_SNAPSHOT_OBSTACLE_COUNT 32

// Per-King state (Jungle Camps Milestones 2/4). Always exactly ARENA_SNAPSHOT_CAMP_COUNT
// entries, index-matched to camps (0=N/Wealth, 1=S/Growth, 2=E/Music, 3=W/All-Seeing), same
// "always fully populated" convention as powerups above -- a not-yet-spawned or dead King just
// sits at alive=0. Simulated server-side since commit 3943f94 but, like camp minions, never had
// a wire representation -- see this struct's use in ArenaSnapshotMsg for the full story.
typedef struct {
    float x, z;
    uint16_t hp;
    uint16_t max_hp;
    uint8_t alive;
} ArenaKingSnapshot;

// ARENA_SNAPSHOT_MAX_LANE_CREEPS must match packages/simulation/arena_game.h's
// ARENA_MAX_LANE_CREEPS, same duplication reasoning as the others above.
#define ARENA_SNAPSHOT_MAX_LANE_CREEPS 12

// PACKET_ARENA_SNAPSHOT payload (S170-193 split, see ArenaSnapshotHeroesMsg's
// own doc comment below for the full story): the "world" half of a
// broadcast tick -- match phase, draft-pick status, nodes, projectiles,
// creeps, powerups, lane creeps, and the resource race. `count` says how
// many hero slots are actually meaningful (2 for a 1v1 match, up to 20 for
// a full 10v10 lobby) -- heroes[] itself no longer lives here; see
// ArenaSnapshotHeroesMsg. During ARENA_PHASE_WAITING/DRAFT, hero state is
// not meaningful yet -- clients should render a lobby/draft UI instead,
// driven by `phase` and `picked[]`.
typedef struct {
    uint8_t count;
    uint8_t winner; /* 0 = none yet, 1 = team/owner 0 won, 2 = team/owner 1 won */
    uint8_t phase;  /* ARENA_PHASE_WAITING/DRAFT/LIVE */
    uint8_t picked[ARENA_SNAPSHOT_MAX_HEROES]; /* 1 once that slot has locked in a hero this draft */
    ArenaNodeSnapshot nodes[ARENA_SNAPSHOT_NODE_COUNT]; /* S170-87 */
    uint8_t projectile_count; /* S170-136 */
    ArenaProjectileSnapshot projectiles[ARENA_SNAPSHOT_MAX_PROJECTILES];
    ArenaCreepSnapshot creeps[ARENA_SNAPSHOT_CREEP_COUNT]; /* S170-146 */
    ArenaTowerSnapshot towers[ARENA_SNAPSHOT_TOWER_COUNT]; /* 2026-07-30 */
    ArenaPowerupSnapshot powerups[ARENA_SNAPSHOT_POWERUP_COUNT]; /* S170-190 */
    uint8_t lane_creep_count; /* S170-146 */
    ArenaLaneCreepSnapshot lane_creeps[ARENA_SNAPSHOT_MAX_LANE_CREEPS];
    uint16_t resources[2]; /* S170-153: team resource race, capped at ARENA_RESOURCE_CAP */
    uint8_t camp_minion_count; /* jungle camps client-visibility fix, 2026-08-20 */
    ArenaCampMinionSnapshot camp_minions[ARENA_SNAPSHOT_MAX_CAMP_MINIONS];
    ArenaKingSnapshot kings[ARENA_SNAPSHOT_CAMP_COUNT]; /* always fully populated, see that struct's doc comment */
    uint16_t obstacle_hp[ARENA_SNAPSHOT_OBSTACLE_COUNT]; /* Tree passive (2026-08-25) -- see ARENA_SNAPSHOT_OBSTACLE_COUNT's own doc comment. Index-matched to the deterministic obstacle layout both sides already compute identically. */
} ArenaSnapshotMsg;

// PACKET_ARENA_SNAPSHOT_HEROES payload (S170-193, founder: split the
// snapshot into multiple packets rather than accept fragmentation risk).
// Found while sizing this file's own structs: with a full 20-hero lobby,
// heroes[ARENA_SNAPSHOT_MAX_HEROES] alone was 1680 of ArenaSnapshotMsg's
// total 2460 bytes -- comfortably over the typical 1500-byte Ethernet MTU
// even before NetHeader/IP/UDP overhead. A UDP datagram larger than the
// path MTU gets fragmented by IP, and losing any ONE fragment loses the
// WHOLE datagram -- worse packet-loss behavior than staying under MTU. The
// fix here is deliberately NOT "reassemble one logical message from
// sequenced fragments" (that needs its own sequencing/buffering and adds
// real latency waiting on the last piece) -- it's "send several genuinely
// independent, individually-complete packets instead," so losing one only
// ever costs that packet's own slice of state for one tick, not the whole
// snapshot. heroes[] is split into ARENA_SNAPSHOT_HERO_CHUNKS
// self-contained chunks of ARENA_SNAPSHOT_HERO_CHUNK_SIZE heroes each,
// sent as this packet type once per chunk, every broadcast tick, on top of
// the (now heroes-less, ~780-byte) ArenaSnapshotMsg above. total_count is
// duplicated onto every chunk (not read from the ArenaSnapshotMsg) so a
// chunk is fully self-describing and doesn't depend on packet arrival
// order -- team-split math (`i < total_count / 2`) and the "which slots
// are actually meaningful" bound both need it, and either chunk can
// legitimately arrive before or after the world packet, or before or after
// the OTHER chunk, on any given tick.
// 2026-07-30: widened from 10 to 14 (chunk COUNT stays 2 -- 28/14 -- so no new MTU-fragmentation
// risk, same reasoning as the S170-193 split this already is) so the chunk range covers
// ARENA_SNAPSHOT_HEROES_ARRAY_SIZE (28, real heroes + Tyler's clone pool) instead of just
// ARENA_SNAPSHOT_MAX_HEROES (20, real heroes only) -- clone slots were never synced to any
// client before this, see is_clone/clone_owner's own doc comment on ArenaHeroSnapshot above. 14
// heroes' worth is still comfortably under the ~1500-byte MTU this split was built to respect.
#define ARENA_SNAPSHOT_HERO_CHUNK_SIZE 14
#define ARENA_SNAPSHOT_HERO_CHUNKS (ARENA_SNAPSHOT_HEROES_ARRAY_SIZE / ARENA_SNAPSHOT_HERO_CHUNK_SIZE) /* 2 */
typedef struct {
    uint8_t chunk_index;  /* 0..ARENA_SNAPSHOT_HERO_CHUNKS-1 -- which slice of heroes[] this is */
    uint8_t total_count;  /* real lobby size only (same value as ArenaSnapshotMsg.count) -- NOT the
                              clone-inclusive ARENA_SNAPSHOT_HEROES_ARRAY_SIZE, still exactly what
                              the team-split math (`i < total_count / 2`) needs for REAL hero slots;
                              clone-range slots (i >= ARENA_SNAPSHOT_MAX_HEROES) are never gated on
                              this, see the client's own apply-loop doc comment for why */
    ArenaHeroSnapshot heroes[ARENA_SNAPSHOT_HERO_CHUNK_SIZE]; /* owner slots [chunk_index*ARENA_SNAPSHOT_HERO_CHUNK_SIZE .. +SIZE) */
} ArenaSnapshotHeroesMsg;

// Shared receive-buffer sizing for every PACKET_ARENA_SNAPSHOT*-handling
// socket in this codebase (apps/arena_server's send side doesn't need this,
// but every recvfrom call sizing a fixed rbuf does) -- one source of truth
// for "big enough for either snapshot packet type," same "size dynamically,
// never a magic-number guess" discipline S170-192's own critical fixed-
// buffer bug established. Whichever of the two message types is currently
// larger wins; both are comfortably under a real MTU today (this whole
// section exists because the OLD single combined message wasn't), but if a
// future field addition ever pushes one of them back over that line, this
// is the one place that needs the resulting redesign, not three
// independently-drifting call sites.
#define ARENA_SNAPSHOT_RECV_BUF_SIZE (sizeof(NetHeader) + \
    (sizeof(ArenaSnapshotMsg) > sizeof(ArenaSnapshotHeroesMsg) ? sizeof(ArenaSnapshotMsg) : sizeof(ArenaSnapshotHeroesMsg)))

#endif
