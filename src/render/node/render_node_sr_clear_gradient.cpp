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

#include "render_node_sr_clear_gradient.h"

#include <render/datastore/intf_render_data_store_manager.h>
#include <render/datastore/intf_render_data_store_pod.h>
#include <render/device/intf_gpu_resource_manager.h>
#include <render/device/intf_shader_manager.h>
#include <render/nodecontext/intf_node_context_descriptor_set_manager.h>
#include <render/nodecontext/intf_node_context_pso_manager.h>
#include <render/nodecontext/intf_render_command_list.h>
#include <render/nodecontext/intf_render_node_context_manager.h>
#include <render/nodecontext/intf_render_node_graph_share_manager.h>
#include <render/nodecontext/intf_render_node_util.h>

// Temporarily disable PLUGIN_LOG due to crash issue
#define PLUGIN_LOG_I(...) do {} while (0)

using namespace BASE_NS;
using namespace RENDER_NS;

namespace {
constexpr const char* LR_GRADIENT_SSBO_NAME { "lr_gradient_ssbo" };
}

IRenderNode* RenderNodeSRClearGradient::Create()
{
    return new RenderNodeSRClearGradient;
}

void RenderNodeSRClearGradient::Destroy(IRenderNode* instance)
{
    delete static_cast<RenderNodeSRClearGradient*>(instance);
}

void RenderNodeSRClearGradient::InitNode(IRenderNodeContextManager& renderNodeContextMgr)
{
    renderNodeContextMgr_ = &renderNodeContextMgr;

    // Get all image handles from the render node graph share manager
    IRenderNodeGraphShareManager& rngShareMgr = renderNodeContextMgr.GetRenderNodeGraphShareManager();
    lrGradient_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_gradient");
    lossOutput_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "loss_output");
    debugOutput_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "debugOutput");
    predictedBaseColor_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "predicted_base_color");
    dL_dBaseColor_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "testColor");
    lrMomentum1_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_momentum1");
    lrMomentum2_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_momentum2");

    if (!RenderHandleUtil::IsValid(lrGradient_)) {
        return;
    }

    // Get image dimensions from gradient buffer
    const IRenderNodeGpuResourceManager& gpuResourceMgr = renderNodeContextMgr.GetGpuResourceManager();
    const GpuImageDesc gradientDesc = gpuResourceMgr.GetImageDescriptor(lrGradient_);
    lrWidth_ = gradientDesc.width;
    lrHeight_ = gradientDesc.height;

    const uint32_t gradientSsboByteSize = lrWidth_ * lrHeight_ * 4u * static_cast<uint32_t>(sizeof(float));
    lrGradientSsbo_ = renderNodeContextMgr.GetGpuResourceManager().Create(LR_GRADIENT_SSBO_NAME,
        GpuBufferDesc { CORE_BUFFER_USAGE_STORAGE_BUFFER_BIT, CORE_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0u,
            gradientSsboByteSize });
    
    // Get GT dimensions from loss output
    if (RenderHandleUtil::IsValid(lossOutput_)) {
        const GpuImageDesc lossDesc = gpuResourceMgr.GetImageDescriptor(lossOutput_);
        gtWidth_ = lossDesc.width;
        gtHeight_ = lossDesc.height;
    }

    // Load the compute shader and create PSO
    auto& shaderMgr = renderNodeContextMgr.GetShaderManager();
    auto& psoMgr = renderNodeContextMgr.GetPsoManager();
    INodeContextDescriptorSetManager& dSetMgr = renderNodeContextMgr.GetDescriptorSetManager();

    RenderHandle shaderHandle = shaderMgr.GetShaderHandle("pt://shaders/computeshader/sr_clear_gradient.shader");
    if (RenderHandleUtil::GetHandleType(shaderHandle) != RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
        return;
    }

    const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
    pso_ = psoMgr.GetComputePsoHandle(shaderHandle, pl, {});
    threadGroupSize_ = shaderMgr.GetReflectionThreadGroupSize(shaderHandle);

    // Reserve descriptor sets
    const auto& renderNodeUtil = renderNodeContextMgr.GetRenderNodeUtil();
    const DescriptorCounts dc = renderNodeUtil.GetDescriptorCounts(pl);
    dSetMgr.ResetAndReserve(dc);

    // Create descriptor set binder (set 0 only)
    constexpr uint32_t setIdx = 0U;
    const auto& bindings = pl.descriptorSetLayouts[setIdx].bindings;
    binder_ = dSetMgr.CreateDescriptorSetBinder(dSetMgr.CreateDescriptorSet(bindings), bindings);

    valid_ = true;
    rngShareMgr.RegisterRenderNodeOutput(LR_GRADIENT_SSBO_NAME, lrGradientSsbo_.GetHandle());
    PLUGIN_LOG_I("RenderNodeSRClearGradient: Initialized");
}

void RenderNodeSRClearGradient::PreExecuteFrame()
{
    IRenderNodeGraphShareManager& rngShareMgr = renderNodeContextMgr_->GetRenderNodeGraphShareManager();
    rngShareMgr.RegisterRenderNodeOutput(LR_GRADIENT_SSBO_NAME, lrGradientSsbo_.GetHandle());

    // Check for view switch flag from RenderDataStorePod
    const auto& renderDataStoreMgr = renderNodeContextMgr_->GetRenderDataStoreManager();
    auto* dataStorePod = static_cast<IRenderDataStorePod*>(
        renderDataStoreMgr.GetRenderDataStore("RenderDataStorePod"));
    
    if (dataStorePod) {
        auto flagData = dataStorePod->Get("ViewSwitchFlag");
        if (!flagData.empty()) {
            struct ViewSwitchFlag {
                uint32_t shouldClearBuffers;
            };
            const ViewSwitchFlag* flag = reinterpret_cast<const ViewSwitchFlag*>(flagData.data());
            
            if (flag && flag->shouldClearBuffers == 1) {
                viewSwitched_ = true;
                PLUGIN_LOG_I("RenderNodeSRClearGradient: View switch detected");
            }
            
            // Always destroy the Pod flag after reading it, to ensure it only affects one frame
            dataStorePod->DestroyPod("SRTraining", "ViewSwitchFlag");
        }
    }
}

void RenderNodeSRClearGradient::ExecuteFrame(IRenderCommandList& cmdList)
{
    if (!valid_ || !RenderHandleUtil::IsValid(pso_)) {
        return;
    }

    // Bind pipeline
    cmdList.BindPipeline(pso_);

    // Bind all images
    binder_->ClearBindings();
    binder_->BindImage(0, lrGradient_);
    binder_->BindImage(1, lossOutput_);
    binder_->BindImage(2, debugOutput_);
    binder_->BindImage(3, predictedBaseColor_);
    binder_->BindImage(4, dL_dBaseColor_);
    binder_->BindImage(5, lrMomentum1_);
    binder_->BindImage(6, lrMomentum2_);
    binder_->BindBuffer(7, lrGradientSsbo_.GetHandle(), 0u);

    cmdList.UpdateDescriptorSet(binder_->GetDescriptorSetHandle(),
                                binder_->GetDescriptorSetLayoutBindingResources());
    cmdList.BindDescriptorSet(0U, binder_->GetDescriptorSetHandle());

    // Push constants
    struct PushConstantData {
        uint32_t shouldClearBuffers;  // 1 = clear all buffers (view switch), 0 = only clear gradient
        uint32_t gtWidth;
        uint32_t gtHeight;
    } pc;
    
    pc.shouldClearBuffers = viewSwitched_ ? 1u : 0u;
    pc.gtWidth = gtWidth_;
    pc.gtHeight = gtHeight_;
    
    constexpr PushConstant pushConstant { ShaderStageFlagBits::CORE_SHADER_STAGE_COMPUTE_BIT, sizeof(PushConstantData) };
    cmdList.PushConstantData(pushConstant, arrayviewU8(pc));

    const uint32_t dispatchWidth = (gtWidth_ > lrWidth_) ? gtWidth_ : lrWidth_;
    const uint32_t dispatchHeight = (gtHeight_ > lrHeight_) ? gtHeight_ : lrHeight_;
    const uint32_t groupX = (dispatchWidth + threadGroupSize_.x - 1u) / threadGroupSize_.x;
    const uint32_t groupY = (dispatchHeight + threadGroupSize_.y - 1u) / threadGroupSize_.y;
    cmdList.Dispatch(groupX, groupY, 1u);
    
    // Reset flag after use
    if (viewSwitched_) {
        viewSwitched_ = false;
        PLUGIN_LOG_I("RenderNodeSRClearGradient: Buffers cleared via shader");
    }
}
