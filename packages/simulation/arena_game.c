#include "arena_game.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../common/rl_policy_weights.h"
#include "bloodflower_mod_host.h"
#include "tree_passive_mod_host.h"
#include "build_template_mod_host.h"
#include "item_curriculum_mod_host.h"
#include "duck_smoke_bomb_mod_host.h"
#include "abraham_fireball_mod_host.h"
#include "bacon_puck_intangible_speed_mod_host.h"

ArenaState arena_state;
int arena_bot_enabled = 1;

/* arena_tick_daynight / arena_hero_claim_bloodflower / redgarden_host_spawn_bloodflower
 * (2026-08-25): see arena_game.h's own doc comments (near ARENA_DAYNIGHT_ORBIT_SPEED and each
 * function's declaration) for the full design. Placed here, right after arena_state's own
 * definition, so redgarden_host_spawn_bloodflower -- called back into from the PARENA-compiled
 * on_moon_zenith -- is defined before any other translation unit could need its address; it's
 * only referenced via the extern in bloodflower_mod_host.h, but keeping the real host
 * implementation textually close to that extern (matching this file's existing convention of
 * defining tick functions near their own state) rather than scattered at the bottom. */

void redgarden_host_spawn_bloodflower(int x, int z) {
    arena_state.bloodflower_active = 1;
    arena_state.bloodflower_x = (float)x;
    arena_state.bloodflower_z = (float)z;
    arena_state.bloodflower_ms_remaining = ARENA_BLOODFLOWER_LIFETIME_MS;
}

void arena_hero_claim_bloodflower(void) {
    if (!arena_state.bloodflower_active) return;
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive) continue;
        float dx = h->x - arena_state.bloodflower_x;
        float dz = h->z - arena_state.bloodflower_z;
        float dist_sq = dx * dx + dz * dz;
        if (dist_sq <= ARENA_BLOODFLOWER_CLAIM_RADIUS * ARENA_BLOODFLOWER_CLAIM_RADIUS) {
            h->flow += ARENA_BLOODFLOWER_CLAIM_FLOW;
            arena_state.bloodflower_active = 0;
            return; /* first claim wins, same "stop scanning once resolved" idiom this file's other claim-style loops use */
        }
    }
}

void arena_tick_daynight(unsigned int dt_ms) {
    float dt_sec = (float)dt_ms / 1000.0f;
    arena_state.time_of_day_sec += dt_sec;

    /* Ported from SHANKPIT retro_sky.c's retro_sky_eval_sun_dir -- same orbit_t/tilt math, same
     * sun_y formula. moon_dir_y = -sun_dir_y (retro_lighting.c's own s.moon_dir_x/y/z = -sun_dir
     * relation) -- moon height is highest exactly when sun height is lowest. */
    float orbit_t = arena_state.time_of_day_sec * ARENA_DAYNIGHT_ORBIT_SPEED;
    float sun_height = sinf(orbit_t) * cosf(ARENA_DAYNIGHT_TILT);
    float moon_height = -sun_height;

    int rising_now = (moon_height > arena_state.prev_moon_height);
    if (!rising_now && arena_state.moon_was_rising && !arena_state.daynight_zenith_fired) {
        /* moon_height was climbing last tick, isn't climbing this tick -- we just passed its
         * local maximum (zenith). Real event, through the PARENA mod surface -- see this
         * function's own header comment. */
        arena_state.daynight_zenith_fired = 1;
        on_moon_zenith(0, 0); /* map center -- see ArenaState.bloodflower_x/z's own doc comment */
    }
    if (moon_height < ARENA_DAYNIGHT_ZENITH_REARM_THRESHOLD) {
        arena_state.daynight_zenith_fired = 0;
    }
    arena_state.moon_was_rising = rising_now;
    arena_state.prev_moon_height = moon_height;

    if (arena_state.bloodflower_active) {
        arena_state.bloodflower_ms_remaining -= (int)dt_ms;
        if (arena_state.bloodflower_ms_remaining <= 0) {
            arena_state.bloodflower_active = 0; /* unclaimed, timed out -- real time pressure, see ARENA_BLOODFLOWER_LIFETIME_MS's own doc comment */
        }
    }
}

static float arena_daynight_smoothstep(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

void arena_daynight_ambient_rgb(float *out_r, float *out_g, float *out_b) {
    float orbit_t = arena_state.time_of_day_sec * ARENA_DAYNIGHT_ORBIT_SPEED;
    float sun_height = sinf(orbit_t) * cosf(ARENA_DAYNIGHT_TILT);
    /* Ported from SHANKPIT retro_lighting.c's RETRO_LIGHTING_DYNAMIC ambient formula
     * (sun_visibility = smoothstep(0, 0.22, sun_dir_y); ambient_r/g/b blend by sun_visibility) --
     * same edge values, same blend shape, this game's own dark-green base tone (0.03, 0.05,
     * 0.04, the existing hardcoded glClearColor this replaces) used as the night-side floor
     * instead of SHANKPIT's own night-ambient numbers, so night in this game still reads as
     * "this game's palette at night," not a copy-pasted different game's color grade. */
    float sun_visibility = arena_daynight_smoothstep(0.0f, 0.22f, sun_height);
    if (out_r) *out_r = 0.03f + sun_visibility * 0.10f;
    if (out_g) *out_g = 0.05f + sun_visibility * 0.10f;
    if (out_b) *out_b = 0.04f + sun_visibility * 0.08f;
}

/* ARENA_ITEMS (S170-175): the actual 24-item shop catalog. See
 * arena_game.h's own doc comment on ArenaItemDef/ArenaItemTier for the
 * design shape. WEAPON carries all 12 of docs/HEROES_VS0.md's existing
 * "specific" Season-3-styled items (founder: "season 3 lol is the gold
 * standard for the best meta ever") plus 2 "weird" items pulled from
 * docs/FFXI_ITEM_PARITY_SEED.md's own "notable end-game weapons" section
 * (Kraken Club, Ridill -- both already flagged there as having real
 * unusual reputations; expressed here as unusual STAT SHAPE -- Kraken
 * Club's real "designed to miss" glass-cannon identity becomes huge AD
 * with zero defense, Ridill's real dual-purpose reputation becomes an
 * oddly-even AD/HP/Armor split -- rather than new hit-chance RNG this
 * engine has never needed, same scope discipline NORTHSTAR §17.2 already
 * flagged: no miss/crit RNG exists anywhere in this codebase yet, and
 * this first pass doesn't introduce it just for two items). Every other
 * slot gets exactly one plain, real FFXI name (docs/FFXI_ITEM_PARITY_SEED.md
 * §4, "Armor, by real equip slot") with a flat single-or-double-stat
 * bonus -- a first pass, not exhaustive; more items per slot is a real,
 * cheap follow-on once this catalog is live.
 *
 * bonus_ad only ever affects basic auto-attack damage (ARENA_ATTACK_DAMAGE/
 * ARENA_GARY_ATTACK_DAMAGE at the point of purchase-driven recompute,
 * arena_recompute_item_stats) -- every hero's Q/W/R still deals its own
 * hardcoded flat damage, unaffected by items. Making ability damage
 * stat-scaled too is a real, much larger follow-on (every one of the
 * roster's 26 kits would need its own damage formula rewritten), not
 * attempted this pass. */
const ArenaItemDef ARENA_ITEMS[ARENA_ITEM_COUNT] = {
    /* -- WEAPON: the 12 existing "specific" items, docs/HEROES_VS0.md -- */
    { "Seedling Charm",     ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC,  300,  8,  40,   0,  0, 0.0f },
    { "Bramble Fang",       ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC, 1000, 35,   0,   0,  0, 0.0f },
    { "Thornrender",        ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC,  950, 28,  10,   0,  0, 0.0f },
    { "Bloomheart Core",    ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC, 1100, 45,   0,   0,  0, 0.0f },
    { "Wanecall Grimoire",  ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC,  950, 25,   0,  60,  0, 0.0f },
    { "Ironbark Plate",     ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC,  900, 10, 150,   0, 20, 0.0f },
    { "Willowveil",         ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC,  850,  0, 120,   0, 25, 0.0f },
    { "Vampiric Bloom",     ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC, 1000, 32,  30,   0,  0, 0.0f },
    { "Splinterfang",       ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC,  900, 30,   0,   0,  0, 0.0f },
    { "Hollow Needle",      ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC,  900, 30,   0,  40,  0, 0.0f },
    { "Rootrunner Treads",  ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC,  500,  0,  10,   0,  0, 0.8f },
    { "Gardener's Ward",    ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_SPECIFIC,  800,  0, 100,   0, 15, 0.0f },
    /* -- WEAPON: 2 "weird" items, docs/FFXI_ITEM_PARITY_SEED.md §6 -- */
    { "Kraken Club",        ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_WEIRD,    1200, 60,   0,   0,  0, 0.0f },
    { "Ridill",              ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_WEIRD,    1100, 20,  20,   0, 20, 0.0f },
    /* -- one generic FFXI item per remaining slot -- */
    { "Optical Hat",        ARENA_ITEM_SLOT_HEAD,    ARENA_ITEM_TIER_GENERIC,  400,  0,  60,   0,  0, 0.0f },
    { "Haubergeon",         ARENA_ITEM_SLOT_BODY,    ARENA_ITEM_TIER_GENERIC,  450,  0,   0,   0, 18, 0.0f },
    { "Battle Gloves",      ARENA_ITEM_SLOT_HANDS,   ARENA_ITEM_TIER_GENERIC,  400, 12,   0,   0,  0, 0.0f },
    { "Iron Ram Trousers",  ARENA_ITEM_SLOT_LEGS,    ARENA_ITEM_TIER_GENERIC,  400,  0,   0,   0, 18, 0.0f },
    { "Creek F. Boots",     ARENA_ITEM_SLOT_FEET,    ARENA_ITEM_TIER_GENERIC,  400,  0,   0,   0,  0, 0.6f },
    { "Astral Ring",        ARENA_ITEM_SLOT_RING,    ARENA_ITEM_TIER_GENERIC,  350,  0,   0,  50,  0, 0.0f },
    { "Justice Badge",      ARENA_ITEM_SLOT_NECK,    ARENA_ITEM_TIER_GENERIC,  400,  0,   0,   0, 14, 0.0f },
    { "Forager's Mantle",   ARENA_ITEM_SLOT_BACK,    ARENA_ITEM_TIER_GENERIC,  350,  8,   0,   0,  0, 0.4f },
    { "Warwolf Belt",       ARENA_ITEM_SLOT_WAIST,   ARENA_ITEM_TIER_GENERIC,  400,  0,  80,   0,  0, 0.0f },
    { "Peace Earring",      ARENA_ITEM_SLOT_TRINKET, ARENA_ITEM_TIER_GENERIC,  350,  0,  30,  40,  0, 0.0f },
    /* -- Blink Dagger (S170-205, founder: "add blink dagger 1400 flow it gives a new keybind on
       screen for tilda" -> "+6ap +6hp") -- see ARENA_BLINK_DAGGER_ITEM_ID's own header doc
       comment for why this is fundamentally not like the 24 items above it: the value here is
       the tilde-bound active (arena_use_blink), the +6 AD/+6 HP are real but secondary. Trinket
       slot, same as Peace Earring -- a player picks one or the other, not both. WEIRD tier,
       stretched slightly past its original "unusual FFXI stat shape" framing to cover "an item
       with a genuinely unusual MECHANIC," the more apt read for the one item in this catalog
       that isn't just stats at all. -- */
    { "Blink Dagger",       ARENA_ITEM_SLOT_TRINKET, ARENA_ITEM_TIER_WEIRD,   1400,  6,   6,   0,  0, 0.0f },
    /* -- Donkey (S170-206, founder: "add the weatherman and donkey" -> "donkey should be an
       item" -> "3.2k flow") -- see ARENA_DONKEY_FOLD_HP_FRACTION's own header doc comment for
       the full mechanic. Back slot ("rides on your back"), no stat bonuses at all -- unlike
       Blink Dagger, the founder didn't ask for stats on this one, and its value is entirely the
       two procs (Immortal's Fold + Paper Glide). WEIRD tier, same "unusual mechanic, not just
       unusual stats" stretch as Blink Dagger. -- */
    { "Donkey",             ARENA_ITEM_SLOT_BACK,   ARENA_ITEM_TIER_WEIRD,   3200,  0,   0,   0,  0, 0.0f },
    /* -- Haste Trinket (S170-207, founder: "add a haste trinket" -> "passive haste lowers cd
       and auto attack cd make it a modest improvement 6%") -- pure passive, no active use and
       deliberately no flat stat bonuses either (unlike Blink Dagger's own +6/+6) since the
       founder's own framing was specifically "a modest improvement," not another mobility-tier
       power item. Cost is a judgment call, not founder-specified -- priced as a cheap-ish,
       early-buy utility item (well under Blink Dagger's 1400) matching that "modest" framing,
       not confirmed final tuning. -- */
    { "Haste Trinket",      ARENA_ITEM_SLOT_TRINKET, ARENA_ITEM_TIER_GENERIC,  900,  0,   0,   0,  0, 0.0f, 6 },
    /* -- "expand the play space" first pass (2026-08-11), founder real-time: "do a first pass
       generating weird items that expand the play space" -> "using ffxi items and your own best
       judgement on how the new items with unique qualities" -> "and how they push the meta
       forward" -> "if you cant be creative and want to block on founder direction thats fine but
       honestly i think you can handle it." 6 items, real FFXI names (docs/FFXI_ITEM_PARITY_SEED.md
       §3/§4), pushing this catalog into the shop UI's real page 4 (founder: "add page 4 to the
       shop" -- see ARENA_ITEM_COUNT's own doc comment, no separate paging code needed). Two
       genuinely NEW mechanic categories (true damage, lifesteal -- see ArenaItemDef's own doc
       comment), one dynamic live-computed comeback stat (Balance Ring, see arena_hero_armor's
       own doc comment), three stat-shape-only items following the existing Kraken Club/Ridill
       convention. Index order is catalog-append order (27-32), same "fixed literal, stays stable
       as more items get added" reasoning ARENA_BLINK_DAGGER_ITEM_ID's own doc comment gives. */
    /* Gae Bolg (S170-parity §3 Polearm) -- real mythological spear (Cu Chulainn's, various
       fiction's "never misses/always finds the heart"). This engine has no miss/crit RNG and
       deliberately doesn't introduce any for this pass (same boundary Kraken Club/Ridill's own
       doc comment already flags), so "always finds the heart" is expressed as ARMOR-PIERCING
       true damage instead -- a real, different lever: flat bonus_true_dmg applied AFTER
       apply_armor, the first stat in this catalog that isn't reduced by the target's armor at
       all. Opens a real counter-build against armor-stacking compositions -- "push the meta
       forward" in the most literal sense, a genuine answer to a strategy that previously had no
       direct counter-item. */
    /* Cost tripled 2026-08-13, founder real-time: "the page 4 items need tripple costs" --
       1000 -> 3000, page-4-only pass (other items flagged for a future iteration, not touched
       here). */
    { "Gae Bolg",           ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_WEIRD,   3000,  0,   0,   0,  0, 0.0f, 0, 18, 0 },
    /* Masamune (S170-parity §3 Great Katana) -- real legendary blade, fiction's own recurring
       "benevolent/protective" half of the Masamune-vs-Muramasa pairing (paired below with
       Muramasa's own "cursed/bloodthirsty" half -- a real thematic build-around CHOICE between
       two katana, not two flavors of the same stat). This catalog's first-ever lifesteal: heals
       the wielder for bonus_lifesteal_pct of the FINAL (post-armor) damage on a landed
       auto-attack -- a genuinely new sustain mechanic, opens a real "outlast, don't burst"
       playstyle previously unavailable at all. */
    /* Cost tripled 2026-08-13 (see Gae Bolg's comment above): 1100 -> 3300. */
    { "Masamune",           ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_WEIRD,   3300, 15,   0,   0,  0, 0.0f, 0,  0, 15 },
    /* Muramasa (S170-parity §3 Great Katana) -- Masamune's own real cursed counterpart, fiction's
       "the blade that thirsts for blood, even its wielder's own." Stat-SHAPE only (same
       discipline Kraken Club/Ridill already established -- not every weird item needs a new
       mechanic), pushed further than Kraken Club's own glass-cannon shape: huge AD, genuinely
       ZERO of everything else, the single most extreme risk/reward weapon in the catalog. */
    /* Cost tripled 2026-08-13 (see Gae Bolg's comment above): 1150 -> 3450. */
    { "Muramasa",            ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_WEIRD,   3450, 70,   0,   0,  0, 0.0f, 0,  0, 0 },
    /* Balance Ring (S170-parity §4 Ring) -- real FFXI accessory name, reframed here around its
       own name's literal meaning: a COMEBACK item, armor bonus that scales with the wearer's own
       MISSING hp fraction, computed live in arena_hero_armor() (see that function's own doc
       comment) rather than baked in once at purchase time -- can't be, since it depends on
       state that changes every tick, the same reason Zagan's mirror/King Wealth's aura are also
       computed live there instead of cached. The catalog's first dynamic, state-dependent item
       stat -- rewards fighting on while low rather than always being strictly better at full HP,
       real rubber-band design in the same spirit as §25.3's own synergy decay (a different
       system, same "give the losing side real openings" philosophy). ARENA_BALANCE_RING_ITEM_ID
       (arena_game.h) is the named index arena_hero_armor checks. */
    /* Cost tripled 2026-08-13 (see Gae Bolg's comment above): 900 -> 2700. */
    { "Balance Ring",       ARENA_ITEM_SLOT_RING,    ARENA_ITEM_TIER_WEIRD,   2700,  0,   0,   0,  0, 0.0f, 0,  0, 0 },
    /* Empress Hairpin (S170-parity §4 Head) -- real FFXI item, real-game reputation as a
       caster/MP-support accessory. Mana-focused stat blend: a real bonus_max_mp plus a touch of
       the existing bonus_cdr_pct stat (Haste Trinket's own mechanic, reused not duplicated) --
       supports an ability-spam playstyle the existing Head-slot item (Optical Hat, flat HP) does
       nothing for. */
    /* Cost tripled 2026-08-13 (see Gae Bolg's comment above): 450 -> 1350. */
    { "Empress Hairpin",    ARENA_ITEM_SLOT_HEAD,    ARENA_ITEM_TIER_GENERIC, 1350,  0,   0, 100,  0, 0.0f, 4,  0, 0 },
    /* Ninja Tekko (S170-parity §4 Hands) -- real FFXI ninja gauntlets. AD+move-speed hybrid
       (an "assassin" stat shape -- hit hard AND get there fast), distinct from the existing
       Hands-slot item (Battle Gloves, pure flat AD) rather than a straight upgrade to it --
       a real alternative build path, not strictly better/worse. */
    /* Cost tripled 2026-08-13 (see Gae Bolg's comment above): 500 -> 1500. */
    { "Ninja Tekko",        ARENA_ITEM_SLOT_HANDS,   ARENA_ITEM_TIER_GENERIC, 1500, 20,   0,   0,  0, 1.0f, 0,  0, 0 },
    /* Kite String (S202-34, founder: "add an item that increases auto attack range by 4% 3333
       flow 'Kite String' trinket") -- no flat stats at all, same "this IS the item, not a bonus
       on top of one" shape Haste Trinket's own cdr_pct-only entry already established. Shares
       the Trinket slot with Haste Trinket/Empress Hairpin/Balance Ring -- a real build choice
       between them, not a strict upgrade. */
    { "Kite String",        ARENA_ITEM_SLOT_TRINKET, ARENA_ITEM_TIER_GENERIC, 3333,  0,   0,   0,  0, 0.0f, 0,  0, 0, 4 },
    /* Luck of the Draw (S205-87, founder, cruise-queue: "we should have a weapon that is on
       like page 5 for 2.2k flow a trinket called 'luck of the draw' that gives some mana regen
       during combat") -- no flat stats at all, same "this IS the item, not a bonus on top of
       one" shape Kite String's own entry just above already established. Shares the Trinket
       slot with Haste Trinket/Empress Hairpin/Balance Ring/Kite String -- a real build choice,
       not a strict upgrade over any of them. +1 flat mp/sec while in combat, doubling the base
       ARENA_MP_REGEN_IN_COMBAT_PER_SEC trickle (1) to 2 while equipped -- a real, modest
       improvement, same restraint Haste Trinket's own "make it a modest improvement" precedent
       set, not a build-defining spike. */
    { "Luck of the Draw",   ARENA_ITEM_SLOT_TRINKET, ARENA_ITEM_TIER_GENERIC, 2200,  0,   0,   0,  0, 0.0f, 0,  0, 0, 0, 1 },
};

/* Item curriculum: see arena_game.h's own "Item curriculum" section doc comment for the full
 * founder-quote chain and honest scope note. Runtime-mutable, separate from the fixed, const
 * ARENA_ITEMS[] catalog above -- deliberately not appended into it, see that doc comment for
 * why. Zero-initialized to a safe, clearly-labeled "not yet generated" placeholder rather than
 * left name=NULL, so any accidental early read (before the training loop ever calls the
 * generator) doesn't crash on a null string. */
static char arena_item_curriculum_names[ARENA_ITEM_CURRICULUM_SLOT_COUNT][80] = {
    { "(curriculum slot: not yet generated)" },
    { "(curriculum slot: not yet generated)" },
    { "(curriculum slot: not yet generated)" },
    { "(curriculum slot: not yet generated)" },
};
ArenaItemDef ARENA_ITEM_CURRICULUM_SLOTS[ARENA_ITEM_CURRICULUM_SLOT_COUNT] = {
    { arena_item_curriculum_names[0], ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_WEIRD, 0, 0, 0, 0, 0, 0.0f, 0, 0, 0 },
    { arena_item_curriculum_names[1], ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_WEIRD, 0, 0, 0, 0, 0, 0.0f, 0, 0, 0 },
    { arena_item_curriculum_names[2], ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_WEIRD, 0, 0, 0, 0, 0, 0.0f, 0, 0, 0 },
    { arena_item_curriculum_names[3], ARENA_ITEM_SLOT_WEAPON, ARENA_ITEM_TIER_WEIRD, 0, 0, 0, 0, 0, 0.0f, 0, 0, 0 },
};

/* arena_item_curriculum_blend_int: average two base stats, then apply a small, DETERMINISTIC
 * jitter (a plain integer hash of the two item ids + slot, not rand()/srand()) so the same pair
 * of base items always generates the same result -- reproducibility matters for a training
 * pipeline that will re-run this deterministically across restarts, and avoids the global
 * rand() state every other real-RNG use in this codebase (item drops, etc.) doesn't need to
 * share with a curriculum-generation call. Jitter is bounded to +/-12% of the averaged value
 * (never negative) so a blend of two real items stays a plausible item, not an outlier. */
static int arena_item_curriculum_blend_int(int a, int b, unsigned int hash) {
    int base = (a + b) / 2;
    if (base == 0) return 0;
    int jitter_range = base * 12 / 100;
    if (jitter_range < 1) return base;
    int jitter = (int)(hash % (unsigned int)(2 * jitter_range + 1)) - jitter_range;
    int result = base + jitter;
    return result < 0 ? 0 : result;
}

/* redgarden_host_item_curriculum_generate_counter_item: the real work behind
 * on-generate-counter-item (stdlib/redgarden/item_curriculum_mod.prn) -- same "mod is the
 * trigger, host C does the mutation" split every prior REDGARDEN mod (Bloodflower, Tree
 * passive, Build templates) already established. Blends base_item_a and base_item_b's own
 * stat fields into ARENA_ITEM_CURRICULUM_SLOTS[slot_index]. Returns the synthetic item id
 * (ARENA_ITEM_COUNT + slot_index) on success, -1 on an out-of-range base item or slot index. */
int redgarden_host_item_curriculum_generate_counter_item(int base_item_a, int base_item_b, int slot_index) {
    if (base_item_a < 0 || base_item_a >= ARENA_ITEM_COUNT) return -1;
    if (base_item_b < 0 || base_item_b >= ARENA_ITEM_COUNT) return -1;
    if (slot_index < 0 || slot_index >= ARENA_ITEM_CURRICULUM_SLOT_COUNT) return -1;

    const ArenaItemDef *a = &ARENA_ITEMS[base_item_a];
    const ArenaItemDef *b = &ARENA_ITEMS[base_item_b];
    /* One deterministic hash seed per stat field so two blends of the same pair don't jitter
     * every field in lockstep (which would just scale the whole item up or down uniformly,
     * defeating the point of a per-stat jitter). */
    unsigned int seed = (unsigned int)(base_item_a * 733 + base_item_b * 41 + slot_index * 17 + 2026);

    ArenaItemDef *out = &ARENA_ITEM_CURRICULUM_SLOTS[slot_index];
    snprintf(arena_item_curriculum_names[slot_index], sizeof(arena_item_curriculum_names[slot_index]),
             "Curriculum: %.28s x %.28s", a->name, b->name);
    out->name = arena_item_curriculum_names[slot_index];
    out->slot = a->slot; /* the resulting item equips into base_item_a's own slot */
    out->tier = ARENA_ITEM_TIER_WEIRD; /* an unusual, generated stat shape, not a hand-authored one */
    out->cost = arena_item_curriculum_blend_int(a->cost, b->cost, seed * 2654435761u);
    out->bonus_ad = arena_item_curriculum_blend_int(a->bonus_ad, b->bonus_ad, seed * 2654435761u + 1);
    out->bonus_max_hp = arena_item_curriculum_blend_int(a->bonus_max_hp, b->bonus_max_hp, seed * 2654435761u + 2);
    out->bonus_max_mp = arena_item_curriculum_blend_int(a->bonus_max_mp, b->bonus_max_mp, seed * 2654435761u + 3);
    out->bonus_armor = arena_item_curriculum_blend_int(a->bonus_armor, b->bonus_armor, seed * 2654435761u + 4);
    out->bonus_move_speed = (a->bonus_move_speed + b->bonus_move_speed) / 2.0f;
    out->bonus_cdr_pct = arena_item_curriculum_blend_int(a->bonus_cdr_pct, b->bonus_cdr_pct, seed * 2654435761u + 5);
    out->bonus_true_dmg = arena_item_curriculum_blend_int(a->bonus_true_dmg, b->bonus_true_dmg, seed * 2654435761u + 6);
    out->bonus_lifesteal_pct = arena_item_curriculum_blend_int(a->bonus_lifesteal_pct, b->bonus_lifesteal_pct, seed * 2654435761u + 7);

    return ARENA_ITEM_COUNT + slot_index;
}

const ArenaItemDef *redgarden_host_item_curriculum_get(int slot_index) {
    if (slot_index < 0 || slot_index >= ARENA_ITEM_CURRICULUM_SLOT_COUNT) return NULL;
    return &ARENA_ITEM_CURRICULUM_SLOTS[slot_index];
}

/* ARENA_BUILD_TEMPLATES: see arena_game.h's own "Build templates" section doc comment for the
   full founder-quote chain and design reasoning. A first, generic (any-hero) pass -- item
   picks/ordering are a real judgment call, not founder-specified numbers, same "reasonable
   defaults, document the choice" precedent ARENA_BLOODFLOWER_CLAIM_FLOW's own doc comment set.
   Each template's item_ids are ordered CHEAPEST-FIRST within its theme (the "complex ordering
   rules" the founder asked for, expressed as literal purchase priority) so a partial Flow
   balance still lands real, useful progress instead of stalling on one expensive first pick.
   One item per slot, verified by hand against ARENA_ITEMS' own indices above -- no two entries
   in the same template share a slot. */
const ArenaBuildTemplate ARENA_BUILD_TEMPLATES[ARENA_BUILD_TEMPLATE_COUNT] = {
    {
        "Bruiser", "Tanky, front-line -- armor and HP first, a heavy weapon last.",
        { 22 /* Warwolf Belt: waist, 400, +80hp */,
          20 /* Justice Badge: neck, 400, +14armor */,
          15 /* Haubergeon: body, 450, +18armor */,
          5  /* Ironbark Plate: weapon, 900, +10ad +150hp +20armor */ },
        4
    },
    {
        "Assassin", "AD and mobility -- hit hard, get there fast.",
        { 21 /* Forager's Mantle: back, 350, +8ad +0.4spd */,
          18 /* Creek F. Boots: feet, 400, +0.6spd */,
          16 /* Battle Gloves: hands, 400, +12ad */,
          8  /* Splinterfang: weapon, 900, +30ad */ },
        4
    },
    {
        "Caster", "MP and cooldown reduction -- an ability-spam playstyle.",
        { 19 /* Astral Ring: ring, 350, +50mp */,
          26 /* Haste Trinket: trinket, 900, +6% cdr */,
          4  /* Wanecall Grimoire: weapon, 950, +25ad +60mp */ },
        3
    },
};

/* arena_creeps_reset (S170-51): shared init helper for both arena_init_*
 * entry points. memset already zeroes alive/respawn_ms_remaining to the
 * correct "spawn on the first tick" defaults; the one field that needs an
 * explicit non-zero sentinel is last_attacked_by_owner (0 would wrongly
 * mean "owner slot 0", not "never hit"). */
static void arena_creeps_reset(void) {
    for (int i = 0; i < ARENA_MAX_CREEPS; i++) {
        arena_state.creeps[i].last_attacked_by_owner = -1;
    }
    /* S170-143: hover_target's own sentinel-after-memset reset, same idiom
       as last_attacked_by_owner above -- 0 would wrongly mean "owner slot
       0," not "no hover target." Piggybacks on this existing shared reset
       helper (already called from both arena_init_with_heroes and
       arena_init_teams) rather than adding a third near-duplicate call
       site. */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        arena_state.hover_target[i] = -1;
    }
    /* attack_target (S170-162): same sentinel-after-memset idiom -- 0 would
       wrongly mean "attacking owner slot 0," not "no attack lock." */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        arena_state.heroes[i].attack_target = -1;
    }
    /* cast_target (S170-203): same sentinel-after-memset idiom -- 0 would wrongly mean
       "casting at owner slot 0." casting_slot itself doesn't need one (0 is already the correct
       "not casting" value memset already leaves it at). */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        arena_state.heroes[i].cast_target = -1;
    }
    /* zagan_r_target (S170-230): same sentinel-after-memset idiom -- 0 would wrongly mean
       "mirroring owner slot 0's armor." */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        arena_state.heroes[i].zagan_r_target = -1;
    }
    /* last_attacked_by_owner/equipped_item (S170-175): same sentinel-after-
       memset idiom -- 0 would wrongly mean "owner slot 0 gets kill credit"
       / "slot 0 has item #0 equipped." Covers the full ARENA_HEROES_ARRAY_SIZE
       range (real heroes AND clone slots) since a clone can be killed and
       needs the same clean starting state a real hero does. */
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        arena_state.heroes[i].last_attacked_by_owner = -1;
        for (int s = 0; s < ARENA_ITEM_SLOT_COUNT; s++) {
            arena_state.heroes[i].equipped_item[s] = -1;
        }
        /* assist_owner (S170-187): same sentinel-after-memset idiom -- 0 would wrongly mean
           "owner slot 0 gets assist credit," not "empty tracking slot." */
        for (int a = 0; a < ARENA_MAX_ASSIST_TRACK; a++) {
            arena_state.heroes[i].assist_owner[a] = -1;
        }
    }
}

/* ---- Tiny hand-authored feed-forward "brain" (kept for training, see below) ----
 * Same shape as SHANKPIT's bot brain (packages/simulation/neural_net.h,
 * dense_layer(): out = activation(W*in + b)) rather than a copy of it --
 * SHANKPIT's net is trained (PyTorch-exported weights in brain_weights.h)
 * against FPS-specific inputs (yaw/pitch/strafe/shoot) that don't exist in
 * this top-down click-to-move arena. This is the same forward-pass
 * mechanism (dense layer -> ReLU -> dense layer -> Tanh) re-sized for this
 * game's inputs/outputs, with hand-picked (not trained) weights.
 *
 * S170-228: no longer what the LIVE game's own arena_bot_tick uses for real
 * solo-practice play (see arena_bot_tick_rl_move below, and
 * arena_bot_tick_heuristic's own doc comment further down for exactly why
 * this stays real, callable code instead of being deleted once the trained
 * policy took over the live path). */
static float dense_relu(const float *in, const float *w, const float *b, int i, int in_size) {
    float sum = b[i];
    for (int j = 0; j < in_size; j++) sum += in[j] * w[i * in_size + j];
    return sum > 0.0f ? sum : 0.0f;
}

/* inputs: [dx_norm, dz_norm, dist_norm, hp_frac_diff] */
static void bot_brain_forward(const float in[4], float out[2]) {
    /* Layer 1: 4 -> 6, ReLU. Neurons 0-3 split dx/dz into +/- halves so the
       output layer can recombine them with relu(x)-relu(-x) == x -- i.e.
       the net's steering output reduces to "turn toward the target,"
       computed through the same layered structure as a trained net would
       use. Neurons 4-5 carry distance/hp-diff signal, wired in with zero
       output weight for now -- left as the hook a future trained pass
       would use for kiting/retreat behavior. */
    static const float w1[6 * 4] = {
        1, 0, 0, 0,
        -1, 0, 0, 0,
        0, 1, 0, 0,
        0, -1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    static const float b1[6] = {0, 0, 0, 0, 0, 0};
    float h[6];
    for (int i = 0; i < 6; i++) h[i] = dense_relu(in, w1, b1, i, 4);

    /* Layer 2: 6 -> 2, Tanh. */
    static const float w2[2 * 6] = {
        1, -1, 0, 0, 0, 0,
        0, 0, 1, -1, 0, 0,
    };
    static const float b2[2] = {0, 0};
    for (int i = 0; i < 2; i++) {
        float sum = b2[i];
        for (int j = 0; j < 6; j++) sum += h[j] * w2[i * 6 + j];
        out[i] = tanhf(sum);
    }
}

/* arena_rl_fill_hero_onehot (2026-07-29, founder: "not just 2 heroes"): writes the one-hot
 * self/foe hero_id blocks apps/arena_training/src/headless.c's own sim_get_obs() writes during
 * training (see that function's own ARENA_TRAINING_OBS_SIZE doc comment for the full "why
 * one-hot" reasoning) -- kept in sync here so a LIVE observation is built exactly the same way a
 * TRAINING one is, index for index. Guarded by RL_POLICY_OBS_SIZE itself (defined in the
 * generated packages/common/rl_policy_weights.h, only as large as whatever model was actually
 * exported) rather than assuming the wider shape unconditionally -- an older, narrower-input
 * model (pre-2026-07-29, 18 floats, no hero identity at all) genuinely doesn't have these input
 * slots, and writing past the end of an 18-float `obs[RL_POLICY_OBS_SIZE]` stack array would be
 * real out-of-bounds corruption, not just wasted work. This is what makes it safe to land this
 * source change BEFORE a matching wider-input model is trained and promoted -- the code compiles
 * either way, and only actually writes/reads the one-hot blocks once the promoted model's own
 * header says they exist. */
#if RL_POLICY_OBS_SIZE >= (18 + 2 * ARENA_HERO_COUNT)
static void arena_rl_fill_hero_onehot(float *obs, ArenaHeroID self_hero, ArenaHeroID foe_hero) {
    for (int i = 0; i < ARENA_HERO_COUNT; i++) {
        obs[18 + i] = 0.0f;
        obs[18 + ARENA_HERO_COUNT + i] = 0.0f;
    }
    if (self_hero >= 0 && self_hero < ARENA_HERO_COUNT) obs[18 + self_hero] = 1.0f;
    if (foe_hero >= 0 && foe_hero < ARENA_HERO_COUNT) obs[18 + ARENA_HERO_COUNT + foe_hero] = 1.0f;
}
#else
/* Narrower model still live -- no-op, see this block's own doc comment above. */
static void arena_rl_fill_hero_onehot(float *obs, ArenaHeroID self_hero, ArenaHeroID foe_hero) {
    (void)obs; (void)self_hero; (void)foe_hero;
}
#endif

/* ---- Trained reinforcement-learning "brain" for the bot hero's movement ----
 * S170-228 (founder: "let it train longer then dump the weights into c and
 * commit" -> "update our bots to use it instead of the hand written net"):
 * replaces the old bot_brain_forward -- a tiny feed-forward net with
 * HAND-PICKED (never trained) weights, explicitly flagged in its own doc
 * comment as "the honest 'or equivalent' for tonight... real training
 * pipeline is a fast-follow" back when it was written. NORTHSTAR §21's own
 * reward-driven RL pipeline (S170-223..227) is that fast-follow: a real PPO
 * policy trained against exactly this 1v1 local-demo setup (scripts/
 * rl_train.py, ArenaTrainingEnv), exported by scripts/export_rl_policy_to_c.py
 * into packages/common/rl_policy_weights.h's own rl_policy_forward() --
 * genuinely learned weights, not hand-picked ones, run through the same
 * generic packages/common/mlp_infer.c engine SHANKPIT's own precedent uses
 * for its (also trained) bot brain.
 *
 * Movement always comes from this policy for the bot slot. Casting is a narrower call: the
 * policy's own action space is [move_x, move_z, cast_q, cast_w, cast_r] (scripts/rl_env.py's
 * own ArenaTrainingEnv), and 2026-07-29's spatial-generalization retrain still only ever saw
 * ONE specific hero pairing (rl_train.py's own --hero0-id/--hero1-id, Unicorn vs Duck by
 * default) -- using its cast_* outputs for any OTHER hero's kit would be a real behavior
 * regression (Gary's Aimed Shot and Zagan's Conjunction have nothing in common with Unicorn's or
 * Duck's abilities, the policy has no idea either exists). arena_bot_tick_rl_cast below applies
 * the same trained cast_* outputs, but ONLY when the bot is actually playing Unicorn or Duck --
 * bot_cast_kit_if_ready (the per-hero Q/W/R heuristic, hero-aware across the full ~28-hero
 * roster) still drives every other hero's casting, unchanged, same as before this pass. */
static void arena_bot_tick_rl_move(ArenaHero *bot, ArenaHero *foe) {
    /* Mirror correction: training (scripts/rl_train.py's own default
       --hero0-id/--hero1-id) always put the LEARNING side (the Agent, whose
       actions get optimized) at owner 0, which arena_init_with_heroes spawns
       at x=-6 -- the opposing heuristic-AI side was always owner 1, at
       x=+6, for every single training episode, never randomized to the
       other side. This live call site controls owner 1 (the "bot" in the
       1v1 local demo, always the +6 side) -- feeding it raw, unmirrored
       coordinates would show the policy an x-position range it never once
       saw for "self" during training. Negating x before building the
       observation (and negating action[0] back on the way out) makes owner
       1 look, from the policy's own point of view, exactly like the -6-side
       Agent it actually trained as -- z is untouched, since training never
       had a z-axis asymmetry to begin with (both heroes could occupy any z
       freely). Same 18-float layout apps/arena_training/src/headless.c's
       own sim_get_obs() documents and scripts/rl_env.py's own OBS_*
       constants mirror otherwise -- self first, then foe, then dx/dz. */
    float obs[RL_POLICY_OBS_SIZE];
    obs[0]  = (float)bot->hp;
    obs[1]  = (float)bot->max_hp;
    obs[2]  = (float)bot->mp;
    obs[3]  = -bot->x;
    obs[4]  = bot->z;
    obs[5]  = (float)(bot->q_cooldown_ms > 0 ? bot->q_cooldown_ms : 0);
    obs[6]  = (float)(bot->w_cooldown_ms > 0 ? bot->w_cooldown_ms : 0);
    obs[7]  = (float)(bot->r_cooldown_ms > 0 ? bot->r_cooldown_ms : 0);
    obs[8]  = (float)(bot->flow > 0 ? bot->flow : 0);
    obs[9]  = (float)bot->xp;
    obs[10] = bot->alive ? 1.0f : 0.0f;
    obs[11] = (float)(foe->hp > 0 ? foe->hp : 0);
    obs[12] = (float)foe->max_hp;
    obs[13] = -foe->x;
    obs[14] = foe->z;
    obs[15] = foe->alive ? 1.0f : 0.0f;
    obs[16] = (-foe->x) - (-bot->x);
    obs[17] = foe->z - bot->z;
    arena_rl_fill_hero_onehot(obs, bot->hero_id, foe->hero_id);

    float action[RL_POLICY_ACTION_SIZE];
    rl_policy_forward(obs, action);

    /* action[0]/action[1] are absolute world coordinates in the policy's own
       (mirrored) -6-side frame, already clipped to +-RL_POLICY_MOVE_TARGET_RANGE
       by rl_policy_forward itself -- un-mirror action[0] back to owner 1's
       real +6-side frame before applying it. z needs no such correction. */
    arena_set_move_target(bot->owner, -action[0], action[1]);
}

/* arena_bot_tick_rl_cast (2026-07-29): the casting half of the same trained policy
 * arena_bot_tick_rl_move already uses for movement -- see that function's own doc comment for
 * the full mirroring/observation-layout explanation, identical here. Only ever called for
 * Unicorn or Duck (rl_train.py's own trained pairing, see the caller's own gating) -- calling
 * this for any other hero would apply a Unicorn/Duck-shaped cast decision to a kit the policy
 * has never seen, a real behavior regression this stays scoped away from.
 *
 * action[2]/action[3]/action[4] are cast_q/cast_w/cast_r, same ">0 = attempt this tick"
 * threshold scripts/rl_env.py's own step() already uses -- matching the training-time
 * interpretation exactly, rather than inventing a different one for live play. */
static void arena_bot_tick_rl_cast(ArenaHero *bot, ArenaHero *foe) {
    float obs[RL_POLICY_OBS_SIZE];
    obs[0]  = (float)bot->hp;
    obs[1]  = (float)bot->max_hp;
    obs[2]  = (float)bot->mp;
    obs[3]  = -bot->x;
    obs[4]  = bot->z;
    obs[5]  = (float)(bot->q_cooldown_ms > 0 ? bot->q_cooldown_ms : 0);
    obs[6]  = (float)(bot->w_cooldown_ms > 0 ? bot->w_cooldown_ms : 0);
    obs[7]  = (float)(bot->r_cooldown_ms > 0 ? bot->r_cooldown_ms : 0);
    obs[8]  = (float)(bot->flow > 0 ? bot->flow : 0);
    obs[9]  = (float)bot->xp;
    obs[10] = bot->alive ? 1.0f : 0.0f;
    obs[11] = (float)(foe->hp > 0 ? foe->hp : 0);
    obs[12] = (float)foe->max_hp;
    obs[13] = -foe->x;
    obs[14] = foe->z;
    obs[15] = foe->alive ? 1.0f : 0.0f;
    obs[16] = (-foe->x) - (-bot->x);
    obs[17] = foe->z - bot->z;
    arena_rl_fill_hero_onehot(obs, bot->hero_id, foe->hero_id);

    float action[RL_POLICY_ACTION_SIZE];
    rl_policy_forward(obs, action);

    if (action[2] > 0.0f) arena_cast_q(bot->owner);
    if (action[3] > 0.0f) arena_toggle_w(bot->owner);
    if (action[4] > 0.0f) arena_cast_r(bot->owner);
}

/* S170-119: Arathi Basin-style 5-node spread -- two flanking nodes near each
 * team's spawn (heroes[0] at x=-6, heroes[1] at x=6, see below) plus one
 * contested center node, same "two-near-each-side plus a middle" shape as
 * the real Stables/Farm .. Blacksmith .. Lumber Mill/Gold Mine layout, just
 * along this arena's existing spawn axis instead of Arathi's own geography. */
static void arena_nodes_reset_layout(void) {
    /* S170-191, founder: "use golden ratio to expand map size" -- each node scaled by the same
       phi (1.618034) factor ARENA_HALF_EXTENT itself scaled by, so the whole node spread grows
       proportionally with the map instead of staying clustered in the old, now much smaller
       relative footprint. Written as the original pre-S170-191 coordinate times phi, not a
       pre-computed literal, so the scaling stays visible and traceable, same idiom
       ARENA_HALF_EXTENT's own definition now uses. Blacksmith stays at the true center (0,0)
       regardless of scale. */
    static const float layout[ARENA_NODE_COUNT][2] = {
        { -18.0f * 1.618034f,  11.0f * 1.618034f }, /* Stables */
        { -18.0f * 1.618034f, -11.0f * 1.618034f }, /* Farm */
        {   0.0f,   0.0f }, /* Blacksmith (center, contested) */
        {  18.0f * 1.618034f,  11.0f * 1.618034f }, /* Lumber Mill */
        {  18.0f * 1.618034f, -11.0f * 1.618034f }, /* Gold Mine */
    };
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        arena_state.nodes[n].x = layout[n][0];
        arena_state.nodes[n].z = layout[n][1];
        arena_state.nodes[n].marked_by_team = -1;
        arena_state.nodes[n].capturing_team = -1;
    }
}

/* arena_towers_reset: see the header declaration's own doc comment. Must run after
 * arena_nodes_reset_layout -- reads arena_state.nodes[n].x/z, which that function just set. */
void arena_towers_reset(void) {
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        ArenaTower *tower = &arena_state.towers[n];
        tower->x = arena_state.nodes[n].x;
        tower->z = arena_state.nodes[n].z;
        tower->max_hp = tower->hp = ARENA_TOWER_MAX_HP;
        tower->alive = 1;
        tower->attack_cooldown_ms = 0;
        tower->last_attacked_by_owner = -1;
    }
}

/* arena_obstacles_reset_layout (S170-138, "add rocks and trees so we
 * naturally start to create some lanes"): two mirrored jungle walls, one
 * between each team's spawn column and that side's flank nodes (Stables/
 * Farm/Lumber Mill/Gold Mine, see arena_nodes_reset_layout's own layout).
 * Each wall spans a real z-range wide enough that a hero can't draw a
 * straight line through it to a flank node, forcing a detour around the
 * top or the bottom, which is the actual "lanes" the founder asked for:
 * two routes either side of terrain you can't walk through, rather than
 * one open field. Deliberately never reaches the x=0 mid lane or the 1v1
 * local demo's own spawn points/movement-test coordinates (which stay
 * within |x|<7, never rescaled -- see S170-191's own note on why below) --
 * the jungle is additive scenery + flank routing, not a change to how the
 * existing 1v1 demo or its test suite already move heroes.
 *
 * S170-191, founder: "use golden ratio to expand map size and add more
 * jungle obstacles." The original 22-piece layout scaled by the same phi
 * factor ARENA_HALF_EXTENT/the node layout itself now use (written as the
 * original coordinate times phi, same traceable idiom), plus 10 new
 * pieces: 2 more per wall (extending coverage further out to match the
 * flank nodes' own new, further-out z-spread) and 6 more scattered
 * dressing pieces filling the substantial new outer margin the map extent
 * growing from ~32 to ~51.78 opened up -- "add more jungle obstacles" so
 * a bigger map doesn't just mean more empty space. The 1v1 local demo's
 * own spawn points (x=+-6) are deliberately NOT rescaled -- that's a
 * separate, always-compact practice pairing, unrelated to team mode's own
 * map scale (same distinction this function's own original comment
 * already drew before this pass).
 *
 * S170-148 bugfix: made public (was static) so apps/arena's own requeue
 * handler can call it directly. Obstacles are never wire-synced (client
 * computes the same static layout independently, same "no sync needed for
 * a deterministic layout" precedent fountains also use) -- but the
 * requeue-after-a-networked-match button does a blanket
 * `memset(&arena_state, 0, ...)` before reconnecting, which silently wiped
 * the client's own obstacles[] to all-zero with nothing to repopulate it.
 * First match after program start looked correct (this function's own
 * initial call in arena_init_with_heroes/arena_init_teams populated it
 * once); every match reached via requeue afterward showed an empty map --
 * exactly the "first game had jungle rocks and trees, subsequent games
 * didn't" bug report this fixes. */
void arena_obstacles_reset_layout(void) {
    static const struct { float x, z, radius; ArenaObstacleKind kind; } layout[ARENA_OBSTACLE_COUNT] = {
        /* left wall (between team 0's spawn and Stables/Farm) */
        { -11.5f * 1.618034f,  5.5f * 1.618034f, 1.0f, ARENA_OBSTACLE_TREE },
        { -13.0f * 1.618034f,  4.0f * 1.618034f, 0.9f, ARENA_OBSTACLE_ROCK },
        { -10.5f * 1.618034f,  2.5f * 1.618034f, 1.0f, ARENA_OBSTACLE_TREE },
        { -12.5f * 1.618034f,  1.0f * 1.618034f, 0.9f, ARENA_OBSTACLE_ROCK },
        { -11.0f * 1.618034f, -1.0f * 1.618034f, 1.0f, ARENA_OBSTACLE_TREE },
        { -13.5f * 1.618034f, -2.5f * 1.618034f, 0.9f, ARENA_OBSTACLE_ROCK },
        { -10.5f * 1.618034f, -4.0f * 1.618034f, 1.0f, ARENA_OBSTACLE_TREE },
        { -12.0f * 1.618034f, -5.5f * 1.618034f, 0.9f, ARENA_OBSTACLE_ROCK },
        { -17.5f,  11.5f, 1.0f, ARENA_OBSTACLE_TREE }, /* new (S170-191): extends the wall further toward the flank nodes' own new spread */
        { -19.0f, -11.5f, 0.9f, ARENA_OBSTACLE_ROCK },
        /* right wall (mirrored, between team 1's spawn and Lumber Mill/Gold Mine) */
        {  11.5f * 1.618034f,  5.5f * 1.618034f, 1.0f, ARENA_OBSTACLE_TREE },
        {  13.0f * 1.618034f,  4.0f * 1.618034f, 0.9f, ARENA_OBSTACLE_ROCK },
        {  10.5f * 1.618034f,  2.5f * 1.618034f, 1.0f, ARENA_OBSTACLE_TREE },
        {  12.5f * 1.618034f,  1.0f * 1.618034f, 0.9f, ARENA_OBSTACLE_ROCK },
        {  11.0f * 1.618034f, -1.0f * 1.618034f, 1.0f, ARENA_OBSTACLE_TREE },
        {  13.5f * 1.618034f, -2.5f * 1.618034f, 0.9f, ARENA_OBSTACLE_ROCK },
        {  10.5f * 1.618034f, -4.0f * 1.618034f, 1.0f, ARENA_OBSTACLE_TREE },
        {  12.0f * 1.618034f, -5.5f * 1.618034f, 0.9f, ARENA_OBSTACLE_ROCK },
        {  17.5f,  11.5f, 1.0f, ARENA_OBSTACLE_TREE }, /* new (S170-191) */
        {  19.0f, -11.5f, 0.9f, ARENA_OBSTACLE_ROCK },
        /* scattered outer-edge dressing, purely for jungle vibe -- past every
           node and lane, never in the way of anything */
        { -23.0f * 1.618034f,   6.0f * 1.618034f, 1.1f, ARENA_OBSTACLE_TREE },
        {  23.0f * 1.618034f,  -6.0f * 1.618034f, 1.1f, ARENA_OBSTACLE_TREE },
        {  -6.0f * 1.618034f,  17.0f * 1.618034f, 0.9f, ARENA_OBSTACLE_ROCK },
        {   6.0f * 1.618034f, -17.0f * 1.618034f, 0.9f, ARENA_OBSTACLE_ROCK },
        { -20.0f * 1.618034f, -15.0f * 1.618034f, 1.0f, ARENA_OBSTACLE_TREE },
        {  20.0f * 1.618034f,  15.0f * 1.618034f, 1.0f, ARENA_OBSTACLE_TREE },
        /* new (S170-191): fills the substantial new outer margin the map extent's own growth
           (~32 to ~51.78) opened up -- well clear of fountains/graveyards/shops in every
           corner (all sit at |x|,|z| ~44-48). */
        { -45.0f,  20.0f, 1.2f, ARENA_OBSTACLE_TREE },
        {  45.0f, -20.0f, 1.2f, ARENA_OBSTACLE_TREE },
        { -15.0f,  40.0f, 1.0f, ARENA_OBSTACLE_ROCK },
        {  15.0f, -40.0f, 1.0f, ARENA_OBSTACLE_ROCK },
        { -38.0f, -32.0f, 1.1f, ARENA_OBSTACLE_TREE },
        {  38.0f,  32.0f, 1.1f, ARENA_OBSTACLE_TREE },
    };
    for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) {
        arena_state.obstacles[i].x = layout[i].x;
        arena_state.obstacles[i].z = layout[i].z;
        arena_state.obstacles[i].radius = layout[i].radius;
        arena_state.obstacles[i].kind = layout[i].kind;
        /* Tree passive (2026-08-25): only ARENA_OBSTACLE_TREE gets real hp -- rocks stay at
           0/0, same "field exists but only one kind reads it" convention this struct's own doc
           comment describes. */
        if (layout[i].kind == ARENA_OBSTACLE_TREE) {
            arena_state.obstacles[i].hp = ARENA_TREE_HP;
            arena_state.obstacles[i].max_hp = ARENA_TREE_HP;
        } else {
            arena_state.obstacles[i].hp = 0;
            arena_state.obstacles[i].max_hp = 0;
        }
    }
}

/* arena_tick_obstacles: see header declaration's own doc comment. */
void arena_tick_obstacles(unsigned int dt_ms) {
    for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) {
        ArenaObstacle *o = &arena_state.obstacles[i];
        if (o->max_hp <= 0 || o->hp >= o->max_hp) continue;
        o->hp += (ARENA_TREE_REGEN_PER_SEC * (int)dt_ms) / 1000;
        if (o->hp > o->max_hp) o->hp = o->max_hp;
    }
}

/* redgarden_host_tree_passive_strike and arena_hero_tree_passive themselves are defined further
   down (right after arena_hero_attack_camp_minions), not here -- both need hero_is_hittable/
   apply_cdr, static helpers whose real definitions (not just forward declarations) don't exist
   yet at this point in the file; camp_minions' own attack function already sits past both. */

void arena_init_with_heroes(ArenaHeroID player_hero, ArenaHeroID bot_hero) {
    memset(&arena_state, 0, sizeof(arena_state));

    arena_state.heroes[0].x = -6.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].target_x = -6.0f;
    arena_state.heroes[0].target_z = 0.0f;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].mp = arena_state.heroes[0].max_mp = ARENA_MP_MAX;
    arena_state.heroes[0].owner = 0;
    arena_state.heroes[0].alive = 1;
    arena_state.heroes[0].active = 1;
    arena_state.heroes[0].team = 0;
    arena_state.heroes[0].hero_id = player_hero;

    arena_state.heroes[1].x = 6.0f;
    arena_state.heroes[1].z = 0.0f;
    arena_state.heroes[1].target_x = 6.0f;
    arena_state.heroes[1].target_z = 0.0f;
    arena_state.heroes[1].hp = arena_state.heroes[1].max_hp = 100;
    arena_state.heroes[1].mp = arena_state.heroes[1].max_mp = ARENA_MP_MAX;
    arena_state.heroes[1].owner = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].team = 1;
    arena_state.heroes[1].hero_id = bot_hero;

    arena_nodes_reset_layout();
    arena_powerups_reset_layout(); /* S170-190 */
    arena_obstacles_reset_layout();
    arena_creeps_reset();
    /* Deliberately no arena_towers_reset() here -- towers (2026-07-30) are team-mode only, same
       scope lane creep waves already carry ("pushing toward the enemy spawn" isn't a meaningful
       concept in 1v1 practice). Left at memset-zero (alive=0), so arena_tick_towers/
       arena_hero_attack_towers are no-ops for this whole 1v1 path -- not called from arena_update
       at all, see that function's own doc comment. */

    arena_state.winner = 0;
}

void arena_init(void) {
    /* Player=Unicorn, bot=Duck: both slots carry a real kit (S170-31),
     * proving Phase D's "both sides" requirement rather than just adding a
     * second player-selectable option. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
}

/* arena_hero_attack_range (S202-34): which basic-auto-attack range applies to `hero_id` --
 * ranged/homing heroes (Gary, and now Abraham) get their own real ranged constant, every other
 * hero falls through to the flat melee ARENA_ATTACK_RANGE. Pulled out into a real function
 * instead of the same three-way ternary getting hand-copied a sixth time (it was already
 * duplicated identically at 5 call sites before Abraham's own basic attack needed the same
 * check) -- any hero added to this list only has to be added here, not at every call site. */
/* Takes the whole hero (not just hero_id) as of S202-34's own Kite String trinket
 * (item_bonus_attack_range_pct) -- same "%-bonus stat read off the whole hero struct" shape
 * apply_cdr's own item_bonus_cdr_pct handling already established, applied on top of whichever
 * base range the hero's own kit gives them. */
static float arena_hero_attack_range(const ArenaHero *h) {
    float base = ARENA_ATTACK_RANGE;
    if (h->hero_id == ARENA_HERO_GARY) base = ARENA_GARY_ATTACK_RANGE;
    else if (h->hero_id == ARENA_HERO_ABRAHAM) base = ARENA_ABRAHAM_ATTACK_RANGE;
    return base * (1.0f + (float)h->item_bonus_attack_range_pct / 100.0f);
}

void arena_set_move_target(int owner, float x, float z) {
    /* 2026-07-30, Tyler clone-control rework: widened from ARENA_MAX_HEROES to
       ARENA_HEROES_ARRAY_SIZE so a puppet clone slot can receive its own independent move
       command directly, instead of only ever being driven by the now-removed "mirror Tyler's
       own move-target every tick" logic in arena_update_teams. Authorization (is the caller
       actually allowed to command this slot) is the caller's job, same as it always has been --
       see arena_owner_controls, used by the server's own packet handler. */
    if (owner < 0 || owner >= ARENA_HEROES_ARRAY_SIZE) return;
    if (x < -ARENA_HALF_EXTENT) x = -ARENA_HALF_EXTENT;
    if (x > ARENA_HALF_EXTENT) x = ARENA_HALF_EXTENT;
    if (z < -ARENA_HALF_EXTENT) z = -ARENA_HALF_EXTENT;
    if (z > ARENA_HALF_EXTENT) z = ARENA_HALF_EXTENT;
    ArenaHero *mh = &arena_state.heroes[owner];
    /* attack_windup_ms_remaining (S170-204, NORTHSTAR §17.1): "issuing a move command mid-windup
       cancels the swing entirely." Only a command asking the hero to go somewhere it isn't
       already effectively standing cancels the windup -- comparing the new target against the
       hero's OWN current position (not the previous target, which can drift arbitrarily as a
       chased enemy moves) and gating on its own attack range is what tells a genuine reposition/
       retreat apart from the bot AI's own ~100ms decision loop continuously re-affirming
       "stay roughly here and keep fighting" with a slightly different approach-angle offset
       every tick -- without this distinction, that harmless re-affirmation would cancel every
       single windup before it could ever complete, silently breaking melee damage for every
       bot-controlled hero. A real human re-click during windup, or a bot genuinely disengaging,
       both still exceed this radius and correctly cancel. */
    if (mh->attack_windup_ms_remaining > 0) {
        float wdx = x - mh->x, wdz = z - mh->z;
        float range = arena_hero_attack_range(mh);
        if (wdx * wdx + wdz * wdz > range * range) {
            mh->attack_windup_ms_remaining = 0;
        }
    }
    mh->target_x = x;
    mh->target_z = z;
    mh->moving = 1;
    /* S170-162, NORTHSTAR §17.1: "a fresh move command...immediately
       clears the lock" -- a real move command always wins over a stale
       attack-target chase, matching real League's own right-click-ground
       behavior exactly. */
    mh->attack_target = -1;
    mh->attack_move_active = 0; /* a new command always wins, same convention as attack_target's own clear above -- §24 Milestone 2 */
    mh->hold_position = 0; /* same -- §24 Milestone 2 */
    mh->patrol_active = 0; /* same -- §24 Milestone 2 */
}

/* arena_set_attack_target (S170-162): see header declaration's doc
 * comment. */
void arena_set_attack_target(int owner, int target) {
    /* 2026-07-30, Tyler clone-control rework: widened same as arena_set_move_target above, so a
       clone slot can be given its own independent attack-target lock too. */
    if (owner < 0 || owner >= ARENA_HEROES_ARRAY_SIZE) return;
    arena_state.heroes[owner].attack_target = target;
    arena_state.heroes[owner].attack_move_active = 0; /* a new command always wins -- §24 Milestone 2 */
    arena_state.heroes[owner].hold_position = 0; /* same -- §24 Milestone 2 */
    arena_state.heroes[owner].patrol_active = 0; /* same -- §24 Milestone 2 */
}

/* arena_stop_unit (NORTHSTAR.md §24 Milestone 2, 2026-07-31): the first of the real WC3 group-
 * order vocabulary that section names as missing -- cancels owner's current move target and
 * attack-target lock in place, same widened Tyler-clone-control scope as
 * arena_set_move_target/arena_set_attack_target above (any owned slot, not just self). Sets
 * target_x/z to the unit's OWN current position rather than leaving it stale -- moving=0 alone
 * would halt it, but a stale target_x/z sitting there unused is exactly the kind of "looks
 * inert but isn't really" state this file's own conventions elsewhere (e.g. damaged_this_tick's
 * doc comment) go out of their way to avoid. */
void arena_stop_unit(int owner) {
    if (owner < 0 || owner >= ARENA_HEROES_ARRAY_SIZE) return;
    ArenaHero *h = &arena_state.heroes[owner];
    h->target_x = h->x;
    h->target_z = h->z;
    h->moving = 0;
    h->attack_target = -1;
    h->attack_move_active = 0; /* a new command always wins -- §24 Milestone 2 */
    h->hold_position = 0; /* same -- §24 Milestone 2 */
    h->patrol_active = 0; /* same -- §24 Milestone 2 */
}

/* arena_set_attack_move_target (NORTHSTAR.md §17.4 + §24 Milestone 2, 2026-07-31): real LoL/WC3
 * "A + click" -- moves toward (x,z) like a plain move, but arena_tick_attack_move opportunistically
 * diverts to whatever enemy comes within range along the way. Same widened Tyler-clone-control
 * scope and edge clamping as arena_set_move_target above; deliberately does NOT reuse that
 * function's own body, since a plain move must clear attack_move_active (a fresh plain-move
 * command always wins, §17.1) while THIS command needs to set it. */
void arena_set_attack_move_target(int owner, float x, float z) {
    if (owner < 0 || owner >= ARENA_HEROES_ARRAY_SIZE) return;
    if (x < -ARENA_HALF_EXTENT) x = -ARENA_HALF_EXTENT;
    if (x > ARENA_HALF_EXTENT) x = ARENA_HALF_EXTENT;
    if (z < -ARENA_HALF_EXTENT) z = -ARENA_HALF_EXTENT;
    if (z > ARENA_HALF_EXTENT) z = ARENA_HALF_EXTENT;
    ArenaHero *mh = &arena_state.heroes[owner];
    if (mh->attack_windup_ms_remaining > 0) {
        float wdx = x - mh->x, wdz = z - mh->z;
        float range = arena_hero_attack_range(mh);
        if (wdx * wdx + wdz * wdz > range * range) {
            mh->attack_windup_ms_remaining = 0;
        }
    }
    mh->target_x = x;
    mh->target_z = z;
    mh->moving = 1;
    mh->attack_target = -1; /* fresh order, same "a new command clears the old lock" convention */
    mh->attack_move_active = 1;
    mh->attack_move_x = x;
    mh->attack_move_z = z;
    mh->hold_position = 0; /* a new command always wins -- §24 Milestone 2 */
    mh->patrol_active = 0; /* same -- §24 Milestone 2 */
}

/* arena_hold_position (§24 Milestone 2, 2026-07-31): real WC3 "Hold Position" -- see
 * ArenaHero's own hold_position field comment for the full design (why this doesn't chase, why
 * arena_tick_attack_move's scan is extended to cover held units too, why melee "just works" but
 * ranged heroes need that scan). Same shape as arena_stop_unit (halts in place, fresh order
 * clears the old attack-target lock) except attack_move_active/hold_position end up swapped
 * from Stop's own all-zero result. */
void arena_hold_position(int owner) {
    if (owner < 0 || owner >= ARENA_HEROES_ARRAY_SIZE) return;
    ArenaHero *h = &arena_state.heroes[owner];
    h->target_x = h->x;
    h->target_z = h->z;
    h->moving = 0;
    h->attack_target = -1;
    h->attack_move_active = 0;
    h->hold_position = 1;
    h->patrol_active = 0; /* same -- §24 Milestone 2 */
}

/* arena_set_patrol_target (§24 Milestone 2, 2026-07-31): real WC3 "Patrol" -- point A is the
 * unit's own position at the moment of issue, point B is (x,z). Starts walking toward B first
 * (real WC3 behavior: the clicked point is always the first leg), same edge clamping and
 * windup-cancel as arena_set_move_target/arena_set_attack_move_target above. */
void arena_set_patrol_target(int owner, float x, float z) {
    if (owner < 0 || owner >= ARENA_HEROES_ARRAY_SIZE) return;
    if (x < -ARENA_HALF_EXTENT) x = -ARENA_HALF_EXTENT;
    if (x > ARENA_HALF_EXTENT) x = ARENA_HALF_EXTENT;
    if (z < -ARENA_HALF_EXTENT) z = -ARENA_HALF_EXTENT;
    if (z > ARENA_HALF_EXTENT) z = ARENA_HALF_EXTENT;
    ArenaHero *mh = &arena_state.heroes[owner];
    if (mh->attack_windup_ms_remaining > 0) {
        float wdx = x - mh->x, wdz = z - mh->z;
        float range = arena_hero_attack_range(mh);
        if (wdx * wdx + wdz * wdz > range * range) {
            mh->attack_windup_ms_remaining = 0;
        }
    }
    mh->patrol_a_x = mh->x;
    mh->patrol_a_z = mh->z;
    mh->patrol_b_x = x;
    mh->patrol_b_z = z;
    mh->patrol_going_to_b = 1;
    mh->target_x = x;
    mh->target_z = z;
    mh->moving = 1;
    mh->attack_target = -1;
    mh->attack_move_active = 0;
    mh->hold_position = 0;
    mh->patrol_active = 1;
}

/* hero_is_hittable is defined further down this file -- forward-declared here so
 * arena_tick_attack_move below can call it, same "not every caller comes after the real
 * definition" idiom this file's own apply_weapon_skill_damage forward declaration already uses. */
static int hero_is_hittable(const ArenaHero *h);

/* arena_find_opportunistic_target (§24 Milestone 2, 2026-07-31): shared scan used by
 * arena_tick_attack_move (attack-move and hold) and arena_tick_patrol -- finds the nearest
 * hittable enemy within hero index i's own attack range, or -1 if none. Factored out once a
 * third caller (patrol) needed the exact same "who's opportunistically in range right now" logic
 * rather than a third copy of the loop. */
static int arena_find_opportunistic_target(int i) {
    ArenaHero *h = &arena_state.heroes[i];
    float range = arena_hero_attack_range(h);
    int nearest = -1;
    float nearest_dist_sq = range * range;
    for (int j = 0; j < ARENA_MAX_HEROES; j++) {
        if (j == i) continue;
        ArenaHero *cand = &arena_state.heroes[j];
        if (!cand->active || !hero_is_hittable(cand) || cand->team == h->team) continue;
        float dx = cand->x - h->x, dz = cand->z - h->z;
        float dist_sq = dx * dx + dz * dz;
        if (dist_sq <= nearest_dist_sq) {
            nearest = j;
            nearest_dist_sq = dist_sq;
        }
    }
    return nearest;
}

/* arena_tick_attack_move (NORTHSTAR.md §17.4 + §24 Milestone 2): for every hero with
 * attack_move_active OR hold_position (extended 2026-07-31 to cover holding too -- a held ranged
 * hero otherwise never fires at all, since ranged basic attacks only ever go through
 * attack_target, unlike melee's own always-on flat proximity loop, see hold_position's own field
 * comment) and no current attack_target, scans for the nearest hittable enemy within this hero's
 * own attack range and opportunistically locks onto it (arena_tick_attack_targets, called
 * separately, does the actual chase/combat once attack_target is set -- this function only
 * decides WHETHER and WHAT to engage, real "the whole point of attack-move is it re-targets
 * automatically" behavior, §17.1). For attack-move specifically, if nothing's in range and the
 * hero has drifted off its own attack_move_x/z (a previous chase's pure-pursuit overwrote
 * target_x/z, per arena_tick_attack_targets' own "the attack command wins while it's active"
 * precedent), resumes walking toward the original destination instead of standing idle where the
 * chase left off -- held units have no destination to resume, so this half is skipped for them.
 * Deliberately separate from arena_tick_attack_targets rather than folded into it -- that
 * function's whole job is "chase and land hits once a target is locked," this one's is "decide
 * whether a target should be locked at all," different responsibilities even though both
 * read/write attack_target. */
void arena_tick_attack_move(unsigned int dt_ms) {
    (void)dt_ms; /* no per-tick timer needed -- see doc comment; kept for signature symmetry with
                    every other arena_tick_* function in this file. */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive || (!h->attack_move_active && !h->hold_position)) continue;
        if (h->attack_target >= 0) continue; /* already engaged -- arena_tick_attack_targets owns this tick for it */

        int nearest = arena_find_opportunistic_target(i);
        if (nearest >= 0) {
            h->attack_target = nearest;
            continue;
        }
        /* Nothing to engage -- if a prior chase overwrote target_x/z, resume the original
           attack-move destination instead of standing wherever the chase left off. Held units
           have no destination to resume (they're not going anywhere by design), so this only
           applies to attack-move. */
        if (h->attack_move_active &&
            (h->target_x != h->attack_move_x || h->target_z != h->attack_move_z)) {
            h->target_x = h->attack_move_x;
            h->target_z = h->attack_move_z;
            h->moving = 1;
        }
    }
}

#define ARENA_PATROL_ARRIVAL_RADIUS 0.5f /* how close counts as "reached this leg's point" before flipping direction */

/* arena_tick_patrol (§24 Milestone 2, 2026-07-31): real WC3 "Patrol," last of the group-order
 * vocabulary. Walks the unit back and forth between patrol_a_x/z and patrol_b_x/z forever,
 * flipping patrol_going_to_b once the current leg's destination is reached (within
 * ARENA_PATROL_ARRIVAL_RADIUS), opportunistically engaging whatever comes within range along the
 * way via the same arena_find_opportunistic_target scan attack-move/hold already use. Separate
 * from arena_tick_attack_move rather than folded in -- patrol's own two-point ping-pong and
 * arrival detection have no equivalent in that function's single-destination model. */
void arena_tick_patrol(unsigned int dt_ms) {
    (void)dt_ms;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive || !h->patrol_active) continue;

        float leg_x = h->patrol_going_to_b ? h->patrol_b_x : h->patrol_a_x;
        float leg_z = h->patrol_going_to_b ? h->patrol_b_z : h->patrol_a_z;
        float ldx = leg_x - h->x, ldz = leg_z - h->z;
        if (ldx * ldx + ldz * ldz <= ARENA_PATROL_ARRIVAL_RADIUS * ARENA_PATROL_ARRIVAL_RADIUS) {
            h->patrol_going_to_b = !h->patrol_going_to_b;
            leg_x = h->patrol_going_to_b ? h->patrol_b_x : h->patrol_a_x;
            leg_z = h->patrol_going_to_b ? h->patrol_b_z : h->patrol_a_z;
        }

        if (h->attack_target >= 0) continue; /* already engaged -- arena_tick_attack_targets owns this tick for it */

        int nearest = arena_find_opportunistic_target(i);
        if (nearest >= 0) {
            h->attack_target = nearest;
            continue;
        }
        /* Nothing to engage -- resume (or continue) walking toward the current leg, same
           "a prior chase may have overwritten target_x/z" resume logic attack-move uses. */
        if (h->target_x != leg_x || h->target_z != leg_z) {
            h->target_x = leg_x;
            h->target_z = leg_z;
            h->moving = 1;
        }
    }
}

/* arena_owner_controls (2026-07-30, Tyler "Divided We Stand" rework -- founder: "clones multi
 * control drag click all of it"): does `sender_owner` have authority to issue a command for
 * `target_owner`? True for the trivial case (a hero commanding itself, the only case that existed
 * before this) and for a clone whose `clone_owner` names `sender_owner` -- i.e. Tyler can command
 * himself or any of his own active clones, nobody else can command anybody else's. Used by the
 * server's own PACKET_ARENA_MOVE/PACKET_ARENA_ATTACK handlers to validate the new unit_owner/
 * commander_unit fields before acting on them, same trust boundary this game has always drawn at
 * "a client can only ever affect its own hero," just widened from exactly one owned slot to a
 * small owned set. */
int arena_owner_controls(int sender_owner, int target_owner) {
    if (target_owner < 0 || target_owner >= ARENA_HEROES_ARRAY_SIZE) return 0;
    if (target_owner == sender_owner) return 1;
    ArenaHero *target = &arena_state.heroes[target_owner];
    return target->active && target->is_clone && target->clone_owner == sender_owner;
}

/* arena_apply_stun/arena_apply_slow (S170-184): see header declarations' own doc comments.
 * Both take the max of the existing remaining duration and the new one -- a real hard-CC
 * refresh shouldn't ever SHORTEN what's already active (e.g. a weaker follow-up stun landing
 * on top of a stronger one already ticking), same "longer wins" simplification GFD's own
 * Potency-stacking rule approximates for a same-Kind reapply, without needing this codebase to
 * track a separate Potency field just for stun (stun has no potency axis to begin with -- it's
 * binary, on or off). Slow's pct is simply overwritten by the newer application regardless of
 * duration, since a slow's THIS-frame speed effect is what matters, not accumulated history. */
void arena_apply_stun(int owner, int duration_ms) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    if (duration_ms > h->stunned_ms) h->stunned_ms = duration_ms;
}

void arena_apply_slow(int owner, int duration_ms, float pct) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    if (duration_ms > h->slowed_ms) h->slowed_ms = duration_ms;
    h->slow_pct = pct;
}

void arena_bot_tick(unsigned int dt_ms) {
    (void)dt_ms;
    ArenaHero *bot = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    if (!bot->alive || !foe->alive) return;

    arena_bot_tick_rl_move(bot, foe);
}

/* arena_bot_tick_heuristic (S170-228): the ORIGINAL hand-picked-weight movement logic
 * arena_bot_tick itself used before this pass -- kept, not deleted, specifically because
 * apps/arena_training/src/headless.c's own training harness needs a STABLE, never-changing
 * opponent to train against. If the training environment's own "opponent" (owner 1) were
 * driven by arena_bot_tick's now-RL-policy-based movement instead, re-training would mean
 * training against a copy of some EARLIER version of the very policy being trained -- circular,
 * and completely broken on the very first training run, when no rl_policy_weights.h has been
 * exported yet at all. headless.c explicitly disables arena_bot_enabled and calls this function
 * (plus bot_cast_kit_if_ready, also kept callable from outside this file for the same reason)
 * directly for owner 1 instead of going through arena_update's own automatic bot-tick, so
 * training's own opponent never depends on whatever happens to currently be compiled into
 * rl_policy_weights.h. */
void arena_bot_tick_heuristic(unsigned int dt_ms) {
    (void)dt_ms;
    ArenaHero *bot = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    if (!bot->alive || !foe->alive) return;

    float dx = foe->x - bot->x;
    float dz = foe->z - bot->z;
    float dist = sqrtf(dx * dx + dz * dz);

    float in[4];
    in[0] = dx / ARENA_HALF_EXTENT;
    in[1] = dz / ARENA_HALF_EXTENT;
    in[2] = dist / (ARENA_HALF_EXTENT * 2.0f);
    in[3] = ((float)bot->hp / bot->max_hp) - ((float)foe->hp / foe->max_hp);

    float out[2];
    bot_brain_forward(in, out);

    /* Steer a few units ahead in the net's suggested direction each tick --
       cheap re-evaluation gives continuous chase without full pathfinding. */
    float step = 3.0f;
    arena_set_move_target(1, bot->x + out[0] * step, bot->z + out[1] * step);
}

/* resolve_hero_obstacle_collision (S170-138): plain circle-vs-circle push-out,
 * same "cheap and good enough" spirit as the rest of this sim's collision-free
 * approach -- no physics engine, just shove the hero back to the obstacle's
 * edge along the line between their centers. Only ever called right after a
 * hero actually advances under update_hero_motion below, so a hero standing
 * still (or rooted) is never forcibly relocated -- and a few hero abilities
 * that teleport/dash by setting x/z directly (Doc Wheel's W, Morrigan's W,
 * Courier's W/R, ...) bypass this on purpose, same "first pass, not full
 * physics" scope as the rest of S170-138. */
static void resolve_hero_obstacle_collision(ArenaHero *h) {
    for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) {
        const ArenaObstacle *o = &arena_state.obstacles[i];
        float dx = h->x - o->x;
        float dz = h->z - o->z;
        float min_dist = o->radius + ARENA_HERO_COLLISION_RADIUS;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist >= min_dist) continue;
        if (dist < 0.0001f) { dx = 1.0f; dz = 0.0f; dist = 0.0001f; }
        float push = min_dist - dist;
        h->x += dx / dist * push;
        h->z += dz / dist * push;
    }
}

/* resolve_hero_hero_collision (S202-27, "body blocking"): founder real-time, "we need to add
 * body blocking" -> "currently players can ghost through eachother". Same real, honest analog
 * resolve_hero_obstacle_collision just above already uses -- an obstacle is a static circle a
 * hero pushes itself back out of; a hero is now ALSO one, to every other hero. Applies
 * regardless of team (real MOBAs body-block allies too, not just enemies) and across the full
 * ARENA_HEROES_ARRAY_SIZE range (Tyler's puppet clones included -- S170-141's own precedent for
 * widening hero-vs-hero interactions to cover them). Deliberately only called from
 * update_hero_motion's own organic walking-toward-target step, never from forced displacement
 * (Duck's Q/R pulls, Morrigan's W gap-close, knockbacks) -- those set position directly and
 * don't route through this function, so a pull can still land a target inside another hero's
 * radius for that one instant; the NEXT tick's own normal movement resolution pushes it back
 * out again, same "don't fight the ability, let movement self-correct" reasoning
 * donkey_airborne_ms's own skip-collision-while-flying carve-out above already established for
 * a different case. Each hero resolves against every other hero independently (no shared
 * "who moves first" bookkeeping) -- a real, simple v0, not perfectly stable under a 3+-hero
 * pileup (a later hero's own resolution can nudge an earlier one back into slight overlap), but
 * correct and non-jittery for the common 1v1/small-cluster case this engine's own real matches
 * actually produce. */
static void resolve_hero_hero_collision(ArenaHero *h, int self_index) {
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        if (i == self_index) continue;
        const ArenaHero *other = &arena_state.heroes[i];
        if (!other->active || !other->alive) continue;
        float dx = h->x - other->x;
        float dz = h->z - other->z;
        float min_dist = ARENA_HERO_COLLISION_RADIUS * 2.0f;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist >= min_dist) continue;
        if (dist < 0.0001f) { dx = 1.0f; dz = 0.0f; dist = 0.0001f; }
        float push = min_dist - dist;
        h->x += dx / dist * push;
        h->z += dz / dist * push;
    }
}

/* §25.3 synergy-decay helpers -- defined much further down this file (near arena_tick_synergy,
 * their natural home), forward-declared here same as hero_is_hittable/apply_damage's own
 * pattern elsewhere in this file, since both update_hero_motion and apply_cdr below need them
 * before their real definitions appear. */
static int arena_synergy_cdr_pct(const ArenaHero *h);
static float arena_synergy_move_speed_pct(const ArenaHero *h);

static void update_hero_motion(ArenaHero *h, int self_index, float dt_sec) {
    /* rooted_ms (S170-46)/stunned_ms (S170-184): a queued move command is preserved (not
       cancelled) but doesn't advance while either is active -- matches how silence blocks
       casting without clearing the ability off cooldown. Stun is the stronger of the two
       generic movement-blockers (it also blocks casts/attacks, root only blocks movement),
       but there's no meaningful difference in what THIS function does for either -- both just
       mean "don't advance position this tick."
       attack_windup_ms_remaining (S170-204, NORTHSTAR §17.1): same "don't advance position"
       treatment -- a champion mid-windup stands still, full stop, same as real League.
       Abraham's own casting_slot (2026-08-26, founder: "when you hit w on abraham and you are
       moving dont have it blow the cooldown and do nothing have it freeze the player for the
       length of the cast for that ability"): scoped to Abraham specifically, not every
       casting_slot-using hero (Gary's own Aimed Shot deliberately keeps its established
       "movement interrupts the cast" feel, per the founder's own earlier S170-203 spec --
       not touched here) -- see tick_hero_kit's own cast-interrupt block for the matching
       Abraham-only immunity to that same interrupt. */
    if (!h->alive || !h->moving || h->rooted_ms > 0 || h->stunned_ms > 0 || h->attack_windup_ms_remaining > 0 ||
        (h->hero_id == ARENA_HERO_ABRAHAM && h->casting_slot != 0)) return;
    float dx = h->target_x - h->x;
    float dz = h->target_z - h->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.05f) {
        h->moving = 0;
        return;
    }
    /* S202-40: real facing, updated whenever there's a genuine direction to face -- see
       facing_rad's own header doc comment. */
    h->facing_rad = atan2f(dx, dz);
    /* slowed_ms/slow_pct (S170-184, GFD's Slow): a proportional multiplier on top of the base
       speed + item bonus, so it scales correctly regardless of how much flat item speed a hero
       already has (a slow that just subtracted a flat amount could go negative against a
       heavily-itemized hero; a percentage never can).
       donkey_airborne_ms (S170-206, Paper Glide): overrides the slow check entirely rather than
       stacking with it -- "flies over trees etc," off the ground and untouchable while airborne,
       the same reasoning intangible_ms already grants CC-immunity for this exact window. */
    float speed_mult;
    if (h->donkey_airborne_ms > 0) {
        speed_mult = ARENA_DONKEY_GLIDE_SPEED_MULT;
    } else {
        speed_mult = (h->slowed_ms > 0) ? (1.0f - h->slow_pct) : 1.0f;
        if (speed_mult < 0.0f) speed_mult = 0.0f;
    }
    /* East/Music's Catchy Song (Jungle Camps Milestone 2): move-speed half of the buff,
       multiplicative same as the slow above so it scales correctly against item speed too. */
    if (h->king_music_carrier) speed_mult *= (1.0f + ARENA_KING_MUSIC_MOVE_SPEED_PCT / 100.0f);
    /* Bacon+Puck's Shadow Step (Q) intangibility (2026-08-27, founder real-time: "when baconpuck
       goes intangible can we get a movement speed increase?" -> "parena mod powered
       development"). Real PARENA mod call (on_bacon_puck_intangible_speed_pct, stdlib/redgarden/
       bacon_puck_intangible_speed_mod.prn), same multiplicative shape Music's own buff just above
       already uses -- composes correctly with slows/item speed/other buffs rather than fighting
       them. h->intangible_ms is the same real countdown bacon_puck_cast_q already sets (no new
       state added) -- the boost is active for exactly as long as the hero actually is
       intangible, never desyncs from it. */
    if (h->hero_id == ARENA_HERO_BACON_PUCK && h->intangible_ms > 0) {
        speed_mult *= (1.0f + on_bacon_puck_intangible_speed_pct() / 100.0f);
    }
    /* §25.3 synergy decay: ambient team-cohesion move-speed bonus, same multiplicative shape as
       Music's own buff -- decays toward 0 as this hero's team's synergy_tier rises. */
    speed_mult *= (1.0f + arena_synergy_move_speed_pct(h) / 100.0f);
    float step = (ARENA_HERO_SPEED + h->item_bonus_move_speed) * speed_mult * dt_sec; /* S170-175: items (e.g. Rootrunner Treads, Creek F. Boots) */
    if (step >= dist) {
        h->x = h->target_x;
        h->z = h->target_z;
        h->moving = 0;
    } else {
        h->x += dx / dist * step;
        h->z += dz / dist * step;
    }
    /* Skipped while airborne (S170-206) -- "flies over trees etc," the founder's own original
       2026-07-24 direction on Paper Glide, predating this whole item pivot. Every other hero's
       movement still collides normally. */
    if (h->donkey_airborne_ms <= 0) {
        resolve_hero_obstacle_collision(h);
        resolve_hero_hero_collision(h, self_index);
    }
}

/* arena_hero_base_armor: every hero-specific armor rule (S170-175:
 * renamed from arena_hero_armor, which is now a thin public wrapper below
 * that adds item_bonus_armor on top -- items apply universally regardless
 * of which hero-specific branch below fires, so it can't live inside this
 * function's own many early-return branches). Only The Unicorn has passive
 * armor (S170-18); The Duck (S170-31) has none -- dispatch is by hero_id
 * now, not by owner slot, so either side gets Unicorn's armor if either
 * side is playing Unicorn. */
static float arena_hero_base_armor(const ArenaHero *h) {
    if (h->hero_id == ARENA_HERO_UNICORN) {
        float armor = (float)ARENA_UNICORN_ARMOR;
        if (h->r_active_ms > 0) armor *= 2.0f;
        return armor;
    }
    /* Tree's Grand Secret (R, S170-46): flat armor bonus while self-rooted. */
    if (h->hero_id == ARENA_HERO_TREE && h->r_active_ms > 0) {
        return (float)ARENA_TREE_R_ARMOR_BONUS;
    }
    /* Morrigan's Contested Ground (passive, S170-47): bonus armor while
       standing within capture radius of a node that's still contested
       (owner == 0, neither team has claimed it) -- a war goddess belongs
       to the unresolved fight, her jungler tie to the territory system. */
    if (h->hero_id == ARENA_HERO_MORRIGAN) {
        for (int n = 0; n < ARENA_NODE_COUNT; n++) {
            const ArenaNode *node = &arena_state.nodes[n];
            if (node->owner != 0) continue;
            float dx = h->x - node->x, dz = h->z - node->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_NODE_CAPTURE_RADIUS) {
                return (float)ARENA_MORRIGAN_PASSIVE_ARMOR_BONUS;
            }
        }
    }
    /* Loki's Bound Where the Myth Says (W, S170-79): flat armor while toggled on. */
    if (h->hero_id == ARENA_HERO_LOKI && h->w_active) {
        return (float)ARENA_LOKI_W_ARMOR_BONUS;
    }
    /* Ada's frame plating (W, S170-103): flat armor while toggled on. */
    if (h->hero_id == ARENA_HERO_ADA && h->w_active) {
        return (float)ARENA_ADA_W_ARMOR_BONUS;
    }
    /* He Xiangu's Dark stance (W toggle, 2026-08-26): flat armor while in the OFF/Dark half of
       the Light/Dark toggle -- see ARENA_HE_XIANGU_DARK_ARMOR_BONUS's own header comment. Same
       shape as Ada's plating just above, just gated on !w_active instead of w_active since
       Dark is the "toggled off" state of this particular toggle. */
    if (h->hero_id == ARENA_HERO_HE_XIANGU && !h->w_active) {
        return (float)ARENA_HE_XIANGU_DARK_ARMOR_BONUS;
    }
    /* Tyler's Divided We Stand (R, S170-111): armor goes NEGATIVE for the window --
       apply_armor does raw_damage - armor, so a negative value increases damage taken.
       The real risk half of the risk/reward the OG clone-death rule was standing in for. */
    if (h->hero_id == ARENA_HERO_TYLER && h->r_active_ms > 0) {
        return -ARENA_TYLER_R_NEGATIVE_ARMOR;
    }
    /* Cain's founded city (passive, S170-105): flat, always-on -- "the man cast out to wander
       settled down and built civilization anyway," the one permanent thing about him. */
    if (h->hero_id == ARENA_HERO_CAIN) {
        return (float)ARENA_CAIN_PASSIVE_ARMOR;
    }
    /* Gunnr's shieldmaiden stance (passive, S170-93): flat, always-on, same shape as Cain's own. */
    if (h->hero_id == ARENA_HERO_GUNNR) {
        return (float)ARENA_GUNNR_PASSIVE_ARMOR;
    }
    /* Beleth's own survival (passive, S170-93): flat, always-on, same shape as Cain's/
       Gunnr's -- she's outlived every escalation she's ever caused. Lower than either of
       theirs; her kit's damage/control already does the heavy lifting. */
    if (h->hero_id == ARENA_HERO_BELETH) {
        return (float)ARENA_BELETH_PASSIVE_ARMOR;
    }
    /* MnM's shell (passive, S170-134): a flat always-on base, same shape as Cain's/Gunnr's/
       Beleth's own. Used to also get a further toggle bonus while W was active (Loki's/Ada's
       own toggle shape) -- removed under S170-208's Burrow rework, which turned W from a free
       stat toggle into a real cast with its own cooldown, no longer an armor stack at all. */
    if (h->hero_id == ARENA_HERO_MNM) {
        return (float)ARENA_MNM_PASSIVE_ARMOR;
    }
    return 0.0f;
}

/* hero_is_hittable is defined further down this file (S170-32) -- forward-declared here so
 * Zagan's Conjunction mirror (S170-230) below can check target validity; every other caller in
 * this file already comes after the real definition, this is the first that doesn't. */
static int hero_is_hittable(const ArenaHero *h);

/* arena_hero_armor (S170-175): see arena_hero_base_armor's own doc comment
 * just above for why this split exists. */
float arena_hero_armor(const ArenaHero *h) {
    /* Zagan's Conjunction (S170-230): a TRUE mirror, not an additive steal -- for the
       duration, Zagan's TOTAL armor (this function's whole return value, base+items both)
       becomes exactly his locked target's, overriding the normal base+item formula entirely
       rather than adding to it. This has to live here, not in arena_hero_base_armor, precisely
       because it needs to override item_bonus_armor too -- a base-armor-only hook couldn't
       cancel Zagan's own item stats out of the final total. Falls through to the normal
       formula the instant the target stops being hittable (dies, etc.) -- no special-case
       cleanup needed, the mirror just silently stops. */
    if (h->hero_id == ARENA_HERO_ZAGAN && h->r_active_ms > 0 &&
        h->zagan_r_target >= 0 && h->zagan_r_target < ARENA_MAX_HEROES) {
        const ArenaHero *target = &arena_state.heroes[h->zagan_r_target];
        if (hero_is_hittable(target)) return arena_hero_armor(target);
    }
    float total = arena_hero_base_armor(h) + (float)h->item_bonus_armor;
    /* Zagan's Calcination (S170-230, Q): a flat armor-shred debuff, generic to any hero
       carrying it (see zagan_calcination_ms's own struct doc comment) -- applied here, after
       the normal formula, same layering as the mirror override above. */
    if (h->zagan_calcination_ms > 0) total -= (float)ARENA_ZAGAN_Q_ARMOR_SHRED;
    /* North/Wealth's Bulwark aura (Jungle Camps Milestone 2): a flat armor bonus for `h` itself
       AND any teammate within ARENA_KING_WEALTH_AURA_RADIUS of a hero currently holding the
       buff -- "an umbrella large enough to shelter a group," not a per-hero timer, see
       king_wealth_ms's own doc comment. Scans for the nearest holder rather than storing the
       bonus on `h` directly so the aura updates live as holders move, same "computed live, not
       copied" idiom this file already documents for the field itself. */
    for (int wk = 0; wk < ARENA_MAX_HEROES; wk++) {
        const ArenaHero *holder = &arena_state.heroes[wk];
        if (!holder->active || !holder->alive || holder->king_wealth_ms <= 0 || holder->team != h->team) continue;
        if (holder == h) { total += (float)ARENA_KING_WEALTH_ARMOR_BONUS; break; }
        float wdx = holder->x - h->x, wdz = holder->z - h->z;
        if (sqrtf(wdx * wdx + wdz * wdz) <= ARENA_KING_WEALTH_AURA_RADIUS) { total += (float)ARENA_KING_WEALTH_ARMOR_BONUS; break; }
    }
    /* Balance Ring (2026-08-11, "expand the play space" pass): comeback armor, scales with the
       wearer's OWN missing-hp fraction -- 0 bonus at full HP, approaching ARENA_BALANCE_RING_
       MAX_ARMOR_BONUS as hp -> 0. Computed live here, not cached in item_bonus_armor at purchase
       time, same reasoning the Zagan mirror/King Wealth aura above are also computed live --
       this needs to change every tick as hp changes, a plain cached sum can't do that. */
    if (h->max_hp > 0) {
        for (int s = 0; s < ARENA_ITEM_SLOT_COUNT; s++) {
            if (h->equipped_item[s] == ARENA_BALANCE_RING_ITEM_ID) {
                float missing_frac = 1.0f - (float)h->hp / (float)h->max_hp;
                if (missing_frac < 0.0f) missing_frac = 0.0f;
                total += (float)ARENA_BALANCE_RING_MAX_ARMOR_BONUS * missing_frac;
                break;
            }
        }
    }
    return total;
}

/* arena_hero_r_zone_radius: see arena_game.h's own doc comment. Doc Wheel's R (a real
 * ARENA_DOC_WHEEL_R_RADIUS-sized burst, ARENA_HERO_DOC_WHEEL) is deliberately NOT included here
 * -- it's a one-shot heal-and-cleanse applied instantly at cast time with no persisting
 * r_zone_x/z/r_active_ms state to render a lingering circle from (see its own cast-site comment
 * above), unlike every hero below whose kit genuinely lingers on the ground for r_active_ms. Name
 * kept as "r_zone" for every hero here even though Gunnr's Consecration (2026-07-30) is cast from
 * W, not R -- the underlying field/dispatch shape is generic ("this hero has an active ground
 * zone"), not actually R-specific, and renaming the whole shared mechanism for one hero's slot
 * choice isn't worth the churn. */
float arena_hero_r_zone_radius(ArenaHeroID hero_id) {
    switch (hero_id) {
        case ARENA_HERO_GHOST:     return ARENA_GHOST_R_RADIUS;
        case ARENA_HERO_GUNNR:     return ARENA_GUNNR_W_RADIUS; /* 2026-07-30: Consecration, cast from W not R -- see that constant's own doc comment */
        case ARENA_HERO_FLAMEL:    return ARENA_FLAMEL_R_RADIUS;
        case ARENA_HERO_MORRIGAN:  return ARENA_MORRIGAN_R_RADIUS;
        case ARENA_HERO_PAIMON:    return ARENA_PAIMON_R_RADIUS;
        case ARENA_HERO_NOOR1:     return ARENA_NOOR1_R_RADIUS;
        case ARENA_HERO_VASSAGO:   return ARENA_VASSAGO_R_RADIUS;
        case ARENA_HERO_HE_XIANGU: return ARENA_HE_XIANGU_R_RADIUS;
        case ARENA_HERO_BELETH:    return ARENA_BELETH_R_RADIUS;
        default:                   return 0.0f;
    }
}

/* arena_hero_bonus_ad (S170-190): item_bonus_ad's own "add this at every damage call site"
 * shape, extended with the Berserker powerup's flat bonus -- same reasoning arena_hero_armor
 * splits base-vs-item, here it's item-vs-powerup, both additive on top of the same flat
 * ARENA_ATTACK_DAMAGE/ARENA_GARY_ATTACK_DAMAGE base every call site already uses. */
static int arena_hero_bonus_ad(const ArenaHero *h) {
    /* South/Growth's Bloodroar (Jungle Camps Milestone 2): flat AD per stack, same "flat, not
       multiplier" shape Berserker already uses -- see king_growth_stacks's own doc comment. */
    return h->item_bonus_ad + (h->berserker_ms > 0 ? ARENA_BERSERKER_BONUS_AD : 0)
         + h->king_growth_stacks * ARENA_KING_GROWTH_AD_PER_STACK;
}

static int apply_armor(int raw_damage, float armor) {
    int dmg = raw_damage - (int)armor;
    return dmg < 1 ? 1 : dmg;
}

/* apply_damage/apply_damage_ex are defined further down this file -- forward-declared here so
 * apply_weapon_skill_damage below can call it, same "not every caller comes after the real
 * definition" idiom hero_is_hittable's own forward declaration already uses in this file. */
static void apply_damage_ex(ArenaHero *target, int amount, ArenaHeroID source_hero_id);
static void apply_damage(ArenaHero *target, int amount);

/* arena_log_damage (S189-01): pushes one entry into the rolling damage-log ring buffer --
 * see ArenaDamageLogEntry's own doc comment in arena_game.h for the full design/scope
 * reasoning. Always succeeds (no failure mode -- oldest entry is simply overwritten once the
 * buffer wraps, the intended rolling-feed behavior, not a bug). */
static void arena_log_damage(ArenaHeroID target_hero_id, ArenaHeroID source_hero_id, int amount) {
    ArenaDamageLogEntry *e = &arena_state.damage_log[arena_state.damage_log_head];
    e->target_hero_id = target_hero_id;
    e->source_hero_id = source_hero_id;
    e->amount = amount;
    arena_state.damage_log_head = (arena_state.damage_log_head + 1) % ARENA_DAMAGE_LOG_CAPACITY;
    if (arena_state.damage_log_count < ARENA_DAMAGE_LOG_CAPACITY) arena_state.damage_log_count++;
}

/* resonance_combo (REDGARDEN_GUI_NORTHSTAR.md Milestone 2): a straight port of
 * GoblinFoxDragon/server/skillchain.go's own `combinationTable` -- same real (ws1, ws2) pairs,
 * same real tier, same real damage multiplier. Bidirectional pairs are listed explicitly, same
 * as the Go source (not derived/mirrored automatically), so this table can be diffed against
 * that one directly if either ever needs updating. Returns 0 (no chain) if the pair doesn't
 * combine; otherwise returns the tier (1/2/3) and writes the real multiplier to *out_mult. */
static int resonance_combo(ArenaResonance a1, ArenaResonance a2, float *out_mult) {
    typedef struct { ArenaResonance a1, a2; int tier; float mult; } ComboEntry;
    static const ComboEntry TABLE[] = {
        /* Tier 1: same-element closure */
        { ARENA_RESONANCE_LIQUEFACTION,  ARENA_RESONANCE_LIQUEFACTION,  1, ARENA_SKILLCHAIN_TIER1_MULT },
        { ARENA_RESONANCE_IMPACTION,     ARENA_RESONANCE_IMPACTION,     1, ARENA_SKILLCHAIN_TIER1_MULT },
        { ARENA_RESONANCE_DETONATION,    ARENA_RESONANCE_DETONATION,    1, ARENA_SKILLCHAIN_TIER1_MULT },
        { ARENA_RESONANCE_SCISSION,      ARENA_RESONANCE_SCISSION,      1, ARENA_SKILLCHAIN_TIER1_MULT },
        { ARENA_RESONANCE_REVERBERATION, ARENA_RESONANCE_REVERBERATION, 1, ARENA_SKILLCHAIN_TIER1_MULT },
        { ARENA_RESONANCE_INDURATION,    ARENA_RESONANCE_INDURATION,    1, ARENA_SKILLCHAIN_TIER1_MULT },
        { ARENA_RESONANCE_COMPRESSION,   ARENA_RESONANCE_COMPRESSION,   1, ARENA_SKILLCHAIN_TIER1_MULT },
        { ARENA_RESONANCE_TRANSFIXION,   ARENA_RESONANCE_TRANSFIXION,   1, ARENA_SKILLCHAIN_TIER1_MULT },
        /* Tier 2: cross-element closure (bidirectional) */
        { ARENA_RESONANCE_TRANSFIXION,   ARENA_RESONANCE_LIQUEFACTION,  2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_LIQUEFACTION,  ARENA_RESONANCE_TRANSFIXION,   2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_LIQUEFACTION,  ARENA_RESONANCE_IMPACTION,     2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_IMPACTION,     ARENA_RESONANCE_DETONATION,    2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_DETONATION,    ARENA_RESONANCE_IMPACTION,     2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_DETONATION,    ARENA_RESONANCE_REVERBERATION, 2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_REVERBERATION, ARENA_RESONANCE_DETONATION,    2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_SCISSION,      ARENA_RESONANCE_COMPRESSION,   2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_COMPRESSION,   ARENA_RESONANCE_SCISSION,      2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_SCISSION,      ARENA_RESONANCE_REVERBERATION, 2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_REVERBERATION, ARENA_RESONANCE_INDURATION,    2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_INDURATION,    ARENA_RESONANCE_REVERBERATION, 2, ARENA_SKILLCHAIN_TIER2_MULT },
        { ARENA_RESONANCE_INDURATION,    ARENA_RESONANCE_SCISSION,      2, ARENA_SKILLCHAIN_TIER2_MULT },
        /* Tier 3: compound closure (bidirectional) */
        { ARENA_RESONANCE_FUSION,        ARENA_RESONANCE_FRAGMENTATION, 3, ARENA_SKILLCHAIN_TIER3_MULT },
        { ARENA_RESONANCE_FRAGMENTATION, ARENA_RESONANCE_FUSION,        3, ARENA_SKILLCHAIN_TIER3_MULT },
        { ARENA_RESONANCE_GRAVITATION,   ARENA_RESONANCE_DISTORTION,    3, ARENA_SKILLCHAIN_TIER3_MULT },
        { ARENA_RESONANCE_DISTORTION,    ARENA_RESONANCE_GRAVITATION,   3, ARENA_SKILLCHAIN_TIER3_MULT },
    };
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++) {
        if (TABLE[i].a1 == a1 && TABLE[i].a2 == a2) {
            *out_mult = TABLE[i].mult;
            return TABLE[i].tier;
        }
    }
    return 0;
}

/* arena_skillchain_try (Milestone 2): checks whether `new_attrs` closes a chain against
 * `target`'s own pending resonance (set by whatever real weapon skill last landed on it, from
 * any source). Same "highest tier wins, first match within a tier" rule as
 * server/skillchain.Chain -- ported, not reinvented. Returns the tier (0 if no chain) and
 * writes the multiplier to *out_mult. Does NOT mutate target's pending state -- the caller
 * (apply_weapon_skill_damage) does that after deciding the damage, so a whiffed/out-of-range
 * cast (which never reaches this function) never disturbs an in-flight window. */
static int arena_skillchain_try(const ArenaHero *target, const ArenaResonance *new_attrs, int new_attr_count, float *out_mult) {
    if (target->sc_pending_attr_count == 0) return 0;
    if (target->sc_pending_age_ms > ARENA_SKILLCHAIN_WINDOW_MS) return 0;
    int best_tier = 0;
    float best_mult = 0.0f;
    for (int i = 0; i < target->sc_pending_attr_count; i++) {
        for (int j = 0; j < new_attr_count; j++) {
            float mult;
            int tier = resonance_combo(target->sc_pending_attrs[i], new_attrs[j], &mult);
            if (tier > best_tier) {
                best_tier = tier;
                best_mult = mult;
            }
        }
    }
    if (best_tier > 0) *out_mult = best_mult;
    return best_tier;
}

/* apply_weapon_skill_damage (Milestone 2): the one choke point every real weapon-skill cast
 * (warrior_cast_q/w/r today, future ported jobs' own kits later) routes through instead of a
 * bare apply_damage/apply_armor pair -- ordinary abilities, basic attacks, and DoTs never call
 * this, matching real FFXI where only weapon skills open/close/continue a chain. Applies the
 * base (armor-reduced) damage, checks/applies a real skillchain bonus against the target's
 * pending resonance, fires the distinct skillchain_flash_tier visual event on a real closure,
 * then always opens a fresh window with this cast's own attrs (closing a chain doesn't end it --
 * real FFXI lets the next weapon skill continue chaining off the one that just landed). */
static void apply_weapon_skill_damage(ArenaHero *caster, ArenaHero *target, int base_damage, const ArenaResonance *attrs, int attr_count) {
    (void)caster; /* not read yet -- kept in the signature since a real caster-side effect (e.g. TP/MP refund on a landed chain) is a plausible near-future use, not invented here */
    int dmg = apply_armor(base_damage, arena_hero_armor(target));
    float mult;
    int tier = arena_skillchain_try(target, attrs, attr_count, &mult);
    if (tier > 0) {
        dmg += (int)((float)dmg * mult);
        target->skillchain_flash_tier = tier;
    }
    apply_damage(target, dmg);
    int n = attr_count < ARENA_SC_MAX_ATTRS ? attr_count : ARENA_SC_MAX_ATTRS;
    for (int i = 0; i < n; i++) target->sc_pending_attrs[i] = attrs[i];
    target->sc_pending_attr_count = n;
    target->sc_pending_age_ms = 0;
}

/* tyler_clone_cascade_kill (S170-141): the literal OG "one dies, all die"
 * rule -- force-kills every hero entry sharing `link_owner`'s clone link
 * (link_owner itself, plus every is_clone entry whose clone_owner points at
 * it), no exceptions, even bypassing a linked entity's own survive_floor_ms
 * (that mechanic protects against a hit landing on IT, not against this
 * separate shared-fate rule). Clone slots free immediately on death (same
 * "no respawn queue" idiom lane creeps use); the real Tyler, if he's not
 * already the one who triggered this, still gets the normal
 * ARENA_HERO_RESPAWN_MS queued like any other hero death. */
static void tyler_clone_cascade_kill(int link_owner) {
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive) continue;
        int is_linked = (i == link_owner) || (h->is_clone && h->clone_owner == link_owner);
        if (!is_linked) continue;
        h->hp = 0;
        h->alive = 0;
        if (h->is_clone) {
            h->active = 0;
        } else {
            h->respawn_ms_remaining = ARENA_HERO_RESPAWN_MS;
        }
    }
}

/* arena_reward_owner (S170-188, real bug found while auditing Tyler's clone-kill attribution):
 * Tyler's puppet clones fight through the same generic melee loop any real hero uses (S170-141)
 * -- when a CLONE lands the killing blow, the raw owner index handed to last_attacked_by_owner/
 * record_assist_damage would be the clone's own slot, and apply_damage's reward payout would
 * credit Flow/XP/kills to that clone's own disposable ArenaHero fields, lost the moment the
 * slot gets reused on Tyler's next R cast -- never actually reaching Tyler, the real player
 * whose army secured the kill. Resolves a raw owner index to who should ACTUALLY get credit:
 * Tyler himself for any of his clones, unchanged for everyone else. Applied at the point damage
 * attribution is RECORDED (both last_attacked_by_owner and record_assist_damage's own callers),
 * not at reward-payout time, so every downstream reward path just works with zero clone-
 * awareness of its own. */
static int arena_reward_owner(int owner_index) {
    if (owner_index < 0 || owner_index >= ARENA_HEROES_ARRAY_SIZE) return owner_index;
    ArenaHero *h = &arena_state.heroes[owner_index];
    return h->is_clone ? h->clone_owner : owner_index;
}

/* record_assist_damage (S170-187, founder: "assists should gen flow"): tracks up to
 * ARENA_MAX_ASSIST_TRACK distinct recent attackers on `victim`, called from the exact same
 * melee/homing-shot damage sites last_attacked_by_owner already is. Refreshes an already-
 * tracked attacker's timer back to the full window on a repeat hit (real "still actively
 * fighting" credit, not a one-shot memory); LRU-evicts the attacker closest to expiring if a
 * new one lands while all slots are full. */
static void record_assist_damage(ArenaHero *victim, int attacker_owner) {
    for (int i = 0; i < ARENA_MAX_ASSIST_TRACK; i++) {
        if (victim->assist_owner[i] == attacker_owner) {
            victim->assist_ms[i] = ARENA_ASSIST_WINDOW_MS;
            return;
        }
    }
    for (int i = 0; i < ARENA_MAX_ASSIST_TRACK; i++) {
        if (victim->assist_owner[i] < 0) {
            victim->assist_owner[i] = attacker_owner;
            victim->assist_ms[i] = ARENA_ASSIST_WINDOW_MS;
            return;
        }
    }
    int evict = 0;
    for (int i = 1; i < ARENA_MAX_ASSIST_TRACK; i++) {
        if (victim->assist_ms[i] < victim->assist_ms[evict]) evict = i;
    }
    victim->assist_owner[evict] = attacker_owner;
    victim->assist_ms[evict] = ARENA_ASSIST_WINDOW_MS;
}

/* arena_multikill_fib: Nth term (n>=1) of 1, 2, 3, 5, 8, 13, 21, ... -- see
 * ARENA_MULTIKILL_WINDOW_MS's own doc comment in arena_game.h for why this exact,
 * conventionally-1-indexed Fibonacci sequence (not plain 0-indexed Fibonacci, which repeats its
 * own leading 1 and would make a Double kill only 1+1=2x instead of the founder's own specified
 * 1+2=3x) is the per-kill multiplier a multi-kill streak scales by. */
static int arena_multikill_fib(int n) {
    if (n <= 1) return 1;
    int a = 1, b = 2;
    for (int i = 2; i < n; i++) {
        int next = a + b;
        a = b;
        b = next;
    }
    return b;
}

/* apply_damage (S170-46): centralizes "subtract HP, clamp at 0, mark dead"
 * across every damage call site, so Pizza's R (a real damage floor, not a
 * simplified-away shield like Doc Wheel's) only needs one place to check
 * survive_floor_ms rather than duplicating the check at every site. Armor
 * is already applied by the caller via apply_armor -- this only handles the
 * HP-floor/death half. Also (S170-51 cont'd) the single choke point for
 * "this hero took damage this tick," which arena_tick_nodes reads to
 * interrupt a capture channel -- every damage source in this file already
 * routes through here, so this needed no new call sites of its own. */
static void apply_damage_ex(ArenaHero *target, int amount, ArenaHeroID source_hero_id) {
    target->damaged_this_tick = 1;
    target->combat_timer_ms = ARENA_COMBAT_TIMEOUT_MS; /* S170-148: any damage taken re-arms the "in combat" window, gating mana regen */
    target->hp -= amount;
    arena_log_damage(target->hero_id, source_hero_id, amount); /* S189-01 */
    if (target->hp <= 0) {
        if (target->survive_floor_ms > 0) {
            target->hp = 1;
        } else {
            target->hp = 0;
            target->alive = 0;
            target->respawn_ms_remaining = ARENA_HERO_RESPAWN_MS;
            target->deaths++;
            /* multikill_count/multikill_timer_ms (2026-07-29): dying always ends this hero's
               OWN streak, whether or not it also just fed someone else's -- see
               ARENA_MULTIKILL_WINDOW_MS's own doc comment in arena_game.h. */
            target->multikill_count = 0;
            target->multikill_timer_ms = 0;
            /* S170-175: hero-kill Flow/XP/kills bounty -- only ever set at
               the melee/homing-shot damage sites (resolve_combat, the
               team-mode melee loop, arena_tick_attack_targets's Gary
               branch), so a kill finished by an ability cast grants
               nothing this pass, same "not every damage source needs full
               reward wiring" precedent arena_zone_damage_creeps already
               set for creeps. -1 (never hit, or last hit was an ability)
               means no reward, same sentinel convention ArenaCreep's own
               last_attacked_by_owner already uses. */
            if (target->last_attacked_by_owner >= 0 && target->last_attacked_by_owner < ARENA_HEROES_ARRAY_SIZE) {
                ArenaHero *killer = &arena_state.heroes[target->last_attacked_by_owner];
                if (killer->active && killer != target) {
                    /* Multi-kill streak (2026-07-29, see ARENA_MULTIKILL_WINDOW_MS's own doc
                       comment for the full design/founder-quote trail): a kill within the
                       window of this killer's last one continues the streak; otherwise it
                       starts a fresh one at count 1. Each kill's own bounty scales by
                       arena_multikill_fib(streak count so far), so the cumulative total across
                       a streak is that sequence's running sum (Double=3x, Triple=6x,
                       Quadra=11x, Penta=19x a normal kill's worth). */
                    killer->multikill_count = (killer->multikill_timer_ms > 0) ? killer->multikill_count + 1 : 1;
                    killer->multikill_timer_ms = ARENA_MULTIKILL_WINDOW_MS;
                    int multikill_mult = arena_multikill_fib(killer->multikill_count);
                    killer->flow += ARENA_HERO_KILL_FLOW * multikill_mult;
                    killer->flow_earned += ARENA_HERO_KILL_FLOW * multikill_mult;
                    killer->xp += ARENA_HERO_KILL_XP * multikill_mult;
                    killer->kills++;
                    /* South/Growth's Bloodroar (Jungle Camps Milestone 2): "each takedown while
                       holding it adds a stack and refreshes the buff's duration" -- a hero kill
                       is exactly a real-MOBA "takedown," same word the northstar itself uses. */
                    if (killer->king_growth_ms > 0) {
                        killer->king_growth_stacks++;
                        killer->king_growth_ms = ARENA_KING_GROWTH_DURATION_MS;
                    }
                }
            }
            /* S170-187: assists -- anyone else who damaged this hero within the recent
               tracking window also gets a smaller bounty, same real-MOBA "team fight, not
               just last-hit" reward. Excludes whoever already got the full kill bounty above
               (last_attacked_by_owner) so the killer doesn't double-dip on their own kill. */
            for (int a = 0; a < ARENA_MAX_ASSIST_TRACK; a++) {
                int assister_owner = target->assist_owner[a];
                if (assister_owner < 0 || assister_owner >= ARENA_HEROES_ARRAY_SIZE) continue;
                if (target->assist_ms[a] <= 0) continue;
                if (assister_owner == target->last_attacked_by_owner) continue;
                ArenaHero *assister = &arena_state.heroes[assister_owner];
                if (!assister->active || assister == target) continue;
                assister->flow += ARENA_HERO_ASSIST_FLOW;
                assister->flow_earned += ARENA_HERO_ASSIST_FLOW;
                assister->xp += ARENA_HERO_ASSIST_XP;
            }
            /* S170-141: Tyler's real shared-fate death. Only pay the extra
               scan when the hero that just died is actually clone-linked
               (a clone itself, or a real Tyler who may have active clones
               out) -- every other hero's ordinary death in this 26-hero
               roster skips this entirely. */
            if (target->is_clone || target->hero_id == ARENA_HERO_TYLER) {
                int dead_index = (int)(target - arena_state.heroes);
                int link_owner = target->is_clone ? target->clone_owner : dead_index;
                tyler_clone_cascade_kill(link_owner);
            }
        }
    }
}

/* apply_damage: thin wrapper over apply_damage_ex with source_hero_id = ARENA_HERO_COUNT
 * (unattributed) -- every one of this file's ~50 existing call sites keeps calling this
 * unchanged, zero risk to any of them. See ArenaDamageLogEntry's own doc comment in
 * arena_game.h for the full attribution-scope reasoning. */
static void apply_damage(ArenaHero *target, int amount) {
    apply_damage_ex(target, amount, ARENA_HERO_COUNT);
}

/* arena_nearest_enemy: the nearest active, living hero on a different team
 * than `owner` -- generalizes what used to be a hardcoded "the other slot"
 * lookup (1v1-only) so the same cast functions work for both the 1v1 local
 * demo (where it trivially resolves to the one other hero) and team mode
 * (where it picks a real target out of up to 19 others). Returns NULL if
 * owner is out of range or nobody qualifies (e.g. owner's whole team is the
 * only one left, or owner itself isn't active).
 *
 * S170-141: bound widened from ARENA_MAX_HEROES to ARENA_HEROES_ARRAY_SIZE
 * so this ALSO sees Tyler's puppet clones -- both directions: a real enemy
 * hero can find and target a clone through this exact same lookup (no
 * separate clone-targeting path needed), and a clone itself (called with
 * its own puppet-range index as `owner`) can find an enemy to fight. This
 * is the one shared lookup every kit cast and the team-mode melee loop
 * already goes through, so widening it here is what makes clones "just
 * fight like a real hero" rather than needing a parallel combat system. */
/* hero_obscured_from (S202-10, Duck's Smoke Bomb): true if `target` is
 * standing inside an active smoke cloud that `viewer` is NOT also standing
 * inside -- the one concrete, honest "vision-blocking" mechanic this engine
 * can actually support with no real vision/LOS system anywhere (see
 * ARENA_DUCK_W_RADIUS's own doc comment). Symmetric per-cloud, not
 * per-caster: two heroes both inside the same cloud can still see/target
 * each other (real smoke works the same way -- it blocks the outside
 * looking in, not everyone inside from each other). Iterates every active
 * hero's own duck_smoke_ms rather than assuming a single Duck, so it stays
 * correct if team mode ever puts two Ducks on the field. */
static int hero_obscured_from(const ArenaHero *viewer, const ArenaHero *target) {
    if (!viewer || !target) return 0;
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        const ArenaHero *caster = &arena_state.heroes[i];
        if (!caster->active || caster->duck_smoke_ms <= 0) continue;
        float tdx = target->x - caster->duck_smoke_x, tdz = target->z - caster->duck_smoke_z;
        if (tdx * tdx + tdz * tdz > ARENA_DUCK_W_RADIUS * ARENA_DUCK_W_RADIUS) continue; /* target not in this cloud */
        float vdx = viewer->x - caster->duck_smoke_x, vdz = viewer->z - caster->duck_smoke_z;
        if (vdx * vdx + vdz * vdz > ARENA_DUCK_W_RADIUS * ARENA_DUCK_W_RADIUS) return 1; /* viewer outside, target inside */
    }
    return 0;
}

ArenaHero *arena_nearest_enemy(int owner) {
    if (owner < 0 || owner >= ARENA_HEROES_ARRAY_SIZE) return NULL;
    ArenaHero *self = &arena_state.heroes[owner];
    if (!self->active) return NULL;
    ArenaHero *best = NULL;
    float best_dist = 0.0f;
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *cand = &arena_state.heroes[i];
        if (!cand->active || !cand->alive) continue;
        if (cand->team == self->team) continue;
        if (hero_obscured_from(self, cand)) continue; /* Smoke Bomb: can't target into a cloud from outside it */
        float dx = cand->x - self->x, dz = cand->z - self->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (!best || dist < best_dist) { best = cand; best_dist = dist; }
    }
    return best;
}

/* arena_nearest_ally: the nearest active, living hero on the SAME team as
 * `owner`, excluding owner itself. Mirrors arena_nearest_enemy exactly
 * (S170-45) -- the enabling primitive for every ally-targeted kit piece
 * previously skipped for having no target in 1v1. */
ArenaHero *arena_nearest_ally(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return NULL;
    ArenaHero *self = &arena_state.heroes[owner];
    if (!self->active) return NULL;
    ArenaHero *best = NULL;
    float best_dist = 0.0f;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        if (i == owner) continue;
        ArenaHero *cand = &arena_state.heroes[i];
        if (!cand->active || !cand->alive) continue;
        if (cand->team != self->team) continue;
        float dx = cand->x - self->x, dz = cand->z - self->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (!best || dist < best_dist) { best = cand; best_dist = dist; }
    }
    return best;
}

/* arena_set_hover_target (S170-143): see header doc comment. */
void arena_set_hover_target(int owner, int target) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    arena_state.hover_target[owner] = target;
}

/* arena_set_ground_target (S202-34): see header doc comment. */
void arena_set_ground_target(int owner, int has_target, float x, float z) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    arena_state.has_ground_target[owner] = has_target;
    arena_state.ground_target_x[owner] = x;
    arena_state.ground_target_z[owner] = z;
}

/* arena_hover_ally_or_nearest (S170-143): see header doc comment. */
ArenaHero *arena_hover_ally_or_nearest(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return arena_nearest_ally(owner);
    int target = arena_state.hover_target[owner];
    if (target >= 0 && target < ARENA_HEROES_ARRAY_SIZE && target != owner) {
        ArenaHero *cand = &arena_state.heroes[target];
        ArenaHero *self = &arena_state.heroes[owner];
        if (cand->active && cand->alive && self->active && cand->team == self->team) {
            return cand;
        }
    }
    return arena_nearest_ally(owner);
}

/* arena_tick_nodes (S170-46, capture mechanic redesigned S170-50): advances
 * every ArenaNode's Arathi Basin-style channel by dt_ms. One pass per node:
 * classify which team(s) have living presence in radius, apply Flamel's
 * Overgrowth mark refresh/decay, then either start/continue a channel
 * (exclusive single-team presence), interrupt one (mixed presence, a
 * Pizza's corruption, or the channeling team leaving), or leave an
 * already-settled node alone. Generalizes across 1v1 and team mode with no
 * special-casing, same as arena_nearest_ally/arena_nearest_enemy -- 1v1
 * already sets team=0/1 on its two hardcoded heroes. */
void arena_tick_nodes(unsigned int dt_ms) {
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        ArenaNode *node = &arena_state.nodes[n];
        int team_present[2] = {0, 0};
        int team_visible[2] = {0, 0}; /* present AND not currently stealthed (intangible_ms <= 0) */
        int team_damaged[2] = {0, 0}; /* any hero of this team, in radius, took damage this tick */
        int tree_on_team[2] = {0, 0};
        int pizza_in_radius = 0;
        int flamel_marker_team = -1;

        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->active || !h->alive) continue;
            float dx = h->x - node->x, dz = h->z - node->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_NODE_CAPTURE_RADIUS) continue;
            team_present[h->team] = 1;
            if (h->intangible_ms <= 0) team_visible[h->team] = 1;
            if (h->damaged_this_tick) team_damaged[h->team] = 1;
            if (h->hero_id == ARENA_HERO_TREE) tree_on_team[h->team] = 1;
            if (h->hero_id == ARENA_HERO_PIZZA) pizza_in_radius = 1;
            if (h->hero_id == ARENA_HERO_FLAMEL) flamel_marker_team = h->team;
        }

        if (flamel_marker_team >= 0) {
            node->marked_by_team = flamel_marker_team;
            node->mark_ms_remaining = ARENA_FLAMEL_MARK_MS;
        } else if (node->mark_ms_remaining > 0) {
            node->mark_ms_remaining -= (int)dt_ms;
            if (node->mark_ms_remaining <= 0) {
                node->mark_ms_remaining = 0;
                node->marked_by_team = -1;
            }
        }

        /* Exactly one team present (and Pizza isn't corrupting the attempt
           regardless of side) is the only condition that can start or
           continue a channel -- mixed presence, empty presence, or Pizza
           in radius all interrupt whatever's in progress.

           The stealth exception (S170-51 cont'd -- "a stealthed character
           sneaking in and solo-capping an objective while clueless
           opponents run around nearby," the archetypal WoW Arathi Basin
           moment): if a team's ENTIRE presence at this node is stealthed
           (intangible_ms > 0 -- Frog's R, which the doc itself describes as
           "vanishes... can't be targeted or seen"), the other team's
           presence, however visible, never even registers a contest --
           they don't know there's anything there to fight. A lone
           stealthed capper channels straight through a crowd of unaware
           enemies standing right on top of the node. This only ever lets
           ONE side capture undetected at a time: if both sides happen to be
           entirely stealthed simultaneously, or both are visible, the
           normal exclusive-presence rule below still applies unchanged. */
        int exclusive_team = -1;
        if (!pizza_in_radius) {
            if (team_present[0] != team_present[1]) {
                exclusive_team = team_present[0] ? 0 : 1;
            } else if (team_present[0] && team_present[1]) {
                int stealthed_only_0 = !team_visible[0];
                int stealthed_only_1 = !team_visible[1];
                if (stealthed_only_0 && !stealthed_only_1) exclusive_team = 0;
                else if (stealthed_only_1 && !stealthed_only_0) exclusive_team = 1;
            }
        }

        /* 2026-07-30, founder: "add towers around the nodes so beginning of game is a little
           slower" -- a living tower blocks capture outright, the same way mixed/corrupted
           presence does below, regardless of how exclusive the presence actually is. This is
           the entire mechanism that makes the early game slower: nobody can even START a
           channel on this node until its tower is destroyed. */
        if (arena_state.towers[n].alive) exclusive_team = -1;

        /* Damage interrupts the capture, same trigger as real WoW Arathi
           Basin's flag channel (S170-51 cont'd) -- checked before anything
           else so a hero who got hit this tick can't also make progress
           this same tick. */
        if (exclusive_team < 0 || team_damaged[exclusive_team]) {
            /* Interrupted (nothing was happening, mixed/corrupted presence,
               or the channeling team took damage) -- owner is left exactly
               as-is. A defender who denies an attacker doesn't get the node
               handed back for free; they still have to start their own
               channel, same as a would-be attacker who gets chased off. */
            node->capturing_team = -1;
            node->capture_progress_ms = 0;
            continue;
        }

        if (node->owner == exclusive_team + 1) {
            /* Already theirs -- nothing to capture, no channel to run.
               (+1: owner encodes 0=neutral/1=team0/2=team1, exclusive_team
               is the raw 0/1 team index -- comparing them directly would
               make a fresh neutral node (owner=0) collide with team index
               0, wrongly treated as "already owned by team 0".) */
            node->capturing_team = -1;
            node->capture_progress_ms = 0;
            continue;
        }

        if (node->capturing_team != exclusive_team) {
            /* A channel is starting (fresh, or switching from whichever
               team had been channeling) -- the node flips to neutral
               immediately, the "neutral period... as you wait for it to
               finish capturing" the channel spends open and uncaptured,
               not just at the moment it completes. */
            node->capturing_team = exclusive_team;
            node->capture_progress_ms = 0;
            node->owner = 0;

            /* Interacting with the flag breaks stealth (S170-51 cont'd) --
               real Arathi Basin's own rule. The sneaking-in part of the
               archetypal moment is over the instant the channel actually
               starts; whether the enemy crowd standing right there reacts
               in time is now down to their own attention/positioning, not
               a standing invisibility loophole. Only breaks the stealth of
               heroes on the team that just started this channel, in this
               node's radius -- an ally elsewhere on the map keeps theirs. */
            for (int i = 0; i < ARENA_MAX_HEROES; i++) {
                ArenaHero *h = &arena_state.heroes[i];
                if (!h->active || !h->alive || h->team != exclusive_team || h->intangible_ms <= 0) continue;
                float dx = h->x - node->x, dz = h->z - node->z;
                if (sqrtf(dx * dx + dz * dz) > ARENA_NODE_CAPTURE_RADIUS) continue;
                h->intangible_ms = 0;
            }
        }

        int progress = (int)dt_ms;
        if (tree_on_team[exclusive_team]) {
            progress = (int)((float)progress * ARENA_TREE_CHANNEL_SPEED_MULT);
        }
        if (node->marked_by_team == exclusive_team) {
            progress += ARENA_FLAMEL_MARK_CHANNEL_BONUS_MS;
        }
        node->capture_progress_ms += progress;

        if (node->capture_progress_ms >= ARENA_NODE_CAPTURE_CHANNEL_MS) {
            node->owner = exclusive_team + 1; /* encode team index -> owner (1=team0, 2=team1) */
            node->capturing_team = -1;
            node->capture_progress_ms = 0;
        }
    }

    /* damaged_this_tick is a single-tick flag -- cleared here, once, after
       every node has had a chance to read it this tick, not inside the
       per-node loop above (heroes are shared across nodes; clearing mid-
       loop would make node[1]'s check miss damage node[0]'s check already
       correctly saw). */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        arena_state.heroes[i].damaged_this_tick = 0;
    }
}

/* arena_fountain_position (S170-147): see header doc comment. Diagonally opposite corners, well
 * clear of every jungle obstacle's own outer-edge dressing and always within the hero movement
 * clamp (ARENA_HALF_EXTENT), so a fountain is always reachable, never buried in terrain.
 *
 * S170-191 bugfix, found while re-checking everything against the new golden-ratio-scaled
 * ARENA_HALF_EXTENT: this used to be a hardcoded literal (-24,-24)/(24,24) that was ALREADY
 * stale even before this pass -- arena_graveyard_position's own comment flags it as "the
 * fountains' own now-stale literal 24 (their own 28-4)" from the S170-138 map widen (28->32)
 * that never got carried over here. Converted to a real formula (ARENA_HALF_EXTENT - 8.0f,
 * which reproduces the exact same 24.0 the old literal gave against the pre-S170-191 extent of
 * 32) so it can never drift out of sync with the map size again -- same "-4.0f margin" idiom
 * arena_graveyard_position already uses, just a deliberately larger margin so fountains and
 * graveyards sit at two genuinely different rings, not stacked (graveyard_position's own
 * comment explains why: a respawning hero shouldn't land on top of the neutral, contested
 * fountain fight). */
void arena_fountain_position(int index, float *x, float *z) {
    float corner = ARENA_HALF_EXTENT - 8.0f;
    if (index < 0) index = 0;
    if (index >= ARENA_FOUNTAIN_COUNT) index = ARENA_FOUNTAIN_COUNT - 1;
    *x = (index == 0) ? -corner : corner;
    *z = (index == 0) ? -corner : corner;
}

/* arena_powerups_reset_layout (S170-190): see header declaration's doc comment. Positions
 * derived directly from arena_nodes_reset_layout's own layout table (Stables (-18,11), Farm
 * (-18,-11), Lumber Mill (18,11), Gold Mine (18,-11)) -- Berserker sits at the midpoint of the
 * two "top" nodes (0,11), Regen at the midpoint of the two "bottom" nodes (0,-11), both offset
 * from the center Blacksmith node (0,0) so they read as their own distinct ground. */
void arena_powerups_reset_layout(void) {
    /* S170-191: the node layout itself scaled by phi (arena_nodes_reset_layout's own doc
       comment) -- these midpoints scale the same 11.0f * 1.618034f the Stables/Farm/Lumber
       Mill/Gold Mine z-coordinates now use, so the powerups stay genuinely "between the node
       clusters" rather than drifting toward the center as the nodes spread further out. */
    arena_state.powerups[ARENA_POWERUP_BERSERKER].x = 0.0f;
    arena_state.powerups[ARENA_POWERUP_BERSERKER].z = 11.0f * 1.618034f;
    arena_state.powerups[ARENA_POWERUP_REGEN].x = 0.0f;
    arena_state.powerups[ARENA_POWERUP_REGEN].z = -11.0f * 1.618034f;
    for (int p = 0; p < ARENA_POWERUP_COUNT; p++) {
        arena_state.powerups[p].kind = (ArenaPowerupKind)p;
        arena_state.powerups[p].active = 1;
        arena_state.powerups[p].respawn_ms_remaining = 0;
    }
}

/* arena_graveyard_position (S170-153, corner placement S170-156): founder,
 * real-time: "the graveyards ... [should be] behind 2 of the corners not
 * in the middle of the map." The original placement sat each team's
 * graveyard right behind its spawn line's own center (x=+-9, z=0) -- dead
 * center along z, not remotely corner-like. Moved to the map's two
 * corners NOT already claimed by arena_fountain_position (a smaller margin
 * from the true edge than this function's own -- see that function's own
 * doc comment for the exact numbers and the S170-191 bugfix that made both
 * margins real formulas instead of one drifting hardcoded literal), so a
 * respawning hero never lands on top of the neutral, actively-contested
 * fountain fight. Team 0 (the -x side) gets the top-left corner, team 1
 * the bottom-right -- still diagonally opposite each other, same symmetry
 * as before. */
void arena_graveyard_position(int team, float *x, float *z) {
    float corner = ARENA_HALF_EXTENT - 4.0f;
    *x = (team == 0) ? -corner : corner;
    *z = (team == 0) ? corner : -corner;
}

/* arena_shop_position (S170-175): see header declaration's doc comment.
 * Offset a fixed distance from the team's own graveyard, along the same
 * diagonal that corner already sits on, so the shop reads as a second,
 * distinct structure in that corner rather than exactly overlapping the
 * graveyard's own point. */
void arena_shop_position(int team, float *x, float *z) {
    float gx, gz;
    arena_graveyard_position(team, &gx, &gz);
    float offset = (team == 0) ? -5.0f : 5.0f;
    *x = gx + offset;
    *z = gz - offset;
}

/* arena_recompute_item_stats (S170-175): see header declaration's doc
 * comment. */
void arena_recompute_item_stats(ArenaHero *h) {
    int bonus_hp = 0, bonus_mp = 0, bonus_armor = 0, bonus_ad = 0, bonus_cdr = 0;
    int bonus_true_dmg = 0, bonus_lifesteal = 0, bonus_range_pct = 0, bonus_mp_regen_combat = 0;
    float bonus_speed = 0.0f;
    for (int s = 0; s < ARENA_ITEM_SLOT_COUNT; s++) {
        int item_id = h->equipped_item[s];
        if (item_id < 0 || item_id >= ARENA_ITEM_COUNT) continue;
        const ArenaItemDef *def = &ARENA_ITEMS[item_id];
        bonus_hp += def->bonus_max_hp;
        bonus_mp += def->bonus_max_mp;
        bonus_armor += def->bonus_armor;
        bonus_ad += def->bonus_ad;
        bonus_speed += def->bonus_move_speed;
        bonus_cdr += def->bonus_cdr_pct; /* S170-207 */
        bonus_true_dmg += def->bonus_true_dmg; /* 2026-08-11 */
        bonus_lifesteal += def->bonus_lifesteal_pct; /* 2026-08-11 */
        bonus_range_pct += def->bonus_attack_range_pct; /* S202-34, Kite String */
        bonus_mp_regen_combat += def->bonus_mp_regen_combat; /* S205-87, Luck of the Draw */
    }

    int old_max_hp = h->max_hp;
    h->max_hp = 100 + bonus_hp; /* 100 matches every hero's flat base HP everywhere else in this file */
    h->hp += (h->max_hp - old_max_hp); /* buying/selling an HP item tops up/pulls down by the delta, a real change, not a silent cap adjustment */
    if (h->hp > h->max_hp) h->hp = h->max_hp;
    if (h->alive && h->hp < 1) h->hp = 1; /* a stat recompute alone should never be what kills someone */

    int old_max_mp = h->max_mp;
    h->max_mp = ARENA_MP_MAX + bonus_mp;
    h->mp += (h->max_mp - old_max_mp);
    if (h->mp > h->max_mp) h->mp = h->max_mp;
    if (h->mp < 0) h->mp = 0;

    h->item_bonus_armor = bonus_armor;
    h->item_bonus_ad = bonus_ad;
    h->item_bonus_move_speed = bonus_speed;
    h->item_bonus_cdr_pct = bonus_cdr; /* S170-207 */
    h->item_bonus_true_dmg = bonus_true_dmg; /* 2026-08-11 */
    h->item_bonus_lifesteal_pct = bonus_lifesteal; /* 2026-08-11 */
    h->item_bonus_attack_range_pct = bonus_range_pct; /* S202-34, Kite String */
    h->item_bonus_mp_regen_combat = bonus_mp_regen_combat; /* S205-87, Luck of the Draw */
}

/* arena_shop_buy (S170-175): see header declaration's doc comment. */
int arena_shop_buy(int owner, int item_id) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return 0;
    if (item_id < 0 || item_id >= ARENA_ITEM_COUNT) return 0;
    ArenaHero *h = &arena_state.heroes[owner];
    if (!h->active || !h->alive) return 0;

    float shop_x, shop_z;
    arena_shop_position(h->team, &shop_x, &shop_z);
    float dx = h->x - shop_x, dz = h->z - shop_z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_SHOP_RADIUS) return 0;

    const ArenaItemDef *def = &ARENA_ITEMS[item_id];
    int cost = def->cost;
    /* "buying an item auto equips it... no bag" -- an occupied slot gets
       auto-sold first (same refund rate an explicit sell would give),
       netting the price difference in one action rather than requiring
       sell-then-buy as two separate player actions. */
    int existing = h->equipped_item[def->slot];
    if (existing >= 0 && existing < ARENA_ITEM_COUNT) {
        h->flow += (ARENA_ITEMS[existing].cost * ARENA_ITEM_SELL_REFUND_PCT) / 100;
    }
    if (h->flow < cost) return 0; /* checked AFTER the auto-sell credit, same "upgrade" affordability real MOBA shops give */

    h->flow -= cost;
    h->flow_earned += 0; /* spending never reduces flow_earned -- see that field's own doc comment; explicit no-op line so this isn't silently forgotten by a future editor */
    h->equipped_item[def->slot] = item_id;
    arena_recompute_item_stats(h);
    return 1;
}

/* arena_shop_sell (S170-175): see header declaration's doc comment. */
int arena_shop_sell(int owner, ArenaItemSlot slot) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return 0;
    if (slot < 0 || slot >= ARENA_ITEM_SLOT_COUNT) return 0;
    ArenaHero *h = &arena_state.heroes[owner];
    if (!h->active || !h->alive) return 0;

    float shop_x, shop_z;
    arena_shop_position(h->team, &shop_x, &shop_z);
    float dx = h->x - shop_x, dz = h->z - shop_z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_SHOP_RADIUS) return 0;

    int item_id = h->equipped_item[slot];
    if (item_id < 0 || item_id >= ARENA_ITEM_COUNT) return 0; /* nothing there to sell */

    h->flow += (ARENA_ITEMS[item_id].cost * ARENA_ITEM_SELL_REFUND_PCT) / 100;
    h->equipped_item[slot] = -1;
    arena_recompute_item_stats(h);
    return 1;
}

/* redgarden_host_buy_build_item: see header declaration's own doc comment. Thin wrapper --
   arena_shop_buy already does everything (proximity, affordability, auto-sell-on-occupied-slot,
   stat recompute), this exists purely so the PARENA mod boundary has a stable name to call. */
int redgarden_host_buy_build_item(int hero_index, int item_id) {
    return arena_shop_buy(hero_index, item_id);
}

/* arena_hero_apply_build_template: see header declaration's own doc comment. */
int arena_hero_apply_build_template(int owner, int template_id) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return 0;
    if (template_id < 0 || template_id >= ARENA_BUILD_TEMPLATE_COUNT) return 0;
    ArenaHero *h = &arena_state.heroes[owner];
    if (!h->active || !h->alive) return 0;

    const ArenaBuildTemplate *tmpl = &ARENA_BUILD_TEMPLATES[template_id];
    int bought = 0;
    for (int i = 0; i < tmpl->item_count; i++) {
        int item_id = tmpl->item_ids[i];
        if (item_id < 0 || item_id >= ARENA_ITEM_COUNT) continue;
        /* Idempotent re-click: skip an item this hero already has equipped in that slot rather
           than re-buying it (which arena_shop_buy would otherwise happily do, auto-selling and
           re-buying the identical item for a net loss via the sell-refund gap). */
        if (h->equipped_item[ARENA_ITEMS[item_id].slot] == item_id) continue;
        if (!on_apply_build_template_item(owner, item_id)) break; /* first unaffordable item stops the sequence -- partial progress, not all-or-nothing */
        bought++;
    }
    return bought;
}

/* arena_use_blink (S170-205): see header declaration's doc comment. Direction-derivation
 * (move target, else nearest foe, else no-op) is deliberately the exact same fallback chain
 * unicorn_cast_q already established for its own dash -- one convention for "which way does an
 * instant reposition go" across this file, not a second one invented just for this item. */
void arena_use_blink(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    if (!h->active || !h->alive || h->stunned_ms > 0) return; /* not blocked by silenced_ms -- using an item isn't a cast, see header doc comment */
    if (h->equipped_item[ARENA_ITEM_SLOT_TRINKET] != ARENA_BLINK_DAGGER_ITEM_ID) return;
    if (h->blink_cooldown_ms > 0) return;

    float dx, dz;
    if (h->moving) {
        dx = h->target_x - h->x;
        dz = h->target_z - h->z;
    } else {
        ArenaHero *foe = arena_nearest_enemy(owner);
        if (!foe) return;
        dx = foe->x - h->x;
        dz = foe->z - h->z;
    }
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return; /* no meaningful direction, e.g. already standing on the target */
    dx /= len; dz /= len;

    /* Blink covers ARENA_BLINK_RANGE, or the remaining distance to an already-close move
       target/foe, whichever is shorter -- same "don't overshoot past what you were actually
       going toward" behavior a real click-to-move stop would give, just instant. */
    float travel = (len < ARENA_BLINK_RANGE) ? len : ARENA_BLINK_RANGE;
    float nx = h->x + dx * travel;
    float nz = h->z + dz * travel;
    if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
    if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
    if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
    if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
    h->x = nx;
    h->z = nz;
    resolve_hero_obstacle_collision(h);

    h->blink_cooldown_ms = ARENA_BLINK_COOLDOWN_MS;
}

/* arena_use_donkey_glide (S170-206): see header declaration's doc comment. Direction is away
 * from the nearest living enemy -- a real escape, not a general-purpose reposition like Blink
 * Dagger's own toward-target direction -- falling back to the current move target only if
 * there's no enemy nearby to escape from. */
void arena_use_donkey_glide(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    if (!h->active || !h->alive || h->stunned_ms > 0) return; /* not blocked by silenced_ms -- using an item isn't a cast */
    if (h->equipped_item[ARENA_ITEM_SLOT_BACK] != ARENA_DONKEY_ITEM_ID) return;
    if (h->donkey_glide_cooldown_ms > 0) return;

    float dx, dz;
    ArenaHero *foe = arena_nearest_enemy(owner);
    if (foe) {
        dx = h->x - foe->x; /* away from the foe, not toward it */
        dz = h->z - foe->z;
    } else if (h->moving) {
        dx = h->target_x - h->x;
        dz = h->target_z - h->z;
    } else {
        return; /* no enemy to flee, nowhere already headed -- nothing to glide toward */
    }
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return; /* standing exactly on top of the foe -- no meaningful escape direction */
    dx /= len; dz /= len;

    float nx = h->x + dx * ARENA_DONKEY_GLIDE_RANGE;
    float nz = h->z + dz * ARENA_DONKEY_GLIDE_RANGE;
    if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
    if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
    if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
    if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
    /* Unlike Blink Dagger, not an instant position swap -- a real move target the existing
       motion integration carries the hero toward at a boosted speed (update_hero_motion reads
       donkey_airborne_ms for both the speed multiplier and skipping obstacle collision, "flies
       over trees etc"), so the glide plays out over ARENA_DONKEY_GLIDE_DURATION_MS instead of
       teleporting on this exact tick. */
    h->target_x = nx;
    h->target_z = nz;
    h->moving = 1;
    h->donkey_airborne_ms = ARENA_DONKEY_GLIDE_DURATION_MS;
    h->intangible_ms = ARENA_DONKEY_GLIDE_DURATION_MS; /* untargetable while airborne -- reuses hero_is_hittable's existing gate */
    h->donkey_glide_cooldown_ms = ARENA_DONKEY_GLIDE_COOLDOWN_MS;
}

/* arena_use_active_item (S170-206): see header declaration's doc comment. */
void arena_use_active_item(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    if (h->equipped_item[ARENA_ITEM_SLOT_TRINKET] == ARENA_BLINK_DAGGER_ITEM_ID) {
        arena_use_blink(owner);
    } else if (h->equipped_item[ARENA_ITEM_SLOT_BACK] == ARENA_DONKEY_ITEM_ID) {
        arena_use_donkey_glide(owner);
    }
}

/* arena_tick_fountains (S170-147): see header declaration's doc comment. */
void arena_tick_fountains(unsigned int dt_ms) {
    arena_state.fountain_tick_ms += (int)dt_ms;
    while (arena_state.fountain_tick_ms >= 1000) {
        arena_state.fountain_tick_ms -= 1000;
        for (int f = 0; f < ARENA_FOUNTAIN_COUNT; f++) {
            float fx, fz;
            arena_fountain_position(f, &fx, &fz);
            for (int i = 0; i < ARENA_MAX_HEROES; i++) {
                ArenaHero *h = &arena_state.heroes[i];
                /* alive, not hero_is_hittable -- a heal check, not a damage
                   check. Intangibility (Ghost's Not a Ghost, Frog's vanish)
                   blocks being HIT, not being healed -- same "alive" gate
                   Ghost's/Paimon's own ally-heal zone ticks already use. */
                if (!h->active || !h->alive) continue;
                float dx = h->x - fx, dz = h->z - fz;
                if (sqrtf(dx * dx + dz * dz) > ARENA_FOUNTAIN_RADIUS) continue;
                h->hp += ARENA_FOUNTAIN_HEAL_PER_SEC;
                if (h->hp > h->max_hp) h->hp = h->max_hp;
                /* S170-148, founder: "fountains should also restore mana."
                   Unconditional, not gated by combat_timer_ms like passive
                   regen is -- a fountain is a deliberate, location-based
                   resource (real MOBA fountains work in or out of combat),
                   distinct from the passive out-of-combat-only regen tick
                   above it. */
                h->mp += ARENA_FOUNTAIN_MANA_PER_SEC;
                if (h->mp > h->max_mp) h->mp = h->max_mp;
            }
        }
    }
}

/* arena_tick_powerups (S170-190): see header declaration's doc comment. */
void arena_tick_powerups(unsigned int dt_ms) {
    for (int p = 0; p < ARENA_POWERUP_COUNT; p++) {
        ArenaPowerup *pu = &arena_state.powerups[p];
        if (!pu->active) {
            pu->respawn_ms_remaining -= (int)dt_ms;
            if (pu->respawn_ms_remaining <= 0) pu->active = 1;
            continue;
        }
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->active || !h->alive) continue;
            float dx = h->x - pu->x, dz = h->z - pu->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_POWERUP_PICKUP_RADIUS) continue;
            if (pu->kind == ARENA_POWERUP_BERSERKER) h->berserker_ms = ARENA_POWERUP_BUFF_MS;
            else h->regen_ms = ARENA_POWERUP_BUFF_MS;
            pu->active = 0;
            pu->respawn_ms_remaining = ARENA_POWERUP_RESPAWN_MS;
            break; /* one hero claims it -- first found in owner order, same simple "first past the post" idiom every other pickup/target-selection loop in this file already uses */
        }
    }
}

/* cast_cooldown: applies the generic next_cast_refund buff (S170-45,
 * Frog's Borrowed Time) -- returns 0 and consumes the buff if it's set on
 * h, else returns normal_ms unchanged. Every Q/W/R cooldown-assignment
 * site in this file routes through this so any future ally-buff kit gets
 * the same refund semantics for free. */
/* apply_cdr (S170-207, Haste Trinket, founder: "add a haste trinket" -> "passive haste lowers cd
 * and auto attack cd make it a modest improvement 6%"): the actual %-reduction math, shared by
 * cast_cooldown (Q/W/R) below and the auto-attack cooldown assignment in
 * arena_tick_attack_windups -- one formula, not two copies. Deliberately does NOT touch
 * attack_windup_ms_remaining anywhere -- NORTHSTAR §17.1's own real-League note ("the windup
 * fraction... does not shrink as attack speed increases -- only the backswing fraction
 * compresses") means haste should compress the cooldown/backswing, not the windup itself; this
 * function is only ever called on the cooldown, never on a windup duration, so that's already
 * true by construction. */
static int apply_cdr(const ArenaHero *h, int normal_ms) {
    /* East/Music's Catchy Song (Jungle Camps Milestone 2): attack-speed half of the buff, same
       CDR path Haste Trinket items already flow through -- see king_music_carrier's own doc
       comment for the persistence mechanic. §25.3: ambient team-cohesion attack-speed bonus,
       same shape, decays toward 0 as this hero's team's synergy_tier rises. */
    int pct = h->item_bonus_cdr_pct + (h->king_music_carrier ? ARENA_KING_MUSIC_ATTACK_SPEED_PCT : 0)
            + arena_synergy_cdr_pct(h);
    int reduced = normal_ms - (normal_ms * pct) / 100;
    return reduced < 0 ? 0 : reduced;
}

static int cast_cooldown(ArenaHero *h, int normal_ms) {
    if (h->next_cast_refund) {
        h->next_cast_refund = 0;
        return 0;
    }
    return apply_cdr(h, normal_ms);
}

/* hero_is_hittable: The Ghost's W (S170-32) is the first ability in this
 * arena that needs a "can this hero currently be hit at all" concept,
 * distinct from just being alive -- used by auto-attacks and ability
 * damage alike so intangibility means the same thing everywhere. */
static int hero_is_hittable(const ArenaHero *h) {
    /* NULL-safe: arena_nearest_enemy (team mode) returns NULL when nobody
       qualifies (e.g. the last enemy died mid-tick) -- treat "no target" the
       same as "not hittable" rather than crashing. */
    return h && h->alive && h->intangible_ms <= 0;
}

/* arena_tick_attack_targets (S170-162, team mode only): see header
 * declaration's doc comment. */
void arena_tick_attack_targets(unsigned int dt_ms) {
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive) continue;
        int target = h->attack_target;
        if (target < 0) continue;
        if (target >= ARENA_MAX_HEROES) { h->attack_target = -1; continue; }
        ArenaHero *foe = &arena_state.heroes[target];
        if (!foe->active || !foe->alive || !hero_is_hittable(foe) || foe->team == h->team) {
            h->attack_target = -1;
            continue;
        }

        float range = arena_hero_attack_range(h);
        float dx = foe->x - h->x, dz = foe->z - h->z;
        float dist = sqrtf(dx * dx + dz * dz);

        if (dist > range) {
            /* Held units never chase (real WC3 "Hold Position," §24 Milestone 2, 2026-07-31) --
               drop the lock instead of pursuing, so arena_tick_attack_move's own scan picks up
               whoever's actually in range next tick (possibly the same foe wandering back,
               possibly someone else) rather than staying stuck on a target that's left. */
            if (h->hold_position) {
                h->attack_target = -1;
                continue;
            }
            /* Pure pursuit -- chase the target's LIVE position every tick,
               not a stored waypoint, so a target that keeps moving is
               chased continuously rather than toward a single stale point.
               Overrides whatever move target was previously set, same
               "the attack command wins while it's active" precedence
               real League gives an active attack-target lock. */
            arena_state.heroes[i].target_x = foe->x;
            arena_state.heroes[i].target_z = foe->z;
            arena_state.heroes[i].moving = 1;
            continue;
        }

        /* In range. Melee heroes: intentionally do nothing further here --
           their real damage still comes from the existing proximity-based
           combat loops (resolve_combat / the team-mode melee loop), chase
           above just gets them close enough for those to naturally fire,
           unchanged. Gary: begins his own windup here instead of ever
           falling through to the flat melee tick (he's excluded from that
           loop entirely, see the team-mode melee loop's own comment) --
           arena_tick_attack_windups (called after this function) fires the
           actual homing shot once it completes, same S170-204 shape the
           flat melee loop now uses. */
        /* Real, live bug found and fixed 2026-08-26 (founder: "also his auto attack is broken"
           -> "abraham"): S202-34 excluded Abraham from the old flat melee loop everywhere
           (arena_hero_attack_creeps/lane_creeps/towers/kings/camp_minions, the team-mode melee
           loop -- every one of those already has its own "same as Gary" comment), but this,
           the actual windup-START trigger for the NEW ranged path, stayed hardcoded to
           `ARENA_HERO_GARY` alone -- Abraham fell into neither loop and simply never began an
           auto-attack windup at all, ever. */
        if (h->hero_id == ARENA_HERO_GARY || h->hero_id == ARENA_HERO_ABRAHAM) {
            if (h->attack_cooldown_ms <= 0 && h->stunned_ms <= 0 && h->attack_windup_ms_remaining <= 0) { /* S170-184/S170-204 */
                h->attack_windup_ms_remaining = (h->hero_id == ARENA_HERO_GARY)
                    ? ARENA_GARY_ATTACK_WINDUP_MS : ARENA_ABRAHAM_ATTACK_WINDUP_MS;
            }
        }
    }
    (void)dt_ms; /* attack_cooldown_ms is ticked in the team-mode melee loop already; this only spends it */
}

/* arena_tick_attack_windups (S170-204, NORTHSTAR §17.1's "does the champion stop when
 * auto-attacking? yes" -- the flat melee loop and arena_tick_attack_targets's Gary branch
 * above both begin a windup instead of dealing damage/firing instantly now; this is where it
 * actually resolves. Stun landing mid-windup interrupts it (same as it already blocks starting
 * a new one) -- movement-lock CC canceling an in-progress swing is the natural analog to
 * Gary's Aimed Shot silence-interrupt (S170-203). A genuinely new move command interrupts it
 * too, but that's handled in arena_set_move_target itself, not here (the windup is already
 * cleared to 0 by the time this runs, so there's nothing left for this function to do for that
 * case). The target is re-validated HERE, at completion, not continuously during the windup --
 * a target that stepped out of range or became unhittable while the caster stood still still
 * costs the swing (cooldown spent either way, once windup completes -- the swing physically
 * happened, whether or not it connected), same "real commitment" shape every other timed
 * ability in this file already holds itself to. */
static void arena_tick_attack_windups(unsigned int dt_ms) {
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive) continue;
        if (h->attack_windup_ms_remaining <= 0) continue;
        if (h->stunned_ms > 0) {
            h->attack_windup_ms_remaining = 0; /* stun interrupts -- no damage, no cooldown spent */
            continue;
        }
        h->attack_windup_ms_remaining -= (int)dt_ms;
        if (h->attack_windup_ms_remaining > 0) continue;
        h->attack_windup_ms_remaining = 0;

        if (h->hero_id == ARENA_HERO_GARY || h->hero_id == ARENA_HERO_ABRAHAM) {
            /* S202-34: Abraham's basic auto-attack is now ranged/homing too, same mechanic
               Gary's own established (ArenaProjectile.homing_target) -- own named constants
               (currently aliased to Gary's own values, see arena_game.h) rather than hardcoding
               Gary's macros for both, so either hero's numbers can diverge later without
               touching this shared branch again. hero_id passed through as h->hero_id (not a
               hardcoded ARENA_HERO_GARY) so the client's existing per-hero_id color convention
               picks the right shot color for whichever of the two actually fired. */
            int target = h->attack_target;
            float atk_range = (h->hero_id == ARENA_HERO_GARY) ? ARENA_GARY_ATTACK_RANGE : ARENA_ABRAHAM_ATTACK_RANGE;
            float atk_speed = (h->hero_id == ARENA_HERO_GARY) ? ARENA_GARY_ATTACK_SPEED : ARENA_ABRAHAM_ATTACK_SPEED;
            int atk_damage = (h->hero_id == ARENA_HERO_GARY) ? ARENA_GARY_ATTACK_DAMAGE : ARENA_ABRAHAM_ATTACK_DAMAGE;
            int atk_cooldown_ms = (h->hero_id == ARENA_HERO_GARY) ? ARENA_GARY_ATTACK_COOLDOWN_MS : ARENA_ABRAHAM_ATTACK_COOLDOWN_MS;
            if (target >= 0 && target < ARENA_MAX_HEROES) {
                ArenaHero *foe = &arena_state.heroes[target];
                if (hero_is_hittable(foe)) {
                    float dx = foe->x - h->x, dz = foe->z - h->z;
                    if (sqrtf(dx * dx + dz * dz) <= atk_range) {
                        ArenaProjectile *shot = arena_spawn_projectile(i, h->team, h->hero_id,
                            h->x, h->z, foe->x, foe->z, atk_speed, 0.6f,
                            atk_damage + arena_hero_bonus_ad(h), atk_range * 3.0f); /* S170-190 */
                        if (shot) shot->homing_target = target;
                    }
                }
            }
            h->attack_cooldown_ms = apply_cdr(h, atk_cooldown_ms); /* S170-207 */
        } else {
            ArenaHero *foe = arena_nearest_enemy(i);
            if (foe && hero_is_hittable(foe)) {
                float dx = foe->x - h->x, dz = foe->z - h->z;
                if (sqrtf(dx * dx + dz * dz) <= ARENA_ATTACK_RANGE) {
                    /* S170-175/S170-188: same kill-attribution shape the old instant-damage
                       melee loop always used, unchanged. */
                    int reward_owner = arena_reward_owner(i);
                    foe->last_attacked_by_owner = reward_owner;
                    record_assist_damage(foe, reward_owner); /* S170-187 */
                    /* Gae Bolg's true damage (2026-08-11): applied AFTER apply_armor, not
                       before -- armor-piercing, the first stat in this catalog the target's
                       armor does nothing to reduce. Masamune's lifesteal (2026-08-11): a percent
                       of this FINAL (post-armor, post-true-damage) number, matching real-MOBA
                       "lifesteal scales off damage actually dealt" convention, not raw attack
                       power -- same inline heal-and-clamp shape every other heal site in this
                       file already uses (no shared helper exists to reuse). */
                    int final_dmg = apply_armor(ARENA_ATTACK_DAMAGE + arena_hero_bonus_ad(h), arena_hero_armor(foe))
                                     + h->item_bonus_true_dmg;
                    apply_damage(foe, final_dmg); /* S170-190 */
                    if (h->item_bonus_lifesteal_pct > 0 && h->alive) {
                        h->hp += (final_dmg * h->item_bonus_lifesteal_pct) / 100;
                        if (h->hp > h->max_hp) h->hp = h->max_hp;
                    }
                }
            }
            h->attack_cooldown_ms = apply_cdr(h, ARENA_ATTACK_COOLDOWN_MS); /* S170-207 */
        }
    }
}

/* arena_spawn_projectile (S170-136, returns a pointer S170-140): see header doc comment. */
ArenaProjectile *arena_spawn_projectile(int owner, int team, ArenaHeroID hero_id,
                             float x, float z, float target_x, float target_z,
                             float speed, float radius, int damage, float max_range) {
    ArenaProjectile *p = NULL;
    for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
        if (!arena_state.projectiles[i].active) { p = &arena_state.projectiles[i]; break; }
    }
    if (!p) return NULL; /* pool exhausted -- generous headroom, should not happen in practice */

    float dx = target_x - x, dz = target_z - z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.0001f) { dx = 1.0f; dz = 0.0f; dist = 1.0f; } /* degenerate same-position cast: pick an arbitrary direction rather than a NaN velocity */

    p->active = 1;
    p->owner = owner;
    p->team = team;
    p->hero_id = hero_id;
    p->x = x;
    p->z = z;
    p->vx = (dx / dist) * speed;
    p->vz = (dz / dist) * speed;
    p->radius = radius;
    p->damage = damage;
    p->max_range = max_range;
    p->traveled = 0.0f;
    /* S170-140: reset every reused pool slot's on-hit effects -- a stale
       value left over from a PREVIOUS shot (e.g. Tyler's root+burn) must
       never leak onto a fresh plain-damage shot (e.g. Gary's Q) that just
       happened to land in the same recycled slot. */
    p->on_hit_silence_ms = 0;
    p->on_hit_root_ms = 0;
    p->on_hit_burn_ms = 0;
    p->on_hit_burn_dps = 0;
    p->homing_target = -1; /* S170-163: a stale homing lock from a previous shot recycled into this slot must never leak onto a fresh skill-shot */
    p->pierce = 0; /* S202-34: a stale pierce flag from a previous shot (Abraham's Fireball) must never leak onto a fresh single-hit shot recycled into this slot */
    p->pierced_mask = 0;
    return p;
}

/* arena_tick_projectiles (S170-136): see header doc comment. Enemy-ness is
 * determined by `team` (cached at spawn), not by looking the owner hero back
 * up -- correct even if the caster died or respawned into a different state
 * while the shot was still in flight. */
void arena_tick_projectiles(unsigned int dt_ms) {
    float dt_sec = (float)dt_ms / 1000.0f;
    for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
        ArenaProjectile *p = &arena_state.projectiles[i];
        if (!p->active) continue;

        /* S170-163: a homing shot re-aims at its target's LIVE position
           every tick instead of flying the fixed line the rest of this
           function assumes -- once vx/vz is refreshed here, the existing
           travel/collision code below runs completely unchanged and just
           naturally converges on (and registers a hit against) that exact
           hero. Fizzles (deactivates, no damage) the instant the target is
           no longer a valid hit -- dead, inactive, or otherwise unhittable
           -- rather than let an already-fired shot land on a target that's
           no longer really there. */
        if (p->homing_target >= 0) {
            ArenaHero *tracked = &arena_state.heroes[p->homing_target];
            if (!tracked->active || !hero_is_hittable(tracked)) {
                p->active = 0;
                continue;
            }
            float dx = tracked->x - p->x, dz = tracked->z - p->z;
            float dist = sqrtf(dx * dx + dz * dz);
            float speed = sqrtf(p->vx * p->vx + p->vz * p->vz);
            if (dist > 0.0001f) {
                p->vx = (dx / dist) * speed;
                p->vz = (dz / dist) * speed;
            }
        }

        float old_x = p->x, old_z = p->z;
        float step = sqrtf(p->vx * p->vx + p->vz * p->vz) * dt_sec;
        p->x += p->vx * dt_sec;
        p->z += p->vz * dt_sec;
        p->traveled += step;

        /* S170-140 bugfix: collision is checked against the SEGMENT this
           tick's move traced out (old_x,old_z)->(p->x,p->z), not just the
           end-of-tick position. A large dt_ms (this codebase's own test
           helpers routinely call *_update(1000) for "one full second" test
           steps, and a real frame hitch would do the same) can otherwise
           let a fast shot's position jump clean past a foe standing in its
           path without ever registering a hit -- a real tunneling bug,
           found via test_ghost_r_zone_damages_foe_over_time going from
           reliably-passing to failing the instant Ghost's Q became a
           projectile fired inside a dt_ms=1000 arena_update() call. Reduces
           to the old end-position check when the segment is ~0 length
           (small dt_ms, the common real-frame case), so this is a strict
           correctness fix, not a behavior change for the normal case. */
        float seg_dx = p->x - old_x, seg_dz = p->z - old_z;
        float seg_len_sq = seg_dx * seg_dx + seg_dz * seg_dz;

        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *foe = &arena_state.heroes[h];
            if (!foe->active || foe->team == p->team) continue;
            if (!hero_is_hittable(foe)) continue;
            /* S202-34: a piercing shot (Abraham's Fireball) skips anyone it's
               already damaged, so a slow shot that's still geometrically
               overlapping a foe it just hit doesn't re-tick them every
               subsequent frame while they're both still in range. */
            if (p->pierce && (p->pierced_mask & (1u << h))) continue;

            float t = 0.0f;
            if (seg_len_sq > 0.0001f) {
                t = ((foe->x - old_x) * seg_dx + (foe->z - old_z) * seg_dz) / seg_len_sq;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
            }
            float closest_x = old_x + seg_dx * t;
            float closest_z = old_z + seg_dz * t;
            float dx = foe->x - closest_x, dz = foe->z - closest_z;
            if (sqrtf(dx * dx + dz * dz) > p->radius) continue;

            /* S170-175: only a HOMING shot (Gary's basic auto-attack) sets
               kill attribution -- an ordinary ability skill-shot (Tyler's
               Q, Ghost's Q, etc.) travels through this exact same hit code
               but deliberately does NOT grant Flow/XP/kill credit, same
               "ability kills grant nothing this pass" scope this file's
               other reward sites already hold to. */
            if (p->homing_target >= 0) { foe->last_attacked_by_owner = p->owner; record_assist_damage(foe, p->owner); } /* S170-187 */
            apply_damage(foe, apply_armor(p->damage, arena_hero_armor(foe)));
            if (p->on_hit_silence_ms > 0) foe->silenced_ms = p->on_hit_silence_ms;
            if (p->on_hit_root_ms > 0) foe->rooted_ms = p->on_hit_root_ms;
            if (p->on_hit_burn_ms > 0) {
                foe->burning_ms = p->on_hit_burn_ms;
                foe->burn_dps = p->on_hit_burn_dps;
            }
            if (p->pierce) {
                /* Keeps travelling -- mark this foe hit and check the
                   remaining heroes this same tick instead of stopping at
                   the first one, so a shot passing through a cluster of
                   enemies in one tick damages all of them, not just
                   whichever happened to be checked first. */
                p->pierced_mask |= (1u << h);
                continue;
            }
            p->active = 0;
            break;
        }
        if (p->active && p->traveled >= p->max_range) p->active = 0; /* whiffed */
    }
}

/* creep_spawn: (re)rolls flavor/HP from the creep's own node's CURRENT
 * owner -- the "jungle reacts to who controls the ground under it" half of
 * S170-51's design. Spawn POSITION (S170-161, founder: "initially they
 * spawn from the graveyards behind the nodes not the center") now depends
 * on that same flavor: a NEUTRAL/contested creep still spawns at its own
 * node's position, unchanged -- it has no team, nowhere else to come from.
 * A team-flavored creep instead spawns at its owning team's graveyard
 * (arena_graveyard_position) and marches out from there (see
 * arena_tick_creeps' march step below) -- the actual "spawn and fan out
 * from owned nodes" behavior, since a creep that always sat exactly on
 * the node it belongs to had nowhere to fan out FROM. */
static void creep_spawn(ArenaCreep *creep, const ArenaNode *node) {
    creep->flavor = (ArenaCreepFlavor)node->owner; /* owner 0/1/2 map directly onto the flavor enum */
    creep->max_hp = creep->hp = (creep->flavor == ARENA_CREEP_NEUTRAL) ? ARENA_CREEP_NEUTRAL_HP : ARENA_CREEP_TEAM_HP;
    if (creep->flavor == ARENA_CREEP_NEUTRAL) {
        creep->x = node->x;
        creep->z = node->z;
    } else {
        int owning_team = creep->flavor - 1; /* ARENA_CREEP_TEAM0=1/TEAM1=2 -> team 0/1 */
        arena_graveyard_position(owning_team, &creep->x, &creep->z);
    }
    creep->alive = 1;
    creep->attack_cooldown_ms = 0;
    creep->last_attacked_by_owner = -1;
}

/* arena_tick_creeps (S170-51): see the header declaration's doc comment. */
void arena_tick_creeps(unsigned int dt_ms) {
    for (int i = 0; i < ARENA_MAX_CREEPS; i++) {
        ArenaCreep *creep = &arena_state.creeps[i];
        ArenaNode *node = &arena_state.nodes[i];

        if (!creep->alive) {
            /* 2026-07-30: a node-guardian creep never spawns while its own node's tower still
               stands -- both sit at the exact same (x,z), and a hero's attack is a once-per-
               cooldown resource arena_hero_attack_creeps (which runs first every tick) always
               wins if the creep is alive, permanently starving the tower of any damage at all.
               Real bug behind the founder's own "towers are basically invincible" report. One
               guardian per phase: the tower is it until it falls, then the neutral creep resumes
               exactly as it always has (this check simply stops it from spawning early, it
               doesn't change anything about ITS OWN behavior once the tower is gone). */
            if (arena_state.towers[i].alive) continue;
            creep->respawn_ms_remaining -= (int)dt_ms;
            if (creep->respawn_ms_remaining <= 0) creep_spawn(creep, node);
            continue;
        }

        if (creep->attack_cooldown_ms > 0) creep->attack_cooldown_ms -= (int)dt_ms;

        /* S170-152 bugfix, founder: "capturing node should not make the
           user take damage." A team-flavored creep (ARENA_CREEP_TEAM0/1)
           previously attacked ANY hero in radius, including its own
           OWNING team -- and since ARENA_NODE_CAPTURE_RADIUS (5.0)
           comfortably overlaps ARENA_CREEP_AGGRO_RADIUS (4.0), any hero
           who stood still to channel-capture (or simply defend/hold)
           their own already-owned node got attacked by their own
           "home-turf resupply" creep, which makes no thematic sense --
           real home turf doesn't hurt you for standing on it. Fixed:
           a team-flavored creep now only ever targets the OPPOSING
           team, matching the counter-play framing its own kill-reward
           already carries ("farming an enemy's own node-guardian creep helps
           flip their node"). A NEUTRAL (contested) creep is unchanged --
           still attacks anyone regardless of team, the actual
           "fight through the prize" challenge that flavor is meant to be. */
        int owning_team = creep->flavor - 1; /* -1 for ARENA_CREEP_NEUTRAL (flavor 0); never read when flavor is neutral, see below */
        ArenaHero *target = NULL;
        float best_dist = 0.0f;
        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *cand = &arena_state.heroes[h];
            if (!cand->active || !hero_is_hittable(cand)) continue;
            if (creep->flavor != ARENA_CREEP_NEUTRAL && cand->team == owning_team) continue;
            float dx = cand->x - creep->x, dz = cand->z - creep->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > ARENA_CREEP_AGGRO_RADIUS) continue;
            if (!target || dist < best_dist) { target = cand; best_dist = dist; }
        }
        if (target && creep->attack_cooldown_ms <= 0) {
            /* S170-211: route through apply_armor like every hero-vs-hero damage source --
               node-guardian creeps used to deal flat, unmitigated damage, the one outlier
               NORTHSTAR §20.3 named as a likely real contributor to "too strong." */
            apply_damage(target, apply_armor((creep->flavor == ARENA_CREEP_NEUTRAL) ? ARENA_CREEP_NEUTRAL_DAMAGE : ARENA_CREEP_TEAM_DAMAGE, arena_hero_armor(target)));
            creep->attack_cooldown_ms = ARENA_CREEP_ATTACK_COOLDOWN_MS;
        }

        /* S170-161, founder: "have the team creeps spawn and fan out from
           owned nodes marching towards unowned nodes." A team-flavored
           creep continuously walks toward whichever node its own team
           doesn't currently own -- nearest one, recomputed fresh every
           tick (not a target locked in once at spawn), so it naturally
           redirects if that target node gets captured by its own team
           mid-march, or if a formerly-owned node gets lost and becomes a
           closer option instead. Each owned node's creep only ever knows
           about its own position and the current node layout -- no
           coordination between creeps -- but since they each independently
           pick their OWN nearest unowned node, the aggregate effect across
           several owned nodes reads as a real fan-out across the map.
           Idles in place (this branch simply finds nothing and does
           nothing) once its own team already owns every node. Neutral
           creeps never reach this branch at all -- no team, no home to
           push outward from, exactly the static camp they've always
           been. */
        if (creep->flavor != ARENA_CREEP_NEUTRAL) {
            ArenaNode *march_target = NULL;
            float march_best_dist = 0.0f;
            for (int n = 0; n < ARENA_NODE_COUNT; n++) {
                ArenaNode *candidate = &arena_state.nodes[n];
                if (candidate->owner == (int)creep->flavor) continue; /* already owned by this creep's own team */
                float dx = candidate->x - creep->x, dz = candidate->z - creep->z;
                float dist = dx * dx + dz * dz;
                if (!march_target || dist < march_best_dist) { march_target = candidate; march_best_dist = dist; }
            }
            if (march_target) {
                float dx = march_target->x - creep->x, dz = march_target->z - creep->z;
                float dist = sqrtf(dx * dx + dz * dz);
                if (dist > ARENA_CREEP_MARCH_STOP_EPSILON) {
                    float step = ARENA_CREEP_MARCH_SPEED * ((float)dt_ms / 1000.0f);
                    if (step > dist) step = dist;
                    creep->x += (dx / dist) * step;
                    creep->z += (dz / dist) * step;
                }
            }
        }
    }
}

/* creep_die: applies this creep's flavor-specific reward to whoever landed
 * the killing blow (tracked via last_attacked_by_owner -- the LAST hit
 * lands the kill in this arena's simple damage model, so "last attacker"
 * and "killer" are the same thing here), then queues its respawn. See the
 * ARENA_MAX_CREEPS header comment for the full reward design. */
static void creep_die(ArenaCreep *creep, ArenaNode *node) {
    creep->alive = 0;
    creep->respawn_ms_remaining = (creep->flavor == ARENA_CREEP_NEUTRAL)
        ? ARENA_CREEP_NEUTRAL_RESPAWN_MS : ARENA_CREEP_TEAM_RESPAWN_MS;

    if (creep->last_attacked_by_owner < 0) return;
    ArenaHero *killer = &arena_state.heroes[creep->last_attacked_by_owner];

    /* S170-175: Flow/XP applies to any node-guardian creep kill, neutral or
       team-flavored alike -- unlike the capture-bonus/heal rewards below,
       which stay flavor-specific. */
    killer->flow += ARENA_NODE_GUARDIAN_KILL_FLOW;
    killer->flow_earned += ARENA_NODE_GUARDIAN_KILL_FLOW;
    killer->xp += ARENA_NODE_GUARDIAN_KILL_XP;

    if (creep->flavor == ARENA_CREEP_NEUTRAL) {
        /* The contested prize: a big swing toward capturing THIS node,
           but only if the killer's team is actually the one channeling it
           right now -- the reward is for jungle-and-territory synergy, not
           an unconditional stat pad disconnected from what's happening at
           the flag itself. */
        if (node->capturing_team == killer->team) {
            node->capture_progress_ms += ARENA_CREEP_NEUTRAL_KILL_CAPTURE_BONUS_MS;
        }
        return;
    }

    /* Team-flavored creep: owner index 1/2 map back to team index 0/1. */
    int owning_team = creep->flavor - 1;
    if (killer->team == owning_team) {
        /* Home-turf resupply. */
        killer->hp += ARENA_CREEP_TEAM_KILL_HEAL;
        if (killer->hp > killer->max_hp) killer->hp = killer->max_hp;
    } else if (node->capturing_team == killer->team) {
        /* Counter-play: farming the enemy's own node-guardian creep helps flip
           their node, same "only if actually channeling it" gate as above. */
        node->capture_progress_ms += ARENA_CREEP_TEAM_KILL_DENY_CAPTURE_BONUS_MS;
    }
}

/* arena_hero_attack_creeps (S170-51): see the header declaration's doc
 * comment. */
void arena_hero_attack_creeps(unsigned int dt_ms) {
    (void)dt_ms; /* attack_cooldown_ms is ticked in tick_hero_kit/resolve_combat already; this only spends it */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive || h->attack_cooldown_ms > 0 || h->stunned_ms > 0) continue; /* S170-184 */
        if (h->mnm_burrow_ms > 0) continue; /* S170-208: burrowed, not present to swing at anything */
        /* S170-163: Gary's basic attack is exclusively his ranged homing
           shot (arena_tick_attack_targets) -- excluded here too, same
           reasoning as the hero-vs-hero melee loop, so he can't
           incidentally flat-melee a node-guardian creep he happens to be standing
           next to and burn his shared attack_cooldown_ms on it. Real,
           scoped gap: Gary can't auto-attack node-guardian creeps at all until a
           future pass extends the homing system to creep targets too --
           flagged, not faked. */
        if (h->hero_id == ARENA_HERO_GARY || h->hero_id == ARENA_HERO_ABRAHAM) continue; /* S202-34: Abraham's basic attack is homing now too */

        ArenaHero *foe = arena_nearest_enemy(i);
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_ATTACK_RANGE) continue; /* already busy with an enemy hero this tick */
        }

        for (int c = 0; c < ARENA_MAX_CREEPS; c++) {
            ArenaCreep *creep = &arena_state.creeps[c];
            if (!creep->alive) continue;
            float dx = creep->x - h->x, dz = creep->z - h->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_ATTACK_RANGE) continue;

            /* Creeps have no armor stat -- flat damage, no apply_armor call needed. */
            creep->hp -= ARENA_ATTACK_DAMAGE + arena_hero_bonus_ad(h); /* S170-190 */
            creep->last_attacked_by_owner = i;
            h->attack_cooldown_ms = apply_cdr(h, ARENA_ATTACK_COOLDOWN_MS); /* S170-207 */
            if (creep->hp <= 0) {
                creep->hp = 0;
                creep_die(creep, &arena_state.nodes[c]);
            }
            break; /* one creep target per hero per attack, same as hero-vs-hero */
        }
    }
}

/* tower_die: same shape as creep_die above, minus the flavor-specific capture-bonus/heal branches
 * a tower has no flavor to key off of -- just last-hit kill credit, permanently. */
static void tower_die(ArenaTower *tower) {
    tower->alive = 0; /* permanent -- no respawn, see the ARENA_TOWER_MAX_HP header comment for why */
    if (tower->last_attacked_by_owner < 0) return;
    ArenaHero *killer = &arena_state.heroes[tower->last_attacked_by_owner];
    killer->flow += ARENA_TOWER_KILL_FLOW;
    killer->flow_earned += ARENA_TOWER_KILL_FLOW;
    killer->xp += ARENA_TOWER_KILL_XP;
}

/* arena_tick_towers: see the header declaration's own doc comment. */
void arena_tick_towers(unsigned int dt_ms) {
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        ArenaTower *tower = &arena_state.towers[n];
        if (!tower->alive) continue;
        if (tower->attack_cooldown_ms > 0) tower->attack_cooldown_ms -= (int)dt_ms;

        ArenaHero *target = NULL;
        float best_dist = 0.0f;
        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *cand = &arena_state.heroes[h];
            if (!cand->active || !hero_is_hittable(cand)) continue;
            float dx = cand->x - tower->x, dz = cand->z - tower->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > ARENA_TOWER_AGGRO_RADIUS) continue;
            if (!target || dist < best_dist) { target = cand; best_dist = dist; }
        }
        if (target && tower->attack_cooldown_ms <= 0) {
            /* 2026-07-30, founder: "show the tower damage as projectiles" -- a real travelling
               shot instead of an instant, invisible hit. Non-homing (see ARENA_PROJECTILE_NO_OWNER's
               own header doc comment for why); `team` is set to the OPPOSITE of the target's own
               team purely so the shared hit-filter in arena_tick_projectiles (`foe->team ==
               p->team` skips) resolves to "can hit target->team," the same trick every other
               projectile in this file relies on, just computed from the target instead of a
               firing hero since a tower has no team of its own. */
            arena_spawn_projectile(ARENA_PROJECTILE_NO_OWNER, 1 - target->team, ARENA_HERO_UNICORN,
                tower->x, tower->z, target->x, target->z,
                ARENA_TOWER_PROJECTILE_SPEED, ARENA_TOWER_PROJECTILE_RADIUS,
                ARENA_TOWER_DAMAGE, ARENA_TOWER_PROJECTILE_MAX_RANGE);
            tower->attack_cooldown_ms = ARENA_TOWER_ATTACK_COOLDOWN_MS;
        }
    }
}

/* arena_hero_attack_towers: see the header declaration's own doc comment. */
void arena_hero_attack_towers(unsigned int dt_ms) {
    (void)dt_ms; /* attack_cooldown_ms is ticked in tick_hero_kit/resolve_combat already; this only spends it */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive || h->attack_cooldown_ms > 0 || h->stunned_ms > 0) continue;
        if (h->mnm_burrow_ms > 0) continue;
        if (h->hero_id == ARENA_HERO_GARY || h->hero_id == ARENA_HERO_ABRAHAM) continue; /* same homing-only-basic-attack exclusion as arena_hero_attack_creeps (S202-34: Abraham joined Gary here) */

        ArenaHero *foe = arena_nearest_enemy(i);
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_ATTACK_RANGE) continue; /* already busy with an enemy hero this tick */
        }

        for (int n = 0; n < ARENA_NODE_COUNT; n++) {
            ArenaTower *tower = &arena_state.towers[n];
            if (!tower->alive) continue;
            float dx = tower->x - h->x, dz = tower->z - h->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_ATTACK_RANGE) continue;

            /* Towers have no armor stat, same "flat damage" convention arena_hero_attack_creeps
               already uses for creeps -- see that function's own comment. */
            tower->hp -= ARENA_ATTACK_DAMAGE + arena_hero_bonus_ad(h);
            tower->last_attacked_by_owner = i;
            h->attack_cooldown_ms = apply_cdr(h, ARENA_ATTACK_COOLDOWN_MS);
            if (tower->hp <= 0) {
                tower->hp = 0;
                tower_die(tower);
            }
            break; /* one tower target per hero per attack, same as creeps/hero-vs-hero */
        }
    }
}

/* lane_creep_waypoint (S170-139): see the ARENA_LANE_WAYPOINT_COUNT header
 * comment in arena_game.h -- a straight 3-point path along the existing
 * spawn axis, each team's own spawn line to the contested center node to the
 * enemy's spawn line. team 0 and team 1 walk the same three points in
 * opposite order. Clamps out-of-range indices defensively rather than
 * reading past the static array. */
static void lane_creep_waypoint(int team, int index, float *out_x, float *out_z) {
    static const float path_team0[ARENA_LANE_WAYPOINT_COUNT][2] = { { -8.0f, 0.0f }, { 0.0f, 0.0f }, { 8.0f, 0.0f } };
    static const float path_team1[ARENA_LANE_WAYPOINT_COUNT][2] = { { 8.0f, 0.0f }, { 0.0f, 0.0f }, { -8.0f, 0.0f } };
    if (index < 0) index = 0;
    if (index >= ARENA_LANE_WAYPOINT_COUNT) index = ARENA_LANE_WAYPOINT_COUNT - 1;
    const float (*path)[2] = (team == 0) ? path_team0 : path_team1;
    *out_x = path[index][0];
    *out_z = path[index][1];
}

/* lane_creep_range/lane_creep_damage (S170-218): per-role lookups -- ARENA_LANE_CREEP_MELEE
 * (value 0) reuses the original global constants unchanged, ARENA_LANE_CREEP_CASTER gets its
 * own. Kept as tiny helpers rather than inlined at each of the several call sites below that
 * need one or the other. */
static float lane_creep_range(const ArenaLaneCreep *creep) {
    return creep->role == ARENA_LANE_CREEP_CASTER ? ARENA_LANE_CREEP_CASTER_RANGE : ARENA_LANE_CREEP_AGGRO_RADIUS;
}
static int lane_creep_damage(const ArenaLaneCreep *creep) {
    return creep->role == ARENA_LANE_CREEP_CASTER ? ARENA_LANE_CREEP_CASTER_DAMAGE : ARENA_LANE_CREEP_DAMAGE;
}

/* lane_creep_spawn_wave: fills up to ARENA_LANE_CREEPS_PER_WAVE free pool
 * slots with fresh creeps at `team`'s spawn line (waypoint 0), spread along z
 * so a wave doesn't spawn perfectly stacked on one point. If fewer free
 * slots exist than a full wave (a previous wave hasn't fully cleared out),
 * spawns as many as fit rather than blocking the whole wave on pool space --
 * ARENA_MAX_LANE_CREEPS' 4x-a-single-wave headroom should make that rare in
 * practice, not something worth a harder failure mode for.
 *
 * S170-218: the first ARENA_LANE_WAVE_CASTER_COUNT creeps spawned each wave are casters, the
 * rest melee -- an arbitrary but fixed assignment (not randomized), simplest thing that
 * guarantees every wave has the same role mix rather than leaving it to chance. */
static void lane_creep_spawn_wave(int team) {
    int spawned = 0;
    float wx, wz;
    lane_creep_waypoint(team, 0, &wx, &wz);
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS && spawned < ARENA_LANE_CREEPS_PER_WAVE; i++) {
        ArenaLaneCreep *creep = &arena_state.lane_creeps[i];
        if (creep->active) continue;
        creep->active = 1;
        creep->alive = 1;
        creep->team = team;
        creep->waypoint_index = 0;
        creep->role = (spawned < ARENA_LANE_WAVE_CASTER_COUNT) ? ARENA_LANE_CREEP_CASTER : ARENA_LANE_CREEP_MELEE;
        creep->hp = creep->max_hp = (creep->role == ARENA_LANE_CREEP_CASTER) ? ARENA_LANE_CREEP_CASTER_HP : ARENA_LANE_CREEP_HP;
        creep->x = wx;
        creep->z = wz + (spawned - (ARENA_LANE_CREEPS_PER_WAVE - 1) / 2.0f) * 1.0f;
        creep->attack_cooldown_ms = 0;
        spawned++;
    }
}

/* arena_tick_lane_creeps (S170-139): see the header declaration's doc
 * comment. */
void arena_tick_lane_creeps(unsigned int dt_ms) {
    float dt_sec = (float)dt_ms / 1000.0f;

    for (int t = 0; t < 2; t++) {
        arena_state.lane_wave_timer_ms[t] -= (int)dt_ms;
        if (arena_state.lane_wave_timer_ms[t] > 0) continue;
        arena_state.lane_wave_timer_ms[t] = ARENA_LANE_WAVE_INTERVAL_MS;
        lane_creep_spawn_wave(t);
    }

    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) {
        ArenaLaneCreep *creep = &arena_state.lane_creeps[i];
        if (!creep->active || !creep->alive) continue;

        if (creep->attack_cooldown_ms > 0) creep->attack_cooldown_ms -= (int)dt_ms;

        /* Aggro: nearest hittable enemy hero, or nearest opposing-team lane
           creep if that's closer -- a wave clash is the actual "push" this
           subsystem exists for, not just a hero-harassment tool. Stops to
           fight instead of marching past, same "passive-until-approached
           becomes active-once-engaged" idiom as node-guardian creeps (S170-51),
           just with a real opposing target instead of only heroes. */
        ArenaHero *nearest_hero = NULL;
        float hero_dist = 0.0f;
        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *cand = &arena_state.heroes[h];
            if (!cand->active || cand->team == creep->team || !hero_is_hittable(cand)) continue;
            float dx = cand->x - creep->x, dz = cand->z - creep->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > lane_creep_range(creep)) continue;
            if (!nearest_hero || dist < hero_dist) { nearest_hero = cand; hero_dist = dist; }
        }

        ArenaLaneCreep *nearest_creep = NULL;
        float creep_dist = 0.0f;
        for (int c = 0; c < ARENA_MAX_LANE_CREEPS; c++) {
            if (c == i) continue;
            ArenaLaneCreep *cand = &arena_state.lane_creeps[c];
            if (!cand->active || !cand->alive || cand->team == creep->team) continue;
            float dx = cand->x - creep->x, dz = cand->z - creep->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > lane_creep_range(creep)) continue;
            if (!nearest_creep || dist < creep_dist) { nearest_creep = cand; creep_dist = dist; }
        }

        /* S170-214: minion-aggro-redirect -- a hero attacking an enemy hero within THIS
           creep's own aggro radius draws the creep's aggro onto the attacker, overriding
           the plain-nearest pick above, the real "minion aggro" mechanic real MOBA laning
           depends on. Detected via the DEFENDER-side last_attacked_by_owner + combat_timer_ms
           > 0 signal (same fields arena_tick_attack_windups/Gary's homing shot already set,
           same sentinel-after-respawn idiom kill-credit already uses at arena_game.c:814) --
           there's no true same-tick attacker-side flag available here: arena_tick_lane_creeps
           runs BEFORE hero-vs-hero combat resolves this same tick (see arena_update_teams'
           own call order), and damaged_this_tick is cleared at the END of the PREVIOUS tick
           by the time this runs. combat_timer_ms's own ARENA_COMBAT_TIMEOUT_MS recency window
           doubles as "how long ago still counts as currently fighting." */
        ArenaHero *aggro_hero = NULL;
        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *ally = &arena_state.heroes[h];
            if (!ally->active || ally->team != creep->team) continue;
            if (ally->combat_timer_ms <= 0 || ally->last_attacked_by_owner < 0 ||
                ally->last_attacked_by_owner >= ARENA_HEROES_ARRAY_SIZE) continue;
            ArenaHero *attacker = &arena_state.heroes[ally->last_attacked_by_owner];
            if (!attacker->active || attacker->team == creep->team || !hero_is_hittable(attacker)) continue;
            float adx = attacker->x - creep->x, adz = attacker->z - creep->z;
            if (sqrtf(adx * adx + adz * adz) > lane_creep_range(creep)) continue;
            aggro_hero = attacker;
            break;
        }

        ArenaHero *atk_hero = NULL;
        ArenaLaneCreep *atk_creep = NULL;
        if (aggro_hero) atk_hero = aggro_hero;
        else if (nearest_hero && (!nearest_creep || hero_dist <= creep_dist)) atk_hero = nearest_hero;
        else if (nearest_creep) atk_creep = nearest_creep;

        if ((atk_hero || atk_creep) && creep->attack_cooldown_ms <= 0) {
            if (atk_hero) {
                apply_damage(atk_hero, lane_creep_damage(creep)); /* no armor stat on lane-creep attacks, same as node-guardian creeps */
            } else {
                atk_creep->hp -= lane_creep_damage(creep);
                if (atk_creep->hp <= 0) { atk_creep->hp = 0; atk_creep->alive = 0; atk_creep->active = 0; }
            }
            creep->attack_cooldown_ms = ARENA_LANE_CREEP_ATTACK_COOLDOWN_MS;
            continue; /* stopped to fight -- no movement this tick */
        }
        if (atk_hero || atk_creep) continue; /* mid-swing (cooldown not ready yet) -- holds position, still doesn't march */

        float wx, wz;
        lane_creep_waypoint(creep->team, creep->waypoint_index, &wx, &wz);
        float dx = wx - creep->x, dz = wz - creep->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist < ARENA_LANE_CREEP_WAYPOINT_EPSILON) {
            if (creep->waypoint_index >= ARENA_LANE_WAYPOINT_COUNT - 1) {
                /* Reached the enemy spawn line with nothing left to fight --
                   no structure/tower exists yet for the wave to actually
                   push against (see the header comment), so it despawns
                   here rather than damaging anything, flagged not faked. */
                creep->alive = 0;
                creep->active = 0;
            } else {
                creep->waypoint_index++;
            }
        } else {
            float step = ARENA_LANE_CREEP_SPEED * dt_sec;
            if (step >= dist) { creep->x = wx; creep->z = wz; }
            else { creep->x += dx / dist * step; creep->z += dz / dist * step; }
        }
    }
}

/* arena_hero_attack_lane_creeps (S170-139): see the header declaration's doc
 * comment. */
void arena_hero_attack_lane_creeps(unsigned int dt_ms) {
    (void)dt_ms; /* attack_cooldown_ms is ticked in tick_hero_kit/resolve_combat already; this only spends it, same idiom as arena_hero_attack_creeps */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive || h->attack_cooldown_ms > 0 || h->stunned_ms > 0) continue; /* S170-184 */
        if (h->mnm_burrow_ms > 0) continue; /* S170-208: burrowed, not present to swing at anything */
        /* S170-163: same exclusion as arena_hero_attack_creeps above -- see
           that function's own comment. */
        if (h->hero_id == ARENA_HERO_GARY || h->hero_id == ARENA_HERO_ABRAHAM) continue; /* S202-34: Abraham's basic attack is homing now too */

        ArenaHero *foe = arena_nearest_enemy(i);
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_ATTACK_RANGE) continue; /* already busy with an enemy hero this tick */
        }

        for (int c = 0; c < ARENA_MAX_LANE_CREEPS; c++) {
            ArenaLaneCreep *creep = &arena_state.lane_creeps[c];
            if (!creep->active || !creep->alive) continue;
            /* S170-215: deny -- real League doesn't block the enemy from finishing a low-HP
               minion, it's a RACE: once a minion drops below 50% HP, allies (normally excluded
               entirely, see the OPPOSING-team-only rule below) gain the ability to kill their
               own creep first and deny the enemy the reward. Only the "ally CAN kill their own
               below 50%" half is built here -- the "enemy CAN'T finish it below 50%" half §20.3
               separately floated isn't how the real mechanic works (it would be an artificial
               buff beyond what deny actually does), so it's deliberately not added. */
            if (creep->team == h->team && creep->hp * 2 > creep->max_hp) continue;
            float dx = creep->x - h->x, dz = creep->z - h->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_ATTACK_RANGE) continue;

            creep->hp -= ARENA_ATTACK_DAMAGE + arena_hero_bonus_ad(h); /* no armor stat on lane creeps, same as node-guardian creeps; S170-190 */
            h->attack_cooldown_ms = apply_cdr(h, ARENA_ATTACK_COOLDOWN_MS); /* S170-207 */
            if (creep->hp <= 0) {
                creep->hp = 0;
                creep->alive = 0;
                creep->active = 0;
                /* S170-175: melee kill only -- arena_zone_damage_creeps'
                   own AoE-vs-lane-creep branch deliberately doesn't award
                   this, same "not every damage source needs full reward
                   wiring" precedent that function's own doc comment
                   already sets for node-guardian creeps. */
                h->flow += ARENA_LANE_CREEP_KILL_FLOW;
                h->flow_earned += ARENA_LANE_CREEP_KILL_FLOW;
                h->xp += ARENA_LANE_CREEP_KILL_XP;
                /* S170-216: XP-share -- gold/Flow above stays individual/precise (killer only),
                   but every OTHER allied hero within ARENA_LANE_CREEP_XP_SHARE_RADIUS of the
                   kill also gets the XP, real MOBA "present for the wave" parity. */
                for (int a = 0; a < ARENA_MAX_HEROES; a++) {
                    if (a == i) continue;
                    ArenaHero *ally = &arena_state.heroes[a];
                    if (!ally->active || !ally->alive || ally->team != h->team) continue;
                    float sdx = ally->x - creep->x, sdz = ally->z - creep->z;
                    if (sqrtf(sdx * sdx + sdz * sdz) > ARENA_LANE_CREEP_XP_SHARE_RADIUS) continue;
                    ally->xp += ARENA_LANE_CREEP_KILL_XP;
                }
            }
            break; /* one creep target per hero per attack, same as node-guardian creeps/hero-vs-hero */
        }
    }
}

/* arena_camp_position (Jungle Camps Milestone 1): see the header declaration's own doc
 * comment. Same "-margin so it's never buried in terrain" idiom arena_fountain_position/
 * arena_graveyard_position already use -- 8.0f margin matches arena_fountain_position's own. */
void arena_camp_position(int index, float *x, float *z) {
    float edge = ARENA_HALF_EXTENT - 8.0f;
    if (index < 0) index = 0;
    if (index >= ARENA_CAMP_COUNT) index = ARENA_CAMP_COUNT - 1;
    switch (index) {
        case 0: *x = 0.0f;  *z = edge;  break; /* N */
        case 1: *x = 0.0f;  *z = -edge; break; /* S */
        case 2: *x = edge;  *z = 0.0f;  break; /* E */
        default: *x = -edge; *z = 0.0f; break; /* W */
    }
}

/* camp_minion_spawn_wave: fills up to ARENA_CAMP_MINIONS_PER_WAVE free pool slots with fresh
 * minions at `camp_index`'s own position, spread along x so a wave doesn't spawn perfectly
 * stacked on one point -- same "spread the spawn" idiom lane_creep_spawn_wave already uses (that
 * one spreads along z since lanes run along x; camps have no fixed axis, x is an arbitrary but
 * consistent choice). Neutral -- no team field to set, matching ArenaCampMinion's own shape. */
static void camp_minion_spawn_wave(int camp_index) {
    float cx, cz;
    arena_camp_position(camp_index, &cx, &cz);
    int spawned = 0;
    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS && spawned < ARENA_CAMP_MINIONS_PER_WAVE; i++) {
        ArenaCampMinion *m = &arena_state.camp_minions[i];
        if (m->active) continue;
        m->active = 1;
        m->alive = 1;
        m->hp = m->max_hp = ARENA_CAMP_MINION_HP;
        m->x = cx + (spawned - (ARENA_CAMP_MINIONS_PER_WAVE - 1) / 2.0f) * 1.0f;
        m->z = cz;
        m->attack_cooldown_ms = 0;
        m->camp_index = camp_index; /* §3.4 -- which camp's escalation state governs this minion */
        spawned++;
    }
}

/* arena_tick_camp_minions (Jungle Camps Milestone 1): see the header declaration's own doc
 * comment. Neutral aggro (nearest hittable hero of EITHER team, same ARENA_CREEP_NEUTRAL shape
 * node-guardian creeps already use for their own neutral flavor) -- stationary otherwise, no
 * waypoint march (that's §3.4's separate, not-yet-built anti-stall escalation). */
/* camp_minion_nearest_node (§3.4): fills (nx, nz) with whichever ArenaNode is closest to
 * (x, z), regardless of owner -- the march target is "the nearest live objective," not
 * specifically an enemy or unowned one (a camp escalating near a team's OWN node still creates
 * real pressure, forcing that team to respond or lose it). Always finds one: ARENA_NODE_COUNT
 * is never 0. */
static void camp_minion_nearest_node(float x, float z, float *nx, float *nz) {
    int best = 0;
    float best_dist = -1.0f;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        float dx = arena_state.nodes[n].x - x, dz = arena_state.nodes[n].z - z;
        float dist = dx * dx + dz * dz;
        if (best_dist < 0.0f || dist < best_dist) { best = n; best_dist = dist; }
    }
    *nx = arena_state.nodes[best].x;
    *nz = arena_state.nodes[best].z;
}

void arena_tick_camp_minions(unsigned int dt_ms) {
    float dt_sec = (float)dt_ms / 1000.0f;

    for (int c = 0; c < ARENA_CAMP_COUNT; c++) {
        arena_state.camp_wave_timer_ms[c] -= (int)dt_ms;
        if (arena_state.camp_wave_timer_ms[c] > 0) continue;
        arena_state.camp_wave_timer_ms[c] = ARENA_CAMP_WAVE_INTERVAL_MS;
        camp_minion_spawn_wave(c);
    }

    /* §3.4 Anti-stall escalation: per-camp "has this camp had a living minion continuously
       long enough to count as uncleared" tracking -- see ARENA_CAMP_ESCALATION_THRESHOLD_MS's
       own doc comment for why this resets on any full clear rather than accumulating total
       elapsed match time. */
    for (int c = 0; c < ARENA_CAMP_COUNT; c++) {
        int any_active = 0;
        for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
            if (arena_state.camp_minions[i].active && arena_state.camp_minions[i].camp_index == c) { any_active = 1; break; }
        }
        if (!any_active) {
            arena_state.camp_uncleared_ms[c] = 0;
            arena_state.camp_escalated[c] = 0;
            continue;
        }
        arena_state.camp_uncleared_ms[c] += (int)dt_ms;
        if (arena_state.camp_uncleared_ms[c] >= ARENA_CAMP_ESCALATION_THRESHOLD_MS) {
            arena_state.camp_escalated[c] = 1;
        }
    }

    for (int i = 0; i < ARENA_MAX_CAMP_MINIONS; i++) {
        ArenaCampMinion *m = &arena_state.camp_minions[i];
        if (!m->active || !m->alive) continue;
        if (m->attack_cooldown_ms > 0) m->attack_cooldown_ms -= (int)dt_ms;

        ArenaHero *target = NULL;
        float best_dist = 0.0f;
        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *cand = &arena_state.heroes[h];
            if (!cand->active || !hero_is_hittable(cand)) continue;
            float dx = cand->x - m->x, dz = cand->z - m->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > ARENA_CAMP_MINION_AGGRO_RADIUS) continue;
            if (!target || dist < best_dist) { target = cand; best_dist = dist; }
        }
        if (target) {
            /* Same "stops to fight instead of marching past" idiom lane creeps already use --
               a hittable hero in range holds an escalated minion in place too, it doesn't just
               plow through. */
            if (m->attack_cooldown_ms <= 0) {
                apply_damage(target, apply_armor(ARENA_CAMP_MINION_DAMAGE, arena_hero_armor(target)));
                m->attack_cooldown_ms = ARENA_CAMP_MINION_ATTACK_COOLDOWN_MS;
            }
            continue;
        }

        /* §3.4: march toward the nearest node once this minion's own camp has escalated --
           otherwise stationary, the original Milestone 1 guardian behavior. */
        if (!arena_state.camp_escalated[m->camp_index]) continue;
        float nx, nz;
        camp_minion_nearest_node(m->x, m->z, &nx, &nz);
        float dx = nx - m->x, dz = nz - m->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist < ARENA_CAMP_MINION_WAYPOINT_EPSILON) continue; /* arrived -- stands and holds the node, real board presence */
        float step = ARENA_CAMP_MINION_MARCH_SPEED * dt_sec;
        if (step >= dist) { m->x = nx; m->z = nz; }
        else { m->x += dx / dist * step; m->z += dz / dist * step; }
    }
}

/* arena_hero_attack_camp_minions: see the header declaration's own doc comment. */
void arena_hero_attack_camp_minions(unsigned int dt_ms) {
    (void)dt_ms; /* same "only spends the cooldown, doesn't tick it" idiom as arena_hero_attack_lane_creeps */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive || h->attack_cooldown_ms > 0 || h->stunned_ms > 0) continue;
        if (h->mnm_burrow_ms > 0) continue;
        if (h->hero_id == ARENA_HERO_GARY || h->hero_id == ARENA_HERO_ABRAHAM) continue; /* same homing-only-basic-attack exclusion as arena_hero_attack_creeps/lane_creeps (S202-34: Abraham joined Gary here) */

        ArenaHero *foe = arena_nearest_enemy(i);
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_ATTACK_RANGE) continue; /* already busy with an enemy hero this tick */
        }

        for (int c = 0; c < ARENA_MAX_CAMP_MINIONS; c++) {
            ArenaCampMinion *m = &arena_state.camp_minions[c];
            if (!m->active || !m->alive) continue;
            float dx = m->x - h->x, dz = m->z - h->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_ATTACK_RANGE) continue;

            m->hp -= ARENA_ATTACK_DAMAGE + arena_hero_bonus_ad(h); /* no armor stat, same convention every other creep-damage site here uses */
            h->attack_cooldown_ms = apply_cdr(h, ARENA_ATTACK_COOLDOWN_MS);
            if (m->hp <= 0) {
                m->hp = 0;
                m->alive = 0;
                m->active = 0;
                /* West/All-Seeing's Farsight (Jungle Camps Milestone 2): "bonus gold from
                   monster kills" -- a camp minion is exactly that, same bonus King kills get. */
                int flow = ARENA_CAMP_MINION_KILL_FLOW;
                if (arena_state.king_allseeing_team_ms[h->team] > 0) {
                    flow += (flow * ARENA_KING_ALLSEEING_BONUS_FLOW_PCT) / 100;
                }
                h->flow += flow;
                h->flow_earned += flow;
                h->xp += ARENA_CAMP_MINION_KILL_XP;
            }
            break; /* one minion target per hero per attack, same as every other creep type here */
        }
    }
}

/* redgarden_host_tree_passive_strike: see header declaration's own doc comment. Placed here,
   not next to arena_obstacles_reset_layout/arena_tick_obstacles, because it (like
   arena_hero_tree_passive below) needs apply_cdr -- only forward-declared, not yet defined, that
   early in the file; arena_hero_attack_camp_minions just above already established this is where
   real definitions of these helpers first become available. */
void redgarden_host_tree_passive_strike(int hero_index, int obstacle_index) {
    if (hero_index < 0 || hero_index >= ARENA_MAX_HEROES) return;
    if (obstacle_index < 0 || obstacle_index >= ARENA_OBSTACLE_COUNT) return;
    ArenaHero *h = &arena_state.heroes[hero_index];
    ArenaObstacle *o = &arena_state.obstacles[obstacle_index];
    if (o->kind != ARENA_OBSTACLE_TREE) return; /* defensive -- arena_hero_tree_passive only ever targets TREE-kind obstacles, this just refuses to misfire if that ever changes */

    o->hp -= ARENA_TREE_PASSIVE_DAMAGE;
    if (o->hp < 0) o->hp = 0; /* never destroyed/despawned -- see this feature's own doc comment on why trees are a permanent resource */

    h->hp += ARENA_TREE_PASSIVE_HEAL_PER_HIT;
    if (h->hp > h->max_hp) h->hp = h->max_hp;
}

/* redgarden_host_duck_smoke_bomb_cast: see header declaration's own doc comment. */
void redgarden_host_duck_smoke_bomb_cast(int hero_index) {
    if (hero_index < 0 || hero_index >= ARENA_HEROES_ARRAY_SIZE) return;
    ArenaHero *h = &arena_state.heroes[hero_index];
    if (!h->active) return;
    h->duck_smoke_ms = ARENA_DUCK_W_DURATION_MS;
    h->duck_smoke_x = h->x;
    h->duck_smoke_z = h->z;
}

/* redgarden_host_abraham_fireball_cast: see header declaration's own doc comment. Called from
 * tick_hero_kit's ARENA_HERO_ABRAHAM completion branch (the windup already finished, cooldown/
 * mana already spent at cast start) -- the real work here is exactly one thing, spawn the real
 * piercing shot in the direction of the click point, "no real range limit" (founder), so
 * ARENA_ABRAHAM_FIREBALL_MAX_RANGE is a generous map-spanning distance, not the actual clicked
 * point's own distance -- the shot travels the FULL max_range along that direction regardless of
 * where the player actually clicked, matching "just have it go whatever direction is the click"
 * literally (direction only, not a bounded point-to-point shot the way Gary's homing attack is). */
void redgarden_host_abraham_fireball_cast(int hero_index, int target_x, int target_z) {
    if (hero_index < 0 || hero_index >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[hero_index];
    if (!h->active || !h->alive) return;

    float dx = (float)target_x - h->x, dz = (float)target_z - h->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.0001f) { dx = 1.0f; dz = 0.0f; dist = 1.0f; } /* degenerate same-position click: pick an arbitrary direction rather than a NaN velocity */
    /* arena_spawn_projectile takes a target POINT, not a direction -- extend the click
       direction out to the real max range so the shot travels the full distance regardless of
       how far the player actually clicked (see doc comment above). */
    float far_x = h->x + (dx / dist) * ARENA_ABRAHAM_FIREBALL_MAX_RANGE;
    float far_z = h->z + (dz / dist) * ARENA_ABRAHAM_FIREBALL_MAX_RANGE;

    ArenaProjectile *shot = arena_spawn_projectile(hero_index, h->team, ARENA_HERO_ABRAHAM,
        h->x, h->z, far_x, far_z,
        ARENA_ABRAHAM_FIREBALL_SPEED, ARENA_ABRAHAM_FIREBALL_RADIUS, ARENA_ABRAHAM_FIREBALL_DAMAGE,
        ARENA_ABRAHAM_FIREBALL_MAX_RANGE);
    if (shot) {
        shot->pierce = 1;
        /* Ignite (2026-08-26, founder: "make it so that the fireball ignites the enemies it
           touches making them have burning too"): the generic per-hit resolution code (see its
           own comment on p->on_hit_burn_ms, this exact function's own header comment above)
           already runs once per enemy a piercing shot passes through -- setting these two
           fields is the whole change, every enemy the shot touches gets ignited automatically. */
        shot->on_hit_burn_ms = ARENA_ABRAHAM_FIREBALL_BURN_MS;
        shot->on_hit_burn_dps = ARENA_ABRAHAM_FIREBALL_BURN_DPS;
    }
}

/* arena_hero_tree_passive: see header declaration's own doc comment. */
void arena_hero_tree_passive(unsigned int dt_ms) {
    (void)dt_ms; /* same "only spends the cooldown, doesn't tick it" idiom as arena_hero_attack_camp_minions */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive || h->hero_id != ARENA_HERO_TREE) continue;
        if (h->attack_cooldown_ms > 0 || h->stunned_ms > 0) continue;

        ArenaHero *foe = arena_nearest_enemy(i);
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_ATTACK_RANGE) continue; /* already busy with an enemy hero this tick */
        }

        int best = -1;
        float best_dist = 0.0f;
        for (int o = 0; o < ARENA_OBSTACLE_COUNT; o++) {
            if (arena_state.obstacles[o].kind != ARENA_OBSTACLE_TREE) continue;
            float dx = arena_state.obstacles[o].x - h->x, dz = arena_state.obstacles[o].z - h->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > ARENA_ATTACK_RANGE) continue;
            if (best < 0 || dist < best_dist) { best = o; best_dist = dist; }
        }
        if (best < 0) continue;

        on_tree_passive_strike(i, best); /* PARENA-compiled -- the mod call IS the trigger, see this feature's own doc comment */
        h->attack_cooldown_ms = apply_cdr(h, ARENA_TREE_PASSIVE_COOLDOWN_MS);
    }
}

/* king_grant_buff (Jungle Camps Milestone 2): applies camp_index's own King's distinct buff
 * (§3.3) to the killing side. `killer` is the specific hero who landed the kill -- used for the
 * two individual mechanics (Growth/Wealth); the two team-wide mechanics (Music/All-Seeing)
 * instead sweep every living hero on `killer`'s team. Kept as one switch here rather than
 * inlined at the call site so arena_hero_attack_kings' own kill-branch stays readable. */
static void king_grant_buff(int camp_index, ArenaHero *killer) {
    switch (camp_index) {
        case 0: /* North -- Vaisravana, Wealth: Bulwark, individual holder */
            killer->king_wealth_ms = ARENA_KING_WEALTH_DURATION_MS;
            break;
        case 1: /* South -- Virudhaka, Growth: Bloodroar, individual, starts at 1 stack */
            killer->king_growth_stacks = 1;
            killer->king_growth_ms = ARENA_KING_GROWTH_DURATION_MS;
            break;
        case 2: /* East -- Dhrtarastra, Music: Catchy Song, team-viral -- every living teammate
                    becomes a carrier the instant it's claimed, same as the killer themselves. */
            for (int i = 0; i < ARENA_MAX_HEROES; i++) {
                ArenaHero *ally = &arena_state.heroes[i];
                if (ally->active && ally->alive && ally->team == killer->team) ally->king_music_carrier = 1;
            }
            break;
        default: /* West -- Virupaksha, All-Seeing: Farsight, genuinely team-wide flat timer */
            arena_state.king_allseeing_team_ms[killer->team] = ARENA_KING_ALLSEEING_DURATION_MS;
            break;
    }
}

/* arena_tick_kings (Jungle Camps Milestones 2+4): see the header declaration's own doc comment.
 * Silent until ARENA_KING_SPAWN_DELAY_MS (1:00) per camp, then boss-scale neutral-aggro attack,
 * same shape as arena_tick_camp_minions; a defeated King respawns on ARENA_KING_RESPAWN_MS
 * (Milestone 4, §5). Also ticks down the 3 timer-based King buffs (Music's king_music_carrier is
 * not a timer, see its own field doc comment). */
void arena_tick_kings(unsigned int dt_ms) {
    /* King spawn/respawn timer reuses the same per-camp countdown idiom as camp minions, but
       its own field (not camp_wave_timer_ms, which is minion-wave-only) -- see
       king_spawn_timer_ms's own doc comment for how one field unambiguously serves both the
       first spawn (max_hp == 0) and every respawn after (max_hp > 0, reset to 0 the instant the
       King dies -- see arena_hero_attack_kings' own kill branch). Gated on active, not
       alive/max_hp: a currently-alive King (active=1) needs no countdown running at all. */
    for (int c = 0; c < ARENA_CAMP_COUNT; c++) {
        ArenaKing *k = &arena_state.kings[c];
        if (k->active) continue;
        int threshold = (k->max_hp == 0) ? ARENA_KING_SPAWN_DELAY_MS : ARENA_KING_RESPAWN_MS;
        arena_state.king_spawn_timer_ms[c] += (int)dt_ms;
        if (arena_state.king_spawn_timer_ms[c] < threshold) continue;
        float kx, kz;
        arena_camp_position(c, &kx, &kz);
        k->active = 1;
        k->alive = 1;
        k->hp = k->max_hp = ARENA_KING_HP;
        k->x = kx;
        k->z = kz;
        k->attack_cooldown_ms = 0;
    }

    for (int c = 0; c < ARENA_CAMP_COUNT; c++) {
        ArenaKing *k = &arena_state.kings[c];
        if (!k->active || !k->alive) continue;
        if (k->attack_cooldown_ms > 0) k->attack_cooldown_ms -= (int)dt_ms;

        ArenaHero *target = NULL;
        float best_dist = 0.0f;
        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *cand = &arena_state.heroes[h];
            if (!cand->active || !hero_is_hittable(cand)) continue;
            float dx = cand->x - k->x, dz = cand->z - k->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > ARENA_KING_AGGRO_RADIUS) continue;
            if (!target || dist < best_dist) { target = cand; best_dist = dist; }
        }
        if (target && k->attack_cooldown_ms <= 0) {
            apply_damage(target, apply_armor(ARENA_KING_DAMAGE, arena_hero_armor(target)));
            k->attack_cooldown_ms = ARENA_KING_ATTACK_COOLDOWN_MS;
        }
    }

    /* Timer side of Growth/Wealth (individual) and All-Seeing (team-wide) -- Music has no timer
       to tick, see king_music_carrier's own doc comment. */
    for (int h = 0; h < ARENA_MAX_HEROES; h++) {
        ArenaHero *hero = &arena_state.heroes[h];
        if (!hero->active) continue;
        if (hero->king_growth_ms > 0) {
            hero->king_growth_ms -= (int)dt_ms;
            if (hero->king_growth_ms <= 0) { hero->king_growth_ms = 0; hero->king_growth_stacks = 0; }
        }
        if (hero->king_wealth_ms > 0) {
            hero->king_wealth_ms -= (int)dt_ms;
            if (hero->king_wealth_ms < 0) hero->king_wealth_ms = 0;
        }
    }
    for (int t = 0; t < 2; t++) {
        if (arena_state.king_allseeing_team_ms[t] > 0) {
            arena_state.king_allseeing_team_ms[t] -= (int)dt_ms;
            if (arena_state.king_allseeing_team_ms[t] < 0) arena_state.king_allseeing_team_ms[t] = 0;
        }
    }

    /* North/Wealth's gold trickle (Jungle Camps Milestone 2): "a smaller bonus-gold trickle to
       nearby teammates" -- the aura's econ half, separate from arena_hero_armor's own damage-
       reduction half since Flow isn't something that function touches. Per-second accumulation
       would need its own fractional-remainder field (see mp_regen_accum's own doc comment for
       why flat per-tick addition truncates to 0) -- sidestepped here by only crediting once a
       full second's worth of dt_ms has accumulated, same "keep it simple, this is a minor
       trickle not a precision system" spirit as the rest of this King's flavor. */
    arena_state.wealth_gold_tick_ms += (int)dt_ms;
    if (arena_state.wealth_gold_tick_ms >= 1000) {
        arena_state.wealth_gold_tick_ms -= 1000;
        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *holder = &arena_state.heroes[h];
            if (!holder->active || !holder->alive || holder->king_wealth_ms <= 0) continue;
            for (int a = 0; a < ARENA_MAX_HEROES; a++) {
                if (a == h) continue;
                ArenaHero *ally = &arena_state.heroes[a];
                if (!ally->active || !ally->alive || ally->team != holder->team) continue;
                float dx = ally->x - holder->x, dz = ally->z - holder->z;
                if (sqrtf(dx * dx + dz * dz) > ARENA_KING_WEALTH_AURA_RADIUS) continue;
                ally->flow += ARENA_KING_WEALTH_GOLD_PER_SEC;
                ally->flow_earned += ARENA_KING_WEALTH_GOLD_PER_SEC;
            }
        }
    }
}

/* arena_hero_attack_kings: see the header declaration's own doc comment. */
void arena_hero_attack_kings(unsigned int dt_ms) {
    (void)dt_ms;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive || h->attack_cooldown_ms > 0 || h->stunned_ms > 0) continue;
        if (h->mnm_burrow_ms > 0) continue;
        if (h->hero_id == ARENA_HERO_GARY || h->hero_id == ARENA_HERO_ABRAHAM) continue; /* S202-34: Abraham's basic attack is homing now too */

        ArenaHero *foe = arena_nearest_enemy(i);
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_ATTACK_RANGE) continue;
        }

        for (int c = 0; c < ARENA_CAMP_COUNT; c++) {
            ArenaKing *k = &arena_state.kings[c];
            if (!k->active || !k->alive) continue;
            float dx = k->x - h->x, dz = k->z - h->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_ATTACK_RANGE) continue;

            k->hp -= ARENA_ATTACK_DAMAGE + arena_hero_bonus_ad(h);
            h->attack_cooldown_ms = apply_cdr(h, ARENA_ATTACK_COOLDOWN_MS);
            if (k->hp <= 0) {
                k->hp = 0;
                k->alive = 0;
                k->active = 0;
                /* Milestone 4: arm the respawn countdown from this exact moment -- see
                   king_spawn_timer_ms's own doc comment for why resetting to 0 here (not
                   leaving whatever stale value it held pre-death) is what makes the single
                   shared field unambiguous between "counting to first spawn" and "counting to
                   respawn." */
                arena_state.king_spawn_timer_ms[c] = 0;
                /* West/All-Seeing's own Flow bonus (Jungle Camps Milestone 2) applies to this
                   very kill if the killer's team already has it active from a PREVIOUS King --
                   a real, intended stacking-objectives interaction, not a bug: claim West early,
                   every jungle-monster kill after is worth more until it expires. */
                int flow = ARENA_KING_KILL_FLOW;
                if (arena_state.king_allseeing_team_ms[h->team] > 0) {
                    flow += (flow * ARENA_KING_ALLSEEING_BONUS_FLOW_PCT) / 100;
                }
                h->flow += flow;
                h->flow_earned += flow;
                h->xp += ARENA_KING_KILL_XP;
                king_grant_buff(c, h);
            }
            break;
        }
    }
}

/* synergy_roll_tier (§25.3): weighted-random tier pick for one team, given that team's current
 * resource-race lead over the other (positive = ahead). base_probs are the source design's own
 * [0.60, 0.25, 0.10, 0.05] (docs2/MULTI_AGENT_RD_RESEARCH_NOTES.md), shifted per-tier-index by
 * `lead` -- see ARENA_SYNERGY_TIER_COUNT's own header doc comment for why this scales BY tier
 * index rather than reproducing the source's own constant-shift formula (a real bug: adding the
 * same value to every logit is a no-op under softmax). */
static int synergy_roll_tier(int lead) {
    static const float base_logit[ARENA_SYNERGY_TIER_COUNT] = {
        -0.5108256f, /* ln(0.60) */
        -1.3862944f, /* ln(0.25) */
        -2.3025851f, /* ln(0.10) */
        -2.9957323f, /* ln(0.05) */
    };
    float logit[ARENA_SYNERGY_TIER_COUNT];
    float max_logit = -1e30f;
    for (int t = 0; t < ARENA_SYNERGY_TIER_COUNT; t++) {
        logit[t] = base_logit[t] + (float)lead * ARENA_SYNERGY_LEAD_SHIFT_SCALE * (float)t;
        if (logit[t] > max_logit) max_logit = logit[t];
    }
    float prob[ARENA_SYNERGY_TIER_COUNT];
    float sum = 0.0f;
    for (int t = 0; t < ARENA_SYNERGY_TIER_COUNT; t++) {
        prob[t] = expf(logit[t] - max_logit);
        sum += prob[t];
    }
    /* Weighted pick: draw a uniform [0,1) and walk the cumulative distribution -- same idiom
       real weighted-random selection always uses, no library dependency needed for 4 buckets. */
    float roll = (float)rand() / ((float)RAND_MAX + 1.0f);
    float cumulative = 0.0f;
    for (int t = 0; t < ARENA_SYNERGY_TIER_COUNT; t++) {
        cumulative += prob[t] / sum;
        if (roll < cumulative) return t;
    }
    return ARENA_SYNERGY_TIER_COUNT - 1; /* float rounding fallback -- cumulative should reach ~1.0 */
}

/* arena_tick_synergy: see the header declaration's own doc comment. */
void arena_tick_synergy(unsigned int dt_ms) {
    arena_state.synergy_roll_timer_ms += (int)dt_ms;
    if (arena_state.synergy_roll_timer_ms < ARENA_SYNERGY_ROLL_INTERVAL_MS) return;
    arena_state.synergy_roll_timer_ms = 0;

    for (int t = 0; t < 2; t++) {
        int lead = arena_state.resources[t] - arena_state.resources[1 - t];
        arena_state.synergy_tier[t] = synergy_roll_tier(lead);
    }
}

/* arena_synergy_cdr_pct/arena_synergy_move_speed_pct (§25.3): the cohesion bonus for hero `h`'s
 * own team, linearly scaled from the full ARENA_SYNERGY_TIER0_* bonus at tier 0 down to 0 at the
 * fully-decayed tier -- same "read live off team state, not a per-hero copy" idiom North/
 * Wealth's own aura already uses. */
static int arena_synergy_cdr_pct(const ArenaHero *h) {
    /* Team-mode-only guard: synergy_tier's memset default is 0 ("full cohesion," the BEST
       tier), unlike every other King/buff field in this file which defaults to "off." A 1v1
       match never calls arena_tick_synergy (only arena_update_teams does), so without this
       guard synergy_tier would silently stay 0 forever and grant every 1v1 hero the full
       ambient bonus permanently -- a real bug caught while writing this, not a hypothetical.
       heroes[ARENA_TEAM_SIZE] (team B's fixed starting slot, populated only by arena_init_teams)
       is a real, size-independent "is this actually a team match" signal -- 1v1's own
       arena_init_with_heroes never touches that slot. */
    if (!arena_state.heroes[ARENA_TEAM_SIZE].active) return 0;
    int tier = arena_state.synergy_tier[h->team];
    return (ARENA_SYNERGY_TIER0_CDR_PCT * (ARENA_SYNERGY_TIER_COUNT - 1 - tier)) / (ARENA_SYNERGY_TIER_COUNT - 1);
}

static float arena_synergy_move_speed_pct(const ArenaHero *h) {
    /* Same team-mode-only guard as arena_synergy_cdr_pct -- see that function's own doc comment. */
    if (!arena_state.heroes[ARENA_TEAM_SIZE].active) return 0.0f;
    int tier = arena_state.synergy_tier[h->team];
    return (ARENA_SYNERGY_TIER0_MOVE_SPEED_PCT * (float)(ARENA_SYNERGY_TIER_COUNT - 1 - tier)) / (float)(ARENA_SYNERGY_TIER_COUNT - 1);
}

/* arena_zone_damage_creeps (S170-144, "ensure aoe damage spells hit
 * creeps"): applies `dps` flat damage to every living node-guardian creep AND lane
 * creep within `radius` of (x,z) -- AoE zone/aura ticks (Ghost's Recital,
 * Pizza's aura, Beleth's Detonation, Paimon's/NOOR-1's own R zones)
 * previously only ever checked the single nearest-enemy-HERO parameter
 * tick_hero_kit threads through, an existing, already-flagged limitation
 * (see Pizza's own aura comment) -- this closes the "does it hit creeps
 * too" half specifically, not the "hits every hero in radius" half (still
 * out of scope, unchanged). Flat damage, no armor (same convention every
 * other creep-damage site in this file already uses). A team-flavored
 * node-guardian creep is only a valid target for the OPPOSING team's zone, same as
 * melee (arena_hero_attack_creeps); a neutral one is fair game for anyone.
 * Lane creeps: only the opposing team's wave is ever a valid target, same
 * as melee (arena_hero_attack_lane_creeps). Zone kills grant no kill-credit
 * reward (node-guardian creeps' capture-bonus/heal, same as every other "not every
 * damage source needs full reward wiring" simplification already accepted
 * elsewhere in this file) -- flagged, not faked. */
static void arena_zone_damage_creeps(float x, float z, float radius, int caster_team, int dps) {
    for (int c = 0; c < ARENA_MAX_CREEPS; c++) {
        ArenaCreep *creep = &arena_state.creeps[c];
        if (!creep->alive) continue;
        if (creep->flavor != ARENA_CREEP_NEUTRAL && ((int)creep->flavor - 1) == caster_team) continue;
        float dx = creep->x - x, dz = creep->z - z;
        if (sqrtf(dx * dx + dz * dz) > radius) continue;
        creep->hp -= dps;
        creep->last_attacked_by_owner = -1; /* zone damage has no single attributable hero slot -- no reward on a zone kill, see doc comment above */
        if (creep->hp <= 0) {
            creep->hp = 0;
            creep_die(creep, &arena_state.nodes[c]);
        }
    }
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) {
        ArenaLaneCreep *lc = &arena_state.lane_creeps[i];
        if (!lc->active || !lc->alive) continue;
        if (lc->team == caster_team) continue;
        float dx = lc->x - x, dz = lc->z - z;
        if (sqrtf(dx * dx + dz * dz) > radius) continue;
        lc->hp -= dps;
        if (lc->hp <= 0) { lc->hp = 0; lc->alive = 0; lc->active = 0; }
    }
}

static void resolve_combat(unsigned int dt_ms) {
    ArenaHero *a = &arena_state.heroes[0];
    ArenaHero *b = &arena_state.heroes[1];
    if (a->attack_cooldown_ms > 0) a->attack_cooldown_ms -= (int)dt_ms;
    if (b->attack_cooldown_ms > 0) b->attack_cooldown_ms -= (int)dt_ms;
    if (!a->alive || !b->alive) return;

    float dx = b->x - a->x;
    float dz = b->z - a->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > ARENA_ATTACK_RANGE) return;

    /* S170-208: mnm_burrow_ms > 0 gates a hero out of this legacy 1v1 pairwise resolver the
       same way it gates the team-mode melee/creep-attack loops -- burrowed, not present to
       swing at anything. */
    if (a->attack_cooldown_ms <= 0 && a->mnm_burrow_ms <= 0) {
        /* S189-01: real attacker attribution -- both hero pointers already in scope here,
           zero risk, the one call site upgraded to apply_damage_ex (see its own doc comment
           in arena_game.h for why the other ~50 apply_damage sites aren't). */
        if (hero_is_hittable(b)) apply_damage_ex(b, apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(b)), a->hero_id);
        a->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }
    if (b->attack_cooldown_ms <= 0 && b->mnm_burrow_ms <= 0) {
        if (hero_is_hittable(a)) apply_damage_ex(a, apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(a)), b->hero_id);
        b->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }
}

/* --- Kit dispatch (S170-31 generalized this from S170-18's Unicorn-only,
   owner-hardcoded version -- everything below dispatches on hero_id, so
   either owner slot can carry either hero). --- */

static void unicorn_cast_q(ArenaHero *h, ArenaHero *foe) {
    /* Dash toward the current move target if moving, else toward the foe --
       a dash ability needs a direction, and "toward whatever you last
       clicked, or the enemy if you didn't" is the simplest honest default.
       foe may be NULL in team mode (no living enemy at all right now) --
       fall back to "moving" only in that case; if neither gives a
       direction, there's nothing to dash toward, so just no-op. */
    float dx, dz;
    if (h->moving) {
        dx = h->target_x - h->x;
        dz = h->target_z - h->z;
    } else if (foe) {
        dx = foe->x - h->x;
        dz = foe->z - h->z;
    } else {
        return;
    }
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return; /* no meaningful direction, e.g. already on top of foe */
    dx /= len; dz /= len;

    float nx = h->x + dx * ARENA_UNICORN_Q_DASH_DIST;
    float nz = h->z + dz * ARENA_UNICORN_Q_DASH_DIST;
    if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
    if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
    if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
    if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
    h->x = nx;
    h->z = nz;
    h->moving = 0;

    if (foe && hero_is_hittable(foe)) {
        float fdx = foe->x - h->x, fdz = foe->z - h->z;
        if (sqrtf(fdx * fdx + fdz * fdz) <= ARENA_UNICORN_Q_HIT_RADIUS) {
            apply_damage(foe, apply_armor(ARENA_UNICORN_Q_DAMAGE, arena_hero_armor(foe)));
        }
    }
    h->q_cooldown_ms = cast_cooldown(h, ARENA_UNICORN_Q_COOLDOWN_MS);
    h->mp -= ARENA_MP_COST_Q;
}

/* duck_pull_foe: shared logic for Telekinetic Yank (Q) and the bigger
 * Total Telekinesis (R) -- both pull the foe toward the Duck by pull_dist
 * (clamped so it can't overshoot past the Duck) and deal damage, only if
 * the foe starts out within max_range. Returns 1 if it landed (so the
 * caller only consumes the cooldown on an actual hit, not a whiff), 0 if
 * the foe was out of range or currently unhittable (e.g. Ghost's W). */
static int duck_pull_foe(ArenaHero *duck, ArenaHero *foe, float pull_dist, int damage, float max_range) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = duck->x - foe->x;
    float dz = duck->z - foe->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > max_range) return 0; /* out of range -- no whiff-damage, no partial pull */
    /* rooted_ms (S170-46): Tree's Grand Secret is "immune to displacement" --
       the pull is skipped but damage still lands, same as any other root not
       blocking incoming damage. */
    if (dist > 0.01f && foe->rooted_ms <= 0) {
        float pull = pull_dist < dist ? pull_dist : dist; /* never pull the foe past the Duck */
        foe->x += dx / dist * pull;
        foe->z += dz / dist * pull;
    }
    apply_damage(foe, apply_armor(damage, arena_hero_armor(foe)));
    return 1;
}

/* ghost_cast_q: Alien Frequency. S170-140: converted from an instant
 * hit-if-in-range check to a real travelling projectile (docs/HEROES_VS0.md
 * already calls this a "skillshot" -- it just wasn't implemented as one
 * until now), same "requires a real shot lined up at cast time, but landing
 * it is no longer guaranteed" convention as Gary's Q (S170-136). Returns 1
 * once the shot is fired (cooldown spent either way, same as any real
 * skill-shot), 0 if there was no hittable foe in range to fire at at all. */
static int ghost_cast_q(ArenaHero *ghost, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - ghost->x, dz = foe->z - ghost->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_GHOST_Q_RANGE) return 0;
    ArenaProjectile *p = arena_spawn_projectile(ghost->owner, ghost->team, ARENA_HERO_GHOST,
                             ghost->x, ghost->z, foe->x, foe->z,
                             ARENA_GHOST_Q_PROJECTILE_SPEED, ARENA_GHOST_Q_PROJECTILE_RADIUS,
                             ARENA_GHOST_Q_DAMAGE, ARENA_GHOST_Q_RANGE);
    if (p) p->on_hit_silence_ms = ARENA_GHOST_Q_SILENCE_MS;
    return 1;
}

/* frog_cast_q: Loop Back, rewinds the Frog's own position/HP to
 * ARENA_FROG_Q_REWIND_MS ago using the loopback ring buffer any hero
 * accumulates in tick_hero_kit. Self-targeted, so it always "lands" once
 * called (unlike Duck/Ghost, there's no range/hittability check -- you
 * can't whiff a rewind on yourself). If less history exists than the full
 * rewind window (e.g. cast in the first few seconds of a match), rewinds
 * as far back as is actually recorded rather than refusing to cast at all. */
static void frog_cast_q(ArenaHero *frog) {
    if (frog->loopback_count == 0) return; /* no history yet at all */
    int slots_back = ARENA_FROG_Q_REWIND_MS / ARENA_FROG_LOOPBACK_SAMPLE_MS;
    if (slots_back >= frog->loopback_count) slots_back = frog->loopback_count - 1;
    int idx = ((frog->loopback_next_slot - 1 - slots_back) % ARENA_FROG_LOOPBACK_SLOTS
               + ARENA_FROG_LOOPBACK_SLOTS) % ARENA_FROG_LOOPBACK_SLOTS;
    frog->x = frog->loopback_x[idx];
    frog->z = frog->loopback_z[idx];
    frog->hp = frog->loopback_hp[idx];
    frog->moving = 0;
}

/* doc_wheel_heal_amount: Extremely Good At Medicine -- linearly scales from
 * ARENA_DOC_WHEEL_Q_HEAL_BASE at 100% target HP up to
 * ARENA_DOC_WHEEL_Q_HEAL_LOW_HP at 0% target HP (S170-45). */
static int doc_wheel_heal_amount(const ArenaHero *target) {
    if (target->max_hp <= 0) return ARENA_DOC_WHEEL_Q_HEAL_BASE;
    float hp_pct = (float)target->hp / (float)target->max_hp;
    if (hp_pct < 0.0f) hp_pct = 0.0f;
    if (hp_pct > 1.0f) hp_pct = 1.0f;
    float heal = ARENA_DOC_WHEEL_Q_HEAL_BASE +
                 (ARENA_DOC_WHEEL_Q_HEAL_LOW_HP - ARENA_DOC_WHEEL_Q_HEAL_BASE) * (1.0f - hp_pct);
    return (int)heal;
}

static void doc_wheel_heal_and_cleanse(ArenaHero *target, int amount) {
    target->hp += amount;
    if (target->hp > target->max_hp) target->hp = target->max_hp;
    target->silenced_ms = 0; /* Bedside Manner: "cleanses one debuff" -- the only debuff arena has today */
}

/* tree_cast_q: Vine Lash, simplified from "AoE root in a cone in front" to
 * an instant hit-if-in-range check, same precedent as Ghost's Alien
 * Frequency. Returns 1 if it landed (cooldown only consumed on a hit), 0 on
 * a whiff. */
static int tree_cast_q(ArenaHero *tree, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - tree->x, dz = foe->z - tree->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_TREE_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_TREE_Q_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_TREE_Q_ROOT_MS;
    return 1;
}

/* pizza_cast_q: Nobody Checked, simplified from "throw a burning slice +
 * ground patch" to direct damage + a burn DoT applied straight to the foe --
 * no persistent ground-hazard system exists in this arena, so the
 * lingering-patch half is dropped, not faked. Returns 1 if it landed, 0 on
 * a whiff. */
static int pizza_cast_q(ArenaHero *pizza, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - pizza->x, dz = foe->z - pizza->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_PIZZA_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_PIZZA_Q_DAMAGE, arena_hero_armor(foe)));
    foe->burning_ms = ARENA_PIZZA_Q_BURN_MS;
    foe->burn_dps = ARENA_PIZZA_Q_BURN_DPS;
    return 1;
}

/* flamel_cast_q: Vine Growth (absorbed from the former Druid), simplified
 * from "wall of vines in a line" to an instant root-if-in-range check on
 * the nearest enemy -- same cone/line-to-single-target simplification as
 * Tree's Q. Pure crowd control, no damage, matching the doc's own ability
 * (blocks movement, nothing else). Returns 1 if it landed, 0 on a whiff. */
static int flamel_cast_q(ArenaHero *flamel, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - flamel->x, dz = foe->z - flamel->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_FLAMEL_Q_RANGE) return 0;
    foe->rooted_ms = ARENA_FLAMEL_Q_ROOT_MS;
    return 1;
}

/* flamel_cast_w: Philosopher's Bloom (Bloom + Philosopher's Batch merged,
 * S170-46) -- heals every living ally within radius at once, healing for
 * more if Flamel himself is standing within capture radius of a node his
 * own team has marked (Overgrowth, absorbed from Druid). Always "lands"
 * and consumes the cooldown, same always-commits convention as Doc Wheel's
 * R -- an AoE heal isn't a single-target poke that can whiff. */
static void flamel_cast_w(ArenaHero *flamel, int owner) {
    int on_marked_ground = 0;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        ArenaNode *node = &arena_state.nodes[n];
        if (node->marked_by_team != flamel->team) continue;
        float ndx = flamel->x - node->x, ndz = flamel->z - node->z;
        if (sqrtf(ndx * ndx + ndz * ndz) <= ARENA_NODE_CAPTURE_RADIUS) { on_marked_ground = 1; break; }
    }
    int heal = on_marked_ground ? ARENA_FLAMEL_W_HEAL_MARKED : ARENA_FLAMEL_W_HEAL_BASE;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *ally = &arena_state.heroes[i];
        if (i == owner || !ally->active || !ally->alive) continue;
        if (ally->team != flamel->team) continue;
        float dx = ally->x - flamel->x, dz = ally->z - flamel->z;
        if (sqrtf(dx * dx + dz * dz) > ARENA_FLAMEL_W_RADIUS) continue;
        ally->hp += heal;
        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
    }
}

/* execute_scale_damage: linearly scales from base_dmg at 100% target HP up
 * to low_hp_dmg at ~0% target HP -- same shape as doc_wheel_heal_amount,
 * inverted for damage instead of healing (Morrigan's death-omen kit,
 * S170-47: "the crow confirms the kill"). */
static int execute_scale_damage(const ArenaHero *target, int base_dmg, int low_hp_dmg) {
    if (target->max_hp <= 0) return base_dmg;
    float hp_pct = (float)target->hp / (float)target->max_hp;
    if (hp_pct < 0.0f) hp_pct = 0.0f;
    if (hp_pct > 1.0f) hp_pct = 1.0f;
    float dmg = base_dmg + (low_hp_dmg - base_dmg) * (1.0f - hp_pct);
    return (int)dmg;
}

/* morrigan_cast_q: The Washer's Strike, instant hit-if-in-range (same
 * precedent as Ghost/Tree/Pizza's Q), execute-scaled via
 * execute_scale_damage. Returns 1 if it landed, 0 on a whiff. */
static int morrigan_cast_q(ArenaHero *morrigan, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - morrigan->x, dz = foe->z - morrigan->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_MORRIGAN_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(execute_scale_damage(foe, ARENA_MORRIGAN_Q_DAMAGE_BASE, ARENA_MORRIGAN_Q_DAMAGE_LOW_HP),
                                   arena_hero_armor(foe)));
    return 1;
}

/* morrigan_cast_w: Three Forms -- teleports to the nearest enemy's position
 * and roots them on arrival ("she appears where he doesn't expect, in
 * another form"). No range check -- a sudden appearance, not a skillshot.
 * Returns 1 if it landed, 0 with no living enemy at all. */
static int morrigan_cast_w(ArenaHero *morrigan, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    morrigan->x = foe->x;
    morrigan->z = foe->z;
    morrigan->moving = 0;
    foe->rooted_ms = ARENA_MORRIGAN_W_ROOT_MS;
    return 1;
}

/* dagda_cast_q: "the same tool, either direction, depending only on which
 * end swings first" -- built literally. A hittable enemy in range takes
 * priority (the killing end); absent that, a hurt living ally in range
 * gets the reviving end, simplified to a heal since no respawn system
 * exists to revive a dead ally into. Returns 1 if either end landed, 0 on
 * a full whiff (nothing valid in range at all). */
static int dagda_cast_q(ArenaHero *dagda, ArenaHero *foe, ArenaHero *ally) {
    if (foe && hero_is_hittable(foe)) {
        float dx = foe->x - dagda->x, dz = foe->z - dagda->z;
        if (sqrtf(dx * dx + dz * dz) <= ARENA_DAGDA_Q_RANGE) {
            apply_damage(foe, apply_armor(ARENA_DAGDA_Q_KILL_DAMAGE, arena_hero_armor(foe)));
            return 1;
        }
    }
    if (ally && ally->alive && ally->hp < ally->max_hp) {
        float dx = ally->x - dagda->x, dz = ally->z - dagda->z;
        if (sqrtf(dx * dx + dz * dz) <= ARENA_DAGDA_Q_RANGE) {
            ally->hp += ARENA_DAGDA_Q_REVIVE_HEAL;
            if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
            return 1;
        }
    }
    return 0;
}

/* dagda_cast_w: Uaithne, called by name -- all three master strains played
 * over the whole hall in one go. One AoE cast, everyone in radius
 * experiences a different strain depending on side: allies get joy (heal),
 * hittable enemies get sorrow+sleep (root+silence) at once. Always lands
 * and consumes the cooldown, same always-commits convention as other AoE
 * ultimates in this roster (Doc Wheel's R) -- a hall-filling cast isn't a
 * single-target poke that can whiff. */
static void dagda_cast_w(ArenaHero *dagda, int owner) {
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *other = &arena_state.heroes[i];
        if (i == owner || !other->active || !other->alive) continue;
        float dx = other->x - dagda->x, dz = other->z - dagda->z;
        if (sqrtf(dx * dx + dz * dz) > ARENA_DAGDA_W_RADIUS) continue;
        if (other->team == dagda->team) {
            other->hp += ARENA_DAGDA_W_ALLY_HEAL;
            if (other->hp > other->max_hp) other->hp = other->max_hp;
        } else if (hero_is_hittable(other)) {
            other->rooted_ms = ARENA_DAGDA_W_ROOT_MS;
            other->silenced_ms = ARENA_DAGDA_W_SILENCE_MS;
        }
    }
}

/* courier_cast_q: The Insult, Lightly Edited -- dashes a fixed distance
 * toward the nearest enemy (same shape as Unicorn's Diagnostic Charge:
 * fixed dash length, not clamped to the foe's own distance, so it can
 * overshoot past a close target same as Unicorn's does) and deals damage
 * if it lands within hit radius. Also cleanses The Courier's own active
 * debuffs on cast (Lightly Edited passive) -- "editing the message"
 * addressed back to him, regardless of whether the damage half connects.
 * Returns 1 if there was a living enemy to dash toward at all, 0 if not. */
static int courier_cast_q(ArenaHero *courier, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - courier->x, dz = foe->z - courier->z;
    float len = sqrtf(dx * dx + dz * dz);
    if (len > 0.01f) {
        float nx = courier->x + dx / len * ARENA_COURIER_Q_DASH_DIST;
        float nz = courier->z + dz / len * ARENA_COURIER_Q_DASH_DIST;
        if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
        if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
        if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
        if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
        courier->x = nx;
        courier->z = nz;
        courier->moving = 0;
    }
    float fdx = foe->x - courier->x, fdz = foe->z - courier->z;
    if (sqrtf(fdx * fdx + fdz * fdz) <= ARENA_COURIER_Q_HIT_RADIUS) {
        apply_damage(foe, apply_armor(ARENA_COURIER_Q_DAMAGE, arena_hero_armor(foe)));
    }
    courier->silenced_ms = 0;
    courier->rooted_ms = 0;
    return 1;
}

/* courier_toggle_w: Between Eagle and Serpent -- instantly repositions to
 * whichever map node is farthest from The Courier's current position,
 * always making real progress "along the tree" rather than bouncing back
 * and forth to the same one. Pure fixed-geography teleport, distinct from
 * every other hero's ally/foe-relative one. Always lands (there is always
 * at least one node) -- no whiff case. S170-119: generalized from a
 * hardcoded "farther of the two nodes" to farthest-of-N when the map grew
 * from 2 nodes to 5 -- the "always real progress" property holds the same
 * way for any N. */
static void courier_toggle_w(ArenaHero *courier) {
    int target = 0;
    float best_dist = -1.0f;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        float dx = arena_state.nodes[n].x - courier->x, dz = arena_state.nodes[n].z - courier->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist > best_dist) { best_dist = dist; target = n; }
    }
    courier->x = arena_state.nodes[target].x;
    courier->z = arena_state.nodes[target].z;
    courier->moving = 0;
}

/* courier_cast_r: The Debt Collector's Due -- a flat life-drain execute on
 * the nearest enemy in range. "A job that was never meant to involve
 * judgment, and has, over a very long tenure, started to" -- The Courier
 * takes a cut off what passes through him by force. Returns 1 if it
 * landed, 0 on a whiff. */
static int courier_cast_r(ArenaHero *courier, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - courier->x, dz = foe->z - courier->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_COURIER_R_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_COURIER_R_DRAIN, arena_hero_armor(foe)));
    courier->hp += ARENA_COURIER_R_DRAIN;
    if (courier->hp > courier->max_hp) courier->hp = courier->max_hp;
    return 1;
}

/* loki_cast_q: Interference, Not a Signal -- an instant positional swap with
 * the nearest enemy, no travel time (unlike every dash-shaped Q in this
 * file). Loki simply arrives where the enemy was and the enemy where he
 * was, then a small hit lands on arrival if the swap put them in range of
 * each other anyway. Returns 1 if there was a living enemy to swap with. */
static int loki_cast_q(ArenaHero *loki, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float ox = loki->x, oz = loki->z;
    loki->x = foe->x;
    loki->z = foe->z;
    foe->x = ox;
    foe->z = oz;
    loki->moving = 0;
    float fdx = foe->x - loki->x, fdz = foe->z - loki->z;
    if (sqrtf(fdx * fdx + fdz * fdz) <= ARENA_LOKI_Q_HIT_RADIUS) {
        apply_damage(foe, apply_armor(ARENA_LOKI_Q_DAMAGE, arena_hero_armor(foe)));
    }
    return 1;
}

/* loki_cast_r: Held For As Long As The Myth Demands -- self-cast survive
 * floor, same mechanic Pizza/Dagda already use (S170-46), reused here as
 * Sigyn's endurance rather than either of their reasons for it. */
static void loki_cast_r(ArenaHero *loki) {
    loki->survive_floor_ms = ARENA_LOKI_R_FLOOR_MS;
}

/* gary_cast_q: The Property -- a stationary long-range precision shot at the nearest enemy.
 * No dash, no movement at all (unlike every other Q in this file) -- Gary doesn't chase, he
 * watches from where he's standing. Range is longer while W (Watching the Bridge) is toggled
 * on. Returns 1 if a living enemy was in range, 0 on a whiff (range gates this one, not a
 * hit-radius after a dash, since there's no dash to begin with).
 *
 * S170-136: no longer an instant hit -- fires a real projectile straight at
 * the foe's position at cast time (see arena_spawn_projectile). The cast
 * still requires a hittable foe within range at the MOMENT of casting (Gary
 * has to actually have a shot lined up to fire at all -- he's not spraying
 * blind), but landing the hit is no longer guaranteed: the foe can step off
 * the line before the shot arrives. Cooldown is spent on cast either way,
 * same as a real skill-shot -- you don't get it back just because you
 * missed. */
static int gary_cast_q(ArenaHero *gary, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    /* S170-203: no longer w_active-conditional -- W stopped being a toggle that extends this
       range and became its own real ability (Aimed Shot), so Q is back to one fixed range. */
    float dx = foe->x - gary->x, dz = foe->z - gary->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_GARY_Q_RANGE) return 0;
    arena_spawn_projectile(gary->owner, gary->team, ARENA_HERO_GARY,
                           gary->x, gary->z, foe->x, foe->z,
                           ARENA_GARY_Q_PROJECTILE_SPEED, ARENA_GARY_Q_PROJECTILE_RADIUS,
                           ARENA_GARY_Q_DAMAGE, ARENA_GARY_Q_RANGE);
    return 1;
}

/* gary_cast_r: "Slow Down, This Isn't a Track Meet" -- a fixed-duration root on the nearest
 * enemy, the same "slow simplified to a full stop" convention Tree's R/Flamel's R already use
 * rather than adding a real speed-multiplier system. Returns 1 if it landed. */
static int gary_cast_r(ArenaHero *gary, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - gary->x, dz = foe->z - gary->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_GARY_R_RANGE) return 0;
    foe->rooted_ms = ARENA_GARY_R_ROOT_MS;
    return 1;
}

/* flute_debt_cast_q: The Wrong Note -- modest immediate damage plus the shared burning_ms/
 * burn_dps DoT fields (S170-46, already generically ticked by tick_hero_kit for any hero),
 * standing in for the debt accruing. Returns 1 if it landed. */
static int flute_debt_cast_q(ArenaHero *fd, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - fd->x, dz = foe->z - fd->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_FLUTE_DEBT_Q_HIT_RADIUS) return 0;
    apply_damage(foe, apply_armor(ARENA_FLUTE_DEBT_Q_DAMAGE, arena_hero_armor(foe)));
    foe->burning_ms = ARENA_FLUTE_DEBT_Q_BURN_MS;
    foe->burn_dps = ARENA_FLUTE_DEBT_Q_BURN_DPS;
    return 1;
}

/* flute_debt_cast_r: Eventually Collects -- always lands and consumes the cooldown (same
 * "always commits" convention as Doc Wheel's/Flamel's R), but deals real bonus damage if the
 * target still has the Q's debt (burning_ms > 0) active, base damage otherwise. The actual
 * payoff of the kit's whole theme: the debt has to still be open for it to collect big. */
static void flute_debt_cast_r(ArenaHero *fd, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return;
    float dx = foe->x - fd->x, dz = foe->z - fd->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_FLUTE_DEBT_R_RANGE) return;
    int amount = (foe->burning_ms > 0) ? ARENA_FLUTE_DEBT_R_DAMAGE_DEBT : ARENA_FLUTE_DEBT_R_DAMAGE_BASE;
    apply_damage(foe, apply_armor(amount, arena_hero_armor(foe)));
}

/* bacon_puck_cast_q: Ask Again Later -- self intangible_ms, the shared can't-be-hit status
 * (S170-32). Always "lands" (there's no foe/range check -- it's purely self-targeted), same as
 * Ghost's W/Frog's R.
 *
 * Real, honest simplification (2026-08-26, W redesign to Shadow Step, see bacon_puck_cast_w's
 * own doc comment): this used to grant a LONGER intangible duration while W was toggled on
 * (ARENA_BACON_PUCK_Q_INTANGIBLE_MS_WATCHING) -- W is no longer a toggle at all (an instant
 * blink now, doesn't touch w_active), so that longer duration is dead: w_active permanently
 * reads 0 for this hero now. Always uses the base duration -- not compensated with a buff
 * elsewhere, a real, accepted power change from losing the old toggle's own utility, not
 * silently patched over. ARENA_BACON_PUCK_Q_INTANGIBLE_MS_WATCHING itself is left defined,
 * unused, matching this session's own "leave dead constants in place, don't rip out" convention
 * for redesigned abilities. */
static void bacon_puck_cast_q(ArenaHero *bp) {
    bp->intangible_ms = ARENA_BACON_PUCK_Q_INTANGIBLE_MS;
}

/* bacon_puck_cast_w: Shadow Step (2026-08-26 redesign) -- see ARENA_BACON_PUCK_W_RANGE's own
 * header comment for the full founder-quote chain. Reads the real target hero from
 * arena_state.hover_target[owner] (set by arena_set_hover_target right before dispatch, same
 * generic "record right before dispatch" convention every hover-consulted ability already
 * uses) -- an untargeted W (no hero hovered when the click confirmed) is a real no-op, same
 * "real commitment" shape every other kit piece in this file holds itself to. Lands the caster
 * a short, real distance PAST the target's own position, along the target's own real
 * facing_rad (S202-40) -- "roughly behind them" from the target's own perspective, not the
 * caster's approach angle. Instant, no windup -- a blink, not a cast-time ability, same shape
 * Blink Dagger's own arena_use_blink already established (clamp to map bounds, no interrupt
 * concept needed since there's no time window to interrupt). */
static void bacon_puck_cast_w(int owner) {
    ArenaHero *bp = &arena_state.heroes[owner];
    if (bp->w_cooldown_ms > 0 || bp->mp < ARENA_MP_COST_W) return;
    int target_idx = arena_state.hover_target[owner];
    if (target_idx < 0 || target_idx >= ARENA_MAX_HEROES) return;
    ArenaHero *target = &arena_state.heroes[target_idx];
    if (!target->active || target->team == bp->team || !hero_is_hittable(target)) return;
    float dx = target->x - bp->x, dz = target->z - bp->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_BACON_PUCK_W_RANGE) return;

    float bx = target->x + sinf(target->facing_rad) * ARENA_BACON_PUCK_W_BEHIND_OFFSET;
    float bz = target->z + cosf(target->facing_rad) * ARENA_BACON_PUCK_W_BEHIND_OFFSET;
    if (bx < -ARENA_HALF_EXTENT) bx = -ARENA_HALF_EXTENT;
    if (bx > ARENA_HALF_EXTENT) bx = ARENA_HALF_EXTENT;
    if (bz < -ARENA_HALF_EXTENT) bz = -ARENA_HALF_EXTENT;
    if (bz > ARENA_HALF_EXTENT) bz = ARENA_HALF_EXTENT;
    bp->x = bx;
    bp->z = bz;
    bp->w_cooldown_ms = cast_cooldown(bp, ARENA_BACON_PUCK_W_COOLDOWN_MS);
    bp->mp -= ARENA_MP_COST_W;
}

/* bacon_puck_cast_r: The Trick Was Always the Same -- real damage plus a self-heal off a
 * fraction of it. Returns 1 if it landed. */
static int bacon_puck_cast_r(ArenaHero *bp, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - bp->x, dz = foe->z - bp->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_BACON_PUCK_R_RANGE) return 0;
    int dmg = apply_armor(ARENA_BACON_PUCK_R_DAMAGE, arena_hero_armor(foe));
    apply_damage(foe, dmg);
    bp->hp += (int)(dmg * ARENA_BACON_PUCK_R_HEAL_PCT);
    if (bp->hp > bp->max_hp) bp->hp = bp->max_hp;
    return 1;
}

/* abraham_cast_q: The Sacred Magic -- a real ranged magic bolt. Used to be
 * stronger while W (channeling the book) was toggled on; W's own toggle is
 * gone as of S202-34 (replaced by A Line of Fire, see that ability's own
 * doc comment on ARENA_ABRAHAM_FIREBALL_DAMAGE), so Q now always deals the
 * old "channeling" damage value -- a deliberate net-buff rather than
 * silently leaving Q worse off with no way to ever reach its old ceiling.
 * Returns 1 if it landed. */
static int abraham_cast_q(ArenaHero *abraham, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - abraham->x, dz = foe->z - abraham->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_ABRAHAM_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_ABRAHAM_Q_DAMAGE, arena_hero_armor(foe)));
    return 1;
}

/* abraham_cast_r: The Guardian Angel, Contacted -- a full self-cleanse (every debuff field
 * this roster tracks) plus a real heal, the ritual's actual promised payoff. Always "lands"
 * (self-targeted, no foe check) -- same always-commits convention as Doc Wheel's/Flamel's R. */
static void abraham_cast_r(ArenaHero *abraham) {
    abraham->silenced_ms = 0;
    abraham->rooted_ms = 0;
    abraham->burning_ms = 0;
    abraham->burn_dps = 0;
    abraham->burn_tick_ms = 0;
    abraham->hp += ARENA_ABRAHAM_R_HEAL;
    if (abraham->hp > abraham->max_hp) abraham->hp = abraham->max_hp;
}

/* ada_cast_q: computes the nearest enemy's movement to a halt -- a real root, same "slow
 * simplified to a stop" convention Tree's/Flamel's/Gary's R already use, here on Q instead.
 * Returns 1 if it landed. */
static int ada_cast_q(ArenaHero *ada, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - ada->x, dz = foe->z - ada->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_ADA_Q_RANGE) return 0;
    foe->rooted_ms = ARENA_ADA_Q_ROOT_MS;
    return 1;
}

/* ada_cast_r: The First Program, Run a Century Late -- the engine finally executes: real
 * damage plus a short follow-up root. Returns 1 if it landed. */
static int ada_cast_r(ArenaHero *ada, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - ada->x, dz = foe->z - ada->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_ADA_R_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_ADA_R_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_ADA_R_ROOT_MS;
    return 1;
}

/* tyler_cast_q: Earthbind -- roots + a DoT (Geostrike's poison, folded in here since there's
 * no generic per-melee-attack passive hook to hang it off separately). Returns 1 if it landed. */
static int tyler_cast_q(ArenaHero *tyler, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - tyler->x, dz = foe->z - tyler->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_TYLER_Q_RANGE) return 0;
    ArenaProjectile *p = arena_spawn_projectile(tyler->owner, tyler->team, ARENA_HERO_TYLER,
                             tyler->x, tyler->z, foe->x, foe->z,
                             ARENA_TYLER_Q_PROJECTILE_SPEED, ARENA_TYLER_Q_PROJECTILE_RADIUS,
                             ARENA_TYLER_Q_DAMAGE, ARENA_TYLER_Q_RANGE);
    if (p) {
        p->on_hit_root_ms = ARENA_TYLER_Q_ROOT_MS;
        p->on_hit_burn_ms = ARENA_TYLER_Q_BURN_MS;
        p->on_hit_burn_dps = ARENA_TYLER_Q_BURN_DPS;
    }
    return 1;
}

/* tyler_cast_w: Poof -- an instant blink to the nearest enemy plus real damage on arrival.
 * S170-170, "true meepo parity" follow-up (docs/HEROES_VS0.md's own S170-141 scope note: "W
 * (Poof) still moves only Tyler's own body, not the whole clone army teleporting together -- a
 * real next step, not attempted this pass"): every active clone linked to this Tyler
 * (`clone_owner`) now teleports alongside him to the exact same point and independently lands
 * its own arrival-damage check against the same target -- the original design's "TYLER and
 * every active clone teleport to the target point" (docs/HEROES_VS0.md), simplified the same
 * honest way the rest of this kit already is: single-target instant damage on arrival, not a
 * true two-point AoE at both the departure and landing spot. Concentrates the whole clone
 * army's arrival damage onto the one enemy Tyler jumped to -- the actual "full-team dive tool"
 * identity the original design names, just expressed through this engine's existing simplified
 * hit model rather than a new AoE system. Returns 1 if a living enemy was there to blink to. */
static int tyler_cast_w(ArenaHero *tyler, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    int tyler_owner = (int)(tyler - arena_state.heroes);

    tyler->x = foe->x;
    tyler->z = foe->z;
    tyler->moving = 0;
    if (hero_is_hittable(foe)) {
        apply_damage(foe, apply_armor(ARENA_TYLER_W_DAMAGE, arena_hero_armor(foe)));
    }

    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *clone = &arena_state.heroes[i];
        if (!clone->active || !clone->alive || clone->clone_owner != tyler_owner) continue;
        clone->x = foe->x;
        clone->z = foe->z;
        clone->moving = 0;
        if (hero_is_hittable(foe)) {
            apply_damage(foe, apply_armor(ARENA_TYLER_W_DAMAGE, arena_hero_armor(foe)));
        }
    }
    return 1;
}

/* tyler_spawn_clones (S170-141): claims up to ARENA_TYLER_R_CLONE_COUNT free
 * slots from the dedicated puppet-clone range (ARENA_MAX_HEROES..
 * ARENA_HEROES_ARRAY_SIZE-1 -- never a real client's slot, see that
 * constant's own doc comment) and spawns each as a real, fightable
 * ArenaHero at Tyler's own position, sharing his team and hero_id (so it
 * renders identically to Tyler client-side, no new visual needed). Spawns
 * fewer than the full count if the pool is short on free slots rather than
 * refusing the whole cast -- same "generous headroom, graceful if it's ever
 * tight" tone as arena_spawn_projectile's own pool-exhaustion handling. */
static void tyler_spawn_clones(ArenaHero *tyler) {
    int tyler_owner = (int)(tyler - arena_state.heroes);
    int spawned = 0;
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE && spawned < ARENA_TYLER_R_CLONE_COUNT; i++) {
        ArenaHero *clone = &arena_state.heroes[i];
        if (clone->active) continue;
        memset(clone, 0, sizeof(*clone));
        clone->active = 1;
        clone->alive = 1;
        clone->is_clone = 1;
        clone->clone_owner = tyler_owner;
        clone->team = tyler->team;
        clone->hero_id = ARENA_HERO_TYLER;
        clone->owner = i;
        clone->x = clone->target_x = tyler->x;
        clone->z = clone->target_z = tyler->z;
        clone->max_hp = (int)(tyler->max_hp * ARENA_TYLER_CLONE_HP_PCT);
        clone->hp = clone->max_hp;
        spawned++;
    }
}

/* tyler_cast_r: Divided We Stand. S170-141: real puppet clones on top of the
 * existing self-buff (see docs/HEROES_VS0.md's Tyler section for the full
 * design/scope note) -- hits hard right now, stays more fragile (own armor
 * goes negative -- see arena_hero_armor()) for the window after, AND
 * spawns the clone army. Always "lands" (self-buff + clones) even on a
 * whiff against the foe check, same convention as before. */
static void tyler_cast_r(ArenaHero *tyler, ArenaHero *foe) {
    if (hero_is_hittable(foe)) {
        float dx = foe->x - tyler->x, dz = foe->z - tyler->z;
        if (sqrtf(dx * dx + dz * dz) <= ARENA_TYLER_R_RANGE) {
            apply_damage(foe, apply_armor(ARENA_TYLER_R_DAMAGE, arena_hero_armor(foe)));
        }
    }
    tyler->r_active_ms = ARENA_TYLER_R_VULNERABLE_MS;
    tyler_spawn_clones(tyler);
}

/* paimon_cast_q: Teaches All Arts -- a ranged bolt that damages and roots, same instant-hit-if-
 * in-range simplification as Ghost's/Tree's/Flamel's Q. Returns 1 if it landed. */
static int paimon_cast_q(ArenaHero *paimon, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - paimon->x, dz = foe->z - paimon->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_PAIMON_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_PAIMON_Q_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_PAIMON_Q_ROOT_MS;
    return 1;
}

/* paimon_cast_w: Speaks With Total Authority -- an instant decree, damage + silence, same shape
 * as Ghost's Q but on the W slot with its own cooldown. Returns 1 if it landed. */
static int paimon_cast_w(ArenaHero *paimon, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - paimon->x, dz = foe->z - paimon->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_PAIMON_W_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_PAIMON_W_DAMAGE, arena_hero_armor(foe)));
    foe->silenced_ms = ARENA_PAIMON_W_SILENCE_MS;
    return 1;
}

/* noor1_cast_q: File What Is Actually There -- a ranged bolt that damages and roots, same
 * instant-hit-if-in-range shape as Paimon's/Ghost's/Tree's Q. Returns 1 if it landed. */
static int noor1_cast_q(ArenaHero *noor1, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - noor1->x, dz = foe->z - noor1->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_NOOR1_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_NOOR1_Q_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_NOOR1_Q_ROOT_MS;
    return 1;
}

/* cain_cast_q: The First Murder -- instant hit-if-in-range, execute-scaled via
 * execute_scale_damage, same shape as Morrigan's Q. Returns 1 if it landed. */
static int cain_cast_q(ArenaHero *cain, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - cain->x, dz = foe->z - cain->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_CAIN_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(execute_scale_damage(foe, ARENA_CAIN_Q_DAMAGE_BASE, ARENA_CAIN_Q_DAMAGE_LOW_HP),
                                   arena_hero_armor(foe)));
    return 1;
}

/* cain_cast_w: Cursed to Wander -- dashes a fixed distance directly AWAY from the nearest enemy
 * (the mirror of Courier's Q, which dashes toward) and cleanses self debuffs, same self-cleanse
 * as Courier's own Q. Works even with no foe present (still cleanses, just doesn't reposition) --
 * always returns 1, this is a self-only effect that can't whiff the way a targeted cast can. */
static int cain_cast_w(ArenaHero *cain, ArenaHero *foe) {
    if (foe) {
        float dx = cain->x - foe->x, dz = cain->z - foe->z;
        float len = sqrtf(dx * dx + dz * dz);
        if (len > 0.01f) {
            float nx = cain->x + dx / len * ARENA_CAIN_W_DASH_DIST;
            float nz = cain->z + dz / len * ARENA_CAIN_W_DASH_DIST;
            if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
            if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
            if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
            if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
            cain->x = nx;
            cain->z = nz;
            cain->moving = 0;
        }
    }
    cain->silenced_ms = 0;
    cain->rooted_ms = 0;
    return 1;
}

/* gunnr_cast_q: Argued With a Raven -- a plain melee-range correction, damage only, no status
 * effect. Returns 1 if it landed. */
static int gunnr_cast_q(ArenaHero *gunnr, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - gunnr->x, dz = foe->z - gunnr->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_GUNNR_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_GUNNR_Q_DAMAGE, arena_hero_armor(foe)));
    return 1;
}

/* warrior_cast_q: Hard Slash -- real DragonsNShit Great Sword weapon skill (Scission), plain
 * melee-range damage, same shape as Gunnr's Q. Routes through apply_weapon_skill_damage (not a
 * bare apply_damage/apply_armor pair) so it can open/close a real skillchain window on its
 * target (Milestone 2). Returns 1 if it landed. */
static int warrior_cast_q(ArenaHero *warrior, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - warrior->x, dz = foe->z - warrior->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_WARRIOR_Q_RANGE) return 0;
    static const ArenaResonance attrs[] = { ARENA_RESONANCE_SCISSION };
    apply_weapon_skill_damage(warrior, foe, ARENA_WARRIOR_Q_DAMAGE, attrs, 1);
    return 1;
}

/* warrior_cast_w: Power Slash -- real DragonsNShit Great Sword weapon skill (Transfixion), a
 * harder melee-range hit than Hard Slash on a longer cooldown, same real FFXI mid-tier WS
 * progression. Returns 1 if it landed. */
static int warrior_cast_w(ArenaHero *warrior, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - warrior->x, dz = foe->z - warrior->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_WARRIOR_W_RANGE) return 0;
    static const ArenaResonance attrs[] = { ARENA_RESONANCE_TRANSFIXION };
    apply_weapon_skill_damage(warrior, foe, ARENA_WARRIOR_W_DAMAGE, attrs, 1);
    return 1;
}

/* warrior_cast_r: Frostbite -- real DragonsNShit Great Sword weapon skill, dual resonance
 * (Induration+Reverberation), the hardest of Warrior's three real weapon skills on the longest
 * cooldown -- the real FFXI GSword finisher WS. Returns 1 if it landed. */
static int warrior_cast_r(ArenaHero *warrior, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - warrior->x, dz = foe->z - warrior->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_WARRIOR_R_RANGE) return 0;
    static const ArenaResonance attrs[] = { ARENA_RESONANCE_INDURATION, ARENA_RESONANCE_REVERBERATION };
    apply_weapon_skill_damage(warrior, foe, ARENA_WARRIOR_R_DAMAGE, attrs, 2);
    return 1;
}

/* cart_cast_q: minimal self-maintenance heal -- the Cart's own lore (TYLER multiverse_heroes.md
 * #10) isn't a combatant, so Q stays deliberately small rather than padded out with an invented
 * attack. Always succeeds (no target/range gate -- there's nothing to miss). */
static void cart_cast_q(ArenaHero *cart) {
    cart->hp += ARENA_CART_Q_HEAL;
    if (cart->hp > cart->max_hp) cart->hp = cart->max_hp;
}

/* arena_fibonacci / arena_marble_bag_pick: see header doc comments. First real implementation
 * of NORTHSTAR's own long-documented weighted-marble-bag-plus-Fibonacci-pity pull algorithm
 * anywhere in this repo (S202-09/S202-42) -- generic, not Cart-specific, per that doc's own
 * "worth building once as a shared utility" note. */
int arena_fibonacci(int n) {
    if (n <= 1) return 1; /* fib(0)=fib(1)=1, not the textbook fib(0)=0 -- a fresh/reset pity
                              counter still carries its real base weight, never zeroed out */
    int a = 1, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int arena_marble_bag_pick(const int *weights, int *pity, int n) {
    if (n <= 0) return -1;
    long total = 0;
    for (int i = 0; i < n; i++) {
        int tier = pity[i] > ARENA_MARBLE_BAG_MAX_PITY_TIER ? ARENA_MARBLE_BAG_MAX_PITY_TIER : pity[i];
        total += (long)weights[i] * arena_fibonacci(tier);
    }
    if (total <= 0) return -1; /* every effective weight is 0 -- caller error, not a real pick */
    long roll = (long)(rand() % total);
    long cum = 0;
    int picked = n - 1; /* fallback for the very top of the range */
    for (int i = 0; i < n; i++) {
        int tier = pity[i] > ARENA_MARBLE_BAG_MAX_PITY_TIER ? ARENA_MARBLE_BAG_MAX_PITY_TIER : pity[i];
        cum += (long)weights[i] * arena_fibonacci(tier);
        if (roll < cum) {
            picked = i;
            break;
        }
    }
    for (int i = 0; i < n; i++) pity[i] = (i == picked) ? 0 : pity[i] + 1;
    return picked;
}

/* cart_apply_delivery_outcome: applies exactly one real delivery outcome
 * (ARENA_CART_DELIVERY_OUTCOME_*) to `target` -- split out from cart_trigger_delivery so each
 * outcome is directly, deterministically testable (forcing an index) rather than only reachable
 * through many rand()-driven rolls, same "test the real mechanic, not just hope RNG cooperates"
 * discipline this file already holds itself to elsewhere. */
static void cart_apply_delivery_outcome(ArenaHero *target, int outcome) {
    switch (outcome) {
    case ARENA_CART_DELIVERY_OUTCOME_HEAL: {
        int heal = (int)(target->max_hp * ARENA_CART_DELIVERY_HEAL_PCT);
        target->hp += heal;
        if (target->hp > target->max_hp) target->hp = target->max_hp;
        break;
    }
    case ARENA_CART_DELIVERY_OUTCOME_MANA: {
        int mana = (int)(target->max_mp * ARENA_CART_DELIVERY_MANA_PCT);
        target->mp += mana;
        if (target->mp > target->max_mp) target->mp = target->max_mp;
        break;
    }
    case ARENA_CART_DELIVERY_OUTCOME_SLOW:
        arena_apply_slow(target->owner, ARENA_CART_DELIVERY_SLOW_MS, ARENA_CART_DELIVERY_SLOW_PCT);
        break;
    case ARENA_CART_DELIVERY_OUTCOME_FLOW:
        target->flow += ARENA_CART_DELIVERY_FLOW;
        target->flow_earned += ARENA_CART_DELIVERY_FLOW;
        break;
    case ARENA_CART_DELIVERY_OUTCOME_KING_BUFF:
        /* Growth (flat AD stack, ARENA_KING_GROWTH_AD_PER_STACK) -- the simplest, most
           self-contained King buff to grant outside its own real kill-a-King flow (no aura/
           team-wide complexity like Wealth/All-Seeing) -- founder's own literal example for
           the "general random-buff system... a random hero occasionally gets a King buff" ask.
           Stacks with a real king-earned Growth if the target already has one (same "just
           extend the duration" idiom the real King-kill path already uses), rather than a
           separate parallel buff slot. */
        if (target->king_growth_stacks < 1) target->king_growth_stacks = 1;
        target->king_growth_ms = ARENA_KING_GROWTH_DURATION_MS;
        break;
    }
}

/* W: the frequent, mundane roll. R: "bigger... BETTER-WEIGHTED" per ARENA_CART_Q_HEAL's own
 * doc comment block, a real intent stated but never actually built until now -- slow halved,
 * the King-buff outcome tripled relative to W. */
static const int ARENA_CART_DELIVERY_W_WEIGHTS[ARENA_CART_DELIVERY_OUTCOME_COUNT] = { 3, 3, 2, 3, 1 };
static const int ARENA_CART_DELIVERY_R_WEIGHTS[ARENA_CART_DELIVERY_OUTCOME_COUNT] = { 3, 3, 1, 3, 3 };

/* cart_trigger_delivery (NORTHSTAR §24 Milestone 2): the Cart's real signature mechanic --
 * "a requested document turns out to already be waiting on the cart, with no requester logged,"
 * and "nobody, including its own controller, gets to request what." Picks one real outcome via
 * arena_marble_bag_pick (S202-42) onto `target` (which may be an ally, the Cart's own
 * controller, or an enemy -- whoever steps into the zone first, no team check, matching the
 * lore's own unpredictability) -- W and R now genuinely weight differently (see the tables
 * above), and pity is tracked per-caster (`caster->cart_delivery_pity`) so a Cart player's own
 * bad-outcome streak gets rarer for THEM specifically. Which weight table applies is read off
 * `caster->zone_radius` (ARENA_CART_R_RADIUS vs. the W default) rather than adding a new
 * separate "which slot" field -- the two constants are already distinct and this is the same
 * value tick_hero_kit's own CART case already uses to size the zone. Called once per zone, from
 * tick_hero_kit -- the caller is responsible for deactivating the zone afterward so it only
 * fires once. */
static void cart_trigger_delivery(ArenaHero *caster, ArenaHero *target) {
    const int *weights = (caster->zone_radius == ARENA_CART_R_RADIUS) ? ARENA_CART_DELIVERY_R_WEIGHTS : ARENA_CART_DELIVERY_W_WEIGHTS;
    int outcome = arena_marble_bag_pick(weights, caster->cart_delivery_pity, ARENA_CART_DELIVERY_OUTCOME_COUNT);
    if (outcome < 0) return; /* defensive -- every real weight above is positive, should never happen */
    cart_apply_delivery_outcome(target, outcome);
}

/* vassago_cast_q: Reveal the Gentle Maybe -- a ranged bolt, damage + silence, same shape as
 * Ghost's Q. Returns 1 if it landed. */
static int vassago_cast_q(ArenaHero *vassago, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - vassago->x, dz = foe->z - vassago->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_VASSAGO_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_VASSAGO_Q_DAMAGE, arena_hero_armor(foe)));
    foe->silenced_ms = ARENA_VASSAGO_Q_SILENCE_MS;
    return 1;
}

/* he_xiangu_cast_q: Moira Orb redesign (2026-08-26) -- see arena_game.h's own doc comment on
 * ARENA_HE_XIANGU_Q_ORB_SPEED for the full founder-quote chain. No longer an instant hitscan
 * bolt against the passed-in `foe` -- auto-targets the nearest enemy anywhere on the map
 * (arena_nearest_enemy has no range cap), same auto-target reuse Abraham's own W redesign
 * established, and spawns a real, slower, HOMING projectile (arena_spawn_projectile +
 * homing_target, same mechanic Gary/Abraham's own ranged auto-attacks already use) instead of
 * resolving damage instantly. Self-heal fires at cast time, a real, deliberate simplification
 * (see the header comment) rather than new on-hit-heal plumbing. Returns 1 if a real orb was
 * fired. */
static int he_xiangu_cast_q(ArenaHero *he_xiangu, ArenaHero *foe) {
    (void)foe; /* the old single-target hitscan parameter -- unused now, auto-targets instead */
    ArenaHero *target = arena_nearest_enemy(he_xiangu->owner);
    if (!target) return 0;
    ArenaProjectile *shot = arena_spawn_projectile(he_xiangu->owner, he_xiangu->team, ARENA_HERO_HE_XIANGU,
        he_xiangu->x, he_xiangu->z, target->x, target->z,
        ARENA_HE_XIANGU_Q_ORB_SPEED, ARENA_HE_XIANGU_Q_ORB_RADIUS,
        ARENA_HE_XIANGU_Q_ORB_DAMAGE, ARENA_HE_XIANGU_Q_ORB_MAX_RANGE);
    if (!shot) return 0;
    shot->homing_target = target->owner;
    he_xiangu->hp += ARENA_HE_XIANGU_Q_ORB_SELF_HEAL;
    if (he_xiangu->hp > he_xiangu->max_hp) he_xiangu->hp = he_xiangu->max_hp;
    return 1;
}

/* beleth_cast_q: a ranged bolt + burn, same shape as Pizza's Q (S170-46) -- damage that keeps
 * paying out after contact. Returns 1 if it landed. */
static int beleth_cast_q(ArenaHero *beleth, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - beleth->x, dz = foe->z - beleth->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_BELETH_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_BELETH_Q_DAMAGE, arena_hero_armor(foe)));
    foe->burning_ms = ARENA_BELETH_Q_BURN_MS;
    foe->burn_dps = ARENA_BELETH_Q_BURN_DPS;
    return 1;
}

/* beleth_cast_w: an instant decree, same in-range shape as Paimon's Speaks With Total
 * Authority (S170-55) but silence-only, no damage component -- pure escalation-denial.
 * Returns 1 if it landed. */
static int beleth_cast_w(ArenaHero *beleth, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - beleth->x, dz = foe->z - beleth->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_BELETH_W_RANGE) return 0;
    foe->silenced_ms = ARENA_BELETH_W_SILENCE_MS;
    return 1;
}

/* mnm_cast_q: a melee-range clamp+damage, same shape as Paimon's Q. Returns 1 if it landed. */
/* weatherman_cast_q: Barometric Shove -- a ranged wind gust, displacement-only, no damage (the
 * first push-outward Q on this roster; Duck's own Q/R pull inward). Same rooted_ms-immune-to-
 * displacement precedent duck_pull_foe already established -- the push is skipped if the foe is
 * rooted, but the cast still counts as landing (returns 1, cooldown spent) since it required a
 * real target in range to fire at all. */
static int weatherman_cast_q(ArenaHero *wm, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - wm->x, dz = foe->z - wm->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > ARENA_WEATHERMAN_Q_RANGE) return 0;
    if (dist > 0.01f && foe->rooted_ms <= 0) {
        float push = ARENA_WEATHERMAN_Q_KNOCKBACK_DIST;
        float nx = foe->x + dx / dist * push;
        float nz = foe->z + dz / dist * push;
        if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
        if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
        if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
        if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
        foe->x = nx;
        foe->z = nz;
    }
    return 1;
}

/* weatherman_cast_w: Collects On What's Owed -- NORTHSTAR §16.3's own specific Donkey
 * interaction, "same zone, opposite effect depending on team" precedent Ghost's Recital already
 * set, applied to a targeted cast instead of a zone. Checks the nearest enemy first (grounds an
 * airborne one -- "the debt catches up to you no matter how far you fly"), then the nearest
 * ally (extends an already-gliding one instead -- a tailwind, not a headwind). Whiffs (no
 * cooldown consumed) if neither is currently mid-glide at all, the overwhelmingly common case
 * since Paper Glide is a rare escape trigger, not a constant state -- same "whiffed cast costs
 * nothing" convention every other conditional W on this roster already follows. */
static int weatherman_cast_w(ArenaHero *wm, ArenaHero *foe, ArenaHero *ally) {
    if (foe && foe->donkey_airborne_ms > 0) {
        float dx = foe->x - wm->x, dz = foe->z - wm->z;
        if (sqrtf(dx * dx + dz * dz) <= ARENA_WEATHERMAN_W_RANGE) {
            foe->donkey_airborne_ms = 0;
            foe->intangible_ms = 0; /* grounded -- ends the untargetable glide window too */
            return 1;
        }
    }
    if (ally && ally->donkey_airborne_ms > 0) {
        float dx = ally->x - wm->x, dz = ally->z - wm->z;
        if (sqrtf(dx * dx + dz * dz) <= ARENA_WEATHERMAN_W_RANGE) {
            ally->donkey_airborne_ms += ARENA_DONKEY_GLIDE_DURATION_MS;
            ally->intangible_ms += ARENA_DONKEY_GLIDE_DURATION_MS;
            /* "extends the glide's remaining airborne duration and travel distance" -- pushes
               the ally's existing move target further along the same direction they're already
               gliding, reusing ARENA_DONKEY_GLIDE_RANGE rather than a second, one-off distance
               constant. If they've already physically arrived (moving == 0, e.g. the glide's
               own duration outlasted the actual travel), only the duration half applies --
               there's no remaining direction to extend a finished trip along. */
            if (ally->moving) {
                float gdx = ally->target_x - ally->x, gdz = ally->target_z - ally->z;
                float glen = sqrtf(gdx * gdx + gdz * gdz);
                if (glen > 0.01f) {
                    float nx = ally->target_x + gdx / glen * ARENA_DONKEY_GLIDE_RANGE;
                    float nz = ally->target_z + gdz / glen * ARENA_DONKEY_GLIDE_RANGE;
                    if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
                    if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
                    if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
                    if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
                    ally->target_x = nx;
                    ally->target_z = nz;
                }
            }
            return 1;
        }
    }
    return 0;
}

static int mnm_cast_q(ArenaHero *mnm, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - mnm->x, dz = foe->z - mnm->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_MNM_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_MNM_Q_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_MNM_Q_ROOT_MS;
    return 1;
}

/* zagan_cast_q (S170-230, Calcination): a single upfront hit plus a lingering armor-shred
 * debuff -- see zagan_calcination_ms's own struct doc comment and arena_hero_armor for where
 * the shred is actually applied. Deliberately no periodic burn damage on top -- this is a
 * control/setup tool for W/R, not a DPS ability. */
static int zagan_cast_q(ArenaHero *zagan, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - zagan->x, dz = foe->z - zagan->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_ZAGAN_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_ZAGAN_Q_DAMAGE, arena_hero_armor(foe)));
    foe->zagan_calcination_ms = ARENA_ZAGAN_Q_DURATION_MS;
    return 1;
}

/* zagan_cast_w (S170-230, The Standstill): the literal mechanical translation of "Standstill's
 * Confessor" -- forces stillness onto an enemy. This roster's first-ever kit to call
 * arena_apply_stun() (the generic infrastructure has existed since S170-184; no kit used it
 * until now). */
static int zagan_cast_w(ArenaHero *zagan, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - zagan->x, dz = foe->z - zagan->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_ZAGAN_W_RANGE) return 0;
    arena_apply_stun(foe->owner, ARENA_ZAGAN_W_STUN_MS);
    return 1;
}

void arena_cast_q(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    ArenaHero *foe = arena_nearest_enemy(owner);
    if (!h->alive || h->silenced_ms > 0 || h->stunned_ms > 0 || h->q_cooldown_ms > 0 || h->mp < ARENA_MP_COST_Q) return; /* S170-184: stun blocks all three action types, silence just casting */
    h->cast_flash_slot = 1;

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        unicorn_cast_q(h, foe);
        break;
    case ARENA_HERO_DUCK:
        if (duck_pull_foe(h, foe, ARENA_DUCK_Q_PULL_DIST, ARENA_DUCK_Q_DAMAGE, ARENA_DUCK_Q_RANGE)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_DUCK_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_GHOST:
        if (ghost_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_GHOST_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_FROG:
        frog_cast_q(h);
        h->q_cooldown_ms = cast_cooldown(h, ARENA_FROG_Q_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_Q;
        break;
    case ARENA_HERO_DOC_WHEEL: {
        /* Bedside Manner: single-target heal + cleanse. S170-143 ("add
           hover casting like in wow macros for healing start with doc
           wheel"): now prefers whoever the caster was hovering at cast time
           (a real WoW-macro mouseover heal) over the old always-nearest-
           ally default -- arena_hover_ally_or_nearest falls back to the
           exact same arena_nearest_ally() behavior when nothing's hovered
           or the hover target isn't a valid ally, so this is additive, not
           a behavior change for anyone not using the new targeting. No
           ally (1v1, or ally already dead) -- no-op, cooldown not consumed,
           same "whiff doesn't cost you the cooldown" convention as
           Duck/Ghost's Q. */
        ArenaHero *ally = arena_hover_ally_or_nearest(owner);
        if (ally && ally->alive) {
            doc_wheel_heal_and_cleanse(ally, doc_wheel_heal_amount(ally));
            h->q_cooldown_ms = cast_cooldown(h, ARENA_DOC_WHEEL_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    }
    case ARENA_HERO_TREE:
        if (tree_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_TREE_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_PIZZA:
        if (pizza_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_PIZZA_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_FLAMEL:
        if (flamel_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_FLAMEL_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_MORRIGAN:
        if (morrigan_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_MORRIGAN_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_DAGDA:
        if (dagda_cast_q(h, foe, arena_nearest_ally(owner))) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_DAGDA_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_COURIER:
        if (courier_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_COURIER_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_LOKI:
        if (loki_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_LOKI_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_GARY:
        if (gary_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_GARY_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_FLUTE_DEBT:
        if (flute_debt_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_FLUTE_DEBT_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_BACON_PUCK:
        bacon_puck_cast_q(h);
        h->q_cooldown_ms = cast_cooldown(h, ARENA_BACON_PUCK_Q_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_Q;
        break;
    case ARENA_HERO_ABRAHAM:
        if (abraham_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_ABRAHAM_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_ADA:
        if (ada_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_ADA_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_TYLER:
        if (tyler_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_TYLER_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_PAIMON:
        if (paimon_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_PAIMON_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_NOOR1:
        if (noor1_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_NOOR1_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_CAIN:
        if (cain_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_CAIN_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_GUNNR:
        if (gunnr_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_GUNNR_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_VASSAGO:
        if (vassago_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_VASSAGO_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_HE_XIANGU:
        if (he_xiangu_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_HE_XIANGU_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_BELETH:
        if (beleth_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_BELETH_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_MNM:
        if (mnm_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_MNM_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_WEATHERMAN:
        if (weatherman_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_WEATHERMAN_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_ZAGAN:
        if (zagan_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_ZAGAN_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_WARRIOR:
        if (warrior_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_WARRIOR_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_CART:
        cart_cast_q(h);
        h->q_cooldown_ms = cast_cooldown(h, ARENA_CART_Q_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_Q;
        break;
    }
}

void arena_toggle_w(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    if (!h->alive || h->silenced_ms > 0 || h->stunned_ms > 0) return; /* S170-184: stun blocks all three action types, silence just casting */
    /* w_cooldown_ms is 0 (and never touched) for the pure-toggle heroes
       below, so this passes for them unconditionally -- correctly gates
       only the instant-cast-with-cooldown heroes (Ghost, Tyler, Paimon,
       etc.), whose own internal `if (w_cooldown_ms > 0) return;` a few
       lines into their case would otherwise let a blocked cast still flash.
       Gary excluded (S170-203): his W now begins a cast, not an instant effect -- the flash
       belongs at the moment the shot actually fires (cast completion, tick_hero_kit), not the
       moment the wind-up begins, so his own case below sets cast_flash_slot itself instead of
       relying on this shared pre-switch line. */
    if (h->w_cooldown_ms <= 0 && h->hero_id != ARENA_HERO_GARY) h->cast_flash_slot = 2;

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        if (!h->w_active && h->mp <= 0) return; /* S170-181: activating no longer charges a flat cost, just requires some mana to sustain -- see ARENA_MP_DRAIN_W_PER_SEC; toggling off is always free */
        h->w_active = !h->w_active;
        break;
    case ARENA_HERO_GHOST:
        /* Not a Ghost: an instant-use buff on its own cooldown, not a
           toggle -- reuses the W slot but isn't a hold-on/hold-off state
           like Unicorn's regen. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        h->intangible_ms = ARENA_GHOST_W_INTANGIBLE_MS;
        h->w_cooldown_ms = cast_cooldown(h, ARENA_GHOST_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_FROG: {
        /* Borrowed Time: places the refund buff on the nearest ally --
           wired for real now that arena_nearest_ally exists (was skipped
           for no ally target in 1v1, S170-33). No-op, cooldown not
           consumed, if there's no living ally to target. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        ArenaHero *ally = arena_nearest_ally(owner);
        if (ally && ally->alive) {
            ally->next_cast_refund = 1;
            h->w_cooldown_ms = cast_cooldown(h, ARENA_FROG_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    }
    case ARENA_HERO_DOC_WHEEL:
        /* House Call: instant teleport to the nearest ally's location, on
           a long cooldown ("always shows up"). No-op if there's no ally. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        {
            ArenaHero *ally = arena_nearest_ally(owner);
            if (ally && ally->alive) {
                h->x = ally->x;
                h->z = ally->z;
                h->moving = 0;
                h->w_cooldown_ms = cast_cooldown(h, ARENA_DOC_WHEEL_W_COOLDOWN_MS);
                h->mp -= ARENA_MP_COST_W;
            }
        }
        break;
    case ARENA_HERO_FLAMEL:
        /* Philosopher's Bloom: AoE ally heal, always lands (see
           flamel_cast_w) -- same always-commits convention as Doc Wheel's
           R, not a whiff-refunded single-target poke. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        flamel_cast_w(h, owner);
        h->w_cooldown_ms = cast_cooldown(h, ARENA_FLAMEL_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_MORRIGAN:
        /* Three Forms: gap-close + root on the nearest enemy. No-op,
           cooldown not consumed, if there's no living enemy at all
           (1v1's own bot could still die mid-match). */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (morrigan_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_MORRIGAN_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_DAGDA:
        /* Uaithne, called by name: AoE hits everyone in radius, always
           lands (see dagda_cast_w) -- same always-commits convention as
           Flamel's W above. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        dagda_cast_w(h, owner);
        h->w_cooldown_ms = cast_cooldown(h, ARENA_DAGDA_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_DUCK:
        /* Smoke Bomb (S202-10): self-centered, always lands -- no
           click-to-place targeting exists in this input model, same
           reasoning Flamel/Dagda's own W already use. Routes through the
           PARENA-compiled on_duck_smoke_bomb_cast (not
           redgarden_host_duck_smoke_bomb_cast directly) -- the mod call IS
           the trigger, per the founder's explicit "as a parena mod" /
           "mod first dev." */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        on_duck_smoke_bomb_cast(owner);
        h->w_cooldown_ms = cast_cooldown(h, ARENA_DUCK_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_COURIER:
        /* Between Eagle and Serpent: always lands, jumps to whichever
           of the ARENA_NODE_COUNT nodes is farthest right now. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        courier_toggle_w(h);
        h->w_cooldown_ms = cast_cooldown(h, ARENA_COURIER_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_LOKI:
        /* Bound Where the Myth Says: free toggle, no cooldown, same
           convention as Unicorn's W -- arena_hero_armor() reads w_active
           directly for the actual bonus. */
        if (!h->w_active && h->mp <= 0) return; /* S170-181: activating no longer charges a flat cost, just requires some mana to sustain -- see ARENA_MP_DRAIN_W_PER_SEC; toggling off is always free */
        h->w_active = !h->w_active;
        break;
    case ARENA_HERO_GARY: {
        /* Aimed Shot (S170-203): begins a cast, doesn't fire anything itself -- the actual
           damage application lives in tick_hero_kit, at the moment cast_time_remaining_ms
           reaches 0 (or nowhere, if the cast gets interrupted first). Real commitment, same
           "needs a shot lined up to fire at all" gate gary_cast_q already holds itself to --
           no hittable foe in range right now means this is a no-op, no cooldown/mana spent,
           rather than winding up a cast aimed at nothing. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        ArenaHero *foe = arena_nearest_enemy(owner);
        if (!hero_is_hittable(foe)) return;
        float dx = foe->x - h->x, dz = foe->z - h->z;
        if (sqrtf(dx * dx + dz * dz) > ARENA_GARY_W_RANGE) return;
        h->casting_slot = 2;
        h->cast_time_remaining_ms = ARENA_GARY_W_CAST_MS;
        h->cast_total_ms = ARENA_GARY_W_CAST_MS;
        h->cast_anchor_x = h->x;
        h->cast_anchor_z = h->z;
        h->cast_target = foe->owner;
        h->w_cooldown_ms = cast_cooldown(h, ARENA_GARY_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    }
    case ARENA_HERO_FLUTE_DEBT:
        /* Recouping Interest: free toggle self-heal-over-time, same shape
           as Unicorn's W -- see tick_hero_kit for the actual regen tick. */
        if (!h->w_active && h->mp <= 0) return; /* S170-181: activating no longer charges a flat cost, just requires some mana to sustain -- see ARENA_MP_DRAIN_W_PER_SEC; toggling off is always free */
        h->w_active = !h->w_active;
        break;
    case ARENA_HERO_BACON_PUCK:
        bacon_puck_cast_w(owner);
        break;
    case ARENA_HERO_ABRAHAM: {
        /* A Line of Fire, auto-target redesign (2026-08-26, founder real-time, after the
           manual ground-click flow proved genuinely hard to get firing reliably in a live
           20-hero match: "why does gary work but abraham doesnt" -> "fuck it have the
           fireball go infinitely across the map" -> "have it fire at the nearest enemy no
           matter how far away"). No longer requires arena_state.has_ground_target at all --
           auto-targets the nearest living enemy anywhere on the map (arena_nearest_enemy has
           no range cap of its own), same "no real range limit" spirit the ability's own
           original design already had, just auto-aimed instead of click-aimed. A no-op if
           there's no living enemy anywhere (nothing to fire at), same "real commitment, no
           wasted cast" convention every other kit piece in this switch already holds itself
           to. The client's own green-reticle ground-targeting UI (screen_to_ground et al.)
           is now dead code for this ability -- left in place rather than ripped out
           mid-investigation, real cleanup is separate follow-up work. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        ArenaHero *fireball_target = arena_nearest_enemy(owner);
        if (!fireball_target) return;
        h->casting_slot = 2;
        h->cast_time_remaining_ms = ARENA_ABRAHAM_FIREBALL_WINDUP_MS;
        h->cast_total_ms = ARENA_ABRAHAM_FIREBALL_WINDUP_MS;
        h->cast_anchor_x = h->x;
        h->cast_anchor_z = h->z;
        h->cast_target = -1;
        h->cast_target_x = fireball_target->x;
        h->cast_target_z = fireball_target->z;
        h->w_cooldown_ms = cast_cooldown(h, ARENA_ABRAHAM_FIREBALL_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    }
    case ARENA_HERO_ADA:
        /* The frame's own plating: free toggle, no cooldown --
           arena_hero_armor() reads w_active directly for the bonus. */
        if (!h->w_active && h->mp <= 0) return; /* S170-181: activating no longer charges a flat cost, just requires some mana to sustain -- see ARENA_MP_DRAIN_W_PER_SEC; toggling off is always free */
        h->w_active = !h->w_active;
        break;
    case ARENA_HERO_TYLER:
        /* Poof: an instant-use blink-strike on its own cooldown, not a toggle --
           same shape as Ghost's W. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (tyler_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_TYLER_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_PAIMON:
        /* Speaks With Total Authority: instant decree on its own cooldown, not a toggle. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (paimon_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_PAIMON_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_NOOR1:
        /* Sent In Clean: same instant-use intangibility as Ghost's Not a Ghost --
           she goes quiet and unreadable herself for a moment. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        h->intangible_ms = ARENA_NOOR1_W_INTANGIBLE_MS;
        h->w_cooldown_ms = cast_cooldown(h, ARENA_NOOR1_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_CAIN:
        /* Cursed to Wander: instant-use dash-away + self-cleanse on its own cooldown. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (cain_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_CAIN_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_GUNNR:
        /* Consecration (2026-07-30, founder: "gunnr w switch it to consecration just like wow"):
           a real cast on a real cooldown now, not a free toggle -- a ground zone at Gunnr's own
           feet, same r_zone_x/z/r_active_ms/r_zone_tick_ms fields every other zone ability
           already shares (see tick_hero_kit's own GUNNR case for the damage tick). No target
           needed to cast (unlike most abilities in this file, Consecration is cast at your own
           position, not someone else's), so no hittable-foe gate here -- only cooldown/mana. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_GUNNR_W_DURATION_MS;
        h->w_cooldown_ms = cast_cooldown(h, ARENA_GUNNR_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_VASSAGO: {
        /* The Soft Foresight, extended: grants the nearest ally next_cast_refund, same
           mechanic as Frog's Borrowed Time. No-op, cooldown not consumed, with no living
           ally to target (1v1 local demo). */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        ArenaHero *ally = arena_nearest_ally(owner);
        if (ally && ally->alive) {
            ally->next_cast_refund = 1;
            h->w_cooldown_ms = cast_cooldown(h, ARENA_VASSAGO_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    }
    case ARENA_HERO_HE_XIANGU:
        /* Light/Dark stance (2026-08-26 redesign, see ARENA_HE_XIANGU_DARK_ARMOR_BONUS's own
           header comment): still a free toggle, no cooldown -- Light (w_active=1) keeps the
           original regen tick_hero_kit already reads w_active for; Dark (w_active=0) is now a
           real second stance too, granting flat armor via arena_hero_armor's own w_active==0
           check, not just "the buff turned off." */
        if (!h->w_active && h->mp <= 0) return; /* S170-181: activating no longer charges a flat cost, just requires some mana to sustain -- see ARENA_MP_DRAIN_W_PER_SEC; toggling off is always free */
        h->w_active = !h->w_active;
        break;
    case ARENA_HERO_BELETH:
        /* Hope Is a Terror I Leash With Song: instant silence-only decree on its own
           cooldown, same in-range shape as Paimon's Speaks With Total Authority but with
           the damage stripped out -- escalation denied, not a hit landed. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (beleth_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_BELETH_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_MNM:
        /* Burrow (S170-208): a real cast now, not a free toggle -- see the header's own
           ARENA_MNM_BURROW_* doc comment for the founder's exact phrasing. intangible_ms
           makes him unhittable, rooted_ms keeps him from sliding anywhere while underground
           (he resurfaces at the exact spot he went under), and mnm_burrow_ms is the
           dedicated countdown tick_hero_kit watches to fire the eruption AoE exactly once,
           the moment it expires. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        h->mnm_burrow_ms = ARENA_MNM_BURROW_DURATION_MS;
        h->intangible_ms = ARENA_MNM_BURROW_DURATION_MS;
        h->rooted_ms = ARENA_MNM_BURROW_DURATION_MS;
        h->w_cooldown_ms = cast_cooldown(h, ARENA_MNM_BURROW_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_WEATHERMAN:
        /* Collects On What's Owed: instant targeted cast, not a toggle -- see
           weatherman_cast_w's own doc comment for the ally/enemy-airborne branching (NORTHSTAR
           §16.3). */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (weatherman_cast_w(h, arena_nearest_enemy(owner), arena_nearest_ally(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_WEATHERMAN_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_ZAGAN:
        /* The Standstill: instant targeted cast, not a toggle -- see zagan_cast_w's own doc
           comment. This roster's first-ever kit to call arena_apply_stun(). */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (zagan_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_ZAGAN_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_WARRIOR:
        /* Power Slash: instant targeted cast, same shape as Zagan's W -- see warrior_cast_w's
           own doc comment. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (warrior_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_WARRIOR_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_CART:
        /* The delivery zone: cast at the Cart's own position, no target needed -- same
           "self-position zone" shape as Gunnr's Consecration. tick_hero_kit's own CART case
           resolves who (if anyone) triggers it. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_active_ms = ARENA_CART_W_DURATION_MS;
        h->zone_radius = ARENA_CART_W_RADIUS;
        h->w_cooldown_ms = cast_cooldown(h, ARENA_CART_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    default:
        /* No-op for any hero without a real W in this arena, not a crash
           or a silent wrong kit: Duck's W (Government Clearance) needs
           objective structures that don't exist here. Tree's W
           (Untranslated) and Pizza's W (I Am The Chosen One) both fall here
           too -- unbuildable/pure-visual, flagged in the header comments. */
        break;
    }
}

void arena_cast_r(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    ArenaHero *foe = arena_nearest_enemy(owner);
    if (!h->alive || h->silenced_ms > 0 || h->stunned_ms > 0 || h->r_cooldown_ms > 0 || h->mp < ARENA_MP_COST_R) return; /* S170-184: stun blocks all three action types, silence just casting */
    h->cast_flash_slot = 3;

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        h->r_active_ms = ARENA_UNICORN_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_UNICORN_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_DUCK:
        if (duck_pull_foe(h, foe, ARENA_DUCK_R_PULL_DIST, ARENA_DUCK_R_DAMAGE, ARENA_DUCK_R_RANGE)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_DUCK_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_GHOST:
        /* Recital: the ally-heal side (docs/HEROES_VS0.md: "same zone,
           opposite effect depending on team") is wired for real now that
           arena_nearest_ally exists (S170-45) -- see tick_hero_kit's zone
           tick below for the actual heal application, since it needs the
           `ally` parameter that loop already threads through. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_GHOST_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_GHOST_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_FROG:
        /* The Secret, simplified: reuses the intangible_ms mechanic at a
           longer duration. "Reappear at any visited location" needs its
           own location-memory system -- not implemented, so this reappears
           in place, flagged as a simplification rather than the full
           ability. */
        h->intangible_ms = ARENA_FROG_R_VANISH_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_FROG_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_DOC_WHEEL:
        /* No Combat Power, As Advertised: teamwide cleanse + heal within
           radius, simplified from a literal shield (see header comment).
           Unlike Q's single-target heal, this always "lands" and consumes
           the cooldown even with zero allies in range -- a real ultimate
           commitment, not a whiff-refunded poke. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *ally = &arena_state.heroes[i];
            if (i == owner || !ally->active || !ally->alive) continue;
            if (ally->team != h->team) continue;
            float dx = ally->x - h->x, dz = ally->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_DOC_WHEEL_R_RADIUS) {
                doc_wheel_heal_and_cleanse(ally, ARENA_DOC_WHEEL_R_HEAL);
            }
        }
        h->r_cooldown_ms = cast_cooldown(h, ARENA_DOC_WHEEL_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_TREE:
        /* Grand Secret, simplified from "roots until recast, min 8s" to a
           fixed-duration self-root + armor buff + heal -- same
           fixed-duration simplification already used for Frog's R and
           Ghost's R zone. rooted_ms doubles as "immune to displacement"
           (see duck_pull_foe). */
        h->rooted_ms = ARENA_TREE_R_ROOT_MS;
        h->r_active_ms = ARENA_TREE_R_ROOT_MS;
        h->hp += ARENA_TREE_R_HEAL;
        if (h->hp > h->max_hp) h->hp = h->max_hp;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_TREE_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_PIZZA:
        /* Nobody Ever Checks: HP cannot drop below 1 for the duration -- a
           real damage floor via apply_damage's survive_floor_ms check, not
           a simplified-away shield (contrast Doc Wheel's R, deferred for
           exactly that reason). */
        h->survive_floor_ms = ARENA_PIZZA_R_FLOOR_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_PIZZA_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_FLAMEL:
        /* Elixir of Wild Growth (Elixir of Life + Wild Growth merged): a
           fixed zone (reusing Ghost's r_zone_x/z/tick_ms fields) that roots
           enemies and heals allies each tick for its duration -- see
           tick_hero_kit's zone tick below -- plus a one-time mass-mark of
           nodes in radius at cast time. The doc's "heavy slow" is
           simplified to a full root: no per-hero movement-speed-multiplier
           system exists in this arena yet, flagged. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_FLAMEL_R_DURATION_MS;
        for (int n = 0; n < ARENA_NODE_COUNT; n++) {
            ArenaNode *node = &arena_state.nodes[n];
            float ndx = h->x - node->x, ndz = h->z - node->z;
            if (sqrtf(ndx * ndx + ndz * ndz) <= ARENA_FLAMEL_R_RADIUS) {
                node->marked_by_team = h->team;
                node->mark_ms_remaining = ARENA_FLAMEL_MARK_MS;
            }
        }
        h->r_cooldown_ms = cast_cooldown(h, ARENA_FLAMEL_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_MORRIGAN:
        /* The Crow Confirms It: a fixed zone (reusing Ghost's
           r_zone_x/z/tick_ms fields) that deals execute-scaled DPS to
           enemies inside for its duration -- see tick_hero_kit's zone tick
           below. No ally-heal side (unlike Ghost/Flamel's R) -- a war
           goddess's ultimate isn't a support tool. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_MORRIGAN_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_MORRIGAN_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_DAGDA:
        /* The force-fed porridge: a real damage floor (like Pizza's R) plus
           a real heal on top -- "eats every bite, unhurt, fights the next
           day regardless," enduring AND coming out ahead, not just
           surviving. */
        h->survive_floor_ms = ARENA_DAGDA_R_FLOOR_MS;
        h->hp += ARENA_DAGDA_R_HEAL;
        if (h->hp > h->max_hp) h->hp = h->max_hp;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_DAGDA_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_COURIER:
        if (courier_cast_r(h, foe)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_COURIER_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_LOKI:
        loki_cast_r(h);
        h->r_cooldown_ms = cast_cooldown(h, ARENA_LOKI_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_GARY:
        if (gary_cast_r(h, foe)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_GARY_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_FLUTE_DEBT:
        flute_debt_cast_r(h, foe);
        h->r_cooldown_ms = cast_cooldown(h, ARENA_FLUTE_DEBT_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_BACON_PUCK:
        if (bacon_puck_cast_r(h, foe)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_BACON_PUCK_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_ABRAHAM:
        abraham_cast_r(h);
        h->r_cooldown_ms = cast_cooldown(h, ARENA_ABRAHAM_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_ADA:
        if (ada_cast_r(h, foe)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_ADA_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_TYLER:
        tyler_cast_r(h, foe);
        h->r_cooldown_ms = cast_cooldown(h, ARENA_TYLER_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_PAIMON:
        /* Two Hundred Legions: fixed zone, same shape as Ghost's Recital/
           Flamel's Elixir of Wild Growth -- see tick_hero_kit's zone tick
           below for the actual periodic damage/heal. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_PAIMON_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_PAIMON_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_NOOR1:
        /* Do Not Approach: fixed cold zone, damage-only (no ally-heal side --
           the instruction is one-sided) -- see tick_hero_kit's zone tick below. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_NOOR1_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_NOOR1_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_CAIN:
        /* The Mark: survive-floor panic button, same shape as Pizza's/Loki's R --
           "a mark that is a curse and a protection at the same time," made literal. */
        h->survive_floor_ms = ARENA_CAIN_R_FLOOR_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_CAIN_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_GUNNR:
        /* Valhalla Has Yet To Admit It: instant hit-if-in-range, execute-scaled via
           execute_scale_damage, same shape as Morrigan's/Cain's Q -- the vindication
           finally lands hardest against a target who's already nearly beaten. 2026-07-31,
           founder: "give gunnrs e a stun" -- now also stuns the same target it hits, same
           range check, no separate targeting pass. Duration copied from Zagan's own W (The
           Standstill, S170-230), this roster's second-ever arena_apply_stun() call. */
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_GUNNR_R_RANGE) {
                apply_damage(foe, apply_armor(execute_scale_damage(foe, ARENA_GUNNR_R_DAMAGE_BASE, ARENA_GUNNR_R_DAMAGE_LOW_HP),
                                               arena_hero_armor(foe)));
                arena_apply_stun(foe->owner, ARENA_GUNNR_R_STUN_MS);
            }
        }
        h->r_cooldown_ms = cast_cooldown(h, ARENA_GUNNR_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_VASSAGO:
        /* The Gentle Maybe: fixed zone, same shape as Ghost's Recital/Paimon's Two Hundred
           Legions -- see tick_hero_kit's zone tick below. No damage component at all, the
           one hero on this roster whose ultimate is pure control: not a hit, a held breath. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_VASSAGO_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_VASSAGO_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_HE_XIANGU:
        /* Never Once Framed It As Sacrifice: fixed zone, same shape as Flamel's Elixir of
           Wild Growth, heal-only -- no enemy damage component at all, the mirror of
           Vassago's purely-controlling R: she shares her sustenance, doesn't hurt anyone. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_HE_XIANGU_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_HE_XIANGU_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_BELETH:
        /* The Detonation: marks the foe's CURRENT position at cast time (not a
           continuously-tracked target) and starts a silent fuse -- reuses Ghost's own
           r_zone_x/z fields but not r_zone_tick_ms's periodic-tick idiom, since this fires
           exactly once, in tick_hero_kit, the instant r_active_ms counts down to zero.
           Whiffs (consumes no cooldown) with no foe in range -- same "real commitment, not
           a guaranteed poke" shape as every other ranged cast on this roster. */
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_BELETH_R_RANGE) {
                h->r_zone_x = foe->x;
                h->r_zone_z = foe->z;
                h->r_active_ms = ARENA_BELETH_R_FUSE_MS;
                h->r_cooldown_ms = cast_cooldown(h, ARENA_BELETH_R_COOLDOWN_MS);
                h->mp -= ARENA_MP_COST_R;
            }
        }
        break;
    case ARENA_HERO_MNM:
        /* Absorbing Hits Meant For Somebody Else: self-root + a guaranteed-survival window,
           same combining-two-generic-fields shape as Tree's Grand Secret (rooted_ms + a buff),
           with survive_floor_ms standing in for Tree's armor bonus -- the literal mechanical
           translation of the lore's own line that the shapeshifting is just what happens to a
           body that's absorbed hits meant for someone else. Always lands, same "real ultimate
           commitment" convention as every other unconditional self-buff R on this roster. */
        h->rooted_ms = ARENA_MNM_R_ROOT_MS;
        h->survive_floor_ms = ARENA_MNM_R_SURVIVE_FLOOR_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_MNM_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_WEATHERMAN:
        /* The Debt Compounds: fixed zone, same shape as Ghost's Recital/Paimon's Two Hundred
           Legions/NOOR-1's Do Not Approach -- see tick_hero_kit's zone tick below. The literal
           storm finally collecting, biggest and simplest ability on the kit by design (NORTHSTAR
           §16.2), so the interesting design surface stays on W where the actual founder ask
           was. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_WEATHERMAN_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_WEATHERMAN_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_ZAGAN:
        /* Conjunction: locks a target hero-slot (not a fixed ground position, unlike every
           R-zone above) -- the actual armor-mirroring happens live, every time
           arena_hero_armor(zagan) is read, for as long as r_active_ms > 0 and the target stays
           hittable (see that function's own doc comment). Always lands, same "real ultimate
           commitment" convention as every other unconditional self-effect R on this roster. */
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_ZAGAN_R_RANGE) {
                h->zagan_r_target = foe->owner;
                h->r_active_ms = ARENA_ZAGAN_R_DURATION_MS;
                h->r_cooldown_ms = cast_cooldown(h, ARENA_ZAGAN_R_COOLDOWN_MS);
                h->mp -= ARENA_MP_COST_R;
            }
        }
        break;
    case ARENA_HERO_WARRIOR:
        /* Frostbite: instant targeted cast, same shape as Gunnr's Q/warrior_cast_q -- see
           warrior_cast_r's own doc comment. */
        if (h->r_cooldown_ms > 0 || h->mp < ARENA_MP_COST_R) return;
        if (warrior_cast_r(h, foe)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_WARRIOR_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_CART:
        /* Same delivery-zone mechanic as W, bigger radius/duration, no target needed. Shares
           the same r_zone_x/z/r_active_ms fields W already used -- casting R while a W zone is
           still active replaces it (last-cast-wins, same as any other zone-ability hero would
           behave if it could cast two zones back to back; no existing hero's kit lets that
           happen today, so this is a real, honestly-undocumented-until-now interaction unique
           to the Cart having two zone-shaped abilities). */
        if (h->r_cooldown_ms > 0 || h->mp < ARENA_MP_COST_R) return;
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_active_ms = ARENA_CART_R_DURATION_MS;
        h->zone_radius = ARENA_CART_R_RADIUS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_CART_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    }
}

static void tick_hero_kit(ArenaHero *h, ArenaHero *foe, ArenaHero *ally, unsigned int dt_ms) {
    /* Cooldowns and status effects (silence, intangibility) are generic --
       any hero can carry them, not just whichever kit currently applies
       them (S170-32). */
    if (h->q_cooldown_ms > 0) h->q_cooldown_ms -= (int)dt_ms;
    if (h->w_cooldown_ms > 0) h->w_cooldown_ms -= (int)dt_ms;
    if (h->r_cooldown_ms > 0) h->r_cooldown_ms -= (int)dt_ms;
    if (h->blink_cooldown_ms > 0) h->blink_cooldown_ms -= (int)dt_ms; /* S170-205 -- generic same as the three above, independent track */
    if (h->donkey_fold_proc_cooldown_ms > 0) h->donkey_fold_proc_cooldown_ms -= (int)dt_ms; /* S170-206 */
    if (h->donkey_glide_cooldown_ms > 0) h->donkey_glide_cooldown_ms -= (int)dt_ms;
    if (h->donkey_airborne_ms > 0) {
        h->donkey_airborne_ms -= (int)dt_ms;
        if (h->donkey_airborne_ms < 0) h->donkey_airborne_ms = 0;
    }
    /* zagan_calcination_ms (S170-230): generic decrement, any hero can carry this debuff --
       see arena_hero_armor for where it's actually read/applied. */
    if (h->zagan_calcination_ms > 0) {
        h->zagan_calcination_ms -= (int)dt_ms;
        if (h->zagan_calcination_ms < 0) h->zagan_calcination_ms = 0;
    }
    /* Immortal's Fold (S170-206, Donkey's automatic passive -- "unfolds automatically... the
       instant the wearer's HP crosses below ARENA_DONKEY_FOLD_HP_FRACTION"). Checked here,
       generically, for ANY hero wearing Donkey -- not gated on hero_id at all, the whole point
       of building this as an item rather than a hero-specific passive. donkey_fold_ms > 0 is
       the re-entry guard (already folded, don't restack) on top of the proc's own cooldown. */
    if (h->alive && h->equipped_item[ARENA_ITEM_SLOT_BACK] == ARENA_DONKEY_ITEM_ID &&
        h->donkey_fold_ms <= 0 && h->donkey_fold_proc_cooldown_ms <= 0 &&
        h->max_hp > 0 && (float)h->hp / (float)h->max_hp < ARENA_DONKEY_FOLD_HP_FRACTION) {
        h->donkey_fold_ms = ARENA_DONKEY_FOLD_MS;
        h->donkey_fight_tick_ms = 0;
        h->survive_floor_ms = ARENA_DONKEY_FOLD_MS; /* simplified "flat damage shield," see header doc comment */
        h->donkey_fold_proc_cooldown_ms = ARENA_DONKEY_FOLD_PROC_COOLDOWN_MS;
    }
    if (h->donkey_fold_ms > 0) {
        h->donkey_fold_ms -= (int)dt_ms;
        if (h->donkey_fold_ms < 0) h->donkey_fold_ms = 0;
        /* "it unfolds and fights for you" -- periodic damage to the nearest enemy while
           unfolded, same fixed-1000ms-interval accumulator idiom as every other DPS tick in
           this file (e.g. r_zone_tick_ms). */
        if (h->alive) {
            h->donkey_fight_tick_ms += (int)dt_ms;
            while (h->donkey_fight_tick_ms >= 1000) {
                h->donkey_fight_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->x, dz = foe->z - h->z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_DONKEY_FOLD_FIGHT_RADIUS) {
                        apply_damage(foe, ARENA_DONKEY_FOLD_FIGHT_DPS);
                    }
                }
            }
        }
    }
    /* combat_timer_ms (S170-148): ticks down every tick regardless of kit,
       same generic "runs for every hero" reasoning as the cooldowns above --
       re-armed to ARENA_COMBAT_TIMEOUT_MS by apply_damage() whenever this
       hero actually takes damage. */
    if (h->combat_timer_ms > 0) {
        h->combat_timer_ms -= (int)dt_ms;
        if (h->combat_timer_ms < 0) h->combat_timer_ms = 0;
    }
    /* sc_pending_age_ms (Milestone 2): counts UP (not down) from the moment a weapon skill
       landed on this hero, same "generic across every hero" reasoning as combat_timer_ms just
       above -- arena_skillchain_try treats anything past ARENA_SKILLCHAIN_WINDOW_MS as expired,
       so this doesn't need to clamp or clear sc_pending_attr_count itself, just keep counting. */
    if (h->sc_pending_attr_count > 0 && h->sc_pending_age_ms <= ARENA_SKILLCHAIN_WINDOW_MS) {
        h->sc_pending_age_ms += dt_ms;
    }
    /* multikill_timer_ms (2026-07-29): same "ticks down every tick, generic across every hero"
       shape as combat_timer_ms just above -- re-armed to ARENA_MULTIKILL_WINDOW_MS by
       apply_damage() on each kill this hero lands. Once it actually reaches 0 with no new kill
       in the meantime, the streak is over -- clear multikill_count so the NEXT kill starts a
       fresh streak at 1 rather than silently continuing a stale one. */
    if (h->multikill_timer_ms > 0) {
        h->multikill_timer_ms -= (int)dt_ms;
        if (h->multikill_timer_ms <= 0) {
            h->multikill_timer_ms = 0;
            h->multikill_count = 0;
        }
    }
    /* Mana regen (S170-132, combat-gated S170-148, always-trickles S170-150:
       "have mana tic up slowly 1 per second always"). Two rates, not a hard
       on/off gate anymore: a slow ARENA_MP_REGEN_IN_COMBAT_PER_SEC trickle
       runs unconditionally, even mid-fight; the full ARENA_MP_REGEN_PER_SEC
       rate only kicks in once combat_timer_ms has actually expired -- real
       WoW-style out-of-combat regen, just no longer a dead stop at 0 while
       fighting. */
    if (h->alive && h->mp < h->max_mp) {
        /* S205-87, Luck of the Draw: item_bonus_mp_regen_combat only applies to the IN-COMBAT
           rate specifically -- the founder's own ask names "mana regen during combat," not a
           general regen buff, same narrow-scope discipline every other trinket bonus on this
           hero already holds itself to (bonus_attack_range_pct is auto-attack range only, not
           ability range; bonus_cdr_pct is the one deliberate exception, scoped explicitly to
           both cooldown types by its own founder quote). */
        float rate = (h->combat_timer_ms > 0)
            ? ARENA_MP_REGEN_IN_COMBAT_PER_SEC + (float)h->item_bonus_mp_regen_combat
            : ARENA_MP_REGEN_PER_SEC;
        h->mp_regen_accum += rate * ((float)dt_ms / 1000.0f);
        int whole = (int)h->mp_regen_accum;
        if (whole > 0) {
            h->mp += whole;
            h->mp_regen_accum -= (float)whole;
            if (h->mp > h->max_mp) { h->mp = h->max_mp; h->mp_regen_accum = 0.0f; }
        }
    } else {
        h->mp_regen_accum = 0.0f; /* dead or already full -- don't let fractional progress silently bank while it can't apply */
    }
    /* Regen powerup (S170-190, "warsong gulch"-style pickup): same fractional-accumulator idiom
       as mana regen just above, HP instead of MP, active for regen_ms after pickup regardless
       of combat state (a carried buff, not a location -- unlike the fountain's own always-on
       tick, this one only runs while the timed buff from a real pickup is active). */
    if (h->alive && h->regen_ms > 0 && h->hp < h->max_hp) {
        h->regen_accum += (float)ARENA_POWERUP_REGEN_HP_PER_SEC * ((float)dt_ms / 1000.0f);
        int regen_whole = (int)h->regen_accum;
        if (regen_whole > 0) {
            h->hp += regen_whole;
            h->regen_accum -= (float)regen_whole;
            if (h->hp > h->max_hp) { h->hp = h->max_hp; h->regen_accum = 0.0f; }
        }
    } else if (!h->alive || h->regen_ms <= 0) {
        h->regen_accum = 0.0f;
    }
    if (h->berserker_ms > 0) {
        h->berserker_ms -= (int)dt_ms;
        if (h->berserker_ms < 0) h->berserker_ms = 0;
    }
    if (h->regen_ms > 0) {
        h->regen_ms -= (int)dt_ms;
        if (h->regen_ms < 0) h->regen_ms = 0;
    }
    /* Toggle-W mana drain (S170-181): generic across every TRUE toggle hero (see
       ArenaHero.w_drain_accum's own doc comment for the exact case list) -- arena_toggle_w
       itself no longer charges a flat activation cost for these, it only gates on mp > 0 to
       turn on. Same fractional-accumulator idiom as mana regen just above, and can run in the
       same tick as regen (a toggle hero below max mp both regens and drains; the two rates
       simply net out, no special-case needed). Auto-deactivates the instant mp is fully spent
       -- a toggle can't be held on for free once the tank is empty. */
    if (h->alive && h->w_active) {
        h->w_drain_accum += ARENA_MP_DRAIN_W_PER_SEC * ((float)dt_ms / 1000.0f);
        int drain_whole = (int)h->w_drain_accum;
        if (drain_whole > 0) {
            h->mp -= drain_whole;
            h->w_drain_accum -= (float)drain_whole;
            if (h->mp <= 0) {
                h->mp = 0;
                h->w_active = 0;
                h->w_drain_accum = 0.0f;
            }
        }
    } else {
        h->w_drain_accum = 0.0f; /* not toggled on -- don't let fractional progress silently bank */
    }
    if (h->silenced_ms > 0) {
        h->silenced_ms -= (int)dt_ms;
        if (h->silenced_ms < 0) h->silenced_ms = 0;
    }
    /* Cast-time ability progression (S170-203, founder: "movement interrupts cast damage does
     * not interrupt cast silence does"). Generic across any hero/slot that ever sets
     * casting_slot -- Gary's Aimed Shot (W) is the first, not the only one this is meant to
     * support. Checked right after silenced_ms just ticked down above, so a silence landing
     * mid-cast interrupts on the very tick it lands, not one tick late. */
    if (h->casting_slot != 0) {
        if (h->silenced_ms > 0) {
            /* Silence interrupts. No refund of the mana/cooldown already spent at cast start --
               same real-commitment shape as every other Gary ability. */
            h->casting_slot = 0;
            h->cast_time_remaining_ms = 0;
            h->cast_total_ms = 0;
            h->cast_target = -1;
        } else {
            float cast_dx = h->x - h->cast_anchor_x, cast_dz = h->z - h->cast_anchor_z;
            /* Abraham exemption (2026-08-26, founder: "freeze the player for the length of the
               cast for that ability" instead of wasting the cooldown on a movement-interrupted
               cast): update_hero_motion's own matching Abraham-only freeze (see that function's
               doc comment) means h->x/z genuinely can't drift from cast_anchor_x/z while he's
               casting anyway -- this check would always read 0 drift for him now, so the
               `!= ARENA_HERO_ABRAHAM` guard is here mainly for clarity/defense-in-depth (a
               knockback/pull forcing his position mid-cast, which the freeze above doesn't
               block, still shouldn't interrupt this specific ability either -- the founder's
               own ask was "freeze the player," not "still cancel on any forced displacement"
               the way Gary's own cast deliberately still does). Gary's own established
               "movement interrupts" feel (S170-203) is untouched. */
            if (h->hero_id != ARENA_HERO_ABRAHAM && cast_dx * cast_dx + cast_dz * cast_dz > 0.0001f) {
                /* Movement interrupts -- any real drift from where the cast began, whether a
                   fresh move command or a forced displacement (a pull, a knockback), not just a
                   deliberate click; comparing live position against the cast-start anchor every
                   tick catches both uniformly, no need to hook every movement code path
                   separately. Damage taken alone does NOT interrupt -- deliberately no HP/
                   combat_timer_ms check anywhere in this block. */
                h->casting_slot = 0;
                h->cast_time_remaining_ms = 0;
                h->cast_total_ms = 0;
                h->cast_target = -1;
            } else {
                h->cast_time_remaining_ms -= (int)dt_ms;
                if (h->cast_time_remaining_ms <= 0) {
                    int finished_slot = h->casting_slot;
                    int target_idx = h->cast_target;
                    float finished_target_x = h->cast_target_x, finished_target_z = h->cast_target_z;
                    h->casting_slot = 0;
                    h->cast_time_remaining_ms = 0;
                    h->cast_total_ms = 0;
                    h->cast_target = -1;
                    if (h->hero_id == ARENA_HERO_GARY && finished_slot == 2 &&
                        target_idx >= 0 && target_idx < ARENA_MAX_HEROES) {
                        ArenaHero *target = &arena_state.heroes[target_idx];
                        /* Re-validated only here, at completion, not every tick -- a target
                           that stepped out of range mid-cast (without the CASTER moving) still
                           costs Gary the cast, same "real commitment, not a guaranteed poke"
                           convention his Q already holds itself to. */
                        if (hero_is_hittable(target)) {
                            float tdx = target->x - h->cast_anchor_x, tdz = target->z - h->cast_anchor_z;
                            if (sqrtf(tdx * tdx + tdz * tdz) <= ARENA_GARY_W_RANGE) {
                                apply_damage(target, apply_armor(ARENA_GARY_W_DAMAGE, arena_hero_armor(target)));
                                h->cast_flash_slot = 2; /* the shot actually fires now, not at cast start */
                            }
                        }
                    } else if (h->hero_id == ARENA_HERO_ABRAHAM && finished_slot == 2) {
                        /* A Line of Fire (S202-34): the windup itself never re-validates
                           anything (a ground point can't "dodge" the way a unit target can,
                           so there's nothing to re-check here the way Gary's branch above
                           re-checks range/hittability) -- it always fires on completion.
                           Routes through the PARENA-compiled on_abraham_fireball_cast (not
                           redgarden_host_abraham_fireball_cast directly), same "the mod call
                           IS the trigger" convention duck_smoke_bomb_mod's own W case already
                           established. Target coords rounded to int -- see
                           abraham_fireball_mod.prn's own doc comment on why (VS0 has no F32
                           mod-parameter support yet). */
                        on_abraham_fireball_cast(h->owner, (int)finished_target_x, (int)finished_target_z);
                        h->cast_flash_slot = 2; /* the fireball actually fires now, not at cast start */
                    }
                }
            }
        }
    }
    if (h->intangible_ms > 0) {
        h->intangible_ms -= (int)dt_ms;
        if (h->intangible_ms < 0) h->intangible_ms = 0;
    }
    /* Duck's Smoke Bomb (S202-10): decremented here, in the one shared
     * per-hero tick both arena_update (1v1) and arena_update_teams (team
     * mode) already call -- not duplicated into each top-level tick
     * function separately, the exact mistake S202-23 found and fixed for
     * Tree's passive (wired into team-mode's own tick only, silently never
     * fired in 1v1 matches for a full session before being caught). */
    if (h->duck_smoke_ms > 0) {
        h->duck_smoke_ms -= (int)dt_ms;
        if (h->duck_smoke_ms < 0) h->duck_smoke_ms = 0;
    }
    /* rooted_ms/survive_floor_ms (S170-46): generic status effects, any
       kit's ability can apply them, same reasoning as silence/intangible
       above. */
    if (h->rooted_ms > 0) {
        h->rooted_ms -= (int)dt_ms;
        if (h->rooted_ms < 0) h->rooted_ms = 0;
    }
    if (h->survive_floor_ms > 0) {
        h->survive_floor_ms -= (int)dt_ms;
        if (h->survive_floor_ms < 0) h->survive_floor_ms = 0;
    }
    /* stunned_ms/slowed_ms (S170-184): same generic tick-down idiom as every other status
       effect above. slow_pct isn't reset when slowed_ms hits 0 -- update_hero_motion only ever
       reads slow_pct while slowed_ms > 0, so a stale nonzero value sitting there between
       applications is inert, same "don't bother clearing what's already unreachable" precedent
       burn_dps below takes with burning_ms. */
    if (h->stunned_ms > 0) {
        h->stunned_ms -= (int)dt_ms;
        if (h->stunned_ms < 0) h->stunned_ms = 0;
    }
    if (h->slowed_ms > 0) {
        h->slowed_ms -= (int)dt_ms;
        if (h->slowed_ms < 0) h->slowed_ms = 0;
    }
    /* assist_ms (S170-187): same generic tick-down idiom as every status-effect field above --
       an expired (ms<=0) slot is simply skipped by the assist-reward loop in apply_damage,
       same "0 means inert" convention slow_pct's own doc comment already relies on. */
    for (int a = 0; a < ARENA_MAX_ASSIST_TRACK; a++) {
        if (h->assist_ms[a] > 0) {
            h->assist_ms[a] -= (int)dt_ms;
            if (h->assist_ms[a] < 0) h->assist_ms[a] = 0;
        }
    }
    /* burning_ms/burn_dps (S170-46, Pizza's Q): fixed-interval DoT tick,
       same 1000ms-accumulator pattern as Ghost's R zone. burn_tick_ms
       resets when the burn ends so a later re-application starts clean. */
    if (h->burning_ms > 0) {
        if (h->alive) {
            h->burn_tick_ms += (int)dt_ms;
            while (h->burn_tick_ms >= 1000 && h->burning_ms > 0) {
                h->burn_tick_ms -= 1000;
                apply_damage(h, h->burn_dps);
            }
        }
        h->burning_ms -= (int)dt_ms;
        if (h->burning_ms <= 0) { h->burning_ms = 0; h->burn_tick_ms = 0; }
    }

    /* Loop Back's history ring buffer (S170-33) is sampled for every hero,
       not just whoever's playing Frog -- same "generic state, only one
       kit reads it today" reasoning as the status-effect fields. Samples
       while alive only: rewinding into a pre-death state is the ability's
       whole point, but there's nothing meaningful to record once a match
       has already ended for this hero. */
    if (h->alive) {
        h->loopback_since_sample_ms += (int)dt_ms;
        while (h->loopback_since_sample_ms >= ARENA_FROG_LOOPBACK_SAMPLE_MS) {
            h->loopback_since_sample_ms -= ARENA_FROG_LOOPBACK_SAMPLE_MS;
            int slot = h->loopback_next_slot;
            h->loopback_x[slot] = h->x;
            h->loopback_z[slot] = h->z;
            h->loopback_hp[slot] = h->hp;
            h->loopback_next_slot = (slot + 1) % ARENA_FROG_LOOPBACK_SLOTS;
            if (h->loopback_count < ARENA_FROG_LOOPBACK_SLOTS) h->loopback_count++;
        }
    }

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
        }
        if (h->w_active && h->alive) {
            float regen = ARENA_UNICORN_W_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        break;
    case ARENA_HERO_GHOST:
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            /* Fixed-interval zone tick (once per 1000ms of accumulated
               time in the zone's duration), not fractional-per-tick DPS --
               correct at any real frame rate, same reasoning as the match
               event log's snapshot interval elsewhere in this codebase. */
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_GHOST_R_RADIUS) {
                        apply_damage(foe, apply_armor(ARENA_GHOST_R_DPS, arena_hero_armor(foe)));
                    }
                }
                arena_zone_damage_creeps(h->r_zone_x, h->r_zone_z, ARENA_GHOST_R_RADIUS, h->team, ARENA_GHOST_R_DPS);
                /* Ally-heal side (S170-45): "same zone, opposite effect
                   depending on team" -- the nearest living ally standing in
                   the zone heals for the same rate the foe takes damage. */
                if (ally && ally->alive) {
                    float adx = ally->x - h->r_zone_x, adz = ally->z - h->r_zone_z;
                    if (sqrtf(adx * adx + adz * adz) <= ARENA_GHOST_R_RADIUS) {
                        ally->hp += ARENA_GHOST_R_DPS;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_TREE:
        /* Grand Secret's fixed-duration armor/root window (see arena_cast_r) --
           rooted_ms already decrements generically above; this only owns
           r_active_ms, same pattern as Unicorn/Ghost. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
        }
        break;
    case ARENA_HERO_PIZZA:
        /* Uninvestigated Fire: an always-on burn aura, not a cast -- ticks
           independently of Q/W/R cooldowns. Pizza is immune to its own
           burn (per the doc) since this only ever damages `foe`, never h
           itself. The node-corruption half of this passive is handled
           generically in arena_tick_nodes, not here. Only checks the
           single nearest-foe parameter (same limitation as Ghost's R zone
           in team mode -- an existing, accepted precedent, not a new one). */
        if (h->alive) {
            h->aura_tick_ms += (int)dt_ms;
            while (h->aura_tick_ms >= 1000) {
                h->aura_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->x, dz = foe->z - h->z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_PIZZA_AURA_RADIUS) {
                        apply_damage(foe, ARENA_PIZZA_AURA_DPS);
                    }
                }
                arena_zone_damage_creeps(h->x, h->z, ARENA_PIZZA_AURA_RADIUS, h->team, ARENA_PIZZA_AURA_DPS);
            }
        }
        break;
    case ARENA_HERO_FLAMEL:
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_FLAMEL_R_RADIUS) {
                        foe->rooted_ms = ARENA_FLAMEL_R_ROOT_MS;
                    }
                }
                if (ally && ally->alive) {
                    float adx = ally->x - h->r_zone_x, adz = ally->z - h->r_zone_z;
                    if (sqrtf(adx * adx + adz * adz) <= ARENA_FLAMEL_R_RADIUS) {
                        ally->hp += ARENA_FLAMEL_R_HEAL_PER_TICK;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_MORRIGAN:
        /* The Crow Confirms It: execute-scaled DPS zone tick, same
           fixed-interval pattern as Ghost/Flamel's R. No ally-heal side. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_MORRIGAN_R_RADIUS) {
                        apply_damage(foe, apply_armor(
                            execute_scale_damage(foe, ARENA_MORRIGAN_R_DAMAGE_BASE, ARENA_MORRIGAN_R_DAMAGE_LOW_HP),
                            arena_hero_armor(foe)));
                    }
                }
            }
        }
        break;
    case ARENA_HERO_DAGDA:
        /* The Undry: passive self HP regen, always on, no cooldown/cast
           gate at all -- "no one leaves it unsatisfied." */
        if (h->alive) {
            float regen = ARENA_DAGDA_PASSIVE_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        break;
    case ARENA_HERO_FLUTE_DEBT:
        /* Recouping Interest: same toggle-regen shape as Unicorn's W. */
        if (h->w_active && h->alive) {
            float regen = ARENA_FLUTE_DEBT_W_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        break;
    case ARENA_HERO_GUNNR:
        /* Consecration (2026-07-30): same fixed-interval zone-tick idiom as Ghost's own R zone
           (ARENA_HERO_GHOST's case above) -- enemies-only damage, no ally-heal side (real WoW
           Consecration doesn't heal allies either), so this is simpler than Ghost's own version. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_GUNNR_W_RADIUS) {
                        apply_damage(foe, apply_armor(ARENA_GUNNR_W_DPS, arena_hero_armor(foe)));
                    }
                }
                arena_zone_damage_creeps(h->r_zone_x, h->r_zone_z, ARENA_GUNNR_W_RADIUS, h->team, ARENA_GUNNR_W_DPS);
            }
        }
        break;
    case ARENA_HERO_VASSAGO:
        /* Passive: same always-on regen shape as Dagda's Undry -- ambient restorative
           foresight, sensing and softening harm before it fully lands. */
        if (h->alive) {
            float regen = ARENA_VASSAGO_PASSIVE_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        /* The Gentle Maybe: fixed zone, silence-only tick, no damage -- re-applies the
           silence to any foe still standing in it, so leaving and re-entering is the
           only way out, same "you're in it or you're not" logic every other zone uses. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_VASSAGO_R_RADIUS) {
                        foe->silenced_ms = ARENA_VASSAGO_R_SILENCE_MS;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_HE_XIANGU:
        /* Passive: same always-on regen shape as Dagda's Undry -- subsisting on almost
           nothing. */
        if (h->alive) {
            float regen = ARENA_HE_XIANGU_PASSIVE_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        /* W: same toggle-regen shape as Flute Debt's Recouping Interest -- self-denial as
           discipline, a second layer of sustain on top of the passive while active. */
        if (h->w_active && h->alive) {
            float regen = ARENA_HE_XIANGU_W_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        /* Never Once Framed It As Sacrifice: fixed zone, heal-only tick, no damage --
           re-applies each tick, so an ally has to actually stay in it, same "you're in it
           or you're not" logic every other zone uses. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (ally && ally->alive) {
                    float adx = ally->x - h->r_zone_x, adz = ally->z - h->r_zone_z;
                    if (sqrtf(adx * adx + adz * adz) <= ARENA_HE_XIANGU_R_RADIUS) {
                        ally->hp += ARENA_HE_XIANGU_R_HEAL_PER_TICK;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_BELETH:
        /* The Detonation: NOT a periodic zone tick like Ghost/Vassago/He Xiangu's own R
           zones above -- the fuse counts down once, and the instant it crosses from >0 to
           <=0 (this exact branch only ever runs on that one tick, since r_active_ms then
           sits at 0 and the outer guard stops re-entry until the next real cast) it deals
           ONE large burst to whoever's still standing in the marked zone. "The threat
           builds in total silence and only resolves once, all at once." */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms <= 0) {
                h->r_active_ms = 0;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_BELETH_R_RADIUS) {
                        apply_damage(foe, apply_armor(ARENA_BELETH_R_DAMAGE, arena_hero_armor(foe)));
                    }
                }
                arena_zone_damage_creeps(h->r_zone_x, h->r_zone_z, ARENA_BELETH_R_RADIUS, h->team, ARENA_BELETH_R_DAMAGE);
            }
        }
        break;
    case ARENA_HERO_TYLER:
        /* Divided We Stand's vulnerability window -- arena_hero_armor() reads r_active_ms
           directly for the negative-armor effect; this just counts it down. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
        }
        break;
    case ARENA_HERO_PAIMON:
        /* Keeping the Peace: always-on passive, same aura-tick idiom as
           Pizza's burn aura -- periodically silences the nearest enemy in
           range without being cast, talking a fight down before it
           escalates rather than burning it. */
        if (h->alive) {
            h->aura_tick_ms += (int)dt_ms;
            while (h->aura_tick_ms >= ARENA_PAIMON_PASSIVE_INTERVAL_MS) {
                h->aura_tick_ms -= ARENA_PAIMON_PASSIVE_INTERVAL_MS;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->x, dz = foe->z - h->z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_PAIMON_PASSIVE_AURA_RADIUS) {
                        foe->silenced_ms = ARENA_PAIMON_PASSIVE_SILENCE_MS;
                    }
                }
            }
        }
        /* Two Hundred Legions: fixed zone, damage-to-enemy + heal-to-ally
           tick, same shape as Ghost's Recital / Flamel's Elixir of Wild
           Growth -- the literal presence of a commanded army felt by both
           sides at once. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_PAIMON_R_RADIUS) {
                        apply_damage(foe, ARENA_PAIMON_R_DPS);
                    }
                }
                arena_zone_damage_creeps(h->r_zone_x, h->r_zone_z, ARENA_PAIMON_R_RADIUS, h->team, ARENA_PAIMON_R_DPS);
                if (ally && ally->alive) {
                    float adx = ally->x - h->r_zone_x, adz = ally->z - h->r_zone_z;
                    if (sqrtf(adx * adx + adz * adz) <= ARENA_PAIMON_R_RADIUS) {
                        ally->hp += ARENA_PAIMON_R_HEAL_PER_TICK;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_NOOR1:
        /* About Four Days Behind: always-on passive, same aura-tick idiom as
           Pizza's/Paimon's -- periodically silences the nearest enemy in
           range, reading their next move before they've committed to it. */
        if (h->alive) {
            h->aura_tick_ms += (int)dt_ms;
            while (h->aura_tick_ms >= ARENA_NOOR1_PASSIVE_INTERVAL_MS) {
                h->aura_tick_ms -= ARENA_NOOR1_PASSIVE_INTERVAL_MS;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->x, dz = foe->z - h->z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_NOOR1_PASSIVE_AURA_RADIUS) {
                        foe->silenced_ms = ARENA_NOOR1_PASSIVE_SILENCE_MS;
                    }
                }
            }
        }
        /* Do Not Approach: fixed cold zone, damage-only tick -- no ally-heal
           side, the instruction is one-sided. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_NOOR1_R_RADIUS) {
                        apply_damage(foe, ARENA_NOOR1_R_DPS);
                    }
                }
                arena_zone_damage_creeps(h->r_zone_x, h->r_zone_z, ARENA_NOOR1_R_RADIUS, h->team, ARENA_NOOR1_R_DPS);
            }
        }
        break;
    case ARENA_HERO_WEATHERMAN:
        /* The Ledger: passive self HP regen, always on -- reuses Dagda's own Undry shape
           exactly (NORTHSTAR §16.2's own explicit call), flavor-only for this first pass rather
           than a real alternating storm-debt buff/debuff cycle. */
        if (h->alive) {
            float regen = ARENA_WEATHERMAN_PASSIVE_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        /* The Debt Compounds: fixed cold zone, damage-only tick -- same shape as NOOR-1's Do
           Not Approach directly above, the literal storm finally collecting. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_WEATHERMAN_R_RADIUS) {
                        apply_damage(foe, ARENA_WEATHERMAN_R_DPS);
                    }
                }
                arena_zone_damage_creeps(h->r_zone_x, h->r_zone_z, ARENA_WEATHERMAN_R_RADIUS, h->team, ARENA_WEATHERMAN_R_DPS);
            }
        }
        break;
    case ARENA_HERO_MNM:
        /* Burrow's own countdown, distinct from the shared intangible_ms/rooted_ms it also
           set at cast time (see the struct field's own doc comment) -- watched here purely
           to catch the exact tick it crosses to zero and fire the resurface eruption once,
           not every tick spent underground. */
        if (h->mnm_burrow_ms > 0) {
            h->mnm_burrow_ms -= (int)dt_ms;
            if (h->mnm_burrow_ms <= 0) {
                h->mnm_burrow_ms = 0;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->x, dz = foe->z - h->z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_MNM_BURROW_RADIUS) {
                        apply_damage(foe, apply_armor(ARENA_MNM_BURROW_DAMAGE, arena_hero_armor(foe)));
                    }
                }
                arena_zone_damage_creeps(h->x, h->z, ARENA_MNM_BURROW_RADIUS, h->team, ARENA_MNM_BURROW_DAMAGE);
            }
        }
        break;
    case ARENA_HERO_ZAGAN:
        /* Base Metal Screams (passive): checks EVERY enemy hero, not just the single nearest
           `foe` this function is handed -- Zagan "presides," he doesn't have to be the one
           landing the hit or even nearby, matching the lore's own omniscient framing. Loops
           arena_state.heroes[] directly rather than threading a wider foe list through this
           function's own signature. */
        for (int zi = 0; zi < ARENA_MAX_HEROES; zi++) {
            ArenaHero *e = &arena_state.heroes[zi];
            if (!e->active || !e->alive || e->team == h->team) continue;
            if (e->zagan_confessed) continue;
            if (e->max_hp > 0 && e->hp * 2 <= e->max_hp) {
                e->zagan_confessed = 1;
                h->flow += ARENA_ZAGAN_PASSIVE_CONFESSION_FLOW;
                h->flow_earned += ARENA_ZAGAN_PASSIVE_CONFESSION_FLOW;
            }
        }
        /* Conjunction (R): no per-tick work needed beyond the generic cooldown countdown --
           the mirror itself is computed live by arena_hero_armor every time it's read, not
           interpolated/stored here. Just the shared duration countdown, same shape as every
           other r_active_ms user above. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
        }
        break;
    case ARENA_HERO_CART:
        /* The delivery zone (W/R, see arena_cast_r's own CART case): checks EVERY hero, ally or
           foe, including the Cart itself -- "nobody, including its own controller, gets to
           request what" is the lore's own framing, not a flaw to design around. First hittable
           hero found within zone_radius triggers cart_trigger_delivery and the zone deactivates
           immediately (single-use, matching "shows up, leaves something... " -- not a
           repeat-damage-tick zone like Gunnr's/Ghost's own). */
        if (h->r_active_ms > 0) {
            for (int ci = 0; ci < ARENA_MAX_HEROES; ci++) {
                ArenaHero *cand = &arena_state.heroes[ci];
                if (!cand->active || !hero_is_hittable(cand)) continue;
                float dx = cand->x - h->r_zone_x, dz = cand->z - h->r_zone_z;
                if (dx * dx + dz * dz > h->zone_radius * h->zone_radius) continue;
                cart_trigger_delivery(h, cand);
                h->r_active_ms = 0;
                break;
            }
        }
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
        }
        break;
    default:
        break;
    }
}

/* bot_cast_kit_if_ready: simple heuristic AI for whichever hero the bot is
 * playing -- cast Q (then R, once available) whenever off cooldown and the
 * foe is within that ability's range. Not a real decision-making bot brain
 * (that's Phase E's problem, GAME_AI_NORTHSTAR.md), just enough to prove
 * the bot side can actually use a kit at all (Phase D's "both sides").
 *
 * S170-228: made non-static (declared in arena_game.h) so
 * apps/arena_training/src/headless.c can call it directly for owner 1's own
 * casting during training -- see arena_bot_tick_heuristic's own doc comment
 * for why training needs this and arena_bot_tick_heuristic to be stable,
 * callable independent of arena_update's own automatic (now RL-driven)
 * bot-tick path. */
void bot_cast_kit_if_ready(ArenaHero *bot, ArenaHero *foe) {
    if (!bot->alive || !foe->alive) return;
    float dx = foe->x - bot->x, dz = foe->z - bot->z;
    float dist = sqrtf(dx * dx + dz * dz);

    switch (bot->hero_id) {
    case ARENA_HERO_DUCK:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_DUCK_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_DUCK_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_UNICORN:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_UNICORN_Q_HIT_RADIUS * 2.0f) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_GHOST:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_GHOST_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_GHOST_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_FROG:
        /* Defensive kit, so the heuristic is defensive too: rewind when
           hurt, vanish when critical -- not "attack when in range" like
           the other three, since Frog has no damage-dealing ability. */
        if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        } else if (bot->hp < bot->max_hp / 2 && bot->q_cooldown_ms <= 0) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_DOC_WHEEL:
        /* This heuristic is 1v1-only local-demo AI, and Doc Wheel's entire
           kit is ally-targeted -- no useful action exists with no ally
           present (S170-45). Doc Wheel is a real, working pick in team
           mode via apps/arena_bot's own simpler "cast Q periodically"
           heuristic, which the server-side dispatch already handles
           correctly regardless of hero. Intentional no-op here, not a
           missing case. */
        break;
    case ARENA_HERO_TREE:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_TREE_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->hp < bot->max_hp / 3 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_PIZZA:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_PIZZA_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_FLAMEL:
        /* Q is the only foe-targeted piece of this kit -- W/R are ally-AoE
           and have no useful action in the 1v1 local demo's bot heuristic,
           same reasoning as Doc Wheel above. */
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_FLAMEL_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_MORRIGAN:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_MORRIGAN_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->w_cooldown_ms <= 0) {
            arena_toggle_w(bot->owner); /* Three Forms: closes distance on its own */
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_MORRIGAN_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_DAGDA:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_DAGDA_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->hp < bot->max_hp / 3 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_COURIER:
        if (bot->q_cooldown_ms <= 0) {
            arena_cast_q(bot->owner); /* dash-strike, closes distance on its own like Morrigan's W */
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_COURIER_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_LOKI:
        /* Q has no range gate (it's a swap, not a dash) so it's always
           usable off cooldown. W is a defensive stance -- toggle on under
           pressure, like Frog's heuristic. R is the survive-floor panic
           button, same threshold as Pizza/Dagda's. */
        if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        } else if (bot->q_cooldown_ms <= 0) {
            arena_cast_q(bot->owner);
        } else if (!bot->w_active && bot->hp < bot->max_hp / 2) {
            arena_toggle_w(bot->owner);
        }
        break;
    case ARENA_HERO_GARY:
        /* Stationary marksman -- S170-203: W (Aimed Shot) is real burst now, off cooldown and
           in range takes priority over the smaller Q poke; this internal bot AI doesn't reason
           about the cast being interruptible, same "first pass, not a masterclass" level every
           other bot heuristic in this switch already operates at. R when the foe is close
           enough to actually want rooted. */
        if (bot->w_cooldown_ms <= 0 && dist <= ARENA_GARY_W_RANGE) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_GARY_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_GARY_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_FLUTE_DEBT:
        /* Apply the debt with Q, then collect with R once it's landed --
           R deliberately checked first isn't right (R needs foe->burning_ms
           set by a prior Q), so Q leads and R follows once off cooldown. W
           is passive sustain, toggle on early like Loki's early instinct
           but without the pressure gate since it's just regen, not armor. */
        if (!bot->w_active) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_FLUTE_DEBT_Q_HIT_RADIUS) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_FLUTE_DEBT_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_BACON_PUCK:
        /* Q is defensive (self intangible, Frog's escape shape) -- use it
           when hurt, not on cooldown for its own sake. R is the primary
           damage/heal source whenever in range and off cooldown. */
        if (bot->hp < bot->max_hp / 3 && bot->q_cooldown_ms <= 0) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_BACON_PUCK_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_ABRAHAM:
        /* Real bug found and fixed 2026-08-26 (founder, live: "theres some issue with fireball
           its not casting" -> "also his auto attack is broken" -> "abraham"): this comment
           and the branch below both described Abraham's OLD kit ("channeled Q damage... toggle
           W") -- stale leftovers from before the S202-34 W rework (961d500) landed. A Line of
           Fire is a one-shot GROUND-TARGETED cast now, not a toggle: calling arena_toggle_w
           unconditionally here (like the old code did) never sets arena_state.has_ground_target
           first, so the new W code's own real guard (`if
           (!arena_state.has_ground_target[owner]) return;`) silently no-ops it every time --
           bot-controlled Abrahams have never actually cast a fireball since the rework landed.
           Fixed to set a real ground target (the current foe's position) before casting, same
           "aim at the current foe" heuristic every other ground-targeted bot cast in this
           switch already uses. Poke with Q whenever in range and off cooldown, cleanse+heal
           with R when hurt or carrying a debuff, unchanged from before. */
        if (bot->w_cooldown_ms <= 0 && bot->mp >= ARENA_MP_COST_W && foe && hero_is_hittable(foe) &&
            dist <= ARENA_ABRAHAM_FIREBALL_MAX_RANGE) {
            arena_set_ground_target(bot->owner, 1, foe->x, foe->z);
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_ABRAHAM_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 &&
                   (bot->hp < bot->max_hp / 2 || bot->silenced_ms > 0 || bot->rooted_ms > 0 || bot->burning_ms > 0)) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_ADA:
        /* Toggle W on early for the frame's armor, root with Q at range,
           finish with R once close enough. */
        if (!bot->w_active) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_ADA_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_ADA_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_TYLER:
        /* Root+DoT with Q at range, blink-strike with W to close distance,
           R (the vulnerability window) only when confident -- healthy and
           already in range, not a panic button like Loki's/Bacon+Puck's Q. */
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_TYLER_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->w_cooldown_ms <= 0) {
            arena_toggle_w(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_TYLER_R_RANGE && bot->hp > bot->max_hp / 2) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_PAIMON:
        /* Q for the ranged root+damage poke, W as the instant-decree
           follow-up, R (the zone) when the foe is close enough for it to
           matter -- same "Q leads, W/R follow once in range" shape as
           Ghost/Gary above. */
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_PAIMON_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->w_cooldown_ms <= 0 && dist <= ARENA_PAIMON_W_RANGE) {
            arena_toggle_w(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_PAIMON_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_NOOR1:
        /* Q for the ranged root+damage poke when in range, R (the zone)
           when the foe is close enough for it to matter -- same shape as
           Paimon above. W is a defensive self-intangibility, not a foe-
           ranged ability, so it's gated on low HP instead, same panic-
           button pattern as Loki's/Bacon+Puck's Q. */
        if (bot->hp < bot->max_hp / 4 && bot->w_cooldown_ms <= 0) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_NOOR1_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_NOOR1_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_CAIN:
        /* R is the survive-floor panic button, same threshold as every
           other hero that carries one. Q whenever in range and off
           cooldown. W (dash away) is defensive, not offensive, so it's
           gated on low HP too rather than proximity to a foe. */
        if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        } else if (bot->hp < bot->max_hp / 3 && bot->w_cooldown_ms <= 0) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_CAIN_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_GUNNR:
        /* Consecration (2026-07-30) is a real cast on a real cooldown now, not a free toggle --
           same "cast the zone whenever off cooldown and the foe is close enough to actually be
           caught in it" heuristic Ghost's own R-zone bot logic already uses. Q whenever in melee
           range and off cooldown. R when the foe is close enough for the execute to matter. */
        if (bot->w_cooldown_ms <= 0 && dist <= ARENA_GUNNR_W_RADIUS) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_GUNNR_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_GUNNR_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_VASSAGO:
        /* W is ally-targeted -- no useful action in the 1v1 local demo's bot
           heuristic (no ally present), same reasoning as Doc Wheel/Flamel's
           own W above. Q whenever in range and off cooldown, R when the foe
           is close enough for the zone to matter. */
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_VASSAGO_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_VASSAGO_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_HE_XIANGU:
        /* R is ally-only (a heal zone) -- no useful action in the 1v1 local
           demo's bot heuristic (no ally present), same reasoning as Doc
           Wheel/Flamel/Vassago's own ally-only slots above. W is free
           self-sustain, toggle on early like Loki's/Flute Debt's own
           instinct. Q whenever in range and off cooldown -- it self-heals
           too, always worth using. */
        if (!bot->w_active) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_HE_XIANGU_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_BELETH:
        /* W first when in range and off cooldown -- the silence buys the window
           everything else needs. R next: marks the zone and starts the fuse the instant
           a foe is close enough to be worth the long cooldown. Q whenever in range and
           off cooldown otherwise -- the reliable poke+burn. */
        if (bot->w_cooldown_ms <= 0 && dist <= ARENA_BELETH_W_RANGE) {
            arena_toggle_w(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_BELETH_R_RANGE) {
            arena_cast_r(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_BELETH_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_MNM:
        /* R is still the survive-floor panic button, same low-HP threshold as every other
           hero that carries one (Cain's own) -- checked first so a near-death R always wins
           over an opportunistic W. Burrow (S170-208) is no longer a free toggle to flip once
           and forget -- it's cast opportunistically, in range of its own eruption radius so
           the AoE is actually likely to land, same "off cooldown and in range" shape Q
           already uses. Q otherwise, whenever in melee range and off cooldown. */
        if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        } else if (bot->w_cooldown_ms <= 0 && dist <= ARENA_MNM_BURROW_RADIUS) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_MNM_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_WEATHERMAN:
        /* R when a foe is close enough to be worth the AoE zone's long cooldown, else Q
           whenever in range and off cooldown for the poke/knockback -- W deliberately omitted,
           same "leave a situational/conditional ability out of this simple heuristic" precedent
           Frog's own case above already sets (its whole value depends on a Donkey-wearing ally
           or enemy being mid-glide, not a general "use when off cooldown" heuristic). */
        if (bot->r_cooldown_ms <= 0 && dist <= ARENA_WEATHERMAN_R_RADIUS) {
            arena_cast_r(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_WEATHERMAN_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_ZAGAN:
        /* W (the stun) checked first -- landing CC before anything else matters most for a
           control kit. R next: lock the mirror onto whoever's in range right now (the bot
           heuristic doesn't try to reason about whether the target's armor is actually
           favorable -- same "simple heuristic, not real strategy" scope every other bot case
           here already accepts). Q otherwise, whenever in range and off cooldown. */
        if (bot->w_cooldown_ms <= 0 && dist <= ARENA_ZAGAN_W_RANGE) {
            arena_toggle_w(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_ZAGAN_R_RANGE) {
            arena_cast_r(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_ZAGAN_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_WARRIOR:
        /* All three are plain melee-range hits with no special condition (unlike Gunnr's
           execute-scaling or Zagan's CC-priority) -- biggest/longest-cooldown checked first so
           the bot doesn't waste Frostbite's window sitting on Hard Slash. */
        if (bot->r_cooldown_ms <= 0 && dist <= ARENA_WARRIOR_R_RANGE) {
            arena_cast_r(bot->owner);
        } else if (bot->w_cooldown_ms <= 0 && dist <= ARENA_WARRIOR_W_RANGE) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_WARRIOR_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_CART:
        /* No target/range gate on any of the three -- W/R are self-position zone casts, Q is a
           self-heal. R checked first (bigger delivery zone, longer cooldown, don't let it sit
           idle while W keeps firing); Q only when actually below max HP, not spammed for its
           own sake. */
        if (bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        } else if (bot->w_cooldown_ms <= 0) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && bot->hp < bot->max_hp) {
            arena_cast_q(bot->owner);
        }
        break;
    }
}

void arena_update(unsigned int dt_ms) {
    if (arena_state.winner != 0) return;
    float dt_sec = (float)dt_ms / 1000.0f;

    if (arena_bot_enabled) arena_bot_tick(dt_ms);

    /* If the player's hero is close enough to the bot, treat the last
       move-target as an attack-move: keep closing until in range. */
    ArenaHero *a = &arena_state.heroes[0];
    ArenaHero *b = &arena_state.heroes[1];
    if (a->alive && b->alive) {
        float dx = b->x - a->x, dz = b->z - a->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (a->moving && dist <= ARENA_HALF_EXTENT * 4.0f) {
            float tdx = a->target_x - b->x, tdz = a->target_z - b->z;
            if (sqrtf(tdx * tdx + tdz * tdz) < ARENA_ATTACK_RANGE * 3.0f && dist > ARENA_ATTACK_RANGE) {
                a->target_x = b->x;
                a->target_z = b->z;
            }
        }
    }

    update_hero_motion(&arena_state.heroes[0], 0, dt_sec);
    update_hero_motion(&arena_state.heroes[1], 1, dt_sec);
    arena_tick_creeps(dt_ms);
    arena_hero_attack_creeps(dt_ms);
    /* Node towers (2026-07-30) are team-mode only, same scope as lane creep waves just below --
       not called here at all, see arena_towers_reset's own doc comment above. */
    /* Lane creep waves (S170-139) are team-mode only, unlike node-guardian creeps --
       "pushing toward the enemy spawn" isn't a meaningful concept in this
       1v1 practice demo (no team-wide push objective exists here at all),
       and running them here would just be an unrequested third-party
       combatant intruding on solo practice matches/tests. See
       arena_update_teams() for the real integration. */
    resolve_combat(dt_ms);
    /* No ally in the 1v1 local path (S170-45: arena_nearest_ally only
       exists for team mode) -- NULL is the correct value, same NULL-safety
       hero_is_hittable already relies on elsewhere. */
    tick_hero_kit(&arena_state.heroes[0], &arena_state.heroes[1], NULL, dt_ms);
    tick_hero_kit(&arena_state.heroes[1], &arena_state.heroes[0], NULL, dt_ms);
    /* Gated the same as arena_bot_tick (movement) above -- without this, a
       real second player (owner 1) would still get their kit cast
       autonomously by the internal bot AI (including Duck's Q, which pulls
       the foe), fighting their own real cast commands. Found live, 2026-07-24:
       hero0 (owner 0, no move command ever sent) still moved and took
       damage in a server with zero connected clients, because this call
       wasn't gated -- Duck's Q was yanking it every time it came off
       cooldown. */
    if (arena_bot_enabled) {
        /* 2026-07-29: the trained RL policy now drives casting too, but ONLY for the exact
           pairing it was trained on (Unicorn/Duck, see arena_bot_tick_rl_cast's own doc
           comment) -- every other hero still gets the hand-authored per-hero heuristic,
           unchanged. arena_bot_tick above (movement) is unconditionally RL either way; this is
           the one place hero identity actually branches which brain drives the bot. */
        ArenaHeroID bot_hero = arena_state.heroes[1].hero_id;
        if (bot_hero == ARENA_HERO_UNICORN || bot_hero == ARENA_HERO_DUCK) {
            arena_bot_tick_rl_cast(&arena_state.heroes[1], &arena_state.heroes[0]);
        } else {
            bot_cast_kit_if_ready(&arena_state.heroes[1], &arena_state.heroes[0]);
        }
    }
    arena_tick_projectiles(dt_ms);
    arena_tick_fountains(dt_ms);
    arena_tick_powerups(dt_ms); /* S170-190 */
    /* Runs last (S170-51 cont'd): a capture channel is interrupted by
       damage taken this same tick (real Arathi Basin's own rule), so node
       state needs to see everything above -- creeps, melee, kit ticks,
       projectile hits, and the bot's own casts -- before deciding whether
       anyone's channel survives this tick. */
    arena_tick_nodes(dt_ms);

    /* Real, live bug found and fixed 2026-08-25 (founder, real-time: "im playing redgarden on
       latest and the tree is not generating health from auto attacking the other trees ensure
       the server knows about that and its all wired up to work"): arena_tick_daynight/
       arena_tick_obstacles/arena_hero_tree_passive were wired into arena_update_teams (below)
       when Bloodflower/Tree passive landed, but never into THIS function -- the 1v1 tick
       (lobby_size == 2, apps/arena_server/src/main.c's own `if (lobby_size == 2) arena_update
       else arena_update_teams` branch). The founder's own 1v1 matchmaker (:7779, lobby-size 2,
       per REDGARDEN/CLAUDE.md's own deployment table) runs exclusively through this function,
       so Tree's passive (and the day/night cycle + Bloodflower event) silently never fired
       there at all -- team-mode/bot-pool matches (:7778) were unaffected. Same class of gap
       §25.4's own "arena_update hardcodes heroes[0]/heroes[1]" bug already flagged: two
       parallel simulation-tick functions, one gets a new mechanic wired in, the other doesn't. */
    arena_tick_daynight(dt_ms);
    arena_tick_obstacles(dt_ms);
    arena_hero_tree_passive(dt_ms);

    if (!arena_state.heroes[0].alive) arena_state.winner = 2;
    else if (!arena_state.heroes[1].alive) arena_state.winner = 1;
}

/* ---- Team mode (2026-07-24, NORTHSTAR §13 cont'd: 10v10 (S170-183: reverted after briefly
   being 7v7 under S170-178), up to ARENA_TEAM_SIZE per side). Additive, not a replacement for the 1v1 local
   demo above -- arena_update()/arena_init_with_heroes() are untouched, so
   nothing about the existing solo-vs-bot practice mode changes. Every slot
   in team mode is filled by a real network client (human or a real
   apps/arena_bot process) -- there is no internal-bot-AI fallback here,
   unlike the 1v1 path's arena_bot_tick/bot_cast_kit_if_ready. ---- */

void arena_init_teams(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        int team = (i < ARENA_TEAM_SIZE) ? 0 : 1;
        int slot_in_team = (team == 0) ? i : (i - ARENA_TEAM_SIZE);
        /* 2026-07-29, founder: "move the initial spawn at start of game to the 2 graveyards
           not center of the map." Was a fixed spawn LINE near map center (x=+-8) -- moved to
           each team's own graveyard corner (arena_graveyard_position, same point creeps already
           spawn/march from since S170-161: "initially they spawn from the graveyards behind the
           nodes not the center," and the same point a dead hero without an owned node falls
           back to -- a team now starts, marches out from, and (with nothing else owned) returns
           to the exact same place, one coherent "graveyard" concept instead of two separate,
           differently-positioned ones).

           Real bug caught live before this landed: the old fan formula spread SYMMETRICALLY
           around the anchor point (+-9 along z), which was safe when the anchor was map-center
           spawn line (z=0, tons of room either way) but isn't at a graveyard corner -- a
           corner sits only ARENA_HALF_EXTENT-corner (~4 units) from the true map edge on its
           OWN outward side, so half the fanned slots landed past the boundary (measured: one
           hero at z=-56.78 against a +-51.78 map). Fans inward from the graveyard's own corner
           instead -- slot 0 sits exactly at the graveyard, each later slot steps further TOWARD
           map center (copysignf gives the correct inward direction for either team's corner
           sign) -- so the whole line of 10 stays safely inside the map by construction, no
           clamping (which would just stack the overflow slots on the boundary instead) needed. */
        float gx, gz;
        arena_graveyard_position(team, &gx, &gz);
        h->x = gx;
        h->z = gz - copysignf(1.0f, gz) * (float)slot_in_team * 2.0f;
        h->target_x = h->x;
        h->target_z = h->z;
        h->hp = h->max_hp = 100;
        h->mp = h->max_mp = ARENA_MP_MAX;
        h->owner = i;
        h->team = team;
        h->active = 1;
        h->alive = 1;
        h->hero_id = ARENA_HERO_UNICORN; /* placeholder until the real client's draft pick overrides it */
    }
    arena_nodes_reset_layout();
    arena_powerups_reset_layout(); /* S170-190 */
    arena_obstacles_reset_layout();
    arena_creeps_reset();
    /* Deliberately no arena_towers_reset() here -- this shared sim-level function is also called
       directly by ~300 existing unit tests that place heroes at convenient coordinates never
       expecting anything to auto-attack them there (some literally at the Blacksmith node's own
       (0,0)). Left at memset-zero (alive=0) by default; the real server calls
       arena_towers_reset() itself immediately after calling this function for an actual team-mode
       match (apps/arena_server/src/main.c) -- see that call site's own doc comment for why it
       lives there instead of here. */
    /* S170-139: lane creep waves get a short grace period before the first
       wave, same real-MOBA precedent as ARENA_LANE_WAVE_INITIAL_DELAY_MS's
       own doc comment -- memset already zeroed this to 0 (instant spawn),
       overridden here explicitly. */
    arena_state.lane_wave_timer_ms[0] = ARENA_LANE_WAVE_INITIAL_DELAY_MS;
    arena_state.lane_wave_timer_ms[1] = ARENA_LANE_WAVE_INITIAL_DELAY_MS;
    /* §25.3: memset already zeroed synergy_tier to 0 -- but 0 is TIER 0, the BEST tier (full
       cohesion, maximum ambient bonus), unlike every other buff field in this file where 0
       means "off." Left at memset-zero, every team-mode match (and every one of this file's own
       ~300 unit tests that call arena_init_teams) would silently start with the full synergy
       bonus already active -- a real bug caught while adding this feature, not a hypothetical
       (it broke real, pre-existing exact-cooldown-value tests elsewhere in this file the first
       time this landed). Explicitly starts at the fully-decayed (weakest, zero-bonus) tier
       instead -- arena_tick_synergy's own real roll cadence (every ARENA_SYNERGY_ROLL_INTERVAL_MS)
       establishes the real tier once actual gameplay begins, same "starts neutral, ticks change
       it" idiom every other timer/state field in this function already follows. */
    arena_state.synergy_tier[0] = ARENA_SYNERGY_TIER_COUNT - 1;
    arena_state.synergy_tier[1] = ARENA_SYNERGY_TIER_COUNT - 1;
    arena_state.winner = 0;
}

/* arena_team_owns_any_node (S170-121): node.owner is 1 = team 0, 2 = team 1
 * (see ArenaNode) -- this is the literal "controlling a node" gate the
 * founder asked for. */
static int arena_team_owns_any_node(int team) {
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        if (arena_state.nodes[n].owner == team + 1) return 1;
    }
    return 0;
}

/* arena_find_owned_node_for_respawn (S170-121): among nodes this team
 * currently owns, picks the one closest to that team's own home -- a simple
 * stand-in for a real "nearest owned outpost" choice without needing a
 * dedicated fixed-base concept this map doesn't otherwise have. "Home" is
 * the team's graveyard corner (arena_graveyard_position, 2026-07-29) --
 * used to be a hardcoded x=+-8-only distance matching the old central spawn
 * line arena_init_teams itself used before that same commit moved initial
 * spawn to the graveyard too; kept in sync here so this preference measures
 * from wherever "home" actually is now, not a stale reference point, and
 * uses real 2D distance since the graveyard sits on a diagonal, not the old
 * spawn line's own z=0 axis where x-only distance was equivalent to full
 * distance anyway. Returns NULL if the team owns nothing (caller must
 * already have checked arena_team_owns_any_node). */
static ArenaNode *arena_find_owned_node_for_respawn(int team) {
    float home_x, home_z;
    arena_graveyard_position(team, &home_x, &home_z);
    ArenaNode *best = NULL;
    float best_dist = 0.0f;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        ArenaNode *node = &arena_state.nodes[n];
        if (node->owner != team + 1) continue;
        float ddx = node->x - home_x, ddz = node->z - home_z;
        float dist = sqrtf(ddx * ddx + ddz * ddz);
        if (!best || dist < best_dist) {
            best = node;
            best_dist = dist;
        }
    }
    return best;
}

/* arena_respawn_hero: the actual "bring this one hero back" logic, shared
 * by the wave-respawn pass below. Prefers the nearest node the hero's team
 * currently owns (unchanged behavior, still the faster/closer-to-the-
 * action option); falls back to that team's own permanent graveyard
 * (S170-153) if they own nothing -- a team can always eventually come
 * back, never permanently locked out. */
static void arena_respawn_hero(ArenaHero *h, int slot_index) {
    float spawn_x, spawn_z;
    ArenaNode *node = arena_team_owns_any_node(h->team) ? arena_find_owned_node_for_respawn(h->team) : NULL;
    if (node) {
        spawn_x = node->x;
        spawn_z = node->z;
    } else {
        arena_graveyard_position(h->team, &spawn_x, &spawn_z);
    }

    /* Full clear (status effects, cooldowns, ability state) except the
       fields that must survive death: which hero this slot is playing,
       which team it's on, and (S170-175) its economy/stat progression --
       Flow/XP/kills/deaths/equipped items are earned across the whole
       match, not reset by dying, same "death costs tempo, not identity or
       progress" principle the graveyard/wave-respawn system itself was
       already built on (S170-153/154). */
    ArenaHeroID hero_id = h->hero_id;
    int team = h->team;
    int flow = h->flow;
    int flow_earned = h->flow_earned;
    int xp = h->xp;
    int kills = h->kills;
    int deaths = h->deaths;
    int equipped_item[ARENA_ITEM_SLOT_COUNT];
    for (int s = 0; s < ARENA_ITEM_SLOT_COUNT; s++) equipped_item[s] = h->equipped_item[s];
    memset(h, 0, sizeof(*h));
    h->active = 1;
    h->alive = 1;
    h->hp = h->max_hp = 100;
    h->mp = h->max_mp = ARENA_MP_MAX;
    h->owner = slot_index;
    h->team = team;
    h->hero_id = hero_id;
    h->x = h->target_x = spawn_x;
    h->z = h->target_z = spawn_z;
    h->flow = flow;
    h->flow_earned = flow_earned;
    h->xp = xp;
    h->kills = kills;
    h->deaths = deaths;
    for (int s = 0; s < ARENA_ITEM_SLOT_COUNT; s++) h->equipped_item[s] = equipped_item[s];
    /* attack_target/last_attacked_by_owner (real latent bug found while
       auditing this function for the fields above, fixed collaterally):
       memset above zeroes both to 0, which wrongly means "attacking/was-
       hit-by owner slot 0" rather than "nothing." A freshly-respawned
       hero would otherwise silently inherit a bogus attack lock on
       whoever happens to occupy owner slot 0, or misattribute a kill if
       they died again before ever being hit by anyone new -- same
       sentinel convention as everywhere else this field is initialized. */
    h->attack_target = -1;
    h->last_attacked_by_owner = -1;
    /* assist_owner (S170-187): same sentinel-after-memset fix as attack_target/
       last_attacked_by_owner just above -- a fresh respawn shouldn't remember who was
       attacking it before it died. assist_ms doesn't need a matching reset: it's already 0
       from the memset, and 0 already means "expired/inert," same convention this array's own
       tick-down already relies on. */
    for (int a = 0; a < ARENA_MAX_ASSIST_TRACK; a++) h->assist_owner[a] = -1;
    /* cast_target (S170-203): same sentinel convention as attack_target/last_attacked_by_owner
       above -- casting_slot is already correctly 0 ("not casting") from the memset, but 0 for
       cast_target would wrongly mean "casting at owner slot 0." A hero that respawns mid-cast
       already lost the cast anyway (death itself isn't one of this ability's own interrupt
       conditions, but a dead hero can't be casting by construction). */
    h->cast_target = -1;
    /* zagan_r_target (S170-230): same sentinel-after-memset fix as attack_target/cast_target
       above -- a fresh respawn shouldn't stay mirroring whoever happened to occupy owner slot
       0's armor. zagan_confessed also resets: a new life gets a fresh chance to "confess." */
    h->zagan_r_target = -1;
    h->zagan_confessed = 0;
    /* East/Music's Catchy Song relay (Jungle Camps Milestone 2): the memset above already
       cleared this hero's own king_music_carrier along with everything else. Re-grant it here
       if the team still has it -- "the moment ANY teammate respawns, they pick it up too," and
       (the other half of the same sentence) if NO teammate is currently alive+carrying, the
       buff has permanently lapsed and this respawn does NOT revive it. Entirely emergent from
       this one check -- no separate "buff has ended" flag needed, see king_music_carrier's own
       doc comment. */
    for (int mk = 0; mk < ARENA_MAX_HEROES; mk++) {
        const ArenaHero *ally = &arena_state.heroes[mk];
        if (ally == h || !ally->active || !ally->alive || ally->team != h->team) continue;
        if (ally->king_music_carrier) { h->king_music_carrier = 1; break; }
    }
    arena_recompute_item_stats(h);
}

/* arena_tick_respawns (S170-121, "controlling a node enables its spawn for
 * your team"; graveyard fallback + wave timing S170-153/154): before
 * S170-121, hero death was permanent for the rest of the match. S170-154,
 * founder: "respawns happen in 30 second waves" -- every dead, active hero
 * (any team) comes back together the instant the global
 * respawn_wave_timer_ms wraps, not each individually on their own timer
 * counted from their own death. Dying right before a wave costs almost
 * nothing; dying right after one costs almost the full
 * ARENA_RESPAWN_WAVE_MS -- real, intentional timing tension, not smoothed
 * away. */
static void arena_tick_respawns(unsigned int dt_ms) {
    arena_state.respawn_wave_timer_ms += (int)dt_ms;
    if (arena_state.respawn_wave_timer_ms < ARENA_RESPAWN_WAVE_MS) return;
    arena_state.respawn_wave_timer_ms -= ARENA_RESPAWN_WAVE_MS;

    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || h->alive) continue;
        arena_respawn_hero(h, i);
    }
}

/* arena_tick_resources (S170-153): see header declaration's doc comment. */
void arena_tick_resources(unsigned int dt_ms) {
    /* Node-count -> gain-per-tick, index = nodes owned (0..ARENA_NODE_COUNT).
       Deliberately more-than-linear -- controlling every node is meant to
       feel like a real sprint to the finish, not just "5x the 1-node rate",
       matching real Arathi Basin's own map-control bonus. */
    static const int GAIN_BY_NODE_COUNT[ARENA_NODE_COUNT + 1] = {0, 10, 20, 35, 55, 85};

    arena_state.resource_tick_ms += (int)dt_ms;
    while (arena_state.resource_tick_ms >= ARENA_RESOURCE_TICK_MS) {
        arena_state.resource_tick_ms -= ARENA_RESOURCE_TICK_MS;
        int nodes_owned[2] = {0, 0};
        for (int n = 0; n < ARENA_NODE_COUNT; n++) {
            int owner = arena_state.nodes[n].owner;
            if (owner == 1) nodes_owned[0]++;
            else if (owner == 2) nodes_owned[1]++;
        }
        for (int t = 0; t < 2; t++) {
            arena_state.resources[t] += GAIN_BY_NODE_COUNT[nodes_owned[t]];
            if (arena_state.resources[t] > ARENA_RESOURCE_CAP) arena_state.resources[t] = ARENA_RESOURCE_CAP;
        }
    }
}

void arena_update_teams(unsigned int dt_ms) {
    if (arena_state.winner != 0) return;
    float dt_sec = (float)dt_ms / 1000.0f;

    arena_tick_respawns(dt_ms);

    /* 2026-07-30, Tyler "Divided We Stand" rework -- founder: "clones multi control drag click
       all of it." Removed the old "clones mirror Tyler's own move-target every tick" block that
       lived here (S170-141) -- true Meepo parity means each net is independently commanded, not
       auto-following. Clones now just have real target_x/target_z/moving state of their own,
       set directly by arena_set_move_target (widened to accept clone owner slots) exactly like
       any other hero, and driven by the exact same update_hero_motion loop right below --
       nothing clone-specific left to do here at all. A freshly-spawned clone simply sits still
       at Tyler's position (tyler_spawn_clones' own spawn point) until its owner gives it an
       explicit command, same as a real Meepo net does the instant it lands. */
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active) continue;
        update_hero_motion(h, i, dt_sec);
    }
    arena_tick_creeps(dt_ms);
    arena_hero_attack_creeps(dt_ms);
    arena_tick_towers(dt_ms);
    arena_hero_attack_towers(dt_ms);
    arena_tick_lane_creeps(dt_ms);
    arena_hero_attack_lane_creeps(dt_ms);
    arena_tick_camp_minions(dt_ms); /* Jungle Camps Milestone 1 */
    arena_hero_attack_camp_minions(dt_ms);
    arena_tick_kings(dt_ms); /* Jungle Camps Milestone 2 */
    arena_hero_attack_kings(dt_ms);
    arena_tick_daynight(dt_ms); /* 2026-08-25: day/night cycle + moon-zenith Bloodflower event */
    arena_hero_claim_bloodflower();
    arena_tick_obstacles(dt_ms); /* 2026-08-25: Tree passive -- tree obstacle hp regen */
    arena_hero_tree_passive(dt_ms);

    /* Melee combat: each active, alive hero independently attacks its own
       nearest enemy if one is in range and its cooldown is ready -- this is
       the N-hero generalization of the 1v1 resolve_combat's hardcoded pair,
       and multiple heroes on one side can converge on the same target
       (a real team-fight dynamic the 1v1 pairwise version never had to
       handle). S170-141: bound widened to ARENA_HEROES_ARRAY_SIZE so
       Tyler's puppet clones fight through this exact same generic loop --
       both as attackers and (via arena_nearest_enemy, also widened) as
       valid targets for real enemy heroes. Clones deal/take the same flat
       ARENA_ATTACK_DAMAGE as any hero's plain auto-attack; they don't cast
       Q/W/R (see the tick_hero_kit loop below, deliberately not widened). */
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active) continue;
        if (h->attack_cooldown_ms > 0) h->attack_cooldown_ms -= (int)dt_ms;
        if (!h->alive) continue;
        /* S170-163: Gary's basic attack is a real homing ranged shot, not
           this flat melee tick -- fired from arena_tick_attack_targets
           below instead, on his own longer range/cooldown. Cooldown
           decrement above still applies to him uniformly (same field,
           same idiom), just the damage-dealing half of this loop skips
           him. */
        if (h->hero_id == ARENA_HERO_GARY || h->hero_id == ARENA_HERO_ABRAHAM) continue; /* S202-34: Abraham's basic attack is homing now too */
        if (h->stunned_ms > 0) continue; /* S170-184 */
        if (h->attack_windup_ms_remaining > 0) continue; /* already mid-windup -- arena_tick_attack_windups below owns it from here */
        if (h->mnm_burrow_ms > 0) continue; /* S170-208: literally not on the battlefield surface while burrowed */
        if (h->attack_cooldown_ms > 0) continue;
        ArenaHero *foe = arena_nearest_enemy(i);
        if (!foe) continue;
        float dx = foe->x - h->x, dz = foe->z - h->z;
        if (sqrtf(dx * dx + dz * dz) > ARENA_ATTACK_RANGE) continue;
        if (!hero_is_hittable(foe)) continue;
        /* S170-204, NORTHSTAR §17.1: begin windup instead of dealing damage instantly -- "does
           the champion stop when auto-attacking? yes." arena_tick_attack_windups (called after
           this loop) fires the actual hit once the windup completes, re-validating the target
           is still there and in range -- exactly the same "committed at windup start, target
           re-checked only at completion" shape Gary's Aimed Shot (S170-203) already uses. */
        h->attack_windup_ms_remaining = ARENA_ATTACK_WINDUP_MS;
    }

    /* NORTHSTAR.md §17.4 + §24 Milestone 2, 2026-07-31: attack-move's own opportunistic
       target-acquisition, run before the chase below so a freshly-acquired target gets chased/
       attacked in this same tick instead of wasting a frame. */
    arena_tick_attack_move(dt_ms);
    /* Patrol's own arrival/direction-flip + opportunistic engagement -- same "before the chase"
       ordering as attack-move just above, same reasoning. */
    arena_tick_patrol(dt_ms);
    /* S170-162: attack-target chase + Gary's homing-shot firing -- see this
       function's own doc comment. Runs after the melee loop above so
       Gary's attack_cooldown_ms (decremented in that same loop) reflects
       this tick before being checked here. */
    arena_tick_attack_targets(dt_ms);
    /* S170-204: resolves any windup begun above (melee loop) or just above (Gary) -- see its
       own doc comment for why it runs last. */
    arena_tick_attack_windups(dt_ms);

    /* Deliberately NOT widened to ARENA_HEROES_ARRAY_SIZE (S170-141): Tyler's
       puppet clones are melee-only auto-fighters, not independent casters --
       only the real, client-owned hero at clone_owner ever gets a genuine
       PACKET_ARENA_CAST/bot-AI cast decision, so ticking kits for the puppet
       range would just be dead weight (no cooldowns to advance, no aura to
       apply -- flagged in docs/HEROES_VS0.md's Tyler section as the one
       piece of "every clone shares TYLER's cooldowns" not built this pass). */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active) continue;
        tick_hero_kit(h, arena_nearest_enemy(i), arena_nearest_ally(i), dt_ms);
    }
    arena_tick_projectiles(dt_ms);
    arena_tick_fountains(dt_ms);
    arena_tick_powerups(dt_ms); /* S170-190 */
    /* Runs last, same reasoning as arena_update()'s own call site: a
       capture channel needs to see this whole tick's damage (creeps,
       melee, kit ticks, projectile hits) before deciding whether it's
       interrupted. */
    arena_tick_nodes(dt_ms);
    arena_tick_resources(dt_ms);
    arena_tick_synergy(dt_ms); /* §25.3 -- after resources so this tick's lead is current */

    /* S170-153: the win condition is now the Arathi Basin resource race,
       not team-wipe -- with S170-153's own graveyard fallback, a team can
       always eventually respawn, so "wipe the enemy" stopped being a
       reachable win condition at all (the old team0_alive/team1_alive +
       "owns no node" check this replaced is gone, not just supplemented;
       see ARENA_HERO_RESPAWN_MS's own doc comment for why). First team to
       ARENA_RESOURCE_CAP wins. */
    if (arena_state.resources[0] >= ARENA_RESOURCE_CAP) arena_state.winner = 1;
    else if (arena_state.resources[1] >= ARENA_RESOURCE_CAP) arena_state.winner = 2;

    /* S170-157 sudden-death fallback, founder: "i think there may be
       zombie games with infinite win cons." Removing team-wipe (above)
       also removed the only guarantee that a live match always eventually
       ends -- if node control keeps flipping without either team ever
       sustaining ownership long enough to fill the meter, nothing else
       forces resolution. Once a match runs ARENA_MATCH_MAX_DURATION_MS
       without either side reaching the cap, whoever's ahead on resources
       wins outright; an exact resource tie falls back to nodes currently
       owned; a still-exact tie (no nodes owned by either team) resolves
       to team 0 -- arbitrary, but deterministic, and this far down the
       fallback chain a coin flip is genuinely all that's left to decide
       on. */
    arena_state.match_elapsed_ms += (int)dt_ms;
    if (arena_state.winner == 0 && arena_state.match_elapsed_ms >= ARENA_MATCH_MAX_DURATION_MS) {
        if (arena_state.resources[0] > arena_state.resources[1]) {
            arena_state.winner = 1;
        } else if (arena_state.resources[1] > arena_state.resources[0]) {
            arena_state.winner = 2;
        } else {
            int nodes_owned[2] = {0, 0};
            for (int n = 0; n < ARENA_NODE_COUNT; n++) {
                int owner = arena_state.nodes[n].owner;
                if (owner == 1) nodes_owned[0]++;
                else if (owner == 2) nodes_owned[1]++;
            }
            arena_state.winner = (nodes_owned[1] > nodes_owned[0]) ? 2 : 1;
        }
    }
}
