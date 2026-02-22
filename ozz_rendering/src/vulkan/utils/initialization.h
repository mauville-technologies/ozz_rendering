//
// Created by paulm on 2026-02-22.
//

#pragma once

#include <cstdint>
#include <optional>
#include <spdlog/spdlog.h>
#include <volk.h>

namespace OZZ::rendering::vk {
    inline uint32_t ChooseNumberOfSwapchainImages(const VkSurfaceCapabilitiesKHR& capabilities) {
        const uint32_t requestedNumImages = capabilities.minImageCount + 1;

        uint32_t finalNumberOfImages = 0;
        if (capabilities.maxImageCount > 0 && requestedNumImages > capabilities.maxImageCount) {
            finalNumberOfImages = capabilities.maxImageCount;
        } else {
            finalNumberOfImages = requestedNumImages;
        }

        return finalNumberOfImages;
    }

    inline VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& presentModes,
                                              const VkPresentModeKHR preferredMode = VK_PRESENT_MODE_IMMEDIATE_KHR) {
        VkPresentModeKHR fallbackMode = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto presentMode : presentModes) {
            if (presentMode == preferredMode) {
                return presentMode;
            }
            if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                fallbackMode = presentMode;
            }
        }

        return fallbackMode;
    }

    inline VkSurfaceFormatKHR ChooseSurfaceFormatAndColorSpace(const std::vector<VkSurfaceFormatKHR>& surfaceFormats) {
        for (const auto format : surfaceFormats) {
            // TODO: @paulm -- make the preferred format and color space configurable, and don't default to
            // B8G8R8A8_SRGB and SRGB_NONLINEAR... why was this set to preferred in the first place
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }

        return surfaceFormats[0];
    }

    inline std::optional<VkImageView> CreateImageView(VkDevice device,
                                                      VkImage image,
                                                      const VkFormat format,
                                                      const VkImageAspectFlags imageAspectFlags,
                                                      const VkImageViewType imageViewType,
                                                      const uint32_t layerCount,
                                                      const uint32_t mipLevels) {
        VkImageViewCreateInfo imageViewCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image,
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
        if (const auto result = vkCreateImageView(device, &imageViewCreateInfo, nullptr, &imageView);
            result != VK_SUCCESS) {
            spdlog::error("Failed to create image view, error code: {}", static_cast<int>(result));
            return std::nullopt;
        }

        return imageView;
    }

} // namespace OZZ::rendering::vk
