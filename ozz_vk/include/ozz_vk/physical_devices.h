//
// Created by paulm on 2026-02-10.
//

#pragma once
#include <vector>
#include <vulkan/vulkan.h>

struct PhysicalDevice {
    VkPhysicalDevice Device;
    VkPhysicalDeviceProperties Properties;
    std::vector<VkQueueFamilyProperties> QueueFamilyProperties;
    std::vector<VkBool32> QueueSupportsPresent;
    std::vector<VkSurfaceFormatKHR> SurfaceFormats;
    VkSurfaceCapabilitiesKHR SurfaceCapabilities;
    VkPhysicalDeviceMemoryProperties MemoryProperties;
    std::vector<VkPresentModeKHR> PresentModes;
    VkPhysicalDeviceFeatures Features;
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
