# CLAUDE.md — ozz_rendering

## Role

RHI / rendering layer with Vulkan and WebGPU (Dawn) backends. Provides the `ozz_rendering` static library and the public `ozz_rendering/` header set (RHI device, buffer, texture, shader, descriptors, pipeline state, render pass, barrier, backend, handle, types).

Normally consumed by `Lights` via `FetchContent` (`LOCAL_RENDERING_DIR` overrides the GitHub fetch).

## Structure

```
ozz_rendering\
├── CMakeLists.txt              # thin wrapper: add_subdirectory(ozz_rendering)
├── stb_image.h                 # vendored
├── README.md
├── assets\                     # test assets (textures, etc.)
├── docs\
└── ozz_rendering\              # the actual library (note nested name)
    ├── CMakeLists.txt
    ├── include\ozz_rendering\  # PUBLIC headers (consumers include "ozz_rendering/...")
    │   ├── rhi_device.h
    │   ├── rhi_backend.h       # RHIBackend enum + ResolveBackend()
    │   ├── rhi_buffer.h
    │   ├── rhi_texture.h
    │   ├── rhi_shader.h
    │   ├── rhi_descriptors.h
    │   ├── rhi_pipeline_state.h
    │   ├── rhi_renderpass.h
    │   ├── rhi_barrier.h
    │   ├── rhi_handle.h
    │   ├── rhi_types.h
    │   ├── profiling.h
    │   └── utils\              # enums.h, resource_pool.h
    ├── src\
    │   ├── rhi_device.cpp      # backend factory (CreateRHIDevice / ResolveBackend)
    │   ├── vulkan\             # Vulkan backend implementation
    │   │   ├── rhi_device_vulkan.{h,cpp}
    │   │   ├── rhi_buffer_vulkan.{h,cpp}
    │   │   ├── rhi_shader_vulkan.{h,cpp}
    │   │   ├── rhi_texture_vulkan.{h,cpp}
    │   │   ├── vma.cpp         # Vulkan Memory Allocator translation unit
    │   │   └── utils\
    │   ├── webgpu\             # WebGPU backend (Dawn), gated by OZZ_ENABLE_WEBGPU
    │   │   ├── rhi_device_webgpu.{h,cpp}
    │   │   ├── rhi_shader_webgpu.{h,cpp}
    │   │   ├── rhi_buffer_webgpu.h
    │   │   ├── rhi_texture_webgpu.h
    │   │   └── utils\          # pipeline_cache.h, push_constants.h, rhi_webgpu_types.h
    │   └── glslang\
    │       └── resources.cpp   # default GLSL resource limits
    └── third_party\
        └── CMakeLists.txt      # Vulkan SDK + VMA + glslang (+ Dawn/Slang when enabled)
                                # via FetchContent
```

## Configure flags

- `OZZ_ENABLE_WEBGPU` — build the WebGPU backend via Dawn (implies `OZZ_ENABLE_SLANG`). OFF by default.
- `OZZ_ENABLE_SLANG` — enable the Slang shader compiler (usable by either backend). OFF by default.

## Build hygiene

- Build dir: `cmake-build-<variant>-claude`. `.gitignore` covers `cmake-build-*`, `build/`, `dist/`, `.idea/`.
- For end-to-end work, build from `truck-kun/` (which configures Lights, which pulls this in).
- Redirect output to a file; read only on non-zero exit.

## Public RHI surface

The headers under `ozz_rendering/include/ozz_rendering/rhi_*.h` form the abstract device interface. Lights consumes them via:

```cpp
#include "ozz_rendering/rhi_device.h"
#include "ozz_rendering/rhi_pipeline_state.h"
#include "ozz_rendering/utils/enums.h"
```

Backend selection is via `RHIBackend` (`rhi_backend.h`): Vulkan (default via `Auto`) and WebGPU (requires `OZZ_ENABLE_WEBGPU=ON`).

## Where to look first

| Area | Path |
|------|------|
| Abstract RHI API | `ozz_rendering/include/ozz_rendering/rhi_*.h` |
| Backend factory | `ozz_rendering/src/rhi_device.cpp` |
| Vulkan device | `ozz_rendering/src/vulkan/rhi_device_vulkan.*` |
| Vulkan buffers / textures / shaders | `ozz_rendering/src/vulkan/rhi_{buffer,texture,shader}_vulkan.*` |
| WebGPU device | `ozz_rendering/src/webgpu/rhi_device_webgpu.*` |
| WebGPU shaders (GLSL patching / Slang→WGSL) | `ozz_rendering/src/webgpu/rhi_shader_webgpu.*` |
| VMA integration | `ozz_rendering/src/vulkan/vma.cpp` |
| GLSL → SPIR-V plumbing | `ozz_rendering/src/glslang/resources.cpp` |
| Enums / handles / types | `ozz_rendering/include/ozz_rendering/utils/enums.h`, `rhi_handle.h`, `rhi_types.h` |
