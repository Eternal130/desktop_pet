#pragma once

// Plugin SDK — page + bubble registration (P4, §B.3 + §B.5).
//
// Pages are REGISTERED, never loaded directly by the plugin: the host
// owns the QML engine and instantiates plugin pages itself (Loader-based,
// §B.5) so page routing, theme and lifetime stay host-controlled.
//
// Deliberate YAGNI (v2 flag against P6a re-litigation): there is NO
// unregisterPage and no page lifecycle. Disabling a plugin is a config bit
// + restart (§B.6); hot unload is explicitly forbidden (§A.4).

#include "api/PluginTypes.hpp"

namespace pet {

class IUiApi
{
public:
    virtual ~IUiApi() = default;

    // Register one sidebar page. Only valid during initialize(); later
    // calls are logged and ignored (the nav model is stable after boot).
    // qmlUrl must be a qrc:/ URL baked into the plugin's own QML module
    // (§B.6 resource layout) — file:/ URLs are rejected with
    // PluginError::InvalidArgument (stage 1; revisit at stage 2).
    // order: built-in pages occupy 0-99; plugins use ≥100.
    virtual PluginError registerPage(const PageDescriptor& page) = 0;

    // Push a text bubble into the panel's notification stream (top-right
    // stack, same visual path as voice-pack dialogue). durationMs <= 0
    // uses the stream default. Fire-and-forget; never throws.
    virtual void notifyBubble(const QString& text, int durationMs) = 0;
};

} // namespace pet
