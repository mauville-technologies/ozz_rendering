//
// Created by paulm on 2026-02-21.
//
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#include <volk.h>

#include "GLFW/glfw3.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/vec4.hpp"
#include "spdlog/spdlog.h"

#include "ozz_rendering/rhi_device.h"

#include <cstdlib>
#include <glm/glm.hpp>

struct UBOObject {
    glm::mat4 View;
    glm::mat4 Projection;
};

struct Vertex {
    glm::vec3 Position {0.f, 0.f, 1.f};
    glm::vec4 Color {1.f, 0.f, 0.f, 1.f};
    glm::vec2 TexCoord {0.f, 0.f};

    static OZZ::rendering::VertexInputBindingDescriptor GetBindingDescription() {
        return {
            .Binding = 0,
            .Stride = sizeof(Vertex),
            .InputRate = OZZ::rendering::VertexInputRate::Vertex,
        };
    }

    static std::array<OZZ::rendering::VertexInputAttributeDescriptor, 3> GetAttributeDescriptions() {
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
            OZZ::rendering::VertexInputAttributeDescriptor {
                .Location = 2,
                .Binding = 0,
                .Format = OZZ::rendering::VertexFormat::Float2,
                .Offset = offsetof(Vertex, TexCoord),
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
    .DepthAttachment =
        {
            .Load = OZZ::rendering::LoadOp::Clear,
            .Store = OZZ::rendering::StoreOp::DontCare,
            .Clear =
                {
                    .Depth = 1.f,
                    .Stencil = 0,
                },
            .Layout = OZZ::rendering::TextureLayout::DepthStencilAttachment,
        },
    .StencilAttachment =
        {
            .Load = OZZ::rendering::LoadOp::Clear,
            .Store = OZZ::rendering::StoreOp::DontCare,
            .Clear =
                {
                    .Depth = 1.f,
                    .Stencil = 0,
                },
            .Layout = OZZ::rendering::TextureLayout::DepthStencilAttachment,
        },
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

    std::filesystem::path imagePath = std::filesystem::current_path() / "assets" / "images" / "texture.jpg";
    int texWidth, texHeight, texChannels;
    stbi_set_flip_vertically_on_load(true);
    stbi_uc* pixels = stbi_load(imagePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) {
        throw std::runtime_error("failed to load texture image!");
    }

    auto texture = rhiDevice->CreateTexture(OZZ::rendering::TextureDescriptor {
        .Width = static_cast<uint32_t>(texWidth),
        .Height = static_cast<uint32_t>(texHeight),
        .Format = OZZ::rendering::TextureFormat::RGBA8,
        .Usage = OZZ::rendering::TextureUsage::Sampled | OZZ::rendering::TextureUsage::TransferDst,
    });

    rhiDevice->UpdateTexture(texture, pixels, imageSize);
    std::filesystem::path base = std::filesystem::current_path() / "assets" / "shaders" / "basic";
    shader = rhiDevice->CreateShader(OZZ::rendering::ShaderFileParams {
        .Vertex = base / "basic.vert",
        .Fragment = base / "basic.frag",
    });

    // Pipeline layout and descriptor set layouts are created automatically with the shader
    auto pipelineLayoutHandle = rhiDevice->GetShaderPipelineLayoutHandle(shader);
    auto descriptorSetLayoutHandles = rhiDevice->GetShaderDescriptorSetLayoutHandles(shader);

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

    auto indexBuffer = rhiDevice->CreateBuffer(OZZ::rendering::BufferDescriptor {
        .Size = 3 * OZZ::rendering::IndexBufferElementSize,
        .Usage = OZZ::rendering::BufferUsage::IndexBuffer,
        .Access = OZZ::rendering::BufferMemoryAccess::CpuToGpu,
    });

    if (!indexBuffer.IsValid()) {
        spdlog::error("Failed to create index buffer");
        return 1;
    }

    rhiDevice->UpdateBuffer(vertexBuffer,
                            std::array {
                                Vertex {
                                    {0.f, -0.5f, 0.0f},
                                    {1.f, 1.f, 0.f, 1.f},
                                    {0.5f, 1.f},
                                },
                                Vertex {
                                    {0.5f, 0.5f, 0.0f},
                                    {0.f, 1.f, 1.f, 1.f},
                                    {1.f, 0.f},
                                },
                                Vertex {
                                    {-0.5f, 0.5f, 0.0f},
                                    {0.f, 0.f, 1.f, 1.f},
                                    {0.f, 0.f},
                                },
                            }
                                .data(),
                            sizeof(Vertex) * 3,
                            0);
    spdlog::info("Updated vertex buffer with triangle vertices");
    rhiDevice->UpdateBuffer(indexBuffer,
                            std::array {
                                OZZ::rendering::IndexBufferElementType {0},
                                OZZ::rendering::IndexBufferElementType {1},
                                OZZ::rendering::IndexBufferElementType {2},
                            }
                                .data(),
                            OZZ::rendering::IndexBufferElementSize * 3,
                            0);
    spdlog::info("Updated index buffer with triangle indices");
    const auto attributeDescriptions = Vertex::GetAttributeDescriptions();
    const auto bindingDescriptions = Vertex::GetBindingDescription();

    auto projection =
        glm::ortho(-WINDOW_WIDTH / 2.f, WINDOW_WIDTH / 2.f, -WINDOW_HEIGHT / 2.f, WINDOW_HEIGHT / 2.f, 0.1f, 10.f);
    // projection[1][1] *= -1; // flip y axis to match vulkan's coordinate system
    const auto view = glm::lookAt(glm::vec3(0.f, 0.f, 2.f), glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f));
    auto model = glm::scale(glm::mat4(1.f), glm::vec3 {200.f, 200.f, 1.f});

    auto uboBuffer = rhiDevice->CreateBuffer(OZZ::rendering::BufferDescriptor {
        .Size = sizeof(UBOObject),
        .Usage = OZZ::rendering::BufferUsage::UniformBuffer,
        .Access = OZZ::rendering::BufferMemoryAccess::CpuToGpu,
    });

    UBOObject uboObject {
        .View = view,
        .Projection = projection,
    };
    rhiDevice->UpdateBuffer(uboBuffer, &uboObject, sizeof(UBOObject), 0);

    if (!uboBuffer.IsValid()) {
        spdlog::error("Failed to create uniform buffer");
        return 1;
    }

    // Create and populate descriptor set for the UBO (set=0, binding=0)
    auto uboDescriptorSet = rhiDevice->CreateDescriptorSet(*descriptorSetLayoutHandles.begin());
    if (!uboDescriptorSet.IsValid()) {
        spdlog::error("Failed to create descriptor set");
        return 1;
    }
    std::array uboWrites = {
        OZZ::rendering::RHIDescriptorWrite {
            .Binding = 0,
            .Type = OZZ::rendering::DescriptorType::UniformBuffer,
            .Buffer = {.Buffer = uboBuffer, .Offset = 0, .Range = sizeof(UBOObject)},
        },
        OZZ::rendering::RHIDescriptorWrite {
            .Binding = 1,
            .Type = OZZ::rendering::DescriptorType::CombinedImageSampler,
            .Image = {texture},
        },
    };
    rhiDevice->UpdateDescriptorSet(uboDescriptorSet, uboWrites);

    while (!glfwWindowShouldClose(window)) {

        static auto count = 0U;
        auto oscillation = std::sin(count * 0.001f);
        const auto currentModel = glm::scale(model, glm::vec3(1.f + 0.5f * oscillation, 1.f + 0.5f * oscillation, 1.f));
        count++;
        glfwPollEvents();
        auto context = rhiDevice->BeginFrame();
        renderPassDescriptor.ColorAttachments[0].Texture = context.GetBackbufferImage();
        renderPassDescriptor.DepthAttachment.Texture = context.GetBackbufferDepthImage();
        // renderPassDescriptor.StencilAttachment.Texture = context.GetBackbufferDepthImage();
        rhiDevice->BeginRenderPass(context, renderPassDescriptor);
        rhiDevice->SetGraphicsState(
            context,
            {
                .Rasterization =
                    {
                        .Cull = OZZ::rendering::CullMode::None,
                        .Front = OZZ::rendering::FrontFace::CounterClockwise,
                    },
                .ColorBlend = {{
                    .BlendEnable = false,
                }},
                .ColorBlendAttachmentCount = 1,
                .VertexInput =
                    {
                        .Bindings = {bindingDescriptions},
                        .BindingCount = 1,
                        .Attributes = {attributeDescriptions[0], attributeDescriptions[1], attributeDescriptions[2]},
                        .AttributeCount = attributeDescriptions.size(),
                    },
            });
        rhiDevice->SetViewport(context,
                               {
                                   .X = 0,
                                   .Y = 0,
                                   .Width = WINDOW_WIDTH,
                                   .Height = WINDOW_HEIGHT,
                                   .MinDepth = 0.f,
                                   .MaxDepth = 1.f,
                               });
        rhiDevice->SetScissor(context,
                              {
                                  .X = 0,
                                  .Y = 0,
                                  .Width = WINDOW_WIDTH,
                                  .Height = WINDOW_HEIGHT,
                              });
        rhiDevice->BindShader(context, shader);
        rhiDevice->BindDescriptorSet(context, pipelineLayoutHandle, 0, uboDescriptorSet);
        rhiDevice->SetPushConstants(context,
                                    pipelineLayoutHandle,
                                    OZZ::rendering::ShaderStageFlags::Vertex,
                                    0,
                                    sizeof(currentModel),
                                    &currentModel);

        rhiDevice->BindBuffer(context, vertexBuffer);
        rhiDevice->BindBuffer(context, indexBuffer);
        // Bind material
        rhiDevice->DrawIndexed(context, 3, 1, 0, 0, 0);

        rhiDevice->EndRenderPass(context);
        rhiDevice->SubmitAndPresentFrame(std::move(context));
    }

    rhiDevice.reset();
    glfwTerminate();
    return 0;
}