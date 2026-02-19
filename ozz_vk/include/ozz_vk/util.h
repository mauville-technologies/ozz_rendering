//
// Created by paulm on 2026-02-10.
//

#pragma once

#include <cstdio>
#include <cstdlib>
#include <volk.h>

#define CHECK_VK_RESULT(res, msg)                                                                                      \
    if (res != VK_SUCCESS) {                                                                                           \
        fprintf(stderr, "Error in %s:%d - %s, code %d\n", __FILE__, __LINE__, msg, res);                               \
        exit(1);                                                                                                       \
    }

namespace OZZ::vk {
    void BeginCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferUsageFlags usageFlags);
    VkSemaphore CreateSemaphore(VkDevice device);
} // namespace OZZ::vk