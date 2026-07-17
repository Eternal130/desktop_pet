// MountedBehaviorEngineTest — Phase 5 Wave 8 todo 20 TDD for the C++ port of
// Java's MountedBehaviorEngine (246 LOC). Five behaviors locked:
//
//   1. testBuildBehaviorCommand — synthetic VoicePackInfo with one head-tap
//      action (motion + audio) → buildBehaviorCommand returns a play_motion_ext
//      Envelope whose payload has absolute motion_path + audio_path +
//      subtitle_duration > 0 (from the OGG heuristic).
//   2. testHasGroupForAreaFalse — unknown area → false; case-insensitive
//      lookup (HEAD == head).
//   3. testNullVoicePack — default-constructed engine → hasGroupForArea always
//      false, buildBehaviorCommand returns nullopt (the "engine disabled"
//      sentinel that InstanceSession's hit handler relies on for the
//      InteractionHandler fallback).
//   4. testAudioOnly — action with empty motion + non-empty audio →
//      buildAudioOnlyCommand returns a play_audio Envelope.
//   5. testIsIdleMotionPath — an "idle" group action's resolved motion path is
//      recognized as idle; a non-matching path returns false.
//
// The estimateOggDurationMs heuristic has its own test file
// (OggDurationHeuristicTest.cpp) — it's a self-contained static method that
// parses the OGG binary format, distinct from the engine API cluster.

#include "core/MountedBehaviorEngine.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "network/Protocol.hpp"

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

// Minimal blob satisfying the OGG heuristic's two checkpoints: \x01vorbis
// header with sample_rate at marker+12, and a final OggS page with granule
// at +6. Only used here to populate the audio file in buildHeadPack — the
// heuristic ITSELF is tested in OggDurationHeuristicTest.cpp.
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

// Build a VoicePackInfo with one group "head" containing a single action with
// motion + audio + lipSync. The files are written into tempDir.
core::VoicePackInfo buildHeadPack(const QTemporaryDir& tempDir)
{
    core::VoicePackInfo pack;
    pack.dirName = QStringLiteral("test-pack");
    pack.displayName = QStringLiteral("Test Pack");
    pack.code = QStringLiteral("test_pack");
    pack.basePath = tempDir.path();

    {
        QFile f(QDir(tempDir.path()).absoluteFilePath(QStringLiteral("a.ogg")));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(synthesizeOggLikeBlob(44100, 44100));
        }
    }
    {
        QFile f(QDir(tempDir.path()).absoluteFilePath(QStringLiteral("m.json3")));
        if (f.open(QIODevice::WriteOnly)) {
            f.write("{\"Version\":3,\"Meta\":{\"Name\":\"m\"}}");
        }
    }
    {
        QFile f(QDir(tempDir.path()).absoluteFilePath(QStringLiteral("l.json")));
        if (f.open(QIODevice::WriteOnly)) {
            f.write("{}");
        }
    }

    core::VoicePackGroup group;
    group.code = QStringLiteral("head");
    group.name = QStringLiteral("Head");
    group.priority = 3;

    core::VoicePackAction action;
    action.id = 1;
    action.motionPath = QStringLiteral("m.json3");
    action.audioPath = QStringLiteral("a.ogg");
    action.lipSyncPath = QStringLiteral("l.json");
    action.doc = QStringLiteral("Ouch!");
    action.fadeInMs = 200;
    action.fadeOutMs = 300;
    group.actions.append(action);

    pack.groups.insert(QStringLiteral("head"), group);
    return pack;
}

} // namespace

class MountedBehaviorEngineTest : public QObject {
    Q_OBJECT

private slots:

    void testBuildBehaviorCommand()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const auto pack = buildHeadPack(tempDir);
        const core::MountedBehaviorEngine engine(&pack);

        QVERIFY(engine.hasGroupForArea(QStringLiteral("head")));
        QVERIFY(engine.hasGroupForArea(QStringLiteral("HEAD")));

        const auto result = engine.buildBehaviorCommand(QStringLiteral("head"));
        QVERIFY(result.has_value());

        QCOMPARE(result->command.type, QStringLiteral("command"));
        QCOMPARE(result->command.action, QStringLiteral("play_motion_ext"));

        const QString motionPath = result->command.payload.value(
            QStringLiteral("motion_path")).toString();
        QVERIFY(!motionPath.isEmpty());
        QVERIFY(motionPath.endsWith(QStringLiteral("m.json3")));
        QVERIFY(QDir::isAbsolutePath(motionPath));

        const QString audioPath = result->command.payload.value(
            QStringLiteral("audio_path")).toString();
        QVERIFY(audioPath.endsWith(QStringLiteral("a.ogg")));
        QVERIFY(QDir::isAbsolutePath(audioPath));

        const QString lipSyncPath = result->command.payload.value(
            QStringLiteral("lip_sync_path")).toString();
        QVERIFY(lipSyncPath.endsWith(QStringLiteral("l.json")));

        QCOMPARE(result->command.payload.value(
            QStringLiteral("priority")).toInt(), 3);
        QCOMPARE(result->command.payload.value(
            QStringLiteral("fade_in")).toDouble(), 0.2);
        QCOMPARE(result->command.payload.value(
            QStringLiteral("fade_out")).toDouble(), 0.3);

        QCOMPARE(result->command.payload.value(
            QStringLiteral("subtitle_text")).toString(), QStringLiteral("Ouch!"));
        QCOMPARE(result->subtitleText, QStringLiteral("Ouch!"));

        QVERIFY(result->command.payload.contains(QStringLiteral("subtitle_duration")));
        QCOMPARE(result->audioDurationMs, 1000);
    }

    void testHasGroupForAreaFalse()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const auto pack = buildHeadPack(tempDir);
        const core::MountedBehaviorEngine engine(&pack);

        QVERIFY(!engine.hasGroupForArea(QStringLiteral("body")));
        QVERIFY(!engine.hasGroupForArea(QStringLiteral("torso")));
        QVERIFY(!engine.hasGroupForArea(QString()));
        QVERIFY(!engine.buildBehaviorCommand(QStringLiteral("body")).has_value());
    }

    void testNullVoicePack()
    {
        const core::MountedBehaviorEngine engine;

        QVERIFY(!engine.hasGroupForArea(QStringLiteral("head")));
        QVERIFY(!engine.hasGroupForArea(QStringLiteral("body")));
        QVERIFY(!engine.buildBehaviorCommand(QStringLiteral("head")).has_value());
        QVERIFY(!engine.buildAudioOnlyCommand(QStringLiteral("head")).has_value());
        QVERIFY(!engine.isIdleMotionPath(QStringLiteral("C:/anything.json3")));
        QVERIFY(engine.groupNames().isEmpty());
    }

    void testAudioOnly()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        {
            QFile f(QDir(tempDir.path()).absoluteFilePath(QStringLiteral("voice.ogg")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(synthesizeOggLikeBlob(22050, 22050));
        }

        core::VoicePackInfo pack;
        pack.dirName = QStringLiteral("audio-pack");
        pack.displayName = QStringLiteral("Audio");
        pack.code = QStringLiteral("audio_pack");
        pack.basePath = tempDir.path();

        core::VoicePackGroup group;
        group.code = QStringLiteral("body");
        group.name = QStringLiteral("Body");

        core::VoicePackAction motionAction;
        motionAction.motionPath = QStringLiteral("m.json3");
        group.actions.append(motionAction);

        core::VoicePackAction audioOnlyAction;
        audioOnlyAction.audioPath = QStringLiteral("voice.ogg");
        audioOnlyAction.doc = QStringLiteral("Hi there");
        group.actions.append(audioOnlyAction);

        pack.groups.insert(QStringLiteral("body"), group);

        const core::MountedBehaviorEngine engine(&pack);

        const auto motionResult = engine.buildBehaviorCommand(
            QStringLiteral("body"));
        QVERIFY(motionResult.has_value());
        QCOMPARE(motionResult->command.action, QStringLiteral("play_motion_ext"));

        const auto audioResult = engine.buildAudioOnlyCommand(
            QStringLiteral("body"));
        QVERIFY(audioResult.has_value());
        QCOMPARE(audioResult->action, QStringLiteral("play_audio"));

        const QString audioPath = audioResult->payload.value(
            QStringLiteral("audio_path")).toString();
        QVERIFY(audioPath.endsWith(QStringLiteral("voice.ogg")));
        QVERIFY(QDir::isAbsolutePath(audioPath));
        QCOMPARE(audioResult->payload.value(
            QStringLiteral("volume")).toDouble(), 1.0);
    }

    void testIsIdleMotionPath()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        core::VoicePackInfo pack;
        pack.dirName = QStringLiteral("idle-pack");
        pack.displayName = QStringLiteral("Idle");
        pack.code = QStringLiteral("idle_pack");
        pack.basePath = tempDir.path();

        core::VoicePackGroup idleGroup;
        idleGroup.code = QStringLiteral("idle");
        core::VoicePackAction a;
        a.motionPath = QStringLiteral("idle/m01.json3");
        idleGroup.actions.append(a);
        pack.groups.insert(QStringLiteral("idle"), idleGroup);

        const core::MountedBehaviorEngine engine(&pack);

        const QString resolved = QDir(tempDir.path()).absoluteFilePath(
            QStringLiteral("idle/m01.json3"));
        QVERIFY(engine.isIdleMotionPath(resolved));
        QVERIFY(!engine.isIdleMotionPath(
            QStringLiteral("C:/some/other/motion.json3")));
        QVERIFY(!engine.isIdleMotionPath(QString()));
    }
};

QTEST_APPLESS_MAIN(MountedBehaviorEngineTest)
#include "MountedBehaviorEngineTest.moc"
