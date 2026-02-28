//
// Created by paulm on 2026-02-26.
//

#pragma once
#include "ozz_rendering/utils/enums.h"
#include <cstdint>

namespace OZZ::rendering {

    using IndexBufferElementType = uint32_t;
    enum class BufferMemoryAccess : uint8_t {
        GpuOnly,
        CpuToGpu,
        GpuToCpu,
    };

    enum class BufferUsage : uint8_t {
        VertexBuffer = 1 << 0,
        IndexBuffer = 1 << 1,
        UniformBuffer = 1 << 2,
        StorageBuffer = 1 << 3,
        TransferSource = 1 << 4,
        TransferDestination = 1 << 5,
        Indirect = 1 << 6,
    };

    struct BufferDescriptor {
        uint64_t Size {0};
        BufferUsage Usage {BufferUsage::VertexBuffer};
        BufferMemoryAccess Access {BufferMemoryAccess::GpuOnly};
    };
} // namespace OZZ::rendering

template <>
struct enable_bitmask_operators<OZZ::rendering::BufferUsage> : std::true_type {};
