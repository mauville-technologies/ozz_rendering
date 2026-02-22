//
// Created by paulm on 2026-02-10.
//

#include "physical_devices.h"

#include "ozz_rendering/scratch/util.h"
#include "spdlog/spdlog.h"

#include <ranges>

RHIVulkanPhysicalDevices::RHIVulkanPhysicalDevices() {}

RHIVulkanPhysicalDevices::~RHIVulkanPhysicalDevices() {}

bool RHIVulkanPhysicalDevices::Init(const VkInstance& instance, const VkSurfaceKHR& surface) {
    uint32_t numDevices;

    if (vkEnumeratePhysicalDevices(instance, &numDevices, nullptr) != VK_SUCCESS || numDevices == 0) {
        spdlog::error("Failed to enumerate physical devices");
        return false;
    }

    spdlog::trace("Num physical devices: {}", numDevices);
    devices.resize(numDevices);

    std::vector<VkPhysicalDevice> vulkanDevices(numDevices);
    if (vkEnumeratePhysicalDevices(instance, &numDevices, vulkanDevices.data()) != VK_SUCCESS) {
        spdlog::error("Failed to enumerate physical devices - populate");
        return false;
    }

    for (auto&& [vkDevice, physicalDevice] : std::ranges::views::zip(vulkanDevices, devices)) {
        physicalDevice.Device = vkDevice;
        vkGetPhysicalDeviceProperties2(vkDevice, &physicalDevice.Properties);

        spdlog::trace("Device name: {}", physicalDevice.Properties.properties.deviceName);
        const auto apiVersion = physicalDevice.Properties.properties.apiVersion;
        spdlog::trace("\t\t API VERSION: {}.{}.{}.{}",
                      VK_API_VERSION_VARIANT(apiVersion),
                      VK_API_VERSION_MAJOR(apiVersion),
                      VK_API_VERSION_MINOR(apiVersion),
                      VK_API_VERSION_PATCH(apiVersion));

        uint32_t numQueueFamilies {0};
        vkGetPhysicalDeviceQueueFamilyProperties2(vkDevice, &numQueueFamilies, nullptr);
        spdlog::trace("Number of queue families: {}", numQueueFamilies);

        physicalDevice.QueueFamilyProperties.resize(numQueueFamilies,
                                                    {
                                                        .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2,
                                                    });

        physicalDevice.QueueSupportsPresent.resize(numQueueFamilies);
        vkGetPhysicalDeviceQueueFamilyProperties2(vkDevice,
                                                  &numQueueFamilies,
                                                  physicalDevice.QueueFamilyProperties.data());

        auto i = 0u;
        for (auto&& [queueFamily, supportsPresent] :
             std::ranges::views::zip(physicalDevice.QueueFamilyProperties, physicalDevice.QueueSupportsPresent)) {
            spdlog::trace("Family: {} | Num Queues: {}", i, queueFamily.queueFamilyProperties.queueCount);

            VkQueueFlags flags = queueFamily.queueFamilyProperties.queueFlags;
            spdlog::trace("\t\tGFX {}, Compute {}, Transfer {}, Sparse binding {}",
                          (flags & VK_QUEUE_GRAPHICS_BIT) ? "Yes" : "No",
                          (flags & VK_QUEUE_COMPUTE_BIT) ? "Yes" : "No",
                          (flags & VK_QUEUE_TRANSFER_BIT) ? "Yes" : "No",
                          (flags & VK_QUEUE_SPARSE_BINDING_BIT) ? "Yes" : "No");

            if (vkGetPhysicalDeviceSurfaceSupportKHR(vkDevice, i, surface, &supportsPresent) != VK_SUCCESS) {
                spdlog::error("Failed to get surface support for device {}",
                              physicalDevice.Properties.properties.deviceName);
                return false;
            }
            i++;
        }

        uint32_t numFormats;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(vkDevice, surface, &numFormats, nullptr) != VK_SUCCESS) {
            spdlog::error("Failed to get surface formats count for device {}",
                          physicalDevice.Properties.properties.deviceName);
            return false;
        }
        physicalDevice.SurfaceFormats.resize(numFormats);
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(vkDevice,
                                                 surface,
                                                 &numFormats,
                                                 physicalDevice.SurfaceFormats.data()) != VK_SUCCESS) {
            spdlog::error("Failed to get surface formats for device {}",
                          physicalDevice.Properties.properties.deviceName);
            return false;
        }

        for (auto format : physicalDevice.SurfaceFormats) {
            spdlog::trace("Format {:X} color space {:X}",
                          static_cast<uint32_t>(format.format),
                          static_cast<uint32_t>(format.colorSpace));
        }

        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkDevice, surface, &(physicalDevice.SurfaceCapabilities)) !=
            VK_SUCCESS) {
            spdlog::error("Failed to get surface capabilities for device {}",
                          physicalDevice.Properties.properties.deviceName);
            return false;
        }

        uint32_t numPresentationModes;
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(vkDevice, surface, &numPresentationModes, nullptr) !=
            VK_SUCCESS) {
            spdlog::error("Failed to get num presentation modes for device {}",
                          physicalDevice.Properties.properties.deviceName);
            return false;
        }

        physicalDevice.PresentModes.resize(numPresentationModes);
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(vkDevice,
                                                      surface,
                                                      &numPresentationModes,
                                                      physicalDevice.PresentModes.data()) != VK_SUCCESS) {
            spdlog::error("Failed to get presentation modes for device {}",
                          physicalDevice.Properties.properties.deviceName);
            return false;
        }

        spdlog::trace("Num presentation modes: {}", numPresentationModes);

        vkGetPhysicalDeviceMemoryProperties2(vkDevice, &physicalDevice.MemoryProperties);
        spdlog::trace("Num memory types {}", physicalDevice.MemoryProperties.memoryProperties.memoryTypeCount);

        for (auto j = 0U; j < physicalDevice.MemoryProperties.memoryProperties.memoryTypeCount; j++) {
            spdlog::trace(
                "{}: flags {:X} heap {}",
                j,
                static_cast<uint32_t>(physicalDevice.MemoryProperties.memoryProperties.memoryTypes[j].propertyFlags),
                physicalDevice.MemoryProperties.memoryProperties.memoryTypes[j].heapIndex);
        }

        spdlog::trace("Num heap types {}", physicalDevice.MemoryProperties.memoryProperties.memoryHeapCount);

        vkGetPhysicalDeviceFeatures2(vkDevice, &physicalDevice.Features);
    }
    return true;
}

bool RHIVulkanPhysicalDevices::SelectDevice(VkQueueFlags requiredQueueType, bool bSupportsPresent) {
    int potentialDevice = -1;
    int potentialQueueFamilyIndex = -1;
    auto deviceIndex = 0;
    for (const auto& physicalDevice : devices) {
        auto queueFamilyIndex = 0u;
        for (const auto& queueProperty : physicalDevice.QueueFamilyProperties) {
            if ((queueProperty.queueFamilyProperties.queueFlags & requiredQueueType) &&
                (physicalDevice.QueueSupportsPresent[queueFamilyIndex] == bSupportsPresent)) {
                if (physicalDevice.Properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    selectedDevice = deviceIndex;
                    spdlog::trace("Using GFX device {} ({}) and queue family {}",
                                  deviceIndex,
                                  physicalDevice.Properties.properties.deviceName,
                                  queueFamilyIndex);
                    selectedQueueFamily = queueFamilyIndex;
                    return true;
                }
                potentialDevice = deviceIndex;
                potentialQueueFamilyIndex = queueFamilyIndex;
            }
            queueFamilyIndex++;
        }
        deviceIndex++;
    }

    if (potentialDevice >= 0) {
        spdlog::info("No discrete GPU found, using integrated GPU");
        selectedDevice = potentialDevice;
        selectedQueueFamily = potentialQueueFamilyIndex;
        spdlog::trace("Using GFX device {} ({}) and queue family {}",
                      deviceIndex,
                      devices[potentialDevice].Properties.properties.deviceName,
                      potentialQueueFamilyIndex);
        return true;
    }

    spdlog::error("Required queue type {:X} and supports present {} not found",
                  static_cast<uint32_t>(requiredQueueType),
                  bSupportsPresent);
    selectedDevice = -1;
    selectedQueueFamily = UINT32_MAX;
    return false;
}

const PhysicalDevice& RHIVulkanPhysicalDevices::SelectedDevice() const {
    if (selectedDevice < 0) {
        spdlog::error("A device has not been selected");
        exit(1);
    }
    return devices[selectedDevice];
}