# CLAUDE.md — ozz_rendering

## Role

Vulkan-based RHI / rendering layer. Provides the `ozz_rendering` static library and the public `ozz_rendering/` header set (RHI device, buffer, texture, shader, descriptors, pipeline state, render pass, barrier, handle, types).

Normally consumed by `Lights` via `FetchContent` (`LOCAL_RENDERING_DIR` overrides the GitHub fetch). The top-level `main.cpp` is a **standalone demo app** (`ozz_rendering_app`) — it is **not** part of the game build path.

## Structure

```
ozz_rendering\
├── CMakeLists.txt              # standalone demo `ozz_rendering_app`
│                               # (FetchContent: glfw master, glm master).
│                               # Adds add_subdirectory(ozz_rendering).
├── main.cpp                    # demo entry point — NOT used by Lights / truck-kun
├── stb_image.h                 # vendored
├── README.md
├── assets\                     # demo assets (textures, etc.)
├── docs\
└── ozz_rendering\              # the actual library (note nested name)
    ├── CMakeLists.txt
    ├── include\ozz_rendering\  # PUBLIC headers (consumers include "ozz_rendering/...")
    │   ├── rhi_device.h
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
    │   ├── vulkan\             # Vulkan backend implementation
    │   │   ├── rhi_device_vulkan.{h,cpp}
    │   │   ├── rhi_buffer_vulkan.{h,cpp}
    │   │   ├── rhi_shader_vulkan.{h,cpp}
    │   │   ├── rhi_texture_vulkan.{h,cpp}
    │   │   ├── vma.cpp         # Vulkan Memory Allocator translation unit
    │   │   └── utils\
    │   └── glslang\
    │       └── resources.cpp   # default GLSL resource limits
    └── third_party\
        └── CMakeLists.txt      # Vulkan SDK + VMA + glslang via FetchContent
```

## Build hygiene

- Build dir: `cmake-build-<variant>-claude`. `.gitignore` covers `cmake-build-*`, `build/`, `dist/`, `.idea/`.
- Standalone build = the demo app only. For end-to-end work, build from `truck-kun/` (which configures Lights, which pulls this in).
- Redirect output to a file; read only on non-zero exit.

## Public RHI surface

The headers under `ozz_rendering/include/ozz_rendering/rhi_*.h` form the abstract device interface. Lights consumes them via:

```cpp
#include "ozz_rendering/rhi_device.h"
#include "ozz_rendering/rhi_pipeline_state.h"
#include "ozz_rendering/utils/enums.h"
```

The Vulkan implementation in `ozz_rendering/src/vulkan/` is the only backend currently shipped.

## Where to look first

| Area | Path |
|------|------|
| Abstract RHI API | `ozz_rendering/include/ozz_rendering/rhi_*.h` |
| Vulkan device | `ozz_rendering/src/vulkan/rhi_device_vulkan.*` |
| Vulkan buffers / textures / shaders | `ozz_rendering/src/vulkan/rhi_{buffer,texture,shader}_vulkan.*` |
| VMA integration | `ozz_rendering/src/vulkan/vma.cpp` |
| GLSL → SPIR-V plumbing | `ozz_rendering/src/glslang/resources.cpp` |
| Enums / handles / types | `ozz_rendering/include/ozz_rendering/utils/enums.h`, `rhi_handle.h`, `rhi_types.h` |
| Demo app | `main.cpp` (root) |
