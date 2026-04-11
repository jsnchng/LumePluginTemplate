#include <3d/implementation_uids.h>  // CORE3D_NS::UID_3D_PLUGIN
#include <core/intf_engine.h>  // IPluginRegister
#include <core/plugin/intf_plugin.h>
#include <core/plugin/intf_plugin_decl.h>
#include <plugintemplate/implementation_uids.h>
#include <plugintemplate/namespace.h>
#include <render/implementation_uids.h>  // RENDER_NS::UID_RENDER_PLUGIN
#include <render/intf_plugin.h>  // IRenderPlugin

#include <iostream>

#include "render/node/render_node_default_material_deferred_shading.h"

PT_BEGIN_NAMESPACE()
const char* GetVersionInfo() { return "GIT_REVISION: cf4cfcb"; }

CORE_NS::PluginToken CreatePluginPT(RENDER_NS::IRenderContext& context)
{
    return &context;
}

void DestroyPluginPT(CORE_NS::PluginToken token)
{
}

static constexpr RENDER_NS::IRenderPlugin RENDER_PLUGIN(CreatePluginPT, DestroyPluginPT);

template<typename RenderType>
constexpr auto FillRenderNodeTypeInfo()
{
    return RENDER_NS::RenderNodeTypeInfo {
        { RENDER_NS::RenderNodeTypeInfo::UID },
        RenderType::UID,
        RenderType::TYPE_NAME,
        RenderType::Create,
        RenderType::Destroy,
        RENDER_NS::IRenderNode::BackendFlagBits::BACKEND_FLAG_BITS_DEFAULT,
        RENDER_NS::IRenderNode::ClassType::CLASS_TYPE_NODE,
        {}, {} };
}

constexpr RENDER_NS::RenderNodeTypeInfo PT_RENDER_NODE_TYPE_INFOS[1] = {
    FillRenderNodeTypeInfo<CORE3D_NS::RenderNodeMyDeferredShading>(),
};

CORE_NS::PluginToken RegisterInterfaces(CORE_NS::IPluginRegister& pluginRegistry)
{
    pluginRegistry.RegisterTypeInfo(RENDER_PLUGIN);
    // Register custom render nodes
    for (const auto& info : PT_RENDER_NODE_TYPE_INFOS) {
        pluginRegistry.RegisterTypeInfo(info);
    }
    return &pluginRegistry;
}

void UnregisterInterfaces(CORE_NS::PluginToken token)
{
    if (!token) {
        return;
    }
    auto* pluginRegistry = static_cast<CORE_NS::IPluginRegister*>(token);
    // Unregister custom render nodes
    for (const auto& info : PT_RENDER_NODE_TYPE_INFOS) {
        pluginRegistry->UnregisterTypeInfo(info);
    }
    pluginRegistry->UnregisterTypeInfo(RENDER_PLUGIN);
}
PT_END_NAMESPACE()

namespace {
constexpr BASE_NS::Uid PLUGIN_DEPENDENCIES[] = { RENDER_NS::UID_RENDER_PLUGIN, CORE3D_NS::UID_3D_PLUGIN };
extern "C" {
PLUGIN_DATA(LumePT) {
    { CORE_NS::IPlugin::UID },
    "Lume Plugin Template",
    { PT_NS::UID_PT_PLUGIN, PT_NS::GetVersionInfo },
    PT_NS::RegisterInterfaces,
    PT_NS::UnregisterInterfaces,
    { PLUGIN_DEPENDENCIES },
};
DEFINE_STATIC_PLUGIN(LumePT);
}
} // namespace
