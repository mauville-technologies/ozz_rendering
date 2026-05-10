#pragma once

#include <cstdint>
#include <webgpu/webgpu.h>

namespace OZZ::rendering::webgpu {

    struct RHITextureWebGPU {
        WGPUTexture Texture {nullptr};
        WGPUTextureView TextureView {nullptr};
        WGPUSampler Sampler {nullptr};
        uint32_t Width {0};
        uint32_t Height {0};
        // Swapchain images: Texture is not owned; only TextureView is released on free.
        bool IsSwapchainImage {false};
    };

} // namespace OZZ::rendering::webgpu
