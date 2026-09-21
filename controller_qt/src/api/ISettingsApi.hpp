#pragma once

// Plugin SDK — panel settings WRITE family (S6, v1.3).
//
// Post-freeze interface family: derives pet::IExtApi and is obtained
// EXCLUSIVELY through IPluginContext::queryApi(kSettingsApiId,
// minVersion). Gated by the manifest capability "settings_write"
// (kCapabilitySettingsWrite) AND the host-global plugin_write_enabled
// kill-switch — a plugin without the grant (or with the switch off) gets
// a capability stub whose every method returns PluginError::Capability
// (§B.4 loud-failure discipline).
//
// Typed methods ONLY — no QJsonObject "escape hatch": the writable
// surface is an explicit, auditable enum-like list. Writes land on the
// SAME load-modify-save persistence path the panel's own settings page
// uses (SQLite panel_config blob via PanelStateManager — the other
// PanelConfig fields always survive the write).
//
// Deliberately NOT part of this interface: autoLaunchSystem (OS-level
// autostart is a HOST-SHELL capability — registry/.desktop mutation
// stays outside the plugin API by architecture decision), and the
// plugin_write_enabled switch itself (a switch that plugins can flip
// would not be a switch). See docs/refactor revision v3.
//
// Threading contract: GUI thread, non-blocking.
//
// ABI discipline: same rules as every api/ header — pure virtual
// declarations + Qt value types only, no std:: across the boundary, no
// inline function bodies (the defaulted destructor exemption applies).

#include "api/IPluginContext.hpp"
#include "api/PluginTypes.hpp"

namespace pet {

// apiId + per-family version for queryApi(). Version 1 = the four
// whitelisted setters below. minVersion > this → queryApi returns
// nullptr ("feature absent", never an error).
inline constexpr char kSettingsApiId[] = "pet.settings";
inline constexpr int kSettingsApiVersion = 1;

class ISettingsApi : public IExtApi
{
public:
    // Close-button behavior. Legal values are exactly "exit" and
    // "minimize" — anything else returns InvalidArgument (no silent
    // coercion).
    virtual PluginError setCloseAction(const QString& action) = 0;

    // Show the confirm dialog before an exiting close.
    virtual PluginError setConfirmOnExit(bool enabled) = 0;

    // Start the panel minimized to the tray on next launch.
    virtual PluginError setStartMinimized(bool enabled) = 0;

    // The model new instances get by default (a directory name under
    // Resources/Models). Empty name → InvalidArgument (empty means "use
    // the default" at the call sites, it cannot BE the stored value).
    virtual PluginError setDefaultModelName(const QString& name) = 0;
};

} // namespace pet
