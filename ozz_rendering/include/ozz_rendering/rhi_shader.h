//
// Created by paulm on 2026-02-24.
//

#pragma once

#include <filesystem>
#include <string>

#include "ozz_rendering/utils/enums.h"

namespace OZZ::rendering {

    // New enum needed — ShaderStage is a plain enum, bindings need a bitmask
    enum class ShaderStageFlags : uint32_t {
        Vertex = 1 << 0,
        Geometry = 1 << 1,
        Fragment = 1 << 2,
        All = 0xFFFFFFFF,
    };

    struct ShaderSourceParams {
        std::string Vertex;
        std::string Geometry;
        std::string Fragment;
        // Complete Slang module source (vertex+fragment entry points); when non-empty,
        // takes precedence and IsSlang/Vertex are ignored.
        std::string Slang;
        // Legacy: set true to treat Vertex (+Fragment) as Slang source. Prefer `Slang`.
        bool IsSlang = false;
    };

    struct ShaderFileParams {
        std::filesystem::path Vertex;
        std::filesystem::path Geometry;
        std::filesystem::path Fragment;
    };

} // namespace OZZ::rendering

template <>
struct enable_bitmask_operators<OZZ::rendering::ShaderStageFlags> : std::true_type {};
