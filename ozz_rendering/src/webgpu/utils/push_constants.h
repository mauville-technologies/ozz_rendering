#pragma once

#include <cstdint>

namespace OZZ::rendering::webgpu {

    // WebGPU has no native push constants; they are emulated via a dynamic-offset
    // uniform buffer bound at this dedicated set/binding. Shared between the device
    // (buffer/bind-group creation, draw binding) and the shader translation unit
    // (source patching, reflection).
    inline constexpr uint32_t PushConstantSet      = 3;
    inline constexpr uint32_t PushConstantBinding  = 0;
    // Max bytes per emulated push-constant block (one slot per SetPushConstants call).
    inline constexpr uint32_t PushConstantSlotSize = 256;

} // namespace OZZ::rendering::webgpu
