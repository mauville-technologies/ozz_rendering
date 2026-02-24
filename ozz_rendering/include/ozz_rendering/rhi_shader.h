//
// Created by paulm on 2026-02-24.
//

#pragma once

#include <filesystem>
#include <string>

namespace OZZ::rendering {

    struct ShaderSourceParams {
        std::string Vertex;
        std::string Geometry;
        std::string Fragment;
    };

    struct ShaderFileParams {
        std::filesystem::path Vertex;
        std::filesystem::path Geometry;
        std::filesystem::path Fragment;
    };

} // namespace OZZ::rendering
