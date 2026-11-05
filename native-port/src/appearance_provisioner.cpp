// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/appearance_provisioner.hpp"
#include "opensim/framework/permissions.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <array>
#include <ctime>
#include <limits>
namespace opensim::services {
namespace {
framework::Uuid folder_id(const std::shared_ptr<InventoryFolder>& folder) {
    if (!folder)
        throw std::invalid_argument("Missing appearance inventory folder");
    return folder->id;
}
std::int32_t now() {
    auto seconds = static_cast<double>(std::time(nullptr));
    return seconds >= 2147483648. || seconds < -2147483648. ? std::numeric_limits<std::int32_t>::min()
                                                            : static_cast<std::int32_t>(seconds);
}
}
AppearanceProvisioner::AppearanceProvisioner(AppearanceProvisioning operations, Random random, Clock clock)
    : operations_(std::move(operations)),
      random_(random ? std::move(random) : Random{[] { return framework::Uuid::random(); }}),
      clock_(clock ? std::move(clock) : Clock{now}) {
}
std::shared_ptr<InventoryItem> AppearanceProvisioner::new_item(const framework::Uuid& principal) {
    auto id = random_();
    auto item = std::make_shared<InventoryItem>();
    item->creation_date = clock_();
    item->id = id;
    item->owner = principal;
    return item;
}
void AppearanceProvisioner::create_outfit_link(int inventory_type, std::uint32_t flags, std::string name,
                                               framework::Uuid id, framework::Uuid principal,
                                               framework::Uuid folder) {
    auto item = new_item(principal);
    item->asset_id = id;
    item->asset_type = 24;
    item->creator_id = principal.str();
    item->inventory_type = inventory_type;
    item->description = "";
    item->parent = folder;
    item->flags = std::bit_cast<std::int32_t>(flags);
    item->name = name;
    item->base_permissions = item->current_permissions = item->everyone_permissions =
        item->group_permissions = item->next_permissions =
            static_cast<int>(framework::bits(framework::PermissionMask::Copy));
    operations_.add_item(item);
}
void AppearanceProvisioner::create_defaults(framework::Uuid principal) {
    using Wearable = framework::AvatarWearable;
    auto body = operations_.folder(principal, 13);
    auto outfit = operations_.folder(principal, 46);
    std::shared_ptr<InventoryFolder> clothing;
    struct Entry {
        const char* name;
        const framework::Uuid* asset;
        int type, wearable;
    };
    const Entry entries[] = {{"Default Eyes", &Wearable::default_eyes_asset, 13, Wearable::Eyes},
                             {"Default Shape", &Wearable::default_body_asset, 13, Wearable::Body},
                             {"Default Skin", &Wearable::default_skin_asset, 13, Wearable::Skin},
                             {"Default Hair", &Wearable::default_hair_asset, 13, Wearable::Hair},
                             {"Default Shirt", &Wearable::default_shirt_asset, 5, Wearable::Shirt},
                             {"Default Pants", &Wearable::default_pants_asset, 5, Wearable::Pants}};
    std::vector<std::shared_ptr<InventoryItem>> items;
    for (const auto& entry : entries) {
        if (entry.wearable == Wearable::Shirt)
            clothing = operations_.folder(principal, 5);
        auto item = new_item(principal);
        item->asset_id = *entry.asset;
        item->name = entry.name;
        item->creator_id = principal.str();
        item->asset_type = entry.type;
        item->inventory_type = 18;
        item->parent = folder_id(entry.type == 13 ? body : clothing);
        item->base_permissions = item->current_permissions = item->everyone_permissions =
            item->group_permissions = item->next_permissions =
                static_cast<int>(framework::bits(framework::PermissionMask::All));
        item->flags = entry.wearable;
        operations_.add_item(item);
        create_outfit_link(18, entry.wearable, item->name, item->id, principal, folder_id(outfit));
        items.push_back(item);
    }
    if (operations_.set_appearance) {
        std::array<std::shared_ptr<Wearable>, 6> wearables;
        for (std::size_t index = 0; index < items.size(); ++index)
            wearables[entries[index].wearable] =
                std::make_shared<Wearable>(items[index]->id, items[index]->asset_id);
        auto appearance = std::make_shared<framework::AvatarAppearance>();
        for (int index = 0; index < 6; ++index)
            appearance->set_wearable(index, wearables[index]);
        operations_.set_appearance(principal, std::move(appearance));
    }
}
void AppearanceProvisioner::establish(framework::Uuid principal, std::string model) {
    auto text = framework::utf16_replacing(model);
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (framework::unicode_whitespace(text[i])) {
            parts.push_back(framework::utf8(std::u16string_view(text).substr(start, i - start)));
            start = i + 1;
        }
    parts.push_back(framework::utf8(std::u16string_view(text).substr(start)));
    if (parts.size() != 2) {
        create_defaults(principal);
        return;
    }
    auto model_account = operations_.by_name({}, parts[0], parts[1]);
    if (!model_account) {
        create_defaults(principal);
        return;
    }
    auto appearance = operations_.get_appearance(framework::Uuid(model_account->principal_id));
    if (!appearance) {
        create_defaults(principal);
        return;
    }
    try {
        if (operations_.copy_model)
            operations_.copy_model(principal, framework::Uuid(model_account->principal_id), appearance);
        else
            copy_model(principal, framework::Uuid(model_account->principal_id), appearance);
        operations_.set_appearance(principal, appearance);
    } catch (...) { /* The source logs and swallows transfer/set failures. */
    }
}
}
