// OggDurationHeuristicTest — Phase 5 Wave 8 todo 20 TDD for the
// MountedBehaviorEngine::estimateOggDurationMs static method. Three sub-cases
// lock the contract:
//
//   - 1s/2s synthesized OGG → exact duration = granule / sample_rate * 1000.
//   - non-OGG content → fallback kFallbackDurationMs (NOT 0, NOT throw).
//   - missing / too-small file → fallback kFallbackDurationMs.
//
// The heuristic parses the OGG container directly (no libvorbis dep): finds
// the \x01vorbis codec-id header, reads sample_rate (uint32 LE at +12); scans
// the last 64KB for the final OggS page, reads granule_position (int64 LE at
// +6); duration_ms = granule / sample_rate * 1000. We synthesize a minimal
// "OGG-shaped" blob that satisfies these two checkpoints without requiring a
// real Vorbis encoder (no .ogg fixtures ship at test time).

#include "core/MountedBehaviorEngine.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

void appendLeU32(QByteArray& out, quint32 value)
{
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 24) & 0xFF));
}

void appendLeU64(QByteArray& out, quint64 value)
{
    for (int i = 0; i < 8; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

// Minimal blob satisfying the heuristic's two checkpoints: \x01vorbis header
// with sample_rate at marker+12, and a final OggS page with granule at +6.
QByteArray synthesizeOggLikeBlob(quint32 sampleRate, quint64 granulePos)
{
    QByteArray out;
    out.append("OggS");
    out.append(8, '\0');
    out.append(4, '\0');
    out.append(4, '\0');
    out.append(4, '\0');
    out.append(static_cast<char>(1));
    out.append(static_cast<char>(30));
    out.append(static_cast<char>(0x01));
    out.append("vorbis");
    appendLeU32(out, 0);
    out.append(static_cast<char>(1));
    appendLeU32(out, sampleRate);
    out.append(12, '\0');
    out.append(64, '\0');
    out.append("OggS");
    out.append(static_cast<char>(0));
    out.append(static_cast<char>(0x04));
    appendLeU64(out, granulePos);
    out.append(4, '\0');
    out.append(4, '\0');
    out.append(4, '\0');
    out.append(static_cast<char>(0));
    return out;
}

} // namespace

class OggDurationHeuristicTest : public QObject {
    Q_OBJECT

private slots:

    void testExactDurationFromSynthesizedOgg()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // 1 second at 44100 Hz: granule = 44100.
        {
            QFile f(QDir(tempDir.path()).absoluteFilePath(QStringLiteral("1s.ogg")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(synthesizeOggLikeBlob(44100, 44100));
            f.close();
            const qint64 ms = core::MountedBehaviorEngine::estimateOggDurationMs(
                QDir(tempDir.path()).absoluteFilePath(QStringLiteral("1s.ogg")));
            QCOMPARE(ms, 1000);
        }
        // 2 seconds at 48000 Hz: granule = 96000.
        {
            QFile f(QDir(tempDir.path()).absoluteFilePath(QStringLiteral("2s.ogg")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(synthesizeOggLikeBlob(48000, 96000));
            f.close();
            const qint64 ms = core::MountedBehaviorEngine::estimateOggDurationMs(
                QDir(tempDir.path()).absoluteFilePath(QStringLiteral("2s.ogg")));
            QCOMPARE(ms, 2000);
        }
    }

    void testFallbackOnNonOggContent()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Non-OGG content padded past 85 bytes → hits "no \\x01vorbis" branch.
        QFile f(QDir(tempDir.path()).absoluteFilePath(QStringLiteral("not.ogg")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("This is definitely not an OGG file.");
        f.write(QByteArray(200, '\0'));
        f.close();
        const qint64 ms = core::MountedBehaviorEngine::estimateOggDurationMs(
            QDir(tempDir.path()).absoluteFilePath(QStringLiteral("not.ogg")));
        QCOMPARE(ms, core::MountedBehaviorEngine::kFallbackDurationMs);
    }

    void testFallbackOnMissingOrTinyFile()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Missing file → fallback.
        {
            const qint64 ms = core::MountedBehaviorEngine::estimateOggDurationMs(
                QDir(tempDir.path()).absoluteFilePath(QStringLiteral("nope.ogg")));
            QCOMPARE(ms, core::MountedBehaviorEngine::kFallbackDurationMs);
        }
        // Too-small file (< 85 bytes) → fallback.
        {
            QFile f(QDir(tempDir.path()).absoluteFilePath(QStringLiteral("tiny.ogg")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("Og");
            f.close();
            const qint64 ms = core::MountedBehaviorEngine::estimateOggDurationMs(
                QDir(tempDir.path()).absoluteFilePath(QStringLiteral("tiny.ogg")));
            QCOMPARE(ms, core::MountedBehaviorEngine::kFallbackDurationMs);
        }
    }
};

QTEST_APPLESS_MAIN(OggDurationHeuristicTest)
#include "OggDurationHeuristicTest.moc"
