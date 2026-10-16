#pragma once

// DynamicPluginSource (stage-2 host channel, 2026-10-16, SDK v1.12) —
// drop-in dynamic plugin discovery: scans the two plugin directories at
// boot, validates every candidate on METADATA ONLY, and registers the
// survivors into PluginRegistry via addDynamicPlugin.
//
// Architecture invariants (§A.2 / §A.4 — enforced by this class):
//   - metaData() ALWAYS precedes instance(): every gate (iid, manifest,
//     api_version, id uniqueness, abi) runs on the module metadata; a
//     rejected file executes ZERO library code — instance() is called
//     exactly once per accepted file, after all gates passed.
//   - load-only lifetime (§A.4 hot-unload ban): loaded handles are held
//     for the whole process lifetime and carry PreventUnloadHint; there
//     is no unload path (the QPluginLoader dtor honors the hint).
//   - static-first registration: main() registers the CMake static table
//     BEFORE discover(), so a dynamic file colliding with a static id is
//     rejected as a duplicate — static ids can never be shadowed.
//
// Injection seams (the setDatabase/setRendererDir pattern — tests feed
// fakes, production uses the QPluginLoader-backed implementations in the
// .cpp):
//   - ILibraryLoader/ILibraryHandle wrap the per-file QPluginLoader; the
//     production handle sets PreventUnloadHint at construction.
//   - setSearchDirs() overrides the directory pair (default:
//     <exeDir>/plugins then ConfigDir::pluginsDir, exe-dir first — a
//     same-NAME file in both resolves to the exe-dir copy).
//
// Lifetime: a DynamicPluginSource must outlive PluginHost::shutdownAll()
// (main() declares it at function scope for exactly that reason) — the
// member handle list keeps every loaded module referenced until then.

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

#include "core/PluginRegistry.hpp"

namespace core {

// One loaded plugin file (per-file granularity so tests can fake at the
// exact production boundary). Production implementation wraps a
// QPluginLoader and lives in the .cpp.
class ILibraryHandle
{
public:
    virtual ~ILibraryHandle() = default;

    // The module's metaData() — the raw QPluginLoader object: "iid" at
    // the top level, the plugin.json content nested under "metadata".
    // Must be obtainable WITHOUT executing plugin code.
    virtual QJsonObject pluginMetaData() const = 0;

    // Load + return the root QObject (the plugin's factory). Only legal
    // after the caller validated the metadata.
    virtual QObject* instance() = 0;

    virtual QString errorString() const = 0;

    // Re-assert the never-unload hint (idempotent).
    virtual void setPreventUnload() = 0;
};

// Factory for per-file handles. open() returning nullptr = the file could
// not even be opened (logged + skipped by the caller).
class ILibraryLoader
{
public:
    virtual ~ILibraryLoader() = default;

    virtual std::unique_ptr<ILibraryHandle> open(const QString& filePath) = 0;
};

class DynamicPluginSource : public QObject
{
    Q_OBJECT

public:
    explicit DynamicPluginSource(QObject* parent = nullptr);

    // Test seam: inject a fake loader (ownership taken). When unset,
    // discover() constructs the production QPluginLoader-backed loader.
    void setLoader(std::unique_ptr<ILibraryLoader> loader);

    // Test seam: override the search directories (priority order; the
    // first entry wins same-name collisions). Empty = production default
    // (<exeDir>/plugins then ConfigDir::pluginsDir()).
    void setSearchDirs(const QStringList& dirs) { m_searchDirs = dirs; }

    // Scan the plugin directories and register every candidate into
    // `registry`. Missing directories are silently skipped (never
    // created); rejected files become Failed entries (never silently
    // dropped). Call AFTER the static factory table was registered.
    void discover(PluginRegistry& registry);

private:
    void loadCandidate(const QString& filePath, PluginRegistry& registry);

    std::unique_ptr<ILibraryLoader> m_loader;
    QStringList m_searchDirs; // empty = production defaults
    // Process-lifetime handle ownership — never unloaded (§A.4).
    // std::vector, not QList: the elements are move-only and this member
    // is host-internal (std discipline applies only ACROSS the plugin
    // boundary, never inside core).
    std::vector<std::unique_ptr<ILibraryHandle>> m_handles;
};

} // namespace core
