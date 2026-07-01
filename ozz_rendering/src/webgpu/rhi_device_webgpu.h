#pragma once

#include <ozz_rendering/rhi_device.h>
#include <ozz_rendering/utils/resource_pool.h>

#include "rhi_buffer_webgpu.h"
#include "rhi_shader_webgpu.h"
#include "rhi_texture_webgpu.h"
#include "utils/pipeline_cache.h"
#include "utils/push_constants.h"

#include <slang.h>
#include <webgpu/webgpu.h>

#include <array>
#include <mutex>
#include <set>
#include <vector>

namespace OZZ::rendering::webgpu {

    // Internal storage for descriptor sets: bind group + its layout.
    struct DescriptorSetData {
        WGPUBindGroup bindGroup {nullptr};
        RHIDescriptorSetLayoutHandle layoutHandle {};
    };

    class RHIDeviceWebGPU : public RHIDevice {
    public:
        explicit RHIDeviceWebGPU(const PlatformContext& context);
        ~RHIDeviceWebGPU() override;

        // Frame
        RHIFrameContext BeginFrame() override;
        void SubmitAndPresentFrame(RHIFrameContext&& frameContext) override;
        std::pair<uint32_t, uint32_t> GetSwapchainExtent() const override;

        // Render Pass
        void BeginRenderPass(const RHIFrameContext& frameContext,
                             const RenderPassDescriptor& renderPassDescriptor) override;
        void EndRenderPass(const RHIFrameContext& frameContext) override;

        // Barriers — implicit in WebGPU; these are no-ops
        void TextureResourceBarrier(const RHIFrameContext& frameContext,
                                    const TextureBarrierDescriptor& textureBarrierDescriptor) override;
        void BufferMemoryBarrier(const RHIFrameContext& frameContext,
                                 const BufferBarrierDescriptor& bufferBarrierDescriptor) override;

        // State
        void SetViewport(const RHIFrameContext& frameContext, const Viewport& viewport) override;
        void SetScissor(const RHIFrameContext& frameContext, const Scissor& scissor) override;
        void SetGraphicsState(const RHIFrameContext& frameContext,
                              const GraphicsStateDescriptor& graphicsStateDescriptor) override;

        // Binding
        void BindShader(const RHIFrameContext& frameContext, const RHIShaderHandle& shaderHandle) override;
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

        // Draw
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
        void UpdateDescriptorSet(RHIDescriptorSetHandle handle,
                                 std::span<const RHIDescriptorWrite> writes) override;
        void FreeDescriptorSet(RHIDescriptorSetHandle handle) override;

        // Textures
        RHITextureHandle CreateTexture(TextureDescriptor&& descriptor) override;
        void UpdateTexture(const RHITextureHandle& handle, const void* data, size_t size) override;
        void FreeTexture(RHITextureHandle handle) override;

        // Shaders
        RHIShaderHandle CreateShader(ShaderFileParams&& fileParams) override;
        RHIShaderHandle CreateShader(ShaderSourceParams&& sourceParams) override;
        void FreeShader(const RHIShaderHandle& shaderHandle) override;
        RHIPipelineLayoutDescriptor GetShaderPipelineLayout(const RHIShaderHandle& shaderHandle) override;
        RHIPipelineLayoutHandle GetShaderPipelineLayoutHandle(const RHIShaderHandle& shaderHandle) override;
        std::vector<RHIDescriptorSetLayoutHandle>
        GetShaderDescriptorSetLayoutHandles(const RHIShaderHandle& shaderHandle) override;
        std::pair<RHIPipelineLayoutHandle, std::set<RHIDescriptorSetLayoutHandle>>
        CreatePipelineLayout(const RHIPipelineLayoutDescriptor& pipelineLayoutDescriptor) override;
        RHIDescriptorSetLayoutHandle
        CreateDescriptorSetLayout(const RHIDescriptorSetLayoutDescriptor& descriptorSetLayoutDescriptor) override;

        // Buffers
        RHIBufferHandle CreateBuffer(BufferDescriptor&& bufferDescriptor) override;
        void UpdateBuffer(const RHIBufferHandle& handle, const void* data, size_t size, size_t offset) override;
        void FreeBuffer(const RHIBufferHandle& handle) override;

    private:
        void initialize();
        void configureSurface();
        void createDepthTexture();
        void registerShaderLayouts(RHIShaderHandle handle, RHIShaderWebGPU& shader);
        WGPURenderPipeline buildPipeline(const PipelineKey& key, const RHIShaderWebGPU& shader);
        // Applies all pending draw state (pipeline, vertex buffer, descriptor sets,
        // push-constant bind group) to the active render pass encoder. Returns false
        // if the draw must be skipped (no active pass, no shader, pipeline build failed).
        bool flushPendingDrawState();

    private:
        // Dawn (native/Vulkan backend) is not safe to call concurrently from multiple
        // threads on the same device/queue. The contract of this device is that resource
        // creation APIs (CreateBuffer/UpdateBuffer/CreateTexture/etc.) may be called from
        // a non-render thread while a frame is being recorded on the render thread —
        // without serializing access, this races inside Dawn's internal Vulkan backend
        // and segfaults. Every public entry point below takes this lock for its duration.
        mutable std::recursive_mutex apiMutex;

        PlatformContext platformContext;

        WGPUInstance instance {nullptr};
        WGPUAdapter  adapter  {nullptr};
        WGPUDevice   device   {nullptr};
        WGPUQueue    queue    {nullptr};
        WGPUSurface  surface  {nullptr};

        // Slang (runtime shader compiler for WebGPU backend)
        slang::IGlobalSession* slangSession {nullptr};

        WGPUTextureFormat swapchainFormat {WGPUTextureFormat_BGRA8Unorm};
        WGPUTextureFormat depthFormat     {WGPUTextureFormat_Depth32Float};
        uint32_t          swapchainWidth  {0};
        uint32_t          swapchainHeight {0};

        static constexpr uint32_t MaxFramesInFlight {2};
        uint32_t currentFrameIndex {0};

        // Active frame state (reset each frame)
        WGPUCommandEncoder    activeEncoder          {nullptr};
        WGPURenderPassEncoder activeRenderPassEncoder {nullptr};
        WGPUTextureView       currentBackbufferView  {nullptr};
        RHITextureHandle      depthTextureHandle {};

        // Formats of the currently-active render pass — updated in BeginRenderPass,
        // used to build the correct pipeline key for Draw / DrawIndexed.
        WGPUTextureFormat activePassColorFormat {WGPUTextureFormat_Undefined};
        WGPUTextureFormat activePassDepthFormat {WGPUTextureFormat_Undefined};

        // Pending per-draw state (updated by Set* / Bind* before Draw)
        GraphicsStateDescriptor pendingState {};
        RHIShaderHandle         pendingShaderHandle {};
        RHIBufferHandle         pendingVertexBuffer {};
        RHIBufferHandle         pendingIndexBuffer {};
        bool                    hasPendingIndexBuffer {false};
        // True once SetGraphicsState has been called in the current render pass;
        // reset in BeginRenderPass. Guards against draws inheriting stale state.
        bool                    stateSetThisPass {false};
        std::array<RHIDescriptorSetHandle, MaxBoundDescriptorSets> pendingDescriptorSets {};

        // Push constants emulated via a dynamic-offset uniform buffer at
        // set=PushConstantSet, binding=PushConstantBinding (see utils/push_constants.h).
        // wgpuQueueWriteBuffer takes effect immediately (queue-timeline), while draws are
        // recorded into activeEncoder and only submitted at end-of-frame — so a single
        // shared 256-byte buffer would let every draw in the frame see only the LAST
        // object's data by the time the GPU actually executes. Each SetPushConstants call
        // instead writes to its own never-reused slot, and the matching draw binds that
        // slot via a dynamic offset.
        static constexpr uint32_t PushConstantSlotsPerFrame = 4096;
        WGPUBuffer          pushConstantBuffer {nullptr};
        WGPUBindGroupLayout pushConstantBGL    {nullptr};
        WGPUBindGroup       pushConstantBG     {nullptr};
        // Empty BGL/BG used to satisfy gap slots in pipeline layouts with push constants
        WGPUBindGroupLayout emptyBGL           {nullptr};
        WGPUBindGroup       emptyBG            {nullptr};
        uint32_t            pushConstantCursor       {0}; // slot index within current frame's region; reset each BeginFrame
        uint32_t            pendingPushConstantOffset {0}; // byte offset of the most recently written slot

        // Resource pools
        ResourcePool<TextureTag,             RHITextureWebGPU>    texturePool;
        ResourcePool<CommandBufferTag,       uint32_t>            commandBufferPool;
        ResourcePool<ShaderTag,              RHIShaderWebGPU>     shaderPool;
        ResourcePool<BufferTag,              RHIBufferWebGPU>     bufferPool;
        ResourcePool<PipelineLayoutTag,      WGPUPipelineLayout>  pipelineLayoutPool;
        ResourcePool<DescriptorSetLayoutTag, WGPUBindGroupLayout> bindGroupLayoutPool;
        ResourcePool<DescriptorSetTag,       DescriptorSetData>   descriptorSetPool;

        // Pre-allocated command buffer handles indexed by frame index
        std::array<RHICommandBufferHandle, MaxFramesInFlight> frameCommandBuffers {};

        PipelineCache pipelineCache;
    };

} // namespace OZZ::rendering::webgpu
