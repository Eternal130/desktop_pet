#include "ui/VoicePackController.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/MetaMkoParser.hpp"
#include "core/VoicePackScanner.hpp"

namespace {
// core::scanAvailableVoicePacks takes the RENDERER/BASE dir and appends
// "Resources/VoicePacks" itself (verified via runtime log — passing the
// full subpath doubled it). Base dir = app dir (build/bin/ hosts both
// exes), same convention ModelScanner uses.
QString rendererBaseDir() {
    return QCoreApplication::applicationDirPath();
}
} // namespace

VoicePackController::VoicePackController(QObject* parent)
    : QObject(parent) {
    rescan();
}

void VoicePackController::rescan() {
    const QStringList dirs = core::scanAvailableVoicePacks(rendererBaseDir());
    // Display dir: the first discovered pack's parent; when nothing is
    // found, show the conventional location for the empty-state hint.
    m_voicePackDir = dirs.isEmpty()
        ? QDir(rendererBaseDir()).absoluteFilePath("Resources/VoicePacks")
        : QFileInfo(dirs.first()).absolutePath();
    m_packs.clear();
    for (const QString& dir : dirs) {
        auto info = core::parseMetaMko(dir);
        if (info) m_packs.push_back(std::move(*info));
    }
    emit packsChanged();
}

QString VoicePackController::voicePackDir() const {
    return m_voicePackDir;
}

QString VoicePackController::packDirName(int index) const {
    if (index < 0 || index >= m_packs.size()) return {};
    return m_packs.at(index).dirName;
}

QString VoicePackController::packDisplayName(int index) const {
    if (index < 0 || index >= m_packs.size()) return {};
    const auto& p = m_packs.at(index);
    return p.displayName.isEmpty() ? p.dirName : p.displayName;
}

int VoicePackController::packGroupCount(int index) const {
    if (index < 0 || index >= m_packs.size()) return 0;
    return static_cast<int>(m_packs.at(index).groups.size());
}

int VoicePackController::packActionCount(int index) const {
    if (index < 0 || index >= m_packs.size()) return 0;
    int n = 0;
    for (const auto& g : m_packs.at(index).groups) {
        n += static_cast<int>(g.actions.size());
    }
    return n;
}

QStringList VoicePackController::packGroupNames(int index) const {
    QStringList out;
    if (index < 0 || index >= m_packs.size()) return out;
    for (const auto& g : m_packs.at(index).groups) {
        out << (g.name.isEmpty() ? g.code : g.name);
    }
    return out;
}

QString VoicePackController::packPath(int index) const {
    if (index < 0 || index >= m_packs.size()) return {};
    return m_packs.at(index).basePath;
}

QString VoicePackController::packDisplayNameFor(const QString& packPath) const {
    for (const auto& p : m_packs) {
        if (p.basePath == packPath) {
            return p.displayName.isEmpty() ? p.dirName : p.displayName;
        }
    }
    return packPath;
}

bool VoicePackController::setInstanceVoicePack(const QString& instanceUuid,
                                               const QString& packPath)
{
    if (m_manager == nullptr) return false;
    // Locate the session by uuid (same scan the manager's delete path uses).
    for (int i = 0; i < m_manager->rowCount(); ++i) {
        InstanceSession* s = m_manager->instanceAt(i);
        if (s == nullptr || s->uuid() != instanceUuid) continue;
        const bool ok = packPath.isEmpty() ? (s->unmountVoicePack(), true)
                                           : s->mountVoicePack(packPath);
        emit mountsChanged();
        return ok;
    }
    return false;
}

QString VoicePackController::instanceVoicePack(
    const QString& instanceUuid) const
{
    if (m_manager == nullptr) return {};
    for (int i = 0; i < m_manager->rowCount(); ++i) {
        InstanceSession* s = m_manager->instanceAt(i);
        if (s != nullptr && s->uuid() == instanceUuid) {
            return s->mountedVoicePack();
        }
    }
    return {};
}
