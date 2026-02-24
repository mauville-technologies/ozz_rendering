//
// Created by paulm on 2026-02-22.
//

#pragma once

#include <cstdint>
#include <ozz_rendering/rhi_types.h>

namespace OZZ::rendering {

    inline constexpr uint32_t MaxVertexBindings = 16;
    inline constexpr uint32_t MaxVertexAttributes = 16;
    inline constexpr uint32_t MaxBlendAttachments = 8;

    struct InputAssemblyState {
        PrimitiveTopology Topology {PrimitiveTopology::TriangleList};
        bool PrimitiveRestartEnable {false};
    };

    struct RasterizationState {
        CullMode Cull {CullMode::None};
        FrontFace Front {FrontFace::CounterClockwise};
        PolygonMode Polygon {PolygonMode::Fill};
        bool DepthBiasEnable {false};
        bool RasterizerDiscard {false};
    };

    struct DepthStencilState {
        bool DepthTestEnable {false};
        bool DepthWriteEnable {false};
        bool StencilTestEnable {false};
    };

    struct MultisampleState {
        SampleCount Samples {SampleCount::Count1};
        uint32_t SampleMask {0xFFFFFFFF};
        bool AlphaToCoverageEnable {false};
    };

    struct ColorBlendAttachmentState {
        bool BlendEnable {false};
        ColorComponentFlags ColorWriteMask {
            static_cast<ColorComponentFlags>(ColorComponent::All),
        };
    };

    struct VertexInputBindingDescriptor {
        uint32_t Binding {0};
        uint32_t Stride {0};
        VertexInputRate InputRate {VertexInputRate::Vertex};
    };

    struct VertexInputAttributeDescriptor {
        uint32_t Location {0};
        uint32_t Binding {0};
        VertexFormat Format {VertexFormat::Float3};
        uint32_t Offset {0};
    };

    struct VertexInputState {
        VertexInputBindingDescriptor Bindings[MaxVertexBindings] {};
        uint32_t BindingCount {0};
        VertexInputAttributeDescriptor Attributes[MaxVertexAttributes] {};
        uint32_t AttributeCount {0};
    };

    struct GraphicsStateDescriptor {
        InputAssemblyState InputAssembly {};
        RasterizationState Rasterization {};
        DepthStencilState DepthStencil {};
        MultisampleState Multisample {};
        ColorBlendAttachmentState ColorBlend[MaxBlendAttachments] {};
        uint32_t ColorBlendAttachmentCount {0};
        VertexInputState VertexInput {};
    };

} // namespace OZZ::rendering
