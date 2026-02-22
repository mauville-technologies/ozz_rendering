//
// Created by paulm on 2026-02-21.
//

#pragma once
#include "../resource_pool.h"
#include "rhi_texture_vulkan.h"
#include "utils/physical_devices.h"

#include <ozz_rendering/rhi.h>
#include <volk.h>

namespace OZZ::rendering::vk {
    class RHIDeviceVulkan : public RHIDevice {
    public:
        explicit RHIDeviceVulkan(const PlatformContext& context);
        ~RHIDeviceVulkan() override;

        // rhi device interface
        RHITextureHandle CreateTexture() override;

    private:
        bool initialize();
        bool createInstance();
        bool createDebugCallback();
        bool createSurface();
        bool createDevice();
        bool createSwapchain();
        bool createCommandBufferPool();

    private:
        PlatformContext platformContext;

        bool bIsValid {false};

        /**
         * Vulkan Primitives
         */

        /**
         * CORE Vulkan objects
         */
        VkInstance instance {VK_NULL_HANDLE};
        VkDebugUtilsMessengerEXT debugMessenger {VK_NULL_HANDLE};
        VkSurfaceKHR surface {VK_NULL_HANDLE};
        RHIVulkanPhysicalDevices physicalDevices;
        VkDevice device {VK_NULL_HANDLE};
        VkSwapchainKHR swapchain {VK_NULL_HANDLE};
        VkCommandPool commandBufferPool {VK_NULL_HANDLE};

        /**
         * Swapchain Vulkan objects
         */
        VkSurfaceFormatKHR swapchainSurfaceFormat {};
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;

        // resource pools
        ResourcePool<struct TextureTag, RHITextureVulkan> texturePool;
    };
} // namespace OZZ::rendering::vk
