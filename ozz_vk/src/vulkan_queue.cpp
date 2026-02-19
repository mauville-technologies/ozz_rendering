//
// Created by paulm on 2026-02-15.
//

#include "ozz_vk/vulkan_queue.h"

#include "ozz_vk/util.h"
#include "spdlog/spdlog.h"

void OZZ::vk::VulkanQueue::Init(VkDevice inDevice,
                                VkSwapchainKHR inSwapchain,
                                uint32_t inQueueFamily,
                                uint32_t inQueueIndex) {
    device = inDevice;
    swapchain = inSwapchain;
    vkGetDeviceQueue(device, inQueueFamily, inQueueIndex, &queue);

    spdlog::info("Queue acquired");
    createSemaphores();
}

void OZZ::vk::VulkanQueue::Destroy() {
    if (presentCompleteSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, presentCompleteSemaphore, nullptr);
        presentCompleteSemaphore = VK_NULL_HANDLE;
    }

    if (renderCompleteSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, renderCompleteSemaphore, nullptr);
        renderCompleteSemaphore = VK_NULL_HANDLE;
    }
}

uint32_t OZZ::vk::VulkanQueue::AcquireNextImage() {
    uint32_t imageIndex;
    const auto result =
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, presentCompleteSemaphore, nullptr, &imageIndex);

    CHECK_VK_RESULT(result, "Acquire next image");
    return imageIndex;
}

void OZZ::vk::VulkanQueue::SubmitSync(VkCommandBuffer commandBuffer) {
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

    const auto result = vkQueueSubmit(queue, 1, &submitInfo, nullptr);
    CHECK_VK_RESULT(result, "queue submit - sync");
}

void OZZ::vk::VulkanQueue::SubmitAsync(VkCommandBuffer commandBuffer) {
    VkPipelineStageFlags waitFlags {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &presentCompleteSemaphore,
        .pWaitDstStageMask = &waitFlags,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderCompleteSemaphore,
    };

    const auto result = vkQueueSubmit(queue, 1, &submitInfo, nullptr);
    CHECK_VK_RESULT(result, "queue submit - async");
}

void OZZ::vk::VulkanQueue::Present(uint32_t imageIndex) {
    VkPresentInfoKHR presentInfo {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderCompleteSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &imageIndex,
        .pResults = VK_NULL_HANDLE,
    };

    const auto result = vkQueuePresentKHR(queue, &presentInfo);
    CHECK_VK_RESULT(result, "Present queue");
}

void OZZ::vk::VulkanQueue::WaitIdle() {
    vkQueueWaitIdle(queue);
}

void OZZ::vk::VulkanQueue::createSemaphores() {
    presentCompleteSemaphore = CreateSemaphore(device);
    renderCompleteSemaphore = CreateSemaphore(device);
}