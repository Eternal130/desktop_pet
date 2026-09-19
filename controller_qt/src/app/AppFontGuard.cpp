#include "app/AppFontGuard.hpp"

#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStringList>

#include <spdlog/spdlog.h>

#include "logging/Logging.hpp"

// Moved verbatim from main() (P3/M4). Call site order is unchanged:
// after Logging::init, before the QML engine loads.

// ── CJK default-font guard (tofu fix) ─────────────────────────────────────
// Hosts without a system CJK font (fc-list :lang=zh empty — the dev machine
// that reported the bug) render every Chinese glyph as tofu; English text
// works because the Latin fallback resolves. Ship Noto Sans CJK SC next to
// the exe (<appDir>/fonts/, deployed by the controller's own POST_BUILD in
// CMakeLists.txt) and make it the application default with the previous
// default family kept as fallback (Latin glyphs still resolve there first
// if it ranked higher). Never fatal — any failure logs a WARN and keeps the
// system default (§9.5 never-crash contract). Called before engine.load so
// screenshot mode benefits identically.
void installDefaultFontWithCjk()
{
    const QString fontPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/fonts/NotoSansCJKsc-Regular.otf");

    // Qt 6: QFontDatabase is a static-only API surface.
    const int fontId = QFontDatabase::addApplicationFont(fontPath);
    if (fontId < 0) {
        LOG_WARN("main: CJK font not loaded from '{}' — Chinese text may "
                 "render as tofu on hosts without a system CJK font",
                 fontPath.toStdString());
        return;
    }
    const QStringList families =
        QFontDatabase::applicationFontFamilies(fontId);
    if (families.isEmpty()) {
        LOG_WARN("main: CJK font '{}' loaded but exposed no family — "
                 "keeping system default", fontPath.toStdString());
        return;
    }

    // Copy the current default (keeps point size / style hints) and prepend
    // the CJK family: ["Noto Sans CJK SC", <original default>].
    QFont font = QGuiApplication::font();
    QStringList familiesChain{families.first()};
    const QString originalFamily = font.family();
    if (!originalFamily.isEmpty() && !familiesChain.contains(originalFamily))
        familiesChain.append(originalFamily);
    font.setFamilies(familiesChain);
    QGuiApplication::setFont(font);
    LOG_INFO("main: CJK default font installed ('{}' -> family '{}') — "
             "tofu guard active",
             fontPath.toStdString(), families.first().toStdString());
}
