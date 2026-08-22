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
//
// Mount/unmount to InstanceSession is NOT wired here — todo 21 (populating
// MountedBehaviorEngine per instance) remains a core-side gap; the QML
// matrix shows the available packs but marks mounting as pending wiring.
//
// Contract: never throws; all core functions used are noexcept-equivalent
// (return empty optionals/lists on failure).

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "core/VoicePackInfo.hpp"

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

signals:
    void packsChanged();

private:
    QList<core::VoicePackInfo> m_packs;
    QString m_voicePackDir;
};
