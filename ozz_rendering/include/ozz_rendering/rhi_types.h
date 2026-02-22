//
// Created by paulm on 2026-02-22.
//

#pragma once

#include <cstdint>

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
        Count1  = 1,
        Count2  = 2,
        Count4  = 4,
        Count8  = 8,
        Count16 = 16,
    };

    enum class ColorComponent : uint8_t {
        R   = 1 << 0,
        G   = 1 << 1,
        B   = 1 << 2,
        A   = 1 << 3,
        All = 0xF,
    };

    using ColorComponentFlags = uint8_t;

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
        float    R       {0.f};
        float    G       {0.f};
        float    B       {0.f};
        float    A       {1.f};
        float    Depth   {1.f};
        uint32_t Stencil {0};
    };

    struct Viewport {
        float X        {0.f};
        float Y        {0.f};
        float Width    {0.f};
        float Height   {0.f};
        float MinDepth {0.f};
        float MaxDepth {1.f};
    };

    struct Scissor {
        int32_t  X      {0};
        int32_t  Y      {0};
        uint32_t Width  {0};
        uint32_t Height {0};
    };

} // namespace OZZ::rendering
