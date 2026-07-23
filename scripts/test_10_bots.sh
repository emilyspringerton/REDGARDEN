#!/usr/bin/env bash
# scripts/test_10_bots.sh — VS0/VS1 validation: matchmaking + connect-ticket
# accounts + 10 independent headless bots forming 5 concurrent matches.
#
# VS0 = a single bot-vs-bot match works correctly (subset of this run).
# VS1 = online play validated with 10 independent headless bots connected
#       simultaneously, via matchmaking, over the real UDP network stack.
#
# Each pair of bots gets matched into its own isolated red_garden_server
# process (see apps/matchmaker/src/main.c doc comment for why: one match
# per process is this simulation's design, not a limitation of this test).
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

export REDGARDEN_TICKET_SECRET="${REDGARDEN_TICKET_SECRET:-test-secret-for-vs0-vs1-validation}"
LOG_DIR="$(mktemp -d)"
NUM_BOTS="${1:-10}"

cleanup() {
    pkill -9 -f red_garden_bot 2>/dev/null
    pkill -9 -f 'red_garden_server --port' 2>/dev/null
    pkill -9 -f red_garden_matchmaker 2>/dev/null
}
trap cleanup EXIT

echo "Building..."
bash scripts/build.sh

echo "Starting matchmaker (logs: ${LOG_DIR}/matchmaker.log)..."
./build/red_garden_matchmaker > "${LOG_DIR}/matchmaker.log" 2>&1 &
sleep 1

echo "Launching ${NUM_BOTS} bots..."
for i in $(seq 1 "${NUM_BOTS}"); do
    ./build/red_garden_bot 127.0.0.1 > "${LOG_DIR}/bot_${i}.log" 2>&1 &
done

echo "Waiting for matchmaking + connections to settle..."
sleep 6

expected_matches=$(( NUM_BOTS / 2 ))
connected=$(grep -c "CLIENT .* CONNECTED" "${LOG_DIR}/matchmaker.log" || true)
spawned=$(grep -c "spawned server on port" "${LOG_DIR}/matchmaker.log" || true)

echo ""
echo "=== Matchmaker log ==="
cat "${LOG_DIR}/matchmaker.log"
echo ""
echo "=== Results ==="
echo "Expected matches: ${expected_matches}   Spawned servers: ${spawned}   Total CONNECTED lines: ${connected}"

if [ "${spawned}" -ne "${expected_matches}" ] || [ "${connected}" -ne "${NUM_BOTS}" ]; then
    echo "FAIL: expected ${expected_matches} spawned servers and ${NUM_BOTS} connects"
    exit 1
fi

echo "Checking sustained stability (10s under load)..."
sleep 10
alive=$(pgrep -f 'red_garden_server --port' | wc -l)
if [ "${alive}" -ne "${expected_matches}" ]; then
    echo "FAIL: expected ${expected_matches} server processes still alive, found ${alive} (a match crashed)"
    exit 1
fi

echo "PASS: ${NUM_BOTS} bots, ${expected_matches} concurrent matches, all stable."
