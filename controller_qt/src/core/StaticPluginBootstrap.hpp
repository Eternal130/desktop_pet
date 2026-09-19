#pragma once

// Static plugin bootstrap seam (P4, §B.6 two-phase loading).
//
// The DEFINITION lives in the CMake-generated static_plugins.cpp (compiled
// into the desktop-pet-controller-qt target, which links every plugin
// STATIC library). Core cannot host the factory table itself: pet_panel_core
// is linked BY plugins, so core referencing plugin symbols would be
// circular. The generated TU sits on the exe side and pushes rows into the
// registry through this free function.
//
// Zero plugins → the generated TU defines an empty function. Exactly one
// definition exists per build (the exe is a single TU set).

namespace core {
class PluginRegistry;
}

// Registers every plugins/*/ manifest + factory into `registry` in one
// call. Called from main() after PanelApplication construction, before
// PluginHost::initializeAll().
void pet_register_static_plugins(core::PluginRegistry& registry);
