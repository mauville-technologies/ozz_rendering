//
// Created by paulm on 2026-02-21.
//

#include "rhi_device_vulkan.h"

#include <algorithm>
#include <ranges>
#include <spdlog/spdlog.h>

namespace OZZ::rendering::vk {
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT type,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                        void* pUserData) {

        switch (severity) {
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                spdlog::error(pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
                spdlog::trace(pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                spdlog::warn(pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
                spdlog::trace(pCallbackData->pMessage);
                break;
            default:
                break;
        }

        return VK_TRUE;
    }

    RHIDeviceVulkan::RHIDeviceVulkan(const PlatformContext& context) : RHIDevice(context), platformContext(context) {
        auto result = volkInitialize();
        if (result != VK_SUCCESS) {
            spdlog::error("Failed to initialize volk");
            return;
        }

        bIsValid = initialize();
    }

    RHIDeviceVulkan::~RHIDeviceVulkan() {}

    bool RHIDeviceVulkan::initialize() {
        spdlog::info("Initializing Vulkan RHI device");
        constexpr auto failureMessage = [] {
            spdlog::error("Failed to initialize Vulkan RHI device");
        };

        if (!createInstance()) {
            failureMessage();
            return false;
        }
        if (!createDebugCallback()) {
            failureMessage();
            return false;
        }
        if (!createSurface()) {
            failureMessage();
            return false;
        }

        if (!physicalDevices.Init(instance, surface)) {
            failureMessage();
            return false;
        }

        if (!physicalDevices.SelectDevice(VK_QUEUE_GRAPHICS_BIT, true)) {
            failureMessage();
            return false;
        }

        if (!createDevice()) {
            failureMessage();
            return false;
        }

        spdlog::info("Successfully initialized Vulkan RHI device");
        return true;
    }

    bool RHIDeviceVulkan::createInstance() {
        std::vector<const char*> layers {
            // Going to use configurator for validation layers -- uncomment if it's not working
            // "VK_LAYER_KHRONOS_validation"
        };

        // add to the required
        platformContext.RequiredInstanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        std::vector<const char*> enabledExtensions {};
        enabledExtensions.reserve(platformContext.RequiredInstanceExtensions.size());

        std::ranges::transform(platformContext.RequiredInstanceExtensions,
                               std::back_inserter(enabledExtensions),
                               [](const std::string& ext) {
                                   return ext.c_str();
                               });

        VkApplicationInfo appInfo {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = platformContext.AppName.c_str(),
            .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
            .pEngineName = platformContext.EngineName.c_str(),
            .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
            .apiVersion = VK_API_VERSION_1_3,
        };

        VkInstanceCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .flags = 0,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(layers.size()),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
            .ppEnabledExtensionNames = enabledExtensions.data(),
        };

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            spdlog::error("Failed to create Vulkan instance");
            return false;
        }

        spdlog::trace("Vulkan instance created");
        volkLoadInstance(instance);
        return true;
    }

    bool RHIDeviceVulkan::createDebugCallback() {

        VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT,
            .pfnUserCallback = &DebugCallback,
            .pUserData = this,
        };

        if (vkCreateDebugUtilsMessengerEXT(instance, &messengerCreateInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            spdlog::error("Failed to create debug messenger");
            return false;
        }

        spdlog::trace("Debug messenger created");
        return true;
    }

    bool RHIDeviceVulkan::createSurface() {
        if (!platformContext.CreateSurfaceFunction(instance, &surface)) {
            spdlog::error("Failed to create surface");
            return false;
        }

        spdlog::trace("Surface created successfully");
        return true;
    }

    bool RHIDeviceVulkan::createDevice() {
        float queuePriorities[] = {1.f};

        VkDeviceQueueCreateInfo queueCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .flags = 0,
            .queueFamilyIndex = physicalDevices.SelectedQueueFamily(),
            .queueCount = 1,
            .pQueuePriorities = queuePriorities,
        };

        std::vector<const char*> deviceExtensions {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
            VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME,
            VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
        };

        if (physicalDevices.SelectedDevice().Features.features.geometryShader == VK_FALSE) {
            spdlog::error("Geometry shaders not supported on selected physical device");
            exit(1);
        }
        // if (physicalDevices.SelectedDevice().Features.features.tessellationShader == VK_FALSE) {
        //     spdlog::error("Tesselation shaders not supported on selected physical device");
        //     exit(1);
        // }

        VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
            .pNext = nullptr,
            .shaderObject = VK_TRUE,
        };

        VkPhysicalDeviceDynamicRenderingFeatures renderingFeatures {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
            .pNext = &shaderObjectFeatures,
            .dynamicRendering = VK_TRUE,
        };

        // TODO: only enable features that are supported by the physical device
        // TODO: only enable things that we actually use -- for example, if we don't use tesselation shaders, don't
        // enable that
        VkPhysicalDeviceFeatures2 deviceFeatures {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &renderingFeatures,
            .features =
                VkPhysicalDeviceFeatures {
                    .geometryShader = VK_TRUE,
                    // .tessellationShader = VK_TRUE,
                },
        };

        const VkDeviceCreateInfo deviceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &deviceFeatures,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
            .ppEnabledExtensionNames = deviceExtensions.data(),
            .pEnabledFeatures = nullptr,
        };

        if (vkCreateDevice(physicalDevices.SelectedDevice().Device, &deviceCreateInfo, nullptr, &device) !=
            VK_SUCCESS) {
            spdlog::error("Failed to create logical device");
            return false;
        }

        spdlog::trace("Logical device created");
        volkLoadDevice(device);
        return true;
    }
} // namespace OZZ::rendering::vk
