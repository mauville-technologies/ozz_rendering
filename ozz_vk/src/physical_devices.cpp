//
// Created by paulm on 2026-02-10.
//

#include <ozz_vk/physical_devices.h>

#include "ozz_vk/util.h"
#include "spdlog/spdlog.h"

#include <ranges>

PhysicalDevices::PhysicalDevices() {}

PhysicalDevices::~PhysicalDevices() {}

void PhysicalDevices::Init(const VkInstance& instance, const VkSurfaceKHR& surface) {
    uint32_t numDevices;

    VkResult result = vkEnumeratePhysicalDevices(instance, &numDevices, nullptr);
    CHECK_VK_RESULT(result, "Enumerate physical devices - count");

    spdlog::info("Num physical devices: {}", numDevices);
    devices.resize(numDevices);

    std::vector<VkPhysicalDevice> vulkanDevices(numDevices);
    result = vkEnumeratePhysicalDevices(instance, &numDevices, vulkanDevices.data());
    CHECK_VK_RESULT(result, "Enumerate physical devices - populate")

    for (auto&& [vkDevice, physicalDevice] : std::ranges::views::zip(vulkanDevices, devices)) {
        physicalDevice.Device = vkDevice;
        vkGetPhysicalDeviceProperties2(vkDevice, &physicalDevice.Properties);

        spdlog::info("Device name: {}", physicalDevice.Properties.properties.deviceName);
        const auto apiVersion = physicalDevice.Properties.properties.apiVersion;
        spdlog::info("\t\t API VERSION: {}.{}.{}.{}",
                     VK_API_VERSION_VARIANT(apiVersion),
                     VK_API_VERSION_MAJOR(apiVersion),
                     VK_API_VERSION_MINOR(apiVersion),
                     VK_API_VERSION_PATCH(apiVersion));

        uint32_t numQueueFamilies {0};
        vkGetPhysicalDeviceQueueFamilyProperties2(vkDevice, &numQueueFamilies, nullptr);
        spdlog::info("Number of queue families: {}", numQueueFamilies);

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
            spdlog::info("Family: {} | Num Queues: {}", i, queueFamily.queueFamilyProperties.queueCount);

            VkQueueFlags flags = queueFamily.queueFamilyProperties.queueFlags;
            spdlog::info("\t\tGFX {}, Compute {}, Transfer {}, Sparse binding {}",
                         (flags & VK_QUEUE_GRAPHICS_BIT) ? "Yes" : "No",
                         (flags & VK_QUEUE_COMPUTE_BIT) ? "Yes" : "No",
                         (flags & VK_QUEUE_TRANSFER_BIT) ? "Yes" : "No",
                         (flags & VK_QUEUE_SPARSE_BINDING_BIT) ? "Yes" : "No");

            result = vkGetPhysicalDeviceSurfaceSupportKHR(vkDevice, i, surface, &supportsPresent);
            CHECK_VK_RESULT(result, "vkGetPhysicalDeviceSurfaceSupport error");
            i++;
        }

        uint32_t numFormats;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(vkDevice, surface, &numFormats, nullptr);
        CHECK_VK_RESULT(result, "Get surface formats counts");
        physicalDevice.SurfaceFormats.resize(numFormats);
        result =
            vkGetPhysicalDeviceSurfaceFormatsKHR(vkDevice, surface, &numFormats, physicalDevice.SurfaceFormats.data());
        CHECK_VK_RESULT(result, "Get surface formats");
        for (auto format : physicalDevice.SurfaceFormats) {
            spdlog::info("Format {:X} color space {:X}",
                         static_cast<uint32_t>(format.format),
                         static_cast<uint32_t>(format.colorSpace));
        }

        result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkDevice, surface, &(physicalDevice.SurfaceCapabilities));
        CHECK_VK_RESULT(result, "Get Surface Capabilities");

        uint32_t numPresentationModes;
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(vkDevice, surface, &numPresentationModes, nullptr);
        CHECK_VK_RESULT(result, "Get num presentation modes");

        physicalDevice.PresentModes.resize(numPresentationModes);
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(vkDevice,
                                                           surface,
                                                           &numPresentationModes,
                                                           physicalDevice.PresentModes.data());

        CHECK_VK_RESULT(result, "Get presentation modes");
        spdlog::info("Num presentation modes: {}", numPresentationModes);

        vkGetPhysicalDeviceMemoryProperties2(vkDevice, &physicalDevice.MemoryProperties);
        spdlog::info("Num memory types {}", physicalDevice.MemoryProperties.memoryProperties.memoryTypeCount);

        for (auto j = 0U; j < physicalDevice.MemoryProperties.memoryProperties.memoryTypeCount; j++) {
            spdlog::info(
                "{}: flags {:X} heap {}",
                j,
                static_cast<uint32_t>(physicalDevice.MemoryProperties.memoryProperties.memoryTypes[j].propertyFlags),
                physicalDevice.MemoryProperties.memoryProperties.memoryTypes[j].heapIndex);
        }

        spdlog::info("Num heap types {}", physicalDevice.MemoryProperties.memoryProperties.memoryHeapCount);

        vkGetPhysicalDeviceFeatures2(vkDevice, &physicalDevice.Features);
    }
}

uint32_t PhysicalDevices::SelectDevice(VkQueueFlags requiredQueueType, bool bSupportsPresent) {
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
                    spdlog::info("Using GFX device {} ({}) and queue family {}",
                                 deviceIndex,
                                 physicalDevice.Properties.properties.deviceName,
                                 queueFamilyIndex);
                    return queueFamilyIndex;
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
        spdlog::info("Using GFX device {} ({}) and queue family {}",
                     deviceIndex,
                     devices[potentialDevice].Properties.properties.deviceName,
                     potentialQueueFamilyIndex);
        return potentialQueueFamilyIndex;
    }

    spdlog::error("Required queue type {:X} and supports present {} not found",
                  static_cast<uint32_t>(requiredQueueType),
                  bSupportsPresent);
    return 0;
}

const PhysicalDevice& PhysicalDevices::Selected() const {
    if (selectedDevice < 0) {
        spdlog::error("A device has not been selected");
        exit(1);
    }
    return devices[selectedDevice];
}