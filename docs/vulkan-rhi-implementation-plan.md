# Vulkan RHI Implementation Plan

## Problem Statement

The lights engine currently has direct OpenGL integration throughout the codebase. To add Vulkan support, we need to
create a modern RHI (Render Hardware Interface) abstraction that:

- Supports both OpenGL and Vulkan backends with runtime switching
- Modernizes the rendering API to be less OpenGL-specific
- Integrates cleanly with the existing render graph (Renderable) system
- Uses an RHIDevice pattern for resource management

## Proposed Architecture

### RHIDevice-Centric Design

- **RHIDevice**: Central coordinator that owns resources and command submission
- **Resource Management**: Device creates and manages shaders, textures, buffers
- **Command Buffers**: Single command buffer per frame, passed through render graph
- **Backend Abstraction**: OpenGL and Vulkan implementations behind common interface

### Key Design Principles

1. **Modern API**: Command-buffer based, less immediate-mode
2. **Resource Ownership**: RHIDevice owns all GPU resources
3. **Render Graph Integration**: Command buffer flows through existing Renderable::render() calls
4. **Runtime Backend Selection**: Choose OpenGL/Vulkan at application startup
5. **Future-Proof**: Architecture supports later parallelization of command recording

## Implementation Workplan

### Phase 1: Core RHI Foundation

- [ ] Design and implement core RHI interfaces (IRHIDevice, IRHICommandBuffer, IRHIResource)
- [ ] Create RHIDevice factory system with OpenGL/Vulkan backend selection
- [ ] Implement basic OpenGL RHIDevice to validate interface design
- [ ] Design resource handle system (RHITexture, RHIShader, RHIBuffer abstractions)
- [ ] Create command buffer abstraction for draw commands and state changes

### Phase 2: Resource Management Abstraction

- [ ] Abstract texture creation and management through RHIDevice
- [ ] Abstract shader compilation and management through RHIDevice
- [ ] Abstract buffer (vertex/index/uniform) creation and management
- [ ] Implement resource tracking and lifetime management
- [ ] Add render target/framebuffer abstraction

### Phase 3: Command Recording System

- [ ] Design command buffer API for common operations (draw, clear, set state)
- [ ] Integrate command buffer into existing Renderable::render() signature
- [ ] Implement immediate-mode emulation for OpenGL backend
- [ ] Add render pass concept for Vulkan-style rendering
- [ ] Implement basic state tracking and validation

### Phase 4: Engine Integration

- [ ] Refactor existing Material class to use RHI abstractions
- [ ] Update Renderer class to create and submit command buffers
- [ ] Modify resource loading to go through RHIDevice
- [ ] Update scene rendering pipeline to use new command flow
- [ ] Ensure backward compatibility layer if needed for truck-kun app

### Phase 5: Vulkan Backend Implementation

- [ ] Implement Vulkan instance and device creation
- [ ] Create Vulkan RHIDevice implementation
- [ ] Implement Vulkan resource management (textures, buffers, shaders)
- [ ] Add Vulkan command buffer recording and submission
- [ ] Implement Vulkan render passes and synchronization
- [ ] Add Vulkan-specific optimizations (descriptor sets, memory management)

### Phase 6: Testing and Polish

- [ ] Create comprehensive test suite for RHI interface
- [ ] Test OpenGL and Vulkan backends with truck-kun application
- [ ] Add runtime backend switching mechanism
- [ ] Performance testing and optimization
- [ ] Documentation and code cleanup

## Interface Design Examples

### Core RHI Interfaces

```cpp
// Base RHI Device Interface
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;
    
    // Resource Creation
    virtual RHITextureHandle CreateTexture(const TextureDesc& desc) = 0;
    virtual RHIShaderHandle CreateShader(const ShaderDesc& desc) = 0;
    virtual RHIBufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    virtual RHIMaterialHandle CreateMaterial(const MaterialDesc& desc) = 0;
    virtual RHIRenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc) = 0;
    
    // Resource Management
    virtual void DestroyResource(RHIResourceHandle handle) = 0;
    virtual void UpdateBuffer(RHIBufferHandle buffer, const void* data, size_t size, size_t offset = 0) = 0;
    virtual void UpdateTexture(RHITextureHandle texture, const void* data, const TextureUpdateDesc& desc) = 0;
    
    // Command Buffer Management
    virtual RHICommandBufferHandle CreateCommandBuffer() = 0;
    virtual void SubmitCommandBuffer(RHICommandBufferHandle cmdBuffer) = 0;
    virtual void Present() = 0;
    
    // Device Info
    virtual RHIBackendType GetBackendType() const = 0;
    virtual const RHIDeviceCapabilities& GetCapabilities() const = 0;
};

// Command Buffer Interface
class IRHICommandBuffer {
public:
    virtual ~IRHICommandBuffer() = default;
    
    // Render Passes
    virtual void BeginRenderPass(RHIRenderTargetHandle target, const ClearValues& clear) = 0;
    virtual void EndRenderPass() = 0;
    
    // Drawing
    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    
    // Resource Binding
    virtual void BindMaterial(RHIMaterialHandle material) = 0;
    virtual void BindVertexBuffer(RHIBufferHandle buffer, uint32_t binding = 0, uint64_t offset = 0) = 0;
    virtual void BindIndexBuffer(RHIBufferHandle buffer, RHIIndexType indexType, uint64_t offset = 0) = 0;
    
    // State Management
    virtual void SetViewport(const Viewport& viewport) = 0;
    virtual void SetScissor(const ScissorRect& scissor) = 0;
    
    // Uniforms/Push Constants
    virtual void PushConstants(const void* data, uint32_t size, uint32_t offset = 0) = 0;
    virtual void BindUniformBuffer(RHIBufferHandle buffer, uint32_t binding) = 0;
    
    // Debug
    virtual void BeginDebugMarker(const char* name) = 0;
    virtual void EndDebugMarker() = 0;
};

// Resource Handle System (type-safe opaque handles)
template<typename T>
struct RHIHandle {
    uint32_t id = INVALID_HANDLE;
    uint32_t generation = 0;
    
    bool IsValid() const { return id != INVALID_HANDLE; }
    operator bool() const { return IsValid(); }
    
    bool operator==(const RHIHandle& other) const {
        return id == other.id && generation == other.generation;
    }
    bool operator!=(const RHIHandle& other) const { return !(*this == other); }
};

using RHITextureHandle = RHIHandle<struct RHITexture>;
using RHIShaderHandle = RHIHandle<struct RHIShader>;
using RHIBufferHandle = RHIHandle<struct RHIBuffer>;
using RHIMaterialHandle = RHIHandle<struct RHIMaterial>;
using RHIRenderTargetHandle = RHIHandle<struct RHIRenderTarget>;
using RHICommandBufferHandle = RHIHandle<struct RHICommandBuffer>;

// Modern Material Interface
class Material {
private:
    RHIMaterialHandle m_handle;
    IRHIDevice* m_device;
    
public:
    Material(IRHIDevice* device, RHIShaderHandle shader);
    
    // Resource binding - accumulated until BindToCommandBuffer
    void SetTexture(const std::string& name, RHITextureHandle texture);
    void SetUniformBuffer(const std::string& name, RHIBufferHandle buffer);
    void SetPushConstant(const std::string& name, const void* data, size_t size);
    
    // Command buffer integration
    void BindToCommandBuffer(IRHICommandBuffer* cmdBuffer);
    
    RHIMaterialHandle GetHandle() const { return m_handle; }
};
```

### Updated Renderable Interface

```cpp
// Updated base Renderable class
class Renderable : public GraphNode {
public:
    // New signature: receives command buffer for recording
    virtual void render(IRHICommandBuffer* commandBuffer, const RenderParams& params) = 0;
    
    // Legacy compatibility (temporary)
    virtual void render() { /* fallback to old path */ }
};

// Example: Updated GameLayer rendering
class GameLayer : public SceneLayer {
public:
    void render(IRHICommandBuffer* commandBuffer, const RenderParams& params) override {
        // Begin render pass to our render target
        commandBuffer->BeginRenderPass(m_mainRenderTarget, {.color = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f)});
        
        // Set viewport and camera uniforms
        commandBuffer->SetViewport(m_viewport);
        commandBuffer->PushConstants(&params.viewProjection, sizeof(glm::mat4));
        
        // Render all entities through ECS
        RenderEntities(commandBuffer, params);
        
        commandBuffer->EndRenderPass();
    }
    
private:
    void RenderEntities(IRHICommandBuffer* cmdBuffer, const RenderParams& params) {
        auto view = m_registry.view<WorldTransformCacheComponent, RenderComponent>();
        
        for (auto entity : view) {
            auto& transform = view.get<WorldTransformCacheComponent>(entity);
            auto& render = view.get<RenderComponent>(entity);
            
            // Modern material binding
            render.Material->SetPushConstant("model", &transform.WorldTransform, sizeof(glm::mat4));
            render.Material->BindToCommandBuffer(cmdBuffer);
            
            // Bind mesh and draw
            cmdBuffer->BindVertexBuffer(render.Mesh->GetVertexBuffer());
            cmdBuffer->BindIndexBuffer(render.Mesh->GetIndexBuffer(), RHIIndexType::UInt16);
            cmdBuffer->DrawIndexed(render.Mesh->GetIndexCount());
        }
    }
};
```

## Architecture Diagrams

### Overall RHI Architecture

```mermaid
graph TB
    subgraph "Application Layer"
        Game[LightsGame]
        Scene[GameScene]
        Layer[GameLayer]
    end

    subgraph "Engine Core"
        Renderer[Renderer]
        RenderGraph[Render Graph]
        Renderable[Renderable Nodes]
    end

    subgraph "RHI Abstraction"
        RHIDevice[IRHIDevice]
        CommandBuffer[IRHICommandBuffer]
        Material[Material]
        Resources[RHI Resources]
    end

    subgraph "Backend Implementation"
        OpenGL[OpenGL Backend]
        Vulkan[Vulkan Backend]
    end

    Game --> Renderer
    Renderer --> RenderGraph
    RenderGraph --> Renderable
    Renderable --> CommandBuffer
    CommandBuffer --> RHIDevice
    Material --> RHIDevice
    RHIDevice --> OpenGL
    RHIDevice --> Vulkan
    Resources --> RHIDevice
```

### Command Buffer Flow

```mermaid
sequenceDiagram
    participant Game as Game Loop
    participant Renderer as Renderer
    participant Graph as Render Graph
    participant Node as Renderable Node
    participant CMD as Command Buffer
    participant Device as RHI Device
    participant Backend as Backend (GL/VK)
    Game ->> Renderer: ExecuteSceneGraph()
    Renderer ->> Device: CreateCommandBuffer()
    Device -->> Renderer: CommandBufferHandle
    Renderer ->> Graph: render(commandBuffer)

    loop For each node in topological order
        Graph ->> Node: render(commandBuffer, params)
        Node ->> CMD: BeginRenderPass()
        Node ->> CMD: SetViewport(), PushConstants()
        Node ->> CMD: BindMaterial(), BindBuffers()
        Node ->> CMD: Draw() / DrawIndexed()
        Node ->> CMD: EndRenderPass()
    end

    Graph -->> Renderer: Commands recorded
    Renderer ->> Device: SubmitCommandBuffer()
    Device ->> Backend: Execute commands
    Renderer ->> Device: Present()
```

### Resource Management Flow

```mermaid
graph LR
    subgraph "Resource Creation"
        App[Application] --> RM[Resource Manager]
        RM --> Device[RHI Device]
        Device --> Handle[Resource Handle]
    end

    subgraph "Resource Usage"
        Mat[Material] --> Bind[Bind Resources]
        Bind --> CMD[Command Buffer]
        CMD --> Device2[RHI Device]
    end

    subgraph "Backend Storage"
        Device2 --> OpenGL_Res[OpenGL Objects]
        Device2 --> Vulkan_Res[Vulkan Resources]
    end

    Handle -.-> Mat
    style Handle fill: #e1f5fe
    style OpenGL_Res fill: #fff3e0
    style Vulkan_Res fill: #f3e5f5
```

## Generational Handle System Deep Dive

The "generational" aspect is the key innovation that makes handles safe and prevents common resource management bugs. Here's how it works:

### The Core Problem: Slot Reuse
Without generations, handles are just indices into an array:
```cpp
// BAD: Simple index-based handles
struct SimpleHandle { uint32_t id; };

std::vector<Texture> textures;
std::queue<uint32_t> freeSlots;

SimpleHandle CreateTexture() {
    if (!freeSlots.empty()) {
        uint32_t slot = freeSlots.front();
        freeSlots.pop();
        return SimpleHandle{slot};  // Reuse old slot
    }
    // ... allocate new slot
}
```

**The Problem:** What happens when you reuse a slot?
```cpp
auto handleA = CreateTexture();     // Gets slot 5 → {id: 5}
DestroyTexture(handleA);            // Frees slot 5, adds to free list

auto handleB = CreateTexture();     // Reuses slot 5 → {id: 5}

// BUG: handleA and handleB are identical!
// Old code can still access the new texture through handleA
```

### The Solution: Generation Counter
Add a generation counter that increments each time a slot is reused:

```cpp
struct ResourceSlot {
    Texture texture;
    uint32_t generation;    // This is the key!
    bool isAlive;
};

std::vector<ResourceSlot> slots;

RHIHandle CreateTexture() {
    uint32_t slotIndex;
    if (!freeSlots.empty()) {
        slotIndex = freeSlots.front();
        freeSlots.pop();
        
        // CRITICAL: Increment generation when reusing slot
        slots[slotIndex].generation++;
    } else {
        slotIndex = slots.size();
        slots.emplace_back();
        slots[slotIndex].generation = 1;  // Start at 1
    }
    
    slots[slotIndex].isAlive = true;
    
    return RHIHandle{
        .id = slotIndex,
        .generation = slots[slotIndex].generation  // Store current generation
    };
}
```

### Step-by-Step Example

**Step 1: Create first texture**
```cpp
auto handleA = CreateTexture();  // Returns {id: 5, generation: 1}

// Slot array state:
// slots[5] = {texture: TextureA, generation: 1, isAlive: true}
```

**Step 2: Destroy texture**
```cpp
DestroyTexture(handleA);

// Slot array state:
// slots[5] = {texture: <destroyed>, generation: 1, isAlive: false}
// freeSlots.push(5)  // Slot 5 is now available for reuse
```

**Step 3: Create second texture (reuses slot)**
```cpp
auto handleB = CreateTexture();  // Returns {id: 5, generation: 2}

// Slot array state:
// slots[5] = {texture: TextureB, generation: 2, isAlive: true}
//                                         ↑
//                                   GENERATION INCREMENTED!
```

**Step 4: Try to use old handle**
```cpp
// Someone still has the old handleA = {id: 5, generation: 1}
Texture* tex = GetTexture(handleA);

bool IsValidHandle(RHIHandle handle) {
    if (handle.id >= slots.size()) return false;           // ✓ Valid index
    if (!slots[handle.id].isAlive) return false;          // ✓ Slot is alive
    if (slots[handle.id].generation != handle.generation)  // ❌ GENERATION MISMATCH!
        return false;                                      //   1 ≠ 2
    return true;
}

// Result: GetTexture(handleA) returns nullptr
// The old handle is safely invalidated!
```

### Why This Prevents Bugs

**1. Use-After-Free Prevention**
```cpp
auto texture = CreateTexture();
DestroyTexture(texture);

// Later, accidentally use destroyed handle
BindTexture(texture);  // Safe! Returns error instead of corrupting memory
```

**2. ABA Problem Prevention**
The ABA problem occurs when:
- Thread A reads value A
- Thread B changes A to B, then back to A  
- Thread A thinks nothing changed

With generations:
```cpp
// Thread A gets handle
auto handleA = CreateTexture();  // {id: 5, gen: 3}

// Thread B destroys and recreates
DestroyTexture(handleA);
auto handleB = CreateTexture();  // {id: 5, gen: 4} - Different generation!

// Thread A's handle is now safely invalid
UseTexture(handleA);  // Returns error, doesn't use wrong texture
```

**3. Debug Safety**
```cpp
// Easy to detect stale handle usage in debug builds
bool IsValidHandle(RHIHandle handle) {
    bool valid = (handle.id < slots.size() && 
                  slots[handle.id].isAlive && 
                  slots[handle.id].generation == handle.generation);
    
    if (!valid && handle.generation != 0) {
        // This was a real handle that became stale
        LOG_WARNING("Using stale handle {id: {}, gen: {}}, current gen: {}", 
                   handle.id, handle.generation, slots[handle.id].generation);
    }
    
    return valid;
}
```

### Memory Layout Optimization

**Packed Representation (32-bit handle):**
```cpp
struct RHIHandle {
    union {
        struct {
            uint32_t id : 24;          // 16M resources max
            uint32_t generation : 8;   // 256 generations max
        };
        uint32_t packed;               // For easy copying/comparison
    };
};
```

**Benefits:**
- Handle fits in 32 bits (same as a pointer on 32-bit systems)
- Cheap to copy (single integer)
- Fast comparison (single integer comparison)
- Cache-friendly when stored in arrays

### Generation Overflow Handling

What happens when generation counter overflows?
```cpp
constexpr uint32_t MAX_GENERATION = 255;  // 8-bit counter

uint32_t AllocateSlot() {
    uint32_t slot = GetFreeSlot();
    
    // Handle overflow
    if (slots[slot].generation >= MAX_GENERATION) {
        // Option 1: Never reuse this slot again
        slots[slot].generation = MAX_GENERATION;
        // Mark slot as permanently retired
        
        // Option 2: Reset and log warning
        slots[slot].generation = 1;
        LOG_WARNING("Generation counter overflow for slot {}", slot);
    } else {
        slots[slot].generation++;
    }
    
    return slot;
}
```

### Real-World Comparison

**Similar systems:**
- **Entity Component Systems**: Entity handles work exactly the same way
- **Vulkan**: VkObjectType handles are similar (but less safe)
- **DirectX 12**: Resource descriptors use generational handles
- **Game Engines**: Unity's InstanceID, Unreal's UObject handles

**Traditional alternative:**
```cpp
// Old way: Raw pointers (unsafe)
Texture* CreateTexture();  // Returns pointer that can become dangling

// New way: Generational handles (safe)
RHITextureHandle CreateTexture();  // Returns handle that validates on access
```

The generational system trades a small amount of memory (extra generation counter) and CPU time (generation check) for significant safety gains and easier debugging.

## Technical Considerations

### Command Buffer Flow

```
Game Loop → Renderer → RenderGraph → Renderable::render(commandBuffer) → Commands Recorded → Submit → Present
```

### Resource Handle Design

- Opaque handles instead of direct pointers
- RHIDevice validates handles and manages backing resources
- Automatic cleanup on device destruction
- Type-safe resource binding

### Backend-Specific Challenges

**OpenGL:**

- Immediate mode emulation over command buffer abstraction
- Context management and state tracking
- Resource sharing between contexts

**Vulkan:**

- Complex initialization (instance, device, queues, swapchain)
- Explicit memory management
- Synchronization and command buffer lifecycle
- Descriptor set management for resources

### Integration Points

#### 1. GameParameters Extension

```cpp
enum class RHIBackendType {
    Auto,           // Auto-detect best available
    OpenGL,         // Force OpenGL 4.1+
    Vulkan,         // Force Vulkan 1.0+
    Count
};

struct GameParameters {
    float FPS = 60.0f;
    glm::ivec2 WindowSize = {1280, 720};
    EWindowMode WindowMode = EWindowMode::Windowed;
    float SampleRate = 44100.0f;
    int AudioChannels = 2;
    RHIBackendType PreferredBackend = RHIBackendType::Auto;  // NEW
    bool EnableValidation = false;  // NEW - for debug builds
};
```

#### 2. LightsGame Template Updates

```cpp
template<typename SceneType>
class LightsGame {
private:
    std::unique_ptr<IRHIDevice> m_rhiDevice;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    // ... other subsystems

public:
    bool Initialize(const GameParameters& params) {
        // Create window first (needed for graphics context)
        m_window = CreateWindow(params);
        
        // Create RHI device based on preference
        m_rhiDevice = CreateRHIDevice(params.PreferredBackend, m_window.get(), params.EnableValidation);
        if (!m_rhiDevice) {
            LOG_ERROR("Failed to create RHI device");
            return false;
        }
        
        // Pass RHI device to renderer
        m_renderer = std::make_unique<Renderer>(m_rhiDevice.get());
        
        // Initialize other subsystems...
        return true;
    }
};

// Factory function
std::unique_ptr<IRHIDevice> CreateRHIDevice(RHIBackendType preferred, Window* window, bool enableValidation) {
    if (preferred == RHIBackendType::Auto) {
        // Try Vulkan first, fall back to OpenGL
        if (auto vulkanDevice = CreateVulkanDevice(window, enableValidation)) {
            return vulkanDevice;
        }
        return CreateOpenGLDevice(window, enableValidation);
    }
    
    switch (preferred) {
        case RHIBackendType::Vulkan:
            return CreateVulkanDevice(window, enableValidation);
        case RHIBackendType::OpenGL:
            return CreateOpenGLDevice(window, enableValidation);
        default:
            return nullptr;
    }
}
```

#### 3. Renderer Class Updates

```cpp
class Renderer {
private:
    IRHIDevice* m_rhiDevice;
    RHICommandBufferHandle m_currentCommandBuffer;
    
public:
    explicit Renderer(IRHIDevice* rhiDevice) : m_rhiDevice(rhiDevice) {}
    
    void ExecuteSceneGraph() {
        // Create command buffer for this frame
        m_currentCommandBuffer = m_rhiDevice->CreateCommandBuffer();
        auto* cmdBuffer = m_rhiDevice->GetCommandBuffer(m_currentCommandBuffer);
        
        // Execute render graph with command buffer
        if (auto sceneGraph = GetCurrentScene()->GetSceneGraph()) {
            RenderParams params = BuildRenderParams();
            sceneGraph->render(cmdBuffer, params);
        }
        
        // Submit and present
        m_rhiDevice->SubmitCommandBuffer(m_currentCommandBuffer);
        m_rhiDevice->Present();
    }
};
```

#### 4. Resource Manager Integration

```cpp
class ResourceManager {
private:
    IRHIDevice* m_rhiDevice;
    
public:
    explicit ResourceManager(IRHIDevice* rhiDevice) : m_rhiDevice(rhiDevice) {}
    
    // Texture loading through RHI
    RHITextureHandle LoadTexture(const std::string& path) {
        // Load image data (unchanged)
        auto imageData = LoadImageFromFile(path);
        
        // Create through RHI device
        TextureDesc desc {
            .width = imageData.width,
            .height = imageData.height,
            .format = RHIFormat::RGBA8,
            .usage = RHITextureUsage::ShaderRead,
            .data = imageData.pixels
        };
        
        return m_rhiDevice->CreateTexture(desc);
    }
    
    // Shader loading through RHI
    RHIShaderHandle LoadShader(const std::string& vertPath, const std::string& fragPath) {
        auto vertSource = ReadFile(vertPath);
        auto fragSource = ReadFile(fragPath);
        
        ShaderDesc desc {
            .stages = {
                {RHIShaderStage::Vertex, vertSource},
                {RHIShaderStage::Fragment, fragSource}
            }
        };
        
        return m_rhiDevice->CreateShader(desc);
    }
};
```

#### 5. Updated Material System

```cpp
// Materials become resource handle containers + binding logic
class Material {
private:
    IRHIDevice* m_rhiDevice;
    RHIShaderHandle m_shader;
    
    // Resource bindings (accumulated until bind time)
    std::unordered_map<std::string, RHITextureHandle> m_textures;
    std::unordered_map<std::string, RHIBufferHandle> m_uniformBuffers;
    std::vector<uint8_t> m_pushConstantData;
    
public:
    Material(IRHIDevice* device, RHIShaderHandle shader) 
        : m_rhiDevice(device), m_shader(shader) {}
    
    // Resource binding (deferred until command buffer bind)
    void SetTexture(const std::string& name, RHITextureHandle texture) {
        m_textures[name] = texture;
    }
    
    void SetPushConstant(const std::string& name, const void* data, size_t size) {
        // Accumulate push constant data
        // (implementation depends on shader reflection)
    }
    
    // Command buffer integration
    void BindToCommandBuffer(IRHICommandBuffer* cmdBuffer) {
        // Create material descriptor/pipeline state if needed
        auto materialHandle = m_rhiDevice->GetOrCreateMaterialState(m_shader, m_textures, m_uniformBuffers);
        
        cmdBuffer->BindMaterial(materialHandle);
        
        if (!m_pushConstantData.empty()) {
            cmdBuffer->PushConstants(m_pushConstantData.data(), m_pushConstantData.size());
        }
    }
};
```

## Future Enhancements

- Multi-threaded command buffer recording
- Advanced Vulkan features (compute shaders, ray tracing)
- Direct3D 12 backend support
- GPU-driven rendering optimizations
- Advanced memory management and resource pooling

## Notes

- This is a major architectural change that will require touching most rendering code
- Consider keeping the old OpenGL path available during transition for fallback
- Focus on getting the abstraction right before optimizing for performance
- The render graph system provides a natural command buffer recording boundary