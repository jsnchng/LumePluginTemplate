#include <3d/implementation_uids.h>  // CORE3D_NS::UID_3D_PLUGIN
#include <core/intf_engine.h>  // IPluginRegister
#include <core/io/intf_file_manager.h>
#include <core/plugin/intf_plugin.h>
#include <core/plugin/intf_plugin_decl.h>
#include <plugintemplate/implementation_uids.h>
#include <plugintemplate/namespace.h>
#include <render/device/intf_shader_manager.h>
#include <render/implementation_uids.h>  // RENDER_NS::UID_RENDER_PLUGIN
#include <render/intf_plugin.h>  // IRenderPlugin
#include <render/intf_render_context.h>

#include <iostream>

#include <plugintemplate/camera_control_system.h>
#include "render/node/render_node_sr_training.h"
#include "render/node/render_node_sr_downsample_init.h"
#include "render/node/render_node_sr_clear_gradient.h"
#include "render/node/render_node_mask_ubo.h"

PT_BEGIN_NAMESPACE()
const char* GetVersionInfo() { return "GIT_REVISION: cf4cfcb"; }

extern "C" const void* const PT_BINARY[];
extern "C" const uint64_t PT_BIN_SIZE;

CORE_NS::PluginToken CreatePluginPT(RENDER_NS::IRenderContext& context)
{
    std::cout << "CORE_NS::PluginToken CreatePluginPT" << std::endl;
    CORE_NS::IFileManager& fileManager = context.GetEngine().GetFileManager();

    auto rofs = fileManager.CreateROFilesystem(PT_BINARY, PT_BIN_SIZE);

    fileManager.RegisterFilesystem("pt", move(rofs));

    // fileManager.RegisterPath("pt", "assets://pt/", false);
    {
        RENDER_NS::IShaderManager::ShaderFilePathDesc desc;
        desc.shaderPath = "pt://shaders/";
        desc.pipelineLayoutPath = "pt://pipelinelayouts/";
        context.GetDevice().GetShaderManager().LoadShaderFiles(desc);
    }
    return &context;
}

void DestroyPluginPT(CORE_NS::PluginToken token)
{
    RENDER_NS::IRenderContext* context = static_cast<RENDER_NS::IRenderContext*>(token);
    CORE_NS::IFileManager& fileManager = context->GetEngine().GetFileManager();
    fileManager.UnregisterPath("pt", "assets://pt/");
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

constexpr RENDER_NS::RenderNodeTypeInfo PT_RENDER_NODE_TYPE_INFOS[4] = {
    FillRenderNodeTypeInfo<RENDER_NS::RenderNodeSRTraining>(),
    FillRenderNodeTypeInfo<RENDER_NS::RenderNodeSRDownsampleInit>(),
    FillRenderNodeTypeInfo<RENDER_NS::RenderNodeSRClearGradient>(),
    FillRenderNodeTypeInfo<RENDER_NS::RenderNodeMaskUbo>(),
};

// System type info for CameraControlSystem
constexpr CORE_NS::SystemTypeInfo CAMERA_CONTROL_SYSTEM_TYPE_INFO {
    { CORE_NS::SystemTypeInfo::UID },
    CameraControlSystem::UID,
    CameraControlSystem::TYPE_NAME,
    CameraControlSystem::Create,
    CameraControlSystem::Destroy,
};

CORE_NS::PluginToken RegisterInterfaces(CORE_NS::IPluginRegister& pluginRegistry)
{
    std::cout << "CORE_NS::PluginToken RegisterInterfaces" << std::endl;
    pluginRegistry.RegisterTypeInfo(RENDER_PLUGIN);
    // Register custom render nodes
    for (const auto& info : PT_RENDER_NODE_TYPE_INFOS) {
        pluginRegistry.RegisterTypeInfo(info);
    }
    // Register CameraControlSystem
    pluginRegistry.RegisterTypeInfo(CAMERA_CONTROL_SYSTEM_TYPE_INFO);
    return &pluginRegistry;
}

void UnregisterInterfaces(CORE_NS::PluginToken token)
{
    if (!token) {
        return;
    }
    auto* pluginRegistry = static_cast<CORE_NS::IPluginRegister*>(token);
    // Unregister CameraControlSystem
    pluginRegistry->UnregisterTypeInfo(CAMERA_CONTROL_SYSTEM_TYPE_INFO);
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
