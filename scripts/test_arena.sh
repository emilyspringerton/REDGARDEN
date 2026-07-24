#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

mkdir -p "${BUILD_DIR}"

# Headless on purpose -- no SDL/GL dependency, so this runs on any box,
# including this one (no display, no Xvfb). Exercises the sim logic
# underneath apps/arena, which is otherwise unverified here until Xvfb is
# available (see ~/sudo-queue/06-install-xvfb-for-arena-testing.sh).
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_arena_game" \
  "${ROOT_DIR}/tests/test_arena_game.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  -lm

gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_mat4" \
  "${ROOT_DIR}/tests/test_mat4.c" \
  -lm

# Observer mode replay parser/player (NORTHSTAR §12 Phase C, S170-30) --
# same headless-testable reasoning as test_arena_game above.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_arena_replay" \
  "${ROOT_DIR}/tests/test_arena_replay.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/arena_replay.c" \
  -lm

"${BUILD_DIR}/test_arena_game"
"${BUILD_DIR}/test_mat4"
"${BUILD_DIR}/test_arena_replay"
