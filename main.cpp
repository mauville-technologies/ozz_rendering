//
// Created by paulm on 2026-02-21.
//
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#include <volk.h>

#include "GLFW/glfw3.h"
#include "glm/vec4.hpp"
#include "spdlog/spdlog.h"

#include "ozz_rendering/rhi_device.h"

#include <cstdlib>
#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 Position {0.f, 0.f, 1.f};
    glm::vec4 Color {1.f, 0.f, 0.f, 1.f};

    static OZZ::rendering::VertexInputBindingDescriptor GetBindingDescription() {
        return {
            .Binding = 0,
            .Stride = sizeof(Vertex),
            .InputRate = OZZ::rendering::VertexInputRate::Vertex,
        };
    }

    static std::array<OZZ::rendering::VertexInputAttributeDescriptor, 2> GetAttributeDescriptions() {
        return {
            OZZ::rendering::VertexInputAttributeDescriptor {
                .Location = 0,
                .Binding = 0,
                .Format = OZZ::rendering::VertexFormat::Float3,
                .Offset = offsetof(Vertex, Position),
            },
            OZZ::rendering::VertexInputAttributeDescriptor {
                .Location = 1,
                .Binding = 0,
                .Format = OZZ::rendering::VertexFormat::Float4,
                .Offset = offsetof(Vertex, Color),
            },
        };
    }
};

OZZ::rendering::RHIShaderHandle shader {};

OZZ::rendering::RenderPassDescriptor renderPassDescriptor {
    .ColorAttachments =
        {
            OZZ::rendering::AttachmentDescriptor {
                .Load = OZZ::rendering::LoadOp::Clear,
                .Store = OZZ::rendering::StoreOp::Store,
                .Clear =
                    {
                        .R = 0.3f,
                        .G = 0.1f,
                        .B = 0.1f,
                        .A = 1.f,
                    },
                .Layout = OZZ::rendering::TextureLayout::ColorAttachment,
            },
        },
    .ColorAttachmentCount = 1,
    .DepthAttachment = {},
    .StencilAttachment = {},
    .RenderArea =
        {
            .X = 0,
            .Y = 0,
            .Width = WINDOW_WIDTH,
            .Height = WINDOW_HEIGHT,
        },
    .LayerCount = 1,
};

void GLFW_KeyCallback2(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

int main() {
    spdlog::set_level(spdlog::level::trace);

    GLFWwindow* window;
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

    glfwSetKeyCallback(window, GLFW_KeyCallback2);

    // get native window handle

    // Initialize RHI device
    // get instance extensions
    std::vector<std::string> requiredExtensions {};
    uint32_t numExtensions {0};
    const char** extensions = glfwGetRequiredInstanceExtensions(&numExtensions);

    if (extensions && numExtensions > 0) {
        requiredExtensions.reserve(numExtensions);
        for (uint32_t i = 0; i < numExtensions; i++) {
            requiredExtensions.emplace_back(extensions[i]);
        }
    }

    auto rhiDevice = OZZ::rendering::CreateRHIDevice({
        .Backend = OZZ::rendering::RHIBackend::Auto,
        .Context = {
            .AppName = "RHI Playground",
            .AppVersion = {0, 1, 0, 0},
            .EngineName = "RHI Playground Engine",
            .EngineVersion = {0, 1, 0, 0},
            .WindowHandle = window,
            .RequiredInstanceExtensions = requiredExtensions,
            .GetWindowFramebufferSizeFunction =
                [window]() {
                    int width, height;
                    glfwGetFramebufferSize(window, &width, &height);
                    return std::make_pair(width, height);
                },
            .CreateSurfaceFunction =
                [window](void* instance, void* surface) {
                    return glfwCreateWindowSurface(static_cast<VkInstance>(instance),
                                                   window,
                                                   nullptr,
                                                   static_cast<VkSurfaceKHR*>(surface)) == VK_SUCCESS;
                },
        },
    });

    std::filesystem::path base = std::filesystem::current_path() / "assets" / "shaders" / "basic";
    shader = rhiDevice->CreateShader(OZZ::rendering::ShaderFileParams {
        .Vertex = base / "basic.vert",
        .Fragment = base / "basic.frag",
    });

    // Let's create my vertex buffer now
    auto vertexBuffer = rhiDevice->CreateBuffer(OZZ::rendering::BufferDescriptor {
        .Size = 3 * sizeof(Vertex),
        .Usage = OZZ::rendering::BufferUsage::VertexBuffer,
        .Access = OZZ::rendering::BufferMemoryAccess::CpuToGpu,
    });

    if (!vertexBuffer.IsValid()) {
        spdlog::error("Failed to create vertex buffer");
        return 1;
    }

    rhiDevice->UpdateBuffer(vertexBuffer,
                            std::array {
                                Vertex {{0.f, -0.5f, 0.5f}, {1.f, 1.f, 0.f, 1.f}},
                                Vertex {{0.5f, 0.5f, 0.5f}, {0.f, 1.f, 1.f, 1.f}},
                                Vertex {{-0.5f, 0.5f, 0.5f}, {0.f, 0.f, 1.f, 1.f}},
                            }
                                .data(),
                            sizeof(Vertex) * 3,
                            0);
    const auto attributeDescriptions = Vertex::GetAttributeDescriptions();
    const auto bindingDescriptions = Vertex::GetBindingDescription();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        auto context = rhiDevice->BeginFrame();
        renderPassDescriptor.ColorAttachments[0].Texture = context.GetBackbuffer();
        rhiDevice->BeginRenderPass(context.GetCommandBuffer(), renderPassDescriptor);
        rhiDevice->SetGraphicsState(context.GetCommandBuffer(),
                                    {
                                        .ColorBlend = {{
                                            .BlendEnable = false,
                                        }},
                                        .ColorBlendAttachmentCount = 1,
                                        .VertexInput =
                                            {
                                                .Bindings = {bindingDescriptions},
                                                .BindingCount = 1,
                                                .Attributes = {attributeDescriptions[0], attributeDescriptions[1]},
                                                .AttributeCount = attributeDescriptions.size(),
                                            },
                                    });
        rhiDevice->SetViewport(context.GetCommandBuffer(),
                               {
                                   .X = 0,
                                   .Y = 0,
                                   .Width = WINDOW_WIDTH,
                                   .Height = WINDOW_HEIGHT,
                                   .MinDepth = 0.f,
                                   .MaxDepth = 1.f,
                               });
        rhiDevice->SetScissor(context.GetCommandBuffer(),
                              {
                                  .X = 0,
                                  .Y = 0,
                                  .Width = WINDOW_WIDTH,
                                  .Height = WINDOW_HEIGHT,
                              });
        rhiDevice->BindShader(context.GetCommandBuffer(), shader);

        rhiDevice->BindBuffer(context.GetCommandBuffer(), vertexBuffer);
        // Bind material
        rhiDevice->Draw(context.GetCommandBuffer(), 3, 1, 0, 0);

        rhiDevice->EndRenderPass(context.GetCommandBuffer());
        rhiDevice->SubmitAndPresentFrame(std::move(context));
    }

    rhiDevice.reset();
    glfwTerminate();
    return 0;
}