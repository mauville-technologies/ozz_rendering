#pragma once

#include <ozz_rendering/rhi_descriptors.h>
#include <ozz_rendering/rhi_handle.h>
#include <ozz_rendering/rhi_shader.h>

#include <slang.h>
#include <webgpu/webgpu.h>

#include <string>
#include <vector>

namespace OZZ::rendering::webgpu {

    class RHIShaderWebGPU {
    public:
        RHIShaderWebGPU(WGPUDevice device, slang::IGlobalSession* slangSession, ShaderFileParams&& params);
        RHIShaderWebGPU(WGPUDevice device, slang::IGlobalSession* slangSession, ShaderSourceParams&& params);

        void Destroy();

        [[nodiscard]] bool IsValid() const { return vertexModule != nullptr; }

        std::string vertexEntryPoint   {"vertexMain"};
        std::string fragmentEntryPoint {"fragmentMain"};

        WGPUShaderModule vertexModule   {nullptr};
        WGPUShaderModule fragmentModule {nullptr};

        // Slang 2026.8.1 bug: session->release() triggers heap corruption for shaders
        // with std140 matrix types targeting WGSL. Hold the compile session alive;
        // OS reclaims at program exit.
        slang::ISession* slangCompileSession {nullptr};

        RHIPipelineLayoutDescriptor pipelineLayoutDescriptor {};

        RHIPipelineLayoutHandle pipelineLayoutHandle {};
        std::vector<RHIDescriptorSetLayoutHandle> descriptorSetLayoutHandles {};

    private:
        bool compile(WGPUDevice device, slang::IGlobalSession* slangSession, ShaderSourceParams&& params);
        static RHIPipelineLayoutDescriptor reflectLayout(slang::IComponentType* linked);

        static WGPUShaderModule createWGSLModule(WGPUDevice device,
                                                  const char* wgsl,
                                                  const char* label);
    };

} // namespace OZZ::rendering::webgpu
