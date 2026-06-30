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

#ifdef OZZ_SLANG_ENABLED
#include <slang.h>
#endif

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
        RHIShaderVulkan(VkDevice device, ShaderSourceParams&& shaderSources
#ifdef OZZ_SLANG_ENABLED
            , slang::IGlobalSession* slangSession = nullptr
#endif
        );

        void Bind(VkDevice device, VkCommandBuffer commandBuffer) const;
        void Destroy(VkDevice vk_device);

        [[nodiscard]] bool IsValid() const { return bIsValid; }

        [[nodiscard]] bool IsCompiled() const { return bIsCompiled; }

        RHIPipelineLayoutDescriptor GetPipelineLayoutDescriptor() const;

        bool CreateVkShaders(VkDevice device,
                             const std::vector<VkDescriptorSetLayout>& setLayouts,
                             const std::vector<VkPushConstantRange>& pushConstantRanges);

        // Owned layout handles — set by RHIDeviceVulkan after CreateVkShaders
        RHIPipelineLayoutHandle pipelineLayoutHandle {};
        std::vector<RHIDescriptorSetLayoutHandle> descriptorSetLayoutHandles {};

    private:
        bool compileSources(VkDevice device, ShaderSourceParams&& shaderSources);
        static std::optional<CompiledShaderProgram> compileProgram(const ShaderSourceParams& shaderSources);
        static std::pair<bool, std::unique_ptr<glslang::TShader>> compileShader(ShaderStageFlags stage,
                                                                                const std::string& glslCode);
#ifdef OZZ_SLANG_ENABLED
        std::optional<CompiledShaderProgram> compileProgramSlang(const ShaderSourceParams& shaderSources);
#endif

    private:
        std::vector<VkShaderEXT> shaders {};
        std::vector<VkShaderStageFlagBits> shaderStages {};

        CompiledShaderProgram compiledProgram {};
        bool bHasGeometry {false};
        bool bIsValid {false};
        bool bIsCompiled {false};
        bool bIsSlang {false};

        RHIPipelineLayoutDescriptor pipelineLayoutDescriptor {};

#ifdef OZZ_SLANG_ENABLED
        slang::IGlobalSession* slangSession {nullptr};
        // Slang 2026.8.1 bug: session->release() triggers heap corruption for some
        // shaders. Hold the compile session alive; OS reclaims at program exit.
        slang::ISession* slangCompileSession {nullptr};
#endif
    };

} // namespace OZZ::rendering::vk
