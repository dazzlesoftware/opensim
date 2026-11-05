// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include <functional>
namespace opensim::scene {
using ScriptTaskErrors = std::optional<std::vector<std::string>>;
struct ScriptTaskUpdateClient {
    framework::Uuid agent;
    std::function<void(const std::string&, bool)> alert;
};
// Accessors mutate the actual task inventory entry, not a detached copy.
struct ScriptTaskUpdateItem {
    framework::Uuid id;
    std::string name, description;
    std::function<void(bool)> set_running;
    std::function<void(framework::Uuid)> set_asset;
    std::function<framework::Uuid()> asset;
};
struct ScriptTaskUpdatePart {
    std::uint32_t local_id = 0;
    std::function<std::optional<ScriptTaskUpdateItem>(std::uint32_t, framework::Uuid)> get_item;
    std::function<void(framework::Uuid, bool)> remove_instance;
    std::function<void(const ScriptTaskUpdateItem&)> update_inventory;
    std::function<void()> invalidate_permissions;
    std::function<void(const ScriptTaskUpdateClient&)> send_properties;
    std::function<ScriptTaskErrors(framework::Uuid, int, bool, const std::string&, int)> compile;
    std::function<void()> resume_scripts;
};
struct ScriptTaskUpdateBackend {
    std::function<bool(framework::Uuid, framework::Uuid, framework::Uuid)> can_edit_script;
    std::function<std::optional<ScriptTaskUpdatePart>(framework::Uuid)> find_part;
    std::function<void(std::shared_ptr<framework::AssetBase>)> store;
    std::function<void(framework::Uuid, framework::Uuid, framework::Uuid, bool, framework::Uuid)> updated;
    std::function<framework::Uuid()> random_uuid = [] {
        return framework::Uuid::random();
    };
    std::function<std::optional<ScriptTaskUpdateClient>(framework::Uuid)> find_client;
    std::function<void(std::string)> log;
    std::string default_engine;
};
class ScriptTaskUpdateController {
  public:
    explicit ScriptTaskUpdateController(ScriptTaskUpdateBackend);
    // Exact Scene overload flow. Exceptions propagate; the upload updater catches
    // them. Null data becomes one zero byte; empty data stays empty. A null compiler
    // result remains null after update event/resume (the caps wrapper then faults).
    ScriptTaskErrors update(const ScriptTaskUpdateClient&, framework::Uuid item, framework::Uuid part,
                            bool running, framework::AssetBytes data) const;
    ScriptTaskErrors update(framework::Uuid avatar, framework::Uuid item, framework::Uuid part, bool running,
                            framework::AssetBytes data) const;

  private:
    ScriptTaskUpdateBackend backend_;
};
}
