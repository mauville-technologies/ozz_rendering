//
// Created by paulm on 2026-02-21.
//

#include "../include/ozz_rendering/utils/resource_pool.h"
#include <ozz_rendering/rhi_device.h>

#include "vulkan/rhi_device_vulkan.h"

#if defined(OZZ_WEBGPU_ENABLED)
#include "webgpu/rhi_device_webgpu.h"
#endif

std::unique_ptr<OZZ::rendering::RHIDevice> OZZ::rendering::CreateRHIDevice(const RHIInitParams& params) {
    if (params.Backend == RHIBackend::Auto) {
#if defined(__linux__) || defined(_WIN32)
        return std::make_unique<vk::RHIDeviceVulkan>(params.Context);
#else
        throw std::runtime_error("Only vulkan is currently supported");
#endif
    }

    switch (params.Backend) {
        case RHIBackend::Vulkan:
            return std::make_unique<vk::RHIDeviceVulkan>(params.Context);
        case RHIBackend::WebGPU:
#if defined(OZZ_WEBGPU_ENABLED)
            return std::make_unique<webgpu::RHIDeviceWebGPU>(params.Context);
#else
            throw std::runtime_error("WebGPU backend not compiled in (set OZZ_ENABLE_WEBGPU=ON)");
#endif
        case RHIBackend::OpenGL:
        default:
            throw std::runtime_error("Only Vulkan and WebGPU backends are currently supported");
    }
}