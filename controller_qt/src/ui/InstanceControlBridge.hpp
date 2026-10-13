#pragma once

// InstanceControlBridge (S5 host dogfooding; layout ops since v1.8) —
// the QML write bridge for the instance detail page. Holds the SHARED
// pet::ITuningApi (+ pet::IInstanceControlApi for the lifecycle ops the
// page uses beyond create/delete, + pet::ILayoutApi for the v1.8
// layout/auto-start ops that used to call the session directly) and
// exposes a set of uuid-keyed Q_INVOKABLEs returning the
// pet::PluginError code as a plain int (0 = Ok, 4 = NotFound,
// 5 = Busy, ... — the same surface discipline as rosterModel's S2 write
// bridge). getLayout returns a QML-friendly QVariantMap instead
// ({ok:true,x,y,width,height,scale} / {ok:false,error}).
//
// Dependency discipline: api/ headers + Qt ONLY (no core/ includes) —
// the bridge talks exclusively through the injected interface vtables.
// It holds the concrete implementations DIRECTLY (constructor injection
// from the PanelApplication service tree) — it does NOT go through
// queryApi and is deliberately NOT capability-gated (design decision:
// the host UI is the host; only plugin-side access is gated).
//
// Reads stay on the live InstanceSession object (page `instance`
// property): the architecture boundary is WRITES through the API, reads
// on the live object — read bindings are untouched by this migration.
//
// Thread contract: GUI thread only; every call is a non-blocking
// forward. Null interfaces (degraded wiring / tests with fakes only)
// are tolerated → Generic, never a crash.

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "api/IInstanceControlApi.hpp"
#include "api/ILayoutApi.hpp"
#include "api/ITuningApi.hpp"

class InstanceControlBridge : public QObject
{
    Q_OBJECT
public:
    // All pointers are not owned and must outlive this bridge (the
    // PanelApplication service tree does; null tolerated — every
    // Q_INVOKABLE then returns Generic).
    InstanceControlBridge(pet::ITuningApi* tuningApi,
                          pet::IInstanceControlApi* controlApi,
                          pet::ILayoutApi* layoutApi,
                          QObject* parent = nullptr);

    // ── Tuning writes (pet::ITuningApi forwards) ─────────────────────────
    Q_INVOKABLE int setOpacity(const QString& uuid, double v);
    Q_INVOKABLE int setVolume(const QString& uuid, double v);
    Q_INVOKABLE int setMuted(const QString& uuid, bool muted);
    Q_INVOKABLE int setFps(const QString& uuid, int fps);
    Q_INVOKABLE int playMotion(const QString& uuid, const QString& group,
                               int index);
    Q_INVOKABLE int setExpression(const QString& uuid, const QString& exprId);
    Q_INVOKABLE int triggerHitArea(const QString& uuid, const QString& areaId);
    Q_INVOKABLE int mountVoicePack(const QString& uuid, const QString& packId);
    Q_INVOKABLE int unmountVoicePack(const QString& uuid);

    // ── Lifecycle writes (pet::IInstanceControlApi forwards) ─────────────
    // start/stop/restart/loadModel — the page's remaining lifecycle
    // buttons (create/delete stay on rosterModel, the S2 bridge).
    Q_INVOKABLE int start(const QString& uuid);
    Q_INVOKABLE int stop(const QString& uuid);
    Q_INVOKABLE int restart(const QString& uuid);
    Q_INVOKABLE int loadModel(const QString& uuid, const QString& modelName);

    // ── Layout writes (pet::ILayoutApi forwards, v1.8) ──────────────────
    // The detail page's auto-start toggle + layout-reset button — the
    // two remaining direct session writes, rebound onto the shared API
    // in the same change that introduced the family (the API boundary
    // gate's allowlist entries for them were dropped accordingly).
    Q_INVOKABLE int setAutoStart(const QString& uuid, bool enabled);
    Q_INVOKABLE int resetLayout(const QString& uuid);

    // Config-snapshot layout read as a QML-friendly map:
    //   {ok: true,  x, y, width, height, scale} on success
    //   {ok: false, error: <PluginError int>}    on any failure
    // (the plain-int error contract does not fit a two-value read; the
    // map keeps the call a single Q_INVOKABLE round-trip).
    Q_INVOKABLE QVariantMap getLayout(const QString& uuid);

private:
    pet::ITuningApi* m_tuningApi;            // not owned (service tree / fake)
    pet::IInstanceControlApi* m_controlApi;  // not owned (S2; service tree / fake)
    pet::ILayoutApi* m_layoutApi;            // not owned (v1.8; service tree / fake)
};
