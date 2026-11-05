// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/presence_state.hpp"
#include <stdexcept>
namespace opensim::services {
std::string presence_state_name(PresenceState state) {
    switch (state) {
    case PresenceState::running:
        return "Running";
    case PresenceState::pre_remove:
        return "PreRemove";
    case PresenceState::removing:
        return "Removing";
    case PresenceState::removed:
        return "Removed";
    default:
        return std::to_string(static_cast<std::int32_t>(state));
    }
}
void PresenceStateMachine::set_state(PresenceState next) {
    bool okay = false;
    {
        std::lock_guard lock(mutex_);
        if (next == get_state())
            return;
        if (next == PresenceState::running && get_state() == PresenceState::pre_remove)
            okay = true;
        else if (next == PresenceState::pre_remove && get_state() == PresenceState::running)
            okay = true;
        else if (next == PresenceState::removing) {
            if (get_state() == PresenceState::running || get_state() == PresenceState::pre_remove)
                okay = true;
        } else if (next == PresenceState::removed && get_state() == PresenceState::removing)
            okay = true;
    }
    if (!okay) {
        if (!presence_)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        auto name = presence_->presence_name();
        auto previous = get_state();
        auto scene = presence_->scene_name();
        throw std::runtime_error("Scene presence " + name.value_or("") +
                                 " is not allowed to move from state " + presence_state_name(previous) +
                                 " to new state " + presence_state_name(next) + " in " + scene.value_or(""));
    }
    // The original assigns after leaving its validation lock. Keep this boundary;
    // atomic storage permits concurrent observations without a C++ data race.
    state_.store(next, std::memory_order_relaxed);
}
}
