# Entity Renderer Uniform Handling — Vulkan & RHI Plan

## Context

The entity renderer in the codebase sets per-draw uniforms via a material abstraction that ultimately calls glUniform* by name. Typical per-entity data observed:

- `model` (mat4) — entity world transform (per-draw)
- `view` (mat4) — camera view matrix (per-frame / per-pass)
- `projection` (mat4) — camera projection matrix (per-frame / per-pass)
- `uvOffset`, `uvScale` (vec2) — spritesheet animation (per-draw)

The current approach uses string-name uniform lookups and per-draw glUniform calls; this needs different handling in Vulkan and should be surfaced cleanly in the RHI.

---

## Mapping to Vulkan primitives

- Push Constants — for small, hot per-draw data (fastest): `model`, `uvOffset`, `uvScale` fit well as push constants. Typical size: model(64 bytes) + uvOffset/uvScale(16 bytes) = 80 bytes (fits typical 128-byte limits, but query `maxPushConstantsSize`).

  GLSL example:
  ```glsl
  layout(push_constant) uniform PushConstants {
      mat4 model;
      vec2 uvOffset;
      vec2 uvScale;
  } pc;
  ```

  CPU: record into command buffer with a `SetPushConstants` call.

- Uniform Buffer Objects (UBOs) — for per-frame or shared data: `view` and `projection` belong in a UBO that is updated once per frame and bound once per pass.

  GLSL example:
  ```glsl
  layout(set = 0, binding = 0) uniform SceneUBO {
      mat4 view;
      mat4 projection;
  } scene;
  ```

  CPU: create a `RHIBuffer` (Usage: Uniform, Access: CpuToGpu), update once per frame, bind descriptor set containing that buffer.

- Textures and storage buffers map to descriptor sets (combined image samplers and SSBOs respectively).

---

## RHI API recommendations

Add small, explicit methods to the RHIDevice / command buffer API to cover these patterns (examples only):

```cpp
// Create / update buffers (already suggested elsewhere)
virtual RHIBufferHandle CreateBuffer(const RHIBufferDescriptor&) = 0;
virtual void UpdateBuffer(const RHICommandBufferHandle&, RHIBufferHandle dst, size_t dstOffset, size_t size, const void* data) = 0;

// Bind a UBO/SSBO to a descriptor slot (set/binding semantics)
virtual void BindUniformBuffer(const RHICommandBufferHandle&, uint32_t set, uint32_t binding, RHIBufferHandle) = 0;

// Push small per-draw data (push constants)
virtual void SetPushConstants(const RHICommandBufferHandle&, ShaderStage stages, uint32_t offset, uint32_t size, const void* data) = 0;
```

Notes:
- `SetPushConstants` should be the canonical per-draw path for small matrices and frequently-updated per-object data.
- If push constant limits are insufficient, fall back to a dynamic UBO per-draw (use a CpuToGpu ring allocator with dynamic offsets).
- `BindUniformBuffer` expresses the concept of binding a buffer into a descriptor set layout; higher-level `BindDescriptorSet` APIs may already exist and should be used where appropriate.

---

## OpenGL backend mapping

- `SetPushConstants` -> `glUniform*` (or program-specific `glProgramUniform*`) for per-draw small data. Prefer avoiding repeated `glGetUniformLocation` by caching locations or using a small UBO if many locations are used.
- `BindUniformBuffer` -> `glBindBufferBase(GL_UNIFORM_BUFFER, binding, handle)` and the shader uses `layout(std140, binding = N)`.
- Descriptor sets in Vulkan roughly map to grouped UBO / sampler bindings in OpenGL (binding points + texture units).

---

## Layout & alignment considerations

- Push constants: query `VkPhysicalDeviceLimits::maxPushConstantsSize` and ensure alignment rules are respected.
- UBOs: follow `std140` (or `std430` when supported) layout rules — pad to vec4 boundaries, place mat4 as array of vec4s.
- For portability, prefer `std140` for Scene UBOs and structure your structs to avoid unexpected padding.

---

## Migration checklist (practical steps)

1. Add RHI primitives for buffers and push-constants (if not already present): `RHIBufferHandle`, `RHIBufferDescriptor`, `SetPushConstants`, `BindUniformBuffer`, `UpdateBuffer`.
2. Create a per-frame `SceneUBO` buffer (CpuToGpu Uniform) and update it once per frame with `view` and `projection`. Bind it at set 0 binding 0.
3. Replace per-entity `Material::AddUniformSetting("model", ...)` uses with either:
   - A `PushConstants` structure populated by the renderer before each draw; or
   - A per-draw dynamic UBO slot (if push constant limits are insufficient).
4. Material should continue to own textures/samplers as descriptor-set bindings; update Material to expose or create a pre-bound descriptor set for the material's textures and SSBOs.
5. Update the entity renderer code path:
   - Bind pipeline and scene descriptor set (scene UBO)
   - For each entity: prepare push constant struct and call `SetPushConstants(cmd, stages, 0, sizeof(push), &push)`; bind material descriptor set; bind vertex/index buffers; issue draw.

Example renderer pseudocode:

```cpp
struct PushData { glm::mat4 model; glm::vec2 uvOffset; glm::vec2 uvScale; } push;
push.model = ...; push.uvOffset = ...; push.uvScale = ...;

device->BindDescriptorSet(cmd, 0, sceneDescriptorSet);
device->BindDescriptorSet(cmd, 1, materialDescriptorSet);

device->SetPushConstants(cmd, ShaderStage::Vertex, 0, sizeof(push), &push);
mesh->Bind(cmd);
device->DrawIndexed(cmd, meshIndexCount, 0, 0);
```

---

## Rationale / benefits

- Push constants avoid per-draw buffer binds and are the cheapest per-draw path in Vulkan.
- UBOs keep per-frame data in one place and minimize redundant updates.
- Moving away from string-based glUniform lookups removes a whole class of backend-specific code and makes the renderer backend-agnostic.
- Descriptor sets make texture / SSBO binding explicit and efficient for both Vulkan and OpenGL backends.

---

## Notes & fallback considerations

- Some hardware/drivers may limit push-constant size; detect and fallback to dynamic UBOs.
- For very large per-object data (rare), prefer SSBOs with an index into a storage buffer instead of pushing or using UBOs per-draw.

---

This plan documents the mapping from the current glUniform-based pattern to a Vulkan-friendly design and the small RHI additions required to make entity rendering efficient and backend-agnostic.