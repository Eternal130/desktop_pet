#!/usr/bin/env bash
# ============================================================================
# api_boundary_gate_selftest.sh (S7) — proves the API boundary gate BITES.
#
# Three steps against the REAL source tree:
#   1. the gate passes on the current tree (precondition);
#   2. a violation probe file injected into qml/pages/ makes the gate
#      FAIL (exit 1) — the gate really detects direct core writes;
#   3. after cleanup the gate passes again (no probe leakage).
#
# The probe filename sorts last (zz_) and is removed by the EXIT trap;
# ctest runs this test and ApiBoundaryGateTest under a shared
# RESOURCE_LOCK so the probe can never race the plain gate run.
#
# Usage:  api_boundary_gate_selftest.sh [controller_qt-root]
# Exit:   0 = gate verified (pass → bite → pass)   1 = gate is broken
# ============================================================================
set -uo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
GATE="$ROOT/scripts/api_boundary_gate.sh"
PROBE="$ROOT/qml/pages/zz_gate_selftest_probe.qml"

cleanup() { rm -f "$PROBE"; }
trap cleanup EXIT
cleanup # stale probe from a killed earlier run must not poison step 1

fail() { echo "api_boundary_gate_selftest: $1" >&2; exit 1; }

# 1. current tree is clean (precondition — if this fails, fix the tree
#    or the allowlist first; the gate is doing its job).
bash "$GATE" "$ROOT" >/dev/null || fail "precondition: gate does not pass on the current tree"

# 2. injected violation must turn the gate red.
cat > "$PROBE" <<'EOF'
// self-test probe: a QML page calling the session write surface
// directly. The gate MUST flag this.
Rectangle {
    function probe(s) { s.loadModel("Hiyori") }
}
EOF
if bash "$GATE" "$ROOT" > /dev/null 2>&1; then
    fail "gate did NOT flag the injected violation (s.loadModel in qml/pages/)"
fi

# 3. cleanup restores green.
rm -f "$PROBE"
bash "$GATE" "$ROOT" >/dev/null || fail "gate stays red after probe cleanup"

echo "api_boundary_gate_selftest: gate bites (clean → violation red → clean)"
exit 0
