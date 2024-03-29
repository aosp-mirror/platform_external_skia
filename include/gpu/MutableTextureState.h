/*
 * Copyright 2022 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_MutableTextureState_DEFINED
#define skgpu_MutableTextureState_DEFINED

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "include/gpu/GpuTypes.h"

#ifdef SK_VULKAN
#include "include/private/gpu/vk/SkiaVulkan.h"
#include "include/private/gpu/vk/VulkanTypesPriv.h"

#include <cstdint>
#endif

namespace skgpu {

/**
 * Since Skia and clients can both modify gpu textures and their connected state, Skia needs a way
 * for clients to inform us if they have modifiend any of this state. In order to not need setters
 * for every single API and state, we use this class to be a generic wrapper around all the mutable
 * state. This class is used for calls that inform Skia of these texture/image state changes by the
 * client as well as for requesting state changes to be done by Skia. The backend specific state
 * that is wrapped by this class are:
 *
 * Vulkan: VkImageLayout and QueueFamilyIndex
 */
class SK_API MutableTextureState : public SkRefCnt {
public:
    MutableTextureState() {}

#if defined(SK_VULKAN)
    MutableTextureState(VkImageLayout layout, uint32_t queueFamilyIndex);
#endif

    MutableTextureState(const MutableTextureState& that);

    MutableTextureState& operator=(const MutableTextureState& that);

    void set(const MutableTextureState& that);

#if defined(SK_VULKAN)
    // If this class is not Vulkan backed it will return value of VK_IMAGE_LAYOUT_UNDEFINED.
    // Otherwise it will return the VkImageLayout.
    VkImageLayout getVkImageLayout() const {
        if (this->isValid() && fBackend != BackendApi::kVulkan) {
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }
        return fVkState.getImageLayout();
    }

    // If this class is not Vulkan backed it will return value of VK_QUEUE_FAMILY_IGNORED.
    // Otherwise it will return the VkImageLayout.
    uint32_t getQueueFamilyIndex() const {
        if (this->isValid() && fBackend != BackendApi::kVulkan) {
            return VK_QUEUE_FAMILY_IGNORED;
        }
        return fVkState.getQueueFamilyIndex();
    }
#endif

    BackendApi backend() const { return fBackend; }

    // Returns true if the backend mutable state has been initialized.
    bool isValid() const { return fIsValid; }

private:
    friend class MTSVKPriv;
    union {
        char fPlaceholder;
#ifdef SK_VULKAN
        VulkanMutableTextureState fVkState;
#endif
    };

    BackendApi fBackend = BackendApi::kMock;
    bool fIsValid = false;
};

} // namespace skgpu

#endif // skgpu_MutableTextureState_DEFINED
