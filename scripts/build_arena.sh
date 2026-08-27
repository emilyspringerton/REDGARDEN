#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

mkdir -p "${BUILD_DIR}"

# No -lGLU: the arena client is a shader-based (modern GL) renderer, loading
# GL 3.x entry points itself via SDL_GL_GetProcAddress, so it doesn't need
# GLU at all (unlike apps/lobby, which is blocked here on a missing
# libglu1-mesa-dev).
# -D_DEFAULT_SOURCE: needed by packages/common/http_client.h's getaddrinfo/
# struct addrinfo/usleep under -std=c99 (same fix already applied to
# scripts/build.sh for the same reason).
# packages/common/mlp_infer.c (S170-228): arena_game.c's own arena_bot_tick now calls
# rl_policy_forward() (packages/common/rl_policy_weights.h), which calls mlp_forward(),
# defined in mlp_infer.c -- missing here broke CI's Linux build (undefined reference at
# link time; scripts/build.sh and scripts/build_training.sh already had this fix).
# packages/simulation/bloodflower_mod.c (S194-01): arena_game.c's own arena_tick_daynight
# calls on_moon_zenith(), defined in the PARENA-compiled bloodflower mod -- this script is
# a separate, redundant build path from scripts/build.sh's own apps/arena target (which
# already had this fix) and was missed when the mod first landed, breaking CI's "sanity
# check" build (undefined reference at link time) without affecting scripts/build.sh's own
# build or local `bash scripts/test_arena.sh`, which is why this passed locally and only
# broke in CI.
# packages/simulation/tree_passive_mod.c (2026-08-25): same exact gap, same fix, this time for
# arena_hero_tree_passive's on_tree_passive_strike call (Tree hero passive) -- this script was
# missed again when THAT mod landed, for the identical reason: it's redundant with
# scripts/build.sh's own apps/arena target and isn't exercised by `bash scripts/test_arena.sh`,
# so it only broke in CI, not locally. Found live via CI failure on commit c0e3ee6, not caught
# ahead of time -- flagging here so the next PARENA mod doesn't repeat this a third time.
gcc -std=c99 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  -o "${BUILD_DIR}/red_garden_arena" \
  "${ROOT_DIR}/apps/arena/src/main.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/arena_replay.c" \
  "${ROOT_DIR}/packages/simulation/arena_ai_bridge.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  "${ROOT_DIR}/packages/goldenband/gband.c" \
  "${ROOT_DIR}/packages/goldenband/gband_rig.c" \
  "${ROOT_DIR}/packages/goldenband/gskel.c" \
  "${ROOT_DIR}/packages/goldenband/gmesh.c" \
  "${ROOT_DIR}/packages/goldenband/gband_mesh_rig.c" \
  -lSDL2 -lGL -lm
