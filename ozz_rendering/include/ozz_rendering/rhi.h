//
// Created by paulm on 2026-02-21.
//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <ozz_rendering/rhi_barrier.h>
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

    class RHIDevice {
    public:
        virtual ~RHIDevice() = default;
        RHIDevice() = delete;

    protected:
        // doing it this way will force the child classes to take in the platform context, which is necessary for
        // initialization, but allows the base class to be agnostic of the platform context details
        explicit RHIDevice(const PlatformContext&) {};

        // Frame
        virtual RHICommandBufferHandle BeginFrame() = 0;
        virtual void SubmitFrame(const RHICommandBufferHandle&) = 0;

        // Render pass
        virtual void BeginRenderPass(const RHICommandBufferHandle&, const RenderPassDescriptor&) = 0;
        virtual void EndRenderPass(const RHICommandBufferHandle&) = 0;

        // Resource barriers
        virtual void ResourceBarrier(const RHICommandBufferHandle&, const TextureBarrierDescriptor&) = 0;

        // Viewport / scissor
        virtual void SetViewport(const RHICommandBufferHandle&, const Viewport&) = 0;
        virtual void SetScissor(const RHICommandBufferHandle&, const Scissor&) = 0;

        // Pipeline state
        virtual void SetGraphicsState(const RHICommandBufferHandle&, const GraphicsStateDescriptor&) = 0;

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
                                 int32_t  vertexOffset,
                                 uint32_t firstInstance) = 0;

        // Resource creation
        virtual RHITextureHandle CreateTexture() = 0;
    };

    std::unique_ptr<RHIDevice> CreateRHIDevice(const RHIInitParams&);
} // namespace OZZ::rendering