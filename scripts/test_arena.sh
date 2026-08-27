#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

mkdir -p "${BUILD_DIR}"

# Headless on purpose -- no SDL/GL dependency, so this runs on any box,
# including this one (no display, no Xvfb). Exercises the sim logic
# underneath apps/arena, which is otherwise unverified here until Xvfb is
# available (see ~/sudo-queue/06-install-xvfb-for-arena-testing.sh).
# packages/common/mlp_infer.c (S170-228): arena_game.c's own arena_bot_tick now calls
# rl_policy_forward()/mlp_forward() -- every binary linking arena_game.c needs this object,
# same reasoning scripts/build.sh's own comment for this already gives.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_arena_game" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_arena_game.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_mat4" \
  "${ROOT_DIR}/tests/test_mat4.c" \
  -lm

# Observer mode replay parser/player (NORTHSTAR §12 Phase C, S170-30) --
# same headless-testable reasoning as test_arena_game above.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_arena_replay" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_arena_replay.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/arena_replay.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# Game AI bridge: state serializer + action decoder (NORTHSTAR §12 Phase E,
# Milestone-6 equivalent, S170-36) -- same headless-testable reasoning.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_arena_ai_bridge" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_arena_ai_bridge.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/arena_ai_bridge.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# gpt2_infer: ported GPT-2 C inference engine (S170-220) -- same headless-testable reasoning,
# no trained checkpoint needed, just the ported math running against synthetic weights.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_gpt2_infer" \
  "${ROOT_DIR}/tests/test_gpt2_infer.c" \
  "${ROOT_DIR}/packages/common/gpt2_infer.c" \
  -lm

# arena_training: the ctypes-callable RL environment API (S170-224, NORTHSTAR §21) -- exercised
# directly from C here (same functions ctypes calls from Python), no Python/ctypes needed to
# catch a regression in this repo's own test suite.
gcc -std=c99 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_arena_training" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_arena_training.c" \
  "${ROOT_DIR}/apps/arena_training/src/headless.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# mlp_infer: the small embedded-C MLP inference engine (S170-227) for the RL policy network --
# hand-verifiable matmul/activation math, see that test file's own doc comment.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_mlp_infer" \
  "${ROOT_DIR}/tests/test_mlp_infer.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# Bloodflower / day-night cycle (2026-08-25): real live round-trip through the compiled PARENA
# mod (stdlib/redgarden/bloodflower_mod.prn), same headless-testable reasoning as the rest of
# this file.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_bloodflower" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_bloodflower.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# Damage log (S189-01, 2026-08-25): real combat-log ring buffer, driven through the real
# apply_damage_ex/resolve_combat path via arena_update(), same headless-testable reasoning.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_damage_log" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_damage_log.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# Tree passive (2026-08-25): real live round-trip through the compiled PARENA mod
# (stdlib/redgarden/tree_passive_mod.prn), same headless-testable reasoning as Bloodflower above.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_tree_passive" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_tree_passive.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# Build templates (2026-08-25): real live round-trip through the compiled PARENA mod
# (stdlib/redgarden/build_template_mod.prn), same headless-testable reasoning as Bloodflower/
# Tree passive above.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_build_templates" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_build_templates.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# Item curriculum (2026-08-25): real live round-trip through the compiled PARENA mod
# (stdlib/redgarden/item_curriculum_mod.prn), same headless-testable reasoning as Bloodflower/
# Tree passive/Build templates above. NORTHSTAR.md §26.3.2's generation primitive.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_item_curriculum" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_item_curriculum.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# Duck Smoke Bomb (S202-10, 2026-08-25): real live round-trip through the compiled PARENA mod
# (stdlib/redgarden/duck_smoke_bomb_mod.prn), same headless-testable reasoning as Bloodflower/
# Tree passive/Build templates/Item curriculum above.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_duck_smoke_bomb" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_duck_smoke_bomb.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# Abraham's Fireball (S202-34, 2026-08-26): real live round-trip through the compiled PARENA mod
# (stdlib/redgarden/abraham_fireball_mod.prn), same headless-testable reasoning as Duck Smoke
# Bomb above -- including the new real piercing-projectile behavior (arena_tick_projectiles).
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_abraham_fireball" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_abraham_fireball.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# Shadow Step (S202-40, 2026-08-26): real live round-trip through arena_toggle_w's own
# ARENA_HERO_BACON_PUCK case -- plain host C, no PARENA mod for this one (same shape as Blink
# Dagger's own arena_use_blink), but still linked against the full arena_game bag for the same
# reasoning every other test here already is.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_shadow_step" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_shadow_step.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

# Cart delivery + the marble-bag/Fibonacci-pity utility (S202-09/S202-42, 2026-08-27): real
# live round-trip through arena_toggle_w/arena_cast_r's own ARENA_HERO_CART case -- plain host
# C, no PARENA mod for this one, but still linked against the full arena_game bag for the same
# reasoning every other test here already is.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/test_cart_delivery" \
  -include "${ROOT_DIR}/packages/simulation/bloodflower_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/tree_passive_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/build_template_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/item_curriculum_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/abraham_fireball_mod_host.h" \
  -include "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod_host.h" \
  "${ROOT_DIR}/tests/test_cart_delivery.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/bloodflower_mod.c" \
  "${ROOT_DIR}/packages/simulation/tree_passive_mod.c" \
  "${ROOT_DIR}/packages/simulation/build_template_mod.c" \
  "${ROOT_DIR}/packages/simulation/item_curriculum_mod.c" \
  "${ROOT_DIR}/packages/simulation/duck_smoke_bomb_mod.c" \
  "${ROOT_DIR}/packages/simulation/abraham_fireball_mod.c" \
  "${ROOT_DIR}/packages/simulation/bacon_puck_intangible_speed_mod.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

"${BUILD_DIR}/test_arena_game"
"${BUILD_DIR}/test_bloodflower"
"${BUILD_DIR}/test_tree_passive"
"${BUILD_DIR}/test_build_templates"
"${BUILD_DIR}/test_item_curriculum"
"${BUILD_DIR}/test_duck_smoke_bomb"
"${BUILD_DIR}/test_abraham_fireball"
"${BUILD_DIR}/test_shadow_step"
"${BUILD_DIR}/test_cart_delivery"
"${BUILD_DIR}/test_damage_log"
"${BUILD_DIR}/test_mat4"
"${BUILD_DIR}/test_arena_replay"
"${BUILD_DIR}/test_arena_ai_bridge"
"${BUILD_DIR}/test_gpt2_infer"
"${BUILD_DIR}/test_arena_training"
"${BUILD_DIR}/test_mlp_infer"
