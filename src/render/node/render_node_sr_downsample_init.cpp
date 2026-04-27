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

#include "render_node_sr_downsample_init.h"

#include <3d/render/intf_render_data_store_default_material.h>
#include <render/datastore/intf_render_data_store_manager.h>
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


using namespace BASE_NS;
using namespace RENDER_NS;

IRenderNode* RenderNodeSRDownsampleInit::Create()
{
    return new RenderNodeSRDownsampleInit;
}

void RenderNodeSRDownsampleInit::Destroy(IRenderNode* instance)
{
    delete static_cast<RenderNodeSRDownsampleInit*>(instance);
}

void RenderNodeSRDownsampleInit::InitNode(IRenderNodeContextManager& renderNodeContextMgr)
{
    renderNodeContextMgr_ = &renderNodeContextMgr;
    
    ParseJsonInputs();
    CreatePsos();
    
    valid_ = true;
}

void RenderNodeSRDownsampleInit::ParseJsonInputs()
{
    const auto& renderNodeUtil = renderNodeContextMgr_->GetRenderNodeUtil();
    const IRenderNodeParserUtil& parserUtil = renderNodeContextMgr_->GetRenderNodeParserUtil();
    const auto jsonVal = renderNodeContextMgr_->GetNodeJson();
    
    jsonInputs_.resources = parserUtil.GetInputResources(jsonVal, "resources");
    inputResources_ = renderNodeUtil.CreateInputResources(jsonInputs_.resources);
    
    for (size_t i = 0; i < jsonInputs_.resources.images.size(); ++i) {
        const auto& res = jsonInputs_.resources.images[i];
        
        if (res.name == "baseColorBuffer" || res.name == "uBaseColorBuffer") {
            if (i < inputResources_.images.size()) {
                baseColorBuffer_ = inputResources_.images[i].handle;
            }
        } else if (res.name == "lrTexture" || res.name == "uLRTexture") {
            if (i < inputResources_.images.size()) {
                lrTexture_ = inputResources_.images[i].handle;
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
        }
    }

	// Obtain valid handles from GPU images created by previous nodes.
    IRenderNodeGraphShareManager& rngShareMgr = renderNodeContextMgr_->GetRenderNodeGraphShareManager();
    baseColorBuffer_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateDefaultCameraGpuImages", "base_color");
    lrTexture_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_texture");
    lrGradient_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_gradient");
    const auto& gpuResourceMgr = renderNodeContextMgr_->GetGpuResourceManager();
    sampler_ = gpuResourceMgr.GetSamplerHandle("CORE_DEFAULT_SAMPLER_LINEAR_MIPMAP_REPEAT"); // default sampler
}

void RenderNodeSRDownsampleInit::CreatePsos()
{
    auto& shaderMgr = renderNodeContextMgr_->GetShaderManager();
    auto& psoMgr = renderNodeContextMgr_->GetPsoManager();
    INodeContextDescriptorSetManager& dSetMgr = renderNodeContextMgr_->GetDescriptorSetManager();
    
    constexpr uint32_t localSetIdx = 0U;
    
    // prevent BindDescriptorSet crash by calling ResetAndReserve
    DescriptorCounts totalCounts;
    const auto& renderNodeUtil = renderNodeContextMgr_->GetRenderNodeUtil();
    {
        RenderHandle shaderHandle = shaderMgr.GetShaderHandle("pt://shaders/computeshader/sr_downsample_init.shader");
        if (RenderHandleUtil::GetHandleType(shaderHandle) == RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
            const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
            const auto& counts = renderNodeUtil.GetDescriptorCounts(pl);
            for (auto count : counts.counts) {
                totalCounts.counts.push_back(count);
            }
        }
    }
    dSetMgr.ResetAndReserve(totalCounts);

    // Load downsample shader (for LR texture initialization)
    {
        RenderHandle shaderHandle = shaderMgr.GetShaderHandle("pt://shaders/computeshader/sr_downsample_init.shader");
        if (RenderHandleUtil::GetHandleType(shaderHandle) == RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
            const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
            psos_.downsample = psoMgr.GetComputePsoHandle(shaderHandle, pl, {});
            psos_.downsampleTGS = shaderMgr.GetReflectionThreadGroupSize(shaderHandle);
            
            const auto& binds = pl.descriptorSetLayouts[localSetIdx].bindings;
            downsampleBinder_ = dSetMgr.CreateDescriptorSetBinder(dSetMgr.CreateDescriptorSet(binds), binds);
        }
    }
}

void RenderNodeSRDownsampleInit::ExecuteFrame(IRenderCommandList& cmdList)
{
    if (!valid_ || !config_.enabled) {
        return;
    }
    
    // Check resources
    if (!RenderHandleUtil::IsValid(lrTexture_) || !RenderHandleUtil::IsValid(lrGradient_)) {
        return;
    }
    
    // Pass 0: Initialize LR texture (only once, on first frame)
    if (!config_.initialized) {
        DispatchDownsampleInit(cmdList);
        cmdList.AddCustomBarrierPoint();
        config_.initialized = true;
    }
}

void RenderNodeSRDownsampleInit::DispatchDownsampleInit(IRenderCommandList& cmdList)
{
    if (!RenderHandleUtil::IsValid(psos_.downsample) || !RenderHandleUtil::IsValid(baseColorBuffer_)) {
        return;
    }
    
    cmdList.BindPipeline(psos_.downsample);
    
    // Fetch base color texture from material 1
    const auto& renderDataStoreMgr = renderNodeContextMgr_->GetRenderDataStoreManager();
    const auto* dataStoreMaterial = static_cast<CORE3D_NS::IRenderDataStoreDefaultMaterial*>(
        renderDataStoreMgr.GetRenderDataStore("RenderDataStoreDefaultMaterial"));
    const auto& materialHandles = dataStoreMaterial->GetMaterialHandles();
    const auto& handles = materialHandles[1];
    const RenderHandle baseColorImage = handles.images[0];
    const RenderHandle baseColorSampler = handles.samplers[0];
    // Bind: source=baseColorBuffer (G-Buffer), dest=lrTexture
    downsampleBinder_->ClearBindings();
    if (!RenderHandleUtil::IsValid(baseColorImage)) {
        downsampleBinder_->BindImage(0, baseColorBuffer_);
    } else {
        downsampleBinder_->BindImage(0, baseColorImage);
    }
    if (!RenderHandleUtil::IsValid(baseColorSampler)) {
        downsampleBinder_->BindSampler(1, sampler_);
    } else {
        downsampleBinder_->BindSampler(1, baseColorSampler);
    }
    downsampleBinder_->BindImage(2, lrTexture_);
    downsampleBinder_->BindImage(3, lrGradient_);
    
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