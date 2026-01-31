#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>
#endif

#include "../../../packages/common/protocol.h"
#include "../../../packages/simulation/local_game.h"

#define STATE_LOBBY 0
#define STATE_GAME_NET 1
#define STATE_GAME_LOCAL 2

static char SERVER_HOST[64] = "127.0.0.1";
static int SERVER_PORT = 6969;

static int app_state = STATE_LOBBY;
static int my_client_id = -1;
static int sock = -1;
static struct sockaddr_in server_addr;

typedef struct {
    int dragging_card;
    int hover_grid_x;
    int hover_grid_z;
    int placing_valid;
} MouseState;

static MouseState mouse_state = { .dragging_card = -1 };

static const float cell_w = 40.0f;
static const float cell_h = 20.0f;
static const float grid_origin_x = 640.0f;
static const float grid_origin_y = 140.0f;

static void draw_char(char c, float x, float y, float s) {
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    if (c >= '0' && c <= '9') {
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
    } else if (c == 'A') {
        glVertex2f(x, y); glVertex2f(x + s / 2, y + s);
        glVertex2f(x + s / 2, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s / 4, y + s / 2); glVertex2f(x + 3 * s / 4, y + s / 2);
    } else if (c == 'D') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x, y);
    } else if (c == 'E') {
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s * 0.8f, y + s / 2);
    } else if (c == 'G') {
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x + s / 2, y + s / 2);
    } else if (c == 'N') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
    } else if (c == 'O') {
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
    } else if (c == 'R') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x, y + s / 2);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y);
    } else if (c == 'S') {
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y + s / 2);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x, y);
    } else if (c == 'T') {
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s / 2, y + s); glVertex2f(x + s / 2, y);
    } else if (c == ' ') {
    } else {
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
    }
    glEnd();
}

static void draw_string(const char* str, float x, float y, float size) {
    while (*str) {
        draw_char(*str, x, y, size);
        x += size * 1.2f;
        str++;
    }
}

static void grid_to_screen(int gx, int gz, float *sx, float *sy) {
    *sx = grid_origin_x + (gx - gz) * (cell_w / 2.0f);
    *sy = grid_origin_y + (gx + gz) * (cell_h / 2.0f);
}

static void screen_to_grid(float sx, float sy, int *gx, int *gz) {
    float dx = (sx - grid_origin_x) / (cell_w / 2.0f);
    float dy = (sy - grid_origin_y) / (cell_h / 2.0f);
    float fx = (dx + dy) * 0.5f;
    float fz = (dy - dx) * 0.5f;
    *gx = (int)floorf(fx + 0.5f);
    *gz = (int)floorf(fz + 0.5f);
}

static void draw_cell(int gx, int gz, float r, float g, float b) {
    float sx, sy;
    grid_to_screen(gx, gz, &sx, &sy);
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);
    glVertex2f(sx, sy - cell_h / 2.0f);
    glVertex2f(sx + cell_w / 2.0f, sy);
    glVertex2f(sx, sy + cell_h / 2.0f);
    glVertex2f(sx - cell_w / 2.0f, sy);
    glEnd();
}

static void draw_grid(void) {
    for (int x = 0; x < GRID_DIM; x++) {
        for (int z = 0; z < GRID_DIM; z++) {
            draw_cell(x, z, 0.0f, 0.6f, 0.9f);
        }
    }
}

static void draw_entity(const Entity *e) {
    float sx, sy;
    grid_to_screen(e->grid_x, e->grid_z, &sx, &sy);
    if (e->type == ENTITY_OUTPOST) glColor3f(1.0f, 0.9f, 0.1f);
    else if (e->type == ENTITY_VILLAGE) glColor3f(0.0f, 1.0f, 1.0f);
    else glColor3f(1.0f, 0.0f, 0.8f);
    float size = (e->type == ENTITY_OUTPOST) ? 10.0f : 6.0f;
    glBegin(GL_QUADS);
    glVertex2f(sx - size, sy - size);
    glVertex2f(sx + size, sy - size);
    glVertex2f(sx + size, sy + size);
    glVertex2f(sx - size, sy + size);
    glEnd();
}

static void draw_cards(void) {
    float start_x = 360.0f;
    float y = 20.0f;
    float w = 100.0f;
    float h = 80.0f;
    for (int i = 0; i < HAND_SIZE; i++) {
        float x = start_x + i * (w + 12.0f);
        glColor3f(0.05f, 0.05f, 0.05f);
        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
        glEnd();

        glColor3f(1.0f, 0.0f, 0.8f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
        glEnd();

        glColor3f(0.0f, 1.0f, 1.0f);
        if (i == 0) draw_string("MILITIA", x + 10, y + 30, 8);
        if (i == 1) draw_string("SCOUT", x + 10, y + 30, 8);
        if (i == 2) draw_string("SWARM", x + 10, y + 30, 8);
        if (i == 3) draw_string("OUTPOST", x + 10, y + 30, 8);
    }
}

static int card_at(int mx, int my) {
    float start_x = 360.0f;
    float y = 20.0f;
    float w = 100.0f;
    float h = 80.0f;
    if (my < y || my > y + h) return -1;
    for (int i = 0; i < HAND_SIZE; i++) {
        float x = start_x + i * (w + 12.0f);
        if (mx >= x && mx <= x + w) return i;
    }
    return -1;
}

static void draw_ghost(void) {
    if (mouse_state.dragging_card < 0) return;
    if (mouse_state.hover_grid_x < 0 || mouse_state.hover_grid_z < 0) return;
    if (mouse_state.hover_grid_x >= GRID_DIM || mouse_state.hover_grid_z >= GRID_DIM) return;
    float sx, sy;
    grid_to_screen(mouse_state.hover_grid_x, mouse_state.hover_grid_z, &sx, &sy);
    if (mouse_state.placing_valid) glColor4f(0.0f, 1.0f, 0.0f, 0.4f);
    else glColor4f(1.0f, 0.0f, 0.0f, 0.4f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glVertex2f(sx - 12, sy - 12);
    glVertex2f(sx + 12, sy - 12);
    glVertex2f(sx + 12, sy + 12);
    glVertex2f(sx - 12, sy + 12);
    glEnd();
    glDisable(GL_BLEND);
}

static void net_init(void) {
    #ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    #endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    #ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
    #else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    #endif
}

static void net_connect(void) {
    struct hostent *he = gethostbyname(SERVER_HOST);
    if (!he) {
        printf("Failed to resolve %s\n", SERVER_HOST);
        return;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);

    NetHeader h = {0};
    h.type = PACKET_CONNECT;
    sendto(sock, (char *)&h, sizeof(NetHeader), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

static void net_send_card(uint8_t card_id, int16_t grid_x, int16_t grid_z) {
    char packet[256];
    NetHeader head = {0};
    head.type = PACKET_CARD_PLAY;
    head.client_id = (uint8_t)my_client_id;
    CardPlayCmd cmd = {0};
    cmd.card_id = card_id;
    cmd.grid_x = grid_x;
    cmd.grid_z = grid_z;
    memcpy(packet, &head, sizeof(NetHeader));
    memcpy(packet + sizeof(NetHeader), &cmd, sizeof(CardPlayCmd));
    sendto(sock, packet, sizeof(NetHeader) + sizeof(CardPlayCmd), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

static void net_process_snapshot(char *buffer, int len) {
    if (len < (int)(sizeof(NetHeader) + sizeof(uint16_t))) return;
    int cursor = sizeof(NetHeader);
    uint16_t count = 0;
    memcpy(&count, buffer + cursor, sizeof(uint16_t));
    cursor += sizeof(uint16_t);
    memset(local_state.entities, 0, sizeof(local_state.entities));
    for (uint16_t i = 0; i < count; i++) {
        if (cursor + (int)sizeof(NetEntity) > len) break;
        NetEntity *ne = (NetEntity *)(buffer + cursor);
        cursor += sizeof(NetEntity);
        if (i >= MAX_ENTITIES) break;
        Entity *e = &local_state.entities[i];
        e->active = 1;
        e->id = ne->id;
        e->type = ne->type;
        e->owner = ne->owner;
        e->x = ne->x;
        e->z = ne->z;
        e->grid_x = (int16_t)ne->x;
        e->grid_z = (int16_t)ne->z;
        e->hp = ne->hp;
    }
}

static void net_tick(void) {
    char buffer[4096];
    struct sockaddr_in sender;
    socklen_t slen = sizeof(sender);
    int len = recvfrom(sock, buffer, 4096, 0, (struct sockaddr *)&sender, &slen);
    while (len > 0) {
        NetHeader *head = (NetHeader *)buffer;
        if (head->type == PACKET_SNAPSHOT) {
            net_process_snapshot(buffer, len);
        } else if (head->type == PACKET_WELCOME) {
            my_client_id = head->client_id;
        }
        len = recvfrom(sock, buffer, 4096, 0, (struct sockaddr *)&sender, &slen);
    }
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            strncpy(SERVER_HOST, argv[++i], 63);
        }
    }

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("RED GARDEN RTS BUILD", 100, 100, 1280, 720, SDL_WINDOW_OPENGL);
    SDL_GL_CreateContext(win);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, 1280, 0, 720);
    glMatrixMode(GL_MODELVIEW);

    net_init();
    local_init_match(2);

    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (app_state == STATE_LOBBY && e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_l) app_state = STATE_GAME_LOCAL;
                if (e.key.keysym.sym == SDLK_n) {
                    app_state = STATE_GAME_NET;
                    net_connect();
                }
            }
            if (app_state != STATE_LOBBY) {
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int card = card_at(e.button.x, 720 - e.button.y);
                    if (card >= 0) {
                        mouse_state.dragging_card = card;
                    }
                }
                if (e.type == SDL_MOUSEMOTION) {
                    int gx, gz;
                    screen_to_grid((float)e.motion.x, (float)(720 - e.motion.y), &gx, &gz);
                    mouse_state.hover_grid_x = gx;
                    mouse_state.hover_grid_z = gz;
                    mouse_state.placing_valid = (gx >= 0 && gx < GRID_DIM && gz >= 0 && gz < GRID_DIM);
                }
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    if (mouse_state.dragging_card >= 0 && mouse_state.placing_valid) {
                        if (app_state == STATE_GAME_LOCAL) {
                            local_apply_card(1, mouse_state.dragging_card, mouse_state.hover_grid_x, mouse_state.hover_grid_z);
                        } else if (app_state == STATE_GAME_NET) {
                            net_send_card((uint8_t)mouse_state.dragging_card, mouse_state.hover_grid_x, mouse_state.hover_grid_z);
                        }
                    }
                    mouse_state.dragging_card = -1;
                }
            }
        }

        if (app_state == STATE_GAME_LOCAL) {
            local_update(16);
        } else if (app_state == STATE_GAME_NET) {
            net_tick();
        }

        glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glLoadIdentity();
        if (app_state == STATE_LOBBY) {
            glColor3f(0.0f, 1.0f, 1.0f);
            draw_string("RED GARDEN", 420, 480, 18);
            draw_string("L: LOCAL", 460, 380, 10);
            draw_string("N: NETWORK", 450, 340, 10);
        } else {
            draw_grid();
            for (int i = 0; i < MAX_ENTITIES; i++) {
                if (local_state.entities[i].active) draw_entity(&local_state.entities[i]);
            }
            draw_ghost();
            draw_cards();
        }
        SDL_GL_SwapWindow(win);
        SDL_Delay(16);
    }

    SDL_Quit();
    return 0;
}
