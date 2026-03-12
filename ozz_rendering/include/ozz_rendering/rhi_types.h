//
// Created by paulm on 2026-02-22.
//

#pragma once

#include <cstdint>
#include <ozz_rendering/utils/enums.h>

namespace OZZ::rendering {

    enum class LoadOp {
        Load,
        Clear,
        DontCare,
    };

    enum class StoreOp {
        Store,
        DontCare,
    };

    enum class TextureLayout {
        Undefined,
        ColorAttachment,
        DepthStencilAttachment,
        ShaderReadOnly,
        TransferSrc,
        TransferDst,
        Present,
    };

    enum class PrimitiveTopology {
        TriangleList,
        TriangleStrip,
        LineList,
        LineStrip,
        PointList,
    };

    enum class CullMode {
        None,
        Front,
        Back,
        FrontAndBack,
    };

    enum class FrontFace {
        CounterClockwise,
        Clockwise,
    };

    enum class PolygonMode {
        Fill,
        Line,
        Point,
    };

    enum class SampleCount {
        Count1 = 1,
        Count2 = 2,
        Count4 = 4,
        Count8 = 8,
        Count16 = 16,
    };

    enum class PipelineStage {
        None,
        ColorAttachmentOutput,
        Transfer,
        VertexShader,
        FragmentShader,
        EarlyFragmentTests,
        AllGraphics,
        AllCommands,
    };

    enum class Access {
        None,
        ColorAttachmentRead,
        ColorAttachmentWrite,
        ShaderRead,
        ShaderWrite,
        TransferRead,
        TransferWrite,
        DepthStencilAttachmentRead,
        DepthStencilAttachmentWrite,
    };

    enum class TextureAspect {
        Color = 1 << 0,
        Depth = 1 << 1,
        Stencil = 1 << 2,
    };

    struct TextureSubresourceRange {
        TextureAspect Aspect {TextureAspect::Color};
        uint32_t BaseMipLevel {0};
        uint32_t LevelCount {1};
        uint32_t BaseArrayLayer {0};
        uint32_t LayerCount {1};
    };

    inline constexpr uint32_t QueueFamilyIgnored = ~0u;

    enum class ColorComponent : uint8_t {
        R = 1 << 0,
        G = 1 << 1,
        B = 1 << 2,
        A = 1 << 3,
        All = 0xF,
    };

    using ColorComponentFlags = uint8_t;

    enum class BlendFactor {
        Zero,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
        ConstantColor,
        OneMinusConstantColor,
        ConstantAlpha,
        OneMinusConstantAlpha,
    };

    enum class StencilFace : uint8_t {
        Front = 1 << 0,
        Back  = 1 << 1,
    };

    enum class StencilBit : uint8_t {
        Bit0 = 1 << 0,
        Bit1 = 1 << 1,
        Bit2 = 1 << 2,
        Bit3 = 1 << 3,
        Bit4 = 1 << 4,
        Bit5 = 1 << 5,
        Bit6 = 1 << 6,
        Bit7 = 1 << 7,
        All  = 0xFF,
    };

    enum class StencilOp {
        Keep,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap,
    };

    enum class CompareOp {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };

    enum class BlendOp {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    enum class VertexInputRate {
        Vertex,
        Instance,
    };

    enum class VertexFormat {
        Float1,
        Float2,
        Float3,
        Float4,
        Int1,
        Int2,
        Int3,
        Int4,
        UInt1,
        UInt2,
        UInt3,
        UInt4,
    };

    struct ClearValue {
        float R {0.f};
        float G {0.f};
        float B {0.f};
        float A {1.f};
        float Depth {1.f};
        uint32_t Stencil {0};
    };

    struct Viewport {
        float X {0.f};
        float Y {0.f};
        float Width {0.f};
        float Height {0.f};
        float MinDepth {0.f};
        float MaxDepth {1.f};
    };

    struct Scissor {
        int32_t X {0};
        int32_t Y {0};
        uint32_t Width {0};
        uint32_t Height {0};
    };

} // namespace OZZ::rendering

template <>
struct enable_bitmask_operators<OZZ::rendering::TextureAspect> : std::true_type {};

template <>
struct enable_bitmask_operators<OZZ::rendering::ColorComponent> : std::true_type {};

template <>
struct enable_bitmask_operators<OZZ::rendering::StencilFace> : std::true_type {};

template <>
struct enable_bitmask_operators<OZZ::rendering::StencilBit> : std::true_type {};