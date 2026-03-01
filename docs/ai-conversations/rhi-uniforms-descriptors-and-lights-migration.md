# RHI Uniforms / Descriptor Abstraction & lights Migration Plan

## Context

This document covers two related topics discussed while building out `ozz_rendering`:

1. What an abstracted RHI interface for uniforms (descriptors / descriptor sets) should look like
2. How the existing raw-OpenGL rendering layer in `lights/` should be migrated to use the RHI

---

## Part 1: RHI Uniform / Descriptor Abstraction

### The Core Challenge

Different graphics APIs handle resource binding very differently:

- **Vulkan**: Descriptor sets, descriptor set layouts, descriptor pools, pipeline layouts
- **D3D12**: Root signatures, descriptor heaps, CBV/SRV/UAV descriptors
- **Metal**: Argument buffers, direct binding with indices
- **OpenGL**: Uniform locations, UBOs, texture units — one flat global namespace

The goal is a common abstraction that maps cleanly to all backends without leaking API-specific concepts.

### Key Abstraction: `ResourceLayout` + `ResourceSet`

The cleanest approach mirrors Vulkan's mental model (since it's the most explicit), hiding pools, allocation, and layout compatibility details.

```cpp
enum class ResourceType : uint8_t {
    UniformBuffer,
    StorageBuffer,
    Texture2D,
    TextureCube,
    Sampler,
    CombinedTextureSampler,
};

enum class ShaderStage : uint8_t {
    Vertex   = 1 << 0,
    Fragment = 1 << 1,
    Compute  = 1 << 2,
    All      = 0xFF,
};

// Describes the *shape* of a group of resource bindings.
// Vulkan: VkDescriptorSetLayout
// D3D12:  part of root signature
// Metal:  argument buffer layout
struct ResourceLayoutEntry {
    uint32_t     binding;
    ResourceType type;
    ShaderStage  stages;
    uint32_t     count = 1; // for arrays
};

struct ResourceLayoutDesc {
    std::span<const ResourceLayoutEntry> entries;
};

RHIResourceLayoutHandle CreateResourceLayout(const ResourceLayoutDesc& desc);
void DestroyResourceLayout(RHIResourceLayoutHandle layout);

// A concrete, bound instance of resources matching a layout.
// Vulkan: VkDescriptorSet
// D3D12:  descriptor table / root descriptors
// Metal:  MTLArgumentEncoder output
struct ResourceSetDesc {
    RHIResourceLayoutHandle layout;
};

RHIResourceSetHandle CreateResourceSet(const ResourceSetDesc& desc);
void DestroyResourceSet(RHIResourceSetHandle set);

// Update individual bindings (write descriptors)
void WriteBuffer (RHIResourceSetHandle, uint32_t binding, RHIBufferHandle,
                  size_t offset = 0, size_t range = WholeSize);
void WriteTexture(RHIResourceSetHandle, uint32_t binding, RHITextureHandle);
void WriteSampler(RHIResourceSetHandle, uint32_t binding, RHISamplerHandle);

// Bind during command recording, before a draw
void BindResourceSet(RHICommandBufferHandle, RHIResourceSetHandle,
                     uint32_t setIndex,
                     RHIPipelineHandle pipeline); // needed for layout compat

// Small, fast per-draw data.
// Vulkan: vkCmdPushConstants  D3D12: root constants  Metal: setBytes
void PushConstants(RHICommandBufferHandle, const void* data, uint32_t size,
                   uint32_t offset, ShaderStage stages);
```

### Usage Pattern

```cpp
// At startup / material load time:
auto layout = rhi.CreateResourceLayout({
    .entries = {{
        {0, ResourceType::UniformBuffer,          ShaderStage::Vertex},
        {1, ResourceType::CombinedTextureSampler, ShaderStage::Fragment},
    }}
});

auto set = rhi.CreateResourceSet({.layout = layout});
rhi.WriteBuffer (set, 0, transformUBO);
rhi.WriteTexture(set, 1, albedoTexture);

// Per frame:
rhi.UpdateBuffer(transformUBO, &matrices, sizeof(matrices));

// Command recording:
rhi.BindPipeline(cmd, pipeline);
rhi.BindResourceSet(cmd, set, 0, pipeline);
rhi.PushConstants(cmd, &objectID, sizeof(uint32_t), 0, ShaderStage::Vertex);
rhi.Draw(cmd, 36, 1, 0, 0);
```

### Frequency-Based Set Layout (Important Performance Decision)

| Set index | Updates per... | Contents |
|-----------|---------------|----------|
| 0         | Frame         | Camera, time, lights |
| 1         | Pass          | Shadow maps, render targets |
| 2         | Material      | Textures, material params |
| 3         | Draw call     | Model matrix (or use push constants) |

Per-draw data almost always belongs in **push constants** rather than a descriptor set — it avoids descriptor churn entirely.

### What the Backend Hides

- Descriptor pool management and growth
- `VkPipelineLayout` construction from a set of layouts
- Descriptor set caching / dirty tracking
- D3D12 root signature compilation
- Metal argument buffer encoding

### Note on `BindResourceSet` and Pipeline Layout

`BindResourceSet` needs a pipeline handle (or pipeline layout handle) because in Vulkan, `vkCmdBindDescriptorSets` requires the pipeline layout. An alternative is to pass a `PipelineLayoutHandle` separately to decouple binding order from pipeline binding order.

---

## Part 2: OpenGL Mapping

OpenGL has no concept of descriptor sets or layouts. Resources are bound to global slots on the context, and layout is implicit in the shader.

| RHI Concept      | OpenGL equivalent |
|------------------|-------------------|
| `ResourceLayout` | No-op / metadata only. Store entries to know which slots to bind to, create nothing in GL. |
| `ResourceSet`    | A bag of handles. Store `{binding → GLuint}` pairs in a struct. No GPU object created. |
| `WriteBuffer`    | Store the buffer's GL handle + binding index in the set struct. |
| `WriteTexture`   | Store the texture's GL handle + texture unit index. |
| `BindResourceSet`| Does all the work here — iterates bindings and calls `glBindBufferBase`, `glBindTextureUnit`, etc. |
| `PushConstants`  | `glUniform*()` calls or a small UBO at a reserved binding slot. |

### The Flat Namespace Problem

In Vulkan, sets 0–3 have independent binding namespaces. GL has one flat global namespace per resource type. Binding numbers across all sets must not collide.

**Recommended solution: SPIRV-Cross**  
Compile Vulkan SPIR-V to GLSL via SPIRV-Cross, which automatically remaps `(set, binding)` → a single flat GL binding. This is what most modern engines do and is the clean path — the RHI abstraction holds up, translation happens at shader load time.

---

## Part 3: WebGPU

WebGPU maps almost 1:1 to the RHI abstraction:

| RHI Concept        | WebGPU |
|--------------------|--------|
| `ResourceLayout`   | `GPUBindGroupLayout` |
| `ResourceSet`      | `GPUBindGroup` |
| `BindResourceSet`  | `setBindGroup()` |
| `PushConstants`    | Not supported — use a UBO |
| Pipeline layout    | `GPUPipelineLayout` |

### Recommended Backend Targets

| Backend         | Deployment target |
|-----------------|-------------------|
| Vulkan          | Windows, Linux, Android |
| Metal           | macOS, iOS |
| D3D12           | Windows, Xbox |
| WebGPU (Dawn)   | Web + native via Dawn |

WebGL is not worth targeting. WebGL 2 maps to OpenGL ES 3, has all the same abstraction pain, and WebGPU browser support is already solid on Chrome/Edge with Firefox close behind.

**For web deployment:** compile to WASM + WebGPU via Emscripten (`emscripten/webgpu.h`). The RHI's WebGPU backend works both natively (via Dawn) and in the browser.

---

## Part 4: lights/ Migration Plan

### Current State

`lights/engine/src/core/rendering/` uses raw OpenGL (via glad) directly, not going through `ozz_rendering`'s RHI. The files that need migration:

| File | Raw GL used for |
|------|----------------|
| `shader.cpp` | Shader compilation, uniform setting via `glUniform*` |
| `buffer.cpp` | VBO/EBO/VAO, PBO staging ring buffer |
| `texture.cpp` | Texture upload, mipmap generation |
| `material.cpp` | Binding shaders, textures, uniforms, storage buffers |
| `render_target.cpp` | FBO management |
| `renderable_viewport.cpp` | Actual `glDrawElements` call |
| `renderer.cpp` | `glEnable`, blend state |
| `vertex.h` | `glVertexAttribPointer` layout baked in |

### Gap Analysis — RHI Features Needed First

**1. Texture upload API**  
`CreateTexture()` exists but there's no `UploadTextureData(handle, image*)` or `CreateSampler`. The `lights/Texture` + `GPUStagingBuffer` (PBO ring buffer) needs a corresponding RHI path.

**2. Render targets / framebuffers**  
No `CreateRenderTarget` on the RHI. `RenderPassDescriptor` handles load/store ops but doesn't wire up to an offscreen texture yet. `lights/RenderTarget` is entirely FBO-based.

**3. Vertex input layout**  
`IndexVertexBuffer::BindAttribPointers()` bakes the layout into GL. The Vulkan equivalent is `VkPipelineVertexInputStateCreateInfo` inside `GraphicsStateDescriptor`. The `Vertex` struct needs to declare its layout in RHI terms instead.

**4. Resource sets (descriptor sets)**  
`Material` handles: textures at named slots, `uniformSettings` variants (`int`, `vec2`..`mat4`), and `StorageBuffer` bindings. This whole system needs the `ResourceLayout` / `ResourceSet` abstraction described in Part 1 — not yet in the RHI.

**5. StorageBuffer**  
`GL_SHADER_STORAGE_BUFFER` maps to `ResourceType::StorageBuffer` in the descriptor set design.

**6. GPUStagingBuffer (async texture streaming)**  
This is a PBO ring buffer for async texture streaming. In Vulkan this is a `CpuToGpu` staging buffer + `vkCmdCopyBufferToImage` + barrier. No RHI abstraction for this yet — complex enough that it may stay as an engine-internal implementation detail on top of RHI staging buffers.

### Migration Order

```
Phase 1 — RHI surface completion:
  1. RHI: Texture upload API (CreateTexture + UploadTextureData + CreateSampler)
  2. RHI: RenderTarget / offscreen framebuffer (CreateRenderTarget)
  3. RHI: ResourceLayout + ResourceSet (descriptor set design above)

Phase 2 — lights migration (mechanical once Phase 1 is done):
  4. lights: Migrate Shader → RHI CreateShader/BindShader
  5. lights: Migrate Buffer/IndexVertexBuffer → RHI CreateBuffer/BindBuffer/DrawIndexed
  6. lights: Migrate Vertex layout → RHI vertex input description
  7. lights: Migrate Texture → RHI texture + sampler
  8. lights: Migrate Material → ResourceSet (layout + write + bind)
  9. lights: Migrate RenderTarget → RHI CreateRenderTarget
 10. lights: Migrate RenderableViewport → RHI draw calls
 11. lights: Renderer rewrite (scene graph drives RHI frame, not GL context)

Phase 3 — deferred:
 12. lights: GPUStagingBuffer async streaming on top of RHI staging buffers
```

### Notes on the Renderer Rewrite (Step 11)

The current `Renderer::ExecuteSceneGraph` does topological sort → `Render()` per node, where each node does raw GL calls internally. After migration, the frame structure changes to:

```
BeginFrame() → command buffer
  for each renderable in topological order:
    BeginRenderPass(...)
    BindResourceSet(...)
    Draw(...)
    EndRenderPass(...)
SubmitAndPresentFrame()
```

The `RenderTarget` / `Renderable` graph abstraction can survive the migration intact — only the implementation of each node's `render()` changes from GL calls to RHI calls.
