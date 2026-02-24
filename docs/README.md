# ozz_rendering — Architecture & API Reference

This document covers the architecture of `ozz_rendering` and serves as the primary API reference for library consumers.

> **Note:** The other files in this folder (`vulkan-rhi-implementation-plan.md`, `adding-vertex-buffers.md`, `shader-abstractions-and-material-layer.md`) are supplementary design notes and brainstorming documents. They are not normative documentation.

---

## Table of contents

1. [Architecture overview](#architecture-overview)
2. [API reference](#api-reference)
   - [Device creation](#device-creation)
   - [Frame lifecycle](#frame-lifecycle)
   - [Render passes](#render-passes)
   - [Graphics state](#graphics-state)
   - [Viewport and scissor](#viewport-and-scissor)
   - [Resource barriers](#resource-barriers)
   - [Shaders](#shaders)
   - [Draw calls](#draw-calls)
   - [Resource handles](#resource-handles)
3. [Vulkan backend](#vulkan-backend)

---

## Architecture overview

### Device-centric model

`RHIDevice` is the central object. It owns all GPU resources and is the only way to create, use, and destroy them. Calling code never holds raw GPU objects — only opaque handles.

```
RHIDevice
 ├── owns: textures, shaders, command buffers
 ├── creates: RHITextureHandle, RHIShaderHandle, RHICommandBufferHandle
 └── accepts: handles back for commands and destruction
```

The same `RHIDevice` interface will be implemented by both the Vulkan and the planned OpenGL backends. `CreateRHIDevice` selects the backend at runtime based on `RHIBackend` or platform availability.

### Frame lifecycle

Every rendered frame follows the same sequence:

```
BeginFrame()
    │
    ├── TextureResourceBarrier  (Undefined → ColorAttachment)
    │
    ▼
BeginRenderPass()
    │
    ├── SetGraphicsState()
    ├── SetViewport() / SetScissor()
    ├── BindShader()
    └── Draw() / DrawIndexed()
    │
    ▼
EndRenderPass()
    │
    ▼
SubmitAndPresentFrame()
    │
    └── TextureResourceBarrier  (ColorAttachment → Present)
```

`BeginFrame` returns a `FrameContext` that carries the command buffer and the current backbuffer handle for that frame. `SubmitAndPresentFrame` consumes it (move-only) and submits the recorded commands to the GPU.

### Resource handles

All GPU resources are referenced by `RHIHandle<Tag>` — a lightweight, generational handle:

```cpp
template <typename Tag>
struct RHIHandle {
    uint32_t Id         {UINT32_MAX};
    uint32_t Generation {0};

    bool IsValid() const;
    static RHIHandle<Tag> Null();
};
```

The `Generation` field detects use-after-free: freeing a resource increments the generation in the internal slot, so any surviving handle with the old generation will fail `IsValidHandle` checks. Null handles have `Id == UINT32_MAX`.

Defined aliases:

| Alias | Tag |
|---|---|
| `RHITextureHandle` | `TextureTag` |
| `RHICommandBufferHandle` | `CommandBufferTag` |
| `RHIShaderHandle` | `ShaderTag` |

---

## API reference

All types live in the `OZZ::rendering` namespace. Include `<ozz_rendering/rhi.h>` to pull in the full public surface.

---

### Device creation

```cpp
std::unique_ptr<RHIDevice> CreateRHIDevice(const RHIInitParams& params);
```

**`RHIInitParams`**

| Field | Type | Default | Description |
|---|---|---|---|
| `Backend` | `RHIBackend` | `Auto` | Backend to use. `Auto` selects Vulkan if available, falling back to OpenGL. |
| `Context` | `PlatformContext` | — | Platform-specific bootstrap information. |

**`RHIBackend`**

```cpp
enum class RHIBackend { Auto, Vulkan, OpenGL };
```

**`PlatformContext`**

| Field | Type | Description |
|---|---|---|
| `AppName` | `std::string` | Application name reported to the driver. |
| `AppVersion` | `tuple<int,int,int,int>` | Application version (major, minor, patch, variant). |
| `EngineName` | `std::string` | Engine name reported to the driver. |
| `EngineVersion` | `tuple<int,int,int,int>` | Engine version. |
| `WindowHandle` | `void*` | Platform window handle (e.g. `GLFWwindow*`). |
| `RequiredInstanceExtensions` | `vector<string>` | Vulkan instance extensions required by the windowing layer (e.g. from `glfwGetRequiredInstanceExtensions`). |
| `GetWindowFramebufferSizeFunction` | `function<pair<int,int>()>` | Callback that returns the current framebuffer size in pixels. |
| `CreateSurfaceFunction` | `function<bool(void*, void*)>` | Callback that creates the presentation surface. Receives opaque `(VkInstance*, VkSurfaceKHR**)` pointers. |

---

### Frame lifecycle

#### `BeginFrame`

```cpp
FrameContext RHIDevice::BeginFrame();
```

Waits for the previous frame's fence, acquires the next swapchain image, begins the command buffer, and transitions the backbuffer to `ColorAttachment` layout. Returns a `FrameContext` on success, or `FrameContext::Null()` on failure — check with `IsValid()`.

#### `FrameContext`

Move-only. Provides:

```cpp
RHICommandBufferHandle FrameContext::GetCommandBuffer();
RHITextureHandle       FrameContext::GetBackbuffer();
bool                   FrameContext::IsValid() const;
```

#### `SubmitAndPresentFrame`

```cpp
void RHIDevice::SubmitAndPresentFrame(FrameContext context);
```

Transitions the backbuffer to `Present` layout, ends and submits the command buffer, and calls `vkQueuePresentKHR`. Takes ownership of the `FrameContext`.

---

### Render passes

```cpp
void RHIDevice::BeginRenderPass(const RHICommandBufferHandle&, const RenderPassDescriptor&);
void RHIDevice::EndRenderPass(const RHICommandBufferHandle&);
```

**`RenderPassDescriptor`**

| Field | Type | Description |
|---|---|---|
| `ColorAttachments` | `AttachmentDescriptor[8]` | Up to 8 color attachments. |
| `ColorAttachmentCount` | `uint32_t` | Number of active entries in `ColorAttachments`. |
| `DepthAttachment` | `AttachmentDescriptor` | Depth attachment (not yet implemented). |
| `StencilAttachment` | `AttachmentDescriptor` | Stencil attachment (not yet implemented). |
| `RenderArea` | `RenderAreaDescriptor` | Render area (`X`, `Y`, `Width`, `Height`). |
| `LayerCount` | `uint32_t` | Number of array layers. |

**`AttachmentDescriptor`**

| Field | Type | Default | Description |
|---|---|---|---|
| `Texture` | `RHITextureHandle` | Null | Texture to render into. |
| `Load` | `LoadOp` | `DontCare` | `Load`, `Clear`, or `DontCare`. |
| `Store` | `StoreOp` | `Store` | `Store` or `DontCare`. |
| `Clear` | `ClearValue` | — | Clear colour/depth/stencil values. |
| `Layout` | `TextureLayout` | `ColorAttachment` | Expected image layout during the pass. |

---

### Graphics state

```cpp
void RHIDevice::SetGraphicsState(const RHICommandBufferHandle&, const GraphicsStateDescriptor&);
```

Sets all dynamic pipeline state for subsequent draw calls. All fields have defaults, so only the fields you care about need to be specified.

**`GraphicsStateDescriptor`**

| Field | Type | Description |
|---|---|---|
| `InputAssembly` | `InputAssemblyState` | Primitive topology and restart. |
| `Rasterization` | `RasterizationState` | Cull mode, front face, polygon mode, etc. |
| `DepthStencil` | `DepthStencilState` | Depth/stencil test and write enables. |
| `Multisample` | `MultisampleState` | Sample count, sample mask, alpha-to-coverage. |
| `ColorBlend[8]` | `ColorBlendAttachmentState` | Per-attachment blend enable and write mask. |
| `ColorBlendAttachmentCount` | `uint32_t` | Number of active entries in `ColorBlend`. |
| `VertexInput` | `VertexInputState` | Vertex binding and attribute descriptions. |

**`InputAssemblyState`**

| Field | Default | Values |
|---|---|---|
| `Topology` | `TriangleList` | `TriangleList`, `TriangleStrip`, `LineList`, `LineStrip`, `PointList` |
| `PrimitiveRestartEnable` | `false` | |

**`RasterizationState`**

| Field | Default | Values |
|---|---|---|
| `Cull` | `None` | `None`, `Front`, `Back`, `FrontAndBack` |
| `Front` | `CounterClockwise` | `CounterClockwise`, `Clockwise` |
| `Polygon` | `Fill` | `Fill`, `Line`, `Point` |
| `DepthBiasEnable` | `false` | |
| `RasterizerDiscard` | `false` | |

**`DepthStencilState`**

| Field | Default |
|---|---|
| `DepthTestEnable` | `false` |
| `DepthWriteEnable` | `false` |
| `StencilTestEnable` | `false` |

**`MultisampleState`**

| Field | Default |
|---|---|
| `Samples` | `Count1` |
| `SampleMask` | `0xFFFFFFFF` |
| `AlphaToCoverageEnable` | `false` |

**`ColorBlendAttachmentState`**

| Field | Default |
|---|---|
| `BlendEnable` | `false` |
| `ColorWriteMask` | `ColorComponent::All` (RGBA) |

**`VertexInputState`**

Holds up to 16 `VertexInputBindingDescriptor` entries and 16 `VertexInputAttributeDescriptor` entries.

`VertexInputBindingDescriptor`:

| Field | Type | Default |
|---|---|---|
| `Binding` | `uint32_t` | `0` |
| `Stride` | `uint32_t` | `0` |
| `InputRate` | `VertexInputRate` | `Vertex` |

`VertexInputAttributeDescriptor`:

| Field | Type | Default |
|---|---|---|
| `Location` | `uint32_t` | `0` |
| `Binding` | `uint32_t` | `0` |
| `Format` | `VertexFormat` | `Float3` |
| `Offset` | `uint32_t` | `0` |

`VertexFormat` values: `Float1`–`Float4`, `Int1`–`Int4`, `UInt1`–`UInt4`.

---

### Viewport and scissor

```cpp
void RHIDevice::SetViewport(const RHICommandBufferHandle&, const Viewport&);
void RHIDevice::SetScissor(const RHICommandBufferHandle&, const Scissor&);
```

**`Viewport`**: `X`, `Y`, `Width`, `Height` (floats), `MinDepth` (default `0.0`), `MaxDepth` (default `1.0`).

**`Scissor`**: `X`, `Y` (int32), `Width`, `Height` (uint32).

---

### Resource barriers

```cpp
void RHIDevice::TextureResourceBarrier(const RHICommandBufferHandle&, const TextureBarrierDescriptor&);
```

**`TextureBarrierDescriptor`**

| Field | Type | Description |
|---|---|---|
| `Texture` | `RHITextureHandle` | Texture to transition. |
| `OldLayout` | `TextureLayout` | Current layout. |
| `NewLayout` | `TextureLayout` | Target layout. |
| `SrcStage` | `PipelineStage` | Source pipeline stage. |
| `DstStage` | `PipelineStage` | Destination pipeline stage. |
| `SrcAccess` | `Access` | Source access mask. |
| `DstAccess` | `Access` | Destination access mask. |
| `SubresourceRange` | `TextureSubresourceRange` | Mip/layer range (defaults to all). |
| `SrcQueueFamily` | `uint32_t` | Source queue family (default: `QueueFamilyIgnored`). |
| `DstQueueFamily` | `uint32_t` | Destination queue family (default: `QueueFamilyIgnored`). |

**`TextureLayout`**: `Undefined`, `ColorAttachment`, `DepthStencilAttachment`, `ShaderReadOnly`, `TransferSrc`, `TransferDst`, `Present`.

**`PipelineStage`**: `None`, `ColorAttachmentOutput`, `Transfer`, `AllGraphics`, `AllCommands`.

**`Access`**: `None`, `ColorAttachmentRead`, `ColorAttachmentWrite`, `ShaderRead`, `TransferRead`, `TransferWrite`.

---

### Shaders

```cpp
RHIShaderHandle RHIDevice::CreateShader(ShaderFileParams&&   params);
RHIShaderHandle RHIDevice::CreateShader(ShaderSourceParams&& params);

void RHIDevice::BindShader(const RHICommandBufferHandle&, const RHIShaderHandle&);
void RHIDevice::FreeShader(const RHIShaderHandle&);
```

**`ShaderFileParams`** — load GLSL from disk:

| Field | Type | Description |
|---|---|---|
| `Vertex` | `filesystem::path` | Path to vertex shader GLSL source. |
| `Geometry` | `filesystem::path` | Path to geometry shader (optional). |
| `Fragment` | `filesystem::path` | Path to fragment shader GLSL source. |

**`ShaderSourceParams`** — provide GLSL source as strings:

| Field | Type |
|---|---|
| `Vertex` | `std::string` |
| `Geometry` | `std::string` |
| `Fragment` | `std::string` |

Both variants compile GLSL to SPIR-V at runtime via glslang. Returns `RHIShaderHandle::Null()` on compilation failure (errors are logged via spdlog).

`BindShader` binds all shader stages in the program to the command buffer. With the Vulkan backend this calls `vkCmdBindShadersEXT` for each stage.

---

### Draw calls

```cpp
void RHIDevice::Draw(const RHICommandBufferHandle&,
                     uint32_t vertexCount,
                     uint32_t instanceCount,
                     uint32_t firstVertex,
                     uint32_t firstInstance);

void RHIDevice::DrawIndexed(const RHICommandBufferHandle&,
                             uint32_t indexCount,
                             uint32_t instanceCount,
                             uint32_t firstIndex,
                             int32_t  vertexOffset,
                             uint32_t firstInstance);
```

Must be called inside a render pass, after `SetGraphicsState`, `SetViewport`, `SetScissor`, and `BindShader`.

---

### Resource handles

```cpp
// Test validity
bool handle.IsValid() const;

// Obtain a typed null handle
auto null = RHITextureHandle::Null();

// Equality
bool operator==(const RHIHandle<Tag>&, const RHIHandle<Tag>&);
```

Handles become invalid after the corresponding `Free*` call. Passing an invalid or freed handle to any command is a no-op (the device logs a warning in debug builds and skips execution).

---

## Vulkan backend

### Required extensions

| Extension | Purpose |
|---|---|
| `VK_EXT_shader_object` | Bind shader stages independently; no `VkPipeline` needed |
| `VK_KHR_dynamic_rendering` | Begin/end render passes without `VkRenderPass` objects |
| `VK_EXT_vertex_input_dynamic_state` | Set vertex binding/attribute descriptions dynamically per draw |
| `VK_EXT_extended_dynamic_state3` | Set polygon mode, sample count, sample mask, alpha-to-coverage, and blend state dynamically |
| `VK_KHR_swapchain` | Swapchain presentation |

Vulkan 1.3 core (no extension required): `vkCmdSetCullMode`, `vkCmdSetFrontFace`, `vkCmdSetPrimitiveTopology`, `vkCmdSetPrimitiveRestartEnable`, `vkCmdSetDepthTestEnable`, `vkCmdSetDepthWriteEnable`, `vkCmdSetStencilTestEnable`, `vkCmdSetDepthBiasEnable`, `vkCmdSetRasterizerDiscardEnable`, `vkCmdBeginRendering`, `vkCmdEndRendering`, `vkCmdPipelineBarrier2`.

### Shader compilation pipeline

```
GLSL source (string or file)
    │
    ▼  glslang (runtime)
SPIR-V bytecode (vector<uint32_t> per stage)
    │
    ▼  vkCreateShadersEXT
VkShaderEXT (one per stage, stored in RHIVulkanShader)
    │
    ▼  vkCmdBindShadersEXT (at BindShader time)
GPU execution
```

glslang is linked as a static library from the Vulkan SDK. Compilation failures are reported via spdlog and the returned handle is `Null`.

### Memory management

All GPU allocations go through [VulkanMemoryAllocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator). The `VmaAllocator` is owned by `RHIDeviceVulkan` and destroyed on device teardown after all resources have been freed.

### Function loading

[volk](https://github.com/zeux/volk) is used to load all Vulkan entry points at runtime. `volkInitialize()` is called at device construction; `volkLoadInstance` and `volkLoadDevice` are called after instance and device creation respectively. No static linkage to `libvulkan` is required — the compile-time definition `VK_NO_PROTOTYPES` enforces this.

