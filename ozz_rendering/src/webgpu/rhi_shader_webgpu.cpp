#include "rhi_shader_webgpu.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <slang.h>

namespace OZZ::rendering::webgpu {

    static std::string readFile(const std::filesystem::path& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("Failed to open shader file: " + path.string());
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    WGPUShaderModule RHIShaderWebGPU::createWGSLModule(WGPUDevice device,
                                                        const char* wgsl,
                                                        const char* label) {
        WGPUShaderSourceWGSL wgslDesc = {};
        wgslDesc.chain.sType  = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code.data    = wgsl;
        wgslDesc.code.length  = WGPU_STRLEN;

        WGPUShaderModuleDescriptor desc = {};
        desc.nextInChain = &wgslDesc.chain;
        desc.label       = label;

        return wgpuDeviceCreateShaderModule(device, &desc);
    }

    RHIPipelineLayoutDescriptor RHIShaderWebGPU::reflectLayout(slang::IComponentType* linked) {
        RHIPipelineLayoutDescriptor result {};

        slang::ProgramLayout* layout = linked->getLayout(0);
        if (!layout) return result;

        unsigned paramCount = layout->getParameterCount();
        for (unsigned i = 0; i < paramCount; i++) {
            slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
            if (!param) continue;

            uint32_t setIndex  = static_cast<uint32_t>(param->getBindingSpace());
            uint32_t bindIndex = static_cast<uint32_t>(param->getBindingIndex());

            // Set 3 is the dedicated push-constant slot; mark it as a push constant
            // regardless of whether [[vk::push_constant]] or [vk::binding(0,3)] was used.
            if (setIndex == 3U) {
                if (result.PushConstantCount == 0) {
                    result.PushConstants[result.PushConstantCount++] = {
                        ShaderStageFlags::Vertex | ShaderStageFlags::Fragment,
                        0,
                        256,
                    };
                }
                continue;
            }

            if (setIndex >= MaxDescriptorSets) continue;

            slang::TypeReflection* type = param->getType();
            if (!type) continue;
            slang::TypeReflection::Kind kind = type->getKind();

            DescriptorType descType;
            switch (kind) {
                case slang::TypeReflection::Kind::ConstantBuffer:
                    descType = DescriptorType::UniformBuffer;
                    break;
                case slang::TypeReflection::Kind::Resource: {
                    SlangResourceShape  shape     = type->getResourceShape();
                    SlangResourceAccess access    = type->getResourceAccess();
                    SlangResourceShape  baseShape = static_cast<SlangResourceShape>(
                        shape & SLANG_RESOURCE_BASE_SHAPE_MASK);
                    // Texture shapes: SLANG_TEXTURE_1D(1)..SLANG_TEXTURE_BUFFER(5), SLANG_TEXTURE_SUBPASS(0x0A)
                    // Buffer shapes:  SLANG_STRUCTURED_BUFFER(6), SLANG_BYTE_ADDRESS_BUFFER(7), etc.
                    bool isTexture = (baseShape >= SLANG_TEXTURE_1D && baseShape <= SLANG_TEXTURE_BUFFER)
                                  || baseShape == SLANG_TEXTURE_SUBPASS;
                    (void)access;
                    descType = isTexture ? DescriptorType::SampledImage : DescriptorType::StorageBuffer;
                    break;
                }
                case slang::TypeReflection::Kind::SamplerState:
                    descType = DescriptorType::Sampler;
                    break;
                default:
                    continue;
            }

            auto& setDesc = result.Sets[setIndex];
            if (setIndex + 1 > result.SetCount) result.SetCount = setIndex + 1;
            if (setDesc.BindingCount < MaxBoundDescriptorSets) {
                setDesc.Bindings[setDesc.BindingCount++] = {
                    bindIndex,
                    descType,
                    1,
                    ShaderStageFlags::All,
                };
            }
        }

        // getGlobalConstantBufferBinding: implicit aggregated constant buffer (rare in
        // explicit-binding shaders). Only add if the set-3 path hasn't already done so.
        if (result.PushConstantCount == 0) {
            SlangUInt gcbBinding = layout->getGlobalConstantBufferBinding();
            if (gcbBinding != SLANG_UNBOUNDED_SIZE && gcbBinding != SLANG_UNKNOWN_SIZE) {
                result.PushConstants[result.PushConstantCount++] = {
                    ShaderStageFlags::Vertex | ShaderStageFlags::Fragment,
                    0,
                    256,
                };
            }
        }

        return result;
    }

    RHIShaderWebGPU::RHIShaderWebGPU(WGPUDevice device,
                                      slang::IGlobalSession* slangSession,
                                      ShaderFileParams&& params) {
        ShaderSourceParams src;
        if (!params.Vertex.empty())   src.Vertex   = readFile(params.Vertex);
        if (!params.Fragment.empty()) src.Fragment = readFile(params.Fragment);
        if (!params.Geometry.empty()) src.Geometry = readFile(params.Geometry);
        compile(device, slangSession, std::move(src));
    }

    RHIShaderWebGPU::RHIShaderWebGPU(WGPUDevice device,
                                      slang::IGlobalSession* slangSession,
                                      ShaderSourceParams&& params) {
        compile(device, slangSession, std::move(params));
    }

    bool RHIShaderWebGPU::compile(WGPUDevice device,
                                   slang::IGlobalSession* globalSession,
                                   ShaderSourceParams&& params) {
        std::string combined = params.Vertex;
        if (!params.Fragment.empty() && params.Fragment != params.Vertex) {
            combined += "\n";
            combined += params.Fragment;
        }

        // WebGPU has no native push constants. Replace GLSL layout(push_constant) blocks
        // with a plain uniform buffer at set=3, binding=0 (the dedicated PC slot).
        static const std::string kPushConstSearch  = "layout(push_constant) uniform";
        static const std::string kPushConstReplace = "layout(set = 3, binding = 0) uniform";
        for (size_t pos = 0;
             (pos = combined.find(kPushConstSearch, pos)) != std::string::npos; ) {
            combined.replace(pos, kPushConstSearch.size(), kPushConstReplace);
            pos += kPushConstReplace.size();
        }

        // Slang: [[vk::push_constant]] does not emit @group/@binding in WGSL output.
        // Replace with an explicit binding at the dedicated push-constant slot (set=3, binding=0)
        // so Slang generates valid @group(3) @binding(0) decorations.
        static const std::string kSlangPCSearch  = "[[vk::push_constant]]";
        static const std::string kSlangPCReplace = "[vk::binding(0, 3)]";
        for (size_t pos = 0;
             (pos = combined.find(kSlangPCSearch, pos)) != std::string::npos; ) {
            combined.replace(pos, kSlangPCSearch.size(), kSlangPCReplace);
            pos += kSlangPCReplace.size();
        }

        slang::TargetDesc targetDesc = {};
        targetDesc.format = SLANG_WGSL;

        slang::SessionDesc sessionDesc = {};
        sessionDesc.targets     = &targetDesc;
        sessionDesc.targetCount = 1;

        slang::ISession* session = nullptr;
        if (SLANG_FAILED(globalSession->createSession(sessionDesc, &session)) || !session) {
            spdlog::error("Slang: failed to create compile session");
            return false;
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
            slangCompileSession = session;  // hold to avoid corrupt-free crash
            return false;
        }
        // For Slang shaders both entry points live in params.Vertex (combined source).
        // Always search for both; Slang returns null non-fatally if one is absent.
        slang::IEntryPoint* vertEP = nullptr;
        slang::IEntryPoint* fragEP = nullptr;
        module->findAndCheckEntryPoint(
            vertexEntryPoint.c_str(), SLANG_STAGE_VERTEX, &vertEP, &diagBlob);
        if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
        module->findAndCheckEntryPoint(
            fragmentEntryPoint.c_str(), SLANG_STAGE_FRAGMENT, &fragEP, &diagBlob);
        if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
        std::vector<slang::IComponentType*> comps;
        comps.push_back(module);
        if (vertEP) comps.push_back(vertEP);
        if (fragEP) comps.push_back(fragEP);

        slang::IComponentType* composite = nullptr;
        session->createCompositeComponentType(
            comps.data(), static_cast<SlangInt>(comps.size()), &composite, &diagBlob);
        if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }

        slang::IComponentType* linked = nullptr;
        if (composite) {
            composite->link(&linked, &diagBlob);
            if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
            composite->release();
        }

        if (!linked) {
            spdlog::error("Slang: shader link failed");
            if (vertEP) vertEP->release();
            if (fragEP) fragEP->release();
            module->release();
            slangCompileSession = session;  // hold to avoid corrupt-free crash
            return false;
        }

        pipelineLayoutDescriptor = reflectLayout(linked);

        // Entry point indices in the composite: module is comp[0], vertEP is next, fragEP after
        int epIdx          = 0;
        int vertEPCompIdx  = vertEP ? epIdx++ : -1;
        int fragEPCompIdx  = fragEP ? epIdx++ : -1;

        auto extractWGSL = [&](int compEPIdx, const char* label) -> WGPUShaderModule {
            ISlangBlob* codeBlob = nullptr;
            if (SLANG_FAILED(linked->getEntryPointCode(compEPIdx, 0, &codeBlob, &diagBlob))
                    || !codeBlob) {
                if (diagBlob) {
                    spdlog::error("Slang WGSL gen failed ({}): {}", label,
                                  static_cast<const char*>(diagBlob->getBufferPointer()));
                    diagBlob->release(); diagBlob = nullptr;
                }
                return nullptr;
            }
            if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
            const char* wgsl = static_cast<const char*>(codeBlob->getBufferPointer());
            WGPUShaderModule mod = createWGSLModule(device, wgsl, label);
            codeBlob->release();
            return mod;
        };

        if (vertEPCompIdx >= 0) vertexModule   = extractWGSL(vertEPCompIdx, "vertex");
        if (fragEPCompIdx >= 0) fragmentModule  = extractWGSL(fragEPCompIdx, "fragment");

        linked->release();
        if (vertEP) vertEP->release();
        if (fragEP) fragEP->release();
        module->release();
        // Slang 2026.8.1 bug: session->release() triggers "free(): corrupted unsorted
        // chunks" for shaders that generate std140 matrix wrapper types in WGSL.
        // Keep the session alive; the OS reclaims memory at program exit.
        slangCompileSession = session;

        return vertexModule != nullptr;
    }

    void RHIShaderWebGPU::Destroy() {
        if (fragmentModule && fragmentModule != vertexModule) {
            wgpuShaderModuleRelease(fragmentModule);
        }
        if (vertexModule) wgpuShaderModuleRelease(vertexModule);
        vertexModule   = nullptr;
        fragmentModule = nullptr;
    }

} // namespace OZZ::rendering::webgpu
