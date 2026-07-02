//
// Created by paulm on 2026-02-17.
//

#include "rhi_shader_vulkan.h"

#include <fstream>

#include "spdlog/spdlog.h"
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <vulkan/utils/shader_reflection.h>

#include <ozz_rendering/profiling.h>

namespace OZZ::rendering::vk {
    RHIShaderVulkan::RHIShaderVulkan(VkDevice device, ShaderFileParams&& shaderFiles) {
        OZZ_PROFILE_FUNCTION;
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

        bIsValid = compileSources(device,
                                  {
                                      .Vertex = vertexSource,
                                      .Geometry = geometrySource,
                                      .Fragment = fragmentSource,
                                  });
    }

    RHIShaderVulkan::RHIShaderVulkan(VkDevice device, ShaderSourceParams&& shaderSources
#ifdef OZZ_SLANG_ENABLED
        , slang::IGlobalSession* inSlangSession
#endif
    ) {
        OZZ_PROFILE_FUNCTION;
#ifdef OZZ_SLANG_ENABLED
        slangSession = inSlangSession;
#endif
        bIsValid = compileSources(device, std::move(shaderSources));
    }

    void RHIShaderVulkan::Bind(VkDevice device, VkCommandBuffer commandBuffer) const {
        vkCmdBindShadersEXT(commandBuffer, shaderStages.size(), shaderStages.data(), shaders.data());
    }

    void RHIShaderVulkan::Destroy(VkDevice vk_device) {
        for (auto shader : shaders) {
            if (shader != VK_NULL_HANDLE) {
                vkDestroyShaderEXT(vk_device, shader, nullptr);
            }
        }
        shaders.clear();
        shaderStages.clear();
    }

    RHIPipelineLayoutDescriptor RHIShaderVulkan::GetPipelineLayoutDescriptor() const {
        return pipelineLayoutDescriptor;
    }

    bool RHIShaderVulkan::compileSources(VkDevice device, ShaderSourceParams&& shaderSources) {
        OZZ_PROFILE_FUNCTION;
        std::optional<CompiledShaderProgram> compiledOpt;

#ifdef OZZ_SLANG_ENABLED
        if (!shaderSources.Slang.empty()) {
            if (!slangSession) {
                spdlog::error("Slang shader requested but no Slang session provided");
                return false;
            }
            bIsSlang = true;
            compiledOpt = compileProgramSlang(shaderSources);
        } else
#endif
        {
            compiledOpt = compileProgram(shaderSources);
        }

        if (!compiledOpt.has_value()) {
            spdlog::error("Failed to compile shader program. Aborting process. See logs for details.");
            return false;
        }

        compiledProgram = std::move(compiledOpt.value());
        bHasGeometry = !shaderSources.Geometry.empty();
        pipelineLayoutDescriptor = ReflectPipelineLayoutDescriptor(compiledProgram);

        if (!bIsSlang) {
            glslang::FinalizeProcess();
        }
        bIsCompiled = true;
        return true;
    }

    bool RHIShaderVulkan::CreateVkShaders(VkDevice device,
                                          const std::vector<VkDescriptorSetLayout>& setLayouts,
                                          const std::vector<VkPushConstantRange>& pushConstantRanges) {
        OZZ_PROFILE_FUNCTION;
        shaderStages.clear();
        shaders.clear();

        std::vector<VkShaderCreateInfoEXT> createInfos;
        const char* vertexEntryPoint  = "main";
        const char* fragmentEntryPoint = "main";

        VkShaderCreateInfoEXT vertexCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
            .codeSize = compiledProgram.VertexSpirv.size() * sizeof(uint32_t),
            .pCode = compiledProgram.VertexSpirv.data(),
            .pName = vertexEntryPoint,
            .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
            .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
            .pPushConstantRanges = pushConstantRanges.data(),
            .pSpecializationInfo = nullptr,
        };

        createInfos.emplace_back(vertexCreateInfo);
        shaderStages.emplace_back(VK_SHADER_STAGE_VERTEX_BIT);

        if (bHasGeometry) {
            VkShaderCreateInfoEXT geometryCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                .pNext = nullptr,
                .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT,
                .stage = VK_SHADER_STAGE_GEOMETRY_BIT,
                .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
                .codeSize = compiledProgram.GeometrySpirv.size() * sizeof(uint32_t),
                .pCode = compiledProgram.GeometrySpirv.data(),
                .pName = "main",
                .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
                .pSetLayouts = setLayouts.data(),
                .pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
                .pPushConstantRanges = pushConstantRanges.data(),
                .pSpecializationInfo = nullptr,
            };

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
            .codeSize = compiledProgram.FragmentSpirv.size() * sizeof(uint32_t),
            .pCode = compiledProgram.FragmentSpirv.data(),
            .pName = fragmentEntryPoint,
            .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
            .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
            .pPushConstantRanges = pushConstantRanges.data(),
            .pSpecializationInfo = nullptr,
        };

        createInfos.emplace_back(fragmentCreateInfo);
        shaderStages.emplace_back(VK_SHADER_STAGE_FRAGMENT_BIT);

        shaders.resize(createInfos.size());
        if (const auto result =
                vkCreateShadersEXT(device, createInfos.size(), createInfos.data(), nullptr, shaders.data());
            result != VK_SUCCESS) {
            spdlog::error("Failed to create shader objects, error code: {}", static_cast<int>(result));
            return false;
        }
        spdlog::trace("Successfully created shader object");

        if (!bHasGeometry) {
            shaders.emplace_back(VK_NULL_HANDLE);
            shaderStages.emplace_back(VK_SHADER_STAGE_GEOMETRY_BIT);
        }

        bIsValid = true;
        return true;
    }

    std::optional<CompiledShaderProgram> RHIShaderVulkan::compileProgram(const ShaderSourceParams& shaderSources) {
        OZZ_PROFILE_FUNCTION;
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
        auto [vSuccess, vertexShader] = compileShader(ShaderStageFlags::Vertex, shaderSources.Vertex);
        if (!vSuccess) {
            return std::nullopt;
        }
        auto [fSuccess, fragmentShader] = compileShader(ShaderStageFlags::Fragment, shaderSources.Fragment);
        if (!fSuccess) {
            return std::nullopt;
        }

        shaderProgram->addShader(vertexShader.get());
        std::unique_ptr<glslang::TShader> geometryShader;
        if (const auto& GeometryShader = shaderSources.Geometry; !GeometryShader.empty()) {
            auto [gSuccess, geom] = compileShader(ShaderStageFlags::Geometry, GeometryShader);
            geometryShader = std::move(geom);
            if (!gSuccess) {
                return std::nullopt;
            }
            shaderProgram->addShader(geometryShader.get());
        }
        shaderProgram->addShader(fragmentShader.get());

        if (!shaderProgram->link(EShMessages::EShMsgDefault)) {
            spdlog::error("Failed to link shader program\n{} | {}",
                          shaderProgram->getInfoLog(),
                          shaderProgram->getInfoDebugLog());
            return std::nullopt;
        }

        spdlog::trace("Successfully linked and compiled shader");
        CompiledShaderProgram compiled {};
        glslang::GlslangToSpv(*shaderProgram->getIntermediate(ToGLSLANGShaderStage(ShaderStageFlags::Vertex)),
                              compiled.VertexSpirv);
        glslang::GlslangToSpv(*shaderProgram->getIntermediate(ToGLSLANGShaderStage(ShaderStageFlags::Fragment)),
                              compiled.FragmentSpirv);

        if (!shaderSources.Geometry.empty()) {
            glslang::GlslangToSpv(*shaderProgram->getIntermediate(ToGLSLANGShaderStage(ShaderStageFlags::Geometry)),
                                  compiled.GeometrySpirv);
        }
        return compiled;
    }

    std::pair<bool, std::unique_ptr<glslang::TShader>> RHIShaderVulkan::compileShader(const ShaderStageFlags stage,
                                                                                      const std::string& glslCode) {
        OZZ_PROFILE_FUNCTION;
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

#ifdef OZZ_SLANG_ENABLED
    std::optional<CompiledShaderProgram> RHIShaderVulkan::compileProgramSlang(
        const ShaderSourceParams& shaderSources)
    {
        OZZ_PROFILE_FUNCTION;

        std::string combined = shaderSources.Slang;

        slang::TargetDesc targetDesc = {};
        targetDesc.format = SLANG_SPIRV;

        slang::SessionDesc sessionDesc = {};
        sessionDesc.targets     = &targetDesc;
        sessionDesc.targetCount = 1;
        // GLM (used for all matrices uploaded via UBO/push-constant) is column-major;
        // Slang defaults to row-major, which silently transposes every matrix read in
        // shader code (e.g. camera.proj/view, pc.model) unless overridden here.
        sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

        // Slang preprocessor macros. The macro descs hold raw c_str pointers into
        // shaderSources.Defines, which outlives this compile, so no local copy is needed.
        std::vector<slang::PreprocessorMacroDesc> macros;
        macros.reserve(shaderSources.Defines.size());
        for (const auto& def : shaderSources.Defines) {
            macros.push_back({def.Name.c_str(), def.Value.c_str()});
        }
        if (!macros.empty()) {
            sessionDesc.preprocessorMacros     = macros.data();
            sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());
        }

        slang::ISession* session = nullptr;
        if (SLANG_FAILED(slangSession->createSession(sessionDesc, &session)) || !session) {
            spdlog::error("Slang: failed to create session for SPIR-V compilation");
            return std::nullopt;
        }

        ISlangBlob* diagBlob = nullptr;

        slang::IModule* module = session->loadModuleFromSourceString(
            "shader", "shader.slang", combined.c_str(), &diagBlob);
        if (diagBlob) {
            spdlog::warn("Slang diagnostics:\n{}",
                static_cast<const char*>(diagBlob->getBufferPointer()));
            diagBlob->release();
            diagBlob = nullptr;
        }
        if (!module) {
            spdlog::error("Slang: shader module load failed");
            slangCompileSession = session;
            return std::nullopt;
        }

        slang::IEntryPoint* vertEP = nullptr;
        slang::IEntryPoint* fragEP = nullptr;
        module->findAndCheckEntryPoint("vertexMain", SLANG_STAGE_VERTEX, &vertEP, &diagBlob);
        if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
        module->findAndCheckEntryPoint("fragmentMain", SLANG_STAGE_FRAGMENT, &fragEP, &diagBlob);
        if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }

        if (!vertEP) {
            spdlog::error("Slang: no 'vertexMain' entry point found");
            module->release();
            slangCompileSession = session;
            return std::nullopt;
        }
        if (!fragEP) {
            spdlog::error("Slang: no 'fragmentMain' entry point found");
            if (vertEP) vertEP->release();
            module->release();
            slangCompileSession = session;
            return std::nullopt;
        }

        slang::IComponentType* components[] = { module, vertEP, fragEP };
        slang::IComponentType* composite = nullptr;
        session->createCompositeComponentType(components, 3, &composite, &diagBlob);
        if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }

        slang::IComponentType* linked = nullptr;
        if (composite) {
            composite->link(&linked, &diagBlob);
            if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
            composite->release();
        }
        if (!linked) {
            spdlog::error("Slang: shader link failed");
            vertEP->release();
            fragEP->release();
            module->release();
            slangCompileSession = session;
            return std::nullopt;
        }

        auto extractSPIRV = [&](int epIdx) -> std::vector<uint32_t> {
            ISlangBlob* codeBlob = nullptr;
            if (SLANG_FAILED(linked->getEntryPointCode(epIdx, 0, &codeBlob, &diagBlob)) || !codeBlob) {
                if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
                return {};
            }
            if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
            const auto* data = static_cast<const uint32_t*>(codeBlob->getBufferPointer());
            size_t wordCount = codeBlob->getBufferSize() / sizeof(uint32_t);
            std::vector<uint32_t> result(data, data + wordCount);
            codeBlob->release();
            return result;
        };

        // Entry points are in order: vertexMain=0, fragmentMain=1
        CompiledShaderProgram compiled;
        compiled.VertexSpirv   = extractSPIRV(0);
        compiled.FragmentSpirv = extractSPIRV(1);

        linked->release();
        vertEP->release();
        fragEP->release();
        module->release();
        // Slang 2026.8.1 bug: session->release() triggers heap corruption for some
        // shaders. Hold the compile session alive; OS reclaims at program exit. Note
        // this leaks one ISession per shader for the lifetime of the process.
        // TODO(slang>2026.8.1): re-test session->release(); currently crashes / corrupts
        // heap in 2026.8.1.
        slangCompileSession = session;

        if (compiled.VertexSpirv.empty()) {
            spdlog::error("Slang: failed to extract vertex SPIR-V");
            return std::nullopt;
        }
        if (compiled.FragmentSpirv.empty()) {
            spdlog::error("Slang: failed to extract fragment SPIR-V");
            return std::nullopt;
        }
        spdlog::trace("Successfully compiled Slang shader to SPIR-V");
        return compiled;
    }
#endif

} // namespace OZZ::rendering::vk