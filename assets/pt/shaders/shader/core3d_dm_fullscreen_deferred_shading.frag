#version 460 core
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// includes

#include "3d/shaders/common/3d_dm_indirect_lighting_common.h"
#include "3d/shaders/common/3d_dm_structures_common.h"
#include "3d/shaders/common/3d_dm_target_packing_common.h"
#include "render/shaders/common/render_color_conversion_common.h"
#include "render/shaders/common/render_post_process_common.h"
#include "render/shaders/common/render_tonemap_common.h"

// sets and specializations

#define ENABLE_INPUT_ATTACHMENTS 1
#include "3d/shaders/common/3d_dm_deferred_shading_frag_layout_common.h"
#include "3d/shaders/common/3d_dm_lighting_common.h"
#include "3d/shaders/common/3d_dm_inplace_fog_common.h"

// in / out

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

// input attachment: set=1 and binding=4 are defined in .shaderpl, binding=4 maps to inputAttachmentIndices[4] in .rng/.json
layout(input_attachment_index = 0, set = 1, binding = 4) uniform subpassInput uGBufferUv;
layout(input_attachment_index = 0, set = 1, binding = 5) uniform subpassInput uGBufferGeomNormal;
layout(input_attachment_index = 0, set = 1, binding = 6) uniform subpassInput uGBufferTangentW;
// resources: set=1 and binding=0/1 are both defined in .shaderpl, added via resources in .rng/.json
layout(set = 2, binding = 0) uniform texture2D uLRTexture;
layout(set = 2, binding = 1) uniform sampler uLRSamplerRepeat;
layout(set = 2, binding = 2) uniform texture2D uLRTextureAlias;
layout(set = 2, binding = 3) uniform uMaskUbo {
    uint uMaskFlags;
};
layout(set = 2, binding = 4, r11f_g11f_b10f) uniform image2D uPredictedColorOut;
layout(set = 2, binding = 5, rgba32f) uniform image2D uGradOut;

const uint MASK_BASE_COLOR = 1u << 0u;
const uint MASK_NORMAL     = 1u << 1u;
const uint MASK_MATERIAL   = 1u << 2u;
const uint MASK_EMISSIVE   = 1u << 3u;
const uint MASK_AO         = 1u << 4u;

// unpack gbuffer

struct FullGBufferData {
    CORE_RELAXEDP vec4 baseColor;
    vec3 normal;
    float ao;
    vec4 material;
    uint materialType;
    uint materialFlags;
};

FullGBufferData GetUnpackMaterialValues(const vec2 uv)
{
    FullGBufferData fd;
    fd.material = vec4(0.0, 1.0, 1.0, 0.04);
    fd.baseColor = vec4(0.0, 0.0, 0.0, 1.0);
    fd.normal = vec3(0.0, 1.0, 0.0);
    fd.ao = 1.0;
    fd.materialType = 0;
    fd.materialFlags = 0;

#if (ENABLE_INPUT_ATTACHMENTS == 1)
    GetUnpackMaterialWithFlags(subpassLoad(uGBufferMaterial), fd.material, fd.materialType, fd.materialFlags);
#else
    GetUnpackMaterialWithFlags(textureLod(uGBufferMaterial, uv, 0), fd.material, fd.materialType, fd.materialFlags);
#endif
    return fd;
}

void GetSampledGBuffer(const vec2 uv, inout FullGBufferData fd)
{
#if (ENABLE_INPUT_ATTACHMENTS == 1)
    GetUnpackBaseColorWithAo(subpassLoad(uGBufferBaseColor), fd.baseColor.rgb, fd.ao);

    vec2 vel;
    GetUnpackVelocityAndNormal(subpassLoad(uGBufferVelocityNormal).xyzw, vel, fd.normal);
    fd.normal = normalize(fd.normal);
#else
    GetUnpackBaseColorWithAo(textureLod(uGBufferBaseColor, uv, 0), fd.baseColor.rgb, fd.ao);

    vec2 vel;
    GetUnpackVelocityAndNormal(textureLod(uGBufferVelocityNormal, uv, 0), vel, fd.normal);
    fd.normal = normalize(fd.normal);
#endif
}

void GetSimpleSampledGBuffer(const vec2 uv, inout FullGBufferData fd)
{
#if (ENABLE_INPUT_ATTACHMENTS == 1)
    vec2 vel;
    GetUnpackVelocityAndNormal(subpassLoad(uGBufferVelocityNormal).xyzw, vel, fd.normal);
    fd.normal = normalize(fd.normal);
#else
    vec2 vel;
    GetUnpackVelocityAndNormal(textureLod(uGBufferVelocityNormal, uv, 0), vel, fd.normal);
    fd.normal = normalize(fd.normal);
#endif
}

float GetSampledDepthBuffer(const vec2 uv)
{
#if (ENABLE_INPUT_ATTACHMENTS == 1)
    return GetUnpackDepthBuffer(subpassLoad(uGBufferDepthBuffer).x);
#else
    return GetUnpackDepthBuffer(textureLod(uGBufferDepthBuffer, uv, 0).x);
#endif
}

vec4 GetSampledBaseColor(const vec2 uv)
{
    vec4 color = vec4(0.0);
#if (ENABLE_INPUT_ATTACHMENTS == 1)
    GetUnpackBaseColorWithAo(subpassLoad(uGBufferBaseColor), color.rgb, color.a);
#else
    GetUnpackBaseColorWithAo(textureLod(uGBufferBaseColor, uv, 0), color.rgb, color.a);
#endif
    return color;
}

vec3 GetWorldNormalFromSampledTangentNormal(const vec3 tangentNormal)
{
    const vec4 geomNormalScale = subpassLoad(uGBufferGeomNormal);
    const vec4 tangentW = subpassLoad(uGBufferTangentW);
    const vec3 geomNormal = normalize(geomNormalScale.xyz);
    const mat3 tbn = CalcTbnMatrix(geomNormal, tangentW);
    return CalcFinalNormal(tbn, tangentNormal, geomNormalScale.w);
}

bool HasGBufferNormalMap()
{
    return subpassLoad(uGBufferGeomNormal).w >= 0.0;
}

vec3 GetLRNormalSample()
{
    const vec4 gBufferUv = subpassLoad(uGBufferUv);
    return textureLod(sampler2D(uLRTexture, uLRSamplerRepeat), gBufferUv.xy, 0).xyz;
}

void ApplyLRNormal(inout FullGBufferData fd, const vec3 lrNormal)
{
    if (HasGBufferNormalMap()) {
        fd.normal = GetWorldNormalFromSampledTangentNormal(lrNormal);
    }
}

// end gbuffer

vec3 GetWorldPos(const uint cameraIdx, const float depthSample, const vec2 uv)
{
    mat4 projInv = uCameras[cameraIdx].projInv;

    vec4 sceneProj = vec4(uv.xy * 2.0 - 1.0, depthSample, 1.0);
    vec4 sceneView = uCameras[cameraIdx].viewProjInv * sceneProj;
    return sceneView.xyz / sceneView.w;
}

float CoreGetLodForRadianceSample(const float roughness)
{
    return uEnvironmentData.values.x * roughness;
}

vec3 CoreGetIrradianceSample(const vec3 worldNormal)
{
    const vec3 worldNormalEnv = mat3(uEnvironmentData.envRotation) * worldNormal;
    return unpackIblIrradianceSH(worldNormalEnv, uEnvironmentData.shIndirectCoefficients) *
           uEnvironmentData.indirectDiffuseColorFactor.rgb;
}

vec3 CoreGetRadianceSample(const vec3 worldReflect, const float roughness)
{
    const CORE_RELAXEDP float cubeLod = CoreGetLodForRadianceSample(roughness);
    const vec3 worldReflectEnv = mat3(uEnvironmentData.envRotation) * worldReflect;
    return unpackIblRadiance(textureLod(uSampRadiance, worldReflectEnv, cubeLod)) *
           uEnvironmentData.indirectSpecularColorFactor.rgb;
}

vec3 GetTransmissionRadianceSample(const vec2 fragUv, const vec3 worldReflect, const float roughness)
{
    // NOTE: this makes a pre color selection based on alpha
    // we would generally need an extra flag, the default texture is black with alpha zero
    const CORE_RELAXEDP float lod = CoreGetLodForRadianceSample(roughness);
    vec4 color = textureLod(uSampColorPrePass, fragUv, lod).rgba;
    if (color.a < 0.5f) {
        // sample environment if the default pre pass color was 0.0 alpha
        color.rgb = CoreGetRadianceSample(worldReflect, roughness);
    }
    return color.rgb;
}

///////////////////////////////////////////////////////////////////////////////
// "main" functions

vec4 UnlitBasic()
{
    const vec4 color = GetSampledBaseColor(inUv);

    // NOTE: fog missing

    return vec4(color.rgb, 1.0);
}

vec4 UnlitShadowAlpha(float depthBufferSample, FullGBufferData fd)
{
    GetSimpleSampledGBuffer(inUv, fd);

    const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
    const vec3 worldPos = GetWorldPos(cameraIdx, depthBufferSample, inUv.xy);
    const vec3 camWorldPos = uCameras[cameraIdx].viewInv[3].xyz;

    const uint directionalLightCount = uLightData.directionalLightCount;
    const uint directionalLightBeginIndex = uLightData.directionalLightBeginIndex;
    const vec4 atlasSizeInvSize = uLightData.atlasSizeInvSize;
    CORE_RELAXEDP float fullShadowCoeff = 1.0;
    for (uint lightIdx = 0; lightIdx < directionalLightCount; ++lightIdx) {
        const uint currLightIdx = directionalLightBeginIndex + lightIdx;
        const vec3 L = -uLightData.lights[currLightIdx].dir.xyz; // normalization already done in c-code
        const float NoL = clamp(dot(fd.normal, L), 0.0, 1.0);

        CORE_RELAXEDP float shadowCoeff = 1.0;
        const bool shadowReceiver = true;
        if (shadowReceiver) {
            const uvec4 lightFlags = uLightData.lights[currLightIdx].flags;
            if ((lightFlags.x & CORE_LIGHT_USAGE_SHADOW_LIGHT_BIT) == CORE_LIGHT_USAGE_SHADOW_LIGHT_BIT) {
                const vec4 shadowCoord = GetShadowMatrix(lightFlags.y) * vec4(worldPos.xyz, 1.0);
                const vec4 shadowFactors = uLightData.lights[currLightIdx].shadowFactors;
                if ((CORE_LIGHTING_FLAGS & CORE_LIGHTING_SHADOW_TYPE_VSM_BIT) == CORE_LIGHTING_SHADOW_TYPE_VSM_BIT) {
                    shadowCoeff = CalcVsmShadow(
                        uSampColorShadow, shadowCoord, NoL, shadowFactors, atlasSizeInvSize, lightFlags.zw);
                } else {
                    shadowCoeff = CalcPcfShadow(
                        uSampDepthShadow, shadowCoord, NoL, shadowFactors, atlasSizeInvSize, lightFlags.zw);
                }
            }
        }
        fullShadowCoeff *= shadowCoeff;
    }
    if ((CORE_LIGHTING_FLAGS & CORE_LIGHTING_SPOT_ENABLED_BIT) == CORE_LIGHTING_SPOT_ENABLED_BIT) {
        const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
        const uint spotLightCount = uLightData.spotLightCount;
        const uint spotLightLightBeginIndex = uLightData.spotLightBeginIndex;
        for (uint spotIdx = 0; spotIdx < spotLightCount; ++spotIdx) {
            const uint currLightIdx = spotLightLightBeginIndex + spotIdx;

            const vec3 pointToLight = uLightData.lights[currLightIdx].pos.xyz - worldPos.xyz;
            const float dist = length(pointToLight);
            const vec3 L = pointToLight / dist;
            const float NoL = clamp(dot(fd.normal, L), 0.0, 1.0);
            // NOTE: could check for NoL > 0.0 and NoV > 0.0
            CORE_RELAXEDP float shadowCoeff = 1.0;
            const bool shadowReceiver = true;
            if (shadowReceiver) {
                const uvec4 lightFlags = uLightData.lights[currLightIdx].flags;
                if ((lightFlags.x & CORE_LIGHT_USAGE_SHADOW_LIGHT_BIT) == CORE_LIGHT_USAGE_SHADOW_LIGHT_BIT) {
                    const vec4 shadowCoord = GetShadowMatrix(lightFlags.y) * vec4(worldPos.xyz, 1.0);
                    const vec4 shadowFactors = uLightData.lights[currLightIdx].shadowFactors;
                    if ((CORE_LIGHTING_FLAGS & CORE_LIGHTING_SHADOW_TYPE_VSM_BIT) ==
                        CORE_LIGHTING_SHADOW_TYPE_VSM_BIT) {
                        shadowCoeff = CalcVsmShadow(
                            uSampColorShadow, shadowCoord, NoL, shadowFactors, atlasSizeInvSize, lightFlags.zw);
                    } else {
                        shadowCoeff = CalcPcfShadow(
                            uSampDepthShadow, shadowCoord, NoL, shadowFactors, atlasSizeInvSize, lightFlags.zw);
                    }
                }
            }

            const float lightAngleScale = uLightData.lights[currLightIdx].spotLightParams.x;
            const float lightAngleOffset = uLightData.lights[currLightIdx].spotLightParams.y;
            // See: https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Khronos/KHR_lights_punctual
            const float cd = dot(uLightData.lights[currLightIdx].dir.xyz, -L);
            const float angularAttenuation = clamp(cd * lightAngleScale + lightAngleOffset, 0.0, 1.0);

            const float range = uLightData.lights[currLightIdx].dir.w;
            const float attenuation = max(min(1.0 - pow(dist / range, 4.0), 1.0), 0.0) / (dist * dist);
            const float intensity = uLightData.lights[currLightIdx].color.w;
            const float fullAttenuation = min(1.0, angularAttenuation * angularAttenuation * attenuation * intensity);

            fullShadowCoeff *= (1.0 - (1.0 - shadowCoeff) * fullAttenuation);
        }
    }
    CORE_RELAXEDP vec3 color = fd.baseColor.rgb * clamp(1.0 - fullShadowCoeff, 0.0, 1.0);

    // fog handling
    InplaceFogBlock(CORE_CAMERA_FLAGS, worldPos, camWorldPos.xyz, vec4(color, 1.0), color);

    return vec4(color.xyz, 1.0);
}

vec4 PbrBasic(float depthBufferSample, FullGBufferData fd)
{
    GetSampledGBuffer(inUv, fd);

    // should always be metallic roughness
    InputBrdfData brdfData = CalcBRDFMetallicRoughness(fd.baseColor, fd.material);

    const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
    const vec3 worldPos = GetWorldPos(cameraIdx, depthBufferSample, inUv.xy);
    const vec3 camWorldPos = uCameras[cameraIdx].viewInv[3].xyz;

    const vec3 V = normalize(camWorldPos - worldPos);
    const float NoV = clamp(dot(fd.normal, V), CORE3D_PBR_LIGHTING_EPSILON, 1.0);

    ShadingData shadingData;
    shadingData.pos = worldPos;
    shadingData.N = fd.normal;
    shadingData.NoV = NoV;
    shadingData.V = V;
    shadingData.f0 = brdfData.f0;
    shadingData.alpha2 = brdfData.alpha2;
    shadingData.diffuseColor = brdfData.diffuseColor;
    CORE_RELAXEDP const float roughness = brdfData.roughness;

    vec3 color = vec3(0.0); // brdfData.diffuseColor
    if ((fd.materialFlags & CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) {
        color = CalculateLighting(shadingData, fd.materialFlags);
    }

    if ((fd.materialFlags & CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) {
        // lambert baked into irradianceSample (SH)
        CORE_RELAXEDP vec3 irradiance = CoreGetIrradianceSample(shadingData.N) * shadingData.diffuseColor * fd.ao;

        const vec3 worldReflect = reflect(-shadingData.V, shadingData.N);
        const CORE_RELAXEDP vec3 fIndirect = EnvBRDFApprox(shadingData.f0.xyz, roughness, NoV);
        // ao applied after clear coat
        CORE_RELAXEDP vec3 radianceSample = CoreGetRadianceSample(worldReflect, roughness);
        CORE_RELAXEDP vec3 radiance = radianceSample * fIndirect;
        // apply ao for indirect specular as well (cheap version)
#if 1
        radiance *= fd.ao * SpecularHorizonOcclusion(worldReflect, fd.normal);
#else
        radiance *= EnvSpecularAo(fd.ao, NoV, roughness) * SpecularHorizonOcclusion(worldReflect, normNormal);
#endif

        color += (irradiance + radiance);
    }

    // fog handling
    InplaceFogBlock(CORE_CAMERA_FLAGS, worldPos.xyz, camWorldPos.xyz, vec4(color, 1.0), color);

    color.rgb = clamp(color.rgb, 0.0, CORE_HDR_FLOAT_CLAMP_MAX_VALUE); // zero to hdr max
    return vec4(color.rgb, 1.0);
}

vec4 PbrBasicWithLRBaseColor(float depthBufferSample, FullGBufferData fd)
{
    GetSampledGBuffer(inUv, fd);

    // Sample base color from low resolution texture using uv stored in G-Buffer, replacing base color from G-Buffer
    vec4 GBufferUv = subpassLoad(uGBufferUv);
    CORE_RELAXEDP vec4 LRBaseColor = textureLod(sampler2D(uLRTexture, uLRSamplerRepeat), GBufferUv.xy, 0);
    vec4 baseColor = vec4(LRBaseColor.rgb, fd.baseColor.a);  // channel a is from AO not albedo
    // should always be metallic roughness
    InputBrdfData brdfData = CalcBRDFMetallicRoughness(baseColor, fd.material);

    const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
    const vec3 worldPos = GetWorldPos(cameraIdx, depthBufferSample, inUv.xy);
    const vec3 camWorldPos = uCameras[cameraIdx].viewInv[3].xyz;

    const vec3 V = normalize(camWorldPos - worldPos);
    const float NoV = clamp(dot(fd.normal, V), CORE3D_PBR_LIGHTING_EPSILON, 1.0);

    ShadingData shadingData;
    shadingData.pos = worldPos;
    shadingData.N = fd.normal;
    shadingData.NoV = NoV;
    shadingData.V = V;
    shadingData.f0 = brdfData.f0;
    shadingData.alpha2 = brdfData.alpha2;
    shadingData.diffuseColor = brdfData.diffuseColor;
    CORE_RELAXEDP const float roughness = brdfData.roughness;

    vec3 color = vec3(0.0); // brdfData.diffuseColor
    if ((fd.materialFlags & CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) {
        color = CalculateLighting(shadingData, fd.materialFlags);
    }

    if ((fd.materialFlags & CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) {
        // lambert baked into irradianceSample (SH)
        CORE_RELAXEDP vec3 irradiance = CoreGetIrradianceSample(shadingData.N) * shadingData.diffuseColor * fd.ao;

        const vec3 worldReflect = reflect(-shadingData.V, shadingData.N);
        const CORE_RELAXEDP vec3 fIndirect = EnvBRDFApprox(shadingData.f0.xyz, roughness, NoV);
        // ao applied after clear coat
        CORE_RELAXEDP vec3 radianceSample = CoreGetRadianceSample(worldReflect, roughness);
        CORE_RELAXEDP vec3 radiance = radianceSample * fIndirect;
        // apply ao for indirect specular as well (cheap version)
#if 1
        radiance *= fd.ao * SpecularHorizonOcclusion(worldReflect, fd.normal);
#else
        radiance *= EnvSpecularAo(fd.ao, NoV, roughness) * SpecularHorizonOcclusion(worldReflect, normNormal);
#endif

        color += (irradiance + radiance);
    }

    // fog handling
    InplaceFogBlock(CORE_CAMERA_FLAGS, worldPos.xyz, camWorldPos.xyz, vec4(color, 1.0), color);

    color.rgb = clamp(color.rgb, 0.0, CORE_HDR_FLOAT_CLAMP_MAX_VALUE); // zero to hdr max
    return vec4(color.rgb, 1.0);
}

vec4 PbrBasicWithLRNormal(float depthBufferSample, FullGBufferData fd)
{
    GetSampledGBuffer(inUv, fd);
    ApplyLRNormal(fd, GetLRNormalSample());

    // Keep base color and material from the G-Buffer; only replace normal with the LR normal path.
    vec4 baseColor = fd.baseColor;
    InputBrdfData brdfData = CalcBRDFMetallicRoughness(baseColor, fd.material);

    const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
    const vec3 worldPos = GetWorldPos(cameraIdx, depthBufferSample, inUv.xy);
    const vec3 camWorldPos = uCameras[cameraIdx].viewInv[3].xyz;

    const vec3 V = normalize(camWorldPos - worldPos);
    const float NoV = clamp(dot(fd.normal, V), CORE3D_PBR_LIGHTING_EPSILON, 1.0);

    ShadingData shadingData;
    shadingData.pos = worldPos;
    shadingData.N = fd.normal;
    shadingData.NoV = NoV;
    shadingData.V = V;
    shadingData.f0 = brdfData.f0;
    shadingData.alpha2 = brdfData.alpha2;
    shadingData.diffuseColor = brdfData.diffuseColor;
    CORE_RELAXEDP const float roughness = brdfData.roughness;

    vec3 color = vec3(0.0);
    if ((fd.materialFlags & CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) {
        color = CalculateLighting(shadingData, fd.materialFlags);
    }

    if ((fd.materialFlags & CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) {
        CORE_RELAXEDP vec3 irradiance = CoreGetIrradianceSample(shadingData.N) * shadingData.diffuseColor * fd.ao;

        const vec3 worldReflect = reflect(-shadingData.V, shadingData.N);
        const CORE_RELAXEDP vec3 fIndirect = EnvBRDFApprox(shadingData.f0.xyz, roughness, NoV);
        CORE_RELAXEDP vec3 radianceSample = CoreGetRadianceSample(worldReflect, roughness);
        CORE_RELAXEDP vec3 radiance = radianceSample * fIndirect;
        radiance *= fd.ao * SpecularHorizonOcclusion(worldReflect, fd.normal);

        color += (irradiance + radiance);
    }

    InplaceFogBlock(CORE_CAMERA_FLAGS, worldPos.xyz, camWorldPos.xyz, vec4(color, 1.0), color);

    color.rgb = clamp(color.rgb, 0.0, CORE_HDR_FLOAT_CLAMP_MAX_VALUE);
    return vec4(color.rgb, 1.0);
}

// ================== BACKWARD PASS FUNCTIONS ==================

// Per-light gradient extraction (mirrors CalculateLight in 3d_dm_lighting_common.h)
// Returns per-light contributions to diffuseLightAccum and specularDGAccum via out params.
void CalculateLightBackward(
    uint currLightIdx, vec3 materialDiffuseBRDF, vec3 L, float NoL,
    ShadingData sd,
    out vec3 diffLight, out vec3 specDGLight)
{
    const vec3 H = normalize(L + sd.V);
    const float VoH = clamp(dot(sd.V, H), 0.0, 1.0);
    const float NoH = clamp(dot(sd.N, H), 0.0, 1.0);

    const float D = dGGX(sd.alpha2, NoH);
    const float G = vGGXWithCombinedDenominator(sd.alpha2, sd.NoV, NoL);
    const vec3 F = fSchlick(sd.f0, VoH);
    const float p = pow(1.0 - VoH, 5.0);

    // w_i = NoL * lightColor (attenuation applied externally)
    vec3 w = NoL * uLightData.lights[currLightIdx].color.xyz;

    // dC/d(diffuseColor) contribution: (1-F) * (1/pi) * w
    diffLight = (1.0 - F) * dLambert() * w;
    // dC/d(f0) contribution: (1-p) * (D*G - materialDiffuseBRDF) * w
    specDGLight = (1.0 - p) * (vec3(D * G) - materialDiffuseBRDF) * w;
}

// Lighting backward: mirrors CalculateLighting loop structure for directional/spot/point lights.
// Outputs accumulated gradient intermediates.
void CalculateLightingBackward(ShadingData sd, const uint materialFlags,
    out vec3 diffuseLightAccum, out vec3 specularDGAccum)
{
    const vec3 materialDiffuseBRDF = sd.diffuseColor * diffuseCoeff();
    diffuseLightAccum = vec3(0.0);
    specularDGAccum = vec3(0.0);
    vec3 diffLight, specDGLight;

    const vec4 atlasSizeInvSize = uLightData.atlasSizeInvSize;

    // --- Directional lights ---
    const uint dirCount = uLightData.directionalLightCount;
    const uint dirBegin = uLightData.directionalLightBeginIndex;
    for (uint i = 0; i < dirCount; ++i) {
        const uint idx = dirBegin + i;
        const vec3 L = -uLightData.lights[idx].dir.xyz;
        const float NoL = clamp(dot(sd.N, L), 0.0, 1.0);

        CORE_RELAXEDP float shadowCoeff = 1.0;
        if ((materialFlags & CORE_MATERIAL_SHADOW_RECEIVER_BIT) == CORE_MATERIAL_SHADOW_RECEIVER_BIT) {
            const uvec4 lf = uLightData.lights[idx].flags;
            if ((lf.x & CORE_LIGHT_USAGE_SHADOW_LIGHT_BIT) == CORE_LIGHT_USAGE_SHADOW_LIGHT_BIT) {
                const vec4 sc = GetShadowMatrix(lf.y) * vec4(sd.pos.xyz, 1.0);
                const vec4 sf = uLightData.lights[idx].shadowFactors;
                if ((CORE_LIGHTING_FLAGS & CORE_LIGHTING_SHADOW_TYPE_VSM_BIT) == CORE_LIGHTING_SHADOW_TYPE_VSM_BIT) {
                    shadowCoeff = CalcVsmShadow(uSampColorShadow, sc, NoL, sf, atlasSizeInvSize, lf.zw);
                } else {
                    shadowCoeff = CalcPcfShadow(uSampDepthShadow, sc, NoL, sf, atlasSizeInvSize, lf.zw);
                }
            }
        }
        CalculateLightBackward(idx, materialDiffuseBRDF, L, NoL, sd, diffLight, specDGLight);
        diffuseLightAccum += diffLight * shadowCoeff;
        specularDGAccum += specDGLight * shadowCoeff;
    }

    // --- Spot lights ---
    if ((CORE_LIGHTING_FLAGS & CORE_LIGHTING_SPOT_ENABLED_BIT) == CORE_LIGHTING_SPOT_ENABLED_BIT) {
        const uint spotCount = uLightData.spotLightCount;
        const uint spotBegin = uLightData.spotLightBeginIndex;
        for (uint i = 0; i < spotCount; ++i) {
            const uint idx = spotBegin + i;
            const vec3 ptl = uLightData.lights[idx].pos.xyz - sd.pos.xyz;
            const float dist = length(ptl);
            const vec3 L = ptl / dist;
            const float NoL = clamp(dot(sd.N, L), 0.0, 1.0);

            CORE_RELAXEDP float shadowCoeff = 1.0;
            if ((materialFlags & CORE_MATERIAL_SHADOW_RECEIVER_BIT) == CORE_MATERIAL_SHADOW_RECEIVER_BIT) {
                const uvec4 lf = uLightData.lights[idx].flags;
                if ((lf.x & CORE_LIGHT_USAGE_SHADOW_LIGHT_BIT) == CORE_LIGHT_USAGE_SHADOW_LIGHT_BIT) {
                    const vec4 sc = GetShadowMatrix(lf.y) * vec4(sd.pos.xyz, 1.0);
                    const vec4 sf = uLightData.lights[idx].shadowFactors;
                    if ((CORE_LIGHTING_FLAGS & CORE_LIGHTING_SHADOW_TYPE_VSM_BIT) == CORE_LIGHTING_SHADOW_TYPE_VSM_BIT) {
                        shadowCoeff = CalcVsmShadow(uSampColorShadow, sc, NoL, sf, atlasSizeInvSize, lf.zw);
                    } else {
                        shadowCoeff = CalcPcfShadow(uSampDepthShadow, sc, NoL, sf, atlasSizeInvSize, lf.zw);
                    }
                }
            }
            const float las = uLightData.lights[idx].spotLightParams.x;
            const float lao = uLightData.lights[idx].spotLightParams.y;
            const float cd = dot(uLightData.lights[idx].dir.xyz, -L);
            const float angAtt = clamp(cd * las + lao, 0.0, 1.0);
            const float range = uLightData.lights[idx].dir.w;
            const float distAtt = max(min(1.0 - pow(dist / range, 4.0), 1.0), 0.0) / (dist * dist);
            const float att = angAtt * angAtt * distAtt;

            CalculateLightBackward(idx, materialDiffuseBRDF, L, NoL, sd, diffLight, specDGLight);
            diffuseLightAccum += diffLight * att * shadowCoeff;
            specularDGAccum += specDGLight * att * shadowCoeff;
        }
    }

    // --- Point lights ---
    if ((CORE_LIGHTING_FLAGS & CORE_LIGHTING_POINT_ENABLED_BIT) == CORE_LIGHTING_POINT_ENABLED_BIT) {
        const uint ptCount = uLightData.pointLightCount;
        const uint ptBegin = uLightData.pointLightBeginIndex;
        for (uint i = 0; i < ptCount; ++i) {
            const uint idx = ptBegin + i;
            const vec3 ptl = uLightData.lights[idx].pos.xyz - sd.pos.xyz;
            const float dist = length(ptl);
            const vec3 L = ptl / dist;
            const float NoL = clamp(dot(sd.N, L), 0.0, 1.0);
            const float range = uLightData.lights[idx].dir.w;
            const float att = max(min(1.0 - pow(dist / range, 4.0), 1.0), 0.0) / (dist * dist);

            CalculateLightBackward(idx, materialDiffuseBRDF, L, NoL, sd, diffLight, specDGLight);
            diffuseLightAccum += diffLight * att;
            specularDGAccum += specDGLight * att;
        }
    }
}

// Compute dPredictedColor/dLRBaseColor for the current pixel.
// referenceColor is outColor (from high-res PbrBasic), used to compute dLoss/dC.
vec3 BackwardPbrBasicWithLRBaseColor(float depthBufferSample, FullGBufferData fd,
    vec3 predictedRGB, vec3 referenceRGB)
{
    GetSampledGBuffer(inUv, fd);

    // --- Reconstruct same inputs as PbrBasicWithLRBaseColor ---
    vec4 GBufferUv = subpassLoad(uGBufferUv);
    vec3 LRBaseColor = textureLod(sampler2D(uLRTexture, uLRSamplerRepeat), GBufferUv.xy, 0).rgb;
    float m = clamp(fd.material.b, 0.0, 1.0);
    float r0 = fd.material.a; // typically 0.04
    InputBrdfData brdfData = CalcBRDFMetallicRoughness(vec4(LRBaseColor, 1.0), fd.material);

    const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
    const vec3 worldPos = GetWorldPos(cameraIdx, depthBufferSample, inUv.xy);
    const vec3 camWorldPos = uCameras[cameraIdx].viewInv[3].xyz;
    const vec3 V = normalize(camWorldPos - worldPos);
    const float NoV = clamp(dot(fd.normal, V), CORE3D_PBR_LIGHTING_EPSILON, 1.0);
    CORE_RELAXEDP const float roughness = brdfData.roughness;

    ShadingData shadingData;
    shadingData.pos = worldPos;
    shadingData.N = fd.normal;
    shadingData.NoV = NoV;
    shadingData.V = V;
    shadingData.f0 = brdfData.f0;
    shadingData.alpha2 = brdfData.alpha2;
    shadingData.diffuseColor = brdfData.diffuseColor;

    // === Step 1: Accumulate direct lighting gradient intermediates ===
    vec3 diffuseLightAccum = vec3(0.0);
    vec3 specularDGAccum = vec3(0.0);
    if ((fd.materialFlags & CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) {
        CalculateLightingBackward(shadingData, fd.materialFlags, diffuseLightAccum, specularDGAccum);
    }

    // === Step 2: Add indirect lighting (IBL) gradient ===
    if ((fd.materialFlags & CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) {
        // Diffuse IBL: irradiance = CoreGetIrradianceSample(N) * diffuseColor * ao
        // dIrradiance/d(diffuseColor) = CoreGetIrradianceSample(N) * ao
        vec3 irradianceSample = CoreGetIrradianceSample(shadingData.N);
        diffuseLightAccum += irradianceSample * fd.ao;

        // Specular IBL: radiance = radianceSample * EnvBRDFApprox(f0, roughness, NoV) * ao * horizonOcc
        // dRadiance/d(f0) = radianceSample * d(EnvBRDFApprox)/d(f0) * ao * horizonOcc
        // When f90 is saturated (common), d(EnvBRDFApprox)/d(f0) ≈ ab.x
        const vec3 worldReflect = reflect(-shadingData.V, shadingData.N);
        vec3 radianceSample = CoreGetRadianceSample(worldReflect, roughness);
        float horizonOcc = fd.ao * SpecularHorizonOcclusion(worldReflect, fd.normal);

        // Recompute ab.x from EnvBRDFApprox internals
        const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
        const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
        vec4 r = roughness * c0 + c1;
        float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
        float ab_x = -1.04 * a004 + r.z; // ab.x

        specularDGAccum += ab_x * radianceSample * horizonOcc;
    }

    // === Step 3: Chain rule through CalcBRDFMetallicRoughness ===
    // d(diffuseColor)/dB = (1-m) * [(1 - (1-m)*r0) - 2*m*B]  (element-wise for B term)
    vec3 dDc_dB = (1.0 - m) * (vec3(1.0 - (1.0 - m) * r0) - 2.0 * m * LRBaseColor);
    // d(f0)/dB = m

    // dC/dB = diffuseLightAccum * dDc_dB + specularDGAccum * m
    vec3 dC_dB = diffuseLightAccum * dDc_dB + specularDGAccum * m;

    // === Step 4: Multiply by dLoss/dC = (predicted - reference) ===
    vec3 dL_dC = predictedRGB - referenceRGB;
    vec3 gradient = dL_dC * dC_dB;

    return gradient;
}

vec3 EvaluatePbrForBackward(FullGBufferData fd, vec4 baseColor, vec3 N, vec4 material,
    float ao, vec3 worldPos, vec3 V)
{
    InputBrdfData brdfData = CalcBRDFMetallicRoughness(baseColor, material);
    const float NoV = clamp(dot(N, V), CORE3D_PBR_LIGHTING_EPSILON, 1.0);

    ShadingData shadingData;
    shadingData.pos = worldPos;
    shadingData.N = N;
    shadingData.NoV = NoV;
    shadingData.V = V;
    shadingData.f0 = brdfData.f0;
    shadingData.alpha2 = brdfData.alpha2;
    shadingData.diffuseColor = brdfData.diffuseColor;

    vec3 color = vec3(0.0);
    if ((fd.materialFlags & CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) {
        color = CalculateLighting(shadingData, fd.materialFlags);
    }

    if ((fd.materialFlags & CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) {
        vec3 irradiance = CoreGetIrradianceSample(N) * brdfData.diffuseColor * ao;
        const vec3 worldReflect = reflect(-V, N);
        vec3 radiance = CoreGetRadianceSample(worldReflect, brdfData.roughness) *
            EnvBRDFApprox(brdfData.f0.xyz, brdfData.roughness, NoV);
        radiance *= ao * SpecularHorizonOcclusion(worldReflect, N);
        color += irradiance + radiance;
    }

    return color;
}

vec3 GetLRBaseColorForBackward(FullGBufferData fd)
{
    vec4 GBufferUv = subpassLoad(uGBufferUv);
    return textureLod(sampler2D(uLRTexture, uLRSamplerRepeat), GBufferUv.xy, 0).rgb;
}

vec3 BackwardPbrBasicWithLRNormal(float depthBufferSample, FullGBufferData fd,
    vec3 predictedRGB, vec3 referenceRGB)
{
    GetSampledGBuffer(inUv, fd);
    if (!HasGBufferNormalMap()) {
        return vec3(0.0);
    }

    const vec4 baseColor = fd.baseColor;
    const vec3 LRNormal = GetLRNormalSample();

    const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
    const vec3 worldPos = GetWorldPos(cameraIdx, depthBufferSample, inUv.xy);
    const vec3 V = normalize(uCameras[cameraIdx].viewInv[3].xyz - worldPos);

    const float eps = 0.002;
    const vec3 dL_dC = predictedRGB - referenceRGB;
    vec3 grad = vec3(0.0);

    vec3 normalPlus = LRNormal;
    vec3 normalMinus = LRNormal;

    normalPlus.x = clamp(LRNormal.x + eps, 0.0, 1.0);
    normalMinus.x = clamp(LRNormal.x - eps, 0.0, 1.0);
    vec3 colorPlus = EvaluatePbrForBackward(
        fd, baseColor, GetWorldNormalFromSampledTangentNormal(normalPlus), fd.material, fd.ao, worldPos, V);
    vec3 colorMinus = EvaluatePbrForBackward(
        fd, baseColor, GetWorldNormalFromSampledTangentNormal(normalMinus), fd.material, fd.ao, worldPos, V);
    grad.x = dot(dL_dC, (colorPlus - colorMinus) / max(normalPlus.x - normalMinus.x, CORE3D_PBR_LIGHTING_EPSILON));

    normalPlus = LRNormal;
    normalMinus = LRNormal;
    normalPlus.y = clamp(LRNormal.y + eps, 0.0, 1.0);
    normalMinus.y = clamp(LRNormal.y - eps, 0.0, 1.0);
    colorPlus = EvaluatePbrForBackward(
        fd, baseColor, GetWorldNormalFromSampledTangentNormal(normalPlus), fd.material, fd.ao, worldPos, V);
    colorMinus = EvaluatePbrForBackward(
        fd, baseColor, GetWorldNormalFromSampledTangentNormal(normalMinus), fd.material, fd.ao, worldPos, V);
    grad.y = dot(dL_dC, (colorPlus - colorMinus) / max(normalPlus.y - normalMinus.y, CORE3D_PBR_LIGHTING_EPSILON));

    normalPlus = LRNormal;
    normalMinus = LRNormal;
    normalPlus.z = clamp(LRNormal.z + eps, 0.0, 1.0);
    normalMinus.z = clamp(LRNormal.z - eps, 0.0, 1.0);
    colorPlus = EvaluatePbrForBackward(
        fd, baseColor, GetWorldNormalFromSampledTangentNormal(normalPlus), fd.material, fd.ao, worldPos, V);
    colorMinus = EvaluatePbrForBackward(
        fd, baseColor, GetWorldNormalFromSampledTangentNormal(normalMinus), fd.material, fd.ao, worldPos, V);
    grad.z = dot(dL_dC, (colorPlus - colorMinus) / max(normalPlus.z - normalMinus.z, CORE3D_PBR_LIGHTING_EPSILON));

    return grad;
}

vec3 BackwardPbrNormal(float depthBufferSample, FullGBufferData fd,
    vec3 predictedRGB, vec3 referenceRGB)
{
    GetSampledGBuffer(inUv, fd);

    vec4 baseColor = vec4(GetLRBaseColorForBackward(fd), fd.baseColor.a);
    const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
    const vec3 worldPos = GetWorldPos(cameraIdx, depthBufferSample, inUv.xy);
    const vec3 V = normalize(uCameras[cameraIdx].viewInv[3].xyz - worldPos);

    const float eps = 0.002;
    const float inv2e = 1.0 / (2.0 * eps);
    vec3 N = normalize(fd.normal);
    vec3 Cpx = EvaluatePbrForBackward(fd, baseColor, normalize(N + vec3(eps, 0.0, 0.0)), fd.material, fd.ao, worldPos, V);
    vec3 Cmx = EvaluatePbrForBackward(fd, baseColor, normalize(N - vec3(eps, 0.0, 0.0)), fd.material, fd.ao, worldPos, V);
    vec3 Cpy = EvaluatePbrForBackward(fd, baseColor, normalize(N + vec3(0.0, eps, 0.0)), fd.material, fd.ao, worldPos, V);
    vec3 Cmy = EvaluatePbrForBackward(fd, baseColor, normalize(N - vec3(0.0, eps, 0.0)), fd.material, fd.ao, worldPos, V);
    vec3 Cpz = EvaluatePbrForBackward(fd, baseColor, normalize(N + vec3(0.0, 0.0, eps)), fd.material, fd.ao, worldPos, V);
    vec3 Cmz = EvaluatePbrForBackward(fd, baseColor, normalize(N - vec3(0.0, 0.0, eps)), fd.material, fd.ao, worldPos, V);

    vec3 dL_dC = predictedRGB - referenceRGB;
    return vec3(
        dot(dL_dC, (Cpx - Cmx) * inv2e),
        dot(dL_dC, (Cpy - Cmy) * inv2e),
        dot(dL_dC, (Cpz - Cmz) * inv2e));
}

float BackwardPbrAO(float depthBufferSample, FullGBufferData fd,
    vec3 predictedRGB, vec3 referenceRGB)
{
    GetSampledGBuffer(inUv, fd);
    if ((fd.materialFlags & CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) != CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) {
        return 0.0;
    }

    vec4 baseColor = vec4(GetLRBaseColorForBackward(fd), fd.baseColor.a);
    InputBrdfData brdfData = CalcBRDFMetallicRoughness(baseColor, fd.material);

    const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
    const vec3 worldPos = GetWorldPos(cameraIdx, depthBufferSample, inUv.xy);
    const vec3 V = normalize(uCameras[cameraIdx].viewInv[3].xyz - worldPos);
    const vec3 N = normalize(fd.normal);
    const float NoV = clamp(dot(N, V), CORE3D_PBR_LIGHTING_EPSILON, 1.0);

    vec3 dColor_dAO = CoreGetIrradianceSample(N) * brdfData.diffuseColor;
    const vec3 worldReflect = reflect(-V, N);
    dColor_dAO += CoreGetRadianceSample(worldReflect, brdfData.roughness) *
        EnvBRDFApprox(brdfData.f0.xyz, brdfData.roughness, NoV) *
        SpecularHorizonOcclusion(worldReflect, N);

    return dot(predictedRGB - referenceRGB, dColor_dAO);
}

vec4 BackwardPbrMaterial(float depthBufferSample, FullGBufferData fd,
    vec3 predictedRGB, vec3 referenceRGB)
{
    GetSampledGBuffer(inUv, fd);

    vec4 baseColor = vec4(GetLRBaseColorForBackward(fd), fd.baseColor.a);
    const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
    const vec3 worldPos = GetWorldPos(cameraIdx, depthBufferSample, inUv.xy);
    const vec3 V = normalize(uCameras[cameraIdx].viewInv[3].xyz - worldPos);
    const vec3 N = normalize(fd.normal);
    const vec3 dL_dC = predictedRGB - referenceRGB;
    const float eps = 0.002;

    vec4 matPlus = fd.material;
    vec4 matMinus = fd.material;

    matPlus.g = clamp(fd.material.g + eps, CORE_BRDF_MIN_ROUGHNESS, 1.0);
    matMinus.g = clamp(fd.material.g - eps, CORE_BRDF_MIN_ROUGHNESS, 1.0);
    vec3 cPlus = EvaluatePbrForBackward(fd, baseColor, N, matPlus, fd.ao, worldPos, V);
    vec3 cMinus = EvaluatePbrForBackward(fd, baseColor, N, matMinus, fd.ao, worldPos, V);
    float gradRoughness = dot(dL_dC, (cPlus - cMinus) / max(matPlus.g - matMinus.g, CORE3D_PBR_LIGHTING_EPSILON));

    matPlus = fd.material;
    matMinus = fd.material;
    matPlus.b = clamp(fd.material.b + eps, 0.0, 1.0);
    matMinus.b = clamp(fd.material.b - eps, 0.0, 1.0);
    cPlus = EvaluatePbrForBackward(fd, baseColor, N, matPlus, fd.ao, worldPos, V);
    cMinus = EvaluatePbrForBackward(fd, baseColor, N, matMinus, fd.ao, worldPos, V);
    float gradMetallic = dot(dL_dC, (cPlus - cMinus) / max(matPlus.b - matMinus.b, CORE3D_PBR_LIGHTING_EPSILON));

    matPlus = fd.material;
    matMinus = fd.material;
    matPlus.a = clamp(fd.material.a + eps, 0.0, 1.0);
    matMinus.a = clamp(fd.material.a - eps, 0.0, 1.0);
    cPlus = EvaluatePbrForBackward(fd, baseColor, N, matPlus, fd.ao, worldPos, V);
    cMinus = EvaluatePbrForBackward(fd, baseColor, N, matMinus, fd.ao, worldPos, V);
    float gradSpecularF0 = dot(dL_dC, (cPlus - cMinus) / max(matPlus.a - matMinus.a, CORE3D_PBR_LIGHTING_EPSILON));

    return vec4(0.0, gradRoughness, gradMetallic, gradSpecularF0);
}

// Compute dLoss/dLREmissive for the current pixel.
// Emissive is simply added to the final color: color += LREmissive,
// so dColor/dLREmissive = I (identity). The gradient is just dLoss/dC.
vec3 BackwardPbrBasicWithLREmissive(float depthBufferSample, FullGBufferData fd,
    vec3 predictedRGB, vec3 referenceRGB)
{
    // Emissive contributes additively to the final color:
    //   finalColor = pbrLighting + LREmissive
    // Therefore: dFinalColor/dLREmissive = vec3(1.0)
    // And: dLoss/dLREmissive = dLoss/dC * dC/dLREmissive = dLoss/dC * 1
    vec3 dL_dC = predictedRGB - referenceRGB;
    return dL_dC;
}

// Compute dLoss/dLRMetallicRoughness for the current pixel.
// LR texture stores (metallic, roughness) in (r, g) channels.
// These affect the BRDF through CalcBRDFMetallicRoughness:
//   f0 = mix(r0, baseColor, m)
//   diffuseColor = mix(baseColor * (1 - f0), 0, m)
//   roughness -> alpha2 = (roughness^2)^2
// We differentiate through all lighting contributions analytically.
vec3 BackwardPbrBasicWithLRmetallicRoughness(float depthBufferSample, FullGBufferData fd,
    vec3 predictedRGB, vec3 referenceRGB)
{
    GetSampledGBuffer(inUv, fd);

    // --- Reconstruct inputs (mirrors PbrBasicWithLRBaseColor) ---
    vec4 GBufferUv = subpassLoad(uGBufferUv);
    // LR metallic-roughness texture: .r = metallic, .g = roughness
    vec2 LRMetallicRoughness = textureLod(sampler2D(uLRTexture, uLRSamplerRepeat), GBufferUv.xy, 0).rg;
    float m = clamp(LRMetallicRoughness.r, 0.0, 1.0);  // metallic from LR
    float r = clamp(LRMetallicRoughness.g, CORE_BRDF_MIN_ROUGHNESS, 1.0); // roughness from LR
    float r0 = fd.material.a; // dielectric reflectance (typically 0.04)
    vec3 B = fd.baseColor.rgb; // base color from G-Buffer (not optimized here)

    // Reconstruct BRDF data with LR metallic/roughness
    vec4 matOverride = fd.material;
    matOverride.b = m;
    matOverride.g = r;
    InputBrdfData brdfData = CalcBRDFMetallicRoughness(fd.baseColor, matOverride);

    const uint cameraIdx = GetUnpackCameraIndex(uGeneralData);
    const vec3 worldPos = GetWorldPos(cameraIdx, depthBufferSample, inUv.xy);
    const vec3 camWorldPos = uCameras[cameraIdx].viewInv[3].xyz;
    const vec3 V = normalize(camWorldPos - worldPos);
    const float NoV = clamp(dot(fd.normal, V), CORE3D_PBR_LIGHTING_EPSILON, 1.0);
    CORE_RELAXEDP const float roughness = brdfData.roughness;

    ShadingData shadingData;
    shadingData.pos = worldPos;
    shadingData.N = fd.normal;
    shadingData.NoV = NoV;
    shadingData.V = V;
    shadingData.f0 = brdfData.f0;
    shadingData.alpha2 = brdfData.alpha2;
    shadingData.diffuseColor = brdfData.diffuseColor;

    // ============================================================
    // Gradient w.r.t. metallic (m)
    // ============================================================
    // From CalcBRDFMetallicRoughness:
    //   f0 = mix(r0, B, m)  = r0*(1-m) + B*m
    //   diffuseColor = mix(B*(1 - f0), 0, m) = (1-m) * B * (1 - f0)
    //
    // df0/dm = B - r0  (per channel)
    // d(diffuseColor)/dm = -B*(1 - f0) + (1-m)*B*(B - r0)
    //                    = B * [-(1-f0) + (1-m)*(B - r0)]
    vec3 df0_dm = B - vec3(r0);
    vec3 f0_val = brdfData.f0.xyz;
    vec3 dDc_dm = B * (-(1.0 - f0_val) + (1.0 - m) * (B - vec3(r0)));

    // === Accumulate direct lighting gradient intermediates ===
    vec3 diffuseLightAccum = vec3(0.0);
    vec3 specularDGAccum = vec3(0.0);
    if ((fd.materialFlags & CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) {
        CalculateLightingBackward(shadingData, fd.materialFlags, diffuseLightAccum, specularDGAccum);
    }

    // === Add indirect lighting (IBL) gradient ===
    if ((fd.materialFlags & CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) {
        vec3 irradianceSample = CoreGetIrradianceSample(shadingData.N);
        diffuseLightAccum += irradianceSample * fd.ao;

        const vec3 worldReflect = reflect(-shadingData.V, shadingData.N);
        vec3 radianceSample = CoreGetRadianceSample(worldReflect, roughness);
        float horizonOcc = fd.ao * SpecularHorizonOcclusion(worldReflect, fd.normal);

        // d(EnvBRDFApprox)/d(f0) ≈ ab.x
        const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
        const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
        vec4 rv = roughness * c0 + c1;
        float a004 = min(rv.x * rv.x, exp2(-9.28 * NoV)) * rv.x + rv.y;
        float ab_x = -1.04 * a004 + rv.z;

        specularDGAccum += ab_x * radianceSample * horizonOcc;
    }

    // dC/dm = diffuseLightAccum · dDc_dm + specularDGAccum · df0_dm
    // (dot product per channel, i.e. element-wise multiply then sum over color channels)
    float dC_dm = dot(diffuseLightAccum, dDc_dm) + dot(specularDGAccum, df0_dm);

    // ============================================================
    // Gradient w.r.t. roughness (r)
    // ============================================================
    // roughness affects alpha2 = (r^2)^2 = r^4
    // d(alpha2)/dr = 4 * r^3
    // Use finite differences for the specular terms through D and G,
    // since analytical differentiation of dGGX and vGGX w.r.t. alpha2 is complex.
    const float eps = 0.002;
    float r_plus  = clamp(r + eps, CORE_BRDF_MIN_ROUGHNESS, 1.0);
    float r_minus = clamp(r - eps, CORE_BRDF_MIN_ROUGHNESS, 1.0);

    // Evaluate forward pass with r+eps
    vec4 matPlus = matOverride;
    matPlus.g = r_plus;
    InputBrdfData brdfPlus = CalcBRDFMetallicRoughness(fd.baseColor, matPlus);
    ShadingData sdPlus = shadingData;
    sdPlus.alpha2 = brdfPlus.alpha2;
    sdPlus.f0 = brdfPlus.f0;
    sdPlus.diffuseColor = brdfPlus.diffuseColor;
    vec3 colorPlus = vec3(0.0);
    if ((fd.materialFlags & CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) {
        colorPlus = CalculateLighting(sdPlus, fd.materialFlags);
    }
    if ((fd.materialFlags & CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) {
        vec3 irr = CoreGetIrradianceSample(sdPlus.N) * sdPlus.diffuseColor * fd.ao;
        const vec3 wr = reflect(-sdPlus.V, sdPlus.N);
        vec3 fInd = EnvBRDFApprox(sdPlus.f0.xyz, brdfPlus.roughness, NoV);
        vec3 radSamp = CoreGetRadianceSample(wr, brdfPlus.roughness);
        vec3 rad = radSamp * fInd;
        rad *= fd.ao * SpecularHorizonOcclusion(wr, fd.normal);
        colorPlus += (irr + rad);
    }

    // Evaluate forward pass with r-eps
    vec4 matMinus = matOverride;
    matMinus.g = r_minus;
    InputBrdfData brdfMinus = CalcBRDFMetallicRoughness(fd.baseColor, matMinus);
    ShadingData sdMinus = shadingData;
    sdMinus.alpha2 = brdfMinus.alpha2;
    sdMinus.f0 = brdfMinus.f0;
    sdMinus.diffuseColor = brdfMinus.diffuseColor;
    vec3 colorMinus = vec3(0.0);
    if ((fd.materialFlags & CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_PUNCTUAL_LIGHT_RECEIVER_BIT) {
        colorMinus = CalculateLighting(sdMinus, fd.materialFlags);
    }
    if ((fd.materialFlags & CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) == CORE_MATERIAL_INDIRECT_LIGHT_RECEIVER_BIT) {
        vec3 irr = CoreGetIrradianceSample(sdMinus.N) * sdMinus.diffuseColor * fd.ao;
        const vec3 wr = reflect(-sdMinus.V, sdMinus.N);
        vec3 fInd = EnvBRDFApprox(sdMinus.f0.xyz, brdfMinus.roughness, NoV);
        vec3 radSamp = CoreGetRadianceSample(wr, brdfMinus.roughness);
        vec3 rad = radSamp * fInd;
        rad *= fd.ao * SpecularHorizonOcclusion(wr, fd.normal);
        colorMinus += (irr + rad);
    }

    // dC/dr via central differences
    vec3 dC_dr_vec = (colorPlus - colorMinus) / (r_plus - r_minus);

    // === Chain rule: dLoss/d(m,r) = dLoss/dC * dC/d(m,r) ===
    vec3 dL_dC = predictedRGB - referenceRGB;
    float grad_m = dot(dL_dC, vec3(dC_dm));
    float grad_r = dot(dL_dC, dC_dr_vec);

    // Return gradient as vec3: (dLoss/dMetallic, dLoss/dRoughness, 0)
    return vec3(grad_m, grad_r, 0.0);
}

vec4 PbrBasicWithLRTexture(float depthBufferSample, FullGBufferData fd)
{
    if ((uMaskFlags & MASK_NORMAL) == MASK_NORMAL) {
        return PbrBasicWithLRNormal(depthBufferSample, fd);
    }
    if ((uMaskFlags & MASK_BASE_COLOR) == MASK_BASE_COLOR) {
        return PbrBasicWithLRBaseColor(depthBufferSample, fd);
    }

    // First-stage framework only: material/emissive/AO LR PBR variants are added later.
    return PbrBasic(depthBufferSample, fd);
}

vec4 BackwardPbrBasicWithLRTexture(float depthBufferSample, FullGBufferData fd,
    vec3 predictedRGB, vec3 referenceRGB)
{
    if ((uMaskFlags & MASK_NORMAL) == MASK_NORMAL) {
        return vec4(BackwardPbrBasicWithLRNormal(depthBufferSample, fd, predictedRGB, referenceRGB), 1.0);
    }
    if ((uMaskFlags & MASK_BASE_COLOR) == MASK_BASE_COLOR) {
        return vec4(BackwardPbrBasicWithLRBaseColor(depthBufferSample, fd, predictedRGB, referenceRGB), 1.0);
    }

    // First-stage framework only: material/emissive/AO backward variants are added later.
    return vec4(0.0);
}

/*
fragment shader for basic pbr materials.
*/
void main(void)
{
    const ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
    const float depthBufferSample = GetSampledDepthBuffer(inUv);
    vec4 predictedColor = vec4(0.0);
    vec4 outBaseColorGrad = vec4(0.0);
    if (depthBufferSample < 1.0) {
        FullGBufferData fd = GetUnpackMaterialValues(inUv);
        if (fd.materialType == CORE_MATERIAL_UNLIT) {
            outColor = UnlitBasic();
        } else if (fd.materialType == CORE_MATERIAL_UNLIT_SHADOW_ALPHA) {
            outColor = UnlitShadowAlpha(depthBufferSample, fd);
        } else {
            outColor = PbrBasic(depthBufferSample, fd);
            predictedColor = PbrBasicWithLRTexture(depthBufferSample, fd);
            outBaseColorGrad = BackwardPbrBasicWithLRTexture(depthBufferSample, fd, predictedColor.rgb, outColor.rgb);
        }
    } else {
        outColor = vec4(0.0);
    }
    imageStore(uPredictedColorOut, pixelCoord, predictedColor);
    imageStore(uGradOut, pixelCoord, outBaseColorGrad);
}
