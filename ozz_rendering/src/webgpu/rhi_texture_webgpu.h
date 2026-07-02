#pragma once

#include <cstdint>
#include <webgpu/webgpu.h>

#include <ozz_rendering/rhi_texture.h>

namespace OZZ::rendering::webgpu {

    struct RHITextureWebGPU {
        WGPUTexture Texture {nullptr};
        WGPUTextureView TextureView {nullptr};
        WGPUSampler Sampler {nullptr};
        uint32_t Width {0};
        uint32_t Height {0};
        // The RHI format this texture was created with. Used to lazily resolve WebGPU
        // sample types (depth textures need UnfilterableFloat + NonFiltering sampler).
        TextureFormat Format {TextureFormat::RGBA8};
        // Swapchain images: Texture is not owned; only TextureView is released on free.
        bool IsSwapchainImage {false};
    };

    // True for depth / depth-stencil formats. WebGPU rejects a plain Float sample type
    // against these, so bindings that sample them are declared UnfilterableFloat.
    inline bool IsDepthFormat(TextureFormat fmt) {
        return fmt == TextureFormat::D32Float || fmt == TextureFormat::D24S8;
    }

} // namespace OZZ::rendering::webgpu
