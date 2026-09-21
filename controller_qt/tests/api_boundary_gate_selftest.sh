#!/usr/bin/env bash
# ============================================================================
# api_boundary_gate_selftest.sh (S7) — proves the API boundary gate BITES.
#
# Four steps against the REAL source tree:
#   1. the gate passes on the current tree (precondition);
#   2. a violation probe file injected into qml/pages/ makes the gate
#      FAIL (exit 1) — the gate really detects direct core writes (QML-U);
#   2b. an imperative session-property assignment probe injected into
#      qml/pages/ makes the gate FAIL (QML-PW family, audit follow-up);
#   3. a src/ui probe calling the session write surface directly makes
#      the gate FAIL (UI-SES family, audit follow-up);
#   4. after cleanup the gate passes again (no probe leakage).
#
# The probe filenames sort last (zz_) and are removed by the EXIT trap;
# ctest runs this test and ApiBoundaryGateTest under a shared
# RESOURCE_LOCK so the probe can never race the plain gate run.
#
# Usage:  api_boundary_gate_selftest.sh [controller_qt-root]
# Exit:   0 = gate verified (pass → bite → pass)   1 = gate is broken
# ============================================================================
set -uo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
GATE="$ROOT/scripts/api_boundary_gate.sh"
PROBE_QML="$ROOT/qml/pages/zz_gate_selftest_probe.qml"
PROBE_PW="$ROOT/qml/pages/zz_gate_selftest_probe_pw.qml"
PROBE_UI="$ROOT/src/ui/zz_gate_selftest_probe.cpp"

cleanup() { rm -f "$PROBE_QML" "$PROBE_PW" "$PROBE_UI"; }
trap cleanup EXIT
cleanup # stale probe from a killed earlier run must not poison step 1

fail() { echo "api_boundary_gate_selftest: $1" >&2; exit 1; }

# 1. current tree is clean (precondition — if this fails, fix the tree
#    or the allowlist first; the gate is doing its job).
bash "$GATE" "$ROOT" >/dev/null || fail "precondition: gate does not pass on the current tree"

# 2. injected violation must turn the gate red (QML-U: method call).
cat > "$PROBE_QML" <<'EOF'
// self-test probe: a QML page calling the session write surface
// directly. The gate MUST flag this.
Rectangle {
    function probe(s) { s.loadModel("Hiyori") }
}
EOF
if bash "$GATE" "$ROOT" > /dev/null 2>&1; then
    fail "gate did NOT flag the injected violation (s.loadModel in qml/pages/)"
fi
rm -f "$PROBE_QML"

# 2b. injected violation must turn the gate red (QML-PW: imperative
# property assignment — WRITE-bearing Q_PROPERTY bypasses the method
# name patterns, audit follow-up).
cat > "$PROBE_PW" <<'EOF'
// self-test probe: a QML page imperatively assigning a WRITE-bearing
// session property. The gate MUST flag this (QML-PW).
Rectangle {
    function probe(instance) { instance.opacity = 0.5 }
}
EOF
if bash "$GATE" "$ROOT" > /dev/null 2>&1; then
    fail "gate did NOT flag the injected violation (instance.opacity = in qml/pages/)"
fi
rm -f "$PROBE_PW"

# 3. injected src/ui probe must turn the gate red (UI-SES: direct
# session write call from a host bridge TU, audit follow-up).
cat > "$PROBE_UI" <<'EOF'
// self-test probe: a src/ui host bridge TU calling the session write
// surface directly. The gate MUST flag this (UI-SES).
void probe(InstanceSession* session) { session->playMotion("tap", 0); }
EOF
if bash "$GATE" "$ROOT" > /dev/null 2>&1; then
    fail "gate did NOT flag the injected violation (session->playMotion in src/ui/)"
fi
rm -f "$PROBE_UI"

# 4. cleanup restores green.
bash "$GATE" "$ROOT" >/dev/null || fail "gate stays red after probe cleanup"

echo "api_boundary_gate_selftest: gate bites (clean → QML-U red → QML-PW red → UI-SES red → clean)"
exit 0
