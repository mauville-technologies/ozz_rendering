//
// Created by paulm on 2026-02-17.
//

#pragma once

#include "ozz_rendering/rhi_descriptors.h"
#include "ozz_rendering/rhi_shader.h"
#include "utils/rhi_vulkan_types.h"

#include <filesystem>
#include <glslang/Public/ShaderLang.h>
#include <memory>

#include <spirv_cross/spirv_reflect.hpp>
#include <volk.h>

namespace OZZ::rendering::vk {

    inline EShLanguage ToGLSLANGShaderStage(const ShaderStageFlags stage) {
        switch (stage) {
            case ShaderStageFlags::Vertex:
                return EShLangVertex;
            case ShaderStageFlags::Geometry:
                return EShLangGeometry;
            case ShaderStageFlags::Fragment:
                return EShLangFragment;
            default:
                break;
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

        RHIPipelineLayoutDescriptor GetPipelineLayoutDescriptor() const;

    private:
        bool compileSources(VkDevice device, ShaderSourceParams&& shaderSources);
        static std::optional<CompiledShaderProgram> compileProgram(const ShaderSourceParams& shaderSources);
        static std::pair<bool, std::unique_ptr<glslang::TShader>> compileShader(ShaderStageFlags stage,
                                                                                const std::string& glslCode);

    private:
        std::vector<VkShaderEXT> shaders {};
        std::vector<VkShaderStageFlagBits> shaderStages {};

        bool bIsValid {false};

        RHIPipelineLayoutDescriptor pipelineLayoutDescriptor {};
    };

} // namespace OZZ::rendering::vk
