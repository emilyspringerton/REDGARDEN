#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

mkdir -p "${BUILD_DIR}"

# No -lGLU: the arena client is a shader-based (modern GL) renderer, loading
# GL 3.x entry points itself via SDL_GL_GetProcAddress, so it doesn't need
# GLU at all (unlike apps/lobby, which is blocked here on a missing
# libglu1-mesa-dev).
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/red_garden_arena" \
  "${ROOT_DIR}/apps/arena/src/main.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  -lSDL2 -lGL -lm
