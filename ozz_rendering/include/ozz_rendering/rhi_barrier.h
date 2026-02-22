//
// Created by paulm on 2026-02-22.
//

#pragma once

#include <ozz_rendering/rhi_handle.h>
#include <ozz_rendering/rhi_types.h>

namespace OZZ::rendering {

    struct TextureBarrierDescriptor {
        RHITextureHandle Texture   {};
        TextureLayout    OldLayout {TextureLayout::Undefined};
        TextureLayout    NewLayout {TextureLayout::Undefined};
    };

} // namespace OZZ::rendering
