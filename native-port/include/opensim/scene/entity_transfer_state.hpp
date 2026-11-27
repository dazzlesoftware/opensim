// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace opensim::scene {
enum class AgentTransferState { Preparing, Transferring, ReceivedAtDestination, CleaningUp, Cancelling, Aborting };
bool transfer_outside_view(float distance, std::uint32_t old_x, std::uint32_t new_x, std::uint32_t old_y,
                           std::uint32_t new_y, int old_size_x, int old_size_y, int new_size_x, int new_size_y);

class EntityTransferStateMachine {
  public:
    using RegionName = std::function<std::string()>;
    using WaitEnabled = std::function<bool()>;
    using Sleep = std::function<void(int)>;
    using Log = std::function<void(std::string_view level, const std::string &message)>;
    EntityTransferStateMachine(RegionName region, WaitEnabled wait, Sleep sleep = {}, Log log = {});
    // The original public property is independent of the module's setting used
    // by WaitForAgentArrivedAtDestination. Retain that distinction.
    std::atomic<bool> enable_wait_for_agent_arrived_at_destination = false;
    bool set_in_transit(framework::Uuid agent);
    bool update_in_transit(framework::Uuid agent, AgentTransferState state);
    std::optional<AgentTransferState> agent_transfer_state(framework::Uuid agent) const;
    bool reset_from_transit(framework::Uuid agent);
    bool wait_for_agent_arrived_at_destination(framework::Uuid agent);
    void set_agent_arrived_at_destination(framework::Uuid agent);

  private:
    mutable std::recursive_mutex mutex_;
    std::map<framework::Uuid, AgentTransferState> agents_;
    RegionName region_;
    WaitEnabled wait_;
    Sleep sleep_;
    Log log_;
};
} // namespace opensim::scene
