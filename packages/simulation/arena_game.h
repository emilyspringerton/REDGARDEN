#ifndef ARENA_GAME_H
#define ARENA_GAME_H

#define ARENA_HALF_EXTENT 12.0f
#define ARENA_HERO_SPEED 4.0f      /* units/sec */
#define ARENA_ATTACK_RANGE 1.6f
#define ARENA_ATTACK_DAMAGE 8
#define ARENA_ATTACK_COOLDOWN_MS 700
#define ARENA_NODE_COUNT 2

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
} ArenaHeroID;

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
 * S170-32's roster audit. W (Borrowed Time, ally-cooldown-refund) is
 * ally-targeted -- no ally in 1v1, skipped, flagged. R (The Secret) is
 * simplified to reuse Ghost's intangible_ms mechanic at a longer duration;
 * "reappear at any visited location" needs its own location-memory system,
 * deferred, not faked as the full ability. Passive (Never Told Anyone, no
 * visible cooldown UI for enemies) is a bluffing/UI concept -- arena has no
 * separate enemy-facing view to hide anything from, skipped, flagged. */
#define ARENA_FROG_LOOPBACK_SAMPLE_MS 250 /* Q — Loop Back: how often position/HP is sampled */
#define ARENA_FROG_LOOPBACK_SLOTS     16  /* 16 * 250ms = 4000ms of history, enough to rewind 3s */
#define ARENA_FROG_Q_REWIND_MS      3000
#define ARENA_FROG_Q_COOLDOWN_MS    8000
#define ARENA_FROG_R_VANISH_MS      5000  /* The Secret, simplified (see comment above) */
#define ARENA_FROG_R_COOLDOWN_MS    25000

typedef struct {
    float x, z;
    float target_x, target_z;
    int moving;
    int hp;
    int max_hp;
    int attack_cooldown_ms;
    int owner; /* 0 = player, 1 = bot */
    int alive;
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
} ArenaNode;

typedef struct {
    ArenaHero heroes[2];
    ArenaNode nodes[ARENA_NODE_COUNT];
    int winner; /* 0 = none yet, 1 = player, 2 = bot */
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

/* Kit casts dispatch on the hero's hero_id, not a hardcoded owner check
 * (S170-31 generalized this from S170-18's Unicorn-only version). No-ops
 * if the hero's kit doesn't have that ability, or if it's on cooldown. */
void arena_cast_q(int owner);
void arena_toggle_w(int owner);
void arena_cast_r(int owner);
float arena_hero_armor(const ArenaHero *h); /* effective armor, incl. Unicorn R's buff */

#endif
