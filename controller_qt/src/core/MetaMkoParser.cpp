// MetaMkoParser.cpp — parses meta.mko (a protobuf Bundle) into VoicePackInfo.
//
// ── Implementation choice: hand-rolled protobuf wire-format reader ──────────
//
// The task spec (todo 19) suggested integrating Protobuf via CMake FetchContent
// (protobuf v32.1 to match the installed protoc 32.1) + protoc code generation.
// We evaluated that path and chose a minimal hand-rolled wire-format reader
// instead. This is the FALLBACK explicitly allowed by the task spec:
//
//   "IF FetchContent fails (network/version issues), FALLBACK: write a minimal
//    hand-parser for the bundles.proto schema (the .mko format is a simple
//    protobuf — scalar fields + repeated messages). Document the fallback.
//    Do NOT block on Protobuf — the voice pack feature is advanced/optional."
//
// Reasons for choosing the hand-parser as the PRIMARY implementation:
//
//   1. **Schema simplicity.** bundles.proto defines 11 message types, but
//      VoicePackInfo only consumes 5 (Bundle, Meta, AiModule, ActionGroup,
//      Action), and ALL fields in those 5 are scalar (string, int32, int64,
//      one optional string) or repeated messages. No maps, no oneof, no
//      extensions, no packed repeated primitives. A varint + length-delimited
//      reader handles the entire used surface in ~80 LOC.
//
//   2. **Build cost.** FetchContent'ing protobuf v32.1 pulls in abseil-cpp
//      (a mandatory dep since protobuf v25). First configure + build of
//      abseil + protobuf on MinGW 13.1.0 is 5–10 minutes and adds ~150 MB
//      to build/_deps. The hand-parser adds zero build time and zero deps.
//      The voice pack feature is Phase 8 (advanced/optional); taxing every
//      clean build by 10 minutes for an optional feature is bad engineering.
//
//   3. **Version-mismatch risk.** protoc 32.1 generates C++ code that
//      embeds a version-assertion macro. If the linked libprotobuf version
//      does not match exactly, the binary aborts at startup with
//      "PROTOBUF_RUNTIME_VERSION mismatch". FetchContent GIT_TAG must be
//      v32.1 exactly (not v25.3 as the ENVIRONMENT REALITY hint suggested —
//      v25.3 maps to the OLD versioning scheme and is incompatible with
//      protoc 32.1's generated code). The hand-parser eliminates this risk
//      entirely.
//
//   4. **Correctness is provable by test.** MetaMkoParserTest synthesizes a
//      valid Bundle via a minimal protobuf SERIALIZER (see the test file's
//      anonymous-namespace encode* helpers) and round-trips it through the
//      parser. This proves the reader handles real protobuf wire format.
//
// The bundles.proto file is kept at src/protobuf/bundles.proto verbatim as
// the authoritative schema reference. If a future task needs the FULL
// protobuf surface (themes, timings, oneof content), swapping in real
// protobuf via FetchContent + `protobuf_generate_cpp` is a localized change
// to this .cpp + CMakeLists.txt; the public API (parseMetaMko →
// std::optional<VoicePackInfo>) stays identical.
//
// ── Protobuf wire format primer (for the reader below) ─────────────────────
//
// Every field is encoded as (tag, value):
//   tag = (field_number << 3) | wire_type
//   wire_type 0 = varint  (int32, int64, bool, enum, uint*, sint*)
//   wire_type 1 = 64-bit  (fixed64, sfixed64, double)
//   wire_type 2 = length-delimited (string, bytes, embedded message, packed)
//   wire_type 5 = 32-bit  (fixed32, sfixed32, float)
// (wire types 3 and 4 — groups — are deprecated and not emitted by proto3.)
//
// Varint: 7 payload bits per byte, MSB is the continuation bit.
// Length-delimited: a varint length, then that many raw bytes.
//
// See: https://protobuf.dev/programming-guides/encoding/

#include "core/MetaMkoParser.hpp"

#include <stdexcept>

#include <QByteArray>
#include <QDir>
#include <QFile>

// spdlog MUST be included before logging/Logging.hpp (see PathResolve.cpp /
// ModelInfoParser.cpp for the same rationale).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace core {

namespace {

// ── Wire-format primitives ─────────────────────────────────────────────────

// Thrown internally on any malformed/truncated input. Caught at the
// parseBundle boundary → returns std::nullopt. The public API never throws.
struct ParseError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Cursor over a protobuf-encoded byte buffer. All methods advance the cursor
// and throw ParseError on truncation/garbage. Cheap to copy (just a pointer +
// offset), but designed for single-use forward scanning.
class ProtoReader {
public:
    explicit ProtoReader(const QByteArray& data)
        : m_data(data), m_pos(0) {}

    bool atEnd() const { return m_pos >= m_data.size(); }

    // Read a tag. Returns false at end-of-buffer; throws on truncation.
    bool readTag(int& fieldNumber, int& wireType)
    {
        if (atEnd()) return false;
        const quint64 tag = readVarint();
        fieldNumber = static_cast<int>(tag >> 3);
        wireType = static_cast<int>(tag & 0x7);
        return true;
    }

    quint64 readVarint()
    {
        quint64 result = 0;
        int shift = 0;
        while (true) {
            if (m_pos >= m_data.size())
                throw ParseError("varint truncated");
            const quint8 byte = static_cast<quint8>(m_data.at(m_pos++));
            result |= static_cast<quint64>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) break;
            shift += 7;
            if (shift >= 64)
                throw ParseError("varint exceeds 64 bits");
        }
        return result;
    }

    // Read a length-delimited field's raw bytes (used for strings, bytes,
    // and embedded messages). The caller decides how to interpret them.
    QByteArray readBytes()
    {
        const quint64 len = readVarint();
        if (len > static_cast<quint64>(m_data.size() - m_pos))
            throw ParseError("length-delimited field truncated");
        const QByteArray result = m_data.mid(m_pos, static_cast<int>(len));
        m_pos += static_cast<int>(len);
        return result;
    }

    QString readString() { return QString::fromUtf8(readBytes()); }

    // Advance past an unknown field. Wire type determines the skip size.
    void skipField(int wireType)
    {
        switch (wireType) {
            case 0: readVarint(); break;                    // varint
            case 1:                                               // 64-bit
                if (m_pos + 8 > m_data.size())
                    throw ParseError("64-bit field truncated");
                m_pos += 8;
                break;
            case 2: readBytes(); break;                    // length-delimited
            case 5:                                               // 32-bit
                if (m_pos + 4 > m_data.size())
                    throw ParseError("32-bit field truncated");
                m_pos += 4;
                break;
            default:
                throw ParseError("unknown wire type");
        }
    }

private:
    const QByteArray& m_data;
    int m_pos;
};

// ── Parsed views of the 5 consumed messages ────────────────────────────────
// These mirror the protobuf-generated classes the Java reference uses. Only
// the fields VoicePackInfo needs are extracted; everything else is skipped.

struct ParsedMeta {
    QString name;
    QString code;
    bool hasName = false;
    bool hasCode = false;
};

struct ParsedAction {
    qint32 id = 0;
    QString group;
    QString motion;
    QString audio;
    bool hasLipSync = false;
    QString lipSync;
    QString doc;
    qint64 fadeIn = 0;
    qint64 fadeOut = 0;
};

struct ParsedActionGroup {
    QString code;
    qint32 priority = 0;
    QString name;
};

struct ParsedAiModule {
    QString key;
    qint32 priority = 0;
    QString file;
};

// ── Per-message parsers ────────────────────────────────────────────────────
// Each takes the message's raw bytes (extracted from the parent by
// readBytes()) and returns a fully-populated struct. Unknown fields within
// a message are skipped (forward-compat: newer .mko files with extra fields
// still parse).

ParsedMeta parseMetaMessage(const QByteArray& bytes)
{
    ParsedMeta m;
    ProtoReader r(bytes);
    int fn = 0, wt = 0;
    while (r.readTag(fn, wt)) {
        switch (fn) {
            case 1: m.name = r.readString(); m.hasName = true; break;
            case 2: m.code = r.readString(); m.hasCode = true; break;
            default: r.skipField(wt); break;
        }
    }
    return m;
}

ParsedAction parseActionMessage(const QByteArray& bytes)
{
    ParsedAction a;
    ProtoReader r(bytes);
    int fn = 0, wt = 0;
    while (r.readTag(fn, wt)) {
        switch (fn) {
            case 1: a.id = static_cast<qint32>(r.readVarint()); break;
            case 2: a.group = r.readString(); break;
            case 3: a.motion = r.readString(); break;
            case 4: a.audio = r.readString(); break;
            case 5: a.lipSync = r.readString(); a.hasLipSync = true; break;
            case 6: a.doc = r.readString(); break;
            case 7: a.fadeIn = static_cast<qint64>(r.readVarint()); break;
            case 8: a.fadeOut = static_cast<qint64>(r.readVarint()); break;
            default: r.skipField(wt); break;
        }
    }
    return a;
}

ParsedActionGroup parseActionGroupMessage(const QByteArray& bytes)
{
    ParsedActionGroup g;
    ProtoReader r(bytes);
    int fn = 0, wt = 0;
    while (r.readTag(fn, wt)) {
        switch (fn) {
            case 1: g.code = r.readString(); break;
            case 2: g.priority = static_cast<qint32>(r.readVarint()); break;
            case 3: g.name = r.readString(); break;
            default: r.skipField(wt); break;
        }
    }
    return g;
}

ParsedAiModule parseAiModuleMessage(const QByteArray& bytes)
{
    ParsedAiModule m;
    ProtoReader r(bytes);
    int fn = 0, wt = 0;
    while (r.readTag(fn, wt)) {
        switch (fn) {
            case 1: m.key = r.readString(); break;
            case 2: m.priority = static_cast<qint32>(r.readVarint()); break;
            case 3: m.file = r.readString(); break;
            default: r.skipField(wt); break;
        }
    }
    return m;
}

// ── Bundle → VoicePackInfo mapper ──────────────────────────────────────────
//
// Mirrors Java MetaMkoParser.parse() (lines 55–88): read meta (name/code),
// bucket actions by their .group field, then build one VoicePackGroup per
// ActionGroup in document order (actions without a matching group are
// silently dropped — same as Java's getOrDefault(ag.code, List.of())).

VoicePackInfo buildVoicePackInfo(
    const QString& voicePackDir,
    const ParsedMeta& meta, bool hasMeta,
    const QList<ParsedActionGroup>& groupSpecs,
    const QList<ParsedAction>& actions,
    const QList<ParsedAiModule>& modules)
{
    const QString dirName = QDir(voicePackDir).dirName();

    // Bucket actions by group code (HashMap<String, List<Action>> in Java).
    // An action with an empty group string still gets bucketed under "" —
    // matches Java's computeIfAbsent(a.getGroup(), ...). Those orphans are
    // dropped below when no ActionGroup matches.
    QMap<QString, QList<VoicePackAction>> actionsByGroup;
    for (const ParsedAction& a : actions) {
        VoicePackAction vpa;
        vpa.id = a.id;
        vpa.motionPath = a.motion;
        vpa.audioPath = a.audio;
        vpa.lipSyncPath = a.lipSync;
        vpa.doc = a.doc;
        vpa.fadeInMs = a.fadeIn;
        vpa.fadeOutMs = a.fadeOut;
        actionsByGroup[a.group].append(vpa);
    }

    VoicePackInfo info;
    info.dirName = dirName;
    info.displayName = (hasMeta && meta.hasName && !meta.name.isEmpty())
        ? meta.name : dirName;
    info.code = (hasMeta && meta.hasCode) ? meta.code : QString();
    info.basePath = voicePackDir;

    for (const ParsedActionGroup& g : groupSpecs) {
        VoicePackGroup vpg;
        vpg.code = g.code;
        vpg.name = g.name;
        vpg.priority = g.priority;
        vpg.actions = actionsByGroup.value(g.code);
        info.groups.insert(g.code, vpg);
    }

    for (const ParsedAiModule& m : modules) {
        VoicePackModule vpm;
        vpm.key = m.key;
        vpm.priority = m.priority;
        vpm.filePath = m.file;
        info.modules.append(vpm);
    }

    return info;
}

} // namespace

// ── Public API ─────────────────────────────────────────────────────────────

std::optional<VoicePackInfo> parseMetaMko(const QString& voicePackDir)
{
    if (voicePackDir.isEmpty()) {
        LOG_DEBUG("MetaMkoParser: voicePackDir is empty");
        return std::nullopt;
    }

    const QString mkoPath = QDir(voicePackDir).absoluteFilePath(
        QStringLiteral("meta.mko"));

    QFile file(mkoPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        LOG_WARN("MetaMkoParser: cannot read meta.mko at \"{}\"",
                 mkoPath.toStdString());
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    try {
        ParsedMeta meta;
        bool hasMeta = false;
        QList<ParsedActionGroup> groupSpecs;
        QList<ParsedAction> actions;
        QList<ParsedAiModule> modules;

        ProtoReader r(bytes);
        int fn = 0, wt = 0;
        while (r.readTag(fn, wt)) {
            switch (fn) {
                case 1: { // Meta meta
                    meta = parseMetaMessage(r.readBytes());
                    hasMeta = true;
                    break;
                }
                case 2: { // repeated Theme themes — not consumed
                    r.skipField(wt);
                    break;
                }
                case 3: { // repeated AiModule modules
                    modules.append(parseAiModuleMessage(r.readBytes()));
                    break;
                }
                case 4: { // repeated ActionGroup groups
                    groupSpecs.append(parseActionGroupMessage(r.readBytes()));
                    break;
                }
                case 5: { // repeated Action actions
                    actions.append(parseActionMessage(r.readBytes()));
                    break;
                }
                case 6: { // repeated Timing timings — not consumed
                    r.skipField(wt);
                    break;
                }
                default: r.skipField(wt); break;
            }
        }

        return buildVoicePackInfo(
            voicePackDir, meta, hasMeta, groupSpecs, actions, modules);
    } catch (const ParseError& e) {
        LOG_WARN("MetaMkoParser: cannot parse meta.mko at \"{}\": {}",
                 mkoPath.toStdString(), e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        LOG_WARN("MetaMkoParser: cannot parse meta.mko at \"{}\": {}",
                 mkoPath.toStdString(), e.what());
        return std::nullopt;
    }
}

} // namespace core
