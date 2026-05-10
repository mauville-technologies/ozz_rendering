#pragma once

#include <ozz_rendering/rhi_buffer.h>
#include <webgpu/webgpu.h>

namespace OZZ::rendering::webgpu {

    struct RHIBufferWebGPU {
        WGPUBuffer Buffer {nullptr};
        uint64_t Size {0};
        BufferUsage Usage {};
        BufferMemoryAccess Access {};
    };

} // namespace OZZ::rendering::webgpu
