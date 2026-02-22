//
// Created by paulm on 2026-02-10.
//

#pragma once
#include <vector>
#include <volk.h>

struct PhysicalDevice {
    VkPhysicalDevice Device;
    VkPhysicalDeviceProperties2 Properties {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    std::vector<VkQueueFamilyProperties2> QueueFamilyProperties;
    std::vector<VkBool32> QueueSupportsPresent;
    std::vector<VkSurfaceFormatKHR> SurfaceFormats;
    VkSurfaceCapabilitiesKHR SurfaceCapabilities;
    VkPhysicalDeviceMemoryProperties2 MemoryProperties {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
    std::vector<VkPresentModeKHR> PresentModes;
    VkPhysicalDeviceFeatures2 Features {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
};

class RHIVulkanPhysicalDevices {
public:
    RHIVulkanPhysicalDevices();
    ~RHIVulkanPhysicalDevices();

    bool Init(const VkInstance& instance, const VkSurfaceKHR& surface);
    bool SelectDevice(VkQueueFlags requiredQueueType, bool bSupportsPresent);
    [[nodiscard]] const PhysicalDevice& SelectedDevice() const;

    [[nodiscard]] uint32_t SelectedQueueFamily() const { return selectedQueueFamily; }

private:
    std::vector<PhysicalDevice> devices;

    int selectedDevice = -1;
    uint32_t selectedQueueFamily = UINT32_MAX;
};
