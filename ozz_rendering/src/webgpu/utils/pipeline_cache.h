#pragma once

#include <ozz_rendering/rhi_handle.h>
#include <ozz_rendering/rhi_pipeline_state.h>

#include <cstring>
#include <unordered_map>
#include <webgpu/webgpu.h>

namespace OZZ::rendering::webgpu {

    // Identifies a unique compiled pipeline state. All fields participate in equality.
    struct PipelineKey {
        RHIShaderHandle shader {};
        GraphicsStateDescriptor state {};
        WGPUTextureFormat colorFormat {WGPUTextureFormat_Undefined};
        WGPUTextureFormat depthFormat {WGPUTextureFormat_Undefined};
        WGPUPipelineLayout pipelineLayout {nullptr};

        bool operator==(const PipelineKey& o) const {
            return std::memcmp(this, &o, sizeof(PipelineKey)) == 0;
        }
    };

    struct PipelineKeyHash {
        size_t operator()(const PipelineKey& key) const {
            size_t h = 0xcbf29ce484222325ULL; // FNV offset basis
            const auto* bytes = reinterpret_cast<const uint8_t*>(&key);
            for (size_t i = 0; i < sizeof(PipelineKey); i++) {
                h ^= static_cast<size_t>(bytes[i]);
                h *= 0x100000001b3ULL; // FNV prime
            }
            return h;
        }
    };

    class PipelineCache {
    public:
        // Returns existing pipeline or creates a new one via factory.
        // factory is called ONLY on a cache miss.
        template <typename Factory>
        WGPURenderPipeline GetOrCreate(const PipelineKey& key, Factory&& factory) {
            auto it = cache.find(key);
            if (it != cache.end()) return it->second;
            WGPURenderPipeline pipeline = factory(key);
            cache.emplace(key, pipeline);
            return pipeline;
        }

        void Clear() {
            for (auto& [key, pipeline] : cache) {
                if (pipeline) wgpuRenderPipelineRelease(pipeline);
            }
            cache.clear();
        }

        ~PipelineCache() { Clear(); }

    private:
        std::unordered_map<PipelineKey, WGPURenderPipeline, PipelineKeyHash> cache;
    };

} // namespace OZZ::rendering::webgpu
