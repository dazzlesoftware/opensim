// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/script_control.hpp"
#include <stdexcept>
namespace opensim::scene {
ScriptControlController::ScriptControlController(ScriptControlBackend backend)
    : backend_(std::move(backend)) {
}
void ScriptControlController::set_running(framework::Uuid agent, framework::Uuid object, framework::Uuid item,
                                          bool running) const {
    if (!backend_.can_edit(item, object, agent))
        return;
    auto local = backend_.part_local_id(object);
    if (!local)
        return;
    if (running)
        backend_.start(*local, item);
    else
        backend_.stop(*local, item);
}
void ScriptControlController::process_reset(framework::Uuid agent, framework::Uuid object,
                                            framework::Uuid item) const {
    auto local = backend_.part_local_id(object);
    if (!local)
        return;
    if (backend_.can_reset(object, item, agent))
        backend_.reset_event(*local, item);
}
void ScriptControlController::on_start(std::uint32_t, framework::Uuid item) const {
    auto instance = backend_.instance(item);
    if (instance) {
        instance->set_running(true);
        if (instance->set_item_running)
            instance->set_item_running(true);
    }
}
void ScriptControlController::on_stop(std::uint32_t, framework::Uuid item) const {
    auto instance = backend_.instance(item);
    if (instance) {
        instance->set_running(false);
        if (instance->set_item_running)
            instance->set_item_running(false);
    }
}
void ScriptControlController::reset_script(framework::Uuid item) const {
    auto instance = backend_.instance(item);
    if (instance) {
        if (backend_.release_urls)
            backend_.release_urls(item);
        instance->reset();
    }
}
void ScriptControlController::on_reset(std::uint32_t local, framework::Uuid item) const {
    if (backend_.trace_reset)
        backend_.trace_reset(local, item);
    reset_script(item);
}
void script_set_running(ScriptExecutionControl& control, bool running, const ScriptExecutionQueues& queues) {
    control.running = running;
    if (running) {
        if (control.state == ScriptExecutionState::Suspended && control.suspend_count == 0) {
            if (control.active_event) {
                control.state = ScriptExecutionState::OnYieldQueue;
                queues.queue_yield();
            } else if (control.pending_events) {
                control.state = ScriptExecutionState::OnStartQueue;
                queues.queue_start();
            } else
                control.state = ScriptExecutionState::Idle;
        }
    } else {
        if (control.state == ScriptExecutionState::OnSleepQueue) {
            queues.remove_sleep();
            control.state = ScriptExecutionState::Suspended;
        } else if (control.state == ScriptExecutionState::Idle)
            control.state = ScriptExecutionState::Suspended;
        queues.clear_events();
        control.pending_events = 0;
    }
}
StoppedScriptEvent stopped_script_event(const ScriptExecutionControl& control, bool known, bool handled,
                                        bool entry) {
    if (!known || !handled)
        return StoppedScriptEvent::Reject;
    if (control.state != ScriptExecutionState::Construct && !control.running) {
        if (control.state == ScriptExecutionState::Suspended && entry && !control.pending_events)
            return StoppedScriptEvent::QueueStateEntry;
        return StoppedScriptEvent::Ignore;
    }
    return StoppedScriptEvent::Continue;
}
}
