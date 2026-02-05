#include "local_game.h"
#include <string.h>

ServerState local_state;

static const int card_costs[CARD_COUNT] = {2, 3, 1, 5};
static const uint32_t card_cooldowns_ms[CARD_COUNT] = {1500, 2000, 1200, 4000};
static const int unit_damage[CARD_COUNT] = {12, 8, 4, 0};

static void init_grid(void) {
    memset(local_state.grid, 0, sizeof(local_state.grid));
    for (int x = 0; x < GRID_DIM; x++) {
        for (int z = 0; z < GRID_DIM; z++) {
            local_state.grid[x][z].state = CELL_NEUTRAL;
            local_state.grid[x][z].population = 50;
        }
    }
}

static void apply_entity_influence(void) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &local_state.entities[i];
        if (!e->active) continue;
        if (e->grid_x < 0 || e->grid_x >= GRID_DIM || e->grid_z < 0 || e->grid_z >= GRID_DIM) continue;
        if (e->type == ENTITY_VILLAGE) continue;
        if (local_state.grid[e->grid_x][e->grid_z].state != CELL_CORRUPTED) {
            local_state.grid[e->grid_x][e->grid_z].state = (e->owner == 1) ? CELL_PLAYER : CELL_ENEMY;
        }
    }
}

static void tick_automata(void) {
    GridCell next[GRID_DIM][GRID_DIM];
    memcpy(next, local_state.grid, sizeof(next));

    for (int x = 0; x < GRID_DIM; x++) {
        for (int z = 0; z < GRID_DIM; z++) {
            int counts[4] = {0, 0, 0, 0};
            for (int dx = -1; dx <= 1; dx++) {
                for (int dz = -1; dz <= 1; dz++) {
                    if (dx == 0 && dz == 0) continue;
                    int nx = x + dx;
                    int nz = z + dz;
                    if (nx < 0 || nx >= GRID_DIM || nz < 0 || nz >= GRID_DIM) continue;
                    counts[local_state.grid[nx][nz].state]++;
                }
            }
            if (counts[CELL_CORRUPTED] >= 4) {
                next[x][z].state = CELL_CORRUPTED;
                continue;
            }
            if (counts[CELL_PLAYER] >= 3 && counts[CELL_PLAYER] >= counts[CELL_ENEMY]) {
                next[x][z].state = CELL_PLAYER;
            } else if (counts[CELL_ENEMY] >= 3 && counts[CELL_ENEMY] > counts[CELL_PLAYER]) {
                next[x][z].state = CELL_ENEMY;
            }
        }
    }

    memcpy(local_state.grid, next, sizeof(next));
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

    local_state.match_winner = 0;
    for (int i = 1; i <= 2; i++) {
        local_state.tech_tier[i] = 1;
        local_state.control_hold_ms[i] = 0;
        local_state.influence[i] = 10;
        local_state.max_influence[i] = 20;
        local_state.influence_timer_ms[i] = 0;
        for (int c = 0; c < CARD_COUNT; c++) {
            local_state.card_cooldown_ms[i][c] = 0;
        }
    }
}

int local_apply_card(uint8_t owner, uint8_t card_id, int16_t grid_x, int16_t grid_z) {
    if (grid_x < 0 || grid_x >= GRID_DIM || grid_z < 0 || grid_z >= GRID_DIM) return 0;
    if (local_state.grid[grid_x][grid_z].state == CELL_CORRUPTED) return 0;
    if (card_id >= CARD_COUNT) return 0;
    if (owner > 2) return 0;
    if (local_state.influence[owner] < card_costs[card_id]) return 0;
    if (local_state.card_cooldown_ms[owner][card_id] > 0) return 0;
    EntityType type = ENTITY_NONE;
    switch (card_id) {
        case CARD_MILITIA: type = ENTITY_MILITIA; break;
        case CARD_SCOUT: type = ENTITY_SCOUT; break;
        case CARD_SWARMLINGS: type = ENTITY_SWARMLING; break;
        case CARD_OUTPOST: type = ENTITY_OUTPOST; break;
        default: return 0;
    }
    if (spawn_entity(type, owner, grid_x, grid_z) < 0) return 0;
    local_state.influence[owner] -= card_costs[card_id];
    local_state.card_cooldown_ms[owner][card_id] = card_cooldowns_ms[card_id];
    return 1;
}

static void update_outpost(Entity *e, uint32_t dt_ms) {
    if (e->cooldown_ms > dt_ms) {
        e->cooldown_ms -= dt_ms;
        return;
    }
    if (local_state.tech_tier[e->owner] >= 2) {
        e->cooldown_ms = 2500;
    } else {
        e->cooldown_ms = 5000;
    }
    spawn_entity(ENTITY_MILITIA, e->owner, e->grid_x, e->grid_z);
}

static void update_unit_ai(Entity *e, uint32_t dt_ms) {
    (void)dt_ms;
    int target_x = e->grid_x;
    int target_z = e->grid_z;
    if (e->type == ENTITY_SCOUT) {
        target_x += (e->owner == 1) ? 1 : -1;
    } else if (e->type == ENTITY_MILITIA) {
        target_z += (e->owner == 1) ? 1 : -1;
    } else if (e->type == ENTITY_SWARMLING) {
        target_x += (e->owner == 1) ? 1 : -1;
        target_z += (e->owner == 1) ? 1 : -1;
    } else {
        return;
    }
    if (target_x < 0 || target_x >= GRID_DIM || target_z < 0 || target_z >= GRID_DIM) return;
    if (local_state.grid[target_x][target_z].state == CELL_CORRUPTED) return;
    e->grid_x = target_x;
    e->grid_z = target_z;
    e->x = (float)target_x;
    e->z = (float)target_z;
}

static void resolve_unit_combat(Entity *e) {
    int damage = 0;
    if (e->type == ENTITY_MILITIA) damage = unit_damage[CARD_MILITIA];
    if (e->type == ENTITY_SCOUT) damage = unit_damage[CARD_SCOUT];
    if (e->type == ENTITY_SWARMLING) damage = unit_damage[CARD_SWARMLINGS];
    if (damage == 0) return;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *target = &local_state.entities[i];
        if (!target->active) continue;
        if (target->owner == e->owner) continue;
        if (target->grid_x != e->grid_x || target->grid_z != e->grid_z) continue;
        target->hp -= damage;
        if (target->hp <= 0) {
            target->active = 0;
        }
        break;
    }
}

static void apply_tech_upgrades(void) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &local_state.entities[i];
        if (!e->active) continue;
        if (e->type == ENTITY_MILITIA && local_state.tech_tier[e->owner] >= 1) {
            if (e->hp < 140) e->hp = 140;
        }
    }
}

static void update_win_condition(uint32_t dt_ms) {
    int total_cells = GRID_DIM * GRID_DIM;
    int counts[3] = {0, 0, 0};
    for (int x = 0; x < GRID_DIM; x++) {
        for (int z = 0; z < GRID_DIM; z++) {
            CellState state = local_state.grid[x][z].state;
            if (state == CELL_PLAYER) counts[1]++;
            if (state == CELL_ENEMY) counts[2]++;
        }
    }
    for (int owner = 1; owner <= 2; owner++) {
        if (counts[owner] >= (int)(total_cells * 0.6f)) {
            local_state.control_hold_ms[owner] += dt_ms;
        } else {
            local_state.control_hold_ms[owner] = 0;
        }
    }
    int enemy_outpost = 0;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &local_state.entities[i];
        if (!e->active) continue;
        if (e->type == ENTITY_OUTPOST && e->owner == 2) {
            enemy_outpost = 1;
            break;
        }
    }
    if (!enemy_outpost) {
        local_state.match_winner = 1;
    } else if (local_state.control_hold_ms[1] >= 60000) {
        local_state.match_winner = 1;
    } else if (local_state.control_hold_ms[2] >= 60000) {
        local_state.match_winner = 2;
    }
}

void local_update(uint32_t dt_ms) {
    if (local_state.match_winner != 0) return;
    apply_entity_influence();
    for (int owner = 1; owner <= 2; owner++) {
        if (local_state.influence[owner] < local_state.max_influence[owner]) {
            local_state.influence_timer_ms[owner] += dt_ms;
            if (local_state.influence_timer_ms[owner] >= 1000) {
                local_state.influence_timer_ms[owner] -= 1000;
                local_state.influence[owner]++;
            }
        }
        for (int c = 0; c < CARD_COUNT; c++) {
            if (local_state.card_cooldown_ms[owner][c] > dt_ms) {
                local_state.card_cooldown_ms[owner][c] -= dt_ms;
            } else {
                local_state.card_cooldown_ms[owner][c] = 0;
            }
        }
    }
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &local_state.entities[i];
        if (!e->active) continue;
        if (e->type == ENTITY_OUTPOST) {
            update_outpost(e, dt_ms);
        } else if (e->type == ENTITY_MILITIA || e->type == ENTITY_SCOUT || e->type == ENTITY_SWARMLING) {
            update_unit_ai(e, dt_ms);
            resolve_unit_combat(e);
        }
    }
    local_state.automata_timer_ms += dt_ms;
    if (local_state.automata_timer_ms >= 2000) {
        local_state.automata_timer_ms = 0;
        tick_automata();
    }
    local_state.match_time_ms += dt_ms;
    if (local_state.match_time_ms > 15000 && local_state.tech_tier[1] < 2) {
        local_state.tech_tier[1] = 2;
    }
    apply_tech_upgrades();
    update_win_condition(dt_ms);
    local_state.server_tick++;
}
