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

#ifndef PT_ECS_SYSTEM_CAMERA_CONTROL_SYSTEM_H
#define PT_ECS_SYSTEM_CAMERA_CONTROL_SYSTEM_H

#include <base/containers/string_view.h>
#include <base/math/quaternion.h>
#include <base/math/vector.h>
#include <base/util/uid.h>
#include <core/ecs/entity.h>
#include <core/ecs/intf_system.h>
#include <core/namespace.h>
#include <plugintemplate/implementation_uids.h>
#include <plugintemplate/namespace.h>

PT_BEGIN_NAMESPACE()

/** @ingroup group_ecs_systems_camera_control */
/**
 * CameraControlSystem.
 * System that controls camera position and orientation.
 * Supports random camera switching on a sphere around the origin.
 */
class CameraControlSystem : public CORE_NS::ISystem {
public:
    static constexpr BASE_NS::Uid UID { PT_NS::UID_CAMERA_CONTROL_SYSTEM };
    static constexpr const char* TYPE_NAME = "CameraControlSystem";

    /** Configuration for camera control. */
    struct Config {
        /** Target camera entity to control. */
        CORE_NS::Entity targetCamera;
        
        /** Radius of the sphere on which camera orbits. */
        float orbitRadius = 3.0f;
        
        /** Number of frames between camera position switches. */
        uint32_t switchInterval = 300;
        
        /** Whether the system is enabled. */
        bool enabled = true;
    };

    CameraControlSystem(CORE_NS::IEcs& ecs);
    ~CameraControlSystem() override;

    // ISystem interface implementation
    BASE_NS::string_view GetName() const override;
    BASE_NS::Uid GetUid() const override;
    CORE_NS::IPropertyHandle* GetProperties() override;
    const CORE_NS::IPropertyHandle* GetProperties() const override;
    void SetProperties(const CORE_NS::IPropertyHandle& properties) override;
    bool IsActive() const override;
    void SetActive(bool isActive) override;
    void Initialize() override;
    bool Update(bool isFrameRenderingQueued, uint64_t time, uint64_t delta) override;
    void Uninitialize() override;
    const CORE_NS::IEcs& GetECS() const override;

    /** Set configuration for camera control.
     * @param config Configuration to apply.
     */
    void SetConfig(const Config& config);
    
    /** Get current configuration.
     * @return Current configuration.
     */
    const Config& GetConfig() const;
    
    /** Check if camera view has switched this frame.
     * @return True if view switched, false otherwise.
     */
    inline bool HasViewSwitched() const { return viewSwitched_; }
    
    /** Clear the view switched flag (called after processing). */
    inline void ClearViewSwitchedFlag() { viewSwitched_ = false; }

    /** Factory methods for system creation/destruction. */
    static CORE_NS::ISystem* Create(CORE_NS::IEcs& ecs);
    static void Destroy(CORE_NS::ISystem* instance);

private:
    void UpdateCameraPosition();
    void FindMainCamera();

    CORE_NS::IEcs* ecs_;
    Config config_;
    uint32_t frameCount_ = 0;
    bool active_ = true;
    bool initialized_ = false;
    bool viewSwitched_ = false;  // Flag indicating camera view switched this frame
};

/** Return name of this system. */
inline constexpr BASE_NS::string_view GetName(const CameraControlSystem*)
{
    return CameraControlSystem::TYPE_NAME;
}

PT_END_NAMESPACE()

#endif // PT_ECS_SYSTEM_CAMERA_CONTROL_SYSTEM_H