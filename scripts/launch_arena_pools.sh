#!/usr/bin/env bash
# launch_arena_pools.sh — stands up REDGARDEN's two separate MOBA matchmaking
# pools (EMILY/BACKLOG.md S170-14):
#
#   1. BOT POOL   (port 7778, 10v10) — persistent apps/arena_bot processes
#      matchmake here continuously; a human can --queue into it too
#      (S170-44) to validate against real bots.
#   2. PLAYER-ONLY POOL (port 7779, 1v1) — a second, completely separate
#      matchmaker instance. No bot is ever pointed at this port -- pool
#      separation here is operational (two processes, two ports), not a
#      new access-control mechanism inside the matchmaker itself, matching
#      this codebase's existing pattern of generalizing one binary via
#      flags rather than building new machinery per mode.
#
#      Lobby size is 1v1 (--lobby-size 2), not 10v10: with near-zero real
#      concurrent human players today, a 10v10 player-only queue would
#      never fill. 1v1 is the smallest, already-verified real-PvP case
#      (S170-42) -- the responsible default until there's real player
#      volume to justify a bigger lobby (same reasoning as not running a
#      synthetic load test against production with no real traffic yet,
#      S24-05).
#
#      Ranked matchmaking (the third pool S170-14 names) is explicitly NOT
#      started here -- no rank model, MMR, or queue rules are designed yet.
#
# Usage:
#   ./scripts/launch_arena_pools.sh start [n_bots]   # default n_bots=2
#   ./scripts/launch_arena_pools.sh stop
set -euo pipefail
cd "$(dirname "$0")/.."

BOT_POOL_PORT=7778
BOT_POOL_FIRST_GAME_PORT=7300
BOT_POOL_LOBBY_SIZE=20

PLAYER_POOL_PORT=7779
PLAYER_POOL_FIRST_GAME_PORT=7600
PLAYER_POOL_LOBBY_SIZE=2

PIDFILE=var/arena_pools.pids
mkdir -p var var/matches

start() {
    n_bots="${1:-2}"

    if [ ! -x ./build/red_garden_matchmaker ] || [ ! -x ./build/red_garden_arena_bot ]; then
        echo "build first: bash scripts/build.sh" >&2
        exit 1
    fi
    if [ -z "${REDGARDEN_TICKET_SECRET:-}" ]; then
        echo "REDGARDEN_TICKET_SECRET must be set (both matchmakers' spawned servers fail closed without it)" >&2
        exit 1
    fi

    : > "$PIDFILE"

    echo "starting bot pool matchmaker on :$BOT_POOL_PORT (lobby_size=$BOT_POOL_LOBBY_SIZE)..."
    ./build/red_garden_matchmaker --listen-port "$BOT_POOL_PORT" --lobby-size "$BOT_POOL_LOBBY_SIZE" \
        --server-bin ./build/red_garden_arena_server --first-game-port "$BOT_POOL_FIRST_GAME_PORT" \
        > var/bot_pool_matchmaker.log 2>&1 &
    echo $! >> "$PIDFILE"

    echo "starting player-only pool matchmaker on :$PLAYER_POOL_PORT (lobby_size=$PLAYER_POOL_LOBBY_SIZE, no bots ever queue here)..."
    ./build/red_garden_matchmaker --listen-port "$PLAYER_POOL_PORT" --lobby-size "$PLAYER_POOL_LOBBY_SIZE" \
        --server-bin ./build/red_garden_arena_server --first-game-port "$PLAYER_POOL_FIRST_GAME_PORT" \
        > var/player_pool_matchmaker.log 2>&1 &
    echo $! >> "$PIDFILE"

    sleep 1
    echo "starting $n_bots persistent bot(s) into the BOT POOL only..."
    for i in $(seq 1 "$n_bots"); do
        ./build/red_garden_arena_bot --host 127.0.0.1 > "var/arena_bot_$i.log" 2>&1 &
        echo $! >> "$PIDFILE"
    done

    echo "done. bot pool: 127.0.0.1:$BOT_POOL_PORT | player-only pool: 127.0.0.1:$PLAYER_POOL_PORT"
    echo "join the player-only pool: ./build/red_garden_arena --queue 127.0.0.1 --matchmaker-port $PLAYER_POOL_PORT"
    echo "join the bot pool:         ./build/red_garden_arena --queue 127.0.0.1 --matchmaker-port $BOT_POOL_PORT"
}

stop() {
    if [ ! -f "$PIDFILE" ]; then
        echo "no $PIDFILE -- nothing tracked to stop"
        exit 0
    fi
    while read -r pid; do
        kill -9 "$pid" 2>/dev/null || true
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    echo "stopped."
}

case "${1:-}" in
    start) start "${2:-2}" ;;
    stop) stop ;;
    *) echo "usage: $0 start [n_bots] | stop" >&2; exit 1 ;;
esac
