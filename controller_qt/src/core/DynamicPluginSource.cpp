#include "core/DynamicPluginSource.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLibrary>
#include <QPluginLoader>
#include <QSet>

#include <spdlog/spdlog.h>

#include "api/IPluginFactory.hpp"
#include "core/ConfigDir.hpp"
#include "logging/Logging.hpp"

namespace core {
namespace {

// The factory-root iid gate compares against PET_PLUGIN_FACTORY_IID from
// api/IPluginFactory.hpp (single source of the spelling — P2-1). Macro,
// not a local constant, so this gate and Q_DECLARE_INTERFACE can never
// drift apart.

// ── Production implementations of the loader seams ─────────────────────────

// QPluginLoader wrapper. The load-only discipline starts here:
// PreventUnloadHint is set at CONSTRUCTION, before any load attempt, so
// the handle (and the panel) can never unload the module — not on
// destruction, not via unload() (§A.4).
class QtPluginLibraryHandle final : public ILibraryHandle
{
public:
    explicit QtPluginLibraryHandle(const QString& filePath)
        : m_loader(filePath)
    {
        m_loader.setLoadHints(QLibrary::PreventUnloadHint);
    }

    QJsonObject pluginMetaData() const override { return m_loader.metaData(); }
    QObject* instance() override { return m_loader.instance(); }
    QString errorString() const override { return m_loader.errorString(); }
    void setPreventUnload() override
    {
        m_loader.setLoadHints(m_loader.loadHints() | QLibrary::PreventUnloadHint);
    }

private:
    QPluginLoader m_loader;
};

class QtPluginLibraryLoader final : public ILibraryLoader
{
public:
    std::unique_ptr<ILibraryHandle> open(const QString& filePath) override
    {
        return std::make_unique<QtPluginLibraryHandle>(filePath);
    }
};

// Sanitize a file base name into a manifest-id-safe segment ([A-Za-z0-9.-]
// kept, everything else → '_') for the synthetic ids of pre-manifest
// rejections.
QString sanitizedBaseName(const QString& filePath)
{
    QString base = QFileInfo(filePath).completeBaseName();
    for (QChar& c : base) {
        const char16_t u = c.unicode();
        const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                        (u >= '0' && u <= '9') || u == '.' || u == '-';
        if (!ok)
            c = QLatin1Char('_');
    }
    if (base.isEmpty())
        base = QStringLiteral("unnamed");
    return base;
}

} // namespace

DynamicPluginSource::DynamicPluginSource(QObject* parent)
    : QObject(parent)
{
}

void DynamicPluginSource::setLoader(std::unique_ptr<ILibraryLoader> loader)
{
    m_loader = std::move(loader);
}

void DynamicPluginSource::discover(PluginRegistry& registry)
{
    if (!m_loader)
        m_loader = std::make_unique<QtPluginLibraryLoader>();

    // Priority order: exe-dir plugins (shipped with the app) first, then
    // the per-user data dir. Empty m_searchDirs = production defaults.
    QStringList dirs = m_searchDirs;
    if (dirs.isEmpty()) {
        dirs.append(QDir(QCoreApplication::applicationDirPath())
                        .absoluteFilePath(QStringLiteral("plugins")));
        dirs.append(ConfigDir::pluginsDir());
    }

    // Platform library suffix (the only files considered).
#ifdef Q_OS_WIN
    static const QStringList kNameFilters = {QStringLiteral("*.dll")};
#else
    static const QStringList kNameFilters = {QStringLiteral("*.so")};
#endif

    QSet<QString> seenNames;
    for (const QString& dirPath : dirs) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue; // nothing installed here — never created (§B.6)
        const QFileInfoList files = dir.entryInfoList(
            kNameFilters, QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo& fi : files) {
            if (seenNames.contains(fi.fileName())) {
                LOG_INFO("DynamicPluginSource: '{}' shadowed by a higher-priority "
                         "copy — skipped",
                         fi.absoluteFilePath().toStdString());
                continue;
            }
            seenNames.insert(fi.fileName());
            loadCandidate(fi.absoluteFilePath(), registry);
        }
    }
}

void DynamicPluginSource::loadCandidate(const QString& filePath,
                                        PluginRegistry& registry)
{
    std::unique_ptr<ILibraryHandle> handle = m_loader->open(filePath);
    if (!handle) {
        LOG_WARN("DynamicPluginSource: cannot open '{}' — skipped",
                 filePath.toStdString());
        return;
    }

    // §A.2 ordering invariant: everything below the metaData() read runs
    // on metadata only; instance() happens once, at the very end, after
    // every gate passed. A rejected file executes ZERO library code.
    const QJsonObject meta = handle->pluginMetaData();
    if (meta.isEmpty()) {
        LOG_WARN("DynamicPluginSource: '{}' has no plugin metadata ({}) — not a "
                 "Qt plugin, skipped",
                 filePath.toStdString(),
                 handle->errorString().toStdString());
        return;
    }

    // Gate 1 — iid: the module must declare the factory root iid. NOTE the
    // Qt 6.10 metaData() key casing is literally "IID" / "MetaData"
    // (capitalized) — verified empirically against QPluginLoader output;
    // the lowercase spellings silently return nothing. Test fakes must
    // use the SAME casing so they exercise the real contract.
    const QString iid = meta.value(QLatin1String("IID")).toString();
    if (iid != QLatin1String(PET_PLUGIN_FACTORY_IID)) {
        const QString id =
            QStringLiteral("dropin.%1").arg(sanitizedBaseName(filePath));
        const QString synthetic = QStringLiteral(
            R"json({"id":"%1","version":"0.0.0","api_version":"1.0"})json").arg(id);
        registry.addDynamicPlugin(synthetic, /*factory=*/nullptr, filePath,
                                  hostAbiBlock());
        // P1-1 guard (same shape as the backfill guard below): only flip
        // the entry WE just registered. A legitimate plugin whose id
        // happens to equal the synthetic id (e.g. a real "dropin.foo")
        // must never be rewritten by an unrelated rejected file — in the
        // collision case addDynamicPlugin above already appended the
        // visible duplicate-Failed entry, so dropping the transition only
        // means the error text stays "duplicate plugin id".
        PluginEntry* syntheticEntry = registry.entry(id);
        if (syntheticEntry != nullptr &&
            syntheticEntry->source == QStringLiteral("dynamic") &&
            syntheticEntry->status == PluginStatus::Registered &&
            syntheticEntry->libraryPath == filePath) {
            registry.transition(id, PluginStatus::Failed,
                                QStringLiteral("unsupported iid '%1' (expected '%2')")
                                    .arg(iid,
                                         QString::fromLatin1(
                                             PET_PLUGIN_FACTORY_IID)));
            LOG_ERROR("DynamicPluginSource: '{}' rejected — unsupported iid '{}'",
                      filePath.toStdString(), iid.toStdString());
        } else {
            LOG_WARN("DynamicPluginSource: '{}' has unsupported iid '{}' and its "
                     "synthetic id '{}' collides with an existing entry — "
                     "existing entry left untouched",
                     filePath.toStdString(), iid.toStdString(),
                     id.toStdString());
        }
        return;
    }

    // Gates 2..4 — manifest / api_version / id uniqueness / abi, all
    // inside addDynamicPlugin (visible Failed entries on every rejection).
    // The plugin FILE content is nested under "MetaData" (Qt 6.10's
    // capitalized metaData() key — see the iid gate note above); the abi
    // block lives inside that manifest ("abi" key).
    const QJsonObject pluginMeta =
        meta.value(QLatin1String("MetaData")).toObject();
    const QString manifestJson = QString::fromUtf8(
        QJsonDocument(pluginMeta).toJson(QJsonDocument::Compact));
    const QJsonObject abi = pluginMeta.value(QLatin1String("abi")).toObject();
    const QString abiJson = QString::fromUtf8(
        QJsonDocument(abi).toJson(QJsonDocument::Compact));

    // Parse here as well (registry parses again internally) so the source
    // knows the id for the post-registration lookups below.
    QString parseError;
    const auto manifest = PluginRegistry::parseManifest(manifestJson, &parseError);
    if (!manifest.has_value()) {
        // addDynamicPlugin registers the visible Failed entry itself.
        registry.addDynamicPlugin(manifestJson, /*factory=*/nullptr, filePath,
                                  abiJson);
        return;
    }
    const QString id = manifest->id;

    // Register-then-backfill: the entry enters the registry with a null
    // factory (visible as "dynamic"/Registered immediately) and gets its
    // factory pointer only after instance() + qobject_cast succeed.
    registry.addDynamicPlugin(manifestJson, /*factory=*/nullptr, filePath,
                              abiJson);

    PluginEntry* entry = registry.entry(id);
    if (entry == nullptr || entry->source != QStringLiteral("dynamic") ||
        entry->status != PluginStatus::Registered ||
        entry->libraryPath != filePath) {
        // Rejected by a gate, or `id` resolved to an EARLIER same-id entry
        // (the duplicate rejection appends a new Failed row; entry(id)
        // returns the first). Either way: instance() stays untouched.
        return;
    }

    // FIRST and only instance() — every gate above ran on metadata.
    // P0-2: root construction runs ARBITRARY module code (Q_OBJECT static
    // initializers + the root ctor) — an escaping exception must mark the
    // entry Failed, never abort the boot scan.
    QObject* root = nullptr;
    try {
        root = handle->instance();
    } catch (const std::exception& ex) {
        registry.transition(id, PluginStatus::Failed,
                            QStringLiteral("root construction threw: %1")
                                .arg(QString::fromUtf8(ex.what())));
        LOG_ERROR("DynamicPluginSource: '{}' root construction THREW "
                  "(isolated; continuing): {}",
                  filePath.toStdString(), ex.what());
        return;
    } catch (...) {
        registry.transition(id, PluginStatus::Failed,
                            QStringLiteral("root construction threw an "
                                           "unknown exception"));
        LOG_ERROR("DynamicPluginSource: '{}' root construction threw an "
                  "UNKNOWN exception (isolated; continuing)",
                  filePath.toStdString());
        return;
    }

    // P1-2: a NULL root is a LOAD failure (missing dependency, wrong
    // architecture, ...) — the loader's error string is the honest reason.
    // Only a non-null root that fails qobject_cast is a "wrong root
    // interface" problem.
    if (root == nullptr) {
        const QString loadError = handle->errorString();
        registry.transition(id, PluginStatus::Failed,
                            loadError.isEmpty()
                                ? QStringLiteral("library load failed")
                                : loadError);
        LOG_ERROR("DynamicPluginSource: '{}' failed to load: {}",
                  filePath.toStdString(), loadError.toStdString());
        return;
    }
    auto* factory = qobject_cast<pet::IPluginFactory*>(root);
    if (factory == nullptr) {
        registry.transition(id, PluginStatus::Failed,
                            QStringLiteral("root object does not implement "
                                           "IPluginFactory"));
        LOG_ERROR("DynamicPluginSource: '{}' root object is not an "
                  "IPluginFactory — rejected",
                  filePath.toStdString());
        return;
    }

    handle->setPreventUnload();
    entry->factory = factory; // backfill (register-then-backfill)
    m_handles.push_back(std::move(handle));
    LOG_INFO("DynamicPluginSource: dynamic plugin '{}' loaded from '{}'",
             id.toStdString(), filePath.toStdString());
}

} // namespace core
