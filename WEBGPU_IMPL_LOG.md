# WebGPU Backend — Implementation Log

Branch: `feature/webgpu-backend` in `ozz_rendering`

## Architecture summary
- Dawn (native WebGPU): `third_party/` via FetchContent (`OZZ_ENABLE_WEBGPU=ON`)
- Slang: runtime GLSL-like → WGSL compiler (WebGPU backend only; Vulkan keeps glslang)
- Backend follows same `RHIDevice` abstract interface as Vulkan
- Pipeline cache: `src/webgpu/utils/pipeline_cache.h` hashes (shader, state, formats) → `WGPURenderPipeline`
- Barriers: no-ops (WebGPU handles sync implicitly)
- Push constants: stub / no-op — use uniform buffers instead (see TODO below)

## Files created
```
ozz_rendering/src/webgpu/
├── rhi_device_webgpu.h/.cpp      main device
├── rhi_buffer_webgpu.h           buffer struct
├── rhi_texture_webgpu.h          texture struct
├── rhi_shader_webgpu.h/.cpp      Slang→WGSL runtime compile
└── utils/
    ├── rhi_webgpu_types.h        RHI→WebGPU enum converters
    └── pipeline_cache.h          FNV hash + pipeline map
```

## Files modified
- `include/ozz_rendering/rhi_device.h` — added `WebGPU` to `RHIBackend` enum
- `src/rhi_device.cpp` — factory handles `RHIBackend::WebGPU`
- `ozz_rendering/CMakeLists.txt` — WebGPU sources under `OZZ_ENABLE_WEBGPU`
- `third_party/CMakeLists.txt` — FetchContent Dawn + Slang under `OZZ_ENABLE_WEBGPU`

## Build flags (add to cmake configure from truck-kun/)
```
-DOZZ_ENABLE_WEBGPU=ON
-DLOCAL_LIGHTS_DIR=D:\Projects\Game\Lights
-DLOCAL_RENDERING_DIR=D:\Projects\Game\ozz_rendering
-DOZZ_WINDOWING_SYSTEM=SDL3
```

## Status: session 4 — integration blockers resolved; ready for truck-kun first-pass build

### Session 4 fixes applied (Lights/truck-kun integration blockers)
- **Push constants (#7)**: GLSL `layout(push_constant) uniform` preprocessed to `layout(set=3, binding=0) uniform`
  before Slang sees the source. Device creates a dedicated 256-byte uniform buffer + bind group at set=3.
  `SetPushConstants` writes to a staging array; `Draw`/`DrawIndexed` upload via `wgpuQueueWriteBuffer`
  and bind at `PushConstantSet=3` before each draw.
- **CombinedImageSampler (#8)**: `CreateDescriptorSetLayout` now emits TWO entries for
  `CombinedImageSampler`: texture at binding N, sampler at binding N+1 (Slang WGSL convention).
  `UpdateDescriptorSet` likewise emits both texture view and sampler entries for
  `CombinedImageSampler` and `SampledImage` writes. Texture's built-in `Sampler` (created at
  `CreateTexture` time for `TextureUsage::Sampled`) is used for the N+1 slot.
- **UpdateTexture bytes-per-row (#10)**: `bytesPerRow` derived from `size / height` instead of
  hardcoded `Width * 4`. Correctly handles non-RGBA textures (RGB, etc.).
- Smoke test still passes clean (exit 0, 0 validation errors) after all three fixes.

### Session 3 fixes applied
- **Shader dedup in compile()**: when `params.Vertex == params.Fragment` (single-file combined shader),
  skip concatenation to avoid duplicate struct/function definitions in Slang.
  Fix in `rhi_shader_webgpu.cpp`: added `&& params.Fragment != params.Vertex` guard.
- **Stencil state on depth-only formats**: `DepthStencilState` defaults have `StencilTestEnable = true`
  and `StencilCompareOp = LessOrEqual`. WebGPU validation rejects stencilFront.compare != Always
  when depth format has no stencil aspect (e.g. D32Float). Fix in `rhi_device_webgpu.cpp`:
  `formatHasStencil()` helper; stencil face state forced to `{Always, Keep, Keep, Keep}` for
  depth-only formats, regardless of `StencilTestEnable`.
- **Smoke test passes**: `ozz_rendering_webgpu_test` — device init, Slang→WGSL compile, 2 frames rendered,
  pipeline cache hit confirmed on frame 2. Exit 0, no validation errors.
- **Vulkan regression confirmed clean**: `ozz_rendering_app` (Vulkan) builds and links without errors.

### Session 2 fixes applied
- **Dawn chromium/6736 API corrections**:
  - Label fields (`WGPUDeviceDescriptor.label`, `WGPUShaderModuleDescriptor.label`,
    `WGPUVertexState.entryPoint`, `WGPUFragmentState.entryPoint`) are `char const*`, not `WGPUStringView`.
    Only `WGPUShaderSourceWGSL.code` uses `WGPUStringView {.data, .length}`.
  - `WGPUErrorCallback` signature: `(WGPUErrorType, char const*, void*)` — no leading `WGPUDevice` param.
  - `WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal` removed; use `_Success` only.
  - `WGPUTextureUsageFlags`/`WGPUBufferUsageFlags`/`WGPUShaderStageFlags`/`WGPUColorWriteMaskFlags`
    don't exist; the types are `WGPUTextureUsage`/`WGPUBufferUsage`/`WGPUShaderStage`/`WGPUColorWriteMask`.
- **Slang v2026.8.1 API rewrite** (`rhi_shader_webgpu.cpp`):
  - Old `sp*` C API removed entirely; replaced with `slang::IGlobalSession`,
    `createSession(SessionDesc{SLANG_WGSL})`, `loadModuleFromSourceString`,
    `findAndCheckEntryPoint`, `createCompositeComponentType`, `link`, `getEntryPointCode`.
  - Reflection rewritten using `ProgramLayout*` / `VariableLayoutReflection` C++ API.
  - `SLANG_RESOURCE_SCALAR` removed; texture detection now uses shape range
    `SLANG_TEXTURE_1D..SLANG_TEXTURE_BUFFER || SLANG_TEXTURE_SUBPASS`.

## Known issues / TODOs

### HIGH — must fix before first render
1. **`wgpuInstanceRequestAdapter` callback signature** — newer Dawn changed `WGPURequestAdapterCallbackInfo`
   vs the older callback style. Check chromium/6736 API and adjust if needed.

2. **`WGPUSType_ShaderSourceWGSL`** — verify this sType exists in chromium/6736 vs older
   `WGPUSType_ShaderModuleWGSLDescriptor`.

3. **Slang entry point index in `spGetEntryPointCodeBlob`** — first call passes `vertEPIdx` (may be 0 or 1
   depending on which `spAddEntryPoint` was called first). Verify ordering.

4. **Slang reflection** — `spReflectionVariableLayout_getOffset` with `SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT`
   / `SLANG_PARAMETER_CATEGORY_REGISTER_SPACE` may not map correctly to WebGPU group/binding.
   Test with a simple uniform buffer shader.

5. **Depth texture aspect in `BeginRenderPass`** — `depthAtt.stencilLoadOp/storeOp` must be
   `WGPULoadOp_Undefined` only if the format has no stencil. D32Float = no stencil (OK). D24S8 = needs stencil ops.

6. **`WGPUSurfaceCapabilities` memory** — `wgpuSurfaceCapabilitiesFreeMembers` must be called;
   already done in `initialize()` — verify this is the right free function in chromium/6736.

### MEDIUM — needed for full feature parity
7. **Push constants** — implement via `wgpuQueueWriteBuffer` to a fixed 256-byte uniform buffer.
   Add push constant bind group layout at set = SetCount in `CreatePipelineLayout`.

8. **`CombinedImageSampler`** — WebGPU separates texture + sampler. When `UpdateDescriptorSet`
   receives a `CombinedImageSampler` write, it only binds the texture view. The sampler needs
   a separate binding. Consider splitting or requiring callers to use separate `SampledImage` + `Sampler`.

9. **Swapchain resize** — currently re-creates depth texture on resize. Pipeline cache entries with
   old `colorFormat`/`depthFormat` are still valid (formats don't change), but the cache holds references
   to the old pipeline — verify no stale depth texture references.

10. **`UpdateTexture` bytes-per-row** — hardcoded `Width * 4` assumes RGBA8. Must derive from format.

### LOW — polish
11. **Geometry shaders** — not supported in WebGPU. `ShaderSourceParams::Geometry` is ignored.
    Add a warning log when a non-empty geometry source is passed.

12. **Multi-attachment blending** — only color attachment 0 is used for blend state. Expand for
    `state.ColorBlendAttachmentCount > 1`.

13. **`wgpuShaderModuleAddRef`** — used in `rhi_shader_webgpu.cpp` when both entry points share
    one module. Verify this function exists in the target Dawn version; fall back to
    `wgpuShaderModuleReference` if not.

## Next session: integration into truck-kun
Wire `RHIBackend::WebGPU` into the game build from `truck-kun/` via `-DOZZ_ENABLE_WEBGPU=ON`.
Remaining known issues before full game rendering (MEDIUM priority):
- Issue #9: swapchain resize re-creation
- Issue #11: geometry shader warning (debug shape renderer uses .geom — add warn + skip)
- Issue #12: multi-attachment blending
