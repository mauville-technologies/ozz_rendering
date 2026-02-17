#include <iostream>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "ozz_vk/core.h"
#include "ozz_vk/util.h"
#include "ozz_vk/vulkan_queue.h"
#include "spdlog/spdlog.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

GLFWwindow* window {nullptr};

class App {
public:
    App() {};

    ~App() { vkCore.FreeCommandBuffers(commandBuffers.size(), commandBuffers.data()); }

    void Init(const std::string& appName, GLFWwindow* window) {
        vkCore.Init({
            .AppName = "Test Vulkan",
            .SurfaceCreationFunction =
                [window](VkInstance instance, VkSurfaceKHR* surface) {
                    return glfwCreateWindowSurface(instance, window, nullptr, surface);
                },
        });
        queue = vkCore.GetQueue();

        numSwapchainImages = vkCore.GetSwapchainImageCount();
        createCommandBuffers();
        recordCommandBuffers();
    }

    void Render() {
        uint32_t imageIndex = queue->AcquireNextImage();
        queue->SubmitAsync(commandBuffers[imageIndex]);
        queue->Present(imageIndex);
        queue->WaitIdle();
    }

private:
    void createCommandBuffers() {
        commandBuffers.resize(numSwapchainImages);
        vkCore.CreateCommandBuffers(numSwapchainImages, commandBuffers.data());
    }

    void recordCommandBuffers() {
        VkClearColorValue clearColor {
            1.f,
            0.f,
            0.f,
            1.f,
        };

        VkClearValue clearValue;
        clearValue.color = clearColor;

        VkImageMemoryBarrier presentToClearBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = VK_NULL_HANDLE,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        VkImageMemoryBarrier clearToPresentImageBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = VK_NULL_HANDLE,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        int i = 0;
        for (const auto commandBuffer : commandBuffers) {
            OZZ::vk::BeginCommandBuffer(commandBuffer, 0);

            presentToClearBarrier.image = vkCore.GetSwapchainImage(i);

            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &presentToClearBarrier);

            // transition to color attachment optimal
            VkRenderingAttachmentInfo colorAttachment {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .pNext = nullptr,
                .imageView = vkCore.GetSwapchainImageView(i),
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = clearValue,
            };
            VkRenderingInfo renderingInfo {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .pNext = nullptr,
                .flags = 0,
                .renderArea =
                    {
                        .offset =
                            {
                                .x = 0,
                                .y = 0,
                            },
                        .extent =
                            {
                                .width = WINDOW_WIDTH,
                                .height = WINDOW_HEIGHT,
                            },
                    },
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachment,
                .pDepthAttachment = nullptr,
                .pStencilAttachment = nullptr,
            };
            vkCmdBeginRendering(commandBuffer, &renderingInfo);

            vkCmdEndRendering(commandBuffer);

            clearToPresentImageBarrier.image = vkCore.GetSwapchainImage(i);

            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &clearToPresentImageBarrier);
            const auto result = vkEndCommandBuffer(commandBuffer);
            CHECK_VK_RESULT(result, "End command buffer");
            i++;
        }

        spdlog::info("Command buffers recorded");
    }

private:
    OZZ::vk::VulkanCore vkCore;
    OZZ::vk::VulkanQueue* queue {nullptr};
    uint32_t numSwapchainImages = 0;
    std::vector<VkCommandBuffer> commandBuffers {};

    std::vector<VkFramebuffer> framebuffers;
};

void GLFW_KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

int main() {
    if (!glfwInit()) {
        return 1;
    }

    if (!glfwVulkanSupported()) {
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Tutorial 1", nullptr, nullptr);

    if (!window) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwSetKeyCallback(window, GLFW_KeyCallback);

    App app;
    app.Init("lessons", window);
    while (!glfwWindowShouldClose(window)) {
        app.Render();
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}