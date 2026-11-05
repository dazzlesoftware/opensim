// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
namespace opensim::services {
enum class PresenceState : std::int32_t { running, pre_remove, removing, removed };
class PresenceStateIdentity {
  public:
    virtual ~PresenceStateIdentity() = default;
    virtual std::optional<std::string> presence_name() const = 0;
    virtual std::optional<std::string> scene_name() const = 0;
};
class PresenceStateMachine {
  public:
    explicit PresenceStateMachine(std::shared_ptr<PresenceStateIdentity> presence)
        : presence_(std::move(presence)) {}
    PresenceState get_state() const {
        return state_.load(std::memory_order_relaxed);
    }
    void set_state(PresenceState next);

  private:
    std::shared_ptr<PresenceStateIdentity> presence_;
    std::atomic<PresenceState> state_{PresenceState::running};
    std::recursive_mutex mutex_;
};
std::string presence_state_name(PresenceState state);
}
