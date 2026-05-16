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

#include "render_node_sr_training.h"

#include <base/containers/array_view.h>
#include <render/device/intf_gpu_resource_manager.h>
#include <render/device/intf_shader_manager.h>
#include <render/nodecontext/intf_node_context_descriptor_set_manager.h>
#include <render/nodecontext/intf_node_context_pso_manager.h>
#include <render/nodecontext/intf_render_command_list.h>
#include <render/nodecontext/intf_render_node_context_manager.h>
#include <render/nodecontext/intf_render_node_graph_share_manager.h>
#include <render/nodecontext/intf_render_node_util.h>
#include <render/resource_handle.h>

#include <iostream>

#define SR_LOG_ONCE(message)            \
    do {                                \
        static bool logged = false;     \
        if (!logged) {                  \
            std::cout << message << '\n'; \
            logged = true;              \
        }                               \
    } while (0)

using namespace BASE_NS;
using namespace RENDER_NS;

namespace {
constexpr const char* SR_CLEAR_GRADIENT_NODE_NAME { "SR_CLEAR_GRADIENT" };
constexpr const char* LR_GRADIENT_SSBO_NAME { "lr_gradient_ssbo" };
}

IRenderNode* RenderNodeSRTraining::Create()
{
    return new RenderNodeSRTraining;
}

void RenderNodeSRTraining::Destroy(IRenderNode* instance)
{
    delete static_cast<RenderNodeSRTraining*>(instance);
}

void RenderNodeSRTraining::InitNode(IRenderNodeContextManager& renderNodeContextMgr)
{
    renderNodeContextMgr_ = &renderNodeContextMgr;

    ResolveResources();
    CreatePsos();
    
    valid_ = true;
    SR_LOG_ONCE("RenderNodeSRTraining: Initialized");
}

void RenderNodeSRTraining::PreExecuteFrame()
{
    IRenderNodeGraphShareManager& rngShareMgr = renderNodeContextMgr_->GetRenderNodeGraphShareManager();
    lrGradientSsbo_ = rngShareMgr.GetRegisteredRenderNodeOutput(SR_CLEAR_GRADIENT_NODE_NAME, LR_GRADIENT_SSBO_NAME);

    // Note: View switch flag handling moved to RenderNodeSRClearGradient
    config_.iteration++;
}

void RenderNodeSRTraining::ResolveResources()
{
    IRenderNodeGraphShareManager& rngShareMgr = renderNodeContextMgr_->GetRenderNodeGraphShareManager();
    depthBuffer_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateDefaultCameraGpuImages", "depth");
    uvBuffer_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateDefaultCameraGpuImages", "uv");
    lrTexture_ = rngShareMgr.GetRegisteredRenderNodeOutput("LOW_RESOLUTION_TEXTURES", "lrTexture");
    lrGradient_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_gradient");
    lrGradientSsbo_ = rngShareMgr.GetRegisteredRenderNodeOutput(SR_CLEAR_GRADIENT_NODE_NAME, LR_GRADIENT_SSBO_NAME);
    lrMomentum1_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_momentum1");
    lrMomentum2_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_momentum2");
    predictedColorOutput_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "predicted_color_output");
    predictedBaseColor_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "predicted_base_color");
    dLossDSampledTexture_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "dloss_dsampled_texture");
    const auto& gpuResourceMgr = renderNodeContextMgr_->GetGpuResourceManager();
    sampler_ = gpuResourceMgr.GetSamplerHandle("CORE_DEFAULT_SAMPLER_LINEAR_MIPMAP_REPEAT"); // default sampler
}

void RenderNodeSRTraining::CreatePsos()
{
    auto& shaderMgr = renderNodeContextMgr_->GetShaderManager();
    auto& psoMgr = renderNodeContextMgr_->GetPsoManager();
    INodeContextDescriptorSetManager& dSetMgr = renderNodeContextMgr_->GetDescriptorSetManager();
    
    constexpr uint32_t localSetIdx = 0U;
    
    // prevent BindDescriptorSet crash by calling ResetAndReserve
    DescriptorCounts totalCounts;
    const auto& renderNodeUtil = renderNodeContextMgr_->GetRenderNodeUtil();
    {
        RenderHandle shaderHandle = shaderMgr.GetShaderHandle("pt://shaders/computeshader/sr_differentiable_render.shader");
        if (RenderHandleUtil::GetHandleType(shaderHandle) == RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
            const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
            const auto& counts = renderNodeUtil.GetDescriptorCounts(pl);
            for (auto count : counts.counts) {
                totalCounts.counts.push_back(count);
            }
        }
    }
    {
        RenderHandle shaderHandle = shaderMgr.GetShaderHandle("pt://shaders/computeshader/sr_adam_optimizer.shader");
        if (RenderHandleUtil::GetHandleType(shaderHandle) == RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
            const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
            const auto& counts = renderNodeUtil.GetDescriptorCounts(pl);
            for (auto count : counts.counts) {
                totalCounts.counts.push_back(count);
            }
        }
    }
    dSetMgr.ResetAndReserve(totalCounts);

    // Load differentiable render shader
    {
        RenderHandle shaderHandle = shaderMgr.GetShaderHandle("pt://shaders/computeshader/sr_differentiable_render.shader");
        if (RenderHandleUtil::GetHandleType(shaderHandle) == RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
            const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
            psos_.differentiableRender = psoMgr.GetComputePsoHandle(shaderHandle, pl, {});
            psos_.differentiableRenderTGS = shaderMgr.GetReflectionThreadGroupSize(shaderHandle);
            
            const auto& binds = pl.descriptorSetLayouts[localSetIdx].bindings;
            differentiableRenderBinder_ = dSetMgr.CreateDescriptorSetBinder(dSetMgr.CreateDescriptorSet(binds), binds);
            
            SR_LOG_ONCE("RenderNodeSRTraining: Differentiable render shader loaded");
        }
    }
    
    // Load adam optimizer shader
    {
        RenderHandle shaderHandle = shaderMgr.GetShaderHandle("pt://shaders/computeshader/sr_adam_optimizer.shader");
        if (RenderHandleUtil::GetHandleType(shaderHandle) == RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
            const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
            psos_.adamOptimizer = psoMgr.GetComputePsoHandle(shaderHandle, pl, {});
            psos_.adamTGS = shaderMgr.GetReflectionThreadGroupSize(shaderHandle);
            
            const auto& binds = pl.descriptorSetLayouts[localSetIdx].bindings;
            adamBinder_ = dSetMgr.CreateDescriptorSetBinder(dSetMgr.CreateDescriptorSet(binds), binds);
            
            SR_LOG_ONCE("RenderNodeSRTraining: Adam optimizer shader loaded");
        }
    }
}

void RenderNodeSRTraining::ExecuteFrame(IRenderCommandList& cmdList)
{
    if (!valid_ || !config_.enabled) {
        SR_LOG_ONCE("RenderNodeSRTraining: disabled or invalid, skipping");
        return;
    }
    
    // Check resources
    const bool missingResource =
        !RenderHandleUtil::IsValid(depthBuffer_) || !RenderHandleUtil::IsValid(uvBuffer_) ||
        !RenderHandleUtil::IsValid(lrTexture_) || !RenderHandleUtil::IsValid(lrGradient_) ||
        !RenderHandleUtil::IsValid(lrGradientSsbo_) || !RenderHandleUtil::IsValid(lrMomentum1_) ||
        !RenderHandleUtil::IsValid(lrMomentum2_) || !RenderHandleUtil::IsValid(predictedColorOutput_) ||
        !RenderHandleUtil::IsValid(predictedBaseColor_) || !RenderHandleUtil::IsValid(dLossDSampledTexture_) ||
        !RenderHandleUtil::IsValid(sampler_);
    if (missingResource) {
        SR_LOG_ONCE(
            "RenderNodeSRTraining: missing resources, skipping. "
            "depth=" << RenderHandleUtil::IsValid(depthBuffer_) <<
            " uv=" << RenderHandleUtil::IsValid(uvBuffer_) <<
            " lrTexture=" << RenderHandleUtil::IsValid(lrTexture_) <<
            " lrGradient=" << RenderHandleUtil::IsValid(lrGradient_) <<
            " lrGradientSsbo=" << RenderHandleUtil::IsValid(lrGradientSsbo_) <<
            " momentum1=" << RenderHandleUtil::IsValid(lrMomentum1_) <<
            " momentum2=" << RenderHandleUtil::IsValid(lrMomentum2_) <<
            " predictedColor=" << RenderHandleUtil::IsValid(predictedColorOutput_) <<
            " predictedBaseColor=" << RenderHandleUtil::IsValid(predictedBaseColor_) <<
            " dLoss=" << RenderHandleUtil::IsValid(dLossDSampledTexture_) <<
            " sampler=" << RenderHandleUtil::IsValid(sampler_));
        return;
    }

    DispatchDifferentiableRender(cmdList);
    cmdList.AddCustomBarrierPoint();
    
    DispatchAdamOptimizer(cmdList);
}

void RenderNodeSRTraining::DispatchDifferentiableRender(IRenderCommandList& cmdList)
{
    if (!RenderHandleUtil::IsValid(psos_.differentiableRender)) {
        return;
    }
    
    cmdList.BindPipeline(psos_.differentiableRender);
    
    differentiableRenderBinder_->ClearBindings();
    differentiableRenderBinder_->BindImage(0, depthBuffer_);
    differentiableRenderBinder_->BindImage(1, lrGradient_);
    differentiableRenderBinder_->BindImage(3, uvBuffer_);
    differentiableRenderBinder_->BindSampler(6, sampler_);
    differentiableRenderBinder_->BindImage(10, predictedColorOutput_);
    differentiableRenderBinder_->BindImage(11, predictedBaseColor_);
    differentiableRenderBinder_->BindImage(12, dLossDSampledTexture_);
    differentiableRenderBinder_->BindBuffer(13, lrGradientSsbo_, 0u);
    cmdList.UpdateDescriptorSet(differentiableRenderBinder_->GetDescriptorSetHandle(),
                                 differentiableRenderBinder_->GetDescriptorSetLayoutBindingResources());
    cmdList.BindDescriptorSet(0U, differentiableRenderBinder_->GetDescriptorSetHandle());

    const GpuImageDesc depthDesc = renderNodeContextMgr_->GetGpuResourceManager().GetImageDescriptor(depthBuffer_);
    const uint32_t groupX = (depthDesc.width + psos_.differentiableRenderTGS.x - 1u) / psos_.differentiableRenderTGS.x;
    const uint32_t groupY = (depthDesc.height + psos_.differentiableRenderTGS.y - 1u) / psos_.differentiableRenderTGS.y;
    cmdList.Dispatch(groupX, groupY, 1);
}

void RenderNodeSRTraining::DispatchAdamOptimizer(IRenderCommandList& cmdList)
{
    if (!RenderHandleUtil::IsValid(psos_.adamOptimizer)) {
        return;
    }
    
    cmdList.BindPipeline(psos_.adamOptimizer);
    
    adamBinder_->ClearBindings();
    adamBinder_->BindImage(0, lrTexture_);
    adamBinder_->BindImage(1, lrGradient_);
    adamBinder_->BindImage(2, lrMomentum1_);
    adamBinder_->BindImage(3, lrMomentum2_);
    adamBinder_->BindBuffer(4, lrGradientSsbo_, 0u);
    
    cmdList.UpdateDescriptorSet(adamBinder_->GetDescriptorSetHandle(),
                                 adamBinder_->GetDescriptorSetLayoutBindingResources());
    cmdList.BindDescriptorSet(0U, adamBinder_->GetDescriptorSetHandle());
    
    struct PushConstantData {
        float learningRate;
        float beta1;
        float beta2;
        float epsilon;
        int iteration;
    } pc;
    pc.learningRate = config_.learningRate;
    pc.beta1 = config_.beta1;
    pc.beta2 = config_.beta2;
    pc.epsilon = config_.epsilon;
    pc.iteration = static_cast<int>(config_.iteration);
    
    constexpr PushConstant pushConstant { ShaderStageFlagBits::CORE_SHADER_STAGE_COMPUTE_BIT, sizeof(PushConstantData) };
    cmdList.PushConstantData(pushConstant, arrayviewU8(pc));
    
    const GpuImageDesc lrDesc = renderNodeContextMgr_->GetGpuResourceManager().GetImageDescriptor(lrTexture_);
    const uint32_t groupX = (lrDesc.width + psos_.adamTGS.x - 1u) / psos_.adamTGS.x;
    const uint32_t groupY = (lrDesc.height + psos_.adamTGS.y - 1u) / psos_.adamTGS.y;
    cmdList.Dispatch(groupX, groupY, 1);
}

