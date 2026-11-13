/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/services/inventory_store.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
namespace opensim::services {
using framework::Uuid;
namespace {
InventoryFolder framework_folder(InventoryFolder value) {
    value.type = static_cast<std::int16_t>(static_cast<std::uint16_t>(value.type));
    value.version = static_cast<std::uint16_t>(value.version);
    return value;
}
std::string creator(const std::string &value) {
    const auto first = value.find(';');
    if (first == std::string::npos)
        return value;
    const auto second = value.find(';', first + 1);
    if (second == std::string::npos)
        return value + ";Unknown User";
    const auto third = value.find(';', second + 1);
    return value.substr(0, third);
}
InventoryItem framework_item(InventoryItem value) {
    value.sale_type = static_cast<std::uint8_t>(value.sale_type);
    value.group_owned = value.group_owned != 0 ? 1 : 0;
    value.creator_id = creator(value.creator_id);
    if (framework::trim_utf8(value.description).empty())
        value.description.clear();
    return value;
}
}
std::optional<InventoryFolder> InventoryService::folder(const Uuid &, const Uuid &id) {
    auto values = store_.folders({{"folderID", id.str()}});
    return values.empty() ? std::nullopt : std::optional{framework_folder(values.front())};
}
std::optional<InventoryItem> InventoryService::item(const Uuid &, const Uuid &id) {
    auto values = store_.items({{"inventoryID", id.str()}});
    return values.empty() ? std::nullopt : std::optional{framework_item(values.front())};
}
std::vector<InventoryItem> InventoryService::folder_items(const Uuid &, const Uuid &id) {
    auto values = store_.items({{"parentFolderID", id.str()}});
    for (auto &value : values)
        value = framework_item(std::move(value));
    return values;
}
InventoryCollection InventoryService::content(const Uuid &principal, const Uuid &id) {
    InventoryCollection result;
    result.owner = principal;
    result.folder = id;
    result.folders = store_.folders({{"parentFolderID", id.str()}});
    for (auto &value : result.folders)
        value = framework_folder(std::move(value));
    result.items = folder_items(principal, id);
    if (const auto parent = folder(principal, id)) {
        result.owner = parent->owner;
        result.version = parent->version;
    }
    return result;
}
std::vector<InventoryCollection> InventoryService::multiple_content(const Uuid &principal,
                                                                    const std::vector<Uuid> &ids) {
    std::vector<InventoryCollection> result;
    for (const auto &id : ids)
        result.push_back(content(principal, id));
    return result;
}
std::vector<std::optional<InventoryItem>> InventoryService::multiple_items(const Uuid &principal,
                                                                           const std::vector<Uuid> &ids) {
    std::vector<std::optional<InventoryItem>> result;
    for (const auto &id : ids)
        result.push_back(item(principal, id));
    return result;
}
bool InventoryService::add_folder(InventoryFolder value) {
    value = framework_folder(std::move(value));
    if (folder(value.owner, value.id))
        return false;
    if (value.type != -1) {
        auto root_folder = root(value.owner);
        if (!root_folder)
            return false;
        if (value.parent == root_folder->id && for_type(value.owner, value.type))
            return false;
    }
    return store_.store(value);
}
bool InventoryService::update_folder(InventoryFolder value) {
    value = framework_folder(std::move(value));
    auto existing = folder(value.owner, value.id);
    if (!existing)
        return add_folder(std::move(value));
    if ((existing->type != -1 || value.type != -1) && (existing->type != 47 || value.type != 47)) {
        if (value.version < existing->version)
            return false;
        existing->version = value.version;
        return store_.store(*existing);
    }
    value.version = std::max(value.version, existing->version);
    value.id = existing->id;
    return store_.store(value);
}
bool InventoryService::move_folder(const InventoryFolder &value) {
    return store_.move(value.id, value.parent);
}
bool InventoryService::add_item(const InventoryItem &value) {
    auto copy = framework_item(value);
    return store_.store(copy);
}
bool InventoryService::update_item(InventoryItem &value) {
    if (!allow_delete_ && (value.asset_type == 24 || value.asset_type == 25))
        return false;
    const auto existing = item(value.owner, value.id);
    if (!existing)
        return false;
    if (existing->inventory_type != value.inventory_type || existing->asset_type != value.asset_type ||
        existing->parent != value.parent || existing->creator_id != value.creator_id ||
        existing->owner != value.owner) {
        value.inventory_type = existing->inventory_type;
        value.asset_type = existing->asset_type;
        value.parent = existing->parent;
        // CreatorIdentification's setter preserves the current creator ID
        // for an empty assignment and preserves creator data for a plain ID.
        const auto caller_data = value.creator_id.find(';');
        if (existing->creator_id.empty())
            value.creator_id = value.creator_id.substr(0, caller_data);
        else if (existing->creator_id.find(';') == std::string::npos && caller_data != std::string::npos)
            value.creator_id = existing->creator_id + value.creator_id.substr(caller_data);
        else
            value.creator_id = existing->creator_id;
        value.owner = existing->owner;
    }
    auto copy = framework_item(value);
    return store_.store(copy);
}
bool InventoryService::move_items(const Uuid &, const std::vector<InventoryItem> &values) {
    for (const auto &value : values)
        store_.move_item(value.id, value.parent);
    return true;
}
bool InventoryService::delete_items(const Uuid &, const std::vector<Uuid> &ids) {
    for (const auto &id : ids) {
        if (allow_delete_)
            store_.erase_items({{"inventoryID", id.str()}});
        else if (!store_.erase_items({{"inventoryID", id.str()}, {"assetType", "24"}}))
            store_.erase_items({{"inventoryID", id.str()}, {"assetType", "25"}});
    }
    return true;
}
bool InventoryService::parent_is_trash_or_lost(const Uuid &id) {
    auto values = store_.folders({{"folderID", id.str()}});
    if (values.empty())
        return false;
    const auto initial = values.front();
    if (initial.type == 14 || initial.type == 16)
        return true;
    auto parent = initial.parent;
    std::unordered_set<Uuid, framework::UuidHash> visited{id};
    while (!parent.is_zero()) {
        if (!visited.insert(parent).second)
            throw std::runtime_error("Cycle in inventory folder ancestry");
        auto ancestors = store_.folders({{"folderID", parent.str()}});
        if (ancestors.empty())
            return false;
        const auto &ancestor = ancestors.front();
        // Preserve the baseline's original-folder check for Lost And Found.
        if (ancestor.type == 14 || initial.type == 16)
            return true;
        if (ancestor.type == 8)
            return false;
        parent = ancestor.parent;
    }
    return false;
}
bool InventoryService::purge_folder(const Uuid &id, bool only_if_trash) {
    if (!allow_delete_ || (only_if_trash && !parent_is_trash_or_lost(id)))
        return false;
    std::unordered_set<Uuid, framework::UuidHash> active;
    const auto purge = [&](auto &&recurse, const Uuid &current) -> void {
        if (!active.insert(current).second)
            throw std::runtime_error("Cycle in inventory purge");
        if (!only_if_trash || parent_is_trash_or_lost(current)) {
            for (const auto &child : store_.folders({{"parentFolderID", current.str()}})) {
                recurse(recurse, child.id);
                store_.erase({{"folderID", child.id.str()}});
            }
            store_.erase_items({{"parentFolderID", current.str()}});
        }
        active.erase(current);
    };
    purge(purge, id);
    return true;
}
bool InventoryService::delete_folders(const Uuid &, const std::vector<Uuid> &ids, bool only_if_trash) {
    if (!allow_delete_)
        return false;
    for (const auto &id : ids) {
        if (only_if_trash && !parent_is_trash_or_lost(id))
            continue;
        purge_folder(id, only_if_trash);
        store_.erase({{"folderID", id.str()}});
    }
    return true;
}
std::vector<InventoryItem> InventoryService::active_gestures(const Uuid &principal) {
    auto values = store_.active_gestures(principal);
    for (auto &value : values)
        value = framework_item(std::move(value));
    return values;
}
int InventoryService::asset_permissions(const Uuid &principal, const Uuid &asset) {
    return store_.asset_permissions(principal, asset);
}
}
