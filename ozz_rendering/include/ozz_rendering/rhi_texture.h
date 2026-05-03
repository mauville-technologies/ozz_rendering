//
// Created by paulm on 2026-03-05.
//

#pragma once

#include <cstdint>
#include <ozz_rendering/utils/enums.h>

namespace OZZ::rendering {

    enum class TextureFormat {
        RGBA8,
        RGBA8_SRGB, // sRGB variant: GPU decodes sRGB->linear on sample, encodes linear->sRGB on write
        RGBA16Float,
        RGB8,
        R8,
        BGRA8, // color
        D32Float,
        D24S8, // depth / depth-stencil
    };

    enum class TextureUsage : uint8_t {
        Sampled = 1 << 0,         // VK_IMAGE_USAGE_SAMPLED_BIT
        ColorAttachment = 1 << 1, // VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
        DepthAttachment = 1 << 2, // VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
        TransferSrc = 1 << 3,     // VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        TransferDst = 1 << 4,     // VK_IMAGE_USAGE_TRANSFER_DST_BIT
    };

    enum class TextureFilter { Linear, Nearest };
    enum class TextureWrap { Repeat, ClampToEdge, ClampToBorder };

    struct SamplerDescriptor {
        TextureFilter MinFilter {TextureFilter::Linear};
        TextureFilter MagFilter {TextureFilter::Linear};
        TextureWrap WrapU {TextureWrap::Repeat};
        TextureWrap WrapV {TextureWrap::Repeat};
        TextureWrap WrapW {TextureWrap::Repeat};
        bool GenerateMipmaps {false};
    };

    struct TextureDescriptor {
        uint32_t Width {1};
        uint32_t Height {1};
        TextureFormat Format {TextureFormat::RGBA8};
        TextureUsage Usage {TextureUsage::Sampled};
        SamplerDescriptor Sampler {};
    };

} // namespace OZZ::rendering

template <>
struct enable_bitmask_operators<OZZ::rendering::TextureUsage> : std::true_type {};
