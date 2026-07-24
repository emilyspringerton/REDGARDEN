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
// connect to (see apps/matchmaker/src/main.c).
typedef struct {
    uint16_t port;
} MatchFoundMsg;

// ---- apps/arena_server wire structs (2026-07-24 pivot: the MOBA is the
// product) ----

// PACKET_ARENA_MOVE payload: a new move target for the sending client's own
// hero. No owner field -- the server infers "which hero" from which client
// slot sent the packet (same trust model as PACKET_CARD_PLAY / client_id).
typedef struct {
    float target_x;
    float target_z;
} ArenaMoveCmd;

// PACKET_ARENA_CAST payload: which ability slot (0=Q, 1=W, 2=R) to cast.
typedef struct {
    uint8_t slot;
} ArenaCastCmd;

// Per-hero state broadcast in PACKET_ARENA_SNAPSHOT. Deliberately minimal
// for the first networked pass (position/HP/alive/hero_id only, no
// ability-state sync yet) -- enough for a human to see and fight a real
// remote hero; full status-effect sync (silence/intangible/etc.) is a
// later slice once 1v1 human PvP itself is confirmed fun (NORTHSTAR §13).
typedef struct {
    float x, z;
    uint16_t hp;
    uint16_t max_hp;
    uint8_t alive;
    uint8_t hero_id;
} ArenaHeroSnapshot;

// PACKET_ARENA_SNAPSHOT payload: both hero slots, in owner order (0, 1).
typedef struct {
    ArenaHeroSnapshot heroes[2];
    uint8_t winner; /* 0 = none yet, 1 = owner 0 won, 2 = owner 1 won */
} ArenaSnapshotMsg;

#endif
