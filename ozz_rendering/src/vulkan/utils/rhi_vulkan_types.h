//
// Created by paulm on 2026-02-23.
//

#pragma once

#include <vma/vk_mem_alloc.h>
#include <volk.h>

#include <cstdint>
#include <vector>

#include "ozz_rendering/rhi_barrier.h"
#include "ozz_rendering/rhi_buffer.h"
#include "ozz_rendering/rhi_texture.h"
#include "spdlog/spdlog.h"

namespace OZZ::rendering::vk {
    struct CompiledShaderProgram {
        std::vector<uint32_t> VertexSpirv;
        std::vector<uint32_t> GeometrySpirv;
        std::vector<uint32_t> FragmentSpirv;
    };

    inline VkPipelineStageFlags2 ConvertPipelineStageToVulkan(const PipelineStage stage) {
        switch (stage) {
            case PipelineStage::None:
                return VK_PIPELINE_STAGE_NONE;
            case PipelineStage::ColorAttachmentOutput:
                return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            case PipelineStage::Transfer:
                return VK_PIPELINE_STAGE_TRANSFER_BIT;
            case PipelineStage::VertexShader:
                return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
            case PipelineStage::FragmentShader:
                return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            case PipelineStage::EarlyFragmentTests:
                return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
            case PipelineStage::AllGraphics:
                return VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
            case PipelineStage::AllCommands:
                return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }
        return VK_PIPELINE_STAGE_NONE;
    }

    inline VkAccessFlags2 ConvertAccessToVulkan(const Access access) {
        switch (access) {
            case Access::None:
                return VK_ACCESS_2_NONE;
            case Access::ColorAttachmentRead:
                return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            case Access::ColorAttachmentWrite:
                return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            case Access::ShaderRead:
                return VK_ACCESS_2_SHADER_READ_BIT;
            case Access::ShaderWrite:
                return VK_ACCESS_2_SHADER_WRITE_BIT;
            case Access::TransferRead:
                return VK_ACCESS_2_TRANSFER_READ_BIT;
            case Access::TransferWrite:
                return VK_ACCESS_2_TRANSFER_WRITE_BIT;
            case Access::DepthStencilAttachmentRead:
                return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            case Access::DepthStencilAttachmentWrite:
                return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        return VK_ACCESS_2_NONE;
    }

    inline VkImageLayout ConvertTextureLayoutToVulkan(const TextureLayout layout) {
        switch (layout) {
            case TextureLayout::Undefined:
                return VK_IMAGE_LAYOUT_UNDEFINED;
            case TextureLayout::ColorAttachment:
                return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            case TextureLayout::DepthStencilAttachment:
                return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            case TextureLayout::ShaderReadOnly:
                return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case TextureLayout::TransferSrc:
                return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            case TextureLayout::TransferDst:
                return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            case TextureLayout::Present:
                return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        return VK_IMAGE_LAYOUT_UNDEFINED;
    }

    inline VkImageAspectFlags ConvertTextureAspectToVulkan(const TextureAspect aspect) {
        VkImageAspectFlags flags = 0;

        if (has(aspect, TextureAspect::Color))
            flags |= VK_IMAGE_ASPECT_COLOR_BIT;
        if (has(aspect, TextureAspect::Depth))
            flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (has(aspect, TextureAspect::Stencil))
            flags |= VK_IMAGE_ASPECT_STENCIL_BIT;

        return flags;
    }

    inline VkImageAspectFlags GetAspectMask(const TextureFormat format) {
        switch (format) {
            case TextureFormat::D32Float:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            case TextureFormat::D24S8:
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            default:
                return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    inline bool IsDepthFormat(const TextureFormat format) {
        return format == TextureFormat::D32Float || format == TextureFormat::D24S8;
    }

    inline VkAttachmentLoadOp ConvertLoadOpToVulkan(const LoadOp loadOp) {
        switch (loadOp) {
            case LoadOp::DontCare:
                return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            case LoadOp::Load:
                return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOp::Clear:
                return VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    inline VkAttachmentStoreOp ConvertStoreOpToVulkan(const StoreOp storeOp) {
        switch (storeOp) {
            case StoreOp::DontCare:
                return VK_ATTACHMENT_STORE_OP_DONT_CARE;
            case StoreOp::Store:
                return VK_ATTACHMENT_STORE_OP_STORE;
        }
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    inline VkPrimitiveTopology ConvertPrimitiveTopologyToVulkan(const PrimitiveTopology topology) {
        switch (topology) {
            case PrimitiveTopology::TriangleList:
                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case PrimitiveTopology::TriangleStrip:
                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            case PrimitiveTopology::LineList:
                return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveTopology::LineStrip:
                return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case PrimitiveTopology::PointList:
                return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        }
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    inline VkCullModeFlags ConvertCullModeToVulkan(const CullMode cullMode) {
        switch (cullMode) {
            case CullMode::None:
                return VK_CULL_MODE_NONE;
            case CullMode::Front:
                return VK_CULL_MODE_FRONT_BIT;
            case CullMode::Back:
                return VK_CULL_MODE_BACK_BIT;
            case CullMode::FrontAndBack:
                return VK_CULL_MODE_FRONT_AND_BACK;
        }
        return VK_CULL_MODE_NONE;
    }

    inline VkFrontFace ConvertFrontFaceToVulkan(const FrontFace frontFace) {
        switch (frontFace) {
            case FrontFace::CounterClockwise:
                return VK_FRONT_FACE_COUNTER_CLOCKWISE;
            case FrontFace::Clockwise:
                return VK_FRONT_FACE_CLOCKWISE;
        }
        return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }

    inline VkPolygonMode ConvertPolygonModeToVulkan(const PolygonMode polygonMode) {
        switch (polygonMode) {
            case PolygonMode::Fill:
                return VK_POLYGON_MODE_FILL;
            case PolygonMode::Line:
                return VK_POLYGON_MODE_LINE;
            case PolygonMode::Point:
                return VK_POLYGON_MODE_POINT;
        }
        return VK_POLYGON_MODE_FILL;
    }

    inline VkSampleCountFlagBits ConvertSampleCountToVulkan(const SampleCount sampleCount) {
        switch (sampleCount) {
            case SampleCount::Count1:
                return VK_SAMPLE_COUNT_1_BIT;
            case SampleCount::Count2:
                return VK_SAMPLE_COUNT_2_BIT;
            case SampleCount::Count4:
                return VK_SAMPLE_COUNT_4_BIT;
            case SampleCount::Count8:
                return VK_SAMPLE_COUNT_8_BIT;
            case SampleCount::Count16:
                return VK_SAMPLE_COUNT_16_BIT;
        }
        return VK_SAMPLE_COUNT_1_BIT;
    }

    inline VkColorComponentFlags ConvertColorComponentFlagsToVulkan(const ColorComponentFlags flags) {
        VkColorComponentFlags result = 0;
        if (flags & static_cast<ColorComponentFlags>(ColorComponent::R))
            result |= VK_COLOR_COMPONENT_R_BIT;
        if (flags & static_cast<ColorComponentFlags>(ColorComponent::G))
            result |= VK_COLOR_COMPONENT_G_BIT;
        if (flags & static_cast<ColorComponentFlags>(ColorComponent::B))
            result |= VK_COLOR_COMPONENT_B_BIT;
        if (flags & static_cast<ColorComponentFlags>(ColorComponent::A))
            result |= VK_COLOR_COMPONENT_A_BIT;
        return result;
    }

    inline VkBlendFactor ConvertBlendFactorToVulkan(const BlendFactor factor) {
        switch (factor) {
            case BlendFactor::Zero:                  return VK_BLEND_FACTOR_ZERO;
            case BlendFactor::One:                   return VK_BLEND_FACTOR_ONE;
            case BlendFactor::SrcColor:              return VK_BLEND_FACTOR_SRC_COLOR;
            case BlendFactor::OneMinusSrcColor:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case BlendFactor::DstColor:              return VK_BLEND_FACTOR_DST_COLOR;
            case BlendFactor::OneMinusDstColor:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case BlendFactor::SrcAlpha:              return VK_BLEND_FACTOR_SRC_ALPHA;
            case BlendFactor::OneMinusSrcAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DstAlpha:              return VK_BLEND_FACTOR_DST_ALPHA;
            case BlendFactor::OneMinusDstAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case BlendFactor::ConstantColor:         return VK_BLEND_FACTOR_CONSTANT_COLOR;
            case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            case BlendFactor::ConstantAlpha:         return VK_BLEND_FACTOR_CONSTANT_ALPHA;
            case BlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        }
        return VK_BLEND_FACTOR_ONE;
    }

    inline VkBlendOp ConvertBlendOpToVulkan(const BlendOp op) {
        switch (op) {
            case BlendOp::Add:             return VK_BLEND_OP_ADD;
            case BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
            case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
            case BlendOp::Min:             return VK_BLEND_OP_MIN;
            case BlendOp::Max:             return VK_BLEND_OP_MAX;
        }
        return VK_BLEND_OP_ADD;
    }

    inline VkStencilOp ConvertStencilOpToVulkan(const StencilOp op) {
        switch (op) {
            case StencilOp::Keep:           return VK_STENCIL_OP_KEEP;
            case StencilOp::Zero:           return VK_STENCIL_OP_ZERO;
            case StencilOp::Replace:        return VK_STENCIL_OP_REPLACE;
            case StencilOp::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            case StencilOp::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            case StencilOp::Invert:         return VK_STENCIL_OP_INVERT;
            case StencilOp::IncrementWrap:  return VK_STENCIL_OP_INCREMENT_AND_WRAP;
            case StencilOp::DecrementWrap:  return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        }
        return VK_STENCIL_OP_KEEP;
    }

    inline VkStencilFaceFlags ConvertStencilFaceToVulkan(const StencilFace face) {
        VkStencilFaceFlags flags = 0;
        if (has(face, StencilFace::Front))
            flags |= VK_STENCIL_FACE_FRONT_BIT;
        if (has(face, StencilFace::Back))
            flags |= VK_STENCIL_FACE_BACK_BIT;
        return flags;
    }

    inline uint32_t ConvertStencilBitToVulkan(const StencilBit mask) {
        return static_cast<uint32_t>(to_index(mask));
    }

    inline VkCompareOp ConvertCompareOpToVulkan(const CompareOp op) {
        switch (op) {
            case CompareOp::Never:          return VK_COMPARE_OP_NEVER;
            case CompareOp::Less:           return VK_COMPARE_OP_LESS;
            case CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
            case CompareOp::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
            case CompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
            case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
        }
        return VK_COMPARE_OP_LESS;
    }

    inline VkVertexInputRate ConvertVertexInputRateToVulkan(const VertexInputRate inputRate) {
        switch (inputRate) {
            case VertexInputRate::Vertex:
                return VK_VERTEX_INPUT_RATE_VERTEX;
            case VertexInputRate::Instance:
                return VK_VERTEX_INPUT_RATE_INSTANCE;
        }
        return VK_VERTEX_INPUT_RATE_VERTEX;
    }

    inline VkFormat ConvertVertexFormatToVulkan(const VertexFormat format) {
        switch (format) {
            case VertexFormat::Float1:
                return VK_FORMAT_R32_SFLOAT;
            case VertexFormat::Float2:
                return VK_FORMAT_R32G32_SFLOAT;
            case VertexFormat::Float3:
                return VK_FORMAT_R32G32B32_SFLOAT;
            case VertexFormat::Float4:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case VertexFormat::Int1:
                return VK_FORMAT_R32_SINT;
            case VertexFormat::Int2:
                return VK_FORMAT_R32G32_SINT;
            case VertexFormat::Int3:
                return VK_FORMAT_R32G32B32_SINT;
            case VertexFormat::Int4:
                return VK_FORMAT_R32G32B32A32_SINT;
            case VertexFormat::UInt1:
                return VK_FORMAT_R32_UINT;
            case VertexFormat::UInt2:
                return VK_FORMAT_R32G32_UINT;
            case VertexFormat::UInt3:
                return VK_FORMAT_R32G32B32_UINT;
            case VertexFormat::UInt4:
                return VK_FORMAT_R32G32B32A32_UINT;
        }
        return VK_FORMAT_UNDEFINED;
    }

    inline VkBufferUsageFlags ConvertBufferUsageToVulkan(const BufferUsage usage) {
        VkBufferUsageFlags flags = 0;
        auto u = usage;

        if (has(u, BufferUsage::VertexBuffer))
            flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (has(u, BufferUsage::IndexBuffer))
            flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (has(u, BufferUsage::UniformBuffer))
            flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (has(u, BufferUsage::StorageBuffer))
            flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (has(u, BufferUsage::TransferSource))
            flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (has(u, BufferUsage::TransferDestination))
            flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (has(u, BufferUsage::Indirect))
            flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

        return flags;
    }

    inline VmaMemoryUsage ConvertMemoryAccessToVulkan(const BufferMemoryAccess access) {
        switch (access) {
            case BufferMemoryAccess::GpuOnly:
                return VMA_MEMORY_USAGE_GPU_ONLY;
            case BufferMemoryAccess::CpuToGpu:
                return VMA_MEMORY_USAGE_CPU_TO_GPU;
            case BufferMemoryAccess::GpuToCpu:
                return VMA_MEMORY_USAGE_GPU_TO_CPU;
        }
        return VMA_MEMORY_USAGE_GPU_ONLY;
    }

    inline VkDescriptorType ConvertDescriptorTypeToVulkan(const DescriptorType type) {
        switch (type) {
            case DescriptorType::UniformBuffer:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case DescriptorType::StorageBuffer:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case DescriptorType::CombinedImageSampler:
                return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case DescriptorType::SampledImage:
            case DescriptorType::Sampler:
            case DescriptorType::StorageImage:
                spdlog::error("Descriptor type {} not implemented yet", static_cast<int>(type));
        }
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }

    inline VkShaderStageFlags ConvertShaderStageFlagsToVulkan(const ShaderStageFlags flags) {
        VkShaderStageFlags result = 0;
        if (has(flags, ShaderStageFlags::Vertex))
            result |= VK_SHADER_STAGE_VERTEX_BIT;
        if (has(flags, ShaderStageFlags::Geometry))
            result |= VK_SHADER_STAGE_GEOMETRY_BIT;
        if (has(flags, ShaderStageFlags::Fragment))
            result |= VK_SHADER_STAGE_FRAGMENT_BIT;
        return result;
    }

    inline VkImageUsageFlags ConvertTextureUsageToVulkan(const TextureUsage usage) {
        VkImageUsageFlags flags = 0;
        if (has(usage, TextureUsage::ColorAttachment))
            flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (has(usage, TextureUsage::DepthAttachment))
            flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (has(usage, TextureUsage::Sampled))
            flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (has(usage, TextureUsage::TransferSrc))
            flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (has(usage, TextureUsage::TransferDst))
            flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        return flags;
    }

    inline VkFormat ConvertTextureFormatToVulkan(const TextureFormat format) {
        switch (format) {
            case TextureFormat::RGBA8:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case TextureFormat::RGB8:
                return VK_FORMAT_R8G8B8_UNORM;
            case TextureFormat::R8:
                return VK_FORMAT_R8_UNORM;
            case TextureFormat::BGRA8:
                return VK_FORMAT_B8G8R8A8_UNORM;
            case TextureFormat::D32Float:
                return VK_FORMAT_D32_SFLOAT;
            case TextureFormat::D24S8:
                return VK_FORMAT_D24_UNORM_S8_UINT;
        }
        return VK_FORMAT_UNDEFINED;
    }

    inline VkFilter ConvertSamplerFilterToVulkan(const TextureFilter filter) {
        switch (filter) {
            case TextureFilter::Linear:
                return VK_FILTER_LINEAR;
            case TextureFilter::Nearest:
                return VK_FILTER_NEAREST;
        }
        return VK_FILTER_LINEAR;
    }

    inline VkSamplerAddressMode ConvertSamplerAddressModeToVulkan(const TextureWrap wrap) {
        switch (wrap) {
            case TextureWrap::Repeat:
                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case TextureWrap::ClampToEdge:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case TextureWrap::ClampToBorder:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        }
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

} // namespace OZZ::rendering::vk