#include "SamplePlugin.hpp"

#include <QString>

#include "api/IPluginContext.hpp"
#include "api/IUiApi.hpp"

// The ENTIRE plugin-side surface is the SDK (pet::) — no core includes, no
// Qt widget/gui assumptions, no host internals. That is the §B.3/§B.4
// structural narrowing: this body is small enough to review at a glance.

namespace {
// Factory referenced by the CMake-generated static_plugins.cpp
// (pet_plugin_create_sdk_template). Plain function — no static
// constructors, deterministic link via the factory table.
} // namespace

pet::PluginError SamplePlugin::initialize(pet::IPluginContext& context)
{
    context.log(pet::PluginLogLevel::Info,
                QStringLiteral("sample plugin initializing (config dir: %1)")
                    .arg(context.pluginConfigDir()));

    // Register the nav page. qmlUrl intentionally EMPTY here — the host
    // fills it from the build-injected qrc:/ URL (§B.6 resource layout),
    // so the plugin never hardcodes resource paths.
    pet::PageDescriptor page;
    page.title = QStringLiteral("示例插件");
    page.order = 100;
    const pet::PluginError err = context.uiApi().registerPage(page);
    if (err != pet::PluginError::Ok) {
        context.log(pet::PluginLogLevel::Error,
                    QStringLiteral("registerPage failed: %1").arg(int(err)));
        return err;
    }

    context.log(pet::PluginLogLevel::Info, QStringLiteral("page registered"));
    return pet::PluginError::Ok;
}

void SamplePlugin::shutdown()
{
    // Nothing held — the demo's shutdown is a no-op by design (§B.3 budget
    // discipline: heavy teardown belongs to the host services).
}

// Factory for the generated factory table (id → create()).
pet::IPanelPlugin* pet_plugin_create_sdk_template()
{
    return new SamplePlugin();
}
