#ifndef ARENA_GAME_H
#define ARENA_GAME_H

#define ARENA_HALF_EXTENT 12.0f
#define ARENA_HERO_SPEED 4.0f      /* units/sec */
#define ARENA_ATTACK_RANGE 1.6f
#define ARENA_ATTACK_DAMAGE 8
#define ARENA_ATTACK_COOLDOWN_MS 700
#define ARENA_NODE_COUNT 2

/* The Unicorn (docs/HEROES_VS0.md) — first real hero kit wired into the
 * arena demo, owner 0 (player) only for this pass. Bot (owner 1) stays
 * plain melee; this proves the integration path, not the full roster
 * (EMILY/BACKLOG.md S170-18). */
#define ARENA_UNICORN_ARMOR         4    /* passive: Chassis Claim, flat dmg reduction */
#define ARENA_UNICORN_Q_DASH_DIST   4.0f /* Diagnostic Charge */
#define ARENA_UNICORN_Q_DAMAGE      12
#define ARENA_UNICORN_Q_HIT_RADIUS  1.8f
#define ARENA_UNICORN_Q_COOLDOWN_MS 4000
#define ARENA_UNICORN_W_REGEN_PER_SEC 6  /* Spaghetti Vent, while toggled on */
#define ARENA_UNICORN_R_COOLDOWN_MS 15000
#define ARENA_UNICORN_R_DURATION_MS 3000 /* Full Disclosure: armor doubled */

typedef struct {
    float x, z;
    float target_x, target_z;
    int moving;
    int hp;
    int max_hp;
    int attack_cooldown_ms;
    int owner; /* 0 = player, 1 = bot */
    int alive;
    /* Unicorn kit state (meaningful for owner 0 only this pass) */
    int q_cooldown_ms;
    int w_active;      /* Spaghetti Vent toggle */
    int r_cooldown_ms;
    int r_active_ms;   /* remaining duration of Full Disclosure's armor-double */
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

void arena_init(void);
void arena_update(unsigned int dt_ms);
void arena_set_move_target(int owner, float x, float z);
void arena_bot_tick(unsigned int dt_ms);

/* The Unicorn's kit — owner 0 (player) only this pass. No-ops if on cooldown. */
void arena_cast_q(int owner);
void arena_toggle_w(int owner);
void arena_cast_r(int owner);
float arena_hero_armor(const ArenaHero *h); /* effective armor, incl. R's buff */

#endif
