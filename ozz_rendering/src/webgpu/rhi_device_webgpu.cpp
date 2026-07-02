#include "rhi_device_webgpu.h"

#include "utils/rhi_webgpu_types.h"

#include <ozz_rendering/rhi_renderpass.h>

#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>
#include <slang.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace OZZ::rendering::webgpu {

    namespace {
        // Build a WGPUSurface from platform-native window handles. Moved here from the
        // engine's SDL window so all Dawn/WebGPU surface construction lives in the backend.
        WGPUSurface CreateSurfaceFromNativeHandles(WGPUInstance instance,
                                                   const NativeWindowHandles& handles) {
            WGPUSurfaceDescriptor surfDesc = {};

            switch (handles.Platform) {
#ifdef _WIN32
                case NativeWindowHandles::Platform::Win32: {
                    WGPUSurfaceSourceWindowsHWND hwndDesc = {};
                    hwndDesc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
                    // Prefer the provided Display as HINSTANCE; fall back to GetModuleHandle.
                    hwndDesc.hinstance   = handles.Display ? handles.Display : GetModuleHandle(nullptr);
                    hwndDesc.hwnd        = handles.Window;
                    surfDesc.nextInChain = &hwndDesc.chain;
                    return wgpuInstanceCreateSurface(instance, &surfDesc);
                }
#endif
                case NativeWindowHandles::Platform::Wayland: {
                    WGPUSurfaceSourceWaylandSurface wlDesc = {};
                    wlDesc.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
                    wlDesc.display     = handles.Display;
                    wlDesc.surface     = handles.Window;
                    surfDesc.nextInChain = &wlDesc.chain;
                    return wgpuInstanceCreateSurface(instance, &surfDesc);
                }
                case NativeWindowHandles::Platform::X11: {
                    WGPUSurfaceSourceXlibWindow xlibDesc = {};
                    xlibDesc.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
                    xlibDesc.display     = handles.Display;
                    xlibDesc.window      = handles.WindowId;
                    surfDesc.nextInChain = &xlibDesc.chain;
                    return wgpuInstanceCreateSurface(instance, &surfDesc);
                }
                default:
                    return nullptr;
            }
        }
    } // namespace

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
        , bindGroupLayoutPool([](BindGroupLayoutData& data) {
            if (data.bgl) wgpuBindGroupLayoutRelease(data.bgl);
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

        if (emptyBG)            { wgpuBindGroupRelease(emptyBG);                 emptyBG            = nullptr; }
        if (emptyBGL)           { wgpuBindGroupLayoutRelease(emptyBGL);          emptyBGL           = nullptr; }
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

        // Surface — the WebGPU backend owns surface construction. The engine only supplies
        // platform-native window handles via GetNativeWindowHandlesFunction; we build the
        // WGPUSurfaceDescriptor chain here.
        if (!platformContext.GetNativeWindowHandlesFunction)
            throw std::runtime_error("WebGPU backend requires PlatformContext::GetNativeWindowHandlesFunction");
        const NativeWindowHandles nativeHandles = platformContext.GetNativeWindowHandlesFunction();
        if (nativeHandles.Platform == NativeWindowHandles::Platform::None)
            throw std::runtime_error("WebGPU surface: no native window handles available for this platform");
        surface = CreateSurfaceFromNativeHandles(instance, nativeHandles);
        if (!surface)
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
        deviceDesc.uncapturedErrorCallbackInfo.callback =
            [](WGPUErrorType type, char const* message, void*) {
                spdlog::error("WebGPU error ({}): {}", static_cast<int>(type),
                              message ? message : "");
            };
        deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        deviceDesc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason reason, char const* message, void*) {
                // Destroyed and InstanceDropped are expected at shutdown; only log real losses.
                if (reason != WGPUDeviceLostReason_Destroyed &&
                    reason != WGPUDeviceLostReason_InstanceDropped)
                    spdlog::error("WebGPU device lost ({}): {}", static_cast<int>(reason),
                                  message ? message : "");
            };
        wgpuAdapterRequestDevice(
            adapter, &deviceDesc,
            [](WGPURequestDeviceStatus status, WGPUDevice d, char const*, void* ud) {
                if (status == WGPURequestDeviceStatus_Success)
                    *static_cast<WGPUDevice*>(ud) = d;
            },
            &device);
        if (!device) throw std::runtime_error("Failed to create WebGPU device");

        queue = wgpuDeviceGetQueue(device);

        // Surface format — prefer an sRGB variant so the GPU automatically converts
        // linear -> sRGB on output, same as the Vulkan backend's
        // ChooseSurfaceFormatAndColorSpace (see initialization.h). Without this, WebGPU
        // picks whatever Dawn lists first (typically a plain Unorm format), leaving
        // shader output un-gamma-corrected and rendering visibly darker/lower-contrast
        // than the Vulkan path for the same clear colors and shader math.
        WGPUSurfaceCapabilities caps = {};
        wgpuSurfaceGetCapabilities(surface, adapter, &caps);
        swapchainFormat = (caps.formatCount > 0) ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
        for (size_t i = 0; i < caps.formatCount; i++) {
            if (caps.formats[i] == WGPUTextureFormat_BGRA8UnormSrgb ||
                caps.formats[i] == WGPUTextureFormat_RGBA8UnormSrgb) {
                swapchainFormat = caps.formats[i];
                break;
            }
        }
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

        // Push constant emulation: dynamic-offset uniform buffer at set=PushConstantSet, binding=0.
        // Sized to hold PushConstantSlotsPerFrame independent slots per in-flight frame so
        // concurrent CPU recording of frame N+1 can't alias frame N's still-in-flight data.
        {
            WGPUBufferDescriptor pcBufDesc {};
            pcBufDesc.size  = static_cast<uint64_t>(PushConstantSlotSize) * PushConstantSlotsPerFrame * MaxFramesInFlight;
            pcBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            pushConstantBuffer = wgpuDeviceCreateBuffer(device, &pcBufDesc);

            WGPUBindGroupLayoutEntry pcEntry {};
            pcEntry.binding                  = PushConstantBinding;
            pcEntry.visibility               = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            pcEntry.buffer.type              = WGPUBufferBindingType_Uniform;
            pcEntry.buffer.hasDynamicOffset  = true;
            pcEntry.buffer.minBindingSize    = PushConstantSlotSize;
            WGPUBindGroupLayoutDescriptor pcBGLDesc {};
            pcBGLDesc.entryCount = 1;
            pcBGLDesc.entries    = &pcEntry;
            pushConstantBGL = wgpuDeviceCreateBindGroupLayout(device, &pcBGLDesc);

            WGPUBindGroupEntry pcBGEntry {};
            pcBGEntry.binding = PushConstantBinding;
            pcBGEntry.buffer  = pushConstantBuffer;
            pcBGEntry.offset  = 0;
            pcBGEntry.size    = PushConstantSlotSize;
            WGPUBindGroupDescriptor pcBGDesc {};
            pcBGDesc.layout     = pushConstantBGL;
            pcBGDesc.entryCount = 1;
            pcBGDesc.entries    = &pcBGEntry;
            pushConstantBG = wgpuDeviceCreateBindGroup(device, &pcBGDesc);
        }

        // Empty BGL and BG — used to satisfy gap set slots in pipeline layouts
        {
            WGPUBindGroupLayoutDescriptor emptyBGLDesc {};
            emptyBGLDesc.entryCount = 0;
            emptyBGLDesc.entries    = nullptr;
            emptyBGL = wgpuDeviceCreateBindGroupLayout(device, &emptyBGLDesc);

            WGPUBindGroupDescriptor emptyBGDesc {};
            emptyBGDesc.layout     = emptyBGL;
            emptyBGDesc.entryCount = 0;
            emptyBGDesc.entries    = nullptr;
            emptyBG = wgpuDeviceCreateBindGroup(device, &emptyBGDesc);
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
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        pushConstantCursor = 0;
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
        // Swapchain images are always a color format; the exact enum only needs to be
        // non-depth so lazy depth resolution never mistakes it for a depth texture.
        swapTex.Format    = TextureFormat::BGRA8;
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
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
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

        // Dawn (native/Vulkan backend) needs this pumped once per frame to process
        // completed GPU work and advance internal per-queue bookkeeping (submitted
        // command buffer reclamation, recording-context lifecycle). Without it, that
        // internal state never advances and eventually hits an internal assertion
        // (GetPendingRecordingContext) once enough frames/resources have piled up.
        wgpuDeviceTick(device);

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
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        return {swapchainWidth, swapchainHeight};
    }

    // -------------------------------------------------------------------------
    // Render Pass
    // -------------------------------------------------------------------------

    void RHIDeviceWebGPU::BeginRenderPass(const RHIFrameContext& frameContext,
                                           const RenderPassDescriptor& rpDesc) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        if (!activeEncoder) return;

        // Reset active pass formats; they will be set below from the actual attachments.
        activePassColorFormat = WGPUTextureFormat_Undefined;
        activePassDepthFormat = WGPUTextureFormat_Undefined;
        activePassWidth  = 0;
        activePassHeight = 0;

        // Graphics state does not carry across render passes: callers must call
        // SetGraphicsState after each BeginRenderPass, before any draw.
        pendingState     = {};
        stateSetThisPass = false;

        std::vector<WGPURenderPassColorAttachment> colorAttachments;
        colorAttachments.reserve(rpDesc.ColorAttachmentCount);

        for (uint32_t i = 0; i < rpDesc.ColorAttachmentCount; i++) {
            const auto& att = rpDesc.ColorAttachments[i];
            auto* tex = texturePool.Get(att.Texture);
            if (!tex) continue;

            // Capture the first color attachment's format for pipeline key building.
            if (i == 0) {
                activePassColorFormat = wgpuTextureGetFormat(tex->Texture);
                activePassWidth  = tex->Width;
                activePassHeight = tex->Height;
            }

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
                    if (activePassWidth == 0) {
                        activePassWidth  = dTex->Width;
                        activePassHeight = dTex->Height;
                    }
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
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        if (activeRenderPassEncoder) {
            wgpuRenderPassEncoderEnd(activeRenderPassEncoder);
            wgpuRenderPassEncoderRelease(activeRenderPassEncoder);
            activeRenderPassEncoder = nullptr;
        }
    }

    // -------------------------------------------------------------------------
    // Barriers — no-ops in WebGPU (implicit synchronization)
    // -------------------------------------------------------------------------

    // Barriers are no-ops in WebGPU — synchronization is implicit.
    void RHIDeviceWebGPU::TextureResourceBarrier(const RHIFrameContext&,
                                                  const TextureBarrierDescriptor&) {}

    void RHIDeviceWebGPU::BufferMemoryBarrier(const RHIFrameContext&,
                                               const BufferBarrierDescriptor&) {}

    // -------------------------------------------------------------------------
    // State recording
    // -------------------------------------------------------------------------

    void RHIDeviceWebGPU::SetViewport(const RHIFrameContext&, const Viewport& vp) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        if (!activeRenderPassEncoder) return;
        // Clamp to the pass attachment extent: mid-resize the engine can send a
        // rect sized for the previous frame, and Dawn invalidates the whole
        // command buffer on an out-of-bounds viewport.
        const auto maxW = static_cast<float>(activePassWidth);
        const auto maxH = static_cast<float>(activePassHeight);
        const float x = std::clamp(vp.X, 0.0f, maxW);
        const float y = std::clamp(vp.Y, 0.0f, maxH);
        const float w = std::min(vp.Width,  maxW - x);
        const float h = std::min(vp.Height, maxH - y);
        if (w <= 0.0f || h <= 0.0f) return;
        wgpuRenderPassEncoderSetViewport(activeRenderPassEncoder,
                                          x, y, w, h,
                                          vp.MinDepth, vp.MaxDepth);
    }

    void RHIDeviceWebGPU::SetScissor(const RHIFrameContext&, const Scissor& sc) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        if (!activeRenderPassEncoder) return;
        // Same clamp rationale as SetViewport.
        const uint32_t x = std::min(static_cast<uint32_t>(std::max(sc.X, 0)), activePassWidth);
        const uint32_t y = std::min(static_cast<uint32_t>(std::max(sc.Y, 0)), activePassHeight);
        const uint32_t w = std::min(sc.Width,  activePassWidth  - x);
        const uint32_t h = std::min(sc.Height, activePassHeight - y);
        wgpuRenderPassEncoderSetScissorRect(activeRenderPassEncoder, x, y, w, h);
    }

    void RHIDeviceWebGPU::SetGraphicsState(const RHIFrameContext&,
                                            const GraphicsStateDescriptor& state) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        pendingState     = state;
        stateSetThisPass = true;
    }

    void RHIDeviceWebGPU::BindShader(const RHIFrameContext&, const RHIShaderHandle& handle) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        pendingShaderHandle = handle;
    }

    void RHIDeviceWebGPU::BindBuffer(const RHIFrameContext&, const RHIBufferHandle& handle) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
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
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        if (!data || size == 0 || offset + size > PushConstantSlotSize) return;
        if (pushConstantCursor >= PushConstantSlotsPerFrame) {
            spdlog::error("WebGPU: push constant slots exhausted for this frame ({} draws)",
                          PushConstantSlotsPerFrame);
            return;
        }
        const uint64_t slotBase = (static_cast<uint64_t>(currentFrameIndex) * PushConstantSlotsPerFrame +
                                    pushConstantCursor) * PushConstantSlotSize;
        pushConstantCursor++;
        wgpuQueueWriteBuffer(queue, pushConstantBuffer, slotBase + offset, data, size);
        pendingPushConstantOffset = static_cast<uint32_t>(slotBase);
    }

    void RHIDeviceWebGPU::BindDescriptorSet(const RHIFrameContext&,
                                             RHIPipelineLayoutHandle,
                                             uint32_t setIndex,
                                             RHIDescriptorSetHandle descriptorSetHandle) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
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

    bool RHIDeviceWebGPU::flushPendingDrawState() {
        if (!stateSetThisPass) {
            spdlog::error("Draw issued without SetGraphicsState in current render pass");
#ifdef OZZ_DEBUG
            assert(false && "Draw without SetGraphicsState in current render pass");
#endif
            // Skip the draw: without explicit state the pipeline key would be built
            // from a default-constructed GraphicsStateDescriptor (garbage pipeline).
            return false;
        }
        if (!activeRenderPassEncoder) return false;
        auto* shader = shaderPool.Get(pendingShaderHandle);
        if (!shader) return false;

        auto* pipelineLayout = pipelineLayoutPool.Get(shader->pipelineLayoutHandle);

        PipelineKey key {};
        key.shader         = pendingShaderHandle;
        key.state          = pendingState;
        key.colorFormat    = activePassColorFormat;
        key.depthFormat    = activePassDepthFormat;
        key.pipelineLayout = pipelineLayout ? *pipelineLayout : nullptr;

        WGPURenderPipeline pipeline = pipelineCache.GetOrCreate(key,
            [&](const PipelineKey& k) { return buildPipeline(k, *shader); });
        if (!pipeline) {
            spdlog::error("WebGPU: pipeline build failed for shaderHandle.Id={}", pendingShaderHandle.Id);
            return false;
        }

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

        // Only bind group PushConstantSet if THIS shader's pipeline layout actually
        // declared push constants (both reflection paths signal this via
        // PushConstantCount). Unconditionally binding group 3 (the old behavior)
        // mismatches shaders that truly have no push constant at all, e.g. hex.vert/frag —
        // WebGPU then rejects the whole command buffer ("does not match layout... at
        // group index 3" or "no bind group set at group index 1" once the resulting gap
        // in set numbering is also skipped).
        const bool hasPushConstants = shader->pipelineLayoutDescriptor.PushConstantCount > 0;
        if (pushConstantBG && hasPushConstants) {
            // Bind empty groups for gap slots (between last real set and PushConstantSet)
            for (uint32_t i = 1; i < PushConstantSet; i++) {
                if (!pendingDescriptorSets[i].IsValid() && emptyBG)
                    wgpuRenderPassEncoderSetBindGroup(activeRenderPassEncoder, i, emptyBG, 0, nullptr);
            }
            // pendingPushConstantOffset is sticky: a draw whose shader declares push
            // constants but never called SetPushConstants this frame reuses the previous
            // draw's slot — intentional, matching Vulkan push-constant stickiness.
            wgpuRenderPassEncoderSetBindGroup(activeRenderPassEncoder, PushConstantSet,
                                              pushConstantBG, 1, &pendingPushConstantOffset);
        }

        return true;
    }

    void RHIDeviceWebGPU::Draw(const RHIFrameContext&,
                                uint32_t vertexCount, uint32_t instanceCount,
                                uint32_t firstVertex, uint32_t firstInstance) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        if (!flushPendingDrawState()) return;

        wgpuRenderPassEncoderDraw(activeRenderPassEncoder, vertexCount, instanceCount,
                                   firstVertex, firstInstance);
    }

    void RHIDeviceWebGPU::DrawIndexed(const RHIFrameContext&,
                                       uint32_t indexCount, uint32_t instanceCount,
                                       uint32_t firstIndex, int32_t vertexOffset,
                                       uint32_t firstInstance) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        if (!flushPendingDrawState()) return;

        if (hasPendingIndexBuffer && pendingIndexBuffer.IsValid()) {
            auto* ib = bufferPool.Get(pendingIndexBuffer);
            if (ib) wgpuRenderPassEncoderSetIndexBuffer(activeRenderPassEncoder, ib->Buffer,
                                                         WGPUIndexFormat_Uint32, 0, ib->Size);
        }

        wgpuRenderPassEncoderDrawIndexed(activeRenderPassEncoder, indexCount, instanceCount,
                                          firstIndex, vertexOffset, firstInstance);
    }

    // -------------------------------------------------------------------------
    // Descriptor Sets
    // -------------------------------------------------------------------------

    RHIDescriptorSetHandle RHIDeviceWebGPU::CreateDescriptorSet(RHIDescriptorSetLayoutHandle layoutHandle) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        DescriptorSetData ds {};
        ds.layoutHandle = layoutHandle;
        return descriptorSetPool.Allocate(std::move(ds));
    }

    void RHIDeviceWebGPU::UpdateDescriptorSet(RHIDescriptorSetHandle handle,
                                               std::span<const RHIDescriptorWrite> writes) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        auto* ds = descriptorSetPool.Get(handle);
        if (!ds) return;

        auto* bgl = bindGroupLayoutPool.Get(ds->layoutHandle);
        if (!bgl) return;

        // Lazy depth sample-type resolution: for each SampledImage write, inspect the bound
        // texture's format. If it is a depth format, that binding must be declared
        // UnfilterableFloat (+ NonFiltering sampler at binding+1); WebGPU rejects a plain
        // Float sample type against a depth texture at bind-group creation. We rebuild the
        // WGPUBindGroupLayout in place (same pool slot) the first time we learn this, then
        // rebuild any pipeline layouts that reference it.
        bool bglMutated = false;
        for (const auto& write : writes) {
            if (write.Type != DescriptorType::SampledImage) continue;
            const uint32_t b = write.Binding;
            if (b >= MaxBoundDescriptorSets) continue;
            auto* tex = texturePool.Get(write.Image.Texture);
            if (!tex) continue;
            const bool texIsDepth = IsDepthFormat(tex->Format);

            if (texIsDepth) {
                if (!bgl->depthResolved[b]) {
                    // First time we learn this binding samples depth — promote it.
                    bgl->depthResolved[b] = true;
                    bglMutated = true;
                }
            } else {
                // Flip-flop guard: a color texture on a binding we already resolved as depth.
                if (bgl->depthResolved[b]) {
                    spdlog::error("WebGPU: binding {} previously resolved as a depth texture "
                                  "is now bound with a color texture; sample-type mismatch",
                                  b);
                }
            }
        }

        if (bglMutated) {
            // Rebuild the bind-group layout object in place, reusing the same pool handle.
            if (bgl->bgl) wgpuBindGroupLayoutRelease(bgl->bgl);
            bgl->bgl = buildBindGroupLayoutObject(bgl->sourceDesc, bgl->depthResolved);
            // Rebuild affected pipeline layouts (also in place) so pipelines pick up the
            // new layout. Note: rebuildPipelineLayoutsUsingDSL re-Get()s bindGroupLayoutPool,
            // which is fine — the mutated slot already holds the new object.
            rebuildPipelineLayoutsUsingDSL(ds->layoutHandle);
        }

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
                case DescriptorType::StorageBuffer:
                case DescriptorType::ReadOnlyStorageBuffer: {
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
        desc.layout     = bgl->bgl;
        desc.entries    = entries.data();
        desc.entryCount = entries.size();
        ds->bindGroup = wgpuDeviceCreateBindGroup(device, &desc);
    }

    void RHIDeviceWebGPU::FreeDescriptorSet(RHIDescriptorSetHandle handle) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        descriptorSetPool.Free(handle);
    }

    // -------------------------------------------------------------------------
    // Textures
    // -------------------------------------------------------------------------

    RHITextureHandle RHIDeviceWebGPU::CreateTexture(TextureDescriptor&& descriptor) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
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
        tex.Format  = descriptor.Format;
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
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
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
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
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
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        RHIShaderWebGPU shader(device, slangSession, std::move(fileParams));
        if (!shader.IsValid()) return RHIShaderHandle::Null();
        RHIShaderHandle handle = shaderPool.Allocate(std::move(shader));
        auto* s = shaderPool.Get(handle);
        registerShaderLayouts(handle, *s);
        return handle;
    }

    RHIShaderHandle RHIDeviceWebGPU::CreateShader(ShaderSourceParams&& sourceParams) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        RHIShaderWebGPU shader(device, slangSession, std::move(sourceParams));
        if (!shader.IsValid()) return RHIShaderHandle::Null();
        RHIShaderHandle handle = shaderPool.Allocate(std::move(shader));
        auto* s = shaderPool.Get(handle);
        registerShaderLayouts(handle, *s);
        return handle;
    }

    void RHIDeviceWebGPU::FreeShader(const RHIShaderHandle& handle) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        shaderPool.Free(handle);
    }

    RHIPipelineLayoutDescriptor RHIDeviceWebGPU::GetShaderPipelineLayout(const RHIShaderHandle& handle) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        auto* s = shaderPool.Get(handle);
        return s ? s->pipelineLayoutDescriptor : RHIPipelineLayoutDescriptor{};
    }

    RHIPipelineLayoutHandle RHIDeviceWebGPU::GetShaderPipelineLayoutHandle(const RHIShaderHandle& handle) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        auto* s = shaderPool.Get(handle);
        return s ? s->pipelineLayoutHandle : RHIPipelineLayoutHandle::Null();
    }

    std::vector<RHIDescriptorSetLayoutHandle>
    RHIDeviceWebGPU::GetShaderDescriptorSetLayoutHandles(const RHIShaderHandle& handle) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        auto* s = shaderPool.Get(handle);
        return s ? s->descriptorSetLayoutHandles : std::vector<RHIDescriptorSetLayoutHandle>{};
    }

    std::pair<RHIPipelineLayoutHandle, std::set<RHIDescriptorSetLayoutHandle>>
    RHIDeviceWebGPU::CreatePipelineLayout(const RHIPipelineLayoutDescriptor& desc) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        std::set<RHIDescriptorSetLayoutHandle> outHandles;
        // Ordered list of the DSL handles as they map to bind-group slots 0..PushConstantSet-1.
        std::vector<RHIDescriptorSetLayoutHandle> orderedHandles;

        // Slot PushConstantSet (3) is reserved for push-constant emulation and is always
        // special-cased below via pushConstantBGL — never build it here via the generic
        // path. A layout built here would be NON-dynamic-offset — incompatible with the
        // dynamic-offset pushConstantBG the draw calls actually bind there, causing WebGPU
        // to reject the pipeline ("does not match layout... at group index 3").
        for (uint32_t i = 0; i < desc.SetCount && i < PushConstantSet; i++) {
            RHIDescriptorSetLayoutHandle h = CreateDescriptorSetLayout(desc.Sets[i]);
            outHandles.insert(h);
            orderedHandles.push_back(h);
        }

        // Both reflection paths (GLSL and Slang) signal push constants via
        // PushConstantCount; wire up slot PushConstantSet for the emulation buffer.
        const bool hasPushConstants = desc.PushConstantCount > 0;
        if (hasPushConstants && pushConstantBGL) {
            while (orderedHandles.size() < PushConstantSet) {
                const RHIDescriptorSetLayoutDescriptor emptySet {};
                auto h = CreateDescriptorSetLayout(emptySet);
                outHandles.insert(h);
                orderedHandles.push_back(h);
            }
        }

        WGPUPipelineLayout pl = buildPipelineLayoutFromHandles(orderedHandles, desc);

        return {pipelineLayoutPool.Allocate(std::move(pl)), outHandles};
    }

    WGPUPipelineLayout RHIDeviceWebGPU::buildPipelineLayoutFromHandles(
        const std::vector<RHIDescriptorSetLayoutHandle>& dslHandles,
        const RHIPipelineLayoutDescriptor& desc) {
        std::vector<WGPUBindGroupLayout> bgls;
        bgls.reserve(dslHandles.size() + 1);
        for (const auto& h : dslHandles) {
            auto* data = bindGroupLayoutPool.Get(h);
            if (data && data->bgl) bgls.push_back(data->bgl);
        }
        // Append the dynamic-offset push-constant BGL at slot PushConstantSet when declared.
        if (desc.PushConstantCount > 0 && pushConstantBGL) {
            bgls.push_back(pushConstantBGL);
        }

        WGPUPipelineLayoutDescriptor plDesc {};
        plDesc.bindGroupLayouts     = bgls.empty() ? nullptr : bgls.data();
        plDesc.bindGroupLayoutCount = bgls.size();
        return wgpuDeviceCreatePipelineLayout(device, &plDesc);
    }

    void RHIDeviceWebGPU::rebuildPipelineLayoutsUsingDSL(RHIDescriptorSetLayoutHandle mutatedHandle) {
        // For every shader whose descriptor-set-layout handles include the mutated handle,
        // rebuild its WGPUPipelineLayout in place (same pool slot). The pipeline cache keys
        // on the WGPUPipelineLayout pointer, so a fresh pointer yields fresh pipelines
        // automatically; stale cache entries are harmless.
        shaderPool.ForEach([&](const RHIShaderHandle&, RHIShaderWebGPU& shader) {
            bool uses = false;
            for (const auto& h : shader.descriptorSetLayoutHandles) {
                if (h == mutatedHandle) {
                    uses = true;
                    break;
                }
            }
            if (!uses) return;

            // descriptorSetLayoutHandles are stored in set-index / slot order (the order
            // CreatePipelineLayout allocated them); reuse them directly.
            WGPUPipelineLayout newPl =
                buildPipelineLayoutFromHandles(shader.descriptorSetLayoutHandles,
                                               shader.pipelineLayoutDescriptor);
            if (!newPl) return;

            // Replace the WGPU object inside the existing pool slot (handle unchanged).
            if (auto* slot = pipelineLayoutPool.Get(shader.pipelineLayoutHandle)) {
                if (*slot) wgpuPipelineLayoutRelease(*slot);
                *slot = newPl;
            } else {
                wgpuPipelineLayoutRelease(newPl);
            }
        });
    }

    WGPUBindGroupLayout RHIDeviceWebGPU::buildBindGroupLayoutObject(
        const RHIDescriptorSetLayoutDescriptor& desc,
        const std::array<bool, MaxBoundDescriptorSets>& depthResolved) {
        std::vector<WGPUBindGroupLayoutEntry> entries;
        entries.reserve(desc.BindingCount);

        // Slang-reflected layouts declare texture/sampler pairs as EXPLICIT separate
        // bindings (Sampler at texture binding+1); the GLSL-patch path instead consumed
        // the sampler and relies on the auto-added entry below. Collect the explicit
        // sampler bindings so a depth-resolved texture demotes its existing sampler
        // entry to NonFiltering instead of appending a duplicate binding.
        std::array<bool, MaxBoundDescriptorSets * 2> explicitSampler {};
        for (uint32_t i = 0; i < desc.BindingCount; i++) {
            const auto& b = desc.Bindings[i];
            if (b.Count != 0 && b.Type == DescriptorType::Sampler &&
                b.Binding < explicitSampler.size()) {
                explicitSampler[b.Binding] = true;
            }
        }
        auto isDepthPairedSampler = [&](uint32_t samplerBinding) {
            // A sampler at binding N pairs with a texture at N-1.
            if (samplerBinding == 0) return false;
            const uint32_t texBinding = samplerBinding - 1;
            return texBinding < MaxBoundDescriptorSets && depthResolved[texBinding];
        };

        for (uint32_t i = 0; i < desc.BindingCount; i++) {
            const auto& b = desc.Bindings[i];
            if (b.Count == 0) continue; // empty/consumed slot — skip
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
                case DescriptorType::ReadOnlyStorageBuffer:
                    entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                    break;
                case DescriptorType::SampledImage: {
                    // Default: Float sample type. Lazily promoted to UnfilterableFloat once
                    // UpdateDescriptorSet observes a depth-format texture bound here
                    // (depthResolved[Binding] set), rebuilding this layout in place.
                    bool isDepth = (b.Binding < MaxBoundDescriptorSets) && depthResolved[b.Binding];
                    entry.texture.sampleType    = isDepth ? WGPUTextureSampleType_UnfilterableFloat
                                                           : WGPUTextureSampleType_Float;
                    entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                    entries.push_back(entry);
                    if (isDepth && !(b.Binding + 1 < explicitSampler.size() &&
                                     explicitSampler[b.Binding + 1])) {
                        // No explicit sampler declared at binding+1 (GLSL-patch layouts):
                        // add the paired NonFiltering sampler entry ourselves.
                        WGPUBindGroupLayoutEntry samplerEntry {};
                        samplerEntry.binding      = b.Binding + 1;
                        samplerEntry.visibility   = entry.visibility;
                        samplerEntry.sampler.type = WGPUSamplerBindingType_NonFiltering;
                        entries.push_back(samplerEntry);
                    }
                    continue;
                }
                case DescriptorType::Sampler:
                    // A sampler paired with a depth-resolved texture must be NonFiltering.
                    entry.sampler.type = isDepthPairedSampler(b.Binding)
                                             ? WGPUSamplerBindingType_NonFiltering
                                             : WGPUSamplerBindingType_Filtering;
                    break;
                case DescriptorType::StorageImage:
                    // Stub: valid only for RGBA8Unorm write-only storage images. The RHI
                    // descriptor carries no format/access info yet — it will need to be
                    // plumbed through when a storage image is first used otherwise.
                    entry.storageTexture.access        = WGPUStorageTextureAccess_WriteOnly;
                    entry.storageTexture.format        = WGPUTextureFormat_RGBA8Unorm;
                    entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                    break;
                // CombinedImageSampler is produced only by the Vulkan GLSL reflection path;
                // Slang WGSL reflection (the only WebGPU source) emits SampledImage/Sampler,
                // so it is unreachable here. Skip it to keep the switch exhaustive.
                default:
                    continue;
            }
            entries.push_back(entry);
        }

        WGPUBindGroupLayoutDescriptor bglDesc {};
        bglDesc.entries     = entries.empty() ? nullptr : entries.data();
        bglDesc.entryCount  = entries.size();
        return wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    }

    RHIDescriptorSetLayoutHandle
    RHIDeviceWebGPU::CreateDescriptorSetLayout(const RHIDescriptorSetLayoutDescriptor& desc) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);

        BindGroupLayoutData data {};
        data.sourceDesc = desc;
        data.bgl = buildBindGroupLayoutObject(desc, data.depthResolved);

        return bindGroupLayoutPool.Allocate(std::move(data));
    }

    // -------------------------------------------------------------------------
    // Buffers
    // -------------------------------------------------------------------------

    RHIBufferHandle RHIDeviceWebGPU::CreateBuffer(BufferDescriptor&& desc) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
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
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        auto* buf = bufferPool.Get(handle);
        if (!buf || !buf->Buffer) return;
        wgpuQueueWriteBuffer(queue, buf->Buffer, offset, data, size);
    }

    void RHIDeviceWebGPU::FreeBuffer(const RHIBufferHandle& handle) {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        bufferPool.Free(handle);
    }

} // namespace OZZ::rendering::webgpu
