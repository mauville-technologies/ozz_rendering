#include "rhi_shader_webgpu.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <slang.h>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <spirv_reflect.h>

namespace OZZ::rendering::webgpu {

    static EShLanguage toGLSLANGStage(ShaderStageFlags stage) {
        switch (stage) {
            case ShaderStageFlags::Vertex:   return EShLangVertex;
            case ShaderStageFlags::Fragment: return EShLangFragment;
            case ShaderStageFlags::Geometry: return EShLangGeometry;
            default:                         return EShLangCount;
        }
    }

    static std::optional<std::vector<uint32_t>> compileGLSLtoSPIRV(ShaderStageFlags stage,
                                                                     const std::string& glsl) {
        auto shader = std::make_unique<glslang::TShader>(toGLSLANGStage(stage));
        const char* code = glsl.c_str();
        shader->setStrings(&code, 1);
        shader->setEnvInput(glslang::EShSource::EShSourceGlsl,
                            toGLSLANGStage(stage),
                            glslang::EShClient::EShClientVulkan,
                            glslang::EShTargetClientVersion::EShTargetVulkan_1_1);
        shader->setEnvClient(glslang::EShClient::EShClientVulkan,
                             glslang::EShTargetClientVersion::EShTargetVulkan_1_1);
        shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);

        auto includer = glslang::TShader::ForbidIncluder();
        if (!shader->parse(GetDefaultResources(),
                           glslang::EshTargetClientVersion::EShTargetVulkan_1_1,
                           false,
                           EShMessages::EShMsgDefault,
                           includer)) {
            spdlog::error("WebGPU: GLSL compile failed (stage {}):\n{}\n{}",
                          static_cast<uint32_t>(stage),
                          shader->getInfoLog(), shader->getInfoDebugLog());
            return std::nullopt;
        }

        auto prog = std::make_unique<glslang::TProgram>();
        prog->addShader(shader.get());
        if (!prog->link(EShMessages::EShMsgDefault)) {
            spdlog::error("WebGPU: GLSL link failed:\n{}\n{}",
                          prog->getInfoLog(), prog->getInfoDebugLog());
            return std::nullopt;
        }

        std::vector<uint32_t> spirv;
        glslang::GlslangToSpv(*prog->getIntermediate(toGLSLANGStage(stage)), spirv);
        return spirv;
    }

    static WGPUShaderModule createSPIRVModule(WGPUDevice device,
                                               const std::vector<uint32_t>& spirv,
                                               const char* label) {
        WGPUShaderSourceSPIRV spirvDesc = {};
        spirvDesc.chain.sType = WGPUSType_ShaderSourceSPIRV;
        spirvDesc.codeSize    = static_cast<uint32_t>(spirv.size());
        spirvDesc.code        = spirv.data();

        WGPUShaderModuleDescriptor desc = {};
        desc.nextInChain = &spirvDesc.chain;
        desc.label       = label;
        return wgpuDeviceCreateShaderModule(device, &desc);
    }

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

    static void reflectSPIRVStage(const std::vector<uint32_t>& spirv,
                                   ShaderStageFlags stage,
                                   RHIPipelineLayoutDescriptor& out) {
        if (spirv.empty()) return;
        SpvReflectShaderModule mod {};
        if (spvReflectCreateShaderModule(spirv.size() * sizeof(uint32_t), spirv.data(), &mod)
                != SPV_REFLECT_RESULT_SUCCESS) {
            spdlog::error("WebGPU: spvReflectCreateShaderModule failed");
            return;
        }

        uint32_t count = 0;
        spvReflectEnumerateDescriptorBindings(&mod, &count, nullptr);
        if (count > 0) {
            std::vector<SpvReflectDescriptorBinding*> bindings(count);
            spvReflectEnumerateDescriptorBindings(&mod, &count, bindings.data());
            for (const auto* b : bindings) {
                if (!b || b->set >= MaxDescriptorSets) continue;
                // set=3 binding=0 is the push-constant slot — expose as UniformBuffer
                auto& set = out.Sets[b->set];
                if (b->set + 1 > out.SetCount) out.SetCount = b->set + 1;
                DescriptorType dt = DescriptorType::UniformBuffer;
                if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    dt = DescriptorType::CombinedImageSampler;
                else if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                    dt = DescriptorType::SampledImage;
                else if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER)
                    dt = DescriptorType::Sampler;
                else if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    dt = DescriptorType::StorageBuffer;
                if (b->binding >= MaxBoundDescriptorSets) continue;
                set.Bindings[b->binding] = {b->binding, dt, b->count, stage};
                if (b->binding + 1 > set.BindingCount) set.BindingCount = b->binding + 1;
            }
        }

        // Push constants → remap to set=3, binding=0 in WebGPU
        count = 0;
        spvReflectEnumeratePushConstantBlocks(&mod, &count, nullptr);
        if (count > 0) {
            auto& set3 = out.Sets[3];
            if (out.SetCount <= 3) out.SetCount = 4;
            // Only add binding 0 in set 3 once
            if (set3.BindingCount == 0) {
                set3.Bindings[0] = {0, DescriptorType::UniformBuffer, 1,
                                    ShaderStageFlags::Vertex | ShaderStageFlags::Fragment};
                set3.BindingCount = 1;
            }
        }

        spvReflectDestroyShaderModule(&mod);
    }

    static RHIPipelineLayoutDescriptor reflectGLSLLayout(const std::vector<uint32_t>& vert,
                                                          const std::vector<uint32_t>& frag) {
        RHIPipelineLayoutDescriptor out {};
        reflectSPIRVStage(vert, ShaderStageFlags::Vertex, out);
        reflectSPIRVStage(frag, ShaderStageFlags::Fragment, out);
        return out;
    }

    // -------------------------------------------------------------------------
    // GLSL sampler2D splitter: rewrites "uniform sampler2D NAME" into separate
    // "uniform texture2D" + "uniform sampler" with interleaved binding slots so
    // the resulting SPIR-V is WebGPU-compliant (no combined image-samplers).
    // -------------------------------------------------------------------------

    struct SamplerPatch {
        std::string name;
        uint32_t originalBinding;   // binding as declared in the GLSL source
        uint32_t newTextureBinding; // interleaved binding for texture2D
        uint32_t samplerBinding;    // = newTextureBinding + 1
        uint32_t setIndex;          // set the sampler2D was in (usually 0)
    };

    static std::string updateBindingInLayout(const std::string& layoutSpec, uint32_t newBinding) {
        auto pos = layoutSpec.find("binding");
        if (pos == std::string::npos) return layoutSpec;
        auto eq = layoutSpec.find('=', pos);
        if (eq == std::string::npos) return layoutSpec;
        size_t start = eq + 1;
        while (start < layoutSpec.size() && layoutSpec[start] == ' ') ++start;
        size_t end = start;
        while (end < layoutSpec.size() && std::isdigit(static_cast<unsigned char>(layoutSpec[end]))) ++end;
        std::string result = layoutSpec;
        result.replace(start, end - start, std::to_string(newBinding));
        return result;
    }

    // Rewrites the GLSL source and fills `patches` with the binding mapping.
    static std::string patchGLSLSamplers(const std::string& src, std::vector<SamplerPatch>& patches) {
        struct DeclInfo {
            size_t pos;
            size_t len;
            std::string layoutSpec;
            uint32_t binding;
            uint32_t setIndex;
            std::string name;
        };
        std::vector<DeclInfo> decls;

        std::regex declRe(R"((layout\s*\([^)]*\))\s*uniform\s+sampler2D\s+(\w+)\s*;)");
        std::regex bindNumRe(R"(binding\s*=\s*(\d+))");
        std::regex setNumRe(R"(set\s*=\s*(\d+))");

        for (auto it = std::sregex_iterator(src.begin(), src.end(), declRe);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            std::string layoutSpec = m[1].str();
            std::string name       = m[2].str();
            uint32_t binding = 0, setIdx = 0;
            std::smatch bm, sm;
            if (std::regex_search(layoutSpec, bm, bindNumRe))
                binding = static_cast<uint32_t>(std::stoul(bm[1].str()));
            if (std::regex_search(layoutSpec, sm, setNumRe))
                setIdx = static_cast<uint32_t>(std::stoul(sm[1].str()));
            decls.push_back({static_cast<size_t>(m.position()),
                             static_cast<size_t>(m.length()),
                             layoutSpec, binding, setIdx, name});
        }

        if (decls.empty()) return src;

        // Sort by original binding to assign interleaved slots in order.
        std::sort(decls.begin(), decls.end(),
                  [](const DeclInfo& a, const DeclInfo& b) { return a.binding < b.binding; });

        uint32_t B_start = decls[0].binding;
        for (size_t i = 0; i < decls.size(); i++) {
            uint32_t newTex = B_start + static_cast<uint32_t>(i) * 2;
            patches.push_back({decls[i].name, decls[i].binding, newTex, newTex + 1, decls[i].setIndex});
        }

        // Map original binding → patch index for later lookup.
        std::map<uint32_t, size_t> patchIdx;
        for (size_t i = 0; i < patches.size(); i++) patchIdx[patches[i].originalBinding] = i;

        // Replace declarations in reverse order so positions stay valid.
        std::sort(decls.begin(), decls.end(),
                  [](const DeclInfo& a, const DeclInfo& b) { return a.pos > b.pos; });

        std::string result = src;
        for (const auto& d : decls) {
            const auto& p = patches[patchIdx[d.binding]];
            std::string texLayout  = updateBindingInLayout(d.layoutSpec, p.newTextureBinding);
            std::string sampLayout = updateBindingInLayout(d.layoutSpec, p.samplerBinding);
            std::string replacement =
                texLayout  + " uniform texture2D " + d.name + ";\n" +
                sampLayout + " uniform sampler "   + d.name + "_sampler;";
            result.replace(d.pos, d.len, replacement);
        }

        // Rewrite texture/textureSize/texelFetch calls: sampler2D constructor is needed
        // for all texture builtins when the type is texture2D (not a combined sampler2D).
        for (const auto& p : patches) {
            auto combined = "sampler2D(" + p.name + ", " + p.name + "_sampler)";
            std::regex texCallRe("texture\\s*\\(\\s*" + p.name + "\\s*,");
            result = std::regex_replace(result, texCallRe, "texture(" + combined + ",");
            std::regex texSizeRe("textureSize\\s*\\(\\s*" + p.name + "\\s*,");
            result = std::regex_replace(result, texSizeRe, "textureSize(" + combined + ",");
            std::regex texFetchRe("texelFetch\\s*\\(\\s*" + p.name + "\\s*,");
            result = std::regex_replace(result, texFetchRe, "texelFetch(" + combined + ",");
        }

        return result;
    }

    // Post-processes the reflected layout: merges SampledImage@B + Sampler@(B+1) pairs
    // (produced by the patcher) back into CombinedImageSampler@B, preserving the
    // OriginalBinding so material.cpp can find the entry using the pre-patch binding number.
    // The consumed Sampler entries are cleared (Count=0) so CreateDescriptorSetLayout skips them.
    static void mergeGLSLSamplerPatches(RHIPipelineLayoutDescriptor& layout,
                                         const std::vector<SamplerPatch>& patches) {
        for (const auto& p : patches) {
            if (p.setIndex >= MaxDescriptorSets) continue;
            if (p.newTextureBinding >= MaxBoundDescriptorSets) continue;
            if (p.samplerBinding    >= MaxBoundDescriptorSets) continue;

            auto& setDesc   = layout.Sets[p.setIndex];
            auto& texEntry  = setDesc.Bindings[p.newTextureBinding];
            auto& sampEntry = setDesc.Bindings[p.samplerBinding];

            if (texEntry.Count == 0 || sampEntry.Count == 0) continue;

            texEntry.Type           = DescriptorType::CombinedImageSampler;
            texEntry.OriginalBinding = p.originalBinding;
            sampEntry.Count          = 0; // consumed — CreateDescriptorSetLayout will skip this slot
        }
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

    bool RHIShaderWebGPU::compileGLSL(WGPUDevice device, ShaderSourceParams&& params) {
        // WebGPU has no native push constants; rebind push_constant block to set=3, binding=0.
        auto patchPC = [](std::string src) {
            static const std::string kSearch  = "layout(push_constant) uniform";
            static const std::string kReplace = "layout(set = 3, binding = 0) uniform";
            for (size_t pos = 0; (pos = src.find(kSearch, pos)) != std::string::npos; )
                src.replace(pos, kSearch.size(), kReplace), pos += kReplace.size();
            return src;
        };

        glslang::InitializeProcess();

        std::vector<SamplerPatch> vertPatches, fragPatches;
        std::vector<uint32_t> vertSpirv, fragSpirv;
        if (!params.Vertex.empty()) {
            auto patched = patchGLSLSamplers(patchPC(params.Vertex), vertPatches);
            auto v = compileGLSLtoSPIRV(ShaderStageFlags::Vertex, patched);
            if (!v) { glslang::FinalizeProcess(); return false; }
            vertSpirv = std::move(*v);
        }
        if (!params.Fragment.empty()) {
            auto patched = patchGLSLSamplers(patchPC(params.Fragment), fragPatches);
            auto f = compileGLSLtoSPIRV(ShaderStageFlags::Fragment, patched);
            if (!f) { glslang::FinalizeProcess(); return false; }
            fragSpirv = std::move(*f);
        }

        glslang::FinalizeProcess();

        if (!vertSpirv.empty())
            vertexModule = createSPIRVModule(device, vertSpirv, "vertex");
        if (!fragSpirv.empty())
            fragmentModule = createSPIRVModule(device, fragSpirv, "fragment");

        vertexEntryPoint   = "main";
        fragmentEntryPoint = "main";
        pipelineLayoutDescriptor = reflectGLSLLayout(vertSpirv, fragSpirv);

        // Merge the split SampledImage+Sampler pairs back into CombinedImageSampler
        // entries with the original binding number preserved for material lookup.
        mergeGLSLSamplerPatches(pipelineLayoutDescriptor, vertPatches);
        mergeGLSLSamplerPatches(pipelineLayoutDescriptor, fragPatches);

        return vertexModule != nullptr;
    }

    bool RHIShaderWebGPU::compile(WGPUDevice device,
                                   slang::IGlobalSession* globalSession,
                                   ShaderSourceParams&& params) {
        if (!params.IsSlang) {
            return compileGLSL(device, std::move(params));
        }

        std::string combined = params.Vertex;
        if (!params.Fragment.empty() && params.Fragment != params.Vertex) {
            combined += "\n";
            combined += params.Fragment;
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
