/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#include <plugintemplate/camera_control_system.h>

#include <base/math/mathf.h>
#include <base/math/matrix_util.h>
#include <base/math/quaternion_util.h>
#include <base/math/vector_util.h>
#include <core/ecs/intf_ecs.h>
#include <core/ecs/intf_entity_manager.h>
#include <core/property/intf_property_handle.h>
#include <random>
#include <iostream>
#include <3d/ecs/components/camera_component.h>
#include <3d/ecs/components/transform_component.h>

using namespace BASE_NS;
using namespace CORE_NS;
using namespace CORE3D_NS;

PT_BEGIN_NAMESPACE()

CameraControlSystem::CameraControlSystem(IEcs& ecs)
    : ecs_(&ecs)
{
}

CameraControlSystem::~CameraControlSystem() = default;

ISystem* CameraControlSystem::Create(IEcs& ecs)
{
    std::cout << "CameraControlSystem::Create(IEcs& ecs)" << std::endl;
    return new CameraControlSystem(ecs);
}

void CameraControlSystem::Destroy(ISystem* instance)
{
    delete static_cast<CameraControlSystem*>(instance);
}

string_view CameraControlSystem::GetName() const
{
    return TYPE_NAME;
}

Uid CameraControlSystem::GetUid() const
{
    return UID;
}

IPropertyHandle* CameraControlSystem::GetProperties()
{
    // TODO: Implement property handle for config if needed
    return nullptr;
}

const IPropertyHandle* CameraControlSystem::GetProperties() const
{
    // TODO: Implement property handle for config if needed
    return nullptr;
}

void CameraControlSystem::SetProperties(const IPropertyHandle& properties)
{
    // TODO: Parse properties to update config if needed
}

bool CameraControlSystem::IsActive() const
{
    return active_;
}

void CameraControlSystem::SetActive(bool isActive)
{
    active_ = isActive;
}

void CameraControlSystem::Initialize()
{
    initialized_ = true;
    
    // Auto-find main camera if targetCamera is not set
    if (!EntityUtil::IsValid(config_.targetCamera)) {
        FindMainCamera();
    }
}

void CameraControlSystem::FindMainCamera()
{
    auto* cameraManager = GetManager<ICameraComponentManager>(*ecs_);
    if (!cameraManager) {
        std::cout << "CameraControlSystem: CameraManager not found" << std::endl;
        return;
    }
    
    // Iterate through all entities to find the main camera
    const auto& entityManager = ecs_->GetEntityManager();
    for (Entity entity : entityManager) {
        if (cameraManager->HasComponent(entity)) {
            auto handle = cameraManager->Read(entity);
            if (handle && (handle->sceneFlags & CameraComponent::SceneFlagBits::MAIN_CAMERA_BIT)) {
                config_.targetCamera = entity;
                std::cout << "CameraControlSystem: Found main camera entity " << entity.id << std::endl;
                return;
            }
        }
    }
    
    // If no main camera found, use the first camera entity
    for (Entity entity : entityManager) {
        if (cameraManager->HasComponent(entity)) {
            config_.targetCamera = entity;
            std::cout << "CameraControlSystem: Using first camera entity " << entity.id << std::endl;
            return;
        }
    }
    
    std::cout << "CameraControlSystem: No camera found in scene" << std::endl;
}

bool CameraControlSystem::Update(bool isFrameRenderingQueued, uint64_t time, uint64_t delta)
{
    std::cout << "CameraControlSystem::Update" << std::endl;
    
    // Try to find main camera if not set (may be created after Initialize)
    if (!EntityUtil::IsValid(config_.targetCamera)) {
        FindMainCamera();
        if (!EntityUtil::IsValid(config_.targetCamera)) {
            std::cout << "CameraControlSystem::Update: no target camera found" << std::endl;
            return false;
        }
    }
    
    UpdateCameraPosition();
    frameCount_++;
    
    return true;
}

void CameraControlSystem::Uninitialize()
{
    initialized_ = false;
}

const IEcs& CameraControlSystem::GetECS() const
{
    return *ecs_;
}

void CameraControlSystem::SetConfig(const Config& config)
{
    config_ = config;
}

const CameraControlSystem::Config& CameraControlSystem::GetConfig() const
{
    return config_;
}

void CameraControlSystem::UpdateCameraPosition()
{
    // Only update camera position at specified intervals
    if (frameCount_ % config_.switchInterval != 0) {
        return;
    }
    
    // Random number generators for spherical position
    static std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<float> disZ(-1.0f, 1.0f);
    std::uniform_real_distribution<float> disTheta(0.0f, 2.0f * Math::PI);
    
    // Generate random point on a sphere of radius orbitRadius
    float r = config_.orbitRadius;
    float z = disZ(gen);
    float phi = acos(z);
    float theta = disTheta(gen);
    
    Math::Vec3 pos(r * sin(phi) * cos(theta), r * sin(phi) * sin(theta), r * cos(phi));
    
    // Calculate View matrix looking at origin (LookAt RH)
    // Handle pole cases by adjusting up vector
    Math::Vec3 up = (abs(z) > 0.99f) ? Math::Vec3(1.f, 0.f, 0.f) : Math::Vec3(0.f, 1.f, 0.f);
    Math::Mat4X4 view = Math::LookAtRh(pos, Math::Vec3(0.f, 0.f, 0.f), up);
    
    // Convert View matrix to World matrix by inversion
    Math::Mat4X4 world = Math::Inverse(view);
    
    // Decompose matrix to get position and orientation
    Math::Vec3 s, t, sk;
    Math::Quat q;
    Math::Vec4 p;
    Math::Decompose(world, s, q, t, sk, p);
    
    // Get transform component manager and update camera
    auto* transformManager = GetManager<ITransformComponentManager>(*ecs_);
    if (transformManager) {
        auto handle = transformManager->Write(config_.targetCamera);
        if (handle) {
            handle->position = t;
            handle->rotation = q;
        }
    }
}

PT_END_NAMESPACE()