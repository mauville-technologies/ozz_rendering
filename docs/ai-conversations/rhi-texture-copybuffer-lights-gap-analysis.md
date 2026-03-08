# RHI Texture / Copy Infrastructure & lights/ Migration Gap Analysis

## Context

This document captures the gap analysis between the current `ozz_rendering` RHI abstraction
and the features required to port `lights/` (an OpenGL 4.1 renderer) to use the new RHI.
It also covers the mechanics of GPU copy operations and their synchronization requirements
in Vulkan.

---

## Gap Summary

Features in `lights/` are grouped by priority:

| Priority | Feature                                         | lights/ Usage                          | RHI Status                       |
|----------|-------------------------------------------------|----------------------------------------|----------------------------------|
| P0       | One-time submit infrastructure                  | Implicit in every GL upload            | Missing entirely                 |
| P0       | `CreateTexture(TextureDescriptor)`              | 2D textures, RGBA8/RGB8/R8             | Stub returns Null                |
| P0       | `UpdateTexture`                                 | `glTexSubImage2D` uploads              | No API                           |
| P0       | `FreeTexture`                                   | `glDeleteTextures`                     | No API                           |
| P0       | `CombinedImageSampler` descriptor write         | `uniform sampler2D` in shaders         | Logs error, unimplemented        |
| P0       | Depth attachment in render pass                 | `GL_DEPTH_BUFFER_BIT`, depth testing   | Asserts false                    |
| P1       | Render-to-texture (FBO)                         | `RenderTarget` system                  | Falls out of `CreateTexture`     |
| P1       | `BufferMemoryBarrier`                           | SSBO read-after-write                  | Asserts false                    |
| P1       | `ShaderStages` / `ShaderWrite` in barrier enums | Post-upload transitions                | Missing enum values              |
| P2       | Blend src/dst factors                           | `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` | Only enable/disable              |
| P2       | Line width / point size                         | `glLineWidth`, `glPointSize`           | Not in `GraphicsStateDescriptor` |

---

## The Copy Problem: Why Uploads Are More Complex in Vulkan

In OpenGL, uploading data to the GPU is transparent — `glTexSubImage2D` and
`glNamedBufferSubData` handle the entire staging/copy pipeline internally. The driver
manages all synchronization. Vulkan makes this explicit.

### GPU Memory Types

Vulkan (via VMA) exposes three relevant memory archetypes:

| Memory Type | VMA Flag                      | CPU Access     | GPU Access | Use Case                                  |
|-------------|-------------------------------|----------------|------------|-------------------------------------------|
| `GpuOnly`   | `VMA_MEMORY_USAGE_GPU_ONLY`   | ❌ (no mapping) | Fast       | Static geometry, textures, render targets |
| `CpuToGpu`  | `VMA_MEMORY_USAGE_CPU_TO_GPU` | ✅ mapped       | Slower     | Uniform buffers, per-frame data           |
| `GpuToCpu`  | `VMA_MEMORY_USAGE_GPU_TO_CPU` | ✅ mapped       | Slower     | Readback                                  |

**The key rule**: images (`VkImage`) in Vulkan cannot be in `CpuToGpu` memory — they are
always `GpuOnly`. There is no equivalent to `glTexSubImage2D` that writes directly from
CPU to image memory. You must always go through a staging buffer.

### The Staging Buffer Pattern

For any upload to a `GpuOnly` resource (textures, GPU-only buffers), the process is:

```
[CPU data]
    │
    ▼ memcpy
[Staging buffer]          ← CpuToGpu VkBuffer (temporary)
    │
    ▼ vkCmdCopyBufferToImage / vkCmdCopyBuffer
[GPU resource]            ← GpuOnly VkImage or VkBuffer
```

This copy must be recorded into a command buffer and submitted to the GPU.

### Synchronization: Fences vs Semaphores

Vulkan has two primary synchronization primitives:

**Semaphores** (`VkSemaphore`) — GPU-to-GPU synchronization:

- Signal on one queue, wait on another queue (or same queue, different submit)
- Used for: swapchain image acquisition → rendering, rendering → presentation
- CPU cannot wait on a semaphore directly
- Zero overhead if not used (they don't stall the GPU)

**Fences** (`VkFence`) — GPU-to-CPU synchronization:

- Signal when GPU finishes a submission
- CPU waits via `vkWaitForFences`
- Used for: "tell me when this command buffer has finished executing"

For upload operations:

```
CPU: fill staging buffer with data
CPU: submit copy command buffer with fence F
CPU: vkWaitForFences(F)           ← blocks until copy is done on GPU
CPU: free staging buffer          ← now safe, GPU is done reading from it
```

A semaphore would be wrong here — the CPU needs to know when the copy is done so it can
free the staging buffer. Semaphores only coordinate GPU-side queues.

### ImmediateSubmit Helper

Since upload operations happen outside the normal frame loop (at load time, not per-frame),
the RHI needs an internal helper for one-shot command buffer submission. This does NOT
become part of the public `RHIDevice` API — it's an internal implementation detail of
`RHIDeviceVulkan`:

```cpp
// Internal to RHIDeviceVulkan — not on the RHIDevice interface
void RHIDeviceVulkan::ImmediateSubmit(std::function<void(VkCommandBuffer)>&& fn) {
    // 1. Allocate a one-time command buffer from commandBufferPool
    // 2. Begin with VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    // 3. Call fn(cmd) — caller records the copy/barrier commands
    // 4. End the command buffer
    // 5. Submit to graphicsQueue with a local fence
    // 6. vkWaitForFences — blocks CPU until complete
    // 7. Free command buffer
    // (caller frees the staging buffer after return)
}
```

**Why not reuse the frame command buffer?**
The frame command buffer is between `BeginFrame` and `SubmitAndPresentFrame`. Upload
operations happen at setup time (before the render loop), so there is no active frame
context. Even if called during a frame, mixing upload and render commands in the same
submission complicates synchronization. A dedicated one-shot submission keeps concerns
separated.

### Barriers Around Copy Operations

Every copy to a `GpuOnly` resource requires layout/access transitions. For a texture:

```
Before copy:
  vkCmdPipelineBarrier2
    Undefined → TransferDst
    srcStage: TOP_OF_PIPE,  dstStage: TRANSFER
    srcAccess: NONE,        dstAccess: TRANSFER_WRITE

  vkCmdCopyBufferToImage(staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ...)

After copy (if the texture will be sampled):
  vkCmdPipelineBarrier2
    TransferDst → ShaderReadOnly
    srcStage: TRANSFER,             dstStage: FRAGMENT_SHADER
    srcAccess: TRANSFER_WRITE,      dstAccess: SHADER_READ
```

For a GPU-only `VkBuffer` (not currently used in the RHI but the pattern is analogous):

```
  vkCmdCopyBuffer(staging, dstBuffer, ...)

  vkCmdPipelineBarrier2
    srcStage: TRANSFER,       dstStage: VERTEX_INPUT (or appropriate stage)
    srcAccess: TRANSFER_WRITE, dstAccess: VERTEX_ATTRIBUTE_READ (or SHADER_READ)
```

Image layout transitions and buffer access transitions both go through
`vkCmdPipelineBarrier2` (the Vulkan 1.3 synchronization2 API), which `TextureResourceBarrier`
and `BufferMemoryBarrier` on the RHI map to.

### Mipmap Generation

If `SamplerDescriptor::GenerateMipmaps = true`, mipmap generation happens after the upload:

```
After copy barrier (TransferDst → TransferSrc for mip 0):
  for each mip level:
    vkCmdBlitImage (mip N → mip N+1)
    barrier: TRANSFER_WRITE → TRANSFER_READ (for src mip)
Final barrier: TransferSrc → ShaderReadOnly (all mips)
```

This all happens inside `ImmediateSubmit` as part of `UpdateTexture`.

---

## Required API Additions

### New Types (rhi_types.h or new rhi_texture.h)

```cpp
enum class TextureFormat {
    RGBA8, RGB8, R8, BGRA8,       // color
    D32Float, D24S8,               // depth / depth-stencil
};

enum class TextureUsage : uint8_t {
    Sampled         = 1 << 0,      // VK_IMAGE_USAGE_SAMPLED_BIT
    ColorAttachment = 1 << 1,      // VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    DepthAttachment = 1 << 2,      // VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
    TransferSrc     = 1 << 3,      // VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    TransferDst     = 1 << 4,      // VK_IMAGE_USAGE_TRANSFER_DST_BIT
};

enum class TextureFilter { Linear, Nearest };
enum class TextureWrap   { Repeat, ClampToEdge, ClampToBorder };

struct SamplerDescriptor {
    TextureFilter MinFilter       { TextureFilter::Linear };
    TextureFilter MagFilter       { TextureFilter::Linear };
    TextureWrap   WrapU           { TextureWrap::Repeat   };
    TextureWrap   WrapV           { TextureWrap::Repeat   };
    bool          GenerateMipmaps { false };
};

struct TextureDescriptor {
    uint32_t          Width   { 1 };
    uint32_t          Height  { 1 };
    TextureFormat     Format  { TextureFormat::RGBA8 };
    TextureUsage      Usage   { TextureUsage::Sampled };
    SamplerDescriptor Sampler {};
};
```

`TextureUsage` always implicitly includes `TransferDst` when `UpdateTexture` is called
(required for the staging copy). `Sampled` textures also need `TransferDst` at creation
if they will ever be uploaded to.

### New RHIDevice Methods

```cpp
// Replace the stubbed CreateTexture() with:
virtual RHITextureHandle CreateTexture(TextureDescriptor&& descriptor) = 0;
virtual void UpdateTexture(RHITextureHandle handle, const void* data, size_t size) = 0;
virtual void FreeTexture(RHITextureHandle handle) = 0;
```

### Vulkan Texture Resource (RHITextureVulkan)

The Vulkan texture type needs to store the sampler alongside the image:

```cpp
struct RHITextureVulkan {
    VkImage        Image      { VK_NULL_HANDLE };
    VkImageView    ImageView  { VK_NULL_HANDLE };
    VkSampler      Sampler    { VK_NULL_HANDLE };   // NEW — needed for descriptor writes
    VmaAllocation  Allocation { VK_NULL_HANDLE };   // null for swapchain images
    VkFormat       Format     { VK_FORMAT_UNDEFINED };
    uint32_t       Width      { 0 };
    uint32_t       Height     { 0 };
    uint32_t       MipLevels  { 1 };
};
```

The sampler is created and destroyed alongside the image so no separate
`RHISamplerHandle` is needed for the common case.

### CombinedImageSampler Descriptor Write

`UpdateDescriptorSet` needs an image path in addition to the existing buffer path:

```cpp
// In RHIDeviceVulkan::UpdateDescriptorSet, for CombinedImageSampler:
const auto* texture = texturePool.Get(write.Image.Texture);
VkDescriptorImageInfo imageInfo {
    .sampler     = texture->Sampler,
    .imageView   = texture->ImageView,
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
};
vkWrite.pImageInfo = &imageInfo;
```

### Depth Attachment Fix (BeginRenderPass)

The current assert needs to be replaced with real attachment info:

```cpp
// For depth, build:
VkRenderingAttachmentInfo depthAttachmentInfo {
    .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView   = depthTexture->ImageView,
    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    .loadOp      = ConvertLoadOpToVulkan(renderPassDescriptor.DepthAttachment.Load),
    .storeOp     = ConvertStoreOpToVulkan(renderPassDescriptor.DepthAttachment.Store),
    .clearValue  = { .depthStencil = { .depth = 1.0f, .stencil = 0 } },
};
renderingInfo.pDepthAttachment = &depthAttachmentInfo;
```

Depth textures must be transitioned to `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`
before use (typically during texture creation via `ImmediateSubmit`).

### PipelineStage / Access Enum Additions

```cpp
// PipelineStage additions:
VertexShader,
FragmentShader,      // or merge into ShaderStages

// Access additions:
ShaderWrite,         // for SSBO writes in shaders
DepthAttachmentRead,
DepthAttachmentWrite,
```

### Blend Factor Support (P2)

```cpp
enum class BlendFactor {
    Zero, One,
    SrcAlpha, OneMinusSrcAlpha,
    DstAlpha, OneMinusDstAlpha,
    SrcColor, OneMinusSrcColor,
};

struct ColorBlendAttachmentState {
    bool        BlendEnable        { false };
    BlendFactor SrcColorFactor     { BlendFactor::SrcAlpha };
    BlendFactor DstColorFactor     { BlendFactor::OneMinusSrcAlpha };
    BlendFactor SrcAlphaFactor     { BlendFactor::One };
    BlendFactor DstAlphaFactor     { BlendFactor::Zero };
    ColorComponentFlags ColorWriteMask { ColorComponent::All };
};
```

---

## Implementation Order

1. `ImmediateSubmit` internal helper in `RHIDeviceVulkan`
2. `CreateTexture(TextureDescriptor)` — vkCreateImage + VkImageView + VkSampler
3. `UpdateTexture` — staging buffer + `ImmediateSubmit` + layout barriers + optional mip generation
4. `FreeTexture`
5. `CombinedImageSampler` write in `UpdateDescriptorSet`
6. Depth attachment in `BeginRenderPass` (remove assert, wire depth `VkRenderingAttachmentInfo`)
7. `BufferMemoryBarrier` implementation (remove assert, vkCmdPipelineBarrier2)
8. `PipelineStage` / `Access` enum additions for shader stages
9. `BlendFactor` enum + fields in `ColorBlendAttachmentState`
10. `LineWidth` / `PointSize` in `RasterizationState`
