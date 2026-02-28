//
// Created by paulm on 2026-02-22.
//

#pragma once

#include <ozz_rendering/rhi_handle.h>
#include <ozz_rendering/rhi_types.h>

namespace OZZ::rendering {

    struct TextureBarrierDescriptor {
        RHITextureHandle Texture {};
        TextureLayout OldLayout {TextureLayout::Undefined};
        TextureLayout NewLayout {TextureLayout::Undefined};
        PipelineStage SrcStage {PipelineStage::None};
        PipelineStage DstStage {PipelineStage::None};
        Access SrcAccess {Access::None};
        Access DstAccess {Access::None};
        TextureSubresourceRange SubresourceRange {};
        uint32_t SrcQueueFamily {QueueFamilyIgnored};
        uint32_t DstQueueFamily {QueueFamilyIgnored};
    };

    struct BufferBarrierDescriptor {
        RHIBufferHandle Buffer {};
        size_t Offset {0};
        size_t Size {0};
        PipelineStage SrcStage {PipelineStage::None};
        PipelineStage DstStage {PipelineStage::None};
        Access SrcAccess {Access::None};
        Access DstAccess {Access::None};
        uint32_t SrcQueueFamily {QueueFamilyIgnored};
        uint32_t DstQueueFamily {QueueFamilyIgnored};
    };
} // namespace OZZ::rendering
