//
// Created by paulm on 2026-02-21.
//

#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include <ozz_rendering/rhi_device.h>

namespace OZZ::rendering {
    template <typename ResourceType>
    struct ResourcePoolSlot {
        std::optional<ResourceType> Resource;
        uint32_t Generation {0};

        [[nodiscard]] bool Occupied() const { return Resource.has_value(); }
    };

    template <typename Tag, typename ResourceType>
    struct ResourcePool {
        using DestroyFunction = std::function<void(ResourceType&)>;

        explicit ResourcePool(DestroyFunction destroyFunc)
            : destroyFunction(std::move(destroyFunc)) {}

        ~ResourcePool() { Empty(); }

        // std::deque: push_back does not invalidate references to existing elements,
        // so pointers returned by Get() remain valid across concurrent Allocate() calls.
        std::deque<ResourcePoolSlot<ResourceType>> Slots;
        std::vector<uint32_t> FreeIndices;

        bool IsValidHandle(const RHIHandle<Tag>& handle) const {
            std::lock_guard lock(mutex);
            return isValidHandleNoLock(handle);
        }

        ResourceType* Get(const RHIHandle<Tag>& handle) {
            std::lock_guard lock(mutex);
            if (isValidHandleNoLock(handle)) {
                return &Slots[handle.Id].Resource.value();
            }
            return nullptr;
        }

        RHIHandle<Tag> Allocate(ResourceType&& resource) {
            std::lock_guard lock(mutex);
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
            std::lock_guard lock(mutex);
            if (isValidHandleNoLock(handle)) {
                if (auto& slot = Slots[handle.Id]; slot.Occupied()) {
                    destroyFunction(Slots[handle.Id].Resource.value());
                    Slots[handle.Id].Resource.reset();
                    FreeIndices.push_back(handle.Id);
                    ++slot.Generation;
                }
            }
        }

        // Invokes fn(handle, resource&) for every occupied slot. Holds the pool lock for
        // the duration, so fn must not call back into this pool's locking methods.
        void ForEach(const std::function<void(const RHIHandle<Tag>&, ResourceType&)>& fn) {
            std::lock_guard lock(mutex);
            for (uint32_t i = 0; i < Slots.size(); ++i) {
                auto& slot = Slots[i];
                if (slot.Occupied()) {
                    fn(RHIHandle<Tag>{.Id = i, .Generation = slot.Generation}, slot.Resource.value());
                }
            }
        }

        void Empty() {
            std::lock_guard lock(mutex);
            for (auto& slot : Slots) {
                if (slot.Occupied()) {
                    destroyFunction(slot.Resource.value());
                    slot.Resource.reset();
                }
            }
            Slots.clear();
            FreeIndices.clear();
        }

    private:
        bool isValidHandleNoLock(const RHIHandle<Tag>& handle) const {
            return handle.Id < Slots.size() && Slots[handle.Id].Generation == handle.Generation &&
                   Slots[handle.Id].Occupied();
        }

        DestroyFunction destroyFunction;
        mutable std::mutex mutex;
    };
} // namespace OZZ::rendering