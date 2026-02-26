# RHI Shader Abstractions + Material Layer Placement

## Context

This documents the API design for shader abstractions in the `ozz_rendering` RHI layer and the decision on where `Material` lives.

**Constraints:**
- Backend: Vulkan via `VK_EXT_shader_object` — **no pipeline objects** (`VkPipeline`). Shaders are `VkShaderEXT` objects per stage, bound independently.
- Rendering: `VK_KHR_dynamic_rendering` — **no `VkRenderPass` objects**. Already reflected in the existing `BeginRenderPass`/`EndRenderPass` API which wraps `vkCmdBeginRendering`.
- All graphics state (topology, rasterization, depth/stencil, vertex input, blend) is set dynamically via `vkCmdSetXxx`. Already reflected in `SetGraphicsState(cmd, GraphicsStateDescriptor)`.
- RHI handle pattern (`RHIHandle<Tag>`) already established.

---

## Architectural Decision: Material Lives at the Engine Level

**The RHI layer is the GPU API boundary.** It knows about shaders, buffers, textures, and command encoding — but not about "materials" as a content/engine concept.

**Material is an engine-level object** that composes multiple RHI primitives:
- Which shader stages are bound
- What graphics state to set
- What resources (buffers, textures, samplers) are bound and at which descriptor slots

The RHI has no concept of a material. The engine builds one from RHI primitives.

This mirrors the `VK_EXT_shader_object` philosophy itself: instead of a monolithic pipeline object that bakes everything together, you compose independently-managed pieces.

---

## RHI API: What Needs to Be Added

### 1. Shader Handles

```cpp
// rhi_handle.h — add alongside existing handles
using RHIShaderHandle = RHIHandle<struct ShaderTag>;
```

Note: `RHIShaderHandler` (typo) already exists — rename to `RHIShaderHandle` for consistency.

---

### 2. Shader Creation / Destruction on RHIDevice

```cpp
struct ShaderStageDescriptor {
    ShaderStage       Stage;         // Vertex, Fragment, Compute
    ShaderSourceType  SourceType;    // GLSL or SPIRV
    // One of:
    std::string       GLSLSource;    // glslang compiles → SPIR-V internally
    std::vector<uint32_t> SPIRV;     // pre-compiled SPIR-V
};

enum class ShaderStage { Vertex, Fragment, Geometry, Compute };
enum class ShaderSourceType { GLSL, SPIRV };

// Added to RHIDevice:
virtual RHIShaderHandle CreateShader(const ShaderStageDescriptor&) = 0;
virtual void            DestroyShader(RHIShaderHandle) = 0;
```

**Vulkan mapping:** `vkCreateShadersEXT` — one `VkShaderEXT` per stage. The existing `VulkanShader` in `scratch/` already compiles GLSL → SPIR-V via glslang and creates `VkShaderEXT` objects; that logic moves into the backend implementation.

---

### 3. Binding Shaders (Command Encoding)

```cpp
struct ShaderBindingDescriptor {
    RHIShaderHandle Vertex   {};   // Null() if unused
    RHIShaderHandle Fragment {};   // Null() if unused
    RHIShaderHandle Geometry {};   // Null() if unused
    RHIShaderHandle Compute  {};   // Null() if unused
};

// Added to RHIDevice:
virtual void BindShaders(const RHICommandBufferHandle&, const ShaderBindingDescriptor&) = 0;
```

**Vulkan mapping:** `vkCmdBindShadersEXT(cmd, stageCount, stages[], shaders[])`. Null handles bind `VK_NULL_HANDLE` (unbinding that stage).

---

### 4. Buffer Resources

Shaders need data. Buffer handles and creation are missing from the RHI:

```cpp
// rhi_handle.h
using RHIBufferHandle = RHIHandle<struct BufferTag>;

// rhi_types.h
enum class BufferUsage : uint32_t {
    Vertex        = 1 << 0,
    Index         = 1 << 1,
    Uniform       = 1 << 2,   // UBO
    Storage       = 1 << 3,   // SSBO
    TransferSrc   = 1 << 4,
    TransferDst   = 1 << 5,
};
using BufferUsageFlags = uint32_t;

enum class MemoryAccess {
    GPUOnly,       // device local (fastest for GPU reads)
    CPUToGPU,      // host visible + coherent (for uploads / staging)
    GPUToCPU,      // readback
};

struct BufferDescriptor {
    uint64_t         Size   {0};
    BufferUsageFlags Usage  {0};
    MemoryAccess     Access {MemoryAccess::GPUOnly};
};

// Added to RHIDevice:
virtual RHIBufferHandle CreateBuffer(const BufferDescriptor&) = 0;
virtual void            DestroyBuffer(RHIBufferHandle) = 0;
virtual void*           MapBuffer(RHIBufferHandle) = 0;      // returns CPU pointer; only valid for CPUToGPU/GPUToCPU
virtual void            UnmapBuffer(RHIBufferHandle) = 0;
```

---

### 5. Vertex + Index Buffer Binding

```cpp
// Added to RHIDevice:
virtual void BindVertexBuffer(const RHICommandBufferHandle&,
                               uint32_t       binding,
                               RHIBufferHandle buffer,
                               uint64_t        offset) = 0;

virtual void BindIndexBuffer(const RHICommandBufferHandle&,
                              RHIBufferHandle buffer,
                              uint64_t        offset,
                              IndexType       indexType) = 0;

// rhi_types.h
enum class IndexType { UInt16, UInt32 };
```

---

### 6. Descriptor / Resource Binding

With `VK_EXT_shader_object`, descriptor sets still exist — or push descriptors (`VK_KHR_push_descriptor`) can be used to avoid managing `VkDescriptorPool`. Push descriptors are simpler for a first pass:

```cpp
// rhi_types.h
enum class DescriptorType {
    UniformBuffer,
    StorageBuffer,
    SampledTexture,   // combined image-sampler
    StorageTexture,
};

struct DescriptorBinding {
    uint32_t       Binding {0};
    DescriptorType Type    {};
    // One of:
    RHIBufferHandle  Buffer  {};
    RHITextureHandle Texture {};
    uint64_t         Offset  {0};   // for buffer bindings
    uint64_t         Range   {0};   // for buffer bindings (0 = whole buffer)
};

struct PushDescriptorSetDescriptor {
    uint32_t                          Set      {0};
    std::vector<DescriptorBinding>    Bindings {};
};

// Added to RHIDevice:
virtual void PushDescriptorSet(const RHICommandBufferHandle&,
                                const PushDescriptorSetDescriptor&) = 0;
```

**Vulkan mapping:** `vkCmdPushDescriptorSetKHR`. Avoids `VkDescriptorPool` entirely for the first pass.

---

### 7. Push Constants

For small per-draw data (model matrix, material ID, etc.):

```cpp
// Added to RHIDevice:
virtual void PushConstants(const RHICommandBufferHandle&,
                            ShaderStageFlags stageFlags,
                            uint32_t         offset,
                            uint32_t         size,
                            const void*      data) = 0;

// rhi_types.h
enum class ShaderStageFlag : uint32_t {
    Vertex   = 1 << 0,
    Fragment = 1 << 1,
    Compute  = 1 << 2,
    All      = 0x7,
};
using ShaderStageFlags = uint32_t;
```

---

## Material: Engine-Level API

`Material` does not live in the RHI. It lives in the engine and composes the RHI pieces above:

```
Material owns:
  - RHIShaderHandle (vertex stage)
  - RHIShaderHandle (fragment stage)
  - GraphicsStateDescriptor  (topology, rasterization, depth/stencil, blend, vertex input)
  - PushDescriptorSetDescriptor  (per-material textures, UBOs, SSBOs)

Material::Bind(RHIDevice&, RHICommandBufferHandle):
  1. device.BindShaders(cmd, {vert, frag})
  2. device.SetGraphicsState(cmd, state)     ← already exists
  3. device.PushDescriptorSet(cmd, bindings)
```

`Material` has no Vulkan knowledge. It constructs descriptors from RHI handles. The engine may also hold per-draw push constants separately (a model matrix is not part of the material; it's per-draw).

---

## What Already Exists (No Changes Needed)

| Already in RHI | Notes |
|---|---|
| `SetGraphicsState(cmd, GraphicsStateDescriptor)` | Covers all dynamic state: topology, raster, depth/stencil, multisample, blend, vertex input |
| `BeginRenderPass` / `EndRenderPass` | Maps to `vkCmdBeginRendering` / `vkCmdEndRendering` — dynamic rendering |
| `TextureResourceBarrier` | Layout transitions |
| `SetViewport` / `SetScissor` | |
| `Draw` / `DrawIndexed` | |
| `CreateTexture` / `RHITextureHandle` | Texture resource (image + imageview) |
| `RHIHandle<Tag>` pattern | Generational handles with resource pools |
| `FrameContext` (BeginFrame / SubmitAndPresentFrame) | Frame lifecycle |

---

## Summary of Additions to RHI

| Addition | Reason |
|---|---|
| `CreateShader` / `DestroyShader` | Load GLSL/SPIR-V per stage into `VkShaderEXT` |
| `BindShaders` | `vkCmdBindShadersEXT` per-frame encoding |
| `CreateBuffer` / `DestroyBuffer` / `MapBuffer` / `UnmapBuffer` | Vertex, index, UBO, SSBO resources |
| `BindVertexBuffer` / `BindIndexBuffer` | Geometry binding |
| `PushDescriptorSet` | Resource binding without descriptor pool management |
| `PushConstants` | Lightweight per-draw data (model matrix, etc.) |

---

## Deferred / Out of Scope

- Full `VkDescriptorPool` + `VkDescriptorSet` management (push descriptors cover first pass)
- Sampler objects as first-class RHI handles (combined image-sampler via texture for now)
- Compute pipeline / `DispatchCompute`
- Material instancing
- Shader reflection / auto-descriptor-layout generation
- Ray tracing stages
