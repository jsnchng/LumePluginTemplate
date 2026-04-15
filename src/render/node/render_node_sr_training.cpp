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

#include <render/device/intf_gpu_resource_manager.h>
#include <render/device/intf_shader_manager.h>
#include <render/nodecontext/intf_node_context_descriptor_set_manager.h>
#include <render/nodecontext/intf_node_context_pso_manager.h>
#include <render/nodecontext/intf_render_command_list.h>
#include <render/nodecontext/intf_render_node_context_manager.h>
#include <render/nodecontext/intf_render_node_graph_share_manager.h>
#include <render/nodecontext/intf_render_node_parser_util.h>
#include <render/nodecontext/intf_render_node_util.h>
#include <render/resource_handle.h>

// #include "util/log.h"
#define PLUGIN_LOG_I(...) do {} while (0)
#define PLUGIN_LOG_W(...) do {} while (0)

using namespace BASE_NS;
using namespace RENDER_NS;

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
    
    // Initialize viewProjInv to identity
    config_.viewProjInv = Math::Mat4X4(1.0f);
    
    ParseJsonInputs();
    CreatePsos();
    
    valid_ = true;
    PLUGIN_LOG_I("RenderNodeSRTraining: Initialized (Simplified PBR with Downsample Init)");
}

void RenderNodeSRTraining::PreExecuteFrame()
{
    config_.iteration++;
}

void RenderNodeSRTraining::ParseJsonInputs()
{
    const auto& renderNodeUtil = renderNodeContextMgr_->GetRenderNodeUtil();
    const IRenderNodeParserUtil& parserUtil = renderNodeContextMgr_->GetRenderNodeParserUtil();
    const auto jsonVal = renderNodeContextMgr_->GetNodeJson();
    
    jsonInputs_.resources = parserUtil.GetInputResources(jsonVal, "resources");
    inputResources_ = renderNodeUtil.CreateInputResources(jsonInputs_.resources);
    
    for (size_t i = 0; i < jsonInputs_.resources.images.size(); ++i) {
        const auto& res = jsonInputs_.resources.images[i];
        
        if (res.name == "depthBuffer" || res.name == "uDepthBuffer") {
            if (i < inputResources_.images.size()) {
                depthBuffer_ = inputResources_.images[i].handle;
            }
        } else if (res.name == "normalBuffer" || res.name == "uNormalBuffer") {
            if (i < inputResources_.images.size()) {
                normalBuffer_ = inputResources_.images[i].handle;
            }
        } else if (res.name == "materialBuffer" || res.name == "uMaterialBuffer") {
            if (i < inputResources_.images.size()) {
                materialBuffer_ = inputResources_.images[i].handle;
            }
        } else if (res.name == "uvBuffer" || res.name == "uUVBuffer") {
            if (i < inputResources_.images.size()) {
                uvBuffer_ = inputResources_.images[i].handle;
            }
        } else if (res.name == "baseColorBuffer" || res.name == "uBaseColorBuffer") {
            if (i < inputResources_.images.size()) {
                baseColorBuffer_ = inputResources_.images[i].handle;
            }
        } else if (res.name == "lrTexture" || res.name == "uLRTexture") {
            if (i < inputResources_.images.size()) {
                lrTexture_ = inputResources_.images[i].handle;
            }
        } else if (res.name == "gtImage" || res.name == "uGTImage") {
            if (i < inputResources_.images.size()) {
                gtImage_ = inputResources_.images[i].handle;
            }
        }
    }
    
    for (size_t i = 0; i < jsonInputs_.resources.samplers.size(); ++i) {
        const auto& res = jsonInputs_.resources.samplers[i];
        if (res.name == "sampler" || res.name == "uSampler") {
            if (i < inputResources_.samplers.size()) {
                sampler_ = inputResources_.samplers[i].handle;
            }
        }
    }
    
    jsonInputs_.images = parserUtil.GetInputResources(jsonVal, "images");
    imageResources_ = renderNodeUtil.CreateInputResources(jsonInputs_.images);
    
    for (size_t i = 0; i < jsonInputs_.images.images.size(); ++i) {
        const auto& img = jsonInputs_.images.images[i];
        
        if (img.name == "lrGradient" || img.name == "uLRGradient") {
            if (i < imageResources_.images.size()) {
                lrGradient_ = imageResources_.images[i].handle;
            }
        } else if (img.name == "lossOutput" || img.name == "uLossOutput") {
            if (i < imageResources_.images.size()) {
                lossOutput_ = imageResources_.images[i].handle;
            }
        } else if (img.name == "lrMomentum1" || img.name == "momentum1") {
            if (i < imageResources_.images.size()) {
                lrMomentum1_ = imageResources_.images[i].handle;
            }
        } else if (img.name == "lrMomentum2" || img.name == "momentum2") {
            if (i < imageResources_.images.size()) {
                lrMomentum2_ = imageResources_.images[i].handle;
            }
        }
    }

	// Obtain valid handles from GPU images created by previous nodes.
    IRenderNodeGraphShareManager& rngShareMgr = renderNodeContextMgr_->GetRenderNodeGraphShareManager();
    depthBuffer_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateDefaultCameraGpuImages", "depth");
    normalBuffer_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateDefaultCameraGpuImages", "velocity_normal");
    materialBuffer_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateDefaultCameraGpuImages", "material");
    uvBuffer_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateDefaultCameraGpuImages", "uv");
    baseColorBuffer_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateDefaultCameraGpuImages", "base_color");
    gtImage_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateDefaultCameraGpuImages", "color");
    lrTexture_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_texture");
    lrGradient_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_gradient");
    lossOutput_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "loss_output");
    lrMomentum1_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_momentum1");
    lrMomentum2_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_momentum2");
    const auto& gpuResourceMgr = renderNodeContextMgr_->GetGpuResourceManager();
    sampler_ = gpuResourceMgr.GetSamplerHandle("CORE_DEFAULT_SAMPLER_LINEAR_CLAMP");
}

void RenderNodeSRTraining::CreatePsos()
{
    auto& shaderMgr = renderNodeContextMgr_->GetShaderManager();
    auto& psoMgr = renderNodeContextMgr_->GetPsoManager();
    INodeContextDescriptorSetManager& dSetMgr = renderNodeContextMgr_->GetDescriptorSetManager();
    
    constexpr uint32_t localSetIdx = 0U;
    
    // Load downsample shader (for LR texture initialization)
    {
        RenderHandle shaderHandle = shaderMgr.GetShaderHandle("ptshaders://computeshader/texture_downsample.shader");
        if (RenderHandleUtil::GetHandleType(shaderHandle) == RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
            const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
            psos_.downsample = psoMgr.GetComputePsoHandle(shaderHandle, pl, {});
            psos_.downsampleTGS = shaderMgr.GetReflectionThreadGroupSize(shaderHandle);
            
            const auto& binds = pl.descriptorSetLayouts[localSetIdx].bindings;
            downsampleBinder_ = dSetMgr.CreateDescriptorSetBinder(dSetMgr.CreateDescriptorSet(binds), binds);
            
            PLUGIN_LOG_I("RenderNodeSRTraining: Downsample shader loaded");
        }
    }
    
    // Load differentiable render shader
    {
        RenderHandle shaderHandle = shaderMgr.GetShaderHandle("ptshaders://computeshader/sr_differentiable_render.shader");
        if (RenderHandleUtil::GetHandleType(shaderHandle) == RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
            const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
            psos_.differentiableRender = psoMgr.GetComputePsoHandle(shaderHandle, pl, {});
            psos_.differentiableRenderTGS = shaderMgr.GetReflectionThreadGroupSize(shaderHandle);
            
            const auto& binds = pl.descriptorSetLayouts[localSetIdx].bindings;
            differentiableRenderBinder_ = dSetMgr.CreateDescriptorSetBinder(dSetMgr.CreateDescriptorSet(binds), binds);
            
            PLUGIN_LOG_I("RenderNodeSRTraining: Differentiable render shader loaded");
        }
    }
    
    // Load adam optimizer shader
    {
        RenderHandle shaderHandle = shaderMgr.GetShaderHandle("ptshaders://computeshader/sr_adam_optimizer.shader");
        if (RenderHandleUtil::GetHandleType(shaderHandle) == RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
            const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
            psos_.adamOptimizer = psoMgr.GetComputePsoHandle(shaderHandle, pl, {});
            psos_.adamTGS = shaderMgr.GetReflectionThreadGroupSize(shaderHandle);
            
            const auto& binds = pl.descriptorSetLayouts[localSetIdx].bindings;
            adamBinder_ = dSetMgr.CreateDescriptorSetBinder(dSetMgr.CreateDescriptorSet(binds), binds);
            
            PLUGIN_LOG_I("RenderNodeSRTraining: Adam optimizer shader loaded");
        }
    }
}

void RenderNodeSRTraining::ExecuteFrame(IRenderCommandList& cmdList)
{
    if (!valid_ || !config_.enabled) {
        return;
    }
    
    // Check resources
    if (!RenderHandleUtil::IsValid(depthBuffer_) || !RenderHandleUtil::IsValid(normalBuffer_) ||
        !RenderHandleUtil::IsValid(materialBuffer_) || !RenderHandleUtil::IsValid(lrTexture_) ||
        !RenderHandleUtil::IsValid(lrGradient_) || !RenderHandleUtil::IsValid(gtImage_)) {
        PLUGIN_LOG_W("RenderNodeSRTraining: Missing resources, skipping frame");
        return;
    }
    
    // Pass 0: Initialize LR texture (only once, on first frame)
    if (!config_.initialized) {
        DispatchDownsampleInit(cmdList);
        cmdList.AddCustomBarrierPoint();
        config_.initialized = true;
        PLUGIN_LOG_I("RenderNodeSRTraining: LR texture initialized from GT base_color");
    }
    
    // Pass 1: Clear gradient buffer
    DispatchClearGradient(cmdList);
    cmdList.AddCustomBarrierPoint();
    
    // Pass 2: Differentiable render (forward + loss + backward)
    DispatchDifferentiableRender(cmdList);
    cmdList.AddCustomBarrierPoint();
    
    // Pass 3: Adam optimizer update
    DispatchAdamOptimizer(cmdList);
}

void RenderNodeSRTraining::DispatchDownsampleInit(IRenderCommandList& cmdList)
{
    if (!RenderHandleUtil::IsValid(psos_.downsample) || !RenderHandleUtil::IsValid(baseColorBuffer_)) {
        PLUGIN_LOG_W("RenderNodeSRTraining: Cannot initialize LR texture - missing downsample PSO or base_color");
        return;
    }
    
    cmdList.BindPipeline(psos_.downsample);
    
    // Bind: source=baseColorBuffer (G-Buffer), dest=lrTexture
    downsampleBinder_->ClearBindings();
    downsampleBinder_->BindImage(0, baseColorBuffer_);
    downsampleBinder_->BindSampler(1, sampler_);
    downsampleBinder_->BindImage(2, lrTexture_);
    
    cmdList.UpdateDescriptorSet(downsampleBinder_->GetDescriptorSetHandle(),
                                 downsampleBinder_->GetDescriptorSetLayoutBindingResources());
    cmdList.BindDescriptorSet(0U, downsampleBinder_->GetDescriptorSetHandle());
    
    // Push constants
    struct PushConstantData {
        float sourceWidth;
        float sourceHeight;
        float destWidth;
        float destHeight;
    } pc;
    pc.sourceWidth = static_cast<float>(config_.gtWidth);
    pc.sourceHeight = static_cast<float>(config_.gtHeight);
    pc.destWidth = static_cast<float>(config_.lrWidth);
    pc.destHeight = static_cast<float>(config_.lrHeight);
    
    constexpr PushConstant pushConstant { ShaderStageFlagBits::CORE_SHADER_STAGE_COMPUTE_BIT, sizeof(PushConstantData) };
    cmdList.PushConstantData(pushConstant, arrayviewU8(pc));
    
    // Dispatch
    const uint32_t groupX = (config_.lrWidth + psos_.downsampleTGS.x - 1) / psos_.downsampleTGS.x;
    const uint32_t groupY = (config_.lrHeight + psos_.downsampleTGS.y - 1) / psos_.downsampleTGS.y;
    cmdList.Dispatch(groupX, groupY, 1);
}

void RenderNodeSRTraining::DispatchClearGradient(IRenderCommandList& cmdList)
{
    // Gradient buffer is cleared by Adam optimizer at the end of each iteration
    // (see sr_adam_optimizer.comp: imageStore(uGradient, coord, vec4(0.0)))
    // No separate clear pass needed - Adam clears after using the gradient
}

void RenderNodeSRTraining::DispatchDifferentiableRender(IRenderCommandList& cmdList)
{
    if (!RenderHandleUtil::IsValid(psos_.differentiableRender)) {
        return;
    }
    
    cmdList.BindPipeline(psos_.differentiableRender);
    
    differentiableRenderBinder_->ClearBindings();
    differentiableRenderBinder_->BindImage(0, depthBuffer_);
    differentiableRenderBinder_->BindImage(1, normalBuffer_);
    differentiableRenderBinder_->BindImage(2, materialBuffer_);
    differentiableRenderBinder_->BindImage(3, uvBuffer_);
    differentiableRenderBinder_->BindImage(4, baseColorBuffer_);  // binding 4 for downsample init
    differentiableRenderBinder_->BindImage(5, lrTexture_);
    differentiableRenderBinder_->BindSampler(6, sampler_);
    differentiableRenderBinder_->BindImage(7, lrGradient_);
    differentiableRenderBinder_->BindImage(8, lossOutput_);
    differentiableRenderBinder_->BindImage(9, gtImage_);
    
    cmdList.UpdateDescriptorSet(differentiableRenderBinder_->GetDescriptorSetHandle(),
                                 differentiableRenderBinder_->GetDescriptorSetLayoutBindingResources());
    cmdList.BindDescriptorSet(0U, differentiableRenderBinder_->GetDescriptorSetHandle());
    
    // Push constants (must match sr_differentiable_render.comp layout)
    struct PushConstantData {
        uint32_t gtWidth;
        uint32_t gtHeight;
        uint32_t lrWidth;
        uint32_t lrHeight;
        uint32_t useUVBuffer;
        float _pad0;
        
        // Light direction
        float lightDirX;
        float lightDirY;
        float lightDirZ;
        float _pad1;
        
        // Light color
        float lightColorR;
        float lightColorG;
        float lightColorB;
        float lossScale;
        
        // Camera position
        float cameraPosX;
        float cameraPosY;
        float cameraPosZ;
        float _pad2;
        
        // View-Projection Inverse matrix (row 0)
        float vpInv00;
        float vpInv01;
        float vpInv02;
        float vpInv03;
        
        // View-Projection Inverse matrix (row 1)
        float vpInv10;
        float vpInv11;
        float vpInv12;
        float vpInv13;
        
        // View-Projection Inverse matrix (row 2)
        float vpInv20;
        float vpInv21;
        float vpInv22;
        float vpInv23;
        
        // View-Projection Inverse matrix (row 3)
        float vpInv30;
        float vpInv31;
        float vpInv32;
        float vpInv33;
    } pc;
    
    pc.gtWidth = config_.gtWidth;
    pc.gtHeight = config_.gtHeight;
    pc.lrWidth = config_.lrWidth;
    pc.lrHeight = config_.lrHeight;
    pc.useUVBuffer = RenderHandleUtil::IsValid(uvBuffer_) ? 1u : 0u;
    pc._pad0 = 0.0f;
    
    pc.lightDirX = config_.lightDir.x;
    pc.lightDirY = config_.lightDir.y;
    pc.lightDirZ = config_.lightDir.z;
    pc._pad1 = 0.0f;
    
    pc.lightColorR = config_.lightColor.x;
    pc.lightColorG = config_.lightColor.y;
    pc.lightColorB = config_.lightColor.z;
    pc.lossScale = config_.lossScale;
    
    pc.cameraPosX = config_.cameraPos.x;
    pc.cameraPosY = config_.cameraPos.y;
    pc.cameraPosZ = config_.cameraPos.z;
    pc._pad2 = 0.0f;
    
    // View-Projection Inverse matrix
    pc.vpInv00 = config_.viewProjInv.data[0];
    pc.vpInv01 = config_.viewProjInv.data[1];
    pc.vpInv02 = config_.viewProjInv.data[2];
    pc.vpInv03 = config_.viewProjInv.data[3];
    pc.vpInv10 = config_.viewProjInv.data[4];
    pc.vpInv11 = config_.viewProjInv.data[5];
    pc.vpInv12 = config_.viewProjInv.data[6];
    pc.vpInv13 = config_.viewProjInv.data[7];
    pc.vpInv20 = config_.viewProjInv.data[8];
    pc.vpInv21 = config_.viewProjInv.data[9];
    pc.vpInv22 = config_.viewProjInv.data[10];
    pc.vpInv23 = config_.viewProjInv.data[11];
    pc.vpInv30 = config_.viewProjInv.data[12];
    pc.vpInv31 = config_.viewProjInv.data[13];
    pc.vpInv32 = config_.viewProjInv.data[14];
    pc.vpInv33 = config_.viewProjInv.data[15];
    
    constexpr PushConstant pushConstant { ShaderStageFlagBits::CORE_SHADER_STAGE_COMPUTE_BIT, sizeof(PushConstantData) };
    cmdList.PushConstantData(pushConstant, arrayviewU8(pc));
    
    const uint32_t groupX = (config_.gtWidth + psos_.differentiableRenderTGS.x - 1) / psos_.differentiableRenderTGS.x;
    const uint32_t groupY = (config_.gtHeight + psos_.differentiableRenderTGS.y - 1) / psos_.differentiableRenderTGS.y;
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
    
    const uint32_t groupX = (config_.lrWidth + psos_.adamTGS.x - 1) / psos_.adamTGS.x;
    const uint32_t groupY = (config_.lrHeight + psos_.adamTGS.y - 1) / psos_.adamTGS.y;
    cmdList.Dispatch(groupX, groupY, 1);
}