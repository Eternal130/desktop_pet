#pragma once

// Plugin SDK — resource-scope family (v1.13 resource isolation, rev2 §3.3).
//
// Post-freeze interface family: derives pet::IExtApi and is obtained
// EXCLUSIVELY through IPluginContext::queryApi(kResourceScopeApiId,
// minVersion).
//
// 门控语义（无能力词；属主校验即边界 — the pet.rules precedent）: the
// family is deliberately NOT gated by any manifest capability NOR the
// plugin_write_enabled kill-switch at queryApi time. The boundary is the
// OWNER CHECK inside the per-context proxy: a plugin passes its OWN id
// implicitly (the proxy stamps callerId = the context's plugin id), and
// every read/write is confined to that plugin's data domain
// (<dataDir>/PluginData/<ownerId>/). A foreign or empty ownerId never
// reaches the file system — reads come back empty, removeScoped is
// rejected. The host UI holds the raw shared implementation (the host
// domain is not addressable through this family's plugin channel).
//
// Data: the union scans (core::scanModelsScoped / scanVoicePacksScoped)
// over the host renderer tree + every plugin data domain — PURE
// directory traversal, never the loaded plugin registry (rev2 §3.2c:
// disabled/unloaded plugins' domains stay scannable).
//
// Threading contract: GUI thread, never blocking (QDir traversal over
// sidebar-sized trees — the VoicePackApiImpl::listPacks cost class).
//
// ABI discipline: same rules as every api/ header — pure virtual
// declarations + Qt value types only, no std:: across the boundary, no
// inline function bodies (the defaulted destructor exemption applies).

#include "api/IPluginContext.hpp"
#include "api/PluginTypes.hpp"

namespace pet {

// apiId + per-family version for queryApi(). Version 1 = the surface
// below. minVersion > this → queryApi returns nullptr ("feature
// absent", never an error).
inline constexpr char kResourceScopeApiId[] = "pet.resource_scope";
inline constexpr int kResourceScopeApiVersion = 1;

// IResourceScopeApi::scopedModels() row. Field types mirror ModelSummary
// (PluginTypes.hpp) exactly; `owner` names the resource DOMAIN (the
// immediate directory under <dataDir>/PluginData/ that owns the model).
struct ScopedModelInfo {
    QString name;             // directory name == model id within the domain
    QString owner;            // owning plugin id (the data-domain directory)
    QStringList motionGroups; // group names (QMap key order = sorted)
    QStringList expressions;  // FileReferences.Expressions[].Name
    QStringList hitAreas;     // HitAreas[].Name
};

// IResourceScopeApi::scopedPacks() row. Field types mirror PackInfo
// (PluginTypes.hpp) exactly; `owner` names the resource DOMAIN.
struct ScopedPackInfo {
    QString id;          // directory name under the domain's VoicePacks/
    QString owner;       // owning plugin id (the data-domain directory)
    QString displayName;
    QString version;     // meta.mko carries no version field — stays empty
    QString dirPath;     // absolute path to the pack directory
    int groupCount = 0;  // behavior groups parsed from meta.mko
};

class IResourceScopeApi : public IExtApi
{
public:
    // Every model in the CALLER's data domain, in scan order. ownerId is
    // stamped by the per-context proxy (the plugin's own id) — the raw
    // shared implementation validates ownerId == callerId; an empty or
    // foreign ownerId yields an EMPTY vector (read families never error;
    // the rejection is logged under the plugin's context log).
    virtual QVector<ScopedModelInfo> scopedModels(const QString& ownerId) = 0;

    // Every voice pack in the CALLER's data domain, in scan order. Same
    // owner-stamping/empty-on-mismatch contract as scopedModels().
    virtual QVector<ScopedPackInfo> scopedPacks(const QString& ownerId) = 0;

    // Ask the host to re-run the resource scans (the panel's own model
    // cache rides the same refresh when the impls are cross-wired).
    // Cheap; results arrive via the next scopedModels()/scopedPacks().
    virtual PluginError refreshScan() = 0;

    // Recursively remove ONE model directory (isModel true) or voice-pack
    // directory (isModel false) named `name` from the CALLER's data
    // domain. InvalidArgument for an empty/traversal-bearing name
    // ('/', '\\', "..") or a foreign ownerId; NotFound when the directory
    // does not exist; Generic when the recursive delete fails. The host
    // domain is not addressable through the plugin channel.
    virtual PluginError removeScoped(const QString& ownerId,
                                     const QString& name, bool isModel) = 0;
};

} // namespace pet
