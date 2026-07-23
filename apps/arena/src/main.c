/* RED GARDEN — single-hero click-to-move arena demo.
 *
 * New, additive client: does not touch apps/lobby or the existing card-RTS.
 * Modern-GL (shader) rendering on purpose -- this sidesteps the GL/glu.h
 * dependency that blocks apps/lobby on this box (no libglu1-mesa-dev
 * installed): a shader pipeline only needs GL/gl.h + SDL_GL_GetProcAddress
 * function loading, no GLU, no GLEW/GLAD.
 */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../../packages/common/mat4.h"
#include "../../../packages/simulation/arena_game.h"

/* ---------------- manually-loaded GL 3.x function pointers ---------------- */
static PFNGLCREATESHADERPROC glCreateShader_;
static PFNGLSHADERSOURCEPROC glShaderSource_;
static PFNGLCOMPILESHADERPROC glCompileShader_;
static PFNGLGETSHADERIVPROC glGetShaderiv_;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_;
static PFNGLCREATEPROGRAMPROC glCreateProgram_;
static PFNGLATTACHSHADERPROC glAttachShader_;
static PFNGLLINKPROGRAMPROC glLinkProgram_;
static PFNGLGETPROGRAMIVPROC glGetProgramiv_;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_;
static PFNGLUSEPROGRAMPROC glUseProgram_;
static PFNGLDELETESHADERPROC glDeleteShader_;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays_;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray_;
static PFNGLGENBUFFERSPROC glGenBuffers_;
static PFNGLBINDBUFFERPROC glBindBuffer_;
static PFNGLBUFFERDATAPROC glBufferData_;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_;
static PFNGLUNIFORM4FPROC glUniform4f_;
static PFNGLUNIFORM3FPROC glUniform3f_;
static PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation_;

#define LOAD(name, type) name##_ = (type)SDL_GL_GetProcAddress(#name)

static int load_gl_functions(void) {
    LOAD(glCreateShader, PFNGLCREATESHADERPROC);
    LOAD(glShaderSource, PFNGLSHADERSOURCEPROC);
    LOAD(glCompileShader, PFNGLCOMPILESHADERPROC);
    LOAD(glGetShaderiv, PFNGLGETSHADERIVPROC);
    LOAD(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC);
    LOAD(glCreateProgram, PFNGLCREATEPROGRAMPROC);
    LOAD(glAttachShader, PFNGLATTACHSHADERPROC);
    LOAD(glLinkProgram, PFNGLLINKPROGRAMPROC);
    LOAD(glGetProgramiv, PFNGLGETPROGRAMIVPROC);
    LOAD(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC);
    LOAD(glUseProgram, PFNGLUSEPROGRAMPROC);
    LOAD(glDeleteShader, PFNGLDELETESHADERPROC);
    LOAD(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
    LOAD(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
    LOAD(glGenBuffers, PFNGLGENBUFFERSPROC);
    LOAD(glBindBuffer, PFNGLBINDBUFFERPROC);
    LOAD(glBufferData, PFNGLBUFFERDATAPROC);
    LOAD(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
    LOAD(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
    LOAD(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);
    LOAD(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC);
    LOAD(glUniform4f, PFNGLUNIFORM4FPROC);
    LOAD(glUniform3f, PFNGLUNIFORM3FPROC);
    LOAD(glBindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC);
    return glCreateShader_ && glShaderSource_ && glCompileShader_ && glLinkProgram_ &&
           glUseProgram_ && glGenVertexArrays_ && glBindVertexArray_ && glGenBuffers_ &&
           glBufferData_ && glVertexAttribPointer_ && glUniformMatrix4fv_;
}

/* ---------------- shader source ---------------- */
static const char *VS_SRC =
    "#version 150\n"
    "in vec3 aPos;\n"
    "in vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uModel;\n"
    "out vec3 vNormal;\n"
    "void main() {\n"
    "    vNormal = mat3(uModel) * aNormal;\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "}\n";

static const char *FS_SRC =
    "#version 150\n"
    "in vec3 vNormal;\n"
    "uniform vec4 uColor;\n"
    "uniform vec3 uLightDir;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float diff = max(dot(normalize(vNormal), normalize(uLightDir)), 0.2);\n"
    "    fragColor = vec4(uColor.rgb * diff, uColor.a);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader_(type);
    glShaderSource_(s, 1, &src, NULL);
    glCompileShader_(s);
    GLint ok = 0;
    glGetShaderiv_(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog_(s, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile error: %s\n", log);
    }
    return s;
}

static GLuint link_program(const char *vs_src, const char *fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = glCreateProgram_();
    glAttachShader_(prog, vs);
    glAttachShader_(prog, fs);
    glBindAttribLocation_(prog, 0, "aPos");
    glBindAttribLocation_(prog, 1, "aNormal");
    glLinkProgram_(prog);
    GLint ok = 0;
    glGetProgramiv_(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog_(prog, sizeof(log), NULL, log);
        fprintf(stderr, "program link error: %s\n", log);
    }
    glDeleteShader_(vs);
    glDeleteShader_(fs);
    return prog;
}

/* ---------------- meshes ---------------- */
/* Unit cube, -0.5..0.5, position+normal interleaved, 36 verts. */
static const float CUBE_VERTS[] = {
    /* +X */  0.5f,-0.5f,-0.5f, 1,0,0,   0.5f, 0.5f,-0.5f, 1,0,0,   0.5f, 0.5f, 0.5f, 1,0,0,
              0.5f,-0.5f,-0.5f, 1,0,0,   0.5f, 0.5f, 0.5f, 1,0,0,   0.5f,-0.5f, 0.5f, 1,0,0,
    /* -X */ -0.5f,-0.5f, 0.5f,-1,0,0,  -0.5f, 0.5f, 0.5f,-1,0,0,  -0.5f, 0.5f,-0.5f,-1,0,0,
             -0.5f,-0.5f, 0.5f,-1,0,0,  -0.5f, 0.5f,-0.5f,-1,0,0,  -0.5f,-0.5f,-0.5f,-1,0,0,
    /* +Y */ -0.5f, 0.5f,-0.5f, 0,1,0,  -0.5f, 0.5f, 0.5f, 0,1,0,   0.5f, 0.5f, 0.5f, 0,1,0,
             -0.5f, 0.5f,-0.5f, 0,1,0,   0.5f, 0.5f, 0.5f, 0,1,0,   0.5f, 0.5f,-0.5f, 0,1,0,
    /* -Y */ -0.5f,-0.5f, 0.5f, 0,-1,0, -0.5f,-0.5f,-0.5f, 0,-1,0,  0.5f,-0.5f,-0.5f, 0,-1,0,
             -0.5f,-0.5f, 0.5f, 0,-1,0,  0.5f,-0.5f,-0.5f, 0,-1,0,  0.5f,-0.5f, 0.5f, 0,-1,0,
    /* +Z */ -0.5f,-0.5f, 0.5f, 0,0,1,   0.5f,-0.5f, 0.5f, 0,0,1,   0.5f, 0.5f, 0.5f, 0,0,1,
             -0.5f,-0.5f, 0.5f, 0,0,1,   0.5f, 0.5f, 0.5f, 0,0,1,  -0.5f, 0.5f, 0.5f, 0,0,1,
    /* -Z */  0.5f,-0.5f,-0.5f, 0,0,-1, -0.5f,-0.5f,-0.5f, 0,0,-1, -0.5f, 0.5f,-0.5f, 0,0,-1,
              0.5f,-0.5f,-0.5f, 0,0,-1, -0.5f, 0.5f,-0.5f, 0,0,-1,  0.5f, 0.5f,-0.5f, 0,0,-1,
};
#define CUBE_VERT_COUNT 36

/* Flat 1x1 ground quad in the XZ plane, normal up. */
static const float PLANE_VERTS[] = {
    -0.5f, 0, -0.5f, 0,1,0,   0.5f, 0, -0.5f, 0,1,0,   0.5f, 0, 0.5f, 0,1,0,
    -0.5f, 0, -0.5f, 0,1,0,   0.5f, 0,  0.5f, 0,1,0,  -0.5f, 0, 0.5f, 0,1,0,
};
#define PLANE_VERT_COUNT 6

#define RING_SEGMENTS 24
#define RING_VERT_COUNT (RING_SEGMENTS * 6)
static float RING_VERTS[RING_VERT_COUNT * 6]; /* filled at startup: pos.xyz + normal.xyz per vertex */

static void build_ring_mesh(float inner_r, float outer_r) {
    int vi = 0;
    for (int i = 0; i < RING_SEGMENTS; i++) {
        float a0 = (float)i / RING_SEGMENTS * 2.0f * (float)M_PI;
        float a1 = (float)(i + 1) / RING_SEGMENTS * 2.0f * (float)M_PI;
        float ix0 = cosf(a0) * inner_r, iz0 = sinf(a0) * inner_r;
        float ox0 = cosf(a0) * outer_r, oz0 = sinf(a0) * outer_r;
        float ix1 = cosf(a1) * inner_r, iz1 = sinf(a1) * inner_r;
        float ox1 = cosf(a1) * outer_r, oz1 = sinf(a1) * outer_r;
        float quad[6][3] = {
            {ix0, 0, iz0}, {ox0, 0, oz0}, {ox1, 0, oz1},
            {ix0, 0, iz0}, {ox1, 0, oz1}, {ix1, 0, iz1},
        };
        for (int v = 0; v < 6; v++) {
            RING_VERTS[vi++] = quad[v][0];
            RING_VERTS[vi++] = quad[v][1];
            RING_VERTS[vi++] = quad[v][2];
            RING_VERTS[vi++] = 0; RING_VERTS[vi++] = 1; RING_VERTS[vi++] = 0;
        }
    }
}

typedef struct { GLuint vao, vbo; int count; } Mesh;

static Mesh upload_mesh(const float *verts, int vert_count) {
    Mesh m; m.count = vert_count;
    glGenVertexArrays_(1, &m.vao);
    glBindVertexArray_(m.vao);
    glGenBuffers_(1, &m.vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, m.vbo);
    glBufferData_(GL_ARRAY_BUFFER, sizeof(float) * 6 * vert_count, verts, GL_STATIC_DRAW);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray_(1);
    glBindVertexArray_(0);
    return m;
}

static void draw_mesh(const Mesh *m) {
    glBindVertexArray_(m->vao);
    glDrawArrays(GL_TRIANGLES, 0, m->count);
    glBindVertexArray_(0);
}

/* ---------------- tiny immediate-mode HUD text (ported from apps/lobby) ---------------- */
static void draw_char(char c, float x, float y, float s) {
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    if (c >= '0' && c <= '9') {
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
    } else if (c == 'W') {
        glVertex2f(x, y + s); glVertex2f(x + s * 0.25f, y);
        glVertex2f(x + s * 0.25f, y); glVertex2f(x + s * 0.5f, y + s * 0.6f);
        glVertex2f(x + s * 0.5f, y + s * 0.6f); glVertex2f(x + s * 0.75f, y);
        glVertex2f(x + s * 0.75f, y); glVertex2f(x + s, y + s);
    } else if (c == 'I') {
        glVertex2f(x + s / 2, y); glVertex2f(x + s / 2, y + s);
    } else if (c == 'N') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
    } else if (c == 'L') {
        glVertex2f(x, y + s); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x + s, y);
    } else if (c == 'O') {
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
    } else if (c == 'S') {
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y + s / 2);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x, y);
    } else if (c == 'E') {
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s * 0.8f, y + s / 2);
    } else if (c == 'U') {
        glVertex2f(x, y + s); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
    } else if (c == 'Y') {
        glVertex2f(x, y + s); glVertex2f(x + s / 2, y + s / 2);
        glVertex2f(x + s, y + s); glVertex2f(x + s / 2, y + s / 2);
        glVertex2f(x + s / 2, y + s / 2); glVertex2f(x + s / 2, y);
    } else if (c == 'H') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
    } else if (c == 'P') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x, y + s / 2);
    } else if (c == ' ') {
    } else {
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
    }
    glEnd();
}

static void draw_string(const char *str, float x, float y, float size) {
    while (*str) {
        draw_char(*str, x, y, size);
        x += size * 1.2f;
        str++;
    }
}

/* ---------------- placement rings ---------------- */
#define MAX_RINGS 6
#define RING_LIFETIME_MS 500.0f
typedef struct { float x, z, age_ms; int active; } Ring;
static Ring rings[MAX_RINGS];

static void spawn_ring(float x, float z) {
    for (int i = 0; i < MAX_RINGS; i++) {
        if (!rings[i].active) {
            rings[i].active = 1;
            rings[i].x = x;
            rings[i].z = z;
            rings[i].age_ms = 0;
            return;
        }
    }
}

/* ---------------- camera ---------------- */
static float cam_yaw = 45.0f, cam_pitch = 40.0f, cam_dist = 16.0f;

static void camera_basis(float focus_x, float focus_z,
                          float *eye_x, float *eye_y, float *eye_z,
                          float *fwd_x, float *fwd_y, float *fwd_z,
                          float *right_x, float *right_y, float *right_z,
                          float *up_x, float *up_y, float *up_z) {
    float yaw = cam_yaw * (float)M_PI / 180.0f;
    float pitch = cam_pitch * (float)M_PI / 180.0f;
    *eye_x = focus_x + cam_dist * cosf(pitch) * sinf(yaw);
    *eye_y = cam_dist * sinf(pitch);
    *eye_z = focus_z + cam_dist * cosf(pitch) * cosf(yaw);
    float fx = focus_x - *eye_x, fy = -*eye_y, fz = focus_z - *eye_z;
    float flen = sqrtf(fx * fx + fy * fy + fz * fz);
    *fwd_x = fx / flen; *fwd_y = fy / flen; *fwd_z = fz / flen;
    float upx = 0, upy = 1, upz = 0;
    float rx = *fwd_y * upz - *fwd_z * upy;
    float ry = *fwd_z * upx - *fwd_x * upz;
    float rz = *fwd_x * upy - *fwd_y * upx;
    float rlen = sqrtf(rx * rx + ry * ry + rz * rz);
    *right_x = rx / rlen; *right_y = ry / rlen; *right_z = rz / rlen;
    *up_x = *right_y * *fwd_z - *right_z * *fwd_y;
    *up_y = *right_z * *fwd_x - *right_x * *fwd_z;
    *up_z = *right_x * *fwd_y - *right_y * *fwd_x;
}

/* Intersects the mouse ray with the y=0 ground plane. Returns 1 on hit. */
static int screen_to_ground(int mx, int my, int w, int h, float fov_deg,
                             float focus_x, float focus_z, float *out_x, float *out_z) {
    float eye_x, eye_y, eye_z, fx, fy, fz, rx, ry, rz, ux, uy, uz;
    camera_basis(focus_x, focus_z, &eye_x, &eye_y, &eye_z, &fx, &fy, &fz, &rx, &ry, &rz, &ux, &uy, &uz);
    float ndc_x = (2.0f * mx / w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * my / h);
    float aspect = (float)w / (float)h;
    float tan_fov = tanf(fov_deg * 0.5f * (float)M_PI / 180.0f);
    float dx = fx + ndc_x * tan_fov * aspect * rx + ndc_y * tan_fov * ux;
    float dy = fy + ndc_x * tan_fov * aspect * ry + ndc_y * tan_fov * uy;
    float dz = fz + ndc_x * tan_fov * aspect * rz + ndc_y * tan_fov * uz;
    if (fabsf(dy) < 1e-5f) return 0;
    float t = -eye_y / dy;
    if (t <= 0) return 0;
    *out_x = eye_x + t * dx;
    *out_z = eye_z + t * dz;
    return 1;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int win_w = 1280, win_h = 720;
    SDL_Window *win = SDL_CreateWindow("RED GARDEN — ARENA DEMO", 100, 100, win_w, win_h,
                                        SDL_WINDOW_OPENGL);
    if (!win) { fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError()); return 1; }

    if (!load_gl_functions()) {
        fprintf(stderr, "Failed to load required GL 3.x functions via SDL_GL_GetProcAddress\n");
        return 1;
    }

    GLuint prog = link_program(VS_SRC, FS_SRC);
    GLint loc_mvp = glGetUniformLocation_(prog, "uMVP");
    GLint loc_model = glGetUniformLocation_(prog, "uModel");
    GLint loc_color = glGetUniformLocation_(prog, "uColor");
    GLint loc_light = glGetUniformLocation_(prog, "uLightDir");

    build_ring_mesh(0.8f, 1.0f);
    Mesh cube_mesh = upload_mesh(CUBE_VERTS, CUBE_VERT_COUNT);
    Mesh plane_mesh = upload_mesh(PLANE_VERTS, PLANE_VERT_COUNT);
    Mesh ring_mesh = upload_mesh(RING_VERTS, RING_VERT_COUNT);

    glEnable(GL_DEPTH_TEST);

    arena_init();

    int dragging_cam = 0;
    int last_mx = 0, last_my = 0;
    int running = 1;
    uint32_t last_tick = SDL_GetTicks();

    while (running) {
        uint32_t now = SDL_GetTicks();
        uint32_t dt = now - last_tick;
        last_tick = now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
                win_w = e.window.data1; win_h = e.window.data2;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                dragging_cam = 1; last_mx = e.button.x; last_my = e.button.y;
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) {
                dragging_cam = 0;
            }
            if (e.type == SDL_MOUSEMOTION && dragging_cam) {
                int dx = e.motion.x - last_mx, dy = e.motion.y - last_my;
                last_mx = e.motion.x; last_my = e.motion.y;
                cam_yaw += dx * 0.3f;
                cam_pitch += dy * 0.3f;
                if (cam_pitch < 10.0f) cam_pitch = 10.0f;
                if (cam_pitch > 80.0f) cam_pitch = 80.0f;
            }
            if (e.type == SDL_MOUSEWHEEL) {
                cam_dist -= e.wheel.y * 1.0f;
                if (cam_dist < 4.0f) cam_dist = 4.0f;
                if (cam_dist > 30.0f) cam_dist = 30.0f;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                arena_state.winner == 0) {
                float gx, gz;
                float focus_x = arena_state.heroes[0].x, focus_z = arena_state.heroes[0].z;
                if (screen_to_ground(e.button.x, e.button.y, win_w, win_h, 60.0f,
                                     focus_x, focus_z, &gx, &gz)) {
                    arena_set_move_target(0, gx, gz);
                    spawn_ring(gx, gz);
                }
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_r) {
                arena_init();
                memset(rings, 0, sizeof(rings));
            }
        }

        if (arena_state.winner == 0) {
            arena_update(dt);
        }
        for (int i = 0; i < MAX_RINGS; i++) {
            if (!rings[i].active) continue;
            rings[i].age_ms += dt;
            if (rings[i].age_ms >= RING_LIFETIME_MS) rings[i].active = 0;
        }

        glViewport(0, 0, win_w, win_h);
        glClearColor(0.03f, 0.05f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float focus_x = arena_state.heroes[0].x, focus_z = arena_state.heroes[0].z;
        Mat4 view = mat4_orbit_view(focus_x, 0, focus_z, cam_yaw, cam_pitch, cam_dist);
        Mat4 proj = mat4_perspective(60.0f, (float)win_w / (float)win_h, 0.1f, 100.0f);
        Mat4 vp = mat4_multiply(&proj, &view);

        glUseProgram_(prog);
        glUniform3f_(loc_light, 0.4f, 0.8f, 0.3f);

        /* ground */
        {
            Mat4 model = mat4_scale(ARENA_HALF_EXTENT * 2.2f, 1.0f, ARENA_HALF_EXTENT * 2.2f);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 0.08f, 0.18f, 0.10f, 1.0f);
            draw_mesh(&plane_mesh);
        }

        /* nodes */
        for (int i = 0; i < ARENA_NODE_COUNT; i++) {
            Mat4 t = mat4_translate(arena_state.nodes[i].x, 0.15f, arena_state.nodes[i].z);
            Mat4 s = mat4_scale(1.2f, 0.3f, 1.2f);
            Mat4 model = mat4_multiply(&t, &s);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 0.85f, 0.7f, 0.1f, 1.0f);
            draw_mesh(&cube_mesh);
        }

        /* heroes */
        for (int i = 0; i < 2; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->alive) continue;
            Mat4 t = mat4_translate(h->x, 0.5f, h->z);
            Mat4 s = mat4_scale(0.9f, 1.0f, 0.9f);
            Mat4 model = mat4_multiply(&t, &s);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            if (i == 0) glUniform4f_(loc_color, 0.1f, 0.8f, 0.95f, 1.0f);
            else glUniform4f_(loc_color, 0.95f, 0.25f, 0.15f, 1.0f);
            draw_mesh(&cube_mesh);
        }

        /* placement rings */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (int i = 0; i < MAX_RINGS; i++) {
            if (!rings[i].active) continue;
            float t01 = rings[i].age_ms / RING_LIFETIME_MS;
            float scale = 0.3f + t01 * 1.5f;
            float alpha = 1.0f - t01;
            Mat4 tr = mat4_translate(rings[i].x, 0.03f, rings[i].z);
            Mat4 sc = mat4_scale(scale, 1.0f, scale);
            Mat4 model = mat4_multiply(&tr, &sc);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 0.2f, 1.0f, 0.5f, alpha);
            draw_mesh(&ring_mesh);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        /* ---- 2D HUD pass (legacy immediate mode, compatibility profile) ---- */
        glUseProgram_(0);
        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, win_w, 0, win_h, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glColor3f(0.1f, 0.8f, 0.95f);
        draw_string("YOU", 20, win_h - 40.0f, 14);
        glColor3f(1.0f, 1.0f, 1.0f);
        {
            ArenaHero *h = &arena_state.heroes[0];
            float frac = (float)h->hp / h->max_hp;
            glColor3f(0.2f, 0.2f, 0.2f);
            glBegin(GL_QUADS);
            glVertex2f(90, win_h - 38.0f); glVertex2f(290, win_h - 38.0f);
            glVertex2f(290, win_h - 20.0f); glVertex2f(90, win_h - 20.0f);
            glEnd();
            glColor3f(0.1f, 0.9f, 0.3f);
            glBegin(GL_QUADS);
            glVertex2f(90, win_h - 38.0f); glVertex2f(90 + 200 * frac, win_h - 38.0f);
            glVertex2f(90 + 200 * frac, win_h - 20.0f); glVertex2f(90, win_h - 20.0f);
            glEnd();
        }
        glColor3f(0.95f, 0.25f, 0.15f);
        draw_string("BOT", 20, win_h - 70.0f, 14);
        {
            ArenaHero *h = &arena_state.heroes[1];
            float frac = (float)h->hp / h->max_hp;
            glColor3f(0.2f, 0.2f, 0.2f);
            glBegin(GL_QUADS);
            glVertex2f(90, win_h - 68.0f); glVertex2f(290, win_h - 68.0f);
            glVertex2f(290, win_h - 50.0f); glVertex2f(90, win_h - 50.0f);
            glEnd();
            glColor3f(0.9f, 0.3f, 0.1f);
            glBegin(GL_QUADS);
            glVertex2f(90, win_h - 68.0f); glVertex2f(90 + 200 * frac, win_h - 68.0f);
            glVertex2f(90 + 200 * frac, win_h - 50.0f); glVertex2f(90, win_h - 50.0f);
            glEnd();
        }

        if (arena_state.winner == 1) {
            glColor3f(0.2f, 1.0f, 0.4f);
            draw_string("YOU WIN", win_w / 2.0f - 150, win_h / 2.0f, 24);
        } else if (arena_state.winner == 2) {
            glColor3f(1.0f, 0.2f, 0.2f);
            draw_string("YOU LOSE", win_w / 2.0f - 160, win_h / 2.0f, 24);
        }
        glEnable(GL_DEPTH_TEST);

        SDL_GL_SwapWindow(win);
        SDL_Delay(16);
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
