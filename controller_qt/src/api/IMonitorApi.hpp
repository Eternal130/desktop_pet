#pragma once

// Plugin SDK — resource-monitor READ family (S7, v1.4).
//
// Post-freeze interface family: derives pet::IExtApi and is obtained
// EXCLUSIVELY through IPluginContext::queryApi(kMonitorApiId, minVersion).
// READ-ONLY and therefore deliberately NOT gated: neither the
// plugin_write_enabled kill-switch nor any manifest capability applies
// (the switch revokes WRITES; resource stats leak nothing the Monitor
// page does not already show). When the host has no shared
// implementation wired the query returns nullptr ("feature absent").
//
// Data source: the host's per-instance MonitorDataModel — the SAME
// 60-sample ring buffer the Monitor page renders (controller-side
// 2s poll + renderer stats_state events, halves carried forward).
//
// HOST-BOUNDARY NOTE (approved architecture decision, S7): the panel's
// own MonitorPage.qml keeps binding the LIVE session.monitorModel()
// object (QML reads stay on live objects). This family exists for
// PLUGIN consumers that need the stream without a QML context.
// Subscribers build whatever aggregation they need from the pushed
// samples — the host deliberately does NOT hand out chart series
// (QVariantList assembly is the subscriber's own concern).
//
// Threading contract: GUI thread, never blocking (ring-buffer reads +
// signal fanout only). Observer callbacks fire on the GUI thread from
// the sample-arrival path; they must not block (§B.3 — a slow observer
// drags the whole panel event loop).
//
// Observer lifetime: observers are owned by the plugin (the host never
// deletes them) — same contract as IRosterObserver/IInstanceObserver.
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
inline constexpr char kMonitorApiId[] = "pet.monitor";
inline constexpr int kMonitorApiVersion = 1;

// Per-subscriber sample stream. sample() is called on the GUI thread
// for each accepted (throttle-passing) sample of the subscribed
// instance. Exceptions must not escape (the host isolates a throwing
// observer and keeps fanning out, but a plugin should not rely on it).
class IMonitorObserver
{
public:
    virtual ~IMonitorObserver() = default;

    virtual void sample(const QString& uuid, const MonitorSample& s) = 0;
};

class IMonitorApi : public IExtApi
{
public:
    // Full ring-buffer snapshot for one instance (oldest → newest, max
    // 60 samples ≈ 6KB; QVector is implicitly shared, the copy is
    // cheap). Empty when the uuid is unknown or nothing has been
    // sampled yet — never an error (a read family reports absence as
    // emptiness, mirroring IModelApi::availableModels).
    virtual QVector<MonitorSample> history(const QString& uuid) = 0;

    // Push subsequent samples of ONE instance to the observer, merged
    // to at most one push per minIntervalMs (see MonitorSubscription).
    // Returns NotFound for an unknown uuid, Ok otherwise. Subscribing
    // the same (uuid, observer) pair again just updates the interval.
    // The first sample after subscribing always pushes immediately.
    virtual PluginError subscribe(const QString& uuid, IMonitorObserver*,
                                  const MonitorSubscription&) = 0;

    // Stop pushing to this observer for this uuid. No-op when the pair
    // was never subscribed.
    virtual void unsubscribe(const QString& uuid, IMonitorObserver*) = 0;
};

} // namespace pet
