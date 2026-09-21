#!/usr/bin/env bash
# ============================================================================
# api_boundary_gate.sh (S7) — API 边界门禁 (R3 落地)
#
# Drift alarm for the host-write architecture boundary:
#   - WRITES to core (InstanceManager / InstanceSession write methods)
#     must go through the plugin-API facade bridges — in QML that is
#     `instanceControl` (shared ITuningApi/IInstanceControlApi) and
#     `rosterModel` (shared IInstanceControlApi create/delete); in
#     src/ui/*.cpp the injected pet:: API pointers.
#   - READS stay on the live objects (session properties, monitorModel(),
#     motionGroupNames, ...) — the gate does not flag reads at all.
#
# Scanned scopes:
#   <root>/qml/**/*.qml   — QML pages/components
#   <root>/src/ui/*.cpp   — host bridge TU's (QML-facing controllers)
#
# Violation pattern families (see below):
#   QML-U   : receiver-agnostic core WRITE method names (unique enough
#             that no Qt/QML builtin shares them: loadModel, playMotion,
#             setOpacity, createInstance, ...). Lines whose call sits on
#             an approved bridge receiver (instanceControl. / rosterModel.)
#             are excluded — the bridges ARE the API path.
#   QML-SSR : start/stop/restart are NOT unique (Timer.restart(),
#             Animation.start() ...) so they are only flagged on
#             session-plausible receivers (instance/_inst/inst/s) or a
#             same-line instanceAt(...) chain. Cross-line chained
#             lifecycle calls are a known residual blind spot — the
#             unique-name set (QML-U) has no such gap.
#   UI-MGR  : src/ui calling InstanceManager::createInstance/deleteInstance
#             directly (the roster API bridge must be used instead).
#   UI-SES  : src/ui calling InstanceSession write methods directly (only
#             the injected pet:: API pointers are legal).
#
# Allowlist: every entry is a documented, line-precise exemption
# (file-path ERE :: line-content ERE) — see ALLOWLIST below for the
# per-entry justification. Anything NOT in the table is a violation.
#
# Usage:  api_boundary_gate.sh [controller_qt-root]   (default: script's ../)
# Exit:   0 = clean   1 = violations found (listed on stdout)   2 = usage
# ============================================================================
set -uo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"

if [ ! -d "$ROOT/qml" ] || [ ! -d "$ROOT/src/ui" ]; then
    echo "api_boundary_gate: '$ROOT' does not look like the controller_qt root (qml/ + src/ui required)" >&2
    exit 2
fi

# ── Violation patterns (ERE, matched line-wise) ─────────────────────────────
# Core write-method vocabulary = InstanceManager roster writes +
# InstanceSession lifecycle/tuning writes + the not-yet-in-API layout
# ops (setAutoStart/resetLayout/getLayout are pattern-covered but
# individually allowlisted below until an API family absorbs them).
# Extracted from the InstanceControlBridge / InstanceDetailPage
# rebinding set (S5-S7).
QML_UNIQUE='[.](createInstance|deleteInstance|loadModel|playMotion|setExpression|triggerHitArea|mountVoicePack|unmountVoicePack|setOpacity|setVolume|setMuted|setFps|setAutoStart|resetLayout|getLayout)[[:space:]]*\('
QML_SSR='(^|[^[:alnum:]_])(instance|_inst|inst|s)[.](start|stop|restart)[[:space:]]*\('
QML_CHAIN='instanceAt\([^)]*\)[^;]*[.](start|stop|restart|loadModel|playMotion|setExpression|triggerHitArea|mountVoicePack|unmountVoicePack|setOpacity|setVolume|setMuted|setFps)[[:space:]]*\('
UI_MGR='(m_instanceManager|instanceManager|m_manager|mgr)[[:space:]]*->[[:space:]]*(createInstance|deleteInstance)[[:space:]]*\('
UI_SES='(s|session|inst|m_session)[[:space:]]*->[[:space:]]*(loadModel|playMotion|setExpression|triggerHitArea|mountVoicePack|unmountVoicePack|setOpacity|setVolume|setMuted|setFps|setAutoStart|resetLayout|getLayout|start|stop|restart)[[:space:]]*\('

# Approved bridge receivers (QML): calls on these ARE the API path.
QML_APPROVED='(instanceControl|rosterModel)[.]'

# ── Allowlist — file-path ERE :: line-content ERE ───────────────────────────
# Each entry carries its justification; entries are precise on purpose
# (a widening edit must come with a new reason, visible in review).
#
# [1] instance.setAutoStart( — per-instance start-with-panel flag.
#     Not in any API family (deliberately: host persistence convenience,
#     no renderer round-trip). Documented debt — revisit when a
#     lifecycle-adjacent family absorbs it.
# [2] instance.resetLayout( — renderer layout reset; getLayout( (same
#     category, currently unused in QML) is covered by the identical
#     debt note. Outside the current API families.
# [3] instanceManager.stopAll( — host EXIT-SHELL flow (Main.qml close
#     sequence: stop every renderer before quit). Not an instance op;
#     TODO route through an API family if a shell-lifecycle family ever
#     lands. stopAll does not match the violation patterns today — the
#     entry documents the decision and future-proofs the pattern set.
# [4] src/ui/VoicePackController.cpp legacy mount fallback — the
#     production path above it forwards through the shared
#     pet::ITuningApi; this branch runs only when NO api was injected
#     (degraded wiring / unit tests with fakes). S6 seam, kept for the
#     null-API VoicePackController tests.
ALLOWLIST=(
    'qml/pages/InstanceDetailPage\.qml::instance[[:space:]]*[.]setAutoStart[[:space:]]*\('
    'qml/pages/InstanceDetailPage\.qml::instance[[:space:]]*[.]resetLayout[[:space:]]*\('
    'qml/Main\.qml::instanceManager[[:space:]]*[.]stopAll[[:space:]]*\('
    'src/ui/VoicePackController\.cpp::s->(mountVoicePack|unmountVoicePack)'
)

is_allowlisted() { # $1 = repo-relative path, $2 = line
    local entry path_re line_re
    for entry in "${ALLOWLIST[@]}"; do
        path_re="${entry%%::*}"
        line_re="${entry##*::}"
        if [[ "$1" =~ $path_re ]] && [[ "$2" =~ $line_re ]]; then
            return 0
        fi
    done
    return 1
}

violations=0

report() { # $1 = file, $2 = line no, $3 = line, $4 = pattern tag
    if is_allowlisted "$1" "$3"; then
        return 0
    fi
    echo "API-BOUNDARY VIOLATION: $1:$2 [$4]"
    echo "    $3"
    violations=$((violations + 1))
}

check_qml_file() { # $1 = repo-relative .qml path
    local file="$1" line linenum=0
    while IFS= read -r line || [ -n "$line" ]; do
        linenum=$((linenum + 1))
        if [[ "$line" =~ $QML_UNIQUE ]] && ! [[ "$line" =~ $QML_APPROVED ]]; then
            report "$file" "$linenum" "$line" "QML-U"
        elif [[ "$line" =~ $QML_SSR ]]; then
            report "$file" "$linenum" "$line" "QML-SSR"
        elif [[ "$line" =~ $QML_CHAIN ]]; then
            report "$file" "$linenum" "$line" "QML-CHAIN"
        fi
    done < "$file"
}

check_ui_file() { # $1 = repo-relative src/ui/*.cpp path
    local file="$1" line linenum=0
    while IFS= read -r line || [ -n "$line" ]; do
        linenum=$((linenum + 1))
        if [[ "$line" =~ $UI_MGR ]]; then
            report "$file" "$linenum" "$line" "UI-MGR"
        elif [[ "$line" =~ $UI_SES ]]; then
            report "$file" "$linenum" "$line" "UI-SES"
        fi
    done < "$file"
}

# Work relative to ROOT so violations + allowlist share one path space.
cd "$ROOT" || exit 2

while IFS= read -r f; do
    check_qml_file "$f"
done < <(find qml -name '*.qml' | sort)

while IFS= read -r f; do
    check_ui_file "$f"
done < <(find src/ui -name '*.cpp' | sort)

if [ "$violations" -gt 0 ]; then
    echo ""
    echo "api_boundary_gate: $violations violation(s) — host writes must go through the"
    echo "API bridges (QML: instanceControl / rosterModel; src/ui: injected pet:: APIs)."
    echo "Extend scripts/api_boundary_gate.sh's ALLOWLIST ONLY with a reviewed justification."
    exit 1
fi
echo "api_boundary_gate: clean ($ROOT)"
exit 0
