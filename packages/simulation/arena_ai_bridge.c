#include "arena_ai_bridge.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const char *arena_hero_name(ArenaHeroID hero_id) {
    switch (hero_id) {
    case ARENA_HERO_UNICORN: return "unicorn";
    case ARENA_HERO_DUCK:    return "duck";
    case ARENA_HERO_GHOST:   return "ghost";
    case ARENA_HERO_FROG:    return "frog";
    case ARENA_HERO_DOC_WHEEL: return "doc_wheel";
    case ARENA_HERO_TREE:    return "tree";
    case ARENA_HERO_PIZZA:   return "pizza";
    case ARENA_HERO_FLAMEL:  return "flamel";
    case ARENA_HERO_MORRIGAN: return "morrigan";
    case ARENA_HERO_DAGDA:   return "dagda";
    case ARENA_HERO_COURIER: return "courier";
    case ARENA_HERO_LOKI:    return "loki";
    case ARENA_HERO_GARY:    return "gary";
    case ARENA_HERO_FLUTE_DEBT: return "flute_debt";
    case ARENA_HERO_BACON_PUCK: return "bacon_puck";
    case ARENA_HERO_ABRAHAM: return "abraham";
    case ARENA_HERO_ADA:     return "ada";
    case ARENA_HERO_TYLER:   return "tyler";
    case ARENA_HERO_PAIMON:  return "paimon";
    case ARENA_HERO_NOOR1:   return "noor1";
    case ARENA_HERO_CAIN:    return "cain";
    case ARENA_HERO_GUNNR:   return "gunnr";
    case ARENA_HERO_VASSAGO: return "vassago";
    case ARENA_HERO_HE_XIANGU: return "he_xiangu";
    case ARENA_HERO_BELETH:  return "beleth";
    case ARENA_HERO_MNM:     return "mnm";
    case ARENA_HERO_WEATHERMAN: return "weatherman"; /* S170-230: found missing while adding Zagan below -- real pre-existing gap, fixed alongside */
    case ARENA_HERO_ZAGAN:   return "zagan";
    case ARENA_HERO_WARRIOR: return "warrior";
    case ARENA_HERO_CART:    return "cart";
    case ARENA_HERO_MICHAEL: return "michael";
    default:                 return "unknown";
    }
}

/* arena_hero_w_is_toggle (S170-181, founder: "instead of initial mana cost toggle spells
 * should drain mana over time"): which heroes' W is a genuine hold-on/hold-off toggle
 * (arena_toggle_w's own `w_active = !w_active` cases) rather than an instant effect on
 * cooldown that happens to reuse the W slot (Ghost, Frog, Doc Wheel, etc., which keep the
 * old flat ARENA_MP_COST_W charge). Client-side HUD code needs this to know which mana-cost
 * model applies to a given hero's W tile -- kept here, next to arena_hero_name, rather than
 * duplicated client-side, so arena_toggle_w's own case list stays the single source of truth
 * a future new hero only has to update once. */
int arena_hero_w_is_toggle(ArenaHeroID hero_id) {
    switch (hero_id) {
    /* ARENA_HERO_GARY removed (S170-203): W stopped being a toggle and became Aimed Shot, a
       real cast-time ability on its own cooldown -- see ArenaHero's own casting_slot doc
       comment in arena_game.h. */
    case ARENA_HERO_UNICORN:
    case ARENA_HERO_LOKI:
    case ARENA_HERO_FLUTE_DEBT:
    case ARENA_HERO_BACON_PUCK:
    case ARENA_HERO_ADA:
    case ARENA_HERO_HE_XIANGU:
        return 1;
    /* ARENA_HERO_MNM removed (S170-208): W stopped being a free toggle armor stack and became
       Burrow, a real cast-time ability on its own cooldown (flat ARENA_MP_COST_W charge, same
       model Ghost/Frog/Doc Wheel already use) -- see arena_toggle_w's own MnM case.
       ARENA_HERO_GUNNR removed (2026-07-30): W stopped being a free toggle self-regen and became
       Consecration, a real cast-time ground zone on its own cooldown -- same reasoning as MnM's
       own removal just above.
       ARENA_HERO_ABRAHAM removed (S202-34): W stopped being a free toggle that boosted Q's
       damage and became A Line of Fire, a real ground-targeted cast-time ability on its own
       cooldown (flat ARENA_MP_COST_W charge) -- same reasoning as Gary/MnM/Gunnr's own removal
       above. */
    default:
        return 0;
    }
}

/* Real ability names, one row per hero, matching docs/HEROES_VS0.md exactly (S170-96/S170-112
 * follow-up: the HUD only ever showed generic "Q READY/CD", never which real ability that was).
 * {Q, W, R} -- kept short enough to fit the existing cooldown-strip HUD slots. */
const char *arena_ability_name(ArenaHeroID hero_id, int slot) {
    static const char *NAMES[ARENA_HERO_COUNT][3] = {
        [ARENA_HERO_UNICORN]    = {"DIAGNOSTIC CHARGE", "SPAGHETTI VENT", "FULL DISCLOSURE"},
        [ARENA_HERO_DUCK]       = {"TELEKINETIC YANK", "SMOKE BOMB", "TOTAL TELEKINESIS"},
        [ARENA_HERO_GHOST]      = {"ALIEN FREQUENCY", "NOT A GHOST", "RECITAL"},
        [ARENA_HERO_FROG]       = {"LOOP BACK", "BORROWED TIME", "THE SECRET"},
        [ARENA_HERO_DOC_WHEEL]  = {"BEDSIDE MANNER", "HOUSE CALL", "NO COMBAT POWER"},
        [ARENA_HERO_TREE]       = {"VINE LASH", "UNTRANSLATED", "GRAND SECRET"},
        [ARENA_HERO_PIZZA]      = {"NOBODY CHECKED", "I AM THE CHOSEN ONE", "NOBODY EVER CHECKS"},
        [ARENA_HERO_FLAMEL]     = {"VINE GROWTH", "PHILOSOPHER'S BLOOM", "ELIXIR OF WILD GROWTH"},
        [ARENA_HERO_MORRIGAN]   = {"THE WASHER'S STRIKE", "THREE FORMS", "THE CROW CONFIRMS IT"},
        [ARENA_HERO_DAGDA]      = {"THE WHEELED CLUB", "UAITHNE, CALLED BY NAME", "THE PORRIDGE"},
        [ARENA_HERO_COURIER]    = {"THE INSULT, LIGHTLY EDITED", "BETWEEN EAGLE AND SERPENT", "THE DEBT COLLECTOR'S DUE"},
        [ARENA_HERO_LOKI]       = {"INTERFERENCE, NOT A SIGNAL", "BOUND WHERE THE MYTH SAYS", "HELD FOR AS LONG AS THE MYTH DEMANDS"},
        [ARENA_HERO_GARY]       = {"THE PROPERTY", "AIMED SHOT", "SLOW DOWN, TRACK MEET"},
        [ARENA_HERO_FLUTE_DEBT] = {"THE WRONG NOTE", "RECOUPING INTEREST", "EVENTUALLY COLLECTS"},
        [ARENA_HERO_BACON_PUCK] = {"ASK AGAIN LATER", "WHICH ONE IS REAL", "THE TRICK WAS ALWAYS THE SAME"},
        [ARENA_HERO_ABRAHAM]    = {"THE SACRED MAGIC", "A LINE OF FIRE", "THE GUARDIAN ANGEL, CONTACTED"},
        [ARENA_HERO_ADA]        = {"THE ANALYTICAL ENGINE", "POETICAL SCIENCE", "FIRST PROGRAM, RUN LATE"},
        [ARENA_HERO_TYLER]      = {"EARTHBIND", "POOF", "DIVIDED WE STAND"},
        [ARENA_HERO_PAIMON]     = {"TEACHES ALL ARTS", "SPEAKS WITH TOTAL AUTHORITY", "TWO HUNDRED LEGIONS"},
        [ARENA_HERO_NOOR1]      = {"FILE WHAT IS ACTUALLY THERE", "SENT IN CLEAN", "DO NOT APPROACH"},
        [ARENA_HERO_CAIN]       = {"THE FIRST MURDER", "CURSED TO WANDER", "THE MARK"},
        [ARENA_HERO_GUNNR]      = {"ARGUED WITH A RAVEN", "CONSECRATION", "VALHALLA HAS YET TO ADMIT IT"}, /* 2026-07-30: W renamed, "just like wow" */
        [ARENA_HERO_VASSAGO]    = {"REVEAL THE GENTLE MAYBE", "THE SOFT FORESIGHT", "THE GENTLE MAYBE"},
        [ARENA_HERO_HE_XIANGU]  = {"MOTHER-OF-PEARL AND MOONLIGHT", "SELF-DENIAL", "NEVER FRAMED AS SACRIFICE"},
        [ARENA_HERO_BELETH]     = {"EVERY LOVE TRIANGLE", "HOPE IS A TERROR I LEASH WITH SONG", "THE DETONATION"},
        [ARENA_HERO_MNM]        = {"CLAMP DOWN", "BURROW", "ABSORBING HITS MEANT FOR SOMEBODY ELSE"},
        [ARENA_HERO_WEATHERMAN] = {"BAROMETRIC SHOVE", "COLLECTS ON WHAT'S OWED", "THE DEBT COMPOUNDS"},
        [ARENA_HERO_ZAGAN]      = {"CALCINATION", "THE STANDSTILL", "CONJUNCTION"},
        [ARENA_HERO_WARRIOR]    = {"HARD SLASH", "POWER SLASH", "FROSTBITE"}, /* real DragonsNShit Great Sword weapon skills, not invented lore names -- REDGARDEN_GUI_NORTHSTAR.md Milestone 1 */
        [ARENA_HERO_CART]       = {"MAINTENANCE", "NO REQUESTER IN THE LEDGER", "ALREADY WAITING"}, /* NORTHSTAR §24 Milestone 2 -- W/R names drawn from the character's own real seed phrase/lore framing, TYLER multiverse_heroes.md #10 */
        [ARENA_HERO_MICHAEL]    = {"FLAMING SWORD", "HEAVEN'S SHIELD", "RECAST VICTORY"}, /* R named after the character's own TYLER lore epithet, multiverse_heroes.md #124 */
    };
    if (hero_id < 0 || hero_id >= ARENA_HERO_COUNT || slot < 0 || slot > 2) return "?";
    const char *name = NAMES[hero_id][slot];
    return name ? name : "?";
}

/* Short mechanical blurbs, one row per hero (S170-151). Deliberately terse
 * -- a quick in-match reference, not the full docs/HEROES_VS0.md prose --
 * and deliberately UPPERCASE-safe (this client's own hand-drawn font folds
 * lowercase to upper anyway, see draw_char()), so no glyph the panel needs
 * is missing. */
const char *arena_ability_description(ArenaHeroID hero_id, int slot) {
    static const char *DESC[ARENA_HERO_COUNT][3] = {
        [ARENA_HERO_UNICORN]    = {"DASH FORWARD, DAMAGE ON LANDING", "TOGGLE: REGEN HP OVER TIME", "DOUBLES ARMOR FOR A WINDOW"},
        [ARENA_HERO_DUCK]       = {"PULLS NEAREST FOE TOWARD YOU, DAMAGE", "AOE: BLOCKS TARGETING FROM OUTSIDE", "BIGGER PULL + MORE DAMAGE"},
        [ARENA_HERO_GHOST]      = {"PROJECTILE: DAMAGE + SILENCE ON HIT", "TOGGLE: BECOME UNTARGETABLE", "ZONE: DAMAGE FOE, HEAL ALLY"},
        [ARENA_HERO_FROG]       = {"REWIND YOUR OWN POSITION + HP", "REFUNDS AN ALLY'S NEXT CAST", "VANISH, UNTARGETABLE A WHILE"},
        [ARENA_HERO_DOC_WHEEL]  = {"HEAL + CLEANSE HOVERED OR NEAREST ALLY", "TELEPORT TO NEAREST ALLY", "TEAMWIDE HEAL IN RADIUS"},
        [ARENA_HERO_TREE]       = {"ROOT + DAMAGE NEAREST FOE", "PASSIVE, NO EFFECT HERE", "SELF-ROOT, ARMOR + HEAL"},
        [ARENA_HERO_PIZZA]      = {"DAMAGE + BURN DOT ON FOE", "COSMETIC, NO EFFECT", "TEMPORARY DAMAGE FLOOR (CAN'T DIE)"},
        [ARENA_HERO_FLAMEL]     = {"ROOT NEAREST FOE, NO DAMAGE", "AOE HEAL, MORE ON MARKED GROUND", "ZONE: ROOT FOES, HEAL ALLIES"},
        [ARENA_HERO_MORRIGAN]   = {"DAMAGE, MORE AT LOW TARGET HP", "GAP-CLOSE + ROOT NEAREST FOE", "ZONE DOT, MORE AT LOW TARGET HP"},
        [ARENA_HERO_DAGDA]      = {"KILLING END DAMAGES, OTHER END HEALS", "AOE: ROOT+SILENCE FOES, HEAL ALLIES", "DAMAGE FLOOR + SELF-HEAL"},
        [ARENA_HERO_COURIER]    = {"DASH-STRIKE, CLEANSES YOUR DEBUFFS", "TELEPORT TO THE FARTHER NODE", "LIFE-DRAIN EXECUTE ON NEAREST FOE"},
        [ARENA_HERO_LOKI]       = {"INSTANT SWAP WITH NEAREST FOE + HIT", "TOGGLE: FLAT ARMOR BONUS", "TEMPORARY DAMAGE FLOOR (CAN'T DIE)"},
        [ARENA_HERO_GARY]       = {"PROJECTILE SNIPER SHOT, DODGEABLE", "CAST-TIME BIG DAMAGE, INTERRUPTIBLE", "ROOT NEAREST FOE"},
        [ARENA_HERO_FLUTE_DEBT] = {"PROJECTILE: DAMAGE + BURN DOT", "TOGGLE: SELF HP REGEN", "BONUS DAMAGE IF FOE'S DEBT STILL ACTIVE"},
        [ARENA_HERO_BACON_PUCK] = {"SELF UNTARGETABLE FOR A WINDOW", "TOGGLE: EXTENDS Q'S DURATION", "DAMAGE + SELF-HEAL OFF IT"},
        [ARENA_HERO_ABRAHAM]    = {"RANGED MAGIC BOLT", "GROUND-TARGET PROJECTILE, PIERCES, LONG RANGE", "CLEANSE ALL DEBUFFS + SELF-HEAL"},
        [ARENA_HERO_ADA]        = {"ROOT NEAREST FOE, NO DAMAGE", "TOGGLE: FLAT ARMOR BONUS", "BURST DAMAGE + SHORT ROOT"},
        [ARENA_HERO_TYLER]      = {"PROJECTILE NET: ROOT + BURN DOT", "BLINK-STRIKE TO NEAREST FOE", "HIT HARD, OWN ARMOR GOES NEGATIVE"},
        [ARENA_HERO_PAIMON]     = {"DAMAGE + ROOT NEAREST FOE", "DAMAGE + SILENCE NEAREST FOE", "ZONE: DAMAGE FOES, HEAL ALLIES"},
        [ARENA_HERO_NOOR1]      = {"PROJECTILE: DAMAGE + ROOT", "SELF UNTARGETABLE FOR A WINDOW", "COLD ZONE, DAMAGE ONLY"},
        [ARENA_HERO_CAIN]       = {"DAMAGE, MORE AT LOW TARGET HP", "DASH AWAY FROM FOE, CLEANSE SELF", "TEMPORARY DAMAGE FLOOR (CAN'T DIE)"},
        [ARENA_HERO_GUNNR]      = {"MELEE-RANGE DIRECT STRIKE", "ZONE: DAMAGE OVER TIME AT SELF", "DAMAGE + STUN, MORE DAMAGE AT LOW TARGET HP"},
        [ARENA_HERO_VASSAGO]    = {"DAMAGE + SILENCE NEAREST FOE", "REFUNDS AN ALLY'S NEXT CAST", "ZONE: SILENCE ONLY, NO DAMAGE"},
        [ARENA_HERO_HE_XIANGU]  = {"DAMAGE + SELF-HEAL OFF IT", "TOGGLE: BOOSTS SELF REGEN", "ZONE: HEAL ALLIES, NO DAMAGE"},
        [ARENA_HERO_BELETH]     = {"PROJECTILE: DAMAGE + BURN DOT", "SILENCE NEAREST FOE, NO DAMAGE", "MARKS A SPOT, BIG DELAYED BURST"},
        [ARENA_HERO_MNM]        = {"MELEE ROOT + DAMAGE", "UNTARGETABLE, SMALL AOE ON RETURN", "SELF-ROOT + TEMPORARY DAMAGE FLOOR"},
        [ARENA_HERO_WEATHERMAN] = {"RANGED KNOCKBACK, NO DAMAGE", "GROUNDS/EXTENDS DONKEY'S GLIDE", "ZONE: DAMAGE OVER TIME"},
        [ARENA_HERO_ZAGAN]      = {"DAMAGE + LINGERING ARMOR SHRED", "STUN A NEARBY FOE", "MIRROR A FOE'S ARMOR FOR A WINDOW"},
        [ARENA_HERO_WARRIOR]    = {"MELEE-RANGE DIRECT STRIKE", "HARDER MELEE-RANGE STRIKE", "HARDEST MELEE-RANGE STRIKE"},
        [ARENA_HERO_CART]       = {"SMALL SELF-HEAL", "ZONE: RANDOM DELIVERY, ANYONE CAN TRIGGER IT", "BIGGER ZONE, SAME RANDOM DELIVERY"},
        [ARENA_HERO_MICHAEL]    = {"STRONG DIRECT DAMAGE HIT", "SELF: STRONG TEMPORARY SHIELD", "HEAL ALLY, MORE AT LOW TARGET HP"},
    };
    if (hero_id < 0 || hero_id >= ARENA_HERO_COUNT || slot < 0 || slot > 2) return "?";
    const char *desc = DESC[hero_id][slot];
    return desc ? desc : "?";
}

/* ArenaHeroTags / ARENA_HERO_TAGS / arena_hero_tags_string (S170-194, NORTHSTAR §18.6's own
 * "the stronger lever" for cross-hero transfer -- founder: "do the work to prepare for
 * unsupervised learning"). Mechanical-shape tags describing WHAT a hero's kit does, not WHICH
 * hero it is, so a future ranged hero that also gets a homing basic attack (say) carries the
 * same has_homing_attack tag Gary's own training data already carries -- kiting/positioning
 * patterns learned against Gary transfer to that new hero from day one, before it has any
 * replay data of its own. Exact tag vocabulary and per-hero values are the first real pass
 * §18.7 explicitly named as still open ("not enumerated against the full 26-hero roster ...
 * a real design pass of its own") -- classified here from docs/HEROES_VS0.md's own kit
 * writeups, one honest judgment call per hero where a kit blurb is genuinely ambiguous on
 * range (noted inline); refining any single hero's tags later is cheap and doesn't touch this
 * function's own shape. is_ranged is the one mutually-exclusive pair (every hero is one or the
 * other); the rest are independent flags, a hero can carry any combination or none. */
typedef struct {
    int is_ranged;         /* has a real ranged/projectile/zone damage tool, vs. melee-only */
    int has_homing_attack; /* BASIC AUTO-ATTACK itself is a homing shot, not just a ranged ability (S170-163) -- Gary only, so far */
    int has_knockback;     /* forces the target's position (pull, swap, displacement) */
    int has_heal;          /* heals self or an ally, anywhere in the kit */
    int has_dash;          /* gap-closer, blink, or forced self-reposition */
    int has_stealth;       /* grants intangibility/untargetability */
    /* has_shield (2026-09-11, Michael's Heaven's Shield): grants real damage-absorption shield_hp
     * anywhere in the kit -- a mechanically distinct shape from has_heal (heal reduces damage
     * ALREADY taken, a shield prevents it from landing at all), so a bot's engagement calculus
     * against a shielded hero should genuinely differ from one against a healer. Appended at the
     * end (positional initializers with fewer values than members zero-fill the rest in standard
     * C), same convention this file's own ArenaItemDef precedent already established -- none of
     * the existing 30 heroes' own rows below needed touching. */
    int has_shield;
} ArenaHeroTags;

static const ArenaHeroTags ARENA_HERO_TAGS[ARENA_HERO_COUNT] = {
    [ARENA_HERO_UNICORN]    = { 0, 0, 0, 1, 1, 0 }, /* Q dash, W regen toggle */
    [ARENA_HERO_DUCK]       = { 0, 0, 1, 0, 0, 0 }, /* Q pulls the foe -- a forced displacement */
    [ARENA_HERO_GHOST]      = { 1, 0, 0, 1, 0, 1 }, /* Q projectile, W untargetable, R heals ally */
    [ARENA_HERO_FROG]       = { 0, 0, 0, 0, 0, 1 }, /* R vanish */
    [ARENA_HERO_DOC_WHEEL]  = { 0, 0, 0, 1, 1, 0 }, /* Q/R heal, W teleport-to-ally */
    [ARENA_HERO_TREE]       = { 1, 0, 0, 1, 0, 0 }, /* Q same "instant-hit-if-in-range" ranged-root shape as Ghost's/Flamel's own Q, per docs/HEROES_VS0.md; R heals */
    [ARENA_HERO_PIZZA]      = { 1, 0, 0, 0, 0, 0 }, /* Q burn DoT reads as a ranged poke */
    [ARENA_HERO_FLAMEL]     = { 1, 0, 0, 1, 0, 0 }, /* same "instant-hit-if-in-range" Q shape as Tree/Ghost; W/R heal */
    [ARENA_HERO_MORRIGAN]   = { 0, 0, 0, 0, 1, 0 }, /* W gap-close+root */
    [ARENA_HERO_DAGDA]      = { 0, 0, 0, 1, 0, 0 }, /* Q/W both heal */
    [ARENA_HERO_COURIER]    = { 0, 0, 0, 1, 1, 0 }, /* Q dash-strike, W teleport, R life-drain (self-heal) */
    [ARENA_HERO_LOKI]       = { 0, 0, 1, 0, 1, 0 }, /* Q instant swap -- both a blink and a forced displacement */
    [ARENA_HERO_GARY]       = { 1, 1, 0, 0, 0, 0 }, /* the one hero whose basic auto-attack itself homes, S170-163 */
    [ARENA_HERO_FLUTE_DEBT] = { 1, 0, 0, 1, 0, 0 }, /* Q projectile, W self-regen toggle */
    [ARENA_HERO_BACON_PUCK] = { 0, 0, 0, 1, 0, 1 }, /* Q untargetable, R self-heal */
    [ARENA_HERO_ABRAHAM]    = { 1, 1, 0, 1, 0, 0 }, /* Q magic bolt, W ground-targeted piercing fireball, R self-heal; S202-34: basic auto-attack is now a real homing shot too, matching Gary's own has_homing_attack */
    [ARENA_HERO_ADA]        = { 0, 0, 0, 0, 0, 0 }, /* Q root reads melee-range per docs, no ranged/heal/dash/stealth tool */
    [ARENA_HERO_TYLER]      = { 1, 0, 0, 0, 1, 0 }, /* Q projectile net, W blink-strike */
    [ARENA_HERO_PAIMON]     = { 1, 0, 0, 1, 0, 0 }, /* Q "ranged bolt" per docs, R heals allies */
    [ARENA_HERO_NOOR1]      = { 1, 0, 0, 0, 0, 1 }, /* Q projectile, W untargetable */
    [ARENA_HERO_CAIN]       = { 0, 0, 0, 0, 1, 0 }, /* W dash away from the foe */
    [ARENA_HERO_GUNNR]      = { 1, 0, 0, 0, 0, 0 }, /* 2026-07-30: W is now Consecration, a real zone damage tool -- is_ranged flips true, has_heal drops (the old self-regen toggle is gone); Q still explicitly "melee-range direct strike" per docs */
    [ARENA_HERO_VASSAGO]    = { 1, 0, 0, 0, 0, 0 }, /* Q "ranged bolt" per docs */
    [ARENA_HERO_HE_XIANGU]  = { 0, 0, 0, 1, 0, 0 }, /* Q/W/R all heal-shaped, kit reads melee-range */
    [ARENA_HERO_BELETH]     = { 1, 0, 0, 0, 0, 0 }, /* Q projectile burn */
    [ARENA_HERO_MNM]        = { 0, 0, 0, 0, 0, 1 }, /* Q explicitly "melee root+damage" per docs; W Burrow grants untargetability (S170-208) */
    [ARENA_HERO_WEATHERMAN] = { 1, 0, 1, 1, 0, 0 }, /* Q ranged + a real knockback (the roster's first push-outward Q); passive is a heal-shaped regen */
    [ARENA_HERO_ZAGAN]      = { 1, 0, 0, 0, 0, 0 }, /* Q is a real ranged poke (5.0 range, same "instant-hit-if-in-range" shape as Tree/Ghost/Pizza); no heal/knockback/dash/stealth anywhere in the kit */
    [ARENA_HERO_WARRIOR]    = { 0, 0, 0, 0, 0, 0 }, /* all three real weapon skills are melee-range hits, no ranged/knockback/heal/dash/stealth tool anywhere in the kit */
    [ARENA_HERO_CART]       = { 1, 0, 0, 1, 0, 0 }, /* W/R are zone-shaped (reads ranged); Q and a possible delivery roll both heal -- has_heal true; no knockback/dash/stealth anywhere in the kit */
    [ARENA_HERO_MICHAEL]    = { 1, 0, 0, 1, 0, 0, 1 }, /* Q is a real ranged poke (5.5 range, same "instant-hit-if-in-range" shape as Tree/Abraham/Flamel/Zagan); R heals an ally; W grants a real shield -- this roster's first has_shield=1 */
};

/* arena_hero_tags_string writes a space-separated list of hero_id's TRUE tags into out (empty
 * string if none, same "only show what's active" idiom hero_status_label already uses for
 * status effects). is_ranged always prints as "ranged" or "melee" (the one mutually-exclusive
 * pair); every other tag is independent and only appears when true. */
void arena_hero_tags_string(ArenaHeroID hero_id, char *out, size_t out_len) {
    if (out_len == 0) return;
    out[0] = '\0';
    if (hero_id < 0 || hero_id >= ARENA_HERO_COUNT) return;
    const ArenaHeroTags *t = &ARENA_HERO_TAGS[hero_id];
    size_t used = 0;
#define APPEND(tag) do { \
        int n = snprintf(out + used, out_len - used, "%s%s", used > 0 ? " " : "", tag); \
        if (n > 0 && (size_t)n < out_len - used) used += (size_t)n; \
    } while (0)
    APPEND(t->is_ranged ? "ranged" : "melee");
    if (t->has_homing_attack) APPEND("has_homing_attack");
    if (t->has_knockback) APPEND("has_knockback");
    if (t->has_heal) APPEND("has_heal");
    if (t->has_dash) APPEND("has_dash");
    if (t->has_stealth) APPEND("has_stealth");
    if (t->has_shield) APPEND("has_shield");
#undef APPEND
}

void arena_serialize_state(int owner, unsigned int tick_ms, char *out, size_t out_len) {
    if (out_len == 0) return;
    out[0] = '\0';
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;

    const ArenaHero *self = &arena_state.heroes[owner];
    if (!self->active) return;
    /* S170-194 bugfix, found while preparing this function for the unsupervised-learning
       corpus: this used to hardcode `owner > 1` out of range and `heroes[1-owner]` as "the
       foe" -- correct ONLY for the 1v1 local demo this function was originally built against
       (S170-36, before team mode existed at all), silently unusable for every team-mode match
       since. Team mode is now this game's primary mode and the actual source of most of the
       replay corpus NORTHSTAR §18.4 names ("every replay log from every hero, from every
       match") -- excluding it meant excluding almost the whole corpus. arena_nearest_enemy is
       the same real team-aware lookup every other system in this file already uses; NULL (no
       living enemy left) writes a self-only state rather than crashing or reading garbage. */
    const ArenaHero *foe = arena_nearest_enemy(owner);
    char self_tags[96], foe_tags[96];
    arena_hero_tags_string(self->hero_id, self_tags, sizeof(self_tags));
    if (!foe) {
        snprintf(out, out_len,
            "redgarden arena tick:%u\n"
            "self hero:%s tags:%s pos:%.2f,%.2f hp:%d max_hp:%d alive:%d "
            "q_cd:%d w_active:%d w_cd:%d r_cd:%d r_active:%d silenced:%d intangible:%d\n"
            "foe none",
            tick_ms,
            arena_hero_name(self->hero_id), self_tags, self->x, self->z, self->hp, self->max_hp, self->alive,
            self->q_cooldown_ms, self->w_active, self->w_cooldown_ms, self->r_cooldown_ms,
            self->r_active_ms, self->silenced_ms, self->intangible_ms);
        return;
    }
    arena_hero_tags_string(foe->hero_id, foe_tags, sizeof(foe_tags));
    float dx = foe->x - self->x, dz = foe->z - self->z;
    float dist = sqrtf(dx * dx + dz * dz);

    snprintf(out, out_len,
        "redgarden arena tick:%u\n"
        "self hero:%s tags:%s pos:%.2f,%.2f hp:%d max_hp:%d alive:%d "
        "q_cd:%d w_active:%d w_cd:%d r_cd:%d r_active:%d silenced:%d intangible:%d\n"
        "foe hero:%s tags:%s pos:%.2f,%.2f hp:%d max_hp:%d alive:%d dist:%.2f "
        "q_cd:%d w_cd:%d r_cd:%d r_active:%d silenced:%d intangible:%d",
        tick_ms,
        arena_hero_name(self->hero_id), self_tags, self->x, self->z, self->hp, self->max_hp, self->alive,
        self->q_cooldown_ms, self->w_active, self->w_cooldown_ms, self->r_cooldown_ms,
        self->r_active_ms, self->silenced_ms, self->intangible_ms,
        arena_hero_name(foe->hero_id), foe_tags, foe->x, foe->z, foe->hp, foe->max_hp, foe->alive, dist,
        foe->q_cooldown_ms, foe->w_cooldown_ms, foe->r_cooldown_ms, foe->r_active_ms,
        foe->silenced_ms, foe->intangible_ms);
}

int arena_decode_action(const char *action_str, ArenaAction *out) {
    memset(out, 0, sizeof(*out));
    if (!action_str) return 0;
    int found = 0;

    const char *mp = strstr(action_str, "move:");
    if (mp) {
        float mx, mz;
        if (sscanf(mp, "move:%f,%f", &mx, &mz) == 2) {
            out->move_x = mx;
            out->move_z = mz;
            out->has_move = 1;
            found = 1;
        }
    }

    const char *p;
    if ((p = strstr(action_str, "cast_q:")) != NULL) {
        out->cast_q = (p[7] == '1');
        found = 1;
    }
    if ((p = strstr(action_str, "cast_w:")) != NULL) {
        out->cast_w = (p[7] == '1');
        found = 1;
    }
    if ((p = strstr(action_str, "cast_r:")) != NULL) {
        out->cast_r = (p[7] == '1');
        found = 1;
    }

    return found;
}

/* json_escape_append: appends src to out (starting at *used), escaping the two characters that
 * would otherwise break a JSON string literal -- '"' and the real newlines
 * arena_serialize_state's own self/foe sections are separated by. No other escaping needed:
 * every field this corpus ever serializes is ASCII hero names/numbers/tags, never arbitrary
 * user text. Truncates cleanly (never writes past out_len) rather than overflowing. */
static void json_escape_append(char *out, size_t out_len, size_t *used, const char *src) {
    for (; *src && *used < out_len - 1; src++) {
        char c = *src;
        if (c == '"' || c == '\\') {
            if (*used + 2 > out_len - 1) break;
            out[(*used)++] = '\\';
            out[(*used)++] = c;
        } else if (c == '\n') {
            if (*used + 2 > out_len - 1) break;
            out[(*used)++] = '\\';
            out[(*used)++] = 'n';
        } else {
            out[(*used)++] = c;
        }
    }
    out[*used] = '\0';
}

void arena_corpus_record(int owner, unsigned int tick_ms, char *out, size_t out_len) {
    if (out_len == 0) return;
    out[0] = '\0';
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    const ArenaHero *h = &arena_state.heroes[owner];
    if (!h->active) return;

    char state[768];
    arena_serialize_state(owner, tick_ms, state, sizeof(state));
    if (state[0] == '\0') return;

    /* Same "move:x,z cast_q:0/1 cast_w:0/1 cast_r:0/1" shape arena_decode_action already
       parses -- this hero's own CURRENT in-flight action, not necessarily a decision freshly
       made this exact tick (most ticks continue an already-issued move, same "current input
       state" framing NORTHSTAR §12 Phase E's own SHANKPIT example already uses, not "a new
       decision just happened"). cast_flash_slot is the real one-tick-only "a cast just landed
       its cooldown gate" signal (protocol.h's own doc comment) -- exactly the moment a real
       new cast decision is visible in this data. */
    char action[128];
    snprintf(action, sizeof(action), "action: move:%.2f,%.2f cast_q:%d cast_w:%d cast_r:%d",
             h->moving ? h->target_x : h->x, h->moving ? h->target_z : h->z,
             h->cast_flash_slot == 1, h->cast_flash_slot == 2, h->cast_flash_slot == 3);

    char text[1024];
    size_t used = 0;
    json_escape_append(text, sizeof(text), &used, state);
    json_escape_append(text, sizeof(text), &used, "\n");
    json_escape_append(text, sizeof(text), &used, action);

    snprintf(out, out_len, "{\"text\":\"%s\"}", text);
}
