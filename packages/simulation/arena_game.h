#ifndef ARENA_GAME_H
#define ARENA_GAME_H

#define ARENA_HALF_EXTENT 12.0f
#define ARENA_HERO_SPEED 4.0f      /* units/sec */
#define ARENA_ATTACK_RANGE 1.6f
#define ARENA_ATTACK_DAMAGE 8
#define ARENA_ATTACK_COOLDOWN_MS 700
#define ARENA_NODE_COUNT 2

typedef struct {
    float x, z;
    float target_x, target_z;
    int moving;
    int hp;
    int max_hp;
    int attack_cooldown_ms;
    int owner; /* 0 = player, 1 = bot */
    int alive;
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

#endif
