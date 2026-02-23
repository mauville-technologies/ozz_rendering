//
// Created by paulm on 2026-02-21.
//

#pragma once
#include <vma/vk_mem_alloc.h>
#include <volk.h>

namespace OZZ::rendering::vk {
    struct RHITextureVulkan {
        VkImage Image {VK_NULL_HANDLE};
        VkImageView ImageView {VK_NULL_HANDLE};
        VmaAllocation Allocation {VK_NULL_HANDLE};
    };
} // namespace OZZ::rendering::vk
