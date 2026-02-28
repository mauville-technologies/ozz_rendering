//
// Created by paulm on 2026-02-26.
//

#pragma once
#include "ozz_rendering/rhi_buffer.h"

#include <vma/vk_mem_alloc.h>

namespace OZZ::rendering::vk {
    struct RHIBufferVulkan {
        VkBuffer Buffer {VK_NULL_HANDLE};
        VmaAllocation Allocation {VK_NULL_HANDLE};
        VmaAllocationInfo AllocationInfo {};

        BufferUsage Usage {BufferUsage::VertexBuffer};
        BufferMemoryAccess Access {BufferMemoryAccess::GpuOnly};
    };
} // namespace OZZ::rendering::vk