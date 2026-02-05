#ifndef LOCAL_GAME_H
#define LOCAL_GAME_H

#include "../common/protocol.h"
#include "../common/physics.h"
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <netinet/in.h>
#endif

typedef struct {
    uint8_t state;
    uint8_t population;
} GridCell;

typedef struct {
    int active;
    uint16_t id;
    uint8_t type;
    uint8_t owner;
    int16_t grid_x;
    int16_t grid_z;
    float x;
    float z;
    int hp;
    uint32_t cooldown_ms;
} Entity;

typedef struct {
    Entity entities[MAX_ENTITIES];
    GridCell grid[GRID_DIM][GRID_DIM];
    int client_active[MAX_CLIENTS];
    struct sockaddr_in clients[MAX_CLIENTS];
    uint32_t server_tick;
    uint32_t next_entity_id;
    uint32_t automata_timer_ms;
    uint32_t match_time_ms;
    uint32_t control_hold_ms[3];
    uint8_t tech_tier[3];
    int influence[3];
    int max_influence[3];
    uint32_t influence_timer_ms[3];
    uint32_t card_cooldown_ms[3][CARD_COUNT];
    int match_winner;
} ServerState;

extern ServerState local_state;

void local_init_match(int num_players);
void local_update(uint32_t dt_ms);
int local_apply_card(uint8_t owner, uint8_t card_id, int16_t grid_x, int16_t grid_z);

#endif
