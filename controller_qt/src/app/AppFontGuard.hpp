#pragma once

// AppFontGuard — the CJK default-font startup guard, extracted from main()
// (P3/M4). App-layer: compiled into the exe target only.

// Installs Noto Sans CJK SC from <appDir>/fonts/ as the application default
// font (original default family kept as fallback). Must run AFTER
// Logging::init and BEFORE the QML engine loads, so every default-fonted
// Text element — including screenshot mode — renders Chinese. Never fatal:
// any failure logs a WARN and keeps the system default (§9.5 never-crash).
void installDefaultFontWithCjk();
