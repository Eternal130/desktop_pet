#pragma once

// VoicePackController — QML bridge for the voice-pack page (Fluent UI
// redesign Phase 5). Read-only discovery surface over the existing
// VoicePackScanner + MetaMkoParser core utilities:
//
//   Q_INVOKABLE rescan()                 — re-scan the voice-pack dir
//   Q_PROPERTY packCount                 — number of discovered packs
//   Q_INVOKABLE packDirName(i)           — directory name
//   Q_INVOKABLE packDisplayName(i)       — display name from meta.mko
//   Q_INVOKABLE packGroupCount(i)        — behavior groups in the pack
//   Q_INVOKABLE packActionCount(i)       — total voice actions
//   Q_INVOKABLE packGroupNames(i)        — group names (for the mapping
//                                          preview chips)
//   Q_INVOKABLE voicePackDir()           — the scanned directory
//   Q_INVOKABLE packPath(i)              — absolute pack directory
//   Q_INVOKABLE setInstanceVoicePack(instanceUuid, packPathOrEmpty)
//                                        — mount/unmount via InstanceSession
//                                          ("" = unmount); emits mountsChanged
//   Q_INVOKABLE instanceVoicePack(instanceUuid)
//                                        — the instance's mounted pack path
//
// Contract: never throws; all core functions used are noexcept-equivalent
// (return empty optionals/lists on failure).

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "core/VoicePackInfo.hpp"

class InstanceManager;

class VoicePackController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int packCount READ packCount NOTIFY packsChanged)
public:
    explicit VoicePackController(QObject* parent = nullptr);

    int packCount() const { return static_cast<int>(m_packs.size()); }

    Q_INVOKABLE void rescan();
    Q_INVOKABLE QString voicePackDir() const;

    Q_INVOKABLE QString packDirName(int index) const;
    Q_INVOKABLE QString packDisplayName(int index) const;
    Q_INVOKABLE int packGroupCount(int index) const;
    Q_INVOKABLE int packActionCount(int index) const;
    Q_INVOKABLE QStringList packGroupNames(int index) const;
    Q_INVOKABLE QString packPath(int index) const;
    Q_INVOKABLE QString packDisplayNameFor(const QString& packPath) const;

    // Mount wiring (todo 21). The manager is injected post-construction by
    // main.cpp (the controller is created before the InstanceManager there);
    // null manager makes these no-ops returning ""/false.
    void setInstanceManager(InstanceManager* manager) { m_manager = manager; }
    Q_INVOKABLE bool setInstanceVoicePack(const QString& instanceUuid,
                                          const QString& packPath);
    Q_INVOKABLE QString instanceVoicePack(const QString& instanceUuid) const;

signals:
    void packsChanged();
    void mountsChanged();

private:
    QList<core::VoicePackInfo> m_packs;
    QString m_voicePackDir;
    InstanceManager* m_manager = nullptr;
};
