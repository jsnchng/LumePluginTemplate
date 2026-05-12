#version 460 core
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// includes

#include "3d/shaders/common/3d_dm_brdf_common.h"
#include "3d/shaders/common/3d_dm_shadowing_common.h"
#include "3d/shaders/common/3d_dm_structures_common.h"
#include "3d/shaders/common/3d_dm_target_packing_common.h"
#include "render/shaders/common/render_color_conversion_common.h"
#include "render/shaders/common/render_post_process_common.h"
#include "render/shaders/common/render_tonemap_common.h"

// sets and specializations

#include "3d/shaders/common/3d_dm_frag_layout_common.h"
#include "3d/shaders/common/3d_dm_lighting_common.h"
#define CORE3D_DM_DF_FRAG_INPUT 1
#include "3d/shaders/common/3d_dm_inout_common.h"
#include "3d/shaders/common/3d_dm_inplace_sampling_common.h"

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outVelocityNormal;
layout(location = 2) out vec4 outBaseColor;
layout(location = 3) out vec4 outMaterial;
layout(location = 4) out vec4 outUv;
layout(location = 5) out vec4 outGeomNormal;
layout(location = 6) out vec4 outTangentW;

uint GetInstanceIndex()
{
    uint instanceIdx = 0U;
    if ((CORE_MATERIAL_FLAGS & CORE_MATERIAL_GPU_INSTANCING_BIT) == CORE_MATERIAL_GPU_INSTANCING_BIT) {
        instanceIdx = GetUnpackFlatIndicesInstanceIdx(inIndices);
    }
    return instanceIdx;
}

///////////////////////////////////////////////////////////////////////////////
// Forced LOD 0 sampling functions
vec4 GetBaseColorSampleLod0(const vec4 uvInput, const uint instanceIdx)
{
    const vec2 uv = GetFinalSamplingUV(
        uvInput, CORE_MATERIAL_TEXCOORD_INFO_BASE_BIT, CORE_MATERIAL_PACK_TEX_BASE_COLOR_UV_IDX, instanceIdx);
    return textureLod(uSampTextureBase, uv, 0.0);
}

vec3 GetNormalSampleLod0(const vec4 uvInput, const uint instanceIdx)
{
    const vec2 uv = GetFinalSamplingUV(
        uvInput, CORE_MATERIAL_TEXCOORD_INFO_NORMAL_BIT, CORE_MATERIAL_PACK_TEX_NORMAL_UV_IDX, instanceIdx);
    return textureLod(uSampTextures[CORE_MATERIAL_TEX_NORMAL_IDX], uv, 0.0).xyz;
}

vec4 GetMaterialSampleLod0(const vec4 uvInput, const uint instanceIdx)
{
    const vec2 uv = GetFinalSamplingUV(
        uvInput, CORE_MATERIAL_TEXCOORD_INFO_MATERIAL_BIT, CORE_MATERIAL_PACK_TEX_MATERIAL_UV_IDX, instanceIdx);
    return textureLod(uSampTextures[CORE_MATERIAL_TEX_MATERIAL_IDX], uv, 0.0);
}

vec3 GetEmissiveSampleLod0(const vec4 uvInput, const uint instanceIdx)
{
    const vec2 uv = GetFinalSamplingUV(
        uvInput, CORE_MATERIAL_TEXCOORD_INFO_EMISSIVE_BIT, CORE_MATERIAL_PACK_TEX_EMISSIVE_UV_IDX, instanceIdx);
    return textureLod(uSampTextures[CORE_MATERIAL_TEX_EMISSIVE_IDX], uv, 0.0).xyz;
}

float GetAOSampleLod0(const vec4 uvInput, const uint instanceIdx)
{
    const vec2 uv =
        GetFinalSamplingUV(uvInput, CORE_MATERIAL_TEXCOORD_INFO_AO_BIT, CORE_MATERIAL_PACK_TEX_AO_UV_IDX, instanceIdx);
    return textureLod(uSampTextures[CORE_MATERIAL_TEX_AO_IDX], uv, 0.0).x;
}

///////////////////////////////////////////////////////////////////////////////
// "main" functions

void UnlitBasic()
{
    const uint instanceIdx = GetInstanceIndex();
    CORE_RELAXEDP vec4 baseColor = GetBaseColorSample(inUv) * GetUnpackBaseColor(instanceIdx) * inColor;
    baseColor.a = clamp(baseColor.a, 0.0, 1.0);
    if ((CORE_MATERIAL_FLAGS & CORE_MATERIAL_ADDITIONAL_SHADER_DISCARD_BIT) ==
        CORE_MATERIAL_ADDITIONAL_SHADER_DISCARD_BIT) {
        if (baseColor.a < GetUnpackAlphaCutoff(instanceIdx)) {
            discard;
        }
    }
    if ((CORE_MATERIAL_FLAGS & CORE_MATERIAL_OPAQUE_BIT) == CORE_MATERIAL_OPAQUE_BIT) {
        baseColor.a = 1.0;
    }
    // write out only values which are needed
    // outColor = vec4(0.0);
    const uint cameraIdx = GetUnpackFlatIndicesCameraIdx(inIndices);
    outVelocityNormal =
        GetPackVelocityAndNormal(GetFinalCalculatedVelocity(inPos.xyz, inPrevPosI.xyz, cameraIdx), inNormal);
    outBaseColor = baseColor;

    const vec4 material = vec4(0.0);
    outMaterial = GetPackMaterialWithFlags(material, CORE_MATERIAL_TYPE, CORE_MATERIAL_FLAGS);
    outUv = vec4(inUv.xy, 0.0, 1.0);
    outGeomNormal = vec4(normalize(inNormal.xyz), -1.0);
    outTangentW = inTangentW;
}

void UnlitShadowAlpha()
{
    const uint instanceIdx = GetInstanceIndex();
    CORE_RELAXEDP vec4 baseColor = GetBaseColorSample(inUv, instanceIdx) * GetUnpackBaseColor(instanceIdx) * inColor;
    baseColor.a = clamp(baseColor.a, 0.0, 1.0);
    if ((CORE_MATERIAL_FLAGS & CORE_MATERIAL_ADDITIONAL_SHADER_DISCARD_BIT) ==
        CORE_MATERIAL_ADDITIONAL_SHADER_DISCARD_BIT) {
        if (baseColor.a < GetUnpackAlphaCutoff(instanceIdx)) {
            discard;
        }
    }
    if ((CORE_MATERIAL_FLAGS & CORE_MATERIAL_OPAQUE_BIT) == CORE_MATERIAL_OPAQUE_BIT) {
        baseColor.a = 1.0;
    }
    // NOTE: add material flags to baseColor alpha
    // write out only values which are needed
    // outColor = vec4(0.0);
    const uint cameraIdx = GetUnpackFlatIndicesCameraIdx(inIndices);
    outVelocityNormal =
        GetPackVelocityAndNormal(GetFinalCalculatedVelocity(inPos.xyz, inPrevPosI.xyz, cameraIdx), inNormal);
    outBaseColor = baseColor;

    const vec4 material = vec4(0.0);
    outMaterial = GetPackMaterialWithFlags(material, CORE_MATERIAL_TYPE, CORE_MATERIAL_FLAGS);
    outUv = vec4(inUv.xy, 0.0, 1.0);
    outGeomNormal = vec4(normalize(inNormal.xyz), -1.0);
    outTangentW = inTangentW;
}

void PbrBasic()
{
    const uint instanceIdx = GetInstanceIndex();
    // NOTE: by the spec with blend mode opaque alpha should be 1.0 from this calculation
    CORE_RELAXEDP vec4 baseColor = GetBaseColorSampleLod0(inUv, instanceIdx) * GetUnpackBaseColor(instanceIdx) * inColor;
    baseColor.a = clamp(baseColor.a, 0.0, 1.0);
    if ((CORE_MATERIAL_FLAGS & CORE_MATERIAL_ADDITIONAL_SHADER_DISCARD_BIT) ==
        CORE_MATERIAL_ADDITIONAL_SHADER_DISCARD_BIT) {
        if (baseColor.a < GetUnpackAlphaCutoff(instanceIdx)) {
            discard;
        }
    }
    if ((CORE_MATERIAL_FLAGS & CORE_MATERIAL_OPAQUE_BIT) == CORE_MATERIAL_OPAQUE_BIT) {
        baseColor.a = 1.0;
    }

    const vec3 normNormal = normalize(inNormal.xyz);
    const float normalScale = GetUnpackNormalScale(instanceIdx);
    vec3 N = normNormal;
    vec3 clearcoatN = normNormal;
    // clear coat normal is calculated if normal_map_bit and if clearcoat_bit
    if ((CORE_MATERIAL_FLAGS & CORE_MATERIAL_NORMAL_MAP_BIT) == CORE_MATERIAL_NORMAL_MAP_BIT) {
        N = GetNormalSampleLod0(inUv, instanceIdx);
        const mat3 tbn = CalcTbnMatrix(normNormal, inTangentW);
        N = CalcFinalNormal(tbn, N, normalScale);
        if ((CORE_MATERIAL_FLAGS & CORE_MATERIAL_CLEARCOAT_BIT) == CORE_MATERIAL_CLEARCOAT_BIT) {
            clearcoatN = GetClearcoatNormalSample(inUv, instanceIdx);
            const float ccNormalScale = GetUnpackClearcoatNormalScale(instanceIdx);
            clearcoatN = CalcFinalNormal(tbn, clearcoatN, ccNormalScale);
        }
    }
    // if no backface culling we flip automatically
    N = gl_FrontFacing ? N : -N;

    CORE_RELAXEDP vec4 material = GetMaterialSampleLod0(inUv, instanceIdx) * GetUnpackMaterial(instanceIdx);
    GetFinalCorrectedRoughness(normNormal, material.g);
    const CORE_RELAXEDP float ao = clamp(GetAOSampleLod0(inUv, instanceIdx) * GetUnpackAO(instanceIdx), 0.0, 1.0);

    // NOTE: one should write emissive to target and use it additively in lighting
    CORE_RELAXEDP vec3 emissive = GetEmissiveSampleLod0(inUv, instanceIdx) * GetUnpackEmissiveColor(instanceIdx);
    emissive = emissive * baseColor.a; // needs to be multiplied with alpha (premultiplied)

    // write out only values which are needed
    outColor = vec4(0.0);
    const uint cameraIdx = GetUnpackFlatIndicesCameraIdx(inIndices);
    const vec2 normalUv = GetFinalSamplingUV(
        inUv, CORE_MATERIAL_TEXCOORD_INFO_NORMAL_BIT, CORE_MATERIAL_PACK_TEX_NORMAL_UV_IDX, instanceIdx);
    outVelocityNormal = GetPackVelocityAndNormal(GetFinalCalculatedVelocity(inPos.xyz, inPrevPosI.xyz, cameraIdx), N);
    outBaseColor = GetPackBaseColorWithAo(baseColor.xyz, ao);
    outMaterial = GetPackMaterialWithFlags(material, CORE_MATERIAL_TYPE, CORE_MATERIAL_FLAGS);
    outUv = vec4(normalUv, 0.0, 1.0);
    outGeomNormal = vec4(normNormal,
        ((CORE_MATERIAL_FLAGS & CORE_MATERIAL_NORMAL_MAP_BIT) == CORE_MATERIAL_NORMAL_MAP_BIT) ? normalScale : -1.0);
    outTangentW = inTangentW;
}

/*
 * fragment shader for basic pbr materials.
 */
void main(void)
{
    if (CORE_MATERIAL_TYPE == CORE_MATERIAL_UNLIT) {
        UnlitBasic();
    } else if (CORE_MATERIAL_TYPE == CORE_MATERIAL_UNLIT_SHADOW_ALPHA) {
        UnlitShadowAlpha();
    } else {
        PbrBasic();
    }
}
