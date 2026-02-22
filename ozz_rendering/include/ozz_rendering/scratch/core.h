//
// Created by paulm on 2026-02-10.
//

#pragma once
#include "../../../src/vulkan/utils/physical_devices.h"
#include "vulkan_queue.h"

#include <functional>
#include <string>

namespace OZZ::vk {
    struct InitParams {
        std::string AppName;
        std::function<int(VkInstance, VkSurfaceKHR*)> SurfaceCreationFunction;
    };

    class VulkanCore {
    public:
        VulkanCore();
        ~VulkanCore();

        void Init(const InitParams& initParams);
        void Shutdown();
        void CreateCommandBuffers(uint32_t numberOfCommandBuffers, VkCommandBuffer* commandBuffers);
        void FreeCommandBuffers(uint32_t numberOfCommandBuffers, VkCommandBuffer* buffers);

        [[nodiscard]] uint32_t GetSwapchainImageCount() const;
        [[nodiscard]] VkImage GetSwapchainImage(uint32_t imageIndex) const;
        [[nodiscard]] VkImageView GetSwapchainImageView(uint32_t imageIndex) const;

        [[nodiscard]] VkDevice GetDevice() const { return device; }

        VulkanQueue* GetQueue();

    private:
        void createInstance(const std::string& appName);
        // init functions
        void createDebugCallback();
        void createSurface(const std::function<int(VkInstance, VkSurfaceKHR*)>& function);
        void createDevice();
        void createSwapchain();
        auto createCommandBufferPool() -> void;

        // helper functions
        uint32_t chooseNumberOfSwapchainImages(const VkSurfaceCapabilitiesKHR& capabilities);
        VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes);
        VkSurfaceFormatKHR chooseSurfaceFormatAndColorSpace(const std::vector<VkSurfaceFormatKHR>& surfaceFormats);
        VkImageView createImageView(VkDevice device,
                                    VkImage swapchainImage,
                                    VkFormat format,
                                    VkImageAspectFlags imageAspectFlags,
                                    VkImageViewType imageViewType,
                                    uint32_t layerCount,
                                    uint32_t mipLevels);

    private:
        VkInstance instance {VK_NULL_HANDLE};
        VkDebugUtilsMessengerEXT debugMessenger {VK_NULL_HANDLE};
        VkSurfaceKHR surface {VK_NULL_HANDLE};

        RHIVulkanPhysicalDevices physicalDevices;
        uint32_t queueFamily;
        VkDevice device;

        VkSwapchainKHR swapchain;
        VkSurfaceFormatKHR surfaceFormat;

        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;

        VkCommandPool commandBufferPool;
        VulkanQueue queue;
    };
} // namespace OZZ::vk
