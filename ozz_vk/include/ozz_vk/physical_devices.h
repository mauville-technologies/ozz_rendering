//
// Created by paulm on 2026-02-10.
//

#pragma once
#include <vector>
#include <vulkan/vulkan.h>

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

class PhysicalDevices {
public:
    PhysicalDevices();
    ~PhysicalDevices();

    void Init(const VkInstance& instance, const VkSurfaceKHR& surface);
    uint32_t SelectDevice(VkQueueFlags requiredQueueType, bool bSupportsPresent);
    [[nodiscard]] const PhysicalDevice& Selected() const;

private:
    std::vector<PhysicalDevice> devices;

    int selectedDevice = -1;
};
