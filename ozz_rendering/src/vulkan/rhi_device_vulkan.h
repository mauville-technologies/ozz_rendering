//
// Created by paulm on 2026-02-21.
//

#pragma once
#include "utils/physical_devices.h"

#include <ozz_rendering/rhi.h>
#include <volk.h>

namespace OZZ::rendering::vk {
    class RHIDeviceVulkan : public RHIDevice {
    public:
        explicit RHIDeviceVulkan(const PlatformContext& context);
        ~RHIDeviceVulkan() override;

    private:
        bool initialize();
        bool createInstance();
        bool createDebugCallback();
        bool createSurface();
        bool createDevice();

    private:
        PlatformContext platformContext;

        bool bIsValid {false};

        // Vulkan primitives
        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkSurfaceKHR surface;
        RHIVulkanPhysicalDevices physicalDevices;
        VkDevice device;
    };
} // namespace OZZ::rendering::vk
