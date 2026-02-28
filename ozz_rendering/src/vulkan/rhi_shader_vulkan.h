//
// Created by paulm on 2026-02-17.
//

#pragma once

#include "ozz_rendering/rhi_shader.h"

#include <filesystem>
#include <glslang/Public/ShaderLang.h>
#include <memory>

#include <volk.h>

namespace OZZ::rendering::vk {
    struct CompiledShaderProgram {
        std::vector<uint32_t> VertexSpirv;
        std::vector<uint32_t> GeometrySpirv;
        std::vector<uint32_t> FragmentSpirv;
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

    class RHIShaderVulkan {
    public:
        RHIShaderVulkan(VkDevice device, ShaderFileParams&& shaderFiles);
        RHIShaderVulkan(VkDevice device, ShaderSourceParams&& shaderSources);

        void Bind(VkDevice device, VkCommandBuffer commandBuffer) const;
        void Destroy(VkDevice vk_device);

        [[nodiscard]] bool IsValid() const { return bIsValid; }

    private:
        bool compileSources(VkDevice device, ShaderSourceParams&& shaderSources);
        static std::optional<CompiledShaderProgram> compileProgram(const ShaderSourceParams& shaderSources);
        static std::pair<bool, std::unique_ptr<glslang::TShader>> compileShader(ShaderStage stage,
                                                                                const std::string& glslCode);

    private:
        std::vector<VkShaderEXT> shaders {};
        std::vector<VkShaderStageFlagBits> shaderStages {};

        bool bIsValid {false};
    };

} // namespace OZZ::rendering::vk
