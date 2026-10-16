#pragma once

// SamplePluginFactory — the stage-2 DYNAMIC variant's root object (2026-10-16,
// SDK v1.12). One instance lives inside the plugin module; the host's
// DynamicPluginSource validates the module metadata (iid / api_version / id
// uniqueness / abi) BEFORE instance(), then casts the root here. Plugin
// instances are constructed AND destroyed through this factory so both happen
// inside the module — the host never deletes across the module boundary
// (P6a criterion 5).
//
// Q_PLUGIN_METADATA embeds the module's manifest as the metaData
// "metadata" content — the manifest the host validates. FILE points at
// the CONFIGURED copy <binary-dir>/plugin_dyn.json (generated from
// plugin.json.in with THIS build's §A.2 abi block injected — see the
// template CMakeLists): moc resolves it via the target's include path,
// because the name deliberately does NOT exist next to this header (the
// checked-in plugin.json is the STATIC variant's abi-less manifest).
// The IID string comes from PET_PLUGIN_FACTORY_IID (api/IPluginFactory.hpp
// — the single source of the spelling).
//
// This file compiles ONLY into pet_plugin_sdk_template_dyn
// (PET_BUILD_SDK_TEMPLATE_DYNAMIC=ON); the static variant keeps using the
// free-function factory at the bottom of SamplePlugin.cpp.

#include <QObject>

#include "api/IPluginFactory.hpp"

class SamplePluginFactory : public QObject, public pet::IPluginFactory
{
    Q_OBJECT
    Q_INTERFACES(pet::IPluginFactory)
    Q_PLUGIN_METADATA(IID PET_PLUGIN_FACTORY_IID FILE "plugin_dyn.json")

public:
    explicit SamplePluginFactory(QObject* parent = nullptr);

    pet::IPanelPlugin* create() override;
    void destroy(pet::IPanelPlugin* plugin) override;
};
