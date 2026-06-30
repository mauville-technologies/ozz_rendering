#pragma once

#include <ozz_rendering/rhi_barrier.h>
#include <ozz_rendering/rhi_buffer.h>
#include <ozz_rendering/rhi_descriptors.h>
#include <ozz_rendering/rhi_pipeline_state.h>
#include <ozz_rendering/rhi_shader.h>
#include <ozz_rendering/rhi_texture.h>
#include <ozz_rendering/rhi_types.h>

#include <webgpu/webgpu.h>

namespace OZZ::rendering::webgpu {

    inline WGPUTextureFormat ToWebGPU(TextureFormat fmt) {
        switch (fmt) {
            case TextureFormat::RGBA8:       return WGPUTextureFormat_RGBA8Unorm;
            case TextureFormat::RGBA8_SRGB:  return WGPUTextureFormat_RGBA8UnormSrgb;
            case TextureFormat::RGBA16Float: return WGPUTextureFormat_RGBA16Float;
            case TextureFormat::RGB8:        return WGPUTextureFormat_RGBA8Unorm;
            case TextureFormat::R8:          return WGPUTextureFormat_R8Unorm;
            case TextureFormat::BGRA8:       return WGPUTextureFormat_BGRA8Unorm;
            case TextureFormat::D32Float:    return WGPUTextureFormat_Depth32Float;
            case TextureFormat::D24S8:       return WGPUTextureFormat_Depth24PlusStencil8;
        }
        return WGPUTextureFormat_RGBA8Unorm;
    }

    inline WGPUTextureUsage ToWebGPU(TextureUsage usage) {
        WGPUTextureUsage flags = WGPUTextureUsage_None;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(TextureUsage::Sampled))
            flags |= WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(TextureUsage::ColorAttachment))
            flags |= WGPUTextureUsage_RenderAttachment;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(TextureUsage::DepthAttachment))
            flags |= WGPUTextureUsage_RenderAttachment;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(TextureUsage::TransferSrc))
            flags |= WGPUTextureUsage_CopySrc;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(TextureUsage::TransferDst))
            flags |= WGPUTextureUsage_CopyDst;
        return flags;
    }

    inline WGPUFilterMode ToWebGPU(TextureFilter f) {
        return f == TextureFilter::Linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    }

    inline WGPUMipmapFilterMode ToWebGPUMipmap(TextureFilter f) {
        return f == TextureFilter::Linear ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
    }

    inline WGPUAddressMode ToWebGPU(TextureWrap w) {
        switch (w) {
            case TextureWrap::Repeat:        return WGPUAddressMode_Repeat;
            case TextureWrap::ClampToEdge:   return WGPUAddressMode_ClampToEdge;
            case TextureWrap::ClampToBorder: return WGPUAddressMode_ClampToEdge;
        }
        return WGPUAddressMode_Repeat;
    }

    inline WGPUBufferUsage ToWebGPU(BufferUsage usage) {
        WGPUBufferUsage flags = WGPUBufferUsage_None;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(BufferUsage::VertexBuffer))
            flags |= WGPUBufferUsage_Vertex;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(BufferUsage::IndexBuffer))
            flags |= WGPUBufferUsage_Index;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(BufferUsage::UniformBuffer))
            flags |= WGPUBufferUsage_Uniform;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(BufferUsage::StorageBuffer))
            flags |= WGPUBufferUsage_Storage;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(BufferUsage::TransferSource))
            flags |= WGPUBufferUsage_CopySrc;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(BufferUsage::TransferDestination))
            flags |= WGPUBufferUsage_CopyDst;
        if (static_cast<uint8_t>(usage) & static_cast<uint8_t>(BufferUsage::Indirect))
            flags |= WGPUBufferUsage_Indirect;
        return flags;
    }

    inline WGPUShaderStage ToWebGPU(ShaderStageFlags stages) {
        WGPUShaderStage flags = WGPUShaderStage_None;
        if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStageFlags::Vertex))
            flags |= WGPUShaderStage_Vertex;
        if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStageFlags::Fragment))
            flags |= WGPUShaderStage_Fragment;
        return flags;
    }

    inline WGPUPrimitiveTopology ToWebGPU(PrimitiveTopology topo) {
        switch (topo) {
            case PrimitiveTopology::TriangleList:  return WGPUPrimitiveTopology_TriangleList;
            case PrimitiveTopology::TriangleStrip: return WGPUPrimitiveTopology_TriangleStrip;
            case PrimitiveTopology::LineList:      return WGPUPrimitiveTopology_LineList;
            case PrimitiveTopology::LineStrip:     return WGPUPrimitiveTopology_LineStrip;
            case PrimitiveTopology::PointList:     return WGPUPrimitiveTopology_PointList;
        }
        return WGPUPrimitiveTopology_TriangleList;
    }

    inline WGPUCullMode ToWebGPU(CullMode cull) {
        switch (cull) {
            case CullMode::None:         return WGPUCullMode_None;
            case CullMode::Front:        return WGPUCullMode_Front;
            case CullMode::Back:         return WGPUCullMode_Back;
            case CullMode::FrontAndBack: return WGPUCullMode_None;
        }
        return WGPUCullMode_None;
    }

    inline WGPUFrontFace ToWebGPU(FrontFace ff) {
        return ff == FrontFace::CounterClockwise ? WGPUFrontFace_CCW : WGPUFrontFace_CW;
    }

    inline WGPUCompareFunction ToWebGPU(CompareOp op) {
        switch (op) {
            case CompareOp::Never:          return WGPUCompareFunction_Never;
            case CompareOp::Less:           return WGPUCompareFunction_Less;
            case CompareOp::Equal:          return WGPUCompareFunction_Equal;
            case CompareOp::LessOrEqual:    return WGPUCompareFunction_LessEqual;
            case CompareOp::Greater:        return WGPUCompareFunction_Greater;
            case CompareOp::NotEqual:       return WGPUCompareFunction_NotEqual;
            case CompareOp::GreaterOrEqual: return WGPUCompareFunction_GreaterEqual;
            case CompareOp::Always:         return WGPUCompareFunction_Always;
        }
        return WGPUCompareFunction_Always;
    }

    inline WGPUStencilOperation ToWebGPU(StencilOp op) {
        switch (op) {
            case StencilOp::Keep:           return WGPUStencilOperation_Keep;
            case StencilOp::Zero:           return WGPUStencilOperation_Zero;
            case StencilOp::Replace:        return WGPUStencilOperation_Replace;
            case StencilOp::IncrementClamp: return WGPUStencilOperation_IncrementClamp;
            case StencilOp::DecrementClamp: return WGPUStencilOperation_DecrementClamp;
            case StencilOp::Invert:         return WGPUStencilOperation_Invert;
            case StencilOp::IncrementWrap:  return WGPUStencilOperation_IncrementWrap;
            case StencilOp::DecrementWrap:  return WGPUStencilOperation_DecrementWrap;
        }
        return WGPUStencilOperation_Keep;
    }

    inline WGPUBlendFactor ToWebGPU(BlendFactor f) {
        switch (f) {
            case BlendFactor::Zero:                  return WGPUBlendFactor_Zero;
            case BlendFactor::One:                   return WGPUBlendFactor_One;
            case BlendFactor::SrcColor:              return WGPUBlendFactor_Src;
            case BlendFactor::OneMinusSrcColor:      return WGPUBlendFactor_OneMinusSrc;
            case BlendFactor::DstColor:              return WGPUBlendFactor_Dst;
            case BlendFactor::OneMinusDstColor:      return WGPUBlendFactor_OneMinusDst;
            case BlendFactor::SrcAlpha:              return WGPUBlendFactor_SrcAlpha;
            case BlendFactor::OneMinusSrcAlpha:      return WGPUBlendFactor_OneMinusSrcAlpha;
            case BlendFactor::DstAlpha:              return WGPUBlendFactor_DstAlpha;
            case BlendFactor::OneMinusDstAlpha:      return WGPUBlendFactor_OneMinusDstAlpha;
            case BlendFactor::ConstantColor:         return WGPUBlendFactor_Constant;
            case BlendFactor::OneMinusConstantColor: return WGPUBlendFactor_OneMinusConstant;
            case BlendFactor::ConstantAlpha:         return WGPUBlendFactor_Constant;
            case BlendFactor::OneMinusConstantAlpha: return WGPUBlendFactor_OneMinusConstant;
        }
        return WGPUBlendFactor_One;
    }

    inline WGPUBlendOperation ToWebGPU(BlendOp op) {
        switch (op) {
            case BlendOp::Add:             return WGPUBlendOperation_Add;
            case BlendOp::Subtract:        return WGPUBlendOperation_Subtract;
            case BlendOp::ReverseSubtract: return WGPUBlendOperation_ReverseSubtract;
            case BlendOp::Min:             return WGPUBlendOperation_Min;
            case BlendOp::Max:             return WGPUBlendOperation_Max;
        }
        return WGPUBlendOperation_Add;
    }

    inline WGPUColorWriteMask ToWebGPU(ColorComponentFlags mask) {
        WGPUColorWriteMask flags = WGPUColorWriteMask_None;
        if (mask & static_cast<uint8_t>(ColorComponent::R)) flags |= WGPUColorWriteMask_Red;
        if (mask & static_cast<uint8_t>(ColorComponent::G)) flags |= WGPUColorWriteMask_Green;
        if (mask & static_cast<uint8_t>(ColorComponent::B)) flags |= WGPUColorWriteMask_Blue;
        if (mask & static_cast<uint8_t>(ColorComponent::A)) flags |= WGPUColorWriteMask_Alpha;
        return flags;
    }

    inline WGPUVertexFormat ToWebGPU(VertexFormat fmt) {
        switch (fmt) {
            case VertexFormat::Float1: return WGPUVertexFormat_Float32;
            case VertexFormat::Float2: return WGPUVertexFormat_Float32x2;
            case VertexFormat::Float3: return WGPUVertexFormat_Float32x3;
            case VertexFormat::Float4: return WGPUVertexFormat_Float32x4;
            case VertexFormat::Int1:   return WGPUVertexFormat_Sint32;
            case VertexFormat::Int2:   return WGPUVertexFormat_Sint32x2;
            case VertexFormat::Int3:   return WGPUVertexFormat_Sint32x3;
            case VertexFormat::Int4:   return WGPUVertexFormat_Sint32x4;
            case VertexFormat::UInt1:  return WGPUVertexFormat_Uint32;
            case VertexFormat::UInt2:  return WGPUVertexFormat_Uint32x2;
            case VertexFormat::UInt3:  return WGPUVertexFormat_Uint32x3;
            case VertexFormat::UInt4:  return WGPUVertexFormat_Uint32x4;
        }
        return WGPUVertexFormat_Float32x3;
    }

    inline WGPUVertexStepMode ToWebGPU(VertexInputRate rate) {
        return rate == VertexInputRate::Vertex ? WGPUVertexStepMode_Vertex : WGPUVertexStepMode_Instance;
    }

    inline WGPULoadOp ToWebGPU(LoadOp op) {
        switch (op) {
            case LoadOp::Load:     return WGPULoadOp_Load;
            case LoadOp::Clear:    return WGPULoadOp_Clear;
            case LoadOp::DontCare: return WGPULoadOp_Clear;
        }
        return WGPULoadOp_Clear;
    }

    inline WGPUStoreOp ToWebGPU(StoreOp op) {
        switch (op) {
            case StoreOp::Store:    return WGPUStoreOp_Store;
            case StoreOp::DontCare: return WGPUStoreOp_Discard;
        }
        return WGPUStoreOp_Store;
    }

} // namespace OZZ::rendering::webgpu
