#!/usr/bin/env bash
# run_bot_pool.sh — launches N persistent apps/arena_bot processes against
# the bot-pool matchmaker and stays in the foreground (via `wait`), so a
# systemd unit supervising this script can actually track liveness instead
# of the launch-and-detach pattern scripts/launch_arena_pools.sh used
# (EMILY/BACKLOG.md S170-65 -- see ops/systemd/redgarden-bot-pool.service).
#
# Usage: ./scripts/run_bot_pool.sh [n_bots]   # default 19 -- lobby-size is 20 and one slot
#                                              # must stay open or a human can never queue in
#                                              # (S170-66: pool used to launch all 20 and the
#                                              # lobby was permanently full of bots).
set -euo pipefail
cd "$(dirname "$0")/.."

N_BOTS="${1:-19}"
mkdir -p var

if [ ! -x ./build/red_garden_arena_bot ]; then
    echo "build first: bash scripts/build.sh" >&2
    exit 1
fi

pids=()
cleanup() {
    for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT TERM INT

for i in $(seq 1 "$N_BOTS"); do
    ./build/red_garden_arena_bot --host 127.0.0.1 --index "$i" > "var/arena_bot_$i.log" 2>&1 &
    pids+=("$!")
done

echo "launched $N_BOTS bots into the bot pool (indices 1-$N_BOTS)"
wait -n  # exit (and let systemd Restart= relaunch the whole set) if any bot dies
