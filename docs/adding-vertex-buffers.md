# Adding Vertex Buffers to vulkan-playground

## Current State

The project renders a hardcoded triangle using **shader objects** (`VK_EXT_shader_object`), **dynamic rendering**, and **dynamic vertex input** (`VK_EXT_vertex_input_dynamic_state`). Vertices are baked into the vertex shader via `gl_VertexIndex` lookups. The vertex input state is currently empty:

```cpp
vkCmdSetVertexInputEXT(commandBuffer, 0, nullptr, 0, nullptr);
```

There is **no buffer creation or memory allocation infrastructure** yet. `PhysicalDevice` already stores `VkPhysicalDeviceMemoryProperties2`, which will be needed.

## Approach

Move vertex data out of the shader and into a GPU-side `VkBuffer`, feeding it through Vulkan's vertex input system. Since you're already using `vkCmdSetVertexInputEXT`, no pipeline rebuild is needed — just set the binding/attribute descriptions dynamically at draw time.

---

## Workplan

### 1. Memory Allocation Helper
- [ ] **Expose physical device memory properties** — `VulkanCore` (or `PhysicalDevices`) needs to provide access to `VkPhysicalDeviceMemoryProperties` so the buffer allocator can find a suitable memory type.
- [ ] **Write a `findMemoryType()` utility** — Given `VkMemoryRequirements` and desired `VkMemoryPropertyFlags` (e.g., `HOST_VISIBLE | HOST_COHERENT`), return the matching memory type index. This is the standard helper every Vulkan app needs.

### 2. Buffer Creation Helper
- [ ] **Create a reusable buffer-creation function** — e.g., `createBuffer(device, size, usage, memoryProperties) → {VkBuffer, VkDeviceMemory}`. Steps inside:
  1. `vkCreateBuffer` with the given size and usage flags
  2. `vkGetBufferMemoryRequirements` to learn alignment/size/type bits
  3. Call `findMemoryType()` to pick the right heap
  4. `vkAllocateMemory`
  5. `vkBindBufferMemory`
- [ ] **Decide: VMA vs manual allocation** — For a playground project, manual allocation is fine and educational. VMA (Vulkan Memory Allocator) is the production choice. Either works; manual keeps dependencies minimal.

### 3. Define Vertex Data on the CPU
- [ ] **Create a `Vertex` struct** — e.g.:
  ```cpp
  struct Vertex {
      glm::vec2 position;
      glm::vec3 color;
  };
  ```
- [ ] **Define the triangle vertices** as a `std::vector<Vertex>` or `std::array<Vertex, 3>` in the `App` class (the same positions/colors currently hardcoded in `basic.vert`).

### 4. Create and Populate the Vertex Buffer
- [ ] **Create a host-visible vertex buffer** — Use the buffer creation helper with `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT` and `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`.
- [ ] **Map → memcpy → unmap** — Copy the CPU vertex data into the buffer. With `HOST_COHERENT`, no explicit flush is needed.
- [ ] **(Optional, later) Staging buffer approach** — For better performance, create a `HOST_VISIBLE` staging buffer, copy data into it, then use a one-shot command buffer to `vkCmdCopyBuffer` into a `DEVICE_LOCAL` vertex buffer. This is a common optimization but not required for a first pass.

### 5. Update the Vertex Shader
- [ ] **Replace hardcoded vertices with `in` attributes**:
  ```glsl
  #version 450
  layout(location = 0) in vec2 inPosition;
  layout(location = 1) in vec3 inColor;
  layout(location = 0) out vec3 fragColor;

  void main() {
      gl_Position = vec4(inPosition, 0.0, 1.0);
      fragColor = inColor;
  }
  ```
  The fragment shader (`basic.frag`) needs **no changes**.

### 6. Update Command Buffer Recording
- [ ] **Bind the vertex buffer** — Before the draw call, add:
  ```cpp
  VkBuffer vertexBuffers[] = { vertexBuffer };
  VkDeviceSize offsets[] = { 0 };
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
  ```
- [ ] **Set vertex input state via `vkCmdSetVertexInputEXT`** — Replace the empty call with actual descriptions:
  ```cpp
  VkVertexInputBindingDescription2EXT bindingDesc {
      .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
      .binding = 0,
      .stride = sizeof(Vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      .divisor = 1,
  };

  VkVertexInputAttributeDescription2EXT attrDescs[2] = {
      { // position
          .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
          .location = 0,
          .binding = 0,
          .format = VK_FORMAT_R32G32_SFLOAT,
          .offset = offsetof(Vertex, position),
      },
      { // color
          .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
          .location = 1,
          .binding = 0,
          .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(Vertex, color),
      },
  };

  vkCmdSetVertexInputEXT(commandBuffer, 1, &bindingDesc, 2, attrDescs);
  ```
  This is the key part that tells Vulkan how to interpret the buffer data, matching the `layout(location = ...)` in the shader.

### 7. Cleanup
- [ ] **Destroy the buffer and free memory on shutdown** — Before device destruction in `App::~App()`:
  ```cpp
  vkDestroyBuffer(device, vertexBuffer, nullptr);
  vkFreeMemory(device, vertexBufferMemory, nullptr);
  ```

---

## Notes

- **No pipeline changes needed.** Since you use shader objects + `vkCmdSetVertexInputEXT`, the vertex layout is set dynamically at draw time. This is one of the big wins of your current approach.
- **GLM is already a dependency**, so `glm::vec2`/`glm::vec3` are ready to use in the Vertex struct.
- **The `vkCmdDraw(commandBuffer, 3, 1, 0, 0)` call stays the same** — 3 vertices, 1 instance.
- **Order of operations in command recording**: set vertex input → bind vertex buffers → draw. The exact order of set-vertex-input vs bind-vertex-buffers doesn't matter as long as both happen before the draw.
- **Future extension**: Once vertex buffers work, adding an **index buffer** (`VK_BUFFER_USAGE_INDEX_BUFFER_BIT` + `vkCmdBindIndexBuffer` + `vkCmdDrawIndexed`) is a natural next step for rendering meshes with shared vertices.
