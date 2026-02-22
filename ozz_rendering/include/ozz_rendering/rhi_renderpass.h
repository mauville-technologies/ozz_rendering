//
// Created by paulm on 2026-02-22.
//

#pragma once

#include <ozz_rendering/rhi_handle.h>
#include <ozz_rendering/rhi_types.h>
#include <cstdint>

namespace OZZ::rendering {

    inline constexpr uint32_t MaxColorAttachments = 8;

    struct RenderAreaDescriptor {
        int32_t  X      {0};
        int32_t  Y      {0};
        uint32_t Width  {0};
        uint32_t Height {0};
    };

    struct AttachmentDescriptor {
        RHITextureHandle Texture {};
        LoadOp           Load    {LoadOp::DontCare};
        StoreOp          Store   {StoreOp::Store};
        ClearValue       Clear   {};
        TextureLayout    Layout  {TextureLayout::ColorAttachment};
    };

    struct RenderPassDescriptor {
        AttachmentDescriptor ColorAttachments[MaxColorAttachments] {};
        uint32_t             ColorAttachmentCount                  {0};
        AttachmentDescriptor DepthAttachment                       {};
        AttachmentDescriptor StencilAttachment                     {};
        RenderAreaDescriptor RenderArea                            {};
        uint32_t             LayerCount                            {1};
    };

} // namespace OZZ::rendering