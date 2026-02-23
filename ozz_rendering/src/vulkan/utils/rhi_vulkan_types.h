//
// Created by paulm on 2026-02-23.
//

#pragma once

namespace OZZ::rendering::vk {
    inline VkPipelineStageFlags2 ConvertPipelineStageToVulkan(const PipelineStage stage) {
        switch (stage) {
            case PipelineStage::None:
                return VK_PIPELINE_STAGE_NONE;
            case PipelineStage::ColorAttachmentOutput:
                return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            case PipelineStage::Transfer:
                return VK_PIPELINE_STAGE_TRANSFER_BIT;
            case PipelineStage::AllGraphics:
                return VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
            case PipelineStage::AllCommands:
                return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }
        return VK_PIPELINE_STAGE_NONE;
    }

    inline VkAccessFlags2 ConvertAccessToVulkan(const Access access) {
        switch (access) {
            case Access::None:
                return VK_ACCESS_2_NONE;
            case Access::ColorAttachmentRead:
                return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            case Access::ColorAttachmentWrite:
                return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            case Access::ShaderRead:
                return VK_ACCESS_2_SHADER_READ_BIT;
            case Access::TransferRead:
                return VK_ACCESS_2_TRANSFER_READ_BIT;
            case Access::TransferWrite:
                return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        return VK_ACCESS_2_NONE;
    }

    inline VkImageLayout ConvertTextureLayoutToVulkan(const TextureLayout layout) {
        switch (layout) {
            case TextureLayout::Undefined:
                return VK_IMAGE_LAYOUT_UNDEFINED;
            case TextureLayout::ColorAttachment:
                return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            case TextureLayout::DepthStencilAttachment:
                return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            case TextureLayout::ShaderReadOnly:
                return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case TextureLayout::TransferSrc:
                return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            case TextureLayout::TransferDst:
                return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            case TextureLayout::Present:
                return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        return VK_IMAGE_LAYOUT_UNDEFINED;
    }

    inline VkImageAspectFlags ConvertTextureAspectToVulkan(const TextureAspect aspect) {
        switch (aspect) {
            case TextureAspect::Color:
                return VK_IMAGE_ASPECT_COLOR_BIT;
            case TextureAspect::Depth:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            case TextureAspect::Stencil:
                return VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        return VK_IMAGE_ASPECT_FLAG_BITS_MAX_ENUM;
    }

    inline VkAttachmentLoadOp ConvertLoadOpToVulkan(const LoadOp loadOp) {
        switch (loadOp) {
            case LoadOp::DontCare:
                return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            case LoadOp::Load:
                return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOp::Clear:
                return VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    inline VkAttachmentStoreOp ConvertStoreOpToVulkan(const StoreOp storeOp) {
        switch (storeOp) {
            case StoreOp::DontCare:
                return VK_ATTACHMENT_STORE_OP_DONT_CARE;
            case StoreOp::Store:
                return VK_ATTACHMENT_STORE_OP_STORE;
        }
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
} // namespace OZZ::rendering::vk