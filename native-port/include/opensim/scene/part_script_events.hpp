// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <atomic>
namespace opensim::scene {
namespace script_event {
inline constexpr std::uint64_t collision = 1ull << 4;
inline constexpr std::uint64_t collision_end = 1ull << 5;
inline constexpr std::uint64_t collision_start = 1ull << 6;
inline constexpr std::uint64_t land_collision = 1ull << 11, land_collision_end = 1ull << 12,
                               land_collision_start = 1ull << 13;
inline constexpr std::uint64_t touch = 1ull << 3, email = 1ull << 9, money = 1ull << 16,
                               touch_start = 1ull << 21, touch_end = 1ull << 29, state_entry = 1ull << 30;
inline constexpr std::uint64_t any_touch = touch | touch_start | touch_end,
                               any_target = (1ull << 14) | (1ull << 19) | (1ull << 20) | (1ull << 24);
}
struct PartEmailEvents {
    std::function<void(framework::Uuid)> add, remove;
};
struct PartScriptEventsBackend {
    std::function<bool()> has_group, has_root, has_scene, is_root, allowed_drop;
    std::function<void(framework::Uuid)> remove_targets;
    std::function<std::optional<PartEmailEvents>()> email;
    std::function<void()> aggregate_group, physics_events, full_update;
};
class PartScriptEvents {
public:
    PartScriptEvents(framework::Uuid, PartScriptEventsBackend);
    void set(framework::Uuid, std::uint64_t);
    void remove(framework::Uuid);
    void aggregate();
    std::uint64_t aggregated() const;
    std::uint32_t local_flags() const;
    std::optional<std::uint64_t> events(framework::Uuid) const;
    // Scene model fields may initially be stale before a parent/root is attached.
    void initial_state(std::uint64_t aggregated, std::uint32_t local_flags);

private:
    framework::Uuid part_;
    PartScriptEventsBackend backend_;
    mutable std::recursive_mutex mutex_;
    std::map<framework::Uuid, std::uint64_t> events_;
    std::atomic<std::uint64_t> aggregated_{0};
    std::atomic<std::uint32_t> flags_{0};
};
}
