#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

mkdir -p "${BUILD_DIR}"

COMMON_FLAGS=(-std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages")

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
