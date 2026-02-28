//
// Created by paulm on 2026-02-21.
//

#pragma once

#include "rhi_shader.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <ozz_rendering/rhi_barrier.h>
#include <ozz_rendering/rhi_buffer.h>
#include <ozz_rendering/rhi_handle.h>
#include <ozz_rendering/rhi_pipeline_state.h>
#include <ozz_rendering/rhi_renderpass.h>
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

    class FrameContext {
    public:
        // Only thing app-level renderables can do:
        RHICommandBufferHandle GetCommandBuffer() { return commandBuffer; }

        RHITextureHandle GetBackbuffer() { return backbuffer; }

        // Non-copyable - one frame in flight at a time
        FrameContext(const FrameContext&) = delete;
        FrameContext& operator=(const FrameContext&) = delete;
        FrameContext(FrameContext&&) = default;

        [[nodiscard]] bool IsValid() const { return commandBuffer.IsValid() && backbuffer.IsValid(); }

        static FrameContext Null() { return {RHICommandBufferHandle::Null(), RHITextureHandle::Null(), 0, 0}; }

    private:
        // Only RHIDevice can construct this
        friend class RHIDevice;

        FrameContext(RHICommandBufferHandle cmd, RHITextureHandle backbuffer, uint32_t imageIndex, uint32_t frameIndex)
            : commandBuffer(cmd)
            , backbuffer(backbuffer)
            , imageIndex(imageIndex)    // hidden from app
            , frameIndex(frameIndex) {} // hidden from app

        RHICommandBufferHandle commandBuffer;
        RHITextureHandle backbuffer;

        uint32_t imageIndex; // raw swapchain index - internal only
        uint32_t frameIndex; // frame-in-flight index - internal only
    };

    class RHIDevice {
    public:
        virtual ~RHIDevice() = default;
        RHIDevice() = delete;

        // Frame
        virtual FrameContext BeginFrame() = 0;
        virtual void SubmitAndPresentFrame(FrameContext frameContext) = 0;

        // Render pass
        virtual void BeginRenderPass(const RHICommandBufferHandle& commandBufferHandle,
                                     const RenderPassDescriptor& renderPassDescriptor) = 0;
        virtual void EndRenderPass(const RHICommandBufferHandle& commandBufferHandle) = 0;

        // Resource barriers
        virtual void TextureResourceBarrier(const RHICommandBufferHandle& commandBufferHandle,
                                            const TextureBarrierDescriptor& textureBarrierDescriptor) = 0;
        virtual void BufferMemoryBarrier(const RHICommandBufferHandle& commandBufferHandle,
                                         const BufferBarrierDescriptor& bufferBarrierDescriptor) = 0;

        // Viewport / scissor
        virtual void SetViewport(const RHICommandBufferHandle& commandBufferHandle, const Viewport& viewport) = 0;
        virtual void SetScissor(const RHICommandBufferHandle& commandBufferHandle, const Scissor& scissor) = 0;

        // Pipeline state
        virtual void SetGraphicsState(const RHICommandBufferHandle& commandBufferHandle,
                                      const GraphicsStateDescriptor& graphicsStateDescriptor) = 0;

        // Draw
        virtual void Draw(const RHICommandBufferHandle&,
                          uint32_t vertexCount,
                          uint32_t instanceCount,
                          uint32_t firstVertex,
                          uint32_t firstInstance) = 0;

        virtual void DrawIndexed(const RHICommandBufferHandle&,
                                 uint32_t indexCount,
                                 uint32_t instanceCount,
                                 uint32_t firstIndex,
                                 int32_t vertexOffset,
                                 uint32_t firstInstance) = 0;

        // Resource creation
        virtual RHITextureHandle CreateTexture() = 0;

        virtual RHIShaderHandle CreateShader(ShaderFileParams&& fileParams) = 0;
        virtual RHIShaderHandle CreateShader(ShaderSourceParams&& sourceParams) = 0;
        virtual void FreeShader(const RHIShaderHandle& shaderHandle) = 0;
        virtual void BindShader(const RHICommandBufferHandle& commandBufferHandle,
                                const RHIShaderHandle& shaderHandle) = 0;

        virtual RHIBufferHandle CreateBuffer(BufferDescriptor&& bufferDescriptor) = 0;
        virtual void UpdateBuffer(const RHIBufferHandle&, const void* data, size_t size, size_t offset) = 0;
        virtual void BindBuffer(const RHICommandBufferHandle& commandBufferHandle, RHIBufferHandle& bufferHandle) = 0;

        virtual void BindUniformBuffer(const RHICommandBufferHandle& commandBufferHandle,
                                       const RHIBufferHandle& bufferHandle,
                                       uint32_t set,
                                       uint32_t binding) = 0;

        virtual void SetPushConstants(const RHICommandBufferHandle& commandBufferHandle,
                                      std::set<ShaderStage> stageFlags,
                                      uint32_t offset,
                                      uint32_t size,
                                      const void* data) = 0;

    protected:
        // doing it this way will force the child classes to take in the platform context, which is necessary for
        // initialization, but allows the base class to be agnostic of the platform context details
        explicit RHIDevice(const PlatformContext&) {};

        static FrameContext BuildFrameContext(RHICommandBufferHandle cmd,
                                              RHITextureHandle backbuffer,
                                              uint32_t imageIndex,
                                              uint32_t frameIndex) {
            return {cmd, backbuffer, imageIndex, frameIndex};
        }

        static uint32_t GetFrameNumberFromFrameContext(const FrameContext& context) { return context.frameIndex; }

        static uint32_t GetImageIndexFromFrameContext(const FrameContext& context) { return context.imageIndex; }
    };

    std::unique_ptr<RHIDevice> CreateRHIDevice(const RHIInitParams&);
} // namespace OZZ::rendering