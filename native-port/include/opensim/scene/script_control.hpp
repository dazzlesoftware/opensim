// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <functional>
#include <optional>
namespace opensim::scene {
struct ScriptControlInstance {
    std::function<void(bool)> set_running;
    std::function<void(bool)> set_item_running; // Empty means source m_Item=null.
    std::function<void()> reset;
};
struct ScriptControlBackend {
    std::function<bool(framework::Uuid, framework::Uuid, framework::Uuid)> can_edit;
    // Source reset arguments are object,item,agent (edit is item,object,agent).
    std::function<bool(framework::Uuid, framework::Uuid, framework::Uuid)> can_reset;
    std::function<std::optional<std::uint32_t>(framework::Uuid)> part_local_id;
    std::function<void(std::uint32_t, framework::Uuid)> start, stop, reset_event;
    std::function<std::optional<ScriptControlInstance>(framework::Uuid)> instance;
    std::function<void(std::uint32_t, framework::Uuid)> trace_reset;
    std::function<void(framework::Uuid)> release_urls; // Optional URL module.
};
class ScriptControlController {
  public:
    explicit ScriptControlController(ScriptControlBackend);
    void set_running(framework::Uuid agent, framework::Uuid object, framework::Uuid item, bool) const;
    void process_reset(framework::Uuid agent, framework::Uuid object, framework::Uuid item) const;
    void on_start(std::uint32_t local, framework::Uuid item) const;
    void on_stop(std::uint32_t local, framework::Uuid item) const;
    void on_reset(std::uint32_t local, framework::Uuid item) const;
    void reset_script(framework::Uuid item) const;

  private:
    ScriptControlBackend backend_;
};
// Pure source XMRInstance.Running state transition. Host serializes access as
// source m_QueueLock does. Active PC, sleep deadline and globals are untouched.
// Queue callbacks execute after the source state assignment.
enum class ScriptExecutionState {
    Construct,
    Idle,
    OnStartQueue,
    Running,
    OnSleepQueue,
    RemovedFromSleepQueue,
    OnYieldQueue,
    Finished,
    Suspended,
    Resetting,
    Disposed
};
struct ScriptExecutionControl {
    ScriptExecutionState state = ScriptExecutionState::Construct;
    bool running = true, active_event = false;
    int suspend_count = 0;
    std::size_t pending_events = 0;
};
struct ScriptExecutionQueues {
    std::function<void()> queue_yield, queue_start, remove_sleep, clear_events;
};
void script_set_running(ScriptExecutionControl&, bool, const ScriptExecutionQueues&);
// Exact early PostEvent gate; Continue delegates the remaining source event
// filtering/priority/queue-limit logic to the existing engine adapter.
enum class StoppedScriptEvent { Reject, Continue, Ignore, QueueStateEntry };
StoppedScriptEvent stopped_script_event(const ScriptExecutionControl&, bool known_event, bool has_handler,
                                        bool state_entry);
}
