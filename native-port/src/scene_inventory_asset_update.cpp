// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/scene_inventory_asset_update.hpp"
#include "opensim/framework/animation_set.hpp"
#include <bit>
#include <stdexcept>
namespace opensim::scene {
std::shared_ptr<framework::AssetBase>
create_scene_inventory_asset(const std::optional<std::string> &name,
                             const std::optional<std::string> &description, std::int8_t type,
                             framework::AssetBytes data, framework::Uuid creator) {
    auto asset = std::make_shared<framework::AssetBase>(framework::Uuid::random(), name, type, creator.str());
    asset->description(description);
    asset->data = data ? std::move(data) : std::make_shared<std::vector<std::uint8_t>>(1);
    return asset;
}
SceneInventoryAssetController::SceneInventoryAssetController(SceneInventoryAssetBackend b)
    : backend_(std::move(b)) {
    if (!backend_.presence || !backend_.inventory_access || !backend_.part || !backend_.edit_inventory ||
        !backend_.edit_notecard || !backend_.asset_permissions || !backend_.create_asset || !backend_.store)
        throw std::invalid_argument("Incomplete scene inventory asset backend");
}
framework::Uuid SceneInventoryAssetController::update(framework::Uuid avatar, framework::Uuid key,
                                                      framework::Uuid object,
                                                      framework::AssetBytes data) const {
    auto presence = backend_.presence(avatar);
    if (!presence) {
        if (backend_.error)
            backend_.error("[CapsUpdateItemAsset]: Avatar " + avatar.str() +
                           " cannot be found to update item asset");
        return {};
    }
    auto client = [&] {
        if (!*presence)
            throw std::invalid_argument("Null scene presence");
        return (*presence)->client();
    };
    if (object.is_zero()) {
        auto access = backend_.inventory_access();
        return access ? (*access)(client(), key, data) : framework::Uuid{};
    }
    auto part = backend_.part(object);
    if (!part || part->group_deleted()) {
        if (backend_.error)
            backend_.error("[CapsUpdateItemAsset]: Object " + object.str() +
                           " cannot be found to update item asset");
        return {};
    }
    auto item = part->item(key);
    if (!item) {
        if (backend_.error)
            backend_.error("[CapsUpdateItemAsset]: Could not find item " + key.str() + " for asset update");
        return {};
    }
    if (!backend_.edit_inventory(object, avatar))
        return {};
    auto alert = [&](const std::string &text, bool permission) {
        auto current = client();
        if (!current)
            throw std::invalid_argument("Null controlling client");
        if (permission)
            current->agent_alert(text, false);
        else
            current->alert(text);
    };
    switch (std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(item->inventory_type))) {
    case 7:
        if (!backend_.edit_notecard(key, object, avatar)) {
            alert("Insufficient permissions to edit notecard", true);
            return {};
        }
        alert("Notecard updated", false);
        break;
    case 96: {
        framework::AnimationSet animations(data, backend_.console);
        auto result = animations.validate([&](auto asset) {
            auto mask = backend_.asset_permissions(avatar, asset);
            return (mask & 0xa000) == 0xa000 ? static_cast<std::uint32_t>(mask) : 0u;
        });
        if (result == 0) {
            alert("Not enought permissions on asset(s) referenced by animation set '{0}', update failed",
                  true);
            return {};
        }
        break;
    }
    case 20:
        if (!(item->current_permissions & 0x4000)) {
            alert("Insufficient permissions to edit gesture", true);
            return {};
        }
        alert("Gesture updated", false);
        break;
    case 25:
        if (!(item->current_permissions & 0x4000)) {
            alert("Insufficient permissions to edit setting", true);
            return {};
        }
        alert("Setting updated", false);
        break;
    case 26:
        if (!(item->current_permissions & 0x4000)) {
            alert("Insufficient permissions to edit setting", true);
            return {};
        }
        break;
    default:
        break;
    }
    // Evaluate the source's value arguments before callbacks can mutate the item.
    auto name = item->name, description = item->description;
    auto type = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(item->type));
    auto asset = backend_.create_asset(name, description, type, data, avatar);
    if (!asset)
        throw std::invalid_argument("Null task asset");
    item->asset_id = asset->full_id();
    backend_.store(asset);
    part->update(item);
    return asset->full_id();
}
}
