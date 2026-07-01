#pragma once

namespace OZZ::rendering {

    enum class RHIBackend {
        Auto,
        Vulkan,
        OpenGL,
        WebGPU,
    };

    // Resolves RHIBackend::Auto to the concrete backend CreateRHIDevice would use on
    // this platform; any other value is returned unchanged.
    RHIBackend ResolveBackend(RHIBackend preferred);

} // namespace OZZ::rendering
