#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

mkdir -p "${BUILD_DIR}"

COMMON_FLAGS=(-std=c99 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -I"${ROOT_DIR}/packages")

gcc "${COMMON_FLAGS[@]}" \
  -o "${BUILD_DIR}/red_garden_server" \
  "${ROOT_DIR}/apps/server/src/main.c" \
  "${ROOT_DIR}/packages/simulation/local_game.c" \
  -lm

gcc "${COMMON_FLAGS[@]}" \
  -o "${BUILD_DIR}/red_garden_bot" \
  "${ROOT_DIR}/apps/client/bot_main.c" \
  -lm

gcc "${COMMON_FLAGS[@]}" \
  -o "${BUILD_DIR}/red_garden_lobby" \
  "${ROOT_DIR}/apps/lobby/src/main.c" \
  "${ROOT_DIR}/packages/simulation/local_game.c" \
  -lSDL2 -lGL -lGLU -lm

gcc "${COMMON_FLAGS[@]}" \
  -o "${BUILD_DIR}/red_garden_matchmaker" \
  "${ROOT_DIR}/apps/matchmaker/src/main.c"

# NORTHSTAR §13 (2026-07-24 pivot): apps/arena is the product now -- this is
# its server-authoritative UDP counterpart (1v1 and N-player team-mode PvP).
gcc "${COMMON_FLAGS[@]}" \
  -o "${BUILD_DIR}/red_garden_arena_server" \
  "${ROOT_DIR}/apps/arena_server/src/main.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  -lm

# A real networked MOBA bot (not the sim's internal practice-mode brain) --
# the "22 bots in the pool" requirement.
gcc "${COMMON_FLAGS[@]}" \
  -o "${BUILD_DIR}/red_garden_arena_bot" \
  "${ROOT_DIR}/apps/arena_bot/src/main.c" \
  -lm
