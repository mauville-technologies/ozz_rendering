//
// Created by paulm on 2026-02-21.
//

#pragma once

#include "ozz_rendering/utils/resource_pool.h"
#include "rhi_buffer_vulkan.h"

#include "rhi_shader_vulkan.h"
#include "rhi_texture_vulkan.h"
#include "utils/physical_devices.h"

#include <ozz_rendering/rhi_device.h>

namespace OZZ::rendering::vk {
    constexpr uint32_t MaxFramesInFlight = 2;

    struct SubmissionContext {
        VkSemaphore AcquireImageSemaphore {VK_NULL_HANDLE};
        // VkSemaphore RenderCompleteSemaphore {VK_NULL_HANDLE};
        VkFence InFlightFence {VK_NULL_HANDLE};

        RHICommandBufferHandle CommandBuffer {};
    };

    class RHIDeviceVulkan : public RHIDevice {
    public:
        explicit RHIDeviceVulkan(const PlatformContext& context);
        ~RHIDeviceVulkan() override;

        // RHI Commands
        FrameContext BeginFrame() override;
        void SubmitAndPresentFrame(FrameContext) override;

        void BeginRenderPass(const RHICommandBufferHandle&, const RenderPassDescriptor&) override;
        void EndRenderPass(const RHICommandBufferHandle&) override;

        // Barriers
        void TextureResourceBarrier(const RHICommandBufferHandle&, const TextureBarrierDescriptor&) override;
        void BufferMemoryBarrier(const RHICommandBufferHandle&, const BufferBarrierDescriptor&) override;

        void SetViewport(const RHICommandBufferHandle&, const Viewport&) override;
        void SetScissor(const RHICommandBufferHandle&, const Scissor&) override;

        void SetGraphicsState(const RHICommandBufferHandle&, const GraphicsStateDescriptor&) override;

        void Draw(const RHICommandBufferHandle&,
                  uint32_t vertexCount,
                  uint32_t instanceCount,
                  uint32_t firstVertex,
                  uint32_t firstInstance) override;

        void DrawIndexed(const RHICommandBufferHandle&,
                         uint32_t indexCount,
                         uint32_t instanceCount,
                         uint32_t firstIndex,
                         int32_t vertexOffset,
                         uint32_t firstInstance) override;

        // Resource creation
        RHITextureHandle CreateTexture() override;

        RHIShaderHandle CreateShader(ShaderFileParams&& shaderFiles) override;
        RHIShaderHandle CreateShader(ShaderSourceParams&& shaderSources) override;
        void FreeShader(const RHIShaderHandle&) override;
        void BindShader(const RHICommandBufferHandle&, const RHIShaderHandle&) override;

        RHIBufferHandle CreateBuffer(BufferDescriptor&& bufferDescriptor) override;
        void UpdateBuffer(const RHIBufferHandle&, const void* data, size_t size, size_t offset) override;

        void BindBuffer(const RHICommandBufferHandle& commandBufferHandle, RHIBufferHandle& bufferHandle) override;
        void BindUniformBuffer(const RHICommandBufferHandle& commandBufferHandle,
                               const RHIBufferHandle& bufferHandle,
                               uint32_t set,
                               uint32_t binding) override;
        void SetPushConstants(const RHICommandBufferHandle& commandBufferHandle,
                              std::set<ShaderStage> stageFlags,
                              uint32_t offset,
                              uint32_t size,
                              const void* data) override;

    private:
        bool initialize();
        bool createInstance();
        bool createDebugCallback();
        bool createSurface();
        bool createDevice();
        bool createSwapchain();
        bool createCommandBufferPool();
        bool createSubmissionContexts();
        bool initializeQueue();

    private:
        PlatformContext platformContext;

        bool bIsValid {false};

        uint8_t framesInFlight {0};
        uint64_t currentFrame {0};

        /**
         * Vulkan Primitives
         */

        /**
         * CORE Vulkan objects
         */
        VkInstance instance {VK_NULL_HANDLE};
        VkDebugUtilsMessengerEXT debugMessenger {VK_NULL_HANDLE};
        VkSurfaceKHR surface {VK_NULL_HANDLE};
        RHIVulkanPhysicalDevices physicalDevices;
        VkDevice device {VK_NULL_HANDLE};
        VmaAllocator vmaAllocator {VK_NULL_HANDLE};
        VkSwapchainKHR swapchain {VK_NULL_HANDLE};
        VkCommandPool commandBufferPool {VK_NULL_HANDLE};
        VkQueue graphicsQueue {VK_NULL_HANDLE};

        /**
         * Swapchain Vulkan objects
         */
        VkSurfaceFormatKHR swapchainSurfaceFormat {};
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        std::vector<RHITextureHandle> swapchainTextureHandles;
        std::vector<VkSemaphore> presentCompleteSemaphores;

        /**
         * Sync objects
         */
        std::vector<SubmissionContext> submissionContexts;

        // resource pools
        ResourcePool<TextureTag, RHITextureVulkan> texturePool;
        ResourcePool<CommandBufferTag, VkCommandBuffer> commandBufferResourcePool;
        ResourcePool<ShaderTag, RHIShaderVulkan> shaderResourcePool;
        ResourcePool<BufferTag, RHIBufferVulkan> bufferResourcePool;
    };
} // namespace OZZ::rendering::vk
