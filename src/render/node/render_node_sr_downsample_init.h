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

#ifndef RENDER_POSTPROCESS_RENDER_NODE_SR_TRAINING_TEST_H
#define RENDER_POSTPROCESS_RENDER_NODE_SR_TRAINING_TEST_H

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
// Downsample Init Node
// ============================================================================
// Passes:
//   Pass 0: Downsample GT → LR (only on first frame, initializes LR texture)
// ============================================================================
class RenderNodeSRDownsampleInit final : public IRenderNode {
public:
    static constexpr BASE_NS::Uid UID { "a1b2c3d4-e5f6-7890-abcd-ef1234567891" };
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

    // Configuration
    struct Config {
        uint32_t gtWidth = 1024;
        uint32_t gtHeight = 1024;
        uint32_t lrWidth = 512;
        uint32_t lrHeight = 512;
        bool enabled = true;
        bool initialized = false;  // Track if LR texture has been initialized
    };

    void SetConfig(const Config& config) { config_ = config; }
    const Config& GetConfig() const { return config_; }

private:
    void CreatePsos();
    void ParseJsonInputs();
    
    // Initialization pass (runs once)
    void DispatchDownsampleInit(IRenderCommandList& cmdList);
    
    // Per-frame passes

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
    RenderHandle baseColorBuffer_;  // G-Buffer base color (for downsample init)
    
    // LR texture (base color to optimize)
    RenderHandle lrTexture_;
    
    // Sampler
    RenderHandle sampler_;
    
    // Gradient
    RenderHandle lrGradient_;

    // Pipeline handles
    struct PSOs {
        RenderHandle downsample;         // For LR texture initialization

        ShaderThreadGroup downsampleTGS { 8, 8, 1 };
    };
    PSOs psos_;

    // Descriptor set binders
    IDescriptorSetBinder::Ptr downsampleBinder_;

    bool valid_ { false };
};

RENDER_END_NAMESPACE()

#endif // RENDER_POSTPROCESS_RENDER_NODE_SR_TRAINING_TEST_H