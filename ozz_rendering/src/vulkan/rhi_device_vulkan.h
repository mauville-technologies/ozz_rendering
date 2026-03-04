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
        // We give them a frame context
        RHIFrameContext BeginFrame() override;
        // and then take it back
        void SubmitAndPresentFrame(RHIFrameContext&& frameContext) override;

        void BeginRenderPass(const RHIFrameContext& frameContext, const RenderPassDescriptor&) override;
        void EndRenderPass(const RHIFrameContext& frameContext) override;

        // Barriers
        void TextureResourceBarrier(const RHIFrameContext& frameContext, const TextureBarrierDescriptor&) override;
        void BufferMemoryBarrier(const RHIFrameContext& frameContext, const BufferBarrierDescriptor&) override;

        void SetViewport(const RHIFrameContext& frameContext, const Viewport&) override;
        void SetScissor(const RHIFrameContext& frameContext, const Scissor&) override;

        void SetGraphicsState(const RHIFrameContext& frameContext, const GraphicsStateDescriptor&) override;

        void BindShader(const RHIFrameContext&, const RHIShaderHandle&) override;

        void BindBuffer(const RHIFrameContext& frameContext, RHIBufferHandle& bufferHandle) override;
        void SetPushConstants(const RHIFrameContext& frameContext,
                              RHIPipelineLayoutHandle pipelineLayoutHandle,
                              ShaderStageFlags stageFlags,
                              uint32_t offset,
                              uint32_t size,
                              const void* data) override;

        // Descriptor sets
        RHIDescriptorSetHandle CreateDescriptorSet(RHIDescriptorSetLayoutHandle layoutHandle) override;
        void UpdateDescriptorSet(RHIDescriptorSetHandle handle,
                                 std::span<const RHIDescriptorWrite> writes) override;
        void BindDescriptorSet(const RHIFrameContext& frameContext,
                               RHIPipelineLayoutHandle pipelineLayoutHandle,
                               uint32_t setIndex,
                               RHIDescriptorSetHandle descriptorSetHandle) override;
        void FreeDescriptorSet(RHIDescriptorSetHandle handle) override;

        void Draw(const RHIFrameContext& frameContext,
                  uint32_t vertexCount,
                  uint32_t instanceCount,
                  uint32_t firstVertex,
                  uint32_t firstInstance) override;

        void DrawIndexed(const RHIFrameContext& frameContext,
                         uint32_t indexCount,
                         uint32_t instanceCount,
                         uint32_t firstIndex,
                         int32_t vertexOffset,
                         uint32_t firstInstance) override;

        // Resource creation
        RHITextureHandle CreateTexture() override;

        RHIShaderHandle CreateShader(ShaderFileParams&& shaderFiles) override;
        RHIShaderHandle CreateShader(ShaderSourceParams&& shaderSources) override;

        RHIPipelineLayoutDescriptor GetShaderPipelineLayout(const RHIShaderHandle& shaderHandle) override;
        std::pair<RHIPipelineLayoutHandle, std::set<RHIDescriptorSetLayoutHandle>>
        CreatePipelineLayout(const RHIPipelineLayoutDescriptor& pipelineLayoutDescriptor) override;
        RHIDescriptorSetLayoutHandle
        CreateDescriptorSetLayout(const RHIDescriptorSetLayoutDescriptor& descriptorSetLayoutDescriptor) override;

        void FreeShader(const RHIShaderHandle&) override;

        RHIBufferHandle CreateBuffer(BufferDescriptor&& bufferDescriptor) override;
        void UpdateBuffer(const RHIBufferHandle&, const void* data, size_t size, size_t offset) override;

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
        bool createDescriptorPool();

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
        ResourcePool<BufferTag, std::array<RHIBufferVulkan, MaxFramesInFlight>> bufferResourcePool;
        ResourcePool<PipelineLayoutTag, VkPipelineLayout> pipelineLayoutResourcePool;
        ResourcePool<DescriptorSetLayoutTag, VkDescriptorSetLayout> descriptorSetLayoutResourcePool;
        ResourcePool<DescriptorSetTag, VkDescriptorSet> descriptorSetResourcePool;

        VkDescriptorPool descriptorPool {VK_NULL_HANDLE};
    };
} // namespace OZZ::rendering::vk
