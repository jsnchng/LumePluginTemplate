#include "render_node_mask_ubo.h"

#include <render/device/intf_gpu_resource_manager.h>
#include <render/nodecontext/intf_render_node_context_manager.h>
#include <render/nodecontext/intf_render_node_graph_share_manager.h>
#include <render/nodecontext/intf_render_node_parser_util.h>

#include <base/containers/allocator.h>

using namespace BASE_NS;
using namespace RENDER_NS;

IRenderNode* RenderNodeMaskUbo::Create()
{
    return new RenderNodeMaskUbo();
}

void RenderNodeMaskUbo::Destroy(IRenderNode* instance)
{
    delete static_cast<RenderNodeMaskUbo*>(instance);
}

void RenderNodeMaskUbo::InitNode(IRenderNodeContextManager& renderNodeContextMgr)
{
    renderNodeContextMgr_ = &renderNodeContextMgr;
    ParseJsonInputs();

    auto& gpuResourceMgr = renderNodeContextMgr_->GetGpuResourceManager();

    constexpr uint32_t kUboSize = 16;

    GpuBufferDesc desc {
        CORE_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        CORE_MEMORY_PROPERTY_HOST_VISIBLE_BIT | CORE_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        0,
        kUboSize
    };

    bufferHandle_ = gpuResourceMgr.Create(bufferShareName_, desc);

    // Register to share manager so other nodes can reference by shareName
    IRenderNodeGraphShareManager& rngShareMgr = renderNodeContextMgr_->GetRenderNodeGraphShareManager();
    rngShareMgr.RegisterRenderNodeOutput(bufferShareName_, bufferHandle_.GetHandle());
}

void RenderNodeMaskUbo::PreExecuteFrame()
{
    if (!initialized_) {
        WriteMaskValue();
    }

    // Re-register every frame (same pattern as RenderNodeCreateGpuBuffers)
    IRenderNodeGraphShareManager& rngShareMgr = renderNodeContextMgr_->GetRenderNodeGraphShareManager();
    rngShareMgr.RegisterRenderNodeOutput(bufferShareName_, bufferHandle_.GetHandle());
}

void RenderNodeMaskUbo::WriteMaskValue()
{
    auto& gpuResourceMgr = renderNodeContextMgr_->GetGpuResourceManager();
    constexpr uint32_t kUboSize = 16;

    // Use MapBufferMemory which takes RenderHandleReference
    if (auto data = reinterpret_cast<uint8_t*>(gpuResourceMgr.MapBufferMemory(bufferHandle_.GetHandle())); data) {
        const uint32_t mask = maskValue_;
        CloneData(data, kUboSize, &mask, sizeof(mask));
        gpuResourceMgr.UnmapBuffer(bufferHandle_.GetHandle());
        initialized_ = true;
    }
}

void RenderNodeMaskUbo::ParseJsonInputs()
{
    const IRenderNodeParserUtil& parserUtil = renderNodeContextMgr_->GetRenderNodeParserUtil();
    const auto jsonVal = renderNodeContextMgr_->GetNodeJson();
    const auto customIt = jsonVal.find("custom");

    if (customIt) {
        maskValue_ = static_cast<uint32_t>(parserUtil.GetUintValue(*customIt, "maskValue"));
        bufferShareName_ = parserUtil.GetStringValue(*customIt, "bufferShareName");
    }

    // Fallback defaults
    if (maskValue_ == 0) {
        maskValue_ = 0x00000001u;
    }
    if (bufferShareName_.empty()) {
        bufferShareName_ = "mask_ubo_buffer";
    }
}
