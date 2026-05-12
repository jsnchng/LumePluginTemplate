#ifndef RENDER_NODE_SR_DOWNSAMPLE_INIT_H
#define RENDER_NODE_SR_DOWNSAMPLE_INIT_H

#include <core/plugin/intf_interface_helper.h>
#include <render/namespace.h>
#include <render/nodecontext/intf_pipeline_descriptor_set_binder.h>
#include <render/nodecontext/intf_render_node.h>
#include <render/resource_handle.h>

RENDER_BEGIN_NAMESPACE()
class IRenderCommandList;
class IRenderNodeContextManager;

class RenderNodeSRDownsampleInit final : public IRenderNode {
public:
    static constexpr BASE_NS::Uid UID { "2ca1a08d-13d9-45f2-81fc-e093db492f3f" };
    static constexpr const char* TYPE_NAME = "RenderNodeSRDownsampleInit";
    static constexpr IRenderNode::BackendFlags BACKEND_FLAGS = IRenderNode::BackendFlagBits::BACKEND_FLAG_BITS_DEFAULT;
    static constexpr IRenderNode::ClassType CLASS_TYPE = IRenderNode::ClassType::CLASS_TYPE_NODE;

    RenderNodeSRDownsampleInit() = default;
    ~RenderNodeSRDownsampleInit() override = default;

    void InitNode(IRenderNodeContextManager& renderNodeContextMgr) override;
    void PreExecuteFrame() override {};
    void ExecuteFrame(IRenderCommandList& cmdList) override;
    ExecuteFlags GetExecuteFlags() const override { return 0U; }

    static IRenderNode* Create();
    static void Destroy(IRenderNode* instance);

private:
    void DispatchDownsampleInit(IRenderCommandList& cmdList);

    IRenderNodeContextManager* renderNodeContextMgr_ { nullptr };

    bool initialized_ { false };

    // high-res raw textures for downsampling
    RenderHandle rawAlbedo_;
    RenderHandle rawNormal_;
    RenderHandle rawMaterial_;
    RenderHandle rawEmissive_;
    RenderHandle rawAo_;

    // low-res texture selected by maskValue for optimization
    RenderHandle lrTexture_;
    RenderHandle maskBuffer_;

    RenderHandle defaultSampler_;
    RenderHandle defaultMaterialImage_;

    RenderHandle psoHandle_;
    ShaderThreadGroup threadGroupSize_{ 1u, 1u, 1u };

    IDescriptorSetBinder::Ptr binder_;
};

RENDER_END_NAMESPACE()

#endif // RENDER_NODE_SR_DOWNSAMPLE_INIT_H
