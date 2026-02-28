# RHIBufferVulkan — layout and GetDescriptor()

## Summary

Recommended concrete shape for an RHIBufferVulkan (fields + helper APIs) and a safe GetDescriptor() implementation that accounts for suballocations and clamps the returned range.

---

## Recommended RHIBufferVulkan layout (conceptual)

```cpp
struct RHIBufferVulkan {
    // Vulkan / VMA handles
    VkBuffer            Buffer         = VK_NULL_HANDLE;
    VmaAllocation       Allocation     = VK_NULL_HANDLE;

    // Size / alignment / usage metadata
    VkDeviceSize        Size           = 0;
    VkDeviceSize        Alignment      = 0;     // useful for UBO dynamic offsets
    VkBufferUsageFlags  VkUsage        = 0;     // vk flags used at creation

    // High-level RHI flags (drive allocation/mapping strategy)
    BufferUsage         Usage          = BufferUsage::None;     // bitmask: Vertex, Index, Uniform, Storage, Staging
    MemoryAccess        Access         = MemoryAccess::GpuOnly; // GpuOnly | CpuToGpu | GpuToCpu

    // Mapping state (only valid for host-visible allocations or suballocations)
    void*               MappedPtr      = nullptr;
    bool                PersistentlyMapped = false;
    bool                HostVisible    = false; // cache of VMA/VK properties
    bool                HostCoherent   = false; // cache: whether flush/invalidate required

    // Descriptor info (helper for descriptor writes)
    VkDescriptorBufferInfo Descriptor{}; // {buffer, offset, range}
    uint32_t            DynamicOffsetStride = 0; // for dynamic UBOs (alignment)

    // Suballocation metadata (optional if the buffer is a slice of a larger allocation)
    bool                IsSuballocated = false;
    VkDeviceSize        SubOffset      = 0;
    VkDeviceSize        SubRange       = 0;

    // Helper methods (implementations use VMA/vk calls)
    void Create(VkDevice device, VmaAllocator allocator, const RHIBufferDescriptor& desc, VkBufferUsageFlags extraVkFlags = 0);
    void Destroy(VkDevice device, VmaAllocator allocator);
    void* Map(VmaAllocator allocator);
    void  Unmap(VmaAllocator allocator);
    void  Write(const void* src, VkDeviceSize dstOffset, VkDeviceSize size, VmaAllocator allocator);
    void  Flush(VmaAllocator allocator, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    void  Invalidate(VmaAllocator allocator, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    VkDescriptorBufferInfo GetDescriptor(VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE) const;
};
```

Notes:
- Store `Usage` and `Access` so the allocator/manager can pick VMA flags and choose mapping/staging strategies.
- Prefer suballocation/ring allocators for dynamic per-frame uploads; represent slices with `IsSuballocated/SubOffset/SubRange`.

---

## Safe GetDescriptor() implementation

This implementation returns a VkDescriptorBufferInfo that points into the underlying VkBuffer, uses the suballocation slice if present, and clamps the returned range so it does not run past the allocation.

```cpp
VkDescriptorBufferInfo RHIBufferVulkan::GetDescriptor(VkDeviceSize offset, VkDeviceSize range) const {
    VkDescriptorBufferInfo desc{};
    if (Buffer == VK_NULL_HANDLE) {
        desc.buffer = VK_NULL_HANDLE;
        desc.offset = 0;
        desc.range  = 0;
        return desc;
    }

    // Base offset inside the VkBuffer (for suballocated slices)
    const VkDeviceSize baseOffset = IsSuballocated ? SubOffset : 0;
    const VkDeviceSize effectiveOffset = baseOffset + offset;

    // Compute how many bytes are actually available from the requested offset
    VkDeviceSize available = 0;
    if (IsSuballocated) {
        available = (SubRange > offset) ? (SubRange - offset) : 0;
    } else {
        available = (Size > offset) ? (Size - offset) : 0;
    }

    // Choose final range: either requested (if it fits) or the available bytes
    VkDeviceSize finalRange = (range == VK_WHOLE_SIZE) ? available : (range <= available ? range : available);

    desc.buffer = Buffer;
    desc.offset = effectiveOffset;
    desc.range  = finalRange;
    return desc;
}
```

### Behavior and edge cases

- If `Buffer` is null, zeroed descriptor is returned.
- If the buffer is a suballocation, the returned offset is `SubOffset + offset` and range is clamped to `SubRange - offset`.
- If `range == VK_WHOLE_SIZE`, the descriptor range becomes the available bytes from `offset` to end of slice/allocation.
- This helper is safe to use when writing descriptor sets and when generating descriptor info for draws; callers should still ensure alignment and binding-size rules required by Vulkan (e.g., range must be multiple of minAlignment when required).

---

## Small implementation note

When the buffer is persistently mapped and non-coherent, remember to call `Flush` after CPU writes and `Invalidate` before CPU reads; `GetDescriptor()` does not touch mapping or synchronization — it's purely a descriptor helper.
