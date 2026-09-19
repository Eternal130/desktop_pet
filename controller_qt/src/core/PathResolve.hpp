#pragma once

#include <QString>
#include <optional>

// Backend-aware renderer path resolution (task T14).
//
// Maps a graphics backend name to the renderer executable filename, with
// platform suffix handling and existence checking. Resolution contract:
//
//   "vulkan" (case-insensitive) -> desktop-pet-renderer-vulkan
//   "opengl", empty, or any other value -> desktop-pet-renderer
//
// On Windows the platform suffix ".exe" is appended
// (docs/controller_qt/architecture-blueprint.md §9.4 — Windows renderer exes carry
// the suffix, Linux renderer binaries have none).
//
// Consumed by:
//   - T13 ProcessManager (launch the resolved renderer exe as a child process)
//   - T16 startup salvo (resolved indirectly through T13)
//   - T27 welcome page environment detection (probe both backends to mark
//     "● Ready / ● Not found" on the OpenGL/Vulkan detection tiles)
//
// rendererDir: absolute or relative path to the directory containing the
//              renderer executables (typically defaultRendererDir()).
// graphicsBackend: "opengl" or "vulkan", case-insensitive. Empty/unknown
//                  values fall back to the OpenGL renderer base name.
//
// Returns the absolute path if the file exists, std::nullopt otherwise
// (a WARN line is emitted via Logging::LOG_WARN on the miss so a misconfigured
// rendererDir or missing Vulkan install is visible in the log without crashing
// the caller).

namespace core {

// Resolve the renderer executable path for the given backend under rendererDir.
// Returns std::nullopt if the resolved file does not exist (with WARN).
std::optional<QString> resolveRendererPath(const QString& rendererDir,
                                           const QString& graphicsBackend = "opengl");

// Convenience: the conventional renderer directory "<app dir>/../build/bin"
// (where the CMake build places the controller_qt exe and the renderer exes
// side-by-side). Uses QCoreApplication::applicationDirPath(); requires a
// QCoreApplication instance to exist (the controller's main.cpp creates one).
//
// Returned verbatim — no existence check — so the caller decides how to react
// to a missing directory.
QString defaultRendererDir();

} // namespace core
