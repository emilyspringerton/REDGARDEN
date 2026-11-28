#!/usr/bin/env bash
# build-linux-servers.sh [OUTDIR] -- the three server-side binaries the game box runs, static (no glibc coupling to the
# build machine): red_garden_matchmaker, red_garden_arena_server, red_garden_arena_bot. Same sources and flags as the
# matching lines of scripts/build.sh plus -static; no SDL2/GL needed. Used by .github/workflows/deploy.yml.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-${ROOT_DIR}/build-linux}"
mkdir -p "$OUT"
P="${ROOT_DIR}/packages"
FLAGS=(-std=c99 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -I"$P" -static)

gcc "${FLAGS[@]}" -o "$OUT/red_garden_matchmaker" "${ROOT_DIR}/apps/matchmaker/src/main.c"

gcc "${FLAGS[@]}" -o "$OUT/red_garden_arena_server" \
  -include "$P/simulation/bloodflower_mod_host.h" \
  -include "$P/simulation/tree_passive_mod_host.h" \
  -include "$P/simulation/build_template_mod_host.h" \
  -include "$P/simulation/item_curriculum_mod_host.h" \
  -include "$P/simulation/duck_smoke_bomb_mod_host.h" \
  -include "$P/simulation/abraham_fireball_mod_host.h" \
  -include "$P/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/apps/arena_server/src/main.c" \
  "$P/simulation/arena_game.c" \
  "$P/simulation/arena_ai_bridge.c" \
  "$P/simulation/bloodflower_mod.c" \
  "$P/simulation/tree_passive_mod.c" \
  "$P/simulation/build_template_mod.c" \
  "$P/simulation/item_curriculum_mod.c" \
  "$P/simulation/duck_smoke_bomb_mod.c" \
  "$P/simulation/abraham_fireball_mod.c" \
  "$P/simulation/bacon_puck_intangible_speed_mod.c" \
  "$P/simulation/camera_unify_mod.c" \
  "$P/common/mlp_infer.c" \
  -lm

gcc "${FLAGS[@]}" -o "$OUT/red_garden_arena_bot" \
  "${ROOT_DIR}/apps/arena_bot/src/main.c" \
  "$P/common/mlp_infer.c" \
  -lm
ls -l "$OUT"
