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

#include <render/device/intf_gpu_resource_manager.h>
#include <render/device/intf_shader_manager.h>
#include <render/nodecontext/intf_node_context_descriptor_set_manager.h>
#include <render/nodecontext/intf_node_context_pso_manager.h>
#include <render/nodecontext/intf_render_command_list.h>
#include <render/nodecontext/intf_render_node_context_manager.h>
#include <render/nodecontext/intf_render_node_graph_share_manager.h>
#include <render/nodecontext/intf_render_node_util.h>

using namespace BASE_NS;
using namespace RENDER_NS;

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

    // 1. Get the lr_gradient image from the render node graph share manager
    IRenderNodeGraphShareManager& rngShareMgr = renderNodeContextMgr.GetRenderNodeGraphShareManager();
    lrGradient_ = rngShareMgr.GetRegisteredRenderNodeOutput("RenderNodeCreateGpuImages", "lr_gradient");

    if (!RenderHandleUtil::IsValid(lrGradient_)) {
        return;
    }

    // 2. Load the compute shader and create PSO
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

    // 3. Reserve descriptor sets
    const auto& renderNodeUtil = renderNodeContextMgr.GetRenderNodeUtil();
    const DescriptorCounts dc = renderNodeUtil.GetDescriptorCounts(pl);
    dSetMgr.ResetAndReserve(dc);

    // 4. Create descriptor set binder (set 0 only)
    constexpr uint32_t setIdx = 0U;
    const auto& bindings = pl.descriptorSetLayouts[setIdx].bindings;
    binder_ = dSetMgr.CreateDescriptorSetBinder(dSetMgr.CreateDescriptorSet(bindings), bindings);

    valid_ = true;
}

void RenderNodeSRClearGradient::ExecuteFrame(IRenderCommandList& cmdList)
{
    if (!valid_ || !RenderHandleUtil::IsValid(lrGradient_) || !RenderHandleUtil::IsValid(pso_)) {
        return;
    }

    // Bind pipeline
    cmdList.BindPipeline(pso_);

    // Bind image to set 0, binding 0
    binder_->ClearBindings();
    binder_->BindImage(0, lrGradient_);

    cmdList.UpdateDescriptorSet(binder_->GetDescriptorSetHandle(),
                                binder_->GetDescriptorSetLayoutBindingResources());
    cmdList.BindDescriptorSet(0U, binder_->GetDescriptorSetHandle());

    // Dispatch based on image size
    const IRenderNodeGpuResourceManager& gpuResourceMgr = renderNodeContextMgr_->GetGpuResourceManager();
    const GpuImageDesc desc = gpuResourceMgr.GetImageDescriptor(lrGradient_);
    const uint32_t groupX = (desc.width + threadGroupSize_.x - 1u) / threadGroupSize_.x;
    const uint32_t groupY = (desc.height + threadGroupSize_.y - 1u) / threadGroupSize_.y;
    cmdList.Dispatch(groupX, groupY, 1u);
}
