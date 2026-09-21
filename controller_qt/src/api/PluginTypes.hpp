#pragma once

// Plugin SDK — shared value types (P4, docs/refactor §B.3).
//
// ABI discipline (§B.3, enforced at review — see src/api/README.md):
//   - ONLY plain-old-data structs and Qt value types (QString/QVector/...)
//     cross the plugin boundary; no std:: types, no exported templates
//   - no inline function bodies in interface headers (pure declarations;
//     the defaulted virtual destructors below are the standard C++
//     interface idiom and are explicitly exempted — they carry no logic
//     and generate no cross-module symbol reference until stage 2, where
//     ownership rules will be re-reviewed at the P6a freeze)
//
// Stage note (§A.3): stage 1 plugins are compiled into the panel binary
// (STATIC). These types are nonetheless designed to stage-2 ABI standards
// so the future switch to QPluginLoader is "change the loader", not
// "change the interfaces".

#include <QString>
#include <QVector>

namespace pet {

// ── Host API version ────────────────────────────────────────────────────────
// Gating rule (§A.2): a plugin is accepted when host_major > plugin_major,
// or host_major == plugin_major && host_minor >= plugin_minor (OBS-style
// asymmetric rule). Bump MINOR for additive changes, MAJOR for breaking ones.
// P6a is the freeze point; afterwards these become versioned contract data.
// v1.1 (2026-09-21): minor bumped for the additive expansion — capability
// vocabulary constants (below), the IExtApi marker + IPluginContext::
// queryApi() seam, and the overturned "IInstanceApi stays read-only"
// commitment (decision record: docs/refactor/plugin-architecture-and-
// cmake-migration.md, revision v3 2026-09-21).
// v1.2 (2026-09-21, S2 write path): minor bumped for the additive
// expansion — InstanceSpec / InstanceRuntime PODs (below), the new
// IInstanceControlApi family (IExtApi subclass, exposed via queryApi,
// gated by kCapabilityInstanceLifecycle), and the IInstanceObserver +
// IInstanceApi::subscribeInstances/unsubscribeInstances tail append
// (stage-1 privilege, consumed with this bump).
constexpr int kApiMajor = 1;
constexpr int kApiMinor = 2;

// ── Capability vocabulary (manifest "capabilities" entries; §B.4) ───────────
// The exact, case-sensitive strings the host parses from plugin manifests
// (PluginRegistry takes entries verbatim) and matches when gating API
// surfaces. Plugins declare these in plugin.json; the host matches them as
// plain string equality — no normalization, no aliases. Declared here so
// plugin code and host gates share one spelling (v1.1 收编: "network" was
// previously only a QStringLiteral literal inside PluginContextImpl).
//
// Form note: inline constexpr QLatin1StringView is a compile-time constant
// (literal type, C++17 inline variable — one entity across TUs); it is not
// an inline function body, so the §B.3 no-inline-implementation rule does
// not apply. Usable directly in Qt string comparisons
// (e.g. list.contains(kCapabilityNetwork)).
inline constexpr QLatin1StringView kCapabilityNetwork =
    QLatin1StringView("network");              // gates IDownloadApi real
                                               // implementation (vs. capability
                                               // stub) — the only capability
                                               // enforced today (P5, §B.4)
inline constexpr QLatin1StringView kCapabilityVoicepacksInstall =
    QLatin1StringView("voicepacks_install");   // (S2) gates write access to
                                               // the voice-pack install
                                               // pipeline / Resources/
                                               // VoicePacks landing zone
inline constexpr QLatin1StringView kCapabilityInstanceLifecycle =
    QLatin1StringView("instance_lifecycle");   // (S2) gates IInstanceControlApi
                                               // lifecycle ops: create /
                                               // remove / start / stop pet
                                               // instances
inline constexpr QLatin1StringView kCapabilityInstanceTuning =
    QLatin1StringView("instance_tuning");      // (S2) gates IInstanceControlApi
                                               // tuning ops: scale / layout /
                                               // motion parameters
inline constexpr QLatin1StringView kCapabilitySettingsWrite =
    QLatin1StringView("settings_write");       // (S2) gates ISettingsApi:
                                               // writes to whitelisted panel
                                               // config keys

// ── Error codes ─────────────────────────────────────────────────────────────
// Every interface method that can fail reports through this enum; exceptions
// must NOT cross the boundary (the host catches everything anyway — §A.1 —
// but plugins should not rely on that).
enum class PluginError : unsigned int {
    Ok = 0,            // success
    Generic = 1,       // unspecified failure (see accompanying message)
    InvalidArgument = 2,
    Capability = 3,    // capability not granted by the plugin manifest
    NotFound = 4,
    Busy = 5,
    Cancelled = 6,     // P5: the operation was cancelled by the caller
};

// Log levels for IPluginContext::log(). Values are stable contract data.
enum class PluginLogLevel : unsigned int {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
};

// ── Snapshot structs (POD; copied across the boundary, never referenced) ───

// IInstanceApi::instances() row. Read-only roster view of one pet instance.
struct InstanceInfo {
    QString uuid;
    QString label;
    QString modelName;
    QString status;      // InstanceSession status string ("running", ...)
    bool connected = false;
};

// IInstanceControlApi::create() argument (v1.2, S2). Mirrors the panel's own
// Add flow semantics: label is required (empty → PluginError::
// InvalidArgument), avatar/modelName empty = the InstanceConfig defaults,
// autoStart persists the start-with-panel flag (the instance is NOT launched
// synchronously — the flag applies on the next panel start, exactly like the
// detail page's auto-start toggle).
struct InstanceSpec {
    QString label;           // required non-empty
    QString avatar;          // empty = default ("🐱")
    QString modelName;       // empty = default ("Hiyori")
    bool autoStart = false;  // persist InstanceConfig.autoStart
};

// IInstanceObserver::instanceStateChanged() payload (v1.2, S2): full
// per-instance runtime snapshot. Coarse-grained by design — the host fans
// this out on any InstanceSession property NOTIFY; observers re-read the
// whole struct instead of subscribing per field.
struct InstanceRuntime {
    QString uuid;
    QString label;
    QString modelName;
    QString status;          // InstanceSession status string
    QString mountedPackId;   // mounted voice-pack directory name ("" = none)
    bool connected = false;
    bool modelLoaded = false;
    bool muted = false;
    double opacity = 1.0;
    double volume = 1.0;
    int targetFps = 30;
    int restartAttempts = 0;
};

// IVoicePackApi::listPacks() row.
struct PackInfo {
    QString id;          // directory name under Resources/VoicePacks
    QString displayName;
    QString version;
    QString dirPath;     // absolute path to the pack directory
    int groupCount = 0;  // behavior groups parsed from meta.mko
};

// IDownloadApi::start() argument. All fields are host-validated before any
// byte is fetched: url must be https:// or file:// (local mirrors / offline
// installs — same convention as the host's own CUBISM_SDK_URL override),
// expectedSha256 (when non-empty) must be 64 hex chars (case-insensitive,
// normalized by the host), destName must be a single path segment.
struct DownloadRequest {
    QString url;
    QString expectedSha256;
    QString destName;    // file name inside the host download staging area
};

// IDownloadApi::installArchive() argument. targetDir is validated against
// zip-slip + allow-list rules by the host pipeline (§B.4); plugins never
// touch the file system during install.
struct InstallSpec {
    QString targetDir;   // logical destination (e.g. "voicePacks")
};

// IUiApi::registerPage() argument. One navigation entry in the panel sidebar.
struct PageDescriptor {
    QString title;       // sidebar + page title (already localized by plugin)
    QString iconUrl;     // qrc:/ or file:/ URL; empty = host default glyph
    QString qmlUrl;      // qrc:/ URL of the page's root QML document
    int order = 100;     // ascending; built-in pages occupy 0-99 (§B.6)
};

} // namespace pet
