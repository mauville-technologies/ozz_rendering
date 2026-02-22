//
// Created by paulm on 2026-02-21.
//

#include <ozz_rendering/rhi.h>

#include "vulkan/rhi_device_vulkan.h"

std::unique_ptr<OZZ::rendering::RHIDevice> OZZ::rendering::CreateRHIDevice(const RHIInitParams& params) {
    if (params.Backend == RHIBackend::Auto) {
        // if windows or linux
#if defined(__linux__) || defined(_WIN32)
        return std::make_unique<vk::RHIDeviceVulkan>(params.Context);
#else
        throw std::runtime_error("Only vulkan is currently supported");
#endif
    }

    switch (params.Backend) {
        case RHIBackend::Vulkan:
            return std::make_unique<vk::RHIDeviceVulkan>(params.Context);
        case RHIBackend::OpenGL:
        default:
            throw std::runtime_error("Only vulkan is currently supported");
    }
}