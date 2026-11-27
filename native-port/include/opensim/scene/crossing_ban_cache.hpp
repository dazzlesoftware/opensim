// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <cstdint>
#include <memory>

namespace opensim::framework {
struct ExpiringCacheBackend;
}

namespace opensim::scene {
class CrossingBanCache {
  public:
    CrossingBanCache();
    explicit CrossingBanCache(framework::ExpiringCacheBackend);
    ~CrossingBanCache();
    bool if_banned(std::uint64_t region, framework::Uuid agent);
    void add(std::uint64_t region, framework::Uuid agent, double seconds);
    void remove(std::uint64_t region, framework::Uuid agent);
    void dispose();

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace opensim::scene
