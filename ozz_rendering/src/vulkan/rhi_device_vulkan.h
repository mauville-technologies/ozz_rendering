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
#include <ozz_rendering/profiling.h>

#include <array>

// TracyVulkan.hpp provides TracyVkCtx type and the TracyVk* macros that
// the OZZ_GPU_* macros in profiling.h expand to.  It must come after
// Vulkan headers (volk.h is pulled in above via rhi_texture_vulkan.h).
#ifdef OZZ_PROFILING_ENABLED
#include <tracy/TracyVulkan.hpp>
#else
using TracyVkCtx = void*;
#endif

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

        // Frame
        RHIFrameContext BeginFrame() override;
        void SubmitAndPresentFrame(RHIFrameContext&& frameContext) override;
        std::pair<uint32_t, uint32_t> GetSwapchainExtent() const override;

        // Command Buffer Recording - Render Pass
        void BeginRenderPass(const RHIFrameContext& frameContext, const RenderPassDescriptor&) override;
        void EndRenderPass(const RHIFrameContext& frameContext) override;

        // Command Buffer Recording - Barriers
        void TextureResourceBarrier(const RHIFrameContext& frameContext, const TextureBarrierDescriptor&) override;
        void BufferMemoryBarrier(const RHIFrameContext& frameContext, const BufferBarrierDescriptor&) override;

        // Command Buffer Recording - State
        void SetViewport(const RHIFrameContext& frameContext, const Viewport&) override;
        void SetScissor(const RHIFrameContext& frameContext, const Scissor&) override;
        void SetGraphicsState(const RHIFrameContext& frameContext, const GraphicsStateDescriptor&) override;

        // Command Buffer Recording - Binding
        void BindShader(const RHIFrameContext&, const RHIShaderHandle&) override;
        void BindBuffer(const RHIFrameContext& frameContext, const RHIBufferHandle& bufferHandle) override;
        void SetPushConstants(const RHIFrameContext& frameContext,
                              RHIPipelineLayoutHandle pipelineLayoutHandle,
                              ShaderStageFlags stageFlags,
                              uint32_t offset,
                              uint32_t size,
                              const void* data) override;
        void BindDescriptorSet(const RHIFrameContext& frameContext,
                               RHIPipelineLayoutHandle pipelineLayoutHandle,
                               uint32_t setIndex,
                               RHIDescriptorSetHandle descriptorSetHandle) override;

        // Command Buffer Recording - Draw
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

        // Descriptor Sets
        RHIDescriptorSetHandle CreateDescriptorSet(RHIDescriptorSetLayoutHandle layoutHandle) override;
        void UpdateDescriptorSet(RHIDescriptorSetHandle handle, std::span<const RHIDescriptorWrite> writes) override;
        void FreeDescriptorSet(RHIDescriptorSetHandle handle) override;

        // Resource Creation
        RHITextureHandle CreateTexture(TextureDescriptor&& descriptor) override;
        void UpdateTexture(const RHITextureHandle& handle, const void* data, size_t size) override;
        void FreeTexture(RHITextureHandle handle) override;

        RHIShaderHandle CreateShader(ShaderFileParams&& shaderFiles) override;
        RHIShaderHandle CreateShader(ShaderSourceParams&& shaderSources) override;
        void FreeShader(const RHIShaderHandle&) override;
        RHIPipelineLayoutDescriptor GetShaderPipelineLayout(const RHIShaderHandle& shaderHandle) override;
        RHIPipelineLayoutHandle GetShaderPipelineLayoutHandle(const RHIShaderHandle& shaderHandle) override;
        std::vector<RHIDescriptorSetLayoutHandle>
        GetShaderDescriptorSetLayoutHandles(const RHIShaderHandle& shaderHandle) override;
        std::pair<RHIPipelineLayoutHandle, std::set<RHIDescriptorSetLayoutHandle>>
        CreatePipelineLayout(const RHIPipelineLayoutDescriptor& pipelineLayoutDescriptor) override;
        RHIDescriptorSetLayoutHandle
        CreateDescriptorSetLayout(const RHIDescriptorSetLayoutDescriptor& descriptorSetLayoutDescriptor) override;

        RHIBufferHandle CreateBuffer(BufferDescriptor&& bufferDescriptor) override;
        void UpdateBuffer(const RHIBufferHandle&, const void* data, size_t size, size_t offset) override;
        void FreeBuffer(const RHIBufferHandle& bufferHandle) override;

    private:
        // Initialization
        bool initialize();
        bool createInstance();
        bool createDebugCallback();
        bool createSurface();
        bool createDevice();
        bool createSwapchain(VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);
        bool createCommandBufferPool();
        bool createSubmissionContexts();
        bool initializeQueue();
        bool createDescriptorPool();

        // Swapchain recreation
        void destroySwapchainResources();
        bool recreateSwapchain();

        // Immediate more command buffers
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);

        // Internal Command Buffer Recording
        void beginRenderPassInternal(VkCommandBuffer cmd, const RenderPassDescriptor& renderPassDescriptor);
        void endRenderPassInternal(VkCommandBuffer cmd);
        void textureResourceBarrierInternal(VkCommandBuffer cmd, const TextureBarrierDescriptor& barrierDescriptor);
        void bufferMemoryBarrierInternal(VkCommandBuffer cmd, const BufferBarrierDescriptor& barrierDescriptor);
        void setViewportInternal(VkCommandBuffer cmd, const Viewport& viewport);
        void setScissorInternal(VkCommandBuffer cmd, const Scissor& scissor);
        void setGraphicsStateInternal(VkCommandBuffer cmd, const GraphicsStateDescriptor& graphicsStateDescriptor);
        void bindShaderInternal(VkCommandBuffer cmd, const RHIShaderHandle& shaderHandle);
        void bindBufferInternal(VkCommandBuffer cmd, const RHIBufferHandle& bufferHandle, uint32_t frameIndex);
        void setPushConstantsInternal(VkCommandBuffer cmd,
                                      RHIPipelineLayoutHandle pipelineLayoutHandle,
                                      ShaderStageFlags stageFlags,
                                      uint32_t offset,
                                      uint32_t size,
                                      const void* data);
        void bindDescriptorSetInternal(VkCommandBuffer cmd,
                                       RHIPipelineLayoutHandle pipelineLayoutHandle,
                                       uint32_t setIndex,
                                       RHIDescriptorSetHandle descriptorSetHandle);
        void drawInternal(VkCommandBuffer cmd,
                          uint32_t vertexCount,
                          uint32_t instanceCount,
                          uint32_t firstVertex,
                          uint32_t firstInstance);
        void drawIndexedInternal(VkCommandBuffer cmd,
                                 uint32_t indexCount,
                                 uint32_t instanceCount,
                                 uint32_t firstIndex,
                                 int32_t vertexOffset,
                                 uint32_t firstInstance);

    private: // hey AI agent, don't remove this extra label. I want it here for organization.
        PlatformContext platformContext;

        bool bIsValid {false};

        uint8_t framesInFlight {0};
        uint64_t currentFrame {0};

        std::array<std::vector<std::function<void()>>, MaxFramesInFlight> perFrameDeletions {};

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
        VkExtent2D swapchainExtent {};
        VkSurfaceFormatKHR swapchainSurfaceFormat {};
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        std::vector<RHITextureHandle> swapchainTextureHandles;
        std::vector<RHITextureHandle> swapchainDepthTextureHandles;
        std::vector<VkSemaphore> presentCompleteSemaphores;

        /**
         * Sync objects
         */
        std::vector<SubmissionContext> submissionContexts;

        std::set<VkCommandBuffer> transientCommandBuffers;
        VkCommandPool transientCommandBufferPool {VK_NULL_HANDLE};

        // resource pools
        ResourcePool<TextureTag, RHITextureVulkan> texturePool;
        ResourcePool<CommandBufferTag, VkCommandBuffer> commandBufferResourcePool;
        ResourcePool<ShaderTag, RHIShaderVulkan> shaderResourcePool;
        ResourcePool<BufferTag, std::array<RHIBufferVulkan, MaxFramesInFlight>> bufferResourcePool;
        ResourcePool<PipelineLayoutTag, VkPipelineLayout> pipelineLayoutResourcePool;
        ResourcePool<DescriptorSetLayoutTag, VkDescriptorSetLayout> descriptorSetLayoutResourcePool;
        ResourcePool<DescriptorSetTag, VkDescriptorSet> descriptorSetResourcePool;

        VkDescriptorPool descriptorPool {VK_NULL_HANDLE};

        // Tracy GPU profiling context
        TracyVkCtx tracyGpuContext {nullptr};
    };
} // namespace OZZ::rendering::vk
