#pragma once

// Plugin SDK — model library READ family (S5, v1.3).
//
// Post-freeze interface family: derives pet::IExtApi and is obtained
// EXCLUSIVELY through IPluginContext::queryApi(kModelApiId, minVersion).
// READ-ONLY and therefore deliberately NOT gated: neither the
// plugin_write_enabled kill-switch nor any manifest capability applies
// (the switch revokes WRITES; discovery metadata leaks nothing the
// model-library page does not already show every user). When the host
// has no shared implementation wired the query returns nullptr
// ("feature absent").
//
// The data is the host's single model-scan cache (ModelScanner +
// ModelInfoParser over <rendererDir>/Resources/Models) — the SAME cache
// the panel's own model-library page consumes; plugins and the host UI
// observe one scan, not two.
//
// Threading contract: GUI thread, never blocking (cache lookups only).
//
// ABI discipline: same rules as every api/ header — pure virtual
// declarations + Qt value types only, no std:: across the boundary, no
// inline function bodies (the defaulted destructor exemption applies).

#include "api/IPluginContext.hpp"
#include "api/PluginTypes.hpp"

namespace pet {

// apiId + per-family version for queryApi(). Version 1 = the read
// surface below. minVersion > this → queryApi returns nullptr ("feature
// absent", never an error).
inline constexpr char kModelApiId[] = "pet.model";
inline constexpr int kModelApiVersion = 1;

class IModelApi : public IExtApi
{
public:
    // Every discovered model, in scan order (case-insensitive by name).
    // Empty when no renderer dir is known or nothing is bundled — never
    // an error.
    virtual QVector<ModelSummary> availableModels() = 0;

    // Ask the host to re-run the model scan (the panel's own library
    // page revision bump rides the same rescan). Cheap; results arrive
    // via the next availableModels()/modelInfo() call.
    virtual void refreshScan() = 0;

    // One model's cached summary by name. Ok + populated *out on hit;
    // NotFound when the name is not in the current scan (out is cleared
    // first — a non-null out is never left half-written).
    virtual PluginError modelInfo(const QString& name, ModelSummary* out) = 0;
};

} // namespace pet
