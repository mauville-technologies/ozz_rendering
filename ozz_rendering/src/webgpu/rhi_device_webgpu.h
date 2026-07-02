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

    // Internal storage for a bind-group layout. Retains the source descriptor and a
    // per-binding "depth resolved" flag so UpdateDescriptorSet can lazily rebuild the
    // WGPUBindGroupLayout in place once it learns a SampledImage binding actually samples
    // a depth-format texture (WebGPU needs UnfilterableFloat + NonFiltering there).
    struct BindGroupLayoutData {
        WGPUBindGroupLayout bgl {nullptr};
        RHIDescriptorSetLayoutDescriptor sourceDesc {};
        std::array<bool, MaxBoundDescriptorSets> depthResolved {};
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

        // Unlocked implementations. Public methods take apiMutex (a plain std::mutex) and
        // delegate here; internal callers that already hold the lock (or run during
        // single-threaded construction) call the *Impl form directly. This avoids
        // re-locking a non-recursive mutex on public-to-public call chains, e.g.
        // BeginFrame -> Free/CreateTexture, and CreateShader -> registerShaderLayouts ->
        // CreatePipelineLayout -> CreateDescriptorSetLayout.
        RHITextureHandle createTextureImpl(TextureDescriptor&& descriptor);
        void freeTextureImpl(RHITextureHandle handle);
        std::pair<RHIPipelineLayoutHandle, std::set<RHIDescriptorSetLayoutHandle>>
        createPipelineLayoutImpl(const RHIPipelineLayoutDescriptor& pipelineLayoutDescriptor);
        RHIDescriptorSetLayoutHandle
        createDescriptorSetLayoutImpl(const RHIDescriptorSetLayoutDescriptor& descriptorSetLayoutDescriptor);
        // Builds a WGPUPipelineLayout from a set of already-created descriptor-set-layout
        // handles (in set-index order) plus the reflected descriptor (for push constants).
        // Shared by CreatePipelineLayout and the in-place rebuild after a BGL is mutated.
        WGPUPipelineLayout buildPipelineLayoutFromHandles(
            const std::vector<RHIDescriptorSetLayoutHandle>& dslHandles,
            const RHIPipelineLayoutDescriptor& desc);
        // After a bind-group layout is rebuilt in place (same pool slot, new WGPU handle),
        // rebuild every shader's WGPUPipelineLayout that references the mutated DSL handle,
        // reusing the shader's existing pipelineLayout pool slot.
        void rebuildPipelineLayoutsUsingDSL(RHIDescriptorSetLayoutHandle mutatedHandle);
        // Builds the raw WGPUBindGroupLayout for a descriptor. `depthResolved[binding]`
        // forces a SampledImage binding to be declared UnfilterableFloat with a paired
        // NonFiltering sampler at binding+1 (WebGPU's depth-texture requirement).
        WGPUBindGroupLayout buildBindGroupLayoutObject(
            const RHIDescriptorSetLayoutDescriptor& desc,
            const std::array<bool, MaxBoundDescriptorSets>& depthResolved);
        WGPURenderPipeline buildPipeline(const PipelineKey& key, const RHIShaderWebGPU& shader);
        // Applies all pending draw state (pipeline, vertex buffer, descriptor sets,
        // push-constant bind group) to the active render pass encoder. Returns false
        // if the draw must be skipped (no active pass, no shader, pipeline build failed).
        bool flushPendingDrawState();

    private:
        // Dawn device calls are not thread-safe on the same device/queue, so this backend
        // serializes EVERYTHING through this single mutex: every public entry point takes
        // it for its full duration. The RHIDevice thread-safety contract (resource
        // creation/update/free may run concurrently with frame recording) is therefore
        // satisfied here by mutual exclusion rather than by real parallelism — unlike the
        // Vulkan backend, which uses fine-grained locks and genuinely creates resources in
        // parallel. This is a non-recursive std::mutex: no locked path may lock again, so
        // all public-to-public call chains route through the unlocked *Impl helpers.
        mutable std::mutex apiMutex;

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

        // Extent of the currently-active render pass attachments. Viewport and
        // scissor are clamped to this: during a window-resize storm the engine can
        // submit a rect sized for the previous frame, and an oversized rect is a
        // Dawn validation error that invalidates the whole command buffer.
        uint32_t activePassWidth  {0};
        uint32_t activePassHeight {0};

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
        ResourcePool<DescriptorSetLayoutTag, BindGroupLayoutData> bindGroupLayoutPool;
        ResourcePool<DescriptorSetTag,       DescriptorSetData>   descriptorSetPool;

        // Pre-allocated command buffer handles indexed by frame index
        std::array<RHICommandBufferHandle, MaxFramesInFlight> frameCommandBuffers {};

        PipelineCache pipelineCache;
    };

} // namespace OZZ::rendering::webgpu
