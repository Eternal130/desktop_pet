#pragma once

// PluginRegistry (P4, §B.6) — manifest parsing/validation + the plugin
// state machine + the entry store. Pure data/logic layer: no QML, no GUI.
//
// State machine (stage-1 simplified, §B.6):
//   Discovered → Validated → Registered → Started → (Stopped | Failed)
//   Failed is reachable from ANY state (validation failure, initialize
//   error, escaping exception); Stopped only from Started.
//
// Compile-time static plugins (§A.2) are abi-EXEMPT: same repo, same build.
// The abi-validation code path still exists (validateAbi) because stage 2
// (QPluginLoader) needs it and P4 unit tests lock its rules now.
//
// Runtime full-schema validation lives here (§B.6 v2): the configure-time
// CMake check is only field-existence + api_version shape — this class is
// the authoritative validator.

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

#include "api/IPanelPlugin.hpp"

namespace core {

enum class PluginStatus {
    Discovered,
    Validated,
    Registered,
    Started,
    Stopped,
    Failed,
};

// QString form for logs + the QML model (stable strings, part of the UI
// contract of the plugin management section).
QString pluginStatusToString(PluginStatus status);

// Parsed plugin.json (§B.6 manifest table). abi is injected by the build
// (§A.2) for static plugins and parsed from the file in stage 2.
struct PluginManifest {
    QString id;              // reverse domain, unique key
    QString version;         // plugin semver (display only in stage 1)
    int apiMajor = 0;
    int apiMinor = 0;
    QStringList capabilities; // e.g. { "network", "voicepacks_install" }
    QString title;
    QString entryQml;        // page file name ("SamplePage.qml")
    QString icon;            // icon file name / emoji token
    int order = 100;         // nav order; built-ins occupy 0-99
    QString vendor;

    // Build-injected (§B.6 resource layout): qrc:/ URLs resolved by the
    // plugin's qt_add_qml_module — empty for dynamic (stage 2) entries.
    QString qmlUrl;
    QString iconUrl;
};

struct PluginEntry {
    PluginManifest manifest;
    PluginStatus status = PluginStatus::Discovered;
    QString errorMessage;    // populated iff status == Failed
    QString source = QStringLiteral("static"); // "static" | "dynamic" (P2+)
    bool abiExempt = true;   // §A.2: compile-time plugins skip abi gating

    // Factory + instance are host-owned. create stays null for dynamic
    // entries until stage 2 wires QPluginLoader::instance.
    pet::IPanelPlugin* (*create)() = nullptr;
    pet::IPanelPlugin* instance = nullptr;
};

class PluginRegistry
{
public:
    using CreateFn = pet::IPanelPlugin* (*)();

    // ── Validation (pure functions; unit-locked) ─────────────────────────
    // Full schema check of a manifest JSON document. Returns the parsed
    // manifest or nullopt + error (never throws — corrupt/truncated input
    // is a normal, UI-reportable condition).
    static std::optional<PluginManifest> parseManifest(const QString& manifestJson,
                                                        QString* error = nullptr);

    // api_version gating (§A.2, OBS asymmetric rule): host_major >
    // plugin_major, or same major and host_minor >= plugin_minor.
    static bool validateApiVersion(int pluginMajor, int pluginMinor,
                                   QString* error = nullptr);

    // abi gating (§A.2) — pure comparison so tests can lock the rules
    // without touching the real toolchain. Fields: compiler +
    // compiler_version strict-equal; qt_version same-major with
    // plugin-minor <= host-minor (patch free); qt_build + build_type
    // strict-equal. Unknown/missing fields → reject (fail closed).
    static bool validateAbi(const QJsonObject& pluginAbi, const QJsonObject& hostAbi,
                            QString* error = nullptr);

    // ── Entry store ───────────────────────────────────────────────────────
    // Register a compile-time plugin: parse + validate (runtime authority,
    // §B.6); on failure the entry is STILL added with status Failed so the
    // management UI surfaces it (never silently dropped). On success the
    // entry lands as Registered (Validated is passed through internally —
    // static plugins validate at registration time, no discovery phase).
    // abiJson: the build-generated §A.2 block (provenance only — static
    // plugins are abi-EXEMPT). qmlUrl: the build-injected qrc:/ page URL
    // (§B.6) the host fills into IUiApi::registerPage descriptors that
    // leave qmlUrl empty.
    void addStaticPlugin(const QString& manifestJson, CreateFn create,
                         const QString& abiJson = QString(),
                         const QString& qmlUrl = QString());

    // State-machine transition. Returns false (and leaves the state
    // unchanged) on an illegal transition; legal Failed transitions always
    // record errorMessage.
    bool transition(const QString& id, PluginStatus to, const QString& errorMessage = {});

    PluginEntry* entry(const QString& id);
    const PluginEntry* entry(const QString& id) const;

    // Entries in manifest order (order asc, then id asc) — the
    // initialize() order mandated by §B.3.
    QList<PluginEntry> orderedEntries() const;

    int size() const { return static_cast<int>(m_entries.size()); }

private:
    bool legalTransition(PluginStatus from, PluginStatus to) const;

    QList<PluginEntry> m_entries; // insertion order; sorted views via orderedEntries
};

} // namespace core
