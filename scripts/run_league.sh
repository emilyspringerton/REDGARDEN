#!/usr/bin/env bash
# run_league.sh — NORTHSTAR §25.4.1: launches all three real AlphaStar-style league roles
# (main / main_exploiter / league_exploiter) as separate concurrent rl_train_team.py --league
# processes against ONE shared --league-dir, stays in the foreground (via `wait -n`, matching
# run_bot_pool.sh's own convention for a systemd unit to track liveness), and forwards SIGTERM/INT
# to every child on exit so a stopped supervising process doesn't leave orphaned trainers running.
#
# This is the real orchestration rl_train_team.py's own --league flag alone doesn't provide: that
# flag trains exactly ONE role per process (by design -- see rl_league.py's own module doc comment
# on why this is a real, honest multi-process design, not a single monolithic trainer). Running
# only one of the three roles is a real, valid thing to do on its own (e.g. iterating on Main
# alone against a league seeded by a prior run) -- this script is for actually running the full
# three-role league together, not the only supported way to use --league.
#
# Usage: ./scripts/run_league.sh [league_dir] [total_timesteps] [team_size]
#   league_dir       default: rl_league  -- shared registry all three processes read/write
#   total_timesteps  default: 500000     -- per-process budget (NOT a combined league budget --
#                                           each of the three roles trains this many timesteps
#                                           independently, same as rl_train_team.py's own default)
#   team_size        default: 3          -- matches rl_train_team.py's own default (3v3)
#
# Real, honest scope: this launches real, long-running training processes -- a real GPU/CPU-time
# cost, the same class of decision NORTHSTAR §25.4's own history already treats as a real founder
# call (e.g. "ok if there is an issue fix it, if you must cancel current training and restart
# after thats ok"), not something to launch speculatively. Whoever runs this should expect it to
# run for a real, substantial amount of wall-clock time across all three processes.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT_DIR="$(pwd)"

LEAGUE_DIR="${1:-rl_league}"
TOTAL_TIMESTEPS="${2:-500000}"
TEAM_SIZE="${3:-3}"

mkdir -p var

pids=()
cleanup() {
    for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT TERM INT

echo "launching REDGARDEN AlphaStar-style league (NORTHSTAR §25.4.1)"
echo "  league_dir=${ROOT_DIR}/${LEAGUE_DIR}  total_timesteps/role=${TOTAL_TIMESTEPS}  team_size=${TEAM_SIZE}"

for role in main main_exploiter league_exploiter; do
    python3 "${ROOT_DIR}/scripts/rl_train_team.py" \
        --league --league-role "$role" --league-dir "${ROOT_DIR}/${LEAGUE_DIR}" \
        --team-size "$TEAM_SIZE" --total-timesteps "$TOTAL_TIMESTEPS" \
        --output-dir "${ROOT_DIR}/var/rl_league_checkpoints_${role}" \
        --skip-export \
        > "${ROOT_DIR}/var/rl_league_${role}.log" 2>&1 &
    pids+=("$!")
    echo "  ${role}: pid $! -> var/rl_league_${role}.log"
done

echo "all three roles launched -- tail var/rl_league_*.log to watch progress"
wait -n  # exit (and let systemd Restart= relaunch the whole league, if run under a unit) if any
         # one role's process dies -- matches run_bot_pool.sh's own real reasoning: a half-dead
         # league (e.g. Main crashed but the two exploiters keep running against a stale Main) is
         # a worse state than restarting the whole group.
