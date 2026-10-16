#!/bin/bash
# WebSocket lifecycle smoke test (ctest-registered; label: REQUIRES_RENDERER)
#
# Spins up a minimal WS "controller" (salvo_server.py) that replays the
# startup salvo's load_model N times and then sends shutdown — exercising
# the model reload/destruction path end-to-end (the class of bug behind the
# CubismLookUpdater double free) plus graceful teardown. The gtest suite
# never instantiates LAppModel/LAppDelegate, so this is the only automated
# coverage for that path.
#
# Args: $1 = renderer binary (injected by CTest), $2 = model name
# Skips (exit 77) without python3 or a display; fails (exit 1) on crash,
# corruption signatures, missing reloads, or a non-graceful exit.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RENDERER_BIN="${1:?usage: websocket_smoke_test.sh <renderer-binary> [model]}"
MODEL="${2:-Hiyori}"
RELOADS=2
PORT=19876

echo "=== WebSocket lifecycle smoke test ==="
echo "renderer: $RENDERER_BIN  model: $MODEL  reloads: $RELOADS"

[ -x "$RENDERER_BIN" ] || { echo "FAIL: renderer binary not found/executable: $RENDERER_BIN"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "SKIP: python3 not available"; exit 77; }
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "SKIP: no display (DISPLAY/WAYLAND_DISPLAY unset) — headless environment"
    exit 77
fi

SRV_LOG="$(mktemp /tmp/opencode/ws-smoke-srv.XXXXXX.log)"
RENDERER_LOG="$(mktemp /tmp/opencode/ws-smoke-renderer.XXXXXX.log)"
SRV_PID=""
trap '[ -n "$SRV_PID" ] && kill "$SRV_PID" 2>/dev/null; rm -f "$SRV_LOG" "$RENDERER_LOG"' EXIT

python3 "$SCRIPT_DIR/salvo_server.py" "$PORT" "$MODEL" "$RELOADS" >"$SRV_LOG" 2>&1 &
SRV_PID=$!
for _ in $(seq 1 50); do
    grep -q listening "$SRV_LOG" 2>/dev/null && break
    sleep 0.1
done
grep -q listening "$SRV_LOG" || { echo "FAIL: salvo server did not start"; cat "$SRV_LOG"; exit 1; }

# cwd must contain Resources/ (CTest WORKING_DIRECTORY = binary output dir).
timeout 45 "$RENDERER_BIN" --port "$PORT" --model "$MODEL" --width 300 --height 400 >"$RENDERER_LOG" 2>&1
RC=$?

echo "--- server log ---"
cat "$SRV_LOG"
echo "--- renderer tail ---"
tail -5 "$RENDERER_LOG"

FAIL=0
if [ "$RC" -ne 0 ]; then
    echo "FAIL: renderer exit code $RC (expected graceful 0; 124 = shutdown ignored/never arrived)"
    FAIL=1
fi
grep -q 'handshake done' "$SRV_LOG" || { echo "FAIL: renderer never completed the WS handshake"; FAIL=1; }
LOADS=$(grep -c 'load model setting' "$RENDERER_LOG" || true)
EXPECTED=$((RELOADS + 1))
if [ "${LOADS:-0}" -lt "$EXPECTED" ]; then
    echo "FAIL: expected >= $EXPECTED model loads (startup + $RELOADS reloads), saw ${LOADS:-0}"
    FAIL=1
fi
if grep -qaE 'double free|corruption|Segmentation fault|SIGABRT|Assertion' "$RENDERER_LOG"; then
    echo "FAIL: memory-corruption signature in renderer output"
    FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
    echo "=== SMOKE_TEST_RESULT: PASS (loads=$LOADS, graceful exit=$RC) ==="
    exit 0
fi
exit 1
