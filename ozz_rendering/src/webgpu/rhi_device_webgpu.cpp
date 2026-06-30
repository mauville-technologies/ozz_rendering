#include "rhi_device_webgpu.h"

#include "utils/rhi_webgpu_types.h"

#include <ozz_rendering/rhi_renderpass.h>

#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>
#include <slang.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace OZZ::rendering::webgpu {

    // -------------------------------------------------------------------------
    // Constructor / destructor
    // -------------------------------------------------------------------------

    RHIDeviceWebGPU::RHIDeviceWebGPU(const PlatformContext& context)
        : RHIDevice(context)
        , platformContext(context)
        , texturePool([this](RHITextureWebGPU& t) {
            if (!t.IsSwapchainImage && t.Texture) wgpuTextureRelease(t.Texture);
            if (t.TextureView) wgpuTextureViewRelease(t.TextureView);
            if (t.Sampler) wgpuSamplerRelease(t.Sampler);
        })
        , commandBufferPool([](uint32_t&) {})
        , shaderPool([](RHIShaderWebGPU& s) { s.Destroy(); })
        , bufferPool([](RHIBufferWebGPU& b) {
            if (b.Buffer) wgpuBufferRelease(b.Buffer);
        })
        , pipelineLayoutPool([](WGPUPipelineLayout& pl) {
            if (pl) wgpuPipelineLayoutRelease(pl);
        })
        , bindGroupLayoutPool([](WGPUBindGroupLayout& bgl) {
            if (bgl) wgpuBindGroupLayoutRelease(bgl);
        })
        , descriptorSetPool([](DescriptorSetData& ds) {
            if (ds.bindGroup) wgpuBindGroupRelease(ds.bindGroup);
        })
    {
        initialize();
    }

    RHIDeviceWebGPU::~RHIDeviceWebGPU() {
        pipelineCache.Clear();

        texturePool.Empty();
        commandBufferPool.Empty();
        shaderPool.Empty();
        bufferPool.Empty();
        pipelineLayoutPool.Empty();
        bindGroupLayoutPool.Empty();
        descriptorSetPool.Empty();

        if (activeRenderPassEncoder) wgpuRenderPassEncoderRelease(activeRenderPassEncoder);
        if (activeEncoder)           wgpuCommandEncoderRelease(activeEncoder);
        if (currentBackbufferView)   wgpuTextureViewRelease(currentBackbufferView);

        if (pushConstantBG)     { wgpuBindGroupRelease(pushConstantBG);          pushConstantBG     = nullptr; }
        if (pushConstantBGL)    { wgpuBindGroupLayoutRelease(pushConstantBGL);   pushConstantBGL    = nullptr; }
        if (pushConstantBuffer) { wgpuBufferRelease(pushConstantBuffer);         pushConstantBuffer = nullptr; }

        if (surface)   wgpuSurfaceRelease(surface);
        if (device)    wgpuDeviceRelease(device);
        if (adapter)   wgpuAdapterRelease(adapter);
        if (instance)  wgpuInstanceRelease(instance);
        if (slangSession) slangSession->release();
    }

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------

    void RHIDeviceWebGPU::initialize() {
        // Register Dawn native procs (required for static/non-monolithic Dawn linking)
        dawnProcSetProcs(&dawn::native::GetProcs());

        // Slang global session (one per process is typical, but per-device is fine)
        if (SLANG_FAILED(slang_createGlobalSession(SLANG_API_VERSION, &slangSession)) || !slangSession)
            throw std::runtime_error("Failed to create Slang session");

        // Dawn instance
        WGPUInstanceDescriptor instanceDesc = {};
        instance = wgpuCreateInstance(&instanceDesc);
        if (!instance) throw std::runtime_error("Failed to create WebGPU instance");

        // Surface — delegate to PlatformContext (stays backend-agnostic for Emscripten compatibility)
        if (!platformContext.CreateSurfaceFunction(&instance, &surface) || !surface)
            throw std::runtime_error("Failed to create WebGPU surface");

        // Adapter (synchronous in Dawn's native backend)
        WGPURequestAdapterOptions adapterOpts = {};
        adapterOpts.compatibleSurface = surface;
        adapterOpts.powerPreference   = WGPUPowerPreference_HighPerformance;
        wgpuInstanceRequestAdapter(
            instance, &adapterOpts,
            [](WGPURequestAdapterStatus status, WGPUAdapter a, char const*, void* ud) {
                if (status == WGPURequestAdapterStatus_Success)
                    *static_cast<WGPUAdapter*>(ud) = a;
            },
            &adapter);
        if (!adapter) throw std::runtime_error("No suitable WebGPU adapter found");

        // Device
        WGPUDeviceDescriptor deviceDesc = {};
        deviceDesc.label = "ozz_rendering_webgpu";
        wgpuAdapterRequestDevice(
            adapter, &deviceDesc,
            [](WGPURequestDeviceStatus status, WGPUDevice d, char const*, void* ud) {
                if (status == WGPURequestDeviceStatus_Success)
                    *static_cast<WGPUDevice*>(ud) = d;
            },
            &device);
        if (!device) throw std::runtime_error("Failed to create WebGPU device");

        wgpuDeviceSetUncapturedErrorCallback(
            device,
            [](WGPUErrorType type, char const* message, void*) {
                spdlog::error("WebGPU error ({}): {}", static_cast<int>(type),
                              message ? message : "");
            },
            nullptr);

        queue = wgpuDeviceGetQueue(device);

        // Surface format
        WGPUSurfaceCapabilities caps = {};
        wgpuSurfaceGetCapabilities(surface, adapter, &caps);
        swapchainFormat = (caps.formatCount > 0) ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
        wgpuSurfaceCapabilitiesFreeMembers(caps);

        // Initial size
        auto [w, h] = platformContext.GetWindowFramebufferSizeFunction();
        swapchainWidth  = static_cast<uint32_t>(w);
        swapchainHeight = static_cast<uint32_t>(h);

        configureSurface();
        createDepthTexture();

        // Pre-allocate per-frame command buffer handles (used as sentinel IDs)
        for (uint32_t i = 0; i < MaxFramesInFlight; i++) {
            frameCommandBuffers[i] = commandBufferPool.Allocate(std::move(i));
        }

        // Push constant emulation: 256-byte uniform buffer at set=PushConstantSet, binding=0
        {
            WGPUBufferDescriptor pcBufDesc {};
            pcBufDesc.size  = 256;
            pcBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            pushConstantBuffer = wgpuDeviceCreateBuffer(device, &pcBufDesc);

            WGPUBindGroupLayoutEntry pcEntry {};
            pcEntry.binding               = PushConstantBinding;
            pcEntry.visibility            = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            pcEntry.buffer.type           = WGPUBufferBindingType_Uniform;
            pcEntry.buffer.minBindingSize = 0;
            WGPUBindGroupLayoutDescriptor pcBGLDesc {};
            pcBGLDesc.entryCount = 1;
            pcBGLDesc.entries    = &pcEntry;
            pushConstantBGL = wgpuDeviceCreateBindGroupLayout(device, &pcBGLDesc);

            WGPUBindGroupEntry pcBGEntry {};
            pcBGEntry.binding = PushConstantBinding;
            pcBGEntry.buffer  = pushConstantBuffer;
            pcBGEntry.offset  = 0;
            pcBGEntry.size    = 256;
            WGPUBindGroupDescriptor pcBGDesc {};
            pcBGDesc.layout     = pushConstantBGL;
            pcBGDesc.entryCount = 1;
            pcBGDesc.entries    = &pcBGEntry;
            pushConstantBG = wgpuDeviceCreateBindGroup(device, &pcBGDesc);
        }
    }

    void RHIDeviceWebGPU::configureSurface() {
        WGPUSurfaceConfiguration config = {};
        config.device      = device;
        config.format      = swapchainFormat;
        config.usage       = WGPUTextureUsage_RenderAttachment;
        config.alphaMode   = WGPUCompositeAlphaMode_Auto;
        config.width       = swapchainWidth;
        config.height      = swapchainHeight;
        config.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface, &config);
    }

    void RHIDeviceWebGPU::createDepthTexture() {
        TextureDescriptor depthDesc {};
        depthDesc.Width  = swapchainWidth;
        depthDesc.Height = swapchainHeight;
        depthDesc.Format = TextureFormat::D32Float;
        depthDesc.Usage  = TextureUsage::DepthAttachment;
        depthTextureHandle = CreateTexture(std::move(depthDesc));
    }

    // -------------------------------------------------------------------------
    // Frame management
    // -------------------------------------------------------------------------

    RHIFrameContext RHIDeviceWebGPU::BeginFrame() {
        auto [w, h] = platformContext.GetWindowFramebufferSizeFunction();
        uint32_t newW = static_cast<uint32_t>(w);
        uint32_t newH = static_cast<uint32_t>(h);
        if (newW != swapchainWidth || newH != swapchainHeight) {
            swapchainWidth  = newW;
            swapchainHeight = newH;
            configureSurface();
            FreeTexture(depthTextureHandle);
            createDepthTexture();
        }

        // Acquire current surface texture
        WGPUSurfaceTexture surfaceTex = {};
        wgpuSurfaceGetCurrentTexture(surface, &surfaceTex);
        if (surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
            return RHIFrameContext::Null();
        }

        // Wrap in a texture handle (swapchain image — view only, not owned)
        RHITextureWebGPU swapTex {};
        swapTex.IsSwapchainImage = true;
        swapTex.Texture   = surfaceTex.texture; // not owned
        swapTex.TextureView = wgpuTextureCreateView(surfaceTex.texture, nullptr);
        swapTex.Width     = swapchainWidth;
        swapTex.Height    = swapchainHeight;
        RHITextureHandle colorHandle = texturePool.Allocate(std::move(swapTex));
        currentBackbufferView = texturePool.Get(colorHandle)->TextureView;

        // Command encoder
        WGPUCommandEncoderDescriptor encDesc = {};
        activeEncoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

        const RHICommandBufferHandle cmdHandle = frameCommandBuffers[currentFrameIndex];

        return BuildFrameContext(cmdHandle, colorHandle, depthTextureHandle,
                                 currentFrameIndex, currentFrameIndex);
    }

    void RHIDeviceWebGPU::SubmitAndPresentFrame(RHIFrameContext&& frameContext) {
        // End any open render pass
        if (activeRenderPassEncoder) {
            wgpuRenderPassEncoderEnd(activeRenderPassEncoder);
            wgpuRenderPassEncoderRelease(activeRenderPassEncoder);
            activeRenderPassEncoder = nullptr;
        }

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(activeEncoder, &cmdDesc);
        wgpuQueueSubmit(queue, 1, &commands);
        wgpuCommandBufferRelease(commands);

        wgpuCommandEncoderRelease(activeEncoder);
        activeEncoder = nullptr;

        wgpuSurfacePresent(surface);

        // Release the swapchain texture handle allocated in BeginFrame
        RHITextureHandle colorHandle = frameContext.GetBackbufferImage();
        if (currentBackbufferView) {
            wgpuTextureViewRelease(currentBackbufferView);
            currentBackbufferView = nullptr;
        }
        // Release the swapchain texture object (not the view — we already released above)
        auto* swapTex = texturePool.Get(colorHandle);
        if (swapTex) {
            swapTex->TextureView = nullptr; // already released above
        }
        texturePool.Free(colorHandle);

        currentFrameIndex = (currentFrameIndex + 1) % MaxFramesInFlight;

        // Clear pending draw state
        pendingShaderHandle = RHIShaderHandle::Null();
        pendingVertexBuffer = RHIBufferHandle::Null();
        pendingIndexBuffer  = RHIBufferHandle::Null();
        hasPendingIndexBuffer = false;
        pendingDescriptorSets.fill(RHIDescriptorSetHandle::Null());
    }

    std::pair<uint32_t, uint32_t> RHIDeviceWebGPU::GetSwapchainExtent() const {
        return {swapchainWidth, swapchainHeight};
    }

    // -------------------------------------------------------------------------
    // Render Pass
    // -------------------------------------------------------------------------

    void RHIDeviceWebGPU::BeginRenderPass(const RHIFrameContext& frameContext,
                                           const RenderPassDescriptor& rpDesc) {
        if (!activeEncoder) return;

        // Reset active pass formats; they will be set below from the actual attachments.
        activePassColorFormat = WGPUTextureFormat_Undefined;
        activePassDepthFormat = WGPUTextureFormat_Undefined;

        std::vector<WGPURenderPassColorAttachment> colorAttachments;
        colorAttachments.reserve(rpDesc.ColorAttachmentCount);

        for (uint32_t i = 0; i < rpDesc.ColorAttachmentCount; i++) {
            const auto& att = rpDesc.ColorAttachments[i];
            auto* tex = texturePool.Get(att.Texture);
            if (!tex) continue;

            // Capture the first color attachment's format for pipeline key building.
            if (i == 0) activePassColorFormat = wgpuTextureGetFormat(tex->Texture);

            WGPURenderPassColorAttachment ca = {};
            ca.view       = tex->TextureView;
            ca.loadOp     = ToWebGPU(att.Load);
            ca.storeOp    = ToWebGPU(att.Store);
            ca.clearValue = {att.Clear.R, att.Clear.G, att.Clear.B, att.Clear.A};
            ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachments.push_back(ca);
        }

        WGPURenderPassDepthStencilAttachment depthAtt = {};
        WGPURenderPassDepthStencilAttachment* depthAttPtr = nullptr;
        if (rpDesc.DepthAttachment.Texture.IsValid()) {
            auto* dTex = texturePool.Get(rpDesc.DepthAttachment.Texture);
            if (dTex) {
                depthAtt.view            = dTex->TextureView;
                depthAtt.depthLoadOp     = ToWebGPU(rpDesc.DepthAttachment.Load);
                depthAtt.depthStoreOp    = ToWebGPU(rpDesc.DepthAttachment.Store);
                depthAtt.depthClearValue = rpDesc.DepthAttachment.Clear.Depth;
                // For combined depth+stencil formats (D24S8) the view includes the stencil
                // aspect, so WebGPU requires either explicit stencil ops or stencilReadOnly=true.
                // We never use stencil, so mark it read-only to avoid specifying ops.
                {
                    WGPUTextureFormat depFmt = wgpuTextureGetFormat(dTex->Texture);
                    activePassDepthFormat = depFmt;
                    bool hasStencil = (depFmt == WGPUTextureFormat_Depth24PlusStencil8 ||
                                       depFmt == WGPUTextureFormat_Depth32FloatStencil8 ||
                                       depFmt == WGPUTextureFormat_Stencil8);
                    depthAtt.stencilLoadOp   = WGPULoadOp_Undefined;
                    depthAtt.stencilStoreOp  = WGPUStoreOp_Undefined;
                    depthAtt.stencilReadOnly = hasStencil;
                }
                depthAttPtr = &depthAtt;
            }
        }

        WGPURenderPassDescriptor wgpuRpDesc = {};
        wgpuRpDesc.colorAttachmentCount  = colorAttachments.size();
        wgpuRpDesc.colorAttachments      = colorAttachments.data();
        wgpuRpDesc.depthStencilAttachment = depthAttPtr;

        activeRenderPassEncoder = wgpuCommandEncoderBeginRenderPass(activeEncoder, &wgpuRpDesc);
    }

    void RHIDeviceWebGPU::EndRenderPass(const RHIFrameContext&) {
        if (activeRenderPassEncoder) {
            wgpuRenderPassEncoderEnd(activeRenderPassEncoder);
            wgpuRenderPassEncoderRelease(activeRenderPassEncoder);
            activeRenderPassEncoder = nullptr;
        }
    }

    // -------------------------------------------------------------------------
    // Barriers — no-ops in WebGPU (implicit synchronization)
    // -------------------------------------------------------------------------

    void RHIDeviceWebGPU::TextureResourceBarrier(const RHIFrameContext&,
                                                  const TextureBarrierDescriptor&) {}

    void RHIDeviceWebGPU::BufferMemoryBarrier(const RHIFrameContext&,
                                               const BufferBarrierDescriptor&) {}

    // -------------------------------------------------------------------------
    // State recording
    // -------------------------------------------------------------------------

    void RHIDeviceWebGPU::SetViewport(const RHIFrameContext&, const Viewport& vp) {
        if (!activeRenderPassEncoder) return;
        wgpuRenderPassEncoderSetViewport(activeRenderPassEncoder,
                                          vp.X, vp.Y, vp.Width, vp.Height,
                                          vp.MinDepth, vp.MaxDepth);
    }

    void RHIDeviceWebGPU::SetScissor(const RHIFrameContext&, const Scissor& sc) {
        if (!activeRenderPassEncoder) return;
        wgpuRenderPassEncoderSetScissorRect(activeRenderPassEncoder,
                                             static_cast<uint32_t>(sc.X),
                                             static_cast<uint32_t>(sc.Y),
                                             sc.Width, sc.Height);
    }

    void RHIDeviceWebGPU::SetGraphicsState(const RHIFrameContext&,
                                            const GraphicsStateDescriptor& state) {
        pendingState = state;
    }

    void RHIDeviceWebGPU::BindShader(const RHIFrameContext&, const RHIShaderHandle& handle) {
        pendingShaderHandle = handle;
    }

    void RHIDeviceWebGPU::BindBuffer(const RHIFrameContext&, const RHIBufferHandle& handle) {
        auto* buf = bufferPool.Get(handle);
        if (!buf) return;
        if (static_cast<uint8_t>(buf->Usage) & static_cast<uint8_t>(BufferUsage::VertexBuffer))
            pendingVertexBuffer = handle;
        if (static_cast<uint8_t>(buf->Usage) & static_cast<uint8_t>(BufferUsage::IndexBuffer)) {
            pendingIndexBuffer    = handle;
            hasPendingIndexBuffer = true;
        }
    }

    void RHIDeviceWebGPU::SetPushConstants(const RHIFrameContext&,
                                            RHIPipelineLayoutHandle,
                                            ShaderStageFlags,
                                            uint32_t offset,
                                            uint32_t size,
                                            const void* data) {
        if (!data || size == 0 || offset + size > 256) return;
        std::memcpy(pendingPushConstantData + offset, data, size);
        pendingPushConstantDirty = true;
    }

    void RHIDeviceWebGPU::BindDescriptorSet(const RHIFrameContext&,
                                             RHIPipelineLayoutHandle,
                                             uint32_t setIndex,
                                             RHIDescriptorSetHandle descriptorSetHandle) {
        if (setIndex < MaxBoundDescriptorSets)
            pendingDescriptorSets[setIndex] = descriptorSetHandle;
    }

    // -------------------------------------------------------------------------
    // Pipeline building
    // -------------------------------------------------------------------------

    WGPURenderPipeline RHIDeviceWebGPU::buildPipeline(const PipelineKey& key,
                                                        const RHIShaderWebGPU& shader) {
        const auto& state = key.state;

        // Collect vertex attributes, sorted by binding
        std::vector<WGPUVertexAttribute> sortedAttribs;
        std::vector<uint32_t>            attribStart;
        attribStart.reserve(state.VertexInput.BindingCount);

        for (uint32_t b = 0; b < state.VertexInput.BindingCount; b++) {
            attribStart.push_back(static_cast<uint32_t>(sortedAttribs.size()));
            for (uint32_t a = 0; a < state.VertexInput.AttributeCount; a++) {
                if (state.VertexInput.Attributes[a].Binding != b) continue;
                WGPUVertexAttribute attr {};
                attr.shaderLocation = state.VertexInput.Attributes[a].Location;
                attr.format         = ToWebGPU(state.VertexInput.Attributes[a].Format);
                attr.offset         = state.VertexInput.Attributes[a].Offset;
                sortedAttribs.push_back(attr);
            }
        }

        std::vector<WGPUVertexBufferLayout> bufLayouts;
        bufLayouts.reserve(state.VertexInput.BindingCount);
        for (uint32_t b = 0; b < state.VertexInput.BindingCount; b++) {
            uint32_t count = (b + 1 < state.VertexInput.BindingCount)
                             ? attribStart[b + 1] - attribStart[b]
                             : static_cast<uint32_t>(sortedAttribs.size()) - attribStart[b];
            WGPUVertexBufferLayout layout {};
            layout.arrayStride   = state.VertexInput.Bindings[b].Stride;
            layout.stepMode      = ToWebGPU(state.VertexInput.Bindings[b].InputRate);
            layout.attributeCount = count;
            layout.attributes    = count ? sortedAttribs.data() + attribStart[b] : nullptr;
            bufLayouts.push_back(layout);
        }

        // Blend state for first color attachment
        WGPUBlendState          blendState {};
        WGPUColorTargetState    colorTarget {};
        colorTarget.format    = key.colorFormat;
        colorTarget.writeMask = WGPUColorWriteMask_All;
        if (state.ColorBlendAttachmentCount > 0) {
            const auto& b = state.ColorBlend[0];
            colorTarget.writeMask = ToWebGPU(b.ColorWriteMask);
            if (b.BlendEnable) {
                blendState.color = {ToWebGPU(b.ColorBlendOp), ToWebGPU(b.SrcColorFactor), ToWebGPU(b.DstColorFactor)};
                blendState.alpha = {ToWebGPU(b.AlphaBlendOp), ToWebGPU(b.SrcAlphaFactor), ToWebGPU(b.DstAlphaFactor)};
                colorTarget.blend = &blendState;
            }
        }

        WGPUFragmentState fragState {};
        fragState.module      = shader.fragmentModule;
        fragState.entryPoint = shader.fragmentEntryPoint.c_str();
        fragState.targetCount = 1;
        fragState.targets     = &colorTarget;

        // Depth / stencil
        auto formatHasStencil = [](WGPUTextureFormat fmt) {
            return fmt == WGPUTextureFormat_Depth24PlusStencil8
                || fmt == WGPUTextureFormat_Depth32FloatStencil8;
        };
        WGPUDepthStencilState  depthStencil {};
        WGPUDepthStencilState* depthStencilPtr = nullptr;
        if (key.depthFormat != WGPUTextureFormat_Undefined) {
            depthStencil.format            = key.depthFormat;
            depthStencil.depthWriteEnabled = state.DepthStencil.DepthWriteEnable
                                           ? WGPUOptionalBool_True : WGPUOptionalBool_False;
            depthStencil.depthCompare      = state.DepthStencil.DepthTestEnable
                                           ? ToWebGPU(state.DepthStencil.DepthCompareOp)
                                           : WGPUCompareFunction_Always;
            // WebGPU requires stencilFront/Back.compare == Always when format has no stencil aspect
            if (state.DepthStencil.StencilTestEnable && formatHasStencil(key.depthFormat)) {
                WGPUStencilFaceState sf {};
                sf.compare     = ToWebGPU(state.DepthStencil.StencilCompareOp);
                sf.passOp      = ToWebGPU(state.DepthStencil.StencilPassOp);
                sf.failOp      = ToWebGPU(state.DepthStencil.StencilFailOp);
                sf.depthFailOp = ToWebGPU(state.DepthStencil.StencilDepthFailOp);
                depthStencil.stencilFront     = sf;
                depthStencil.stencilBack      = sf;
                depthStencil.stencilWriteMask = static_cast<uint8_t>(state.DepthStencil.StencilWriteMask);
            } else {
                WGPUStencilFaceState noop {};
                noop.compare     = WGPUCompareFunction_Always;
                noop.failOp      = WGPUStencilOperation_Keep;
                noop.depthFailOp = WGPUStencilOperation_Keep;
                noop.passOp      = WGPUStencilOperation_Keep;
                depthStencil.stencilFront = noop;
                depthStencil.stencilBack  = noop;
            }
            depthStencilPtr = &depthStencil;
        }

        WGPURenderPipelineDescriptor desc {};
        desc.layout = key.pipelineLayout;

        desc.vertex.module       = shader.vertexModule;
        desc.vertex.entryPoint = shader.vertexEntryPoint.c_str();
        desc.vertex.bufferCount  = state.VertexInput.BindingCount;
        desc.vertex.buffers      = bufLayouts.empty() ? nullptr : bufLayouts.data();

        desc.primitive.topology        = ToWebGPU(state.InputAssembly.Topology);
        desc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
        desc.primitive.frontFace       = ToWebGPU(state.Rasterization.Front);
        desc.primitive.cullMode        = ToWebGPU(state.Rasterization.Cull);

        desc.multisample.count              = static_cast<uint32_t>(state.Multisample.Samples);
        desc.multisample.mask               = state.Multisample.SampleMask;
        desc.multisample.alphaToCoverageEnabled = state.Multisample.AlphaToCoverageEnable;

        desc.depthStencil = depthStencilPtr;
        desc.fragment     = &fragState;

        return wgpuDeviceCreateRenderPipeline(device, &desc);
    }

    // -------------------------------------------------------------------------
    // Draw calls
    // -------------------------------------------------------------------------

    void RHIDeviceWebGPU::Draw(const RHIFrameContext&,
                                uint32_t vertexCount, uint32_t instanceCount,
                                uint32_t firstVertex, uint32_t firstInstance) {
        if (!activeRenderPassEncoder) return;
        auto* shader = shaderPool.Get(pendingShaderHandle);
        if (!shader) return;

        auto* pipelineLayout = pipelineLayoutPool.Get(shader->pipelineLayoutHandle);

        PipelineKey key {};
        key.shader         = pendingShaderHandle;
        key.state          = pendingState;
        key.colorFormat    = activePassColorFormat;
        key.depthFormat    = activePassDepthFormat;
        key.pipelineLayout = pipelineLayout ? *pipelineLayout : nullptr;

        WGPURenderPipeline pipeline = pipelineCache.GetOrCreate(key,
            [&](const PipelineKey& k) { return buildPipeline(k, *shader); });
        if (!pipeline) return;

        wgpuRenderPassEncoderSetPipeline(activeRenderPassEncoder, pipeline);

        if (pendingVertexBuffer.IsValid()) {
            auto* vb = bufferPool.Get(pendingVertexBuffer);
            if (vb) wgpuRenderPassEncoderSetVertexBuffer(activeRenderPassEncoder, 0, vb->Buffer, 0, vb->Size);
        }

        for (uint32_t i = 0; i < MaxBoundDescriptorSets; i++) {
            if (!pendingDescriptorSets[i].IsValid()) continue;
            auto* ds = descriptorSetPool.Get(pendingDescriptorSets[i]);
            if (ds && ds->bindGroup)
                wgpuRenderPassEncoderSetBindGroup(activeRenderPassEncoder, i, ds->bindGroup, 0, nullptr);
        }

        if (pushConstantBG) {
            if (pendingPushConstantDirty) {
                wgpuQueueWriteBuffer(queue, pushConstantBuffer, 0, pendingPushConstantData, 256);
                pendingPushConstantDirty = false;
            }
            wgpuRenderPassEncoderSetBindGroup(activeRenderPassEncoder, PushConstantSet,
                                              pushConstantBG, 0, nullptr);
        }

        wgpuRenderPassEncoderDraw(activeRenderPassEncoder, vertexCount, instanceCount,
                                   firstVertex, firstInstance);
    }

    void RHIDeviceWebGPU::DrawIndexed(const RHIFrameContext&,
                                       uint32_t indexCount, uint32_t instanceCount,
                                       uint32_t firstIndex, int32_t vertexOffset,
                                       uint32_t firstInstance) {
        if (!activeRenderPassEncoder) return;
        auto* shader = shaderPool.Get(pendingShaderHandle);
        if (!shader) return;

        auto* pipelineLayout = pipelineLayoutPool.Get(shader->pipelineLayoutHandle);

        PipelineKey key {};
        key.shader         = pendingShaderHandle;
        key.state          = pendingState;
        key.colorFormat    = activePassColorFormat;
        key.depthFormat    = activePassDepthFormat;
        key.pipelineLayout = pipelineLayout ? *pipelineLayout : nullptr;

        WGPURenderPipeline pipeline = pipelineCache.GetOrCreate(key,
            [&](const PipelineKey& k) { return buildPipeline(k, *shader); });
        if (!pipeline) return;

        wgpuRenderPassEncoderSetPipeline(activeRenderPassEncoder, pipeline);

        if (pendingVertexBuffer.IsValid()) {
            auto* vb = bufferPool.Get(pendingVertexBuffer);
            if (vb) wgpuRenderPassEncoderSetVertexBuffer(activeRenderPassEncoder, 0, vb->Buffer, 0, vb->Size);
        }
        if (hasPendingIndexBuffer && pendingIndexBuffer.IsValid()) {
            auto* ib = bufferPool.Get(pendingIndexBuffer);
            if (ib) wgpuRenderPassEncoderSetIndexBuffer(activeRenderPassEncoder, ib->Buffer,
                                                         WGPUIndexFormat_Uint32, 0, ib->Size);
        }

        for (uint32_t i = 0; i < MaxBoundDescriptorSets; i++) {
            if (!pendingDescriptorSets[i].IsValid()) continue;
            auto* ds = descriptorSetPool.Get(pendingDescriptorSets[i]);
            if (ds && ds->bindGroup)
                wgpuRenderPassEncoderSetBindGroup(activeRenderPassEncoder, i, ds->bindGroup, 0, nullptr);
        }

        if (pushConstantBG) {
            if (pendingPushConstantDirty) {
                wgpuQueueWriteBuffer(queue, pushConstantBuffer, 0, pendingPushConstantData, 256);
                pendingPushConstantDirty = false;
            }
            wgpuRenderPassEncoderSetBindGroup(activeRenderPassEncoder, PushConstantSet,
                                              pushConstantBG, 0, nullptr);
        }

        wgpuRenderPassEncoderDrawIndexed(activeRenderPassEncoder, indexCount, instanceCount,
                                          firstIndex, vertexOffset, firstInstance);
    }

    // -------------------------------------------------------------------------
    // Descriptor Sets
    // -------------------------------------------------------------------------

    RHIDescriptorSetHandle RHIDeviceWebGPU::CreateDescriptorSet(RHIDescriptorSetLayoutHandle layoutHandle) {
        DescriptorSetData ds {};
        ds.layoutHandle = layoutHandle;
        return descriptorSetPool.Allocate(std::move(ds));
    }

    void RHIDeviceWebGPU::UpdateDescriptorSet(RHIDescriptorSetHandle handle,
                                               std::span<const RHIDescriptorWrite> writes) {
        auto* ds = descriptorSetPool.Get(handle);
        if (!ds) return;

        auto* bgl = bindGroupLayoutPool.Get(ds->layoutHandle);
        if (!bgl) return;

        if (ds->bindGroup) {
            wgpuBindGroupRelease(ds->bindGroup);
            ds->bindGroup = nullptr;
        }

        std::vector<WGPUBindGroupEntry> entries;
        entries.reserve(writes.size());

        for (const auto& write : writes) {
            WGPUBindGroupEntry entry {};
            entry.binding = write.Binding;

            switch (write.Type) {
                case DescriptorType::UniformBuffer:
                case DescriptorType::StorageBuffer: {
                    auto* buf = bufferPool.Get(write.Buffer.Buffer);
                    if (!buf) continue;
                    entry.buffer = buf->Buffer;
                    entry.offset = write.Buffer.Offset;
                    entry.size   = (write.Buffer.Range == ~0ULL) ? buf->Size : write.Buffer.Range;
                    break;
                }
                case DescriptorType::CombinedImageSampler:
                case DescriptorType::SampledImage: {
                    auto* tex = texturePool.Get(write.Image.Texture);
                    if (!tex) continue;
                    // Texture view entry
                    entry.textureView = tex->TextureView;
                    entries.push_back(entry);
                    // Sampler entry at binding+1 (Slang assigns sampler consecutively after texture)
                    if (tex->Sampler) {
                        WGPUBindGroupEntry sampEntry {};
                        sampEntry.binding = write.Binding + 1;
                        sampEntry.sampler = tex->Sampler;
                        entries.push_back(sampEntry);
                    }
                    continue; // already pushed both
                }
                case DescriptorType::Sampler: {
                    auto* tex = texturePool.Get(write.Image.Texture);
                    if (!tex) continue;
                    entry.sampler = tex->Sampler;
                    break;
                }
                default: continue;
            }
            entries.push_back(entry);
        }

        WGPUBindGroupDescriptor desc {};
        desc.layout     = *bgl;
        desc.entries    = entries.data();
        desc.entryCount = entries.size();
        ds->bindGroup = wgpuDeviceCreateBindGroup(device, &desc);
    }

    void RHIDeviceWebGPU::FreeDescriptorSet(RHIDescriptorSetHandle handle) {
        descriptorSetPool.Free(handle);
    }

    // -------------------------------------------------------------------------
    // Textures
    // -------------------------------------------------------------------------

    RHITextureHandle RHIDeviceWebGPU::CreateTexture(TextureDescriptor&& descriptor) {
        WGPUTextureDescriptor desc {};
        desc.usage           = ToWebGPU(descriptor.Usage);
        desc.dimension       = WGPUTextureDimension_2D;
        desc.size            = {descriptor.Width, descriptor.Height, 1};
        desc.format          = ToWebGPU(descriptor.Format);
        desc.mipLevelCount   = 1;
        desc.sampleCount     = 1;
        desc.viewFormatCount = 0;

        RHITextureWebGPU tex {};
        tex.Width   = descriptor.Width;
        tex.Height  = descriptor.Height;
        tex.Texture = wgpuDeviceCreateTexture(device, &desc);

        WGPUTextureViewDescriptor viewDesc {};
        viewDesc.format          = desc.format;
        viewDesc.dimension       = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel    = 0;
        viewDesc.mipLevelCount   = 1;
        viewDesc.baseArrayLayer  = 0;
        viewDesc.arrayLayerCount = 1;
        // Pure depth formats (no stencil) use DepthOnly; combined depth+stencil must use All
        // because DepthOnly would require a different view format (e.g. Depth24Plus ≠ Depth24PlusStencil8).
        bool isPureDepth = (descriptor.Format == TextureFormat::D32Float);
        viewDesc.aspect = isPureDepth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
        tex.TextureView = wgpuTextureCreateView(tex.Texture, &viewDesc);

        // Sampler (for sampled textures)
        if (static_cast<uint8_t>(descriptor.Usage) & static_cast<uint8_t>(TextureUsage::Sampled)) {
            const auto& s = descriptor.Sampler;
            WGPUSamplerDescriptor samplerDesc {};
            samplerDesc.minFilter     = ToWebGPU(s.MinFilter);
            samplerDesc.magFilter     = ToWebGPU(s.MagFilter);
            samplerDesc.mipmapFilter  = ToWebGPUMipmap(s.MinFilter);
            samplerDesc.addressModeU  = ToWebGPU(s.WrapU);
            samplerDesc.addressModeV  = ToWebGPU(s.WrapV);
            samplerDesc.addressModeW  = ToWebGPU(s.WrapW);
            samplerDesc.maxAnisotropy = 1;
            tex.Sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
        }

        return texturePool.Allocate(std::move(tex));
    }

    void RHIDeviceWebGPU::UpdateTexture(const RHITextureHandle& handle,
                                         const void* data, size_t size) {
        auto* tex = texturePool.Get(handle);
        if (!tex || !tex->Texture) return;

        WGPUImageCopyTexture dst {};
        dst.texture  = tex->Texture;
        dst.mipLevel = 0;
        dst.origin   = {0, 0, 0};
        dst.aspect   = WGPUTextureAspect_All;

        WGPUTextureDataLayout layout {};
        layout.offset       = 0;
        layout.bytesPerRow  = (tex->Height > 0 && tex->Width > 0)
                            ? static_cast<uint32_t>(size / tex->Height)
                            : tex->Width * 4;
        layout.rowsPerImage = tex->Height;

        WGPUExtent3D extent {tex->Width, tex->Height, 1};
        wgpuQueueWriteTexture(queue, &dst, data, size, &layout, &extent);
    }

    void RHIDeviceWebGPU::FreeTexture(RHITextureHandle handle) {
        texturePool.Free(handle);
    }

    // -------------------------------------------------------------------------
    // Shaders
    // -------------------------------------------------------------------------

    void RHIDeviceWebGPU::registerShaderLayouts(RHIShaderHandle handle, RHIShaderWebGPU& shader) {
        auto [plHandle, dsHandles] = CreatePipelineLayout(shader.pipelineLayoutDescriptor);
        shader.pipelineLayoutHandle = plHandle;
        shader.descriptorSetLayoutHandles.assign(dsHandles.begin(), dsHandles.end());
    }

    RHIShaderHandle RHIDeviceWebGPU::CreateShader(ShaderFileParams&& fileParams) {
        RHIShaderWebGPU shader(device, slangSession, std::move(fileParams));
        if (!shader.IsValid()) return RHIShaderHandle::Null();
        RHIShaderHandle handle = shaderPool.Allocate(std::move(shader));
        auto* s = shaderPool.Get(handle);
        registerShaderLayouts(handle, *s);
        return handle;
    }

    RHIShaderHandle RHIDeviceWebGPU::CreateShader(ShaderSourceParams&& sourceParams) {
        RHIShaderWebGPU shader(device, slangSession, std::move(sourceParams));
        if (!shader.IsValid()) return RHIShaderHandle::Null();
        RHIShaderHandle handle = shaderPool.Allocate(std::move(shader));
        auto* s = shaderPool.Get(handle);
        registerShaderLayouts(handle, *s);
        return handle;
    }

    void RHIDeviceWebGPU::FreeShader(const RHIShaderHandle& handle) {
        shaderPool.Free(handle);
    }

    RHIPipelineLayoutDescriptor RHIDeviceWebGPU::GetShaderPipelineLayout(const RHIShaderHandle& handle) {
        auto* s = shaderPool.Get(handle);
        return s ? s->pipelineLayoutDescriptor : RHIPipelineLayoutDescriptor{};
    }

    RHIPipelineLayoutHandle RHIDeviceWebGPU::GetShaderPipelineLayoutHandle(const RHIShaderHandle& handle) {
        auto* s = shaderPool.Get(handle);
        return s ? s->pipelineLayoutHandle : RHIPipelineLayoutHandle::Null();
    }

    std::vector<RHIDescriptorSetLayoutHandle>
    RHIDeviceWebGPU::GetShaderDescriptorSetLayoutHandles(const RHIShaderHandle& handle) {
        auto* s = shaderPool.Get(handle);
        return s ? s->descriptorSetLayoutHandles : std::vector<RHIDescriptorSetLayoutHandle>{};
    }

    std::pair<RHIPipelineLayoutHandle, std::set<RHIDescriptorSetLayoutHandle>>
    RHIDeviceWebGPU::CreatePipelineLayout(const RHIPipelineLayoutDescriptor& desc) {
        std::vector<WGPUBindGroupLayout> bgls;
        std::set<RHIDescriptorSetLayoutHandle> outHandles;

        for (uint32_t i = 0; i < desc.SetCount; i++) {
            RHIDescriptorSetLayoutHandle h = CreateDescriptorSetLayout(desc.Sets[i]);
            outHandles.insert(h);
            auto* bgl = bindGroupLayoutPool.Get(h);
            if (bgl) bgls.push_back(*bgl);
        }

        // If the shader uses push constants, extend the layout to include pushConstantBGL
        // at slot PushConstantSet (3). Fill any gap slots with empty BGLs.
        if (desc.PushConstantCount > 0 && pushConstantBGL) {
            while (bgls.size() < PushConstantSet) {
                const RHIDescriptorSetLayoutDescriptor emptySet {};
                auto h = CreateDescriptorSetLayout(emptySet);
                outHandles.insert(h);
                auto* bgl = bindGroupLayoutPool.Get(h);
                if (bgl) bgls.push_back(*bgl);
            }
            bgls.push_back(pushConstantBGL);
        }

        WGPUPipelineLayoutDescriptor plDesc {};
        plDesc.bindGroupLayouts     = bgls.empty() ? nullptr : bgls.data();
        plDesc.bindGroupLayoutCount = bgls.size();
        WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(device, &plDesc);

        return {pipelineLayoutPool.Allocate(std::move(pl)), outHandles};
    }

    RHIDescriptorSetLayoutHandle
    RHIDeviceWebGPU::CreateDescriptorSetLayout(const RHIDescriptorSetLayoutDescriptor& desc) {
        std::vector<WGPUBindGroupLayoutEntry> entries;
        entries.reserve(desc.BindingCount);

        for (uint32_t i = 0; i < desc.BindingCount; i++) {
            const auto& b = desc.Bindings[i];
            WGPUBindGroupLayoutEntry entry {};
            entry.binding    = b.Binding;
            entry.visibility = ToWebGPU(b.StageFlags);

            switch (b.Type) {
                case DescriptorType::UniformBuffer:
                    entry.buffer.type = WGPUBufferBindingType_Uniform;
                    break;
                case DescriptorType::StorageBuffer:
                    entry.buffer.type = WGPUBufferBindingType_Storage;
                    break;
                case DescriptorType::SampledImage:
                    entry.texture.sampleType    = WGPUTextureSampleType_Float;
                    entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                    break;
                case DescriptorType::Sampler:
                    entry.sampler.type = WGPUSamplerBindingType_Filtering;
                    break;
                case DescriptorType::CombinedImageSampler:
                    // Texture entry at the declared binding
                    entry.texture.sampleType    = WGPUTextureSampleType_Float;
                    entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                    entries.push_back(entry);
                    {
                        // Sampler entry at binding+1 (Slang WGSL convention for split combined samplers)
                        WGPUBindGroupLayoutEntry samplerEntry {};
                        samplerEntry.binding      = b.Binding + 1;
                        samplerEntry.visibility   = entry.visibility;
                        samplerEntry.sampler.type = WGPUSamplerBindingType_Filtering;
                        entries.push_back(samplerEntry);
                    }
                    continue;
                case DescriptorType::StorageImage:
                    entry.storageTexture.access        = WGPUStorageTextureAccess_WriteOnly;
                    entry.storageTexture.format        = WGPUTextureFormat_RGBA8Unorm;
                    entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                    break;
            }
            entries.push_back(entry);
        }

        WGPUBindGroupLayoutDescriptor bglDesc {};
        bglDesc.entries     = entries.empty() ? nullptr : entries.data();
        bglDesc.entryCount  = entries.size();
        WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

        return bindGroupLayoutPool.Allocate(std::move(bgl));
    }

    // -------------------------------------------------------------------------
    // Buffers
    // -------------------------------------------------------------------------

    RHIBufferHandle RHIDeviceWebGPU::CreateBuffer(BufferDescriptor&& desc) {
        WGPUBufferDescriptor wgpuDesc {};
        wgpuDesc.size  = desc.Size;
        wgpuDesc.usage = ToWebGPU(desc.Usage) | WGPUBufferUsage_CopyDst; // CopyDst for WriteBuffer
        if (desc.Access == BufferMemoryAccess::GpuToCpu)
            wgpuDesc.usage |= WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapRead;
        wgpuDesc.mappedAtCreation = false;

        RHIBufferWebGPU buf {};
        buf.Buffer = wgpuDeviceCreateBuffer(device, &wgpuDesc);
        buf.Size   = desc.Size;
        buf.Usage  = desc.Usage;
        buf.Access = desc.Access;
        return bufferPool.Allocate(std::move(buf));
    }

    void RHIDeviceWebGPU::UpdateBuffer(const RHIBufferHandle& handle,
                                        const void* data, size_t size, size_t offset) {
        auto* buf = bufferPool.Get(handle);
        if (!buf || !buf->Buffer) return;
        wgpuQueueWriteBuffer(queue, buf->Buffer, offset, data, size);
    }

    void RHIDeviceWebGPU::FreeBuffer(const RHIBufferHandle& handle) {
        bufferPool.Free(handle);
    }

} // namespace OZZ::rendering::webgpu
