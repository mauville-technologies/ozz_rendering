//
// Created by paulm on 2026-02-21.
//

#include "rhi_device_vulkan.h"

#include "utils/initialization.h"

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

    RHIDeviceVulkan::RHIDeviceVulkan(const PlatformContext& context)
        : RHIDevice(context)
        , platformContext(context)
        , texturePool([this](RHITextureVulkan& texture) {
            // no allocation means something else owns this texture, so don't destroy it
            // this is the case for swapchain images, which are owned by the swapchain and just wrapped in a texture for
            // ease of use
            if (texture.Allocation != VK_NULL_HANDLE) {
                vkDestroyImageView(device, texture.ImageView, nullptr);
                vmaDestroyImage(vmaAllocator, texture.Image, texture.Allocation);
                texture.Image = VK_NULL_HANDLE;
                texture.Allocation = VK_NULL_HANDLE;
            }
        })
        , commandBufferResourcePool([this](VkCommandBuffer& commandBuffer) {
            if (commandBufferPool) {
                vkFreeCommandBuffers(device, commandBufferPool, 1, &commandBuffer);
            }
        }) {
        texturePool = ResourcePool<struct TextureTag, RHITextureVulkan>([](RHITextureVulkan& texture) {

        });
        auto result = volkInitialize();
        if (result != VK_SUCCESS) {
            spdlog::error("Failed to initialize volk");
            return;
        }

        bIsValid = initialize();
    }

    RHIDeviceVulkan::~RHIDeviceVulkan() {
        if (commandBufferPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandBufferPool, nullptr);
            spdlog::trace("Destroyed command buffer pool");
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
            spdlog::trace("Swapchain destroyed");
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }

        if (vmaAllocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(vmaAllocator);
            spdlog::trace("VMA allocator destroyed");
            vmaAllocator = VK_NULL_HANDLE;
        }

        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
            spdlog::trace("Logical device destroyed");
            device = VK_NULL_HANDLE;
        }

        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            spdlog::trace("Surface destroyed");
            surface = VK_NULL_HANDLE;
        }

        if (debugMessenger != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
            spdlog::trace("Vulkan debug messenger destroyed");
            debugMessenger = VK_NULL_HANDLE;
        }

        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
            spdlog::trace("Vulkan instance destroyed");
            instance = VK_NULL_HANDLE;
        }

        spdlog::info("Tore down Vulkan RHI device");
    }

    RHITextureHandle RHIDeviceVulkan::CreateTexture() {
        return RHITextureHandle::Null();
    }

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

        VmaVulkanFunctions vulkanFunctions {
            .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
            .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
            .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
            .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
            .vkAllocateMemory = vkAllocateMemory,
            .vkFreeMemory = vkFreeMemory,
            .vkMapMemory = vkMapMemory,
            .vkUnmapMemory = vkUnmapMemory,
            .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
            .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
            .vkBindBufferMemory = vkBindBufferMemory,
            .vkBindImageMemory = vkBindImageMemory,
            .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
            .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
            .vkCreateBuffer = vkCreateBuffer,
            .vkDestroyBuffer = vkDestroyBuffer,
            .vkCreateImage = vkCreateImage,
            .vkDestroyImage = vkDestroyImage,
            .vkCmdCopyBuffer = vkCmdCopyBuffer,
            .vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2,
            .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2,
            .vkBindBufferMemory2KHR = vkBindBufferMemory2,
            .vkBindImageMemory2KHR = vkBindImageMemory2,
            .vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2,
            .vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements,
            .vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements,
        };

        VmaAllocatorCreateInfo allocatorCreateInfo {
            .flags = 0,
            .physicalDevice = physicalDevices.SelectedDevice().Device,
            .device = device,
            .pVulkanFunctions = &vulkanFunctions,
            .instance = instance,
            .vulkanApiVersion = VK_API_VERSION_1_3,
        };
        if (const auto result = vmaCreateAllocator(&allocatorCreateInfo, &vmaAllocator); result != VK_SUCCESS) {
            spdlog::error("Failed to create VMA allocator for Vulkan RHI. Error: {}", static_cast<int>(result));
            failureMessage();
            return false;
        }

        if (!createSwapchain()) {
            failureMessage();
            return false;
        }

        if (!createCommandBufferPool()) {
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

    bool RHIDeviceVulkan::createSwapchain() {
        const VkSurfaceCapabilitiesKHR& surfaceCapabilities = physicalDevices.SelectedDevice().SurfaceCapabilities;
        const uint32_t numImages = ChooseNumberOfSwapchainImages(surfaceCapabilities);

        const std::vector<VkPresentModeKHR>& presentModes = physicalDevices.SelectedDevice().PresentModes;

        // TODO: @paulm -- make the present mode a parameter that can be configured, and don't default to IMMEDIATE
        VkPresentModeKHR presentMode = ChoosePresentMode(presentModes, VK_PRESENT_MODE_IMMEDIATE_KHR);

        swapchainSurfaceFormat = ChooseSurfaceFormatAndColorSpace(physicalDevices.SelectedDevice().SurfaceFormats);

        auto queueFamily = physicalDevices.SelectedQueueFamily();
        VkSwapchainCreateInfoKHR swapchainCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .flags = 0,
            .surface = surface,
            .minImageCount = numImages,
            .imageFormat = swapchainSurfaceFormat.format,
            .imageColorSpace = swapchainSurfaceFormat.colorSpace,
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

        if (const auto result = vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain);
            result != VK_SUCCESS) {
            spdlog::error("Failed to create swapchain, error code: {}", static_cast<int>(result));
            return false;
        }

        spdlog::trace("Swapchain created");

        uint32_t numSwapchainImages = 0;
        if (const auto result = vkGetSwapchainImagesKHR(device, swapchain, &numSwapchainImages, nullptr);
            result != VK_SUCCESS) {
            spdlog::error("Failed to get swapchain image count, error code: {}", static_cast<int>(result));
            return false;
        }

        if (numImages != numSwapchainImages) {
            spdlog::error("Swapchain image count mismatch!");
        }

        spdlog::trace("Num swapchain images: {}", numSwapchainImages);
        swapchainImages.resize(numSwapchainImages);
        swapchainImageViews.resize(numSwapchainImages);

        if (const auto result = vkGetSwapchainImagesKHR(device, swapchain, &numSwapchainImages, swapchainImages.data());
            result != VK_SUCCESS) {
            spdlog::error("Failed to get swapchain images, error code: {}", static_cast<int>(result));
            return false;
        }

        for (auto i = 0; i < numSwapchainImages; i++) {
            constexpr int mipLevels = 1;
            constexpr int layerCount = 1;
            const auto optIV = CreateImageView(device,
                                               swapchainImages[i],
                                               swapchainSurfaceFormat.format,
                                               VK_IMAGE_ASPECT_COLOR_BIT,
                                               VK_IMAGE_VIEW_TYPE_2D,
                                               layerCount,
                                               mipLevels);
            if (!optIV.has_value()) {
                spdlog::error("Failed to create image view for swapchain image {}", i);
                return false;
            }

            swapchainImageViews[i] = optIV.value();
        }
        return true;
    }

    bool RHIDeviceVulkan::createCommandBufferPool() {
        VkCommandPoolCreateInfo commandPoolCreateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = physicalDevices.SelectedQueueFamily(),
        };

        if (const auto result = vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandBufferPool);
            result != VK_SUCCESS) {
            spdlog::error("Failed to create command buffer pool, error code: {}", static_cast<int>(result));
            return false;
        };

        spdlog::trace("created command buffer pool");
        return true;
    }
} // namespace OZZ::rendering::vk
