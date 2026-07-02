//
// Created by paulm on 2026-02-24.
//

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ozz_rendering/utils/enums.h"

namespace OZZ::rendering {

    // New enum needed — ShaderStage is a plain enum, bindings need a bitmask
    enum class ShaderStageFlags : uint32_t {
        Vertex = 1 << 0,
        Geometry = 1 << 1,
        Fragment = 1 << 2,
        All = 0xFFFFFFFF,
    };

    // A Slang preprocessor macro (name=value). Applied to Slang compile sessions on both
    // backends; ignored by the GLSL compilation paths.
    struct ShaderDefine {
        std::string Name;
        std::string Value;
    };

    struct ShaderSourceParams {
        std::string Vertex;
        std::string Geometry;
        std::string Fragment;
        // Complete Slang module source (vertex+fragment entry points); when non-empty,
        // the Slang compilation path is used instead of the GLSL Vertex/Geometry/Fragment path.
        std::string Slang;
        // Slang preprocessor macros; ignored by GLSL paths.
        std::vector<ShaderDefine> Defines;
    };

    struct ShaderFileParams {
        std::filesystem::path Vertex;
        std::filesystem::path Geometry;
        std::filesystem::path Fragment;
        // Whole-module Slang file; takes precedence over Vertex/Geometry/Fragment when non-empty.
        std::filesystem::path Slang;
        // Slang preprocessor macros; ignored by GLSL paths.
        std::vector<ShaderDefine> Defines;
    };

} // namespace OZZ::rendering

template <>
struct enable_bitmask_operators<OZZ::rendering::ShaderStageFlags> : std::true_type {};
