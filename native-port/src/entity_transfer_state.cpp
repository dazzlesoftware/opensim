// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/entity_transfer_state.hpp"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace opensim::scene {
bool transfer_outside_view(float distance, std::uint32_t old_x, std::uint32_t new_x, std::uint32_t old_y,
                           std::uint32_t new_y, int old_size_x, int old_size_y, int new_size_x, int new_size_y) {
    distance -= 1.f;
    old_x *= 256u;
    new_x *= 256u;
    if (static_cast<float>(std::int64_t(old_x) + old_size_x) + distance < static_cast<float>(new_x) ||
        static_cast<float>(std::int64_t(new_x) + new_size_x) + distance < static_cast<float>(old_x))
        return true;
    old_y *= 256u;
    new_y *= 256u;
    return static_cast<float>(std::int64_t(old_y) + old_size_y) + distance < static_cast<float>(new_y) ||
           static_cast<float>(std::int64_t(new_y) + new_size_y) + distance < static_cast<float>(old_y);
}

namespace {
std::string name(AgentTransferState state) {
    switch (state) {
    case AgentTransferState::Preparing:
        return "Preparing";
    case AgentTransferState::Transferring:
        return "Transferring";
    case AgentTransferState::ReceivedAtDestination:
        return "ReceivedAtDestination";
    case AgentTransferState::CleaningUp:
        return "CleaningUp";
    case AgentTransferState::Cancelling:
        return "Cancelling";
    case AgentTransferState::Aborting:
        return "Aborting";
    }
    return std::to_string(static_cast<int>(state));
}
} // namespace

EntityTransferStateMachine::EntityTransferStateMachine(RegionName region, WaitEnabled wait, Sleep sleep, Log log)
    : region_(std::move(region)), wait_(std::move(wait)), sleep_(std::move(sleep)), log_(std::move(log)) {
    if (!sleep_)
        sleep_ = [](int milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); };
}

bool EntityTransferStateMachine::set_in_transit(framework::Uuid agent) {
    std::lock_guard lock(mutex_);
    return agents_.try_emplace(agent, AgentTransferState::Preparing).second;
}

bool EntityTransferStateMachine::update_in_transit(framework::Uuid agent, AgentTransferState next) {
    std::lock_guard lock(mutex_);
    const auto found = agents_.find(agent);
    if (found == agents_.end()) {
        if (next == AgentTransferState::Cancelling || next == AgentTransferState::Aborting)
            return false;
        const auto failure = "Agent with ID " + agent.str() + " is not registered as in transit in " + region_();
        if (log_)
            log_("Debug", "[ENTITY TRANSFER STATE MACHINE] UpdateInTransit. Throwing transition failure = " + failure);
        throw std::runtime_error(failure);
    }
    const auto previous = found->second;
    const bool allowed =
        next == AgentTransferState::Aborting ||
        (next == AgentTransferState::CleaningUp && previous != AgentTransferState::CleaningUp) ||
        (next == AgentTransferState::Transferring && previous == AgentTransferState::Preparing) ||
        (next == AgentTransferState::ReceivedAtDestination && previous == AgentTransferState::Transferring) ||
        (next == AgentTransferState::Cancelling &&
         (previous == AgentTransferState::Preparing || previous == AgentTransferState::Transferring));
    if (allowed)
        found->second = next;
    else
        (void)region_(); // The source formats even an ignored transition's failure message.
    return allowed;
}

std::optional<AgentTransferState> EntityTransferStateMachine::agent_transfer_state(framework::Uuid agent) const {
    std::lock_guard lock(mutex_);
    const auto found = agents_.find(agent);
    return found == agents_.end() ? std::nullopt : std::optional(found->second);
}

bool EntityTransferStateMachine::reset_from_transit(framework::Uuid agent) {
    std::lock_guard lock(mutex_);
    return agents_.erase(agent) != 0;
}

bool EntityTransferStateMachine::wait_for_agent_arrived_at_destination(framework::Uuid agent) {
    if (!wait_())
        return true;
    {
        std::lock_guard lock(mutex_);
        const auto state = agent_transfer_state(agent);
        const auto prefix = "Asked to wait for destination callback for agent with ID " + agent.str() + " in ";
        if (!state)
            throw std::runtime_error(prefix + region_() + " but agent is not in transit");
        if (*state != AgentTransferState::Transferring && *state != AgentTransferState::ReceivedAtDestination)
            throw std::runtime_error(prefix + region_() + " but agent is in state " + name(*state));
    }
    int count = 400;
    while (count-- > 0) {
        {
            std::lock_guard lock(mutex_);
            const auto found = agents_.find(agent);
            if (found == agents_.end())
                throw std::out_of_range("The given key '" + agent.str() + "' was not present in the dictionary.");
            if (found->second == AgentTransferState::ReceivedAtDestination)
                break;
        }
        sleep_(100);
    }
    return count > 0;
}

void EntityTransferStateMachine::set_agent_arrived_at_destination(framework::Uuid agent) {
    std::lock_guard lock(mutex_);
    const auto found = agents_.find(agent);
    const auto prefix = "[ENTITY TRANSFER STATE MACHINE]: Region ";
    if (found == agents_.end()) {
        if (log_)
            log_("Warn", prefix + region_() + " received notification of arrival in destination of agent " +
                             agent.str() + " but no teleport request is active");
        return;
    }
    if (found->second == AgentTransferState::ReceivedAtDestination) {
        if (log_)
            log_("Warn", prefix + region_() + " received notification of arrival in destination of agent " +
                             agent.str() + " but notification has already previously been received");
    } else if (found->second != AgentTransferState::Transferring) {
        if (log_)
            log_("Error", prefix + region_() + " received notification of arrival in destination of agent " +
                              agent.str() + " but agent is in state " + name(found->second));
        return;
    }
    // Logging is a callback and can reenter/reset the machine. The original
    // dictionary indexer restores the entry after that callback returns.
    agents_[agent] = AgentTransferState::ReceivedAtDestination;
}
} // namespace opensim::scene
