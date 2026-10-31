// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/script_task_caps.hpp"
#include "opensim/services/scene_capability_events.hpp"
namespace opensim::services {
struct RegionScriptTaskBackend {
 std::function<bool(framework::Uuid)> object_exists;
 std::function<bool(framework::Uuid,framework::Uuid)> can_edit_inventory;
 std::function<bool(framework::Uuid,framework::Uuid,framework::Uuid)> can_edit_script;
 std::function<std::vector<std::string>(framework::Uuid,framework::Uuid,framework::Uuid,bool,std::span<const std::uint8_t>)> update;
 std::function<void(std::string)> log;
};
// Registers the two source aliases on each Caps, plus real one-shot binary
// upload routes and 30-second expiration. Quiesce HTTP before stop; Caps owns
// initial routes. Backend scene/asset/compiler dependencies must outlive stop.
class RegionScriptTaskRuntime {
public:
 RegionScriptTaskRuntime(std::shared_ptr<SceneCapabilityEvents>,RegionScriptTaskBackend);
 ~RegionScriptTaskRuntime();
 RegionScriptTaskRuntime(const RegionScriptTaskRuntime&)=delete;
 RegionScriptTaskRuntime& operator=(const RegionScriptTaskRuntime&)=delete;
 void stop();std::size_t pending_uploads()const;
private:struct State;std::shared_ptr<State> state_;
};
}
