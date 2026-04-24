//
// Created by paulm on 2026-02-21.
//

#pragma once
#include <vk_mem_alloc.h>
#include <volk.h>

namespace OZZ::rendering::vk {
    struct RHITextureVulkan {
        VkImage Image {VK_NULL_HANDLE};
        VkImageView ImageView {VK_NULL_HANDLE};
        VkSampler Sampler {VK_NULL_HANDLE};
        VmaAllocation Allocation {VK_NULL_HANDLE};
        VmaAllocationInfo AllocationInfo {};

        uint32_t Width {0};
        uint32_t Height {0};
    };
} // namespace OZZ::rendering::vk
