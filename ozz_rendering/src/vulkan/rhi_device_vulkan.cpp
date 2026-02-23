//
// Created by paulm on 2026-02-21.
//

#include "rhi_device_vulkan.h"

#include "utils/initialization.h"
#include "utils/rhi_vulkan_types.h"

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
        if (graphicsQueue != VK_NULL_HANDLE) {
            vkQueueWaitIdle(graphicsQueue);
            graphicsQueue = VK_NULL_HANDLE;
        }

        for (auto& context : submissionContexts) {
            if (context.InFlightFence != VK_NULL_HANDLE) {
                vkDestroyFence(device, context.InFlightFence, nullptr);
                context.InFlightFence = VK_NULL_HANDLE;
            }
            if (context.AcquireImageSemaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, context.AcquireImageSemaphore, nullptr);
                context.AcquireImageSemaphore = VK_NULL_HANDLE;
            }

            if (context.CommandBuffer.IsValid()) {
                commandBufferResourcePool.Free(context.CommandBuffer);
                context.CommandBuffer = RHICommandBufferHandle::Null();
            }
        }
        submissionContexts.clear();
        spdlog::trace("cleared submission contexts");

        if (commandBufferPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandBufferPool, nullptr);
            spdlog::trace("Destroyed command buffer pool");
            commandBufferPool = VK_NULL_HANDLE;
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

    FrameContext RHIDeviceVulkan::BeginFrame() {
        const auto& submissionContext = submissionContexts[currentFrame];
        if (const auto fenceResult = vkWaitForFences(device, 1, &submissionContext.InFlightFence, VK_TRUE, UINT64_MAX);
            fenceResult != VK_SUCCESS) {
            spdlog::error("Failed to wait for fence in BeginFrame. Error: {}", static_cast<int>(fenceResult));
            return FrameContext::Null();
        }

        vkResetFences(device, 1, &submissionContext.InFlightFence);

        uint32_t imageIndex;

        if (const auto result = vkAcquireNextImageKHR(device,
                                                      swapchain,
                                                      UINT64_MAX,
                                                      submissionContext.AcquireImageSemaphore,
                                                      VK_NULL_HANDLE,
                                                      &imageIndex);
            result != VK_SUCCESS) {
            spdlog::error("Failed to acquire next image in BeginFrame. Error: {}", static_cast<int>(result));
            return FrameContext::Null();
        }

        const auto commandBuffer = commandBufferResourcePool.Get(submissionContext.CommandBuffer);

        VkCommandBufferBeginInfo VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };

        if (const auto result = vkBeginCommandBuffer(*commandBuffer, &VkCommandBufferBeginInfo); result != VK_SUCCESS) {
            spdlog::error("Failed to begin command buffer in BeginFrame. Error: {}", static_cast<int>(result));
            return FrameContext::Null();
        }

        TextureResourceBarrier(submissionContext.CommandBuffer,
                               TextureBarrierDescriptor {
                                   .Texture = swapchainTextureHandles[imageIndex],
                                   .OldLayout = TextureLayout::Undefined,
                                   .NewLayout = TextureLayout::ColorAttachment,
                                   .SrcStage = PipelineStage::ColorAttachmentOutput,
                                   .DstStage = PipelineStage::ColorAttachmentOutput,
                                   .SrcAccess = Access::None,
                                   .DstAccess = Access::ColorAttachmentWrite,
                               });

        return BuildFrameContext(submissionContext.CommandBuffer,
                                 swapchainTextureHandles[imageIndex],
                                 imageIndex,
                                 currentFrame);
    }

    void RHIDeviceVulkan::SubmitAndPresentFrame(FrameContext context) {
        // Prepare swapchain image for presentation
        const auto imageIndex = GetImageIndexFromFrameContext(context);

        TextureResourceBarrier(context.GetCommandBuffer(),
                               TextureBarrierDescriptor {
                                   .Texture = swapchainTextureHandles[imageIndex],
                                   .OldLayout = TextureLayout::ColorAttachment,
                                   .NewLayout = TextureLayout::Present,
                                   .SrcStage = PipelineStage::ColorAttachmentOutput,
                                   .DstStage = PipelineStage::None,
                                   .SrcAccess = Access::ColorAttachmentWrite,
                                   .DstAccess = Access::None,
                               });

        auto commandBuffer = commandBufferResourcePool.Get(context.GetCommandBuffer());
        if (const auto result = vkEndCommandBuffer(*commandBuffer); result != VK_SUCCESS) {
            spdlog::error("Failed to end command buffer in SubmitFrame. Error: {}", static_cast<int>(result));
            return;
        }

        auto& submissionContext = submissionContexts[GetFrameNumberFromFrameContext(context)];
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
                          currentFrame,
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

        if (const auto result = vkQueuePresentKHR(graphicsQueue, &presentInfo); result != VK_SUCCESS) {
            spdlog::error("Failed to present frame in SubmitFrame. Error: {}", static_cast<int>(result));
        }

        currentFrame = (currentFrame + 1) % framesInFlight;
    }

    void RHIDeviceVulkan::BeginRenderPass(const RHICommandBufferHandle& commandBufferHandle,
                                          const RenderPassDescriptor& renderPassDescriptor) {
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        bool bHasDepthStencilAttachment = false;
        VkRenderingAttachmentInfo depthStencilAttachment;

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
            if (attachment.Layout == TextureLayout::DepthStencilAttachment) {
                clearValue = VkClearValue {
                    .depthStencil =
                        {
                            .depth = attachment.Clear.Depth,
                            .stencil = attachment.Clear.Stencil,
                        },
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

            if (attachment.Layout == TextureLayout::DepthStencilAttachment) {
                // TODO: implement this when needed
                // bHasDepthStencilAttachment = true;
                // depthStencilAttachment = attachmentInfo;
                // depthStencilAttachment.imageLayout =
                //     ConvertTextureLayoutToVulkan(TextureLayout::DepthStencilAttachment);
                // depthStencilAttachment.loadOp = ConvertLoadOpToVulkan(attachment.Load);
                // depthStencilAttachment.storeOp = ConvertStoreOpToVulkan(attachment.Store);
                assert(false && "Depth stencil attachments not implemented!!");
            } else {
                colorAttachments.emplace_back(attachmentInfo);
            }
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
            .pDepthAttachment = bHasDepthStencilAttachment ? &depthStencilAttachment : nullptr,
            .pStencilAttachment = bHasDepthStencilAttachment ? &depthStencilAttachment : nullptr,
        };

        const auto commandBuffer = *commandBufferResourcePool.Get(commandBufferHandle);
        vkCmdBeginRendering(commandBuffer, &renderingInfo);
    }

    void RHIDeviceVulkan::EndRenderPass(const RHICommandBufferHandle& commandBufferHandle) {
        const auto commandBuffer = *commandBufferResourcePool.Get(commandBufferHandle);
        vkCmdEndRendering(commandBuffer);
    }

    void RHIDeviceVulkan::TextureResourceBarrier(const RHICommandBufferHandle& cbHandle,
                                                 const TextureBarrierDescriptor& barrierDescriptor) {
        const auto commandBuffer = *commandBufferResourcePool.Get(cbHandle);

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

        vkCmdPipelineBarrier2(commandBuffer, &barrierDependency);
    }

    void RHIDeviceVulkan::SetViewport(const RHICommandBufferHandle&, const Viewport&) {}

    void RHIDeviceVulkan::SetScissor(const RHICommandBufferHandle&, const Scissor&) {}

    void RHIDeviceVulkan::SetGraphicsState(const RHICommandBufferHandle&, const GraphicsStateDescriptor&) {}

    void RHIDeviceVulkan::Draw(const RHICommandBufferHandle&,
                               uint32_t vertexCount,
                               uint32_t instanceCount,
                               uint32_t firstVertex,
                               uint32_t firstInstance) {}

    void RHIDeviceVulkan::DrawIndexed(const RHICommandBufferHandle&,
                                      uint32_t indexCount,
                                      uint32_t instanceCount,
                                      uint32_t firstIndex,
                                      int32_t vertexOffset,
                                      uint32_t firstInstance) {}

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

        if (!createSubmissionContexts()) {
            failureMessage();
            return false;
        }

        if (!initializeQueue()) {
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

        spdlog::trace("created command buffer pool");
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
} // namespace OZZ::rendering::vk
