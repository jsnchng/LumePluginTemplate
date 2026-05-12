/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RENDER_NODE_SR_CLEAR_GRADIENT_H
#define RENDER_NODE_SR_CLEAR_GRADIENT_H

#include <base/util/uid.h>
#include <render/namespace.h>
#include <render/nodecontext/intf_pipeline_descriptor_set_binder.h>
#include <render/nodecontext/intf_render_node.h>
#include <render/resource_handle.h>

RENDER_BEGIN_NAMESPACE()
class IRenderCommandList;
class IRenderNodeContextManager;

class RenderNodeSRClearGradient final : public IRenderNode {
public:
    static constexpr BASE_NS::Uid UID { "b3148cd2-6fbb-4849-bfc2-631667d126ea" };
    static constexpr const char* TYPE_NAME = "RenderNodeSRClearGradient";
    static constexpr IRenderNode::BackendFlags BACKEND_FLAGS = IRenderNode::BackendFlagBits::BACKEND_FLAG_BITS_DEFAULT;
    static constexpr IRenderNode::ClassType CLASS_TYPE = IRenderNode::ClassType::CLASS_TYPE_NODE;

    RenderNodeSRClearGradient() = default;
    ~RenderNodeSRClearGradient() override = default;

    void InitNode(IRenderNodeContextManager& renderNodeContextMgr) override;
    void PreExecuteFrame() override;
    void ExecuteFrame(IRenderCommandList& cmdList) override;
    ExecuteFlags GetExecuteFlags() const override { return 0U; }

    static IRenderNode* Create();
    static void Destroy(IRenderNode* instance);

private:
    IRenderNodeContextManager* renderNodeContextMgr_ { nullptr };

    // Gradient buffer (cleared every frame)
    RenderHandle lrGradient_;
    RenderHandleReference lrGradientSsbo_;
    
    // Loss and training outputs (cleared on view switch)
    RenderHandle lossOutput_;
    RenderHandle predictedColorOutput_;
    RenderHandle predictedBaseColor_;
    RenderHandle dLossDSampledTexture_;
    
    // Adam optimizer momentum buffers (cleared on view switch)
    RenderHandle lrMomentum1_;
    RenderHandle lrMomentum2_;

    // Pipeline
    RenderHandle pso_;
    ShaderThreadGroup threadGroupSize_ { 8, 8, 1 };

    // Descriptor set binder
    IDescriptorSetBinder::Ptr binder_;
    
    // View switch flag (set in PreExecuteFrame, used in ExecuteFrame)
    bool viewSwitched_ { false };
    
    // Image dimensions for dispatch
    uint32_t gtWidth_ { 1024 };
    uint32_t gtHeight_ { 1024 };
    uint32_t lrWidth_ { 1024 };
    uint32_t lrHeight_ { 1024 };

    bool valid_ { false };
};

RENDER_END_NAMESPACE()

#endif // RENDER_NODE_SR_CLEAR_GRADIENT_H
