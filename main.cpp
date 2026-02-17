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

    ~App() {
        vkCore.FreeCommandBuffers(commandBuffers.size(), commandBuffers.data());
        vkDestroyRenderPass(vkCore.GetDevice(), renderPass, nullptr);
    }

    void Init(const std::string& appName, GLFWwindow* window) {
        vkCore.Init({
            .AppName = "Test Vulkan",
            .SurfaceCreationFunction =
                [window](VkInstance instance, VkSurfaceKHR* surface) {
                    return glfwCreateWindowSurface(instance, window, nullptr, surface);
                },
        });
        queue = vkCore.GetQueue();
        renderPass = vkCore.CreateSimpleRenderPass();
        framebuffers = vkCore.CreateFramebuffers(renderPass, [window]() {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            return std::pair<int, int>(width, height);
        });

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
            0.f,
        };

        VkClearValue clearValue;
        clearValue.color = clearColor;

        VkRenderPassBeginInfo renderPassBeginInfo {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = renderPass,
            .framebuffer = VK_NULL_HANDLE,
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
            .clearValueCount = 1,
            .pClearValues = &clearValue,
        };

        int i = 0;
        for (const auto commandBuffer : commandBuffers) {
            OZZ::vk::BeginCommandBuffer(commandBuffer, 0);
            renderPassBeginInfo.framebuffer = framebuffers[i];
            vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdEndRenderPass(commandBuffer);
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

    VkRenderPass renderPass;
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