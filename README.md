# ozz_rendering

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/mauville-technologies/ozz_rendering)

A reusable C++23 RHI (Render Hardware Interface) library that provides a clean, handle-based GPU API. The current
backend is **Vulkan**; an **OpenGL** backend is planned.

The API is intentionally backend-agnostic — calling code never touches Vulkan types directly. The same render loop will
work across all supported backends.

---

## Design highlights

- **No `VkPipeline` objects** — shaders are bound via `VK_EXT_shader_object` (`VkShaderEXT` per stage)
- **No `VkRenderPass` objects** — dynamic rendering via `VK_KHR_dynamic_rendering`
- **Fully dynamic graphics state** — topology, rasterization, depth/stencil, vertex input, and blending are all set
  per-draw via `vkCmdSetXxx` (`VK_EXT_extended_dynamic_state3`, `VK_EXT_vertex_input_dynamic_state`)
- **Generational resource handles** — all GPU resources are identified by opaque `RHIHandle<Tag>` values; no raw Vulkan
  handles leak through the public API
- **Runtime GLSL compilation** — shaders are compiled from GLSL source (or files) to SPIR-V at runtime via glslang
- **VMA** for GPU memory allocation
- **volk** for Vulkan function loading (zero static Vulkan linkage)

---

## Requirements

| Requirement  | Minimum                                                                                                      |
|--------------|--------------------------------------------------------------------------------------------------------------|
| C++ standard | C++23                                                                                                        |
| CMake        | 3.20                                                                                                         |
| Vulkan SDK   | 1.4 (tested with 1.4.341)                                                                                    |
| GPU          | Vulkan 1.3 + `VK_EXT_shader_object` + `VK_EXT_vertex_input_dynamic_state` + `VK_EXT_extended_dynamic_state3` |

All other dependencies (volk, VMA, spdlog, glslang) are fetched automatically by CMake via `FetchContent`.

---

## Building the sample app

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The compiled sample binary is placed under `dist/<OS>/<BuildType>/ozz_rendering_app/`.

---

## Integrating ozz_rendering as a library

`ozz_rendering` is a static library. Add it to your project with `add_subdirectory`:

```cmake
add_subdirectory(ozz_rendering)

target_link_libraries(my_app
        PRIVATE
        ozz_rendering
)
```

Public headers are under `ozz_rendering/include/`. Include the main header:

```cpp
#include <ozz_rendering/rhi.h>
```

---

## Quick start

```cpp
#include <ozz_rendering/rhi.h>

// 1. Create the device (platform context provided by your window layer)
auto device = OZZ::rendering::CreateRHIDevice({
    .Backend = OZZ::rendering::RHIBackend::Auto,
    .Context = {
        .AppName    = "My App",
        .AppVersion = {0, 1, 0, 0},
        .WindowHandle              = window,
        .RequiredInstanceExtensions = requiredExtensions,
        .GetWindowFramebufferSizeFunction = [&]() {
            return std::make_pair(width, height);
        },
        .CreateSurfaceFunction = [&](void* instance, void* surface) {
            return glfwCreateWindowSurface(
                static_cast<VkInstance>(instance), window, nullptr,
                static_cast<VkSurfaceKHR*>(surface)) == VK_SUCCESS;
        },
    },
});

// 2. Create a shader
auto shader = device->CreateShader(OZZ::rendering::ShaderFileParams {
    .Vertex   = "shaders/basic.vert",
    .Fragment = "shaders/basic.frag",
});

// 3. Render loop
while (running) {
    auto frame = device->BeginFrame();

    device->BeginRenderPass(frame.GetCommandBuffer(), renderPassDesc);

    device->SetGraphicsState(frame.GetCommandBuffer(), {
        .ColorBlend              = {{ .BlendEnable = false }},
        .ColorBlendAttachmentCount = 1,
    });
    device->SetViewport(frame.GetCommandBuffer(), { .Width = 800, .Height = 600, .MaxDepth = 1.f });
    device->SetScissor(frame.GetCommandBuffer(),  { .Width = 800, .Height = 600 });
    device->BindShader(frame.GetCommandBuffer(), shader);
    device->Draw(frame.GetCommandBuffer(), 3, 1, 0, 0);

    device->EndRenderPass(frame.GetCommandBuffer());
    device->SubmitAndPresentFrame(std::move(frame));
}
```

See [`main.cpp`](main.cpp) for a complete working example using GLFW.

---

## Repository layout

| Path                         | Contents                                       |
|------------------------------|------------------------------------------------|
| `ozz_rendering/include/`     | Public headers (`rhi.h` and friends)           |
| `ozz_rendering/src/`         | Library implementation                         |
| `ozz_rendering/src/vulkan/`  | Vulkan backend                                 |
| `ozz_rendering/third_party/` | CMake wiring for volk, VMA, spdlog, glslang    |
| `main.cpp`                   | Sample application (GLFW window + render loop) |
| `assets/shaders/`            | GLSL shaders for the sample app                |
| `docs/`                      | Architecture overview and API reference        |
| `dist/`                      | Compiled output (generated, not committed)     |

---

## Current status

**Working**

- Vulkan 1.3 device initialisation (instance, surface, physical device selection, logical device, swapchain)
- Frame lifecycle: `BeginFrame` / `SubmitAndPresentFrame`
- Dynamic rendering (no render pass objects)
- Shader objects: runtime GLSL → SPIR-V → `VkShaderEXT` compilation
- Fully dynamic graphics state via `SetGraphicsState`
- Resource barriers (`TextureResourceBarrier`)
- Triangle rendered with hardcoded shader vertices

**Planned**

- Vertex and index buffer support
- Texture creation and sampling
- Descriptor sets / uniform buffers
- Depth/stencil attachment support
- OpenGL backend

---

## Documentation

Full architecture overview and API reference: [`docs/README.md`](docs/README.md)
