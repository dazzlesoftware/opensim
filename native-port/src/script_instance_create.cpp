// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/script_instance_create.hpp"
#include "opensim/script/asset_text.hpp"
#include <stdexcept>
namespace opensim::scene {
ScriptInstanceCreateController::ScriptInstanceCreateController(ScriptInstanceCreateBackend backend)
    : backend_(std::move(backend)) {
    if (!backend_.can_run || !backend_.mark_scripted || !backend_.scripts_disabled ||
        !backend_.trust_binaries || !backend_.has_saved_state || !backend_.find_item ||
        !backend_.load_asset || !backend_.local_id || !backend_.trigger_rez || !backend_.store_errors ||
        !backend_.active_scripts || !backend_.schedule_full)
        throw std::invalid_argument("Incomplete script instance creation backend");
}
bool ScriptInstanceCreateController::create(std::shared_ptr<framework::TaskInventoryItem> item, int start,
                                            bool post, const std::string& engine, int state) const {
    if (!backend_.can_run(item)) {
        if (!item)
            throw std::invalid_argument("Null task inventory item");
        backend_.store_errors(item->item_id, std::vector<std::string>{"no permission"});
        return false;
    }
    backend_.mark_scripted();
    if (backend_.scripts_disabled())
        return false;
    if (!item)
        throw std::invalid_argument("Null task inventory item");
    auto key = item->item_id;
    auto found = backend_.find_item(item->item_id);
    if (!found) {
        backend_.store_errors(
            key, std::vector<std::string>{"TaskItem ID " + item->item_id.str() + " could not be found"});
        if (backend_.error)
            backend_.error("Could not start script: task inventory item not found");
        return false;
    }
    auto stored = *found;
    if (state == 2 && backend_.trust_binaries()) {
        if (!stored)
            throw std::invalid_argument("Null stored task inventory item");
        stored->permissions_mask = 0;
        stored->permissions_granter = {};
        backend_.trigger_rez(backend_.local_id(), key, "", start, post, engine, state);
        backend_.store_errors(key, {});
        backend_.active_scripts(1);
        backend_.schedule_full();
        return true;
    }
    auto asset = backend_.load_asset(item->asset_id);
    if (!asset) {
        backend_.store_errors(
            key, std::vector<std::string>{"asset ID " + item->asset_id.str() + " could not be found"});
        if (backend_.error)
            backend_.error("Could not start script: asset not found");
        return false;
    }
    if (backend_.has_saved_state()) {
        if (!backend_.restore_saved_state)
            throw std::invalid_argument("Missing saved script XML restoration provider");
        item->old_item_id = backend_.restore_saved_state(item->loaded_item_id, item->old_item_id, key);
    }
    if (!stored)
        throw std::invalid_argument("Null stored task inventory item");
    stored->old_item_id = item->old_item_id;
    stored->permissions_mask = 0;
    stored->permissions_granter = {};
    auto text = script::asset_text(asset->data);
    backend_.trigger_rez(backend_.local_id(), key, text, start, post, engine, state);
    backend_.store_errors(key, {});
    backend_.active_scripts(1);
    return true;
}
}
