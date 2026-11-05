// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/lludp.hpp"
#include <functional>
#include <optional>
namespace opensim::scene {
struct ScriptRunningInstance {
    std::function<bool()> running;
    std::function<void(bool)> set_item_running;
};
struct ScriptRunningBackend {
    std::function<std::optional<ScriptRunningInstance>(framework::Uuid)> find_instance;
    std::function<void(framework::Uuid, framework::Uuid, framework::Uuid, bool)> reply;
    std::function<void(framework::Uuid, framework::Uuid)> trace;
};
class GetScriptRunningController {
  public:
    explicit GetScriptRunningController(ScriptRunningBackend);
    // Original YEngine looks up only itemID, echoes objectID, and sends nothing
    // for absent instances. Host handles authenticated circuit/session ownership.
    bool request(framework::Uuid agent, framework::Uuid object, framework::Uuid item) const;

  private:
    ScriptRunningBackend backend_;
};
// LLClientView's fallback ONLY when no event queue module is present. Normal
// native hosts use EventQueueModule::script_running (whose Mono field is true).
network::lludp::Packet script_running_reply_packet(framework::Uuid object, framework::Uuid item,
                                                   bool running);
}
