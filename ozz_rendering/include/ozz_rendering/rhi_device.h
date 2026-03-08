//
// Created by paulm on 2026-02-21.
//

#pragma once

#include "rhi_shader.h"

#include <functional>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include <ozz_rendering/rhi_barrier.h>
#include <ozz_rendering/rhi_buffer.h>
#include <ozz_rendering/rhi_descriptors.h>
#include <ozz_rendering/rhi_handle.h>
#include <ozz_rendering/rhi_pipeline_state.h>
#include <ozz_rendering/rhi_renderpass.h>
#include <ozz_rendering/rhi_texture.h>
#include <ozz_rendering/rhi_types.h>

namespace OZZ::rendering {

    enum class RHIBackend {
        Auto,
        Vulkan,
        OpenGL,
    };

    struct PlatformContext {
        std::string AppName {"ozz_rendering_app"};
        std::tuple<int, int, int, int> AppVersion {1, 0, 0, 0};
        std::string EngineName {"ozz_rendering_engine"};
        std::tuple<int, int, int, int> EngineVersion {1, 0, 0, 0};

        void* WindowHandle {nullptr};
        std::vector<std::string> RequiredInstanceExtensions;
        std::function<std::pair<int, int>()> GetWindowFramebufferSizeFunction {};
        // CreateSurfaceFunction takes opaque pointers (instance, surface_out) to remain backend-agnostic.
        // For Vulkan: instance is VkInstance, surface_out is VkSurfaceKHR*
        std::function<bool(void*, void*)> CreateSurfaceFunction {};
    };

    struct RHIInitParams {
        RHIBackend Backend {RHIBackend::Auto};
        PlatformContext Context {};
    };

    class RHIFrameContext {
    public:
        // Only thing app-level renderables can do:
        [[nodiscard]] RHICommandBufferHandle GetCommandBuffer() const { return commandBuffer; }

        [[nodiscard]] RHITextureHandle GetBackbufferImage() const { return colorAttachment; }

        [[nodiscard]] RHITextureHandle GetBackbufferDepthImage() const {
            return depthAttachment;
        } // for now, assume combined depth+color

        // Non-copyable - one frame in flight at a time
        RHIFrameContext(const RHIFrameContext&) = delete;
        RHIFrameContext& operator=(const RHIFrameContext&) = delete;
        RHIFrameContext(RHIFrameContext&&) = default;

        [[nodiscard]] bool IsValid() const { return commandBuffer.IsValid() && colorAttachment.IsValid(); }

        static RHIFrameContext Null() {
            return {RHICommandBufferHandle::Null(), RHITextureHandle::Null(), RHITextureHandle::Null(), 0, 0};
        }

    private:
        // Only RHIDevice can construct this
        friend class RHIDevice;

        RHIFrameContext(RHICommandBufferHandle cmd,
                        RHITextureHandle colorImage,
                        RHITextureHandle depthImage,
                        uint32_t imageIndex,
                        uint32_t frameIndex)
            : commandBuffer(cmd)
            , colorAttachment(colorImage)
            , depthAttachment(depthImage)
            , imageIndex(imageIndex)    // hidden from app
            , frameIndex(frameIndex) {} // hidden from app

        RHICommandBufferHandle commandBuffer;
        RHITextureHandle colorAttachment;
        RHITextureHandle depthAttachment;

        uint32_t imageIndex; // raw swapchain index - internal only
        uint32_t frameIndex; // frame-in-flight index - internal only
    };

    class RHIDevice {
    public:
        virtual ~RHIDevice() = default;
        RHIDevice() = delete;

        // Frame
        virtual RHIFrameContext BeginFrame() = 0;
        virtual void SubmitAndPresentFrame(RHIFrameContext&& frameContext) = 0;

        // Command Buffer Recording - Render Pass
        virtual void BeginRenderPass(const RHIFrameContext& frameContext,
                                     const RenderPassDescriptor& renderPassDescriptor) = 0;
        virtual void EndRenderPass(const RHIFrameContext& frameContext) = 0;

        // Command Buffer Recording - Barriers
        virtual void TextureResourceBarrier(const RHIFrameContext& frameContext,
                                            const TextureBarrierDescriptor& textureBarrierDescriptor) = 0;
        virtual void BufferMemoryBarrier(const RHIFrameContext& frameContext,
                                         const BufferBarrierDescriptor& bufferBarrierDescriptor) = 0;

        // Command Buffer Recording - State
        virtual void SetViewport(const RHIFrameContext& frameContext, const Viewport& viewport) = 0;
        virtual void SetScissor(const RHIFrameContext& frameContext, const Scissor& scissor) = 0;
        virtual void SetGraphicsState(const RHIFrameContext& frameContext,
                                      const GraphicsStateDescriptor& graphicsStateDescriptor) = 0;

        // Command Buffer Recording - Binding
        virtual void BindShader(const RHIFrameContext& frameContext, const RHIShaderHandle& shaderHandle) = 0;
        virtual void BindBuffer(const RHIFrameContext& frameContext, RHIBufferHandle& bufferHandle) = 0;
        virtual void SetPushConstants(const RHIFrameContext& frameContext,
                                      RHIPipelineLayoutHandle pipelineLayoutHandle,
                                      ShaderStageFlags stageFlags,
                                      uint32_t offset,
                                      uint32_t size,
                                      const void* data) = 0;
        virtual void BindDescriptorSet(const RHIFrameContext& frameContext,
                                       RHIPipelineLayoutHandle pipelineLayoutHandle,
                                       uint32_t setIndex,
                                       RHIDescriptorSetHandle descriptorSetHandle) = 0;

        // Command Buffer Recording - Draw
        virtual void Draw(const RHIFrameContext& frameContext,
                          uint32_t vertexCount,
                          uint32_t instanceCount,
                          uint32_t firstVertex,
                          uint32_t firstInstance) = 0;
        virtual void DrawIndexed(const RHIFrameContext& frameContext,
                                 uint32_t indexCount,
                                 uint32_t instanceCount,
                                 uint32_t firstIndex,
                                 int32_t vertexOffset,
                                 uint32_t firstInstance) = 0;

        // Descriptor Sets
        virtual RHIDescriptorSetHandle CreateDescriptorSet(RHIDescriptorSetLayoutHandle layoutHandle) = 0;
        virtual void UpdateDescriptorSet(RHIDescriptorSetHandle handle, std::span<const RHIDescriptorWrite> writes) = 0;
        virtual void FreeDescriptorSet(RHIDescriptorSetHandle handle) = 0;

        // Resource Creation
        virtual RHITextureHandle CreateTexture(TextureDescriptor&& descriptor) = 0;
        virtual void UpdateTexture(const RHITextureHandle& handle, const void* data, size_t size) = 0;
        virtual void FreeTexture(RHITextureHandle handle) = 0;

        virtual RHIShaderHandle CreateShader(ShaderFileParams&& fileParams) = 0;
        virtual RHIShaderHandle CreateShader(ShaderSourceParams&& sourceParams) = 0;
        virtual void FreeShader(const RHIShaderHandle& shaderHandle) = 0;
        virtual RHIPipelineLayoutDescriptor GetShaderPipelineLayout(const RHIShaderHandle& shaderHandle) = 0;
        virtual RHIPipelineLayoutHandle GetShaderPipelineLayoutHandle(const RHIShaderHandle& shaderHandle) = 0;
        virtual std::vector<RHIDescriptorSetLayoutHandle>
        GetShaderDescriptorSetLayoutHandles(const RHIShaderHandle& shaderHandle) = 0;
        virtual std::pair<RHIPipelineLayoutHandle, std::set<RHIDescriptorSetLayoutHandle>>
        CreatePipelineLayout(const RHIPipelineLayoutDescriptor& pipelineLayoutDescriptor) = 0;
        virtual RHIDescriptorSetLayoutHandle
        CreateDescriptorSetLayout(const RHIDescriptorSetLayoutDescriptor& descriptorSetLayoutDescriptor) = 0;

        virtual RHIBufferHandle CreateBuffer(BufferDescriptor&& bufferDescriptor) = 0;
        virtual void UpdateBuffer(const RHIBufferHandle&, const void* data, size_t size, size_t offset) = 0;

    protected:
        // doing it this way will force the child classes to take in the platform context, which is necessary for
        // initialization, but allows the base class to be agnostic of the platform context details
        explicit RHIDevice(const PlatformContext&) {};

        static RHIFrameContext BuildFrameContext(RHICommandBufferHandle cmd,
                                                 RHITextureHandle colorImage,
                                                 RHITextureHandle depthImage,
                                                 uint32_t imageIndex,
                                                 uint32_t frameIndex) {
            return {cmd, colorImage, depthImage, imageIndex, frameIndex};
        }

        static uint32_t GetFrameNumberFromFrameContext(const RHIFrameContext& context) { return context.frameIndex; }

        static uint32_t GetImageIndexFromFrameContext(const RHIFrameContext& context) { return context.imageIndex; }
    };

    std::unique_ptr<RHIDevice> CreateRHIDevice(const RHIInitParams&);
} // namespace OZZ::rendering