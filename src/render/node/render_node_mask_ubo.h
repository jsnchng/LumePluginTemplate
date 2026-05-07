#ifndef RENDER_NODE_MASK_UBO_H
#define RENDER_NODE_MASK_UBO_H

#include <base/util/uid.h>
#include <base/containers/string.h>
#include <render/namespace.h>
#include <render/nodecontext/intf_render_node.h>
#include <render/resource_handle.h>

RENDER_BEGIN_NAMESPACE()
class IRenderCommandList;
class IRenderNodeContextManager;

class RenderNodeMaskUbo final : public IRenderNode {
public:
    static constexpr BASE_NS::Uid UID { "c8d9e0f1-a2b3-4c5d-6e7f-8a9b0c1d2e3f" };
    static constexpr const char* TYPE_NAME = "RenderNodeMaskUbo";
    static constexpr IRenderNode::BackendFlags BACKEND_FLAGS = IRenderNode::BackendFlagBits::BACKEND_FLAG_BITS_DEFAULT;
    static constexpr IRenderNode::ClassType CLASS_TYPE = IRenderNode::ClassType::CLASS_TYPE_NODE;

    RenderNodeMaskUbo() = default;
    ~RenderNodeMaskUbo() override = default;

    static IRenderNode* Create();
    static void Destroy(IRenderNode* instance);

    void InitNode(IRenderNodeContextManager& renderNodeContextMgr) override;
    void PreExecuteFrame() override;
    void ExecuteFrame(IRenderCommandList& cmdList) override {}
    ExecuteFlags GetExecuteFlags() const override { return 0; }

private:
    void ParseJsonInputs();
    void WriteMaskValue();

    IRenderNodeContextManager* renderNodeContextMgr_ { nullptr };
    RenderHandleReference bufferHandle_;
    uint32_t maskValue_ { 0 };
    BASE_NS::string bufferShareName_;
    bool initialized_ { false };

    struct JsonInputs {
        uint32_t maskValue { 0 };
        BASE_NS::string bufferShareName;
    };
    JsonInputs jsonInputs_;
};

RENDER_END_NAMESPACE()
#endif
