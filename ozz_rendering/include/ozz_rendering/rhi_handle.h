//
// Created by paulm on 2026-02-22.
//

#pragma once

#include <cstdint>

namespace OZZ::rendering {

    template <typename Tag>
    struct RHIHandle {
        uint32_t Id {UINT32_MAX};
        uint32_t Generation {0};

        static RHIHandle<Tag> Null() {
            return RHIHandle<Tag> {
                .Id = UINT32_MAX,
                .Generation = 0,
            };
        }

        [[nodiscard]] bool IsValid() const { return Id != UINT32_MAX; }
    };

    using RHITextureHandle = RHIHandle<struct TextureTag>;
    using RHICommandBufferHandle = RHIHandle<struct CommandBufferTag>;
    using RHIShaderHandler = RHIHandle<struct ShaderTag>;

} // namespace OZZ::rendering
