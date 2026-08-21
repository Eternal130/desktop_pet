#include "logging/Logging.hpp"

#include <QDir>
#include <QMessageLogContext>
#include <QString>
#include <QtLogging> // QtMsgType, qInstallMessageHandler

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace {

// Base log filename under <logDir>. Rotated copies become desktop-pet-qt.log.1,
// .log.2, ... up to .log.<maxFiles-1>.
constexpr const char* kLogFileName = "desktop-pet-qt.log";

// Log line pattern: timestamp, level (colored by spdlog's %^...%$ band), thread
// id, message. Mirrors the Logback-style layout used by the Java baseline.
constexpr const char* kLogPattern =
    "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v";

// Map a QtMsgType to the matching spdlog level. QML console.* is routed here by
// Qt's own runtime: console.log/info -> QtInfoMsg, console.warn -> QtWarningMsg,
// console.error -> QtCriticalMsg, console.debug -> QtDebugMsg. The handler only
// has to translate the enum it receives.
spdlog::level::level_enum qtTypeToLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return spdlog::level::debug;
    case QtInfoMsg:     return spdlog::level::info;
    case QtWarningMsg:  return spdlog::level::warn;
    case QtCriticalMsg: return spdlog::level::err;
    case QtFatalMsg:    return spdlog::level::critical;
    }
    return spdlog::level::off;
}

// qInstallMessageHandler bridge. Qt pre-formats printf-style calls
// (qInfo("hello %d", 42) -> msg "hello 42") before invoking this handler, and
// routes QML console.* here with its own level mapping. We forward each message
// to the "desktop_pet_qt" spdlog logger, prefixing source context (file:line +
// function name) when Qt provides it. Context fields are null in release builds
// compiled with QT_NO_MESSAGELOGCONTEXT, so they are guarded.
void qtMessageHandler(QtMsgType type, const QMessageLogContext& ctx,
                      const QString& msg)
{
    const auto level = qtTypeToLevel(type);

    QString formatted = msg;
    if ((ctx.file != nullptr && ctx.line > 0) || ctx.function != nullptr) {
        QString location;
        if (ctx.file != nullptr && ctx.line > 0) {
            location = QString::fromLatin1(ctx.file) + QLatin1Char(':')
                       + QString::number(ctx.line);
        }
        if (ctx.function != nullptr) {
            if (!location.isEmpty())
                location += QLatin1Char(' ');
            location += QLatin1Char('(')
                        + QString::fromLatin1(ctx.function)
                        + QLatin1Char(')');
        }
        formatted = location + QLatin1Char(' ') + msg;
    }

    auto logger = spdlog::get(Logging::kLoggerName);
    if (!logger)
        logger = spdlog::default_logger();
    if (logger)
        logger->log(level, "{}", formatted.toStdString());
}

} // namespace

namespace Logging {

bool init(const QString& logDir, std::size_t maxBytes, std::size_t maxFiles)
{
    // Drop any prior registration so re-init (e.g. between test slots) does not
    // collide with an existing logger under the same name. spdlog::drop only
    // removes it from the registry; outstanding shared_ptrs keep it alive.
    spdlog::drop(kLoggerName);

    std::vector<spdlog::sink_ptr> sinks;
    bool fileOk = false;

    // Always-on color console sink (stderr). Present even when the file sink
    // fails so logs are still visible during a degraded run.
    auto consoleSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    sinks.push_back(consoleSink);

    // Rotating file sink: <logDir>/desktop-pet-qt.log -> .1 -> .2 -> ...
    // Construction opens the file; a missing/read-only logDir throws spdlog_ex,
    // which we catch to degrade to console-only.
    try {
        const std::string basePath =
            QDir(logDir).absoluteFilePath(QString::fromLatin1(kLogFileName))
                .toStdString();
        sinks.push_back(std::make_shared<
            spdlog::sinks::rotating_file_sink_mt>(basePath, maxBytes, maxFiles));
        fileOk = true;
    } catch (const spdlog::spdlog_ex&) {
        fileOk = false;
        // Defer the diagnostic until after the logger is built, below.
    }

    auto logger = std::make_shared<spdlog::logger>(kLoggerName,
        sinks.begin(), sinks.end());
    logger->set_pattern(kLogPattern);
    // flush_on(info) yields near-immediate file writes for info and above so
    // tests can read a just-logged line without long delays. flush_every is a
    // backstop for debug lines between periodic flushes.
    logger->flush_on(spdlog::level::info);
    logger->set_level(spdlog::level::debug);
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);

    if (!fileOk)
        logger->error("Logging file sink unavailable (logDir=\"{}\"); "
                      "degrading to console-only", logDir.toStdString());

    // Periodic flush backstop for any level not covered by flush_on.
    spdlog::flush_every(std::chrono::seconds(3));

    // Bridge Qt (incl. QML console.*) into spdlog.
    qInstallMessageHandler(qtMessageHandler);

    return fileOk;
}

void shutdown()
{
    // Restore Qt's default message handler BEFORE flushing: objects destroyed
    // later during stack unwind (engine, window) may still emit Qt messages,
    // and the installed handler would route them into spdlog.
    qInstallMessageHandler(nullptr);
    spdlog::default_logger()->flush();
    // Deliberately NO spdlog::shutdown() here: it destroys the registry
    // before main's stack unwind and spdlog's own static destructors run,
    // which crashed the process with 0xC0000005 on every exit. The logger
    // and its sinks are reclaimed by normal static destruction; flush above
    // guarantees the log tail is on disk.
}

} // namespace Logging
