#include "local_game.h"
#include <string.h>

ServerState local_state;

static void init_grid(void) {
    memset(local_state.grid, 0, sizeof(local_state.grid));
    for (int x = 0; x < GRID_DIM; x++) {
        for (int z = 0; z < GRID_DIM; z++) {
            local_state.grid[x][z].state = CELL_NEUTRAL;
            local_state.grid[x][z].population = 50;
        }
    }
}

static int spawn_entity(uint8_t type, uint8_t owner, int16_t grid_x, int16_t grid_z) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &local_state.entities[i];
        if (!e->active) {
            e->active = 1;
            e->id = local_state.next_entity_id++;
            e->type = type;
            e->owner = owner;
            e->grid_x = grid_x;
            e->grid_z = grid_z;
            e->x = (float)grid_x;
            e->z = (float)grid_z;
            e->cooldown_ms = 0;
            switch (type) {
                case ENTITY_MILITIA: e->hp = 120; break;
                case ENTITY_SCOUT: e->hp = 60; break;
                case ENTITY_SWARMLING: e->hp = 20; break;
                case ENTITY_OUTPOST: e->hp = 200; e->cooldown_ms = 5000; break;
                case ENTITY_VILLAGE: e->hp = 150; break;
                default: e->hp = 50; break;
            }
            return e->id;
        }
    }
    return -1;
}

void local_init_match(int num_players) {
    (void)num_players;
    memset(&local_state, 0, sizeof(local_state));
    init_grid();

    spawn_entity(ENTITY_VILLAGE, 0, 5, 5);
    spawn_entity(ENTITY_VILLAGE, 0, 14, 14);
    spawn_entity(ENTITY_OUTPOST, 1, 3, 3);
    spawn_entity(ENTITY_OUTPOST, 2, 16, 16);
}

int local_apply_card(uint8_t owner, uint8_t card_id, int16_t grid_x, int16_t grid_z) {
    if (grid_x < 0 || grid_x >= GRID_DIM || grid_z < 0 || grid_z >= GRID_DIM) return 0;
    EntityType type = ENTITY_NONE;
    switch (card_id) {
        case CARD_MILITIA: type = ENTITY_MILITIA; break;
        case CARD_SCOUT: type = ENTITY_SCOUT; break;
        case CARD_SWARMLINGS: type = ENTITY_SWARMLING; break;
        case CARD_OUTPOST: type = ENTITY_OUTPOST; break;
        default: return 0;
    }
    return spawn_entity(type, owner, grid_x, grid_z) >= 0;
}

static void update_outpost(Entity *e, uint32_t dt_ms) {
    if (e->cooldown_ms > dt_ms) {
        e->cooldown_ms -= dt_ms;
        return;
    }
    e->cooldown_ms = 5000;
    spawn_entity(ENTITY_MILITIA, e->owner, e->grid_x, e->grid_z);
}

void local_update(uint32_t dt_ms) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &local_state.entities[i];
        if (!e->active) continue;
        if (e->type == ENTITY_OUTPOST) {
            update_outpost(e, dt_ms);
        }
    }
    local_state.server_tick++;
}
