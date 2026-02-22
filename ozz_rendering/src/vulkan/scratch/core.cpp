//
// Created by paulm on 2026-02-10.
//

#include <ozz_rendering/scratch/core.h>

#include "volk.h"

#include <iostream>
#include <vector>

#include <spdlog/spdlog.h>

#include "ozz_rendering/scratch/util.h"

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
            spdlog::info(pCallbackData->pMessage);
            break;
        default:
            break;
    }

    return VK_TRUE;
}

OZZ::vk::VulkanCore::VulkanCore() {}

OZZ::vk::VulkanCore::~VulkanCore() {}

void OZZ::vk::VulkanCore::Init(const InitParams& initParams) {
    auto result = volkInitialize();
    CHECK_VK_RESULT(result, "Initialize volk");
    createInstance(initParams.AppName);
    createDebugCallback();
    createSurface(initParams.SurfaceCreationFunction);

    physicalDevices.Init(instance, surface);
    queueFamily = physicalDevices.SelectDevice(VK_QUEUE_GRAPHICS_BIT, true);
    createDevice();
    createSwapchain();
    createCommandBufferPool();

    queue.Init(device, swapchainImages.size(), swapchain, queueFamily, 0);
}

void OZZ::vk::VulkanCore::Shutdown() {
    queue.WaitIdle();
    queue.Destroy();

    if (commandBufferPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, commandBufferPool, nullptr);
        spdlog::info("Destroyed command buffer pool");
        commandBufferPool = VK_NULL_HANDLE;
    }

    for (auto imageView : swapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, imageView, nullptr);
            imageView = VK_NULL_HANDLE;
        }
    }
    swapchainImageViews.clear();

    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        spdlog::info("Swapchain destroyed");
        swapchain = VK_NULL_HANDLE;
    }

    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
        spdlog::info("Logical device destroyed");
        device = VK_NULL_HANDLE;
    }

    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        spdlog::info("Surface destroyed");
        surface = VK_NULL_HANDLE;
    }

    if (debugMessenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        spdlog::info("Vulkan debug messenger destroyed");
        debugMessenger = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        spdlog::info("Vulkan instance destroyed");
        instance = VK_NULL_HANDLE;
    }
}

void OZZ::vk::VulkanCore::CreateCommandBuffers(const uint32_t numberOfCommandBuffers, VkCommandBuffer* commandBuffers) {
    VkCommandBufferAllocateInfo commandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = commandBufferPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = numberOfCommandBuffers,
    };

    const auto result = vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers);
    CHECK_VK_RESULT(result, "Allocate command buffers");
    spdlog::info("{} command buffers created", numberOfCommandBuffers);
}

void OZZ::vk::VulkanCore::FreeCommandBuffers(uint32_t numberOfCommandBuffers, VkCommandBuffer* buffers) {
    queue.WaitIdle();
    vkFreeCommandBuffers(device, commandBufferPool, numberOfCommandBuffers, buffers);
}

uint32_t OZZ::vk::VulkanCore::GetSwapchainImageCount() const {
    return swapchainImages.size();
}

VkImage OZZ::vk::VulkanCore::GetSwapchainImage(const uint32_t imageIndex) const {
    return swapchainImages[imageIndex];
}

VkImageView OZZ::vk::VulkanCore::GetSwapchainImageView(uint32_t imageIndex) const {
    return swapchainImageViews[imageIndex];
}

OZZ::vk::VulkanQueue* OZZ::vk::VulkanCore::GetQueue() {
    return &queue;
}

void OZZ::vk::VulkanCore::createInstance(const std::string& appName) {
    std::vector<const char*> layers {
        // Going to use configurator for validation layers -- uncomment if it's not working
        // "VK_LAYER_KHRONOS_validation"
    };
    std::vector<const char*> enabledExtensions {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
        "VK_KHR_win32_surface",
#endif
#ifdef __APPLE__
        "VK_MVK_macos_surface",
#endif
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

#ifdef __linux__
    // runtime fallback: prefer Wayland if WAYLAND_DISPLAY present, else X11 if DISPLAY present
    if (std::getenv("WAYLAND_DISPLAY")) {
        enabledExtensions.push_back("VK_KHR_wayland_surface");
    } else if (std::getenv("DISPLAY")) {
        enabledExtensions.push_back("VK_KHR_xcb_surface");
    }
#endif

    VkApplicationInfo appInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = appName.c_str(),
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = "OZZ Engine",
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

    const auto result = vkCreateInstance(&createInfo, nullptr, &instance);
    CHECK_VK_RESULT(result, "Create Instance");

    spdlog::info("Vulkan instance created.");

    volkLoadInstance(instance);
}

void OZZ::vk::VulkanCore::createDebugCallback() {
    VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT,
        .pfnUserCallback = &DebugCallback,
        .pUserData = this,
    };

    PFN_vkCreateDebugUtilsMessengerEXT vkCreateUtilsMessenger = VK_NULL_HANDLE;
    vkCreateUtilsMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!vkCreateUtilsMessenger) {
        spdlog::error("Could not find create debug messenger address");
        exit(1);
    }

    auto result = vkCreateUtilsMessenger(instance, &messengerCreateInfo, nullptr, &debugMessenger);
    CHECK_VK_RESULT(result, "Debug messenger creation");
    spdlog::info("Debug messenger created");
}

void OZZ::vk::VulkanCore::createSurface(const std::function<int(VkInstance, VkSurfaceKHR*)>& function) {

    auto result = function(instance, &surface);
    CHECK_VK_RESULT(static_cast<VkResult>(result), "Create surface");
    spdlog::info("Surface created successfully");
}

void OZZ::vk::VulkanCore::createDevice() {
    float queuePriorities[] = {1.f};

    VkDeviceQueueCreateInfo queueCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = queueFamily,
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
    if (physicalDevices.SelectedDevice().Features.features.tessellationShader == VK_FALSE) {
        spdlog::error("Tesselation shaders not supported on selected physical device");
        exit(1);
    }

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
    const auto result = vkCreateDevice(physicalDevices.SelectedDevice().Device, &deviceCreateInfo, nullptr, &device);
    CHECK_VK_RESULT(result, "create logical device");

    spdlog::info("Logical device created");
    volkLoadDevice(device);
}

void OZZ::vk::VulkanCore::createSwapchain() {
    const VkSurfaceCapabilitiesKHR& surfaceCapabilities = physicalDevices.SelectedDevice().SurfaceCapabilities;
    const uint32_t numImages = chooseNumberOfSwapchainImages(surfaceCapabilities);

    const std::vector<VkPresentModeKHR>& presentModes = physicalDevices.SelectedDevice().PresentModes;
    VkPresentModeKHR presentMode = choosePresentMode(presentModes);
    surfaceFormat = chooseSurfaceFormatAndColorSpace(physicalDevices.SelectedDevice().SurfaceFormats);

    VkSwapchainCreateInfoKHR swapchainCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .flags = 0,
        .surface = surface,
        .minImageCount = numImages,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = surfaceCapabilities.currentExtent,
        .imageArrayLayers = 1,
        .imageUsage = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &queueFamily,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
    };

    auto result = vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain);
    CHECK_VK_RESULT(result, "Create swapchain");

    spdlog::info("Swapchain created");

    uint32_t numSwapchainImages = 0;
    result = vkGetSwapchainImagesKHR(device, swapchain, &numSwapchainImages, nullptr);
    CHECK_VK_RESULT(result, "Get swapchain image count");
    if (numImages != numSwapchainImages) {
        spdlog::error("Swapchain image count mismatch!");
    }

    spdlog::info("Num swapchain images: {}", numSwapchainImages);
    swapchainImages.resize(numSwapchainImages);
    swapchainImageViews.resize(numSwapchainImages);

    result = vkGetSwapchainImagesKHR(device, swapchain, &numSwapchainImages, swapchainImages.data());
    CHECK_VK_RESULT(result, "Get swapchain images");

    int layerCount = 1;
    int mipLevels = 1;
    for (auto i = 0; i < numSwapchainImages; i++) {
        swapchainImageViews[i] = createImageView(device,
                                                 swapchainImages[i],
                                                 surfaceFormat.format,
                                                 VK_IMAGE_ASPECT_COLOR_BIT,
                                                 VK_IMAGE_VIEW_TYPE_2D,
                                                 layerCount,
                                                 mipLevels);
    }
}

void OZZ::vk::VulkanCore::createCommandBufferPool() {
    VkCommandPoolCreateInfo commandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = queueFamily,
    };

    const auto result = vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandBufferPool);
    CHECK_VK_RESULT(result, "Create command buffer pool");
    spdlog::info("create command buffer pool");
}

uint32_t OZZ::vk::VulkanCore::chooseNumberOfSwapchainImages(const VkSurfaceCapabilitiesKHR& capabilities) {
    uint32_t requestedNumImages = capabilities.minImageCount + 1;

    uint32_t finalNumberOfImages = 0;
    if (capabilities.maxImageCount > 0 && requestedNumImages > capabilities.maxImageCount) {
        finalNumberOfImages = capabilities.maxImageCount;
    } else {
        finalNumberOfImages = requestedNumImages;
    }

    return finalNumberOfImages;
}

VkPresentModeKHR OZZ::vk::VulkanCore::choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) {
    for (const auto presentMode : presentModes) {
        if (presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return presentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkSurfaceFormatKHR
OZZ::vk::VulkanCore::chooseSurfaceFormatAndColorSpace(const std::vector<VkSurfaceFormatKHR>& surfaceFormats) {
    for (const auto format : surfaceFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return surfaceFormats[0];
}

VkImageView OZZ::vk::VulkanCore::createImageView(VkDevice device,
                                                 VkImage swapchainImage,
                                                 VkFormat format,
                                                 VkImageAspectFlags imageAspectFlags,
                                                 VkImageViewType imageViewType,
                                                 uint32_t layerCount,
                                                 uint32_t mipLevels) {
    VkImageViewCreateInfo imageViewCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = swapchainImage,
        .viewType = imageViewType,
        .format = format,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            {
                .aspectMask = imageAspectFlags,
                .baseMipLevel = 0,
                .levelCount = mipLevels,
                .baseArrayLayer = 0,
                .layerCount = layerCount,
            },
    };

    VkImageView imageView;
    const auto result = vkCreateImageView(device, &imageViewCreateInfo, nullptr, &imageView);
    CHECK_VK_RESULT(result, "create image view");

    return imageView;
}
