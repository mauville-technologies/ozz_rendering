//
// Created by paulm on 2026-03-01.
//

#pragma once
#include "rhi_handle.h"
#include "rhi_shader.h"

#include <cstdint>

namespace OZZ::rendering {
    constexpr uint32_t MaxBoundDescriptorSets = 16;
    constexpr uint32_t MaxDescriptorSets = 16;
    constexpr uint32_t MaxPushConstantRanges = 4;

    enum class DescriptorType {
        UniformBuffer,
        StorageBuffer,
        CombinedImageSampler,
        SampledImage,
        Sampler,
        StorageImage,
        // A texture binding known (by naming convention, see patchGLSLSamplers's depth
        // detection) to be a depth-format texture. WebGPU requires depth-format textures
        // to be declared with sampleType Depth/UnfilterableFloat (not Float) and sampled
        // with a NonFiltering sampler — unlike Vulkan, which samples them like any other
        // combined image sampler. Vulkan backends treat this identically to
        // CombinedImageSampler.
        DepthSampledImage,
        // A GLSL `readonly buffer` SSBO. WebGPU disallows a read-write storage buffer
        // (WGPUBufferBindingType_Storage) from being visible to the vertex stage at all —
        // it must be declared WGPUBufferBindingType_ReadOnlyStorage. Vulkan has no such
        // restriction and treats this identically to StorageBuffer.
        ReadOnlyStorageBuffer,
    };

    struct RHIDescriptorSetLayoutBinding {
        uint32_t Binding {0};
        DescriptorType Type {DescriptorType::UniformBuffer};
        uint32_t Count {0};         // 0 = empty/unused slot (skip); populated slots have Count >= 1
        ShaderStageFlags StageFlags {};
        uint32_t OriginalBinding {UINT32_MAX}; // UINT32_MAX = same as Binding (no WebGPU remapping)
    };

    struct RHIDescriptorSetLayoutDescriptor {
        RHIDescriptorSetLayoutBinding Bindings[MaxBoundDescriptorSets] {};
        uint32_t BindingCount {0};
    };

    // Push constants are currently ad-hoc in SetPushConstants — worth formalizing here
    struct RHIPushConstantRange {
        ShaderStageFlags StageFlags {ShaderStageFlags::All};
        uint32_t Offset {0};
        uint32_t Size {0};
    };

    // Pipeline layout ties it all together
    struct RHIPipelineLayoutDescriptor {
        RHIDescriptorSetLayoutDescriptor Sets[MaxDescriptorSets] {};
        uint32_t SetCount {0};
        RHIPushConstantRange PushConstants[MaxPushConstantRanges] {};
        uint32_t PushConstantCount {0};
    };

    struct RHIDescriptorWrite {
        uint32_t Binding {0};
        DescriptorType Type {DescriptorType::UniformBuffer};

        struct BufferInfo {
            RHIBufferHandle Buffer {};
            uint64_t Offset {0};
            uint64_t Range {~0ULL}; // VK_WHOLE_SIZE equivalent
        };

        struct ImageInfo {
            RHITextureHandle Texture {};
        };

        BufferInfo Buffer {};
        ImageInfo Image {};
    };
} // namespace OZZ::rendering