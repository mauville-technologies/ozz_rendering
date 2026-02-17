//
// Created by paulm on 2026-02-10.
//

#include "ozz_vk/util.h"

namespace OZZ::vk {
    void BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferUsageFlags usageFlags) {
        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = usageFlags,
            .pInheritanceInfo = nullptr,
        };

        const auto result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
        CHECK_VK_RESULT(result, "Begin command buffer");
    }

    VkSemaphore CreateSemaphore(const VkDevice device) {
        VkSemaphore semaphore;

        VkSemaphoreCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        };
        const auto result = vkCreateSemaphore(device, &createInfo, nullptr, &semaphore);
        CHECK_VK_RESULT(result, "Create semaphore");
        return semaphore;
    }
} // namespace OZZ::vk