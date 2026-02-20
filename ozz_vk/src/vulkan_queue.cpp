//
// Created by paulm on 2026-02-15.
//

#include "ozz_vk/vulkan_queue.h"

#include "ozz_vk/util.h"
#include "spdlog/spdlog.h"

void OZZ::vk::VulkanQueue::Init(VkDevice inDevice,
                                uint32_t inNumSwapchainImages,
                                VkSwapchainKHR inSwapchain,
                                uint32_t inQueueFamily,
                                uint32_t inQueueIndex) {
    device = inDevice;
    numSwapchainImages = inNumSwapchainImages;
    swapchain = inSwapchain;
    vkGetDeviceQueue(device, inQueueFamily, inQueueIndex, &queue);

    spdlog::info("Queue acquired");
    createSyncObjects();
}

void OZZ::vk::VulkanQueue::Destroy() {
    for (auto& syncObjects : frameSyncObjects) {
        if (syncObjects.PresentCompleteSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, syncObjects.PresentCompleteSemaphore, nullptr);
            syncObjects.PresentCompleteSemaphore = VK_NULL_HANDLE;
        }

        if (syncObjects.RenderCompleteSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, syncObjects.RenderCompleteSemaphore, nullptr);
            syncObjects.RenderCompleteSemaphore = VK_NULL_HANDLE;
        }

        if (syncObjects.InFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(device, syncObjects.InFlightFence, nullptr);
            syncObjects.InFlightFence = VK_NULL_HANDLE;
        }
    }
    frameSyncObjects.clear();
}

uint32_t OZZ::vk::VulkanQueue::AcquireNextImage() {
    auto& frameSync = this->frameSyncObjects[currentFrame];
    auto fenceResult = vkWaitForFences(device, 1, &frameSync.InFlightFence, VK_TRUE, UINT64_MAX);
    CHECK_VK_RESULT(fenceResult, "Wait for fence");
    vkResetFences(device, 1, &frameSync.InFlightFence);

    uint32_t imageIndex;
    const auto result = vkAcquireNextImageKHR(device,
                                              swapchain,
                                              UINT64_MAX,
                                              frameSync.PresentCompleteSemaphore,
                                              VK_NULL_HANDLE,
                                              &imageIndex);

    CHECK_VK_RESULT(result, "Acquire next image");
    return currentFrame;
}

void OZZ::vk::VulkanQueue::SubmitSync(VkCommandBuffer commandBuffer) {
    const auto& frameSync = this->frameSyncObjects[currentFrame];
    VkSubmitInfo submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = VK_NULL_HANDLE,
        .pWaitDstStageMask = VK_NULL_HANDLE,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = VK_NULL_HANDLE,
    };

    const auto result = vkQueueSubmit(queue, 1, &submitInfo, frameSync.InFlightFence);
    CHECK_VK_RESULT(result, "queue submit - sync");

    currentFrame = (currentFrame + 1) % numSwapchainImages;
}

void OZZ::vk::VulkanQueue::SubmitAsync(VkCommandBuffer commandBuffer) {
    auto& [PresentCompleteSemaphore, RenderCompleteSemaphore, InFlightFence] = this->frameSyncObjects[currentFrame];
    VkPipelineStageFlags waitFlags {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &PresentCompleteSemaphore,
        .pWaitDstStageMask = &waitFlags,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &RenderCompleteSemaphore,
    };

    const auto result = vkQueueSubmit(queue, 1, &submitInfo, InFlightFence);
    CHECK_VK_RESULT(result, "queue submit - async");
}

void OZZ::vk::VulkanQueue::Present(uint32_t imageIndex) {
    const auto& frameSync = frameSyncObjects[currentFrame];
    VkPresentInfoKHR presentInfo {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frameSync.RenderCompleteSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &currentFrame,
        .pResults = VK_NULL_HANDLE,
    };

    const auto result = vkQueuePresentKHR(queue, &presentInfo);
    CHECK_VK_RESULT(result, "Present queue");

    currentFrame = (currentFrame + 1) % numSwapchainImages;
}

void OZZ::vk::VulkanQueue::WaitIdle() {
    vkQueueWaitIdle(queue);
}

void OZZ::vk::VulkanQueue::createSyncObjects() {
    for (uint32_t i = 0; i < numSwapchainImages; i++) {
        frameSyncObjects.push_back({
            .PresentCompleteSemaphore = CreateSemaphore(device),
            .RenderCompleteSemaphore = CreateSemaphore(device),
            .InFlightFence = CreateFence(device, VK_FENCE_CREATE_SIGNALED_BIT),
        });
    }
}