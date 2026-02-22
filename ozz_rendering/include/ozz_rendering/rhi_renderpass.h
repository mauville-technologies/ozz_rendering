//
// Created by paulm on 2026-02-22.
//

#pragma once

namespace OZZ::rendering {
    struct RenderPassDescriptor {
        struct RenderPassDynamicState {
            // VkViewport viewport {0.f, 0.f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT, 0.f, 1.f};
            // VkRect2D scissor {{0, 0}, {WINDOW_WIDTH, WINDOW_HEIGHT}};
            // vkCmdSetViewportWithCount(commandBuffer, 1, &viewport);
            // vkCmdSetScissorWithCount(commandBuffer, 1, &scissor);
            // // Input assembly
            // vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            // vkCmdSetPrimitiveRestartEnable(commandBuffer, VK_FALSE);
            //
            // // Rasterization
            // vkCmdSetRasterizerDiscardEnable(commandBuffer, VK_FALSE);
            // vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
            // vkCmdSetFrontFace(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            // vkCmdSetDepthBiasEnable(commandBuffer, VK_FALSE);
            //
            // // Depth/stencil
            // vkCmdSetDepthTestEnable(commandBuffer, VK_FALSE);
            // vkCmdSetDepthWriteEnable(commandBuffer, VK_FALSE);
            // vkCmdSetStencilTestEnable(commandBuffer, VK_FALSE);
            //
            // // Vertex input (empty for hardcoded vertices in shader)
            // // Declare the function pointer (at class or file scope)
            // vkCmdSetVertexInputEXT(commandBuffer, 0, nullptr, 0, nullptr);
            // auto colorBlendEnabled = VK_FALSE;
            // vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &colorBlendEnabled);
            //
            // VkColorComponentFlags colorWriteMasks = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            //                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            // vkCmdSetColorWriteMaskEXT(commandBuffer, 0, 1, &colorWriteMasks);
            //
            // vkCmdSetAlphaToCoverageEnableEXT(commandBuffer, VK_FALSE);
            // auto sampleMask = 0xFFFFFFFF;
            // vkCmdSetSampleMaskEXT(commandBuffer, VK_SAMPLE_COUNT_1_BIT, &sampleMask);
            // vkCmdSetRasterizationSamplesEXT(commandBuffer, VK_SAMPLE_COUNT_1_BIT);
            // vkCmdSetPolygonModeEXT(commandBuffer, VK_POLYGON_MODE_FILL);
        };

        RenderPassDynamicState DynamicState;
    };
} // namespace OZZ::rendering