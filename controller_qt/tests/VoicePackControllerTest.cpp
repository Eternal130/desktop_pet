// VoicePackControllerTest (v1.13 resource isolation, rev2 §3.2b) — the
// voice-pack PAGE LIST must keep only HOST-domain packs: plugin-domain
// packs discovered by the scoped scan are filtered out of m_packs (they
// are managed by their owning plugin, never offered for host mounting).
//
// No pre-existing test covered VoicePackController's rescan listing (the
// class was only exercised transitively through app wiring), so this file
// adds the coverage the filter needs.
//
// Hermeticity: rescan() derives its scan roots from
// QCoreApplication::applicationDirPath() (built-in source — a dir that
// never exists next to the test exe) and ConfigDir::userVoicePacksDir() /
// pluginDataRoot() (QStandardPaths AppLocalDataLocation). The CUSTOM main
// below redirects XDG_DATA_HOME at a QTemporaryDir BEFORE any Qt object
// exists, so the user-source and plugin-domain fixtures land inside the
// temp tree instead of the developer's real ~/.local/share. POSIX-only:
// the XDG override is a no-op on Windows, so the slot QSKIPs there (same
// discipline as the fake-renderer harnesses in InstanceSessionTest).
//
// The meta.mko fixtures are synthesized with the same minimal protobuf
// wire-format writer as InstanceSessionTest / MetaMkoParserTest (a Bundle
// with just a Meta message parses into a groupless VoicePackInfo).
//
// QTEST_MAIN-style custom main — QStandardPaths wants a QCoreApplication
// for the application-name segment of AppLocalDataLocation.

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "ui/VoicePackController.hpp"

#include "core/ConfigDir.hpp"

namespace {

// ── Minimal protobuf writer (same approach as MetaMkoParserTest) ─────────
QByteArray encodeVarint(quint64 value)
{
    QByteArray out;
    while (value >= 0x80) {
        out.append(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.append(static_cast<char>(value));
    return out;
}

QByteArray encodeTag(int fieldNumber, int wireType)
{
    return encodeVarint((static_cast<quint64>(fieldNumber) << 3)
                        | static_cast<quint64>(wireType));
}

QByteArray encodeStringField(int fieldNumber, const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return encodeTag(fieldNumber, 2) + encodeVarint(utf8.size()) + utf8;
}

QByteArray encodeMessageField(int fieldNumber, const QByteArray& message)
{
    return encodeTag(fieldNumber, 2) + encodeVarint(message.size()) + message;
}

// A minimal valid Bundle: one Meta (name+code), no groups. parseMetaMko
// accepts it (buildVoicePackInfo needs no groups).
QByteArray buildMinimalBundle(const QString& displayName)
{
    QByteArray meta;
    meta += encodeStringField(1, displayName);
    meta += encodeStringField(2, QStringLiteral("fixture-code"));
    QByteArray bundle;
    bundle += encodeMessageField(1, meta);
    return bundle;
}

// Write <dir>/meta.mko with a minimal valid bundle.
bool writePack(const QString& dir, const QString& displayName)
{
    if (!QDir().mkpath(dir))
        return false;
    QFile f(QDir(dir).absoluteFilePath(QStringLiteral("meta.mko")));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(buildMinimalBundle(displayName));
    f.close();
    return true;
}

} // namespace

class VoicePackControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void testPluginDomainPacksFilteredFromPageList();
};

void VoicePackControllerTest::testPluginDomainPacksFilteredFromPageList()
{
#ifdef Q_OS_WIN
    QSKIP("XDG_DATA_HOME redirection is POSIX-only");
#else
    // Precondition guaranteed by the custom main: ConfigDir::dataDir() is
    // rooted under the redirected XDG_DATA_HOME (<tmp>/<appname>/).
    const QString dataRoot = ConfigDir::dataDir();
    QVERIFY2(dataRoot.contains(QStringLiteral("/desktop-pet/"))
                 || dataRoot.endsWith(QStringLiteral("/desktop-pet")),
             qPrintable(QStringLiteral(
                "dataDir() unexpectedly outside the redirected root: %1")
                    .arg(dataRoot)));

    // One HOST pack (user source) + one PLUGIN-DOMAIN pack, both valid.
    QVERIFY2(writePack(QDir(ConfigDir::userVoicePacksDir())
                           .absoluteFilePath(QStringLiteral("HostPack")),
                       QStringLiteral("Host Fixture")),
             "precondition: failed to write host pack fixture");
    QVERIFY2(writePack(QDir(ConfigDir::pluginVoicePacksDir(
                           QStringLiteral("com.example.pet")))
                           .absoluteFilePath(QStringLiteral("PluginPack")),
                       QStringLiteral("Plugin Fixture")),
             "precondition: failed to write plugin-domain pack fixture");

    // The controller ctor runs rescan() — the scoped scan discovers BOTH,
    // the page list must keep only the HOST one (rev2 §3.2b).
    VoicePackController controller(/*tuningApi=*/nullptr);

    QCOMPARE(controller.packCount(), 1);
    QCOMPARE(controller.packDirName(0), QStringLiteral("HostPack"));
    QCOMPARE(controller.packDisplayName(0), QStringLiteral("Host Fixture"));
    const QString listedPath = controller.packPath(0);
    QVERIFY2(listedPath.endsWith(QStringLiteral("/VoicePacks/HostPack")),
             qPrintable(listedPath));
    QVERIFY2(!listedPath.contains(QStringLiteral("PluginData")),
             "a plugin-domain pack leaked into the page list");

    // The empty-state display dir falls back to the built-in convention
    // when nothing is host-listed: wipe the host pack, rescan, and the
    // dir hint must NOT point into PluginData either.
    QDir(QDir(ConfigDir::userVoicePacksDir())
             .absoluteFilePath(QStringLiteral("HostPack")))
        .removeRecursively();
    controller.rescan();
    QCOMPARE(controller.packCount(), 0); // plugin pack still filtered
    QVERIFY2(!controller.voicePackDir().contains(QStringLiteral("PluginData")),
             qPrintable(controller.voicePackDir()));
#endif
}

// Custom main: redirect XDG_DATA_HOME BEFORE the QCoreApplication exists so
// every QStandardPaths::writableLocation(AppLocalDataLocation) call in this
// binary resolves under the temp tree (QStandardPaths honors XDG_DATA_HOME
// on Linux — ConfigDir.hpp's "Linux/XDG note (m6)").
int main(int argc, char** argv)
{
#ifdef Q_OS_WIN
    QTemporaryDir placeholder; // keep the symbol referenced on WIN builds
    Q_UNUSED(placeholder);
#else
    QTemporaryDir xdgRoot;
    if (!xdgRoot.isValid()) {
        qCritical("VoicePackControllerTest: cannot create XDG_DATA_HOME root");
        return 1;
    }
    qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8().constData());
#endif
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("desktop-pet"));
    QCoreApplication::setOrganizationName(QString()); // ConfigDir contract
    VoicePackControllerTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "VoicePackControllerTest.moc"
