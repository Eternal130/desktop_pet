#pragma once

// SamplePlugin — the living dummy of plugins/sdk-template (P4, §D P4 row).
// Copy this directory to start a new first-party plugin:
//   1. rename the dir + the plugin.json id (reverse domain)
//   2. keep api_version at the host API your tree builds (pet::kApiMajor/Minor)
//   3. implement initialize/shutdown against the pet:: interfaces ONLY
// The CMake pipeline (controller_qt/CMakeLists.txt) discovers every
// plugins/*/plugin.json, builds the target, and wires the factory table.

#include "api/IPanelPlugin.hpp"

class SamplePlugin final : public pet::IPanelPlugin
{
public:
    pet::PluginError initialize(pet::IPluginContext& context) override;
    void shutdown() override;
};
