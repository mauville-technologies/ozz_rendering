//
// Created by paulm on 2026-02-21.
//

#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <ozz_rendering/rhi.h>

namespace OZZ::rendering {
    template <typename ResourceType>
    struct ResourcePoolSlot {
        std::optional<ResourceType> Resource;
        uint64_t Generation {0};

        [[nodiscard]] bool Occupied() const { return Resource.has_value(); }
    };

    template <typename Tag, typename ResourceType>
    struct ResourcePool {
        using DestroyFunction = std::function<void(ResourceType&)>;

        explicit ResourcePool(DestroyFunction destroyFunc) : destroyFunction(std::move(destroyFunc)) {}

        std::vector<ResourcePoolSlot<ResourceType>> Slots;
        std::vector<uint32_t> FreeIndices;

        bool IsValidHandle(const RHIHandle<Tag>& handle) const {
            return handle.Id < Slots.size() && Slots[handle.Id].Generation == handle.Generation &&
                   Slots[handle.Id].Occupied();
        }

        ResourceType* Get(const RHIHandle<Tag>& handle) {
            if (IsValidHandle(handle)) {
                return &Slots[handle.Id].Resource.value();
            }
            return nullptr;
        }

        RHIHandle<Tag> Allocate(ResourceType&& resource) {
            uint32_t index;
            if (!FreeIndices.empty()) {
                index = FreeIndices.back();
                FreeIndices.pop_back();
                Slots[index].Resource = std::move(resource);
            } else {
                index = static_cast<uint32_t>(Slots.size());
                Slots.push_back({std::move(resource), 0});
            }
            return RHIHandle<Tag> {
                .Id = index,
                .Generation = Slots[index].Generation,
            };
        }

        void Free(const RHIHandle<Tag>& handle) {
            if (handle.Id < Slots.size() && Slots[handle.Id].Generation == handle.Generation) {
                if (auto& slot = Slots[handle.Id]; slot.Occupied()) {
                    destroyFunction(Slots[handle.Id].Resource.value());
                    Slots[handle.Id].Resource.reset();
                    FreeIndices.push_back(handle.Id);

                    ++slot.Generation;
                }
            }
        }

    private:
        DestroyFunction destroyFunction;
    };
} // namespace OZZ::rendering