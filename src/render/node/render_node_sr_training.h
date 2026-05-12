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

#ifndef RENDER_POSTPROCESS_RENDER_NODE_SR_TRAINING_H
#define RENDER_POSTPROCESS_RENDER_NODE_SR_TRAINING_H

#include <core/plugin/intf_interface_helper.h>
#include <render/namespace.h>
#include <render/nodecontext/intf_pipeline_descriptor_set_binder.h>
#include <render/nodecontext/intf_render_node.h>
#include <render/resource_handle.h>

RENDER_BEGIN_NAMESPACE()
class IRenderCommandList;
class IRenderNodeContextManager;

// Accumulates gradients produced by deferred shading and updates the selected LR texture.
class RenderNodeSRTraining final : public IRenderNode {
public:
    static constexpr BASE_NS::Uid UID { "a1b2c3d4-e5f6-7890-abcd-ef1234567890" };
    static constexpr const char* TYPE_NAME = "RenderNodeSRTraining";
    static constexpr IRenderNode::BackendFlags BACKEND_FLAGS = IRenderNode::BackendFlagBits::BACKEND_FLAG_BITS_DEFAULT;
    static constexpr IRenderNode::ClassType CLASS_TYPE = IRenderNode::ClassType::CLASS_TYPE_NODE;

    RenderNodeSRTraining() = default;
    ~RenderNodeSRTraining() override = default;

    void InitNode(IRenderNodeContextManager& renderNodeContextMgr) override;
    void PreExecuteFrame() override;
    void ExecuteFrame(IRenderCommandList& cmdList) override;
    ExecuteFlags GetExecuteFlags() const override { return 0U; }

    static IRenderNode* Create();
    static void Destroy(IRenderNode* instance);

    // Configuration
    struct Config {
        float learningRate = 0.001f;
        float beta1 = 0.9f;
        float beta2 = 0.999f;
        float epsilon = 1e-8f;
        uint32_t iteration = 0;
        bool enabled = true;
    };

    void SetConfig(const Config& config) { config_ = config; }
    const Config& GetConfig() const { return config_; }

private:
    void CreatePsos();
    void ResolveResources();
    void DispatchDifferentiableRender(IRenderCommandList& cmdList);
    void DispatchAdamOptimizer(IRenderCommandList& cmdList);

    IRenderNodeContextManager* renderNodeContextMgr_ { nullptr };

    Config config_;

    // G-Buffer handles
    RenderHandle depthBuffer_;
    RenderHandle uvBuffer_;
    
    // LR texture selected by maskValue for optimization
    RenderHandle lrTexture_;
    
    // Sampler
    RenderHandle sampler_;
    
    // Gradient and loss
    RenderHandle lrGradient_;
    RenderHandle lrGradientSsbo_;
    
    // Adam optimizer
    RenderHandle lrMomentum1_;
    RenderHandle lrMomentum2_;

    // Predicted color output
    RenderHandle predictedColorOutput_;

    // Predicted Base Color
    RenderHandle predictedBaseColor_;

    // dLoss/dSampledTexture from deferred shading
    RenderHandle dLossDSampledTexture_;

    // Pipeline handles
    struct PSOs {
        RenderHandle differentiableRender;
        RenderHandle adamOptimizer;

        ShaderThreadGroup differentiableRenderTGS { 8, 8, 1 };
        ShaderThreadGroup adamTGS { 8, 8, 1 };
    };
    PSOs psos_;

    // Descriptor set binders
    IDescriptorSetBinder::Ptr differentiableRenderBinder_;
    IDescriptorSetBinder::Ptr adamBinder_;

    bool valid_ { false };
};

RENDER_END_NAMESPACE()

#endif // RENDER_POSTPROCESS_RENDER_NODE_SR_TRAINING_H
