#pragma once

// PluginContextImpl (P4, §B.3) — the host-side pet::IPluginContext handed
// to every plugin's initialize(). One instance per plugin, parented to the
// PluginHost (dies with the service tree, AFTER the QML context — plugin
// pages/bridges never outlive it).
//
// Exception contract (§A.1): the HOST wraps initialize() in try/catch;
// the context methods themselves are also defensive (roster fanout and
// page registration never throw across the boundary).

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include "api/IInstanceApi.hpp"
#include "api/IPluginContext.hpp"
#include "api/IUiApi.hpp"
#include "core/PluginCapabilityStubs.hpp"

// InstanceManager + NotificationStreamController are GLOBAL-namespace
// classes (mixed-namespace codebase); declared outside namespace core.
class InstanceManager;
class NotificationStreamController;

namespace core {

class PluginHost;
class PluginPageModel;

// Roster API: read-only snapshot + pure-virtual observer fanout. Observers
// are notified from InstanceManager model signals (GUI thread — §B.3
// thread contract).
class InstanceApiImpl : public QObject, public pet::IInstanceApi
{
    Q_OBJECT
public:
    InstanceApiImpl(InstanceManager* instanceManager, QObject* parent);

    QVector<pet::InstanceInfo> instances() override;
    void subscribeRoster(pet::IRosterObserver* observer) override;
    void unsubscribeRoster(pet::IRosterObserver* observer) override;

private:
    void fanoutRosterChanged();

    InstanceManager* m_instanceManager; // not owned (service tree)
    QVector<pet::IRosterObserver*> m_observers;
};

// UI API: page registration (boot window enforced by PluginPageModel) +
// bubbles through the panel's NotificationStreamController.
class UiApiImpl : public QObject, public pet::IUiApi
{
    Q_OBJECT
public:
    UiApiImpl(const QString& pluginId, PluginPageModel* pageModel,
              NotificationStreamController* stream, QObject* parent);

    pet::PluginError registerPage(const pet::PageDescriptor& page) override;
    void notifyBubble(const QString& text, int durationMs) override;

private:
    QString m_pluginId;
    PluginPageModel* m_pageModel;                       // not owned
    NotificationStreamController* m_notificationStream; // not owned (may be null)
};

class PluginContextImpl : public QObject, public pet::IPluginContext
{
    Q_OBJECT
public:
    // All pointers are host-owned and outlive the context. configRoot is
    // ConfigDir::configDir(); the per-plugin dir is created on demand.
    PluginContextImpl(const QString& pluginId, InstanceManager* instanceManager,
                      PluginPageModel* pageModel,
                      NotificationStreamController* stream,
                      const QString& configRoot, QObject* parent);

    // pet::IPluginContext
    pet::IInstanceApi& instanceApi() override { return m_instanceApi; }
    pet::IVoicePackApi& voicePackApi() override { return m_voicePackApi; }
    pet::IUiApi& uiApi() override { return m_uiApi; }
    pet::IDownloadApi& downloadApi() override { return m_downloadApi; }
    QString pluginConfigDir() override;
    void log(pet::PluginLogLevel level, const QString& message) override;

private:
    QString m_pluginId;
    QString m_configRoot;
    InstanceApiImpl m_instanceApi;
    UiApiImpl m_uiApi;
    VoicePackApiImpl m_voicePackApi;
    DownloadApiStub m_downloadApi; // P5: capability-gated real service
};

} // namespace core
