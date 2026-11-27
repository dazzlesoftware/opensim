// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace opensim::scene {
// FriendsModule's per-user HashSet cache, shared by the region modules.
class FriendsOnlineCache {
    struct Entry {
        std::unordered_map<framework::Uuid, std::size_t, framework::UuidHash> index;
        std::vector<std::optional<framework::Uuid>> slots;
        std::vector<std::size_t> free;

        void update(framework::Uuid id, bool online) {
            const auto found = index.find(id);
            if (!online) {
                if (found != index.end()) {
                    slots[found->second].reset();
                    free.push_back(found->second);
                    index.erase(found);
                }
            } else if (found == index.end()) {
                const auto slot = free.empty() ? slots.size() : free.back();
                if (free.empty())
                    slots.emplace_back(id);
                else {
                    free.pop_back();
                    slots[slot] = id;
                }
                index.emplace(id, slot);
            }
        }
    };

    mutable std::mutex mutex_;
    std::unordered_map<framework::Uuid, Entry, framework::UuidHash> users_;

  public:
    void update(framework::Uuid user, std::span<const framework::Uuid> friends, bool online) {
        std::lock_guard lock(mutex_);
        auto found = users_.find(user);
        if (found == users_.end()) {
            if (!online)
                return;
            found = users_.try_emplace(user).first;
        }
        for (const auto id : friends)
            found->second.update(id, online);
    }

    bool contains(framework::Uuid user, framework::Uuid friend_id) const {
        std::lock_guard lock(mutex_);
        const auto found = users_.find(user);
        return found != users_.end() && found->second.index.contains(friend_id);
    }

    std::optional<std::vector<framework::Uuid>> get(framework::Uuid user) const {
        std::lock_guard lock(mutex_);
        const auto found = users_.find(user);
        if (found == users_.end())
            return std::nullopt;
        std::vector<framework::Uuid> result;
        for (const auto &slot : found->second.slots)
            if (slot)
                result.push_back(*slot);
        return result;
    }

    void erase(framework::Uuid user) {
        std::lock_guard lock(mutex_);
        users_.erase(user);
    }
};
} // namespace opensim::scene
