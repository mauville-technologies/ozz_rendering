//
// Created by paulm on 2026-02-15.
//

#pragma once
#include <vulkan/vulkan_core.h>

namespace OZZ::vk {
    class VulkanQueue {
    public:
        VulkanQueue() = default;
        ~VulkanQueue() = default;

        void Init(VkDevice inDevice, VkSwapchainKHR inSwapchain, uint32_t inQueueFamily, uint32_t inQueueIndex);
        void Destroy();
        uint32_t AcquireNextImage();
        void SubmitSync(VkCommandBuffer commandBuffer);
        void SubmitAsync(VkCommandBuffer commandBuffer);
        void Present(uint32_t imageIndex);
        void WaitIdle();

    private:
        void createSemaphores();

    private:
        VkDevice device {VK_NULL_HANDLE};
        VkSwapchainKHR swapchain {VK_NULL_HANDLE};
        VkQueue queue {VK_NULL_HANDLE};
        VkSemaphore renderCompleteSemaphore {VK_NULL_HANDLE};
        VkSemaphore presentCompleteSemaphore {VK_NULL_HANDLE};
    };
} // namespace OZZ::vk