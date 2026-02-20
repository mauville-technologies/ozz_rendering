//
// Created by paulm on 2026-02-17.
//

#include "ozz_rendering/vulkan_shader.h"

#include "ozz_rendering/util.h"
#include "spdlog/spdlog.h"

#include <fstream>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

namespace OZZ::vk {
    VulkanShader::VulkanShader(VkDevice device, ShaderFileParams&& shaderFiles) {
        // load files
        std::ifstream vertexFile(shaderFiles.Vertex);
        std::ifstream fragmentFile(shaderFiles.Fragment);

        if (!vertexFile.is_open()) {
            throw std::runtime_error("Failed to open vertex shader file");
        }

        if (!fragmentFile.is_open()) {
            throw std::runtime_error("Failed to open fragment shader file");
        }

        std::string vertexSource((std::istreambuf_iterator<char>(vertexFile)), std::istreambuf_iterator<char>());
        std::string fragmentSource((std::istreambuf_iterator<char>(fragmentFile)), std::istreambuf_iterator<char>());
        std::string geometrySource;

        if (!shaderFiles.Geometry.empty()) {
            std::ifstream geometryShader(shaderFiles.Geometry);
            geometrySource = std::string((std::istreambuf_iterator(geometryShader)), std::istreambuf_iterator<char>());
        }

        compileSources(device,
                       {
                           .Vertex = vertexSource,
                           .Geometry = geometrySource,
                           .Fragment = fragmentSource,
                       });
    }

    VulkanShader::VulkanShader(VkDevice device, ShaderSourceParams&& shaderSources) {
        compileSources(device, std::move(shaderSources));
    }

    void VulkanShader::Bind(VkDevice device, VkCommandBuffer commandBuffer) const {
        vkCmdBindShadersEXT(commandBuffer, shaderStages.size(), shaderStages.data(), shaders.data());
    }

    void VulkanShader::Destroy(VkDevice vk_device) {
        for (auto shader : shaders) {
            if (shader != VK_NULL_HANDLE) {
                vkDestroyShaderEXT(vk_device, shader, nullptr);
            }
        }
        shaders.clear();
        shaderStages.clear();
    }

    bool VulkanShader::compileSources(VkDevice device, ShaderSourceParams&& shaderSources) {
        shaderStages.clear();
        shaders.clear();

        auto compiledOpt = compileProgram(shaderSources);
        if (!compiledOpt.has_value()) {
            spdlog::error("Failed to compile shader program. Aborting process. See logs for details.");
            return false;
        }

        auto& compiled = compiledOpt.value();

        std::vector<VkShaderCreateInfoEXT> createInfos;
        // get vertex code
        VkShaderCreateInfoEXT vertexCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
            .codeSize = compiled.VertexSpirv.size() * sizeof(uint32_t),
            .pCode = compiled.VertexSpirv.data(),
            .pName = "main",
            .setLayoutCount = 0,
            .pSetLayouts = nullptr,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
            .pSpecializationInfo = nullptr,
        };

        createInfos.emplace_back(vertexCreateInfo);
        shaderStages.emplace_back(VK_SHADER_STAGE_VERTEX_BIT);

        bool bHasGeo = !shaderSources.Geometry.empty();
        if (bHasGeo) {
            VkShaderCreateInfoEXT geometryCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                .pNext = nullptr,
                .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT,
                .stage = VK_SHADER_STAGE_GEOMETRY_BIT,
                .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
                .codeSize = compiled.GeometrySpirv.size() * sizeof(uint32_t),
                .pCode = compiled.GeometrySpirv.data(),
                .pName = "main",
                .setLayoutCount = 0,
                .pSetLayouts = nullptr,
                .pushConstantRangeCount = 0,
                .pPushConstantRanges = nullptr,
                .pSpecializationInfo = nullptr,
            };

            // vertex will go to geometry instead
            createInfos[0].nextStage = VK_SHADER_STAGE_GEOMETRY_BIT;
            createInfos.emplace_back(geometryCreateInfo);
            shaderStages.emplace_back(VK_SHADER_STAGE_GEOMETRY_BIT);
        }

        VkShaderCreateInfoEXT fragmentCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .nextStage = 0,
            .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
            .codeSize = compiled.FragmentSpirv.size() * sizeof(uint32_t),
            .pCode = compiled.FragmentSpirv.data(),
            .pName = "main",
            .setLayoutCount = 0,
            .pSetLayouts = nullptr,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
            .pSpecializationInfo = nullptr,
        };

        createInfos.emplace_back(fragmentCreateInfo);
        shaderStages.emplace_back(VK_SHADER_STAGE_FRAGMENT_BIT);

        shaders.resize(createInfos.size());

        const auto result = vkCreateShadersEXT(device, createInfos.size(), createInfos.data(), nullptr, shaders.data());
        CHECK_VK_RESULT(result, "Creating shader object");
        spdlog::info("Successfully created shader object");

        if (!bHasGeo) {
            shaders.emplace_back(VK_NULL_HANDLE);
            shaderStages.emplace_back(VK_SHADER_STAGE_GEOMETRY_BIT);
        }

        glslang::FinalizeProcess();
        return true;
    }

    std::optional<CompiledShaderProgram> VulkanShader::compileProgram(const ShaderSourceParams& shaderSources) {
        glslang::InitializeProcess();

        // vertex and fragment are mandatory
        if (shaderSources.Vertex.empty()) {
            spdlog::error("No vertex shader provided. Cannot compile shader");
            return std::nullopt;
        }
        if (shaderSources.Fragment.empty()) {
            spdlog::error("No fragment shader provided. Cannot compile shader");
            return std::nullopt;
        }

        auto shaderProgram = std::make_unique<glslang::TProgram>();
        auto [vSuccess, vertexShader] = compileShader(ShaderStage::Vertex, shaderSources.Vertex);
        if (!vSuccess) {
            return std::nullopt;
        }
        auto [fSuccess, fragmentShader] = compileShader(ShaderStage::Fragment, shaderSources.Fragment);
        if (!fSuccess) {
            return std::nullopt;
        }

        shaderProgram->addShader(vertexShader.get());
        shaderProgram->addShader(fragmentShader.get());

        if (const auto& GeometryShader = shaderSources.Geometry; !GeometryShader.empty()) {
            auto [gSuccess, geometryShader] = compileShader(ShaderStage::Geometry, GeometryShader);
            if (!gSuccess) {
                return std::nullopt;
            }
            shaderProgram->addShader(geometryShader.get());
        }

        if (!shaderProgram->link(EShMessages::EShMsgDefault)) {
            spdlog::error("Failed to link shader program\n{} | {}",
                          shaderProgram->getInfoLog(),
                          shaderProgram->getInfoDebugLog());
            return std::nullopt;
        }

        spdlog::info("Successfully linked and compiled shader");
        CompiledShaderProgram compiled {};
        glslang::GlslangToSpv(*shaderProgram->getIntermediate(ToGLSLANGShaderStage(ShaderStage::Vertex)),
                              compiled.VertexSpirv);
        glslang::GlslangToSpv(*shaderProgram->getIntermediate(ToGLSLANGShaderStage(ShaderStage::Fragment)),
                              compiled.FragmentSpirv);

        if (!shaderSources.Geometry.empty()) {
            glslang::GlslangToSpv(*shaderProgram->getIntermediate(ToGLSLANGShaderStage(ShaderStage::Geometry)),
                                  compiled.GeometrySpirv);
        }
        return compiled;
    }

    std::pair<bool, std::unique_ptr<glslang::TShader>> VulkanShader::compileShader(const ShaderStage stage,
                                                                                   const std::string& glslCode) {
        auto shader = std::make_unique<glslang::TShader>(ToGLSLANGShaderStage(stage));
        const char* code = glslCode.c_str();
        shader->setStrings(&code, 1);

        // What type of shader code
        shader->setEnvInput(glslang::EShSource::EShSourceGlsl,
                            ToGLSLANGShaderStage(stage),
                            glslang::EShClient::EShClientOpenGL,
                            glslang::EShTargetClientVersion::EShTargetVulkan_1_3);

        // What is the client type
        shader->setEnvClient(glslang::EShClient::EShClientVulkan, glslang::EShTargetClientVersion::EShTargetVulkan_1_3);

        // What is the output format
        shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
        if (auto includer = glslang::TShader::ForbidIncluder();

            !shader->parse(GetDefaultResources(),
                           glslang::EshTargetClientVersion::EShTargetVulkan_1_3,
                           false,
                           EShMessages::EShMsgDefault,
                           includer)) {
            spdlog::error("Failed to compile shader of stage {}.\n{}\n{}",
                          static_cast<uint32_t>(stage),
                          shader->getInfoLog(),
                          shader->getInfoDebugLog());
            return {false, nullptr};
        }

        return {true, std::move(shader)};
    }
} // namespace OZZ::vk