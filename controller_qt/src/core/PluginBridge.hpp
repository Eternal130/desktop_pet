#pragma once

// PluginBridge (P4, §B.5) — the per-plugin facade object exposed to the
// plugin's QML page ("plugin" name in the ideal per-page child context;
// in the stage-1 Loader layout it rides the PluginPageModel's `bridge`
// role through the delegate context — see the known-debt note in
// Main.qml). Small by design: identity + the two cheap UI affordances a
// page needs (log line, bubble). Everything else goes through the plugin
// C++ body via initialize().

#include <QObject>
#include <QString>

namespace core {

class PluginHost;

class PluginBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)

public:
    PluginBridge(const QString& pluginId, const QString& title, QObject* parent);

    QString pluginId() const { return m_pluginId; }
    QString title() const { return m_title; }

    // Fire-and-forget bubble into the panel notification stream.
    Q_INVOKABLE void notify(const QString& text, int durationMs = 0);

    // Host-sink log line (prefixed with the plugin id).
    Q_INVOKABLE void log(const QString& message);

private:
    QString m_pluginId;
    QString m_title;
};

} // namespace core
