#include "SamplePluginFactory.hpp"

#include "SamplePlugin.hpp"

SamplePluginFactory::SamplePluginFactory(QObject* parent)
    : QObject(parent)
{
}

pet::IPanelPlugin* SamplePluginFactory::create()
{
    // Module-side construction — the same body the static variant's
    // free-function factory (pet_plugin_create_sdk_template) uses.
    return new SamplePlugin();
}

void SamplePluginFactory::destroy(pet::IPanelPlugin* plugin)
{
    // Module-side destruction — the ONLY legal delete for an instance that
    // crossed the module boundary (P6a criterion 5: cross-module delete is
    // UB, so the host routes shutdown teardown through this call).
    delete plugin;
}
