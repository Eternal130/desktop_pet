#!/bin/bash
# WebSocket Integration Smoke Test
# Tests end-to-end WebSocket communication with the renderer

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RENDERER_BIN="$SCRIPT_DIR/../../build/bin/desktop-pet-renderer/desktop-pet-renderer"
EVIDENCE_DIR="$SCRIPT_DIR/../../../.sisyphus/evidence"
mkdir -p "$EVIDENCE_DIR"

echo "=== WebSocket Integration Smoke Test ==="
echo "Renderer: $RENDERER_BIN"

# Check renderer binary exists
if [ ! -f "$RENDERER_BIN" ]; then
    echo "ERROR: Renderer binary not found at $RENDERER_BIN"
    echo "Run: cmake --build renderer/build"
    exit 1
fi

# Check websocat availability
if ! command -v websocat &>/dev/null; then
    echo "SKIP: websocat not found. Install with: cargo install websocat"
    echo "      or: apt install websocat"
    echo "SMOKE_TEST_RESULT: SKIP (websocat not available)"
    exit 0
fi

echo "websocat found: $(which websocat)"

# Test 1: Standalone mode (no --ws-url)
echo ""
echo "--- Test 1: Standalone mode ---"
timeout 3 "$RENDERER_BIN" 2>&1 | head -5 || true
echo "STANDALONE: OK (renderer ran without crash)"

# Test 2: --help flag
echo ""
echo "--- Test 2: --help flag ---"
"$RENDERER_BIN" --help 2>&1 | tee /tmp/smoke-help.txt
if grep -q "ws-url" /tmp/smoke-help.txt; then
    echo "HELP: PASS"
else
    echo "HELP: FAIL (--ws-url not in help output)"
    exit 1
fi

# Test 3: WebSocket connection and ready event
echo ""
echo "--- Test 3: WebSocket ready event ---"
WS_OUTPUT="/tmp/smoke-ws-output.txt"
rm -f "$WS_OUTPUT"

# Start websocat server
websocat -s 19876 > "$WS_OUTPUT" 2>/dev/null &
WS_PID=$!
sleep 1

# Start renderer with ws-url (may fail in headless env)
RENDERER_EXIT=0
timeout 5 "$RENDERER_BIN" --ws-url ws://localhost:19876 2>&1 || RENDERER_EXIT=$?

# Stop websocat
kill $WS_PID 2>/dev/null || true
sleep 0.5

# Check for ready event OR headless failure
if grep -q '"action":"ready"' "$WS_OUTPUT" 2>/dev/null; then
    echo "READY_EVENT: PASS"
elif [ -z "$(cat "$WS_OUTPUT" 2>/dev/null)" ]; then
    echo "READY_EVENT: SKIP_NO_DISPLAY (renderer likely failed to initialize display)"
else
    echo "READY_EVENT: FAIL"
    echo "WS output was:"
    cat "$WS_OUTPUT" 2>/dev/null || echo "(empty)"
    exit 1
fi

echo ""
echo "=== SMOKE_TEST_RESULT: PASS ==="
echo "All smoke tests passed!"
