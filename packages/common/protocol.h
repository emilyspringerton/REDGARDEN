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

#endif
