//
// Created by paulm on 2026-03-01.
//

#pragma once
#include "ozz_rendering/rhi_descriptors.h"
#include "rhi_vulkan_types.h"
#include <algorithm>
#include <map>
#include <spdlog/spdlog.h>
#include <spirv_reflect.h>
#include <vector>

namespace OZZ::rendering::vk {

    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    inline DescriptorType ConvertSpvDescriptorType(SpvReflectDescriptorType type) {
        switch (type) {
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                return DescriptorType::UniformBuffer;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                return DescriptorType::StorageBuffer;
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                return DescriptorType::CombinedImageSampler;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                return DescriptorType::SampledImage;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                return DescriptorType::Sampler;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                return DescriptorType::StorageImage;
            default:
                spdlog::warn("shader_reflection: unknown SpvReflectDescriptorType {}, defaulting to UniformBuffer",
                             static_cast<uint32_t>(type));
                return DescriptorType::UniformBuffer;
        }
    }

    struct MergedBinding {
        uint32_t Set {0};
        uint32_t Binding {0};
        DescriptorType Type {DescriptorType::UniformBuffer};
        uint32_t Count {1};
        ShaderStageFlags StageFlags {};
    };

    struct MergedPushConstant {
        uint32_t Offset {0};
        uint32_t Size {0};
        ShaderStageFlags StageFlags {};
    };

    // Reflect one SPIR-V stage into the merged maps.
    inline void ReflectStage(const std::vector<uint32_t>& spirv,
                             ShaderStageFlags stage,
                             std::map<uint64_t, MergedBinding>& outBindings, // key: (set << 32 | binding)
                             std::vector<MergedPushConstant>& outPushConstants) {
        if (spirv.empty())
            return;

        SpvReflectShaderModule module {};
        SpvReflectResult result = spvReflectCreateShaderModule(spirv.size() * sizeof(uint32_t), spirv.data(), &module);

        if (result != SPV_REFLECT_RESULT_SUCCESS) {
            spdlog::error("shader_reflection: spvReflectCreateShaderModule failed (result={})",
                          static_cast<int>(result));
            return;
        }

        // --- Descriptor bindings ---
        uint32_t bindingCount = 0;
        result = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr);
        if (result != SPV_REFLECT_RESULT_SUCCESS) {
            spdlog::error("shader_reflection: spvReflectEnumerateDescriptorBindings (count) failed (result={})",
                          static_cast<int>(result));
        } else if (bindingCount > 0) {
            std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
            result = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, bindings.data());
            if (result != SPV_REFLECT_RESULT_SUCCESS) {
                spdlog::error("shader_reflection: spvReflectEnumerateDescriptorBindings (fill) failed (result={})",
                              static_cast<int>(result));
            } else {
                for (const auto* b : bindings) {
                    if (!b)
                        continue;
                    uint64_t key = (static_cast<uint64_t>(b->set) << 32) | static_cast<uint64_t>(b->binding);
                    auto it = outBindings.find(key);
                    if (it != outBindings.end()) {
                        // Seen in another stage — merge stage flags
                        it->second.StageFlags = it->second.StageFlags | stage;
                    } else {
                        outBindings[key] = MergedBinding {
                            .Set = b->set,
                            .Binding = b->binding,
                            .Type = ConvertSpvDescriptorType(b->descriptor_type),
                            .Count = b->count,
                            .StageFlags = stage,
                        };
                    }
                }
            }
        }

        // --- Push constants ---
        uint32_t pushCount = 0;
        result = spvReflectEnumeratePushConstantBlocks(&module, &pushCount, nullptr);
        if (result != SPV_REFLECT_RESULT_SUCCESS) {
            spdlog::error("shader_reflection: spvReflectEnumeratePushConstantBlocks (count) failed (result={})",
                          static_cast<int>(result));
        } else if (pushCount > 0) {
            std::vector<SpvReflectBlockVariable*> pushBlocks(pushCount);
            result = spvReflectEnumeratePushConstantBlocks(&module, &pushCount, pushBlocks.data());
            if (result != SPV_REFLECT_RESULT_SUCCESS) {
                spdlog::error("shader_reflection: spvReflectEnumeratePushConstantBlocks (fill) failed (result={})",
                              static_cast<int>(result));
            } else {
                for (const auto* block : pushBlocks) {
                    if (!block)
                        continue;

                    // Merge with any existing range that overlaps or is adjacent
                    bool merged = false;
                    for (auto& existing : outPushConstants) {
                        uint32_t existEnd = existing.Offset + existing.Size;
                        uint32_t blockEnd = block->offset + block->size;
                        bool overlaps = block->offset <= existEnd && existing.Offset <= blockEnd;
                        if (overlaps) {
                            uint32_t newOffset = std::min(existing.Offset, block->offset);
                            uint32_t newEnd = std::max(existEnd, blockEnd);
                            existing.Offset = newOffset;
                            existing.Size = newEnd - newOffset;
                            existing.StageFlags = existing.StageFlags | stage;
                            merged = true;
                            break;
                        }
                    }
                    if (!merged) {
                        outPushConstants.push_back(MergedPushConstant {
                            .Offset = block->offset,
                            .Size = block->size,
                            .StageFlags = stage,
                        });
                    }
                }
            }
        }

        spvReflectDestroyShaderModule(&module);
    }

    // -------------------------------------------------------------------------
    // Public entry point
    // -------------------------------------------------------------------------

    inline RHIPipelineLayoutDescriptor ReflectPipelineLayoutDescriptor(const CompiledShaderProgram& program) {
        std::map<uint64_t, MergedBinding> mergedBindings;
        std::vector<MergedPushConstant> mergedPushConstants;

        ReflectStage(program.VertexSpirv, ShaderStageFlags::Vertex, mergedBindings, mergedPushConstants);
        ReflectStage(program.GeometrySpirv, ShaderStageFlags::Geometry, mergedBindings, mergedPushConstants);
        ReflectStage(program.FragmentSpirv, ShaderStageFlags::Fragment, mergedBindings, mergedPushConstants);

        RHIPipelineLayoutDescriptor descriptor {};

        // --- Populate descriptor sets ---
        // Group merged bindings by set index
        for (const auto& mergedBinding : mergedBindings | std::views::values) {
            uint32_t setIndex = mergedBinding.Set;
            if (setIndex >= MaxDescriptorSets) {
                spdlog::error("shader_reflection: descriptor set index {} exceeds MaxDescriptorSets ({})",
                              setIndex,
                              static_cast<uint32_t>(MaxDescriptorSets));
                continue;
            }

            // Extend SetCount to cover this set
            if (setIndex >= descriptor.SetCount) {
                descriptor.SetCount = setIndex + 1;
            }

            auto& [Bindings, BindingCount] = descriptor.Sets[setIndex];
            const uint32_t bindingSlot = BindingCount;
            if (bindingSlot >= MaxBoundDescriptorSets) {
                spdlog::error("shader_reflection: too many bindings in set {} (max {})",
                              setIndex,
                              MaxBoundDescriptorSets);
                continue;
            }

            Bindings[bindingSlot] = RHIDescriptorSetLayoutBinding {
                .Binding = mergedBinding.Binding,
                .Type = mergedBinding.Type,
                .Count = mergedBinding.Count,
                .StageFlags = mergedBinding.StageFlags,
            };
            ++BindingCount;
        }

        // Sort push constant ranges by offset for deterministic Vulkan submission order
        std::sort(mergedPushConstants.begin(),
                  mergedPushConstants.end(),
                  [](const MergedPushConstant& a, const MergedPushConstant& b) {
                      return a.Offset < b.Offset;
                  });

        // --- Populate push constant ranges ---
        for (const auto& pc : mergedPushConstants) {
            if (descriptor.PushConstantCount >= MaxPushConstantRanges) {
                spdlog::error("shader_reflection: too many push constant ranges (max {})",
                              static_cast<uint32_t>(MaxPushConstantRanges));
                break;
            }
            descriptor.PushConstants[descriptor.PushConstantCount++] = RHIPushConstantRange {
                .StageFlags = pc.StageFlags,
                .Offset = pc.Offset,
                .Size = pc.Size,
            };
        }

        return descriptor;
    }

} // namespace OZZ::rendering::vk
