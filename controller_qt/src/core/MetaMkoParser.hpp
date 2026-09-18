#pragma once

#include <QString>
#include <optional>

#include "core/VoicePackInfo.hpp"

// MetaMkoParser (Phase 5 Wave 8 todo 19) — parses the meta.mko file inside a
// voice-pack directory into a VoicePackInfo.
//
// The .mko format is a protobuf Bundle message (see src/protobuf/bundles.proto).
// The original Java code generates C++ bindings via protoc + links
// libprotobuf; the Qt port uses a minimal hand-rolled protobuf wire-format
// reader instead — see MetaMkoParser.cpp's file-level comment for the
// rationale (short version: the schema is 5 messages of scalar fields, a
// ~150 LOC reader is cleaner than FetchContent'ing protobuf+abseil for an
// optional Phase 8 feature, and the task spec explicitly allows this
// fallback). The bundles.proto file is kept verbatim as the schema reference
// and for future protobuf integration.
//
// Contract: NEVER throws. On any parse failure (missing file, unreadable,
// truncated, malformed wire format), returns std::nullopt + emits a WARN log.
// This matches the Java reference's null-return-on-failure contract.
//
// Consumed by:
//   - todo 20 (MountedBehaviorEngine): builds play_motion_ext commands from
//     the parsed groups/actions when a hit event arrives.

namespace core {

// Parse <voicePackDir>/meta.mko into a VoicePackInfo.
//
// Returns std::nullopt when:
//   - voicePackDir is empty
//   - <voicePackDir>/meta.mko does not exist or cannot be read
//   - the file's bytes are not valid protobuf wire format for a Bundle
//
// Never throws — all I/O and parse errors are caught and logged as WARN.
// The caller renders "voice pack unavailable" rather than crashing.
std::optional<VoicePackInfo> parseMetaMko(const QString& voicePackDir);

} // namespace core
