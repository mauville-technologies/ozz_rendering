//
// Created by paulm on 2026-02-21.
//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <ozz_rendering/rhi_renderpass.h>

namespace OZZ::rendering {

    template <typename ResourceType>
    struct RHIHandle {
        uint32_t Id {UINT32_MAX};
        uint32_t Generation {0};

        static RHIHandle<ResourceType> Null() {
            return RHIHandle<ResourceType> {
                .Id = UINT32_MAX,
                .Generation = 0,
            };
        }
    };

    using RHITextureHandle = RHIHandle<struct TextureTag>;
    using RHICommandBufferHandle = RHIHandle<struct CommandBufferTag>;

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

        // Frame commands
        virtual RHICommandBufferHandle BeginFrame() = 0;
        virtual void SubmitFrame(const RHICommandBufferHandle&) = 0;

        // Commands
        virtual void BeginRenderPass(const RHICommandBufferHandle&, const RenderPassDescriptor&) = 0;
        virtual void EndRenderPass(const RHICommandBufferHandle&) = 0;

        virtual void Draw(const RHICommandBufferHandle&,
                          uint32_t vertexCount,
                          uint32_t instanceCount,
                          uint32_t firstVertex,
                          uint32_t firstInstance) = 0;

        // Resource creation
        virtual RHITextureHandle CreateTexture() = 0;
    };

    std::unique_ptr<RHIDevice> CreateRHIDevice(const RHIInitParams&);
} // namespace OZZ::rendering