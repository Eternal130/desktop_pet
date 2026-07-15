#pragma once

#include <QString>
#include <cstddef>

// Logging infrastructure for controller_qt (task T2).
//
// Single entry point: Logging::init(logDir) boots spdlog with a rotating file
// sink + a color console sink, registers it as the spdlog default logger, and
// installs a qInstallMessageHandler that bridges Qt's qDebug/qInfo/qWarning/
// qCritical AND QML console.* into spdlog with the appropriate level.
//
// After init(), shipped code MUST emit log messages only through the LOG_*
// macros below (or spdlog directly). No std::cout / printf / raw qDebug in any
// production path. The Java control panel baseline this mirrors in spirit is
// docs/system/logging.md (SLF4J + Logback: file rotation, levels, file layout).

namespace Logging {

// Default per-file rotation threshold (5 MiB) and retained file count (5),
// matching the controller_qt log policy. The Java baseline (logging.md) uses
// 10 MiB/5 files for the renderer; controller_qt uses the lighter 5 MiB.
inline constexpr std::size_t kDefaultMaxBytes = 5 * 1024 * 1024;
inline constexpr std::size_t kDefaultMaxFiles = 5;

// Registered spdlog logger name. Tests use spdlog::get/drop with this name.
inline constexpr const char* kLoggerName = "desktop_pet_qt";

// Initialize spdlog: creates a rotating file logger (maxBytes x maxFiles)
// writing to <logDir>/desktop-pet-qt.log plus a color stderr console sink,
// registers it as the default logger, and installs the Qt message handler
// bridge. The directory MUST exist before calling init() — the caller creates
// it (T17 config dir). init() does NOT mkdir.
//
// maxBytes / maxFiles default to the 5 MiB x 5 production policy; tests pass a
// small maxBytes to force rotation deterministically without emitting MiB of
// data.
//
// Returns true on success, false if the file sink could not be created (e.g.
// logDir missing / read-only). On failure the console sink + handler are still
// installed so the app degrades to console-only logging without crashing.
bool init(const QString& logDir,
          std::size_t maxBytes = kDefaultMaxBytes,
          std::size_t maxFiles = kDefaultMaxFiles);

// Flush all sinks and tear down spdlog. Call once before app exit.
void shutdown();

} // namespace Logging

// Convenience macros wrapping the spdlog *active* macros. They route to the
// default logger set up by Logging::init(), i.e. the rotating-file + console
// "desktop_pet_qt" logger. SPDLOG_ACTIVE_LEVEL is forced to SPDLOG_LEVEL_DEBUG
// for the controller_qt + test targets in CMakeLists.txt so LOG_DEBUG is live
// by default; release builds raise it via -DCMAKE_BUILD_TYPE=Release or an
// explicit SPDLOG_ACTIVE_LEVEL override.
#define LOG_DEBUG(fmt, ...) SPDLOG_DEBUG(fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  SPDLOG_INFO(fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  SPDLOG_WARN(fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) SPDLOG_ERROR(fmt, ##__VA_ARGS__)
