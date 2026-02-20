//
// Created by paulm on 2026-02-15.
//

#pragma once
#include <vector>
#include <volk.h>

namespace OZZ::vk {
    struct FrameSynchronizationObjects {
        VkSemaphore PresentCompleteSemaphore {VK_NULL_HANDLE};
        VkSemaphore RenderCompleteSemaphore {VK_NULL_HANDLE};

        VkFence InFlightFence {VK_NULL_HANDLE};
    };

    class VulkanQueue {
    public:
        VulkanQueue() = default;
        ~VulkanQueue() = default;

        void Init(VkDevice inDevice,
                  uint32_t inNumSwapchainImages,
                  VkSwapchainKHR inSwapchain,
                  uint32_t inQueueFamily,
                  uint32_t inQueueIndex);
        void Destroy();
        uint32_t AcquireNextImage();
        void SubmitSync(VkCommandBuffer commandBuffer);
        void SubmitAsync(VkCommandBuffer commandBuffer);
        void Present(uint32_t imageIndex);
        void WaitIdle();

    private:
        void createSyncObjects();

    private:
        VkDevice device {VK_NULL_HANDLE};
        uint32_t numSwapchainImages {0};
        VkSwapchainKHR swapchain {VK_NULL_HANDLE};
        VkQueue queue {VK_NULL_HANDLE};

        std::vector<FrameSynchronizationObjects> frameSyncObjects {};

        uint32_t currentFrame = 0;
    };
} // namespace OZZ::vk