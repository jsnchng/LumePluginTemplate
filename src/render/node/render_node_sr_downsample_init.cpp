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

void RenderNodeSRDownsampleInit::InitNode(IRenderNodeContextManager& renderNodeContextMgr)
{
    renderNodeContextMgr_ = &renderNodeContextMgr;
    
    const auto& parserUtil = renderNodeContextMgr_->GetRenderNodeParserUtil();
    const auto nodeJsonVal = renderNodeContextMgr_->GetNodeJson();
    const auto shaderPath = parserUtil.GetStringValue(nodeJsonVal, "shader");
    // Get custom settings from json
    const auto customJsonVal = nodeJsonVal.find("custom");
    const auto renderDataStoreName = parserUtil.GetStringValue(*customJsonVal, "renderDataStoreName");
    const auto targetMaterialIndex = parserUtil.GetUintValue(*customJsonVal, "targetMaterialIndex");
    auto lrTextureShareName = parserUtil.GetStringValue(*customJsonVal, "lrTextureShareName");
    auto maskBufferShareName = parserUtil.GetStringValue(*customJsonVal, "maskBufferShareName");
    const auto shareNameFromNode = parserUtil.GetStringValue(*customJsonVal, "shareNameFromNode");
    const auto defaultSamplerName = parserUtil.GetStringValue(*customJsonVal, "defaultSamplerName");
    if (lrTextureShareName.empty()) {
        lrTextureShareName = "lrTexture";
    }
    if (maskBufferShareName.empty()) {
        maskBufferShareName = "mask_ubo_buffer";
    }
    // Get high-res raw textures
    const auto& renderDataStoreMgr = renderNodeContextMgr_->GetRenderDataStoreManager();
    const auto* dataStoreMaterial = static_cast<CORE3D_NS::IRenderDataStoreDefaultMaterial*>(
        renderDataStoreMgr.GetRenderDataStore(renderDataStoreName));
    const auto& materialHandles = dataStoreMaterial->GetMaterialHandles();
    const auto& currentHandles = materialHandles[targetMaterialIndex];
    rawAlbedo_ = currentHandles.images[0];
    rawNormal_ = currentHandles.images[1];
    rawMaterial_ = currentHandles.images[2];
    rawEmissive_ = currentHandles.images[3];
    rawAo_ = currentHandles.images[4];
    // Get low-res texture and mask selection buffer
    const auto& rngShareMgr = renderNodeContextMgr_->GetRenderNodeGraphShareManager();
    lrTexture_ = rngShareMgr.GetRegisteredRenderNodeOutput(shareNameFromNode, lrTextureShareName);
    maskBuffer_ = rngShareMgr.GetRegisteredRenderNodeOutput("MASK_UBO_NODE", maskBufferShareName);
    // Get default sampler
    const auto& gpuResourceMgr = renderNodeContextMgr_->GetGpuResourceManager();
    defaultSampler_ = gpuResourceMgr.GetSamplerHandle(defaultSamplerName);
    defaultMaterialImage_ = rngShareMgr.GetRegisteredRenderNodeOutput(shareNameFromNode, "default_1x1_white");

    constexpr uint32_t localSetIdx = 0U;
    const auto& renderNodeUtil = renderNodeContextMgr_->GetRenderNodeUtil();
    const auto& shaderMgr = renderNodeContextMgr_->GetShaderManager();
    auto& psoMgr = renderNodeContextMgr_->GetPsoManager();
    auto& dSetMgr = renderNodeContextMgr_->GetDescriptorSetManager();
    // Get shader handle
    const auto shaderHandle = shaderMgr.GetShaderHandle(shaderPath);
    if (RenderHandleUtil::GetHandleType(shaderHandle) != RenderHandleType::COMPUTE_SHADER_STATE_OBJECT) {
        return;
    }
    // Get pipeline layout
    const PipelineLayout& pl = shaderMgr.GetReflectionPipelineLayout(shaderHandle);
    // Get descriptor counts
    DescriptorCounts totalCounts;
    for (auto count : renderNodeUtil.GetDescriptorCounts(pl).counts) {
        totalCounts.counts.push_back(count);
    }
    dSetMgr.ResetAndReserve(totalCounts);
    psoHandle_ = psoMgr.GetComputePsoHandle(shaderHandle, pl, {});
    threadGroupSize_ = shaderMgr.GetReflectionThreadGroupSize(shaderHandle);
    // Create descriptor set
    const auto& bindings = pl.descriptorSetLayouts[localSetIdx].bindings;
    binder_ = dSetMgr.CreateDescriptorSetBinder(dSetMgr.CreateDescriptorSet(bindings), bindings);
}

void RenderNodeSRDownsampleInit::ExecuteFrame(IRenderCommandList& cmdList)
{
    if (!config_.initialized) {
        DispatchDownsampleInit(cmdList);
        cmdList.AddCustomBarrierPoint();
        config_.initialized = true;
    }
}

void RenderNodeSRDownsampleInit::DispatchDownsampleInit(IRenderCommandList& cmdList)
{
    // Check resources
    if (!RenderHandleUtil::IsValid(lrTexture_)
        || !RenderHandleUtil::IsValid(maskBuffer_)
        || !RenderHandleUtil::IsValid(defaultSampler_)) {
        return;
    }

    // Replace invalid handles with default material image
    if (!RenderHandleUtil::IsValid(rawAlbedo_)) {
        rawAlbedo_ = defaultMaterialImage_;
    }
    if (!RenderHandleUtil::IsValid(rawNormal_)) {
        rawNormal_ = defaultMaterialImage_;
    }
    if (!RenderHandleUtil::IsValid(rawMaterial_)) {
        rawMaterial_ = defaultMaterialImage_;
    }
    if (!RenderHandleUtil::IsValid(rawEmissive_)) {
        rawEmissive_ = defaultMaterialImage_;
    }
    if (!RenderHandleUtil::IsValid(rawAo_)) {
        rawAo_ = defaultMaterialImage_;
    }

    if (!RenderHandleUtil::IsValid(psoHandle_)) {
        return;
    }

    cmdList.BindPipeline(psoHandle_);
    binder_->ClearBindings();
    binder_->BindSampler(0, defaultSampler_);
    binder_->BindImage(1, rawAlbedo_);
    binder_->BindImage(2, rawNormal_);
    binder_->BindImage(3, rawMaterial_);
    binder_->BindImage(4, rawEmissive_);
    binder_->BindImage(5, rawAo_);
    binder_->BindBuffer(6, maskBuffer_, 0u);
    binder_->BindImage(7, lrTexture_);
    cmdList.UpdateDescriptorSet(binder_->GetDescriptorSetHandle(),
                                binder_->GetDescriptorSetLayoutBindingResources());
    cmdList.BindDescriptorSet(0U, binder_->GetDescriptorSetHandle());
    
    // Push constants
    struct PushConstantData {
        uint32_t lrWidth;
        uint32_t lrHeight;
    } pc;
    pc.lrWidth = config_.lrWidth;
    pc.lrHeight = config_.lrHeight;
    
    constexpr PushConstant pushConstant { ShaderStageFlagBits::CORE_SHADER_STAGE_COMPUTE_BIT, sizeof(PushConstantData) };
    cmdList.PushConstantData(pushConstant, arrayviewU8(pc));
    
    // Dispatch
    const uint32_t groupX = config_.lrWidth / threadGroupSize_.x;
    const uint32_t groupY = config_.lrHeight / threadGroupSize_.y;
    cmdList.Dispatch(groupX, groupY, 1);
}

IRenderNode* RenderNodeSRDownsampleInit::Create()
{
    return new RenderNodeSRDownsampleInit;
}

void RenderNodeSRDownsampleInit::Destroy(IRenderNode* instance)
{
    delete static_cast<RenderNodeSRDownsampleInit*>(instance);
}
