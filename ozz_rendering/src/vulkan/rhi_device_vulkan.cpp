//
// Created by paulm on 2026-02-21.
//

#include "rhi_device_vulkan.h"

#include "utils/initialization.h"
#include "utils/rhi_vulkan_types.h"

#include <algorithm>
#include <fstream>
#include <ranges>
#include <spdlog/spdlog.h>

namespace OZZ::rendering::vk {
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT type,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                        void* pUserData) {

        const char* msg = pCallbackData && pCallbackData->pMessage ? pCallbackData->pMessage : "<null Vulkan message>";

        switch (severity) {
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                spdlog::error("{}", msg);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
                spdlog::trace("{}", msg);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                spdlog::warn("{}", msg);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
                spdlog::info("{}", msg);
                break;
            default:
                break;
        }

        return VK_TRUE;
    }

    // ============================================================
    // === Constructor / Destructor ===
    // ============================================================

    RHIDeviceVulkan::RHIDeviceVulkan(const PlatformContext& context)
        : RHIDevice(context)
        , platformContext(context)
        , texturePool([this](RHITextureVulkan& texture) {
            // no allocation means something else owns this texture, so don't destroy it
            // this is the case for swapchain images, which are owned by the swapchain and just wrapped in a texture for
            // ease of use
            if (texture.Allocation != VK_NULL_HANDLE) {
                vkDestroySampler(device, texture.Sampler, nullptr);
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
        })
        , shaderResourcePool([this](RHIShaderVulkan& shader) {
            pipelineLayoutResourcePool.Free(shader.pipelineLayoutHandle);
            for (const auto& handle : shader.descriptorSetLayoutHandles) {
                descriptorSetLayoutResourcePool.Free(handle);
            }
            shader.Destroy(device);
        })
        , bufferResourcePool([this](const std::array<RHIBufferVulkan, MaxFramesInFlight>& buffer) {
            for (const auto buffer : buffer) {
                if (buffer.Buffer != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(vmaAllocator, buffer.Buffer, buffer.Allocation);
                }
            }
        })
        , pipelineLayoutResourcePool([this](VkPipelineLayout& layout) {
            if (layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, layout, nullptr);
                layout = VK_NULL_HANDLE;
            }
        })
        , descriptorSetLayoutResourcePool([this](VkDescriptorSetLayout& layout) {
            if (layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, layout, nullptr);
                layout = VK_NULL_HANDLE;
            }
        })
        , descriptorSetResourcePool([this](VkDescriptorSet& set) {
            if (set != VK_NULL_HANDLE && descriptorPool != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(device, descriptorPool, 1, &set);
                set = VK_NULL_HANDLE;
            }
        }) {

        auto result = volkInitialize();
        if (result != VK_SUCCESS) {
            spdlog::error("Failed to initialize volk");
            return;
        }

        bIsValid = initialize();
    }

    RHIDeviceVulkan::~RHIDeviceVulkan() {
        if (graphicsQueue != VK_NULL_HANDLE) {
            vkQueueWaitIdle(graphicsQueue);
            graphicsQueue = VK_NULL_HANDLE;
        }

        for (const auto buffer : transientCommandBuffers) {
            vkFreeCommandBuffers(device, transientCommandBufferPool, 1, &buffer);
        }
        transientCommandBuffers.clear();
        commandBufferResourcePool.Empty();
        shaderResourcePool.Empty();
        descriptorSetResourcePool.Empty();
        pipelineLayoutResourcePool.Empty();
        descriptorSetLayoutResourcePool.Empty();
        bufferResourcePool.Empty();
        texturePool.Empty();

        for (auto& context : submissionContexts) {
            if (context.InFlightFence != VK_NULL_HANDLE) {
                vkDestroyFence(device, context.InFlightFence, nullptr);
                context.InFlightFence = VK_NULL_HANDLE;
            }
            if (context.AcquireImageSemaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, context.AcquireImageSemaphore, nullptr);
                context.AcquireImageSemaphore = VK_NULL_HANDLE;
            }
        }

        submissionContexts.clear();
        spdlog::trace("cleared submission contexts");

        if (commandBufferPool != VK_NULL_HANDLE) {
            vkResetCommandPool(device, commandBufferPool, 0);
            vkDestroyCommandPool(device, commandBufferPool, nullptr);
            spdlog::trace("Destroyed command buffer pool");
            commandBufferPool = VK_NULL_HANDLE;
        }

        if (transientCommandBufferPool != VK_NULL_HANDLE) {
            vkResetCommandPool(device, transientCommandBufferPool, 0);
            vkDestroyCommandPool(device, transientCommandBufferPool, nullptr);
            spdlog::trace("Destroyed transient command buffer pool");
            transientCommandBufferPool = VK_NULL_HANDLE;
        }

        for (auto semaphore : presentCompleteSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
                semaphore = VK_NULL_HANDLE;
            }
        }
        presentCompleteSemaphores.clear();

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

        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            spdlog::trace("Descriptor pool destroyed");
            descriptorPool = VK_NULL_HANDLE;
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

    // ============================================================
    // === Initialization ===
    // ============================================================

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

        if (!createCommandBufferPool()) {
            failureMessage();
            return false;
        }

        if (!initializeQueue()) {
            failureMessage();
            return false;
        }

        if (!createSwapchain()) {
            failureMessage();
            return false;
        }

        if (!createSubmissionContexts()) {
            failureMessage();
            return false;
        }

        if (!createDescriptorPool()) {
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

        VkPhysicalDeviceSynchronization2Features synchronization2Features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
            .pNext = nullptr,
            .synchronization2 = VK_TRUE,
        };

        VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
            .pNext = &synchronization2Features,
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
                    .samplerAnisotropy = VK_TRUE,
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

    bool RHIDeviceVulkan::createSwapchain(VkSwapchainKHR oldSwapchain) {
        const VkSurfaceCapabilitiesKHR& surfaceCapabilities = physicalDevices.SelectedDevice().SurfaceCapabilities;
        const uint32_t numImages = ChooseNumberOfSwapchainImages(surfaceCapabilities);

        const std::vector<VkPresentModeKHR>& presentModes = physicalDevices.SelectedDevice().PresentModes;

        // TODO: @paulm -- make the present mode a parameter that can be configured, and don't default to IMMEDIATE
        VkPresentModeKHR presentMode = ChoosePresentMode(presentModes, VK_PRESENT_MODE_IMMEDIATE_KHR);

        swapchainSurfaceFormat = ChooseSurfaceFormatAndColorSpace(physicalDevices.SelectedDevice().SurfaceFormats);
        swapchainExtent = surfaceCapabilities.currentExtent;

        auto queueFamily = physicalDevices.SelectedQueueFamily();
        VkSwapchainCreateInfoKHR swapchainCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .flags = 0,
            .surface = surface,
            .minImageCount = numImages,
            .imageFormat = swapchainSurfaceFormat.format,
            .imageColorSpace = swapchainSurfaceFormat.colorSpace,
            .imageExtent = swapchainExtent,
            .imageArrayLayers = 1,
            .imageUsage = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = &queueFamily,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = presentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = oldSwapchain,
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

        // Let's put these in the texturepool
        presentCompleteSemaphores.resize(numSwapchainImages);
        for (auto i = 0; i < numSwapchainImages; i++) {
            RHITextureVulkan texture {
                .Image = swapchainImages[i],
                .ImageView = swapchainImageViews[i],
                .Allocation = VK_NULL_HANDLE,
            };
            const auto handle = texturePool.Allocate(std::move(texture));
            if (!handle.IsValid()) {
                spdlog::error("Failed to allocate texture for swapchain image {}", i);
                return false;
            }

            swapchainTextureHandles.push_back(handle);

            VkSemaphoreCreateInfo semaphoreCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
            };

            if (const auto result =
                    vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &presentCompleteSemaphores[i]);
                result != VK_SUCCESS) {
                spdlog::error("Failed to create present complete semaphore for swapchain image {}, error code: {}",
                              i,
                              static_cast<int>(result));
                return false;
            }

            // Create the depth images
            swapchainDepthTextureHandles.emplace_back(CreateTexture({
                .Width = swapchainExtent.width,
                .Height = swapchainExtent.height,
                .Format = TextureFormat::D24S8,
                .Usage = TextureUsage::DepthAttachment,
            }));
        }

        return true;
    }

    bool RHIDeviceVulkan::createCommandBufferPool() {
        VkCommandPoolCreateInfo commandPoolCreateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = physicalDevices.SelectedQueueFamily(),
        };

        if (const auto result = vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandBufferPool);
            result != VK_SUCCESS) {
            spdlog::error("Failed to create command buffer pool, error code: {}", static_cast<int>(result));
            return false;
        };

        if (const auto result =
                vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &transientCommandBufferPool);
            result != VK_SUCCESS) {
            spdlog::error("Failed to create transient command buffer pool, error code: {}", static_cast<int>(result));
            return false;
        }

        spdlog::trace("created command buffer pools");
        return true;
    }

    bool RHIDeviceVulkan::createSubmissionContexts() {
        framesInFlight = std::min(MaxFramesInFlight, static_cast<uint32_t>(swapchainImages.size()));

        submissionContexts.resize(framesInFlight);

        std::vector<VkCommandBuffer> commandBuffers(framesInFlight);
        VkCommandBufferAllocateInfo commandBufferAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandBufferPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = framesInFlight,
        };

        if (const auto result = vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers.data());
            result != VK_SUCCESS) {
            spdlog::error("Failed to allocate command buffers for submission contexts, error code: {}",
                          static_cast<int>(result));
            return false;
        }

        for (auto i = 0U; i < framesInFlight; i++) {
            VkSemaphoreCreateInfo semaphoreCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
            };
            if (const auto result = vkCreateSemaphore(device,
                                                      &semaphoreCreateInfo,
                                                      nullptr,
                                                      &submissionContexts[i].AcquireImageSemaphore);
                result != VK_SUCCESS) {
                spdlog::error("Failed to create present complete semaphore for submission context {}, error code: {}",
                              i,
                              static_cast<int>(result));
                return false;
            }

            VkFenceCreateInfo fenceCreateInfo {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            };

            if (const auto result =
                    vkCreateFence(device, &fenceCreateInfo, nullptr, &submissionContexts[i].InFlightFence);
                result != VK_SUCCESS) {
                spdlog::error("Failed to create render fence for submission context {}, error code: {}",
                              i,
                              static_cast<int>(result));
                return false;
            }

            auto handle = commandBufferResourcePool.Allocate(std::move(commandBuffers[i]));
            if (!handle.IsValid()) {
                spdlog::error("Failed to allocate command buffer for submission context {}", i);
                return false;
            }
            submissionContexts[i].CommandBuffer = handle;
        }
        return true;
    }

    bool RHIDeviceVulkan::initializeQueue() {
        vkGetDeviceQueue(device, physicalDevices.SelectedQueueFamily(), 0, &graphicsQueue);
        return true;
    }

    bool RHIDeviceVulkan::createDescriptorPool() {
        constexpr uint32_t PoolSize = 1000;
        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, PoolSize},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PoolSize},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PoolSize},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, PoolSize},
            {VK_DESCRIPTOR_TYPE_SAMPLER, PoolSize},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, PoolSize},
        };

        const VkDescriptorPoolCreateInfo poolCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = PoolSize,
            .poolSizeCount = static_cast<uint32_t>(std::size(poolSizes)),
            .pPoolSizes = poolSizes,
        };

        if (const auto result = vkCreateDescriptorPool(device, &poolCreateInfo, nullptr, &descriptorPool);
            result != VK_SUCCESS) {
            spdlog::error("Failed to create descriptor pool. Error: {}", static_cast<int>(result));
            return false;
        }

        spdlog::trace("Descriptor pool created");
        return true;
    }

    void RHIDeviceVulkan::destroySwapchainResources() {
        for (const auto handle : swapchainDepthTextureHandles) {
            FreeTexture(handle);
        }
        swapchainDepthTextureHandles.clear();

        for (const auto handle : swapchainTextureHandles) {
            texturePool.Free(handle);
        }
        swapchainTextureHandles.clear();

        for (auto semaphore : presentCompleteSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
        }
        presentCompleteSemaphores.clear();

        for (auto imageView : swapchainImageViews) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, imageView, nullptr);
            }
        }
        swapchainImageViews.clear();
        swapchainImages.clear();
    }

    bool RHIDeviceVulkan::recreateSwapchain() {
        // Don't recreate while the window is minimized (framebuffer is 0x0).
        if (platformContext.GetWindowFramebufferSizeFunction) {
            auto [w, h] = platformContext.GetWindowFramebufferSizeFunction();
            if (w == 0 || h == 0) {
                return true;
            }
        }

        // Wait for all in-flight work to complete before touching any resources.
        vkDeviceWaitIdle(device);

        if (!physicalDevices.RefreshSurfaceCapabilities(surface)) {
            spdlog::error("Failed to refresh surface capabilities during swapchain recreation");
            return false;
        }

        destroySwapchainResources();

        VkSwapchainKHR oldSwapchain = swapchain;
        swapchain = VK_NULL_HANDLE;

        if (!createSwapchain(oldSwapchain)) {
            spdlog::error("Failed to recreate swapchain");
            if (oldSwapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
            }
            return false;
        }

        if (oldSwapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
        }

        spdlog::info("Swapchain recreated ({}x{})", swapchainExtent.width, swapchainExtent.height);
        return true;
    }

    std::pair<uint32_t, uint32_t> RHIDeviceVulkan::GetSwapchainExtent() const {
        return {swapchainExtent.width, swapchainExtent.height};
    }

    VkCommandBuffer RHIDeviceVulkan::beginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = transientCommandBufferPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer commandBuffer;
        if (const auto result = vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer); result != VK_SUCCESS) {
            spdlog::error("Failed to allocate command buffer for single time commands. Error: {}",
                          static_cast<int>(result));
            return VK_NULL_HANDLE;
        }

        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };

        if (const auto result = vkBeginCommandBuffer(commandBuffer, &beginInfo); result != VK_SUCCESS) {
            spdlog::error("Failed to begin command buffer for single time commands. Error: {}",
                          static_cast<int>(result));
            return VK_NULL_HANDLE;
        }

        transientCommandBuffers.insert(commandBuffer);
        return commandBuffer;
    }

    void RHIDeviceVulkan::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
        const auto cleanCommandBuffer = [this](VkCommandBuffer cmdBuffer) {
            const auto commandBufferCopy = cmdBuffer;
            vkFreeCommandBuffers(device, transientCommandBufferPool, 1, &cmdBuffer);
            transientCommandBuffers.erase(commandBufferCopy);
        };

        if (const auto result = vkEndCommandBuffer(commandBuffer); result != VK_SUCCESS) {
            spdlog::error("Failed to end command buffer for single time commands. Error: {}", static_cast<int>(result));
            cleanCommandBuffer(commandBuffer);
            return;
        }

        VkSubmitInfo submitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
        };

        // create a fence
        VkFenceCreateInfo fenceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        };

        VkFence submitFence {VK_NULL_HANDLE};
        if (const auto result = vkCreateFence(device, &fenceCreateInfo, nullptr, &submitFence); result != VK_SUCCESS) {
            spdlog::error("Failed to create fence for single time command submission. Error: {}",
                          static_cast<int>(result));
            // destroy the command buffer before returning
            cleanCommandBuffer(commandBuffer);
            return;
        }

        if (const auto result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, submitFence); result != VK_SUCCESS) {
            spdlog::error("Failed to submit command buffer for single time commands. Error: {}",
                          static_cast<int>(result));
            vkDestroyFence(device, submitFence, nullptr);
            cleanCommandBuffer(commandBuffer);
            return;
        }

        vkWaitForFences(device, 1, &submitFence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, submitFence, nullptr);

        cleanCommandBuffer(commandBuffer);
    }

    // ============================================================
    // === Frame ===
    // ============================================================

    RHIFrameContext RHIDeviceVulkan::BeginFrame() {
        const auto& submissionContext = submissionContexts[currentFrame];
        if (const auto fenceResult = vkWaitForFences(device, 1, &submissionContext.InFlightFence, VK_TRUE, UINT64_MAX);
            fenceResult != VK_SUCCESS) {
            spdlog::error("Failed to wait for fence in BeginFrame. Error: {}", static_cast<int>(fenceResult));
            return RHIFrameContext::Null();
        }

        // Clear per-frame deletion queue now that the GPU has finished with this slot.
        for (const auto& deletionFunc : perFrameDeletions[currentFrame]) {
            deletionFunc();
        }
        perFrameDeletions[currentFrame].clear();

        uint32_t imageIndex;
        VkResult acquireResult = vkAcquireNextImageKHR(device,
                                                       swapchain,
                                                       UINT64_MAX,
                                                       submissionContext.AcquireImageSemaphore,
                                                       VK_NULL_HANDLE,
                                                       &imageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            // Fence is still signaled (we haven't reset it yet), so the next BeginFrame
            // will not deadlock waiting on it.
            if (!recreateSwapchain()) {
                return RHIFrameContext::Null();
            }
            // Retry acquire on the new swapchain.
            acquireResult = vkAcquireNextImageKHR(device,
                                                  swapchain,
                                                  UINT64_MAX,
                                                  submissionContext.AcquireImageSemaphore,
                                                  VK_NULL_HANDLE,
                                                  &imageIndex);
        }

        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            spdlog::error("Failed to acquire next image in BeginFrame. Error: {}", static_cast<int>(acquireResult));
            // Fence is still signaled — no deadlock on the next frame.
            return RHIFrameContext::Null();
        }

        // Commit to this frame: reset the fence only now that we will submit work.
        vkResetFences(device, 1, &submissionContext.InFlightFence);

        const auto commandBuffer = commandBufferResourcePool.Get(submissionContext.CommandBuffer);

        VkCommandBufferBeginInfo VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };

        if (const auto result = vkBeginCommandBuffer(*commandBuffer, &VkCommandBufferBeginInfo); result != VK_SUCCESS) {
            spdlog::error("Failed to begin command buffer in BeginFrame. Error: {}", static_cast<int>(result));
            return RHIFrameContext::Null();
        }

        auto frameContext = BuildFrameContext(submissionContext.CommandBuffer,
                                              swapchainTextureHandles[imageIndex],
                                              swapchainDepthTextureHandles[imageIndex],
                                              imageIndex,
                                              currentFrame);
        TextureResourceBarrier(frameContext,
                               TextureBarrierDescriptor {
                                   .Texture = swapchainTextureHandles[imageIndex],
                                   .OldLayout = TextureLayout::Undefined,
                                   .NewLayout = TextureLayout::ColorAttachment,
                                   .SrcStage = PipelineStage::ColorAttachmentOutput,
                                   .DstStage = PipelineStage::ColorAttachmentOutput,
                                   .SrcAccess = Access::None,
                                   .DstAccess = Access::ColorAttachmentWrite,
                               });

        return std::move(frameContext);
    }

    void RHIDeviceVulkan::SubmitAndPresentFrame(RHIFrameContext&& frameContext) {
        // Prepare swapchain image for presentation
        const auto imageIndex = GetImageIndexFromFrameContext(frameContext);

        TextureResourceBarrier(frameContext,
                               TextureBarrierDescriptor {
                                   .Texture = swapchainTextureHandles[imageIndex],
                                   .OldLayout = TextureLayout::ColorAttachment,
                                   .NewLayout = TextureLayout::Present,
                                   .SrcStage = PipelineStage::ColorAttachmentOutput,
                                   .DstStage = PipelineStage::None,
                                   .SrcAccess = Access::ColorAttachmentWrite,
                                   .DstAccess = Access::None,
                               });

        auto commandBuffer = commandBufferResourcePool.Get(frameContext.GetCommandBuffer());
        if (const auto result = vkEndCommandBuffer(*commandBuffer); result != VK_SUCCESS) {
            spdlog::error("Failed to end command buffer in SubmitFrame. Error: {}", static_cast<int>(result));
            return;
        }

        const auto frameNumber = GetFrameNumberFromFrameContext(frameContext);
        auto& submissionContext = submissionContexts[frameNumber];
        VkPipelineStageFlags waitFlags {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo submitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &submissionContext.AcquireImageSemaphore,
            .pWaitDstStageMask = &waitFlags,
            .commandBufferCount = 1,
            .pCommandBuffers = commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &presentCompleteSemaphores[imageIndex],
        };

        if (const auto result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, submissionContext.InFlightFence)) {
            spdlog::error("Failed to submit command buffer in SubmitFrame. Error: {} | {} / {} | {:x}",
                          static_cast<int>(result),
                          imageIndex,
                          frameNumber,
                          reinterpret_cast<uint64_t>(presentCompleteSemaphores[imageIndex]));
            return;
        }

        VkPresentInfoKHR presentInfo {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presentCompleteSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex,
            .pResults = VK_NULL_HANDLE,
        };

        if (const auto result = vkQueuePresentKHR(graphicsQueue, &presentInfo);
            result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain();
        } else if (result != VK_SUCCESS) {
            spdlog::error("Failed to present frame in SubmitFrame. Error: {}", static_cast<int>(result));
        }

        currentFrame = (currentFrame + 1) % framesInFlight;
    }

    // ============================================================
    // === Command Buffer Recording - Render Pass ===
    // ============================================================

    void RHIDeviceVulkan::BeginRenderPass(const RHIFrameContext& frameContext,
                                          const RenderPassDescriptor& renderPassDescriptor) {
        beginRenderPassInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()), renderPassDescriptor);
    }

    void RHIDeviceVulkan::beginRenderPassInternal(VkCommandBuffer cmd,
                                                  const RenderPassDescriptor& renderPassDescriptor) {
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        bool bHasDepthAttachment = false;
        bool bHasStencilAttachment = false;
        VkRenderingAttachmentInfo depthAttachment;
        VkRenderingAttachmentInfo stencilAttachment;

        for (auto i = 0u; i < renderPassDescriptor.ColorAttachmentCount; i++) {
            const auto& attachment = renderPassDescriptor.ColorAttachments[i];
            if (attachment.Texture == RHITextureHandle::Null()) {
                spdlog::error("Color attachment {} is null in BeginRenderPass", i);
                continue;
            }
            VkClearValue clearValue;
            const auto* texture = texturePool.Get(attachment.Texture);
            if (attachment.Layout == TextureLayout::ColorAttachment) {
                clearValue.color = {
                    attachment.Clear.R,
                    attachment.Clear.G,
                    attachment.Clear.B,
                    attachment.Clear.A,
                };
            }

            VkRenderingAttachmentInfo attachmentInfo {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .pNext = nullptr,
                .imageView = texture->ImageView,
                .imageLayout = ConvertTextureLayoutToVulkan(attachment.Layout),
                .loadOp = ConvertLoadOpToVulkan(attachment.Load),
                .storeOp = ConvertStoreOpToVulkan(attachment.Store),
                .clearValue = clearValue,
            };
            colorAttachments.emplace_back(attachmentInfo);
        }

        if (renderPassDescriptor.DepthAttachment.Texture != RHITextureHandle::Null()) {
            bHasDepthAttachment = true;
            const auto* texture = texturePool.Get(renderPassDescriptor.DepthAttachment.Texture);
            VkClearValue clearValue;
            clearValue.depthStencil.depth = renderPassDescriptor.DepthAttachment.Clear.Depth;
            depthAttachment = VkRenderingAttachmentInfo {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .pNext = nullptr,
                .imageView = texture->ImageView,
                .imageLayout = ConvertTextureLayoutToVulkan(renderPassDescriptor.DepthAttachment.Layout),
                .loadOp = ConvertLoadOpToVulkan(renderPassDescriptor.DepthAttachment.Load),
                .storeOp = ConvertStoreOpToVulkan(renderPassDescriptor.DepthAttachment.Store),
                .clearValue = clearValue,
            };
        }

        if (renderPassDescriptor.StencilAttachment.Texture != RHITextureHandle::Null()) {
            bHasStencilAttachment = true;
            const auto* texture = texturePool.Get(renderPassDescriptor.StencilAttachment.Texture);
            VkClearValue clearValue;
            clearValue.depthStencil.stencil = renderPassDescriptor.StencilAttachment.Clear.Stencil;
            stencilAttachment = VkRenderingAttachmentInfo {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .pNext = nullptr,
                .imageView = texture->ImageView,
                .imageLayout = ConvertTextureLayoutToVulkan(renderPassDescriptor.StencilAttachment.Layout),
                .loadOp = ConvertLoadOpToVulkan(renderPassDescriptor.StencilAttachment.Load),
                .storeOp = ConvertStoreOpToVulkan(renderPassDescriptor.StencilAttachment.Store),
                .clearValue = clearValue,
            };
        }

        VkRenderingInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderArea =
                {
                    .offset =
                        {
                            .x = renderPassDescriptor.RenderArea.X,
                            .y = renderPassDescriptor.RenderArea.Y,
                        },
                    .extent =
                        {
                            .width = renderPassDescriptor.RenderArea.Width,
                            .height = renderPassDescriptor.RenderArea.Height,
                        },
                },
            .layerCount = 1,
            .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
            .pColorAttachments = colorAttachments.data(),
            .pDepthAttachment = bHasDepthAttachment ? &depthAttachment : nullptr,
            .pStencilAttachment = bHasStencilAttachment ? &stencilAttachment
                                  : bHasDepthAttachment ? &depthAttachment
                                                        : nullptr,
        };

        vkCmdBeginRendering(cmd, &renderingInfo);
    }

    void RHIDeviceVulkan::EndRenderPass(const RHIFrameContext& frameContext) {
        endRenderPassInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()));
    }

    void RHIDeviceVulkan::endRenderPassInternal(VkCommandBuffer cmd) {
        vkCmdEndRendering(cmd);
    }

    // ============================================================
    // === Command Buffer Recording - Barriers ===
    // ============================================================

    void RHIDeviceVulkan::TextureResourceBarrier(const RHIFrameContext& frameContext,
                                                 const TextureBarrierDescriptor& barrierDescriptor) {
        textureResourceBarrierInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()),
                                       barrierDescriptor);
    }

    void RHIDeviceVulkan::textureResourceBarrierInternal(VkCommandBuffer cmd,
                                                         const TextureBarrierDescriptor& barrierDescriptor) {
        VkImageMemoryBarrier2 imageMemoryBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = ConvertPipelineStageToVulkan(barrierDescriptor.SrcStage),
            .srcAccessMask = ConvertAccessToVulkan(barrierDescriptor.SrcAccess),
            .dstStageMask = ConvertPipelineStageToVulkan(barrierDescriptor.DstStage),
            .dstAccessMask = ConvertAccessToVulkan(barrierDescriptor.DstAccess),
            .oldLayout = ConvertTextureLayoutToVulkan(barrierDescriptor.OldLayout),
            .newLayout = ConvertTextureLayoutToVulkan(barrierDescriptor.NewLayout),
            .srcQueueFamilyIndex = barrierDescriptor.SrcQueueFamily == QueueFamilyIgnored
                                       ? VK_QUEUE_FAMILY_IGNORED
                                       : static_cast<uint32_t>(barrierDescriptor.SrcQueueFamily),
            .dstQueueFamilyIndex = barrierDescriptor.DstQueueFamily == QueueFamilyIgnored
                                       ? VK_QUEUE_FAMILY_IGNORED
                                       : static_cast<uint32_t>(barrierDescriptor.DstQueueFamily),
            .image = texturePool.Get(barrierDescriptor.Texture)->Image,
            .subresourceRange =
                {
                    .aspectMask = ConvertTextureAspectToVulkan(barrierDescriptor.SubresourceRange.Aspect),
                    .baseMipLevel = barrierDescriptor.SubresourceRange.BaseMipLevel,
                    .levelCount = barrierDescriptor.SubresourceRange.LevelCount,
                    .baseArrayLayer = barrierDescriptor.SubresourceRange.BaseArrayLayer,
                    .layerCount = barrierDescriptor.SubresourceRange.LayerCount,
                },
        };
        VkDependencyInfo barrierDependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imageMemoryBarrier, // to be filled out based on the TextureBarrierDescriptor
        };

        vkCmdPipelineBarrier2(cmd, &barrierDependency);
    }

    void RHIDeviceVulkan::BufferMemoryBarrier(const RHIFrameContext& frameContext,
                                              const BufferBarrierDescriptor& barrierDescriptor) {
        bufferMemoryBarrierInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()), barrierDescriptor);
    }

    void RHIDeviceVulkan::bufferMemoryBarrierInternal(VkCommandBuffer /*cmd*/,
                                                      const BufferBarrierDescriptor& /*barrierDescriptor*/) {
        assert(false && "Buffer memory barriers not implemented!!");
    }

    // ============================================================
    // === Command Buffer Recording - State ===
    // ============================================================

    void RHIDeviceVulkan::SetViewport(const RHIFrameContext& frameContext, const Viewport& viewport) {
        setViewportInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()), viewport);
    }

    void RHIDeviceVulkan::setViewportInternal(VkCommandBuffer cmd, const Viewport& viewport) {
        // Flip Y to convert from Vulkan NDC (Y+ down) to OpenGL NDC (Y+ up),
        // keeping GLM projection matrices correct without any shader changes.
        VkViewport vkViewport {
            .x = viewport.X,
            .y = viewport.Y + viewport.Height,
            .width = viewport.Width,
            .height = -viewport.Height,
            .minDepth = viewport.MinDepth,
            .maxDepth = viewport.MaxDepth,
        };
        vkCmdSetViewportWithCount(cmd, 1, &vkViewport);
    }

    void RHIDeviceVulkan::SetScissor(const RHIFrameContext& frameContext, const Scissor& scissor) {
        setScissorInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()), scissor);
    }

    void RHIDeviceVulkan::setScissorInternal(VkCommandBuffer cmd, const Scissor& scissor) {
        VkRect2D vkScissor {
            .offset =
                {
                    .x = static_cast<int32_t>(scissor.X),
                    .y = static_cast<int32_t>(scissor.Y),
                },
            .extent =
                {
                    .width = scissor.Width,
                    .height = scissor.Height,
                },
        };
        vkCmdSetScissorWithCount(cmd, 1, &vkScissor);
    }

    void RHIDeviceVulkan::SetGraphicsState(const RHIFrameContext& frameContext,
                                           const GraphicsStateDescriptor& graphicsStateDescriptor) {
        setGraphicsStateInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()),
                                 graphicsStateDescriptor);
    }

    void RHIDeviceVulkan::setGraphicsStateInternal(VkCommandBuffer cmd,
                                                   const GraphicsStateDescriptor& graphicsStateDescriptor) {
        // Input Assembly
        vkCmdSetPrimitiveTopology(cmd,
                                  ConvertPrimitiveTopologyToVulkan(graphicsStateDescriptor.InputAssembly.Topology));
        vkCmdSetPrimitiveRestartEnable(cmd, graphicsStateDescriptor.InputAssembly.PrimitiveRestartEnable);

        // Rasterization
        vkCmdSetCullMode(cmd, ConvertCullModeToVulkan(graphicsStateDescriptor.Rasterization.Cull));
        vkCmdSetFrontFace(cmd, ConvertFrontFaceToVulkan(graphicsStateDescriptor.Rasterization.Front));
        vkCmdSetPolygonModeEXT(cmd, ConvertPolygonModeToVulkan(graphicsStateDescriptor.Rasterization.Polygon));
        vkCmdSetDepthBiasEnable(cmd, graphicsStateDescriptor.Rasterization.DepthBiasEnable);
        vkCmdSetRasterizerDiscardEnable(cmd, graphicsStateDescriptor.Rasterization.RasterizerDiscard);
        vkCmdSetLineWidth(cmd, graphicsStateDescriptor.Rasterization.LineWidth);

        // Depth / Stencil
        vkCmdSetDepthTestEnable(cmd, graphicsStateDescriptor.DepthStencil.DepthTestEnable);
        vkCmdSetDepthWriteEnable(cmd, graphicsStateDescriptor.DepthStencil.DepthWriteEnable);
        vkCmdSetStencilTestEnable(cmd, graphicsStateDescriptor.DepthStencil.StencilTestEnable);
        if (graphicsStateDescriptor.DepthStencil.DepthTestEnable) {
            vkCmdSetDepthCompareOp(cmd, ConvertCompareOpToVulkan(graphicsStateDescriptor.DepthStencil.DepthCompareOp));
        }
        if (graphicsStateDescriptor.DepthStencil.StencilTestEnable) {
            vkCmdSetStencilOp(cmd,
                              ConvertStencilFaceToVulkan(graphicsStateDescriptor.DepthStencil.StencilFaceMask),
                              ConvertStencilOpToVulkan(graphicsStateDescriptor.DepthStencil.StencilFailOp),
                              ConvertStencilOpToVulkan(graphicsStateDescriptor.DepthStencil.StencilPassOp),
                              ConvertStencilOpToVulkan(graphicsStateDescriptor.DepthStencil.StencilDepthFailOp),
                              ConvertCompareOpToVulkan(graphicsStateDescriptor.DepthStencil.StencilCompareOp));
            vkCmdSetStencilCompareMask(
                cmd,
                ConvertStencilFaceToVulkan(graphicsStateDescriptor.DepthStencil.StencilFaceMask),

                ConvertStencilBitToVulkan(graphicsStateDescriptor.DepthStencil.StencilWriteMask));

            vkCmdSetStencilReference(cmd,
                                     ConvertStencilFaceToVulkan(graphicsStateDescriptor.DepthStencil.StencilFaceMask),
                                     0);
            vkCmdSetStencilWriteMask(cmd,
                                     ConvertStencilFaceToVulkan(graphicsStateDescriptor.DepthStencil.StencilFaceMask),
                                     ConvertStencilBitToVulkan(graphicsStateDescriptor.DepthStencil.StencilWriteMask));
        }
        // Multisample
        const VkSampleCountFlagBits sampleCount =
            ConvertSampleCountToVulkan(graphicsStateDescriptor.Multisample.Samples);
        const VkSampleMask sampleMask = graphicsStateDescriptor.Multisample.SampleMask;
        vkCmdSetRasterizationSamplesEXT(cmd, sampleCount);
        vkCmdSetSampleMaskEXT(cmd, sampleCount, &sampleMask);
        vkCmdSetAlphaToCoverageEnableEXT(cmd, graphicsStateDescriptor.Multisample.AlphaToCoverageEnable);

        // Color Blend
        if (graphicsStateDescriptor.ColorBlendAttachmentCount > 0) {
            VkBool32 blendEnables[MaxBlendAttachments];
            VkColorComponentFlags colorWriteMasks[MaxBlendAttachments];
            VkColorBlendEquationEXT blendEquations[MaxBlendAttachments];
            for (uint32_t i = 0; i < graphicsStateDescriptor.ColorBlendAttachmentCount; ++i) {
                const auto& src = graphicsStateDescriptor.ColorBlend[i];
                blendEnables[i] = src.BlendEnable;
                colorWriteMasks[i] = ConvertColorComponentFlagsToVulkan(src.ColorWriteMask);
                blendEquations[i] = {
                    .srcColorBlendFactor = ConvertBlendFactorToVulkan(src.SrcColorFactor),
                    .dstColorBlendFactor = ConvertBlendFactorToVulkan(src.DstColorFactor),
                    .colorBlendOp = ConvertBlendOpToVulkan(src.ColorBlendOp),
                    .srcAlphaBlendFactor = ConvertBlendFactorToVulkan(src.SrcAlphaFactor),
                    .dstAlphaBlendFactor = ConvertBlendFactorToVulkan(src.DstAlphaFactor),
                    .alphaBlendOp = ConvertBlendOpToVulkan(src.AlphaBlendOp),
                };
            }
            vkCmdSetColorBlendEnableEXT(cmd, 0, graphicsStateDescriptor.ColorBlendAttachmentCount, blendEnables);
            vkCmdSetColorWriteMaskEXT(cmd, 0, graphicsStateDescriptor.ColorBlendAttachmentCount, colorWriteMasks);
            vkCmdSetColorBlendEquationEXT(cmd, 0, graphicsStateDescriptor.ColorBlendAttachmentCount, blendEquations);
        }

        // Vertex Input
        VkVertexInputBindingDescription2EXT bindings[MaxVertexBindings];
        for (uint32_t i = 0; i < graphicsStateDescriptor.VertexInput.BindingCount; ++i) {
            const auto& src = graphicsStateDescriptor.VertexInput.Bindings[i];
            bindings[i] = {
                .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
                .pNext = nullptr,
                .binding = src.Binding,
                .stride = src.Stride,
                .inputRate = ConvertVertexInputRateToVulkan(src.InputRate),
                .divisor = 1,
            };
        }
        VkVertexInputAttributeDescription2EXT attributes[MaxVertexAttributes];
        for (uint32_t i = 0; i < graphicsStateDescriptor.VertexInput.AttributeCount; ++i) {
            const auto& src = graphicsStateDescriptor.VertexInput.Attributes[i];
            attributes[i] = {
                .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
                .pNext = nullptr,
                .location = src.Location,
                .binding = src.Binding,
                .format = ConvertVertexFormatToVulkan(src.Format),
                .offset = src.Offset,
            };
        }
        vkCmdSetVertexInputEXT(cmd,
                               graphicsStateDescriptor.VertexInput.BindingCount,
                               bindings,
                               graphicsStateDescriptor.VertexInput.AttributeCount,
                               attributes);
    }

    // ============================================================
    // === Command Buffer Recording - Binding ===
    // ============================================================

    void RHIDeviceVulkan::BindShader(const RHIFrameContext& frameContext, const RHIShaderHandle& shaderHandle) {
        bindShaderInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()), shaderHandle);
    }

    void RHIDeviceVulkan::bindShaderInternal(VkCommandBuffer cmd, const RHIShaderHandle& shaderHandle) {
        if (const auto* shader = shaderResourcePool.Get(shaderHandle)) {
            shader->Bind(device, cmd);
        }
    }

    void RHIDeviceVulkan::BindBuffer(const RHIFrameContext& frameContext, const RHIBufferHandle& bufferHandle) {
        bindBufferInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()),
                           bufferHandle,
                           GetFrameNumberFromFrameContext(frameContext));
    }

    void
    RHIDeviceVulkan::bindBufferInternal(VkCommandBuffer cmd, const RHIBufferHandle& bufferHandle, uint32_t frameIndex) {
        const auto buffers = bufferResourcePool.Get(bufferHandle);
        if (!buffers) {
            spdlog::error("Failed to bind buffer. Buffer handle is invalid.");
            return;
        }

        const auto buffer = (*buffers)[frameIndex];

        if (has(buffer.Usage, BufferUsage::VertexBuffer)) {
            vkCmdBindVertexBuffers2(cmd, 0, 1, &buffer.Buffer, (VkDeviceSize[]) {0}, nullptr, nullptr);
            return;
        }

        if (has(buffer.Usage, BufferUsage::IndexBuffer)) {
            vkCmdBindIndexBuffer(cmd, buffer.Buffer, 0, VK_INDEX_TYPE_UINT32);
            return;
        }
        assert(false && "Buffer type not implemented.");
    }

    void RHIDeviceVulkan::SetPushConstants(const RHIFrameContext& frameContext,
                                           RHIPipelineLayoutHandle pipelineLayoutHandle,
                                           ShaderStageFlags stageFlags,
                                           uint32_t offset,
                                           uint32_t size,
                                           const void* data) {
        setPushConstantsInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()),
                                 pipelineLayoutHandle,
                                 stageFlags,
                                 offset,
                                 size,
                                 data);
    }

    void RHIDeviceVulkan::setPushConstantsInternal(VkCommandBuffer cmd,
                                                   RHIPipelineLayoutHandle pipelineLayoutHandle,
                                                   ShaderStageFlags stageFlags,
                                                   uint32_t offset,
                                                   uint32_t size,
                                                   const void* data) {
        const auto* layout = pipelineLayoutResourcePool.Get(pipelineLayoutHandle);
        if (!layout) {
            spdlog::error("SetPushConstants: invalid command buffer or pipeline layout handle");
            return;
        }
        vkCmdPushConstants(cmd, *layout, ConvertShaderStageFlagsToVulkan(stageFlags), offset, size, data);
    }

    void RHIDeviceVulkan::BindDescriptorSet(const RHIFrameContext& frameContext,
                                            RHIPipelineLayoutHandle pipelineLayoutHandle,
                                            uint32_t setIndex,
                                            RHIDescriptorSetHandle descriptorSetHandle) {
        bindDescriptorSetInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()),
                                  pipelineLayoutHandle,
                                  setIndex,
                                  descriptorSetHandle);
    }

    void RHIDeviceVulkan::bindDescriptorSetInternal(VkCommandBuffer cmd,
                                                    RHIPipelineLayoutHandle pipelineLayoutHandle,
                                                    uint32_t setIndex,
                                                    RHIDescriptorSetHandle descriptorSetHandle) {
        const auto* layout = pipelineLayoutResourcePool.Get(pipelineLayoutHandle);
        const auto* set = descriptorSetResourcePool.Get(descriptorSetHandle);
        if (!layout || !set) {
            spdlog::error("BindDescriptorSet: invalid handle(s)");
            return;
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *layout, setIndex, 1, set, 0, nullptr);
    }

    // ============================================================
    // === Command Buffer Recording - Draw ===
    // ============================================================

    void RHIDeviceVulkan::Draw(const RHIFrameContext& frameContext,
                               const uint32_t vertexCount,
                               const uint32_t instanceCount,
                               const uint32_t firstVertex,
                               const uint32_t firstInstance) {
        drawInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()),
                     vertexCount,
                     instanceCount,
                     firstVertex,
                     firstInstance);
    }

    void RHIDeviceVulkan::drawInternal(VkCommandBuffer cmd,
                                       uint32_t vertexCount,
                                       uint32_t instanceCount,
                                       uint32_t firstVertex,
                                       uint32_t firstInstance) {
        vkCmdDraw(cmd, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void RHIDeviceVulkan::DrawIndexed(const RHIFrameContext& frameContext,
                                      uint32_t indexCount,
                                      uint32_t instanceCount,
                                      uint32_t firstIndex,
                                      int32_t vertexOffset,
                                      uint32_t firstInstance) {
        drawIndexedInternal(*commandBufferResourcePool.Get(frameContext.GetCommandBuffer()),
                            indexCount,
                            instanceCount,
                            firstIndex,
                            vertexOffset,
                            firstInstance);
    }

    void RHIDeviceVulkan::drawIndexedInternal(VkCommandBuffer cmd,
                                              uint32_t indexCount,
                                              uint32_t instanceCount,
                                              uint32_t firstIndex,
                                              int32_t vertexOffset,
                                              uint32_t firstInstance) {
        vkCmdDrawIndexed(cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    // ============================================================
    // === Descriptor Sets ===
    // ============================================================

    RHIDescriptorSetHandle RHIDeviceVulkan::CreateDescriptorSet(RHIDescriptorSetLayoutHandle layoutHandle) {
        const auto* layout = descriptorSetLayoutResourcePool.Get(layoutHandle);
        if (!layout) {
            spdlog::error("CreateDescriptorSet: invalid descriptor set layout handle");
            return RHIDescriptorSetHandle::Null();
        }

        VkDescriptorSetAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = layout,
        };

        VkDescriptorSet set {VK_NULL_HANDLE};
        if (const auto result = vkAllocateDescriptorSets(device, &allocInfo, &set); result != VK_SUCCESS) {
            spdlog::error("Failed to allocate descriptor set. Error: {}", static_cast<int>(result));
            return RHIDescriptorSetHandle::Null();
        }

        return descriptorSetResourcePool.Allocate(std::move(set));
    }

    void RHIDeviceVulkan::UpdateDescriptorSet(RHIDescriptorSetHandle handle,
                                              std::span<const RHIDescriptorWrite> writes) {
        const auto* set = descriptorSetResourcePool.Get(handle);
        if (!set) {
            spdlog::error("UpdateDescriptorSet: invalid descriptor set handle");
            return;
        }

        std::vector<VkWriteDescriptorSet> vkWrites;
        vkWrites.reserve(writes.size());

        // Storage for descriptor infos (must outlive vkUpdateDescriptorSets)
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        bufferInfos.reserve(writes.size());
        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(writes.size());

        for (const auto& write : writes) {
            VkWriteDescriptorSet vkWrite {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = *set,
                .dstBinding = write.Binding,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = ConvertDescriptorTypeToVulkan(write.Type),
            };

            if (write.Type == DescriptorType::UniformBuffer || write.Type == DescriptorType::StorageBuffer) {
                const auto* buffers = bufferResourcePool.Get(write.Buffer.Buffer);
                if (!buffers) {
                    spdlog::error("UpdateDescriptorSet: invalid buffer handle at binding {}", write.Binding);
                    continue;
                }
                bufferInfos.push_back({
                    .buffer = (*buffers)[0].Buffer,
                    .offset = write.Buffer.Offset,
                    .range = write.Buffer.Range,
                });
                vkWrite.pBufferInfo = &bufferInfos.back();
            } else {
                const auto* texture = texturePool.Get(write.Image.Texture);
                if (!texture) {
                    spdlog::error("UpdateDescriptorSet: invalid texture handle at binding {}", write.Binding);
                    continue;
                }
                imageInfos.push_back({
                    .sampler = texture->Sampler,
                    .imageView = texture->ImageView,
                    .imageLayout = ConvertTextureLayoutToVulkan(TextureLayout::ShaderReadOnly),
                });

                vkWrite.pImageInfo = &imageInfos.back();
            }

            vkWrites.push_back(vkWrite);
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(vkWrites.size()), vkWrites.data(), 0, nullptr);
    }

    void RHIDeviceVulkan::FreeDescriptorSet(RHIDescriptorSetHandle handle) {
        perFrameDeletions[currentFrame].emplace_back([this, handle]() {
            descriptorSetResourcePool.Free(handle);
        });
    }

    // ============================================================
    // === Resource Creation ===
    // ============================================================

    RHITextureHandle RHIDeviceVulkan::CreateTexture(TextureDescriptor&& descriptor) {
        VkImageCreateInfo imageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = ConvertTextureFormatToVulkan(descriptor.Format),
            .extent =
                {
                    .width = descriptor.Width,
                    .height = descriptor.Height,
                    .depth = 1,
                },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = ConvertTextureUsageToVulkan(descriptor.Usage |
                                                 TextureUsage::TransferDst), // Ensure we can copy data to the texture
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VmaAllocationCreateInfo allocationCreateInfo {
            .flags = 0,
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };

        RHITextureVulkan texture {
            .Width = descriptor.Width,
            .Height = descriptor.Height,
        };
        if (auto result = vmaCreateImage(vmaAllocator,
                                         &imageCreateInfo,
                                         &allocationCreateInfo,
                                         &texture.Image,
                                         &texture.Allocation,
                                         &texture.AllocationInfo);
            result != VK_SUCCESS) {
            spdlog::error("Failed to create image for texture. Error: {}", static_cast<int>(result));
            return RHITextureHandle::Null();
        }

        // Texture is on device, let's make our sample and image view
        VkImageViewCreateInfo viewCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = texture.Image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ConvertTextureFormatToVulkan(descriptor.Format),
            .components =
                {
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                },
            .subresourceRange =
                {
                    .aspectMask = GetAspectMask(descriptor.Format),
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        if (const auto result = vkCreateImageView(device, &viewCreateInfo, nullptr, &texture.ImageView);
            result != VK_SUCCESS) {
            spdlog::error("Failed to create image view for texture. Error: {}", static_cast<int>(result));
            vmaDestroyImage(vmaAllocator, texture.Image, texture.Allocation);
            return RHITextureHandle::Null();
        }

        if (has(descriptor.Usage, TextureUsage::Sampled)) {
            VkSamplerCreateInfo samplerCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .magFilter = ConvertSamplerFilterToVulkan(descriptor.Sampler.MagFilter),
                .minFilter = ConvertSamplerFilterToVulkan(descriptor.Sampler.MinFilter),
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = ConvertSamplerAddressModeToVulkan(descriptor.Sampler.WrapU),
                .addressModeV = ConvertSamplerAddressModeToVulkan(descriptor.Sampler.WrapV),
                .addressModeW = ConvertSamplerAddressModeToVulkan(descriptor.Sampler.WrapW),
                .mipLodBias = 0.f,
                .anisotropyEnable = VK_TRUE,
                .maxAnisotropy = physicalDevices.SelectedDevice().Properties.properties.limits.maxSamplerAnisotropy,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.f,
                .maxLod = 0.f,
                .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                .unnormalizedCoordinates = VK_FALSE,
            };

            if (const auto result = vkCreateSampler(device, &samplerCreateInfo, nullptr, &texture.Sampler);
                result != VK_SUCCESS) {
                spdlog::error("Failed to create sampler for texture. Error: {}", static_cast<int>(result));
                vkDestroyImageView(device, texture.ImageView, nullptr);
                vmaDestroyImage(vmaAllocator, texture.Image, texture.Allocation);
                return RHITextureHandle::Null();
            }
        }

        const auto handle = texturePool.Allocate(std::move(texture));
        if (has(descriptor.Usage, TextureUsage::DepthAttachment)) {
            const auto immediateCmd = beginSingleTimeCommands();
            textureResourceBarrierInternal(immediateCmd,
                                           TextureBarrierDescriptor {
                                               .Texture = handle,
                                               .OldLayout = TextureLayout::Undefined,
                                               .NewLayout = TextureLayout::DepthStencilAttachment,
                                               .SrcStage = PipelineStage::None,
                                               .DstStage = PipelineStage::EarlyFragmentTests,
                                               .SrcAccess = Access::None,
                                               .DstAccess = Access::DepthStencilAttachmentWrite,
                                               .SubresourceRange =
                                                   {
                                                       .Aspect = TextureAspect::Stencil | TextureAspect::Depth,
                                                       .BaseMipLevel = 0,
                                                       .LevelCount = 1,
                                                       .BaseArrayLayer = 0,
                                                       .LayerCount = 1,
                                                   },
                                           });
            endSingleTimeCommands(immediateCmd);
        }
        return handle;
    }

    void RHIDeviceVulkan::UpdateTexture(const RHITextureHandle& handle, const void* data, size_t size) {
        // Create a staging buffer
        auto stagingBufferHandle =
            CreateBuffer({.Size = size, .Usage = BufferUsage::TransferSource, .Access = BufferMemoryAccess::CpuToGpu});

        // Map the staging buffer and copy the data
        const auto* stagingBuffer = bufferResourcePool.Get(stagingBufferHandle);
        if (!stagingBuffer) {
            spdlog::error("Failed to get staging buffer for texture update. Handle is invalid.");
            return;
        }
        void* mapped = (*stagingBuffer)[0].AllocationInfo.pMappedData;
        memcpy(mapped, data, size);
        // copy from staging buffer to texture using a command buffer and appropriate barriers
        auto immediateCmd = beginSingleTimeCommands();

        auto texture = texturePool.Get(handle);
        VkBufferImageCopy region {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = {0, 0, 0};
        region.imageExtent = {texture->Width, texture->Height, 1};
        textureResourceBarrierInternal(immediateCmd,
                                       TextureBarrierDescriptor {
                                           .Texture = handle,
                                           .OldLayout = TextureLayout::Undefined,
                                           .NewLayout = TextureLayout::TransferDst,
                                           .SrcStage = PipelineStage::None,
                                           .DstStage = PipelineStage::Transfer,
                                           .SrcAccess = Access::None,
                                           .DstAccess = Access::TransferWrite,
                                       });
        vkCmdCopyBufferToImage(immediateCmd,
                               (*stagingBuffer)[0].Buffer,
                               texturePool.Get(handle)->Image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &region);
        textureResourceBarrierInternal(immediateCmd,
                                       TextureBarrierDescriptor {
                                           .Texture = handle,
                                           .OldLayout = TextureLayout::TransferDst,
                                           .NewLayout = TextureLayout::ShaderReadOnly,
                                           .SrcStage = PipelineStage::Transfer,
                                           .DstStage = PipelineStage::AllGraphics,
                                           .SrcAccess = Access::TransferWrite,
                                           .DstAccess = Access::ShaderRead,
                                       });
        endSingleTimeCommands(immediateCmd);

        bufferResourcePool.Free(stagingBufferHandle);
    }

    void RHIDeviceVulkan::FreeTexture(RHITextureHandle handle) {
        perFrameDeletions[currentFrame].emplace_back([this, handle]() {
            texturePool.Free(handle);
        });
    }

    RHIShaderHandle RHIDeviceVulkan::CreateShader(ShaderFileParams&& shaderFiles) {
        std::ifstream vertexFile(shaderFiles.Vertex);
        std::ifstream fragmentFile(shaderFiles.Fragment);

        if (!vertexFile.is_open() || !fragmentFile.is_open()) {
            spdlog::error("Failed to open shader files");
            return RHIShaderHandle::Null();
        }

        std::string vertexSource((std::istreambuf_iterator<char>(vertexFile)), std::istreambuf_iterator<char>());
        std::string fragmentSource((std::istreambuf_iterator<char>(fragmentFile)), std::istreambuf_iterator<char>());
        std::string geometrySource;

        if (!shaderFiles.Geometry.empty()) {
            std::ifstream geometryFile(shaderFiles.Geometry);
            geometrySource =
                std::string((std::istreambuf_iterator<char>(geometryFile)), std::istreambuf_iterator<char>());
        }

        return CreateShader(ShaderSourceParams {
            .Vertex = vertexSource,
            .Geometry = geometrySource,
            .Fragment = fragmentSource,
        });
    }

    RHIShaderHandle RHIDeviceVulkan::CreateShader(ShaderSourceParams&& shaderSources) {
        RHIShaderVulkan shader {device, std::move(shaderSources)};
        if (!shader.IsCompiled()) {
            spdlog::error("Failed to compile shader. Aborting.");
            return RHIShaderHandle::Null();
        }

        // Build pipeline layout + descriptor set layouts from reflection
        auto layoutDesc = shader.GetPipelineLayoutDescriptor();
        auto [pipelineLayoutHandle, dsLayoutHandles] = CreatePipelineLayout(layoutDesc);
        if (!pipelineLayoutHandle.IsValid()) {
            return RHIShaderHandle::Null();
        }

        // Gather VkDescriptorSetLayout objects (ordered by set index = pool allocation order)
        std::vector<VkDescriptorSetLayout> vkDsLayouts;
        vkDsLayouts.reserve(dsLayoutHandles.size());
        for (const auto& handle : dsLayoutHandles) {
            vkDsLayouts.push_back(*descriptorSetLayoutResourcePool.Get(handle));
        }

        // Gather VkPushConstantRange objects
        std::vector<VkPushConstantRange> vkPushConstants;
        for (uint32_t i = 0; i < layoutDesc.PushConstantCount; i++) {
            vkPushConstants.push_back({
                .stageFlags = ConvertShaderStageFlagsToVulkan(layoutDesc.PushConstants[i].StageFlags),
                .offset = layoutDesc.PushConstants[i].Offset,
                .size = layoutDesc.PushConstants[i].Size,
            });
        }

        if (!shader.CreateVkShaders(device, vkDsLayouts, vkPushConstants)) {
            pipelineLayoutResourcePool.Free(pipelineLayoutHandle);
            for (const auto& handle : dsLayoutHandles) {
                descriptorSetLayoutResourcePool.Free(handle);
            }
            return RHIShaderHandle::Null();
        }

        shader.pipelineLayoutHandle = pipelineLayoutHandle;
        shader.descriptorSetLayoutHandles =
            std::vector<RHIDescriptorSetLayoutHandle>(dsLayoutHandles.begin(), dsLayoutHandles.end());

        return shaderResourcePool.Allocate(std::move(shader));
    }

    RHIPipelineLayoutDescriptor RHIDeviceVulkan::GetShaderPipelineLayout(const RHIShaderHandle& shaderHandle) {
        const auto* shader = shaderResourcePool.Get(shaderHandle);
        if (!shader) {
            spdlog::error("Failed to get shader pipeline layout. Shader handle is invalid.");
            return {};
        }
        return shader->GetPipelineLayoutDescriptor();
    }

    RHIPipelineLayoutHandle RHIDeviceVulkan::GetShaderPipelineLayoutHandle(const RHIShaderHandle& shaderHandle) {
        const auto* shader = shaderResourcePool.Get(shaderHandle);
        if (!shader) {
            spdlog::error("GetShaderPipelineLayoutHandle: invalid shader handle");
            return RHIPipelineLayoutHandle::Null();
        }
        return shader->pipelineLayoutHandle;
    }

    std::vector<RHIDescriptorSetLayoutHandle>
    RHIDeviceVulkan::GetShaderDescriptorSetLayoutHandles(const RHIShaderHandle& shaderHandle) {
        const auto* shader = shaderResourcePool.Get(shaderHandle);
        if (!shader) {
            spdlog::error("GetShaderDescriptorSetLayoutHandles: invalid shader handle");
            return {};
        }
        return shader->descriptorSetLayoutHandles;
    }

    std::pair<RHIPipelineLayoutHandle, std::set<RHIDescriptorSetLayoutHandle>>
    RHIDeviceVulkan::CreatePipelineLayout(const RHIPipelineLayoutDescriptor& pipelineLayoutDescriptor) {
        std::set<RHIDescriptorSetLayoutHandle> descriptorSetLayoutHandles;
        RHIPipelineLayoutHandle pipelineLayoutHandle {RHIPipelineLayoutHandle::Null()};
        auto cleanOnFailure = [&]() {
            // TODO: do this!
            for (const auto& handle : descriptorSetLayoutHandles) {
                descriptorSetLayoutResourcePool.Free(handle);
            }
        };

        for (auto i = 0U; i < pipelineLayoutDescriptor.SetCount; i++) {
            auto descriptorSetLayoutHandle = CreateDescriptorSetLayout(pipelineLayoutDescriptor.Sets[i]);
            if (descriptorSetLayoutHandle == RHIDescriptorSetLayoutHandle::Null()) {
                spdlog::error("Failed to create pipeline layout. Aborting process. See logs for details.");
                cleanOnFailure();
                return {RHIPipelineLayoutHandle::Null(), {}};
            }
            descriptorSetLayoutHandles.insert(descriptorSetLayoutHandle);
        }
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        descriptorSetLayouts.reserve(descriptorSetLayoutHandles.size());
        std::ranges::transform(descriptorSetLayoutHandles,
                               std::back_inserter(descriptorSetLayouts),
                               [this](const RHIDescriptorSetLayoutHandle& handle) {
                                   return *descriptorSetLayoutResourcePool.Get(handle);
                               });

        std::vector<VkPushConstantRange> pushConstantRanges {pipelineLayoutDescriptor.PushConstantCount};

        for (auto i = 0U; i < pipelineLayoutDescriptor.PushConstantCount; i++) {
            const auto& src = pipelineLayoutDescriptor.PushConstants[i];
            pushConstantRanges[i] = {
                .stageFlags = ConvertShaderStageFlagsToVulkan(src.StageFlags),
                .offset = src.Offset,
                .size = src.Size,
            };
        }

        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
            .pSetLayouts = descriptorSetLayouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
            .pPushConstantRanges = pushConstantRanges.data()};

        VkPipelineLayout pipelineLayout;
        if (const auto result = vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout);
            result != VK_SUCCESS) {
            spdlog::error("Failed to create pipeline layout. Error: {}", static_cast<int>(result));
            cleanOnFailure();
            return {RHIPipelineLayoutHandle::Null(), {}};
        };
        pipelineLayoutHandle = pipelineLayoutResourcePool.Allocate(std::move(pipelineLayout));
        return {pipelineLayoutHandle, descriptorSetLayoutHandles};
    }

    RHIDescriptorSetLayoutHandle
    RHIDeviceVulkan::CreateDescriptorSetLayout(const RHIDescriptorSetLayoutDescriptor& descriptorSetLayoutDescriptor) {
        RHIDescriptorSetLayoutHandle handle {RHIDescriptorSetLayoutHandle::Null()};
        // build up the bindings
        std::vector<VkDescriptorSetLayoutBinding> bindings {descriptorSetLayoutDescriptor.BindingCount};
        for (auto i = 0U; i < descriptorSetLayoutDescriptor.BindingCount; i++) {
            auto& srcBinding = descriptorSetLayoutDescriptor.Bindings[i];
            bindings[i] = {
                .binding = srcBinding.Binding,
                .descriptorType = ConvertDescriptorTypeToVulkan(srcBinding.Type),
                .descriptorCount = srcBinding.Count,
                .stageFlags = ConvertShaderStageFlagsToVulkan(srcBinding.StageFlags),
                .pImmutableSamplers = nullptr, // TODO: support immutable samplers when needed
            };
        }
        VkDescriptorSetLayoutCreateInfo layoutCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = descriptorSetLayoutDescriptor.BindingCount,
            .pBindings = bindings.data(),
        };

        VkDescriptorSetLayout layout;
        if (const auto result = vkCreateDescriptorSetLayout(device, &layoutCreateInfo, nullptr, &layout);
            result != VK_SUCCESS) {
            spdlog::error("Failed to create descriptor set layout. Error: {}", static_cast<int>(result));
            return handle;
        }

        return descriptorSetLayoutResourcePool.Allocate(std::move(layout));
    }

    void RHIDeviceVulkan::FreeShader(const RHIShaderHandle& shaderHandle) {
        perFrameDeletions[currentFrame].emplace_back([this, shaderHandle]() {
            shaderResourcePool.Free(shaderHandle);
        });
    }

    RHIBufferHandle RHIDeviceVulkan::CreateBuffer(BufferDescriptor&& bufferDescriptor) {
        std::array<RHIBufferVulkan, MaxFramesInFlight> buffers;
        size_t createdBuffers = 0;

        for (auto& buffer : buffers) {
            VkBufferCreateInfo bufferCreateInfo {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = bufferDescriptor.Size,
                .usage = ConvertBufferUsageToVulkan(bufferDescriptor.Usage),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };

            VmaAllocationCreateInfo allocationCreateInfo {
                .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = ConvertMemoryAccessToVulkan(bufferDescriptor.Access),
                .requiredFlags = 0,
                .preferredFlags = 0,
                .memoryTypeBits = 0,
                .pool = VK_NULL_HANDLE,
                .pUserData = nullptr,
            };

            if (const auto result = vmaCreateBuffer(vmaAllocator,
                                                    &bufferCreateInfo,
                                                    &allocationCreateInfo,
                                                    &buffer.Buffer,
                                                    &buffer.Allocation,
                                                    &buffer.AllocationInfo);
                result != VK_SUCCESS) {
                spdlog::error("Failed to create buffer. Error: {}", static_cast<int>(result));
                for (size_t i = 0; i < createdBuffers; ++i) {
                    vmaDestroyBuffer(vmaAllocator, buffers[i].Buffer, buffers[i].Allocation);
                }
                return RHIBufferHandle::Null();
            }

            buffer.Access = bufferDescriptor.Access;
            buffer.Usage = bufferDescriptor.Usage;
            createdBuffers++;
        }

        const auto handle = bufferResourcePool.Allocate(std::move(buffers));

        return handle;
    }

    void
    RHIDeviceVulkan::UpdateBuffer(const RHIBufferHandle& bufferHandle, const void* data, size_t size, size_t offset) {
        const auto buffers = bufferResourcePool.Get(bufferHandle);
        if (!buffers) {
            spdlog::error("Failed to update buffer. Buffer handle is invalid.");
            return;
        }

        for (const auto& buffer : *buffers) {
            // ensure usage is CPU accessible
            if (buffer.Access == BufferMemoryAccess::GpuOnly) {
                spdlog::error("Failed to update buffer. Buffer is not CPU accessible.");
                return;
            }

            // make sure offset + size does not exceed buffer size
            if (offset + size > buffer.AllocationInfo.size) {
                spdlog::error("Failed to update buffer. Data size exceeds buffer size.");
                return;
            }

            void* mappedData = buffer.AllocationInfo.pMappedData;
            if (!mappedData) {
                spdlog::error("Failed to update buffer. Buffer memory is not mapped.");
                return;
            }
            std::memcpy(static_cast<uint8_t*>(mappedData) + offset, data, size);
            vmaFlushAllocation(vmaAllocator, buffer.Allocation, offset, size);
        }
    }

    void RHIDeviceVulkan::FreeBuffer(const RHIBufferHandle& bufferHandle) {

        perFrameDeletions[currentFrame].emplace_back([this, bufferHandle]() {
            bufferResourcePool.Free(bufferHandle);
        });
    }

} // namespace OZZ::rendering::vk
