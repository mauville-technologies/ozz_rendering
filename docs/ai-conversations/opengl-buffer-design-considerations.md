# OpenGL Buffer Design Considerations

## Context

Analysis of buffer types in the `lights/` OpenGL codebase against the ozz_rendering RHI, ahead of adding an OpenGL backend. The goal is to avoid the structural problems that emerged in `lights/` by building a clean buffer abstraction into the RHI from the start.

---

## Key Issues in `lights/` to Avoid

### 1. Buffer Re-creation on Upload

`Buffer::UploadData(..., replace=true)` deletes and recreates the GL buffer object every time. This causes implicit GPU stalls — the driver has to wait for the GPU to finish using the old buffer before destroying it.

**For ozz_rendering:** Use `glNamedBufferStorage` once at allocation time (immutable storage), and use **buffer orphaning** (`glBufferData(target, size, nullptr, usage)` then sub-data) or allocate a new name and swap if you truly need resize semantics. Better: size buffers for the max you'll need and use `glNamedBufferSubData` for updates.

### 2. No Buffer Abstraction in the RHI

The `lights/` codebase has `Buffer`, `IndexVertexBuffer`, `StorageBuffer<T,N>`, and `GPUStagingBuffer` all as separate, unrelated classes. There's no unified concept of "a GPU buffer with a usage flag." The RHI in ozz_rendering already has `RHITextureHandle` + `ResourcePool` — a `RHIBufferHandle` should be introduced as a first-class citizen on the same pattern.

### 3. Compile-Time Buffer Sizes (`StorageBuffer<T, N>`)

The template approach forces element count to be known at compile time. This is inflexible for dynamic scenes.

**For ozz_rendering:** Accept `SizeBytes` at runtime in the descriptor, not as a template parameter.

### 4. Missing Bind/Upload API Separation

In `lights/`, binding and uploading are often tangled (e.g., `UploadData` calling `Bind` internally). The RHI needs a clean separation:
- `UpdateBuffer(...)` — writes data, no binding side effect
- Binding is implicit via pipeline state / descriptor sets or explicit binding points managed by the device

### 5. Synchronization is an Afterthought

`lights/` uses `GL_MAP_COHERENT_BIT` for the staging buffer, which hides sync costs. For a proper RHI staging path:
- The OpenGL backend should emulate Vulkan's "transfer queue" concept using `GL_MAP_PERSISTENT_BIT` + explicit `glMemoryBarrier` or `glFenceSync`
- Expose `UpdateBuffer` on the command buffer interface (not as a separate ad-hoc staging class), so the Vulkan backend can use real transfer queues and OpenGL can use the PBO trick — both behind the same call

### 6. VAO Ownership

`IndexVertexBuffer` tightly couples the VAO to the mesh. In OpenGL 4.5+, VAOs describe vertex layout and can be reused across many meshes. The existing `VertexInputState` inside `GraphicsStateDescriptor` is the right place for this description. The OpenGL backend should cache one VAO per unique `VertexInputState` hash, treating it as pipeline state rather than per-mesh data.

### 7. Raw Pointer Buffer Bindings in `Material`

`Material::StorageBufferBindings` stores a raw `StorageBufferBase*` with no lifetime tracking. In the RHI, the `RHIBufferHandle` pattern already solves this — binding points should hold handles, not pointers.

---

## Proposed RHI Buffer Interface

### `RHIBufferDescriptor`

```cpp
enum class BufferUsage : uint32_t {
    None    = 0,
    Vertex  = 1 << 0,  // used as vertex input
    Index   = 1 << 1,  // used as index input
    Uniform = 1 << 2,  // used as a uniform / constant buffer
    Storage = 1 << 3,  // used as a shader storage / UAV buffer
    Staging = 1 << 4,  // CPU-side upload / readback buffer
};

enum class MemoryAccess {
    GpuOnly,   // Device-local; fastest for GPU reads, not CPU-visible
    CpuToGpu,  // Host-visible, write-combined; ideal for per-frame uploads
    GpuToCpu,  // Host-visible, cached; for GPU readbacks
};

struct RHIBufferDescriptor {
    size_t       SizeBytes   {0};
    BufferUsage  Usage       {BufferUsage::None};
    MemoryAccess Access      {MemoryAccess::GpuOnly};
};
```

`BufferUsage` is a bitmask so a buffer can serve multiple roles (e.g., `Vertex | Storage` for a compute-written vertex buffer). `MemoryAccess` drives how the backend allocates memory:

| `MemoryAccess` | Vulkan | OpenGL |
|---|---|---|
| `GpuOnly` | `VMA_MEMORY_USAGE_GPU_ONLY` | `glNamedBufferStorage` with no map flags |
| `CpuToGpu` | `VMA_MEMORY_USAGE_CPU_TO_GPU` | `GL_MAP_WRITE_BIT \| GL_MAP_PERSISTENT_BIT` |
| `GpuToCpu` | `VMA_MEMORY_USAGE_GPU_TO_CPU` | `GL_MAP_READ_BIT \| GL_MAP_PERSISTENT_BIT` |

### New `RHIDevice` Methods

```cpp
// Resource creation / destruction
virtual RHIBufferHandle CreateBuffer(const RHIBufferDescriptor&) = 0;
virtual void            DestroyBuffer(RHIBufferHandle) = 0;

// Data transfer (recorded into a command buffer)
virtual void UpdateBuffer(const RHICommandBufferHandle&,
                          RHIBufferHandle dst,
                          size_t dstOffset,
                          size_t size,
                          const void* data) = 0;

// Vertex / index binding (recorded into a command buffer)
virtual void BindVertexBuffer(const RHICommandBufferHandle&,
                              uint32_t slot,
                              RHIBufferHandle,
                              size_t offset) = 0;

virtual void BindIndexBuffer(const RHICommandBufferHandle&,
                             RHIBufferHandle,
                             IndexType,
                             size_t offset) = 0;
```

For OpenGL these map to `glVertexArrayVertexBuffer` / `glVertexArrayElementBuffer` on a cached VAO. For Vulkan they map to `vkCmdBindVertexBuffers` / `vkCmdBindIndexBuffer`.

---

## Summary

| `lights/` problem | ozz_rendering recommendation |
|---|---|
| Buffer delete+recreate on resize | Immutable storage; orphan or swap buffer names when resize is needed |
| No unified buffer type | `RHIBufferHandle` + `RHIBufferDescriptor` with `Usage` and `Access` |
| `StorageBuffer<T,N>` compile-time size | Runtime `SizeBytes` in descriptor |
| Staging as a separate ad-hoc class | `UpdateBuffer` on the command buffer; backend picks the mechanism |
| VAO tightly coupled to mesh | Cached `RHIVertexLayout` derived from `VertexInputState` in pipeline state |
| Raw pointer buffer bindings | Handles everywhere; lifetime tracked via `ResourcePool` generational IDs |

The existing handle/pool pattern and descriptor structs in ozz_rendering are the right foundation — the main gap is adding `RHIBufferHandle` as a first-class citizen alongside `RHITextureHandle`.
