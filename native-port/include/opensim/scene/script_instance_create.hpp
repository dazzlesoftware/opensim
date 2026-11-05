// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/task_inventory_item.hpp"
#include "opensim/framework/asset_base.hpp"
namespace opensim::scene {
struct ScriptInstanceCreateBackend {
    using Item = std::shared_ptr<framework::TaskInventoryItem>;
    std::function<bool(Item)> can_run;
    std::function<void()> mark_scripted;
    std::function<bool()> scripts_disabled, trust_binaries, has_saved_state;
    std::function<std::optional<Item>(framework::Uuid)> find_item;
    std::function<std::shared_ptr<framework::AssetBase>(framework::Uuid)> load_asset;
    std::function<framework::Uuid(framework::Uuid, framework::Uuid, framework::Uuid)> restore_saved_state;
    std::function<std::uint32_t()> local_id;
    std::function<void(std::uint32_t, framework::Uuid, const std::string&, int, bool, const std::string&,
                       int)>
        trigger_rez;
    // nullopt means source StoreScriptErrors(item,null): retrieve actual compiler
    // errors. Nonnull vector is source StoreScriptError's concrete diagnostics.
    std::function<void(framework::Uuid, std::optional<std::vector<std::string>>)> store_errors;
    std::function<void(int)> active_scripts;
    std::function<void()> schedule_full;
    std::function<void(std::string)> error;
};
class ScriptInstanceCreateController {
  public:
    explicit ScriptInstanceCreateController(ScriptInstanceCreateBackend);
    // Does not increment inventory serial or invent compiler success. A stopped
    // script still follows construction and adds +1 after real rez/error handling.
    bool create(std::shared_ptr<framework::TaskInventoryItem>, int start, bool post_on_rez,
                const std::string& engine, int state_source) const;

  private:
    ScriptInstanceCreateBackend backend_;
};
}
