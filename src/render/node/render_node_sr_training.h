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

#include <base/containers/array_view.h>
#include <base/math/vector.h>
#include <core/plugin/intf_interface_helper.h>
#include <render/namespace.h>
#include <render/nodecontext/intf_pipeline_descriptor_set_binder.h>
#include <render/nodecontext/intf_render_node.h>
#include <render/resource_handle.h>

RENDER_BEGIN_NAMESPACE()
class IRenderCommandList;
class IRenderNodeContextManager;

// ============================================================================
// Super-Resolution Differentiable Rendering Training Node
// ============================================================================
// Passes:
//   Pass 0: Downsample GT → LR (only on first frame, initializes LR texture)
//   Pass 1: Clear Gradient Buffer (every frame)
//   Pass 2: Differentiable Render (forward + loss + backward)
//   Pass 3: Adam Optimizer (update LR texture)
//
// Simplified PBR:
//   - Only optimize base_color texture
//   - Fixed material params from G-Buffer
//   - Single directional light
//   - No shadows, fog, or indirect lighting
// ============================================================================
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
        uint32_t gtWidth = 1024;
        uint32_t gtHeight = 1024;
        uint32_t lrWidth = 512;
        uint32_t lrHeight = 512;
        float lossScale = 1.0f;
        bool enabled = true;
        bool initialized = false;  // Track if LR texture has been initialized
        
        // Light parameters (should be from scene data)
        BASE_NS::Math::Vec3 lightDir { 0.0f, 1.0f, 0.0f };
        BASE_NS::Math::Vec3 lightColor { 1.0f, 1.0f, 1.0f };
        
        // Camera parameters (should be from camera data)
        BASE_NS::Math::Vec3 cameraPos { 0.0f, 0.0f, 3.0f };
        BASE_NS::Math::Mat4X4 viewProjInv;  // For world position calculation
    };

    void SetConfig(const Config& config) { config_ = config; }
    const Config& GetConfig() const { return config_; }

private:
    void CreatePsos();
    void ParseJsonInputs();
    
    // Initialization pass (runs once)
    void DispatchDownsampleInit(IRenderCommandList& cmdList);
    
    // Per-frame passes
    void DispatchClearGradient(IRenderCommandList& cmdList);
    void DispatchDifferentiableRender(IRenderCommandList& cmdList);
    void DispatchAdamOptimizer(IRenderCommandList& cmdList);

    IRenderNodeContextManager* renderNodeContextMgr_ { nullptr };

    Config config_;

    // JSON parsed resources
    struct JsonInputs {
        RenderNodeGraphInputs::InputResources resources;
        RenderNodeGraphInputs::InputResources images;
    };
    JsonInputs jsonInputs_;

    RenderNodeHandles::InputResources inputResources_;
    RenderNodeHandles::InputResources imageResources_;

    // G-Buffer handles
    RenderHandle depthBuffer_;
    RenderHandle normalBuffer_;
    RenderHandle materialBuffer_;
    RenderHandle uvBuffer_;
    RenderHandle baseColorBuffer_;  // G-Buffer base color (for downsample init)
    
    // LR texture (base color to optimize)
    RenderHandle lrTexture_;
    
    // GT image (rendered result from deferred shading)
    RenderHandle gtImage_;
    
    // Sampler
    RenderHandle sampler_;
    
    // Gradient and loss
    RenderHandle lrGradient_;
    RenderHandle lossOutput_;
    
    // Adam optimizer
    RenderHandle lrMomentum1_;
    RenderHandle lrMomentum2_;

    // Debug Output
    RenderHandle debugOutput_;

    // Pipeline handles
    struct PSOs {
        RenderHandle downsample;         // For LR texture initialization
        RenderHandle differentiableRender;
        RenderHandle adamOptimizer;

        ShaderThreadGroup downsampleTGS { 8, 8, 1 };
        ShaderThreadGroup differentiableRenderTGS { 8, 8, 1 };
        ShaderThreadGroup adamTGS { 8, 8, 1 };
    };
    PSOs psos_;

    // Descriptor set binders
    IDescriptorSetBinder::Ptr downsampleBinder_;
    IDescriptorSetBinder::Ptr differentiableRenderBinder_;
    IDescriptorSetBinder::Ptr adamBinder_;

    bool valid_ { false };
};

RENDER_END_NAMESPACE()

#endif // RENDER_POSTPROCESS_RENDER_NODE_SR_TRAINING_H