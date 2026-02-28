//
// Created by paulm on 2026-02-23.
//

#pragma once

#include "ozz_rendering/rhi_buffer.h"
#include <volk.h>

namespace OZZ::rendering::vk {
    inline VkPipelineStageFlags2 ConvertPipelineStageToVulkan(const PipelineStage stage) {
        switch (stage) {
            case PipelineStage::None:
                return VK_PIPELINE_STAGE_NONE;
            case PipelineStage::ColorAttachmentOutput:
                return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            case PipelineStage::Transfer:
                return VK_PIPELINE_STAGE_TRANSFER_BIT;
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
            case Access::TransferRead:
                return VK_ACCESS_2_TRANSFER_READ_BIT;
            case Access::TransferWrite:
                return VK_ACCESS_2_TRANSFER_WRITE_BIT;
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
        switch (aspect) {
            case TextureAspect::Color:
                return VK_IMAGE_ASPECT_COLOR_BIT;
            case TextureAspect::Depth:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            case TextureAspect::Stencil:
                return VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        return VK_IMAGE_ASPECT_FLAG_BITS_MAX_ENUM;
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
} // namespace OZZ::rendering::vk