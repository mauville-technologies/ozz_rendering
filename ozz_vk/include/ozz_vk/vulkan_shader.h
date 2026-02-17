//
// Created by paulm on 2026-02-17.
//

#pragma once

#include <glslang/Public/ShaderLang.h>
#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>

namespace OZZ::vk {
    struct CompiledShaderProgram {
        std::vector<uint32_t> VertexSpirv;
        std::vector<uint32_t> GeometrySpirv;
        std::vector<uint32_t> FragmentSpirv;
    };

    struct ShaderSourceParams {
        std::string Vertex;
        std::string Geometry;
        std::string Fragment;
    };

    struct ShaderFileParams {
        std::filesystem::path Vertex;
        std::filesystem::path Geometry;
        std::filesystem::path Fragment;
    };

    enum class ShaderStage {
        Vertex,
        Geometry,
        Fragment,
    };

    inline EShLanguage ToGLSLANGShaderStage(const ShaderStage stage) {
        switch (stage) {
            case ShaderStage::Vertex:
                return EShLangVertex;
            case ShaderStage::Geometry:
                return EShLangGeometry;
            case ShaderStage::Fragment:
                return EShLangFragment;
        }
        return EShLangCount;
    }

    class VulkanShader {
    public:
        VulkanShader(VkDevice device, ShaderFileParams&& shaderFiles);
        VulkanShader(VkDevice device, ShaderSourceParams&& shaderSources);

    private:
        bool compileSources(VkDevice device, ShaderSourceParams&& shaderSources);
        static std::optional<CompiledShaderProgram> compileProgram(const ShaderSourceParams& shaderSources);
        static std::pair<bool, std::unique_ptr<glslang::TShader>> compileShader(ShaderStage stage,
                                                                                const std::string& glslCode);

    private:
        std::unique_ptr<glslang::TProgram> shaderProgram;
    };

} // namespace OZZ::vk
